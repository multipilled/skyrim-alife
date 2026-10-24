// Companions: the Skyrim side of the party model (docs/COMPANIONS.md). The core (src/core/Party.cpp) decides who joins and for how
// long; this file publishes the dialogue globals, keeps companion squads on screen beside the player and carries out the orders.
#include "ALife/Companions.h"

#include "ALife/Forms.h"
#include "ALife/Jobs.h"
#include "ALife/Log.h"
#include "ALife/Ltx.h"
#include "ALife/Rep.h"
#include "ALife/Settings.h"
#include "ALife/Systems.h"

namespace ALife::Companions {
namespace {
	constexpr std::string_view kPlugin = "ALifeTalk.esp"sv;
	constexpr std::uint32_t kRec = 'COMP';
	constexpr float kTeleportAfter = 3.f;    // real seconds out of reach before a following member is brought over
	constexpr float kArrive = 320.f;         // "go up ahead": the leader is there

	bool gReady{ false };
	Party gP;
	RE::TESGlobal* gOffer{ nullptr };
	RE::TESGlobal* gState{ nullptr };
	RE::TESGlobal* gHire1{ nullptr };
	RE::TESGlobal* gHire2{ nullptr };
	RE::TESGlobal* gFire{ nullptr };
	RE::TESGlobal* gLoot{ nullptr };
	struct Pending { std::uint64_t squad; Talk::Choice c; RE::ActorHandle speaker; };
	std::vector<Pending> gPending;
	struct Driven
	{
		Order order{ Order::Follow };
		bool holdFire{ false };
		std::vector<RE::FormID> actors;   // the actors the packages were set on (a re-placement gives new ones)
	};
	std::unordered_map<std::uint64_t, Driven> gDriven;
	std::unordered_map<RE::FormID, float> gAway;          // member actor -> real seconds it has been out of reach
	std::unordered_map<std::uint64_t, bool> gLooting;     // squad -> busy at a body last tick
	bool gRestore{ false };
	float gUpdate{ 0 }, gDrive{ 0 };
	Rng gRng{ 0xC0A1 };

	RE::FormID Hex(const std::string& s)
	{
		try { return RE::FormID(std::stoul(s, nullptr, 16)); } catch (...) { return 0; }
	}

