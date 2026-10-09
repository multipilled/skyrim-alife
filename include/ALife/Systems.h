#pragma once
#include "ALife/Board.h"
#include "ALife/Config.h"
#include "ALife/Model.h"
#include "ALife/World.h"

// Plugin systems. The simulation brain (smart terrains, squads, factions, offline combat) lives in the dependency-free
// core (Board.h); everything below is the Skyrim side: building the map from the game's locations, the actor pool,
// bringing squads on screen, gear, protection of vanilla NPCs, the co-save and the UI hooks.
namespace ALife {

// ---------------- Graph: smart terrains built from every placed vanilla location
namespace Graph {
void Build(const Config& a_cfg);                // kDataLoaded; once per session
bool Ready();
World& W();
const std::vector<GraphNode>& Nodes();
const GraphNode* Node(std::uint32_t a_id);
std::uint32_t Nearest(float a_x, float a_y, int a_ws = 0);
std::uint32_t ByLocation(RE::FormID a_location);   // UINT32_MAX when unknown
bool InCityBuffer(float a_x, float a_y, int a_ws = 0);
RE::TESObjectREFR* MarkerOf(const GraphNode& a_n);  // persistent world marker, nullptr for places without one
}

// ---------------- Pool
namespace Pool {
struct Slot
{
	RE::FormID refId{ 0 };
	RE::FormID baseId{ 0 };
	RaceId race{ RaceId::Nord };
	Sex sex{ Sex::Male };
	bool inUse{ false };
	bool reserved{ false };
	std::uint64_t owner{ 0 };     // sticky member id
	bool debugFill{ false };
};
void Init();                                          // kDataLoaded: first resolve attempt (refs may not exist yet)
int Resolve(const char* a_when);                      // (re)resolve missing ACHRs; NewGame/PostLoad/tick retry; returns resolved count
int ResolvedCount();
void ResetScan();                                     // allow one more base-NPC fallback scan (per load)
std::vector<Slot>& Slots();
int StickyUsed();
int Unreserved(RaceId a_r, Sex a_s);                 // pool actors of race/sex not held by a sticky tie
bool Assign(Member& a_m, bool a_allowFallbackRace);
bool CanAssignAll(const std::vector<Member>& a_members, bool a_allowFallbackRace);   // dry run, no state change
void Release(Member& a_m);
void ReleaseSticky(Member& a_m);                      // death: free reservation
void RebuildReservations(std::vector<Group>& a_groups);   // after co-save load
RE::Actor* ActorOf(const Member& a_m);
bool IsPoolActor(const RE::Actor* a_a);
void SweepAllToHolding();                             // load / uninstall: no stray actors
void FullWipe(RE::Actor* a_a);
void ApplyPerks(RE::Actor* a_a, const std::vector<RE::FormID>& a_perks);
}

// ---------------- Squads loot the dead (src/Scavenge.cpp)
namespace Scavenge {
void Reset();
void Tick(float a_dt);                                  // once per frame
bool Busy(std::uint64_t a_groupId);
bool TickGroup(Group& a_g, RE::Actor* a_leader, float a_dt);   // true while the squad is walking to / standing at a body (its packages are not driven)
int Looted();
}

// ---------------- Protection of vanilla NPCs (named / quest / merchants / followers ...)
namespace Protect {
void Init();
bool IsFollower(RE::Actor* a_a);
bool IsProtectedActor(RE::Actor* a_a);
bool IsBossRef(const RE::TESObjectREFR* a_r);
bool IsBossRefId(RE::FormID a_id);
bool InQuestAlias(RE::TESObjectREFR* a_r);
bool IsLeaveAloneActor(RE::Actor* a_a);
void OnHit(RE::Actor* a_target, RE::Actor* a_aggressor);
void OnCombat(RE::Actor* a_actor, RE::Actor* a_target);
void Tick();
}

// ---------------- Patch hooks (0x800/0x801/0x802 in "ALife - *" plugins)
namespace Patch {
void Reload();
bool AvoidLocation(RE::FormID a_loc);
bool CampKeepAway(float a_x, float a_y, float a_z);
bool LeaveAloneFaction(RE::FormID a_fac);
const std::vector<RE::FormID>& LeaveAloneFactions();
}

// ---------------- Combat strength (Anomaly "rank" for the offline combat simulator)
namespace Combat {
float MemberStrength(const Member& a_m);       // current health included
float MemberPower(const Member& a_m);          // health independent (Anomaly rank)
float GroupStrength(const Group& a_g);
int MainCombatSkill(const Member& a_m);
}

// ---------------- Economy: gear by faction and role, loot, growth
namespace Economy {
void OutfitNew(Member& a_m);                        // starting kit for a fresh member (m.faction / m.role / m.level / m.race set)
void LootBodies(Group& a_winners, std::vector<Member>& a_dead);
float CarryLimit(const Member& a_m);
float Carried(const Member& a_m);
void ApplyLoadoutTo(RE::Actor* a_a, const Member& a_m);    // coming online
void ReadBackFrom(RE::Actor* a_a, Member& a_m);            // going offline
void Grow(Member& a_m, float a_rankXp, Rng& a_rng);        // offline combat rank -> skill gains (capped per day)
void LevelUp(Member& a_m);
void ScaleToLevel(Member& a_m, int a_target);              // the world levels with the player: stats, skills, perks, and a new kit when far behind
void RegrowPerks(Member& a_m);
int HealPotions(const Member& a_m);
bool DrinkHealPotion(Member& a_m);
void Reequip_(Member& a_m);
void Haul(Member& a_m, int a_level, Rng& a_rng);              // loot from the raid-haul list into a member's pack
void HuntCatch(Member& a_m, Rng& a_rng);
// helpers for the merchant / activity layers
bool Wants(const Member& a_m, RE::TESBoundObject* a_obj);   // useful gear or consumable for this member
bool IsFoodItem(RE::TESForm* a_f);
bool IsHealPotionItem(RE::TESForm* a_f);
bool IsArtifactItem(RE::FormID a_id);
bool IsArcher(const Member& a_m);
int CountItem(const Member& a_m, RE::FormID a_id);
int FoodCount(const Member& a_m);
int ArrowCount(const Member& a_m);
void AddItem(Member& a_m, RE::FormID a_id, int a_n);
bool RemoveItem(Member& a_m, RE::FormID a_id, int a_n);
void Consume(Member& a_m);                                  // eat when hungry, drink a potion when badly hurt
}

// ---------------- Activities: Skyrim-side effects of what squads do at a place
namespace Activities {
bool SurvivalOn();
bool FishingAvailable();
bool HasFood(const Group& a_g);
float DefenderStrength(const GraphNode& a_n, Faction a_raiders);
void DoRaidSettlement(Group& a_g, GraphNode& a_n, bool a_success, float a_ratio);
void DoClearSection(Group& a_g, GraphNode& a_n, int a_section, int a_kills);
void DoHaul(Group& a_g, GraphNode& a_n);
void DoHunt(Group& a_g, double a_hours, Rng& a_rng);
void DoFish(Group& a_g, double a_hours, Rng& a_rng);
}

// ---------------- Sim: the plugin host around the board
namespace Sim {
std::recursive_mutex& Lock();
Board& B();                       // valid once the map is built
Config& Cfg();
bool BoardReady();
std::vector<Group>& Groups();
Group* FindGroup(std::uint64_t a_id);
Group* GroupOfActor(RE::Actor* a_a, Member** a_outMember = nullptr);
double Hours();
std::uint32_t Day();
int GroupsOnline();
int ActorsLoaded();
bool GateOpen();
bool CatchingUp();                // simulation is more than an hour behind the game clock (after a wait / fast travel)

void OnDataLoaded();
void OnNewGame();
void OnPreLoad();
void OnPostLoad();
void MainTick(float a_realDt);    // main thread, ~10 Hz, never input-driven
void SetEnabled(bool a_on);
void RequestPrepareUninstall();
void ApplySettings();             // MCM options -> config (population factor, offline combat mode, news)

// player relations (Anomaly "actor" row + dynamic goodwill)
float PlayerRel(Faction a_f);
void AddPlayerRel(Faction a_f, float a_delta);

bool ReleaseStickyFor(const Member& a_newcomer);    // 48 full: free the most releasable tie
void OnGroupMemberDied(Group& a_g, Member& a_m);    // pool bookkeeping for a dead member
void OnActorKilled(RE::Actor* a_victim, RE::Actor* a_killer);   // an online squad member died in the world
void ApplyQueuedKills(RE::TESObjectCELL* a_cell);

struct Persist
{
	bool gateOpened{ false };
	std::vector<std::uint64_t> waitlist;                              // member ids waiting for a sticky slot
	std::unordered_map<RE::FormID, std::int32_t> queuedKills;         // cell -> kills to apply when attached
	std::array<float, kFactionCount> playerRelDelta{};                // dynamic goodwill change per faction
};
Persist& P();

// debug (bDebug=1 hotkeys)
void DebugStatus();
void DebugFastForward();
void DebugHunters();      // M5g bridge "hunters": lair-hunting squads, their targets and parts
void DebugPlacePair();
void DebugGotoSquad();
void DebugScenario();             // place the next faction's test squad ahead of the player
void DebugKill();                 // the player strikes and kills one member of that squad
void DebugTalk();                 // talk to that squad: the same as pressing Activate on its leader
std::uint64_t DebugScenarioGroup();   // that squad's id, 0 when none
std::string StatusLine();         // one-line summary for the MCM
}

// ---------------- Online: pool actors in the world
namespace Online {
void Tick(float a_dt);
void SendAllOffline(bool a_finishFightsOffscreen);
void SendOffline(Group& a_g, bool a_finishFight);
bool SpawnPointValid(const RE::NiPoint3& a_p);
void OnCellDetach(RE::TESObjectCELL* a_cell);
bool BringOnlineEx(Group& a_g, bool a_allowFallbackRace, bool a_forceNearPlayer);
void Reset();         // clear slot owners / retry + skip state (PreLoad, PostLoad, NewGame)
void ParkMarkers();   // slot markers back to the holding cell
std::string DescribeActors(const Group& a_g);
std::string OfflineWhy(const Group& a_g);                 // why Tick would not bring this offline squad online now ("" = eligible)
bool LeaderNear(const Group& a_g, const GraphNode& a_n);   // leader within the node's arrive distance (for the board)
void Approach(Group& a_g, float a_stopDist);              // the leader walks up to the player and stops a_stopDist short
bool ApproachRef(Group& a_g, RE::TESObjectREFR* a_target, float a_stopDist);   // ... to any reference; true once there (and holding)
void Hold(Group& a_g);                                    // stand where they are (sandbox)
void Resume(Group& a_g);                                  // back to what the board planned
}

// ---------------- Creatures: monster squads on screen (docs/MUTANTS.md)
namespace Creatures {
RE::Actor* ActorOf(const Member& a_m);                          // the placed creature of a monster member, nullptr when none
bool IsCreature(const RE::Actor* a_a);
Group* GroupOf(RE::Actor* a_a, Member** a_outMember = nullptr); // online monster squad of a creature
RE::Actor* Spawn(Member& a_m, RE::TESObjectREFR* a_at);         // place the member's creature (monsters.ltx form) at a_at
void Despawn(Group& a_g);                                       // wounds and deaths back into the squad; living creatures deleted
int Live();
std::vector<RE::FormID> Refs();                                 // living placed creatures (co-save: deleted again after a load)
void QueueSweep(std::vector<RE::FormID> a_refs);
void ClearSweep();
void Tick();                                                    // after a load: sweep; carried-over wounds once 3D is in
void Reset();
}

// ---------------- Behavior: stance toward the player, trust, hostility
namespace Behavior {
bool TooHurt(const Group& a_g);
bool AttacksOnSight(const Group& a_g);                 // faction hostile to the player (and not too hurt)
void TickOnline(Group& a_g, float a_dt);
void OnPlayerAttacked(Group& a_g);
void Reset();                                          // forget every meeting (new game / load)
std::uint64_t HostilityHash(std::uint64_t a_gid, std::uint32_t a_day);
}

// ---------------- Co-save
namespace Save {
void Register();
}

namespace Papyrus {
bool Register(RE::BSScript::IVirtualMachine* a_vm);
}

namespace Events {
void RegisterEarly();     // input (kInputLoaded)
void RegisterGame();      // script events + menus (kDataLoaded)
}
}
