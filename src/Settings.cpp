#include "ALife/PCH.h"
#include "ALife/Settings.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace ALife::Settings {
namespace {
	Data g;
	std::string Trim(std::string s)
	{
		while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
		std::size_t i = 0;
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
		return s.substr(i);
	}
	int ToInt(const std::string& v, int def)
	{
		try { return std::stoi(v); } catch (...) { return def; }
	}
	void ApplyLine(Data& d, const std::string& key, const std::string& val, bool debugOnly)
	{
		if (!debugOnly) {
			if (key == "Enabled") { d.enabled = (val != "0"); return; }
			if (key == "PopulationScale") {
				try { d.populationScale = std::stof(val); } catch (...) {}
				d.populationScale = std::clamp(std::round(d.populationScale * 10.f) / 10.f, 0.5f, 1.5f);
				return;
			}
			if (key == "OfflineCombat") { d.ocsMode = std::clamp(ToInt(val, 1), 0, 2); return; }
			if (key == "News") { d.news = (val != "0"); return; }
			if (key == "DynamicRelations") { d.dynamicRelations = (val == "1"); return; }
			if (key == "TalkInitiative") { d.talkInitiative = std::clamp(ToInt(val, 2), 0, 3); return; }
			if (key == "Loot") { d.loot = (val != "0"); return; }
			if (key == "PrepareUninstall") { d.prepareUninstall = (val == "1"); return; }
		}
		if (key == "bDebug") d.bDebug = (val == "1");
		else if (key == "bForceGate") d.bForceGate = (val == "1");
		else if (key == "bBackground") d.bBackground = (val == "1");
		else if (key == "sDebugStartup") d.sDebugStartup = val;
		else if (key == "sDebugInWorld") d.sDebugInWorld = val;
		else if (key == "sDebugAuto") d.sDebugAuto = val;
		else if (key == "iDebugSeed") d.iDebugSeed = ToInt(val, 1);
		else if (key == "iDebugKeyStatus") d.iDebugKeyStatus = ToInt(val, 79);
		else if (key == "iDebugKeyFight") d.iDebugKeyFight = ToInt(val, 80);
		else if (key == "iDebugKeyFillSticky") d.iDebugKeyFillSticky = ToInt(val, 81);
		else if (key == "iDebugKeyGoto") d.iDebugKeyGoto = ToInt(val, 75);
		else if (key == "iDebugKeyScenario") d.iDebugKeyScenario = ToInt(val, 76);
		else if (key == "iDebugKeyKill") d.iDebugKeyKill = ToInt(val, 77);
		else if (key == "iDebugKeyTalk") d.iDebugKeyTalk = ToInt(val, 71);
	}
	void ParseFile(Data& d, bool debugOnly)
	{
		std::ifstream in(IniPath());
		if (!in) return;
		std::string line;
		while (std::getline(in, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
			auto eq = line.find('=');
			if (eq == std::string::npos) continue;
			auto key = Trim(line.substr(0, eq));
			auto val = Trim(line.substr(eq + 1));
			if (auto sc = val.find(';'); sc != std::string::npos) val = Trim(val.substr(0, sc));
			ApplyLine(d, key, val, debugOnly);
		}
	}
}

Data& Get() { return g; }

std::string IniPath()
{
	char mod[MAX_PATH]{};
	HMODULE hm = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCSTR>(&IniPath), &hm);
	GetModuleFileNameA(hm, mod, MAX_PATH);
	return (fs::path(mod).parent_path() / "ALife.ini").string();
}

std::string ConfigDir()
{
	return (fs::path(IniPath()).parent_path() / "ALife").string();
}

void Load()
{
	g = Data{};
	ParseFile(g, false);
}

void ReloadDebug()
{
	ParseFile(g, true);
}

void Save()
{
	// Preserve hand-edited [Debug] from disk so MCM density/enabled writes cannot wipe bDebug=1.
	ReloadDebug();
	std::ofstream out(IniPath(), std::ios::trunc);
	if (!out) return;
	out << "; A-Life for Skyrim (Anomaly edition). The MCM writes [General]; edit [Debug] by hand only.\n";
	out << "[General]\n";
	out << "Enabled=" << (g.enabled ? 1 : 0) << "\n";
	char buf[16];
	std::snprintf(buf, sizeof(buf), "%.1f", g.populationScale);
	out << "PopulationScale=" << buf << "\n";
	out << "OfflineCombat=" << g.ocsMode << "   ; 0 off, 1 only at smart terrains (default), 2 full\n";
	out << "News=" << (g.news ? 1 : 0) << "\n";
	out << "DynamicRelations=" << (g.dynamicRelations ? 1 : 0) << "\n";
	out << "TalkInitiative=" << g.talkInitiative << "   ; squads stopping you to talk: 0 never, 1 rarely, 2 normally (default), 3 often\n";
	out << "Loot=" << (g.loot ? 1 : 0) << "   ; squads strip the bodies they pass\n";
	out << "PrepareUninstall=" << (g.prepareUninstall ? 1 : 0) << "\n";
	out << "\n; Test switch. Never shown in the MCM. Hotkeys are DirectX scan codes.\n";
	out << "; Numpad1=79 status dump, Numpad2=80 fast-forward one game day, Numpad3=81 place a hostile pair near you.\n";
	out << "; Set bDebug=1 then restart OR wait ~1s in-game (ReloadDebug). MCM Save no longer clobbers this.\n";
	out << "[Debug]\n";
	out << "bDebug=" << (g.bDebug ? 1 : 0) << "\n";
	out << "bForceGate=" << (g.bForceGate ? 1 : 0) << "\n";
	if (g.bBackground) out << "bBackground=1\n";
	out << "sDebugStartup=" << g.sDebugStartup << "\n";
	out << "sDebugInWorld=" << g.sDebugInWorld << "\n";
	out << "sDebugAuto=" << g.sDebugAuto << "\n";
	out << "iDebugSeed=" << g.iDebugSeed << "\n";
	out << "iDebugKeyStatus=" << g.iDebugKeyStatus << "\n";
	out << "iDebugKeyFight=" << g.iDebugKeyFight << "\n";
	out << "iDebugKeyFillSticky=" << g.iDebugKeyFillSticky << "\n";
	out << "iDebugKeyGoto=" << g.iDebugKeyGoto << "\n";
	out << "iDebugKeyScenario=" << g.iDebugKeyScenario << "\n";
	out << "iDebugKeyKill=" << g.iDebugKeyKill << "\n";
	out << "iDebugKeyTalk=" << g.iDebugKeyTalk << "\n";
}
}
