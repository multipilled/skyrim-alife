#include "ALife/Rep.h"

#include <chrono>
#include <deque>

#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Settings.h"

// Standing, the plugin side (docs/STANDING.md). Everything the model needs to know about the player is read here:
// who they swore to, what they are, what they wear, what they owe the law, how strong they look.
namespace ALife::Rep {
namespace {
	ALife::Standing gS;
	PlayerContext gCtx;
	std::chrono::steady_clock::time_point gLast{};
	std::chrono::steady_clock::time_point gLastSay{};   // last tier-change notice

	struct Forms_
	{
		RE::TESFaction *cwImperial{}, *cwSons{}, *companions{}, *thieves{}, *brotherhood{}, *college{}, *dawnguard{}, *volkihar{},
			*blades{}, *greybeards{}, *vampirePC{}, *playerWerewolf{};
		RE::BGSKeyword *kwImperialLight{}, *kwImperialStudded{}, *kwImperialHeavy{}, *kwStormcloak{}, *kwDawnguard{}, *kwHunter{},
			*kwVampireArmor{}, *kwElvenGilded{}, *kwVampire{};
		RE::FormID beastRace[2]{ 0, 0 };
		RE::TESQuest *skaalFreed{}, *ravenRock[3]{};   // Dragonborn: DLC2SV01 (the Skaal), DLC2RR01/02/03 (Raven Rock)
	} gF;

	struct HoldLaw { RE::FormID hold; RE::FormID crime; RE::TESFaction* faction{}; };
	std::vector<HoldLaw> gLaw = {
		{ 0x16769, 0x2816C }, { 0x1676A, 0x267E3 }, { 0x1676B, 0x2816F }, { 0x1676C, 0x2816B }, { 0x1676D, 0x2816E },
		{ 0x1676E, 0x2816D }, { 0x1676F, 0x28170 }, { 0x16770, 0x29DB0 }, { 0x16772, 0x267EA },
	};
	std::unordered_map<RE::FormID, RaceId> gRaces;

	template <class T>
	T* FromPlugin(RE::FormID a_local, std::string_view a_plugin)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!dh || !dh->LookupModByName(a_plugin)) return nullptr;
		auto* f = dh->LookupForm(a_local & 0xFFFFFF, a_plugin);
		return f ? f->As<T>() : nullptr;
	}

	float PlayerPower(RE::PlayerCharacter* pc)
	{
		auto* av = pc->AsActorValueOwner();
		const float hp = std::max(40.f, av->GetActorValue(RE::ActorValue::kHealth));
		const float reduction = std::min(0.8f, av->GetActorValue(RE::ActorValue::kDamageResist) * 0.0012f);
		float dmg = 4.f;
		int skill = int(RE::ActorValue::kOneHanded);
		if (auto* eq = pc->GetEquippedObject(false)) {
			if (auto* w = eq->As<RE::TESObjectWEAP>()) {
				if (w->IsBow() || w->IsCrossbow()) skill = int(RE::ActorValue::kArchery);
				else if (w->GetWeaponType() == RE::WEAPON_TYPE::kTwoHandSword || w->GetWeaponType() == RE::WEAPON_TYPE::kTwoHandAxe)
					skill = int(RE::ActorValue::kTwoHanded);
				dmg = float(w->GetAttackDamage()) * (1.f + av->GetActorValue(RE::ActorValue(skill)) / 200.f);
				if (!w->IsBow() && !w->IsCrossbow()) dmg *= std::clamp(w->GetSpeed(), 0.5f, 1.3f);
			}
		} else if (av->GetActorValue(RE::ActorValue::kMagicka) > 60.f) {
			dmg = 12.f * (1.f + av->GetActorValue(RE::ActorValue::kDestruction) / 200.f);   // a caster with magicka to spend
		}
		const float ehp = hp / std::max(0.2f, 1.f - reduction);
		return std::sqrt(std::max(1.f, dmg * 0.7f) * ehp) * 10.f;
	}

	// Squads of the victim's side that saw it take it to heart: they remember the player as someone who strikes their own.
	void AlliesSaw(const Group& g, float a_delta)
	{
		const auto& cfg = Sim::Cfg();
		for (auto& og : Sim::Groups()) {
			if (&og == &g || !og.online || og.livingCount() == 0 || og.trust == Trust::Hostile) continue;
			if (og.faction != g.faction && cfg.Rel(g.faction, og.faction) < 400) continue;
			const float dx = og.x - g.x, dy = og.y - g.y;
			if (dx * dx + dy * dy > 3500.f * 3500.f) continue;
			og.memory = std::max(-1000.f, og.memory + a_delta);
		}
	}

	// How convincingly the player wears each faction's colours: body 0.55, head 0.2, hands 0.1, feet 0.1, shield 0.05.
	void FillDisguise(RE::PlayerCharacter* pc, PlayerContext& c)
	{
		using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
		struct Piece { Slot slot; float weight; };
		static const Piece kPieces[] = { { Slot::kBody, 0.55f }, { Slot::kHead, 0.2f }, { Slot::kHands, 0.1f }, { Slot::kFeet, 0.1f }, { Slot::kShield, 0.05f } };
		for (auto& p : kPieces) {
			auto* a = pc->GetWornArmor(p.slot);
			if (!a) continue;
			auto has = [&](RE::BGSKeyword* kw) { return kw && a->HasKeyword(kw); };
			if (has(gF.kwImperialLight) || has(gF.kwImperialStudded) || has(gF.kwImperialHeavy)) c.disguise[int(Faction::Imperial)] += p.weight;
			if (has(gF.kwStormcloak)) c.disguise[int(Faction::Stormcloak)] += p.weight;
			if (has(gF.kwDawnguard) || has(gF.kwHunter)) c.disguise[int(Faction::Dawnguard)] += p.weight;
			if (has(gF.kwVampireArmor)) c.disguise[int(Faction::Vampire)] += p.weight;
			if (has(gF.kwElvenGilded)) c.disguise[int(Faction::Thalmor)] += p.weight * 0.8f;
		}
		for (auto& d : c.disguise) d = std::min(1.f, d);
	}
}

