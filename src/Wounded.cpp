// Wounded: the Skyrim side of the wounded / help / finish / surrender model (docs/WOUNDED.md). The core (src/core/Wounds.cpp) decides
// who goes down, who helps, who finishes and who gives up; this file kneels and raises the actors, walks helpers over, runs the
// dialogue choices and saves. [U] until the M5d lab check: every engine call below (bleedout flags, BleedOutStart / BleedOutStop,
// SetLifeState) follows WOUNDED.md s1 but has not been seen in the game yet.
#include "ALife/Wounded.h"

#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Ltx.h"
#include "ALife/Rep.h"
#include "ALife/Settings.h"
#include "ALife/Systems.h"

namespace ALife::Wounded {
namespace {
	constexpr std::string_view kPlugin = "ALifeTalk.esp"sv;
	constexpr std::uint32_t kRec = 'WOND';
	constexpr float kTickSeconds = 0.25f;
	constexpr float kHelpArrive = 200.f;     // a helper this close patches the wounded up
	constexpr float kHelpGiveUp = 45.f;      // real seconds a helper may take to get there

	bool gReady{ false };    // attached to the populated board: the hook runs
	bool gLoaded{ false };   // wounded.ltx loaded and enabled (Init); Attach() hooks in once the world is populated
	Wounds gW;
	WoundConfig gBase;                 // wounded.ltx as loaded; gW.cfg = it scaled by the difficulty (T21 Mercy)
	float gMercy{ 1.f };
	float gDiffCheck{ 0 };
	RE::TESGlobal* gHeal{ nullptr };
	bool gRestore{ false };
	float gAcc{ 0 };
	Rng gRng{ 0x30DD };
	std::unordered_map<std::uint64_t, float> gHealth;            // member -> health share last tick (the crossing roll)
	std::unordered_set<RE::FormID> gKneeling;                    // actors we put into the kneel (their flags are ours to clear)
	struct Helper { std::uint64_t squad{ 0 }, helper{ 0 }; float seconds{ 0 }; };
	std::unordered_map<std::uint64_t, Helper> gHelpers;          // wounded member -> who is walking over
	struct Finisher { std::uint64_t squad{ 0 }, member{ 0 }; float left{ 0 }; };
	std::unordered_map<std::uint64_t, Finisher> gFinishers;      // wounded member -> the enemy about to finish it
	struct Pending { std::uint64_t squad; Talk::Choice c; RE::ActorHandle speaker; };
	std::vector<Pending> gPending;

	RE::FormID Hex(const std::string& s)
	{
		try { return RE::FormID(std::stoul(s, nullptr, 16)); } catch (...) { return 0; }
	}

	float Dist(const RE::NiPoint3& a, const RE::NiPoint3& b)
	{
		const float dx = a.x - b.x, dy = a.y - b.y;
		return std::sqrt(dx * dx + dy * dy);
	}

	RE::Actor* Live(const Member& m)
	{
		auto* a = m.alive() ? Pool::ActorOf(m) : nullptr;
		return a && !a->IsDead() && Pool::InWorld(a) ? a : nullptr;
	}

	Member* MemberById(Group& g, std::uint64_t id)
	{
		for (auto& m : g.members) if (m.id == id) return &m;
		return nullptr;
	}

	float HealthShare(RE::Actor* a)
	{
		auto* avo = a->AsActorValueOwner();
		const float max = std::max(1.f, avo->GetPermanentActorValue(RE::ActorValue::kHealth));
		return std::clamp(avo->GetActorValue(RE::ActorValue::kHealth) / max, 0.f, 1.f);
	}

	bool HostileToPlayer(const Group& g) { return g.trust == Trust::Hostile || Behavior::AttacksOnSight(g); }

	bool FightsPlayer(Group& g)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		for (auto& m : g.members)
			if (auto* a = Live(m); a && !gW.IsDown(m.id) && a->IsInCombat()) {
				auto t = a->GetActorRuntimeData().currentCombatTarget.get();
				if (t && t.get() == pc) return true;
			}
		return false;
	}

	bool SquadFighting(Group& g)
	{
		for (auto& m : g.members)
			if (auto* a = Live(m); a && !gW.IsDown(m.id) && a->IsInCombat()) return true;
		return false;
	}

