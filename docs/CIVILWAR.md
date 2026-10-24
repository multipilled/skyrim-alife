# Civil War progress and faction gates (T10)

ALife's Imperial and Stormcloak squads follow the vanilla Civil War, after Immersive Patrols SE (Nexus SSE 718): hold places
take the side that holds the hold, the hold's guard squads change side with it, the losing side stands down when the war
ends, and Dawnguard squads only exist once Fort Dawnguard's rebuild has begun.

Nothing new goes into the co-save: the plugin reads the game's own state at every load and on change (polled every few
seconds), and the board re-derives everything from it. Older saves load unchanged; the first sync after the load re-aims
their places and swaps or retires squads that no longer fit.

## Vanilla state read (verified from Skyrim.esm / Dawnguard.esm with tools/esp.py and the vanilla Papyrus sources, Oct 9 2026)

| What | Record | FormID | How it is read |
|---|---|---|---|
| Hold owner | KYWD `CWOwner` | Skyrim.esm 0x02A456 | keyword data on the hold's BGSLocation (`BGSLocation::keywordData`, the array Papyrus `Location.GetKeywordData` reads): 1 = Imperials, 2 = Sons; missing / other = not set yet (use the config start owner) |
| Holds | LCTN `ReachHoldLocation` .. `RiftHoldLocation` | 0x016769 Reach, 0x01676A Eastmarch, 0x01676B Winterhold, 0x01676C Rift, 0x01676D Pale, 0x01676E Hjaalmarch, 0x01676F Falkreath, 0x016770 Haafingar, 0x016772 Whiterun | the keys of factions.ltx `[hold_faction]` |
| War over | QUST `CWObj` | Skyrim.esm 0x029940, stages 1, 255 | `CWFinale` stage fragment ("COMPLETE THE CIVIL WAR!!!") runs `CWObj.SetStage(255)`; read `GetCurrentStageID() >= 255` or `IsCompleted()` |
| Winner | the capitals' holds | Haafingar 0x016770, Eastmarch 0x01676A | the same fragment sets Solitude + Haafingar to 2 (Sons won) or Windhelm + Eastmarch to 1 (Imperials won) |
| (context) main quest | QUST `CW` | 0x019E53, stages 0..256 | `CWScript.OnInit` writes the start owners (Haafingar, Reach, Hjaalmarch, Whiterun, Falkreath = 1; Pale, Winterhold, Eastmarch, Rift = 2), same as `[hold_faction]` |
| (context) finale | QUST `CWFinale` | 0x0D1444 | Battle for Solitude / Windhelm; also `CWSiege` 0x0954E1, `CWSiegeObj` 0x096E71, `CWFin` 0x065C8D (post-war dialogue) |
| (context) allegiance | GLOB `CWPlayerAllegiance` | 0x023A43 | 1 Imperials, 2 Sons (standing already reads the player's factions) |
| Dawnguard gate: A New Order | QUST `DLC1VQ03Hunter` | Dawnguard.esm 0x0098CB, stage 5 | Isran starts rebuilding the order (set at the end of `DLC1HunterBaseIntro` 0x004E24) |
| (not a gate) Fort Dawnguard rebuild | QUST `DLC1HunterBaseEnableSet1` | Dawnguard.esm 0x0198B2, stages 0 (start-up), 200 (shut-down) | stage 0 swaps the ruined smithy, enchanting room, troll pen and barracks for the restored ones, dresses Sorine and Gunmar, sets 200, which stops the quest: it reads stage 0 again, so it cannot serve as a gate (W1 lab, Oct 18) |

Hold owners change through `CWScript.SetOwner` / `WinHoldAndSetOwnerKeywordDataOnly` (campaigns, fort sieges, the city
sieges, and holds traded at the Season Unending peace council), all of which write the same `CWOwner` keyword data.

## Rules (core: `src/core/CivilWar.cpp`, board state not saved)
- **Hold places.** A place whose preset says `default_faction = hold` and whose hold is in `[hold_faction]` belongs to the
  live owner: its `owner` priority bonus and `faction = owner` respawn entries move to the new side, and its controller is
  set to the new side when it was held by either Civil War side (a bandit-held fort stays the bandits'). Squads of the other
  side then see it as enemy-held (Anomaly's base / territory raid windows). Places outside the nine holds (Solstheim) keep
  their `[ws_faction]` side.
- **Guards.** Squads of a Civil War side that came from a guard entry (`[civil_war] guard_entries`: the hold garrisons and
  patrols) at a hold place whose side is now the other one are replaced, when off screen, by a squad of the new side from the
  same entry, at the same spot and doing the same thing. On screen they finish what they are doing first.
- **War over.** Every hold counts as the winner's. The loser's squads stand down in the worldspaces listed in
  `stand_down` (Tamriel; Raven Rock's East Empire Company garrison on Solstheim is not part of the war): no new squads, and
  existing ones are retired when off screen (squads on screen walk on until the player leaves them).
- **Gates.** `[faction_gates]` lists quest stages (`plugin|local id:stage`, open at that stage or later) or, since M4c, global
  values (`plugin|local id=value`, open when the global is at least the value: `dragon = Skyrim.esm|042851=1`); any one opens
  the gate. A gated faction
  has no squads until a gate opens: no respawn, and existing squads retire off screen. Dawnguard: A New Order
  (`DLC1VQ03Hunter` 5, or that quest finished). Without Dawnguard.esm the gate stays shut.
- News lines when a hold changes side, when the war ends and when a gate opens.

## Lab check (bridge, never OS input)
`cw` dumps hold owners, the war state and the gates; console lines through `SKSE/ALife.cmd`:
`setstage 29940 255` ends the war (after flipping Haafingar); the hold owner is flipped through the CW quest script
(`cw flip <hold hex> <1|2>` runs `CWScript.SetOwnerKeywordDataOnly` through Papyrus); the Dawnguard gate opens with
`setstage <xx>0098CB 5`, where `<xx>` is Dawnguard.esm's load-order prefix (`help DLC1VQ03Hunter 4`).
