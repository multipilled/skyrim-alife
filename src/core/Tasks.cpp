// Tasks: the engine-free job model (docs/TASKS.md).
#include "ALife/Tasks.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ALife/Board.h"
#include "ALife/SaveFormat.h"
#include "ALife/World.h"

namespace ALife {

namespace {
	const char* kTypeIds[kTaskTypes] = { "none", "bounty", "assault", "delivery", "fetch", "escort", "defend", "rescue", "recover", "hunt" };
	constexpr float kUnitsPerKm = 70000.f;

	float Dist2D(float ax, float ay, float bx, float by)
	{
		const float dx = ax - bx, dy = ay - by;
		return std::sqrt(dx * dx + dy * dy);
	}

	const Member* Leader(const Group& g)
	{
		for (auto& m : g.members) if (m.alive() && m.role == Role::Leader) return &m;
		for (auto& m : g.members) if (m.alive()) return &m;
		return nullptr;
	}

	const Member* MemberById(const Group& g, std::uint64_t id)
	{
		for (auto& m : g.members) if (m.id == id) return &m;
		return nullptr;
	}

	bool Living(Board& b, std::uint64_t id)
	{
		const Group* g = b.Find(id);
		return g && g->livingCount() > 0;
	}

	std::string PlaceNear(const World& w, float x, float y, int ws)
	{
		const auto n = w.Nearest(x, y, ws);
		const auto* node = w.Node(n);
		return node ? node->name : std::string{};
	}

	FormID ParseSpec(const std::string& spec)
	{
		if (spec.find('|') != std::string::npos) return 0;   // another plugin: the host resolves it
		try { return FormID(std::stoul(spec, nullptr, 16)); } catch (...) { return 0; }
	}

	// pick one of the (up to) three nearest candidates at random (Anomaly caches three bounties and prefers nearby ones)
	template <class T>
	const T* PickNear(std::vector<std::pair<float, const T*>>& c, Rng& rng)
	{
		if (c.empty()) return nullptr;
		std::sort(c.begin(), c.end(), [](auto& a, auto& b) { return a.first < b.first; });
		return c[std::size_t(rng.index(std::min<int>(3, int(c.size()))))].second;
	}

	// the squads of factions at war with a_f staying at each place of worldspace a_ws (assault holders, rescue captors)
	std::unordered_map<std::uint32_t, std::vector<const Group*>> EnemiesStaying(Board& b, Faction a_f, int a_ws, bool a_monsters)
	{
		const auto& cf = b.Cfg();
		std::unordered_map<std::uint32_t, std::vector<const Group*>> held;
		for (auto& s : b.squads) {
			if (int(s.ws) != a_ws || s.livingCount() == 0 || s.action != Action::Stay || s.currentTarget < 0) continue;
			if (b.companions.count(s.id) || b.held.count(s.id) || b.IsDragon(s)) continue;   // dragons are nobody's job (docs/DRAGONS.md)
			if (!cf.IsEnemy(a_f, s.faction) && !(a_monsters && b.IsMonster(s))) continue;
			held[std::uint32_t(s.currentTarget)].push_back(&s);
		}
		return held;
	}

	// A job's squad of a description (defend wave, recover guard), its size the description's roll x defend_wave_size (enemy
	// strength), at least 1 and at most defend_wave_max (the on-screen budget). A bigger squad takes members of more squads of the
	// same description, made and folded in.
	Group* MakeSized(Board& b, std::uint32_t node, const std::string& section, const TaskNumbers& N)
	{
		Group* g = b.CreateSquad(node, section, true);
		if (!g) return nullptr;
		const std::uint64_t id = g->id;
		const int want = std::clamp(int(std::lround(float(g->members.size()) * N.defendWaveSize)), 1, std::max(1, N.defendWaveMax));
		for (int guard = 0; guard < 8 && int(b.Find(id)->members.size()) < want; ++guard) {
			Group* more = b.CreateSquad(node, section, false);   // invalidates g
			if (!more) break;
			std::vector<Member> extra = std::move(more->members);
			b.RemoveSquad(*more, "folded into a job squad");
			Group* main = b.Find(id);
			for (auto& m : extra) {
				if (int(main->members.size()) >= want) break;
				if (m.role == Role::Leader) m.role = Role::Melee;
				main->members.push_back(std::move(m));
			}
		}
		g = b.Find(id);
		if (int(g->members.size()) > want) g->members.resize(std::size_t(want));
		g->startMembers = int(g->members.size());
		g->respawnSmart = -1;
		return g;
	}
	struct Bytes
	{
		std::vector<std::uint8_t>& b;
		template <class T> void v(const T& x) { auto* p = reinterpret_cast<const std::uint8_t*>(&x); b.insert(b.end(), p, p + sizeof(T)); }
		void s(const std::string& x) { v(std::uint32_t(x.size())); b.insert(b.end(), x.begin(), x.end()); }
	};
	struct Reader
	{
		const std::uint8_t* d;
		std::size_t n, o{ 0 };
		bool ok{ true };
		template <class T> T v()
		{
			T x{};
			if (o + sizeof(T) > n) { ok = false; return x; }
			std::memcpy(&x, d + o, sizeof(T));
			o += sizeof(T);
			return x;
		}
		std::string s()
		{
			const auto len = v<std::uint32_t>();
			if (!ok || o + len > n || len > 4096) { ok = false; return {}; }
			std::string x(reinterpret_cast<const char*>(d + o), len);
			o += len;
			return x;
		}
	};

