# Mutants: wildlife, draugr and falmer squads (phase 2, milestone M4)

Anomaly's A-Life runs two kinds of squads on one board: stalker squads (factions) and mutant squads. ALife 2.0 ported
the stalker side. This document is the mutant side, mapped onto Skyrim's wildlife and its undead and Falmer.

## 1. What Anomaly does (1.5.3, read from `ref/anomaly`; not redistributed)

| Rule | Anomaly source | Behaviour |
|---|---|---|
| Monster recognition | `is_squad_monster[player_id]` | The squad's community (`monster_predatory_day`, `monster_predatory_night`, `monster_zombied_day`, `monster_zombied_night`, `monster_vegetarian`, `monster_special`) says it is a monster and which behaviour it has |
| Priors | `simulation_objects.script` `default_monster_behaviour` | `lair = 1, monster = 1, territory = 1`, everything else 0 (stalkers: `all = 1, base = 1, resource = 2, territory = 1, lair = 0`). Prior = 5 + props[community] + sum(w * props), times the distance factor; full places (population >= max_population) score 0 |
| Which places a monster accepts | `smart_terrain.script` `target_precondition` | Only places with `all`, `all_monster` or its own community key > 0; then a lair is valid, or a territory when its time window is open. Stalkers never look at `lair` |
| Lairs | `sim_board.general_lair_precondition` | Always true: "will fill up all the lairs first, by monster squads" (lair weight + always open = lairs win first, territories take the overflow) |
| Territory windows | `sim_board.general_territory_precondition` | predatory/zombied **day**: 06-19; predatory/zombied **night**: 19-06; vegetarian: always |
| Online windows | `sim_squad_scripted.script` (offline monster squads) | day types never come online 22:00-05:59; night types never 06:00-18:59; a squad held offline is sent back to its own place |
| Respawn | `smart_terrain.script` respawn params | Same machinery as stalkers (`respawn_params`, `respawn_idle`, `respawn_radius`); mutant lists are scaled by a separate population factor |
| Relations | `sim_squad_scripted.set_squad_relation` | Monsters have no relation book: they are enemies of every stalker community and leave each other alone |
| Offline combat | `sim_offline_combat.script` | Monsters fight like anyone else; their power is a per-species class value times a rank, no rank gain |

## 2. Skyrim mapping

User decisions (Oct 9 2026, project notes): vanilla, Dawnguard and Dragonborn creatures
only; the one Creation Club exception is "Plague of the Dead" for Anomaly's zombies. The core set below is built now;
the rest of the table (section 2.1) is milestone M4b.

Six monster factions are appended to `enum Faction` (ids 15-20 when M4 added them, after the 15 human factions, so every saved
human id keeps its value; since co-save 208 the three Solstheim factions sit before them, so they are ids 18-23 today:
`include/ALife/Core.h` `Faction`, docs/SOLSTHEIM.md). A faction is a behaviour (its Anomaly community); several creatures can share one:

