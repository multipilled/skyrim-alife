#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Settings.h"
#include "ALife/Systems.h"
#include "ALife/Talk.h"

namespace ALife::Events {
namespace {
	RE::Actor* AsActor(const RE::NiPointer<RE::TESObjectREFR>& p) { return p ? p->As<RE::Actor>() : nullptr; }

	// bDebug=1 hotkeys (DirectInput scan codes in ALife.ini [Debug]): status dump / fast-forward a day / place a hostile pair
	class InputSink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputSink* Get() { static InputSink s; return &s; }
		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_ev, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			const auto& st = Settings::Get();
			if (!st.bDebug || !a_ev) return RE::BSEventNotifyControl::kContinue;
			auto* ui = RE::UI::GetSingleton();
			if (!ui || ui->GameIsPaused() || ui->IsMenuOpen(RE::Console::MENU_NAME)) return RE::BSEventNotifyControl::kContinue;
			for (auto* e = *a_ev; e; e = e->next) {
				if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || e->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;
				auto* b = e->AsButtonEvent();
				if (!b || !b->IsDown()) continue;
				const std::uint32_t dik = b->GetIDCode();
				if (dik == std::uint32_t(st.iDebugKeyStatus)) SKSE::GetTaskInterface()->AddTask([] { Sim::DebugStatus(); });
				else if (dik == std::uint32_t(st.iDebugKeyFight)) SKSE::GetTaskInterface()->AddTask([] { Sim::DebugFastForward(); });
				else if (dik == std::uint32_t(st.iDebugKeyFillSticky)) SKSE::GetTaskInterface()->AddTask([] { Sim::DebugPlacePair(); });
				else if (dik == std::uint32_t(st.iDebugKeyGoto)) SKSE::GetTaskInterface()->AddTask([] { Sim::DebugGotoSquad(); });
				else if (dik == std::uint32_t(st.iDebugKeyScenario)) SKSE::GetTaskInterface()->AddTask([] { Sim::DebugScenario(); });
				else if (dik == std::uint32_t(st.iDebugKeyKill)) SKSE::GetTaskInterface()->AddTask([] { Sim::DebugKill(); });
				else if (dik == std::uint32_t(st.iDebugKeyTalk)) SKSE::GetTaskInterface()->AddTask([] { Sim::DebugTalk(); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	class GameSink :
		public RE::BSTEventSink<RE::TESHitEvent>,
		public RE::BSTEventSink<RE::TESCombatEvent>,
		public RE::BSTEventSink<RE::TESDeathEvent>,
		public RE::BSTEventSink<RE::TESTopicInfoEvent>
	{
	public:
		static GameSink* Get() { static GameSink s; return &s; }

		RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* e, RE::BSTEventSource<RE::TESHitEvent>*) override
		{
			if (!e) return RE::BSEventNotifyControl::kContinue;
			auto* tgt = AsActor(e->target);
			auto* src = AsActor(e->cause);
			if (!tgt || !src) return RE::BSEventNotifyControl::kContinue;
			Protect::OnHit(tgt, src);
			if (src->IsPlayerRef() && Pool::IsPoolActor(tgt)) {
				std::lock_guard l(Sim::Lock());
				if (auto* g = Sim::GroupOfActor(tgt)) Behavior::OnPlayerAttacked(*g);
			}
			return RE::BSEventNotifyControl::kContinue;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* e, RE::BSTEventSource<RE::TESCombatEvent>*) override
		{
			if (e && e->newState.any(RE::ACTOR_COMBAT_STATE::kCombat)) Protect::OnCombat(AsActor(e->actor), AsActor(e->targetActor));
			return RE::BSEventNotifyControl::kContinue;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESTopicInfoEvent* e, RE::BSTEventSource<RE::TESTopicInfoEvent>*) override
		{
			Talk::OnTopicInfo(e);
			return RE::BSEventNotifyControl::kContinue;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* e, RE::BSTEventSource<RE::TESDeathEvent>*) override
		{
			if (!e || !e->dead) return RE::BSEventNotifyControl::kContinue;
			auto* a = AsActor(e->actorDying);
			if (!a) return RE::BSEventNotifyControl::kContinue;
			std::lock_guard l(Sim::Lock());   // the creature table is the sim's
			if (!(Pool::IsPoolActor(a) || Creatures::IsCreature(a))) return RE::BSEventNotifyControl::kContinue;
			Sim::OnActorKilled(a, AsActor(e->actorKiller));
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

void RegisterEarly()
{
	if (auto* idm = RE::BSInputDeviceManager::GetSingleton()) idm->AddEventSink<RE::InputEvent*>(InputSink::Get());
}

void RegisterGame()
{
	auto* h = RE::ScriptEventSourceHolder::GetSingleton();
	if (!h) return;
	h->AddEventSink<RE::TESHitEvent>(GameSink::Get());
	h->AddEventSink<RE::TESCombatEvent>(GameSink::Get());
	h->AddEventSink<RE::TESDeathEvent>(GameSink::Get());
	h->AddEventSink<RE::TESTopicInfoEvent>(GameSink::Get());
}
}
