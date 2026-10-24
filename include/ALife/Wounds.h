#pragma once
// Wounds (docs/WOUNDED.md, M5d): Anomaly's wounded, help, finish and surrender on ALife squads.
// Dependency-free: who goes down, offline lethal hits, the after-battle finish / help / wounded survivors, recovery, the surrender
// and yield decisions, the player's choices (with their Standing deeds) and the co-save blob run the same in the plugin and in the
// headless tests (tests/wounds_test.cpp). The plugin turns the state into bleedout flags, help lines, dialogue and notices.
//
// Ported (Anomaly 1.5.3): xr_wounded.script (hp_state 20%, autoheal 90 s, eat_medkit), xr_help_wounded.script (a squadmate brings a
// medkit, rank +10..20), xrs_kill_wounded.script (enemies finish the wounded; actor_cause_surrender_chance 0.25 within 8 m),
// dialogs.script (transfer_medkit, surrender answers), xr_motivator.script (killed_prisoners). Offline Anomaly has no wound
// state (sim_offline_combat only kills); ALife adds it so beaten squads can lie where they fell.
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ALife/Core.h"
#include "ALife/Ltx.h"

namespace ALife {

class Board;
class Standing;

struct WoundConfig
{
	bool enabled{ true };
	float hp{ 0.20f };                           // under this share of its health a member goes down ...
	float chance{ 0.7f };                        // ... with this chance per crossing
	std::array<bool, kFactionCount> factions{};  // who can go down (human factions by default; monsters never)
	float ocsChance{ 0.35f };                    // offline: a lethal hit leaves the member down instead
	float downHpMin{ 0.05f }, downHpMax{ 0.15f };
	float reviveHp{ 0.30f };
	float callSeconds{ 15.f }, callRadius{ 3000.f };
	float helpRadius{ 2500.f };
	float selfHealSeconds{ 90.f };
	float finishRadius{ 1500.f };
	float finishDelayMin{ 2.f }, finishDelayMax{ 5.f };
	float downHoursMin{ 2.f }, downHoursMax{ 6.f };
	float fallBackHoursMin{ 3.f }, fallBackHoursMax{ 6.f };
	int helperRankMin{ 10 }, helperRankMax{ 20 };
	// surrender
	float yieldDistance{ 600.f }, yieldChance{ 0.25f };
	float losingShare{ 0.5f }, outnumber{ 1.5f };
	float spareHours{ 12.f };
	float releaseRadius{ 3000.f }, surrenderSeconds{ 120.f };
	std::array<float, kFactionCount> finish{};      // offline: chance a human winner finishes each down enemy
	std::array<float, kFactionCount> surrender{};   // chance a losing / outnumbered squad fighting the player gives up
	// the player's choices (wounded.ltx [standing]): magnitudes, the deed gives the sign
	float woundHelp{ 40 }, woundHelpMemory{ 300 }, rob{ 15 }, robMemory{ 200 }, finishDeed{ 30 }, spare{ 20 }, spareMemory{ 250 };
	float prisoner{ 60 }, prisonerNotoriety{ 40 };
};

// Loads wounded.ltx (missing keys keep the defaults above; a missing [wounded] section is an error). The result is clamped.
bool LoadWoundConfig(const Ltx& a_ltx, WoundConfig& a_out, std::string& a_err);
WoundConfig DefaultWoundConfig();   // the documented defaults (every human faction can go down)

// Difficulty channels (T21): m > 1 makes that channel harder, 1 = wounded.ltx as written. Each balance key is tagged in wounded.ltx
// with its channel and direction ({lethality-}: value / m ...). Not built yet: T21 fills it from difficulty.ltx and the MCM.
struct WoundScale
{
	float lethality{ 1.f };   // offline / online combat lethality: fewer go down, they lie longer, more get finished
	float enemy{ 1.f };       // enemy squad strength: fewer yield or surrender, more losses before they consider it
	float economy{ 1.f };     // goodwill and memory: smaller rewards for mercy, bigger costs for cruelty
	// T21's Mercy channel (Config::Diff(DiffChannel::Mercy), 1.0 at Adept), the other way round: m > 1 is kinder. The chances to go
	// down, to yield and to surrender are multiplied by it (clamped to 1), the finish chances divided; 0 means nobody goes down.
	float mercy{ 1.f };
};
// The one read path for the balance numbers: a_base scaled by the channels, then clamped (chances and shares in [0, 1], times and
// distances >= 0, ranges ordered). Wounds keeps the result in cfg; T21 calls it again when a slider or the game difficulty moves.
WoundConfig ScaleWoundConfig(const WoundConfig& a_base, const WoundScale& a_scale);
void ClampWoundConfig(WoundConfig& a_cfg);

// One member who is down.
struct Down
{
	enum Flag : std::uint8_t { kYielded = 1, kOffline = 2, kCalled = 4 };
	std::uint64_t member{ 0 }, squad{ 0 };
	double since{ 0 };                 // game hour it went down
	double upAt{ 0 };                  // offline wounded survivors: game hour the squad gets up (0: not set)
	float healthBefore{ 0 };           // yielded: health before it knelt (it gets up with it when spared)
	std::uint8_t flags{ 0 };
	// runtime only (the plugin's on-screen timers; not saved, they restart after a load)
	float seconds{ 0 };                // real seconds down
	float nextCall{ 0 };               // seconds until the next help line
	bool Yielded() const { return flags & kYielded; }
	bool Offline() const { return flags & kOffline; }
};

// A squad's surrender state in the current fight with the player.
struct Surrender
{
	bool rolled{ false };              // the squad check was rolled this fight
	bool yielded{ false };             // the whole squad gave up
	float seconds{ 0 };                // real seconds since it yielded (runtime)
};

struct WoundEvent
{
	enum class Kind : std::uint8_t { Down, Up, Finished, Call, SelfHeal, Yielded, Released, Survivors, Helped };
	Kind kind{ Kind::Down };
	std::uint64_t squad{ 0 }, member{ 0 };
	std::uint64_t by{ 0 };             // Helped: the helper's member id; Finished: the killer's squad id (0: the player / nobody)
};

// The player's choices with a down or yielded member (talk modes 5 and 6).
enum class WoundChoice : std::uint8_t { Heal, Rob, Finish, Spare, RobAndGo, NoMercy };
const char* WoundChoiceId(WoundChoice a_c);   // "heal", "rob", "finish", "spare", "robgo", "nomercy" (the debug `choose` prompts)

class Wounds
{
public:
	WoundConfig cfg{ DefaultWoundConfig() };

