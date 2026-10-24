// Headless tests for patrol-mod compatibility (docs/COMPAT.md): plugin detection with fake load orders, the actor budget and
// its MCM override, the hand-over of forts over the real Skyrim map, a 30-day board run with no outlaw squad at a handed-over
// fort, and the faction reactions. Build: tests\build_compat_test.bat ; run: compat_test.exe [config dir] [world.tsv]
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "ALife/Board.h"
#include "ALife/Compat.h"
#include "ALife/Config.h"
#include "ALife/Util.h"
#include "ALife/World.h"

using namespace ALife;

static int gFails = 0, gPass = 0;
#define CHECK(cond, msg) do { if (cond) { ++gPass; } else { ++gFails; std::printf("FAIL: %s   [%s]\n", msg, #cond); } } while (0)

namespace {
	struct TestHost : Host
	{
		Member MakeMember(Faction f, Tier, Role role, int level, bool, Rng&, std::uint64_t id) override
		{
			Member m;
			m.id = id; m.faction = f; m.role = role; m.level = level;
			m.name = std::string(FactionId(f)) + "#" + std::to_string(id);
			m.health = m.healthMax = 80.f + 8.f * float(level);
			return m;
		}
		float MemberPower(const Member& m) const override { return 10.f + 6.f * float(m.level); }
	};

	bool LoadWorld(const std::string& path, std::vector<LocationDef>& out)
	{
		std::ifstream in(path);
		if (!in) return false;
		std::string line;
		while (std::getline(in, line)) {
			if (line.empty()) continue;
			std::vector<std::string> f;
			std::string cur;
			std::stringstream ss(line);
			while (std::getline(ss, cur, '\t')) f.push_back(cur);
			if (f.size() < 9) continue;
			LocationDef d;
			d.id = FormID(std::stoul(f[0]));
			d.parent = FormID(std::stoul(f[1]));
			d.hold = FormID(std::stoul(f[2]));
			d.marker = FormID(std::stoul(f[3]));
			d.x = std::stof(f[4]); d.y = std::stof(f[5]); d.z = std::stof(f[6]); d.radius = std::stof(f[7]);
			d.name = f[8];
			if (f.size() > 9) for (auto& k : SplitList(f[9])) d.keywords.push_back(FormID(std::stoul(k)));
			out.push_back(std::move(d));
		}
		return !out.empty();
	}

	bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

	std::string DefaultConfigDir()
	{
		std::string f = __FILE__;   // ...\tests\compat_test.cpp -> ...\config
		const auto p = f.find_last_of("\\/");
		f = f.substr(0, p);
		const auto q = f.find_last_of("\\/");
		return f.substr(0, q) + "/config";
	}

	const GraphNode* ByLoc(const World& w, FormID loc)
	{
		for (auto& n : w.nodes) if (n.location == loc) return &n;
		return nullptr;
	}
}

static void TestPatterns()
{
	CHECK(PluginMatches("Immersive Patrols*.esp", "Immersive Patrols II.esp"), "IP plugin matches");
	CHECK(PluginMatches("immersive patrols*.esp", "IMMERSIVE PATROLS II.ESP"), "case-insensitive");
	CHECK(!PluginMatches("Immersive Patrols*.esp", "TS - Immersive Patrols Addon - Civil War.esp"), "an add-on is not the mod");
	CHECK(PluginMatches("Skyrim at War*.esp", "Skyrim at War Reborn - 2000.esp"), "SaW size variant matches");
	CHECK(!PluginMatches("Skyrim at War*.esp", "Skyrim at War Reborn.esm"), "extension respected");
	CHECK(PluginMatches("a*b*c", "aXXbYYc") && !PluginMatches("a*b*c", "aXXbYY"), "multi-star");
	CHECK(!PluginMatches("", "anything.esp"), "empty pattern never matches");
}