	// ---------------------------------------------------------------- the kneel (WOUNDED.md s1)
	void Kneel(RE::Actor* a)
	{
		using F = RE::Actor::BOOL_FLAGS;
		auto& flags = a->GetActorRuntimeData().boolFlags;
		flags.set(F::kProtected, F::kNoBleedoutRecovery, F::kCanSpeakToEssentialDown);   // only the player can kill it; it stays down; it can talk
		if (a->IsInCombat()) a->StopCombat();
		if (auto* avo = a->AsActorValueOwner()) avo->SetBaseActorValue(RE::ActorValue::kAggression, 0.f);
		a->SetLifeState(RE::ACTOR_LIFE_STATE::kEssentialDown);
		a->NotifyAnimationGraph("BleedOutStart");
		gKneeling.insert(a->GetFormID());
	}

	void Rise(RE::Actor* a)
	{
		if (!a || !gKneeling.erase(a->GetFormID())) return;
		using F = RE::Actor::BOOL_FLAGS;
		a->GetActorRuntimeData().boolFlags.reset(F::kProtected, F::kNoBleedoutRecovery, F::kCanSpeakToEssentialDown);
		if (a->IsDead()) return;
		if (auto* avo = a->AsActorValueOwner()) avo->SetBaseActorValue(RE::ActorValue::kAggression, 1.f);
		a->SetLifeState(RE::ACTOR_LIFE_STATE::kAlive);
		a->NotifyAnimationGraph("BleedOutStop");
		a->EvaluatePackage(true, false);
	}

	// health on the actor after getting up: the share the core gave the member
	void SetHealth(RE::Actor* a, const Member& m)
	{
		auto* avo = a->AsActorValueOwner();
		const float max = std::max(1.f, avo->GetPermanentActorValue(RE::ActorValue::kHealth));
		const float want = max * std::clamp(m.health / std::max(1.f, m.healthMax), 0.f, 1.f);
		const float now = avo->GetActorValue(RE::ActorValue::kHealth);
		if (want > now) avo->RestoreActorValue(RE::ActorValue::kHealth, want - now);
	}

	void GetUp(Group& g, Member& m, float frac)
	{
		gW.GetUp(Sim::B(), g, m, frac);
		gHelpers.erase(m.id);
		gFinishers.erase(m.id);
		if (auto* a = Pool::ActorOf(m)) {
			Rise(a);
			if (!a->IsDead()) SetHealth(a, m);
		}
		gHealth[m.id] = std::clamp(m.health / std::max(1.f, m.healthMax), 0.f, 1.f);
	}

