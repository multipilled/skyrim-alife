#include "ALife/Systems.h"
#include "ALife/Forms.h"
#include "ALife/Log.h"

namespace ALife::Pool {
namespace {
	std::vector<Slot> gSlots;

	RaceId RaceFromForm(RE::FormID a_id, bool& a_ok)
	{
		a_ok = true;
		switch (a_id) {
		case 0x13746: return RaceId::Nord;
		case 0x13744: return RaceId::Imperial;
		case 0x13741: return RaceId::Breton;
		case 0x13748: return RaceId::Redguard;
		case 0x13743: return RaceId::Altmer;
		case 0x13749: return RaceId::Bosmer;
		case 0x13742: return RaceId::Dunmer;
		case 0x13747: return RaceId::Orsimer;
		case 0x13740: return RaceId::Argonian;
		case 0x13745: return RaceId::Khajiit;
		case 0x88794: return RaceId::NordVampire;
		case 0x88844: return RaceId::ImperialVampire;
		case 0x8883C: return RaceId::BretonVampire;
		case 0x8883D: return RaceId::DunmerVampire;
		default: a_ok = false; return RaceId::Nord;
		}
	}

	Slot* Find(std::function<bool(const Slot&)> a_pred)
	{
		for (auto& s : gSlots)
			if (a_pred(s)) return &s;
		return nullptr;
	}

	void SetBasePerks(RE::TESNPC* a_npc, const std::vector<RE::BGSPerk*>& a_perks)
	{
		auto& arr = *static_cast<RE::BGSPerkRankArray*>(a_npc);
		if (arr.perks) {
			RE::free(arr.perks);
			arr.perks = nullptr;
			arr.perkCount = 0;
		}
		if (a_perks.empty()) return;
		auto* mem = RE::malloc<RE::PerkRankData>(sizeof(RE::PerkRankData) * a_perks.size());
		if (!mem) return;
		for (std::size_t i = 0; i < a_perks.size(); ++i) {
			std::memset(&mem[i], 0, sizeof(RE::PerkRankData));
			mem[i].perk = a_perks[i];
			mem[i].currentRank = 1;   // vanilla NPC_ PRKR rank
		}
		arr.perks = mem;
		arr.perkCount = std::uint32_t(a_perks.size());
	}
}

std::vector<Slot>& Slots() { return gSlots; }

namespace {
	bool gScanDone{ false };

	RaceId RaceFromEditorID(const char* a_edid, bool& a_ok)
	{
		static const std::pair<const char*, RaceId> kMap[] = {
			{ "NordRace", RaceId::Nord }, { "ImperialRace", RaceId::Imperial }, { "BretonRace", RaceId::Breton },
			{ "RedguardRace", RaceId::Redguard }, { "HighElfRace", RaceId::Altmer }, { "WoodElfRace", RaceId::Bosmer },
			{ "DarkElfRace", RaceId::Dunmer }, { "OrcRace", RaceId::Orsimer }, { "ArgonianRace", RaceId::Argonian },
			{ "KhajiitRace", RaceId::Khajiit }, { "NordRaceVampire", RaceId::NordVampire },
			{ "ImperialRaceVampire", RaceId::ImperialVampire }, { "BretonRaceVampire", RaceId::BretonVampire },
			{ "DarkElfRaceVampire", RaceId::DunmerVampire } };
		a_ok = false;
		if (!a_edid || !*a_edid) return RaceId::Nord;
		for (auto& [k, v] : kMap)
			if (_stricmp(a_edid, k) == 0) { a_ok = true; return v; }
		return RaceId::Nord;
	}

	struct ResolveStats
	{
		int noForm{ 0 }, notActor{ 0 }, noBase{ 0 }, badRace{ 0 }, scanned{ 0 };
		RE::FormID firstMiss{ 0 }, firstBadRace{ 0 };
		int firstType{ -1 };
	};