static void TestResolve(const Config& cfg)
{
	const auto& cc = cfg.compatCfg;
	CHECK(cc.mods.size() == 2, "compat.ltx lists two mods");
	CHECK(cc.factionMap.size() == 2, "compat.ltx maps the CW pair");

	const std::vector<std::string> vanilla = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "ALife.esp", "ALifeFactions.esp" };
	auto none = ResolveCompat(cc, vanilla);
	CHECK(!none.Active() && !none.handover && none.factionMap.empty(), "nothing detected on a vanilla load order");
	CHECK(none.onlineGroups == kMaxOnlineGroups && none.onlineActors == kMaxOnlineActors && Near(none.population, 1.f), "full budget without mods");

	auto lo = vanilla;
	lo.push_back("TS - Immersive Patrols Addon - Civil War.esp");
	CHECK(!ResolveCompat(cc, lo).Active(), "an add-on alone does not count");

	auto ip = vanilla;
	ip.push_back("Immersive Patrols II.esp");
	auto sIp = ResolveCompat(cc, ip);
	CHECK(sIp.Detected("immersive_patrols") && !sIp.Detected("skyrim_at_war"), "IP detected");
	CHECK(sIp.matched.size() == 1 && sIp.matched[0] == "Immersive Patrols II.esp", "IP: matched file recorded");
	CHECK(!sIp.handover, "IP hands over nothing (patrols only)");
	CHECK(sIp.onlineGroups == 2 && sIp.onlineActors == 12, "IP budget 2 / 12");
	CHECK(Near(sIp.population, 0.85f) && Near(sIp.patrolScale, 0.6f), "IP population / patrol scale");
	CHECK(sIp.factionMap.size() == 2, "faction map active with any mod");

	auto saw = vanilla;
	saw.push_back("Skyrim at War Reborn - 2000.esp");
	auto sSaw = ResolveCompat(cc, saw);
	CHECK(sSaw.Detected("skyrim_at_war") && sSaw.handover, "SaW detected, hands over");
	CHECK(sSaw.onlineGroups == 2 && sSaw.onlineActors == 9 && Near(sSaw.population, 0.6f), "SaW budget 2 / 9 / 0.6");

	auto both = saw;
	both.push_back("Immersive Patrols II.esp");
	auto sBoth = ResolveCompat(cc, both);
	CHECK(sBoth.detected.size() == 2, "both detected");
	CHECK(sBoth.onlineGroups == 2 && sBoth.onlineActors == 9, "both: lowest caps");
	CHECK(Near(sBoth.population, 0.51f) && Near(sBoth.patrolScale, 0.3f), "both: multipliers multiply");

	auto off = ResolveCompat(cc, both, kCompatBudgetOff);
	CHECK(off.Active() && off.handover && off.onlineGroups == 3 && off.onlineActors == 15 && Near(off.population, 1.f) && Near(off.patrolScale, 1.f),
		"MCM Off: full budget, hand-over stays");
	auto one = ResolveCompat(cc, both, 1);
	CHECK(one.onlineGroups == 1 && one.onlineActors == 5 && Near(one.population, 0.51f), "MCM 1 squad: 1 / 5, population still scaled");
	auto three = ResolveCompat(cc, both, 3);
	CHECK(three.onlineGroups == 3 && three.onlineActors == 15, "MCM 3 squads");
	{   // the on-screen budget with companions (AtOnlineBudget: Online::AtBudget, Companions make-room, the summon verb, Tick)
		auto two = ResolveCompat(cc, both, 2);
		const int b1 = one.onlineGroups, b2 = two.onlineGroups, b3 = three.onlineGroups;
		CHECK(b2 == 2, "MCM 2 squads: 2 groups");
		// make room (a companion squad needs a slot) / summon (a bounty target comes on screen): fires at the budget, not at 3
		CHECK(AtOnlineBudget(1, b1) && !AtOnlineBudget(0, b1), "budget 1: one squad on screen is full (make room / summon fire); none is not");
		CHECK(AtOnlineBudget(2, b2) && !AtOnlineBudget(1, b2), "budget 2: two squads on screen fire make room / summon, below 3; one does not");
		CHECK(!AtOnlineBudget(2, b3) && AtOnlineBudget(3, b3), "budget 3 (no patrol mod): the old 3-slot behaviour");
		// world squads (Tick): companions count and outrank them; slots kept for companions not placed yet count too
		CHECK(AtOnlineBudget(1, b1), "budget 1, one companion squad on screen: no world squad comes on screen");
		CHECK(AtOnlineBudget(0, b1, 1), "budget 1, a companion squad still to be placed after a load: its slot is kept");
		CHECK(AtOnlineBudget(1, b2, 1) && !AtOnlineBudget(1, b3, 1), "one companion on screen + one to place: full under 2 squads, room left under 3");
		CHECK(AtOnlineBudget(2, b2) && !AtOnlineBudget(1, b2), "two companion squads fill 2 squads; one leaves room for a world squad");
	}
	auto noHand = ResolveCompat(cc, both, kCompatBudgetAuto, false);
	CHECK(noHand.Active() && !noHand.handover, "bCompatHandover = 0");

	CompatConfig forced = cc;
	forced.force = { "skyrim_at_war" };
	auto sF = ResolveCompat(forced, vanilla);
	CHECK(sF.Detected("skyrim_at_war") && sF.matched[0] == "force", "force switches a mod on without its plugin");

	// runtime MCM change
	ApplyCompatBudget(cc, sBoth, 1);
	CHECK(sBoth.onlineGroups == 1 && sBoth.onlineActors == 5, "ApplyCompatBudget recomputes caps");
	ApplyCompatBudget(cc, none, 1);
	CHECK(none.onlineGroups == 3 && none.onlineActors == 15, "override is ignored without mods");

	// a bad config line is reported, not fatal
	Ltx bad;
	bad.LoadString("[compat]\nmods = x, y\n[compat_x]\nplugins = x.esp\nhandover_kinds = castle\nhandover = zz\n[compat_factions]\n2BF9A = nobody\n");
	CompatConfig bc;
	std::string err;
	LoadCompat(bad, bc, &err);
	CHECK(bc.mods.size() == 1 && bc.factionMap.empty(), "bad entries skipped");
	CHECK(err.find("castle") != std::string::npos && err.find("compat_y") != std::string::npos && err.find("nobody") != std::string::npos,
		"bad entries reported");
}

