# Parity with STALKER Anomaly: the gap list (T3, Oct 9 2026)

The goal (user, Oct 9 2026): **"fully playable to the same standard as STALKER Anomaly"**. This file lists every Anomaly
gameplay system that touches A-Life or the living world, says what it becomes in Skyrim, what ALife 2.0.0 already has,
how big the gap is, and splits milestone M5 (40% of the goal) into sub-milestones.

Sources. Anomaly 1.5.3 scripts are cited as `name.script:line` (in `ref/anomaly/scripts`), configs as
`configs/...` (in `ref/anomaly/configs`). That extract is local only and never shipped. ALife code is cited from this
repo (`src/...`, `config/...`) at master `cde44f8`. Anomaly facts were read through summaries and the key ones
spot-checked against the files; line numbers can drift by a few lines.

Size = worker sessions (S = 1, M = 2, L = 3-4). Core = needed for "Anomaly's standard"; optional = Anomaly has it
behind an option or a separate game mode, or Skyrim already covers it.

## 1. What ALife already matches (the A-Life core, M0-M2)

| # | Anomaly system | Anomaly source | Skyrim mapping | ALife status |
|---|---|---|---|---|
| 1 | Smart terrains with props (base, territory, resource, lair, surge), max population | `smart_terrain.script:1505` (`target_precondition`), `simulation_objects.script:16` (squad weights) | vanilla BGSLocations with world markers, props by location keyword | **have**: `src/Graph.cpp`, `src/core/Config.cpp:287-301`, `config/props.ltx` |
| 2 | Squad targeting (`evaluate_prior`, best-5, time windows for base/territory/resource) | `sim_board.script:11`, `smart_terrain.script:1559-1590` | same, Skyrim clock | **have**: `src/core/Board.cpp:124-191`, `config/alife.ltx` `[time_windows]` |
| 3 | Respawn (respawn_params, 12 h idle, 150 m no-respawn radius), faction-controlled smarts | `smart_terrain.script` respawn block | same | **have**: `src/core/Board.cpp:256-266` |
| 4 | Offline combat (Tronex OCS) | `sim_offline_combat.script` | same | **have**: `src/core/Board.cpp:414-553` |
| 5 | NPC rank growth from fights | `game_statistics.script:140-154`, `:325-339` | level + skills | **have**: `src/core/Board.cpp:452-460`, `src/Economy.cpp:632-646`, level pacing `Board.cpp:611-628` |
| 6 | Territory changing hands (faction_controlled, war on shared smart) | `smart_terrain.script` (`faction_controlled`) | forts, camps, mines change owner | **have**: `src/core/Board.cpp:556-588` |
| 7 | Player standing: community goodwill + personal + rank + reputation | `game_relations.script:18-36`, `configs/creatures/game_relations.ltx` | Civil War side, race, guilds, gear, bounty, notoriety | **have**: `src/core/Standing.cpp`, `src/Rep.cpp`, `docs/STANDING.md` |
| 8 | Meet scheme: greeting, "hide your weapon", talk/trade toggles | `xr_meet.script:513-539` | Skyrim dialogue (Hello / challenge / toll) | **have**: `src/Talk.cpp`, `src/Behavior.cpp:261-273`, `docs/CONVERSATIONS.md` |
| 9 | Trade with any stalker (not only traders) | `dialogs.script:2590-2614` | `ShowBarterMenu` over the squad's real pack | **have**: `src/Behavior.cpp`, `src/Economy.cpp` |
| 10 | Traders restock (24 h), NPCs sell surplus and buy supplies at traders | `trade_manager.script:133-155`, `axr_trade_manager.script:111-114`, `:236-327` | town merchants' real chests | **have**: `src/Merchants.cpp:73-86`, `:237-325`, `config/alife.ltx` `[activities]` |
| 11 | NPC loadouts by faction and rank; loot on corpses | `xrs_rnd_npc_loadout.script:110-173`, `death_manager.script:177-231` | squad kit made at level; real packs | **have**: `src/Economy.cpp` |
| 12 | Gather items, loot corpses | `xr_gather_items.script:129-196`, `xr_corpse_detection.script:174-275` | same | **have**: `src/Scavenge.cpp`, OCS loot `src/Economy.cpp:527-552` |
| 13 | NPCs use medkits | `xr_eat_medkit.script:47-94` | potions, food | **have**: `src/Economy.cpp:673-687` |
| 13b | Silent kills: a kill nobody sees does not count against you (`gameplay_silent_kills.script:83-122`, options `gameplay/silent_kills/*`) | see left | unwitnessed kills | **have**: witnessed vs unwitnessed deeds, bodies discovered later at a discount (`src/core/Standing.cpp:401-415`, `config/standing.ltx` `[deeds]`) |
| 14 | Persistence | `alife_storage_manager.script`, per-system `save`/`load` | SKSE co-save | **have** (squads, smarts, merchants, standing): `src/Save.cpp` v205; the news feed and the live relation matrix: co-save record `NEWS` v1 (M5g) |

