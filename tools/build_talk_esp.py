"""Builds ALifeTalk.esp (ESL-flagged) and config/talk.ltx: the dialogue of A-Life squads.

Every conversation with a squad is real Skyrim dialogue: the squad's opening line (a HELO "Hello" topic) and the player's choices
(top-level topics) are selected by conditions on the speaker's faction and on a global the plugin sets for the squad the player is
looking at (ALifeTalkMode: 1 toll, 2 challenge, 3 greeting). The plugin learns what the player chose from the TESTopicInfoEvent of
the chosen response (talk.ltx maps each response FormID to an action), so no Papyrus fragments are needed.

The topics belong to the vanilla "Generic dialogue" quest (DialogueGeneric, Skyrim.esm 0x13EB3), the way the Dawnguard and Dragonborn
dialogue does: the engine only offers dialogue from the quests it already treats as generic dialogue, and a quest of our own is never
consulted for a squad member (verified in the game: our own quest's Hello and topics were skipped, even with no conditions at all, while
the same records owned by DialogueGeneric were spoken and offered at once). Every record is conditioned on our own globals and factions,
so no other NPC is ever affected.

All text is original. Layout follows vanilla Skyrim.esm records (DLBR / DIAL / INFO, form version 44).

usage: python build_talk_esp.py [out.esp] [out_talk.ltx]
"""
import struct, sys, os

FACTIONS = ["bandit", "forsworn", "imperial", "stormcloak", "thalmor", "wanderer", "hunter", "mercenary", "mage",
            "necromancer", "vampire", "dawnguard", "silverhand", "vigilant", "caravan"]   # must match enum Faction order in Core.h
MASTERS = ["Skyrim.esm", "Update.esm", "Dawnguard.esm", "ALifeFactions.esp"]
OWN = len(MASTERS) << 24
FACTIONS_MASTER = 3 << 24
FACTION_BASE = 0x800                      # ALifeFactions.esp: FormID 0x800 + faction index

F_GETGLOBAL, F_GETINFACTION = 74, 71      # condition function indices
GLOBAL_MODE, GLOBAL_TOLL = 0x800, 0x801   # local IDs of the globals
GENERIC_DIALOGUE = 0x00013EB3                # DialogueGeneric (Skyrim.esm), the quest that owns every record below
HELLO = 0x820                             # the one Hello topic
BRANCH0, TOPIC0 = 0x830, 0x850            # branches / player topics
INFO0 = 0x900                             # first response FormID

M_TOLL, M_CHAL, M_GREET = 1, 2, 3

# --- jobs (docs/TASKS.md): two more globals, the sealed parcel, the ALifeTasks quest (journal + markers), the job topics
GLOBAL_OFFER, GLOBAL_STATE = 0x802, 0x803   # ALifeTaskOffer (0 none, 1 bounty, 2 assault, 3 delivery, 4 fetch), ALifeTaskState
PARCEL, TASK_QUEST = 0x805, 0x806
TASK_SLOTS = 4                            # objectives 10*s (work, alias Target<s>) and 10*s+1 (return, alias Giver<s>)
TBRANCH0, TTOPIC0, TINFO0 = 0x840, 0x860, 0xA00
OP_GT = 0x40
JOB_PITCH = {   # what a leader says when asked for work, by offer type; the specifics go into the journal
    1: ["There's someone out there I want dead. Do it and the coin is yours.", "I've a name for you. Find them, end them, come back for your pay."],
    2: ["Enemies have dug in not far from here. Clear them out and you'll be paid.", "There's a nest of trouble nearby. Burn it out and come back to me."],
    3: ["I need a parcel carried to someone. Sealed, and it stays that way.", "Take this to a friend of ours. Don't open it, and don't lose it."],
    4: ["We're short on supplies. Bring me what I need and I'll pay well.", "I've a list of things we need. Fill it and you won't regret it."],
}
# (key, prompt, ALifeTaskState it needs (None: a child of "Got any work?"), answers, action in talk.ltx)
JOB_TOPICS = [
    ("accept", "I'll do it.", None, ["Good. I'll mark it on your map.", "Then we have a deal. Don't keep me waiting."], "accept"),
    ("decline", "Not right now.", None, ["Suit yourself. The offer stands, for a while."], "decline"),
    ("turnin", "About that job. It's done.", 2, ["Good work. Here, as promised.", "So it's done. Here's your pay."], "turnin"),
    ("deliver", "I have a delivery for you.", 3, ["About time. Here, for your trouble.", "Ah, the parcel. My thanks, and your fee."], "deliver"),
    ("abandon", "I can't finish that job.", 1, ["Then I'll find someone who can.", "Pity. I thought you were up to it."], "abandon"),
]

