// CivilWar: the Skyrim side of T10 (docs/CIVILWAR.md). Reads the vanilla Civil War (CWOwner keyword data on the hold locations,
// CWObj stage 255) and the faction gate quests (Fort Dawnguard's rebuild), and hands them to the board (src/core/CivilWar.cpp).
// Nothing is saved: the game's own save holds the war, so every load re-reads it.
#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Settings.h"
#include "ALife/Systems.h"

namespace ALife::CivilWar {
namespace {
	constexpr RE::FormID kCWOwner = 0x02A456;            // Skyrim.esm KYWD CWOwner: 1 = Imperials, 2 = Sons
	constexpr RE::FormID kCW = 0x019E53;                 // Skyrim.esm QUST CW (CWScript)
	constexpr RE::FormID kCWObj = 0x029940;              // Skyrim.esm QUST CWObj: stage 255 = the war is won
	constexpr RE::FormID kAllegiance = 0x023A43;         // Skyrim.esm GLOB CWPlayerAllegiance: 1 Imperials, 2 Sons
	constexpr RE::FormID kHaafingar = 0x016770, kEastmarch = 0x01676A;   // the capitals' holds: who took the other's tells the winner

	struct Gate { Faction faction; RE::TESQuest* quest; std::uint16_t stage; std::string label; RE::TESGlobal* global{ nullptr }; float value{ 1.f }; bool isGlobal{ false }; };

	RE::BGSKeyword* gOwnerKw{};
	RE::TESQuest *gCW{}, *gCWObj{};
	RE::TESGlobal* gAllegiance{};
	std::vector<std::pair<FormID, RE::BGSLocation*>> gHolds;
	std::vector<Gate> gGates;
	std::array<bool, kFactionCount> gHasGate{};
	bool gForce{ true };
	float gTimer{ 0.f };
	std::string gLastSig;

