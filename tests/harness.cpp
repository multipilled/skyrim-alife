// Headless harness: runs the Anomaly-style board over the real Skyrim location map (data/world_skyrim.tsv) and prints
// the resulting ecology. No game, no CommonLibSSE - the same core sources the plugin links.
//
//   harness.exe [--config dir] [--world file.tsv] [--days N] [--seed N] [--pop F] [--ocs off|smarts|full] [--report-every D] [--verbose]
//               [--mutants]   check the monster rules (docs/MUTANTS.md) and exit 1 when one fails
//               [--longrun [--budget-ms MS]]   stability: CPU time per offline step: p99 <= MS (default 2), max <= 5 x MS and no unbounded growth; exit 1 on failure
//               [--m5g [--dyn-rel]]   living news, dynamic relations (off unless --dyn-rel) and lair hunters; exit 1 on failure
//               [--probe name:x,y]... [--probe-radius R]   how often an online-eligible squad is within the online radius of that spot
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include "ALife/Board.h"
#include "ALife/Config.h"
#include "ALife/Util.h"
#include "ALife/World.h"

using namespace ALife;

namespace {
	struct HarnessHost : Host
	{
		int playerLevel{ 15 };
		bool verbose{ false };
		std::uint64_t newsCount{ 0 };
		std::vector<std::string> newsSample;
		float px{ 0 }, py{ 0 };
		bool havePlayer{ false };

		int PlayerLevel() const override { return playerLevel; }
		bool PlayerNear(float x, float y, float r, int) const override
		{
			if (!havePlayer) return false;
			const float dx = x - px, dy = y - py;
			return dx * dx + dy * dy < r * r;
		}
		float RoleMul(Role r) const
		{
			switch (r) {
			case Role::Heavy: return 1.25f;
			case Role::Mage: return 1.1f;
			case Role::Spellsword: return 1.2f;
			case Role::Leader: return 1.3f;
			case Role::Archer: return 0.95f;
			default: return 1.f;
			}
		}
		Member MakeMember(Faction f, Tier tier, Role role, int level, bool leader, Rng& rng, std::uint64_t id) override
		{
			Member m;
			m.id = id;
			m.faction = f;
			m.role = role;
			m.level = level;
			m.name = std::string(FactionId(f)) + "#" + std::to_string(id);
			m.healthMax = m.health = 80.f + 8.f * float(level) + (role == Role::Heavy ? 40.f : 0.f) + (leader ? 20.f : 0.f);
			(void)tier; (void)rng;
			return m;
		}
		float MemberPower(const Member& m) const override { return (10.f + 6.f * float(m.level)) * RoleMul(m.role); }
		// the harness' people are about a third of the plugin's power scale (a level-15 bandit: 100 here, ~300 in game)
		float MonsterPower(const Member&, float a_power) const override { return a_power * 0.33f; }
		// --- needs / activities stand-ins: food is an item with form id 1
		static int Food(const Member& m) { for (auto& it : m.inventory) if (it.form == 1) return it.count; return 0; }
		static void AddFood(Member& m, int n) { for (auto& it : m.inventory) if (it.form == 1) { it.count += n; return; } m.inventory.push_back({ 1, n }); }
		bool FishingAvailable() const override { return true; }
		bool HasFood(const Group& g) const override { for (auto& m : g.members) if (m.alive() && Food(m) > 0) return true; return false; }
		std::unordered_set<FormID> partForms;   // creature parts (monsters.ltx): sellable, counted when sold
		int Parts(const Member& m) const { int n = 0; for (auto& it : m.inventory) if (partForms.count(it.form)) n += it.count; return n; }
		bool HasSellables(const Group& g) const override { for (auto& m : g.members) if (m.alive() && (m.gold > 80 || Parts(m) >= 3)) return true; return false; }
		bool NeedsSupplies(const Group&) const override { return false; }
		bool VendorsOpen(const GraphNode& n, double now) const override { return n.vendors > 0 && (n.inn || InTimeInterval(HourOfDay(now), 8.f, 20.f)); }
		float DefenderStrength(const GraphNode& n, Faction) const override { return n.kind == Kind::Town ? 160.f : 90.f; }
		void Consume(Member& m) override { if (m.hunger >= 500.f && Food(m) > 0) { for (auto& it : m.inventory) if (it.form == 1) --it.count; m.hunger = std::max(0.f, m.hunger - 350.f); } }
		void DoTrade(Group& g, GraphNode&) override
		{
			++trades;
			for (auto& m : g.members) {
				if (!m.alive()) continue;
				AddFood(m, 3);
				m.gold += 25;
				for (auto& it : m.inventory)
					if (partForms.count(it.form) && it.count > 0) { partsSold[int(g.faction)] += std::uint64_t(it.count); m.gold += 15 * it.count; it.count = 0; }
				std::erase_if(m.inventory, [](const Item& it) { return it.count <= 0; });
			}
		}
		std::array<std::uint64_t, kFactionCount> partsSold{};
		void DoRaidSettlement(Group& g, GraphNode&, bool ok, float) override { if (ok) for (auto& m : g.members) if (m.alive()) { m.gold += 60; AddFood(m, 2); } }
		void DoClearSection(Group&, GraphNode&, int, int kills) override { clearedKills += kills; }
		void DoHaul(Group& g, GraphNode&) override { for (auto& m : g.members) if (m.alive()) m.gold += 120; }
		void DoHunt(Group& g, double h, Rng& r) override { for (auto& m : g.members) if (m.alive() && r.chance(0.25f * float(h))) AddFood(m, 1); }
		void DoFish(Group& g, double h, Rng& r) override { for (auto& m : g.members) if (m.alive() && r.chance(0.3f * float(h))) AddFood(m, 1); }
		std::uint64_t trades{ 0 }, clearedKills{ 0 };
		std::map<std::string, std::uint64_t> newsKinds;   // by prefix: "SOS", "Death report", "Battle report", "Gossip", other
		void News(const std::string& line, float, float) override
		{
			++newsCount;
			const auto colon = line.find(':');
			const std::string kind = colon != std::string::npos && colon < 14 ? line.substr(0, colon) : (line.rfind("SOS", 0) == 0 ? "SOS" : "event");
			++newsKinds[kind.rfind("SOS", 0) == 0 ? "SOS" : kind];
			if (newsSample.size() < 40) newsSample.push_back(line);
		}
		void Log(const std::string& line) override { if (verbose) std::cout << line << "\n"; }
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

