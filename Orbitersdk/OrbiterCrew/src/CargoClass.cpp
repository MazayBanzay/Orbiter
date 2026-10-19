// OrbiterCrew - cargo classes (see CargoClass.h).
#include "CargoClass.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace ocrew
{
	bool UiEnglish();   // CrewMember.cpp: the interface's language

	namespace
	{
		std::string Trim(const std::string& s)
		{
			const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
		}
		std::string Lower(std::string s) { for (char& ch : s) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a'); return s; }
	}

	const CargoClass& FindCargoClass(const std::string& name)
	{
		static std::map<std::string, CargoClass> cache;
		auto it = cache.find(name);
		if (it != cache.end()) return it->second;
		CargoClass c; c.name = name;
		// key = value lines; ';' starts a comment; the values may hold spaces (the labels)
		std::ifstream f("Config\\OrbiterCrew\\Cargo\\" + name + ".cfg");
		if (!f) { oapiWriteLogV("OrbiterCrew: cargo class %s - no Config\\OrbiterCrew\\Cargo\\%s.cfg", name.c_str(), name.c_str()); return cache.emplace(name, c).first->second; }
		std::string line;
		while (std::getline(f, line))
		{
			const size_t sc = line.find(';'); if (sc != std::string::npos) line.erase(sc);
			const size_t eq = line.find('='); if (eq == std::string::npos) continue;
			const std::string k = Lower(Trim(line.substr(0, eq))), v = Trim(line.substr(eq + 1));
			std::istringstream ss(v);
			if (k == "label") c.labelRu = v;
			else if (k == "label_en") c.labelEn = v;
			else if (k == "box")
			{
				const std::string b = Lower(v);
				const double e = b == "s" ? 0.75 : 0.5;   // the standard crates (CARGO_STANDARD.md)
				c.dims[0] = c.dims[1] = c.dims[2] = e;
				if (c.mesh.empty()) c.mesh = b == "s" ? "OrbiterCrew\\Crate_S" : "OrbiterCrew\\Crate_M";
			}
			else if (k == "dims") ss >> c.dims[0] >> c.dims[1] >> c.dims[2];
			else if (k == "tare") ss >> c.tare;
			else if (k == "mesh") c.mesh = v;
			else if (k == "resource") c.resource = Lower(v);
			else if (k == "resourcekg") ss >> c.resourceKg;
			else if (k == "resourcecap") ss >> c.resourceCap;
			else if (k == "unpack") c.unpack = v;
		}
		if (c.resourceCap < c.resourceKg) c.resourceCap = c.resourceKg;
		if (c.labelEn.empty()) c.labelEn = name;
		if (c.labelRu.empty()) c.labelRu = c.labelEn;
		c.ok = true;
		oapiWriteLogV("OrbiterCrew: cargo class %s: %.2f x %.2f x %.2f m, tare %.1f kg, %s %.1f/%.1f kg", name.c_str(), c.dims[0], c.dims[1], c.dims[2],
			c.tare, c.resource.empty() ? "no resource" : c.resource.c_str(), c.resourceKg, c.resourceCap);
		return cache.emplace(name, c).first->second;
	}

	OcHeld MakeCargo(const CargoClass& c, double resourceKg)
	{
		OcHeld h{}; h.size = sizeof h; h.kind = OC_HELD_OTHER;
		const double r = c.resource.empty() ? 0.0 : std::clamp(resourceKg < 0 ? c.resourceKg : resourceKg, 0.0, c.resourceCap);
		h.massKg = c.tare + r;
		for (int i = 0; i < 3; ++i) h.dims[i] = c.dims[i];
		std::snprintf(h.mesh, sizeof h.mesh, "%s", c.mesh.c_str());
		std::snprintf(h.label, sizeof h.label, "%s", (UiEnglish() ? c.labelEn : c.labelRu).c_str());
		std::snprintf(h.data, sizeof h.data, "cargo %s %.3f", c.name.c_str(), r);
		return h;
	}

	bool CargoOf(const OcHeld& h, std::string& cls, double& resourceKg)
	{
		if (std::strncmp(h.data, "cargo ", 6) != 0) return false;
		std::istringstream ss(h.data + 6); resourceKg = 0;
		return static_cast<bool>(ss >> cls) && (ss >> resourceKg, true);
	}
}
