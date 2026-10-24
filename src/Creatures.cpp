#include "ALife/Log.h"
#include "ALife/Systems.h"

#include <chrono>
#include <random>

// Monster squads on screen (docs/MUTANTS.md): every living member is a vanilla creature placed from its monsters.ltx form
// (a leveled list lets the engine pick the variant for the player's level). They run their own AI; the board only
// gets them back (wounds and deaths) when the squad goes offline. Pool::ActorOf delegates here for monster members.
namespace ALife::Creatures {
namespace {
	std::unordered_map<std::uint64_t, RE::ObjectRefHandle> gRefs;   // member id -> placed creature
	std::unordered_map<std::uint64_t, float> gWounds;              // member id -> health share to apply once its 3D is in
	std::unordered_map<std::uint64_t, float> gLastShare;           // member id -> health share last seen (its ref can vanish before the offline pass)
	std::vector<RE::FormID> gSweep;                                 // creatures a saved session left in the world (deleted after the load)
	using Clock = std::chrono::steady_clock;
	std::vector<std::pair<RE::ObjectRefHandle, Clock::time_point>> gDoomed;   // disabled creatures, deleted once the time is up

	RE::Actor* Resolve(std::uint64_t a_id)
	{
		auto it = gRefs.find(a_id);
		if (it == gRefs.end()) return nullptr;
		auto ref = it->second.get();
		return ref ? ref->As<RE::Actor>() : nullptr;
	}

	// Disabled now, deleted a few seconds later: deleting a creature placed moments ago crashed in its movement controller
	// (T2 lab, MovementControllerNPC null read one second after a pack spawned and went offline again).
	void Remove(RE::Actor* a_a)
	{
		if (!a_a) return;
		a_a->StopCombat();
		a_a->Disable();
		gDoomed.emplace_back(a_a->GetHandle(), Clock::now() + std::chrono::seconds(5));
	}