	void Report(const Board& b, const World& w, const Config& cfg, const char* when)
	{
		std::printf("\n=== %s  (day %.1f, hour %.1f)  squads=%zu ===\n", when, b.now / 24.0, HourOfDay(b.now), b.squads.size());
		std::printf("  %-12s %7s %8s %9s %9s %9s %8s\n", "faction", "squads", "members", "spawned", "wiped", "killed", "arrivals");
		int reach = 0, stay = 0, none = 0, locked = 0;
		for (auto& g : b.squads) {
			(g.action == Action::Reach ? reach : g.action == Action::Stay ? stay : none)++;
			locked += g.locked;
		}
		for (int f = 0; f < kFactionCount; ++f)
			std::printf("  %-12s %7d %8d %9llu %9llu %9llu %8llu\n", FactionId(Faction(f)), b.SquadCount(Faction(f)), b.MemberCount(Faction(f)),
				(unsigned long long)b.stats.spawned[f], (unsigned long long)b.stats.wiped[f], (unsigned long long)b.stats.membersKilled[f],
				(unsigned long long)b.stats.arrivals[f]);
		std::map<std::string, int> acts;
		for (auto& g : b.squads) if (g.action == Action::Stay) acts[ActName(g.activity)]++;
		std::printf("  activities now:");
		for (auto& [k, v] : acts) std::printf(" %s=%d", k.c_str(), v);
		std::printf(" | totals: trades=%llu raidsWon=%llu raidsLost=%llu cleared=%llu aborted=%llu hunts=%llu fish=%llu\n",
			(unsigned long long)b.stats.trades, (unsigned long long)b.stats.raidsWon, (unsigned long long)b.stats.raidsLost,
			(unsigned long long)b.stats.dungeonsCleared, (unsigned long long)b.stats.dungeonsAborted, (unsigned long long)b.stats.hunts,
			(unsigned long long)b.stats.fishings);
		std::printf("  states: reach=%d stay=%d none=%d locked=%d | battles=%llu retreats=%llu retargets=%llu locks=%llu controlChanges=%llu\n",
			reach, stay, none, locked, (unsigned long long)b.stats.battles, (unsigned long long)b.stats.retreats,
			(unsigned long long)b.stats.retargets, (unsigned long long)b.stats.locks, (unsigned long long)b.stats.controlChanges);
		std::map<std::string, int> ctrl;
		int war = 0, over = 0;
		for (std::size_t i = 0; i < w.Size(); ++i) {
			const auto& s = b.smart[i];
			const auto& n = w.nodes[i];
			if (n.kind == Kind::Fort || n.kind == Kind::MilitaryCamp) ctrl[s.faction == kNoFaction ? "none" : FactionId(Faction(s.faction))]++;
			war += s.war;
			over += s.population > n.maxPopulation;
		}
		std::printf("  fort/camp control:");
		for (auto& [k, v] : ctrl) std::printf(" %s=%d", k.c_str(), v);
		std::printf(" | places at war=%d overfull=%d\n", war, over);
		(void)cfg;
	}
}


namespace {
	// docs/MUTANTS.md checks, sampled after every board step.
	struct MutantCheck
	{
		std::uint64_t samples{ 0 }, windowViolations{ 0 };
		double lairSlots{ 0 }, lairUsed{ 0 };                       // stocked lairs / those held by a monster squad, summed over hourly samples after day 1
		double stayLair{ 0 }, stayTerritory{ 0 };                   // monster squad-steps staying in lairs / out on territories
		std::array<double, kFactionCount> stayLairF{}, stayTerrF{}, heldF{}, stockedF{};
		std::array<std::array<std::uint32_t, 24>, kFactionCount> roamByHour{};   // territory stays by hour of day
		std::uint64_t monsterControl{ 0 };
		std::string firstViolation;
		double nextHourly{ 0 };

