# FaceGen (NPC face files)

`ALife.esp` (built by `tools/esp/build_esp.py`) has **166 NPC_ pool actors**, `ALife_Pool_*`, local FormIDs
`0xA00`..`0xAA5`, placed in `ALifeHoldingCell` as persistent ACHR records (placed actors, local FormIDs `0xB00`..`0xBA5`;
REFR is only for placed objects).

**Face files are not built or stored in this repo.** They are Creation Kit output baked from the game's own head
meshes and textures, so they are generated on the player's/maintainer's machine:

- Running the Creation Kit needs the owner's OK that turn (it is a separate program on the owner's PC; agents never start it unasked).
- Load the repo-built `ALife.esp` in the Creation Kit, select all `ALife_Pool_*` NPCs and press **Ctrl+F4**.
- Output: `meshes/actors/character/FaceGenData/FaceGeom/ALife.esp/<formid>.nif` and
  `textures/actors/character/FaceGenData/FaceTint/ALife.esp/<formid>.dds`.
- Re-export after any change to the pool NPC records. Faces from an older build stay valid only while its NPC_ records are
  unchanged: `compare_esp.py <old> <new>` shows it (the Oct 15 rebuild, T18, is byte-identical to the 2.0.0 release, so the
  2.0.0 face files still match).

**The pool NPC FormIDs must never change.** Each face file is named after its NPC's FormID; a shifted ID leaves that
NPC with the dark-face bug. `tools/esp/compare_esp.py` fails if any NPC_ FormID differs from the reference build.

Without face files the NPCs show dark faces in game; sim, AI and spawn tests are unaffected.
