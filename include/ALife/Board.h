#pragma once
// The Anomaly-style simulation board: smart terrains + squads + factions.
//
// Ported mechanics (Anomaly 1.5.3 scripts, see docs/PARITY.md):
//   sim_board.get_squad_target        -> Board::GetSquadTarget      (best-5, 50% best / 50% random)
//   simulation_objects.evaluate_prior -> Board::EvaluatePrior       (5 + props + weighted behaviour, * distance factor)
//   smart_terrain.target_precondition -> Board::TargetPrecondition  (base / territory / resource windows, relations)
//   sim_squad_scripted.update         -> Board::UpdateSquad         (reach target -> stay idle_time -> pick next)
//   smart_terrain.try_respawn         -> Board::TryRespawn          (respawn_idle, respawn_radius, per-entry quotas)
//   sim_offline_combat (Tronex OCS)   -> Board::RunOcs / Battle     (power = sum of ranks, damage = power * (0.4 + 0.2 rnd))
//   smart_terrain.check_smart_faction -> Board::UpdateControl       (controlling faction, war in progress)
// Dependency-free: everything Skyrim-specific goes through Host.
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ALife/Config.h"
#include "ALife/Core.h"
#include "ALife/World.h"

namespace ALife {

class Wounds;

struct Host
{
	virtual ~Host() = default;
	virtual int PlayerLevel() const { return 10; }
	// true when the player is inside the sim worldspace and closer than a_radius to the point
	virtual bool PlayerNear(float /*a_x*/, float /*a_y*/, float /*a_radius*/, int /*a_ws*/) const { return false; }
	// Build one squad member (race, name, outfit, skills ...). a_leader marks the commander.
	virtual Member MakeMember(Faction a_f, Tier a_tier, Role a_role, int a_level, bool a_leader, Rng& a_rng, std::uint64_t a_id) = 0;
	// ... in worldspace a_ws (the squad's home at creation: races.ltx has per-worldspace mixes, T24). Hosts that do not care keep MakeMember.
	virtual Member MakeMemberIn(int /*a_ws*/, Faction a_f, Tier a_tier, Role a_role, int a_level, bool a_leader, Rng& a_rng, std::uint64_t a_id)
	{
		return MakeMember(a_f, a_tier, a_role, a_level, a_leader, a_rng, a_id);
	}
	// Anomaly "rank": relative fighting value of one member (health-independent).
	virtual float MemberPower(const Member& a_m) const = 0;
	// Monster members: the board computes a creature's power from monsters.ltx with the same formula the plugin uses for people;
	// a host whose human scale differs (the harness) rescales it here.
	virtual float MonsterPower(const Member& /*a_m*/, float a_power) const { return a_power; }
	// Called when a member of an offline squad dies (killer may be null: wildlife / hazards).
	virtual void OnMemberKilled(Group& /*a_victimGroup*/, Member& /*a_victim*/, Group* /*a_killerGroup*/, Member* /*a_killer*/) {}
	virtual void LootBodies(Group& /*a_winner*/, std::vector<Member>& /*a_dead*/) {}
	virtual void GrowMember(Member& /*a_m*/, float /*a_rankXp*/, Rng& /*a_rng*/) {}
	// The player has levelled: bring an offline member up to a_targetLevel (stats, skills, perks, and a new kit when the old one is far behind).
	virtual void ScaleMember(Member& /*a_m*/, int /*a_targetLevel*/) {}
	// Dynamic news line (a_x,a_y = where it happened). The host decides whether to show it.
	virtual void News(const std::string& /*a_line*/, float /*a_x*/, float /*a_y*/) {}
	virtual void Log(const std::string& /*a_line*/) {}
	// Online squads: has the leader reached the smart yet (engine distance check)?
	virtual bool OnlineReached(const Group& /*a_g*/, const GraphNode& /*a_smart*/) { return false; }
	// A group of A-Life squads cleared/occupied a hostile lair (the plugin may kill vanilla inhabitants when the player arrives).
	virtual void OnConquest(const GraphNode& /*a_smart*/, Faction /*a_by*/, int /*a_power*/) {}

