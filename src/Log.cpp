#include "ALife/PCH.h"
#include "ALife/Log.h"
#include "ALife/Settings.h"
#include <filesystem>
#include <fstream>
#include <ctime>

namespace ALife::Log {
namespace {
	std::ofstream gOut;
	std::mutex gMx;
	bool gOpenedThisSession{ false };
}
bool On() { return Settings::Get().bDebug && gOut.is_open(); }

void Open()
{
	std::lock_guard l(gMx);
	if (!Settings::Get().bDebug) {
		if (gOut.is_open()) { gOut.close(); }
		return;
	}
	if (gOut.is_open()) return;
	std::optional<std::filesystem::path> dir = SKSE::log::log_directory();
	if (!dir) return;
	std::error_code ec;
	std::filesystem::create_directories(*dir, ec);
	// First open this session truncates; a mid-session bDebug flip appends so we keep earlier lines if any.
	gOut.open(*dir / "ALife.log", gOpenedThisSession ? std::ios::app : std::ios::trunc);
	gOpenedThisSession = true;
	if (gOut) {
		gOut << "A-Life 2.0 debug log (bDebug=1)\n";
		gOut.flush();
	}
}

void Line(const std::string& a_msg)
{
	if (!Settings::Get().bDebug) return;
	std::lock_guard l(gMx);
	if (!gOut.is_open()) return;
	// 176f: wall-clock stamp (PC local time) on every line so playtest reports can be lined up with saves.
	const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm tm{};
	localtime_s(&tm, &now);
	char ts[16];
	std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);
	gOut << '[' << ts << "] " << a_msg << '\n';
	gOut.flush();
}
}