// ----------------------------------------------------------------------------------------------- setup
ALife::Standing& S() { return gS; }
const PlayerContext& Context() { return gCtx; }

void Init()
{
	gS.SetConfig(&Sim::Cfg());
	auto get = [](RE::FormID id) { return Forms::Get<RE::TESFaction>(id); };
	gF.cwImperial = get(0x2BF9A);     gF.cwSons = get(0x2BF9B);       gF.companions = get(0x48362);
	gF.thieves = get(0x29DA9);        gF.brotherhood = get(0x1BDB3);  gF.college = get(0x1F259);
	gF.blades = get(0x72834);         gF.greybeards = get(0x2C6C8);   gF.vampirePC = get(0xC4DE0);
	gF.playerWerewolf = get(0x91822);
	gF.dawnguard = FromPlugin<RE::TESFaction>(0x14217, "Dawnguard.esm");      // DLC1DawnguardFaction
	gF.volkihar = FromPlugin<RE::TESFaction>(0x3376, "Dawnguard.esm");        // DLC1VampireFaction (the clan)
	auto kw = [](RE::FormID id) { return Forms::Get<RE::BGSKeyword>(id); };
	gF.kwImperialLight = kw(0x6BBE0);  gF.kwImperialStudded = kw(0x6BBE1);  gF.kwImperialHeavy = kw(0x6BBE2);
	gF.kwStormcloak = kw(0xAC13A);     gF.kwElvenGilded = kw(0x6BBDA);      gF.kwVampire = kw(0xA82BB);
	gF.kwDawnguard = FromPlugin<RE::BGSKeyword>(0x12CCD, "Dawnguard.esm");    // DLC1ArmorMaterialDawnguard
	gF.kwHunter = FromPlugin<RE::BGSKeyword>(0x050C4, "Dawnguard.esm");       // DLC1ArmorMaterialHunter
	gF.kwVampireArmor = FromPlugin<RE::BGSKeyword>(0x1463E, "Dawnguard.esm"); // DLC1ArmorMaterialVampire
	gF.beastRace[0] = 0xCDD84;                                                // WerewolfBeastRace
	if (auto* dh = RE::TESDataHandler::GetSingleton(); dh && dh->LookupModByName("Dawnguard.esm")) gF.beastRace[1] = dh->LookupFormID(0x283A, "Dawnguard.esm");
	for (auto& l : gLaw) l.faction = get(l.crime);
	// Solstheim is a hold of its own to the engine (DLC2SolstheimLocation carries LocTypeHold); Raven Rock's crime faction is its law
	if (auto* sol = FromPlugin<RE::BGSLocation>(0x16E2A, "Dragonborn.esm"); sol && std::none_of(gLaw.begin(), gLaw.end(), [&](auto& l) { return l.hold == sol->GetFormID(); }))
		gLaw.push_back({ sol->GetFormID(), 0x18279, FromPlugin<RE::TESFaction>(0x18279, "Dragonborn.esm") });   // DLC2CrimeRavenRockFaction
	gF.skaalFreed = FromPlugin<RE::TESQuest>(0x19B4A, "Dragonborn.esm");      // DLC2SV01
	gF.ravenRock[0] = FromPlugin<RE::TESQuest>(0x18B13, "Dragonborn.esm");    // DLC2RR01
	gF.ravenRock[1] = FromPlugin<RE::TESQuest>(0x18B14, "Dragonborn.esm");    // DLC2RR02
	gF.ravenRock[2] = FromPlugin<RE::TESQuest>(0x18B15, "Dragonborn.esm");    // DLC2RR03

	gRaces = { { 0x13746, RaceId::Nord },     { 0x13744, RaceId::Imperial },  { 0x13741, RaceId::Breton },   { 0x13748, RaceId::Redguard },
			   { 0x13743, RaceId::Altmer },   { 0x13749, RaceId::Bosmer },    { 0x13742, RaceId::Dunmer },   { 0x13747, RaceId::Orsimer },
			   { 0x13740, RaceId::Argonian }, { 0x13745, RaceId::Khajiit },   { 0x88794, RaceId::Nord },      { 0x88844, RaceId::Imperial },
			   { 0x8883C, RaceId::Breton },   { 0x88846, RaceId::Redguard },  { 0x88840, RaceId::Altmer },    { 0x88884, RaceId::Bosmer },
			   { 0x8883D, RaceId::Dunmer },   { 0xA82B9, RaceId::Orsimer },   { 0x8883A, RaceId::Argonian },  { 0x88845, RaceId::Khajiit } };
	int laws = 0, fac = 0;
	for (auto& l : gLaw) laws += l.faction ? 1 : 0;
	for (auto* f : { gF.cwImperial, gF.cwSons, gF.companions, gF.thieves, gF.brotherhood, gF.college, gF.blades, gF.greybeards, gF.vampirePC, gF.playerWerewolf, gF.dawnguard, gF.volkihar })
		fac += f ? 1 : 0;
	const int dlc2 = (gF.skaalFreed ? 1 : 0) + (gF.ravenRock[0] ? 1 : 0) + (gF.ravenRock[1] ? 1 : 0) + (gF.ravenRock[2] ? 1 : 0);
	Log::Line("[standing] forms: allegiance factions " + std::to_string(fac) + "/12, crime factions " + std::to_string(laws) + "/" + std::to_string(gLaw.size()) +
			  ", Solstheim quests " + std::to_string(dlc2) + "/4, rules " +
			  std::to_string(Sim::Cfg().standing.rules.size()));
}

