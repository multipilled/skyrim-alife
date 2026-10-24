#pragma once
// Typed view of the A-Life .ltx configs (alife.ltx, factions.ltx, props.ltx, squads.ltx).
// Dependency-free: shared by the plugin and the headless harness.
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "ALife/Compat.h"
#include "ALife/Core.h"
#include "ALife/Difficulty.h"
#include "ALife/Ltx.h"

namespace ALife {

enum class OcsMode : std::uint8_t { Off = 0, Smarts = 1, Full = 2 };

// What the player is, as far as the world can tell (bit flags in PlayerContext::flags; names are used by standing.ltx rules).
namespace PlayerFlag {
inline constexpr std::uint32_t Imperial = 1u << 0, Stormcloak = 1u << 1, Companions = 1u << 2, ThievesGuild = 1u << 3,
	DarkBrotherhood = 1u << 4, College = 1u << 5, Dawnguard = 1u << 6, Volkihar = 1u << 7, Blades = 1u << 8, Greybeards = 1u << 9,
	Vampire = 1u << 10, Werewolf = 1u << 11, BeastForm = 1u << 12, Armed = 1u << 13, Sneaking = 1u << 14, Mounted = 1u << 15,
	InCombat = 1u << 16, Night = 1u << 17,
	SkaalFriend = 1u << 18, RavenRock = 1u << 19;   // Dragonborn: freed the Skaal (DLC2SV01), stood by Raven Rock (DLC2RR01/02/03)
std::uint32_t FromName(const std::string& a_name);   // 0 when unknown
}

// One condition of a standing rule ("imperial", "!vampire", "race:altmer,bosmer", "level>=20", "bounty>=100", "enforces" ...).
struct RuleCond
{
	enum class Type : std::uint8_t { Flag, Race, Level, Notoriety, Bounty, Enforces, Disguise };
	Type type{ Type::Flag };
	bool negate{ false };
	std::uint32_t flag{ 0 };               // Flag
	std::vector<std::uint8_t> races;       // Race: RaceId values
	bool atLeast{ true };                  // Level / Notoriety / Bounty / Disguise: >= (true) or < (false) value
	float value{ 0 };
	std::uint8_t faction{ kNoFaction };    // Disguise
};

// "Because you are X, faction Y regards you this much better or worse."
struct StandingRule
{
	std::string name, label;
	std::vector<RuleCond> when;            // all must hold
	std::array<float, kFactionCount> delta{};
};

// standing.ltx: how the factions regard the player (see docs/STANDING.md).
struct StandingConfig
{
	std::array<float, kFactionCount> base{};          // attitude toward an unremarkable traveller
	std::array<float, kFactionCount> opportunism{};   // 0..1 outlaw boldness: attacks when it holds the advantage
	std::array<float, kFactionCount> territorial{};   // 0..1 defends its ground against intruders
	std::array<bool, kFactionCount> lawful{};         // challenges armed strangers instead of hunting them
	std::array<bool, kFactionCount> parley{};         // offers a toll before attacking
	std::array<bool, kFactionCount> nocturnal{};      // bolder at night
	std::array<float, kFactionCount> murderWeight{};  // how much killing their people counts toward notoriety (1 = fully)
	float hostileAt{ -1000 }, unfriendlyAt{ -300 }, friendlyAt{ 300 }, alliedAt{ 1000 };
	float goodwillMax{ 2000 };
	// deeds
	float killBase{ 40 }, killPerLevel{ 1.2f }, killVeteran{ 25 }, killProvokedMult{ 0.35f };
	float allyShare{ 0.5f }, friendShare{ 0.25f }, enemyShare{ 0.25f };
	float unseenMult{ 0.15f }, discoverChance{ 0.4f }, discoverMult{ 0.6f }, discoverMinHours{ 6 }, discoverMaxHours{ 36 };
	float helpBase{ 14 }, helpPerLevel{ 0.5f }, dailyGainCap{ 150 };
	float tradePer100Gold{ 1.5f };
	float attackGoodwill{ 8 };
	float notorietyPerSeverity{ 0.8f };
	float attackMemory{ -250 }, killMemory{ -180 }, helpMemory{ 150 }, tollMemory{ 120 }, memoryHalfLifeDays{ 20 };
	float halfLifePosDays{ 40 }, halfLifeNegDays{ 100 }, notorietyHalfLifeDays{ 60 };
	float disguiseBonus{ 350 }, disguiseSuspicion{ 250 };
	// predators and watchers
	float advantageFloor{ 0.7f }, advantageFull{ 2.0f };
	float hungerBonus{ 0.15f }, wealthBonus{ 0.2f }, nightBonus{ 0.25f }, fearPenalty{ 0.25f };
	float parleyFrom{ 0.35f }, attackAt{ 0.6f }, ambushAt{ 0.85f };
	float noticeRange{ 2200 }, greetRange{ 700 }, challengeRange{ 1100 }, challengeGrace{ 9 };
	float tollBase{ 40 }, tollPerLevel{ 6 }, tollPassHours{ 12 };
	float territoryRadius{ 2500 };
	float talkCooldown{ 90 };           // seconds after a conversation closes before another squad stops the player to talk (attacks are not held back)
	bool notices{ false };                             // tell the player on screen when a faction's regard changes tier (off: they find out by talking)
	float murderBounty{ 650 };                         // crime gold a hold adds for the unprovoked killing of its soldiers
	std::vector<StandingRule> rules;
};


// Anomaly mutant communities: how a monster faction uses territories and when it may be on screen (docs/MUTANTS.md).
enum class MonsterType : std::uint8_t { None = 0, PredatoryDay, PredatoryNight, ZombiedDay, ZombiedNight, Vegetarian, Zombied, Special, Flyer };   // Special: Anomaly monster_special, keeps to its lairs at any hour; Flyer: dragons (docs/DRAGONS.md), their own board path
MonsterType MonsterTypeFromName(const std::string& a_name);   // "predatory_day" ... "zombied"; None when unknown

// One kind of creature a monster squad is made of (monsters.ltx [creature_<id>]).
struct CreatureDef
{
	std::string id, name;
	FormID form{ 0 };                   // leveled actor list (LVLN) or actor base (NPC_) spawned online; 0 = not available
	std::string plugin;                 // empty: a Skyrim.esm FormID; else `form` is the local id in this plugin (looked up there when spawned)
	float health{ 60 }, healthPerLevel{ 4 };
	float damage{ 6 }, damagePerLevel{ 0.4f };
	float hit{ 0.7f };                  // share of attacks that land (power formula, same as human members)
	std::vector<Item> parts;            // what a hunter takes from one (pelts, troll fat, ...): Skyrim.esm forms and counts
};

// Dragons (docs/DRAGONS.md; config/dragons.ltx, config/dragon_places.ltx). Tags name the difficulty channel each number scales
// with (T21): [dragons] the Dragons channel (frequency, level), [-] not scaled. The board reads them only through
// Board::DragonEff() (= ScaledDragons(config, multiplier)), never from Config directly.
struct DragonNumbers
{
	float maxDragons{ 2.f };            // [dragons] dragons alive at once, world-wide (Tamriel)
	double respawnDays{ 3.0 };          // [dragons] a new dragon this long after the last was made or killed; more dragons = sooner
	float levelMult{ 1.f };             // [dragons] x the player level when a new dragon's tier is picked
	float flySpeed{ 120000.f };         // [-] world units per game hour, straight line (walking: alife.ltx walk_speed 20000)
	double stayMin{ 1.5 }, stayMax{ 4.0 };   // [-] game hours at a place before the next flight
	float preyBonus{ 0.5f };            // [-] place weight x (1 + this x human squads staying there)
	float lairWeight{ 3.f }, wallWeight{ 2.f }, highWeight{ 1.f };   // [-] dragon lairs, word walls, high wilderness
	float highZ{ 10000.f };             // [-] wilderness places at least this high are dragon country
	double fleeHours{ 6.0 };            // [-] novice / advanced human squads that fled a dragon keep away this long
	float onlineDistance{ 16000.f }, offlineDistance{ 24000.f }, spawnHeight{ 2500.f };   // [-] plugin: on screen
	double onlineMaxHours{ 2.0 };       // [-] plugin: then it flies off (sent offline)
};
struct DragonConfig : DragonNumbers
{
	std::vector<std::pair<std::string, int>> tiers;   // squad section, from (effective) player level; ascending
	std::unordered_map<FormID, float> places;         // [places] location = weight (overrides; 0 removes a place)
	std::unordered_map<FormID, int> walls;            // dragon_places.ltx [word_walls]: location -> word-wall refs
};
DragonNumbers ScaledDragons(const DragonNumbers& a_base, float a_dragons);   // the one place the Dragons multiplier applies
struct GraphNode;
struct Config;
float DragonWeight(const Config& a_cfg, const GraphNode& a_n);             // a place: its dragon weight (call after the node flags are set)

struct SquadDef
{
	std::string section;
	Faction faction{ Faction::Bandit };
	Tier tier{ Tier::Novice };
	int minMembers{ 2 }, maxMembers{ 3 };
	std::array<float, int(Role::COUNT)> roleWeights{};   // relative weight per member role
	int levelOffsetMin{ -2 }, levelOffsetMax{ 2 };       // member level relative to the player
	bool common{ true };                                 // Anomaly "common": scaled by the population factor
	std::vector<std::pair<int, float>> creatures;        // monster squads: creature index (Config::creatures) and weight
	int leaderCreature{ -1 };                            // monster squads: the first member is this creature (pack leader, draugr boss)
};

// One faction's race mix (races.ltx): integer weights in draw order (the order is part of the draw: keep it when editing).
struct RaceWeight
{
	RaceId race{ RaceId::Nord };
	int w{ 1 };
};
using RaceTable = std::vector<RaceWeight>;

struct Config
{
	// [sim]
	float stepHours{ 1.f / 6.f };       // offline simulation step (game hours)
	float walkSpeed{ 20000.f };         // world units per game hour
	double maxCatchUpHours{ 336.0 };

