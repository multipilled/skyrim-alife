#!/usr/bin/env python3
"""Validate every config/*.ltx the C++ loaders read (ticket G2). Python 3 standard library only.

    python3 tools/check_config.py config          -> "OK: <N> files, <K> checks", exit 0
                                                     or one "path:line: [rule] message (source)" per error, exit 1

Every rule comes from the loader that reads the value. The source of each rule is cited as file:line, either in a comment
next to the code that checks it or in the last column of the schema tables (`cite`). tools/tests/test_check_config.py
checks that every cited line exists and that each table row's line names the key it checks.

Principle for numbers: a value is an error when the loader would read it differently from what the file says: it does not
parse (std::stof / std::stoi / std::stoul would throw or stop early), it lies outside the range the loader clamps it to, or
it breaks the ticket's kinds (probabilities and shares in [0, 1], weights and multipliers > 0, counts integers >= 0).
Unknown keys in sections whose keys the loader lists in full are warnings ("WARN ..."): the loader ignores them.

Rule ids (the text in brackets):
  ltx.header ltx.orphan ltx.include ltx.key ltx.cycle      LTX syntax (src/core/Ltx.cpp)
  dup.section dup.key                                       duplicates
  ref.parent ref.section ref.faction ref.squad ref.kind ref.race ref.channel ref.creature ref.role ref.tasktype
  ref.job ref.flag ref.enum ref.worldspace                  something another key refers to does not exist
  req.section req.key                                       a section / key the loader needs is missing
  num.number num.int num.bool num.hex num.prob num.weight num.mult num.count num.range num.pair
"""
import os
import re
import sys

# ------------------------------------------------------------------------------------------------ ids from the C++ tables
# src/core/Core.cpp:7-18 kFac (enum Faction order, include/ALife/Core.h:23-33)
FACTIONS = ["bandit", "forsworn", "imperial", "stormcloak", "thalmor", "wanderer", "hunter", "mercenary", "mage",
            "necromancer", "vampire", "dawnguard", "silverhand", "vigilant", "caravan", "skaal", "redoran", "reaver",
            "wildlife", "draugr", "falmer", "troll", "spriggan", "zombie", "hound", "werebeast", "riekling", "spirit",
            "hagraven", "giant", "dwemer", "dragon"]
# include/ALife/Core.h:34 kHumanFactionCount = Wildlife: the first 18 have pool actors, standing, dialogue
HUMAN = FACTIONS[:FACTIONS.index("wildlife")]
# src/core/Core.cpp:57-58 kRaceIds (enum RaceId, include/ALife/Core.h:49-54): the 14 races the actor pool has
RACES = ["nord", "imperial", "breton", "redguard", "altmer", "bosmer", "dunmer", "orsimer", "argonian", "khajiit",
         "nordvampire", "imperialvampire", "bretonvampire", "dunmervampire"]
# src/core/Standing.cpp:63 RaceFromName: standing rules know the 10 player races only
PLAYER_RACES = RACES[:10]
# src/core/Core.cpp:22-26 kKindNames (enum Kind)
KINDS = ["wilderness", "city", "town", "farm", "inn", "fort", "bandit_camp", "forsworn_camp", "military_camp", "dungeon",
         "crypt", "dwarven_ruin", "mine", "vampire_lair", "warlock_lair", "werewolf_lair", "giant_camp", "falmer_hive",
         "dragon_lair", "animal_den", "hagraven_nest", "spriggan_grove", "orc_stronghold", "dragon_priest_lair",
         "shipwreck", "mill"]
# src/core/Difficulty.cpp:9 kIds (enum DiffChannel) and src/core/Difficulty.cpp:11 kRows (iDifficulty 0-5)
CHANNELS = ["population", "monsters", "strength", "lethality", "economy", "mercy", "tasks", "dragons"]
DIFF_ROWS = ["novice", "apprentice", "adept", "expert", "master", "legendary"]
# src/core/Config.cpp:45-52 MonsterTypeFromName
MONSTER_TYPES = ["predatory_day", "predatory_night", "zombied_day", "zombied_night", "vegetarian", "zombied", "special",
                 "flyer"]
# src/core/Tasks.cpp:15 kTypeIds (index 0 "none" is not a task type: TaskTypeFromId starts at 1, src/core/Tasks.cpp:181)
TASK_TYPES = ["bounty", "assault", "delivery", "fetch", "escort", "defend", "rescue", "recover", "hunt"]
# src/core/Camp.cpp:12 kJobIds (CampJobFromId skips "none", src/core/Camp.cpp:32)
CAMP_JOBS = ["camper", "sniper", "guard", "patrol", "collector", "walker", "campfire", "sleep"]
# src/core/Config.cpp:515 roleNames
ROLES = ["melee", "heavy", "archer", "mage", "spellsword", "leader"]
# src/core/Config.cpp:506-507 tier names (anything else silently becomes novice)
TIERS = ["novice", "advanced", "veteran"]
# src/core/Standing.cpp:14-19 PlayerFlag::FromName
PLAYER_FLAGS = ["imperial", "stormcloak", "companions", "thieves", "brotherhood", "college", "dawnguard", "volkihar",
                "blades", "greybeards", "vampire", "werewolf", "beast", "armed", "sneaking", "mounted", "incombat",
                "night", "skaal", "ravenrock"]
# include/ALife/Core.h:58 kWorldspaceIds: races.ltx [races] is Tamriel, [races.<id>] the others
WORLDSPACES = ["Tamriel", "DLC2SolstheimWorld"]
# include/ALife/Core.h:249-250, include/ALife/Compat.h:39-40, include/ALife/Stashes.h:21
MAX_ONLINE_GROUPS, MAX_ONLINE_ACTORS, COMPAT_ACTORS_PER_GROUP, COMPAT_MIN_POPULATION, STASH_TIERS = 3, 15, 5, 0.3, 5

# ------------------------------------------------------------------------------------------------ results
class Report:
    def __init__(self):
        self.errors, self.warnings, self.checks, self.files = [], [], 0, []

    def check(self, cond, path, line, rule, msg, cite):
        self.checks += 1
        if not cond:
            self.errors.append((path, line, rule, msg, cite))
        return cond

    def error(self, path, line, rule, msg, cite):
        return self.check(False, path, line, rule, msg, cite)

    def warn(self, path, line, rule, msg, cite):
        self.warnings.append((path, line, rule, msg, cite))

    @staticmethod
    def fmt(e, prefix=""):
        path, line, rule, msg, cite = e
        return "%s%s:%d: [%s] %s (%s)" % (prefix, path, line, rule, msg, cite)

# ------------------------------------------------------------------------------------------------ LTX, as src/core/Ltx.cpp
# The reader itself is tools/ltxlib.py (ticket G7), shared with tools/gen_config_docs.py; this class only turns what the
# loader silently accepts into errors, through ltxlib's hooks.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ltxlib  # noqa: E402
from ltxlib import trim, split_list  # noqa: E402

Ln = ltxlib.Line      # key, val, path, line
Sec = ltxlib.Section  # name, path, line, parents, parents_at, lines


class Ltx(ltxlib.Ltx):
    """One Ltx object of the loader: a top-level file plus its #include files."""

    def __init__(self, rep, path):
        self.rep, self.name = rep, os.path.basename(path)
        ltxlib.Ltx.__init__(self, path)
        self.check_structure()

    def on_file(self, path):
        self.rep.files.append(path)

    def on_open_error(self, path, inc_at, err):
        if inc_at:   # src/core/Ltx.cpp:58: a missing #include is only an error message, the rest still loads
            self.rep.error(inc_at[0], inc_at[1], "ltx.include", "cannot open included file %s (%s)" % (path, err.strerror),
                           "src/core/Ltx.cpp:58")

    def on_include_cycle(self, path, inc_at):   # src/core/Ltx.cpp:58 has no guard: the loader would recurse without end
        self.rep.error(inc_at[0], inc_at[1], "ltx.include", "#include cycle: %s is already being read" % path,
                       "src/core/Ltx.cpp:58")

    def on_include_unquoted(self, path, no, line):
        self.rep.error(path, no, "ltx.include", "#include without a quoted file name is ignored", "src/core/Ltx.cpp:56")

    def on_bad_header(self, path, no, line):   # src/core/Ltx.cpp:65-68: the header is skipped, its keys land in the previous section
        self.rep.error(path, no, "ltx.header", "bad section header '%s' (no ']')" % line, "src/core/Ltx.cpp:66")

    def on_empty_section(self, path, no):
        self.rep.error(path, no, "ltx.header", "empty section name: the keys below it are dropped", "src/core/Ltx.cpp:79")

    def on_section(self, s, path, no, reopened):
        if reopened:   # src/core/Ltx.cpp:70: a second [name] reopens the first and appends to it
            self.rep.error(path, no, "dup.section", "duplicate section [%s] (first at %s:%d)" % (s.name, s.path, s.line),
                           "src/core/Ltx.cpp:70")
        else:
            self.rep.check(True, path, no, "dup.section", "", "src/core/Ltx.cpp:70")

    def on_header_text(self, path, no, rest):
        self.rep.error(path, no, "ltx.header", "text after ']' is ignored: '%s'" % rest, "src/core/Ltx.cpp:73")

    def on_orphan(self, path, no, line):   # src/core/Ltx.cpp:79: keys before the first section are dropped
        self.rep.error(path, no, "ltx.orphan", "key outside any section is ignored: '%s'" % line, "src/core/Ltx.cpp:79")

    def on_empty_key(self, s, ln):
        self.rep.error(ln.path, ln.line, "ltx.key", "empty key in [%s]" % s.name, "src/core/Ltx.cpp:81")

    def on_key(self, s, ln, first):
        if first:   # src/core/Ltx.cpp:100: the later line silently replaces the earlier one
            p, l = first
            self.rep.error(ln.path, ln.line, "dup.key", "duplicate key '%s' in [%s] (first at %s:%d)" % (ln.key, s.name, p, l),
                           "src/core/Ltx.cpp:100")
        else:
            self.rep.check(True, ln.path, ln.line, "dup.key", "", "src/core/Ltx.cpp:100")

    def check_structure(self):
        for name in self.order:
            s = self.sections[name]
            for p in s.parents:   # src/core/Ltx.cpp:94: a missing parent is silently skipped
                self.rep.check(p in self.sections, s.parents_at[0], s.parents_at[1], "ref.parent",
                               "[%s] inherits from missing section [%s]" % (name, p), "src/core/Ltx.cpp:94")
            if s.parents:
                cyc = self.cycle(name, [])
                self.rep.check(not cyc, s.parents_at[0], s.parents_at[1], "ltx.cycle",
                               "inheritance cycle %s" % " -> ".join(cyc or []), "src/core/Ltx.cpp:92")

    def has(self, sec, key=None):
        return self.Has(sec, key)

    def sec(self, name):   # src/core/Ltx.cpp:90-115: parents first, a child's key replaces in place
        return self.section(name)

    def get(self, sec, key):
        return self.line(sec, key)

    def at(self, name):
        s = self.sections.get(name)
        return (s.path, s.line) if s else (self.path, 1)

