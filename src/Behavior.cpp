#include "ALife/Forms.h"
#include "ALife/Jobs.h"
#include "ALife/Log.h"
#include "ALife/Rep.h"
#include "ALife/Settings.h"
#include "ALife/Systems.h"
#include "ALife/Talk.h"

// How an on-screen squad treats the player. Every half second a squad weighs what it knows about the player (docs/STANDING.md:
// allegiance, status, gear, bounty, reputation, its own memory, the odds) and the model returns a call:
//   ignore / greet / watch / challenge / parley / attack / withdraw / assist
// This file turns the call into behaviour, quietly: a squad that wants a word walks up, stops and says one subtitled line, then
// waits for the player to talk to it (Skyrim's own dialogue, see Talk.h). It only turns on the player when the player ignores it
// for too long, draws steel on it, or tells it to go to hell in the conversation. No menus open, nothing pauses.
namespace ALife::Behavior {
namespace {
	float Dist(const RE::NiPoint3& a, float x, float y)
	{
		float dx = a.x - x, dy = a.y - y;
		return std::sqrt(dx * dx + dy * dy);
	}

	enum class After : std::uint8_t { None, Calm, Hostile, Barter };

	struct Meeting
	{
		float clock{ 0 };            // seconds this squad has been tracked
		float nextJudge{ 0 };
		float nextMove{ 0 };
		float challengeAt{ -1 };     // clock value when it began challenging (-1: not challenging)
		float calmFor{ 0 };          // seconds without a reason to be tense
		bool greeted{ false };
		bool parleyOffered{ false };
		bool parleyRefused{ false };
		bool warned{ false };
		float pursueUntil{ -1 };     // committed to catching the player until this clock value (like the courier)
		float chaseStart{ -1 };      // clock value when the squad set out to reach the player
		float lastDist{ 99999 };     // distance at the last judgement of a chase, to notice a squad that is stuck behind scenery
		float stuckFor{ 0 };
		bool forced{ false };        // the conversation has been forced
		float forceAt{ 0 };
		bool wasTalking{ false };
		bool afterDone{ false };     // the reply to the player's choice has been spoken; its consequence waits until the conversation has closed
		int talkMode{ 0 };           // Talk::Mode the squad is in: what the dialogue offers
		int toll{ 0 };
		float talkAt{ 0 };
		float say2At{ -1 };
		std::string say2;
		After after{ After::None };  // what happens when the reply being spoken ends
		Call lastCall{ Call::Ignore };
		float nextLog{ 0 };
	};
	std::unordered_map<std::uint64_t, Meeting> gMeet;

	RE::Actor* LeaderOf(const Group& g)
	{
		for (auto& m : g.members)
			if (auto* a = Pool::ActorOf(m); a && !a->IsDead()) return a;
		return nullptr;
	}

	void SetFlee(Group& g, bool on)
	{
		const auto& r = Forms::R();
		auto* pc = RE::PlayerCharacter::GetSingleton();
		for (auto& m : g.members) {
			auto* a = Pool::ActorOf(m);
			if (!a || a->IsDead()) continue;
			if (on) {
				a->extraList.SetLinkedRef(pc, r.kwFlee);   // flee-from package keeps distance from the player
				if (r.fMode) a->AddToFaction(r.fMode, 4);
			} else {
				a->extraList.SetLinkedRef(nullptr, r.kwFlee);
				if (r.fMode) a->AddToFaction(r.fMode, &m == &g.members.front() ? 2 : 3);
			}
			a->EvaluatePackage(true, false);
		}
	}

