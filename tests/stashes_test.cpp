// Headless tests for stashes (docs/STASHES.md, M5f): stashes.ltx, spots from the graph, the kind / tier fill, reveals and
// marker slots, notes on the dead, job rewards paid with a stash, looting, placement bookkeeping, the STSH co-save and old
// saves, Diff(Economy). Build: tests\build_stashes_test.bat ; run: stashes_test.exe [config dir]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>

#include "ALife/Config.h"
#include "ALife/Ltx.h"
#include "ALife/Stashes.h"
#include "ALife/World.h"

using namespace ALife;

static int gFails = 0, gPass = 0;
#define CHECK(cond, msg) do { if (cond) { ++gPass; } else { ++gFails; std::printf("FAIL: %s   [%s]\n", msg, #cond); } } while (0)

namespace {
	constexpr FormID kNote = 0xFE00081E;

	GraphNode Node(std::uint32_t id, const char* name, Kind k, float x, float y, int ws = 0)
	{
		GraphNode n;
		n.id = id; n.name = name; n.kind = k; n.x = x; n.y = y; n.ws = std::uint8_t(ws);
		n.city = k == Kind::City;
		return n;
	}

	Group Squad(std::uint64_t id, Faction f, int members)
	{
		static std::uint64_t mid = 1000;
		Group g;
		g.id = id; g.faction = f;
		for (int i = 0; i < members; ++i) {
			Member m;
			m.id = ++mid; m.faction = f; m.health = m.healthMax = 100.f;
			g.members.push_back(m);
		}
		return g;
	}

	int Count(const std::vector<StashEvent>& ev, StashEvent::Kind k)
	{
		int n = 0;
		for (auto& e : ev) n += e.kind == k;
		return n;
	}

