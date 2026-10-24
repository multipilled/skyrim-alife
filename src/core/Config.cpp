#include "ALife/Config.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace ALife {

namespace {
	// The responder faction a respawn entry may use instead of a fixed one: the smart's current controller.
	// Stored in RespawnParam::faction.
	constexpr std::uint8_t kController = 254;

	std::string Lower(std::string s)
	{
		for (auto& c : s) c = char(std::tolower((unsigned char)c));
		return s;
	}

	// "ally: a, b | enemy: c, d"
	void ParseRelationLine(Config& c, Faction self, const std::string& value)
	{
		for (auto& part : SplitList(value, '|')) {
			auto colon = part.find(':');
			if (colon == std::string::npos) continue;
			const std::string kind = Lower(Trim(part.substr(0, colon)));
			const int v = kind == "ally" ? 2000 : kind == "friend" ? 600 : kind == "rival" ? -600 : kind == "enemy" ? -2000 : 0;
			for (auto& id : SplitList(part.substr(colon + 1))) {
				bool ok = false;
				Faction other = FactionFromId(id, &ok);
				if (ok) c.rel[int(self)][int(other)] = std::int16_t(v);
			}
		}
	}

	bool ParseFloat(const std::string& s, float& out)
	{
		try { out = std::stof(s); return true; } catch (...) { return false; }
	}
}

MonsterType MonsterTypeFromName(const std::string& a_name)
{
	const std::string n = Lower(Trim(a_name));
	if (n == "predatory_day") return MonsterType::PredatoryDay;
	if (n == "predatory_night") return MonsterType::PredatoryNight;
	if (n == "zombied_day") return MonsterType::ZombiedDay;
	if (n == "zombied_night") return MonsterType::ZombiedNight;
	if (n == "vegetarian") return MonsterType::Vegetarian;
	if (n == "zombied") return MonsterType::Zombied;
	if (n == "special") return MonsterType::Special;
	if (n == "flyer") return MonsterType::Flyer;
	return MonsterType::None;
}

void Config::IndexCreatures()
{
	creatureByForm.clear();
	for (std::size_t i = 0; i < creatures.size(); ++i)
		if (creatures[i].form) creatureByForm[creatures[i].form] = int(i);
	for (auto& s : squads) {
		s.creatures.erase(std::remove_if(s.creatures.begin(), s.creatures.end(),
			[&](const std::pair<int, float>& c) { return !creatures[std::size_t(c.first)].form; }), s.creatures.end());
		if (s.leaderCreature >= 0 && !creatures[std::size_t(s.leaderCreature)].form) s.leaderCreature = -1;
	}
}

Kind ClassifyKeywords(const std::vector<FormID>& a_kw)
{
	auto has = [&](FormID id) { return std::find(a_kw.begin(), a_kw.end(), id) != a_kw.end(); };
	// Vanilla Skyrim.esm keyword FormIDs (verified against the user's Skyrim.esm with tools/kwtable.py).
	if (has(0x130DF)) return Kind::BanditCamp;          // LocTypeBanditCamp
	if (has(0x130EE)) return Kind::ForswornCamp;        // LocTypeForswornCamp
	if (has(0x130EC)) return Kind::WarlockLair;         // LocTypeWarlockLair
	if (has(0x130EB)) return Kind::VampireLair;         // LocTypeVampireLair
	if (has(0x130ED)) return Kind::WerewolfLair;        // LocTypeWerewolfLair
	if (has(0x130E4)) return Kind::FalmerHive;          // LocTypeFalmerHive
	if (has(0x130E5)) return Kind::GiantCamp;           // LocTypeGiantCamp
	if (has(0x130E0)) return Kind::DragonLair;          // LocTypeDragonLair
	if (has(0x130E1)) return Kind::DragonPriestLair;    // LocTypeDragonPriestLair
	if (has(0x130E6)) return Kind::HagravenNest;        // LocTypeHagravenNest
	if (has(0x130EA)) return Kind::SprigganGrove;       // LocTypeSprigganGrove
	if (has(0x130E9)) return Kind::OrcStronghold;       // LocTypeOrcStronghold
	if (has(0x13168)) return Kind::City;                // LocTypeCity
	if (has(0x130E7) || has(0x130F1) || has(0x1CD57)) return Kind::Fort;   // MilitaryFort, LocSetMilitaryFort, Castle
	if (has(0x130E8) || has(0x1926A)) return Kind::MilitaryCamp;           // MilitaryCamp, LocSetMilitaryCamp
	if (has(0x130E3) || has(0x130F0)) return Kind::DwarvenRuin;             // DwarvenAutomatons, LocSetDwarvenRuin
	if (has(0x130E2) || has(0x1CD58)) return Kind::Crypt;                  // DraugrCrypt, Cemetery
	if (has(0x18EF1)) return Kind::Mine;                // LocTypeMine
	if (has(0x13166) || has(0x13167)) return Kind::Town;   // Town, Settlement
	if (has(0x18EF0)) return Kind::Farm;                // LocTypeFarm
	if (has(0x1CB87) || has(0xA6E84)) return Kind::Inn; // Inn, HabitationHasInn
	if (has(0x18EF2)) return Kind::Mill;                // LumberMill
	if (has(0x39793) || has(0x130DC) || has(0x1CB85) || has(0x1CD5A) || has(0x1CB86) || has(0x504F9)) return Kind::Town;   // Habitation, Dwelling, House, Guild, Store, Steward
	if (has(0x1929F) || has(0x1CD5B)) return Kind::Shipwreck;   // Shipwreck, Ship
	if (has(0x130DE)) return Kind::AnimalDen;           // LocTypeAnimalDen
	if (has(0x130DB) || has(0xF5E80) || has(0x130EF) || has(0x100819) || has(0x130F2)) return Kind::Dungeon;   // Dungeon, Clearable, Cave, IceCave, NordicRuin
	return Kind::Wilderness;
}