	bool TakePotion(RE::TESObjectREFR* from)
	{
		if (!from) return false;
		RE::TESBoundObject* best = nullptr;
		float bestValue = 1e9f;
		for (auto& [obj, data] : from->GetInventory()) {
			if (data.first <= 0 || !Economy::IsHealPotionItem(obj)) continue;
			const float v = float(obj->GetGoldValue());
			if (v < bestValue) { bestValue = v; best = obj; }   // the weakest one goes
		}
		if (!best) return false;
		from->RemoveItem(best, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		return true;
	}

	// gold and weapons of a member go to the player (the core zeroes its gold; the plugin moves what the actor carries)
	void Loot(RE::Actor* a)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!a || !pc) return;
		std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> take;
		for (auto& [obj, data] : a->GetInventory())
			if (data.first > 0 && (obj == Forms::R().gold || obj->Is(RE::FormType::Weapon))) take.emplace_back(obj, data.first);
		for (auto& [obj, n] : take) a->RemoveItem(obj, n, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, pc);
	}

	void Notify(const std::string& text)
	{
		RE::SendHUDMessage::ShowHUDMessage(text.c_str());
		Log::Line("[wound] " + text);
	}

	std::string Label(const Group& g) { return std::string(FactionId(g.faction)) + " squad #" + std::to_string(g.id); }

	// ---------------------------------------------------------------- per squad on screen
	void TickSquad(Group& g, float dt)
	{
		auto& b = Sim::B();
		auto* pc = RE::PlayerCharacter::GetSingleton();
		const auto pp = pc ? pc->GetPosition() : RE::NiPoint3{};
		const bool hostile = HostileToPlayer(g);
		for (auto& m : g.members) {
			auto* a = Live(m);
			if (!a) continue;
			const float share = HealthShare(a);
			if (gW.IsDown(m.id)) {
				if (!gKneeling.count(a->GetFormID())) Kneel(a);   // came on screen down (survivors, a load): kneel again
				if (a->IsInCombat()) a->StopCombat();
				m.health = std::max(1.f, m.health);
				// the armed player close by: a down member of a hostile squad may yield (rolled once)
				if (pc && hostile) {
					const bool armed = pc->AsActorState()->IsWeaponDrawn();
					if (gW.RollYield(b, g, m, Dist(a->GetPosition(), pp), armed, true, gRng)) {
						Talk::Subtitle(a, "Mercy! I yield!");
						Log::Line("[wound] " + m.name + " (" + Label(g) + ") yields");
					}
				}
				continue;
			}
			// going down: the share fell under the line since the last look (xr_wounded hp_state)
			auto it = gHealth.find(m.id);
			const float before = it == gHealth.end() ? 1.f : it->second;
			gHealth[m.id] = share;
			if (share > 0.f && share < gW.cfg.hp && before >= gW.cfg.hp && gW.RollDown(b, g, m, gRng)) {
				m.health = std::max(1.f, m.healthMax * share);
				Kneel(a);
				Log::Line("[wound] " + m.name + " (" + Label(g) + ") goes down at " + std::to_string(int(share * 100.f)) + "%");
			}
		}
		// a losing or outnumbered squad fighting the player may give up (rolled once per fight)
		if (hostile && FightsPlayer(g) && !gW.SquadYielded(g.id)) {
			const int side = 1 + Rep::Context().companions;
			if (gW.CheckSurrender(b, g, side, gRng)) {
				for (auto& m : g.members) if (auto* a = Live(m)) Kneel(a);
				if (auto* lead = [&]() -> RE::Actor* { for (auto& m : g.members) if (auto* a = Live(m)) return a; return nullptr; }())
					Talk::Subtitle(lead, "Enough! We yield, we yield!");
				Notify("The " + std::string(FactionName(g.faction)) + " surrender");
			}
		} else if (!FightsPlayer(g) && !gW.SquadYielded(g.id) && gW.fights.count(g.id)) {
			gW.EndFight(g.id);   // the fight is over: a new one rolls again
		}
		// a yielded squad the player walked away from gets up and backs off (no standing change)
		if (gW.SquadYielded(g.id) && pc) {
			bool inReach = false;
			for (auto& m : g.members) if (auto* a = Live(m); a && Dist(a->GetPosition(), pp) <= gW.cfg.releaseRadius) inReach = true;
			if (!inReach) {
				gW.Release(b, g);
				for (auto& m : g.members) if (auto* a = Pool::ActorOf(m)) { Rise(a); SetHealth(a, m); }
				Log::Line("[wound] " + Label(g) + " was left alone: it gets up and backs off");
			}
		}
		(void)dt;
	}

	// squadmates walk over to the down (xr_help_wounded): one helper per wounded, travel package to the wounded actor
	void TickHelp(float dt)
	{
		auto& b = Sim::B();
		const auto& r = Forms::R();
		for (auto& [mid, d] : gW.down) {
			if (d.Offline() || d.Yielded()) continue;
			Group* g = Sim::FindGroup(d.squad);
			Member* w = g && g->online ? MemberById(*g, mid) : nullptr;
			RE::Actor* wa = w ? Live(*w) : nullptr;
			if (!wa) continue;
			auto hit = gHelpers.find(mid);
			if (hit == gHelpers.end()) {
				if (SquadFighting(*g)) continue;
				std::vector<float> dist(g->members.size(), 1e9f);
				for (std::size_t i = 0; i < g->members.size(); ++i)
					if (auto* a = Live(g->members[i]); a && a != wa) dist[i] = Dist(a->GetPosition(), wa->GetPosition());
				const int k = gW.ChooseHelper(*g, dist, false);
				if (k < 0) continue;
				auto* ha = Live(g->members[std::size_t(k)]);
				if (!ha || !r.kwTravel) continue;
				gHelpers[mid] = { g->id, g->members[std::size_t(k)].id, 0.f };
				ha->extraList.SetLinkedRef(wa, r.kwTravel);
				Online::SetActorMode(ha, 1);
				ha->EvaluatePackage(true, false);
				Log::Line("[wound] " + g->members[std::size_t(k)].name + " goes to help " + w->name);
				continue;
			}
			Member* h = MemberById(*g, hit->second.helper);
			RE::Actor* ha = h && b.Stands(*h) ? Live(*h) : nullptr;
			hit->second.seconds += dt;
			if (!ha || hit->second.seconds > kHelpGiveUp || SquadFighting(*g)) { gHelpers.erase(hit); Online::Resume(*g); continue; }
			if (Dist(ha->GetPosition(), wa->GetPosition()) > kHelpArrive) continue;
			const bool potion = TakePotion(ha);
			gHelpers.erase(hit);
			gW.Help(b, *g, *w, *h, potion, gRng);
			Rise(wa);
			SetHealth(wa, *w);
			Talk::Subtitle(ha, potion ? "Here, drink this. Up you get." : "Hold still... there. You'll live.");
			Log::Line("[wound] " + h->name + " helped " + w->name + (potion ? " (potion)" : ""));
			Online::Resume(*g);
			break;   // the map changed
		}
	}

	// enemies finish the down (xrs_kill_wounded): an enemy standing within finish_radius that fights nobody, after finish_delay
	void TickFinish(float dt)
	{
		const auto& cfg = Sim::Cfg();
		std::vector<std::uint64_t> drop;
		std::vector<std::pair<RE::Actor*, RE::Actor*>> kills;   // after the loop: the death event changes the down map
		for (auto& [mid, d] : gW.down) {
			if (d.Offline()) continue;
			Group* g = Sim::FindGroup(d.squad);
			Member* w = g && g->online ? MemberById(*g, mid) : nullptr;
			RE::Actor* wa = w ? Live(*w) : nullptr;
			if (!wa) continue;
			auto fit = gFinishers.find(mid);
			if (fit != gFinishers.end()) {
				Group* fg = Sim::FindGroup(fit->second.squad);
				Member* fm = fg ? MemberById(*fg, fit->second.member) : nullptr;
				RE::Actor* fa = fm && !gW.IsDown(fm->id) ? Live(*fm) : nullptr;
				if (!fa || !gW.MayFinish(Dist(fa->GetPosition(), wa->GetPosition()), fa->IsInCombat())) { drop.push_back(mid); continue; }
				if ((fit->second.left -= dt) > 0.f) continue;
				Log::Line("[wound] " + fm->name + " (" + Label(*fg) + ") finishes " + w->name);
				drop.push_back(mid);
				kills.emplace_back(wa, fa);
				continue;
			}
			for (auto& fg : Sim::Groups()) {
				if (!fg.online || &fg == g || !cfg.IsEnemy(fg.faction, g->faction)) continue;
				for (auto& fm : fg.members) {
					auto* fa = gW.IsDown(fm.id) ? nullptr : Live(fm);
					if (!fa || !gW.MayFinish(Dist(fa->GetPosition(), wa->GetPosition()), fa->IsInCombat())) continue;
					gFinishers[mid] = { fg.id, fm.id, gW.FinishDelay(gRng) };
					break;
				}
				if (gFinishers.count(mid)) break;
			}
		}
		for (auto id : drop) gFinishers.erase(id);
		// the death event credits the kill (Sim::OnActorKilled); protected actors only die to the player, so lift the flag first
		for (auto& [victim, by] : kills) { Rise(victim); victim->KillImpl(by, 9999.f, true, false); }
	}

	void ApplyEvents(const std::vector<WoundEvent>& ev)
	{
		auto& b = Sim::B();
		for (auto& e : ev) {
			Group* g = Sim::FindGroup(e.squad);
			Member* m = g ? MemberById(*g, e.member) : nullptr;
			switch (e.kind) {
			case WoundEvent::Kind::Call:
				if (auto* a = m ? Live(*m) : nullptr) {
					std::string line = Talk::OpeningLine(*g, Talk::kWounded);
					Talk::Subtitle(a, line.empty() ? "Help... someone, please..." : line);
					if (e.by) Notify(m->name + " (" + FactionName(g->faction) + ") is wounded and calls for help");
				}
				break;
			case WoundEvent::Kind::SelfHeal:   // xr_wounded autoheal: its own potion, or it gets up hurt
				if (m && g) {
					auto* a = Pool::ActorOf(*m);
					const bool potion = a && TakePotion(a);
					GetUp(*g, *m, potion ? 1.f : gW.cfg.reviveHp);
					Log::Line("[wound] " + m->name + " gets up on its own" + (potion ? " (potion)" : ""));
				}
				break;
			case WoundEvent::Kind::Released:
				if (g) {
					for (auto& x : g->members) if (auto* a = Pool::ActorOf(x)) { Rise(a); if (!a->IsDead()) SetHealth(a, x); }
					Log::Line("[wound] " + Label(*g) + " gets up and backs off (nobody decided)");
				}
				break;
			default:
				break;
			}
		}
		(void)b;
	}

	void RunChoices()
	{
		if (gPending.empty() || Talk::InConversation()) return;
		auto pending = std::move(gPending);
		gPending.clear();
		auto& b = Sim::B();
		auto* pc = RE::PlayerCharacter::GetSingleton();
		for (auto& p : pending) {
			Group* g = Sim::FindGroup(p.squad);
			auto sp = p.speaker.get();
			Member* m = nullptr;
			if (sp && g) Sim::GroupOfActor(sp.get(), &m);
			if (!g || !m || !gW.IsDown(m->id)) continue;
			const bool hostile = HostileToPlayer(*g);
			auto* a = sp.get();
			switch (p.c) {
			case Talk::Choice::Heal:
				if (!TakePotion(pc)) { Notify("You have no healing potion"); break; }
				gW.Choose(b, Rep::S(), *g, *m, WoundChoice::Heal, hostile, true, Sim::Hours());
				Rise(a);
				SetHealth(a, *m);
				Log::Line("[wound] the player healed " + m->name);
				break;
			case Talk::Choice::WRob:
				Loot(a);
				gW.Choose(b, Rep::S(), *g, *m, WoundChoice::Rob, hostile, true, Sim::Hours());
				break;
			case Talk::Choice::Finish:
			case Talk::Choice::NoMercy:
				// the same as striking it: the death event applies the kill and the finish (or prisoner) deed
				Rise(a);
				if (pc) a->KillImpl(pc, 9999.f, true, false);
				break;
			case Talk::Choice::Spare:
			case Talk::Choice::RobGo:
				if (p.c == Talk::Choice::RobGo)
					for (auto& x : g->members)
						if (const Down* d = gW.Find(x.id); d && d->Yielded()) Loot(Pool::ActorOf(x));
				gW.Choose(b, Rep::S(), *g, *m, p.c == Talk::Choice::Spare ? WoundChoice::Spare : WoundChoice::RobAndGo, hostile, true, Sim::Hours());
				for (auto& x : g->members) if (auto* xa = Pool::ActorOf(x); xa && !xa->IsDead()) { Rise(xa); SetHealth(xa, x); }
				Log::Line("[wound] the player let " + Label(*g) + " go" + (p.c == Talk::Choice::RobGo ? " (robbed)" : ""));
				break;
			default:
				break;
			}
		}
	}
}

