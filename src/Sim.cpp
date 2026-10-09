#include "ALife/Forms.h"
#include "ALife/Jobs.h"
#include "ALife/Log.h"
#include "ALife/Merchants.h"
#include "ALife/Rep.h"
#include "ALife/Settings.h"
#include "ALife/Systems.h"
#include "ALife/Talk.h"

// Plugin host of the Anomaly-style board: builds the map, creates members, feeds the sim the game clock, and keeps the
// on-screen systems (Online) in step with it.
namespace ALife::Sim {
namespace {
	std::recursive_mutex gMx;
	Config gCfg;
	std::unique_ptr<Board> gBoard;
	Persist gP;
	bool gReady{ false };                 // a game is loaded and state is valid
	bool gLoading{ false };               // between kPreLoadGame and kPostLoadGame
	std::uint64_t gScenarioGroup{ 0 };    // the squad the last debug scenario placed (Numpad4 goes to it)
	int gScenarioIdx{ 0 };                // next scenario in the debug cycle
	bool gFilled{ false };                // the world has been populated for this save
	bool gUninstallPending{ false };
	bool gSweepPending{ false };          // sweep pool actors on the first main tick after load / new game
	bool gLagging{ false };
	float gGateHold{ 0.f };               // unpaused real seconds the open condition has held without a break
	constexpr float kGateDelay = 5.f;     // ~5 s after keep exit / cart move before the gate opens
	constexpr double kStepBudgetMs = 10.0;

	// ---------------------------------------------------------------- names
	const char* kNordM[] = { "Bjorn", "Hrolf", "Ulfric", "Torvar", "Gunnar", "Sigurd", "Ragnar", "Eirik", "Halvar", "Vigdis", "Arnbjorn", "Thorald", "Kjell", "Rolff" };
	const char* kNordF[] = { "Astrid", "Freydis", "Hilde", "Sigrid", "Ingrid", "Brynja", "Ragnhild", "Gerdur", "Jorunn", "Solveig", "Thyra", "Eydis" };
	const char* kImpM[] = { "Marcus", "Gaius", "Lucius", "Quintus", "Titus", "Decimus", "Varro", "Cassius", "Aulus", "Brutus" };
	const char* kImpF[] = { "Livia", "Aurelia", "Claudia", "Flavia", "Junia", "Valeria", "Octavia", "Serena", "Cornelia" };
	const char* kBreM[] = { "Henri", "Gaston", "Alain", "Bernard", "Mathieu", "Raymond", "Odric", "Jules", "Lucien" };
	const char* kBreF[] = { "Adrienne", "Celine", "Margot", "Elise", "Solene", "Mirabelle", "Odette", "Ysolde", "Colette" };
	const char* kRedM[] = { "Kematu", "Rashid", "Amir", "Hakim", "Faruk", "Nazir", "Sadri", "Tariq" };
	const char* kRedF[] = { "Saadia", "Iman", "Nadia", "Zaira", "Leyla", "Samira", "Farah", "Amina" };
	const char* kElfM[] = { "Aranel", "Faendal", "Ondolemar", "Talvas", "Brelyn", "Elrindir", "Nelos", "Athis", "Erandur", "Revyn" };
	const char* kElfF[] = { "Elenwen", "Nirya", "Ancarion", "Faralda", "Brelas", "Irileth", "Arivanya", "Sondas", "Ilmeni" };
	const char* kOrcM[] = { "Ghorbash", "Mauhulakh", "Burguk", "Yamarz", "Larak", "Dushnamub", "Ghak" };
	const char* kOrcF[] = { "Lob", "Sharamph", "Shuftharz", "Bolar", "Gharol", "Atub", "Ugor" };
	const char* kArgM[] = { "Derkeethus", "Keerava", "Madesi", "Scouts-Many-Marshes", "Neetrenaza", "Teeba-Ei", "Wujeeta" };
	const char* kArgF[] = { "Shahvee", "Deeja", "Talen-Jei", "Ree-Zeeus", "Weeping-Toad", "Meena" };
	const char* kKhaM[] = { "Ri'saad", "Ma'dran", "J'zargo", "Kharjo", "Ahkari", "Dro'marash", "Ma'jhad" };
	const char* kKhaF[] = { "Atahbah", "Ra'zhinda", "Khayla", "Ahkari", "Zaynabi", "Shavari" };
	template <std::size_t N>
	const char* Pick(const char* (&arr)[N], Rng& r) { return arr[r.index(int(N))]; }
	std::string MakeName(RaceId race, Sex sex, Rng& r)
	{
		const bool f = sex == Sex::Female;
		switch (race) {
		case RaceId::Nord: case RaceId::NordVampire: return f ? Pick(kNordF, r) : Pick(kNordM, r);
		case RaceId::Imperial: case RaceId::ImperialVampire: return f ? Pick(kImpF, r) : Pick(kImpM, r);
		case RaceId::Breton: case RaceId::BretonVampire: return f ? Pick(kBreF, r) : Pick(kBreM, r);
		case RaceId::Redguard: return f ? Pick(kRedF, r) : Pick(kRedM, r);
		case RaceId::Orsimer: return f ? Pick(kOrcF, r) : Pick(kOrcM, r);
		case RaceId::Argonian: return f ? Pick(kArgF, r) : Pick(kArgM, r);
		case RaceId::Khajiit: return f ? Pick(kKhaF, r) : Pick(kKhaM, r);
		default: return f ? Pick(kElfF, r) : Pick(kElfM, r);
		}
	}

	struct W { RaceId race; int w; };
	RaceId Weighted(const W* tbl, int n, Rng& r)
	{
		int tot = 0;
		for (int i = 0; i < n; ++i) tot += tbl[i].w;
		int x = r.index(tot);
		for (int i = 0; i < n; ++i) { if (x < tbl[i].w) return tbl[i].race; x -= tbl[i].w; }
		return tbl[0].race;
	}
	// Race mix per faction (all drawn from the 14 races the pool has faces for).
	RaceId RollRace(Faction f, Rng& r)
	{
		static const W bandit[] = { { RaceId::Nord, 40 }, { RaceId::Imperial, 12 }, { RaceId::Redguard, 10 }, { RaceId::Breton, 8 }, { RaceId::Dunmer, 8 },
			{ RaceId::Orsimer, 8 }, { RaceId::Bosmer, 5 }, { RaceId::Khajiit, 4 }, { RaceId::Argonian, 3 }, { RaceId::Altmer, 2 } };
		static const W forsworn[] = { { RaceId::Breton, 90 }, { RaceId::Nord, 10 } };
		static const W imperial[] = { { RaceId::Imperial, 45 }, { RaceId::Nord, 20 }, { RaceId::Redguard, 15 }, { RaceId::Breton, 12 }, { RaceId::Bosmer, 5 }, { RaceId::Altmer, 3 } };
		static const W storm[] = { { RaceId::Nord, 75 }, { RaceId::Dunmer, 5 }, { RaceId::Redguard, 5 }, { RaceId::Breton, 5 }, { RaceId::Orsimer, 5 }, { RaceId::Khajiit, 3 }, { RaceId::Argonian, 2 } };
		static const W thalmor[] = { { RaceId::Altmer, 88 }, { RaceId::Bosmer, 12 } };
		static const W hunter[] = { { RaceId::Nord, 50 }, { RaceId::Bosmer, 15 }, { RaceId::Imperial, 10 }, { RaceId::Redguard, 8 }, { RaceId::Breton, 6 },
			{ RaceId::Orsimer, 5 }, { RaceId::Khajiit, 3 }, { RaceId::Argonian, 3 } };
		static const W adv[] = { { RaceId::Nord, 25 }, { RaceId::Imperial, 12 }, { RaceId::Breton, 12 }, { RaceId::Redguard, 10 }, { RaceId::Altmer, 8 },
			{ RaceId::Bosmer, 8 }, { RaceId::Dunmer, 10 }, { RaceId::Orsimer, 6 }, { RaceId::Khajiit, 5 }, { RaceId::Argonian, 4 } };
		static const W merc[] = { { RaceId::Nord, 25 }, { RaceId::Redguard, 20 }, { RaceId::Imperial, 15 }, { RaceId::Orsimer, 15 }, { RaceId::Breton, 10 },
			{ RaceId::Dunmer, 5 }, { RaceId::Khajiit, 5 }, { RaceId::Argonian, 5 } };
		static const W mage[] = { { RaceId::Breton, 30 }, { RaceId::Altmer, 25 }, { RaceId::Dunmer, 20 }, { RaceId::Imperial, 15 }, { RaceId::Nord, 5 }, { RaceId::Redguard, 5 } };
		static const W necro[] = { { RaceId::Dunmer, 30 }, { RaceId::Breton, 25 }, { RaceId::Imperial, 15 }, { RaceId::Altmer, 15 }, { RaceId::Nord, 10 }, { RaceId::Redguard, 5 } };
		static const W vamp[] = { { RaceId::NordVampire, 2 }, { RaceId::ImperialVampire, 1 }, { RaceId::BretonVampire, 1 }, { RaceId::DunmerVampire, 2 } };
		static const W dawn[] = { { RaceId::Nord, 35 }, { RaceId::Imperial, 25 }, { RaceId::Redguard, 15 }, { RaceId::Breton, 15 }, { RaceId::Orsimer, 10 } };
		static const W silver[] = { { RaceId::Nord, 55 }, { RaceId::Imperial, 15 }, { RaceId::Breton, 10 }, { RaceId::Redguard, 10 }, { RaceId::Orsimer, 10 } };
		static const W vigil[] = { { RaceId::Nord, 35 }, { RaceId::Imperial, 30 }, { RaceId::Breton, 20 }, { RaceId::Redguard, 10 }, { RaceId::Altmer, 5 } };
		static const W khajiit[] = { { RaceId::Khajiit, 1 } };
#define ALIFE_TBL(t) return Weighted(t, int(std::size(t)), r)
		switch (f) {
		case Faction::Bandit: ALIFE_TBL(bandit);
		case Faction::Forsworn: ALIFE_TBL(forsworn);
		case Faction::Imperial: ALIFE_TBL(imperial);
		case Faction::Stormcloak: ALIFE_TBL(storm);
		case Faction::Thalmor: ALIFE_TBL(thalmor);
		case Faction::Hunter: ALIFE_TBL(hunter);
		case Faction::Mercenary: ALIFE_TBL(merc);
		case Faction::Mage: ALIFE_TBL(mage);
		case Faction::Necromancer: ALIFE_TBL(necro);
		case Faction::Vampire: ALIFE_TBL(vamp);
		case Faction::Dawnguard: ALIFE_TBL(dawn);
		case Faction::SilverHand: ALIFE_TBL(silver);
		case Faction::Vigilant: ALIFE_TBL(vigil);
		case Faction::Caravan: ALIFE_TBL(khajiit);
		default: ALIFE_TBL(adv);
		}
#undef ALIFE_TBL
	}