	// --- state (saved in WOND)
	std::unordered_map<std::uint64_t, Down> down;          // by member id
	std::unordered_map<std::uint64_t, Surrender> fights;   // by squad id: surrender rolls in the current fight with the player
	// runtime only
	std::unordered_set<std::uint64_t> yieldRolled;          // members that rolled the single yield check
	std::unordered_set<std::uint64_t> foughtThisStep;       // offline squads that fought in the current board step (no help mid-fight)
	std::unordered_set<std::uint64_t> foughtLastStep;       // ... in the step Tick just closed (checks in the tests)
	std::unordered_map<std::uint32_t, double> sosUntil;     // per place: no repeat survivors' SOS before (the battle SOS has its own)
	double lastSos{ -1e9 };                                  // world-wide gap between survivors' SOS lines (the board's SOS gap)
	struct Counters { std::uint64_t downed{ 0 }, ocsDowned{ 0 }, helped{ 0 }, finished{ 0 }, survivors{ 0 }, gotUp{ 0 }, yielded{ 0 }, surrendered{ 0 }; } counters;

	// The board hook (Board::wounds). The plugin loads wounded.ltx before the world is populated, so it calls this again once the
	// board is filled (first populate, after a load, after a new game); a_filled false never attaches. True when it attached
	// now; false when not filled, disabled, or already attached to this board (idempotent).
	bool Attach(Board& a_b, bool a_filled);
	bool AttachedTo(const Board& a_b) const;

	void Reset() { down.clear(); fights.clear(); yieldRolled.clear(); foughtThisStep.clear(); foughtLastStep.clear(); sosUntil.clear(); lastSos = -1e9; counters = {}; }

	// --- queries
	bool CanGoDown(const Board& a_b, const Group& a_g) const;   // human faction listed in [wounded] factions
	bool IsDown(std::uint64_t a_member) const { return down.count(a_member) != 0; }
	const Down* Find(std::uint64_t a_member) const;
	bool Stands(const Member& a_m) const { return a_m.alive() && !IsDown(a_m.id); }
	int StandingCount(const Group& a_g) const;
	int DownCount(const Group& a_g) const;
	// Wounded survivors: an offline squad with nobody standing that lies where it fell (does not move or fight).
	bool Survivors(const Group& a_g) const;
	bool SquadYielded(std::uint64_t a_squad) const;

	// --- going down and getting up
	// Online: a member's health just went under cfg.hp (the host calls it once per crossing). True: it goes down (rolled with chance).
	bool RollDown(Board& a_b, Group& a_g, Member& a_m, Rng& a_rng);
	void GoDown(Board& a_b, Group& a_g, Member& a_m, bool a_offline);
	// It gets up: health = a_healthFrac of its maximum (1: a potion; cfg.reviveHp: patched without one; < 0: health before yielding).
	void GetUp(Board& a_b, Group& a_g, Member& a_m, float a_healthFrac);
	// It dies while down (finished by an enemy squad, a_killer null: the player or nobody). Credits the kill like any death.
	void Kill(Board& a_b, Group& a_g, Member& a_m, Group* a_killer);
	void Forget(std::uint64_t a_member) { down.erase(a_member); yieldRolled.erase(a_member); }

