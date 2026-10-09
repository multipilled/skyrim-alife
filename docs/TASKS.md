# Tasks: work from squad leaders (M5a)

Anomaly's generic jobs ("got any work?") ported onto ALife's squads: a squad leader you are on speaking terms with may
have one job for you; you take it in ordinary Skyrim dialogue, it appears in your journal with a map and compass
marker, the world keeps simulating while you work, and you collect the pay from the leader who gave it.

Sources (Anomaly 1.5.3, local only, read through a summary and spot-checked): `axr_task_manager.script`,
`task_manager.script`, `task_objects.script`, `tasks_bounty.script`, `tasks_assault.script`, `tasks_delivery.script`,
`tasks_fetch.script`, `configs/misc/task/tm_dynamic.ltx` (67 generic sim tasks: fetch 32, assault 13, bounty 8,
delivery 3, ...). M5a ports the four types that make up 56 of the 67. The rest (escort, defend, rescue, recover, top-10
hunt) are M5e; stash rewards are M5f.

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

- `ALifeTaskOffer`: 0 none, 1 bounty, 2 assault, 3 delivery, 4 fetch (the squad's sticky offer, 0 when it has none or
  you hold `max_active` jobs).
- `ALifeTaskState`: 0 nothing between you, 1 you hold an unfinished job from this squad, 2 that job is ready to hand
  in, 3 you carry a package addressed to this squad.

Player topics (greeting mode only):

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
| **Bounty** | kill a live stalker of an enemy/neutral faction (`tasks_bounty.script:55-204`), completes on the death whoever killed (`:92-112`) | a human squad of a faction at war with the giver's (`factions.ltx` enemy), not in a city, within `bounty_range` of the giver, avg level ≤ player + 6; the target is its leader | the target member is dead (any killer, offline or online) | return to the giver giver squad wiped (squads only leave the board when their last member dies, so a vanished target counts as dead) |
| **Assault** | clear the enemy squads staying at a smart (`tasks_assault.script:79-122`, `:344-345`), their stay pinned on accept (`:484-510`) | a smart (not a city) where squads of factions at war with the giver are staying, including monster lairs (trolls, draugr, falmer ...: Anomaly assaults mutant nests too); within `assault_range` | every squad that held the place at acceptance is wiped; they are **pinned** there (no leaving, no retargeting) while the job runs | return to the giver | giver squad wiped |
| **Delivery** | carry a package to a sim squad commander of a non-enemy faction on another level (`tasks_delivery.script:46-120`), done at the recipient (`:301-311`) | a squad of a non-enemy faction at least `delivery_min` away from the giver; you get a sealed parcel (a quest MISC item from `ALifeTalk.esp`) | talk to the recipient's leader with the parcel ("I have a delivery for you") | at the recipient: the recipient pays | recipient squad wiped; parcel no longer in your pack (dropped, sold) for `parcel_grace` seconds |
| **Fetch** | bring N items from `fetch_list.ltx` by rank tier (`tasks_fetch.script:314-456`), turn-in removes them and pays from their value (`:109-235`) | an item list from `tasks.ltx` `[fetch_<faction>]` (pelts for hunters, ore for mercenaries, ingredients for mages, potions, ...), count rolled in its range, tier by player level | the player holds N of them (checked every few seconds; the objective switches to "return") | return to the giver: the items are taken | giver squad wiped |

Expiry: Anomaly's generic jobs have no time limit; neither do these by default (`expire_days = 0`). Setting it fails a
job that is still open after that many game days.

## 5. Rewards and penalties

| Anomaly | ALife (`tasks.ltx [rewards]`) |
|---|---|
| money: bounty 8500-9500, assault 5500-10000, delivery 1500-4500 by distance (`reward_random_money_by_dist`), fetch = item cost x condition² x mult ±10%, min 50 (`tasks_fetch.script:202-206`) | gold: bounty `150 + 15 x target level`, assault `120 x squads + 10 x their levels`, delivery `80 + 25 per km` (straight line), fetch `1.3 x value of the items` (±10%, min 50); all x `gold_mult` |
| goodwill: +50 with the giver's faction on success, -25 on failure (`complete_task_inc_goodwill(50)`, `fail_task_dec_goodwill(25)`) | the same, through Standing (spills to allies like any deed); the giver squad's own memory +100 / -50 |
| random consumables (beer, vodka, cigarettes) | one consumable from the giver's real pack (food, drink, potion), if it has one |
| `reward_stash(true)` on 54 of 67 jobs | M5f (stashes): hook only |

Gold is paid from the plugin (the squad's pack is not emptied: in Anomaly the money also comes from nowhere). Penalties:
abandoning or a job expiring costs the -25; a giver who dies or a target that despawns costs nothing (no-one is left to
hold it against you). Delivery: losing the parcel fails the job with the penalty.

## 6. Co-save

A separate record `TASK` (version 1) after `SIM2`: open jobs (slot, type, stage, giver squad id, target squad / member
ids, pinned squad ids, node, item FormID and count, reward, accepted time, objective texts are rebuilt), per-squad
offers and cooldowns, the 8 marker FormIDs. `SIM2` keeps its version (206): a save without `TASK` (any older save) just
has no jobs. A `TASK` record of an unknown version is skipped.

## 7. Code

- `include/ALife/Tasks.h`, `src/core/Tasks.cpp`: engine-free model (offers, target choice, validity, completion,
  expiry, rewards). Tested headless: `tests/tasks_test.cpp` (`tests/build_tasks_test.bat`).
- `include/ALife/Jobs.h`, `src/Jobs.cpp`: plugin side (globals, quest/aliases/objectives, markers, inventory checks, payment, events).
- `config/tasks.ltx`: givers, ranges, rewards, fetch lists.
- `tools/build_talk_esp.py`: topics, the ALifeTasks quest, the parcel item, the two globals.
- `papyrus/ALife_Tasks.psc`: the objective calls.

Debug (bDebug): `SKSE/ALife.cmd` lines `task offer <type>` (the nearest human squad offers that job now), `task accept`
(take it without the dialogue), `task list`, `task kill` (the open bounty's target dies where it stands: in the world when
on screen), `task where` (marker FormIDs, for `console player.moveto <id>`). In a conversation, `Talk::DebugChoose` knows
the job prompts too (`work`, `accept`, `decline`, `turnin`, `deliver`, `abandon`).