	float FemaleChance(Faction f)
	{
		switch (f) {
		case Faction::Bandit: case Faction::SilverHand: return 0.28f;
		case Faction::Imperial: case Faction::Stormcloak: case Faction::Dawnguard: return 0.3f;
		case Faction::Thalmor: case Faction::Mage: return 0.5f;
		default: return 0.4f;
		}
	}

	const char* LeaderTitle(Faction f)
	{
		switch (f) {
		case Faction::Bandit: case Faction::SilverHand: return "Boss";
		case Faction::Imperial: case Faction::Stormcloak: return "Officer";
		case Faction::Thalmor: return "Justiciar";
		case Faction::Forsworn: return "Chief";
		case Faction::Mercenary: case Faction::Dawnguard: return "Captain";
		case Faction::Mage: return "Adept";
		case Faction::Necromancer: return "Master";
		case Faction::Vampire: return "Elder";
		case Faction::Vigilant: return "Brother";
		case Faction::Caravan: return "Merchant";
		default: return "";
		}
	}

	bool InHold(const RE::BGSLocation* l, RE::FormID a_id)
	{
		for (int guard = 0; l && guard < 16; l = l->parentLoc, ++guard)
			if (l->GetFormID() == a_id) return true;
		return false;
	}

	// ---------------------------------------------------------------- news
	std::mutex gNewsMx;
	std::deque<std::string> gNews;
	float gNewsCooldown{ 0.f };

	// ---------------------------------------------------------------- the board's window onto Skyrim
	struct PluginHost : Host
	{
		int PlayerLevel() const override
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			return pc ? std::max<int>(1, pc->GetLevel()) : 1;
		}
		bool PlayerNear(float x, float y, float radius, int ws) const override
		{
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!pc || !pc->Is3DLoaded() || Forms::WsIndex(pc->GetWorldspace()) != ws) return false;
			const auto p = pc->GetPosition();
			const float dx = p.x - x, dy = p.y - y;
			return dx * dx + dy * dy < radius * radius;
		}
		Member MakeMember(Faction f, Tier tier, Role role, int level, bool leader, Rng& r, std::uint64_t id) override
		{
			Member m;
			m.id = id;
			m.faction = f;
			m.role = role;
			m.level = std::max(1, level);
			m.race = RollRace(f, r);
			m.sex = r.chance(FemaleChance(f)) ? Sex::Female : Sex::Male;
			m.name = MakeName(m.race, m.sex, r);
			if (leader) m.title = LeaderTitle(f);
			m.veteran = tier == Tier::Veteran;
			m.sticky = leader && tier != Tier::Novice;   // commanders keep one face for life
			Economy::OutfitNew(m);
			m.gearLevel = m.level;
			m.hunger = float(r.index(300));
			return m;
		}
		float MemberPower(const Member& m) const override { return Combat::MemberPower(m); }
		void OnMemberKilled(Group& g, Member& m, Group*, Member*) override { OnGroupMemberDied(g, m); }
		void LootBodies(Group& w, std::vector<Member>& dead) override { Economy::LootBodies(w, dead); }
		void GrowMember(Member& m, float xp, Rng& r) override { Economy::Grow(m, xp, r); }
		void ScaleMember(Member& m, int level) override
		{
			const int before = m.level, gear = m.gearLevel;
			Economy::ScaleToLevel(m, level);
			static int logged = 0;
			if (Settings::Get().bDebug && logged++ < 24)
				Log::Line("[level] " + std::string(FactionId(m.faction)) + " " + m.name + " " + std::to_string(before) + " -> " + std::to_string(m.level) + " (player " +
						  std::to_string(PlayerLevel()) + ", bias " + std::to_string(m.levelBias) + (m.gearLevel != gear ? ", re-equipped" : "") + ")");
		}
		void News(const std::string& line, float x, float y) override
		{
			Rep::PushNews(line, gBoard ? gBoard->now : Hours());   // the PDA feed keeps everything; the toast below only the nearby events
			if (!gCfg.news || !Settings::Get().news) return;
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!pc || Forms::WsIndex(pc->GetWorldspace()) < 0) return;
			if (gCfg.newsRadius > 0.f) {
				const auto p = pc->GetPosition();
				const float dx = p.x - x, dy = p.y - y;
				if (dx * dx + dy * dy > gCfg.newsRadius * gCfg.newsRadius) return;
			}
			std::lock_guard l(gNewsMx);
			if (gNews.size() < 6) gNews.push_back(line);
		}
		void Log(const std::string& line) override { Log::Line(line); }
		bool OnlineReached(const Group& g, const GraphNode& n) override { return Online::LeaderNear(g, n); }
		// --- needs and activities
		bool SurvivalOn() const override { return Activities::SurvivalOn(); }
		bool FishingAvailable() const override { return Activities::FishingAvailable(); }
		void Consume(Member& m) override { Economy::Consume(m); }
		bool HasFood(const Group& g) const override { return Activities::HasFood(g); }
		bool HasSellables(const Group& g) const override { return Merchants::HasSellables(g); }
		bool NeedsSupplies(const Group& g) const override { return Merchants::NeedsSupplies(g); }
		bool VendorsOpen(const GraphNode& n, double now) const override { return Merchants::AnyOpen(n, now); }
		float DefenderStrength(const GraphNode& n, Faction f) const override { return Activities::DefenderStrength(n, f); }
		void DoTrade(Group& g, GraphNode& n) override { Merchants::Trade(g, n, gBoard ? gBoard->now : Hours()); }
		void DoRaidSettlement(Group& g, GraphNode& n, bool ok, float ratio) override { Activities::DoRaidSettlement(g, n, ok, ratio); }
		void DoClearSection(Group& g, GraphNode& n, int section, int kills) override { Activities::DoClearSection(g, n, section, kills); }
		void DoHaul(Group& g, GraphNode& n) override { Activities::DoHaul(g, n); }
		void DoHunt(Group& g, double h, Rng& r) override { Activities::DoHunt(g, h, r); }
		void DoFish(Group& g, double h, Rng& r) override { Activities::DoFish(g, h, r); }
	};
	PluginHost gHost;

	// ---------------------------------------------------------------- gate (Alternate Start / vanilla intro)
	bool MenuBlocksSpawns()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) return true;
		return ui->IsMenuOpen(RE::SleepWaitMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
		       ui->IsMenuOpen(RE::FaderMenu::MENU_NAME) || ui->IsMenuOpen("RaceSex Menu") || ui->GameIsPaused();
	}

	bool LocUnder(const RE::BGSLocation* l, RE::FormID a_id) { return InHold(l, a_id); }

	// The vanilla Helgen intro is physically in progress: on the cart or anywhere in Helgen / Helgen Keep.
	bool InHelgenIntro(RE::PlayerCharacter* pc)
	{
		if (!pc) return false;
		if (LocUnder(pc->GetCurrentLocation(), Forms::Vanilla::HelgenLocation)) return true;
		if (auto* cm = RE::ControlMap::GetSingleton(); cm && !cm->IsMovementControlsEnabled()) return true;
		return false;
	}

	void GateLog(const std::string& a_msg)
	{
		static std::string last;
		static double lastLog = -1;
		if (!Settings::Get().bDebug) return;
		if (a_msg == last && Hours() - lastLog < 1.0) return;
		last = a_msg;
		lastLog = Hours();
		Log::Line(a_msg);
	}

	// Nothing starts while Live Another Life's chargen runs or the race menu is open; with a vanilla Helgen start, also wait
	// until Unbound (MQ101) finishes. With LAL present, MQ101 only holds while the Helgen intro is physically in progress.
	void UpdateGate(float a_realDt)
	{
		if (gP.gateOpened) return;
		auto* ui = RE::UI::GetSingleton();
		if (ui && ui->IsMenuOpen("RaceSex Menu")) { gGateHold = 0.f; return; }
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc || !pc->GetParentCell() || !pc->Is3DLoaded()) { gGateHold = 0.f; return; }
		auto* mq101 = Forms::Get<RE::TESQuest>(Forms::Vanilla::MQ101);
		auto* lal = Forms::R().lalChargen;
		if (lal && lal->IsRunning() && !lal->IsCompleted()) {
			GateLog("[gate] waiting LAL chargen (running stage=" + std::to_string(lal->GetCurrentStageID()) + ")");
			gGateHold = 0.f;
			return;
		}
		if (lal && pc->GetParentCell()->GetFormID() == Forms::Vanilla::LALPrisonCell) {
			GateLog("[gate] waiting - player still in the LAL start cell");
			gGateHold = 0.f;
			return;
		}
		const bool forced = Settings::Get().bDebug && Settings::Get().bForceGate;   // lab saves: no Helgen / LAL start to wait out
		const bool mqRunning = !forced && mq101 && !mq101->IsCompleted() && mq101->IsRunning();
		if (mqRunning) {
			// stage 0 with the player free and outside Helgen = the intro never ran (coc from the main menu, skip-intro
			// mods): MQ101 would stay at 0 forever, so treat it like the LAL case below
			if (!lal && (mq101->GetCurrentStageID() > 0 || InHelgenIntro(pc))) {
				GateLog("[gate] waiting MQ101 Unbound (stage=" + std::to_string(mq101->GetCurrentStageID()) + ", vanilla start)");
				gGateHold = 0.f;
				return;
			}
			if (InHelgenIntro(pc)) {
				GateLog("[gate] waiting MQ101 Unbound (stage=" + std::to_string(mq101->GetCurrentStageID()) + ", LAL Helgen intro in progress)");
				gGateHold = 0.f;
				return;
			}
		}
		if (ui && ui->GameIsPaused()) return;   // paused time does not count, timer kept
		gGateHold += a_realDt;
		if (gGateHold < kGateDelay) {
			GateLog("[gate] conditions met - opening after " + std::to_string(int(kGateDelay)) + "s if they hold");
			return;
		}
		gP.gateOpened = true;
		auto* ws = pc->GetWorldspace();
		Log::Line("[gate] opened at day " + std::to_string(Day()) + " ws=" + (ws && ws->GetFormEditorID() ? std::string(ws->GetFormEditorID()) : std::string(ws ? "?" : "interior")));
	}

	void Populate()
	{
		if (!gBoard) return;
		gBoard->Reset(Hours());
		gBoard->rng = Rng(Mix64(std::uint64_t(Hours() * 100) ^ 0xA11FE));
		gBoard->worldSeed = Mix64(std::uint64_t(Hours() * 100) ^ 0x5EED);
		gBoard->StartFill();
		gFilled = true;
		Log::Line("[sim] world populated: " + std::to_string(gBoard->squads.size()) + " squads on " + std::to_string(Graph::Nodes().size()) + " smart terrains");
	}

	void ResetState()
	{
		gCfg.ResetLiveRelations();   // dynamic relations start from factions.ltx (a load restores its own)
		if (gBoard) gBoard->Reset(Hours());
		gP = Persist{};
		gFilled = false;
		gLagging = false;
		Pool::RebuildReservations(Groups());
		Online::Reset();
		Rep::Reset();
		Behavior::Reset();
		Scavenge::Reset();
		Talk::Reset();
		Jobs::Reset();
	}

	void DrainNews(float a_dt)
	{
		gNewsCooldown = std::max(0.f, gNewsCooldown - a_dt);
		if (gNewsCooldown > 0.f) return;
		std::string line;
		{
			std::lock_guard l(gNewsMx);
			if (gNews.empty()) return;
			line = gNews.front();
			gNews.pop_front();
		}
		RE::SendHUDMessage::ShowHUDMessage(line.c_str());
		Log::Line("[news] " + line);
		gNewsCooldown = 12.f;
	}
}

