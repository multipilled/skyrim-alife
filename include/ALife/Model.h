#pragma once
// Plugin-side bridge to the dependency-free simulation core (ALife/Core.h). Everything that needs RE:: lives elsewhere.
#include "ALife/PCH.h"
#include "ALife/Core.h"

namespace ALife {

// Skill index (Core.h enum Skill) -> engine actor value.
RE::ActorValue SkillAV(int a_skill);

}  // namespace ALife
