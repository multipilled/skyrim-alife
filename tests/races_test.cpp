// Headless tests for the race mixes (T24): config/races.ltx through Config::RollRace. Tamriel draws are exactly the old ones
// (the tables src/Sim.cpp rolled from before T24, copied here as the reference), Solstheim favours the Dunmer with Altmer and
// Bosmer rare, the island's own factions keep their tables. Build: tests\build_races_test.bat ; run: races_test.exe [config dir]
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "ALife/Config.h"

using namespace ALife;

static int gFails = 0, gPass = 0;
#define CHECK(cond, msg) do { if (cond) { ++gPass; } else { ++gFails; std::printf("FAIL: %s   [%s]\n", msg, #cond); } } while (0)

namespace {
	// ---- the pre-T24 plugin code, verbatim in behaviour (src/Sim.cpp RollRace / Weighted)
	struct W { RaceId race; int w; };
	RaceId Weighted(const W* tbl, int n, Rng& r)
	{
		int tot = 0;
		for (int i = 0; i < n; ++i) tot += tbl[i].w;
		int x = r.index(tot);
		for (int i = 0; i < n; ++i) { if (x < tbl[i].w) return tbl[i].race; x -= tbl[i].w; }
		return tbl[0].race;
	}
	RaceId OldRollRace(Faction f, Rng& r)
	{
		static const W bandit[] = { { RaceId::Nord, 40 }, { RaceId::Imperial, 12 }, { RaceId::Redguard, 10 }, { RaceId::Breton, 8 }, { RaceId::Dunmer, 8 },
			{ RaceId::Orsimer, 8 }, { RaceId::Bosmer, 5 }, { RaceId::Khajiit, 4 }, { RaceId::Argonian, 3 }, { RaceId::Altmer, 2 } };
		static const W forsworn[] = { { RaceId::Breton, 90 }, { RaceId::Nord, 10 } };
		static const W imperial[] = { { RaceId::Imperial, 45 }, { RaceId::Nord, 20 }, { RaceId::Redguard, 15 }, { RaceId::Breton, 12 }, { RaceId::Bosmer, 5 }, { RaceId::Altmer, 3 } };
		static const W storm[] = { { RaceId::Nord, 75 }, { RaceId::Dunmer, 5 }, { RaceId::Redguard, 5 }, { RaceId::Breton, 5 }, { RaceId::Orsimer, 5 }, { RaceId::Khajiit, 3 }, { RaceId::Argonian, 2 } };
		static const W thalmor[] = { { RaceId::Altmer, 88 }, { RaceId::Bosmer, 12 } };
		static const W hunter[] = { { RaceId::Nord, 50 }, { RaceId::Bosmer, 15 }, { RaceId::Imperial, 10 }, { RaceId::Redguard, 8 }, { RaceId::Breton, 6 },
			{ RaceId::Orsimer, 5 }, { RaceId::Khajiit, 3 }, { RaceId::Argonian, 3 } };
		static const W adv[] = { { RaceId::Nord, 25 }, { RaceId::Imperial, 12 }, { RaceId::Breton, 12 }, { RaceId::Redguard, 10 }, { RaceId::Altmer, 8 },
			{ RaceId::Bosmer, 8 }, { RaceId::Dunmer, 10 }, { RaceId::Orsimer, 6 }, { RaceId::Khajiit, 5 }, { RaceId::Argonian, 4 } };
		static const W merc[] = { { RaceId::Nord, 25 }, { RaceId::Redguard, 20 }, { RaceId::Imperial, 15 }, { RaceId::Orsimer, 15 }, { RaceId::Breton, 10 },
			{ RaceId::Dunmer, 5 }, { RaceId::Khajiit, 5 }, { RaceId::Argonian, 5 } };
		static const W mage[] = { { RaceId::Breton, 30 }, { RaceId::Altmer, 25 }, { RaceId::Dunmer, 20 }, { RaceId::Imperial, 15 }, { RaceId::Nord, 5 }, { RaceId::Redguard, 5 } };
		static const W necro[] = { { RaceId::Dunmer, 30 }, { RaceId::Breton, 25 }, { RaceId::Imperial, 15 }, { RaceId::Altmer, 15 }, { RaceId::Nord, 10 }, { RaceId::Redguard, 5 } };
		static const W vamp[] = { { RaceId::NordVampire, 2 }, { RaceId::ImperialVampire, 1 }, { RaceId::BretonVampire, 1 }, { RaceId::DunmerVampire, 2 } };
		static const W dawn[] = { { RaceId::Nord, 35 }, { RaceId::Imperial, 25 }, { RaceId::Redguard, 15 }, { RaceId::Breton, 15 }, { RaceId::Orsimer, 10 } };
		static const W silver[] = { { RaceId::Nord, 55 }, { RaceId::Imperial, 15 }, { RaceId::Breton, 10 }, { RaceId::Redguard, 10 }, { RaceId::Orsimer, 10 } };
		static const W vigil[] = { { RaceId::Nord, 35 }, { RaceId::Imperial, 30 }, { RaceId::Breton, 20 }, { RaceId::Redguard, 10 }, { RaceId::Altmer, 5 } };
		static const W khajiit[] = { { RaceId::Khajiit, 1 } };
		static const W skaal[] = { { RaceId::Nord, 1 } };
		static const W redoran[] = { { RaceId::Dunmer, 1 } };
		static const W reaver[] = { { RaceId::Dunmer, 40 }, { RaceId::Nord, 30 }, { RaceId::Imperial, 8 }, { RaceId::Redguard, 8 }, { RaceId::Orsimer, 6 },
			{ RaceId::Breton, 4 }, { RaceId::Argonian, 2 }, { RaceId::Khajiit, 2 } };
#define T(t) return Weighted(t, int(std::size(t)), r)
		switch (f) {
		case Faction::Bandit: T(bandit);
		case Faction::Forsworn: T(forsworn);
		case Faction::Imperial: T(imperial);
		case Faction::Stormcloak: T(storm);
		case Faction::Thalmor: T(thalmor);
		case Faction::Hunter: T(hunter);
		case Faction::Mercenary: T(merc);
		case Faction::Mage: T(mage);
		case Faction::Necromancer: T(necro);
		case Faction::Vampire: T(vamp);
		case Faction::Dawnguard: T(dawn);
		case Faction::SilverHand: T(silver);
		case Faction::Vigilant: T(vigil);
		case Faction::Caravan: T(khajiit);
		case Faction::Skaal: T(skaal);
		case Faction::Redoran: T(redoran);
		case Faction::Reaver: T(reaver);
		default: T(adv);
		}
#undef T
	}

