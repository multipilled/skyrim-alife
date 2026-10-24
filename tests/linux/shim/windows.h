// Linux shim for the headless tests (tests/linux/build.sh puts tests/linux/shim on the include path).
// Covers only what tests/harness.cpp and tests/camp_test.cpp use:
//   QueryThreadCycleTime: this thread's CPU time from CLOCK_THREAD_CPUTIME_ID, in NANOSECONDS (Windows gives cycles);
//   GetTickCount64: CLOCK_MONOTONIC in milliseconds.
#pragma once
#include <ctime>

typedef unsigned long long ULONG64;

inline void* GetCurrentThread() { return nullptr; }

inline int QueryThreadCycleTime(void*, ULONG64* out)
{
	timespec ts{};
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	*out = ULONG64(ts.tv_sec) * 1000000000ull + ULONG64(ts.tv_nsec);
	return 1;
}

inline ULONG64 GetTickCount64()
{
	timespec ts{};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ULONG64(ts.tv_sec) * 1000ull + ULONG64(ts.tv_nsec) / 1000000ull;
}
