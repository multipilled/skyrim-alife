#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Rep.h"
#include "ALife/Systems.h"

// Bringing offline squads into the world: a squad whose simulated position is near the player comes online as pool
// actors placed out of view on the navmesh, is driven with linked-ref AI packages toward the smart terrain the board
// assigned it (reach) or sandboxes around it (stay), and goes offline again when the player leaves.
namespace ALife::Online {
namespace {
	// A group comes online when its sim position is inside the pick radius (alife.ltx [online] switch_distance, Anomaly's ~250 m);
	// it is placed 4.5-8.5k from the player whatever the radius (Candidates), so a wider pick pulls squads in from further away.
	float PickRadius() { return Sim::Cfg().switchDistance; }
	// Hysteresis: a group dropped for distance comes back only once the player is kRejoinMargin closer to it than at the drop,
	// so a squad the player walked away from is not pulled back in front of them again and again.
	// The rule lapses after kRejoinForget real seconds (bounded memory; a squad may wander back into view later).
	constexpr float kRejoinMargin = 3000.f;
	constexpr float kRejoinForget = 600.f;
	struct Dropped { float d; float at; };
	std::unordered_map<std::uint64_t, Dropped> gDroppedAt;   // group id -> distance and clock when it went offline for distance
	// Spawns are capped at kSpawnMaxDist, and a group only goes offline when its leader is beyond OfflineDist(), or beyond
	// kOfflineUnloadedDist with no 3D / an unattached cell (walked out of the loaded area), never inside the first kOnlineGrace seconds.
	constexpr float kSpawnMaxDist = 9000.f;
	float OfflineDist() { return Sim::Cfg().offlineDistance; }   // alife.ltx [online] offline_distance (11000)
	constexpr float kOfflineUnloadedDist = 8000.f;
	constexpr float kOnlineGrace = 5.f;       // real (unpaused) seconds after BringOnline
	constexpr float kVerifyDelay = 2.f;       // per-member world-state check after BringOnline
	constexpr float kViewHalfCone = 55.f;     // 110 degree view cone
	constexpr float kReDriveSeconds = 6.f;    // refresh the waypoint of a walking squad this often
	std::array<std::uint64_t, kMaxOnlineGroups> gSlotOwner{};

	float Dist2D(const RE::NiPoint3& a, float x, float y)
	{
		float dx = a.x - x, dy = a.y - y;
		return std::sqrt(dx * dx + dy * dy);
	}

	int FreeSlot()
	{
		for (int i = 0; i < kMaxOnlineGroups; ++i)
			if (!gSlotOwner[i] && Forms::R().slotMarkers[i] && Forms::R().fSlot[i]) return i;
		return -1;
	}

	// A point counts as visible if it lies inside ANY of three horizontal cones: the body heading (110 deg), the camera's
	// view direction from the NiCamera transform, and the camera yaw - the camera cones sized from the real worldFOV with
	// widescreen Hor+ headroom. Every such point is rejected at any distance, so nothing can pop in on screen.
	float CameraHalfAngle()
	{
		float fov = 70.f;
		if (auto* cam = RE::PlayerCamera::GetSingleton()) {
			const float f = cam->GetRuntimeData2().worldFOV;
			if (f > 20.f && f < 170.f) fov = f;
		}
		const float half = std::atan(std::tan(fov * 0.5f * 0.0174532925f) * 2.0f) * 57.2957795f + 10.f;
		return std::max(kViewHalfCone, half);
	}

	bool InCone(const RE::NiPoint3& a_origin, float fx, float fy, const RE::NiPoint3& p, float a_halfDeg)
	{
		const float dx = p.x - a_origin.x, dy = p.y - a_origin.y;
		const float l = std::sqrt(dx * dx + dy * dy);
		if (l < 1.f) return true;
		const float c = std::clamp((dx * fx + dy * fy) / l, -1.f, 1.f);
		return std::acos(c) * 57.2957795f < a_halfDeg;
	}

	bool InViewCone(RE::PlayerCharacter* pc, const RE::NiPoint3& a_p)
	{
		if (pc->GetHeadingAngle(a_p, true) < kViewHalfCone) return true;   // body heading, 110 deg
		const RE::NiPoint3 head{ a_p.x, a_p.y, a_p.z + 120.f };
		auto* pcam = RE::PlayerCamera::GetSingleton();
		RE::NiPoint3 cpos = pc->GetPosition();
		cpos.z += 120.f;
		if (pcam && pcam->cameraRoot) cpos = RE::PlayerCamera::GetActiveCameraPosition();
		const float half = CameraHalfAngle();
		if (auto* nc = RE::Main::WorldRootCamera()) {
			const auto& m = nc->world.rotate;
			float fx = m.entry[0][0], fy = m.entry[1][0];   // NiCamera looks down its local +X (rotate column 0)
			const float l = std::sqrt(fx * fx + fy * fy);
			if (l > 0.1f && InCone(cpos, fx / l, fy / l, head, half)) return true;
		}
		if (auto* cam = pcam) {
			const float y = cam->GetRuntimeData2().yaw;
			if (InCone(cpos, std::sin(y), std::cos(y), head, half)) return true;
		}
		return false;
	}

	bool OnPredictedPath(RE::PlayerCharacter* pc, const RE::NiPoint3& p)
	{
		bool fast = pc->IsOnMount() || (pc->AsActorState() && pc->AsActorState()->IsSprinting());
		if (!fast) return false;
		RE::NiPoint3 v;
		pc->GetLinearVelocity(v);
		float speed = std::sqrt(v.x * v.x + v.y * v.y);
		if (speed < 50.f) return false;
		RE::NiPoint3 a = pc->GetPosition();
		RE::NiPoint3 b{ a.x + v.x * 12.f, a.y + v.y * 12.f, a.z };
		float abx = b.x - a.x, aby = b.y - a.y;
		float t = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / (abx * abx + aby * aby), 0.f, 1.f);
		float cx = a.x + abx * t, cy = a.y + aby * t;
		float dx = p.x - cx, dy = p.y - cy;
		return dx * dx + dy * dy < 3000.f * 3000.f;
	}

	void SetMode(RE::Actor* a, int rank)
	{
		if (Forms::R().fMode) a->AddToFaction(Forms::R().fMode, std::int8_t(rank));
	}

	// ---------------------------------------------------------------- placement
	// Points are snapped to the nearest navmesh vertex across the *loaded grid cells around that point* (a ref's own parent
	// cell is not trusted), validated, and the marker is moved straight into the cell that owns the vertex.
	struct Snap
	{
		RE::NiPoint3 pos;
		RE::TESObjectCELL* cell{ nullptr };
	};
	std::array<Snap, kMaxOnlineGroups> gPlaced{};

