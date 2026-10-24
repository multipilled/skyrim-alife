#include "ALife/Companions.h"
#include "ALife/Forms.h"
#include "ALife/Jobs.h"
#include "ALife/Log.h"
#include "ALife/Merchants.h"
#include "ALife/Rep.h"
#include "ALife/SaveFormat.h"
#include "ALife/Settings.h"
#include "ALife/Systems.h"
#include "ALife/StashesPlugin.h"
#include "ALife/Wounded.h"

// Co-save: every piece of simulated state. Form IDs are resolved on load so load-order changes are safe; places are saved
// by their location FormID, respawn quotas by entry name, so editing the .ltx files never corrupts an old save.
namespace ALife::Save {
namespace {
	constexpr std::uint32_t kUID = 'ALIF';
	constexpr std::uint32_t kRec = 'SIM2';
	using SaveFormat::kVer;   // SIM2 versions and the faction migration: ALife/SaveFormat.h
	using SaveFormat::kVerMutants;
	constexpr std::uint32_t kNewsRec = 'NEWS';
	constexpr std::uint32_t kNewsVer = 1;
	constexpr std::uint32_t kDragonRec = 'DRGN';
	constexpr std::uint32_t kDragonVer = 1;
	constexpr std::uint32_t kDiffRec = 'DIFF';   // T21 (docs/DIFFICULTY.md): the difficulty sliders this save is played with
	constexpr std::uint32_t kDiffVer = 1;
	// 205: nodes carry a worldspace (Solstheim);    // 204: a fifteenth faction (Khajiit caravans) resizes every per-faction table;    // 203: members carry levelBias / gearLevel (the world levels with the player); 202: standing (goodwill, notoriety, rumours; squad memory, toll)

	struct W
	{
		std::vector<std::uint8_t> b;
		template <class T> void v(const T& x) { auto* p = reinterpret_cast<const std::uint8_t*>(&x); b.insert(b.end(), p, p + sizeof(T)); }
		void s(const std::string& x) { v(std::uint32_t(x.size())); b.insert(b.end(), x.begin(), x.end()); }
		void forms(const std::vector<RE::FormID>& f) { v(std::uint32_t(f.size())); for (auto id : f) v(id); }
	};
	struct R
	{
		const SKSE::SerializationInterface* i;
		const std::vector<std::uint8_t>& b;
		std::size_t o{ 0 };
		bool ok{ true };
		template <class T> T v()
		{
			T x{};
			if (o + sizeof(T) > b.size()) { ok = false; return x; }
			std::memcpy(&x, b.data() + o, sizeof(T));
			o += sizeof(T);
			return x;
		}
		std::string s()
		{
			auto n = v<std::uint32_t>();
			if (!ok || o + n > b.size()) { ok = false; return {}; }
			std::string x(reinterpret_cast<const char*>(b.data() + o), n);
			o += n;
			return x;
		}
		RE::FormID f()
		{
			auto id = v<RE::FormID>();
			if (!id) return 0;
			RE::FormID out = 0;
			return i->ResolveFormID(id, out) ? out : 0;
		}
		std::vector<RE::FormID> forms()
		{
			std::vector<RE::FormID> out;
			auto n = v<std::uint32_t>();
			for (std::uint32_t k = 0; ok && k < n; ++k) if (auto id = f()) out.push_back(id);
			return out;
		}
	};

	// a graph node id <-> its location FormID; -1 / 0 = none
	void WNode(W& w, std::int64_t id)
	{
		auto* n = id >= 0 ? Graph::Node(std::uint32_t(id)) : nullptr;
		w.v(n ? n->location : RE::FormID(0));
	}
	std::int64_t RNode(R& r)
	{
		const RE::FormID loc = r.f();
		if (!loc) return -1;
		const auto id = Graph::ByLocation(loc);
		return id == UINT32_MAX ? -1 : std::int64_t(id);
	}

