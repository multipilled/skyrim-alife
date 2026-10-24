#pragma once
// ALife core data model - the Anomaly-style simulation (smart terrains, squads, factions).
// Dependency-free: no CommonLibSSE / RE headers, so the same code runs inside the plugin and in the headless harness.
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ALife/Util.h"

namespace ALife {

using FormID = std::uint32_t;

// ------------------------------------------------------------------ factions
// Anomaly faction (community) -> Skyrim faction:
//   stalker -> Wanderer, bandit -> Bandit, dolg -> Imperial, freedom -> Stormcloak, army -> Thalmor,
//   monolith -> Forsworn, killer -> Mercenary, ecolog -> Mage, renegade -> SilverHand, ...
// Anomaly's mutant communities -> the monster factions after the human ones (docs/MUTANTS.md): Wildlife (wolves, skeevers,
// sabre cats, bears), Draugr, Falmer, Troll, Spriggan, Zombie (Plague of the Dead, optional). Their behaviour (predator by
// day / night ...) comes from factions.ltx [monsters].
enum class Faction : std::uint8_t
{
	Bandit = 0, Forsworn, Imperial, Stormcloak, Thalmor, Wanderer, Hunter, Mercenary, Mage,
	Necromancer, Vampire, Dawnguard, SilverHand, Vigilant, Caravan,
	Skaal, Redoran, Reaver,         // Solstheim (Dragonborn): the Skaal, House Redoran's guard, the Reavers
	Wildlife, Draugr, Falmer, Troll, Spriggan, Zombie,
	Hound, Werebeast, Riekling, Spirit, Hagraven, Giant, Dwemer,   // M4b
	Dragon,                         // M4c (docs/DRAGONS.md): appended, so every saved faction index keeps its value
	COUNT
};
inline constexpr int kFactionCount = int(Faction::COUNT);
inline constexpr int kHumanFactionCount = int(Faction::Wildlife);   // factions with pool actors, standing, dialogue (ALifeFactions.esp)
inline constexpr bool IsHumanFaction(Faction a_f) { return int(a_f) < kHumanFactionCount; }
inline constexpr std::uint8_t kNoFaction = 255;
// Saves from before the Solstheim factions numbered the monsters from 15: the shift lives in ALife/SaveFormat.h (FactionIndexOf).
const char* FactionId(Faction a_f);        // short id used in .ltx files ("imperial")
const char* FactionName(Faction a_f);      // display name ("Imperial Legion")
Faction FactionFromId(const std::string& a_id, bool* a_ok = nullptr);

enum class Tier : std::uint8_t { Novice = 0, Advanced, Veteran, COUNT };
const char* TierName(Tier a_t);

enum class Sex : std::uint8_t { Male = 0, Female = 1 };

inline constexpr int kWorldspaces = 2;   // 0 Tamriel (and its child worldspaces), 1 Solstheim

enum class RaceId : std::uint8_t
{
	Nord = 0, Imperial, Breton, Redguard, Altmer, Bosmer, Dunmer, Orsimer, Argonian, Khajiit,
	NordVampire, ImperialVampire, BretonVampire, DunmerVampire,
	COUNT
};
bool IsVampireRace(RaceId a_r);
const char* RaceIdName(RaceId a_r);                                     // races.ltx id ("dunmer", "dunmervampire")
RaceId RaceFromId(const std::string& a_id, bool* a_ok = nullptr);
inline constexpr const char* kWorldspaceIds[kWorldspaces] = { "Tamriel", "DLC2SolstheimWorld" };   // races.ltx [races.<id>] overrides

// Skill indices match RE::ActorValue order kOneHanded..kEnchanting (6..23)
enum Skill : std::uint8_t
{
	kOneHanded = 0, kTwoHanded, kArchery, kBlock, kSmithing, kHeavyArmor, kLightArmor, kPickpocket,
	kLockpicking, kSneak, kAlchemy, kSpeech, kAlteration, kConjuration, kDestruction, kIllusion,
	kRestoration, kEnchanting, kSkillCount
};

// A squad member's equipment role (Economy::KitOf picks gear from the vanilla class tables).
enum class Role : std::uint8_t { Melee = 0, Heavy, Archer, Mage, Spellsword, Leader, COUNT };

enum class Trust : std::uint8_t { Neutral = 0, Wary, BackingOff, Hostile };

// What a squad is doing while it idles at a place (the analogue of Anomaly's smart terrain jobs). Run by the plugin's
// activity layer; the board only stores the state.
enum class Act : std::uint8_t { None = 0, Rest, Trade, Hunt, Fish, RaidSettlement, RaidDungeon };
const char* ActName(Act a_a);

struct Item
{
	FormID form{ 0 };
	std::int32_t count{ 0 };
};

struct Member
{
	std::uint64_t id{ 0 };
	std::string name;
	std::string title;
	Faction faction{ Faction::Bandit };
	Role role{ Role::Melee };
	RaceId race{ RaceId::Nord };
	Sex sex{ Sex::Male };
	FormID classId{ 0 };
	std::int32_t level{ 1 };
	std::int32_t levelBias{ 0 };       // level relative to the player when the member was made: the world levels with the player, veterans stay ahead
	std::int32_t gearLevel{ 1 };       // level the kit was rolled at (a member that has grown far past it is re-equipped)
	std::array<float, kSkillCount> skills{};
	float health{ 100 }, magicka{ 50 }, stamina{ 50 };
	float healthMax{ 100 }, magickaMax{ 50 }, staminaMax{ 50 };
	std::int32_t gold{ 0 };
	std::vector<Item> inventory;   // everything carried (gear included)
	std::vector<FormID> equipped;
	std::vector<FormID> perks;
	std::vector<FormID> spells;
	float hunger{ 0 }, cold{ 0 }, fatigue{ 0 };
	bool sticky{ false };          // keeps one pool face for life (named / veteran members)
	bool veteran{ false };
	bool waitlisted{ false };
	std::int32_t poolLocal{ -1 };      // index into the actor pool, -1 none
	std::uint32_t growthDay{ 0 };
	std::array<std::uint8_t, kSkillCount> gainToday{};
	float fightBonusToday{ 0 };
	float rankXp{ 0 };                 // Anomaly rank points earned in offline combat (damage / rank_fraction)
	std::int32_t kills{ 0 };
	std::int32_t arrows{ 0 };
	bool alive() const { return health > 0.f; }
};

// ------------------------------------------------------------------ world
// Smart terrain "kind" - what the location is. Drives the props preset (props.ltx [kind_map]).
enum class Kind : std::uint8_t
{
	Wilderness = 0, City, Town, Farm, Inn, Fort, BanditCamp, ForswornCamp, MilitaryCamp, Dungeon, Crypt, DwarvenRuin, Mine,
	VampireLair, WarlockLair, WerewolfLair, GiantCamp, FalmerHive, DragonLair, AnimalDen, HagravenNest, SprigganGrove,
	OrcStronghold, DragonPriestLair, Shipwreck, Mill, COUNT
};
const char* KindName(Kind a_k);
Kind KindFromName(const std::string& a_name, bool* a_ok = nullptr);

// Anomaly simulation_objects_props.ltx: base/territory/resource/lair/surge plus a coefficient per faction.
struct Props
{
	float base{ 0 }, territory{ 0 }, resource{ 0 }, surge{ 0 }, lair{ 0 };
	float all{ 0 };                                   // "all": any non-monster squad may target
	float monster{ 0 };                               // "monster" (Anomaly all_monster): any monster squad may target
	std::array<float, kFactionCount> byFaction{};     // props[faction]
};

struct RespawnParam
{
	std::string name;                     // key in the smart's respawn list (Anomaly: prop section name)
	std::vector<std::string> squads;      // squad section names ("bandit_advanced")
	float num{ 1 };                       // max squads of this entry alive at once (fractions = spawn chance)
	std::uint8_t faction{ kNoFaction };   // only when the smart is faction controlled: spawn only for the controlling faction
	bool owner{ false };                  // "faction = owner": follows the place's owner when its hold changes side (docs/CIVILWAR.md)
};


struct GraphNode
{
	std::uint32_t id{ 0 };
	std::uint8_t ws{ 0 };      // worldspace index: coordinates and the travel graph are per worldspace
	FormID location{ 0 };      // BGSLocation
	FormID markerRef{ 0 };     // persistent world marker ref
	FormID hold{ 0 };          // owning hold location
	std::string name;
	std::string holdName;
	float x{ 0 }, y{ 0 }, z{ 0 };
	float radius{ 0 };
	Kind kind{ Kind::Wilderness };
	std::vector<FormID> keywords;
	// board data (filled from props.ltx)
	Props props;
	std::string preset;
	int maxPopulation{ 2 };
	int monsterCap{ 0 };                // monster squads a lair holds at once (props monster_population; 0 = only max_population)
	float arriveDist{ 600.f };
	bool simAvail{ true };
	bool raidable{ true };              // settlement raids may target it (guild halls, faction bases opt out)
	bool disabled{ false };
	bool factionControlled{ false };
	std::uint8_t defaultFaction{ kNoFaction };
	bool holdAligned{ false };          // default_faction = hold: follows the hold's live Civil War owner (docs/CIVILWAR.md)
	float ownerBonus{ 0 };              // the preset's "owner" priority, moved with defaultFaction
	float respawnIdleHours{ 12.f };
	float respawnRadius{ 10500.f };
	std::vector<RespawnParam> respawn;
	// derived flags used by the plugin (online placement / protection)
	bool city{ false }, settlement{ false }, inn{ false }, dungeon{ false }, water{ false }, playerHome{ false };
	std::vector<FormID> cells;          // interior cells of a dungeon, boss cell last (activity layer: raids)
	FormID bossCell{ 0 };
	int vendors{ 0 };                   // merchants that trade here (activity layer)
	bool handedOver{ false };           // a patrol mod garrisons it (docs/COMPAT.md): no ALife respawn, only its owner's side visits
	float dragon{ 0 };                  // dragon place weight (docs/DRAGONS.md: lair, word wall, high wilderness); 0 = dragons never come
	std::vector<std::uint32_t> edges;
};

// ------------------------------------------------------------------ squads
enum class Action : std::uint8_t { None = 0, Reach = 1, Stay = 2 };   // Anomaly current_action: nil / 0 / 1

struct Group
{
	std::uint64_t id{ 0 };
	std::uint8_t ws{ 0 };                      // worldspace of the node it was made at; squads never change worldspace (one exception: a companion dismissed in another worldspace, Party::Dismiss)
	Faction faction{ Faction::Bandit };
	Tier tier{ Tier::Novice };
	std::string section;                       // squad description it was created from
	float x{ 0 }, y{ 0 }, z{ 0 };
	std::uint32_t node{ 0 };                   // last graph node passed / current node
	std::uint32_t prevNode{ 0 };
	std::vector<Member> members;

