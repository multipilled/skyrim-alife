#include "ALife/Standing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ALife {

// --------------------------------------------------------------------------------------------- names
namespace PlayerFlag {
std::uint32_t FromName(const std::string& a_n)
{
	struct E { const char* name; std::uint32_t bit; };
	static const E kTable[] = {
		{ "imperial", Imperial },     { "stormcloak", Stormcloak }, { "companions", Companions }, { "thieves", ThievesGuild },
		{ "brotherhood", DarkBrotherhood }, { "college", College }, { "dawnguard", Dawnguard },   { "volkihar", Volkihar },
		{ "blades", Blades },         { "greybeards", Greybeards }, { "vampire", Vampire },       { "werewolf", Werewolf },
		{ "beast", BeastForm },       { "armed", Armed },           { "sneaking", Sneaking },     { "mounted", Mounted },
		{ "incombat", InCombat },     { "night", Night },           { "skaal", SkaalFriend },     { "ravenrock", RavenRock },
	};
	for (auto& e : kTable)
		if (a_n == e.name) return e.bit;
	return 0;
}
}

const char* RegardName(Regard a_r)
{
	switch (a_r) {
	case Regard::Hostile: return "Hostile";
	case Regard::Unfriendly: return "Unfriendly";
	case Regard::Neutral: return "Neutral";
	case Regard::Friendly: return "Friendly";
	case Regard::Allied: return "Allied";
	}
	return "?";
}

const char* CallName(Call a_c)
{
	switch (a_c) {
	case Call::Ignore: return "ignore";
	case Call::Greet: return "greet";
	case Call::Watch: return "watch";
	case Call::Challenge: return "challenge";
	case Call::Parley: return "parley";
	case Call::Attack: return "attack";
	case Call::Withdraw: return "withdraw";
	case Call::Assist: return "assist";
	}
	return "?";
}

// --------------------------------------------------------------------------------------------- config
namespace {
	bool ParseNum(const std::string& s, float& out)
	{
		try { out = std::stof(s); return true; } catch (...) { return false; }
	}

	int RaceFromName(const std::string& n)
	{
		static const char* kNames[] = { "nord", "imperial", "breton", "redguard", "altmer", "bosmer", "dunmer", "orsimer", "argonian", "khajiit" };
		for (int i = 0; i < 10; ++i)
			if (n == kNames[i]) return i;
		return -1;
	}

	// "level>=20" / "bounty<100" -> name, atLeast, value
	bool SplitCompare(const std::string& t, std::string& name, bool& atLeast, float& value)
	{
		auto p = t.find(">=");
		if (p != std::string::npos) { name = Trim(t.substr(0, p)); atLeast = true; return ParseNum(Trim(t.substr(p + 2)), value); }
		p = t.find('<');
		if (p != std::string::npos) { name = Trim(t.substr(0, p)); atLeast = false; return ParseNum(Trim(t.substr(p + 1)), value); }
		return false;
	}

	bool ParseCond(const std::string& raw, RuleCond& c)
	{
		std::string t = Trim(raw);
		if (t.empty()) return false;
		c = RuleCond{};
		if (t[0] == '!') { c.negate = true; t = Trim(t.substr(1)); }
		if (t.rfind("race:", 0) == 0) {
			c.type = RuleCond::Type::Race;
			for (auto& n : SplitList(t.substr(5))) {
				const int r = RaceFromName(n);
				if (r < 0) return false;
				c.races.push_back(std::uint8_t(r));
			}
			return !c.races.empty();
		}
		if (t == "enforces") { c.type = RuleCond::Type::Enforces; return true; }
		std::string name;
		bool atLeast = true;
		float value = 0;
		if (SplitCompare(t, name, atLeast, value)) {
			c.atLeast = atLeast;
			c.value = value;
			if (name == "level") { c.type = RuleCond::Type::Level; return true; }
			if (name == "notoriety") { c.type = RuleCond::Type::Notoriety; return true; }
			if (name == "bounty") { c.type = RuleCond::Type::Bounty; return true; }
			if (name.rfind("disguise:", 0) == 0) {
				bool ok = false;
				const Faction f = FactionFromId(name.substr(9), &ok);
				if (!ok) return false;
				c.type = RuleCond::Type::Disguise;
				c.faction = std::uint8_t(f);
				return true;
			}
			return false;
		}
		c.type = RuleCond::Type::Flag;
		c.flag = PlayerFlag::FromName(t);
		return c.flag != 0;
	}