std::recursive_mutex& Lock() { return gMx; }
Board& B() { return *gBoard; }
Config& Cfg() { return gCfg; }
bool BoardReady() { return gBoard != nullptr && gFilled; }
std::vector<Group>& Groups()
{
	static std::vector<Group> empty;
	return gBoard ? gBoard->squads : empty;
}
Persist& P() { return gP; }
bool GateOpen() { return gP.gateOpened; }
bool CatchingUp() { return gLagging; }

namespace { double gDebugOffset = 0.0; }   // debug fast-forward: whole days added to the sim clock (the engine rewrites its own calendar)

double Hours()
{
	auto* cal = RE::Calendar::GetSingleton();
	return (cal ? double(cal->GetHoursPassed()) : 0.0) + gDebugOffset;
}
std::uint32_t Day() { return std::uint32_t(Hours() / 24.0); }

Group* FindGroup(std::uint64_t id) { return gBoard ? gBoard->Find(id) : nullptr; }
Group* GroupOfActor(RE::Actor* a, Member** out)
{
	if (!a || !gBoard) return nullptr;
	for (auto& g : gBoard->squads)
		for (auto& m : g.members)
			if (m.poolLocal >= 0 && Pool::ActorOf(m) == a) { if (out) *out = &m; return &g; }
	return nullptr;
}
int GroupsOnline() { int n = 0; for (auto& g : Groups()) n += g.online; return n; }
int ActorsLoaded() { int n = Creatures::Live(); for (auto& s : Pool::Slots()) n += s.inUse; return n; }

float PlayerRel(Faction f)
{
	return std::clamp(Rep::Attitude(f), -2000.f, 2000.f);   // standing: lore baseline + rules + earned goodwill
}
void AddPlayerRel(Faction f, float d)
{
	auto& v = Rep::S().state.goodwill[int(f)];
	v = std::clamp(v + d, -gCfg.standing.goodwillMax, gCfg.standing.goodwillMax);
}

void ApplySettings()
{
	const auto& s = Settings::Get();
	gCfg.populationFactor = s.populationScale;
	gCfg.ocsMode = s.ocsMode <= 0 ? OcsMode::Off : s.ocsMode >= 2 ? OcsMode::Full : OcsMode::Smarts;
	gCfg.news = s.news;
	gCfg.dynamicRelations = s.dynamicRelations;   // off: factions.ltx rules; the live matrix is kept, so turning it back on resumes
}

// ---------------------------------------------------------------- sticky faces / member death bookkeeping
void SyncWaitlist()
{
	gP.waitlist.clear();
	for (auto& g : Groups())
		for (auto& m : g.members)
			if (m.alive() && m.sticky && m.waitlisted) gP.waitlist.push_back(m.id);
}

bool ReleaseStickyFor(const Member& a_newcomer)
{
	// release the tie of the member whose group's standing with the player is closest to neutral and who has gone longest
	// without meeting the player; veterans are never released while alive
	Member* best = nullptr;
	Group* bestG = nullptr;
	for (auto& g : Groups()) {
		if (g.online) continue;   // never swap a face that is on screen
		for (auto& m : g.members) {
			if (!m.alive() || !m.sticky || m.veteran || m.waitlisted || m.id == a_newcomer.id) continue;
			if (m.poolLocal < 0 || m.poolLocal >= int(Pool::Slots().size())) continue;
			const auto& slot = Pool::Slots()[m.poolLocal];
			if (!slot.reserved || slot.owner != m.id || slot.inUse) continue;
			if (!best) { best = &m; bestG = &g; continue; }
			const int a = std::abs(g.standing), b = std::abs(bestG->standing);
			if (a < b || (a == b && g.lastMetHours < bestG->lastMetHours)) { best = &m; bestG = &g; }
		}
	}
	if (!best) return false;
	Pool::ReleaseSticky(*best);
	best->waitlisted = true;   // keeps its record; regular until a slot frees up
	SyncWaitlist();
	Log::Line("[sticky] released tie of " + best->name + " (standing " + std::to_string(bestG->standing) + ") for " + a_newcomer.name);
	return true;
}

void OnGroupMemberDied(Group&, Member& m)
{
	if (!m.sticky) {
		Pool::Release(m);
		return;
	}
	const bool heldSlot = !m.waitlisted;
	Pool::ReleaseSticky(m);
	m.waitlisted = false;
	if (heldSlot) {
		// the freed slot goes to the waiting member with the strongest standing with the player
		Member* best = nullptr;
		Group* bestG = nullptr;
		for (auto& og : Groups())
			for (auto& om : og.members) {
				if (!om.alive() || !om.sticky || !om.waitlisted) continue;
				if (!best || og.standing > bestG->standing || (og.standing == bestG->standing && og.lastMetHours > bestG->lastMetHours)) {
					best = &om;
					bestG = &og;
				}
			}
		if (best) {
			best->waitlisted = false;
			if (best->poolLocal >= 0 && !bestG->online) best->poolLocal = -1;
			if (best->poolLocal >= 0) {
				auto& slot = Pool::Slots()[best->poolLocal];
				if (!slot.reserved) { slot.reserved = true; slot.owner = best->id; }
			}
			Log::Line("[sticky] slot handed to waitlisted " + best->name + " (standing " + std::to_string(bestG->standing) + ")");
		}
	}
	SyncWaitlist();
}

// A squad member died in the world: record it and remember who did it (the player's reputation with the faction drops).
void OnActorKilled(RE::Actor* victim, RE::Actor* killer)
{
	std::lock_guard l(gMx);
	Member* vm = nullptr;
	Group* vg = GroupOfActor(victim, &vm);
	const bool beast = !vg;
	if (beast) vg = Creatures::GroupOf(victim, &vm);   // a monster squad's creature: no standing involved
	if (!vg || !vm) return;
	vm->health = 0;
	std::string by = "something";
	if (killer && killer->IsPlayerRef()) {
		if (!beast) {
			Rep::OnPlayerKilled(*vg, *vm);
			Rep::OnPlayerHelped(*vg, *vm);
		}
		by = "the player";
	} else if (killer) {
		Member* km = nullptr;
		Group* kg = GroupOfActor(killer, &km);
		if (!kg) kg = Creatures::GroupOf(killer, &km);
		if (kg && km) {
			++km->kills;
			if (gBoard) {
				gBoard->stats.membersKilled[int(kg->faction)]++;
				gBoard->stats.membersLost[int(vg->faction)]++;
				gBoard->stats.killMatrix[int(kg->faction)][int(vg->faction)]++;
				gBoard->OnKill(kg->faction, vg->faction, vg->tier);   // dynamic relations (off by default)
			}
			by = std::string(FactionId(kg->faction)) + " " + km->name;
		}
	}
	Log::Line("[death] " + std::string(FactionId(vg->faction)) + " " + vm->name + " of squad " + std::to_string(vg->id) + " killed by " + by);
}

