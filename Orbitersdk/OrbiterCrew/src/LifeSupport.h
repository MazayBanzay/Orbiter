// OrbiterCrew - life support: what she breathes, what the suit carries, and how her body copes.
//
// Air:   Orbiter gives pressure and temperature only; the gas mix comes from Config\OrbiterCrew\Atmospheres.cfg
//        (O2 and CO2 fractions per body). A body not listed has no breathable oxygen.
// Suit:  pure-O2 suit at ~30 kPa. One battery runs everything: O2 regulation, CO2 sorbent and fans, heating and
//        cooling, the exoskeleton drives. Tank empty or battery flat: she breathes down the gas in the suit and CO2
//        builds up; battery flat: no thermal control either. Rated environment -157..+121 C (effective sink temperature).
// Body:  metabolic power from activity (mocap-independent, energy per metre for walking and running) sets O2 use,
//        CO2 output, pulse and breathing. Effort above the aerobic threshold spends a finite anaerobic reserve
//        (stamina), which limits the running speed. Low inspired O2 or high CO2 drains a consciousness reserve;
//        a vacuum without the suit is fatal within about a minute and a half.
#pragma once
#include <Orbitersdk.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace ocrew
{
	struct Air
	{
		std::string body;
		double p{}, T{};           // kPa, K
		double ppO2{}, ppCO2{};    // kPa
		// can she take the suit off here? reason is filled when not
		bool Breathable(std::string* reason = nullptr) const;
	};

	class Atmospheres
	{
	public:
		void Load();
		bool Loaded() const { return loaded; }
		Air Sample(const VESSEL* v) const;
	private:
		struct Mix { double o2{}, co2{}; };
		std::map<std::string, Mix> mix;
		bool loaded{};
	};

	// thermal surroundings of the suit, reduced to one effective sink temperature (K)
	struct Thermal
	{
		bool sunlit{ true };
		double sunFlux{ 1361 };    // W/m^2
		double groundT{};          // K, airless surface under her (0 = not on a surface)
		double tEnv{ 293 };        // K, what the suit's outer layer "sees": air, or radiative balance in vacuum
	};

	// One battery runs everything: O2 regulation, fans and CO2 sorbent, thermal control, the exoskeleton drives.
	// Flat battery: no O2 flow, no scrubbing, no heating or cooling, the drives are dead weight.
	struct Suit
	{
		double o2Cap{ 1.0 }, o2{ 1.0 };               // kg
		double sorbCap{ 1.6 }, sorbUsed{};            // kg CO2
		double battCap{ 6.0 * 3.6e6 }, batt{ 6.0 * 3.6e6 };   // J
		double pressure{ 30 };                        // kPa, regulated helmet O2
		double volume{ 0.12 };                        // m^3 of gas around the body
		double lifeW{ 60 };                           // fans, pumps, regulator, sorbent regeneration, radio
		double lampW{};                               // helmet lamps when on
		double conductance{ 1.5 };                    // W/K through the insulation to the environment
		double heatMaxW{ 400 }, coolMaxW{ 700 };      // thermal control capacity
		double copCool{ 3 };                          // heat pump to the radiator
		double tMin{ 116 }, tMax{ 394 };              // K: rated environment -157..+121 C
		double ppO2{ 30 }, ppCO2{ 0.05 };             // helmet gas
		double driveW{}, thermalW{}, drawW{};         // last step
		bool drivesOn{ true };

		bool Powered() const { return batt > 0; }
		bool Drives() const { return drivesOn && Powered(); }
		bool InSpec(double tEnv) const { return tEnv >= tMin && tEnv <= tMax; }
		void Seal() { ppO2 = o2 > 0 && Powered() ? pressure : ppO2; ppCO2 = 0.05; }
		// returns the heat (W) the suit could not handle, going into her body (+ warms, - cools)
		double Step(double dt, double o2Use, double co2Made, double driveDemandW, double bodyHeatW, double tEnv);
		double HoursLeft(double o2Use) const { return o2Use > 0 ? o2 / o2Use / 3600 : 0; }
		double BattHours() const { return drawW > 0 ? batt / drawW / 3600 : 0; }
	};

	class Body
	{
	public:
		enum State { OK, UNCONSCIOUS, DEAD };

		double mass{ 62 };
		double vo2max{ 48 };       // ml/kg/min: a fit woman
		double wbal{ 1 };          // anaerobic reserve 0..1 (stamina)
		double reserve{ 1 };       // consciousness reserve 0..1 (brain oxygenation)
		double anoxia{}, injury{};
		double coreT{ 310.15 };    // K
		State state{ OK };
		double met{ 90 }, pulse{ 62 }, breath{ 13 };
		std::string warning;

		double AerobicMax() const { return vo2max * mass * 0.335; }   // W: VO2 (L/min) * 20.1 kJ/L
		// activityW: metabolic cost of what she does beyond standing; heatW: heat reaching the body from outside control
		void Step(double dt, double activityW, double ppO2, double ppCO2, double ambientP, bool suited, double heatW);
		void Spend(double joules) { wbal = (std::max)(0.0, wbal - joules / WCAP); }
		// touchdown at 'v' m/s (vertical): the suit and its frame take the first 7 m/s; beyond, injury; above 14, fatal
		void Impact(double v);
		double O2Use() const { return state == DEAD ? 0 : met / 14.07e6; }       // kg/s, 14.07 MJ per kg O2
		double CO2Made() const { return O2Use() * 1.169; }                       // kg/s at RQ 0.85
		double Heat() const { return state == DEAD ? 0 : 0.8 * met; }            // W released as heat
		double Fatigue() const { return 1 - wbal; }
		double Effort() const { return std::clamp((met - 90) / (AerobicMax() - 90), 0.0, 1.2); }
		double RunLimit() const { return 3.0 + 2.0 * std::pow(wbal, 0.6); }     // m/s
		bool CanAct() const { return state == OK; }

		static constexpr double WCAP = 40e3;   // J of metabolic work above the threshold
	};

	// metabolic power of moving over the ground (W); v m/s, g m/s^2, load = carried mass
	double LocomotionPower(double mass, double v, double g);
}