	void FillFloats(const Ltx& l, const char* section, std::array<float, kFactionCount>& out)
	{
		if (auto* lines = l.Section(section))
			for (auto& kv : *lines) {
				bool ok = false;
				const Faction f = FactionFromId(kv.first, &ok);
				float v = 0;
				if (ok && ParseNum(kv.second, v)) out[int(f)] = v;
			}
	}

	void FillBools(const Ltx& l, const char* section, std::array<bool, kFactionCount>& out)
	{
		std::array<float, kFactionCount> tmp{};
		FillFloats(l, section, tmp);
		for (int i = 0; i < kFactionCount; ++i) out[i] = tmp[i] != 0.f;
	}
}

void LoadStandingConfig(const Ltx& l, StandingConfig& s)
{
	s = StandingConfig{};
	s.murderWeight.fill(1.f);
	FillFloats(l, "base", s.base);
	FillFloats(l, "opportunism", s.opportunism);
	FillFloats(l, "territorial", s.territorial);
	FillFloats(l, "murder_weight", s.murderWeight);
	FillBools(l, "lawful", s.lawful);
	FillBools(l, "parley", s.parley);
	FillBools(l, "nocturnal", s.nocturnal);

	auto f = [&](const char* sec, const char* key, float& v) { v = l.GetF(sec, key, v); };
	f("tiers", "hostile", s.hostileAt); f("tiers", "unfriendly", s.unfriendlyAt);
	f("tiers", "friendly", s.friendlyAt); f("tiers", "allied", s.alliedAt);
	f("deeds", "goodwill_max", s.goodwillMax);
	f("deeds", "kill_base", s.killBase); f("deeds", "kill_per_level", s.killPerLevel); f("deeds", "kill_veteran", s.killVeteran);
	f("deeds", "kill_provoked_mult", s.killProvokedMult);
	f("deeds", "ally_share", s.allyShare); f("deeds", "friend_share", s.friendShare); f("deeds", "enemy_share", s.enemyShare);
	f("deeds", "unseen_mult", s.unseenMult); f("deeds", "discover_chance", s.discoverChance); f("deeds", "discover_mult", s.discoverMult);
	f("deeds", "discover_min_hours", s.discoverMinHours); f("deeds", "discover_max_hours", s.discoverMaxHours);
	f("deeds", "help_base", s.helpBase); f("deeds", "help_per_level", s.helpPerLevel); f("deeds", "daily_gain_cap", s.dailyGainCap);
	f("deeds", "trade_per_100_gold", s.tradePer100Gold); f("deeds", "attack_goodwill", s.attackGoodwill);
	f("deeds", "notoriety_per_severity", s.notorietyPerSeverity);
	f("memory", "attack", s.attackMemory); f("memory", "kill", s.killMemory); f("memory", "help", s.helpMemory);
	f("memory", "toll", s.tollMemory); f("memory", "half_life_days", s.memoryHalfLifeDays);
	f("decay", "goodwill_positive_half_life_days", s.halfLifePosDays);
	f("decay", "goodwill_negative_half_life_days", s.halfLifeNegDays);
	f("decay", "notoriety_half_life_days", s.notorietyHalfLifeDays);
	f("disguise", "bonus", s.disguiseBonus); f("disguise", "suspicion", s.disguiseSuspicion);
	f("predators", "advantage_floor", s.advantageFloor); f("predators", "advantage_full", s.advantageFull);
	f("predators", "hunger_bonus", s.hungerBonus); f("predators", "wealth_bonus", s.wealthBonus);
	f("predators", "night_bonus", s.nightBonus); f("predators", "fear_penalty", s.fearPenalty);
	f("predators", "parley_from", s.parleyFrom); f("predators", "attack_at", s.attackAt); f("predators", "ambush_at", s.ambushAt);
	f("meeting", "notice_range", s.noticeRange); f("meeting", "greet_range", s.greetRange);
	f("meeting", "challenge_range", s.challengeRange); f("meeting", "challenge_grace", s.challengeGrace);
	f("meeting", "territory_radius", s.territoryRadius);
	f("meeting", "talk_cooldown", s.talkCooldown);
	f("toll", "base", s.tollBase); f("toll", "per_level", s.tollPerLevel); f("toll", "pass_hours", s.tollPassHours);
	f("crime", "murder_bounty", s.murderBounty);
	s.notices = l.GetB("notices", "enabled", false);

	for (auto& name : l.Sections()) {
		if (name.rfind("rule.", 0) != 0) continue;
		StandingRule r;
		r.name = name.substr(5);
		r.label = r.name;
		auto* lines = l.Section(name);
		if (!lines) continue;
		bool good = true;
		for (auto& kv : *lines) {
			if (kv.first == "label") { r.label = kv.second; continue; }
			if (kv.first == "when") {
				for (auto& tok : SplitList(kv.second, '&')) {
					RuleCond c;
					if (ParseCond(tok, c)) r.when.push_back(std::move(c)); else good = false;
				}
				continue;
			}
			bool ok = false;
			const Faction fac = FactionFromId(kv.first, &ok);
			float v = 0;
			if (ok && ParseNum(kv.second, v)) r.delta[int(fac)] = v; else good = false;
		}
		if (good) s.rules.push_back(std::move(r));
	}
}

