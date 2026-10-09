#include "ALife/Merchants.h"
#include "ALife/Forms.h"
#include "ALife/Log.h"
#include "ALife/Settings.h"

// Merchant economy. Squads that idle at a settlement sell their surplus loot to the vendors who would really buy it (each
// vendor's own buy/sell list and opening hours), buy what they need, and pay innkeepers for a bed. Everything moves through the
// vendor's real chest, so the player can buy what adventurers sold, and a vendor never drops below its gold floor.
namespace ALife::Merchants {
namespace {
	struct Merchant
	{
		std::vector<RE::FormID> factions;   // vendor factions that share this chest (opening hours and buy lists are unioned)
		RE::FormID chest{ 0 };
		std::uint32_t node{ 0 };
		State st;
	};
	std::vector<Merchant> gMerch;
	std::unordered_map<std::uint32_t, std::vector<int>> gByNode;
	std::vector<State> gPending;   // restored from the co-save before Init ran

	RE::TESObjectREFR* ChestOf(const Merchant& m) { return RE::TESForm::LookupByID<RE::TESObjectREFR>(m.chest); }

	std::int32_t ChestGold(RE::TESObjectREFR* c)
	{
		auto* gold = Forms::R().gold;
		if (!c || !gold) return 0;
		auto inv = c->GetInventory([&](RE::TESBoundObject& o) { return &o == gold; });
		for (auto& [o, d] : inv) return d.first;
		return 0;
	}

	bool OpenFaction(const RE::TESFaction* f, double a_now)
	{
		const auto& v = f->vendorData.vendorValues;
		const int h = int(HourOfDay(a_now));
		if (v.startHour == v.endHour) return true;
		if (v.startHour == 0 && v.endHour >= 24) return true;
		if (v.startHour < v.endHour) return h >= int(v.startHour) && h < int(v.endHour);
		return h >= int(v.startHour) || h < int(v.endHour);   // wraps past midnight
	}