	std::array<double, int(RaceId::COUNT)> Shares(const Config& c, Faction f, int ws, int n, std::uint64_t seed)
	{
		std::array<double, int(RaceId::COUNT)> s{};
		Rng r(seed);
		for (int i = 0; i < n; ++i) s[int(c.RollRace(f, ws, r))] += 1.0 / n;
		return s;
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const std::string dir = argc > 1 ? argv[1] : "config";
	Config cfg;
	std::string err;
	if (!LoadConfig(dir, cfg, err)) { std::printf("cannot load config: %s\n", err.c_str()); return 2; }
	CHECK(err.find("races.ltx") == std::string::npos, "races.ltx loads without warnings");
	if (err.find("races.ltx") != std::string::npos) std::printf("  %s\n", err.c_str());

	// ---- 1. Tamriel: the same draws as before T24, for every faction, from the same seed
	std::printf("section 1: Tamriel unchanged\n");
	bool same = true;
	std::string first;
	for (int f = 0; f < kHumanFactionCount && same; ++f) {
		Rng a(1000 + std::uint64_t(f)), b(1000 + std::uint64_t(f));
		for (int i = 0; i < 20000; ++i) {
			const RaceId x = cfg.RollRace(Faction(f), 0, a), y = OldRollRace(Faction(f), b);
			if (x != y) { same = false; first = FactionId(Faction(f)); break; }
		}
		same = same && a.s == b.s;
	}
	if (!same) std::printf("  first difference: %s\n", first.c_str());
	CHECK(same, "every faction in Tamriel draws exactly as before (same races, same rng state)");
	// worldspaces the tables do not know fall back to Tamriel
	Rng a(7), b(7);
	CHECK(cfg.RollRace(Faction::Bandit, 5, a) == OldRollRace(Faction::Bandit, b) && cfg.RollRace(Faction::Bandit, -1, a) == OldRollRace(Faction::Bandit, b),
	      "an unknown worldspace index uses the Tamriel table");

	// ---- 2. Solstheim: Dunmer first, Altmer and Bosmer rare
	std::printf("section 2: Solstheim\n");
	const Faction general[] = { Faction::Bandit, Faction::Wanderer, Faction::Hunter, Faction::Mercenary, Faction::Mage, Faction::Necromancer,
	                            Faction::Dawnguard, Faction::SilverHand, Faction::Vigilant };
	for (Faction f : general) {
		const auto s = Shares(cfg, f, 1, 40000, 99);
		const double dunmer = s[int(RaceId::Dunmer)], elves = s[int(RaceId::Altmer)] + s[int(RaceId::Bosmer)];
		double top = 0;
		int topRace = 0;
		for (int r = 0; r < int(RaceId::COUNT); ++r) if (s[r] > top) { top = s[r]; topRace = r; }
		char what[160];
		std::snprintf(what, sizeof what, "Solstheim %s: Dunmer %.1f%% (> 45%%, the most), Altmer + Bosmer %.1f%% (< 6%%)", FactionId(f), 100 * dunmer, 100 * elves);
		std::printf("  %s\n", what);
		CHECK(dunmer > 0.45 && topRace == int(RaceId::Dunmer) && elves < 0.06, what);
		CHECK(s[int(RaceId::Nord)] > s[int(RaceId::Breton)] && s[int(RaceId::Nord)] > s[int(RaceId::Altmer)], "Nords next, before Bretons and Altmer");
	}
	{
		const auto s = Shares(cfg, Faction::Vampire, 1, 40000, 3);
		CHECK(s[int(RaceId::DunmerVampire)] > 0.5 && s[int(RaceId::DunmerVampire)] > s[int(RaceId::NordVampire)], "Solstheim vampires: Dunmer vampires first");
		const auto t = Shares(cfg, Faction::Vampire, 0, 40000, 3);
		CHECK(t[int(RaceId::DunmerVampire)] < 0.4, "Tamriel vampires keep their old mix");
	}
	// the island's own factions and the caravans keep their tables; factions not living there fall back to Tamriel's
	for (Faction f : { Faction::Skaal, Faction::Redoran, Faction::Reaver, Faction::Caravan, Faction::Forsworn, Faction::Thalmor, Faction::Imperial, Faction::Stormcloak }) {
		Rng x(11), y(11);
		bool eq = true;
		for (int i = 0; i < 5000; ++i) eq = eq && cfg.RollRace(f, 1, x) == cfg.RollRace(f, 0, y);
		char what[120];
		std::snprintf(what, sizeof what, "%s on Solstheim draws as in Tamriel", FactionId(f));
		CHECK(eq, what);
	}
	CHECK(&cfg.RacesFor(Faction::Skaal, 1) == &cfg.RacesFor(Faction::Skaal, 0) && &cfg.RacesFor(Faction::Bandit, 1) != &cfg.RacesFor(Faction::Bandit, 0),
	      "RacesFor: no override = the Tamriel table itself");

	// ---- 3. the loader: names, bad entries, sections
	std::printf("section 3: loader\n");
	CHECK(std::string(RaceIdName(RaceId::DunmerVampire)) == "dunmervampire" && RaceFromId("bosmer") == RaceId::Bosmer, "race ids");
	bool ok = true;
	RaceFromId("dwemer", &ok);
	CHECK(!ok, "an unknown race id is refused");
	CHECK(std::string(kWorldspaceIds[1]) == "DLC2SolstheimWorld", "worldspace 1 is DLC2SolstheimWorld");

	std::printf("\nraces_test: %d passed, %d failed\n", gPass, gFails);
	return gFails ? 1 : 0;
}
