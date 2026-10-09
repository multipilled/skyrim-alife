#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Systems.h"

// Gear, loadouts, loot and growth. Everything is derived from the game's own leveled lists and outfits.
namespace ALife::Economy {
namespace {
	// ---- vanilla leveled lists / outfits (Skyrim.esm; FormIDs verified against the user's Skyrim.esm with tools/)
	namespace L {
		constexpr RE::FormID BanditWeapon1H = 0x37C1B, BanditWeapon2H = 0x37C21, BanditShield50 = 0x37C1C,
							 BanditBow = 0x39D2E, BanditArrows = 0x39D2F, BanditBossWeapon1H = 0x3DF1C, BanditBossWeapon2H = 0x3DEDA;
		constexpr RE::FormID OtftBanditHeavy = 0x15BE6, OtftBanditShield = 0xC0197, OtftBanditNoShield = 0x1E608,
							 OtftBanditMissile = 0x37C4D, OtftBanditBoss = 0x3DED3, OtftBanditMage = 0x6E26F;
		constexpr RE::FormID OtftHunter[4] = { 0x106988, 0x106989, 0x10698A, 0x10698B };
		constexpr RE::FormID HunterBow = 0x10D9D2, ArrowsAll = 0x68839, WeaponDagger = 0x17178;
		constexpr RE::FormID CuirassHeavy = 0x166F8, BootsHeavy = 0x1673A, GauntletsHeavy = 0x1673D, HelmetHeavy = 0x16740, ShieldHeavy = 0x165C2;
		constexpr RE::FormID CuirassLight = 0x16705, BootsLight = 0x1673B, GauntletsLight = 0x1673E, HelmetLight = 0x16741, ShieldLight = 0x165E3;
		constexpr RE::FormID WeaponSword = 0x16578, WeaponWarAxe = 0x16579, WeaponMace = 0x16577, WeaponGreatSword = 0x1657A,
							 WeaponBattleAxe = 0x1657C, WeaponWarhammer = 0x165B8, WeaponBow = 0x17024;
		constexpr RE::FormID OtftMageDestruction = 0xB7A3F, OtftMageConjuration = 0xB7A3E, OtftMageRobes = 0xE9AC4, OtftBattlemage = 0x1034EF;
		constexpr RE::FormID PotionRestoreHealth = 0x3A16A, FoodInnCommon = 0x98E5F;
		// faction outfits
		constexpr RE::FormID OtftImperialSoldier = 0x964DF, OtftImperialLight = 0xD29A0, OtftImperialArcher = 0xE108F, OtftImperialOfficer = 0xABF44,
							 OtftImperialMage = 0xF3E7D, OtftImperialHeavy = 0xAD23C;
		constexpr RE::FormID OtftSonsSoldier = 0xABF55, OtftSonsOfficer = 0xABF57, OtftStormcloak = 0xAD244;
		constexpr RE::FormID OtftThalmor = 0x39F22, OtftThalmorHelm = 0x2B0FB;
		constexpr RE::FormID OtftForswornMelee = 0x43BD9, OtftForswornMissile = 0x43BDA, OtftForswornMagic = 0x43BDB, OtftForswornBoss = 0x44314;
		constexpr RE::FormID OtftNecromancer = 0x9EF86, OtftWarlock = 0x44CD5;
		constexpr RE::FormID OtftVampire = 0x33840, OtftVampireBoss = 0x2E1E9;
		constexpr RE::FormID OtftVigilant = 0xB3290, OtftVigilantNoHood = 0xC7891;
		constexpr RE::FormID OtftOrcishNoHelm = 0x57A1B, OtftImperialStudded = 0x57A28;
	}

	struct PerkReq { RE::FormID perk; int skill; float min; };
	constexpr PerkReq kPerks[] = {
		{ 0xBABE4, kOneHanded, 20 }, { 0xBABE8, kTwoHanded, 20 }, { 0xBABED, kArchery, 20 }, { 0xBE123, kLightArmor, 20 },
		{ 0xBCD2A, kHeavyArmor, 20 }, { 0xBCCAE, kBlock, 20 }, { 0x58F61, kArchery, 30 }, { 0x58F62, kArchery, 50 },
		{ 0x105F19, kArchery, 70 }, { 0x52D50, kOneHanded, 40 }, { 0x52D51, kTwoHanded, 40 }, { 0x581F9, kRestoration, 40 },
		{ 0x58F6F, kHeavyArmor, 30 }, { 0x51B17, kHeavyArmor, 70 }, { 0x51B1B, kLightArmor, 30 }, { 0x58F6D, kHeavyArmor, 70 },
		{ 0xD8C33, kBlock, 30 }, { 0x58F69, kBlock, 50 }, { 0x58F68, kBlock, 30 }, { 0xF3933, kDestruction, 90 }, { 0xF3F0E, kDestruction, 90 },
	};
	struct SpellReq { RE::FormID spell; int skill; float min; bool attack; };
	constexpr SpellReq kSpells[] = {
		{ 0x12FCD, kDestruction, 0, true }, { 0x2DD2A, kDestruction, 0, true }, { 0x2B96B, kDestruction, 0, true },
		{ 0x12FD0, kDestruction, 25, true }, { 0x2B96C, kDestruction, 25, true }, { 0x2DD29, kDestruction, 25, true },
		{ 0x1C789, kDestruction, 50, true }, { 0x45F9D, kDestruction, 50, true }, { 0x45F9C, kDestruction, 50, true },
		{ 0x10F7ED, kDestruction, 75, true }, { 0x10F7EE, kDestruction, 75, true }, { 0x10F7EC, kDestruction, 75, true },
		{ 0x7A82B, kDestruction, 100, true }, { 0x7E8E5, kDestruction, 100, true },
		{ 0x12FCC, kRestoration, 0, false }, { 0x2F3B8, kRestoration, 25, false }, { 0xB62EF, kRestoration, 50, false }, { 0x12FD2, kRestoration, 25, false },
		{ 0x5AD5C, kAlteration, 0, false }, { 0x5AD5D, kAlteration, 25, false }, { 0x51B16, kAlteration, 50, false },
		{ 0x640B6, kConjuration, 0, false }, { 0x204C3, kConjuration, 25, false },
	};

