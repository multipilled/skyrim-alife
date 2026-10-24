// Headless tests for the wounded, help, finish and surrender (docs/WOUNDED.md, M5d): wounded.ltx, offline down / finish / wounded
// survivors / squadmate help in the board's combat, on-screen timers, the yield and surrender checks, the player's choices with their
// standing, and the WOND co-save blob (old saves: no WOND = nobody down). Build: tests\build_wounds_test.bat ; run: wounds_test.exe [config dir]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "ALife/Board.h"
#include "ALife/Config.h"
#include "ALife/Ltx.h"
#include "ALife/SaveFormat.h"
#include "ALife/Standing.h"
#include "ALife/World.h"
#include "ALife/Wounds.h"

using namespace ALife;

static int gFails = 0, gPass = 0;
#define CHECK(cond, msg) do { if (cond) { ++gPass; } else { ++gFails; std::printf("FAIL: %s   [%s]\n", msg, #cond); } } while (0)

namespace {
	struct TestHost : Host
	{
		int news{ 0 };
		std::string lastNews;
		Member MakeMember(Faction f, Tier, Role role, int level, bool, Rng&, std::uint64_t id) override
		{
			Member m;
			m.id = id; m.faction = f; m.role = role; m.level = level;
			m.health = m.healthMax = 100.f;
			return m;
		}
		float MemberPower(const Member& m) const override { return 10.f + 6.f * float(m.level); }
		void News(const std::string& line, float, float) override { ++news; lastNews = line; }
	};

	GraphNode Node(const char* name, Kind k, float x, float y)
	{
		GraphNode n;
		n.name = name; n.kind = k; n.x = x; n.y = y; n.radius = 1500.f;
		return n;
	}

	std::uint64_t gMemberId = 1000;
	Group Squad(std::uint64_t id, Faction f, const World& w, std::uint32_t node, int members, int level)
	{
		Group g;
		g.id = id; g.faction = f; g.tier = Tier::Advanced; g.ws = 0;
		g.x = w.nodes[node].x; g.y = w.nodes[node].y; g.node = node;
		g.action = Action::Stay; g.assignedTarget = g.currentTarget = node; g.stayStart = 0; g.idleHours = 100000;
		for (int i = 0; i < members; ++i) {
			Member m;
			m.id = ++gMemberId; m.faction = f; m.level = level; m.health = m.healthMax = 100.f; m.gold = 50;
			m.role = i == 0 ? Role::Leader : Role::Melee;
			g.members.push_back(m);
		}
		g.startMembers = members;
		return g;
	}

	int Count(const std::vector<WoundEvent>& ev, WoundEvent::Kind k)
	{
		int n = 0;
		for (auto& e : ev) n += e.kind == k;
		return n;
	}
	bool Near(float a, float b, float eps = 0.5f) { return std::fabs(a - b) <= eps; }