# ------------------------------------------------------------------------------------------------ content
# Opening lines: what the squad says when the player talks to it (also what it says aloud when it stops the player).
OPEN = {
    (M_TOLL, "bandit"): ["Well? Let's see the coin.", "Don't make this hard. A little gold and you walk on."],
    (M_CHAL, "forsworn"): ["Outsider. State why you walk the Reach.", "You are far from any road you know. Speak."],
    (M_CHAL, "necromancer"): ["You trespass on work you cannot grasp. Speak, before I lose patience."],
    (M_CHAL, "vampire"): ["Few come here willingly. What do you want?"],
    (M_CHAL, "silverhand"): ["Show me your hands. Any sign of the beast on you?"],
    (M_CHAL, "imperial"): ["You there. State your business in these lands.", "Halt. Where are you headed?"],
    (M_CHAL, "stormcloak"): ["Stop. What brings you through Stormcloak lands?", "You. Name and business."],
    (M_CHAL, "thalmor"): ["Your papers. And be brief.", "The Thalmor do not ask twice. Your business?"],
    (M_CHAL, "dawnguard"): ["Easy. You're not one of the bloodsuckers, are you?"],
    (M_CHAL, "vigilant"): ["Stendarr watches. Have you cause to fear his judgment?"],
    (M_GREET, "bandit"): ["You again. Keep walking."],
    (M_GREET, "forsworn"): ["You walk quietly. The Reach tolerates that."],
    (M_GREET, "imperial"): ["Good road to you, traveller.", "Quiet times, thanks to the Legion. What can I do for you?"],
    (M_GREET, "stormcloak"): ["Talos guide you, friend.", "A good day for Skyrim. What do you need?"],
    (M_GREET, "thalmor"): ["Yes? Be quick."],
    (M_GREET, "wanderer"): ["Well met on the road! What can I do for you?", "Another traveller. Sit a moment if you like."],
    (M_GREET, "hunter"): ["Good hunting to you. What is it?", "Keep your voice low. The game spooks easily."],
    (M_GREET, "mercenary"): ["Safe roads, if you can pay for them. What do you need?"],
    (M_GREET, "mage"): ["Magicka favours the curious. Yes?"],
    (M_GREET, "necromancer"): ["You may speak. Do not mistake my patience."],
    (M_GREET, "vampire"): ["Hmm. A friend. What do you want?"],
    (M_GREET, "dawnguard"): ["The Dawnguard welcomes a friend. What do you need?"],
    (M_GREET, "silverhand"): ["Hunter's luck. What is it?"],
    (M_GREET, "vigilant"): ["May Stendarr's mercy keep you. How can I help?"],
    (M_GREET, "caravan"): ["Khajiit has wares, if you have coin.", "Welcome, friend. What does Khajiit's friend need?", "Good road, traveller. Look, look: Khajiit has much to sell."],
}