	// --- needs and activities (the plugin's activity layer: trade, raids, hunting, fishing ...)
	virtual bool SurvivalOn() const { return false; }                       // cold matters
	virtual bool FishingAvailable() const { return false; }
	virtual void Consume(Member& /*a_m*/) {}                                // eat when hungry, drink a potion when hurt
	virtual bool HasFood(const Group& /*a_g*/) const { return true; }
	virtual bool HasSellables(const Group& /*a_g*/) const { return false; } // loot worth selling
	virtual bool NeedsSupplies(const Group& /*a_g*/) const { return false; }// potions / ammo / food running low
	virtual bool VendorsOpen(const GraphNode& /*a_t*/, double /*a_nowHours*/) const { return false; }
	// strength of the vanilla guards and villagers a raid has to get past (garrison squads are handled by offline combat)
	virtual float DefenderStrength(const GraphNode& /*a_t*/, Faction /*a_raiders*/) const { return 0.f; }
	virtual void DoTrade(Group& /*a_g*/, GraphNode& /*a_t*/) {}
	virtual void DoRaidSettlement(Group& /*a_g*/, GraphNode& /*a_t*/, bool /*a_success*/, float /*a_ratio*/) {}
	virtual void DoClearSection(Group& /*a_g*/, GraphNode& /*a_t*/, int /*a_section*/, int /*a_kills*/) {}
	virtual void DoHaul(Group& /*a_g*/, GraphNode& /*a_t*/) {}
	virtual void DoHunt(Group& /*a_g*/, double /*a_hours*/, Rng& /*a_rng*/) {}
	virtual void DoFish(Group& /*a_g*/, double /*a_hours*/, Rng& /*a_rng*/) {}
	virtual void OnDepart(Group& /*a_g*/, GraphNode& /*a_t*/) {}
};

struct Stats
{
	std::array<std::uint64_t, kFactionCount> spawned{}, wiped{}, membersLost{}, membersKilled{}, arrivals{};
	std::uint64_t battles{ 0 }, retreats{ 0 }, retargets{ 0 }, locks{ 0 }, controlChanges{ 0 }, steps{ 0 };
	std::uint64_t trades{ 0 }, raidsWon{ 0 }, raidsLost{ 0 }, dungeonsCleared{ 0 }, dungeonsAborted{ 0 }, hunts{ 0 }, fishings{ 0 }, rests{ 0 };
	std::array<std::array<std::uint32_t, kFactionCount>, kFactionCount> killMatrix{};   // [killer][victim]
	std::array<std::uint64_t, kFactionCount> partsHarvested{};   // creature parts taken by each faction (pelts, troll fat ...)
	std::uint64_t relationChanges{ 0 }, relationFlips{ 0 }, sosNews{ 0 }, deathNews{ 0 }, battleNews{ 0 }, gossipNews{ 0 };
	std::uint64_t holdFlips{ 0 }, guardsSwapped{ 0 }, stoodDown{ 0 };   // Civil War progress (docs/CIVILWAR.md)
	std::uint64_t dragonsMade{ 0 }, dragonFlees{ 0 }, dragonFights{ 0 }, dragonFlights{ 0 };   // docs/DRAGONS.md
};

struct SmartState
{
	std::vector<int> spawned;                    // already_spawned per respawn entry
	double lastRespawn{ -1 };
	std::uint8_t faction{ kNoFaction };          // controlling faction (Anomaly se_smart_terrain.faction)
	bool war{ false };                           // hostile factions share the place (faction_war_in_progress)
	int population{ 0 };                         // squads assigned (heading here or staying)
	int monsters{ 0 };                           // ... of them monster squads (a lair's monster_population cap)
	int stayed{ 0 };                             // squads that have arrived and are idling here
	double raidedUntil{ 0 };                     // game hours: no settlement raid / dungeon clearing before this
};

class Board
{
public:
	Board(const Config& a_cfg, World& a_world, Host& a_host);

	// --- lifecycle
	void Reset(double a_nowHours);
	void StartFill(const std::array<bool, kFactionCount>* a_only = nullptr);   // initial population (Anomaly simulation.ltx start squads); a_only: just these factions (monster kinds a loaded save has none of)
	void Advance(double a_toHours);                // simulate up to a_toHours in cfg.stepHours chunks
	void Step(double a_hours);                     // one simulation step; now += a_hours

	// --- state (public: co-save, debug UI and the harness read/write it)
	std::vector<Group> squads;
	std::vector<SmartState> smart;
	std::uint64_t nextId{ 1 };
	std::uint64_t worldSeed{ 1 };                  // fixes which places have a fractional respawn slot (stable per save)
	double now{ 0 };
	Rng rng{ 1 };
	Stats stats;
	int maxSquads{ 400 };                          // safety valve for CPU / pool pressure (~200 people + ~120 monster squads at full lairs)
	bool surgeActive{ false };                     // reserved: Anomaly emission analogue
	std::unordered_set<std::uint64_t> pinned;      // squads held at their place by a job (assault: tasks_assault.script:484-510); set by Tasks, not saved
	std::unordered_set<std::uint64_t> companions;  // squads travelling with the player (docs/COMPANIONS.md): out of the simulation; set by Party, not saved
	std::unordered_map<std::uint64_t, std::uint32_t> forced;   // squad -> place it is sent to and held at once there (defend waves, a captive walking home); set by Tasks, not saved
	std::unordered_set<std::uint64_t> held;        // squads out of the simulation where they stand (a rescue's captive): no moving, no offline fights, never on screen; set by Tasks, not saved
	Wounds* wounds{ nullptr };                     // wounded / help / finish / surrender (docs/WOUNDED.md); null: nobody ever goes down