	template <class... Args>
	void Papyrus(const char* a_fn, Args... a_args)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) return;
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
		std::unique_ptr<RE::BSScript::IFunctionArguments> args{ RE::MakeFunctionArguments(std::move(a_args)...) };
		vm->DispatchStaticCall("ALife_Companions", a_fn, args.get(), cb);
	}

	void Notify(const std::string& a_text)
	{
		RE::SendHUDMessage::ShowHUDMessage(a_text.c_str());
		Log::Line("[comp] " + a_text);
	}

	std::string Label(const Group& g) { return std::string(FactionId(g.faction)) + " squad #" + std::to_string(g.id); }

	// where the player stands, for a dismissal (Party::Dismiss: a squad dismissed in another worldspace settles there)
	PartyPlace PlayerPlace()
	{
		PartyPlace at;
		if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
			at.ws = Forms::WsIndex(pc->GetWorldspace());   // -1 in an interior: the squad keeps its own worldspace
			const auto pos = pc->GetPosition();
			at.x = pos.x; at.y = pos.y;
		}
		return at;
	}

	RE::Actor* LeaderActor(Group& g)
	{
		for (auto& m : g.members)
			if (auto* a = m.alive() ? Pool::ActorOf(m) : nullptr; a && !a->IsDead()) return a;
		return nullptr;
	}

	std::vector<std::pair<Member*, RE::Actor*>> Living(Group& g)
	{
		std::vector<std::pair<Member*, RE::Actor*>> out;
		for (auto& m : g.members)
			if (auto* a = m.alive() ? Pool::ActorOf(m) : nullptr; a && !a->IsDead()) out.emplace_back(&m, a);
		return out;
	}

	void SetAggression(RE::Actor* a, bool hold)
	{
		if (auto* avo = a->AsActorValueOwner()) avo->SetBaseActorValue(RE::ActorValue::kAggression, hold ? 0.f : 1.f);
	}

	bool HeldByJob(const Group& g) { return Jobs::Holds(g.id) || (Sim::BoardReady() && Sim::B().pinned.count(g.id)); }

	CompOffer OfferOf(const Group& g)
	{
		CompOffer o = gP.OfferFor(g, Rep::RegardOf(g), HeldByJob(g), gRng);
		if ((o == CompOffer::Free || o == CompOffer::Paid) && !gP.Room(Sim::B(), g)) o = CompOffer::NoRoom;
		return o;
	}

	// ---------------------------------------------------------------- driving (packages: 1 travel to the slot marker, 2 sandbox around it, 3 follow the linked leader)
	void Follow(Group& g)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		const auto& r = Forms::R();
		for (auto& [m, a] : Living(g)) {
			a->extraList.SetLinkedRef(pc, r.kwLeader);
			Online::SetActorMode(a, 3);
			a->EvaluatePackage(true, false);
		}
	}

	// every member sandboxes around the slot marker (put at the leader, or where the squad was sent)
	void HoldAt(Group& g, bool a_atLeader)
	{
		const auto& r = Forms::R();
		auto* lead = LeaderActor(g);
		if (!lead || g.slot < 0 || !r.slotMarkers[g.slot]) return;
		auto* mk = r.slotMarkers[g.slot];
		if (a_atLeader) mk->MoveTo(lead);
		for (auto& [m, a] : Living(g)) {
			a->extraList.SetLinkedRef(mk, r.kwTravel);
			Online::SetActorMode(a, 2);
			a->EvaluatePackage(true, false);
		}
		if (Companion* c = gP.Find(g.id)) {   // the wait point, for the co-save
			const auto p = mk->GetPosition();
			auto* cell = mk->GetParentCell();
			c->cell = cell ? cell->GetFormID() : 0;
			c->ws = mk->GetWorldspace() ? mk->GetWorldspace()->GetFormID() : 0;
			c->wx = p.x; c->wy = p.y; c->wz = p.z;
		}
	}

	// the leader walks to the slot marker, the others follow the leader
	void MoveTo(Group& g)
	{
		const auto& r = Forms::R();
		auto* lead = LeaderActor(g);
		if (!lead || g.slot < 0 || !r.slotMarkers[g.slot]) return;
		for (auto& [m, a] : Living(g)) {
			if (a == lead) { a->extraList.SetLinkedRef(r.slotMarkers[g.slot], r.kwTravel); Online::SetActorMode(a, 1); }
			else { a->extraList.SetLinkedRef(lead, r.kwLeader); Online::SetActorMode(a, 3); }
			a->EvaluatePackage(true, false);
		}
	}

	void Apply(Group& g, Companion& c)
	{
		g.trust = Trust::Neutral;
		for (auto& [m, a] : Living(g)) {
			SetAggression(a, c.holdFire);
			if (c.holdFire && a->IsInCombat() && Pool::InWorld(a)) a->StopCombat();
			if (!a->IsPlayerTeammate()) Papyrus("Team", a, true);
			if (auto* pe = Forms::R().fPlayerEnemy; pe && a->IsInFaction(pe)) a->RemoveFromFaction(pe);
			Pool::Tie(*m);   // one face for as long as it travels with the player (and after)
		}
		switch (c.order) {
		case Order::Follow: Follow(g); break;
		case Order::Wait: HoldAt(g, false); break;
		case Order::Move: MoveTo(g); break;
		}
		auto& d = gDriven[g.id];
		d.order = c.order;
		d.holdFire = c.holdFire;
		d.actors.clear();
		for (auto& [m, a] : Living(g)) d.actors.push_back(a->GetFormID());
	}

	bool NeedsApply(Group& g, const Companion& c)
	{
		auto it = gDriven.find(g.id);
		if (it == gDriven.end() || it->second.order != c.order || it->second.holdFire != c.holdFire) return true;
		std::vector<RE::FormID> now;
		for (auto& [m, a] : Living(g)) now.push_back(a->GetFormID());
		return now != it->second.actors;
	}

	// a following member that is out of reach (far, another cell across a door, another worldspace) is brought to the player
	void Teleport(Group& g, float dt)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || pc->IsInCombat()) return;
		if (auto* ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) return;
		auto* pcell = pc->GetParentCell();
		if (!pcell || !pcell->IsAttached()) return;
		const auto pp = pc->GetPosition();
		const bool pInt = pcell->IsInteriorCell();
		for (auto& [m, a] : Living(g)) {
			auto* acell = a->GetParentCell();
			const auto ap = a->GetPosition();
			const float d = std::sqrt((ap.x - pp.x) * (ap.x - pp.x) + (ap.y - pp.y) * (ap.y - pp.y));
			const bool otherPlace = !acell || a->GetWorldspace() != pc->GetWorldspace() || ((pInt || acell->IsInteriorCell()) && acell != pcell);
			const bool away = otherPlace || d > gP.cfg.teleportDistance;
			float& t = gAway[a->GetFormID()];
			if (!away || a->IsInCombat()) { t = 0.f; continue; }
			if ((t += dt) < kTeleportAfter) continue;
			t = 0.f;
			a->MoveTo(pc);   // Anomaly on_level_changing: TeleportSquad to the actor
			const float yaw = pc->GetAngleZ();
			a->SetPosition({ pp.x - std::sin(yaw) * 150.f, pp.y - std::cos(yaw) * 150.f, pp.z + 10.f }, true);
			a->EvaluatePackage(true, false);
			Log::Line("[comp] " + Label(g) + ": " + m->name + " was " + (otherPlace ? "in another place" : std::to_string(int(d)) + " units away") +
					  " and is brought to the player");
		}
	}

	// "go up ahead and hold there": a point beyond the speaker, on the line from the player through it
	void SendAhead(Group& g, RE::Actor* speaker)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		const auto& r = Forms::R();
		if (!pc || g.slot < 0 || !r.slotMarkers[g.slot]) return;
		RE::Actor* from = speaker && !speaker->IsDead() ? speaker : LeaderActor(g);
		if (!from) return;
		const auto pp = pc->GetPosition(), sp = from->GetPosition();
		float dx = sp.x - pp.x, dy = sp.y - pp.y;
		const float d = std::max(1.f, std::sqrt(dx * dx + dy * dy));
		dx /= d; dy /= d;
		RE::NiPoint3 to{ sp.x + dx * gP.cfg.moveDistance, sp.y + dy * gP.cfg.moveDistance, sp.z };
		RE::TESObjectCELL* cell = from->GetParentCell();
		RE::NiPoint3 snapped;
		RE::TESObjectCELL* scell = nullptr;
		if (Online::SnapPoint(to, 900.f, snapped, scell)) { to = snapped; cell = scell; }
		Online::MoveRef(r.slotMarkers[g.slot], cell, from->GetWorldspace(), to);
		Log::Line("[comp] " + Label(g) + " moves up to " + std::to_string(int(to.x)) + "," + std::to_string(int(to.y)) + " (" +
				  std::to_string(int(std::sqrt((to.x - pp.x) * (to.x - pp.x) + (to.y - pp.y) * (to.y - pp.y)))) + " from the player)");
	}

	void ApplyEvents(const std::vector<PartyEvent>& ev)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		for (auto& e : ev) {
			Group* g = Sim::FindGroup(e.squad);
			switch (e.kind) {
			case PartyEvent::Kind::Joined:
				if (e.gold > 0 && pc && Forms::R().gold) pc->RemoveItem(Forms::R().gold, e.gold, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				if (g) for (auto& m : g->members) if (m.alive()) { m.gold += e.gold; break; }
				Notify(e.gold > 0 ? e.name + "'s band is hired for " + std::to_string(e.gold) + " gold" : e.name + "'s band travels with you");
				if (g) Log::Line("[comp] " + Label(*g) + " joined: " + std::to_string(g->livingCount()) + " members, " + (e.gold > 0 ? "paid until hour " + std::to_string(int(e.until)) : "free") +
								 "; companions now " + std::to_string(gP.LivingCompanions(Sim::B())) + " in " + std::to_string(gP.list.size()) + " squads");
				break;
			case PartyEvent::Kind::Warned:
				Notify(e.name + "'s contract ends within the hour");
				break;
			case PartyEvent::Kind::Left:
			case PartyEvent::Kind::Expired:
			case PartyEvent::Kind::Turned:
				Notify(e.kind == PartyEvent::Kind::Expired ? e.name + "'s contract has run out: the band goes its own way"
					   : e.kind == PartyEvent::Kind::Turned ? e.name + "'s band no longer trusts you and leaves" : e.name + "'s band goes its own way");
				if (g) {
					for (auto& [m, a] : Living(*g)) { Unteam(a); SetAggression(a, true); if (a->IsInCombat() && Pool::InWorld(a)) a->StopCombat(); }
					gDriven.erase(g->id);
					gLooting.erase(g->id);
					if (g->online) Online::Resume(*g);   // back to what the board plans (it picks a target on its next step)
					Log::Line("[comp] " + Label(*g) + " left (" + (e.kind == PartyEvent::Kind::Expired ? "expired" : e.kind == PartyEvent::Kind::Turned ? "turned" : "dismissed") +
							  ") and rejoins the simulation in worldspace " + std::to_string(int(g->ws)) + " at node " + std::to_string(g->node));
				}
				break;
			case PartyEvent::Kind::Lost:
				Notify(e.name + "'s band fell while travelling with you");
				gDriven.erase(e.squad);
				gLooting.erase(e.squad);
				break;
			}
		}
	}

	bool PlaceNearPlayer(Group& g)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) return false;
		auto* cell = pc->GetParentCell();
		if (!cell || !cell->IsAttached()) return false;
		const auto pp = pc->GetPosition();
		const float yaw = pc->GetAngleZ();
		RE::NiPoint3 at{ pp.x - std::sin(yaw) * 200.f, pp.y - std::cos(yaw) * 200.f, pp.z + 10.f };
		if (Online::AtBudget()) Online::MakeRoom();   // the world's squads give way to the player's companions (the patrol-mod budget counts them)
		return Online::PlaceAt(g, cell, pc->GetWorldspace(), at);
	}

	bool PlaceAtWaitPoint(Group& g, const Companion& c)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !c.cell) return false;
		auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(c.cell);
		if (!cell) return false;
		auto* pcell = pc->GetParentCell();
		if (cell->IsInteriorCell()) {
			if (cell != pcell) return false;
		} else {
			auto* ws = pc->GetWorldspace();
			if (!ws || ws->GetFormID() != c.ws) return false;
			const auto pp = pc->GetPosition();
			if ((pp.x - c.wx) * (pp.x - c.wx) + (pp.y - c.wy) * (pp.y - c.wy) > gP.cfg.waitPlaceDistance * gP.cfg.waitPlaceDistance) return false;
		}
		return Online::PlaceAt(g, cell, cell->IsInteriorCell() ? nullptr : pc->GetWorldspace(), { c.wx, c.wy, c.wz });
	}

	void RunChoices()
	{
		if (gPending.empty() || Talk::InConversation()) return;
		std::vector<PartyEvent> ev;
		const double now = Sim::Hours();
		auto* pc = RE::PlayerCharacter::GetSingleton();
		for (auto& p : gPending) {
			Group* g = Sim::FindGroup(p.squad);
			if (!g) continue;
			Companion* c = gP.Find(p.squad);
			auto sp = p.speaker.get();
			switch (p.c) {
			case Talk::Choice::Recruit:
				if (!gP.Recruit(Sim::B(), *g, Rep::RegardOf(*g), HeldByJob(*g), 0, now, gRng, ev)) Notify("They have changed their mind.");
				break;
			case Talk::Choice::Hire1:
			case Talk::Choice::Hire2: {
				const int days = gP.cfg.days[p.c == Talk::Choice::Hire1 ? 0 : 1];
				const int price = gP.Price(*g, days, gRng, Sim::Cfg().Diff(DiffChannel::Economy));
				if (!pc || pc->GetGoldAmount(true) < price) { Notify("You do not have the " + std::to_string(price) + " gold."); break; }
				if (!gP.Recruit(Sim::B(), *g, Rep::RegardOf(*g), HeldByJob(*g), days, now, gRng, ev)) Notify("They have changed their mind.");
				break;
			}
			case Talk::Choice::Follow: if (c) c->order = Order::Follow; break;
			case Talk::Choice::Wait: if (c) { c->order = Order::Wait; HoldAt(*g, true); } break;
			case Talk::Choice::Move: if (c) { c->order = Order::Move; SendAhead(*g, sp ? sp->As<RE::Actor>() : nullptr); gDriven.erase(g->id); } break;
			case Talk::Choice::HoldFire: if (c) c->holdFire = true; break;
			case Talk::Choice::Attack: if (c) c->holdFire = false; break;
			case Talk::Choice::Loot: if (c) c->loot = true; break;
			case Talk::Choice::NoLoot: if (c) c->loot = false; break;
			case Talk::Choice::Gear:
				if (auto* a = sp ? sp->As<RE::Actor>() : nullptr; c && a && !a->IsDead()) Papyrus("Gear", a);
				break;
			case Talk::Choice::Dismiss: {
				const PartyPlace at = PlayerPlace();
				const std::string name = c ? c->name : Label(*g);
				if (!gP.Dismiss(Sim::B(), p.squad, PartyEvent::Kind::Left, ev, &at) && gP.Has(p.squad))   // no place for them here: they stay
					Notify(name + "'s band: \"Not today.\" They will not part ways here.");
				break;
			}
			default: break;
			}
			if (c && p.c >= Talk::Choice::Follow && p.c <= Talk::Choice::NoLoot)
				Log::Line("[comp] " + Label(*g) + " order: " + OrderName(c->order) + (c->holdFire ? ", hold fire" : ", attack") + (c->loot ? ", loot" : ", no loot"));
		}
		gPending.clear();
		ApplyEvents(ev);
	}

	void TickSquad(Group& g, Companion& c, float dt)
	{
		if (g.livingCount() == 0) return;   // Party::Update reports the loss
		if (Rep::RegardOf(g) < Regard::Neutral) {   // struck by the player, or a deed turned them
			std::vector<PartyEvent> ev;
			const PartyPlace at = PlayerPlace();
			gP.Dismiss(Sim::B(), g.id, PartyEvent::Kind::Turned, ev, &at);
			ApplyEvents(ev);
			return;
		}
		if (!g.online) {
			bool placed = false;
			if (c.order == Order::Follow) placed = PlaceNearPlayer(g);
			else placed = PlaceAtWaitPoint(g, c);
			if (placed) { gDriven.erase(g.id); Log::Line("[comp] " + Label(g) + " placed " + (c.order == Order::Follow ? "beside the player" : "at its wait point")); }
			return;
		}
		auto* lead = LeaderActor(g);
		if (!lead) {   // every member on screen is dead: the online layer would retire it; do it here (the dead are reaped)
			Online::SendOffline(g, false);
			return;
		}
		if (const auto p = lead->GetPosition(); Forms::WsIndex(lead->GetWorldspace()) == int(g.ws)) { g.x = p.x; g.y = p.y; g.z = p.z; }
		// looting (ALife's scavenging, only when told): while busy at a body the scavenger drives the squad; afterwards it falls back in
		const bool busy = c.loot && c.order == Order::Follow && Scavenge::TickGroup(g, lead, dt);
		if (busy) { gLooting[g.id] = true; return; }
		if (gLooting[g.id]) { gLooting[g.id] = false; gDriven.erase(g.id); }
		if (NeedsApply(g, c)) Apply(g, c);
		if (c.holdFire)
			for (auto& [m, a] : Living(g))
				if (a->IsInCombat() && Pool::InWorld(a)) { a->StopCombat(); a->EvaluatePackage(true, false); }
		if (c.order == Order::Follow) Teleport(g, dt);
		if (c.order == Order::Move && g.slot >= 0) {
			if (auto* mk = Forms::R().slotMarkers[g.slot]) {
				const auto a = lead->GetPosition(), b = mk->GetPosition();
				if ((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) < kArrive * kArrive) {
					c.order = Order::Wait;
					HoldAt(g, false);
					gDriven[g.id].order = Order::Wait;
					Log::Line("[comp] " + Label(g) + " arrived and holds");
				}
			}
		}
	}
}

