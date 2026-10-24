# Stashes (M5f)

Anomaly's caches: half the boxes in the world hold something nobody has marked, a note on a dead stalker reveals one,
and a job can pay with one. ALife does the same across Skyrim with containers it places itself near the places of its
graph, a stash note (MISC) carried by squad members, and job rewards. Markers go through a quest of their own,
ALifeStashes. Evidence: [V] seen in the files or by running, [I] read from code or data, [U] not checked.

## 1. Anomaly behaviour (1.5.3, local only)
Targeted greps of `treasure_manager.script`, `ui_pda_npc_tab.script`, `xr_effects.script` and
`configs/items/settings/treasure_manager.ltx`:
- [I] New game (`on_game_load`, :626-659): every inventory box is a cache (`false` = available). **Half** of them get
  random content with **no map spot** (`create_random_stash(true, "stash")`). Content is spawned on the box's first use
  (`try_spawn_treasure`, :512-586).
- [I] Fill (`set_random_stash`, :360-509): a weight budget `random(500, 800)`. It loops over shuffled kinds, then shuffled tiers
  1-5. An item goes in when `random(1,100) < chance(kind) * chance_multi(tier) + inc`, and each pick costs
  `weight(kind) * weight_multi(tier) + inc * 5`. A round with no pick raises `inc` by 10; a pick lowers it by 10. The tier
  multipliers are mostly chance 1 / 0.8 / 0.75 / 0.6 / 0.5 and impact 1 / 1.25 / 1.5 / 2 / 3. Anomaly has no level scaling.
- [I] Reveal (`get_random_stash`, :267-357): a random available (empty) box, optionally only on the same level
  (`spawn_local`), gets content, a map spot and the news "stash found".
- [I] Corpse PDAs (`ui_pda_npc_tab.script`:107-113, :695-699): a stash message with chance `stash_chance` x a PDA factor
  (economy presets 0.4 / 0.3 / 0.2). The stash is marked when the message is read.
- [I] Task rewards (`xr_effects.reward_stash`, :1377-1396): 50% chance of a stash on top of the money, then a 50% chance of
  one bonus item. `spawn_local = true`.
- [I] Looting (`actor_on_item_take_from_box`, :662-680): the first item taken empties the box's state, removes the spot
  and counts `stashes_found`. **No timed refill**: a looted box gets new content only when a later reveal picks it.

## 2. ALife design

### Spots (core `Stashes::Build`)
- Every place of the world graph whose kind is listed in `[stashes] kinds` (camps, forts, dungeons, crypts, ruins, mines,
  shipwrecks, wilderness, dens, mills; not cities, towns, inns or farms) gets `spots_per_place` spots. Each sits
  `offset` (400-1200) units from the place in a direction drawn from the save's `worldSeed`, so a new game builds the
  same spots. The total is capped at `max_spots`, and spots are saved with their positions: later graph changes do not
  move them.
