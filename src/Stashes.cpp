// Stashes, the Skyrim side (docs/STASHES.md). The core (src/core/Stashes.cpp) decides where stashes are, what they hold and when
// they are marked; this file places and fills the containers, drives the ALifeStashes markers through ALife_Tasks.psc, handles
// notes and looting, and saves. [U] until the M5f lab check: placement, filling and the events are untested in game.
#include "ALife/StashesPlugin.h"

#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Ltx.h"
#include "ALife/Settings.h"
#include "ALife/Systems.h"

namespace ALife::StashPlugin {
namespace {
	constexpr std::string_view kPlugin = "ALifeTalk.esp"sv;
	constexpr std::uint32_t kRec = 'STSH';
	constexpr RE::FormID kXMarker = 0x0000003B;   // Skyrim.esm XMarker (static)
	constexpr float kSnapRadius = 800.f;
	constexpr int kMaxSlots = 32;

	bool gReady{ false };
	Stashes gS;
	RE::TESQuest* gQuest{ nullptr };
	RE::TESBoundObject* gNote{ nullptr };
	std::array<RE::FormID, kMaxSlots> gMarker{};   // XMarkers, one per marker slot (persistent; their FormIDs in the co-save)
	bool gRestore{ false };
	float gAcc{ 0 };
	Rng gRng{ 0x57A5 };
	struct Change { RE::FormID from, to, base; std::int32_t count; };
	std::vector<Change> gChanges;   // filled on the event thread, drained in Tick
	std::mutex gChangeMx;
	std::vector<RE::ActorHandle> gNoteBodies;   // on-screen dead who get a note next tick

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
		vm->DispatchStaticCall("ALife_Tasks", a_fn, args.get(), cb);   // the generic quest helpers (Begin / Show / Mark)
	}

	void Notify(const std::string& a_text)
	{
		RE::SendHUDMessage::ShowHUDMessage(a_text.c_str());
		Log::Line("[stash] " + a_text);
	}

