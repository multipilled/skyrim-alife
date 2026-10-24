Fixtures for tools/tests/test_publish_lint.py (ticket G12). Everything here is synthetic: no game files, no Anomaly or
GAMMA files, no real keys. Key-shaped strings are built at test time from obvious placeholders (EXAMPLE-NOT-A-KEY).

clean/    a fake release tree. The repo's .gitignore keeps *.pex and *.dll out of git, so the tests add the two binary
          placeholders at run time (Scripts/ALife_MCM.pex, SKSE/Plugins/ALife.dll); with them it passes publish_lint
          with 0 FAIL and 0 WARN. Tests copy it and plant one failure per rule.
ref/      fake reference trees passed as --game ref/game --ref ref/anomaly --ref ref/gamma. Each file is > 64 bytes so the
          byte-identical check (size index, then sha256) applies to it.