	void TurnHostile(Group& g)
	{
		// Escalation must arm combat AI, not just flip a faction: a civil pop-in leaves Aggression 0 (Unaggressive).
		const auto& r = Forms::R();
		auto* pc = RE::PlayerCharacter::GetSingleton();
		const bool pcOk = pc && !pc->IsDead();
		int armed = 0, engaged = 0;
		for (auto& m : g.members) {
			auto* a = Pool::ActorOf(m);
			if (!a || a->IsDead()) continue;
			a->extraList.SetLinkedRef(nullptr, r.kwFlee);
			if (r.fPlayerEnemy) a->AddToFaction(r.fPlayerEnemy, 0);
			if (r.fMode) a->AddToFaction(r.fMode, 0);
			if (auto* avo = a->AsActorValueOwner()) {
				if (avo->GetBaseActorValue(RE::ActorValue::kAggression) < 1.f)
					avo->SetBaseActorValue(RE::ActorValue::kAggression, 1.f);
			}
			++armed;
			a->EvaluatePackage(true, false);
			if (pcOk && a->Is3DLoaded() && !a->IsInCombat()) {
				a->StartCombat(pc);
				++engaged;
			}
		}
		g.trust = Trust::Hostile;
		g.foughtPlayer = true;
		g.standing -= 2;
		Log::Line("[trust] " + std::string(FactionId(g.faction)) + " squad " + std::to_string(g.id) + " turns hostile (aggr=1 x" +
				  std::to_string(armed) + ", combat x" + std::to_string(engaged) + ")");
	}

	// Back to ordinary life: stop waiting for a word, stop fleeing, pick the board's plan up again.
	void Calm(Group& g, Meeting& mt)
	{
		const bool tense = g.trust == Trust::Wary || g.trust == Trust::BackingOff;
		if (g.trust == Trust::BackingOff) SetFlee(g, false);
		mt.challengeAt = -1.f;
		mt.calmFor = 0.f;
		mt.talkMode = Talk::kNone;
		mt.parleyOffered = false;
		mt.warned = false;
		mt.say2At = -1.f;
		mt.pursueUntil = -1.f;
		mt.forced = false;
		if (tense) {
			g.trust = Trust::Neutral;
			Online::Resume(g);
		}
	}

	bool InTerritory(const Group& g, const RE::NiPoint3& pp)
	{
		// the place it holds: reached (currentTarget) or settled at (assigned and staying)
		const std::int64_t tid = g.currentTarget >= 0 ? g.currentTarget : (g.action == Action::Stay ? g.assignedTarget : -1);
		if (tid < 0) return false;
		const auto* n = Graph::Node(std::uint32_t(tid));
		if (!n) return false;
		return Dist(pp, n->x, n->y) <= std::max(n->radius, Sim::Cfg().standing.territoryRadius);
	}

	Env BuildEnv(const Group& g, const Meeting& mt, const RE::NiPoint3& pp)
	{
		Env e;
		e.dist = Dist(pp, g.x, g.y);
		e.squadPower = Rep::SquadPower(g);
		e.now = Sim::Hours();
		e.site = Rep::SiteOf(g);
		e.inTerritory = InTerritory(g, pp);
		for (auto& m : g.members)
			if (m.alive() && m.hunger >= Sim::Cfg().hungryAt) { e.hungry = true; break; }
		e.tooHurt = TooHurt(g);
		e.greeted = mt.greeted;
		e.parleyRefused = mt.parleyRefused;
		e.challengeElapsed = mt.challengeAt >= 0.f ? mt.clock - mt.challengeAt : 0.f;
		return e;
	}

	void Speak(Group& g, int mode)
	{
		if (auto* lead = LeaderOf(g)) Talk::Subtitle(lead, Talk::OpeningLine(g, mode));
	}

