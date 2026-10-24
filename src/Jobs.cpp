// Jobs: the Skyrim side of the task model (docs/TASKS.md). The core (src/core/Tasks.cpp) decides; this file publishes the
// dialogue globals, runs the ALifeTasks quest (objectives, aliases, markers), moves money, items and goodwill, and saves.
#include "ALife/Jobs.h"

#include "ALife/Companions.h"
#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Ltx.h"
#include "ALife/Rep.h"
#include "ALife/Settings.h"
#include "ALife/StashesPlugin.h"
#include "ALife/Systems.h"

namespace ALife::Jobs {
namespace {
	constexpr std::string_view kPlugin = "ALifeTalk.esp"sv;
	constexpr std::uint32_t kRec = 'TASK';
	constexpr RE::FormID kXMarker = 0x0000003B;   // Skyrim.esm XMarker (static)
	constexpr int kMaxSlots = 4;

	bool gReady{ false };
	Tasks gT;
	RE::TESGlobal* gOffer{ nullptr };
	RE::TESGlobal* gState{ nullptr };
	RE::TESQuest* gQuest{ nullptr };
	RE::TESBoundObject* gParcel{ nullptr };
	RE::TESBoundObject* gSatchel{ nullptr };                         // recover: the satchel on the body (M5e)
	std::array<RE::TESBoundObject*, kFactionCount> gCorpse{};        // recover: [recover] corpse_<faction> (null: a pool face)
	int gSlots{ kMaxSlots };
	std::array<RE::FormID, 2 * kMaxSlots> gMarker{};   // created XMarkers: Target0..3, Giver0..3 (alias ids 0..7)
	std::array<int, kMaxSlots> gShown{};               // objective shown per slot: 0 none, 1 work, 2 return
	struct Pending { std::uint64_t squad; Talk::Choice c; };
	std::vector<Pending> gPending;
	struct Hide { int index; float left; };
	std::vector<Hide> gHide;                            // finished objectives leave the journal after a moment
	bool gRestore{ false };
	float gUpdate{ 0 }, gMove{ 0 };
	Rng gRng{ 0x7A5C };

	RE::FormID Hex(const std::string& s)
	{
		try { return RE::FormID(std::stoul(s, nullptr, 16)); } catch (...) { return 0; }
	}

