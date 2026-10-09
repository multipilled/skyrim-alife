// Tasks: the engine-free job model (docs/TASKS.md).
#include "ALife/Tasks.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ALife/Board.h"
#include "ALife/World.h"

namespace ALife {

namespace {
	const char* kTypeIds[kTaskTypes] = { "none", "bounty", "assault", "delivery", "fetch" };

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

	void SaveTask(Bytes& w, const Task& t)
	{
		w.v(t.id); w.v(t.type); w.v(t.stage); w.v(t.slot); w.v(t.giver); w.v(t.giverFaction); w.s(t.giverName);
		w.v(t.target); w.v(t.targetMember); w.v(t.targetFaction); w.s(t.targetName);
		w.v(std::uint32_t(t.pinned.size()));
		for (auto p : t.pinned) w.v(p);
		w.v(t.node); w.s(t.place); w.v(t.item); w.s(t.itemName); w.v(t.count); w.v(t.gold);
		w.v(t.offeredAt); w.v(t.acceptedAt); w.v(t.parcelMissingSince);
	}
	Task LoadTask(Reader& r)
	{
		Task t;
		t.id = r.v<std::uint32_t>(); t.type = r.v<TaskType>(); t.stage = r.v<TaskStage>(); t.slot = r.v<std::int32_t>();
		t.giver = r.v<std::uint64_t>(); t.giverFaction = r.v<Faction>(); t.giverName = r.s();
		t.target = r.v<std::uint64_t>(); t.targetMember = r.v<std::uint64_t>(); t.targetFaction = r.v<Faction>(); t.targetName = r.s();
		for (auto n = r.v<std::uint32_t>(); r.ok && n && n < 64; --n) t.pinned.push_back(r.v<std::uint64_t>());
		t.node = r.v<std::uint32_t>(); t.place = r.s(); t.item = r.v<FormID>(); t.itemName = r.s(); t.count = r.v<std::int32_t>(); t.gold = r.v<std::int32_t>();
		t.offeredAt = r.v<double>(); t.acceptedAt = r.v<double>(); t.parcelMissingSince = r.v<double>();
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
		if (t.target == squad) return true;
		for (auto p : t.pinned) if (p == squad) return true;
	}
	return false;
}

int Tasks::FreeSlot() const
{
	for (int s = 0; s < cfg.maxActive; ++s) {
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
	std::vector<std::pair<float, const Group*>> c;
	for (auto& s : b.squads) {
		if (s.id == giver.id || s.ws != giver.ws || !IsHumanFaction(s.faction) || s.livingCount() == 0) continue;
		const auto& cf = b.Cfg();
		if (!cf.IsEnemy(giver.faction, s.faction) || Targeted(s.id)) continue;
		if (s.avgLevel() > float(h.PlayerLevel() + cfg.maxLevelOver)) continue;
		if (w.InCityBuffer(s.x, s.y, kCityBuffer, s.ws)) continue;
		const float d = Dist2D(giver.x, giver.y, s.x, s.y);
		if (d <= cfg.bountyRange) c.emplace_back(d, &s);
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
	t.gold = int(cfg.bountyBase + cfg.bountyPerLevel * float(m->level));
	return true;
}

bool Tasks::FindAssault(Board& b, const World& w, const Group& giver, const TaskHost& h, Rng& rng, Task& t) const
{
	const auto& cf = b.Cfg();
	// places where enemies of the giver are staying, with the squads that hold each
	std::unordered_map<std::uint32_t, std::vector<const Group*>> held;
	for (auto& s : b.squads) {
		if (s.ws != giver.ws || s.livingCount() == 0 || s.action != Action::Stay || s.currentTarget < 0) continue;
		if (!cf.IsEnemy(giver.faction, s.faction) || Targeted(s.id)) continue;
		held[std::uint32_t(s.currentTarget)].push_back(&s);
	}
	std::vector<std::pair<float, const std::vector<const Group*>*>> c;
	std::vector<std::uint32_t> nodeOf;
	for (auto& [node, gs] : held) {
		const GraphNode* n = w.Node(node);
		if (!n || n->city || n->settlement || n->playerHome) continue;
		float lvl = 0.f;
		for (auto* g : gs) lvl = std::max(lvl, g->avgLevel());
		if (lvl > float(h.PlayerLevel() + cfg.maxLevelOver)) continue;
		const float d = Dist2D(giver.x, giver.y, n->x, n->y);
		if (d <= cfg.assaultRange) c.emplace_back(d, &gs);
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
	t.gold = int(cfg.assaultPerSquad * float(gs->size()) + cfg.assaultPerLevel * levels);
	return true;
}

bool Tasks::FindDelivery(Board& b, const World& w, const Group& giver, const TaskHost& h, Rng& rng, Task& t) const
{
	if (!cfg.parcel && cfg.parcelSpec.empty()) return false;
	const auto& cf = b.Cfg();
	std::vector<std::pair<float, const Group*>> c;
	for (auto& s : b.squads) {
		if (s.id == giver.id || s.ws != giver.ws || !IsHumanFaction(s.faction) || s.livingCount() == 0) continue;
		if (cf.IsEnemy(giver.faction, s.faction) || Targeted(s.id) || !h.Approachable(s)) continue;
		const float d = Dist2D(giver.x, giver.y, s.x, s.y);
		if (d >= cfg.deliveryMin && d <= cfg.deliveryMax) c.emplace_back(d, &s);
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
	t.gold = int(cfg.deliveryBase + cfg.deliveryPerKm * Dist2D(giver.x, giver.y, s->x, s->y) / 70000.f);
	return true;
}

bool Tasks::FindFetch(const Group& giver, const TaskHost& h, Rng& rng, Task& t) const
{
	std::vector<const FetchItem*> c;
	for (auto& it : cfg.fetch[int(giver.faction)])
		if ((it.form || !it.spec.empty()) && it.minLevel <= h.PlayerLevel()) c.push_back(&it);
	if (c.empty()) return false;
	const FetchItem& it = *c[std::size_t(rng.index(int(c.size())))];
	t.item = it.form;
	t.itemName = it.name;
	t.count = rng.range(it.lo, it.hi);
	const float value = float(h.ItemValue(it.form, it.value)) * float(t.count) * cfg.fetchMult;
	t.gold = int(value * rng.uniform(0.9f, 1.1f));   // tasks_fetch.script:202-206 (+-10%)
	return true;
}

TaskType Tasks::OfferOf(Board& b, const World& w, const Group& giver, double now, const TaskHost& h, Rng& rng)
{
	if (!IsHumanFaction(giver.faction) || giver.livingCount() == 0) return TaskType::None;
	if (ByGiver(giver.id)) return TaskType::None;                        // one job at a time from a squad
	if (int(active.size()) >= cfg.maxActive) return TaskType::None;
	if (auto it = cooldown.find(giver.id); it != cooldown.end()) {
		if (now < it->second) return TaskType::None;
		cooldown.erase(it);
	}
	if (auto it = offers.find(giver.id); it != offers.end()) {
		if (now < it->second.until && (it->second.task.type == TaskType::None || StillValid(b, it->second.task))) return it->second.task.type;
		offers.erase(it);
	}
	Offer o;
	o.until = now + cfg.offerHours;
	o.task.giver = giver.id;
	o.task.giverFaction = giver.faction;
	if (const Member* m = Leader(giver)) o.task.giverName = m->name;
	o.task.offeredAt = now;
	if (!rng.chance(cfg.noJobChance)) {
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
			default: break;
			}
			if (found) {
				t.gold = std::max(cfg.goldMin, int(float(t.gold) * cfg.goldMult));
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
		for (auto id : t.pinned)
			if (const Group* g = b.Find(id); g && g->livingCount() > 0 && g->currentTarget == std::int64_t(t.node)) return true;
		return false;
	case TaskType::Fetch: return true;
	default: return false;
	}
}

Task* Tasks::Accept(Board& b, std::uint64_t squad, double now, const TaskHost&, std::vector<TaskEvent>& ev)
{
	auto it = offers.find(squad);
	if (it == offers.end() || it->second.task.type == TaskType::None) return nullptr;
	const int slot = FreeSlot();
	if (slot < 0 || ByGiver(squad) || !StillValid(b, it->second.task)) return nullptr;
	Task t = it->second.task;
	offers.erase(it);
	if (t.type == TaskType::Assault) {   // only the squads still holding the place count
		t.pinned.erase(std::remove_if(t.pinned.begin(), t.pinned.end(), [&](std::uint64_t id) {
			const Group* g = b.Find(id);
			return !g || g->livingCount() == 0 || g->currentTarget != std::int64_t(t.node);
		}), t.pinned.end());
	}
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
	cooldown[t.giver] = now + cfg.repeatHours;   // task_manager.script:215-246
	offers.erase(t.giver);
	active.erase(active.begin() + std::ptrdiff_t(i));
}

void Tasks::RefreshPins(Board& b)
{
	m_pinned.clear();
	for (auto& t : active)
		if (t.type == TaskType::Assault && t.stage == TaskStage::Open)
			for (auto id : t.pinned) m_pinned.insert(id);
	b.pinned = m_pinned;
}

void Tasks::Update(Board& b, double now, const TaskHost& h, std::vector<TaskEvent>& ev)
{
	for (std::size_t i = 0; i < active.size();) {
		Task& t = active[i];
		const Group* giver = b.Find(t.giver);
		if (!giver || giver->livingCount() == 0) {   // the squad that hired you is gone: nobody to pay, nobody to blame
			Finish(i, TaskStage::Failed, now, 0.f, 0.f, "giver dead", ev);
			continue;
		}
		if (cfg.expireDays > 0.0 && now - t.acceptedAt > cfg.expireDays * 24.0) {
			Finish(i, TaskStage::Failed, now, -cfg.goodwillFail, -cfg.memoryFail, "expired", ev);
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
				for (auto id : t.pinned) if (const Group* g = b.Find(id); g && g->livingCount() > 0) any = true;
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
			else if (now - t.parcelMissingSince > cfg.parcelGraceHours) {
				Finish(i, TaskStage::Failed, now, -cfg.goodwillFail, -cfg.memoryFail, "parcel lost", ev);
				failed = true;
			}
			break;
		}
		case TaskType::Fetch: {
			const bool have = h.PlayerItemCount(t.item) >= t.count;
			if (have) setStage(TaskStage::Ready, TaskEvent::Kind::Ready);
			else if (t.stage == TaskStage::Ready) setStage(TaskStage::Open, TaskEvent::Kind::Reopened);
			break;
		}
		default: break;
		}
		if (!failed) ++i;
	}
	RefreshPins(b);
}

bool Tasks::TurnIn(std::uint64_t giver, double now, const TaskHost& h, std::vector<TaskEvent>& ev)
{
	for (std::size_t i = 0; i < active.size(); ++i) {
		const Task& t = active[i];
		if (t.giver != giver || t.type == TaskType::Delivery) continue;
		if (t.type == TaskType::Fetch && h.PlayerItemCount(t.item) < t.count) return false;
		if (t.stage != TaskStage::Ready && t.type != TaskType::Fetch) return false;
		const FormID take = t.type == TaskType::Fetch ? t.item : 0;
		const std::int32_t n = t.type == TaskType::Fetch ? t.count : 0;
		Finish(i, TaskStage::Done, now, cfg.goodwillDone, cfg.memoryDone, "done", ev, t.gold, take, n);
		return true;
	}
	return false;
}

bool Tasks::Deliver(std::uint64_t recipient, double now, const TaskHost& h, std::vector<TaskEvent>& ev)
{
	for (std::size_t i = 0; i < active.size(); ++i) {
		const Task& t = active[i];
		if (t.type != TaskType::Delivery || t.target != recipient || t.stage != TaskStage::Open) continue;
		if (h.PlayerItemCount(t.item) < 1) return false;
		Finish(i, TaskStage::Done, now, cfg.goodwillDone, cfg.memoryDone, "delivered", ev, t.gold, t.item, 1);
		return true;
	}
	return false;
}

bool Tasks::Abandon(std::uint64_t giver, double now, std::vector<TaskEvent>& ev)
{
	for (std::size_t i = 0; i < active.size(); ++i) {
		if (active[i].giver != giver) continue;
		const Task& t = active[i];
		Finish(i, TaskStage::Failed, now, -cfg.goodwillFail, -cfg.memoryFail, "abandoned", ev, 0, t.type == TaskType::Delivery ? t.item : 0,
		       t.type == TaskType::Delivery ? 1 : 0);
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
		const bool ready = t.stage == TaskStage::Ready || (t.type == TaskType::Fetch && h.PlayerItemCount(t.item) >= t.count);
		st = std::max(st, ready && t.type != TaskType::Delivery ? int(kTaskReady) : int(kTaskWorking));
	}
	return st;
}

std::string Tasks::Objective(const Task& t, bool ret)
{
	const std::string where = t.place.empty() ? std::string{} : " near " + t.place;
	if (ret) return "Return to " + t.giverName + " (" + FactionName(t.giverFaction) + ") for your pay";
	switch (t.type) {
	case TaskType::Bounty: return "Kill " + t.targetName + " (" + FactionName(t.targetFaction) + ")" + where;
	case TaskType::Assault: return "Clear the " + std::string(FactionName(t.targetFaction)) + " out of " + (t.place.empty() ? "their camp" : t.place);
	case TaskType::Delivery: return "Deliver the sealed parcel to " + t.targetName + " (" + FactionName(t.targetFaction) + ")" + where;
	case TaskType::Fetch: return "Bring " + std::to_string(t.count) + " " + t.itemName + " to " + t.giverName;
	default: return {};
	}
}

// ------------------------------------------------------------------ co-save
void Tasks::Save(std::vector<std::uint8_t>& out) const
{
	Bytes w{ out };
	w.v(nextId);
	w.v(std::uint32_t(active.size()));
	for (auto& t : active) SaveTask(w, t);
	w.v(std::uint32_t(offers.size()));
	for (auto& [id, o] : offers) { w.v(id); w.v(o.until); SaveTask(w, o.task); }
	w.v(std::uint32_t(cooldown.size()));
	for (auto& [id, until] : cooldown) { w.v(id); w.v(until); }
}

bool Tasks::Load(const std::uint8_t* d, std::size_t n, std::uint32_t version, std::size_t* used)
{
	Reset();
	if (version != kSaveVersion) return false;
	Reader r{ d, n };
	nextId = r.v<std::uint32_t>();
	for (auto k = r.v<std::uint32_t>(); r.ok && k && k <= 16; --k) {
		Task t = LoadTask(r);
		if (r.ok && t.slot >= 0 && t.slot < 4) active.push_back(t);
	}
	for (auto k = r.v<std::uint32_t>(); r.ok && k && k < 100000; --k) {
		const auto id = r.v<std::uint64_t>();
		Offer o;
		o.until = r.v<double>();
		o.task = LoadTask(r);
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