	RE::TESForm* F(RE::FormID id) { return RE::TESForm::LookupByID(id); }
	RE::BGSKeyword* Kw(RE::FormID id) { return RE::TESForm::LookupByID<RE::BGSKeyword>(id); }

	// Deterministic leveled-list walk (vanilla rules, our seeded RNG).
	void Resolve(RE::TESForm* a_form, int a_level, int a_count, Rng& rng, std::vector<Item>& out, int depth = 0)
	{
		if (!a_form || depth > 8 || a_count <= 0) return;
		auto* lvl = a_form->As<RE::TESLevItem>();
		if (!lvl) {
			if (a_form->IsBoundObject()) {
				for (auto& it : out) if (it.form == a_form->GetFormID()) { it.count += a_count; return; }
				out.push_back({ a_form->GetFormID(), a_count });
			}
			return;
		}
		auto& ll = *static_cast<RE::TESLeveledList*>(lvl);
		const auto fl = std::uint8_t(ll.llFlags);
		const bool all = (fl & RE::TESLeveledList::Flag::kUseAll) != 0;
		const bool fromAll = (fl & RE::TESLeveledList::Flag::kCalculateFromAllLevelsLTOrEqPCLevel) != 0;
		const bool each = (fl & RE::TESLeveledList::Flag::kCalculateForEachItemInCount) != 0;
		const int rolls = each ? a_count : 1;
		const int mult = each ? 1 : a_count;
		for (int r = 0; r < rolls; ++r) {
			if (rng.index(100) < int(ll.GetChanceNone())) continue;
			std::vector<const RE::LEVELED_OBJECT*> cands;
			int top = -1;
			for (auto& e : ll.entries) if (e.level <= a_level) top = std::max(top, int(e.level));
			for (auto& e : ll.entries) {
				if (!e.form || e.level > a_level) continue;
				if (all || fromAll || int(e.level) == top) cands.push_back(&e);
			}
			if (cands.empty()) continue;
			if (all) {
				for (auto* e : cands) Resolve(e->form, a_level, e->count * mult, rng, out, depth + 1);
			} else {
				auto* e = cands[rng.index(int(cands.size()))];
				Resolve(e->form, a_level, e->count * mult, rng, out, depth + 1);
			}
		}
	}
	void ResolveOutfit(RE::FormID a_otft, int a_level, Rng& rng, std::vector<Item>& out)
	{
		auto* o = RE::TESForm::LookupByID<RE::BGSOutfit>(a_otft);
		if (!o) return;
		for (auto* f : o->outfitItems) Resolve(f, a_level, 1, rng, out);
	}
	void Add(Member& m, RE::FormID id, int n)
	{
		for (auto& it : m.inventory) if (it.form == id) { it.count += n; return; }
		m.inventory.push_back({ id, n });
	}
	bool Remove(Member& m, RE::FormID id, int n)
	{
		for (auto it = m.inventory.begin(); it != m.inventory.end(); ++it)
			if (it->form == id) {
				if (it->count < n) return false;
				it->count -= n;
				if (it->count == 0) {
					m.inventory.erase(it);
					m.equipped.erase(std::remove(m.equipped.begin(), m.equipped.end(), id), m.equipped.end());
				}
				return true;
			}
		return false;
	}

	enum class Kit { Melee1H, Melee2H, MeleeShield, Archer, Mage, Spellsword, Boss };
	Kit KitOf(const Member& m)
	{
		using namespace Forms::Vanilla;
		switch (m.classId) {
		case ClassBanditMissile: case ClassRanger: case ClassScout: return Kit::Archer;
		case ClassBanditWizard: return Kit::Mage;
		case ClassWarrior2H: return Kit::Melee2H;
		case ClassSpellsword: return Kit::Spellsword;
		default: return Kit::Melee1H;
		}
	}
	bool IsArmorType(RE::TESObjectARMO* a, bool heavy) { return heavy ? a->IsHeavyArmor() : !a->IsHeavyArmor(); }

