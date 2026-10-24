#include "ALife/Camp.h"
#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Settings.h"
#include "ALife/Systems.h"
#include "ALife/Companions.h"
#include "ALife/Util.h"

#include <unordered_set>

// Camp life on screen (docs/CAMPLIFE.md 3.2-3.5, M5h): squads staying at a place take jobs (core Camp::AssignJobs), each member
// gets a target (a job marker for watch jobs, a campfire or a bed for sitting and sleeping, the place's marker for the rest) and
// one of the camp packages (ALifeModeFaction rank 5 hold / 6 sit, ALife.esp 0x924 / 0x925); a campfire with two or more at it plays
// a story now and then. Offline nothing changes. Runs inside Online::Tick (sim lock held).
namespace ALife::CampLife {
namespace {
	CampConfig gCfg;
	bool gLoaded{ false };
	enum SpotKind { kFire = 0, kUnlit, kBed, kSeat, kLean, kCook, kSpotKinds };
	constexpr const char* kSpotIds[kSpotKinds] = { "campfire", "campfire_unlit", "bed", "seat", "lean", "cook" };
	std::array<std::unordered_set<RE::FormID>, kSpotKinds> gSpotBases;

	struct PlaceSpots
	{
		bool scanned{ false };
		std::array<std::vector<RE::ObjectRefHandle>, kSpotKinds> refs;
	};
	std::unordered_map<std::uint32_t, PlaceSpots> gSpots;   // by node, for the session (cleared on load)

	struct PlaceJobs
	{
		std::vector<CampAssign> assign;
		float at{ -1e9f };
		bool night{ false };
		std::size_t living{ 0 };
		std::vector<std::uint64_t> groups;
	};
	std::unordered_map<std::uint32_t, PlaceJobs> gJobs;

	struct Applied
	{
		std::uint64_t group{ 0 };
		std::uint32_t node{ 0 };
		CampJob job{ CampJob::None };
		int index{ 0 };
		int marker{ -1 };
		RE::ObjectRefHandle target;
	};
	std::unordered_map<std::uint64_t, Applied> gApplied;    // by member id
	std::array<std::uint64_t, Forms::Local::kJobMarkers> gMarkerOwner{};

	struct Scene
	{
		float next{ 0 };
		std::uint64_t director{ 0 };
		std::vector<std::string> lines;
		std::size_t line{ 0 };
		std::string name;
	};
	std::unordered_map<std::uint32_t, Scene> gScenes;   // by the campfire ref's FormID

	float gClock{ 0 };
	int gChanges{ 0 };   // package changes this tick (budget max_package_changes)

	float Dist2D(const RE::NiPoint3& a, const RE::NiPoint3& b) { return std::hypot(a.x - b.x, a.y - b.y); }

	RE::TESObjectREFR* Ref(const RE::ObjectRefHandle& h)
	{
		auto p = h.get();
		return p ? p.get() : nullptr;
	}

	void Scan(std::uint32_t a_node, const GraphNode& n, RE::Actor* a_lead)
	{
		auto& sp = gSpots[a_node];
		if (sp.scanned) return;
		auto* origin = Graph::MarkerOf(n);
		if (!origin || !origin->Is3DLoaded() || Dist2D(origin->GetPosition(), a_lead->GetPosition()) > 3000.f) origin = a_lead;
		auto* tes = RE::TES::GetSingleton();
		if (!tes) return;
		sp.scanned = true;
		int seen = 0;
		tes->ForEachReferenceInRange(origin, gCfg.scanRadius, [&](RE::TESObjectREFR* r) {
			++seen;
			if (!r || r->IsDisabled() || r->IsDeleted()) return RE::BSContainer::ForEachResult::kContinue;
			auto* base = r->GetBaseObject();
			if (!base) return RE::BSContainer::ForEachResult::kContinue;
			const RE::FormID id = base->GetFormID();
			for (int k = 0; k < kSpotKinds; ++k)
				if (gSpotBases[k].count(id)) { sp.refs[k].push_back(r->CreateRefHandle()); break; }
			return RE::BSContainer::ForEachResult::kContinue;
		});
		Log::Line("[camp] " + n.name + ": scanned " + std::to_string(seen) + " refs within " + std::to_string(int(gCfg.scanRadius)) + ": fires=" +
				  std::to_string(sp.refs[kFire].size()) + " unlit=" + std::to_string(sp.refs[kUnlit].size()) + " beds=" + std::to_string(sp.refs[kBed].size()) +
				  " seats=" + std::to_string(sp.refs[kSeat].size()) + " lean=" + std::to_string(sp.refs[kLean].size()) + " cook=" + std::to_string(sp.refs[kCook].size()));
	}

