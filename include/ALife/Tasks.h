#pragma once
// Tasks (docs/TASKS.md): Anomaly's generic jobs from squad leaders - bounty, assault, delivery, fetch.
// Dependency-free: offers, target choice, validity, completion, expiry and rewards run the same in the plugin and in the
// headless tests (tests/tasks_test.cpp). The plugin turns the events into journal objectives, markers, gold and goodwill.
//
// Ported (Anomaly 1.5.3): axr_task_manager.script (givers, 30% no job, one sticky job per squad), task_manager.script
// (repeat_timeout, complete / fail), tasks_bounty / tasks_assault / tasks_delivery / tasks_fetch.script, tm_dynamic.ltx.
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
class World;

enum class TaskType : std::uint8_t { None = 0, Bounty, Assault, Delivery, Fetch, COUNT };
inline constexpr int kTaskTypes = int(TaskType::COUNT);
const char* TaskTypeId(TaskType a_t);                      // "bounty"
TaskType TaskTypeFromId(const std::string& a_id);

// Open: the work is not done yet. Ready: done, go back to the giver. Delivery has no Ready: the recipient pays.
enum class TaskStage : std::uint8_t { Open = 0, Ready, Done, Failed };

// What ALifeTaskState tells the dialogue about the squad under the crosshair.
enum TaskState : int { kTaskNone = 0, kTaskWorking = 1, kTaskReady = 2, kTaskDeliverHere = 3 };

struct FetchItem
{
	std::string spec;          // "<hex>" (Skyrim.esm) or "<plugin>|<local hex>"
	FormID form{ 0 };          // resolved by the host (the harness keeps the hex)
	std::string name;          // display name for the journal ("Wolf Pelt")
	int lo{ 1 }, hi{ 3 };      // how many are asked for
	int minLevel{ 0 };         // player level from which this item is asked for (Anomaly's rank tiers)
	int value{ 5 };            // gold value of one, from the config (the plugin replaces it with the item's real value)
};

struct TaskConfig
{
	int maxActive{ 4 };                 // journal slots (Anomaly max_tasks)
	float noJobChance{ 0.3f };          // axr_task_manager.script:852-857
	double offerHours{ 6.0 };           // a squad's sticky offer lasts this long
	double repeatHours{ 6.0 };          // repeat_timeout 21600 s: nothing new from a squad after its job ended
	double expireDays{ 0.0 };           // 0 = jobs never expire (Anomaly)
	int maxLevelOver{ 6 };              // bounty / assault targets at most this many levels above the player
	float bountyRange{ 90000.f };       // giver -> target, world units
	float assaultRange{ 90000.f };
	float deliveryMin{ 40000.f }, deliveryMax{ 160000.f };
	double parcelGraceHours{ 0.5 };     // game hours without the parcel before the delivery fails
	float goodwillDone{ 50.f }, goodwillFail{ 25.f };   // complete_task_inc_goodwill(50), fail_task_dec_goodwill(25)
	float memoryDone{ 100.f }, memoryFail{ 50.f };      // the giver squad's own memory of the player
	float goldMult{ 1.f };
	float bountyBase{ 150.f }, bountyPerLevel{ 15.f };
	float assaultPerSquad{ 120.f }, assaultPerLevel{ 10.f };
	float deliveryBase{ 80.f }, deliveryPerKm{ 25.f };  // 1 km = 70000 units
	float fetchMult{ 1.3f };
	int goldMin{ 50 };
	std::string parcelSpec;              // the sealed parcel (ALifeTalk.esp MISC)
	FormID parcel{ 0 };
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
	std::uint64_t giver{ 0 };              // giver squad (the job lives while any member does)
	Faction giverFaction{ Faction::Wanderer };
	std::string giverName;                 // leader's name when offered
	std::uint64_t target{ 0 };             // bounty: target squad; delivery: recipient squad
	std::uint64_t targetMember{ 0 };       // bounty: the member to kill
	Faction targetFaction{ Faction::Bandit };
	std::string targetName;
	std::vector<std::uint64_t> pinned;     // assault: squads holding the place at acceptance
	std::uint32_t node{ 0 };               // assault: the place; others: where the target was when offered
	std::string place;                     // its name
	FormID item{ 0 };                      // fetch item / parcel
	std::string itemName;
	std::int32_t count{ 0 };
	std::int32_t gold{ 0 };
	double offeredAt{ 0 }, acceptedAt{ 0 };
	double parcelMissingSince{ -1 };       // delivery: game hours when the parcel left the player's pack, -1 while held
};