// --------------------------------------------------------------------------------------------- evaluation
Regard Standing::RegardOf(float a) const
{
	const auto& s = m_cfg->standing;
	if (a <= s.hostileAt) return Regard::Hostile;
	if (a <= s.unfriendlyAt) return Regard::Unfriendly;
	if (a < s.friendlyAt) return Regard::Neutral;
	if (a < s.alliedAt) return Regard::Friendly;
	return Regard::Allied;
}

bool Standing::RuleHolds(const StandingRule& r, const PlayerContext& ctx, const Site& site) const
{
	for (auto& c : r.when) {
		bool ok = true;
		switch (c.type) {
		case RuleCond::Type::Flag: ok = (ctx.flags & c.flag) == c.flag; break;
		case RuleCond::Type::Race: ok = std::find(c.races.begin(), c.races.end(), std::uint8_t(ctx.race)) != c.races.end(); break;
		case RuleCond::Type::Level: ok = c.atLeast ? float(ctx.level) >= c.value : float(ctx.level) < c.value; break;
		case RuleCond::Type::Notoriety: ok = c.atLeast ? state.notoriety >= c.value : state.notoriety < c.value; break;
		case RuleCond::Type::Bounty: {
			const float b = float(ctx.BountyIn(site.hold));
			ok = c.atLeast ? b >= c.value : b < c.value;
			break;
		}
		case RuleCond::Type::Enforces: ok = site.enforces; break;
		case RuleCond::Type::Disguise: {
			const float d = c.faction < kFactionCount ? ctx.disguise[c.faction] : 0.f;
			ok = c.atLeast ? d >= c.value : d < c.value;
			break;
		}
		}
		if (c.negate) ok = !ok;
		if (!ok) return false;
	}
	return true;
}

Attitude Standing::Evaluate(Faction f, const PlayerContext& ctx, const Group* g, const Site& site, bool explain) const
{
	const auto& sc = m_cfg->standing;
	Attitude at;
	float v = sc.base[int(f)];
	auto add = [&](float d, const std::string& text) {
		if (d == 0.f) return;
		v += d;
		if (explain) at.why.push_back({ text, d });
	};
	if (explain && v != 0.f) at.why.push_back({ "How they see a stranger", v });
	for (auto& r : sc.rules) {
		const float d = r.delta[int(f)];
		if (d != 0.f && RuleHolds(r, ctx, site)) add(d, r.label);
	}
	add(state.goodwill[int(f)], "Your reputation with them");
	if (g && g->memory != 0.f) add(g->memory, g->memory < 0 ? "This squad holds a grudge" : "This squad owes you");

	// colours: a disguise earns trust (unless they already count you an enemy) and their enemies' colours earn suspicion
	const float own = ctx.disguise[int(f)];
	if (own > 0.f && v > sc.hostileAt * 0.5f) add(sc.disguiseBonus * own, "Wearing their colours");
	float worst = 0.f;
	for (int o = 0; o < kFactionCount; ++o)
		if (o != int(f) && m_cfg->rel[int(f)][o] <= -1000) worst = std::max(worst, ctx.disguise[o]);
	if (worst > 0.f) add(-sc.disguiseSuspicion * worst, "Wearing their enemy's colours");

	at.value = std::clamp(v, -3000.f, 3000.f);
	at.regard = RegardOf(at.value);
	return at;
}

float Standing::Advantage(float squadPower, const PlayerContext& ctx) const
{
	const float mine = std::max(1.f, ctx.power) * (1.f + 0.6f * float(std::max(0, ctx.companions)));
	return squadPower / mine;
}