	int PlayerLevel()
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		return pc ? int(pc->GetLevel()) : 1;
	}

	float Economy() { return Sim::Cfg().Diff(DiffChannel::Economy); }

	std::string PlaceOf(const StashSpot& s)
	{
		const GraphNode* n = Graph::Node(s.node);
		return n ? n->name : std::string("the wilds");
	}

	RE::TESObjectREFR* Ref(RE::FormID id)
	{
		auto* r = id ? RE::TESForm::LookupByID<RE::TESObjectREFR>(id) : nullptr;
		return r && !r->IsDeleted() ? r : nullptr;
	}

	RE::TESObjectREFR* Marker(int slot)
	{
		if (slot < 0 || slot >= kMaxSlots) return nullptr;
		if (auto* r = Ref(gMarker[std::size_t(slot)])) return r;
		auto* pc = RE::PlayerCharacter::GetSingleton();
		auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker);
		if (!pc || !base) return nullptr;
		auto ref = pc->PlaceObjectAtMe(base, true);   // persistent: lives in the save, its FormID in the co-save
		if (!ref) return nullptr;
		gMarker[std::size_t(slot)] = ref->GetFormID();
		return ref.get();
	}

	// the marker onto the container once placed, else onto the place's world marker and then over the spot
	void Aim(const StashSpot& s)
	{
		auto* mk = Marker(s.slot);
		if (!mk) return;
		if (auto* c = Ref(s.ref)) { if (c != mk) mk->MoveTo(c); return; }
		if (const GraphNode* n = Graph::Node(s.node))
			if (auto* at = Graph::MarkerOf(*n)) {
				mk->MoveTo(at);
				mk->SetPosition(RE::NiPoint3{ s.x, s.y, s.z });
			}
	}

	void Show(const StashSpot& s)
	{
		if (!gQuest || s.slot < 0) return;
		for (auto* o : gQuest->objectives)
			if (o && o->index == std::uint16_t(s.slot)) o->displayText = ("Stash near " + PlaceOf(s)).c_str();
		Aim(s);
		Papyrus("Begin", gQuest);
		Papyrus("Show", gQuest, int(s.slot), int(s.slot), Marker(s.slot));
	}

	void Apply(const std::vector<StashEvent>& ev)
	{
		for (auto& e : ev) {
			const StashSpot* s = gS.Find(e.spot);
			switch (e.kind) {
			case StashEvent::Kind::Marked:
				if (s) {
					Show(*s);
					Notify(std::string(e.why == "reward" ? "Your pay: a stash" : "A stash") + " near " + PlaceOf(*s) + " is marked on your map");
				}
				break;
			case StashEvent::Kind::Unmarked:
				if (gQuest && e.slot >= 0) Papyrus("Mark", gQuest, e.slot, e.why == "looted" ? 2 : 0);
				break;
			case StashEvent::Kind::Looted:
				Log::Line("[stash] stash #" + std::to_string(e.spot) + " looted (" + std::to_string(gS.found) + " found so far)");
				break;
			}
		}
	}

	// a spot near the player gets its container (on walkable ground, in a loaded cell) and its content
	bool Place(StashSpot& s)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(s.base);
		if (!pc || !base) return false;
		RE::TESObjectREFR* ref = Ref(s.ref);
		if (!ref) {
			RE::NiPoint3 at;
			RE::TESObjectCELL* cell = nullptr;
			if (!Online::SnapPoint(RE::NiPoint3{ s.x, s.y, s.z }, kSnapRadius, at, cell) || !cell || !cell->IsAttached()) return false;   // its ground is not loaded yet
			auto placed = pc->PlaceObjectAtMe(base, true);
			ref = placed.get();
			if (!ref) return false;
			Online::MoveRef(ref, cell, Forms::WsObj(s.ws), at);
			std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> leveled;   // a sack's own leveled content goes: the stash is ours
			for (auto& [obj, data] : ref->GetInventory()) if (data.first > 0) leveled.emplace_back(obj, data.first);
			for (auto& [obj, n] : leveled) ref->RemoveItem(obj, n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			s.x = at.x; s.y = at.y; s.z = at.z;
		}
		if (!s.rolled) gS.Roll(s, gS.Budget(PlayerLevel(), Economy(), gRng), PlayerLevel(), gRng);   // a hidden stash: content on first use
		int n = 0;
		for (auto& it : s.items)
			if (auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(it.form); obj && it.count > 0) { ref->AddObjectToContainer(obj, nullptr, it.count, nullptr); ++n; }
		gS.Placed(s, ref->GetFormID());
		if (s.slot >= 0) Aim(s);
		Log::Line("[stash] stash #" + std::to_string(s.id) + " placed near " + PlaceOf(s) + " (" + std::to_string(n) + " kinds of item)");
		return true;
	}

	void Drain()
	{
		std::vector<Change> changes;
		{
			std::lock_guard l(gChangeMx);
			changes.swap(gChanges);
		}
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) return;
		const RE::FormID player = pc->GetFormID();
		std::vector<StashEvent> ev;
		for (auto& c : changes) {
			if (gNote && c.base == gNote->GetFormID() && c.to == player) {   // a stash note read off a body (or found)
				pc->RemoveItem(gNote, std::max(1, c.count), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				const auto p = pc->GetPosition();
				const int ws = Forms::WsIndex(pc->GetWorldspace());
				if (gS.OnNoteRead(p.x, p.y, ws < 0 ? 0 : ws, PlayerLevel(), Economy(), gRng, ev) < 0) Notify("The note mentions a stash, but nothing is left to find");
				continue;
			}
			if (c.to == player)
				if (StashSpot* s = gS.FindByRef(c.from); s && s->Filled()) gS.OnLooted(s->id, ev);   // the first item out of a stash
		}
		Apply(ev);
	}
}

bool Ready() { return gReady; }
Stashes& S() { return gS; }

void Init()
{
	gReady = false;
	gQuest = nullptr;
	gNote = nullptr;
	Ltx sl;
	std::string err;
	if (!sl.LoadFile(Settings::ConfigDir() + "/stashes.ltx", &err)) { Log::Line("[stash] stashes.ltx missing: no stashes (" + err + ")"); return; }
	StashConfig c;
	if (!LoadStashConfig(sl, c, err)) { Log::Line("[stash] stashes.ltx: " + err); return; }
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (dh && dh->LookupModByName(kPlugin)) {
		Ltx talk;
		if (talk.LoadFile(Settings::ConfigDir() + "/talk.ltx", nullptr)) {
			if (auto* f = dh->LookupForm(Hex(talk.Get("stashes", "note", "0")), kPlugin)) gNote = f->As<RE::TESBoundObject>();
			if (auto* f = dh->LookupForm(Hex(talk.Get("stashes", "quest", "0")), kPlugin)) gQuest = f->As<RE::TESQuest>();
			c.markerSlots = std::min(c.markerSlots, std::min(kMaxSlots, talk.GetI("stashes", "slots", c.markerSlots)));
		}
	}
	c.noteForm = gNote ? gNote->GetFormID() : 0;   // no note record: no notes (stashes and job rewards still work)
	gS.cfg = c;
	gReady = c.enabled;
	Log::Line(std::string("[stash] ") + (gReady ? "ready" : "disabled") + ": note " + (gNote ? "ok" : "missing") + ", markers " + (gQuest ? "ok" : "missing") +
			  ", " + std::to_string(c.loot.size()) + " loot kinds");
}