bool Ready() { return gReady; }
Wounds& W() { return gW; }
bool IsDown(std::uint64_t id) { return gReady && gW.IsDown(id); }

void Init()
{
	gReady = gLoaded = false;
	gHeal = nullptr;
	Ltx wl;
	std::string err;
	if (!wl.LoadFile(Settings::ConfigDir() + "/wounded.ltx", &err)) { Log::Line("[wound] wounded.ltx missing: nobody goes down (" + err + ")"); return; }
	WoundConfig c;
	if (!LoadWoundConfig(wl, c, err)) { Log::Line("[wound] wounded.ltx: " + err); return; }
	gBase = c;
	gMercy = Sim::Cfg().Diff(DiffChannel::Mercy);
	gW.cfg = ScaleWoundConfig(gBase, WoundScale{ .mercy = gMercy });   // the one read path (docs/WOUNDED.md s8)
	if (!c.enabled) { Log::Line("[wound] disabled in wounded.ltx"); return; }
	if (auto* dh = RE::TESDataHandler::GetSingleton(); dh && dh->LookupModByName(kPlugin)) {
		Ltx talk;
		if (talk.LoadFile(Settings::ConfigDir() + "/talk.ltx", nullptr))
			if (auto* f = dh->LookupForm(Hex(talk.Get("globals", "wound_heal", "0")), kPlugin)) gHeal = f->As<RE::TESGlobal>();
	}
	gLoaded = true;
	Log::Line(std::string("[wound] loaded: down under ") + std::to_string(int(c.hp * 100.f)) + "% with " + std::to_string(int(c.chance * 100.f)) + "%, offline " +
			  std::to_string(int(c.ocsChance * 100.f)) + "%; dialogue " + (gHeal ? "ok" : "without ALifeWoundHeal") + "; attaches once the world is populated");
	Attach();   // the board exists here but is not populated yet (Sim::MainTick attaches it)
}

