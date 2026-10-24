# Companions: squads that travel with you (M5b)

Anomaly lets you take a friendly squad along, or hire a neutral one, and order it about. ALife does the same with its
squads: you ask in ordinary Skyrim dialogue, the whole squad leaves the A-Life simulation and follows you, you give it
orders by talking to any of its members, and it goes back to its own life when you part ways or the paid days run out.

Sources (Anomaly 1.5.3, local only, read through a summary and spot-checked): `axr_companions.script`,
`dialogs_axr_companion.script`, `txr_paid_companions.script`, `ui_companion_inv.script`,
`configs/ai_tweaks/axr_companions.ltx`.

## 1. Who joins, and for what

| Anomaly | ALife (`config/companions.ltx`) |
|---|---|
| Only a squad **commander** can be asked (`axr_companions.script:314-324`) | the topic is offered by any member, the whole squad answers (squads are the unit everywhere in ALife) |
| Free when the NPC is a **friend** (`allow_only_friends_as_companions`, `dialogs_axr_companion.script:268-285`) | free when the squad regards you **Friendly** or **Allied** (`docs/STANDING.md`) |
| Paid when **neutral** and of a paid faction: stalker, freedom, killer, bandit (`txr_paid_companions.script:11-18`, `:33-49`) | paid when **Neutral** and of a faction in `paid_factions`: wanderer (stalker), mercenary (killer), bandit, hunter (Skyrim's freelancers in place of Freedom, whose analogue is the Stormcloak army), reaver (Solstheim's raiders, when neutral; the Skaal and the Redoran guard do not hire out) |
| Not every paid squad works: veteran 70%, advanced 40%, others never (`:52-83`), rolled once per squad and remembered | `willing_chance` per tier (0 / 40 / 70%), rolled once per squad and kept for the session |
| Price = squad factor x 1500 (one day) or x 3000 (two days); factor novice 0.8-1.2, advanced 1.3-1.6, veteran 1.8-2.4 (`:52-83`, `:127-152`) | `day_gold` (250) x the same factors, `days = 1, 2`; the gold comes off the player when the contract starts |
| Contract ends after the paid game time (`:222-266`, a 10 s check on game time) | the same; one hour before the end a notice, at the end the squad leaves |
| Max NPCs = `max_actor_squad_size` 2 (4 with the Patriarch achievement), counting the asking squad's size (`dialogs_axr_companion.script:256-261`) | `max_members` (6) living companions in all, and at most `max_squads` (2) squads (each holds one of the three on-screen slots; one stays for the world). More than Anomaly's 2 because ALife squads are 2-5 strong, where Anomaly's recruitable sim squads of that size were 1-2 men |
| No room / no work: the topic is not offered | the squad says why (too many already, or "we have our own road") |
| Story / task squads never join (`is_non_story`) | squads held by a job (assault pins, a bounty target, a delivery recipient) do not join |

A squad that regards you Unfriendly or worse never joins. Hostile squads do not talk at all.

## 2. What joining does

Anomaly: `companion_squads[id]`, `SIMBOARD:assign_squad_to_smart(squad, nil)`, `scripted_target = "actor"`, the companion
scheme on every member (`axr_companions.script:344-404`). ALife:

- The squad **leaves the simulation**: the board skips it (no targets, no smart population, no offline combat, no
  activities, no level pacing) while it is in `Board::companions`. It keeps its id, members, packs and memory.
- It stays **on screen** while it follows you (its pool actors are kept; it holds one of the three online slots), and the
  online layer leaves it to the companion code (no board driving, no tolls or challenges, no going offline by distance).
- Every member gets the vanilla **teammate** flag (`SetPlayerTeammate`, through `ALife_Companions.psc`), so the engine
  treats them like followers: they sneak when you sneak, are not crime witnesses against you, help in your fights and
  follow you through doors. They are not in `CurrentFollowerFaction`, so vanilla followers (Lydia, hirelings, the dog)
  are untouched and can travel with you at the same time; ALife's limit counts only its own companions.
- **Teleport** (Anomaly `on_level_changing`, `:115-132`): a following member that ends up more than `teleport_distance`
  away, or in another cell after you passed a door, is moved to your side once out of your sight. Waiting members are
  never moved (Anomaly blocks teleport for `npcx_beh_wait` too).

## 3. Orders (talk to any member)

Anomaly has a radial menu and dialogue for these (`axr_companions.script:865-935`); ALife uses dialogue only, gated by
the squad's current state so only the orders that change something are offered.

