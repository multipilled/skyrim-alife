#pragma once
// Tasks (docs/TASKS.md): Anomaly's generic jobs from squad leaders - bounty, assault, delivery, fetch (M5a); escort, defend,
// rescue, recover and the top-10 hunt with its rankings (M5e, section 8).
// Dependency-free: offers, target choice, validity, completion, expiry and rewards run the same in the plugin and in the
// headless tests (tests/tasks_test.cpp). The plugin turns the events into journal objectives, markers, gold and goodwill.
//
// Ported (Anomaly 1.5.3): axr_task_manager.script (givers, 30% no job, one sticky job per squad), task_manager.script
// (repeat_timeout, complete / fail), tasks_bounty / tasks_assault / tasks_delivery / tasks_fetch.script, tm_dynamic.ltx;
// tasks_guide / tasks_defense / tasks_agent_rescue / tasks_recover_item_on_corpse / tasks_top_10.script, pda.script rankings.
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ALife/Core.h"
#include "ALife/Ltx.h"
#include "ALife/Party.h"

namespace ALife {

class Board;
class World;

enum class TaskType : std::uint8_t { None = 0, Bounty, Assault, Delivery, Fetch, Escort, Defend, Rescue, Recover, Hunt, COUNT };
inline constexpr int kTaskTypes = int(TaskType::COUNT);
const char* TaskTypeId(TaskType a_t);                      // "bounty"
TaskType TaskTypeFromId(const std::string& a_id);

// Open: the work is not done yet. Ready: done, go back to the giver (escort: the squad you brought pays where it stands).
// Delivery has no Ready: the recipient pays.
enum class TaskStage : std::uint8_t { Open = 0, Ready, Done, Failed };

// What ALifeTaskState tells the dialogue about the squad under the crosshair.
enum TaskState : int { kTaskNone = 0, kTaskWorking = 1, kTaskReady = 2, kTaskDeliverHere = 3 };
// Which talk modes (ALifeTalkMode, src/Talk.h) carry the job topics: a squad that greets the player (offers, hand-in, abandon) and a
// job squad travelling with the player (an escort or a freed captive hands in where it stands). The dialogue publishes the job state
// only in these modes; everywhere else ALifeTaskState stays 0.
inline constexpr int kTalkModeGreet = 3, kTalkModeCompanion = 4;
inline constexpr bool JobTopicsInMode(int a_mode) { return a_mode == kTalkModeGreet || a_mode == kTalkModeCompanion; }

struct FetchItem
{
	std::string spec;          // "<hex>" (Skyrim.esm) or "<plugin>|<local hex>"
	FormID form{ 0 };          // resolved by the host (the harness keeps the hex)
	std::string name;          // display name for the journal ("Wolf Pelt")
	int lo{ 1 }, hi{ 3 };      // how many are asked for
	int minLevel{ 0 };         // player level from which this item is asked for (Anomaly's rank tiers)
	int value{ 5 };            // gold value of one, from the config (the plugin replaces it with the item's real value)
};

// Every balance number of the jobs (docs/TASKS.md section 8, "Difficulty channels"). Tags name the difficulty channel each
// scales with (T21, docs/DIFFICULTY.md): [enemy] enemy strength, [task] task difficulty, [economy] economy; [-] not scaled
// (geometry, scoring, timing of the offer system). The logic never reads these from TaskConfig directly: it reads Tasks::Eff(),
// the one place the TaskScale multipliers are applied.
struct TaskNumbers
{
	int maxActive{ 4 };                 // [-] journal slots (Anomaly max_tasks)
	float noJobChance{ 0.3f };          // [-] axr_task_manager.script:852-857
	double offerHours{ 6.0 };           // [-] a squad's sticky offer lasts this long
	double repeatHours{ 6.0 };          // [-] repeat_timeout 21600 s: nothing new from a squad after its job ended
	double expireDays{ 0.0 };           // [task] 0 = jobs never expire (Anomaly); harder = less time
	int maxLevelOver{ 6 };              // [enemy] bounty / assault / defend / rescue targets at most this many levels above the player
	float bountyRange{ 90000.f };       // [-] giver -> target, world units
	float assaultRange{ 90000.f };      // [-]
	float deliveryMin{ 40000.f }, deliveryMax{ 160000.f };   // [-]
	double parcelGraceHours{ 0.5 };     // [task] game hours without the parcel before the delivery fails; harder = less
	float goodwillDone{ 50.f }, goodwillFail{ 25.f };   // [economy] reward / [task] penalty (complete_task_inc_goodwill(50), fail_task_dec_goodwill(25))
	float memoryDone{ 100.f }, memoryFail{ 50.f };      // [economy] / [task] the giver squad's own memory of the player
	float goldMult{ 1.f };              // [economy]
	float bountyBase{ 150.f }, bountyPerLevel{ 15.f };          // [economy]
	float assaultPerSquad{ 120.f }, assaultPerLevel{ 10.f };    // [economy]
	float deliveryBase{ 80.f }, deliveryPerKm{ 25.f };          // [economy] 1 km = 70000 units
	float fetchMult{ 1.3f };            // [economy]
	int goldMin{ 50 };                  // [economy]
	// --- M5e (docs/TASKS.md section 8)
	float escortMin{ 40000.f }, escortMax{ 160000.f };   // [-] giver -> destination
	float escortArrive{ 3000.f };                         // [-] the squad is this close to the destination: arrived
	double escortHoursPerKm{ 3.0 };                       // [task] arriving later than this x km after acceptance halves the pay (tasks_guide.script); harder = less
	float escortBase{ 100.f }, escortPerKm{ 40.f };       // [economy]
	float escortLatePay{ 0.5f };                          // [-] share of the pay when the escort arrives late (Anomaly: half)
	float defendSpawnMin{ 12000.f }, defendSpawnMax{ 40000.f };   // [-] the defended place -> where the waves are made
	int defendWaves{ 3 };                                 // [task] attack waves
	float defendWaveSize{ 1.f };                          // [enemy] members of a wave (and of a recover guard) x the squad description's roll
	int defendWaveMax{ 5 };                               // [-] at most this many in one wave or guard: the on-screen budget (15 actors in 3 groups)
	float defendPerWave{ 120.f }, defendPerLevel{ 10.f }; // [economy]
	float rescueRange{ 90000.f };                         // [-]
	float rescueArrive{ 2500.f };                         // [-] the freed captive is this close to the giver: hand-in
	float rescueBase{ 150.f }, rescuePerLevel{ 10.f };    // [economy]
	float recoverMin{ 30000.f }, recoverMax{ 120000.f };  // [-]
	float recoverSpawn{ 8000.f };                         // [-] the plugin lays out the body once the player is this close
	float recoverGuardChance{ 0.6f };                     // [task] an unguarded place gets a monster squad of the nearest kind
	float recoverBase{ 120.f }, recoverPerKm{ 30.f };     // [economy]
	int huntMarks{ 10 };                                  // [task] marks on the list (Anomaly's top 10; 3..10)
	int huntMin{ 3 };                                     // [task] marks the giver's worldspace must hold for a hunt
	float huntBase{ 300.f }, huntPerLevel{ 8.f };         // [economy]
	float rankKillPoints{ 5.f }, rankVeteran{ 50.f };     // [-] rankings: fighting value + per kill + veterans (pda.script rankings)
};

// The difficulty multipliers (T21 sets them; 1 = the config as written). Applied only in Tasks::Eff().
struct TaskScale
{
	float enemy{ 1.f };        // enemy strength
	float task{ 1.f };         // task difficulty
	float economy{ 1.f };      // economy (pay and goodwill earned)
};
// The config as written x the multipliers (the one accessor's rule, public for the tests).
TaskNumbers Scaled(const TaskNumbers& a_base, const TaskScale& a_s);
struct Config;
// The multipliers from T21's difficulty state (docs/DIFFICULTY.md): enemy and task = Diff(Tasks), economy = Diff(Economy). The host sets
// Tasks::scale from it before each call; at Adept (all 1.0) the jobs are exactly the config as written.
TaskScale TaskScaleFrom(const Config& a_cfg);

struct TaskConfig : TaskNumbers
{
	std::string parcelSpec;              // the sealed parcel (ALifeTalk.esp MISC)
	FormID parcel{ 0 };
	std::string satchelSpec;             // the satchel on the recover body (ALifeTalk.esp MISC)
	FormID satchel{ 0 };
	std::array<std::string, kFactionCount> corpse;        // [recover] corpse_<faction>: leveled actor list for the body (the plugin resolves it)
	std::array<std::array<float, kTaskTypes>, kFactionCount> weight{};   // [giver faction][type]
	std::array<std::vector<FetchItem>, kFactionCount> fetch;             // [giver faction]
};

// Loads tasks.ltx. Form specs stay unresolved (form = the hex for Skyrim.esm specs); the plugin resolves them after.
bool LoadTaskConfig(const Ltx& a_ltx, TaskConfig& a_out, std::string& a_err);

struct Task
{
	std::uint32_t id{ 0 };
	TaskType type{ TaskType::None };
	TaskStage stage{ TaskStage::Open };
	std::int32_t slot{ -1 };               // journal slot 0..maxActive-1, -1 while only offered
	std::uint64_t giver{ 0 };              // giver squad (the job lives while any member does); escort: the squad you take along
	Faction giverFaction{ Faction::Wanderer };
	std::string giverName;                 // leader's name when offered
	std::uint64_t target{ 0 };             // bounty: target squad; delivery: recipient squad; hunt: the current mark's squad
	std::uint64_t targetMember{ 0 };       // bounty, hunt: the member to kill
	Faction targetFaction{ Faction::Bandit };
	std::string targetName;
	std::vector<std::uint64_t> pinned;     // assault: squads holding the place at acceptance; rescue: the captors; recover: the guard it made
	std::uint32_t node{ 0 };               // assault, defend, rescue, recover: the place; others: where the target was when offered
	std::string place;                     // its name
	FormID item{ 0 };                      // fetch item / parcel / satchel
	std::string itemName;                  // ... its name; rescue: the captive's name
	std::int32_t count{ 0 };               // fetch: how many; defend: waves in all
	std::int32_t gold{ 0 };
	double offeredAt{ 0 }, acceptedAt{ 0 };
	double parcelMissingSince{ -1 };       // delivery: game hours when the parcel left the player's pack, -1 while held
	// --- TASK v3 (M5e); empty on jobs loaded from v1 / v2
	std::uint32_t dest{ 0 };               // escort: the destination; defend: where the waves are made; rescue: where a captive with no room in the party walks
	struct Mark { std::uint64_t squad{ 0 }, member{ 0 }; };
	std::vector<Mark> marks;               // hunt: the marks, best-ranked first (the hunt starts from the last)
	std::int32_t step{ 0 };                // defend: waves made; hunt: marks passed; escort: 1 = arrived late (half pay); rescue: 1 = the captive is in the party
	std::uint64_t extra{ 0 };              // defend: the current wave; rescue: the captive; recover: the guard squad it made
	FormID corpse{ 0 };                    // recover: the body the plugin laid out (raw in the co-save, 0 until it exists)
	double deadline{ 0 };                  // escort: arrival due (game hours); 0 = none
	std::string squadDesc;                 // defend: the attackers' squad section; recover: the guard's (empty: none)
	bool atGiver{ false };                 // rescue: the freed captive stands by the giver (runtime only, set by Update)
};

// What the plugin must do after a change (journal, gold, goodwill, items).
struct TaskEvent
{
	enum class Kind : std::uint8_t { Accepted, Ready, Reopened, Done, Failed, Progress };   // Progress: the next wave / mark (new objective text, target)
	Kind kind{ Kind::Accepted };
	Task task;                              // copy at the time of the event
	std::int32_t gold{ 0 };                 // pay the player
	float goodwill{ 0 };                    // goodwill delta with task.giverFaction
	float memory{ 0 };                      // memory delta of the giver squad
	FormID takeItem{ 0 };                   // remove from the player's pack
	std::int32_t takeCount{ 0 };
	std::string why;
};

// What the tasks need to know about the player.
struct TaskHost
{
	virtual ~TaskHost() = default;
	virtual int PlayerLevel() const { return 10; }
	virtual int PlayerItemCount(FormID /*a_item*/) const { return 0; }
	virtual int ItemValue(FormID /*a_item*/, int a_configValue) const { return a_configValue; }
	// may the player walk up to this squad (it regards them Neutral or better)? Delivery recipients must be.
	virtual bool Approachable(const Group& /*a_g*/) const { return true; }
};

// One line of the rankings (pda.script:457-635): a living person on the board, by score.
struct RankEntry
{
	std::uint64_t squad{ 0 }, member{ 0 };
	Faction faction{ Faction::Bandit };
	std::string name;
	int level{ 0 };
	float score{ 0 };
	int ws{ 0 };
};

class Tasks
{
public:
	TaskConfig cfg;                     // as loaded from tasks.ltx
	TaskScale scale;                    // difficulty multipliers (T21); not saved, the host sets them
	// The balance numbers in play: cfg x scale. Every rule reads its numbers here (one read path for difficulty scaling).
	TaskNumbers Eff() const { return Scaled(cfg, scale); }
	// Escort and the freed captive travel with the player through the party (docs/COMPANIONS.md). Null: no escort or rescue
	// offers. Join / Dismiss events of job companions are queued here for the host, which drains them after each call.
	Party* party{ nullptr };
	std::vector<PartyEvent> partyEvents;

