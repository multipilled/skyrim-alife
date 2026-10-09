# Contributing

Thanks for helping. Bug reports from real play sessions are the most useful thing you can send.

## Reporting a bug

Use the *Bug report* issue template and include:
- game version (e.g. 1.6.1170 or 1.7.104), SKSE version, mod manager, and your load order (or your SKSE plugin list);
- what happened and where (location, what the squad was doing);
- `ALife.log`: set `[Debug] bDebug=1` in `SKSE/Plugins/ALife.ini`; the log goes to
  `Documents/My Games/Skyrim Special Edition/SKSE/ALife.log`;
- for a crash, the log from your crash logger.

Please don't attach game files.

## Code

- Small, focused pull requests: one feature or fix each.
- Engine-free logic goes in `src/core/` and gets a headless test in `tests/` (the harness runs the whole board without
  the game). Game-side code goes in `src/`.
- Keep the style of the file you edit (tabs in C++; LTX comments that say what a setting does).
- Tuning belongs in `config/*.ltx`, not in code. `config/talk.ltx` is generated: edit the generator instead.
- Test in game before you open the PR and say what you checked (at least one save/load cycle).
- No copyrighted game files, no files from STALKER Anomaly, and nothing decompiled.

Good first issues carry the `good first issue` label. Bigger features follow the gap list in `docs/PARITY.md`.

## AI-assisted contributions

Welcome, as long as you have read, built and tested what you submit.
