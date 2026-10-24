# ALife co-save format

What ALife writes into the SKSE co-save (`<save>.skse`, next to the game's `.ess`), record by record, and how each loader
reads older versions. The decoder `tools/cosave_decode.py` implements every layout below; `tools/tests/test_cosave_decode.py`
round-trips a synthetic record of each type and version and checks this file against the source (record codes, current
versions, and that every `file:line` cited here exists).

    python3 tools/cosave_decode.py <save>.skse          # type, version and fields of each ALife record (long arrays shortened)
    python3 tools/cosave_decode.py --full <save>.skse   # every array element
    python3 tools/cosave_decode.py --json <save>.skse   # the same as JSON

Exit 0: all ALife records decoded. Exit 1: something was skipped (an unknown type or version, a truncated record or file;
each prints a `WARNING` line and the decoder goes on). Exit 2: not an SKSE co-save.

Line numbers are as of master d0fc955.

## 1. The container (SKSE)

SKSE64 writes the co-save itself; a plugin only hands it records. Source: `skse64/Serialization.cpp` in
<https://github.com/ianpatt/skse64> (read at commit 25b7235): the "file format internals" block (`Header`, `PluginHeader`,
`ChunkHeader`), `HandleSaveGlobalData`, `OpenRecord` / `FlushWriteChunk` and `HandleLoadGame`. All fields are
little-endian `UInt32`.

