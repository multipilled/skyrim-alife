// Headless lore tests for the standing system (docs/STANDING.md). Build: tests\build_standing_test.bat
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ALife/Standing.h"

using namespace ALife;

static int gFails = 0, gPass = 0;
#define CHECK(cond, msg) do { if (cond) { ++gPass; } else { ++gFails; std::printf("FAIL: %s   [%s]\n", msg, #cond); } } while (0)

static const FormID kWhiterun = 0x16772, kEastmarch = 0x1676A;

static PlayerContext Traveller()
{
	PlayerContext c;
	c.race = RaceId::Nord;
	c.level = 5;
	c.power = 100.f;
	return c;
}

static Group MakeGroup(Faction f, std::uint64_t id = 7)
{
	Group g;
	g.id = id;
	g.faction = f;
	Member m;
	m.level = 8;
	m.health = m.healthMax = 100;
	g.members.push_back(m);
	return g;
}

static Env Meeting(float dist, float squadPower)
{
	Env e;
	e.dist = dist;
	e.squadPower = squadPower;
	e.now = 24.0 * 10 + 14;   // an afternoon
	return e;
}

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : "config";
	Config cfg;
	std::string err;
	if (!LoadConfig(dir, cfg, err)) { std::printf("cannot load config: %s\n", err.c_str()); return 2; }
	std::printf("standing: %zu rules loaded\n", cfg.standing.rules.size());
	CHECK(cfg.standing.rules.size() >= 20, "standing.ltx rules parsed");

	Standing st(&cfg);
	const Site none;

	// ---- 1. a fresh traveller is not hated by decree
	{
		auto ctx = Traveller();
		for (int f = 0; f < kHumanFactionCount; ++f) {   // monsters have no standing (docs/MUTANTS.md)
			const auto a = st.Evaluate(Faction(f), ctx, nullptr, none);
			char m[96];
			std::snprintf(m, sizeof m, "fresh traveller is not hostile to %s (%+.0f)", FactionId(Faction(f)), a.value);
			CHECK(a.regard != Regard::Hostile, m);
		}
		CHECK(st.Evaluate(Faction::Imperial, ctx, nullptr, none).regard == Regard::Neutral, "Legion neutral to a stranger");
		CHECK(st.Evaluate(Faction::Bandit, ctx, nullptr, none).regard == Regard::Unfriendly, "bandits unfriendly (not on-sight hostile)");
		CHECK(st.Evaluate(Faction::Wanderer, ctx, nullptr, none).regard == Regard::Neutral, "wanderers neutral");
		CHECK(st.Evaluate(Faction::Thalmor, ctx, nullptr, none).regard == Regard::Unfriendly, "Thalmor are cold");
	}

	// ---- 2. the Civil War
	{
		auto ctx = Traveller();
		ctx.flags = PlayerFlag::Imperial;
		CHECK(st.Evaluate(Faction::Imperial, ctx, nullptr, none).regard == Regard::Friendly, "sworn Legion: Legion friendly");
		CHECK(st.Evaluate(Faction::Stormcloak, ctx, nullptr, none).regard == Regard::Hostile, "sworn Legion: Stormcloaks hostile");
		CHECK(st.Evaluate(Faction::Thalmor, ctx, nullptr, none).regard != Regard::Hostile, "sworn Legion: Thalmor tolerate");
		ctx.flags = PlayerFlag::Stormcloak;
		CHECK(st.Evaluate(Faction::Stormcloak, ctx, nullptr, none).regard >= Regard::Friendly, "sworn Stormcloak: Stormcloaks friendly");
		CHECK(st.Evaluate(Faction::Imperial, ctx, nullptr, none).regard == Regard::Hostile, "sworn Stormcloak: Legion hostile");
	}

	// ---- 3. what you are
	{
		auto ctx = Traveller();
		ctx.flags = PlayerFlag::Vampire;
		CHECK(st.Evaluate(Faction::Dawnguard, ctx, nullptr, none).regard == Regard::Hostile, "vampire: Dawnguard hostile");
		CHECK(st.Evaluate(Faction::Vigilant, ctx, nullptr, none).regard == Regard::Hostile, "vampire: Vigilants hostile");
		CHECK(st.Evaluate(Faction::Vampire, ctx, nullptr, none).regard >= Regard::Friendly, "vampire: kin friendly");
		CHECK(st.Evaluate(Faction::Imperial, ctx, nullptr, none).regard == Regard::Neutral, "vampire: ordinary soldiers do not know");
		ctx.flags = PlayerFlag::Werewolf;
		CHECK(st.Evaluate(Faction::SilverHand, ctx, nullptr, none).regard == Regard::Hostile, "werewolf: Silver Hand hostile");
		ctx.flags = PlayerFlag::Dawnguard;
		CHECK(st.Evaluate(Faction::Dawnguard, ctx, nullptr, none).regard >= Regard::Friendly, "Dawnguard member: Dawnguard friendly");
		CHECK(st.Evaluate(Faction::Vampire, ctx, nullptr, none).regard == Regard::Hostile, "Dawnguard member: vampires hostile");
		ctx.flags = PlayerFlag::BeastForm;
		CHECK(st.Evaluate(Faction::Wanderer, ctx, nullptr, none).regard == Regard::Unfriendly, "beast form frightens travellers");
	}

	// ---- 4. race
	{
		auto ctx = Traveller();
		ctx.race = RaceId::Breton;
		const float bretonF = st.Evaluate(Faction::Forsworn, ctx, nullptr, none).value;
		ctx.race = RaceId::Nord;
		const float nordF = st.Evaluate(Faction::Forsworn, ctx, nullptr, none).value;
		CHECK(bretonF > nordF + 400, "Forsworn favour Reachfolk over Nords");
		ctx.race = RaceId::Altmer;
		CHECK(st.Evaluate(Faction::Thalmor, ctx, nullptr, none).regard >= Regard::Neutral, "High Elf: Thalmor warm up");
		CHECK(st.Evaluate(Faction::Stormcloak, ctx, nullptr, none).regard == Regard::Unfriendly, "High Elf: Stormcloaks distrust");
	}

	// ---- 5. disguise is mild
	{
		auto ctx = Traveller();
		ctx.disguise[int(Faction::Stormcloak)] = 1.f;
		const auto imp = st.Evaluate(Faction::Imperial, ctx, nullptr, none);
		CHECK(imp.regard != Regard::Hostile, "Stormcloak colours alone do not make the Legion attack");
		const auto base = st.Evaluate(Faction::Imperial, Traveller(), nullptr, none);
		CHECK(imp.value < base.value, "Stormcloak colours make the Legion suspicious");
		CHECK(st.Evaluate(Faction::Stormcloak, ctx, nullptr, none).value > st.Evaluate(Faction::Stormcloak, Traveller(), nullptr, none).value + 300,
			  "Stormcloak colours please Stormcloaks");
	}

	// ---- 6. the law of the hold
	{
		auto ctx = Traveller();
		ctx.bounty.push_back({ kWhiterun, 150 });
		Site enforcing{ kWhiterun, true };
		CHECK(st.Evaluate(Faction::Imperial, ctx, nullptr, enforcing).regard == Regard::Unfriendly, "bounty 150: Legion in Whiterun unfriendly");
		ctx.bounty[0].second = 700;
		CHECK(st.Evaluate(Faction::Imperial, ctx, nullptr, enforcing).regard == Regard::Hostile, "bounty 700: Legion in Whiterun hostile");
		CHECK(st.Evaluate(Faction::Imperial, ctx, nullptr, Site{ kEastmarch, true }).regard == Regard::Neutral, "bounty elsewhere means nothing");
		CHECK(st.Evaluate(Faction::Imperial, ctx, nullptr, Site{ kWhiterun, false }).regard == Regard::Neutral, "not their hold: not their law");
		CHECK(st.Evaluate(Faction::Bandit, ctx, nullptr, enforcing).regard == Regard::Unfriendly, "bandits do not enforce anything");
	}

	// ---- 7. deeds
	{
		Standing s(&cfg);
		Group sc = MakeGroup(Faction::Stormcloak, 11);
		Member vm = sc.members[0];
		const float imp0 = s.Evaluate(Faction::Imperial, Traveller(), nullptr, none).value;
		const float stm0 = s.Evaluate(Faction::Stormcloak, Traveller(), nullptr, none).value;
		s.OnKill(sc, vm, true, false, 100.0);
		CHECK(s.Evaluate(Faction::Stormcloak, Traveller(), nullptr, none).value < stm0 - 40, "killing a Stormcloak costs Stormcloak goodwill");
		CHECK(s.Evaluate(Faction::Imperial, Traveller(), nullptr, none).value > imp0, "...and pleases their enemies");
		CHECK(sc.memory < 0, "...and the squad remembers");
		CHECK(s.state.notoriety > 0, "an unprovoked killing of a soldier counts toward notoriety");

		Standing s2(&cfg);
		Group g2 = MakeGroup(Faction::Imperial, 12);
		for (int i = 0; i < 6; ++i) s2.OnKill(g2, g2.members[0], true, true, 100.0);   // they attacked first
		CHECK(s2.state.notoriety == 0.f, "defending yourself does not make you a murderer");
		CHECK(s2.state.goodwill[int(Faction::Imperial)] > -250.f, "provoked kills cost little");

		Standing s3(&cfg), s4(&cfg);
		Group g3 = MakeGroup(Faction::Imperial, 13), g4 = MakeGroup(Faction::Imperial, 13);
		s3.OnKill(g3, g3.members[0], true, false, 100.0);
		s4.OnKill(g4, g4.members[0], false, false, 100.0);
		CHECK(s3.state.goodwill[int(Faction::Imperial)] < s4.state.goodwill[int(Faction::Imperial)] - 30, "unwitnessed kills cost far less at once");

		int discovered = 0;
		for (int i = 0; i < 400; ++i) {
			Standing sx(&cfg);
			Group gx = MakeGroup(Faction::Imperial, std::uint64_t(1000 + i));
			sx.OnKill(gx, gx.members[0], false, false, 100.0 + i);
			discovered += sx.state.rumors.empty() ? 0 : 1;
		}
		CHECK(discovered > 120 && discovered < 200, "about 40% of hidden kills are discovered later");

		Standing s5(&cfg);
		s5.state.rumors.push_back({ Faction::Imperial, -300.f, 120.0 });
		const float before = s5.state.goodwill[int(Faction::Imperial)];
		s5.Tick(121.0, 0.5);
		CHECK(s5.state.goodwill[int(Faction::Imperial)] < before - 250.f, "a discovered body reaches the faction later");
		CHECK(s5.state.rumors.empty(), "...and the rumour is spent");

		Standing s6(&cfg);
		Group wb = MakeGroup(Faction::Bandit, 15);
		for (int i = 0; i < 5; ++i) s6.OnKill(wb, wb.members[0], true, false, 100.0);
		CHECK(s6.state.notoriety == 0.f, "killing outlaws does not make a murderer");
		CHECK(s6.state.goodwill[int(Faction::Imperial)] > 0.f, "the Legion thanks you for killing bandits");
		CHECK(s6.state.goodwill[int(Faction::Imperial)] <= cfg.standing.dailyGainCap + 0.1f, "goodwill cannot be farmed in a day");
	}

	// ---- 8. decay
	{
		Standing s(&cfg);
		s.state.goodwill[int(Faction::Imperial)] = -500.f;
		s.state.goodwill[int(Faction::Wanderer)] = 500.f;
		s.state.notoriety = 400.f;
		s.Tick(24.0 * 100, 24.0 * 100);
		CHECK(std::fabs(s.state.goodwill[int(Faction::Imperial)] + 250.f) < 12.f, "bad reputation halves in ~100 days");
		CHECK(s.state.goodwill[int(Faction::Wanderer)] < 100.f, "good reputation fades faster");
		CHECK(s.state.notoriety < 150.f, "notoriety fades");
	}

	// ---- 9. predators weigh the odds
	{
		auto weak = Traveller();   weak.power = 40.f;    weak.level = 1;
		auto strong = Traveller(); strong.power = 700.f; strong.level = 30;
		Group bandits = MakeGroup(Faction::Bandit, 21);
		const auto vWeak = st.Judge(bandits, weak, Meeting(800.f, 160.f));
		CHECK(vWeak.call == Call::Parley || vWeak.call == Call::Attack, "bandits act on a weak lone traveller");
		const auto vStrong = st.Judge(bandits, strong, Meeting(800.f, 160.f));
		CHECK(vStrong.call == Call::Watch, "bandits keep away from a strong hero");
		Group paid = bandits;
		paid.tollUntil = 24.0 * 10 + 20;
		CHECK(st.Judge(paid, weak, Meeting(800.f, 160.f)).call == Call::Ignore, "a paid toll buys passage");
		Env refused = Meeting(800.f, 160.f);
		refused.parleyRefused = true;
		CHECK(st.Judge(bandits, weak, refused).call == Call::Attack, "refusing the toll ends in a fight");
		auto rich = weak;
		rich.wealth = 1.f;
		CHECK(st.Judge(bandits, rich, Meeting(800.f, 120.f)).desire >= st.Judge(bandits, weak, Meeting(800.f, 120.f)).desire, "wealth tempts");
		Standing fear(&cfg);
		fear.state.notoriety = 900.f;
		auto mid = Traveller();
		mid.power = 150.f;
		CHECK(fear.Judge(bandits, mid, Meeting(800.f, 160.f)).desire < st.Judge(bandits, mid, Meeting(800.f, 160.f)).desire, "a notorious killer is feared");
		Env far = Meeting(5000.f, 160.f);
		CHECK(st.Judge(bandits, weak, far).call == Call::Watch, "nobody ambushes from across the valley");
		Env hurt = Meeting(800.f, 160.f);
		hurt.tooHurt = true;
		CHECK(st.Judge(bandits, weak, hurt).call == Call::Withdraw, "a battered squad withdraws");
		CHECK(st.TollFor(weak) >= 40 && st.TollFor(strong) > st.TollFor(weak), "tolls scale with the traveller");
	}

	// ---- 10. territory and challenges
	{
		auto ctx = Traveller();
		ctx.flags = PlayerFlag::Armed;
		Group fs = MakeGroup(Faction::Forsworn, 31);
		Env road = Meeting(900.f, 200.f);
		const Call onRoad = st.Judge(fs, ctx, road).call;
		CHECK(onRoad == Call::Watch || onRoad == Call::Ignore, "Forsworn let a traveller on the road pass");
		Env camp = Meeting(900.f, 200.f);
		camp.inTerritory = true;
		CHECK(st.Judge(fs, ctx, camp).call == Call::Challenge, "Forsworn challenge an intruder in their camp");
		camp.challengeElapsed = 12.f;
		CHECK(st.Judge(fs, ctx, camp).call == Call::Attack, "...and attack if they stay");

		Group th = MakeGroup(Faction::Thalmor, 32);
		Env nearby = Meeting(800.f, 200.f);
		CHECK(st.Judge(th, ctx, nearby).call == Call::Challenge, "Thalmor challenge an armed stranger");
		auto calm = Traveller();
		CHECK(st.Judge(th, calm, nearby).call == Call::Watch, "...but only watch a peaceful one");
		nearby.challengeElapsed = 10.f;
		CHECK(st.Judge(th, ctx, nearby).call == Call::Attack, "...and strike when the challenge is ignored");

		Group leg = MakeGroup(Faction::Imperial, 33);
		CHECK(st.Judge(leg, ctx, nearby).call == Call::Ignore, "the Legion ignores an armed stranger who has done nothing");
	}

	// ---- 11. friends
	{
		auto ctx = Traveller();
		ctx.flags = PlayerFlag::Companions | PlayerFlag::College;
		Group w = MakeGroup(Faction::Wanderer, 41);
		Env e = Meeting(500.f, 100.f);
		CHECK(st.Judge(w, ctx, e).call == Call::Greet, "a friendly squad greets");
		e.greeted = true;
		CHECK(st.Judge(w, ctx, e).call == Call::Ignore, "...once");
		ctx.flags = PlayerFlag::Imperial;
		ctx.level = 50;
		Group leg = MakeGroup(Faction::Imperial, 42);
		leg.memory = 300.f;
		CHECK(st.Judge(leg, ctx, e).call == Call::Assist, "an allied squad stands with the player");
	}

	// ---- 12. thresholds
	CHECK(st.RegardOf(-1000) == Regard::Hostile && st.RegardOf(-300) == Regard::Unfriendly && st.RegardOf(0) == Regard::Neutral &&
		  st.RegardOf(300) == Regard::Friendly && st.RegardOf(1000) == Regard::Allied, "tier thresholds");

	std::printf("\nstanding tests: %d passed, %d failed\n", gPass, gFails);
	return gFails ? 1 : 0;
}
