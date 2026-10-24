// Headless tests for companions and recruiting (docs/COMPANIONS.md): offers, prices, room, joining (out of the simulation),
// contracts, losses, dismissal, orders and the co-save blob. Build: tests\build_party_test.bat ; run: party_test.exe [config dir]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "ALife/Board.h"
#include "ALife/Config.h"
#include "ALife/Ltx.h"
#include "ALife/Party.h"
#include "ALife/Standing.h"
#include "ALife/World.h"

using namespace ALife;

static int gFails = 0, gPass = 0;
#define CHECK(cond, msg) do { if (cond) { ++gPass; } else { ++gFails; std::printf("FAIL: %s   [%s]\n", msg, #cond); } } while (0)

namespace {
	struct TestHost : Host
	{
		Member MakeMember(Faction f, Tier, Role role, int level, bool, Rng&, std::uint64_t id) override
		{
			Member m;
			m.id = id; m.faction = f; m.role = role; m.level = level;
			m.name = std::string(FactionId(f)) + "#" + std::to_string(id);
			m.health = m.healthMax = 100.f;
			return m;
		}
		float MemberPower(const Member& m) const override { return 10.f + 6.f * float(m.level); }
	};

	GraphNode Node(const char* name, Kind k, float x, float y)
	{
		GraphNode n;
		n.name = name; n.kind = k; n.x = x; n.y = y; n.radius = 1500.f;
		return n;
	}

	std::uint64_t gMemberId = 1000;
	Group Squad(std::uint64_t id, Faction f, Tier t, const World& w, std::uint32_t node, int members)
	{
		Group g;
		g.id = id; g.faction = f; g.tier = t; g.ws = 0;
		g.x = w.nodes[node].x; g.y = w.nodes[node].y; g.node = node;
		g.action = Action::Stay; g.assignedTarget = g.currentTarget = node; g.stayStart = 0; g.idleHours = 1000;
		for (int i = 0; i < members; ++i) {
			Member m;
			m.id = ++gMemberId; m.faction = f; m.level = 10; m.health = m.healthMax = 100.f;
			m.role = i == 0 ? Role::Leader : Role::Melee;
			m.name = std::string(FactionId(f)) + (i == 0 ? " leader" : " grunt");
			g.members.push_back(m);
		}
		return g;
	}

	int Count(const std::vector<PartyEvent>& ev, PartyEvent::Kind k)
	{
		int n = 0;
		for (auto& e : ev) n += e.kind == k;
		return n;
	}
	float Health(const Group& g)
	{
		float h = 0.f;
		for (auto& m : g.members) h += m.health;
		return h;
	}
}