	RE::FormID ResolveSpec(const std::string& spec)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh || spec.empty()) return 0;
		if (const auto bar = spec.find('|'); bar != std::string::npos) {
			const std::string plugin = Trim(spec.substr(0, bar));
			if (!dh->LookupModByName(plugin)) return 0;
			return dh->LookupFormID(Hex(spec.substr(bar + 1)), plugin);
		}
		return dh->LookupFormID(Hex(spec), "Skyrim.esm"sv);
	}

	struct PlayerHost : TaskHost
	{
		int PlayerLevel() const override
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			return pc ? int(pc->GetLevel()) : 1;
		}
		int PlayerItemCount(FormID id) const override
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!pc || !id) return 0;
			int n = 0;
			for (auto& [obj, count] : pc->GetInventoryCounts([id](RE::TESBoundObject& o) { return o.GetFormID() == id; })) n += count;
			return n;
		}
		int ItemValue(FormID id, int cfgValue) const override
		{
			auto* f = id ? RE::TESForm::LookupByID(id) : nullptr;
			const int v = f ? f->GetGoldValue() : 0;
			return v > 0 ? v : cfgValue;
		}
		bool Approachable(const Group& g) const override { return Rep::RegardOf(g) >= Regard::Neutral; }
	};
	PlayerHost gHost;

	// ---------------------------------------------------------------- the quest (through ALife_Tasks.psc: the engine's own objective calls)
	template <class... Args>
	void Papyrus(const char* a_fn, Args... a_args)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) return;
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
		std::unique_ptr<RE::BSScript::IFunctionArguments> args{ RE::MakeFunctionArguments(std::move(a_args)...) };
		vm->DispatchStaticCall("ALife_Tasks", a_fn, args.get(), cb);
	}

	RE::BGSQuestObjective* Objective(int a_index)
	{
		if (!gQuest) return nullptr;
		for (auto* o : gQuest->objectives)
			if (o && o->index == a_index) return o;
		return nullptr;
	}

	void SetText(int a_index, const std::string& a_text)
	{
		if (auto* o = Objective(a_index)) o->displayText = a_text.c_str();
	}

	RE::TESObjectREFR* Marker(int a_alias)
	{
		if (a_alias < 0 || a_alias >= 2 * kMaxSlots) return nullptr;
		if (gMarker[a_alias])
			if (auto* r = RE::TESForm::LookupByID<RE::TESObjectREFR>(gMarker[a_alias]); r && !r->IsDeleted()) return r;
		auto* pc = RE::PlayerCharacter::GetSingleton();
		auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(kXMarker);
		if (!pc || !base) return nullptr;
		auto ref = pc->PlaceObjectAtMe(base, true);   // persistent: lives in the save, its FormID in the co-save
		if (!ref) return nullptr;
		gMarker[a_alias] = ref->GetFormID();
		Log::Line("[task] marker " + std::to_string(a_alias) + " created");
		return ref.get();
	}

	// where a squad is: its leader while on screen, else the world marker of the place it last passed
	RE::TESObjectREFR* Anchor(const Group& g)
	{
		if (g.online)
			for (auto& m : g.members)
				if (m.alive())
					if (auto* a = IsHumanFaction(g.faction) ? Pool::ActorOf(m) : Creatures::ActorOf(m); a && !a->IsDead()) return a;
		const GraphNode* n = Graph::Node(g.node);
		return n ? Graph::MarkerOf(*n) : nullptr;
	}

	void Place(int a_alias, const Group* g, std::uint32_t a_node)
	{
		auto* mk = Marker(a_alias);
		if (!mk) return;
		RE::TESObjectREFR* at = g ? Anchor(*g) : nullptr;
		if (!at)
			if (const GraphNode* n = Graph::Node(a_node)) at = Graph::MarkerOf(*n);
		if (at && at != mk) mk->MoveTo(at);
	}

	const Group* TargetGroup(const Task& t)
	{
		if (t.type == TaskType::Bounty || t.type == TaskType::Delivery || t.type == TaskType::Hunt) return Sim::FindGroup(t.target);
		if (t.type == TaskType::Defend)
			if (const Group* g = Sim::FindGroup(t.extra); g && g->livingCount() > 0) return g;   // the wave on its way
		if (t.type == TaskType::Assault || t.type == TaskType::Rescue)
			for (auto id : t.pinned)
				if (const Group* g = Sim::FindGroup(id); g && g->livingCount() > 0) return g;
		return nullptr;
	}

	// the work marker: where the job is done
	void PlaceWork(int a_alias, const Task& t)
	{
		switch (t.type) {
		case TaskType::Fetch: Place(a_alias, Sim::FindGroup(t.giver), t.node); break;   // the items are anywhere, the giver is the place to go
		case TaskType::Assault: Place(a_alias, nullptr, t.node); break;                  // the place itself
		case TaskType::Escort: Place(a_alias, nullptr, t.dest); break;                   // the destination
		case TaskType::Rescue: Place(a_alias, nullptr, t.node); break;                   // the captors' place (the captive is held there)
		case TaskType::Recover:                                                          // the body once it lies there, else the place
			if (auto* body = t.corpse ? RE::TESForm::LookupByID<RE::TESObjectREFR>(t.corpse) : nullptr; body && !body->IsDeleted()) {
				if (auto* mk = Marker(a_alias); mk && mk != body) mk->MoveTo(body);
			} else Place(a_alias, nullptr, t.node);
			break;
		case TaskType::Defend: Place(a_alias, nullptr, t.node); break;                   // the place you hold
		default: Place(a_alias, TargetGroup(t), t.node); break;
		}
	}

	// the return marker: the giver (rescue: the giver too; the freed captive follows you there)
	void PlaceReturn(int a_alias, const Task& t) { Place(a_alias, Sim::FindGroup(t.giver), t.node); }

	void MoveMarkers()
	{
		for (auto& t : gT.active) {
			if (t.slot < 0 || t.slot >= gSlots) continue;
			PlaceWork(t.slot, t);
			PlaceReturn(kMaxSlots + t.slot, t);
		}
	}

	void Show(const Task& t, bool a_return)
	{
		const int s = t.slot;
		const int index = 10 * s + (a_return ? 1 : 0);
		const int alias = a_return ? kMaxSlots + s : s;
		SetText(index, Tasks::Objective(t, a_return));
		if (a_return) PlaceReturn(alias, t);
		else PlaceWork(alias, t);
		std::erase_if(gHide, [&](const Hide& h) { return h.index == index; });   // only this objective: a just-completed sibling still leaves
		Papyrus("Show", gQuest, index, alias, Marker(alias));
		gShown[s] = a_return ? 2 : 1;
	}

	void Mark(int a_slot, int a_how)   // 2 completed, 3 failed: the objective on show, then it leaves the journal
	{
		if (a_slot < 0 || a_slot >= kMaxSlots || !gShown[a_slot]) return;
		const int index = 10 * a_slot + (gShown[a_slot] == 2 ? 1 : 0);
		Papyrus("Mark", gQuest, index, a_how);
		gHide.push_back({ index, 8.f });
		gShown[a_slot] = 0;
	}

	void Notify(const std::string& a_text)
	{
		RE::SendHUDMessage::ShowHUDMessage(a_text.c_str());
		Log::Line("[task] " + a_text);
	}

	void GiveConsumable(std::uint64_t a_giver)   // Anomaly: reward_random_item (beer, vodka...): one bite or potion from the giver's pack
	{
		Group* g = Sim::FindGroup(a_giver);
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!g || !pc) return;
		for (auto& m : g->members) {
			if (!m.alive()) continue;
			for (auto& it : m.inventory) {
				auto* f = RE::TESForm::LookupByID(it.form);
				if (!f || it.count <= 0 || !(Economy::IsFoodItem(f) || Economy::IsHealPotionItem(f))) continue;
				auto* obj = f->As<RE::TESBoundObject>();
				if (!obj) continue;
				if (g->online)
					if (auto* a = Pool::ActorOf(m)) a->RemoveItem(obj, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				Economy::RemoveItem(m, it.form, 1);
				pc->AddObjectToContainer(obj, nullptr, 1, nullptr);
				return;
			}
		}
	}

	// recover: the body the plugin laid out goes when its job ends (it was made persistent)
	void RemoveBody(const Task& t)
	{
		if (t.type != TaskType::Recover || !t.corpse) return;
		auto* body = RE::TESForm::LookupByID<RE::TESObjectREFR>(t.corpse);
		if (!body || body->IsDeleted()) return;
		body->Disable();
		body->SetDelete(true);
	}

	// recover (docs/TASKS.md section 8): once the player is within recover_spawn of the place and its cell is loaded, an actor of
	// the giver's faction is killed beside the place's world marker with the satchel in its pack
	void LayOutBodies()
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !gSatchel) return;
		for (auto& t : gT.active) {
			if (t.type != TaskType::Recover || t.stage != TaskStage::Open || t.corpse) continue;
			const GraphNode* n = Graph::Node(t.node);
			auto* mk = n ? Graph::MarkerOf(*n) : nullptr;
			auto* cell = mk ? mk->GetParentCell() : nullptr;
			if (!cell || !cell->IsAttached() || Forms::WsIndex(pc->GetWorldspace()) != int(n->ws)) continue;
			const auto pp = pc->GetPosition(), mp = mk->GetPosition();
			if ((pp.x - mp.x) * (pp.x - mp.x) + (pp.y - mp.y) * (pp.y - mp.y) > gT.cfg.recoverSpawn * gT.cfg.recoverSpawn) continue;
			RE::TESBoundObject* base = gCorpse[int(t.giverFaction)];
			if (!base) {   // no list for the faction: a pool face (a fresh actor of a pool NPC base, not the pool actor itself)
				auto& slots = Pool::Slots();
				for (std::size_t k = 0; k < slots.size() && !base; ++k)
					if (auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(slots[(t.id * 7 + k) % slots.size()].baseId)) base = npc;
			}
			if (!base) continue;
			auto ref = mk->PlaceObjectAtMe(base, true);
			auto* a = ref ? ref->As<RE::Actor>() : nullptr;
			if (!a) continue;
			a->AddObjectToContainer(gSatchel, nullptr, 1, nullptr);
			a->KillImmediate();
			t.corpse = a->GetFormID();
			Log::Line("[task] recover: body " + std::to_string(t.corpse) + " laid out at " + n->name);
			PlaceWork(t.slot, t);
		}
	}

	void Apply(const std::vector<TaskEvent>& ev)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (auto* party = gT.party; party && !gT.partyEvents.empty()) {   // job companions joined or left (escort, a freed captive)
			const auto pev = std::move(gT.partyEvents);
			gT.partyEvents.clear();
			Companions::ApplyJobEvents(pev);
		}
		for (auto& e : ev) {
			const Task& t = e.task;
			const std::string what = Tasks::Objective(t, false);
			switch (e.kind) {
			case TaskEvent::Kind::Progress:   // the next wave / mark: new text and target on the same objective
				Show(t, false);
				Notify(t.type == TaskType::Hunt ? "Next mark: " + what : "Another wave: " + what);
				break;
			case TaskEvent::Kind::Accepted:
				Papyrus("Begin", gQuest);
				if (t.type == TaskType::Delivery && pc && gParcel) pc->AddObjectToContainer(gParcel, nullptr, 1, nullptr);
				Show(t, false);
				Notify("New job: " + what + " (" + std::to_string(t.gold) + " gold)");
				break;
			case TaskEvent::Kind::Ready:
				Mark(t.slot, 2);
				Show(t, true);
				Notify("Job ready: return to " + t.giverName);
				break;
			case TaskEvent::Kind::Reopened:
				Papyrus("Mark", gQuest, 10 * t.slot + 1, 0);
				Show(t, false);
				break;
			case TaskEvent::Kind::Done:
			case TaskEvent::Kind::Failed: {
				const bool done = e.kind == TaskEvent::Kind::Done;
				std::int32_t gold = e.gold;
				if (done && gold > 0) StashPlugin::OnTaskDone(gold, t);   // a job may pay with a stash instead (docs/STASHES.md)
				Mark(t.slot, done ? 2 : 3);
				RemoveBody(t);
				if (pc && e.takeItem && e.takeCount > 0)
					if (auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(e.takeItem))
						pc->RemoveItem(obj, e.takeCount, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				if (pc && gold > 0 && Forms::R().gold) pc->AddObjectToContainer(Forms::R().gold, nullptr, gold, nullptr);
				if (e.goodwill != 0.f) Rep::S().OnTask(t.giverFaction, e.goodwill);
				if (e.memory != 0.f)
					if (Group* g = Sim::FindGroup(t.giver)) g->memory = std::clamp(g->memory + e.memory, -1000.f, 1000.f);
				if (done) {
					GiveConsumable(t.giver);
					Notify("Job done: " + what + (gold > 0 || e.gold == 0 ? ". Paid " + std::to_string(gold) + " gold" : ". Paid with a stash"));
				} else {
					Notify("Job failed (" + e.why + "): " + what);
				}
				break;
			}
			}
		}
	}

	void RunChoices()
	{
		if (gPending.empty() || Talk::InConversation()) return;
		std::vector<TaskEvent> ev;
		const double now = Sim::Hours();
		for (auto& p : gPending) {
			switch (p.c) {
			case Talk::Choice::Accept:
				if (!gT.Accept(Sim::B(), p.squad, now, gHost, ev)) Notify("The job is no longer on offer.");
				break;
			case Talk::Choice::TurnIn: gT.TurnIn(Sim::B(), p.squad, now, gHost, ev); break;
			case Talk::Choice::Deliver: gT.Deliver(p.squad, now, gHost, ev); break;
			case Talk::Choice::Abandon: gT.Abandon(Sim::B(), p.squad, now, ev); break;
			default: break;   // asking for work or turning it down changes nothing
			}
		}
		gPending.clear();
		Apply(ev);
	}

	void RestoreJournal()   // after a load: the objective texts are not in the save
	{
		gRestore = false;
		gShown.fill(0);
		gT.Rebind(Sim::B());
		for (auto& t : gT.active) {
			if (t.slot < 0 || t.slot >= kMaxSlots) continue;
			const bool ret = t.stage == TaskStage::Ready && t.type != TaskType::Delivery;
			SetText(10 * t.slot, Tasks::Objective(t, false));
			SetText(10 * t.slot + 1, Tasks::Objective(t, true));
			gShown[t.slot] = ret ? 2 : 1;
		}
		MoveMarkers();
		Log::Line("[task] journal restored: " + std::to_string(gT.active.size()) + " jobs");
	}

	Group* NearestGroup(bool a_human)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) return nullptr;
		const auto p = pc->GetPosition();
		Group* best = nullptr;
		float bd = 1e30f;
		for (auto& g : Sim::Groups()) {
			if (g.livingCount() == 0 || (a_human && !IsHumanFaction(g.faction))) continue;
			const float d = (g.x - p.x) * (g.x - p.x) + (g.y - p.y) * (g.y - p.y);
			if (d < bd) { bd = d; best = &g; }
		}
		return best;
	}
}

