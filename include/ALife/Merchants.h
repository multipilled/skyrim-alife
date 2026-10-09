#pragma once
// Plugin-side merchant economy: every vanilla (and modded) vendor in Tamriel, attached to the smart terrain it belongs to.
#include "ALife/Systems.h"

namespace ALife::Merchants {
struct State   // what must survive a save so gold floors and restock cycles hold across loads
{
	RE::FormID chest{ 0 };              // the vendor container this state belongs to
	std::int32_t baselineGold{ -1 };
	std::int32_t itemsBought{ 0 };      // items A-Life bought from the merchant this cycle
	std::int32_t goldStolen{ 0 };       // taken by raiders this cycle
	double cycleStart{ 0 };
};

void Init();                                            // kDataLoaded after the graph: find vendors, attach them to nodes
int Count();
bool AnyOpen(const GraphNode& a_node, double a_nowHours);
int OnNode(const GraphNode& a_node);
void Trade(Group& a_g, GraphNode& a_node, double a_nowHours);     // sell surplus, buy needs, rest at inns
// Raiders take gold from the merchants of a settlement (never below the floor); returns what they got.
int Plunder(GraphNode& a_node, float a_share, double a_nowHours);
bool HasSellables(const Group& a_g);                    // anything worth selling to a vendor
bool NeedsSupplies(const Group& a_g);                   // potions / ammo / food running low
std::vector<State> Snapshot();
void Restore(const std::vector<State>& a_states);
void Reset();
}
