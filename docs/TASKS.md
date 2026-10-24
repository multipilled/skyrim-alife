# Tasks: work from squad leaders (M5a)

Anomaly's generic jobs ("got any work?") ported onto ALife's squads: a squad leader you are on speaking terms with may
have one job for you; you take it in ordinary Skyrim dialogue, it appears in your journal with a map and compass
marker, the world keeps simulating while you work, and you collect the pay from the leader who gave it.

Sources (Anomaly 1.5.3, local only, read through a summary and spot-checked): `axr_task_manager.script`,
`task_manager.script`, `task_objects.script`, `tasks_bounty.script`, `tasks_assault.script`, `tasks_delivery.script`,
`tasks_fetch.script`, `configs/misc/task/tm_dynamic.ltx` (67 generic sim tasks: fetch 32, assault 13, bounty 8,
delivery 3, ...). M5a ports the four types that make up 56 of the 67. M5e adds escort, defend, rescue, recover and the
top-10 hunt with its rankings (section 8; sources `tasks_guide.script`, `tasks_defense.script`, `tasks_agent_rescue.script`,
`tasks_recover_item_on_corpse.script`, `tasks_top_10.script`, `pda.script` rankings); stash rewards are M5f.

## 1. Who gives work

| Anomaly | ALife |
|---|---|
| Any sim squad commander can give a job (`axr_task_manager.script:838-938`) | the **leader** (first living member) of any human ALife squad; monster squads never |
| Filter `sim_communities` per task (`tm_dynamic.ltx`) | `config/tasks.ltx` `[givers]`: which task types each faction hands out, with weights |
| 30% of sim NPCs have no job at all (`:852-857`) | 30% of offers roll "no work" (`no_job_chance`) |
| One sticky job per NPC until it goes offline (`:933-938`) | one sticky offer per squad, kept `offer_hours` (6 game hours) or until taken |
| Turn-in, then `repeat_timeout` 6 h before that job comes again (`task_manager.script:109-115`, `:215-246`) | a squad whose job you finished or failed has nothing for you for `repeat_hours` (6 game hours) |
| `max_tasks` option | at most `max_active` (4) open jobs: the journal quest has 4 slots |
| Giver dies: the squad's new commander inherits the job (`:129-141`); no squad: the job fails | the giver is the **squad**: the job lives while any member does; it fails when the squad is wiped |

Standing: work is offered in the **greeting** conversation only, i.e. to a player the squad regards as Neutral or better
(`docs/STANDING.md`). A squad that tolls or challenges you does not offer jobs. Outlaw factions (bandits, Forsworn,
necromancers, vampires) give work too, to a player they are on speaking terms with.

Rank: Anomaly scales fetch items by the actor's rank (`tasks_fetch.script:303-311`). ALife picks bounty and assault
targets whose average level is at most `max_level_over` (6) above the player's, and fetch items by player level tier.

## 2. Offering through the existing dialogue

All records live on the vanilla `DialogueGeneric` quest in the generated `ALifeTalk.esp` (`tools/build_talk_esp.py`),
like every other squad line (see `docs/CONVERSATIONS.md` and the knowledge note on DialogueGeneric: a quest of our own is
never consulted for these NPCs). The plugin publishes, next to `ALifeTalkMode`, two more globals for the squad under the
crosshair:

