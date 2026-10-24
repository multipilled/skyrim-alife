# ALife.esp builder

Rebuilds `ALife.esp` (ESL-flagged; masters Skyrim.esm, Update.esm, Dawnguard.esm; 390 records and groups) from the player's own
`Skyrim.esm`. No game data is stored here: the scripts hold only FormID/EditorID references, and every face, package
template and other vanilla value is read from the `Skyrim.esm` path you pass in at build time.

    python -B tools/esp/build_esp.py <Skyrim.esm> <out/ALife.esp> [out/esp-manifest.json] [--author NAME]
    python -B tools/esp/validate_esp.py <out/ALife.esp> <folder with the masters> [<more folders> ...]
    python -B tools/esp/compare_esp.py <reference .esp> <candidate .esp>      (any of the three plugins)

- `build_esp.py` writes the plugin; FormIDs match `include/ALife/Forms.h` (0x900-0x970, NPC_ 0xA00-0xAA5, ACHR 0xB00-0xBA5).
- `validate_esp.py` checks structure, ESL range, HEDR (record count = records plus groups, next object ID), persistence/placement
  and that every reference exists with the right type in the master its FormID names (every MAST of the plugin, looked up by
  name in the folders you pass, then next to the plugin). It also takes ALifeFactions.esp and ALifeTalk.esp. A master it cannot
  find is reported as skipped and the result is INCOMPLETE (exit 2), never PASS.
- `compare_esp.py` compares two builds of any of the three plugins record by record (types, counts, FormIDs, EDIDs, flags,
  placement, data, masters, ESL flag, the TES4 header but the author) and says whether they are byte-identical. Own records
  are matched by local ID, so a plugin that gained a master still lines up; an EditorID that moved to another ID fails.
- `esp-manifest.json` is the expected pool roster (NPC_/ACHR FormIDs, EditorIDs, race FormID, sex, the vanilla preset
  EditorID each face starts from, variant) plus the record count and next object ID. A fresh build's manifest must equal
  it, and `validate_esp.py ... --manifest tools/esp/esp-manifest.json` fails when the plugin and the manifest disagree (package.py runs it).
- `fastscan.py` is the shared reader.

FaceGen (`meshes/.../FaceGeom/ALife.esp/00000a00.nif` ... and `textures/.../FaceTint/ALife.esp/...`) is not built here:
it is a Creation Kit export (Ctrl+F4 on the `ALife_Pool_*` actors) and its files are named after the NPC_ FormIDs, so
those must never shift. Build outputs go outside the repo; `*.esp` is ignored.
