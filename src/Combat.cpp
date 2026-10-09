#include "ALife/Systems.h"
#include "ALife/Forms.h"
#include "ALife/Log.h"

namespace ALife::Combat {
namespace {
	struct Profile
	{
		float hp{ 100 };
		float dmg{ 4 };          // per swing/cast after skill and perks
		float reduction{ 0 };    // 0..0.8 from real armor rating
		float hitBase{ 0.6f };
		int skill{ kOneHanded };
	};

	bool HasPerk(const Member& m, RE::FormID id) { return std::find(m.perks.begin(), m.perks.end(), id) != m.perks.end(); }

	RE::TESObjectWEAP* MainWeapon(const Member& m)
	{
		RE::TESObjectWEAP* best = nullptr;
		for (auto id : m.equipped)
			if (auto* w = RE::TESForm::LookupByID<RE::TESObjectWEAP>(id)) {
				if (w->IsStaff()) continue;
				if (!best || w->GetAttackDamage() > best->GetAttackDamage()) best = w;
			}
		return best;
	}

	// strongest damaging Destruction spell the member knows (mage stand-in for weapon damage)
	float DestructionDamage(const Member& m)
	{
		float best = 0;
		for (auto id : m.spells) {
			auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(id);
			if (!sp) continue;
			for (auto* e : sp->effects) {
				if (!e || !e->baseEffect) continue;
				if (e->baseEffect->GetMagickSkill() != RE::ActorValue::kDestruction) continue;
				if (e->baseEffect->IsDetrimental()) best = std::max(best, e->GetMagnitude());
			}
		}
		return best;
	}

	float ArmorRating(const Member& m)
	{
		float ar = 0;
		bool heavy = false, light = false;
		for (auto id : m.equipped)
			if (auto* a = RE::TESForm::LookupByID<RE::TESObjectARMO>(id)) {
				float base = float(a->armorRating) / 100.f;
				float skill = a->IsHeavyArmor() ? m.skills[kHeavyArmor] : (a->IsLightArmor() ? m.skills[kLightArmor] : 0.f);
				if (a->IsHeavyArmor()) heavy = true;
				if (a->IsLightArmor()) light = true;
				if (a->IsShield()) skill = m.skills[kBlock];
				ar += base * (1.f + 0.4f * skill / 100.f);   // vanilla displayed-armor skill scaling
			}
		if (heavy && HasPerk(m, 0xBCD2A /*Juggernaut00*/)) ar *= 1.2f;
		if (light && HasPerk(m, 0xBE123 /*AgileDefender00*/)) ar *= 1.2f;
		return ar;
	}

	Profile Build(const Member& m)
	{
		Profile p;
		p.hp = std::max(1.f, m.health);
		p.reduction = std::min(0.8f, ArmorRating(m) * 0.0012f);   // 0.12% per point, 80% cap (vanilla)
		p.skill = MainCombatSkill(m);
		if (p.skill == kDestruction) {
			float d = DestructionDamage(m);
			if (d <= 0.f) d = 8.f;   // Flames-tier stand-in when the spell list is empty
			float mult = 1.f + m.skills[kDestruction] / 200.f;
			// magicka sustain: a mage with an empty pool falls back to fists
			p.dmg = (m.magickaMax > 20.f ? d : 2.f) * mult;
		} else {
			auto* w = MainWeapon(m);
			float base = w ? float(w->GetAttackDamage()) : 3.f;
			float mult = 1.f + m.skills[p.skill] / 200.f;
			if (p.skill == kOneHanded && HasPerk(m, 0xBABE4)) mult *= 1.2f;   // Armsman
			if (p.skill == kTwoHanded && HasPerk(m, 0xBABE8)) mult *= 1.2f;   // Barbarian
			if (p.skill == kArchery && HasPerk(m, 0xBABED)) mult *= 1.2f;     // Overdraw
			if (p.skill == kArchery && m.arrows <= 0) { base = 3.f; mult = 1.f; }   // out of arrows: fists
			p.dmg = base * mult;
			if (w && p.skill != kArchery) p.dmg *= std::clamp(w->GetSpeed(), 0.5f, 1.3f);
		}
		p.hitBase = 0.55f + std::min(0.3f, m.skills[p.skill] / 400.f);
		return p;
	}

}

int MainCombatSkill(const Member& m)
{
	if (auto* w = MainWeapon(m)) {
		if (w->IsBow() || w->IsCrossbow()) return kArchery;
		auto t = w->GetWeaponType();
		if (t == RE::WEAPON_TYPE::kTwoHandSword || t == RE::WEAPON_TYPE::kTwoHandAxe) return kTwoHanded;
		if (t == RE::WEAPON_TYPE::kHandToHandMelee) {
			return m.skills[kDestruction] > m.skills[kOneHanded] ? int(kDestruction) : int(kOneHanded);
		}
		return kOneHanded;
	}
	// no weapon: mages use Destruction, everyone else brawls with One-Handed
	if (!m.spells.empty() && m.skills[kDestruction] >= m.skills[kOneHanded]) return kDestruction;
	return kOneHanded;
}

float MemberStrength(const Member& m)
{
	if (!m.alive()) return 0.f;
	auto p = Build(m);
	float ehp = p.hp / (1.f - p.reduction);
	return std::sqrt(std::max(1.f, p.dmg * p.hitBase) * ehp) * 10.f;
}

// Health-independent fighting value (the board scales it by current health): Anomaly's NPC "rank".
float MemberPower(const Member& m)
{
	Member full = m;
	full.health = std::max(1.f, m.healthMax);
	auto p = Build(full);
	const float ehp = p.hp / (1.f - p.reduction);
	return std::sqrt(std::max(1.f, p.dmg * p.hitBase) * ehp) * 10.f;
}

float GroupStrength(const Group& g)
{
	float s = 0;
	for (auto& m : g.members) s += MemberStrength(m);
	return s;
}
}