int main(int argc, char** argv)
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	const std::string dir = argc > 1 ? argv[1] : "config";
	Config cfg;
	std::string err;
	if (!LoadConfig(dir, cfg, err)) { std::printf("cannot load config: %s\n", err.c_str()); return 2; }
	Ltx pl;
	if (!pl.LoadFile(dir + "/companions.ltx", &err)) { std::printf("cannot load companions.ltx: %s\n", err.c_str()); return 2; }

	// ---- 0. companions.ltx
	std::printf("section 0\n");
	Party p;
	const bool loaded = LoadPartyConfig(pl, p.cfg, err);
	if (!loaded) std::printf("companions.ltx: %s\n", err.c_str());
	CHECK(loaded, "companions.ltx parses");
	CHECK(p.cfg.maxMembers == 6 && p.cfg.maxSquads == 2, "[companions] limits");
	CHECK(p.cfg.paid[int(Faction::Wanderer)] && p.cfg.paid[int(Faction::Mercenary)] && p.cfg.paid[int(Faction::Bandit)] && p.cfg.paid[int(Faction::Hunter)],
		  "the freelance factions hire out");
	CHECK(!p.cfg.paid[int(Faction::Imperial)] && !p.cfg.paid[int(Faction::Thalmor)], "soldiers do not hire out");
	CHECK(p.cfg.paid[int(Faction::Reaver)] && !p.cfg.paid[int(Faction::Skaal)] && !p.cfg.paid[int(Faction::Redoran)], "Reavers hire out; the Skaal and the Redoran guard do not");
	CHECK(p.cfg.willing[0] == 0.f && std::fabs(p.cfg.willing[1] - 0.4f) < 1e-4f && std::fabs(p.cfg.willing[2] - 0.7f) < 1e-4f, "willing chances by tier");
	CHECK(p.cfg.priceLo[2] == 1.8f && p.cfg.priceHi[2] == 2.4f && p.cfg.dayGold == 250.f && p.cfg.days[1] == 2, "prices and contracts");
	{
		Ltx bad;
		bad.LoadString("[companions]\npaid_factions = wanderer, nobody\n");
		PartyConfig pc;
		std::string e2;
		CHECK(!LoadPartyConfig(bad, pc, e2) && e2.find("nobody") != std::string::npos, "an unknown paid faction is an error");
	}

	// ---- world: a road, a fort, a camp (all close together)
	cfg.minSquadsForOcs = 0;          // offline combat even in this tiny world
	cfg.ocsMode = OcsMode::Full;
	World w;
	w.nodes.push_back(Node("Riverwood Road", Kind::Wilderness, 0, 0));      // 0
	w.nodes.push_back(Node("Fort Greymoor", Kind::Fort, 20000, 0));         // 1
	w.nodes.push_back(Node("Halted Stream Camp", Kind::BanditCamp, 40000, 0));   // 2
	w.nodes.push_back(Node("Raven Rock Road", Kind::Wilderness, 0, 0));       // 3: Solstheim (ws 1), same coordinates as node 0
	w.nodes.push_back(Node("Solstheim Wilds", Kind::Wilderness, 30000, 0));   // 4: Solstheim
	w.nodes[3].ws = w.nodes[4].ws = 1;
	w.Finalize(cfg);
	TestHost host;
	Board b(cfg, w, host);
	b.Reset(100.0);
	b.squads.push_back(Squad(1, Faction::Imperial, Tier::Advanced, w, 0, 4));   // friendly soldiers
	b.squads.push_back(Squad(2, Faction::Wanderer, Tier::Veteran, w, 1, 1));    // a neutral veteran wanderer
	b.squads.push_back(Squad(3, Faction::Wanderer, Tier::Novice, w, 1, 2));     // a neutral novice: never works for pay
	b.squads.push_back(Squad(4, Faction::Hunter, Tier::Advanced, w, 2, 3));     // one more friendly band
	b.squads.push_back(Squad(5, Faction::Bandit, Tier::Advanced, w, 2, 2));
	for (auto& g : b.squads) b.AssignToSmart(g, g.assignedTarget);
	Rng rng(7);

	// ---- 1. offers
	std::printf("section 1\n");
	CHECK(p.OfferFor(*b.Find(1), Regard::Friendly, false, rng) == CompOffer::Free, "a friendly squad joins for free");
	CHECK(p.OfferFor(*b.Find(1), Regard::Allied, false, rng) == CompOffer::Free, "an allied squad joins for free");
	CHECK(p.OfferFor(*b.Find(1), Regard::Neutral, false, rng) == CompOffer::None, "neutral soldiers do not hire out");
	CHECK(p.OfferFor(*b.Find(1), Regard::Unfriendly, false, rng) == CompOffer::None, "an unfriendly squad never joins");
	CHECK(p.OfferFor(*b.Find(1), Regard::Friendly, true, rng) == CompOffer::None, "a squad held by a job does not join");
	CHECK(p.OfferFor(*b.Find(3), Regard::Neutral, false, rng) == CompOffer::Unwilling, "a novice never works for pay");
	CHECK(p.OfferFor(*b.Find(3), Regard::Neutral, false, rng) == CompOffer::Unwilling, "... and the roll is remembered");
	p.rolls[2] = { true, 2.0f };   // force the veteran's roll
	CHECK(p.OfferFor(*b.Find(2), Regard::Neutral, false, rng) == CompOffer::Paid, "a willing neutral veteran works for pay");
	CHECK(p.Price(*b.Find(2), 1, rng) == 500 && p.Price(*b.Find(2), 2, rng) == 1000, "price = day_gold x factor x days");
	{
		int willing = 0;
		for (std::uint64_t id = 100; id < 1100; ++id) {
			Group g = Squad(id, Faction::Mercenary, Tier::Veteran, w, 0, 1);
			willing += p.OfferFor(g, Regard::Neutral, false, rng) == CompOffer::Paid;
			const float f = p.rolls[id].factor;
			if (f < 1.8f || f > 2.4f) { willing = -100000; break; }
		}
		CHECK(willing > 620 && willing < 780, "veterans work for pay about 70% of the time, factor 1.8-2.4");
		for (std::uint64_t id = 100; id < 1100; ++id) p.rolls.erase(id);
	}

	// ---- 2. joining: out of the simulation
	std::printf("section 2\n");
	std::vector<PartyEvent> ev;
	CHECK(p.Recruit(b, *b.Find(1), Regard::Friendly, false, 0, b.now, rng, ev), "the friendly squad joins");
	CHECK(Count(ev, PartyEvent::Kind::Joined) == 1 && ev[0].gold == 0 && ev[0].name == "imperial leader", "joined for free, named after its leader");
	{
		const Group& g = *b.Find(1);
		CHECK(b.companions.count(1) && g.smartAssigned == -1 && g.assignedTarget == -1 && g.action == Action::None, "it left its place and the board");
		CHECK(b.smart[0].population == 0, "it no longer counts toward the place's population");
	}
	CHECK(p.OfferFor(*b.Find(1), Regard::Friendly, false, rng) == CompOffer::None, "a companion is not asked again");
	ev.clear();
	CHECK(!p.Recruit(b, *b.Find(2), Regard::Neutral, false, 0, b.now, rng, ev), "a paid squad needs a contract length");
	CHECK(!p.Room(b, *b.Find(4)), "no room: 4 companions + a squad of 3 > 6");
	CHECK(!p.Recruit(b, *b.Find(4), Regard::Friendly, false, 0, b.now, rng, ev), "... so it does not join");
	CHECK(p.Recruit(b, *b.Find(2), Regard::Neutral, false, 1, b.now, rng, ev), "the willing veteran is hired for a day");
	CHECK(Count(ev, PartyEvent::Kind::Joined) == 1 && ev.back().gold == 500 && std::fabs(ev.back().until - (b.now + 24.0)) < 1e-6, "for 500 gold, until a day from now");
	CHECK(p.LivingCompanions(b) == 5 && p.list.size() == 2, "5 companions in 2 squads");
	{
		Group solo = Squad(9, Faction::Hunter, Tier::Advanced, w, 2, 1);
		b.squads.push_back(solo);
		CHECK(!p.Room(b, *b.Find(9)), "max 2 squads even when members would fit");
		b.squads.pop_back();
	}

	// ---- 3. companions stay out of the simulation: no targets, no offline combat
	std::printf("section 3\n");
	{
		Group foe = Squad(6, Faction::Stormcloak, Tier::Veteran, w, 0, 4);   // at war with the Legion, right where the companions stand
		b.squads.push_back(foe);
		CHECK(cfg.IsEnemy(Faction::Imperial, Faction::Stormcloak), "Legion and Stormcloaks are at war");
		const float before = Health(*b.Find(1));
		for (int i = 0; i < 60; ++i) b.Step(0.1);
		CHECK(Health(*b.Find(1)) == before && b.Find(6)->opponent != 1, "no offline battle with a companion squad");
		CHECK(b.Find(1)->assignedTarget == -1 && b.Find(1)->action == Action::None, "the board picks no target for it");
		b.squads.erase(std::remove_if(b.squads.begin(), b.squads.end(), [](const Group& g) { return g.id == 6; }), b.squads.end());
	}

	// ---- 4. orders and the co-save
	std::printf("section 4\n");
	p.Find(1)->order = Order::Wait;
	p.Find(1)->holdFire = true;
	p.Find(1)->loot = true;
	p.Find(1)->cell = 0x1234; p.Find(1)->ws = 0x3C; p.Find(1)->wx = 10.f; p.Find(1)->wy = -20.f; p.Find(1)->wz = 30.f;
	std::vector<std::uint8_t> blob;
	p.Save(blob);
	{
		Party q;
		q.cfg = p.cfg;
		CHECK(q.Load(blob.data(), blob.size(), Party::kSaveVersion), "the co-save blob loads");
		const Companion* c = q.Find(1);
		const Companion* d = q.Find(2);
		CHECK(c && c->order == Order::Wait && c->holdFire && c->loot && c->cell == 0x1234 && c->ws == 0x3C && c->wx == 10.f && c->wz == 30.f && !c->paid,
			  "orders and wait point survive");
		CHECK(d && d->paid && std::fabs(d->until - (b.now + 24.0) + 6.0) < 1e-6 && d->name == "wanderer leader", "the contract survives");
		CHECK(q.rolls.count(2) && q.rolls[2].willing && q.rolls[2].factor == 2.0f && q.rolls.count(3) && !q.rolls[3].willing, "the rolls survive");
		Board b2(cfg, w, host);
		b2.Reset(b.now);
		b2.squads = b.squads;
		for (auto& g : b2.squads) if (g.id == 1 || g.id == 2) { g.assignedTarget = 0; g.action = Action::Stay; }   // as a stale load might have them
		q.Rebind(b2);
		CHECK(b2.companions.count(1) && b2.companions.count(2) && b2.Find(1)->assignedTarget == -1, "Rebind takes them out of the board again");
		Party r;
		CHECK(!r.Load(blob.data(), blob.size(), 99) && r.list.empty(), "an unknown version is skipped");
		CHECK(!r.Load(blob.data(), blob.size() / 2, Party::kSaveVersion) && r.list.empty() && r.rolls.empty(), "a truncated blob loads nothing");
		Party s;
		CHECK(s.list.empty(), "a save without the record has no companions");
	}

	// ---- 5. contracts
	std::printf("section 5\n");
	ev.clear();
	p.Update(b, p.Find(2)->until - 2.0, ev);
	CHECK(ev.empty(), "nothing two hours before the end");
	p.Update(b, p.Find(2)->until - 0.5, ev);
	p.Update(b, p.Find(2)->until - 0.4, ev);
	CHECK(Count(ev, PartyEvent::Kind::Warned) == 1, "one notice within the last hour");
	ev.clear();
	const double end = p.Find(2)->until;
	p.Update(b, end + 0.01, ev);
	CHECK(Count(ev, PartyEvent::Kind::Expired) == 1 && !p.Has(2) && !b.companions.count(2), "the contract runs out: the squad leaves");
	b.Step(0.1);
	CHECK(b.Find(2)->assignedTarget >= 0 || b.Find(2)->locked, "back on the board: it picks its next target");

	// ---- 6. dismissal and losses
	std::printf("section 6\n");
	ev.clear();
	CHECK(b.Find(2) && p.Recruit(b, *b.Find(2), Regard::Friendly, false, 0, b.now, rng, ev), "the wanderer joins again, now as a friend (4 + 1 <= 6)");
	CHECK(p.Dismiss(b, 2, PartyEvent::Kind::Left, ev) && !p.Has(2) && !b.companions.count(2) && Count(ev, PartyEvent::Kind::Left) == 1, "dismissed");
	CHECK(!p.Dismiss(b, 2, PartyEvent::Kind::Left, ev), "dismissing twice does nothing");
	{   // dismissed in another worldspace (docs/COMPANIONS.md): it settles at the nearest place there it could hold, or refuses
		const PartyPlace sol{ 1, 25000.f, 0.f };   // the player on Solstheim, nearer Tamriel's fort (20000) than to anything but node 4
		auto recruit = [&] { ev.clear(); return b.Find(2) && p.Recruit(b, *b.Find(2), Regard::Friendly, false, 0, b.now, rng, ev); };
		auto same = [&](const Group& a, std::uint8_t ws, std::uint32_t node, float x) { return a.ws == ws && a.node == node && a.x == x; };
		CHECK(b.NearestValidPlace(*b.Find(2), 1, 25000.f, 0.f) == 4 && b.NearestValidPlace(*b.Find(2), 1, 1000.f, 0.f) == 3,
		      "NearestValidPlace looks only at the asked worldspace");
		CHECK(b.NearestValidPlace(*b.Find(2), 0, 25000.f, 0.f) >= 0 && b.NearestValidPlace(*b.Find(2), 7, 0.f, 0.f) == -1, "... and finds none in an empty one");

		// refused: the faction is blocked on Solstheim
		CHECK(recruit(), "the wanderer joins again");
		const Group before = *b.Find(2);
		b.SetGated(Faction::Wanderer, true);
		CHECK(!p.Dismiss(b, 2, PartyEvent::Kind::Left, ev, &sol) && p.Has(2) && b.companions.count(2) && Count(ev, PartyEvent::Kind::Left) == 0 &&
		      same(*b.Find(2), before.ws, before.node, before.x), "blocked faction on Solstheim: the dismissal is refused, the squad unchanged");
		b.SetGated(Faction::Wanderer, false);
		// refused: no place on Solstheim it could hold
		w.nodes[3].disabled = w.nodes[4].disabled = true;
		CHECK(!p.Dismiss(b, 2, PartyEvent::Kind::Left, ev, &sol) && p.Has(2) && same(*b.Find(2), before.ws, before.node, before.x),
		      "no valid place on Solstheim: refused, the squad unchanged");
		// turned on the player there with no place: it leaves anyway, back to its own worldspace
		CHECK(p.Dismiss(b, 2, PartyEvent::Kind::Turned, ev, &sol) && !p.Has(2) && b.Find(2)->ws == 0 && b.Find(2)->node == before.node,
		      "turned with no place: it goes, keeping its own worldspace");
		w.nodes[3].disabled = w.nodes[4].disabled = false;
		// adopted: the squad becomes a Solstheim squad at the nearest valid place
		CHECK(recruit(), "the wanderer joins once more");
		CHECK(p.Dismiss(b, 2, PartyEvent::Kind::Left, ev, &sol) && !p.Has(2) && !b.companions.count(2) && Count(ev, PartyEvent::Kind::Left) == 1,
		      "dismissed on Solstheim");
		const Group& g2 = *b.Find(2);
		CHECK(g2.ws == 1 && g2.node == 4 && g2.prevNode == 4 && g2.x == 25000.f && g2.assignedTarget == -1 && g2.currentTarget == -1,
		      "it adopts Solstheim and the nearest valid place there (node 4, not Tamriel's nearer fort)");
		b.Step(0.1);
		CHECK(b.Find(2)->ws == 1 && (b.Find(2)->assignedTarget < 0 || w.nodes[std::size_t(b.Find(2)->assignedTarget)].ws == 1),
		      "back on the board it stays on Solstheim and targets only Solstheim places");
		// same worldspace: as before (no move)
		CHECK(recruit(), "joins again (on Solstheim now)");
		const Group s2 = *b.Find(2);
		CHECK(p.Dismiss(b, 2, PartyEvent::Kind::Left, ev, &sol) && same(*b.Find(2), s2.ws, s2.node, s2.x), "dismissed in its own worldspace: no move");
	}
	for (auto& m : b.Find(1)->members) m.health = 0.f;
	ev.clear();
	p.Update(b, b.now, ev);
	CHECK(Count(ev, PartyEvent::Kind::Lost) == 1 && ev.back().name == "imperial leader" && p.list.empty() && b.companions.empty(), "a wiped companion squad is lost");

	std::printf("party_test: %d passed, %d failed\n", gPass, gFails);
	return gFails ? 1 : 0;
}
