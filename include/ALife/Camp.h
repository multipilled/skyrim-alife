#pragma once
// Camp life at smart terrains (docs/CAMPLIFE.md, M5h): job slots per place and the Anomaly-style job choice (gulag_general /
// smart_terrain select_npc_job). Dependency-free: the plugin's job director and tests/camp_test.cpp run the same rules.
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ALife/Core.h"
#include "ALife/Ltx.h"

namespace ALife {

struct Config;

// Job kinds in Anomaly's priority order (camp.ltx [jobs] holds the priorities). Walker is the fallback and has no slot limit.
enum class CampJob : std::uint8_t { None = 0, Camper, Sniper, Guard, Patrol, Collector, Walker, Campfire, Sleep, COUNT };
inline constexpr int kCampJobs = int(CampJob::COUNT);
const char* CampJobId(CampJob a_j);                        // "camper" ... (camp.ltx keys)
bool CampJobFromId(const std::string& a_id, CampJob& a_out);
inline bool IsWatchJob(CampJob a_j) { return a_j == CampJob::Camper || a_j == CampJob::Sniper || a_j == CampJob::Guard || a_j == CampJob::Patrol; }

// camp.ltx (+ camp_spots.ltx)
struct CampConfig
{
	std::array<int, kCampJobs> prior{};                                   // [jobs]
	std::array<std::array<float, kCampJobs>, int(Kind::COUNT)> slots{};   // [slots_<kind>] per max_population
	std::array<bool, int(Kind::COUNT)> hasSlots{};
	std::array<float, kCampJobs> defaultSlots{};                          // [slots_default]
	float sleepFrom{ 21.f }, sleepTo{ 7.f };                              // [time] Anomaly precond_safe_job_sleeper
	int nightSleepPrior{ 20 };                                            // the sleep priority inside its window (the header value of gulag_general)
	float watchShare{ 0.25f };                                            // share of the living members that stays up at night (x Diff(Strength))
	float idleMin{ 25.f }, idleMax{ 120.f }, storyChance{ 0.5f }, chatterChance{ 0.3f };   // [scene] sr_camp (seconds)
	float restRate{ 1.f };                                                // [rest] offline overnight recovery at a camp (x the inn rate)
	float scanRadius{ 3000.f }, reassignSeconds{ 30.f };                  // [budget]
	int maxPackageChanges{ 2 };
	std::unordered_map<std::string, std::vector<FormID>> spots;          // camp_spots.ltx [spots]: campfire, bed, seat, ...
	std::array<std::vector<std::vector<std::string>>, kFactionCount> stories;   // [stories_<faction>] story_N = line | line | ...
	CampConfig();   // compiled-in defaults (docs/CAMPLIFE.md 3.1 / 3.7)
};
bool LoadCampConfig(const Ltx& a_camp, const Ltx* a_spots, CampConfig& a_out, std::string& a_err);

struct CampSlot
{
	CampJob job{ CampJob::None };
	int index{ 0 };   // n-th slot of that job at the place
};
// The slots a place has: its kind's table (or the default) x max_population, rounded; guard / sniper / camper counts x a_strength
// (Config::Diff(Strength), rounded, so 1.0 is the table). Sorted by priority (daytime), then job, then index.
std::vector<CampSlot> SlotsFor(const GraphNode& a_node, const CampConfig& a_cfg, float a_strength);

struct CampAssign
{
	std::uint64_t squad{ 0 }, member{ 0 };
	CampJob job{ CampJob::None };
	int index{ 0 };   // slot index; -1 for the unlimited walker fallback
};
bool InSleepWindow(const CampConfig& a_cfg, float a_hour);

// Anomaly select_npc_job for every living member of the squads at one place: a member keeps its job while it holds unless a free
// slot of higher priority exists; free members take the highest priority free slot whose condition holds; one member per slot
// across all squads; leaders go first and take patrol (then guard) when the place has one. Monster squads get no jobs.
// Conditions: sleep only inside the sleep window; at night at most ceil(living x watch_share x strength) members stay on watch
// (camper, sniper, guard, patrol); sniper only archers; camper only non-archers of outlaw factions (standing opportunism > 0); collector only
// hunting factions (activity_factions hunt) and mages. Deterministic: no random numbers.
std::vector<CampAssign> AssignJobs(const GraphNode& a_node, const std::vector<const Group*>& a_squads, float a_hour, const Config& a_cfg,
	const CampConfig& a_camp, const std::vector<CampAssign>* a_prev = nullptr);

}  // namespace ALife