void Attach()
{
	if (!gLoaded || !Sim::BoardReady()) return;
	auto& b = Sim::B();
	if (gW.Attach(b, true))
		Log::Line("[wound] ready: hooked into the board (" + std::to_string(b.squads.size()) + " squads); down under " + std::to_string(int(gW.cfg.hp * 100.f)) +
				  "%, offline " + std::to_string(int(gW.cfg.ocsChance * 100.f)) + "%");
	gReady = gW.AttachedTo(b);
}

void Reset()
{
	for (auto id : gKneeling)
		if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) {
			using F = RE::Actor::BOOL_FLAGS;
			a->GetActorRuntimeData().boolFlags.reset(F::kProtected, F::kNoBleedoutRecovery, F::kCanSpeakToEssentialDown);
		}
	gKneeling.clear();
	gW.Reset();
	gHealth.clear();
	gHelpers.clear();
	gFinishers.clear();
	gPending.clear();
	gRestore = false;
	if (gHeal) gHeal->value = 0.f;
}

int ModeFor(const Group&, const Member& m)
{
	const Down* d = gReady ? gW.Find(m.id) : nullptr;
	if (!d) return Talk::kNone;
	return d->Yielded() ? Talk::kYielded : Talk::kWounded;
}

void Publish()
{
	if (!gHeal) return;
	int has = 0;
	if (auto* pc = RE::PlayerCharacter::GetSingleton())
		for (auto& [obj, data] : pc->GetInventory())
			if (data.first > 0 && Economy::IsHealPotionItem(obj)) { has = 1; break; }
	gHeal->value = float(has);
}