bool Ready() { return gReady; }
bool Has(std::uint64_t id) { return gP.Has(id); }
Party* Model() { return &gP; }
void ApplyJobEvents(const std::vector<PartyEvent>& ev) { ApplyEvents(ev); }
bool HoldsFire(std::uint64_t id) { const Companion* c = gP.Find(id); return c && c->holdFire; }
bool Loots(std::uint64_t id) { const Companion* c = gP.Find(id); return c && c->loot; }
int SlotsWanted()
{
	int n = 0;
	for (auto& c : gP.list)
		if (const Group* g = c.order == Order::Follow ? Sim::FindGroup(c.squad) : nullptr; g && !g->online && g->livingCount() > 0) ++n;
	return n;
}

void Init()
{
	gReady = false;
	gOffer = gState = gHire1 = gHire2 = gFire = gLoot = nullptr;
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh || !dh->LookupModByName(kPlugin)) { Log::Line("[comp] ALifeTalk.esp is not loaded: no companions"); return; }
	Ltx talk, cl;
	std::string err;
	if (!talk.LoadFile(Settings::ConfigDir() + "/talk.ltx", &err) || !cl.LoadFile(Settings::ConfigDir() + "/companions.ltx", &err)) {
		Log::Line("[comp] config missing: " + err);
		return;
	}
	auto glob = [&](const char* key) -> RE::TESGlobal* {
		auto* f = dh->LookupForm(Hex(talk.Get("globals", key, "0")), kPlugin);
		return f ? f->As<RE::TESGlobal>() : nullptr;
	};
	gOffer = glob("comp_offer"); gState = glob("comp_state"); gHire1 = glob("hire1"); gHire2 = glob("hire2"); gFire = glob("comp_fire"); gLoot = glob("comp_loot");
	if (!LoadPartyConfig(cl, gP.cfg, err)) { Log::Line("[comp] companions.ltx: " + err); return; }
	gReady = gOffer && gState && gHire1 && gHire2 && gFire && gLoot;
	Log::Line(std::string("[comp] ") + (gReady ? "ready" : "NOT ready (ALifeTalk.esp without the companion globals)") + ": max " +
			  std::to_string(gP.cfg.maxMembers) + " companions in " + std::to_string(gP.cfg.maxSquads) + " squads, " + std::to_string(int(gP.cfg.dayGold)) + " gold a day");
}