void ApplyQueuedKills(RE::TESObjectCELL* a_cell)
{
	if (!a_cell) return;
	std::lock_guard l(gMx);
	auto it = gP.queuedKills.find(a_cell->GetFormID());
	if (it == gP.queuedKills.end() || it->second <= 0) return;
	int left = it->second;
	auto* pc = RE::PlayerCharacter::GetSingleton();
	a_cell->ForEachReference([&](RE::TESObjectREFR* ref) {
		if (left <= 0) return RE::BSContainer::ForEachResult::kStop;
		auto* a = ref ? ref->As<RE::Actor>() : nullptr;
		if (!a || a->IsDead() || a->IsPlayerRef() || Pool::IsPoolActor(a)) return RE::BSContainer::ForEachResult::kContinue;
		if (Protect::IsProtectedActor(a) || Protect::IsBossRef(a)) return RE::BSContainer::ForEachResult::kContinue;
		if (!pc || !a->IsHostileToActor(pc)) return RE::BSContainer::ForEachResult::kContinue;
		a->KillImmediate();
		--left;
		return RE::BSContainer::ForEachResult::kContinue;
	});
	Log::Line("[conquest] applied " + std::to_string(it->second - left) + " queued kills in cell " + std::to_string(a_cell->GetFormID()));
	gP.queuedKills.erase(it);
}

// ---------------------------------------------------------------- lifecycle
void OnDataLoaded()
{
	std::lock_guard l(gMx);
	Forms::ResolveAll();
	Patch::Reload();
	Protect::Init();
	std::string err;
	if (!LoadConfig(Settings::ConfigDir(), gCfg, err)) {
		Log::Line("[config] FAILED to load " + Settings::ConfigDir() + ": " + err);
		return;
	}
	if (!err.empty()) Log::Line("[config] warnings: " + err);
	// creatures from another plugin (monsters.ltx "plugin|local id"): dropped when that plugin is not loaded. The local id stays
	// the creature's key (members save it) and Creatures::Spawn looks the record up in its plugin, so a load-order change keeps them.
	if (auto* dh = RE::TESDataHandler::GetSingleton()) {
		for (auto& d : gCfg.creatures)
			if (!d.plugin.empty() && d.form && (!dh->LookupModByName(d.plugin) || !dh->LookupForm(d.form, d.plugin))) {
				Log::Line("[config] creature " + d.id + ": " + d.plugin + " is not loaded (or has no such record): no " + d.name + " squads");
				d.form = 0;
			}
		gCfg.IndexCreatures();
	}
	ApplySettings();
	Graph::Build(gCfg);
	Rep::Init();
	Talk::Init();
	Jobs::Init();
	if (!Graph::Ready()) { Log::Line("[sim] no smart terrains: A-Life stays idle"); return; }
	Merchants::Init();
	gBoard = std::make_unique<Board>(gCfg, Graph::W(), gHost);
	gBoard->Reset(Hours());
	Pool::Init();
	Log::Line("[sim] ready: " + std::to_string(gCfg.squads.size()) + " squad types, " + std::to_string(Graph::Nodes().size()) + " smart terrains");
}

void OnNewGame()
{
	std::lock_guard l(gMx);
	ResetState();
	Creatures::ClearSweep();
	Patch::Reload();
	Pool::ResetScan();
	Pool::Resolve("newGame");
	gSweepPending = true;
	gLoading = false;
	gReady = gBoard != nullptr;
}

void OnPreLoad()
{
	std::lock_guard l(gMx);
	gReady = false;
	gLoading = true;
	ResetState();   // the co-save load fills it again
}

void OnPostLoad()
{
	std::lock_guard l(gMx);
	gLagging = false;
	Patch::Reload();
	if (gBoard) {
		for (auto& g : gBoard->squads) { g.online = false; g.slot = -1; }
		gFilled = gP.gateOpened && !gBoard->squads.empty();
		gBoard->now = std::min(gBoard->now, Hours());   // a save never lies in the future of the loaded clock
		// a save from before the mutants (or before M4b's kinds): stock the lairs of the monster kinds it has none of now
		std::array<bool, kFactionCount> missing{};
		std::string names;
		for (int f = kHumanFactionCount; f < kFactionCount; ++f)
			if (gCfg.IsMonster(Faction(f)) && gBoard->SquadCount(Faction(f)) == 0) { missing[f] = true; names += std::string(" ") + FactionId(Faction(f)); }
		if (gFilled && !names.empty() && !gCfg.creatures.empty()) {
			const auto before = gBoard->squads.size();
			gBoard->StartFill(&missing);
			Log::Line("[sim] no squads of" + names + " in this save: lairs stocked with " + std::to_string(gBoard->squads.size() - before) + " squads");
		}
	}
	Online::Reset();
	gGateHold = 0.f;
	Pool::ResetScan();
	Pool::Resolve("postLoad");
	Pool::RebuildReservations(Groups());
	for (auto& g : Groups())
		for (auto& m : g.members)
			if (m.waitlisted && std::find(gP.waitlist.begin(), gP.waitlist.end(), m.id) == gP.waitlist.end()) gP.waitlist.push_back(m.id);
	gSweepPending = true;
	gLoading = false;
	gReady = gBoard != nullptr;
	if (Settings::Get().prepareUninstall) gUninstallPending = true;
}

void SetEnabled(bool on)
{
	std::lock_guard l(gMx);
	if (Settings::Get().enabled == on) return;
	Settings::Get().enabled = on;
	if (!on) Online::SendAllOffline(true);
	if (on && gBoard) gBoard->now = Hours();   // re-enabling never catches up the paused time
}

void RequestPrepareUninstall() { gUninstallPending = true; }

static void DoPrepareUninstall()
{
	Online::SendAllOffline(false);
	for (auto& s : Pool::Slots())
		if (auto* a = s.refId ? RE::TESForm::LookupByID<RE::Actor>(s.refId) : nullptr) Pool::FullWipe(a);
	Pool::SweepAllToHolding();
	if (gBoard) gBoard->Reset(Hours());
	gP = Persist{};
	gFilled = false;
	Pool::RebuildReservations(Groups());
	Settings::Get().enabled = false;
	Settings::Get().prepareUninstall = false;
	Settings::Save();
	Log::Line("[uninstall] all A-Life actors removed; save now, then remove the mod");
	RE::SendHUDMessage::ShowHUDMessage("A-Life: ready to uninstall. Save your game, then remove the mod.");
}

