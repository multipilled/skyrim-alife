# Dragons as A-Life actors (M4c, PARITY G20)

Anomaly's helicopters ported as Skyrim's dragons: a few dragons live on the board like any monster squad, fly straight
between dragon places (lairs, word walls, high country), stay a while, hunt the squads they find there (offline through the
combat simulator, on screen through the vanilla dragon AI) and come on screen near the player under the same rules and
timer as vanilla's random dragon attacks. Status (Oct 18 2026): step 2 (core, harness `--dragons`, plugin side) merged to master (cfbc409, Oct 18);
no game run yet (lab check scheduled by the coordinator). Implementation notes: section 11.

Marks: **[V]** verified (read from the data or the code, with where), **[I]** inferred from verified facts, **[U]** unverified:
to check in step 2 (code) or step 3 (lab) before relying on it.

## 1. Anomaly reference (`heli_alife.script`, local only, read Oct 15)

- [V] A heli targets places whose `props[heli community] > 0` on its level (flat prior 5), picks one of up to 5 at random, flies
  there (climbs to target height + 70 first), stays `STAY_POINT_IDLE` = 6800 game s (~1.9 h), then retargets.
- [V] It can be sent to a squad of its own community that calls for help (`force_target_destination`); it circles a squad
  target (`GoPatrolByRoundPath`, radius 50-70) and fights through `heli_combat`.
- [V] Stalker panic (`axr_stalker_panic.script:73-101`, PARITY G14): novices panic at the big mutants, trainee/experienced at
  fewer, veterans never.

| Anomaly | ALife |
|---|---|
| heli community, one per level, sim targets with a `heli` prop | `Faction::Dragon` (a monster faction), squads of one dragon, places with the `dragon` prop |
| climbs, flies straight to the target | flies straight (not the road graph) at `fly_speed`, any hour |
| stays 6800 s, then the next target | stays `stay_min..stay_max` game hours, then the next target |
| `heli_combat` against what it meets | offline: the combat simulator against squads at its place; on screen: the vanilla dragon AI |
| sent to help its own community | none: dragons answer to nobody (no SOS) |
| stalker panic | novice and advanced human squads that share a place with a dragon flee instead of fighting |

## 2. Vanilla gate and timer (read from Skyrim.esm with tools/esp.py, read-only)

