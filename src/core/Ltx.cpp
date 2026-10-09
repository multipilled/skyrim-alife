#include "ALife/Ltx.h"
#include "ALife/Util.h"

#include <fstream>
#include <sstream>

namespace ALife {

namespace {
	std::string StripComment(const std::string& line)
	{
		// ';' starts a comment unless inside double quotes
		bool q = false;
		for (std::size_t i = 0; i < line.size(); ++i) {
			if (line[i] == '"') q = !q;
			else if (line[i] == ';' && !q) return line.substr(0, i);
			else if (line[i] == '/' && !q && i + 1 < line.size() && line[i + 1] == '/') return line.substr(0, i);
		}
		return line;
	}

	std::string DirOf(const std::string& path)
	{
		auto p = path.find_last_of("/\\");
		return p == std::string::npos ? std::string() : path.substr(0, p + 1);
	}
}

bool Ltx::LoadFile(const std::string& a_path, std::string* a_err)
{
	std::ifstream in(a_path, std::ios::binary);
	if (!in) {
		if (a_err) *a_err = "cannot open " + a_path;
		return false;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	return LoadString(ss.str(), DirOf(a_path), a_err);
}

bool Ltx::LoadString(const std::string& a_text, const std::string& a_dir, std::string* a_err)
{
	std::string cur;
	std::istringstream is(a_text);
	std::string raw;
	bool first = true;
	while (std::getline(is, raw)) {
		if (first && raw.size() >= 3 && (unsigned char)raw[0] == 0xEF) raw.erase(0, 3);   // UTF-8 BOM
		first = false;
		std::string line = Trim(StripComment(raw));
		if (line.empty()) continue;
		if (line[0] == '#') {
			if (line.rfind("#include", 0) == 0) {
				auto q1 = line.find('"');
				auto q2 = line.rfind('"');
				if (q1 != std::string::npos && q2 > q1) {
					std::string err;
					if (!LoadFile(a_dir + line.substr(q1 + 1, q2 - q1 - 1), &err) && a_err) *a_err += err + "\n";
				}
			}
			continue;
		}
		if (line[0] == '[') {
			auto close = line.find(']');
			if (close == std::string::npos) {
				if (a_err) *a_err += "bad section header: " + line + "\n";
				continue;
			}
			cur = Trim(line.substr(1, close - 1));
			auto& sec = m_sections[cur];
			if (std::find(m_order.begin(), m_order.end(), cur) == m_order.end()) m_order.push_back(cur);
			auto rest = Trim(line.substr(close + 1));
			if (!rest.empty() && rest[0] == ':') {
				sec.parents.clear();
				for (auto& p : SplitList(rest.substr(1))) sec.parents.push_back(p);
			}
			continue;
		}
		if (cur.empty()) continue;
		auto eq = line.find('=');
		std::string key = eq == std::string::npos ? line : Trim(line.substr(0, eq));
		std::string val = eq == std::string::npos ? std::string() : Trim(line.substr(eq + 1));
		if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
		m_sections[cur].lines.emplace_back(std::move(key), std::move(val));
	}
	m_resolved.clear();
	return true;
}

void Ltx::Resolve(const std::string& a_name, std::vector<std::string>& a_stack, Lines& a_out) const
{
	if (std::find(a_stack.begin(), a_stack.end(), a_name) != a_stack.end()) return;   // inheritance cycle
	auto it = m_sections.find(a_name);
	if (it == m_sections.end()) return;
	a_stack.push_back(a_name);
	for (auto& p : it->second.parents) Resolve(p, a_stack, a_out);
	for (auto& kv : it->second.lines) {
		bool replaced = false;
		for (auto& ex : a_out)
			if (ex.first == kv.first) { ex.second = kv.second; replaced = true; break; }
		if (!replaced) a_out.push_back(kv);
	}
	a_stack.pop_back();
}

const Ltx::Lines* Ltx::Section(const std::string& a_section) const
{
	auto r = m_resolved.find(a_section);
	if (r != m_resolved.end()) return &r->second;
	if (!Has(a_section)) return nullptr;
	Lines out;
	std::vector<std::string> stack;
	Resolve(a_section, stack, out);
	return &(m_resolved[a_section] = std::move(out));
}

bool Ltx::Has(const std::string& a_section, const std::string& a_key) const
{
	auto* s = Section(a_section);
	if (!s) return false;
	for (auto& kv : *s) if (kv.first == a_key) return true;
	return false;
}

std::string Ltx::Get(const std::string& a_section, const std::string& a_key, const std::string& a_def) const
{
	auto* s = Section(a_section);
	if (!s) return a_def;
	for (auto& kv : *s) if (kv.first == a_key) return kv.second;
	return a_def;
}

float Ltx::GetF(const std::string& a_section, const std::string& a_key, float a_def) const
{
	auto v = Get(a_section, a_key);
	if (v.empty()) return a_def;
	try { return std::stof(v); } catch (...) { return a_def; }
}

int Ltx::GetI(const std::string& a_section, const std::string& a_key, int a_def) const
{
	auto v = Get(a_section, a_key);
	if (v.empty()) return a_def;
	try { return int(std::stof(v)); } catch (...) { return a_def; }
}

bool Ltx::GetB(const std::string& a_section, const std::string& a_key, bool a_def) const
{
	auto v = Get(a_section, a_key);
	if (v.empty()) return a_def;
	return v == "true" || v == "on" || v == "yes" || v == "1";
}

std::vector<std::string> Ltx::List(const std::string& a_section, const std::string& a_key) const
{
	return SplitList(Get(a_section, a_key));
}

}  // namespace ALife
