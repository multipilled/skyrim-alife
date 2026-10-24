// Stashes (docs/STASHES.md): engine-free model; see include/ALife/Stashes.h.
#include "ALife/Stashes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ALife/World.h"

namespace ALife {

namespace {
	constexpr float kPi = 3.14159265f;

	FormID Hex(const std::string& s, bool& ok)
	{
		try { ok = true; return FormID(std::stoul(s, nullptr, 16)); } catch (...) { ok = false; return 0; }
	}

	bool Pair(const Ltx& t, const char* sec, const char* key, float& lo, float& hi, std::string& err)
	{
		if (!t.Has(sec, key)) return true;
		const auto v = t.List(sec, key);
		try {
			if (v.size() != 2) throw 0;
			lo = std::stof(v[0]);
			hi = std::stof(v[1]);
		} catch (...) { err = std::string("stashes.ltx: [") + sec + "] " + key + " needs two numbers"; return false; }
		if (hi < lo) std::swap(lo, hi);
		return true;
	}

	template <std::size_t N, class T>
	bool Five(const Ltx& t, const char* key, std::array<T, N>& out, std::string& err)
	{
		if (!t.Has("loot", key)) return true;
		const auto v = t.List("loot", key);
		if (v.size() != N) { err = std::string("stashes.ltx: [loot] ") + key + " needs " + std::to_string(N) + " values"; return false; }
		try { for (std::size_t i = 0; i < N; ++i) out[i] = T(std::stof(v[i])); } catch (...) { err = std::string("stashes.ltx: [loot] ") + key; return false; }
		return true;
	}

	float Dist(float ax, float ay, float bx, float by)
	{
		const float dx = ax - bx, dy = ay - by;
		return std::sqrt(dx * dx + dy * dy);
	}