void OnChoice(std::uint64_t squad, Talk::Choice c, RE::Actor* speaker)
{
	gPending.push_back({ squad, c, speaker ? speaker->GetHandle() : RE::ActorHandle{} });
}

void Tick(float dt)
{
	if (!gReady || !Sim::BoardReady()) return;
	if (gRestore) {
		gRestore = false;
		gW.Rebind(Sim::B());
		Log::Line("[wound] restored " + std::to_string(gW.down.size()) + " down members");
	}
	RunChoices();
	if ((gDiffCheck += dt) >= 2.f) {   // the game difficulty or the MCM Mercy slider moved: rescale from the file's values
		gDiffCheck = 0.f;
		if (const float mercy = Sim::Cfg().Diff(DiffChannel::Mercy); mercy != gMercy) {
			gMercy = mercy;
			gW.cfg = ScaleWoundConfig(gBase, WoundScale{ .mercy = mercy });
			Log::Line("[wound] mercy x" + std::to_string(mercy) + ": down " + std::to_string(int(gW.cfg.chance * 100.f)) + "%, offline " +
					  std::to_string(int(gW.cfg.ocsChance * 100.f)) + "%");
		}
	}
	if ((gAcc += dt) < kTickSeconds) return;
	const float step = gAcc;
	gAcc = 0.f;
	if (Talk::InConversation()) return;   // the kneel and the timers wait while the player talks to someone
	auto* pc = RE::PlayerCharacter::GetSingleton();
	const auto pp = pc ? pc->GetPosition() : RE::NiPoint3{};
	std::unordered_set<std::uint64_t> playerClose;
	for (auto& g : Sim::Groups()) {
		if (!g.online || !IsHumanFaction(g.faction)) continue;
		TickSquad(g, step);
		for (auto& m : g.members)
			if (auto* a = Live(m); a && Dist(a->GetPosition(), pp) <= gW.cfg.callRadius) { playerClose.insert(g.id); break; }
	}
	std::vector<WoundEvent> ev;
	gW.UpdateOnline(Sim::B(), step, playerClose, ev);
	ApplyEvents(ev);
	TickHelp(step);
	TickFinish(step);
}