bool Ready() { return gReady; }

void Init()
{
	gReady = false;
	gOffer = gState = nullptr;
	gQuest = nullptr;
	gParcel = nullptr;
	gSatchel = nullptr;
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh || !dh->LookupModByName(kPlugin)) { Log::Line("[task] ALifeTalk.esp is not loaded: no jobs"); return; }
	Ltx talk, tl;
	std::string err;
	if (!talk.LoadFile(Settings::ConfigDir() + "/talk.ltx", &err) || !tl.LoadFile(Settings::ConfigDir() + "/tasks.ltx", &err)) {
		Log::Line("[task] config missing: " + err);
		return;
	}
	if (auto* g = dh->LookupForm(Hex(talk.Get("globals", "task_offer", "0")), kPlugin)) gOffer = g->As<RE::TESGlobal>();
	if (auto* g = dh->LookupForm(Hex(talk.Get("globals", "task_state", "0")), kPlugin)) gState = g->As<RE::TESGlobal>();
	if (auto* q = dh->LookupForm(Hex(talk.Get("tasks", "quest", "0")), kPlugin)) gQuest = q->As<RE::TESQuest>();
	if (auto* p = dh->LookupForm(Hex(talk.Get("tasks", "parcel", "0")), kPlugin)) gParcel = p->As<RE::TESBoundObject>();
	gSlots = std::clamp(talk.GetI("tasks", "slots", kMaxSlots), 1, kMaxSlots);
	if (!LoadTaskConfig(tl, gT.cfg, err)) { Log::Line("[task] tasks.ltx: " + err); return; }
	gT.cfg.maxActive = std::min(gT.cfg.maxActive, gSlots);
	gT.cfg.parcel = gParcel ? gParcel->GetFormID() : 0;
	// M5e: the satchel (talk.ltx from the ESP builder, else the tasks.ltx spec), the corpse lists, the party for job companions
	if (auto* s = dh->LookupForm(Hex(talk.Get("tasks", "satchel", "0")), kPlugin)) gSatchel = s->As<RE::TESBoundObject>();
	if (!gSatchel)
		if (const auto id = ResolveSpec(gT.cfg.satchelSpec)) gSatchel = RE::TESForm::LookupByID<RE::TESBoundObject>(id);
	gT.cfg.satchel = gSatchel ? gSatchel->GetFormID() : 0;
	if (!gSatchel) gT.cfg.satchelSpec.clear();   // no satchel: no recover jobs
	gCorpse.fill(nullptr);
	for (int f = 0; f < kFactionCount; ++f)
		if (const auto id = ResolveSpec(gT.cfg.corpse[f])) gCorpse[f] = RE::TESForm::LookupByID<RE::TESBoundObject>(id);
	gT.party = Companions::Model();
	int items = 0, missing = 0;
	for (auto& list : gT.cfg.fetch)
		for (auto& it : list) {
			it.form = ResolveSpec(it.spec);
			auto* f = it.form ? RE::TESForm::LookupByID(it.form) : nullptr;
			if (!f) { it.form = 0; ++missing; continue; }
			if (const char* n = f->GetName(); n && *n) it.name = n;
			++items;
		}
	gReady = gOffer && gState && gQuest && gParcel;
	Log::Line(std::string("[task] ") + (gReady ? "ready" : "NOT ready") + ": quest " + (gQuest ? "ok" : "missing") + ", globals " +
			  (gOffer && gState ? "ok" : "missing") + ", parcel " + (gParcel ? "ok" : "missing") + ", satchel " + (gSatchel ? "ok" : "missing (no recover jobs)") +
			  ", fetch items " + std::to_string(items) +
			  (missing ? " (" + std::to_string(missing) + " not found)" : ""));
}