	// [online]
	float switchDistance{ 7000.f };     // Anomaly switch_distance: squads inside it may come online
	float offlineDistance{ 11000.f };

	// [board]
	int topTargets{ 5 };                // Anomaly: best 5 candidates
	float bestChance{ 0.5f };           // Anomaly: 50% best / 50% random of the five
	float distanceBias{ 4000.f };       // prior *= 1 + bias / max(dist, 1)   (Anomaly: 1 + 1/dist with dist in metres)
	float idleMin{ 2.f }, idleMax{ 8.f };   // STAY_POINT_IDLE_MIN/MAX (game hours)
	float retargetHours{ 1.f };         // a squad with no target (__lock) tries again after this long
	// simulation_objects.default_squad_behaviour (weights applied to target props)
	float wActor{ 0 }, wAll{ 1 }, wBase{ 1 }, wLair{ 0 }, wResource{ 2 }, wSquad{ 0 }, wSurge{ 3 }, wTerritory{ 1 };
	// time windows (Anomaly general_*_precondition)
	float baseRaidFrom{ 3 }, baseRaidTo{ 6 };            // enemy-held base: only 03-06h
	float territoryRaidFrom{ 9 }, territoryRaidTo{ 15 }; // enemy-held territory: only 09-15h
	float resourceFrom{ 10 }, resourceTo{ 6 };           // resource spots: 10-06h
	// monsters (Anomaly default_monster_behaviour and the mutant windows of general_territory_precondition / sim_squad_scripted)
	float mLair{ 1 }, mMonster{ 1 }, mTerritory{ 1 }, mBase{ 0 }, mResource{ 0 };   // no emissions (user): no surge weight for monsters
	float mHome{ 0 };
	float lairIdleMin{ 2 }, lairIdleMax{ 8 };            // Skyrim addition: how long a monster squad stays once it is in a lair (dens are where beasts sleep)                                    // Skyrim addition: extra priority for the lair that spawned the squad (draugr keep to their tombs)
	float monsterDayFrom{ 6 }, monsterDayTo{ 19 };       // day predators / day zombies hunt territories 06-19
	float monsterNightFrom{ 19 }, monsterNightTo{ 6 };   // night predators / night zombies 19-06
	float dayOfflineFrom{ 22 }, dayOfflineTo{ 6 };       // day monsters are never brought online 22:00-05:59
	float nightOfflineFrom{ 6 }, nightOfflineTo{ 19 };   // night monsters never 06:00-18:59

