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
		double waterCap{ 2.0 }, water{ 2.0 };         // kg: the drink bag in the helmet ring (she sips from it, Body::Sustain)
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
		double heatW{};                               // last step: heat moved, + cooling, - heating
		// radiation: passive shielding (BNNT with hydrogen + a layered shield) and an active magnetic field on the battery.
		// fieldFactor divides the charged particles' dose; the cosmic rays only by ~1.5 (Radiation::Dose applies both).
		double shieldGcm2{ 2.0 };                    // g/cm^2 areal density
		bool fieldOn{};                               // magnetic shield switched on (works only while powered)
		double fieldW{ 600 };                        // its draw, W
		double fieldFactor{ 30 };                    // charged particles divided by this with the field up
		bool FieldUp() const { return fieldOn && Powered() && !shedField; }
		double tIn{ 295.15 }, tSet{ 295.15 };          // K: the air and the cooling garment inside, and what the control holds
		double cIn{ 15000 };                          // J/K: garment water loop, air, underwear
		bool drivesOn{ true };   // the servo drives: switched by hand (off: no help carrying the suit, slower, no bounding)

		// ---- power and the suit's own hardware ----
		// The supply's continuous limit: above it the loads are shed by the three loops - ЗАДАЧА first (field, lamps,
		// drives), ВОЗВРАТ next (the pack has its own fuel), ЖИЗНЬ last (the thermal control is cut back before O2/sorbent).
		double supplyW{ 1000 };
		bool shedField{}, shedLamps{}, shedDrives{}, overload{};
		// The equipment as one thermal node: its losses and the outside heat it, the cooling loop takes the heat away.
		// 60 C caution, 75 C derating (field and drives off), 90 C shutdown - everything off as with a flat battery (no O2
		// flow, no scrubbing, no heating or cooling); back on below 50 C, life first, the task loads 30 s later.
		double tEq{ 305 }, cEq{ 8000 }, gEq{ 6 }, gEqOut{ 0.3 };   // K, J/K, W/K to the cooling loop, W/K to the outside
		bool tripped{}; double restartT{};
		static constexpr double EQ_WARN = 333.15, EQ_LIMIT = 348.15, EQ_TRIP = 363.15, EQ_BACK = 323.15;
		bool econ{};   // economy: half the thermal capacity, slower fans - a longer battery, a wider swing inside

		bool Powered() const { return batt > 0 && !tripped; }
		bool Drives() const { return drivesOn && Powered() && !shedDrives; }
		bool LampsUp() const { return Powered() && !shedLamps; }
		bool InSpec(double tEnv) const { return tEnv >= tMin && tEnv <= tMax; }
		void Seal() { ppO2 = o2 > 0 && Powered() ? pressure : ppO2; ppCO2 = 0.05; }
		// returns the heat (W) the suit could not handle, going into her body (+ warms, - cools)
		double Step(double dt, double o2Use, double co2Made, double driveDemandW, double bodyHeatW, double tEnv);
		double HoursLeft(double o2Use) const { return o2Use > 0 ? o2 / o2Use / 3600 : 0; }
		// in breathable air the fan draws the air around through the helmet: the tank and the sorbent are not used,
		// only the battery (set before Step)
		bool vent{}; double ventPpO2{}, ventPpCO2{};
		// the outside: its pressure (kPa, Orbiter's own) sets how much heat the gas carries through the suit - nothing in
		// vacuum (radiation only), more the denser the gas; above the shell's rated outside pressure the shell gives way
		double pOut{};                   // kPa, set before Step
		double pMaxOutKPa{ 200 };        // class 1 «Каркас»: built for vacuum and Earth-like air, not for a deep atmosphere
		bool breached{};                 // the shell crushed / torn: the outside comes in
		double Conductance() const { return conductance * (1 + 4 * std::sqrt((std::max)(0.0, pOut) / 101.3)) * (breached ? 10 : 1); }   // W/K
		bool Venting() const { return vent && Powered(); }
		// an impact through the suit: the frame carries the suit's and the pack's mass to the ground, and its passive
		// dampers (physical loop, no power needed) take that mass's energy and a share of the body's - 40 % feet first,
		// 20 % on the back, 30 % head first (the helmet) - until their stroke (~0.25 m) runs out at ~7 m/s; the rest
		// reaches the body. contact: as Body::Contact (0 feet, 1 back, 2 head).
		// Returns the impact speed the bare body feels the same energy at.
		static double ImpactThrough(double v, double mBody, double mWorn, int contact)
		{
			if (v <= 0 || mBody <= 0) return 0;
			const double share = contact == 0 ? 0.4 : contact == 2 ? 0.3 : 0.2, VSTROKE = 7.0;
			const double eBody = 0.5 * mBody * v * v, eWorn = 0.5 * mWorn * v * v;
			const double cap = 0.5 * VSTROKE * VSTROKE * (mWorn + share * mBody);
			const double toBody = eBody + eWorn - (std::min)(cap, eWorn + share * eBody);
			return std::sqrt(2 * (std::max)(0.0, toBody) / mBody);
		}
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
		bool inCare{}; double careMaxSv{};   // in a medical bay this step (Treat) and the dose it can cure
		double coreT{ 310.15 };    // K
		double doseSv{}, careerSv{}, doseRate{};   // acute dose of this mission, lifetime dose, current rate (Sv/h)
		// --- water and food (drunk and eaten automatically when available: a convention, not a survival game) ---
		double waterDef{};         // kg of body water lost and not replaced
		double glycogen{ 8.0e6 };  // J: muscle and liver carbohydrate, about a day of quiet living
		double fat{ 12.0 };        // kg of usable fat (a fit 62 kg woman)
		double fastDays{};         // days since the last meal
		double sweat{};            // kg/s, last step
		// --- injuries by part of the body, 0 (whole) .. 1 (useless); they heal slowly ---
		enum Part { HEAD, TORSO, ARMS, LEGS, PARTS };
		double hurt[PARTS]{};
		double liftMax{ 60 };      // kg she can lift briefly when well (set per person); carrying on the move: a third
		State state{ OK };
		double met{ 90 }, pulse{ 62 }, breath{ 13 };
		std::string warning;

		double AerobicMax() const { return vo2max * mass * 0.335 * RadFitness() * Condition(); }   // W: VO2 (L/min) * 20.1 kJ/L
		// water, food and a hurt torso take the edge off every effort
		double Condition() const;
		double LiftCapacity() const { return liftMax * (1 - 0.8 * hurt[ARMS]) * (0.6 + 0.4 * Condition()); }   // kg, briefly
		double CarryCapacity() const { return LiftCapacity() / 3; }                                            // kg, on the move
		// drink, eat and sweat for dt seconds. 'supplied': water and food at hand (a base, a ship, a breathable world
		// with her kit); 'suitWaterKg': drinking water in the suit (taken from it); 'airT' K at the skin, 'suited'
		void Sustain(double dt, bool supplied, double* suitWaterKg, double airT, bool suited);
		void Hurt(Part p, double amount);
		// radiation sickness takes the edge off: about -10 % aerobic power per Sv above 1, down to half
		double RadFitness() const { return std::clamp(1.0 - 0.1 * (doseSv - 1.0), 0.5, 1.0); }
		// absorb radiation at 'rateSvh' for dt seconds (call after Step, and after Treat)
		void Irradiate(double dt, double rateSvh);
		// a medical bay - the ship's, with the medicine of the book (Eastern and wave, not surgery): how fast it heals and
		// the highest acute radiation dose it can cure
		struct Care { double hoursPerUnit{ 48 }; double svPerDay{ 2 }; double maxSv{ 20 }; };
		// in a medical bay for dt seconds (the user, 2026-10-04): injuries heal in a time that follows their severity
		// (hoursPerUnit for an injury of 1.0), the acute dose is repaired while the bay can cure it (above, the sickness
		// goes on). Call after Step, before Irradiate
		void Treat(double dt, const Care& care);
		// days the acute radiation sickness of the present dose takes to kill without care (0: it does not kill)
		double ArsDays() const;
		// activityW: metabolic cost of what she does beyond standing; heatW: heat reaching the body from outside control
		void Step(double dt, double activityW, double ppO2, double ppCO2, double ambientP, bool suited, double heatW);
		void Spend(double joules) { wbal = (std::max)(0.0, wbal - joules / WCAP); }
		// touchdown at 'v' m/s (vertical): the suit and its frame take the first 7 m/s; beyond, injury; above 14, fatal
		// what hits first: the injuries follow the body's own physics - feet first, the legs take it (the spine only in a
		// very hard landing); on the back, the torso and the back of the head; head first, the head
		enum Contact { ON_FEET, ON_BACK, ON_HEAD };
		void Impact(double v, Contact c = ON_FEET);
		double O2Use() const { return state == DEAD ? 0 : met / 14.07e6; }       // kg/s, 14.07 MJ per kg O2
		double CO2Made() const { return O2Use() * 1.169; }                       // kg/s at RQ 0.85
		double Heat() const { return state == DEAD ? 0 : 0.8 * met; }            // W released as heat
		double Fatigue() const { return 1 - wbal; }
		double Effort() const { return std::clamp((met - 90) / (AerobicMax() - 90), 0.0, 1.2); }
		double RunLimit() const { return (3.0 + 2.0 * std::pow(wbal, 0.6)) * (1 - 0.7 * hurt[LEGS]); }     // m/s, hurt legs slow her
		bool CanAct() const { return state == OK; }

		static constexpr double WCAP = 40e3;   // J of metabolic work above the threshold
	};

	// metabolic power of moving over the ground (W); v m/s, g m/s^2, load = carried mass
	double LocomotionPower(double mass, double v, double g);
}
