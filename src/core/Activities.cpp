// What squads do while they idle at a place: the analogue of Anomaly's smart terrain "jobs".
// Decisions and outcome math live here (headless testable); the Skyrim effects (vendor chests, loot lists, killing vanilla
// inhabitants when the player arrives) are Host callbacks.
#include "ALife/Board.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace ALife {

namespace {
	std::string Fmt(const char* f, ...)
	{
		char b[384];
		va_list ap;
		va_start(ap, f);
		std::vsnprintf(b, sizeof(b), f, ap);
		va_end(ap);
		return b;
	}
	bool Huntable(Kind k)
	{
		return k == Kind::Farm || k == Kind::Mill || k == Kind::AnimalDen || k == Kind::Wilderness || k == Kind::Shipwreck;
	}
	bool Camp(Kind k)
	{
		return k == Kind::Fort || k == Kind::MilitaryCamp || k == Kind::BanditCamp || k == Kind::ForswornCamp;
	}
}

// ------------------------------------------------------------------ needs
Board::SquadNeeds Board::NeedsOf(const Group& g) const
{
	SquadNeeds n;
	float hs = 0, fs = 0, cs = 0, hp = 0;
	int c = 0;
	for (auto& m : g.members) {
		if (!m.alive()) continue;
		hs += m.hunger; fs += m.fatigue; cs += m.cold;
		hp += m.health / std::max(1.f, m.healthMax);
		++c;
	}
	if (!c) return n;
	n.food = hs / float(c) >= m_cfg.hungryAt && !m_host.HasFood(g);
	n.rest = fs / float(c) >= m_cfg.tiredAt;
	n.warm = m_host.SurvivalOn() && cs / float(c) >= m_cfg.coldAt;
	n.heal = hp / float(c) < 0.55f;
	n.sell = m_cfg.doesTrade[int(g.faction)] && m_host.HasSellables(g);
	n.supplies = m_cfg.doesTrade[int(g.faction)] && m_host.NeedsSupplies(g);
	return n;
}

// Extra target priority from needs: hungry squads head for markets, hunting grounds, fishing spots (or, if they are
// raiders, for something to take); tired ones for inns; cold ones for shelter; loaded ones for a buyer; hurt ones for care.
float Board::NeedBias(const Group& g, const GraphNode& t) const
{
	if (m_needsGid != g.id || m_needsStep != stats.steps) {
		m_needsCache = NeedsOf(g);
		m_needsGid = g.id;
		m_needsStep = stats.steps;
	}
	const SquadNeeds& nd = m_needsCache;
	if (!(nd.food || nd.rest || nd.warm || nd.sell || nd.supplies || nd.heal)) return 0.f;
	const int f = int(g.faction);
	const bool market = m_cfg.doesTrade[f] && t.vendors > 0;
	const bool shelter = t.city || t.settlement || t.inn || Camp(t.kind);
	float b = 0.f;
	if (nd.food) {
		if (market) b += m_cfg.needBiasFood;
		if (m_cfg.doesHunt[f] && Huntable(t.kind)) b += 0.7f * m_cfg.needBiasFood;
		if (m_cfg.doesFish[f] && t.water && m_host.FishingAvailable()) b += 0.7f * m_cfg.needBiasFood;
		if (m_cfg.doesRaidSettlements[f] && t.settlement && !t.city) b += m_cfg.needBiasFood;
	}
	if (nd.rest) {
		if (t.inn) b += m_cfg.needBiasRest;
		else if (market) b += 0.6f * m_cfg.needBiasRest;
		else if (Camp(t.kind)) b += 0.4f * m_cfg.needBiasRest;
	}
	if (nd.warm && shelter) b += m_cfg.needBiasWarm;
	if ((nd.sell || nd.supplies) && market) b += m_cfg.needBiasSell;
	if (nd.heal && shelter) b += m_cfg.needBiasHeal;
	return b;
}