int Standing::TollFor(const PlayerContext& ctx) const
{
	const auto& sc = m_cfg->standing;
	return std::max(10, int((sc.tollBase + sc.tollPerLevel * float(ctx.level)) / std::max(0.01f, m_cfg->Diff(DiffChannel::Economy))));   // economy: richer games, smaller tolls
}

Verdict Standing::Judge(const Group& g, const PlayerContext& ctx, const Env& env) const
{
	const auto& sc = m_cfg->standing;
	const int fi = int(g.faction);
	Verdict v;
	const Attitude at = Evaluate(g.faction, ctx, &g, env.site, false);
	v.attitude = at.value;
	v.regard = at.regard;

	const bool menacing = ctx.Has(PlayerFlag::Armed) || ctx.Has(PlayerFlag::Sneaking) || ctx.Has(PlayerFlag::BeastForm);
	const float jitter = (float(Mix64(g.id ^ (std::uint64_t(DayOf(env.now)) << 20)) % 1000ULL) / 1000.f - 0.5f) * 0.12f;

	switch (at.regard) {
	case Regard::Hostile:
		v.call = env.tooHurt ? Call::Withdraw : Call::Attack;
		v.why = env.tooHurt ? "hostile but too hurt to fight" : "regards the player as an enemy";
		return v;

	case Regard::Unfriendly: {
		if (env.tooHurt) { v.call = Call::Withdraw; v.why = "wary and too hurt to risk it"; return v; }
		if (g.tollUntil > env.now) { v.call = Call::Ignore; v.why = "paid for passage"; return v; }
		const float depth = std::clamp((-at.value + sc.unfriendlyAt) / std::max(1.f, -sc.hostileAt + sc.unfriendlyAt), 0.f, 1.f);
		const float terr = sc.territorial[fi];

		if (sc.lawful[fi]) {
			// soldiers and watchers challenge armed strangers they distrust; they never hunt them down
			const float range = sc.noticeRange * (env.inTerritory ? 1.3f : 1.f);   // they spot an armed stranger from afar and walk up to him
			if (menacing && env.dist <= range) {
				if (env.challengeElapsed >= sc.challengeGrace) { v.call = Call::Attack; v.why = "ignored a challenge"; }
				else { v.call = Call::Challenge; v.why = "distrusts an armed stranger"; }
			} else { v.call = Call::Watch; v.why = "keeps a wary eye"; }
			return v;
		}

		// trespass: territorial squads defend their ground against anyone they dislike
		if (terr > 0.f && env.inTerritory) {
			if (env.challengeElapsed >= sc.challengeGrace / std::max(0.3f, terr)) { v.call = Call::Attack; v.why = "intruder did not leave"; return v; }
			if (env.dist <= sc.challengeRange * 1.6f) { v.call = Call::Challenge; v.why = "intruder on its ground"; return v; }
		}

		// predation: weigh the odds
		const float opp = sc.opportunism[fi];
		const float adv = Advantage(env.squadPower, ctx);
		const float sat = std::clamp((adv - sc.advantageFloor) / std::max(0.01f, sc.advantageFull - sc.advantageFloor), 0.f, 1.f);
		float desire = opp * sat;
		if (env.hungry) desire += sc.hungerBonus * opp;
		desire += sc.wealthBonus * ctx.wealth * opp;
		if (sc.nocturnal[fi] && ctx.Has(PlayerFlag::Night)) desire += sc.nightBonus;
		if (opp > 0.f) desire -= sc.fearPenalty * std::clamp(state.notoriety / 600.f, 0.f, 1.f);
		desire *= 0.4f + 0.6f * depth;
		desire = std::clamp(desire + (opp > 0.f ? jitter : 0.f), 0.f, 1.f);
		v.desire = desire;

		if (env.parleyRefused && desire >= sc.parleyFrom * 0.5f) { v.call = Call::Attack; v.why = "terms refused"; return v; }
		if (desire >= sc.ambushAt && env.dist <= sc.noticeRange) { v.call = Call::Attack; v.why = "sees an easy mark"; return v; }
		if (desire >= sc.attackAt) {
			if (sc.parley[fi] && env.dist <= sc.noticeRange) { v.call = Call::Parley; v.why = "wants a toll first"; return v; }
			if (env.dist <= sc.noticeRange) { v.call = Call::Attack; v.why = "likes the odds"; return v; }
		}
		if (desire >= sc.parleyFrom && sc.parley[fi] && env.dist <= sc.noticeRange) { v.call = Call::Parley; v.why = "sizing up a mark"; return v; }
		v.call = Call::Watch;
		v.why = opp > 0.f && sat <= 0.f ? "does not like the odds" : "keeps its distance";
		return v;
	}

	case Regard::Neutral:
		v.call = Call::Ignore;
		v.why = "goes about its business";
		return v;

	case Regard::Friendly:
		if (!env.greeted && env.dist <= sc.greetRange && !menacing) { v.call = Call::Greet; v.why = "knows a friendly face"; }
		else { v.call = Call::Ignore; v.why = "friendly"; }
		return v;

	case Regard::Allied:
		v.call = Call::Assist;
		v.why = "stands with the player";
		return v;
	}
	return v;
}

