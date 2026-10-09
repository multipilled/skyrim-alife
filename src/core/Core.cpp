#include "ALife/Core.h"

namespace ALife {

namespace {
	struct FacInfo { const char* id; const char* name; };
	constexpr FacInfo kFac[] = {
		{ "bandit", "Bandits" },           { "forsworn", "Forsworn" },      { "imperial", "Imperial Legion" },
		{ "stormcloak", "Stormcloaks" },   { "thalmor", "Thalmor" },        { "wanderer", "Wanderers" },
		{ "hunter", "Hunters" },           { "mercenary", "Mercenaries" },  { "mage", "Mages" },
		{ "necromancer", "Necromancers" }, { "vampire", "Vampires" },       { "dawnguard", "Dawnguard" },
		{ "silverhand", "Silver Hand" },   { "vigilant", "Vigilants of Stendarr" }, { "caravan", "Khajiit Caravans" },
		{ "wildlife", "Wild beasts" },     { "draugr", "Draugr" },          { "falmer", "Falmer" },
		{ "troll", "Trolls" },             { "spriggan", "Spriggans" },     { "zombie", "Zombies" },
		{ "hound", "Death hounds" },       { "werebeast", "Werebeasts" },   { "riekling", "Rieklings" },
		{ "spirit", "Spirits" },          { "hagraven", "Hagravens" },     { "giant", "Giants" },
		{ "dwemer", "Dwemer automatons" },
	};
	static_assert(sizeof(kFac) / sizeof(kFac[0]) == kFactionCount, "faction table out of sync with enum Faction");

	constexpr const char* kKindNames[] = {
		"wilderness", "city", "town", "farm", "inn", "fort", "bandit_camp", "forsworn_camp", "military_camp", "dungeon", "crypt",
		"dwarven_ruin", "mine", "vampire_lair", "warlock_lair", "werewolf_lair", "giant_camp", "falmer_hive", "dragon_lair",
		"animal_den", "hagraven_nest", "spriggan_grove", "orc_stronghold", "dragon_priest_lair", "shipwreck", "mill",
	};
	static_assert(sizeof(kKindNames) / sizeof(kKindNames[0]) == int(Kind::COUNT), "kind table out of sync with enum Kind");
}

const char* FactionId(Faction a_f) { return int(a_f) < kFactionCount ? kFac[int(a_f)].id : "none"; }
const char* FactionName(Faction a_f) { return int(a_f) < kFactionCount ? kFac[int(a_f)].name : "Nobody"; }

Faction FactionFromId(const std::string& a_id, bool* a_ok)
{
	for (int i = 0; i < kFactionCount; ++i)
		if (a_id == kFac[i].id) { if (a_ok) *a_ok = true; return Faction(i); }
	if (a_ok) *a_ok = false;
	return Faction::Bandit;
}

const char* TierName(Tier a_t)
{
	switch (a_t) {
	case Tier::Novice: return "novice";
	case Tier::Advanced: return "advanced";
	case Tier::Veteran: return "veteran";
	default: return "?";
	}
}

bool IsVampireRace(RaceId a_r)
{
	return a_r == RaceId::NordVampire || a_r == RaceId::ImperialVampire || a_r == RaceId::BretonVampire || a_r == RaceId::DunmerVampire;
}

const char* ActName(Act a_a)
{
	switch (a_a) {
	case Act::Rest: return "rest";
	case Act::Trade: return "trade";
	case Act::Hunt: return "hunt";
	case Act::Fish: return "fish";
	case Act::RaidSettlement: return "raid";
	case Act::RaidDungeon: return "clear";
	default: return "-";
	}
}

const char* KindName(Kind a_k) { return int(a_k) < int(Kind::COUNT) ? kKindNames[int(a_k)] : "?"; }

Kind KindFromName(const std::string& a_name, bool* a_ok)
{
	for (int i = 0; i < int(Kind::COUNT); ++i)
		if (a_name == kKindNames[i]) { if (a_ok) *a_ok = true; return Kind(i); }
	if (a_ok) *a_ok = false;
	return Kind::Wilderness;
}

std::int32_t Group::livingCount() const
{
	std::int32_t n = 0;
	for (auto& m : members) n += m.alive() ? 1 : 0;
	return n;
}

float Group::avgLevel() const
{
	float s = 0;
	int n = 0;
	for (auto& m : members) if (m.alive()) { s += float(m.level); ++n; }
	return n ? s / float(n) : 0.f;
}

}  // namespace ALife