void Reset()
{
	gS.Reset();
	gMarker.fill(0);
	gRestore = false;
	gNoteBodies.clear();
	std::lock_guard l(gChangeMx);
	gChanges.clear();
}

void Tick(float dt)
{
	if (!gReady || !Sim::BoardReady()) return;
	if (!gS.Built()) {   // a new game, or a save from before M5f: the same spots a new game with this world seed gets
		gS.Build(Graph::W(), Sim::B().worldSeed);
		int hidden = 0;
		for (auto& s : gS.spots) hidden += s.Filled();
		Log::Line("[stash] " + std::to_string(gS.spots.size()) + " stash spots, " + std::to_string(hidden) + " of them filled and unmarked");
	}
	if (gRestore) {   // after a load: objective texts are not saved, markers are aimed again
		gRestore = false;
		for (auto& s : gS.spots) if (s.slot >= 0) Show(s);
	}
	for (auto& h : gNoteBodies)   // on-screen dead: the note goes on the body now, outside the death event
		if (auto a = h.get(); a && gNote) a->AddObjectToContainer(gNote, nullptr, 1, nullptr);
	gNoteBodies.clear();
	Drain();
	if ((gAcc += dt) < 1.f) return;
	gAcc = 0.f;
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || !pc->GetParentCell() || pc->GetParentCell()->IsInteriorCell()) return;
	const int ws = Forms::WsIndex(pc->GetWorldspace());
	if (ws < 0) return;
	const auto p = pc->GetPosition();
	int placed = 0;
	for (auto* s : gS.ToPlace(p.x, p.y, ws))
		if (placed < 2 && Place(*s)) ++placed;   // a couple a second at most
}

void OnActorKilled(Group& g, Member& m, RE::Actor* victim)
{
	if (!gReady || !gNote || !victim || (Sim::BoardReady() && Sim::B().companions.count(g.id))) return;
	if (!gS.OnKill(g, m, nullptr, true, gRng)) return;
	if (Stashes::CarriesNote(m, gS.cfg.noteForm)) return;   // a carried note is on the actor already
	gNoteBodies.push_back(victim->GetHandle());
	Log::Line("[stash] " + m.name + " carried a stash note");
}

void OnOfflineKill(Group& g, Member& m, Group* killer)
{
	if (!gReady || !gNote || (Sim::BoardReady() && Sim::B().companions.count(g.id))) return;
	gS.OnKill(g, m, killer, false, gRng);
}

void OnContainerChanged(const RE::TESContainerChangedEvent* e)
{
	if (!e || !gReady) return;
	std::lock_guard l(gChangeMx);
	gChanges.push_back({ e->oldContainer, e->newContainer, e->baseObj, e->itemCount });
}

void OnTaskDone(std::int32_t& gold, const Task& t)
{
	if (!gReady || !Sim::BoardReady()) return;
	float x = 0.f, y = 0.f;
	int ws = 0;
	if (const Group* g = Sim::FindGroup(t.giver)) { x = g->x; y = g->y; ws = g->ws; }
	else if (auto* pc = RE::PlayerCharacter::GetSingleton()) { const auto p = pc->GetPosition(); x = p.x; y = p.y; ws = std::max(0, Forms::WsIndex(pc->GetWorldspace())); }
	std::vector<StashEvent> ev;
	const std::int32_t before = gold;
	if (gS.TaskReward(gold, x, y, ws, PlayerLevel(), Economy(), gRng, ev)) Log::Line("[stash] job pays with a stash (gold " + std::to_string(before) + " -> " + std::to_string(gold) + ")");
	Apply(ev);
}

// ---------------------------------------------------------------- co-save
void SaveTo(SKSE::SerializationInterface* a)
{
	std::vector<std::uint8_t> b;
	gS.Save(b);
	for (auto id : gMarker) { auto* p = reinterpret_cast<const std::uint8_t*>(&id); b.insert(b.end(), p, p + sizeof(id)); }
	if (!a->WriteRecord(kRec, Stashes::kSaveVersion, b.data(), std::uint32_t(b.size()))) Log::Line("[stash] FAILED to write the co-save record");
	else Log::Line("[stash] saved " + std::to_string(gS.spots.size()) + " stash spots, " + std::to_string(gS.MarkedCount()) + " marked");
}