// ------------------------------------------------------------------ races (T24)
namespace {
	// The tables the plugin rolled from before T24 (src/Sim.cpp RollRace), in the same order: Tamriel draws stay identical.
	void DefaultRaces(Config& c)
	{
		using R = RaceId;
		auto set = [&](Faction f, std::initializer_list<RaceWeight> t) { c.races[0][int(f)] = t; };
		const RaceTable adv = { { R::Nord, 25 }, { R::Imperial, 12 }, { R::Breton, 12 }, { R::Redguard, 10 }, { R::Altmer, 8 },
		                        { R::Bosmer, 8 }, { R::Dunmer, 10 }, { R::Orsimer, 6 }, { R::Khajiit, 5 }, { R::Argonian, 4 } };
		for (auto& ws : c.races) for (auto& t : ws) t.clear();
		for (int f = 0; f < kFactionCount; ++f) c.races[0][f] = adv;   // the default (Wanderer, and anyone not listed)
		set(Faction::Bandit, { { R::Nord, 40 }, { R::Imperial, 12 }, { R::Redguard, 10 }, { R::Breton, 8 }, { R::Dunmer, 8 },
		                       { R::Orsimer, 8 }, { R::Bosmer, 5 }, { R::Khajiit, 4 }, { R::Argonian, 3 }, { R::Altmer, 2 } });
		set(Faction::Forsworn, { { R::Breton, 90 }, { R::Nord, 10 } });
		set(Faction::Imperial, { { R::Imperial, 45 }, { R::Nord, 20 }, { R::Redguard, 15 }, { R::Breton, 12 }, { R::Bosmer, 5 }, { R::Altmer, 3 } });
		set(Faction::Stormcloak, { { R::Nord, 75 }, { R::Dunmer, 5 }, { R::Redguard, 5 }, { R::Breton, 5 }, { R::Orsimer, 5 }, { R::Khajiit, 3 }, { R::Argonian, 2 } });
		set(Faction::Thalmor, { { R::Altmer, 88 }, { R::Bosmer, 12 } });
		set(Faction::Hunter, { { R::Nord, 50 }, { R::Bosmer, 15 }, { R::Imperial, 10 }, { R::Redguard, 8 }, { R::Breton, 6 },
		                       { R::Orsimer, 5 }, { R::Khajiit, 3 }, { R::Argonian, 3 } });
		set(Faction::Mercenary, { { R::Nord, 25 }, { R::Redguard, 20 }, { R::Imperial, 15 }, { R::Orsimer, 15 }, { R::Breton, 10 },
		                          { R::Dunmer, 5 }, { R::Khajiit, 5 }, { R::Argonian, 5 } });
		set(Faction::Mage, { { R::Breton, 30 }, { R::Altmer, 25 }, { R::Dunmer, 20 }, { R::Imperial, 15 }, { R::Nord, 5 }, { R::Redguard, 5 } });
		set(Faction::Necromancer, { { R::Dunmer, 30 }, { R::Breton, 25 }, { R::Imperial, 15 }, { R::Altmer, 15 }, { R::Nord, 10 }, { R::Redguard, 5 } });
		set(Faction::Vampire, { { R::NordVampire, 2 }, { R::ImperialVampire, 1 }, { R::BretonVampire, 1 }, { R::DunmerVampire, 2 } });
		set(Faction::Dawnguard, { { R::Nord, 35 }, { R::Imperial, 25 }, { R::Redguard, 15 }, { R::Breton, 15 }, { R::Orsimer, 10 } });
		set(Faction::SilverHand, { { R::Nord, 55 }, { R::Imperial, 15 }, { R::Breton, 10 }, { R::Redguard, 10 }, { R::Orsimer, 10 } });
		set(Faction::Vigilant, { { R::Nord, 35 }, { R::Imperial, 30 }, { R::Breton, 20 }, { R::Redguard, 10 }, { R::Altmer, 5 } });
		set(Faction::Caravan, { { R::Khajiit, 1 } });
		set(Faction::Skaal, { { R::Nord, 1 } });
		set(Faction::Redoran, { { R::Dunmer, 1 } });
		set(Faction::Reaver, { { R::Dunmer, 40 }, { R::Nord, 30 }, { R::Imperial, 8 }, { R::Redguard, 8 }, { R::Orsimer, 6 },
		                       { R::Breton, 4 }, { R::Argonian, 2 }, { R::Khajiit, 2 } });
	}

	// "nord:40, imperial:12" -> a table; false (and the old table kept) on any bad entry
	bool ParseRaces(const std::string& v, RaceTable& out, std::string& err)
	{
		RaceTable t;
		for (auto& e : SplitList(v)) {
			const auto colon = e.find(':');
			bool ok = false;
			const RaceId r = RaceFromId(Trim(e.substr(0, colon)), &ok);
			int w = 0;
			try { w = colon == std::string::npos ? 0 : std::stoi(e.substr(colon + 1)); } catch (...) { ok = false; }
			if (!ok || w <= 0) { err = "races.ltx: bad entry '" + e + "'"; return false; }
			t.push_back({ r, w });
		}
		if (t.empty()) { err = "races.ltx: empty table"; return false; }
		out = std::move(t);
		return true;
	}

	void LoadRaces(const Ltx& l, Config& c, std::string& warn)
	{
		for (int ws = 0; ws < kWorldspaces; ++ws) {
			const std::string sec = ws == 0 ? "races" : std::string("races.") + kWorldspaceIds[ws];
			if (auto* lines = l.Section(sec))
				for (auto& kv : *lines) {
					bool ok = false;
					const Faction f = FactionFromId(kv.first, &ok);
					std::string err;
					if (!ok || !IsHumanFaction(f)) { warn += "races.ltx [" + sec + "]: unknown faction '" + kv.first + "'; "; continue; }
					if (!ParseRaces(kv.second, c.races[std::size_t(ws)][int(f)], err)) warn += err + " in [" + sec + "] " + kv.first + "; ";
				}
		}
	}
}