	// Same engine call CommonLib's (private) TESObjectREFR::MoveTo_Impl uses: relocate a ref to an explicit cell + worldspace + position.
	void MoveRefTo(RE::TESObjectREFR* a_ref, RE::TESObjectCELL* a_cell, RE::TESWorldSpace* a_ws, const RE::NiPoint3& a_pos)
	{
		using func_t = void(RE::TESObjectREFR*, const RE::ObjectRefHandle&, RE::TESObjectCELL*, RE::TESWorldSpace*, const RE::NiPoint3&, const RE::NiPoint3&);
		static REL::Relocation<func_t> func{ RELOCATION_ID(56227, 56626) };
		const auto handle = a_ref->CreateRefHandle();
		const auto angle = a_ref->GetAngle();
		func(a_ref, handle, a_cell, a_ws, a_pos, angle);
	}

	bool SnapToNavmesh(const RE::NiPoint3& p, float a_radius, Snap& out)
	{
		auto* tes = RE::TES::GetSingleton();
		if (!tes || tes->interiorCell || !tes->gridCells) return false;
		const auto len = tes->gridCells->length;
		float best = a_radius * a_radius;
		bool found = false;
		for (std::uint32_t x = 0; x < len; ++x) {
			for (std::uint32_t y = 0; y < len; ++y) {
				auto* cell = tes->gridCells->GetCell(x, y);
				if (!cell || !cell->IsAttached()) continue;
				auto* c = cell->GetCoordinates();
				if (!c) continue;
				if (p.x + a_radius < c->worldX || p.x - a_radius > c->worldX + 4096.f ||
					p.y + a_radius < c->worldY || p.y - a_radius > c->worldY + 4096.f) continue;
				auto* nm = cell->GetRuntimeData().navMeshes;
				if (!nm) continue;
				for (auto& navMesh : nm->navMeshes) {
					if (!navMesh) continue;
					for (auto& v : navMesh->vertices) {
						const float dx = v.location.x - p.x, dy = v.location.y - p.y, dz = v.location.z - p.z;
						const float d = dx * dx + dy * dy + 0.25f * dz * dz;   // prefer same height band
						if (d < best) {
							best = d;
							out.pos = v.location;
							out.cell = cell;
							found = true;
						}
					}
				}
			}
		}
		return found;
	}

	bool SpawnRulesOk(Group& g, RE::PlayerCharacter* pc, const RE::NiPoint3& p, bool forceNear, const char*& why)
	{
		const float d = Dist2D(pc->GetPosition(), p.x, p.y);
		if (d < kPopInMinDist) { why = "tooClose"; return false; }
		if (d > kSpawnMaxDist) { why = "tooFar"; return false; }
		if (!forceNear && Graph::InCityBuffer(p.x, p.y, g.ws) && (Rep::IsOutlaw(g.faction) || Rep::RegardOf(g) == Regard::Hostile)) { why = "cityBuffer"; return false; }
		if (!forceNear && OnPredictedPath(pc, p)) { why = "predictedPath"; return false; }
		if (InViewCone(pc, p)) { why = "inView"; return false; }
		return true;
	}

	void Candidates(Group& g, RE::PlayerCharacter* pc, bool forceNear, std::vector<RE::NiPoint3>& out)
	{
		const RE::NiPoint3 pp = pc->GetPosition();
		Rng r(Mix64(g.id ^ std::uint64_t(Sim::Hours() * 100)));
		const float yaw = pc->GetAngleZ();   // radians; facing vector is (sin yaw, cos yaw)
		auto add = [&](float ang, float rad) { out.push_back({ pp.x + std::sin(ang) * rad, pp.y + std::cos(ang) * rad, pp.z }); };
		out.push_back({ g.x, g.y, g.z });   // the sim position itself
		if (!forceNear) {
			const float base = std::atan2(g.x - pp.x, g.y - pp.y);   // approach side first: the bearing the group is coming in from
			for (int i = 0; i < 12; ++i) add(base + (r.frac() - 0.5f) * 2.4f, kSpawnPreferMin + r.frac() * (kSpawnPreferMax - kSpawnPreferMin));
		}
		const float behind = yaw + 3.14159265f;   // behind the player (outside the 110 degree cone), 4.5k-8.5k
		for (int i = 0; i < 14; ++i) add(behind + (r.frac() - 0.5f) * 2.0f, 4500.f + r.frac() * 4000.f);
		for (int i = 0; i < 12; ++i) add(r.frac() * 6.2831853f, 4500.f + r.frac() * 4500.f);   // full ring as a last resort
		if (auto* tes = RE::TES::GetSingleton()) {
			for (auto& p : out) {
				float h = 0;
				if (tes->GetLandHeight(p, h)) p.z = h;
			}
		}
	}

	// Places slot marker 'slot' at a valid navmesh point. Returns false (marker parked) when nothing fits.
	bool PlaceGroupMarker(int slot, Group& g, RE::PlayerCharacter* pc, bool forceNear, std::string& a_why)
	{
		auto* mk = Forms::R().slotMarkers[slot];
		auto* ws = pc->GetWorldspace();
		if (!mk || !ws || Forms::WsIndex(ws) != int(g.ws)) { a_why = "notInTheSquadsWorld"; return false; }
		gPlaced[slot] = Snap{};
		std::vector<RE::NiPoint3> cands;
		Candidates(g, pc, forceNear, cands);
		std::map<std::string, int> fails;
		bool movedIntoWorld = false;
		for (auto& c : cands) {
			const char* why = "";
			Snap sn;
			if (!SnapToNavmesh(c, 1500.f, sn)) { ++fails["noNavmesh"]; continue; }
			if (!SpawnRulesOk(g, pc, sn.pos, forceNear, why)) { ++fails[why]; continue; }
			if (!movedIntoWorld) { mk->MoveTo(pc); movedIntoWorld = true; }   // out of the holding cell, into Tamriel
			MoveRefTo(mk, sn.cell, ws, sn.pos);
			const auto fp = mk->GetPosition();
			if (Dist2D(pc->GetPosition(), fp.x, fp.y) < kPopInMinDist) { ++fails["movedClose"]; continue; }
			if (InViewCone(pc, fp)) { ++fails["inViewAfterMove"]; continue; }
			gPlaced[slot] = sn;
			return true;
		}
		if (forceNear && !cands.empty()) {
			Snap sn;
			if (SnapToNavmesh(cands.front(), 4000.f, sn) && Dist2D(pc->GetPosition(), sn.pos.x, sn.pos.y) >= kPopInMinDist && !InViewCone(pc, sn.pos)) {
				if (!movedIntoWorld) mk->MoveTo(pc);
				MoveRefTo(mk, sn.cell, ws, sn.pos);
				if (!InViewCone(pc, mk->GetPosition())) {
					gPlaced[slot] = sn;
					Log::Line("[online] forceNear wide-radius placement used");
					return true;
				}
				++fails["forceNearInView"];
			}
		}
		std::string sum;
		for (auto& [k, v] : fails) sum += (sum.empty() ? "" : ",") + k + "=" + std::to_string(v);
		a_why = sum.empty() ? "noCandidates" : sum;
		if (Forms::R().holdingMarker) mk->MoveTo(Forms::R().holdingMarker);
		return false;
	}