void LoadFrom(SKSE::SerializationInterface* a, std::uint32_t ver, std::uint32_t len)
{
	std::vector<std::uint8_t> b(len);
	if (a->ReadRecordData(b.data(), len) != len) return;
	const std::size_t tail = sizeof(RE::FormID) * gMarker.size();
	if (len < tail || !gS.Load(b.data(), len - tail, ver)) { Log::Line("[stash] co-save record v" + std::to_string(ver) + " skipped: spots built afresh"); gS.Reset(); return; }
	std::memcpy(gMarker.data(), b.data() + (len - tail), tail);
	for (auto& id : gMarker) { RE::FormID out = 0; id = id && a->ResolveFormID(id, out) ? out : 0; }
	for (auto& s : gS.spots) {
		RE::FormID out = 0;
		s.ref = s.ref && a->ResolveFormID(s.ref, out) ? out : 0;
		if (!s.ref && s.rolled && s.items.empty()) s.rolled = false;   // its container is gone with what it held: rolled again when placed
	}
	gRestore = true;
	Log::Line("[stash] loaded " + std::to_string(gS.spots.size()) + " stash spots, " + std::to_string(gS.MarkedCount()) + " marked, " + std::to_string(gS.found) + " found");
}

// ---------------------------------------------------------------- debug bridge
void Debug(const std::string& a_args)
{
	std::lock_guard l(Sim::Lock());
	if (!gReady || !Sim::BoardReady()) { Log::Line("[stash] debug: not ready"); return; }
	auto* pc = RE::PlayerCharacter::GetSingleton();
	const auto parts = SplitList(a_args, ' ');
	const std::string verb = parts.empty() ? "list" : parts[0];
	const auto p = pc ? pc->GetPosition() : RE::NiPoint3{};
	const int ws = pc ? std::max(0, Forms::WsIndex(pc->GetWorldspace())) : 0;
	std::vector<StashEvent> ev;
	if (verb == "list") {
		int filled = 0, placed = 0;
		for (auto& s : gS.spots) {
			filled += s.Filled();
			placed += s.ref != 0;
			if (s.slot >= 0 || (s.ref && s.Filled()))
				Log::Line("[stash] #" + std::to_string(s.id) + " near " + PlaceOf(s) + (s.slot >= 0 ? " marked (slot " + std::to_string(s.slot) + ")" : "") +
						  (s.ref ? " placed" : "") + ", " + std::to_string(gS.Value(s)) + " gold held by the core");
		}
		const auto& c = gS.counters;
		Log::Line("[stash] " + std::to_string(gS.spots.size()) + " spots, " + std::to_string(filled) + " filled, " + std::to_string(placed) + " placed, " +
				  std::to_string(gS.MarkedCount()) + " marked, " + std::to_string(gS.found) + " found; notes made " + std::to_string(c.notesMade) + ", passed " +
				  std::to_string(c.notesPassed) + ", read " + std::to_string(c.notesRead) + "; rewards " + std::to_string(c.rewards));
		return;
	}
	if (verb == "reveal") { gS.Reveal(p.x, p.y, ws, PlayerLevel(), Economy(), gRng, ev, "debug"); Apply(ev); return; }
	if (verb == "note") {   // into the player's pack, as if taken from a body: the container event reads it
		if (pc && gNote) pc->AddObjectToContainer(gNote, nullptr, 1, nullptr);
		Log::Line(std::string("[stash] debug: ") + (gNote ? "a note into the player's pack" : "no note record"));
		return;
	}
	if (verb == "place") {
		int n = 0;
		for (auto* s : gS.ToPlace(p.x, p.y, ws)) n += Place(*s) ? 1 : 0;
		Log::Line("[stash] debug: " + std::to_string(n) + " placed");
		return;
	}
	if (verb == "reward") {
		const float keep = gS.cfg.rewardChance;
		gS.cfg.rewardChance = 1.f;
		std::int32_t gold = 500;
		gS.TaskReward(gold, p.x, p.y, ws, PlayerLevel(), Economy(), gRng, ev);
		gS.cfg.rewardChance = keep;
		Apply(ev);
		Log::Line("[stash] debug: a 500-gold job paid " + std::to_string(gold) + " gold and " + std::to_string(ev.size()) + " stash event(s)");
		return;
	}
	Log::Line("[stash] debug: unknown '" + a_args + "' (list, reveal, note, place, reward)");
}
}
