# Camp life at smart terrains (M5h, PARITY G13)

Goal: a squad that stays at a place looks like it lives there. Members take jobs (guard, patrol, sleep, sit at the fire,
watch from high ground, lie in wait, gather), and a campfire with two or more people around it plays a small scene (one
tells a story or plays, the others listen). Anomaly reference: gulag jobs per smart (`gulag_general.script`), job choice by
priority (`smart_terrain.script`), the camp scene (`sr_camp.script`, `xr_campfire_point.script`), lighting fires
(`axr_turn_on_campfire.script`). No LLM anywhere: every line a member says is fixed text in config (a project rule).

Facts are marked [V] verified (read in the Anomaly scripts or ALife code, or seen in a run), [I] inferred, [U] unverified.

## 1. Anomaly: what happens at a smart

### 1.1 Job table (`gulag_general.script`)
- [V] `load_job` (`:1252-1330`) builds one job table per smart from auto-generated jobs (`init_*_jobs`) plus exclusive
  jobs from the smart's ltx (`work1..n` / `[exclusive]`, `add_exclusive_job` `:1183-1250`).
- [V] Priorities (`job_info_by_job_type_id`, `:282-302`): generic point 3, campfire point 10, sleep 10, walker 15,
  animpoint 15, follower 14, collector 25, patrol 25, guard 25, sniper 30, beh 30, camper 45, mob home 40, trader /
  mechanic 59, surge 60. The header comment (`:17-34`) gives sleep 20 and patrol "25, -1 per follower": the table is what
  runs, so sleep is 10 [V].
- [V] Preconditions (`:158-262`): sleep only 21:00-07:00 and never zombied (`precond_safe_job_sleeper`); patrol, sniper,
  collector, animpoint never zombied; collector only with a detector in the inventory; trader only with items to sell and
  not during an emission; surge jobs only during an emission. Guard and camper have no condition (the alarm checks are
  commented out).
- [V] Campfire points: `max_population x 3` per smart (`init_campfire_point_jobs` `:308-337`), anim `sit_ass` (zombied:
  `guard`), `use_camp = true` (joins the camp scene).
- [V] Guards come with a follower job each (`init_guard_jobs` `:654-660`, prior 14): a second man stands by the guard.
- [I] Path jobs (guard, patrol, sniper, camper, collector, walker, sleep) need hand-placed waypoint paths named
  `<smart>_<job>_<n>_walk` / `_look` (naming table `:46-66`); a smart without such paths has only campfire / generic point
  jobs. Anomaly's level designers placed them; Skyrim has none.

### 1.2 Who gets which job (`smart_terrain.script`)
- [V] `update_jobs` (`:582-616`) runs only while the smart is on the player's level (`is_on_actor_level`); squads that
  have arrived get an `npc_info`, and `select_npc_job` (`:626-700`) gives each the best free job: one NPC per job
  section (`npc_by_job_section`), a higher priority free job replaces the current one, the scan advances a few jobs per
  update (`STEP_SIZE`, `STEP_SIZE_OFFLINE`) instead of the whole table.
- [V] Monsters use `monster_jobs` (mob home, prior 40); helicopters their own table.

### 1.3 Camp scene (`sr_camp.script`, `xr_campfire_point.script`)
- [V] A camp manager per campfire: states idle (25-120 s) -> one of harmonica, guitar, story -> idle (`sr_camp.script:20-100`);
  a director is picked among the members at the fire (`:200-235`), the others listen; idle state plays a random
  "state" bark from one talker (`:180-190`); stories come from `story_table` (11 stories, `:4-17`).
- [V] Listener animations: sit, sit_ass, sit_knee, sit with weapon, eat bread, drink vodka, drink energy, eat sausage
  (`xr_campfire_point.script:21-24`); the director plays guitar / harmonica or sits and tells.
- [V] Seats: up to 6 directions around the fire at radius 8 m shrinking to 1 m to find a free level vertex
  (`xr_campfire_point.script:58-110`), else a random cover 5-15 m from the fire or the smart.
- [V] Lighting: a calm, unwounded NPC with no enemy and no emission walks to an unlit campfire of its smart and lights it
  (`axr_turn_on_campfire.script:145-186`; the night-only check is commented out).

## 2. ALife today
- [V] Stay at a place: `Online::StayDirector` (`src/Online.cpp:322-410`) walks the leader between 3-4 navmesh points
  around the place's marker (shape by place kind: forts patrol 800-2600 units, camps 400-1500) and pauses 8-55 s with the
  sandbox package; members follow the leader. A fight is never interrupted.
- [V] Packages are four templates copied from Skyrim.esm (travel, sandbox 1200 radius, follow leader, flee), picked by
  the member's rank in `ALifeModeFaction` (`tools/esp/build_esp.py:90-127`, `SetMode` `src/Online.cpp:113`). The target is
  a linked ref (keyword 0x961) that the plugin moves.