void Reset()
{
	gP.Reset();
	gPending.clear();
	gDriven.clear();
	gAway.clear();
	gLooting.clear();
	gRestore = false;
	if (Sim::BoardReady()) Sim::B().companions.clear();
	for (auto* g : { gOffer, gState, gHire1, gHire2, gFire, gLoot }) if (g) g->value = 0.f;
}

void PublishFor(const Group* g, int mode)
{
	if (!gReady) return;
	int offer = 0, state = 0, fire = 0, loot = 0, h1 = 0, h2 = 0;
	if (g && Sim::BoardReady()) {
		if (const Companion* c = gP.Find(g->id); c && mode == Talk::kCompanion) {
			state = c->order == Order::Follow ? 1 : 2;
			fire = c->holdFire ? 1 : 0;
			loot = c->loot ? 1 : 0;
		} else if (mode == Talk::kGreet) {
			offer = int(OfferOf(*g));
			if (offer == int(CompOffer::Paid)) { h1 = gP.Price(*g, gP.cfg.days[0], gRng, Sim::Cfg().Diff(DiffChannel::Economy)); h2 = gP.Price(*g, gP.cfg.days[1], gRng, Sim::Cfg().Diff(DiffChannel::Economy)); }
		}
	}
	gOffer->value = float(offer);
	gState->value = float(state);
	gFire->value = float(fire);
	gLoot->value = float(loot);
	gHire1->value = float(h1);
	gHire2->value = float(h2);
}