	// --- offline (Board calls these when Board::wounds is set)
	// A hit in Round would kill a_m: true when it goes down instead (cfg.ocsChance, at down_hp).
	bool OfflineLethal(Board& a_b, Group& a_g, Member& a_m);
	// The fight is over for a_loser (nobody standing, or it fled): a_winner finishes its down members (monsters all of them,
	// people with their faction's finish chance). With nobody standing left alive, the loser becomes wounded survivors.
	// Returns how many it finished.
	int AfterBattle(Board& a_b, Group& a_loser, Group& a_winner, bool a_fled);
	// Once per board step: squads not fighting patch their down members up (cfg.reviveHp, helper rank), wounded survivors whose
	// time ran out get up and fall back, stale records go.
	void Tick(Board& a_b, double a_hours, std::vector<WoundEvent>* a_ev = nullptr);

	// --- on screen (the plugin)
	// Real-time timers of down members on screen: help lines (a_playerNear: per squad id, the player within call_radius), the
	// self-heal after cfg.selfHealSeconds (the host gives the potion or revive_hp and calls GetUp), yielded squads left alone.
	void UpdateOnline(Board& a_b, float a_dtSeconds, const std::unordered_set<std::uint64_t>& a_playerNear, std::vector<WoundEvent>& a_ev);
	// Who walks over to help a down member: the index (into a_g.members) of the nearest standing squadmate within help_radius;
	// a_dist[i] = member i's distance to the wounded (the host measures). -1 when the squad is fighting or nobody is close.
	int ChooseHelper(const Group& a_g, const std::vector<float>& a_dist, bool a_squadFighting) const;
	// The helper arrived: the wounded gets up (full with a potion, else revive_hp) and the helper earns rank.
	void Help(Board& a_b, Group& a_g, Member& a_wounded, Member& a_helper, bool a_potion, Rng& a_rng);
	float FinishDelay(Rng& a_rng) const { return a_rng.uniform(cfg.finishDelayMin, cfg.finishDelayMax); }
	bool MayFinish(float a_dist, bool a_finisherFighting) const { return a_dist <= cfg.finishRadius && !a_finisherFighting; }

	// --- surrender (squads fighting the player)
	// A down member of a squad hostile to the player, the player within yield_distance with a weapon drawn: rolled once per member.
	bool RollYield(Board& a_b, Group& a_g, Member& a_m, float a_playerDist, bool a_weaponDrawn, bool a_hostile, Rng& a_rng);
	// Losing (dead or down >= losing_share of the members it started with) or outnumbered (a_playerSide >= outnumber x standing).
	bool Losing(const Group& a_g) const;
	bool Outnumbered(const Group& a_g, int a_playerSide) const;
	// The squad check, rolled once per fight: on success every standing member kneels, yielded.
	bool CheckSurrender(Board& a_b, Group& a_g, int a_playerSide, Rng& a_rng, float a_chanceOverride = -1.f);
	void EndFight(std::uint64_t a_squad) { fights.erase(a_squad); }
	// Every yielded member of the squad gets up (health before yielding); no standing change (left alone, or spared).
	void Release(Board& a_b, Group& a_g);

	// --- the player's choice (the plugin moves the potion, gold and weapons; this applies state and standing).
	// a_hostile: the squad was hostile to the player (looting someone who attacked you costs nothing). Returns false when a_m is
	// not down (or not yielded for the surrender choices).
	bool Choose(Board& a_b, Standing& a_st, Group& a_g, Member& a_m, WoundChoice a_c, bool a_hostile, bool a_witnessed, double a_now);

	// --- co-save (record WOND, docs/WOUNDED.md s6)
	static constexpr std::uint32_t kSaveVersion = 1;
	void Save(std::vector<std::uint8_t>& a_out) const;
	// An unknown version or a broken blob leaves nobody down (false).
	bool Load(const std::uint8_t* a_data, std::size_t a_size, std::uint32_t a_version);
	// After a load: drop records whose member or squad no longer exists or is dead.
	void Rebind(Board& a_b);

private:
	Member* FindMember(Group& a_g, std::uint64_t a_id) const;
	void Patch(Board& a_b, Group& a_g, Member& a_m, Member* a_helper, Rng& a_rng);
};

}  // namespace ALife
