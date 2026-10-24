// Squads loot the dead. In STALKER the stalkers strip every corpse they pass, the player's kills included; in Skyrim a body stays full until
// the player empties it. A calm squad that has a body in sight (any body: its own fallen, an enemy's, something the player killed and
// left) walks over after a grace period, stops, and takes what its members would use: gold, gear, potions, food, ammunition. Bodies the
// player kills are therefore worth looting promptly, and what the world's fighters wear ends up on whoever won.
#include "ALife/Systems.h"
#include "ALife/Companions.h"
#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Settings.h"
#include "ALife/Talk.h"

namespace ALife::Scavenge {
namespace {
	struct Job
	{
		RE::ObjectRefHandle corpse;
		float started{ 0 };
		float arrivedAt{ -1 };
		float nextMove{ 0 };
		float lastDist{ 99999 };
		float stuckFor{ 0 };
	};
	std::unordered_map<std::uint64_t, Job> gJobs;
	std::unordered_map<std::uint64_t, float> gNextScan;
	std::unordered_map<RE::FormID, float> gSeen;     // when a body was first noticed: nobody loots it before the grace period is over
	std::unordered_set<RE::FormID> gDone;            // bodies already looted (or not worth the walk)
	float gClock{ 0 };
	int gLooted{ 0 };

	Member* MemberOf(Group& g, RE::Actor* a)
	{
		for (auto& m : g.members) if (Pool::ActorOf(m) == a) return &m;
		return nullptr;
	}

	// What the member would take from this body: gold and the items it wants, within what it can still carry.
	struct Take
	{
		std::vector<std::pair<RE::TESBoundObject*, int>> items;
		int gold{ 0 };
		int value{ 0 };
	};

	Take Survey(RE::Actor* corpse, RE::Actor* looter, Member& m)
	{
		Take t;
		const auto* gold = Forms::R().gold;
		float room = looter->AsActorValueOwner()->GetActorValue(RE::ActorValue::kCarryWeight) - looter->GetWeightInContainer();
		auto inv = corpse->GetInventory();
		for (auto& [obj, data] : inv) {
			const int count = data.first;
			if (!obj || count <= 0) continue;
			if (obj == gold) { t.gold += count; t.value += count; continue; }
			if (data.second && data.second->IsQuestObject()) continue;   // never a quest item
			if (obj->Is(RE::FormType::KeyMaster)) continue;
			if (!Economy::Wants(m, obj)) continue;
			const float w = obj->GetWeight() * float(count);
			if (w > room) continue;
			room -= w;
			t.items.emplace_back(obj, count);
			t.value += obj->GetGoldValue() * count;
		}
		return t;
	}

