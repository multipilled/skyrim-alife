#pragma once
// Jobs: the Skyrim side of the task model (docs/TASKS.md, core in Tasks.h). Publishes ALifeTaskOffer / ALifeTaskState for
// the squad the player is talking to, turns the player's dialogue choices into accepted / handed-in / delivered / abandoned
// jobs, shows them as objectives of the ALifeTasks quest with markers that follow the squads, pays rewards, and saves.
#include "ALife/Tasks.h"
#include "ALife/Talk.h"

namespace ALife::Jobs {
void Init();                                   // kDataLoaded (after Talk::Init): tasks.ltx, forms of ALifeTalk.esp
bool Ready();
void Reset();                                  // new game / before a load: forget every job (the game's own quest state belongs to the save)

// The globals for the squad the dialogue is about (nullptr: none). Called with the sim lock held.
void PublishFor(const Group* a_g);
// A job choice was made in the dialogue (applied once the menu has closed).
void OnChoice(std::uint64_t a_squad, Talk::Choice a_choice);
bool Holds(std::uint64_t a_squad);             // an open job's target / recipient / assault holder (it will not join the player)
// Main thread, sim lock held: progress, markers, rewards.
void Tick(float a_realDt);

// Co-save record 'TASK' (written after 'SIM2').
void SaveTo(SKSE::SerializationInterface* a_intfc);
void LoadFrom(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);

// Debug bridge: "task offer <type>", "task accept", "task list", "task kill", "task goto target|giver", "task talk".
void Debug(const std::string& a_args);
}
