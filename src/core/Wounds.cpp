// Wounds (docs/WOUNDED.md): wounded, help, finish and surrender. Engine-free; see include/ALife/Wounds.h.
#include "ALife/Wounds.h"

#include <algorithm>
#include <cmath>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "ALife/Board.h"
#include "ALife/Standing.h"

namespace ALife {

namespace {
	std::string Fmt(const char* f, ...)
	{
		char b[512];
		va_list ap;
		va_start(ap, f);
		std::vsnprintf(b, sizeof(b), f, ap);
		va_end(ap);
		return b;
	}

	// per-faction defaults (wounded.ltx [finish] / [surrender_chance]); index = Faction
	constexpr float kFinish[kHumanFactionCount] = { 0.8f, 0.9f, 0.3f, 0.4f, 0.9f, 0.2f, 0.3f, 0.6f, 0.3f, 0.9f, 1.0f, 0.5f, 0.9f, 0.6f, 0.1f, 0.2f, 0.4f, 0.9f };
	constexpr float kSurrender[kHumanFactionCount] = { 0.5f, 0.f, 0.25f, 0.25f, 0.f, 0.6f, 0.5f, 0.45f, 0.4f, 0.f, 0.f, 0.2f, 0.f, 0.3f, 0.7f, 0.4f, 0.3f, 0.f };

	bool Pair(const Ltx& t, const char* sec, const char* key, float& lo, float& hi, std::string& err)
	{
		if (!t.Has(sec, key)) return true;
		const auto v = t.List(sec, key);
		try {
			if (v.size() != 2) throw 0;
			lo = std::stof(v[0]);
			hi = std::stof(v[1]);
		} catch (...) { err = std::string("wounded.ltx: ") + key + " needs two numbers"; return false; }
		if (hi < lo) std::swap(lo, hi);
		return true;
	}

