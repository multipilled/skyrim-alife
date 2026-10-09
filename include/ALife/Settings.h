#pragma once
#include <string>

namespace ALife::Settings {
struct Data
{
	bool enabled{ true };
	float populationScale{ 1.0f };   // Anomaly alife_stalker_pop (0.5 - 1.5)
	int ocsMode{ 1 };                // Anomaly offline_combat: 0 off, 1 only at smart terrains, 2 full
	bool news{ true };               // dynamic news notifications
	bool dynamicRelations{ false };  // Anomaly dynamic faction relations (off by default, like Anomaly)
	int talkInitiative{ 2 };         // how often squads stop the player to talk: 0 never, 1 rarely, 2 normally, 3 often
	bool loot{ true };               // squads strip the bodies they pass
	bool prepareUninstall{ false };
	// test switch - never in the MCM
	bool bDebug{ false };
	std::string sDebugStartup;      // debug only: console commands (;-separated) run once from the main menu, e.g. "coc WhiterunExterior01"
	std::string sDebugAuto;         // debug only: after the world has loaded, place scenario N (0 bandit, 1 thalmor, ...), walk up to it and talk to it
	std::string sDebugInWorld;      // debug only: console commands run once the world has loaded, e.g. "tgm"
	bool bForceGate{ false };       // debug only: open the start gate as soon as the player is in the world (lab saves without a Helgen/LAL start)
	bool bBackground{ false };      // debug only: the game window never activates or minimizes (background test runs keep the user's focus)
	int iDebugSeed{ 1 };
	int iDebugKeyStatus{ 79 };      // DIK_NUMPAD1: status dump
	int iDebugKeyFight{ 80 };       // DIK_NUMPAD2: fast-forward one game day
	int iDebugKeyFillSticky{ 81 };  // DIK_NUMPAD3: place a hostile pair near the player
	int iDebugKeyGoto{ 75 };        // DIK_NUMPAD4: teleport next to the nearest online squad
	int iDebugKeyTalk{ 71 };        // DIK_NUMPAD7: talk to the last scenario squad (what pressing Activate on it would do)
	int iDebugKeyKill{ 77 };        // DIK_NUMPAD6: the player strikes and kills one member of the last scenario squad
	int iDebugKeyScenario{ 76 };    // DIK_NUMPAD5: place the next test squad (one faction, 2000 units ahead) to watch how it treats the player
};
Data& Get();
void Load();
void Save();
// Re-read [Debug] from disk (MCM Save must not clobber hand edits; mid-session edits take effect).
void ReloadDebug();
std::string IniPath();
std::string ConfigDir();   // Data/SKSE/Plugins/ALife/ (the .ltx files)
}
