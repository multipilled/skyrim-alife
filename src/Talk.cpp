#include "ALife/Talk.h"

#include "ALife/Forms.h"
#include "ALife/Jobs.h"
#include "ALife/Ltx.h"
#include "ALife/Log.h"
#include "ALife/Rep.h"
#include "ALife/Settings.h"

namespace ALife::Talk {
namespace {
	constexpr std::string_view kTalkPlugin = "ALifeTalk.esp"sv;

	bool gReady{ false };
	RE::TESGlobal* gMode{ nullptr };
	RE::TESGlobal* gToll{ nullptr };
	std::unordered_map<RE::FormID, Choice> gChoice;                 // runtime response FormID -> what choosing it means
	std::map<std::pair<int, int>, std::vector<std::string>> gOpen;  // (mode, faction) -> opening lines

	struct Shown
	{
		RE::ObjectRefHandle speaker;
		std::string text;
		float left{ 0 };
	};
	std::vector<Shown> gShown;
	float gHold{ 0 };
	float gTopicsAt{ -1 };   // debug: when to list the topics the open conversation offers

	void LogTopics()
	{
		auto* mtm = RE::MenuTopicManager::GetSingleton();
		if (!mtm || !mtm->menuOpen) { Log::Line("[talk] topics: no conversation is open"); return; }
		std::string all;
		int n = 0;
		if (mtm->dialogueList)
			for (auto* d : *mtm->dialogueList) { if (d) { all += std::string("'") + d->topicText.c_str() + "' "; ++n; } }
		Log::Line("[talk] topics offered (" + std::to_string(n) + "): " + all);
	}

	Choice ChoiceOf(const std::string& a)
	{
		if (a == "pay") return Choice::Pay;
		if (a == "poor") return Choice::Poor;
		if (a == "refuse") return Choice::Refuse;
		if (a == "fight") return Choice::Fight;
		if (a == "peace") return Choice::Peace;
		if (a == "defy") return Choice::Defy;
		if (a == "news") return Choice::News;
		if (a == "trade") return Choice::Trade;
		if (a == "work") return Choice::Work;
		if (a == "accept") return Choice::Accept;
		if (a == "decline") return Choice::Decline;
		if (a == "turnin") return Choice::TurnIn;
		if (a == "deliver") return Choice::Deliver;
		if (a == "abandon") return Choice::Abandon;
		return Choice::None;
	}

	RE::FormID Hex(const std::string& s)
	{
		try { return RE::FormID(std::stoul(s, nullptr, 16)); } catch (...) { return 0; }
	}

	void Withdraw(const Shown& s)
	{
		auto* sm = RE::SubtitleManager::GetSingleton();
		if (!sm) return;
		RE::BSSpinLockGuard guard(sm->lock);
		for (std::uint32_t i = 0; i < sm->subtitles.size(); ++i) {
			auto& e = sm->subtitles[i];
			if (e.speaker == s.speaker && s.text == e.subtitle.c_str()) {
				sm->subtitles.erase(sm->subtitles.begin() + i);
				return;
			}
		}
	}
}

bool Ready() { return gReady; }

namespace { float gSinceConversation{ 1.0e9f }; bool gWasOpen{ false }; }
float SecondsSinceConversation() { return gSinceConversation; }

void Init()
{
	gReady = false;
	gMode = gToll = nullptr;
	gChoice.clear();
	gOpen.clear();
	gShown.clear();
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh || !dh->LookupModByName(kTalkPlugin)) { Log::Line("[talk] ALifeTalk.esp is not loaded: squads cannot be talked to"); return; }
	Ltx t;
	std::string err;
	if (!t.LoadFile(Settings::ConfigDir() + "/talk.ltx", &err)) { Log::Line("[talk] talk.ltx missing: " + err); return; }
	if (auto* g = dh->LookupForm(Hex(t.Get("globals", "mode", "0")), kTalkPlugin)) gMode = g->As<RE::TESGlobal>();
	if (auto* g = dh->LookupForm(Hex(t.Get("globals", "toll", "0")), kTalkPlugin)) gToll = g->As<RE::TESGlobal>();
	if (auto* lines = t.Section("responses"))
		for (auto& kv : *lines) {
			const RE::FormID id = dh->LookupFormID(Hex(kv.first), kTalkPlugin);
			const Choice c = ChoiceOf(kv.second);
			if (id && c != Choice::None) gChoice[id] = c;
		}
	if (auto* lines = t.Section("open"))
		for (auto& kv : *lines) {
			const auto dot = kv.first.find('.');
			if (dot == std::string::npos) continue;
			bool ok = false;
			const Faction f = FactionFromId(kv.first.substr(dot + 1), &ok);
			if (!ok) continue;
			int mode = 0;
			try { mode = std::stoi(kv.first.substr(0, dot)); } catch (...) {}
			gOpen[{ mode, int(f) }] = SplitList(kv.second, '|');
		}
	gReady = gMode && gToll && !gChoice.empty();
	Log::Line(std::string("[talk] ") + (gReady ? "ready" : "NOT ready") + ": " + std::to_string(gChoice.size()) + " responses, " + std::to_string(gOpen.size()) +
			  " opening-line sets, globals " + (gMode && gToll ? "ok" : "missing"));
}

