#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Settings.h"
#include "ALife/Systems.h"

// Smart terrains: every placed vanilla (and modded) location in Tamriel becomes a node, classified from its location keywords
// by the shared core (ClassifyKeywords / World::BuildNodes) so the headless harness sees exactly the same map.
namespace ALife::Graph {
namespace {
	World gWorld;
	bool gReady{ false };
	std::unordered_map<RE::FormID, std::uint32_t> gByLoc;

	RE::FormID HoldOf(const RE::BGSLocation* l)
	{
		for (int guard = 0; l && guard < 16; l = l->parentLoc, ++guard)
			if (Forms::HasKw(l, Forms::Vanilla::KwLocTypeHold)) return l->GetFormID();
		return 0;
	}
}

bool Ready() { return gReady && gWorld.Size() > 0; }
World& W() { return gWorld; }
const std::vector<GraphNode>& Nodes() { return gWorld.nodes; }
const GraphNode* Node(std::uint32_t a_id) { return gWorld.Node(a_id); }
std::uint32_t Nearest(float a_x, float a_y, int a_ws) { return gWorld.Nearest(a_x, a_y, a_ws); }
bool InCityBuffer(float a_x, float a_y, int a_ws) { return gWorld.InCityBuffer(a_x, a_y, kCityBuffer, a_ws); }
std::uint32_t ByLocation(RE::FormID a_location)
{
	auto it = gByLoc.find(a_location);
	return it == gByLoc.end() ? UINT32_MAX : it->second;
}
RE::TESObjectREFR* MarkerOf(const GraphNode& a_n)
{
	return a_n.markerRef ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_n.markerRef) : nullptr;
}