	// Re-derive what is equipped: best weapon for the main skill, best armor per biped slot of the class's armor weight.
	void Reequip(Member& m)
	{
		m.equipped.clear();
		const Kit k = KitOf(m);
		const bool wantsHeavy = m.skills[kHeavyArmor] > m.skills[kLightArmor];
		const bool mageRobes = (k == Kit::Mage);
		RE::TESObjectWEAP* bestW = nullptr;
		RE::TESObjectARMO* bestShield = nullptr;
		std::unordered_map<std::uint32_t, RE::TESObjectARMO*> bySlot;
		m.arrows = 0;
		for (auto& it : m.inventory) {
			auto* f = F(it.form);
			if (!f) continue;
			if (f->Is(RE::FormType::Ammo)) { if (k == Kit::Archer) m.arrows += it.count; continue; }
			if (auto* w = f->As<RE::TESObjectWEAP>()) {
				const bool bow = w->IsBow() || w->IsCrossbow();
				const auto t = w->GetWeaponType();
				const bool twoH = t == RE::WEAPON_TYPE::kTwoHandSword || t == RE::WEAPON_TYPE::kTwoHandAxe;
				bool fits = false;
				switch (k) {
				case Kit::Archer: fits = bow; break;
				case Kit::Melee2H: fits = twoH; break;
				case Kit::Mage: fits = false; break;
				default: fits = !bow && !twoH && !w->IsStaff(); break;
				}
				if (fits && (!bestW || w->GetAttackDamage() > bestW->GetAttackDamage())) bestW = w;
				continue;
			}
			if (auto* a = f->As<RE::TESObjectARMO>()) {
				if (a->IsShield()) {
					if ((k == Kit::Melee1H || k == Kit::MeleeShield || k == Kit::Boss) && (!bestShield || a->armorRating > bestShield->armorRating)) bestShield = a;
					continue;
				}
				bool ok = mageRobes ? a->armorRating == 0 : (a->armorRating == 0 || IsArmorType(a, wantsHeavy) || !a->IsHeavyArmor());
				if (!ok) continue;
				auto mask = a->GetSlotMask().underlying();
				auto& cur = bySlot[mask];
				if (!cur || a->armorRating > cur->armorRating) cur = a;
			}
		}
		if (bestW) m.equipped.push_back(bestW->GetFormID());
		if (bestShield) m.equipped.push_back(bestShield->GetFormID());
		std::uint32_t used = 0;
		std::vector<RE::TESObjectARMO*> armors;
		for (auto& [mask, a] : bySlot) armors.push_back(a);
		std::sort(armors.begin(), armors.end(), [](auto* a, auto* b) { return a->armorRating > b->armorRating; });
		for (auto* a : armors) {
			auto mask = a->GetSlotMask().underlying();
			if (mask & used) continue;
			used |= mask;
			m.equipped.push_back(a->GetFormID());
		}
	}

	void GrantPerksAndSpells(Member& m)
	{
		const auto& r = Forms::R();
		for (auto& p : kPerks) {
			if (m.skills[p.skill] < p.min) continue;
			if (r.workingPerks && !r.workingPerks->HasForm(p.perk)) continue;
			if (std::find(m.perks.begin(), m.perks.end(), p.perk) == m.perks.end()) m.perks.push_back(p.perk);
		}
		const Kit k = KitOf(m);
		if (k != Kit::Mage && k != Kit::Spellsword) return;
		std::vector<const SpellReq*> attack;
		for (auto& s : kSpells) {
			if (m.skills[s.skill] < s.min) continue;
			if (r.castableSpells && !r.castableSpells->HasForm(s.spell)) continue;
			if (s.attack) attack.push_back(&s);
			else if (std::find(m.spells.begin(), m.spells.end(), s.spell) == m.spells.end() &&
					 (s.skill == kRestoration || k == Kit::Mage)) m.spells.push_back(s.spell);
		}
		std::sort(attack.begin(), attack.end(), [](auto* a, auto* b) { return a->min > b->min; });
		for (std::size_t i = 0; i < attack.size() && i < 3; ++i)
			if (std::find(m.spells.begin(), m.spells.end(), attack[i]->spell) == m.spells.end()) m.spells.push_back(attack[i]->spell);
	}

	bool IsArtifact(RE::FormID id)
	{
		const auto& r = Forms::R();
		return r.daedricArtifacts && r.daedricArtifacts->HasForm(id);
	}
	bool IsFoodForm(RE::TESForm* f)
	{
		auto* al = f ? f->As<RE::AlchemyItem>() : nullptr;
		return al && al->IsFood();
	}
	bool IsHealPotion(RE::TESForm* f)
	{
		auto* al = f ? f->As<RE::AlchemyItem>() : nullptr;
		if (!al || al->IsFood() || al->IsPoison()) return false;
		auto* kw = Kw(Forms::Vanilla::KwMagicAlchRestoreHealth);
		if (kw && al->HasKeyword(kw)) return true;
		auto* e = al->GetCostliestEffectItem();
		return e && e->baseEffect && e->baseEffect->data.primaryAV == RE::ActorValue::kHealth && !e->baseEffect->IsDetrimental();
	}