	int Owner(const RE::BGSLocation* a_loc)
	{
		if (!a_loc || !gOwnerKw) return -1;
		for (auto& kd : a_loc->keywordData)   // what Papyrus Location.GetKeywordData reads
			if (kd.keyword == gOwnerKw) return int(kd.data);
		return -1;
	}
	RE::BGSLocation* Hold(FormID a_id)
	{
		for (auto& [id, loc] : gHolds) if (id == a_id) return loc;
		return nullptr;
	}
	bool Reached(const RE::TESQuest* a_q, std::uint16_t a_stage) { return a_q && (a_q->GetCurrentStageID() >= a_stage || a_q->IsCompleted()); }
	std::uint8_t Side(int a_vanilla)
	{
		const auto& c = Sim::Cfg();
		return a_vanilla == 1 ? c.cwSides[0] : a_vanilla == 2 ? c.cwSides[1] : kNoFaction;
	}
	std::uint8_t Loser()
	{
		if (!Reached(gCWObj, 255)) return kNoFaction;
		const int haaf = Owner(Hold(kHaafingar)), east = Owner(Hold(kEastmarch));
		if (haaf == 2 && east != 1) return Side(1);   // the Sons took Solitude
		if (east == 1 && haaf != 2) return Side(2);   // the Legion took Windhelm
		const int a = gAllegiance ? int(gAllegiance->value) : 0;   // the finale is always the player's side winning
		return a == 1 ? Side(2) : a == 2 ? Side(1) : kNoFaction;
	}
}

void Init()
{
	const auto& c = Sim::Cfg();
	gOwnerKw = Forms::Get<RE::BGSKeyword>(kCWOwner);
	gCW = Forms::Get<RE::TESQuest>(kCW);
	gCWObj = Forms::Get<RE::TESQuest>(kCWObj);
	gAllegiance = Forms::Get<RE::TESGlobal>(kAllegiance);
	gHolds.clear();
	for (auto& [id, f] : c.holdFaction) gHolds.emplace_back(id, Forms::Get<RE::BGSLocation>(id));
	gGates.clear();
	gHasGate.fill(false);
	auto* dh = RE::TESDataHandler::GetSingleton();
	for (auto& g : c.gates) {
		gHasGate[int(g.faction)] = true;   // listed: shut unless one of its quests is found and reached
		if (g.global) {   // dragon = Skyrim.esm|042851=1: a global at least value (DragonsReturned, docs/DRAGONS.md)
			RE::TESGlobal* gl = nullptr;
			if (dh && dh->LookupModByName(g.plugin))
				if (auto* f = dh->LookupForm(g.quest & 0xFFFFFF, g.plugin)) gl = f->As<RE::TESGlobal>();
			char label[96];
			std::snprintf(label, sizeof(label), "%s|%06X=%g", g.plugin.c_str(), g.quest & 0xFFFFFF, double(g.value));
			Gate gate{ g.faction, nullptr, 0, label };
			gate.global = gl;
			gate.value = g.value;
			gate.isGlobal = true;
			gGates.push_back(gate);
			if (!gl) Log::Line(std::string("[cw] gate ") + FactionId(g.faction) + ": " + label + " not loaded (that gate stays shut)");
			continue;
		}
		RE::TESQuest* q = nullptr;
		if (dh && dh->LookupModByName(g.plugin))
			if (auto* f = dh->LookupForm(g.quest & 0xFFFFFF, g.plugin)) q = f->As<RE::TESQuest>();
		char label[96];
		std::snprintf(label, sizeof(label), "%s|%06X>=%u", g.plugin.c_str(), g.quest & 0xFFFFFF, unsigned(g.stage));
		gGates.push_back({ g.faction, q, g.stage, label });
		if (!q) Log::Line(std::string("[cw] gate ") + FactionId(g.faction) + ": " + label + " not loaded (that gate stays shut)");
	}
	int holds = 0;
	for (auto& h : gHolds) holds += h.second != nullptr;
	Log::Line("[cw] ready: CWOwner " + std::string(gOwnerKw ? "ok" : "MISSING") + ", CWObj " + (gCWObj ? "ok" : "MISSING") + ", holds " +
			  std::to_string(holds) + "/" + std::to_string(gHolds.size()) + ", gates " + std::to_string(gGates.size()));
}

void Reset() { gForce = true; gLastSig.clear(); }

// The board and the sim lock are the caller's (Sim::MainTick). First call after a load or a new game always reads.
void Tick(Board& a_b, float a_realDt)
{
	gTimer += a_realDt;
	if (!gForce && gTimer < 3.f) return;
	gTimer = 0.f;
	const bool first = gForce;
	gForce = false;
	const auto& c = Sim::Cfg();
	std::string sig;
	if (c.civilWar && gOwnerKw) {
		for (auto& [id, loc] : gHolds) {
			const std::uint8_t f = Side(Owner(loc));   // unset (-1) before CWScript's OnInit: the config's start owner stays
			if (f != kNoFaction) a_b.SetHoldOwner(id, Faction(f));
			char b[24];
			std::snprintf(b, sizeof(b), "%X=%d ", id, Owner(loc));
			sig += b;
		}
		a_b.SetWarOver(Loser());
		sig += std::string("loser=") + (a_b.WarLoser() == kNoFaction ? "none" : FactionId(Faction(a_b.WarLoser())));
	}
	for (int f = 0; f < kFactionCount; ++f) {
		if (!gHasGate[f]) continue;
		bool open = false;
		for (auto& g : gGates)
			if (int(g.faction) == f) open |= g.isGlobal ? (g.global && g.global->value >= g.value) : Reached(g.quest, g.stage);
		if (f == int(Faction::Dragon) && !Settings::Get().dragons) open = false;   // the MCM toggle shuts the dragons whatever the game says
		a_b.SetGated(Faction(f), !open);
		sig += std::string(" ") + FactionId(Faction(f)) + (open ? "=open" : "=shut");
	}
	if (sig != gLastSig) {
		Log::Line(std::string("[cw] ") + (first ? "read at load: " : "changed: ") + sig);
		gLastSig = sig;
	}
}

std::uint8_t HoldOwner(FormID a_hold)
{
	if (Sim::BoardReady()) return Sim::B().EffectiveOwner(a_hold);
	auto it = Sim::Cfg().holdFaction.find(a_hold);
	return it == Sim::Cfg().holdFaction.end() ? kNoFaction : std::uint8_t(it->second);
}

// Bridge "cw": dump; "cw flip <hold hex> <1|2>" runs CWScript.SetOwnerKeywordDataOnly (the war's own owner write) on the CW quest.
void Debug(const std::string& a_arg)
{
	if (a_arg.rfind("flip", 0) == 0) {
		unsigned hold = 0;
		int side = 0;
		if (std::sscanf(a_arg.c_str(), "flip %x %d", &hold, &side) != 2 || !Hold(hold) || (side != 1 && side != 2) || !gCW) {
			Log::Line("[cw] flip <hold hex> <1|2>: bad arguments or no CW quest");
			return;
		}
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) return;
		const auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(static_cast<RE::VMTypeID>(RE::TESQuest::FORMTYPE), gCW);
		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> cb;
		std::unique_ptr<RE::BSScript::IFunctionArguments> args{ RE::MakeFunctionArguments(static_cast<RE::BGSLocation*>(Hold(hold)), std::int32_t(side)) };
		const bool ok = vm->DispatchMethodCall(handle, "CWScript", "SetOwnerKeywordDataOnly", args.get(), cb);
		Log::Line("[cw] flip " + a_arg.substr(5) + ": CWScript.SetOwnerKeywordDataOnly dispatched=" + std::to_string(ok));
		return;
	}
	std::lock_guard l(Sim::Lock());
	if (!Sim::BoardReady()) { Log::Line("[cw] board not ready"); return; }
	Board& b = Sim::B();
	Log::Line("[cw] war: CWObj stage " + std::to_string(gCWObj ? gCWObj->GetCurrentStageID() : -1) + (gCWObj && gCWObj->IsCompleted() ? " (completed)" : "") +
			  ", allegiance " + std::to_string(gAllegiance ? int(gAllegiance->value) : -1) + ", loser " +
			  (b.WarLoser() == kNoFaction ? "none" : FactionId(Faction(b.WarLoser()))) + ", stood down " + std::to_string(b.stats.stoodDown) +
			  ", guards swapped " + std::to_string(b.stats.guardsSwapped) + ", hold flips " + std::to_string(b.stats.holdFlips));
	for (auto& [id, loc] : gHolds) {
		int places = 0, held[2] = { 0, 0 }, squads[2] = { 0, 0 };
		const auto& cfg = Sim::Cfg();
		for (auto& n : Graph::Nodes()) {
			if (n.hold != id || !n.holdAligned) continue;
			++places;
			for (int s = 0; s < 2; ++s) held[s] += b.smart[n.id].faction == cfg.cwSides[s];
		}
		for (auto& g : b.squads) {
			if (g.livingCount() == 0 || g.respawnSmart < 0) continue;
			const auto* n = Graph::Node(std::uint32_t(g.respawnSmart));
			if (!n || n->hold != id) continue;
			for (int s = 0; s < 2; ++s) squads[s] += std::uint8_t(g.faction) == cfg.cwSides[s];
		}
		const std::uint8_t eff = b.EffectiveOwner(id);
		char line[256];
		std::snprintf(line, sizeof(line), "[cw]   hold %X %s: game %d, board %s, %d places (held %s %d / %s %d), squads from there %s %d / %s %d", id,
		              loc && loc->GetName() ? loc->GetName() : "?", Owner(loc), eff == kNoFaction ? "none" : FactionId(Faction(eff)), places,
		              FactionId(Faction(cfg.cwSides[0])), held[0], FactionId(Faction(cfg.cwSides[1])), held[1], FactionId(Faction(cfg.cwSides[0])), squads[0],
		              FactionId(Faction(cfg.cwSides[1])), squads[1]);
		Log::Line(line);
	}
	for (auto& g : gGates)
		Log::Line(std::string("[cw]   gate ") + FactionId(g.faction) + " " + g.label + ": " + (g.quest ? "stage " + std::to_string(g.quest->GetCurrentStageID()) : std::string("not loaded")) +
				  (Reached(g.quest, g.stage) ? " reached" : "") + " -> " + (b.Gated(g.faction) ? "shut" : "open") + ", squads " +
				  std::to_string(b.SquadCount(g.faction)) + " (spawned " + std::to_string(b.stats.spawned[int(g.faction)]) + ")");
	int counts[2] = { 0, 0 };
	for (auto& g : b.squads)
		for (int s = 0; s < 2; ++s) counts[s] += std::uint8_t(g.faction) == Sim::Cfg().cwSides[s] && g.livingCount() > 0 && g.ws == 0;
	Log::Line("[cw]   Tamriel squads: " + std::string(FactionId(Faction(Sim::Cfg().cwSides[0]))) + " " + std::to_string(counts[0]) + ", " +
			  FactionId(Faction(Sim::Cfg().cwSides[1])) + " " + std::to_string(counts[1]));
}

}  // namespace ALife::CivilWar
