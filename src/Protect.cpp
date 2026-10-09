#include "ALife/Systems.h"
#include "ALife/Forms.h"
#include "ALife/Log.h"

namespace ALife::Protect {
namespace {
	std::unordered_set<RE::FormID> gBossRefs;        // LCRT Boss refs (actors) in every location
	std::unordered_set<RE::FormID> gBossContainers;  // LCRT BossContainer refs (never looted by A-Life)
	std::vector<RE::TESFaction*> gJobFactions;
	RE::TESFaction* gFollowerFac{ nullptr };
	RE::TESFaction* gPlayerFollowerFac{ nullptr };
	std::uint32_t gSaves{ 0 };
	std::mutex gDeferMx;
	std::vector<std::pair<RE::ActorHandle, RE::ActorHandle>> gDeferred;   // fights to break up, handled on the next tick

	bool InAnyFaction(RE::Actor* a, const std::vector<RE::TESFaction*>& v)
	{
		for (auto* f : v) if (f && a->IsInFaction(f)) return true;
		return false;
	}
	bool IsVendorActor(RE::Actor* a)
	{
		auto* base = a->GetActorBase();
		if (!base) return false;
		for (auto& fr : base->factions)
			if (fr.faction && fr.rank >= 0 && fr.faction->IsVendor()) return true;
		return false;
	}
}

void Init()
{
	gBossRefs.clear();
	gBossContainers.clear();
	const auto& r = Forms::R();
	if (auto* dh = RE::TESDataHandler::GetSingleton()) {
		for (auto* loc : dh->GetFormArray<RE::BGSLocation>()) {
			if (!loc) continue;
			for (auto& sr : loc->specialRefs) {
				if (!sr.type) continue;
				if (sr.type == r.lcrtBoss) gBossRefs.insert(sr.refData.refID);
				else if (sr.type == r.lcrtBossContainer) gBossContainers.insert(sr.refData.refID);
			}
		}
	}
	gJobFactions.clear();
	using namespace Forms::Vanilla;
	for (auto id : { JobMerchantFaction, JobInnkeeperFaction, JobFarmerFaction, JobBlacksmithFaction, JobApothecaryFaction,
			 JobCourtWizardFaction, JobJarlFaction, JobStewardFaction })
		if (auto* f = Forms::Get<RE::TESFaction>(id)) gJobFactions.push_back(f);
	gFollowerFac = Forms::Get<RE::TESFaction>(CurrentFollowerFaction);
	gPlayerFollowerFac = Forms::Get<RE::TESFaction>(PlayerFollowerFaction);
	Log::Line("[protect] boss refs=" + std::to_string(gBossRefs.size()) + " boss containers=" + std::to_string(gBossContainers.size()));
}

bool IsFollower(RE::Actor* a)
{
	if (!a || a->IsPlayerRef()) return false;
	if (a->IsPlayerTeammate()) return true;
	if (gFollowerFac && a->IsInFaction(gFollowerFac) && a->GetFactionRank(gFollowerFac, false) >= 0) return true;
	if (a->IsCommandedActor()) {
		auto cmd = a->GetCommandingActor();
		if (cmd && cmd->IsPlayerRef()) return true;
	}
	return false;
}

bool InQuestAlias(RE::TESObjectREFR* a_r)
{
	if (!a_r) return false;
	auto* xa = a_r->extraList.GetByType<RE::ExtraAliasInstanceArray>();
	if (!xa) return false;
	RE::BSReadLockGuard l(xa->lock);
	for (auto* d : xa->aliases)
		if (d && d->quest && (d->quest->IsRunning() || d->quest->IsStarting())) return true;
	return false;
}

bool IsBossRefId(RE::FormID a_id) { return gBossRefs.contains(a_id) || gBossContainers.contains(a_id); }
bool IsBossRef(const RE::TESObjectREFR* a_r) { return a_r && IsBossRefId(a_r->GetFormID()); }

bool IsLeaveAloneActor(RE::Actor* a)
{
	if (!a) return false;
	for (auto id : Patch::LeaveAloneFactions())
		if (auto* f = RE::TESForm::LookupByID<RE::TESFaction>(id); f && a->IsInFaction(f)) return true;
	return false;
}

bool IsProtectedActor(RE::Actor* a)
{
	if (!a || a->IsPlayerRef() || Pool::IsPoolActor(a)) return false;
	if (IsFollower(a)) return true;
	if (auto* base = a->GetActorBase()) {
		if (base->IsUnique() || base->IsEssential() || base->IsProtected()) return true;   // named NPCs
	}
	if (a->IsEssential() || a->IsProtected() || a->IsChild()) return true;
	if (IsBossRef(a)) return true;
	if (InQuestAlias(a)) return true;
	if (InAnyFaction(a, gJobFactions) || IsVendorActor(a)) return true;     // merchants, innkeepers, farmers...
	if (IsLeaveAloneActor(a)) return true;
	return false;
}

static void Pacify(RE::Actor* a_aggr, RE::Actor* a_target);

// Breaking up a fight from inside the combat event that announced it makes the engine start the fight again at once (the two are
// enemies), which raises the same event again: an endless recursion. So the event only notes the fight; the tick ends it.
static void Defer(RE::Actor* a_aggr, RE::Actor* a_target)
{
	std::lock_guard l(gDeferMx);
	if (gDeferred.size() < 64) gDeferred.emplace_back(a_aggr->GetHandle(), a_target ? a_target->GetHandle() : RE::ActorHandle{});
}

static void Pacify(RE::Actor* a_aggr, RE::Actor* a_target)
{
	a_aggr->StopCombat();
	if (a_target) {
		auto tgt = a_target->GetActorRuntimeData().currentCombatTarget.get();
		if (tgt && tgt.get() == a_aggr) a_target->StopCombat();
	}
	a_aggr->EvaluatePackage(true, false);
	++gSaves;
}

void OnHit(RE::Actor* a_target, RE::Actor* a_aggressor)
{
	if (!a_target || !a_aggressor || !Pool::IsPoolActor(a_aggressor)) return;
	if (!IsProtectedActor(a_target)) return;
	// undo A-Life damage on a protected NPC and break the fight
	if (auto* avo = a_target->AsActorValueOwner()) avo->RestoreActorValue(RE::ActorValue::kHealth, 100000.f);
	Defer(a_aggressor, a_target);
	Log::Line(std::string("[protect] hit blocked: ") + a_aggressor->GetDisplayFullName() + " -> " + a_target->GetDisplayFullName());
}

void OnCombat(RE::Actor* a_actor, RE::Actor* a_target)
{
	if (!a_actor || !a_target) return;
	if (Pool::IsPoolActor(a_actor) && IsProtectedActor(a_target)) {
		Defer(a_actor, a_target);
		Log::Line(std::string("[protect] combat refused: ") + a_actor->GetDisplayFullName() + " vs protected " + a_target->GetDisplayFullName());
	} else if (Pool::IsPoolActor(a_target) && IsProtectedActor(a_actor) && !IsFollower(a_actor)) {
		// protected NPCs started it (e.g. merchants on a pool actor): end it so A-Life never retaliates
		Defer(a_target, a_actor);
	}
}

void Tick()
{
	std::vector<std::pair<RE::ActorHandle, RE::ActorHandle>> work;
	{
		std::lock_guard l(gDeferMx);
		work.swap(gDeferred);
	}
	for (auto& [ah, th] : work) {
		auto aggr = ah.get();
		auto tgt = th.get();
		if (aggr && !aggr->IsDead()) Pacify(aggr.get(), tgt.get());
	}
	for (auto& s : Pool::Slots()) {
		if (!s.inUse || !s.refId) continue;
		auto* a = RE::TESForm::LookupByID<RE::Actor>(s.refId);
		if (!a || !a->IsInCombat()) continue;
		auto tgt = a->GetActorRuntimeData().currentCombatTarget.get();
		if (tgt && IsProtectedActor(tgt.get())) Pacify(a, tgt.get());
	}
}
}