| Faction id | Name | Anomaly community (territory hours; never online) | Members (Skyrim.esm unless noted) | Lairs (location keywords) |
|---|---|---|---|---|
| `wildlife` | Wild beasts | `predatory_day` (06-19; 22-05) | wolf packs (`LCharWolf` 0B83C2; the larger packs led by an alpha, the ice wolf `EncWolfIce` 023ABF), skeever packs (`EncSkeever` 023AB7), sabre cats (`LCharSabrecat` 0FE2D5), bears (`LCharBearAll` 042266) | LocTypeAnimalDen 130DE (den), LocSetCave 130EF / LocSetCaveIce 100819 (one cave in four) |
| `troll` | Trolls | `predatory_night` (19-06; 06-18) | `EncTroll` 023ABA, `EncTrollFrost` 023ABB; one or two, a rare top predator (Anomaly chimera) | caves (one in eight) |
| `spriggan` | Spriggans | `vegetarian` (any hour) | `LCharSpriggan` 10EC84 (spriggan, matron) | LocTypeSprigganGrove 130EA |
| `draugr` | Draugr | `zombied_night` (19-06; 06-18) | `LCharDraugrMeleeAllMale` 055955, `LCharDraugrMissile` 01E7AA, `LCharDraugrWarlock` 01E7AB, boss `LCharDraugrBossNoDragonPriest` 0DD9D8 | LocSetNordicRuin 130F2, LocTypeDraugrCrypt 130E2 |
| `falmer` | Falmer | `predatory_night` (19-06; 06-18) | `LCharFalmerMelee` 01E77D, `LCharFalmerMissile` 01E77E, `LCharFalmerShaman` 01E77F, boss `LCharFalmerBoss` 05238F | LocTypeFalmerHive 130E4, LocSetDwarvenRuin 130F0 |
| `zombie` | The Plague | `zombied` (any hour) | `ccBGSSSE003_LCharBGSSSE003ZombieMelee`, local id 822 in `ccbgssse003-zombies.esl` (Plague of the Dead; masters: the base game and DLCs only) | LocTypeWarlockLair 130EC (necromancers' work), LocTypeShipwreck 1929F (the drowned) |

**M4b** (Oct 9 2026) appends seven more (ids 21-27 then, 24-30 since 208) and adds kinds to existing factions. Dawnguard / Dragonborn records
are named `Dawnguard.esm|local id` / `Dragonborn.esm|local id` like the zombies; Dragonborn's own location keywords are
stored as `04xxxxxx` (the vanilla load order) whatever index the game gave the plugin, so `[keyword_props]` can name them.

| Faction id | Name | Anomaly community | Members | Lairs |
|---|---|---|---|---|
| `wildlife` (+) | | | horkers `EncHorker` 023AB1 | shipwrecks (with the zombies) |
| `falmer` (+) | | | shaman bands (`LCharFalmerShaman` + a chaurus), chaurus `LCharChaurus` 01FA27, chaurus hunters (Dawnguard 0029A2) | their hives |
| `zombie` (+) | | | a necromancer (`LCharWarlockNecromancer` 01E777) leading Plague zombies; ash spawn (Dragonborn 0322BD) | warlock lairs; DLC2LocTypeAshSpawn 402C969 |
| `hound` | Death hounds | `predatory_night` | `DLC1LvlDeathHound` (Dawnguard 017EC2) | LocTypeVampireLair 130EB |
| `werebeast` | Werebeasts | `predatory_night` | `LCharWerewolf` 01E791, boss `LCharWerewolfBoss` 0A1981; werebears (Dragonborn 0322B1) | LocTypeWerewolfLair 130ED; LocTypeWerebearLair 402C96A |
| `riekling` | Rieklings | `predatory_day` | rieklings (Dragonborn 01B653 / 01B654), riders on bristlebacks (038EB0), wild bristlebacks (025F9A) | DLC2LocTypeRieklingCamp 402972E |
| `spirit` | Spirits | `zombied_night` | ghosts `LCharGhostWizard` 104B62, a wispmother `EncWispMother` 023ABD | Nordic ruins (beside the draugr, one in four) |
| `hagraven` | Hagravens | `predatory_day` | `EncHagraven` 023AB0 (friends of the Forsworn) | LocTypeHagravenNest 130E6 |
| `giant` | Giants | `vegetarian` | a giant `LCharGiant` 030529 leading mammoths `EncMammothWild` 023AB4 | LocTypeGiantCamp 130E5 |
| `dwemer` | Dwemer automatons | `special` (lairs only, any hour) | spiders 10EC90, spheres 10EC8F, a centurion 10FCE5 leading veterans | LocTypeDwarvenAutomatons 130E3; Dwemer ruins (beside the falmer) |

`special` is Anomaly's `monster_special`: the squad keeps to its lairs (no territory window) and may come online at
any hour. Death hounds and spirits leave vampires (and necromancers) alone, hagravens the Forsworn; the rest are
enemies of every human faction.

Zombies are a soft dependency: `monsters.ltx` names the creature as `plugin|local id`; the plugin resolves it after
the data has loaded, and if `ccbgssse003-zombies.esl` is not in the load order the creature is dropped with one log
line, every squad that has no creature left is never made, and the board runs without zombies. Nothing of that
plugin's own quest (its encounters, the `ZombieEncountersEnabled` / `ZombieNightAttacks` globals) is read or changed:
ALife only places actors from its leveled list. The Ghosts of the Tribunal ash zombies (`ccasvsse001-almsivi.esm`)
are not used.

On the real map (`data/world_skyrim.tsv`, 337 smart terrains): 34 Nordic ruins and 21 crypts (draugr), 13 falmer hives
and the Dwemer ruins (falmer), 17 animal dens and 76 caves (wildlife, trolls), 8 spriggan groves, 18 warlock lairs and
7 shipwrecks (zombies). Many carry another kind too (bandit camps, warlock and vampire lairs in Nordic ruins): the
people there and the lair's monsters share the place.

Anomaly behaviours copied in the core set: packs have an alpha (the squad's first member is its `leader` creature:
an ice wolf for the larger wolf packs, a deathlord or gloomlurker for veteran draugr and falmer bands); hunters are drawn to dens
and caves (`hunter` key in those overlays), where they meet the beasts. Left for M4b: mutants feeding on corpses (not implemented on master: branch `t10b-feeding` is not merged).
Lair hunters (M5g): factions in `alife.ltx [activity_factions] hunt_lairs` (hunters) target a lair while a monster
squad holds it (priority `hunt_lair_weight`); `harvest_parts` factions (hunters, wanderers) take each beast's
`monsters.ltx parts` (wolf / ice wolf / sabre cat / bear pelts, bear claws, skeever tails, troll fat, spriggan sap,
bone meal, falmer ears) from offline kills and sell them to traders on their next trade stop. Bridge `hunters` lists them.

### 2.1 Full Anomaly -> Skyrim mapping (user-approved; core = M4, M4b = the rest, both done)

| Anomaly | Skyrim | Set |
|---|---|---|
| blind dog / pseudodog | wolf / ice wolf packs | core |
| rat / tushkano | skeevers | core |
| cat / lurker | sabre cats | core |
| chimera | trolls / frost trolls, rare top predator | core |
| (wildlife) | bears | core |
| (Skyrim lairs) | spriggan groves | core |
| (undead) | draugr in barrows / Nordic ruins | core |
| (Dwemer ruins) | falmer | core |
| zombie, zombied stalkers | Plague of the Dead zombies | core |
| psy-dog | death hounds (Dawnguard) | M4b (done) |
| flesh | horkers (coast) | M4b (done) |
| boar | bristlebacks + riekling riders (Dragonborn) | M4b (done) |
| bloodsucker | werewolves (werebears on Solstheim), night-only predators | M4b (done) |
| snork / karlik | riekling raiding packs | M4b (done) |
| burer | falmer shamans as their own psychic caster bands | M4b (done) |
| controller | a necromancer or lich leading zombie squads | M4b (done) |
| poltergeist | wispmothers / ghosts tied to ruins | M4b (done) |
| fracture | hagravens | M4b (done) |
| pseudogiant | giants + mammoths at giant camps | M4b (done) |
| (Skyrim lairs) | chaurus nests, Dwemer automatons, ash spawn on Solstheim | M4b (done) |

Behaviour is data, not code: `factions.ltx [monsters]` names the Anomaly behaviour of each monster faction, so a
faction can be made a day hunter or a grazer (`vegetarian`) without a rebuild. Adding a *new* monster faction still
needs an enum entry (fixed-size per-faction tables).

Blackreach is its own worldspace with no Tamriel world marker, so it is not on the graph; Falmer use the surface hives
and Dwemer ruins. Giants and hagravens stayed vanilla until M4b (both are squads now, section 2); dragons are squads since
M4c (docs/DRAGONS.md). `kind_map` still gives their places the neutral `default` preset; the giant and hagraven lairs come from
the `[keyword_props]` overlays (`kw_giant_camp`, `kw_hagraven_nest`).

## 3. Lair and territory props (`config/props.ltx`)

- **Keyword overlays** `[keyword_props]`: `<keyword FormID hex> = <section>`. After the kind preset (and any
  `[loc_...]` override) is applied, every overlay whose keyword the location carries is *added*: numeric props add
  up, `max_population` adds, `respawn` entries are appended, `monster_population` and `respawn_idle` take the larger
  value. Two keywords naming the same overlay apply it once (a Nordic ruin that is also a crypt). That is how "a Nordic
  ruin is a draugr lair" stays independent of whether the classifier called the place a dungeon or a crypt.
- **`monster` prop** (Anomaly `all_monster`): any monster squad may target the place. `all` stays the stalker-only
  "anyone" key, as before. A monster squad accepts a place only when `monster > 0` or its own faction key > 0.
- **Lairs**: `lair = 1` plus the owner key (`draugr = 2` ...). The overlay also adds one to `max_population`, so a
  barrow has room for its draugr and a visiting party of adventurers, and sets `monster_population = 1`: one monster
  squad per lair (the squad does not count against its own lair). That cap is what makes Anomaly's "lairs fill up
  first" visible: a squad that leaves its den finds the next *empty* lair, and only the overflow goes out on
  territories. Without it, squads bunched up three to a lair and a third of the lairs stood empty.
- Lairs restock no faster than every 24 game hours (`respawn_idle = 24` in the overlay; a place's own preset may be slower).
- **Territory**: the open wild (`p_wild`) gets `monster = 1`: packs roam there by day, draugr and falmer by night.
- Human squads ignore `lair` (Anomaly), but they still visit crypts, ruins and caves for `territory`/`resource`, and
  that is where they meet the monsters.

## 4. Board rules (engine-free core, `src/core/Board.cpp`)

- `Config::monster[f]` (from `[monsters]`) marks a monster faction and its behaviour.
- `EvaluatePrior`: monster squads use the monster weights `[monster_behaviour]` (`lair = 1, monster = 1,
  territory = 1`, rest 0) over the props, plus `props[faction]`; no need bias (monsters do not eat, trade or rest at inns).
- `TargetPrecondition`: monsters: accepted (`monster` or own key > 0) and (lair, always) or (territory and the window
  is open). The time check runs every step against the current target (`ignorePopulation`), so when the window closes a
  roaming pack drops its territory and heads for a lair (Anomaly does the same through its re-evaluation).
- `MonsterMayComeOnline(g, hour)`: the Anomaly online windows above; the plugin asks before bringing a monster squad
  online.
- Two Skyrim additions in `[monster_behaviour]` (not in Anomaly), so lair dwellers behave like lair dwellers:
  `home = 3` (extra priority for the lair that spawned the squad: draugr keep to their own tomb) and
  `lair_idle_min/max = 12/36` (a monster squad stays 12-36 game hours once it is in a lair, people 2-8 h anywhere).
- Monsters skip: needs, activities (trade/hunt/raid), looting, rank growth from kills, and smart-terrain control
  (a wolf pack in a fort never makes the fort "wildlife-held", so garrison respawns are never wolves). They still
  count for `war` (hostile factions sharing a place) and fight through OCS.
- Members of a monster squad come from `squads.ltx` `creatures = id:weight, ...` (plus `leader = id` for the
  first member); each id is a `[creature_<id>]` in `config/monsters.ltx` (display name, the leveled list or actor base
  to spawn, health and damage at level 1 and per level). Offline power uses the same formula as human members,
  `sqrt(damage * hit * health) * 10`, so monsters and people are on one scale. At level 15 a wolf is about 0.65 of a
  bandit, a draugr or falmer about 0.9, a sabre cat 1.35, a bear or a deathlord about 1.8. Monsters gain no rank;
  the level pacer raises their level and health with the player like everyone else.

## 5. Relations (`config/factions.ltx`)

- Every monster faction is an enemy of all 18 human factions (Anomaly: no relation book, everyone shoots them),
  except `draugr` and `zombie` vs `necromancer` and `vampire` (neutral: undead and the people who raise them).
- Monsters are neutral to each other (Anomaly mutants do not hunt each other offline).
- The player: monster squads use the vanilla creatures' own aggression online; standing, dialogue, tolls and
  goodwill skip monster factions entirely (no MCM row, no "regard" tier, no notoriety for killing a wolf).

## 6. Online (plugin)

- `src/Creatures.cpp` owns monster actors. Coming online, the slot marker is placed exactly like a human squad's
  (out of view, at least 4000 units away, on the navmesh); then each living member is placed from its creature's actor
  base, or from its leveled list levelled by the plugin itself (entries up to the player's level: all of them with
  "calculate from all levels", else the highest such level; nested lists recurse), wounds carried over as health damage.
  `PlaceObjectAtMe` with a leveled list gives no actor (CreateReferenceAtLocation does not level it).
- A `plugin|local id` creature keeps its local id as its key (saved in the members) and is looked up in its plugin when
  placed, so a load-order change keeps it.
- `Pool::ActorOf(member)` returns the creature for monster members, so the online tick, verify pass and offline read-back
  keep working unchanged; `Pool::IsPoolActor` stays false for creatures, so dialogue, standing, protection and looting
  never see them.
- Online monsters run their vanilla AI (wander / sandbox around where they were placed, attack on sight by their own
  aggression). The board treats an online monster squad as arrived (Anomaly `always_arrived`) and moves it on once it
  is offline again. Human squads nearby fight them through the engine and the existing "start combat with an enemy
  squad" pass.
- Going offline: dead creatures are marked dead in the squad (bodies stay in the world for the player to skin), living
  ones are disabled at once and deleted 5 s later (deleting a creature placed a second earlier crashed in its movement
  controller).
- Respawn is the board's ordinary respawn (lair entries `spawn_draugr` ...), never within `respawn_radius` of the player.

## 7. Co-save

Version 207 (M4b). The per-faction goodwill table is written with its length (28 entries in 207; 32 today: co-save 208 with the
Solstheim factions, plus M4c's dragon; `include/ALife/SaveFormat.h`); a 206 save is read
with 21, a 205 save with 15, and factions added since start at 0. After a load, every monster kind the save has no
squads of (all of them in a 205 save, the M4b kinds in a 206 save) gets the start fill of its respawn entries only, so
its lairs are populated at once instead of over days of respawn ticks.

## 8. Checks

- Headless (`tests/harness.cpp --mutants`, `--lairs` lists every lair): 30 game days over the real map; exit 1 unless
  stocked lairs (those that keep a monster squad) are held by a monster squad on >= 75% of hourly samples after day 1,
  monsters spend >= 60% of their staying time in lairs, no monster ever targets a non-lair place outside its window,
  every monster faction does roam territories (only inside its hours), the online windows answer as above, monsters
  and factions have killed each other offline, and no place is ever monster-held.
- Roaming is required only of species with hours (day / night) and at least 5 squads; any-hour species (spriggans,
  zombies) and rare ones are reported, not required.
- Result, full core set (5 seeds, Oct 9 2026): all checks pass on every seed. Stocked lairs held 81-82% (wildlife ~89%,
  falmer ~95%, spriggans ~88%, zombies ~84%, trolls 65-86%, draugr 59-68%: adventurers keep clearing barrows), 96-98%
  of monster staying time in lairs (spriggans ~84%: grazers roam any hour), 0 window violations, about 1,450 people and
  1,450 monsters killed offline per 30 days, ~305 squads. Human squad counts end where they do without monsters, but the
  factions that go into dungeons and lairs lose squads 2-3x as often (wanderers 184 wiped vs 77, Dawnguard 55 vs 0 at
  the zombie-held warlock lairs): tune with the spawn `num` values, the creature stats and `respawn_idle`.
- The harness takes the zombie creature as available (it has no load order); in game it exists only with
  `ccbgssse003-zombies.esl` loaded.
- Lab (a separate MO2 test instance, background-only via `tools/lab_mutants.sh` + `guarded_launch.ps1`): a draugr squad online at a
  barrow at night, a wildlife pack online in the wild by day, Plague of the Dead zombies online, save/load keeps the
  monster squads (co-save 206; a 205 save still loads), `ALife.log` without errors.
- Result (Oct 9 2026, T2 run lab-0451): wolf pack #225 (alpha ice wolf + 5 wolves) online at Brood Cavern at hour 8; draugr
  #288 (deathlord, warlock, archer, draugr) online at Dead Men's Respite at hour 19; zombies #449 (3, bases from
  `ccbgssse003-zombies.esl`) online at Harmugstahl; save squads=314 = load 314 (v206), the old 205 save loads 201; no
  errors, no crash, 0 watchdog strikes. A wildlife squad *on a territory* was never found in the first hour after the
  fill (packs fill the lairs first; the harness shows them roaming over the days), so the pack was checked at its den.
  Two fixes came out of it: leveled lists placed no actor (every monster squad was skipped online) and a crash when a
  pack placed a second earlier went offline.
- M4b (Oct 9 2026): harness `--mutants` passes on seeds 1-5 with the new check "every monster kind is made" (each kind
  with a lair on the Tamriel map spawns; Solstheim-only rieklings are reported with *). Seed 1: stocked lairs held 90%
  (hounds 95%, werebeasts 97%, spirits 86%, hagravens 96%, giants 89%, automatons 100%), 94% of monster staying time in
  lairs, automatons never on a territory. Guarded lab run `recon/m4b/lab-1300`: giant + mammoth at Bleakwind Basin,
  hagravens at Dead Crone Rock, dwarven spheres at Alftand, death hounds (Dawnguard bases) at Pinemoon Cave, a werewolf
  beastmaster with werewolves at Bloated Man's Grotto, ghosts at Silverdrift Lair; 4 riekling squads on Solstheim.
  Save 391 = load 391 (v207); the T2 v206 save loads 314 and stocks 62 squads for the seven new kinds; the 205 save loads
  201 and stocks all thirteen. No errors, no crash, 0 watchdog strikes.