	void SaveTask(Bytes& w, const Task& t, std::uint32_t a_version)
	{
		w.v(t.id); w.v(t.type); w.v(t.stage); w.v(t.slot); w.v(t.giver); w.v(t.giverFaction); w.s(t.giverName);
		w.v(t.target); w.v(t.targetMember); w.v(t.targetFaction); w.s(t.targetName);
		w.v(std::uint32_t(t.pinned.size()));
		for (auto p : t.pinned) w.v(p);
		w.v(t.node); w.s(t.place); w.v(t.item); w.s(t.itemName); w.v(t.count); w.v(t.gold);
		w.v(t.offeredAt); w.v(t.acceptedAt); w.v(t.parcelMissingSince);
		if (a_version < 3) return;
		// v3 (M5e)
		w.v(t.dest);
		w.v(std::uint32_t(t.marks.size()));
		for (auto& m : t.marks) { w.v(m.squad); w.v(m.member); }
		w.v(t.step); w.v(t.extra); w.v(t.corpse); w.v(t.deadline); w.s(t.squadDesc);
	}
	Task LoadTask(Reader& r, std::uint32_t a_version)
	{
		Task t;
		t.id = r.v<std::uint32_t>(); t.type = r.v<TaskType>(); t.stage = r.v<TaskStage>(); t.slot = r.v<std::int32_t>();
		t.giver = r.v<std::uint64_t>(); t.giverFaction = r.v<Faction>(); t.giverName = r.s();
		t.target = r.v<std::uint64_t>(); t.targetMember = r.v<std::uint64_t>(); t.targetFaction = r.v<Faction>(); t.targetName = r.s();
		for (auto n = r.v<std::uint32_t>(); r.ok && n && n < 64; --n) t.pinned.push_back(r.v<std::uint64_t>());
		t.node = r.v<std::uint32_t>(); t.place = r.s(); t.item = r.v<FormID>(); t.itemName = r.s(); t.count = r.v<std::int32_t>(); t.gold = r.v<std::int32_t>();
		t.offeredAt = r.v<double>(); t.acceptedAt = r.v<double>(); t.parcelMissingSince = r.v<double>();
		if (a_version >= 3) {
			t.dest = r.v<std::uint32_t>();
			const auto n = r.v<std::uint32_t>();
			if (n > 64) r.ok = false;
			for (auto k = n; r.ok && k; --k) {
				Task::Mark m;
				m.squad = r.v<std::uint64_t>();
				m.member = r.v<std::uint64_t>();
				t.marks.push_back(m);
			}
			t.step = r.v<std::int32_t>(); t.extra = r.v<std::uint64_t>(); t.corpse = r.v<FormID>(); t.deadline = r.v<double>(); t.squadDesc = r.s();
		}
		if (a_version == 1) { t.giverFaction = FactionFromPreSolstheim(std::uint8_t(t.giverFaction)); t.targetFaction = FactionFromPreSolstheim(std::uint8_t(t.targetFaction)); }
		if (int(t.type) >= kTaskTypes || int(t.giverFaction) >= kFactionCount || int(t.targetFaction) >= kFactionCount) r.ok = false;
		return t;
	}
}

const char* TaskTypeId(TaskType t) { return int(t) < kTaskTypes ? kTypeIds[int(t)] : "none"; }

TaskType TaskTypeFromId(const std::string& id)
{
	for (int i = 1; i < kTaskTypes; ++i) if (id == kTypeIds[i]) return TaskType(i);
	return TaskType::None;
}

// ------------------------------------------------------------------ difficulty (the one place the multipliers apply)
TaskScale TaskScaleFrom(const Config& c)
{
	TaskScale s;
	s.enemy = c.Diff(DiffChannel::Tasks);     // waves, guards, target levels: task difficulty (a wave's squad is also sized by Strength in CreateSquad)
	s.task = c.Diff(DiffChannel::Tasks);
	s.economy = c.Diff(DiffChannel::Economy);
	return s;
}

TaskNumbers Scaled(const TaskNumbers& base, const TaskScale& s)
{
	TaskNumbers n = base;
	const float e = std::clamp(s.enemy, 0.1f, 10.f), d = std::clamp(s.task, 0.1f, 10.f), m = std::clamp(s.economy, 0.1f, 10.f);
	// [enemy] enemy strength
	n.maxLevelOver = int(std::lround(float(base.maxLevelOver) * e));
	n.defendWaveSize = base.defendWaveSize * e;
	// [task] task difficulty: more waves, guards and marks; less time; heavier penalties
	n.defendWaves = std::clamp(int(std::lround(float(base.defendWaves) * d)), 1, 10);
	n.recoverGuardChance = std::clamp(base.recoverGuardChance * d, 0.f, 1.f);
	n.huntMarks = std::clamp(int(std::lround(float(base.huntMarks) * d)), 3, 10);
	n.huntMin = std::clamp(int(std::lround(float(base.huntMin) * d)), 1, n.huntMarks);
	n.escortHoursPerKm = base.escortHoursPerKm / d;
	n.parcelGraceHours = base.parcelGraceHours / d;
	n.expireDays = base.expireDays / d;
	n.goodwillFail = base.goodwillFail * d;
	n.memoryFail = base.memoryFail * d;
	// [economy] pay and what a job earns
	n.goldMult = base.goldMult * m;
	n.goldMin = std::max(1, int(std::lround(float(base.goldMin) * m)));
	n.goodwillDone = base.goodwillDone * m;
	n.memoryDone = base.memoryDone * m;
	return n;
}

// ------------------------------------------------------------------ config
bool LoadTaskConfig(const Ltx& t, TaskConfig& c, std::string& err)
{
	c = TaskConfig{};
	if (!t.Has("tasks")) { err = "tasks.ltx: no [tasks] section"; return false; }
	c.maxActive = std::clamp(t.GetI("tasks", "max_active", c.maxActive), 1, 4);
	c.noJobChance = t.GetF("tasks", "no_job_chance", c.noJobChance);
	c.offerHours = t.GetF("tasks", "offer_hours", float(c.offerHours));
	c.repeatHours = t.GetF("tasks", "repeat_hours", float(c.repeatHours));
	c.expireDays = t.GetF("tasks", "expire_days", float(c.expireDays));
	c.maxLevelOver = t.GetI("tasks", "max_level_over", c.maxLevelOver);
	c.bountyRange = t.GetF("tasks", "bounty_range", c.bountyRange);
	c.assaultRange = t.GetF("tasks", "assault_range", c.assaultRange);
	c.deliveryMin = t.GetF("tasks", "delivery_min", c.deliveryMin);
	c.deliveryMax = t.GetF("tasks", "delivery_max", c.deliveryMax);
	c.parcelGraceHours = t.GetF("tasks", "parcel_grace_hours", float(c.parcelGraceHours));
	c.parcelSpec = t.Get("tasks", "parcel");
	c.parcel = ParseSpec(c.parcelSpec);
	// M5e
	c.escortMin = t.GetF("tasks", "escort_min", c.escortMin);
	c.escortMax = t.GetF("tasks", "escort_max", c.escortMax);
	c.escortArrive = t.GetF("tasks", "escort_arrive", c.escortArrive);
	c.escortHoursPerKm = t.GetF("tasks", "escort_hours_per_km", float(c.escortHoursPerKm));
	c.escortLatePay = std::clamp(t.GetF("rewards", "escort_late_pay", c.escortLatePay), 0.f, 1.f);
	c.defendSpawnMin = t.GetF("tasks", "defend_spawn_min", c.defendSpawnMin);
	c.defendSpawnMax = t.GetF("tasks", "defend_spawn_max", c.defendSpawnMax);
	c.defendWaves = std::clamp(t.GetI("tasks", "defend_waves", c.defendWaves), 1, 10);
	c.defendWaveSize = std::clamp(t.GetF("tasks", "defend_wave_size", c.defendWaveSize), 0.1f, 5.f);
	c.defendWaveMax = std::clamp(t.GetI("tasks", "defend_wave_max", c.defendWaveMax), 1, kMaxOnlineActors / kMaxOnlineGroups);
	c.rescueRange = t.GetF("tasks", "rescue_range", c.rescueRange);
	c.rescueArrive = t.GetF("tasks", "rescue_arrive", c.rescueArrive);
	c.recoverMin = t.GetF("tasks", "recover_min", c.recoverMin);
	c.recoverMax = t.GetF("tasks", "recover_max", c.recoverMax);
	c.recoverSpawn = t.GetF("tasks", "recover_spawn", c.recoverSpawn);
	c.recoverGuardChance = t.GetF("tasks", "recover_guard_chance", c.recoverGuardChance);
	c.satchelSpec = t.Get("tasks", "satchel");
	c.satchel = ParseSpec(c.satchelSpec);
	c.huntMarks = std::clamp(t.GetI("tasks", "hunt_marks", c.huntMarks), 3, 10);
	c.huntMin = std::clamp(t.GetI("tasks", "hunt_min", c.huntMin), 1, c.huntMarks);
	c.rankKillPoints = t.GetF("tasks", "rank_kill_points", c.rankKillPoints);
	c.rankVeteran = t.GetF("tasks", "rank_veteran", c.rankVeteran);

	c.goodwillDone = t.GetF("rewards", "goodwill_done", c.goodwillDone);
	c.goodwillFail = t.GetF("rewards", "goodwill_fail", c.goodwillFail);
	c.memoryDone = t.GetF("rewards", "memory_done", c.memoryDone);
	c.memoryFail = t.GetF("rewards", "memory_fail", c.memoryFail);
	c.goldMult = t.GetF("rewards", "gold_mult", c.goldMult);
	c.bountyBase = t.GetF("rewards", "bounty_base", c.bountyBase);
	c.bountyPerLevel = t.GetF("rewards", "bounty_per_level", c.bountyPerLevel);
	c.assaultPerSquad = t.GetF("rewards", "assault_per_squad", c.assaultPerSquad);
	c.assaultPerLevel = t.GetF("rewards", "assault_per_level", c.assaultPerLevel);
	c.deliveryBase = t.GetF("rewards", "delivery_base", c.deliveryBase);
	c.deliveryPerKm = t.GetF("rewards", "delivery_per_km", c.deliveryPerKm);
	c.fetchMult = t.GetF("rewards", "fetch_mult", c.fetchMult);
	c.goldMin = t.GetI("rewards", "gold_min", c.goldMin);
	c.escortBase = t.GetF("rewards", "escort_base", c.escortBase);
	c.escortPerKm = t.GetF("rewards", "escort_per_km", c.escortPerKm);
	c.defendPerWave = t.GetF("rewards", "defend_per_wave", c.defendPerWave);
	c.defendPerLevel = t.GetF("rewards", "defend_per_level", c.defendPerLevel);
	c.rescueBase = t.GetF("rewards", "rescue_base", c.rescueBase);
	c.rescuePerLevel = t.GetF("rewards", "rescue_per_level", c.rescuePerLevel);
	c.recoverBase = t.GetF("rewards", "recover_base", c.recoverBase);
	c.recoverPerKm = t.GetF("rewards", "recover_per_km", c.recoverPerKm);
	c.huntBase = t.GetF("rewards", "hunt_base", c.huntBase);
	c.huntPerLevel = t.GetF("rewards", "hunt_per_level", c.huntPerLevel);

	// [recover] corpse_<faction> = leveled actor list (or actor base) for the body
	if (auto* lines = t.Section("recover"))
		for (auto& [key, val] : *lines) {
			if (key.rfind("corpse_", 0) != 0) continue;
			bool ok = false;
			const Faction f = FactionFromId(key.substr(7), &ok);
			if (!ok) { err = "tasks.ltx [recover]: unknown faction in " + key; return false; }
			c.corpse[int(f)] = val;
		}

	// [givers] faction = bounty:2, assault:1, delivery:1, fetch:3
	if (auto* lines = t.Section("givers"))
		for (auto& [key, val] : *lines) {
			bool ok = false;
			const Faction f = FactionFromId(key, &ok);
			if (!ok) { err = "tasks.ltx [givers]: unknown faction " + key; return false; }
			for (auto& part : SplitList(val, ',')) {
				const auto colon = part.find(':');
				const TaskType ty = TaskTypeFromId(Trim(part.substr(0, colon)));
				if (ty == TaskType::None) { err = "tasks.ltx [givers]: unknown task type in " + part; return false; }
				float w = 1.f;
				if (colon != std::string::npos) try { w = std::stof(part.substr(colon + 1)); } catch (...) {}
				c.weight[int(f)][int(ty)] = w;
			}
		}

	// [fetch_<list>] name = spec, min:max, min_level, value ; [fetch_lists] faction = list, list
	std::unordered_map<std::string, std::vector<FetchItem>> lists;
	for (auto& sec : t.Sections()) {
		if (sec.rfind("fetch_", 0) != 0 || sec == "fetch_lists") continue;
		auto& items = lists[sec.substr(6)];
		for (auto& [name, val] : *t.Section(sec)) {
			const auto f = SplitList(val, ',');
			if (f.size() < 2) { err = "tasks.ltx [" + sec + "] " + name + ": want spec, min:max[, min_level[, value]]"; return false; }
			FetchItem it;
			it.name = name;
			it.spec = f[0];
			it.form = ParseSpec(f[0]);
			const auto colon = f[1].find(':');
			try {
				it.lo = std::stoi(f[1].substr(0, colon));
				it.hi = colon == std::string::npos ? it.lo : std::stoi(f[1].substr(colon + 1));
				if (f.size() > 2) it.minLevel = std::stoi(f[2]);
				if (f.size() > 3) it.value = std::stoi(f[3]);
			} catch (...) { err = "tasks.ltx [" + sec + "] " + name + ": bad number"; return false; }
			for (auto& ch : it.name) if (ch == '_') ch = ' ';
			items.push_back(it);
		}
	}
	if (auto* lines = t.Section("fetch_lists"))
		for (auto& [key, val] : *lines) {
			bool ok = false;
			const Faction f = FactionFromId(key, &ok);
			if (!ok) { err = "tasks.ltx [fetch_lists]: unknown faction " + key; return false; }
			for (auto& name : SplitList(val, ',')) {
				auto it = lists.find(name);
				if (it == lists.end()) { err = "tasks.ltx [fetch_lists]: no [fetch_" + name + "]"; return false; }
				c.fetch[int(f)].insert(c.fetch[int(f)].end(), it->second.begin(), it->second.end());
			}
		}
	return true;
}

// ------------------------------------------------------------------ offers
bool Tasks::Targeted(std::uint64_t squad) const
{
	for (auto& t : active) {
		if (t.target == squad || (t.extra && t.extra == squad)) return true;
		for (auto p : t.pinned) if (p == squad) return true;
		for (auto& m : t.marks) if (m.squad == squad) return true;
	}
	return false;
}

int Tasks::FreeSlot() const
{
	const TaskNumbers N = Eff();
	for (int s = 0; s < N.maxActive; ++s) {
		bool used = false;
		for (auto& t : active) used |= t.slot == s;
		if (!used) return s;
	}
	return -1;
}

Task* Tasks::ByGiver(std::uint64_t squad)
{
	for (auto& t : active) if (t.giver == squad) return &t;
	return nullptr;
}

Task* Tasks::BySlot(int slot)
{
	for (auto& t : active) if (t.slot == slot) return &t;
	return nullptr;
}

const Task* Tasks::Offered(std::uint64_t squad) const
{
	auto it = offers.find(squad);
	return it == offers.end() || it->second.task.type == TaskType::None ? nullptr : &it->second.task;
}

bool Tasks::FindBounty(Board& b, const World& w, const Group& giver, const TaskHost& h, Rng& rng, Task& t) const
{
	const TaskNumbers N = Eff();
	std::vector<std::pair<float, const Group*>> c;
	for (auto& s : b.squads) {
		if (s.id == giver.id || s.ws != giver.ws || !IsHumanFaction(s.faction) || s.livingCount() == 0) continue;
		const auto& cf = b.Cfg();
		if (!cf.IsEnemy(giver.faction, s.faction) || Targeted(s.id) || b.companions.count(s.id)) continue;   // never a squad travelling with the player
		if (s.avgLevel() > float(h.PlayerLevel() + N.maxLevelOver)) continue;
		if (w.InCityBuffer(s.x, s.y, kCityBuffer, s.ws)) continue;
		const float d = Dist2D(giver.x, giver.y, s.x, s.y);
		if (d <= N.bountyRange) c.emplace_back(d, &s);
	}
	const Group* s = PickNear(c, rng);
	if (!s) return false;
	const Member* m = Leader(*s);
	if (!m) return false;
	t.target = s->id;
	t.targetMember = m->id;
	t.targetFaction = s->faction;
	t.targetName = m->name;
	t.node = w.Nearest(s->x, s->y, s->ws);
	t.place = PlaceNear(w, s->x, s->y, s->ws);
	t.gold = int(N.bountyBase + N.bountyPerLevel * float(m->level));
	return true;
}

bool Tasks::FindAssault(Board& b, const World& w, const Group& giver, const TaskHost& h, Rng& rng, Task& t) const
{
	const TaskNumbers N = Eff();
	// places where enemies of the giver are staying, with the squads that hold each
	auto held = EnemiesStaying(b, giver.faction, giver.ws, false);
	std::vector<std::pair<float, const std::vector<const Group*>*>> c;
	for (auto& [node, gs] : held) {
		const GraphNode* n = w.Node(node);
		if (!n || n->city || n->settlement || n->playerHome) continue;
		bool taken = false;
		float lvl = 0.f;
		for (auto* g : gs) { lvl = std::max(lvl, g->avgLevel()); taken |= Targeted(g->id); }
		if (taken || lvl > float(h.PlayerLevel() + N.maxLevelOver)) continue;
		const float d = Dist2D(giver.x, giver.y, n->x, n->y);
		if (d <= N.assaultRange) c.emplace_back(d, &gs);
	}
	const auto* gs = PickNear(c, rng);
	if (!gs || gs->empty()) return false;
	const Group& first = *gs->front();
	t.node = std::uint32_t(first.currentTarget);
	t.place = w.Node(t.node) ? w.Node(t.node)->name : std::string{};
	t.targetFaction = first.faction;
	t.targetName = FactionName(first.faction);
	float levels = 0.f;
	for (auto* g : *gs) { t.pinned.push_back(g->id); levels += g->avgLevel(); }
	t.gold = int(N.assaultPerSquad * float(gs->size()) + N.assaultPerLevel * levels);
	return true;
}

bool Tasks::FindDelivery(Board& b, const World& w, const Group& giver, const TaskHost& h, Rng& rng, Task& t) const
{
	const TaskNumbers N = Eff();
	if (!cfg.parcel && cfg.parcelSpec.empty()) return false;
	const auto& cf = b.Cfg();
	std::vector<std::pair<float, const Group*>> c;
	for (auto& s : b.squads) {
		if (s.id == giver.id || s.ws != giver.ws || !IsHumanFaction(s.faction) || s.livingCount() == 0) continue;
		if (cf.IsEnemy(giver.faction, s.faction) || Targeted(s.id) || !h.Approachable(s) || b.companions.count(s.id) || b.held.count(s.id)) continue;
		const float d = Dist2D(giver.x, giver.y, s.x, s.y);
		if (d >= N.deliveryMin && d <= N.deliveryMax) c.emplace_back(d, &s);
	}
	const Group* s = PickNear(c, rng);
	if (!s) return false;
	const Member* m = Leader(*s);
	if (!m) return false;
	t.target = s->id;
	t.targetMember = m->id;
	t.targetFaction = s->faction;
	t.targetName = m->name;
	t.node = w.Nearest(s->x, s->y, s->ws);
	t.place = PlaceNear(w, s->x, s->y, s->ws);
	t.item = cfg.parcel;
	t.itemName = "sealed parcel";
	t.count = 1;
	t.gold = int(N.deliveryBase + N.deliveryPerKm * Dist2D(giver.x, giver.y, s->x, s->y) / kUnitsPerKm);
	return true;
}

bool Tasks::FindFetch(const Group& giver, const TaskHost& h, Rng& rng, Task& t) const
{
	const TaskNumbers N = Eff();
	std::vector<const FetchItem*> c;
	for (auto& it : cfg.fetch[int(giver.faction)])
		if ((it.form || !it.spec.empty()) && it.minLevel <= h.PlayerLevel()) c.push_back(&it);
	if (c.empty()) return false;
	const FetchItem& it = *c[std::size_t(rng.index(int(c.size())))];
	t.item = it.form;
	t.itemName = it.name;
	t.count = rng.range(it.lo, it.hi);
	const float value = float(h.ItemValue(it.form, it.value)) * float(t.count) * N.fetchMult;
	t.gold = int(value * rng.uniform(0.9f, 1.1f));   // tasks_fetch.script:202-206 (+-10%)
	return true;
}

// Escort (tasks_guide.script:282-449): a place in the squad's worldspace it may go to, escort_min..max away; the party has room.
bool Tasks::FindEscort(Board& b, const World& w, const Group& giver, Rng& rng, Task& t) const
{
	const TaskNumbers N = Eff();
	if (!party || party->Has(giver.id) || !party->Room(b, giver)) return false;
	std::vector<std::pair<float, const GraphNode*>> c;
	for (auto& n : w.nodes) {
		if (n.ws != giver.ws || n.disabled || n.playerHome || std::int64_t(n.id) == giver.currentTarget || n.id >= b.smart.size()) continue;
		const float d = Dist2D(giver.x, giver.y, n.x, n.y);
		if (d < N.escortMin || d > N.escortMax) continue;
		if (!b.TargetPrecondition(n, b.smart[n.id], giver, true)) continue;   // board target rules: a place its faction goes to
		c.emplace_back(d, &n);
	}
	if (c.empty()) return false;
	const auto& pick = c[std::size_t(rng.index(int(c.size())))];
	t.dest = t.node = pick.second->id;
	t.place = pick.second->name;
	t.targetFaction = giver.faction;
	t.targetName = FactionName(giver.faction);
	t.gold = int(N.escortBase + N.escortPerKm * pick.first / kUnitsPerKm);
	return true;
}

// Defend (tasks_defense.script:443-540): the giver stays at a place outside a city; an enemy of its faction has a squad in this
// worldspace the player can face (its squad description makes the waves); a spawn place defend_spawn_min..max away.
bool Tasks::FindDefend(Board& b, const World& w, const Group& giver, const TaskHost& h, Rng& rng, Task& t) const
{
	const TaskNumbers N = Eff();
	if (giver.action != Action::Stay || giver.currentTarget < 0 || Targeted(giver.id)) return false;
	const GraphNode* home = w.Node(std::uint32_t(giver.currentTarget));
	if (!home || home->city || home->playerHome || home->ws != giver.ws) return false;
	const auto& cf = b.Cfg();
	std::vector<std::pair<float, const Group*>> c;
	for (auto& s : b.squads) {
		if (s.id == giver.id || s.ws != giver.ws || s.livingCount() == 0 || !cf.IsEnemy(giver.faction, s.faction)) continue;
		if (b.companions.count(s.id) || b.held.count(s.id) || b.IsDragon(s) || s.section.empty() || !cf.Squad(s.section) || b.Blocked(s.faction, home->ws)) continue;
		if (s.avgLevel() > float(h.PlayerLevel() + N.maxLevelOver)) continue;
		c.emplace_back(Dist2D(home->x, home->y, s.x, s.y), &s);
	}
	const Group* e = PickNear(c, rng);
	if (!e) return false;
	std::vector<const GraphNode*> spawn;
	for (auto& n : w.nodes) {
		if (n.ws != home->ws || n.id == home->id || n.disabled || n.city || n.playerHome) continue;
		const float d = Dist2D(home->x, home->y, n.x, n.y);
		if (d >= N.defendSpawnMin && d <= N.defendSpawnMax) spawn.push_back(&n);
	}
	if (spawn.empty()) return false;
	t.node = home->id;
	t.place = home->name;
	t.dest = spawn[std::size_t(rng.index(int(spawn.size())))]->id;
	t.squadDesc = e->section;
	t.targetFaction = e->faction;
	t.targetName = FactionName(e->faction);
	t.count = std::max(1, N.defendWaves);
	t.gold = int(N.defendPerWave * float(t.count) + N.defendPerLevel * e->avgLevel());
	return true;
}

// Rescue (tasks_agent_rescue.script:277-400): an enemy squad of the giver (people or monsters) staying at a place outside a city.
bool Tasks::FindRescue(Board& b, const World& w, const Group& giver, const TaskHost& h, Rng& rng, Task& t) const
{
	const TaskNumbers N = Eff();
	if (giver.section.empty() || !b.Cfg().Squad(giver.section)) return false;   // the captive is made from the giver's squad description
	auto held = EnemiesStaying(b, giver.faction, giver.ws, true);
	std::vector<std::pair<float, const std::vector<const Group*>*>> c;
	for (auto& [node, gs] : held) {
		const GraphNode* n = w.Node(node);
		if (!n || n->city || n->settlement || n->playerHome) continue;
		bool taken = false;
		float lvl = 0.f;
		for (auto* g : gs) { lvl = std::max(lvl, g->avgLevel()); taken |= Targeted(g->id); }
		if (taken || lvl > float(h.PlayerLevel() + N.maxLevelOver)) continue;
		const float d = Dist2D(giver.x, giver.y, n->x, n->y);
		if (d <= N.rescueRange) c.emplace_back(d, &gs);
	}
	const auto* gs = PickNear(c, rng);
	if (!gs || gs->empty()) return false;
	const Group& first = *gs->front();
	t.node = std::uint32_t(first.currentTarget);
	t.place = w.Node(t.node) ? w.Node(t.node)->name : std::string{};
	t.targetFaction = first.faction;
	t.targetName = FactionName(first.faction);
	float levels = 0.f;
	for (auto* g : *gs) { t.pinned.push_back(g->id); levels += g->avgLevel(); }
	t.gold = int(N.rescueBase + N.rescuePerLevel * levels);
	return true;
}

// Recover (tasks_recover_item_on_corpse.script:162-260): a place outside a city recover_min..max away, preferring one held by
// enemies or monsters; an unguarded one may get a monster squad of the nearest kind (rolled here, made on accept).
bool Tasks::FindRecover(Board& b, const World& w, const Group& giver, Rng& rng, Task& t) const
{
	const TaskNumbers N = Eff();
	if (!cfg.satchel && cfg.satchelSpec.empty()) return false;
	auto held = EnemiesStaying(b, giver.faction, giver.ws, true);
	std::vector<std::pair<float, const GraphNode*>> all, guarded;
	for (auto& n : w.nodes) {
		if (n.ws != giver.ws || n.disabled || n.city || n.settlement || n.playerHome) continue;
		const float d = Dist2D(giver.x, giver.y, n.x, n.y);
		if (d < N.recoverMin || d > N.recoverMax) continue;
		all.emplace_back(d, &n);
		if (held.count(n.id)) guarded.emplace_back(d, &n);
	}
	auto& c = guarded.empty() ? all : guarded;
	if (c.empty()) return false;
	const auto& pick = c[std::size_t(rng.index(int(c.size())))];
	const GraphNode& n = *pick.second;
	t.node = n.id;
	t.place = n.name;
	t.item = cfg.satchel;
	t.itemName = "satchel";
	t.count = 1;
	t.gold = int(N.recoverBase + N.recoverPerKm * pick.first / kUnitsPerKm);
	if (auto it = held.find(n.id); it != held.end()) {
		t.targetFaction = it->second.front()->faction;
		t.targetName = FactionName(t.targetFaction);
	} else if (rng.chance(N.recoverGuardChance)) {   // Anomaly's critters around the body: the nearest monster kind
		const Group* closest = nullptr;
		float best = 1e30f;
		for (auto& s : b.squads) {
			if (s.ws != n.ws || !b.IsMonster(s) || b.IsDragon(s) || s.livingCount() == 0 || s.section.empty() || !b.Cfg().Squad(s.section)) continue;
			const float d = Dist2D(n.x, n.y, s.x, s.y);
			if (d < best) { best = d; closest = &s; }
		}
		if (closest) {
			t.squadDesc = closest->section;
			t.targetFaction = closest->faction;
			t.targetName = FactionName(closest->faction);
		}
	}
	return true;
}

// Hunt (tasks_top_10.script:31-106): the ten best-ranked enemies of the giver in its worldspace, at least hunt_min of them.
bool Tasks::FindHunt(Board& b, const World& w, const Group& giver, Task& t) const
{
	const TaskNumbers N = Eff();
	const auto& cf = b.Cfg();
	float levels = 0.f;
	for (auto& r : Rankings(b, giver.ws)) {
		if (r.squad == giver.id || !cf.IsEnemy(giver.faction, r.faction)) continue;
		t.marks.push_back({ r.squad, r.member });
		levels += float(r.level);
		if (int(t.marks.size()) >= std::clamp(N.huntMarks, 1, 10)) break;
	}
	if (int(t.marks.size()) < std::max(1, N.huntMin)) { t.marks.clear(); return false; }
	t.step = 0;
	const Task::Mark& m = t.marks.back();   // the lowest-ranked first
	const Group* g = b.Find(m.squad);
	const Member* mm = g ? MemberById(*g, m.member) : nullptr;
	if (!mm) { t.marks.clear(); return false; }
	t.target = m.squad;
	t.targetMember = m.member;
	t.targetFaction = g->faction;
	t.targetName = mm->name;
	t.node = w.Nearest(g->x, g->y, g->ws);
	t.place = PlaceNear(w, g->x, g->y, g->ws);
	t.gold = int(N.huntBase + N.huntPerLevel * levels);
	return true;
}

std::vector<RankEntry> Tasks::Rankings(const Board& b, int ws) const
{
	const TaskNumbers N = Eff();
	std::vector<RankEntry> r;
	for (auto& g : b.squads) {
		if (!IsHumanFaction(g.faction) || b.companions.count(g.id) || b.held.count(g.id)) continue;
		if (ws >= 0 && int(g.ws) != ws) continue;
		for (auto& m : g.members) {
			if (!m.alive()) continue;
			RankEntry e;
			e.squad = g.id; e.member = m.id; e.faction = g.faction; e.name = m.name; e.level = m.level; e.ws = g.ws;
			e.score = b.PowerOf(m) + N.rankKillPoints * float(m.kills) + (m.veteran ? N.rankVeteran : 0.f);
			r.push_back(std::move(e));
		}
	}
	std::sort(r.begin(), r.end(), [](const RankEntry& a, const RankEntry& c) { return a.score != c.score ? a.score > c.score : a.member < c.member; });
	return r;
}

const Task::Mark* Tasks::CurrentMark(const Task& t)
{
	const int i = int(t.marks.size()) - 1 - t.step;
	return i >= 0 && i < int(t.marks.size()) ? &t.marks[std::size_t(i)] : nullptr;
}

TaskType Tasks::OfferOf(Board& b, const World& w, const Group& giver, double now, const TaskHost& h, Rng& rng)
{
	const TaskNumbers N = Eff();
	if (!IsHumanFaction(giver.faction) || giver.livingCount() == 0 || b.companions.count(giver.id) || b.held.count(giver.id)) return TaskType::None;   // companions and captives have no work to hand out
	if (ByGiver(giver.id)) return TaskType::None;                        // one job at a time from a squad
	if (int(active.size()) >= N.maxActive) return TaskType::None;
	if (auto it = cooldown.find(giver.id); it != cooldown.end()) {
		if (now < it->second) return TaskType::None;
		cooldown.erase(it);
	}
	if (auto it = offers.find(giver.id); it != offers.end()) {
		if (now < it->second.until && (it->second.task.type == TaskType::None || StillValid(b, it->second.task))) return it->second.task.type;
		offers.erase(it);
	}
	Offer o;
	o.until = now + N.offerHours;
	o.task.giver = giver.id;
	o.task.giverFaction = giver.faction;
	if (const Member* m = Leader(giver)) o.task.giverName = m->name;
	o.task.offeredAt = now;
	if (!rng.chance(N.noJobChance)) {
		// weighted pick among the faction's job types; a type without a target falls back to the others
		auto weights = cfg.weight[int(giver.faction)];
		for (int tries = 0; tries < kTaskTypes; ++tries) {
			float sum = 0.f;
			for (int i = 1; i < kTaskTypes; ++i) sum += weights[i];
			if (sum <= 0.f) break;
			float roll = rng.frac() * sum;
			int pick = 1;
			for (int i = 1; i < kTaskTypes; ++i) {
				if (roll < weights[i]) { pick = i; break; }
				roll -= weights[i];
				pick = i;
			}
			Task t = o.task;
			t.type = TaskType(pick);
			bool found = false;
			switch (t.type) {
			case TaskType::Bounty: found = FindBounty(b, w, giver, h, rng, t); break;
			case TaskType::Assault: found = FindAssault(b, w, giver, h, rng, t); break;
			case TaskType::Delivery: found = FindDelivery(b, w, giver, h, rng, t); break;
			case TaskType::Fetch: found = FindFetch(giver, h, rng, t); break;
			case TaskType::Escort: found = FindEscort(b, w, giver, rng, t); break;
			case TaskType::Defend: found = FindDefend(b, w, giver, h, rng, t); break;
			case TaskType::Rescue: found = FindRescue(b, w, giver, h, rng, t); break;
			case TaskType::Recover: found = FindRecover(b, w, giver, rng, t); break;
			case TaskType::Hunt: found = FindHunt(b, w, giver, t); break;
			default: break;
			}
			if (found) {
				// economy (docs/DIFFICULTY.md): every type through N.goldMult = gold_mult x Tasks::scale.economy (= Diff(Economy), TaskScaleFrom)
				t.gold = std::max(N.goldMin, int(float(t.gold) * N.goldMult));
				o.task = t;
				break;
			}
			weights[pick] = 0.f;
		}
	}
	offers[giver.id] = o;
	return o.task.type;
}

TaskType Tasks::ForceOffer(Board& b, const World& w, const Group& giver, TaskType ty, double now, const TaskHost& h, Rng& rng)
{
	offers.erase(giver.id);
	cooldown.erase(giver.id);
	const auto weights = cfg.weight[int(giver.faction)];
	const float noJob = cfg.noJobChance;
	for (auto& x : cfg.weight[int(giver.faction)]) x = 0.f;
	cfg.weight[int(giver.faction)][int(ty)] = 1.f;
	cfg.noJobChance = 0.f;
	const TaskType got = OfferOf(b, w, giver, now, h, rng);
	cfg.weight[int(giver.faction)] = weights;
	cfg.noJobChance = noJob;
	return got;
}

bool Tasks::StillValid(Board& b, const Task& t) const
{
	switch (t.type) {
	case TaskType::Bounty:
	case TaskType::Delivery: {
		const Group* g = b.Find(t.target);
		const Member* m = g ? MemberById(*g, t.targetMember) : nullptr;
		return m && m->alive();
	}
	case TaskType::Assault:
	case TaskType::Rescue:
		for (auto id : t.pinned)
			if (const Group* g = b.Find(id); g && g->livingCount() > 0 && g->currentTarget == std::int64_t(t.node)) return true;
		return false;
	case TaskType::Fetch:
	case TaskType::Recover: return true;
	case TaskType::Escort: {
		const Group* g = b.Find(t.giver);
		return g && party && !party->Has(t.giver) && party->Room(b, *g);
	}
	case TaskType::Defend: {
		const Group* g = b.Find(t.giver);
		return g && g->action == Action::Stay && g->currentTarget == std::int64_t(t.node) && b.Cfg().Squad(t.squadDesc);
	}
	case TaskType::Hunt:
		for (auto& m : t.marks)
			if (const Group* g = b.Find(m.squad)) if (const Member* mm = MemberById(*g, m.member); mm && mm->alive()) return true;
		return false;
	default: return false;
	}
}

bool Tasks::SpawnWave(Board& b, Task& t)
{
	++t.step;
	t.extra = 0;
	Group* g = MakeSized(b, t.dest, t.squadDesc, Eff());
	if (!g) return false;
	g->respawnSmart = -1;
	b.forced[g->id] = t.node;   // sent at the place, held there once it arrives
	t.extra = g->id;
	return true;
}

bool Tasks::Setup(Board& b, Task& t, double now)
{
	const TaskNumbers N = Eff();
	switch (t.type) {
	case TaskType::Assault:   // only the squads still holding the place count
	case TaskType::Rescue:
		t.pinned.erase(std::remove_if(t.pinned.begin(), t.pinned.end(), [&](std::uint64_t id) {
			const Group* g = b.Find(id);
			return !g || g->livingCount() == 0 || g->currentTarget != std::int64_t(t.node);
		}), t.pinned.end());
		if (t.pinned.empty()) return false;
		if (t.type == TaskType::Rescue) {   // the captive: a one-man squad of the giver's faction, out of the simulation while held
			const Group* giver = b.Find(t.giver);
			if (!giver) return false;
			const std::string section = giver->section;
			Group* cap = b.CreateSquad(t.node, section, false);   // invalidates giver
			if (!cap || cap->members.empty()) return false;
			cap->members.resize(1);
			cap->members[0].role = Role::Leader;
			cap->startMembers = 1;
			cap->action = Action::None;
			cap->assignedTarget = cap->currentTarget = -1;
			cap->respawnSmart = -1;
			t.extra = cap->id;
			t.itemName = cap->members[0].name;
			t.step = 0;
		}
		return true;
	case TaskType::Escort: {
		Group* g = b.Find(t.giver);
		if (!g || !party || !party->Join(b, *g, now, partyEvents)) return false;
		const GraphNode* d = b.WorldRef().Node(t.dest);
		const float km = d ? Dist2D(g->x, g->y, d->x, d->y) / kUnitsPerKm : 0.f;
		t.deadline = now + std::max(1.0, N.escortHoursPerKm * double(km));
		t.step = 0;
		return true;
	}
	case TaskType::Defend:
		t.pinned = { t.giver };
		t.step = 0;
		return SpawnWave(b, t);
	case TaskType::Recover: {
		if (t.squadDesc.empty()) return true;
		const GraphNode* n = b.WorldRef().Node(t.node);
		if (!n) { t.squadDesc.clear(); return true; }
		if (EnemiesStaying(b, t.giverFaction, n->ws, true).count(t.node)) { t.squadDesc.clear(); return true; }   // someone moved in since the offer: no critters needed
		Group* g = MakeSized(b, t.node, t.squadDesc, N);
		if (!g) { t.squadDesc.clear(); return true; }
		g->respawnSmart = -1;
		g->action = Action::Stay;
		g->assignedTarget = g->currentTarget = t.node;
		g->stayStart = b.now;
		t.extra = g->id;
		t.pinned = { g->id };
		return true;
	}
	default: return true;
	}
}

void Tasks::Release(Board& b, const Task& t, PartyEvent::Kind why)
{
	if (!party) return;
	const std::uint64_t id = t.type == TaskType::Escort ? t.giver : t.type == TaskType::Rescue ? t.extra : 0;
	if (id && party->Has(id)) party->Dismiss(b, id, why, partyEvents);
}

Task* Tasks::Accept(Board& b, std::uint64_t squad, double now, const TaskHost&, std::vector<TaskEvent>& ev)
{
	auto it = offers.find(squad);
	if (it == offers.end() || it->second.task.type == TaskType::None) return nullptr;
	const int slot = FreeSlot();
	if (slot < 0 || ByGiver(squad) || !StillValid(b, it->second.task)) return nullptr;
	Task t = it->second.task;
	if (!Setup(b, t, now)) return nullptr;
	offers.erase(squad);
	t.id = nextId++;
	t.slot = slot;
	t.stage = TaskStage::Open;
	t.acceptedAt = now;
	active.push_back(t);
	RefreshPins(b);
	TaskEvent e;
	e.kind = TaskEvent::Kind::Accepted;
	e.task = t;
	ev.push_back(e);
	return &active.back();
}

// ------------------------------------------------------------------ progress
void Tasks::Finish(std::size_t i, TaskStage stage, double now, float goodwill, float memory, const std::string& why, std::vector<TaskEvent>& ev,
                   std::int32_t gold, FormID take, std::int32_t takeCount)
{
	const TaskNumbers N = Eff();
	Task t = active[i];
	t.stage = stage;
	TaskEvent e;
	e.kind = stage == TaskStage::Done ? TaskEvent::Kind::Done : TaskEvent::Kind::Failed;
	e.task = t;
	e.gold = gold;
	e.goodwill = goodwill;
	e.memory = memory;
	e.takeItem = take;
	e.takeCount = takeCount;
	e.why = why;
	ev.push_back(e);
	cooldown[t.giver] = now + N.repeatHours;   // task_manager.script:215-246
	offers.erase(t.giver);
	active.erase(active.begin() + std::ptrdiff_t(i));
}

void Tasks::RefreshPins(Board& b)
{
	m_pinned.clear();
	b.forced.clear();
	b.held.clear();
	for (auto& t : active) {
		if (t.stage == TaskStage::Open) {
			switch (t.type) {
			case TaskType::Assault:
			case TaskType::Rescue:
			case TaskType::Recover:
				for (auto id : t.pinned) m_pinned.insert(id);
				if (t.type == TaskType::Rescue && t.extra) b.held.insert(t.extra);
				break;
			case TaskType::Defend:
				m_pinned.insert(t.giver);
				if (t.extra) b.forced[t.extra] = t.node;
				break;
			default: break;
			}
		} else if (t.type == TaskType::Rescue && t.stage == TaskStage::Ready && t.step == 0 && t.extra) {
			b.forced[t.extra] = t.dest;   // no room in the party: the freed captive walks to the giver's place
		}
	}
	b.pinned = m_pinned;
}

void Tasks::Update(Board& b, double now, const TaskHost& h, std::vector<TaskEvent>& ev)
{
	const TaskNumbers N = Eff();
	const World& w = b.WorldRef();
	for (std::size_t i = 0; i < active.size();) {
		Task& t = active[i];
		const Group* giver = b.Find(t.giver);
		const bool giverGone = !giver || giver->livingCount() == 0;
		if (t.type == TaskType::Escort) {
			if (giverGone) {   // it died in your care
				Finish(i, TaskStage::Failed, now, -N.goodwillFail, -N.memoryFail, "escort dead", ev);
				continue;
			}
			if (party && !party->Has(t.giver)) {   // the player dismissed it: abandoned
				Finish(i, TaskStage::Failed, now, -N.goodwillFail, -N.memoryFail, "dismissed", ev);
				continue;
			}
		} else if (giverGone) {   // the squad that hired you is gone: nobody to pay, nobody to blame
			Release(b, t, PartyEvent::Kind::Left);
			Finish(i, TaskStage::Failed, now, 0.f, 0.f, "giver dead", ev);
			continue;
		}
		if (N.expireDays > 0.0 && now - t.acceptedAt > N.expireDays * 24.0) {
			Release(b, t, PartyEvent::Kind::Left);
			Finish(i, TaskStage::Failed, now, -N.goodwillFail, -N.memoryFail, "expired", ev);
			continue;
		}
		auto setStage = [&](TaskStage s, TaskEvent::Kind k) {
			if (t.stage == s) return;
			t.stage = s;
			TaskEvent e;
			e.kind = k;
			e.task = t;
			ev.push_back(e);
		};
		auto progress = [&]() {
			TaskEvent e;
			e.kind = TaskEvent::Kind::Progress;
			e.task = t;
			ev.push_back(e);
		};
		bool failed = false;
		switch (t.type) {
		case TaskType::Bounty:
			if (t.stage == TaskStage::Open) {
				const Group* g = b.Find(t.target);
				const Member* m = g ? MemberById(*g, t.targetMember) : nullptr;
				if (!m || !m->alive()) setStage(TaskStage::Ready, TaskEvent::Kind::Ready);   // whoever killed them (tasks_bounty.script:92-112)
			}
			break;
		case TaskType::Assault:
			if (t.stage == TaskStage::Open) {
				bool any = false;
				for (auto id : t.pinned) any |= Living(b, id);
				if (!any) setStage(TaskStage::Ready, TaskEvent::Kind::Ready);
			}
			break;
		case TaskType::Delivery: {
			const Group* g = b.Find(t.target);
			if (!g || g->livingCount() == 0) {
				Finish(i, TaskStage::Failed, now, 0.f, 0.f, "recipient dead", ev, 0, t.item, 1);
				failed = true;
				break;
			}
			if (h.PlayerItemCount(t.item) > 0) t.parcelMissingSince = -1.0;
			else if (t.parcelMissingSince < 0.0) t.parcelMissingSince = now;
			else if (now - t.parcelMissingSince > N.parcelGraceHours) {
				Finish(i, TaskStage::Failed, now, -N.goodwillFail, -N.memoryFail, "parcel lost", ev);
				failed = true;
			}
			break;
		}
		case TaskType::Fetch:
		case TaskType::Recover: {
			const bool have = h.PlayerItemCount(t.item) >= std::max(1, t.count);
			if (have) setStage(TaskStage::Ready, TaskEvent::Kind::Ready);
			else if (t.stage == TaskStage::Ready) setStage(TaskStage::Open, TaskEvent::Kind::Reopened);
			break;
		}
		case TaskType::Escort:
			if (t.stage == TaskStage::Open) {
				const GraphNode* d = w.Node(t.dest);
				if (d && Dist2D(giver->x, giver->y, d->x, d->y) <= N.escortArrive) {
					if (t.deadline > 0.0 && now > t.deadline) {   // late: half the pay (tasks_guide.script)
						t.step = 1;
						t.gold = int(float(t.gold) * N.escortLatePay);
					}
					setStage(TaskStage::Ready, TaskEvent::Kind::Ready);
				}
			}
			break;
		case TaskType::Defend:
			if (t.stage == TaskStage::Open && !Living(b, t.extra)) {   // this wave is wiped (anyone's kills)
				bool next = false;
				while (!next && t.step < t.count) next = SpawnWave(b, t);   // invalidates giver
				if (next) progress();
				else setStage(TaskStage::Ready, TaskEvent::Kind::Ready);
			}
			break;
		case TaskType::Rescue: {
			Group* cap = b.Find(t.extra);
			if (!cap || cap->livingCount() == 0) {
				Finish(i, TaskStage::Failed, now, -N.goodwillFail, -N.memoryFail, "captive dead", ev);
				failed = true;
				break;
			}
			if (t.stage == TaskStage::Open) {
				bool any = false;
				for (auto id : t.pinned) any |= Living(b, id);
				if (!any) {   // the captors are dead: the captive joins the player, or walks home when the party is full
					b.held.erase(cap->id);
					t.dest = giver->currentTarget >= 0 ? std::uint32_t(giver->currentTarget) : giver->node;
					t.step = party && party->Join(b, *cap, now, partyEvents) ? 1 : 0;
					setStage(TaskStage::Ready, TaskEvent::Kind::Ready);
				}
			} else if (t.stage == TaskStage::Ready && t.step == 1 && party && !party->Has(cap->id)) {
				Finish(i, TaskStage::Failed, now, -N.goodwillFail, -N.memoryFail, "dismissed", ev);
				failed = true;
				break;
			}
			t.atGiver = t.stage == TaskStage::Ready && cap->ws == giver->ws && Dist2D(cap->x, cap->y, giver->x, giver->y) <= N.rescueArrive;
			break;
		}
		case TaskType::Hunt:
			if (t.stage == TaskStage::Open) {   // a dead or missing mark moves the hunt on (tasks_top_10.script)
				bool moved = false;
				const Member* mm = nullptr;
				const Group* g = nullptr;
				for (const Task::Mark* m = CurrentMark(t); m; m = CurrentMark(t)) {
					g = b.Find(m->squad);
					mm = g ? MemberById(*g, m->member) : nullptr;
					if (mm && mm->alive()) break;
					mm = nullptr;
					++t.step;
					moved = true;
				}
				if (!mm) setStage(TaskStage::Ready, TaskEvent::Kind::Ready);
				else if (moved) {
					t.target = g->id;
					t.targetMember = mm->id;
					t.targetFaction = g->faction;
					t.targetName = mm->name;
					t.node = w.Nearest(g->x, g->y, g->ws);
					t.place = PlaceNear(w, g->x, g->y, g->ws);
					progress();
				}
			}
			break;
		default: break;
		}
		if (!failed) ++i;
	}
	RefreshPins(b);
}

bool Tasks::TurnIn(Board& b, std::uint64_t giver, double now, const TaskHost& h, std::vector<TaskEvent>& ev)
{
	const TaskNumbers N = Eff();
	for (std::size_t i = 0; i < active.size(); ++i) {
		const Task& t = active[i];
		if (t.giver != giver || t.type == TaskType::Delivery) continue;
		const bool byItem = t.type == TaskType::Fetch || t.type == TaskType::Recover;
		if (byItem && h.PlayerItemCount(t.item) < std::max(1, t.count)) return false;
		if (t.stage != TaskStage::Ready && !byItem) return false;
		if (t.type == TaskType::Rescue) {   // the freed captive must stand by the giver
			const Group* cap = b.Find(t.extra);
			const Group* g = b.Find(t.giver);
			if (!cap || !g || cap->ws != g->ws || Dist2D(cap->x, cap->y, g->x, g->y) > N.rescueArrive) return false;
		}
		const FormID take = byItem ? t.item : 0;
		const std::int32_t n = byItem ? std::max(1, t.count) : 0;
		const Task done = t;
		Release(b, done, PartyEvent::Kind::Left);
		if (done.type == TaskType::Escort)   // the squad stays at the place you brought it to, then goes its own way
			if (Group* g = b.Find(done.giver); g && b.WorldRef().Node(done.dest) && b.WorldRef().Node(done.dest)->ws == g->ws) {
				b.AssignToSmart(*g, done.dest);
				g->assignedTarget = g->currentTarget = done.dest;
				g->action = Action::Stay;
				g->stayStart = b.now;
			}
		Finish(i, TaskStage::Done, now, N.goodwillDone, N.memoryDone, "done", ev, done.gold, take, n);
		RefreshPins(b);
		return true;
	}
	return false;
}

bool Tasks::Deliver(std::uint64_t recipient, double now, const TaskHost& h, std::vector<TaskEvent>& ev)
{
	const TaskNumbers N = Eff();
	for (std::size_t i = 0; i < active.size(); ++i) {
		const Task& t = active[i];
		if (t.type != TaskType::Delivery || t.target != recipient || t.stage != TaskStage::Open) continue;
		if (h.PlayerItemCount(t.item) < 1) return false;
		Finish(i, TaskStage::Done, now, N.goodwillDone, N.memoryDone, "delivered", ev, t.gold, t.item, 1);
		return true;
	}
	return false;
}

bool Tasks::Abandon(Board& b, std::uint64_t giver, double now, std::vector<TaskEvent>& ev)
{
	const TaskNumbers N = Eff();
	for (std::size_t i = 0; i < active.size(); ++i) {
		if (active[i].giver != giver) continue;
		const Task t = active[i];
		Release(b, t, PartyEvent::Kind::Left);
		const bool takeBack = t.type == TaskType::Delivery || t.type == TaskType::Recover;   // the parcel / the satchel go back
		Finish(i, TaskStage::Failed, now, -N.goodwillFail, -N.memoryFail, "abandoned", ev, 0, takeBack ? t.item : 0, takeBack ? 1 : 0);
		RefreshPins(b);
		return true;
	}
	return false;
}

int Tasks::StateFor(std::uint64_t squad, const TaskHost& h) const
{
	int st = kTaskNone;
	for (auto& t : active) {
		if (t.type == TaskType::Delivery && t.target == squad && t.stage == TaskStage::Open && h.PlayerItemCount(t.item) > 0) return kTaskDeliverHere;
		if (t.giver != squad) continue;
		bool ready = t.stage == TaskStage::Ready;
		if (t.type == TaskType::Fetch || t.type == TaskType::Recover) ready |= h.PlayerItemCount(t.item) >= std::max(1, t.count);
		if (t.type == TaskType::Rescue) ready = ready && t.atGiver;
		st = std::max(st, ready && t.type != TaskType::Delivery ? int(kTaskReady) : int(kTaskWorking));
	}
	return st;
}

std::string Tasks::Objective(const Task& t, bool ret)
{
	const std::string where = t.place.empty() ? std::string{} : " near " + t.place;
	const std::string giver = t.giverName + " (" + FactionName(t.giverFaction) + ")";
	const std::string captive = t.itemName.empty() ? std::string("the captive") : t.itemName;
	if (ret) {
		switch (t.type) {
		case TaskType::Escort: return "Talk to " + giver + " for your pay";
		case TaskType::Rescue: return "Bring " + captive + " back to " + giver;
		default: return "Return to " + giver + " for your pay";
		}
	}
	switch (t.type) {
	case TaskType::Bounty: return "Kill " + t.targetName + " (" + FactionName(t.targetFaction) + ")" + where;
	case TaskType::Assault: return "Clear the " + std::string(FactionName(t.targetFaction)) + " out of " + (t.place.empty() ? "their camp" : t.place);
	case TaskType::Delivery: return "Deliver the sealed parcel to " + t.targetName + " (" + FactionName(t.targetFaction) + ")" + where;
	case TaskType::Fetch: return "Bring " + std::to_string(t.count) + " " + t.itemName + " to " + t.giverName;
	case TaskType::Escort: return "Escort " + giver + " to " + (t.place.empty() ? "their destination" : t.place);
	case TaskType::Defend:
		return "Hold " + (t.place.empty() ? std::string("the camp") : t.place) + " with " + t.giverName + " against the " + FactionName(t.targetFaction) +
		       " (wave " + std::to_string(std::clamp(t.step, 1, std::max(1, t.count))) + " of " + std::to_string(std::max(1, t.count)) + ")";
	case TaskType::Rescue:
		if (IsHumanFaction(t.targetFaction)) return "Free " + captive + " (" + FactionName(t.giverFaction) + "), taken by the " + FactionName(t.targetFaction) + where;
		return "Save " + captive + " (" + FactionName(t.giverFaction) + "), wounded and cornered by " + FactionName(t.targetFaction) + where;
	case TaskType::Recover: return "Recover the satchel from the body" + where;
	case TaskType::Hunt: {
		const int n = int(t.marks.size());
		const int left = std::max(0, n - t.step);
		return "Kill " + t.targetName + " (" + FactionName(t.targetFaction) + "), #" + std::to_string(std::max(1, left)) + " of the " + std::to_string(n) +
		       " most dangerous" + where;
	}
	default: return {};
	}
}

// ------------------------------------------------------------------ co-save
void Tasks::Save(std::vector<std::uint8_t>& out, std::uint32_t version) const
{
	Bytes w{ out };
	w.v(nextId);
	w.v(std::uint32_t(active.size()));
	for (auto& t : active) SaveTask(w, t, version);
	w.v(std::uint32_t(offers.size()));
	for (auto& [id, o] : offers) { w.v(id); w.v(o.until); SaveTask(w, o.task, version); }
	w.v(std::uint32_t(cooldown.size()));
	for (auto& [id, until] : cooldown) { w.v(id); w.v(until); }
}

bool Tasks::Load(const std::uint8_t* d, std::size_t n, std::uint32_t version, std::size_t* used)
{
	Reset();
	if (version < 1 || version > kSaveVersion) return false;
	Reader r{ d, n };
	nextId = r.v<std::uint32_t>();
	for (auto k = r.v<std::uint32_t>(); r.ok && k && k <= 16; --k) {
		Task t = LoadTask(r, version);
		if (r.ok && t.slot >= 0 && t.slot < 4) active.push_back(t);
	}
	for (auto k = r.v<std::uint32_t>(); r.ok && k && k < 100000; --k) {
		const auto id = r.v<std::uint64_t>();
		Offer o;
		o.until = r.v<double>();
		o.task = LoadTask(r, version);
		if (r.ok) offers[id] = o;
	}
	for (auto k = r.v<std::uint32_t>(); r.ok && k && k < 100000; --k) {
		const auto id = r.v<std::uint64_t>();
		const auto until = r.v<double>();
		if (r.ok) cooldown[id] = until;
	}
	if (used) *used = r.o;
	if (!r.ok) { Reset(); return false; }
	return true;
}

}  // namespace ALife
