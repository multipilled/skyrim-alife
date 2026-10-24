// Headless tests for the job system (docs/TASKS.md): offers, target choice, validity, completion, failure, expiry,
// rewards, pinning and the co-save blob. Build: tests\build_tasks_test.bat ; run: tasks_test.exe [config dir]
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "ALife/Board.h"
#include "ALife/Config.h"
#include "ALife/Ltx.h"
#include "ALife/SaveFormat.h"
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

	// ---- 1b. economy difficulty channel (docs/DIFFICULTY.md): job gold x Diff(Economy), never under gold_min
	{
		int last = -1;
		bool rising = true, exact = true;
		for (int game : { 0, 2, 5 })
			for (float slider : { 0.5f, 1.f, 2.f }) {
				DifficultyChoice c;
				c.slider[int(DiffChannel::Economy)] = slider;
				cfg.ApplyDifficulty(game, c);
				Tasks t;
				t.cfg = tc;
				t.cfg.noJobChance = 0.f;
				Only(t, Faction::Imperial, TaskType::Bounty);
				t.scale = TaskScaleFrom(cfg);   // the host sets the scale from the difficulty state (Jobs.cpp); OfferOf reads only Eff()
				t.OfferOf(b, w, *b.Find(1), now, ph, rng);
				const Task* o = t.Offered(1);
				const int want = std::max(tc.goldMin, int(float(150 + 15 * 8) * tc.goldMult * cfg.Diff(DiffChannel::Economy)));
				if (!o || o->gold != want) exact = false;
				if (game == 2 && o) { rising = rising && o->gold >= last; last = o->gold; }
			}
		cfg.ApplyDifficulty(kAdept, DifficultyChoice{});
		CHECK(exact, "economy: bounty gold = pay x gold_mult x Diff(Economy) at Novice / Adept / Legendary x slider 0.5 / 1 / 2");
		CHECK(rising, "economy: bounty gold rises with the slider");
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
		CHECK(!t.TurnIn(b, 1, now + 1, ph, ev), "cannot hand in before the kill");
		b.Find(2)->members[0].health = 0.f;   // killed by anyone (tasks_bounty.script:92-112)
		t.Update(b, now + 2, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Ready) == 1 && t.StateFor(1, ph) == kTaskReady, "the kill makes the job ready");
		ev.clear();
		CHECK(t.TurnIn(b, 1, now + 3, ph, ev) && Count(ev, TaskEvent::Kind::Done) == 1, "hand-in completes it");
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
		CHECK(t.Abandon(b, 1, now + 3, ev) && ev.size() == 1 && ev[0].goodwill == -25.f && ev[0].why == "abandoned", "abandon: -25");
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
		CHECK(t.TurnIn(b, 1, now + 6, ph, ev) && ev[0].gold == int(120 * 2 + 10 * 14), "assault paid at the giver");
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
		CHECK(!t.TurnIn(b, 1, now + 1, ph, ev), "a delivery is not handed in at the giver");
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
		CHECK(!t.TurnIn(b, 7, now + 3, ph, ev), "cannot hand in one short");
		ph.items[item] = n + 2;
		CHECK(t.TurnIn(b, 7, now + 4, ph, ev) && ev[0].takeItem == item && ev[0].takeCount == n, "hand-in takes exactly the count");
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
		t.Abandon(b, ids[1], now, ev);
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

	// ================================================================== M5e (docs/TASKS.md section 8)
	CHECK(tc.weight[int(Faction::Caravan)][int(TaskType::Escort)] > 0.f && tc.weight[int(Faction::Imperial)][int(TaskType::Hunt)] > 0.f &&
	      tc.weight[int(Faction::Dawnguard)][int(TaskType::Defend)] > 0.f && tc.weight[int(Faction::Mage)][int(TaskType::Recover)] > 0.f &&
	      tc.weight[int(Faction::Imperial)][int(TaskType::Rescue)] > 0.f, "[givers] hand out the five new types (caravans escort)");
	CHECK(tc.satchelSpec == "ALifeTalk.esp|80D" && tc.satchel == 0 && tc.defendWaves == 3 && tc.huntMin == 3 && tc.escortArrive == 3000.f,
	      "M5e keys read from tasks.ltx");
	CHECK(TaskTypeFromId("escort") == TaskType::Escort && TaskTypeFromId("hunt") == TaskType::Hunt && std::string(TaskTypeId(TaskType::Recover)) == "recover",
	      "type ids");

	// a world every faction may go anywhere in (target rules are not what these tests are about)
	World wE;
	wE.nodes.push_back(Node("Whiterun", Kind::City, 0, 0));                        // 0
	wE.nodes.push_back(Node("Fort Greymoor", Kind::Fort, 30000, 0));               // 1
	wE.nodes.push_back(Node("Halted Stream Camp", Kind::BanditCamp, 50000, 20000));// 2
	wE.nodes.push_back(Node("Riverwood Road", Kind::Wilderness, 20000, -30000));   // 3
	wE.nodes.push_back(Node("Rorikstead", Kind::Town, 100000, 0));                 // 4
	wE.nodes.push_back(Node("Lone Hill", Kind::Wilderness, -40000, -30000));       // 5
	wE.nodes.push_back(Node("Raven Rock", Kind::Town, 0, 0, 1));                   // 6
	wE.nodes.push_back(Node("Bleak Falls", Kind::Dungeon, 60000, -60000));         // 7
	wE.Finalize(cfg);
	for (auto& n : wE.nodes) {
		n.props = Props{};
		n.disabled = false; n.simAvail = true; n.maxPopulation = 4;
		n.props.all = 1.f; n.props.monster = 1.f; n.props.territory = 1.f; n.props.base = 1.f;
	}
	auto squadE = [&](std::uint64_t id, Faction f, std::uint32_t node, int level, const char* section, int members = 3) {
		Group g = Squad(id, f, wE, node, level, members);
		g.section = section;
		return g;
	};
	auto freshTasks = [&](Faction f, TaskType ty, Party* p) {
		Tasks t;
		t.cfg = tc;
		t.cfg.noJobChance = 0.f;
		t.cfg.satchel = 0x0A00080D;   // what the plugin resolves
		t.party = p;
		Only(t, f, ty);
		return t;
	};
	auto has = [](const std::vector<PartyEvent>& pe, PartyEvent::Kind k, std::uint64_t squad) {
		for (auto& e : pe) if (e.kind == k && e.squad == squad) return true;
		return false;
	};
	auto dist = [](float ax, float ay, float bx, float by) { return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by)); };

	// ---- 10. escort: a caravan travels with the player to a place, paid on arrival, half when late; dismissed / dead fail it
	{
		Board bx(cfg, wE, host);
		bx.Reset(now);
		bx.squads.push_back(squadE(101, Faction::Caravan, 3, 8, "caravan_novice"));
		Party p;
		Tasks t = freshTasks(Faction::Caravan, TaskType::Escort, &p);
		{
			Tasks none = freshTasks(Faction::Caravan, TaskType::Escort, nullptr);
			CHECK(none.OfferOf(bx, wE, *bx.Find(101), now, ph, rng) == TaskType::None, "no party: no escort");
			Party full;
			full.cfg.maxSquads = 0;
			Tasks nf = freshTasks(Faction::Caravan, TaskType::Escort, &full);
			CHECK(nf.OfferOf(bx, wE, *bx.Find(101), now, ph, rng) == TaskType::None, "no room in the party: no escort");
		}
		CHECK(t.OfferOf(bx, wE, *bx.Find(101), now, ph, rng) == TaskType::Escort, "caravan offers an escort");
		const Task* o = t.Offered(101);
		const GraphNode* d = o ? wE.Node(o->dest) : nullptr;
		const float dd = d ? dist(wE.nodes[3].x, wE.nodes[3].y, d->x, d->y) : 0.f;
		CHECK(d && d->ws == 0 && o->dest != 3 && dd >= tc.escortMin && dd <= tc.escortMax && o->place == d->name, "destination: a place in its worldspace 40-160k away");
		CHECK(o && o->gold == std::max(tc.goldMin, int(tc.escortBase + tc.escortPerKm * dd / 70000.f)), "escort pay = base + per km");
		const std::uint32_t dest = o ? o->dest : 0;
		std::vector<TaskEvent> ev;
		Task* a = t.Accept(bx, 101, now, ph, ev);
		CHECK(a && p.Has(101) && bx.companions.count(101) && has(t.partyEvents, PartyEvent::Kind::Joined, 101) && t.partyEvents[0].gold == 0,
		      "accept: the squad joins the party for free");
		CHECK(a && a->deadline > now && Tasks::Objective(*a, false).find("Escort ") == 0, "deadline set; objective");
		const double deadline = a ? a->deadline : now;
		const int pay = a ? a->gold : 0;
		ev.clear();
		t.Update(bx, now + 1, ph, ev);
		CHECK(ev.empty() && t.active[0].stage == TaskStage::Open, "not there yet: open");
		CHECK(!t.TurnIn(bx, 101, now + 1, ph, ev), "no pay before arrival");
		bx.Find(101)->x = wE.nodes[dest].x + 500.f; bx.Find(101)->y = wE.nodes[dest].y;   // the plugin keeps the companions' board position
		t.Update(bx, now + 2, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Ready) == 1 && t.active[0].step == 0 && t.active[0].gold == pay && t.StateFor(101, ph) == kTaskReady,
		      "arrived in time: ready, full pay");
		ev.clear();
		t.partyEvents.clear();
		CHECK(t.TurnIn(bx, 101, now + 3, ph, ev) && ev.size() == 1 && ev[0].gold == pay && ev[0].goodwill == 50.f, "paid where it stands");
		const Group* g = bx.Find(101);
		CHECK(!p.Has(101) && !bx.companions.count(101) && has(t.partyEvents, PartyEvent::Kind::Left, 101), "the squad leaves the party");
		CHECK(g && g->action == Action::Stay && g->currentTarget == std::int64_t(dest) && g->assignedTarget == std::int64_t(dest), "and stays at the destination");
		// late: half the pay
		t.cooldown.clear();
		bx.Find(101)->x = wE.nodes[3].x; bx.Find(101)->y = wE.nodes[3].y;
		bx.Find(101)->currentTarget = bx.Find(101)->assignedTarget = 3;
		CHECK(t.OfferOf(bx, wE, *bx.Find(101), now + 10, ph, rng) == TaskType::Escort, "escort again");
		a = t.Accept(bx, 101, now + 10, ph, ev);
		const std::uint32_t dest2 = a ? a->dest : 0;
		const int pay2 = a ? a->gold : 0;
		const double late = a ? a->deadline + 1.0 : now;
		bx.Find(101)->x = wE.nodes[dest2].x; bx.Find(101)->y = wE.nodes[dest2].y;
		ev.clear();
		t.Update(bx, late, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Ready) == 1 && t.active[0].step == 1 && t.active[0].gold == pay2 / 2, "arrived late: half the pay (tasks_guide.script)");
		ev.clear();
		CHECK(t.TurnIn(bx, 101, late, ph, ev) && ev[0].gold == pay2 / 2, "late pay handed over");
		(void)deadline;
		// dismissed by the player: abandoned
		t.cooldown.clear();
		bx.Find(101)->x = wE.nodes[3].x; bx.Find(101)->y = wE.nodes[3].y;
		bx.Find(101)->currentTarget = bx.Find(101)->assignedTarget = 3; bx.Find(101)->action = Action::Stay;
		t.OfferOf(bx, wE, *bx.Find(101), now + 20, ph, rng);
		CHECK(t.Accept(bx, 101, now + 20, ph, ev) != nullptr, "third escort taken");
		std::vector<PartyEvent> pev;
		p.Dismiss(bx, 101, PartyEvent::Kind::Left, pev);
		ev.clear();
		t.Update(bx, now + 21, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Failed) == 1 && ev[0].why == "dismissed" && ev[0].goodwill == -25.f, "dismissing the escort fails it with the penalty");
		// wiped in your care
		t.cooldown.clear();
		bx.Find(101)->currentTarget = bx.Find(101)->assignedTarget = 3; bx.Find(101)->action = Action::Stay;
		t.OfferOf(bx, wE, *bx.Find(101), now + 30, ph, rng);
		t.Accept(bx, 101, now + 30, ph, ev);
		Kill(*bx.Find(101));
		ev.clear();
		t.Update(bx, now + 31, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Failed) == 1 && ev[0].why == "escort dead" && ev[0].goodwill == -25.f, "escort wiped: fails with the penalty");
		// abandoning sends it back to the board
		for (auto& m : bx.Find(101)->members) m.health = 100.f;
		p.Reset(); bx.companions.clear();
		t.cooldown.clear();
		bx.Find(101)->currentTarget = bx.Find(101)->assignedTarget = 3; bx.Find(101)->action = Action::Stay;
		t.OfferOf(bx, wE, *bx.Find(101), now + 40, ph, rng);
		t.Accept(bx, 101, now + 40, ph, ev);
		ev.clear();
		CHECK(t.Abandon(bx, 101, now + 41, ev) && ev[0].goodwill == -25.f && !p.Has(101) && !bx.companions.count(101), "abandon: penalty, the squad leaves the party");
	}

	// ---- 11. defend: waves of the enemy's squad description at the giver's place, done when every wave is wiped
	{
		Board bx(cfg, wE, host);
		bx.Reset(now);
		bx.squads.push_back(squadE(201, Faction::Imperial, 1, 10, "imperial_novice"));   // the giver, at Fort Greymoor
		bx.squads.push_back(squadE(202, Faction::Bandit, 2, 8, "bandit_novice"));        // their enemies' description
		bx.squads.push_back(squadE(203, Faction::Vampire, 7, 40, "vampire_veteran"));    // too strong to send
		Tasks t = freshTasks(Faction::Imperial, TaskType::Defend, nullptr);
		{
			Group roaming = squadE(299, Faction::Imperial, 3, 10, "imperial_novice");
			roaming.action = Action::Reach; roaming.currentTarget = -1;
			CHECK(t.OfferOf(bx, wE, roaming, now, ph, rng) == TaskType::None, "a squad on the move has nothing to defend");
			Group inCity = squadE(298, Faction::Imperial, 0, 10, "imperial_novice");
			CHECK(t.OfferOf(bx, wE, inCity, now, ph, rng) == TaskType::None, "nothing to defend inside a city");
			t.offers.clear();
		}
		CHECK(t.OfferOf(bx, wE, *bx.Find(201), now, ph, rng) == TaskType::Defend, "defend offered");
		const Task* o = t.Offered(201);
		CHECK(o && o->node == 1 && o->squadDesc == "bandit_novice" && o->targetFaction == Faction::Bandit && o->count == 3, "the place, the bandits' description, 3 waves");
		CHECK(o && (o->dest == 2 || o->dest == 3), "spawn place 12-40k from the place (camp or road)");
		CHECK(o && o->gold == int(120 * 3 + 10 * 8), "defend pay = per wave x waves + per level");
		const std::size_t before = bx.squads.size();
		std::vector<TaskEvent> ev;
		Task* a = t.Accept(bx, 201, now, ph, ev);
		CHECK(a && bx.squads.size() == before + 1 && a->step == 1 && a->extra && bx.Find(a->extra), "accept: the first wave is made");
		const std::uint64_t w1 = a ? a->extra : 0;
		const Group* wv = bx.Find(w1);
		CHECK(wv && wv->faction == Faction::Bandit && wv->section == "bandit_novice" && wv->node == a->dest, "wave: the description, at the spawn place");
		CHECK(bx.forced.count(w1) && bx.forced[w1] == 1 && bx.pinned.count(201) && t.IsTarget(w1), "wave forced at the place; giver pinned");
		CHECK(Tasks::Objective(*a, false).find("(wave 1 of 3)") != std::string::npos, "objective counts the waves");
		// the forced wave walks to the place and stays there, long past any stay (own small board: no fights, no respawns)
		{
			Board b2(cfg, wE, host);
			b2.Reset(now);
			b2.squads.push_back(*bx.Find(w1));
			b2.squads.back().idleHours = 0.1;
			b2.maxSquads = 1;
			b2.forced = bx.forced;
			b2.Advance(now + 48.0);
			const Group* g2 = b2.Find(w1);
			CHECK(g2 && g2->action == Action::Stay && g2->currentTarget == 1 && g2->assignedTarget == 1, "the wave reaches the place and is held there");
			CHECK(g2 && dist(g2->x, g2->y, wE.nodes[1].x, wE.nodes[1].y) < 2000.f, "... standing at it");
		}
		ev.clear();
		t.Update(bx, now + 1, ph, ev);
		CHECK(ev.empty(), "wave alive: open");
		Kill(*bx.Find(w1));
		t.Update(bx, now + 2, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Progress) == 1 && t.active[0].step == 2 && t.active[0].extra != w1 && !bx.forced.count(w1) && bx.forced.count(t.active[0].extra),
		      "wave wiped: the next one comes (Progress)");
		CHECK(Tasks::Objective(t.active[0], false).find("(wave 2 of 3)") != std::string::npos, "wave 2 of 3");
		Kill(*bx.Find(t.active[0].extra));
		t.Update(bx, now + 3, ph, ev);
		Kill(*bx.Find(t.active[0].extra));
		ev.clear();
		CHECK(!t.TurnIn(bx, 201, now + 3, ph, ev), "no pay while a wave stands");
		t.Update(bx, now + 4, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Ready) == 1 && bx.forced.empty() && bx.pinned.empty(), "every wave wiped: ready, giver released");
		ev.clear();
		CHECK(t.TurnIn(bx, 201, now + 5, ph, ev) && ev[0].gold == 440 && ev[0].goodwill == 50.f, "defend paid");
		// giver wiped mid-defence: fails, no penalty, the wave goes free
		t.cooldown.clear();
		t.OfferOf(bx, wE, *bx.Find(201), now + 10, ph, rng);
		a = t.Accept(bx, 201, now + 10, ph, ev);
		const std::uint64_t w2 = a ? a->extra : 0;
		Kill(*bx.Find(201));
		ev.clear();
		t.Update(bx, now + 11, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Failed) == 1 && ev[0].why == "giver dead" && ev[0].goodwill == 0.f && !bx.forced.count(w2), "giver wiped: failed, no penalty, wave free");
	}

	// ---- 12. rescue: a captive held by the giver's enemies, out of the simulation; kill the captors, bring it back
	{
		Board bx(cfg, wE, host);
		bx.Reset(now);
		bx.squads.push_back(squadE(301, Faction::Imperial, 3, 10, "imperial_novice"));   // the giver, Riverwood Road
		bx.squads.push_back(squadE(302, Faction::Bandit, 7, 8, "bandit_novice"));        // captors at Bleak Falls
		bx.squads.push_back(squadE(303, Faction::Bandit, 7, 6, "bandit_novice"));
		Party p;
		Tasks t = freshTasks(Faction::Imperial, TaskType::Rescue, &p);
		CHECK(t.OfferOf(bx, wE, *bx.Find(301), now, ph, rng) == TaskType::Rescue, "rescue offered");
		const Task* o = t.Offered(301);
		CHECK(o && o->node == 7 && o->pinned.size() == 2 && o->targetFaction == Faction::Bandit, "the captors' place and both captor squads");
		CHECK(o && o->gold == int(150 + 10 * (8 + 6)), "rescue pay = base + per captor level");
		const std::size_t before = bx.squads.size();
		std::vector<TaskEvent> ev;
		Task* a = t.Accept(bx, 301, now, ph, ev);
		const std::uint64_t cap = a ? a->extra : 0;
		const Group* cg = bx.Find(cap);
		CHECK(a && bx.squads.size() == before + 1 && cg && cg->faction == Faction::Imperial && cg->livingCount() == 1 && cg->node == 7,
		      "accept: a one-man squad of the giver's faction at the captors' place");
		CHECK(a && !a->itemName.empty() && cg && a->itemName == cg->members[0].name && Tasks::Objective(*a, false).find("taken by the Bandit") != std::string::npos,
		      "the journal names the captive and the captors");
		CHECK(bx.held.count(cap) && bx.pinned.count(302) && bx.pinned.count(303) && t.IsTarget(cap), "captive held, captors pinned");
		{   // held: never moves, never fights (own small board)
			Board b2(cfg, wE, host);
			b2.Reset(now);
			b2.squads.push_back(*bx.Find(cap));
			b2.maxSquads = 1;
			b2.held = bx.held;
			b2.Advance(now + 24.0);
			const Group* g2 = b2.Find(cap);
			CHECK(g2 && g2->x == wE.nodes[7].x && g2->y == wE.nodes[7].y && g2->assignedTarget == -1 && g2->livingCount() == 1, "the held captive stays put");
		}
		ev.clear();
		Kill(*bx.Find(302));
		t.Update(bx, now + 1, ph, ev);
		CHECK(ev.empty() && bx.held.count(cap), "one captor squad left: still held");
		Kill(*bx.Find(303));
		t.Update(bx, now + 2, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Ready) == 1 && !bx.held.count(cap) && p.Has(cap) && bx.companions.count(cap) && t.active[0].step == 1 &&
		      has(t.partyEvents, PartyEvent::Kind::Joined, cap), "captors dead: the captive joins the player");
		CHECK(t.StateFor(301, ph) == kTaskWorking && !t.TurnIn(bx, 301, now + 2, ph, ev), "hand-in only once the captive stands by the giver");
		CHECK(Tasks::Objective(t.active[0], true).find("Bring " + t.active[0].itemName) == 0, "return objective names the captive");
		bx.Find(cap)->x = bx.Find(301)->x + 300.f; bx.Find(cap)->y = bx.Find(301)->y;
		t.Update(bx, now + 3, ph, ev);
		CHECK(t.StateFor(301, ph) == kTaskReady, "captive at the giver: ready to hand in");
		ev.clear();
		t.partyEvents.clear();
		CHECK(t.TurnIn(bx, 301, now + 3, ph, ev) && ev[0].gold == 290 && !p.Has(cap) && has(t.partyEvents, PartyEvent::Kind::Left, cap), "rescue paid; the captive leaves the party");
		// the captive dies: penalty
		for (auto& g : bx.squads) if (g.id == 302 || g.id == 303) for (auto& m : g.members) m.health = 100.f;
		t.cooldown.clear();
		t.OfferOf(bx, wE, *bx.Find(301), now + 10, ph, rng);
		a = t.Accept(bx, 301, now + 10, ph, ev);
		Kill(*bx.Find(a ? a->extra : 0));
		ev.clear();
		t.Update(bx, now + 11, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Failed) == 1 && ev[0].why == "captive dead" && ev[0].goodwill == -25.f && bx.held.empty(), "captive dead: fails with the penalty");
		// no room in the party: the freed captive walks to the giver's place
		p.cfg.maxSquads = 0;
		t.cooldown.clear();
		t.OfferOf(bx, wE, *bx.Find(301), now + 20, ph, rng);
		a = t.Accept(bx, 301, now + 20, ph, ev);
		const std::uint64_t cap3 = a ? a->extra : 0;
		Kill(*bx.Find(302)); Kill(*bx.Find(303));
		ev.clear();
		t.Update(bx, now + 21, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Ready) == 1 && !p.Has(cap3) && t.active[0].step == 0 && bx.forced.count(cap3) && bx.forced[cap3] == 3,
		      "party full: the captive walks to the giver's place on its own");
		t.Abandon(bx, 301, now + 22, ev);
		CHECK(bx.forced.empty(), "abandoned: the captive goes its own way");
		// dismissed by the player: fails
		p.cfg.maxSquads = 2;
		for (auto& g : bx.squads) if (g.id == 302 || g.id == 303) for (auto& m : g.members) m.health = 100.f;
		t.cooldown.clear();
		t.OfferOf(bx, wE, *bx.Find(301), now + 30, ph, rng);
		a = t.Accept(bx, 301, now + 30, ph, ev);
		const std::uint64_t cap4 = a ? a->extra : 0;
		Kill(*bx.Find(302)); Kill(*bx.Find(303));
		t.Update(bx, now + 31, ph, ev);
		std::vector<PartyEvent> pev;
		p.Dismiss(bx, cap4, PartyEvent::Kind::Left, pev);
		ev.clear();
		t.Update(bx, now + 32, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Failed) == 1 && ev[0].why == "dismissed" && ev[0].goodwill == -25.f, "dismissing the freed captive fails the job");
		// monsters corner the captive
		Board bm(cfg, wE, host);
		bm.Reset(now);
		bm.squads.push_back(squadE(311, Faction::Imperial, 3, 10, "imperial_novice"));
		bm.squads.push_back(squadE(312, Faction::Wildlife, 7, 6, "wildlife_novice"));
		Tasks tm = freshTasks(Faction::Imperial, TaskType::Rescue, &p);
		CHECK(tm.OfferOf(bm, wE, *bm.Find(311), now, ph, rng) == TaskType::Rescue && tm.Offered(311)->targetFaction == Faction::Wildlife &&
		      Tasks::Objective(*tm.Offered(311), false).find("wounded and cornered") != std::string::npos, "monster captors: wounded and cornered");
	}

	// ---- 13. recover: a body with a satchel at a place, preferring a guarded one; an unguarded place may get critters
	{
		Board bx(cfg, wE, host);
		bx.Reset(now);
		bx.squads.push_back(squadE(401, Faction::Mage, 5, 10, "mage_novice"));            // the giver, Lone Hill
		bx.squads.push_back(squadE(402, Faction::Wildlife, 7, 5, "wildlife_novice"));     // wolves at Bleak Falls
		Tasks t = freshTasks(Faction::Mage, TaskType::Recover, nullptr);
		{
			Tasks ns = freshTasks(Faction::Mage, TaskType::Recover, nullptr);
			ns.cfg.satchel = 0; ns.cfg.satchelSpec.clear();
			CHECK(ns.OfferOf(bx, wE, *bx.Find(401), now, ph, rng) == TaskType::None, "no satchel item: no recover jobs");
		}
		CHECK(t.OfferOf(bx, wE, *bx.Find(401), now, ph, rng) == TaskType::Recover, "recover offered");
		const Task* o = t.Offered(401);
		CHECK(o && o->node == 7 && o->targetFaction == Faction::Wildlife && o->squadDesc.empty() && o->item == 0x0A00080D && o->count == 1,
		      "the guarded place is preferred; no critters added");
		const float km = dist(wE.nodes[5].x, wE.nodes[5].y, wE.nodes[7].x, wE.nodes[7].y) / 70000.f;
		CHECK(o && o->gold == int(tc.recoverBase + tc.recoverPerKm * km), "recover pay = base + per km");
		const int rpay = o ? o->gold : -1;
		std::vector<TaskEvent> ev;
		Task* a = t.Accept(bx, 401, now, ph, ev);
		CHECK(a && a->extra == 0 && bx.pinned.empty() && Tasks::Objective(*a, false).find("satchel") != std::string::npos, "accept: nothing made; objective");
		a->corpse = 0xFF000ABC;   // the plugin lays out the body
		ev.clear();
		t.Update(bx, now + 1, ph, ev);
		CHECK(ev.empty() && t.StateFor(401, ph) == kTaskWorking, "no satchel: open");
		ph.items[0x0A00080D] = 1;
		t.Update(bx, now + 2, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Ready) == 1 && t.StateFor(401, ph) == kTaskReady, "satchel taken: ready");
		ph.items[0x0A00080D] = 0;
		t.Update(bx, now + 3, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Reopened) == 1, "satchel dropped: open again");
		ph.items[0x0A00080D] = 1;
		ev.clear();
		CHECK(t.TurnIn(bx, 401, now + 4, ph, ev) && ev[0].takeItem == 0x0A00080D && ev[0].takeCount == 1 && ev[0].gold == rpay,
		      "hand-in takes the satchel");
		apply(ev);
		CHECK(ph.items[0x0A00080D] == 0, "satchel gone from the pack");
		// unguarded: critters of the nearest kind, pinned at the place
		Board bu(cfg, wE, host);
		bu.Reset(now);
		bu.squads.push_back(squadE(411, Faction::Mage, 5, 10, "mage_novice"));
		bu.squads.push_back(squadE(412, Faction::Wildlife, 4, 5, "wildlife_novice"));   // the nearest monsters, at Rorikstead (out of range)
		Tasks tu = freshTasks(Faction::Mage, TaskType::Recover, nullptr);
		tu.cfg.recoverGuardChance = 1.f;
		CHECK(tu.OfferOf(bu, wE, *bu.Find(411), now, ph, rng) == TaskType::Recover, "recover offered at an unguarded place");
		o = tu.Offered(411);
		CHECK(o && o->squadDesc == "wildlife_novice" && o->node != 4 && o->targetFaction == Faction::Wildlife, "critters: the nearest monster kind");
		a = tu.Accept(bu, 411, now, ph, ev);
		const Group* gd = a ? bu.Find(a->extra) : nullptr;
		CHECK(gd && gd->faction == Faction::Wildlife && gd->currentTarget == std::int64_t(a->node) && gd->action == Action::Stay && bu.pinned.count(gd->id),
		      "accept: the guard is made at the place and pinned");
		ph.items[0x0A00080D] = 1;
		ev.clear();
		CHECK(tu.Abandon(bu, 411, now + 1, ev) && ev[0].takeItem == 0x0A00080D && ev[0].goodwill == -25.f && bu.pinned.empty(), "abandon: satchel back, penalty, guard released");
		ph.items.clear();
		tu.cfg.recoverGuardChance = 0.f;
		tu.cooldown.clear();
		tu.OfferOf(bu, wE, *bu.Find(411), now + 10, ph, rng);
		CHECK(tu.Offered(411) && tu.Offered(411)->squadDesc.empty(), "guard chance 0: no critters");
	}

	// ---- 14. rankings and the top-10 hunt
	{
		Board bx(cfg, wE, host);
		bx.Reset(now);
		bx.squads.push_back(squadE(501, Faction::Imperial, 3, 10, "imperial_novice"));       // the giver
		bx.squads.push_back(squadE(502, Faction::Bandit, 2, 12, "bandit_novice"));           // 3 x level 12
		bx.squads.push_back(squadE(503, Faction::Bandit, 7, 9, "bandit_novice"));            // 3 x level 9
		bx.squads.push_back(squadE(504, Faction::Bandit, 1, 7, "bandit_novice", 4));         // 4 x level 7
		bx.squads.push_back(squadE(505, Faction::Bandit, 5, 5, "bandit_novice", 3));         // 3 x level 5
		bx.squads.push_back(squadE(506, Faction::Imperial, 4, 50, "imperial_veteran"));      // strong, but a friend of the giver
		bx.squads.push_back(squadE(507, Faction::Bandit, 6, 60, "bandit_veteran"));          // another worldspace
		bx.squads.push_back(squadE(508, Faction::Bandit, 7, 70, "bandit_veteran"));          // travelling with the player
		bx.companions.insert(508);
		bx.Find(505)->members[2].veteran = true;   // + rank_veteran
		bx.Find(505)->members[1].kills = 20;       // + 20 x rank_kill_points
		Tasks t = freshTasks(Faction::Imperial, TaskType::Hunt, nullptr);
		const auto all = t.Rankings(bx);
		bool sorted = true, noComp = true;
		for (std::size_t i = 1; i < all.size(); ++i) sorted &= all[i - 1].score >= all[i].score;
		for (auto& r : all) noComp &= r.squad != 508;
		CHECK(!all.empty() && sorted && noComp && all[0].squad == 507, "rankings: best first, companions left out, every worldspace");
		const auto tam = t.Rankings(bx, 0);
		CHECK(!tam.empty() && tam[0].squad == 506 && tam.size() == all.size() - 3, "worldspace filter");
		float vet = 0, killer = 0, plain = 0;
		for (auto& r : all) {
			if (r.member == bx.Find(505)->members[2].id) vet = r.score;
			if (r.member == bx.Find(505)->members[1].id) killer = r.score;
			if (r.member == bx.Find(505)->members[0].id) plain = r.score;
		}
		CHECK(vet == plain + tc.rankVeteran && killer == plain + 20.f * tc.rankKillPoints, "veterans and kills raise the score");
		{
			Board bf(cfg, wE, host);   // two enemies in the giver's worldspace, hunt_min 3
			bf.Reset(now);
			bf.squads.push_back(squadE(501, Faction::Imperial, 3, 10, "imperial_novice"));
			bf.squads.push_back(squadE(502, Faction::Bandit, 2, 12, "bandit_novice", 2));
			Tasks few = freshTasks(Faction::Imperial, TaskType::Hunt, nullptr);
			CHECK(few.OfferOf(bf, wE, *bf.Find(501), now, ph, rng) == TaskType::None, "fewer marks than hunt_min: no hunt");
		}
		CHECK(t.OfferOf(bx, wE, *bx.Find(501), now, ph, rng) == TaskType::Hunt, "hunt offered");
		const Task* o = t.Offered(501);
		bool enemies = true, inWs = true, order = true;
		float levels = 0;
		if (o) for (std::size_t i = 0; i < o->marks.size(); ++i) {
			const Group* g = bx.Find(o->marks[i].squad);
			enemies &= g && g->faction == Faction::Bandit;
			inWs &= g && g->ws == 0 && g->id != 508;
			for (auto& m : g->members) if (m.id == o->marks[i].member) levels += float(m.level);
		}
		if (o) {   // marks in rank order (the board's own ranking of the giver's enemies)
			std::size_t k = 0;
			for (auto& r : tam) if (k < o->marks.size() && r.faction == Faction::Bandit) order &= r.member == o->marks[k++].member;
		}
		CHECK(o && o->marks.size() == 10 && enemies && inWs && order, "ten marks: the best-ranked enemies in the giver's worldspace, in rank order");
		CHECK(o && o->target == o->marks.back().squad && o->targetMember == o->marks.back().member, "the hunt starts from #10");
		CHECK(o && o->gold == int(tc.huntBase + tc.huntPerLevel * levels), "hunt pay = base + per level of every mark");
		CHECK(o && Tasks::Objective(*o, false).find("#10 of the 10") != std::string::npos, "objective: #10 of the 10");
		const int hpay = o ? o->gold : -1;
		std::vector<TaskEvent> ev;
		Task* a = t.Accept(bx, 501, now, ph, ev);
		CHECK(a && t.IsTarget(a->marks[0].squad), "marks are taken (no bounty on them meanwhile)");
		auto killMark = [&](const Task::Mark& m) {
			for (auto& g : bx.squads) if (g.id == m.squad) for (auto& mm : g.members) if (mm.id == m.member) mm.health = 0.f;
		};
		const auto marks = a ? a->marks : std::vector<Task::Mark>{};
		ev.clear();
		t.Update(bx, now + 1, ph, ev);
		CHECK(ev.empty(), "mark alive: open");
		killMark(marks[9]);
		t.Update(bx, now + 2, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Progress) == 1 && t.active[0].step == 1 && t.active[0].targetMember == marks[8].member &&
		      Tasks::Objective(t.active[0], false).find("#9 of the 10") != std::string::npos, "mark dead: on to #9 (Progress)");
		killMark(marks[8]); killMark(marks[7]); killMark(marks[0]);   // #1 dies early, to someone else
		ev.clear();
		t.Update(bx, now + 3, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Progress) == 1 && t.active[0].step == 3 && t.active[0].targetMember == marks[6].member, "two at once: straight to #7");
		for (int i = 6; i >= 1; --i) killMark(marks[std::size_t(i)]);
		ev.clear();
		CHECK(!t.TurnIn(bx, 501, now + 4, ph, ev), "no pay while marks live");
		t.Update(bx, now + 4, ph, ev);
		CHECK(Count(ev, TaskEvent::Kind::Ready) == 1 && t.active[0].step == 10, "every mark dead (the early one skipped): ready");
		ev.clear();
		CHECK(t.TurnIn(bx, 501, now + 5, ph, ev) && ev[0].gold == hpay && ev[0].goodwill == 50.f, "hunt paid");
	}

	// ---- 15. TASK v3 co-save: every new field, older layouts, the board's sets rebuilt after a load
	{
		Tasks t;
		Task e;
		e.id = 11; e.type = TaskType::Escort; e.stage = TaskStage::Ready; e.slot = 0; e.giver = 101; e.giverFaction = Faction::Caravan; e.giverName = "Ri'saad";
		e.dest = 4; e.step = 1; e.deadline = 123.5; e.gold = 77;
		Task d;
		d.id = 12; d.type = TaskType::Defend; d.stage = TaskStage::Open; d.slot = 1; d.giver = 201; d.giverFaction = Faction::Imperial; d.node = 1; d.dest = 3;
		d.extra = 9001; d.step = 2; d.count = 3; d.squadDesc = "bandit_novice"; d.pinned = { 201 };
		Task r;
		r.id = 13; r.type = TaskType::Rescue; r.stage = TaskStage::Open; r.slot = 2; r.giver = 301; r.giverFaction = Faction::Imperial; r.node = 7;
		r.extra = 9002; r.pinned = { 302, 303 }; r.itemName = "Hadvar";
		Task h;
		h.id = 14; h.type = TaskType::Hunt; h.stage = TaskStage::Open; h.slot = 3; h.giver = 501; h.giverFaction = Faction::Imperial;
		for (std::uint64_t k = 0; k < 10; ++k) h.marks.push_back({ 600 + k, 7000 + k });
		h.step = 4; h.target = 605; h.targetMember = 7005;
		t.active = { e, d, r, h };
		Tasks::Offer of;
		of.until = 140.0;
		of.task.type = TaskType::Recover; of.task.giver = 401; of.task.giverFaction = Faction::Mage; of.task.node = 7; of.task.item = 0x0A00080D;
		of.task.squadDesc = "wildlife_novice"; of.task.corpse = 0xFF000ABC;
		t.offers[401] = of;
		t.cooldown[999] = 150.0;
		t.nextId = 15;
		std::vector<std::uint8_t> blob;
		t.Save(blob);
		Tasks u;
		std::size_t used = 0;
		CHECK(u.Load(blob.data(), blob.size(), 3, &used) && used == blob.size() && u.active.size() == 4 && u.offers.size() == 1 && u.nextId == 15, "TASK 3 loads, every byte read");
		bool same = u.active.size() == 4;
		for (std::size_t i = 0; same && i < 4; ++i) {
			const Task &x = t.active[i], &y = u.active[i];
			same = x.type == y.type && x.stage == y.stage && x.dest == y.dest && x.step == y.step && x.extra == y.extra && x.deadline == y.deadline &&
			       x.squadDesc == y.squadDesc && x.marks.size() == y.marks.size() && x.pinned == y.pinned && x.itemName == y.itemName && x.count == y.count;
			for (std::size_t k = 0; same && k < x.marks.size(); ++k) same = x.marks[k].squad == y.marks[k].squad && x.marks[k].member == y.marks[k].member;
		}
		const Task& lo = u.offers[401].task;
		CHECK(same && lo.corpse == 0xFF000ABC && lo.squadDesc == "wildlife_novice" && lo.type == TaskType::Recover, "TASK 3: every new field round-trips (jobs and offers)");
		// the board's sets are not saved: rebuilt from the open jobs
		Board bx(cfg, wE, host);
		bx.Reset(now);
		u.Rebind(bx);
		CHECK(bx.forced.size() == 1 && bx.forced[9001] == 1 && bx.held.size() == 1 && bx.held.count(9002) && bx.pinned.count(201) && bx.pinned.count(302) &&
		      bx.pinned.count(303) && bx.pinned.size() == 3, "after a load: forced wave, held captive, pinned giver and captors rebuilt");
		CHECK(u.IsTarget(9001) && u.IsTarget(9002) && u.IsTarget(605) && u.IsTarget(600) && !u.IsTarget(101), "targets after a load");
		// a TASK 2 save (M5a jobs) still loads; its jobs have the new fields empty
		Tasks old;
		Task f;
		f.id = 3; f.type = TaskType::Fetch; f.stage = TaskStage::Open; f.slot = 0; f.giver = 7; f.giverFaction = Faction::Mage; f.item = 0x3AD74; f.count = 3;
		f.dest = 99; f.step = 5; f.squadDesc = "should not survive";
		old.active.push_back(f);
		old.nextId = 4;
		std::vector<std::uint8_t> v2;
		old.Save(v2, 2);
		CHECK(v2.size() < blob.size() && u.Load(v2.data(), v2.size(), 2, &used) && used == v2.size() && u.active.size() == 1 && u.active[0].item == 0x3AD74 &&
		      u.active[0].dest == 0 && u.active[0].step == 0 && u.active[0].squadDesc.empty() && u.active[0].marks.empty(), "TASK 2 blob: loads, new fields empty");
		CHECK(!u.Load(v2.data(), v2.size(), 3) && u.active.empty(), "a v2 blob read as v3 is rejected (no half-read jobs)");
		CHECK(!u.Load(blob.data(), blob.size(), 4) && u.active.empty(), "TASK 4 (unknown): skipped");
		CHECK(!u.Load(blob.data(), blob.size() - 3, 3) && u.active.empty(), "truncated TASK 3: rejected");
	}

	// ---- 16. difficulty channels (T21): every type at 0.5x and 2x is still completable, pays more with the economy, and keeps
	// its waves and guards within the on-screen budget
	{
		const int budget = kMaxOnlineActors / kMaxOnlineGroups;
		{
			TaskNumbers base = tc;
			const TaskNumbers lo = Scaled(base, { 0.5f, 0.5f, 0.5f }), hi = Scaled(base, { 2.f, 2.f, 2.f }), one = Scaled(base, {});
			CHECK(one.defendWaves == base.defendWaves && one.goldMult == base.goldMult && one.maxLevelOver == base.maxLevelOver && one.huntMarks == base.huntMarks,
			      "1x: the config as written");
			CHECK(lo.defendWaves == 2 && hi.defendWaves == 6 && lo.huntMarks == 5 && hi.huntMarks == 10 && hi.recoverGuardChance == 1.f && lo.huntMin <= lo.huntMarks,
			      "task difficulty: waves, marks, guard chance");
			CHECK(lo.maxLevelOver == 3 && hi.maxLevelOver == 12 && hi.defendWaveSize == 2.f * base.defendWaveSize, "enemy strength: target levels, wave size");
			CHECK(lo.goldMult < one.goldMult && hi.goldMult > one.goldMult && hi.goodwillDone > one.goodwillDone && lo.goldMin >= 1, "economy: pay and goodwill");
			CHECK(hi.escortHoursPerKm < one.escortHoursPerKm && hi.goodwillFail > one.goodwillFail, "harder: less time, heavier penalty");
		}
		struct Run { bool done{ false }; int gold{ 0 }; int maxSquad{ 0 }; int waves{ 0 }; };
		auto runEscort = [&](TaskScale s) {
			Run r;
			Rng lr(7);   // the same rolls in every run: only the multipliers differ
			Board bx(cfg, wE, host);
			bx.Reset(now);
			bx.squads.push_back(squadE(101, Faction::Caravan, 3, 8, "caravan_novice"));
			Party p;
			Tasks t = freshTasks(Faction::Caravan, TaskType::Escort, &p);
			t.scale = s;
			std::vector<TaskEvent> ev;
			if (t.OfferOf(bx, wE, *bx.Find(101), now, ph, lr) != TaskType::Escort) return r;
			Task* a = t.Accept(bx, 101, now, ph, ev);
			if (!a) return r;
			const std::uint32_t d = a->dest;
			bx.Find(101)->x = wE.nodes[d].x; bx.Find(101)->y = wE.nodes[d].y;
			t.Update(bx, now + 0.5, ph, ev);
			ev.clear();
			r.done = t.TurnIn(bx, 101, now + 1, ph, ev);
			r.gold = r.done ? ev[0].gold : 0;
			return r;
		};
		auto runDefend = [&](TaskScale s) {
			Run r;
			Rng lr(7);   // the same rolls in every run: only the multipliers differ
			Board bx(cfg, wE, host);
			bx.Reset(now);
			bx.squads.push_back(squadE(201, Faction::Imperial, 1, 10, "imperial_novice"));
			bx.squads.push_back(squadE(202, Faction::Bandit, 2, 8, "bandit_veteran"));   // npc_in_squad 3..5: x2 would pass the budget
			Tasks t = freshTasks(Faction::Imperial, TaskType::Defend, nullptr);
			t.scale = s;
			std::vector<TaskEvent> ev;
			if (t.OfferOf(bx, wE, *bx.Find(201), now, ph, lr) != TaskType::Defend || !t.Accept(bx, 201, now, ph, ev)) return r;
			for (int k = 0; k < 20 && !t.active.empty() && t.active[0].stage == TaskStage::Open; ++k) {
				Group* wv = bx.Find(t.active[0].extra);
				if (!wv) break;
				r.maxSquad = std::max(r.maxSquad, wv->livingCount());
				++r.waves;
				Kill(*wv);
				t.Update(bx, now + 1 + k, ph, ev);
			}
			ev.clear();
			r.done = t.TurnIn(bx, 201, now + 30, ph, ev);
			r.gold = r.done ? ev[0].gold : 0;
			return r;
		};
		auto runRescue = [&](TaskScale s) {
			Run r;
			Rng lr(7);   // the same rolls in every run: only the multipliers differ
			Board bx(cfg, wE, host);
			bx.Reset(now);
			bx.squads.push_back(squadE(301, Faction::Imperial, 3, 10, "imperial_novice"));
			bx.squads.push_back(squadE(302, Faction::Bandit, 7, 8, "bandit_novice"));
			Party p;
			Tasks t = freshTasks(Faction::Imperial, TaskType::Rescue, &p);
			t.scale = s;
			std::vector<TaskEvent> ev;
			if (t.OfferOf(bx, wE, *bx.Find(301), now, ph, lr) != TaskType::Rescue) return r;
			Task* a = t.Accept(bx, 301, now, ph, ev);
			if (!a) return r;
			const std::uint64_t cap = a->extra;
			Kill(*bx.Find(302));
			t.Update(bx, now + 1, ph, ev);
			bx.Find(cap)->x = bx.Find(301)->x; bx.Find(cap)->y = bx.Find(301)->y;
			t.Update(bx, now + 2, ph, ev);
			ev.clear();
			r.done = t.TurnIn(bx, 301, now + 3, ph, ev);
			r.gold = r.done ? ev[0].gold : 0;
			return r;
		};
		auto runRecover = [&](TaskScale s) {
			Run r;
			Rng lr(7);   // the same rolls in every run: only the multipliers differ
			Board bx(cfg, wE, host);
			bx.Reset(now);
			bx.squads.push_back(squadE(401, Faction::Mage, 5, 10, "mage_novice"));
			bx.squads.push_back(squadE(402, Faction::Wildlife, 4, 5, "wildlife_veteran"));   // nearest monsters, out of range: guards come from them
			Tasks t = freshTasks(Faction::Mage, TaskType::Recover, nullptr);
			t.cfg.recoverGuardChance = 1.f;
			t.scale = s;
			std::vector<TaskEvent> ev;
			if (t.OfferOf(bx, wE, *bx.Find(401), now, ph, lr) != TaskType::Recover) return r;
			Task* a = t.Accept(bx, 401, now, ph, ev);
			if (!a) return r;
			if (const Group* gd = bx.Find(a->extra)) r.maxSquad = gd->livingCount();
			PlayerHost holder = ph;
			holder.items[0x0A00080D] = 1;
			t.Update(bx, now + 1, holder, ev);
			ev.clear();
			r.done = t.TurnIn(bx, 401, now + 2, holder, ev);
			r.gold = r.done ? ev[0].gold : 0;
			return r;
		};
		auto runHunt = [&](TaskScale s) {
			Run r;
			Rng lr(7);   // the same rolls in every run: only the multipliers differ
			Board bx(cfg, wE, host);
			bx.Reset(now);
			bx.squads.push_back(squadE(501, Faction::Imperial, 3, 10, "imperial_novice"));
			bx.squads.push_back(squadE(502, Faction::Bandit, 2, 12, "bandit_novice"));
			bx.squads.push_back(squadE(503, Faction::Bandit, 7, 9, "bandit_novice"));
			bx.squads.push_back(squadE(504, Faction::Bandit, 1, 7, "bandit_novice", 4));
			bx.squads.push_back(squadE(505, Faction::Bandit, 5, 5, "bandit_novice", 3));
			Tasks t = freshTasks(Faction::Imperial, TaskType::Hunt, nullptr);
			t.scale = s;
			std::vector<TaskEvent> ev;
			if (t.OfferOf(bx, wE, *bx.Find(501), now, ph, lr) != TaskType::Hunt || !t.Accept(bx, 501, now, ph, ev)) return r;
			r.waves = int(t.active[0].marks.size());
			for (int k = 0; k < 20 && t.active[0].stage == TaskStage::Open; ++k) {
				const Task::Mark* m = Tasks::CurrentMark(t.active[0]);
				if (!m) break;
				for (auto& g : bx.squads) if (g.id == m->squad) for (auto& mm : g.members) if (mm.id == m->member) mm.health = 0.f;
				t.Update(bx, now + 1 + k, ph, ev);
			}
			ev.clear();
			r.done = t.TurnIn(bx, 501, now + 30, ph, ev);
			r.gold = r.done ? ev[0].gold : 0;
			return r;
		};
		const char* names[5] = { "escort", "defend", "rescue", "recover", "hunt" };
		auto runAll = [&](TaskScale s) {
			return std::array<Run, 5>{ runEscort(s), runDefend(s), runRescue(s), runRecover(s), runHunt(s) };
		};
		const auto lo = runAll({ 0.5f, 0.5f, 0.5f }), hi = runAll({ 2.f, 2.f, 2.f });
		const auto eLo = runAll({ 1.f, 1.f, 0.5f }), eOne = runAll({}), eHi = runAll({ 1.f, 1.f, 2.f });
		for (int i = 0; i < 5; ++i) {
			const std::string n = names[i];
			CHECK(lo[i].done && lo[i].gold > 0, (n + ": completable and paid at 0.5x").c_str());
			CHECK(hi[i].done && hi[i].gold > 0, (n + ": completable and paid at 2x").c_str());
			CHECK(eOne[i].done && eLo[i].gold < eOne[i].gold && eOne[i].gold < eHi[i].gold, (n + ": pay moves with the economy multiplier").c_str());
		}
		CHECK(lo[1].waves == 2 && hi[1].waves == 6 && eOne[1].waves == 3, "defend: 2 / 3 / 6 waves at 0.5x / 1x / 2x task difficulty");
		CHECK(hi[1].maxSquad >= 1 && hi[1].maxSquad <= budget && lo[1].maxSquad >= 1, "defend: waves within the on-screen budget at 2x enemy strength");
		CHECK(hi[3].maxSquad >= 1 && hi[3].maxSquad <= budget, "recover: the guard within the on-screen budget at 2x");
		CHECK(hi[1].maxSquad > lo[1].maxSquad, "defend: bigger waves at 2x enemy strength than at 0.5x");
		CHECK(lo[4].waves == 5 && hi[4].waves == 10, "hunt: 5 marks at 0.5x, 10 at 2x");
	}

	// ---- 17. T21 difficulty: Tasks::scale from the config's difficulty state; Economy reaches the pay once (an M5a bounty: exactly 2x)
	{
		auto offerGold = [&](float economy, float taskMult) {
			cfg.diff.mult[int(DiffChannel::Economy)] = economy;
			cfg.diff.mult[int(DiffChannel::Tasks)] = taskMult;
			Tasks t;
			t.cfg = tc;
			t.cfg.noJobChance = 0.f;
			Only(t, Faction::Imperial, TaskType::Bounty);
			t.scale = TaskScaleFrom(cfg);
			Rng r(5);
			const bool ok = t.OfferOf(b, w, *b.Find(1), now, ph, r) == TaskType::Bounty && t.Offered(1);
			return ok ? t.Offered(1)->gold : -1;
		};
		const int g1 = offerGold(1.f, 1.f), g2 = offerGold(2.f, 1.f), gHalf = offerGold(0.5f, 1.f);
		cfg.diff.mult[int(DiffChannel::Economy)] = 3.f;
		cfg.diff.mult[int(DiffChannel::Tasks)] = 1.5f;
		const TaskScale s = TaskScaleFrom(cfg);
		cfg.diff.mult[int(DiffChannel::Economy)] = 1.f;
		cfg.diff.mult[int(DiffChannel::Tasks)] = 1.f;
		CHECK(s.economy == 3.f && s.task == 1.5f && s.enemy == 1.5f && TaskScaleFrom(cfg).economy == 1.f, "TaskScaleFrom: economy = Diff(Economy), task and enemy = Diff(Tasks)");
		CHECK(g1 > 0 && g2 == 2 * g1 && gHalf == g1 / 2, ("bounty at Economy 2x pays exactly 2x (not 4x), at 0.5x half: " + std::to_string(g1) + " / " + std::to_string(g2) + " / " + std::to_string(gHalf)).c_str());
	}

	// ---- 18. W3: an escort that is "Job ready" hands in through the companion dialogue
	{
		CHECK(JobTopicsInMode(kTalkModeGreet) && JobTopicsInMode(kTalkModeCompanion) && !JobTopicsInMode(0) && !JobTopicsInMode(1) && !JobTopicsInMode(2) &&
		      !JobTopicsInMode(5) && !JobTopicsInMode(6), "the job state is published to a squad that greets the player and to one travelling with them, nowhere else");
		Board bx(cfg, wE, host);
		bx.Reset(now);
		bx.squads.push_back(squadE(101, Faction::Caravan, 3, 8, "caravan_novice"));
		Party p;
		Tasks t = freshTasks(Faction::Caravan, TaskType::Escort, &p);
		std::vector<TaskEvent> ev;
		t.OfferOf(bx, wE, *bx.Find(101), now, ph, rng);
		Task* a = t.Accept(bx, 101, now, ph, ev);
		CHECK(a && p.Has(101) && t.StateFor(101, ph) == kTaskWorking, "escort under way: the companion squad says 'working' (ALifeTaskState 1)");
		const std::uint32_t d = a ? a->dest : 0;
		bx.Find(101)->x = wE.nodes[d].x; bx.Find(101)->y = wE.nodes[d].y;
		t.Update(bx, now + 1, ph, ev);
		CHECK(t.StateFor(101, ph) == kTaskReady, "escort arrived: the companion squad says 'ready' (ALifeTaskState 2), which shows 'About that job. It's done.' in companion mode");
		ev.clear();
		CHECK(t.TurnIn(bx, 101, now + 2, ph, ev) && Count(ev, TaskEvent::Kind::Done) == 1 && ev[0].gold > 0 && !p.Has(101), "the hand-in pays and the squad leaves the party");
		// the generated dialogue (tools/build_talk_esp.py -> talk.ltx): hand-in and abandon each have companion-mode answers besides the greeting ones
		Ltx tk;
		int turnin = 0, abandon = 0;
		if (tk.LoadFile(dir + "/talk.ltx", nullptr))
			if (auto* lines = tk.Section("responses"))
				for (auto& [id, action] : *lines) { turnin += Trim(action) == "turnin"; abandon += Trim(action) == "abandon"; }
		CHECK(turnin >= 4 && abandon >= 4, "talk.ltx: 'It's done.' and 'I can't finish that job.' answer in greeting and companion mode");
	}

	// ---- T11: saves from before the Solstheim factions (TASK 1) numbered the monsters from 15
	{
		CHECK(FactionFromPreSolstheim(14) == Faction::Caravan && FactionFromPreSolstheim(15) == Faction::Wildlife &&
		      FactionFromPreSolstheim(20) == Faction::Zombie && FactionFromPreSolstheim(kNoFaction) == Faction(kNoFaction),
		      "pre-Solstheim ids: humans keep theirs, monsters shift past the three");
		Tasks t;
		Task k;
		k.id = 7; k.type = TaskType::Bounty; k.stage = TaskStage::Open; k.slot = 0;
		k.giverFaction = Faction::Imperial; k.targetFaction = Faction(15);   // byte 15: Wildlife in TASK 1, Skaal in TASK 2
		t.active.push_back(k); t.nextId = 8;
		std::vector<std::uint8_t> blob;
		t.Save(blob, 2);   // TASK 1 and 2 share the layout; only the faction numbering differs
		Tasks u;
		CHECK(u.Load(blob.data(), blob.size(), 1) && u.active.size() == 1 && u.active[0].giverFaction == Faction::Imperial &&
		      u.active[0].targetFaction == Faction::Wildlife, "TASK 1 blob: faction byte 15 loads as Wildlife");
		CHECK(u.Load(blob.data(), blob.size(), 2) && u.active.size() == 1 && u.active[0].targetFaction == Faction::Skaal,
		      "TASK 2 blob: faction byte 15 loads as Skaal");
	}

	// ---- T11 step 9: squads never target, re-check or walk into another worldspace (the harness --solstheim leak)
	{
		World w2;
		w2.nodes.push_back(Node("Dragon Bridge Road", Kind::Wilderness, 0, 0));        // 0: Tamriel, nothing for anyone
		w2.nodes.push_back(Node("Karthwasten Road", Kind::Wilderness, 20000, 0));      // 1: Tamriel, nothing for anyone
		w2.nodes.push_back(Node("Raven Rock", Kind::Town, 0, 0, 1));                   // 2: Solstheim, open to all
		w2.nodes.push_back(Node("Skaal Village", Kind::Town, 30000, 0, 1));            // 3: Solstheim, open to all
		w2.Finalize(cfg);
		for (auto& n : w2.nodes) {
			n.props = Props{};
			n.disabled = false; n.simAvail = true; n.maxPopulation = 4;
			if (n.ws == 1) { n.props.all = 1.f; n.props.territory = 1.f; n.props.base = 1.f; }
		}
		CHECK(w2.NextHop(0, 2) == 0 && w2.NextHop(2, 0) == 2, "no path between worldspaces (NextHop reports 0xFFFF as the start node)");
		TestHost h2;
		Board b2(cfg, w2, h2);
		b2.Reset(100.0);
		// (a) a Tamriel squad whose only above-zero targets are on Solstheim gets no target, on choice or on the re-check
		Group tam = Squad(901, Faction::Imperial, w2, 0, 10);
		bool solPositive = false;
		for (std::uint32_t i = 2; i < 4; ++i) solPositive |= b2.EvaluatePrior(w2.nodes[i], b2.smart[i], tam) > 0.f;
		CHECK(solPositive, "the Solstheim places score above zero for the Tamriel squad (the test would be empty otherwise)");
		CHECK(b2.GetSquadTarget(tam) == nullptr, "a Tamriel squad whose only valid places are on Solstheim gets no target");
		CHECK(!b2.TargetPrecondition(w2.nodes[2], b2.smart[2], tam, true) && !b2.TargetPrecondition(w2.nodes[3], b2.smart[3], tam, false),
		      "the arrival re-check rejects a place in another worldspace");
		// (b) an unreachable hop never moves the squad: a squad already heading for a place it has no path to (its worldspace says
		// Solstheim, it stands on a Tamriel node, as a stale or corrupt record would) drops the target and stays where it is
		Group lost = Squad(902, Faction::Skaal, w2, 1, 10);
		lost.ws = 1;
		lost.action = Action::Reach; lost.assignedTarget = 2; lost.currentTarget = -1;
		b2.squads.push_back(lost);
		b2.Step(cfg.stepHours);
		const Group* g = b2.Find(902);
		CHECK(g && g->node == 1 && g->x == w2.nodes[1].x && g->y == w2.nodes[1].y, "an unreachable hop never moves the squad (no jump to the target)");
		CHECK(g && g->assignedTarget == -1 && g->currentTarget == -1 && g->action != Action::Reach, "an unreachable target is dropped");
	}

	std::printf("tasks: %d passed, %d failed\n", gPass, gFails);
	return gFails ? 1 : 0;
}