	// --- state (saved)
	struct Offer { Task task; double until{ 0 }; };   // task.type None = "no work" (sticky too)
	std::vector<Task> active;                           // Open / Ready, one per slot
	std::unordered_map<std::uint64_t, Offer> offers;    // by giver squad
	std::unordered_map<std::uint64_t, double> cooldown; // giver squad -> no offer before this game hour
	std::uint32_t nextId{ 1 };

	void Reset() { active.clear(); offers.clear(); cooldown.clear(); nextId = 1; }

	// The squad's job offer right now (sticky; rolled the first time it is asked). None when it has no work, when the player
	// already holds a job from it, or when every slot is taken.
	TaskType OfferOf(Board& a_b, const World& a_w, const Group& a_giver, double a_now, const TaskHost& a_h, Rng& a_rng);
	const Task* Offered(std::uint64_t a_squad) const;
	// Debug: replace the squad's offer with one of this type (no-job roll, cooldown and weights ignored).
	TaskType ForceOffer(Board& a_b, const World& a_w, const Group& a_giver, TaskType a_type, double a_now, const TaskHost& a_h, Rng& a_rng);

	// The player said "I'll do it": the offer becomes an active job in a free slot (re-checked first; escort: the squad joins
	// the party; defend: the first wave; rescue: the captive; recover: the guard). Nullptr when it is gone.
	Task* Accept(Board& a_b, std::uint64_t a_squad, double a_now, const TaskHost& a_h, std::vector<TaskEvent>& a_ev);

