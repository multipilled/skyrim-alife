#pragma once
// Party (docs/COMPANIONS.md): Anomaly's companions on ALife squads - who joins (free for friends, paid for neutrals of the
// freelance factions), room, contracts, orders, and the squad leaving / rejoining the simulation.
// Dependency-free: the same code runs in the plugin and in the headless tests (tests/party_test.cpp). The plugin turns the
// events into dialogue, packages, teammate flags, gold and notices.
//
// Ported (Anomaly 1.5.3): axr_companions.script (companion_squads, add/remove_from_actor_squad, max_actor_squad_size),
// dialogs_axr_companion.script (is_actor_friend, is_room_in_actor_squad), txr_paid_companions.script (paid factions,
// is_squad_competent, take_guard_money_1/2, contract_timer).
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ALife/Core.h"
#include "ALife/Ltx.h"
#include "ALife/Standing.h"

namespace ALife {

class Board;

// What ALifeCompOffer tells the dialogue about the squad under the crosshair.
enum class CompOffer : int { None = 0, Free = 1, Paid = 2, NoRoom = 3, Unwilling = 4 };
enum class Order : std::uint8_t { Follow = 0, Wait = 1, Move = 2 };
const char* OrderName(Order a_o);

struct PartyConfig
{
	int maxMembers{ 6 };                         // living companions in all (Anomaly max_actor_squad_size 2, 4 with Patriarch; ALife squads are bigger)
	int maxSquads{ 2 };                          // each holds one of the three on-screen slots
	std::array<bool, kFactionCount> paid{};      // factions that hire out (txr_paid_companions.script:11-18)
	std::array<float, 3> willing{ 0.f, 0.4f, 0.7f };          // by tier: novices never, advanced 40%, veterans 70% (:52-83)
	std::array<float, 3> priceLo{ 0.8f, 1.3f, 1.8f };         // price factor range by tier
	std::array<float, 3> priceHi{ 1.2f, 1.6f, 2.4f };
	float dayGold{ 250.f };                      // Anomaly money_1_base 1500 per day, scaled to Skyrim's economy
	std::array<int, 2> days{ 1, 2 };             // the two contracts offered (Anomaly: one day, two days)
	double warnHours{ 1.0 };                     // notice before a contract ends
	float teleportDistance{ 4000.f };            // a following member further than this is brought to the player
	float moveDistance{ 1500.f };                // "go up ahead": how far beyond the speaker
	float waitPlaceDistance{ 8000.f };           // after a load, a waiting squad is put back once the player is this close
	bool lootDefault{ false };
};

bool LoadPartyConfig(const Ltx& a_ltx, PartyConfig& a_out, std::string& a_err);

struct Companion
{
	std::uint64_t squad{ 0 };
	std::string name;                    // leader's name when it joined (notices after its death)
	bool paid{ false };
	double joinedAt{ 0 };
	double until{ 0 };                   // paid: contract end (game hours); 0 = free
	bool warned{ false };
	Order order{ Order::Follow };
	bool holdFire{ false };
	bool loot{ false };
	// where it waits (the plugin fills these; FormIDs are raw in the co-save and resolved after a load)
	FormID cell{ 0 }, ws{ 0 };
	float wx{ 0 }, wy{ 0 }, wz{ 0 };
};

// Where the player stands when a companion squad is dismissed (ws: worldspace index, -1 when unknown, e.g. an interior).
struct PartyPlace
{
	int ws{ -1 };
	float x{ 0 }, y{ 0 };
};

struct PartyEvent
{
	enum class Kind : std::uint8_t { Joined, Left, Warned, Expired, Lost, Turned };
	Kind kind{ Kind::Joined };
	std::uint64_t squad{ 0 };
	std::string name;
	std::int32_t gold{ 0 };              // Joined (paid): take this from the player
	double until{ 0 };
};

class Party
{
public:
	PartyConfig cfg;

	// --- state (saved)
	std::vector<Companion> list;
	struct Roll { bool willing{ false }; float factor{ 1.f }; };
	std::unordered_map<std::uint64_t, Roll> rolls;   // per squad, rolled once (Anomaly dialog_squad[id])

	void Reset() { list.clear(); rolls.clear(); }

	// What the squad would say to "Travel with me": free, paid, no room, not willing; None when it will not even discuss it
	// (unfriendly, a faction that does not hire out, held by a job, a monster squad, already a companion).
	CompOffer OfferFor(const Group& a_g, Regard a_regard, bool a_heldByJob, Rng& a_rng);
	// Its price for a_days (paid offers): dayGold x the squad's factor x days.
	int Price(const Group& a_g, int a_days, Rng& a_rng, float a_economy = 1.f);   // a_economy: Config::Diff(Economy), pay x 1 / it
	bool Room(const Board& a_b, const Group& a_g) const;    // max_members and max_squads, not counting a_g itself
	bool RoomFor(const Board& a_b, int a_members) const;   // room for one more squad of that many
	int LivingCompanions(const Board& a_b) const;
	// A job puts the squad in the party (an escort, a freed captive: docs/TASKS.md section 8): room only, no offer roll, no pay.
	bool Join(Board& a_b, Group& a_g, double a_now, std::vector<PartyEvent>& a_ev);

	// The squad joins (re-checked first: offer, room). Paid: a_days > 0, the Joined event carries the price. The squad leaves
	// the board's simulation (Board::companions, no smart, no target).
	bool Recruit(Board& a_b, Group& a_g, Regard a_regard, bool a_heldByJob, int a_days, double a_now, Rng& a_rng, std::vector<PartyEvent>& a_ev);
	// It leaves (dismissed, turned on the player): back to the board, which picks its next target. With a_player in another
	// worldspace than the squad's, the squad settles there at Board::NearestValidPlace (the one exception to "squads never change
	// worldspace"). When it cannot (its faction is blocked there, or no place there suits it), a dismissal the player asked for
	// (Left) is refused: false, nothing changes; any other reason lets it go back to its own worldspace as before.
	bool Dismiss(Board& a_b, std::uint64_t a_squad, PartyEvent::Kind a_why, std::vector<PartyEvent>& a_ev, const PartyPlace* a_player = nullptr);
	// Contracts (warn, expire) and wiped squads.
	void Update(Board& a_b, double a_now, std::vector<PartyEvent>& a_ev);

	Companion* Find(std::uint64_t a_squad);
	const Companion* Find(std::uint64_t a_squad) const;
	bool Has(std::uint64_t a_squad) const { return Find(a_squad) != nullptr; }

	// After a load: the board's companion set and each squad's detached state from the list.
	void Rebind(Board& a_b);

	// Co-save blob. Wait-point FormIDs are raw: the plugin resolves them after Load.
	static constexpr std::uint32_t kSaveVersion = 1;
	void Save(std::vector<std::uint8_t>& a_out) const;
	bool Load(const std::uint8_t* a_data, std::size_t a_size, std::uint32_t a_version);

private:
	Roll& RollFor(const Group& a_g, Rng& a_rng);
	static void Detach(Board& a_b, Group& a_g);
};

}  // namespace ALife
