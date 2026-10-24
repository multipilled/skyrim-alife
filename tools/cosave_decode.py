#!/usr/bin/env python3
"""Decode the ALife records of an SKSE co-save (ticket G6). Python 3 standard library only.

    python3 tools/cosave_decode.py <save>.skse            each ALife record: type, version, decoded fields
    python3 tools/cosave_decode.py --full <save>.skse     ... without shortening long arrays
    python3 tools/cosave_decode.py --json <save>.skse     the decoded records as JSON

Exit 0: every ALife record decoded. Exit 1: something was skipped (unknown type or version, truncated record or file);
each case prints a "WARNING" line and the decoder goes on with the next record. Exit 2: not an SKSE co-save.

The layouts are in docs/COSAVE.md; every field below cites the C++ that writes / reads it. One layout function per record
type serves both directions: with a Decoder it reads bytes into a dict, with an Encoder it writes such a dict back to
bytes (tools/tests/test_cosave_decode.py round-trips every type and version through both).
"""
import json
import struct
import sys

# ------------------------------------------------------------------------------------------------ the SKSE container
# SKSE64 skse64/Serialization.cpp (ianpatt/skse64 @ 25b7235): Header, PluginHeader, ChunkHeader, all little-endian UInt32.
# Header.signature is MACRO_SWAP32('SKSE'), so the file starts with the bytes "SKSE"; plugin uids and chunk types are
# MSVC multi-character constants written as plain UInt32, so 'ALIF' is stored as the bytes "FILA" and 'SIM2' as "2MIS".
SKSE_SIGNATURE = b"SKSE"
SKSE_FORMAT_VERSION = 1
HEADER = struct.Struct("<4sIIII")      # signature, formatVersion, skseVersion, runtimeVersion, numPlugins
PLUGIN = struct.Struct("<III")         # signature (uid), numChunks, length (of the chunks that follow, headers included)
CHUNK = struct.Struct("<III")          # type, version, length (of the data that follows)


def fourcc(code):
    """'SIM2' -> the UInt32 MSVC gives the multi-character constant 'SIM2' (first char in the high byte)."""
    b = code.encode("ascii")
    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]