void OnChoice(std::uint64_t squad, Talk::Choice c, RE::Actor* speaker)
{
	if (c == Talk::Choice::HireAsk) {   // the price, said plainly (the spoken line cannot carry numbers)
		if (gHire1 && gHire2)
			RE::SendHUDMessage::ShowHUDMessage(("Hire: " + std::to_string(gP.cfg.days[0]) + " day for " + std::to_string(int(gHire1->value)) + " gold, " +
												std::to_string(gP.cfg.days[1]) + " days for " + std::to_string(int(gHire2->value)) + " gold").c_str());
		Log::Line("[comp] squad #" + std::to_string(squad) + " asks " + std::to_string(int(gHire1 ? gHire1->value : 0)) + " / " +
				  std::to_string(int(gHire2 ? gHire2->value : 0)) + " gold");
		return;
	}
	gPending.push_back({ squad, c, speaker ? speaker->GetHandle() : RE::ActorHandle{} });
}

void Redrive(Group& g) { gDriven.erase(g.id); }

void Unteam(RE::Actor* a)
{
	if (a && a->IsPlayerTeammate()) Papyrus("Team", a, false);
}

void Tick(float dt)
{
	if (!gReady || !Sim::BoardReady()) return;
	if (gRestore) {
		gRestore = false;
		gP.Rebind(Sim::B());
		Log::Line("[comp] restored " + std::to_string(gP.list.size()) + " companion squads (" + std::to_string(gP.LivingCompanions(Sim::B())) + " members)");
	}
	RunChoices();
	if ((gUpdate += dt) >= 1.f) {
		gUpdate = 0.f;
		std::vector<PartyEvent> ev;
		gP.Update(Sim::B(), Sim::Hours(), ev);
		ApplyEvents(ev);
	}
	if ((gDrive += dt) < 0.5f) return;
	const float step = gDrive;
	gDrive = 0.f;
	if (Talk::InConversation()) return;
	for (std::size_t i = 0; i < gP.list.size(); ++i) {
		Companion& c = gP.list[i];
		if (Group* g = Sim::FindGroup(c.squad)) TickSquad(*g, c, step);
		if (i >= gP.list.size() || &gP.list[i] != &c) break;   // the list changed (a squad left): next tick
	}
}

