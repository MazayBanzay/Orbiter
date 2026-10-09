// «Грань» 25,4 м: the lander's systems core - Step(dt, Inputs) -> Snapshot, as core/TantraCore of «Тантра». No Orbiter
// dependencies (tests/lander_core_test.cpp, docs/CORE.md).
//  * НАКОПИТЕЛЬ (charged from «Тантра»: the rescue rocket spends the common stock) feeds by priority: life support, the analog
//    contour's battery, the lift cups' triggers, cryocooler A, the march triggers, the photonic contour, cryocooler B, the drives.
//    As it empties the core sheds from the bottom: < 20 % cryocooler B and the drives' speed, < 10 % the photonic contour
//    (the analog one flies), < 5 % the march triggers (the landing reserve for the lift cups), 0 - the triggers stall, the
//    cryo stops (the windings warm to a quench), the analog contour and life support go on the battery.
//  * Propulsion (core/LanderPropulsion) and avionics (core/LanderAvionics) are stepped with what the store gives.
//  * The transfer schedule by mode: the hover modes and the hangar capture keep the CG on the rows' centre; the entry, the
//    ballistic descent and the pre-entry command (preEntry, in orbit: 61 с двумя путями, 121 с одним) - the nose tank full;
//    the flight - the wing and the nose into the aft tank; the runway and the rest - no transfer.
#pragma once
#include "LanderAvionics.h"
#include "LanderPropulsion.h"

#include <string>

namespace tantra::lander {

struct Inputs {
    double dt = 0;
    bool master = true;                       // the master switch (arms the units)
    int mode = kHover;
    Flight f;                                 // the sensors
    Pilot p;
    bool reserveRelease = false;              // the engines may take the nose's 0,5 т
    bool preEntry = false;                    // the pre-entry transfer into the nose tank (in orbit, before the entry)
    bool transferFail[2] = {};                // the transfer paths А, Б failed (a test or a damage)
    int crew = 0;                             // people aboard (the hover rule)
    double shock[8] = {};                     // g on each unit
    double shockCabin = 0, shockCooler[2] = {}, shockTank[3] = {}, shockFrame[2] = {};
    bool deployLift = true;                   // the rows out (the avionics also asks them in the hover modes)
};

struct Snapshot {
    Node store, bus, photon, analog, mech, mode;
    Node unit[8], row[2], cooler[2], cryo, tank[3], cross, feed, charges, nose, rcs, transfer;
    double storeE = 0, storeFrac = 0, storeOut = 0, analogBatt = 0;   // J, -, W, J
    double loadLife = 0, loadTrigLift = 0, loadTrigMarch = 0, loadCryo = 0, loadPhoton = 0, loadDrives = 0;   // W given
    double thrustLift = 0, thrustMarch = 0, liftNominal = 0, twNominal = 0;   // N, N, N available at the nominal, T/W
    double argonKg = 0, argonFlow = 0, noseFlow = 0, chargesKg = 0;
    double tankKg[3] = {}, transferFlow = 0, xcg = 0, mass = 0;   // kg (нос, корма, крыло), kg/s, m, kg
    double B[8] = {}, margin[8] = {}, coilT[8] = {}, thrust[8] = {}, thr[8] = {};
    double tvcY[2] = {}, tvcP[2] = {}, elevon = 0, flap = 0;
    double tip = 90; bool gearDown = false;   // deg the wing tips, the gear (WingConfig by mode)
    bool shed[8] = {}, noseValve = false;
    int active = kPhoton;
    const char* decision = "";
    const char* shedWhy = "";                 // the power shedding now
};

class Core {
public:
    Core();
    void Step(const Inputs& in);
    const Snapshot& S() const { return s_; }
    Propulsion& Prop() { return prop_; }
    const Avionics& Av() const { return av_; }
    void SetStore(double joules) { storeE_ = joules; }   // the charge from «Тантра» before the undocking
private:
    Propulsion prop_;
    Avionics av_;
    AvOut out_;
    Snapshot s_;
    double storeE_, batt_;
    double trigLiftLast_ = 0, trigMarchLast_ = 0, cryoLast_ = 0;
};

}  // namespace tantra::lander
