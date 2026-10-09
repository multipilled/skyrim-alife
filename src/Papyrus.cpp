#include "ALife/Systems.h"
#include "ALife/Forms.h"
#include "ALife/Settings.h"
#include "ALife/Log.h"
#include "ALife/Rep.h"

// Natives used by ALife_MCM.psc. Papyrus runs on its own thread: anything that touches the
// simulation or the world is queued to the main thread with SKSE's task interface.
namespace ALife::Papyrus {
namespace {
	constexpr auto kScript = "ALife_MCM"sv;

	bool GetEnabled(RE::StaticFunctionTag*) { return Settings::Get().enabled; }
	void SetEnabled(RE::StaticFunctionTag*, bool a_on)
	{
		SKSE::GetTaskInterface()->AddTask([a_on] {
			Sim::SetEnabled(a_on);
			Settings::Save();
		});
	}
	float GetPopulationScale(RE::StaticFunctionTag*) { return Settings::Get().populationScale; }
	void SetPopulationScale(RE::StaticFunctionTag*, float a_v)
	{
		float v = std::clamp(std::round(a_v * 10.f) / 10.f, 0.5f, 1.5f);
		SKSE::GetTaskInterface()->AddTask([v] {
			Settings::Get().populationScale = v;
			Settings::Save();
			Sim::ApplySettings();
		});
	}
	int GetOfflineCombat(RE::StaticFunctionTag*) { return Settings::Get().ocsMode; }
	void SetOfflineCombat(RE::StaticFunctionTag*, int a_mode)
	{
		const int m = std::clamp(a_mode, 0, 2);
		SKSE::GetTaskInterface()->AddTask([m] {
			Settings::Get().ocsMode = m;
			Settings::Save();
			Sim::ApplySettings();
		});
	}
	bool GetNews(RE::StaticFunctionTag*) { return Settings::Get().news; }
	void SetNews(RE::StaticFunctionTag*, bool a_on)
	{
		SKSE::GetTaskInterface()->AddTask([a_on] {
			Settings::Get().news = a_on;
			Settings::Save();
			Sim::ApplySettings();
		});
	}
	bool GetDynamicRelations(RE::StaticFunctionTag*) { return Settings::Get().dynamicRelations; }
	void SetDynamicRelations(RE::StaticFunctionTag*, bool a_on)
	{
		SKSE::GetTaskInterface()->AddTask([a_on] {
			Settings::Get().dynamicRelations = a_on;
			Settings::Save();
			Sim::ApplySettings();
			Log::Line(std::string("[mcm] dynamic relations ") + (a_on ? "on" : "off"));
		});
	}
	std::int32_t GetTalkInitiative(RE::StaticFunctionTag*) { return Settings::Get().talkInitiative; }
	void SetTalkInitiative(RE::StaticFunctionTag*, std::int32_t a_mode)
	{
		SKSE::GetTaskInterface()->AddTask([a_mode] {
			Settings::Get().talkInitiative = std::clamp(a_mode, 0, 3);
			Settings::Save();
		});
	}
	bool GetLoot(RE::StaticFunctionTag*) { return Settings::Get().loot; }
	void SetLoot(RE::StaticFunctionTag*, bool a_on)
	{
		SKSE::GetTaskInterface()->AddTask([a_on] {
			Settings::Get().loot = a_on;
			Settings::Save();
		});
	}
	void PrepareUninstall(RE::StaticFunctionTag*)
	{
		SKSE::GetTaskInterface()->AddTask([] { Sim::RequestPrepareUninstall(); });
	}
	bool Ready(RE::StaticFunctionTag*) { return Forms::R().pluginLoaded && Graph::Ready(); }
	RE::BSFixedString Status(RE::StaticFunctionTag*) { return RE::BSFixedString(Sim::StatusLine().c_str()); }
	std::int32_t StandingCount(RE::StaticFunctionTag*) { return Rep::MenuCount(); }
	RE::BSFixedString StandingName(RE::StaticFunctionTag*, std::int32_t a_i) { return RE::BSFixedString(Rep::MenuName(a_i).c_str()); }
	RE::BSFixedString StandingValue(RE::StaticFunctionTag*, std::int32_t a_i) { return RE::BSFixedString(Rep::MenuValue(a_i).c_str()); }
	RE::BSFixedString StandingStatus(RE::StaticFunctionTag*) { return RE::BSFixedString(Rep::MenuStatus().c_str()); }
	std::int32_t NewsCount(RE::StaticFunctionTag*) { return Rep::NewsCount(); }
	RE::BSFixedString NewsLine(RE::StaticFunctionTag*, std::int32_t a_i) { return RE::BSFixedString(Rep::NewsLine(a_i).c_str()); }
}

bool Register(RE::BSScript::IVirtualMachine* vm)
{
	vm->RegisterFunction("ALifeGetEnabled"sv, kScript, GetEnabled);
	vm->RegisterFunction("ALifeSetEnabled"sv, kScript, SetEnabled);
	vm->RegisterFunction("ALifeGetPopulationScale"sv, kScript, GetPopulationScale);
	vm->RegisterFunction("ALifeSetPopulationScale"sv, kScript, SetPopulationScale);
	vm->RegisterFunction("ALifeGetOfflineCombat"sv, kScript, GetOfflineCombat);
	vm->RegisterFunction("ALifeSetOfflineCombat"sv, kScript, SetOfflineCombat);
	vm->RegisterFunction("ALifeGetNews"sv, kScript, GetNews);
	vm->RegisterFunction("ALifeSetNews"sv, kScript, SetNews);
	vm->RegisterFunction("ALifeGetDynamicRelations"sv, kScript, GetDynamicRelations);
	vm->RegisterFunction("ALifeSetDynamicRelations"sv, kScript, SetDynamicRelations);
	vm->RegisterFunction("ALifeGetTalkInitiative"sv, kScript, GetTalkInitiative);
	vm->RegisterFunction("ALifeSetTalkInitiative"sv, kScript, SetTalkInitiative);
	vm->RegisterFunction("ALifeGetLoot"sv, kScript, GetLoot);
	vm->RegisterFunction("ALifeSetLoot"sv, kScript, SetLoot);
	vm->RegisterFunction("ALifePrepareUninstall"sv, kScript, PrepareUninstall);
	vm->RegisterFunction("ALifeReady"sv, kScript, Ready);
	vm->RegisterFunction("ALifeStatus"sv, kScript, Status);
	vm->RegisterFunction("ALifeStandingCount"sv, kScript, StandingCount);
	vm->RegisterFunction("ALifeStandingName"sv, kScript, StandingName);
	vm->RegisterFunction("ALifeStandingValue"sv, kScript, StandingValue);
	vm->RegisterFunction("ALifeStandingStatus"sv, kScript, StandingStatus);
	vm->RegisterFunction("ALifeNewsCount"sv, kScript, NewsCount);
	vm->RegisterFunction("ALifeNewsLine"sv, kScript, NewsLine);
	return true;
}
}
