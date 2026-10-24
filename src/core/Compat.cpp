#include "ALife/Compat.h"

#include <algorithm>
#include <cctype>

#include "ALife/Config.h"

namespace ALife {

namespace {
	std::string Lower(std::string s)
	{
		for (auto& c : s) c = char(std::tolower((unsigned char)c));
		return s;
	}

	bool Glob(const char* p, const char* s)
	{
		// iterative '*' matcher with backtracking to the last star
		const char *star = nullptr, *ss = s;
		while (*s) {
			if (*p == '*') { star = p++; ss = s; continue; }
			if (*p && *p == *s) { ++p; ++s; continue; }
			if (star) { p = star + 1; s = ++ss; continue; }
			return false;
		}
		while (*p == '*') ++p;
		return *p == 0;
	}

	std::vector<std::string> NonEmpty(std::vector<std::string> v)
	{
		v.erase(std::remove_if(v.begin(), v.end(), [](const std::string& s) { return s.empty(); }), v.end());
		return v;
	}

	std::vector<FormID> Ids(const Ltx& a_ltx, const std::string& a_sec, const std::string& a_key, std::string* a_err)
	{
		std::vector<FormID> out;
		for (auto& s : NonEmpty(a_ltx.List(a_sec, a_key))) {
			try { out.push_back(FormID(std::stoul(s, nullptr, 16))); }
			catch (...) { if (a_err) *a_err += "compat.ltx [" + a_sec + "] " + a_key + ": bad FormID '" + s + "'; "; }
		}
		return out;
	}
}

bool PluginMatches(const std::string& a_pattern, const std::string& a_file)
{
	const std::string p = Lower(a_pattern), f = Lower(a_file);
	return !p.empty() && Glob(p.c_str(), f.c_str());
}

void LoadCompat(const Ltx& x, CompatConfig& c, std::string* a_err)
{
	c = CompatConfig{};
	c.force = NonEmpty(x.List("compat", "force"));
	for (auto& s : c.force) s = Lower(s);
	c.ownerHold = Lower(x.Get("compat", "handover_owner", "hold")) == "hold";
	c.handoverPopulation = std::max(0, x.GetI("compat", "handover_population", 1));
	for (auto& id : NonEmpty(x.List("compat", "mods"))) {
		const std::string sec = "compat_" + id;
		if (!x.Has(sec)) {
			if (a_err) *a_err += "compat.ltx: no [" + sec + "]; ";
			continue;
		}
		CompatMod m;
		m.id = Lower(id);
		m.name = x.Get(sec, "name", id);
		m.plugins = NonEmpty(x.List(sec, "plugins"));
		for (auto& k : NonEmpty(x.List(sec, "handover_kinds"))) {
			bool ok = false;
			const Kind kind = KindFromName(Lower(k), &ok);
			if (ok) m.handoverKinds.push_back(kind);
			else if (a_err) *a_err += "compat.ltx [" + sec + "] handover_kinds: unknown kind '" + k + "'; ";
		}
		m.handover = Ids(x, sec, "handover", a_err);
		m.keep = Ids(x, sec, "keep", a_err);
		m.onlineGroups = std::clamp(x.GetI(sec, "online_groups", kMaxOnlineGroups), 1, kMaxOnlineGroups);
		m.onlineActors = std::clamp(x.GetI(sec, "online_actors", kMaxOnlineActors), kCompatActorsPerGroup, kMaxOnlineActors);
		m.population = std::clamp(x.GetF(sec, "population", 1.f), kCompatMinPopulation, 1.f);
		m.patrolScale = std::clamp(x.GetF(sec, "patrol_scale", 1.f), 0.f, 1.f);
		c.mods.push_back(std::move(m));
	}
	// [compat_factions] <vanilla faction hex> = <ALife faction id>
	if (auto* fm = x.Section("compat_factions")) {
		for (auto& [key, val] : *fm) {
			FormID id = 0;
			try { id = FormID(std::stoul(key, nullptr, 16)); } catch (...) { if (a_err) *a_err += "compat.ltx [compat_factions]: bad FormID '" + key + "'; "; continue; }
			bool ok = false;
			const Faction f = FactionFromId(Lower(val), &ok);
			if (ok && IsHumanFaction(f)) c.factionMap.emplace_back(id, f);
			else if (a_err) *a_err += "compat.ltx [compat_factions]: unknown faction '" + val + "'; ";
		}
	}
}

bool CompatState::Detected(const std::string& a_id) const
{
	return std::find(detected.begin(), detected.end(), Lower(a_id)) != detected.end();
}

bool CompatState::HandsOver(const GraphNode& a_n) const
{
	if (!handover || keep.count(a_n.location)) return false;
	return places.count(a_n.location) || std::find(kinds.begin(), kinds.end(), a_n.kind) != kinds.end();
}

void ApplyCompatBudget(const CompatConfig& a_cfg, CompatState& s, int a_budget)
{
	s.onlineGroups = kMaxOnlineGroups;
	s.onlineActors = kMaxOnlineActors;
	s.population = 1.f;
	s.patrolScale = 1.f;
	if (!s.Active() || a_budget == kCompatBudgetOff) return;
	for (auto& m : a_cfg.mods) {
		if (!s.Detected(m.id)) continue;
		s.onlineGroups = std::min(s.onlineGroups, m.onlineGroups);
		s.onlineActors = std::min(s.onlineActors, m.onlineActors);
		s.population *= m.population;
		s.patrolScale *= m.patrolScale;
	}
	s.population = std::max(s.population, kCompatMinPopulation);
	if (a_budget > 0) {   // MCM: a fixed number of groups, five actors each
		s.onlineGroups = std::clamp(a_budget, 1, kMaxOnlineGroups);
		s.onlineActors = std::min(kMaxOnlineActors, s.onlineGroups * kCompatActorsPerGroup);
	}
	s.onlineGroups = std::clamp(s.onlineGroups, 1, kMaxOnlineGroups);
	s.onlineActors = std::clamp(s.onlineActors, kCompatActorsPerGroup, kMaxOnlineActors);
}

CompatState ResolveCompat(const CompatConfig& a_cfg, const std::vector<std::string>& a_loaded, int a_budget, bool a_handover)
{
	CompatState s;
	s.ownerHold = a_cfg.ownerHold;
	s.handoverPopulation = a_cfg.handoverPopulation;
	for (auto& m : a_cfg.mods) {
		std::string hit;
		if (std::find(a_cfg.force.begin(), a_cfg.force.end(), m.id) != a_cfg.force.end()) hit = "force";
		for (auto& pat : m.plugins) {
			if (!hit.empty()) break;
			for (auto& file : a_loaded)
				if (PluginMatches(pat, file)) { hit = file; break; }
		}
		if (hit.empty()) continue;
		s.detected.push_back(m.id);
		s.matched.push_back(hit);
		for (auto k : m.handoverKinds)
			if (std::find(s.kinds.begin(), s.kinds.end(), k) == s.kinds.end()) s.kinds.push_back(k);
		s.places.insert(m.handover.begin(), m.handover.end());
		s.keep.insert(m.keep.begin(), m.keep.end());
	}
	s.handover = a_handover && s.Active() && (!s.kinds.empty() || !s.places.empty());
	if (s.Active()) s.factionMap = a_cfg.factionMap;
	ApplyCompatBudget(a_cfg, s, a_budget);
	return s;
}

void ApplyHandover(const Config& a_cfg, GraphNode& n)
{
	const CompatState& s = a_cfg.compat;
	n.handedOver = false;
	if (!s.HandsOver(n)) return;
	n.handedOver = true;
	if (s.ownerHold) {
		auto it = a_cfg.holdFaction.find(n.hold);
		if (it != a_cfg.holdFaction.end()) n.defaultFaction = std::uint8_t(it->second);
		else if (n.ws < kWorldspaces && a_cfg.wsFaction[n.ws] != kNoFaction) n.defaultFaction = a_cfg.wsFaction[n.ws];
	}
	n.respawn.clear();
	n.factionControlled = false;
	n.props.all = 0.f;
	n.props.monster = 0.f;
	n.props.lair = 0.f;
	const std::uint8_t owner = n.defaultFaction;
	for (int f = 0; f < kFactionCount; ++f) {
		const bool friendly = owner != kNoFaction && (f == owner || a_cfg.rel[f][owner] >= 1000);
		if (!friendly) n.props.byFaction[f] = 0.f;
	}
	n.maxPopulation = std::min(n.maxPopulation, s.handoverPopulation);
}

std::vector<CompatReaction> CompatReactions(const Config& a_cfg)
{
	std::vector<CompatReaction> out;
	for (auto& [ext, target] : a_cfg.compat.factionMap) {
		for (int f = 0; f < kHumanFactionCount; ++f) {
			const int r = a_cfg.rel[f][int(target)];
			if (f == int(target) || r >= 1000) out.push_back({ Faction(f), ext, true });
			else if (r <= -1000) out.push_back({ Faction(f), ext, false });
		}
	}
	return out;
}

}  // namespace ALife