	// nearest ref of a kind to a point; a_skip: refs already given to someone (beds)
	RE::TESObjectREFR* Nearest(std::uint32_t a_node, int a_kind, const RE::NiPoint3& a_from, const std::unordered_set<RE::FormID>* a_skip = nullptr)
	{
		RE::TESObjectREFR* best = nullptr;
		float bd = 1e30f;
		for (auto& h : gSpots[a_node].refs[a_kind]) {
			auto* r = Ref(h);
			if (!r || (a_skip && a_skip->count(r->GetFormID()))) continue;
			const float d = Dist2D(r->GetPosition(), a_from);
			if (d < bd) { bd = d; best = r; }
		}
		return best;
	}

	int TakeMarker(std::uint64_t a_member)
	{
		const auto& mk = Forms::R().jobMarkers;
		for (int i = 0; i < Forms::Local::kJobMarkers; ++i)
			if (gMarkerOwner[std::size_t(i)] == a_member && mk[std::size_t(i)]) return i;
		for (int i = 0; i < Forms::Local::kJobMarkers; ++i)
			if (!gMarkerOwner[std::size_t(i)] && mk[std::size_t(i)]) { gMarkerOwner[std::size_t(i)] = a_member; return i; }
		return -1;
	}
	void FreeMarker(std::uint64_t a_member)
	{
		for (int i = 0; i < Forms::Local::kJobMarkers; ++i)
			if (gMarkerOwner[std::size_t(i)] == a_member) {
				gMarkerOwner[std::size_t(i)] = 0;
				if (auto* m = Forms::R().jobMarkers[std::size_t(i)]; m && Forms::R().holdingMarker) m->MoveTo(Forms::R().holdingMarker);
			}
	}

	// a watch spot around the place: guards on the ring, the sniper on the highest of six points, the camper out on the approach
	bool WatchSpot(const GraphNode& n, const RE::NiPoint3& c, CampJob j, int idx, std::uint64_t seed, RE::NiPoint3& out, RE::TESObjectCELL*& cell)
	{
		Rng r(Mix64(seed ^ (std::uint64_t(j) << 32) ^ std::uint64_t(idx)));
		auto pick = [&](float ang, float rad, RE::NiPoint3& p, RE::TESObjectCELL*& cl) {
			RE::NiPoint3 q{ c.x + std::sin(ang) * rad, c.y + std::cos(ang) * rad, c.z };
			if (auto* tes = RE::TES::GetSingleton()) {
				float h = 0;
				if (tes->GetLandHeight(q, h)) q.z = h;
			}
			return Online::SnapPoint(q, 900.f, p, cl);
		};
		if (j == CampJob::Sniper) {
			bool any = false;
			for (int i = 0; i < 6; ++i) {
				RE::NiPoint3 p;
				RE::TESObjectCELL* cl = nullptr;
				if (pick(r.frac() * 6.2831853f, r.uniform(700.f, 1500.f), p, cl) && (!any || p.z > out.z)) { out = p; cell = cl; any = true; }
			}
			return any;
		}
		if (j == CampJob::Camper) {   // toward the first neighbouring place: where a squad would come from
			float ang = r.frac() * 6.2831853f;
			if (!n.edges.empty())
				if (const GraphNode* nb = Graph::Node(n.edges[std::size_t(idx) % n.edges.size()])) ang = std::atan2(nb->x - c.x, nb->y - c.y);
			return pick(ang + (r.frac() - 0.5f) * 0.5f, r.uniform(1500.f, 2500.f), out, cell);
		}
		return pick(float(idx) * 2.3999632f + r.frac() * 0.4f, r.uniform(500.f, 1100.f), out, cell);   // guards: golden-angle ring
	}

