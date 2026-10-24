#include "ALife/Forms.h"
#include "ALife/Log.h"

namespace ALife::Forms {

int WsIndex(const RE::TESWorldSpace* a_ws)
{
	if (!a_ws) return -1;
	const auto& r = R();
	if (a_ws == r.tamriel || a_ws->parentWorld == r.tamriel) return 0;
	if (r.solstheim && (a_ws == r.solstheim || a_ws->parentWorld == r.solstheim)) return 1;
	return -1;
}

RE::TESWorldSpace* WsObj(int a_ws) { return a_ws == 1 ? R().solstheim : R().tamriel; }
namespace {
	Resolved g;
	template <class T>
	T* Opt(RE::FormID a_local, std::string_view a_plugin)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh || !dh->LookupModByName(a_plugin)) return nullptr;
		// not LookupForm<T>: it compares against T::FORMTYPE, which is None for base classes (TESBoundObject)
		auto* form = dh->LookupForm(a_local, a_plugin);
		return form ? form->As<T>() : nullptr;
	}
}
const Resolved& R() { return g; }

bool HasKw(const RE::BGSLocation* a_loc, RE::FormID a_kw)
{
	if (!a_loc) return false;
	auto* kw = Get<RE::BGSKeyword>(a_kw);
	return kw && a_loc->HasKeyword(kw);
}

