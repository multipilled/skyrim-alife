#include "ALife/Difficulty.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ALife {
namespace {
	constexpr const char* kIds[kDiffChannels] = { "population", "monsters", "strength", "lethality", "economy", "mercy", "tasks", "dragons" };
	constexpr const char* kNames[kDiffChannels] = { "Population", "Monsters", "Squad strength", "Combat lethality", "Economy", "Wounded and surrender", "Task difficulty", "Dragons" };
	constexpr const char* kRows[kGameDifficulties] = { "novice", "apprentice", "adept", "expert", "master", "legendary" };
	constexpr const char* kGame[kGameDifficulties] = { "Novice", "Apprentice", "Adept", "Expert", "Master", "Legendary" };
}

const char* DiffChannelId(DiffChannel a_c) { return int(a_c) < kDiffChannels ? kIds[int(a_c)] : "?"; }
const char* DiffChannelName(DiffChannel a_c) { return int(a_c) < kDiffChannels ? kNames[int(a_c)] : "?"; }
const char* GameDifficultyName(int a_d) { return a_d >= 0 && a_d < kGameDifficulties ? kGame[a_d] : "?"; }
bool DiffChannelFromId(const std::string& a_id, DiffChannel& a_out)
{
	for (int c = 0; c < kDiffChannels; ++c)
		if (a_id == kIds[c]) { a_out = DiffChannel(c); return true; }
	return false;
}

DifficultyConfig::DifficultyConfig()
{
	// docs/DIFFICULTY.md section 4: Novice .. Legendary per channel, Adept = 1
	const float def[kDiffChannels][kGameDifficulties] = {
		{ 0.8f, 0.9f, 1.f, 1.05f, 1.1f, 1.2f },     // population
		{ 0.75f, 0.9f, 1.f, 1.05f, 1.15f, 1.25f },  // monsters
		{ 0.7f, 0.85f, 1.f, 1.1f, 1.2f, 1.35f },    // strength
		{ 0.9f, 0.95f, 1.f, 1.f, 1.05f, 1.1f },     // lethality
		{ 1.5f, 1.25f, 1.f, 0.9f, 0.8f, 0.7f },     // economy
		{ 1.3f, 1.15f, 1.f, 0.95f, 0.9f, 0.8f },    // mercy
		{ 0.75f, 0.9f, 1.f, 1.1f, 1.2f, 1.3f },     // tasks
		{ 0.6f, 0.8f, 1.f, 1.1f, 1.25f, 1.4f },     // dragons
	};
	const float lo[kDiffChannels] = { 0.25f, 0.25f, 0.4f, 0.25f, 0.25f, 0.f, 0.25f, 0.f };
	const float hi[kDiffChannels] = { 2.f, 2.f, 2.5f, 3.f, 4.f, 3.f, 3.f, 3.f };
	for (int c = 0; c < kDiffChannels; ++c) {
		for (int d = 0; d < kGameDifficulties; ++d) rows[d][c] = def[c][d];
		limitMin[c] = lo[c];
		limitMax[c] = hi[c];
		sliderMin[c] = 0.5f;
		sliderMax[c] = 2.f;
	}
	sliderMax[int(DiffChannel::Population)] = sliderMax[int(DiffChannel::Monsters)] = 1.5f;   // the old density slider's range
}

void LoadDifficultyConfig(const Ltx& x, DifficultyConfig& c)
{
	c = DifficultyConfig{};
	for (int d = 0; d < kGameDifficulties; ++d)
		for (int k = 0; k < kDiffChannels; ++k) c.rows[d][k] = std::max(0.f, x.GetF(kRows[d], kIds[k], c.rows[d][k]));
	for (int k = 0; k < kDiffChannels; ++k) {
		auto lim = x.List("limits", kIds[k]);
		if (lim.size() == 2) {
			c.limitMin[k] = std::max(0.f, float(std::atof(lim[0].c_str())));
			c.limitMax[k] = std::max(c.limitMin[k], float(std::atof(lim[1].c_str())));
		}
		auto sl = x.List("sliders", kIds[k]);
		if (sl.size() == 2) {
			c.sliderMin[k] = std::max(0.f, float(std::atof(sl[0].c_str())));
			c.sliderMax[k] = std::max(c.sliderMin[k], float(std::atof(sl[1].c_str())));
		}
	}
	c.sizeExponent = std::clamp(x.GetF("derive", "size_exponent", c.sizeExponent), 0.f, 3.f);
	c.maxSquadSize = std::clamp(x.GetI("derive", "max_squad_size", c.maxSquadSize), 1, 15);
	c.levelsPerStrength = x.GetF("derive", "levels_per_strength", c.levelsPerStrength);
	c.levelMin = x.GetI("derive", "level_min", c.levelMin);
	c.levelMax = std::max(c.levelMin, x.GetI("derive", "level_max", c.levelMax));
	c.gearLevelsPerStrength = x.GetF("derive", "gear_levels_per_strength", c.gearLevelsPerStrength);
	c.gearMin = x.GetI("derive", "gear_min", c.gearMin);
	c.gearMax = std::max(c.gearMin, x.GetI("derive", "gear_max", c.gearMax));
}

float ClampSlider(const DifficultyConfig& a_cfg, DiffChannel a_c, float a_v)
{
	const int k = int(a_c);
	if (!(a_v == a_v)) a_v = 1.f;   // NaN from a bad ini / co-save
	return std::clamp(std::round(a_v * 10.f) / 10.f, a_cfg.sliderMin[k], a_cfg.sliderMax[k]);
}

DifficultyState ComputeDifficulty(const DifficultyConfig& a_cfg, int a_game, const DifficultyChoice& a_choice, int a_onlineActors)
{
	DifficultyState s;
	s.game = std::clamp(a_game, 0, kGameDifficulties - 1);
	s.followGame = a_choice.followGame;
	for (int k = 0; k < kDiffChannels; ++k) {
		const float g = a_choice.followGame ? a_cfg.rows[s.game][k] : 1.f;
		const float v = g * ClampSlider(a_cfg, DiffChannel(k), a_choice.slider[k]);
		s.mult[k] = std::clamp(v, a_cfg.limitMin[k], a_cfg.limitMax[k]);
		if (std::fabs(s.mult[k] - 1.f) < 1e-4f) s.mult[k] = 1.f;   // exactly neutral: no extra rolls (Adept plays as before T21)
	}
	const float st = s.mult[int(DiffChannel::Strength)];
	s.sizeMult = st == 1.f ? 1.f : std::pow(st, a_cfg.sizeExponent);
	s.sizeCap = std::max(1, std::min(a_cfg.maxSquadSize, a_onlineActors));
	s.levelOffset = std::clamp(int(std::lround(a_cfg.levelsPerStrength * (st - 1.f))), a_cfg.levelMin, a_cfg.levelMax);
	s.gearLevels = std::clamp(int(std::lround(a_cfg.gearLevelsPerStrength * (st - 1.f))), a_cfg.gearMin, a_cfg.gearMax);
	return s;
}

std::string DifficultyState::Describe() const
{
	std::string out = std::string("game=") + GameDifficultyName(game) + "(" + std::to_string(game) + ") follow=" + (followGame ? "1" : "0");
	char buf[64];
	for (int k = 0; k < kDiffChannels; ++k) {
		std::snprintf(buf, sizeof(buf), " %s=%.2f", kIds[k], mult[k]);
		out += buf;
	}
	std::snprintf(buf, sizeof(buf), " | size x%.2f cap %d, levels %+d, gear %+d", sizeMult, sizeCap, levelOffset, gearLevels);
	return out + buf;
}

}  // namespace ALife