	bool OnlyTier(const StashConfig& c, const StashSpot& s, int maxTier)   // every rolled item is from tiers 1..maxTier (or coins)
	{
		for (auto& it : s.items) {
			if (it.form == c.goldForm) continue;
			bool ok = false;
			for (auto& k : c.loot)
				for (int t = 0; t < maxTier; ++t)
					for (auto& li : k.tiers[std::size_t(t)]) if (li.form == it.form) ok = true;
			if (!ok) return false;
		}
		return true;
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const std::string dir = argc > 1 ? argv[1] : "config";
	Ltx sl;
	std::string err;
	if (!sl.LoadFile(dir + "/stashes.ltx", &err)) { std::printf("cannot load stashes.ltx: %s\n", err.c_str()); return 2; }

	// ---- 0. stashes.ltx
	std::printf("section 0: config\n");
	StashConfig sc;
	const bool loaded = LoadStashConfig(sl, sc, err);
	if (!loaded) std::printf("stashes.ltx: %s\n", err.c_str());
	CHECK(loaded, "stashes.ltx parses");
	CHECK(sc.enabled && sc.spotsPerPlace == 1 && sc.offsetMin == 400.f && sc.offsetMax == 1200.f && sc.maxSpots == 900, "[stashes] spots");
	CHECK(std::fabs(sc.hiddenShare - 0.5f) < 1e-4f && sc.revealRadius == 60000.f && sc.revealMin == 1500.f && sc.placeRadius == 5000.f && sc.markerSlots == 8, "[stashes] reveal, place, markers");
	CHECK(sc.kinds[int(Kind::BanditCamp)] && sc.kinds[int(Kind::Crypt)] && sc.kinds[int(Kind::Wilderness)] && !sc.kinds[int(Kind::City)] && !sc.kinds[int(Kind::Town)] &&
	      !sc.kinds[int(Kind::Inn)] && !sc.kinds[int(Kind::Farm)], "spots near camps, crypts, the wild; none in cities, towns, inns, farms");
	CHECK(std::fabs(sc.noteChance - 0.15f) < 1e-4f && std::fabs(sc.rewardChance - 0.5f) < 1e-4f && sc.replaceGold && sc.budgetPerGold == 1.f, "[notes] and [reward]");
	CHECK(sc.budgetMin == 250.f && sc.budgetMax == 500.f && std::fabs(sc.perLevel - 0.04f) < 1e-5f && sc.tierLevels[4] == 36 && sc.tierChance[1] == 0.8f && sc.tierCount[4] == 3.f, "[loot] budget and tiers");
	CHECK(sc.loot.size() == 7 && sc.goldForm == 0xF, "seven loot kinds, coins Gold001");
	{
		bool potions = false, arrows = false;
		for (auto& k : sc.loot) {
			if (k.name == "potions") potions = k.tiers[0].size() == 3 && k.tiers[0][0].form == 0x3EADD && k.tiers[0][0].value == 17 && k.tiers[4][1].form == 0x39BE6;
			if (k.name == "arrows") arrows = k.countLo == 10 && k.countHi == 24 && k.tiers[4][0].form == 0x139C0 && k.tiers[4][0].value == 8;
		}
		CHECK(potions && arrows, "loot tables: FormIDs and values as checked in Skyrim.esm");
	}
	CHECK(sc.containerDefault == 0xF8478 && sc.containers[int(Kind::Crypt)] == 0xF8477 && sc.containers[int(Kind::Fort)] == 0x9424C &&
	      sc.containers[int(Kind::BanditCamp)] == 0x10E886 && sc.containers[int(Kind::Mine)] == 0, "container bases by place kind");
	{
		Ltx bad;
		bad.LoadString("[stashes]\nkinds = fort, castle\n");
		StashConfig x;
		std::string e2;
		CHECK(!LoadStashConfig(bad, x, e2) && e2.find("castle") != std::string::npos, "an unknown place kind is an error");
		Ltx bad2;
		bad2.LoadString("[stashes]\n[loot]\nkinds = junk\n");
		CHECK(!LoadStashConfig(bad2, x, e2) && e2.find("kind.junk") != std::string::npos, "a loot kind without its section is an error");
		Ltx none;
		none.LoadString("[loot]\nbudget = 1, 2\n");
		CHECK(!LoadStashConfig(none, x, e2), "no [stashes] section is an error");
	}
	sc.noteForm = kNote;

	// ---- world: a city, camps, a crypt, a fort, a town, a mine on Tamriel; a camp on Solstheim
	World w;
	w.nodes.push_back(Node(0, "Whiterun", Kind::City, 0, 0));
	w.nodes.push_back(Node(1, "Halted Stream Camp", Kind::BanditCamp, 20000, 0));
	w.nodes.push_back(Node(2, "Bleak Falls Barrow", Kind::Crypt, 0, 30000));
	w.nodes.push_back(Node(3, "Fort Greymoor", Kind::Fort, -25000, 0));
	w.nodes.push_back(Node(4, "Riverwood", Kind::Town, 5000, 5000));
	w.nodes.push_back(Node(5, "Embershard Mine", Kind::Mine, 2000, 2000));
	w.nodes.push_back(Node(6, "Far Camp", Kind::ForswornCamp, 200000, 0));
	w.nodes.push_back(Node(7, "Reaver Camp", Kind::BanditCamp, 5000, 0, 1));

	// ---- 1. spots
	std::printf("section 1: spots\n");
	{
		Stashes st;
		st.cfg = sc;
		st.Build(w, 42);
		CHECK(st.spots.size() == 6, "one spot per listed place (no city, no town)");
		bool where = true, ids = true;
		std::set<std::uint32_t> nodes;
		for (std::size_t i = 0; i < st.spots.size(); ++i) {
			const auto& s = st.spots[i];
			const auto& n = w.nodes[s.node];
			const float d = std::sqrt((s.x - n.x) * (s.x - n.x) + (s.y - n.y) * (s.y - n.y));
			where = where && d >= 399.f && d <= 1201.f && s.ws == n.ws;
			ids = ids && s.id == i + 1;
			nodes.insert(s.node);
		}
		CHECK(where && ids && !nodes.count(0) && !nodes.count(4), "400-1200 units from its place, in its worldspace, ids 1..n");
		const StashSpot* crypt = nullptr;
		const StashSpot* fort = nullptr;
		const StashSpot* mine = nullptr;
		for (auto& s : st.spots) { if (s.node == 2) crypt = &s; if (s.node == 3) fort = &s; if (s.node == 5) mine = &s; }
		CHECK(crypt && crypt->base == 0xF8477 && fort && fort->base == 0x9424C && mine && mine->base == 0xF8478, "container base by kind, the chest by default");
		Stashes again;
		again.cfg = sc;
		again.Build(w, 42);
		bool same = again.spots.size() == st.spots.size();
		for (std::size_t i = 0; same && i < st.spots.size(); ++i)
			same = again.spots[i].x == st.spots[i].x && again.spots[i].y == st.spots[i].y && again.spots[i].state == st.spots[i].state;
		CHECK(same, "the same seed builds the same spots");
		// many places: about half start filled (hidden), none marked, nothing rolled yet
		World big;
		for (std::uint32_t i = 0; i < 1000; ++i) big.nodes.push_back(Node(i, "camp", Kind::BanditCamp, float(i) * 3000.f, 0));
		Stashes many;
		many.cfg = sc;
		many.Build(big, 7);
		int filled = 0, marked = 0, rolled = 0;
		for (auto& s : many.spots) { filled += s.Filled(); marked += s.slot >= 0; rolled += s.rolled; }
		CHECK(many.spots.size() == 900, "max_spots caps the spots");
		CHECK(filled > 380 && filled < 520 && marked == 0 && rolled == 0, "about half hidden and filled, none marked, content rolled lazily");
	}

	// ---- 2. the fill
	std::printf("section 2: content\n");
	{
		Stashes st;
		st.cfg = sc;
		StashSpot s;
		Rng rng(5);
		const int v = st.Roll(s, 400.f, 1, rng);
		CHECK(v >= 399 && v <= 400 && s.rolled && !s.items.empty(), "a 400-gold stash is worth 400 (the rest as coins)");
		CHECK(OnlyTier(st.cfg, s, 1), "level 1: tier 1 items only");
		int highTier = 0;
		for (int k = 0; k < 40; ++k) {
			StashSpot h;
			st.Roll(h, 3000.f, 40, rng);
			if (!OnlyTier(st.cfg, h, 3)) ++highTier;
		}
		CHECK(highTier > 0, "level 40: tiers 4 and 5 show up");
		int coinsOnly = 0;
		for (int k = 0; k < 40; ++k) {
			StashSpot h;
			st.Roll(h, 500.f, 10, rng);
			coinsOnly += h.items.size() == 1 && h.items[0].form == sc.goldForm;
		}
		CHECK(coinsOnly < 8, "stashes hold goods, not only coins");
		// the budget: level and Diff(Economy); at 1.0 no extra draws (the same rng state as without the multiplier)
		Rng a(11), b(11);
		const float b1 = st.Budget(20, 1.f, a);
		const float manual = b.uniform(sc.budgetMin, sc.budgetMax) * (1.f + sc.perLevel * 20.f);
		CHECK(b1 == manual && a.s == b.s, "economy 1.0: the budget and the draws are exactly as without the multiplier");
		Rng c(11), d(11);
		CHECK(std::fabs(st.Budget(20, 2.f, c) - 2.f * st.Budget(20, 1.f, d)) < 1e-3f, "economy 2: twice the value");
		Rng e(11);
		CHECK(st.Budget(0, 0.f, e) == 0.f && st.Budget(0, std::nanf(""), e) > 0.f, "economy 0: worthless; NaN is taken as 1");
		Rng f(3);
		const float lo = st.Budget(0, 1.f, f), hi = st.Budget(30, 1.f, f);
		CHECK(lo >= 250.f && lo <= 500.f && hi > lo, "the budget grows with the player's level");
	}

	// ---- 3. reveal and markers
	std::printf("section 3: reveal\n");
	{
		Stashes st;
		st.cfg = sc;
		st.Build(w, 42);
		for (auto& s : st.spots) { s.state = StashSpot::State::Empty; s.rolled = false; }
		std::vector<StashEvent> ev;
		Rng rng(9);
		const int id = st.Reveal(0.f, 0.f, 0, 10, 1.f, rng, ev, "note");
		const StashSpot* s = id > 0 ? st.Find(std::uint32_t(id)) : nullptr;
		CHECK(s && s->Filled() && s->rolled && !s->items.empty() && s->slot == 0, "a reveal fills a stash and marks it (slot 0)");
		CHECK(Count(ev, StashEvent::Kind::Marked) == 1 && ev[0].why == "note", "a Marked event says why");
		const float dist = s ? std::sqrt(s->x * s->x + s->y * s->y) : 0.f;
		CHECK(s && dist >= sc.revealMin && dist <= sc.revealRadius && s->ws == 0, "within reveal_radius, not under the player's feet, same worldspace");
		CHECK(s && w.nodes[s->node].name != std::string("Far Camp"), "not the far camp (beyond reveal_radius)");
		// Solstheim
		ev.clear();
		const int sol = st.Reveal(0.f, 0.f, 1, 10, 1.f, rng, ev, "note");
		CHECK(sol > 0 && st.Find(std::uint32_t(sol))->ws == 1, "on Solstheim, a Solstheim stash");
		// the rest: four Tamriel spots left in range (far camp only as the last resort)
		int revealed = 2;
		for (int k = 0; k < 10; ++k) if (st.Reveal(0.f, 0.f, 0, 10, 1.f, rng, ev, "note") > 0) ++revealed;
		CHECK(revealed == 6 && st.Reveal(0.f, 0.f, 0, 10, 1.f, rng, ev, "note") < 0, "every spot once; then none left");
		CHECK(st.MarkedCount() == 6, "six marked");
		// marker slots: a ninth marked stash takes the oldest marker
		Stashes many;
		many.cfg = sc;
		World line;
		for (std::uint32_t i = 0; i < 12; ++i) line.nodes.push_back(Node(i, "camp", Kind::BanditCamp, 2000.f + float(i) * 3000.f, 0));
		many.Build(line, 1);
		for (auto& sp : many.spots) { sp.state = StashSpot::State::Empty; sp.rolled = false; }
		std::vector<StashEvent> ev2;
		std::vector<int> order;
		for (int k = 0; k < 10; ++k) order.push_back(many.Reveal(0.f, 0.f, 0, 10, 1.f, rng, ev2, "note"));
		CHECK(many.MarkedCount() == 8 && Count(ev2, StashEvent::Kind::Unmarked) == 2 && many.counters.evicted == 2, "eight markers; the oldest two give theirs up");
		CHECK(many.Find(std::uint32_t(order[0]))->slot < 0 && many.Find(std::uint32_t(order[0]))->Filled() && many.Find(std::uint32_t(order[9]))->slot >= 0,
		      "the first stash lost its marker but keeps its content");
		std::set<int> slots;
		for (auto& sp : many.spots) if (sp.slot >= 0) slots.insert(sp.slot);
		CHECK(slots.size() == 8, "no slot used twice");
	}

	// ---- 4. notes on the dead
	std::printf("section 4: notes\n");
	{
		Stashes st;
		st.cfg = sc;
		st.cfg.noteChance = 1.f;
		st.Build(w, 42);
		Rng rng(4);
		Group victims = Squad(1, Faction::Bandit, 2), killers = Squad(2, Faction::Imperial, 2), wolves = Squad(3, Faction::Wildlife, 2);
		CHECK(st.OnKill(victims, victims.members[0], &killers, false, rng) && Stashes::CarriesNote(killers.members[0], kNote), "offline: the winner takes the note off the body");
		CHECK(st.OnKill(victims, victims.members[1], &killers, false, rng) && Stashes::CarriesNote(killers.members[1], kNote), "... a second note goes to the next member");
		Group v2 = Squad(4, Faction::Bandit, 1);
		CHECK(st.OnKill(v2, v2.members[0], &killers, false, rng) && st.counters.notesLost == 1, "nobody without a note left: it is lost");
		CHECK(killers.members[0].inventory.size() == 1 && killers.members[0].inventory[0].count == 1, "one note per member");
		Group v3 = Squad(5, Faction::Bandit, 1);
		st.OnKill(v3, v3.members[0], &wolves, false, rng);
		CHECK(!Stashes::CarriesNote(wolves.members[0], kNote) && st.counters.notesLost == 2, "beasts do not carry notes");
		CHECK(!st.OnKill(wolves, wolves.members[0], &killers, false, rng), "a wolf never carries one");
		Group v4 = Squad(6, Faction::Bandit, 1);
		const auto passed = st.counters.notesPassed;
		CHECK(st.OnKill(v4, v4.members[0], &killers, true, rng) && st.counters.notesPassed == passed, "on screen: the note is for the body (the plugin puts it there)");
		// a note carried on: the member who took it dies too, the note goes on even when the chance is 0
		st.cfg.noteChance = 0.f;
		Group third = Squad(7, Faction::Stormcloak, 1);
		const auto made = st.counters.notesMade;
		CHECK(st.OnKill(killers, killers.members[0], &third, false, rng) && Stashes::CarriesNote(third.members[0], kNote) && st.counters.notesMade == made,
		      "a carried note passes on (no new note made)");
		Group plain = Squad(8, Faction::Bandit, 1);
		CHECK(!st.OnKill(plain, plain.members[0], &third, false, rng), "chance 0 and nothing carried: no note");
		// the done-check: a note on a body reveals a marked stash
		for (auto& s : st.spots) { s.state = StashSpot::State::Empty; s.rolled = false; s.slot = -1; }
		std::vector<StashEvent> ev;
		const int id = st.OnNoteRead(20000.f, 0.f, 0, 12, 1.f, rng, ev);
		const StashSpot* s = id > 0 ? st.Find(std::uint32_t(id)) : nullptr;
		CHECK(s && s->Filled() && s->slot >= 0 && Count(ev, StashEvent::Kind::Marked) == 1 && st.Value(*s) > 0, "a note taken from a body reveals a marked, filled stash");
		CHECK(st.counters.notesRead == 1, "notes read counted");
		// no note form (no ALifeTalk.esp): nothing
		Stashes off;
		off.cfg = sc;
		off.cfg.noteForm = 0;
		Group v5 = Squad(9, Faction::Bandit, 1);
		CHECK(!off.OnKill(v5, v5.members[0], &killers, false, rng), "without the note record nothing is made");
	}

	// ---- 5. job rewards (the done-check: a task pays with a stash)
	std::printf("section 5: rewards\n");
	{
		Stashes st;
		st.cfg = sc;
		st.cfg.rewardChance = 1.f;
		st.Build(w, 42);
		for (auto& s : st.spots) { s.state = StashSpot::State::Empty; s.rolled = false; }
		std::vector<StashEvent> ev;
		Rng rng(21);
		std::int32_t gold = 900;
		CHECK(st.TaskReward(gold, 0.f, 0.f, 0, 10, 1.f, rng, ev) && gold == 0, "a job done pays with a stash instead of the gold");
		const StashSpot* s = nullptr;
		for (auto& sp : st.spots) if (sp.slot >= 0) s = &sp;
		CHECK(s && st.Value(*s) >= 900 && Count(ev, StashEvent::Kind::Marked) == 1 && ev.back().why == "reward", "the reward stash is marked and worth at least the gold");
		CHECK(st.counters.rewards == 1, "rewards counted");
		st.cfg.rewardChance = 0.f;
		gold = 300;
		CHECK(!st.TaskReward(gold, 0.f, 0.f, 0, 10, 1.f, rng, ev) && gold == 300, "chance 0: the gold as before");
		st.cfg.rewardChance = 1.f;
		st.cfg.replaceGold = false;
		gold = 300;
		CHECK(st.TaskReward(gold, 0.f, 0.f, 0, 10, 1.f, rng, ev) && gold == 300, "replace_gold false: the stash comes on top of the gold (Anomaly)");
		st.cfg.replaceGold = true;
		// a hidden, not yet placed stash used as a reward is topped up to the gold
		Stashes h;
		h.cfg = sc;
		h.cfg.rewardChance = 1.f;
		h.Build(w, 42);
		for (auto& sp : h.spots) { sp.state = StashSpot::State::Filled; sp.rolled = true; sp.items = { { sc.goldForm, 10 } }; }
		gold = 2000;
		CHECK(h.TaskReward(gold, 0.f, 0.f, 0, 10, 1.f, rng, ev) && gold == 0, "with only hidden stashes left, one of them pays");
		const StashSpot* hs = nullptr;
		for (auto& sp : h.spots) if (sp.slot >= 0) hs = &sp;
		CHECK(hs && h.Value(*hs) >= 2000, "... topped up to the gold it replaces");
		// placed hidden stashes cannot be topped up: no reward stash there, the gold stays
		for (auto& sp : h.spots) { sp.slot = -1; sp.ref = 0xFF000001; sp.items.clear(); }
		gold = 500;
		CHECK(!h.TaskReward(gold, 0.f, 0.f, 0, 10, 1.f, rng, ev) && gold == 500, "no usable stash: the job pays its gold");
		// economy: the gold already carries Diff(Economy); the stash's own budget follows it too
		Stashes e2;
		e2.cfg = sc;
		e2.cfg.rewardChance = 1.f;
		e2.Build(w, 42);
		for (auto& sp : e2.spots) { sp.state = StashSpot::State::Empty; sp.rolled = false; }
		gold = 1;
		Rng r2(8);
		e2.TaskReward(gold, 0.f, 0.f, 0, 10, 4.f, r2, ev);
		const StashSpot* es = nullptr;
		for (auto& sp : e2.spots) if (sp.slot >= 0) es = &sp;
		CHECK(es && e2.Value(*es) >= int(4.f * 250.f * 1.4f) - 1, "economy 4: the stash's own value x 4");
	}

	// ---- 6. looting and placement bookkeeping
	std::printf("section 6: looting\n");
	{
		Stashes st;
		st.cfg = sc;
		st.Build(w, 42);
		for (auto& s : st.spots) { s.state = StashSpot::State::Empty; s.rolled = false; }
		std::vector<StashEvent> ev;
		Rng rng(2);
		const int id = st.Reveal(20000.f, 0.f, 0, 10, 1.f, rng, ev, "note");
		StashSpot* s = st.Find(std::uint32_t(id));
		auto near = st.ToPlace(s->x + 100.f, s->y, 0);
		CHECK(near.size() == 1 && near[0] == s, "the player within place_radius: this stash is to be placed");
		CHECK(st.ToPlace(s->x + 6000.f, s->y, 0).empty() && st.ToPlace(s->x, s->y, 1).empty(), "not beyond place_radius, not from another worldspace");
		st.Placed(*s, 0xFF000123);
		CHECK(s->ref == 0xFF000123 && s->items.empty() && s->rolled && st.ToPlace(s->x, s->y, 0).empty() && st.FindByRef(0xFF000123) == s, "placed: the container holds the content now");
		ev.clear();
		st.OnLooted(s->id, ev);
		CHECK(!s->Filled() && s->slot < 0 && st.found == 1 && Count(ev, StashEvent::Kind::Unmarked) == 1 && Count(ev, StashEvent::Kind::Looted) == 1, "looted: empty, unmarked, counted");
		st.OnLooted(s->id, ev);
		CHECK(st.found == 1, "looting an empty stash again changes nothing");
		// no timed refill; a later reveal may pick the same spot again and its container gets the new content
		for (auto& o : st.spots) if (&o != s) { o.state = StashSpot::State::Filled; o.slot = 0; }
		ev.clear();
		const int again = st.Reveal(s->x + 3000.f, s->y, 0, 10, 1.f, rng, ev, "note");
		CHECK(again == int(s->id) && s->Filled() && !s->items.empty() && st.ToPlace(s->x, s->y, 0).size() == 1, "revealed again: the existing container is to be filled again");
	}

	// ---- 7. co-save STSH and old saves
	std::printf("section 7: co-save\n");
	{
		Stashes st;
		st.cfg = sc;
		st.Build(w, 42);
		std::vector<StashEvent> ev;
		Rng rng(6);
		const int id = st.Reveal(0.f, 0.f, 0, 10, 1.f, rng, ev, "note");
		st.spots[0].ref = 0xFF000777;
		st.found = 3;
		std::vector<std::uint8_t> blob;
		st.Save(blob);
		Stashes back;
		back.cfg = sc;
		CHECK(back.Load(blob.data(), blob.size(), Stashes::kSaveVersion), "STSH v1 loads");
		bool same = back.spots.size() == st.spots.size() && back.found == 3;
		for (std::size_t i = 0; same && i < st.spots.size(); ++i) {
			const auto& a = st.spots[i];
			const auto& b = back.spots[i];
			same = a.id == b.id && a.node == b.node && a.ws == b.ws && a.x == b.x && a.y == b.y && a.base == b.base && a.state == b.state && a.rolled == b.rolled &&
			       a.slot == b.slot && a.ref == b.ref && a.items.size() == b.items.size() && a.markedAt == b.markedAt;
		}
		CHECK(same, "spots, states, markers, refs, contents survive");
		CHECK(back.Find(std::uint32_t(id)) && back.Value(*back.Find(std::uint32_t(id))) == st.Value(*st.Find(std::uint32_t(id))), "the marked stash keeps its value");
		// the next marker after a load is still the newest (the clock comes back)
		std::vector<StashEvent> ev2;
		const int next = back.Reveal(0.f, 0.f, 0, 10, 1.f, rng, ev2, "note");
		CHECK(next > 0 && back.Find(std::uint32_t(next))->markedAt > back.Find(std::uint32_t(id))->markedAt, "marker ages continue after a load");
		Stashes v2;
		CHECK(!v2.Load(blob.data(), blob.size(), 2) && v2.spots.empty(), "an unknown STSH version is skipped");
		Stashes cut;
		CHECK(!cut.Load(blob.data(), blob.size() - 5, Stashes::kSaveVersion) && cut.spots.empty(), "a truncated blob leaves nothing half loaded");
		// an old save (before M5f) has no STSH: the plugin builds the spots afresh, the same way a new game does
		Stashes old;
		old.cfg = sc;
		CHECK(!old.Built(), "an old save: no stashes yet");
		old.Build(w, 42);
		Stashes fresh;
		fresh.cfg = sc;
		fresh.Build(w, 42);
		bool equal = old.spots.size() == fresh.spots.size() && old.Built();
		for (std::size_t i = 0; equal && i < old.spots.size(); ++i) equal = old.spots[i].x == fresh.spots[i].x && old.spots[i].state == fresh.spots[i].state;
		CHECK(equal, "... built on load exactly like a new game (the hidden half filled)");
		Stashes fewer;
		fewer.cfg = sc;
		fewer.cfg.markerSlots = 0;
		CHECK(fewer.Load(blob.data(), blob.size(), Stashes::kSaveVersion) && fewer.MarkedCount() == 0, "fewer marker slots in the config: markers above them are dropped");
	}

	// ---- 8. disabled
	std::printf("section 8: disabled\n");
	{
		Stashes st;
		st.cfg = sc;
		st.cfg.enabled = false;
		st.Build(w, 42);
		std::vector<StashEvent> ev;
		Rng rng(1);
		std::int32_t gold = 100;
		Group v = Squad(10, Faction::Bandit, 1), k = Squad(11, Faction::Imperial, 1);
		CHECK(st.spots.empty() && st.Reveal(0, 0, 0, 1, 1.f, rng, ev, "x") < 0 && !st.TaskReward(gold, 0, 0, 0, 1, 1.f, rng, ev) && gold == 100 &&
		      !st.OnKill(v, v.members[0], &k, false, rng), "enabled = false: no spots, notes or reward stashes");
	}

	std::printf("\nstashes_test: %d passed, %d failed\n", gPass, gFails);
	return gFails ? 1 : 0;
}
