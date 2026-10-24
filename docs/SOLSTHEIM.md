# Solstheim factions

Dragonborn's island gets three factions of its own (BOARD T11; reference: the Skaal, Redoran guard and Reaver patrols of
Immersive Patrols SE). Rieklings are a monster faction (docs/MUTANTS.md). Everything is vanilla and Dragonborn content.

| Faction | Who | Home | Gear and faces | Relations |
|---|---|---|---|---|
| `skaal` | Skaal villagers and hunters | Skaal Village, the Headwaters of Harstrad | `DLC2SkaalOutfit*`, Nords | friends of the Redoran, hunters and wanderers; wary of outsiders (base -120) |
| `redoran` | House Redoran's guard | Raven Rock, the Earth Stone, every other Solstheim place a side holds (`[ws_faction] solstheim = redoran`) | `DLC2RRRedoranGuardOutfit`, Dunmer | friends of the Legion, the Skaal, caravans and sellswords; lawful (they challenge armed strangers, enforce Raven Rock's bounty) |
| `reaver` | the island's raiders | Brodir Grove, White Ridge Barrow, Damphall Mine, Horker Island, Broken Tusk Mine, below Bloodskal Barrow, Haknir's Shoal | `DLC2BanditArmor*`, mostly Dunmer and Nords | enemies of every human faction; rivals (not targets) of Skyrim's bandits; toll before a fight |

The Reaver haunts are the places where Dragonborn.esm itself places actors from its bandit lists (`DLC2LCharBandit*`, vanilla
BanditFaction), the Skaal and Redoran homes where it places `DLC2SkaalVillageCitizenFaction` / `DLC2RavenRockGuardFaction` actors.
props.ltx names them as `[loc_dlc2_<local id>]`: Dragonborn.esm-local FormIDs, so the sections hold whatever the load order.

## Standing
- `skaal` flag: The Fate of the Skaal (`DLC2SV01`) completed: Skaal +900, Redoran +100.
- `ravenrock` flag: any of Raven Rock's quests (`DLC2RR01`/`02`/`03`) completed: Redoran +700, Skaal +100.
- Raven Rock's crime faction (`DLC2CrimeRavenRockFaction`) is the law of the Solstheim "hold" (DLC2SolstheimLocation carries
  LocTypeHold): gold owed there makes the Redoran treat the player as wanted, like the soldiers in Skyrim's holds.
- Race: Dunmer +150 with the Redoran, Nords +100 with the Skaal.

## Engine side
ALifeFactions.esp now has Dragonborn.esm as a master (every SE install has it) to tie the Skaal and the Redoran to their
vanilla factions, so ALife squads and the vanilla villagers and guards never fight. The Reavers are tied to BanditFaction
(Dragonborn's Reavers are vanilla bandits) and hostile to IsGuardFaction, which covers Raven Rock's guards.

Co-save SIM2 208 (and TASK 2) insert the three before the monster factions; 207 (M4b, 28 factions, the per-faction table read
by its length), 206 (21 entries), 205 (15 entries) and TASK 1 saves load with every faction index from 15 up shifted by 3
(`include/ALife/SaveFormat.h`, checked by `standing_test`).
M5g's live relation matrix (co-save NEWS) is only kept when it was written by 208 or later: a 31-faction matrix (before M4c's
dragons) fills the top-left and the dragon row / column keep factions.ltx's relations (`include/ALife/SaveFormat.h`
`RestoreRelations`); a matrix from before this change (another faction order) resets the live relations to the defaults from
factions.ltx on the first load.

## Checks
- Headless: `harness --solstheim` (world from `tools/gen_world_dlc.py`): each faction has squads on Solstheim and none in
  Tamriel, Reavers fight the Redoran or the Skaal offline, Skaal and Redoran never fight each other.
- Headless (T24): `harness --solstheim` also checks the race mix of the general factions' people on the island (Dunmer above 45%,
  Altmer + Bosmer under 6%) and that Tamriel keeps its mix; `tests/races_test.cpp` checks every faction's Tamriel draws are
  exactly the pre-T24 ones.

## Races (T24)
New members draw their race from `config/races.ltx` for the squad's home worldspace at creation (`Config::RollRace`, called
through `Host::MakeMemberIn`). `[races]` holds the Tamriel tables, which are exactly the plugin's tables before T24 (same weights,
same order, so the same seed gives the same races). `[races.DLC2SolstheimWorld]` overrides them for the general factions living
on Solstheim: Dunmer first (50-60%), then Nords, Imperials, a few Bretons, Redguards and Orcs, and Altmer, Bosmer, Argonians and
Khajiit rare (1-2% each). Vampires there are mostly Dunmer vampires. The Skaal, the Redoran guard, the Reavers and the caravans
keep their own tables. Existing members never change race, so old saves load unchanged.

Pool [I] (`tools/esp/build_esp.py` COUNTS / VAMPIRES): 6 Dunmer actors per sex out of 166, plus 2 per sex with the Dunmer
vampire race. A squad on screen whose members find no free actor of their race can switch an anonymous member's race
(`Online.cpp`, `Pool::Assign` fallback). Raven Rock with Redoran guards (all Dunmer) and Dunmer-heavy general squads on screen
together can use up the 6 male Dunmer, so more members will fall back there. More Dunmer pool actors would need new NPC_ records
in ALife.esp: that is the coordinator's call.