# Player topics. mode -> list of (key, prompt, action, goodbye)
TOPICS = {
    M_TOLL: [("pay", "Pay the toll.", "pay", True),
             ("poor", "I don't have that much.", "poor", True),
             ("refuse", "I'm not paying.", "refuse", True),
             ("fight", "Come and take it, then.", "fight", True)],
    M_CHAL: [("peace", "I mean no harm. I'm only passing through.", "peace", True),
             ("defy", "I don't answer to you.", "defy", True)],
    M_GREET: [("trade", "Let me see what you have for sale.", "trade", True),
              ("news", "What's the word on the road?", "news", False)],
}

# NPC answers: (mode, topic key, faction) -> lines
ANSWER = {
    (M_TOLL, "pay", "bandit"): ["Sensible. On your way.", "Pleasure doing business. Mind the road."],
    (M_TOLL, "refuse", "bandit"): ["Then we do this the hard way.", "Your mistake, friend."],
    (M_TOLL, "fight", "bandit"): ["Gladly!", "Big talk. Let's see it."],
    (M_TOLL, "poor", "bandit"): ["Hah. Empty pockets. Not worth the trouble. Go.", "Then you're no use to us. Get out of my sight."],
}
TRADE = {   # what a squad says before it opens its pack (the barter menu follows the conversation)
    "caravan": ["Khajiit has skooma, moon sugar, potions... all for a fair price, friend.", "Look, look. Khajiit's wares are the best on the road."],
    "wanderer": ["Some spare things, if the price is right.", "I can part with a few items. Take a look."],
    "hunter": ["Pelts, meat, a few arrows. Plain goods, honestly priced.", "Whatever the hunt gave me. Have a look."],
    "mercenary": ["Loot is loot. Make me an offer.", "I carry what the contracts leave me. Look if you like."],
    "mage": ["A few potions and scrolls, if you need them.", "Reagents and trinkets. Nothing dangerous."],
    "dawnguard": ["Spare supplies, yes. We can part with some.", "Potions and bolts for the cause. Take a look."],
    "bandit": ["Hah. Take a look. All of it 'liberated'.", "Cheap, if you ask no questions."],
    "silverhand": ["Spare supplies from the hunt. Have a look."],
}
for f_, lines_ in TRADE.items():
    ANSWER[(M_GREET, "trade", f_)] = lines_
LAWFUL_PEACE = ["Then keep that steel sheathed and keep moving.", "See that you do. We'll be watching."]
LAWFUL_DEFY = ["Then you'll answer to the law.", "That was a mistake."]
WILD_PEACE = ["Then go. And do not come back this way.", "Quickly, then. Before I change my mind."]
WILD_DEFY = ["Then you have chosen.", "Fool. You will not leave."]
for f in ("imperial", "stormcloak", "thalmor", "dawnguard", "vigilant"):
    ANSWER[(M_CHAL, "peace", f)] = LAWFUL_PEACE
    ANSWER[(M_CHAL, "defy", f)] = LAWFUL_DEFY
for f in ("forsworn", "necromancer", "vampire", "silverhand"):
    ANSWER[(M_CHAL, "peace", f)] = WILD_PEACE
    ANSWER[(M_CHAL, "defy", f)] = WILD_DEFY
ANSWER[(M_CHAL, "peace", "thalmor")] = ["Then be on your way. And do not linger near the Embassy.", "See that you remain so."]

