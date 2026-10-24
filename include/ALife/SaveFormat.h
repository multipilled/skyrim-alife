#pragma once
// Co-save SIM2 versions and the faction-index migration (docs/SOLSTHEIM.md). Engine-free: Save.cpp and the headless tests
// (standing_test) share it, so the shift the tests check is the one the plugin runs.
#include <cstdint>

#include "ALife/Core.h"
#include "ALife/Standing.h"

namespace ALife::SaveFormat {
inline constexpr std::uint32_t kVer = 208;               // 208: Skaal, Redoran, Reaver before the monsters (per-faction table written with its length)
inline constexpr std::uint32_t kVerPreSolstheim = 207;   // 207: the per-faction table is written with its length (M4b: 28 factions, monsters from 15)
inline constexpr std::uint32_t kVerMutants = 206;        // 206: six monster factions (21 per-faction entries), creatures on screen at save time
inline constexpr int kFactionsMutants = 21;
inline constexpr std::uint32_t kVerPreMutants = 205;
inline constexpr int kFactionsPreMutants = 15;

// 208, 207, 206 and 205 load; older layouts are not migrated (the world is populated afresh)
inline constexpr bool Loads(std::uint32_t a_ver) { return a_ver == kVer || a_ver == kVerPreSolstheim || a_ver == kVerMutants || a_ver == kVerPreMutants; }

inline constexpr int kFactionsBeforeSolstheim = 15;      // factions before Skaal, Redoran, Reaver (the monsters started here before 208)
inline constexpr int kSolstheimFactions = 3;             // Skaal, Redoran, Reaver

// The one copy of the shift rule. Index today of faction index a_f written by save version a_ver: before 208 the monsters sat from 15,
// so they move past the three Solstheim factions. An int, so a bad index stays out of range instead of wrapping.
inline constexpr int FactionIndexOf(std::uint32_t a_ver, int a_f)
{
	return a_ver < kVer && a_f >= kFactionsBeforeSolstheim && a_f != kNoFaction ? a_f + kSolstheimFactions : a_f;
}
static_assert(kFactionsBeforeSolstheim + kSolstheimFactions == int(Faction::Wildlife), "the first monster faction follows the Solstheim three");
// A faction byte (squad, member, place owner, rumour, task giver/target) as written by save version a_ver; kNoFaction stays kNoFaction.
inline Faction FactionOf(std::uint32_t a_ver, std::uint8_t a_f) { return Faction(FactionIndexOf(a_ver, a_f)); }

// Goodwill table, notoriety and rumours, in save order. R: a reader with template v<T>() and an ok flag.
// 208 and 207 write the table's length; 206 wrote 21 entries, 205 wrote 15 (factions added since start at 0).
template <class R>
void ReadStanding(R& r, std::uint32_t a_ver, StandingState& st)
{
	st = StandingState{};
	const int nf = a_ver >= kVerPreSolstheim ? int(r.template v<std::uint8_t>()) : a_ver >= kVerMutants ? kFactionsMutants : kFactionsPreMutants;
	for (int f = 0; f < nf; ++f) {
		const float v = r.template v<float>();
		if (const int to = FactionIndexOf(a_ver, f); to < kFactionCount) st.goodwill[to] = v;
	}
	st.notoriety = r.template v<float>();
	for (auto n = r.template v<std::uint32_t>(); r.ok && n; --n) {
		Rumor ru;
		ru.faction = FactionOf(a_ver, r.template v<std::uint8_t>());
		ru.delta = r.template v<float>();
		ru.dueHours = r.template v<double>();
		st.rumors.push_back(ru);
	}
}
// The live relation matrix of the NEWS record, written with its faction count nf. Since 208 factions are only appended (31 = the
// 208 table, 32 = + Dragon, docs/DRAGONS.md), so a smaller table fills the top-left and the newer rows / columns keep what a_rel
// holds (the static relations); a table from before 208 (another order) or a bigger one is dropped. Returns whether it was used.
inline constexpr std::uint32_t kRelationsSince208 = 31;
template <class Rel>
bool RestoreRelations(Rel& a_rel, const std::int32_t* a_m, std::uint32_t a_nf)
{
	if (a_nf < kRelationsSince208 || a_nf > std::uint32_t(kFactionCount)) return false;
	for (std::uint32_t x = 0; x < a_nf; ++x)
		for (std::uint32_t y = 0; y < a_nf; ++y) a_rel[x][y] = a_m[std::size_t(x) * a_nf + y];
	return true;
}
}  // namespace ALife::SaveFormat

namespace ALife {
// A faction byte from any save written before 208 (SIM2 207 and older, TASK 1): the shift above, nothing of its own.
inline Faction FactionFromPreSolstheim(std::uint8_t a_old) { return SaveFormat::FactionOf(SaveFormat::kVerPreSolstheim, a_old); }
}  // namespace ALife