	// --- dragons (docs/DRAGONS.md): an own board path for MonsterType::Flyer (straight flight between dragon places, hunting the
	// squads at its place, a world-wide cap and a respawn timer). Numbers only through DragonEff().
	float dragonScale{ 1.f };                      // a test override on top of T21's Diff(Dragons) (the harness sweeps it); applied only in DragonEff()
	double dragonLast{ -1e9 };                     // game hour the last dragon was made or killed (respawn timer); co-saved (record DRGN)
	std::uint32_t dragonKills{ 0 };                // dragons the player killed (the plugin counts them); co-saved (DRGN)
	DragonNumbers DragonEff() const { return ScaledDragons(m_cfg.dragons, m_cfg.Diff(DiffChannel::Dragons) * dragonScale); }   // frequency and level x Diff(Dragons)
	bool IsDragon(const Group& a_g) const { return m_cfg.monster[int(a_g.faction)] == MonsterType::Flyer; }
	int DragonCount() const;                       // living dragons on the board
	int DragonCap() const;                         // dragons allowed now (max_dragons x the multiplier, rounded)

	Host& HostRef() { return m_host; }
	const World& WorldRef() const { return m_world; }
	// A member of faction a_victim was killed by a_killer (offline here, or online by the plugin): dynamic relations
	// (Anomaly game_relations.calculate_relation_change; nothing happens unless cfg.dynamicRelations).
	void OnKill(Faction a_killer, Faction a_victim, Tier a_victimTier);
	bool MonsterHeld(std::uint32_t a_smart) const { return a_smart < smart.size() && smart[a_smart].monsters > 0; }
	const Config& Cfg() const { return m_cfg; }

	// --- Civil War progress and faction gates (docs/CIVILWAR.md, core/CivilWar.cpp). Not saved: the host derives them from the
	// game at every load and on change; Reset puts the places back to the config's start owners and opens every gate.
	std::uint8_t HoldOwner(FormID a_hold) const;         // live owner of a [hold_faction] hold (its start owner until told otherwise)
	std::uint8_t EffectiveOwner(FormID a_hold) const;    // ... the winner everywhere once the war is over; kNoFaction: not a war hold
	bool SetHoldOwner(FormID a_hold, Faction a_f);       // true when it changed: the hold's places, their bonus and owner respawns move
	void SetWarOver(std::uint8_t a_loser);               // the losing side (kNoFaction: the war goes on)
	std::uint8_t WarLoser() const { return m_cwLoser; }
	void SetGated(Faction a_f, bool a_shut);             // a shut faction has no squads (Dawnguard before Fort Dawnguard's rebuild)
	bool Gated(Faction a_f) const { return m_gated[int(a_f)]; }
	bool Blocked(Faction a_f, int a_ws) const;           // squads of a_f may not exist in that worldspace (stood down or gated)
	// Off-screen squads only: a hold's guards whose place changed side are replaced by the new side's, squads of a blocked
	// faction retire. Runs at the start of every step; returns how many squads it touched.
	int ApplyFactionState();

	// --- queries
	Group* Find(std::uint64_t a_id);
	int SquadCount(Faction a_f) const;
	int MemberCount(Faction a_f) const;
	std::string DescribeSquad(const Group& a_g) const;