const RaceTable& Config::RacesFor(Faction f, int ws) const
{
	const int w = ws >= 0 && ws < kWorldspaces ? ws : 0;
	const auto& t = races[std::size_t(w)][int(f)];
	return t.empty() ? races[0][int(f)] : t;
}

RaceId Config::RollRace(Faction f, int ws, Rng& r) const
{
	const RaceTable& t = RacesFor(f, ws);
	if (t.empty()) return RaceId::Nord;
	int tot = 0;
	for (auto& e : t) tot += e.w;
	int x = r.index(tot);
	for (auto& e : t) { if (x < e.w) return e.race; x -= e.w; }
	return t[0].race;
}

bool LoadConfig(const std::string& a_dir, Config& c, std::string& a_err)
{
	c = Config{};
	std::string dir = a_dir;
	if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';

	Ltx alife, fac, sq, stand, mon;
	std::string err;
	bool okAlife = alife.LoadFile(dir + "alife.ltx", &err);
	bool okFac = fac.LoadFile(dir + "factions.ltx", &err);
	bool okProps = c.props.LoadFile(dir + "props.ltx", &err);
	bool okSq = sq.LoadFile(dir + "squads.ltx", &err);
	stand.LoadFile(dir + "standing.ltx", nullptr);   // optional: compiled-in defaults stand in when missing
	mon.LoadFile(dir + "monsters.ltx", nullptr);       // optional: without it no monster squad can be made
	Ltx compat;
	const bool okCompat = compat.LoadFile(dir + "compat.ltx", nullptr);   // optional: without it no patrol mod is recognised
	{
		Ltx diff;   // optional: without it the compiled-in rows of docs/DIFFICULTY.md
		if (diff.LoadFile(dir + "difficulty.ltx", nullptr)) LoadDifficultyConfig(diff, c.diffCfg);
		c.ApplyDifficulty(kAdept, DifficultyChoice{});
	}
	a_err = err;
	if (!okAlife || !okFac || !okProps || !okSq) return false;
	if (okCompat) LoadCompat(compat, c.compatCfg, &a_err);

	// ---- alife.ltx
	c.stepHours = alife.GetF("sim", "step_hours", c.stepHours);
	c.walkSpeed = alife.GetF("sim", "walk_speed", c.walkSpeed);
	c.maxCatchUpHours = alife.GetF("sim", "max_catchup_hours", float(c.maxCatchUpHours));
	c.switchDistance = alife.GetF("online", "switch_distance", c.switchDistance);
	c.offlineDistance = alife.GetF("online", "offline_distance", c.offlineDistance);
	c.topTargets = alife.GetI("board", "top_targets", c.topTargets);
	c.bestChance = alife.GetF("board", "best_chance", c.bestChance);
	c.distanceBias = alife.GetF("board", "distance_bias", c.distanceBias);
	c.idleMin = alife.GetF("board", "idle_min", c.idleMin);
	c.idleMax = alife.GetF("board", "idle_max", c.idleMax);
	c.retargetHours = alife.GetF("board", "retarget_hours", c.retargetHours);
	c.wActor = alife.GetF("squad_behaviour", "actor", c.wActor);
	c.wAll = alife.GetF("squad_behaviour", "all", c.wAll);
	c.wBase = alife.GetF("squad_behaviour", "base", c.wBase);
	c.wLair = alife.GetF("squad_behaviour", "lair", c.wLair);
	c.wResource = alife.GetF("squad_behaviour", "resource", c.wResource);
	c.wSquad = alife.GetF("squad_behaviour", "squad", c.wSquad);
	c.wSurge = alife.GetF("squad_behaviour", "surge", c.wSurge);
	c.wTerritory = alife.GetF("squad_behaviour", "territory", c.wTerritory);
	c.baseRaidFrom = alife.GetF("time_windows", "base_raid_from", c.baseRaidFrom);
	c.baseRaidTo = alife.GetF("time_windows", "base_raid_to", c.baseRaidTo);
	c.territoryRaidFrom = alife.GetF("time_windows", "territory_raid_from", c.territoryRaidFrom);
	c.territoryRaidTo = alife.GetF("time_windows", "territory_raid_to", c.territoryRaidTo);
	c.resourceFrom = alife.GetF("time_windows", "resource_from", c.resourceFrom);
	c.resourceTo = alife.GetF("time_windows", "resource_to", c.resourceTo);
	c.monsterDayFrom = alife.GetF("time_windows", "monster_day_from", c.monsterDayFrom);
	c.monsterDayTo = alife.GetF("time_windows", "monster_day_to", c.monsterDayTo);
	c.monsterNightFrom = alife.GetF("time_windows", "monster_night_from", c.monsterNightFrom);
	c.monsterNightTo = alife.GetF("time_windows", "monster_night_to", c.monsterNightTo);
	c.dayOfflineFrom = alife.GetF("time_windows", "day_monster_offline_from", c.dayOfflineFrom);
	c.dayOfflineTo = alife.GetF("time_windows", "day_monster_offline_to", c.dayOfflineTo);
	c.nightOfflineFrom = alife.GetF("time_windows", "night_monster_offline_from", c.nightOfflineFrom);
	c.nightOfflineTo = alife.GetF("time_windows", "night_monster_offline_to", c.nightOfflineTo);
	c.mLair = alife.GetF("monster_behaviour", "lair", c.mLair);
	c.mMonster = alife.GetF("monster_behaviour", "monster", c.mMonster);
	c.mTerritory = alife.GetF("monster_behaviour", "territory", c.mTerritory);
	c.mBase = alife.GetF("monster_behaviour", "base", c.mBase);
	c.mResource = alife.GetF("monster_behaviour", "resource", c.mResource);
	c.mHome = alife.GetF("monster_behaviour", "home", c.mHome);
	c.lairIdleMin = alife.GetF("monster_behaviour", "lair_idle_min", c.lairIdleMin);
	c.lairIdleMax = alife.GetF("monster_behaviour", "lair_idle_max", c.lairIdleMax);
	c.defaultRespawnIdle = alife.GetF("respawn", "default_idle", c.defaultRespawnIdle);
	c.defaultRespawnRadius = alife.GetF("respawn", "default_radius", c.defaultRespawnRadius);
	c.populationFactor = alife.GetF("respawn", "population_factor", c.populationFactor);
	c.populationLtx = c.populationFactor;
	c.startFill = alife.GetF("respawn", "start_fill", c.startFill);
	c.monsterPopulationFactor = alife.GetF("respawn", "monster_population_factor", c.monsterPopulationFactor);
	{
		auto m = Lower(alife.Get("ocs", "mode", "smarts"));
		c.ocsMode = m == "off" ? OcsMode::Off : m == "full" ? OcsMode::Full : OcsMode::Smarts;
	}
	c.combatDistance = alife.GetF("ocs", "combat_distance", c.combatDistance);
	c.hitBase = alife.GetF("ocs", "hit_base", c.hitBase);
	c.hitFactor = alife.GetF("ocs", "hit_factor", c.hitFactor);
	c.rankFraction = alife.GetF("ocs", "rank_fraction", c.rankFraction);
	c.commanderGain = alife.GetF("ocs", "commander_gain", c.commanderGain);
	c.roundsPerStep = alife.GetI("ocs", "rounds_per_step", c.roundsPerStep);
	c.minSquadsForOcs = alife.GetI("ocs", "min_squads", c.minSquadsForOcs);
	c.fleeRatio = alife.GetF("ocs", "flee_ratio", c.fleeRatio);
	c.healPerHour = alife.GetF("ocs", "heal_per_hour", c.healPerHour);
	c.lootCorpses = alife.GetI("loot", "corpses", c.lootCorpses ? 1 : 0) != 0;
	c.lootRadius = alife.GetF("loot", "radius", c.lootRadius);
	c.lootGraceSeconds = alife.GetF("loot", "grace_seconds", c.lootGraceSeconds);
	c.lootGiveUpSeconds = alife.GetF("loot", "give_up_seconds", c.lootGiveUpSeconds);
	c.lootMinValue = alife.GetI("loot", "min_value", c.lootMinValue);
	for (auto& id : SplitList(alife.Get("loot", "factions", "all"), ',')) {
		if (Lower(Trim(id)) == "all") { c.looters.fill(true); continue; }
		bool ok = false;
		const Faction f = FactionFromId(Trim(id), &ok);
		if (ok) c.looters[int(f)] = true;
	}
	c.levelPaceHours = alife.GetF("balance", "level_pace_hours", c.levelPaceHours);
	c.levelsPerPace = alife.GetI("balance", "levels_per_pace", c.levelsPerPace);
	c.regearGap = alife.GetI("balance", "regear_gap", c.regearGap);
	c.hungerPerHour = alife.GetF("needs", "hunger_per_hour", c.hungerPerHour);
	c.fatiguePerHour = alife.GetF("needs", "fatigue_per_hour", c.fatiguePerHour);
	c.coldDay = alife.GetF("needs", "cold_day", c.coldDay);
	c.coldNight = alife.GetF("needs", "cold_night", c.coldNight);
	c.hungryAt = alife.GetF("needs", "hungry_at", c.hungryAt);
	c.tiredAt = alife.GetF("needs", "tired_at", c.tiredAt);
	c.coldAt = alife.GetF("needs", "cold_at", c.coldAt);
	c.needBiasFood = alife.GetF("needs", "bias_food", c.needBiasFood);
	c.needBiasRest = alife.GetF("needs", "bias_rest", c.needBiasRest);
	c.needBiasWarm = alife.GetF("needs", "bias_warm", c.needBiasWarm);
	c.needBiasSell = alife.GetF("needs", "bias_sell", c.needBiasSell);
	c.needBiasHeal = alife.GetF("needs", "bias_heal", c.needBiasHeal);
	c.settlementRaidCooldownDays = alife.GetF("activities", "settlement_raid_cooldown_days", c.settlementRaidCooldownDays);
	c.dungeonResetDays = alife.GetF("activities", "dungeon_reset_days", c.dungeonResetDays);
	c.raidSectionHours = alife.GetF("activities", "raid_section_hours", c.raidSectionHours);
	c.merchantCycleHours = alife.GetF("activities", "merchant_cycle_hours", c.merchantCycleHours);
	c.merchantGoldFloor = alife.GetF("activities", "merchant_gold_floor", c.merchantGoldFloor);
	c.sellRate = alife.GetF("activities", "sell_rate", c.sellRate);
	c.buyMarkup = alife.GetF("activities", "buy_markup", c.buyMarkup);
	c.maxPurchasesPerCycle = alife.GetI("activities", "max_purchases_per_cycle", c.maxPurchasesPerCycle);
	c.huntRate = alife.GetF("activities", "hunt_rate", c.huntRate);
	c.wildlifeRisk = alife.GetF("activities", "wildlife_risk", c.wildlifeRisk);
	c.fishRate = alife.GetF("activities", "fish_rate", c.fishRate);
	c.news = alife.GetB("news", "enabled", c.news);
	c.newsRadius = alife.GetF("news", "radius", c.newsRadius);
	c.newsSos = alife.GetB("news", "sos", c.newsSos);
	c.newsDeaths = alife.GetB("news", "death_reports", c.newsDeaths);
	c.newsGossip = alife.GetB("news", "gossip", c.newsGossip);
	c.sosCooldownHours = alife.GetF("news", "sos_cooldown_hours", c.sosCooldownHours);
	c.deathCooldownHours = alife.GetF("news", "death_report_cooldown_hours", c.deathCooldownHours);
	c.gossipHours = alife.GetF("news", "gossip_hours", c.gossipHours);
	c.sosGapHours = alife.GetF("news", "sos_gap_hours", c.sosGapHours);
	c.battleGapHours = alife.GetF("news", "battle_report_gap_hours", c.battleGapHours);
	c.deathGapHours = alife.GetF("news", "death_report_gap_hours", c.deathGapHours);

	// ---- factions.ltx
	for (int i = 0; i < kFactionCount; ++i) {
		c.rel[i].fill(0);
		c.rel[i][i] = 2000;
		c.playerRel[i] = 0;
	}
	if (auto* lines = fac.Section("relations")) {
		for (auto& kv : *lines) {
			bool ok = false;
			Faction f = FactionFromId(kv.first, &ok);
			if (ok) ParseRelationLine(c, f, kv.second);
		}
	}
	for (int a = 0; a < kFactionCount; ++a)           // symmetric: an unset reverse entry copies the explicit one
		for (int b = 0; b < kFactionCount; ++b)
			if (c.rel[a][b] != 0 && c.rel[b][a] == 0) c.rel[b][a] = c.rel[a][b];
	c.dynamicRelations = fac.GetB("dynamic_relations", "enabled", c.dynamicRelations);
	c.deathValue = fac.GetF("dynamic_relations", "death_value", c.deathValue);
	c.friendLimit = fac.GetI("dynamic_relations", "friend_limit", c.friendLimit);
	c.enemyLimit = fac.GetI("dynamic_relations", "enemy_limit", c.enemyLimit);
	for (auto& pair : fac.List("dynamic_relations", "locked_pairs")) {   // a/b, ...
		const auto slash = pair.find('/');
		if (slash == std::string::npos) continue;
		bool okA = false, okB = false;
		const Faction fa = FactionFromId(Trim(pair.substr(0, slash)), &okA), fb = FactionFromId(Trim(pair.substr(slash + 1)), &okB);
		if (okA && okB) c.lockedPairs.emplace_back(fa, fb);
	}
	c.ResetLiveRelations();
	if (auto* lines = fac.Section("player")) {
		for (auto& kv : *lines) {
			bool ok = false;
			Faction f = FactionFromId(kv.first, &ok);
			float v = 0;
			if (ok && ParseFloat(kv.second, v)) c.playerRel[int(f)] = std::int16_t(v);
		}
	}
	if (auto* lines = fac.Section("ws_faction")) {
		for (auto& kv : *lines) {
			bool ok = false;
			const Faction f = FactionFromId(kv.second, &ok);
			if (!ok) continue;
			if (Lower(kv.first) == "tamriel") c.wsFaction[0] = std::uint8_t(f);
			else if (Lower(kv.first) == "solstheim") c.wsFaction[1] = std::uint8_t(f);
		}
	}
	c.civilWar = fac.GetB("civil_war", "enabled", c.civilWar);
	if (auto sides = fac.List("civil_war", "sides"); sides.size() == 2) {
		bool okA = false, okB = false;
		const Faction a = FactionFromId(Trim(sides[0]), &okA), b = FactionFromId(Trim(sides[1]), &okB);
		if (okA && okB) c.cwSides = { std::uint8_t(a), std::uint8_t(b) };
	}
	if (fac.Has("civil_war", "guard_entries")) c.guardEntries = fac.List("civil_war", "guard_entries");
	if (fac.Has("civil_war", "stand_down")) {
		c.standDownWs.fill(false);
		for (auto& w : fac.List("civil_war", "stand_down")) {
			if (Lower(Trim(w)) == "tamriel") c.standDownWs[0] = true;
			else if (Lower(Trim(w)) == "solstheim") c.standDownWs[1] = true;
		}
	}
	if (auto* lines = fac.Section("faction_gates")) {   // dawnguard = Dawnguard.esm|0098CB:5
		for (auto& kv : *lines) {
			bool ok = false;
			const Faction f = FactionFromId(Trim(kv.first), &ok);
			if (!ok) continue;
			for (auto& item : SplitList(kv.second)) {
				const auto bar = item.find('|'), colon = item.rfind(':'), eq = item.rfind('=');
				if (bar != std::string::npos && eq != std::string::npos && eq > bar) {   // dragon = Skyrim.esm|042851=1: a global at least 1
					Config::FactionGate g;
					g.faction = f;
					g.plugin = Trim(item.substr(0, bar));
					g.global = true;
					try {
						g.quest = FormID(std::stoul(item.substr(bar + 1, eq - bar - 1), nullptr, 16));
						g.value = std::stof(item.substr(eq + 1));
					} catch (...) { continue; }
					c.gates.push_back(g);
					continue;
				}
				if (bar == std::string::npos || colon == std::string::npos || colon < bar) continue;
				Config::FactionGate g;
				g.faction = f;
				g.plugin = Trim(item.substr(0, bar));
				try {
					g.quest = FormID(std::stoul(item.substr(bar + 1, colon - bar - 1), nullptr, 16));
					g.stage = std::uint16_t(std::stoul(item.substr(colon + 1)));
				} catch (...) { continue; }
				c.gates.push_back(g);
			}
		}
	}
	if (auto* lines = fac.Section("hold_faction")) {
		for (auto& kv : *lines) {
			bool ok = false;
			Faction f = FactionFromId(kv.second, &ok);
			if (!ok) continue;
			try { c.holdFaction[FormID(std::stoul(kv.first, nullptr, 16))] = f; } catch (...) {}
		}
	}

	if (auto* lines = fac.Section("monsters")) {
		for (auto& kv : *lines) {
			bool ok = false;
			const Faction f = FactionFromId(kv.first, &ok);
			if (ok && !IsHumanFaction(f)) c.monster[int(f)] = MonsterTypeFromName(kv.second);
		}
	}

	// ---- monsters.ltx: the creatures monster squads are made of
	for (auto& name : mon.Sections()) {
		if (name.rfind("creature_", 0) != 0) continue;
		CreatureDef d;
		d.id = name.substr(9);
		d.name = mon.Get(name, "name", d.id);
		std::string form = Trim(mon.Get(name, "form", "0"));
		if (auto bar = form.find('|'); bar != std::string::npos) {   // "plugin.esl|local id": resolved by the host after data load
			d.plugin = Trim(form.substr(0, bar));
			form = Trim(form.substr(bar + 1));
		}
		try { d.form = FormID(std::stoul(form, nullptr, 16)); } catch (...) { d.form = 0; }
		if (!d.form) continue;
		d.health = mon.GetF(name, "health", d.health);
		d.healthPerLevel = mon.GetF(name, "health_per_level", d.healthPerLevel);
		d.damage = mon.GetF(name, "damage", d.damage);
		d.damagePerLevel = mon.GetF(name, "damage_per_level", d.damagePerLevel);
		d.hit = mon.GetF(name, "hit", d.hit);
		for (auto& part : mon.List(name, "parts")) {   // parts = <hex form>:<count>, ...
			const auto colon = part.find(':');
			Item it;
			try { it.form = FormID(std::stoul(Trim(part.substr(0, colon)), nullptr, 16)); } catch (...) { continue; }
			it.count = colon == std::string::npos ? 1 : std::max(1, std::atoi(part.substr(colon + 1).c_str()));
			if (it.form) d.parts.push_back(it);
		}
		c.creatureByForm[d.form] = int(c.creatures.size());
		c.creatures.push_back(std::move(d));
	}
	auto creatureIndex = [&](const std::string& id) {
		for (std::size_t i = 0; i < c.creatures.size(); ++i) if (c.creatures[i].id == id) return int(i);
		return -1;
	};

	LoadStandingConfig(stand, c.standing);
	DefaultRaces(c);
	{
		Ltx races;
		std::string warn;
		if (races.LoadFile(dir + "races.ltx", nullptr)) LoadRaces(races, c, warn);   // optional: the compiled-in tables stand in when missing
		if (!warn.empty()) a_err += warn;
	}
	// the old [player] row of factions.ltx is only a fallback baseline when standing.ltx has none
	if (!stand.Has("base"))
		for (int i = 0; i < kFactionCount; ++i) c.standing.base[i] = float(c.playerRel[i]);

	// ---- activity factions (defaults when the section is missing)
	{
		auto fill = [&](std::array<bool, kFactionCount>& out, const char* key, std::initializer_list<Faction> def) {
			out.fill(false);
			if (alife.Has("activity_factions", key)) {
				for (auto& id : alife.List("activity_factions", key)) {
					bool ok = false;
					Faction f = FactionFromId(id, &ok);
					if (ok) out[int(f)] = true;
				}
			} else {
				for (auto f : def) out[int(f)] = true;
			}
		};
		using F = Faction;
		fill(c.doesTrade, "trade", { F::Imperial, F::Stormcloak, F::Thalmor, F::Wanderer, F::Hunter, F::Mercenary, F::Mage, F::Dawnguard, F::Vigilant, F::Caravan, F::Skaal, F::Redoran });
		fill(c.doesRaidSettlements, "raid_settlement", { F::Bandit, F::Forsworn, F::SilverHand, F::Reaver });
		fill(c.doesClearDungeons, "clear_dungeon", { F::Wanderer, F::Mercenary, F::Mage, F::Dawnguard, F::Vigilant, F::Necromancer, F::Hunter });
		fill(c.doesHunt, "hunt", { F::Hunter, F::Wanderer, F::Mercenary, F::Bandit, F::Dawnguard, F::SilverHand, F::Forsworn, F::Skaal, F::Reaver });
		fill(c.doesFish, "fish", { F::Hunter, F::Wanderer, F::Mercenary, F::Skaal });
		fill(c.harvestsParts, "harvest_parts", { F::Hunter, F::Wanderer });
		fill(c.huntsLairs, "hunt_lairs", { F::Hunter });
		c.wHuntLair = alife.GetF("activities", "hunt_lair_weight", c.wHuntLair);
	}

	// ---- squads.ltx
	for (auto& name : sq.Sections()) {
		SquadDef d;
		d.section = name;
		bool ok = false;
		d.faction = FactionFromId(sq.Get(name, "faction"), &ok);
		if (!ok) continue;
		auto t = Lower(sq.Get(name, "tier", "novice"));
		d.tier = t == "veteran" ? Tier::Veteran : t == "advanced" ? Tier::Advanced : Tier::Novice;
		auto n = SplitList(sq.Get(name, "npc_in_squad", "2,3"));
		d.minMembers = n.size() > 0 ? std::max(1, std::atoi(n[0].c_str())) : 2;
		d.maxMembers = n.size() > 1 ? std::max(d.minMembers, std::atoi(n[1].c_str())) : d.minMembers;
		auto lo = SplitList(sq.Get(name, "level_offset", "-2,2"));
		d.levelOffsetMin = lo.size() > 0 ? std::atoi(lo[0].c_str()) : -2;
		d.levelOffsetMax = lo.size() > 1 ? std::atoi(lo[1].c_str()) : 2;
		d.common = sq.GetB(name, "common", true);
		static const char* roleNames[] = { "melee", "heavy", "archer", "mage", "spellsword", "leader" };
		for (auto& w : d.roleWeights) w = 0.f;
		for (auto& item : SplitList(sq.Get(name, "roles", "melee:1"))) {
			auto colon = item.find(':');
			std::string rn = Lower(Trim(colon == std::string::npos ? item : item.substr(0, colon)));
			float w = 1.f;
			if (colon != std::string::npos) ParseFloat(Trim(item.substr(colon + 1)), w);
			for (int r = 0; r < int(Role::COUNT); ++r) if (rn == roleNames[r]) d.roleWeights[r] = w;
		}
		float tot = 0;
		for (auto w : d.roleWeights) tot += w;
		if (tot <= 0) d.roleWeights[int(Role::Melee)] = 1.f;
		if (c.IsMonster(d.faction)) {
			for (auto& item : SplitList(sq.Get(name, "creatures", ""))) {
				auto colon = item.find(':');
				const int ci = creatureIndex(Lower(Trim(colon == std::string::npos ? item : item.substr(0, colon))));
				float w = 1.f;
				if (colon != std::string::npos) ParseFloat(Trim(item.substr(colon + 1)), w);
				if (ci >= 0 && w > 0.f) d.creatures.emplace_back(ci, w);
			}
			d.leaderCreature = creatureIndex(Lower(Trim(sq.Get(name, "leader", ""))));
			if (d.creatures.empty()) continue;   // a monster squad needs creatures (monsters.ltx missing or ids unknown)
		}
		c.squadIndex[d.section] = int(c.squads.size());
		c.squads.push_back(std::move(d));
	}

	// ---- dragons.ltx + dragon_places.ltx (docs/DRAGONS.md): optional; without them no dragon is ever made
	{
		Ltx dl, dp;
		auto& d = c.dragons;
		if (dl.LoadFile(dir + "dragons.ltx", nullptr)) {
			d.maxDragons = std::max(0.f, dl.GetF("dragons", "max_dragons", d.maxDragons));
			d.respawnDays = std::max(0.01f, dl.GetF("dragons", "respawn_days", float(d.respawnDays)));
			d.levelMult = std::max(0.f, dl.GetF("dragons", "level_mult", d.levelMult));
			d.flySpeed = std::max(1000.f, dl.GetF("dragons", "fly_speed", d.flySpeed));
			d.stayMin = dl.GetF("dragons", "stay_min", float(d.stayMin));
			d.stayMax = std::max(float(d.stayMin), dl.GetF("dragons", "stay_max", float(d.stayMax)));
			d.preyBonus = dl.GetF("dragons", "prey_bonus", d.preyBonus);
			d.lairWeight = dl.GetF("dragons", "lair_weight", d.lairWeight);
			d.wallWeight = dl.GetF("dragons", "wall_weight", d.wallWeight);
			d.highWeight = dl.GetF("dragons", "high_weight", d.highWeight);
			d.highZ = dl.GetF("dragons", "high_z", d.highZ);
			d.fleeHours = dl.GetF("dragons", "flee_hours", float(d.fleeHours));
			d.onlineDistance = dl.GetF("dragons", "online_distance", d.onlineDistance);
			d.offlineDistance = std::max(d.onlineDistance, dl.GetF("dragons", "offline_distance", d.offlineDistance));
			d.spawnHeight = dl.GetF("dragons", "spawn_height", d.spawnHeight);
			d.onlineMaxHours = dl.GetF("dragons", "online_max_hours", float(d.onlineMaxHours));
			if (auto* lines = dl.Section("tiers"))   // dragon_1 = 1  (squad section = from player level)
				for (auto& [sec, lvl] : *lines)
					if (c.Squad(Trim(sec))) d.tiers.emplace_back(Trim(sec), std::atoi(lvl.c_str()));
			std::sort(d.tiers.begin(), d.tiers.end(), [](auto& a, auto& b) { return a.second < b.second; });
			if (auto* lines = dl.Section("places"))
				for (auto& [k, v] : *lines) {
					float w = 0.f;
					ParseFloat(Trim(v), w);
					try { d.places[FormID(std::stoul(Trim(k), nullptr, 16))] = w; } catch (...) {}
				}
		}
		if (dp.LoadFile(dir + "dragon_places.ltx", nullptr))
			if (auto* lines = dp.Section("word_walls"))
				for (auto& [k, v] : *lines)
					try { d.walls[FormID(std::stoul(Trim(k), nullptr, 16))] = std::max(1, std::atoi(v.c_str())); } catch (...) {}
	}
	return !c.squads.empty();
}

