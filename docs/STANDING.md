# Standing: how Skyrim's people decide what you are to them

A-Life squads do not roll a die when they meet you. Every squad works out an **attitude** toward you from what it can
know about you, the way Anomaly's relation registry does (community goodwill + personal goodwill + rank + reputation),
but with Skyrim's reasons: who you swore to, what you are, what you wear, what you owe the law, what you have done
to its people, and how dangerous you look.

## Attitude and stance

    attitude = base(faction, you)           lore rules over your context (allegiance, race, status, gear, notoriety, bounty)
             + goodwill(faction)            earned reputation, persistent, fades slowly
             + memory(squad)                what this squad itself has seen of you

| Tier | Attitude | What a squad does |
|---|---|---|
| Hostile | <= -1000 | attacks on sight |
| Unfriendly | -1000 .. -300 | wary. Lawful squads challenge; outlaws and predators weigh the odds; territorial ones defend their ground |
| Neutral | -300 .. +300 | goes about its business, ignores you, may nod |
| Friendly | +300 .. +1000 | greets you, will not turn on you if you draw steel, shares news |
| Allied | >= +1000 | fights beside you |

Nothing starts hostile by fiat. A fresh traveller is unfriendly to bandits, not their prey by default: whether a
bandit squad attacks depends on the odds (below). Hostility is *earned* (killing their people, standing with their
enemy, a bounty, being a vampire in front of a Dawnguard) or *chosen* by a predator that sees an easy mark.

## Where attitude comes from

1. **Base, by lore** (`standing.ltx`). Context rules add to a faction's baseline:
   - sworn to the Legion: the Legion warms to you, the Stormcloaks go cold (and the reverse). The Thalmor tolerate the Legion.
   - Dawnguard member / vampire clan member / vampire / werewolf: Dawnguard and Vigilants hate vampires; Silver Hand hates
     lycanthropes; vampires welcome their own. A transformed beast form frightens everyone.
   - Companions, College, Thieves Guild, Dark Brotherhood, Blades: small, believable nudges (outlaws respect the guilds of
     thieves and killers; mages trust the College).
   - race: Stormcloaks favour Nords and distrust elves; Forsworn favour Reachfolk; Thalmor favour Altmer.
   - gear: dressed in a faction's colours earns its trust and its enemies' suspicion. Mild: a disguise, not a passport.
   - bounty: soldiers who enforce the law of the hold they hold go hostile when you are wanted there (crime faction gold,
     the same number the guards read). Elsewhere it means nothing.
   - notoriety: lawful folk fear a murderer, outlaws respect one. Fame (level) earns lawful goodwill.
2. **Goodwill** (earned). Killing a faction's people costs its goodwill (more for veterans), half that with their allies,
   and earns a quarter of it with their enemies. Unwitnessed kills are discovered later (a patrol finds the bodies) and
   cost less. Killing a faction's enemy while its squads watch earns thanks. Goodwill fades: bad deeds are remembered
   longer than good ones.
3. **Squad memory**. A squad that you attacked remembers it, one you fought beside remembers that, one you paid a toll to
   leaves you alone for a while.

## Predators weigh the odds

Outlaw factions have an *opportunism* rating. An unfriendly outlaw squad attacks when it thinks it will win
(`advantage = squad strength / (your strength + companions)`), more readily when hungry, when you look rich, or at night
for vampires. If you look too strong it keeps its distance, watches, and leaves you be. Territorial factions (Forsworn,
necromancers, vampires, bandits in their camp) attack *intruders* in their place and ignore travellers on the road.

Law-abiding squads do not hunt you; they challenge (halt, warn) when you are armed and unfriendly, and escalate if you
stay or draw.

## Talking (quiet and optional)

Nothing opens a menu or pauses the game. A squad that wants a word walks up, stops, says one short line as an ordinary subtitle,
and waits. Talking to it is the player's choice, through Skyrim's own dialogue (`ALifeTalk.esp`):

- **Toll (bandits).** They stop you and name a price. In the dialogue: pay it, say you can't (a pauper is waved through), refuse, or
  dare them. If you ignore them they warn you once, then attack; drawing steel at the table ends it at once.
- **Challenge (soldiers, Thalmor, Forsworn, necromancers, vampires, the Dawnguard, the Silver Hand).** They halt an armed stranger
  they distrust. Say you mean no harm and they let you go on for a while; tell them you don't answer to them and it ends in blood.
- **Hello (everyone who doesn't regard you as an enemy).** Talk to a squad and they greet you in their own voice of speech and
  answer "what's the word on the road?".

The dialogue knows the situation because the plugin publishes it (two globals) for the squad under your crosshair; your choice is
read from the response the engine reports. Combat barks stay the game's own. Tier-change notices are off by default.

## Persistence

Goodwill, notoriety, pending rumours and each squad's memory are saved in the SKSE co-save.

## Files

- `config/standing.ltx`: baselines, dispositions, rules, deed values, decay.
- `config/factions.ltx`: who is allied or at war with whom (spillover and faction-versus-faction behaviour).
- `core/Standing.*`: engine-free model (tested headless: `tests/standing_test.cpp`).
- `src/Standing.cpp`: reads the player's state from the game (allegiance factions, crime gold, race, gear keywords).