// ---------------------------------------------------------------- co-save
void BeforeSave()
{
	for (auto& c : gP.list)
		if (Group* g = Sim::FindGroup(c.squad); g && g->online)
			for (auto& [m, a] : Living(*g)) Economy::ReadBackFrom(a, *m);   // gear handed over survives the save
}

void SaveTo(SKSE::SerializationInterface* a)
{
	std::vector<std::uint8_t> b;
	gP.Save(b);
	if (!a->WriteRecord(kRec, Party::kSaveVersion, b.data(), std::uint32_t(b.size()))) Log::Line("[comp] FAILED to write the co-save record");
	else Log::Line("[comp] saved " + std::to_string(gP.list.size()) + " companion squads");
}

void LoadFrom(SKSE::SerializationInterface* a, std::uint32_t ver, std::uint32_t len)
{
	std::vector<std::uint8_t> b(len);
	if (a->ReadRecordData(b.data(), len) != len) return;
	if (!gP.Load(b.data(), b.size(), ver)) { Log::Line("[comp] co-save record v" + std::to_string(ver) + " skipped: no companions"); return; }
	for (auto& c : gP.list) {
		RE::FormID out = 0;
		c.cell = c.cell && a->ResolveFormID(c.cell, out) ? out : 0;
		out = 0;
		c.ws = c.ws && a->ResolveFormID(c.ws, out) ? out : 0;
	}
	gRestore = true;
	Log::Line("[comp] loaded " + std::to_string(gP.list.size()) + " companion squads");
}

