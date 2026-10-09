#pragma once
#include <string>
#include <cstdint>

// ALife.log exists only when bDebug=1 (v1.7.6). No other log file is ever created.
namespace ALife::Log {
void Open();      // truncates at game start when bDebug=1
void Line(const std::string& a_msg);
bool On();
}
