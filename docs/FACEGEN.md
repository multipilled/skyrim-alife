# FaceGen status (A-Life v1.7.6 slice 1)

`ALife.esp` ships **166 NPC_ pool actors** (race/sex correct per spec counts: 24 Breton, 24 Altmer/HighElf, 18 Khajiit, 16 vampire-race, rest 6/sex playable) with placeholder NAM9/QNAM morphs and persistent REFRs in `ALifeHoldingCell`.

**Missing until CK step:** FaceGen exports under
`meshes/actors/character/FaceGenData/FaceGeom/ALife.esp/` and
`textures/actors/character/FaceGenData/FaceTint/ALife.esp/`.

Run Creation Kit **Ctrl+F4** on all `ALife_Pool_*` NPCs once. That launches CK (not Skyrim). No headless FaceGen is in this package. Dark-face bug until then; sim/AI/spawn tests can still proceed.
