#pragma once
// Stashes (docs/STASHES.md, M5f): Anomaly's caches on ALife's world graph. Spots near places (half filled and unmarked at a
// new game), the kind / tier fill with a gold-value budget, stash notes carried by squad members and read off the dead, job
// rewards paid with a stash, marker slots and looting. Dependency-free: the same code runs in the plugin and in the headless
// tests (tests/stashes_test.cpp). The plugin places the containers, fills them, aims the ALifeStashes markers and saves.
//
// Ported (Anomaly 1.5.3): treasure_manager.script (on_game_load half filled, set_random_stash fill, get_random_stash reveal,
// actor_on_item_take_from_box looting), ui_pda_npc_tab.script (stash messages on the dead), xr_effects.reward_stash.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "ALife/Core.h"
#include "ALife/Ltx.h"

namespace ALife {

class World;

inline constexpr int kStashTiers = 5;

struct LootItem
{
	FormID form{ 0 };
	int value{ 1 };            // gold value of one (stashes.ltx; the plugin may replace it with the item's real value)
};

struct LootKind
{
	std::string name;
	float chance{ 50.f };      // percent per try
	int countLo{ 1 }, countHi{ 1 };
	std::array<std::vector<LootItem>, kStashTiers> tiers;
};

struct StashConfig
{
	bool enabled{ true };
	std::array<bool, int(Kind::COUNT)> kinds{};        // place kinds that get spots
	int spotsPerPlace{ 1 };
	float offsetMin{ 400.f }, offsetMax{ 1200.f };
	int maxSpots{ 900 };
	float hiddenShare{ 0.5f };
	float revealRadius{ 60000.f }, revealMin{ 1500.f };
	float placeRadius{ 5000.f };
	int markerSlots{ 8 };
	float noteChance{ 0.15f };
	float rewardChance{ 0.5f };
	bool replaceGold{ true };
	float budgetPerGold{ 1.f };                        // {economy+}
	float budgetMin{ 250.f }, budgetMax{ 500.f };      // {economy+}
	float perLevel{ 0.04f };                           // {economy+}
	std::array<int, kStashTiers> tierLevels{ 1, 8, 16, 26, 36 };
	std::array<float, kStashTiers> tierChance{ 1.f, 0.8f, 0.75f, 0.6f, 0.5f };
	std::array<float, kStashTiers> tierCount{ 1.f, 1.25f, 1.5f, 2.f, 3.f };
	FormID goldForm{ 0xF };
	std::vector<LootKind> loot;
	FormID containerDefault{ 0xF8478 };
	std::array<FormID, int(Kind::COUNT)> containers{};  // 0: the default
	FormID noteForm{ 0 };                               // the stash note (ALifeTalk.esp MISC; the plugin resolves it, tests set any id)
};

bool LoadStashConfig(const Ltx& a_ltx, StashConfig& a_out, std::string& a_err);

struct StashSpot
{
	enum class State : std::uint8_t { Empty = 0, Filled = 1 };
	std::uint32_t id{ 0 };
	std::uint32_t node{ 0 };            // the place it belongs to
	std::uint8_t ws{ 0 };
	float x{ 0 }, y{ 0 }, z{ 0 };
	FormID base{ 0 };                   // container base
	State state{ State::Empty };
	bool rolled{ false };               // its content exists (in items, or in the placed container)
	std::int8_t slot{ -1 };             // marker slot, -1 unmarked
	FormID ref{ 0 };                    // the placed container (raw in the co-save until resolved), 0 not placed
	std::vector<Item> items;            // rolled content waiting for the container (cleared once filled in)
	double markedAt{ 0 };
	bool Filled() const { return state == State::Filled; }
};

struct StashEvent
{
	enum class Kind : std::uint8_t { Marked, Unmarked, Looted };
	Kind kind{ Kind::Marked };
	std::uint32_t spot{ 0 };
	int slot{ -1 };
	std::string why;                    // "note", "reward", "debug"
};

class Stashes
{
public:
	StashConfig cfg;