void Build(const Config& a_cfg)
{
	gWorld = World{};
	gByLoc.clear();
	gReady = false;
	auto* dh = RE::TESDataHandler::GetSingleton();
	const auto& r = Forms::R();
	if (!dh || !r.tamriel) return;

	Ltx supp;
	supp.LoadFile(Settings::ConfigDir() + "/positions.ltx");

	std::vector<LocationDef> defs;
	const RE::FormID dragonbornIndex = r.solstheim ? (dh->LookupFormID(0x800, "Dragonborn.esm"sv) >> 24) : 0;   // 0: Dragonborn is not installed
	int viaMarker = 0, viaSupplement = 0, noPos = 0;
	for (auto* loc : dh->GetFormArray<RE::BGSLocation>()) {
		if (!loc) continue;
		if (Patch::AvoidLocation(loc->GetFormID())) continue;
		LocationDef d;
		d.id = loc->GetFormID();
		d.parent = loc->parentLoc ? loc->parentLoc->GetFormID() : 0;
		d.hold = HoldOf(loc);
		d.radius = loc->worldLocRadius;
		if (const char* n = loc->GetFullName(); n && *n) d.name = n;
		else { char b[24]; std::snprintf(b, sizeof(b), "Place %06X", d.id & 0xFFFFFF); d.name = b; }
		// Dragonborn's own keywords (riekling camp, werebear lair, ash spawn) are stored as 04xxxxxx, the vanilla load order, so
		// props.ltx [keyword_props] can name them whatever index the game gave the plugin
		if (loc->keywords)
			for (std::uint32_t i = 0; i < loc->numKeywords; ++i)
				if (loc->keywords[i]) {
					RE::FormID k = loc->keywords[i]->GetFormID();
					if (dragonbornIndex && (k >> 24) == dragonbornIndex) k = 0x04000000 | (k & 0xFFFFFF);
					d.keywords.push_back(k);
				}

		bool placed = false;
		if (auto mref = loc->worldLocMarker.get()) {
			auto* ws = mref->GetWorldspace();
			const int wi = Forms::WsIndex(ws);   // child city worldspaces share their parent's coordinates
			if (wi >= 0) {
				const auto p = mref->GetPosition();
				d.x = p.x; d.y = p.y; d.z = p.z;
				d.marker = mref->GetFormID();
				d.ws = std::uint8_t(wi);
				placed = true;
				++viaMarker;
			}
		}
		if (!placed) {   // locations without a usable marker (Solitude, Falkreath ..., all of Solstheim): positions shipped in positions.ltx
			char key[16];
			std::snprintf(key, sizeof(key), "%X", d.id);
			std::string section = "pos";
			if (dragonbornIndex && (d.id >> 24) == dragonbornIndex) {   // Dragonborn's own locations, keyed by their plugin-local id
				std::snprintf(key, sizeof(key), "%X", d.id & 0xFFFFFF);
				section = "pos_dlc2";
				d.ws = 1;
			}
			auto v = SplitList(supp.Get(section, key));
			if (v.size() >= 2) {
				d.x = std::stof(v[0]); d.y = std::stof(v[1]); d.z = 0.f;
				placed = true;
				++viaSupplement;
			}
		}
		if (!placed) {
			++noPos;
			static int shownDlc = 0;
			if ((loc->GetFormID() >> 24) == (dh->LookupFormID(0x800, "Dragonborn.esm"sv) >> 24) && shownDlc++ < 12) {
				auto mref = loc->worldLocMarker.get();
				auto* mws = mref ? mref->GetWorldspace() : nullptr;
				Log::Line(std::string("[graph] unplaced Dragonborn location ") + d.name + " marker=" + (mref ? "yes" : "no") + " ws=" + (mws ? mws->GetFormEditorID() : "none") +
						  " wsIndex=" + std::to_string(Forms::WsIndex(mws)) + " parentCell=" + (mref && mref->GetParentCell() ? "yes" : "no"));
			}
			continue;
		}
		defs.push_back(std::move(d));
	}
	gWorld.BuildNodes(defs);
	gWorld.Finalize(a_cfg);
	for (auto& n : gWorld.nodes) gByLoc[n.location] = n.id;
	gReady = gWorld.Size() > 0;

	// waterside places (fishing spots), from the cell water levels in the vanilla data
	{
		Ltx water;
		water.LoadFile(Settings::ConfigDir() + "/water.ltx");
		for (auto& n : gWorld.nodes) {
			char key[16];
			std::snprintf(key, sizeof(key), "%X", n.location);
			if (water.Has("water", key)) n.water = true;
		}
	}
	// interior cells of every dungeon, boss cell last: a clearing squad works through them section by section
	int dungeonsWithCells = 0;
	{
		std::unordered_map<RE::FormID, RE::FormID> bossCellOf;   // location -> cell holding its Boss ref
		for (auto* loc : dh->GetFormArray<RE::BGSLocation>()) {
			if (!loc) continue;
			for (auto& sr : loc->specialRefs)
				if (sr.type && sr.type == r.lcrtBoss && sr.refData.parentSpaceID) bossCellOf[loc->GetFormID()] = sr.refData.parentSpaceID;
		}
		for (auto* cell : dh->interiorCells) {
			if (!cell) continue;
			auto* cl = cell->GetLocation();
			for (int guard = 0; cl && guard < 4; cl = cl->parentLoc, ++guard) {
				auto it = gByLoc.find(cl->GetFormID());
				if (it != gByLoc.end() && gWorld.nodes[it->second].dungeon) { gWorld.nodes[it->second].cells.push_back(cell->GetFormID()); break; }
			}
		}
		for (auto& n : gWorld.nodes) {
			if (n.cells.empty()) continue;
			std::sort(n.cells.begin(), n.cells.end());
			auto bc = bossCellOf.find(n.location);
			n.bossCell = bc != bossCellOf.end() ? bc->second : 0;
			std::stable_partition(n.cells.begin(), n.cells.end(), [&](RE::FormID c) { return c != n.bossCell; });   // boss last
			++dungeonsWithCells;
		}
	}

	{
		int perWs[kWorldspaces] = {};
		for (auto& n : gWorld.nodes) ++perWs[n.ws];
		Log::Line("[graph] worldspaces: Tamriel " + std::to_string(perWs[0]) + " places, Solstheim " + std::to_string(perWs[1]) + (r.solstheim ? " (Dragonborn loaded)" : " (Dragonborn not installed)"));
		int shown = 0;
		for (auto& n : gWorld.nodes)
			if (n.ws == 1 && shown++ < 40) Log::Line("[graph]   solstheim " + n.name + " kind=" + KindName(n.kind) + " at " + std::to_string(int(n.x)) + "," + std::to_string(int(n.y)));
	}
	std::map<std::string, int> kinds;
	for (auto& n : gWorld.nodes) kinds[KindName(n.kind)]++;
	std::string ks;
	for (auto& [k, v] : kinds) ks += " " + k + "=" + std::to_string(v);
	Log::Line("[graph] locations=" + std::to_string(defs.size()) + " (marker " + std::to_string(viaMarker) + ", positions.ltx " +
			  std::to_string(viaSupplement) + ", unplaced " + std::to_string(noPos) + ") -> smart terrains=" + std::to_string(gWorld.Size()) +
			  " components=" + std::to_string(gWorld.ComponentCount()) + " dungeonsWithCells=" + std::to_string(dungeonsWithCells) + " |" + ks);
}
}