	// ---------------------------------------------------------------- driving a squad toward its board target
	// A waypoint on the navmesh toward the target (the whole way when it is close, otherwise ~5500 units along the bearing,
	// which is always inside the loaded grid). The group's slot marker is the waypoint; null when nothing is loaded there.
	RE::TESObjectREFR* Waypoint(Group& g, RE::Actor* leader, const GraphNode& t)
	{
		if (g.slot < 0) return nullptr;
		auto* mk = Forms::R().slotMarkers[g.slot];
		if (!mk) return nullptr;
		const auto lp = leader->GetPosition();
		float dx = t.x - lp.x, dy = t.y - lp.y;
		const float d = std::sqrt(dx * dx + dy * dy);
		RE::NiPoint3 p{ t.x, t.y, lp.z };
		if (d > 6000.f) p = { lp.x + dx / d * 5500.f, lp.y + dy / d * 5500.f, lp.z };
		if (auto* tes = RE::TES::GetSingleton()) {
			float h = 0;
			if (tes->GetLandHeight(p, h)) p.z = h;
		}
		Snap sn;
		if (!SnapToNavmesh(p, 2500.f, sn)) return nullptr;
		MoveRefTo(mk, sn.cell, Forms::WsObj(g.ws), sn.pos);
		return mk;
	}

	struct Driven
	{
		std::int64_t target{ -2 };
		Action action{ Action::None };
		float at{ -100.f };
	};
	std::unordered_map<std::uint64_t, Driven> gDriven;
	float gClock{ 0 };

	// Leader drives the group (travel to the waypoint, or sandbox around the place when staying); members follow the leader.
	void DriveOnline(Group& g)
	{
		const auto& r = Forms::R();
		const GraphNode* tgt = g.assignedTarget >= 0 ? Graph::Node(std::uint32_t(g.assignedTarget)) : nullptr;
		const bool staying = !tgt || g.action != Action::Reach;
		RE::Actor* leader = nullptr;
		for (auto& m : g.members) {
			auto* a = Pool::ActorOf(m);
			if (!a || a->IsDead()) continue;
			if (!leader) {
				leader = a;
				RE::TESObjectREFR* anchor = nullptr;
				int mode = 2;
				if (!staying) {
					anchor = Waypoint(g, a, *tgt);
					mode = anchor ? 1 : 2;
				}
				if (!anchor) {   // camp around the place's own marker when we are at it, otherwise around the leader
					auto* nm = tgt ? Graph::MarkerOf(*tgt) : nullptr;
					if (nm && Dist2D(a->GetPosition(), nm->GetPositionX(), nm->GetPositionY()) < 3000.f) anchor = nm;
					else {
						anchor = (g.slot >= 0) ? r.slotMarkers[g.slot] : nullptr;
						if (anchor) anchor->MoveTo(a);
					}
					mode = 2;
				}
				a->extraList.SetLinkedRef(anchor, r.kwTravel);
				SetMode(a, mode);
			} else {
				a->extraList.SetLinkedRef(leader, r.kwLeader);
				SetMode(a, 3);
			}
			a->EvaluatePackage(true, false);
		}
		auto& d = gDriven[g.id];
		d.target = g.assignedTarget;
		d.action = g.action;
		d.at = gClock;
	}

	// ---------------------------------------------------------------- life at a place
	// A squad idling at a place does not stand on one spot: it walks legs between navmesh waypoints around it and pauses at each
	// (sandboxing: sitting, leaning, idles), so camps, forts and farms look inhabited. Military squads patrol the perimeter,
	// camps and farms stay tighter and rest longer.
	struct Stay
	{
		bool init{ false };
		std::int64_t target{ -2 };
		bool walking{ false };
		float until{ 0 };
		float started{ 0 };
		std::size_t idx{ 0 };
		std::vector<Snap> pts;
	};
	std::unordered_map<std::uint64_t, Stay> gStay;

	struct PatrolShape { float rmin, rmax, pauseMin, pauseMax; int points; };
	PatrolShape ShapeFor(const GraphNode* n, Faction f)
	{
		if (!n) return { 500.f, 1500.f, 20.f, 50.f, 3 };
		switch (n->kind) {
		case Kind::Fort: case Kind::MilitaryCamp: return { 800.f, 2600.f, 8.f, 26.f, 4 };
		case Kind::BanditCamp: case Kind::ForswornCamp: return { 400.f, 1500.f, 18.f, 48.f, 3 };
		case Kind::Town: case Kind::Inn: case Kind::Farm: case Kind::Mill: return { 400.f, 1300.f, 22.f, 55.f, 3 };
		default: break;
		}
		const bool soldiers = f == Faction::Imperial || f == Faction::Stormcloak || f == Faction::Thalmor;
		return soldiers ? PatrolShape{ 700.f, 2200.f, 10.f, 30.f, 4 } : PatrolShape{ 600.f, 1800.f, 20.f, 55.f, 3 };
	}

	std::vector<Snap> PatrolPoints(const GraphNode* node, RE::Actor* lead, Faction f, Rng& r)
	{
		RE::NiPoint3 c = lead->GetPosition();
		if (node)
			if (auto* nm = Graph::MarkerOf(*node); nm && Dist2D(c, nm->GetPositionX(), nm->GetPositionY()) < 3000.f) c = nm->GetPosition();
		const auto shape = ShapeFor(node, f);
		std::vector<Snap> pts;
		const float a0 = r.frac() * 6.2831853f;
		for (int i = 0; i < shape.points; ++i) {
			const float ang = a0 + float(i) * (6.2831853f / float(shape.points)) + (r.frac() - 0.5f) * 0.6f;
			const float rad = r.uniform(shape.rmin, shape.rmax);
			RE::NiPoint3 p{ c.x + std::sin(ang) * rad, c.y + std::cos(ang) * rad, c.z };
			if (auto* tes = RE::TES::GetSingleton()) {
				float h = 0;
				if (tes->GetLandHeight(p, h)) p.z = h;
			}
			Snap sn;
			if (SnapToNavmesh(p, 1200.f, sn)) pts.push_back(sn);
		}
		return pts;
	}