# ------------------------------------------------------------------------------------------------ value checks
FLOAT_RE = re.compile(r"^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$")
INT_RE = re.compile(r"^[+-]?\d+$")
HEX_RE = re.compile(r"^(0[xX])?[0-9A-Fa-f]+$")
BOOLS = ("true", "false", "on", "off", "yes", "no", "1", "0")   # src/core/Ltx.cpp:151: anything else reads as false


class V:
    """A value kind: base type (f / i / b / h / s) plus an optional range and the rule a range break reports."""

    def __init__(self, t, lo=None, hi=None, rule="num.range", lo_open=False, what=""):
        self.t, self.lo, self.hi, self.rule, self.lo_open, self.what = t, lo, hi, rule, lo_open, what


F = V("f")
I = V("i")
B = V("b")
HEX = V("h")
STR = V("s")
PROB = V("f", 0, 1, "num.prob", what="probability / share in [0, 1]")
MULT = V("f", 0, None, "num.mult", True, "multiplier > 0")
WEIGHT = V("f", 0, None, "num.weight", True, "weight > 0")
WEIGHT0 = V("f", 0, None, "num.weight", what="weight >= 0")
COUNT = V("i", 0, None, "num.count", what="count: integer >= 0")
NN = V("f", 0, None, what=">= 0")
POS = V("f", 0, None, lo_open=True, what="> 0")
HOUR = V("f", 0, 24, what="hour of day in [0, 24]")


def R(lo, hi, t="f"):
    return V(t, lo, hi, what="in [%s, %s] (the loader clamps)" % (lo, hi))


def MIN(lo, t="f"):
    return V(t, lo, None, what=">= %s (the loader clamps)" % lo)


def parse_num(rep, ln, t, cite, text=None, label=None):
    """Number of type t ('f' float, 'i' int) from text (default ln.val); None (and an error) when it does not parse."""
    s = ln.val if text is None else text
    label = label or ln.key
    if t == "i":
        if rep.check(bool(INT_RE.match(s)), ln.path, ln.line, "num.int", "%s = '%s': not an integer" % (label, s), cite):
            return int(s)
        return None
    if rep.check(bool(FLOAT_RE.match(s)), ln.path, ln.line, "num.number", "%s = '%s': not a number" % (label, s), cite):
        return float(s)
    return None


def in_range(rep, ln, v, kind, cite, label=None):
    if v is None or (kind.lo is None and kind.hi is None):
        return
    ok = True
    if kind.lo is not None:
        ok = v > kind.lo if kind.lo_open else v >= kind.lo
    if kind.hi is not None:
        ok = ok and v <= kind.hi
    rep.check(ok, ln.path, ln.line, kind.rule, "%s = %s: must be %s" % (label or ln.key, fmtn(v), kind.what), cite)


def fmtn(v):
    return ("%d" % v) if float(v).is_integer() else ("%g" % v)


def value(rep, ln, kind, cite, text=None, label=None):
    s = ln.val if text is None else text
    label = label or ln.key
    if kind.t == "b":
        rep.check(s in BOOLS, ln.path, ln.line, "num.bool", "%s = '%s': not a boolean (%s)" % (label, s, "/".join(BOOLS)), cite)
        return s in ("true", "on", "yes", "1")
    if kind.t == "h":
        return hexid(rep, ln, s, cite, label)
    if kind.t == "s":
        return s
    v = parse_num(rep, ln, kind.t, cite, s, label)
    in_range(rep, ln, v, kind, cite, label)
    return v


def hexid(rep, ln, s, cite, label=None, nonzero=True):
    ok = bool(HEX_RE.match(s)) and (not nonzero or int(s, 16) != 0)
    rep.check(ok, ln.path, ln.line, "num.hex", "%s = '%s': not a non-zero hex FormID" % (label or ln.key, s), cite)
    return int(s, 16) if ok else None


def pair(rep, ln, kind, cite, ordered=True):
    """'lo, hi': two numbers of kind; with ordered, lo <= hi (the loaders swap or clamp a reversed pair)."""
    v = split_list(ln.val)
    if not rep.check(len(v) == 2, ln.path, ln.line, "num.pair", "%s = '%s': needs two numbers" % (ln.key, ln.val), cite):
        return None
    a = value(rep, ln, kind, cite, v[0], ln.key + "[0]")
    b = value(rep, ln, kind, cite, v[1], ln.key + "[1]")
    if a is None or b is None:
        return None
    if ordered:
        rep.check(a <= b, ln.path, ln.line, "num.range", "%s = %s: the first value is above the second" % (ln.key, ln.val), cite)
    return a, b


def ref(rep, ln, ok, rule, what, name, cite):
    return rep.check(ok, ln.path, ln.line, rule, "%s = %s: unknown %s '%s'" % (ln.key, ln.val, what, name), cite)


def faction(rep, ln, name, cite, human=False):
    ok = name in (HUMAN if human else FACTIONS)
    return ref(rep, ln, ok, "ref.faction", "human faction" if human else "faction", name, cite)


def need_section(rep, x, sec, cite, at=None, why=""):
    p, l = at if at else (x.path, 1)
    return rep.check(x.has(sec), p, l, "ref.section" if at else "req.section",
                     "missing section [%s]%s" % (sec, why), cite)