void ResolveAll()
{
	g = Resolved{};
	auto* dh = RE::TESDataHandler::GetSingleton();
	g.pluginLoaded = dh && dh->LookupModByName(kPlugin);
	if (g.pluginLoaded) {
		using namespace Local;
		g.holdingCell = Forms::Mine<RE::TESObjectCELL>(HoldingCell);
		for (int i = 0; i < 3; ++i) g.slotMarkers[i] = Forms::Mine<RE::TESObjectREFR>(SlotMarker0 + i);
		for (int i = 0; i < kJobMarkers; ++i) g.jobMarkers[std::size_t(i)] = Forms::Mine<RE::TESObjectREFR>(JobMarker0 + RE::FormID(i));
		g.holdingMarker = Forms::Mine<RE::TESObjectREFR>(HoldingMarker);
		g.fALife = Forms::Mine<RE::TESFaction>(FactionALife);
		g.fBandit = Forms::Mine<RE::TESFaction>(FactionBandit);
		g.fHunter = Forms::Mine<RE::TESFaction>(FactionHunter);
		g.fAdventurer = Forms::Mine<RE::TESFaction>(FactionAdventurer);
		g.fMode = Forms::Mine<RE::TESFaction>(FactionMode);
		for (int i = 0; i < 3; ++i) g.fSlot[i] = Forms::Mine<RE::TESFaction>(FactionSlot0 + i);
		g.fPlayerEnemy = Forms::Mine<RE::TESFaction>(FactionPlayerEnemy);
		g.kwPool = Forms::Mine<RE::BGSKeyword>(KwPoolActor);
		g.kwTravel = Forms::Mine<RE::BGSKeyword>(KwTravelTarget);
		g.kwLeader = Forms::Mine<RE::BGSKeyword>(KwLeader);
		g.kwFlee = Forms::Mine<RE::BGSKeyword>(KwFleeTarget);
		g.workingPerks = Forms::Mine<RE::BGSListForm>(ListWorkingPerks);
		g.castableSpells = Forms::Mine<RE::BGSListForm>(ListCastableSpells);
		g.daedricArtifacts = Forms::Mine<RE::BGSListForm>(ListDaedricArtifacts);
		g.raidHaul = Forms::Mine<RE::TESLevItem>(RaidHaulList);
		g.uninstallMsg = Forms::Mine<RE::BGSMessage>(UninstallMsg);
	}
	if (dh && dh->LookupModByName(kFactionsPlugin)) {
		int ok = 0;
		for (int i = 0; i < kHumanFactionCount; ++i) {   // monster factions have no ALifeFactions.esp record: creatures keep their vanilla factions
			g.fFaction[i] = dh->LookupForm<RE::TESFaction>(kFactionBase + RE::FormID(i), kFactionsPlugin);
			ok += g.fFaction[i] ? 1 : 0;
		}
		g.factionsLoaded = ok == kHumanFactionCount;
	}
	g.lcrtBoss = Get<RE::BGSLocationRefType>(Vanilla::LcrtBoss);
	g.lcrtBossContainer = Get<RE::BGSLocationRefType>(Vanilla::LcrtBossContainer);
	g.whiterunHold = Get<RE::BGSLocation>(Vanilla::WhiterunHoldLocation);
	g.tamriel = Get<RE::TESWorldSpace>(Vanilla::Tamriel);
	if (dh) g.solstheim = dh->LookupForm<RE::TESWorldSpace>(0x800, "Dragonborn.esm"sv);   // DLC2SolstheimWorld
	g.gold = Get<RE::TESBoundObject>(Vanilla::Gold);

	// Anniversary Edition fishing: looked up at runtime only, never a master.
	if (dh && dh->LookupModByName(kFishPlugin)) {
		g.fishingLoaded = true;
		// raw fish food items (temperate lakes/streams; Whiterun Hold waters)
		for (RE::FormID id : { 0x898u, 0x89Bu, 0x8A2u, 0x89Cu, 0x897u, 0x8A0u, 0x89Eu, 0x8A1u }) {
			if (auto* f = Opt<RE::TESBoundObject>(id, kFishPlugin)) g.fishItems.push_back(f);
		}
		g.fishingRod = Opt<RE::TESBoundObject>(0x84F, kFishPlugin);   // ccBGSSSE001_FishingRodImperialWeap
		if (g.fishItems.empty()) g.fishingLoaded = false;
	}
	// Without the AE fishing content anglers still land vanilla catches: the five pond-fish ingredients and Salmon Meat (all Skyrim.esm).
	if (g.fishItems.empty()) {
		for (RE::FormID id : { 0x106E1Cu, 0x106E1Bu, 0x106E1Au, 0x106E19u, 0x106E18u, 0x65C9Fu })
			if (auto* f = Get<RE::TESBoundObject>(id)) g.fishItems.push_back(f);
		g.fishingLoaded = !g.fishItems.empty();
	}
	// Survival Mode: GetFormFromFile-style runtime lookup.
	if (dh && dh->LookupModByName(kSurvivalPlugin)) {
		g.survivalEnabled = Opt<RE::TESGlobal>(0x826, kSurvivalPlugin);   // Survival_ModeEnabled
		g.survivalLoaded = g.survivalEnabled != nullptr;
	}
	// Live Another Life: the quest keeps its editor ID (quests store EDIDs), plugin never a master.
	g.lalChargen = RE::TESForm::LookupByEditorID<RE::TESQuest>("ARTHLALChargenQuest");

	Log::Line(std::string("[forms] ALife.esp=") + (g.pluginLoaded ? "yes" : "NO") + " ALifeFactions.esp=" + (g.factionsLoaded ? "yes" : "NO") +
			  " holdingCell=" + (g.holdingCell ? "ok" : "missing") +
			  " slotMarkers=" + std::to_string(int(g.slotMarkers[0] != nullptr) + int(g.slotMarkers[1] != nullptr) + int(g.slotMarkers[2] != nullptr)) + "/3" +
			  " holdingMarker=" + (g.holdingMarker ? "ok" : "missing") +
			  " jobMarkers=" + std::to_string(std::count_if(g.jobMarkers.begin(), g.jobMarkers.end(), [](auto* r) { return r != nullptr; })) + "/" +
			  std::to_string(Local::kJobMarkers) +
			  " whiterunHold=" + (g.whiterunHold ? "ok" : "missing") + " tamriel=" + (g.tamriel ? "ok" : "missing") +
			  " fishing=" + (g.fishingLoaded ? "yes" : "no") +
			  " survival=" + (g.survivalLoaded ? "yes" : "no") +
			  " LAL=" + (g.lalChargen ? "yes" : "no"));
}
}