// ---------------------------------------------------------------- debug bridge
void Debug(const std::string& a_args)
{
	std::lock_guard l(Sim::Lock());
	if (!gReady || !Sim::BoardReady()) { Log::Line("[comp] debug: not ready"); return; }
	const auto parts = SplitList(a_args, ' ');
	const std::string verb = parts.empty() ? "list" : parts[0];
	const std::string arg = parts.size() > 1 ? parts[1] : std::string();
	auto* pc = RE::PlayerCharacter::GetSingleton();
	Group* scen = Sim::DebugScenarioGroup() ? Sim::FindGroup(Sim::DebugScenarioGroup()) : nullptr;
	if (verb == "list") {
		for (auto& c : gP.list) {
			Group* g = Sim::FindGroup(c.squad);
			std::string who;
			if (g)
				for (auto& [m, a] : Living(*g)) {
					const auto p = a->GetPosition();
					const auto pp = pc ? pc->GetPosition() : p;
					char b[200];
					std::snprintf(b, sizeof b, " [%s %08X d=%d team=%d combat=%d aggr=%g cell=%08X]", m->name.c_str(), a->GetFormID(),
								  int(std::sqrt((p.x - pp.x) * (p.x - pp.x) + (p.y - pp.y) * (p.y - pp.y))), a->IsPlayerTeammate() ? 1 : 0, a->IsInCombat() ? 1 : 0,
								  a->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAggression), a->GetParentCell() ? a->GetParentCell()->GetFormID() : 0);
					who += b;
				}
			Log::Line("[comp] " + (g ? Label(*g) : "squad #" + std::to_string(c.squad) + " (gone)") + ": " + (c.paid ? "paid until hour " + std::to_string(int(c.until)) : std::string("free")) +
					  ", " + OrderName(c.order) + (c.holdFire ? ", hold fire" : ", attack") + (c.loot ? ", loot" : "") + ", living " + std::to_string(g ? g->livingCount() : 0) +
					  (g && g->online ? " online" : " offline") + ", board " + (Sim::B().companions.count(c.squad) ? "detached" : "ATTACHED") +
					  ", target " + std::to_string(g ? g->assignedTarget : -2) + who);
		}
		Log::Line("[comp] " + std::to_string(gP.list.size()) + " companion squads, " + std::to_string(gP.LivingCompanions(Sim::B())) + " members; player gold " +
				  std::to_string(pc ? pc->GetGoldAmount(true) : -1) + ", teammates " + std::to_string(pc ? int(pc->GetPlayerRuntimeData().teammateCount) : -1) +
				  ", online squads " + std::to_string(Sim::GroupsOnline()));
		if (scen) Log::Line("[comp] scenario squad #" + std::to_string(scen->id) + " (" + FactionId(scen->faction) + " " + TierName(scen->tier) + "): regard " +
							RegardName(Rep::RegardOf(*scen)) + ", offer " + std::to_string(int(OfferOf(*scen))) + ", price " +
							std::to_string(gP.Price(*scen, gP.cfg.days[0], gRng, Sim::Cfg().Diff(DiffChannel::Economy))) + "/" + std::to_string(gP.Price(*scen, gP.cfg.days[1], gRng, Sim::Cfg().Diff(DiffChannel::Economy))));
		return;
	}
	if (verb == "like") {   // the scenario squad's own memory of the player (STANDING: memory adds to its attitude)
		if (!scen) { Log::Line("[comp] debug: no scenario squad"); return; }
		scen->memory = std::clamp(float(std::atof(arg.c_str())), -1000.f, 1000.f);
		Log::Line("[comp] debug: squad #" + std::to_string(scen->id) + " memory " + std::to_string(int(scen->memory)) + " -> regard " + RegardName(Rep::RegardOf(*scen)));
		return;
	}
	if (verb == "willing") {   // its paid roll succeeds
		if (!scen) { Log::Line("[comp] debug: no scenario squad"); return; }
		gP.Price(*scen, 1, gRng, Sim::Cfg().Diff(DiffChannel::Economy));   // rolls it if not yet
		gP.rolls[scen->id].willing = true;
		Log::Line("[comp] debug: squad #" + std::to_string(scen->id) + " is willing to hire out");
		return;
	}
	if (verb == "recruit") {   // join without the dialogue (days > 0: paid)
		if (!scen) { Log::Line("[comp] debug: no scenario squad"); return; }
		std::vector<PartyEvent> ev;
		const int days = std::atoi(arg.c_str());
		const bool ok = gP.Recruit(Sim::B(), *scen, Rep::RegardOf(*scen), HeldByJob(*scen), days, Sim::Hours(), gRng, ev);
		Log::Line("[comp] debug: recruit squad #" + std::to_string(scen->id) + (ok ? " ok" : " refused (offer " + std::to_string(int(OfferOf(*scen))) + ")"));
		ApplyEvents(ev);
		return;
	}
	if (verb == "order") {   // "comp order wait" for every companion squad
		static const std::map<std::string, Talk::Choice> kOrder{ { "follow", Talk::Choice::Follow }, { "wait", Talk::Choice::Wait }, { "move", Talk::Choice::Move },
																 { "hold", Talk::Choice::HoldFire }, { "attack", Talk::Choice::Attack }, { "loot", Talk::Choice::Loot },
																 { "noloot", Talk::Choice::NoLoot }, { "dismiss", Talk::Choice::Dismiss } };
		auto it = kOrder.find(arg);
		if (it == kOrder.end()) { Log::Line("[comp] debug: unknown order '" + arg + "'"); return; }
		for (auto& c : gP.list) {
			Group* g = Sim::FindGroup(c.squad);
			gPending.push_back({ c.squad, it->second, g && LeaderActor(*g) ? LeaderActor(*g)->GetHandle() : RE::ActorHandle{} });
		}
		RunChoices();
		return;
	}
	if (verb == "talk") {   // talk to companion squad n (0 = first), as if the player activated its leader
		const std::size_t n = std::size_t(std::max(0, std::atoi(arg.c_str())));
		if (n >= gP.list.size()) { Log::Line("[comp] debug: no companion squad " + arg); return; }
		Sim::DebugTalkTo(gP.list[n].squad);
		return;
	}
	if (verb == "fight") {   // each companion's combat state, and the hostile squads on screen
		for (auto& c : gP.list)
			if (Group* g = Sim::FindGroup(c.squad))
				for (auto& [m, a] : Living(*g)) {
					auto t = a->GetActorRuntimeData().currentCombatTarget.get();
					Log::Line("[comp] fight: " + m->name + (a->IsInCombat() ? " IN COMBAT" : " calm") + (t ? std::string(" with ") + t->GetName() : std::string()) +
							  (c.holdFire ? " (hold fire)" : " (attack)"));
				}
		for (auto& g : Sim::Groups())
			if (g.online && !gP.Has(g.id)) Log::Line("[comp] fight: on screen " + Label(g) + " living " + std::to_string(g.livingCount()) + (Behavior::AttacksOnSight(g) ? " hostile" : ""));
		return;
	}
	if (verb == "foe") {   // a hostile squad of the companions' enemies comes on screen near the player (the scenario squad slot is not used)
		Group* comp = gP.list.empty() ? nullptr : Sim::FindGroup(gP.list[0].squad);
		if (!comp || !pc) { Log::Line("[comp] debug: no companion squad"); return; }
		for (auto& g : Sim::Groups())   // room on screen: everyone but the companions leaves
			if (g.online && !gP.Has(g.id)) Online::SendOffline(g, true);
		const auto& cfg = Sim::Cfg();
		Group* foe = nullptr;
		float best = 1e30f;
		const auto pp = pc->GetPosition();
		for (auto& g : Sim::Groups()) {
			if (gP.Has(g.id) || !IsHumanFaction(g.faction) || g.livingCount() == 0 || g.ws != comp->ws || !cfg.IsEnemy(comp->faction, g.faction)) continue;
			const float d = (g.x - pp.x) * (g.x - pp.x) + (g.y - pp.y) * (g.y - pp.y);
			if (d < best) { best = d; foe = &g; }
		}
		if (!foe) { Log::Line("[comp] debug: no enemy squad of the " + std::string(FactionId(comp->faction))); return; }
		const bool ok = Online::BringOnlineEx(*foe, true, true);
		Log::Line("[comp] debug: foe " + Label(*foe) + (ok ? " is on screen" : " could not come on screen"));
		if (ok) {   // placed out of sight (4.5k+): put it 1200 units ahead of the player. No StartCombat here: the actors were moved this
			// frame and their AI is not loaded yet (lab run 2 crashed in the engine's combat start); the online layer pairs enemies every 2 s
			const float yaw = pc->GetAngleZ();
			const RE::NiPoint3 at{ pp.x + std::sin(yaw) * 1200.f, pp.y + std::cos(yaw) * 1200.f, pp.z + 50.f };
			float k = 0.f;
			for (auto& [m, a] : Living(*foe)) {
				a->MoveTo(pc);
				a->SetPosition({ at.x + 80.f * k, at.y, at.z }, true);
				k += 1.f;
			}
			foe->x = at.x; foe->y = at.y;
		}
		return;
	}
	if (verb == "shift") {   // the player steps n units forward (testing wait / follow without input)
		if (!pc) return;
		const float n = float(std::atof(arg.c_str()));
		const auto pp = pc->GetPosition();
		const float yaw = pc->GetAngleZ();
		RE::NiPoint3 to{ pp.x + std::sin(yaw) * n, pp.y + std::cos(yaw) * n, pp.z + 50.f };
		if (auto* tes = RE::TES::GetSingleton()) { float h = 0.f; if (tes->GetLandHeight(to, h)) to.z = h + 20.f; }
		pc->SetPosition(to, true);
		Log::Line("[comp] debug: the player stepped " + std::to_string(int(n)) + " units forward");
		return;
	}
	if (verb == "away") {   // the first companion squad is moved 6000 units off: the teleport must bring it back
		Group* g = gP.list.empty() ? nullptr : Sim::FindGroup(gP.list[0].squad);
		if (!g || !pc) { Log::Line("[comp] debug: no companion squad"); return; }
		const auto pp = pc->GetPosition();
		for (auto& [m, a] : Living(*g)) a->SetPosition({ pp.x + 6000.f, pp.y, pp.z + 200.f }, true);
		Log::Line("[comp] debug: " + Label(*g) + " moved 6000 units away");
		return;
	}
	Log::Line("[comp] debug: unknown '" + a_args + "' (list, like <n>, willing, recruit [days], order <o>, talk [n], fight, foe, away, shift <n>)");
}
}
