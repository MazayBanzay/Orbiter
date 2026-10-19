// OrbiterCrew - what the person wears (see Wearable.h).
#include "Wearable.h"
#include "JetPack.h"
#include "Frame.h"
#include "SuitComputer.h"
#include <cstdio>
#include <istream>
#include <sstream>

namespace ocrew
{
	SuitItem::SuitItem() : computer(std::make_unique<SuitComputer>()) {}
	SuitItem::~SuitItem() = default;

	bool SuitItem::ApplyClass(const std::string& asked)
	{
		// the classes' names (2026-10-18): Suit_T10, Suit_T30; older saves and configs say Karkas / Pantsir
		const std::string name = asked == "Karkas" ? "Suit_T10" : asked == "Pantsir" ? "Suit_T30" : asked;
		const std::string path = "OrbiterCrew\\Suits\\" + name + ".cfg";
		FILEHANDLE f = oapiOpenFile(path.c_str(), FILE_IN_ZEROONFAIL, CONFIG);
		if (!f) { oapiWriteLogV("OrbiterCrew: suit class %s - no Config\\%s", name.c_str(), path.c_str()); return false; }
		const bool hadSet = setOn;
		if (hadSet) SetPlates(false);           // the class sets the bare suit; its set goes back over it below
		Suit& s = life;
		double v = 0; int iv = 0; char buf[256];
		auto rd = [&](const char* k, double& to, double mul = 1.0) { double x; if (oapiReadItem_float(f, const_cast<char*>(k), x)) to = x * mul; };
		cls = name; title = name;
		if (oapiReadItem_string(f, const_cast<char*>("Title"), buf)) title = buf;
		rd("Mass", mass);
		rd("ShieldGcm2", s.shieldGcm2); rd("BremsFactor", s.bremsFactor);
		rd("FieldW", s.fieldW); rd("FieldFactor", s.fieldFactor); rd("FieldHeat", s.fieldHeat);
		if (oapiReadItem_int(f, const_cast<char*>("FieldSteps"), iv)) s.fieldSteps = iv != 0;
		if (oapiReadItem_float(f, const_cast<char*>("Battery"), v)) s.batt = s.battCap = v * 3.6e6;
		rd("SupplyW", s.supplyW); rd("LifeW", s.lifeW); rd("Conductance", s.conductance); rd("LiftKg", s.liftKg);
		rd("HeatMaxW", s.heatMaxW); rd("CoolMaxW", s.coolMaxW); rd("Cop", s.copCool); rd("CopMin", s.copMin); rd("CopHotK", s.copHotK);
		if (oapiReadItem_float(f, const_cast<char*>("EvapWater"), v)) s.evap = s.evapCap = v;
		rd("EvapMaxW", s.evapMaxW);
		if (oapiReadItem_float(f, const_cast<char*>("TMinC"), v)) s.tMin = v + 273.15;
		if (oapiReadItem_float(f, const_cast<char*>("TMaxC"), v)) s.tMax = v + 273.15;
		rd("PMaxOutKPa", s.pMaxOutKPa);
		if (oapiReadItem_float(f, const_cast<char*>("O2"), v)) s.o2 = s.o2Cap = v;
		if (oapiReadItem_float(f, const_cast<char*>("Sorbent"), v)) { s.sorbCap = v; s.sorbUsed = 0; }
		if (oapiReadItem_float(f, const_cast<char*>("Water"), v)) s.water = s.waterCap = v;
		rd("Pressure", s.pressure);
		rd("EquipC", s.cEq); rd("EquipG", s.gEq);
		rd("WalkFactor", s.walkFactor); rd("StepPitch", s.stepPitch);
		s.armOutDeg = s.legOutDeg = -1; rd("ArmOutDeg", s.armOutDeg); rd("LegOutDeg", s.legOutDeg); s.foreArmInDeg = 0; rd("ForeArmInDeg", s.foreArmInDeg);
		if (oapiReadItem_int(f, const_cast<char*>("LocksUpright"), iv)) s.locksUpright = iv != 0;
		if (oapiReadItem_float(f, const_cast<char*>("DamageC"), v)) s.damageK = v + 273.15;
		if (oapiReadItem_float(f, const_cast<char*>("ColdC"), v)) s.coldK = v + 273.15;
		rd("ShellArea", s.shellArea); rd("ShellEps", s.shellEps); rd("SealDepth", s.sealDepth);
		if (oapiReadItem_float(f, const_cast<char*>("ContactC"), v)) s.contactK = v + 273.15;
		rd("LegSoakS", s.legSoak);
		s.computer = "Karkas";
		if (oapiReadItem_string(f, const_cast<char*>("Computer"), buf)) s.computer = buf;
		oapiCloseFile(f, FILE_IN_ZEROONFAIL);
		oapiWriteLogV("OrbiterCrew: suit class %s (%s): %.0f kg, %.1f g/cm2, field %.0f W x%.0f, battery %.1f kWh",
			cls.c_str(), title.c_str(), mass, s.shieldGcm2, s.fieldW, s.fieldFactor, s.battCap / 3.6e6);
		if (hadSet) SetPlates(true);            // (refused if the set is not for this class)
		return true;
	}