// --------------------------------------------------------------------------------------------- deeds
void Standing::Gain(Faction f, float amount)
{
	const auto& sc = m_cfg->standing;
	auto& today = state.gainedToday[int(f)];
	const float a = std::min(amount, std::max(0.f, sc.dailyGainCap - today));
	state.goodwill[int(f)] = std::min(sc.goodwillMax, state.goodwill[int(f)] + a);
	today += a;
}

void Standing::Deed(Faction f, float delta, bool spill, double)
{
	const auto& sc = m_cfg->standing;
	auto apply = [&](Faction who, float d) {
		if (d < 0.f) state.goodwill[int(who)] = std::max(-sc.goodwillMax, state.goodwill[int(who)] + d);
		else if (d > 0.f) Gain(who, d);
	};
	apply(f, delta);
	if (!spill) return;
	for (int o = 0; o < kHumanFactionCount; ++o) {   // goodwill never spills over to beasts
		if (o == int(f)) continue;
		const int r = m_cfg->rel[int(f)][o];
		float share = 0.f;
		if (r >= 1000) share = sc.allyShare;
		else if (r >= 400) share = sc.friendShare;
		else if (r <= -1000) share = -sc.enemyShare;
		else if (r <= -400) share = -sc.enemyShare * 0.5f;
		if (share != 0.f) apply(Faction(o), delta * share);
	}
}

void Standing::OnKill(Group& g, const Member& victim, bool witnessed, bool provoked, double now)
{
	const auto& sc = m_cfg->standing;
	float sev = sc.killBase + sc.killPerLevel * float(victim.level) + (victim.veteran ? sc.killVeteran : 0.f);
	if (provoked) sev *= sc.killProvokedMult;
	g.memory = std::max(-1000.f, g.memory + sc.killMemory * (provoked ? 0.5f : 1.f));
	g.foughtPlayer = true;

	if (witnessed) {
		Deed(g.faction, -sev, true, now);
	} else {
		Deed(g.faction, -sev * sc.unseenMult, true, now);
		Rng rng(Mix64(g.id ^ victim.id ^ std::uint64_t(now * 10.0)));
		if (rng.chance(sc.discoverChance))   // a patrol finds the bodies later
			state.rumors.push_back({ g.faction, -sev * sc.discoverMult, now + double(rng.uniform(sc.discoverMinHours, sc.discoverMaxHours)) });
	}

	const int fi = int(g.faction);
	if (!provoked && sc.opportunism[fi] <= 0.f)   // killing decent folk who did nothing makes a murderer
		state.notoriety = std::min(1000.f, state.notoriety + sev * sc.notorietyPerSeverity * sc.murderWeight[fi] * (witnessed ? 1.f : 0.3f));
	else if (sc.opportunism[fi] > 0.f)             // killing outlaws is welcome news
		state.notoriety = std::max(0.f, state.notoriety - sev * 0.1f);
}

void Standing::OnAttack(Group& g)
{
	const auto& sc = m_cfg->standing;
	g.memory = std::max(-1000.f, g.memory + sc.attackMemory);
	g.attackedByPlayer = true;
	Deed(g.faction, -sc.attackGoodwill, false, 0);
}

void Standing::OnHelp(Faction who, int foeLevel, double now)
{
	const auto& sc = m_cfg->standing;
	Deed(who, sc.helpBase + sc.helpPerLevel * float(foeLevel), true, now);
}