- [V] Offline: needs (hunger, fatigue, cold) and activities (rest at inns, hunt, fish, trade, raid, clear dungeons) in
  `src/core/Activities.cpp`; no per-member job.
- [V] Smart data (`GraphNode`, `include/ALife/Core.h:144-180`): kind, marker ref, position, radius, props, max
  population, inn / settlement / city / dungeon flags, vendors, cells. No job points.

## 3. Design

### 3.1 Job slots per smart (core, engine-free)
`config/camp.ltx` defines job kinds and, per place kind, how many slots of each a smart has:

    [jobs]                ; priority, Anomaly job_info_by_job_type_id
    camper     = 45       ; lie in wait by the approach (outlaws)
    sniper     = 30       ; archer on high ground
    guard      = 25
    patrol     = 25       ; -1 per follower, as Anomaly's comment
    collector  = 25       ; gathers plants / hunts small game near the place (hunters, mages, wanderers)
    walker     = 15       ; wanders the place (sandbox)
    campfire   = 10
    sleep      = 10       ; 21:00-07:00 only

    [slots_bandit_camp]   ; per place kind (Kind names), counts x max_population like Anomaly's campfire points
    campfire = 3
    sleep    = 2
    guard    = 1
    sniper   = 1
    camper   = 1
    walker   = 1

- [I] Skyrim has no authored job paths, so a slot is a job kind and a count, not a path. The plugin finds the spots when
  a squad arrives (3.3); a slot with no spot found falls back to walker.
- Core module `Camp` (new `src/core/Camp.cpp`, `include/ALife/Camp.h`): `AssignJobs(node, squads at it, hour, cfg)` returns
  one job per living member, in Anomaly's order: highest priority free slot whose condition holds; one member per slot
  across every squad at the place (`npc_by_job_section`); the leader takes patrol when the place has one (it drives the
  existing StayDirector walk), else guard. Conditions: sleep 21-07; no jobs for zombies but walker and guard
  (Anomaly zombied rules); monster squads get none (they keep the lair sandbox); archers first for sniper; camper only
  for outlaw factions (`standing.ltx` opportunism > 0); collector only for factions with `[activity_factions] hunt` or mages.
- Kept in step: re-run when the hour crosses a window (sleep on / off), a member dies, a squad arrives or leaves, or
  every `reassign_seconds` (30) at most: Anomaly's step-limited rescan without its per-update cost.

### 3.2 Online / offline
- Online only. Anomaly runs jobs only for the player's level [V 1.2]; ALife's equivalent is the on-screen squads (at most
  3 squads / 15 actors, `kMaxOnlineGroups`, `kMaxOnlineActors`). Offline squads keep today's stay behaviour and needs:
  no new offline state, nothing new in the co-save.
- One offline effect, cheap: a squad that stays at a place with a campfire or beds slot overnight recovers fatigue and
  cold at `rest_rate` (default the inn rate in Activities), so camps are worth staying at. Read from the slot table, no
  ref scan offline. (not implemented: `LoadCampConfig` reads `rest_rate` into `CampConfig::restRate`, but nothing uses it;
  see section 6.)
- A fight ends every job at once (members drop their packages, the engine's combat takes over, as StayDirector already
  does); after combat the squad is re-assigned on the next director tick.

### 3.3 Finding spots in the world (plugin)
- When an online squad reaches a place, scan once for refs within `scan_radius` (3000 units) of the place's marker:
  campfires, bedrolls / beds, seats (benches, chairs, logs), lean / idle markers, high ground for snipers. Cache the
  result per place for the session (not saved); invalidated when the cell detaches.
- [U] What to match: Skyrim furniture keywords (bed, bedroll, sit furniture, lean markers) and campfire bases (the
  vanilla "Campfire" statics / lights, the Hearthfire / Survival cooking spits). The exact FormIDs and keywords are to be
  read from Skyrim.esm with `tools/esp/fastscan.py` (FURN with keyword, STAT / LIGH bases) and listed in camp.ltx
  `[spots]`, the same way monsters.ltx lists creature bases. Nothing is assumed from memory.
- [U] Ref iteration: CommonLibSSE-NG `RE::TES::ForEachReferenceInRange` (check the v11 header before use); cost to be
  measured in the lab (one scan per arrival, a few thousand refs expected).
- Sniper spot: the highest navmesh point among the place's patrol points (the existing `PatrolPoints` snap) with a line
  of sight to the marker; camper spot: a point on the approach between the place and its nearest road node (the graph
  edge the squad came along), 1500-2500 units out.

