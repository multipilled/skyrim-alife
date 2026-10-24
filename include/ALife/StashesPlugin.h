#pragma once
// Stashes, the Skyrim side (docs/STASHES.md, core in Stashes.h): places the containers near the spots when the player comes
// close, fills them, aims the ALifeStashes markers, puts stash notes on the dead and reads the ones the player takes, pays jobs
// with a stash, notices looting, saves the record STSH and answers the "stash" debug verbs. Named StashPlugin so the core
// class keeps the name Stashes.
#include "ALife/Stashes.h"
#include "ALife/Tasks.h"

namespace ALife::StashPlugin {
void Init();                                   // kDataLoaded, after Talk::Init and the board: stashes.ltx, the note and the quest of ALifeTalk.esp
bool Ready();
void Reset();                                  // new game / before a load (the spots are built on the next tick)
Stashes& S();

// Main thread, sim lock held: build the spots once, place and fill containers near the player, markers, queued notes and pickups.
void Tick(float a_realDt);
// A member died on screen (Sim::OnActorKilled): a note may go onto its body (next tick).
void OnActorKilled(Group& a_g, Member& a_m, RE::Actor* a_victim);
// A member died off screen (Host::OnMemberKilled): a note may pass to a living member of the killer squad.
void OnOfflineKill(Group& a_g, Member& a_m, Group* a_killer);
// TESContainerChangedEvent (any thread): queued; a note into the player's pack is read, the first item out of a stash loots it.
void OnContainerChanged(const RE::TESContainerChangedEvent* a_e);
// A job is done (Jobs): with [reward] chance it pays with a stash near the giver; a_gold may become 0.
void OnTaskDone(std::int32_t& a_gold, const Task& a_t);

// Co-save record 'STSH' (written after 'WOND'); a save without it builds the spots afresh.
void SaveTo(SKSE::SerializationInterface* a_intfc);
void LoadFrom(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);

// Debug bridge "stash": list | reveal | note | place | reward
void Debug(const std::string& a_args);
}
