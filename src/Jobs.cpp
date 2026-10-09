// Jobs: the Skyrim side of the task model (docs/TASKS.md). The core (src/core/Tasks.cpp) decides; this file publishes the
// dialogue globals, runs the ALifeTasks quest (objectives, aliases, markers), moves money, items and goodwill, and saves.
#include "ALife/Jobs.h"

#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Ltx.h"
#include "ALife/Rep.h"
#include "ALife/Settings.h"
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
		if (t.type == TaskType::Bounty || t.type == TaskType::Delivery) return Sim::FindGroup(t.target);
		if (t.type == TaskType::Assault)
			for (auto id : t.pinned)
				if (const Group* g = Sim::FindGroup(id); g && g->livingCount() > 0) return g;
		return nullptr;
	}

	void MoveMarkers()
	{
		for (auto& t : gT.active) {
			if (t.slot < 0 || t.slot >= gSlots) continue;
			if (t.type == TaskType::Assault) Place(t.slot, nullptr, t.node);   // the place itself
			else if (t.type != TaskType::Fetch) Place(t.slot, TargetGroup(t), t.node);
			Place(kMaxSlots + t.slot, Sim::FindGroup(t.giver), t.node);
		}
	}

	void Show(const Task& t, bool a_return)
	{
		const int s = t.slot;
		const int index = 10 * s + (a_return ? 1 : 0);
		const int alias = a_return ? kMaxSlots + s : s;
		SetText(index, Tasks::Objective(t, a_return));
		if (a_return) Place(alias, Sim::FindGroup(t.giver), t.node);
		else if (t.type == TaskType::Fetch) Place(alias, Sim::FindGroup(t.giver), t.node);   // fetch: the items are anywhere, the giver is the place to go
		else if (t.type == TaskType::Assault) Place(alias, nullptr, t.node);
		else Place(alias, TargetGroup(t), t.node);
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

	void Apply(const std::vector<TaskEvent>& ev)
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		for (auto& e : ev) {
			const Task& t = e.task;
			const std::string what = Tasks::Objective(t, false);
			switch (e.kind) {
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
				Mark(t.slot, done ? 2 : 3);
				if (pc && e.takeItem && e.takeCount > 0)
					if (auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(e.takeItem))
						pc->RemoveItem(obj, e.takeCount, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				if (pc && e.gold > 0 && Forms::R().gold) pc->AddObjectToContainer(Forms::R().gold, nullptr, e.gold, nullptr);
				if (e.goodwill != 0.f) Rep::S().OnTask(t.giverFaction, e.goodwill);
				if (e.memory != 0.f)
					if (Group* g = Sim::FindGroup(t.giver)) g->memory = std::clamp(g->memory + e.memory, -1000.f, 1000.f);
				if (done) {
					GiveConsumable(t.giver);
					Notify("Job done: " + what + ". Paid " + std::to_string(e.gold) + " gold");
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
			case Talk::Choice::TurnIn: gT.TurnIn(p.squad, now, gHost, ev); break;
			case Talk::Choice::Deliver: gT.Deliver(p.squad, now, gHost, ev); break;
			case Talk::Choice::Abandon: gT.Abandon(p.squad, now, ev); break;
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
			  (gOffer && gState ? "ok" : "missing") + ", parcel " + (gParcel ? "ok" : "missing") + ", fetch items " + std::to_string(items) +
			  (missing ? " (" + std::to_string(missing) + " not found)" : ""));
}

void Reset()
{
	gT.Reset();
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
	int offer = 0, state = 0;
	if (g && Sim::BoardReady()) {
		state = gT.StateFor(g->id, gHost);
		if (state == kTaskNone) offer = int(gT.OfferOf(Sim::B(), Graph::W(), *g, Sim::Hours(), gHost, gRng));
	}
	gOffer->value = float(offer);
	gState->value = float(state);
}

void OnChoice(std::uint64_t squad, Talk::Choice c)
{
	gPending.push_back({ squad, c });
}

void Tick(float dt)
{
	if (!gReady || !Sim::BoardReady()) return;
	if (gRestore) RestoreJournal();
	RunChoices();
	if ((gUpdate += dt) >= 1.f) {
		gUpdate = 0.f;
		std::vector<TaskEvent> ev;
		gT.Update(Sim::B(), Sim::Hours(), gHost, ev);
		Apply(ev);
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
	auto fix = [&](Task& t) { RE::FormID out = 0; t.item = t.item && a->ResolveFormID(t.item, out) ? out : 0; };
	for (auto& t : gT.active) fix(t);
	for (auto& [id, o] : gT.offers) fix(o.task);
	gRestore = true;
	Log::Line("[task] loaded " + std::to_string(gT.active.size()) + " jobs, " + std::to_string(gT.offers.size()) + " offers");
}

// ---------------------------------------------------------------- debug bridge
void Debug(const std::string& a_args)
{
	std::lock_guard l(Sim::Lock());
	if (!gReady || !Sim::BoardReady()) { Log::Line("[task] debug: not ready"); return; }
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
			if (!g->online && Sim::GroupsOnline() >= kMaxOnlineGroups)   // every on-screen slot taken: a bystander squad steps off screen
				for (auto& o : Sim::Groups())
					if (o.online && o.id != t.giver && o.id != t.target) {
						Log::Line("[task] debug: squad #" + std::to_string(o.id) + " sent off screen to make room");
						Online::SendOffline(o, false);
						break;
					}
			const bool ok = g->online || Online::BringOnlineEx(*g, true, true);
			Log::Line("[task] debug: bounty target squad #" + std::to_string(g->id) + (ok ? " is on screen" : " could not come on screen"));
			return;
		}
		Log::Line("[task] debug: no open bounty");
		return;
	}
	if (verb == "kill") {   // the open bounty's target dies where it stands (on screen: killed in the world), then the rest of its squad
		for (auto& t : gT.active) {
			if (t.type != TaskType::Bounty || t.stage != TaskStage::Open) continue;
			Group* g = Sim::FindGroup(t.target);
			if (!g) continue;
			for (auto& m : g->members) {
				if (m.id != t.targetMember || !m.alive()) continue;
				if (auto* a = g->online ? Pool::ActorOf(m) : nullptr) {
					a->KillImmediate();
					Log::Line("[task] debug: killed " + m.name + " in the world");
					for (auto& o : g->members)   // the rest too: no fight with the giver's squad
						if (o.id != m.id && o.alive())
							if (auto* b = Pool::ActorOf(o); b && !b->IsDead()) b->KillImmediate();
				} else {
					m.health = 0.f;
					Sim::OnGroupMemberDied(*g, m);
					Log::Line("[task] debug: killed " + m.name + " offline");
				}
				return;
			}
		}
		Log::Line("[task] debug: no open bounty");
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
	Log::Line("[task] debug: unknown '" + a_args + "' (list, offer <type>, accept, summon, kill, where)");
}
}
