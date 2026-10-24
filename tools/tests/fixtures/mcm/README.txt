Fixtures for tools/tests/test_mcm_check.py (ticket G12). Everything here is synthetic: a cut-down MCM script, DLL
registration, Settings.h / Settings.cpp snippets, difficulty.ltx and a shipped ALife.ini laid out like the mod checkout
(tools/mcm_check.py reads papyrus/ALife_MCM.psc, src/Papyrus.cpp, include/ALife/Settings.h, src/Settings.cpp and
config/difficulty.ltx under its MOD root). The names the tool hard-codes (SETTERS, MENUS, INI, SCALE_KEYS) are used as is.

good/            a matching pair: mcm_check passes with 0 mismatches (with and without --ini good/ALife.ini).
Tests copy good/ to a temp dir, plant one mismatch, point the tool's MOD at the copy and check the reported line.