- [V] GLOB `DragonsReturned` 0x042851 (MQ105's script property `DragonsEnabled` points at it) is set to 1 by MQ105 Fragment_9
  ("enable dragons in the world", with `DragonEnableMarker.Enable()`) once Mirmulnir dies in MQ104 (MQ104 0x02610C sets MQ105
  stage 5 / 10). GLOB 0x09F24F `DragonsEnabled` is unused (always 0): never read it.
- [V] Vanilla random dragon attacks: story manager node `WEDragonQuests` (SMQN 0x09F362) needs `DragonsReturned == 1`,
  `MQ106TurnOffRandomDragons` (0x0C65BB) `== 0` and `WINextDragon` (0x0E518E) `< GameDaysPassed`.
  `WIFunctionsScript.updateWIDragonTimer()` sets `WINextDragon = GameDaysPassed + WIWaitDragon` (0x0E518F: 3 days; +3 after MQ305).
- Gate (design): **no ALife dragon exists before `DragonsReturned == 1`**. The existing `[faction_gates]` path only knows quest
  stages (`plugin|quest:stage`, [V] core/Config.cpp:288); it gains a global form `plugin|glob=value` (the gate opens when the
  global's value is at least `value`): `dragon = Skyrim.esm|042851=1`. The plugin reads it at every load and on a slow timer
  (the global changes in a quest fragment, with no event to hook). [I] The board's existing `SetGated` / `Blocked` /
  `ApplyFactionState` then do the rest: shut = no dragon squads made, offline ones retire.
- On-screen timer (design): an offline dragon may come on screen near the player only when `MQ106TurnOffRandomDragons == 0` and
  `WINextDragon < GameDaysPassed`; when one does, the plugin sets `WINextDragon = GameDaysPassed + WIWaitDragon` exactly as
  `updateWIDragonTimer` does, so ALife and vanilla dragon attacks share one timer and never double up. [U] Writing the two
  globals from SKSE (`TESGlobal::value`) is what the vanilla script does through Papyrus; check in the lab that the story
  manager then holds back its own WE dragon for the 3 days.
- Off screen, the timer does not apply: dragons fly, stay and fight on the board whatever the timer says (the timer only spaces out
  what the player sees, like vanilla).

## 3. Dragon bases: scripted, not NoScript

- [V] The vanilla leveled list `LCharDragonAny` 0x05EACF (flags 3; levels 1 / 18 / 27 / 36 / 45) holds the
  `EncDragon0xxxNoScript` bases, which have **no VMAD**: no `DragonActorScript`, so no soul absorb, no flyby / landing effects.
- [V] The scripted bases (all race 0x012E82): `EncDragon01Fire` 0x01CA03 (L10), `EncDragon01Frost` 0x0F80FA, `EncDragon02Fire`
  0x0F80FD / `EncDragon02Frost` 0x0F77F8 (L20), `EncDragon03Frost` 0x0351C3 (L30), `EncDragon04Fire` 0x0F811B / `Frost` 0x0F811A
  (L40), `EncDragon05Fire` 0x0F811C / `Frost` 0x0F811E (L50).
- Design: monsters.ltx `[creature_dragon_<n>]` per tier with the scripted base as `form` and its own level table (the board picks
  the tier by player level, like `LCharDragonAny`'s 1 / 18 / 27 / 36 / 45 steps), so a dragon killed on screen gives the player a
  soul and leaves bones. [I] The scripted bases are the ones vanilla's WE dragon encounters place, so placing them outside a quest
  is a normal use; [U] check in the lab that `DragonActorScript` runs its landing / soul absorb without a quest alias.
- The member's creature `id` keeps the tier, so a saved dragon comes back as the same kind (fire / frost by a roll at creation).
- A dragon killed on screen keeps its skeleton until its cell unloads (the player loots bones and scales); `Creatures` must not
  disable and delete it the way it does its other creatures once they go offline ([V] src/Creatures.cpp `gDoomed`: disabled, deleted a few seconds later); dragons need that exception.

## 4. Places

- [V] 10 vanilla dragon lairs are graph nodes (keyword LocTypeDragonLair 0x130E0 -> `Kind::DragonLair`, [V] core/Config.cpp:78):
  DragonLair01-10 except the empty DragonLair3, plus Mount Anthor and Skyborn Altar.
- [V] Word walls (`WordWallTrigger` ACTI 0x05095E, 47 refs) sit in 43 locations, 40 of them graph nodes (Nordic ruins, the dragon
  lairs, Hag's End, Dead Crone Rock ...); leave out DarkBrotherhoodSanctuary 0x3444E and Warehouse 0xE9DA7.
- Design: `config/dragons.ltx [places]` lists the dragon places by location FormID with a weight: lairs 3, word walls 2, and open
  high country (mountain wilderness nodes, by a z threshold) 1. A tool (`tools/gen_dragon_places.py`, read-only on Skyrim.esm via
  tools/esp.py, like gen_positions.py) writes the word-wall list so nobody types 40 FormIDs by hand. [U] The z threshold for
  "high country" is a step-2 choice (look at the node heights in data/world_skyrim.tsv).
- Never a city, settlement, inn or player home: dragons over Whiterun are a quest event, not A-Life.
- [V] Vanilla lairs already hold their own dragon (placed refs). An ALife dragon **staying at a vanilla lair stays offline**: the
  lair's own dragon is the one the player meets there; ours leaves when its stay is over.
- Solstheim: no dragons there in this step (the Dragonborn DLC's dragons are quest-driven); `[places]` holds Tamriel only.

## 5. The offline dragon (core, engine-free)

- `Faction::Dragon` appended after Dwemer: index 31, `kFactionCount` 32 ([V] Core.h: Dwemer is 30, COUNT follows). Appending keeps
  every saved faction index valid. A monster faction (`IsHumanFaction` false: no pool actors, standing or dialogue).
- New `MonsterType::Flyer` (monsters.ltx `type = flyer`): targets only places with the `dragon` prop (dragons.ltx weights), any
  hour; flies in a straight line at `fly_speed` world units per game hour (default 120000, ~6x walking: [V] Config.h walkSpeed
  20000), ignoring the road graph and the water map; one dragon per place.
- Dragons do not count toward a place's `population` / `monsters` (they never block a farm's respawn, never make a lair "held"
  for lair hunters, never change who controls a place: [V] UpdateControl already skips monsters).
- How many: `max_dragons` world-wide (default 2) x `Diff(Dragons)` (T21, docs/DIFFICULTY.md: 1.0 at Adept), at least 0. A new
  dragon is made at a random lair when there are fewer than that and `respawn_days` (default 3) have passed since the last one
  was made or killed. Its level tier from the player's level; `Diff(Dragons)` also moves the tier (x the level, rounded to a tier).
- Target choice: weight from dragons.ltx x (1 + `prey_bonus` when human squads stay there), straight-line distance factor like
  `EvaluatePrior`, best of 5 / random of 5 as `GetSquadTarget` does; never the place it just left.
- Fights: a dragon at a place fights the squads staying there through the existing combat simulator (`Board::Battle`), with its
  power from monsters.ltx (big: a dragon beats most novice squads). Before the fight, novice and advanced **human** squads at the
  place flee instead (G14: they retreat like a lost fight, `retreatUntilHours`, no losses); veterans and other monsters fight.
  A dragon never fights another dragon.
- A dragon killed offline is gone (no soul: the player was not there); news: "A dragon fell near X, brought down by the Y"
  (the existing death-news path, monsters excluded today: dragons are the exception).
- Dynamic relations, standing and tasks ignore dragons (no goodwill for killing one; no bounty on a dragon) in this step.

## 6. The online dragon (plugin)

- A dragon squad comes on screen through the existing monster path ([V] src/Creatures.cpp: members placed from their monsters.ltx
  form, own AI, wounds back to the board when offline) with three differences:
  - **distance**: it comes on screen when the player is within `online_distance` (default 16000, ~4 cells: dragons are seen from
    afar) instead of the squad switch distance, and goes offline beyond `offline_distance` (default 24000);
  - **gate**: only when the timer of section 2 allows it (else it stays offline even when near), and never at a vanilla lair;
  - **placement**: above the place's world marker (z + `spawn_height`, default 2500) so it arrives flying, not on the ground.
- On screen it is a vanilla dragon: it attacks the player and anything hostile, ALife squads on screen included (the engine
  decides; our squads fight it like any enemy creature). [U] A scripted dragon placed in the air starts in flight with
  `DragonActorScript`'s flyby; check in the lab.
- It stays on screen until killed, until the player is beyond `offline_distance`, or for `online_max_hours` (default 2 game hours,
  then it flies off: sent offline with its wounds). Dragons count toward the on-screen budget as one group of one actor.
- The MCM toggle shut while one is on screen: it is sent offline first (no despawn in view), then retired.

## 7. Switch, difficulty, config

- MCM toggle **Dragons** (default on) on the "A-Life" page under "Simulation", next to Dynamic faction relations
  (`papyrus/ALife_MCM.psc` `OnPageReset`, MCM version 205); ini `[General] Dragons=1` (`src/Settings.cpp`). Off = the `dragon`
  gate shut whatever `DragonsReturned` says.
- `Diff(Dragons)` (T21, `m_cfg.Diff(DiffChannel::Dragons)`, 1.0 at Adept): x `max_dragons` (frequency) and x the level tier. Rule:
  at 1.0 draw no extra random numbers. Wired: `Board::DragonEff()` (`include/ALife/Board.h`) applies `Diff(Dragons)`.
- `config/dragons.ltx`: `[dragons] max_dragons, respawn_days, level_mult, fly_speed, stay_min, stay_max, prey_bonus,
  lair_weight, wall_weight, high_weight, high_z, flee_hours, online_distance, offline_distance, spawn_height, online_max_hours`,
  `[tiers]` (squad section = from player level), `[places]` (location = weight), each key tagged with its difficulty channel
  as tasks.ltx does (`max_dragons`, `respawn_days`, `level_mult`: Dragons; the rest not scaled).

## 8. Co-save

- Dragon squads are ordinary squads in `SIM2` (position, members with creature id and level, target). [I] **No SIM2 version bump
  is needed**: the faction index of a dragon (31) is new but no older index moves, the goodwill table is written with its length
  ([V] Save.cpp:99, ReadStanding since 207), and the dynamic-relation matrix is written with its size ([V] Save.cpp:169). The
  matrix loader copies an old 31 x 31 matrix into the top-left and keeps the dragon row / column from the static relations
  ([V] `include/ALife/SaveFormat.h` `RestoreRelations`, called from `src/Save.cpp` `LoadCb`); a pre-208 or bigger matrix is dropped.
- New record `DRGN` v1 (written after `NEWS`, before `DIFF`: `src/Save.cpp` `SaveCb`): the hour the last dragon was made or killed (respawn timer) and the dragons killed by the
  player (news, later the PDA). A save without it: timer starts now, no kills. Unknown version: skipped.
- `WINextDragon` lives in the game's own save (a vanilla global): nothing of ours.
- [U] Check in step 2 with a v208 save from the lab: loads, relations kept, no dragon until the gate opens.

## 9. Harness `--dragons` (headless test plan, step 2)

`harness --dragons [--days 60]` on the real world (data/world_skyrim.tsv), dragons gate open from day 0 unless named:

1. gate shut (`SetGated(Dragon, true)`): 0 dragon squads ever; opening it at day 10 makes the first within `respawn_days`.
2. population: dragons never exceed `max_dragons`; at `Diff(Dragons)` 0 / 0.5 / 1 / 2 the counts are 0 / <= 1 / <= 2 / <= 4
   (rounded), and the tiers move with the multiplier.
3. places: every dragon target is a `[places]` node; never a city, settlement, inn or player home; never two dragons at one place.
4. flight: a dragon's position moves in a straight line at `fly_speed` (distance per step within 1% of speed x hours), across
   water and off the road graph, and reaches its target.
5. population counts: a dragon staying at a farm or lair changes no place's `population`, `monsters` or controlling faction.
6. panic: novice / advanced human squads at a dragon's place retreat with no losses; veteran squads fight; dragon power wins
   against a lone novice squad > 90% over seeds.
7. deaths: a dragon killed offline leaves the board; news line once; the respawn timer restarts.
8. retire: toggling the gate shut retires every offline dragon on the next step.
9. budget: `--longrun --dragons --days 120` keeps p99 <= 2 ms per step and squads / members plateau as without dragons.
10. co-save: a board with dragons round-trips (squads, `DRGN`); a 208-style blob (31-faction matrix) loads with its relations kept.

Lab (step 3, one guarded run, BOARD M4c done-check): with the gate open and the timer due, `LAB_*` scenario near Skyborn Altar:
a dragon comes on screen flying, lands / fights, dies to console damage, gives a soul, leaves bones; `WINextDragon` moved 3 days.

## 10. Code (step 2)

- `include/ALife/Core.h`: `Faction::Dragon`; `Config.h`: `MonsterType::Flyer`, dragon config; `core/Config.cpp`: dragons.ltx,
  `glob=` gates; `core/Board.cpp`: flyer targets, straight flight, one per place, population skip, panic, max / respawn;
  `config/monsters.ltx`, `config/squads.ltx` (`dragon_*` one-member sections), `config/factions.ltx` (relations: hostile to all,
  gate line), `config/dragons.ltx`.
- Plugin: `src/Creatures.cpp` (distance, gate, air placement, skeleton kept, timer), `src/Save.cpp` (matrix copy, `DRGN`),
  `papyrus/ALife_MCM.psc` (toggle), `src/Settings.cpp` (`Dragons`).
- Tests: harness `--dragons` (section 9); compat / standing tests stay green with 32 factions.

## 11. Implementation notes (step 2, Oct 18)

- Core: `Faction::Dragon` (31), `MonsterType::Flyer` (factions.ltx `dragon = flyer`), relations: enemy of every people. Board:
  `UpdateDragon` (flight / stay / next place), `DragonTarget`, `TickDragons` (spawn: cap, timer, tier, a free lair first),
  `DragonHunt` (after the combat simulator; panic, one veteran fight per step); dragons never enter `RunOcs` and never call
  `AssignToSmart`. `Board::dragonLast` / `dragonKills` (record `DRGN` v1), `Board::dragonScale` (T21) -> `DragonEff()` =
  `ScaledDragons(cfg.dragons, scale)`: x `max_dragons`, / `respawn_days`, x `level_mult`; nothing else reads the numbers.
- Places: `GraphNode::dragon` from `DragonWeight()` in `World::Finalize` (lairs, `dragon_places.ltx` word walls from
  `tools/gen_dragon_places.py`: 40 locations from 46 refs in Skyrim.esm, high wilderness by `high_z`, `[places]` overrides).
- Gate: `[faction_gates] dragon = Skyrim.esm|042851=1` (new `glob=value` form, core/Config.cpp + src/CivilWar.cpp) and the MCM
  toggle (`Settings::dragons`, ini `Dragons=1`, `ALifeGet/SetDragons`, ALife_MCM.psc main page next to Dynamic relations; T21 is
  on master and the toggle stayed on the main page).
- [V] Bodies: `Creatures::Despawn` already leaves dead creatures in the world ("the body stays for the player to skin"), so a dragon
  killed on screen keeps its skeleton; no exception was needed.
- Plugin: `Online.cpp` picks the nearest due dragon within `online_distance` only when no squad is due, checks
  `MQ106TurnOffRandomDragons` / `WINextDragon` and the vanilla-lair rule, pushes `WINextDragon` like `updateWIDragonTimer`, uses
  `offline_distance` and `online_max_hours`; `Creatures::Spawn` lifts a dragon by `spawn_height`; `Sim::OnGroupMemberDied` restarts
  the timer and counts on-screen dragon deaths as the player's.
- Tasks (M5e) never pick a dragon (assault / rescue holders, defend waves, recover guards).
- The harness default runs keep dragons gated (the world before Mirmulnir), so their numbers are unchanged by M4c.