RUMORS = {   # general road news by faction flavour; the live news feed is in the MCM
    "imperial": ["Bandits keep the old forts, but the Legion clears them one by one.", "The Stormcloaks are restless. Keep to the main roads."],
    "stormcloak": ["Imperial patrols are thick on the roads. Watch for them.", "Ulfric's men hold the north, and the north holds."],
    "thalmor": ["Some people talk too freely. It is rarely wise."],
    "wanderer": ["I hear the old barrows are stirring again. I'd stay clear.", "Bandits at the crossroads lately. Keep a few coins handy.", "Heard a patrol went missing near the ruins. Nobody went to look."],
    "hunter": ["Wolves have come down from the hills early. The elk are skittish.", "There's a bear den near the river. Don't linger."],
    "mercenary": ["Work's plentiful if you don't mind the dirt. Everyone wants someone cleared out.", "A band of sellswords went up to the barrow last week. Only one came back."],
    "mage": ["The College says the ruins are restless. Take care if you go digging.", "Old magic in the old places. It does not like being disturbed."],
    "necromancer": ["The dead are patient. The living are not. Remember that."],
    "vampire": ["Night is long, friend. The roads are safer in daylight, for you."],
    "dawnguard": ["More lairs than we can count. We clear what we can.", "Vampires hunt closer to the roads than they used to."],
    "silverhand": ["Beasts walk among the Companions. One day we will prove it."],
    "vigilant": ["Evil stirs in the barrows. Stendarr's light must be carried there.", "Daedra worshippers hide in plain sight. Trust no stranger's smile."],
    "caravan": ["The roads are not kind to Khajiit. Keep a blade close, friend.", "Khajiit hears many things between the holds. Bandits on the old forts again.", "Moon sugar for the cold nights, friend. Khajiit has seen many winters."],
    "bandit": ["Walk on. I've nothing to say to you."],
    "forsworn": ["Outsiders carve the Reach into pieces. We remember."],
}


# ------------------------------------------------------------------------------------------------ binary helpers
def sub(sig, data): return sig.encode() + struct.pack("<H", len(data)) + data
def zstr(s): return s.encode("latin1") + b"\0"
def u32(v): return struct.pack("<I", v)
def own(local): return OWN | local
def fac_fid(name): return FACTIONS_MASTER | (FACTION_BASE + FACTIONS.index(name))


def record(sig, fid, subs, flags=0):
    body = b"".join(subs)
    return sig.encode() + struct.pack("<IIIIHH", len(body), flags, fid, 0, 44, 0) + body


def group(label, gtype, body):
    lbl = label.encode() if isinstance(label, str) else struct.pack("<I", label)
    return b"GRUP" + struct.pack("<I", 24 + len(body)) + lbl + struct.pack("<iHHHH", gtype, 0, 0, 0, 0) + body


OP_EQ, OP_GE, OP_LT = 0x00, 0x60, 0x80       # CTDA type byte, compare operator bits
USE_GLOBAL = 0x04                            # the comparison value is a GLOB FormID
PLAYER_REF = 0x14


def ctda(func, param1, value=1.0, op=OP_EQ, global_value=0, run_on=0, reference=0):
    """One condition: `func(param1)` compared (op) with `value`, or with a global's value; run on the Subject, or on `reference`."""
    typ = op | (USE_GLOBAL if global_value else 0)
    raw = struct.pack("<I", global_value) if global_value else struct.pack("<f", value)
    return sub("CTDA", struct.pack("<B3s4sH2sIIIII", typ, b"\0\0\0", raw, func, b"\0\0", param1, 0, run_on, reference, 0xFFFFFFFF))


def trdt(resp_no=1):
    # emotion neutral(0) value 50, unused, response number, sound 0, use-emotion-animation 1
    return sub("TRDT", struct.pack("<IIIB3sIB3s", 0, 50, 0, resp_no, b"\0\0\0", 0, 1, b"\0\0\0"))


def info(fid, text, conds, goodbye=False, random=False, choices=()):
    flags = (1 if goodbye else 0) | (2 if random else 0)
    subs = [sub("ENAM", struct.pack("<HH", flags, 0)), sub("CNAM", b"\0")]
    subs += [sub("TCLT", u32(c)) for c in choices]   # follow-up topics offered after this line (vanilla order: ENAM, CNAM, TCLT, TRDT)
    subs += [trdt(1), sub("NAM1", zstr(text)), sub("NAM2", b"\0"), sub("NAM3", b"\0")]
    subs += conds
    return record("INFO", fid, subs)


F_GETITEMCOUNT, GOLD = 47, 0x0F


