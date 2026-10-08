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
		// the equipment cooled down after a shutdown: back on, life first (the task loads follow 30 s later)
		if (tripped && tEq < EQ_BACK) { tripped = false; restartT = 30; }
		if (restartT > 0) restartT = (std::max)(0.0, restartT - dt);
		const bool power = Powered();
		// ~0.13 of the way to dark per Sv, half recovered in ~4 h without radiation (slower under it)
		{
			const double sv = (std::max)(0.0, radRate) * dt / 3600;
			optDark += 0.13 * sv * (1 - optDark) - optDark * dt / (6 * 3600) * (radRate < 1e-3 ? 1.0 : 0.2);
			optDark = std::clamp(optDark, 0.0, 0.95);
		}
		const double heatMax = econ ? 0.5 * heatMaxW : heatMaxW, coolMax = econ ? 0.5 * coolMaxW : coolMaxW, life = econ ? 0.75 * lifeW : lifeW;

		// heat to move: her own heat, what leaks in (+) or out (-) through the insulation, and the equipment's heat the
		// cooling loop takes (the loop barely moves it without power)
		if (pOut > pMaxOutKPa) breached = true;   // the outside pressure crushes the shell: it does not come back
		const double qEq = (power ? gEq : 0.2 * gEq) * (tEq - tIn);
		const double net = bodyHeatW + Conductance() * (tEnv - 295) + qEq;
		double residual = net;
		thermalW = driveW = heatW = 0;
		double q = 0;   // heat the control would move, W (+ cooling, - heating)
		if (power) { q = net > 0 ? (std::min)(net, coolMax) : -(std::min)(-net, heatMax); thermalW = q > 0 ? q / copCool : -q; }
		// load shedding, decided on what is asked (not on what is left - no flapping): the derating and the restart first
		shedField = shedLamps = shedDrives = overload = false;
		if (power && (tEq > EQ_LIMIT || restartT > 0)) { shedField = shedDrives = true; if (restartT > 0) shedLamps = true; }
		if (power)
		{
			double ask = life + thermalW + (drivesOn && !shedDrives ? driveDemandW : 0) + (shedLamps ? 0 : lampW) + (fieldOn && !shedField ? fieldW : 0);
			if (ask > supplyW && fieldOn && !shedField) { shedField = overload = true; ask -= fieldW; }
			if (ask > supplyW && lampW > 0 && !shedLamps) { shedLamps = overload = true; ask -= lampW; }
			if (ask > supplyW && drivesOn && !shedDrives) { shedDrives = overload = true; ask -= driveDemandW; }
			if (ask > supplyW && thermalW > 0)   // what is left over the limit comes off the thermal control, not off the O2 and sorbent
			{
				overload = true;
				const double cut = (std::min)(thermalW, ask - supplyW), k = (thermalW - cut) / thermalW;
				thermalW -= cut; q *= k;
			}
			residual = net - q; heatW = q;
			if (drivesOn && !shedDrives) driveW = driveDemandW;
		}
		// inside: held at the set point while the control keeps up; what it cannot move warms or cools the inside
		if (std::abs(residual) < 5 && power) tIn += (tSet - tIn) * (1 - std::exp(-dt / 90));
		else tIn += residual * dt / cIn;
		tIn = std::clamp(tIn, 200.0, 400.0);
		drawW = power ? life + thermalW + driveW + (shedLamps ? 0 : lampW) + (fieldOn && !shedField ? fieldW : 0) : 0;
		batt = (std::max)(0.0, batt - drawW * dt);
		// the equipment: its losses (more near the supply's limit), the drives' heat, the outside through the shell; the
		// cooling loop takes qEq. Too hot: it shuts down - as a flat battery
		const double losses = power ? 0.10 * drawW + 0.25 * driveW + 0.5 * (std::max)(0.0, drawW - 0.8 * supplyW) : 0;
		tEq += (losses - qEq + gEqOut * (tEnv - tEq)) * dt / cEq;
		tEq = std::clamp(tEq, 150.0, 900.0);
		if (power && tEq > EQ_TRIP) tripped = true;

		if (breached)   // the shell is open: the gas outside fills the helmet within seconds, the tank bleeds out
		{
			ppO2 += (ventPpO2 - ppO2) * (1 - std::exp(-dt / 3)); ppCO2 += (ventPpCO2 - ppCO2) * (1 - std::exp(-dt / 3));
			o2 = (std::max)(0.0, o2 - 0.01 * dt);
			return residual;
		}
		if (vent && power)   // breathable air around: the fan breathes it in, the tank and the sorbent rest
		{
			ppO2 += (ventPpO2 - ppO2) * (1 - std::exp(-dt / 10));
			ppCO2 += (ventPpCO2 - ppCO2) * (1 - std::exp(-dt / 20));
			return residual;
		}
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
		// radiation, the cerebral form (80 Sv and more): out within ten minutes; under the beam (200 Sv): within seconds
		if (doseSv >= 200) drain += 1.0 / 20; else if (doseSv >= 80) drain += 1.0 / 600;
		if (ppCO2 > 15) injury += dt * (ppCO2 - 15) / 15 / 600;   // CO2 poisoning: from ~15 kPa, minutes at 30 kPa
		reserve = drain > 0 ? reserve - drain * dt : reserve + dt / 20;
		reserve = std::clamp(reserve, 0.0, 1.0);

		// the brain dies of a lack of oxygen only (the user, 2026-10-04: three minutes without it); out cold from CO2, heat
		// or cold she lives on to their own limits (CO2 narcosis passes with fresh air; heat and cold: 'injury' above)
		const double o2Lack = exposed ? 1.0 : std::clamp((10 - ppO2) / 6, 0.0, 1.0);   // 1 at 4 kPa and below: 3 minutes
		if (reserve <= 0) { state = UNCONSCIOUS; anoxia += dt * o2Lack; }
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

	void Body::Irradiate(double dt, double rate)
	{
		doseRate = rate;
		if (state == DEAD) return;
		doseSv += rate * dt / 3600; careerSv += rate * dt / 3600;
		// acute radiation sickness by the level of the dose (the user, 2026-10-04): it kills in ArsDays() unless a medical
		// bay can cure that dose (her consciousness under a cerebral dose: Step). From 200 Sv - "смерть под лучом", death
		// during the exposure itself, in minutes
		const bool cured = inCare && doseSv < careMaxSv;
		inCare = false;
		if (const double days = ArsDays(); days > 0 && !cured) injury += dt / (days * 86400);
		if (doseSv >= 200) injury += dt / 120;
		if (injury >= 1) state = DEAD;
		if (warning.empty())
		{
			if (rate > 0.05) warning = "RADIATION - TAKE COVER";
			else if (doseSv > 1) warning = "RADIATION SICKNESS";
		}
	}

	// the clinical forms of acute radiation sickness, without care: bone marrow from ~4.5 Sv (LD50) - weeks; intestinal
	// 10-20 Sv - one to two weeks; toxaemic 20-80 Sv - 4-7 days; cerebral 80-200 Sv - 1-3 days. Log-linear between
	double Body::ArsDays() const
	{
		static const double D[] = { 4.5, 6, 10, 20, 80, 200 }, Days[] = { 60, 30, 14, 7, 2, 1 };
		if (doseSv < D[0]) return 0;
		for (int i = 1; i < 6; ++i)
			if (doseSv < D[i]) return Days[i - 1] * std::pow(Days[i] / Days[i - 1], std::log(doseSv / D[i - 1]) / std::log(D[i] / D[i - 1]));
		return Days[5];
	}

	void Body::Treat(double dt, const Care& care)
	{
		if (state == DEAD) return;
		inCare = true; careMaxSv = care.maxSv;
		const double k = dt / ((std::max)(1.0, care.hoursPerUnit) * 3600);   // severity healed per step: time follows severity
		for (double& h : hurt) h = (std::max)(0.0, h - k);
		if (doseSv >= care.maxSv) return;                                      // a dose beyond the bay: the sickness goes on
		injury = (std::max)(0.0, injury - k);
		doseSv = (std::max)(0.0, doseSv - care.svPerDay * dt / 86400);         // the acute dose repaired (the career dose stays)
	}

	double Body::Condition() const
	{
		const double dehyd = waterDef / mass;                                        // fraction of body mass
		double c = 1.0;
		if (dehyd > 0.02) c *= std::clamp(1.0 - 5.0 * (dehyd - 0.02), 0.4, 1.0);    // -5 % per 1 % beyond 2 %
		if (glycogen < 0.2 * 8.0e6) c *= 0.8;                                        // out of carbohydrate: fat only
		c *= std::clamp(1.0 - 0.04 * (std::max)(0.0, fastDays - 2.0), 0.6, 1.0);     // a week without food tells
		c *= 1.0 - 0.5 * hurt[TORSO];
		return c;
	}

	void Body::Hurt(Part p, double amount)
	{
		if (amount <= 0) return;
		hurt[p] = std::clamp(hurt[p] + amount, 0.0, 1.0);
		if (p == HEAD && hurt[HEAD] > 0.5) reserve = (std::max)(0.0, reserve - 0.5 * amount);   // a blow to the head
	}

	void Body::Sustain(double dt, bool supplied, double* suitWater, double airT, bool suited)
	{
		if (state == DEAD) { sweat = 0; return; }
		// water out: breath and skin at rest, sweat with effort and heat (the suit's cooling takes most of the heat)
		const double load = std::clamp((met - 90) / (std::max)(1.0, AerobicMax() - 90), 0.0, 1.2);
		const double hot = suited ? 0.0 : (std::max)(0.0, airT - 298.15);
		const double lph = 0.08 + (suited ? 0.5 : 0.9) * load + 0.04 * hot;                  // L/h
		sweat = (std::min)(lph, 2.0) / 3600;
		waterDef += sweat * dt;
		// water in: drink what is at hand, up to ~1 L/h
		const double drinkMax = 1.0 / 3600 * dt;
		double drink = 0;
		if (supplied) drink = (std::min)(waterDef, drinkMax);
		else if (suitWater && *suitWater > 0) { drink = (std::min)({ waterDef, drinkMax, *suitWater }); *suitWater -= drink; }
		waterDef = (std::max)(0.0, waterDef - drink);
		// food: carbohydrate burns first; meals refill it when food is at hand
		const double burn = met * dt;
		if (glycogen > 0) { glycogen = (std::max)(0.0, glycogen - 0.6 * burn); fat -= 0.4 * burn / 37.0e6; }
		else fat -= burn / 37.0e6;
		if (supplied) { glycogen = (std::min)(8.0e6, glycogen + 8.0e6 * dt / 3600); fastDays = 0; }
		else fastDays += dt / 86400;
		// healing: slow on her own, a few percent of an injury a day
		for (double& h : hurt) h = (std::max)(0.0, h - 0.03 * dt / 86400);
		// what kills: severe dehydration (~12 % of body mass), starvation (no usable fat left)
		const double dehyd = waterDef / mass;
		if (dehyd > 0.08) reserve -= dt * (dehyd - 0.08) / 0.04 / 3600;
		if (dehyd > 0.12) injury += dt / 6 / 3600;
		if (fat < 1.0) injury += dt / (2 * 86400);
		if (injury >= 1) state = DEAD;
		if (warning.empty())
		{
			if (dehyd > 0.04) warning = "DEHYDRATED";
			else if (fastDays > 3) warning = "STARVING";
			else if (hurt[LEGS] > 0.3 || hurt[ARMS] > 0.3 || hurt[TORSO] > 0.3 || hurt[HEAD] > 0.3) warning = "INJURED";
		}
	}

	void Body::Impact(double v, Contact c)
	{
		if (state == DEAD) return;
		switch (c)
		{
		case ON_FEET:   // legs bend and take it; the spine only in a very hard landing; the head is spared
			if (v <= 7.0) return;
			if (v >= 16.0) { state = DEAD; return; }
			injury += (v - 7.0) / 9.0;
			Hurt(LEGS, (v - 7.0) / 5.0);
			if (v > 11.0) Hurt(TORSO, (v - 11.0) / 6.0);
			reserve = (std::max)(0.0, reserve - 0.2 * (v - 7.0) / 9.0);
			break;
		case ON_BACK:   // a fall on the back: the torso, the back of the head, the arms flung out
			if (v <= 5.0) return;
			if (v >= 13.0) { state = DEAD; return; }
			injury += (v - 5.0) / 8.0;
			Hurt(TORSO, (v - 5.0) / 6.0); Hurt(HEAD, (v - 6.0) / 6.0); Hurt(ARMS, (v - 7.0) / 8.0);
			reserve = (std::max)(0.0, reserve - 0.5 * (v - 5.0) / 8.0);
			break;
		case ON_HEAD:   // head first: the head and the neck
			if (v <= 4.0) return;
			if (v >= 10.0) { state = DEAD; return; }
			injury += (v - 4.0) / 6.0;
			Hurt(HEAD, (v - 4.0) / 4.0); Hurt(TORSO, (v - 7.0) / 6.0);
			reserve = (std::max)(0.0, reserve - (v - 4.0) / 6.0);
			break;
		}
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
