# Difficulty scaling (T21)

User, Oct 18 2026: "ensure scalability based on difficulty sliders as well". Every A-Life system scales with
(a) Skyrim's own difficulty (Novice .. Legendary) and (b) one ALife MCM slider per channel, and stays stable and inside
the CPU / actor budget at the extremes.

Facts are marked [V] verified (headers, code, a run), [I] inferred (read from code or scripts, not run), [U] unverified.

## 1. One rule

For every channel `c`:

    M(c) = clamp( G(c, game difficulty) x S(c), limit_min(c), limit_max(c) )

- `G` is the row of `config/difficulty.ltx` for the current game difficulty (`[novice]` .. `[legendary]`). With the MCM
  toggle "Follow game difficulty" off, `G = 1` for every channel (the sliders alone decide).
- `S` is the MCM slider for the channel (default 1.0). The population slider is the old "Population density" slider
  (`ALife.ini PopulationScale`, 0.5 - 1.5): same key, same range, so nothing changes for existing installs.
- `limit_min / limit_max` (`[limits]`) keep the product sane whatever the two say.
- Systems never read `G` or `S`: they read `Config::Diff(channel)` (the final `M`) or the derived values below, all
  computed once by `ApplyDifficulty` (core, `src/core/Difficulty.cpp`) whenever the game difficulty, a slider or the
  config changes. The headless harness calls the same function with `--difficulty N` and `--diff channel=value`.
- Adept (the game's default, `iDifficulty = 2`) is all 1.0: an Adept game with default sliders plays exactly as before T21
  (same squads, same rolls: no random number is drawn for a channel at 1.0).

## 2. Channels

| Channel (MCM) | Key | What it scales | Who reads it | Status |
|---|---|---|---|---|
| Population | `population` | respawn quota of every human respawn entry (Anomaly `alife_stalker_pop`) | `Board::MaxCount` | wired |
| Monsters | `monsters` | respawn quota of every monster entry (Anomaly `alife_mutant_pop`) | `Board::MaxCount` | wired |
| Squad strength | `strength` | squad size, member level offset, gear level (see 3) | `Board::CreateSquad`, `Board::PaceLevels`, `Economy::OutfitNew` | wired |
| Combat lethality | `lethality` | offline combat damage per round (more deaths when squads meet offline) | `Board::Round` | wired |
| Economy | `economy` | job gold x M; tolls and companion pay x 1/M; stash value x M (M5f) | `Tasks::OfferOf`, `Standing::TollFor`, `Companions`, `src/Stashes.cpp` | wired |
| Wounded and surrender | `mercy` | chance a member goes down instead of dying, yields, surrenders x M; finish chance x 1/M | `src/Wounded.cpp` (into `WoundScale::mercy`, docs/WOUNDED.md section 8) | wired |
| Task difficulty | `tasks` | wave sizes, guard chance, target levels, hunt marks and time limits of jobs (no ambushes: docs/TASKS.md section 8) | `src/core/Tasks.cpp` (`TaskScale`, `Tasks::Eff()`) | wired |
| Dragons | `dragons` | dragon (M4c) frequency and level | `Board::DragonEff()` (`include/ALife/Board.h`) | wired |

The OCS "crowded world" gate uses Anomaly's own mean of the two population factors:
`squads > ceil((pop + monsters) / 2 x max_squad_count)` [I] (`sim_offline_combat.script:333-338`).

Strength applies to every squad (friendly ones too, and so to hired companions): the world as a whole is tougher, the
player's standing decides who is an enemy. Skyrim's own difficulty already scales damage between the player and
everyone else [I]; A-Life adds who and how many.

## 3. Derived values (`[derive]` in difficulty.ltx)

| Value | Formula (`s = M(strength)`) | Limits |
|---|---|---|
| squad size | `round_random(n x s ^ size_exponent)`, at least 1 | at most `max_squad_size` (7) and the on-screen actor budget per squad (`compat.onlineActors`), so every squad can still come on screen |
| member level offset | `round(levels_per_strength x (s - 1))`, added to the player level + the squad's own `level_offset` | `[level_min, level_max]` |
| gear level offset | `round(gear_levels_per_strength x (s - 1))`: the leveled lists (outfits, weapons) are resolved this many levels higher | `[gear_min, gear_max]` |
| OCS damage | `damage x M(lethality)` | |
| job gold | `max(gold_min, gold x gold_mult x M(economy))` | |
| toll, companion pay | `x 1 / M(economy)` | at least 10 / 1 gold |

Level offsets go through the existing "world levels with the player" pass: a raised difficulty raises offline members
over the next level steps; a lowered one never lowers anybody (only new squads are smaller / lower), the same rule as
`PaceLevels` already has. Rank (Anomaly rank points) is not scaled: rank comes from fights, and lethality already
changes how many fights members survive.

## 4. Default rows (proposal, tune in game)

| | Novice | Apprentice | Adept | Expert | Master | Legendary | limits |
|---|---|---|---|---|---|---|---|
| population | 0.8 | 0.9 | 1.0 | 1.05 | 1.1 | 1.2 | 0.25 - 2.0 |
| monsters | 0.75 | 0.9 | 1.0 | 1.05 | 1.15 | 1.25 | 0.25 - 2.0 |
| strength | 0.7 | 0.85 | 1.0 | 1.1 | 1.2 | 1.35 | 0.4 - 2.5 |
| lethality | 0.9 | 0.95 | 1.0 | 1.0 | 1.05 | 1.1 | 0.25 - 3.0 |
| economy | 1.5 | 1.25 | 1.0 | 0.9 | 0.8 | 0.7 | 0.25 - 4.0 |
| mercy | 1.3 | 1.15 | 1.0 | 0.95 | 0.9 | 0.8 | 0 - 3.0 |
| tasks | 0.75 | 0.9 | 1.0 | 1.1 | 1.2 | 1.3 | 0.25 - 3.0 |
| dragons | 0.6 | 0.8 | 1.0 | 1.1 | 1.25 | 1.4 | 0 - 3.0 |

Economy follows Anomaly's progression presets (easy rewards 1.5, medium 1.25, hard 1.0 [V] `configs/plugins/difficulty.ltx`
`econ_1..3`), shifted so Adept = 1.0. MCM slider ranges: population and monsters 0.5 - 1.5 (the old density slider),
the others 0.5 - 2.0, step 0.1; 0 for mercy and dragons is not offered (the dragon toggle and the wounded switch turn
those off).

