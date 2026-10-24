# A-Life for Skyrim: changes

"Checked in game" means a background lab run in the real game passed; "headless" means the engine-free tests passed and the
code builds, but the in-game check is still open.

## 2.1.0 (release candidate, October 2026)

Saves: the co-save is version 208. Saves from 2.0.x (co-save 205, 206 and 207) load; newer records (tasks, companions, wounded,
difficulty) are separate and simply start empty on an older save.

### The world
- **Civil War follows the vanilla quests** (T10): Imperial and Stormcloak squads follow hold ownership, a hold's guard squads
  change side with the hold, the losing side stands down when the war ends, Dawnguard squads wait for Fort Dawnguard's rebuild.
  Checked in game.
- **Solstheim factions** (T11): the Skaal, House Redoran's guard and the Reavers. Checked in game.
- **More monsters** (M4b): death hounds, werewolves and werebears, rieklings (and riders), ghosts and wispmothers, hagravens,
  giants with mammoths, Dwemer automatons, horkers, chaurus, necromancer-led zombie bands, ash spawn. Checked in game (the
  Solstheim kinds, horkers and chaurus headless only).
- **Patrol mods** (T12): Immersive Patrols and Skyrim at War are detected; A-Life shrinks its on-screen budget while they run and
  leaves the forts they garrison to them. MCM: "On screen with patrol mods". Headless; the in-game check needs both mods.
- **Living news and lair hunters** (M5g): SOS calls, battle and death reports and gossip, kept on the MCM News page; dynamic faction
  relations (off by default, as in Anomaly); hunter squads go after occupied lairs and sell the parts. Checked in game.
- **Camp life** (M5h): squads staying at a place take jobs (watch, patrol, a sniper on high ground, an ambusher on the approach,
  sitting at the fire, sleeping at night) and campfires with company get stories. Headless; in-game check pending.

### You and the squads
- **Tasks from squad leaders** (M5a): bounty, assault, delivery and fetch, with journal entries, markers and rewards. Checked in game.
- **More tasks** (M5e): escort a caravan, defend a place, rescue a captive, recover a lost item, and the hunt for the ten most
  dangerous people in Skyrim with rankings. Headless; in-game check pending.
- **Companions** (M5b): friendly squads join for free, neutral ones for pay by the day; commands, gear, dismissal, save/load.
  Checked in game.
- **Wounded, help, finish, surrender** (M5d): beaten people go down instead of dying, call for help, are patched up by squadmates or
  finished by enemies; losing squads may surrender to you; offline fights leave wounded survivors. Headless; in-game check pending.

### Difficulty (T21)
- A-Life now follows Skyrim's difficulty (Novice .. Legendary) and has its own sliders on a new MCM **Difficulty** page:
  population, monsters, squad strength (size, level, gear), offline combat lethality, economy (rewards up, tolls and companion pay
  down), wounded and surrender, task difficulty, dragons. Adept with all sliders at 1.0 plays exactly as before. The sliders are
  saved with each save. Checked in game at Novice and Legendary.

### Fixes and stability
- 120 simulated days stay within the CPU budget, also at Legendary with every slider at its maximum.
- Background test runs never take the mouse, keyboard or window focus (test builds only).
- A wider online radius (Anomaly's ~250 m) with hysteresis, so squads near the player show up without popping in and out.
- Fixes from the play-tests: a crash when a just-placed pack went offline, the Dawnguard gate of the Civil War quests, companion
  and Solstheim edge cases.

### Not in 2.1.0
- Dragons as A-Life actors (M4c) and stashes (M5f): in development. The Dragons slider already exists and has no effect until
  M4c ships.
- Emissions (Anomaly's blowouts): not planned for Skyrim.

## 2.0.0 (October 2026)
The Anomaly edition: 15 factions with squads across all of Skyrim and Solstheim, monsters with lairs, standing instead of a hostile
switch, every encounter a conversation, needs, trade, looting and the world levelling with you.