def fourcc_str(v):
    try:
        s = bytes([(v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255]).decode("ascii")
        return s if s.isprintable() else "0x%08X" % v
    except UnicodeDecodeError:
        return "0x%08X" % v


ALIFE_UID = fourcc("ALIF")             # src/Save.cpp:17 kUID, src/Save.cpp:359 SetUniqueID

# ------------------------------------------------------------------------------------------------ enums (for printing)
# src/core/Core.cpp:7-18 kFac, in enum Faction order (include/ALife/Core.h:23-33)
FACTIONS = ["bandit", "forsworn", "imperial", "stormcloak", "thalmor", "wanderer", "hunter", "mercenary", "mage",
            "necromancer", "vampire", "dawnguard", "silverhand", "vigilant", "caravan", "skaal", "redoran", "reaver",
            "wildlife", "draugr", "falmer", "troll", "spriggan", "zombie", "hound", "werebeast", "riekling", "spirit",
            "hagraven", "giant", "dwemer", "dragon"]
NO_FACTION = 255                       # include/ALife/Core.h:36 kNoFaction
TIERS = ["novice", "advanced", "veteran"]                                      # include/ALife/Core.h:42
SEXES = ["male", "female"]                                                     # include/ALife/Core.h:45
RACES = ["nord", "imperial", "breton", "redguard", "altmer", "bosmer", "dunmer", "orsimer", "argonian", "khajiit",
         "nordvampire", "imperialvampire", "bretonvampire", "dunmervampire"]  # src/core/Core.cpp:57-58
ROLES = ["melee", "heavy", "archer", "mage", "spellsword", "leader"]           # include/ALife/Core.h:69
TRUST = ["neutral", "wary", "backingoff", "hostile"]                           # include/ALife/Core.h:71
ACTS = ["none", "rest", "trade", "hunt", "fish", "raid", "clear"]              # include/ALife/Core.h:75, src/core/Core.cpp ActName
ACTIONS = ["none", "reach", "stay"]                                            # include/ALife/Core.h:189
TASK_TYPES = ["none", "bounty", "assault", "delivery", "fetch", "escort", "defend", "rescue", "recover", "hunt"]  # src/core/Tasks.cpp:15
TASK_STAGES = ["open", "ready", "done", "failed"]                              # include/ALife/Tasks.h:33
ORDERS = ["follow", "wait", "move"]                                            # include/ALife/Party.h:26
DIFF_CHANNELS = ["population", "monsters", "strength", "lethality", "economy", "mercy", "tasks", "dragons"]  # include/ALife/Difficulty.h:12
SKILL_COUNT = 18                                                               # include/ALife/Core.h:61-66 kSkillCount

# include/ALife/SaveFormat.h:10-31: SIM2 versions and the faction shift of saves before 208
SIM2_VER, SIM2_PRE_SOLSTHEIM, SIM2_MUTANTS, SIM2_PRE_MUTANTS = 208, 207, 206, 205
FACTIONS_BEFORE_SOLSTHEIM, SOLSTHEIM_FACTIONS = 15, 3


def faction_index_of(shift, f):
    """SaveFormat::FactionIndexOf: with shift (SIM2 < 208, TASK 1) a monster index >= 15 moves past the Solstheim three."""
    return f + SOLSTHEIM_FACTIONS if shift and f >= FACTIONS_BEFORE_SOLSTHEIM and f != NO_FACTION else f


class Truncated(Exception):
    pass


# ------------------------------------------------------------------------------------------------ codec
# Scalar kinds: struct format letters. bool is one byte (MSVC sizeof(bool) == 1); enums are std::uint8_t; FormIDs are u32
# (raw in the co-save: the loader resolves them with ResolveFormID); strings are a u32 length then the bytes (no NUL).
KINDS = {"u8": "B", "i8": "b", "bool": "?", "u32": "I", "i32": "i", "u64": "Q", "f32": "f", "f64": "d", "form": "I"}


class Decoder:
    def __init__(self, data):
        self.d = data
        self.o = 0
        self.root = {}
        self.cur = self.root

    def _take(self, n):
        if self.o + n > len(self.d):
            raise Truncated("needs %d bytes at offset %d, record has %d" % (n, self.o, len(self.d)))
        b = self.d[self.o:self.o + n]
        self.o += n
        return b

    def _scalar(self, kind):
        fmt = "<" + KINDS[kind]
        return struct.unpack(fmt, self._take(struct.calcsize(fmt)))[0]

    def _string(self):
        n = self._scalar("u32")
        return self._take(n).decode("utf-8", "replace")

    def remaining(self):
        return len(self.d) - self.o

    def scalar(self, name, kind):
        v = self._scalar(kind)
        self.cur[name] = v
        return v

    def string(self, name):
        v = self._string()
        self.cur[name] = v
        return v

    def array(self, name, elem, count=None, count_kind="u32"):
        """elem: a scalar kind, "str", or a function(io) filling one dict. count None: a count_kind count precedes it."""
        n = self._scalar(count_kind) if count is None else count
        out = []
        self.cur[name] = out
        for _ in range(n):
            if elem == "str":
                out.append(self._string())
            elif isinstance(elem, str):
                out.append(self._scalar(elem))
            else:
                outer, self.cur = self.cur, {}
                out.append(self.cur)
                try:
                    elem(self)
                finally:
                    self.cur = outer
        return n


class Encoder:
    def __init__(self, rec):
        self.b = bytearray()
        self.cur = rec

    def remaining(self):
        return None

    def _scalar(self, kind, v):
        self.b += struct.pack("<" + KINDS[kind], v)

    def _string(self, v):
        raw = v.encode("utf-8")
        self._scalar("u32", len(raw))
        self.b += raw

    def scalar(self, name, kind):
        v = self.cur[name]
        self._scalar(kind, v)
        return v

    def string(self, name):
        v = self.cur[name]
        self._string(v)
        return v

    def array(self, name, elem, count=None, count_kind="u32"):
        items = self.cur[name]
        if count is None:
            self._scalar(count_kind, len(items))
        elif count != len(items):
            raise ValueError("%s: %d items, layout says %d" % (name, len(items), count))
        for it in items:
            if elem == "str":
                self._string(it)
            elif isinstance(elem, str):
                self._scalar(elem, it)
            else:
                outer, self.cur = self.cur, it
                try:
                    elem(self)
                finally:
                    self.cur = outer
        return len(items)


def fields(io, spec):
    """spec: "name:kind name:kind ..." in save order; kind str = a string."""
    for f in spec.split():
        name, kind = f.split(":")
        if kind == "str":
            io.string(name)
        else:
            io.scalar(name, kind)


# ------------------------------------------------------------------------------------------------ record layouts
# SIM2: the board (src/Save.cpp:88-158 SaveCb writes, :244-337 LoadCb reads). 205-208 share one layout except the
# goodwill table (SaveFormat::ReadStanding, include/ALife/SaveFormat.h:35-52) and the creature list (206+, Save.cpp:326).
def _sim2_member(io):   # src/Save.cpp:142-151 / :306-321
    fields(io, "id:u64 name:str title:str faction:u8 role:u8 race:u8 sex:u8 classId:form level:i32 levelBias:i32 gearLevel:i32")
    io.array("skills", "f32", count=SKILL_COUNT)
    fields(io, "health:f32 magicka:f32 stamina:f32 healthMax:f32 magickaMax:f32 staminaMax:f32 gold:i32")
    io.array("inventory", lambda e: fields(e, "form:form count:i32"))
    io.array("equipped", "form")
    io.array("perks", "form")
    io.array("spells", "form")
    fields(io, "hunger:f32 cold:f32 fatigue:f32 sticky:bool veteran:bool waitlisted:bool poolLocal:i32 growthDay:u32")
    io.array("gainToday", "u8", count=SKILL_COUNT)
    fields(io, "fightBonusToday:f32 rankXp:f32 kills:i32 arrows:i32")


def _sim2_squad(io):   # src/Save.cpp:130-140 / :292-305; node fields are the node's location FormID (WNode/RNode :75-86)
    fields(io, "id:u64 faction:u8 tier:u8 section:str x:f32 y:f32 z:f32 node:form prevNode:form "
               "activity:u8 actStep:i32 actHours:f32 action:u8 assignedTarget:form currentTarget:form smartAssigned:form "
               "stayStart:f64 idleHours:f64 locked:bool lockedUntil:f64 respawnSmart:form respawnEntry:str "
               "retreatUntilHours:f64 avoidNode:form commonSquad:bool standing:i32 letGoCount:i32 foughtPlayer:bool "
               "attackedByPlayer:bool lastMetHours:f64 trust:u8 helpedByPlayer:bool startStrength:f32 startMembers:i32 "
               "memory:f32 tollUntil:f64")
    io.array("members", _sim2_member)


def layout_sim2(io, ver):
    fields(io, "now:f64 gateOpened:bool nextId:u64 worldSeed:u64")              # Save.cpp:96 / :255
    if ver >= SIM2_PRE_SOLSTHEIM:                                               # SaveFormat.h:39: 207+ write the length
        io.array("goodwill", "f32", count_kind="u8")                            # Save.cpp:99-100
    else:                                                                       # 206: 21 entries, 205: 15
        io.array("goodwill", "f32", count=21 if ver >= SIM2_MUTANTS else 15)
    io.scalar("notoriety", "f32")                                               # Save.cpp:101
    io.array("rumors", lambda e: fields(e, "faction:u8 delta:f32 dueHours:f64"))  # Save.cpp:102-103
    io.array("waitlist", "u64")                                                 # Save.cpp:105-106 / :257
    io.array("queuedKills", lambda e: fields(e, "cell:form kills:i32"))        # Save.cpp:107-108 / :258
    io.array("smarts", lambda e: (fields(e, "location:form lastRespawn:f64 faction:u8 raidedUntil:f64"),  # :111-121 / :260-277
                                  e.array("respawn", lambda r: fields(r, "name:str spawned:i32"))))
    io.array("merchants", lambda e: fields(e, "chest:form baselineGold:i32 itemsBought:i32 goldStolen:i32 cycleStart:f64"))  # :123-125 / :280-287
    io.array("squads", _sim2_squad)                                             # Save.cpp:127-153 / :290-325
    if ver >= SIM2_MUTANTS:
        io.array("creatures", "form")                                           # Save.cpp:154 / :326-330


# TASK: the jobs (src/core/Tasks.cpp:1201-1211 Save, :1213-1238 Load; one task :135-148 / :150-173), then the
# eight quest marker refs (src/Jobs.cpp:30, :497 / :505-513). 1 and 2 share a layout; 3 adds the M5e tail.
JOB_MARKERS = 8                        # src/Jobs.cpp:19,30: 2 * kMaxSlots


def _task(ver):
    def f(io):
        fields(io, "id:u32 type:u8 stage:u8 slot:i32 giver:u64 giverFaction:u8 giverName:str target:u64 targetMember:u64 "
                   "targetFaction:u8 targetName:str")
        io.array("pinned", "u64")
        fields(io, "node:u32 place:str item:form itemName:str count:i32 gold:i32 offeredAt:f64 acceptedAt:f64 "
                   "parcelMissingSince:f64")
        if ver >= 3:
            io.scalar("dest", "u32")
            io.array("marks", lambda e: fields(e, "squad:u64 member:u64"))
            fields(io, "step:i32 extra:u64 corpse:form deadline:f64 squadDesc:str")
    return f


def layout_task(io, ver):
    io.scalar("nextId", "u32")
    io.array("active", _task(ver))
    io.array("offers", lambda e: (fields(e, "giver:u64 until:f64"), _task(ver)(e)))
    io.array("cooldown", lambda e: fields(e, "giver:u64 until:f64"))
    # the loader reads the markers leniently (missing ones are 0, src/Jobs.cpp:507-513), so decode what is there
    rem = io.remaining()
    n = JOB_MARKERS if rem is None or rem >= 4 * JOB_MARKERS else rem // 4
    io.array("markers", "form", count=n if rem is not None else len(io.cur["markers"]))


# COMP: companion squads (src/core/Party.cpp:261-272 Save, :274-296 Load; src/Companions.cpp:526-547)
def layout_comp(io, ver):
    io.array("companions", lambda e: fields(e, "squad:u64 name:str paid:bool joinedAt:f64 until:f64 warned:bool order:u8 "
                                               "holdFire:bool loot:bool cell:form ws:form wx:f32 wy:f32 wz:f32"))
    io.array("rolls", lambda e: fields(e, "squad:u64 willing:bool factor:f32"))


# WOND: down members and surrender rolls (src/core/Wounds.cpp:590-599 Save, :601-621 Load; src/Wounded.cpp:545-560)
def layout_wond(io, ver):
    io.array("down", lambda e: fields(e, "member:u64 squad:u64 since:f64 upAt:f64 healthBefore:f32 flags:u8"))
    io.array("fights", lambda e: fields(e, "squad:u64 rolled:bool yielded:bool"))


# STSH: stash spots (src/core/Stashes.cpp:396-408 Save, :410-439 Load), then 32 marker refs (src/Stashes.cpp:18,24,295 / :304-307)
STASH_MARKERS = 32


def layout_stsh(io, ver):
    io.array("spots", lambda e: (fields(e, "id:u32 node:u32 ws:u8 x:f32 y:f32 z:f32 base:form state:u8 rolled:bool slot:i8 "
                                           "ref:form markedAt:f64"),
                                 e.array("items", lambda i: fields(i, "form:form count:i32"))))
    fields(io, "found:u64 notesRead:u64 revealed:u64 rewards:u64")
    io.array("markers", "form", count=STASH_MARKERS)


# NEWS: News page lines and the live relation matrix (src/Save.cpp:163-174 / :229-243)
def layout_news(io, ver):
    io.array("lines", "str")
    nf = io.scalar("factions", "u32")
    io.array("relations", "i32", count=nf * nf)


# DRGN: dragon respawn timer and the player's dragon kills (src/Save.cpp:175-180 / :217-226)
def layout_drgn(io, ver):
    fields(io, "dragonLast:f64 dragonKills:u32")


# DIFF: difficulty sliders (src/Save.cpp:181-188 / :199-214)
def layout_diff(io, ver):
    io.scalar("followGame", "u8")
    io.array("sliders", "f32", count_kind="u8")


# type -> (versions the current loader accepts, layout). Order = the order SaveCb writes them (src/Save.cpp:155-188).
RECORDS = {
    "SIM2": ((205, 206, 207, 208), layout_sim2),   # SaveFormat::Loads, include/ALife/SaveFormat.h:18
    "TASK": ((1, 2, 3), layout_task),              # Tasks::Load, src/core/Tasks.cpp:1216
    "COMP": ((1,), layout_comp),                   # Party::Load, src/core/Party.cpp:277
    "WOND": ((1,), layout_wond),                   # Wounds::Load, src/core/Wounds.cpp:604
    "STSH": ((1,), layout_stsh),                   # Stashes::Load, src/core/Stashes.cpp:414
    "NEWS": ((1,), layout_news),                   # src/Save.cpp:22,229
    "DRGN": ((1,), layout_drgn),                   # src/Save.cpp:24,219
    "DIFF": ((1,), layout_diff),                   # src/Save.cpp:26,199
}
CURRENT = {"SIM2": 208, "TASK": 3, "COMP": 1, "WOND": 1, "STSH": 1, "NEWS": 1, "DRGN": 1, "DIFF": 1}


def decode_record(code, ver, data):
    """-> (fields dict, warning or None). Unknown type / version: (None, warning). Truncated: (partial dict, warning)."""
    spec = RECORDS.get(code)
    if spec is None:
        return None, "unknown record type %s (v%d, %d bytes): skipped" % (code, ver, len(data))
    if ver not in spec[0]:
        return None, "record %s v%d is not a version this decoder knows (%s): skipped" % (
            code, ver, ", ".join(str(v) for v in spec[0]))
    d = Decoder(data)
    try:
        spec[1](d, ver)
    except Truncated as e:
        return d.root, "record %s v%d truncated (%s): fields after that are missing" % (code, ver, e)
    if d.remaining():
        return d.root, "record %s v%d has %d trailing bytes the layout does not explain" % (code, ver, d.remaining())
    return d.root, None


def encode_record(code, ver, rec):
    e = Encoder(rec)
    RECORDS[code][1](e, ver)
    return bytes(e.b)


# ------------------------------------------------------------------------------------------------ container io
def build_cosave(plugins, skse_version=0x02020060, runtime_version=0x01061470):
    """plugins: [(uid, [(type, version, data bytes), ...]), ...] -> bytes of a .skse file (the layout SKSE writes)."""
    out = bytearray(HEADER.pack(SKSE_SIGNATURE, SKSE_FORMAT_VERSION, skse_version, runtime_version, len(plugins)))
    for uid, chunks in plugins:
        body = bytearray()
        for typ, ver, data in chunks:
            body += CHUNK.pack(typ, ver, len(data)) + data
        out += PLUGIN.pack(uid, len(chunks), len(body)) + body
    return bytes(out)


def read_cosave(data):
    """-> (header dict, [plugin dict with chunks], warnings). Raises ValueError when it is not a co-save."""
    warnings = []
    if len(data) < HEADER.size:
        raise ValueError("file is %d bytes, an SKSE co-save header is %d" % (len(data), HEADER.size))
    sig, fmt, skse, runtime, nplugins = HEADER.unpack_from(data, 0)
    if sig != SKSE_SIGNATURE:
        raise ValueError("bad signature %r (expected %r): not an SKSE co-save" % (sig, SKSE_SIGNATURE))
    if fmt != SKSE_FORMAT_VERSION:
        warnings.append("co-save format version %d (SKSE writes %d); reading it as %d" % (fmt, SKSE_FORMAT_VERSION, SKSE_FORMAT_VERSION))
    header = {"formatVersion": fmt, "skseVersion": skse, "runtimeVersion": runtime, "numPlugins": nplugins}
    plugins = []
    o = HEADER.size
    # SKSE's loader walks plugin headers while >= 12 bytes remain (Serialization.cpp HandleLoadGame), not numPlugins
    while len(data) - o >= PLUGIN.size:
        uid, nchunks, length = PLUGIN.unpack_from(data, o)
        o += PLUGIN.size
        end = o + length
        if end > len(data):
            warnings.append("plugin %s: %d bytes announced, %d left in the file (truncated)" % (fourcc_str(uid), length, len(data) - o))
            end = len(data)
        chunks = []
        p = o
        for k in range(nchunks):
            if end - p < CHUNK.size:
                warnings.append("plugin %s: chunk %d of %d missing (truncated)" % (fourcc_str(uid), k + 1, nchunks))
                break
            typ, ver, clen = CHUNK.unpack_from(data, p)
            p += CHUNK.size
            body = data[p:p + clen]
            if len(body) < clen or p + clen > end:
                body = data[p:min(p + clen, end)]
                warnings.append("plugin %s: chunk %s v%d announces %d bytes, %d present (truncated)" % (
                    fourcc_str(uid), fourcc_str(typ), ver, clen, len(body)))
            chunks.append({"type": fourcc_str(typ), "version": ver, "length": clen, "data": body})
            p += len(body)
        plugins.append({"uid": uid, "name": fourcc_str(uid), "chunks": chunks})
        o = end
    if len(plugins) != nplugins:
        warnings.append("header says %d plugins, found %d" % (nplugins, len(plugins)))
    return header, plugins, warnings


# ------------------------------------------------------------------------------------------------ printing
ENUMS = {"tier": TIERS, "sex": SEXES, "race": RACES, "role": ROLES, "trust": TRUST, "activity": ACTS, "action": ACTIONS,
         "order": ORDERS}
FACTION_FIELDS = {"faction", "giverFaction", "targetFaction"}
SUMMARY = 6                            # array items printed before "... (N more)"


def _faction(v, shift):
    if v == NO_FACTION:
        return "%d (none)" % v
    to = faction_index_of(shift, v)
    name = FACTIONS[to] if to < len(FACTIONS) else "?"
    return "%d (%s%s)" % (v, name, ", now %d" % to if to != v else "")


def _value(code, key, v, shift):
    if isinstance(v, float):
        return "%g" % v
    if key in FACTION_FIELDS:
        return _faction(v, shift)
    if code == "TASK" and key == "type":
        return "%d (%s)" % (v, TASK_TYPES[v] if v < len(TASK_TYPES) else "?")
    if code == "TASK" and key == "stage":
        return "%d (%s)" % (v, TASK_STAGES[v] if v < len(TASK_STAGES) else "?")
    if key in ENUMS:
        t = ENUMS[key]
        return "%d (%s)" % (v, t[v] if 0 <= v < len(t) else "?")
    if isinstance(v, str):
        return json.dumps(v, ensure_ascii=False)
    if isinstance(v, bool):
        return "true" if v else "false"
    if code == "WOND" and key == "flags":
        names = [n for bit, n in ((1, "yielded"), (2, "offline"), (4, "called")) if v & bit]
        return "%d (%s)" % (v, "|".join(names) or "-")
    if key in ("cell", "location", "chest", "classId", "form", "item", "corpse", "base", "ref") or (key == "ws" and code == "COMP") \
            or code == "SIM2" and (key.endswith("Node") or key in ("node", "assignedTarget", "currentTarget", "smartAssigned", "respawnSmart")):
        return "%08X" % v
    return str(v)


def _print(code, key, v, depth, full, shift, out, labels=None):
    pad = "  " * depth
    if isinstance(v, dict):
        out.append(pad + key + ":")
        for k, x in v.items():
            _print(code, k, x, depth + 1, full, shift, out)
        return
    if isinstance(v, list):
        n = len(v)
        shown = v if full else v[:SUMMARY]
        if all(not isinstance(x, (dict, list)) for x in v):
            if labels:
                items = ["%s=%s" % (labels[i] if i < len(labels) else "#%d" % i, _value(code, key, x, shift)) for i, x in enumerate(shown)]
            elif key == "goodwill":
                items = ["%s=%g" % (FACTIONS[faction_index_of(shift, i)] if faction_index_of(shift, i) < len(FACTIONS) else "#%d" % i, x)
                         for i, x in enumerate(shown)]
            elif key in ("markers", "creatures", "equipped", "perks", "spells"):
                items = ["%08X" % x for x in shown]
            else:
                items = [_value(code, key, x, shift) for x in shown]
            more = "" if len(shown) == n else ", ... (%d more)" % (n - len(shown))
            out.append("%s%s [%d]%s%s" % (pad, key, n, ": " + ", ".join(items) if items else "", more))
            return
        out.append("%s%s [%d]:" % (pad, key, n))
        for i, x in enumerate(shown):
            _print(code, "#%d" % i, x, depth + 1, full, shift, out)
        if len(shown) != n:
            out.append("%s  ... (%d more)" % (pad, n - len(shown)))
        return
    out.append("%s%s = %s" % (pad, key, _value(code, key, v, shift)))


def format_record(code, ver, rec, full=False):
    shift = (code == "SIM2" and ver < SIM2_VER) or (code == "TASK" and ver == 1)
    out = []
    for k, v in rec.items():
        if code == "DIFF" and k == "sliders":
            _print(code, k, v, 1, True, shift, out, labels=DIFF_CHANNELS)
        elif code == "NEWS" and k == "relations" and not full:
            out.append("  relations [%d]: %d x %d matrix (--full prints it)" % (len(v), rec.get("factions", 0), rec.get("factions", 0)))
        else:
            _print(code, k, v, 1, full, shift, out)
    return out


def decode_file(data):
    """-> (header, [(plugin name, type, version, length, fields or None)], warnings). Raises ValueError for a non co-save."""
    header, plugins, warnings = read_cosave(data)
    records = []
    for p in plugins:
        if p["uid"] != ALIFE_UID:
            continue
        for c in p["chunks"]:
            rec, w = decode_record(c["type"], c["version"], c["data"])
            if w:
                warnings.append(w)
            records.append((c["type"], c["version"], c["length"], rec))
    return header, plugins, records, warnings


def main(argv):
    full = "--full" in argv
    as_json = "--json" in argv
    args = [a for a in argv if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        print("usage: cosave_decode.py [--full] [--json] <file.skse>", file=sys.stderr)
        return 2
    try:
        with open(args[0], "rb") as f:
            data = f.read()
        header, plugins, records, warnings = decode_file(data)
    except (OSError, ValueError) as e:
        print("ERROR: %s" % e)
        return 2
    if as_json:
        print(json.dumps({"header": header, "plugins": [{"uid": p["name"], "chunks": len(p["chunks"])} for p in plugins],
                          "records": [{"type": t, "version": v, "length": n, "fields": r} for t, v, n, r in records],
                          "warnings": warnings}, indent=1, ensure_ascii=False))
        return 1 if warnings else 0
    print("SKSE co-save: format %d, skse %08X, runtime %08X, %d plugin(s)" % (
        header["formatVersion"], header["skseVersion"], header["runtimeVersion"], header["numPlugins"]))
    for p in plugins:
        tag = "ALife" if p["uid"] == ALIFE_UID else "other plugin, skipped"
        print("plugin %s (%s): %d record(s)" % (p["name"], tag, len(p["chunks"])))
    if not any(p["uid"] == ALIFE_UID for p in plugins):
        print("no ALife data (plugin ALIF): the save was made without ALife or before its board was ready")
    for t, v, n, rec in records:
        print("== %s v%d (%d bytes)%s" % (t, v, n, "" if rec is not None else ": skipped"))
        if rec is not None:
            for line in format_record(t, v, rec, full):
                print(line)
    for w in warnings:
        print("WARNING: " + w)
    return 1 if warnings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
