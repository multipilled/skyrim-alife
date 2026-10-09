// Headless tests for the job system (docs/TASKS.md): offers, target choice, validity, completion, failure, expiry,
// rewards, pinning and the co-save blob. Build: tests\build_tasks_test.bat ; run: tasks_test.exe [config dir]
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "ALife/Board.h"
#include "ALife/Config.h"
#include "ALife/Ltx.h"
#include "ALife/Standing.h"
#include "ALife/Tasks.h"
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

	struct PlayerHost : TaskHost
	{
		int level{ 10 };
		std::map<FormID, int> items;
		int PlayerLevel() const override { return level; }
		int PlayerItemCount(FormID f) const override { auto it = items.find(f); return it == items.end() ? 0 : it->second; }
	};

	GraphNode Node(const char* name, Kind k, float x, float y, std::uint8_t ws = 0)
	{
		GraphNode n;
		n.name = name; n.kind = k; n.x = x; n.y = y; n.ws = ws; n.radius = 1500.f;
		return n;
	}

	std::uint64_t gMemberId = 1000;
	Group Squad(std::uint64_t id, Faction f, const World& w, std::uint32_t node, int level, int members = 3)
	{
		Group g;
		g.id = id; g.faction = f; g.ws = w.nodes[node].ws;
		g.x = w.nodes[node].x; g.y = w.nodes[node].y; g.node = node;
		g.action = Action::Stay; g.assignedTarget = g.currentTarget = node; g.stayStart = 0; g.idleHours = 1000;
		for (int i = 0; i < members; ++i) {
			Member m;
			m.id = ++gMemberId; m.faction = f; m.level = level; m.health = m.healthMax = 100.f;
			m.role = i == 0 ? Role::Leader : Role::Melee;
			m.name = std::string(FactionId(f)) + " " + (i == 0 ? "leader" : "grunt") + std::to_string(m.id);
			g.members.push_back(m);
		}
		return g;
	}

	void Only(Tasks& t, Faction f, TaskType ty)
	{
		for (auto& w : t.cfg.weight[int(f)]) w = 0.f;
		t.cfg.weight[int(f)][int(ty)] = 1.f;
	}

	void Kill(Group& g) { for (auto& m : g.members) m.health = 0.f; }

	int Count(const std::vector<TaskEvent>& ev, TaskEvent::Kind k)
	{
		int n = 0;
		for (auto& e : ev) n += e.kind == k;
		return n;
	}
}

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : "config";
	Config cfg;
	std::string err;
	if (!LoadConfig(dir, cfg, err)) { std::printf("cannot load config: %s\n", err.c_str()); return 2; }
	Ltx tl;
	if (!tl.LoadFile(dir + "/tasks.ltx", &err)) { std::printf("cannot load tasks.ltx: %s\n", err.c_str()); return 2; }
	TaskConfig tc;
	const bool loaded = LoadTaskConfig(tl, tc, err);
	if (!loaded) std::printf("tasks.ltx: %s\n", err.c_str());

	// ---- 0. tasks.ltx
	CHECK(loaded, "tasks.ltx parses");
	CHECK(tc.maxActive == 4 && std::fabs(tc.noJobChance - 0.3f) < 1e-4f, "[tasks] read");
	CHECK(tc.weight[int(Faction::Imperial)][int(TaskType::Bounty)] == 2.f, "[givers] imperial bounty weight");
	CHECK(tc.parcelSpec == "ALifeTalk.esp|805" && tc.parcel == 0, "parcel spec left for the plugin to resolve");
	{
		bool allForms = true, every = true;
		for (int f = 0; f < kHumanFactionCount; ++f) {
			float sum = 0.f;
			for (int i = 1; i < kTaskTypes; ++i) sum += tc.weight[f][i];
			every &= sum > 0.f;
			if (tc.weight[f][int(TaskType::Fetch)] > 0.f) every &= !tc.fetch[f].empty();
			for (auto& it : tc.fetch[f]) allForms &= it.form != 0 && it.lo >= 1 && it.hi >= it.lo;
		}
		CHECK(every, "every human faction gives some work; fetch givers have a fetch list");
		CHECK(allForms, "every fetch item resolves to a Skyrim.esm FormID with a sane count range");
		CHECK(tc.fetch[int(Faction::Hunter)].size() >= 4 && tc.fetch[int(Faction::Hunter)][0].name == "Wolf Pelt", "hunters ask for pelts");
	}

	// ---- world: a city, a fort, a bandit camp, a road, a town, a far fort, Solstheim
	World w;
	w.nodes.push_back(Node("Whiterun", Kind::City, 0, 0));                  // 0
	w.nodes.push_back(Node("Fort Greymoor", Kind::Fort, 30000, 0));          // 1
	w.nodes.push_back(Node("Halted Stream Camp", Kind::BanditCamp, 50000, 20000));   // 2
	w.nodes.push_back(Node("Riverwood Road", Kind::Wilderness, 20000, -30000));        // 3
	w.nodes.push_back(Node("Rorikstead", Kind::Town, 100000, 0));            // 4
	w.nodes.push_back(Node("Fort Far", Kind::Fort, 300000, 0));              // 5
	w.nodes.push_back(Node("Raven Rock", Kind::Town, 0, 0, 1));              // 6
	w.nodes.push_back(Node("Lone Hill", Kind::Wilderness, -40000, -30000));  // 7
	w.Finalize(cfg);
	TestHost host;
	Board b(cfg, w, host);
	b.Reset(100.0);
	b.squads.push_back(Squad(1, Faction::Imperial, w, 3, 10));      // the giver
	b.squads.push_back(Squad(2, Faction::Bandit, w, 2, 8));         // the only valid bounty target
	b.squads.push_back(Squad(3, Faction::Forsworn, w, 5, 8));       // out of range
	{
		Group g = Squad(4, Faction::Stormcloak, w, 0, 8);           // inside Whiterun's buffer
		g.x = 1000; g.y = 1000;
		b.squads.push_back(g);
	}
	b.squads.push_back(Squad(5, Faction::Wanderer, w, 4, 8));       // a delivery recipient (85k away)
	b.squads.push_back(Squad(6, Faction::Vampire, w, 1, 40));       // far too strong for a level-10 player
	b.squads.push_back(Squad(7, Faction::Mage, w, 7, 10));          // a fetch giver
	b.squads.push_back(Squad(8, Faction::Bandit, w, 6, 8));         // other worldspace
	PlayerHost ph;
	Standing st(&cfg);
	Rng rng(42);
	double now = 100.0;

	auto apply = [&](const std::vector<TaskEvent>& ev) {
		for (auto& e : ev) if (e.goodwill != 0.f) st.OnTask(e.task.giverFaction, e.goodwill);
		for (auto& e : ev) if (e.takeItem && e.takeCount) ph.items[e.takeItem] -= e.takeCount;
	};

	// ---- 1. no work, sticky
	{
		Tasks t;
		t.cfg = tc;
		t.cfg.noJobChance = 1.f;
		CHECK(t.OfferOf(b, w, *b.Find(1), now, ph, rng) == TaskType::None, "no-job roll: no work");
		t.cfg.noJobChance = 0.f;
		CHECK(t.OfferOf(b, w, *b.Find(1), now + 1, ph, rng) == TaskType::None, "no work is sticky for the offer's lifetime");
		CHECK(t.OfferOf(b, w, *b.Find(1), now + t.cfg.offerHours + 0.1, ph, rng) != TaskType::None, "a new roll after the offer expires");
		CHECK(t.OfferOf(b, w, *b.Find(4), now, ph, rng) != TaskType::None || true, "(stormcloak may roll anything)");
		Group wolf = Squad(99, Faction::Wildlife, w, 7, 5);
		CHECK(t.OfferOf(b, w, wolf, now, ph, rng) == TaskType::None, "monsters never give work");
	}

	// ---- 2. bounty: target choice, completion on any death, turn-in, reward, cooldown
	{
		Tasks t;
		t.cfg = tc;
		t.cfg.noJobChance = 0.f;
		Only(t, Faction::Imperial, TaskType::Bounty);
		CHECK(t.OfferOf(b, w, *b.Find(1), now, ph, rng) == TaskType::Bounty, "imperial leader offers a bounty");
		const Task* o = t.Offered(1);
		CHECK(o && o->target == 2, "bounty target: the in-range bandits (not out of range, not in a city, not too strong, not another worldspace)");
		CHECK(o && o->targetMember == b.Find(2)->members[0].id, "the target is the squad's leader");
		CHECK(o && o->gold == 150 + 15 * 8, "bounty pay = base + per level");
		CHECK(o && o->place == "Halted Stream Camp", "the journal names the place");
		CHECK(t.OfferOf(b, w, *b.Find(1), now + 1, ph, rng) == TaskType::Bounty && t.Offered(1)->target == 2, "the offer is sticky");
		std::vector<TaskEvent> ev;
		Task* a = t.Accept(b, 1, now, ph, ev);
		CHECK(a && a->slot == 0 && a->stage == TaskStage::Open && Count(ev, TaskEvent::Kind::Accepted) == 1, "accept: slot 0, open, event");
		CHECK(t.StateFor(1, ph) == kTaskWorking, "giver: working");
		CHECK(t.OfferOf(b, w, *b.Find(1), now, ph, rng) == TaskType::None, "no second job from the same squad");
		CHECK(Tasks::Objective(t.active[0], false).find("Kill bandit leader") == 0, "objective text names the target");
		ev.clear();
		t.Update(b, now + 1, ph, ev);
		CHECK(ev.empty() && t.active[0].stage == TaskStage::Open, "target alive: still open");
		CHECK(!t.TurnIn(1, now + 1, ph, ev), "cannot hand in before the kill");
		b.Find(2)->members[0].health = 0.f;   // killed by anyone (tasks_bounty.script:92-112)
		t.Update(b, now + 2, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Ready) == 1 && t.StateFor(1, ph) == kTaskReady, "the kill makes the job ready");
		ev.clear();
		CHECK(t.TurnIn(1, now + 3, ph, ev) && Count(ev, TaskEvent::Kind::Done) == 1, "hand-in completes it");
		CHECK(ev.size() == 1 && ev[0].gold == 270 && ev[0].goodwill == 50.f && ev[0].memory == 100.f, "reward: gold, +50 goodwill, squad memory");
		apply(ev);
		CHECK(st.state.goodwill[int(Faction::Imperial)] == 50.f, "goodwill reaches the giver's faction");
		CHECK(st.state.goodwill[int(Faction::Vigilant)] == 0.f, "and only that faction (Anomaly)");
		CHECK(t.active.empty() && t.StateFor(1, ph) == kTaskNone, "job gone after hand-in");
		CHECK(t.OfferOf(b, w, *b.Find(1), now + 4, ph, rng) == TaskType::None, "repeat timeout: nothing new for 6 h");
		b.Find(2)->members[0].health = 100.f;
		CHECK(t.OfferOf(b, w, *b.Find(1), now + 3 + t.cfg.repeatHours + 0.1, ph, rng) == TaskType::Bounty, "work again after the timeout");
	}

	// ---- 3. giver wiped: fails without a penalty; abandoning costs goodwill
	{
		Tasks t;
		t.cfg = tc;
		t.cfg.noJobChance = 0.f;
		Only(t, Faction::Imperial, TaskType::Bounty);
		t.OfferOf(b, w, *b.Find(1), now, ph, rng);
		std::vector<TaskEvent> ev;
		t.Accept(b, 1, now, ph, ev);
		Group saved = *b.Find(1);
		Kill(*b.Find(1));
		ev.clear();
		t.Update(b, now + 1, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Failed) == 1 && ev[0].goodwill == 0.f && ev[0].why == "giver dead", "giver wiped: failed, no penalty");
		*b.Find(1) = saved;
		t.cooldown.clear();
		t.OfferOf(b, w, *b.Find(1), now + 2, ph, rng);
		t.Accept(b, 1, now + 2, ph, ev);
		ev.clear();
		CHECK(t.Abandon(1, now + 3, ev) && ev.size() == 1 && ev[0].goodwill == -25.f && ev[0].why == "abandoned", "abandon: -25");
		const float before = st.state.goodwill[int(Faction::Imperial)];
		apply(ev);
		CHECK(st.state.goodwill[int(Faction::Imperial)] == before - 25.f, "the penalty lands");
	}

	// ---- 4. expiry (off by default, like Anomaly)
	{
		Tasks t;
		t.cfg = tc;
		t.cfg.noJobChance = 0.f;
		Only(t, Faction::Imperial, TaskType::Bounty);
		t.OfferOf(b, w, *b.Find(1), now, ph, rng);
		std::vector<TaskEvent> ev;
		t.Accept(b, 1, now, ph, ev);
		ev.clear();
		t.Update(b, now + 24.0 * 30, ph, ev);
		CHECK(ev.empty() && t.active.size() == 1, "no expiry by default");
		t.cfg.expireDays = 1.0;
		t.Update(b, now + 25.0, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Failed) == 1 && ev[0].why == "expired" && ev[0].goodwill == -25.f, "expire_days: fails with the penalty");
	}

	// ---- 5. assault: the place, pinning, completion when every holder is dead
	{
		Tasks t;
		t.cfg = tc;
		t.cfg.noJobChance = 0.f;
		Only(t, Faction::Imperial, TaskType::Assault);
		b.squads.push_back(Squad(20, Faction::Bandit, w, 2, 6));   // a second bandit squad at the camp
		CHECK(t.OfferOf(b, w, *b.Find(1), now, ph, rng) == TaskType::Assault, "assault offered");
		const Task* o = t.Offered(1);
		CHECK(o && o->node == 2 && o->pinned.size() == 2, "assault: the camp and both squads holding it");
		CHECK(o && o->gold == int(120 * 2 + 10 * (8 + 6)), "assault pay");
		std::vector<TaskEvent> ev;
		t.Accept(b, 1, now, ph, ev);
		CHECK(t.IsPinned(2) && t.IsPinned(20) && b.pinned.count(2) && b.pinned.count(20), "holders pinned on the board");
		// a pinned squad stays even when its stay is long over; a free one moves on (own small board: no fights, no respawns)
		{
			Board b2(cfg, w, host);
			b2.Reset(now);
			b2.squads.push_back(*b.Find(2));
			b2.squads.push_back(*b.Find(20));
			b2.squads.push_back(Squad(21, Faction::Thalmor, w, 7, 8));   // control: not pinned
			for (auto& g : b2.squads) g.idleHours = 0.1;
			b2.maxSquads = int(b2.squads.size());
			b2.pinned = b.pinned;
			b2.Advance(now + 1.0);
			CHECK(b2.Find(2) && b2.Find(2)->action == Action::Stay && b2.Find(2)->currentTarget == 2 && b2.Find(2)->assignedTarget == 2, "pinned squad stays at the place");
			CHECK(b2.Find(20) && b2.Find(20)->action == Action::Stay && b2.Find(20)->currentTarget == 2, "second pinned squad stays");
			CHECK(b2.Find(21) && !(b2.Find(21)->action == Action::Stay && b2.Find(21)->stayStart == 0), "an unpinned squad moves on");
		}
		ev.clear();
		Kill(*b.Find(2));
		t.Update(b, now + 4, ph, ev);
		CHECK(ev.empty(), "one squad left: still open");
		Kill(*b.Find(20));
		t.Update(b, now + 5, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Ready) == 1 && !t.IsPinned(20) && b.pinned.empty(), "place cleared: ready, pins released");
		ev.clear();
		CHECK(t.TurnIn(1, now + 6, ph, ev) && ev[0].gold == int(120 * 2 + 10 * 14), "assault paid at the giver");
		// restore the bandits for later sections
		for (auto& g : b.squads) if (g.id == 2 || g.id == 20) for (auto& m : g.members) m.health = 100.f;
	}

	// ---- 6. delivery: recipient, parcel, paid at the recipient, lost parcel
	{
		Tasks t;
		t.cfg = tc;
		t.cfg.noJobChance = 0.f;
		t.cfg.parcel = 0x0A000805;   // what the plugin resolves
		Only(t, Faction::Imperial, TaskType::Delivery);
		CHECK(t.OfferOf(b, w, *b.Find(1), now, ph, rng) == TaskType::Delivery, "delivery offered");
		const Task* o = t.Offered(1);
		// recipients: non-enemies 40-160k from the giver - the wanderers (85k) or the mages (60k), never the Thalmor-friendly bandits
		CHECK(o && (o->target == 5 || o->target == 7) && o->item == 0x0A000805 && o->count == 1, "recipient: a non-enemy squad 40-160k away; the parcel");
		const std::uint64_t to = o ? o->target : 5;
		std::vector<TaskEvent> ev;
		t.Accept(b, 1, now, ph, ev);
		ph.items[0x0A000805] = 1;   // the plugin hands the parcel over on Accepted
		CHECK(t.StateFor(to, ph) == kTaskDeliverHere && t.StateFor(1, ph) == kTaskWorking, "recipient offers the delivery topic; giver shows working");
		ev.clear();
		CHECK(!t.TurnIn(1, now + 1, ph, ev), "a delivery is not handed in at the giver");
		CHECK(!t.Deliver(to == 5 ? 7 : 5, now + 1, ph, ev) && ev.empty(), "the wrong squad cannot take the parcel");
		CHECK(t.Deliver(to, now + 1, ph, ev) && ev.size() == 1 && ev[0].kind == TaskEvent::Kind::Done && ev[0].takeItem == 0x0A000805 && ev[0].gold > 0,
		      "delivered: done, parcel taken, paid");
		apply(ev);
		CHECK(ph.items[0x0A000805] == 0, "parcel left the pack");
		// lost parcel
		t.cooldown.clear();
		t.OfferOf(b, w, *b.Find(1), now + 2, ph, rng);
		t.Accept(b, 1, now + 2, ph, ev);
		ph.items[0x0A000805] = 1;
		ev.clear();
		t.Update(b, now + 2.1, ph, ev);
		ph.items[0x0A000805] = 0;   // sold it
		t.Update(b, now + 2.2, ph, ev);
		CHECK(ev.empty(), "parcel just gone: grace period");
		ph.items[0x0A000805] = 1;
		t.Update(b, now + 2.3, ph, ev);
		ph.items[0x0A000805] = 0;
		t.Update(b, now + 2.4, ph, ev);
		t.Update(b, now + 2.8, ph, ev);
		CHECK(ev.empty(), "picking it back up resets the grace period");
		t.Update(b, now + 3.0, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Failed) == 1 && ev[0].why == "parcel lost" && ev[0].goodwill == -25.f, "parcel lost: fails with the penalty");
		// recipient wiped
		t.cooldown.clear();
		t.OfferOf(b, w, *b.Find(1), now + 4, ph, rng);
		Task* d = t.Accept(b, 1, now + 4, ph, ev);
		const std::uint64_t rid = d ? d->target : 5;
		ph.items[0x0A000805] = 1;
		Group saved = *b.Find(rid);
		Kill(*b.Find(rid));
		ev.clear();
		t.Update(b, now + 5, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Failed) == 1 && ev[0].why == "recipient dead" && ev[0].goodwill == 0.f && ev[0].takeItem == 0x0A000805,
		      "recipient dead: fails without penalty, the parcel is taken back");
		*b.Find(rid) = saved;
		ph.items.clear();
	}

	// ---- 7. fetch: item by level tier, ready / reopened, items taken on hand-in
	{
		Tasks t;
		t.cfg = tc;
		t.cfg.noJobChance = 0.f;
		Only(t, Faction::Mage, TaskType::Fetch);
		CHECK(t.OfferOf(b, w, *b.Find(7), now, ph, rng) == TaskType::Fetch, "mage offers a fetch job");
		const Task* o = t.Offered(7);
		bool tierOk = false;
		for (auto& it : t.cfg.fetch[int(Faction::Mage)]) if (o && it.form == o->item) tierOk = it.minLevel <= ph.level && o->count >= it.lo && o->count <= it.hi;
		CHECK(tierOk, "fetch item from the mage lists, within the player's tier and count range");
		CHECK(o && o->gold >= t.cfg.goldMin, "fetch pay at least the minimum");
		const FormID item = o->item;
		const int n = o->count;
		std::vector<TaskEvent> ev;
		t.Accept(b, 7, now, ph, ev);
		ev.clear();
		t.Update(b, now + 1, ph, ev);
		CHECK(ev.empty() && t.StateFor(7, ph) == kTaskWorking, "nothing gathered: open");
		ph.items[item] = n;
		t.Update(b, now + 2, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Ready) == 1 && t.StateFor(7, ph) == kTaskReady, "enough items: ready");
		ph.items[item] = n - 1;
		t.Update(b, now + 3, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Reopened) == 1 && t.StateFor(7, ph) == kTaskWorking, "sold one: back to open");
		ev.clear();
		CHECK(!t.TurnIn(7, now + 3, ph, ev), "cannot hand in one short");
		ph.items[item] = n + 2;
		CHECK(t.TurnIn(7, now + 4, ph, ev) && ev[0].takeItem == item && ev[0].takeCount == n, "hand-in takes exactly the count");
		apply(ev);
		CHECK(ph.items[item] == 2, "the rest stays with the player");
		ph.items.clear();
	}

	// ---- 8. slots: four jobs at most; slots are reused
	{
		Tasks t;
		t.cfg = tc;
		t.cfg.noJobChance = 0.f;
		for (int f = 0; f < kHumanFactionCount; ++f) for (auto& x : t.cfg.weight[f]) x = 0.f;
		for (int f = 0; f < kHumanFactionCount; ++f) t.cfg.weight[f][int(TaskType::Fetch)] = 1.f;
		const std::uint64_t ids[5] = { 31, 32, 33, 34, 35 };
		for (int i = 0; i < 5; ++i) b.squads.push_back(Squad(ids[i], Faction::Wanderer, w, 7, 5));
		std::vector<TaskEvent> ev;
		for (int i = 0; i < 4; ++i) {
			CHECK(t.OfferOf(b, w, *b.Find(ids[i]), now, ph, rng) == TaskType::Fetch, "offer while slots are free");
			Task* a = t.Accept(b, ids[i], now, ph, ev);
			CHECK(a && a->slot == i, "slots fill in order");
		}
		CHECK(t.OfferOf(b, w, *b.Find(ids[4]), now, ph, rng) == TaskType::None, "no offers with four jobs open");
		t.Abandon(ids[1], now, ev);
		CHECK(t.OfferOf(b, w, *b.Find(ids[4]), now, ph, rng) == TaskType::Fetch, "a slot freed: offers again");
		Task* a = t.Accept(b, ids[4], now, ph, ev);
		CHECK(a && a->slot == 1, "the freed slot is reused");

		// ---- 9. co-save round trip
		std::vector<std::uint8_t> blob;
		t.Save(blob);
		Tasks u;
		u.cfg = tc;
		std::size_t used = 0;
		CHECK(u.Load(blob.data(), blob.size(), Tasks::kSaveVersion, &used) && used == blob.size(), "co-save loads, every byte read");
		bool same = u.active.size() == t.active.size() && u.offers.size() == t.offers.size() && u.cooldown.size() == t.cooldown.size() && u.nextId == t.nextId;
		for (std::size_t i = 0; same && i < t.active.size(); ++i) {
			const Task &x = t.active[i], &y = u.active[i];
			same = x.id == y.id && x.type == y.type && x.slot == y.slot && x.giver == y.giver && x.item == y.item && x.count == y.count &&
			       x.gold == y.gold && x.giverName == y.giverName && x.itemName == y.itemName && x.acceptedAt == y.acceptedAt;
		}
		CHECK(same, "co-save: jobs, offers, cooldowns identical");
		CHECK(!u.Load(blob.data(), blob.size(), Tasks::kSaveVersion + 1) && u.active.empty(), "unknown version: skipped, no jobs");
		CHECK(!u.Load(blob.data(), blob.size() / 2, Tasks::kSaveVersion) && u.active.empty(), "truncated blob: rejected, no jobs");
	}

	std::printf("tasks: %d passed, %d failed\n", gPass, gFails);
	return gFails ? 1 : 0;
}