// ------------------------------------------------------------------ choosing what to do at a place
Act Board::ChooseActivity(Group& g, GraphNode& n)
{
	const SquadNeeds nd = NeedsOf(g);
	const int f = int(g.faction);
	const SmartState& st = smart[n.id];
	const float hungry = nd.food ? 1.f : 0.f;
	std::array<float, 7> w{};
	w[int(Act::Rest)] = 0.5f + (nd.rest ? 3.f : 0.f) + (nd.heal ? 2.f : 0.f) + (n.inn ? 1.f : 0.f);
	if (m_cfg.doesTrade[f] && n.vendors > 0 && m_host.VendorsOpen(n, now))
		w[int(Act::Trade)] = 0.8f + (nd.sell ? 2.f : 0.f) + (nd.supplies ? 2.f : 0.f) + 2.f * hungry;
	if (m_cfg.doesHunt[f] && Huntable(n.kind))
		w[int(Act::Hunt)] = 0.6f + 2.5f * hungry + (g.faction == Faction::Hunter ? 2.5f : 0.f);
	if (m_cfg.doesFish[f] && n.water && m_host.FishingAvailable())
		w[int(Act::Fish)] = 0.5f + 2.f * hungry + (g.faction == Faction::Hunter ? 1.5f : 0.f);
	if (m_cfg.doesRaidSettlements[f] && n.settlement && n.raidable && !n.city && now >= st.raidedUntil &&
	    !(st.faction != kNoFaction && m_cfg.IsAlly(g.faction, Faction(st.faction))))
		w[int(Act::RaidSettlement)] = 2.f + 2.f * hungry;
	if (m_cfg.doesClearDungeons[f] && n.dungeon && !n.cells.empty() && now >= st.raidedUntil && g.livingCount() >= 2 &&
	    !(st.faction != kNoFaction && m_cfg.IsAlly(g.faction, Faction(st.faction))))
		w[int(Act::RaidDungeon)] = 2.f;
	float tot = 0;
	for (auto x : w) tot += x;
	float roll = rng.frac() * tot;
	for (int i = 1; i < int(w.size()); ++i) {
		if (roll < w[i]) return Act(i);
		roll -= w[i];
	}
	return Act::Rest;
}

void Board::SendOnItsWay(Group& g)
{
	g.stayStart = now - g.idleHours - 1.0;   // the stay is over: the next update picks a new target
}

void Board::WoundSquad(Group& g, float chance, float lo, float hi)
{
	for (auto& m : g.members) {
		if (!Stands(m)) continue;   // the down lie out of the raid (docs/WOUNDED.md)
		if (rng.chance(chance)) m.health -= rng.uniform(lo, hi);
		if (m.alive() && m.health < m.healthMax * 0.4f) m_host.Consume(m);   // drink a potion when badly hurt
	}
	Reap(g);
}

void Board::Reap(Group& g)
{
	for (auto& m : g.members) if (!m.alive()) MemberDied(g, m, nullptr, nullptr);
	g.members.erase(std::remove_if(g.members.begin(), g.members.end(), [](const Member& m) { return !m.alive(); }), g.members.end());
}

// ------------------------------------------------------------------ settlement raids (bandits, Forsworn ...)
void Board::ResolveSettlementRaid(Group& g, GraphNode& n)
{
	SmartState& st = smart[n.id];
	// an enemy garrison idling here is the offline combat's business: no raid until it is gone
	for (auto& s : squads) {
		if (&s == &g || s.livingCount() == 0 || s.action != Action::Stay || s.currentTarget != std::int64_t(n.id)) continue;
		if (m_cfg.IsEnemy(g.faction, s.faction)) { g.activity = Act::Rest; return; }
	}
	const float power = SquadPower(g);
	const float defense = m_host.DefenderStrength(n, g.faction);
	const float ratio = power / (power + defense + 1.f);
	const bool success = rng.frac() < 0.2f + 0.75f * ratio;
	st.raidedUntil = now + double(success ? m_cfg.settlementRaidCooldownDays : 2.f) * 24.0;
	if (success) {
		WoundSquad(g, 0.2f, 10.f, 15.f + 40.f * (1.f - ratio));   // defenders fight back
		stats.raidsWon++;
		g.activity = Act::Rest;
	} else {
		WoundSquad(g, 0.8f, 20.f, 25.f + 60.f * (1.f - ratio));
		stats.raidsLost++;
		if (g.livingCount() > 0) {   // driven off
			g.retreatUntilHours = now + rng.uniform(3.f, 6.f);
			g.avoidNode = n.id;
			AssignToSmart(g, -1);
			g.assignedTarget = g.currentTarget = -1;
			g.action = Action::None;
			g.locked = false;
			g.activity = Act::None;
		}
	}
	m_host.DoRaidSettlement(g, n, success, ratio);
}