	// --- mechanics (public for tests and for the plugin's online driver)
	float EvaluatePrior(const GraphNode& a_t, const SmartState& a_s, const Group& a_g) const;
	bool TargetPrecondition(const GraphNode& a_t, const SmartState& a_s, const Group& a_g, bool a_ignorePopulation) const;
	// The place in worldspace a_ws nearest (a_x, a_y) that a_g could target there (TargetPrecondition as if it were in a_ws,
	// population ignored); -1 when there is none. Companions dismissed in another worldspace settle there (docs/COMPANIONS.md).
	std::int64_t NearestValidPlace(const Group& a_g, int a_ws, float a_x, float a_y) const;
	const GraphNode* GetSquadTarget(const Group& a_g);
	Group* CreateSquad(std::uint32_t a_smart, const std::string& a_section, bool a_register = true);
	void AssignToSmart(Group& a_g, std::int64_t a_smart);   // -1 releases
	void RemoveSquad(Group& a_g, const char* a_why);
	float SquadPower(const Group& a_g) const;               // sum of health-weighted power of the members standing (Anomaly ocs power)
	bool Stands(const Member& a_m) const;                   // alive and not down (docs/WOUNDED.md)
	int StandingCount(const Group& a_g) const;
	float PowerOf(const Member& a_m) const;                 // health-independent power: the host's for people, monsters.ltx for creatures
	// --- monsters (docs/MUTANTS.md)
	bool IsMonster(const Group& a_g) const { return m_cfg.IsMonster(a_g.faction); }
	bool MonsterTerritoryOpen(Faction a_f, float a_hour) const;        // Anomaly general_territory_precondition for mutants
	bool MonsterMayComeOnline(const Group& a_g, float a_hour) const;   // sim_squad_scripted: day / night mutants stay offline outside their hours
	int MonsterSquads() const;
	bool MonsterRoom(const GraphNode& a_t, const SmartState& a_s, const Group& a_g) const;   // the lair's monster cap, not counting the squad itself
	void MakeCreature(Member& a_m, const CreatureDef& a_def, int a_level) const;   // stats of a monster member from its creature
	// How many squads of a respawn entry a place keeps alive: floor(num * pop) plus one more when its stable lottery
	// ticket is under the fractional part, so num = 0.4 means "about 40% of such places have a squad".
	int MaxCount(std::uint32_t a_smart, std::size_t a_entry) const;

private:
	friend class Wounds;   // after-battle finishing, wounded survivors' news and falling back use the board's internals
	void RecomputePopulation();
	void TryRespawn(std::uint32_t a_smart);
	void UpdateSquad(Group& a_g, double a_hours);
	bool Move(Group& a_g, const GraphNode& a_target, double a_hours);   // true when it arrived
	void RunOcs();
	void UpdateDragon(Group& a_g, double a_hours);   // dragons (docs/DRAGONS.md)
	const GraphNode* DragonTarget(const Group& a_g);
	void TickDragons();                              // a new dragon when below the cap and the timer has run
	void DragonHunt();                               // dragons at a place: novice / advanced people flee, veterans fight
	void Battle(Group& a, Group& b, double a_hours);
	void ChangeRelation(Faction a, Faction b, int a_delta);
	void Gossip();
	std::unordered_map<std::uint32_t, double> m_sosUntil, m_battleNewsUntil;   // per place: no repeat SOS / battle report before
	std::array<double, kFactionCount> m_deathNewsUntil{};                    // per faction: next death report
	double m_nextGossip{ -1 };
	double m_lastSos{ -1e9 }, m_lastBattleNews{ -1e9 }, m_lastDeathNews{ -1e9 };   // world-wide gaps per news kind
	void Round(Group& a_attacker, Group& a_victim, std::vector<Member>& a_deadVictims);
	void UpdateControl();
	void ResetFactionState();
	void AimHold(FormID a_hold);
	bool SwapGuard(std::size_t a_index);
	std::unordered_map<FormID, std::uint8_t> m_holdOwner;
	std::uint8_t m_cwLoser{ kNoFaction };
	std::array<bool, kFactionCount> m_gated{};
	void TickNeeds(double a_hours);
	void PaceLevels();
	double m_lastPace{ -1e9 };
	int m_lastPlayerLevel{ 0 };
	// activities (core/Activities.cpp)
	struct SquadNeeds { bool food{ false }, rest{ false }, warm{ false }, sell{ false }, supplies{ false }, heal{ false }; };
	SquadNeeds NeedsOf(const Group& a_g) const;
	float NeedBias(const Group& a_g, const GraphNode& a_t) const;
	Act ChooseActivity(Group& a_g, GraphNode& a_n);
	void RunActivity(Group& a_g, GraphNode& a_n, double a_hours, bool a_fresh);
	void ResolveSettlementRaid(Group& a_g, GraphNode& a_n);
	void StepDungeonClear(Group& a_g, GraphNode& a_n, double a_hours);
	void WoundSquad(Group& a_g, float a_chance, float a_minHp, float a_maxHp);
	void Reap(Group& a_g);                         // retire members an activity killed
	void SendOnItsWay(Group& a_g);                 // end the stay now
	void Heal(double a_hours);
	void Sweep();
	void MemberDied(Group& a_victimGroup, Member& a_victim, Group* a_killerGroup, Member* a_killer);
	std::string SquadLabel(const Group& a_g) const;
	std::string PlaceName(std::uint32_t a_node) const;
	float MemberEff(const Member& a_m) const;

	const Config& m_cfg;
	World& m_world;
	Host& m_host;
	std::vector<std::uint64_t> m_order;
	mutable std::uint64_t m_needsGid{ 0 }, m_needsStep{ ~0ULL };
	mutable SquadNeeds m_needsCache;
};

}  // namespace ALife