	// A small busy world (two places, eight squads of four warring factions, refilled as they die) for a_days with a_wc.
	struct SoakResult
	{
		Wounds::Counters c;
		std::uint64_t battles{ 0 }, deaths{ 0 };
		bool records{ true };       // every record is a living member of an existing squad, after every step
		bool health{ true };        // every member's health within [0, max]; everyone down has at least 1
		double longestSurvivors{ 0 };
		std::string firstBad;       // the first broken record or health, for the log
	};
	SoakResult Soak(const Config& a_cfg, const World& a_w, const WoundConfig& a_wc, int a_days, std::uint64_t a_seed)
	{
		SoakResult r;
		TestHost host;
		World w = a_w;
		Board b(a_cfg, w, host);
		Wounds wd;
		wd.cfg = a_wc;
		b.wounds = &wd;
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		b.rng = Rng(a_seed);
		const Faction sides[4] = { Faction::Imperial, Faction::Bandit, Faction::Stormcloak, Faction::Forsworn };
		std::uint64_t id = 1;
		std::unordered_map<std::uint64_t, double> since;
		for (int h = 0; h < a_days * 24 * 2; ++h) {
			while (b.squads.size() < 8) {
				const std::uint64_t k = id++;
				b.squads.push_back(Squad(k, sides[k % 4], w, std::uint32_t(k % 2), 3, 5 + int(k % 10)));
			}
			b.Step(0.5);
			for (auto& [mid, d] : wd.down) {
				Group* g = b.Find(d.squad);
				bool found = false;
				if (g) for (auto& m : g->members) if (m.id == mid && m.alive()) found = true;
				if (!found && r.records) { r.records = false; r.firstBad = "record of member " + std::to_string(mid) + " (squad " + std::to_string(d.squad) + (g ? ")" : ", gone)"); }
			}
			for (auto& g : b.squads) {
				for (auto& m : g.members) {
					const bool bad = !(m.health >= 0.f && m.health <= m.healthMax + 0.01f) || (wd.IsDown(m.id) && m.health < 1.f);
					if (bad && r.health) {
						r.health = false;
						r.firstBad += " health " + std::to_string(m.health) + "/" + std::to_string(m.healthMax) + (wd.IsDown(m.id) ? " (down)" : "") + " at h " + std::to_string(b.now);
					}
				}
				if (wd.Survivors(g)) {
					auto it = since.emplace(g.id, b.now).first;
					r.longestSurvivors = std::max(r.longestSurvivors, b.now - it->second);
				} else since.erase(g.id);
			}
		}
		r.c = wd.counters;
		r.battles = b.stats.battles;
		for (auto v : b.stats.membersLost) r.deaths += v;
		return r;
	}
	void PrintSoak(const char* a_label, const SoakResult& r)
	{
		std::printf("  %-22s battles %4llu deaths %4llu | downed %4llu (offline %4llu) finished %4llu helped %4llu survivors %3llu got up %4llu | longest survivors %.1f h\n",
		            a_label, (unsigned long long)r.battles, (unsigned long long)r.deaths, (unsigned long long)r.c.downed, (unsigned long long)r.c.ocsDowned,
		            (unsigned long long)r.c.finished, (unsigned long long)r.c.helped, (unsigned long long)r.c.survivors, (unsigned long long)r.c.gotUp, r.longestSurvivors);
		if (!r.firstBad.empty()) std::printf("    first problem: %s\n", r.firstBad.c_str());
	}
	// every chance / share of a config in [0, 1], ranges ordered, nothing negative or NaN
	bool InRange(const WoundConfig& c)
	{
		auto p = [](float v) { return std::isfinite(v) && v >= 0.f && v <= 1.f; };
		auto nn = [](float v) { return std::isfinite(v) && v >= 0.f; };
		bool ok = p(c.hp) && p(c.chance) && p(c.ocsChance) && p(c.downHpMin) && p(c.downHpMax) && c.downHpMin <= c.downHpMax && c.downHpMin >= 0.01f &&
		          p(c.reviveHp) && c.reviveHp >= 0.05f && p(c.yieldChance) && p(c.losingShare) && c.outnumber >= 1.f &&
		          nn(c.helpRadius) && nn(c.finishRadius) && c.selfHealSeconds >= 1.f && c.finishDelayMin <= c.finishDelayMax && nn(c.finishDelayMin) &&
		          c.downHoursMin <= c.downHoursMax && nn(c.downHoursMin) && c.helperRankMin >= 0 && c.helperRankMin <= c.helperRankMax &&
		          nn(c.woundHelp) && nn(c.rob) && nn(c.robMemory) && nn(c.finishDeed) && nn(c.spare) && nn(c.prisoner) && nn(c.prisonerNotoriety);
		for (int f = 0; f < kFactionCount; ++f) ok = ok && p(c.finish[f]) && p(c.surrender[f]);
		return ok;
	}
	// all chances of a config set to a_p (the extremes test)
	WoundConfig AllChances(WoundConfig c, float a_p)
	{
		c.chance = c.ocsChance = c.yieldChance = a_p;
		for (int f = 0; f < kHumanFactionCount; ++f) { c.finish[f] = a_p; c.surrender[f] = a_p; }
		return c;
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const std::string dir = argc > 1 ? argv[1] : "config";
	Config cfg;
	std::string err;
	if (!LoadConfig(dir, cfg, err)) { std::printf("cannot load config: %s\n", err.c_str()); return 2; }
	Ltx wl;
	if (!wl.LoadFile(dir + "/wounded.ltx", &err)) { std::printf("cannot load wounded.ltx: %s\n", err.c_str()); return 2; }

	// ---- 0. wounded.ltx and the standing deeds
	std::printf("section 0: config\n");
	WoundConfig wc;
	const bool loaded = LoadWoundConfig(wl, wc, err);
	if (!loaded) std::printf("wounded.ltx: %s\n", err.c_str());
	CHECK(loaded, "wounded.ltx parses");
	CHECK(wc.enabled && Near(wc.hp, 0.2f, 1e-4f) && Near(wc.chance, 0.7f, 1e-4f) && Near(wc.ocsChance, 0.35f, 1e-4f), "[wounded] thresholds");
	CHECK(Near(wc.downHpMin, 0.05f, 1e-4f) && Near(wc.downHpMax, 0.15f, 1e-4f) && Near(wc.reviveHp, 0.3f, 1e-4f), "down_hp and revive_hp");
	CHECK(wc.callSeconds == 15.f && wc.callRadius == 3000.f && wc.helpRadius == 2500.f && wc.selfHealSeconds == 90.f, "call / help / self-heal");
	CHECK(wc.finishRadius == 1500.f && wc.finishDelayMin == 2.f && wc.finishDelayMax == 5.f && wc.downHoursMin == 2.f && wc.downHoursMax == 6.f, "finish and survivors");
	CHECK(wc.helperRankMin == 10 && wc.helperRankMax == 20, "helper rank");
	CHECK(wc.yieldDistance == 600.f && Near(wc.yieldChance, 0.25f, 1e-4f) && Near(wc.losingShare, 0.5f, 1e-4f) && Near(wc.outnumber, 1.5f, 1e-4f), "[surrender] yield / losing / outnumbered");
	CHECK(wc.spareHours == 12.f && wc.releaseRadius == 3000.f && wc.surrenderSeconds == 120.f, "spare / release");
	bool allHuman = true;
	for (int f = 0; f < kHumanFactionCount; ++f) allHuman = allHuman && wc.factions[f];
	CHECK(allHuman, "every human faction can go down");
	CHECK(wc.surrender[int(Faction::Thalmor)] == 0.f && wc.surrender[int(Faction::Forsworn)] == 0.f && wc.surrender[int(Faction::Necromancer)] == 0.f &&
	      wc.surrender[int(Faction::Vampire)] == 0.f && wc.surrender[int(Faction::SilverHand)] == 0.f && wc.surrender[int(Faction::Reaver)] == 0.f,
	      "fanatics fight to the end");
	CHECK(wc.finish[int(Faction::Bandit)] > wc.finish[int(Faction::Imperial)] && wc.finish[int(Faction::Caravan)] < 0.2f, "outlaws finish more than soldiers");
	{
		const WoundConfig d = DefaultWoundConfig();
		bool same = true;
		for (int f = 0; f < kHumanFactionCount; ++f) same = same && Near(d.finish[f], wc.finish[f], 1e-4f) && Near(d.surrender[f], wc.surrender[f], 1e-4f);
		CHECK(same, "compiled-in defaults match wounded.ltx");
		Ltx bad;
		bad.LoadString("[wounded]\nfactions = bandit, wolf\n");
		WoundConfig x;
		std::string e2;
		CHECK(!LoadWoundConfig(bad, x, e2) && e2.find("wolf") != std::string::npos, "a monster or unknown faction in factions is an error");
		Ltx none;
		none.LoadString("[surrender]\nyield_chance = 1\n");
		CHECK(!LoadWoundConfig(none, x, e2), "no [wounded] section is an error");
	}
	const auto& sc = cfg.standing;
	CHECK(wc.woundHelp == 40.f && wc.rob == 15.f && wc.finishDeed == 30.f && wc.spare == 20.f && wc.prisoner == 60.f && wc.prisonerNotoriety == 40.f, "wounded.ltx [standing] goodwill and notoriety");
	CHECK(wc.woundHelpMemory == 300.f && wc.robMemory == 200.f && wc.spareMemory == 250.f && wc.fallBackHoursMin == 3.f && wc.fallBackHoursMax == 6.f, "wounded.ltx [standing] memory, fall_back_hours");

	// ---- world: two places close together
	cfg.minSquadsForOcs = 0;
	cfg.ocsMode = OcsMode::Full;
	cfg.fleeRatio = 0.f;          // nobody runs: fights end with one side down or dead
	cfg.dynamicRelations = false;
	World w;
	w.nodes.push_back(Node("Riverwood Road", Kind::Wilderness, 0, 0));   // 0
	w.nodes.push_back(Node("Fort Greymoor", Kind::Fort, 30000, 0));      // 1
	w.Finalize(cfg);
	CHECK(cfg.IsEnemy(Faction::Imperial, Faction::Bandit), "the Legion and the bandits are enemies");

	// ---- 1. without Board::wounds nothing changes
	std::printf("section 1: no wounds\n");
	{
		TestHost host;
		Board b(cfg, w, host);
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		b.squads.push_back(Squad(1, Faction::Imperial, w, 0, 3, 10));
		CHECK(b.StandingCount(b.squads[0]) == 3 && b.Stands(b.squads[0].members[0]), "without wounds everyone alive stands");
		b.squads[0].members[1].health = 0.f;
		CHECK(b.StandingCount(b.squads[0]) == 2, "the dead do not stand");
	}

	// ---- 2. offline: a beaten squad lies wounded (ocs_chance 1, nobody finishes), then gets up and falls back
	std::printf("section 2: wounded survivors\n");
	{
		TestHost host;
		Board b(cfg, w, host);
		Wounds wd;
		wd.cfg = wc;
		wd.cfg.ocsChance = 1.f;
		wd.cfg.finish[int(Faction::Imperial)] = 0.f;
		b.wounds = &wd;
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		b.squads.push_back(Squad(1, Faction::Imperial, w, 0, 5, 30));
		b.squads.push_back(Squad(2, Faction::Bandit, w, 0, 3, 1));
		b.pinned = { 1, 2 };   // both hold the place: they meet there
		for (int i = 0; i < 12 && b.Find(2) && b.StandingCount(*b.Find(2)) > 0; ++i) b.Step(0.5);
		Group* ban = b.Find(2);
		CHECK(b.stats.battles > 0, "they fought");
		CHECK(ban && ban->livingCount() == 3 && b.StandingCount(*ban) == 0 && wd.DownCount(*ban) == 3, "every bandit went down instead of dying");
		CHECK(ban && wd.Survivors(*ban), "the beaten squad is wounded survivors");
		CHECK(wd.counters.ocsDowned >= 3 && wd.counters.survivors == 1, "counters: downed offline, one survivors squad");
		CHECK(b.stats.wiped[int(Faction::Bandit)] == 0 && b.stats.membersLost[int(Faction::Bandit)] == 0, "nobody died");
		CHECK(host.lastNews.find("SOS: wounded fighters of the") != std::string::npos, "an SOS calls for help");
		bool hpOk = true;
		for (auto& m : ban->members) hpOk = hpOk && m.health >= 1.f && m.health <= 0.15f * m.healthMax + 0.01f;
		CHECK(hpOk, "down at down_hp (5-15%)");
		CHECK(ban->assignedTarget == -1 && ban->smartAssigned == -1 && ban->action == Action::None, "out of the simulation: no target, no place");
		double upAt = 0;
		for (auto& m : ban->members) upAt = std::max(upAt, wd.Find(m.id)->upAt);
		CHECK(upAt >= b.now + 0.5 && upAt <= b.now + 6.0 + 0.01, "they get up after down_hours (2-6)");
		const float x0 = ban->x, y0 = ban->y;
		const auto battles = b.stats.battles;
		while (b.now + 0.5 < upAt) {
			b.Step(0.5);
			ban = b.Find(2);
			if (!ban || !wd.Survivors(*ban)) break;
		}
		ban = b.Find(2);
		CHECK(ban && wd.Survivors(*ban) && ban->x == x0 && ban->y == y0, "survivors do not move");
		CHECK(b.stats.battles == battles, "survivors are neither attackers nor targets (the Legion stands next to them)");
		CHECK(ban && ban->members[0].health < 0.16f * ban->members[0].healthMax, "the down do not heal while they lie there");
		std::vector<WoundEvent> ev;
		b.now += 1.0;
		wd.Tick(b, 1.0, &ev);
		ban = b.Find(2);
		CHECK(Count(ev, WoundEvent::Kind::Up) == 3 && ban && wd.DownCount(*ban) == 0, "time up: all three get up");
		CHECK(ban && Near(ban->members[0].health, wc.reviveHp * ban->members[0].healthMax), "at revive_hp");
		CHECK(ban && ban->retreatUntilHours > b.now && ban->avoidNode == std::int64_t(ban->node), "and fall back like a squad that fled");
	}

	// ---- 3. offline: the winner finishes the down (finish chance 1), so the squad is wiped and the kills are credited
	std::printf("section 3: finish\n");
	{
		TestHost host;
		Board b(cfg, w, host);
		Wounds wd;
		wd.cfg = wc;
		wd.cfg.ocsChance = 1.f;
		wd.cfg.finish[int(Faction::Imperial)] = 1.f;
		b.wounds = &wd;
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		b.squads.push_back(Squad(1, Faction::Imperial, w, 0, 5, 30));
		b.squads.push_back(Squad(2, Faction::Bandit, w, 0, 3, 1));
		b.pinned = { 1, 2 };
		for (int i = 0; i < 12 && b.Find(2); ++i) b.Step(0.5);
		CHECK(!b.Find(2), "the beaten squad was finished and is gone");
		CHECK(b.stats.wiped[int(Faction::Bandit)] == 1 && b.stats.membersLost[int(Faction::Bandit)] == 3, "wiped, three dead");
		CHECK(b.stats.killMatrix[int(Faction::Imperial)][int(Faction::Bandit)] == 3, "the kills are credited to the winner");
		CHECK(wd.counters.finished >= 1 && wd.down.empty(), "finished, no records left");
	}
	// monsters take no prisoners (AfterBattle directly, a wildlife winner)
	{
		TestHost host;
		Board b(cfg, w, host);
		Wounds wd;
		wd.cfg = wc;
		b.wounds = &wd;
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		b.squads.push_back(Squad(1, Faction::Wildlife, w, 0, 2, 5));
		b.squads.push_back(Squad(2, Faction::Bandit, w, 0, 3, 5));
		Group& wolves = b.squads[0];
		Group& ban = b.squads[1];
		CHECK(!wd.CanGoDown(b, wolves) && wd.CanGoDown(b, ban), "beasts never go down, bandits do");
		CHECK(!wd.RollDown(b, wolves, wolves.members[0], b.rng), "a wolf cannot be rolled down");
		for (auto& m : ban.members) wd.GoDown(b, ban, m, true);
		const int killed = wd.AfterBattle(b, ban, wolves, false);
		CHECK(killed == 3 && ban.livingCount() == 0 && wd.down.empty(), "a monster winner kills every down enemy");
	}
	// a fled loser leaves its down behind: a winner that never finishes leaves them, and they are not survivors
	{
		TestHost host;
		Board b(cfg, w, host);
		Wounds wd;
		wd.cfg = wc;
		wd.cfg.finish[int(Faction::Caravan)] = 0.f;
		b.wounds = &wd;
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		b.squads.push_back(Squad(1, Faction::Caravan, w, 0, 2, 5));
		b.squads.push_back(Squad(2, Faction::Bandit, w, 0, 3, 5));
		wd.GoDown(b, b.squads[1], b.squads[1].members[2], true);
		CHECK(wd.AfterBattle(b, b.squads[1], b.squads[0], true) == 0 && wd.counters.survivors == 0, "fled: nobody finished, no survivors");
	}

	// ---- 4. squadmates help: after the fight the down get up at revive_hp, the helper earns rank; not in the middle of a fight
	std::printf("section 4: help\n");
	{
		TestHost host;
		Board b(cfg, w, host);
		Wounds wd;
		wd.cfg = wc;
		b.wounds = &wd;
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		b.squads.push_back(Squad(1, Faction::Imperial, w, 0, 3, 10));
		Group& g = b.squads[0];
		g.members[2].health = 8.f;
		wd.GoDown(b, g, g.members[2], true);
		CHECK(!b.Stands(g.members[2]) && g.members[2].alive() && b.StandingCount(g) == 2 && wd.DownCount(g) == 1, "down: alive, not standing");
		const float full = b.SquadPower(g);
		CHECK(full > 0.f, "power counts those standing");
		wd.foughtThisStep.insert(g.id);
		std::vector<WoundEvent> ev;
		wd.Tick(b, 0.5, &ev);
		CHECK(wd.IsDown(g.members[2].id) && Count(ev, WoundEvent::Kind::Helped) == 0, "no help in the middle of a fight");
		CHECK(wd.foughtThisStep.empty(), "the fight marks clear every step");
		const float xp0 = g.members[0].rankXp + g.members[1].rankXp;
		wd.Tick(b, 0.5, &ev);
		CHECK(!wd.IsDown(g.members[2].id) && Count(ev, WoundEvent::Kind::Helped) == 1, "after the fight a squadmate patches it up");
		CHECK(Near(g.members[2].health, wc.reviveHp * g.members[2].healthMax), "it gets up at revive_hp");
		CHECK(g.members[0].rankXp + g.members[1].rankXp > xp0, "the helper earns rank");
		// stale records go: a down member that died some other way, or whose squad is gone
		wd.GoDown(b, g, g.members[1], true);
		g.members[1].health = 0.f;
		wd.down[777] = Down{ 777, 99, 0, 0, 0, 0 };
		wd.Tick(b, 0.5, &ev);
		CHECK(wd.down.empty(), "stale records are dropped");
	}

	// ---- 5. on screen: rolling down, help calls, self-heal, choosing a helper
	std::printf("section 5: on screen\n");
	{
		TestHost host;
		Board b(cfg, w, host);
		Wounds wd;
		wd.cfg = wc;
		wd.cfg.chance = 1.f;
		b.wounds = &wd;
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		b.squads.push_back(Squad(1, Faction::Bandit, w, 0, 3, 10));
		Group& g = b.squads[0];
		g.online = true;
		Rng rng(7);
		CHECK(wd.RollDown(b, g, g.members[0], rng) && wd.IsDown(g.members[0].id) && !wd.Find(g.members[0].id)->Offline(), "online: crossing the line with chance 1 goes down");
		CHECK(!wd.RollDown(b, g, g.members[0], rng), "already down");
		wd.cfg.chance = 0.f;
		CHECK(!wd.RollDown(b, g, g.members[1], rng), "chance 0: fights on");
		std::vector<WoundEvent> ev;
		std::unordered_set<std::uint64_t> near{ g.id };
		wd.UpdateOnline(b, 1.f, near, ev);
		CHECK(Count(ev, WoundEvent::Kind::Call) == 1 && ev[0].by == 1, "the first help call (with a notice)");
		ev.clear();
		wd.UpdateOnline(b, 5.f, near, ev);
		CHECK(Count(ev, WoundEvent::Kind::Call) == 0, "no repeat before call_seconds");
		wd.UpdateOnline(b, 10.f, near, ev);
		CHECK(Count(ev, WoundEvent::Kind::Call) == 1 && ev.back().by == 0, "the next call after call_seconds, no notice");
		ev.clear();
		wd.UpdateOnline(b, 20.f, {}, ev);
		CHECK(Count(ev, WoundEvent::Kind::Call) == 0, "no calls with the player away");
		wd.UpdateOnline(b, 60.f, {}, ev);
		CHECK(Count(ev, WoundEvent::Kind::SelfHeal) == 1, "self-heal after self_heal_seconds");
		wd.GetUp(b, g, g.members[0], 1.f);
		CHECK(!wd.IsDown(g.members[0].id) && g.members[0].health == g.members[0].healthMax, "a potion: up with full health");
		// helper choice
		g.members[2].health = 12.f;   // badly hurt
		wd.GoDown(b, g, g.members[2], false);
		std::vector<float> dist{ 2000.f, 900.f, 10.f };
		CHECK(wd.ChooseHelper(g, dist, false) == 1, "the nearest standing squadmate within help_radius (not the wounded itself)");
		CHECK(wd.ChooseHelper(g, dist, true) == -1, "not while the squad is fighting");
		dist = { 3000.f, 2600.f, 10.f };
		CHECK(wd.ChooseHelper(g, dist, false) == -1, "nobody within help_radius");
		const float xp0 = g.members[0].rankXp;
		wd.Help(b, g, g.members[2], g.members[0], false, rng);
		CHECK(!wd.IsDown(g.members[2].id) && Near(g.members[2].health, wc.reviveHp * 100.f) && g.members[0].rankXp > xp0, "helped without a potion: revive_hp, helper rank");
		CHECK(wd.MayFinish(1400.f, false) && !wd.MayFinish(1600.f, false) && !wd.MayFinish(100.f, true), "finish_radius, not while fighting someone standing");
		const float d0 = wd.FinishDelay(rng);
		CHECK(d0 >= 2.f && d0 <= 5.f, "finish_delay 2-5 s");
	}

	// ---- 6. surrender and yield
	std::printf("section 6: surrender\n");
	{
		TestHost host;
		Board b(cfg, w, host);
		Wounds wd;
		wd.cfg = wc;
		b.wounds = &wd;
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		b.squads.push_back(Squad(1, Faction::Bandit, w, 0, 4, 10));
		b.squads.push_back(Squad(2, Faction::Thalmor, w, 0, 4, 10));
		b.squads.push_back(Squad(3, Faction::Bandit, w, 0, 2, 10));
		Group& g = b.squads[0];
		Group& th = b.squads[1];
		Rng rng(11);
		CHECK(!wd.Losing(g) && !wd.Outnumbered(g, 1), "a fresh squad is neither losing nor outnumbered");
		CHECK(!wd.CheckSurrender(b, g, 1, rng, 1.f) && !wd.fights[g.id].rolled, "no reason to give up: not even rolled");
		g.members[3].health = 0.f;                 // one dead ...
		wd.GoDown(b, g, g.members[2], false);      // ... one down: half of the four it started with
		CHECK(wd.Losing(g), "losing: half dead or down");
		CHECK(wd.Outnumbered(g, 3) && !wd.Outnumbered(g, 2), "outnumbered at 1.5 x its two standing");
		CHECK(wd.CheckSurrender(b, g, 1, rng, 1.f), "the squad gives up");
		CHECK(wd.SquadYielded(g.id) && b.StandingCount(g) == 0 && wd.Find(g.members[0].id)->Yielded() && wd.Find(g.members[2].id)->Yielded(), "every living member kneels, yielded");
		CHECK(!wd.CheckSurrender(b, g, 1, rng, 1.f), "rolled once per fight");
		th.members[0].health = th.members[1].health = 0.f;
		CHECK(!wd.CheckSurrender(b, th, 5, rng) && wd.fights[th.id].rolled && !wd.SquadYielded(th.id), "the Thalmor (surrender 0) fight to the end");
		// single yield
		wd.cfg.yieldChance = 1.f;
		Group& g3 = b.squads[2];
		wd.GoDown(b, g3, g3.members[0], false);
		CHECK(!wd.RollYield(b, g3, g3.members[0], 500.f, true, false, rng), "not hostile: no yield");
		CHECK(!wd.RollYield(b, g3, g3.members[0], 700.f, true, true, rng), "beyond yield_distance");
		CHECK(!wd.RollYield(b, g3, g3.members[0], 500.f, false, true, rng), "the player not armed");
		CHECK(wd.RollYield(b, g3, g3.members[0], 500.f, true, true, rng) && wd.Find(g3.members[0].id)->Yielded(), "down, hostile, armed player close: yields");
		wd.GoDown(b, g3, g3.members[1], false);
		wd.cfg.yieldChance = 0.f;
		CHECK(!wd.RollYield(b, g3, g3.members[1], 500.f, true, true, rng), "chance 0");
		wd.cfg.yieldChance = 1.f;
		CHECK(!wd.RollYield(b, g3, g3.members[1], 500.f, true, true, rng), "rolled once per member");
		wd.GoDown(b, th, th.members[2], false);
		CHECK(!wd.RollYield(b, th, th.members[2], 100.f, true, true, rng), "fanatics never yield");
		// left alone: released after surrender_seconds
		std::vector<WoundEvent> ev;
		wd.UpdateOnline(b, 119.f, {}, ev);
		CHECK(wd.SquadYielded(g.id) && Count(ev, WoundEvent::Kind::Released) == 0, "still kneeling before surrender_seconds");
		CHECK(Count(ev, WoundEvent::Kind::SelfHeal) >= 1, "the plain wounded (not yielded) still self-heal");
		wd.UpdateOnline(b, 2.f, {}, ev);
		CHECK(Count(ev, WoundEvent::Kind::Released) == 1 && !wd.SquadYielded(g.id) && b.StandingCount(g) == 3, "released: everyone gets up");
		CHECK(g.trust == Trust::BackingOff && g.tollUntil >= b.now + 11.9, "and backs off for spare_hours");
	}

	// ---- 7. the player's choices and their standing
	std::printf("section 7: choices\n");
	{
		TestHost host;
		Board b(cfg, w, host);
		Wounds wd;
		wd.cfg = wc;
		b.wounds = &wd;
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		Standing st(&cfg);
		for (int i = 0; i < 6; ++i) b.squads.push_back(Squad(10 + i, i < 3 ? Faction::Wanderer : Faction::Bandit, w, 0, 3, 10));
		const double now = b.now;
		auto gw = [&](Faction f) { return st.state.goodwill[int(f)]; };
		// heal (a neutral wanderer)
		Group& a = b.squads[0];
		wd.GoDown(b, a, a.members[0], false);
		CHECK(!wd.Choose(b, st, a, a.members[1], WoundChoice::Heal, false, true, now), "a choice needs a down member");
		CHECK(wd.Choose(b, st, a, a.members[0], WoundChoice::Heal, false, true, now), "heal");
		CHECK(!wd.IsDown(a.members[0].id) && a.members[0].health == a.members[0].healthMax, "up with full health");
		CHECK(Near(gw(Faction::Wanderer), wc.woundHelp) && Near(a.memory, wc.woundHelpMemory) && a.helpedByPlayer, "+40 goodwill, +300 memory");
		bool spilled = false;
		for (int o = 0; o < kHumanFactionCount; ++o) if (o != int(Faction::Wanderer) && cfg.rel[int(Faction::Wanderer)][o] >= 400 && gw(Faction(o)) > 0.f) spilled = true;
		CHECK(spilled, "goodwill spills to allies");
		// heal a hostile squad's wounded: it owes you
		Group& h = b.squads[3];
		wd.GoDown(b, h, h.members[0], false);
		wd.Choose(b, st, h, h.members[0], WoundChoice::Heal, true, true, now);
		CHECK(h.tollUntil >= now + sc.tollPassHours - 0.01, "a hostile squad that is helped stands down");
		// rob: a neutral costs, a hostile does not
		Group& r1 = b.squads[1];
		wd.GoDown(b, r1, r1.members[0], false);
		const float w0 = gw(Faction::Wanderer);
		CHECK(wd.Choose(b, st, r1, r1.members[0], WoundChoice::Rob, false, true, now) && r1.members[0].gold == 0 && wd.IsDown(r1.members[0].id), "rob: gold gone, still down");
		CHECK(Near(gw(Faction::Wanderer), w0 - wc.rob) && Near(r1.memory, -wc.robMemory), "-15 goodwill, -200 memory");
		Group& r2 = b.squads[4];
		wd.GoDown(b, r2, r2.members[0], false);
		const float b0 = gw(Faction::Bandit), m0 = r2.memory;
		wd.Choose(b, st, r2, r2.members[0], WoundChoice::Rob, true, true, now);
		CHECK(Near(gw(Faction::Bandit), b0) && Near(r2.memory, m0), "looting someone who attacked you costs nothing");
		// finish a neutral's wounded
		const float w1 = gw(Faction::Wanderer);
		const float noto0 = st.state.notoriety;
		CHECK(wd.Choose(b, st, r1, r1.members[0], WoundChoice::Finish, false, true, now) && !r1.members[0].alive() && !wd.IsDown(r1.members[0].id), "finish: dead");
		const float killSev = sc.killBase + sc.killPerLevel * 10.f;
		CHECK(gw(Faction::Wanderer) <= w1 - killSev - wc.finishDeed + 0.5f, "the kill plus -30");
		CHECK(st.state.notoriety > noto0, "a murder for decent folk");
		// a surrendered bandit squad: no mercy for one, spare the rest
		Group& s = b.squads[5];
		CHECK(wd.CheckSurrender(b, s, 9, b.rng, 1.f), "the bandits surrender");
		CHECK(!wd.Choose(b, st, b.squads[2], b.squads[2].members[0], WoundChoice::Spare, false, true, now), "spare needs a down member");
		const float noto1 = st.state.notoriety;
		const float b1 = gw(Faction::Bandit);
		wd.Choose(b, st, s, s.members[0], WoundChoice::NoMercy, true, true, now);
		// a provoked kill of an outlaw lowers notoriety a little (sev x 0.1); the prisoner's notoriety comes on top
		CHECK(!s.members[0].alive() && st.state.notoriety >= noto1 + wc.prisonerNotoriety - 0.1f * killSev - 0.01f && st.state.notoriety > noto1, "killing a prisoner: notoriety even though they were hostile");
		CHECK(gw(Faction::Bandit) < b1 - wc.prisoner + 0.5f, "and -60 with their faction");
		const float b2 = gw(Faction::Bandit), mem2 = s.memory;
		const float hp1 = s.members[1].health;
		CHECK(wd.Choose(b, st, s, s.members[1], WoundChoice::Spare, true, true, now), "spare");
		CHECK(!wd.SquadYielded(s.id) && b.StandingCount(s) == 2 && s.members[1].health == hp1, "everyone yielded gets up with the health they had");
		CHECK(Near(gw(Faction::Bandit), b2 + wc.spare) && Near(s.memory, mem2 + wc.spareMemory) && s.letGoCount == 1, "+20 goodwill, +250 memory");
		CHECK(s.tollUntil >= now + wc.spareHours - 0.01 && s.trust == Trust::BackingOff, "the spared squad backs off");
		// rob and go
		Group& s2 = b.squads[2];
		CHECK(wd.CheckSurrender(b, s2, 9, b.rng, 1.f), "the wanderers surrender");
		const float w2 = gw(Faction::Wanderer);
		CHECK(wd.Choose(b, st, s2, s2.members[0], WoundChoice::RobAndGo, false, true, now), "empty your pockets, then go");
		CHECK(s2.members[0].gold == 0 && s2.members[1].gold == 0 && s2.members[2].gold == 0 && b.StandingCount(s2) == 3, "everyone's gold, then they get up");
		CHECK(Near(gw(Faction::Wanderer), w2 - wc.rob), "rob once per squad");
		CHECK(std::string(WoundChoiceId(WoundChoice::RobAndGo)) == "robgo" && std::string(WoundChoiceId(WoundChoice::NoMercy)) == "nomercy", "choice ids for the debug prompts");
	}

	// ---- 8. a yielded squad that went off screen was left alone: it gets up, no standing change
	std::printf("section 8: off screen\n");
	{
		TestHost host;
		Board b(cfg, w, host);
		Wounds wd;
		wd.cfg = wc;
		b.wounds = &wd;
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		b.squads.push_back(Squad(1, Faction::Bandit, w, 0, 3, 10));
		Group& g = b.squads[0];
		g.online = true;
		g.members[2].health = 0.f;
		wd.GoDown(b, g, g.members[1], false);
		CHECK(wd.CheckSurrender(b, g, 9, b.rng, 1.f), "surrendered on screen");
		g.online = false;
		std::vector<WoundEvent> ev;
		wd.Tick(b, 0.5, &ev);
		CHECK(Count(ev, WoundEvent::Kind::Released) == 2 && wd.down.empty() && !wd.SquadYielded(g.id), "off screen: released");
		// an on-screen squad left with nobody standing becomes survivors once it is off screen
		g.online = true;
		wd.GoDown(b, g, g.members[0], false);
		wd.GoDown(b, g, g.members[1], false);
		g.online = false;
		ev.clear();
		wd.Tick(b, 0.5, &ev);
		CHECK(Count(ev, WoundEvent::Kind::Survivors) == 1 && wd.Survivors(g) && wd.Find(g.members[0].id)->upAt > b.now && wd.Find(g.members[0].id)->Offline(), "down on screen, nobody standing: survivors off screen");
	}

	// ---- 9. co-save WOND, and old saves without it
	std::printf("section 9: co-save\n");
	{
		TestHost host;
		Board b(cfg, w, host);
		Wounds wd;
		wd.cfg = wc;
		b.wounds = &wd;
		b.Reset(100.0);
		b.nextId = 1ULL << 40;   // board-made squads never share an id with the hand-made test squads
		b.squads.push_back(Squad(1, Faction::Bandit, w, 0, 3, 10));
		b.squads.push_back(Squad(2, Faction::Imperial, w, 0, 3, 10));
		Group& g1 = b.squads[0];
		Group& g2 = b.squads[1];
		g1.members[0].health = 7.f;
		wd.GoDown(b, g1, g1.members[0], true);
		wd.down[g1.members[0].id].upAt = 105.5;
		wd.CheckSurrender(b, g2, 9, b.rng, 0.f);   // rolled, did not yield
		wd.GoDown(b, g2, g2.members[1], false);
		wd.down[g2.members[1].id].flags |= Down::kYielded;
		wd.down[g2.members[1].id].healthBefore = 66.f;
		std::vector<std::uint8_t> blob;
		wd.Save(blob);
		Wounds back;
		CHECK(back.Load(blob.data(), blob.size(), Wounds::kSaveVersion), "WOND v1 loads");
		CHECK(back.down.size() == 2 && back.fights.size() == 1 && back.fights[g2.id].rolled && !back.fights[g2.id].yielded, "records and fight rolls survive");
		const Down* d1 = back.Find(g1.members[0].id);
		const Down* d2 = back.Find(g2.members[1].id);
		CHECK(d1 && d1->squad == g1.id && d1->upAt == 105.5 && d1->since == 100.0 && d1->Offline() && !d1->Yielded(), "an offline survivor: squad, hours, flags");
		CHECK(d2 && d2->Yielded() && !d2->Offline() && d2->healthBefore == 66.f, "a yielded member: flags, health before yielding");
		std::vector<std::uint8_t> again;
		back.Save(again);
		CHECK(again.size() == blob.size(), "round trip keeps the size");
		// after a load: records of members that no longer exist go
		g1.members[0].health = 0.f;
		back.Rebind(b);
		CHECK(back.down.size() == 1 && back.IsDown(g2.members[1].id), "Rebind drops the dead");
		b.squads.pop_back();
		back.Rebind(b);
		CHECK(back.down.empty() && back.fights.empty(), "Rebind drops records of squads that are gone");
		// unknown version, broken blob: nobody down
		Wounds v2;
		CHECK(!v2.Load(blob.data(), blob.size(), 2) && v2.down.empty(), "an unknown WOND version is skipped");
		Wounds cut;
		CHECK(!cut.Load(blob.data(), blob.size() - 3, Wounds::kSaveVersion) && cut.down.empty() && cut.fights.empty(), "a truncated blob leaves nobody down");
		Wounds empty;
		CHECK(empty.Load(nullptr, 0, Wounds::kSaveVersion) == false && empty.down.empty(), "no data: nobody down");
		// old saves: SIM2 208 / 207 / 206 / 205 still load; they have no WOND, so nobody is down and everyone alive stands
		CHECK(SaveFormat::kVer == 208 && SaveFormat::Loads(208) && SaveFormat::Loads(207) && SaveFormat::Loads(206) && SaveFormat::Loads(205) && !SaveFormat::Loads(204),
		      "SIM2 layouts 205-208 load (WOND is a record of its own, SIM2 unchanged)");
		Wounds fresh;   // what a load without WOND leaves (the plugin resets before reading records)
		b.wounds = &fresh;
		CHECK(b.StandingCount(b.squads[0]) == 2 && fresh.down.empty(), "an old save: nobody down, the living stand");
	}

	// ---- 10. a small busy world for a month: the invariants hold
	std::printf("section 10: soak\n");
	{
		WoundConfig c = wc;
		c.ocsChance = 0.6f;
		const SoakResult r = Soak(cfg, w, c, 30, 1);
		PrintSoak("wounded.ltx (ocs 0.6)", r);
		CHECK(r.records, "every record points at a living member of an existing squad (after each step)");
		CHECK(r.health, "health stays within [0, max]; the down keep at least 1");
		CHECK(r.c.ocsDowned > 0 && r.c.finished > 0 && r.c.helped > 0 && r.c.survivors > 0, "down, finish, help and survivors all happen");
		CHECK(r.longestSurvivors <= wc.downHoursMax + 0.51, "no squad lies wounded longer than down_hours");
	}

	// ---- 11. difficulty (T21): the chances at 0 and 1, every channel at 0.5x and 2x; nothing breaks or leaves its range
	std::printf("section 11: difficulty extremes\n");
	{
		CHECK(InRange(wc), "wounded.ltx as loaded is in range");
		// the one read path: scale x1 is the file, 0.5x / 2x move each tagged key the documented way, clamped
		const WoundConfig s1 = ScaleWoundConfig(wc, WoundScale{});
		CHECK(s1.ocsChance == wc.ocsChance && s1.finish[0] == wc.finish[0] && s1.woundHelp == wc.woundHelp && s1.selfHealSeconds == wc.selfHealSeconds, "scale 1 = wounded.ltx");
		const float ms[2] = { 0.5f, 2.f };
		bool allIn = true;
		for (float L : ms)
			for (float E : ms)
				for (float M : ms) {
					allIn = allIn && InRange(ScaleWoundConfig(wc, WoundScale{ L, E, M }));
					allIn = allIn && InRange(ScaleWoundConfig(AllChances(wc, 1.f), WoundScale{ L, E, M }));
					allIn = allIn && InRange(ScaleWoundConfig(AllChances(wc, 0.f), WoundScale{ L, E, M }));
				}
		CHECK(allIn, "every channel at 0.5x / 2x (with the chances at 0, as loaded and at 1): all in range");
		const WoundConfig hard = ScaleWoundConfig(wc, WoundScale{ 2.f, 2.f, 2.f });
		const WoundConfig easy = ScaleWoundConfig(wc, WoundScale{ 0.5f, 0.5f, 0.5f });
		CHECK(hard.ocsChance < wc.ocsChance && easy.ocsChance > wc.ocsChance && hard.chance < easy.chance, "lethality: fewer go down when harder");
		CHECK(hard.finish[int(Faction::Imperial)] > easy.finish[int(Faction::Imperial)] && hard.finish[int(Faction::Vampire)] == 1.f, "lethality: more get finished (clamped at 1)");
		CHECK(hard.selfHealSeconds > easy.selfHealSeconds && hard.downHoursMax > easy.downHoursMax && hard.reviveHp < easy.reviveHp, "lethality: slower and weaker recovery");
		CHECK(hard.yieldChance < easy.yieldChance && hard.surrender[int(Faction::Bandit)] < easy.surrender[int(Faction::Bandit)] && hard.surrender[int(Faction::Thalmor)] == 0.f,
		      "enemy strength: fewer give up (fanatics stay at 0)");
		CHECK(easy.losingShare < hard.losingShare && hard.losingShare <= 1.f && easy.outnumber >= 1.f, "enemy strength: losing share and outnumber ratio, clamped");
		CHECK(hard.woundHelp < easy.woundHelp && hard.prisoner > easy.prisoner && hard.rob > easy.rob, "economy: smaller rewards, bigger costs");
		const WoundConfig silly = ScaleWoundConfig(wc, WoundScale{ 0.f, -3.f, std::nanf("") });
		CHECK(InRange(silly), "zero, negative or NaN multipliers are clamped, not trusted");
		WoundConfig wild = wc;
		wild.hp = 7.f; wild.chance = -2.f; wild.ocsChance = std::nanf(""); wild.downHpMin = 0.9f; wild.downHpMax = 0.1f; wild.finish[3] = 5.f; wild.helperRankMin = 30; wild.helperRankMax = 5;
		ClampWoundConfig(wild);
		CHECK(InRange(wild), "a hand-edited config out of range is clamped");
		{
			Ltx odd;
			odd.LoadString("[wounded]\nchance = 3\nocs_chance = -1\ndown_hp = 0.5, 0.2\n[finish]\nbandit = 9\n[surrender_chance]\nwanderer = -4\n");
			WoundConfig oc;
			std::string e3;
			CHECK(LoadWoundConfig(odd, oc, e3) && InRange(oc) && oc.chance == 1.f && oc.ocsChance == 0.f && oc.downHpMin == 0.2f && oc.finish[0] == 1.f && oc.surrender[int(Faction::Wanderer)] == 0.f,
			      "out-of-range keys in wounded.ltx load clamped");
		}
		// the world at the extremes: no breakage, no stuck records, health in range
		struct Run { const char* label; WoundConfig c; };
		std::vector<Run> runs = {
			{ "all chances 0", AllChances(wc, 0.f) },
			{ "all chances 1", AllChances(wc, 1.f) },
			{ "all channels 0.5x", easy },
			{ "all channels 2x", hard },
			{ "chances 1, 2x", ScaleWoundConfig(AllChances(wc, 1.f), WoundScale{ 2.f, 2.f, 2.f }) },
			{ "chances 0, 0.5x", ScaleWoundConfig(AllChances(wc, 0.f), WoundScale{ 0.5f, 0.5f, 0.5f }) },
		};
		std::vector<SoakResult> res;
		for (auto& run : runs) {
			res.push_back(Soak(cfg, w, run.c, 15, 3));
			PrintSoak(run.label, res.back());
			char what[160];
			std::snprintf(what, sizeof what, "%s: records valid, health in range, survivors up in time", run.label);
			CHECK(res.back().records && res.back().health && res.back().longestSurvivors <= run.c.downHoursMax + 0.51, what);
			std::snprintf(what, sizeof what, "%s: the fights go on", run.label);
			CHECK(res.back().battles > 0, what);
		}
		CHECK(res[0].c.downed == 0 && res[0].c.survivors == 0, "chances 0: nobody goes down (the board as before M5d)");
		CHECK(res[1].c.ocsDowned > 0 && res[1].c.finished > 0, "chances 1: every lethal hit goes down, every beaten one is finished");
		CHECK(res[1].c.survivors == 0, "chances 1: finish 1 leaves no survivors");
		CHECK(res[1].deaths > 0 && res[1].deaths <= res[1].c.finished + res[1].c.ocsDowned, "chances 1: every death was a finish");
		CHECK(res[2].c.ocsDowned >= res[3].c.ocsDowned, "0.5x goes down at least as often as 2x");
	}

	// ---- 12. T21's Mercy channel (Config::Diff(DiffChannel::Mercy), 1.0 at Adept): chances x Mercy, finish / Mercy, 0 = nobody down
	std::printf("section 12: mercy\n");
	{
		auto mercy = [&](float m) { return ScaleWoundConfig(wc, WoundScale{ .mercy = m }); };
		const WoundConfig m1 = mercy(1.f);
		CHECK(m1.chance == wc.chance && m1.ocsChance == wc.ocsChance && m1.yieldChance == wc.yieldChance && m1.finish == wc.finish && m1.surrender == wc.surrender &&
		      m1.hp == wc.hp && m1.reviveHp == wc.reviveHp, "mercy 1.0: every value exactly as loaded");
		const WoundConfig m2 = mercy(2.f);
		CHECK(m2.chance == 1.f && Near(m2.ocsChance, 0.7f, 1e-5f) && Near(m2.yieldChance, 0.5f, 1e-5f), "mercy 2: chances doubled, clamped to 1");
		CHECK(Near(m2.surrender[int(Faction::Bandit)], 1.f, 1e-5f) && Near(m2.surrender[int(Faction::Imperial)], 0.5f, 1e-5f) && m2.surrender[int(Faction::Thalmor)] == 0.f,
		      "mercy 2: surrender doubled (fanatics stay at 0)");
		CHECK(Near(m2.finish[int(Faction::Bandit)], 0.4f, 1e-5f) && Near(m2.finish[int(Faction::Vampire)], 0.5f, 1e-5f), "mercy 2: finish halved");
		const WoundConfig mh = mercy(0.5f);
		CHECK(Near(mh.ocsChance, 0.175f, 1e-5f) && Near(mh.chance, 0.35f, 1e-5f) && mh.finish[int(Faction::Bandit)] == 1.f && Near(mh.finish[int(Faction::Caravan)], 0.2f, 1e-5f),
		      "mercy 0.5: chances halved, finish doubled (clamped to 1)");
		const WoundConfig m0 = mercy(0.f);
		bool none = m0.chance == 0.f && m0.ocsChance == 0.f && m0.yieldChance == 0.f;
		for (int f = 0; f < kHumanFactionCount; ++f) none = none && m0.surrender[f] == 0.f && m0.finish[f] == 1.f;
		CHECK(none, "mercy 0: nobody goes down, yields or surrenders");
		CHECK(mercy(-2.f).ocsChance == 0.f && mercy(std::nanf("")).ocsChance == wc.ocsChance && InRange(mercy(50.f)) && InRange(mercy(-1.f)), "mercy below 0 is 0, NaN is 1, huge is clamped");
		// at 1.0 no extra random draws: the same seed gives the same world, step for step
		const SoakResult base = Soak(cfg, w, wc, 10, 9), same = Soak(cfg, w, m1, 10, 9);
		CHECK(base.battles == same.battles && base.deaths == same.deaths && base.c.downed == same.c.downed && base.c.finished == same.c.finished &&
		      base.c.helped == same.c.helped && base.c.survivors == same.c.survivors, "mercy 1.0: the run is identical to the unscaled one");
		const SoakResult r0 = Soak(cfg, w, m0, 15, 3), rh = Soak(cfg, w, mh, 15, 3), r2 = Soak(cfg, w, m2, 15, 3);
		PrintSoak("mercy 0", r0);
		PrintSoak("mercy 0.5", rh);
		PrintSoak("mercy 2", r2);
		CHECK(r0.c.downed == 0 && r0.c.survivors == 0 && r0.records && r0.health, "mercy 0 world: nobody down, nothing broken");
		CHECK(rh.records && rh.health && rh.longestSurvivors <= mh.downHoursMax + 0.51, "mercy 0.5 world: records valid, health in range");
		CHECK(r2.records && r2.health && r2.longestSurvivors <= m2.downHoursMax + 0.51, "mercy 2 world: records valid, health in range");
	}


	// ---- 13. the plugin's order (W2 play-test, Oct 18: "[wound] NOT ready (no board)"): wounded.ltx is loaded when the board
	// exists but is still empty; the hook must come once the board is filled, once, and stay through a reset
	std::printf("section 13: hook order\n");
	{
		TestHost host;
		Board b(cfg, w, host);
		Wounds wd;
		wd.cfg = wc;
		wd.cfg.ocsChance = 1.f;
		wd.cfg.finish.fill(0.f);
		CHECK(!wd.Attach(b, false) && b.wounds == nullptr && !wd.AttachedTo(b), "Init before the fill: not attached yet");
		b.Reset(100.0);
		b.nextId = 1ULL << 40;
		b.squads.push_back(Squad(1, Faction::Imperial, w, 0, 5, 30));   // the fill
		b.squads.push_back(Squad(2, Faction::Bandit, w, 0, 3, 1));
		b.pinned = { 1, 2 };
		CHECK(wd.Attach(b, true) && b.wounds == &wd && wd.AttachedTo(b), "after the fill: attached");
		CHECK(!wd.Attach(b, true) && b.wounds == &wd, "every later tick: no second attach");
		for (int i = 0; i < 12 && wd.counters.ocsDowned == 0; ++i) b.Step(0.5);
		CHECK(wd.counters.ocsDowned > 0, "the hook runs: members go down in offline fights");
		b.Reset(200.0);   // a new game / a load keeps the same board and its hook
		CHECK(wd.AttachedTo(b) && !wd.Attach(b, true), "a board reset keeps the hook (no re-attach needed)");
		Wounds off;
		off.cfg = wc;
		off.cfg.enabled = false;
		Board b2(cfg, w, host);
		CHECK(!off.Attach(b2, true) && b2.wounds == nullptr, "disabled in wounded.ltx: never attached");
	}

	std::printf("\nwounds_test: %d passed, %d failed\n", gPass, gFails);
	return gFails ? 1 : 0;
}