void Reset2();
void ClearNews();
void Reset()
{
	gS.Reset();
	gCtx = PlayerContext{};
	gLast = {};
	Reset2();
	ClearNews();
}

// ----------------------------------------------------------------------------------------------- the player
void Refresh(bool a_force)
{
	const auto now = std::chrono::steady_clock::now();
	if (!a_force && std::chrono::duration<double>(now - gLast).count() < 0.5) return;
	gLast = now;
	auto* pc = RE::PlayerCharacter::GetSingleton();
	if (!pc) return;
	PlayerContext c;
	auto in = [&](RE::TESFaction* f) { return f && pc->IsInFaction(f); };
	if (in(gF.cwImperial)) c.flags |= PlayerFlag::Imperial;
	if (in(gF.cwSons)) c.flags |= PlayerFlag::Stormcloak;
	if (in(gF.companions)) c.flags |= PlayerFlag::Companions;
	if (in(gF.thieves)) c.flags |= PlayerFlag::ThievesGuild;
	if (in(gF.brotherhood)) c.flags |= PlayerFlag::DarkBrotherhood;
	if (in(gF.college)) c.flags |= PlayerFlag::College;
	if (in(gF.dawnguard)) c.flags |= PlayerFlag::Dawnguard;
	if (in(gF.volkihar)) c.flags |= PlayerFlag::Volkihar;
	if (in(gF.blades)) c.flags |= PlayerFlag::Blades;
	if (in(gF.greybeards)) c.flags |= PlayerFlag::Greybeards;
	if (in(gF.playerWerewolf)) c.flags |= PlayerFlag::Werewolf;
	if (gF.skaalFreed && gF.skaalFreed->IsCompleted()) c.flags |= PlayerFlag::SkaalFriend;
	for (auto* q : gF.ravenRock)
		if (q && q->IsCompleted()) c.flags |= PlayerFlag::RavenRock;

	if (auto* race = pc->GetRace()) {
		const auto id = race->GetFormID();
		if (auto it = gRaces.find(id); it != gRaces.end()) c.race = it->second;
		if (in(gF.vampirePC) || (gF.kwVampire && race->HasKeyword(gF.kwVampire))) c.flags |= PlayerFlag::Vampire;
		if (id == gF.beastRace[0] || (gF.beastRace[1] && id == gF.beastRace[1])) c.flags |= PlayerFlag::BeastForm;
	}
	if (auto* st = pc->AsActorState(); st && st->IsWeaponDrawn()) c.flags |= PlayerFlag::Armed;
	if (pc->IsSneaking()) c.flags |= PlayerFlag::Sneaking;
	if (pc->IsOnMount()) c.flags |= PlayerFlag::Mounted;
	if (pc->IsInCombat()) c.flags |= PlayerFlag::InCombat;
	if (!InTimeInterval(HourOfDay(Sim::Hours()), 6.f, 20.f)) c.flags |= PlayerFlag::Night;

	c.level = int(pc->GetLevel());
	c.power = PlayerPower(pc);
	{
		auto* av = pc->AsActorValueOwner();
		const float cur = av->GetActorValue(RE::ActorValue::kHealth);
		const float max = std::max(1.f, av->GetBaseActorValue(RE::ActorValue::kHealth));
		c.healthFrac = std::clamp(cur / max, 0.f, 1.f);
	}
	c.companions = int(pc->GetPlayerRuntimeData().teammateCount);
	c.wealth = std::clamp(float(pc->GetGoldAmount(true)) / 2000.f, 0.f, 1.f);
	for (auto& l : gLaw) {
		if (!l.faction) continue;
		const int gold = int(pc->GetViolentCrimeGoldValue(l.faction) + pc->GetNonViolentCrimeGoldValue(l.faction));
		if (gold > 0) c.bounty.emplace_back(l.hold, gold);
	}
	FillDisguise(pc, c);
	gCtx = std::move(c);
}