struct WorldRun
{
	int squads{ 0 }, outlawAtFort{ 0 }, spawnedAtFort{ 0 }, samples{ 0 };
};

static WorldRun RunBoard(Config& cfg, World& w, int days, std::uint64_t seed)
{
	TestHost host;
	cfg.ResetLiveRelations();
	Board b(cfg, w, host);
	b.Reset(24.0 * 3 + 9.0);
	b.rng = Rng(seed);
	b.worldSeed = Mix64(seed);
	b.StartFill();
	WorldRun r;
	auto sample = [&]() {
		++r.samples;
		for (auto& g : b.squads) {
			if (g.livingCount() <= 0) continue;
			auto at = [&](std::int64_t id) -> const GraphNode* {
				return id >= 0 && id < std::int64_t(w.nodes.size()) ? &w.nodes[std::size_t(id)] : nullptr;
			};
			for (std::int64_t id : { g.smartAssigned, std::int64_t(g.assignedTarget) }) {
				const GraphNode* n = at(id);
				if (!n || !n->handedOver) continue;
				const std::uint8_t owner = n->defaultFaction;
				const bool friendly = owner != kNoFaction && (int(g.faction) == owner || cfg.IsAlly(g.faction, Faction(owner)));
				if (!friendly) {
					if (r.outlawAtFort < 5)
						std::printf("  %s squad %llu targets handed-over %s (owner %s)\n", FactionId(g.faction), (unsigned long long)g.id,
							n->name.c_str(), owner == kNoFaction ? "none" : FactionId(Faction(owner)));
					++r.outlawAtFort;
				}
				break;
			}
			if (const GraphNode* n = at(g.respawnSmart); n && n->handedOver) ++r.spawnedAtFort;
		}
	};
	sample();
	const double end = b.now + days * 24.0;
	double next = b.now + 6.0;
	while (b.now + cfg.stepHours <= end) {
		b.Step(cfg.stepHours);
		if (b.now >= next) { sample(); next += 6.0; }
	}
	for (auto& g : b.squads) r.squads += g.livingCount() > 0;
	return r;
}