Not ported because the engine already does it: post-combat idle (`post_combat_idle.script`), fight from cover
(`axr_fight_from_cover.script`), smart covers, hearing/danger. Skyrim's combat AI covers these.

## 2. The gap list

| # | Anomaly system | Anomaly source | Skyrim mapping | ALife status | Size | Depends on | Core? |
|---|---|---|---|---|---|---|---|
| G1 | **Emission (surge)**: every ~24 h (option, `ui_options.script:440`) a timed event with warnings (siren, task "hide"), waves that kill anyone outdoors (actor: `surge_manager.script:945-1060`, default fate `kill_at_wave`; online NPCs outside `surge` smarts or cover: `:1244-1287`); squads retarget to `surge` smarts only (`smart_terrain.script:1564-1569`, weight 3 `simulation_objects.script:16`); cover zones `configs/misc/surge_manager.ltx`; aftermath respawns artefacts (`bind_anomaly_zone.script:42-70`); state saved (`surge_manager.script:1531-1587`) | see left | **needs your decision** (D1). Candidate: a magical storm (sky FX + weather + damage outdoors); shelter = interior cell or under a roof (raycast) or a smart with `surge` | **partial**: the board side exists (`src/core/Board.cpp:128`, `surge` prop in `config/props.ltx`); nothing starts an event, no FX, no damage, no warnings, no save | M | none | core |
| G2 | Psi-storm: second event type, vortex strikes near the actor, kills online NPCs within 50 m outside cover | `psi_storm_manager.script:51-63`, `:338-506` | e.g. a dragon storm or Oblivion-touched storm (D1) | **missing** | S (on top of G1) | G1 | optional |
| G3 | Artefacts respawn in anomaly fields after an emission; dynamic anomalies reshuffle | `bind_anomaly_zone.script:42-70`, `bind_anomaly_field.script:487-489` | rare reagents / soul-gem shards appear in exposed places after the storm | **missing** | S | G1 | optional |
| G4 | **NPC tasks (framework)**: givers are faction leaders, traders and any sim squad commander (30% have none, one sticky job: `axr_task_manager.script:853-938`); 67 generic sim tasks in `configs/misc/task/tm_dynamic.ltx` (fetch 32, assault 13, bounty 8, delivery 3, quest-item stash 3, measure 3, escort 4, base defense 1, fate 1); rewards money + goodwill (+50 done / -25 failed) + often a stash (54 of 67 use `reward_stash`); `repeat_timeout` 6 h; fail if giver dies; saved | `task_manager.script:1-246`, `task_objects.script:58-278`, `axr_task_manager.script:23-938`, `dialog_manager.script:277-366` | dialogue "Got any work?" in the generated `ALifeTalk.esp`; journal entry + compass/map marker through one ALife quest whose ReferenceAliases the plugin fills (`ForceRefTo`) and whose objective text uses alias names | **missing** (the only dialogue choices are pay, poor, refuse, fight, peace, defy, news, trade: `src/Talk.cpp:40-50`) | L | dialogue generator (`tools/build_talk_esp.py`) | core |
| G5 | Task types on live A-Life objects: **bounty** (kill a real squad member: `tasks_bounty.script:88-182`), **assault / smart control** (clear the squads at a smart: `tasks_assault.script:97-498`, `tasks_smart_control.script:215-425`), **delivery** (carry a package to a live squad's commander: `tasks_delivery.script:85-311`), **fetch** (N items from `configs/items/settings/fetch_list.ltx`: `tasks_fetch.script:109-714`) | see left | targets are ALife squads/smarts; fetch lists from Skyrim items (pelts, ore, ingredients, potions) | **missing** | M | G4 | core |
| G6 | More task types: **escort/guide** (`tasks_guide.script:282-449`), **defend a base** (spawned attack waves: `tasks_defense.script:295-540`), **rescue** a captive (`tasks_agent_rescue.script:277-511`), **recover item on a corpse** (`tasks_recover_item_on_corpse.script:162-316`), **fate** (find a dead squad: `tasks_fate.script:13-435`), **top-10 hunt** (`tasks_top_10.script:31-106` + rankings `pda.script:457-635`), **dominance** (`tasks_dominance.script:167-360`) | see left | escort a caravan or a squad to a smart; defend a fort/camp; rescue from bandits; hunt the ten most dangerous outlaws | **missing** (caravans exist but nobody can be escorted: `src/Economy.cpp:346-364`) | L | G4, G5 | core (escort, defend, recover, top-10); rest optional |
| G7 | **Companions / squad recruiting**: a friendly squad commander joins with his squad (`axr_companions.script:246-404`), max 2 squads (4 for some), paid escorts for neutrals (stalker/freedom/killer/bandit, 1500-3000 per day by skill: `txr_paid_companions.script:11-153`); commands follow / wait / attack / ignore / stealth / loot / distance (`:865-935`); inventory hand-over (`ui_companion_inv.script:42-256`); teleport on level change; saved; config `configs/ai_tweaks/axr_companions.ltx` | see left | dialogue "Travel with me" / "I'll pay you to come along"; vanilla teammate flag (`SetPlayerTeammate`) for each member; commands as dialogue; "let me see your gear" as vanilla share-items; squad pinned out of board targeting; follows across doors | **missing** (only `Call::Assist` fights beside you: `src/Behavior.cpp:221-230`) | M-L | G4 dialogue path (shares `build_talk_esp.py`) | core |
| G8 | **Wounded, help, finish, surrender**: wounded NPCs crawl and ask for a medkit (`xr_wounded.script:394-557`), squadmates heal them (`xr_help_wounded.script:22-60`, rank +10..20), enemies finish them (`xrs_kill_wounded.script:143-195`), wounded enemies can surrender and hand over loot/stash (`dialogs.script:1039-1203`), giving a medkit earns gratitude | see left | bleedout state for pool actors (no bleedout recovery while "wounded"), "Here, drink this" (healing potion) dialogue; surrender = yield + "spare me" dialogue that offers gold or a stash marker | **partial**: `TooHurt`, flee, drink potions (`src/Behavior.cpp:238-251`, `src/core/Activities.cpp:122-128`); no wounded state, no help/finish, no surrender | M | G4 (stash marker reward), dialogue | core |
| G9 | **Stashes**: on a new game half of all stash boxes get random loot by tier (`treasure_manager.script:415-659`, `configs/items/settings/treasure_manager.ltx`); dead NPCs' PDAs reveal stashes (`ui_pda_npc_tab.script:107-113`, `:695-699`); tasks reward stashes; map marker + news on found (`treasure_manager.script:498-501`, `:662-674`) | see left | stash = existing non-respawning containers outside towns (satchels, knapsacks, chests in ruins); a dead squad member may carry a "stash note"; marker through the G4 quest | **missing** | M | G4 (markers), G5 (stash rewards) | core |
| G10 | **Dynamic news** (full): event types death (stalker/mutant/named), death reports, SOS under attack, offline battles, kill wounded, loot, artefacts, surge forecast/end, task/bounty gossip, faction spam, found-dead, companions, random (`dynamic_news_manager.script:29-55`, `:558-661`, `:1767-2400`); channels by faction, queue of 3, cooldowns (`:57-68`, `:205-206`, `:1467-1535`); heard through the PDA | see left | no PDA in Skyrim: news reaches you through squads' "what's the word", innkeepers, couriers (letters) and the MCM News page; optional HUD notification | **have (M5g)**: raids, dungeon clears, wipes, territory changes, plus SOS when an offline fight starts, battle reports (a side falls back), death reports (commanders, veterans), gossip every ~6 h, war / alliance / cease-fire news (`src/core/Board.cpp` Battle, MemberDied, ChangeRelation, Gossip); per-place and world-wide gaps per kind (`alife.ltx [news]`); MCM News page, saved in the co-save. Still missing: surge, tasks, companions, loot, found-dead kinds | M | G1, G4, G7 add their own event types | core |
| G11 | Dynamic faction-vs-faction relations: kills shift the matrix, news on war / alliance / cease-fire, limits in `configs/plugins/dynamic_faction_relations.ltx` (`game_relations.script:200-277`, `:343-424`); **off by default** in Anomaly (`ui_options.script:431`) | see left | the same over ALife's 15 factions; Civil War pair stays locked at war | **have (M5g)**: `Board::OnKill` / `ChangeRelation` port `calculate_relation_change` + `change_faction_relations` (enemies of the victim warm to the killer or its friends cool, 50% each; pegged values reset to neutral half the time), `factions.ltx [dynamic_relations]`, Civil War pair locked, MCM toggle *Dynamic faction relations* (off), online kills count too, matrix in the co-save | S | G10 (news) | optional (MCM toggle, off like Anomaly) |
| G12 | Hunter squads sent after the player: after day 1, enemy squads are spawned at 200 m+ and rushed at the actor, count by rank/reputation (`sim_squad_bounty.script:14-201`, `configs/plugins/bounty_squad.ltx`), paused in safe smarts | see left | hostile factions send a hunting party (Thalmor justiciars, Forsworn, Silver Hand, vampires) when standing is hostile; never inside towns | **missing** (predators only weigh the odds when they meet you: `docs/STANDING.md`). Not to be confused with M5g's lair hunters (Hunter squads after occupied lairs for parts, `docs/MUTANTS.md`), which are done | S | none | core |
| G13 | Camp life at smarts: gulag jobs per smart (guard, patrol, sleep, campfire, sniper, camper, collector, surge cover: `gulag_general.script:17-34`, `:96-111`), campfire gatherings with stories, guitar, harmonica (`sr_camp.script:20-360`, `xr_campfire_point.script:193-247`), NPCs light the camp fire (`axr_turn_on_campfire.script:45-140`) | see left | sandbox/sleep/guard packages on furniture and idle markers at the smart, lute/drum/flute idles and "story at the fire" barks at camps | **partial**: stay = patrol + sandbox pauses (`src/Online.cpp:308-399`), rest/hunt/fish/trade jobs (`src/core/Activities.cpp:86-251`); no per-smart job slots, no campfire scenes | M | none | core |
| G14 | Stalker panic: NPCs flee from mutants above their rank (`axr_stalker_panic.script:73-457`) | see left | low-level squads flee dragons, giants, trolls | **missing** | S | M4 mutants | optional |
| G15 | Rankings and statistics: top-100 alive stalkers, actor rank/reputation tips (`pda.script:457-635`, `game_statistics.script:104-137`, `:455-503`) | see left | MCM page "Most dangerous people in Skyrim"; feeds G6 top-10 hunt | **missing** | S | none | optional (needed by the top-10 hunt) |
| G16 | Bribe / cease-fire with an enemy faction (4000-12000 for 2-4 days: `xr_bribe.script:191-365`) | see left | pay a hostile squad's leader to leave you be | **partial**: tolls exist one way (`src/Behavior.cpp:446-474`) | S | none | optional |
| G17 | Disguise with suspicion and exposure (`gameplay_disguise.script:441-864`, `configs/plugins/disguise.ltx`) | see left | wearing a faction's armour: suspicion grows near its squads; exposure costs goodwill | **partial**: gear shifts attitude (`src/core/Standing.cpp:104-110`, `:262-268`), no suspicion or exposure | S | none | optional |
| G18 | Faction-specific trader prices (discount condlists per goodwill: `trade_manager.script:198-238`) | see left | merchants of a faction give better prices to friends of it | **missing** | S | none | optional |
| G19 | **Warfare mode**: factions own smarts, spawn from owned bases scaled by resources, plan invasions, player buys squads, win at 50% of smarts (`warfare.script:275-462`, `smart_terrain_warfare.script:167-2265`, `warfare_factions.script:142-280`, `ui_pda_warfare_tab.script:149-238`). Off unless the warfare game mode is chosen (`warfare.script:275-280`) | see left | a "Skyrim at war" start option over ALife's existing territory control | **partial** (control and capture exist; no resource-scaled spawning, no invasions, no player command) | L | G10, G11 | optional, **needs your decision** (D2) |
| G20 | Ambient helicopters that patrol smarts and attack squads (`heli_alife.script:9`, `:144-266`) | see left | dragons as A-Life actors that fly between smarts and attack squads offline and online | **missing** (vanilla dragons are separate) | M | M4 | optional, **decision** (D4) |
| G21 | Game modes: Azazel (continue as a random squad member on death: `gamemode_azazel.script:63-392`), ironman, agony, timer (`gamemode_*.script`) | see left | Azazel would mean swapping the player into a pool actor: not practical in Skyrim; ironman/agony belong to save-game mods | **missing** | L (Azazel) | none | out of scope (recommend) |
| G22 | Faction start / player as a faction member: Anomaly starts you in a faction; tasks, companions, relations key off it (`ui_mm_faction_select.script`, `configs/plugins/new_game_start_locations.ltx`, `faction_profile.ltx` via `faction_expansions.script:171-221`) | see left | Skyrim allegiances already count (Civil War, guilds: `docs/STANDING.md`); new: "join" an ALife faction (wanderers, hunters, mercenaries, bandits) by dialogue | **partial** (allegiance read from vanilla factions only) | M | G4, G7 | optional, **decision** (D3) |
| G23 | Guides and paid fast travel (`ka_travel.script:6-111` cost = ceil(dist/10)*100, `game_fast_travel.script:62-196`, `txr_routes.script`) | see left | Skyrim carriages and fast travel already exist; escort travel is G6 | n/a | - | - | not needed |
| G24 | Survival layer (sleep, thirst, cooking at campfires: `actor_status_sleep.script`, `actor_status_thirst.script`, `item_cooking.script`) | see left | Skyrim Survival Mode (Creation Club) covers the player; NPC needs are already in ALife | n/a | - | - | not needed |
| G25 | Corpse cleanup (max 5 bodies, 180 s, 75 m: `release_body_manager.script:6-192`) | see left | pool actors are recycled | **unverified**: check in the T1 play-test that dead pool actors are recycled and bodies do not pile up | S if broken | T1 | core (stability; M6 if it fails) |
| G26 | Mutants: lairs, monster priors, mob_home / mob_camp schemes (`mob_home.script:33-88`, `gulag_general.script:1018-1033`, `bind_monster.script:427-447`) | see left | wildlife, draugr, falmer; zombied stalkers and zombie mutants = the AE Creation Club "Plague of the Dead" zombies (`ccbgssse003-zombies.esl`, `ccBGSSSE003ZombieRace`, leveled `ccBGSSSE003_LCharBGSSSE003ZombieMelee`), a **soft dependency** (user decision, Oct 9) | **covered by M4** (T2, `docs/MUTANTS.md` on branch `t2-mutants`) | - | - | M4 (not M5) |

## 2b. Cross-check against Anomaly's options

Every gameplay option Anomaly exposes (`ui_options.script`, saved in `gamedata/configs/axr_options.ltx`) maps to a row:

| Option group | Options | Row |
|---|---|---|
| `alife/general` | `alife_stalker_pop`, `alife_mutant_pop` | population density (MCM, have); mutants M4 |
| | `offline_combat`, `excl_dist` | #4, #3 (have) |
| | `dynamic_relations`, `war_goodwill_reset` | G11 |
| | `dynamic_anomalies` | G3 |
| | `heli_spawn`, `heli_engine_sound` | G20 |
| `alife/event` | `emission_*` (state, frequency, fate, warning, task), `psi_storm_*` | G1, G2 |
| `alife/dynamic_news` | 25 toggles, one per news type | G10 |
| `alife/warfare/*`, `alife/warfare/azazel` | ~980 warfare settings (71 per faction), Azazel respawn rules | G19, G21 |
| `gameplay/general` | `max_tasks` | G4 (cap on active tasks) |
| | `corpse_max_count`, `corpse_min_dist`, `release_dropped_items` | G25 |
| | `npc_loot_distance` | #12 (have) |
| | `mechanic_feature`, `need_equipped_hkit`, `hardcore_ai_aim`, `outfit_portrait` | n/a (Skyrim smithing, UI, engine AI) |
| `gameplay/silent_kills` | `sk_*` | #13b (have) |
| `gameplay/disguise` | 19 settings (gear, speed, distance, stay time factors) | G17 |
| `gameplay/economy_diff` | goodwill, rewards, repair, buy/sell, loots, stash_chance, restock, outfit_drops | #10, G4 rewards, G9 stash chance, G18 |
| `gameplay/gameplay_diff` | thirst, sleep, radiation, weight, damage factors | G24 (n/a) |
| `gameplay/fast_travel`, `gameplay/backpack_travel` | travel rules (blocked in combat, overweight, emission) | G23 (n/a); the "not during an emission" rule goes into G1 if Skyrim's fast travel should be blocked during the storm |

## 3. Decisions for the user

- **D1. What is the emission in Skyrim?** Anomaly's emission is lethal to everyone outdoors and is on by default every
  ~24 h. Options: (a) a magical storm (red/green sky, lightning, magicka damage outdoors; lore hook: an Aetherial or
  Oblivion-touched storm), lethal like Anomaly; (b) the same storm but only damaging (no instant death); (c) a "dragon
  storm" that sends everyone to shelter because dragons hunt the open roads; (d) none. Also: how often (Anomaly offers
  12 h / 1 / 2 / 4 days) and whether the psi-storm (G2) is wanted as a second event. Anomaly can also turn those
  caught outside into zombies (`emission_fate` = `turn_to_zombie`, `surge_manager.script:1268-1277`); with the user's
  zombie decision (Oct 9: Plague of the Dead, M4) the storm could raise its outdoor dead as those zombies, only when
  `ccbgssse003-zombies.esl` is loaded (soft dependency, as in M4). **Recommendation: (a) with MCM
  settings for frequency and lethality, default every 2 days, damaging but not instantly lethal for the player.**
