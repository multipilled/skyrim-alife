#pragma once
// Difficulty scaling (docs/DIFFICULTY.md, T21): one multiplier per channel = the difficulty.ltx row of the game difficulty
// x the channel's MCM slider, clamped. Dependency-free: the plugin and the headless harness run the same ApplyDifficulty.
#include <array>
#include <cstdint>
#include <string>

#include "ALife/Ltx.h"

namespace ALife {

enum class DiffChannel : std::uint8_t { Population = 0, Monsters, Strength, Lethality, Economy, Mercy, Tasks, Dragons, COUNT };
inline constexpr int kDiffChannels = int(DiffChannel::COUNT);
inline constexpr int kGameDifficulties = 6;   // Novice .. Legendary (iDifficulty 0-5)
inline constexpr int kAdept = 2;              // the game's default: every row is 1.0 there

const char* DiffChannelId(DiffChannel a_c);       // "population" ... (difficulty.ltx keys, harness --diff names)
const char* DiffChannelName(DiffChannel a_c);     // "Population" ... (log)
const char* GameDifficultyName(int a_d);          // "Novice" ... "Legendary", "?" out of range
bool DiffChannelFromId(const std::string& a_id, DiffChannel& a_out);

// config/difficulty.ltx
struct DifficultyConfig
{
	std::array<std::array<float, kDiffChannels>, kGameDifficulties> rows{};   // [difficulty][channel]
	std::array<float, kDiffChannels> limitMin{}, limitMax{};
	std::array<float, kDiffChannels> sliderMin{}, sliderMax{};               // MCM / ini range per channel
	// [derive]
	float sizeExponent{ 1.f };
	int maxSquadSize{ 7 };
	float levelsPerStrength{ 10.f };
	int levelMin{ -8 }, levelMax{ 10 };
	float gearLevelsPerStrength{ 6.f };
	int gearMin{ -6 }, gearMax{ 10 };
	DifficultyConfig();   // compiled-in defaults (docs/DIFFICULTY.md section 4)
};
void LoadDifficultyConfig(const Ltx& a_ltx, DifficultyConfig& a_out);

// What the player chose (MCM / ini / co-save).
struct DifficultyChoice
{
	bool followGame{ true };
	std::array<float, kDiffChannels> slider{ 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
	bool operator==(const DifficultyChoice&) const = default;
};

// The one read path: every system reads these (Config::diff), never the rows or the sliders.
struct DifficultyState
{
	int game{ kAdept };                                  // game difficulty the values were made for
	bool followGame{ true };
	std::array<float, kDiffChannels> mult{ 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
	// derived from Strength
	float sizeMult{ 1.f };      // squad size multiplier (s ^ size_exponent)
	int sizeCap{ 7 };           // never more members than this (max_squad_size, on-screen budget)
	int levelOffset{ 0 };       // added to every new member's level and to the level target the world catches up to
	int gearLevels{ 0 };        // leveled gear is resolved this many levels higher
	float Get(DiffChannel a_c) const { return mult[int(a_c)]; }
	bool Neutral(DiffChannel a_c) const { return mult[int(a_c)] == 1.f; }
	std::string Describe() const;   // one log line
};

// Clamp a slider to its channel range, rounded to 0.1 (what the MCM shows).
float ClampSlider(const DifficultyConfig& a_cfg, DiffChannel a_c, float a_v);
// G(c, game) x S(c), clamped, plus the derived strength values. a_onlineActors: the per-squad on-screen budget (size cap).
DifficultyState ComputeDifficulty(const DifficultyConfig& a_cfg, int a_game, const DifficultyChoice& a_choice, int a_onlineActors);

}  // namespace ALife
