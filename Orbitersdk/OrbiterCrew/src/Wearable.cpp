// OrbiterCrew - what the person wears (see Wearable.h).
#include "Wearable.h"
#include "JetPack.h"
#include "SuitComputer.h"
#include <cstdio>
#include <istream>
#include <sstream>

namespace ocrew
{
	SuitItem::SuitItem() : computer(std::make_unique<SuitComputer>()) {}
	SuitItem::~SuitItem() = default;

	Worn::Worn() : pack(std::make_unique<JetPack>()) {}
	Worn::~Worn() = default;

	double Worn::DryMass() const
	{
		return (suit.on ? suit.mass : 0) + (pack->Worn() ? JetPack::DRY : 0);
	}

	double Worn::Mass() const
	{
		return DryMass() + (pack->Worn() ? pack->Fuel() : 0);
	}

	void Worn::Save(FILEHANDLE scn) const
	{
		const Suit& s = suit.life;
		oapiWriteScenario_int(scn, const_cast<char*>("SUIT"), suit.on);
		oapiWriteScenario_float(scn, const_cast<char*>("SUIT_O2"), s.o2);
		oapiWriteScenario_float(scn, const_cast<char*>("SUIT_WATER"), s.water);
		oapiWriteScenario_float(scn, const_cast<char*>("SUIT_SORBENT"), s.sorbUsed);
		oapiWriteScenario_float(scn, const_cast<char*>("SUIT_BATTERY"), s.batt / 3.6e6);
		if (suit.n2Kg >= 0) oapiWriteScenario_float(scn, const_cast<char*>("SUIT_N2"), suit.n2Kg);
		if (s.breached) oapiWriteScenario_int(scn, const_cast<char*>("SUIT_BREACH"), 1);
		oapiWriteScenario_int(scn, const_cast<char*>("SUIT_SERVO"), s.drivesOn);
		if (s.econ) oapiWriteScenario_int(scn, const_cast<char*>("SUIT_ECON"), 1);
		oapiWriteScenario_float(scn, const_cast<char*>("SUIT_TEQ"), s.tEq);
		if (s.tripped) oapiWriteScenario_int(scn, const_cast<char*>("SUIT_TRIP"), 1);
		if (s.optDark > 1e-4) oapiWriteScenario_float(scn, const_cast<char*>("SUIT_OPTDARK"), s.optDark);
		oapiWriteScenario_int(scn, const_cast<char*>("FIELD"), s.fieldOn);
		oapiWriteScenario_string(scn, const_cast<char*>("HUD"), const_cast<char*>(suit.computer->hud.Save().c_str()));
		oapiWriteScenario_int(scn, const_cast<char*>("JETPACK"), pack->Worn());
		// the pack's fuel goes with the pack (older scenarios: only the body's PRPLEVEL had it)
		if (pack->Worn()) oapiWriteScenario_float(scn, const_cast<char*>("JET_FUEL"), pack->Fuel());
	}

	bool Worn::Load(const std::string& key, std::istream& ss)
	{
		Suit& s = suit.life;
		if (key == "SUIT") { ss >> suit.on; suit.fromScenario = true; }
		else if (key == "SUIT_O2") ss >> s.o2;
		else if (key == "SUIT_WATER") ss >> s.water;
		else if (key == "SUIT_SORBENT") ss >> s.sorbUsed;
		else if (key == "SUIT_BATTERY") { double kwh; ss >> kwh; s.batt = kwh * 3.6e6; }
		else if (key == "SUIT_N2") ss >> suit.n2Kg;
		else if (key == "SUIT_BREACH") { int b = 0; ss >> b; s.breached = b != 0; }
		else if (key == "SUIT_SERVO") { int b = 1; ss >> b; s.drivesOn = b != 0; }
		else if (key == "SUIT_ECON") { int b = 0; ss >> b; s.econ = b != 0; }
		else if (key == "SUIT_TEQ") ss >> s.tEq;
		else if (key == "SUIT_OPTDARK") ss >> s.optDark;
		else if (key == "SUIT_TRIP") { int b = 0; ss >> b; s.tripped = b != 0; }
		else if (key == "FIELD") ss >> s.fieldOn;
		else if (key == "HUD") { std::string rest; std::getline(ss, rest); suit.computer->hud.Load(rest); }
		else if (key == "JETPACK") ss >> packFromScenario;
		else if (key == "JET_FUEL") { double f = 0; ss >> f; pack->Keep(f); }
		else return false;
		return true;
	}
}
