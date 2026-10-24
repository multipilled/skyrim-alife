// Headless tests for Civil War progress and faction gates (docs/CIVILWAR.md, T10) over the real Skyrim map and configs.
// Build: tests\build_civilwar_test.bat ; run: civilwar_test.exe [config dir] [world tsv]
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ALife/Board.h"
#include "ALife/Config.h"
#include "ALife/World.h"

using namespace ALife;

static int gFails = 0, gPass = 0;
#define CHECK(cond, msg) do { if (cond) { ++gPass; } else { ++gFails; std::printf("FAIL: %s   [%s]\n", msg, #cond); } } while (0)

namespace {
	constexpr FormID kWhiterunHold = 0x16772, kEastmarchHold = 0x1676A, kHaafingarHold = 0x16770;
	const auto kImp = Faction::Imperial, kSons = Faction::Stormcloak;

	struct TestHost : Host
	{
		std::vector<std::string> news, log;
		Member MakeMember(Faction f, Tier, Role role, int level, bool, Rng&, std::uint64_t id) override
		{
			Member m;
			m.id = id; m.faction = f; m.role = role; m.level = level;
			m.name = std::string(FactionId(f)) + "#" + std::to_string(id);
			m.health = m.healthMax = 100.f;
			return m;
		}
		float MemberPower(const Member& m) const override { return 10.f + 6.f * float(m.level); }
		void News(const std::string& l, float, float) override { news.push_back(l); }
		void Log(const std::string& l) override { log.push_back(l); }
	};

	bool LoadWorld(const std::string& path, std::vector<LocationDef>& out)
	{
		std::ifstream in(path);
		std::string line;
		while (std::getline(in, line)) {
			std::vector<std::string> f;
			std::string cur;
			std::stringstream ss(line);
			while (std::getline(ss, cur, '\t')) f.push_back(cur);
			if (f.size() < 9) continue;
			LocationDef d;
			d.id = FormID(std::stoul(f[0])); d.parent = FormID(std::stoul(f[1])); d.hold = FormID(std::stoul(f[2]));
			d.marker = FormID(std::stoul(f[3]));
			d.x = std::stof(f[4]); d.y = std::stof(f[5]); d.z = std::stof(f[6]); d.radius = std::stof(f[7]);
			d.name = f[8];
			if (f.size() > 9) for (auto& k : SplitList(f[9])) d.keywords.push_back(FormID(std::stoul(k)));
			out.push_back(std::move(d));
		}
		return !out.empty();
	}

	bool IsGuard(const Config& c, const Group& g)
	{
		for (auto& e : c.guardEntries) if (g.respawnEntry == e) return true;
		return false;
	}

	// guard squads (living, from a guard entry) of a faction whose spawning place lies in a hold
	int Guards(const Board& b, const World& w, const Config& c, FormID hold, Faction f, bool offlineOnly = false)
	{
		int n = 0;
		for (auto& g : b.squads)
			if (g.faction == f && g.livingCount() > 0 && IsGuard(c, g) && g.respawnSmart >= 0 && w.nodes[std::size_t(g.respawnSmart)].hold == hold &&
				w.nodes[std::size_t(g.respawnSmart)].holdAligned && (!offlineOnly || !g.online))
				++n;
		return n;
	}

	int Squads(const Board& b, Faction f, int ws, bool offlineOnly = false)
	{
		int n = 0;
		for (auto& g : b.squads) n += g.faction == f && g.ws == ws && g.livingCount() > 0 && (!offlineOnly || !g.online);
		return n;
	}

	void Run(Board& b, const Config& c, double hours) { for (double t = 0; t < hours; t += c.stepHours) b.Step(c.stepHours); }
}