		void Sample(const Board& b, const World& w, const Config& cfg, double startHours)
		{
			++samples;
			const float hour = HourOfDay(b.now);
			for (auto& g : b.squads) {
				if (!cfg.IsMonster(g.faction) || g.livingCount() == 0) continue;
				const GraphNode* t = g.assignedTarget >= 0 ? w.Node(std::uint32_t(g.assignedTarget)) : nullptr;
				if (!t) continue;
				const bool lair = t->props.lair > 0.f;
				if (!lair && !b.MonsterTerritoryOpen(g.faction, hour)) {
					if (!windowViolations++) {
						char buf[200];
						std::snprintf(buf, sizeof buf, "%s squad #%llu targets %s (no lair) at %.2fh", FactionId(g.faction), (unsigned long long)g.id, t->name.c_str(), hour);
						firstViolation = buf;
					}
				}
				if (g.action == Action::Stay && g.currentTarget == g.assignedTarget) {
					if (lair) { stayLair += 1; stayLairF[int(g.faction)] += 1; }
					else { stayTerritory += 1; stayTerrF[int(g.faction)] += 1; roamByHour[int(g.faction)][int(hour) % 24]++; }
				}
			}
			for (std::size_t i = 0; i < w.Size(); ++i)
				if (b.smart[i].faction != kNoFaction && cfg.IsMonster(Faction(b.smart[i].faction))) ++monsterControl;
			if (b.now - startHours >= 24.0 && b.now >= nextHourly) {
				nextHourly = b.now + 1.0;
				// a stocked lair (one that keeps at least one monster squad) is held when a monster squad is assigned to it
				std::vector<char> held(w.Size(), 0);
				for (auto& g : b.squads)
					if (cfg.IsMonster(g.faction) && g.livingCount() > 0 && g.smartAssigned >= 0) held[std::size_t(g.smartAssigned)] = 1;
				for (std::size_t i = 0; i < w.Size(); ++i) {
					const auto& n = w.nodes[i];
					if (n.props.lair <= 0.f) continue;
					int stocked = -1;
					for (std::size_t k = 0; k < n.respawn.size() && stocked < 0; ++k)
						if (n.respawn[k].faction < kFactionCount && cfg.IsMonster(Faction(n.respawn[k].faction)) && b.MaxCount(std::uint32_t(i), k) >= 1)
							stocked = n.respawn[k].faction;
					if (stocked < 0) continue;
					lairSlots += 1;
					lairUsed += held[i];
					stockedF[stocked] += 1;
					heldF[stocked] += held[i];
				}
			}
		}
	};

	struct Result
	{
		std::array<double, kFactionCount> squads{}, members{}, wiped{}, spawned{};
		double total{ 0 }, battles{ 0 }, controlChanges{ 0 }, retreats{ 0 }, ms{ 0 };
		std::map<std::string, double> ctrl;
		double atWar{ 0 };
	};
}

namespace {
	// --probe name:x,y  - where would the player find an online-eligible squad? Mirrors Online::Tick's pick: same worldspace (Tamriel),
	// not inside a city buffer, monsters only in their hours; reports the share of steps with one inside the online radius
	// (7000, Online.cpp kOnlineDist) and the wait until the first one, plus the nearest squads at the start.
	struct Probe
	{
		std::string name;
		float x{}, y{};
		std::uint64_t samples{}, within{}, within10k{}, cityHidden{}, gap{}, maxGap{};
		double firstHours{ -1 };
		std::map<std::string, int> byTarget;   // eligible squads in range: what they were doing there
	};
	std::vector<Probe> gProbes;

	// --longrun: what the plugin's offline tick costs per step, and whether anything keeps growing
	struct Growth
	{
		double day{};
		std::size_t squads{}, members{}, stacks{}, maxStacks{}, spawnedSlots{};
		double workingSetMb{};
	};
	Growth Measure(const Board& b)
	{
		Growth g;
		g.day = b.now / 24.0;
		g.squads = b.squads.size();
		for (auto& sq : b.squads)
			for (auto& m : sq.members) { ++g.members; g.stacks += m.inventory.size(); g.maxStacks = std::max(g.maxStacks, m.inventory.size()); }
		for (auto& sm : b.smart) g.spawnedSlots += sm.spawned.size();
		PROCESS_MEMORY_COUNTERS pmc{};
		if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) g.workingSetMb = double(pmc.WorkingSetSize) / (1024.0 * 1024.0);
		return g;
	}
	void PrintGrowth(const Growth& g)
	{
		std::printf("  growth day %6.1f: squads=%zu members=%zu stacks=%zu (max/member %zu) respawnSlots=%zu workingSet=%.1f MB\n", g.day, g.squads, g.members,
			g.stacks, g.maxStacks, g.spawnedSlots, g.workingSetMb);
	}
	float gProbeRadius{ -1.f };   // --probe-radius; default: alife.ltx [online] switch_distance (the online pick radius)

