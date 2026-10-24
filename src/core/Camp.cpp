#include "ALife/Camp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "ALife/Config.h"

namespace ALife {
namespace {
	constexpr const char* kJobIds[kCampJobs] = { "none", "camper", "sniper", "guard", "patrol", "collector", "walker", "campfire", "sleep" };

	std::string CampTrim(std::string s)
	{
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
		std::size_t i = 0;
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
		return s.substr(i);
	}

	// one row of slot counts per job from a [slots_*] section
	void ReadSlots(const Ltx& x, const std::string& sec, std::array<float, kCampJobs>& out)
	{
		for (int j = 1; j < kCampJobs; ++j) out[j] = std::max(0.f, x.GetF(sec, kJobIds[j], out[j]));
	}
}

const char* CampJobId(CampJob a_j) { return int(a_j) < kCampJobs ? kJobIds[int(a_j)] : "?"; }
bool CampJobFromId(const std::string& a_id, CampJob& a_out)
{
	for (int j = 1; j < kCampJobs; ++j)
		if (a_id == kJobIds[j]) { a_out = CampJob(j); return true; }
	return false;
}

CampConfig::CampConfig()
{
	// gulag_general.script job_info_by_job_type_id (docs/CAMPLIFE.md 1.1)
	prior[int(CampJob::Camper)] = 45;
	prior[int(CampJob::Sniper)] = 30;
	prior[int(CampJob::Guard)] = 25;
	prior[int(CampJob::Patrol)] = 25;
	prior[int(CampJob::Collector)] = 25;
	prior[int(CampJob::Walker)] = 15;
	prior[int(CampJob::Campfire)] = 10;
	prior[int(CampJob::Sleep)] = 10;
	defaultSlots[int(CampJob::Guard)] = 0.5f;
	defaultSlots[int(CampJob::Patrol)] = 0.5f;
	defaultSlots[int(CampJob::Campfire)] = 1.f;
	defaultSlots[int(CampJob::Sleep)] = 1.f;
}

bool LoadCampConfig(const Ltx& x, const Ltx* a_spots, CampConfig& c, std::string& err)
{
	c = CampConfig{};
	if (!x.Has("jobs")) { err = "camp.ltx: no [jobs] section"; return false; }
	for (int j = 1; j < kCampJobs; ++j) c.prior[j] = x.GetI("jobs", kJobIds[j], c.prior[j]);
	ReadSlots(x, "slots_default", c.defaultSlots);
	for (int k = 0; k < int(Kind::COUNT); ++k) {
		const std::string sec = std::string("slots_") + KindName(Kind(k));
		c.hasSlots[k] = x.Has(sec);
		c.slots[k] = c.defaultSlots;
		if (c.hasSlots[k]) {
			for (auto& v : c.slots[k]) v = 0.f;   // a kind's own table lists every job it has
			ReadSlots(x, sec, c.slots[k]);
		}
	}
	c.sleepFrom = x.GetF("time", "sleep_from", c.sleepFrom);
	c.sleepTo = x.GetF("time", "sleep_to", c.sleepTo);
	c.nightSleepPrior = x.GetI("time", "night_sleep_prior", c.nightSleepPrior);
	c.watchShare = std::clamp(x.GetF("time", "watch_share", c.watchShare), 0.f, 1.f);
	c.idleMin = x.GetF("scene", "idle_min", c.idleMin);
	c.idleMax = std::max(c.idleMin, x.GetF("scene", "idle_max", c.idleMax));
	c.storyChance = std::clamp(x.GetF("scene", "story_chance", c.storyChance), 0.f, 1.f);
	c.chatterChance = std::clamp(x.GetF("scene", "chatter_chance", c.chatterChance), 0.f, 1.f);
	c.restRate = std::max(0.f, x.GetF("rest", "rest_rate", c.restRate));
	c.scanRadius = std::max(0.f, x.GetF("budget", "scan_radius", c.scanRadius));
	c.reassignSeconds = std::max(1.f, x.GetF("budget", "reassign_seconds", c.reassignSeconds));
	c.maxPackageChanges = std::clamp(x.GetI("budget", "max_package_changes", c.maxPackageChanges), 1, 15);
	for (int f = 0; f < kHumanFactionCount; ++f) {
		const std::string sec = std::string("stories_") + FactionId(Faction(f));
		const auto* lines = x.Section(sec);
		if (!lines) continue;
		for (auto& [k, v] : *lines) {
			std::vector<std::string> story;
			std::size_t p = 0;
			while (p <= v.size()) {
				const auto q = v.find('|', p);
				const std::string part = CampTrim(v.substr(p, q == std::string::npos ? std::string::npos : q - p));
				if (!part.empty()) story.push_back(part);
				if (q == std::string::npos) break;
				p = q + 1;
			}
			if (!story.empty()) c.stories[f].push_back(std::move(story));
		}
	}
	if (a_spots)
		if (const auto* lines = a_spots->Section("spots"))
			for (auto& [k, v] : *lines) {
				auto& ids = c.spots[k];
				for (auto& id : a_spots->List("spots", k))
					if (const FormID f = FormID(std::strtoul(id.c_str(), nullptr, 16))) ids.push_back(f);
			}
	return true;
}

std::vector<CampSlot> SlotsFor(const GraphNode& n, const CampConfig& c, float strength)
{
	const auto& row = c.hasSlots[int(n.kind)] ? c.slots[int(n.kind)] : c.defaultSlots;
	const float pop = float(std::max(1, n.maxPopulation));
	std::vector<CampSlot> out;
	for (int j = 1; j < kCampJobs; ++j) {
		if (CampJob(j) == CampJob::Walker) continue;   // the unlimited fallback
		float want = row[j] * pop;
		const CampJob job = CampJob(j);
		if (job == CampJob::Guard || job == CampJob::Sniper || job == CampJob::Camper) want *= std::max(0.f, strength);   // harder camps keep watch
		const int count = int(std::lround(want));
		for (int i = 0; i < count; ++i) out.push_back({ job, i });
	}
	std::stable_sort(out.begin(), out.end(), [&](const CampSlot& a, const CampSlot& b) { return c.prior[int(a.job)] > c.prior[int(b.job)]; });
	return out;
}

bool InSleepWindow(const CampConfig& c, float h)
{
	h = std::fmod(std::fmod(h, 24.f) + 24.f, 24.f);
	return c.sleepFrom <= c.sleepTo ? (h >= c.sleepFrom && h < c.sleepTo) : (h >= c.sleepFrom || h < c.sleepTo);
}

std::vector<CampAssign> AssignJobs(const GraphNode& n, const std::vector<const Group*>& squads, float hour, const Config& cfg, const CampConfig& c,
	const std::vector<CampAssign>* prev)
{
	const float strength = cfg.Diff(DiffChannel::Strength);
	const bool night = InSleepWindow(c, hour);
	const auto slots = SlotsFor(n, c, strength);
	auto prior = [&](CampJob j) { return j == CampJob::Sleep && night ? c.nightSleepPrior : c.prior[int(j)]; };

	struct Who { const Group* g; const Member* m; bool leader; };
	std::vector<Who> who;
	for (const Group* g : squads) {
		if (!g || cfg.IsMonster(g->faction)) continue;   // monsters keep their lair sandbox (Anomaly monster_jobs)
		bool first = true;
		for (auto& m : g->members) {
			if (!m.alive()) continue;
			who.push_back({ g, &m, first && m.role == Role::Leader });
			first = false;
		}
	}
	std::stable_sort(who.begin(), who.end(), [](const Who& a, const Who& b) { return a.leader > b.leader; });   // leaders choose first
	const int living = int(who.size());
	const int watchCap = night ? std::clamp(int(std::ceil(float(living) * c.watchShare * strength)), living ? 1 : 0, living) : living;

	auto allowed = [&](const Who& w, CampJob j) {
		const Faction f = w.g->faction;
		switch (j) {
		case CampJob::Sleep: return night;
		case CampJob::Sniper: return w.m->role == Role::Archer;
		case CampJob::Camper: return w.m->role != Role::Archer && int(f) < kFactionCount && cfg.standing.opportunism[int(f)] > 0.f;   // archers go up, not into the brush
		case CampJob::Collector: return cfg.doesHunt[int(f)] || f == Faction::Mage;
		default: return true;
		}
	};

	std::vector<int> owner(slots.size(), -1);   // slot -> index in who
	std::vector<int> mine(who.size(), -1);      // who -> slot (-1: walker)
	int watching = 0;
	auto take = [&](int wi, int si) {
		owner[std::size_t(si)] = wi;
		mine[std::size_t(wi)] = si;
		if (IsWatchJob(slots[std::size_t(si)].job)) ++watching;
	};
	auto drop = [&](int wi) {
		const int si = mine[std::size_t(wi)];
		if (si < 0) return;
		if (IsWatchJob(slots[std::size_t(si)].job)) --watching;
		owner[std::size_t(si)] = -1;
		mine[std::size_t(wi)] = -1;
	};
	auto best = [&](int wi, int above) {   // highest priority free slot the member may take, strictly above a_above
		int pick = -1;
		for (std::size_t si = 0; si < slots.size(); ++si) {
			if (owner[si] >= 0) continue;
			const CampJob j = slots[si].job;
			const int p = prior(j);
			if (p <= above || !allowed(who[std::size_t(wi)], j)) continue;
			if (IsWatchJob(j) && watching >= watchCap) continue;
			if (pick < 0 || p > prior(slots[std::size_t(pick)].job)) pick = int(si);
		}
		return pick;
	};

	// 1. keep what still holds (Anomaly: a job stays while job_avail_to_npc)
	if (prev)
		for (auto& a : *prev) {
			for (int wi = 0; wi < living; ++wi) {
				if (who[std::size_t(wi)].m->id != a.member || mine[std::size_t(wi)] >= 0) continue;
				for (std::size_t si = 0; si < slots.size(); ++si)
					if (owner[si] < 0 && slots[si].job == a.job && slots[si].index == a.index && allowed(who[std::size_t(wi)], a.job) &&
						(!IsWatchJob(a.job) || watching < watchCap)) {
						take(wi, int(si));
						break;
					}
			}
		}
	// 2. leaders: patrol first (it drives the squad's walk), else guard
	for (int wi = 0; wi < living; ++wi) {
		if (!who[std::size_t(wi)].leader || mine[std::size_t(wi)] >= 0) continue;
		for (CampJob want : { CampJob::Patrol, CampJob::Guard }) {
			if (mine[std::size_t(wi)] >= 0 || (IsWatchJob(want) && watching >= watchCap)) break;
			for (std::size_t si = 0; si < slots.size(); ++si)
				if (owner[si] < 0 && slots[si].job == want) { take(wi, int(si)); break; }
		}
	}
	// 3. everyone: a free slot of higher priority replaces the current one; free members take the best one
	for (int wi = 0; wi < living; ++wi) {
		const int cur = mine[std::size_t(wi)];
		const int curPrior = cur >= 0 ? prior(slots[std::size_t(cur)].job) : -1;
		if (cur >= 0 && who[std::size_t(wi)].leader) continue;   // a leader keeps its patrol / guard
		if (cur >= 0) drop(wi);
		const int si = best(wi, curPrior);
		if (si >= 0) take(wi, si);
		else if (cur >= 0) take(wi, cur);
	}

	std::vector<CampAssign> out;
	out.reserve(who.size());
	for (int wi = 0; wi < living; ++wi) {
		CampAssign a;
		a.squad = who[std::size_t(wi)].g->id;
		a.member = who[std::size_t(wi)].m->id;
		const int si = mine[std::size_t(wi)];
		if (si >= 0) { a.job = slots[std::size_t(si)].job; a.index = slots[std::size_t(si)].index; }
		else { a.job = CampJob::Walker; a.index = -1; }
		out.push_back(a);
	}
	return out;
}

}  // namespace ALife