void Reset()
{
	gT.Reset();
	gT.partyEvents.clear();
	gMarker.fill(0);
	gShown.fill(0);
	gPending.clear();
	gHide.clear();
	gRestore = false;
	if (gOffer) gOffer->value = 0.f;
	if (gState) gState->value = 0.f;
}

void PublishFor(const Group* g)
{
	if (!gReady) return;
	gT.scale = TaskScaleFrom(Sim::Cfg());   // T21 difficulty: Diff(Tasks) / Diff(Economy), read here and in Tick / Debug
	int offer = 0, state = 0;
	if (g && Sim::BoardReady()) {
		state = gT.StateFor(g->id, gHost);
		if (state == kTaskNone) offer = int(gT.OfferOf(Sim::B(), Graph::W(), *g, Sim::Hours(), gHost, gRng));
	}
	gOffer->value = float(offer);
	gState->value = float(state);
}

bool Holds(std::uint64_t squad) { return gT.IsTarget(squad) || gT.IsPinned(squad); }

void OnChoice(std::uint64_t squad, Talk::Choice c)
{
	gPending.push_back({ squad, c });
}

void Tick(float dt)
{
	if (!gReady || !Sim::BoardReady()) return;
	gT.scale = TaskScaleFrom(Sim::Cfg());
	if (gRestore) RestoreJournal();
	RunChoices();
	if ((gUpdate += dt) >= 1.f) {
		gUpdate = 0.f;
		std::vector<TaskEvent> ev;
		gT.Update(Sim::B(), Sim::Hours(), gHost, ev);
		Apply(ev);
		LayOutBodies();
	}
	if ((gMove += dt) >= 2.f) {
		gMove = 0.f;
		MoveMarkers();
	}
	for (std::size_t i = 0; i < gHide.size();) {
		if ((gHide[i].left -= dt) <= 0.f) {
			Papyrus("Mark", gQuest, gHide[i].index, 0);
			gHide.erase(gHide.begin() + std::ptrdiff_t(i));
		} else ++i;
	}
}