	void StayDirector(Group& g, RE::Actor* lead, bool changed)
	{
		const auto& r = Forms::R();
		if (g.slot < 0 || !r.slotMarkers[g.slot] || !lead) return;
		for (auto& m : g.members)
			if (auto* a = Pool::ActorOf(m); a && !a->IsDead() && a->IsInCombat()) return;   // a fight is not interrupted
		auto& st = gStay[g.id];
		auto* mk = r.slotMarkers[g.slot];
		Rng rng(Mix64(g.id ^ std::uint64_t(gClock * 10.f)));
		const GraphNode* node = g.assignedTarget >= 0 ? Graph::Node(std::uint32_t(g.assignedTarget)) : nullptr;
		const auto shape = ShapeFor(node, g.faction);
		auto pause = [&] {
			st.walking = false;
			st.until = gClock + rng.uniform(shape.pauseMin, shape.pauseMax);
			mk->MoveTo(lead);   // sandbox around where the leader stands
			lead->extraList.SetLinkedRef(mk, r.kwTravel);
			SetMode(lead, 2);
			lead->EvaluatePackage(true, false);
		};
		if (!st.init || st.target != g.assignedTarget || changed) {
			st = Stay{};
			st.init = true;
			st.target = g.assignedTarget;
			st.pts = PatrolPoints(node, lead, g.faction, rng);
			st.until = gClock + rng.uniform(4.f, 14.f);   // look around first, then start moving
			return;
		}
		if (st.walking) {
			const auto mp = mk->GetPosition();
			if (Dist2D(lead->GetPosition(), mp.x, mp.y) < 380.f || gClock - st.started > 50.f) pause();
		} else if (gClock >= st.until && !st.pts.empty()) {
			const Snap& sn = st.pts[st.idx++ % st.pts.size()];
			MoveRefTo(mk, sn.cell, Forms::WsObj(g.ws), sn.pos);
			lead->extraList.SetLinkedRef(mk, r.kwTravel);
			SetMode(lead, 1);
			lead->EvaluatePackage(true, false);
			st.walking = true;
			st.started = gClock;
		}
	}

	// ---------------------------------------------------------------- retry / verify bookkeeping
	std::unordered_map<std::uint64_t, float> gRetryAt;   // group id -> real-time seconds of next BringOnline try
	constexpr float kRetrySeconds = 1.f;
	constexpr float kSkipSummarySeconds = 30.f;
	std::unordered_map<std::uint64_t, std::set<std::string>> gSkipSeen;
	std::map<std::string, int> gSkipCount;
	float gSkipSummaryAt{ kSkipSummarySeconds };

	void SkipLog(const Group& g, const std::string& a_reason, const std::string& a_detail, bool a_always)
	{
		++gSkipCount[a_reason];
		if (!a_always && !gSkipSeen[g.id].insert(a_reason).second) return;
		Log::Line(std::string("[online] skip ") + FactionId(g.faction) + " #" + std::to_string(g.id) + " - " + a_reason +
				  (a_detail.empty() ? std::string() : " (" + a_detail + ")") + (a_always ? "" : " [repeats summarized]"));
	}

	void SkipSummary()
	{
		if (gClock < gSkipSummaryAt) return;
		gSkipSummaryAt = gClock + kSkipSummarySeconds;
		if (gSkipCount.empty()) return;
		std::string sum;
		for (auto& [k, v] : gSkipCount) sum += (sum.empty() ? "" : ", ") + k + " x" + std::to_string(v);
		Log::Line("[online] skip summary (last " + std::to_string(int(kSkipSummarySeconds)) + "s): " + sum);
		gSkipCount.clear();
	}

	std::unordered_map<std::uint64_t, float> gOnlineSince;
	struct Verify
	{
		std::uint64_t gid{ 0 };
		float due{ 0 };
		int stage{ 0 };   // 0 = check + one repair, 1 = re-check after a repair
	};
	std::vector<Verify> gVerify;
	std::string gOffWhy;   // reason text for the next [offline] line

	std::string Hex8(std::uint32_t a_v)
	{
		char b[12];
		std::snprintf(b, sizeof(b), "%08X", a_v);
		return b;
	}

	std::string CellText(RE::TESObjectCELL* c)
	{
		if (!c) return "none";
		std::string t;
		const char* e = c->GetFormEditorID();
		if (e && *e) t = e;
		else if (c->IsExteriorCell()) {
			if (auto* xy = c->GetCoordinates()) t = "ext(" + std::to_string(xy->cellX) + "," + std::to_string(xy->cellY) + ")";
			else t = "ext";
		} else t = "int";
		return t + " (" + Hex8(c->GetFormID()) + ")" + (c->IsAttached() ? " attached" : " detached");
	}

	const char* ProcText(RE::Actor* a)
	{
		auto* proc = a->GetActorRuntimeData().currentProcess;
		if (!proc) return "none";
		switch (proc->processLevel.get()) {
		case RE::PROCESS_TYPE::kHigh: return "high";
		case RE::PROCESS_TYPE::kMiddleHigh: return "midHigh";
		case RE::PROCESS_TYPE::kMiddleLow: return "midLow";
		case RE::PROCESS_TYPE::kLow: return "low";
		default: return "none";
		}
	}