	void AddItem(std::vector<Item>& v, FormID f, int n)
	{
		if (n <= 0) return;
		for (auto& it : v) if (it.form == f) { it.count += n; return; }
		v.push_back({ f, n });
	}
}

// ------------------------------------------------------------------ config
bool LoadStashConfig(const Ltx& t, StashConfig& c, std::string& err)
{
	c = StashConfig{};
	if (!t.Has("stashes")) { err = "stashes.ltx: no [stashes] section"; return false; }
	const char* S = "stashes";
	c.enabled = t.GetB(S, "enabled", c.enabled);
	for (auto& k : t.List(S, "kinds")) {
		bool ok = false;
		const Kind kind = KindFromName(k, &ok);
		if (!ok) { err = "stashes.ltx: kinds: unknown place kind '" + k + "'"; return false; }
		c.kinds[int(kind)] = true;
	}
	c.spotsPerPlace = std::clamp(t.GetI(S, "spots_per_place", c.spotsPerPlace), 0, 4);
	if (!Pair(t, S, "offset", c.offsetMin, c.offsetMax, err)) return false;
	c.maxSpots = std::max(0, t.GetI(S, "max_spots", c.maxSpots));
	c.hiddenShare = std::clamp(t.GetF(S, "hidden_share", c.hiddenShare), 0.f, 1.f);
	c.revealRadius = std::max(0.f, t.GetF(S, "reveal_radius", c.revealRadius));
	c.revealMin = std::max(0.f, t.GetF(S, "reveal_min", c.revealMin));
	c.placeRadius = std::max(0.f, t.GetF(S, "place_radius", c.placeRadius));
	c.markerSlots = std::clamp(t.GetI(S, "marker_slots", c.markerSlots), 0, 32);
	c.noteChance = std::clamp(t.GetF("notes", "chance", c.noteChance), 0.f, 1.f);
	c.rewardChance = std::clamp(t.GetF("reward", "chance", c.rewardChance), 0.f, 1.f);
	c.replaceGold = t.GetB("reward", "replace_gold", c.replaceGold);
	c.budgetPerGold = std::max(0.f, t.GetF("reward", "budget_per_gold", c.budgetPerGold));
	if (!Pair(t, "loot", "budget", c.budgetMin, c.budgetMax, err)) return false;
	c.budgetMin = std::max(0.f, c.budgetMin);
	c.budgetMax = std::max(c.budgetMin, c.budgetMax);
	c.perLevel = std::max(0.f, t.GetF("loot", "per_level", c.perLevel));
	if (!Five(t, "tier_levels", c.tierLevels, err) || !Five(t, "tier_chance", c.tierChance, err) || !Five(t, "tier_count", c.tierCount, err)) return false;
	for (auto& v : c.tierChance) v = std::clamp(v, 0.f, 1.f);
	for (auto& v : c.tierCount) v = std::max(0.f, v);
	bool ok = true;
	if (t.Has("loot", "gold_form")) { c.goldForm = Hex(t.Get("loot", "gold_form"), ok); if (!ok) { err = "stashes.ltx: [loot] gold_form"; return false; } }
	for (auto& name : t.List("loot", "kinds")) {
		const std::string sec = "kind." + name;
		if (!t.Has(sec)) { err = "stashes.ltx: no [" + sec + "]"; return false; }
		LootKind k;
		k.name = name;
		k.chance = std::clamp(t.GetF(sec, "chance", k.chance), 0.f, 100.f);
		float lo = 1, hi = 1;
		if (!Pair(t, sec.c_str(), "count", lo, hi, err)) return false;
		k.countLo = std::max(1, int(lo));
		k.countHi = std::max(k.countLo, int(hi));
		for (int ti = 0; ti < kStashTiers; ++ti)
			for (auto& e : t.List(sec, "t" + std::to_string(ti + 1))) {
				const auto colon = e.find(':');
				LootItem it;
				it.form = Hex(e.substr(0, colon), ok);
				try { it.value = colon == std::string::npos ? 1 : std::max(1, std::stoi(e.substr(colon + 1))); } catch (...) { ok = false; }
				if (!ok || !it.form) { err = "stashes.ltx: [" + sec + "] t" + std::to_string(ti + 1) + ": '" + e + "'"; return false; }
				k.tiers[std::size_t(ti)].push_back(it);
			}
		c.loot.push_back(std::move(k));
	}
	if (t.Has("containers", "default")) { c.containerDefault = Hex(t.Get("containers", "default"), ok); if (!ok) { err = "stashes.ltx: [containers] default"; return false; } }
	if (auto* lines = t.Section("containers"))
		for (auto& kv : *lines) {
			if (kv.first == "default") continue;
			bool kok = false;
			const Kind kind = KindFromName(kv.first, &kok);
			if (!kok) { err = "stashes.ltx: [containers] unknown place kind '" + kv.first + "'"; return false; }
			c.containers[int(kind)] = Hex(kv.second, ok);
			if (!ok) { err = "stashes.ltx: [containers] " + kv.first; return false; }
		}
	return true;
}

// ------------------------------------------------------------------ spots
void Stashes::Build(const World& w, std::uint64_t seed)
{
	spots.clear();
	if (!cfg.enabled) return;
	Rng rng(Mix64(seed ^ 0x57A54ULL));
	for (const auto& n : w.nodes) {
		if (int(spots.size()) >= cfg.maxSpots) break;
		if (!cfg.kinds[int(n.kind)] || n.city || n.playerHome) continue;
		for (int k = 0; k < cfg.spotsPerPlace && int(spots.size()) < cfg.maxSpots; ++k) {
			StashSpot s;
			s.id = std::uint32_t(spots.size()) + 1;
			s.node = n.id;
			s.ws = n.ws;
			const float a = rng.uniform(0.f, 2.f * kPi), d = rng.uniform(cfg.offsetMin, cfg.offsetMax);
			s.x = n.x + std::cos(a) * d;
			s.y = n.y + std::sin(a) * d;
			s.z = n.z;
			s.base = cfg.containers[int(n.kind)] ? cfg.containers[int(n.kind)] : cfg.containerDefault;
			if (rng.chance(cfg.hiddenShare)) s.state = StashSpot::State::Filled;   // Anomaly: half the boxes, content on first use
			spots.push_back(std::move(s));
		}
	}
}

float Stashes::Budget(int level, float economy, Rng& rng) const
{
	const float e = std::isfinite(economy) ? std::max(0.f, economy) : 1.f;
	return rng.uniform(cfg.budgetMin, cfg.budgetMax) * (1.f + cfg.perLevel * float(std::max(0, level))) * e;
}

int Stashes::Roll(StashSpot& s, float budget, int level, Rng& rng)
{
	s.items.clear();
	float left = std::max(0.f, budget);
	std::vector<int> open;
	for (int t = 0; t < kStashTiers; ++t) if (level >= cfg.tierLevels[std::size_t(t)]) open.push_back(t);
	if (open.empty()) open.push_back(0);
	std::vector<std::size_t> kinds(cfg.loot.size());
	for (std::size_t i = 0; i < kinds.size(); ++i) kinds[i] = i;
	float inc = 0.f;
	for (int round = 0; round < 64 && left >= 1.f && !kinds.empty(); ++round) {   // set_random_stash: rounds until the budget is spent
		for (std::size_t i = kinds.size(); i > 1; --i) std::swap(kinds[i - 1], kinds[std::size_t(rng.index(int(i)))]);
		bool picked = false;
		for (auto ki : kinds) {
			const LootKind& k = cfg.loot[ki];
			std::vector<int> tiers = open;
			for (std::size_t i = tiers.size(); i > 1; --i) std::swap(tiers[i - 1], tiers[std::size_t(rng.index(int(i)))]);
			for (int t : tiers) {
				const auto& pool = k.tiers[std::size_t(t)];
				if (pool.empty()) continue;
				if (float(rng.range(1, 100)) >= k.chance * cfg.tierChance[std::size_t(t)] + inc) continue;
				const LootItem& it = pool[std::size_t(rng.index(int(pool.size())))];
				int n = std::max(1, int(std::lround(float(rng.range(k.countLo, k.countHi)) * cfg.tierCount[std::size_t(t)])));
				n = std::min(n, int(left / float(it.value)));
				if (n <= 0) continue;
				AddItem(s.items, it.form, n);
				left -= float(n * it.value);
				picked = true;
				inc -= 10.f;
				break;   // one pick per kind per round
			}
			if (left < 1.f) break;
		}
		if (!picked) inc += 10.f;
	}
	if (left >= 1.f && cfg.goldForm) AddItem(s.items, cfg.goldForm, int(left));   // the rest as coins: a stash is worth its budget
	s.rolled = true;
	++counters.rolls;
	return Value(s);
}

int Stashes::Value(const StashSpot& s) const
{
	int v = 0;
	for (auto& it : s.items) {
		int each = it.form == cfg.goldForm ? 1 : 0;
		for (auto& k : cfg.loot)
			for (auto& tier : k.tiers)
				for (auto& li : tier)
					if (li.form == it.form) each = li.value;
		v += each * it.count;
	}
	return v;
}

// ------------------------------------------------------------------ reveal and markers
void Stashes::Mark(StashSpot& s, double, std::vector<StashEvent>& ev, const char* why)
{
	if (s.slot >= 0 || cfg.markerSlots <= 0) return;
	std::vector<bool> used(std::size_t(cfg.markerSlots), false);
	for (auto& o : spots) if (o.slot >= 0 && o.slot < cfg.markerSlots) used[std::size_t(o.slot)] = true;
	int slot = -1;
	for (int i = 0; i < cfg.markerSlots; ++i) if (!used[std::size_t(i)]) { slot = i; break; }
	if (slot < 0) {   // all taken: the oldest marked stash gives its marker up (it stays filled)
		StashSpot* oldest = nullptr;
		for (auto& o : spots) if (o.slot >= 0 && (!oldest || o.markedAt < oldest->markedAt)) oldest = &o;
		if (!oldest) return;
		slot = oldest->slot;
		oldest->slot = -1;
		ev.push_back({ StashEvent::Kind::Unmarked, oldest->id, slot, "evicted" });
		++counters.evicted;
	}
	s.slot = std::int8_t(slot);
	s.markedAt = ++m_clock;
	ev.push_back({ StashEvent::Kind::Marked, s.id, slot, why ? why : "" });
}

int Stashes::Reveal(float x, float y, int ws, int level, float economy, Rng& rng, std::vector<StashEvent>& ev, const char* why, float minBudget)
{
	if (!cfg.enabled || spots.empty()) return -1;
	const bool reward = minBudget > 0.f;
	// a reward needs content the core can still top up: an empty spot, or a filled one whose container is not placed yet
	auto usable = [&](const StashSpot& s, bool filled) {
		if (s.ws != ws || s.slot >= 0) return false;
		if (!filled) return !s.Filled();
		return s.Filled() && (!reward || (s.ref == 0 && (!s.rolled || !s.items.empty())));
	};
	std::vector<StashSpot*> pick;
	for (bool filled : { false, true }) {   // empty boxes first (Anomaly), then a hidden filled one there
		for (auto& s : spots) {
			if (!usable(s, filled)) continue;
			const float d = Dist(s.x, s.y, x, y);
			if (d >= cfg.revealMin && d <= cfg.revealRadius) pick.push_back(&s);
		}
		if (!pick.empty()) break;
	}
	if (pick.empty()) {   // nothing in range: the nearest empty (else unmarked filled) spot in the worldspace
		for (bool filled : { false, true }) {
			StashSpot* best = nullptr;
			float bd = 1e30f;
			for (auto& s : spots) {
				if (!usable(s, filled)) continue;
				const float d = Dist(s.x, s.y, x, y);
				if (d >= cfg.revealMin && d < bd) { bd = d; best = &s; }
			}
			if (best) { pick.push_back(best); break; }
		}
	}
	if (pick.empty()) return -1;
	StashSpot& s = *pick[std::size_t(rng.index(int(pick.size())))];
	const float budget = std::max(Budget(level, economy, rng), minBudget);
	if (!s.Filled() || !s.rolled) {
		s.state = StashSpot::State::Filled;
		Roll(s, budget, level, rng);
	} else if (reward && Value(s) < int(minBudget)) {
		AddItem(s.items, cfg.goldForm, int(minBudget) - Value(s));   // a hidden stash used as a reward is topped up to the gold it replaces
	}
	Mark(s, 0, ev, why);
	++counters.revealed;
	return int(s.id);
}

// ------------------------------------------------------------------ notes, rewards, looting
bool Stashes::CarriesNote(const Member& m, FormID note)
{
	if (!note) return false;
	for (auto& it : m.inventory) if (it.form == note && it.count > 0) return true;
	return false;
}

bool Stashes::OnKill(const Group& vg, const Member& victim, Group* killer, bool online, Rng& rng)
{
	if (!cfg.enabled || !cfg.noteForm || !IsHumanFaction(vg.faction)) return false;
	const bool carried = CarriesNote(victim, cfg.noteForm);
	const bool fresh = !carried && rng.chance(cfg.noteChance);
	if (!carried && !fresh) return false;
	if (fresh) ++counters.notesMade;
	if (online) return true;   // the plugin puts it on the body (a carried one is on the actor already)
	if (killer && IsHumanFaction(killer->faction))
		for (auto& m : killer->members)
			if (m.alive() && !CarriesNote(m, cfg.noteForm)) {   // the winner takes it off the body
				AddItem(m.inventory, cfg.noteForm, 1);
				++counters.notesPassed;
				return true;
			}
	++counters.notesLost;
	return true;
}

int Stashes::OnNoteRead(float x, float y, int ws, int level, float economy, Rng& rng, std::vector<StashEvent>& ev)
{
	++counters.notesRead;
	return Reveal(x, y, ws, level, economy, rng, ev, "note");
}

bool Stashes::TaskReward(std::int32_t& gold, float x, float y, int ws, int level, float economy, Rng& rng, std::vector<StashEvent>& ev)
{
	if (!cfg.enabled || cfg.rewardChance <= 0.f || !rng.chance(cfg.rewardChance)) return false;
	const float minBudget = cfg.replaceGold ? std::max(1.f, float(std::max(0, gold)) * cfg.budgetPerGold) : 1.f;
	if (Reveal(x, y, ws, level, economy, rng, ev, "reward", minBudget) < 0) return false;
	if (cfg.replaceGold) gold = 0;
	++counters.rewards;
	return true;
}

void Stashes::OnLooted(std::uint32_t id, std::vector<StashEvent>& ev)
{
	StashSpot* s = Find(id);
	if (!s || !s->Filled()) return;
	s->state = StashSpot::State::Empty;
	s->rolled = false;
	s->items.clear();
	if (s->slot >= 0) ev.push_back({ StashEvent::Kind::Unmarked, s->id, s->slot, "looted" });
	s->slot = -1;
	++found;
	ev.push_back({ StashEvent::Kind::Looted, s->id, -1, "looted" });
}

StashSpot* Stashes::Find(std::uint32_t id)
{
	return id >= 1 && id <= spots.size() && spots[id - 1].id == id ? &spots[id - 1] : nullptr;
}

const StashSpot* Stashes::Find(std::uint32_t id) const
{
	return id >= 1 && id <= spots.size() && spots[id - 1].id == id ? &spots[id - 1] : nullptr;
}

StashSpot* Stashes::FindByRef(FormID ref)
{
	if (!ref) return nullptr;
	for (auto& s : spots) if (s.ref == ref) return &s;
	return nullptr;
}

int Stashes::MarkedCount() const
{
	int n = 0;
	for (auto& s : spots) n += s.slot >= 0;
	return n;
}

std::vector<StashSpot*> Stashes::ToPlace(float x, float y, int ws)
{
	std::vector<StashSpot*> out;
	for (auto& s : spots)
		if (s.ws == ws && s.Filled() && (s.ref == 0 || !s.items.empty()) && Dist(s.x, s.y, x, y) <= cfg.placeRadius) out.push_back(&s);
	return out;
}

void Stashes::Placed(StashSpot& s, FormID ref)
{
	s.ref = ref;
	s.items.clear();
	s.rolled = true;
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

void Stashes::Save(std::vector<std::uint8_t>& out) const
{
	Bytes w{ out };
	w.v(std::uint32_t(spots.size()));
	for (auto& s : spots) {
		w.v(s.id); w.v(s.node); w.v(s.ws); w.v(s.x); w.v(s.y); w.v(s.z); w.v(s.base); w.v(std::uint8_t(s.state)); w.v(s.rolled); w.v(s.slot);
		w.v(s.ref); w.v(s.markedAt);
		w.v(std::uint32_t(s.items.size()));
		for (auto& it : s.items) { w.v(it.form); w.v(it.count); }
	}
	w.v(found);
	w.v(counters.notesRead); w.v(counters.revealed); w.v(counters.rewards);
}

bool Stashes::Load(const std::uint8_t* data, std::size_t size, std::uint32_t version)
{
	Reset();
	m_clock = 0;
	if (version != kSaveVersion || !data) return false;
	Reader r{ data, size };
	const auto n = r.v<std::uint32_t>();
	for (std::uint32_t i = 0; r.ok && i < n && i < 100000; ++i) {
		StashSpot s;
		s.id = r.v<std::uint32_t>(); s.node = r.v<std::uint32_t>(); s.ws = r.v<std::uint8_t>(); s.x = r.v<float>(); s.y = r.v<float>(); s.z = r.v<float>();
		s.base = r.v<FormID>();
		s.state = r.v<std::uint8_t>() ? StashSpot::State::Filled : StashSpot::State::Empty;
		s.rolled = r.v<bool>(); s.slot = r.v<std::int8_t>(); s.ref = r.v<FormID>(); s.markedAt = r.v<double>();
		for (auto k = r.v<std::uint32_t>(); r.ok && k && k <= 256; --k) {
			Item it;
			it.form = r.v<FormID>();
			it.count = r.v<std::int32_t>();
			if (r.ok) s.items.push_back(it);
		}
		if (s.slot >= cfg.markerSlots) s.slot = -1;   // fewer slots in the config now
		m_clock = std::max(m_clock, s.markedAt);
		if (r.ok) spots.push_back(std::move(s));
	}
	found = r.v<std::uint64_t>();
	counters.notesRead = r.v<std::uint64_t>(); counters.revealed = r.v<std::uint64_t>(); counters.rewards = r.v<std::uint64_t>();
	bool ids = true;
	for (std::size_t i = 0; i < spots.size(); ++i) ids = ids && spots[i].id == i + 1;   // Find() indexes by id
	if (!r.ok || !ids) { Reset(); return false; }
	return true;
}

}  // namespace ALife