// ---------------------------------------------------------------- co-save
void SaveTo(SKSE::SerializationInterface* a)
{
	std::vector<std::uint8_t> b;
	gT.Save(b);
	for (auto id : gMarker) { auto* p = reinterpret_cast<const std::uint8_t*>(&id); b.insert(b.end(), p, p + sizeof id); }
	if (!a->WriteRecord(kRec, Tasks::kSaveVersion, b.data(), std::uint32_t(b.size()))) Log::Line("[task] FAILED to write the co-save record");
}

void LoadFrom(SKSE::SerializationInterface* a, std::uint32_t ver, std::uint32_t len)
{
	std::vector<std::uint8_t> b(len);
	if (a->ReadRecordData(b.data(), len) != len) return;
	std::size_t used = 0;
	if (!gT.Load(b.data(), b.size(), ver, &used)) { Log::Line("[task] co-save record v" + std::to_string(ver) + " skipped: no jobs"); return; }
	for (std::size_t k = 0; k < gMarker.size(); ++k) {
		RE::FormID id = 0;
		if (used + sizeof id <= b.size()) std::memcpy(&id, b.data() + used, sizeof id);
		used += sizeof id;
		RE::FormID out = 0;
		gMarker[k] = id && a->ResolveFormID(id, out) ? out : 0;
	}
	auto fix = [&](Task& t) {
		RE::FormID out = 0;
		t.item = t.item && a->ResolveFormID(t.item, out) ? out : 0;
		out = 0;
		t.corpse = t.corpse && a->ResolveFormID(t.corpse, out) ? out : 0;   // recover: the body (a new one is laid out when it is gone)
	};
	for (auto& t : gT.active) fix(t);
	for (auto& [id, o] : gT.offers) fix(o.task);
	if (Sim::BoardReady()) gT.Rebind(Sim::B());   // pinned / forced / held are not saved (SIM2 is read before TASK)
	gRestore = true;
	Log::Line("[task] loaded " + std::to_string(gT.active.size()) + " jobs, " + std::to_string(gT.offers.size()) + " offers");
}