	// [respawn]
	float defaultRespawnIdle{ 12.f };   // respawn_idle (game hours; Anomaly 43200 s)
	float defaultRespawnRadius{ 10500.f };   // 150 m
	float populationFactor{ 1.f };      // alife_stalker_pop: the ltx value x the patrol-mod share (difficulty: Diff(Population) on top)
	float populationLtx{ 1.f };         // [respawn] population_factor as loaded (Sim::ApplySettings rebuilds populationFactor from it)
	float startFill{ 0.7f };            // fraction of each smart's respawn quota present at game start
	float monsterPopulationFactor{ 1.f };   // Anomaly alife_mutant_pop: scales monster respawn entries instead of populationFactor

	// [ocs] Tronex offline combat simulator
	OcsMode ocsMode{ OcsMode::Smarts };
	float combatDistance{ 2500.f };     // enemy squads closer than this fight (Anomaly 20 m)
	float hitBase{ 0.4f }, hitFactor{ 0.2f };
	float rankFraction{ 250.f };        // attacker rank += damage / rank_fraction
	float commanderGain{ 50.f };
	int roundsPerStep{ 4 };
	int minSquadsForOcs{ 30 };          // MAX_SQUAD_COUNT: OCS only thins crowded worlds
	float fleeRatio{ 0.35f };           // a losing squad below this fraction of its starting power may break off
	float healPerHour{ 4.f };           // offline wound recovery

