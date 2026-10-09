#include "ALife/World.h"

#include <cfloat>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace ALife {

namespace {
	inline float D2(const GraphNode& a, const GraphNode& b)
	{
		float dx = a.x - b.x, dy = a.y - b.y;
		return std::sqrt(dx * dx + dy * dy);
	}
	void Link(std::vector<GraphNode>& n, std::uint32_t a, std::uint32_t b)
	{
		if (a == b) return;
		auto& ea = n[a].edges;
		if (std::find(ea.begin(), ea.end(), b) == ea.end()) ea.push_back(b);
		auto& eb = n[b].edges;
		if (std::find(eb.begin(), eb.end(), a) == eb.end()) eb.push_back(a);
	}
}

void World::BuildNodes(const std::vector<LocationDef>& a_defs)
{
	nodes.clear();
	std::unordered_map<FormID, const LocationDef*> byId;
	for (auto& d : a_defs) byId[d.id] = &d;
	auto hasKw = [](const LocationDef& d, FormID k) { return std::find(d.keywords.begin(), d.keywords.end(), k) != d.keywords.end(); };
	auto isRegion = [&](const LocationDef& d) { return hasKw(d, 0x16771) || hasKw(d, 0x868E1) || hasKw(d, 0x868E2) || hasKw(d, 0x868E3); };
	std::vector<const LocationDef*> cand;
	for (auto& d : a_defs) if (!isRegion(d)) cand.push_back(&d);
	std::unordered_set<FormID> candIds;
	for (auto* d : cand) candIds.insert(d->id);

	for (auto* d : cand) {
		const Kind kind = ClassifyKeywords(d->keywords);
		if (d->hold == 0 && kind == Kind::Wilderness) continue;   // region roots (e.g. the Tamriel location), not places
		const bool minor = kind == Kind::Wilderness || kind == Kind::Town || kind == Kind::Farm || kind == Kind::Inn || kind == Kind::Mill;
		if (minor) {
			// a sub-location inside a bigger candidate place is part of that place, not a separate smart terrain
			bool absorbed = false;
			FormID p = d->parent;
			for (int guard = 0; p && guard < 8; ++guard) {
				if (candIds.count(p)) {
					const LocationDef* pd = byId[p];
					const float dx = pd->x - d->x, dy = pd->y - d->y;
					if (pd->ws == d->ws && std::sqrt(dx * dx + dy * dy) < 6000.f) absorbed = true;
					break;
				}
				auto it = byId.find(p);
				p = it == byId.end() ? 0 : it->second->parent;
			}
			if (absorbed) continue;
		}
		GraphNode n;
		n.location = d->id;
		n.markerRef = d->marker;
		n.hold = d->hold;
		n.name = d->name;
		n.x = d->x; n.y = d->y; n.z = d->z;
		n.ws = d->ws;
		n.radius = d->radius;
		n.keywords = d->keywords;
		n.kind = kind;
		nodes.push_back(std::move(n));
	}
	std::sort(nodes.begin(), nodes.end(), [](const GraphNode& a, const GraphNode& b) { return a.location < b.location; });
}

void World::Finalize(const Config& a_cfg, int a_neighbours)
{
	for (std::uint32_t i = 0; i < nodes.size(); ++i) {
		auto& n = nodes[i];
		n.id = i;
		n.edges.clear();
		ApplyProps(a_cfg, n);
		n.city = n.kind == Kind::City;
		n.settlement = n.kind == Kind::Town || n.kind == Kind::Farm || n.kind == Kind::Inn || n.kind == Kind::Mill;
		n.inn = n.kind == Kind::Inn;
		n.dungeon = n.kind == Kind::Dungeon || n.kind == Kind::Crypt || n.kind == Kind::DwarvenRuin || n.kind == Kind::Mine ||
		            n.kind == Kind::BanditCamp || n.kind == Kind::VampireLair || n.kind == Kind::WarlockLair || n.kind == Kind::FalmerHive ||
		            n.kind == Kind::WerewolfLair || n.kind == Kind::DragonPriestLair || n.kind == Kind::HagravenNest;
	}
	m_city = UINT32_MAX;
	for (auto& n : nodes) if (n.city) { m_city = n.id; break; }
	LinkNeighbours(a_neighbours);
	JoinComponents();
	ComputePaths();
}

void World::LinkNeighbours(int a_k)
{
	const std::uint32_t N = std::uint32_t(nodes.size());
	for (std::uint32_t i = 0; i < N; ++i) {
		std::vector<std::pair<float, std::uint32_t>> d;
		d.reserve(N);
		for (std::uint32_t j = 0; j < N; ++j) if (i != j && nodes[i].ws == nodes[j].ws) d.emplace_back(D2(nodes[i], nodes[j]), j);   // never across worldspaces
		const std::size_t k = std::min<std::size_t>(std::size_t(a_k), d.size());
		std::partial_sort(d.begin(), d.begin() + k, d.end());
		for (std::size_t t = 0; t < k; ++t) Link(nodes, i, d[t].second);
	}
}