	void SaveCb(SKSE::SerializationInterface* a)
	{
		std::lock_guard l(Sim::Lock());
		if (!Sim::BoardReady()) return;
		Companions::BeforeSave();   // packs of companions on screen (gear handed over) into their records first
		auto& P = Sim::P();
		auto& b = Sim::B();
		W w;
		w.v(b.now); w.v(P.gateOpened); w.v(b.nextId); w.v(b.worldSeed);
		{
			const auto& st = Rep::S().state;
			w.v(std::uint8_t(kFactionCount));
			for (int f = 0; f < kFactionCount; ++f) w.v(st.goodwill[f]);
			w.v(st.notoriety);
			w.v(std::uint32_t(st.rumors.size()));
			for (auto& ru : st.rumors) { w.v(ru.faction); w.v(ru.delta); w.v(ru.dueHours); }
		}
		w.v(std::uint32_t(P.waitlist.size()));
		for (auto id : P.waitlist) w.v(id);
		w.v(std::uint32_t(P.queuedKills.size()));
		for (auto& [cell, n] : P.queuedKills) { w.v(cell); w.v(n); }

		// smart terrains: control, respawn clock and quota usage
		w.v(std::uint32_t(b.smart.size()));
		for (std::size_t i = 0; i < b.smart.size(); ++i) {
			const auto& n = Graph::Nodes()[i];
			const auto& st = b.smart[i];
			w.v(n.location); w.v(st.lastRespawn); w.v(st.faction); w.v(st.raidedUntil);
			w.v(std::uint32_t(n.respawn.size()));
			for (std::size_t k = 0; k < n.respawn.size(); ++k) {
				w.s(n.respawn[k].name);
				w.v(k < st.spawned.size() ? st.spawned[k] : 0);
			}
		}

		const auto merchants = Merchants::Snapshot();
		w.v(std::uint32_t(merchants.size()));
		for (auto& m : merchants) { w.v(m.chest); w.v(m.baselineGold); w.v(m.itemsBought); w.v(m.goldStolen); w.v(m.cycleStart); }

		auto& gs = b.squads;
		w.v(std::uint32_t(gs.size()));
		for (auto& g : gs) {
			w.v(g.id); w.v(g.faction); w.v(g.tier); w.s(g.section);
			w.v(g.x); w.v(g.y); w.v(g.z);
			WNode(w, g.node); WNode(w, g.prevNode);
			w.v(g.activity); w.v(g.actStep); w.v(g.actHours);
			w.v(g.action); WNode(w, g.assignedTarget); WNode(w, g.currentTarget); WNode(w, g.smartAssigned);
			w.v(g.stayStart); w.v(g.idleHours); w.v(g.locked); w.v(g.lockedUntil);
			WNode(w, g.respawnSmart); w.s(g.respawnEntry);
			w.v(g.retreatUntilHours); WNode(w, g.avoidNode); w.v(g.commonSquad);
			w.v(g.standing); w.v(g.letGoCount); w.v(g.foughtPlayer); w.v(g.attackedByPlayer); w.v(g.lastMetHours);
			w.v(g.trust); w.v(g.helpedByPlayer); w.v(g.startStrength); w.v(g.startMembers); w.v(g.memory); w.v(g.tollUntil);
			w.v(std::uint32_t(g.members.size()));
			for (auto& m : g.members) {
				w.v(m.id); w.s(m.name); w.s(m.title); w.v(m.faction); w.v(m.role); w.v(m.race); w.v(m.sex); w.v(m.classId); w.v(m.level); w.v(m.levelBias); w.v(m.gearLevel);
				w.v(m.skills);
				w.v(m.health); w.v(m.magicka); w.v(m.stamina); w.v(m.healthMax); w.v(m.magickaMax); w.v(m.staminaMax);
				w.v(m.gold);
				w.v(std::uint32_t(m.inventory.size()));
				for (auto& it : m.inventory) { w.v(it.form); w.v(it.count); }
				w.forms(m.equipped); w.forms(m.perks); w.forms(m.spells);
				w.v(m.hunger); w.v(m.cold); w.v(m.fatigue);
				w.v(m.sticky); w.v(m.veteran); w.v(m.waitlisted); w.v(m.poolLocal); w.v(m.growthDay); w.v(m.gainToday);
				w.v(m.fightBonusToday); w.v(m.rankXp); w.v(m.kills); w.v(m.arrows);
			}
		}
		w.forms(Creatures::Refs());   // 206: monster creatures in the world right now (the next load deletes them: squads load offline)
		if (!a->WriteRecord(kRec, kVer, w.b.data(), std::uint32_t(w.b.size())))
			Log::Line("[save] FAILED to write co-save record");
		else
			Log::Line("[save] co-save " + std::to_string(w.b.size()) + " bytes, squads=" + std::to_string(gs.size()));
		Jobs::SaveTo(a);   // record 'TASK' (docs/TASKS.md): SIM2 keeps its layout, a save without TASK simply has no jobs
		Companions::SaveTo(a);   // record 'COMP' (docs/COMPANIONS.md): a save without it has no companions
		Wounded::SaveTo(a);      // record 'WOND' (docs/WOUNDED.md): a save without it has nobody down; SIM2 keeps its layout (208)
		StashPlugin::SaveTo(a);  // record 'STSH' (docs/STASHES.md): a save without it builds the stash spots afresh
		{   // record 'NEWS' (M5g): the News page and the live relation matrix; a save without it starts both afresh
			W n;
			const auto lines = Rep::NewsAll();
			n.v(std::uint32_t(lines.size()));
			for (auto& line : lines) n.s(line);
			const auto& cfg = Sim::Cfg();
			n.v(std::uint32_t(kFactionCount));
			for (int x = 0; x < kFactionCount; ++x)
				for (int y = 0; y < kFactionCount; ++y) n.v(std::int32_t(cfg.relLive[x][y]));
			if (!a->WriteRecord(kNewsRec, kNewsVer, n.b.data(), std::uint32_t(n.b.size()))) Log::Line("[save] FAILED to write the news record");
			else Log::Line("[save] news record: " + std::to_string(lines.size()) + " lines");
		}
		{   // record 'DRGN' (docs/DRAGONS.md): the dragon respawn timer and the player's dragon kills; a save without it starts the timer at the load
			W d;
			d.v(b.dragonLast);
			d.v(b.dragonKills);
			if (!a->WriteRecord(kDragonRec, kDragonVer, d.b.data(), std::uint32_t(d.b.size()))) Log::Line("[save] FAILED to write the dragon record");
		}
		{   // record 'DIFF' v1: follow flag, slider count, sliders (DiffChannel order). A save without it plays with the ini's
			W d;
			const auto& c = Settings::Get().diff;
			d.v(std::uint8_t(c.followGame ? 1 : 0));
			d.v(std::uint8_t(kDiffChannels));
			for (float x : c.slider) d.v(x);
			if (!a->WriteRecord(kDiffRec, kDiffVer, d.b.data(), std::uint32_t(d.b.size()))) Log::Line("[save] FAILED to write the difficulty record");
		}
	}