Site SiteOf(const Group& g)
{
	Site s;
	const GraphNode* n = nullptr;
	if (g.currentTarget >= 0) n = Graph::Node(std::uint32_t(g.currentTarget));
	else if (g.assignedTarget >= 0 && g.action == Action::Stay) n = Graph::Node(std::uint32_t(g.assignedTarget));
	if (!n) n = Graph::Node(g.node);
	if (!n) return s;
	s.hold = n->hold;
	// the hold's law is whoever holds it now in the Civil War (docs/CIVILWAR.md)
	if (const std::uint8_t owner = CivilWar::HoldOwner(n->hold); owner != kNoFaction)
		s.enforces = Sim::Cfg().standing.lawful[int(g.faction)] && owner == std::uint8_t(g.faction);
	else if (n->ws < kWorldspaces) s.enforces = Sim::Cfg().standing.lawful[int(g.faction)] && Sim::Cfg().wsFaction[n->ws] == std::uint8_t(g.faction);   // Solstheim: Raven Rock's guard
	return s;
}

float SquadPower(const Group& g)
{
	float s = 0.f;
	for (auto& m : g.members)
		if (m.alive()) s += Combat::MemberPower(m) * std::clamp(m.health / std::max(1.f, m.healthMax), 0.2f, 1.f);
	return s;
}

Regard RegardOf(const Group& g)
{
	Refresh();
	return gS.Evaluate(g.faction, gCtx, &g, SiteOf(g)).regard;
}

float Attitude(Faction f)
{
	Refresh();
	return gS.Evaluate(f, gCtx, nullptr, Site{}).value;
}

