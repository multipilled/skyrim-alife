// Headless tests of camp life (docs/CAMPLIFE.md 4.1, M5h): camp.ltx / camp_spots.ltx, job slots per place, the Anomaly job choice.
//   camp_test.exe [config dir]
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

#include "ALife/Camp.h"
#include "ALife/Config.h"
#include "ALife/Ltx.h"

using namespace ALife;

namespace {
	int gPass = 0, gFails = 0;
#define CHECK(cond, msg) do { if (cond) { ++gPass; } else { ++gFails; std::printf("FAIL: %s   [%s]\n", msg, #cond); } } while (0)

	std::uint64_t gId = 100;
	Group Squad(std::uint64_t id, Faction f, std::vector<Role> roles)
	{
		Group g;
		g.id = id; g.faction = f;
		for (auto r : roles) {
			Member m;
			m.id = ++gId; m.faction = f; m.role = r; m.level = 10; m.health = m.healthMax = 100.f;
			g.members.push_back(m);
		}
		return g;
	}
	GraphNode Place(Kind k, int pop)
	{
		GraphNode n;
		n.kind = k; n.maxPopulation = pop; n.name = KindName(k);
		return n;
	}
	std::map<CampJob, int> Count(const std::vector<CampAssign>& a)
	{
		std::map<CampJob, int> c;
		for (auto& x : a) c[x.job]++;
		return c;
	}
	const CampAssign* Of(const std::vector<CampAssign>& a, std::uint64_t member)
	{
		for (auto& x : a) if (x.member == member) return &x;
		return nullptr;
	}
	bool UniqueSlots(const std::vector<CampAssign>& a)
	{
		std::map<std::pair<int, int>, int> seen;
		for (auto& x : a) if (x.index >= 0 && seen[{ int(x.job), x.index }]++) return false;
		return true;
	}
}

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : "config";
	Config cfg;
	std::string err;
	if (!LoadConfig(dir, cfg, err)) { std::printf("cannot load config: %s\n", err.c_str()); return 2; }
	Ltx cl, sl;
	const bool okCamp = cl.LoadFile(dir + "/camp.ltx", &err);
	const bool okSpots = sl.LoadFile(dir + "/camp_spots.ltx", &err);
	CampConfig cc;
	const bool loaded = okCamp && LoadCampConfig(cl, okSpots ? &sl : nullptr, cc, err);

	// ---- 1. config
	CHECK(loaded, "camp.ltx parses");
	CHECK(cc.prior[int(CampJob::Camper)] == 45 && cc.prior[int(CampJob::Sniper)] == 30 && cc.prior[int(CampJob::Guard)] == 25 &&
			  cc.prior[int(CampJob::Campfire)] == 10 && cc.prior[int(CampJob::Sleep)] == 10,
		"[jobs] Anomaly priorities");
	CHECK(cc.hasSlots[int(Kind::BanditCamp)] && cc.slots[int(Kind::BanditCamp)][int(CampJob::Camper)] > 0.f, "bandit camps have a camper slot table");
	CHECK(okSpots && cc.spots["campfire"].size() >= 10 && cc.spots["bed"].size() >= 50 && cc.spots["seat"].size() >= 50, "camp_spots.ltx: campfires, beds, seats");
	CHECK(!cc.stories[int(Faction::Bandit)].empty() && cc.stories[int(Faction::Bandit)][0].size() == 3, "bandit stories: three lines each");
	{
		bool every = true;
		for (int k = 0; k < int(Kind::COUNT); ++k) {
			const auto s = SlotsFor(Place(Kind(k), 2), cc, 1.f);
			if (s.empty()) every = false;
		}
		CHECK(every, "every place kind has slots (its own table or the default)");
	}
	CHECK(InSleepWindow(cc, 22.f) && InSleepWindow(cc, 3.f) && !InSleepWindow(cc, 7.f) && !InSleepWindow(cc, 12.f) && InSleepWindow(cc, 21.f),
		"sleep window 21:00-07:00");

	// ---- 2. priority order: a bandit squad of 5 at a camp (max population 2) at noon
	const GraphNode camp = Place(Kind::BanditCamp, 2);
	Group bandits = Squad(1, Faction::Bandit, { Role::Leader, Role::Archer, Role::Melee, Role::Melee, Role::Heavy });
	{
		const auto a = AssignJobs(camp, { &bandits }, 12.f, cfg, cc);
		auto c = Count(a);
		CHECK(a.size() == 5, "every living member gets a job");
		CHECK(Of(a, bandits.members[0].id)->job == CampJob::Patrol, "the leader takes patrol");
		CHECK(Of(a, bandits.members[1].id)->job == CampJob::Sniper, "the archer takes sniper");
		CHECK(c[CampJob::Camper] == 1 && c[CampJob::Guard] == 1, "camper and guard before the fire");
		CHECK(c[CampJob::Campfire] == 1, "the rest sit at the fire");
		CHECK(c[CampJob::Sleep] == 0, "nobody sleeps at noon");
		CHECK(UniqueSlots(a), "one member per slot");
	}

	// ---- 3. two squads share the slots; the overflow walks
	{
		Group more = Squad(2, Faction::Bandit, { Role::Leader, Role::Melee, Role::Melee, Role::Melee, Role::Melee, Role::Melee });
		const auto a = AssignJobs(camp, { &bandits, &more }, 12.f, cfg, cc);
		CHECK(a.size() == 11 && UniqueSlots(a), "two squads never share a slot");
		CHECK(Count(a)[CampJob::Walker] >= 1, "members without a free slot walk the place");
	}

	// ---- 4. night: sleepers in beds, a share stays on watch
	{
		const auto a = AssignJobs(camp, { &bandits }, 23.f, cfg, cc);
		auto c = Count(a);
		const int watch = c[CampJob::Camper] + c[CampJob::Sniper] + c[CampJob::Guard] + c[CampJob::Patrol];
		CHECK(watch == 2, "night: ceil(5 x 0.25) = 2 on watch");
		CHECK(c[CampJob::Sleep] == 2, "night: the sleep slots fill before the fire (night_sleep_prior)");
		CHECK(UniqueSlots(a), "night: one member per slot");
	}

	// ---- 5. monsters get nothing; lawful factions never camp in ambush; collectors only for hunters / mages
	{
		Group wolves = Squad(3, Faction::Wildlife, { Role::Leader, Role::Melee, Role::Melee });
		CHECK(AssignJobs(camp, { &wolves }, 12.f, cfg, cc).empty(), "monster squads take no jobs");
		Group legion = Squad(4, Faction::Imperial, { Role::Leader, Role::Melee, Role::Melee, Role::Archer });
		const auto a = AssignJobs(camp, { &legion }, 12.f, cfg, cc);
		CHECK(Count(a)[CampJob::Camper] == 0, "the Legion never lies in wait (not an outlaw faction)");
		const GraphNode wild = Place(Kind::Wilderness, 2);
		Group hunters = Squad(5, Faction::Hunter, { Role::Leader, Role::Archer, Role::Archer });
		Group soldiers = Squad(6, Faction::Imperial, { Role::Leader, Role::Melee, Role::Melee });
		CHECK(Count(AssignJobs(wild, { &hunters }, 12.f, cfg, cc))[CampJob::Collector] == 1, "hunters gather in the wild");
		CHECK(Count(AssignJobs(wild, { &soldiers }, 12.f, cfg, cc))[CampJob::Collector] == 0, "soldiers do not");
	}

	// ---- 6. re-assignment: jobs held stay; a death frees a slot that the next member takes
	{
		auto a = AssignJobs(camp, { &bandits }, 12.f, cfg, cc);
		auto again = AssignJobs(camp, { &bandits }, 12.f, cfg, cc, &a);
		bool same = again.size() == a.size();
		for (auto& x : a) { const auto* y = Of(again, x.member); same = same && y && y->job == x.job && y->index == x.index; }
		CHECK(same, "a re-check with nothing changed keeps every job");
		Group hurt = bandits;
		const auto camper = std::find_if(a.begin(), a.end(), [](const CampAssign& x) { return x.job == CampJob::Camper; });
		for (auto& m : hurt.members) if (camper != a.end() && m.id == camper->member) m.health = 0.f;
		const auto b = AssignJobs(camp, { &hurt }, 12.f, cfg, cc, &a);
		CHECK(b.size() == 4 && Count(b)[CampJob::Camper] == 1, "the dead camper's slot is taken by a member from a lower job");
	}

	// ---- 7. difficulty: Diff(Strength) raises the watch, 1.0 is the table
	{
		auto watchSlots = [&](float s) {
			int n = 0;
			for (auto& x : SlotsFor(Place(Kind::Fort, 4), cc, s)) n += x.job == CampJob::Guard || x.job == CampJob::Sniper || x.job == CampJob::Camper;
			return n;
		};
		const int lo = watchSlots(0.7f), mid = watchSlots(1.f), hi = watchSlots(1.35f);
		CHECK(lo <= mid && mid <= hi && lo < hi, "guard + sniper + camper slots rise with squad strength");
		CHECK(mid == int(std::lround(cc.slots[int(Kind::Fort)][int(CampJob::Guard)] * 4.f)) + int(std::lround(cc.slots[int(Kind::Fort)][int(CampJob::Sniper)] * 4.f)),
			"strength 1.0 = the camp.ltx table");
		DifficultyChoice ch;
		ch.slider[int(DiffChannel::Strength)] = 2.f;
		cfg.ApplyDifficulty(5, ch);
		auto night = Count(AssignJobs(camp, { &bandits }, 23.f, cfg, cc));
		cfg.ApplyDifficulty(kAdept, DifficultyChoice{});
		CHECK(night[CampJob::Camper] + night[CampJob::Sniper] + night[CampJob::Guard] + night[CampJob::Patrol] > 2, "a harder game keeps more on watch at night");
	}

	// ---- 8. determinism and budget
	{
		Group more = Squad(7, Faction::Bandit, { Role::Leader, Role::Archer, Role::Melee, Role::Melee, Role::Melee });
		Group third = Squad(8, Faction::Mercenary, { Role::Leader, Role::Archer, Role::Melee, Role::Heavy, Role::Mage });
		const GraphNode fort = Place(Kind::Fort, 4);
		const auto a = AssignJobs(fort, { &bandits, &more, &third }, 22.f, cfg, cc);
		const auto b = AssignJobs(fort, { &bandits, &more, &third }, 22.f, cfg, cc);
		bool same = a.size() == b.size();
		for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i].member == b[i].member && a[i].job == b[i].job && a[i].index == b[i].index;
		CHECK(same, "same input, same assignment");
		ULONG64 c0 = 0, c1 = 0;
		QueryThreadCycleTime(GetCurrentThread(), &c0);
		const auto t0 = GetTickCount64();
		std::size_t sink = 0;
		for (int i = 0; i < 10000; ++i) sink += AssignJobs(fort, { &bandits, &more, &third }, float(i % 24), cfg, cc, &a).size();
		QueryThreadCycleTime(GetCurrentThread(), &c1);
		const double ms = double(GetTickCount64() - t0);
		std::printf("budget: 10000 assignments of 15 members at a fort: %.0f ms wall, %.1f Mcycles (sink %zu)\n", ms, double(c1 - c0) / 1e6, sink);
		CHECK(double(c1 - c0) / 1e6 < 300.0, "10000 assignments of 15 members under 300 Mcycles (~0.01 ms each)");
	}

	std::printf("camp_test: %d passed, %d failed\n", gPass, gFails);
	return gFails ? 1 : 0;
}