	// --- state (saved in STSH)
	std::vector<StashSpot> spots;
	std::uint64_t found{ 0 };           // stashes looted by the player (Anomaly stashes_found)
	struct Counters { std::uint64_t notesMade{ 0 }, notesPassed{ 0 }, notesLost{ 0 }, notesRead{ 0 }, revealed{ 0 }, rewards{ 0 }, rolls{ 0 }, evicted{ 0 }; } counters;

	void Reset() { spots.clear(); found = 0; counters = {}; }
	bool Built() const { return !spots.empty(); }

	// Spots from the world graph (stable per a_seed); the hidden share starts filled and unmarked (content rolled on placement).
	void Build(const World& a_world, std::uint64_t a_seed);

	// Content worth a_budget gold: the kind / tier loop, the rest as coins. Returns the value put in.
	int Roll(StashSpot& a_s, float a_budget, int a_playerLevel, Rng& a_rng);
	float Budget(int a_playerLevel, float a_economy, Rng& a_rng) const;   // [loot] budget x level x Diff(Economy)
	int Value(const StashSpot& a_s) const;                                // gold value of the content the core holds

	// A stash near (a_x, a_y) in worldspace a_ws gets content (if empty) and a marker. Returns the spot id, -1 when none.
	int Reveal(float a_x, float a_y, int a_ws, int a_playerLevel, float a_economy, Rng& a_rng, std::vector<StashEvent>& a_ev,
	           const char* a_why, float a_minBudget = 0.f);

	// A member died: with [notes] chance it carried a stash note; offline it passes to a living member of the killer squad
	// (people only, one per member). a_online: the plugin puts it on the body instead. True when a note was made.
	bool OnKill(const Group& a_victimGroup, const Member& a_victim, Group* a_killer, bool a_online, Rng& a_rng);
	static bool CarriesNote(const Member& a_m, FormID a_note);
	// The player took a note (from a body, at (a_x, a_y)): a stash is revealed near there.
	int OnNoteRead(float a_x, float a_y, int a_ws, int a_playerLevel, float a_economy, Rng& a_rng, std::vector<StashEvent>& a_ev);

	// A job is done: with [reward] chance it pays with a stash near (a_x, a_y). With replace_gold the gold becomes 0 and the
	// stash is worth at least gold x budget_per_gold. True when a stash was revealed (a_gold may have changed).
	bool TaskReward(std::int32_t& a_gold, float a_x, float a_y, int a_ws, int a_playerLevel, float a_economy, Rng& a_rng,
	                std::vector<StashEvent>& a_ev);

	// The player took the first item from a placed stash: it is empty again, its marker goes.
	void OnLooted(std::uint32_t a_spot, std::vector<StashEvent>& a_ev);

	StashSpot* Find(std::uint32_t a_id);
	const StashSpot* Find(std::uint32_t a_id) const;
	StashSpot* FindByRef(FormID a_ref);
	int MarkedCount() const;
	// Spots within place_radius of (a_x, a_y) in a_ws without a container yet (the plugin places them).
	std::vector<StashSpot*> ToPlace(float a_x, float a_y, int a_ws);
	// The container is placed and filled: the core's copy of the content goes, the ref is kept.
	void Placed(StashSpot& a_s, FormID a_ref);

	// --- co-save (record STSH, docs/STASHES.md)
	static constexpr std::uint32_t kSaveVersion = 1;
	void Save(std::vector<std::uint8_t>& a_out) const;
	bool Load(const std::uint8_t* a_data, std::size_t a_size, std::uint32_t a_version);   // false: unknown version or broken (nothing loaded)

private:
	void Mark(StashSpot& a_s, double a_now, std::vector<StashEvent>& a_ev, const char* a_why);
	double m_clock{ 0 };   // order of marking (the oldest gives its slot up)
};

}  // namespace ALife