	// sim_squad_scripted state
	Action action{ Action::None };
	std::int64_t assignedTarget{ -1 };         // smart id the squad is heading to / staying at, -1 none
	std::int64_t currentTarget{ -1 };          // smart id it has reached (after the Reach -> Stay transition)
	std::int64_t smartAssigned{ -1 };          // smart whose population this squad counts toward
	double stayStart{ 0 };
	double idleHours{ 4 };
	bool locked{ false };                      // Anomaly __lock: no target found, retry later
	double lockedUntil{ 0 };
	std::int64_t respawnSmart{ -1 };           // smart that spawned it (frees its already_spawned slot on death)
	std::string respawnEntry;
	double retreatUntilHours{ 0 };             // broke off a losing fight: no OCS engagements until this time
	std::int64_t avoidNode{ -1 };              // while retreating, do not pick targets near this node (the enemy)
	float ocsStartPower{ 0 };                  // squad power when the current fight began (for the morale check)
	std::uint64_t opponent{ 0 };
	bool commonSquad{ true };

	// online bridge (plugin)
	bool online{ false };
	std::int32_t slot{ -1 };

	// activity at the current place
	Act activity{ Act::None };
	std::int32_t actStep{ 0 };                 // activity progress (raid section ...)
	float actHours{ 0 };                       // game hours spent on it
	bool arrivedFresh{ false };                // set on arrival, cleared once the activity layer saw it

