// Civil War progress and faction gates (docs/CIVILWAR.md): the board's side of T10. The host reads the vanilla war (CWOwner keyword
// data on the hold locations, CWObj stage 255) and the gate quests, and tells the board; everything here is engine-free.
#include <algorithm>
#include <cstdarg>
#include <cstdio>

#include "ALife/Board.h"

namespace ALife {

namespace {
	constexpr std::uint8_t kController = 254;   // RespawnParam::faction: the smart's current controlling faction (as in Board.cpp / Config.cpp)

	std::string Fmt(const char* a_fmt, ...)
	{
		char b[512];
		va_list ap;
		va_start(ap, a_fmt);
		std::vsnprintf(b, sizeof(b), a_fmt, ap);
		va_end(ap);
		return b;
	}
}

void Board::ResetFactionState()
{
	m_holdOwner.clear();
	m_cwLoser = kNoFaction;
	m_gated.fill(false);
	for (auto& [hold, f] : m_cfg.holdFaction) AimHold(hold);
}

std::uint8_t Board::HoldOwner(FormID a_hold) const
{
	auto start = m_cfg.holdFaction.find(a_hold);
	if (start == m_cfg.holdFaction.end()) return kNoFaction;
	auto it = m_holdOwner.find(a_hold);
	return it != m_holdOwner.end() ? it->second : std::uint8_t(start->second);
}

std::uint8_t Board::EffectiveOwner(FormID a_hold) const
{
	const std::uint8_t owner = HoldOwner(a_hold);
	if (owner == kNoFaction || !m_cfg.civilWar) return owner;
	if (m_cwLoser != kNoFaction) return m_cfg.CwOpponent(m_cwLoser);   // the winner takes every hold
	return owner;
}

bool Board::Blocked(Faction a_f, int a_ws) const
{
	if (m_gated[int(a_f)]) return true;
	return m_cwLoser == std::uint8_t(a_f) && a_ws >= 0 && a_ws < kWorldspaces && m_cfg.standDownWs[std::size_t(a_ws)];
}

// The hold's places follow its effective owner: the "owner" priority bonus, the owner respawn entries and, when one of the two
// sides held it, the controller (a fort the bandits took stays theirs).
void Board::AimHold(FormID a_hold)
{
	const std::uint8_t owner = EffectiveOwner(a_hold);
	if (owner == kNoFaction || owner >= kFactionCount) return;
	for (std::size_t i = 0; i < m_world.Size(); ++i) {
		GraphNode& n = m_world.nodes[i];
		if (!n.holdAligned || n.hold != a_hold) continue;
		if (n.defaultFaction != owner) {
			if (n.defaultFaction < kFactionCount) n.props.byFaction[n.defaultFaction] -= n.ownerBonus;
			n.props.byFaction[owner] += n.ownerBonus;
			for (auto& rp : n.respawn)
				if (rp.owner) rp.faction = owner;
			n.defaultFaction = owner;
		}
		if (i < smart.size()) {
			SmartState& st = smart[i];
			if (st.faction != owner && (st.faction == kNoFaction || m_cfg.IsCwSide(st.faction))) st.faction = owner;
		}
	}
}

bool Board::SetHoldOwner(FormID a_hold, Faction a_f)
{
	if (!m_cfg.civilWar || !m_cfg.IsCwSide(std::uint8_t(a_f))) return false;
	const std::uint8_t prev = HoldOwner(a_hold);
	if (prev == kNoFaction || prev == std::uint8_t(a_f)) return false;
	m_holdOwner[a_hold] = std::uint8_t(a_f);
	AimHold(a_hold);
	stats.holdFlips++;
	const GraphNode* where = nullptr;   // the hold's capital, else any of its places
	for (auto& n : m_world.nodes)
		if (n.hold == a_hold && (!where || (n.kind == Kind::City && where->kind != Kind::City))) where = &n;
	const std::string hold = where && !where->holdName.empty() ? where->holdName : Fmt("hold %X", a_hold);
	m_host.Log(Fmt("[cw] %s: %s -> %s", hold.c_str(), FactionId(Faction(prev)), FactionId(a_f)));
	if (m_cwLoser == kNoFaction && where)
		m_host.News(Fmt("%s now holds %s: its garrisons change their colours", FactionName(a_f), hold.c_str()), where->x, where->y);
	return true;
}

void Board::SetWarOver(std::uint8_t a_loser)
{
	if (!m_cfg.civilWar || (a_loser != kNoFaction && !m_cfg.IsCwSide(a_loser))) return;
	if (m_cwLoser == a_loser) return;
	m_cwLoser = a_loser;
	for (auto& [hold, f] : m_cfg.holdFaction) AimHold(hold);
	if (a_loser == kNoFaction) return;
	const std::uint8_t winner = m_cfg.CwOpponent(a_loser);
	m_host.Log(Fmt("[cw] war over: %s won, %s stands down", FactionId(Faction(winner)), FactionId(Faction(a_loser))));
	m_host.News(Fmt("The war is over. %s holds Skyrim; the last bands of the %s lay down their arms", FactionName(Faction(winner)),
	                FactionName(Faction(a_loser))), 0.f, 0.f);
}

void Board::SetGated(Faction a_f, bool a_shut)
{
	if (m_gated[int(a_f)] == a_shut) return;
	m_gated[int(a_f)] = a_shut;
	m_host.Log(Fmt("[gate] %s %s", FactionId(a_f), a_shut ? "shut: no squads" : "open"));
	if (!a_shut) m_host.News(a_f == Faction::Dragon ? std::string("Word on the roads: dragons have been seen in the skies again")
	                                                : Fmt("Word on the roads: the %s is mustering again", FactionName(a_f)), 0.f, 0.f);
}

// A hold guard whose place now belongs to the other side: the new side's squad from the same respawn entry takes over its spot
// and its business (same place, same plan), the old one goes. Returns true when it swapped.
bool Board::SwapGuard(std::size_t a_index)
{
	const Group& g = squads[a_index];
	if (!m_cfg.IsCwSide(std::uint8_t(g.faction)) || g.respawnSmart < 0 || std::size_t(g.respawnSmart) >= m_world.Size()) return false;
	const auto smartIdx = std::uint32_t(g.respawnSmart);
	const GraphNode& n = m_world.nodes[smartIdx];
	if (!n.holdAligned) return false;
	if (std::find(m_cfg.guardEntries.begin(), m_cfg.guardEntries.end(), g.respawnEntry) == m_cfg.guardEntries.end()) return false;
	std::size_t k = 0;
	while (k < n.respawn.size() && n.respawn[k].name != g.respawnEntry) ++k;
	if (k == n.respawn.size()) return false;
	const RespawnParam& rp = n.respawn[k];
	// a hold place's guard posts are its owner's, even while outlaws sit in the fort (the garrison comes back for it)
	if (!rp.owner && rp.faction != kController) return false;
	const std::uint8_t target = n.defaultFaction;
	if (!m_cfg.IsCwSide(target) || target == std::uint8_t(g.faction) || Blocked(Faction(target), n.ws)) return false;
	const std::string from = FactionId(g.faction);
	if (g.section.rfind(from, 0) != 0) return false;
	const std::string section = std::string(FactionId(Faction(target))) + g.section.substr(from.size());   // imperial_veteran -> stormcloak_veteran
	if (!m_cfg.Squad(section)) return false;

	const Group old = g;   // copy: CreateSquad grows the vector
	RemoveSquad(squads[a_index], "guard swap");
	Group* ng = CreateSquad(smartIdx, section, false);
	stats.guardsSwapped++;
	m_host.Log(Fmt("[cw] guards at %s: %s #%llu -> %s #%llu", PlaceName(smartIdx).c_str(), old.section.c_str(), (unsigned long long)old.id,
	               section.c_str(), (unsigned long long)(ng ? ng->id : 0)));
	if (!ng) return true;   // the slot is free again: the place respawns its new owner's guards later
	ng->x = old.x; ng->y = old.y; ng->z = old.z;
	ng->node = old.node; ng->prevNode = old.prevNode;
	ng->action = old.action;
	ng->assignedTarget = old.assignedTarget; ng->currentTarget = old.currentTarget;
	ng->stayStart = old.stayStart; ng->idleHours = old.idleHours;
	ng->respawnSmart = smartIdx;
	ng->respawnEntry = old.respawnEntry;
	if (k < smart[smartIdx].spawned.size()) ++smart[smartIdx].spawned[k];
	if (old.smartAssigned >= 0) AssignToSmart(*ng, old.smartAssigned);
	return true;
}

int Board::ApplyFactionState()
{
	int touched = 0;
	const std::size_t count = squads.size();   // swaps append their replacement: those are not looked at again
	for (std::size_t i = 0; i < count; ++i) {
		if (squads[i].online || squads[i].members.empty()) continue;
		if (m_cfg.civilWar && SwapGuard(i)) { ++touched; continue; }
		Group& g = squads[i];
		if (!Blocked(g.faction, g.ws)) continue;
		m_host.Log(Fmt("[cw] %s #%llu stands down (%s)", g.section.c_str(), (unsigned long long)g.id,
		               m_gated[int(g.faction)] ? "faction not founded yet" : "lost the war"));
		RemoveSquad(g, "stood down");
		stats.stoodDown++;
		++touched;
	}
	return touched;
}

}  // namespace ALife