// ------------------------------------------------------------------ dungeon clearing (adventurers, mercenaries ...)
// One section every raid_section_hours. Each section's kills are queued against its interior cell and applied to vanilla
// hostiles when the player next walks in; the boss section is never touched (protected vanilla actor): the squad takes the
// haul from the cleared sections and leaves.
void Board::StepDungeonClear(Group& g, GraphNode& n, double a_hours)
{
	SmartState& st = smart[n.id];
	const int sections = std::max(1, int(n.cells.size()) - (n.bossCell ? 1 : 0));
	g.actHours += float(a_hours);
	while (g.actHours >= m_cfg.raidSectionHours && g.activity == Act::RaidDungeon) {
		g.actHours -= m_cfg.raidSectionHours;
		if (g.actStep >= sections) {   // cleared up to the boss
			m_host.DoHaul(g, n);
			st.raidedUntil = now + double(m_cfg.dungeonResetDays) * 24.0;
			stats.dungeonsCleared++;
			m_host.News(Fmt("%s cleared %s", FactionName(g.faction), n.name.c_str()), n.x, n.y);
			g.activity = Act::Rest;
			SendOnItsWay(g);
			return;
		}
		const int kills = 1 + rng.index(std::max(1, g.livingCount()));
		m_host.DoClearSection(g, n, g.actStep, kills);
		WoundSquad(g, 0.25f, 10.f, 40.f);
		if (g.livingCount() == 0) return;
		if (g.livingCount() * 2 < g.startMembers) {   // too many losses: pull out
			st.raidedUntil = now + 2.0 * 24.0;
			stats.dungeonsAborted++;
			g.activity = Act::Rest;
			SendOnItsWay(g);
			return;
		}
		++g.actStep;
	}
}

// ------------------------------------------------------------------ the per-step driver
void Board::RunActivity(Group& g, GraphNode& n, double h, bool fresh)
{
	if (fresh && g.activity == Act::None) {
		g.activity = ChooseActivity(g, n);
		g.actStep = 0;
		g.actHours = 0.f;
		m_host.Log(Fmt("[act] %s #%llu at %s: %s", FactionId(g.faction), (unsigned long long)g.id, n.name.c_str(), ActName(g.activity)));
	}
	const float hh = float(h);
	switch (g.activity) {
	case Act::Trade:
		m_host.DoTrade(g, n);
		stats.trades++;
		g.activity = Act::Rest;
		break;
	case Act::Hunt:
		if (g.actStep == 0) { ++stats.hunts; g.actStep = 1; }
		m_host.DoHunt(g, h, rng);
		Reap(g);
		break;
	case Act::Fish:
		if (g.actStep == 0) { ++stats.fishings; g.actStep = 1; }
		m_host.DoFish(g, h, rng);
		Reap(g);
		break;
	case Act::RaidSettlement:
		ResolveSettlementRaid(g, n);
		break;
	case Act::RaidDungeon:
		StepDungeonClear(g, n, h);
		break;
	case Act::Rest:
	case Act::None:
	default: {
		const float inn = n.inn ? 2.f : 1.f;
		for (auto& m : g.members) {
			if (!Stands(m)) continue;   // the down get up only through Wounds
			m.fatigue = std::max(0.f, m.fatigue - 140.f * hh * inn);
			m.cold = std::max(0.f, m.cold - 250.f * hh);
			m.health = std::min(m.healthMax, m.health + m.healthMax * 0.02f * hh * inn);
		}
		break;
	}
	}
}

}  // namespace ALife