### 3.4 Packages
Two new template packages in ALife.esp (tools/esp, rank in `ALifeModeFaction` as today; no hand edits of the esp):
- rank 5 "hold": travel to the linked ref, then stand facing out (guard, sniper, camper; snipers keep bows drawn [U]).
  Built as `ALife_PackCampHold` 0x924, a sandbox of radius 150 around the linked ref (`tools/esp/build_esp.py` `PACK_RADIUS`;
  section 6).
- rank 6 "sit / sleep at": sandbox with a small radius around the linked ref with sitting, sleeping and eating
  allowed (campfire, sleep, walker with a small radius). Built as `ALife_PackCampSit` 0x925, radius 450 (the design said 350). [I] The vanilla sandbox procedure already uses furniture in
  range; the radius keeps them at the fire or the beds.
Patrol stays the existing StayDirector walk; collector is a sandbox around the chosen flora / game spot.

### 3.5 Campfire scene
- A scene runs at a campfire with >= 2 members on campfire jobs (Anomaly sr_camp): idle `idle_min..idle_max` s
  (25-120), then story, music or idle again; the director is a random member at the fire, not the player's companion.
- Story: 3-5 unvoiced subtitle lines from `config/camp.ltx [stories_<faction>]`, fixed text (nothing generated at runtime), spoken
  through the existing talk path (ALifeTalk.esp topics built by `tools/build_talk_esp.py`; unvoiced lines stay on screen
  for their length, knowledge note skyrim-se-skse-plugins). Listeners use eating / drinking idles between lines.
  (not implemented: the talk path and the listener idles; step 3 shows each line as a HUD notification "Name: line" every
  6 s, `src/CampLife.cpp` `SceneTick`. `[scene] chatter_chance` is read but no chatter is played.)
- [U] Music: Skyrim's bard instruments (lute, drum, flute) are played by the bard scenes with their own idles; whether a
  plugin can start those idles on a pool actor (and with sound) must be checked in the lab before it is offered. Until
  then the scene has stories and idle chatter only.
- [U] Lighting fires: most vanilla camp fires are always-lit statics [I]; lighting is in scope only if unlit campfire
  refs exist at camps (check with fastscan) and toggling them is safe on a non-persistent ref.

### 3.6 CPU budget
- Core `AssignJobs`: at most 15 members x ~20 slots per place x 3 places, run on events or every 30 s: well under 0.1 ms
  [I]; headless micro-benchmark below.
- Plugin: one ref scan per place arrival (cached); package changes rate-limited to `max_package_changes` (2) per tick
  (`EvaluatePackage` is the expensive call); scene director at 1 Hz. Budget: camp life adds < 0.2 ms per frame on average
  and never more than 1 ms in one tick (measured with the existing `kStepBudgetMs` timing in the lab run).

### 3.7 Config keys and difficulty channels (docs/DIFFICULTY.md)
`config/camp.ltx`:

| Section / key | Meaning | Difficulty |
|---|---|---|
| `[jobs] <kind> = prior` | Anomaly priorities | - |
| `[slots_<place kind>] <job> = n` | slots per max_population | guard, sniper, camper counts x Diff(Strength), rounded, at least the base when > 1 (harder camps keep watch) |
| `[time] sleep_from, sleep_to` | 21, 7 | - |
| `[time] night_sleep_prior` | sleep's priority at night (20) | - |
| `[time] watch_share` | at night at most ceil(living x this) members stay on watch (0.25) | x Diff(Strength), clamped to 1 |
| `[scene] idle_min, idle_max, story_chance, chatter_chance` | sr_camp timings (25 / 120 s) | - |
| `[rest] rest_rate` | offline fatigue / cold recovery at a camp overnight (not implemented: read, unused) | - |
| `[budget] scan_radius, reassign_seconds, max_package_changes` | 3000, 30, 2 | - |
| `[spots]` (in `config/camp_spots.ltx`, generated by `tools/gen_camp_spots.py`) | Skyrim.esm FURN / STAT / LIGH bases and keywords per spot kind | - |
| `[stories_<faction>]` | campfire stories (fixed text) | - |

Every multiplier is read through `Config::Diff(...)`; at 1.0 no extra random numbers are drawn (T21 rule).

## 4. Tests

### 4.1 Headless (`tests/camp_test.cpp`, `tests/build_camp_test.bat`, core sources only)
1. camp.ltx parses; every place kind used by props.ltx has a slot table or the default.
2. Priority order: one bandit squad of 5 at a camp at noon gets camper, sniper (the archer), guard, campfire, campfire.
3. One member per slot across squads: two squads at one place never share a slot; the second squad gets the next free
   ones, then walker.