	void LoadCb(SKSE::SerializationInterface* a)
	{
		std::uint32_t type, ver, len;
		Rep::RestoreNews({});
		bool sawDragons = false;
		Sim::Cfg().ResetLiveRelations();   // the static matrix unless the save carries a live one
		bool diffLoaded = false;
		while (a->GetNextRecordInfo(type, ver, len)) {
			if (type == kDiffRec && ver == kDiffVer) {
				std::vector<std::uint8_t> buf(len);
				if (a->ReadRecordData(buf.data(), len) != len) continue;
				R r{ a, buf };
				DifficultyChoice c = Settings::Get().diff;
				c.followGame = r.v<std::uint8_t>() != 0;
				const int n = r.v<std::uint8_t>();
				for (int k = 0; r.ok && k < n; ++k) {   // a later version's extra channels are skipped, missing ones keep the ini's
					const float x = r.v<float>();
					if (k < kDiffChannels) c.slider[k] = ClampSlider(Sim::Cfg().diffCfg, DiffChannel(k), x);
				}
				if (!r.ok) continue;
				Settings::Get().diff = c;
				diffLoaded = true;
				continue;
			}
			if (type == 'TASK') { Jobs::LoadFrom(a, ver, len); continue; }
			if (type == 'COMP') { Companions::LoadFrom(a, ver, len); continue; }
			if (type == kDragonRec) {   // docs/DRAGONS.md: the respawn timer and the player's dragon kills (an unknown version is skipped)
				std::vector<std::uint8_t> buf(len);
				if (ver != kDragonVer || a->ReadRecordData(buf.data(), len) != len) continue;
				R r{ a, buf };
				const double last = r.v<double>();
				const auto kills = r.v<std::uint32_t>();
				std::lock_guard l(Sim::Lock());
				if (r.ok && Sim::BoardReady()) { Sim::B().dragonLast = last; Sim::B().dragonKills = kills; sawDragons = true; }
				continue;
			}
			if (type == 'WOND') { Wounded::LoadFrom(a, ver, len); continue; }
			if (type == 'STSH') { StashPlugin::LoadFrom(a, ver, len); continue; }
			if (type == kNewsRec && ver == kNewsVer) {
				std::vector<std::uint8_t> buf(len);
				if (a->ReadRecordData(buf.data(), len) != len) continue;
				R r{ a, buf };
				std::vector<std::string> lines;
				for (auto n = r.v<std::uint32_t>(); r.ok && n; --n) lines.push_back(r.s());
				const auto nf = r.v<std::uint32_t>();
				std::vector<std::int32_t> m;
				for (std::uint32_t k = 0; r.ok && k < nf * nf; ++k) m.push_back(r.v<std::int32_t>());
				if (!r.ok) continue;
				Rep::RestoreNews(lines);
				const bool rel = !m.empty() && SaveFormat::RestoreRelations(Sim::Cfg().relLive, m.data(), nf);   // a 31-faction table: dragons keep the static row
				Log::Line("[save] news record: " + std::to_string(lines.size()) + " lines restored" + (rel ? "" : ", relation matrix not used (" + std::to_string(nf) + " factions)"));
				continue;
			}
			if (type != kRec || !SaveFormat::Loads(ver)) continue;   // older layouts are not migrated: the world is populated afresh
			std::vector<std::uint8_t> buf(len);
			if (a->ReadRecordData(buf.data(), len) != len) continue;
			std::lock_guard l(Sim::Lock());
			if (!Graph::Ready()) continue;
			R r{ a, buf };
			auto Fac = [ver](std::uint8_t f) { return SaveFormat::FactionOf(ver, f); };   // squads, members, place owners: monsters shifted before 208
			auto& P = Sim::P();
			auto& b = Sim::B();
			P = Sim::Persist{};
			b.Reset(0);
			b.now = r.v<double>(); P.gateOpened = r.v<bool>(); b.nextId = r.v<std::uint64_t>(); b.worldSeed = r.v<std::uint64_t>();
			SaveFormat::ReadStanding(r, ver, Rep::S().state);   // goodwill table, notoriety, rumours (monsters shifted past the Solstheim three before 208)
			for (auto n = r.v<std::uint32_t>(); r.ok && n; --n) P.waitlist.push_back(r.v<std::uint64_t>());
			for (auto n = r.v<std::uint32_t>(); r.ok && n; --n) { auto c = r.f(); auto k = r.v<std::int32_t>(); if (c) P.queuedKills[c] = k; }

			for (auto ns = r.v<std::uint32_t>(); r.ok && ns; --ns) {
				const RE::FormID loc = r.f();
				const double lastRespawn = r.v<double>();
				const std::uint8_t faction = r.v<std::uint8_t>();
				const double raidedUntil = r.v<double>();
				const auto id = loc ? Graph::ByLocation(loc) : UINT32_MAX;
				std::vector<std::pair<std::string, int>> spawned;
				for (auto ne = r.v<std::uint32_t>(); r.ok && ne; --ne) { auto name = r.s(); auto c = r.v<std::int32_t>(); spawned.emplace_back(name, c); }
				if (id == UINT32_MAX || id >= b.smart.size()) continue;
				auto& st = b.smart[id];
				st.lastRespawn = lastRespawn;
				st.faction = std::uint8_t(Fac(faction));
				st.raidedUntil = raidedUntil;
				const auto& n = Graph::Nodes()[id];
				st.spawned.assign(n.respawn.size(), 0);
				for (std::size_t k = 0; k < n.respawn.size(); ++k)
					for (auto& [nm, c] : spawned) if (nm == n.respawn[k].name) st.spawned[k] = c;
			}

			{
				std::vector<Merchants::State> ms;
				for (auto nm = r.v<std::uint32_t>(); r.ok && nm; --nm) {
					Merchants::State m;
					m.chest = r.f();
					m.baselineGold = r.v<std::int32_t>(); m.itemsBought = r.v<std::int32_t>(); m.goldStolen = r.v<std::int32_t>(); m.cycleStart = r.v<double>();
					if (m.chest) ms.push_back(m);
				}
				Merchants::Restore(ms);
			}

			b.squads.clear();
			for (auto ng = r.v<std::uint32_t>(); r.ok && ng; --ng) {
				Group g;
				g.id = r.v<std::uint64_t>(); g.faction = Fac(r.v<std::uint8_t>()); g.tier = r.v<Tier>(); g.section = r.s();
				g.x = r.v<float>(); g.y = r.v<float>(); g.z = r.v<float>();
				g.node = std::uint32_t(std::max<std::int64_t>(0, RNode(r))); g.prevNode = std::uint32_t(std::max<std::int64_t>(0, RNode(r)));
				if (auto* nd = Graph::Node(g.node)) g.ws = nd->ws;   // squads never change worldspace: it is the node's
				g.activity = r.v<Act>(); g.actStep = r.v<std::int32_t>(); g.actHours = r.v<float>();
				g.action = r.v<Action>(); g.assignedTarget = RNode(r); g.currentTarget = RNode(r); g.smartAssigned = RNode(r);
				g.stayStart = r.v<double>(); g.idleHours = r.v<double>(); g.locked = r.v<bool>(); g.lockedUntil = r.v<double>();
				g.respawnSmart = RNode(r); g.respawnEntry = r.s();
				g.retreatUntilHours = r.v<double>(); g.avoidNode = RNode(r); g.commonSquad = r.v<bool>();
				g.standing = r.v<std::int32_t>(); g.letGoCount = r.v<std::int32_t>(); g.foughtPlayer = r.v<bool>(); g.attackedByPlayer = r.v<bool>();
				g.lastMetHours = r.v<double>(); g.trust = r.v<Trust>(); g.helpedByPlayer = r.v<bool>(); g.startStrength = r.v<float>();
				g.startMembers = r.v<std::int32_t>(); g.memory = r.v<float>(); g.tollUntil = r.v<double>();
				for (auto nm = r.v<std::uint32_t>(); r.ok && nm; --nm) {
					Member m;
					m.id = r.v<std::uint64_t>(); m.name = r.s(); m.title = r.s(); m.faction = Fac(r.v<std::uint8_t>()); m.role = r.v<Role>();
					m.race = r.v<RaceId>(); m.sex = r.v<Sex>(); m.classId = r.f(); m.level = r.v<std::int32_t>(); m.levelBias = r.v<std::int32_t>(); m.gearLevel = r.v<std::int32_t>();
					m.skills = r.v<decltype(m.skills)>();
					m.health = r.v<float>(); m.magicka = r.v<float>(); m.stamina = r.v<float>();
					m.healthMax = r.v<float>(); m.magickaMax = r.v<float>(); m.staminaMax = r.v<float>();
					m.gold = r.v<std::int32_t>();
					for (auto ni = r.v<std::uint32_t>(); r.ok && ni; --ni) {
						auto f = r.f(); auto c = r.v<std::int32_t>();
						if (f && c > 0) m.inventory.push_back({ f, c });
					}
					m.equipped = r.forms(); m.perks = r.forms(); m.spells = r.forms();
					m.hunger = r.v<float>(); m.cold = r.v<float>(); m.fatigue = r.v<float>();
					m.sticky = r.v<bool>(); m.veteran = r.v<bool>(); m.waitlisted = r.v<bool>(); m.poolLocal = r.v<std::int32_t>();
					m.growthDay = r.v<std::uint32_t>(); m.gainToday = r.v<decltype(m.gainToday)>();
					m.fightBonusToday = r.v<float>(); m.rankXp = r.v<float>(); m.kills = r.v<std::int32_t>(); m.arrows = r.v<std::int32_t>();
					g.members.push_back(std::move(m));
				}
				b.squads.push_back(std::move(g));
			}
			if (r.ok && ver >= kVerMutants) {
				std::vector<RE::FormID> refs;
				for (auto n = r.v<std::uint32_t>(); r.ok && n; --n) if (auto id = r.f()) refs.push_back(id);
				Creatures::QueueSweep(std::move(refs));
			}
			if (!r.ok) {
				Log::Line("[save] co-save truncated: starting clean");
				b.squads.clear();
				P = Sim::Persist{};
			} else {
				Log::Line("[save] loaded squads=" + std::to_string(b.squads.size()) + " (co-save v" + std::to_string(ver) + ")");
			}
		}
		if (!sawDragons) {   // a save from before M4c: the dragon timer starts now (the first dragon comes respawn_days later)
			std::lock_guard l(Sim::Lock());
			if (Sim::BoardReady()) Sim::B().dragonLast = Sim::B().now;
		}
		if (!diffLoaded) Settings::ReloadDifficulty();   // a save from before T21 (or without the record): the ini's sliders
		Sim::ApplySettings();
		Log::Line(std::string("[save] difficulty sliders from ") + (diffLoaded ? "the save" : "ALife.ini") + ": " + Sim::Cfg().diff.Describe());
	}

	void RevertCb(SKSE::SerializationInterface*)
	{
		std::lock_guard l(Sim::Lock());
		if (Sim::BoardReady() || Graph::Ready()) { Sim::Groups().clear(); }
		Sim::P() = Sim::Persist{};
	}
}

void Register()
{
	auto* s = SKSE::GetSerializationInterface();
	s->SetUniqueID(kUID);
	s->SetSaveCallback(SaveCb);
	s->SetLoadCallback(LoadCb);
	s->SetRevertCallback(RevertCb);
}
}