# Keys the loaders read, one row per key: key, kind, the loader line it comes from. Kinds: f (float), i (int), b (bool),
# hex, str (checked by the code below), prob, mult, weight (> 0), weight0 (>= 0), count, nn (>= 0), pos (> 0), hour,
# range:lo:hi / irange:lo:hi and min:lo / imin:lo (the loader clamps), pair:<kind> ("lo, hi", lo <= hi), pairany:<kind>.
# Section templates: [creature_*] [<squad>] [<preset>] [<overlay>] [spawn_*] [kind.*] [compat_*] [slots_*]; <faction> = every
# faction id. A key the loader does not list for its section is a warning (key.unknown).
SPEC_TEXT = """
== alife.ltx
[sim]
  step_hours                         pos              src/core/Config.cpp:213
  walk_speed                         pos              src/core/Config.cpp:214
  max_catchup_hours                  pos              src/core/Config.cpp:215
[online]
  switch_distance                    nn               src/core/Config.cpp:216
  offline_distance                   nn               src/core/Config.cpp:217
[board]
  top_targets                        count            src/core/Config.cpp:218
  best_chance                        prob             src/core/Config.cpp:219
  distance_bias                      nn               src/core/Config.cpp:220
  idle_min                           nn               src/core/Config.cpp:221
  idle_max                           nn               src/core/Config.cpp:222
  retarget_hours                     pos              src/core/Config.cpp:223
[squad_behaviour]
  actor                              weight0          src/core/Config.cpp:224
  all                                weight0          src/core/Config.cpp:225
  base                               weight0          src/core/Config.cpp:226
  lair                               weight0          src/core/Config.cpp:227
  resource                           weight0          src/core/Config.cpp:228
  squad                              weight0          src/core/Config.cpp:229
  surge                              weight0          src/core/Config.cpp:230
  territory                          weight0          src/core/Config.cpp:231
[monster_behaviour]
  lair                               weight0          src/core/Config.cpp:246
  monster                            weight0          src/core/Config.cpp:247
  territory                          weight0          src/core/Config.cpp:248
  base                               weight0          src/core/Config.cpp:249
  resource                           weight0          src/core/Config.cpp:250
  home                               weight0          src/core/Config.cpp:251
  lair_idle_min                      nn               src/core/Config.cpp:252
  lair_idle_max                      nn               src/core/Config.cpp:253
[time_windows]
  base_raid_from                     hour             src/core/Config.cpp:232
  base_raid_to                       hour             src/core/Config.cpp:233
  territory_raid_from                hour             src/core/Config.cpp:234
  territory_raid_to                  hour             src/core/Config.cpp:235
  resource_from                      hour             src/core/Config.cpp:236
  resource_to                        hour             src/core/Config.cpp:237
  monster_day_from                   hour             src/core/Config.cpp:238
  monster_day_to                     hour             src/core/Config.cpp:239
  monster_night_from                 hour             src/core/Config.cpp:240
  monster_night_to                   hour             src/core/Config.cpp:241
  day_monster_offline_from           hour             src/core/Config.cpp:242
  day_monster_offline_to             hour             src/core/Config.cpp:243
  night_monster_offline_from         hour             src/core/Config.cpp:244
  night_monster_offline_to           hour             src/core/Config.cpp:245
[respawn]
  default_idle                       nn               src/core/Config.cpp:254
  default_radius                     nn               src/core/Config.cpp:255
  population_factor                  mult             src/core/Config.cpp:256
  start_fill                         prob             src/core/Config.cpp:258
  monster_population_factor          mult             src/core/Config.cpp:259
[ocs]
  mode                               str              src/core/Config.cpp:261
  combat_distance                    nn               src/core/Config.cpp:264
  hit_base                           nn               src/core/Config.cpp:265
  hit_factor                         nn               src/core/Config.cpp:266
  rank_fraction                      pos              src/core/Config.cpp:267
  commander_gain                     nn               src/core/Config.cpp:268
  rounds_per_step                    count            src/core/Config.cpp:269
  min_squads                         count            src/core/Config.cpp:270
  flee_ratio                         prob             src/core/Config.cpp:271
  heal_per_hour                      nn               src/core/Config.cpp:272
[loot]
  corpses                            count            src/core/Config.cpp:273
  radius                             nn               src/core/Config.cpp:274
  grace_seconds                      nn               src/core/Config.cpp:275
  give_up_seconds                    nn               src/core/Config.cpp:276
  min_value                          count            src/core/Config.cpp:277
  factions                           str              src/core/Config.cpp:278
[balance]
  level_pace_hours                   pos              src/core/Config.cpp:284
  levels_per_pace                    count            src/core/Config.cpp:285
  regear_gap                         count            src/core/Config.cpp:286
[needs]
  hunger_per_hour                    nn               src/core/Config.cpp:287
  fatigue_per_hour                   nn               src/core/Config.cpp:288
  cold_day                           nn               src/core/Config.cpp:289
  cold_night                         nn               src/core/Config.cpp:290
  hungry_at                          nn               src/core/Config.cpp:291
  tired_at                           nn               src/core/Config.cpp:292
  cold_at                            nn               src/core/Config.cpp:293
  bias_food                          nn               src/core/Config.cpp:294
  bias_rest                          nn               src/core/Config.cpp:295
  bias_warm                          nn               src/core/Config.cpp:296
  bias_sell                          nn               src/core/Config.cpp:297
  bias_heal                          nn               src/core/Config.cpp:298
[activities]
  hunt_lair_weight                   weight0          src/core/Config.cpp:496
  settlement_raid_cooldown_days      nn               src/core/Config.cpp:299
  dungeon_reset_days                 nn               src/core/Config.cpp:300
  raid_section_hours                 pos              src/core/Config.cpp:301
  merchant_cycle_hours               pos              src/core/Config.cpp:302
  merchant_gold_floor                prob             src/core/Config.cpp:303
  sell_rate                          prob             src/core/Config.cpp:304
  buy_markup                         mult             src/core/Config.cpp:305
  max_purchases_per_cycle            count            src/core/Config.cpp:306
  hunt_rate                          nn               src/core/Config.cpp:307
  wildlife_risk                      prob             src/core/Config.cpp:308
  fish_rate                          nn               src/core/Config.cpp:309
[news]
  enabled                            b                src/core/Config.cpp:310
  radius                             nn               src/core/Config.cpp:311
  sos                                b                src/core/Config.cpp:312
  death_reports                      b                src/core/Config.cpp:313
  gossip                             b                src/core/Config.cpp:314
  sos_cooldown_hours                 nn               src/core/Config.cpp:315
  death_report_cooldown_hours        nn               src/core/Config.cpp:316
  gossip_hours                       nn               src/core/Config.cpp:317
  sos_gap_hours                      nn               src/core/Config.cpp:318
  battle_report_gap_hours            nn               src/core/Config.cpp:319
  death_report_gap_hours             nn               src/core/Config.cpp:320
[activity_factions]
  trade                              str              src/core/Config.cpp:478
  raid_settlement                    str              src/core/Config.cpp:478
  clear_dungeon                      str              src/core/Config.cpp:478
  hunt                               str              src/core/Config.cpp:478
  fish                               str              src/core/Config.cpp:478
  harvest_parts                      str              src/core/Config.cpp:478
  hunt_lairs                         str              src/core/Config.cpp:478

== camp.ltx
[jobs]
  camper                             count            src/core/Camp.cpp:58
  sniper                             count            src/core/Camp.cpp:58
  guard                              count            src/core/Camp.cpp:58
  patrol                             count            src/core/Camp.cpp:58
  collector                          count            src/core/Camp.cpp:58
  walker                             count            src/core/Camp.cpp:58
  campfire                           count            src/core/Camp.cpp:58
  sleep                              count            src/core/Camp.cpp:58
[time]
  sleep_from                         hour             src/core/Camp.cpp:69
  sleep_to                           hour             src/core/Camp.cpp:70
  night_sleep_prior                  count            src/core/Camp.cpp:71
  watch_share                        prob             src/core/Camp.cpp:72
[scene]
  idle_min                           nn               src/core/Camp.cpp:73
  idle_max                           nn               src/core/Camp.cpp:74
  story_chance                       prob             src/core/Camp.cpp:75
  chatter_chance                     prob             src/core/Camp.cpp:76
[rest]
  rest_rate                          nn               src/core/Camp.cpp:77
[budget]
  scan_radius                        nn               src/core/Camp.cpp:78
  reassign_seconds                   min:1            src/core/Camp.cpp:79
  max_package_changes                irange:1:15      src/core/Camp.cpp:80
[slots_*]
  camper                             nn               src/core/Camp.cpp:25
  sniper                             nn               src/core/Camp.cpp:25
  guard                              nn               src/core/Camp.cpp:25
  patrol                             nn               src/core/Camp.cpp:25
  collector                          nn               src/core/Camp.cpp:25
  walker                             nn               src/core/Camp.cpp:25
  campfire                           nn               src/core/Camp.cpp:25
  sleep                              nn               src/core/Camp.cpp:25

== companions.ltx
[companions]
  max_members                        irange:1:8       src/core/Party.cpp:26
  max_squads                         irange:1:2       src/core/Party.cpp:27
  paid_factions                      str              src/core/Party.cpp:28
  day_gold                           min:1            src/core/Party.cpp:43
  days                               pairany:imin:1   src/core/Party.cpp:44
  warn_hours                         nn               src/core/Party.cpp:48
  teleport_distance                  nn               src/core/Party.cpp:49
  move_distance                      nn               src/core/Party.cpp:50
  wait_place_distance                nn               src/core/Party.cpp:51
  loot_default                       b                src/core/Party.cpp:52
[willing_chance]
  novice                             prob             src/core/Party.cpp:36
  advanced                           prob             src/core/Party.cpp:36
  veteran                            prob             src/core/Party.cpp:36
[price_factor]
  novice                             pair:mult        src/core/Party.cpp:37
  advanced                           pair:mult        src/core/Party.cpp:37
  veteran                            pair:mult        src/core/Party.cpp:37

== compat.ltx
[compat]
  mods                               str              src/core/Compat.cpp:61
  force                              str              src/core/Compat.cpp:57
  handover_owner                     str              src/core/Compat.cpp:59
  handover_population                count            src/core/Compat.cpp:60
[compat_*]
  name                               str              src/core/Compat.cpp:69
  plugins                            str              src/core/Compat.cpp:70
  handover_kinds                     str              src/core/Compat.cpp:71
  handover                           str              src/core/Compat.cpp:77
  keep                               str              src/core/Compat.cpp:78
  online_groups                      irange:1:3       src/core/Compat.cpp:79
  online_actors                      irange:5:15      src/core/Compat.cpp:80
  population                         range:0.3:1      src/core/Compat.cpp:81
  patrol_scale                       range:0:1        src/core/Compat.cpp:82

== difficulty.ltx
[derive]
  size_exponent                      range:0:3        src/core/Difficulty.cpp:67
  max_squad_size                     irange:1:15      src/core/Difficulty.cpp:68
  levels_per_strength                f                src/core/Difficulty.cpp:69
  level_min                          i                src/core/Difficulty.cpp:70
  level_max                          i                src/core/Difficulty.cpp:71
  gear_levels_per_strength           f                src/core/Difficulty.cpp:72
  gear_min                           i                src/core/Difficulty.cpp:73
  gear_max                           i                src/core/Difficulty.cpp:74

== dragons.ltx
[dragons]
  max_dragons                        nn               src/core/Config.cpp:547
  respawn_days                       min:0.01         src/core/Config.cpp:548
  level_mult                         mult             src/core/Config.cpp:549
  fly_speed                          min:1000         src/core/Config.cpp:550
  stay_min                           nn               src/core/Config.cpp:551
  stay_max                           nn               src/core/Config.cpp:552
  prey_bonus                         nn               src/core/Config.cpp:553
  lair_weight                        weight0          src/core/Config.cpp:554
  wall_weight                        weight0          src/core/Config.cpp:555
  high_weight                        weight0          src/core/Config.cpp:556
  high_z                             f                src/core/Config.cpp:557
  flee_hours                         nn               src/core/Config.cpp:558
  online_distance                    nn               src/core/Config.cpp:559
  offline_distance                   nn               src/core/Config.cpp:560
  spawn_height                       f                src/core/Config.cpp:561
  online_max_hours                   nn               src/core/Config.cpp:562

== factions.ltx
[dynamic_relations]
  enabled                            b                src/core/Config.cpp:338
  death_value                        nn               src/core/Config.cpp:339
  friend_limit                       i                src/core/Config.cpp:340
  enemy_limit                        i                src/core/Config.cpp:341
  locked_pairs                       str              src/core/Config.cpp:342
[civil_war]
  enabled                            b                src/core/Config.cpp:367
  sides                              str              src/core/Config.cpp:368
  guard_entries                      str              src/core/Config.cpp:373
  stand_down                         str              src/core/Config.cpp:374

== monsters.ltx
[creature_*]
  name                               str              src/core/Config.cpp:434
  form                               str              src/core/Config.cpp:435
  health                             nn               src/core/Config.cpp:442
  health_per_level                   nn               src/core/Config.cpp:443
  damage                             nn               src/core/Config.cpp:444
  damage_per_level                   nn               src/core/Config.cpp:445
  hit                                prob             src/core/Config.cpp:446
  parts                              str              src/core/Config.cpp:447

== props.ltx
[<preset>]
  base                               f                src/core/Config.cpp:619
  territory                          f                src/core/Config.cpp:620
  resource                           f                src/core/Config.cpp:621
  surge                              f                src/core/Config.cpp:622
  lair                               f                src/core/Config.cpp:623
  all                                f                src/core/Config.cpp:624
  monster                            f                src/core/Config.cpp:625
  owner                              f                src/core/Config.cpp:640
  max_population                     count            src/core/Config.cpp:627
  monster_population                 count            src/core/Config.cpp:628
  arrive_dist                        nn               src/core/Config.cpp:629
  sim_avail                          b                src/core/Config.cpp:630
  raidable                           b                src/core/Config.cpp:631
  faction_controlled                 b                src/core/Config.cpp:632
  respawn_idle                       nn               src/core/Config.cpp:633
  respawn_radius                     nn               src/core/Config.cpp:634
  default_faction                    str              src/core/Config.cpp:638
  respawn                            str              src/core/Config.cpp:655
  <faction>                          f                src/core/Config.cpp:626
[<overlay>]
  base                               f                src/core/Config.cpp:664
  territory                          f                src/core/Config.cpp:665
  resource                           f                src/core/Config.cpp:666
  lair                               f                src/core/Config.cpp:667
  all                                f                src/core/Config.cpp:668
  monster                            f                src/core/Config.cpp:669
  max_population                     count            src/core/Config.cpp:671
  monster_population                 count            src/core/Config.cpp:672
  respawn_idle                       nn               src/core/Config.cpp:673
  respawn                            str              src/core/Config.cpp:674
  <faction>                          f                src/core/Config.cpp:670
[spawn_*]
  squads                             str              src/core/Config.cpp:686
  num                                mult             src/core/Config.cpp:687
  faction                            str              src/core/Config.cpp:689

== squads.ltx
[<squad>]
  faction                            str              src/core/Config.cpp:504
  tier                               str              src/core/Config.cpp:506
  npc_in_squad                       str              src/core/Config.cpp:508
  level_offset                       str              src/core/Config.cpp:511
  common                             b                src/core/Config.cpp:514
  roles                              str              src/core/Config.cpp:517
  creatures                          str              src/core/Config.cpp:528
  leader                             str              src/core/Config.cpp:535

== standing.ltx
[tiers]
  hostile                            f                src/core/Standing.cpp:151
  unfriendly                         f                src/core/Standing.cpp:151
  friendly                           f                src/core/Standing.cpp:152
  allied                             f                src/core/Standing.cpp:152
[deeds]
  goodwill_max                       pos              src/core/Standing.cpp:153
  kill_base                          nn               src/core/Standing.cpp:154
  kill_per_level                     nn               src/core/Standing.cpp:154
  kill_veteran                       nn               src/core/Standing.cpp:154
  kill_provoked_mult                 mult             src/core/Standing.cpp:155
  ally_share                         prob             src/core/Standing.cpp:156
  friend_share                       prob             src/core/Standing.cpp:156
  enemy_share                        prob             src/core/Standing.cpp:156
  unseen_mult                        mult             src/core/Standing.cpp:157
  discover_chance                    prob             src/core/Standing.cpp:157
  discover_mult                      mult             src/core/Standing.cpp:157
  discover_min_hours                 nn               src/core/Standing.cpp:158
  discover_max_hours                 nn               src/core/Standing.cpp:158
  help_base                          nn               src/core/Standing.cpp:159
  help_per_level                     nn               src/core/Standing.cpp:159
  daily_gain_cap                     nn               src/core/Standing.cpp:159
  trade_per_100_gold                 nn               src/core/Standing.cpp:160
  attack_goodwill                    nn               src/core/Standing.cpp:160
  notoriety_per_severity             nn               src/core/Standing.cpp:161
[memory]
  attack                             f                src/core/Standing.cpp:162
  kill                               f                src/core/Standing.cpp:162
  help                               f                src/core/Standing.cpp:162
  toll                               f                src/core/Standing.cpp:163
  half_life_days                     pos              src/core/Standing.cpp:163
[decay]
  goodwill_positive_half_life_days   pos              src/core/Standing.cpp:164
  goodwill_negative_half_life_days   pos              src/core/Standing.cpp:165
  notoriety_half_life_days           pos              src/core/Standing.cpp:166
[disguise]
  bonus                              f                src/core/Standing.cpp:167
  suspicion                          f                src/core/Standing.cpp:167
[predators]
  advantage_floor                    nn               src/core/Standing.cpp:168
  advantage_full                     nn               src/core/Standing.cpp:168
  hunger_bonus                       nn               src/core/Standing.cpp:169
  wealth_bonus                       nn               src/core/Standing.cpp:169
  night_bonus                        nn               src/core/Standing.cpp:170
  fear_penalty                       nn               src/core/Standing.cpp:170
  parley_from                        nn               src/core/Standing.cpp:171
  attack_at                          nn               src/core/Standing.cpp:171
  ambush_at                          nn               src/core/Standing.cpp:171
[meeting]
  notice_range                       nn               src/core/Standing.cpp:172
  greet_range                        nn               src/core/Standing.cpp:172
  challenge_range                    nn               src/core/Standing.cpp:173
  challenge_grace                    nn               src/core/Standing.cpp:173
  territory_radius                   nn               src/core/Standing.cpp:174
  talk_cooldown                      nn               src/core/Standing.cpp:175
[toll]
  base                               nn               src/core/Standing.cpp:176
  per_level                          nn               src/core/Standing.cpp:176
  pass_hours                         nn               src/core/Standing.cpp:176
[crime]
  murder_bounty                      nn               src/core/Standing.cpp:177
[notices]
  enabled                            b                src/core/Standing.cpp:178

== stashes.ltx
[stashes]
  enabled                            b                src/core/Stashes.cpp:63
  kinds                              str              src/core/Stashes.cpp:64
  spots_per_place                    irange:0:4       src/core/Stashes.cpp:70
  offset                             pair:nn          src/core/Stashes.cpp:71
  max_spots                          count            src/core/Stashes.cpp:72
  hidden_share                       prob             src/core/Stashes.cpp:73
  reveal_radius                      nn               src/core/Stashes.cpp:74
  reveal_min                         nn               src/core/Stashes.cpp:75
  place_radius                       nn               src/core/Stashes.cpp:76
  marker_slots                       irange:0:32      src/core/Stashes.cpp:77
[notes]
  chance                             prob             src/core/Stashes.cpp:78
[reward]
  chance                             prob             src/core/Stashes.cpp:79
  replace_gold                       b                src/core/Stashes.cpp:80
  budget_per_gold                    nn               src/core/Stashes.cpp:81
[loot]
  budget                             pair:nn          src/core/Stashes.cpp:82
  per_level                          nn               src/core/Stashes.cpp:85
  tier_levels                        str              src/core/Stashes.cpp:86
  tier_chance                        str              src/core/Stashes.cpp:86
  tier_count                         str              src/core/Stashes.cpp:86
  gold_form                          hex              src/core/Stashes.cpp:90
  kinds                              str              src/core/Stashes.cpp:91
[kind.*]
  chance                             range:0:100      src/core/Stashes.cpp:96
  count                              pair:imin:1      src/core/Stashes.cpp:98
  t1                                 str              src/core/Stashes.cpp:102
  t2                                 str              src/core/Stashes.cpp:102
  t3                                 str              src/core/Stashes.cpp:102
  t4                                 str              src/core/Stashes.cpp:102
  t5                                 str              src/core/Stashes.cpp:102

== tasks.ltx
[tasks]
  max_active                         irange:1:4       src/core/Tasks.cpp:225
  no_job_chance                      prob             src/core/Tasks.cpp:226
  offer_hours                        nn               src/core/Tasks.cpp:227
  repeat_hours                       nn               src/core/Tasks.cpp:228
  expire_days                        nn               src/core/Tasks.cpp:229
  max_level_over                     count            src/core/Tasks.cpp:230
  bounty_range                       nn               src/core/Tasks.cpp:231
  assault_range                      nn               src/core/Tasks.cpp:232
  delivery_min                       nn               src/core/Tasks.cpp:233
  delivery_max                       nn               src/core/Tasks.cpp:234
  parcel_grace_hours                 nn               src/core/Tasks.cpp:235
  parcel                             str              src/core/Tasks.cpp:236
  escort_min                         nn               src/core/Tasks.cpp:239
  escort_max                         nn               src/core/Tasks.cpp:240
  escort_arrive                      nn               src/core/Tasks.cpp:241
  escort_hours_per_km                pos              src/core/Tasks.cpp:242
  defend_spawn_min                   nn               src/core/Tasks.cpp:244
  defend_spawn_max                   nn               src/core/Tasks.cpp:245
  defend_waves                       irange:1:10      src/core/Tasks.cpp:246
  defend_wave_size                   range:0.1:5      src/core/Tasks.cpp:247
  defend_wave_max                    irange:1:5       src/core/Tasks.cpp:248
  rescue_range                       nn               src/core/Tasks.cpp:249
  rescue_arrive                      nn               src/core/Tasks.cpp:250
  recover_min                        nn               src/core/Tasks.cpp:251
  recover_max                        nn               src/core/Tasks.cpp:252
  recover_spawn                      nn               src/core/Tasks.cpp:253
  recover_guard_chance               prob             src/core/Tasks.cpp:254
  satchel                            str              src/core/Tasks.cpp:255
  hunt_marks                         irange:3:10      src/core/Tasks.cpp:257
  hunt_min                           imin:1           src/core/Tasks.cpp:258
  rank_kill_points                   nn               src/core/Tasks.cpp:259
  rank_veteran                       nn               src/core/Tasks.cpp:260
[rewards]
  escort_late_pay                    prob             src/core/Tasks.cpp:243
  gold_mult                          mult             src/core/Tasks.cpp:266
  fetch_mult                         mult             src/core/Tasks.cpp:273
  gold_min                           count            src/core/Tasks.cpp:274
  goodwill_done                      nn               src/core/Tasks.cpp:262
  goodwill_fail                      nn               src/core/Tasks.cpp:263
  memory_done                        nn               src/core/Tasks.cpp:264
  memory_fail                        nn               src/core/Tasks.cpp:265
  bounty_base                        nn               src/core/Tasks.cpp:267
  bounty_per_level                   nn               src/core/Tasks.cpp:268
  assault_per_squad                  nn               src/core/Tasks.cpp:269
  assault_per_level                  nn               src/core/Tasks.cpp:270
  delivery_base                      nn               src/core/Tasks.cpp:271
  delivery_per_km                    nn               src/core/Tasks.cpp:272
  escort_base                        nn               src/core/Tasks.cpp:275
  escort_per_km                      nn               src/core/Tasks.cpp:276
  defend_per_wave                    nn               src/core/Tasks.cpp:277
  defend_per_level                   nn               src/core/Tasks.cpp:278
  rescue_base                        nn               src/core/Tasks.cpp:279
  rescue_per_level                   nn               src/core/Tasks.cpp:280
  recover_base                       nn               src/core/Tasks.cpp:281
  recover_per_km                     nn               src/core/Tasks.cpp:282
  hunt_base                          nn               src/core/Tasks.cpp:283
  hunt_per_level                     nn               src/core/Tasks.cpp:284

== wounded.ltx
[wounded]
  enabled                            b                src/core/Wounds.cpp:91
  hp                                 prob             src/core/Wounds.cpp:92
  chance                             prob             src/core/Wounds.cpp:93
  factions                           str              src/core/Wounds.cpp:94
  ocs_chance                         prob             src/core/Wounds.cpp:103
  down_hp                            pair:range:0.01:1 src/core/Wounds.cpp:104
  revive_hp                          range:0.05:1     src/core/Wounds.cpp:105
  call_seconds                       min:1            src/core/Wounds.cpp:106
  call_radius                        nn               src/core/Wounds.cpp:107
  help_radius                        nn               src/core/Wounds.cpp:108
  self_heal_seconds                  min:1            src/core/Wounds.cpp:109
  finish_radius                      nn               src/core/Wounds.cpp:110
  finish_delay                       pair:nn          src/core/Wounds.cpp:111
  down_hours                         pair:nn          src/core/Wounds.cpp:112
  fall_back_hours                    pair:nn          src/core/Wounds.cpp:113
  helper_rank                        pair:count       src/core/Wounds.cpp:115
[surrender]
  yield_distance                     nn               src/core/Wounds.cpp:118
  yield_chance                       prob             src/core/Wounds.cpp:119
  losing_share                       prob             src/core/Wounds.cpp:120
  outnumber                          min:1            src/core/Wounds.cpp:121
  spare_hours                        nn               src/core/Wounds.cpp:122
  release_radius                     nn               src/core/Wounds.cpp:123
  surrender_seconds                  min:1            src/core/Wounds.cpp:124
[standing]
  wound_help                         range:0:2000     src/core/Wounds.cpp:125
  wound_help_memory                  range:0:2000     src/core/Wounds.cpp:126
  rob                                range:0:2000     src/core/Wounds.cpp:127
  rob_memory                         range:0:2000     src/core/Wounds.cpp:128
  finish                             range:0:2000     src/core/Wounds.cpp:129
  spare                              range:0:2000     src/core/Wounds.cpp:130
  spare_memory                       range:0:2000     src/core/Wounds.cpp:131
  prisoner                           range:0:2000     src/core/Wounds.cpp:132
  prisoner_notoriety                 range:0:1000     src/core/Wounds.cpp:133
"""
KINDS_BY_NAME = {"f": F, "i": I, "b": B, "hex": HEX, "str": STR, "prob": PROB, "mult": MULT, "weight": WEIGHT,
                 "weight0": WEIGHT0, "count": COUNT, "nn": NN, "pos": POS, "hour": HOUR}


