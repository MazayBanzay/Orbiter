// OrbiterCrew - the person and the crew registry (see Person.h).
#include "Person.h"
#include <algorithm>
#include <cstdio>

namespace ocrew
{
	Person& Crew::Create()
	{
		people.push_back(std::make_unique<Person>());
		people.back()->id = nextId++;
		return *people.back();
	}

	Person* Crew::Find(int id)
	{
		for (auto& p : people) if (p->id == id) return p.get();
		return nullptr;
	}

	Person& Crew::Claim()
	{
		Person* p = pending ? Find(pending) : nullptr;
		pending = 0;
		claimedExisting = p != nullptr;
		return p ? *p : Create();
	}

	bool Crew::AnyAboard()
	{
		for (auto& p : people) if (p->where == Person::ABOARD) return true;
		return false;
	}

	void Person::Save(FILEHANDLE scn)
	{
		auto str = [&](const char* k, const std::string& v) { oapiWriteScenario_string(scn, const_cast<char*>(k), const_cast<char*>(v.c_str())); };
		auto fmt = [&](const char* k, const char* f, auto... a) { char b[128]; std::snprintf(b, sizeof b, f, a...); str(k, b); };
		str("NAME", name);
		str("ROLE", role);
		str("SEX", sex);
		fmt("PHYS", "%.1f %.3f %.2f %.2f %.1f", age, heightM, body.mass, body.vo2max, body.liftMax);
		str("CLASS", bodyClass);
		fmt("STAMINA", "%.6f", body.wbal);
		fmt("DOSE", "%.5f %.5f", body.doseSv, body.careerSv);
		fmt("NOURISH", "%.4f %.0f %.3f %.3f", body.waterDef, body.glycogen, body.fat, body.fastDays);
		fmt("HURT", "%.3f %.3f %.3f %.3f", body.hurt[0], body.hurt[1], body.hurt[2], body.hurt[3]);
		fmt("RESERVE", "%.6f", body.reserve);
		fmt("CORE", "%.3f", body.coreT);
		fmt("INJURY", "%.6f", body.injury);
		if (where == ABOARD && place != STORED) oapiWriteScenario_int(scn, const_cast<char*>("PLACE"), static_cast<int>(place));
		oapiWriteScenario_int(scn, const_cast<char*>("BODY"), static_cast<int>(body.state));
		fmt("VIEW", "%d %.2f", viewOutside ? 1 : 0, viewDist);
		worn.Save(scn);                    // the suit, its computer and the pack: their own lines
	}

	bool Person::LoadLine(const std::string& key, std::istringstream& ss)
	{
		auto rest = [&](std::string& v) { std::getline(ss >> std::ws, v); while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back(); };
		if (key == "NAME") rest(name);
		else if (key == "ROLE") rest(role);
		else if (key == "SEX") ss >> sex;
		else if (key == "PHYS") ss >> age >> heightM >> body.mass >> body.vo2max >> body.liftMax;
		else if (key == "CLASS") { rest(bodyClass); if (bodyClass.rfind("OrbiterCrew", 0) == 0 && bodyClass.find('\\') == std::string::npos) bodyClass.insert(11, "\\"); }   // (an early build lost the backslash)
		else if (worn.Load(key, ss)) {}
		else if (key == "STAMINA") ss >> body.wbal;
		else if (key == "DOSE") ss >> body.doseSv >> body.careerSv;
		else if (key == "NOURISH") ss >> body.waterDef >> body.glycogen >> body.fat >> body.fastDays;
		else if (key == "HURT") for (double& h : body.hurt) ss >> h;
		else if (key == "RESERVE") ss >> body.reserve;
		else if (key == "CORE") ss >> body.coreT;
		else if (key == "INJURY") ss >> body.injury;
		else if (key == "PLACE") { int pl = 0; ss >> pl; place = static_cast<Place>(std::clamp(pl, 0, 2)); }
		else if (key == "VIEW") { int o = 0; ss >> o >> viewDist; viewOutside = o != 0; }
		else if (key == "BODY") { int st = 0; ss >> st; body.state = static_cast<Body::State>(std::clamp(st, 0, 2)); }
		else return false;
		return true;
	}
}