void OnMemberDied(Group& g, Member& m, RE::Actor* killer)
{
	if (auto* a = Pool::ActorOf(m)) Rise(a);
	gHealth.erase(m.id);
	gHelpers.erase(m.id);
	gFinishers.erase(m.id);
	const Down* d = gW.Find(m.id);
	if (!d) return;
	if (killer && killer->IsPlayerRef()) {   // striking a kneeling member finishes it (WOUNDED.md s3)
		const auto& c = gW.cfg;
		if (d->Yielded()) Rep::S().OnPrisonerKilled(g, c.prisoner, c.prisonerNotoriety, Sim::Hours());
		else Rep::S().OnFinish(g, c.finishDeed, Sim::Hours());
		Log::Line(std::string("[wound] the player ") + (d->Yielded() ? "killed a prisoner: " : "finished ") + m.name + " (" + Label(g) + ")");
	}
	gW.Forget(m.id);
}

void OnSendOffline(Group& g)
{
	for (auto& m : g.members) {
		if (auto* a = Pool::ActorOf(m)) Rise(a);
		gHealth.erase(m.id);
		gHelpers.erase(m.id);
		gFinishers.erase(m.id);
		if (m.alive() && gW.IsDown(m.id)) m.health = std::max(1.f, m.health);   // the kneel read back as health 0 would be a death
	}
}

// ---------------------------------------------------------------- co-save
void SaveTo(SKSE::SerializationInterface* a)
{
	std::vector<std::uint8_t> b;
	gW.Save(b);
	if (!a->WriteRecord(kRec, Wounds::kSaveVersion, b.data(), std::uint32_t(b.size()))) Log::Line("[wound] FAILED to write the co-save record");
	else Log::Line("[wound] saved " + std::to_string(gW.down.size()) + " down members");
}

void LoadFrom(SKSE::SerializationInterface* a, std::uint32_t ver, std::uint32_t len)
{
	std::vector<std::uint8_t> b(len);
	if (a->ReadRecordData(b.data(), len) != len) return;
	if (!gW.Load(b.data(), b.size(), ver)) { Log::Line("[wound] co-save record v" + std::to_string(ver) + " skipped: nobody down"); return; }
	gRestore = true;
	Log::Line("[wound] loaded " + std::to_string(gW.down.size()) + " down members");
}