namespace {
	// Debug builds: a tiny file-driven bridge for hands-off UI checks (the MCM pages, menus). Lines of <SKSE log dir>/ALife.cmd are run once and the file is
	// deleted: "key <pause|journal|tween|accept|cancel|up|down|left|right|activate|forward>" pushes that key press into the game's own input queue (no OS input),
	// "console <command>" runs a console command. The game is driven from inside the process: nothing reaches the keyboard the user is typing on.
	void RunConsole(const std::string& a_commands);
	void StartMonsterTrip(const std::string& a_arg);
	void BridgePoll(float a_dt)
	{
		static float t = 0.f;
		t += a_dt;
		if (t < 0.4f || !Settings::Get().bDebug) return;
		t = 0.f;
		std::optional<std::filesystem::path> dir = SKSE::log::log_directory();
		if (!dir) return;
		const auto path = *dir / "ALife.cmd";
		std::error_code ec;
		if (!std::filesystem::exists(path, ec)) return;
		std::ifstream in(path);
		std::vector<std::string> lines;
		for (std::string l; std::getline(in, l);) if (!l.empty()) lines.push_back(l);
		in.close();
		std::filesystem::remove(path, ec);
		auto* q = RE::BSInputEventQueue::GetSingleton();
		auto* ue = RE::UserEvents::GetSingleton();
		for (auto& l : lines) {
			const auto sp = l.find(' ');
			const std::string verb = l.substr(0, sp), arg = sp == std::string::npos ? std::string() : l.substr(sp + 1);
			if (verb == "console") { RunConsole(arg); continue; }
			if (verb == "status") { SKSE::GetTaskInterface()->AddTask([] { DebugStatus(); }); continue; }   // the Numpad1 dump (takes the sim lock)
			if (verb == "hunters") { SKSE::GetTaskInterface()->AddTask([] { DebugHunters(); }); continue; }   // M5g: lair hunters, targets, parts
			if (verb == "ff") { SKSE::GetTaskInterface()->AddTask([] { DebugFastForward(); }); continue; }    // the Numpad2 day fast-forward
			if (verb == "monster") { StartMonsterTrip(arg); continue; }   // "monster <draugr|falmer|wildlife> [wild]": see MonsterTripTick
			if (verb == "task") { SKSE::GetTaskInterface()->AddTask([arg] { Jobs::Debug(arg); }); continue; }   // docs/TASKS.md: list, offer <type>, accept, kill, where
			if (verb == "dump") {   // what the MCM's Standing and News pages would show
				Rep::PumpMenuCache(99.f);
				Log::Line("[mcm] standing page: " + std::to_string(Rep::MenuCount()) + " rows, status: " + Rep::MenuStatus());
				for (int i = 0; i < Rep::MenuCount(); ++i) Log::Line("[mcm]   " + Rep::MenuName(i) + " = " + Rep::MenuValue(i));
				Log::Line("[mcm] news page: " + std::to_string(Rep::NewsCount()) + " lines");
				for (int i = 0; i < Rep::NewsCount() && i < 8; ++i) Log::Line("[mcm]   " + Rep::NewsLine(i));
				continue;
			}
			if (verb == "mouse" && q && ue) {   // "mouse x y": move the menu cursor there (screen pixels) and click
				float x = 0.f, y = 0.f;
				if (std::sscanf(arg.c_str(), "%f %f", &x, &y) == 2)
					if (auto* mc = RE::MenuCursor::GetSingleton()) {
						mc->GetRuntimeData().cursorPosX = x;
						mc->GetRuntimeData().cursorPosY = y;
						q->PushOntoInputQueue(RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, ue->click, 0, 1.f, 0.f));
						q->PushOntoInputQueue(RE::ButtonEvent::Create(RE::INPUT_DEVICE::kMouse, ue->click, 0, 0.f, 0.1f));
						Log::Line("[bridge] click at " + arg);
					}
				continue;
			}
			if (verb != "key" || !q || !ue) continue;
			struct K { const char* name; const RE::BSFixedString* ev; std::uint32_t code; };
			const K keys[] = { { "pause", &ue->pause, 0x01 }, { "journal", &ue->journal, 0x24 }, { "tween", &ue->tweenMenu, 0x0F }, { "accept", &ue->accept, 0x1C },
							   { "cancel", &ue->cancel, 0x01 }, { "up", &ue->up, 0xC8 }, { "down", &ue->down, 0xD0 }, { "left", &ue->left, 0xCB },
							   { "right", &ue->right, 0xCD }, { "activate", &ue->activate, 0x12 }, { "forward", &ue->forward, 0x11 },
							   { "nextpage", &ue->nextPage, 0xCD }, { "prevpage", &ue->prevPage, 0xCB }, { "nextsub", &ue->nextSubPage, 0xD1 }, { "prevsub", &ue->prevSubPage, 0xC9 },
							   { "strright", &ue->strafeRight, 0x20 }, { "strleft", &ue->strafeLeft, 0x1E } };
			bool done = false;
			for (auto& k : keys)
				if (arg == k.name) {
					q->PushOntoInputQueue(RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, *k.ev, k.code, 1.f, 0.f));
					q->PushOntoInputQueue(RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, *k.ev, k.code, 0.f, 0.1f));
					Log::Line("[bridge] key " + arg);
					done = true;
					break;
				}
			if (!done) Log::Line("[bridge] unknown key " + arg);
		}
	}

	// ';' starts a comment in the ini the commands come from, so "|" separates them there
	std::string NormalizeSeparators(std::string s)
	{
		std::replace(s.begin(), s.end(), '|', ';');
		return s;
	}

	// Debug: "monster <faction> [wild]" takes the player to a monster squad the way the lab check needs it (docs/MUTANTS.md): the hour is moved
	// into the faction's online window (forward only), the board catches up, a living squad of that faction staying in a lair (or, with
	// "wild", out on a territory) is picked, the player is taken to its place and then put 5000 units away facing away, so the ordinary
	// online path (out of sight, 4000+ units out) brings it on screen.
	struct MonsterTrip { Faction faction{ Faction::Draugr }; bool wild{ false }; int stage{ 0 }; float t{ 0 }; std::uint64_t gid{ 0 }; std::uint32_t node{ 0 }; };
	MonsterTrip gTrip;

	// Debug: sDebugInWorld items "alife <verb> <arg>" (verbs: monster, console, wait <seconds>, done) run one after another, each once the
	// previous one has finished, so a whole lab check is set from the lab's ini (no file on the user's drive, no OS input).
	std::deque<std::string> gLabQueue;
	float gLabWait{ 0.f };

	void StartMonsterTrip(const std::string& a_arg)
	{
		auto parts = SplitList(a_arg, ' ');
		bool ok = false;
		const Faction f = parts.empty() ? Faction::Draugr : FactionFromId(Trim(parts[0]), &ok);
		if (!ok || IsHumanFaction(f) || !gBoard) { Log::Line("[debug] monster: unknown monster faction '" + a_arg + "'"); return; }
		gTrip = MonsterTrip{};
		gTrip.faction = f;
		gTrip.wild = parts.size() > 1 && Trim(parts[1]) == "wild";
		gTrip.stage = 1;
		Group probe;
		probe.faction = f;
		const float h = HourOfDay(Hours());
		if (!gBoard->MonsterMayComeOnline(probe, h + 0.5f) || (gTrip.wild && !gBoard->MonsterTerritoryOpen(f, h + 0.5f))) {
			float want = h;
			for (int k = 1; k < 24; ++k) {
				const float c = std::floor(h) + float(k);
				const float hc = std::fmod(c, 24.f);
				if (gBoard->MonsterMayComeOnline(probe, hc + 0.5f) && (!gTrip.wild || gBoard->MonsterTerritoryOpen(f, hc + 0.5f)) && c < 24.f) { want = c; break; }
			}
			if (want != h) RunConsole("set gamehour to " + std::to_string(int(want)));
			else Log::Line("[debug] monster: the window opens after midnight; run this again once the day has turned");
		}
		Log::Line(std::string("[debug] monster: looking for a ") + FactionId(f) + (gTrip.wild ? " squad out on a territory" : " squad in a lair"));
	}

	void MonsterTripTick(float a_dt)
	{
		if (!gTrip.stage || !gBoard || !gFilled) return;
		gTrip.t += a_dt;
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) return;
		if (gTrip.stage == 1) {
			if (gLagging || gTrip.t < 3.f) return;   // let the board catch up with a moved clock first
			const Group* pick = nullptr;
			for (auto& g : gBoard->squads) {
				if (g.faction != gTrip.faction || g.online || g.livingCount() == 0 || g.action != Action::Stay || g.currentTarget < 0) continue;
				const GraphNode* n = Graph::Node(std::uint32_t(g.currentTarget));
				if (!n || n->ws != 0 || !Graph::MarkerOf(*n)) continue;
				if ((n->props.lair > 0.f) == gTrip.wild) continue;
				pick = &g;
				break;
			}
			if (!pick) {
				if (gTrip.t > 60.f) { Log::Line("[debug] monster: no such squad found"); gTrip.stage = 0; }
				return;
			}
			const GraphNode* n = Graph::Node(std::uint32_t(pick->currentTarget));
			gTrip.gid = pick->id;
			gTrip.node = n->id;
			pc->MoveTo(Graph::MarkerOf(*n));
			Log::Line(std::string("[debug] monster: ") + gBoard->DescribeSquad(*pick) + (n->props.lair > 0.f ? " (lair)" : " (territory)") +
					  "; player taken there at hour " + std::to_string(int(HourOfDay(Hours()))));
			gTrip.stage = 2;
			gTrip.t = 0.f;
			return;
		}
		if (gTrip.stage == 2) {
			if (gTrip.t < 6.f || !pc->Is3DLoaded()) return;
			const GraphNode* n = Graph::Node(gTrip.node);
			if (!n) { gTrip.stage = 0; return; }
			if (const Group* g = gBoard->Find(gTrip.gid); g && g->online) {   // already out of sight around the place: moving away would send it offline
				gTrip.stage = 3;
				gTrip.t = 0.f;
				return;
			}
			// 5000 units out, on the side away from the world's centre line (any direction works; this one is stable), facing away
			const float ang = std::atan2(n->x, n->y);
			RE::NiPoint3 to{ n->x + std::sin(ang) * 5000.f, n->y + std::cos(ang) * 5000.f, pc->GetPosition().z + 200.f };
			float ground = 0.f;
			if (auto* tes = RE::TES::GetSingleton(); tes && tes->GetLandHeight(to, ground)) to.z = ground + 12.f;
			pc->SetPosition(to, true);
			pc->SetAngle(RE::NiPoint3{ pc->GetAngleX(), 0.f, ang });
			Log::Line("[debug] monster: player 5000 units from " + n->name + ", facing away; waiting for the squad to come online");
			gTrip.stage = 3;
			gTrip.t = 0.f;
			return;
		}
		if (gTrip.stage == 3) {
			const Group* g = gBoard->Find(gTrip.gid);
			if (g && g->online) {
				Log::Line(std::string("[debug] monster: ") + FactionId(g->faction) + " squad " + std::to_string(g->id) + " is online: " + std::to_string(Creatures::Live()) + " creature(s) placed");
				gTrip.stage = 0;
			} else if (gTrip.t > 90.f) {
				Log::Line("[debug] monster: squad " + std::to_string(gTrip.gid) + (g ? " stayed offline" : " is gone") + " after 90 s");
				gTrip.stage = 0;
			}
		}
	}

	void LabQueueTick(float a_dt)
	{
		if (gTrip.stage) return;   // a monster trip is still running
		if (gLabWait > 0.f) { gLabWait -= a_dt; return; }
		if (gLabQueue.empty()) return;
		const std::string item = gLabQueue.front();
		gLabQueue.pop_front();
		const auto sp = item.find(' ');
		const std::string verb = item.substr(0, sp), arg = sp == std::string::npos ? std::string() : item.substr(sp + 1);
		Log::Line("[debug] lab: " + item);
		if (verb == "wait") gLabWait = float(std::atof(arg.c_str()));
		else if (verb == "monster") StartMonsterTrip(arg);
		else if (verb == "console") RunConsole(arg);
		else if (verb == "scenario") { gScenarioIdx = std::atoi(arg.c_str()); DebugScenario(); }   // place that kSections squad ahead of the player
		else if (verb == "talk") DebugTalk();
		else if (verb == "choose") Talk::DebugChoose(arg);                                            // pick a topic in the open conversation
		else if (verb == "task") Jobs::Debug(arg);                                                    // docs/TASKS.md
		else if (verb == "done") Log::Line("[debug] lab queue done");
		else Log::Line("[debug] lab: unknown item " + item);
	}

	void RunConsole(const std::string& a_commands)
	{
		auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!factory) return;
		for (auto& cmd : SplitList(NormalizeSeparators(a_commands), ';')) {
			if (cmd.rfind("alife ", 0) == 0) { gLabQueue.push_back(cmd.substr(6)); continue; }
			const bool quit = cmd == "qqq" || cmd == "quitgame";
			if (quit) {   // nothing of ours may be pathing while the engine tears the world down (T7: quit crash in a pathing tasklet)
				Online::SendAllOffline(false);
				Log::Line("[quit] bridge quit: squads sent offline first, online=" + std::to_string(GroupsOnline()));
			}
			if (auto* script = factory->Create()) {
				script->SetCommand(cmd);
				script->CompileAndRun(pc);
				delete script;
				Log::Line("[debug] console: " + cmd);
			}
			if (quit) {
				auto* main = RE::Main::GetSingleton();
				Log::Line(std::string("[quit] after qqq: Main::quitGame=") + (main && main->quitGame ? "1" : "0"));
			}
		}
	}

	// Debug builds start their own test world (no keyboard injection): the main menu has been up for a few seconds -> run
	// sDebugStartup; once the world has loaded, answer the Survival Mode prompt with No and run sDebugInWorld.
	void DebugStartup(float a_dt)
	{
		const auto& st = Settings::Get();
		static int stage = 0;
		static float t = 0.f;
		if (!st.bDebug || st.sDebugStartup.empty()) return;
		auto* ui = RE::UI::GetSingleton();
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (stage >= 2) {
			// debug test sessions run unattended: any message box that appears later (the Survival Mode prompt arrives after the
			// world has loaded) is answered with its second button ("No"), so nobody has to dismiss it by hand
			static float jan = 0.f;
			jan += a_dt;
			if (jan > 0.5f) {
				jan = 0.f;
				if (ui && ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME) && RE::MessageBoxMenu::GetCurrentMessageBoxData()) {
					RE::MessageBoxMenu::SelectOption(1);
					Log::Line("[debug] answered a message box with No");
				} else if (ui && ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME)) {
					static int logged = 0;
					if (logged++ < 3) Log::Line("[debug] a message box is up but the engine queue is empty: cannot answer it from here");
				}
			}
			return;
		}
		if (stage == 0) {
			if (ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME) && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) t += a_dt; else t = 0.f;
			if (t > 8.f) { t = 0.f; stage = 1; RunConsole(st.sDebugStartup); }
		} else {
			if (p && p->Is3DLoaded() && p->GetParentCell()) t += a_dt; else t = 0.f;
			// answer any message box that is up (the Survival Mode prompt on a new character) with its second button: "No"
			static float diagT = 0.f;
			diagT += a_dt;
			const bool boxUp = ui && ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME);
			const auto queued = RE::MessageBoxMenu::GetQueueSize();
			if (diagT > 2.f) { diagT = 0.f; Log::Line("[debug] startup: world t=" + std::to_string(int(t)) + "s, message box " + (boxUp ? "UP" : "none") + ", queue " + std::to_string(queued) + (RE::MessageBoxMenu::GetCurrentMessageBoxData() ? ", data present" : ", no data")); }
			if (t > 3.f && (boxUp || queued > 0) && RE::MessageBoxMenu::GetCurrentMessageBoxData()) {
				RE::MessageBoxMenu::SelectOption(1);
				Log::Line("[debug] answered a prompt with No");
				t = 3.f;
			}
			if (t > 8.f) { stage = 2; RunConsole(st.sDebugInWorld); }
		}
	}
}