	bool PerFaction(const Ltx& t, const char* sec, std::array<float, kFactionCount>& out, std::string& err)
	{
		if (auto* lines = t.Section(sec))
			for (auto& kv : *lines) {
				bool ok = false;
				const Faction f = FactionFromId(kv.first, &ok);
				if (!ok || !IsHumanFaction(f)) { err = std::string("wounded.ltx [") + sec + "]: unknown faction '" + kv.first + "'"; return false; }
				try { out[int(f)] = std::clamp(std::stof(kv.second), 0.f, 1.f); } catch (...) { err = std::string("wounded.ltx [") + sec + "] " + kv.first; return false; }
			}
		return true;
	}
}

static_assert(kHumanFactionCount == 18, "wounded defaults: one entry per human faction");

const char* WoundChoiceId(WoundChoice c)
{
	switch (c) {
	case WoundChoice::Heal: return "heal";
	case WoundChoice::Rob: return "rob";
	case WoundChoice::Finish: return "finish";
	case WoundChoice::Spare: return "spare";
	case WoundChoice::RobAndGo: return "robgo";
	case WoundChoice::NoMercy: return "nomercy";
	}
	return "?";
}

WoundConfig DefaultWoundConfig()
{
	WoundConfig c;
	for (int f = 0; f < kHumanFactionCount; ++f) {
		c.factions[f] = true;
		c.finish[f] = kFinish[f];
		c.surrender[f] = kSurrender[f];
	}
	return c;
}

bool LoadWoundConfig(const Ltx& t, WoundConfig& c, std::string& err)
{
	c = DefaultWoundConfig();
	if (!t.Has("wounded")) { err = "wounded.ltx: no [wounded] section"; return false; }
	const char* W = "wounded";
	const char* S = "surrender";
	const char* D = "standing";
	auto f = [&](const char* sec, const char* key, float& v) { v = t.GetF(sec, key, v); };
	c.enabled = t.GetB(W, "enabled", c.enabled);
	f(W, "hp", c.hp);
	f(W, "chance", c.chance);
	if (t.Has(W, "factions")) {
		c.factions.fill(false);
		for (auto& id : t.List(W, "factions")) {
			bool ok = false;
			const Faction fa = FactionFromId(id, &ok);
			if (!ok || !IsHumanFaction(fa)) { err = "wounded.ltx: factions: '" + id + "' is not a human faction"; return false; }
			c.factions[int(fa)] = true;
		}
	}
	f(W, "ocs_chance", c.ocsChance);
	if (!Pair(t, W, "down_hp", c.downHpMin, c.downHpMax, err)) return false;
	f(W, "revive_hp", c.reviveHp);
	f(W, "call_seconds", c.callSeconds);
	f(W, "call_radius", c.callRadius);
	f(W, "help_radius", c.helpRadius);
	f(W, "self_heal_seconds", c.selfHealSeconds);
	f(W, "finish_radius", c.finishRadius);
	if (!Pair(t, W, "finish_delay", c.finishDelayMin, c.finishDelayMax, err)) return false;
	if (!Pair(t, W, "down_hours", c.downHoursMin, c.downHoursMax, err)) return false;
	if (!Pair(t, W, "fall_back_hours", c.fallBackHoursMin, c.fallBackHoursMax, err)) return false;
	float rlo = float(c.helperRankMin), rhi = float(c.helperRankMax);
	if (!Pair(t, W, "helper_rank", rlo, rhi, err)) return false;
	c.helperRankMin = int(rlo);
	c.helperRankMax = int(rhi);
	f(S, "yield_distance", c.yieldDistance);
	f(S, "yield_chance", c.yieldChance);
	f(S, "losing_share", c.losingShare);
	f(S, "outnumber", c.outnumber);
	f(S, "spare_hours", c.spareHours);
	f(S, "release_radius", c.releaseRadius);
	f(S, "surrender_seconds", c.surrenderSeconds);
	f(D, "wound_help", c.woundHelp);
	f(D, "wound_help_memory", c.woundHelpMemory);
	f(D, "rob", c.rob);
	f(D, "rob_memory", c.robMemory);
	f(D, "finish", c.finishDeed);
	f(D, "spare", c.spare);
	f(D, "spare_memory", c.spareMemory);
	f(D, "prisoner", c.prisoner);
	f(D, "prisoner_notoriety", c.prisonerNotoriety);
	if (!PerFaction(t, "finish", c.finish, err)) return false;
	if (!PerFaction(t, "surrender_chance", c.surrender, err)) return false;
	ClampWoundConfig(c);
	return true;
}

void ClampWoundConfig(WoundConfig& c)
{
	auto frac = [](float& v) { v = std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f; };
	auto pos = [](float& v, float lo = 0.f) { v = std::isfinite(v) ? std::max(lo, v) : lo; };
	auto range = [&](float& lo, float& hi, float floor) { pos(lo, floor); pos(hi, floor); if (hi < lo) std::swap(lo, hi); };
	frac(c.hp);
	frac(c.chance);
	frac(c.ocsChance);
	range(c.downHpMin, c.downHpMax, 0.01f);
	c.downHpMin = std::min(c.downHpMin, 1.f);
	c.downHpMax = std::min(c.downHpMax, 1.f);
	frac(c.reviveHp);
	c.reviveHp = std::max(0.05f, c.reviveHp);   // getting up with no health would only drop it again
	pos(c.callSeconds, 1.f);
	pos(c.callRadius);
	pos(c.helpRadius);
	pos(c.selfHealSeconds, 1.f);
	pos(c.finishRadius);
	range(c.finishDelayMin, c.finishDelayMax, 0.f);
	range(c.downHoursMin, c.downHoursMax, 0.f);
	range(c.fallBackHoursMin, c.fallBackHoursMax, 0.f);
	c.helperRankMin = std::max(0, c.helperRankMin);
	c.helperRankMax = std::max(c.helperRankMin, c.helperRankMax);
	pos(c.yieldDistance);
	frac(c.yieldChance);
	frac(c.losingShare);
	pos(c.outnumber, 1.f);
	pos(c.spareHours);
	pos(c.releaseRadius);
	pos(c.surrenderSeconds, 1.f);
	for (auto* v : { &c.woundHelp, &c.woundHelpMemory, &c.rob, &c.robMemory, &c.finishDeed, &c.spare, &c.spareMemory, &c.prisoner })
		*v = std::isfinite(*v) ? std::clamp(*v, 0.f, 2000.f) : 0.f;   // goodwill / memory magnitudes (goodwill_max 2000, memory 1000)
	c.prisonerNotoriety = std::isfinite(c.prisonerNotoriety) ? std::clamp(c.prisonerNotoriety, 0.f, 1000.f) : 0.f;
	for (int f = 0; f < kFactionCount; ++f) {
		frac(c.finish[f]);
		frac(c.surrender[f]);
		if (!IsHumanFaction(Faction(f))) c.factions[f] = false;   // monsters never go down
	}
}

WoundConfig ScaleWoundConfig(const WoundConfig& base, const WoundScale& s)
{
	WoundConfig c = base;
	auto safe = [](float m) { return std::isfinite(m) && m > 0.01f ? m : 0.01f; };
	const float L = safe(s.lethality), E = safe(s.enemy), M = safe(s.economy);
	// the tags in wounded.ltx: {channel+} value x m, {channel-} value / m
	c.hp /= L; c.chance /= L; c.ocsChance /= L; c.downHpMin /= L; c.downHpMax /= L; c.reviveHp /= L; c.helpRadius /= L;
	c.finishDelayMin /= L; c.finishDelayMax /= L;
	c.selfHealSeconds *= L; c.finishRadius *= L; c.downHoursMin *= L; c.downHoursMax *= L;
	for (auto& v : c.finish) v *= L;
	c.helperRankMin = int(std::lround(float(c.helperRankMin) * E)); c.helperRankMax = int(std::lround(float(c.helperRankMax) * E));
	c.yieldChance /= E; c.losingShare *= E; c.outnumber *= E;
	for (auto& v : c.surrender) v /= E;
	c.woundHelp /= M; c.woundHelpMemory /= M; c.spare /= M; c.spareMemory /= M;
	c.rob *= M; c.robMemory *= M; c.finishDeed *= M; c.prisoner *= M; c.prisonerNotoriety *= M;
	// Mercy (docs/DIFFICULTY.md): x 1.0 leaves every value bit for bit as loaded, so Adept runs stay what they were
	const float mercy = std::isfinite(s.mercy) ? std::max(0.f, s.mercy) : 1.f;
	if (mercy != 1.f) {
		c.chance *= mercy; c.ocsChance *= mercy; c.yieldChance *= mercy;
		for (auto& v : c.surrender) v *= mercy;
		for (auto& v : c.finish) v = mercy > 0.f ? v / mercy : 1.f;   // nobody goes down at 0: the finish chance no longer matters
	}
	ClampWoundConfig(c);
	return c;
}

// ------------------------------------------------------------------ the board hook
bool Wounds::Attach(Board& b, bool filled)
{
	if (!filled || !cfg.enabled || b.wounds == this) return false;
	b.wounds = this;
	return true;
}

bool Wounds::AttachedTo(const Board& b) const { return b.wounds == this; }

// ------------------------------------------------------------------ queries
bool Wounds::CanGoDown(const Board& b, const Group& g) const
{
	return cfg.enabled && !b.IsMonster(g) && IsHumanFaction(g.faction) && cfg.factions[int(g.faction)];
}

const Down* Wounds::Find(std::uint64_t id) const
{
	auto it = down.find(id);
	return it == down.end() ? nullptr : &it->second;
}

int Wounds::StandingCount(const Group& g) const
{
	int n = 0;
	for (auto& m : g.members) n += Stands(m) ? 1 : 0;
	return n;
}

int Wounds::DownCount(const Group& g) const
{
	if (down.empty()) return 0;
	int n = 0;
	for (auto& m : g.members) n += m.alive() && IsDown(m.id) ? 1 : 0;
	return n;
}

bool Wounds::Survivors(const Group& g) const
{
	if (down.empty() || g.online) return false;
	bool any = false;
	for (auto& m : g.members) {
		if (!m.alive()) continue;
		if (!IsDown(m.id)) return false;
		any = true;
	}
	return any;
}

bool Wounds::SquadYielded(std::uint64_t sq) const
{
	auto it = fights.find(sq);
	return it != fights.end() && it->second.yielded;
}

Member* Wounds::FindMember(Group& g, std::uint64_t id) const
{
	for (auto& m : g.members) if (m.id == id) return &m;
	return nullptr;
}

// ------------------------------------------------------------------ going down and getting up
bool Wounds::RollDown(Board& b, Group& g, Member& m, Rng& rng)
{
	if (!m.alive() || IsDown(m.id) || !CanGoDown(b, g) || !rng.chance(cfg.chance)) return false;
	GoDown(b, g, m, false);
	return true;
}

void Wounds::GoDown(Board& b, Group& g, Member& m, bool offline)
{
	Down d;
	d.member = m.id;
	d.squad = g.id;
	d.since = b.now;
	d.healthBefore = m.health;
	d.flags = offline ? Down::kOffline : 0;
	m.health = std::max(1.f, m.health);   // down is alive: the engine's bleedout shows it, the board keeps it standing-out
	down[m.id] = d;
	++counters.downed;
}

void Wounds::GetUp(Board&, Group&, Member& m, float frac)
{
	auto it = down.find(m.id);
	if (it == down.end()) return;
	if (frac < 0.f) m.health = std::max(m.health, it->second.healthBefore);
	else m.health = std::clamp(std::max(m.health, m.healthMax * frac), 1.f, std::max(1.f, m.healthMax));
	down.erase(it);
	++counters.gotUp;
}

void Wounds::Kill(Board& b, Group& g, Member& m, Group* killer)
{
	++counters.finished;
	b.MemberDied(g, m, killer, nullptr);   // credits the kill (stats, dynamic relations, death report) and forgets the record
	Forget(m.id);
}

void Wounds::Patch(Board& b, Group& g, Member& m, Member* helper, Rng& rng)
{
	GetUp(b, g, m, cfg.reviveHp);
	++counters.helped;
	if (!helper) return;
	const float xp = float(rng.range(cfg.helperRankMin, cfg.helperRankMax)) / std::max(1.f, b.Cfg().rankFraction);   // xr_help_wounded: rank +10..20
	helper->rankXp += xp;
	b.HostRef().GrowMember(*helper, xp, rng);
}

// ------------------------------------------------------------------ offline
bool Wounds::OfflineLethal(Board& b, Group& g, Member& m)
{
	if (!CanGoDown(b, g) || IsDown(m.id) || !b.rng.chance(cfg.ocsChance)) return false;
	m.health = std::max(1.f, m.healthMax * b.rng.uniform(cfg.downHpMin, cfg.downHpMax));
	GoDown(b, g, m, true);
	++counters.ocsDowned;
	return true;
}

int Wounds::AfterBattle(Board& b, Group& loser, Group& winner, bool fled)
{
	int finished = 0;
	// beasts take no prisoners; people finish with their faction's chance (xrs_kill_wounded)
	const float p = b.IsMonster(winner) ? 1.f : cfg.finish[int(winner.faction)];
	for (auto& m : loser.members) {
		if (!m.alive() || !IsDown(m.id) || !b.rng.chance(p)) continue;
		Kill(b, loser, m, &winner);
		++finished;
	}
	if (!fled && loser.livingCount() > 0 && StandingCount(loser) == 0) {   // the finished are dead already
		// wounded survivors: they lie where they fell for down_hours, out of the simulation, and call for help
		const double upAt = b.now + double(b.rng.uniform(cfg.downHoursMin, cfg.downHoursMax));
		for (auto& m : loser.members)
			if (auto it = down.find(m.id); m.alive() && it != down.end()) { it->second.upAt = upAt; it->second.flags |= Down::kOffline; }
		b.AssignToSmart(loser, -1);
		loser.assignedTarget = loser.currentTarget = -1;
		loser.action = Action::None;
		loser.activity = Act::None;
		++counters.survivors;
		const Config& c = b.Cfg();
		auto& until = sosUntil[loser.node];   // its own cooldowns: the battle SOS that started this fight must not silence the plea after it
		if (c.news && c.newsSos && b.now >= until && b.now - lastSos >= c.sosGapHours) {
			until = b.now + c.sosCooldownHours;
			lastSos = b.now;
			const GraphNode* at = b.m_world.Node(loser.node);
			b.m_host.News(Fmt("SOS: wounded fighters of the %s near %s are calling for help", FactionName(loser.faction), b.PlaceName(loser.node).c_str()),
			              at ? at->x : loser.x, at ? at->y : loser.y);
			b.stats.sosNews++;
		}
	}
	return finished;
}

void Wounds::Tick(Board& b, double, std::vector<WoundEvent>* ev)
{
	if (down.empty()) { foughtLastStep.swap(foughtThisStep); foughtThisStep.clear(); return; }
	std::unordered_map<std::uint64_t, Group*> byId;
	for (auto& g : b.squads) byId[g.id] = &g;
	std::vector<std::uint64_t> squadsDown;
	for (auto it = down.begin(); it != down.end();) {   // stale records: the member died some other way, the squad is gone
		auto gi = byId.find(it->second.squad);
		Member* m = gi == byId.end() ? nullptr : FindMember(*gi->second, it->first);
		if (!m || !m->alive()) { yieldRolled.erase(it->first); it = down.erase(it); continue; }
		if (std::find(squadsDown.begin(), squadsDown.end(), it->second.squad) == squadsDown.end()) squadsDown.push_back(it->second.squad);
		++it;
	}
	auto emit = [&](WoundEvent::Kind k, std::uint64_t sq, std::uint64_t mem, std::uint64_t by = 0) { if (ev) ev->push_back({ k, sq, mem, by }); };
	for (auto sq : squadsDown) {
		Group& g = *byId[sq];
		if (g.online || b.companions.count(g.id)) continue;   // on screen (and travelling with the player) the plugin runs them
		std::vector<Member*> hurt;
		for (auto& m : g.members) if (m.alive() && IsDown(m.id)) hurt.push_back(&m);
		// a yielded squad that went off screen was left alone: it gets up, no standing change
		bool released = false;
		for (auto* m : hurt)
			if (down[m->id].Yielded()) { GetUp(b, g, *m, -1.f); released = true; emit(WoundEvent::Kind::Released, sq, m->id); }
		if (released) {
			fights.erase(sq);
			hurt.erase(std::remove_if(hurt.begin(), hurt.end(), [&](Member* m) { return !IsDown(m->id); }), hurt.end());
		}
		if (hurt.empty()) continue;
		for (auto* m : hurt) down[m->id].flags |= Down::kOffline;
		std::vector<Member*> up;
		for (auto& m : g.members) if (Stands(m)) up.push_back(&m);
		if (up.empty()) {
			double upAt = 0;
			for (auto* m : hurt) upAt = std::max(upAt, down[m->id].upAt);
			if (upAt <= 0) {   // went down on screen and the squad left it with nobody standing: wounded survivors from now
				upAt = b.now + double(b.rng.uniform(cfg.downHoursMin, cfg.downHoursMax));
				for (auto* m : hurt) down[m->id].upAt = upAt;
				++counters.survivors;
				emit(WoundEvent::Kind::Survivors, sq, 0);
				continue;
			}
			if (b.now < upAt) continue;
			for (auto* m : hurt) { GetUp(b, g, *m, cfg.reviveHp); emit(WoundEvent::Kind::Up, sq, m->id); }
			// they fall back like a squad that fled
			b.stats.retreats++;
			g.retreatUntilHours = b.now + double(b.rng.uniform(cfg.fallBackHoursMin, cfg.fallBackHoursMax));
			g.avoidNode = g.node;
			b.AssignToSmart(g, -1);
			g.assignedTarget = g.currentTarget = -1;
			g.action = Action::None;
			g.locked = false;
			g.ocsStartPower = 0;
			continue;
		}
		if (foughtThisStep.count(sq)) continue;   // nobody runs over with a potion in the middle of a fight
		for (auto* m : hurt) {   // squadmates patch them up (xr_help_wounded)
			Member* helper = up[std::size_t(b.rng.index(int(up.size())))];
			Patch(b, g, *m, helper, b.rng);
			emit(WoundEvent::Kind::Helped, sq, m->id, helper->id);
		}
	}
	foughtLastStep.swap(foughtThisStep);
	foughtThisStep.clear();
}

// ------------------------------------------------------------------ on screen
void Wounds::UpdateOnline(Board& b, float dt, const std::unordered_set<std::uint64_t>& playerNear, std::vector<WoundEvent>& ev)
{
	for (auto& [id, d] : down) {
		if (d.Offline()) continue;
		d.seconds += dt;
		if (d.Yielded()) continue;   // the yielded wait for the player's choice
		if (playerNear.count(d.squad)) {
			d.nextCall -= dt;
			if (d.nextCall <= 0.f) {
				ev.push_back({ WoundEvent::Kind::Call, d.squad, id, (d.flags & Down::kCalled) ? 0ULL : 1ULL });   // by = 1: the first call (notice)
				d.flags |= Down::kCalled;
				d.nextCall = cfg.callSeconds;
			}
		}
		if (d.seconds >= cfg.selfHealSeconds) {   // xr_wounded autoheal: the host gives its potion or revive_hp and calls GetUp
			ev.push_back({ WoundEvent::Kind::SelfHeal, d.squad, id, 0 });
			d.seconds = 0.f;
		}
	}
	std::vector<std::uint64_t> timedOut;
	for (auto& [sq, f] : fights) {
		if (!f.yielded) continue;
		f.seconds += dt;
		if (f.seconds >= cfg.surrenderSeconds) timedOut.push_back(sq);
	}
	for (auto sq : timedOut)
		if (Group* g = b.Find(sq)) { Release(b, *g); ev.push_back({ WoundEvent::Kind::Released, sq, 0, 0 }); }
		else fights.erase(sq);
}

int Wounds::ChooseHelper(const Group& g, const std::vector<float>& dist, bool fighting) const
{
	if (fighting) return -1;
	int best = -1;
	float bestD = cfg.helpRadius;
	for (std::size_t i = 0; i < g.members.size() && i < dist.size(); ++i)
		if (Stands(g.members[i]) && dist[i] <= bestD) { bestD = dist[i]; best = int(i); }
	return best;
}

void Wounds::Help(Board& b, Group& g, Member& wounded, Member& helper, bool potion, Rng& rng)
{
	if (!IsDown(wounded.id) || !Stands(helper)) return;
	Patch(b, g, wounded, &helper, rng);
	if (potion) wounded.health = wounded.healthMax;
}

// ------------------------------------------------------------------ surrender
bool Wounds::RollYield(Board& b, Group& g, Member& m, float dist, bool weapon, bool hostile, Rng& rng)
{
	auto it = down.find(m.id);
	if (it == down.end() || it->second.Yielded() || !hostile || !weapon || dist > cfg.yieldDistance || !CanGoDown(b, g)) return false;
	if (cfg.surrender[int(g.faction)] <= 0.f) return false;   // fanatics never beg
	if (!yieldRolled.insert(m.id).second) return false;       // rolled once per member
	if (!rng.chance(cfg.yieldChance)) return false;
	it->second.flags |= Down::kYielded;
	++counters.yielded;
	return true;
}

bool Wounds::Losing(const Group& g) const
{
	const int start = g.startMembers > 0 ? g.startMembers : int(g.members.size());
	if (start <= 0) return false;
	const int lost = std::max(0, start - g.livingCount()) + DownCount(g);
	return float(lost) >= cfg.losingShare * float(start);
}

bool Wounds::Outnumbered(const Group& g, int side) const
{
	const int up = StandingCount(g);
	return up > 0 && float(side) >= cfg.outnumber * float(up);
}

bool Wounds::CheckSurrender(Board& b, Group& g, int side, Rng& rng, float chanceOverride)
{
	if (!CanGoDown(b, g)) return false;
	auto& f = fights[g.id];
	if (f.rolled || f.yielded) return false;
	if (!Losing(g) && !Outnumbered(g, side)) return false;   // rolled only once there is a reason to give up
	f.rolled = true;
	const float p = chanceOverride >= 0.f ? chanceOverride : cfg.surrender[int(g.faction)];
	if (p <= 0.f || !rng.chance(p)) return false;
	f.yielded = true;
	f.seconds = 0.f;
	for (auto& m : g.members) {
		if (!m.alive()) continue;
		if (!IsDown(m.id)) GoDown(b, g, m, false);
		down[m.id].flags |= Down::kYielded;
	}
	++counters.surrendered;
	return true;
}

void Wounds::Release(Board& b, Group& g)
{
	for (auto& m : g.members)
		if (auto it = down.find(m.id); m.alive() && it != down.end() && it->second.Yielded()) GetUp(b, g, m, -1.f);
	fights.erase(g.id);
	// it backs off from the player for a while (Judge: Ignore while tollUntil runs)
	g.tollUntil = std::max(g.tollUntil, b.now + double(cfg.spareHours));
	g.trust = Trust::BackingOff;
}

// ------------------------------------------------------------------ the player's choice
bool Wounds::Choose(Board& b, Standing& st, Group& g, Member& m, WoundChoice c, bool hostile, bool witnessed, double now)
{
	auto it = down.find(m.id);
	if (it == down.end() || !m.alive()) return false;
	const bool yielded = it->second.Yielded();
	switch (c) {
	case WoundChoice::Heal:   // transfer_medkit: the member gets up with full health
		GetUp(b, g, m, 1.f);
		st.OnWoundHelped(g, cfg.woundHelp, cfg.woundHelpMemory, now);
		if (hostile) g.tollUntil = std::max(g.tollUntil, now + double(st.Cfg().tollPassHours));   // it owes you: stands down
		return true;
	case WoundChoice::Rob:    // gold and weapons go to the player (the host moves the items); it stays down
		m.gold = 0;
		st.OnRob(g, hostile, cfg.rob, cfg.robMemory, now);
		return true;
	case WoundChoice::Finish:
	case WoundChoice::NoMercy:
		st.OnKill(g, m, witnessed, hostile, now);
		if (yielded) st.OnPrisonerKilled(g, cfg.prisoner, cfg.prisonerNotoriety, now); else st.OnFinish(g, cfg.finishDeed, now);
		Kill(b, g, m, nullptr);
		return true;
	case WoundChoice::Spare:
	case WoundChoice::RobAndGo:
		if (!yielded) return false;
		if (c == WoundChoice::RobAndGo) {
			for (auto& x : g.members)
				if (auto d = down.find(x.id); x.alive() && d != down.end() && d->second.Yielded()) x.gold = 0;
			st.OnRob(g, hostile, cfg.rob, cfg.robMemory, now);
		} else {
			st.OnSpare(g, cfg.spare, cfg.spareMemory, now);
		}
		Release(b, g);
		return true;
	}
	return false;
}

// ------------------------------------------------------------------ co-save
namespace {
	struct Bytes
	{
		std::vector<std::uint8_t>& b;
		template <class T> void v(const T& x) { auto* p = reinterpret_cast<const std::uint8_t*>(&x); b.insert(b.end(), p, p + sizeof(T)); }
	};
	struct Reader
	{
		const std::uint8_t* d;
		std::size_t n, o{ 0 };
		bool ok{ true };
		template <class T> T v()
		{
			T x{};
			if (!ok || o + sizeof(T) > n) { ok = false; return x; }
			std::memcpy(&x, d + o, sizeof(T));
			o += sizeof(T);
			return x;
		}
	};
}

void Wounds::Save(std::vector<std::uint8_t>& out) const
{
	Bytes w{ out };
	w.v(std::uint32_t(down.size()));
	for (auto& [id, d] : down) {
		w.v(d.member); w.v(d.squad); w.v(d.since); w.v(d.upAt); w.v(d.healthBefore); w.v(std::uint8_t(d.flags & (Down::kYielded | Down::kOffline | Down::kCalled)));
	}
	w.v(std::uint32_t(fights.size()));
	for (auto& [sq, f] : fights) { w.v(sq); w.v(f.rolled); w.v(f.yielded); }
}

bool Wounds::Load(const std::uint8_t* data, std::size_t size, std::uint32_t version)
{
	Reset();
	if (version != kSaveVersion || !data) return false;
	Reader r{ data, size };
	for (auto k = r.v<std::uint32_t>(); r.ok && k && k <= 100000; --k) {
		Down d;
		d.member = r.v<std::uint64_t>(); d.squad = r.v<std::uint64_t>(); d.since = r.v<double>(); d.upAt = r.v<double>();
		d.healthBefore = r.v<float>(); d.flags = r.v<std::uint8_t>();
		if (r.ok && d.member) down[d.member] = d;
	}
	for (auto k = r.v<std::uint32_t>(); r.ok && k && k <= 100000; --k) {
		const auto sq = r.v<std::uint64_t>();
		Surrender f;
		f.rolled = r.v<bool>();
		f.yielded = r.v<bool>();
		if (r.ok) fights[sq] = f;
	}
	if (!r.ok) { Reset(); return false; }
	return true;
}

void Wounds::Rebind(Board& b)
{
	for (auto it = down.begin(); it != down.end();) {
		Group* g = b.Find(it->second.squad);
		Member* m = g ? FindMember(*g, it->first) : nullptr;
		if (!m || !m->alive()) { it = down.erase(it); continue; }
		it->second.seconds = 0.f;
		it->second.nextCall = 0.f;
		++it;
	}
	for (auto it = fights.begin(); it != fights.end();)
		it = b.Find(it->first) ? std::next(it) : fights.erase(it);
}

}  // namespace ALife