void Reset()
{
	for (auto& s : gShown) Withdraw(s);
	gShown.clear();
	Publish(kNone, 0);
}

void Publish(int a_mode, int a_toll)
{
	if (gMode) gMode->value = float(a_mode);
	if (gToll) gToll->value = float(a_toll);
}

void Hold(float a_seconds) { gHold = a_seconds; }

void Diagnose(RE::Actor* a)
{
	if (!a) return;
	std::string in;
	const auto& r = Forms::R();
	for (int i = 0; i < kFactionCount; ++i)
		if (r.fFaction[i] && a->IsInFaction(r.fFaction[i])) in += std::string(FactionId(Faction(i))) + " ";
	char b[240];
	std::snprintf(b, sizeof b, "[talk] diag: mode=%g toll=%g, speaker %s base %08X, ALife factions: %s, blocked %s, canTalk %s", gMode ? gMode->value : -1.f,
				  gToll ? gToll->value : -1.f, a->GetName(), a->GetActorBase() ? a->GetActorBase()->GetFormID() : 0, in.empty() ? "none" : in.c_str(),
				  a->IsActivationBlocked() ? "yes" : "no", a->CanTalkToPlayer() ? "yes" : "no");
	Log::Line(b);
}

void DebugChoose(const std::string& a_choice)
{
	auto* q = RE::BSInputEventQueue::GetSingleton();
	auto* ue = RE::UserEvents::GetSingleton();
	auto* mtm = RE::MenuTopicManager::GetSingleton();
	if (!q || !ue || !mtm || !mtm->menuOpen) return;
	static const std::map<std::string, std::string> kPrompt{ { "pay", "Pay the toll" }, { "poor", "have that much" }, { "refuse", "not paying" }, { "fight", "take it" },
															 { "peace", "mean no harm" }, { "defy", "answer to you" }, { "news", "word on the road" }, { "trade", "what you have for sale" },
														 { "work", "any work" }, { "accept", "do it" }, { "decline", "Not right now" }, { "turnin", "It's done" },
														 { "deliver", "delivery for you" }, { "abandon", "can't finish" } };
	int a_down = 0;
	if (auto it = kPrompt.find(a_choice); it != kPrompt.end() && mtm->dialogueList) {
		int i = 0;
		bool found = false;
		for (auto* d : *mtm->dialogueList) {
			if (d && std::string(d->topicText.c_str()).find(it->second) != std::string::npos) { a_down = i; found = true; break; }
			++i;
		}
		if (!found) { Log::Line("[talk] debug: '" + a_choice + "' is not offered"); LogTopics(); return; }
	}
	constexpr std::uint32_t kEnter = 0x1C, kDown = 0xD0;
	for (int i = 0; i < a_down; ++i) {
		q->PushOntoInputQueue(RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, ue->down, kDown, 1.f, 0.f));
		q->PushOntoInputQueue(RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, ue->down, kDown, 0.f, 0.1f));
	}
	q->PushOntoInputQueue(RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, ue->accept, kEnter, 1.f, 0.f));
	q->PushOntoInputQueue(RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, ue->accept, kEnter, 0.f, 0.1f));
	Log::Line("[talk] debug: moved down " + std::to_string(a_down) + " and accepted the topic");
}

void OpenBarter(RE::Actor* a)
{
	auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
	if (!a || !factory) return;
	if (auto* script = factory->Create()) {   // the console's own command: the barter menu with this actor, over what it really carries
		script->SetCommand("ShowBarterMenu");
		script->CompileAndRun(a);
		delete script;
		Log::Line(std::string("[talk] barter with ") + a->GetName());
	}
}

bool InConversation()
{
	auto* mtm = RE::MenuTopicManager::GetSingleton();
	return mtm && mtm->menuOpen;
}

