#include "ALife/Model.h"

namespace ALife {
RE::ActorValue SkillAV(int a_skill)
{
	return static_cast<RE::ActorValue>(static_cast<int>(RE::ActorValue::kOneHanded) + a_skill);
}
}
