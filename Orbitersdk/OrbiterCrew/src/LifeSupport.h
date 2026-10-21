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
		double contactT{};         // K, what her soles touch where it differs from groundT (a lava crust; 0 = groundT)
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
		double solarAlpha{ 0.20 };                    // the shell's sunlight absorptance (white outer layer; a set's plates over it add)
		double heatMaxW{ 400 }, coolMaxW{ 700 };      // thermal control capacity
		double copCool{ 3 };                          // heat pump to the radiator
		// the hotter the surroundings, the hotter the radiator must run: the pump's COP falls from copCool at 300 K to
		// copMin at copHotK (0: a constant COP, class 1). Its limit floats with the heat and the other loads on the supply
		double copMin{ 0.5 }, copHotK{ 0 };
		double CopAt(double tEnv) const
		{
			if (copHotK <= 300) return copCool;
			if (tEnv > copHotK)   // past it the radiator's own material limit: nothing more to reject 150 K hotter - the evaporator only
				return copMin * std::clamp(1 - (tEnv - copHotK) / 150, 0.0, 1.0);
			const double k = std::clamp((tEnv - 300) / (copHotK - 300), 0.0, 1.0);
			return (std::max)(copMin, copCool - (copCool - copMin) * k);
		}
		// the evaporator (class 2): water boiled off takes the heat the pump cannot - 2.26 MJ/kg, up to evapMaxW
		double evapCap{}, evap{}, evapMaxW{}, evapW{};   // kg, kg, W, W (last step)
		// the radiator on the pack: it must run hotter than the surroundings to give the heat away - the more it rejects,
		// the hotter (in heat it glows dull red past ~800 K); idle (heating) it settles to the surroundings, in deep cold
		// it frosts. compStrain: the compressor's electric draw against its nominal one (COP falling: it labours, louder)
		double tRad{ 295 }, compStrain{};
		// heat damage (HEAVY_SUIT.md): the outer structure follows the surroundings in ~10 min; past the materials' onset
		// (class DamageC, default the rating + 70 K) the insulation's spacers and bonds burn through (its conductance up to
		// x5), then the joint seals and bellows go (a leak, at the end the shell open). Faults that stay: the field's coils
		// quench, the drives' windings fail, the radiator's loop boils dry
		double tShell{ 295 }, dmgIns{}, dmgSeal{}, damageK{};
		bool fieldFail{}, drivesFail{}, coolFail{};
		// both suits (the user, 2026-10-18): the heat damage above and the cold. The outer structure's temperature is not the
		// surroundings' own: the leak through the insulation reaches the outer surface, which gives it away by radiation
		// (shellEps, shellArea) and to a gas - in deep space at 3 K the light suit's outside settles near -25 C, the heavy
		// suit's near -50 C; it is the battery (heating) that runs out, not the shell. Dense cold gas (Titan) takes the
		// outside down to the gas. The joint seals lie sealDepth of the way in from the outer surface to the inside (the
		// light suit's bearings near the outside, the heavy suit's under its insulation); below coldK they turn glassy and
		// crack as the joints flex (a leak, at the end the shell open), e-fold per 25 K, ~2 h at the onset while walking
		double shellArea{ 2.6 }, shellEps{ 0.8 }, sealDepth{ 0.3 }, coldK{}, tSeal{ 295 };
		bool coldSeal{};   // last step: the seals are in the cold, not the heat
		// the suit's own computer and model (class key Computer): "Karkas" - the light suit, exactly as it was; "Pantsir" -
		// the heavy suit: the radiative leak, the heat damage and faults above and its own suit computer. Nothing of the
		// heavy model reaches a suit whose class does not name it
		std::string computer{ "Karkas" };
		// the legs in molten lava (CrewMember sets immersion, m, and the melt's temperature): the boots and shins are their own
		// heat node - ~8 kJ/K, heated by the viscous melt at ~300 W/m2K over the sole and the sunk part of the shin (both
		// legs ~2 x (0.03 + 0.35 * depth) m2), cooling to the surroundings when out; what they pass inward through the leg
		// insulation (its share ~0.1 of the shell, radiative, worse as it burns) goes into the inside like the rest of the
		// leak. The heavy suit's model also burns the leg seals (dmgLeg) - burnt through, the shell is open
		double immersion{}, meltK{ 1450 }, tLeg{ 295 }, dmgLeg{};
		// the boots in two layers: the outer shell (tLeg, its contact limit contactK - class ContactC, default the onset) and
		// the seals under the insulation (tLegIn, following with legSoak s - class LegSoakS); dmgLegOut: the outer shell burnt
		double contactK{}, legSoak{ 120 }, tLegIn{ 295 }, dmgLegOut{};
		bool Heavy() const { return computer == "Pantsir"; }
		double tMin{ 116 }, tMax{ 394 };              // K: rated environment -157..+121 C
		double ppO2{ 30 }, ppCO2{ 0.05 };             // helmet gas
		double driveW{}, thermalW{}, drawW{};         // last step
		double heatW{};                               // last step: heat moved, + cooling, - heating
		// radiation: passive shielding (BNNT with hydrogen + a layered shield) and an active magnetic field on the battery.
		// fieldFactor divides the charged particles' dose; the cosmic rays only by ~1.5 (Radiation::Dose applies both).
		double shieldGcm2{ 2.0 };                    // g/cm^2 areal density
		bool fieldOn{};                               // magnetic shield switched on (works only while powered)
		double fieldW{ 600 };                        // its draw at full level, W
		double fieldFactor{ 30 };                    // charged particles divided by this with the field up at full level
		// (class 2 «Панцирь», HEAVY_SUIT.md) the field's level, 0.25..1 in steps: its draw and its deflection grow together
		// (the coils' current; the cutoff rigidity with it), 1 + (fieldFactor - 1) * level
		double fieldLevel{ 1.0 };
		double fieldHeat{};                           // share of the field's draw left in the equipment as heat (class 2: 0.15)
		bool fieldSteps{};                            // its switch steps 25/50/75/100 % (class 2); else on/off at full
		double FieldW() const { return fieldW * fieldLevel; }
		double FieldFactorNow() const { return 1 + ((std::max)(1.0, fieldFactor) - 1) * fieldLevel; }
		bool FieldUp() const { return fieldOn && Powered() && !shedField; }
		// the high-Z liner (tantalum) under the shield: the electrons' bremsstrahlung the field cannot turn away, x this
		double bremsFactor{ 1.0 };
		double tIn{ 295.15 }, tSet{ 295.15 };          // K: the air and the cooling garment inside, and what the control holds
		double cIn{ 15000 };                          // J/K: garment water loop, air, underwear
		double gBody{ 15 };                           // W/K: her skin to the garment and the air inside
		// (the class) how it walks: its pace against the light suit's (a heavy suit is slower), its footfall's pitch
		double walkFactor{ 1.0 }, stepPitch{ 1.0 };
		double armOutDeg{ -1 }, legOutDeg{ -1 }, foreArmInDeg{};
		bool driveSound{};   // its joint drives are heard inside (class DriveSound; the Pantsir's ring motors)   // the stance its mesh is built for (class ArmOutDeg / LegOutDeg; -1: by WalkFactor)
		// a rigid suit whose joint drives brake when its wearer stops acting (or the power fails): it stays standing with her
		// inside instead of falling (class key LocksUpright)
		bool locksUpright{};
		bool drivesOn{ true };   // the servo drives: switched by hand (off: no help carrying the suit, slower, no bounding)
		double liftKg{ 200 };        // kg anyone in it lifts and carries with the drives on (Karkas 200, Pantsir 350): the suit evens people out

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
		// too cold: below -20 C the cells give less, below -40 C they cannot deliver - everything off; back above -30 C
		bool coldTrip{};
		static constexpr double EQ_COLDWARN = 253.15, EQ_COLD = 233.15, EQ_COLDBACK = 243.15;
		// the optics in radiation: colour centres in the visor glass and the light guides darken them (blue first), the
		// projection's sources weaken; out of the radiation the glass anneals slowly. radRate: Sv/h where she is (set before Step)
		double radRate{}, optDark{};   // optDark 0 clear .. 0.95
		bool econ{};   // economy: half the thermal capacity, slower fans - a longer battery, a wider swing inside

		bool Powered() const { return batt > 0 && !tripped && !coldTrip; }
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
		// heat through the shell from surroundings at T (W, + inward): the multilayer insulation passes heat by radiation, so
		// its conductance grows as (T^2 + Ti^2)(T + Ti) - the rated value holds up to ~500 K, at +600 C it is ~2x, at +775 C
		// ~3x (HEAVY_SUIT.md: the evaporator then cannot keep up and the heat reaches her)
		double Leak(double T) const
		{
			const double Ti = 295, rad = (T * T + Ti * Ti) * (T + Ti) / (4.0 * 500.0 * 500.0 * 500.0);
			if (!Heavy()) return Conductance() * (T - Ti) * (1 + 4 * dmgIns);   // the light suit: its rated conductance (burnt: worse)
			return Conductance() * (T - Ti) * (std::max)(1.0, rad) * (1 + 4 * dmgIns);
		}
		bool Venting() const { return vent && Powered(); }
		// the helmet's front (in a ship's or a station's air). helmetOpen is what is asked; helmetPos the front itself,
		// 0 shut .. 1 open: opening first equalises the helmet with the outside through a valve (2 s), then unlatches and
		// moves the front (4 s); closing moves it back (4 s) and latches: the cabin's air shut in, the regulator goes on.
		// From the moment the seal cracks she breathes the outside and her head takes its heat (and its dose: CrewMember);
		// the tank and the sorbent are not spent while it is open. Opened only where the outside allows it (CrewMember: the interlock)
		bool helmetOpen{};
		double helmetPos{}, helmetEq{};   // the front 0..1; the equalising before it moves, s
		bool HelmetCracked() const { return helmetPos > 0.001; }
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
		// the hottest surroundings it holds now (K): the pump within its capacity and what the supply leaves it after the
		// other loads (otherW: life, drives, lamps, the field - the field is shed first, so the limit rises without it),
		// plus (evapToo) the evaporator while it has water. It floats with the loads and the equipment's heat
		double HeatLimitK(double bodyHeatW, double otherW, bool evapToo = false) const
		{
			for (double T = 300; T <= 1200; T += 5)
			{
				const double q = bodyHeatW + Leak(T) + gEq * (tEq - tIn);
				const double cop = CopAt(T), pumpCap = cop > 0.01 ? (std::min)(econ ? 0.5 * coolMaxW : coolMaxW, (std::max)(0.0, supplyW - otherW) * cop) : 0.0;
				if (q > pumpCap + (evapToo && evap > 0 ? evapMaxW : 0.0)) return T - 5;
			}
			return 1200;
		}
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