	void Follow(RE::Actor* a, RE::Actor* lead)
	{
		const auto& r = Forms::R();
		a->extraList.SetLinkedRef(lead, r.kwLeader);
		Online::SetActorMode(a, 3);
		a->EvaluatePackage(true, false);
	}

	// one member: the target and package of its job; false when the tick's package budget is used up
	bool Apply(Group& g, Member& m, RE::Actor* a, RE::Actor* lead, const GraphNode& n, std::uint32_t node, const CampAssign& want,
		std::unordered_set<RE::FormID>& bedsTaken)
	{
		auto it = gApplied.find(m.id);
		if (it != gApplied.end() && it->second.job == want.job && it->second.index == want.index && (want.job == CampJob::Patrol || Ref(it->second.target))) {
			if (want.job == CampJob::Sleep) if (auto* t = Ref(it->second.target)) bedsTaken.insert(t->GetFormID());
			return true;
		}
		if (gChanges >= gCfg.maxPackageChanges) return false;
		const auto& r = Forms::R();
		auto* marker = Graph::MarkerOf(n);
		RE::NiPoint3 c = marker && marker->Is3DLoaded() && Dist2D(marker->GetPosition(), lead->GetPosition()) < 3000.f ? marker->GetPosition() : lead->GetPosition();
		RE::TESObjectREFR* target = nullptr;
		int mode = 2, mk = -1;
		switch (want.job) {
		case CampJob::Guard: case CampJob::Sniper: case CampJob::Camper: {
			RE::NiPoint3 p;
			RE::TESObjectCELL* cell = nullptr;
			mk = TakeMarker(m.id);
			if (mk >= 0 && WatchSpot(n, c, want.job, want.index, std::uint64_t(n.location) ^ g.id, p, cell)) {
				target = r.jobMarkers[std::size_t(mk)];
				Online::MoveRef(target, cell, Forms::WsObj(g.ws), p);
				mode = 5;
			}
			break;
		}
		case CampJob::Sleep:
			target = Nearest(node, kBed, c, &bedsTaken);
			if (target) { bedsTaken.insert(target->GetFormID()); mode = 6; break; }
			[[fallthrough]];   // no bed: the fire
		case CampJob::Campfire:
			target = Nearest(node, kFire, c);
			if (!target) target = Nearest(node, kUnlit, c);
			if (!target) target = Nearest(node, kSeat, c);
			mode = 6;
			break;
		case CampJob::Patrol:   // a second squad's leader without the patrol: it stays with its own squad
			if (a == lead) return true;
			Follow(a, lead);
			++gChanges;
			if (mk < 0) FreeMarker(m.id);
			gApplied[m.id] = { g.id, node, want.job, want.index, -1, {} };
			return true;
		default: break;   // collector, walker: the place's own sandbox
		}
		if (!target) { target = marker && Dist2D(marker->GetPosition(), lead->GetPosition()) < 3000.f ? marker : nullptr; mode = 2; }
		if (!target) { if (mk >= 0) FreeMarker(m.id); return true; }   // nothing to anchor to: keep following the leader
		if (mk < 0) FreeMarker(m.id);
		a->extraList.SetLinkedRef(target, r.kwTravel);
		Online::SetActorMode(a, mode);
		a->EvaluatePackage(true, false);
		++gChanges;
		gApplied[m.id] = { g.id, node, want.job, want.index, mk, target->CreateRefHandle() };
		if (Settings::Get().bDebug)
			Log::Line("[camp] " + m.name + " (" + FactionId(g.faction) + ") at " + n.name + ": " + CampJobId(want.job) + " #" + std::to_string(want.index) +
					  " mode " + std::to_string(mode) + " -> " + std::to_string(target->GetFormID()));
		return true;
	}

	void ReleaseMember(std::uint64_t a_member) { FreeMarker(a_member); gApplied.erase(a_member); }