	std::string ActorLine(const Member& m)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		auto* a = Pool::ActorOf(m);
		std::string o = "    " + m.name + " pool=" + std::to_string(m.poolLocal);
		if (!a) return o + " ref=NONE (pool slot has no actor)";
		auto* base = a->GetActorBase();
		auto* ws = a->GetWorldspace();
		auto* pkg = a->GetCurrentPackage();
		const auto p = a->GetPosition();
		const float d = pc ? Dist2D(pc->GetPosition(), p.x, p.y) : -1.f;
		o += " ref=" + Hex8(a->GetFormID()) + " base=" + Hex8(base ? base->GetFormID() : 0) +
			 " disabled=" + std::to_string(int(a->IsDisabled())) + " 3d=" + std::to_string(int(a->Is3DLoaded())) +
			 " dead=" + std::to_string(int(a->IsDead())) + " cell=" + CellText(a->GetParentCell()) +
			 " ws=" + (ws ? (ws->GetFormEditorID() && *ws->GetFormEditorID() ? std::string(ws->GetFormEditorID()) : Hex8(ws->GetFormID())) : std::string("none")) +
			 " pos=" + std::to_string(int(p.x)) + "," + std::to_string(int(p.y)) + "," + std::to_string(int(p.z)) +
			 " d=" + std::to_string(int(d)) + " proc=" + ProcText(a) + " gear=" + std::to_string(m.equipped.size()) + "/" + std::to_string(m.inventory.size()) +
			 " pkg=" + (pkg ? ((pkg->GetFormEditorID() && *pkg->GetFormEditorID()) ? std::string(pkg->GetFormEditorID()) + " " : std::string()) + Hex8(pkg->GetFormID()) : std::string("none"));
		return o;
	}

	void SetupActor(RE::Actor* a, Group& g, Member& m, int slot, bool hostileToPlayer)
	{
		const auto& r = Forms::R();
		Pool::FullWipe(a);
		a->SetDisplayName(m.title.empty() ? m.name : m.name + " " + m.title, true);
		if (auto* base = a->GetActorBase()) {
			base->actorData.level = std::uint16_t(std::clamp(m.level, 1, 81));
			if (auto* cls = RE::TESForm::LookupByID<RE::TESClass>(m.classId)) base->npcClass = cls;
		}
		if (auto* avo = a->AsActorValueOwner()) {
			avo->SetBaseActorValue(RE::ActorValue::kHealth, m.healthMax);
			avo->SetBaseActorValue(RE::ActorValue::kMagicka, m.magickaMax);
			avo->SetBaseActorValue(RE::ActorValue::kStamina, m.staminaMax);
			for (int s = 0; s < kSkillCount; ++s) avo->SetBaseActorValue(SkillAV(s), m.skills[s]);
			avo->SetBaseActorValue(RE::ActorValue::kConfidence, g.faction == Faction::Hunter ? 1.f : 3.f);
			// Aggression only when hostile to the player; neutral squads stay at 0 and take the wary path.
			avo->SetBaseActorValue(RE::ActorValue::kAggression, hostileToPlayer ? 1.f : 0.f);
			avo->RestoreActorValue(RE::ActorValue::kHealth, 100000.f);
			const float dmg = m.healthMax - std::max(1.f, m.health);   // carry the simulated wounds into the world
			if (dmg > 0.f) avo->DamageActorValue(RE::ActorValue::kHealth, dmg);
		}
		if (auto* ff = r.fFaction[int(g.faction)]) a->AddToFaction(ff, 0);
		if (r.fSlot[slot]) a->AddToFaction(r.fSlot[slot], 0);
		if (hostileToPlayer && r.fPlayerEnemy) a->AddToFaction(r.fPlayerEnemy, 0);
		Economy::ApplyLoadoutTo(a, m);
	}

	void VerifyTick()
	{
		if (gVerify.empty()) return;
		const auto& r = Forms::R();
		std::vector<Verify> next;
		for (auto& v : gVerify) {
			if (gClock < v.due) { next.push_back(v); continue; }
			Group* g = Sim::FindGroup(v.gid);
			if (!g || !g->online) continue;
			if (g->slot < 0 || g->slot >= kMaxOnlineGroups) continue;
			auto* mk = r.slotMarkers[g->slot];
			int ok = 0, n = 0, repaired = 0;
			std::string body;
			for (auto& m : g->members) {
				if (!m.alive()) continue;
				++n;
				auto* a = Pool::ActorOf(m);
				std::string fix;
				if (a && v.stage == 0) {
					auto* cell = a->GetParentCell();
					const auto& pl = gPlaced[g->slot];
					const bool wrongPlace = !cell || cell == r.holdingCell || Forms::WsIndex(a->GetWorldspace()) != int(g->ws) ||
											(!cell->IsAttached() && pl.cell && pl.cell->IsAttached());
					if (wrongPlace && pl.cell && pl.cell->IsAttached()) {
						MoveRefTo(a, pl.cell, Forms::WsObj(g->ws), pl.pos);
						if (a->IsDisabled()) a->Enable(false);
						fix = "moved";
					} else if (a->IsDisabled()) {
						a->Enable(false);
						fix = "enabled";
					} else if (!a->Is3DLoaded() && cell && cell->IsAttached()) {
						a->Disable();
						a->Enable(false);
						fix = "reenabled";
					}
					if (!fix.empty()) { ++repaired; a->EvaluatePackage(true, false); }
				}
				const bool good = a && !a->IsDisabled() && a->Is3DLoaded() && Forms::WsIndex(a->GetWorldspace()) == int(g->ws);
				ok += good ? 1 : 0;
				body += "\n" + ActorLine(m) + (fix.empty() ? std::string() : " REPAIR=" + fix);
			}
			Log::Line(std::string("[verify] ") + FactionId(g->faction) + " #" + std::to_string(g->id) +
					  (v.stage == 0 ? " +2s" : " +4s (after repair)") + " ok=" + std::to_string(ok) + "/" + std::to_string(n) +
					  " slot=" + std::to_string(g->slot) + " placedCell=" + CellText(gPlaced[g->slot].cell) +
					  " marker=" + (mk ? CellText(mk->GetParentCell()) : std::string("none")) + body);
			if (repaired && v.stage == 0) next.push_back({ v.gid, gClock + kVerifyDelay, 1 });
		}
		gVerify.swap(next);
	}
}

bool SpawnPointValid(const RE::NiPoint3& p)
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) return false;
	float d = Dist2D(pc->GetPosition(), p.x, p.y);
	return d >= kPopInMinDist && !OnPredictedPath(pc, p) && !InViewCone(pc, p);
}

bool LeaderNear(const Group& g, const GraphNode& n)
{
	for (auto& m : g.members) {
		auto* a = Pool::ActorOf(m);
		if (!a || a->IsDead()) continue;
		const auto p = a->GetPosition();
		return Dist2D(p, n.x, n.y) < std::max(n.arriveDist, 1500.f);
	}
	return false;
}

// A monster squad on screen: one vanilla creature per living member at the slot marker (docs/MUTANTS.md). They keep their
// own AI and aggression; the board leaves the squad alone until it is offline again.
static bool PlaceCreatures(Group& g, RE::PlayerCharacter* pc, int slot)
{
	const auto& r = Forms::R();
	auto* mk = r.slotMarkers[slot];
	auto* mkCell = gPlaced[slot].cell ? gPlaced[slot].cell : mk->GetParentCell();
	auto* mkWs = Forms::WsObj(g.ws);
	const auto mkPos = gPlaced[slot].cell ? gPlaced[slot].pos : mk->GetPosition();
	int placed = 0, failed = 0;
	std::string kinds;
	for (auto& m : g.members) {
		if (!m.alive() || m.health < 1.f) continue;
		auto* a = Creatures::Spawn(m, mk);
		if (!a) { ++failed; continue; }
		if (a->GetParentCell() != mkCell || a->GetWorldspace() != mkWs) MoveRefTo(a, mkCell, mkWs, mkPos);
		++placed;
		if (kinds.find(m.name) == std::string::npos) kinds += (kinds.empty() ? "" : ",") + m.name;
	}
	if (placed == 0) {
		Creatures::Despawn(g);
		if (r.holdingMarker) mk->MoveTo(r.holdingMarker);
		SkipLog(g, "no creature form", std::to_string(failed) + " member(s) without a placeable form (monsters.ltx)", true);
		return false;
	}
	gOnlineSince[g.id] = gClock;
	gVerify.push_back({ g.id, gClock + kVerifyDelay, 0 });
	gSlotOwner[slot] = g.id;
	g.online = true;
	g.slot = slot;
	g.trust = Trust::Hostile;
	const GraphNode* at = g.currentTarget >= 0 ? Graph::Node(std::uint32_t(g.currentTarget)) : nullptr;
	Log::Line(std::string("[online] ") + FactionId(g.faction) + " " + TierName(g.tier) + " #" + std::to_string(g.id) + " x" + std::to_string(placed) +
			  " (" + kinds + ") at " + std::to_string(int(mkPos.x)) + "," + std::to_string(int(mkPos.y)) +
			  " d=" + std::to_string(int(Dist2D(pc->GetPosition(), mkPos.x, mkPos.y))) + " slot=" + std::to_string(slot) + " cell=" + CellText(mkCell) +
			  " hour=" + std::to_string(int(HourOfDay(Sim::Hours()))) + (at ? std::string(at->props.lair > 0.f ? " lair " : " near ") + at->name : std::string(" roaming")) +
			  (failed ? " FAILED=" + std::to_string(failed) : std::string()));
	return true;
}

