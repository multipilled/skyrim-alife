# Wounded, help, finish and surrender (M5d)

Anomaly's stalkers rarely die cleanly: a badly hurt one drops, clutches the wound and calls for help; a squadmate runs over
with a medkit, an enemy walks up and finishes him, or the player decides. A losing enemy can throw down his weapon and beg.
ALife does the same with its squads, online with Skyrim's own bleedout and offline in the combat simulator, and every
choice the player makes goes through the standing rules (`docs/STANDING.md`).

Sources (Anomaly 1.5.3, local only; targeted reads): `xr_wounded.script` (wound states, the
20% threshold, autoheal after 90 s, `eat_medkit`), `xr_help_wounded.script` (a squadmate brings a medkit, rank +10..20),
`xrs_kill_wounded.script` (enemies finish the wounded; the player's armed presence makes a wounded enemy surrender,
`actor_cause_surrender_chance` 0.25 within 8 m), `dialogs.script:1040-1260` (`transfer_medkit`, the surrender answers:
weapons, a stash, a PDA or nothing; "let him go" = medkit + weapons taken), `xr_motivator.script:335-345` and
`game_statistics.script` (`killed_prisoners` -25 reputation, `wounded_helped` +10), `configs/ai_tweaks/xrs_kill_wounded.ltx`,
`xr_help_wounded.ltx`, `sim_offline_combat.script` (no wound state offline: OCS only kills).

## 1. Who gets wounded

| Anomaly | ALife (`config/wounded.ltx`) |
|---|---|
| A stalker under 20% health goes into a wound state (`hp_state = 20|wounded_heavy@help_heavy`) | a squad member of a human faction under `hp` (0.20) of its health goes **down**, with `chance` (0.7) per time it crosses the line (the rest fight on and die normally) |
| Zombified and monolith have their own rules (no help, suicide) | monster squads never go down (creatures die); `factions` lists who can (all human factions by default) |
| A wounded stalker does not fight, does not move, cannot trade | down = Skyrim **bleedout** (kneeling, holding the wound): no fighting, no moving, out of the fight for everyone |
| Offline (OCS) nothing is wounded: damage kills | a hit that would kill a member offline leaves it down instead with `ocs_chance` (0.35), at `down_hp` (5-15%) health |