- `ALifeTaskOffer`: 0 none, 1 bounty, 2 assault, 3 delivery, 4 fetch, 5 escort, 6 defend, 7 rescue, 8 recover, 9 hunt
  (the squad's sticky offer, 0 when it has none or you hold `max_active` jobs).
- `ALifeTaskState`: 0 nothing between you, 1 you hold an unfinished job from this squad, 2 that job is ready to hand
  in, 3 you carry a package addressed to this squad.

Player topics (greeting mode; hand-in and abandon also in companion mode, for the squad you escort: section 8):

| Topic | Shown when | Squad's answer | Then |
|---|---|---|---|
| *Got any work for me?* | Offer > 0, State = 0 | a pitch per type ("There's someone I want dead...") | two child topics: *I'll do it.* (accept) / *Not right now.* (decline) |
| *About that job. It's done.* | State = 2 | "Good work. Here, as promised." | reward paid after the menu closes |
| *I have a delivery for you.* | State = 3 and the player holds the parcel | "About time. Here, for your trouble." | parcel taken, reward paid |
| *I can't finish that job.* | State = 1 | "Then I'll find someone who can." | job abandoned (fail) |

The specifics (who, where, how many) are not in the spoken line (the generic quest's text cannot name runtime targets);
they appear as the journal objective and as a notification the moment you accept ("New job: kill Hrolf the Red, Silver
Hand, near Fort Greymoor"). As with tolls, the choice is read from `TESTopicInfoEvent` and applied on the next tick
after the menu closes: no gold, items or quest changes inside the dialogue event.

## 3. How a job shows in Skyrim: one quest, four slots

`ALifeTalk.esp` also carries one quest, **ALifeTasks** (type Miscellaneous, not start-game-enabled, no stages that
matter, no scripts):

- 8 optional reference aliases: `Target0..3` and `Giver0..3`.
- 8 objectives: slot `s` has objective `10*s` (the work, quest target alias `Target<s>`) and `10*s+1` (return to the
  giver, quest target alias `Giver<s>`).
- The plugin writes each objective's display text at runtime (`BGSQuestObjective::displayText`) before showing it, so
  the journal reads like "Kill Hrolf the Red (Silver Hand) near Fort Greymoor" or "Bring 5 Wolf Pelts to Ulfgar".
  The text is not saved by the game: the plugin writes it again after every load from the co-save.
- The markers are **persistent XMarker references the plugin creates** (`PlaceObjectAtMe`, forced persistent; their
  FormIDs go into the co-save) and forces into the aliases (`TESQuest::ForceRefIntoAlias`). Squads are mostly virtual
  (offline), so the marker cannot be an actor: the plugin moves each marker to its squad's position every few seconds
  (the pool actor's position while online, the board position while offline). Compass and map follow it.
- Showing, completing and failing objectives goes through a small global Papyrus script (`ALife_Tasks.psc`: `Begin`,
  `Show` = `ForceRefTo` + `SetObjectiveDisplayed`, `Mark` = completed / failed / hidden), called with `DispatchStaticCall`;
  the engine's own functions update the journal, the HUD "objective" message and the player's objective-target list
  together. A finished objective leaves the journal 8 seconds later, so a reused slot never shows an old line with new text.

## 4. The four types

| Type | Anomaly | Target picked at the offer | Done when | Hand-in | Fails when |
|---|---|---|---|---|---|
| **Bounty** | kill a live stalker of an enemy/neutral faction (`tasks_bounty.script:55-204`), completes on the death whoever killed (`:92-112`) | a human squad of a faction at war with the giver's (`factions.ltx` enemy), not in a city, within `bounty_range` of the giver, avg level ≤ player + 6; the target is its leader | the target member is dead (any killer, offline or online) | return to the giver | giver squad wiped (squads only leave the board when their last member dies, so a vanished target counts as dead) |
| **Assault** | clear the enemy squads staying at a smart (`tasks_assault.script:79-122`, `:344-345`), their stay pinned on accept (`:484-510`) | a smart (not a city) where squads of factions at war with the giver are staying, including monster lairs (trolls, draugr, falmer ...: Anomaly assaults mutant nests too); within `assault_range` | every squad that held the place at acceptance is wiped; they are **pinned** there (no leaving, no retargeting) while the job runs | return to the giver | giver squad wiped |
| **Delivery** | carry a package to a sim squad commander of a non-enemy faction on another level (`tasks_delivery.script:46-120`), done at the recipient (`:301-311`) | a squad of a non-enemy faction at least `delivery_min` away from the giver; you get a sealed parcel (a quest MISC item from `ALifeTalk.esp`) | talk to the recipient's leader with the parcel ("I have a delivery for you") | at the recipient: the recipient pays | recipient squad wiped; parcel no longer in your pack (dropped, sold) for `parcel_grace_hours` (0.5 game hours) |
| **Fetch** | bring N items from `fetch_list.ltx` by rank tier (`tasks_fetch.script:314-456`), turn-in removes them and pays from their value (`:109-235`) | an item list from `tasks.ltx` `[fetch_<faction>]` (pelts for hunters, ore for mercenaries, ingredients for mages, potions, ...), count rolled in its range, tier by player level | the player holds N of them (checked every few seconds; the objective switches to "return") | return to the giver: the items are taken | giver squad wiped |

Expiry: Anomaly's generic jobs have no time limit; neither do these by default (`expire_days = 0`). Setting it fails a
job that is still open after that many game days.

## 5. Rewards and penalties

| Anomaly | ALife (`tasks.ltx [rewards]`) |
|---|---|
| money: bounty 8500-9500, assault 5500-10000, delivery 1500-4500 by distance (`reward_random_money_by_dist`), fetch = item cost x condition² x mult ±10%, min 50 (`tasks_fetch.script:202-206`) | gold: bounty `150 + 15 x target level`, assault `120 x squads + 10 x their levels`, delivery `80 + 25 per km` (straight line), fetch `1.3 x value of the items` (±10%, min 50); all x `gold_mult` |
| goodwill: +50 with the giver's faction on success, -25 on failure (`complete_task_inc_goodwill(50)`, `fail_task_dec_goodwill(25)`) | the same, through Standing (spills to allies like any deed); the giver squad's own memory +100 / -50 |
| random consumables (beer, vodka, cigarettes) | one consumable from the giver's real pack (food, drink, potion), if it has one |
| `reward_stash(true)` on 54 of 67 jobs | M5f (stashes): with `[reward] chance` (0.5) in `stashes.ltx` the job pays with a stash revealed near the giver (docs/STASHES.md; `src/Jobs.cpp` calls `StashPlugin::OnTaskDone`) |

Gold is paid from the plugin (the squad's pack is not emptied: in Anomaly the money also comes from nowhere). Penalties:
abandoning or a job expiring costs the -25; a giver who dies or a target that despawns costs nothing (no-one is left to
hold it against you). Delivery: losing the parcel fails the job with the penalty.

## 6. Co-save

A separate record `TASK` after `SIM2`: open jobs (slot, type, stage, giver squad id, target squad / member ids, pinned
squad ids, node, item FormID and count, reward, accepted time; objective texts are rebuilt), per-squad offers and
cooldowns, the 8 marker FormIDs. Version 3 (M5e) adds per job the destination, the mark list (hunt marks, defend
attackers), the progress step, the extra squad (rescue captive, recover guards), the corpse FormID, the deadline and the
squad description of the attackers. Versions 1 (pre-Solstheim factions) and 2 still load: their jobs get the new fields
empty, which is all an M5a job has. `SIM2` keeps its version: a save without `TASK` (any older save) just has no jobs. A
`TASK` record of an unknown version is skipped. The board's job sets (`pinned`, `forced`, `held`) are not saved: they are
rebuilt from the open jobs after every load.

## 7. Code

- `include/ALife/Tasks.h`, `src/core/Tasks.cpp`: engine-free model (offers, target choice, validity, completion,
  expiry, rewards, rankings). Tested headless: `tests/tasks_test.cpp` (`tests/build_tasks_test.bat`).
- `include/ALife/Jobs.h`, `src/Jobs.cpp`: plugin side (globals, quest/aliases/objectives, markers, inventory checks,
  payment, events, the escort / rescue party joins, the recover corpse).
- `config/tasks.ltx`: givers, ranges, rewards, fetch lists, corpse actors.
- `tools/build_talk_esp.py`: topics, the ALifeTasks quest, the parcel and satchel items, the two globals.
- `papyrus/ALife_Tasks.psc`: the objective calls.

Debug (bDebug): `SKSE/ALife.cmd` lines `task offer <type>` (the scenario squad, else the nearest human squad, offers that
job now), `task accept` (take it without the dialogue), `task list`, `task kill` (the open bounty's target, the hunt's
current mark, the defend wave or the rescue's captors die where they stand: in the world when on screen), `task summon`
(the open bounty's target squad comes on screen near the player; bounty only), `task where` (marker FormIDs, for
`console player.moveto <id>`), `task goto` (the player is moved to the open job's work marker: the escort's destination, the
recover corpse's place; not implemented: `src/Jobs.cpp` has no `goto` verb), `task rank` (the top 20 of the rankings). In a conversation, `Talk::DebugChoose` knows the job prompts too (`work`,
`accept`, `decline`, `turnin`, `deliver`, `abandon`).

## 8. M5e: escort, defend, rescue, recover, top-10 hunt

All five reuse sections 1-6: the same givers (`[givers]` weights per faction), sticky offers, four slots, the quest's
objectives and markers, hand-in at the giver ("About that job. It's done."), abandon, cooldown, goodwill +50 / -25 and
squad memory. What is new per type:

| Type | Anomaly | Offered when | On accept | Done (Ready) when | Fails when |
|---|---|---|---|---|---|
| **Escort** | guide a squad to a smart: it joins the actor as a companion, paid on arrival, half pay when late (`tasks_guide.script:282-449`, `axr_companions`) | the giver squad (caravans mostly) has a destination: a place in its worldspace `escort_min`..`escort_max` from it that its faction may go to (board target rules), and the party has room for it | the squad travels with the player as companions (docs/COMPANIONS.md: follow, teleport after the player, orders) | the squad is within `escort_arrive` of the destination | the squad is wiped (penalty: it died in your care); it leaves the party (dismissed: abandon penalty) |
| **Defend** | attack waves on a base, done when every attacker is dead (`tasks_defense.script:443-540`) | the giver squad is staying at a place outside a city; an enemy of its faction has a squad in this worldspace no more than `max_level_over` above the player (its squad description is used for the attackers); a spawn place `defend_spawn_min`..`max` away | the giver squad is held at its place (pinned); the first wave (one squad of that description) is created at the spawn place and sent at the place, held there once it arrives (`Board::forced`); the next wave starts when one is wiped, `defend_waves` in all | every wave is wiped (anyone's kills: the giver's squad fights too) | giver wiped (no penalty) |
| **Rescue** | a captive agent in an enemy squad: kill the others, he joins the actor, release him at the giver (`tasks_agent_rescue.script:277-400`) | an enemy squad of the giver (people: "taken"; monsters: "wounded and cornered") stays at a place outside a city within `rescue_range`, no more than `max_level_over` above the player | a one-man squad of the giver's faction (the captive) is created at that place; the captors are pinned; the captive is out of the simulation while held (`Board::held`: no offline fights, no moving, never on screen) | every captor squad is wiped: the captive joins the player (party) and the return objective points at the giver; hand-in once the captive is within `rescue_arrive` of the giver | the captive dies (penalty); giver wiped |
| **Recover** | a corpse with an item, critters around it, bring the item back (`tasks_recover_item_on_corpse.script:162-260`) | a place outside a city `recover_min`..`recover_max` from the giver, preferring one enemies or monsters hold | no enemy there: with `recover_guard_chance` a monster squad of the nearest kind is created there and pinned (Anomaly's critters); the plugin lays out the body once the player is within `recover_spawn` and the place is loaded: an actor of the giver's faction (`[recover] corpse_<faction>`, a leveled list) killed beside the place's world marker, the satchel (ALifeTalk.esp MISC) in its pack; the marker moves onto the body | the satchel is in the player's pack (back to open if it leaves it, as fetch) | giver wiped |
| **Hunt** (top 10) | the actor's top-10 list: kill them one by one from #10, a missing mark moves it on, done at stage 10 (`tasks_top_10.script:31-106`; rankings `pda.script:457-635`) | at least `hunt_min` of the ten best-ranked people of factions at war with the giver live in its worldspace | the marks are fixed (rank order); the journal and marker follow the current mark (the lowest-ranked first) | every mark is dead (any killer, offline or on screen) | giver wiped |

Rankings (Anomaly G15): every living person on the board who is not travelling with the player is scored by fighting
value (`Board::PowerOf`, Anomaly's rank) + `rank_kill_points` per kill + `rank_veteran` for veterans; `task rank` prints
the top 20 ("Most dangerous people in Skyrim"). Anomaly's top-10 task takes the ten best of every faction; ALife takes the
ten best of the giver's enemies (nobody in Skyrim pays you to murder their own).

Pay (`[rewards]`, all x `gold_mult`, min `gold_min`): escort `escort_base + escort_per_km` (straight line), half when it
arrives after `escort_hours_per_km` x km game hours (Anomaly: late = half the pay); defend `defend_per_wave x waves +
defend_per_level x attackers' level`; rescue `rescue_base + rescue_per_level x captors' levels`; recover `recover_base +
recover_per_km`; hunt `hunt_base + hunt_per_level x marks' levels`.

Dialogue: the pitches for offers 5-9 are new INFOs on *Got any work for me?*; *About that job. It's done.* and *I can't
finish that job.* also show in companion mode (`ALifeTalkMode` 4), so the squad you escort takes the hand-in where it
stands. Escort and the freed captive go through the party (docs/COMPANIONS.md): `Party::Join` (room only, no offer roll
or pay) and `Party::Dismiss` when the job ends; a job companion dismissed by the player fails its job.

Implementation notes (M5e step 2/3, Oct 18): `Board::forced` (squad -> place: sent there and held once there; defend waves, a freed
captive walking home) and `Board::held` (out of the simulation where it stands: no moving, no offline fights, never on screen;
the captive) are rebuilt by `Tasks::Rebind` / every `Update` from the open jobs. `Tasks::party` points at the plugin's party
(`Companions::Model()`); job joins and departures queue in `Tasks::partyEvents` and go through `Companions::ApplyJobEvents`.
A rescue whose captive finds the party full at the moment the captors die still turns Ready: the captive walks to the giver's place
on its own (`forced`), and the hand-in waits until it stands within `rescue_arrive` of the giver. The recover guard chance is
rolled at the offer (the guard is made on accept, only if nobody moved in since). `[recover] corpse_<faction>` is optional: a
faction without a list gets a fresh actor of a pool NPC base. The satchel is `ALifeTalk.esp` 0x80D (0x80E-0x80F kept free).
Escort deadline: `escort_hours_per_km` (3) x km, at least 1 game hour. Debug: `task kill` covers the hunt's mark, the defend wave and
the rescue's captors; `task rank` prints the top 20; `task summon` and `task goto` still cover the bounty only / do not exist yet.

Difficulty channels (T21, user Oct 18: "ensure scalability based on difficulty sliders as well"). Every balance number of the jobs
lives in `config/tasks.ltx`, tagged there with the channel it scales with; the rules read them only through `Tasks::Eff()`
(`TaskNumbers` = the config as written x `TaskScale`, applied in `Scaled()`), so T21 sets `Tasks::scale` and touches no rule.

| Channel | Keys | Applied as |
|---|---|---|
| enemy strength | `max_level_over`, `defend_wave_size` | x (levels rounded); a wave or guard squad is the description's roll x size, 1..`defend_wave_max` (5: the on-screen budget, 15 actors in 3 groups) |
| task difficulty | `defend_waves`, `recover_guard_chance`, `hunt_marks`, `hunt_min`, `goodwill_fail`, `memory_fail`; `escort_hours_per_km`, `parcel_grace_hours`, `expire_days` | x (waves 1..10, marks 3..10, chance <= 1); the time limits divided (harder = less time) |
| economy | `gold_mult`, `gold_min`, `goodwill_done`, `memory_done` (every job's pay goes through `gold_mult`) | x |
| not scaled | ranges, arrival distances, offer timing, `escort_late_pay`, ranking points | - |

M5e has no escort ambushes and no rescue / recover timers (the design has none; `expire_days` covers every job). tasks_test section 16
runs every type at 0.5x and 2x: each completes and pays, pay rises with the economy multiplier, waves and guards stay within the budget.