| Order | Anomaly | ALife |
|---|---|---|
| *Follow me.* | clear wait / patrol, `fight_from_point` cleared | every member follows you (package mode 3, linked to the player) |
| *Wait here.* | `npcx_beh_wait` at its level vertex | the squad holds where it stands (sandbox around a marker at the leader); after a load it is put back there |
| *Go up ahead and hold there.* | move to the point the actor aims at (`:1199-1248`) | the point 1500 units beyond the speaker, away from you (you face the speaker while talking), snapped to the navmesh; the leader travels there, the rest follow, then they hold |
| *Hold your fire.* | `ignore_combat` | aggression 0, and any fight they are in is broken off (they still flee from harm) |
| *Fight anything that threatens us.* | clear `ignore_combat` | aggression 1: they attack enemies on sight and join your fights (default) |
| *Help yourselves to what the dead carry.* / *Leave the dead be.* | `gather_items` / `loot_corpses` | ALife's scavenging (`src/Scavenge.cpp`) runs for this squad (off by default); afterwards they fall back in |
| *Let me see your gear.* | `ui_companion_inv` (take only assigned items, weight cap 30) | the vanilla follower share menu on that member (`OpenInventory`); what you hand over is theirs (read back into the squad's pack when they leave the screen or the game saves) |
| *We part ways here.* | `remove_from_actor_squad`: scripted target nil, scheme restored | the squad rejoins the simulation where it stands and picks its next target; a paid contract is not refunded |

Not ported: stealth / near-far (the engine's follower behaviour already sneaks with you), patrol waypoints, the radial menu.

## 4. Leaving

- **Dismissed** in dialogue, **contract over**, or the squad turned on you (regard below Neutral, e.g. you struck them): it
  rejoins the board. Teammate flags come off before anything else.
- **Wiped out**: removed like any squad; a HUD notification (not a News page entry): "<leader>'s band fell while travelling with you".
- **Dismissed in another worldspace** (a Tamriel squad taken to Solstheim, or the other way round): the squad settles there.
  It takes the player's worldspace and the nearest place there it could target (`Board::NearestValidPlace`) and goes on as a
  squad of that worldspace. This is the one exception to "squads never change worldspace" (`Party::Dismiss`). If its faction
  is blocked in that worldspace (stood down or gated) or no place there suits it, the dismissal is refused: the band says
  "Not today." and keeps travelling with you. A squad that turns on you there with no such place leaves anyway and goes back
  to its own worldspace (its last position there). Dismissed inside an interior, a squad keeps its own worldspace, as does
  one whose contract runs out.

## 5. Co-save

Record `COMP` (version 1), written after `TASK`: per companion squad its id, paid flag, contract end (game hours),
order, fire and loot flags, the wait point (cell / worldspace FormIDs + position), plus the willing rolls and price factors
already made. A save without `COMP` (any older save) has no companions; a record of an unknown version is skipped. Before
`SIM2` is written, each companion's pack is read back from its actor, so gear handed over survives the save.
After a load the board's squads come back offline: following companions are placed at your side, waiting ones at their
wait point when you are near it.

## 6. Code

- `include/ALife/Party.h`, `src/core/Party.cpp`: engine-free model (offers, prices, room, recruit / dismiss, orders,
  contracts, save). Tested headless: `tests/party_test.cpp` (`tests/build_party_test.bat`).
- `include/ALife/Companions.h`, `src/Companions.cpp`: plugin side (globals, packages, teammate flags, teleport, gear,
  hold fire, events, co-save `COMP`).
- `config/companions.ltx`: limits, paid factions, prices, chances, distances.
- `tools/build_talk_esp.py`: the companion globals, a fourth talk mode (companion), the recruit and order topics.
- `papyrus/ALife_Companions.psc`: `Team(actor, on)` = `SetPlayerTeammate`, `Gear(actor)` = `OpenInventory(true)`.

Debug (bDebug, `SKSE/ALife.cmd` lines or lab queue items): `comp list`, `comp like <n>` (the scenario squad's memory
of the player = n), `comp willing` (its paid roll succeeds), `comp recruit [days]` (join without the dialogue),
`comp order follow|wait|move|hold|attack|loot|noloot|dismiss`, `comp foe` (a hostile squad comes on screen near you),
`comp fight` (each companion's combat state), `comp away` (teleport check: the companions are moved 6000 units off).
In a conversation, `choose` knows the companion prompts (`travel`, `day1`, `day2`, `follow`, `wait`, `move`, `hold`,
`attack`, `loot`, `noloot`, `gear`, `dismiss`).
