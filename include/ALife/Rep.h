#pragma once
// Plugin side of the standing system: reads the player's state from the game (allegiances, status, crime gold, race,
// gear, strength) for the engine-free model in Standing.h, and puts its verdicts on screen.
#include <functional>

#include "ALife/Standing.h"
#include "ALife/Systems.h"

namespace ALife::Rep {
void Init();                                     // kDataLoaded: resolve forms, bind the model to the loaded config
void Reset();                                    // new game / load: forget everything
ALife::Standing& S();
const PlayerContext& Context();
void Refresh(bool a_force = false);              // read the player (rate limited)

Site SiteOf(const Group& a_g);                   // hold the squad stands in and whether its faction is the law there
float SquadPower(const Group& a_g);              // living members, current health weighted
Regard RegardOf(const Group& a_g);               // how this squad regards the player right now
float Attitude(Faction a_f);                     // faction-wide attitude toward the player (no squad memory)
bool IsOutlaw(Faction a_f);

// deeds, called from the event sinks
void OnPlayerStruck(Group& a_g);                 // first blow of the player against a squad that was not hostile
void OnPlayerKilled(Group& a_g, Member& a_victim);
void OnPlayerHelped(const Group& a_victimGroup, const Member& a_victim);
void Tick(double a_nowHours, double a_dtHours);

// News feed: every event the board reports (not only the nearby ones that raise a toast), newest first
void PushNews(const std::string& a_line, double a_nowHours);
int NewsCount();
std::string NewsLine(int a_index);
std::vector<std::string> NewsAll();                          // newest first, stamped (co-save)
void RestoreNews(const std::vector<std::string>& a_lines);   // after a load

// MCM (Papyrus runs on its own thread: the pages read a cache the main thread refreshes every couple of seconds)
void PumpMenuCache(float a_realDt);
int MenuCount();
std::string MenuName(int a_index);               // "Imperial Legion"
std::string MenuValue(int a_index);              // "Neutral (+42)"
std::string MenuStatus();                        // notoriety, bounty
}