	// Fills one slot from a live pool actor. Race by Skyrim.esm FormID, then by race editor ID (TESRace keeps EDIDs).
	bool FillSlot(Slot& a_s, RE::Actor* a_actor, ResolveStats& a_st)
	{
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		if (!base) { ++a_st.noBase; return false; }
		bool ok = false;
		RaceId race = RaceFromForm(base->race ? base->race->GetFormID() : 0, ok);
		if (!ok && base->race) race = RaceFromEditorID(base->race->GetFormEditorID(), ok);
		if (!ok) {
			++a_st.badRace;
			if (!a_st.firstBadRace) a_st.firstBadRace = base->race ? base->race->GetFormID() : 0;
			return false;
		}
		a_s.refId = a_actor->GetFormID();
		a_s.baseId = base->GetFormID();
		a_s.race = race;
		a_s.sex = base->IsFemale() ? Sex::Female : Sex::Male;
		return true;
	}
}

static std::string Hex(RE::FormID a_id)
{
	char b[16];
	std::snprintf(b, sizeof(b), "%08X", a_id);
	return b;
}

int ResolvedCount()
{
	int n = 0;
	for (auto& s : gSlots) n += s.refId ? 1 : 0;
	return n;
}

void ResetScan() { gScanDone = false; }

// 176f: the 166 persistent pool ACHRs (ALife.esp is ESL-flagged, so 0xFE<idx>B00..BA5 at runtime) were looked
// up exactly once at kDataLoaded and never again. 176e logged "resolved 0/166" and every BringOnline then
// failed in Pool::Assign. Resolve() is now re-run at NewGame / PostLoad and retried from the main tick until all
// 166 are found, never clears reservations of already-resolved slots, and falls back to one scan of the form
// map for actors whose base is one of the 166 pool NPC_ records. The log line breaks failures down by cause.
int Resolve(const char* a_when)
{
	if (gSlots.size() != std::size_t(kPoolSize)) gSlots.assign(kPoolSize, Slot{});
	const auto& r = Forms::R();
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!r.pluginLoaded || !dh) {
		Log::Line(std::string("[pool] resolve(") + a_when + ") skipped - ALife.esp not loaded");
		return 0;
	}
	ResolveStats st;
	int ok = 0;
	for (int i = 0; i < kPoolSize; ++i) {
		auto& s = gSlots[i];
		if (s.refId && RE::TESForm::LookupByID<RE::Actor>(s.refId)) { ++ok; continue; }
		s.refId = 0;
		const RE::FormID fid = dh->LookupFormID(Forms::Local::PoolRefBase + RE::FormID(i), Forms::kPlugin);
		auto* form = fid ? RE::TESForm::LookupByID(fid) : nullptr;
		if (!form) {
			++st.noForm;
			if (!st.firstMiss) st.firstMiss = fid;
			continue;
		}
		auto* actor = form->As<RE::Actor>();
		if (!actor) {
			++st.notActor;
			if (st.firstType < 0) st.firstType = int(form->GetFormType());
			continue;
		}
		if (FillSlot(s, actor, st)) ++ok;
	}
	if (ok < kPoolSize && !gScanDone) {
		gScanDone = true;
		const RE::FormID npc0 = dh->LookupFormID(Forms::Local::PoolNPCBase, Forms::kPlugin);
		std::vector<std::pair<int, RE::Actor*>> found;
		if (npc0) {
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			[[maybe_unused]] const RE::BSReadLockGuard l{ lock };
			if (map) {
				for (auto& [id, form] : *map) {
					if (!form || form->GetFormType() != RE::FormType::ActorCharacter) continue;
					auto* a = form->As<RE::Actor>();
					auto* base = a ? a->GetActorBase() : nullptr;
					if (!base) continue;
					const RE::FormID bid = base->GetFormID();
					if (bid < npc0 || bid >= npc0 + RE::FormID(kPoolSize)) continue;
					found.emplace_back(int(bid - npc0), a);
				}
			}
		}
		for (auto& [idx, a] : found) {
			auto& s = gSlots[idx];
			if (s.refId) continue;
			if (FillSlot(s, a, st)) { ++ok; ++st.scanned; }
		}
	}
	Log::Line(std::string("[pool] resolve(") + a_when + ") " + std::to_string(ok) + "/" + std::to_string(kPoolSize) +
			  " noForm=" + std::to_string(st.noForm) + " notActor=" + std::to_string(st.notActor) +
			  " noBase=" + std::to_string(st.noBase) + " badRace=" + std::to_string(st.badRace) +
			  " viaScan=" + std::to_string(st.scanned) +
			  (st.firstMiss ? " firstMiss=" + Hex(st.firstMiss) : std::string()) +
			  (st.firstType >= 0 ? " firstType=" + std::to_string(st.firstType) : std::string()) +
			  (st.firstBadRace ? " firstRace=" + Hex(st.firstBadRace) : std::string()));
	return ok;
}