def parse_kind(tok):
    pair = None
    if tok.startswith(("pair:", "pairany:")):
        pair, tok = tok.split(":", 1)
    if tok in KINDS_BY_NAME:
        k = KINDS_BY_NAME[tok]
    else:
        name, *args = tok.split(":")
        t = "i" if name in ("irange", "imin") else "f"
        conv = int if t == "i" else float
        k = R(conv(args[0]), conv(args[1]), t) if name in ("range", "irange") else MIN(conv(args[0]), t)
    return k, pair


def parse_spec(text):
    spec, cur, sec = {}, None, None
    for line in text.split("\n"):
        line = line.strip()
        if not line:
            continue
        if line.startswith("== "):
            cur = spec.setdefault(line[3:], {})
        elif line.startswith("["):
            sec = cur.setdefault(line[1:-1], [])
        else:
            key, kind, cite = line.split()
            k, pair = parse_kind(kind)
            sec.append((key, k, pair, cite))
    return spec


SPEC = parse_spec(SPEC_TEXT)


def schema(rep, x, template, sec=None):
    """Check the keys of section sec (default: the template itself) against SPEC[file][template]."""
    sec = sec or template
    rows = SPEC[x.name][template]
    if not x.has(sec):
        return
    known = {r[0] for r in rows}
    for key, kind, pr, cite in rows:
        keys = FACTIONS if key == "<faction>" else [key]
        for k in keys:
            ln = x.get(sec, k)
            if ln is None:
                continue
            if pr:
                pair(rep, ln, kind, cite, ordered=pr == "pair")
            else:
                value(rep, ln, kind, cite)
    for ln in x.sec(sec):
        if ln.key not in known and not ("<faction>" in known and ln.key in FACTIONS):
            rep.warn(ln.path, ln.line, "key.unknown", "[%s] %s: the loader does not read this key" % (sec, ln.key), rows[0][3])


