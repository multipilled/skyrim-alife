// Headless harness: runs the Anomaly-style board over the real Skyrim location map (data/world_skyrim.tsv) and prints
// the resulting ecology. No game, no CommonLibSSE - the same core sources the plugin links.
//
//   harness.exe [--config dir] [--world file.tsv] [--days N] [--seed N] [--pop F] [--ocs off|smarts|full] [--report-every D] [--verbose]
//               [--mutants]   check the monster rules (docs/MUTANTS.md) and exit 1 when one fails
//               [--longrun [--budget-ms MS]]   stability: CPU time per offline step: p99 <= MS (default 2), max <= 5 x MS and no unbounded growth; exit 1 on failure
//               [--m5g [--dyn-rel]]   living news, dynamic relations (off unless --dyn-rel) and lair hunters; exit 1 on failure
//               [--solstheim]         also load data/world_solstheim.tsv (tools/gen_world_dlc.py): Skaal, Redoran and Reaver
//               [--dragons]           dragons (docs/DRAGONS.md): gate, cap and multiplier, places, straight flight, panic, retire; exit 1 on failure
//                                     squads on Solstheim, Reavers fight them offline, none of them in Tamriel; exit 1 on failure
//               [--m5d]               wounded / help / finish / survivors in offline combat (docs/WOUNDED.md); exit 1 on failure
//               [--m5f]               stash notes on the dead, notes read, job reward stashes, marker slots (docs/STASHES.md); exit 1 on failure
//               [--no-wounds]         run without config/wounded.ltx (the board as before M5d); with it present wounds are on, as in the plugin
//               [--probe name:x,y]... [--probe-radius R]   how often an online-eligible squad is within the online radius of that spot
//               [--difficulty 0-5] [--diff channel=value]... [--no-follow]   Skyrim's difficulty and the MCM sliders (docs/DIFFICULTY.md)
//               [--diff-sweep [--days N]]   6 difficulties x each slider at min / 1.0 / max (10 days each): monotonic, no errors; exit 1 on failure
//               [--json path]         also write one JSON report at exit (claude-handoff.md ticket G8; compare two with tools/compare_harness.py);
//                                     the printed report and the exit code stay as without it (exit 2 only if the file cannot be written)
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
#include "ALife/SaveFormat.h"
#include "ALife/Standing.h"
#include "ALife/Util.h"
#include "ALife/World.h"
#include "ALife/Stashes.h"
#include "ALife/Wounds.h"

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
		Member MakeMemberIn(int ws, Faction f, Tier tier, Role role, int level, bool leader, Rng& rng, std::uint64_t id) override
		{
			Member m = MakeMember(f, tier, role, level, leader, rng, id);
			if (raceCfg) m.race = raceCfg->RollRace(f, ws, raceRng);   // T24: races.ltx; its own rng, so no board draw changes
			return m;
		}
		const Config* raceCfg{ nullptr };
		Rng raceRng{ 0xACE5 };
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
		std::array<std::array<std::uint64_t, kFactionCount>, kFactionCount> kills{};   // [killer][victim], offline fights
		void OnMemberKilled(Group& vg, Member& m, Group* kg, Member*) override
		{
			if (kg) ++kills[int(kg->faction)][int(vg.faction)];
			if (stash && !companions.count(vg.id)) stash->OnKill(vg, m, kg, false, stashRng);   // --m5f: the plugin's offline hook
		}
		Stashes* stash{ nullptr };   // --m5f
		Rng stashRng{ 0x5757 };
		std::unordered_set<std::uint64_t> companions;
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
			if (f.size() > 10) d.ws = std::uint8_t(std::stoul(f[10]));   // world_solstheim.tsv: 1
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
	// --json <path> (ticket G8): a machine-readable copy of what the run found, written once at exit. Collecting it prints
	// nothing and draws no random number, so the run and its printed report are the same with and without --json.
	bool gJson{ false };
	struct JsonCheck { std::uint64_t runs{ 0 }, failed{ 0 }; };
	std::map<std::string, JsonCheck> gJsonChecks;   // "<section>: <check name>" -> how often it ran / failed (over all seeds)
	void JsonCheckResult(const char* a_section, const std::string& a_what, bool a_ok)
	{
		if (!gJson) return;
		auto& c = gJsonChecks[std::string(a_section) + ": " + a_what];
		++c.runs;
		c.failed += a_ok ? 0 : 1;
	}
	// a minimal writer: values after a key or inside a list, commas handled
	struct JsonOut
	{
		std::string s;
		std::vector<bool> first;
		bool afterKey{ false };
		static std::string Quote(const std::string& a_v)
		{
			std::string o = "\"";
			for (unsigned char c : a_v) {
				switch (c) {
				case '"': o += "\\\""; break;
				case '\\': o += "\\\\"; break;
				case '\n': o += "\\n"; break;
				case '\r': o += "\\r"; break;
				case '\t': o += "\\t"; break;
				default:
					if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
					else o += char(c);
				}
			}
			return o + "\"";
		}
		void Sep()
		{
			if (afterKey) { afterKey = false; return; }
			if (!first.empty()) { if (!first.back()) s += ','; first.back() = false; }
		}
		JsonOut& Key(const std::string& a_k) { Sep(); s += Quote(a_k); s += ':'; afterKey = true; return *this; }
		JsonOut& Obj() { Sep(); s += '{'; first.push_back(true); return *this; }
		JsonOut& EndObj() { s += '}'; first.pop_back(); return *this; }
		JsonOut& Arr() { Sep(); s += '['; first.push_back(true); return *this; }
		JsonOut& EndArr() { s += ']'; first.pop_back(); return *this; }
		JsonOut& Str(const std::string& a_v) { Sep(); s += Quote(a_v); return *this; }
		JsonOut& Bool(bool a_v) { Sep(); s += a_v ? "true" : "false"; return *this; }
		JsonOut& Null() { Sep(); s += "null"; return *this; }
		JsonOut& Raw(const std::string& a_v) { Sep(); s += a_v; return *this; }
		JsonOut& U(std::uint64_t a_v) { Sep(); s += std::to_string(a_v); return *this; }
		JsonOut& I(std::int64_t a_v) { Sep(); s += std::to_string(a_v); return *this; }
		JsonOut& D(double a_v)
		{
			Sep();
			if (!std::isfinite(a_v)) { s += "null"; return *this; }
			char b[40];
			std::snprintf(b, sizeof b, "%.10g", a_v);
			s += b;
			return *this;
		}
	};
	std::vector<std::string> gJsonRuns;       // one object per seed run
	std::vector<std::string> gJsonDiffRuns;   // --diff-sweep: one object per short run
	// CPU ms per step from this thread's cycle counter, scaled by the wall-ms-per-cycle of the median steps (as --longrun does)
	void JsonCpu(JsonOut& a_j, const std::vector<double>& a_ms, const std::vector<double>& a_cyc, double a_wallMs)
	{
		a_j.Key("cpu").Obj();
		a_j.Key("steps").U(a_ms.size());
		a_j.Key("wall_ms_total").D(a_wallMs);
		std::vector<double> ratio;
		for (std::size_t i = 0; i < a_ms.size(); ++i) if (a_cyc[i] > 0) ratio.push_back(a_ms[i] / a_cyc[i]);
		std::sort(ratio.begin(), ratio.end());
		const double msPerCycle = ratio.empty() ? 0.0 : ratio[ratio.size() / 2];
		std::vector<double> cpu(a_cyc.size());
		for (std::size_t i = 0; i < a_cyc.size(); ++i) cpu[i] = a_cyc[i] * msPerCycle;
		std::sort(cpu.begin(), cpu.end());
		auto at = [&](double q) { return cpu.empty() ? 0.0 : cpu[std::size_t(double(cpu.size() - 1) * q)]; };
		double sum = 0;
		for (double v : cpu) sum += v;
		a_j.Key("step_ms").Obj();
		a_j.Key("p50").D(at(0.5)).Key("p99").D(at(0.99)).Key("max").D(cpu.empty() ? 0.0 : cpu.back()).Key("mean").D(cpu.empty() ? 0.0 : sum / double(cpu.size()));
		a_j.EndObj();
		a_j.EndObj();
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
	WoundConfig gWoundBase;   // wounded.ltx as loaded (main): every board scales it by its own Mercy
	bool gWoundsOn{ false };
	// --diff-sweep (docs/DIFFICULTY.md section 8): one short run per game difficulty x channel x slider min / 1.0 / max
	struct DiffRun
	{
		int humanStart{}, monsterStart{}, humanEnd{}, monsterEnd{};
		int quotaHuman{}, quotaMonster{};   // respawn quotas (Board::MaxCount): deterministic, unlike the start counts (rounded at random)
		double meanSize{}, meanLevel{};
		int maxSize{}, maxDefSize{};
		double deaths{};
		double downs{}, finished{};   // offline downs and finishes (docs/WOUNDED.md, Mercy)
		int toll{};
		bool nan{ false };
		DifficultyState diff;
	};
	DiffRun RunDifficulty(const Config& a_base, World& a_world, int a_game, const DifficultyChoice& a_choice, std::uint64_t a_seed, double a_days)
	{
		Config cfg = a_base;   // the board keeps a reference: one copy per run
		cfg.ApplyDifficulty(a_game, a_choice);
		HarnessHost host;
		host.raceCfg = &cfg;
		cfg.ResetLiveRelations();
		Board board(cfg, a_world, host);
		Wounds wd;
		if (gWoundsOn) {
			wd.cfg = ScaleWoundConfig(gWoundBase, WoundScale{ .mercy = cfg.Diff(DiffChannel::Mercy) });
			board.wounds = &wd;
		}
		board.Reset(24.0 * 3 + 9.0);
		board.rng = Rng(a_seed);
		board.worldSeed = Mix64(a_seed * 7919);
		board.SetGated(Faction::Dragon, true);   // the world before Mirmulnir dies, as in the default runs (docs/DRAGONS.md)
		board.StartFill();
		DiffRun r;
		r.diff = cfg.diff;
		for (std::uint32_t i = 0; i < a_world.Size(); ++i)
			for (std::size_t k = 0; k < a_world.nodes[i].respawn.size(); ++k) {
				const auto f = a_world.nodes[i].respawn[k].faction;
				(f < kFactionCount && cfg.IsMonster(Faction(f)) ? r.quotaMonster : r.quotaHuman) += board.MaxCount(i, k);
			}
		int people = 0, squadsH = 0;
		double size = 0, level = 0;
		for (auto& g : board.squads) {
			if (cfg.IsMonster(g.faction)) { ++r.monsterStart; continue; }
			++r.humanStart;
			++squadsH;
			size += double(g.members.size());
			r.maxSize = std::max(r.maxSize, int(g.members.size()));
			if (const SquadDef* d = cfg.Squad(g.section)) r.maxDefSize = std::max(r.maxDefSize, d->maxMembers);
			for (auto& m : g.members) { level += double(m.level); ++people; }
		}
		r.meanSize = squadsH ? size / squadsH : 0.0;
		r.meanLevel = people ? level / people : 0.0;
		const double end = board.now + a_days * 24.0;
		while (board.now + cfg.stepHours <= end) board.Step(cfg.stepHours);
		for (auto& g : board.squads) {
			(cfg.IsMonster(g.faction) ? r.monsterEnd : r.humanEnd)++;
			for (auto& m : g.members) if (!(m.health == m.health) || !(m.healthMax == m.healthMax)) r.nan = true;
		}
		for (auto v : board.stats.membersLost) r.deaths += double(v);
		r.downs = double(wd.counters.ocsDowned);
		r.finished = double(wd.counters.finished);
		Standing st(&cfg);
		PlayerContext pc;
		pc.level = 20;
		r.toll = st.TollFor(pc);
		return r;
	}

	int DiffSweep(const Config& a_base, World& a_world, std::uint64_t a_seed, double a_days)
	{
		int failures = 0;
		auto check = [&](bool ok, const std::string& what) {
			if (!ok) { ++failures; std::printf("  [FAIL] %s\n", what.c_str()); }
			JsonCheckResult("diff-sweep", what, ok);
		};
		const char* jsonGame = "";
		auto print = [&](const char* tag, const DiffRun& r) {
			if (gJson) {
				JsonOut j;
				j.Obj().Key("game").Str(jsonGame).Key("run").Str(tag).Key("quota_human").I(r.quotaHuman).Key("quota_monster").I(r.quotaMonster);
				j.Key("human_start").I(r.humanStart).Key("human_end").I(r.humanEnd).Key("monster_start").I(r.monsterStart).Key("monster_end").I(r.monsterEnd);
				j.Key("mean_size").D(r.meanSize).Key("max_size").I(r.maxSize).Key("mean_level").D(r.meanLevel).Key("deaths").D(r.deaths);
				j.Key("downs").D(r.downs).Key("finished").D(r.finished).Key("toll").I(r.toll).Key("nan").Bool(r.nan).Key("difficulty").Str(r.diff.Describe()).EndObj();
				gJsonDiffRuns.push_back(j.s);
			}
			std::printf("  %-26s quota %4d/%4d people %4d->%4d monsters %4d->%4d size %.2f (max %d) level %.2f deaths %5.0f toll %4d | %s\n", tag, r.quotaHuman, r.quotaMonster, r.humanStart, r.humanEnd,
				r.monsterStart, r.monsterEnd, r.meanSize, r.maxSize, r.meanLevel, r.deaths, r.toll, r.diff.Describe().c_str());
		};
		const auto& dc = a_base.diffCfg;
		std::array<DiffRun, kGameDifficulties> neutral;
		std::printf("\ndifficulty sweep: %d game difficulties x %d channels x slider min / 1.0 / max, %.0f days each, seed %llu\n", kGameDifficulties,
			kDiffChannels, a_days, (unsigned long long)a_seed);
		for (int d = 0; d < kGameDifficulties; ++d) {
			std::printf("%s:\n", GameDifficultyName(d));
			jsonGame = GameDifficultyName(d);
			neutral[d] = RunDifficulty(a_base, a_world, d, DifficultyChoice{}, a_seed, a_days);
			print("all sliders 1.0", neutral[d]);
			check(!neutral[d].nan && neutral[d].humanEnd > 0, std::string(GameDifficultyName(d)) + ": sim ran (no NaN, squads alive)");
			for (int c = 0; c < kDiffChannels; ++c) {
				const DiffChannel ch = DiffChannel(c);
				DifficultyChoice lo, hi;
				lo.slider[c] = dc.sliderMin[c];
				hi.slider[c] = dc.sliderMax[c];
				const DiffRun a = RunDifficulty(a_base, a_world, d, lo, a_seed, a_days);
				const DiffRun& b = neutral[d];
				const DiffRun z = RunDifficulty(a_base, a_world, d, hi, a_seed, a_days);
				char tag[64];
				std::snprintf(tag, sizeof tag, "%s %.1f", DiffChannelId(ch), lo.slider[c]);
				print(tag, a);
				std::snprintf(tag, sizeof tag, "%s %.1f", DiffChannelId(ch), hi.slider[c]);
				print(tag, z);
				const std::string at = std::string(GameDifficultyName(d)) + " " + DiffChannelId(ch) + ": ";
				for (const DiffRun* r : { &a, &z }) {
					check(!r->nan && r->humanEnd > 0, at + "sim ran (no NaN, squads alive)");
					const float want = std::clamp(dc.rows[d][c] * (r == &a ? lo.slider[c] : hi.slider[c]), dc.limitMin[c], dc.limitMax[c]);
					check(std::fabs(r->diff.Get(ch) - want) < 1e-3f, at + "multiplier = row x slider, clamped");
					check(r->maxSize <= std::max(r->diff.sizeCap, r->maxDefSize), at + "no squad above the size cap");
				}
				auto up = [&](double x, double y, double w, double tol, const char* what) {
					char buf[160];
					std::snprintf(buf, sizeof buf, "%s rises with the slider (%.2f <= %.2f <= %.2f, tolerance %.0f%%)", what, x, y, w, tol * 100.0);
					check(x <= y + std::fabs(y) * tol + 1e-9 && y <= w + std::fabs(w) * tol + 1e-9, at + buf);
				};
				switch (ch) {
				case DiffChannel::Population:
					up(a.quotaHuman, b.quotaHuman, z.quotaHuman, 0.0, "people respawn quota");
					up(a.humanStart, b.humanStart, z.humanStart, 0.03, "people squads at start");
					up(a.humanEnd, b.humanEnd, z.humanEnd, 0.03, "people squads after the run");
					break;
				case DiffChannel::Monsters:
					up(a.quotaMonster, b.quotaMonster, z.quotaMonster, 0.0, "monster respawn quota");
					up(a.monsterStart, b.monsterStart, z.monsterStart, 0.03, "monster squads at start");
					up(a.monsterEnd, b.monsterEnd, z.monsterEnd, 0.03, "monster squads after the run");
					break;
				case DiffChannel::Strength:
					up(a.meanSize, b.meanSize, z.meanSize, 0.0, "mean squad size");
					up(a.meanLevel, b.meanLevel, z.meanLevel, 0.0, "mean member level");
					break;
				case DiffChannel::Lethality: up(a.deaths, b.deaths, z.deaths, 0.05, "offline deaths"); break;
				case DiffChannel::Mercy:   // M5d: more go down and fewer are finished as Mercy rises
					if (gWoundsOn) {
						up(a.downs, b.downs, z.downs, 0.05, "offline downs");
						check(a.downs > 0 || a.diff.Get(ch) <= 0.f, at + "someone still goes down at the lowest Mercy above 0");
					}
					break;
				case DiffChannel::Economy: up(-a.toll, -b.toll, -z.toll, 0.0, "minus the toll (tolls fall as the economy rises)"); break;
				default: break;   // mercy, tasks, dragons: read by M5d / M5e / M4c; here only the multiplier is checked
				}
			}
		}
		// across the game difficulties, sliders at 1.0: Novice -> Legendary is ever more and stronger squads, dearer tolls
		auto rising = [&](const char* what, auto get, double tol) {
			bool ok = true;
			std::string vals;
			char buf[32];
			for (int d = 0; d < kGameDifficulties; ++d) {
				std::snprintf(buf, sizeof buf, " %.2f", double(get(neutral[d])));
				vals += buf;
				if (d > 0 && double(get(neutral[d - 1])) > double(get(neutral[d])) * (1.0 + tol) + 1e-9) ok = false;
			}
			check(ok, std::string("Novice -> Legendary: ") + what + " rises:" + vals);
			std::printf("  Novice -> Legendary %-20s%s\n", what, vals.c_str());
		};
		std::printf("game difficulty, sliders 1.0:\n");
		rising("people respawn quota", [](const DiffRun& r) { return r.quotaHuman; }, 0.0);
		rising("monster respawn quota", [](const DiffRun& r) { return r.quotaMonster; }, 0.0);
		rising("people squads at start", [](const DiffRun& r) { return r.humanStart; }, 0.03);
		rising("monster squads at start", [](const DiffRun& r) { return r.monsterStart; }, 0.03);
		rising("people squads after", [](const DiffRun& r) { return r.humanEnd; }, 0.03);
		rising("mean squad size", [](const DiffRun& r) { return r.meanSize; }, 0.0);
		rising("mean member level", [](const DiffRun& r) { return r.meanLevel; }, 0.0);
		rising("toll", [](const DiffRun& r) { return r.toll; }, 0.0);
		// follow off: every difficulty plays as Adept
		DifficultyChoice off;
		off.followGame = false;
		const DiffRun leg = RunDifficulty(a_base, a_world, kGameDifficulties - 1, off, a_seed, a_days);
		check(leg.humanStart == neutral[kAdept].humanStart && leg.monsterStart == neutral[kAdept].monsterStart && leg.meanSize == neutral[kAdept].meanSize &&
				  leg.humanEnd == neutral[kAdept].humanEnd,
			"follow game difficulty off: Legendary plays exactly as Adept");
		std::printf("\ndifficulty sweep: %s\n", failures ? "FAILED" : "all passed");
		return failures;
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

// --m5f (docs/STASHES.md): once a game day the "player" kills a note carrier and reads the note, takes a job reward and loots
// the stashes marked two days ago; after every step the carried notes and the markers are counted
struct StashCheck
{
	double nextDay{ 0 };
	int reads{ 0 }, readOk{ 0 }, rewards{ 0 }, rewardTries{ 0 }, rewardGold{ 400 }, minRewardValue{ 1 << 30 }, looted{ 0 };
	int maxMarked{ 0 }, doubled{ 0 }, carried{ 0 }, people{ 0 };
	std::unordered_map<std::uint32_t, double> markedSince;
	Rng rng{ 0xF5 };
	void Sample(Board& b, const World& w, Stashes& st, float economy)
	{
		carried = people = 0;
		for (auto& g : b.squads)
			for (auto& m : g.members) {
				if (!m.alive() || !IsHumanFaction(g.faction)) continue;
				++people;
				int n = 0;
				for (auto& it : m.inventory) if (it.form == st.cfg.noteForm) n += it.count;
				carried += n > 0;
				if (n > 1) ++doubled;
			}
		maxMarked = std::max(maxMarked, st.MarkedCount());
		if (b.now < nextDay) return;
		nextDay += 24.0;
		std::vector<StashEvent> ev;
		for (auto& g : b.squads) {   // the player kills one note carrier and reads the note on the body
			Member* carrier = nullptr;
			for (auto& m : g.members) if (m.alive() && Stashes::CarriesNote(m, st.cfg.noteForm)) { carrier = &m; break; }
			if (!carrier) continue;
			std::erase_if(carrier->inventory, [&](const Item& it) { return it.form == st.cfg.noteForm; });
			++reads;
			if (st.OnNoteRead(g.x, g.y, g.ws, 20, economy, rng, ev) > 0) ++readOk;
			break;
		}
		const auto& n = w.nodes[std::size_t(rng.index(int(w.Size())))];   // a job done somewhere
		std::int32_t gold = rewardGold;
		++rewardTries;
		if (st.TaskReward(gold, n.x, n.y, n.ws, 20, economy, rng, ev)) {
			++rewards;
			for (auto& e : ev)
				if (e.kind == StashEvent::Kind::Marked && e.why == "reward")
					if (const StashSpot* sp = st.Find(e.spot)) minRewardValue = std::min(minRewardValue, st.Value(*sp));
		}
		for (auto& e : ev) if (e.kind == StashEvent::Kind::Marked) markedSince[e.spot] = b.now;
		std::vector<StashEvent> ev2;
		for (auto it = markedSince.begin(); it != markedSince.end();) {   // stashes marked two days ago are looted
			if (b.now - it->second >= 48.0) { st.OnLooted(it->first, ev2); ++looted; it = markedSince.erase(it); }
			else ++it;
		}
	}
};

// --m5d: samples taken after every step (docs/WOUNDED.md)
struct WoundCheck
{
	std::uint64_t samples{ 0 }, badRecords{ 0 }, survivorsActive{ 0 }, unhelped{ 0 };
	std::size_t peakDown{ 0 };
	double longestSurvivors{ 0 };
	std::string firstBad;
	std::unordered_map<std::uint64_t, double> survivorsSince;
	std::unordered_map<std::uint64_t, std::pair<float, float>> survivorsAt;
	void Sample(Board& b, const Wounds& wd)
	{
		++samples;
		peakDown = std::max(peakDown, wd.down.size());
		for (auto& [id, d] : wd.down) {
			const Group* g = b.Find(d.squad);
			bool ok = false;
			if (g) for (auto& m : g->members) if (m.id == id && m.alive()) ok = true;
			if (!ok) { if (firstBad.empty()) firstBad = "member " + std::to_string(id) + " of squad " + std::to_string(d.squad); ++badRecords; }
		}
		std::unordered_set<std::uint64_t> seen;
		for (auto& g : b.squads) {
			if (g.online) continue;
			if (wd.Survivors(g)) {
				seen.insert(g.id);
				auto [it, fresh] = survivorsSince.emplace(g.id, b.now);
				if (fresh) survivorsAt[g.id] = { g.x, g.y };
				else if (survivorsAt[g.id] != std::make_pair(g.x, g.y)) ++survivorsActive;
				longestSurvivors = std::max(longestSurvivors, b.now - it->second);
			} else if (wd.DownCount(g) > 0 && b.StandingCount(g) > 0 && !wd.foughtLastStep.count(g.id)) {
				++unhelped;   // did not fight in this step, someone standing, and still someone down after the step's help
			}
		}
		for (auto it = survivorsSince.begin(); it != survivorsSince.end();)
			if (!seen.count(it->first)) { survivorsAt.erase(it->first); it = survivorsSince.erase(it); } else ++it;
	}
};

namespace {
	// --json: the report at exit. a_mode e.g. "default", "mutants", "longrun"; flags that change a mode are in a_args.
	bool WriteJson(const std::string& a_path, const std::string& a_mode, const std::vector<std::string>& a_args, std::uint64_t a_seed, int a_seeds,
		double a_days, double a_stepHours, int a_failures, int a_exit)
	{
		JsonOut j;
		j.Obj();
		j.Key("format").Str("alife-harness-report").Key("version").I(1);
		j.Key("mode").Str(a_mode);
		j.Key("args").Arr();
		for (auto& a : a_args) j.Str(a);
		j.EndArr();
		j.Key("seed").U(a_seed).Key("seeds").I(a_seeds).Key("days").D(a_days).Key("step_hours").D(a_stepHours);
		j.Key("failures").I(a_failures).Key("exit_code").I(a_exit);
		j.Key("checks").Obj();
		for (auto& [name, c] : gJsonChecks) j.Key(name).Obj().Key("pass").Bool(c.failed == 0).Key("runs").U(c.runs).Key("failed").U(c.failed).EndObj();
		j.EndObj();
		j.Key("runs").Arr();
		for (auto& r : gJsonRuns) j.Raw(r);
		j.EndArr();
		if (!gJsonDiffRuns.empty()) {
			j.Key("diff_sweep").Arr();
			for (auto& r : gJsonDiffRuns) j.Raw(r);
			j.EndArr();
		}
		// --probe (seed run 0 only, as printed): where the player would find an online-eligible squad
		j.Key("probes").Arr();
		for (auto& p : gProbes) {
			j.Obj().Key("name").Str(p.name).Key("x").D(p.x).Key("y").D(p.y).Key("radius").D(gProbeRadius).Key("samples").U(p.samples);
			j.Key("within").U(p.within).Key("within_10000").U(p.within10k).Key("city_hidden").U(p.cityHidden);
			j.Key("first_hours").D(p.firstHours).Key("longest_gap_hours").D(double(p.maxGap) * a_stepHours);
			j.Key("by_target").Obj();
			for (auto& [k, v] : p.byTarget) j.Key(k).I(v);
			j.EndObj().EndObj();
		}
		j.EndArr();
		j.EndObj();
		std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out << j.s << "\n";
		out.close();
		return bool(out);
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
	bool verbose = false, summary = false, mutants = false, listLairs = false, longrun = false, m5g = false, dynRel = false, solstheim = false, dragons = false;
	bool m5d = false, noWounds = false, m5f = false;
	double budgetMs = 2.0;
	int gameDiff = kAdept;              // --difficulty 0-5 (Novice .. Legendary)
	DifficultyChoice diffChoice;        // --diff channel=value, --no-follow
	bool diffSweep = false, daysSet = false;
	std::string jsonPath;               // --json <path> (ticket G8)
	std::vector<std::string> jsonArgs;  // the command line, minus --json and its path
	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
		if (a == "--json") { jsonPath = next(); continue; }
		if (a == "--config") cfgDir = next();
		else if (a == "--world") worldPath = next();
		else if (a == "--days") { days = std::atof(next().c_str()); daysSet = true; }
		else if (a == "--seed") seed = std::strtoull(next().c_str(), nullptr, 10);
		else if (a == "--seeds") { seeds = std::atoi(next().c_str()); summary = true; }
		else if (a == "--pop") pop = float(std::atof(next().c_str()));
		else if (a == "--ocs") ocs = next();
		else if (a == "--report-every") reportEvery = std::atof(next().c_str());
		else if (a == "--verbose") verbose = true;
		else if (a == "--summary") summary = true;
		else if (a == "--mutants") { mutants = true; days = 30; }
		else if (a == "--dragons") { dragons = true; if (!longrun) days = 30; }
		else if (a == "--lairs") listLairs = true;
		else if (a == "--longrun") longrun = true;
		else if (a == "--m5g") { m5g = true; days = 30; }
		else if (a == "--dyn-rel") dynRel = true;
		else if (a == "--m5d") { m5d = true; days = 30; }
		else if (a == "--m5f") { m5f = true; days = 30; }
		else if (a == "--no-wounds") noWounds = true;
		else if (a == "--solstheim") { solstheim = true; days = 15; }
		else if (a == "--budget-ms") budgetMs = std::atof(next().c_str());
		else if (a == "--difficulty") gameDiff = std::clamp(std::atoi(next().c_str()), 0, kGameDifficulties - 1);
		else if (a == "--no-follow") diffChoice.followGame = false;
		else if (a == "--diff-sweep") diffSweep = true;
		else if (a == "--diff") {   // channel=value
			const std::string v = next();
			const auto eq = v.find('=');
			DiffChannel ch{};
			if (eq == std::string::npos || !DiffChannelFromId(v.substr(0, eq), ch)) { std::fprintf(stderr, "--diff channel=value (population, monsters, strength, lethality, economy, mercy, tasks, dragons)\n"); return 2; }
			diffChoice.slider[int(ch)] = float(std::atof(v.substr(eq + 1).c_str()));
		}
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
	for (int i = 1; i < argc; ++i) {
		if (std::string(argv[i]) == "--json") { ++i; continue; }
		jsonArgs.push_back(argv[i]);
	}
	gJson = !jsonPath.empty();
	std::string jsonMode;
	for (auto [on, name] : { std::pair<bool, const char*>{ diffSweep, "diff-sweep" }, { longrun, "longrun" }, { mutants, "mutants" }, { m5g, "m5g" },
			 { solstheim, "solstheim" }, { m5d, "m5d" }, { m5f, "m5f" }, { dragons, "dragons" } })
		if (on) jsonMode += (jsonMode.empty() ? "" : "+") + std::string(name);
	if (jsonMode.empty()) jsonMode = "default";
	Config cfg;
	auto finish = [&](int a_failures, int a_exit) {
		if (gJson && !WriteJson(jsonPath, jsonMode, jsonArgs, seed, seeds, days, cfg.stepHours, a_failures, a_exit)) {
			std::fprintf(stderr, "--json: cannot write %s\n", jsonPath.c_str());
			return 2;
		}
		return a_exit;
	};
	std::string err;
	if (!LoadConfig(cfgDir, cfg, err)) { std::fprintf(stderr, "config load failed: %s\n", err.c_str()); return 2; }
	if (!err.empty()) std::fprintf(stderr, "config warnings: %s\n", err.c_str());
	if (pop > 0) cfg.populationFactor = pop;
	cfg.ApplyDifficulty(gameDiff, diffChoice);   // docs/DIFFICULTY.md: what the plugin does with Skyrim's difficulty and the MCM sliders
	if (gameDiff != kAdept || !(diffChoice == DifficultyChoice{})) std::printf("difficulty: %s\n", cfg.diff.Describe().c_str());
	WoundConfig woundCfg;
	bool wounds = false;
	if (!noWounds) {
		Ltx wl;
		if (wl.LoadFile(cfgDir + "/wounded.ltx", nullptr)) {
			std::string werr;
			if (!LoadWoundConfig(wl, woundCfg, werr)) { std::fprintf(stderr, "wounded.ltx: %s\n", werr.c_str()); return 2; }
			wounds = woundCfg.enabled;
			gWoundBase = woundCfg;
			gWoundsOn = wounds;
			woundCfg = ScaleWoundConfig(woundCfg, WoundScale{ .mercy = cfg.Diff(DiffChannel::Mercy) });   // the plugin's read path
		}
	}
	if (m5d && !wounds) { std::fprintf(stderr, "--m5d needs config/wounded.ltx (enabled) and no --no-wounds\n"); return 2; }
	std::printf("wounds: %s\n", wounds ? "on (wounded.ltx)" : "off");
	if (dynRel) cfg.dynamicRelations = true;
	if (gProbeRadius <= 0) gProbeRadius = cfg.switchDistance;
	if (ocs == "off") cfg.ocsMode = OcsMode::Off;
	else if (ocs == "full") cfg.ocsMode = OcsMode::Full;
	else if (ocs == "smarts") cfg.ocsMode = OcsMode::Smarts;

	std::vector<LocationDef> defs;
	if (!LoadWorld(worldPath, defs)) { std::fprintf(stderr, "cannot read %s\n", worldPath.c_str()); return 2; }
	if (solstheim) {
		const std::string sol = worldPath.substr(0, worldPath.find_last_of("/\\") + 1) + "world_solstheim.tsv";
		const std::size_t before = defs.size();
		if (!LoadWorld(sol, defs) || defs.size() == before) { std::fprintf(stderr, "cannot read %s (python tools/gen_world_dlc.py)\n", sol.c_str()); return 2; }
		std::printf("solstheim: %zu Dragonborn locations from %s\n", defs.size() - before, sol.c_str());
	}
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
	if (diffSweep) {
		if (!daysSet) days = 10.0;   // the sweep's default length, as passed below (for the JSON report)
		const int f = DiffSweep(cfg, world, seed, daysSet ? days : 10.0);
		return finish(f, f ? 1 : 0);
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
		host.raceCfg = &cfg;
		for (auto& cd : cfg.creatures) for (auto& it : cd.parts) host.partForms.insert(it.form);
		cfg.ResetLiveRelations();
		std::uint64_t huntersAtLairs = 0;
		std::array<std::unordered_map<std::uint64_t, RaceId>, kWorldspaces> raceSeen;   // --solstheim (T24): every general-faction member seen, by worldspace
		std::array<std::unordered_map<std::uint64_t, Faction>, kWorldspaces> raceFaction;
		std::uint64_t solSeen[3]{}, solInTamriel = 0;   // --solstheim: squad-step samples of skaal, redoran, reaver on Solstheim / in Tamriel   // step samples: hunter squads heading for / staying at a lair a monster squad holds
		host.verbose = verbose && !summary;
		Board board(cfg, world, host);
		Wounds wd;
		wd.cfg = woundCfg;
		if (wounds && wd.Attach(board, false)) { std::fprintf(stderr, "wounds attached before the fill\n"); return 2; }   // as Wounded::Init
		board.Reset(24.0 * 3 + 9.0);
		board.rng = Rng(seed + std::uint64_t(si));
		board.worldSeed = Mix64(seed + std::uint64_t(si) * 7919);
		board.StartFill();
		if (wounds && (!wd.Attach(board, true) || !wd.AttachedTo(board) || wd.Attach(board, true))) {   // as Sim::MainTick after the fill
			std::fprintf(stderr, "wounds hook: not attached after the fill (or attached twice)\n");
			return 2;
		}
		if (!dragons) board.SetGated(Faction::Dragon, true);   // docs/DRAGONS.md: the default runs are the world before Mirmulnir dies
		struct DragonSample
		{
			int maxAlive{ 0 };
			std::uint64_t steps{ 0 }, placeBad{ 0 }, shared{ 0 }, flightBad{ 0 }, smartBad{ 0 }, seen{ 0 };
			double flown{ 0 };
			std::map<std::uint64_t, std::pair<float, float>> last;
			std::map<std::string, int> sections;
		} ds;
		Stashes stash;
		StashCheck sk;
		if (m5f) {
			Ltx sl;
			std::string serr;
			if (!sl.LoadFile(cfgDir + "/stashes.ltx", &serr) || !LoadStashConfig(sl, stash.cfg, serr)) { std::fprintf(stderr, "stashes.ltx: %s\n", serr.c_str()); return 2; }
			stash.cfg.noteForm = 0xFE00081E;
			stash.Build(world, board.worldSeed);
			host.stash = &stash;
			sk.nextDay = board.now + 24.0;
		}
		if (!summary) Report(board, world, cfg, "start");
		MutantCheck mc;
		WoundCheck wc;
		const double start = board.now;
		const int monstersAtStart = board.MonsterSquads();
		auto t0 = std::chrono::steady_clock::now();
		const double end = board.now + days * 24.0;
		double nextReport = board.now + reportEvery * 24.0;
		std::vector<double> stepMs, stepCyc, stepAt;   // wall ms, this thread's CPU cycles (immune to preemption), game hour
		std::vector<Growth> growth;
		std::vector<double> jsonStepMs, jsonStepCyc;   // --json: every step's wall ms and thread cycles (CPU p50 / p99 / max)
		if (longrun) growth.push_back(Measure(board));
		double nextGrowth = board.now + 10.0 * 24.0;
		while (board.now + cfg.stepHours <= end) {
			const auto s0 = std::chrono::steady_clock::now();
			ULONG64 c0 = 0, c1 = 0;
			QueryThreadCycleTime(GetCurrentThread(), &c0);
			board.Step(cfg.stepHours);
			QueryThreadCycleTime(GetCurrentThread(), &c1);
			if (gJson) {
				jsonStepMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s0).count());
				jsonStepCyc.push_back(double(c1 - c0));
			}
			if (longrun) {
				stepMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s0).count());
				stepCyc.push_back(double(c1 - c0));
				stepAt.push_back(board.now);
				if (board.now >= nextGrowth) { growth.push_back(Measure(board)); PrintGrowth(growth.back()); nextGrowth += 10.0 * 24.0; }
			}
			if (mutants) mc.Sample(board, world, cfg, start);
			if (dragons) {   // docs/DRAGONS.md sections 4-5: places, one per place, straight flight at fly_speed, no place slots
				const DragonNumbers dn = board.DragonEff();
				std::map<std::int64_t, int> at;
				int alive = 0;
				for (auto& g : board.squads) {
					if (!board.IsDragon(g) || g.livingCount() == 0) continue;
					++alive;
					++ds.seen;
					ds.sections[g.section]++;
					if (g.smartAssigned != -1) ++ds.smartBad;
					for (auto t : { g.assignedTarget, g.currentTarget }) {
						if (t < 0) continue;
						const GraphNode& n = world.nodes[std::size_t(t)];
						if (n.dragon <= 0.f || n.city || n.settlement || n.inn || n.playerHome || n.ws != 0) ++ds.placeBad;
					}
					const std::int64_t place = g.action == Action::Reach ? g.assignedTarget : g.currentTarget;
					if (place >= 0 && ++at[place] > 1) ++ds.shared;
					if (auto it = ds.last.find(g.id); it != ds.last.end()) {
						const float dx = g.x - it->second.first, dy = g.y - it->second.second, moved = std::sqrt(dx * dx + dy * dy);
						if (moved > dn.flySpeed * float(cfg.stepHours) * 1.01f + 1.f) ++ds.flightBad;
						ds.flown += moved;
					}
					ds.last[g.id] = { g.x, g.y };
				}
				ds.maxAlive = std::max(ds.maxAlive, alive);
				++ds.steps;
			}
			if (m5d) wc.Sample(board, wd);
			if (m5f) sk.Sample(board, world, stash, cfg.Diff(DiffChannel::Economy));
			if (solstheim)
				for (auto& g : board.squads) {
					if (g.livingCount() == 0) continue;
					const int f = int(g.faction);
					if (f >= int(Faction::Skaal) && f <= int(Faction::Reaver)) (g.ws == 1 ? solSeen[f - int(Faction::Skaal)] : solInTamriel)++;
					else if (IsHumanFaction(g.faction) && g.faction != Faction::Caravan && g.faction != Faction::Vampire)
						for (auto& m : g.members) if (m.alive()) { raceSeen[g.ws][m.id] = m.race; raceFaction[g.ws][m.id] = g.faction; }
				}
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
		if (dragons) {
			auto check = [&](bool ok, const char* what, const std::string& detail) {
				std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
				JsonCheckResult("dragons", what, ok);
				if (!ok) ++failures;
			};
			char d[512];
			const auto& st = board.stats;
			const int dr = int(Faction::Dragon);
			std::printf("\nDragon checks (seed %llu, %.0f days):\n", (unsigned long long)(seed + std::uint64_t(si)), days);
			int places = 0, lairs = 0, walls = 0, high = 0;
			for (auto& n : world.nodes)
				if (n.dragon > 0.f) {
					++places;
					lairs += n.kind == Kind::DragonLair;
					walls += cfg.dragons.walls.count(n.location) ? 1 : 0;
					high += n.kind == Kind::Wilderness && n.z >= cfg.dragons.highZ;
				}
			std::snprintf(d, sizeof d, "%d places: %d lairs, %d word walls, %d high wilderness; %zu tiers", places, lairs, walls, high, cfg.dragons.tiers.size());
			check(lairs >= 8 && walls >= 30 && cfg.dragons.tiers.size() == 5, "dragon places and tiers from the config", d);
			std::string secs;
			for (auto& [s, n] : ds.sections) secs += " " + s + "=" + std::to_string(n);
			std::snprintf(d, sizeof d, "made %llu, alive at most %d (cap %d); dragon-steps by tier:%s", (unsigned long long)st.dragonsMade, ds.maxAlive, board.DragonCap(), secs.c_str());
			check(st.dragonsMade > 0 && ds.maxAlive > 0 && ds.maxAlive <= board.DragonCap(), "dragons exist, never above the cap", d);
			std::snprintf(d, sizeof d, "%llu bad targets, %llu shared places over %llu dragon-steps", (unsigned long long)ds.placeBad, (unsigned long long)ds.shared, (unsigned long long)ds.seen);
			check(ds.placeBad == 0 && ds.shared == 0, "dragon places only (no city, settlement, inn, home, Solstheim), one dragon per place", d);
			std::snprintf(d, sizeof d, "%llu flights, %llu arrivals, %.0f units flown, %llu steps faster than fly_speed", (unsigned long long)st.dragonFlights,
			              (unsigned long long)st.arrivals[dr], ds.flown, (unsigned long long)ds.flightBad);
			check(ds.flightBad == 0 && st.dragonFlights > 0 && st.arrivals[dr] > 0 && ds.flown > 0.0, "straight flight at fly_speed, arriving", d);
			int ctrl = 0;
			for (auto& s : board.smart) ctrl += s.faction == std::uint8_t(dr);
			std::snprintf(d, sizeof d, "%llu dragon-steps with a place slot, %d places controlled by dragons", (unsigned long long)ds.smartBad, ctrl);
			check(ds.smartBad == 0 && ctrl == 0, "dragons take no population / monster slot and hold no place", d);
			std::uint64_t killed = 0;
			for (int f = 0; f < kFactionCount; ++f) killed += st.killMatrix[dr][f];
			std::snprintf(d, sizeof d, "%llu squads fled, %llu fights with veterans, %llu people killed by dragons, %llu dragons lost",
			              (unsigned long long)st.dragonFlees, (unsigned long long)st.dragonFights, (unsigned long long)killed, (unsigned long long)st.membersLost[dr]);
			check(st.dragonFlees > 0, "novice and advanced people flee a dragon (Anomaly stalker panic)", d);
			if (si == 0) {
				// gate: shut = no dragon and no roll; open = one at once (the timer has never run); shut again = retired on the next step
				HarnessHost h2;
				Board b2(cfg, world, h2);
				b2.Reset(24.0 * 3 + 9.0);
				b2.rng = Rng(seed + 101);
				b2.SetGated(Faction::Dragon, true);
				b2.StartFill();
				int shutMax = 0;
				for (double end2 = b2.now + 5 * 24.0; b2.now + cfg.stepHours <= end2;) { b2.Step(cfg.stepHours); shutMax = std::max(shutMax, b2.DragonCount()); }
				const auto madeShut = b2.stats.dragonsMade;
				b2.SetGated(Faction::Dragon, false);
				b2.Step(cfg.stepHours);
				const int opened = b2.DragonCount();
				// a dragon brought down offline (worn out, a veteran squad at its place): it leaves the board, the timer restarts, news once
				{
					Group* dg = nullptr;
					for (auto& g : b2.squads) if (b2.IsDragon(g) && g.livingCount() > 0) { dg = &g; break; }
					bool killedOk = false;
					std::string why = "no dragon";
					if (dg) {
						const std::uint64_t did = dg->id;
						const auto place = std::uint32_t(dg->currentTarget >= 0 ? dg->currentTarget : dg->node);
						dg->members.front().health = 1.f;
						dg->idleHours = 1000.0;
						const double lastBefore = b2.dragonLast;
						if (Group* vet = b2.CreateSquad(place, "imperial_veteran", false)) {
							vet->action = Action::Stay;
							vet->assignedTarget = vet->currentTarget = place;
							vet->stayStart = b2.now;
							vet->idleHours = 1000.0;
							for (auto& m : vet->members) { m.level = 50; m.health = m.healthMax = 500.f; }
							b2.pinned.insert(vet->id);   // stays put whatever the place's target rules say
						}
						const auto newsBefore = h2.newsCount;
						b2.Step(cfg.stepHours);
						const Group* after2 = b2.Find(did);
						killedOk = (!after2 || after2->livingCount() == 0) && b2.dragonLast > lastBefore && b2.DragonCount() == 0 && h2.newsCount > newsBefore;
						why = std::string(after2 && after2->livingCount() > 0 ? "dragon alive" : "dragon gone") + ", timer " + (b2.dragonLast > lastBefore ? "restarted" : "unchanged") +
						      ", news " + std::to_string(h2.newsCount - newsBefore);
					}
					check(killedOk, "a dragon brought down offline leaves the board, restarts the timer, makes the news", why);
				}
				b2.SetGated(Faction::Dragon, true);
				b2.Step(cfg.stepHours);
				const int after = b2.DragonCount();
				std::snprintf(d, sizeof d, "shut 5 days: %d alive, %llu made; opened: %d; shut again: %d", shutMax, (unsigned long long)madeShut, opened, after);
				check(shutMax == 0 && madeShut == 0 && opened >= 1 && after == 0, "gate (DragonsReturned / MCM): none while shut, the first at once when opened, retired when shut", d);
				// the Dragons multiplier (T21): cap and tier move with it; 0 = none
				std::string sweep;
				int prevMax = -1, prevMade = -1;
				std::string topHalf;
				bool okSweep = true;
				for (float s : { 0.f, 0.5f, 1.f, 2.f }) {
					HarnessHost h3;
					Board b3(cfg, world, h3);
					b3.Reset(24.0 * 3 + 9.0);
					b3.rng = Rng(seed + 202);
					b3.dragonScale = s;
					b3.StartFill();
					int mx = 0;
					std::map<std::string, int> tiers;
					for (double end3 = b3.now + 20 * 24.0; b3.now + cfg.stepHours <= end3;) {
						b3.Step(cfg.stepHours);
						mx = std::max(mx, b3.DragonCount());
						for (auto& g : b3.squads) if (b3.IsDragon(g) && g.livingCount() > 0) tiers[g.section]++;
					}
					const int made = int(b3.stats.dragonsMade);
					okSweep &= mx <= b3.DragonCap() && mx >= prevMax && made >= prevMade;
					if (s == 0.f) okSweep &= made == 0;
					if (s == 0.5f) okSweep &= tiers.size() == 1 && tiers.count("dragon_1");
					const std::string top = tiers.empty() ? std::string() : tiers.rbegin()->first;   // dragon_1 .. dragon_5 sort by tier
					if (s == 2.f) okSweep &= top > topHalf && b3.DragonCap() == 4;   // x2 the level: a higher tier than at x0.5
					if (s == 0.5f) topHalf = top;
					prevMax = mx;
					prevMade = made;
					char one[160];
					std::snprintf(one, sizeof one, " x%.1f: cap %d, max %d, made %d, tiers", s, b3.DragonCap(), mx, made);
					sweep += one;
					for (auto& [k, v] : tiers) sweep += " " + k;
					sweep += ";";
				}
				check(okSweep, "Dragons multiplier: cap and tier rise with it, 0 = none", sweep);
				{   // T21: the Dragons channel is what DragonEff reads (the sweep above uses the test override on top of it)
					const float keep = cfg.diff.mult[int(DiffChannel::Dragons)];
					cfg.diff.mult[int(DiffChannel::Dragons)] = 2.f;
					HarnessHost h4;
					Board b4(cfg, world, h4);
					const int cap2 = b4.DragonCap();
					const double days2 = b4.DragonEff().respawnDays;
					cfg.diff.mult[int(DiffChannel::Dragons)] = keep;
					std::snprintf(d, sizeof d, "Diff(Dragons) 2: cap %d (config %g), respawn every %.1f days (config %.1f)", cap2, double(cfg.dragons.maxDragons), days2, cfg.dragons.respawnDays);
					check(cap2 == int(std::lround(2.f * cfg.dragons.maxDragons)) && days2 < cfg.dragons.respawnDays, "Diff(Dragons) reaches the dragon numbers", d);
				}
				// co-save (NEWS record): a 31-faction relation table from a 208 save fills the top-left, dragons keep the static row
				std::array<std::array<std::int32_t, kFactionCount>, kFactionCount> rel{};
				for (int x = 0; x < kFactionCount; ++x) for (int y = 0; y < kFactionCount; ++y) rel[x][y] = cfg.rel[x][y];
				std::vector<std::int32_t> m31(31 * 31, 7), m28(28 * 28, 9);
				const bool used31 = SaveFormat::RestoreRelations(rel, m31.data(), 31);
				const bool used28 = SaveFormat::RestoreRelations(rel, m28.data(), 28);
				const bool okRel = used31 && !used28 && rel[0][0] == 7 && rel[30][30] == 7 && rel[dr][0] == cfg.rel[dr][0] && rel[0][dr] == cfg.rel[0][dr];
				std::snprintf(d, sizeof d, "31-faction table used %d, pre-208 28-faction table used %d, dragon row static %d", int(used31), int(used28), int(rel[dr][0] == cfg.rel[dr][0]));
				check(okRel, "old saves keep their relation matrix (31 x 31 into the top-left)", d);
			}
			for (std::size_t i = 0; i < host.newsSample.size() && i < 6; ++i) std::printf("    news: %s\n", host.newsSample[i].c_str());
		}
		if (m5g) {
			auto check = [&](bool ok, const char* what, const std::string& detail) {
				std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
				JsonCheckResult("m5g", what, ok);
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
		if (solstheim) {
			auto check = [&](bool ok, const char* what, const std::string& detail) {
				std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
				JsonCheckResult("solstheim", what, ok);
				if (!ok) ++failures;
			};
			char d[512];
			const int sk = int(Faction::Skaal), rd = int(Faction::Redoran), rv = int(Faction::Reaver);
			std::printf("\nSolstheim checks (seed %llu, %.0f days):\n", (unsigned long long)(seed + std::uint64_t(si)), days);
			int now[3]{};
			for (auto& g : board.squads)
				if (g.ws == 1 && g.livingCount() > 0 && int(g.faction) >= sk && int(g.faction) <= rv) ++now[int(g.faction) - sk];
			std::snprintf(d, sizeof d, "squad-steps skaal=%llu redoran=%llu reaver=%llu (at the end %d / %d / %d), spawned %llu / %llu / %llu; in Tamriel %llu",
			              (unsigned long long)solSeen[0], (unsigned long long)solSeen[1], (unsigned long long)solSeen[2], now[0], now[1], now[2],
			              (unsigned long long)board.stats.spawned[sk], (unsigned long long)board.stats.spawned[rd], (unsigned long long)board.stats.spawned[rv],
			              (unsigned long long)solInTamriel);
			check(solSeen[0] > 0 && solSeen[1] > 0 && solSeen[2] > 0 && solInTamriel == 0, "each Solstheim faction has squads there, none in Tamriel", d);
			{   // T24: people of the general factions living on Solstheim are mostly Dunmer, rarely Altmer or Bosmer; Tamriel's mix as before.
				// Every distinct member seen over the run (the few alive at the end are too small a sample).
				std::array<std::array<double, int(RaceId::COUNT)>, kWorldspaces> n{};
				std::array<double, kWorldspaces> all{};
				std::array<int, kFactionCount> onIsland{};
				for (int ws = 0; ws < kWorldspaces; ++ws)
					for (auto& [id, r] : raceSeen[std::size_t(ws)]) {
						n[std::size_t(ws)][int(r)] += 1.0;
						all[std::size_t(ws)] += 1.0;
						if (ws == 1) ++onIsland[int(raceFaction[1][id])];
					}
				auto share = [&](int ws, RaceId r) { return all[std::size_t(ws)] > 0 ? n[std::size_t(ws)][int(r)] / all[std::size_t(ws)] : 0.0; };
				std::string who;
				for (int f = 0; f < kFactionCount; ++f) if (onIsland[f]) who += std::string(" ") + FactionId(Faction(f)) + "=" + std::to_string(onIsland[f]);
				std::snprintf(d, sizeof d, "%.0f people of general factions seen on Solstheim (%s): Dunmer %.1f%%, Nord %.1f%%, Altmer + Bosmer %.1f%%", all[1], who.c_str(),
				              100 * share(1, RaceId::Dunmer), 100 * share(1, RaceId::Nord), 100 * (share(1, RaceId::Altmer) + share(1, RaceId::Bosmer)));
				check(all[1] >= 40 && share(1, RaceId::Dunmer) > 0.45 && share(1, RaceId::Altmer) + share(1, RaceId::Bosmer) < 0.06, "Solstheim people are mostly Dunmer (T24)", d);
				std::snprintf(d, sizeof d, "%.0f people seen in Tamriel: Nord %.1f%%, Dunmer %.1f%%, Altmer + Bosmer %.1f%%", all[0], 100 * share(0, RaceId::Nord),
				              100 * share(0, RaceId::Dunmer), 100 * (share(0, RaceId::Altmer) + share(0, RaceId::Bosmer)));
				check(share(0, RaceId::Nord) > 0.25 && share(0, RaceId::Dunmer) < 0.2, "Tamriel keeps its mix", d);
			}
			const auto& k = host.kills;
			std::snprintf(d, sizeof d, "reaver killed redoran %llu, skaal %llu; redoran killed reaver %llu, skaal killed reaver %llu; reaver killed bandit %llu",
			              (unsigned long long)k[rv][rd], (unsigned long long)k[rv][sk], (unsigned long long)k[rd][rv], (unsigned long long)k[sk][rv],
			              (unsigned long long)k[rv][int(Faction::Bandit)]);
			check(k[rv][rd] + k[rv][sk] + k[rd][rv] + k[sk][rv] > 0, "Reavers fight the Redoran or the Skaal offline", d);
			check(k[sk][rd] + k[rd][sk] == 0, "Skaal and Redoran never fight each other", std::to_string(k[sk][rd] + k[rd][sk]) + " kills");
			for (std::size_t i = 0; i < world.Size(); ++i) {
				const auto& n = world.nodes[i];
				if (n.ws != 1 || board.smart[i].faction == kNoFaction) continue;
				std::printf("    %-24s %-12s held by %s\n", n.name.c_str(), KindName(n.kind), FactionId(Faction(board.smart[i].faction)));
			}
		}
		if (longrun && !stepMs.empty() && growth.size() >= 4) {
			auto check = [&](bool ok, const char* what, const std::string& detail) {
				std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
				JsonCheckResult("longrun", what, ok);
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
		if (wounds && !summary)
			std::printf("wounds: downed %llu (offline %llu), finished %llu, helped %llu, survivors %llu, got up %llu, down now %zu\n",
			            (unsigned long long)wd.counters.downed, (unsigned long long)wd.counters.ocsDowned, (unsigned long long)wd.counters.finished,
			            (unsigned long long)wd.counters.helped, (unsigned long long)wd.counters.survivors, (unsigned long long)wd.counters.gotUp, wd.down.size());
		if (m5f) {
			auto check = [&](bool ok, const char* what, const std::string& detail) {
				std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
				JsonCheckResult("m5f", what, ok);
				if (!ok) ++failures;
			};
			char d[512];
			const auto& c = stash.counters;
			int hidden = 0;
			for (auto& sp : stash.spots) hidden += sp.Filled() && sp.slot < 0;
			std::printf("\nM5f checks (seed %llu, %.0f days):\n", (unsigned long long)(seed + std::uint64_t(si)), days);
			std::snprintf(d, sizeof d, "%zu spots (%d filled and unmarked now)", stash.spots.size(), hidden);
			check(stash.spots.size() > 100 && hidden > 0, "spots near the places, the hidden share filled", d);
			std::snprintf(d, sizeof d, "%llu notes made, %llu passed to the winners, %llu lost; %d carried now by %d people",
			              (unsigned long long)c.notesMade, (unsigned long long)c.notesPassed, (unsigned long long)c.notesLost, sk.carried, sk.people);
			check(c.notesMade > 0 && c.notesPassed > 0, "the dead carry notes and the winners take them", d);
			std::snprintf(d, sizeof d, "%d samples with a member carrying more than one note", sk.doubled);
			check(sk.doubled == 0 && sk.carried <= sk.people, "one note per member at most", d);
			std::snprintf(d, sizeof d, "%d notes read off the dead, %d revealed a marked stash", sk.reads, sk.readOk);
			check(sk.reads > 0 && sk.readOk == sk.reads, "a note on a body reveals a marked stash", d);
			std::snprintf(d, sizeof d, "%d jobs paid with a stash (of %d tried), the poorest worth %d for %d gold", sk.rewards, sk.rewardTries, sk.minRewardValue, sk.rewardGold);
			check(sk.rewards > 0 && sk.minRewardValue >= sk.rewardGold, "jobs pay with a stash worth the gold it replaces", d);
			std::snprintf(d, sizeof d, "at most %d marked at once (slots %d), %llu markers given up, %d looted", sk.maxMarked, stash.cfg.markerSlots,
			              (unsigned long long)c.evicted, sk.looted);
			check(sk.maxMarked <= stash.cfg.markerSlots && sk.looted > 0, "marker slots hold, looted stashes go back to the pool", d);
		}
		if (m5d) {
			auto check = [&](bool ok, const char* what, const std::string& detail) {
				std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
				JsonCheckResult("m5d", what, ok);
				if (!ok) ++failures;
			};
			char d[512];
			const auto& c = wd.counters;
			std::printf("\nM5d checks (seed %llu, %.0f days):\n", (unsigned long long)(seed + std::uint64_t(si)), days);
			std::snprintf(d, sizeof d, "%llu went down offline in %llu battles; %llu finished, %llu patched up by squadmates, %llu squads lay wounded, %llu got up",
			              (unsigned long long)c.ocsDowned, (unsigned long long)board.stats.battles, (unsigned long long)c.finished, (unsigned long long)c.helped,
			              (unsigned long long)c.survivors, (unsigned long long)c.gotUp);
			check(c.ocsDowned > 0 && c.finished > 0 && c.helped > 0 && c.survivors > 0, "down, finish, help and wounded survivors all happen", d);
			std::snprintf(d, sizeof d, "%llu bad records (dead member or missing squad) in %llu step samples%s%s", (unsigned long long)wc.badRecords,
			              (unsigned long long)wc.samples, wc.firstBad.empty() ? "" : "; first: ", wc.firstBad.c_str());
			check(wc.badRecords == 0, "every record is a living member of an existing squad", d);
			std::snprintf(d, sizeof d, "longest wounded survivors stay %.1f game h (down_hours max %.0f + one step)", wc.longestSurvivors, woundCfg.downHoursMax);
			check(wc.longestSurvivors <= woundCfg.downHoursMax + cfg.stepHours + 0.01, "survivors get up in time", d);
			std::snprintf(d, sizeof d, "%llu step samples where survivors moved or fought", (unsigned long long)wc.survivorsActive);
			check(wc.survivorsActive == 0, "survivors stay put out of fights", d);
			std::snprintf(d, sizeof d, "%llu step samples with someone down in a squad that has someone standing and did not fight", (unsigned long long)wc.unhelped);
			check(wc.unhelped == 0, "squadmates patch up the down once the fight is over", d);
			std::snprintf(d, sizeof d, "%zu down at the end, peak %zu", wd.down.size(), wc.peakDown);
			check(wc.peakDown < 400, "bounded", d);
		}
		if (mutants) {
			auto check = [&](bool ok, const char* what, const std::string& detail) {
				std::printf("  [%s] %s: %s\n", ok ? "PASS" : "FAIL", what, detail.c_str());
				JsonCheckResult("mutants", what, ok);
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
		if (gJson) {   // this seed's run (ticket G8): squads by faction and kind, online picks, kills, downs, news, CPU
			JsonOut j;
			j.Obj();
			j.Key("seed").U(seed + std::uint64_t(si)).Key("days").D(days).Key("steps").U(board.stats.steps).Key("end_day").D(board.now / 24.0);
			j.Key("squads").Obj();
			j.Key("total").U(board.squads.size());
			j.Key("by_faction").Obj();
			for (int f = 0; f < kFactionCount; ++f) {
				j.Key(FactionId(Faction(f))).Obj();
				j.Key("squads").I(board.SquadCount(Faction(f))).Key("members").I(board.MemberCount(Faction(f)));
				j.Key("spawned").U(board.stats.spawned[f]).Key("wiped").U(board.stats.wiped[f]).Key("killed").U(board.stats.membersKilled[f]);
				j.Key("lost").U(board.stats.membersLost[f]).Key("arrivals").U(board.stats.arrivals[f]);
				j.EndObj();
			}
			j.EndObj();
			std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> byKind{ { "people", {} }, { "monsters", {} }, { "dragons", {} } };   // squads, living members
			std::map<std::string, std::uint64_t> bySection;
			int reach = 0, stay = 0, none = 0, locked = 0;
			for (auto& g : board.squads) {
				auto& k = byKind[board.IsDragon(g) ? "dragons" : cfg.IsMonster(g.faction) ? "monsters" : "people"];
				++k.first;
				k.second += std::uint64_t(g.livingCount());
				++bySection[g.section];
				(g.action == Action::Reach ? reach : g.action == Action::Stay ? stay : none)++;
				locked += g.locked;
			}
			j.Key("by_kind").Obj();
			for (auto& [k, v] : byKind) j.Key(k).Obj().Key("squads").U(v.first).Key("members").U(v.second).EndObj();
			j.EndObj();
			j.Key("by_section").Obj();
			for (auto& [k, v] : bySection) j.Key(k).U(v);
			j.EndObj();
			j.Key("states").Obj().Key("reach").I(reach).Key("stay").I(stay).Key("none").I(none).Key("locked").I(locked).EndObj();
			j.EndObj();
			{   // online picks: squads the plugin's online pick could take at the end (Tamriel, outside a city buffer, monsters in their hours)
				std::map<std::string, std::uint64_t> elig;
				std::uint64_t n = 0;
				for (auto& g : board.squads) {
					if (g.members.empty() || g.ws != 0 || world.InCityBuffer(g.x, g.y, kCityBuffer, 0)) continue;
					if (!IsHumanFaction(g.faction) && !board.MonsterMayComeOnline(g, HourOfDay(board.now))) continue;
					++n;
					++elig[FactionId(g.faction)];
				}
				j.Key("online").Obj().Key("eligible_at_end").U(n).Key("eligible_by_faction").Obj();
				for (auto& [k, v] : elig) j.Key(k).U(v);
				j.EndObj().EndObj();
			}
			{
				std::uint64_t total = 0;
				j.Key("kills").Obj().Key("matrix").Obj();   // killer -> victim -> members, offline fights (only non-zero cells)
				for (int a = 0; a < kFactionCount; ++a) {
					bool any = false;
					for (int v = 0; v < kFactionCount; ++v) any = any || board.stats.killMatrix[a][v] > 0;
					if (!any) continue;
					j.Key(FactionId(Faction(a))).Obj();
					for (int v = 0; v < kFactionCount; ++v)
						if (board.stats.killMatrix[a][v] > 0) { j.Key(FactionId(Faction(v))).U(board.stats.killMatrix[a][v]); total += board.stats.killMatrix[a][v]; }
					j.EndObj();
				}
				j.EndObj().Key("total").U(total).EndObj();
			}
			if (wounds) {
				const auto& c = wd.counters;
				j.Key("downs").Obj().Key("downed").U(c.downed).Key("offline").U(c.ocsDowned).Key("finished").U(c.finished).Key("helped").U(c.helped);
				j.Key("survivors").U(c.survivors).Key("got_up").U(c.gotUp).Key("down_now").U(wd.down.size()).EndObj();
			}
			else j.Key("downs").Null();
			j.Key("news").Obj().Key("lines").U(host.newsCount);
			j.Key("by_kind").Obj();
			for (auto& [k, v] : host.newsKinds) j.Key(k).U(v);
			j.EndObj();
			j.Key("sos").U(board.stats.sosNews).Key("death").U(board.stats.deathNews).Key("battle").U(board.stats.battleNews).Key("gossip").U(board.stats.gossipNews);
			j.Key("sample").Arr();
			for (auto& n : host.newsSample) j.Str(n);
			j.EndArr().EndObj();
			const auto& st = board.stats;
			j.Key("totals").Obj().Key("battles").U(st.battles).Key("retreats").U(st.retreats).Key("retargets").U(st.retargets).Key("locks").U(st.locks);
			j.Key("control_changes").U(st.controlChanges).Key("trades").U(st.trades).Key("raids_won").U(st.raidsWon).Key("raids_lost").U(st.raidsLost);
			j.Key("dungeons_cleared").U(st.dungeonsCleared).Key("dungeons_aborted").U(st.dungeonsAborted).Key("hunts").U(st.hunts).Key("fishings").U(st.fishings);
			j.Key("places_at_war").D(all.back().atWar).Key("fort_camp_control").Obj();
			for (auto& [k, v] : all.back().ctrl) j.Key(k).D(v);
			j.EndObj().EndObj();
			JsonCpu(j, jsonStepMs, jsonStepCyc, ms);
			j.EndObj();
			gJsonRuns.push_back(j.s);
		}
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
	if (m5d) std::printf("\nM5d checks: %s\n", failures ? "FAILED" : "all passed");
	if (m5f) std::printf("\nM5f checks: %s\n", failures ? "FAILED" : "all passed");
	if (solstheim) std::printf("\nSolstheim checks: %s\n", failures ? "FAILED" : "all passed");
	if (mutants) std::printf("\nmutant checks: %s\n", failures ? "FAILED" : "all passed");
	if (dragons) std::printf("\nDragon checks: %s\n", failures ? "FAILED" : "all passed");
	return finish(failures, failures ? 1 : 0);
}
