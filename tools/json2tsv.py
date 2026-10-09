"""world_skyrim.json -> world_skyrim.tsv (id parent hold marker x y z radius name keywords) for the C++ harness. Tamriel exterior only."""
import json
d = json.load(open("../data/world_skyrim.json"))
rows = []
for L in d["locations"]:
    if L["root_world"] != 0x3C: continue
    name = L["edid"]
    if name.endswith("Location"): name = name[:-8]
    rows.append("\t".join(map(str, [L["formid"], L["parent"], L["hold"], 0, L["x"], L["y"], L["z"], L["radius"], name, ",".join(map(str, L["keywords"]))])))
open("../data/world_skyrim.tsv", "w", newline="\n").write("\n".join(rows) + "\n")
print(len(rows), "locations written")