	bool WantsItem(const Member& m, RE::TESBoundObject* a_obj)
	{
		if (!a_obj) return false;
		if (a_obj == Forms::R().gold) return true;
		if (IsArtifact(a_obj->GetFormID())) return true;
		const Kit k = KitOf(m);
		if (a_obj->Is(RE::FormType::Ammo)) return k == Kit::Archer;
		if (auto* w = a_obj->As<RE::TESObjectWEAP>()) {
			const bool bow = w->IsBow() || w->IsCrossbow();
			const auto t = w->GetWeaponType();
			const bool twoH = t == RE::WEAPON_TYPE::kTwoHandSword || t == RE::WEAPON_TYPE::kTwoHandAxe;
			const bool fits = (k == Kit::Archer) ? bow : (k == Kit::Melee2H) ? twoH : (k == Kit::Mage) ? false : (!bow && !twoH && !w->IsStaff());
			auto* cur = RE::TESForm::LookupByID<RE::TESObjectWEAP>(m.equipped.empty() ? 0 : m.equipped.front());
			return fits && (!cur || w->GetAttackDamage() > cur->GetAttackDamage());
		}
		if (auto* a = a_obj->As<RE::TESObjectARMO>()) {
			const bool wantsHeavy = m.skills[kHeavyArmor] > m.skills[kLightArmor];
			if (k == Kit::Mage && a->armorRating > 0) return false;
			if (a->IsHeavyArmor() != wantsHeavy && a->armorRating > 0) return false;
			for (auto id : m.equipped)
				if (auto* e = RE::TESForm::LookupByID<RE::TESObjectARMO>(id))
					if ((e->GetSlotMask().underlying() & a->GetSlotMask().underlying()) && e->armorRating >= a->armorRating) return false;
			return true;
		}
		if (a_obj->Is(RE::FormType::AlchemyItem)) return true;
		return false;
	}