static bool BringOnline(Group& g, RE::PlayerCharacter* pc, bool allowFallbackRace, bool forceNear)
{
	const auto& r = Forms::R();
	int slot = FreeSlot();
	if (slot < 0) {
		SkipLog(g, "no free slot", "online groups=" + std::to_string(Sim::GroupsOnline()), forceNear);
		return false;
	}
	int living = 0;
	for (auto& m : g.members) living += (m.alive() && m.health >= 1.f);
	if (living == 0) return false;
	if (Sim::ActorsLoaded() + living > kMaxOnlineActors) {
		SkipLog(g, "actor cap", std::to_string(Sim::ActorsLoaded()) + "+" + std::to_string(living) + ">" + std::to_string(kMaxOnlineActors), forceNear);
		return false;
	}
	const bool beasts = !IsHumanFaction(g.faction);
	if (!beasts && !Pool::CanAssignAll(g.members, allowFallbackRace)) {
		SkipLog(g, "no free pool actor", "dry run, resolved " + std::to_string(Pool::ResolvedCount()) + "/" + std::to_string(kPoolSize), forceNear);
		return false;
	}
	// Find and validate the spawn point FIRST; only when the marker sits on a good point are pool actors assigned.
	std::string why;
	if (!PlaceGroupMarker(slot, g, pc, forceNear, why)) {
		SkipLog(g, "no navmesh point", why, forceNear);
		return false;
	}
	if (beasts) return PlaceCreatures(g, pc, slot);
	std::vector<Member*> assigned;
	std::vector<RaceId> races;   // Assign may switch an anonymous member's race (fallback); undo if we bail out
	for (auto& m : g.members) races.push_back(m.race);
	auto undoRaces = [&] { for (std::size_t i = 0; i < g.members.size() && i < races.size(); ++i) g.members[i].race = races[i]; };
	for (auto& m : g.members) {
		if (!m.alive() || m.health < 1.f) continue;
		if (!Pool::Assign(m, allowFallbackRace)) {
			undoRaces();
			for (auto* am : assigned) Pool::Release(*am);
			if (r.holdingMarker) r.slotMarkers[slot]->MoveTo(r.holdingMarker);
			SkipLog(g, "no free pool actor", "resolved " + std::to_string(Pool::ResolvedCount()) + "/" + std::to_string(kPoolSize) +
				", race=" + std::to_string(int(m.race)) + " sex=" + std::to_string(int(m.sex)), forceNear);
			return false;
		}
		assigned.push_back(&m);
	}
	const bool hostile = Behavior::AttacksOnSight(g);
	auto* mk = r.slotMarkers[slot];
	auto* mkCell = gPlaced[slot].cell ? gPlaced[slot].cell : mk->GetParentCell();
	auto* mkWs = Forms::WsObj(g.ws);
	const auto mkPos = gPlaced[slot].cell ? gPlaced[slot].pos : mk->GetPosition();
	int placed = 0, missing = 0, reMoved = 0;
	for (auto* m : assigned) {
		auto* a = Pool::ActorOf(*m);
		if (!a) { ++missing; continue; }
		a->MoveTo(mk);
		if (a->GetParentCell() != mkCell || a->GetWorldspace() != mkWs) {
			MoveRefTo(a, mkCell, mkWs, mkPos);
			++reMoved;
		}
		if (a->IsDisabled()) a->Enable(false);
		SetupActor(a, g, *m, slot, hostile);
		++placed;
	}
	if (placed == 0) {
		undoRaces();
		for (auto* am : assigned) Pool::Release(*am);
		if (r.holdingMarker) mk->MoveTo(r.holdingMarker);
		SkipLog(g, "no pool actor form", "assigned " + std::to_string(assigned.size()) + ", none resolved to an Actor", true);
		return false;
	}
	gOnlineSince[g.id] = gClock;
	gVerify.push_back({ g.id, gClock + kVerifyDelay, 0 });
	gSlotOwner[slot] = g.id;
	g.online = true;
	g.slot = slot;
	g.trust = hostile ? Trust::Hostile : (Behavior::TooHurt(g) ? Trust::BackingOff : Trust::Neutral);
	g.trustTimer = 0;
	if (g.trust == Trust::BackingOff) {
		for (auto* m : assigned) {
			auto* a = Pool::ActorOf(*m);
			if (!a || a->IsDead()) continue;
			a->extraList.SetLinkedRef(pc, r.kwFlee);
			if (r.fMode) a->AddToFaction(r.fMode, 4);
			a->EvaluatePackage(true, false);
		}
	} else {
		DriveOnline(g);
	}
	const GraphNode* tgt = g.assignedTarget >= 0 ? Graph::Node(std::uint32_t(g.assignedTarget)) : nullptr;
	Log::Line(std::string("[online] ") + FactionId(g.faction) + " " + TierName(g.tier) + " #" + std::to_string(g.id) + " x" + std::to_string(placed) +
			  " at " + std::to_string(int(mkPos.x)) + "," + std::to_string(int(mkPos.y)) +
			  " d=" + std::to_string(int(Dist2D(pc->GetPosition(), mkPos.x, mkPos.y))) + " slot=" + std::to_string(slot) +
			  " cell=" + CellText(mkCell) + (hostile ? " hostile" : " civil") + (allowFallbackRace ? "" : " noFallback") +
			  " " + (g.action == Action::Reach ? "heading to " : g.action == Action::Stay ? "staying at " : "idle ") + (tgt ? tgt->name : std::string("-")) +
			  (missing ? " MISSING=" + std::to_string(missing) : std::string()) + (reMoved ? " reMoved=" + std::to_string(reMoved) : std::string()));
	return true;
}

namespace {
	RE::Actor* LeaderOf(Group& g)
	{
		for (auto& m : g.members)
			if (auto* a = Pool::ActorOf(m); a && !a->IsDead()) return a;
		return nullptr;
	}
}

// A squad that has something to say to the player walks up to them and stops short: the leader travels to a point on the line
// to the player, the others follow the leader (the same package machinery DriveOnline and the patrol director use).
// ALife_PackTravel has an arrival radius of 256 units: a leader stops once it is that close to its marker. So the marker is put beyond the
// target by (radius - stop), and the leader ends up about `stop` units from it.
constexpr float kTravelRadius = 256.f;