def num_of(rep, x, sec, key, t="f"):
    ln = x.get(sec, key)
    if ln is None:
        return None, None
    s = ln.val
    if t == "i":
        return (int(s) if INT_RE.match(s) else None), ln
    return (float(s) if FLOAT_RE.match(s) else None), ln


def not_below(rep, x, sec, lo_key, hi_key, cite, t="f"):
    """The loader clamps hi to >= lo (std::max(lo, hi))."""
    lo, _ = num_of(rep, x, sec, lo_key, t)
    hi, ln = num_of(rep, x, sec, hi_key, t)
    if lo is not None and hi is not None:
        rep.check(hi >= lo, ln.path, ln.line, "num.range", "%s = %s is below %s = %s (the loader raises it)" %
                  (hi_key, fmtn(hi), lo_key, fmtn(lo)), cite)

# ------------------------------------------------------------------------------------------------ alife.ltx


def check_alife(rep, x, all_):
    for sec in SPEC["alife.ltx"]:
        schema(rep, x, sec)
    ln = x.get("ocs", "mode")
    if ln:   # src/core/Config.cpp:262: anything but off / full silently becomes smarts
        rep.check(ln.val.lower() in ("off", "smarts", "full"), ln.path, ln.line, "ref.enum",
                  "mode = %s: not off / smarts / full" % ln.val, "src/core/Config.cpp:262")
    ln = x.get("loot", "factions")
    if ln:   # src/core/Config.cpp:278-283: "all" or faction ids; unknown ids are skipped
        for f in split_list(ln.val):
            if f.lower() != "all":
                faction(rep, ln, f, "src/core/Config.cpp:282")
    for ln in x.sec("activity_factions") or []:   # src/core/Config.cpp:479-482: unknown ids are skipped
        for f in split_list(ln.val):
            faction(rep, ln, f, "src/core/Config.cpp:482")

# ------------------------------------------------------------------------------------------------ factions.ltx
REL_KINDS = ("ally", "friend", "rival", "enemy")   # src/core/Config.cpp:27 (anything else means 0)


def check_factions(rep, x, all_):
    for ln in x.sec("relations") or []:   # src/core/Config.cpp:328-333 key = "ally: a, b | enemy: c"
        faction(rep, ln, ln.key, "src/core/Config.cpp:332")
        for part in split_list(ln.val, "|"):
            colon = part.find(":")
            if not rep.check(colon != -1, ln.path, ln.line, "ref.enum", "%s: '%s' has no 'kind:'" % (ln.key, part),
                             "src/core/Config.cpp:25"):
                continue
            kind = trim(part[:colon]).lower()
            rep.check(kind in REL_KINDS, ln.path, ln.line, "ref.enum", "%s: relation '%s' is not ally / friend / rival / enemy"
                      % (ln.key, kind), "src/core/Config.cpp:27")
            for f in split_list(part[colon + 1:]):
                faction(rep, ln, f, "src/core/Config.cpp:31")
    schema(rep, x, "dynamic_relations")
    ln = x.get("dynamic_relations", "locked_pairs")
    if ln:   # src/core/Config.cpp:342-347 a/b
        for p in split_list(ln.val):
            if rep.check("/" in p, ln.path, ln.line, "ref.faction", "locked_pairs: '%s' is not a/b" % p, "src/core/Config.cpp:344"):
                a, b = p.split("/", 1)
                faction(rep, ln, trim(a), "src/core/Config.cpp:346")
                faction(rep, ln, trim(b), "src/core/Config.cpp:346")
    for ln in x.sec("player") or []:   # src/core/Config.cpp:350-356
        faction(rep, ln, ln.key, "src/core/Config.cpp:353")
        value(rep, ln, F, "src/core/Config.cpp:355")
    for ln in x.sec("ws_faction") or []:   # src/core/Config.cpp:358-365
        rep.check(ln.key.lower() in ("tamriel", "solstheim"), ln.path, ln.line, "ref.worldspace",
                  "%s: not tamriel / solstheim" % ln.key, "src/core/Config.cpp:363")
        faction(rep, ln, ln.val, "src/core/Config.cpp:361")
    schema(rep, x, "civil_war")
    ln = x.get("civil_war", "sides")
    if ln:   # src/core/Config.cpp:368-372: exactly two factions, else the default sides
        v = split_list(ln.val)
        if rep.check(len(v) == 2, ln.path, ln.line, "num.pair", "sides = %s: needs two factions" % ln.val, "src/core/Config.cpp:368"):
            for f in v:
                faction(rep, ln, f, "src/core/Config.cpp:370")
    ln = x.get("civil_war", "guard_entries")
    props = all_.get("props.ltx")
    if ln and props:   # src/core/Config.cpp:373, respawn entry names: props.ltx [spawn_<name>] (src/core/Config.cpp:682)
        for e in split_list(ln.val):
            rep.check(props.has("spawn_" + e), ln.path, ln.line, "ref.section",
                      "guard_entries: no [spawn_%s] in props.ltx" % e, "src/core/Config.cpp:682")
    ln = x.get("civil_war", "stand_down")
    if ln:   # src/core/Config.cpp:376-379
        for w in split_list(ln.val):
            rep.check(w.lower() in ("tamriel", "solstheim"), ln.path, ln.line, "ref.worldspace",
                      "stand_down: '%s' is not tamriel / solstheim" % w, "src/core/Config.cpp:377")
    for ln in x.sec("faction_gates") or []:   # src/core/Config.cpp:381-410 Plugin|quest:stage or Plugin|global=value
        faction(rep, ln, trim(ln.key), "src/core/Config.cpp:384")
        for item in split_list(ln.val):
            bar, colon, eq = item.find("|"), item.rfind(":"), item.rfind("=")
            if bar != -1 and eq > bar:
                hexid(rep, ln, trim(item[bar + 1:eq]), "src/core/Config.cpp:394", "gate global")
                parse_num(rep, ln, "f", "src/core/Config.cpp:395", trim(item[eq + 1:]), "gate value")
            elif rep.check(bar != -1 and colon > bar, ln.path, ln.line, "num.hex",
                           "gate '%s': want Plugin|quest:stage or Plugin|global=value" % item, "src/core/Config.cpp:400"):
                hexid(rep, ln, trim(item[bar + 1:colon]), "src/core/Config.cpp:405", "gate quest")
                v = parse_num(rep, ln, "i", "src/core/Config.cpp:406", trim(item[colon + 1:]), "gate stage")
                in_range(rep, ln, v, COUNT, "src/core/Config.cpp:406", "gate stage")
    for ln in x.sec("hold_faction") or []:   # src/core/Config.cpp:412-418
        hexid(rep, ln, ln.key, "src/core/Config.cpp:417", "hold")
        faction(rep, ln, ln.val, "src/core/Config.cpp:415")
    for ln in x.sec("monsters") or []:   # src/core/Config.cpp:421-426: monster factions only, a known behaviour
        ok = ln.key in FACTIONS and ln.key not in HUMAN
        ref(rep, ln, ok, "ref.faction", "monster faction", ln.key, "src/core/Config.cpp:425")
        ref(rep, ln, ln.val.lower() in MONSTER_TYPES, "ref.enum", "monster type", ln.val, "src/core/Config.cpp:53")


def monster_factions(all_):
    """Factions with a [monsters] behaviour (Config::IsMonster, include/ALife/Config.h:277)."""
    fac = all_.get("factions.ltx")
    out = set()
    for ln in (fac.sec("monsters") if fac else None) or []:
        if ln.key in FACTIONS and ln.key not in HUMAN and ln.val.lower() in MONSTER_TYPES:
            out.add(ln.key)
    return out

# ------------------------------------------------------------------------------------------------ monsters.ltx


def check_monsters(rep, x, all_):
    for name in x.order:
        if not name.startswith("creature_"):   # src/core/Config.cpp:431
            continue
        schema(rep, x, "creature_*", name)
        ln = x.get(name, "form")
        sp, sl = x.at(name)
        if rep.check(ln is not None, sp, sl, "req.key", "[%s] has no form: the creature is dropped" % name,
                     "src/core/Config.cpp:441"):
            form = trim(ln.val)
            if "|" in form:   # src/core/Config.cpp:436-439 plugin|local id
                form = trim(form.split("|", 1)[1])
            hexid(rep, ln, form, "src/core/Config.cpp:440")
        ln = x.get(name, "parts")
        for part in split_list(ln.val) if ln else []:   # src/core/Config.cpp:447-452 <hex>:<count>
            form, _, cnt = part.partition(":")
            hexid(rep, ln, trim(form), "src/core/Config.cpp:450", "part")
            if cnt:
                v = parse_num(rep, ln, "i", "src/core/Config.cpp:451", trim(cnt), "part count")
                in_range(rep, ln, v, MIN(1, "i"), "src/core/Config.cpp:451", "part count")

# ------------------------------------------------------------------------------------------------ squads.ltx


def squad_names(all_):
    sq = all_.get("squads.ltx")
    return set(sq.order) if sq else set()