	bool GroupBusy(const Group& g)
	{
		for (auto& m : g.members)
			if (auto* a = Pool::ActorOf(m); a && !a->IsDead() && a->IsInCombat()) return true;
		return false;
	}

	void SceneTick(float a_dt)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) return;
		std::unordered_map<RE::FormID, std::vector<std::pair<std::uint64_t, Group*>>> atFire;
		for (auto& [mid, ap] : gApplied) {
			if (ap.job != CampJob::Campfire) continue;
			auto* t = Ref(ap.target);
			if (!t || Dist2D(t->GetPosition(), pc->GetPosition()) > 2500.f) continue;
			if (Group* g = Sim::FindGroup(ap.group)) atFire[t->GetFormID()].push_back({ mid, g });
		}
		for (auto it = gScenes.begin(); it != gScenes.end();) it = atFire.count(it->first) ? std::next(it) : gScenes.erase(it);
		for (auto& [fire, who] : atFire) {
			if (who.size() < 2) continue;
			auto& sc = gScenes[fire];
			Rng r(Mix64(std::uint64_t(fire) ^ std::uint64_t(gClock * 7.f)));
			if (sc.next <= 0.f) { sc.next = gClock + r.uniform(gCfg.idleMin, gCfg.idleMax); continue; }
			if (gClock < sc.next) continue;
			if (sc.line < sc.lines.size()) {   // the story goes on: one line every few seconds
				RE::SendHUDMessage::ShowHUDMessage((sc.name + ": " + sc.lines[sc.line]).c_str());
				++sc.line;
				sc.next = gClock + 6.f;
				if (sc.line >= sc.lines.size()) { sc.lines.clear(); sc.line = 0; sc.next = gClock + r.uniform(gCfg.idleMin, gCfg.idleMax); }
				continue;
			}
			sc.next = gClock + r.uniform(gCfg.idleMin, gCfg.idleMax);
			if (!r.chance(gCfg.storyChance)) continue;
			const auto& [mid, g] = who[std::size_t(r.index(int(who.size())))];
			const auto& stories = gCfg.stories[std::size_t(g->faction)];
			if (stories.empty()) continue;
			const Member* dm = nullptr;
			for (auto& m : g->members) if (m.id == mid) dm = &m;
			if (!dm) continue;
			sc.director = mid;
			sc.name = dm->name;
			sc.lines = stories[std::size_t(r.index(int(stories.size())))];
			sc.line = 0;
			sc.next = gClock;
			Log::Line("[camp] story at the fire by " + dm->name + " (" + FactionId(g->faction) + "): " + sc.lines.front());
		}
	}
}

void Init()
{
	gLoaded = false;
	gCfg = CampConfig{};
	for (auto& s : gSpotBases) s.clear();
	Ltx camp, spots;
	std::string err;
	const std::string dir = Settings::ConfigDir() + "/";
	if (!camp.LoadFile(dir + "camp.ltx", &err)) { Log::Line("[camp] no camp.ltx: camp life off (" + err + ")"); return; }
	const bool okSpots = spots.LoadFile(dir + "camp_spots.ltx", nullptr);
	if (!LoadCampConfig(camp, okSpots ? &spots : nullptr, gCfg, err)) { Log::Line("[camp] camp.ltx: " + err); return; }
	for (int k = 0; k < kSpotKinds; ++k)
		if (auto it = gCfg.spots.find(kSpotIds[k]); it != gCfg.spots.end())
			for (auto id : it->second) gSpotBases[k].insert(id);   // Skyrim.esm: the load index is 00
	int markers = 0;
	for (auto* m : Forms::R().jobMarkers) markers += m != nullptr;
	gLoaded = markers > 0;
	Log::Line("[camp] " + std::string(gLoaded ? "on" : "OFF (ALife.esp has no job markers: build it with tools/esp/build_esp.py)") + ": " +
			  std::to_string(markers) + " job markers, spot bases fires=" + std::to_string(gSpotBases[kFire].size()) + " beds=" + std::to_string(gSpotBases[kBed].size()) +
			  " seats=" + std::to_string(gSpotBases[kSeat].size()));
}