namespace {
	// sDebugAuto="N": once the world is populated, place scenario N, walk up to the squad, and talk to it when it is waiting for
	// the player (or after 30 s regardless). Hands-free test of the whole conversation path.
	void DebugAuto(float a_dt)
	{
		const auto& st = Settings::Get();
		static int stage = 0;
		static float t = 0.f;
		if (!st.bDebug || st.sDebugAuto.empty() || stage == 7 || !gFilled) return;
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!p || !p->Is3DLoaded() || Forms::WsIndex(p->GetWorldspace()) < 0) { t = 0.f; return; }
		if (RE::UI::GetSingleton() && RE::UI::GetSingleton()->GameIsPaused()) return;
		t += a_dt;
		const auto colon = st.sDebugAuto.find(':');                       // "N[t][:choice]"
		const bool brought = st.sDebugAuto.substr(0, colon).find('t') != std::string::npos;
		const bool armed = st.sDebugAuto.substr(0, colon).find('a') != std::string::npos;   // the player carries a drawn iron sword
		const bool barter = st.sDebugAuto.substr(0, colon).find('b') != std::string::npos;   // open the barter menu on the squad's leader
		const bool loot = st.sDebugAuto.substr(0, colon).find('l') != std::string::npos;    // a member of the squad dies of natural causes: do the survivors strip it?   // "Nt": the leader is brought to the player instead of chasing
		if (stage == 0 && t > 8.f) {
			try { gScenarioIdx = std::stoi(st.sDebugAuto); } catch (...) { gScenarioIdx = 0; }
			DebugScenario();
			stage = 1; t = 0.f;
		} else if (stage == 1 && t > 4.f) {
			if (armed) { RunConsole("player.additem 00012EB7 1;player.equipitem 00012EB7"); p->DrawWeaponMagicHands(true); }
			DebugGotoSquad();
			stage = loot ? 10 : (barter ? 20 : 2); t = 0.f;
		} else if (stage == 20 && t > 3.f) {
			std::lock_guard l(gMx);
			Group* g = gScenarioGroup ? gBoard->Find(gScenarioGroup) : nullptr;
			RE::Actor* who = nullptr;
			for (auto& m : g ? g->members : std::vector<Member>{}) if (m.alive()) if (auto* a = Pool::ActorOf(m); a && !a->IsDead()) { who = a; break; }
			if (who) {
				auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
				if (auto* script = factory ? factory->Create() : nullptr) {
					script->SetCommand("ShowBarterMenu");
					script->CompileAndRun(who);
					delete script;
					Log::Line(std::string("[debug] auto: ShowBarterMenu run on ") + who->GetDisplayFullName());
				}
			}
			stage = 21; t = 0.f;
		} else if (stage == 21 && t > 3.f) {
			auto* ui = RE::UI::GetSingleton();
			Log::Line(std::string("[debug] auto: barter menu is ") + (ui && ui->IsMenuOpen(RE::BarterMenu::MENU_NAME) ? "OPEN" : "not open"));
			stage = 7;
		} else if (stage == 10 && t > 3.f) {
			std::lock_guard l(gMx);
			Group* g = gScenarioGroup ? gBoard->Find(gScenarioGroup) : nullptr;
			RE::Actor* victim = nullptr;
			for (auto& m : g ? g->members : std::vector<Member>{}) if (m.alive()) if (auto* a = Pool::ActorOf(m); a && !a->IsDead()) victim = a;   // the last living member
			if (victim) { victim->KillImpl(nullptr, 9999.f, true, false); Log::Line(std::string("[debug] auto: ") + victim->GetDisplayFullName() + " dies of natural causes"); }
			stage = 11; t = 0.f;
		} else if (stage == 11 && t > 45.f) {
			Log::Line("[debug] auto: loot watch over, " + std::to_string(Scavenge::Looted()) + " bodies looted");
			stage = 7;
		} else if (stage == 2 && brought && t > 2.f && t < 3.f) {
			std::lock_guard l(gMx);
			Group* g = gScenarioGroup ? gBoard->Find(gScenarioGroup) : nullptr;
			for (auto& m : g ? g->members : std::vector<Member>{}) {
				auto* a = m.alive() ? Pool::ActorOf(m) : nullptr;
				if (!a || a->IsDead() || !a->Is3DLoaded()) continue;
				const auto pp = p->GetPosition();
				const float yaw = p->GetAngleZ();
				RE::NiPoint3 to{ pp.x + std::sin(yaw) * 220.f, pp.y + std::cos(yaw) * 220.f, pp.z + 60.f };
				float ground = 0.f;
				if (auto* tes = RE::TES::GetSingleton(); tes && tes->GetLandHeight(to, ground)) to.z = ground + 8.f;
				a->SetPosition(to, true);
				Log::Line("[debug] auto: the leader was brought to the player");
				t = 3.f;
				break;
			}
		} else if (stage == 2) {
			// the squad commits, chases and forces the conversation itself; this only reports whether it opened
			static bool tried = false;
			if (!tried && t > 12.f && !Talk::InConversation()) { tried = true; Log::Line("[debug] auto: nobody came, so the player talks to the squad"); DebugTalk(); }
			if (Talk::InConversation()) { Log::Line("[debug] auto: the squad caught up and the conversation opened"); stage = 3; t = 0.f; }
			else if (t > 45.f) { Log::Line("[debug] auto: no conversation opened within 45 s"); stage = 3; t = 0.f; }
		} else if (stage == 3 && t > 3.f) {
			Talk::DebugChoose(colon == std::string::npos ? std::string() : st.sDebugAuto.substr(colon + 1));   // ":pay" -> the toll topic; none -> the first offered
			stage = 4; t = 0.f;
		} else if (stage == 4 && t > 15.f) {
			Log::Line(std::string("[debug] auto: 15 s after the choice the conversation is ") + (Talk::InConversation() ? "still open" : "closed"));
			stage = 7;
		}
	}
}