Online, bleedout comes from three flags the plugin sets on the actor reference (`Actor::BOOL_FLAGS`): **protected** (only the
player can kill it; anyone else's blow puts it into bleedout), **no bleedout recovery** (it stays down until ALife lets it up)
and **can speak to essential down** (Papyrus `AllowBleedoutDialogue`: the player can talk to it while it kneels). The member is
taken out of the player-enemy faction and its aggression set to 0 while it is down, so a hostile squad's wounded can be talked
to; its health goes to 0 so the engine plays the bleedout. Nothing is set on the base NPC: pool actors are reused, so the flags
are cleared whenever a member gets up, dies or leaves the screen, and before an actor is given to another member.

## 2. What happens to a member who is down

| Anomaly | ALife |
|---|---|
| Calls for help (`help_heavy` sound) while the player is around | says a help line (a subtitle: "Help... someone, please...") every `call_seconds` (15) while the player is within `call_radius` (3000); the first time a notice: "Hrolf (Bandits) is wounded and calls for help" |
| A squadmate finds the nearest wounded within reach (`always_help_distance`), not while its own enemy sees it, comes over, gives a medkit (`xr_help_wounded.script:22-60`); the helper earns rank +10..20 | when its squad is not fighting, the nearest standing squadmate within `help_radius` (2500) walks over; on arrival it uses one of its healing potions on the wounded (if it has none, it patches the wound anyway, as Anomaly's spawned medkit); the wounded gets up with full health (potion) or `revive_hp` (0.3); the helper's rank points +10..20 |
| Autoheal: after 90 s the wounded uses a medkit and stands (`xr_wounded.script:143-152`, `eat_medkit`) | after `self_heal_seconds` (90 real seconds) it drinks a healing potion from its own pack if it has one (full health) or gets up at `revive_hp` |
| Enemies walk up and finish a wounded enemy (`xrs_kill_wounded.script:143-195`, 1.5-4.5 s after reaching it) | an enemy of the member's faction within `finish_radius` (1500) that is not fighting anyone still standing finishes it after `finish_delay` (2-5 s); the kill is credited to that enemy and its faction (death report, dynamic relations) |
| The player can talk to a wounded stalker (`dm_help_wounded_medkit_dialog`) | the player can talk to it (below) |

Offline, the combat simulator decides at the end of a fight (`Board::Battle`):
- a winner of a monster faction kills every down enemy (beasts do not take prisoners);
- a human winner finishes each down enemy with its faction's `finish` chance (outlaws and fanatics most, soldiers least);
- down members of a squad that still has someone standing are patched up at once (`revive_hp`): squadmates help;
- a squad with nobody left standing stays where it fell as **wounded survivors** for `down_hours` (2-6 game hours): it does not
  move, take part in offline fights, or get targeted by them; a news line calls for help ("Wounded Imperial Legion soldiers near
  Fort Greymoor are calling for help", cooldown per place). When the player comes near, the squad comes on screen with those
  members kneeling and calling for help. When the time runs out they get up at `revive_hp` and fall back (like a squad that fled).

## 3. The player and the wounded

Talking to a member who is down (talk mode 5, `ALifeTalkMode`), in ordinary Skyrim dialogue:

| Topic | Shown when | Then (applied after the menu closes) | Standing |
|---|---|---|---|
| *Here, drink this.* | the player carries a healing potion (`ALifeWoundHeal` = 1) | the weakest restore-health potion leaves the player's pack; the member gets up with full health | the faction's goodwill `wound_help` (+40, spills to allies, daily cap) and the squad's memory `wound_help` (+300). Anomaly `transfer_medkit`: an enemy becomes neutral, anyone else a friend; `wounded_helped` +10 reputation. A hostile squad that is helped stands down for `tollPassHours` (it owes you) |
| *Hand over your gold.* (loot) | always | its gold and its weapons go to the player; it stays down | its faction `rob` (-15, no spill) and squad memory -200 unless the squad was hostile to the player (looting someone who attacked you costs nothing) |
| *Your fight is over.* (finish) | always | the player kills it (the same as striking it while it kneels) | the kill itself (`Standing::OnKill`, witnessed by its squad) plus `finish` (-30 with its faction, spills); counts toward notoriety like a murder for lawful factions |

Striking a down member also kills it (protected actors die to the player) and counts as finishing it.

## 4. Surrender

| Anomaly | ALife |
|---|---|
| A wounded enemy near the armed player (8 m) may surrender: `actor_cause_surrender_chance` 0.25, rolled once (`xrs_kill_wounded.script:46-82`), communities in `cause_surrender_communities` | a down member of a squad hostile to the player, with the player within `yield_distance` (600) and a weapon drawn, **yields** with `yield_chance` (0.25), rolled once per member |
| (not in Anomaly; asked for in PARITY G8) | a whole squad fighting the player that is **losing** (`losing_share`: half or more of the members it started with are dead or down) or **outnumbered** (the player's side, player + companions + teammates, at least `outnumber` (1.5) times its standing members) surrenders with its faction's `surrender` chance, rolled once per fight: every standing member drops into the same kneeling state, yielded |
| The prisoner plays the surrender animation, the player talks to it (`dm_surrender_dialog`) | the yielded kneel (bleedout pose) and say "Mercy! I yield!"; talk mode 6 |
| "Let him go": medkit, his weapons are taken, he runs from the player (`surrender_victim_answers_2`) | *Go. And don't come back.* (spare): every yielded member of the squad gets up and the squad backs off from the player for `spare_hours`; its faction `spare` (+20, no spill), squad memory +250 |
| Answers: weapons + a stash (40%), a PDA, gold 500-3000 (bounties), or nothing (`surrender_victim_answers_1`, `_bounty`) | *Empty your pockets, then go.* (rob): every yielded member's gold and weapons go to the player, then they back off; `rob` as above (nothing when they attacked you); a stash marker joins the reward when M5f (stashes) lands (not implemented: M5f is on master, but `src/Wounded.cpp` gives no stash on a rob) |
| Killing a surrendered victim: `killed_prisoners`, -25 reputation (`xr_motivator.script:341`) | *No mercy.* or any killing blow on a yielded member: the kill, plus `prisoner` (-60 with its faction, spills) and notoriety `prisoner_notoriety` (+40) **even when its faction is hostile** |

A yielded squad that is left alone (the player walks beyond `release_radius` (3000), or `surrender_seconds` (120) pass without a
choice) gets up and backs off without any standing change. Factions with `surrender = 0` (fanatics: Thalmor, Forsworn,
necromancers, vampires, the Silver Hand, Reavers by default) fight to the end; wounded members of those factions still go down and
can still be helped, finished or looted.

## 5. Fit with the existing systems

- **Standing**: every deed is a `Standing` call (`OnWoundHelped`, `OnRob`, `OnFinish`, `OnSpare`, `OnPrisonerKilled`) with its
  amounts from `wounded.ltx` `[standing]` (kept there, not in `standing.ltx`, so all M5d balance goes through one read path, s8),
  so allies share the goodwill, the daily gain cap applies, memory decays as usual, and a squad's regard (and so its dialogue mode)
  follows from them.
- **Behaviour**: a squad member who is down is skipped by hostility (`TurnHostile`), the online combat driver (it neither starts nor
  receives a fight), assisting the player and fleeing; a fully yielded squad is not judged by `Behavior` until it is released.
- **Tasks**: a bounty target that is down is not dead: the bounty completes when it dies (finish it). Assault places count down
  squads as still there until they are finished or get up and leave.
- **Companions**: companion members go down and get helped like anyone (by each other or by the player).
- **News (M5g)**: wounded survivors call for help through the news feed (SOS); finishing is reported like any death.
- **Old saves** load with nobody down. Members' health is in `SIM2` as before.

## 6. Co-save

Record `WOND` (version 1), written after `COMP`: per member that is down its id, squad id, the game hour it went down, the hour an
offline squad gets up, its health before yielding, and flags (yielded, offline); the squads that surrendered or rolled the
surrender check in the current fight. A save without `WOND` has nobody down; an unknown version is skipped. `WOND` is a record of its own, like `TASK` and
`COMP`: the `SIM2` layout is unchanged, so its version stays 208 and 207 / 206 / 205 still load (`SaveFormat::Loads`). After a load, members
that were down online are down again as soon as their squad is back on screen.

## 7. Code

- `include/ALife/Wounds.h`, `src/core/Wounds.cpp`: engine-free model (who can go down, the online roll, offline lethal hits,
  after-battle finish / help / wounded survivors, recovery, surrender and yield decisions, co-save blob). `Board` calls it through
  `Board::wounds` (null: the board behaves exactly as before). Tested headless: `tests/wounds_test.cpp`
  (`tests/build_wounds_test.bat`).
- `src/core/Standing.cpp`: the deeds (amounts passed in from `WoundConfig`).
- `src/core/Board.cpp`: `Stands` / `StandingCount` (alive and not down); `SquadPower`, `Round`, `Battle`, `RunOcs`, `UpdateSquad`,
  activities and `Heal` skip the down; `Step` calls `Wounds::Tick` after the fights.
- `tests/harness.cpp`: wounds are on whenever `config/wounded.ltx` is there (as in the plugin); `--no-wounds` runs the old board,
  `--m5d` checks down / finish / help / survivors, stale records, survivors staying put and getting up in time.
- `include/ALife/Wounded.h`, `src/Wounded.cpp`: plugin side (bleedout flags, help calls, squadmate help, self-heal, enemies
  finishing, surrender, dialogue globals and choices, co-save `WOND`, debug verbs).
- `config/wounded.ltx`: thresholds, chances, distances, timers, per-faction finish and surrender chances.
- `tools/build_talk_esp.py`: global `ALifeWoundHeal`, talk modes 5 (wounded) and 6 (yielded), their opening lines and topics.

Debug (bDebug, `SKSE/ALife.cmd` lines or lab queue items): `wound list`, `wound down [n]` (the scenario squad's n-th member goes
down now), `wound yield` (the scenario squad surrenders now), `wound check` (run the surrender check with chance 1), `wound help` /
`wound rob` / `wound finish` / `wound spare` (the player's choice on the nearest down member, without the dialogue), `wound call`
(the nearest down member calls now), `wound ocs` (run one offline fight with a forced down). In a conversation, `choose` knows the
prompts `heal`, `rob`, `finish`, `spare`, `robgo`, `nomercy`.

## 8. Difficulty channels (T21)

Every balance number is a key of `config/wounded.ltx`, never a code constant (the compiled-in defaults only mirror the file), and
each key carries the difficulty channel it scales with. `LoadWoundConfig` reads the file into one `WoundConfig`;
`ScaleWoundConfig(base, WoundScale{ lethality, enemy, economy })` is the one place multipliers apply (m > 1 = harder, 1 = the file),
followed by `ClampWoundConfig` (chances and shares in [0, 1], times and distances >= 0, ranges ordered, NaN and negative
multipliers refused). T21 builds the sliders and `difficulty.ltx` and calls it; M5d has no difficulty logic of its own.

**Wired to T21 (docs/DIFFICULTY.md): the Mercy channel.** `Config::Diff(DiffChannel::Mercy)` (1.0 at Adept, above 1 is kinder)
goes into `WoundScale::mercy`. It multiplies `chance`, `ocs_chance`, `yield_chance` and `[surrender_chance]` (clamped to 1) and
divides `[finish]`. At 0 nobody goes down, yields or surrenders. At 1.0 every value stays exactly as loaded and no random number is
drawn that wasn't drawn before (wounds_test s12 runs the same seed both ways). The plugin keeps the file's values and rescales
whenever Mercy changes: game difficulty, the MCM slider, or a load restoring T21's `DIFF` record (a record of its own next to
`WOND`; neither touches the other). The harness scales its main run and every `--diff-sweep` run the same way, and the sweep checks
that offline downs rise with Mercy. T21 already applies its Lethality channel in `Board::Round`, so the `{lethality}` / `{enemy}` /
`{economy}` tags above stay at 1 for now: wiring them too would count those channels twice.

| Channel | Grows with m (`{x+}`) | Shrinks with m (`{x-}`) |
|---|---|---|
| lethality | `self_heal_seconds`, `finish_radius`, `down_hours`, `[finish]` per faction | `hp`, `chance`, `ocs_chance`, `down_hp`, `revive_hp`, `help_radius`, `finish_delay` |
| enemy strength | `helper_rank`, `losing_share`, `outnumber` | `yield_chance`, `[surrender_chance]` per faction (0 stays 0) |
| economy | `rob`, `rob_memory`, `finish`, `prisoner`, `prisoner_notoriety` (`[standing]`) | `wound_help`, `wound_help_memory`, `spare`, `spare_memory` |

Not scaled (design values): `enabled`, `factions`, `call_seconds`, `call_radius`, `fall_back_hours`, `yield_distance`,
`spare_hours`, `release_radius`, `surrender_seconds`. `wounds_test` section 11 runs a busy world with every chance at 0 and at 1 and
every channel at 0.5x and 2x: records stay valid, health stays in range, survivors get up in time, and the scaled values move the
documented way.