## 5. Reading the game difficulty

- [V] CommonLibSSE-NG v11.0.0 (`deps/CommonLibSSE-NG-alandtse`, 94faaed): `RE::PlayerCharacter::GetGameStatsData()`
  returns `GameStateData` whose first field is `std::int32_t difficulty` (`include/RE/P/PlayerCharacter.h:518-530`;
  accessor versioned for 1.6.629+ and 1.7.99+, line 1091).
- [V] Same headers: `RE::INIPrefSettingCollection::GetSingleton()->GetSetting("iDifficulty:GamePlay")` (SkyrimPrefs.ini),
  `data.i`.
- [I] Values 0 Novice, 1 Apprentice, 2 Adept, 3 Expert, 4 Master, 5 Legendary (the menu order and the vanilla
  `fDiffMultHPByPCVE` .. `fDiffMultHPByPCL` game settings).
- [U] Which of the two is authoritative after the player changes the menu slider or loads a save made at another
  difficulty. The plugin reads the player field first, falls back to the pref when the player is not there or the value
  is out of range, and logs both on every change (`[difficulty] game=... pref=...`) so the lab run settles it.
- The value is polled once per sim tick (a 4-byte read); a change re-runs `ApplyDifficulty` and logs one line. Debug
  only: `iDebugDifficulty=N` in `[Debug]` writes N into the player field once in the world (lab runs at Novice and
  Legendary without a menu).

## 6. Persistence

- `ALife.ini [General]`: `PopulationScale` (unchanged), `MonsterScale`, `StrengthScale`, `LethalityScale`,
  `EconomyScale`, `MercyScale`, `TaskScale`, `DragonScale`, `FollowGameDifficulty` (default 1). The ini is the default for
  new games and for saves made before T21.
- Co-save record `DIFF` v1 (its own record like `TASK` / `NEWS`; `SIM2` stays 208, so saves from before T21 load
  unchanged and M5d's / M5e's record versions do not clash): follow flag + 8 slider values. A save carries the values it
  was played with; loading it puts them back into the MCM. A save without `DIFF` keeps the ini values.

## 7. Anomaly references (local `ref/anomaly`, not redistributed)

