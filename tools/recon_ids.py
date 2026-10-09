"""Recon of vanilla/DLC FormIDs the standing system needs: factions, keywords, armor sets. Writes recon/ids_recon.txt (not shipped)."""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import esp
D = os.environ.get("SKYRIM_DIR", "C:/Program Files (x86)/Steam/steamapps/common/Skyrim Special Edition") + "/Data/"
FILES = ["Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"]
PAT = {
  "FACT": r"thane|crime|companions|thieves|darkbrother|college|^cw|stormcloak|imperial|thalmor|forsworn|vampire|werewolf|silverhand|vigilant|dawnguard|^bandit|necromancer|greybeard|blades|bards|jarl|playerfaction|potentialfollower|dragonborn|guard|legion|sons|volkihar|morag|skaal|redoran|telvanni|eastempire|hunter|mage|warlock|hagraven|giant|witchlight|^orc|^dremora|^draugr|^falmer|^spriggan|^troll|^wolf|^bear|^sabre|^skeever|^mudcrab",
  "KYWD": r"armormaterial|^vampire|actortypenpc|^isbeast|^lycan|werewolf|^armorlight|^armorheavy|armorcuirass|armorclothing|^clothingrich|^clothingpoor|^jewelry|ismilitary|dragon",
  "ARMO": r"stormcloak|imperial|legion|thalmor|forsworn|dawnguard|vigilant|silverhand|bandit|blades|^armorfalmer|volkihar|nightingale|thievesguild|darkbrother|^armorcompanions|wolf|^armorwolf|jarl|guard",
}
out = open("recon/ids_recon.txt", "w", encoding="utf-8")
for fn in FILES:
    buf = esp.load(D + fn)
    rows = {"FACT": [], "KYWD": [], "ARMO": []}
    for r in esp.walk(buf):
        if r.sig in PAT:
            for s, d in r.subs():
                if s == "EDID":
                    e = esp.zstr(d)
                    if re.search(PAT[r.sig], e, re.I): rows[r.sig].append((r.fid, e))
                    break
    out.write(f"=== {fn}\n")
    for sig, lst in rows.items():
        out.write(f" -- {sig} ({len(lst)})\n")
        for fid, e in sorted(lst, key=lambda t: t[1].lower()): out.write(f"  {fid:08X} {e}\n")
out.close()
print("ok")
