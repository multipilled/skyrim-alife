#include "ALife/Board.h"

#include <algorithm>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <set>

namespace ALife {

namespace {
	constexpr std::uint8_t kController = 254;   // RespawnParam::faction: the smart's current controlling faction

	std::string Fmt(const char* f, ...)
	{
		char b[512];
		va_list ap;
		va_start(ap, f);
		std::vsnprintf(b, sizeof(b), f, ap);
		va_end(ap);
		return b;
	}
}

Board::Board(const Config& a_cfg, World& a_world, Host& a_host) : m_cfg(a_cfg), m_world(a_world), m_host(a_host)
{
	smart.assign(m_world.Size(), SmartState{});
}

void Board::Reset(double a_now)
{
	squads.clear();
	smart.assign(m_world.Size(), SmartState{});
	for (std::size_t i = 0; i < m_world.Size(); ++i) {
		smart[i].spawned.assign(m_world.nodes[i].respawn.size(), 0);
		smart[i].faction = m_world.nodes[i].defaultFaction;
	}
	now = a_now;
	nextId = 1;
	stats = Stats{};
	m_sosUntil.clear();
	m_battleNewsUntil.clear();
	m_deathNewsUntil.fill(0);
	m_nextGossip = -1;
	m_lastSos = m_lastBattleNews = m_lastDeathNews = -1e9;
	rng = Rng(Mix64(std::uint64_t(a_now * 100) + 7));
	worldSeed = Mix64(std::uint64_t(a_now * 100) + 99);
}

Group* Board::Find(std::uint64_t a_id)
{
	for (auto& g : squads) if (g.id == a_id) return &g;
	return nullptr;
}

int Board::SquadCount(Faction a_f) const
{
	int n = 0;
	for (auto& g : squads) n += (g.faction == a_f && g.livingCount() > 0) ? 1 : 0;
	return n;
}

int Board::MemberCount(Faction a_f) const
{
	int n = 0;
	for (auto& g : squads) if (g.faction == a_f) n += g.livingCount();
	return n;
}

std::string Board::PlaceName(std::uint32_t a_node) const
{
	auto* n = m_world.Node(a_node);
	if (!n) return "the wilds";
	if (!n->name.empty()) return n->name;
	return std::string(KindName(n->kind)) + " #" + std::to_string(n->id);
}

std::string Board::SquadLabel(const Group& a_g) const
{
	return std::string(FactionName(a_g.faction));
}

std::string Board::DescribeSquad(const Group& a_g) const
{
	const GraphNode* at = m_world.Node(a_g.node);
	const GraphNode* tgt = a_g.assignedTarget >= 0 ? m_world.Node(std::uint32_t(a_g.assignedTarget)) : nullptr;
	return Fmt("%s %s #%llu x%d L%.0f at %s -> %s [%s%s]", FactionId(a_g.faction), TierName(a_g.tier), (unsigned long long)a_g.id,
		a_g.livingCount(), a_g.avgLevel(), at ? PlaceName(at->id).c_str() : "?", tgt ? PlaceName(tgt->id).c_str() : "-",
		a_g.action == Action::Reach ? "reach" : a_g.action == Action::Stay ? "stay" : "none", a_g.locked ? ",locked" : "");
}

// ------------------------------------------------------------------ population accounting
void Board::RecomputePopulation()
{
	for (auto& s : smart) { s.population = 0; s.stayed = 0; s.monsters = 0; }
	for (auto& g : squads) {
		if (g.smartAssigned >= 0 && std::size_t(g.smartAssigned) < smart.size()) {
			++smart[g.smartAssigned].population;
			if (IsMonster(g)) ++smart[g.smartAssigned].monsters;
		}
		if (g.action == Action::Stay && g.currentTarget >= 0 && std::size_t(g.currentTarget) < smart.size() && g.livingCount() > 0)
			++smart[g.currentTarget].stayed;
	}
}

void Board::AssignToSmart(Group& a_g, std::int64_t a_smart)
{
	const bool monster = IsMonster(a_g);
	if (a_g.smartAssigned >= 0 && std::size_t(a_g.smartAssigned) < smart.size()) {
		auto& s = smart[a_g.smartAssigned];
		s.population = std::max(0, s.population - 1);
		if (monster) s.monsters = std::max(0, s.monsters - 1);
	}
	a_g.smartAssigned = a_smart;
	if (a_smart >= 0 && std::size_t(a_smart) < smart.size()) {
		++smart[a_smart].population;
		if (monster) ++smart[a_smart].monsters;
	}
}

float Board::MemberEff(const Member& m) const
{
	if (!m.alive()) return 0.f;
	const float frac = std::clamp(m.health / std::max(1.f, m.healthMax), 0.05f, 1.f);
	return std::max(1.f, PowerOf(m)) * frac;
}

float Board::PowerOf(const Member& m) const
{
	if (!m_cfg.IsMonster(m.faction)) return m_host.MemberPower(m);
	const CreatureDef* d = m_cfg.CreatureOf(m);
	if (!d) return m_host.MonsterPower(m, 100.f);
	const float dmg = d->damage + d->damagePerLevel * float(std::max(0, m.level - 1));
	return m_host.MonsterPower(m, std::sqrt(std::max(1.f, dmg * d->hit) * std::max(1.f, m.healthMax)) * 10.f);
}

void Board::MakeCreature(Member& m, const CreatureDef& d, int a_level) const
{
	m.level = std::max(1, a_level);
	m.name = d.name;
	m.classId = d.form;
	m.healthMax = m.health = d.health + d.healthPerLevel * float(m.level - 1);
	m.magickaMax = m.magicka = 0.f;
	m.staminaMax = m.stamina = 100.f;
	m.gearLevel = m.level;
}

bool Board::MonsterTerritoryOpen(Faction f, float hour) const
{
	switch (m_cfg.monster[int(f)]) {
	case MonsterType::PredatoryDay:
	case MonsterType::ZombiedDay: return InTimeInterval(hour, m_cfg.monsterDayFrom, m_cfg.monsterDayTo);
	case MonsterType::PredatoryNight:
	case MonsterType::ZombiedNight: return InTimeInterval(hour, m_cfg.monsterNightFrom, m_cfg.monsterNightTo);
	case MonsterType::Vegetarian:
	case MonsterType::Zombied: return true;   // Anomaly: grazers and the zombified roam at any hour
	default: return false;
	}
}

bool Board::MonsterMayComeOnline(const Group& g, float hour) const
{
	switch (m_cfg.monster[int(g.faction)]) {
	case MonsterType::PredatoryDay:
	case MonsterType::ZombiedDay: return !InTimeInterval(hour, m_cfg.dayOfflineFrom, m_cfg.dayOfflineTo);
	case MonsterType::PredatoryNight:
	case MonsterType::ZombiedNight: return !InTimeInterval(hour, m_cfg.nightOfflineFrom, m_cfg.nightOfflineTo);
	default: return true;
	}
}

bool Board::MonsterRoom(const GraphNode& t, const SmartState& st, const Group& g) const
{
	if (t.monsterCap <= 0) return true;
	const int others = st.monsters - (g.smartAssigned == std::int64_t(t.id) ? 1 : 0);
	return others < t.monsterCap;
}

int Board::MonsterSquads() const
{
	int n = 0;
	for (auto& g : squads) n += (IsMonster(g) && g.livingCount() > 0) ? 1 : 0;
	return n;
}

float Board::SquadPower(const Group& a_g) const
{
	float s = 0;
	for (auto& m : a_g.members) s += MemberEff(m);
	return s;
}

// ------------------------------------------------------------------ targeting (sim_board / simulation_objects / smart_terrain)
bool Board::TargetPrecondition(const GraphNode& t, const SmartState& st, const Group& g, bool a_ignorePopulation) const
{
	if (t.disabled || !t.simAvail) return false;
	if (!a_ignorePopulation && st.population >= t.maxPopulation) return false;
	const Props& p = t.props;
	const float coeff = p.byFaction[int(g.faction)];
	const float hour = HourOfDay(now);
	if (IsMonster(g)) {
		// Anomaly smart_terrain.target_precondition, mutant branch: all_monster or the own key; lairs always (they fill first),
		// territories only inside the species' hours. Monsters ignore bases and resources.
		if (!(p.monster > 0.f || coeff > 0.f)) return false;
		if (!a_ignorePopulation && !MonsterRoom(t, st, g)) return false;   // one pack per den: the overflow moves on to empty lairs and territories
		if (p.lair > 0.f) return true;
		return p.territory > 0.f && MonsterTerritoryOpen(g.faction, hour);
	}
	if (!(p.all > 0.f || coeff > 0.f)) return false;
	if (surgeActive) return p.surge > 0.f;   // emission analogue: only shelters are valid
	// controlling faction of the place; Anomaly general_*_precondition treats "nobody" and "my own" as open
	const bool openToMe = st.faction == kNoFaction || st.faction == std::uint8_t(g.faction);
	const bool enemyHeld = !openToMe && m_cfg.IsEnemy(g.faction, Faction(st.faction));
	if (p.base > 0.f) {
		if (openToMe || !enemyHeld || InTimeInterval(hour, m_cfg.baseRaidFrom, m_cfg.baseRaidTo)) return true;
	}
	if (p.resource > 0.f) {
		if (InTimeInterval(hour, m_cfg.resourceFrom, m_cfg.resourceTo)) return true;
	}
	if (p.territory > 0.f) {
		if (openToMe || !enemyHeld || InTimeInterval(hour, m_cfg.territoryRaidFrom, m_cfg.territoryRaidTo)) return true;
	}
	// Anomaly: "lair" only matters to mutant squads (general_lair_precondition), never to stalker-type squads. ALife extension
	// (docs/MUTANTS.md): lair-hunting factions go after a lair while a monster squad holds it, for the parts.
	if (m_cfg.huntsLairs[int(g.faction)] && p.lair > 0.f && MonsterHeld(t.id)) return true;
	return false;
}

float Board::EvaluatePrior(const GraphNode& t, const SmartState& st, const Group& g) const
{
	if (t.disabled || !t.simAvail) return 0.f;
	if (st.population >= t.maxPopulation) return 0.f;
	if (IsMonster(g) && !MonsterRoom(t, st, g)) return 0.f;
	const Props& p = t.props;
	float prior = 5.f;
	prior += p.byFaction[int(g.faction)];   // props[squad.player_id] * 1
	if (IsMonster(g)) {   // default_monster_behaviour: lair = 1, monster = 1, territory = 1
		prior += m_cfg.mLair * p.lair + m_cfg.mMonster * p.monster + m_cfg.mTerritory * p.territory + m_cfg.mBase * p.base +
		         m_cfg.mResource * p.resource;
		if (std::int64_t(t.id) == g.respawnSmart) prior += m_cfg.mHome;
	} else {
		prior += NeedBias(g, t);   // hungry / tired / loaded squads are drawn to markets, inns and camps
		prior += m_cfg.wAll * p.all + m_cfg.wBase * p.base + m_cfg.wLair * p.lair + m_cfg.wResource * p.resource +
		         m_cfg.wSurge * p.surge + m_cfg.wTerritory * p.territory;
		if (m_cfg.huntsLairs[int(g.faction)] && p.lair > 0.f && MonsterHeld(t.id)) prior += m_cfg.wHuntLair;
	}
	// 1 + 1/dist in Anomaly (dist >= 1 m) -> between 1 and 2; bias is the distance at which the bonus reaches 2x
	const float dist = std::max(m_world.PathDist(g.node, t.id), m_cfg.distanceBias);
	prior *= 1.f + m_cfg.distanceBias / dist;
	return prior;
}

const GraphNode* Board::GetSquadTarget(const Group& g)
{
	struct Cand { float prior; const GraphNode* n; };
	std::vector<Cand> top;
	top.reserve(std::size_t(m_cfg.topTargets) + 1);
	const bool fleeing = now < g.retreatUntilHours && g.avoidNode >= 0;
	const GraphNode* avoid = fleeing ? m_world.Node(std::uint32_t(g.avoidNode)) : nullptr;
	for (auto& n : m_world.nodes) {
		const SmartState& st = smart[n.id];
		const float prior = EvaluatePrior(n, st, g);
		if (prior <= 0.f) continue;
		if (!TargetPrecondition(n, st, g, false)) continue;
		if (avoid) {
			float dx = n.x - avoid->x, dy = n.y - avoid->y;
			if (n.id == avoid->id || dx * dx + dy * dy < 9000.f * 9000.f) continue;
		}
		if (int(top.size()) < m_cfg.topTargets) {
			top.push_back({ prior, &n });
		} else {
			auto lo = std::min_element(top.begin(), top.end(), [](const Cand& a, const Cand& b) { return a.prior < b.prior; });
			if (prior > lo->prior) *lo = { prior, &n };
		}
	}
	if (top.empty()) return nullptr;
	if (rng.chance(m_cfg.bestChance)) {
		auto best = std::max_element(top.begin(), top.end(), [](const Cand& a, const Cand& b) { return a.prior < b.prior; });
		return best->n;
	}
	return top[std::size_t(rng.index(int(top.size())))].n;
}

// ------------------------------------------------------------------ squad creation / respawn
Group* Board::CreateSquad(std::uint32_t a_smart, const std::string& a_section, bool a_register)
{
	const SquadDef* def = m_cfg.Squad(a_section);
	const GraphNode* n = m_world.Node(a_smart);
	if (!def || !n) return nullptr;
	if (m_cfg.IsMonster(def->faction) && def->creatures.empty()) return nullptr;   // its creatures' plugin is not loaded
	Group g;
	g.id = nextId++;
	g.faction = def->faction;
	g.tier = def->tier;
	g.section = def->section;
	g.commonSquad = def->common;
	g.node = g.prevNode = a_smart;
	g.ws = n->ws;
	g.x = n->x; g.y = n->y; g.z = n->z;
	g.idleHours = rng.uniform(m_cfg.idleMin, m_cfg.idleMax);
	if (m_cfg.IsMonster(def->faction) && n->props.lair > 0.f) g.idleHours = rng.uniform(m_cfg.lairIdleMin, m_cfg.lairIdleMax);
	const int size = rng.range(def->minMembers, def->maxMembers);
	const int base = std::max(1, m_host.PlayerLevel());
	float totalW = 0;
	for (auto w : def->roleWeights) totalW += w;
	const bool monster = m_cfg.IsMonster(def->faction) && !def->creatures.empty();
	float creatureW = 0;
	for (auto& c : def->creatures) creatureW += c.second;
	for (int i = 0; i < size; ++i) {
		const int level = std::max(1, base + rng.range(def->levelOffsetMin, def->levelOffsetMax));
		if (monster) {   // a pack / band: creatures from monsters.ltx, the first one the leader kind when the squad names one
			int ci = def->creatures.front().first;
			if (i == 0 && def->leaderCreature >= 0) ci = def->leaderCreature;
			else {
				float roll = rng.frac() * creatureW;
				for (auto& c : def->creatures) { if (roll < c.second) { ci = c.first; break; } roll -= c.second; }
			}
			Member m;
			m.id = nextId++;
			m.faction = g.faction;
			m.role = i == 0 ? Role::Leader : Role::Melee;
			MakeCreature(m, m_cfg.creatures[std::size_t(ci)], level);
			m.levelBias = level - base;
			g.members.push_back(std::move(m));
			continue;
		}
		Role role = Role::Melee;
		if (i == 0 && def->roleWeights[int(Role::Leader)] > 0.f) role = Role::Leader;
		else {
			float roll = rng.frac() * totalW;
			for (int r = 0; r < int(Role::COUNT); ++r) {
				if (r == int(Role::Leader) && i != 0) continue;
				if (roll < def->roleWeights[r]) { role = Role(r); break; }
				roll -= def->roleWeights[r];
			}
		}
		g.members.push_back(m_host.MakeMember(g.faction, g.tier, role, level, i == 0, rng, nextId++));
		g.members.back().levelBias = level - base;
	}
	g.startMembers = size;
	g.startStrength = SquadPower(g);
	g.smartAssigned = -1;
	squads.push_back(std::move(g));
	Group& ref = squads.back();
	if (a_register) AssignToSmart(ref, a_smart);
	stats.spawned[int(ref.faction)]++;
	return &ref;
}

int Board::MaxCount(std::uint32_t a_smart, std::size_t a_entry) const
{
	const GraphNode& n = m_world.nodes[a_smart];
	const auto& rp = n.respawn[a_entry];
	const bool monsterEntry = rp.faction < kFactionCount && m_cfg.IsMonster(Faction(rp.faction));
	const float q = rp.num * (monsterEntry ? m_cfg.monsterPopulationFactor : m_cfg.populationFactor);
	const int whole = int(std::floor(q));
	const float frac = q - float(whole);
	const std::uint64_t h = Mix64(worldSeed ^ (std::uint64_t(n.location) << 8) ^ std::uint64_t(a_entry + 1) * 0x9E3779B1ULL);
	const float ticket = float(h % 100000ULL) / 100000.f;
	return whole + (ticket < frac ? 1 : 0);
}

void Board::TryRespawn(std::uint32_t i)
{
	const GraphNode& n = m_world.nodes[i];
	SmartState& st = smart[i];
	if (n.respawn.empty() || n.disabled || !n.simAvail) return;
	if (int(squads.size()) >= maxSquads) return;
	if (st.spawned.size() != n.respawn.size()) st.spawned.assign(n.respawn.size(), 0);
	if (m_host.PlayerNear(n.x, n.y, n.respawnRadius, n.ws)) return;   // never respawn in the player's face (Anomaly respawn_radius)
	if (st.lastRespawn >= 0 && now - st.lastRespawn <= n.respawnIdleHours) return;
	st.lastRespawn = now;

	std::vector<int> cand;
	for (std::size_t k = 0; k < n.respawn.size(); ++k) {
		const RespawnParam& rp = n.respawn[k];
		std::uint8_t f = rp.faction;
		if (f == kController) f = st.faction != kNoFaction ? st.faction : n.defaultFaction;
		if (rp.faction == kController && f == kNoFaction) continue;
		if (n.factionControlled && (f == kNoFaction || f != st.faction)) continue;   // controlled: only the controller respawns
		if (MaxCount(i, k) > st.spawned[k]) cand.push_back(int(k));
	}
	if (cand.empty()) return;
	const int k = cand[std::size_t(rng.index(int(cand.size())))];
	const RespawnParam& rp = n.respawn[std::size_t(k)];
	std::uint8_t f = rp.faction == kController ? (st.faction != kNoFaction ? st.faction : n.defaultFaction) : rp.faction;
	std::string section = rp.squads[std::size_t(rng.index(int(rp.squads.size())))];
	if (auto p = section.find("{f}"); p != std::string::npos && f != kNoFaction) section.replace(p, 3, FactionId(Faction(f)));
	Group* g = CreateSquad(i, section);
	if (!g) return;
	g->respawnSmart = i;
	g->respawnEntry = rp.name;
	++st.spawned[std::size_t(k)];
}

void Board::StartFill(const std::array<bool, kFactionCount>* a_only)
{
	const bool a_monstersOnly = a_only != nullptr;
	for (std::uint32_t i = 0; i < m_world.Size(); ++i) {
		const GraphNode& n = m_world.nodes[i];
		SmartState& st = smart[i];
		if (n.respawn.empty()) continue;
		if (!a_monstersOnly || st.spawned.size() != n.respawn.size()) st.spawned.assign(n.respawn.size(), 0);
		if (!a_monstersOnly) st.lastRespawn = now - rng.uniform(0.f, n.respawnIdleHours);
		for (std::size_t k = 0; k < n.respawn.size(); ++k) {
			const RespawnParam& rp = n.respawn[k];
			if (a_only && !(rp.faction < kFactionCount && (*a_only)[rp.faction])) continue;
			std::uint8_t f = rp.faction == kController ? (st.faction != kNoFaction ? st.faction : n.defaultFaction) : rp.faction;
			if (rp.faction == kController && f == kNoFaction) continue;
			if (n.factionControlled && (f == kNoFaction || f != st.faction)) continue;
			// start_fill of the slots this place owns are populated at game start
			const int slots = MaxCount(i, k) - (a_monstersOnly ? st.spawned[k] : 0);
			int count = int(std::floor(float(std::max(0, slots)) * m_cfg.startFill + rng.frac()));
			for (int c = 0; c < count && int(squads.size()) < maxSquads; ++c) {
				std::string section = rp.squads[std::size_t(rng.index(int(rp.squads.size())))];
				if (auto p = section.find("{f}"); p != std::string::npos && f != kNoFaction) section.replace(p, 3, FactionId(Faction(f)));
				Group* g = CreateSquad(i, section);
				if (!g) continue;
				g->respawnSmart = i;
				g->respawnEntry = rp.name;
				++st.spawned[k];
				// start as if they had been idling here for a while, so departures are staggered
				g->action = Action::Stay;
				g->assignedTarget = g->currentTarget = i;
				g->stayStart = now - rng.uniform(0.f, g->idleHours);
				g->arrivedFresh = true;   // pick an activity on the first step
			}
		}
	}
}

void Board::RemoveSquad(Group& g, const char*)
{
	if (g.respawnSmart >= 0 && std::size_t(g.respawnSmart) < smart.size()) {
		auto& st = smart[std::size_t(g.respawnSmart)];
		const auto& n = m_world.nodes[std::size_t(g.respawnSmart)];
		for (std::size_t k = 0; k < n.respawn.size() && k < st.spawned.size(); ++k)
			if (n.respawn[k].name == g.respawnEntry) { st.spawned[k] = std::max(0, st.spawned[k] - 1); break; }
		g.respawnSmart = -1;
	}
	AssignToSmart(g, -1);
	g.action = Action::None;
	g.assignedTarget = g.currentTarget = -1;
	g.members.clear();
}

// ------------------------------------------------------------------ movement and the squad state machine
bool Board::Move(Group& g, const GraphNode& t, double a_hours)
{
	double budget = double(m_cfg.walkSpeed) * a_hours;
	for (int guard = 0; guard < 256; ++guard) {
		if (g.node == t.id) {
			const float dx = t.x - g.x, dy = t.y - g.y;
			const float d = std::sqrt(dx * dx + dy * dy);
			if (d <= budget || d <= t.arriveDist) { g.x = t.x; g.y = t.y; g.z = t.z; return true; }
			g.x += float(dx / d * budget); g.y += float(dy / d * budget);
			return false;
		}
		if (budget <= 0.0) return false;
		const std::uint32_t next = m_world.NextHop(g.node, t.id);
		if (next == g.node) { g.node = t.id; continue; }   // no path (should not happen): snap
		const GraphNode* nn = m_world.Node(next);
		if (!nn) { g.node = t.id; continue; }
		const float dx = nn->x - g.x, dy = nn->y - g.y;
		const float d = std::sqrt(dx * dx + dy * dy);
		if (d <= budget) {
			budget -= d;
			g.x = nn->x; g.y = nn->y; g.z = nn->z;
			g.prevNode = g.node;
			g.node = next;
		} else {
			g.x += float(dx / d * budget); g.y += float(dy / d * budget);
			return false;
		}
	}
	return false;
}

void Board::UpdateSquad(Group& g, double a_hours)
{
	if (g.livingCount() == 0) return;
	if (g.online && IsMonster(g)) return;   // on screen the creatures run their own AI (Anomaly: mutant jobs); the board moves them again once offline
	if (g.action == Action::Stay && g.currentTarget >= 0 && pinned.count(g.id)) return;   // a job holds it at the place (assault)
	if (g.locked) {
		if (now < g.lockedUntil) return;
		g.locked = false;
	}
	const GraphNode* target = g.assignedTarget >= 0 ? m_world.Node(std::uint32_t(g.assignedTarget)) : nullptr;
	if (target) {
		const SmartState& st = smart[target->id];
		if (TargetPrecondition(*target, st, g, true)) {
			if (g.action == Action::Reach) {
				const bool reached = g.online ? m_host.OnlineReached(g, *target) : Move(g, *target, a_hours);
				if (reached) {
					g.currentTarget = g.assignedTarget;
					g.action = Action::Stay;
					g.stayStart = now;
					g.idleHours = IsMonster(g) && target->props.lair > 0.f ? rng.uniform(m_cfg.lairIdleMin, m_cfg.lairIdleMax)
					                                                       : rng.uniform(m_cfg.idleMin, m_cfg.idleMax);
					g.activity = Act::None; g.actStep = 0; g.actHours = 0.f; g.arrivedFresh = true;
					stats.arrivals[int(g.faction)]++;
				}
				return;
			}
			if (g.action == Action::Stay && g.currentTarget == g.assignedTarget) {
				if (now - g.stayStart < g.idleHours) return;   // still idling at the place
			}
		}
	}
	// leaving a place the squad was idling at: let the activity layer wrap up
	if (g.action == Action::Stay && g.currentTarget >= 0) {
		if (GraphNode* here = m_world.Node(std::uint32_t(g.currentTarget))) m_host.OnDepart(g, *here);
		g.activity = Act::None; g.actStep = 0; g.actHours = 0.f; g.arrivedFresh = false;
	}
	// pick a new target (Anomaly generic_update)
	const GraphNode* t = GetSquadTarget(g);
	if (!t) {
		AssignToSmart(g, -1);
		g.assignedTarget = g.currentTarget = -1;
		g.action = Action::None;
		g.locked = true;
		g.lockedUntil = now + m_cfg.retargetHours;
		stats.locks++;
		return;
	}
	AssignToSmart(g, t->id);
	g.assignedTarget = t->id;
	g.currentTarget = -1;
	g.action = Action::Reach;
	stats.retargets++;
}

// ------------------------------------------------------------------ offline combat simulator (Tronex OCS)
void Board::MemberDied(Group& vg, Member& victim, Group* kg, Member* killer)
{
	victim.health = 0;
	stats.membersLost[int(vg.faction)]++;
	if (kg) {
		stats.membersKilled[int(kg->faction)]++;
		stats.killMatrix[int(kg->faction)][int(vg.faction)]++;
		OnKill(kg->faction, vg.faction, vg.tier);
		// hunters skin what they kill: the creature's parts go to the killer (or whoever in the squad still stands)
		if (IsMonster(vg) && !IsMonster(*kg) && m_cfg.harvestsParts[int(kg->faction)])
			if (const CreatureDef* cd = m_cfg.CreatureOf(victim); cd && !cd->parts.empty()) {
				Member* taker = killer && killer->alive() ? killer : nullptr;
				for (auto& m : kg->members) if (!taker && m.alive()) taker = &m;
				if (taker)
					for (auto& part : cd->parts) {
						auto it = std::find_if(taker->inventory.begin(), taker->inventory.end(), [&](const Item& x) { return x.form == part.form; });
						if (it != taker->inventory.end()) it->count += part.count; else taker->inventory.push_back(part);
						stats.partsHarvested[int(kg->faction)] += std::uint64_t(part.count);
					}
			}
	}
	if (killer) ++killer->kills;
	// death report (Anomaly dynamic_news death / named death): commanders and veterans, one per faction per cooldown
	const bool notable = victim.veteran || (!vg.members.empty() && &vg.members.front() == &victim);
	if (m_cfg.news && m_cfg.newsDeaths && !IsMonster(vg) && notable && now >= m_deathNewsUntil[int(vg.faction)] && now - m_lastDeathNews >= m_cfg.deathGapHours) {
		m_deathNewsUntil[int(vg.faction)] = now + m_cfg.deathCooldownHours;
		m_lastDeathNews = now;
		const GraphNode* at = m_world.Node(vg.node);
		const std::string who = victim.name.empty() ? std::string("a ") + FactionName(vg.faction) + " commander" : victim.name + " of the " + FactionName(vg.faction);
		const std::string by = kg ? std::string(", killed by ") + (IsMonster(*kg) ? "" : "the ") + FactionName(kg->faction) : std::string();
		m_host.News(Fmt("Death report: %s fell near %s%s", who.c_str(), PlaceName(vg.node).c_str(), by.c_str()), at ? at->x : vg.x, at ? at->y : vg.y);
		stats.deathNews++;
	}
	m_host.OnMemberKilled(vg, victim, kg, killer);
}

void Board::OnKill(Faction k, Faction v, Tier vt)
{
	if (!m_cfg.dynamicRelations || k == v || !IsHumanFaction(k) || !IsHumanFaction(v)) return;   // monsters are unaffected (Anomaly)
	const int value = int(m_cfg.deathValue * (1.f + 0.5f * float(int(vt))));   // the victim's rank weighs most
	// half the time the victim's enemies warm to the killer, otherwise its friends and neutrals cool towards it; each faction 50%
	const bool enemiesWarm = rng.chance(0.5f);
	for (int f = 0; f < kHumanFactionCount; ++f) {
		if (Faction(f) == k || m_cfg.IsEnemy(Faction(f), v) != enemiesWarm || !rng.chance(0.5f)) continue;
		ChangeRelation(Faction(f), k, enemiesWarm ? value : -value);
	}
}

void Board::ChangeRelation(Faction a, Faction b, int a_delta)
{
	for (auto& [x, y] : m_cfg.lockedPairs)
		if ((x == a && y == b) || (x == b && y == a)) return;
	const int old = m_cfg.relLive[int(a)][int(b)];
	int v = old + a_delta;
	if (v > m_cfg.friendLimit) v = rng.chance(0.5f) ? 0 : m_cfg.friendLimit;   // Anomaly: a pegged relation resets to neutral half of the time
	else if (v < m_cfg.enemyLimit) v = rng.chance(0.5f) ? 0 : m_cfg.enemyLimit;
	m_cfg.relLive[int(a)][int(b)] = m_cfg.relLive[int(b)][int(a)] = v;
	stats.relationChanges++;
	const char* what = nullptr;
	if (old > -1000 && v <= -1000) what = "The %s and the %s are now at war";
	else if (old < 1000 && v >= 1000) what = "The %s and the %s have made an alliance";
	else if (old >= 1000 && v < 1000 && v > -1000) what = "Relations between the %s and the %s have cooled";
	else if (old <= -1000 && v > -1000 && v < 1000) what = "The %s and the %s have agreed a cease-fire";
	if (what) {
		stats.relationFlips++;
		m_host.News(Fmt(what, FactionName(a), FactionName(b)), 0.f, 0.f);
		m_host.Log(Fmt("[relations] %s / %s %d -> %d", FactionId(a), FactionId(b), old, v));
	}
}

void Board::Round(Group& attacker, Group& victim, std::vector<Member>& deadVictims)
{
	const float pA = SquadPower(attacker);
	if (pA <= 0.f) return;
	float dmg = pA * (m_cfg.hitBase + rng.frac() * m_cfg.hitFactor);
	const float dealt = dmg;
	// the attacker's random MVP earns rank
	std::vector<Member*> liveA;
	for (auto& m : attacker.members) if (m.alive()) liveA.push_back(&m);
	Member* mvp = liveA.empty() ? nullptr : liveA[std::size_t(rng.index(int(liveA.size())))];
	for (int guard = 0; guard < 64 && dmg > 0.01f; ++guard) {
		std::vector<Member*> liveV;
		for (auto& m : victim.members) if (m.alive()) liveV.push_back(&m);
		if (liveV.empty()) break;
		Member* v = liveV[std::size_t(rng.index(int(liveV.size())))];
		const float power = std::max(1.f, PowerOf(*v));
		const float eff = MemberEff(*v);
		const float used = std::min(dmg, eff);
		v->health -= v->healthMax * used / power;
		dmg -= used;
		if (v->health <= 0.5f) {
			MemberDied(victim, *v, &attacker, mvp);
			deadVictims.push_back(*v);
		}
	}
	if (mvp && !IsMonster(attacker)) {   // Anomaly: only stalkers gain rank
		const float xp = dealt / std::max(1.f, m_cfg.rankFraction);
		mvp->rankXp += xp;
		m_host.GrowMember(*mvp, xp, rng);
		if (!attacker.members.empty() && attacker.members.front().alive()) {   // commander bonus
			const float bonus = float(rng.range(1, std::max(1, int(m_cfg.commanderGain)))) / std::max(1.f, m_cfg.rankFraction);
			attacker.members.front().rankXp += bonus;
			m_host.GrowMember(attacker.members.front(), bonus, rng);
		}
	}
}

void Board::Battle(Group& a, Group& b, double)
{
	stats.battles++;
	// SOS (Anomaly dynamic_news "under attack"): the weaker human side calls for help when a fight starts, once per place per cooldown
	if (m_cfg.news && m_cfg.newsSos && (a.ocsStartPower <= 0.f || b.ocsStartPower <= 0.f)) {
		Group& weak = SquadPower(a) <= SquadPower(b) ? a : b;
		Group& strong = &weak == &a ? b : a;
		auto& until = m_sosUntil[a.node];
		if (!IsMonster(weak) && now >= until && now - m_lastSos >= m_cfg.sosGapHours) {
			until = now + m_cfg.sosCooldownHours;
			m_lastSos = now;
			const GraphNode* at = m_world.Node(a.node);
			m_host.News(Fmt("SOS from the %s near %s: under attack by %s%s!", FactionName(weak.faction), PlaceName(a.node).c_str(),
			                IsMonster(strong) ? "" : "the ", FactionName(strong.faction)), at ? at->x : a.x, at ? at->y : a.y);
			stats.sosNews++;
		}
	}
	a.opponent = b.id;
	b.opponent = a.id;
	if (a.ocsStartPower <= 0.f) a.ocsStartPower = SquadPower(a);
	if (b.ocsStartPower <= 0.f) b.ocsStartPower = SquadPower(b);
	const std::string before = Fmt("%s x%d (%.0f) vs %s x%d (%.0f)", FactionId(a.faction), a.livingCount(), SquadPower(a),
	                                FactionId(b.faction), b.livingCount(), SquadPower(b));
	std::vector<Member> deadA, deadB;
	bool aFled = false, bFled = false;
	for (int r = 0; r < std::max(1, m_cfg.roundsPerStep); ++r) {
		if (r % 2 == 0) Round(a, b, deadB); else Round(b, a, deadA);
		if (a.livingCount() == 0 || b.livingCount() == 0) break;
		const float pa = SquadPower(a), pb = SquadPower(b);
		if (pa < m_cfg.fleeRatio * a.ocsStartPower && pb > 1.4f * pa && rng.chance(0.6f)) { aFled = true; break; }
		if (pb < m_cfg.fleeRatio * b.ocsStartPower && pa > 1.4f * pb && rng.chance(0.6f)) { bFled = true; break; }
	}
	const GraphNode* where = m_world.Node(a.node);
	const float wx = where ? where->x : a.x, wy = where ? where->y : a.y;
	// bodies and loot go to the survivors
	const bool beasts = IsMonster(a) || IsMonster(b);   // beasts do not loot, and carry nothing worth taking
	if (!deadB.empty() && a.livingCount() > 0 && !beasts) m_host.LootBodies(a, deadB);
	if (!deadA.empty() && b.livingCount() > 0 && !beasts) m_host.LootBodies(b, deadA);
	auto prune = [](Group& g) {
		g.members.erase(std::remove_if(g.members.begin(), g.members.end(), [](const Member& m) { return !m.alive(); }), g.members.end());
	};
	auto finish = [&](Group& loser, Group& winner, bool fled) {
		if (loser.livingCount() == 0) {
			stats.wiped[int(loser.faction)]++;
			if (IsMonster(loser))
				m_host.News(Fmt("%s killed a band of %s near %s", FactionName(winner.faction), FactionName(loser.faction), PlaceName(a.node).c_str()), wx, wy);
			else if (IsMonster(winner))
				m_host.News(Fmt("%s squad torn apart by %s near %s", FactionName(loser.faction), FactionName(winner.faction), PlaceName(a.node).c_str()), wx, wy);
			else
				m_host.News(Fmt("%s squad wiped out by %s near %s", FactionName(loser.faction), FactionName(winner.faction),
				                PlaceName(a.node).c_str()), wx, wy);
			if (where && !IsMonster(winner)) m_host.OnConquest(*where, winner.faction, int(SquadPower(winner)));
			m_host.Log(Fmt("[ocs] %s wiped by %s at %s (%s)", SquadLabel(loser).c_str(), SquadLabel(winner).c_str(), PlaceName(a.node).c_str(), before.c_str()));
			prune(loser);
			RemoveSquad(loser, "ocs");
			winner.ocsStartPower = 0;
		} else if (fled) {
			stats.retreats++;
			loser.retreatUntilHours = now + rng.uniform(3.f, 6.f);
			loser.avoidNode = winner.node;
			AssignToSmart(loser, -1);
			loser.assignedTarget = loser.currentTarget = -1;
			loser.action = Action::None;
			loser.locked = false;
			loser.ocsStartPower = 0;
			winner.ocsStartPower = 0;
			m_host.Log(Fmt("[ocs] %s retreats from %s at %s (%s)", SquadLabel(loser).c_str(), SquadLabel(winner).c_str(), PlaceName(a.node).c_str(), before.c_str()));
			if (m_cfg.news && !(IsMonster(loser) && IsMonster(winner))) {   // battle report
				auto& until = m_battleNewsUntil[a.node];
				if (now >= until && now - m_lastBattleNews >= m_cfg.battleGapHours) {
					until = now + m_cfg.sosCooldownHours;
					m_lastBattleNews = now;
					m_host.News(Fmt("Battle report: the %s fell back from %s%s near %s", FactionName(loser.faction), IsMonster(winner) ? "" : "the ",
					                FactionName(winner.faction), PlaceName(a.node).c_str()), wx, wy);
					stats.battleNews++;
				}
			}
		}
	};
	prune(a);
	prune(b);
	if (a.livingCount() == 0) finish(a, b, false);
	else if (b.livingCount() == 0) finish(b, a, false);
	else if (aFled) finish(a, b, true);
	else if (bFled) finish(b, a, true);
}

void Board::RunOcs()
{
	if (m_cfg.ocsMode == OcsMode::Off) return;
	if (int(squads.size()) <= int(std::ceil(m_cfg.populationFactor * float(m_cfg.minSquadsForOcs)))) return;   // Anomaly: thin crowded worlds only
	std::vector<std::size_t> idx;
	for (std::size_t i = 0; i < squads.size(); ++i) {
		const Group& g = squads[i];
		if (g.online || g.livingCount() == 0 || now < g.retreatUntilHours) continue;
		idx.push_back(i);
	}
	// shuffle so no squad is always the attacker
	for (std::size_t i = idx.size(); i > 1; --i) std::swap(idx[i - 1], idx[std::size_t(rng.index(int(i)))]);
	std::set<std::size_t> busy;
	const float cd2 = m_cfg.combatDistance * m_cfg.combatDistance;
	for (std::size_t ai = 0; ai < idx.size(); ++ai) {
		const std::size_t i = idx[ai];
		if (busy.count(i)) continue;
		for (std::size_t bi = ai + 1; bi < idx.size(); ++bi) {
			const std::size_t j = idx[bi];
			if (busy.count(j)) continue;
			Group& a = squads[i];
			Group& b = squads[j];
			if (!m_cfg.IsEnemy(a.faction, b.faction)) continue;
			const float dx = a.x - b.x, dy = a.y - b.y;
			if (dx * dx + dy * dy > cd2) continue;
			if (m_cfg.ocsMode == OcsMode::Smarts && !(a.smartAssigned >= 0 && a.smartAssigned == b.smartAssigned)) continue;
			Battle(a, b, 0);
			busy.insert(i);
			busy.insert(j);
			break;
		}
	}
}

// ------------------------------------------------------------------ control / healing / main step
void Board::UpdateControl()
{
	std::vector<std::vector<Faction>> present(m_world.Size()), beasts(m_world.Size());
	for (auto& g : squads) {
		if (g.action != Action::Stay || g.currentTarget < 0 || g.livingCount() == 0) continue;
		auto& v = (IsMonster(g) ? beasts : present)[std::size_t(g.currentTarget)];   // monsters never hold a place (no wolf-held forts)
		if (std::find(v.begin(), v.end(), g.faction) == v.end()) v.push_back(g.faction);
	}
	for (std::size_t i = 0; i < m_world.Size(); ++i) {
		SmartState& st = smart[i];
		const GraphNode& n = m_world.nodes[i];
		auto& v = present[i];
		st.war = false;
		for (std::size_t a = 0; a < v.size() && !st.war; ++a)
			for (std::size_t b = a + 1; b < v.size(); ++b)
				if (m_cfg.IsEnemy(v[a], v[b])) { st.war = true; break; }
		for (auto fb : beasts[i])
			for (auto fa : v)
				if (m_cfg.IsEnemy(fa, fb)) st.war = true;
		if (n.kind == Kind::City || st.war || v.empty()) continue;
		if (st.faction != kNoFaction && std::find(v.begin(), v.end(), Faction(st.faction)) != v.end()) continue;   // holder is present
		// Anomaly lets whoever is present own the place; in Skyrim only an *enemy* of the holder conquers it (a hunter
		// resting at a captured fort does not take it over), and only places that matter change hands.
		std::uint8_t taker = kNoFaction;
		if (st.faction == kNoFaction) taker = std::uint8_t(v.front());
		else for (auto f : v) if (m_cfg.IsEnemy(f, Faction(st.faction))) { taker = std::uint8_t(f); break; }
		if (taker == kNoFaction) continue;
		const std::uint8_t prev = st.faction;
		st.faction = taker;
		stats.controlChanges++;
		const bool notable = n.kind == Kind::Fort || n.kind == Kind::MilitaryCamp || n.kind == Kind::BanditCamp || n.kind == Kind::ForswornCamp ||
		                     n.kind == Kind::Town || n.kind == Kind::VampireLair || n.kind == Kind::WarlockLair;
		if (prev != kNoFaction && notable)
			m_host.News(Fmt("%s took control of %s from %s", FactionName(Faction(taker)), PlaceName(n.id).c_str(), FactionName(Faction(prev))), n.x, n.y);
		m_host.Log(Fmt("[control] %s -> %s at %s", prev == kNoFaction ? "none" : FactionId(Faction(prev)), FactionId(Faction(taker)), PlaceName(n.id).c_str()));
	}
}

// Survival-style needs: everyone gets hungry and tired; cold only matters with Survival Mode. Members eat and drink on their own
// when they carry something; starvation weakens but never kills.
void Board::TickNeeds(double a_hours)
{
	const float h = float(a_hours);
	const bool night = !InTimeInterval(HourOfDay(now), 6.f, 20.f);
	const bool survival = m_host.SurvivalOn();
	for (auto& g : squads) {
		if (g.online || IsMonster(g)) continue;   // the engine owns members on screen; beasts and the dead do not starve
		const float rest = g.action == Action::Stay ? 0.5f : 1.f;
		for (auto& m : g.members) {
			if (!m.alive()) continue;
			m.hunger = std::min(1000.f, m.hunger + m_cfg.hungerPerHour * h * rest);
			m.fatigue = std::min(1000.f, m.fatigue + m_cfg.fatiguePerHour * h * (g.action == Action::Stay ? 0.4f : 1.f));
			m.cold = survival ? std::min(1000.f, m.cold + (night ? m_cfg.coldNight : m_cfg.coldDay) * h) : 0.f;
			m_host.Consume(m);
			if (m.hunger >= 1000.f) m.health = std::max(1.f, m.health - 2.f * h);   // starvation weakens, never kills
		}
	}
}

// The world levels with the player: an offline member whose level has fallen behind the player's (plus the offset it was made with: novices
// trail, veterans lead) gains levels a step at a time. Nobody is ever lowered: whoever grew by fighting stays as grown.
void Board::PaceLevels()
{
	if (now - m_lastPace < double(m_cfg.levelPaceHours)) return;
	m_lastPace = now;
	const int pl = std::max(1, m_host.PlayerLevel());
	m_lastPlayerLevel = pl;
	for (auto& g : squads) {
		if (g.online) continue;   // the engine owns members on screen; they catch up once offline again
		for (auto& m : g.members) {
			if (!m.alive()) continue;
			const int target = std::max(1, pl + m.levelBias);
			if (m.level >= target) continue;
			const int to = std::min(target, m.level + std::max(1, m_cfg.levelsPerPace));
			if (IsMonster(g)) {   // creatures grow by their monsters.ltx curve, wounds kept as a share
				if (const CreatureDef* d = m_cfg.CreatureOf(m)) {
					const float frac = m.health / std::max(1.f, m.healthMax);
					MakeCreature(m, *d, to);
					m.health = m.healthMax * frac;
				}
				continue;
			}
			m_host.ScaleMember(m, to);
		}
	}
}

void Board::Heal(double a_hours)
{
	for (auto& g : squads) {
		if (g.online) continue;   // the engine heals members that are in the world
		const float rate = (g.action == Action::Stay ? 2.f : 1.f) * m_cfg.healPerHour / 100.f * float(a_hours);
		for (auto& m : g.members) if (m.alive()) m.health = std::min(m.healthMax, m.health + m.healthMax * rate);
	}
}

void Board::Sweep()
{
	// a squad that lost its last member (in a fight offline, or on screen) frees its respawn slot before it is dropped
	for (auto& g : squads)
		if (g.members.empty() && !g.online) RemoveSquad(g, "empty");
	squads.erase(std::remove_if(squads.begin(), squads.end(), [](const Group& g) { return g.members.empty() && !g.online; }), squads.end());
}

void Board::Step(double a_hours)
{
	now += a_hours;
	stats.steps++;
	RecomputePopulation();
	for (std::uint32_t i = 0; i < m_world.Size(); ++i) TryRespawn(i);
	// squads in shuffled order (positions: indices stay valid because nothing is created or erased inside the loop)
	std::vector<std::size_t> order(squads.size());
	for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
	for (std::size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[std::size_t(rng.index(int(i)))]);
	for (auto i : order) UpdateSquad(squads[i], a_hours);
	RunOcs();
	TickNeeds(a_hours);
	for (auto& g : squads) {   // idle squads work at their place (trade, raid, hunt ...)
		if (g.online || g.livingCount() == 0 || g.action != Action::Stay || g.currentTarget < 0 || IsMonster(g)) continue;
		if (GraphNode* here = m_world.Node(std::uint32_t(g.currentTarget))) {
			const bool fresh = g.arrivedFresh;
			g.arrivedFresh = false;
			RunActivity(g, *here, a_hours, fresh);
		}
	}
	Heal(a_hours);
	PaceLevels();
	UpdateControl();
	Sweep();
	if (m_cfg.news && m_cfg.newsGossip && now >= m_nextGossip) {
		if (m_nextGossip >= 0) Gossip();
		m_nextGossip = now + m_cfg.gossipHours * rng.uniform(0.75f, 1.25f);
	}
}

// Gossip (Anomaly dynamic_news random / faction chatter): one line about the state of the world every few game hours.
void Board::Gossip()
{
	std::vector<std::pair<std::string, std::pair<float, float>>> lines;   // candidate line, where it is about
	// the strongest band
	int best = -1, bestN = 0;
	for (int f = 0; f < kHumanFactionCount; ++f)
		if (int n = SquadCount(Faction(f)); n > bestN) { bestN = n; best = f; }
	if (best >= 0) lines.push_back({ Fmt("Gossip: they say the %s have more men on the roads than anyone, %d bands of them", FactionName(Faction(best)), bestN), { 0.f, 0.f } });
	// a place under dispute, a monster band near a settlement, hunters after a lair
	std::vector<std::uint32_t> war, beasts, hunted;
	for (std::uint32_t i = 0; i < smart.size(); ++i) {
		if (smart[i].war) war.push_back(i);
		const GraphNode& n = m_world.nodes[i];
		if (MonsterHeld(i) && (n.settlement || n.kind == Kind::Farm)) beasts.push_back(i);
	}
	for (auto& g : squads)
		if (!IsMonster(g) && m_cfg.huntsLairs[int(g.faction)] && g.assignedTarget >= 0 && MonsterHeld(std::uint32_t(g.assignedTarget)))
			hunted.push_back(std::uint32_t(g.assignedTarget));
	auto at = [&](std::uint32_t i) { const GraphNode& n = m_world.nodes[i]; return std::make_pair(n.x, n.y); };
	if (!war.empty()) { auto i = war[std::size_t(rng.index(int(war.size())))]; lines.push_back({ Fmt("Gossip: there's fighting over %s, best keep clear", PlaceName(i).c_str()), at(i) }); }
	for (std::uint32_t i : beasts) {
		for (auto& g : squads)
			if (IsMonster(g) && g.smartAssigned == std::int64_t(i)) { lines.push_back({ Fmt("Gossip: %s were seen around %s", FactionName(g.faction), PlaceName(i).c_str()), at(i) }); break; }
		if (lines.size() > 6) break;
	}
	if (!hunted.empty()) { auto i = hunted[std::size_t(rng.index(int(hunted.size())))]; lines.push_back({ Fmt("Gossip: hunters are heading for %s, there's good money in what lives there", PlaceName(i).c_str()), at(i) }); }
	std::uint64_t parts = 0;
	for (auto v : stats.partsHarvested) parts += v;
	if (parts > 0) lines.push_back({ Fmt("Gossip: the traders say hunters have brought in %llu pelts and parts so far", (unsigned long long)parts), { 0.f, 0.f } });
	if (lines.empty()) return;
	auto& pick = lines[std::size_t(rng.index(int(lines.size())))];
	m_host.News(pick.first, pick.second.first, pick.second.second);
	stats.gossipNews++;
}

void Board::Advance(double a_to)
{
	const double step = std::max(0.01, double(m_cfg.stepHours));
	while (now + step <= a_to + 1e-9) Step(step);
}

}  // namespace ALife
