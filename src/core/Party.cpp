// Party: companions and recruiting (docs/COMPANIONS.md). Engine-free.
#include "ALife/Party.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ALife/Board.h"

namespace ALife {

const char* OrderName(Order o)
{
	switch (o) {
	case Order::Follow: return "follow";
	case Order::Wait: return "wait";
	case Order::Move: return "move";
	}
	return "?";
}

bool LoadPartyConfig(const Ltx& t, PartyConfig& c, std::string& err)
{
	c = PartyConfig{};
	if (!t.Has("companions")) { err = "companions.ltx: no [companions] section"; return false; }
	c.maxMembers = std::clamp(t.GetI("companions", "max_members", c.maxMembers), 1, 8);
	c.maxSquads = std::clamp(t.GetI("companions", "max_squads", c.maxSquads), 1, kMaxOnlineGroups - 1);   // one slot always stays for the world
	for (auto& id : t.List("companions", "paid_factions")) {
		bool ok = false;
		const Faction f = FactionFromId(id, &ok);
		if (!ok || !IsHumanFaction(f)) { err = "companions.ltx: paid_factions: unknown faction '" + id + "'"; return false; }
		c.paid[int(f)] = true;
	}
	const char* tiers[3] = { "novice", "advanced", "veteran" };
	for (int i = 0; i < 3; ++i) {
		c.willing[i] = std::clamp(t.GetF("willing_chance", tiers[i], c.willing[i]), 0.f, 1.f);
		const auto r = t.List("price_factor", tiers[i]);
		if (r.size() == 2) {
			try { c.priceLo[i] = std::stof(r[0]); c.priceHi[i] = std::stof(r[1]); } catch (...) { err = "companions.ltx: price_factor " + std::string(tiers[i]); return false; }
		}
		if (c.priceHi[i] < c.priceLo[i]) std::swap(c.priceLo[i], c.priceHi[i]);
	}
	c.dayGold = std::max(1.f, t.GetF("companions", "day_gold", c.dayGold));
	const auto d = t.List("companions", "days");
	if (d.size() == 2) {
		try { c.days = { std::max(1, std::stoi(d[0])), std::max(1, std::stoi(d[1])) }; } catch (...) { err = "companions.ltx: days"; return false; }
	}
	c.warnHours = t.GetF("companions", "warn_hours", float(c.warnHours));
	c.teleportDistance = t.GetF("companions", "teleport_distance", c.teleportDistance);
	c.moveDistance = t.GetF("companions", "move_distance", c.moveDistance);
	c.waitPlaceDistance = t.GetF("companions", "wait_place_distance", c.waitPlaceDistance);
	c.lootDefault = t.GetB("companions", "loot_default", c.lootDefault);
	return true;
}

Companion* Party::Find(std::uint64_t id)
{
	for (auto& c : list) if (c.squad == id) return &c;
	return nullptr;
}

const Companion* Party::Find(std::uint64_t id) const
{
	for (auto& c : list) if (c.squad == id) return &c;
	return nullptr;
}

int Party::LivingCompanions(const Board& b) const
{
	int n = 0;
	for (auto& c : list)
		for (auto& g : b.squads)
			if (g.id == c.squad) { n += g.livingCount(); break; }
	return n;
}

bool Party::Room(const Board& b, const Group& g) const
{
	int squads = 0, members = 0;
	for (auto& c : list) {
		if (c.squad == g.id) continue;
		++squads;
		for (auto& o : b.squads)
			if (o.id == c.squad) { members += o.livingCount(); break; }
	}
	return squads + 1 <= cfg.maxSquads && members + g.livingCount() <= cfg.maxMembers;   // is_room_in_actor_squad: count + squad size <= max
}

bool Party::RoomFor(const Board& b, int n) const
{
	int members = 0;
	for (auto& c : list)
		for (auto& o : b.squads)
			if (o.id == c.squad) { members += o.livingCount(); break; }
	return int(list.size()) + 1 <= cfg.maxSquads && members + n <= cfg.maxMembers;
}

bool Party::Join(Board& b, Group& g, double now, std::vector<PartyEvent>& ev)
{
	if (g.livingCount() == 0 || Has(g.id) || !Room(b, g)) return false;
	Companion c;
	c.squad = g.id;
	for (auto& m : g.members) if (m.alive()) { c.name = m.name; break; }
	c.joinedAt = now;
	c.loot = cfg.lootDefault;
	list.push_back(c);
	Detach(b, g);
	ev.push_back({ PartyEvent::Kind::Joined, g.id, c.name, 0, 0.0 });
	return true;
}

Party::Roll& Party::RollFor(const Group& g, Rng& rng)
{
	auto it = rolls.find(g.id);
	if (it != rolls.end()) return it->second;
	const int t = std::clamp(int(g.tier), 0, 2);
	Roll r;
	r.willing = rng.chance(cfg.willing[t]);
	r.factor = rng.uniform(cfg.priceLo[t], cfg.priceHi[t]);
	return rolls.emplace(g.id, r).first->second;
}

CompOffer Party::OfferFor(const Group& g, Regard regard, bool heldByJob, Rng& rng)
{
	if (!IsHumanFaction(g.faction) || g.livingCount() == 0 || Has(g.id) || heldByJob) return CompOffer::None;
	CompOffer o = CompOffer::None;
	if (regard >= Regard::Friendly) o = CompOffer::Free;   // is_actor_friend
	else if (regard == Regard::Neutral && cfg.paid[int(g.faction)]) o = RollFor(g, rng).willing ? CompOffer::Paid : CompOffer::Unwilling;
	return o;
}

int Party::Price(const Group& g, int days, Rng& rng, float economy)
{
	const float f = RollFor(g, rng).factor;
	return std::max(1, int(std::lround(cfg.dayGold * f * float(std::max(1, days)) / std::max(0.01f, economy))));
}

void Party::Detach(Board& b, Group& g)
{
	b.AssignToSmart(g, -1);   // SIMBOARD:assign_squad_to_smart(squad, nil)
	g.assignedTarget = g.currentTarget = -1;
	g.action = Action::None;
	g.activity = Act::None; g.actStep = 0; g.actHours = 0.f; g.arrivedFresh = false;
	g.locked = false;
	g.retreatUntilHours = 0;
	b.companions.insert(g.id);
}

bool Party::Recruit(Board& b, Group& g, Regard regard, bool heldByJob, int days, double now, Rng& rng, std::vector<PartyEvent>& ev)
{
	const CompOffer o = OfferFor(g, regard, heldByJob, rng);
	if (o != CompOffer::Free && o != CompOffer::Paid) return false;
	if (o == CompOffer::Paid && days <= 0) return false;
	if (!Room(b, g)) return false;
	Companion c;
	c.squad = g.id;
	for (auto& m : g.members) if (m.alive()) { c.name = m.name; break; }
	c.paid = o == CompOffer::Paid;
	c.joinedAt = now;
	c.until = c.paid ? now + 24.0 * double(days) : 0.0;
	c.loot = cfg.lootDefault;
	PartyEvent e{ PartyEvent::Kind::Joined, g.id, c.name, c.paid ? Price(g, days, rng) : 0, c.until };
	list.push_back(c);
	Detach(b, g);
	ev.push_back(e);
	return true;
}

bool Party::Dismiss(Board& b, std::uint64_t id, PartyEvent::Kind why, std::vector<PartyEvent>& ev, const PartyPlace* a_player)
{
	auto it = std::find_if(list.begin(), list.end(), [&](const Companion& c) { return c.squad == id; });
	if (it == list.end()) return false;
	Group* g = b.Find(id);
	std::int64_t place = -1;   // dismissed in another worldspace: the place there it settles at
	if (g && a_player && a_player->ws >= 0 && a_player->ws != int(g->ws)) {
		if (!b.Blocked(g->faction, a_player->ws)) place = b.NearestValidPlace(*g, a_player->ws, a_player->x, a_player->y);
		if (place < 0 && why == PartyEvent::Kind::Left) return false;   // refused: it stays with the player
	}
	ev.push_back({ why, id, it->name, 0, it->until });
	list.erase(it);
	b.companions.erase(id);
	if (g) {   // back to the simulation: no target, so the next update picks one where it stands
		if (place >= 0) {
			g->ws = std::uint8_t(a_player->ws);
			g->node = g->prevNode = std::uint32_t(place);
			g->x = a_player->x; g->y = a_player->y;
		}
		g->assignedTarget = g->currentTarget = -1;
		g->action = Action::None;
		g->locked = false;
	}
	return true;
}

void Party::Update(Board& b, double now, std::vector<PartyEvent>& ev)
{
	for (std::size_t i = 0; i < list.size();) {
		Companion& c = list[i];
		const Group* g = b.Find(c.squad);
		if (!g || g->livingCount() == 0) {   // wiped out (members are erased once dead and off screen; the squad goes with the last)
			ev.push_back({ PartyEvent::Kind::Lost, c.squad, c.name, 0, c.until });
			b.companions.erase(c.squad);
			list.erase(list.begin() + std::ptrdiff_t(i));
			continue;
		}
		if (c.paid && now >= c.until) {     // contract_timer: diffSec > period
			Dismiss(b, c.squad, PartyEvent::Kind::Expired, ev);
			continue;
		}
		if (c.paid && !c.warned && now >= c.until - cfg.warnHours) {
			c.warned = true;
			ev.push_back({ PartyEvent::Kind::Warned, c.squad, c.name, 0, c.until });
		}
		++i;
	}
}

void Party::Rebind(Board& b)
{
	b.companions.clear();
	for (std::size_t i = 0; i < list.size();) {
		Group* g = b.Find(list[i].squad);
		if (!g) { list.erase(list.begin() + std::ptrdiff_t(i)); continue; }   // gone from the save's board: forget it
		Detach(b, *g);
		++i;
	}
}

// ---------------------------------------------------------------- co-save
namespace {
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
			if (!ok || o + sizeof(T) > n) { ok = false; return x; }
			std::memcpy(&x, d + o, sizeof(T));
			o += sizeof(T);
			return x;
		}
		std::string s()
		{
			const auto k = v<std::uint32_t>();
			if (!ok || k > 4096 || o + k > n) { ok = false; return {}; }
			std::string x(reinterpret_cast<const char*>(d + o), k);
			o += k;
			return x;
		}
	};
}