namespace {
	// Quitting (qqq / Quit to desktop): the world is being torn down. Our tick would still move slot markers and re-evaluate
	// packages, queueing engine pathing requests that a tasklet then runs against freed navmeshes (T5 quit crash,
	// SkyrimSE+11DFA9A in NavMeshSearchClosePoint). Checked at the start of the tick and again after the bridge, which can
	// run qqq itself in the middle of a tick.
	bool Quitting()
	{
		auto* main = RE::Main::GetSingleton();
		if (!main || !main->quitGame) return false;
		static bool said = false;
		if (!said) { said = true; Log::Line("[quit] the game is quitting: A-Life tick stopped"); }
		return true;
	}
}

void MainTick(float a_realDt)
{
	if (!gBoard || Quitting()) return;
	DebugStartup(a_realDt);
	BridgePoll(a_realDt);
	if (Quitting()) return;
	MonsterTripTick(a_realDt);
	LabQueueTick(a_realDt);
	if (gReady) DebugAuto(a_realDt);
	if (!gReady) {
		// A console start (coc from the main menu) and some alternate starts send neither kNewGame nor a load message:
		// once the player has stood in the world for a few seconds with no lifecycle message, begin as a new game.
		static float seen = 0.f;
		auto* p = RE::PlayerCharacter::GetSingleton();
		if (!gLoading && p && p->Is3DLoaded() && p->GetParentCell()) seen += a_realDt; else seen = 0.f;
		if (seen > 3.f) {
			seen = 0.f;
			Log::Line("[sim] no new-game or load message seen (console / alternate start): starting as a new game");
			OnNewGame();
		}
		return;
	}
	std::unique_lock l(gMx, std::try_to_lock);
	if (!l.owns_lock()) return;
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || !pc->Is3DLoaded()) return;
	if (gUninstallPending) { gUninstallPending = false; DoPrepareUninstall(); return; }
	{
		static float resolveTimer = 0.f;
		resolveTimer += a_realDt;
		if (Pool::ResolvedCount() < kPoolSize && resolveTimer > 5.f) {
			resolveTimer = 0.f;
			static int tries = 0;
			if (tries < 24) { ++tries; Pool::Resolve("tick"); }
		}
	}
	if (gSweepPending) {
		gSweepPending = false;
		Pool::SweepAllToHolding();
		Online::ParkMarkers();
	}
	static RE::FormID lastCell = 0;
	if (auto* c = pc->GetParentCell(); c && c->GetFormID() != lastCell && c->IsAttached()) {
		lastCell = c->GetFormID();
		if (c->IsInteriorCell()) ApplyQueuedKills(c);
	}
	if (!Settings::Get().enabled || !Forms::R().pluginLoaded) return;
	{   // pick up hand-edited [Debug] without a restart
		static float dbgTimer = 0.f;
		dbgTimer += a_realDt;
		if (dbgTimer > 1.f) {
			dbgTimer = 0.f;
			const bool was = Settings::Get().bDebug;
			Settings::ReloadDebug();
			if (Settings::Get().bDebug && !was) Log::Open();
			else if (!Settings::Get().bDebug && was) Log::Open();
		}
	}
	UpdateGate(a_realDt);
	if (gP.gateOpened && !gFilled) Populate();
	if (!gFilled) return;

	const double now = Hours();
	// a wait / sleep / fast travel leaves the board behind the clock: nobody is on screen until it caught up
	double lag = now - gBoard->now;
	if (lag > gCfg.maxCatchUpHours) { gBoard->now = now - gCfg.maxCatchUpHours; lag = gCfg.maxCatchUpHours; }
	if (lag >= 1.0 && !gLagging) { gLagging = true; Online::SendAllOffline(true); }
	if (lag < 0.25) gLagging = false;
	{
		const auto t0 = std::chrono::steady_clock::now();
		const double step = std::max(0.02, double(gCfg.stepHours));
		while (gBoard->now + step <= now + 1e-9) {
			gBoard->Step(step);
			Rep::Tick(gBoard->now, step);   // goodwill, notoriety and squad memory fade; rumours arrive
			if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > kStepBudgetMs) break;
		}
	}
	{
		static double lastHourLog = -1.0;
		if (Settings::Get().bDebug && (lastHourLog < 0.0 || now - lastHourLog >= 6.0)) {
			lastHourLog = now;
			Log::Line("[hour] day=" + std::to_string(Day()) + " squads=" + std::to_string(gBoard->squads.size()) + " online=" + std::to_string(GroupsOnline()) +
					  " actors=" + std::to_string(ActorsLoaded()) + " lag=" + std::to_string(lag) + "h battles=" + std::to_string(gBoard->stats.battles));
		}
	}
	if (!MenuBlocksSpawns()) { Online::Tick(a_realDt); DrainNews(a_realDt); }
	Protect::Tick();
	Rep::PumpMenuCache(a_realDt);
	Talk::Pump(a_realDt);
	Jobs::Tick(a_realDt);
}

// ---------------------------------------------------------------- debug (bDebug=1 hotkeys)
std::string StatusLine()
{
	std::unique_lock l(gMx, std::try_to_lock);
	if (!l.owns_lock()) return "Simulating...";
	if (!gP.gateOpened) return "Waiting for the opening quest";
	if (!gBoard) return "Not ready";
	return std::to_string(gBoard->squads.size()) + " squads, " + std::to_string(GroupsOnline()) + " nearby";
}

void DebugStatus()
{
	std::lock_guard l(gMx);
	if (!gBoard) { Log::Line("[status] board not ready"); return; }
	std::ostringstream o;
	auto& b = *gBoard;
	o << "[status] day " << Day() << " hour " << HourOfDay(Hours()) << " gate=" << (gP.gateOpened ? "open" : "closed") << " squads=" << b.squads.size()
	  << " online=" << GroupsOnline() << " actors=" << ActorsLoaded() << " sticky=" << Pool::StickyUsed() << "/" << kStickyCap
	  << " lag=" << (Hours() - b.now) << "h smarts=" << Graph::Nodes().size() << " battles=" << b.stats.battles << " retreats=" << b.stats.retreats
	  << " controlChanges=" << b.stats.controlChanges << " pool=" << Pool::ResolvedCount() << "/" << kPoolSize << "\n";
	for (int f = 0; f < kFactionCount; ++f)
		o << "  " << FactionId(Faction(f)) << ": squads=" << b.SquadCount(Faction(f)) << " members=" << b.MemberCount(Faction(f))
		  << " spawned=" << b.stats.spawned[f] << " wiped=" << b.stats.wiped[f] << " playerRel=" << PlayerRel(Faction(f)) << "\n";
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (pc) {
		const auto pp = pc->GetPosition();
		std::vector<std::pair<float, const Group*>> nearby;
		for (auto& g : b.squads) {
			const float dx = g.x - pp.x, dy = g.y - pp.y;
			nearby.emplace_back(std::sqrt(dx * dx + dy * dy), &g);
		}
		std::sort(nearby.begin(), nearby.end(), [](auto& a, auto& c) { return a.first < c.first; });
		o << "  player at " << int(pp.x) << "," << int(pp.y) << " ws=" << Forms::WsIndex(pc->GetWorldspace()) << ", nearest squads:\n";
		for (std::size_t i = 0; i < nearby.size() && i < 8; ++i)
		{
			const auto& g = *nearby[i].second;
			const auto why = g.online ? std::string() : Online::OfflineWhy(g);   // why the online tick leaves it offline right now
			o << "    d=" << int(nearby[i].first) << " " << b.DescribeSquad(g) << (g.online ? " ONLINE" : " offline:" + (why.empty() ? std::string("eligible") : why)) << "\n";
			if (nearby[i].second->online) o << Online::DescribeActors(*nearby[i].second);
		}
	}
	Log::Line(o.str());
	RE::SendHUDMessage::ShowHUDMessage(("A-Life: " + StatusLine()).c_str());
}

// M5g: every squad of a lair-hunting faction, where it is going, whether a monster squad holds that lair, the parts it carries
void DebugHunters()
{
	std::lock_guard l(gMx);
	if (!gBoard) return;
	auto& b = *gBoard;
	std::uint64_t harvested = 0;
	for (auto v : b.stats.partsHarvested) harvested += v;
	std::ostringstream o;
	o << "[hunters] parts harvested so far " << harvested << " (hunter " << b.stats.partsHarvested[int(Faction::Hunter)] << ")\n";
	std::unordered_set<FormID> partForms;
	for (auto& cd : gCfg.creatures) for (auto& it : cd.parts) partForms.insert(it.form);
	int shown = 0;
	for (auto& g : b.squads) {
		if (!gCfg.huntsLairs[int(g.faction)] || g.livingCount() == 0) continue;
		int parts = 0;
		for (auto& m : g.members) for (auto& it : m.inventory) if (partForms.count(it.form)) parts += it.count;
		const GraphNode* t = g.assignedTarget >= 0 ? Graph::Node(std::uint32_t(g.assignedTarget)) : nullptr;
		const bool lair = t && t->props.lair > 0.f;
		o << "  " << b.DescribeSquad(g) << (lair ? " LAIR" : "") << (t && b.MonsterHeld(t->id) ? " (monsters there)" : "") << " parts=" << parts << "\n";
		if (++shown >= 20) break;
	}
	Log::Line(o.str());
}