	void LootCorpse(Group& g, RE::Actor* lead, RE::Actor* corpse)
	{
		Member* m = MemberOf(g, lead);
		if (!m) return;
		const Take t = Survey(corpse, lead, *m);
		if (t.gold > 0 && Forms::R().gold) corpse->RemoveItem(Forms::R().gold, t.gold, RE::ITEM_REMOVE_REASON::kRemove, nullptr, lead);
		for (auto& [obj, count] : t.items) corpse->RemoveItem(obj, count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, lead);
		++gLooted;
		if (Settings::Get().bDebug)
			Log::Line(std::string("[scav] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + " looted " + corpse->GetDisplayFullName() + ": " + std::to_string(t.items.size()) +
					  " kinds of item and " + std::to_string(t.gold) + " gold");
	}

	// The nearest body in sight that is worth the walk and past its grace period.
	RE::Actor* FindCorpse(Group& g, RE::Actor* lead, Member& m)
	{
		const auto& cfg = Sim::Cfg();
		const auto lp = lead->GetPosition();
		std::vector<std::pair<float, RE::Actor*>> found;
		if (auto* tes = RE::TES::GetSingleton())
			tes->ForEachReferenceInRange(lead, cfg.lootRadius, [&](RE::TESObjectREFR* r) {
				auto* a = r ? r->As<RE::Actor>() : nullptr;
				if (!a || a == lead || a->IsPlayerRef() || !a->IsDead() || !a->Is3DLoaded()) return RE::BSContainer::ForEachResult::kContinue;
				if (gDone.contains(a->GetFormID())) return RE::BSContainer::ForEachResult::kContinue;
				if (Protect::IsBossRef(a) || Protect::IsProtectedActor(a)) return RE::BSContainer::ForEachResult::kContinue;   // quest characters, followers, merchants
				const auto p = a->GetPosition();
				found.emplace_back((p.x - lp.x) * (p.x - lp.x) + (p.y - lp.y) * (p.y - lp.y), a);
				return RE::BSContainer::ForEachResult::kContinue;
			});
		std::sort(found.begin(), found.end(), [](auto& x, auto& y) { return x.first < y.first; });
		int looked = 0;
		for (auto& [d2, a] : found) {
			const auto id = a->GetFormID();
			auto seen = gSeen.try_emplace(id, gClock).first;
			if (gClock - seen->second < cfg.lootGraceSeconds) continue;
			if (++looked > 3) break;   // only the nearest few are inventoried per look
			const Take t = Survey(a, lead, m);
			if (t.value < cfg.lootMinValue) { gDone.insert(id); continue; }
			return a;
		}
		return nullptr;
	}

	void End(Group& g, bool a_resume)
	{
		gJobs.erase(g.id);
		if (a_resume) Online::Resume(g);
	}
}

void Reset()
{
	gJobs.clear();
	gNextScan.clear();
	gSeen.clear();
	gDone.clear();
	gLooted = 0;
}

bool Busy(std::uint64_t a_groupId) { return gJobs.contains(a_groupId); }

bool TickGroup(Group& g, RE::Actor* lead, float dt)
{
	const auto& cfg = Sim::Cfg();
	gClock += 0.f;   // the clock advances once per frame, in Tick
	const bool looter = Companions::Has(g.id) ? Companions::Loots(g.id) : cfg.looters[int(g.faction)];   // companions: only when told
	if (!cfg.lootCorpses || !Settings::Get().loot || !looter || !lead) return false;
	auto job = gJobs.find(g.id);
	const bool calm = g.trust == Trust::Neutral && !lead->IsInCombat() && Talk::ConversationSquad() != g.id && !Behavior::TalkPending(g);
	if (job != gJobs.end()) {
		auto corpseRef = job->second.corpse.get();
		auto* corpse = corpseRef ? corpseRef->As<RE::Actor>() : nullptr;
		if (!calm || !corpse || !corpse->Is3DLoaded() || gClock - job->second.started > cfg.lootGiveUpSeconds) {
			if (Settings::Get().bDebug)
				Log::Line(std::string("[scav] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + " drops the body: " + (!calm ? "not calm" : !corpse ? "body gone" : !corpse->Is3DLoaded() ? "body unloaded" : "took too long"));
			if (corpse && gClock - job->second.started > cfg.lootGiveUpSeconds) gDone.insert(corpse->GetFormID());   // unreachable: forget it
			End(g, true);
			return false;
		}
		auto& j = job->second;
		if (gClock >= j.nextMove) {
			j.nextMove = gClock + 1.5f;
			if (Online::ApproachRef(g, corpse, 90.f) && j.arrivedAt < 0.f) j.arrivedAt = gClock;
			// a body on a hillside or in the brush can be out of the navmesh's reach: if the squad stops short and the player cannot see it,
			// it simply gets there; if the player can see it, the squad tries a while longer and then forgets the body
			if (j.arrivedAt < 0.f) {
				const auto a = lead->GetPosition(), b = corpse->GetPosition();
				const float d = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
				if (j.lastDist - d > 40.f) j.stuckFor = 0.f; else j.stuckFor += 1.5f;
				j.lastDist = d;
				bool unused = false;
				auto* pc = RE::PlayerCharacter::GetSingleton();
				if (j.stuckFor >= 6.f && pc && !pc->HasLineOfSight(lead, unused)) {
					RE::NiPoint3 to{ b.x + (a.x - b.x) / std::max(1.f, d) * 110.f, b.y + (a.y - b.y) / std::max(1.f, d) * 110.f, b.z + 40.f };
					float ground = 0.f;
					if (auto* tes = RE::TES::GetSingleton(); tes && tes->GetLandHeight(to, ground)) to.z = ground + 8.f;
					lead->SetPosition(to, true);
					j.stuckFor = 0.f;
					if (Settings::Get().bDebug) Log::Line(std::string("[scav] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + " was stuck " + std::to_string(int(d)) + " units out of sight and gets to the body");
				}
			}
			if (Settings::Get().bDebug) {
				static float lastLog = 0.f;
				if (gClock - lastLog > 4.f) {
					lastLog = gClock;
					const auto a = lead->GetPosition(), b = corpse->GetPosition();
					Log::Line(std::string("[scav] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + " is " + std::to_string(int(std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)))) +
							  " units from the body" + (j.arrivedAt >= 0.f ? " (arrived)" : ""));
				}
			}
		}
		if (j.arrivedAt >= 0.f && gClock - j.arrivedAt >= 3.f) {   // a few seconds at the body, then the pack is heavier
			LootCorpse(g, lead, corpse);
			gDone.insert(corpse->GetFormID());
			End(g, true);
			return false;
		}
		return true;
	}
	if (!calm) return false;
	auto& next = gNextScan[g.id];
	if (gClock < next) return false;
	next = gClock + 5.f + float(Mix64(g.id * 31ULL + std::uint64_t(gClock)) % 40) / 10.f;   // 5..9 s, staggered between squads
	Member* m = MemberOf(g, lead);
	if (!m || !m->alive()) return false;
	if (auto* corpse = FindCorpse(g, lead, *m)) {
		Job j;
		j.corpse = corpse->GetHandle();
		j.started = gClock;
		gJobs[g.id] = j;
		if (Settings::Get().bDebug)
			Log::Line(std::string("[scav] ") + FactionId(g.faction) + " squad #" + std::to_string(g.id) + " heads for " + corpse->GetDisplayFullName());
		return true;
	}
	return false;
}

void Tick(float dt)
{
	gClock += dt;
	if (gSeen.size() > 600) {   // bodies decay and bases reload: do not let the notes grow for ever
		gSeen.clear();
		gDone.clear();
	}
}

int Looted() { return gLooted; }
}