void Init()
{
	gSlots.assign(kPoolSize, Slot{});
	gScanDone = false;
	Resolve("dataLoaded");
	gScanDone = false;   // the fallback scan gets another chance once a game is loaded
}

int StickyUsed()
{
	int n = 0;
	for (auto& s : gSlots) n += s.reserved ? 1 : 0;
	return n;
}

int Unreserved(RaceId a_r, Sex a_s)
{
	int n = 0;
	for (auto& s : gSlots) n += (s.refId && !s.reserved && s.race == a_r && s.sex == a_s) ? 1 : 0;
	return n;
}

// An actor the engine can run AI on right now. One moved, enabled or placed this frame can have 3D but no parent cell yet:
// StartCombat on it read a null cell in the engine (M5b lab run 2, Oct 9: 38561 StartCombat -> 39444 at +0x10).
bool InWorld(RE::Actor* a_a)
{
	auto* c = a_a ? a_a->GetParentCell() : nullptr;
	return c && c->IsAttached() && a_a->Is3DLoaded() && !a_a->IsDisabled();
}

RE::Actor* ActorOf(const Member& a_m)
{
	if (!IsHumanFaction(a_m.faction)) return Creatures::ActorOf(a_m);   // monster members are placed creatures, not pool actors
	if (a_m.poolLocal < 0 || a_m.poolLocal >= int(gSlots.size())) return nullptr;
	auto id = gSlots[a_m.poolLocal].refId;
	return id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
}

bool IsPoolActor(const RE::Actor* a_a)
{
	if (!a_a) return false;
	auto id = a_a->GetFormID();
	for (auto& s : gSlots)
		if (s.refId == id) return true;
	return false;
}

bool Assign(Member& a_m, bool a_allowFallbackRace)
{
	if (gSlots.empty()) return false;
	// 1) sticky reservation
	if (a_m.poolLocal >= 0 && a_m.poolLocal < int(gSlots.size())) {
		auto& s = gSlots[a_m.poolLocal];
		if (s.refId && (s.owner == a_m.id || (!s.reserved && !s.inUse)) && s.race == a_m.race && s.sex == a_m.sex) {
			const bool tie = a_m.sticky && !a_m.waitlisted && (s.owner == a_m.id || StickyUsed() < kStickyCap);
			if (a_m.sticky && !a_m.waitlisted && !tie) a_m.waitlisted = true;   // cap reached: regular for now
			s.inUse = true;
			if (tie) { s.reserved = true; s.owner = a_m.id; }
			return true;
		}
		a_m.poolLocal = -1;
	}
	auto freeOf = [&](RaceId r, Sex x) {
		return Find([&](const Slot& s) { return s.refId && !s.inUse && !s.reserved && s.race == r && s.sex == x; });
	};
	if (a_m.sticky && a_m.waitlisted) {
		// regular for now (spec v1.2.1): any free actor of its own race and sex at each meeting, no tie;
		// its record (name, standing, gear, skills) is kept and it stays on the waiting list
		Slot* w = freeOf(a_m.race, a_m.sex);
		if (!w) return false;
		w->inUse = true;
		a_m.poolLocal = int(w - gSlots.data());
		return true;
	}
	Slot* s = freeOf(a_m.race, a_m.sex);
	if (!s && a_allowFallbackRace && !a_m.sticky && !IsVampireRace(a_m.race)) {
		// anonymous members only: same sex, never a vampire race, never for the same-race tests
		s = Find([&](const Slot& c) { return c.refId && !c.inUse && !c.reserved && c.sex == a_m.sex && !IsVampireRace(c.race); });
		if (s) a_m.race = s->race;
	}
	if (!s) return false;
	if (a_m.sticky) {
		if (StickyUsed() >= kStickyCap && !Sim::ReleaseStickyFor(a_m)) {
			// all 48 held by Veterans / nobles / Khajiit traders (or online): regular for now + waiting list
			a_m.waitlisted = true;
			s->inUse = true;
			a_m.poolLocal = int(s - gSlots.data());
			return true;
		}
		s->reserved = true;
		s->owner = a_m.id;
	}
	s->inUse = true;
	a_m.poolLocal = int(s - gSlots.data());
	return true;
}