	// The courier's way of opening a conversation: the squad walks up and the player is made to talk to its leader, exactly as if the
	// player had activated it. The situation (what the dialogue should offer) is published first and held for a few seconds, because
	// the engine reads it when the greeting is chosen. (The engine's own SetDialogueWithPlayer only raises a flag that a ForceGreet
	// package acts on, and squad members have no such package.)
	void ForceGreet(Group& g, Meeting& mt, RE::Actor* lead)
	{
		// not over a menu the player is already in (inventory, map, another conversation ...): the squad waits its turn
		if (auto* ui = RE::UI::GetSingleton(); ui && (ui->GameIsPaused() || Talk::InConversation())) return;
		int mode = 0, toll = 0;
		TalkSituation(g, mode, toll);
		Talk::Publish(mode, toll);
		Jobs::PublishFor(mode == Talk::kGreet ? &g : nullptr);
		Talk::Hold(6.f);
		mt.forced = true;
		mt.forceAt = mt.clock;
		auto* pc = RE::PlayerCharacter::GetSingleton();
		const bool ok = pc && lead->ActivateRef(pc, 0, nullptr, 1, false);
		Log::Line(std::string("[talk] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + " catches up with the player and speaks (mode " +
				  std::to_string(mode) + ", activation " + (ok ? "accepted" : "refused") + ")");
		if (Settings::Get().bDebug) Talk::Diagnose(lead);
	}

	// A squad sent to talk can get stuck behind scenery (a hillside, a fallen tree) where the engine's pathing gives up. If it has not
	// closed in for a few seconds and the player cannot see its leader, the squad turns up within calling distance, like the courier
	// who always seems to find the player; if the player can see the leader, the walk simply goes on.
	void UnstickChase(Group& g, Meeting& mt, const Env& env, RE::Actor* lead)
	{
		if (mt.forced || env.dist <= 700.f) { mt.stuckFor = 0.f; mt.lastDist = env.dist; return; }
		if (mt.lastDist - env.dist > 40.f) mt.stuckFor = 0.f;
		else mt.stuckFor += 0.5f;   // judged every half second
		mt.lastDist = env.dist;
		if (mt.stuckFor < 8.f) return;
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !lead) return;
		bool unused = false;
		if (pc->HasLineOfSight(lead, unused)) return;   // the player can see them walk: let them
		const auto pp = pc->GetPosition(), lp = lead->GetPosition();
		const float dx = lp.x - pp.x, dy = lp.y - pp.y, d = std::max(1.f, std::sqrt(dx * dx + dy * dy));
		RE::NiPoint3 to{ pp.x + dx / d * 520.f, pp.y + dy / d * 520.f, pp.z + 60.f };
		float ground = 0.f;
		if (auto* tes = RE::TES::GetSingleton(); tes && tes->GetLandHeight(to, ground)) to.z = ground + 8.f;
		lead->SetPosition(to, true);
		mt.stuckFor = 0.f;
		mt.lastDist = 520.f;
		Log::Line(std::string("[talk] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + " was stuck " + std::to_string(int(d)) + " units out of sight and catches up");
	}

	// Ninety seconds of trying and the squad has not reached the player: they lose the trail and let it go (it was never refused).
	void GiveUp(Group& g, Meeting& mt)
	{
		Log::Line(std::string("[standing] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + " loses the player's trail");
		g.tollUntil = Sim::Hours() + double(Sim::Cfg().standing.tollPassHours) * 0.25;
		mt.chaseStart = -1.f;
		mt.pursueUntil = -1.f;
		mt.parleyOffered = false;
		mt.talkMode = Talk::kNone;
		mt.challengeAt = -1.f;
		Calm(g, mt);
	}

	// Allies stand with the player: whoever the player is fighting, they fight.
	void AssistPlayer(Group& g)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !pc->IsInCombat()) return;
		auto tgt = pc->GetActorRuntimeData().currentCombatTarget.get();
		if (!tgt || tgt->IsDead()) return;
		for (auto& m : g.members) {
			auto* a = Pool::ActorOf(m);
			if (a && !a->IsDead() && !a->IsInCombat() && a->Is3DLoaded()) a->StartCombat(tgt.get());
		}
	}
}

std::uint64_t HostilityHash(std::uint64_t gid, std::uint32_t day) { return Mix64(gid * 1000003ULL ^ (std::uint64_t(day) << 20)); }

void Reset() { gMeet.clear(); }

bool TooHurt(const Group& g)
{
	// lost half its members or more, or average health under 40%: never attack, move away
	const int living = g.livingCount();
	if (g.startMembers > 0 && living * 2 <= g.startMembers) return true;
	float sum = 0.f;
	int n = 0;
	for (auto& m : g.members) {
		if (!m.alive()) continue;
		sum += m.health / std::max(1.f, m.healthMax);
		++n;
	}
	return n > 0 && (sum / float(n)) < 0.4f;
}

bool AttacksOnSight(const Group& g)
{
	if (TooHurt(g)) return false;
	if (g.attackedByPlayer) return true;
	return Rep::RegardOf(g) == Regard::Hostile;
}

// What the dialogue should offer if the player talks to this squad now (Talk::Pump publishes it for the squad under the crosshair).
void TalkSituation(const Group& g, int& mode, int& toll)
{
	mode = Talk::kNone;
	toll = 0;
	if (g.trust == Trust::Hostile || g.trust == Trust::BackingOff) return;
	if (auto it = gMeet.find(g.id); it != gMeet.end()) {
		const auto& mt = it->second;
		if (mt.talkMode == Talk::kToll && !mt.parleyRefused) { mode = Talk::kToll; toll = mt.toll; return; }
		if (mt.talkMode == Talk::kChallenge) { mode = Talk::kChallenge; return; }
	}
	if (g.trust == Trust::Wary) return;   // tense, but nothing was said: no conversation to have
	if (Rep::RegardOf(g) >= Regard::Neutral) mode = Talk::kGreet;
}

bool TalkPending(const Group& g)
{
	auto it = gMeet.find(g.id);
	return it != gMeet.end() && it->second.talkMode != Talk::kNone;
}

void OnTalk(Group& g, Talk::Choice c, bool begin)
{
	auto& mt = gMeet[g.id];
	auto* pc = RE::PlayerCharacter::GetSingleton();
	static const char* kNames[] = { "-", "pay", "poor", "refuse", "fight", "peace", "defy", "news", "trade", "work", "accept", "decline", "turnin", "deliver", "abandon" };
	Log::Line(std::string("[talk] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + ": player chose '" + kNames[int(c)] + "'" + (begin ? "" : " (finished)"));
	const auto& sc = Sim::Cfg().standing;
	if (begin) {
		switch (c) {
		case Talk::Choice::Pay: {
			const int gold = pc ? pc->GetGoldAmount(true) : 0;
			const int pay = std::min(mt.toll, gold);
			if (pay > 0 && pc && Forms::R().gold) pc->RemoveItem(Forms::R().gold, pay, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			for (auto& m : g.members) if (m.alive()) { m.gold += pay; break; }
			Rep::S().OnToll(g, Sim::Hours());
			mt.after = After::Calm;
			Log::Line("[standing] toll of " + std::to_string(pay) + " paid to " + FactionId(g.faction) + " squad #" + std::to_string(g.id));
			break;
		}
		case Talk::Choice::Poor:
			g.tollUntil = Sim::Hours() + double(sc.tollPassHours) * 0.25;   // not worth their time: they let the pauper go
			mt.after = After::Calm;
			break;
		case Talk::Choice::Peace:
			g.memory = std::min(1000.f, g.memory + 80.f);
			g.tollUntil = Sim::Hours() + double(sc.tollPassHours) * 0.5;    // the explanation satisfies them for a while
			mt.after = After::Calm;
			break;
		case Talk::Choice::Refuse:
		case Talk::Choice::Fight:
		case Talk::Choice::Defy:
			mt.parleyRefused = true;
			mt.after = After::Hostile;
			break;
		case Talk::Choice::Trade:
			mt.greeted = true;
			mt.after = After::Barter;   // the menu opens once the conversation has closed
			break;
		case Talk::Choice::News: {
			g.memory = std::min(1000.f, g.memory + 15.f);
			mt.greeted = true;
			// after the stock remark, the squad passes on something that really happened (one of the world's latest events)
			if (const int n = std::min(Rep::NewsCount(), 6); n > 0) {
				std::string line = Rep::NewsLine(int(Mix64(g.id * 31ULL + std::uint64_t(Sim::Hours() * 10.0)) % std::uint64_t(n)));
				if (const auto p = line.find("  "); p != std::string::npos) line = line.substr(p + 2);   // drop the "Day 3 14:20" stamp
				mt.say2 = "Word is: " + line;
				mt.say2At = mt.clock + 4.f;
			}
			break;
		}
		default: break;
		}
		return;
	}
	// the reply has been spoken. The consequence (the squad calms, or turns on the player) waits for the tick: starting a fight from inside
	// the dialogue's own event, while the menu is still closing, hangs the game
	mt.afterDone = true;
}

void TickOnline(Group& g, float dt)
{
	if (g.trust == Trust::Hostile) return;   // hostile squads are engine-hostile via ALifePlayerEnemyFaction
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) return;
	auto& mt = gMeet[g.id];
	mt.clock += dt;
	auto* lead = LeaderOf(g);
	if (!lead) return;
	if (mt.say2At >= 0.f && mt.clock >= mt.say2At) { mt.say2At = -1.f; Talk::Subtitle(lead, mt.say2); }

	// the player is in a conversation with this squad: nothing else happens, and every timer waits
	if (Talk::ConversationSquad() == g.id) {
		mt.wasTalking = true;
		if (mt.challengeAt >= 0.f) mt.challengeAt += dt;
		mt.talkAt += dt;
		return;
	}
	bool settled = false;
	if (mt.afterDone) {    // the conversation is over: the choice takes effect now
		mt.afterDone = false;
		settled = true;
		if (mt.after == After::Calm) Calm(g, mt);
		else if (mt.after == After::Barter) Talk::OpenBarter(lead);
		else if (mt.after == After::Hostile && g.trust != Trust::Hostile) TurnHostile(g);
		mt.after = After::None;
		if (g.trust == Trust::Hostile) return;
	}
	if (mt.wasTalking) {   // the conversation ended; if no choice settled it, the player walked away from the table
		mt.wasTalking = false;
		if (!settled && mt.after == After::None && mt.talkMode != Talk::kNone) {
			mt.forced = true;                 // do not force it again
			mt.talkAt = mt.clock - 14.f;      // the patience clock resumes near its warning
			mt.pursueUntil = mt.clock + 20.f;
		}
	}
	if (mt.clock < mt.nextJudge) return;
	mt.nextJudge = mt.clock + 0.5f;

	Rep::Refresh();
	const auto& ctx = Rep::Context();
	const Env env = BuildEnv(g, mt, pc->GetPosition());
	const Verdict v = Rep::S().Judge(g, ctx, env);
	g.lastMetHours = env.now;
	if (Settings::Get().bDebug && (v.call != mt.lastCall || mt.clock >= mt.nextLog)) {
		mt.lastCall = v.call;
		mt.nextLog = mt.clock + 4.f;
		char fl[16];
		std::snprintf(fl, sizeof fl, "%X", unsigned(ctx.flags));
		Log::Line(std::string("[judge] ") + FactionId(g.faction) + " #" + std::to_string(g.id) + " " + CallName(v.call) + " (" + v.why + ") att=" +
				  std::to_string(int(v.attitude)) + " dist=" + std::to_string(int(env.dist)) + " terr=" + (env.inTerritory ? "y" : "n") + " chal=" +
				  std::to_string(env.challengeElapsed).substr(0, 4) + " pow=" + std::to_string(int(env.squadPower)) + "/" + std::to_string(int(ctx.power)) +
				  " flags=0x" + fl);
	}

	auto approach = [&](float stop) {
		g.trust = Trust::Wary;
		if (mt.clock >= mt.nextMove) { mt.nextMove = mt.clock + 1.5f; Online::Approach(g, stop); }
	};

	Call call = v.call;
	if ((call == Call::Parley || call == Call::Challenge) && mt.talkMode == Talk::kNone) {
		// how often squads stop the player is the player's choice (MCM): never / rarely / normally / often; a conversation that has just closed is always followed by a pause
		static const float kCooldownScale[] = { 1e9f, 3.f, 1.f, 0.3f };
		const int ti = std::clamp(Settings::Get().talkInitiative, 0, 3);
		if (Talk::SecondsSinceConversation() < Sim::Cfg().standing.talkCooldown * kCooldownScale[ti])
			call = Call::Watch;   // the player has just been talked to: no queue of strangers
	}
	if ((call == Call::Watch || call == Call::Ignore) && mt.talkMode != Talk::kNone && mt.pursueUntil > mt.clock && env.dist <= 7000.f)
		call = mt.talkMode == Talk::kToll ? Call::Parley : Call::Challenge;   // committed: the courier does not give up at the edge of sight

	switch (call) {
	case Call::Attack:
		Log::Line(std::string("[standing] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + " attacks: " + v.why + " (attitude " +
				  std::to_string(int(v.attitude)) + ", desire " + std::to_string(v.desire).substr(0, 4) + ")");
		TurnHostile(g);   // the fight opens with the squad's own combat barks
		return;

	case Call::Withdraw:
		mt.calmFor = 0.f;
		if (g.trust != Trust::BackingOff) {
			g.trust = Trust::BackingOff;
			SetFlee(g, true);
			Log::Line(std::string("[trust] ") + FactionId(g.faction) + " squad " + std::to_string(g.id) + " withdraws: " + v.why);
		}
		return;

	case Call::Challenge:
		mt.calmFor = 0.f;
		if (mt.challengeAt < 0.f) {
			mt.challengeAt = mt.clock;
			mt.talkMode = Talk::kChallenge;
			mt.pursueUntil = mt.clock + 45.f;
			mt.chaseStart = mt.clock;
			Speak(g, Talk::kChallenge);
			Log::Line(std::string("[standing] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + " challenges: " + v.why + " (attitude " +
					  std::to_string(int(v.attitude)) + ")");
		}
		approach(380.f);   // the travel package arrives within 256 of its marker: a marker 124 short of the player leaves the squad about talking distance out
		if (!mt.forced && env.dist > 700.f && mt.challengeAt >= 0.f) mt.challengeAt += 0.5f;   // still on the way: the player's time to answer has not started
		UnstickChase(g, mt, env, lead);
		if (!mt.forced && mt.chaseStart >= 0.f && mt.clock - mt.chaseStart > 90.f) { GiveUp(g, mt); return; }
		if (env.dist <= 560.f && !mt.forced) ForceGreet(g, mt, lead);
		else if (mt.forced && mt.talkMode == Talk::kChallenge && mt.clock - mt.forceAt > 6.f) mt.forced = false;   // it did not open: try again
		return;

	case Call::Parley: {
		mt.calmFor = 0.f;
		approach(380.f);
		if (!mt.parleyOffered) {
			mt.parleyOffered = true;
			mt.talkMode = Talk::kToll;
			mt.toll = Rep::S().TollFor(ctx);
			mt.talkAt = mt.clock;
			mt.pursueUntil = mt.clock + 45.f;
			mt.chaseStart = mt.clock;
			Speak(g, Talk::kToll);   // called out across the distance: "Don't make this hard..."
			mt.say2 = std::to_string(mt.toll) + " gold buys you the road.";
			mt.say2At = mt.clock + 3.5f;
			Log::Line(std::string("[standing] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + " stops the player and names a toll of " +
					  std::to_string(mt.toll) + " (desire " + std::to_string(v.desire).substr(0, 4) + ")");
		}
		UnstickChase(g, mt, env, lead);
		if (!mt.forced && mt.chaseStart >= 0.f && mt.clock - mt.chaseStart > 90.f) { GiveUp(g, mt); return; }
		if (!mt.forced && env.dist > 900.f) mt.talkAt = mt.clock;   // still on the way: the player's patience is not being tried yet
		if (env.dist <= 560.f && !mt.forced) ForceGreet(g, mt, lead);
		else if (mt.forced && mt.clock - mt.forceAt > 6.f && mt.after == After::None && Talk::ConversationSquad() != g.id) mt.forced = false;   // it did not open: try again
		if (mt.parleyOffered) {
			// the player may talk to them; if they are ignored for too long, or steel is drawn at the bargaining table, it ends badly
			const float waited = mt.clock - mt.talkAt;
			if (ctx.Has(PlayerFlag::Armed) && env.dist <= 900.f) mt.parleyRefused = true;
			else if (waited > 14.f && !mt.warned) { mt.warned = true; Talk::Subtitle(lead, "Last chance, friend."); }
			else if (waited > 24.f) mt.parleyRefused = true;
		}
		return;
	}

	case Call::Greet:
		if (!mt.greeted) {
			mt.greeted = true;
			Speak(g, Talk::kGreet);   // an ordinary hello in passing; the player may answer by talking to them
		}
		break;

	case Call::Assist:
		AssistPlayer(g);
		break;

	default: break;   // ignore / watch
	}

	// nothing to be tense about: ease off after a few quiet seconds, and forget the last meeting once the player is long gone
	mt.calmFor += 0.5f;
	if ((g.trust == Trust::Wary || g.trust == Trust::BackingOff) && mt.calmFor >= 4.f) Calm(g, mt);
	if (mt.challengeAt >= 0.f && mt.calmFor >= 4.f) mt.challengeAt = -1.f;
	if (env.dist > 4500.f) {
		mt.greeted = false;
		mt.parleyOffered = false;
		mt.parleyRefused = false;
		mt.warned = false;
	}
}

void OnPlayerAttacked(Group& g)
{
	if (!g.attackedByPlayer) Rep::OnPlayerStruck(g);   // first blow: provoked or not, and who saw it
	g.attackedByPlayer = true;
	if (g.online && g.trust != Trust::Hostile) TurnHostile(g);
}
}
