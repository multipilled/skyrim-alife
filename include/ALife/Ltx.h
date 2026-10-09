#pragma once
// Reader for Anomaly's .ltx config dialect: [section]:parent1,parent2 / key = value ; comment / #include "file".
// Child sections override inherited keys. Dependency-free (no RE headers).
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ALife {

class Ltx
{
public:
	using Lines = std::vector<std::pair<std::string, std::string>>;

	bool LoadFile(const std::string& a_path, std::string* a_err = nullptr);
	bool LoadString(const std::string& a_text, const std::string& a_dir = {}, std::string* a_err = nullptr);

	bool Has(const std::string& a_section) const { return m_sections.count(a_section) != 0; }
	bool Has(const std::string& a_section, const std::string& a_key) const;
	const Lines* Section(const std::string& a_section) const;       // resolved with inheritance; nullptr when missing
	std::vector<std::string> Sections() const { return m_order; }

	std::string Get(const std::string& a_section, const std::string& a_key, const std::string& a_def = {}) const;
	float GetF(const std::string& a_section, const std::string& a_key, float a_def = 0.f) const;
	int GetI(const std::string& a_section, const std::string& a_key, int a_def = 0) const;
	bool GetB(const std::string& a_section, const std::string& a_key, bool a_def = false) const;
	std::vector<std::string> List(const std::string& a_section, const std::string& a_key) const;   // comma separated value

private:
	struct Raw
	{
		std::vector<std::string> parents;
		Lines lines;   // own lines only, file order
	};
	void Resolve(const std::string& a_name, std::vector<std::string>& a_stack, Lines& a_out) const;
	void Finalize();

	std::map<std::string, Raw> m_sections;
	std::vector<std::string> m_order;
	mutable std::map<std::string, Lines> m_resolved;
};

}  // namespace ALife
