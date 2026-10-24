"""Generates mod/config/squads.ltx: {faction}_{novice|advanced|veteran} squad descriptions (Anomaly squad_descr.ltx dialect)."""
fac = {
 # faction: (roles, size_scale) ; roles = role weights
 "bandit":      ("melee:3, heavy:1, archer:2, mage:0.4, leader:1", 1.0, "Bandit raiders"),
 "forsworn":    ("melee:3, archer:2, mage:1, leader:1", 1.0, "Forsworn clansmen"),
 "imperial":    ("melee:3, heavy:2, archer:1.5, mage:0.3, leader:1", 1.0, "Imperial Legion patrol"),
 "stormcloak":  ("melee:3, heavy:2, archer:1.5, mage:0.2, leader:1", 1.0, "Stormcloak patrol"),
 "thalmor":     ("mage:3, spellsword:2, heavy:1, archer:1, leader:1", 0.8, "Thalmor justiciars"),
 "wanderer":    ("melee:2, archer:2, mage:1, spellsword:1, heavy:0.5, leader:1", 0.9, "Adventurers"),
 "hunter":      ("archer:4, melee:1, leader:1", 0.7, "Hunters"),
 "mercenary":   ("melee:3, heavy:1, archer:1.5, spellsword:1, leader:1", 1.0, "Sellswords"),
 "mage":        ("mage:4, spellsword:1, leader:1", 0.8, "Mages"),
 "necromancer": ("mage:4, melee:1, leader:1", 0.9, "Necromancers"),
 "vampire":     ("spellsword:2, melee:2, mage:2, leader:1", 0.9, "Vampires"),
 "dawnguard":   ("archer:3, heavy:2, melee:2, leader:1", 0.9, "Dawnguard"),
 "silverhand":  ("melee:3, heavy:1, archer:1, leader:1", 1.0, "Silver Hand"),
 "vigilant":    ("melee:3, heavy:2, mage:1, leader:1", 0.9, "Vigilants of Stendarr"),
 "caravan":     ("melee:2, archer:1.5, heavy:0.3, leader:1", 0.9, "Khajiit caravan"),
 "skaal":       ("melee:2, archer:2, heavy:0.5, leader:1", 0.8, "Skaal hunters"),
 "redoran":     ("melee:3, heavy:2, archer:1.5, spellsword:0.3, leader:1", 1.0, "Redoran Guard"),
 "reaver":      ("melee:3, heavy:1, archer:2, mage:0.5, leader:1", 1.0, "Reavers"),
}
tiers = {
 "novice":   dict(n=(2, 3), lvl=(-4, 0)),
 "advanced": dict(n=(3, 4), lvl=(-1, 3)),
 "veteran":  dict(n=(3, 5), lvl=(2, 7)),
}
out = ["; Squad descriptions (Anomaly squad_descr.ltx dialect).",
       "; faction = id, tier = novice|advanced|veteran, npc_in_squad = min, max, level_offset = min, max (relative to the player),",
       "; roles = role:weight list (melee, heavy, archer, mage, spellsword, leader), common = scaled by the population factor.", ""]
for f, (roles, scale, desc) in fac.items():
    for t, v in tiers.items():
        lo, hi = max(1, round(v["n"][0] * scale)), max(2, round(v["n"][1] * scale))
        if f == "hunter": lo, hi = 1, min(hi, 3)
        out += [f"[{f}_{t}]", f"faction = {f}", f"tier = {t}", f"npc_in_squad = {lo}, {hi}",
                f"level_offset = {v['lvl'][0]}, {v['lvl'][1]}", f"roles = {roles}", "common = true", ""]
open("../mod/config/squads.ltx", "w", newline="\n").write("\n".join(out))
print(len(fac) * 3, "squad sections written")