// What the plugin must do after a change (journal, gold, goodwill, items).
struct TaskEvent
{
	enum class Kind : std::uint8_t { Accepted, Ready, Reopened, Done, Failed };
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

class Tasks
{
public:
	TaskConfig cfg;

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

	// The player said "I'll do it": the offer becomes an active job in a free slot (re-checked first). Nullptr when it is gone.
	Task* Accept(Board& a_b, std::uint64_t a_squad, double a_now, const TaskHost& a_h, std::vector<TaskEvent>& a_ev);

	// Advance every active job (targets dead, places cleared, items held, givers wiped, expiry). Also refreshes the
	// board's pinned set (assault). Returns what changed.
	void Update(Board& a_b, double a_now, const TaskHost& a_h, std::vector<TaskEvent>& a_ev);

	// Dialogue results (the plugin calls them after the menu closed).
	bool TurnIn(std::uint64_t a_giver, double a_now, const TaskHost& a_h, std::vector<TaskEvent>& a_ev);     // "It's done."
	bool Deliver(std::uint64_t a_recipient, double a_now, const TaskHost& a_h, std::vector<TaskEvent>& a_ev);// "I have a delivery for you."
	bool Abandon(std::uint64_t a_giver, double a_now, std::vector<TaskEvent>& a_ev);                         // "I can't finish that job."

	// What ALifeTaskState should say for this squad.
	int StateFor(std::uint64_t a_squad, const TaskHost& a_h) const;

	Task* ByGiver(std::uint64_t a_squad);
	Task* BySlot(int a_slot);
	bool IsPinned(std::uint64_t a_squad) const { return m_pinned.count(a_squad) != 0; }

	// Journal text: the work (a_return false) or "go back to the giver".
	static std::string Objective(const Task& a_t, bool a_return);

	// Co-save blob (version kTaskSaveVersion). Item FormIDs are raw: the plugin resolves them after Load.
	static constexpr std::uint32_t kSaveVersion = 1;
	void Save(std::vector<std::uint8_t>& a_out) const;
	bool Load(const std::uint8_t* a_data, std::size_t a_size, std::uint32_t a_version, std::size_t* a_used = nullptr);

private:
	bool FindBounty(Board& a_b, const World& a_w, const Group& a_giver, const TaskHost& a_h, Rng& a_rng, Task& a_t) const;
	bool FindAssault(Board& a_b, const World& a_w, const Group& a_giver, const TaskHost& a_h, Rng& a_rng, Task& a_t) const;
	bool FindDelivery(Board& a_b, const World& a_w, const Group& a_giver, const TaskHost& a_h, Rng& a_rng, Task& a_t) const;
	bool FindFetch(const Group& a_giver, const TaskHost& a_h, Rng& a_rng, Task& a_t) const;
	bool StillValid(Board& a_b, const Task& a_t) const;
	void Finish(std::size_t a_index, TaskStage a_stage, double a_now, float a_goodwill, float a_memory, const std::string& a_why,
	            std::vector<TaskEvent>& a_ev, std::int32_t a_gold = 0, FormID a_take = 0, std::int32_t a_takeCount = 0);
	bool Targeted(std::uint64_t a_squad) const;
	int FreeSlot() const;
	void RefreshPins(Board& a_b);

	std::unordered_set<std::uint64_t> m_pinned;
};

}  // namespace ALife