static void TestWorld(Config& cfg, const std::string& worldPath)
{
	std::vector<LocationDef> defs;
	if (!LoadWorld(worldPath, defs)) { std::printf("SKIP world tests: cannot read %s\n", worldPath.c_str()); return; }

	const std::vector<std::string> saw = { "Skyrim.esm", "Skyrim at War Reborn - 2000.esp" };
	const std::vector<std::string> ip = { "Skyrim.esm", "Immersive Patrols II.esp" };

	cfg.compat = CompatState{};
	World plain;
	plain.BuildNodes(defs);
	plain.Finalize(cfg);

	cfg.compat = ResolveCompat(cfg.compatCfg, ip);
	World ipw;
	ipw.BuildNodes(defs);
	ipw.Finalize(cfg);

	cfg.compat = ResolveCompat(cfg.compatCfg, saw);
	World held;
	held.BuildNodes(defs);
	held.Finalize(cfg);
	CHECK(plain.nodes.size() == held.nodes.size(), "same smarts with and without the mod");

	int forts = 0, handed = 0, bad = 0, untouched = 0, changedOther = 0, patrolScaled = 0, patrolTotal = 0;
	for (std::size_t i = 0; i < held.nodes.size(); ++i) {
		const auto &a = plain.nodes[i], &h = held.nodes[i], &p = ipw.nodes[i];
		CHECK(!a.handedOver && !p.handedOver, "no hand-over without SaW");
		for (std::size_t k = 0; k < a.respawn.size() && k < p.respawn.size(); ++k)
			if (a.respawn[k].name == "patrol" || a.respawn[k].name == "owner_patrol") {
				++patrolTotal;
				patrolScaled += Near(p.respawn[k].num, a.respawn[k].num * 0.6f);
			}
		if (h.kind != Kind::Fort) {
			if (h.handedOver || h.respawn.size() != a.respawn.size() || h.maxPopulation != a.maxPopulation) ++changedOther;
			continue;
		}
		++forts;
		if (h.location == 0x1916E) { untouched += !h.handedOver && !h.respawn.empty(); continue; }   // Driftshade Refuge: kept
		if (!h.handedOver) continue;
		++handed;
		const bool ok = h.respawn.empty() && !h.factionControlled && h.maxPopulation <= 1 && h.props.all == 0.f && h.props.monster == 0.f &&
		                h.props.byFaction[int(Faction::Bandit)] == 0.f && h.props.byFaction[int(Faction::Forsworn)] == 0.f &&
		                h.props.byFaction[int(Faction::Necromancer)] == 0.f &&
		                (h.defaultFaction == std::uint8_t(Faction::Imperial) || h.defaultFaction == std::uint8_t(Faction::Stormcloak));
		if (!ok) {
			++bad;
			std::printf("  fort %s: respawn=%zu ctrl=%d maxPop=%d owner=%d bandit=%.1f\n", h.name.c_str(), h.respawn.size(), int(h.factionControlled),
				h.maxPopulation, int(h.defaultFaction), h.props.byFaction[int(Faction::Bandit)]);
		}
	}
	std::printf("  world: %zu smarts, %d forts, %d handed over, %d patrol entries\n", held.nodes.size(), forts, handed, patrolTotal);
	CHECK(forts >= 10, "the map has forts");
	CHECK(handed == forts - untouched && handed > 0, "every fort but the kept one is handed over");
	CHECK(untouched == 1, "Driftshade Refuge stays the Silver Hand's");
	CHECK(bad == 0, "handed-over forts: no respawn, CW owner, outlaws zeroed, one squad");
	CHECK(changedOther == 0, "non-fort smarts untouched by the hand-over");
	CHECK(patrolTotal > 0 && patrolScaled == patrolTotal, "IP scales every patrol entry by 0.6");

	if (auto* g = ByLoc(held, 0x1917D)) {   // Fort Greymoor (Whiterun, Imperial at the start): bandits in vanilla, Legion with SaW
		CHECK(g->handedOver && g->defaultFaction == std::uint8_t(Faction::Imperial), "Fort Greymoor: Imperial garrison, no bandits");
		CHECK(g->props.byFaction[int(Faction::Imperial)] > 0.f && g->props.byFaction[int(Faction::Stormcloak)] == 0.f, "Fort Greymoor: Legion may visit, Stormcloaks not");
	} else CHECK(false, "Fort Greymoor in the world");

	// 30 days on the real map: no outlaw / enemy squad ever targets or sits at a handed-over fort, none spawns there
	cfg.compat = CompatState{};
	cfg.populationFactor = 1.f;
	const WorldRun base = RunBoard(cfg, plain, 30, 7);
	cfg.compat = ResolveCompat(cfg.compatCfg, saw);
	cfg.populationFactor = 1.f * cfg.compat.population;   // what Sim::ApplySettings does with the MCM density
	const WorldRun run = RunBoard(cfg, held, 30, 7);
	std::printf("  30 days: without SaW %d squads, with SaW %d squads (population x%.2f); %d samples, outlaw-at-fort %d, spawned-at-fort %d\n",
		base.squads, run.squads, cfg.compat.population, run.samples, run.outlawAtFort, run.spawnedAtFort);
	CHECK(run.squads > 50, "the world still lives with SaW");
	CHECK(run.squads < base.squads, "fewer squads with SaW (population scaled, no fort garrisons)");
	CHECK(run.outlawAtFort == 0, "no hostile squad targets a handed-over fort");
	CHECK(run.spawnedAtFort == 0, "no squad spawns at a handed-over fort");
	CHECK(base.spawnedAtFort == 0, "(sanity: the plain world was built without hand-over)");
	cfg.populationFactor = 1.f;
	cfg.compat = CompatState{};
}