	// relation with the player
	std::int32_t standing{ 0 };
	std::int32_t letGoCount{ 0 };
	bool foughtPlayer{ false };
	bool attackedByPlayer{ false };
	double lastMetHours{ -1.0 };
	Trust trust{ Trust::Neutral };
	float trustTimer{ 0 };
	bool helpedByPlayer{ false };
	float memory{ 0 };                         // what this squad itself holds against / owes the player (-1000..1000)
	double tollUntil{ 0 };                     // paid for passage until this game time
	bool playerStruckFirst{ false };           // runtime only: the player hit this squad before it was hostile
	float startStrength{ 0 };
	std::int32_t startMembers{ 0 };

	std::int32_t livingCount() const;
	float avgLevel() const;
};

inline constexpr int kMaxOnlineGroups = 3;
inline constexpr int kMaxOnlineActors = 15;
inline constexpr int kStickyCap = 48;
inline constexpr int kPoolSize = 166;
inline constexpr float kPopInMinDist = 4000.f;
inline constexpr float kSpawnPreferMin = 6000.f;
inline constexpr float kSpawnPreferMax = 9000.f;
inline constexpr float kNoiseBase = 1500.f;
inline constexpr float kNoisePerExtra = 400.f;
inline constexpr float kNoiseFightCap = 5500.f;
inline constexpr float kNoiseCap = 8500.f;
inline constexpr float kCityBuffer = 2000.f;
inline constexpr double kMaxCatchUpHours = 336.0;

}  // namespace ALife
