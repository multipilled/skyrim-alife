#include "ALife/Systems.h"
#include "ALife/Forms.h"
#include "ALife/Settings.h"
#include "ALife/Log.h"

using namespace std::literals;

SKSEPluginInfo(
	.Version = REL::Version{ 2, 0, 0, 1 },   // Anomaly edition
	.Name = "ALife"sv,
	.Author = "multipilled"sv,
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

namespace {
	std::thread gTicker;
	std::atomic<bool> gStop{ false };
	std::atomic<bool> gTaskQueued{ false };
	std::chrono::steady_clock::time_point gLastTick{};

	// ~10 Hz main-thread heartbeat that never depends on player input (B12).
	void StartTicker()
	{
		if (gTicker.joinable()) return;
		gLastTick = std::chrono::steady_clock::now();
		gTicker = std::thread([] {
			while (!gStop.load()) {
				std::this_thread::sleep_for(100ms);
				if (gTaskQueued.exchange(true)) continue;   // previous tick has not run yet: never pile up
				SKSE::GetTaskInterface()->AddTask([] {
					auto now = std::chrono::steady_clock::now();
					float dt = std::chrono::duration<float>(now - gLastTick).count();
					gLastTick = now;
					ALife::Sim::MainTick(std::min(dt, 1.f));
					gTaskQueued = false;
				});
			}
		});
		gTicker.detach();   // lives for the whole session; the process tears it down
	}

	// bDebug + bBackground (background test runs): a CBT hook on the main thread, which creates the game window, refuses
	// every activation, focus change and minimize. The window can never take the user's foreground (Windows hands it to a
	// new window once the user has been idle past ForegroundLockTimeout) and never minimizes (with bAlwaysActive=1 that
	// crashed in the renderer). DirectInput then never acquires the keyboard or mouse; the bridge drives the game.
	LRESULT CALLBACK NoActivate(int a_code, WPARAM a_w, LPARAM a_l)
	{
		if (a_code == HCBT_ACTIVATE || a_code == HCBT_SETFOCUS) return 1;
		if (a_code == HCBT_MINMAX) {
			const auto cmd = LOWORD(a_l);
			if (cmd == SW_MINIMIZE || cmd == SW_SHOWMINIMIZED || cmd == SW_SHOWMINNOACTIVE || cmd == SW_FORCEMINIMIZE) return 1;
		}
		return CallNextHookEx(nullptr, a_code, a_w, a_l);
	}

	// ... and the game never reads the real keyboard, mouse or gamepad: with bAlwaysActive=1 the user's own key presses
	// elsewhere could reach it (a main-menu "Continue" loaded a save during a background test). The bridge pushes its events
	// straight into BSInputEventQueue, so it keeps working.
	void NoPoll(RE::BSIInputDevice*, float) {}
	void MuteRealInput()
	{
		REL::Relocation<std::uintptr_t> kb{ RE::VTABLE_BSWin32KeyboardDevice[0] };
		REL::Relocation<std::uintptr_t> mouse{ RE::VTABLE_BSWin32MouseDevice[0] };
		REL::Relocation<std::uintptr_t> pad{ RE::VTABLE_BSPCGamepadDeviceHandler[0] };
		for (auto* v : { &kb, &mouse, &pad }) v->write_vfunc(2, &NoPoll);   // BSIInputDevice::Poll
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kInputLoaded:
			ALife::Events::RegisterEarly();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			ALife::Sim::OnDataLoaded();
			ALife::Events::RegisterGame();
			StartTicker();
			break;
		case SKSE::MessagingInterface::kNewGame:
			ALife::Sim::OnNewGame();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			ALife::Sim::OnPreLoad();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			ALife::Sim::OnPostLoad();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .log = false });   // no SKSE log file of our own; ALife.log only when bDebug=1
	ALife::Settings::Load();
	ALife::Log::Open();
	if (ALife::Settings::Get().bDebug && ALife::Settings::Get().bBackground) {
		const bool hooked = SetWindowsHookExW(WH_CBT, NoActivate, nullptr, GetCurrentThreadId()) != nullptr;
		MuteRealInput();
		ALife::Log::Line(std::string("[init] background mode: window activation ") + (hooked ? "blocked" : "NOT blocked (hook failed)") + ", real keyboard/mouse/gamepad muted");
	}
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	ALife::Save::Register();
	SKSE::GetPapyrusInterface()->Register(ALife::Papyrus::Register);
	ALife::Log::Line("[init] A-Life 2.0 (Anomaly edition) loaded (CommonLibSSE-NG v11.0.0)");
	return true;
}