void Party::Save(std::vector<std::uint8_t>& out) const
{
	Bytes w{ out };
	w.v(std::uint32_t(list.size()));
	for (auto& c : list) {
		w.v(c.squad); w.s(c.name); w.v(c.paid); w.v(c.joinedAt); w.v(c.until); w.v(c.warned);
		w.v(c.order); w.v(c.holdFire); w.v(c.loot);
		w.v(c.cell); w.v(c.ws); w.v(c.wx); w.v(c.wy); w.v(c.wz);
	}
	w.v(std::uint32_t(rolls.size()));
	for (auto& [id, r] : rolls) { w.v(id); w.v(r.willing); w.v(r.factor); }
}

bool Party::Load(const std::uint8_t* d, std::size_t n, std::uint32_t version)
{
	Reset();
	if (version != kSaveVersion) return false;
	Reader r{ d, n };
	for (auto k = r.v<std::uint32_t>(); r.ok && k && k <= 16; --k) {
		Companion c;
		c.squad = r.v<std::uint64_t>(); c.name = r.s(); c.paid = r.v<bool>(); c.joinedAt = r.v<double>(); c.until = r.v<double>(); c.warned = r.v<bool>();
		c.order = r.v<Order>(); c.holdFire = r.v<bool>(); c.loot = r.v<bool>();
		c.cell = r.v<FormID>(); c.ws = r.v<FormID>(); c.wx = r.v<float>(); c.wy = r.v<float>(); c.wz = r.v<float>();
		if (int(c.order) > int(Order::Move)) c.order = Order::Follow;
		if (r.ok) list.push_back(c);
	}
	for (auto k = r.v<std::uint32_t>(); r.ok && k && k < 100000; --k) {
		const auto id = r.v<std::uint64_t>();
		Roll ro;
		ro.willing = r.v<bool>();
		ro.factor = r.v<float>();
		if (r.ok) rolls[id] = ro;
	}
	if (!r.ok) { Reset(); return false; }
	return true;
}

}  // namespace ALife