// ---------------------------------------------------------------- debug bridge
void Debug(const std::string& a_args)
{
	std::lock_guard l(Sim::Lock());
	if (!gReady || !Sim::BoardReady()) { Log::Line("[task] debug: not ready"); return; }
	gT.scale = TaskScaleFrom(Sim::Cfg());
	const auto parts = SplitList(a_args, ' ');
	const std::string verb = parts.empty() ? "list" : parts[0];
	const double now = Sim::Hours();
	if (verb == "list") {
		for (auto& t : gT.active)
			Log::Line("[task] slot " + std::to_string(t.slot) + " " + TaskTypeId(t.type) + " stage " + std::to_string(int(t.stage)) + " giver #" +
					  std::to_string(t.giver) + " target #" + std::to_string(t.target) + ": " + Tasks::Objective(t, false) + " / " + std::to_string(t.gold) + " gold");
		auto* player = RE::PlayerCharacter::GetSingleton();
		Log::Line("[task] " + std::to_string(gT.active.size()) + " jobs, " + std::to_string(gT.offers.size()) + " offers, " + std::to_string(gT.cooldown.size()) +
				  " cooldowns; player gold " + std::to_string(player ? player->GetGoldAmount(true) : -1) + ", goodwill imperial " +
				  std::to_string(int(Rep::S().state.goodwill[int(Faction::Imperial)])));
		// what the game shows: the quest's objectives, the player's objective list (journal) and quest targets (compass / map)
		std::string objs;
		if (gQuest)
			for (auto* o : gQuest->objectives)
				if (o && o->state.underlying() != 0) objs += " " + std::to_string(o->index) + "=" + std::to_string(int(o->state.underlying())) + " '" + o->displayText.c_str() + "'";
		int journal = 0, targets = 0;
		if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
			auto& rt = pc->GetPlayerRuntimeData();
			for (auto& io : rt.objectives) if (io.Objective && io.Objective->ownerQuest == gQuest) ++journal;
			if (auto it = rt.questTargets.find(gQuest); it != rt.questTargets.end() && it->second) targets = int(it->second->size());
		}
		Log::Line(std::string("[task] quest ") + (gQuest && gQuest->IsRunning() ? "running" : "not running") + ", objectives:" + (objs.empty() ? " none" : objs) +
				  "; player journal entries " + std::to_string(journal) + ", quest targets " + std::to_string(targets));
		for (int k = 0; k < 2 * kMaxSlots; ++k) {
			auto* mk = gMarker[k] ? RE::TESForm::LookupByID<RE::TESObjectREFR>(gMarker[k]) : nullptr;
			if (!mk) continue;
			const auto p = mk->GetPosition();
			auto h = gQuest ? gQuest->GetAliasedRef(std::uint32_t(k)) : RE::ObjectRefHandle{};
			char b[200];
			std::snprintf(b, sizeof b, "[task] marker %d %08X at %.0f,%.0f,%.0f ws %d, alias %s", k, gMarker[k], p.x, p.y, p.z, Forms::WsIndex(mk->GetWorldspace()),
						  h.get().get() == mk ? "filled" : "empty");
			Log::Line(b);
		}
		return;
	}
	if (verb == "offer") {   // "task offer bounty": the debug scenario squad (else the nearest human squad) gets that offer now
		Group* g = Sim::DebugScenarioGroup() ? Sim::FindGroup(Sim::DebugScenarioGroup()) : nullptr;
		if (!g) g = NearestGroup(true);
		const TaskType ty = parts.size() > 1 ? TaskTypeFromId(parts[1]) : TaskType::Bounty;
		if (!g) { Log::Line("[task] debug: no squad"); return; }
		const TaskType got = gT.ForceOffer(Sim::B(), Graph::W(), *g, ty, now, gHost, gRng);
		const Task* o = gT.Offered(g->id);
		Log::Line("[task] debug: squad #" + std::to_string(g->id) + " (" + FactionId(g->faction) + ") offers " + TaskTypeId(got) +
				  (o ? ": " + Tasks::Objective(*o, false) : std::string()));
		return;
	}
	if (verb == "accept") {   // bypass the dialogue: accept the scenario (else nearest) squad's offer
		Group* g = Sim::DebugScenarioGroup() ? Sim::FindGroup(Sim::DebugScenarioGroup()) : nullptr;
		if (!g || !gT.Offered(g->id)) g = NearestGroup(true);
		std::vector<TaskEvent> ev;
		if (!g || !gT.Accept(Sim::B(), g->id, now, gHost, ev)) { Log::Line("[task] debug: nothing to accept"); return; }
		Apply(ev);
		return;
	}
	if (verb == "summon") {   // the open bounty's target squad comes on screen near the player
		for (auto& t : gT.active) {
			if (t.type != TaskType::Bounty || t.stage != TaskStage::Open) continue;
			Group* g = Sim::FindGroup(t.target);
			if (!g) continue;
			if (!g->online && Online::AtBudget())   // the on-screen budget is full: the furthest bystander steps off screen (never a companion)
				Online::MakeRoom({ t.giver, t.target }, "roomForSummon");
			const bool ok = g->online || Online::BringOnlineEx(*g, true, true);
			Log::Line("[task] debug: bounty target squad #" + std::to_string(g->id) + (ok ? " is on screen" : " could not come on screen"));
			return;
		}
		Log::Line("[task] debug: no open bounty");
		return;
	}
	if (verb == "kill") {   // the open bounty's target / the hunt's current mark dies where it stands (on screen: killed in the world, then the
		                    // rest of its squad), the defend wave or the rescue's captors die whole
		auto killOne = [](Group& g, Member& m) {
			if (!m.alive()) return;
			RE::Actor* a = g.online ? (IsHumanFaction(g.faction) ? Pool::ActorOf(m) : Creatures::ActorOf(m)) : nullptr;
			if (a && !a->IsDead()) a->KillImmediate();
			else if (!a) { m.health = 0.f; Sim::OnGroupMemberDied(g, m); }
			Log::Line("[task] debug: killed " + m.name + (a ? " in the world" : " offline"));
		};
		auto killAll = [&](std::uint64_t id) {
			if (Group* g = Sim::FindGroup(id)) for (auto& m : g->members) killOne(*g, m);
		};
		for (auto& t : gT.active) {
			if (t.stage != TaskStage::Open) continue;
			if (t.type == TaskType::Bounty || t.type == TaskType::Hunt) {
				Group* g = Sim::FindGroup(t.target);
				if (!g) continue;
				for (auto& m : g->members)
					if (m.id == t.targetMember && m.alive()) {
						const bool online = g->online;
						killOne(*g, m);
						if (online) killAll(g->id);   // the rest too: no fight with the giver's squad
						return;
					}
			} else if (t.type == TaskType::Defend && t.extra) {
				killAll(t.extra);
				return;
			} else if (t.type == TaskType::Rescue) {
				for (auto id : t.pinned) killAll(id);
				return;
			}
		}
		Log::Line("[task] debug: no open bounty, hunt, defend or rescue");
		return;
	}
	if (verb == "rank") {   // the top 20 of the rankings (pda.script: most dangerous people)
		const auto r = gT.Rankings(Sim::B());
		Log::Line("[task] rankings: " + std::to_string(r.size()) + " people; most dangerous in Skyrim:");
		for (std::size_t i = 0; i < r.size() && i < 20; ++i) {
			char b[200];
			std::snprintf(b, sizeof b, "[task] #%zu %s (%s, level %d, squad #%llu, ws %d) %.0f", i + 1, r[i].name.c_str(), FactionName(r[i].faction), r[i].level,
						  static_cast<unsigned long long>(r[i].squad), r[i].ws, r[i].score);
			Log::Line(b);
		}
		return;
	}
	if (verb == "where") {   // marker FormIDs, for "console player.moveto <id>"
		for (std::size_t k = 0; k < gMarker.size(); ++k) {
			char b[64];
			std::snprintf(b, sizeof b, "[task] marker %zu = %08X", k, gMarker[k]);
			Log::Line(b);
		}
		return;
	}
	Log::Line("[task] debug: unknown '" + a_args + "' (list, offer <type>, accept, summon, kill, rank, where)");
}
}
