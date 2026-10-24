#pragma once
// Patrol-mod compatibility (docs/COMPAT.md): which patrol / garrison mods are loaded (by plugin filename), the smarts they
// hold, how their vanilla factions map to ALife factions, and the actor budget ALife keeps while they run.
// Dependency-free: the plugin passes the loaded plugin names, the tests pass fake ones.
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ALife/Core.h"
#include "ALife/Ltx.h"

namespace ALife {

struct Config;

// One [compat_<id>] section of compat.ltx.
struct CompatMod
{
	std::string id, name;
	std::vector<std::string> plugins;    // filename patterns: case-insensitive, '*' matches any run of characters
	std::vector<Kind> handoverKinds;     // every smart of these kinds is the other mod's
	std::vector<FormID> handover, keep;  // ... plus these locations, minus these
	int onlineGroups{ kMaxOnlineGroups }, onlineActors{ kMaxOnlineActors };
	float population{ 1.f }, patrolScale{ 1.f };
};

struct CompatConfig
{
	std::vector<CompatMod> mods;
	std::vector<std::string> force;      // [compat] force = ids: detected without their plugin (lab)
	bool ownerHold{ true };              // handover_owner = hold: a handed-over smart belongs to the hold's side
	int handoverPopulation{ 1 };         // ALife squads a handed-over smart takes at once
	std::vector<std::pair<FormID, Faction>> factionMap;   // [compat_factions]: vanilla faction -> ALife faction
};

// Budget override (MCM "Patrol mods", INI iCompatBudget).
inline constexpr int kCompatBudgetAuto = -1, kCompatBudgetOff = 0;
inline constexpr float kCompatMinPopulation = 0.3f;
inline constexpr int kCompatActorsPerGroup = 5;

// The on-screen budget (docs/COMPAT.md 4): a_online groups on screen (companions included), a_reserve slots kept for companions
// not placed yet. Companions count against the budget and outrank world squads: when they fill it, no world squad comes on screen,
// and a companion or a summoned job squad that needs a slot makes a world squad leave.
inline bool AtOnlineBudget(int a_online, int a_budget, int a_reserve = 0) { return a_online + a_reserve >= a_budget; }

// What is in effect after detection.
struct CompatState
{
	std::vector<std::string> detected;   // mod ids
	std::vector<std::string> matched;    // the plugin file that matched, per detected mod ("force" when forced)
	bool handover{ false };
	std::vector<Kind> kinds;
	std::unordered_set<FormID> places, keep;
	bool ownerHold{ true };
	int handoverPopulation{ 1 };
	int onlineGroups{ kMaxOnlineGroups }, onlineActors{ kMaxOnlineActors };
	float population{ 1.f }, patrolScale{ 1.f };
	std::vector<std::pair<FormID, Faction>> factionMap;   // empty unless a mod is detected

	bool Active() const { return !detected.empty(); }
	bool Detected(const std::string& a_id) const;
	bool HandsOver(const GraphNode& a_n) const;          // by location or kind, not kept
};

bool PluginMatches(const std::string& a_pattern, const std::string& a_file);
void LoadCompat(const Ltx& a_ltx, CompatConfig& a_out, std::string* a_err = nullptr);
// a_budget: kCompatBudgetAuto (the mods' own caps), kCompatBudgetOff (ALife's full caps), 1..3 online groups.
CompatState ResolveCompat(const CompatConfig& a_cfg, const std::vector<std::string>& a_loadedPlugins, int a_budget = kCompatBudgetAuto,
	bool a_handover = true);
// Recompute only the budget part (MCM change at runtime; the hand-over is fixed at data load).
void ApplyCompatBudget(const CompatConfig& a_cfg, CompatState& a_state, int a_budget);

// Runs at the end of ApplyProps: a smart the other mod holds gets no respawn, the hold's side as owner, and only that side
// and its allies may target it.
void ApplyHandover(const Config& a_cfg, GraphNode& a_n);

// Engine faction reactions to add while a mod is detected: ALife faction record <-> vanilla faction (both directions).
struct CompatReaction
{
	Faction alife;
	FormID external;      // Skyrim.esm-relative FormID of the vanilla faction
	bool ally;            // ally (combat reaction 2) or enemy (1)
};
std::vector<CompatReaction> CompatReactions(const Config& a_cfg);

}  // namespace ALife