void DebugFastForward()
{
	std::lock_guard l(gMx);
	if (!gBoard || !gFilled) return;
	Online::SendAllOffline(true);
	const auto t0 = std::chrono::steady_clock::now();
	const auto before = gBoard->stats;
	const double target = gBoard->now + 24.0;
	gBoard->Advance(target);
	gDebugOffset += 24.0;   // the sim clock moves with the board; the game's own calendar is left alone
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	Log::Line("[debug] fast-forward 24h in " + std::to_string(ms) + " ms: battles +" + std::to_string(gBoard->stats.battles - before.battles) +
			  " retreats +" + std::to_string(gBoard->stats.retreats - before.retreats) + " squads=" + std::to_string(gBoard->squads.size()));
	RE::SendHUDMessage::ShowHUDMessage(("A-Life: +1 day, " + std::to_string(gBoard->squads.size()) + " squads").c_str());
}

void DebugPlacePair()
{
	std::lock_guard l(gMx);
	if (!gBoard || !gFilled) return;
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || Forms::WsIndex(pc->GetWorldspace()) < 0) {
		Log::Line("[debug] place pair: the player must be in the Tamriel or Solstheim exterior");
		return;
	}
	const auto pp = pc->GetPosition();
	const float yaw = pc->GetAngleZ();
	int ok = 0;
	Online::SendAllOffline(true);   // free the slots: the pair is the test
	const std::pair<const char*, float> pair[] = { { "imperial_advanced", -1.f }, { "stormcloak_advanced", +1.f } };
	for (auto [section, side] : pair) {
		const auto node = Graph::Nearest(pp.x, pp.y, Forms::WsIndex(pc->GetWorldspace()));
		Group* g = gBoard->CreateSquad(node, section);
		if (!g) continue;
		g->x = pp.x - std::sin(yaw) * 5500.f + std::cos(yaw) * side * 800.f;
		g->y = pp.y - std::cos(yaw) * 5500.f - std::sin(yaw) * side * 800.f;
		g->z = pp.z;
		g->node = node;
		g->action = Action::Stay;
		g->assignedTarget = g->currentTarget = node;
		g->stayStart = gBoard->now;
		const auto gid = g->id;
		if (Online::BringOnlineEx(*g, true, true)) ++ok;
		else {
			Log::Line("[debug] place pair: " + std::string(section) + " could not come online");
			if (Group* f = gBoard->Find(gid)) gBoard->RemoveSquad(*f, "debug");
		}
	}
	Log::Line("[debug] placed Imperial vs Stormcloak squads near the player: online " + std::to_string(ok) + "/2");
	RE::SendHUDMessage::ShowHUDMessage(("A-Life: pair placed, " + std::to_string(ok) + "/2 online").c_str());
}

// Numpad5: one squad of the next faction in a fixed cycle, 2000 units ahead of the player, standing at the nearest place.
// Used to watch the standing system react (ignore / greet / watch / challenge / toll / attack) one faction at a time.
void DebugScenario()
{
	int& idx = gScenarioIdx;
	static const char* kSections[] = { "bandit_advanced", "thalmor_advanced", "forsworn_advanced", "wanderer_advanced", "imperial_advanced",
									   "stormcloak_advanced", "vampire_advanced", "dawnguard_advanced", "silverhand_advanced", "necromancer_advanced", "caravan_advanced",
									   "wildlife_advanced", "draugr_advanced", "falmer_advanced" };   // monsters last: earlier indices stay as before
	std::lock_guard l(gMx);
	if (!gBoard || !gFilled) { Log::Line("[debug] scenario: the world is not populated yet"); return; }
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || Forms::WsIndex(pc->GetWorldspace()) < 0) { Log::Line("[debug] scenario: the player must be in the Tamriel or Solstheim exterior"); return; }
	const int pws = Forms::WsIndex(pc->GetWorldspace());
	const char* section = kSections[idx++ % (sizeof(kSections) / sizeof(kSections[0]))];
	Online::SendAllOffline(true);
	const auto pp = pc->GetPosition();
	const float yaw = pc->GetAngleZ();
	const auto node = Graph::Nearest(pp.x, pp.y, pws);
	Group* g = gBoard->CreateSquad(node, section);
	if (!g) { Log::Line(std::string("[debug] scenario: cannot create ") + section); return; }
	g->x = pp.x + std::sin(yaw) * 2000.f;
	g->y = pp.y + std::cos(yaw) * 2000.f;
	g->z = pp.z;
	g->node = node;
	g->action = Action::Stay;
	g->assignedTarget = g->currentTarget = node;
	g->stayStart = gBoard->now;
	const auto gid = g->id;
	const bool ok = Online::BringOnlineEx(*g, true, true);
	if (!ok) {
		if (Group* f = gBoard->Find(gid)) gBoard->RemoveSquad(*f, "debug");
	} else {
		gScenarioGroup = gid;
	}
	Log::Line(std::string("[debug] scenario ") + section + (ok ? ": online" : ": could not come online"));
	RE::SendHUDMessage::ShowHUDMessage((std::string("A-Life scenario: ") + section + (ok ? "" : " (failed)")).c_str());
}

std::uint64_t DebugScenarioGroup() { return gScenarioGroup; }

// Numpad7: talk to the last scenario squad as if the player had pressed Activate on its leader (for testing the dialogue).
void DebugTalk()
{
	std::lock_guard l(gMx);
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || !gBoard) return;
	Group* g = gScenarioGroup ? gBoard->Find(gScenarioGroup) : nullptr;
	if (!g || !g->online) { Log::Line("[debug] talk: no scenario squad is online"); return; }
	for (auto& m : g->members) {
		auto* a = m.alive() ? Pool::ActorOf(m) : nullptr;
		if (!a || a->IsDead()) continue;
		int mode = 0, toll = 0;
		Behavior::TalkSituation(*g, mode, toll);
		Talk::Publish(mode, toll);
		Jobs::PublishFor(mode == Talk::kGreet ? g : nullptr);
		Talk::Hold(5.f);   // the engine reads the situation a frame after Activate
		Log::Line(std::string("[debug] talk: ") + FactionId(g->faction) + " " + m.name + " mode=" + std::to_string(mode) + " toll=" + std::to_string(toll));
		Log::Line(std::string("[debug] talk: activationBlocked=") + (a->IsActivationBlocked() ? "1" : "0") + " canTalk=" + (a->CanTalkToPlayer() ? "1" : "0") +
				  " inCombat=" + (a->IsInCombat() ? "1" : "0") + " dead=" + (a->IsDead() ? "1" : "0") + " 3d=" + (a->Is3DLoaded() ? "1" : "0"));
		Talk::Diagnose(a);
		// the player activates the member, as if pressing the activate key on it
		const bool r = a->ActivateRef(pc, 0, nullptr, 1, false);
		Log::Line(std::string("[debug] talk: the player activates it -> ") + (r ? "accepted" : "refused"));
		return;
	}
	Log::Line("[debug] talk: the scenario squad has no living member");
}

// Numpad6: the player lands the first blow and kills one living member of the last scenario squad, so the consequences
// (goodwill, notoriety, witnesses, bounty, the squad's memory) can be checked without a duel.
void DebugKill()
{
	std::lock_guard l(gMx);
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || !gBoard) return;
	Group* g = gScenarioGroup ? gBoard->Find(gScenarioGroup) : nullptr;
	if (!g || !g->online) { Log::Line("[debug] kill: no scenario squad is online"); return; }
	for (auto& m : g->members) {
		auto* a = m.alive() ? Pool::ActorOf(m) : nullptr;
		if (!a || a->IsDead()) continue;
		Behavior::OnPlayerAttacked(*g);   // the first blow
		Log::Line(std::string("[debug] kill: the player kills ") + FactionId(g->faction) + " " + m.name);
		a->KillImpl(pc, 9999.f, true, false);
		return;
	}
	Log::Line("[debug] kill: the scenario squad has no living member");
}

// Teleports the player next to the nearest on-screen squad so its behaviour can be watched.
void DebugGotoSquad()
{
	std::lock_guard l(gMx);
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc || !gBoard) return;
	const auto pp = pc->GetPosition();
	RE::Actor* best = nullptr;
	float bd = 1e18f;
	const Group* bg = nullptr;
	for (auto& g : gBoard->squads) {
		if (!g.online) continue;
		if (gScenarioGroup && g.id != gScenarioGroup) continue;   // the squad the last scenario placed, if it is still online
		for (auto& m : g.members) {
			auto* a = m.alive() ? Pool::ActorOf(m) : nullptr;
			if (!a || a->IsDead() || !a->Is3DLoaded()) continue;
			const auto p = a->GetPosition();
			const float d = (p.x - pp.x) * (p.x - pp.x) + (p.y - pp.y) * (p.y - pp.y);
			if (d < bd) { bd = d; best = a; bg = &g; }
			break;
		}
	}
	if (!best && gScenarioGroup) { gScenarioGroup = 0; DebugGotoSquad(); return; }   // the scenario squad left: any squad will do
	if (!best) { Log::Line("[debug] goto: no squad is on screen"); return; }
	const auto lp = best->GetPosition();
	// land ON the ground (a fixed height offset buries the player in rising terrain), a few hundred units out, facing the squad
	RE::NiPoint3 to{ lp.x + 700.f, lp.y + 700.f, lp.z + 60.f };
	float ground = 0.f;
	if (auto* tes = RE::TES::GetSingleton(); tes && tes->GetLandHeight(to, ground)) to.z = ground + 12.f;
	pc->SetPosition(to, true);
	pc->SetAngle(RE::NiPoint3{ pc->GetAngleX(), 0.f, std::atan2(lp.x - to.x, lp.y - to.y) });
	Log::Line("[debug] goto: next to " + std::string(FactionId(bg->faction)) + " squad " + std::to_string(bg->id) + " (" + bg->members.front().name + ")");
}
}