def check_squads(rep, x, all_):
    monsters = monster_factions(all_)
    mon = all_.get("monsters.ltx")
    creatures = {n[9:] for n in mon.order if n.startswith("creature_")} if mon else set()
    for name in x.order:
        sp, sl = x.at(name)
        schema(rep, x, "<squad>", name)
        ln = x.get(name, "faction")
        if not rep.check(ln is not None, sp, sl, "req.key", "[%s] has no faction: the squad is dropped" % name,
                         "src/core/Config.cpp:505"):
            continue
        if not faction(rep, ln, ln.val, "src/core/Config.cpp:505"):
            continue
        ln = x.get(name, "tier")
        if ln:   # src/core/Config.cpp:506-507: anything else is novice
            ref(rep, ln, ln.val.lower() in TIERS, "ref.enum", "tier", ln.val, "src/core/Config.cpp:507")
        ln = x.get(name, "npc_in_squad")
        if ln:   # src/core/Config.cpp:508-510: min >= 1, max >= min
            p = pair(rep, ln, MIN(1, "i"), "src/core/Config.cpp:510")
        ln = x.get(name, "level_offset")
        if ln:   # src/core/Config.cpp:511-513
            pair(rep, ln, I, "src/core/Config.cpp:513")
        ln = x.get(name, "roles")
        if ln:   # src/core/Config.cpp:517-523 role:weight; unknown roles are ignored
            tot = 0.0
            for item in split_list(ln.val):
                rn, colon, w = item.partition(":")
                rn = trim(rn).lower()
                ref(rep, ln, rn in ROLES, "ref.role", "role", rn, "src/core/Config.cpp:522")
                if colon:
                    v = parse_num(rep, ln, "f", "src/core/Config.cpp:521", trim(w), "role " + rn)
                    in_range(rep, ln, v, WEIGHT0, "src/core/Config.cpp:525", "role " + rn)
                    tot += v or 0
                else:
                    tot += 1
            rep.check(tot > 0, ln.path, ln.line, "num.weight", "roles: the weights add up to 0 (the loader makes it melee:1)",
                      "src/core/Config.cpp:526")
        fac = x.get(name, "faction").val
        ln = x.get(name, "creatures")
        if fac in monsters:   # src/core/Config.cpp:527-536: a monster squad needs creatures of monsters.ltx
            n = 0
            for item in split_list(ln.val) if ln else []:
                cid, colon, w = item.partition(":")
                cid = trim(cid).lower()
                ok = ref(rep, ln, cid in creatures, "ref.creature", "creature (no [creature_%s] in monsters.ltx)" % cid, cid,
                         "src/core/Config.cpp:530")
                wv = 1.0
                if colon:
                    wv = parse_num(rep, ln, "f", "src/core/Config.cpp:532", trim(w), "creature " + cid)
                    in_range(rep, ln, wv, WEIGHT, "src/core/Config.cpp:533", "creature " + cid)
                n += 1 if ok and wv and wv > 0 else 0
            rep.check(n > 0, (ln or Ln("", "", sp, sl)).path, (ln or Ln("", "", sp, sl)).line, "req.key",
                      "[%s]: a %s squad needs creatures (it is dropped)" % (name, fac), "src/core/Config.cpp:536")
            ln = x.get(name, "leader")
            if ln and ln.val:
                cid = trim(ln.val).lower()
                ref(rep, ln, cid in creatures, "ref.creature", "creature", cid, "src/core/Config.cpp:535")
        elif ln:
            rep.warn(ln.path, ln.line, "key.unknown", "[%s] creatures: read only for monster factions" % name,
                     "src/core/Config.cpp:527")

# ------------------------------------------------------------------------------------------------ props.ltx


def expand_squad(entry, fac):
    """src/core/Board.cpp:429 '{f}' is replaced by the spawning faction id."""
    return entry.replace("{f}", fac, 1) if "{f}" in entry else entry


def check_props(rep, x, all_):
    squads = squad_names(all_)
    presets = {"default"}
    ln_km = x.sec("kind_map") or []
    for ln in ln_km:   # src/core/Config.cpp:607 [kind_map] <kind> = preset; src/core/Config.cpp:615 a missing preset is "default"
        ref(rep, ln, ln.key in KINDS, "ref.kind", "place kind", ln.key, "src/core/Core.cpp:85")
        rep.check(ln.val == "default" or x.has(ln.val), ln.path, ln.line, "ref.section",
                  "%s = %s: no preset section [%s]" % (ln.key, ln.val, ln.val), "src/core/Config.cpp:615")
        presets.add(ln.val)
    for name in x.order:   # src/core/Config.cpp:608-614 per-location presets [loc_<hex>] / [loc_dlc2_<hex>]
        if name.startswith("loc_"):
            hx = name[9:] if name.startswith("loc_dlc2_") else name[4:]
            sp, sl = x.at(name)
            rep.check(bool(HEX_RE.match(hx)), sp, sl, "num.hex", "[%s]: not loc_<hex FormID>" % name, "src/core/Config.cpp:609")
            presets.add(name)
    for name in sorted(p for p in presets if x.has(p)):
        schema(rep, x, "<preset>", name)
        ln = x.get(name, "default_faction")
        if ln and ln.val.lower() not in ("hold", "none", ""):   # src/core/Config.cpp:638-649
            faction(rep, ln, ln.val.lower(), "src/core/Config.cpp:647")
    overlays = set()
    for ln in x.sec("keyword_props") or []:   # src/core/Config.cpp:656-661 <keyword hex> = section
        hexid(rep, ln, ln.key, "src/core/Config.cpp:660", "keyword")
        rep.check(x.has(ln.val), ln.path, ln.line, "ref.section", "%s = %s: no section [%s]" % (ln.key, ln.val, ln.val),
                  "src/core/Config.cpp:661")
        if x.has(ln.val):
            overlays.add(ln.val)
    for name in sorted(overlays):
        schema(rep, x, "<overlay>", name)
    for name in sorted(presets | overlays):   # src/core/Config.cpp:655, 674, 681-683: respawn = a, b -> [spawn_a], [spawn_b]
        if not x.has(name):
            continue
        ln = x.get(name, "respawn")
        for e in split_list(ln.val) if ln else []:
            rep.check(x.has("spawn_" + e), ln.path, ln.line, "ref.section", "[%s] respawn: no section [spawn_%s]" % (name, e),
                      "src/core/Config.cpp:683")
    for name in x.order:
        if not name.startswith("spawn_"):
            continue
        schema(rep, x, "spawn_*", name)
        ln = x.get(name, "faction")
        fac = ln.val.lower() if ln else "none"
        if ln and fac not in ("controller", "owner", "none"):   # src/core/Config.cpp:689-698
            faction(rep, ln, fac, "src/core/Config.cpp:697")
        ln = x.get(name, "squads")
        sp, sl = x.at(name)
        if not rep.check(ln is not None and split_list(ln.val), sp, sl, "req.key", "[%s] has no squads: the entry is dropped" % name,
                         "src/core/Config.cpp:700"):
            continue
        for e in split_list(ln.val):   # src/core/Board.cpp:428-430 CreateSquad(section): an unknown section makes no squad
            if "{f}" in e:
                ok = any(expand_squad(e, f) in squads for f in FACTIONS)
            else:
                ok = e in squads
            rep.check(ok, ln.path, ln.line, "ref.squad", "[%s] squads: no squad section [%s] in squads.ltx" % (name, e),
                      "src/core/Board.cpp:430")

# ------------------------------------------------------------------------------------------------ standing.ltx
STANDING_PER_FACTION = ["base", "opportunism", "territorial", "murder_weight", "lawful", "parley", "nocturnal"]


def rule_cond(tok):
    """src/core/Standing.cpp:79-116 ParseCond: (ok, what was wrong)."""
    t = trim(tok)
    if t.startswith("!"):
        t = trim(t[1:])
    if not t:
        return False, "empty condition"
    if t.startswith("race:"):
        bad = [r for r in split_list(t[5:]) if r not in PLAYER_RACES]
        return (not bad and bool(split_list(t[5:]))), ("unknown player race '%s'" % bad[0] if bad else "race: needs races")
    if t == "enforces":
        return True, ""
    for op in (">=", "<"):
        if op in t:
            name, num = trim(t[:t.find(op)]), trim(t[t.find(op) + len(op):])
            if not FLOAT_RE.match(num):
                return False, "'%s' is not a number" % num
            if name in ("level", "notoriety", "bounty"):
                return True, ""
            if name.startswith("disguise:"):
                return name[9:] in FACTIONS, "unknown faction '%s'" % name[9:]
            return False, "unknown comparison '%s'" % name
    return t in PLAYER_FLAGS, "unknown player flag '%s'" % t


def check_standing(rep, x, all_):
    for sec in SPEC["standing.ltx"]:
        schema(rep, x, sec)
    for sec in STANDING_PER_FACTION:   # src/core/Standing.cpp:119-128 FillFloats: unknown factions are skipped
        for ln in x.sec(sec) or []:
            faction(rep, ln, ln.key, "src/core/Standing.cpp:124")
            v = value(rep, ln, F, "src/core/Standing.cpp:126")
            if sec == "murder_weight":
                in_range(rep, ln, v, WEIGHT0, "src/core/Standing.cpp:145")
    # tiers ordered as RegardOf reads them (src/core/Standing.cpp:209-213)
    vals = [num_of(rep, x, "tiers", k) for k in ("hostile", "unfriendly", "friendly", "allied")]
    for (a, _), (b, ln) in zip(vals, vals[1:]):
        if a is not None and b is not None:
            rep.check(a <= b, ln.path, ln.line, "num.range", "[tiers] %s = %s is below the tier before it" % (ln.key, ln.val),
                      "src/core/Standing.cpp:209")
    for name in x.order:   # src/core/Standing.cpp:180-203 [rule.<name>]: a bad line drops the whole rule
        if not name.startswith("rule."):
            continue
        for ln in x.sec(name):
            if ln.key == "label":
                continue
            if ln.key == "when":
                for tok in split_list(ln.val, "&"):
                    ok, why = rule_cond(tok)
                    rule = "ref.race" if "race" in why else "ref.faction" if "faction" in why else "ref.flag"
                    rep.check(ok, ln.path, ln.line, rule, "[%s] when '%s': %s" % (name, trim(tok), why),
                              "src/core/Standing.cpp:193")
                continue
            faction(rep, ln, ln.key, "src/core/Standing.cpp:200")
            value(rep, ln, F, "src/core/Standing.cpp:200")

