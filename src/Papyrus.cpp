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
	// difficulty sliders (docs/DIFFICULTY.md): channel index = DiffChannel order; population is the old density slider
	bool ValidChannel(std::int32_t c) { return c >= 0 && c < kDiffChannels; }
	float GetDiffScale(RE::StaticFunctionTag*, std::int32_t c)
	{
		return ValidChannel(c) ? ClampSlider(Sim::Cfg().diffCfg, DiffChannel(c), Settings::Get().diff.slider[c]) : 1.f;
	}
	void SetDiffScale(RE::StaticFunctionTag*, std::int32_t c, float a_v)
	{
		if (!ValidChannel(c)) return;
		const float v = ClampSlider(Sim::Cfg().diffCfg, DiffChannel(c), a_v);
		SKSE::GetTaskInterface()->AddTask([c, v] {
			Settings::Get().diff.slider[c] = v;
			Settings::Save();
			Sim::ApplySettings();
		});
	}
	float GetDiffMin(RE::StaticFunctionTag*, std::int32_t c) { return ValidChannel(c) ? Sim::Cfg().diffCfg.sliderMin[c] : 0.5f; }
	float GetDiffMax(RE::StaticFunctionTag*, std::int32_t c) { return ValidChannel(c) ? Sim::Cfg().diffCfg.sliderMax[c] : 1.5f; }
	RE::BSFixedString DiffEffectiveText(RE::StaticFunctionTag*, std::int32_t c)   // "1.35x" (strength: "+ size, levels, gear")
	{
		if (!ValidChannel(c)) return RE::BSFixedString("");
		const auto& d = Sim::Cfg().diff;
		char buf[96];
		if (DiffChannel(c) == DiffChannel::Strength)
			std::snprintf(buf, sizeof(buf), "%.2fx (size x%.2f, %+d levels, gear %+d)", d.Get(DiffChannel(c)), d.sizeMult, d.levelOffset, d.gearLevels);
		else
			std::snprintf(buf, sizeof(buf), "%.2fx", d.Get(DiffChannel(c)));
		return RE::BSFixedString(buf);
	}
	bool GetFollowGame(RE::StaticFunctionTag*) { return Settings::Get().diff.followGame; }
	void SetFollowGame(RE::StaticFunctionTag*, bool a_on)
	{
		SKSE::GetTaskInterface()->AddTask([a_on] {
			Settings::Get().diff.followGame = a_on;
			Settings::Save();
			Sim::ApplySettings();
		});
	}
	RE::BSFixedString GameDifficultyLabel(RE::StaticFunctionTag*) { return RE::BSFixedString(GameDifficultyName(Sim::GameDifficulty())); }
	float GetPopulationScale(RE::StaticFunctionTag* t) { return GetDiffScale(t, std::int32_t(DiffChannel::Population)); }
	void SetPopulationScale(RE::StaticFunctionTag* t, float a_v) { SetDiffScale(t, std::int32_t(DiffChannel::Population), a_v); }
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
	bool GetDragons(RE::StaticFunctionTag*) { return Settings::Get().dragons; }
	void SetDragons(RE::StaticFunctionTag*, bool a_on)
	{
		SKSE::GetTaskInterface()->AddTask([a_on] {
			Settings::Get().dragons = a_on;
			Settings::Save();
			CivilWar::Reset();   // the dragon gate is read again on the next tick (off: offline dragons retire, ones on screen are sent off first)
			Log::Line(std::string("[mcm] dragons ") + (a_on ? "on" : "off"));
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
	// patrol mods (docs/COMPAT.md): -1 auto, 0 off, 1-3 online groups
	std::int32_t GetCompatBudget(RE::StaticFunctionTag*) { return Settings::Get().compatBudget; }
	void SetCompatBudget(RE::StaticFunctionTag*, std::int32_t a_v)
	{
		const int v = std::clamp(a_v, -1, 3);
		SKSE::GetTaskInterface()->AddTask([v] {
			Settings::Get().compatBudget = v;
			Settings::Save();
			Sim::ApplySettings();
			const auto& c = Sim::Cfg().compat;
			Log::Line("[mcm] patrol mods budget " + std::to_string(v) + ": " + std::to_string(c.onlineGroups) + " groups / " +
					  std::to_string(c.onlineActors) + " actors");
		});
	}
	RE::BSFixedString CompatInfo(RE::StaticFunctionTag*)
	{
		const auto& cfg = Sim::Cfg();
		if (!cfg.compat.Active()) return RE::BSFixedString("none detected");
		std::string out;
		for (auto& id : cfg.compat.detected)
			for (auto& m : cfg.compatCfg.mods)
				if (m.id == id) out += (out.empty() ? "" : ", ") + m.name;
		return RE::BSFixedString(out.c_str());
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
	vm->RegisterFunction("ALifeGetDiffScale"sv, kScript, GetDiffScale);
	vm->RegisterFunction("ALifeSetDiffScale"sv, kScript, SetDiffScale);
	vm->RegisterFunction("ALifeGetDiffMin"sv, kScript, GetDiffMin);
	vm->RegisterFunction("ALifeGetDiffMax"sv, kScript, GetDiffMax);
	vm->RegisterFunction("ALifeDiffEffectiveText"sv, kScript, DiffEffectiveText);
	vm->RegisterFunction("ALifeGetFollowGame"sv, kScript, GetFollowGame);
	vm->RegisterFunction("ALifeSetFollowGame"sv, kScript, SetFollowGame);
	vm->RegisterFunction("ALifeGameDifficulty"sv, kScript, GameDifficultyLabel);
	vm->RegisterFunction("ALifeGetOfflineCombat"sv, kScript, GetOfflineCombat);
	vm->RegisterFunction("ALifeSetOfflineCombat"sv, kScript, SetOfflineCombat);
	vm->RegisterFunction("ALifeGetNews"sv, kScript, GetNews);
	vm->RegisterFunction("ALifeSetNews"sv, kScript, SetNews);
	vm->RegisterFunction("ALifeGetDynamicRelations"sv, kScript, GetDynamicRelations);
	vm->RegisterFunction("ALifeSetDynamicRelations"sv, kScript, SetDynamicRelations);
	vm->RegisterFunction("ALifeGetDragons"sv, kScript, GetDragons);
	vm->RegisterFunction("ALifeSetDragons"sv, kScript, SetDragons);
	vm->RegisterFunction("ALifeGetTalkInitiative"sv, kScript, GetTalkInitiative);
	vm->RegisterFunction("ALifeSetTalkInitiative"sv, kScript, SetTalkInitiative);
	vm->RegisterFunction("ALifeGetLoot"sv, kScript, GetLoot);
	vm->RegisterFunction("ALifeSetLoot"sv, kScript, SetLoot);
	vm->RegisterFunction("ALifeGetCompatBudget"sv, kScript, GetCompatBudget);
	vm->RegisterFunction("ALifeSetCompatBudget"sv, kScript, SetCompatBudget);
	vm->RegisterFunction("ALifeCompatInfo"sv, kScript, CompatInfo);
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