4. Sleep only 21:00-07:00; at 22:00 the sleepers move to beds and `watch_share` stay up.
5. Zombies: walker / guard only; monster squads: no jobs.
6. Outlaw factions only for camper; collector only for hunting factions and mages.
7. Re-assignment: a dead member frees its slot; a higher priority free slot takes a member off a lower one (Anomaly rule).
8. Difficulty: Diff(Strength) 0.7 / 1.0 / 1.35 gives non-decreasing guard + sniper + camper counts; 1.0 equals the
   untouched table and draws no random numbers.
9. Determinism: same seed, same assignment.
10. Budget: 10000 assignments of 15 members x 3 places < 50 ms (CPU cycles, QueryThreadCycleTime).
Plus: `harness` output unchanged (offline untouched apart from `rest_rate`); `--longrun --days 120` CPU p99 within 2 ms.

### 4.2 Lab (guarded run, after the headless pass; W1 owns the lane first)
- Scenario squad at a bandit camp with a fire (e.g. Halted Stream Camp) in daytime: `[camp]` log lines show the
  assignment; screenshots show one at the fire, one on watch, the archer on high ground.
- Fast-forward to 22:00: sleepers on bedrolls, one guard stays up.
- Two members at the fire: a story plays (subtitles), listeners idle; a fight breaks the scene and jobs, they come back
  after it.
- Clean ALife.log, frame budget within 3.6, 0 watchdog strikes, nothing left open.

## 5. Steps (proposal)
1. This design (done when committed).
2. fastscan pass over Skyrim.esm for `[spots]` (campfire / bed / seat bases and keywords) -> camp.ltx; core Camp + camp_test.
3. Plugin: spot scan, two packages (tools/esp), job director in Online, campfire scene + fixed stories; harness and
   long-run regression.
4. Lab run (4.2).

## 6. Status
- Step 1 (this design): 702909a.
- Step 2 (Oct 18): `include/ALife/Camp.h`, `src/core/Camp.cpp` (slots per place, Anomaly job choice, night watch share x
  Diff(Strength), no random numbers), `config/camp.ltx` (priorities, slot tables for 11 place kinds, time, scene, rest, budget,
  fixed stories for 6 factions), `tools/gen_camp_spots.py` -> `config/camp_spots.ltx` (from Skyrim.esm [V]: 22 lit
  campfires, MSTT `Campfire01LandBurning*` / LIGH `LightCampFire01`; 8 unlit, STAT `Campfire01LandOff*`; 110 beds / bedrolls,
  keyword FurnitureBedRoll + EDIDs; 118 seats; 6 lean / wall markers; 5 cooking spits and pots). Design changes found while
  writing: archers never take camper (sniper is theirs); at night sleep uses priority 20 (gulag_general's header value) so the
  beds fill before the fire, and at most ceil(living x watch_share x strength) stay on watch.
  `tests/camp_test.cpp` 30/0 [V]; budget: 10000 assignments of 15 members at a fort 66 Mcycles (~0.003 ms each) [V];
  tasks_test 217/0 and the default harness unchanged on the m5h-camplife branch (merged to master in dddb4b2).
- Step 3 (Oct 18, built, not run in game): `src/CampLife.cpp` (spot scan once per place with `TES::ForEachReferenceInRange`
  [V header], job director in `Online::Tick` before StayDirector, watch spots on job markers: guards on a golden-angle ring 500-1100
  out, the sniper on the highest of six snapped points 700-1500 out, the camper 1500-2500 out toward a neighbouring place;
  campfire / sleep on the nearest lit fire / a free bed; package budget 2 per tick; campfire scene: idle 25-120 s, a story told
  line by line as HUD notifications "Name: line" when the player is within 2500; bridge verb `camp`). ALife.esp (tools/esp
  build_esp.py): PACK 0x924 ALife_PackCampHold (rank 5, sandbox radius 150) and 0x925 ALife_PackCampSit (rank 6, radius 450),
  both from the sandbox template; REFR 0x980-0x98E job markers; both PKIDs appended to the 166 pool NPC_ (validate_esp PASS; the
  only differences from the lab's 2.0.0 ALife.esp are these records, the two PKIDs per NPC [V] and the HEDR count).
  MSVC 2 passes EXIT 0, 3 pex EXIT 0, camp_test 30/0, tasks 217/0, standing 98/0, civilwar 81/0, compat 419/0, party 61/0,
  wounds 168/0, harness default pass.
- Release note: `tools/package.py` (M7) builds ALife.esp with `tools/esp/build_esp.py` (camp packages and job markers included)
  and takes only the FaceGen from the previous release zip (FaceGen stays valid: the NPC_ FormIDs and head data do not change).
- Open [U], for the lab run (`tools/lab_camp.sh`, root): whether the sandbox template lets members sit and sleep in range
  (bedrolls at night), how a radius-150 sandbox reads as "standing guard", HUD lines vs real subtitles (the talk path is a later
  step), bard instruments (not offered).
- Not done: the offline `rest_rate` effect (deferred; offline stays unchanged).