	// the vendor's buy/sell list is a keyword list; "notBuySell" flips it into "everything except these"
	bool AcceptsFaction(const RE::TESFaction* f, RE::TESBoundObject* o)
	{
		const auto& vd = f->vendorData;
		// VENV byte 0x0A is "only buys stolen items" (CommonLib calls it buysNonStolen): 0 for every ordinary shop, 1 for fences
		if (vd.vendorValues.buysNonStolen || !vd.vendorSellBuyList || !o) return false;
		auto* kf = skyrim_cast<RE::BGSKeywordForm*>(o);
		bool inList = false;
		if (kf) {
			vd.vendorSellBuyList->ForEachForm([&](RE::TESForm* kw) {
				if (auto* k = kw ? kw->As<RE::BGSKeyword>() : nullptr; k && kf->HasKeyword(k)) { inList = true; return RE::BSContainer::ForEachResult::kStop; }
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		return vd.vendorValues.notBuySell ? !inList : inList;
	}

	bool OpenAt(const Merchant& m, double a_now)
	{
		for (auto id : m.factions)
			if (auto* f = RE::TESForm::LookupByID<RE::TESFaction>(id); f && OpenFaction(f, a_now)) return true;
		return false;
	}
	bool Accepts(const Merchant& m, RE::TESBoundObject* o)
	{
		for (auto id : m.factions)
			if (auto* f = RE::TESForm::LookupByID<RE::TESFaction>(id); f && AcceptsFaction(f, o)) return true;
		return false;
	}

	void Cycle(Merchant& m, RE::TESObjectREFR* chest, double a_now)
	{
		const double len = double(Sim::Cfg().merchantCycleHours);
		const std::int32_t gold = ChestGold(chest);
		if (m.st.baselineGold < 0 || a_now - m.st.cycleStart >= len) {
			// vanilla vendors restock every 48 game hours: start a fresh cycle from whatever they hold now
			m.st.baselineGold = gold;
			m.st.itemsBought = 0;
			m.st.goldStolen = 0;
			m.st.cycleStart = a_now;
		} else {
			m.st.baselineGold = std::max(m.st.baselineGold, gold);
		}
	}

	std::int32_t Floor(const Merchant& m)
	{
		return std::int32_t(std::ceil(float(m.st.baselineGold) * Sim::Cfg().merchantGoldFloor));
	}

	float SpeechMult(const Member& m) { return 1.f + m.skills[kSpeech] / 500.f; }

	std::uint32_t NodeOfChest(RE::TESObjectREFR* chest)
	{
		RE::BGSLocation* loc = nullptr;
		if (auto* cell = chest->GetParentCell()) loc = cell->GetLocation();
		if (!loc) loc = chest->GetEditorLocation();
		for (int guard = 0; loc && guard < 12; loc = loc->parentLoc, ++guard) {
			auto id = Graph::ByLocation(loc->GetFormID());
			if (id != UINT32_MAX) return id;
		}
		// an exterior stall: the closest place within reach
		if (chest->GetParentCell() && chest->GetParentCell()->IsExteriorCell()) {
			const auto p = chest->GetPosition();
			auto id = Graph::Nearest(p.x, p.y, Forms::WsIndex(chest->GetWorldspace()) > 0 ? 1 : 0);
			if (auto* n = Graph::Node(id)) {
				const float dx = n->x - p.x, dy = n->y - p.y;
				if (dx * dx + dy * dy < 9000.f * 9000.f) return id;
			}
		}
		return UINT32_MAX;
	}

	float UpgradeScore(RE::TESBoundObject* o)
	{
		if (auto* w = o->As<RE::TESObjectWEAP>()) return float(w->GetAttackDamage());
		if (auto* a = o->As<RE::TESObjectARMO>()) return float(a->armorRating) / 100.f;
		return 0.f;
	}
}

void Reset()
{
	gMerch.clear();
	gByNode.clear();
}

int Count() { return int(gMerch.size()); }

int OnNode(const GraphNode& a_node)
{
	auto it = gByNode.find(a_node.id);
	return it == gByNode.end() ? 0 : int(it->second.size());
}

void Init()
{
	Reset();
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (!dh || !Graph::Ready()) return;
	for (auto& n : Graph::W().nodes) n.vendors = 0;
	std::unordered_map<RE::FormID, int> byChest;
	int vendorFactions = 0;
	for (auto* fac : dh->GetFormArray<RE::TESFaction>()) {
		if (!fac || !fac->IsVendor()) continue;
		++vendorFactions;
		auto* chest = fac->vendorData.merchantContainer;
		if (!chest) continue;
		const auto node = NodeOfChest(chest);
		if (node == UINT32_MAX) continue;
		auto ex = byChest.find(chest->GetFormID());
		if (ex != byChest.end()) {   // several vendor factions share one chest
			gMerch[std::size_t(ex->second)].factions.push_back(fac->GetFormID());
			continue;
		}
		Merchant m;
		m.factions.push_back(fac->GetFormID());
		m.chest = chest->GetFormID();
		m.node = node;
		m.st.chest = m.chest;
		for (auto& p : gPending) if (p.chest == m.chest) m.st = p;
		byChest[m.chest] = int(gMerch.size());
		gByNode[node].push_back(int(gMerch.size()));
		gMerch.push_back(std::move(m));
		++Graph::W().nodes[node].vendors;
	}
	gPending.clear();
	if (Settings::Get().bDebug) {   // what a vendor's buy/sell list really looks like
		int shown = 0;
		for (auto& m : gMerch) {
			if (shown >= 6) break;
			auto* f = RE::TESForm::LookupByID<RE::TESFaction>(m.factions.front());
			if (!f) continue;
			const auto& vd = f->vendorData;
			int kws = 0;
			if (vd.vendorSellBuyList) vd.vendorSellBuyList->ForEachForm([&](RE::TESForm*) { ++kws; return RE::BSContainer::ForEachResult::kContinue; });
			Log::Line("[merchants] sample vendor " + std::to_string(m.chest) + ": hours " + std::to_string(vd.vendorValues.startHour) + "-" +
					  std::to_string(vd.vendorValues.endHour) + " buysNonStolen=" + std::to_string(int(vd.vendorValues.buysNonStolen)) +
					  " notBuySell=" + std::to_string(int(vd.vendorValues.notBuySell)) + " list=" + (vd.vendorSellBuyList ? std::to_string(kws) + " keywords" : "none") +
					  " factions=" + std::to_string(m.factions.size()));
			++shown;
		}
	}
	int towns = 0;
	for (auto& n : Graph::Nodes()) towns += n.vendors > 0;
	Log::Line("[merchants] vendor factions=" + std::to_string(vendorFactions) + " -> " + std::to_string(gMerch.size()) + " distinct vendor chests across " + std::to_string(towns) + " places");
	{   // the busiest places, for a sanity check
		std::vector<std::pair<int, std::string>> top;
		for (auto& n : Graph::Nodes()) if (n.vendors > 0) top.emplace_back(n.vendors, n.name);
		std::sort(top.begin(), top.end(), [](auto& a, auto& b) { return a.first > b.first; });
		std::string t;
		for (std::size_t i = 0; i < top.size() && i < 8; ++i) t += (i ? ", " : "") + top[i].second + "=" + std::to_string(top[i].first);
		Log::Line("[merchants] busiest: " + t);
	}
}

bool AnyOpen(const GraphNode& a_node, double a_now)
{
	auto it = gByNode.find(a_node.id);
	if (it == gByNode.end()) return false;
	for (int i : it->second)
		if (OpenAt(gMerch[std::size_t(i)], a_now)) return true;
	return a_node.inn;   // innkeepers never close their doors
}

bool HasSellables(const Group& a_g)
{
	for (auto& m : a_g.members) {
		if (!m.alive()) continue;
		int value = 0;
		for (auto& it : m.inventory) {
			auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(it.form);
			if (!obj || obj == Forms::R().gold || obj->Is(RE::FormType::KeyMaster)) continue;
			if (std::find(m.equipped.begin(), m.equipped.end(), it.form) != m.equipped.end()) continue;
			if (Economy::IsArtifactItem(it.form) || Economy::Wants(m, obj)) continue;
			if (Economy::IsFoodItem(obj) || Economy::IsHealPotionItem(obj)) continue;
			value += obj->GetGoldValue() * it.count;
		}
		if (value >= 150 || Economy::Carried(m) > Economy::CarryLimit(m) * 0.7f) return true;
	}
	return false;
}

bool NeedsSupplies(const Group& a_g)
{
	for (auto& m : a_g.members) {
		if (!m.alive()) continue;
		if (Economy::HealPotions(m) < 2) return true;
		if (Economy::IsArcher(m) && Economy::ArrowCount(m) < 20) return true;
		if (Economy::FoodCount(m) == 0) return true;
	}
	return false;
}

void Trade(Group& g, GraphNode& node, double a_now)
{
	auto it = gByNode.find(node.id);
	auto* gold = Forms::R().gold;
	int sold = 0, bought = 0, goldIn = 0, goldOut = 0, shops = 0, stockSeen = 0, acceptable = 0, wanted = 0, affordable = 0;
	if (it != gByNode.end() && gold) {
		for (int idx : it->second) {
			Merchant& mc = gMerch[std::size_t(idx)];
			auto* chest = ChestOf(mc);
			if (!chest || !OpenAt(mc, a_now)) continue;
			Cycle(mc, chest, a_now);
			++shops;
			stockSeen += int(chest->GetInventory().size());
			for (auto& m : g.members) {
				if (!m.alive()) continue;
				// 1) sell surplus the vendor really buys, never below the gold floor
				std::vector<Item> stock = m.inventory;
				for (auto& item : stock) {
					auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(item.form);
					if (!obj || obj == gold || obj->Is(RE::FormType::KeyMaster)) continue;
					if (std::find(m.equipped.begin(), m.equipped.end(), item.form) != m.equipped.end()) continue;
					if (Economy::IsArtifactItem(item.form) || Economy::Wants(m, obj)) continue;
					if (Economy::IsFoodItem(obj) || Economy::IsHealPotionItem(obj)) continue;
					if (obj->GetGoldValue() <= 0 || !Accepts(mc, obj)) continue;
					const int price = std::max(1, int(float(obj->GetGoldValue()) * Sim::Cfg().sellRate * SpeechMult(m)));
					for (int c = 0; c < item.count; ++c) {
						if (ChestGold(chest) - price < Floor(mc)) break;   // the merchant keeps its floor
						chest->RemoveItem(gold, price, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
						chest->AddObjectToContainer(obj, nullptr, 1, nullptr);
						Economy::RemoveItem(m, item.form, 1);
						m.gold += price;
						goldIn += price;
						++sold;
					}
				}
				// 2) buy what the member needs, best items first, a few per vendor per cycle
				if (mc.st.itemsBought >= Sim::Cfg().maxPurchasesPerCycle) continue;
				struct Cand { RE::TESBoundObject* obj; int n; int price; float score; };
				std::vector<Cand> cands;
				for (auto& [obj, data] : chest->GetInventory()) {
					if (!obj || obj == gold || data.first <= 0 || Economy::IsArtifactItem(obj->GetFormID())) continue;
					if (!Accepts(mc, obj)) continue;   // a vendor only restocks what it trades in
					++acceptable;
					float score = 0.f;
					int n = 1;
					if (Economy::IsHealPotionItem(obj)) { if (Economy::HealPotions(m) >= 3) continue; score = 100.f; }
					else if (Economy::IsFoodItem(obj)) { if (m.hunger < 300.f && Economy::FoodCount(m) > 0) continue; score = 90.f; }
					else if (obj->Is(RE::FormType::Ammo)) { if (!Economy::IsArcher(m) || Economy::ArrowCount(m) >= 30) continue; score = 80.f; n = std::min(data.first, 20); }
					else if (obj->Is(RE::FormType::Weapon) || obj->Is(RE::FormType::Armor)) { if (!Economy::Wants(m, obj)) continue; score = UpgradeScore(obj); }
					else continue;
					++wanted;
					const int price = std::max(1, int(float(obj->GetGoldValue()) * Sim::Cfg().buyMarkup / SpeechMult(m))) * n;
					if (m.gold < price || Economy::Carried(m) + obj->GetWeight() * float(n) > Economy::CarryLimit(m)) continue;
					++affordable;
					cands.push_back({ obj, n, price, score });
				}
				std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
				for (auto& c : cands) {
					if (mc.st.itemsBought >= Sim::Cfg().maxPurchasesPerCycle) break;
					if (m.gold < c.price) continue;
					chest->RemoveItem(c.obj, c.n, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					chest->AddObjectToContainer(gold, nullptr, c.price, nullptr);
					m.gold -= c.price;
					goldOut += c.price;
					Economy::AddItem(m, c.obj->GetFormID(), c.n);
					++mc.st.itemsBought;
					++bought;
				}
			}
		}
	}
	// innkeepers: a bed and a meal clear fatigue
	if (node.inn) {
		auto it2 = gByNode.find(node.id);
		RE::TESObjectREFR* till = nullptr;
		if (it2 != gByNode.end() && !it2->second.empty()) till = ChestOf(gMerch[std::size_t(it2->second.front())]);
		for (auto& m : g.members) {
			if (!m.alive()) continue;
			m.fatigue = 0.f;
			if (m.gold >= 10) {
				m.gold -= 10;
				if (till && gold) till->AddObjectToContainer(gold, nullptr, 10, nullptr);
			}
		}
	}
	for (auto& m : g.members) if (m.alive()) Economy::Reequip_(m);
	Log::Line("[trade] " + std::string(FactionId(g.faction)) + " squad " + std::to_string(g.id) + " at " + node.name + ": shops=" + std::to_string(shops) +
			  " stock=" + std::to_string(stockSeen) + " acceptable=" + std::to_string(acceptable) + " wanted=" + std::to_string(wanted) + " affordable=" + std::to_string(affordable) + " sold " + std::to_string(sold) + " (+" + std::to_string(goldIn) + "g) bought " + std::to_string(bought) + " (-" + std::to_string(goldOut) + "g)");
}

int Plunder(GraphNode& node, float a_share, double a_now)
{
	auto it = gByNode.find(node.id);
	auto* gold = Forms::R().gold;
	if (it == gByNode.end() || !gold) return 0;
	int total = 0;
	for (int idx : it->second) {
		Merchant& mc = gMerch[std::size_t(idx)];
		auto* chest = ChestOf(mc);
		if (!chest) continue;
		Cycle(mc, chest, a_now);
		const std::int32_t have = ChestGold(chest);
		const std::int32_t room = have - Floor(mc);
		const std::int32_t take = std::min(room, std::int32_t(float(have) * a_share));
		if (take <= 0) continue;
		chest->RemoveItem(gold, take, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		mc.st.goldStolen += take;
		total += take;
	}
	return total;
}

std::vector<State> Snapshot()
{
	std::vector<State> out;
	for (auto& m : gMerch) if (m.st.baselineGold >= 0) out.push_back(m.st);
	return out;
}

void Restore(const std::vector<State>& a_states)
{
	gPending = a_states;
	for (auto& m : gMerch)
		for (auto& p : a_states)
			if (p.chest == m.chest) m.st = p;
}
}