	void SampleProbes(const Board& a_b, const World& a_w, double a_elapsed, bool a_show)
	{
		for (auto& p : gProbes) {
			++p.samples;
			float best = FLT_MAX;
			const Group* bg = nullptr;
			bool hidden = false;
			std::vector<std::pair<float, const Group*>> nearSq;
			for (auto& g : a_b.squads) {
				if (g.members.empty() || g.ws != 0) continue;
				const float d = std::hypot(g.x - p.x, g.y - p.y);
				if (a_show) nearSq.emplace_back(d, &g);
				if (a_w.InCityBuffer(g.x, g.y, kCityBuffer, 0)) { if (d < gProbeRadius) hidden = true; continue; }
				if (!IsHumanFaction(g.faction) && !a_b.MonsterMayComeOnline(g, HourOfDay(a_b.now))) continue;
				if (d < best) { best = d; bg = &g; }
			}
			if (best < gProbeRadius) {
				++p.within;
				p.gap = 0;
				if (p.firstHours < 0) p.firstHours = a_elapsed;
				const auto* t = bg->assignedTarget >= 0 ? a_w.Node(std::uint32_t(bg->assignedTarget)) : nullptr;
				++p.byTarget[std::string(bg->action == Action::Reach ? "to " : "at ") + (t ? t->name : "?")];
			}
			else if (hidden) ++p.cityHidden;
			if (best >= gProbeRadius) p.maxGap = std::max(p.maxGap, ++p.gap);
			if (best < 10000.f) ++p.within10k;
			if (a_show) {
				std::sort(nearSq.begin(), nearSq.end(), [](auto& a, auto& c) { return a.first < c.first; });
				std::printf("probe %s (%.0f,%.0f) nearest squads:\n", p.name.c_str(), p.x, p.y);
				for (std::size_t i = 0; i < nearSq.size() && i < 6; ++i)
					std::printf("    d=%6.0f %s%s\n", nearSq[i].first, a_b.DescribeSquad(*nearSq[i].second).c_str(),
						a_w.InCityBuffer(nearSq[i].second->x, nearSq[i].second->y, kCityBuffer, 0) ? " [cityBuffer]" : "");
			}
		}
	}
	void ReportProbes(double a_stepHours)
	{
		for (auto& p : gProbes) {
			std::printf("probe %s: eligible squad within %.0f in %.1f%% of steps (10000: %.1f%%), only city-buffered squads within %.0f %.1f%%, first after %.1f game h, longest gap %.1f game h\n",
				p.name.c_str(), gProbeRadius, 100.0 * p.within / std::max<std::uint64_t>(1, p.samples), 100.0 * p.within10k / std::max<std::uint64_t>(1, p.samples),
				gProbeRadius, 100.0 * p.cityHidden / std::max<std::uint64_t>(1, p.samples), p.firstHours, p.maxGap * a_stepHours);
			std::vector<std::pair<int, std::string>> top;
			for (auto& [k, v] : p.byTarget) top.emplace_back(v, k);
			std::sort(top.rbegin(), top.rend());
			for (std::size_t i = 0; i < top.size() && i < 8; ++i) std::printf("    %5.1f h  %s\n", top[i].first * a_stepHours, top[i].second.c_str());
		}
	}
}