bool ApproachRef(Group& g, RE::TESObjectREFR* target, float stop)
{
	const auto& r = Forms::R();
	if (!target || g.slot < 0 || !r.slotMarkers[g.slot]) return false;
	auto* lead = LeaderOf(g);
	if (!lead) return false;
	const auto lp = lead->GetPosition(), pp = target->GetPosition();
	const float dx = pp.x - lp.x, dy = pp.y - lp.y;
	const float d = std::sqrt(dx * dx + dy * dy);
	if (d <= stop + 60.f) { Hold(g); return true; }
	const float beyond = kTravelRadius - stop;   // negative: the marker stops short of the target
	RE::NiPoint3 to{ pp.x + dx / d * beyond, pp.y + dy / d * beyond, pp.z };
	auto* cell = target->GetParentCell();
	// the travel package can only path to a point on the navmesh: a marker dropped on a hillside or in the grass is why chasing squads used to stall
	// (the player stands on the navmesh, and snapping the marker to a vertex beside them sent chasing squads off the wrong way: bodies are snapped, the player is not)
	if (target != static_cast<RE::TESObjectREFR*>(RE::PlayerCharacter::GetSingleton()))
		if (Snap sn; SnapToNavmesh(to, 400.f, sn)) { to = sn.pos; cell = sn.cell; }
	auto* mk = r.slotMarkers[g.slot];
	MoveRefTo(mk, cell, target->GetWorldspace(), to);
	lead->extraList.SetLinkedRef(mk, r.kwTravel);
	SetMode(lead, 1);
	lead->EvaluatePackage(true, false);
	for (auto& m : g.members) {
		auto* a = Pool::ActorOf(m);
		if (!a || a->IsDead() || a == lead) continue;
		a->extraList.SetLinkedRef(lead, r.kwLeader);
		SetMode(a, 3);
	}
	return false;
}

void Approach(Group& g, float stop) { ApproachRef(g, RE::PlayerCharacter::GetSingleton(), stop); }

void Hold(Group& g)
{
	const auto& r = Forms::R();
	auto* lead = LeaderOf(g);
	if (!lead || g.slot < 0 || !r.slotMarkers[g.slot]) return;
	r.slotMarkers[g.slot]->MoveTo(lead);
	lead->extraList.SetLinkedRef(r.slotMarkers[g.slot], r.kwTravel);
	SetMode(lead, 2);
	lead->EvaluatePackage(true, false);
}

void Resume(Group& g)
{
	gDriven.erase(g.id);
	gStay.erase(g.id);
	DriveOnline(g);
}

bool BringOnlineEx(Group& g, bool allowFallbackRace, bool forceNear)
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) return false;
	return BringOnline(g, pc, allowFallbackRace, forceNear);
}

void SendOffline(Group& g, bool)
{
	const auto& r = Forms::R();
	if (!IsHumanFaction(g.faction)) {   // monster squad: creatures report wounds and deaths, the living are deleted
		for (auto& m : g.members) {
			auto* a = Pool::ActorOf(m);
			if (!a || a->IsDead() || Forms::WsIndex(a->GetWorldspace()) != int(g.ws)) continue;
			const auto p = a->GetPosition();
			g.x = p.x; g.y = p.y; g.z = p.z;
			g.node = Graph::Nearest(p.x, p.y, g.ws);
			g.prevNode = g.node;
			break;
		}
		Creatures::Despawn(g);
		for (auto& m : g.members) if (!m.alive()) Sim::OnGroupMemberDied(g, m);
		g.members.erase(std::remove_if(g.members.begin(), g.members.end(), [](const Member& m) { return !m.alive(); }), g.members.end());
		if (g.slot >= 0 && g.slot < kMaxOnlineGroups) {
			gSlotOwner[g.slot] = 0;
			if (r.slotMarkers[g.slot] && r.holdingMarker) r.slotMarkers[g.slot]->MoveTo(r.holdingMarker);
		}
		g.online = false;
		g.slot = -1;
		gOnlineSince.erase(g.id);
		gDriven.erase(g.id);
		gStay.erase(g.id);
		Log::Line(std::string("[offline] ") + FactionId(g.faction) + " #" + std::to_string(g.id) + " living=" + std::to_string(g.livingCount()) +
				  (gOffWhy.empty() ? std::string() : " reason=" + gOffWhy));
		gOffWhy.clear();
		return;
	}
	RE::Actor* lead = nullptr;
	for (auto& m : g.members) {
		auto* a = Pool::ActorOf(m);
		if (!a) continue;
		if (!lead && !a->IsDead()) lead = a;
		Economy::ReadBackFrom(a, m);   // gear persistence: what they picked up or used stays with them
	}
	if (lead) {
		auto p = lead->GetPosition();
		g.x = p.x; g.y = p.y; g.z = p.z;
		if (Forms::WsIndex(lead->GetWorldspace()) == int(g.ws)) {
			g.node = Graph::Nearest(p.x, p.y, g.ws);
			g.prevNode = g.node;
		}
	}
	for (auto& m : g.members) {
		auto* a = Pool::ActorOf(m);
		if (a) {
			Pool::FullWipe(a);
			if (a->IsDead()) a->Resurrect(false, false);
			if (r.holdingMarker) a->MoveTo(r.holdingMarker);
			a->Disable();
		}
	}
	for (auto& m : g.members) {
		if (!m.alive()) Sim::OnGroupMemberDied(g, m);
		else Pool::Release(m);
	}
	g.members.erase(std::remove_if(g.members.begin(), g.members.end(), [](const Member& m) { return !m.alive(); }), g.members.end());
	if (g.slot >= 0 && g.slot < kMaxOnlineGroups) {
		gSlotOwner[g.slot] = 0;
		if (r.slotMarkers[g.slot] && r.holdingMarker) r.slotMarkers[g.slot]->MoveTo(r.holdingMarker);
	}
	g.online = false;
	g.slot = -1;
	gOnlineSince.erase(g.id);
	gDriven.erase(g.id);
	gStay.erase(g.id);
	Log::Line(std::string("[offline] ") + FactionId(g.faction) + " #" + std::to_string(g.id) + " living=" + std::to_string(g.livingCount()) +
			  (gOffWhy.empty() ? std::string() : " reason=" + gOffWhy));
	gOffWhy.clear();
}

void SendAllOffline(bool a_finish)
{
	for (auto& g : Sim::Groups()) if (g.online) SendOffline(g, a_finish);
	gSlotOwner.fill(0);
}

void OnCellDetach(RE::TESObjectCELL*) {}

// Slot owners were only cleared by SendOffline/SendAllOffline. A load or new game rebuilt the groups as offline but left
// stale owner ids; called from PreLoad, PostLoad and NewGame.
void Reset()
{
	gSlotOwner.fill(0);
	gRetryAt.clear();
	gDroppedAt.clear();
	gOnlineSince.clear();
	gVerify.clear();
	gDriven.clear();
	gStay.clear();
	gPlaced.fill(Snap{});
	gSkipSeen.clear();
	gSkipCount.clear();
	gSkipSummaryAt = gClock + kSkipSummarySeconds;
	Creatures::Reset();
}

void ParkMarkers()
{
	const auto& r = Forms::R();
	if (!r.holdingMarker) return;
	int moved = 0;
	for (auto* mk : r.slotMarkers) {
		if (!mk) continue;
		if (mk->GetParentCell() != r.holdingCell) { mk->MoveTo(r.holdingMarker); ++moved; }
	}
	Log::Line("[online] slots reset; parked " + std::to_string(moved) + " slot marker(s) in the holding cell");
}

