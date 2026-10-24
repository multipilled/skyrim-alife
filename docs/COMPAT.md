# Sharing Skyrim with patrol mods (COMPAT)

ALife runs its own world: about 300 offline squads, at most 3 groups / 15 actors on screen, vanilla locations as smart
terrains. Two popular mods also fill the roads and forts with soldiers:

| Mod | Nexus | What it adds | What collides with ALife |
|---|---|---|---|
| Immersive Patrols SE (Scrabbulor) | SSE 718 | ESP-only placed patrols of most factions (Legion, Stormcloaks, Thalmor, Vigilants, Dawnguard, vampires, bandits ...) on scheduled routes, CW-gated, no scripts. Three sizes of main file. | Extra actors on the roads: the on-screen budget is shared. Its patrols use vanilla factions, so ALife's Legion and Stormcloak squads ignore its soldiers. |
| Skyrim at War Reborn | SSE 79932 | 200 to 2,000 Civil War soldiers (four sizes), 200+ road patrols, and some CW forts and road towers held by Legion / Stormcloak garrisons instead of bandits and necromancers (they spawn once the fort is cleared or the war is won). | Double garrisons and permanent fights at those forts (ALife's own fort garrison respawns, bandit squads retaking them), and a much bigger actor count. |

Immersive Patrols needs Skyrim 1.6.1130+ (or an index-incrementing mod); the lab runs 1.7.104, so it loads there. It
disables the losing side's patrols when the Civil War ends; ALife's own CW squads stand down the same way (T10), so after
the war neither mod keeps the loser on the roads. A third mod, Nexus SSE 92995 (faction framework with commanders and
location capture), is listed in compat.ltx (`[compat_faction_commanders]`) without plugin names (not verified yet) and is not
in `[compat] mods`, so `LoadCompat` does not read it yet: once its filename is known it gets the same budget as Skyrim at War. Its captures happen at runtime, so handing over what it captures needs its state read at
runtime (a follow-up, not done here).

This file is the design; `config/compat.ltx` holds the data, `include/ALife/Compat.h` / `src/core/Compat.cpp` the logic,
`tests/compat_test.cpp` the headless checks.

## 1. Detection (data load, by plugin filename)

`config/compat.ltx` lists each mod as a `[compat_<id>]` section with `plugins = name, name ...`. Names are case-insensitive
and may use `*` as a wildcard (`Skyrim at War*.esp`), because each size of a mod ships its own plugin name. At kDataLoaded
the plugin asks `TESDataHandler::LookupModByName` for every loaded file (`files[]` + compiled ESL list) and a mod counts as
detected when any of its patterns matches a loaded plugin. Detection never makes either mod a master: nothing is looked up
by FormID in their plugins. The result goes to ALife.log as one `[compat]` line per detected mod.

The exact plugin filenames were not checked against the downloads yet (Nexus pages could not be read and nothing was
downloaded), so the patterns are deliberately broad. A wrong pattern is a one-line fix in compat.ltx; `[compat] force = id`
switches a mod on for a lab without its plugin.

## 2. Hand-over: places the other mod holds

Per mod, `handover_kinds = fort, military_camp` (location kinds) and/or `handover = <location FormIDs>` choose the
smarts it garrisons; `keep = <FormIDs>` exempts places it leaves alone (Driftshade Refuge stays the Silver Hand's). A
handed-over smart:

- has **no ALife respawn** (no garrison, patrol or bandit entries, so nothing is stocked there at game start either);
- is **owned by the hold's side** (`handover_owner = hold`) and is no longer `faction_controlled`: ALife squads can't take
  it, so control never flips;
- may only be **targeted by the owner and its allies** (`all`, `monster`, `lair` and every other faction's priority
  are zeroed): no ALife bandit, Forsworn, necromancer or monster squad picks it as a target, so nobody camps there or
  fights the other mod's garrison;
- takes at most `handover_population` (default 1) ALife squads, a friendly visit rather than a second garrison.

Why every fort for Skyrim at War: its page names "some CW forts and road towers" without a list. Handing over all
fort-kind smarts costs ALife a few Legion/Stormcloak garrisons (the other mod's soldiers are there instead) and guarantees
no double garrison. A precise list can replace `handover_kinds` once someone reads the plugin in xEdit.

Immersive Patrols places patrols, not garrisons, so it hands over nothing; it only touches the budget.

## 3. Faction mapping

`[compat_factions]` holds `<vanilla faction FormID> = <ALife faction>` lines (Skyrim.esm ids, one shared section, applied
whenever any mod is detected; `src/core/Compat.cpp` `LoadCompat`). At data load the plugin adds faction reactions between the ALife faction records
(ALifeFactions.esp) and the vanilla faction, copied from the ALife relation matrix: if `imperial` is the map target of
`CWImperialFaction`, every ALife faction allied to `imperial` gets an ally reaction to CWImperialFaction and every
enemy an enemy reaction, both ways. Result: ALife Stormcloak squads fight the other mod's Legion soldiers, ALife
Legion squads walk beside them, ALife bandits fight both. ALifeFactions.esp does not tie `imperial`/`stormcloak` to the
vanilla CW factions on its own; the mapping only exists while a CW patrol mod is loaded.
The reactions are runtime-only (not written to the save) and rebuilt every data load. Only the CW pair is mapped by
default: the other vanilla factions those mods use (Thalmor, bandits, Vigilants, Dawnguard, vampires) are already tied
to their ALife analogues in ALifeFactions.esp.

## 4. Actor budget

Each detected mod declares how much of the screen it takes:

| key | meaning | IP SE | SaW Reborn |
|---|---|---|---|
| `online_groups` | ALife groups on screen at once (max 3) | 2 | 2 |
| `online_actors` | ALife actors on screen at once (max 15) | 12 | 9 |
| `population` | multiplies the MCM population density (respawn quotas, start fill) | 0.85 | 0.6 |
| `patrol_scale` | multiplies the `patrol` and `owner_patrol` respawn entries (road traffic) | 0.6 | 0.5 |

With several mods detected the lowest caps and the product of the multipliers apply (floors: 1 group, 5 actors,
population 0.3). MCM A-Life page, World, "On screen with patrol mods": **Auto** (the table above), **Off** (ALife's full
3 / 15 and normal population, handover stays), **1 squad** / **2 squads** / **3 squads** (that many groups, 5 actors each). INI: `iCompatBudget` = -1 auto,
0 off, 1-3 groups. `bCompatHandover = 0` (INI only) turns the hand-over off.

Companions (docs/COMPANIONS.md) count against `online_groups` and outrank world squads. A companion squad that needs a slot
makes a world squad leave the screen when the budget is full, and so does a bounty target brought in with the `summon` debug
verb. When the player's companions fill a small budget (one companion squad under **1 squad**, two under **2 squads**), no
world squad comes on screen at all. That is intended; ALife.log says so once each time it starts
(`[online] companions fill the on-screen budget`). Under a budget of 1, only one companion squad can be on screen: make-room
moves world squads, never another companion, so a second companion squad (`max_squads = 2`) waits off screen.

## 5. Done-check (lab, M7)

Both mods loaded in the test lab (a separate MO2 instance), one guarded run: `[compat]` lines in ALife.log name both mods; no ALife
bandit / Forsworn squad is assigned to a handed-over fort (status dump); online groups / actors never exceed the budget
(`[hour]` lines); frame time no worse than the run without ALife's own garrisons; clean log; 0 watchdog strikes.
