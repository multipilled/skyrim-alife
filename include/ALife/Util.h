#pragma once
// Dependency-free helpers shared by the sim core, the plugin and the headless harness.
// Nothing in this header may include CommonLibSSE / RE headers.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ALife {

inline std::uint64_t Mix64(std::uint64_t a_x)
{
	a_x += 0x9E3779B97F4A7C15ULL;
	a_x = (a_x ^ (a_x >> 30)) * 0xBF58476D1CE4E5B9ULL;
	a_x = (a_x ^ (a_x >> 27)) * 0x94D049BB133111EBULL;
	return a_x ^ (a_x >> 31);
}

struct Rng
{
	std::uint64_t s;
	explicit Rng(std::uint64_t a_seed = 1) : s(Mix64(a_seed ? a_seed : 0xA11FEULL)) {}
	std::uint64_t next()
	{
		s ^= s << 13; s ^= s >> 7; s ^= s << 17;
		return s;
	}
	float frac() { return float(next() % 1000000ULL) / 1000000.f; }
	int index(int a_n) { return a_n <= 0 ? 0 : int(next() % std::uint64_t(a_n)); }
	int range(int a_lo, int a_hi) { return a_hi <= a_lo ? a_lo : a_lo + index(a_hi - a_lo + 1); }
	bool chance(float a_p) { return frac() < a_p; }
	float uniform(float a_lo, float a_hi) { return a_lo + (a_hi - a_lo) * frac(); }
};

// Anomaly's in_time_interval(a, b): hour-of-day h in [a, b), wrapping past midnight when a > b.
inline bool InTimeInterval(float a_hourOfDay, float a_from, float a_to)
{
	if (a_from <= a_to) return a_hourOfDay >= a_from && a_hourOfDay < a_to;
	return a_hourOfDay >= a_from || a_hourOfDay < a_to;
}

inline float HourOfDay(double a_hours) { return float(std::fmod(a_hours < 0 ? 0.0 : a_hours, 24.0)); }
inline std::uint32_t DayOf(double a_hours) { return std::uint32_t(a_hours < 0 ? 0.0 : a_hours / 24.0); }

inline std::string Trim(const std::string& s)
{
	std::size_t b = 0, e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
	return s.substr(b, e - b);
}

inline std::vector<std::string> SplitList(const std::string& s, char sep = ',')
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : s) {
		if (c == sep) { out.push_back(Trim(cur)); cur.clear(); }
		else cur.push_back(c);
	}
	out.push_back(Trim(cur));
	out.erase(std::remove_if(out.begin(), out.end(), [](const std::string& x) { return x.empty(); }), out.end());
	return out;
}

}  // namespace ALife