// ---------------------------------------------------------------- debug bridge
void Debug(const std::string& a_args)
{
	std::lock_guard l(Sim::Lock());
	if (!gReady || !Sim::BoardReady()) { Log::Line("[wound] debug: not ready"); return; }
	auto& b = Sim::B();
	const auto parts = SplitList(a_args, ' ');
	const std::string verb = parts.empty() ? "list" : parts[0];
	const std::string arg = parts.size() > 1 ? parts[1] : std::string();
	Group* scen = Sim::DebugScenarioGroup() ? Sim::FindGroup(Sim::DebugScenarioGroup()) : nullptr;
	auto* pc = RE::PlayerCharacter::GetSingleton();
	auto nearest = [&](bool yieldedOnly, Group** og) -> Member* {   // the nearest down member on screen
		Member* best = nullptr;
		float bd = 1e30f;
		for (auto& g : Sim::Groups())
			for (auto& m : g.members) {
				const Down* d = gW.Find(m.id);
				auto* a = d && (!yieldedOnly || d->Yielded()) ? Live(m) : nullptr;
				if (!a || !pc) continue;
				const float dd = Dist(a->GetPosition(), pc->GetPosition());
				if (dd < bd) { bd = dd; best = &m; *og = &g; }
			}
		return best;
	};
	if (verb == "list") {
		for (auto& [id, d] : gW.down) {
			Group* g = Sim::FindGroup(d.squad);
			Member* m = g ? MemberById(*g, id) : nullptr;
			Log::Line("[wound] " + (m ? m->name : "member " + std::to_string(id)) + " of " + (g ? Label(*g) : "squad " + std::to_string(d.squad)) +
					  (d.Yielded() ? " yielded" : " down") + (d.Offline() ? " offline, up at hour " + std::to_string(int(d.upAt)) : " on screen") +
					  ", since hour " + std::to_string(int(d.since)) + (gHelpers.count(id) ? ", helper on the way" : "") + (gFinishers.count(id) ? ", about to be finished" : ""));
		}
		const auto& c = gW.counters;
		Log::Line("[wound] " + std::to_string(gW.down.size()) + " down; counters: downed " + std::to_string(c.downed) + " (offline " + std::to_string(c.ocsDowned) +
				  "), helped " + std::to_string(c.helped) + ", finished " + std::to_string(c.finished) + ", survivors " + std::to_string(c.survivors) +
				  ", yielded " + std::to_string(c.yielded) + ", surrendered " + std::to_string(c.surrendered));
		return;
	}
	if (verb == "down") {   // the scenario squad's n-th member goes down now
		if (!scen) { Log::Line("[wound] debug: no scenario squad"); return; }
		const std::size_t n = std::size_t(std::max(0, std::atoi(arg.c_str())));
		if (n >= scen->members.size()) { Log::Line("[wound] debug: no member " + arg); return; }
		Member& m = scen->members[n];
		gW.GoDown(b, *scen, m, !scen->online);
		if (auto* a = Live(m)) Kneel(a);
		Log::Line("[wound] debug: " + m.name + " is down");
		return;
	}
	if (verb == "yield" || verb == "check") {   // the scenario squad surrenders now / the surrender check with chance 1
		if (!scen) { Log::Line("[wound] debug: no scenario squad"); return; }
		gW.EndFight(scen->id);
		const bool ok = verb == "yield" ? gW.CheckSurrender(b, *scen, 1000, gRng, 1.f) : gW.CheckSurrender(b, *scen, 1 + Rep::Context().companions, gRng, 1.f);
		if (ok) for (auto& m : scen->members) if (auto* a = Live(m)) Kneel(a);
		Log::Line("[wound] debug: " + Label(*scen) + (ok ? " yielded" : " did not yield (not losing or outnumbered)"));
		return;
	}
	if (verb == "help" || verb == "rob" || verb == "finish" || verb == "spare") {   // the player's choice on the nearest down member, no dialogue
		Group* g = nullptr;
		Member* m = nearest(verb == "spare", &g);
		auto* a = m ? Live(*m) : nullptr;
		if (!a) { Log::Line("[wound] debug: nobody down on screen"); return; }
		const Talk::Choice c = verb == "help" ? Talk::Choice::Heal : verb == "rob" ? Talk::Choice::WRob : verb == "finish" ? Talk::Choice::Finish : Talk::Choice::Spare;
		gPending.push_back({ g->id, c, a->GetHandle() });
		RunChoices();
		return;
	}
	if (verb == "call") {   // the nearest down member calls now
		Group* g = nullptr;
		Member* m = nearest(false, &g);
		if (!m) { Log::Line("[wound] debug: nobody down on screen"); return; }
		ApplyEvents({ WoundEvent{ WoundEvent::Kind::Call, g->id, m->id, 1 } });
		return;
	}
	if (verb == "ocs") {   // one offline fight between the scenario squad and its nearest offline enemy, every lethal hit leaves the member down
		if (!scen || scen->online) { Log::Line("[wound] debug: needs the scenario squad off screen"); return; }
		const float keep = gW.cfg.ocsChance;
		gW.cfg.ocsChance = 1.f;
		const auto before = b.stats.battles;
		for (int i = 0; i < 6 && b.stats.battles == before; ++i) b.Step(b.Cfg().stepHours);
		gW.cfg.ocsChance = keep;
		Log::Line("[wound] debug: " + std::to_string(b.stats.battles - before) + " offline fight(s); down now " + std::to_string(gW.down.size()));
		return;
	}
	Log::Line("[wound] debug: unknown '" + a_args + "' (list, down [n], yield, check, help, rob, finish, spare, call, ocs)");
}
}