	bool SuitItem::SetPlates(bool put)
	{
		if (put == setOn) return true;
		if (put && !setRead)
		{
			const std::string path = "OrbiterCrew\\Suits\\Sets\\" + setName + ".cfg";
			FILEHANDLE f = oapiOpenFile(path.c_str(), FILE_IN_ZEROONFAIL, CONFIG);
			if (!f) { oapiWriteLogV("OrbiterCrew: suit set %s - no Config\\%s", setName.c_str(), path.c_str()); return false; }
			double v = 0; char buf[256];
			setCls.clear();
			if (oapiReadItem_string(f, const_cast<char*>("Class"), buf)) setCls = buf;
			if (oapiReadItem_float(f, const_cast<char*>("Mass"), v)) setMass = v;
			if (oapiReadItem_float(f, const_cast<char*>("ShieldGcm2"), v)) setShield = v;
			if (oapiReadItem_float(f, const_cast<char*>("ConductanceMul"), v) && v > 0) setCondMul = v;
			if (oapiReadItem_float(f, const_cast<char*>("AlphaAdd"), v)) setAlphaAdd = v;
			oapiCloseFile(f, FILE_IN_ZEROONFAIL); setRead = true;
		}
		if (put && !setCls.empty() && setCls != cls)
		{
			oapiWriteLogV("OrbiterCrew: suit set %s is for %s, the suit is %s - not put on", setName.c_str(), setCls.c_str(), cls.c_str());
			return false;
		}
		Suit& s = life; const double sg = put ? 1.0 : -1.0;
		mass += sg * setMass; s.shieldGcm2 += sg * setShield; s.solarAlpha += sg * setAlphaAdd;
		s.conductance = put ? s.conductance * setCondMul : s.conductance / setCondMul;
		setOn = put;
		oapiWriteLogV("OrbiterCrew: suit set %s %s (%s): suit %.1f kg, shield %.2f g/cm2, conductance %.2f W/K, sunlight absorptance %.3f",
			setName.c_str(), put ? "on" : "off", cls.c_str(), mass, s.shieldGcm2, s.conductance, s.solarAlpha);
		return true;
	}

	Worn::Worn() : pack(std::make_unique<JetPack>()), frame(std::make_unique<Frame>()) {}
	Worn::~Worn() = default;

	double Worn::DryMass() const
	{
		return (suit.on ? suit.mass : 0) + (pack->Worn() ? pack->DryMass() : 0) + (frame->Worn() ? frame->mass : 0);
	}

	double Worn::Mass() const
	{
		return DryMass() + (pack->Worn() ? pack->Fuel() : 0);
	}