// 176h (Mod Check 176g non-blocker): dry run of Assign for a whole group, no state change. Mirrors Assign's
// availability rules (own sticky slot, free slot of race+sex, same-sex non-vampire fallback for anonymous members)
// so it is never stricter than Assign. Lets BringOnline skip the navmesh search when the pool cannot fill the group.
bool CanAssignAll(const std::vector<Member>& a_members, bool a_allowFallbackRace)
{
	if (gSlots.empty()) return false;
	std::vector<char> taken(gSlots.size(), 0);
	auto freeIdx = [&](auto&& a_pred) -> int {
		for (std::size_t i = 0; i < gSlots.size(); ++i) {
			const auto& s = gSlots[i];
			if (!taken[i] && s.refId && !s.inUse && !s.reserved && a_pred(s)) return int(i);
		}
		return -1;
	};
	for (auto& m : a_members) {
		if (!m.alive() || m.health < 1.f) continue;
		if (m.poolLocal >= 0 && m.poolLocal < int(gSlots.size()) && !taken[m.poolLocal]) {
			const auto& s = gSlots[m.poolLocal];
			if (s.refId && (s.owner == m.id || (!s.reserved && !s.inUse)) && s.race == m.race && s.sex == m.sex) {
				taken[m.poolLocal] = 1;
				continue;
			}
		}
		int i = freeIdx([&](const Slot& s) { return s.race == m.race && s.sex == m.sex; });
		if (i < 0 && a_allowFallbackRace && !m.sticky && !IsVampireRace(m.race))
			i = freeIdx([&](const Slot& s) { return s.sex == m.sex && !IsVampireRace(s.race); });
		if (i < 0) return false;
		taken[i] = 1;
	}
	return true;
}

void Release(Member& a_m)
{
	if (a_m.poolLocal < 0 || a_m.poolLocal >= int(gSlots.size())) return;
	auto& s = gSlots[a_m.poolLocal];
	s.inUse = false;
	s.debugFill = false;
	if (!s.reserved || s.owner != a_m.id) a_m.poolLocal = -1;   // sticky members keep their face
}

bool Tie(Member& a_m)
{
	if (a_m.poolLocal < 0 || a_m.poolLocal >= int(gSlots.size())) return false;
	auto& s = gSlots[a_m.poolLocal];
	if (s.owner == a_m.id && s.reserved) return true;
	if (s.reserved || StickyUsed() >= kStickyCap) return false;
	s.reserved = true;
	s.owner = a_m.id;
	a_m.sticky = true;
	a_m.waitlisted = false;
	return true;
}

void ReleaseSticky(Member& a_m)
{
	if (a_m.poolLocal >= 0 && a_m.poolLocal < int(gSlots.size())) {
		auto& s = gSlots[a_m.poolLocal];
		if (s.owner == a_m.id) { s.reserved = false; s.owner = 0; }
		s.inUse = false;
	}
	a_m.poolLocal = -1;
}