	// [loot] squads strip the dead they pass
	bool lootCorpses{ true };
	float lootRadius{ 1800.f };         // how far a calm squad notices a body
	float lootGraceSeconds{ 90.f };     // a body is left alone this long (the player, who killed it, gets the first look)
	float lootGiveUpSeconds{ 50.f };    // a squad that cannot reach the body in this time forgets it
	int lootMinValue{ 25 };             // not worth the walk below this much gold + wanted goods
	std::array<bool, kFactionCount> looters{};

	// [balance] the world levels with the player
	float levelPaceHours{ 4.f };        // game hours between catch-up steps
	int levelsPerPace{ 1 };             // levels a member that is behind its target gains per step
	int regearGap{ 6 };                 // levels grown past the kit's level before the member is re-equipped

	// [needs] Survival-style needs (0..1000 per member)
	float hungerPerHour{ 18.f }, fatiguePerHour{ 22.f }, coldDay{ 8.f }, coldNight{ 30.f };
	float hungryAt{ 600.f }, tiredAt{ 700.f }, coldAt{ 600.f };
	float needBiasFood{ 3.f }, needBiasRest{ 4.f }, needBiasWarm{ 3.f }, needBiasSell{ 3.f }, needBiasHeal{ 3.f };

	// [activities]
	float settlementRaidCooldownDays{ 7.f };    // a place is raided at most this often
	float dungeonResetDays{ 10.f };             // a cleared dungeon is left alone for this long (vanilla respawn analogue)
	float raidSectionHours{ 2.f };              // game hours per dungeon section
	float merchantCycleHours{ 48.f };           // vanilla vendors restock every 48 h: so do the sim's limits
	float merchantGoldFloor{ 0.40f };           // sales / raids never leave a merchant under this share of its gold
	float sellRate{ 0.33f }, buyMarkup{ 1.5f };
	int maxPurchasesPerCycle{ 3 };
	float huntRate{ 0.25f }, wildlifeRisk{ 0.03f }, fishRate{ 0.30f };

