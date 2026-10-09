#pragma once
// The simulation map: smart terrains (one per located place) plus a travel graph with precomputed shortest paths.
// Dependency-free (shared by the plugin and the headless harness).
#include <cstdint>
#include <vector>

#include "ALife/Config.h"
#include "ALife/Core.h"

namespace ALife {

// A placed location as the host (plugin or harness) sees it, before filtering.
struct LocationDef
{
	FormID id{ 0 };
	FormID parent{ 0 };
	FormID hold{ 0 };               // owning hold location (0 = none)
	FormID marker{ 0 };
	std::string name;
	std::vector<FormID> keywords;
	float x{ 0 }, y{ 0 }, z{ 0 };
	float radius{ 0 };
	std::uint8_t ws{ 0 };           // worldspace index (0 Tamriel, 1 Solstheim)
};

class World
{
public:
	std::vector<GraphNode> nodes;

	// Filters and classifies host locations into smart terrain nodes (shared by the plugin and the harness):
	// drops hold regions, and sub-locations that merely sit inside their parent place (docks of a city, a farm's interior ...).
	void BuildNodes(const std::vector<LocationDef>& a_defs);

	// Call once after nodes[] holds positions/kinds/holds. Applies props, links the graph and computes shortest paths.
	void Finalize(const Config& a_cfg, int a_neighbours = 5);

	std::size_t Size() const { return nodes.size(); }
	const GraphNode* Node(std::uint32_t a_id) const { return a_id < nodes.size() ? &nodes[a_id] : nullptr; }
	GraphNode* Node(std::uint32_t a_id) { return a_id < nodes.size() ? &nodes[a_id] : nullptr; }

	float Dist(std::uint32_t a, std::uint32_t b) const;                       // straight line
	float PathDist(std::uint32_t a, std::uint32_t b) const;                   // along the travel graph
	std::uint32_t NextHop(std::uint32_t a_from, std::uint32_t a_to) const;    // first node to walk to
	std::uint32_t Nearest(float a_x, float a_y, int a_ws = 0) const;
	std::uint32_t CityNode() const { return m_city; }
	bool InCityBuffer(float a_x, float a_y, float a_buffer, int a_ws = 0) const;
	int ComponentCount() const { return m_components; }

private:
	void LinkNeighbours(int a_k);
	void JoinComponents();
	void ComputePaths();

	std::vector<float> m_dist;             // N*N path distances
	std::vector<std::uint16_t> m_next;     // N*N first hops
	std::uint32_t m_city{ UINT32_MAX };
	int m_components{ 0 };
};

}  // namespace ALife