int main(int argc, char** argv)
{
	std::string cfgDir = "config", worldPath = "../data/world_skyrim.tsv";
	double days = 60, reportEvery = 20;
	std::uint64_t seed = 1;
	int seeds = 1;
	float pop = -1.f;
	std::string ocs;
	bool verbose = false, summary = false, mutants = false, listLairs = false, longrun = false, m5g = false, dynRel = false;
	double budgetMs = 2.0;
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
		if (a == "--config") cfgDir = next();
		else if (a == "--world") worldPath = next();
		else if (a == "--days") days = std::atof(next().c_str());
		else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 10);
		else if (a == "--seeds") { seeds = std::atoi(next().c_str()); summary = true; }
		else if (a == "--pop") pop = float(std::atof(next().c_str()));
		else if (a == "--ocs") ocs = next();
		else if (a == "--report-every") reportEvery = std::atof(next().c_str());
		else if (a == "--verbose") verbose = true;
		else if (a == "--summary") summary = true;
		else if (a == "--mutants") { mutants = true; days = 30; }
		else if (a == "--lairs") listLairs = true;
		else if (a == "--longrun") longrun = true;
		else if (a == "--m5g") { m5g = true; days = 30; }
		else if (a == "--dyn-rel") dynRel = true;
		else if (a == "--budget-ms") budgetMs = std::atof(next().c_str());
		else if (a == "--probe-radius") gProbeRadius = float(std::atof(next().c_str()));
		else if (a == "--probe") {   // name:x,y
			std::string v = next();
			Probe p;
			const auto c = v.find(':'), k = v.find(',');
			if (c == std::string::npos || k == std::string::npos) { std::fprintf(stderr, "--probe name:x,y\n"); return 2; }
			p.name = v.substr(0, c);
			p.x = float(std::atof(v.substr(c + 1, k - c - 1).c_str()));
			p.y = float(std::atof(v.substr(k + 1).c_str()));
			gProbes.push_back(p);
		}
	}
	Config cfg;
	std::string err;
	if (!LoadConfig(cfgDir, cfg, err)) { std::fprintf(stderr, "config load failed: %s\n", err.c_str()); return 2; }
	if (!err.empty()) std::fprintf(stderr, "config warnings: %s\n", err.c_str());
	if (pop > 0) cfg.populationFactor = pop;
	if (dynRel) cfg.dynamicRelations = true;
	if (gProbeRadius <= 0) gProbeRadius = cfg.switchDistance;
	if (ocs == "off") cfg.ocsMode = OcsMode::Off;
	else if (ocs == "full") cfg.ocsMode = OcsMode::Full;
	else if (ocs == "smarts") cfg.ocsMode = OcsMode::Smarts;

	std::vector<LocationDef> defs;
	if (!LoadWorld(worldPath, defs)) { std::fprintf(stderr, "cannot read %s\n", worldPath.c_str()); return 2; }
	World world;
	world.BuildNodes(defs);
	world.Finalize(cfg);
	{
		Ltx waterLtx;
		waterLtx.LoadFile(cfgDir + "/water.ltx");
		int nw = 0;
		for (auto& n : world.nodes) {
			char key[16];
			std::snprintf(key, sizeof(key), "%X", n.location);
			if (waterLtx.Has("water", key)) { n.water = true; ++nw; }
			if (n.kind == Kind::City) n.vendors = 4;
			else if (n.kind == Kind::Town || n.kind == Kind::Inn) n.vendors = 2;
			if (n.dungeon && n.kind != Kind::BanditCamp) { n.cells.assign(4, n.location); n.bossCell = n.location; }
		}
		std::printf("  waterside smarts: %d\n", nw);
	}
	std::map<std::string, int> kinds;
	for (auto& n : world.nodes) kinds[KindName(n.kind)]++;
	std::printf("world: %zu locations -> %zu smart terrains, %d component(s)\n  kinds:", defs.size(), world.Size(), world.ComponentCount());
	for (auto& [k, v] : kinds) std::printf(" %s=%d", k.c_str(), v);
	std::printf("\n");

	if (mutants) {
		int lairs = 0, slots = 0;
		std::map<std::string, int> lairKinds;
		for (auto& n : world.nodes)
			if (n.props.lair > 0.f) { ++lairs; slots += n.maxPopulation; lairKinds[KindName(n.kind)]++; }
		std::printf("mutants: %zu creatures, lairs=%d (capacity %d):", cfg.creatures.size(), lairs, slots);
		for (auto& [k, v] : lairKinds) std::printf(" %s=%d", k.c_str(), v);
		std::printf("\n");
		if (listLairs)
			for (auto& n : world.nodes) {
				if (n.props.lair <= 0.f) continue;
				std::printf("  lair %-28s %-16s pop %d idle %.0fh", n.name.c_str(), KindName(n.kind), n.maxPopulation, n.respawnIdleHours);
				for (int f = kHumanFactionCount; f < kFactionCount; ++f)
					if (n.props.byFaction[f] > 0.f) std::printf(" %s=%g", FactionId(Faction(f)), n.props.byFaction[f]);
				for (auto& rp : n.respawn) std::printf(" +%s", rp.name.c_str());
				std::printf("\n");
			}
	}
	int failures = 0;
	Result acc;
	std::vector<Result> all;
	for (int si = 0; si < seeds; ++si) {
		HarnessHost host;
		for (auto& cd : cfg.creatures) for (auto& it : cd.parts) host.partForms.insert(it.form);
		cfg.ResetLiveRelations();
		std::uint64_t huntersAtLairs = 0;   // step samples: hunter squads heading for / staying at a lair a monster squad holds
		host.verbose = verbose && !summary;
		Board board(cfg, world, host);
		board.Reset(24.0 * 3 + 9.0);
		board.rng = Rng(seed + std::uint64_t(si));
		board.worldSeed = Mix64(seed + std::uint64_t(si) * 7919);
		board.StartFill();
		if (!summary) Report(board, world, cfg, "start");
		MutantCheck mc;
		const double start = board.now;
		const int monstersAtStart = board.MonsterSquads();
		auto t0 = std::chrono::steady_clock::now();
		const double end = board.now + days * 24.0;
		double nextReport = board.now + reportEvery * 24.0;
		std::vector<double> stepMs, stepCyc, stepAt;   // wall ms, this thread's CPU cycles (immune to preemption), game hour
		std::vector<Growth> growth;
		if (longrun) growth.push_back(Measure(board));
		double nextGrowth = board.now + 10.0 * 24.0;
		while (board.now + cfg.stepHours <= end) {
			const auto s0 = std::chrono::steady_clock::now();
			ULONG64 c0 = 0, c1 = 0;
			QueryThreadCycleTime(GetCurrentThread(), &c0);
			board.Step(cfg.stepHours);
			QueryThreadCycleTime(GetCurrentThread(), &c1);
			if (longrun) {
				stepMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s0).count());
				stepCyc.push_back(double(c1 - c0));
				stepAt.push_back(board.now);
				if (board.now >= nextGrowth) { growth.push_back(Measure(board)); PrintGrowth(growth.back()); nextGrowth += 10.0 * 24.0; }
			}
			if (mutants) mc.Sample(board, world, cfg, start);
			if (m5g)
				for (auto& g : board.squads)
					if (cfg.huntsLairs[int(g.faction)] && g.assignedTarget >= 0 && board.MonsterHeld(std::uint32_t(g.assignedTarget)) &&
						world.nodes[std::size_t(g.assignedTarget)].props.lair > 0.f) ++huntersAtLairs;
			if (!gProbes.empty() && si == 0) SampleProbes(board, world, board.now - start, board.now - start < cfg.stepHours * 1.5);
			if (!summary && board.now >= nextReport) { Report(board, world, cfg, "report"); nextReport += reportEvery * 24.0; }
		}
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		if (!summary) {
			Report(board, world, cfg, "end");
			std::printf("\nsimulated %.0f days in %.0f ms (%.1f ms / game day); news lines=%llu\n", days, ms, ms / std::max(1.0, days),
				(unsigned long long)host.newsCount);
			for (auto& n : host.newsSample) std::printf("  news: %s\n", n.c_str());
		}
		if (m5g) {
			auto check = [&](bool ok, const char* what, const std::string& detail) {
				std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
				if (!ok) ++failures;
			};
			char d[512];
			const auto& st = board.stats;
			std::printf("\nM5g checks (seed %llu, %.0f days, dynamic relations %s):\n", (unsigned long long)(seed + std::uint64_t(si)), days, cfg.dynamicRelations ? "on" : "off");
			std::string kinds;
			for (auto& [k, v] : host.newsKinds) kinds += " " + k + "=" + std::to_string(v);
			std::snprintf(d, sizeof d, "%llu lines (%.1f per game day):%s", (unsigned long long)host.newsCount, double(host.newsCount) / days, kinds.c_str());
			const double newKinds = double(st.sosNews + st.deathNews + st.battleNews + st.gossipNews) / days;
			check(st.sosNews > 0 && st.deathNews > 0 && st.battleNews > 0 && st.gossipNews > 0 && newKinds <= 40.0,
			      "living news (SOS, death reports, battle reports, gossip all present; the new kinds <= 40 a day)", d);
			int changed = 0, atWar0 = 0, atWar1 = 0;
			for (int a = 0; a < kFactionCount; ++a)
				for (int b = a + 1; b < kFactionCount; ++b) {
					changed += cfg.relLive[a][b] != cfg.rel[a][b];
					atWar0 += cfg.rel[a][b] <= -1000;
					atWar1 += cfg.Rel(Faction(a), Faction(b)) <= -1000;
				}
			const int imp = int(Faction::Imperial), sc = int(Faction::Stormcloak);
			std::snprintf(d, sizeof d, "%llu changes, %llu war/alliance flips, %d pairs differ, pairs at war %d -> %d, imperial/stormcloak %d -> %d",
			              (unsigned long long)st.relationChanges, (unsigned long long)st.relationFlips, changed, atWar0, atWar1, cfg.rel[imp][sc], cfg.relLive[imp][sc]);
			if (cfg.dynamicRelations)
				check(st.relationChanges > 0 && cfg.relLive[imp][sc] == cfg.rel[imp][sc], "dynamic relations move, Civil War pair locked", d);
			else
				check(st.relationChanges == 0 && changed == 0, "dynamic relations off: matrix untouched", d);
			std::snprintf(d, sizeof d, "parts harvested hunter=%llu wanderer=%llu, sold hunter=%llu wanderer=%llu; hunter-squad steps aimed at an occupied lair %llu",
			              (unsigned long long)st.partsHarvested[int(Faction::Hunter)], (unsigned long long)st.partsHarvested[int(Faction::Wanderer)],
			              (unsigned long long)host.partsSold[int(Faction::Hunter)], (unsigned long long)host.partsSold[int(Faction::Wanderer)], (unsigned long long)huntersAtLairs);
			check(st.partsHarvested[int(Faction::Hunter)] > 0 && host.partsSold[int(Faction::Hunter)] > 0 && huntersAtLairs > 0, "hunters take lairs' parts and sell them", d);
			for (std::size_t i = 0; i < host.newsSample.size() && i < 12; ++i) std::printf("    news: %s\n", host.newsSample[i].c_str());
		}
		if (longrun && !stepMs.empty() && growth.size() >= 4) {
			auto check = [&](bool ok, const char* what, const std::string& detail) {
				std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
				if (!ok) ++failures;
			};
			// CPU time per step: cycles scaled by the wall-ms-per-cycle of the median steps (those are almost never preempted)
			std::vector<double> cycRatio;
			for (std::size_t i = 0; i < stepMs.size(); ++i) if (stepCyc[i] > 0) cycRatio.push_back(stepMs[i] / stepCyc[i]);
			std::sort(cycRatio.begin(), cycRatio.end());
			const double msPerCycle = cycRatio.empty() ? 0.0 : cycRatio[cycRatio.size() / 2];
			std::vector<double> cpuMs(stepCyc.size());
			for (std::size_t i = 0; i < stepCyc.size(); ++i) cpuMs[i] = stepCyc[i] * msPerCycle;
			std::vector<std::size_t> slow(cpuMs.size());
			for (std::size_t i = 0; i < slow.size(); ++i) slow[i] = i;
			std::partial_sort(slow.begin(), slow.begin() + std::min<std::size_t>(5, slow.size()), slow.end(), [&](auto x, auto y) { return cpuMs[x] > cpuMs[y]; });
			std::printf("\n  slowest steps (CPU ms / wall ms @ day hour):");
			for (std::size_t k = 0; k < 5 && k < slow.size(); ++k) std::printf(" %.1f/%.1f@%.0f:%02.0f", cpuMs[slow[k]], stepMs[slow[k]], stepAt[slow[k]] / 24.0, HourOfDay(stepAt[slow[k]]));
			std::printf("\n");
			std::vector<double> sorted = cpuMs;
			std::sort(sorted.begin(), sorted.end());
			const double p99 = sorted[std::size_t(double(sorted.size() - 1) * 0.99)], mx = sorted.back();
			double sum = 0;
			for (double v : cpuMs) sum += v;
			char d[256];
			std::printf("\nlong-run checks (seed %llu, %.0f days, %zu steps of %.3f game h):\n", (unsigned long long)(seed + std::uint64_t(si)), days, stepMs.size(), cfg.stepHours);
			std::snprintf(d, sizeof d, "CPU mean %.3f ms, p99 %.3f ms, max %.3f ms per step (budget: p99 <= %.1f ms, max <= %.1f ms)", sum / double(stepMs.size()), p99, mx, budgetMs, budgetMs * 5);
			check(p99 <= budgetMs && mx <= budgetMs * 5, "offline tick within budget", d);
			// growth: compare the second half against the first settled sample (day 20); the world should plateau, not climb
			const Growth& a = growth[2];
			const Growth& z = growth.back();
			auto ratio = [](double x, double y) { return y > 0 ? x / y : 1.0; };
			std::snprintf(d, sizeof d, "day %.0f -> %.0f: squads %zu -> %zu, members %zu -> %zu", a.day, z.day, a.squads, z.squads, a.members, z.members);
			check(ratio(double(z.squads), double(a.squads)) <= 1.25 && ratio(double(z.members), double(a.members)) <= 1.25, "population plateaus", d);
			std::snprintf(d, sizeof d, "stacks %zu -> %zu, max per member %zu -> %zu", a.stacks, z.stacks, a.maxStacks, z.maxStacks);
			check(ratio(double(z.stacks), double(a.stacks)) <= 1.5 && z.maxStacks <= 64, "inventories bounded", d);
			std::snprintf(d, sizeof d, "respawn slots %zu -> %zu, working set %.1f -> %.1f MB", a.spawnedSlots, z.spawnedSlots, a.workingSetMb, z.workingSetMb);
			check(z.spawnedSlots == a.spawnedSlots && z.workingSetMb <= a.workingSetMb * 1.25 + 8.0, "no leaks", d);
		}
		if (mutants) {
			auto check = [&](bool ok, const char* what, const std::string& detail) {
				std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
				if (!ok) ++failures;
			};
			char d[256];
			std::printf("\nmutant checks (seed %llu, %.0f days):\n", (unsigned long long)(seed + std::uint64_t(si)), days);
			std::snprintf(d, sizeof d, "%d monster squads at start, %d now", monstersAtStart, board.MonsterSquads());
			check(monstersAtStart > 0 && board.MonsterSquads() > 0, "monster squads exist", d);
			{   // every monster kind with a lair on this map is made (Solstheim-only kinds have none here: reported with *)
				std::array<bool, kFactionCount> hasLair{};
				for (auto& n : world.nodes)
					for (auto& rp : n.respawn) if (rp.faction < kFactionCount) hasLair[rp.faction] = true;
				std::string kinds;
				bool all = true;
				for (int f = kHumanFactionCount; f < kFactionCount; ++f) {
					if (!cfg.IsMonster(Faction(f))) continue;
					const auto made = board.stats.spawned[f];
					kinds += std::string(" ") + FactionId(Faction(f)) + "=" + std::to_string(made) + "/" + std::to_string(board.SquadCount(Faction(f))) + (hasLair[f] ? "" : "*");
					all = all && (!hasLair[f] || made > 0);
				}
				check(all, "every monster kind is made", "squads spawned/alive per kind (* = no lair on this map):" + kinds);
			}
			const double occ = mc.lairSlots > 0 ? mc.lairUsed / mc.lairSlots : 0.0;
			std::string heldBy;
			for (int f = kHumanFactionCount; f < kFactionCount; ++f) {
				char b2[64];
				std::snprintf(b2, sizeof b2, " %s %.0f%%", FactionId(Faction(f)), mc.stockedF[f] > 0 ? 100.0 * mc.heldF[f] / mc.stockedF[f] : 0.0);
				heldBy += b2;
			}
			std::snprintf(d, sizeof d, "stocked lairs held by a monster squad %.0f%% of hourly samples after day 1 (need >= 75%%); by owner:%s", 100.0 * occ, heldBy.c_str());
			check(occ >= 0.75, "lairs filled", d);
			const double share = mc.stayLair + mc.stayTerritory > 0 ? mc.stayLair / (mc.stayLair + mc.stayTerritory) : 0.0;
			std::string per;
			for (int f = kHumanFactionCount; f < kFactionCount; ++f) {
				const double t = mc.stayLairF[f] + mc.stayTerrF[f];
				char b2[64];
				std::snprintf(b2, sizeof b2, " %s %.1f%%", FactionId(Faction(f)), t > 0 ? 100.0 * mc.stayLairF[f] / t : 0.0);
				per += b2;
			}
			std::snprintf(d, sizeof d, "%.1f%% of monster staying time in lairs (need >= 60%%); by faction:%s", 100.0 * share, per.c_str());
			check(share >= 0.6, "lairs first", d);
			std::snprintf(d, sizeof d, "%llu of %llu step samples had a monster targeting a territory outside its hours%s%s",
				(unsigned long long)mc.windowViolations, (unsigned long long)mc.samples, mc.windowViolations ? "; first: " : "", mc.firstViolation.c_str());
			check(mc.windowViolations == 0, "territory windows", d);
			std::string roam;
			bool roamsInWindow = true;
			for (int f = kHumanFactionCount; f < kFactionCount; ++f) {
				if (!cfg.IsMonster(Faction(f))) continue;
				std::uint32_t tot = 0, outside = 0;
				for (int hr = 0; hr < 24; ++hr) {
					tot += mc.roamByHour[f][hr];
					if (!board.MonsterTerritoryOpen(Faction(f), float(hr) + 0.5f)) outside += mc.roamByHour[f][hr];
				}
				// a species with hours (day / night) and a real population must actually use its window; rare or any-hour ones are reported
				const MonsterType mt = cfg.monster[f];
				const bool timed = mt == MonsterType::PredatoryDay || mt == MonsterType::PredatoryNight || mt == MonsterType::ZombiedDay || mt == MonsterType::ZombiedNight;
				const bool mustRoam = timed && board.SquadCount(Faction(f)) >= 5;
				roam += std::string(" ") + FactionId(Faction(f)) + "=" + std::to_string(tot) + (outside ? "(" + std::to_string(outside) + " outside!)" : "") + (mustRoam ? "" : "*");
				roamsInWindow = roamsInWindow && outside == 0 && (tot > 0 || !mustRoam);
			}
			check(roamsInWindow, "monsters roam territories only in their hours", "territory stay samples (* = any-hour or under 5 squads: roaming not required):" + roam);
			// sim_squad_scripted online windows: day mutants never online 22-05, night mutants never 06-18
			int onlineBad = 0;
			std::string onlineDesc;
			for (int f = kHumanFactionCount; f < kFactionCount; ++f) {
				Group probe;
				probe.faction = Faction(f);
				const MonsterType mt = cfg.monster[f];
				std::string hours;
				for (int hr = 0; hr < 24; ++hr) {
					const bool may = board.MonsterMayComeOnline(probe, float(hr) + 0.5f);
					bool want = true;
					if (mt == MonsterType::PredatoryDay || mt == MonsterType::ZombiedDay) want = !(hr <= 5 || hr >= 22);
					else if (mt == MonsterType::PredatoryNight || mt == MonsterType::ZombiedNight) want = !(hr >= 6 && hr <= 18);
					onlineBad += may != want;
					hours += may ? '+' : '.';
				}
				onlineDesc += std::string(" ") + FactionId(Faction(f)) + "[" + hours + "]";
			}
			check(onlineBad == 0, "online windows", "hours 0-23 (+ may come online):" + onlineDesc);
			std::uint64_t mVsH = 0, hVsM = 0;
			for (int a = 0; a < kFactionCount; ++a)
				for (int v = 0; v < kFactionCount; ++v) {
					const bool am = cfg.IsMonster(Faction(a)), vm = cfg.IsMonster(Faction(v));
					if (am && !vm) mVsH += board.stats.killMatrix[a][v];
					if (!am && vm) hVsM += board.stats.killMatrix[a][v];
				}
			std::snprintf(d, sizeof d, "monsters killed %llu people, people killed %llu monsters", (unsigned long long)mVsH, (unsigned long long)hVsM);
			check(mVsH > 0 && hVsM > 0, "offline combat monsters vs factions", d);
			std::snprintf(d, sizeof d, "%llu step samples with a monster-held place", (unsigned long long)mc.monsterControl);
			check(mc.monsterControl == 0, "monsters never hold places", d);
		}
		Result r;
		for (int f = 0; f < kFactionCount; ++f) {
			r.squads[f] = board.SquadCount(Faction(f));
			r.members[f] = board.MemberCount(Faction(f));
			r.wiped[f] = double(board.stats.wiped[f]);
			r.spawned[f] = double(board.stats.spawned[f]);
			r.total += r.squads[f];
		}
		r.battles = double(board.stats.battles);
		r.controlChanges = double(board.stats.controlChanges);
		r.retreats = double(board.stats.retreats);
		r.ms = ms;
		for (std::size_t i = 0; i < world.Size(); ++i) {
			const auto& n = world.nodes[i];
			if (n.kind == Kind::Fort || n.kind == Kind::MilitaryCamp)
				r.ctrl[board.smart[i].faction == kNoFaction ? "none" : FactionId(Faction(board.smart[i].faction))] += 1;
			r.atWar += board.smart[i].war;
		}
		all.push_back(std::move(r));
	}
	if (summary) {
		std::printf("\nsummary over %d seed(s), %.0f game days each\n", seeds, days);
		std::printf("  %-12s %8s %5s %5s %9s %8s %8s\n", "faction", "squads", "min", "max", "members", "spawned", "wiped");
		double tot = 0;
		for (int f = 0; f < kFactionCount; ++f) {
			double s = 0, mn = 1e9, mx = 0, m = 0, sp = 0, wp = 0;
			for (auto& r : all) { s += r.squads[f]; mn = std::min(mn, r.squads[f]); mx = std::max(mx, r.squads[f]); m += r.members[f]; sp += r.spawned[f]; wp += r.wiped[f]; }
			const double n = double(all.size());
			std::printf("  %-12s %8.1f %5.0f %5.0f %9.1f %8.0f %8.0f\n", FactionId(Faction(f)), s / n, mn, mx, m / n, sp / n, wp / n);
			tot += s / n;
		}
		double b = 0, c = 0, r2 = 0, ms = 0, war = 0;
		std::map<std::string, double> ctrl;
		for (auto& r : all) { b += r.battles; c += r.controlChanges; r2 += r.retreats; ms += r.ms; war += r.atWar; for (auto& [k, v] : r.ctrl) ctrl[k] += v; }
		const double n = double(all.size());
		std::printf("  total squads %.1f | battles %.0f retreats %.0f controlChanges %.0f placesAtWar %.1f | %.1f ms/day\n", tot, b / n, r2 / n, c / n, war / n, ms / n / std::max(1.0, days));
		std::printf("  fort/camp control (avg):");
		for (auto& [k, v] : ctrl) std::printf(" %s=%.1f", k.c_str(), v / n);
		std::printf("\n");
	}
	ReportProbes(cfg.stepHours);
	if (m5g) std::printf("\nM5g checks: %s\n", failures ? "FAILED" : "all passed");
	if (mutants) std::printf("\nmutant checks: %s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