	// Advance every active job (targets dead, places cleared, items held, givers wiped, waves, marks, arrivals, expiry). Also
	// refreshes the board's pinned / forced / held sets. Returns what changed.
	void Update(Board& a_b, double a_now, const TaskHost& a_h, std::vector<TaskEvent>& a_ev);

	// Dialogue results (the plugin calls them after the menu closed).
	bool TurnIn(Board& a_b, std::uint64_t a_giver, double a_now, const TaskHost& a_h, std::vector<TaskEvent>& a_ev);   // "It's done."
	bool Deliver(std::uint64_t a_recipient, double a_now, const TaskHost& a_h, std::vector<TaskEvent>& a_ev);            // "I have a delivery for you."
	bool Abandon(Board& a_b, std::uint64_t a_giver, double a_now, std::vector<TaskEvent>& a_ev);                          // "I can't finish that job."

	// What ALifeTaskState should say for this squad.
	int StateFor(std::uint64_t a_squad, const TaskHost& a_h) const;

	Task* ByGiver(std::uint64_t a_squad);
	Task* BySlot(int a_slot);
	bool IsPinned(std::uint64_t a_squad) const { return m_pinned.count(a_squad) != 0; }
	bool IsTarget(std::uint64_t a_squad) const { return Targeted(a_squad); }   // a target, recipient, holder, captor, wave, captive, guard or mark of an open job