bool IsOutlaw(Faction f)
{
	const auto& sc = Sim::Cfg().standing;
	return sc.opportunism[int(f)] > 0.f && !sc.lawful[int(f)];
}

// ----------------------------------------------------------------------------------------------- deeds
void OnPlayerStruck(Group& g)
{
	if (g.playerStruckFirst || g.attackedByPlayer) return;
	g.playerStruckFirst = g.trust != Trust::Hostile;
	gS.OnAttack(g);
	g.standing -= 3;
	if (g.playerStruckFirst) AlliesSaw(g, -200.f);
	Log::Line(std::string("[standing] the player struck a ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) +
			  (g.playerStruckFirst ? " unprovoked" : " that was already hostile"));
}

void OnPlayerKilled(Group& g, Member& victim)
{
	Refresh(true);
	const bool provoked = (g.trust == Trust::Hostile || g.foughtPlayer) && !g.playerStruckFirst;
	// witnesses: another member of the squad, or someone of its side close by
	bool witnessed = g.livingCount() > 0;
	if (!witnessed) {
		for (auto& og : Sim::Groups()) {
			if (&og == &g || !og.online || og.livingCount() == 0) continue;
			const int rel = Sim::Cfg().Rel(g.faction, og.faction);
			if (og.faction != g.faction && rel < 400) continue;
			const float dx = og.x - g.x, dy = og.y - g.y;
			if (dx * dx + dy * dy < 3500.f * 3500.f) { witnessed = true; break; }
		}
	}
	const Site site = SiteOf(g);
	gS.OnKill(g, victim, witnessed, provoked, Sim::Hours());
	g.standing -= 1;
	if (!provoked) AlliesSaw(g, -250.f);

	// killing the law's own soldiers in a hold they guard is murder to the hold
	const auto& sc = Sim::Cfg().standing;
	int bounty = 0;
	if (!provoked && witnessed && site.enforces && sc.murderBounty > 0.f) {
		for (auto& l : gLaw) {
			if (l.hold != site.hold || !l.faction) continue;
			if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
				pc->ModCrimeGoldValue(l.faction, true, std::int32_t(sc.murderBounty));
				bounty = int(sc.murderBounty);
			}
		}
	}
	Log::Line(std::string("[standing] player killed ") + FactionId(g.faction) + " " + victim.name + (provoked ? " (provoked)" : " (unprovoked)") +
			  (witnessed ? " seen" : " unseen") + (bounty ? " bounty+" + std::to_string(bounty) : std::string()) + " -> goodwill " +
			  std::to_string(int(gS.state.goodwill[int(g.faction)])) + " notoriety " + std::to_string(int(gS.state.notoriety)));
}

void OnPlayerHelped(const Group& victimGroup, const Member& victim)
{
	// a squad that was watching (online, not hostile to the player, an enemy of the victims) is grateful
	const auto& cfg = Sim::Cfg();
	for (auto& og : Sim::Groups()) {
		if (!og.online || og.livingCount() == 0 || og.trust == Trust::Hostile) continue;
		if (cfg.Rel(og.faction, victimGroup.faction) > -1000) continue;
		const float dx = og.x - victimGroup.x, dy = og.y - victimGroup.y;
		if (dx * dx + dy * dy > 3500.f * 3500.f) continue;
		gS.OnHelp(og.faction, victim.level, Sim::Hours());
		const_cast<Group&>(og).memory = std::min(1000.f, og.memory + cfg.standing.helpMemory);
		const_cast<Group&>(og).helpedByPlayer = true;
		Log::Line(std::string("[standing] ") + FactionId(og.faction) + " squad #" + std::to_string(og.id) + " saw the player kill its enemy: grateful");
	}
}

void Tick(double now, double dt)
{
	gS.Tick(now, dt);
	for (auto& g : Sim::Groups()) gS.DecayMemory(g, dt);
}

// ----------------------------------------------------------------------------------------------- news feed
namespace {
	std::mutex gNewsMx;
	std::deque<std::string> gNewsLog;
}

