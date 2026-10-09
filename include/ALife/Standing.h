#pragma once
// Standing: how the factions and squads of Skyrim regard the player (docs/STANDING.md).
// Engine-free: shared by the plugin and the headless tests. The host fills a PlayerContext; this decides what to do about it.
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "ALife/Config.h"
#include "ALife/Core.h"
#include "ALife/Util.h"

namespace ALife {

// Anomaly's relation tiers: attitude <= -1000 enemy ... >= +1000 ally.
enum class Regard : std::uint8_t { Hostile = 0, Unfriendly, Neutral, Friendly, Allied };
const char* RegardName(Regard a_r);

// The player as the world can know them, refreshed by the host a few times a second.
struct PlayerContext
{
	std::uint32_t flags{ 0 };                       // PlayerFlag bits
	RaceId race{ RaceId::Nord };                    // base race (vampire variants folded in)
	int level{ 1 };
	float power{ 1.f };                             // same scale as the sum of Host::MemberPower over a squad
	float healthFrac{ 1.f };
	int companions{ 0 };                            // followers and allies who fight beside the player
	float wealth{ 0.f };                            // 0..1 how worth robbing the player looks
	std::array<float, kFactionCount> disguise{};    // 0..1 how convincingly the player wears each faction's colours
	std::vector<std::pair<FormID, int>> bounty;    // hold location -> crime gold owed there

	bool Has(std::uint32_t a_flags) const { return (flags & a_flags) == a_flags; }
	int BountyIn(FormID a_hold) const
	{
		for (auto& b : bounty) if (b.first == a_hold) return b.second;
		return 0;
	}
};

struct Reason
{
	std::string text;
	float delta{ 0 };
};

struct Attitude
{
	float value{ 0 };
	Regard regard{ Regard::Neutral };
	std::vector<Reason> why;                         // filled only when explain is requested
};

// Where a squad stands when it forms its opinion.
struct Site
{
	FormID hold{ 0 };                                // hold the squad is in (crime faction / enforcement)
	bool enforces{ false };                          // the squad's faction holds that hold: it is the law there
};

enum class Call : std::uint8_t { Ignore = 0, Greet, Watch, Challenge, Parley, Attack, Withdraw, Assist };
const char* CallName(Call a_c);

// What the host knows about this particular meeting.
struct Env
{
	float dist{ 99999.f };                           // squad to player
	float squadPower{ 0.f };                         // living members, Host::MemberPower scale
	double now{ 0 };                                 // game hours
	Site site;
	bool inTerritory{ false };                       // the player is inside the squad's place
	bool hungry{ false };
	bool tooHurt{ false };
	bool greeted{ false };                           // already said hello this meeting
	bool parleyRefused{ false };                     // the player turned down the terms
	float challengeElapsed{ 0.f };                   // seconds since the squad challenged the player (0 = not yet)
};

struct Verdict
{
	Call call{ Call::Ignore };
	Regard regard{ Regard::Neutral };
	float attitude{ 0 };
	float desire{ 0 };                               // how much a predator wants the fight (0..1)
	std::string why;
};

struct Rumor
{
	Faction faction{ Faction::Bandit };
	float delta{ 0 };                                // goodwill change that reaches the faction later
	double dueHours{ 0 };
};

struct StandingState
{
	std::array<float, kFactionCount> goodwill{};     // earned reputation per faction
	float notoriety{ 0 };                            // how much a murderer the player is known to be (0..1000)
	std::vector<Rumor> rumors;
	std::array<float, kFactionCount> gainedToday{};  // positive goodwill earned today (daily cap)
	std::uint32_t gainDay{ 0 };
};

class Standing
{
public:
	Standing() = default;
	explicit Standing(const Config* a_cfg) : m_cfg(a_cfg) {}
	void SetConfig(const Config* a_cfg) { m_cfg = a_cfg; }
	const StandingConfig& Cfg() const { return m_cfg->standing; }

	StandingState state;
	void Reset() { state = StandingState{}; }

	Regard RegardOf(float a_attitude) const;

	// How faction f regards the player (optionally as one particular squad that has its own memory of them).
	Attitude Evaluate(Faction a_f, const PlayerContext& a_ctx, const Group* a_g, const Site& a_site, bool a_explain = false) const;

	// What a squad does about the player right now.
	Verdict Judge(const Group& a_g, const PlayerContext& a_ctx, const Env& a_env) const;

	// Strength comparison used by predators: squad power over player (+ companions) power.
	float Advantage(float a_squadPower, const PlayerContext& a_ctx) const;
	int TollFor(const PlayerContext& a_ctx) const;

	// ---- deeds
	void OnKill(Group& a_victimGroup, const Member& a_victim, bool a_witnessed, bool a_provoked, double a_now);
	void OnAttack(Group& a_g);                                        // the player hurt one of its members
	void OnHelp(Faction a_beneficiary, int a_foeLevel, double a_now); // the player killed a foe of a squad that was watching
	void OnTrade(Faction a_f, int a_gold, double a_now);
	void OnToll(Group& a_g, double a_now);                            // the player paid for passage
	void OnTask(Faction a_f, float a_delta);                          // a job done (+) or failed (-): the giver's faction only, no daily cap (docs/TASKS.md)
	void Tick(double a_nowHours, double a_dtHours);                   // fade, rumours, daily cap
	void DecayMemory(Group& a_g, double a_dtHours) const;

	// One line per faction for logs and the MCM, e.g. "Imperial Legion: Friendly (+420)".
	std::string Describe(Faction a_f, const PlayerContext& a_ctx, const Site& a_site) const;

private:
	void Deed(Faction a_f, float a_delta, bool a_spill, double a_now);
	void Gain(Faction a_f, float a_amount);
	bool RuleHolds(const StandingRule& a_r, const PlayerContext& a_ctx, const Site& a_site) const;
	const Config* m_cfg{ nullptr };
};

}  // namespace ALife
