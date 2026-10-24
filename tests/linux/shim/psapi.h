// Linux shim for the headless harness: the process memory stat is Windows-only, so K32GetProcessMemoryInfo fails
// (returns 0) and the harness's working-set figure reads 0.
#pragma once
#include <cstddef>

struct PROCESS_MEMORY_COUNTERS { unsigned long cb; std::size_t WorkingSetSize; };

inline void* GetCurrentProcess() { return nullptr; }
inline int K32GetProcessMemoryInfo(void*, PROCESS_MEMORY_COUNTERS*, unsigned long) { return 0; }