| Part | Fields | Notes |
|---|---|---|
| `Header` (20 bytes) | `signature`, `formatVersion`, `skseVersion`, `runtimeVersion`, `numPlugins` | `signature = MACRO_SWAP32('SKSE')`, so the file starts with the bytes `SKSE`; `formatVersion = 1`. |
| `PluginHeader` (12 bytes), one per plugin | `signature` (the plugin's unique id), `numChunks`, `length` | `length` = bytes of all its chunks, chunk headers included. Only plugins that wrote at least one record are in the file. |
| `ChunkHeader` (12 bytes), `numChunks` per plugin | `type`, `version`, `length` | then `length` bytes of record data. |

- The loader walks plugin headers while 12 bytes remain (not by `numPlugins`) and jumps to `pluginStart + length` after each
  plugin, so a plugin that reads too little or too much does not disturb the next one.
- ALife's id is `'ALIF'` (`src/Save.cpp:17`, set at `src/Save.cpp:359`). Ids and record types are MSVC multi-character
  constants stored as a plain `UInt32` (no swap), so `'ALIF'` = `0x414C4946` appears in the file as the bytes `FILA`, and
  `'SIM2'` as `2MIS`.
- A save made before ALife's board is ready has no `ALIF` plugin at all: `SaveCb` returns first (`src/Save.cpp:91`).

## 2. Encoding inside a record

Every record is built with the same little writer: each value is copied with its in-memory size (`W::v`,
`src/Save.cpp:32`; the same `Bytes` struct in `src/core/Tasks.cpp:106-111`, `src/core/Party.cpp:231-236`,
`src/core/Wounds.cpp:569-573`, `src/core/Stashes.cpp:375-379`). No padding and no alignment: fields follow each other.
Sizes are those of the x64 MSVC build.

| Type | Bytes | |
|---|---|---|
| `u8` / `i8` / enum | 1 | every enum (`Faction`, `Tier`, `Role`, `TaskType` ...) is `std::uint8_t` (`include/ALife/Core.h:23`, `:42-75`, `:189`; `include/ALife/Tasks.h:26-33`; `include/ALife/Party.h:26`) |
| `bool` | 1 | 0 or 1 |
| `i32` / `u32` | 4 | |
| `u64` | 8 | ids of squads and members |
| `f32` / `f64` | 4 / 8 | game hours are `f64` |
| `form` | 4 | a FormID as the game had it at save time; the loader maps it to the current load order with `ResolveFormID` (SIM2 at read time, `src/Save.cpp:58-64`; the others after their core `Load`) |
| `str` | 4 + n | `u32` byte count, then the bytes, no terminator (`src/Save.cpp:33`) |
| `T[n]` | | `u32` count then the items, unless the count is fixed (marked below) |

A truncated record is detected by every reader (`ok = false` once a read passes the end): `src/Save.cpp:45`,
`src/core/Tasks.cpp:120`, `src/core/Party.cpp:245`, `src/core/Wounds.cpp:582`, `src/core/Stashes.cpp:388`.

Faction bytes: index into `enum Faction` (`include/ALife/Core.h:23-33`; 32 factions today, `kNoFaction = 255`,
`include/ALife/Core.h:36`). Saves written before SIM2 208 (and TASK 1) numbered the monster factions from 15; the one
shift rule is `SaveFormat::FactionIndexOf` (`include/ALife/SaveFormat.h:25-28`): an old index >= 15 (not 255) becomes
index + 3. The decoder prints such bytes as `19 (spriggan, now 22)`.

## 3. Records

Written in this order by `SaveCb` (`src/Save.cpp:88-189`), every save:

| Record | Current version | Written by | Read by | Loads versions |
|---|---|---|---|---|
| `SIM2` | 208 | `src/Save.cpp:155` | `src/Save.cpp:244-337` | 205, 206, 207, 208 |
| `TASK` | 3 | `src/Jobs.cpp:498` | `src/Jobs.cpp:501-525` | 1, 2, 3 |
| `COMP` | 1 | `src/Companions.cpp:530` | `src/Companions.cpp:534-547` | 1 |
| `WOND` | 1 | `src/Wounded.cpp:549` | `src/Wounded.cpp:553-560` | 1 |
| `STSH` | 1 | `src/Stashes.cpp:296` | `src/Stashes.cpp:300-315` | 1 |
| `NEWS` | 1 | `src/Save.cpp:172` | `src/Save.cpp:229-243` | 1 |
| `DRGN` | 1 | `src/Save.cpp:179` | `src/Save.cpp:217-226` | 1 |
| `DIFF` | 1 | `src/Save.cpp:187` | `src/Save.cpp:199-214` | 1 |

`LoadCb` (`src/Save.cpp:191-346`) dispatches on type; a type it does not know, or a version a reader does not accept, is
skipped (SKSE moves on to the next chunk). A record that is missing means "start that system afresh" (each one's
comment at `src/Save.cpp:159-181`); `DRGN` missing sets the dragon timer to now (`src/Save.cpp:339-342`), `DIFF` missing
reloads the sliders from ALife.ini (`src/Save.cpp:343`).

### `SIM2`: the board

Squads, members, places, merchants, standing. `SaveCb` `src/Save.cpp:96-158`, loader `src/Save.cpp:244-337`. Listed
versions: `SIM2` v208 (current), `SIM2` v207, `SIM2` v206, `SIM2` v205. The SIM2 version constants live in
`include/ALife/SaveFormat.h:10-15`.

Header and standing:

| # | Field | Type | Source (write / read) |
|---|---|---|---|
| 1 | `now` (game hours) | f64 | `src/Save.cpp:96` / `:255` |
| 2 | `gateOpened` | bool | `:96` / `:255` |
| 3 | `nextId` | u64 | `:96` / `:255` |
| 4 | `worldSeed` | u64 | `:96` / `:255` |
| 5 | goodwill count (207+ only) | u8 | `:99` / `include/ALife/SaveFormat.h:39` |
| 6 | `goodwill[count]` per faction | f32 | `src/Save.cpp:100` / `include/ALife/SaveFormat.h:40-43` |
| 7 | `notoriety` | f32 | `src/Save.cpp:101` / `include/ALife/SaveFormat.h:44` |
| 8 | `rumors[]`: `faction` u8, `delta` f32, `dueHours` f64 | | `src/Save.cpp:102-103` / `include/ALife/SaveFormat.h:45-51` |
| 9 | `waitlist[]`: member id | u64 | `src/Save.cpp:105-106` / `:257` |
| 10 | `queuedKills[]`: `cell` form, `kills` i32 | | `:107-108` / `:258` |
| 11 | `smarts[]` (places, below) | | `:111-121` / `:260-277` |
| 12 | `merchants[]`: `chest` form, `baselineGold` i32, `itemsBought` i32, `goldStolen` i32, `cycleStart` f64 | | `:123-125` / `:280-287` |
| 13 | `squads[]` (below) | | `:127-153` / `:290-325` |
| 14 | `creatures[]` (206+ only): monster refs in the world at save time | form | `:154` / `:326-330` |

Place (`smarts[]` item): `location` form (the node's BGSLocation), `lastRespawn` f64, `faction` u8 (owner, 255 none),
`raidedUntil` f64, then `respawn[]`: `name` str (the respawn entry in the .ltx), `spawned` i32. On load a place is found
by location and entries by name, so editing the .ltx files never shifts an old save (`src/Save.cpp:13-14`, `:265-276`).

Squad (`squads[]` item), in order (`src/Save.cpp:130-140` / `:292-305`). Node fields are written as the node's
location FormID, 0 for none (`WNode` / `RNode`, `src/Save.cpp:75-86`):

| Field | Type | Field | Type | Field | Type |
|---|---|---|---|---|---|
| `id` | u64 | `faction` | u8 | `tier` | u8 |
| `section` | str | `x`, `y`, `z` | f32 x3 | `node` | form |
| `prevNode` | form | `activity` | u8 (`Act`) | `actStep` | i32 |
| `actHours` | f32 | `action` | u8 | `assignedTarget` | form |
| `currentTarget` | form | `smartAssigned` | form | `stayStart` | f64 |
| `idleHours` | f64 | `locked` | bool | `lockedUntil` | f64 |
| `respawnSmart` | form | `respawnEntry` | str | `retreatUntilHours` | f64 |
| `avoidNode` | form | `commonSquad` | bool | `standing` | i32 |
| `letGoCount` | i32 | `foughtPlayer` | bool | `attackedByPlayer` | bool |
| `lastMetHours` | f64 | `trust` | u8 | `helpedByPlayer` | bool |
| `startStrength` | f32 | `startMembers` | i32 | `memory` | f32 |
| `tollUntil` | f64 | `members[]` | below | | |

Member (`members[]` item), in order (`src/Save.cpp:142-151` / `:306-321`; struct `include/ALife/Core.h:84-115`):
`id` u64, `name` str, `title` str, `faction` u8, `role` u8, `race` u8, `sex` u8, `classId` form, `level` i32,
`levelBias` i32, `gearLevel` i32, `skills` f32 x 18 (fixed, `kSkillCount`, `include/ALife/Core.h:61-66`), `health`,
`magicka`, `stamina`, `healthMax`, `magickaMax`, `staminaMax` f32, `gold` i32, `inventory[]` (`form` form, `count` i32),
`equipped[]` form, `perks[]` form, `spells[]` form, `hunger`, `cold`, `fatigue` f32, `sticky`, `veteran`, `waitlisted`
bool, `poolLocal` i32, `growthDay` u32, `gainToday` u8 x 18 (fixed), `fightBonusToday` f32, `rankXp` f32, `kills` i32,
`arrows` i32.

Versions (the loader reads one layout for 205-208; only rows 5, 6 and 14 differ; `SaveFormat::Loads`,
`include/ALife/SaveFormat.h:18`):

| Version | Lacks, compared with the next | How the loader upgrades it |
|---|---|---|
| `SIM2` v208 | (current) Skaal, Redoran, Reaver at 15-17, monsters from 18 | none. Since M4c (`include/ALife/Core.h:30`) the Dragon faction is appended without a version bump, so a 208 save has 31 or 32 goodwill entries; the count byte covers both. |
| `SIM2` v207 | the Solstheim factions sit after the monsters: monster bytes are 15.. | every faction byte (squad, member, place owner, rumour, goodwill index) goes through `FactionIndexOf` (`src/Save.cpp:250`, `include/ALife/SaveFormat.h:42`, `:47`) |
| `SIM2` v206 | the goodwill count byte (row 5): always 21 entries | `ReadStanding` reads 21 (`include/ALife/SaveFormat.h:39`); factions added since keep 0; faction shift as 207 |
| `SIM2` v205 | the six monster factions' goodwill (15 entries, no count byte) and `creatures[]` (row 14) | reads 15 entries; no creature sweep (`src/Save.cpp:326`); faction shift as 207 |
| 204 and older | 204: 15th faction (caravans); 203: `levelBias` / `gearLevel`; 202: standing (`src/Save.cpp:27`) | not loaded: the record is skipped and the world is populated afresh (`src/Save.cpp:244`, `include/ALife/SaveFormat.h:17`) |

A truncated SIM2 (`!r.ok` after the read) clears the squads and the persistent state: "starting clean"
(`src/Save.cpp:331-334`).

### `TASK`: jobs

Core blob `Tasks::Save` / `Tasks::Load` (`src/core/Tasks.cpp:1201-1211` / `:1213-1238`), one job `SaveTask` / `LoadTask`
(`src/core/Tasks.cpp:135-148` / `:150-173`); the plugin appends its quest markers (`src/Jobs.cpp:493-499`).
Listed versions: `TASK` v3 (current), `TASK` v2, `TASK` v1 (`include/ALife/Tasks.h:261`).

| # | Field | Type |
|---|---|---|
| 1 | `nextId` | u32 |
| 2 | `active[]`: a job | |
| 3 | `offers[]`: `giver` u64 (squad), `until` f64, then a job | |
| 4 | `cooldown[]`: `giver` u64, `until` f64 | |
| 5 | `markers`: 8 forms (fixed; `2 * kMaxSlots`, `src/Jobs.cpp:19`, `:30`), Target0..3 then Giver0..3 | form x 8 |

Job: `id` u32, `type` u8 (`TaskType`), `stage` u8, `slot` i32, `giver` u64, `giverFaction` u8, `giverName` str, `target`
u64, `targetMember` u64, `targetFaction` u8, `targetName` str, `pinned[]` u64, `node` u32 (graph node index), `place`
str, `item` form, `itemName` str, `count` i32, `gold` i32, `offeredAt` f64, `acceptedAt` f64, `parcelMissingSince` f64;
v3 adds `dest` u32, `marks[]` (`squad` u64, `member` u64), `step` i32, `extra` u64, `corpse` form, `deadline` f64,
`squadDesc` str (`src/core/Tasks.cpp:143-147`; fields `include/ALife/Tasks.h:129-161`).

| Version | Lacks | Upgrade |
|---|---|---|
| `TASK` v3 | (current) | |
| `TASK` v2 | the M5e tail (`dest` .. `squadDesc`) | the fields keep their defaults: no escort, defend, rescue, recover, hunt state (`src/core/Tasks.cpp:160`; `include/ALife/Tasks.h:151`) |
| `TASK` v1 | same layout as v2, monster faction bytes numbered from 15 | `giverFaction` / `targetFaction` shifted (`src/core/Tasks.cpp:171`) |

Loader limits (the decoder prints raw values; these are what the game would reject): at most 16 active jobs, `slot`
0..3, fewer than 100000 offers / cooldowns, strings up to 4096 bytes, fewer than 64 pinned squads, at most 64 marks,
`type` < 10 and faction bytes < 32 or the record is dropped (`src/core/Tasks.cpp:1219-1233`, `:128`, `:156`, `:162`,
`:172`). Markers are read leniently: missing ones are 0 (`src/Jobs.cpp:507-513`). Job FormIDs are resolved after
`Load` (`src/Jobs.cpp:514-521`).

### `COMP`: companions

`Party::Save` / `Party::Load` (`src/core/Party.cpp:261-272` / `:274-296`), plugin side `src/Companions.cpp:526-547`.
Listed version: `COMP` v1 (current; `include/ALife/Party.h:123`), the only one: another version is rejected
(`src/core/Party.cpp:277`).

1. `companions[]`: `squad` u64, `name` str, `paid` bool, `joinedAt` f64, `until` f64, `warned` bool, `order` u8 (`Order`:
   follow, wait, move), `holdFire` bool, `loot` bool, `cell` form, `ws` form, `wx`, `wy`, `wz` f32
   (`src/core/Party.cpp:264-268`).
2. `rolls[]`: `squad` u64, `willing` bool, `factor` f32 (`src/core/Party.cpp:270-271`).

Loader limits: at most 16 companions, fewer than 100000 rolls, names up to 4096 bytes, an `order` past `move` becomes
`follow` (`src/core/Party.cpp:279-292`, `:253`). `cell` / `ws` are resolved after `Load` (`src/Companions.cpp:539-544`).

### `WOND`: wounded and surrender

`Wounds::Save` / `Wounds::Load` (`src/core/Wounds.cpp:590-599` / `:601-621`), plugin side `src/Wounded.cpp:545-560`.
Listed version: `WOND` v1 (current; `include/ALife/Wounds.h:197`); another version is rejected (`src/core/Wounds.cpp:604`).

1. `down[]`: `member` u64, `squad` u64, `since` f64, `upAt` f64, `healthBefore` f32, `flags` u8 (1 yielded, 2 offline,
   4 called; `include/ALife/Wounds.h:78`) (`src/core/Wounds.cpp:594-596`).
2. `fights[]`: `squad` u64, `rolled` bool, `yielded` bool (`src/core/Wounds.cpp:597-598`).

Loader limits: at most 100000 of each; a `member` of 0 is dropped (`src/core/Wounds.cpp:606-618`).

### `STSH`: stashes

`Stashes::Save` / `Stashes::Load` (`src/core/Stashes.cpp:396-408` / `:410-439`), then the plugin's 32 marker refs
(`src/Stashes.cpp:18`, `:24`, `:295`). Listed version: `STSH` v1 (current; `include/ALife/Stashes.h:142`); another
version is rejected (`src/core/Stashes.cpp:414`).

1. `spots[]`: `id` u32, `node` u32, `ws` u8, `x`, `y`, `z` f32, `base` form, `state` u8 (0 empty, 1 filled), `rolled`
   bool, `slot` i8 (-1 unmarked), `ref` form, `markedAt` f64, `items[]` (`form` form, `count` i32)
   (`src/core/Stashes.cpp:399-405`).
2. `found` u64, `notesRead` u64, `revealed` u64, `rewards` u64 (`src/core/Stashes.cpp:406-407`).
3. `markers`: 32 forms (fixed), the last 128 bytes of the record (`src/Stashes.cpp:304-306`).

Loader limits: fewer than 100000 spots, at most 256 items per spot, ids must run 1..n in order, a `slot` past the
config's marker slots becomes -1; a record shorter than the marker tail is rejected (`src/core/Stashes.cpp:417-437`,
`src/Stashes.cpp:305`).

### `NEWS`: News page and live relations

`src/Save.cpp:163-174` / `:229-243`. Listed version: `NEWS` v1 (current; `src/Save.cpp:22`).

1. `lines[]`: str.
2. `factions` u32 (nf), then `relations` i32 x nf*nf, row-major (`src/Save.cpp:169-171`).

The matrix is used only when 31 <= nf <= 32 (the 208 table, plus the appended Dragon); a smaller one (another faction
order) or a bigger one is dropped and the static relations stay (`include/ALife/SaveFormat.h:53-64`, `src/Save.cpp:240`).

### `DRGN`: dragons

`src/Save.cpp:175-180` / `:217-226`. Listed version: `DRGN` v1 (current; `src/Save.cpp:24`).
`dragonLast` f64 (game hour of the last dragon), `dragonKills` u32 (`include/ALife/Board.h:134-135`).

### `DIFF`: difficulty sliders

`src/Save.cpp:181-188` / `:199-214`. Listed version: `DIFF` v1 (current; `src/Save.cpp:26`).
`followGame` u8 (0/1), slider count u8, then the sliders f32 in `DiffChannel` order: population, monsters, strength,
lethality, economy, mercy, tasks, dragons (`include/ALife/Difficulty.h:12`). Extra channels from a later build are
skipped and missing ones keep the ini's value; each slider is clamped on load (`src/Save.cpp:206-209`).

## 4. Record codes

`src/Save.cpp` names these 4-character codes: `ALIF` (the plugin id, not a record), `SIM2`, `NEWS`, `DRGN`, `DIFF`,
`TASK`, `COMP`, `WOND`, `STSH`. `TASK`, `COMP`, `WOND` and `STSH` are also defined as `kRec` in `src/Jobs.cpp:17`,
`src/Companions.cpp:16`, `src/Wounded.cpp:17`, `src/Stashes.cpp:15`. The unittest checks that every code in
`src/Save.cpp` appears here and that every `kRec` in `src/` is a type the decoder knows.

## 5. Changing a record

- Bump the record's version when its layout changes, keep the old reader (or say here that the old version is dropped),
  and add the new layout to `tools/cosave_decode.py` (`RECORDS`, `CURRENT`) and a sample to
  `tools/tests/test_cosave_decode.py` (`CASES`); the test fails until the doc, the decoder and the source agree on the
  current versions.
- Appending a faction needs no SIM2 bump (the goodwill table carries its length); inserting one does (see 208).
