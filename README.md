# A-Life for Skyrim SE (Anomaly edition)

A persistent, simulated world for Skyrim Special Edition, after STALKER Anomaly's A-Life. Factions keep squads that
live, travel, trade, fight, loot, level up and die all over the map whether you are watching or not, and meet you on
the road with a reason to be there and an opinion of you. An SKSE plugin; no scripts run on a timer.

![A bandit squad names a toll in ordinary Skyrim dialogue](docs/images/toll-dialogue.jpg)

| | |
|---|---|
| ![A Khajiit caravan on the road](docs/images/khajiit-caravan.jpg) | ![Bartering over what the caravan really carries](docs/images/caravan-barter.jpg) |

## Features

- **A living map.** Every location in Skyrim and Solstheim (vanilla, DLC, Creation Club and modded) becomes a *smart
  terrain*: about 350 in Skyrim and 45 on Solstheim. Forts change hands, camps are cleared and retaken.
- **15 factions with squads**: bandits, Forsworn, Legion, Stormcloaks, Thalmor, wanderers, hunters, mercenaries, mages,
  necromancers, vampires, Dawnguard, Silver Hand, Vigilants and Khajiit caravans, plus wildlife, draugr, falmer,
  giants, hagravens, werewolves and other monsters with lairs. They pick targets, travel, rest, fight off screen and
  respawn from the places they hold. The news feed tells you what happened.
- **Needs and a pack**: squads eat, heal, hunt, fish, rest, trade at real shops (vendors really gain and lose stock),
  raid settlements and clear dungeons.
- **They level with you** and are never scaled down. **They loot** the dead they pass, yours included (after a grace period).
- **Standing, not a hostile switch**: each squad works out its attitude from the Civil War, your race, guilds, clothes,
  bounty, what you did to its people and how dangerous you look.
- **Every encounter is a conversation** in the ordinary dialogue menu: tolls, patrol challenges, news, trade.
- **Tasks** from squad leaders (bounty, assault, delivery, fetch) with journal entries, markers and rewards.
- **Stable over long play**: 120 simulated days, repeated save/load cycles, a CPU budget.

Full player documentation: [docs/README.md](docs/README.md). Design notes: [docs/](docs/).

## Requirements

- Skyrim Special Edition / Anniversary Edition 1.6.x or 1.7.x (built and tested on 1.7.104). **You need your own copy
  of the game: no game files are in this repository.**
- SKSE64 for your game version and the Address Library for SKSE Plugins.
- SkyUI 5.2 SE for the MCM.
- The official DLCs (Dawnguard is a master; Dragonborn adds Solstheim).

## Install

Download the release archive from the [Releases](../../releases) page.

**Mod Organizer 2**: *Install a new mod from an archive*, pick the zip, enable it in the left pane, and check the three
plugins (`ALife.esp`, `ALifeFactions.esp`, `ALifeTalk.esp`) in the right pane. Launch SKSE from MO2.

**Vortex**: drag the zip onto the *Mods* page (or *Install From File*), click *Enable*, then *Deploy*. Launch through SKSE.

Load order: anywhere after the master files. Configure it in MCM > A-Life. To uninstall: MCM > A-Life > Prepare to
uninstall, save, quit, then remove the mod.

## Build from source

Visual Studio 2022 (C++ desktop workload), CMake, [vcpkg](https://github.com/microsoft/vcpkg) and
[CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng).

```powershell
$env:ALIFE_CLIB = "C:\path\to\CommonLibSSE-NG"; $env:VCPKG_ROOT = "C:\path\to\vcpkg"
powershell -File build.ps1 -Config Release
```

The core simulation also builds without the game: `tests\build_harness.bat` builds a headless harness that runs the
whole A-Life board against a world dump (see `tests/`). `tools/package.py` assembles the release archive on top of the previous release (`--base-zip`, which carries the
FaceGen and the pool actors' plugin);
`tools/build_factions_esp.py` and `tools/build_talk_esp.py` generate the plugins from `config/*.ltx`.

## Layout

- `src/`, `include/ALife/`: the plugin. `src/core/` is the engine-free simulation (board, factions, smart terrains,
  standing, tasks) that the headless tests build on their own.
- `config/*.ltx`: all tuning, in Anomaly's LTX dialect (squads, places, relations, standing, tasks, dialogue).
- `papyrus/`: the MCM and task scripts. `tools/`: plugin generators, data dumps, packaging. `tests/`: headless tests.

## Status and roadmap

Version 2.x, in active development. Done: the A-Life brain across all of Skyrim and Solstheim, factions and standing,
dialogue, mutants and monsters with lairs, tasks, living news, long-run stability. Next: companions and recruiting,
dragons as A-Life actors, wounded and surrender, more task types (escort, defend, rescue, hunts), stashes, camp life,
and a polished installable package. See [CONTRIBUTING.md](CONTRIBUTING.md) to help.

## Credits

- Design after **STALKER Anomaly**'s A-Life (Anomaly 1.5.3). This is an independent reimplementation in C++: no code
  or files from Anomaly or GSC are included.
- Built on [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng), [SKSE64](https://skse.silverlock.org/),
  the Address Library for SKSE Plugins and SkyUI.
- Written with AI assistance (Claude) and verified in the real game.

Not affiliated with Bethesda, GSC Game World or the Anomaly team. All trademarks belong to their owners.

## License

MIT, see [LICENSE](LICENSE).