DragonNumbers ScaledDragons(const DragonNumbers& base, float s)
{
	DragonNumbers n = base;
	s = std::clamp(s, 0.f, 10.f);
	n.maxDragons = base.maxDragons * s;                                      // 0: no dragons
	n.respawnDays = s > 0.f ? base.respawnDays / double(s) : base.respawnDays * 1000.0;
	n.levelMult = base.levelMult * s;
	return n;
}

float DragonWeight(const Config& c, const GraphNode& n)
{
	const auto& d = c.dragons;
	if (n.ws != 0 || n.city || n.settlement || n.inn || n.playerHome || n.disabled) return 0.f;   // Tamriel only; never where people live
	float w = 0.f;
	if (n.kind == Kind::DragonLair) w = d.lairWeight;
	if (d.walls.count(n.location)) w = std::max(w, d.wallWeight);
	if (n.kind == Kind::Wilderness && n.z >= d.highZ) w = std::max(w, d.highWeight);
	if (auto it = d.places.find(n.location); it != d.places.end()) w = it->second;
	return std::max(0.f, w);
}

void ApplyProps(const Config& cfg, GraphNode& n)
{
	const Ltx& p = cfg.props;
	std::string preset = p.Get("kind_map", KindName(n.kind), "default");
	char loc[32];
	std::snprintf(loc, sizeof(loc), "loc_%X", n.location);
	if (p.Has(loc)) preset = loc;                 // per-location override section [loc_1917D]:p_fort ...
	if (n.ws == 1) {                              // Solstheim: Dragonborn.esm's own locations by plugin-local id, [loc_dlc2_143BB]
		std::snprintf(loc, sizeof(loc), "loc_dlc2_%X", n.location & 0xFFFFFF);
		if (p.Has(loc)) preset = loc;
	}
	if (!p.Has(preset)) preset = "default";
	n.preset = preset;

	n.props = Props{};
	n.props.base = p.GetF(preset, "base");
	n.props.territory = p.GetF(preset, "territory");
	n.props.resource = p.GetF(preset, "resource");
	n.props.surge = p.GetF(preset, "surge");
	n.props.lair = p.GetF(preset, "lair");
	n.props.all = p.GetF(preset, "all");
	n.props.monster = p.GetF(preset, "monster");
	for (int f = 0; f < kFactionCount; ++f) n.props.byFaction[f] = p.GetF(preset, FactionId(Faction(f)));
	n.maxPopulation = p.GetI(preset, "max_population", 2);
	n.monsterCap = p.GetI(preset, "monster_population", 0);
	n.arriveDist = p.GetF(preset, "arrive_dist", 600.f);
	n.simAvail = p.GetB(preset, "sim_avail", true);
	n.raidable = p.GetB(preset, "raidable", true);
	n.factionControlled = p.GetB(preset, "faction_controlled", false);
	n.respawnIdleHours = p.GetF(preset, "respawn_idle", cfg.defaultRespawnIdle);
	n.respawnRadius = p.GetF(preset, "respawn_radius", cfg.defaultRespawnRadius);

	// default faction: a fixed id, "hold" (the hold's aligned faction) or none
	n.defaultFaction = kNoFaction;
	const std::string df = Lower(p.Get(preset, "default_faction", "none"));
	n.holdAligned = df == "hold" && cfg.holdFaction.count(n.hold) != 0;
	n.ownerBonus = p.GetF(preset, "owner");
	if (df == "hold") {
		auto it = cfg.holdFaction.find(n.hold);
		if (it != cfg.holdFaction.end()) n.defaultFaction = std::uint8_t(it->second);
		else if (n.ws < kWorldspaces) n.defaultFaction = cfg.wsFaction[n.ws];   // no hold: the worldspace's own side
	} else if (df != "none" && !df.empty()) {
		bool ok = false;
		Faction f = FactionFromId(df, &ok);
		if (ok) n.defaultFaction = std::uint8_t(f);
	}
	// "owner" coefficient: weight for the default (hold-aligned) faction, so cities/towns welcome their own side
	if (n.defaultFaction != kNoFaction) n.props.byFaction[n.defaultFaction] += n.ownerBonus;

	// keyword overlays ([keyword_props] <keyword hex> = section): a Nordic ruin is a draugr lair whatever its kind preset says.
	// Numbers add up (max_population too), respawn entries are appended.
	std::vector<std::string> respawnNames = p.List(preset, "respawn");
	if (auto* overlays = p.Section("keyword_props")) {
		std::vector<std::string> applied;   // two keywords naming one overlay (Nordic ruin + crypt) apply it once
		for (auto& [key, sec] : *overlays) {
			FormID kw = 0;
			try { kw = FormID(std::stoul(key, nullptr, 16)); } catch (...) { continue; }
			if (std::find(n.keywords.begin(), n.keywords.end(), kw) == n.keywords.end() || !p.Has(sec)) continue;
			if (std::find(applied.begin(), applied.end(), sec) != applied.end()) continue;
			applied.push_back(sec);
			n.props.base += p.GetF(sec, "base");
			n.props.territory += p.GetF(sec, "territory");
			n.props.resource += p.GetF(sec, "resource");
			n.props.lair += p.GetF(sec, "lair");
			n.props.all += p.GetF(sec, "all");
			n.props.monster += p.GetF(sec, "monster");
			for (int f = 0; f < kFactionCount; ++f) n.props.byFaction[f] += p.GetF(sec, FactionId(Faction(f)));
			n.maxPopulation += p.GetI(sec, "max_population", 0);
			n.monsterCap = std::max(n.monsterCap, p.GetI(sec, "monster_population", 0));
			n.respawnIdleHours = std::max(n.respawnIdleHours, p.GetF(sec, "respawn_idle", 0.f));   // a lair restocks no faster than this
			for (auto& r : p.List(sec, "respawn"))
				if (std::find(respawnNames.begin(), respawnNames.end(), r) == respawnNames.end()) respawnNames.push_back(r);
		}
	}

	// respawn entries: respawn = a, b  -> sections [spawn_a], [spawn_b]
	n.respawn.clear();
	for (auto& name : respawnNames) {
		const std::string sec = "spawn_" + name;
		if (!p.Has(sec)) continue;
		RespawnParam rp;
		rp.name = name;
		rp.squads = p.List(sec, "squads");
		rp.num = p.GetF(sec, "num", 1.f);
		if (name == "patrol" || name == "owner_patrol") rp.num *= cfg.compat.patrolScale;   // patrol mods already fill the roads
		const std::string f = Lower(p.Get(sec, "faction", "none"));
		if (f == "controller") rp.faction = kController;
		else if (f == "owner") {
			if (n.defaultFaction == kNoFaction) continue;   // nobody owns this place: nothing to respawn
			rp.faction = n.defaultFaction;
			rp.owner = true;
		} else {
			bool ok = false;
			Faction ff = FactionFromId(f, &ok);
			rp.faction = ok ? std::uint8_t(ff) : kNoFaction;
		}
		if (!rp.squads.empty()) n.respawn.push_back(std::move(rp));
	}
	ApplyHandover(cfg, n);   // last: a patrol mod's garrison overrides the preset (docs/COMPAT.md)
}

}  // namespace ALife