void RebuildReservations(std::vector<Group>& a_groups)
{
	for (auto& s : gSlots) { s.inUse = false; s.reserved = false; s.owner = 0; s.debugFill = false; }
	for (auto& g : a_groups)
		for (auto& m : g.members) {
			if (!m.alive() || !m.sticky || m.waitlisted) continue;
			if (m.poolLocal >= 0 && m.poolLocal < int(gSlots.size())) {
				auto& s = gSlots[m.poolLocal];
				if (s.refId && !s.reserved && s.race == m.race && s.sex == m.sex) {
					s.reserved = true;
					s.owner = m.id;
					continue;
				}
			}
			m.poolLocal = -1;
			m.waitlisted = true;   // re-queued, sticky identity preserved
		}
}

void FullWipe(RE::Actor* a_a)
{
	if (!a_a) return;
	// inventory
	auto inv = a_a->GetInventory();
	for (auto& [obj, data] : inv) {
		if (!obj || data.first <= 0) continue;
		a_a->RemoveItem(obj, data.first, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
	}
	// spells added at runtime
	auto& added = a_a->GetActorRuntimeData().addedSpells;
	std::vector<RE::SpellItem*> spells(added.begin(), added.end());
	for (auto* sp : spells) if (sp) a_a->RemoveSpell(sp);
	// active effects
	if (auto* mt = a_a->AsMagicTarget()) {
		if (auto* list = mt->GetActiveEffectList()) {
			std::vector<RE::ActiveEffect*> fx(list->begin(), list->end());
			for (auto* e : fx) if (e) e->Dispel(true);
		}
	}
	// perks live on the (unique) base NPC
	if (auto* base = a_a->GetActorBase()) {
		SetBasePerks(base, {});
		a_a->ApplyPerksFromBase();
	}
	// runtime factions
	const auto& r = Forms::R();
	for (auto* f : { r.fBandit, r.fHunter, r.fAdventurer, r.fPlayerEnemy, r.fSlot[0], r.fSlot[1], r.fSlot[2] })
		if (f) a_a->RemoveFromFaction(f);
	for (auto* f : r.fFaction)
		if (f) a_a->RemoveFromFaction(f);
	if (r.fMode) a_a->AddToFaction(r.fMode, 0);
	// combat / aggression
	a_a->StopCombat();
	if (auto* avo = a_a->AsActorValueOwner()) {
		avo->SetBaseActorValue(RE::ActorValue::kAggression, 0.f);
		for (auto av : { RE::ActorValue::kHealth, RE::ActorValue::kMagicka, RE::ActorValue::kStamina })
			avo->RestoreActorValue(av, 100000.f);
	}
	// linked refs
	for (auto* kw : { r.kwTravel, r.kwLeader, r.kwFlee }) a_a->extraList.SetLinkedRef(nullptr, kw);
	a_a->SetDisplayName("", false);
}

void ApplyPerks(RE::Actor* a_a, const std::vector<RE::FormID>& a_perks)
{
	auto* base = a_a ? a_a->GetActorBase() : nullptr;
	if (!base) return;
	std::vector<RE::BGSPerk*> v;
	for (auto id : a_perks)
		if (auto* p = RE::TESForm::LookupByID<RE::BGSPerk>(id)) v.push_back(p);
	SetBasePerks(base, v);
	a_a->ApplyPerksFromBase();
}

void SweepAllToHolding()
{
	const auto& r = Forms::R();
	if (!r.holdingMarker) return;
	for (auto& s : gSlots) {
		s.inUse = false;
		if (!s.refId) continue;
		auto* a = RE::TESForm::LookupByID<RE::Actor>(s.refId);
		if (!a) continue;
		if (a->GetParentCell() != r.holdingCell) {
			a->StopCombat();
			a->MoveTo(r.holdingMarker);
		}
		if (!a->IsDisabled()) a->Disable();
	}
}


}