void Reset()
{
	gSpots.clear();
	gJobs.clear();
	gApplied.clear();
	gScenes.clear();
	gMarkerOwner.fill(0);
}

bool OwnsLeader(const Group& g)
{
	if (!gLoaded || g.members.empty()) return false;
	auto it = gApplied.find(g.members.front().id);
	return it != gApplied.end() && it->second.job != CampJob::Patrol;
}

void Tick(float a_dt)
{
	gClock += a_dt;
	gChanges = 0;
	if (!gLoaded) return;
	// members whose squad left the place, went offline or started travelling: DriveOnline already re-drove them, free their markers
	for (auto it = gApplied.begin(); it != gApplied.end();) {
		const Group* g = Sim::FindGroup(it->second.group);
		const bool keep = g && g->online && g->slot >= 0 && g->action != Action::Reach && g->assignedTarget == std::int64_t(it->second.node);
		if (keep) { ++it; continue; }
		FreeMarker(it->first);
		it = gApplied.erase(it);
	}
	SceneTick(a_dt);
}

void TickGroup(Group& g, RE::Actor* a_lead, bool a_changed)
{
	if (!gLoaded || !a_lead || g.assignedTarget < 0) return;
	const auto node = std::uint32_t(g.assignedTarget);
	const GraphNode* n = Graph::Node(node);
	if (!n) return;
	if (a_changed) for (auto& m : g.members) ReleaseMember(m.id);
	if (!Online::LeaderNear(g, *n)) return;   // still walking in
	if (GroupBusy(g)) { gJobs[node].at = -1e9f; for (auto& m : g.members) ReleaseMember(m.id); return; }   // a fight ends every job
	Scan(node, *n, a_lead);
	auto& pj = gJobs[node];
	const float hour = HourOfDay(Sim::Hours());
	const bool night = InSleepWindow(gCfg, hour);
	std::vector<const Group*> here;
	std::vector<std::uint64_t> ids;
	std::size_t living = 0;
	for (auto& o : Sim::Groups())
		if (o.online && o.slot >= 0 && o.assignedTarget == g.assignedTarget && o.action != Action::Reach && IsHumanFaction(o.faction) && !Companions::Has(o.id)) {
			here.push_back(&o);
			ids.push_back(o.id);
			living += std::size_t(o.livingCount());
		}
	if (a_changed || gClock - pj.at >= gCfg.reassignSeconds || night != pj.night || living != pj.living || ids != pj.groups) {
		pj.assign = AssignJobs(*n, here, hour, Sim::Cfg(), gCfg, &pj.assign);
		pj.at = gClock;
		pj.night = night;
		pj.living = living;
		pj.groups = ids;
	}
	std::unordered_set<RE::FormID> bedsTaken;
	for (auto& m : g.members) {
		if (!m.alive()) continue;
		auto* a = Pool::ActorOf(m);
		if (!a || a->IsDead()) continue;
		const CampAssign* want = nullptr;
		for (auto& x : pj.assign) if (x.member == m.id) want = &x;
		if (!want) continue;
		if (a == a_lead && want->job == CampJob::Patrol) { ReleaseMember(m.id); continue; }   // StayDirector walks the leader
		if (!Apply(g, m, a, a_lead, *n, node, *want, bedsTaken)) break;   // package budget for this tick
	}
}

std::string Debug()
{
	std::string out = std::string("[camp] ") + (gLoaded ? "on" : "off") + ", " + std::to_string(gApplied.size()) + " members on jobs, " +
					  std::to_string(gScenes.size()) + " fires with company";
	for (auto& [mid, ap] : gApplied) {
		const Group* g = Sim::FindGroup(ap.group);
		const GraphNode* n = Graph::Node(ap.node);
		std::string name;
		if (g) for (auto& m : g->members) if (m.id == mid) name = m.name;
		out += "\n  " + name + " (" + (g ? FactionId(g->faction) : "?") + ") at " + (n ? n->name : "?") + ": " + CampJobId(ap.job) + " #" + std::to_string(ap.index) +
			   (ap.marker >= 0 ? " marker " + std::to_string(ap.marker) : "");
	}
	return out;
}
}  // namespace ALife::CampLife