	// Which vanilla outfit a faction / role wears.
	RE::FormID FactionOutfit(const Member& m, Kit k, Rng& rng, bool leader)
	{
		switch (m.faction) {
		case Faction::Imperial:
			if (leader) return L::OtftImperialOfficer;
			if (k == Kit::Mage) return L::OtftImperialMage;
			if (k == Kit::Archer) return L::OtftImperialArcher;
			return rng.chance(0.5f) ? L::OtftImperialSoldier : (rng.chance(0.5f) ? L::OtftImperialLight : L::OtftImperialHeavy);
		case Faction::Stormcloak:
			if (leader) return L::OtftSonsOfficer;
			return rng.chance(0.7f) ? L::OtftSonsSoldier : L::OtftStormcloak;
		case Faction::Thalmor: return (k == Kit::Mage || rng.chance(0.5f)) ? L::OtftThalmor : L::OtftThalmorHelm;
		case Faction::Forsworn:
			if (leader) return L::OtftForswornBoss;
			return k == Kit::Mage ? L::OtftForswornMagic : k == Kit::Archer ? L::OtftForswornMissile : L::OtftForswornMelee;
		case Faction::Necromancer: return rng.chance(0.4f) ? L::OtftWarlock : L::OtftNecromancer;
		case Faction::Vampire: return leader ? L::OtftVampireBoss : L::OtftVampire;
		case Faction::Vigilant: return rng.chance(0.5f) ? L::OtftVigilant : L::OtftVigilantNoHood;
		case Faction::Mage:
			return k == Kit::Spellsword ? L::OtftBattlemage : rng.chance(0.5f) ? L::OtftMageDestruction : (rng.chance(0.5f) ? L::OtftMageConjuration : L::OtftMageRobes);
		case Faction::Mercenary: return rng.chance(0.6f) ? L::OtftOrcishNoHelm : L::OtftImperialStudded;
		case Faction::Dawnguard: return rng.chance(0.5f) ? L::OtftImperialStudded : L::OtftOrcishNoHelm;
		default: return 0;
		}
	}
}

float CarryLimit(const Member& m) { return 200.f + m.staminaMax * 0.5f; }
float Carried(const Member& m)
{
	float w = 0;
	for (auto& it : m.inventory) if (auto* f = F(it.form)) w += f->GetWeight() * float(it.count);
	return w;
}

// Vanilla leveled outfits can come back empty at low levels (a level-1 bandit mage rolled nothing and went into battle in a
// loincloth). Whatever the lists gave, every member leaves with a body piece, boots and a weapon that fits the kit.
static void EnsureGear(Member& m, int a_kit, Rng& rng)
{
	const Kit k = Kit(a_kit);
	// what the member would actually wear / wield after the normal equip pass
	auto survey = [&](bool& body, bool& feet, bool& weapon) {
		body = feet = weapon = false;
		for (auto id : m.equipped) {
			auto* f = F(id);
			if (!f) continue;
			if (auto* a = f->As<RE::TESObjectARMO>()) {
				const auto mask = a->GetSlotMask().underlying();
				if (mask & 0x4) body = true;       // kBody (slot 32)
				if (mask & 0x80) feet = true;      // kFeet (slot 37)
			} else if (f->As<RE::TESObjectWEAP>()) {
				weapon = true;
			}
		}
	};
	bool body, feet, weapon;
	survey(body, feet, weapon);
	if (body && feet && (weapon || k == Kit::Mage)) return;
	std::vector<Item> got;
	const int lvl = std::max(1, m.level);
	if (!body) {
		if (k == Kit::Mage) ResolveOutfit(L::OtftMageRobes, lvl, rng, got);
		if (got.empty()) {
			const bool heavy = k == Kit::Melee1H || k == Kit::Melee2H;
			Resolve(F(heavy ? L::CuirassHeavy : L::CuirassLight), lvl, 1, rng, got);
		}
	}
	if (!feet) Resolve(F(L::BootsLight), lvl, 1, rng, got);
	if (!weapon && k != Kit::Mage) {
		if (k == Kit::Archer) { Resolve(F(L::WeaponBow), lvl, 1, rng, got); Resolve(F(L::ArrowsAll), lvl, 2, rng, got); }
		else Resolve(F(k == Kit::Melee2H ? L::WeaponGreatSword : L::WeaponSword), lvl, 1, rng, got);
	}
	for (auto& it : got) Add(m, it.form, it.count);
	Reequip(m);
	// a mage kit only wears robes: if that still left the body bare, give the member light armour and let it wear it
	survey(body, feet, weapon);
	if (!body) {
		got.clear();
		Resolve(F(L::CuirassLight), lvl, 1, rng, got);
		for (auto& it : got) Add(m, it.form, it.count);
		m.classId = Forms::Vanilla::ClassSpellsword;   // armoured caster: the class decides what the equip pass accepts
		Reequip(m);
	}
}

// The wares of a Khajiit caravan, drawn from vanilla's own leveled lists: the merchant carries the stock, the guards a little to sell on.
static void CaravanStock(Member& m, int lvl, bool leader, Rng& rng, std::vector<Item>& got)
{
	const int mult = leader ? 3 : 1;
	auto put = [&](RE::FormID list, int times) { for (int i = 0; i < times * mult; ++i) Resolve(F(list), lvl, 1, rng, got); };
	put(0x10962C, 1);   // LItemSkooma75
	put(0x10962D, 2);   // LItemMoonsugar75
	put(0x3A16A, 1);    // LItemPotionRestoreHealth
	put(0x3A171, 1);    // LItemPotionRestoreMagicka
	put(0x3A173, 1);    // LItemPotionRestoreStamina
	put(0xE3E99, 1);    // LItemPotionAll
	put(0x65A8A, 1);    // LItemPoisonAll
	put(0x87131, 3);    // LItemIngredientsCommon
	put(0x87133, 1);    // LItemIngredientsUncommon
	put(0xA44CC, 1);    // LItemScrolls
	put(0x98E5F, 2);    // LItemFoodInnCommon
	put(0x68525, 1);    // LItemGems
	m.gold = (leader ? 400 : 60) + rng.index((leader ? 700 : 120) + m.level * (leader ? 12 : 3));
}

void OutfitNew(Member& m)
{
	using namespace Forms::Vanilla;
	Rng rng(Mix64(m.id * 7919ULL + std::uint64_t(m.level)));
	m.inventory.clear(); m.equipped.clear(); m.perks.clear(); m.spells.clear();
	m.skills.fill(5.f + float(m.level) * 0.5f);
	auto prim = [&](int s) { m.skills[s] = std::min(100.f, 15.f + float(m.level) * 2.5f); };
	auto sec = [&](int s) { m.skills[s] = std::min(100.f, 15.f + float(m.level) * 1.5f); };
	const bool leader = m.role == Role::Leader;
	const bool bandit = m.faction == Faction::Bandit || m.faction == Faction::SilverHand;

	// role -> combat class (the class drives the kit, the skills and the AI's combat style)
	switch (m.role) {
	case Role::Archer: m.classId = bandit ? ClassBanditMissile : ClassRanger; break;
	case Role::Mage: m.classId = ClassBanditWizard; break;
	case Role::Spellsword: m.classId = ClassSpellsword; break;
	case Role::Heavy: m.classId = rng.chance(0.45f) ? ClassWarrior2H : (bandit ? ClassBanditMelee : ClassWarrior1H); break;
	case Role::Leader: m.classId = rng.chance(0.4f) ? ClassWarrior2H : (bandit ? ClassBanditMelee : ClassWarrior1H); break;
	default: m.classId = bandit ? ClassBanditMelee : ClassWarrior1H; break;
	}
	if (m.faction == Faction::Hunter) m.classId = ClassRanger;
	const Kit k = KitOf(m);
	const int lvl = std::min(m.level, m.level + 4);
	float hpL = 8, mpL = 3, spL = 7;
	switch (k) {
	case Kit::Archer: prim(kArchery); sec(kLightArmor); sec(kSneak); sec(kOneHanded); hpL = 7; mpL = 3; spL = 10; break;
	case Kit::Mage: prim(kDestruction); sec(kRestoration); sec(kAlteration); sec(kConjuration); hpL = 5; mpL = 13; spL = 2; break;
	case Kit::Spellsword: prim(kOneHanded); prim(kDestruction); sec(kRestoration); sec(kLightArmor); sec(kBlock); hpL = 7; mpL = 8; spL = 5; break;
	case Kit::Melee2H: prim(kTwoHanded); sec(kHeavyArmor); sec(kSmithing); hpL = 10; mpL = 1; spL = 9; break;
	default: prim(kOneHanded); prim(kBlock); sec(kHeavyArmor); hpL = 10; mpL = 2; spL = 8; break;
	}
	switch (m.race) {   // vanilla racial skill bonuses
	case RaceId::Nord: case RaceId::NordVampire: m.skills[kTwoHanded] += 10; break;
	case RaceId::Imperial: case RaceId::ImperialVampire: m.skills[kRestoration] += 10; break;
	case RaceId::Breton: case RaceId::BretonVampire: m.skills[kConjuration] += 10; break;
	case RaceId::Redguard: m.skills[kOneHanded] += 10; break;
	case RaceId::Altmer: m.skills[kDestruction] += 10; break;
	case RaceId::Bosmer: m.skills[kArchery] += 10; break;
	case RaceId::Dunmer: case RaceId::DunmerVampire: m.skills[kDestruction] += 10; break;
	case RaceId::Orsimer: m.skills[kHeavyArmor] += 10; break;
	case RaceId::Argonian: m.skills[kLockpicking] += 10; break;
	case RaceId::Khajiit: m.skills[kSneak] += 10; break;
	default: break;
	}
	for (auto& s : m.skills) s = std::min(100.f, s);
	const float baseMp = (m.race == RaceId::Altmer) ? 100.f : 50.f;
	m.healthMax = 50.f + hpL * float(m.level);
	m.magickaMax = baseMp + mpL * float(m.level);
	m.staminaMax = 50.f + spL * float(m.level);
	m.health = m.healthMax; m.magicka = m.magickaMax; m.stamina = m.staminaMax;

	// gear from vanilla lists, level-capped
	std::vector<Item> got;
	if (bandit) {
		if (leader && m.level >= 6) {
			ResolveOutfit(L::OtftBanditBoss, lvl, rng, got);
			Resolve(F(k == Kit::Melee2H ? L::BanditBossWeapon2H : L::BanditBossWeapon1H), lvl, 1, rng, got);
		} else if (k == Kit::Archer) {
			ResolveOutfit(L::OtftBanditMissile, lvl, rng, got);
			Resolve(F(L::BanditBow), lvl, 1, rng, got);
			Resolve(F(L::BanditArrows), lvl, 1, rng, got);
			Resolve(F(L::BanditWeapon1H), lvl, 1, rng, got);
		} else if (k == Kit::Mage) {
			ResolveOutfit(L::OtftBanditMage, lvl, rng, got);
		} else {
			int o = rng.index(3);
			ResolveOutfit(o == 0 ? L::OtftBanditHeavy : o == 1 ? L::OtftBanditShield : L::OtftBanditNoShield, lvl, rng, got);
			Resolve(F(rng.chance(0.3f) || k == Kit::Melee2H ? L::BanditWeapon2H : L::BanditWeapon1H), lvl, 1, rng, got);
			if (o != 2 && k != Kit::Melee2H) Resolve(F(L::BanditShield50), lvl, 1, rng, got);
		}
		m.gold = 10 + rng.index(15 + m.level * 4);
	} else if (m.faction == Faction::Hunter) {
		ResolveOutfit(L::OtftHunter[rng.index(4)], lvl, rng, got);
		Resolve(F(L::HunterBow), lvl, 1, rng, got);
		Resolve(F(L::ArrowsAll), lvl, 2, rng, got);
		Resolve(F(L::WeaponDagger), lvl, 1, rng, got);
		m.gold = 20 + rng.index(40 + m.level * 3);
	} else {
		const RE::FormID otft = FactionOutfit(m, k, rng, leader);
		const bool heavy = (k == Kit::Melee1H || k == Kit::Melee2H);
		if (otft) {
			ResolveOutfit(otft, lvl, rng, got);
		} else if (k == Kit::Mage) {
			ResolveOutfit(L::OtftMageDestruction, lvl, rng, got);
		} else {   // wanderers: plain armour by weight class
			for (auto id : heavy ? std::array<RE::FormID, 4>{ L::CuirassHeavy, L::BootsHeavy, L::GauntletsHeavy, L::HelmetHeavy }
								 : std::array<RE::FormID, 4>{ L::CuirassLight, L::BootsLight, L::GauntletsLight, L::HelmetLight })
				Resolve(F(id), lvl, 1, rng, got);
		}
		if (k == Kit::Archer) { Resolve(F(L::WeaponBow), lvl, 1, rng, got); Resolve(F(L::ArrowsAll), lvl, 2, rng, got); Resolve(F(L::WeaponDagger), lvl, 1, rng, got); }
		else if (k == Kit::Melee2H) Resolve(F(rng.chance(0.5f) ? L::WeaponGreatSword : (rng.chance(0.5f) ? L::WeaponBattleAxe : L::WeaponWarhammer)), lvl, 1, rng, got);
		else if (k != Kit::Mage) {
			RE::FormID w[] = { L::WeaponSword, L::WeaponWarAxe, L::WeaponMace };
			Resolve(F(w[rng.index(3)]), lvl, 1, rng, got);
			if (k == Kit::Melee1H) Resolve(F(heavy ? L::ShieldHeavy : L::ShieldLight), lvl, 1, rng, got);
		}
		Resolve(F(L::PotionRestoreHealth), lvl, 2, rng, got);
		m.gold = 50 + rng.index(100 + m.level * 8);
	}
	Resolve(F(L::FoodInnCommon), lvl, 1, rng, got);
	if (m.faction == Faction::Caravan) CaravanStock(m, lvl, leader, rng, got);
	for (auto& it : got) Add(m, it.form, it.count);
	Reequip(m);
	EnsureGear(m, int(k), rng);
	GrantPerksAndSpells(m);
	Reequip(m);
}

void RegrowPerks(Member& m) { GrantPerksAndSpells(m); }

// The skill curve OutfitNew rolls for a member of this kit at its level: the floor a levelled-up member's skills are raised to.
void SkillFloor(const Member& m, std::array<float, kSkillCount>& out)
{
	const Kit k = KitOf(m);
	out.fill(5.f + float(m.level) * 0.5f);
	auto prim = [&](int s) { out[s] = std::min(100.f, 15.f + float(m.level) * 2.5f); };
	auto sec = [&](int s) { out[s] = std::min(100.f, 15.f + float(m.level) * 1.5f); };
	switch (k) {
	case Kit::Archer: prim(kArchery); sec(kLightArmor); sec(kSneak); sec(kOneHanded); break;
	case Kit::Mage: prim(kDestruction); sec(kRestoration); sec(kAlteration); sec(kConjuration); break;
	case Kit::Spellsword: prim(kOneHanded); prim(kDestruction); sec(kRestoration); sec(kLightArmor); sec(kBlock); break;
	case Kit::Melee2H: prim(kTwoHanded); sec(kHeavyArmor); sec(kSmithing); break;
	default: prim(kOneHanded); prim(kBlock); sec(kHeavyArmor); break;
	}
}

// A member who has grown far past the level its kit was rolled at is re-equipped for its level; what it earned (gold, loot that is not
// kit, skills, how hurt or hungry it is) stays.
void Regear(Member& m)
{
	const int gold = m.gold;
	const auto skills = m.skills;
	const float hurt = m.healthMax > 0.f ? m.health / m.healthMax : 1.f;
	const float hunger = m.hunger, cold = m.cold, fatigue = m.fatigue;
	std::vector<Item> keep;
	for (auto& it : m.inventory) {
		auto* f = F(it.form);
		if (f && !f->Is(RE::FormType::Weapon) && !f->Is(RE::FormType::Armor) && !f->Is(RE::FormType::Ammo)) keep.push_back(it);
	}
	OutfitNew(m);
	for (auto& it : keep) Add(m, it.form, it.count);
	m.gold = std::max(gold, m.gold);
	for (int s = 0; s < kSkillCount; ++s) m.skills[s] = std::max(m.skills[s], skills[s]);
	m.health = std::max(1.f, m.healthMax * hurt);
	m.hunger = hunger; m.cold = cold; m.fatigue = fatigue;
	Reequip(m);
}

void ScaleToLevel(Member& m, int target)
{
	if (target <= m.level) return;
	while (m.level < target) LevelUp(m);
	std::array<float, kSkillCount> floor{};
	SkillFloor(m, floor);
	for (int s = 0; s < kSkillCount; ++s) m.skills[s] = std::max(m.skills[s], floor[s]);
	if (m.level - m.gearLevel >= Sim::Cfg().regearGap) {
		Regear(m);
		m.gearLevel = m.level;
	}
}

void LootBodies(Group& a_win, std::vector<Member>& a_dead)
{
	std::vector<Member*> takers;
	for (auto& m : a_win.members) if (m.alive()) takers.push_back(&m);
	if (takers.empty()) return;
	for (auto& d : a_dead) {
		for (auto* t : takers) t->gold += d.gold / int(takers.size());
		d.gold = 0;
		std::sort(d.inventory.begin(), d.inventory.end(), [](const Item& a, const Item& b) {
			auto* fa = F(a.form); auto* fb = F(b.form);
			return (fa ? fa->GetGoldValue() : 0) > (fb ? fb->GetGoldValue() : 0);
		});
		for (auto& it : d.inventory) {
			auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(it.form);
			if (!obj) continue;
			for (auto* t : takers) {
				if (!WantsItem(*t, obj)) continue;
				if (Carried(*t) + obj->GetWeight() * float(it.count) > CarryLimit(*t)) continue;
				Add(*t, it.form, it.count);
				break;
			}
		}
		d.inventory.clear();
	}
	for (auto* t : takers) Reequip(*t);
}

int HealPotions(const Member& m)
{
	int n = 0;
	for (auto& it : m.inventory) if (IsHealPotion(F(it.form))) n += it.count;
	return n;
}
bool DrinkHealPotion(Member& m)
{
	for (auto& it : m.inventory)
		if (IsHealPotion(F(it.form))) {
			RE::FormID id = it.form;
			Remove(m, id, 1);
			m.health = std::min(m.healthMax, m.health + 50.f);
			return true;
		}
	return false;
}

void ApplyLoadoutTo(RE::Actor* a, const Member& m)
{
	if (!a) return;
	for (auto& it : m.inventory)
		if (auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(it.form)) a->AddObjectToContainer(obj, nullptr, it.count, nullptr);
	if (m.gold > 0 && Forms::R().gold) a->AddObjectToContainer(Forms::R().gold, nullptr, m.gold, nullptr);
	auto* eq = RE::ActorEquipManager::GetSingleton();
	for (auto id : m.equipped)
		if (auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(id)) eq->EquipObject(a, obj, nullptr, 1, nullptr, true, false, false, false);
	if (m.arrows > 0) {
		for (auto& it : m.inventory)
			if (auto* f = F(it.form); f && f->Is(RE::FormType::Ammo)) {
				eq->EquipObject(a, f->As<RE::TESBoundObject>(), nullptr, it.count, nullptr, true, false, false, false);
				break;
			}
	}
	for (auto id : m.spells) if (auto* sp = RE::TESForm::LookupByID<RE::SpellItem>(id)) a->AddSpell(sp);
	Pool::ApplyPerks(a, m.perks);
}

void ReadBackFrom(RE::Actor* a, Member& m)
{
	if (!a) return;
	m.inventory.clear();
	m.gold = 0;
	auto inv = a->GetInventory();
	for (auto& [obj, data] : inv) {
		if (!obj || data.first <= 0) continue;
		if (obj == Forms::R().gold) { m.gold += data.first; continue; }
		if (obj->Is(RE::FormType::KeyMaster)) continue;
		Add(m, obj->GetFormID(), data.first);
	}
	if (auto* avo = a->AsActorValueOwner()) {
		m.health = std::max(0.f, avo->GetActorValue(RE::ActorValue::kHealth));
		m.magicka = std::max(0.f, avo->GetActorValue(RE::ActorValue::kMagicka));
		m.stamina = std::max(0.f, avo->GetActorValue(RE::ActorValue::kStamina));
		for (int s = 0; s < kSkillCount; ++s) m.skills[s] = std::max(m.skills[s], avo->GetBaseActorValue(SkillAV(s)));
	}
	if (a->IsDead()) m.health = 0;
	Reequip(m);
}

void Reequip_(Member& m) { Reequip(m); }

void LevelUp(Member& m)
{
	float hp = 8, mp = 3, sp = 7;
	switch (KitOf(m)) {
	case Kit::Archer: hp = 7; mp = 3; sp = 10; break;
	case Kit::Mage: hp = 5; mp = 13; sp = 2; break;
	case Kit::Spellsword: hp = 7; mp = 8; sp = 5; break;
	case Kit::Melee2H: hp = 10; mp = 1; sp = 9; break;
	default: hp = 10; mp = 2; sp = 8; break;
	}
	++m.level;
	m.healthMax += hp; m.magickaMax += mp; m.staminaMax += sp;
	m.health = std::min(m.healthMax, m.health + hp);
	GrantPerksAndSpells(m);
}

// Anomaly rank gain (damage / rank_fraction) -> main-skill points, capped per day so fighters improve but never explode.
void Grow(Member& m, float a_rankXp, Rng&)
{
	const std::uint32_t day = Sim::Day();
	if (m.growthDay != day) { m.growthDay = day; m.gainToday.fill(0); m.fightBonusToday = 0; }
	const float room = 4.f - m.fightBonusToday;
	if (room <= 0.f || a_rankXp <= 0.f) return;
	const float gain = std::min(room, a_rankXp * 0.5f);
	const int sk = Combat::MainCombatSkill(m);
	const float before = m.skills[sk];
	m.skills[sk] = std::min(100.f, before + gain);
	const float got = m.skills[sk] - before;
	m.fightBonusToday += got;
	if (int(m.skills[sk]) / 10 > int(before) / 10) LevelUp(m);
}

bool Wants(const Member& m, RE::TESBoundObject* a_obj) { return WantsItem(m, a_obj); }
bool IsFoodItem(RE::TESForm* a_f) { return IsFoodForm(a_f); }
bool IsHealPotionItem(RE::TESForm* a_f) { return IsHealPotion(a_f); }
bool IsArtifactItem(RE::FormID a_id) { return IsArtifact(a_id); }
bool IsArcher(const Member& m) { return KitOf(m) == Kit::Archer; }
int CountItem(const Member& m, RE::FormID a_id)
{
	for (auto& it : m.inventory) if (it.form == a_id) return it.count;
	return 0;
}
int FoodCount(const Member& m)
{
	int n = 0;
	for (auto& it : m.inventory) if (IsFoodForm(F(it.form))) n += it.count;
	return n;
}
int ArrowCount(const Member& m)
{
	int n = 0;
	for (auto& it : m.inventory) if (auto* f = F(it.form); f && f->Is(RE::FormType::Ammo)) n += it.count;
	return n;
}
void AddItem(Member& m, RE::FormID a_id, int a_n) { Add(m, a_id, a_n); }
bool RemoveItem(Member& m, RE::FormID a_id, int a_n) { return Remove(m, a_id, a_n); }

// Members eat when hungry (food or fish, whichever they carry) and drink a healing potion when badly hurt.
void Consume(Member& m)
{
	if (m.hunger >= 500.f) {
		for (auto& it : m.inventory) {
			if (IsFoodForm(F(it.form))) {
				const RE::FormID id = it.form;
				Remove(m, id, 1);
				m.hunger = std::max(0.f, m.hunger - 350.f);
				break;
			}
		}
	}
	if (m.alive() && m.health < m.healthMax * 0.4f) DrinkHealPotion(m);
}

// Loot from the raid-haul leveled list (gold, jewellery, gems, small goods) into one member's pack, respecting carry weight.
void Haul(Member& m, int a_level, Rng& rng)
{
	if (!Forms::R().raidHaul) return;
	std::vector<Item> got;
	Resolve(Forms::R().raidHaul, std::max(1, a_level), 1, rng, got);
	auto* gold = Forms::R().gold;
	for (auto& it : got) {
		auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(it.form);
		if (!obj) continue;
		if (obj == gold) { m.gold += it.count; continue; }
		if (Carried(m) + obj->GetWeight() * float(it.count) <= CarryLimit(m)) Add(m, it.form, it.count);
	}
}

// Raw meat and animal parts from a kill.
void HuntCatch(Member& m, Rng& rng)
{
	std::vector<Item> got;
	Resolve(F(0x10D9D3 /*FoodRawMeatHunter*/), m.level, 1, rng, got);
	Resolve(F(0x10D9D1 /*LItemMiscHunterAnimalParts75*/), m.level, 1, rng, got);
	for (auto& it : got) {
		auto* f = F(it.form);
		if (f && Carried(m) + f->GetWeight() * float(it.count) <= CarryLimit(m)) Add(m, it.form, it.count);
	}
}
}