- `hidden_share` (0.5, Anomaly) of them start **filled and unmarked**. Their content is rolled on first placement (as
  Anomaly's lazy spawn) and they are found only by walking by. The rest start **empty**, as the pool that notes and
  rewards draw from.
- The container base per place kind (`[containers]`, all [V] in Skyrim.esm via `tools/esp/fastscan.py`, DATA flags 0):
  - `0F8478` TreasChestSmallEMPTYNoRespawn (no CNTO), the default;
  - `0F8477` TreasRuinsUrnLargeEmptyNoRespawn (no CNTO): crypts, dungeons, Dwarven ruins, dragon priest lairs;
  - `09424C` StrongBox (no CNTO): forts, military camps, shipwrecks;
  - `10E886` MiscSackLarge_NoRespawn (1 leveled CNTO, so the plugin empties it after placing): camps, dens, wilderness.

### Content (core `Stashes::Roll`)
Anomaly's kind / tier loop with a **gold-value** budget instead of a weight: each pick costs its value x count, so a job
reward can promise "at least the gold it replaces".
- Budget: `budget` (250-500 gold) x (1 + `per_level` x player level), x **Diff(Economy)** (T21: stash value x M).
- The multiplier only scales the budget: at 1.0 the budget, the draws and so the content are exactly as without T21
  (no extra random draws).
- Tiers 1-5 open at player level 1 / 8 / 16 / 26 / 36 (`tier_levels`), with chance x 1 / 0.8 / 0.75 / 0.6 / 0.5 and
  count x 1 / 1.25 / 1.5 / 2 / 3 (Anomaly's multipliers).
- What's left of the budget when the loop stops goes in as gold coins (`Gold001`), so a stash is always worth its budget.
- Kinds (`[kind.<name>]`: `chance` in percent, `count`, `t1`..`t5` = `formid:value` lists). Every FormID and value below
  is [V] in Skyrim.esm (`scan_loot.py` over EDID / DATA / ENIT, Oct 18):
  - potions: health 3EADD/3EADE/3EADF/3EAE3/39BE4, magicka 3EAE0/3EAE1/3EAE2/3EAE4/39BE6, stamina 3EAE5/39BE8/3EAE7/3EAE8/3EAE6;
  - arrows: iron 1397D, steel 1397F, orcish 139BB, dwarven 139BC, elven 139BD, glass 139BE, ebony 139BF, daedric 139C0 (1-8 gold);
  - gems: garnet 63B45, amethyst 63B46, ruby 63B42, sapphire 63B44, emerald 63B43, diamond 63B47, and the flawless ones
    68521 / 6851E / 68523 / 68520 / 6851F;
  - soul gems: petty..black, empty and filled, 2E4E2-2E504;
  - metal: iron ore 71CF3, ingots iron 5ACE4, steel 5ACE5, corundum 5AD93, orichalcum 5AD99, quicksilver 5ADA0,
    moonstone 5AD9F, Dwarven DB8A2, malachite 5ADA1, gold 5AD9E, ebony 5AD9D, ebony ore 5ACDC, silver ore 5ACDF;
  - scrolls: fury 965A1, courage 965A0, firebolt 96598, ice spike 96599, flame atronach 965A5, fireball A44AE,
    ice storm A44AF, chain lightning A44B0, heal other A44C1;
  - lockpicks 0000A; gold 0000F.

### Notes on the dead (core `Stashes::OnKill`, `OnNoteRead`)
- A member of a human faction who dies carries a stash note with `[notes] chance` (0.15). Notes are not for
  companions or monsters.
- Offline (`Host::OnMemberKilled`): the note passes to a living member of the killer squad (people only), so it is on
  that member's body when the player kills them later. A member carries at most one. Beasts and an empty killer squad
  lose it.
- On screen (`Sim::OnActorKilled`): the note is put on the body.
- Reading: the player picking a note up (`TESContainerChangedEvent`, new container = the player) reveals a stash within
  `reveal_radius` of where it was taken. The note leaves the player's pack (no double reveal) and a HUD notification says
  where ("A stash near <place> is marked on your map", `src/Stashes.cpp` `Notify`; no News page entry). If no empty spot is in range, a hidden filled one there is marked instead; if there is none at all, the nearest
  empty spot anywhere in the worldspace is used.

### Job rewards (core `Stashes::TaskReward`, plugin `Jobs`)
- On a job done, with `[reward] chance` (0.5, Anomaly), the job pays with a stash revealed near the giver.
- `replace_gold = true` (the brief): the gold is replaced, and the stash's budget is at least the gold x
  `budget_per_gold` (1.0) x Diff(Economy).
- `replace_gold = false` is Anomaly's "on top of the money".

### Markers (ALifeStashes)
- A misc quest **ALifeStashes** in `ALifeTalk.esp` (`tools/build_talk_esp.py`) has `marker_slots` (8) objectives
  (index i) and optional aliases (alias i). It is driven through the existing generic `ALife_Tasks.psc` (`Begin` /
  `Show` / `Mark`) with the stash's place name as the objective text, as M5a does.
- Each alias aims at a persistent XMarker the plugin moves onto the spot (the M5a path), then onto the container once
  it is placed.
- A ninth marked stash takes the oldest marker: that stash stays filled, unmarked.
- Local IDs: 0x81E (note MISC, `Clutter\Books\Note01.nif`, [V] used by 92 vanilla note books) and 0x81F (quest). Both are
  clear of 0x80D-0x810 (M5e satchel and reserve, M5d `ALifeWoundHeal`); `tools/check_next_id.py` and a duplicate scan
  run on the built ESP.

### Placement (plugin `Stashes.cpp`)
- [I] Containers are placed at runtime (PlaceObjectAtMe, persistent), not pre-placed in an ESP. Pre-placed exterior
  refs would need overrides of vanilla CELL / WRLD records, and the user's rule is no plugin edits of vanilla records
  (BOARD Oct 9).
- A spot gets its container when the player comes within `place_radius` (5000) and the spot's ground is loaded, on a
  navmesh point near the spot (`Online::SnapPoint`). The ref's FormID goes into the co-save. A container that vanished
  (cell reset, another mod) is placed again with what the core still holds.
- The rolled items go into the container when it is placed. After that the container's own inventory is the truth
  (saved by the game); the core clears its copy and keeps only the state.
- Looting: the first item the player takes from a stash container (`TESContainerChangedEvent`, old container = the ref)
  marks it **looted**: the marker goes, the spot is empty again (Anomaly: back in the pool) and `stashes_found` counts
  up. **No timed refill.**

### Co-save `STSH` v1
Written after `WOND`: the spots (node, worldspace, position, container base, state, rolled, marked slot, ref FormID raw,
items while not placed, the time marked) and the counters. A save without `STSH` (every save before M5f) builds the spots
afresh on load, so old saves load with the hidden half filled. An unknown version is skipped (built afresh too). Ref
FormIDs are resolved with `ResolveFormID` after a load; a spot whose ref no longer resolves is placed again. SIM2 stays
at 208.

## 3. Code
- `include/ALife/Stashes.h`, `src/core/Stashes.cpp`: the engine-free model (config, spots, roll, reveal, notes, job
  reward, looting, markers, co-save blob). Tested headless in `tests/stashes_test.cpp` (`tests/build_stashes_test.bat`),
  plus harness `--m5f` (notes carried and read across a month of offline fights, marker slots, no growth).
- `include/ALife/StashesPlugin.h`, `src/Stashes.cpp`: the plugin side (placement, filling, markers, the note on the
  dead, reading, looting, the job hook, co-save `STSH`, the `stash` debug verbs).
- `config/stashes.ltx`: spots, notes, rewards, the loot table and the container bases.
- `tools/build_talk_esp.py`: the note MISC 0x81E, the ALifeStashes quest 0x81F, and the `[stashes]` keys in talk.ltx.

Debug (bDebug, `SKSE/ALife.cmd` or lab queue): `stash list`, `stash reveal` (a stash near the player is marked),
`stash note` (a note goes into the player's pack, as if taken from a body), `stash place` (place every spot in range now),
`stash reward` (a job reward stash near the player).

## 4. Difficulty (T21)
- `Diff(Economy)` scales the stash budget, and the job-reward budget through the gold it replaces.
- The note and reward chances are design values, not scaled: a richer or poorer economy changes what a stash holds,
  not how often you find one.
- Every balance key is tagged in `stashes.ltx` (`{economy+}`).