def conds_for(mode, faction, gold=None):
    """Mode + speaker faction; gold='enough' / 'short' also requires the player to hold at least / less than the toll in gold."""
    c = [ctda(F_GETGLOBAL, own(GLOBAL_MODE), float(mode)), ctda(F_GETINFACTION, fac_fid(faction), 1.0)]
    if gold == "enough":
        c.append(ctda(F_GETITEMCOUNT, GOLD, op=OP_GE, global_value=own(GLOBAL_TOLL), run_on=2, reference=PLAYER_REF))
    elif gold == "short":
        c.append(ctda(F_GETITEMCOUNT, GOLD, op=OP_LT, global_value=own(GLOBAL_TOLL), run_on=2, reference=PLAYER_REF))
    return c


def build(out_esp, out_ltx):
    next_info = [INFO0]
    talk_map = []        # (info local id, action)
    opening = []         # (mode, faction, line)

    def new_info():
        v = next_info[0]
        next_info[0] += 1
        return v

    # --- globals
    def glob(local, edid, value):
        return record("GLOB", own(local), [sub("EDID", zstr(edid)), sub("FNAM", b"f"), sub("FLTV", struct.pack("<f", value))])
    globs = [glob(GLOBAL_MODE, "ALifeTalkMode", 0.0), glob(GLOBAL_TOLL, "ALifeTollGold", 0.0)]

    # --- Hello topic: the squad's opening line
    hello_infos = b""
    count = 0
    for (mode, faction), lines in OPEN.items():
        for line in lines:
            fid = new_info()
            hello_infos += info(own(fid), line, conds_for(mode, faction), goodbye=False, random=len(lines) > 1)
            opening.append((mode, faction, line))
            count += 1
    hello = record("DIAL", own(HELLO), [sub("EDID", zstr("ALifeTalkHello")), sub("PNAM", struct.pack("<f", 100.0)), sub("QNAM", u32(GENERIC_DIALOGUE)),
                                        sub("DATA", bytes([0, 7, 0x4F, 0])), sub("SNAM", b"HELO"), sub("TIFC", u32(count))])
    dial_blob = hello + group(own(HELLO), 7, hello_infos)

    # --- player topics (each is the start topic of its own top-level branch)
    branches = []
    ti = 0
    for mode, topics in TOPICS.items():
        for key, prompt, action, goodbye in topics:
            topic_fid, branch_fid = own(TOPIC0 + ti), own(BRANCH0 + ti)
            infos_blob, n = b"", 0
            for faction in FACTIONS:
                lines = ANSWER.get((mode, key, faction)) or (RUMORS.get(faction) if (mode == M_GREET and key == "news") else None)
                if not lines: continue
                # one INFO per line so the engine can vary them (random flag)
                for line in lines:
                    fid = new_info()
                    gold = "enough" if key == "pay" else ("short" if key == "poor" else None)
                    infos_blob += info(own(fid), line, conds_for(mode, faction, gold), goodbye=goodbye, random=len(lines) > 1)
                    talk_map.append((fid, action, faction, mode))
                    n += 1
            if not n: ti += 1; continue
            branches.append(record("DLBR", branch_fid, [sub("EDID", zstr(f"ALifeTalkBranch_{key}")), sub("QNAM", u32(GENERIC_DIALOGUE)), sub("TNAM", u32(0)),
                                                       sub("DNAM", u32(1)), sub("SNAM", u32(topic_fid))]))
            dial = record("DIAL", topic_fid, [sub("EDID", zstr(f"ALifeTalkTopic_{mode}_{key}")), sub("FULL", zstr(prompt)),
                                              sub("PNAM", struct.pack("<f", 100.0)), sub("BNAM", u32(branch_fid)), sub("QNAM", u32(GENERIC_DIALOGUE)),
                                              sub("DATA", b"\0\0\0\0"), sub("SNAM", b"CUST"), sub("TIFC", u32(n))])
            dial_blob += dial + group(topic_fid, 7, infos_blob)
            ti += 1

    # --- jobs (docs/TASKS.md): "Got any work for me?" with its two follow-ups, hand-in, delivery, abandon
    globs += [glob(GLOBAL_OFFER, "ALifeTaskOffer", 0.0), glob(GLOBAL_STATE, "ALifeTaskState", 0.0)]
    next_info[0] = max(next_info[0], TINFO0)
    greet = ctda(F_GETGLOBAL, own(GLOBAL_MODE), float(M_GREET))
    def gv(gl, v, op=OP_EQ): return ctda(F_GETGLOBAL, own(gl), float(v), op=op)
    keys = ["work"] + [t[0] for t in JOB_TOPICS]
    def tfid(key): return own(TTOPIC0 + keys.index(key))
    def dial_rec(key, prompt, branch, n):
        return record("DIAL", tfid(key), [sub("EDID", zstr(f"ALifeTaskTopic_{key}")), sub("FULL", zstr(prompt)), sub("PNAM", struct.pack("<f", 100.0)),
                                          sub("BNAM", u32(branch)), sub("QNAM", u32(GENERIC_DIALOGUE)), sub("DATA", b"\0\0\0\0"), sub("SNAM", b"CUST"), sub("TIFC", u32(n))])
    def branch_rec(key, branch):
        return record("DLBR", branch, [sub("EDID", zstr(f"ALifeTaskBranch_{key}")), sub("QNAM", u32(GENERIC_DIALOGUE)), sub("TNAM", u32(0)),
                                       sub("DNAM", u32(1)), sub("SNAM", u32(tfid(key)))])
    work_branch = own(TBRANCH0)
    job_dials = 0
    infos_blob, n = b"", 0
    for ty, lines in JOB_PITCH.items():
        for line in lines:
            fid = new_info()
            infos_blob += info(own(fid), line, [greet, gv(GLOBAL_OFFER, ty), gv(GLOBAL_STATE, 0)], random=len(lines) > 1,
                               choices=(tfid("accept"), tfid("decline")))
            talk_map.append((fid, "work", "", M_GREET))
            n += 1
    branches.append(branch_rec("work", work_branch))
    dial_blob += dial_rec("work", "Got any work for me?", work_branch, n) + group(tfid("work"), 7, infos_blob)
    job_dials += 1
    for key, prompt, state, answers, action in JOB_TOPICS:
        if state is None:   # a follow-up of "Got any work?": same branch, not offered at the top level
            branch, conds = work_branch, [greet, gv(GLOBAL_OFFER, 0, OP_GT)]
        else:
            branch, conds = own(TBRANCH0 + keys.index(key)), [greet, gv(GLOBAL_STATE, state)]
            branches.append(branch_rec(key, branch))
        if key == "deliver":
            conds.append(ctda(F_GETITEMCOUNT, own(PARCEL), 1.0, op=OP_GE, run_on=2, reference=PLAYER_REF))
        infos_blob, n = b"", 0
        for line in answers:
            fid = new_info()
            infos_blob += info(own(fid), line, conds, goodbye=True, random=len(answers) > 1)
            talk_map.append((fid, action, "", M_GREET))
            n += 1
        dial_blob += dial_rec(key, prompt, branch, n) + group(tfid(key), 7, infos_blob)
        job_dials += 1

    # the sealed parcel (a vanilla satchel mesh; worth nothing to a merchant)
    parcel = record("MISC", own(PARCEL), [sub("EDID", zstr("ALifeSealedParcel")), sub("OBND", bytes.fromhex("f8fff7ff0000080007000700")),
                                          sub("FULL", zstr("Sealed Parcel")), sub("MODL", zstr("Clutter\\Containers\\Satchel.nif")),
                                          sub("DATA", struct.pack("<if", 0, 0.5))])
    # ALifeTasks: Miscellaneous quest, 4 slots x (work, return) objectives aimed at 8 optional aliases the plugin fills with markers.
    # Layout after vanilla JailQuest: DNAM (flags, priority, ., ., type 6 = misc), NEXT, objectives (QOBJ FNAM NNAM QSTA), ANAM, aliases.
    q = [sub("EDID", zstr("ALifeTasks")), sub("FULL", zstr("A-Life Jobs")), sub("DNAM", struct.pack("<HBBII", 0, 50, 0, 0, 6)), sub("NEXT", b"")]
    for s in range(TASK_SLOTS):
        for k, alias, text in ((0, s, "Job"), (1, TASK_SLOTS + s, "Return for your pay")):
            q += [sub("QOBJ", struct.pack("<H", 10 * s + k)), sub("FNAM", u32(0)), sub("NNAM", zstr(text)), sub("QSTA", struct.pack("<iI", alias, 0))]
    q.append(sub("ANAM", u32(2 * TASK_SLOTS)))
    for i in range(2 * TASK_SLOTS):
        name = f"Target{i}" if i < TASK_SLOTS else f"Giver{i - TASK_SLOTS}"
        q += [sub("ALST", u32(i)), sub("ALID", zstr(name)), sub("FNAM", u32(0x2 | 0x8)), sub("ALED", b"")]   # optional, allow reuse
    quest = record("QUST", own(TASK_QUEST), q)

    nrec = len(globs) + len(branches) + 1 + (len(branches) - 4) + job_dials + len(talk_map) + len(opening) + 2
    body = (group("GLOB", 0, b"".join(globs)) + group("MISC", 0, parcel) + group("DLBR", 0, b"".join(branches)) + group("DIAL", 0, dial_blob)
            + group("QUST", 0, quest))
    hedr = sub("HEDR", struct.pack("<fII", 1.71, nrec, next_info[0]))
    tes4_subs = [hedr, sub("CNAM", zstr("A-Life")), sub("SNAM", zstr("A-Life squad dialogue (generated by tools/build_talk_esp.py)"))]
    for m in MASTERS:
        tes4_subs += [sub("MAST", zstr(m)), sub("DATA", struct.pack("<Q", 0))]
    tes4_subs.append(sub("INTV", u32(0)))
    tes4 = record("TES4", 0, tes4_subs)
    tes4 = tes4[:8] + struct.pack("<I", 0x200) + tes4[12:]   # ESL flag
    open(out_esp, "wb").write(tes4 + body)

    # --- talk.ltx for the plugin: response -> action, and the opening lines it speaks aloud
    lines = ["; generated by tools/build_talk_esp.py - do not edit", "", "[responses]   ; local FormID in ALifeTalk.esp = action"]
    for fid, action, faction, mode in talk_map:
        lines.append(f"{fid:X} = {action}")
    lines += ["", "[globals]", f"mode = {GLOBAL_MODE:X}", f"toll = {GLOBAL_TOLL:X}", f"hello = {HELLO:X}", f"task_offer = {GLOBAL_OFFER:X}",
              f"task_state = {GLOBAL_STATE:X}", "", "[tasks]   ; docs/TASKS.md", f"quest = {TASK_QUEST:X}", f"parcel = {PARCEL:X}",
              f"slots = {TASK_SLOTS}", "", "[open]   ; mode.faction = line | line"]
    grouped = {}
    for mode, faction, line in opening: grouped.setdefault((mode, faction), []).append(line)
    for (mode, faction), ls in sorted(grouped.items()):
        lines.append(f"{mode}.{faction} = " + " | ".join(ls))
    open(out_ltx, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print(f"wrote {out_esp}: {len(talk_map)} answers, {len(opening)} opening lines, {len(branches)} top-level topics, job quest + parcel; {out_ltx}")


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "package", "ALifeTalk.esp")
    ltx = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "config", "talk.ltx")
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    build(out, ltx)