// Joins the pieces of each worldspace's graph into one connected map per worldspace (worldspaces are never joined to each other).
void World::JoinComponents()
{
	const std::uint32_t N = std::uint32_t(nodes.size());
	m_components = 0;
	for (int ws = 0; ws < kWorldspaces; ++ws) {
		std::uint32_t first = UINT32_MAX;
		for (std::uint32_t i = 0; i < N; ++i) if (nodes[i].ws == ws) { first = i; break; }
		if (first == UINT32_MAX) continue;
		for (;;) {
			std::vector<int> comp(N, -1);
			int nc = 0;
			for (std::uint32_t s = 0; s < N; ++s) {
				if (nodes[s].ws != ws || comp[s] >= 0) continue;
				std::vector<std::uint32_t> st{ s };
				comp[s] = nc;
				while (!st.empty()) {
					auto u = st.back(); st.pop_back();
					for (auto v : nodes[u].edges) if (comp[v] < 0) { comp[v] = nc; st.push_back(v); }
				}
				++nc;
			}
			if (nc <= 1) { m_components += 1; break; }
			const int base = comp[first];
			float best = FLT_MAX;
			std::uint32_t ba = 0, bb = 0;
			for (std::uint32_t a = 0; a < N; ++a)
				for (std::uint32_t b = 0; b < N; ++b)
					if (nodes[a].ws == ws && nodes[b].ws == ws && comp[a] == base && comp[b] >= 0 && comp[b] != base) {
						float dd = D2(nodes[a], nodes[b]);
						if (dd < best) { best = dd; ba = a; bb = b; }
					}
			Link(nodes, ba, bb);
		}
	}
}

void World::ComputePaths()
{
	const std::uint32_t N = std::uint32_t(nodes.size());
	m_dist.assign(std::size_t(N) * N, FLT_MAX);
	m_next.assign(std::size_t(N) * N, 0xFFFF);
	using QE = std::pair<float, std::uint32_t>;
	for (std::uint32_t s = 0; s < N; ++s) {
		float* dist = &m_dist[std::size_t(s) * N];
		std::uint16_t* nxt = &m_next[std::size_t(s) * N];
		std::vector<std::uint32_t> first(N, UINT32_MAX);
		std::priority_queue<QE, std::vector<QE>, std::greater<>> pq;
		dist[s] = 0.f;
		nxt[s] = std::uint16_t(s);
		pq.emplace(0.f, s);
		while (!pq.empty()) {
			auto [d, u] = pq.top(); pq.pop();
			if (d > dist[u]) continue;
			for (auto v : nodes[u].edges) {
				float nd = d + D2(nodes[u], nodes[v]);
				if (nd < dist[v]) {
					dist[v] = nd;
					first[v] = (u == s) ? v : first[u];
					nxt[v] = std::uint16_t(first[v]);
					pq.emplace(nd, v);
				}
			}
		}
	}
}

float World::Dist(std::uint32_t a, std::uint32_t b) const
{
	if (a >= nodes.size() || b >= nodes.size()) return 0.f;
	return D2(nodes[a], nodes[b]);
}

float World::PathDist(std::uint32_t a, std::uint32_t b) const
{
	const std::size_t N = nodes.size();
	if (a >= N || b >= N) return 0.f;
	return m_dist[std::size_t(a) * N + b];
}

std::uint32_t World::NextHop(std::uint32_t a_from, std::uint32_t a_to) const
{
	const std::size_t N = nodes.size();
	if (a_from >= N || a_to >= N) return a_from;
	auto n = m_next[std::size_t(a_from) * N + a_to];
	return n == 0xFFFF ? a_from : std::uint32_t(n);
}

std::uint32_t World::Nearest(float a_x, float a_y, int a_ws) const
{
	std::uint32_t best = 0;
	float bd = FLT_MAX;
	for (auto& n : nodes) {
		if (n.ws != a_ws) continue;
		float dx = n.x - a_x, dy = n.y - a_y, d = dx * dx + dy * dy;
		if (d < bd) { bd = d; best = n.id; }
	}
	return best;
}

bool World::InCityBuffer(float a_x, float a_y, float a_buffer, int a_ws) const
{
	for (auto& n : nodes) {
		if (!n.city || n.ws != a_ws) continue;
		float dx = n.x - a_x, dy = n.y - a_y;
		float r = std::max(2500.f, n.radius) + a_buffer;
		if (dx * dx + dy * dy < r * r) return true;
	}
	return false;
}

}  // namespace ALife
