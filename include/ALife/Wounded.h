#pragma once
// Wounded: the Skyrim side of the wounded / help / finish / surrender model (docs/WOUNDED.md, core in Wounds.h). Puts down members
// into a bleedout kneel through actor flags (protected, no bleedout recovery, can speak while down) and clears them whenever a member
// gets up, dies or leaves the screen; runs the help calls, squadmates walking over to help, self-heal, enemies finishing, the yield
// and surrender checks against the player, the dialogue global and choices (talk modes 5 and 6), the co-save record WOND and the
// debug verbs. Nothing here is set on a base NPC: pool actors are reused.
#include "ALife/Talk.h"
#include "ALife/Wounds.h"

namespace ALife::Wounded {
void Init();                                   // kDataLoaded, after the board exists: wounded.ltx, ALifeWoundHeal (the hook waits for Attach)
// Hook Board::wounds once the board is populated (Sim::MainTick, every tick after the fill: the first populate, a load, a new
// game). Idempotent: attaches once per board and logs "[wound] ready" then.
void Attach();
bool Ready();
void Reset();                                  // new game / before a load: nobody down, every actor flag cleared
Wounds& W();

bool IsDown(std::uint64_t a_member);           // the online layer and Behavior skip the down (no fights, no packages)
// Talk mode for the member under the crosshair: Talk::kWounded (5), Talk::kYielded (6), or kNone when it is standing.
int ModeFor(const Group& a_g, const Member& a_m);
void Publish();                                // ALifeWoundHeal: 1 when the player carries a healing potion
void OnChoice(std::uint64_t a_squad, Talk::Choice a_choice, RE::Actor* a_speaker);   // applied after the menu has closed

// Main thread, sim lock held: the crossing roll, kneeling, calls, self-heal, help, finishing, yield / surrender, release.
void Tick(float a_realDt);
// An online member died (Sim::OnActorKilled): a down one killed by the player counts as finishing it (a yielded one as killing a
// prisoner); the record and the actor's flags go.
void OnMemberDied(Group& a_g, Member& a_m, RE::Actor* a_killer);
// A squad leaves the screen (Online::SendOffline, after the gear read-back): flags off every actor, the down keep >= 1 health.
void OnSendOffline(Group& a_g);

// Co-save record 'WOND' (written after 'COMP'); a save without it has nobody down.
void SaveTo(SKSE::SerializationInterface* a_intfc);
void LoadFrom(SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);

// Debug bridge "wound": list | down [n] | yield | check | help | rob | finish | spare | call | ocs
void Debug(const std::string& a_args);
}
