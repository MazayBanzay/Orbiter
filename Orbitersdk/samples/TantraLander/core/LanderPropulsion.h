// «Грань» 25,4 м: the propulsion as real systems. No Orbiter dependencies (tests/lander_core_test.cpp).
//  * 8 ion-trigger units in magnetic cups: 2 marches (УВТ ±15°) and 6 lift cups in two rows of three under two doors.
//  * Each unit: the field B (ramped, REBCO windings: the current against the critical current, the windings' temperature
//    against its cold line), the trigger (charged from the store: P_jet/Q), the charge injectors A/B, the argon feed.
//  * Thrust: F = 2P/v below the field cap F ≤ B²/2μ0·S; the emergency mode lowers v to 11,5 km/s (more thrust from the same
//    power) and heats the windings: the automat ends it at a 20 % current margin, the physics quenches at 0 %.
//  * A stall (the plasma loses the field): thrust to zero in ~1 s, a restart from the other injector. A quench: the field
//    drops, the windings warm, the unit waits for its cold line, then ramps up again.
//  * Two cryocoolers (one standby), the argon attitude thrusters, the nose reserve 0,5 т.
//  * Т1Б-А: three argon tanks - носовой, кормовой, в кессоне крыла; the engines feed from the aft one (the others through the
//    cross-feed); the transfer by two paths (А основной, Б - второй насос/клапан, 40 кг/с каждый) keeps the CG by mode:
//    подача (крыло, нос → корма), вход (корма → нос, все 6,3 т: планирование и баллистика), висение (ЦМ к центру рядов −3,21).
#pragma once

namespace tantra::lander {

enum State { kOff = 0, kReady, kRun, kLimit, kFault, kLost };   // выкл / готов / работа / предел / отказ / потерян
struct Node { int state = kOff; const char* why = ""; };       // why: the reason in Russian (a literal)
const char* StateRu(int s);

enum Stage { kStOff = 0, kStFieldUp, kStTrigger, kStRun, kStStall, kStQuench, kStBroken };

struct Unit {
    // design
    bool march = false;
    double Fnom = 0, vNom = 0, S = 0, x = 0, z = 0, line = 0, C = 0, ramp = 0;
    // state
    Node node;
    int stage = kStOff;
    double B = 0, Bset = 0, ratio = 0, margin = 1, coilT = 20, coilHeat = 0, coilCool = 0;
    double thrust = 0, thrustSet = 0, mdot = 0, pJet = 0, pTrig = 0, v = 0, fieldCap = 0;
    double timer = 0;                         // the stage's timer (trigger, restart)
    double emTime = 0; bool emergency = false; // s in the emergency mode
    bool emBlocked = false;                   // ended by the current margin: no emergency mode until the windings cool
    int injector = 0; bool injOk[2] = {true, true};
    bool broken = false;
    double tvcP = 0, tvcY = 0;                // deg (marches)
    bool tvcWinding[2] = {true, true};
    int side = 0;                             // the row (lift cups), the side (marches)
};

struct Row { double door = 0, frame = 0; bool winding[2] = {true, true}; Node node; };   // 0..1 each
struct Tank { double kg = 0; bool leak = false, isolated = false; Node node; };
enum Trim { kTrimHold = 0, kTrimFeed, kTrimEntry, kTrimHover };   // what the transfer keeps
struct Cooler { bool ok = true, powered = true, running = false; double load = 0; Node node; };

// what the core gives the propulsion each step
struct PropIn {
    double dt = 0;
    bool armed = false;                       // the master switch
    double thr[8] = {};                       // 0..1 of the nominal thrust (the avionics' distribution)
    bool em[8] = {};                          // the emergency mode asked
    double tvcP[2] = {}, tvcY[2] = {};        // deg asked
    bool deploy[2] = {};                      // the rows out
    double trigSupply = 1.0;                  // share of the trigger power the store gives
    bool coolerPower[2] = {true, true};       // the shedding
    bool noseValve = false;                   // the porous nose blow
    bool reserveRelease = false;              // the engines may take the nose reserve (the pilot's override)
    double rcs = 0;                           // N·m asked of the attitude thrusters (abs used for the flow)
    double shock[8] = {};                     // g on each unit (an impact, a hard landing)
    double shockCooler[2] = {}, shockTank[3] = {}, shockFrame[2] = {};
    int trim = kTrimHold;                     // the transfer's schedule (the core sets it by mode)
    bool transferFail[2] = {};                // a transfer path failed (the pump/valve): А, Б
};

class Propulsion {
public:
    Propulsion();
    void Step(const PropIn& in);
    Unit u[8];
    Row row[2];
    Tank tank[3];                             // spec::kTNose, kTAft, kTWing
    Cooler cooler[2];
    Node cryo, feed, cross, charges, nose, rcs, transfer;
    bool crossOpen = false;
    bool pathOk[2] = {true, true};            // the transfer paths А (основной), Б (второй насос/клапан)
    double transferFlow = 0;                  // kg/s moved now (+ into the nose, - out of it)
    double chargesKg = 0, noseFlow = 0, rcsFlow = 0, argonFlow = 0;
    double powerDemand = 0;                   // W the trigger, the field ramps (from the store)
    double coolerPowerDemand = 0;             // W the coolers
    double ArgonKg() const { return tank[0].kg + tank[1].kg + tank[2].kg; }
    void SetArgon(double nose, double aft, double wing) { tank[0].kg = nose; tank[1].kg = aft; tank[2].kg = wing; }
    double MassKg() const;                    // the budget (spec::kBudget) + the charges + the argon
    double XCg() const;                       // m along the body (mass_budget frame)
    double NoseTarget(int trim) const;        // kg the transfer wants in the nose tank
    double Usable(bool release) const;        // kg the engines may take
    double LiftAvail(bool emergency) const;   // N the lift cups give at full, those running or able to
    bool RowOut(int r) const { return row[r].frame >= 1.0; }
    static int Opposite(int lift) { return 7 - (lift - 2); }   // central symmetry: 2↔7, 3↔6, 4↔5
private:
    void StepUnit(int i, const PropIn& in, double coolShare, bool argonOk);
    void Quench(Unit& un, const char* why);
    void Stall(Unit& un, const char* why);
    void Transfer(const PropIn& in);
};

}  // namespace tantra::lander