	// [activity_factions]
	std::array<bool, kFactionCount> doesTrade{}, doesRaidSettlements{}, doesClearDungeons{}, doesHunt{}, doesFish{};
	std::array<bool, kFactionCount> harvestsParts{}, huntsLairs{};   // take creature parts from kills / go after occupied lairs
	float wHuntLair{ 3.f };            // target priority of a lair a monster squad holds, for lair-hunting factions

	// [news]
	bool news{ true };
	float newsRadius{ 25000.f };        // only mention events this close to the player (0 = whole map)
	bool newsSos{ true }, newsDeaths{ true }, newsGossip{ true };   // Anomaly dynamic_news: SOS under attack, death reports, gossip
	float sosCooldownHours{ 3.f }, deathCooldownHours{ 2.f }, gossipHours{ 6.f };
	float sosGapHours{ 2.f }, battleGapHours{ 3.f }, deathGapHours{ 3.f };   // world-wide: at most one of each kind per gap (Anomaly queues 3)
	// [dynamic_relations] (Anomaly game_relations + dynamic_faction_relations.ltx; off by default like Anomaly)
	bool dynamicRelations{ false };
	float deathValue{ 40.f };           // relation step per death (scaled by the victim's tier)
	int friendLimit{ 5000 }, enemyLimit{ -5000 };   // pegged relations reset to neutral half of the time (Anomaly)
	std::vector<std::pair<Faction, Faction>> lockedPairs;   // never change (the Civil War pair)

	// [relations]
	std::array<std::array<std::int16_t, kFactionCount>, kFactionCount> rel{};   // >= 1000 ally, <= -1000 enemy
	std::array<std::int16_t, kFactionCount> playerRel{};                         // baseline attitude toward the player
	std::unordered_map<FormID, Faction> holdFaction;                              // hold location -> aligned faction
	std::array<std::uint8_t, kWorldspaces> wsFaction{ kNoFaction, kNoFaction };   // places outside any hold (Solstheim) default to this faction
	StandingConfig standing;                                                      // standing.ltx
	// races.ltx (T24): [ws][faction]; an empty table in a worldspace above 0 means "as in Tamriel". The compiled-in defaults are
	// the tables the plugin used before T24, so Tamriel squads draw exactly as before.
	std::array<std::array<RaceTable, kFactionCount>, kWorldspaces> races;
	const RaceTable& RacesFor(Faction a_f, int a_ws) const;
	RaceId RollRace(Faction a_f, int a_ws, Rng& a_rng) const;   // one r.index(total) draw, as before

	// [civil_war] (docs/CIVILWAR.md): the two sides, which respawn entries are a hold's guards, where the loser stands down
	bool civilWar{ true };
	std::array<std::uint8_t, 2> cwSides{ std::uint8_t(Faction::Imperial), std::uint8_t(Faction::Stormcloak) };
	std::vector<std::string> guardEntries{ "owner_garrison", "owner_patrol", "garrison", "patrol" };
	std::array<bool, kWorldspaces> standDownWs{ true, false };
	bool IsCwSide(std::uint8_t f) const { return civilWar && (f == cwSides[0] || f == cwSides[1]); }
	std::uint8_t CwOpponent(std::uint8_t f) const { return f == cwSides[0] ? cwSides[1] : f == cwSides[1] ? cwSides[0] : kNoFaction; }
	// [faction_gates]: a faction has no squads until one of its quest stages is reached (the plugin reads the quests)
	// "plugin|quest:stage", or "plugin|global=value" (global: the gate opens once the global is at least value; dragons: DragonsReturned)
	struct FactionGate { Faction faction{ Faction::Dawnguard }; std::string plugin; FormID quest{ 0 }; std::uint16_t stage{ 0 }; bool global{ false }; float value{ 1.f }; };
	std::vector<FactionGate> gates;