void Standing::OnTrade(Faction f, int gold, double now)
{
	const auto& sc = m_cfg->standing;
	if (gold > 0) Deed(f, float(gold) / 100.f * sc.tradePer100Gold, false, now);
}

void Standing::OnTask(Faction f, float delta)
{
	// Anomaly: complete_task_inc_goodwill / fail_task_dec_goodwill touch the giver's community alone
	const float m = m_cfg->standing.goodwillMax;
	state.goodwill[int(f)] = std::clamp(state.goodwill[int(f)] + delta, -m, m);
}

void Standing::OnToll(Group& g, double now)
{
	const auto& sc = m_cfg->standing;
	g.tollUntil = now + double(sc.tollPassHours);
	g.memory = std::min(1000.f, g.memory + sc.tollMemory);
}

// The wounded and the surrendered (docs/WOUNDED.md s3-4). Anomaly: transfer_medkit makes an enemy neutral and anyone else a friend,
// wounded_helped +10 reputation; killed_prisoners -25 reputation (xr_motivator.script:341). The amounts come from wounded.ltx
// [standing] through WoundConfig (one read path for the difficulty scaling), so goodwill and memory are passed in.
void Standing::OnWoundHelped(Group& g, float goodwill, float memory, double now)
{
	Deed(g.faction, std::max(0.f, goodwill), true, now);
	g.memory = std::clamp(g.memory + std::max(0.f, memory), -1000.f, 1000.f);
	g.helpedByPlayer = true;
}

void Standing::OnRob(Group& g, bool hostile, float goodwill, float memory, double now)
{
	if (hostile) return;   // looting someone who attacked you costs nothing
	Deed(g.faction, -std::max(0.f, goodwill), false, now);
	g.memory = std::clamp(g.memory - std::max(0.f, memory), -1000.f, 1000.f);
}

void Standing::OnFinish(Group& g, float goodwill, double now)
{
	Deed(g.faction, -std::max(0.f, goodwill), true, now);
}

void Standing::OnSpare(Group& g, float goodwill, float memory, double now)
{
	Deed(g.faction, std::max(0.f, goodwill), false, now);
	g.memory = std::clamp(g.memory + std::max(0.f, memory), -1000.f, 1000.f);
	++g.letGoCount;
}

void Standing::OnPrisonerKilled(Group& g, float goodwill, float notoriety, double now)
{
	Deed(g.faction, -std::max(0.f, goodwill), true, now);
	state.notoriety = std::clamp(state.notoriety + notoriety, 0.f, 1000.f);   // even when its faction is hostile
}

void Standing::Tick(double now, double dt)
{
	const auto& sc = m_cfg->standing;
	const std::uint32_t day = DayOf(now);
	if (day != state.gainDay) { state.gainedToday.fill(0.f); state.gainDay = day; }
	if (dt > 0.0) {
		for (int i = 0; i < kFactionCount; ++i) {
			auto& gw = state.goodwill[i];
			const float hl = gw >= 0.f ? sc.halfLifePosDays : sc.halfLifeNegDays;
			gw *= float(std::pow(0.5, dt / (24.0 * double(std::max(1.f, hl)))));
			if (std::fabs(gw) < 0.5f) gw = 0.f;
		}
		state.notoriety *= float(std::pow(0.5, dt / (24.0 * double(std::max(1.f, sc.notorietyHalfLifeDays)))));
		if (state.notoriety < 0.5f) state.notoriety = 0.f;
	}
	for (std::size_t i = 0; i < state.rumors.size();) {
		if (state.rumors[i].dueHours <= now) {
			Deed(state.rumors[i].faction, state.rumors[i].delta, true, now);
			state.rumors.erase(state.rumors.begin() + std::ptrdiff_t(i));
		} else ++i;
	}
}

void Standing::DecayMemory(Group& g, double dt) const
{
	if (g.memory == 0.f || dt <= 0.0) return;
	g.memory *= float(std::pow(0.5, dt / (24.0 * double(std::max(1.f, m_cfg->standing.memoryHalfLifeDays)))));
	if (std::fabs(g.memory) < 1.f) g.memory = 0.f;
}

std::string Standing::Describe(Faction f, const PlayerContext& ctx, const Site& site) const
{
	const Attitude a = Evaluate(f, ctx, nullptr, site, false);
	char buf[96];
	std::snprintf(buf, sizeof(buf), "%s: %s (%+d)", FactionName(f), RegardName(a.regard), int(std::lround(a.value)));
	return buf;
}

}  // namespace ALife
