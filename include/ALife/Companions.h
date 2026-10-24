#pragma once
// Companions: the Skyrim side of the party model (docs/COMPANIONS.md, core in Party.h). Publishes the companion globals for the
// squad the player is talking to, turns the dialogue choices into recruiting, hiring, orders and dismissal, keeps companion squads
// on screen beside the player (follow / wait / move packages, teammate flags, teleport, hold fire, looting, gear), and saves.
#include "ALife/Party.h"
#include "ALife/Talk.h"

namespace ALife::Companions {
void Init();                                   // kDataLoaded (after Talk::Init): companions.ltx, the globals of ALifeTalk.esp
bool Ready();
void Reset();                                  // new game / before a load

bool Has(std::uint64_t a_squad);               // travelling with the player (the online layer leaves it alone)
Party* Model();                                // the party model: the job layer puts job companions in it (an escort, a freed captive)
void ApplyJobEvents(const std::vector<PartyEvent>& a_ev);   // joins / departures the job layer caused (sim lock held, main thread)
bool HoldsFire(std::uint64_t a_squad);
bool Loots(std::uint64_t a_squad);
int SlotsWanted();                             // following companion squads not on screen yet (after a load): the online layer keeps their slots free

// The globals for the squad the dialogue is about. a_mode is the talk mode the squad was given (kGreet: recruiting topics,
// kCompanion: orders). Called with the sim lock held.
void PublishFor(const Group* a_g, int a_mode);
// A companion choice was made (applied once the menu has closed); a_speaker = the member spoken to.
void OnChoice(std::uint64_t a_squad, Talk::Choice a_choice, RE::Actor* a_speaker);
// Main thread, sim lock held: contracts, packages, teleport, hold fire.
void Tick(float a_realDt);
// Back to the player's last order (after looting, or whenever the online layer would have re-driven the squad).
void Redrive(Group& a_g);
// An actor leaves the screen or is reused: no teammate flag left behind.
void Unteam(RE::Actor* a_a);

// Co-save record 'COMP' (written after 'TASK'). BeforeSave reads each companion's pack back from its actor first.
void BeforeSave();
void SaveTo(SKSE::SerializationInterface* a_intfc);
void LoadFrom(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);

// Debug bridge: "comp list|like <n>|willing|recruit [days]|order <o>|talk [n]|foe|fight|away".
void Debug(const std::string& a_args);
}