static void TestReactions(Config& cfg)
{
	cfg.compat = CompatState{};
	CHECK(CompatReactions(cfg).empty(), "no reactions without a mod");
	cfg.compat = ResolveCompat(cfg.compatCfg, { "Skyrim at War Reborn.esp" });
	const auto rs = CompatReactions(cfg);
	auto find = [&](Faction f, FormID ext, int want) {   // want: 1 ally, -1 enemy, 0 none
		for (auto& r : rs)
			if (r.alife == f && r.external == ext) return want == (r.ally ? 1 : -1);
		return want == 0;
	};
	CHECK(find(Faction::Imperial, 0x2BF9A, 1), "ALife Legion allies the vanilla Legion");
	CHECK(find(Faction::Stormcloak, 0x2BF9A, -1), "ALife Stormcloaks fight the vanilla Legion");
	CHECK(find(Faction::Stormcloak, 0x2BF9B, 1), "ALife Stormcloaks ally the vanilla Stormcloaks");
	CHECK(find(Faction::Imperial, 0x2BF9B, -1), "ALife Legion fights the vanilla Stormcloaks");
	CHECK(find(Faction::Bandit, 0x2BF9A, -1) && find(Faction::Bandit, 0x2BF9B, -1), "bandits fight both");
	CHECK(find(Faction::Vigilant, 0x2BF9A, 1), "Vigilants (Legion allies) ally the vanilla Legion");
	CHECK(find(Faction::Thalmor, 0x2BF9A, 0), "Thalmor only friendly: no engine tie");
	CHECK(find(Faction::Thalmor, 0x2BF9B, -1), "Thalmor fight the Stormcloaks");
	for (auto& r : rs) CHECK(IsHumanFaction(r.alife), "only factions with ALifeFactions.esp records");
	cfg.compat = CompatState{};
}

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : DefaultConfigDir();
	const std::string worldPath = argc > 2 ? argv[2] : "../data/world_skyrim.tsv";
	Config cfg;
	std::string err;
	if (!LoadConfig(dir, cfg, err)) { std::printf("cannot load config from %s: %s\n", dir.c_str(), err.c_str()); return 2; }
	if (!err.empty()) std::printf("config warnings: %s\n", err.c_str());
	CHECK(err.find("compat") == std::string::npos, "compat.ltx loads without warnings");
	TestPatterns();
	TestResolve(cfg);
	TestReactions(cfg);
	TestWorld(cfg, worldPath);
	std::printf("compat_test: %d passed, %d failed\n", gPass, gFails);
	return gFails ? 1 : 0;
}