int main(int argc, char** argv)
{
	const std::string cfgDir = argc > 1 ? argv[1] : "config";
	const std::string worldPath = argc > 2 ? argv[2] : "../data/world_skyrim.tsv";
	Config cfg;
	std::string err;
	if (!LoadConfig(cfgDir, cfg, err)) { std::printf("config load failed: %s\n", err.c_str()); return 2; }
	std::vector<LocationDef> defs;
	if (!LoadWorld(worldPath, defs)) { std::printf("cannot read %s\n", worldPath.c_str()); return 2; }
	World world;
	world.BuildNodes(defs);
	world.Finalize(cfg);

	// --- config
	CHECK(cfg.civilWar, "civil war on by default");
	CHECK(cfg.cwSides[0] == std::uint8_t(kImp) && cfg.cwSides[1] == std::uint8_t(kSons), "sides imperial / stormcloak");
	CHECK(cfg.standDownWs[0] && !cfg.standDownWs[1], "loser stands down in Tamriel only");
	int dgGates = 0;
	bool newOrderGate = false;
	for (auto& g : cfg.gates) {
		dgGates += g.faction == Faction::Dawnguard;
		newOrderGate |= g.faction == Faction::Dawnguard && g.plugin == "Dawnguard.esm" && g.quest == 0x0098CB && g.stage == 5;
	}
	// not DLC1HunterBaseEnableSet1 200: a shut-down stage, the quest stops and reads stage 0 again (W1 lab run t10g-0620)
	CHECK(dgGates == 1 && newOrderGate, "dawnguard gate parsed (DLC1VQ03Hunter 5)");

	// --- the map: Whiterun hold's places are Imperial at the start
	int whiterunPlaces = 0, whiterunGuardEntries = 0;
	std::uint32_t whiterunFort = UINT32_MAX;
	for (auto& n : world.nodes) {
		if (n.hold != kWhiterunHold || !n.holdAligned) continue;
		++whiterunPlaces;
		CHECK(n.defaultFaction == std::uint8_t(kImp), "Whiterun hold place starts Imperial");
		for (auto& rp : n.respawn) whiterunGuardEntries += rp.owner || rp.faction == 254 /* controller */;
		if (n.kind == Kind::Fort && n.factionControlled && whiterunFort == UINT32_MAX) whiterunFort = n.id;
	}
	std::printf("Whiterun hold: %d hold places, %d owner/controller respawn entries, fort node %u\n", whiterunPlaces, whiterunGuardEntries, whiterunFort);
	CHECK(whiterunPlaces >= 3, "Whiterun hold has hold-aligned places");

	TestHost host;
	cfg.ResetLiveRelations();
	Board b(cfg, world, host);
	b.Reset(24.0 * 3 + 9.0);
	b.rng = Rng(7);
	b.worldSeed = Mix64(7);
	b.StartFill();
	Run(b, cfg, 24.0);

	// --- (a) a hold flips: places, bonus, owner respawns, controllers
	const int impGuardsBefore = Guards(b, world, cfg, kWhiterunHold, kImp);
	const int impSquadsBefore = Squads(b, kImp, 0);
	std::printf("before flip: Imperial guards in Whiterun hold %d, Imperial squads in Tamriel %d, Stormcloak guards there %d\n", impGuardsBefore, impSquadsBefore,
	            Guards(b, world, cfg, kWhiterunHold, kSons));
	CHECK(impGuardsBefore > 0, "Imperial guard squads at Whiterun hold before the flip");
	CHECK(b.HoldOwner(kWhiterunHold) == std::uint8_t(kImp), "Whiterun hold starts Imperial");
	CHECK(!b.SetHoldOwner(kWhiterunHold, kImp), "setting the same owner changes nothing");
	CHECK(!b.SetHoldOwner(0x12345, kSons), "an unknown hold is ignored");
	CHECK(!b.SetHoldOwner(kWhiterunHold, Faction::Bandit), "only a Civil War side can own a hold");

	// put one Imperial guard squad on screen: it is left alone until it goes offline
	Group* onl = nullptr;
	for (auto& g : b.squads)
		if (g.faction == kImp && IsGuard(cfg, g) && g.respawnSmart >= 0 && world.nodes[std::size_t(g.respawnSmart)].hold == kWhiterunHold && g.livingCount() > 0) { onl = &g; break; }
	const std::uint64_t onlId = onl ? onl->id : 0;
	if (onl) onl->online = true;

	// the fort may have fallen to outlaws in the first day (the RNG stream follows the config): that holder keeps it
	const std::uint8_t fortBefore = whiterunFort != UINT32_MAX ? b.smart[whiterunFort].faction : kNoFaction;
	CHECK(b.SetHoldOwner(kWhiterunHold, kSons), "Whiterun hold flips to the Stormcloaks");
	bool allSons = true, bonusMoved = true, ownerEntries = true, controllers = true;
	for (auto& n : world.nodes) {
		if (n.hold != kWhiterunHold || !n.holdAligned) continue;
		allSons &= n.defaultFaction == std::uint8_t(kSons);
		if (n.ownerBonus > 0.f && n.kind == Kind::City) bonusMoved &= n.props.byFaction[int(kSons)] >= n.ownerBonus;
		for (auto& rp : n.respawn) if (rp.owner) ownerEntries &= rp.faction == std::uint8_t(kSons);
		const auto f = b.smart[n.id].faction;
		controllers &= !(f == std::uint8_t(kImp));
	}
	CHECK(allSons, "every Whiterun hold place now defaults to the Stormcloaks");
	CHECK(bonusMoved, "the owner bonus moved to the Stormcloaks");
	CHECK(ownerEntries, "owner respawn entries spawn Stormcloaks");
	CHECK(controllers, "no Whiterun hold place is still held by the Imperials");
	CHECK(b.stats.holdFlips == 1 && !host.news.empty(), "one hold flip, with news");
	if (whiterunFort != UINT32_MAX) {
		CHECK(cfg.IsEnemy(kImp, Faction(b.smart[whiterunFort].faction)), "the Whiterun fort is enemy-held for an Imperial squad");
		if (fortBefore == kNoFaction || cfg.IsCwSide(fortBefore))
			CHECK(b.smart[whiterunFort].faction == std::uint8_t(kSons), "the Whiterun fort is Stormcloak-held");
		else
			CHECK(b.smart[whiterunFort].faction == fortBefore, "an outlaw-held Whiterun fort stays its holder's");
	}

	// --- (c) guards switch sides off screen
	b.Step(cfg.stepHours);
	const int impGuardsOffline = Guards(b, world, cfg, kWhiterunHold, kImp, true);
	const int sonsGuards = Guards(b, world, cfg, kWhiterunHold, kSons);
	std::printf("after flip: Imperial guards offline %d (online kept %d), Stormcloak guards %d, swapped %llu\n", impGuardsOffline,
	            Guards(b, world, cfg, kWhiterunHold, kImp) - impGuardsOffline, sonsGuards, (unsigned long long)b.stats.guardsSwapped);
	CHECK(impGuardsOffline == 0, "no off-screen Imperial guard squad left in Whiterun hold");
	CHECK(sonsGuards >= impGuardsBefore - 1, "the Stormcloaks took over the guard posts");
	CHECK(b.stats.guardsSwapped >= std::uint64_t(impGuardsBefore - 1), "guard swaps counted");
	CHECK(!onlId || b.Find(onlId) != nullptr, "the on-screen guard squad is left alone");
	if (Group* g = b.Find(onlId)) g->online = false;
	b.Step(cfg.stepHours);
	CHECK(Guards(b, world, cfg, kWhiterunHold, kImp) == 0, "the guard squad swaps once off screen");
	Run(b, cfg, 24.0 * 5);
	CHECK(Guards(b, world, cfg, kWhiterunHold, kImp) == 0, "no new Imperial guards in Whiterun hold after 5 days");
	CHECK(Squads(b, kImp, 0) > 0, "Imperial field squads go on elsewhere");

	// flip back
	CHECK(b.SetHoldOwner(kWhiterunHold, kImp), "Whiterun hold flips back");
	b.Step(cfg.stepHours);
	CHECK(Guards(b, world, cfg, kWhiterunHold, kSons, true) == 0, "Stormcloak guards hand Whiterun back");
	for (auto& g : b.squads)
		if (g.faction == kSons && IsGuard(cfg, g) && g.respawnSmart >= 0 && world.nodes[std::size_t(g.respawnSmart)].hold == kWhiterunHold && g.livingCount() > 0)
			std::printf("  left: %s #%llu entry %s at %s (%s), holder %d, default %d\n", g.section.c_str(), (unsigned long long)g.id, g.respawnEntry.c_str(),
			            world.nodes[std::size_t(g.respawnSmart)].name.c_str(), KindName(world.nodes[std::size_t(g.respawnSmart)].kind),
			            int(b.smart[std::size_t(g.respawnSmart)].faction), int(world.nodes[std::size_t(g.respawnSmart)].defaultFaction));

	// --- (b) the war ends: the Imperials win
	CHECK(b.SetHoldOwner(kEastmarchHold, kImp), "Eastmarch falls (Windhelm taken)");
	const int sonsTamrielBefore = Squads(b, kSons, 0);
	// two Stormcloak field bands (no guard post): one off screen, one on screen
	const std::uint32_t field = world.Nearest(150000.f, 30000.f, 0);   // near Windhelm
	Group* f1 = b.CreateSquad(field, "stormcloak_advanced");
	const std::uint64_t sonsFieldId = f1 ? f1->id : 0;
	Group* f2 = b.CreateSquad(field, "stormcloak_novice");
	const std::uint64_t sonsOnlineId = f2 ? f2->id : 0;
	if (f2) f2->online = true;
	CHECK(sonsFieldId && sonsOnlineId, "two Stormcloak field bands made");
	const std::uint64_t stoodBefore = b.stats.stoodDown;
	b.SetWarOver(std::uint8_t(kSons));
	CHECK(b.WarLoser() == std::uint8_t(kSons), "the Stormcloaks lost");
	CHECK(b.EffectiveOwner(kEastmarchHold) == std::uint8_t(kImp) && b.EffectiveOwner(0x1676B) == std::uint8_t(kImp), "every hold is the winner's");
	CHECK(b.Blocked(kSons, 0) && !b.Blocked(kSons, 1) && !b.Blocked(kImp, 0), "the loser is blocked in Tamriel only");
	b.Step(cfg.stepHours);
	std::printf("war over: Stormcloak squads in Tamriel %d -> %d (offline %d), stood down %llu, guards swapped %llu\n", sonsTamrielBefore, Squads(b, kSons, 0),
	            Squads(b, kSons, 0, true), (unsigned long long)b.stats.stoodDown, (unsigned long long)b.stats.guardsSwapped);
	CHECK(sonsTamrielBefore > 0, "there were Stormcloak squads to stand down");
	CHECK(Squads(b, kSons, 0, true) == 0, "no off-screen Stormcloak squad left in Tamriel");
	CHECK(b.Find(sonsFieldId) == nullptr, "the off-screen Stormcloak field band stood down");
	CHECK(b.Find(sonsOnlineId) != nullptr && b.Find(sonsOnlineId)->livingCount() > 0, "an on-screen Stormcloak band walks on until it is off screen");
	if (Group* g = b.Find(sonsOnlineId)) g->online = false;
	b.Step(cfg.stepHours);
	CHECK(b.Find(sonsOnlineId) == nullptr, "... and stands down once off screen");
	CHECK(b.stats.stoodDown >= stoodBefore + 2, "stand-downs counted");
	Run(b, cfg, 24.0 * 10);
	CHECK(Squads(b, kSons, 0) == 0, "no Stormcloak squads respawn in Tamriel for 10 days");
	CHECK(b.CreateSquad(world.Nearest(0, 0, 0), "stormcloak_novice") == nullptr, "a stood-down faction cannot be created");
	CHECK(Guards(b, world, cfg, kHaafingarHold, kImp) > 0, "the Legion still guards Haafingar");
	int formerSons = 0;   // Rift / Pale / Winterhold posts now Imperial
	for (FormID h : { FormID(0x1676B), FormID(0x1676C), FormID(0x1676D), kEastmarchHold }) formerSons += Guards(b, world, cfg, h, kImp);
	CHECK(formerSons > 0, "the Legion took over the Stormcloak holds' guard posts");

	// --- (d) gates: Dawnguard only after the rebuild
	TestHost h2;
	Board d(cfg, world, h2);
	d.Reset(24.0 * 3 + 9.0);
	CHECK(d.HoldOwner(kWhiterunHold) == std::uint8_t(kImp) && d.WarLoser() == kNoFaction, "a reset board starts from the config owners");
	bool resetPlaces = true;
	for (auto& n : world.nodes) if (n.hold == kEastmarchHold && n.holdAligned) resetPlaces &= n.defaultFaction == std::uint8_t(kSons);
	CHECK(resetPlaces, "reset puts the places back to their start owners");
	d.rng = Rng(11);
	d.worldSeed = Mix64(11);
	d.SetGated(Faction::Dawnguard, true);
	d.StartFill();
	const auto dgSpawned = [&] { return d.stats.spawned[int(Faction::Dawnguard)]; };   // Dawnguard bands die fast hunting necromancers
	CHECK(d.SquadCount(Faction::Dawnguard) == 0 && dgSpawned() == 0, "no Dawnguard squads at the start while the gate is shut");
	Run(d, cfg, 24.0 * 6);
	CHECK(dgSpawned() == 0, "no Dawnguard squads respawn while the gate is shut");
	d.SetGated(Faction::Dawnguard, false);
	Run(d, cfg, 24.0 * 6);
	std::printf("gate opened: %llu Dawnguard squads spawned in 6 days, %d alive\n", (unsigned long long)dgSpawned(), d.SquadCount(Faction::Dawnguard));
	CHECK(dgSpawned() > 0, "Dawnguard squads appear once the gate opens");
	for (int i = 0; i < 6 * 24 * 6 && d.SquadCount(Faction::Dawnguard) == 0; ++i) d.Step(cfg.stepHours);
	CHECK(d.SquadCount(Faction::Dawnguard) > 0, "a living Dawnguard squad to retire");
	d.SetGated(Faction::Dawnguard, true);
	d.Step(cfg.stepHours);
	CHECK(d.SquadCount(Faction::Dawnguard) == 0, "a shut gate retires existing Dawnguard squads (an old save before the rebuild)");

	// --- (e) a bandit-held fort stays the bandits' (docs/CIVILWAR.md:31), set by hand: no RNG decides who holds it
	CHECK(whiterunFort != UINT32_MAX, "Whiterun hold has a faction-controlled fort");
	if (whiterunFort != UINT32_MAX) {
		TestHost h3;
		Board e(cfg, world, h3);
		e.Reset(24.0 * 3 + 9.0);
		std::uint32_t cwPlace = UINT32_MAX;   // another Whiterun hold place, held by the Imperials
		for (auto& n : world.nodes)
			if (n.hold == kWhiterunHold && n.holdAligned && n.id != whiterunFort) { cwPlace = n.id; break; }
		CHECK(cwPlace != UINT32_MAX, "a second Whiterun hold place");
		e.smart[whiterunFort].faction = std::uint8_t(Faction::Bandit);
		if (cwPlace != UINT32_MAX) e.smart[cwPlace].faction = std::uint8_t(kImp);
		CHECK(e.HoldOwner(kWhiterunHold) == std::uint8_t(kImp), "fresh board: Whiterun hold is Imperial");
		CHECK(e.SetHoldOwner(kWhiterunHold, kSons), "fresh board: Whiterun hold flips to the Stormcloaks");
		CHECK(e.smart[whiterunFort].faction == std::uint8_t(Faction::Bandit), "a bandit-held Whiterun fort stays the bandits' after the flip");
		CHECK(world.nodes[whiterunFort].defaultFaction == std::uint8_t(kSons), "... while its default owner moves to the Stormcloaks");
		CHECK(cwPlace != UINT32_MAX && e.smart[cwPlace].faction == std::uint8_t(kSons), "an Imperial-held Whiterun place goes to the Stormcloaks");
		CHECK(e.SetHoldOwner(kWhiterunHold, kImp), "fresh board: Whiterun hold flips back");
		CHECK(e.smart[whiterunFort].faction == std::uint8_t(Faction::Bandit), "the bandit-held fort stays the bandits' after the flip back");
		CHECK(cwPlace != UINT32_MAX && e.smart[cwPlace].faction == std::uint8_t(kImp), "the Stormcloak-held place returns to the Imperials");
	}

	// --- (f) a flip and a flip back leave the fort (and every Whiterun hold place) at its default owner: default faction, owner
	// bonus, owner respawn entries and an Imperial-held fort's controller all return to what they were before the first flip
	if (whiterunFort != UINT32_MAX) {
		TestHost h4;
		Board f(cfg, world, h4);
		f.Reset(24.0 * 3 + 9.0);
		f.smart[whiterunFort].faction = std::uint8_t(kImp);   // set by hand: the garrison holds its fort
		struct Snap { std::uint8_t def; std::array<float, kFactionCount> props; std::vector<std::uint8_t> entries; };
		std::vector<Snap> before;
		for (auto& n : world.nodes) {
			if (n.hold != kWhiterunHold || !n.holdAligned) continue;
			Snap sn{ n.defaultFaction, n.props.byFaction, {} };
			for (auto& rp : n.respawn) sn.entries.push_back(rp.faction);
			before.push_back(sn);
		}
		const GraphNode& fort = world.nodes[whiterunFort];
		CHECK(fort.defaultFaction == std::uint8_t(kImp), "flip and back: the fort's default owner starts Imperial");
		CHECK(f.SetHoldOwner(kWhiterunHold, kSons), "flip and back: Whiterun hold flips to the Stormcloaks");
		CHECK(fort.defaultFaction == std::uint8_t(kSons) && f.smart[whiterunFort].faction == std::uint8_t(kSons), "flip and back: the fort is the Stormcloaks' after the flip");
		CHECK(f.SetHoldOwner(kWhiterunHold, kImp), "flip and back: Whiterun hold flips back");
		CHECK(f.HoldOwner(kWhiterunHold) == std::uint8_t(kImp), "flip and back: the hold owner is Imperial again");
		CHECK(fort.defaultFaction == std::uint8_t(kImp), "flip and back: the fort's default owner returns to the Imperials");
		CHECK(f.smart[whiterunFort].faction == std::uint8_t(kImp), "flip and back: the fort's controller returns to the Imperials");
		bool sameDefault = true, sameBonus = true, sameEntries = true;
		std::size_t k = 0;
		for (auto& n : world.nodes) {
			if (n.hold != kWhiterunHold || !n.holdAligned) continue;
			const Snap& sn = before[k++];
			sameDefault &= n.defaultFaction == sn.def;
			for (int i = 0; i < kFactionCount; ++i) sameBonus &= std::fabs(n.props.byFaction[std::size_t(i)] - sn.props[std::size_t(i)]) < 1e-4f;
			std::vector<std::uint8_t> now;
			for (auto& rp : n.respawn) now.push_back(rp.faction);
			sameEntries &= now == sn.entries;
		}
		CHECK(k == before.size() && sameDefault, "flip and back: every Whiterun hold place is back at its default owner");
		CHECK(sameBonus, "flip and back: the owner bonus is back where it started");
		CHECK(sameEntries, "flip and back: the owner respawn entries spawn Imperials again");
		CHECK(f.stats.holdFlips == 2, "flip and back: two hold flips counted");
	}

	std::printf("\ncivilwar_test: %d passed, %d failed\n", gPass, gFails);
	return gFails ? 1 : 0;
}
