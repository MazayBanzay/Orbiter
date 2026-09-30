// OrbiterCrew - life support (see LifeSupport.h).
#include "LifeSupport.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace ocrew
{
	namespace
	{
		const double RT = 8.314 * 305;   // J/mol at suit temperature
		double KPaPerKg(double molarMass, double volume) { return RT / (molarMass * volume) / 1000; }
	}

	bool Air::Breathable(std::string* reason) const
	{
		auto no = [&](const char* r) { if (reason) *reason = r; return false; };
		if (p < 6.3) return no("вакуум");
		if (ppO2 < 16) return no("мало кислорода");
		if (ppO2 > 60) return no("избыток кислорода");
		if (ppCO2 > 1.0) return no("много CO2");
		if (T < 243) return no("слишком холодно");
		if (T > 323) return no("слишком жарко");
		return true;
	}

	void Atmospheres::Load()
	{
		loaded = true;
		std::ifstream f("Config\\OrbiterCrew\\Atmospheres.cfg");
		std::string line;
		while (std::getline(f, line))
		{
			if (auto c = line.find(';'); c != std::string::npos) line.erase(c);
			std::istringstream ss(line);
			std::string name, key; Mix m; double v;
			if (!(ss >> name)) continue;
			while (ss >> key >> v) { if (key == "O2") m.o2 = v; else if (key == "CO2") m.co2 = v; }
			mix[name] = m;
		}
		if (mix.empty()) oapiWriteLog(const_cast<char*>("OrbiterCrew: Config\\OrbiterCrew\\Atmospheres.cfg missing, no body has breathable air"));
	}

	Air Atmospheres::Sample(const VESSEL* v) const
	{
		Air a;
		OBJHANDLE ref = v->GetAtmRef();
		if (!ref) return a;
		char name[256]; oapiGetObjectName(ref, name, 256);
		a.body = name;
		a.p = v->GetAtmPressure() / 1000; a.T = v->GetAtmTemperature();
		if (auto it = mix.find(a.body); it != mix.end()) { a.ppO2 = a.p * it->second.o2; a.ppCO2 = a.p * it->second.co2; }
		return a;
	}

	double Suit::Step(double dt, double o2Use, double co2Made, double driveDemandW, double bodyHeatW, double tEnv)
	{
		const bool power = Powered();

		// heat to move: her own heat plus what leaks in (+) or out (-) through the insulation
		const double net = bodyHeatW + conductance * (tEnv - 295);
		double residual = net;
		thermalW = driveW = 0;
		if (power)
		{
			if (net > 0) { const double q = (std::min)(net, coolMaxW); thermalW = q / copCool; residual = net - q; }
			else { const double q = (std::min)(-net, heatMaxW); thermalW = q; residual = net + q; }
			if (drivesOn) driveW = driveDemandW;
		}
		drawW = power ? lifeW + thermalW + driveW + lampW : 0;
		batt = (std::max)(0.0, batt - drawW * dt);

		if (power && o2 > 0) { o2 = (std::max)(0.0, o2 - o2Use * dt); ppO2 = pressure; }
		else ppO2 = (std::max)(0.0, ppO2 - o2Use * dt * KPaPerKg(0.032, volume));

		if (power && sorbUsed < sorbCap)
		{
			sorbUsed = (std::min)(sorbCap, sorbUsed + co2Made * dt);
			ppCO2 += (0.05 - ppCO2) * (1 - std::exp(-dt / 20));   // the fans scrub the helmet down within a minute
		}
		else ppCO2 += co2Made * dt * KPaPerKg(0.044, volume);
		return residual;
	}

	void Body::Step(double dt, double activityW, double ppO2, double ppCO2, double ambientP, bool suited, double heatW)
	{
		warning.clear();
		if (state == DEAD) { met = pulse = breath = 0; return; }

		// core temperature: 62 kg at ~3.5 kJ/kg/K; her own thermoregulation handles small imbalances
		coreT += heatW * dt / (mass * 3500);
		if (std::abs(heatW) < 60) coreT += (310.15 - coreT) * (1 - std::exp(-dt / 1800));

		// metabolism follows the demand within a few seconds
		const double demand = 90 + (state == OK ? activityW : 0);
		met += (demand - met) * (1 - std::exp(-dt / 4));

		// stamina: work above the aerobic threshold spends the anaerobic reserve, rest refills it
		const double thr = 0.8 * AerobicMax();
		if (met > thr) wbal -= (met - thr) * dt / WCAP;
		else wbal += (thr - met) / thr * dt / 45;
		wbal = std::clamp(wbal, 0.0, 1.0);

		// consciousness: hypoxia below ~10 kPa inspired O2, CO2 narcosis above ~7 kPa, ebullism in near vacuum
		double drain = 0;
		if (ppO2 < 10) drain += (10 - ppO2) / 10 / 15;
		if (ppCO2 > 7) drain += (ppCO2 - 7) / 5 / 30;
		const bool exposed = !suited && ambientP < 6.3;
		if (exposed) { drain += 1.0 / 10; injury += dt / 90; }
		const double c = coreT - 273.15;
		if (c > 41) drain += (c - 41) / 2 / 60;          // heat stroke
		if (c < 32) drain += (32 - c) / 4 / 60;          // deep hypothermia
		if (c > 43 || c < 26) injury += dt / 300;
		reserve = drain > 0 ? reserve - drain * dt : reserve + dt / 20;
		reserve = std::clamp(reserve, 0.0, 1.0);

		if (reserve <= 0) { state = UNCONSCIOUS; anoxia += dt; }
		else { anoxia = (std::max)(0.0, anoxia - 0.2 * dt); if (state == UNCONSCIOUS && reserve > 0.4) state = OK; }
		if (anoxia > 180 || injury >= 1) state = DEAD;

		const double load = std::clamp((met - 90) / (AerobicMax() - 90), 0.0, 1.2);
		const double stress = 1 - reserve;
		pulse = 62 + 110 * load + 25 * Fatigue() + 30 * stress;
		breath = 13 + 27 * load + 10 * Fatigue() + (ppCO2 > 1 ? 4 * (ppCO2 - 1) : 0);
		if (state == UNCONSCIOUS) breath = (std::min)(breath, 8.0);

		if (exposed) warning = "VACUUM - NO SUIT";
		else if (injury > 0.05) warning = "INJURED";
		else if (c > 39.5) warning = "OVERHEATING";
		else if (c < 35) warning = "HYPOTHERMIA";
		else if (ppO2 < 10) warning = "HYPOXIA";
		else if (ppO2 < 14) warning = "LOW OXYGEN";
		else if (ppCO2 > 7) warning = "CO2 NARCOSIS";
		else if (ppCO2 > 2) warning = "HIGH CO2";
		else if (wbal < 0.15) warning = "EXHAUSTED";
	}

	void Body::Impact(double v)
	{
		if (state == DEAD || v <= 7.0) return;
		if (v >= 14.0) { state = DEAD; return; }
		injury += (v - 7.0) / 7.0;          // hard landings add up
		reserve = (std::max)(0.0, reserve - 0.3 * (v - 7.0) / 7.0);
		if (injury >= 1) state = DEAD;
	}

	double LocomotionPower(double mass, double v, double g)
	{
		v = std::abs(v);
		if (v < 0.02) return 0;
		// net cost of transport (J/kg/m): walking has an optimum near 1.3 m/s, running is nearly flat
		const double walk = 2.0 + 1.1 * (v - 1.3) * (v - 1.3);
		const double runC = 3.8;
		const double t = std::clamp((v - 1.9) / 0.6, 0.0, 1.0);
		const double c = walk * (1 - t) + runC * t;
		const double gf = 0.3 + 0.7 * std::clamp(g / 9.81, 0.0, 1.5);   // lighter gravity, cheaper steps
		return mass * v * c * gf;
	}
}
