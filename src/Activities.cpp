#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Merchants.h"
#include "ALife/Systems.h"

// The Skyrim effects of what squads do at a place. The decisions and the odds are the core's (core/Activities.cpp); here a raid
// really empties a merchant's till, a cleared dungeon really has fewer hostile inhabitants when the player walks in, a hunting
// party really carries meat and pelts to the market.
namespace ALife::Activities {
namespace {
	std::string Place(const GraphNode& n) { return n.name.empty() ? std::string(KindName(n.kind)) : n.name; }
}

bool SurvivalOn()
{
	const auto& r = Forms::R();
	return r.survivalLoaded && r.survivalEnabled && r.survivalEnabled->value >= 1.f;
}

bool FishingAvailable()
{
	const auto& r = Forms::R();
	return r.fishingLoaded && !r.fishItems.empty();
}

bool HasFood(const Group& g)
{
	for (auto& m : g.members) if (m.alive() && Economy::FoodCount(m) > 0) return true;
	return false;
}

// The vanilla guards and villagers a raid has to get past, scaled to the player's level like vanilla's own encounters.
float DefenderStrength(const GraphNode& n, Faction)
{
	float base = 80.f;
	switch (n.kind) {
	case Kind::Town: base = 130.f + 18.f * float(std::min(n.vendors, 4)); break;
	case Kind::Inn: base = 85.f; break;
	case Kind::Farm: base = 55.f; break;
	case Kind::Mill: base = 50.f; break;
	default: break;
	}
	auto* pc = RE::PlayerCharacter::GetSingleton();
	const float level = pc ? float(std::max<int>(1, pc->GetLevel())) : 1.f;
	return base * (0.6f + 0.06f * level);
}

void DoRaidSettlement(Group& g, GraphNode& n, bool success, float ratio)
{
	auto& b = Sim::B();
	if (!success) {
		Log::Line("[raid] " + std::string(FactionId(g.faction)) + " squad " + std::to_string(g.id) + " driven off from " + Place(n) + " (odds " +
				  std::to_string(int(ratio * 100)) + "%)");
		b.HostRef().News(std::string(FactionName(g.faction)) + " raiders were driven off from " + Place(n), n.x, n.y);
		return;
	}
	Rng rng(Mix64(g.id ^ std::uint64_t(b.now * 7)));
	int level = int(std::max(1.f, g.avgLevel()));
	const int stolen = Merchants::Plunder(n, 0.25f + 0.2f * rng.frac(), b.now);
	int living = 0;
	for (auto& m : g.members) living += m.alive();
	for (auto& m : g.members) {
		if (!m.alive()) continue;
		Economy::Haul(m, level, rng);
		m.gold += stolen / std::max(1, living);
	}
	Log::Line("[raid] " + std::string(FactionId(g.faction)) + " squad " + std::to_string(g.id) + " raided " + Place(n) + ": took " + std::to_string(stolen) +
			  " gold from the merchants (odds " + std::to_string(int(ratio * 100)) + "%)");
	b.HostRef().News(std::string(FactionName(g.faction)) + " raided " + Place(n), n.x, n.y);
}

// The section's kills are applied to the dungeon's real hostile inhabitants the next time the player enters that cell.
void DoClearSection(Group& g, GraphNode& n, int section, int kills)
{
	if (section < 0 || section >= int(n.cells.size())) return;
	const RE::FormID cell = n.cells[std::size_t(section)];
	if (n.bossCell && cell == n.bossCell) return;   // the boss section is never touched
	Sim::P().queuedKills[cell] += kills;
	Log::Line("[clear] " + std::string(FactionId(g.faction)) + " squad " + std::to_string(g.id) + " clears section " + std::to_string(section + 1) + " of " +
			  Place(n) + ": " + std::to_string(kills) + " kills queued");
}

void DoHaul(Group& g, GraphNode& n)
{
	Rng rng(Mix64(g.id ^ std::uint64_t(Sim::B().now * 13)));
	const int level = int(std::max(1.f, g.avgLevel()));
	int items = 0;
	for (auto& m : g.members) {
		if (!m.alive()) continue;
		const std::size_t before = m.inventory.size();
		Economy::Haul(m, level, rng);
		items += int(m.inventory.size() - before);
	}
	Log::Line("[clear] " + std::string(FactionId(g.faction)) + " squad " + std::to_string(g.id) + " leaves " + Place(n) + " with the haul (" + std::to_string(items) + " new stacks)");
}

void DoHunt(Group& g, double h, Rng& rng)
{
	const auto& cfg = Sim::Cfg();
	int living = 0;
	for (auto& m : g.members) living += m.alive();
	for (auto& m : g.members)
		if (m.alive() && rng.chance(cfg.huntRate * float(h))) Economy::HuntCatch(m, rng);
	// wildlife turns on the hunters: smaller parties are in more danger, and it is rarely fatal to a healthy hunter
	if (living > 0 && rng.chance(cfg.wildlifeRisk * float(h) * (2.f / float(living + 1)) * 1.5f)) {
		auto& m = g.members[std::size_t(rng.index(int(g.members.size())))];
		if (!m.alive()) return;
		m.health -= 20.f + rng.frac() * 60.f;
		if (!m.alive()) Log::Line("[wildlife] " + m.name + " of squad " + std::to_string(g.id) + " was killed by wildlife");
	}
}

void DoFish(Group& g, double h, Rng& rng)
{
	const auto& r = Forms::R();
	if (!r.fishingLoaded || r.fishItems.empty()) return;
	for (auto& m : g.members)
		if (m.alive() && rng.chance(Sim::Cfg().fishRate * float(h)))
			Economy::AddItem(m, r.fishItems[std::size_t(rng.index(int(r.fishItems.size())))]->GetFormID(), 1);
}
}