- [V] `ui_options.script:426-427`: `alife_mutant_pop` / `alife_stalker_pop` are lists 0.25 / 0.5 / 0.75 / 1 (defaults
  0.75 / 0.5); `sim_board.script:314-330` multiplies each common respawn count by them (monsters by the mutant factor),
  `smart_terrain.script:1663` the same for live respawns.
- [V] `game_difficulties.script`: two separate settings, gameplay (`gdiff_1..4`: actor immunities, hit power,
  dispersion, stamina, weight) and economy / progression (`econ_1..3`: goodwill, rewards, repair, upgrade, buy, sell,
  loots, money_loots, stash_chance, restock ...). Each preset can be overridden key by key from the options menu and the
  overrides are stored in the save (`alife_storage_manager.get_state().diff_game / diff_eco`): the same shape as ALife's
  row x slider, co-saved.
- [V] `tasks_fetch.script:194-196` scales artefact reward value by the economy type; `death_manager.script:36` scales
  loot by `loots`.
- Anomaly does not scale squad size, level or offline combat with difficulty; those channels are ALife's own, for a
  game where the player levels.

## 8. Checks

- Harness: `harness --diff-sweep`: 6 game difficulties x each channel at min / 1.0 / max (30 days each): no errors,
  squad counts rise with population and monsters, mean squad size and member level rise with strength, OCS deaths
  rise with lethality, job gold rises with economy (monotonic, non-strict where a cap is reached);
  `--longrun --days 120 --difficulty 5 --diff population=1.5 --diff monsters=1.5`: CPU p99 within 2 ms.
- Lab: one guarded run at Novice and at Legendary (`iDebugDifficulty`), `[difficulty]` lines and the squads that come
  on screen (size, levels) as expected, clean ALife.log.

## 9. Results (Oct 18, headless, seed 1)

- [V] `harness --diff-sweep` (6 x 8 x min / 1.0 / max, 10 days each): all passed. Sliders at 1.0, Novice -> Legendary:
  people respawn quota 169 / 192 / 208 / 220 / 231 / 246; monster quota 105 / 136 / 148 / 157 / 176 / 197; mean squad size
  2.26 / 2.62 / 3.24 / 3.61 / 3.75 / 4.34; mean member level (player 15) 13.3 / 15.1 / 16.3 / 17.2 / 17.9 / 20.1; toll at
  level 20: 106 / 128 / 160 / 177 / 200 / 228. Squads at start are rounded at random per respawn entry (Expert 154 > Master
  152), so the sweep checks the quotas strictly and the counts within 3%. Follow off: Legendary plays exactly as Adept.
- [V] `--longrun --days 120` (Adept, defaults): CPU p99 0.879 ms, max 1.337 ms per step.
- [V] `--longrun --days 120 --difficulty 5 --diff population=1.5 --diff monsters=1.5 --diff strength=2.0` (population 1.80,
  monsters 1.88, strength 2.50: size x2.5 capped at 7, +10 levels, gear +9): CPU p99 1.150 ms, max 1.707 ms; no unbounded
  growth. Board::maxSquads (400) caps the squad count at these extremes.
- [V] tasks_test 93/0 (incl. bounty gold x Diff(Economy) at Novice / Adept / Legendary x 0.5 / 1 / 2), standing 98/0,
  civilwar 81/0, compat 419/0, party 61/0; harness default, `--mutants`, `--m5g`, `--solstheim` all pass.
- [V] Lab (Oct 18, guarded, recon/t21 + recon/t1/short-0631 / short-0635, lab profile SkyrimPrefs iDifficulty 0 then 5, no
  iDebugDifficulty): the player's GameStateData difficulty equals the pref value (log `game 0 pref 0`, `game 5 pref 5`), so the
  section 5 [U] is settled for a new game started from the main menu (a save made at another difficulty is still unchecked).
  Novice: 136 people squads at start, mean size 2.19, mean level 1.4 (player 1, offset -3 clamped at 1), 87 monster squads.
  Legendary: 204 people squads, mean size 4.20, mean level 6.1 (+4), 178 monster squads. 0 watchdog strikes, OWN lines only
  the launcher's tree, nothing left open, 0 ALife.log / skse64.log errors, no crash. Not checked in game: the MCM Difficulty
  page (opening SkyUI menus needs focus) and loading a save with a DIFF record.