	void Worn::Save(FILEHANDLE scn) const
	{
		const Suit& s = suit.life;
		oapiWriteScenario_int(scn, const_cast<char*>("SUIT"), suit.on);
		if (suit.cls != "Suit_T10") oapiWriteScenario_string(scn, const_cast<char*>("SUIT_CLASS"), const_cast<char*>(suit.cls.c_str()));   // before its supplies
		if (suit.setOn) oapiWriteScenario_string(scn, const_cast<char*>("SUIT_SET"), const_cast<char*>(suit.setName.c_str()));   // the class's set on (the plates)
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
		if (s.helmetOpen) oapiWriteScenario_int(scn, const_cast<char*>("HELMET_OPEN"), 1);
		if (s.fieldSteps) oapiWriteScenario_float(scn, const_cast<char*>("FIELD_LEVEL"), s.fieldLevel);
		if (s.evapCap > 0) oapiWriteScenario_float(scn, const_cast<char*>("SUIT_EVAP"), s.evap);
		if (s.dmgIns > 0 || s.dmgSeal > 0 || s.fieldFail || s.drivesFail || s.coolFail)
		{
			char b[128]; std::snprintf(b, sizeof b, "%.4f %.4f %d %d %d", s.dmgIns, s.dmgSeal, s.fieldFail, s.drivesFail, s.coolFail);
			oapiWriteScenario_string(scn, const_cast<char*>("SUIT_DAMAGE"), b);
		}
		oapiWriteScenario_string(scn, const_cast<char*>("HUD"), const_cast<char*>(suit.computer->hud.Save().c_str()));
		oapiWriteScenario_int(scn, const_cast<char*>("JETPACK"), pack->Worn());
		// the pack's fuel goes with the pack (older scenarios: only the body's PRPLEVEL had it)
		if (pack->Worn()) oapiWriteScenario_float(scn, const_cast<char*>("JET_FUEL"), pack->Fuel());
		if (pack->Worn() && pack->GetModel() != JetPack::SHOULDER) oapiWriteScenario_int(scn, const_cast<char*>("JET_MODEL"), pack->GetModel());   // the impeller pack
		if (pack->Worn() && pack->DamageMask()) oapiWriteScenario_int(scn, const_cast<char*>("JET_DAMAGE"), pack->DamageMask());   // the Гекса's damaged chambers
		// the amplifier frame: worn, its class, its cassette (kWh)
		if (frame->Worn() || frame->cls != "Usilitel")
		{
			oapiWriteScenario_int(scn, const_cast<char*>("FRAME"), frame->Worn());
			if (frame->cls != "Usilitel") oapiWriteScenario_string(scn, const_cast<char*>("FRAME_CLASS"), const_cast<char*>(frame->cls.c_str()));
			oapiWriteScenario_float(scn, const_cast<char*>("FRAME_BATT"), frame->Batt() / 3.6e6);
			if (frame->link) oapiWriteScenario_int(scn, const_cast<char*>("FRAME_LINK"), 1);
		}
	}

	bool Worn::Load(const std::string& key, std::istream& ss)
	{
		Suit& s = suit.life;
		if (key == "SUIT") { ss >> suit.on; suit.fromScenario = true; }
		else if (key == "SUIT_CLASS") { std::string c; ss >> c; if (!c.empty()) suit.ApplyClass(c); }
		else if (key == "SUIT_SET") { std::string c; ss >> c; if (!c.empty() && c != suit.setName) { suit.SetPlates(false); suit.setName = c; suit.setRead = false; } suit.SetPlates(true); }
		else if (key == "HELMET_OPEN") { int b = 0; ss >> b; s.helmetOpen = b != 0; s.helmetPos = s.helmetOpen ? 1.0 : 0.0; }
		else if (key == "FIELD_LEVEL") { ss >> s.fieldLevel; s.fieldLevel = s.fieldLevel < 0.2 ? 0.25 : s.fieldLevel > 1 ? 1.0 : s.fieldLevel; }
		else if (key == "SUIT_DAMAGE") { int a = 0, b = 0, c = 0; ss >> s.dmgIns >> s.dmgSeal >> a >> b >> c; s.fieldFail = a != 0; s.drivesFail = b != 0; s.coolFail = c != 0;
			if (!s.Heavy()) s.coolFail = false; }   // the radiator's loop: only the heavy model has it
		else if (key == "SUIT_EVAP") { ss >> s.evap; s.evap = s.evap < 0 ? 0 : s.evap > s.evapCap ? s.evapCap : s.evap; }
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
		else if (key == "JET_MODEL") { int m = 0; ss >> m; pack->KeepModel(m); }
		else if (key == "JET_DAMAGE") { int m = 0; ss >> m; pack->SetDamageMask(m); }   // bit 2*capsule + chamber (0 right, 1 left; 0 outboard, 1 inboard)
		else if (key == "FRAME") { int b = 0; ss >> b; frame->SetWorn(b != 0); if (b) frame->ApplyClass(frame->cls); }
		else if (key == "FRAME_CLASS") { std::string c; ss >> c; if (!c.empty()) frame->ApplyClass(c); }
		else if (key == "FRAME_LINK") { int b = 0; ss >> b; frame->link = b != 0; }
		else if (key == "FRAME_BATT") { double kwh = 0; ss >> kwh; frame->SetBatt(kwh * 3.6e6); }
		else return false;
		return true;
	}
}