# ------------------------------------------------------------------------------------------------ races.ltx
def check_races(rep, x, all_):
    for name in x.order:   # src/core/Config.cpp:155-157 [races] = Tamriel, [races.<worldspace id>]
        sp, sl = x.at(name)
        ws_ok = name == "races" or (name.startswith("races.") and name[6:] in WORLDSPACES[1:])
        if not rep.check(ws_ok, sp, sl, "ref.worldspace", "[%s]: not [races] or [races.<%s>]" % (name, "|".join(WORLDSPACES[1:])),
                         "src/core/Config.cpp:156"):
            continue
        for ln in x.sec(name):
            faction(rep, ln, ln.key, "src/core/Config.cpp:162", human=True)
            entries = split_list(ln.val)
            rep.check(bool(entries), ln.path, ln.line, "req.key", "%s: empty table" % ln.key, "src/core/Config.cpp:148")
            seen = set()
            for e in entries:   # src/core/Config.cpp:139-146 race:weight, weight an integer > 0
                race, colon, w = e.partition(":")
                race = trim(race)
                ref(rep, ln, race in RACES, "ref.race", "race (the pool has %s)" % ", ".join(RACES), race,
                    "src/core/Config.cpp:142")
                rep.check(race not in seen, ln.path, ln.line, "dup.key", "%s: race '%s' listed twice" % (ln.key, race),
                          "src/core/Config.cpp:146")
                seen.add(race)
                if rep.check(bool(colon), ln.path, ln.line, "num.weight", "%s: '%s' has no :weight" % (ln.key, e),
                             "src/core/Config.cpp:144"):
                    v = parse_num(rep, ln, "i", "src/core/Config.cpp:144", trim(w), race)
                    in_range(rep, ln, v, V("i", 0, None, "num.weight", True, "weight: integer > 0"), "src/core/Config.cpp:145", race)

# ------------------------------------------------------------------------------------------------ difficulty.ltx


def check_difficulty(rep, x, all_):
    limits = {}
    for ch in CHANNELS:   # src/core/Difficulty.cpp:55-66 [limits] / [sliders] <channel> = min, max
        for sec in ("limits", "sliders"):
            ln = x.get(sec, ch)
            if ln is None:
                continue
            p = pair(rep, ln, NN, "src/core/Difficulty.cpp:%d" % (58 if sec == "limits" else 63))
            if p and sec == "limits":
                limits[ch] = p
    for sec in ("limits", "sliders"):
        for ln in x.sec(sec) or []:
            ref(rep, ln, ln.key in CHANNELS, "ref.channel", "difficulty channel (%s)" % ", ".join(CHANNELS), ln.key,
                "src/core/Difficulty.cpp:56")
    for row in DIFF_ROWS:   # src/core/Difficulty.cpp:53-54 one row per game difficulty, one value per channel, >= 0
        if not need_section(rep, x, row, "src/core/Difficulty.cpp:54", why=": game difficulty row (the compiled-in row is used)"):
            continue
        sp, sl = x.at(row)
        for ch in CHANNELS:
            ln = x.get(row, ch)
            if not rep.check(ln is not None, sp, sl, "req.key", "[%s] has no '%s' channel" % (row, ch), "src/core/Difficulty.cpp:54"):
                continue
            v = value(rep, ln, NN, "src/core/Difficulty.cpp:54", label="[%s] %s" % (row, ch))
            if v is not None and ch in limits:   # src/core/Difficulty.cpp:92 the multiplier is clamped to [limits]
                lo, hi = limits[ch]
                rep.check(lo <= v <= hi, ln.path, ln.line, "num.range", "[%s] %s = %s: outside [limits] %s = %s, %s" %
                          (row, ch, ln.val, ch, fmtn(lo), fmtn(hi)), "src/core/Difficulty.cpp:92")
        for ln in x.sec(row):
            ref(rep, ln, ln.key in CHANNELS, "ref.channel", "difficulty channel (%s)" % ", ".join(CHANNELS), ln.key,
                "src/core/Difficulty.cpp:54")
    schema(rep, x, "derive")
    not_below(rep, x, "derive", "level_min", "level_max", "src/core/Difficulty.cpp:71", "i")
    not_below(rep, x, "derive", "gear_min", "gear_max", "src/core/Difficulty.cpp:74", "i")
    for name in x.order:
        if name not in DIFF_ROWS + ["limits", "sliders", "derive"]:
            sp, sl = x.at(name)
            rep.warn(sp, sl, "key.unknown", "[%s]: the loader does not read this section" % name, "src/core/Difficulty.cpp:50")

# ------------------------------------------------------------------------------------------------ wounded.ltx


def check_wounded(rep, x, all_):
    need_section(rep, x, "wounded", "src/core/Wounds.cpp:86")
    for sec in SPEC["wounded.ltx"]:
        schema(rep, x, sec)
    ln = x.get("wounded", "factions")
    for f in split_list(ln.val) if ln else []:   # src/core/Wounds.cpp:96-99
        faction(rep, ln, f, "src/core/Wounds.cpp:99", human=True)
    for sec in ("finish", "surrender_chance"):   # src/core/Wounds.cpp:44-52 human faction = chance, clamped to [0, 1]
        for ln in x.sec(sec) or []:
            faction(rep, ln, ln.key, "src/core/Wounds.cpp:50", human=True)
            value(rep, ln, PROB, "src/core/Wounds.cpp:51")

# ------------------------------------------------------------------------------------------------ stashes.ltx


def check_stashes(rep, x, all_):
    need_section(rep, x, "stashes", "src/core/Stashes.cpp:61")
    for sec in ("stashes", "notes", "reward", "loot"):
        schema(rep, x, sec)
    ln = x.get("stashes", "kinds")
    for k in split_list(ln.val) if ln else []:   # src/core/Stashes.cpp:64-69
        ref(rep, ln, k in KINDS, "ref.kind", "place kind", k, "src/core/Stashes.cpp:67")
    for key, kind, cite in (("tier_levels", COUNT, "src/core/Stashes.cpp:86"), ("tier_chance", PROB, "src/core/Stashes.cpp:87"),
                            ("tier_count", NN, "src/core/Stashes.cpp:88")):
        ln = x.get("loot", key)
        if ln is None:
            continue
        v = split_list(ln.val)   # src/core/Stashes.cpp:34-41 exactly kStashTiers values
        if rep.check(len(v) == STASH_TIERS, ln.path, ln.line, "num.pair", "%s needs %d values, has %d" % (key, STASH_TIERS, len(v)),
                     "src/core/Stashes.cpp:38"):
            for i, s in enumerate(v):
                value(rep, ln, kind, cite, s, "%s[%d]" % (key, i))
    ln = x.get("loot", "kinds")
    for name in split_list(ln.val) if ln else []:   # src/core/Stashes.cpp:91-111 [kind.<name>]
        sec = "kind." + name
        if not rep.check(x.has(sec), ln.path, ln.line, "ref.section", "kinds: no section [%s]" % sec, "src/core/Stashes.cpp:93"):
            continue
        schema(rep, x, "kind.*", sec)
        for i in range(STASH_TIERS):
            tl = x.get(sec, "t%d" % (i + 1))
            for e in split_list(tl.val) if tl else []:   # src/core/Stashes.cpp:103-107 <hex>:<value >= 1>
                form, colon, v = e.partition(":")
                hexid(rep, tl, trim(form), "src/core/Stashes.cpp:107", "item")
                if colon:
                    n = parse_num(rep, tl, "i", "src/core/Stashes.cpp:106", trim(v), "item value")
                    in_range(rep, tl, n, MIN(1, "i"), "src/core/Stashes.cpp:106", "item value")
    for ln in x.sec("containers") or []:   # src/core/Stashes.cpp:112-121 default / <kind> = container hex
        if ln.key != "default":
            ref(rep, ln, ln.key in KINDS, "ref.kind", "place kind", ln.key, "src/core/Stashes.cpp:118")
        hexid(rep, ln, ln.val, "src/core/Stashes.cpp:119")

# ------------------------------------------------------------------------------------------------ companions.ltx


def check_companions(rep, x, all_):
    need_section(rep, x, "companions", "src/core/Party.cpp:25")
    for sec in SPEC["companions.ltx"]:
        schema(rep, x, sec)
    ln = x.get("companions", "paid_factions")
    for f in split_list(ln.val) if ln else []:   # src/core/Party.cpp:28-33
        faction(rep, ln, f, "src/core/Party.cpp:31", human=True)

# ------------------------------------------------------------------------------------------------ tasks.ltx
SPEC_RE = re.compile(r"^[^|]+\.es[pml]\|(0[xX])?[0-9A-Fa-f]+$")


def spec(rep, ln, s, cite, label):
    """src/core/Tasks.cpp:50-54 ParseSpec: <hex> (Skyrim.esm) or Plugin.esp|<hex> (resolved by the host)."""
    ok = bool(SPEC_RE.match(s)) or (bool(HEX_RE.match(s)) and int(s, 16) != 0)
    rep.check(ok, ln.path, ln.line, "num.hex", "%s = '%s': not <hex> or Plugin.esp|<hex>" % (label, s), cite)


def check_tasks(rep, x, all_):
    need_section(rep, x, "tasks", "src/core/Tasks.cpp:224")
    for sec in SPEC["tasks.ltx"]:
        schema(rep, x, sec)
    not_below(rep, x, "tasks", "hunt_min", "hunt_marks", "src/core/Tasks.cpp:258", "i")   # hunt_min clamped to <= hunt_marks
    for key in ("parcel", "satchel"):
        ln = x.get("tasks", key)
        if ln and ln.val:
            spec(rep, ln, ln.val, "src/core/Tasks.cpp:%d" % (237 if key == "parcel" else 256), key)
    for ln in x.sec("recover") or []:   # src/core/Tasks.cpp:287-294 corpse_<faction> = form
        if ln.key.startswith("corpse_"):
            faction(rep, ln, ln.key[7:], "src/core/Tasks.cpp:292")
            spec(rep, ln, ln.val, "src/core/Tasks.cpp:293", ln.key)
    for ln in x.sec("givers") or []:   # src/core/Tasks.cpp:297-310 faction = type:weight, ...
        faction(rep, ln, ln.key, "src/core/Tasks.cpp:301")
        for part in split_list(ln.val):
            ty, colon, w = part.partition(":")
            ty = trim(ty)
            ref(rep, ln, ty in TASK_TYPES, "ref.tasktype", "task type (%s)" % ", ".join(TASK_TYPES), ty, "src/core/Tasks.cpp:305")
            if colon:
                v = parse_num(rep, ln, "f", "src/core/Tasks.cpp:307", trim(w), ty)
                in_range(rep, ln, v, WEIGHT, "src/core/Tasks.cpp:308", "%s weight" % ty)
    for sec in x.order:   # src/core/Tasks.cpp:314-334 [fetch_<list>] name = spec, min:max[, min_level[, value]]
        if not sec.startswith("fetch_") or sec == "fetch_lists":
            continue
        for ln in x.sec(sec):
            f = split_list(ln.val)
            if not rep.check(len(f) >= 2, ln.path, ln.line, "num.pair", "[%s] %s: want spec, min:max[, min_level[, value]]" %
                             (sec, ln.key), "src/core/Tasks.cpp:319"):
                continue
            spec(rep, ln, f[0], "src/core/Tasks.cpp:323", ln.key)
            lo_s, colon, hi_s = f[1].partition(":")
            lo = parse_num(rep, ln, "i", "src/core/Tasks.cpp:326", trim(lo_s), ln.key + " min")
            hi = parse_num(rep, ln, "i", "src/core/Tasks.cpp:327", trim(hi_s), ln.key + " max") if colon else lo
            in_range(rep, ln, lo, COUNT, "src/core/Tasks.cpp:326", ln.key + " min")
            if lo is not None and hi is not None:
                rep.check(lo <= hi, ln.path, ln.line, "num.range", "%s: min %d above max %d" % (ln.key, lo, hi),
                          "src/core/Tasks.cpp:327")
            for i, what in ((2, "min_level"), (3, "value")):
                if len(f) > i:
                    v = parse_num(rep, ln, "i", "src/core/Tasks.cpp:%d" % (326 + i), f[i], "%s %s" % (ln.key, what))
                    in_range(rep, ln, v, COUNT, "src/core/Tasks.cpp:%d" % (326 + i), "%s %s" % (ln.key, what))
    for ln in x.sec("fetch_lists") or []:   # src/core/Tasks.cpp:335-344
        faction(rep, ln, ln.key, "src/core/Tasks.cpp:339")
        for name in split_list(ln.val):
            rep.check(x.has("fetch_" + name), ln.path, ln.line, "ref.section", "%s: no section [fetch_%s]" % (ln.key, name),
                      "src/core/Tasks.cpp:342")