- **D2. Warfare mode in scope?** Anomaly ships it as a separate game mode, not the normal game. Recommendation: out of
  M5; revisit after M6 as its own milestone.
- **D3. Joining ALife factions?** Anomaly is built around the player being a faction member. Recommendation: later and
  optional; vanilla allegiances already drive standing.
- **D4. Dragons as A-Life actors (the helicopter analogue)?** Recommendation: optional, after M4.

## 4. Proposed M5 sub-milestones (total 40% of the goal)

| # | Sub-milestone | Gaps | Weight | Size | Notes |
|---|---|---|---|---|---|
| M5a | Task framework + first tasks: work dialogue, giver choice, one ALife quest with filled aliases for journal and markers, rewards (gold, goodwill, stash), fail on giver death, cooldowns, co-save; bounty, assault/clear a smart, delivery, fetch | G4, G5 | 11 | L + M | first: every later system reuses its dialogue and marker path |
| M5b | Companions and recruiting: free (friendly) and paid (neutral) squads, commands, gear hand-over, pinned out of the board, follow through doors, death and save | G7 | 7 | M-L | after M5a (same dialogue generator) |
| M5c | Emission analogue: scheduler, warnings, sky/weather FX, outdoor damage for player and online NPCs, offline deaths outside shelters, board retarget to shelters, aftermath, co-save, MCM | G1 (+G2, G3 if chosen) | 6 | M | needs D1; files independent of M5a/M5b, can run in parallel |
| M5d | Wounded, help, finish and surrender: bleedout state, squadmates heal, enemies finish, "here, drink this", surrender with loot or stash | G8 | 5 | M | after M5a (stash reward) |
| M5e | More tasks: escort/guide (incl. caravans), defend a smart, rescue, recover item on a corpse, top-10 hunt with rankings | G6, G15 | 5 | L | after M5a |
| M5f | Stashes: seeded stash containers, stash notes on the dead, task-reward stashes, markers, found news | G9 | 3 | M | after M5a |
| M5g | Living news and hostility: all news types (SOS, death reports, gossip, surge, tasks, companions), persisted news, dynamic faction relations (MCM, off by default), hunter squads after a hated player | G10, G11, G12 | 2 | M + S | last of the systems: collects event types from M5a-M5f |
| M5h | Camp life: per-smart job slots (guard, patrol, sleep, campfire), campfire gatherings with music and stories | G13 | 1 | M | polish; any time after M5a |
| | **Total** | | **40** | ~20 sessions | |

Optional, weight 0 until the user opts in: G2/G3 (with M5c), G14, G16, G17, G18 (each S), G19 warfare (L), G20 dragons
(M), G22 faction membership (M). G21 Azazel and the game modes: recommended out of scope. G23/G24: Skyrim covers them.

## 5. Recommended order

1. **M5a** task framework (unlocks the dialogue, journal and marker path for M5b, M5d, M5e, M5f).
   In parallel, once D1 is answered: **M5c** emission (Board, a new world-event module, FX; no shared files with M5a).
2. **M5b** companions (shares `tools/build_talk_esp.py` and `src/Talk.cpp` with M5a: never at the same time).
3. **M5d** wounded/surrender, then **M5e** more tasks, then **M5f** stashes (all reuse M5a).
4. **M5g** news and hostility (collects every event type), then **M5h** camp life.

Writers that touch `src/Talk.cpp`, `tools/build_talk_esp.py` or `ALifeTalk.esp` go one at a time. M4 (mutants) is
independent of M5 except G14 and G20.
