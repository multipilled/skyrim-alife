# A-Life for Skyrim 2.1: player guide

<!-- G10 (docs only). Every section below is sourced from the docs and code on master; each section names its sources in
     an HTML comment like this one. "(checked in game)" is used only where docs/TESTLOG.md shows a passing in-game run
     (the run is named in that section's comment). Everything else is described from the design docs and the code and
     has not been tested in the game yet. -->

This guide explains what A-Life does while you play, and what every menu option changes.
It is written for players. You do not need to read anything else to use the mod.

## What A-Life is

<!-- Sources: docs/README.md "What it does". -->

A-Life fills Skyrim with groups of people who live their own lives.
They travel, trade, hunt, fight, loot, grow stronger and die all over the map.
This happens whether you are watching or not.
When you meet them on the road, they have a reason to be there and an opinion of you.

It is not a combat mod.
Most of what A-Life adds is people with somewhere to go and something to say.

## Squads and factions

<!-- Sources: docs/README.md "What it does"; docs/SOLSTHEIM.md (factions, homes, relations); docs/CIVILWAR.md (rules,
     gates); docs/MUTANTS.md; docs/COMPAT.md (on-screen limits: 3 groups / 15 people). In game: docs/TESTLOG.md W1
     t10-0643 (Civil War flip there and back, Dawnguard gate through A New Order, war end: pass), W1 t11-0651 and t11s-0708
     (Redoran, Reavers, Skaal on Solstheim: pass), W1 t9-0546 (squads near the player come into view and stay: pass). -->

People travel in small groups called squads.
Each squad belongs to a faction.

In Skyrim there are fifteen factions:
bandits, the Forsworn, the Imperial Legion, the Stormcloaks, the Thalmor, wanderers, hunters, mercenaries, mages,
necromancers, vampires, the Dawnguard, the Silver Hand, the Vigilants of Stendarr and Khajiit caravans.

On Solstheim (with Dragonborn installed) there are three more, each with squads at its own homes (checked in game):

- **The Skaal** live around Skaal Village. They are wary of outsiders at first.
- **House Redoran's guard** holds Raven Rock and the places around it. They keep the law, like soldiers in Skyrim.
- **The Reavers** are the island's raiders. They fight every other group and demand a toll before a fight.

Each world is its own map. Squads from Skyrim stay in Skyrim, and squads from Solstheim stay on Solstheim.
(The one exception is a companion you take across and part ways with there; see Companions.)

What squads do:

- They pick a place to go: a fort, camp, cave, ruin, farm, inn, mine, shrine, town or city.
- They walk there, stay a while, work, rest and move on.
- They get hungry, tired and hurt. They eat what they carry, drink potions, hunt, fish and rest at inns.
- They trade at the shops of the towns they pass. The shops really gain and lose stock.
- Strong squads raid settlements and clear dungeons.
- Rival squads fight each other, even far away from you. Forts change hands. Camps are cleared and retaken.
- They grow with you. When you level up, squads that fell behind catch up a step at a time. Nobody is ever scaled down.
- They loot the dead they pass: their own fallen, their enemies, and bodies you left behind (after a while, so you get
  the first look). Quest items are never taken.

Monsters live in lairs and territories of their own:
wolves, bears, sabre cats, draugr, falmer, trolls, spriggans, death hounds, werewolves and werebears, rieklings, ghosts,
hagravens, giants with their mammoths, Dwemer automatons, chaurus, ash spawn and more.
They hunt by day or by night and fight the factions.

Only the squads near you are in the world as real people. At most 3 squads (15 people) are on screen at once.
Squads near you come into view without popping in and out (checked in game).

### The Civil War

Imperial and Stormcloak squads follow the Civil War quests (checked in game).

- The places in each hold belong to the side that holds the hold.
- When a hold changes sides, its guard squads change sides too.
- When the war ends, the losing side stands down. Its squads leave the roads of Skyrim.
- Dawnguard squads only appear after Isran starts rebuilding the order (the quest "A New Order").

## Standing: how people see you

<!-- Sources: docs/STANDING.md (tiers, base rules, goodwill, memory, predators). -->

Nobody is hostile to you just because of their faction.
Every squad works out an attitude toward you from what it can know:

- who you swore to in the Civil War;
- your race;
- the guilds you belong to;
- what you wear (a faction's colours earn its trust, a little);
- whether you have a bounty in the hold they guard;
- what you have done to their people;
- how dangerous you look.

There are five levels:

| Level | What the squad does |
|---|---|
| Hostile | attacks on sight |
| Unfriendly | is wary: soldiers challenge you, outlaws weigh their odds |
| Neutral | goes about its business |
| Friendly | greets you, shares news, does not turn on you if you draw a weapon |
| Allied | fights beside you |

Killing a faction's people lowers how it sees you. Its allies care a bit too, and its enemies like you a bit more.
Each squad also remembers what it saw you do: who you attacked, who you helped, whether you paid.
Good and bad deeds both fade over time. Bad deeds take longer.

Outlaws weigh the odds. A bandit squad attacks when it thinks it will win.
If you look too strong, it keeps its distance.

You can see how each faction regards you on the MCM **Standing** page.

## Talking to squads

<!-- Sources: docs/CONVERSATIONS.md (modes, choices, consequences); docs/STANDING.md "Talking"; docs/TASKS.md section 2
     (job topics); docs/COMPANIONS.md sections 1 and 3; tools/build_talk_esp.py (the exact topic lines: TOPICS,
     COMP_TRAVEL, COMP_HIRE, COMP_ORDERS). -->

Every meeting is ordinary Skyrim dialogue. There are no pop-ups and no hotkeys.
A squad walks up to you only when it has a reason. Otherwise, you start the conversation by talking to one of them.

**Tolls.** Bandits (and Reavers) who think they have the upper hand stop you and name a price. You can say:

- *Pay the toll.* (you need the gold)
- *I don't have that much.* (believed if you really are poor)
- *I'm not paying.*
- *Come and take it, then.*

If you ignore them, they warn you once and then attack.

**Challenges.** Soldiers, the Thalmor, the Dawnguard and the Vigilants stop armed strangers they distrust.
The Forsworn, necromancers, vampires and the Silver Hand stop you on their own ground.
On Solstheim, the Redoran guard and the Skaal challenge strangers too. You can say:

- *I mean no harm. I'm only passing through.* (they let you go on for a while)
- *I don't answer to you.* (this ends in a fight)

**Greetings.** Anyone who regards you as Neutral or better will talk. You can ask:

- *Let me see what you have for sale.* This opens trade over what that person really carries and the gold they really
  have. Khajiit caravans carry a real stock and sell it on to town shops.
- *What's the word on the road?* They tell you about something that really happened in the world.
- *Got any work for me?* A squad leader may have a job for you (see Tasks).
- *Travel with me.* The squad may join you (see Companions).

Refusing or fighting never starts a fight in the middle of the dialogue. It happens once the menu closes.
If you walk away without answering, their patience runs out after a few seconds.
Trading and asking for news make a squad think a little better of you.

You can set how often squads walk up to you with the **Squads that stop you to talk** option.

## Tasks

<!-- Sources: docs/TASKS.md sections 1-5 (givers, offers, four slots, the four M5a types, rewards, penalties, expiry) and
     section 8 (escort, defend, rescue, recover, top-10 hunt, no ambushes, pay); docs/STASHES.md "Job rewards"
     (reward chance 0.5, the stash replaces the gold). -->

Ask a squad leader *Got any work for me?*
Leaders of people squads give work to anyone they are on speaking terms with (Neutral or better).
Outlaw factions give work too. Monster packs never do.

- Not every leader has work. About 3 in 10 have nothing.
- An offer stays open for about 6 game hours.
- You can hold up to 4 jobs at once.
- After you finish or fail a job, that squad has nothing for you for about 6 game hours.

Each job appears in your journal with a map and compass marker. When you accept, a notice tells you who, where and how
many. The world keeps going while you work.

The job types:

| Job | What you do | Where you collect |
|---|---|---|
| Bounty | kill the leader of an enemy squad (anyone's kill counts) | back at the leader who gave it |
| Assault | wipe out the enemy squads (or monsters) holding a place; they stay put while the job runs | back at the leader |
| Delivery | carry a sealed parcel to the leader of another squad | the person you deliver to pays |
| Fetch | bring a number of items (pelts, ore, ingredients, potions...) | back at the leader; the items are taken |
| Escort | the squad travels with you to a place | back at the leader, where they stand |
| Defend | hold off waves of attackers at the squad's place | back at the leader |
| Rescue | kill the captors; the captive then follows you home | back at the leader, once the captive is near |
| Recover | find a body near a place and bring back the satchel it carries | back at the leader |
| Hunt | kill the ten most dangerous enemies of that faction, one by one | back at the leader |

To hand in, say *About that job. It's done.*
For a delivery, say *I have a delivery for you.* to the person it is for.
To give up, say *I can't finish that job.*

**Rewards.** You get gold. Harder jobs, stronger targets and longer distances pay more.
You may also get one item from the leader's own pack: food, a drink or a potion.
Half the time, a job pays with a hidden stash near the leader instead of the gold. The stash is worth at least as much as
the gold it replaces (see Stashes).
A finished job raises how the leader's faction sees you. Its allies like it too.

**Failing.** A job fails if the squad that gave it is wiped out (no penalty: nobody is left to blame you).
Giving up costs some of that faction's goodwill.
A delivery fails if you lose the parcel, and an escort fails if the squad dies in your care.
Jobs have no time limit, except that an escort that arrives late pays half.

## Companions

<!-- Sources: docs/COMPANIONS.md sections 1-4 (who joins, prices, limits, orders, leaving, other worldspace);
     tools/build_talk_esp.py COMP_TRAVEL / COMP_HIRE / COMP_ORDERS (the exact lines). In game: docs/TESTLOG.md T13b
     full-1015-2039 (free join, paid hire, wait / follow / move / hold fire / attack, save and load, dismissal: pass), W1
     whiterun-1018-0530 (6 rounds of recruit, follow, fight, dismiss, with save and load: pass) and W1 solstheim-1018-0540
     (squads taken to Solstheim and dismissed there settle there: pass). -->

A whole squad can travel with you (checked in game).
Talk to any member and say *Travel with me.*

- A squad that regards you as **Friendly** or **Allied** joins for free.
- A **Neutral** squad of wanderers, mercenaries, bandits, hunters or Reavers may join for pay. They ask for one or two
  days. Answer *One day, then. Here's the gold.*, *Two days. Here's the gold.* or *Not today.*
  Veteran squads agree more often than others. Novice squads never hire out.
  The gold is paid up front. An hour before the time runs out, you get a notice. Then they leave.
- If you already have enough company, or the squad has its own road to walk, they say so.

You can have up to 6 A-Life companions in at most 2 squads.
They do not count against vanilla followers. Lydia, hirelings and your dog can come along too.

Companions sneak when you sneak, help in your fights, follow you through doors, and catch up if they fall far behind.
They are never witnesses to your crimes.

Orders (talk to any member):

- *Follow me.*
- *Wait here.*
- *Go up ahead and hold there.*
- *Hold your fire.* (they stop fighting and break off fights)
- *Fight anything that threatens us.* (the default)
- *Help yourselves to what the dead carry.* / *Leave the dead be.* (they loot or leave bodies; off at first)
- *Let me see your gear.* (trade gear with that member; what you give is theirs)
- *We part ways here.* (they go back to their own life; paid time is not refunded)

A squad also leaves if its paid time runs out or if it turns against you (for example, you attack them).
If you part ways on Solstheim with a Skyrim squad (or the other way round), they settle there.
If they cannot live there, they say *"Not today."* and stay with you.

Companions are kept with your save and placed back beside you when you load (checked in game).

## Wounded, help, finish and surrender

<!-- Sources: docs/WOUNDED.md sections 1-4 (going down, calls for help, squadmates, self-heal, finishing, offline
     survivors, the player's choices, surrender) and section 5 (standing); tools/build_talk_esp.py WOUND_TOPICS (the exact
     lines). docs/TESTLOG.md W2 found this feature inactive in game; the fix is on master (merge 5941532), but TESTLOG has
     no passing in-game run of it yet, so it is not marked as checked in game. -->

Beaten people often do not die at once.
A badly hurt member of a people squad may drop to their knees instead. They cannot fight or move.
Monsters never do this.

While someone is down:

- They call for help when you are near.
- A squadmate who is not busy fighting may come over and heal them with a potion.
- After about a minute and a half, they may drink a potion of their own and get up.
- An enemy nearby may walk over and finish them.

You can talk to someone who is down:

- *Here, drink this.* (only if you carry a healing potion) They get up with full health. Their faction likes you more,
  and that squad remembers it well. A hostile squad you help leaves you alone for a while.
- *Hand over your gold.* You take their gold and weapons. They stay down. Their faction likes you less,
  unless they attacked you first.
- *Your fight is over.* You kill them. This counts as a kill, and it looks bad to their faction.

**Surrender.** A hostile squad that is losing a fight with you, or is badly outnumbered, may give up.
A wounded enemy may also yield when you stand close with a weapon drawn.
They kneel and say *"Mercy! I yield!"* You can say:

- *Go. And don't come back.* They get up and leave you alone for a while. Their faction likes you a bit more.
- *Empty your pockets, then go.* You take their gold and weapons, then they leave.
- *No mercy.* You kill them. Killing someone who surrendered is frowned on, even by their enemies.

If you walk away or wait about two minutes, they get up and leave.
Some factions never surrender: the Thalmor, the Forsworn, necromancers, vampires, the Silver Hand and the Reavers.

Far away from you, fights can leave a beaten squad wounded where it fell for a few hours.
You may hear their calls for help in the news. If you go there, you find them kneeling and calling for help.

## Stashes and notes

<!-- Sources: docs/STASHES.md section 2 (spots, hidden half, content and budget, notes on the dead, reading, job rewards,
     markers, looting, no refill) and section 4 (Economy scaling). Not marked as checked in game: docs/TESTLOG.md has no
     stash run. -->

Hidden stashes lie near camps, forts, caves, ruins, mines, shipwrecks and the wilds.
They are never in cities, towns, inns or farms.

- About half of them already hold something. Nothing marks them. You find them by walking by.
- The dead sometimes carry a **stash note**. When you pick one up, a stash nearby is marked on your map, and a notice
  tells you where. The note is used up.
- A finished job may pay with a stash near the leader who gave it (see Tasks).

A stash holds a mix of potions, arrows, gems, soul gems, ore and ingots, scrolls, lockpicks and gold.
Its value grows with your level and with the **Economy (rewards)** slider.

Up to 8 stashes can be marked at once. A ninth takes the oldest marker (that stash stays full, just unmarked).
When you take the first item from a stash, its marker goes away. Stashes do not refill on a timer.

## Camp life

<!-- Sources: docs/CAMPLIFE.md section 3 (jobs, sleep 21:00-07:00, on screen only, campfire scene) and section 6
     (status: built, not run in game; stories shown as on-screen lines; no music; offline rest not done). -->

A squad that stays at a place looks like it lives there. While it is on screen:

- someone keeps watch, and someone walks a patrol around the place;
- an archer may take high ground;
- outlaws may post someone to lie in wait on the way in;
- others sit at the fire;
- at night (9 PM to 7 AM) most go to sleep on beds and bedrolls, and a few stay on watch.

When two or more sit at a campfire and you are close, one may tell a story.
The story appears as short on-screen lines, one at a time.
A fight ends all of this at once. They go back to it afterwards.

## Dragons

<!-- Sources: docs/DRAGONS.md sections 2 (gate after the dragons return, shared timer), 4 (places, never cities, vanilla
     lairs, no Solstheim), 5 (how many, flight, panic, offline deaths), 6 (on screen: distance, 2 game hours, soul and
     bones), 7 (MCM toggle) and 11 (bodies stay). The shared timer and the soul absorb are [U] in DRAGONS.md (lab checks),
     hence "meant" / "should". Not marked as checked in game: docs/DRAGONS.md says no game run yet and
     docs/TESTLOG.md has none. -->

A few dragons live in the A-Life world like any other creature.
There are none until the dragons return in the main quest.

- Dragons fly straight between dragon lairs, word walls and the high mountains, stay a while, then move on.
- They hunt the squads they find. Novice and advanced squads run from a dragon. Veterans fight.
- There are about 2 dragons at a time at the default settings. A new one appears every few days if one is missing.
- They never fly over cities, towns, inns or player homes.
- At a vanilla dragon lair, the lair's own dragon is the one you meet.
- There are no A-Life dragons on Solstheim.

A-Life dragons use the same timer as vanilla random dragon attacks, so the two are meant not to come at once.
A dragon that comes near you arrives flying. It stays for about 2 game hours, then flies off unless it is killed.
They are Skyrim's own dragons, so killing one yourself should give you its soul. Its bones stay for you to loot.
A dragon killed far away by others is reported in the news.

The **Dragons** option turns A-Life's dragons on or off. Off means no A-Life dragons. Vanilla dragons are not touched.
The **Dragons** difficulty slider changes how many there are and how strong they are.

## Difficulty

<!-- Sources: docs/DIFFICULTY.md sections 1 (one rule), 2 (channels), 3 (derived values), 4 (default rows, slider
     ranges); the difficulty config file on master, sections novice..legendary, limits, sliders (same values); docs/CONFIG.md;
     papyrus/ALife_MCM.psc (slider names, info texts, "In effect now"). In game: docs/TESTLOG.md W2 load-0838
     ("difficulty sliders restored from the save": pass). Note: the MCM info text for Task difficulty says "more ambushes on
     escorts", but docs/TASKS.md section 8 says there are no escort ambushes; this guide follows TASKS.md. -->

A-Life has two difficulty controls that work together:

1. **Skyrim's own difficulty** (Novice to Legendary), if **Follow game difficulty** is on.
2. **Eight sliders** on the MCM **Difficulty** page, one for each part of A-Life.

For each part, the effect is Skyrim's difficulty times the slider.
Adept with every slider at 1.0 is the baseline.
With **Follow game difficulty** off, Skyrim's difficulty is ignored and only the sliders count.
The **In effect now** column shows the result for each part.

What Skyrim's difficulty does to each part (1.0 = Adept):

| Part | Novice | Apprentice | Adept | Expert | Master | Legendary |
|---|---|---|---|---|---|---|
| Population density | 0.8 | 0.9 | 1.0 | 1.05 | 1.1 | 1.2 |
| Monster population | 0.75 | 0.9 | 1.0 | 1.05 | 1.15 | 1.25 |
| Squad strength | 0.7 | 0.85 | 1.0 | 1.1 | 1.2 | 1.35 |
| Offline combat lethality | 0.9 | 0.95 | 1.0 | 1.0 | 1.05 | 1.1 |
| Economy (rewards) | 1.5 | 1.25 | 1.0 | 0.9 | 0.8 | 0.7 |
| Wounded and surrender | 1.3 | 1.15 | 1.0 | 0.95 | 0.9 | 0.8 |
| Task difficulty | 0.75 | 0.9 | 1.0 | 1.1 | 1.2 | 1.3 |
| Dragons | 0.6 | 0.8 | 1.0 | 1.1 | 1.25 | 1.4 |

So on Novice there are fewer, weaker squads and jobs pay more.
On Legendary there are more and stronger squads, and pay is leaner.

The eight sliders (each 1.0 by default, in steps of 0.1):

| Slider | Range | What it changes |
|---|---|---|
| **Population density** | 0.5 - 1.5 | how many people squads live in the world. The number on screen at once never changes. |
| **Monster population** | 0.5 - 1.5 | how many monster packs fill the lairs |
| **Squad strength** | 0.5 - 2.0 | bigger squads (at most 7), higher levels and better gear, for everyone, friends and companions too |
| **Offline combat lethality** | 0.5 - 2.0 | how deadly fights between squads are while you are far away |
| **Economy (rewards)** | 0.5 - 2.0 | higher: job pay and stash value go up; tolls and companion pay go down |
| **Wounded and surrender** | 0.5 - 2.0 | higher: more people go down wounded, yield or surrender instead of dying, and fewer are finished off |
| **Task difficulty** | 0.5 - 2.0 | higher: jobs ask for more (more defend waves, a better chance of guards at a recovery, less time where a job has a time limit); lower: easier jobs and a shorter hunt list |
| **Dragons** | 0.5 - 2.0 | how often dragons appear and how strong they are |

The final value for each part is kept within safe limits, whatever the two controls say.
Raising strength makes squads stronger over the next few level steps. Lowering it never weakens anyone who already
exists; only new squads are smaller.

The sliders are saved with your game. Loading a save puts its slider values back (checked in game).

## The MCM, option by option

<!-- Sources: papyrus/ALife_MCM.psc (pages, every Add*Option label, menu choices, defaults in OnOptionDefault, info
     texts in OnOptionHighlight); the read-only lines: Sim::StatusLine, CompatInfo in the Papyrus natives,
     Rep::PumpMenuCache (standing rows and the Record line); docs/README.md "MCM" and "Uninstall"; docs/COMPAT.md section 4 (patrol mod budget). -->

Open it through SkyUI: **Mod Configuration > A-Life**. It has four pages:
**A-Life**, **Difficulty**, **Standing** and **News**.
Options are greyed out until the A-Life world is ready.

### Page: A-Life

**Simulation**

- **A-Life enabled** (default on). Off sends every A-Life squad away from the screen and pauses the A-Life world.
- **Offline combat** (default "At places (default)"). Decides whether enemy squads fight when they meet far away from you.
  - "Off": they never fight while you are away.
  - "At places (default)": only squads heading to the same place fight.
  - "Everywhere": any two enemy squads that cross paths fight.
- **News notifications** (default on). Short on-screen notices about events near you: calls for help, battle and death
  reports, squads wiped out, forts changing hands. Everything, near or far, is kept on the **News** page either way.
- **Dynamic faction relations** (default off). When on, every death shifts how the factions see each other. Wars,
  alliances and cease-fires are announced. The Civil War always stays a war.
- **Dragons** (default on). A-Life's dragons on or off (see Dragons). Vanilla dragons are not touched.

**Encounters**

- **Squads that stop you to talk** (default "Normally (default)"). How often a squad walks up and stops you to talk,
  for example a bandit's toll or a patrol's challenge.
  - "Never (only when I talk to them)": every conversation is up to you.
  - "Rarely"
  - "Normally (default)"
  - "Often"
  There is always a pause after one conversation before the next.
- **Squads loot bodies** (default on). Squads strip the bodies they pass. Quest items are never taken.

**World**

- **Status**: how many squads the world holds and how many are near you. Before the A-Life world has started, it says it
  is waiting for the opening quest. You cannot change it.
- **Patrol mods**: the patrol mods A-Life has found, or "none detected". You cannot change it.
- **On screen with patrol mods** (default "Auto (default)"). How much room A-Life takes on screen when Immersive Patrols
  or Skyrim at War is loaded (see Compatibility).
  - "Auto (default)": A-Life shrinks its on-screen squads and its population while those mods are loaded.
  - "Off (full A-Life)": A-Life keeps its full 3 squads and 15 people on screen.
  - "1 squad", "2 squads", "3 squads": a fixed number of A-Life squads on screen, 5 people each.
  Forts those mods garrison are left to them in every case.
- **Prepare to uninstall**: press it and answer Yes. It removes every A-Life person from the world and clears A-Life's
  state. The option then reads "Done - save, then remove". Save, quit, then remove the mod.

### Page: Difficulty

**Skyrim difficulty**

- **Current**: the Skyrim difficulty you are playing at. You cannot change it here.
- **Follow game difficulty** (default on). On: Skyrim's difficulty also scales A-Life. Off: only the sliders count.

**A-Life sliders (x the difficulty)**

One slider per part, each 1.0 by default (see the Difficulty section above for what each one does):
**Population density**, **Monster population**, **Squad strength**, **Offline combat lethality**,
**Economy (rewards)**, **Wounded and surrender**, **Task difficulty** and **Dragons**.

**In effect now**

The same eight names, each showing the value actually in use right now: Skyrim's difficulty times the slider.

### Page: Standing

- **How the people of Skyrim see you**: one line per faction with its level (Hostile to Allied) and a number.
- **What they know**: the **Record** line shows your notoriety and any bounty you owe.

### Page: News

- **News of the road (newest first)**: the latest events in the world, up to 24 lines.
  Before anything has happened, it shows **Nothing yet**.

## Compatibility

<!-- Sources: docs/COMPAT.md (the two mods, hand-over, faction mapping, actor budget table, companions and small budgets,
     the third mod not read yet, lab done-check not run); docs/README.md "Patrol mods", "Requirements", "Install"
     (vanilla followers: docs/COMPANIONS.md section 2). Two mods together: ApplyCompatBudget in the compat core takes the
     smaller groups / actors (2 / 9) and multiplies population (0.85 x 0.6 = 0.51, floor 0.3) and patrol scale
     (0.6 x 0.5 = 0.30); docs/COMPAT.md section 4. Not marked as checked in game: docs/CHANGELOG.md says the in-game
     check needs both mods, and docs/TESTLOG.md has no run with them. -->

**Requirements.** Skyrim Special Edition or Anniversary Edition (1.6 or 1.7), SKSE, the Address Library for SKSE Plugins,
SkyUI for the menu, and the Dawnguard DLC. Dragonborn is optional: with it, Solstheim gets its own squads.
Creation Club content is used when you have it.

**Patrol mods.** Two popular mods also put soldiers on the roads:
**Immersive Patrols SE** and **Skyrim at War Reborn**. A-Life detects them and makes room.

| | Immersive Patrols SE | Skyrim at War Reborn |
|---|---|---|
| A-Life squads on screen | 2 | 2 |
| A-Life people on screen | 12 | 9 |
| A-Life population | x0.85 | x0.6 |
| A-Life road patrols | x0.6 | x0.5 |

With both loaded, the smaller on-screen limits apply (2 squads, 9 people), and the population and patrol cuts combine
(population x0.51, road patrols x0.3).

- With Skyrim at War, A-Life leaves the forts to that mod's garrisons. A-Life squads do not try to take them, so there
  are no double garrisons and no endless fights there.
- While either mod is loaded, A-Life's Stormcloaks fight that mod's Legion soldiers, A-Life's Legion walks beside them,
  and A-Life's bandits fight both.
- Your A-Life companions use the same on-screen room and come first. With a small setting (for example "1 squad" and one
  companion squad), no other A-Life squad comes on screen. That is intended.

**Vanilla followers.** Lydia, hirelings and other followers are not affected and can travel with your A-Life companions.

**Other NPCs.** A-Life's dialogue only applies to A-Life's own people. No other NPC's dialogue is changed.

## Known limits

<!-- Sources: docs/CAMPLIFE.md section 6 (stories as on-screen lines, no music, offline rest not done, not run in game);
     docs/DRAGONS.md (status: no game run; no Solstheim dragons; no goodwill or bounty for dragons, section 5);
     docs/WOUNDED.md section 4 (no stash on a rob: not implemented); docs/TASKS.md sections 4 and 8 (no time limit by
     default, no escort ambushes); docs/COMPANIONS.md section 1 (squads held by a job do not join); docs/COMPAT.md
     (detection patterns not checked against the downloads, the third mod not read yet, no in-game check);
     docs/SOLSTHEIM.md "Races" (few Dunmer pool actors); docs/README.md "Uninstall"; docs/CHANGELOG.md "Not in 2.1.0"
     (emissions); docs/TESTLOG.md (which features have a passing in-game run). Old saves: W1 lab-0715 shows v207 / v206 /
     v205 saves loading their squads (pass); standing is read from those saves by SaveFormat::ReadStanding (goodwill table,
     notoriety, rumours; called from the plugin's save loader), checked headless only. News is not claimed: the news
     record came with M5g, after 2.0.0 (docs/CHANGELOG.md). -->

- **Not yet tested in the game:** the new job types (escort, defend, rescue, recover, the hunt), the wounded and
  surrender system, stashes, camp life, dragons, and play alongside patrol mods. They pass the mod's own automated
  tests, but nobody has played them in the real game yet.
- Camp stories show as on-screen text, not spoken lines. There is no music at the campfire yet.
  Staying at a camp does not yet help squads rest while you are away.
- Robbing a squad that surrendered gives gold and weapons, but no stash yet.
- Killing a dragon does not change how any faction sees you, and no job asks you to kill one.
- There are no A-Life dragons on Solstheim.
- Escorts have no ambushes on the way.
- A squad that is part of one of your jobs (a bounty target, a delivery recipient, a place you must clear) will not join
  you as a companion.
- Patrol mod detection has not been checked against every version of those mods. A third faction mod (with commanders
  and location capture) is not supported yet.
- On Solstheim, Raven Rock's guards and the island's many Dunmer can use up the available Dunmer faces. A few people may
  then appear as another race.
- Anomaly's emissions (blowouts) are not part of A-Life for Skyrim.
- To remove the mod, always use **Prepare to uninstall** first, then save, quit and remove it.

Saves from A-Life 2.0 load in 2.1, with their squads (checked in game). Your standing carries over too.