	// A leveled actor list down to one actor base, the way the engine levels an encounter for the player: entries up to the
	// player's level, all of them with "calculate from all levels", else those of the highest such level; nested lists recurse.
	// (TESObjectREFR::PlaceObjectAtMe goes through CreateReferenceAtLocation, which does not level a list: its ref is no actor.)
	RE::TESBoundObject* PickBase(RE::TESForm* a_form, int a_depth = 0)
	{
		if (!a_form || a_depth > 8) return nullptr;
		if (auto* npc = a_form->As<RE::TESNPC>()) return npc;
		auto* lv = a_form->As<RE::TESLevCharacter>();
		if (!lv || lv->entries.empty()) return nullptr;
		auto* pc = RE::PlayerCharacter::GetSingleton();
		const std::uint16_t level = pc ? pc->GetLevel() : 1;
		const bool all = (lv->llFlags & RE::TESLeveledList::kCalculateFromAllLevelsLTOrEqPCLevel) != 0;
		std::vector<RE::TESForm*> pick;
		std::uint16_t best = 0, lowest = 0xFFFF;
		RE::TESForm* lowestForm = nullptr;
		for (auto& e : lv->entries) {
			if (!e.form) continue;
			if (e.level < lowest) { lowest = e.level; lowestForm = e.form; }
			if (e.level > level) continue;
			if (!all && e.level > best) { best = e.level; pick.clear(); }
			if (all || e.level == best) pick.push_back(e.form);
		}
		if (pick.empty() && lowestForm) pick.push_back(lowestForm);
		if (pick.empty()) return nullptr;
		static std::mt19937 rng{ std::random_device{}() };
		return PickBase(pick[std::uniform_int_distribution<std::size_t>(0, pick.size() - 1)(rng)], a_depth + 1);
	}
}

RE::Actor* ActorOf(const Member& a_m) { return Resolve(a_m.id); }

bool IsCreature(const RE::Actor* a_a)
{
	if (!a_a) return false;
	for (auto& [id, h] : gRefs)
		if (auto ref = h.get(); ref && ref.get() == a_a) return true;
	return false;
}

Group* GroupOf(RE::Actor* a_a, Member** a_out)
{
	if (!a_a || gRefs.empty() || !Sim::BoardReady()) return nullptr;
	for (auto& g : Sim::Groups()) {
		if (!g.online || !Sim::Cfg().IsMonster(g.faction)) continue;
		for (auto& m : g.members)
			if (Resolve(m.id) == a_a) { if (a_out) *a_out = &m; return &g; }
	}
	return nullptr;
}

RE::Actor* Spawn(Member& a_m, RE::TESObjectREFR* a_at)
{
	const CreatureDef* d = Sim::Cfg().CreatureOf(a_m);
	if (!d || !d->form || !a_at) return nullptr;
	RE::TESForm* form = nullptr;
	if (d->plugin.empty()) form = RE::TESForm::LookupByID(d->form);
	else if (auto* dh = RE::TESDataHandler::GetSingleton(); dh && dh->LookupModByName(d->plugin)) form = dh->LookupForm(d->form, d->plugin);
	auto* base = PickBase(form);
	if (!base) return nullptr;
	auto ref = a_at->PlaceObjectAtMe(base, false);
	auto* a = ref ? ref->As<RE::Actor>() : nullptr;
	if (!a) {
		if (ref) { ref->Disable(); ref->SetDelete(true); }
		return nullptr;
	}
	gRefs[a_m.id] = a->GetHandle();
	if (a_m.faction == Faction::Dragon && Sim::BoardReady()) {   // docs/DRAGONS.md: placed above the spot, so it arrives flying
		auto p = a->GetPosition();
		p.z += Sim::B().DragonEff().spawnHeight;
		a->SetPosition(p, true);
	}
	const float share = a_m.health / std::max(1.f, a_m.healthMax);
	if (share < 0.98f) gWounds[a_m.id] = std::clamp(share, 0.05f, 1.f);
	return a;
}

void Despawn(Group& a_g)
{
	for (auto& m : a_g.members) {
		auto* a = Resolve(m.id);
		gRefs.erase(m.id);
		gWounds.erase(m.id);
		const auto last = gLastShare.find(m.id);
		const float lastShare = last != gLastShare.end() ? last->second : -1.f;
		if (last != gLastShare.end()) gLastShare.erase(last);
		if (!a) {   // never placed, or the engine dropped the ref (a long teleport): deaths came in through the death event, wounds as last seen
			if (lastShare > 0.f) m.health = std::min(m.health, std::clamp(lastShare, 0.05f, 1.f) * m.healthMax);
			continue;
		}
		if (a->IsDead()) { m.health = 0.f; continue; }   // the body stays for the player to skin
		if (auto* avo = a->AsActorValueOwner()) {
			const float perm = std::max(1.f, avo->GetPermanentActorValue(RE::ActorValue::kHealth));
			const float cur = avo->GetActorValue(RE::ActorValue::kHealth);
			m.health = std::clamp(cur / perm, 0.05f, 1.f) * m.healthMax;
		}
		Remove(a);
	}
}

int Live()
{
	int n = 0;
	for (auto& [id, h] : gRefs)
		if (auto ref = h.get(); ref && !ref->IsDead()) ++n;
	return n;
}

std::vector<RE::FormID> Refs()
{
	std::vector<RE::FormID> out;
	for (auto& [id, h] : gRefs)
		if (auto ref = h.get(); ref && !ref->IsDead()) out.push_back(ref->GetFormID());
	for (auto& [h, t] : gDoomed)   // disabled, not deleted yet: the sweep after a load removes them too
		if (auto ref = h.get()) out.push_back(ref->GetFormID());
	return out;
}

void QueueSweep(std::vector<RE::FormID> a_refs) { gSweep = std::move(a_refs); }
void ClearSweep() { gSweep.clear(); }

void Tick()
{
	if (!gSweep.empty()) {
		int removed = 0;
		for (auto id : gSweep)
			if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id); a && !a->IsDead() && !a->IsPlayerRef()) { Remove(a); ++removed; }
		Log::Line("[creatures] removed " + std::to_string(removed) + " of " + std::to_string(gSweep.size()) + " creature(s) the saved session had on screen");
		gSweep.clear();
	}
	for (auto it = gWounds.begin(); it != gWounds.end();) {
		auto* a = Resolve(it->first);
		if (!a) { it = gWounds.erase(it); continue; }
		if (!a->Is3DLoaded()) { ++it; continue; }
		if (auto* avo = a->AsActorValueOwner()) {
			const float perm = std::max(1.f, avo->GetPermanentActorValue(RE::ActorValue::kHealth));
			avo->DamageActorValue(RE::ActorValue::kHealth, perm * (1.f - it->second));
		}
		it = gWounds.erase(it);
	}
	for (auto& [id, h] : gRefs)
		if (auto ref = h.get(); ref && ref->Is3DLoaded() && !ref->IsDead())
			if (auto* a = ref->As<RE::Actor>(); a && a->AsActorValueOwner()) {
				auto* avo = a->AsActorValueOwner();
				gLastShare[id] = avo->GetActorValue(RE::ActorValue::kHealth) / std::max(1.f, avo->GetPermanentActorValue(RE::ActorValue::kHealth));
			}
	if (!gDoomed.empty()) {
		const auto now = Clock::now();
		std::erase_if(gDoomed, [&](const auto& d) {
			if (now < d.second) return false;
			if (auto ref = d.first.get()) ref->SetDelete(true);
			return true;
		});
	}
}

void Reset()
{
	gRefs.clear();
	gWounds.clear();
	gLastShare.clear();
	gDoomed.clear();
}
}