# ------------------------------------------------------------------------------------------------ camp.ltx, camp_spots.ltx


def check_camp(rep, x, all_):
    need_section(rep, x, "jobs", "src/core/Camp.cpp:57")
    for sec in ("jobs", "time", "scene", "rest", "budget"):
        schema(rep, x, sec)
    not_below(rep, x, "scene", "idle_min", "idle_max", "src/core/Camp.cpp:74")
    for name in x.order:
        sp, sl = x.at(name)
        if name.startswith("slots_"):   # src/core/Camp.cpp:59-67 [slots_default] and [slots_<kind>]
            kind = name[6:]
            if not rep.check(kind == "default" or kind in KINDS, sp, sl, "ref.kind", "[%s]: unknown place kind '%s'" % (name, kind),
                             "src/core/Camp.cpp:61"):
                continue
            schema(rep, x, "slots_*", name)
        elif name.startswith("stories_"):   # src/core/Camp.cpp:81-84 [stories_<human faction>]
            rep.check(name[8:] in HUMAN, sp, sl, "ref.faction", "[%s]: unknown human faction '%s'" % (name, name[8:]),
                      "src/core/Camp.cpp:82")


def check_camp_spots(rep, x, all_):
    for ln in x.sec("spots") or []:   # src/core/Camp.cpp:98-104 type = hex, hex, ... (0 / bad ids are skipped)
        for i in split_list(ln.val):
            hexid(rep, ln, i, "src/core/Camp.cpp:103", ln.key)

# ------------------------------------------------------------------------------------------------ compat.ltx


def check_compat(rep, x, all_):
    schema(rep, x, "compat")
    ln = x.get("compat", "handover_owner")
    if ln:   # src/core/Compat.cpp:59: "hold" or anything else (the smart's own owner)
        ref(rep, ln, ln.val.lower() in ("hold", "owner"), "ref.enum", "handover owner (hold / owner)", ln.val, "src/core/Compat.cpp:59")
    ln = x.get("compat", "mods")
    for mid in split_list(ln.val) if ln else []:   # src/core/Compat.cpp:61-84 [compat_<id>]
        sec = "compat_" + mid
        if not rep.check(x.has(sec), ln.path, ln.line, "ref.section", "mods: no section [%s]" % sec, "src/core/Compat.cpp:63"):
            continue
    for sec in x.order:
        if not sec.startswith("compat_") or sec == "compat_factions":
            continue
        schema(rep, x, "compat_*", sec)
        hk = x.get(sec, "handover_kinds")
        for k in split_list(hk.val) if hk else []:
            ref(rep, hk, k.lower() in KINDS, "ref.kind", "place kind", k, "src/core/Compat.cpp:73")
        for key in ("handover", "keep"):   # src/core/Compat.cpp:37-45
            l2 = x.get(sec, key)
            for i in split_list(l2.val) if l2 else []:
                hexid(rep, l2, i, "src/core/Compat.cpp:41", key)
    for ln in x.sec("compat_factions") or []:   # src/core/Compat.cpp:86-94 <hex> = human faction
        hexid(rep, ln, ln.key, "src/core/Compat.cpp:89", "faction FormID")
        faction(rep, ln, ln.val.lower(), "src/core/Compat.cpp:92", human=True)

# ------------------------------------------------------------------------------------------------ dragons.ltx, dragon_places.ltx


def check_dragons(rep, x, all_):
    schema(rep, x, "dragons")
    not_below(rep, x, "dragons", "stay_min", "stay_max", "src/core/Config.cpp:552")
    not_below(rep, x, "dragons", "online_distance", "offline_distance", "src/core/Config.cpp:560")
    squads = squad_names(all_)
    for ln in x.sec("tiers") or []:   # src/core/Config.cpp:563-565 squad section = from player level
        rep.check(trim(ln.key) in squads, ln.path, ln.line, "ref.squad", "[tiers] %s: no squad section [%s] in squads.ltx" %
                  (ln.key, ln.key), "src/core/Config.cpp:565")
        value(rep, ln, COUNT, "src/core/Config.cpp:565")
    for ln in x.sec("places") or []:   # src/core/Config.cpp:567-572 location hex = weight (>= 0, src/core/Config.cpp:601)
        hexid(rep, ln, trim(ln.key), "src/core/Config.cpp:571", "location")
        value(rep, ln, WEIGHT0, "src/core/Config.cpp:601")


def check_dragon_places(rep, x, all_):
    for ln in x.sec("word_walls") or []:   # src/core/Config.cpp:575-577 location hex = word-wall count (>= 1)
        hexid(rep, ln, trim(ln.key), "src/core/Config.cpp:577", "location")
        value(rep, ln, MIN(1, "i"), "src/core/Config.cpp:577")

# ------------------------------------------------------------------------------------------------ plugin-side files
def check_positions(rep, x, all_):
    for sec in ("pos", "pos_dlc2"):   # src/Graph.cpp:87-97 [pos] / [pos_dlc2] <location hex> = x, y
        for ln in x.sec(sec) or []:
            hexid(rep, ln, ln.key, "src/Graph.cpp:89", "location")
            v = split_list(ln.val)
            if rep.check(len(v) >= 2, ln.path, ln.line, "num.pair", "%s = %s: needs x, y" % (ln.key, ln.val), "src/Graph.cpp:96"):
                for i, s in enumerate(v[:2]):
                    parse_num(rep, ln, "f", "src/Graph.cpp:97", s, "%s[%d]" % (ln.key, i))


def check_water(rep, x, all_):
    for ln in x.sec("water") or []:   # src/Graph.cpp:121-129 [water] <location hex> = level (only the key is read)
        hexid(rep, ln, ln.key, "src/Graph.cpp:127", "location")


def check_talk(rep, x, all_):
    for ln in x.sec("responses") or []:   # src/Talk.cpp:120-125 <local FormID> = action
        hexid(rep, ln, ln.key, "src/Talk.cpp:122", "response")
    for sec in ("globals", "tasks", "stashes"):   # src/Talk.cpp:118, src/Jobs.cpp:400-409, src/Stashes.cpp:204-206
        for ln in x.sec(sec) or []:
            if ln.key == "slots":
                value(rep, ln, MIN(1, "i"), "src/Jobs.cpp:404" if sec == "tasks" else "src/Stashes.cpp:206")
            else:
                hexid(rep, ln, ln.val, "src/Talk.cpp:118")
    for ln in x.sec("open") or []:   # src/Talk.cpp:126-136 <mode>.<faction> = line | line
        mode, dot, fac = ln.key.partition(".")
        if rep.check(bool(dot), ln.path, ln.line, "ref.faction", "%s: want <mode>.<faction>" % ln.key, "src/Talk.cpp:129"):
            parse_num(rep, ln, "i", "src/Talk.cpp:134", mode, ln.key + " mode")
            faction(rep, ln, fac, "src/Talk.cpp:131")

# ------------------------------------------------------------------------------------------------ main
CHECKERS = {
    "alife.ltx": check_alife, "factions.ltx": check_factions, "monsters.ltx": check_monsters,
    "squads.ltx": check_squads, "props.ltx": check_props, "standing.ltx": check_standing, "races.ltx": check_races,
    "difficulty.ltx": check_difficulty, "wounded.ltx": check_wounded, "stashes.ltx": check_stashes,
    "companions.ltx": check_companions, "tasks.ltx": check_tasks, "camp.ltx": check_camp,
    "camp_spots.ltx": check_camp_spots, "compat.ltx": check_compat, "dragons.ltx": check_dragons,
    "dragon_places.ltx": check_dragon_places, "positions.ltx": check_positions, "water.ltx": check_water,
    "talk.ltx": check_talk,
}
# src/core/Config.cpp:195-198, 209: LoadConfig fails without these four
REQUIRED = ["alife.ltx", "factions.ltx", "props.ltx", "squads.ltx"]


def run(cfg_dir, out=sys.stdout):
    rep = Report()
    if not os.path.isdir(cfg_dir):
        print("error: %s is not a directory" % cfg_dir, file=out)
        return 2, rep
    names = sorted(n for n in os.listdir(cfg_dir) if n.endswith(".ltx"))
    all_ = {n: Ltx(rep, os.path.join(cfg_dir, n)) for n in names}
    for n in REQUIRED:
        rep.check(n in all_, os.path.join(cfg_dir, n), 1, "req.section", "missing %s: LoadConfig fails" % n,
                  "src/core/Config.cpp:209")
    for n in names:
        if n in CHECKERS:
            CHECKERS[n](rep, all_[n], all_)
    rep.errors.sort(key=lambda e: (e[0], e[1]))
    rep.warnings.sort(key=lambda e: (e[0], e[1]))
    for w in rep.warnings:
        print(Report.fmt(w, "WARN "), file=out)
    for e in rep.errors:
        print(Report.fmt(e), file=out)
    if rep.errors:
        print("FAIL: %d errors in %d files, %d checks" % (len(rep.errors), len(rep.files), rep.checks), file=out)
        return 1, rep
    print("OK: %d files, %d checks" % (len(rep.files), rep.checks), file=out)
    return 0, rep


def main(argv):
    if len(argv) != 2 or argv[1] in ("-h", "--help"):
        print("usage: python3 tools/check_config.py <config dir>", file=sys.stderr)
        return 2
    return run(argv[1])[0]


if __name__ == "__main__":
    sys.exit(main(sys.argv))