std::string DescribeActors(const Group& g)
{
	std::string o;
	for (auto& m : g.members) {
		if (!m.alive()) continue;
		o += ActorLine(m) + "\n";
	}
	return o;
}

void Tick(float a_dt)
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || !Forms::R().pluginLoaded) return;
	gClock += a_dt;
	Creatures::Tick();
	Scavenge::Tick(a_dt);
	SkipSummary();
	VerifyTick();
	const auto pp = pc->GetPosition();
	const int playerWs = Forms::WsIndex(pc->GetWorldspace());   // the worldspace the simulation is on screen in (-1: somewhere else)
	const bool inTamriel = playerWs >= 0;
	auto& groups = Sim::Groups();
	// update / retire online groups
	for (auto& g : groups) {
		if (!g.online) continue;
		RE::Actor* lead = nullptr;
		int alive = 0, present = 0;
		for (auto& m : g.members) {
			auto* a = Pool::ActorOf(m);
			present += a ? 1 : 0;
			if (a && !a->IsDead()) { ++alive; if (!lead) lead = a; }
		}
		const bool leadInWorld = lead && Forms::WsIndex(lead->GetWorldspace()) == int(g.ws);
		if (leadInWorld) { auto p = lead->GetPosition(); g.x = p.x; g.y = p.y; g.z = p.z; }
		auto since = gOnlineSince.find(g.id);
		const bool grace = since != gOnlineSince.end() && gClock - since->second < kOnlineGrace;
		const float d = Dist2D(pp, g.x, g.y);
		std::string why;
		if (!inTamriel || playerWs != int(g.ws)) why = "playerLeftTheSquadsWorld";
		else if (!grace) {
			if (!lead) why = present ? "allDead" : "noActor";
			else if (!leadInWorld) why = "leaderNotInTamriel";
			else if (d > OfflineDist()) why = "far d=" + std::to_string(int(d));
			else if (d > kOfflineUnloadedDist) {
				auto* lc = lead->GetParentCell();
				if (!lead->Is3DLoaded() || !lc || !lc->IsAttached()) why = "leftLoadedArea d=" + std::to_string(int(d));
			}
		}
		if (!why.empty()) {
			if (why.starts_with("far") || why.starts_with("leftLoadedArea")) gDroppedAt[g.id] = { d, gClock };
			gOffWhy = why;
			SendOffline(g, true);
			continue;
		}
		if (!lead) continue;   // grace period: nothing to drive yet
		if (!IsHumanFaction(g.faction)) continue;   // creatures run their own AI: no packages, talk, looting or standing
		// the board decides what a squad does next; re-drive the packages when it changed its mind or the waypoint went stale
		auto& dr = gDriven[g.id];
		const bool changed = dr.target != g.assignedTarget || dr.action != g.action;
		const bool scavenging = Scavenge::TickGroup(g, lead, a_dt);                  // a squad heading for a body is driven by Scavenge
		const bool free = !scavenging && g.trust != Trust::BackingOff && g.trust != Trust::Wary;   // a challenging squad is driven by Behavior
		if (free && (changed || (g.action == Action::Reach && gClock - dr.at > kReDriveSeconds)))
			DriveOnline(g);
		if (free && g.action != Action::Reach) StayDirector(g, lead, changed);   // idle squads patrol and pause
		Behavior::TickOnline(g, a_dt);
	}
	// online squads of hostile factions fight each other on sight (engine relations do most of it; this starts it for stragglers)
	static float pairTimer = 0;
	pairTimer += a_dt;
	if (pairTimer > 2.f) {
		pairTimer = 0;
		const auto& cfg = Sim::Cfg();
		for (auto& ga : groups)
			for (auto& gb : groups) {
				if (&ga == &gb || !ga.online || !gb.online) continue;
				if (!cfg.IsEnemy(ga.faction, gb.faction)) continue;
				float dx = ga.x - gb.x, dy = ga.y - gb.y;
				if (dx * dx + dy * dy > 3000.f * 3000.f) continue;
				std::vector<RE::Actor*> targets;
				for (auto& m : gb.members) if (auto* t = Pool::ActorOf(m); t && !t->IsDead()) targets.push_back(t);
				if (targets.empty()) continue;
				std::size_t k = 0;
				for (auto& m : ga.members) {
					auto* a = Pool::ActorOf(m);
					if (!a || a->IsDead() || a->IsInCombat()) continue;
					a->StartCombat(targets[k++ % targets.size()]);
				}
			}
	}
	std::erase_if(gDroppedAt, [](const auto& e) { return gClock - e.second.at >= kRejoinForget; });
	if (!inTamriel || Sim::CatchingUp()) return;
	// bring the nearest eligible group online
	Group* best = nullptr;
	float bd = PickRadius();
	for (auto& g : groups) {
		if (g.online || g.members.empty() || int(g.ws) != playerWs) continue;
		if (Graph::InCityBuffer(g.x, g.y, g.ws)) continue;   // squads inside a city stay offline (the guards own the streets)
		if (auto it = gRetryAt.find(g.id); it != gRetryAt.end() && gClock < it->second) continue;
		if (!IsHumanFaction(g.faction) && !Sim::B().MonsterMayComeOnline(g, HourOfDay(Sim::Hours()))) continue;   // Anomaly: day / night mutants stay offline outside their hours
		float d = Dist2D(pp, g.x, g.y);
		if (auto it = gDroppedAt.find(g.id); it != gDroppedAt.end() && gClock - it->second.at < kRejoinForget && d > it->second.d - kRejoinMargin) continue;   // walked away from: not pulled back
		if (d < bd) { bd = d; best = &g; }
	}
	if (best && Sim::GroupsOnline() < kMaxOnlineGroups) {
		if (BringOnline(*best, pc, true, false)) { gRetryAt.erase(best->id); gSkipSeen.erase(best->id); gDroppedAt.erase(best->id); }
		else gRetryAt[best->id] = gClock + kRetrySeconds;
	}
}
std::string OfflineWhy(const Group& g)
{
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) return "noPlayer";
	if (g.online) return "";
	if (g.members.empty()) return "empty";
	if (int(g.ws) != Forms::WsIndex(pc->GetWorldspace())) return "otherWorld";
	if (Sim::CatchingUp()) return "catchingUp";
	if (Graph::InCityBuffer(g.x, g.y, g.ws)) return "cityBuffer";
	const float d = Dist2D(pc->GetPosition(), g.x, g.y);
	if (d >= PickRadius()) return "beyondOnlineRadius";
	if (auto it = gDroppedAt.find(g.id); it != gDroppedAt.end() && gClock - it->second.at < kRejoinForget && d > it->second.d - kRejoinMargin) return "walkedAway";
	if (!IsHumanFaction(g.faction) && !Sim::B().MonsterMayComeOnline(g, HourOfDay(Sim::Hours()))) return "monsterHours";
	if (auto it = gRetryAt.find(g.id); it != gRetryAt.end() && gClock < it->second) return "retryWait";
	if (Sim::GroupsOnline() >= kMaxOnlineGroups) return "cap";
	return "";
}
}