void PushNews(const std::string& line, double now)
{
	char stamp[32];
	std::snprintf(stamp, sizeof(stamp), "Day %u %02d:%02d  ", unsigned(DayOf(now)) + 1, int(HourOfDay(now)), int(std::fmod(HourOfDay(now), 1.f) * 60.f));
	std::lock_guard l(gNewsMx);
	gNewsLog.push_front(std::string(stamp) + line);
	while (gNewsLog.size() > 40) gNewsLog.pop_back();
}
void ClearNews() { std::lock_guard l(gNewsMx); gNewsLog.clear(); }
int NewsCount() { std::lock_guard l(gNewsMx); return int(gNewsLog.size()); }
std::string NewsLine(int i) { std::lock_guard l(gNewsMx); return i >= 0 && i < int(gNewsLog.size()) ? gNewsLog[i] : std::string(); }
std::vector<std::string> NewsAll() { std::lock_guard l(gNewsMx); return { gNewsLog.begin(), gNewsLog.end() }; }
void RestoreNews(const std::vector<std::string>& a_lines)
{
	std::lock_guard l(gNewsMx);
	gNewsLog.assign(a_lines.begin(), a_lines.end());
	while (gNewsLog.size() > 40) gNewsLog.pop_back();
}

// ----------------------------------------------------------------------------------------------- MCM
namespace {
	std::mutex gMenuMx;
	std::vector<std::pair<std::string, std::string>> gMenu;
	std::string gMenuStatus;
	float gMenuTimer = 99.f;

	// tier changes are announced once, a few seconds apart (never on the first reading after a load)
	std::array<int, kFactionCount> gLastTier = [] { std::array<int, kFactionCount> a; a.fill(-1); return a; }();
	std::array<float, kFactionCount> gLastValue{};
	std::deque<std::string> gToasts;
}

void Reset2() { gLastTier.fill(-1); gToasts.clear(); }
void ClearNews();

void PumpMenuCache(float dt)
{
	gMenuTimer += dt;
	if (gMenuTimer < 2.f) return;
	gMenuTimer = 0.f;
	Refresh(true);
	std::vector<std::pair<std::string, std::string>> rows;
	for (int i = 0; i < kHumanFactionCount; ++i) {   // monsters have no standing (docs/MUTANTS.md)
		const auto at = gS.Evaluate(Faction(i), gCtx, nullptr, Site{});
		char buf[48];
		std::snprintf(buf, sizeof(buf), "%s (%+d)", RegardName(at.regard), int(std::lround(at.value)));
		rows.emplace_back(FactionName(Faction(i)), buf);
		const int tier = int(at.regard);
		if (gLastTier[i] >= 0 && gLastTier[i] != tier && std::fabs(at.value - gLastValue[i]) >= 40.f && Sim::Cfg().standing.notices && Settings::Get().news && Sim::Cfg().news) {
			std::string t = std::string("The ") + FactionName(Faction(i)) + " now regard you as " + RegardName(at.regard);
			if (gToasts.size() < 4) gToasts.push_back(t);
			Log::Line("[standing] " + t + " (" + std::to_string(int(at.value)) + ")");
		}
		if (gLastTier[i] != tier) gLastValue[i] = at.value;
		gLastTier[i] = tier;
	}
	if (!gToasts.empty() && std::chrono::duration<double>(std::chrono::steady_clock::now() - gLastSay).count() >= 3.0) {
		RE::SendHUDMessage::ShowHUDMessage(gToasts.front().c_str());
		gLastSay = std::chrono::steady_clock::now();
		gToasts.pop_front();
	}
	std::string st = "Notoriety " + std::to_string(int(gS.state.notoriety));
	int owed = 0;
	for (auto& b : gCtx.bounty) owed += b.second;
	st += owed ? ", bounty " + std::to_string(owed) : std::string(", no bounty");
	std::lock_guard l(gMenuMx);
	gMenu = std::move(rows);
	gMenuStatus = std::move(st);
}

int MenuCount() { std::lock_guard l(gMenuMx); return int(gMenu.size()); }
std::string MenuName(int i) { std::lock_guard l(gMenuMx); return i >= 0 && i < int(gMenu.size()) ? gMenu[i].first : std::string(); }
std::string MenuValue(int i) { std::lock_guard l(gMenuMx); return i >= 0 && i < int(gMenu.size()) ? gMenu[i].second : std::string(); }
std::string MenuStatus() { std::lock_guard l(gMenuMx); return gMenuStatus; }
}
