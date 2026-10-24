"""Builds ALifeTalk.esp (ESL-flagged) and config/talk.ltx: the dialogue of A-Life squads.

Every conversation with a squad is real Skyrim dialogue: the squad's opening line (a HELO "Hello" topic) and the player's choices
(top-level topics) are selected by conditions on the speaker's faction and on a global the plugin sets for the squad the player is
looking at (ALifeTalkMode: 1 toll, 2 challenge, 3 greeting, 4 companion, 5 wounded, 6 yielded). The plugin learns what the player chose from the TESTopicInfoEvent of
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
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_next_id, esp

FACTIONS = ["bandit", "forsworn", "imperial", "stormcloak", "thalmor", "wanderer", "hunter", "mercenary", "mage",
            "necromancer", "vampire", "dawnguard", "silverhand", "vigilant", "caravan",
            "skaal", "redoran", "reaver"]   # must match enum Faction order in Core.h
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
GLOBAL_OFFER, GLOBAL_STATE = 0x802, 0x803   # ALifeTaskOffer (0 none, 1 bounty, 2 assault, 3 delivery, 4 fetch, 5 escort, 6 defend, 7 rescue, 8 recover, 9 hunt), ALifeTaskState
PARCEL, TASK_QUEST = 0x805, 0x806
STASH_NOTE, STASH_QUEST = 0x81E, 0x81F    # M5f (docs/STASHES.md): the stash note MISC, the ALifeStashes marker quest
STASH_SLOTS = 8                           # objective i, alias Stash<i>
TASK_SLOTS = 4                            # objectives 10*s (work, alias Target<s>) and 10*s+1 (return, alias Giver<s>)
TBRANCH0, TTOPIC0, TINFO0 = 0x840, 0x860, 0xA00
OP_GT = 0x40
JOB_PITCH = {   # what a leader says when asked for work, by offer type; the specifics go into the journal
    1: ["There's someone out there I want dead. Do it and the coin is yours.", "I've a name for you. Find them, end them, come back for your pay."],
    2: ["Enemies have dug in not far from here. Clear them out and you'll be paid.", "There's a nest of trouble nearby. Burn it out and come back to me."],
    3: ["I need a parcel carried to someone. Sealed, and it stays that way.", "Take this to a friend of ours. Don't open it, and don't lose it."],
    4: ["We're short on supplies. Bring me what I need and I'll pay well.", "I've a list of things we need. Fill it and you won't regret it."],
    # M5e (docs/TASKS.md section 8): escort, defend, rescue, recover, top-10 hunt
    5: ["We're moving on, and the roads aren't safe. Walk with us and you'll be paid when we get there.", "Could use another blade on the road. See us there safe and the coin is yours."],
    6: ["They'll come for this place soon. Stand with us when they do.", "We've had word of an attack. Help us hold here and you'll be paid."],
    7: ["One of ours was taken. Get them out alive and bring them back to me.", "We lost someone out there. Alive, I hope. Find them and bring them home."],
    8: ["One of ours never came back. Find the body and bring me the satchel they carried.", "There's a satchel on a dead friend of ours out there. I want it back."],
    9: ["There are people out there more dangerous than the rest. I want them gone, one by one.", "I keep a list of the worst of them. Work your way up it and I'll make it worth your while."],
}
SATCHEL = 0x80D                           # recover: the satchel on the body (0x80E-0x80F kept free for M5e)
# (key, prompt, ALifeTaskState it needs (None: a child of "Got any work?"), answers, action in talk.ltx)
JOB_TOPICS = [
    ("accept", "I'll do it.", None, ["Good. I'll mark it on your map.", "Then we have a deal. Don't keep me waiting."], "accept"),
    ("decline", "Not right now.", None, ["Suit yourself. The offer stands, for a while."], "decline"),
    ("turnin", "About that job. It's done.", 2, ["Good work. Here, as promised.", "So it's done. Here's your pay."], "turnin"),
    ("deliver", "I have a delivery for you.", 3, ["About time. Here, for your trouble.", "Ah, the parcel. My thanks, and your fee."], "deliver"),
    ("abandon", "I can't finish that job.", 1, ["Then I'll find someone who can.", "Pity. I thought you were up to it."], "abandon"),
]

# --- companions (docs/COMPANIONS.md): six more globals, a fourth talk mode (talking to your own companion), recruit + order topics
GLOBAL_COMP_OFFER, GLOBAL_COMP_STATE = 0x807, 0x808   # ALifeCompOffer (0 none, 1 free, 2 paid, 3 no room, 4 unwilling), ALifeCompState (1 follow, 2 wait/hold)
GLOBAL_HIRE1, GLOBAL_HIRE2 = 0x809, 0x80A             # the two contract prices (gold)
GLOBAL_COMP_FIRE, GLOBAL_COMP_LOOT = 0x80B, 0x80C     # 1 = holding fire, 1 = looting
M_COMP = 4
CBRANCH0, CTOPIC0, CINFO0 = 0x870, 0x890, 0xB00
COMP_HELLO = ["Yes?", "What do you need?", "I'm listening."]
# offer -> (answer lines, action, goodbye, follow-up child topics)
COMP_TRAVEL = {
    1: (["Gladly. Lead the way.", "Aye, we'll come along. Lead on."], "recruit", True, ()),
    2: (["For coin, we'd walk with you. A day, or two?", "We hire out, if the pay is right. How long?"], "hireask", False, ("day1", "day2", "notnow")),
    3: (["You've company enough already. We'd only slow you down."], "noroom", True, ()),
    4: (["We have our own road to walk. Ask someone else."], "unwilling", True, ()),
}
# (key, prompt, conditions on (global, value), answers, action); "parent": a child of the paid "Travel with me" answer
COMP_HIRE = [
    ("day1", "One day, then. Here's the gold.", GLOBAL_HIRE1, ["Done. We're with you till this time tomorrow.", "Coin's good. Lead on."], "hire1"),
    ("day2", "Two days. Here's the gold.", GLOBAL_HIRE2, ["Two days it is. Lead on.", "Done. We'll keep you breathing."], "hire2"),
    ("notnow", "Not today.", None, ["Suit yourself."], "hirenot"),
]
COMP_ORDERS = [
    ("follow", "Follow me.", (GLOBAL_COMP_STATE, 2), ["Right behind you.", "Lead on."], "follow"),
    ("wait", "Wait here.", (GLOBAL_COMP_STATE, 1), ["We'll wait.", "We'll hold here."], "wait"),
    ("move", "Go up ahead and hold there.", None, ["Moving up.", "Going."], "move"),
    ("hold", "Hold your fire.", (GLOBAL_COMP_FIRE, 0), ["We'll keep our heads down.", "Weapons down, then."], "hold"),
    ("attack", "Fight anything that threatens us.", (GLOBAL_COMP_FIRE, 1), ["With pleasure.", "About time."], "attack"),
    ("loot", "Help yourselves to what the dead carry.", (GLOBAL_COMP_LOOT, 0), ["Won't say no to that."], "loot"),
    ("noloot", "Leave the dead be.", (GLOBAL_COMP_LOOT, 1), ["As you say."], "noloot"),
    ("gear", "Let me see your gear.", None, ["Here. Take a look."], "gear"),
    ("dismiss", "We part ways here.", None, ["Safe roads, then.", "Fair enough. Luck to you."], "dismiss"),
]

# --- the wounded and the yielded (docs/WOUNDED.md): one more global, talk modes 5 (a member who is down) and 6 (a member who yielded)
GLOBAL_WOUND_HEAL = 0x810                 # ALifeWoundHeal: 1 when the player carries a healing potion
M_WOUND, M_YIELD = 5, 6
WBRANCH0, WTOPIC0, WINFO0 = 0x8A0, 0x8B0, 0xC00
WOUND_HELLO = ["Help... someone, please...", "I'm hurt... badly...", "Don't... don't leave me here..."]
YIELD_HELLO = ["Mercy! I yield!", "Enough! I give up, I give up!", "Don't kill me! Please!"]
# (key, prompt, mode, needs ALifeWoundHeal, answers, action)
WOUND_TOPICS = [
    ("heal", "Here, drink this.", M_WOUND, True, ["Thank you... I won't forget this.", "Gods... thank you, friend."], "heal"),
    ("rob", "Hand over your gold.", M_WOUND, False, ["Take it... just leave me be.", "Here... take it, take it."], "wrob"),
    ("finish", "Your fight is over.", M_WOUND, False, ["No... wait...", "Please, no..."], "finish"),
    ("spare", "Go. And don't come back.", M_YIELD, False, ["Thank you! We're going, we're going!", "You won't see us again. I swear it."], "spare"),
    ("robgo", "Empty your pockets, then go.", M_YIELD, False, ["Fine, fine! Take it all!", "Here, everything we have. Just let us go."], "robgo"),
    ("nomercy", "No mercy.", M_YIELD, False, ["No! Please...!", "You swore... no!"], "nomercy"),
]

# ------------------------------------------------------------------------------------------------ content
# Opening lines: what the squad says when the player talks to it (also what it says aloud when it stops the player).
OPEN = {
    (M_TOLL, "bandit"): ["Well? Let's see the coin.", "Don't make this hard. A little gold and you walk on."],
    (M_TOLL, "reaver"): ["This is Reaver ground, outlander. Coin, or blood.", "Ash and coin, that's what this island gives. Hand over yours."],
    (M_CHAL, "redoran"): ["Halt. Raven Rock keeps the peace on this road. Your business?", "Outlander. State your business on Solstheim."],
    (M_CHAL, "skaal"): ["You are far from the village, outsider. What do you want here?", "The All-Maker's land is not for wandering. Speak."],
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
    (M_GREET, "skaal"): ["The All-Maker keep you, traveller.", "Welcome. Few outsiders come this far north."],
    (M_GREET, "redoran"): ["Walk in peace. Raven Rock watches these roads.", "Keep to the roads, outlander. The ash hides more than stone."],
    (M_GREET, "reaver"): ["You again. Move along before we change our minds."],
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
    (M_TOLL, "pay", "reaver"): ["Smart. Now get off our ground.", "Good. The ash keeps your secret, and we keep your coin."],
    (M_TOLL, "refuse", "reaver"): ["Then the island takes you.", "Wrong answer, outlander."],
    (M_TOLL, "fight", "reaver"): ["Ha! At last, some sport.", "Come on, then!"],
    (M_TOLL, "poor", "reaver"): ["Not even a few septims? Pathetic. Go.", "Then crawl back to Raven Rock, beggar."],
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
    "skaal": ["Furs and meat from the hunt. The All-Maker provides.", "We trade fairly. Have a look."],
    "redoran": ["Supplies from the garrison stores. Don't tell the captain.", "A few things we can spare. Look quickly."],
}
for f_, lines_ in TRADE.items():
    ANSWER[(M_GREET, "trade", f_)] = lines_
LAWFUL_PEACE = ["Then keep that steel sheathed and keep moving.", "See that you do. We'll be watching."]
LAWFUL_DEFY = ["Then you'll answer to the law.", "That was a mistake."]
WILD_PEACE = ["Then go. And do not come back this way.", "Quickly, then. Before I change my mind."]
WILD_DEFY = ["Then you have chosen.", "Fool. You will not leave."]
for f in ("imperial", "stormcloak", "thalmor", "dawnguard", "vigilant", "redoran"):
    ANSWER[(M_CHAL, "peace", f)] = LAWFUL_PEACE
    ANSWER[(M_CHAL, "defy", f)] = LAWFUL_DEFY
for f in ("forsworn", "necromancer", "vampire", "silverhand", "skaal"):
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
    "skaal": ["The stones have been quiet since the outsider came. Or so we hope.", "Reavers came down past the Wind Stone again. We drove them off."],
    "redoran": ["The Reavers grow bolder every season. We hold the roads near the Rock, no further.", "Ash storms out of the Red Mountain. Keep your face covered."],
    "reaver": ["Walk on. Nothing for you here."],
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
    comp_info = [CINFO0]

    def new_comp_info():
        v = comp_info[0]
        comp_info[0] += 1
        return v
    for faction in FACTIONS:
        for line in COMP_HELLO:
            hello_infos += info(own(new_comp_info()), line, conds_for(M_COMP, faction), goodbye=False, random=True)
            count += 1
    wound_info = [WINFO0]

    def new_wound_info():
        v = wound_info[0]
        wound_info[0] += 1
        return v
    for faction in FACTIONS:
        for mode, lines_ in ((M_WOUND, WOUND_HELLO), (M_YIELD, YIELD_HELLO)):
            for line in lines_:
                hello_infos += info(own(new_wound_info()), line, conds_for(mode, faction), goodbye=False, random=True)
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
        if key in ("turnin", "abandon"):   # M5e: the squad you escort hands in or takes back the job where it stands (companion mode)
            comp_conds = [ctda(F_GETGLOBAL, own(GLOBAL_MODE), float(M_COMP)), gv(GLOBAL_STATE, state)]
            for line in answers:
                fid = new_info()
                infos_blob += info(own(fid), line, comp_conds, goodbye=True, random=len(answers) > 1)
                talk_map.append((fid, action, "", M_COMP))
                n += 1
        dial_blob += dial_rec(key, prompt, branch, n) + group(tfid(key), 7, infos_blob)

    # --- companions (docs/COMPANIONS.md): "Travel with me." (mode 3) with the paid follow-ups, and the orders (mode 4)
    globs += [glob(GLOBAL_COMP_OFFER, "ALifeCompOffer", 0.0), glob(GLOBAL_COMP_STATE, "ALifeCompState", 0.0), glob(GLOBAL_HIRE1, "ALifeHireGold1", 0.0),
              glob(GLOBAL_HIRE2, "ALifeHireGold2", 0.0), glob(GLOBAL_COMP_FIRE, "ALifeCompFire", 0.0), glob(GLOBAL_COMP_LOOT, "ALifeCompLoot", 0.0)]
    ckeys = ["travel"] + [t[0] for t in COMP_HIRE] + [t[0] for t in COMP_ORDERS]
    def cfid(key): return own(CTOPIC0 + ckeys.index(key))
    def cbranch(key): return own(CBRANCH0 + ckeys.index(key))
    def cdial(key, prompt, branch, n):
        return record("DIAL", cfid(key), [sub("EDID", zstr(f"ALifeCompTopic_{key}")), sub("FULL", zstr(prompt)), sub("PNAM", struct.pack("<f", 100.0)),
                                          sub("BNAM", u32(branch)), sub("QNAM", u32(GENERIC_DIALOGUE)), sub("DATA", b"\0\0\0\0"), sub("SNAM", b"CUST"), sub("TIFC", u32(n))])
    def cbranch_rec(key):
        return record("DLBR", cbranch(key), [sub("EDID", zstr(f"ALifeCompBranch_{key}")), sub("QNAM", u32(GENERIC_DIALOGUE)), sub("TNAM", u32(0)),
                                             sub("DNAM", u32(1)), sub("SNAM", u32(cfid(key)))])
    infos_blob, n = b"", 0
    for offer, (lines_, action, goodbye, children) in COMP_TRAVEL.items():
        for line in lines_:
            fid = new_comp_info()
            infos_blob += info(own(fid), line, [greet, gv(GLOBAL_COMP_OFFER, offer)], goodbye=goodbye, random=len(lines_) > 1,
                               choices=tuple(cfid(c) for c in children))
            talk_map.append((fid, action, "", M_GREET))
            n += 1
    branches.append(cbranch_rec("travel"))
    dial_blob += cdial("travel", "Travel with me.", cbranch("travel"), n) + group(cfid("travel"), 7, infos_blob)
    for key, prompt, price, answers, action in COMP_HIRE:   # follow-ups of the paid answer: same branch, not offered at the top level
        conds = [greet, gv(GLOBAL_COMP_OFFER, 2)]
        if price is not None:
            conds.append(ctda(F_GETITEMCOUNT, GOLD, op=OP_GE, global_value=own(price), run_on=2, reference=PLAYER_REF))
        infos_blob, n = b"", 0
        for line in answers:
            fid = new_comp_info()
            infos_blob += info(own(fid), line, conds, goodbye=True, random=len(answers) > 1)
            talk_map.append((fid, action, "", M_GREET))
            n += 1
        dial_blob += cdial(key, prompt, cbranch("travel"), n) + group(cfid(key), 7, infos_blob)
    comp_mode = ctda(F_GETGLOBAL, own(GLOBAL_MODE), float(M_COMP))
    for key, prompt, need, answers, action in COMP_ORDERS:
        conds = [comp_mode] + ([gv(need[0], need[1])] if need else [])
        infos_blob, n = b"", 0
        for line in answers:
            fid = new_comp_info()
            infos_blob += info(own(fid), line, conds, goodbye=True, random=len(answers) > 1)
            talk_map.append((fid, action, "", M_COMP))
            n += 1
        branches.append(cbranch_rec(key))
        dial_blob += cdial(key, prompt, cbranch(key), n) + group(cfid(key), 7, infos_blob)
    assert comp_info[0] <= 0xFFF, "ESL FormID range exceeded"

    # --- the wounded (mode 5) and the yielded (mode 6): docs/WOUNDED.md s3-4
    globs.append(glob(GLOBAL_WOUND_HEAL, "ALifeWoundHeal", 0.0))
    wkeys = [t[0] for t in WOUND_TOPICS]
    for key, prompt, mode, heal, answers, action in WOUND_TOPICS:
        topic_fid, branch_fid = own(WTOPIC0 + wkeys.index(key)), own(WBRANCH0 + wkeys.index(key))
        conds = [ctda(F_GETGLOBAL, own(GLOBAL_MODE), float(mode))] + ([gv(GLOBAL_WOUND_HEAL, 1)] if heal else [])
        infos_blob, n = b"", 0
        for line in answers:
            fid = new_wound_info()
            infos_blob += info(own(fid), line, conds, goodbye=True, random=len(answers) > 1)
            talk_map.append((fid, action, "", mode))
            n += 1
        branches.append(record("DLBR", branch_fid, [sub("EDID", zstr(f"ALifeWoundBranch_{key}")), sub("QNAM", u32(GENERIC_DIALOGUE)), sub("TNAM", u32(0)),
                                                    sub("DNAM", u32(1)), sub("SNAM", u32(topic_fid))]))
        dial_blob += record("DIAL", topic_fid, [sub("EDID", zstr(f"ALifeWoundTopic_{key}")), sub("FULL", zstr(prompt)), sub("PNAM", struct.pack("<f", 100.0)),
                                                sub("BNAM", u32(branch_fid)), sub("QNAM", u32(GENERIC_DIALOGUE)), sub("DATA", b"\0\0\0\0"), sub("SNAM", b"CUST"),
                                                sub("TIFC", u32(n))]) + group(topic_fid, 7, infos_blob)
    assert wound_info[0] <= 0xFFF and WTOPIC0 + len(WOUND_TOPICS) <= WINFO0, "ESL FormID range exceeded"

    # the sealed parcel (a vanilla satchel mesh; worth nothing to a merchant)
    parcel = record("MISC", own(PARCEL), [sub("EDID", zstr("ALifeSealedParcel")), sub("OBND", bytes.fromhex("f8fff7ff0000080007000700")),
                                          sub("FULL", zstr("Sealed Parcel")), sub("MODL", zstr("Clutter\\Containers\\Satchel.nif")),
                                          sub("DATA", struct.pack("<if", 0, 0.5))])
    # M5e: the satchel on the recover job's body (same mesh; worth nothing to a merchant)
    parcel += record("MISC", own(SATCHEL), [sub("EDID", zstr("ALifeRecoverSatchel")), sub("OBND", bytes.fromhex("f8fff7ff0000080007000700")),
                                            sub("FULL", zstr("Bloodied Satchel")), sub("MODL", zstr("Clutter\\Containers\\Satchel.nif")),
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
    # M5f: the stash note (the mesh and bounds of vanilla note books, e.g. dunLabyrinthianMazeClue 108160; worth nothing) and the
    # ALifeStashes quest: STASH_SLOTS objectives, each aimed at an optional alias the plugin fills with a marker on the stash
    parcel += record("MISC", own(STASH_NOTE), [sub("EDID", zstr("ALifeStashNote")), sub("OBND", bytes.fromhex("f8fffbff0000080005000000")),
                                               sub("FULL", zstr("Stash Note")), sub("MODL", zstr("Clutter\\Books\\Note01.nif")),
                                               sub("DATA", struct.pack("<if", 0, 0.1))])
    q = [sub("EDID", zstr("ALifeStashes")), sub("FULL", zstr("A-Life Stashes")), sub("DNAM", struct.pack("<HBBII", 0, 40, 0, 0, 6)), sub("NEXT", b"")]
    for i in range(STASH_SLOTS):
        q += [sub("QOBJ", struct.pack("<H", i)), sub("FNAM", u32(0)), sub("NNAM", zstr("Stash")), sub("QSTA", struct.pack("<iI", i, 0))]
    q.append(sub("ANAM", u32(STASH_SLOTS)))
    for i in range(STASH_SLOTS):
        q += [sub("ALST", u32(i)), sub("ALID", zstr(f"Stash{i}")), sub("FNAM", u32(0x2 | 0x8)), sub("ALED", b"")]   # optional, allow reuse
    quest += record("QUST", own(STASH_QUEST), q)

    body = (group("GLOB", 0, b"".join(globs)) + group("MISC", 0, parcel) + group("DLBR", 0, b"".join(branches)) + group("DIAL", 0, dial_blob)
            + group("QUST", 0, quest))
    # next object ID: one past the highest local ID written (tools/check_next_id.py checks it after the write)
    next_id = 1 + max(r.fid & 0xFFFFFF for r in esp.walk(body) if (r.fid >> 24) == len(MASTERS))
    nrec = esp.hedr_count(body)   # records plus GRUPs, the way the Creation Kit and the DLC masters count them
    hedr = sub("HEDR", struct.pack("<fII", 1.71, nrec, next_id))
    tes4_subs = [hedr, sub("CNAM", zstr("A-Life")), sub("SNAM", zstr("A-Life squad dialogue (generated by tools/build_talk_esp.py)"))]
    for m in MASTERS:
        tes4_subs += [sub("MAST", zstr(m)), sub("DATA", struct.pack("<Q", 0))]
    tes4_subs.append(sub("INTV", u32(0)))
    tes4 = record("TES4", 0, tes4_subs)
    tes4 = tes4[:8] + struct.pack("<I", 0x200) + tes4[12:]   # ESL flag
    open(out_esp, "wb").write(tes4 + body)
    ok, nid, top, bad = check_next_id.check(tes4 + body)
    assert ok, f"{out_esp}: next object ID 0x{nid:X}, own records at or above it: {[hex(b) for b in bad]}"

    # --- talk.ltx for the plugin: response -> action, and the opening lines it speaks aloud
    lines = ["; generated by tools/build_talk_esp.py - do not edit", "", "[responses]   ; local FormID in ALifeTalk.esp = action"]
    for fid, action, faction, mode in talk_map:
        lines.append(f"{fid:X} = {action}")
    lines += ["", "[globals]", f"mode = {GLOBAL_MODE:X}", f"toll = {GLOBAL_TOLL:X}", f"hello = {HELLO:X}", f"task_offer = {GLOBAL_OFFER:X}",
              f"task_state = {GLOBAL_STATE:X}",
              f"comp_offer = {GLOBAL_COMP_OFFER:X}", f"comp_state = {GLOBAL_COMP_STATE:X}", f"hire1 = {GLOBAL_HIRE1:X}", f"hire2 = {GLOBAL_HIRE2:X}",
              f"comp_fire = {GLOBAL_COMP_FIRE:X}", f"comp_loot = {GLOBAL_COMP_LOOT:X}", f"wound_heal = {GLOBAL_WOUND_HEAL:X}", "", "[tasks]   ; docs/TASKS.md", f"quest = {TASK_QUEST:X}", f"parcel = {PARCEL:X}",
              f"satchel = {SATCHEL:X}", f"slots = {TASK_SLOTS}", "", "[stashes]   ; docs/STASHES.md", f"note = {STASH_NOTE:X}", f"quest = {STASH_QUEST:X}",
              f"slots = {STASH_SLOTS}", "", "[open]   ; mode.faction = line | line"]
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