	DragonConfig dragons;                  // dragons.ltx + dragon_places.ltx (docs/DRAGONS.md)

	// monsters (factions.ltx [monsters], monsters.ltx)
	std::array<MonsterType, kFactionCount> monster{};
	std::vector<CreatureDef> creatures;
	std::unordered_map<FormID, int> creatureByForm;
	bool IsMonster(Faction f) const { return monster[int(f)] != MonsterType::None; }
	// After the host has checked plugin creatures (form stays the local id, or 0 when the plugin is missing): rebuild the
	// form index and drop unavailable creatures from the squads; a monster squad left without creatures is never made.
	void IndexCreatures();
	const CreatureDef* CreatureOf(const Member& a_m) const
	{
		auto it = creatureByForm.find(a_m.classId);
		return it == creatureByForm.end() ? nullptr : &creatures[std::size_t(it->second)];
	}

	// squads
	std::vector<SquadDef> squads;
	std::unordered_map<std::string, int> squadIndex;
	const SquadDef* Squad(const std::string& a_section) const
	{
		auto it = squadIndex.find(a_section);
		return it == squadIndex.end() ? nullptr : &squads[it->second];
	}

	// compat.ltx: patrol mods sharing the world (docs/COMPAT.md). compat is filled by the host after LoadConfig
	// (ResolveCompat with the loaded plugins), before the graph is built.
	CompatConfig compatCfg;
	CompatState compat;

	// difficulty.ltx (docs/DIFFICULTY.md): diff is filled by ApplyDifficulty (plugin: game difficulty + MCM; harness: --difficulty / --diff).
	// Every system reads Diff(channel) or the derived diff values, nothing else.
	DifficultyConfig diffCfg;
	DifficultyState diff;
	float Diff(DiffChannel a_c) const { return diff.Get(a_c); }
	void ApplyDifficulty(int a_game, const DifficultyChoice& a_choice) { diff = ComputeDifficulty(diffCfg, a_game, a_choice, compat.onlineActors); }

	// props presets (resolved on demand from props.ltx)
	Ltx props;

	// relations in play: the static matrix, or the live one when dynamic relations are on (written by Board::OnKill, co-saved)
	mutable std::array<std::array<std::int32_t, kFactionCount>, kFactionCount> relLive{};
	int Rel(Faction a, Faction b) const { return dynamicRelations ? relLive[int(a)][int(b)] : rel[int(a)][int(b)]; }
	void ResetLiveRelations() const { for (int a = 0; a < kFactionCount; ++a) for (int b = 0; b < kFactionCount; ++b) relLive[a][b] = rel[a][b]; }
	bool IsEnemy(Faction a, Faction b) const { return Rel(a, b) <= -1000; }
	bool IsAlly(Faction a, Faction b) const { return Rel(a, b) >= 1000; }
};

// Loads every *.ltx in a_dir that exists. Missing files fall back to compiled-in defaults (see DefaultsDir/ docs).
bool LoadConfig(const std::string& a_dir, Config& a_out, std::string& a_err);

// standing.ltx -> StandingConfig (called by LoadConfig; also usable on its own by tests).
void LoadStandingConfig(const Ltx& a_ltx, StandingConfig& a_out);

// Fills a node's board data (props, caps, respawn entries) from the props preset for its kind / location override.
void ApplyProps(const Config& a_cfg, GraphNode& a_node);

// Classifies a location from its vanilla keyword FormIDs (LocTypeBanditCamp = 0x130DF ...).
Kind ClassifyKeywords(const std::vector<FormID>& a_keywords);

}  // namespace ALife