// ---------------------------------------------------------------- patch hooks
namespace ALife::Patch {
namespace {
	std::unordered_set<RE::FormID> gAvoid;
	std::vector<RE::NiPoint3> gKeepAway;
	std::vector<RE::FormID> gLeaveAlone;

	void Collect(std::string_view a_file)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto eat = [&](RE::FormID a_local, auto&& a_fn) {
			if (auto* l = dh->LookupForm<RE::BGSListForm>(a_local, a_file)) {
				l->ForEachForm([&](RE::TESForm* f) { if (f) a_fn(f); return RE::BSContainer::ForEachResult::kContinue; });
			}
		};
		eat(0x800, [](RE::TESForm* f) { if (f->Is(RE::FormType::Location)) gAvoid.insert(f->GetFormID()); });
		eat(0x801, [](RE::TESForm* f) {
			if (auto* r = f->As<RE::TESObjectREFR>()) gKeepAway.push_back(r->GetPosition());
		});
		eat(0x802, [](RE::TESForm* f) { if (f->Is(RE::FormType::Faction)) gLeaveAlone.push_back(f->GetFormID()); });
	}
}

void Reload()
{
	gAvoid.clear();
	gKeepAway.clear();
	gLeaveAlone.clear();
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh) return;
	int n = 0;
	auto visit = [&](const RE::TESFile* f) {
		if (!f) return;
		std::string_view name = f->GetFilename();
		if (name.size() > 8 && name.substr(0, 8) == "ALife - "sv) { Collect(name); ++n; }
	};
	for (std::uint32_t i = 0; i < dh->GetLoadedModCount(); ++i) visit(dh->GetLoadedMods()[i]);
	for (std::uint32_t i = 0; i < dh->GetLoadedLightModCount(); ++i) visit(dh->GetLoadedLightMods()[i]);
	Log::Line("[patch] plugins=" + std::to_string(n) + " avoid=" + std::to_string(gAvoid.size()) + " keepAway=" +
			  std::to_string(gKeepAway.size()) + " leaveAlone=" + std::to_string(gLeaveAlone.size()));
}

bool AvoidLocation(RE::FormID a_loc) { return gAvoid.contains(a_loc); }
bool CampKeepAway(float x, float y, float z)
{
	for (auto& p : gKeepAway) {
		float dx = p.x - x, dy = p.y - y, dz = p.z - z;
		if (dx * dx + dy * dy + dz * dz < 2000.f * 2000.f) return true;
	}
	return false;
}
bool LeaveAloneFaction(RE::FormID a_fac) { return std::find(gLeaveAlone.begin(), gLeaveAlone.end(), a_fac) != gLeaveAlone.end(); }
const std::vector<RE::FormID>& LeaveAloneFactions() { return gLeaveAlone; }
}