std::uint64_t ConversationSquad()
{
	auto* mtm = RE::MenuTopicManager::GetSingleton();
	if (!mtm || !mtm->menuOpen) return 0;
	auto ref = mtm->speaker.get();
	auto* a = ref ? ref->As<RE::Actor>() : nullptr;
	if (!a) return 0;
	std::lock_guard l(Sim::Lock());
	auto* g = Sim::GroupOfActor(a);
	return g ? g->id : 0;
}

void Pump(float dt)
{
	{   // how long ago the player last finished talking to anyone: squads do not queue up to stop a player who has just been interrupted
		const bool open = InConversation();
		if (open) gSinceConversation = 0.f;
		else {
			if (gWasOpen) gSinceConversation = 0.f;
			gSinceConversation += dt;
		}
		gWasOpen = open;
	}
	if (!gReady) return;
	// spoken lines fade after a few seconds, like any subtitle
	for (std::size_t i = 0; i < gShown.size();) {
		gShown[i].left -= dt;
		if (gShown[i].left <= 0.f) { Withdraw(gShown[i]); gShown.erase(gShown.begin() + std::ptrdiff_t(i)); }
		else ++i;
	}
	if (Settings::Get().bDebug) {   // log when a conversation opens or closes, and what it offers
		static bool was = false;
		const bool now = InConversation();
		if (now != was) { was = now; Log::Line(std::string("[talk] conversation ") + (now ? "opened" : "closed")); gTopicsAt = now ? 1.5f : -1.f; }
		if (gTopicsAt >= 0.f && (gTopicsAt -= dt) < 0.f) LogTopics();
	}
	if (gHold > 0.f) { gHold -= dt; return; }
	if (InConversation()) return;   // the situation is fixed once a conversation has opened
	int mode = kNone, toll = 0;
	if (auto* ch = RE::CrosshairPickData::GetSingleton()) {
		auto ref = ch->GetActiveTarget().get();
		auto* a = ref ? ref->As<RE::Actor>() : nullptr;
		if (a && Pool::IsPoolActor(a)) {
			std::lock_guard l(Sim::Lock());
			if (auto* g = Sim::GroupOfActor(a)) {
				Behavior::TalkSituation(*g, mode, toll);
				Jobs::PublishFor(mode == kGreet ? g : nullptr);   // work is offered to a player the squad greets
				Publish(mode, toll);
				return;
			}
		}
	}
	Jobs::PublishFor(nullptr);
	Publish(mode, toll);
}

std::string OpeningLine(const Group& g, int mode)
{
	auto it = gOpen.find({ mode, int(g.faction) });
	if (it == gOpen.end() || it->second.empty()) return {};
	return it->second[std::size_t(Mix64(g.id * 7919ULL + std::uint64_t(mode)) % it->second.size())];
}

void Subtitle(RE::Actor* speaker, const std::string& text, float seconds)
{
	auto* sm = RE::SubtitleManager::GetSingleton();
	if (!speaker || !sm || text.empty()) return;
	RE::SubtitleInfo info{};
	info.speaker = speaker->GetHandle();
	info.subtitle = text.c_str();
	info.targetDistance = 0.f;
	info.forceDisplay = true;
	{
		RE::BSSpinLockGuard guard(sm->lock);
		sm->subtitles.push_back(info);
	}
	gShown.push_back({ info.speaker, text, seconds });
	Log::Line("[talk] says: " + text);
}

void OnTopicInfo(const RE::TESTopicInfoEvent* e)
{
	if (!e || !gReady) return;
	auto it = gChoice.find(e->topicInfoFormID);
	if (it == gChoice.end()) return;
	if (Settings::Get().bDebug) {
		auto* sp = e->speakerRef ? e->speakerRef->As<RE::Actor>() : nullptr;
		char b[160];
		std::snprintf(b, sizeof b, "[talk] topic event: our info %08X, speaker %s, type %d", e->topicInfoFormID, sp ? sp->GetName() : "?", int(e->type.get()));
		Log::Line(b);
	}
	auto* a = e->speakerRef ? e->speakerRef->As<RE::Actor>() : nullptr;
	if (!a) return;
	std::lock_guard l(Sim::Lock());
	auto* g = Sim::GroupOfActor(a);
	if (!g) return;
	const bool begin = e->type.get() == RE::TESTopicInfoEvent::TopicInfoEventType::kTopicBegin;
	Behavior::OnTalk(*g, it->second, begin);
	if (begin && it->second >= Choice::Work) Jobs::OnChoice(g->id, it->second);   // applied after the menu has closed
}
}