	// After a load: the board's pinned / forced / held sets from the open jobs (the sets are not saved).
	void Rebind(Board& a_b) { RefreshPins(a_b); }

	// The rankings: every living person on the board not travelling with the player nor held captive, best first.
	// a_ws >= 0: that worldspace only.
	std::vector<RankEntry> Rankings(const Board& a_b, int a_ws = -1) const;
	// The hunt's current mark (marks[size-1-step]); null when every mark is passed.
	static const Task::Mark* CurrentMark(const Task& a_t);

	// Journal text: the work (a_return false) or "go back to the giver".
	static std::string Objective(const Task& a_t, bool a_return);

	// Co-save blob (version kTaskSaveVersion). Item FormIDs are raw: the plugin resolves them after Load.
	static constexpr std::uint32_t kSaveVersion = 3;   // 3: M5e fields; 2: Solstheim factions (1 still loads, monster factions shifted)
	void Save(std::vector<std::uint8_t>& a_out, std::uint32_t a_version = kSaveVersion) const;   // a_version < 3: the v2 layout (tests of older saves)
	bool Load(const std::uint8_t* a_data, std::size_t a_size, std::uint32_t a_version, std::size_t* a_used = nullptr);

private:
	bool FindBounty(Board& a_b, const World& a_w, const Group& a_giver, const TaskHost& a_h, Rng& a_rng, Task& a_t) const;
	bool FindAssault(Board& a_b, const World& a_w, const Group& a_giver, const TaskHost& a_h, Rng& a_rng, Task& a_t) const;
	bool FindDelivery(Board& a_b, const World& a_w, const Group& a_giver, const TaskHost& a_h, Rng& a_rng, Task& a_t) const;
	bool FindFetch(const Group& a_giver, const TaskHost& a_h, Rng& a_rng, Task& a_t) const;
	bool FindEscort(Board& a_b, const World& a_w, const Group& a_giver, Rng& a_rng, Task& a_t) const;
	bool FindDefend(Board& a_b, const World& a_w, const Group& a_giver, const TaskHost& a_h, Rng& a_rng, Task& a_t) const;
	bool FindRescue(Board& a_b, const World& a_w, const Group& a_giver, const TaskHost& a_h, Rng& a_rng, Task& a_t) const;
	bool FindRecover(Board& a_b, const World& a_w, const Group& a_giver, Rng& a_rng, Task& a_t) const;
	bool FindHunt(Board& a_b, const World& a_w, const Group& a_giver, Task& a_t) const;
	bool Setup(Board& a_b, Task& a_t, double a_now);                         // on accept: party, captive, first wave, guard
	bool SpawnWave(Board& a_b, Task& a_t);                                   // defend: the next wave (counts it even when none could be made)
	void Release(Board& a_b, const Task& a_t, PartyEvent::Kind a_why);       // a job ends: its companions leave the party
	bool StillValid(Board& a_b, const Task& a_t) const;
	void Finish(std::size_t a_index, TaskStage a_stage, double a_now, float a_goodwill, float a_memory, const std::string& a_why,
	            std::vector<TaskEvent>& a_ev, std::int32_t a_gold = 0, FormID a_take = 0, std::int32_t a_takeCount = 0);
	bool Targeted(std::uint64_t a_squad) const;
	int FreeSlot() const;
	void RefreshPins(Board& a_b);

	std::unordered_set<std::uint64_t> m_pinned;
};

}  // namespace ALife
