#pragma once
#include "ALife/PCH.h"
#include "ALife/Core.h"

// Every form A-Life touches is resolved by FormID (never by editor ID: the engine drops
// editor IDs for most form types without third-party tweaks).
namespace ALife::Forms {
inline constexpr std::string_view kPlugin = "ALife.esp"sv;
inline constexpr std::string_view kFactionsPlugin = "ALifeFactions.esp"sv;   // one hidden faction per human A-Life faction + relations (generated from factions.ltx)
inline constexpr RE::FormID kFactionBase = 0x800;                          // + enum Faction index

// World space index of a worldspace: 0 Tamriel (and its child worldspaces), 1 Solstheim (and its children), -1 anything else.
int WsIndex(const RE::TESWorldSpace* a_ws);
RE::TESWorldSpace* WsObj(int a_ws);

// ---- ALife.esp local IDs (ESL range 0x800-0xFFF). Kept stable by tools/build_esp.py and CK saves.
namespace Local {
inline constexpr RE::FormID HoldingCell = 0x900;
inline constexpr RE::FormID SlotMarker0 = 0x910;  // 0x910..0x912
inline constexpr RE::FormID HoldingMarker = 0x913;
inline constexpr RE::FormID PackTravel = 0x920;
inline constexpr RE::FormID PackSandbox = 0x921;
inline constexpr RE::FormID PackFollow = 0x922;
inline constexpr RE::FormID PackFlee = 0x923;
inline constexpr RE::FormID PackCampHold = 0x924;     // camp life (docs/CAMPLIFE.md): mode rank 5, sandbox radius 150 at a job marker
inline constexpr RE::FormID PackCampSit = 0x925;      // mode rank 6, sandbox radius 450 at a campfire / bed
inline constexpr RE::FormID JobMarker0 = 0x980;       // 0x980..0x98E camp life job markers
inline constexpr int kJobMarkers = 15;
inline constexpr RE::FormID FactionALife = 0x930;
inline constexpr RE::FormID FactionBandit = 0x931;
inline constexpr RE::FormID FactionHunter = 0x932;
inline constexpr RE::FormID FactionAdventurer = 0x933;
inline constexpr RE::FormID FactionMode = 0x934;          // rank selects package: 1 travel 2 sandbox 3 follow 4 flee 5 camp hold 6 camp sit
inline constexpr RE::FormID FactionSlot0 = 0x935;         // 0x935..0x937 per online slot (group allies)
inline constexpr RE::FormID FactionPlayerEnemy = 0x938;   // enemy reaction to PlayerFaction
inline constexpr RE::FormID MCMQuest = 0x940;
inline constexpr RE::FormID UninstallMsg = 0x945;
inline constexpr RE::FormID ListWorkingPerks = 0x950;
inline constexpr RE::FormID ListCastableSpells = 0x951;
inline constexpr RE::FormID ListDaedricArtifacts = 0x952;
inline constexpr RE::FormID KwPoolActor = 0x960;
inline constexpr RE::FormID KwTravelTarget = 0x961;
inline constexpr RE::FormID KwLeader = 0x962;
inline constexpr RE::FormID KwFleeTarget = 0x963;
inline constexpr RE::FormID RaidHaulList = 0x970;
inline constexpr RE::FormID PoolNPCBase = 0xA00;  // 0xA00..0xAA5
inline constexpr RE::FormID PoolRefBase = 0xB00;  // 0xB00..0xBA5
}

// ---- Skyrim.esm / Update.esm (verified against Skyrim.esm, md5 9d3d112d0c420eae1d49f79abc013de6)
namespace Vanilla {
inline constexpr RE::FormID PlayerRef = 0x14;
inline constexpr RE::FormID Tamriel = 0x3C;
inline constexpr RE::FormID Gold = 0xF;
inline constexpr RE::FormID Lockpick = 0xA;
inline constexpr RE::FormID LcrtBoss = 0x130F7;
inline constexpr RE::FormID LcrtBossContainer = 0x130F8;
inline constexpr RE::FormID CurrentFollowerFaction = 0x5C84E;
inline constexpr RE::FormID PlayerFollowerFaction = 0x84D1B;
inline constexpr RE::FormID PlayerFaction = 0xDB1;
inline constexpr RE::FormID JobMerchantFaction = 0x51596;
inline constexpr RE::FormID JobInnkeeperFaction = 0x5091B;
inline constexpr RE::FormID JobFarmerFaction = 0x51591;
inline constexpr RE::FormID JobBlacksmithFaction = 0x5091D;
inline constexpr RE::FormID JobApothecaryFaction = 0x5091C;
inline constexpr RE::FormID JobCourtWizardFaction = 0x5091E;
inline constexpr RE::FormID JobJarlFaction = 0x50920;
inline constexpr RE::FormID JobStewardFaction = 0x50922;
inline constexpr RE::FormID IsGuardFaction = 0x86EEE;
inline constexpr RE::FormID GuardDialogueFaction = 0x2BE3B;
inline constexpr RE::FormID CrimeFactionWhiterun = 0x267EA;
inline constexpr RE::FormID CWSonsFaction = 0x2BF9B;
inline constexpr RE::FormID CWImperialFaction = 0x2BF9A;
inline constexpr RE::FormID WhiterunHoldLocation = 0x16772;
inline constexpr RE::FormID WhiterunLocation = 0x18A56;   // MNAM marker 0x29AAF lives in WhiterunWorld (child of Tamriel)
inline constexpr RE::FormID WhiterunWorld = 0x1A26F;      // WNAM parent = Tamriel 0x3C
inline constexpr RE::FormID HelgenLocation = 0x18A4A;     // HelgenKeep01 / Helgen cells XLCN
inline constexpr RE::FormID LALPrisonCell = 0x21594;      // AbandonedPrison01 (LAL start cell, vanilla record)
inline constexpr RE::FormID RiverwoodLocation = 0x13163;
inline constexpr RE::FormID RoriksteadLocation = 0x18A47;
inline constexpr RE::FormID FortGreymoorLocation = 0x1917D;
inline constexpr RE::FormID DustmansCairnLocation = 0x19173;
inline constexpr RE::FormID SilentMoonsCampLocation = 0x192A9;
inline constexpr RE::FormID KwLocTypeHold = 0x16771;
inline constexpr RE::FormID KwLocTypeDungeon = 0x130DB;
inline constexpr RE::FormID KwLocTypeCity = 0x13168;
inline constexpr RE::FormID KwLocTypeTown = 0x13166;
inline constexpr RE::FormID KwLocTypeSettlement = 0x13167;
inline constexpr RE::FormID KwLocTypeDwelling = 0x130DC;
inline constexpr RE::FormID KwLocTypePlayerHouse = 0xFC1A3;
inline constexpr RE::FormID KwLocTypeFarm = 0x18EF0;
inline constexpr RE::FormID KwLocTypeHabitation = 0x39793;
inline constexpr RE::FormID KwLocTypeVampireLair = 0x130EB;
inline constexpr RE::FormID KwLocTypeDraugrCrypt = 0x130E2;
inline constexpr RE::FormID KwLocTypeBanditCamp = 0x130DF;
inline constexpr RE::FormID KwLocTypeMilitaryFort = 0x130E7;
inline constexpr RE::FormID KwLocTypeGiantCamp = 0x130E5;
inline constexpr RE::FormID KwLocTypeInn = 0x1CB87;
inline constexpr RE::FormID KwLocTypeStore = 0x1CB86;
inline constexpr RE::FormID KwLocTypeClearable = 0xF5E80;
inline constexpr RE::FormID KwLocTypeDwarvenAutomatons = 0x130E3;
inline constexpr RE::FormID KwLocTypeAnimalDen = 0x130DE;
inline constexpr RE::FormID KwLocTypeDragonLair = 0x130E0;
inline constexpr RE::FormID KwLocTypeMine = 0x18EF1;
inline constexpr RE::FormID KwActorTypeNPC = 0x13794;
inline constexpr RE::FormID KwArmorHeavy = 0x6BBD2;
inline constexpr RE::FormID KwArmorLight = 0x6BBD3;
inline constexpr RE::FormID KwArmorClothing = 0x6BBE8;
inline constexpr RE::FormID KwArmorShield = 0x965B2;
inline constexpr RE::FormID KwWeapTypeBow = 0x1E715;
inline constexpr RE::FormID KwWeapTypeStaff = 0x1E716;
inline constexpr RE::FormID KwVendorItemFood = 0x8CDEA;
inline constexpr RE::FormID KwVendorItemPotion = 0x8CDEC;
inline constexpr RE::FormID KwVendorItemArrow = 0x917E7;
inline constexpr RE::FormID KwMagicAlchRestoreHealth = 0x42503;
inline constexpr RE::FormID MQ101 = 0x3372B;
inline constexpr RE::FormID MQ104 = 0x2610C;       // Dragon Rising
inline constexpr RE::FormID ClassBanditMelee = 0x1CE17;
inline constexpr RE::FormID ClassBanditMissile = 0x15BE7;
inline constexpr RE::FormID ClassBanditWizard = 0x39D30;
inline constexpr RE::FormID ClassRanger = 0x13181;
inline constexpr RE::FormID ClassWarrior1H = 0x13176;
inline constexpr RE::FormID ClassWarrior2H = 0x1CE15;
inline constexpr RE::FormID ClassSpellsword = 0x13177;
inline constexpr RE::FormID ClassScout = 0x1317D;
inline constexpr RE::FormID OutfitBanditMeleeHeavy = 0x15BE6;
inline constexpr RE::FormID OutfitBanditMeleeShield = 0xC0197;
inline constexpr RE::FormID OutfitBanditMeleeNoShield = 0x1E608;
inline constexpr RE::FormID OutfitBanditMissile = 0x37C4D;
inline constexpr RE::FormID OutfitBanditBoss = 0xA95F8;
inline constexpr RE::FormID OutfitHunter01 = 0x106988;
inline constexpr RE::FormID IdleSurrender = 0x105D47;
}

// ---- Optional runtime-only plugins (never masters)
inline constexpr std::string_view kFishPlugin = "ccbgssse001-fish.esm"sv;
inline constexpr std::string_view kSurvivalPlugin = "ccqdrsse001-survivalmode.esl"sv;
inline constexpr std::string_view kLALPlugin = "Alternate Start - Live Another Life.esp"sv;

template <class T>
T* Get(RE::FormID a_id)
{
	return RE::TESForm::LookupByID<T>(a_id);
}

template <class T>
T* Mine(RE::FormID a_local)
{
	auto* dh = RE::TESDataHandler::GetSingleton();
	return dh ? dh->LookupForm<T>(a_local, kPlugin) : nullptr;
}

struct Resolved
{
	bool pluginLoaded{ false };
	bool factionsLoaded{ false };
	std::array<RE::TESFaction*, kFactionCount> fFaction{};   // ALifeFactions.esp, indexed by enum Faction
	RE::TESObjectCELL* holdingCell{ nullptr };
	std::array<RE::TESObjectREFR*, 3> slotMarkers{};
	std::array<RE::TESObjectREFR*, Local::kJobMarkers> jobMarkers{};   // camp life (null with an ALife.esp from before M5h)
	RE::TESObjectREFR* holdingMarker{ nullptr };
	RE::TESFaction* fALife{ nullptr };
	RE::TESFaction* fBandit{ nullptr };
	RE::TESFaction* fHunter{ nullptr };
	RE::TESFaction* fAdventurer{ nullptr };
	RE::TESFaction* fMode{ nullptr };
	std::array<RE::TESFaction*, 3> fSlot{};
	RE::TESFaction* fPlayerEnemy{ nullptr };
	RE::BGSKeyword* kwPool{ nullptr };
	RE::BGSKeyword* kwTravel{ nullptr };
	RE::BGSKeyword* kwLeader{ nullptr };
	RE::BGSKeyword* kwFlee{ nullptr };
	RE::BGSListForm* workingPerks{ nullptr };
	RE::BGSListForm* castableSpells{ nullptr };
	RE::BGSListForm* daedricArtifacts{ nullptr };
	RE::TESLevItem* raidHaul{ nullptr };
	RE::BGSMessage* uninstallMsg{ nullptr };
	// vanilla
	RE::BGSLocationRefType* lcrtBoss{ nullptr };
	RE::BGSLocationRefType* lcrtBossContainer{ nullptr };
	RE::BGSLocation* whiterunHold{ nullptr };
	RE::TESWorldSpace* tamriel{ nullptr };
	RE::TESWorldSpace* solstheim{ nullptr };      // Dragonborn.esm, when installed
	RE::TESBoundObject* gold{ nullptr };
	// optional runtime plugins
	bool fishingLoaded{ false };
	std::vector<RE::TESBoundObject*> fishItems;
	RE::TESBoundObject* fishingRod{ nullptr };
	bool survivalLoaded{ false };
	RE::TESGlobal* survivalEnabled{ nullptr };
	RE::TESQuest* lalChargen{ nullptr };
};

const Resolved& R();
void ResolveAll();   // kDataLoaded
bool HasKw(const RE::BGSLocation* a_loc, RE::FormID a_kw);
}
