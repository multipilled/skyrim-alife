# A-Life for Skyrim 2.0 (Anomaly edition)

A persistent, simulated world for Skyrim, built on the ideas of STALKER Anomaly's A-Life: groups of people live, travel,
trade, fight, loot, grow and die all over the map whether you are watching or not, and meet you on the road with the
weight of a world that was already going on. It is not a combat mod. Everything that touches you is an encounter with
somebody who has a reason to be there and an opinion of you.

## What it does

- **A living map.** Every placed location of Skyrim (vanilla, the Dawnguard and Anniversary content, and anything a mod adds)
  becomes a *smart terrain*: a fort, camp, cave, ruin, farm, inn, mine, shrine, town or city. About 350 of them in Skyrim itself and 45 on Solstheim (Dragonborn). Each worldspace is its own map: squads never cross between them.
- **Squads with lives.** Fifteen factions (bandits, Forsworn, the Legion, the Stormcloaks, the Thalmor, wanderers, hunters,
  mercenaries, mages, necromancers, vampires, the Dawnguard, the Silver Hand, the Vigilants of Stendarr and Khajiit caravans)
  keep squads that pick targets, walk there, stay, work, rest, fight each other off screen, win or lose territory, and
  respawn from the places they hold. Forts change hands. Camps are cleared and retaken. The news feed (MCM > A-Life > News)
  tells you what happened.
- **People with needs and a pack.** They get hungry, tired, hurt; they eat what they carry, drink potions, heal, hunt, fish, rest,
  trade at the shops of the towns they pass (the vendors really lose and gain stock), raid settlements and clear dungeons
  when they are strong enough.
- **They level with you.** A squad is made at your level plus an offset (novices trail, veterans lead). When you level up,
  the ones that fell behind catch up a step at a time: more health and stamina, better skills, perks, a better kit. Nobody is
  ever scaled down; whoever grew by fighting stays as grown.
- **They loot.** Every squad strips the dead it passes: its own fallen, its enemies, and the bodies you leave behind (after a
  grace period, so you get the first look). Quest items are never taken.
- **Standing, not a hostile switch.** Nothing starts hostile by decree. Every squad works out an attitude from Skyrim's own
  reasons: who you swore to in the Civil War, your race, the guilds you belong to, what you wear, what you owe the law in this
  hold, what you have done to its people, and how dangerous you look. Bandits weigh the odds before they act. Soldiers
  challenge armed strangers they distrust. Forsworn defend the Reach. See `docs/STANDING.md`.
- **Every interaction is a conversation.** Squads that have something to say walk up to you (the courier's way) and the
  ordinary dialogue menu opens: a bandit names a toll (pay, plead poverty, refuse, fight); a Legion patrol challenges you
  (explain yourself, or defy them); anyone friendly will pass on news that really happened, or open their pack for trade.
  No pop-ups, no hotkeys. See `docs/CONVERSATIONS.md`.
- **Real trade.** "Let me see what you have for sale" opens the barter menu over what that person actually carries and what
  gold they have. Khajiit caravans roam the roads with a real stock (skooma, moon sugar, potions, poisons, ingredients, scrolls,
  gems) and sell on to the shops of the towns they visit.

## Requirements

- Skyrim Special Edition / Anniversary Edition 1.6.x or 1.7.x (built for and checked against 1.7.104)
- SKSE64 matching your game version, and the Address Library for SKSE Plugins
- SkyUI 5.2 SE for the MCM (`SKSE/Plugins/ALife.ini` is an emergency fallback)
- The official DLCs (Dawnguard is a master). Hearthfire and Dragonborn are optional (Solstheim is simulated when Dragonborn is installed); Creation Club content is used when present.

## Install

Install the archive with your mod manager (Vortex or MO2). Everything in the archive root goes to `Data`.
Load order: after the master files; the three plugins can sit anywhere after your overhauls.
- `ALife.esp` (ESL): the 166 pool actors, packages and markers. Masters: `Skyrim.esm`, `Update.esm`, `Dawnguard.esm`.
- `ALifeFactions.esp` (ESL): one hidden faction per squad faction and their relations.
- `ALifeTalk.esp` (ESL): the dialogue. It adds records to the vanilla *Generic dialogue* quest, every one conditioned on
  A-Life's own globals and factions, so no other NPC is affected.
- `SKSE/Plugins/ALife.dll`, `ALife.ini` and `SKSE/Plugins/ALife/*.ltx` (all the tuning: squads, places, relations, standing).
- `Scripts/ALife_MCM.pex`, `Source/Scripts/ALife_MCM.psc` and FaceGen for the pool actors.

## MCM (SkyUI > A-Life)

**A-Life**: enable, population density, offline combat, news; **Encounters**: how often squads stop you to talk (never / rarely / normally / often) and whether they loot bodies; prepare to uninstall. **Standing**: how each faction regards you and why.
**News**: the world's latest events, newest first.

## Uninstall

MCM > A-Life > Prepare to uninstall > Yes. Save, quit, then remove the mod.
(Without SkyUI: set `PrepareUninstall=1` in `ALife.ini`, load the save, wait for the notice, save, remove.)

## Tuning

`SKSE/Plugins/ALife/*.ltx` use Anomaly's own file dialect; comments in each file say what a setting does.
`alife.ltx` (simulation, needs, loot, levelling), `squads.ltx`, `props.ltx` (places and respawns), `factions.ltx` (relations),
`standing.ltx` (attitude rules), `talk.ltx` (generated: do not edit).

## Test switch

`[Debug] bDebug=1` in `ALife.ini` writes `Documents/My Games/Skyrim Special Edition/SKSE/ALife.log` and enables test hotkeys.
With `bDebug=0` (default) A-Life writes no files and the hotkeys do nothing.

## Credits

Design after STALKER Anomaly's A-Life (Anomaly 1.5.3 scripts, read from the user's own install: nothing from Anomaly is shipped).
Built on CommonLibSSE-NG and SKSE64. Written with AI assistance (Claude); verified in the real game.
