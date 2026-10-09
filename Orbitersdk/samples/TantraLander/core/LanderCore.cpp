#include "LanderCore.h"
#include "LanderSpec.h"

#include <algorithm>
#include <cmath>

namespace tantra::lander {
using namespace spec;

Core::Core() : storeE_(kStoreE), batt_(kAnalogBatt) {}

void Core::Step(const Inputs& in) {
    const double dt = in.dt;
    const double frac = storeE_ / kStoreE;
    // the shedding level from the store's charge
    const int level = storeE_ <= 0 ? 4 : frac < 0.05 ? 3 : frac < 0.10 ? 2 : frac < 0.20 ? 1 : 0;
    s_.shedWhy = level == 0 ? "" : level == 1 ? "накопитель < 20 %: снят резервный криокулер, приводы медленнее"
               : level == 2 ? "накопитель < 10 %: снят фотонный контур, ведёт аналоговый"
               : level == 3 ? "накопитель < 5 %: сняты маршевые триггеры, резерв посадки - подъёмным чашам"
                            : "накопитель пуст: триггеры, криогеника, фотоника без питания; аналог и жизнеобеспечение на батарее";
    const bool storeOk = storeE_ > 0;

    // the avionics
    AvIn ai;
    ai.dt = dt; ai.mode = in.mode; ai.f = in.f; ai.p = in.p; ai.prop = &prop_;
    ai.photonPower = storeOk && level < 2;
    ai.analogPower = batt_ > 0;
    ai.shockCabin = in.shockCabin;
    ai.crew = in.crew;
    av_.Step(ai, out_);

    // the propulsion with what the store gives
    PropIn pi;
    pi.dt = dt; pi.armed = in.master;
    for (int i = 0; i < kUnits; ++i) { pi.thr[i] = out_.thr[i]; pi.em[i] = out_.em[i]; pi.shock[i] = in.shock[i]; }
    if (level >= 3) for (int i = 0; i < kMarch; ++i) pi.thr[i] = 0.0;
    for (int i = 0; i < kMarch; ++i) { pi.tvcP[i] = out_.tvcP[i]; pi.tvcY[i] = out_.tvcY[i]; }
    for (int r = 0; r < 2; ++r) {
        pi.deploy[r] = in.deployLift || out_.deploy[r];
        pi.shockCooler[r] = in.shockCooler[r]; pi.shockFrame[r] = in.shockFrame[r];
        pi.transferFail[r] = in.transferFail[r];
    }
    for (int t = 0; t < kTanks; ++t) pi.shockTank[t] = in.shockTank[t];
    pi.trim = (in.preEntry || in.mode == kEntry || in.mode == kBallistic) ? kTrimEntry
            : (in.mode == kHover || in.mode == kVertLand || in.mode == kTransition || in.mode == kDock) ? kTrimHover
            : in.mode == kFlight ? kTrimFeed : kTrimHold;
    const bool aAlive = prop_.cooler[0].ok;
    pi.coolerPower[0] = storeOk;
    pi.coolerPower[1] = storeOk && (level < 1 || !aAlive);   // B keeps A's priority when A is dead
    pi.trigSupply = storeOk ? 1.0 : 0.0;
    pi.noseValve = out_.noseValve;
    pi.reserveRelease = in.reserveRelease;
    pi.rcs = out_.rcs;
    prop_.Step(pi);

    // the energy: by priority, the store's power limit
    double trigLift = 0, trigMarch = 0;
    for (int i = 0; i < kUnits; ++i) (i < kMarch ? trigMarch : trigLift) += prop_.u[i].pTrig;
    const double fieldRamp = std::max(0.0, prop_.powerDemand - trigLift - trigMarch);
    double avail = storeOk ? std::min(kStoreP, storeE_ / std::max(dt, 1e-6)) : 0.0;
    auto give = [&](double w) { const double g = std::min(w, avail); avail -= g; return g; };
    s_.loadLife = give(kLifeP);
    const double battCharge = batt_ < kAnalogBatt ? give(kAnalogP * 4.0) : give(kAnalogP);
    s_.loadTrigLift = give(trigLift + fieldRamp);
    s_.loadCryo = give(prop_.coolerPowerDemand);
    s_.loadTrigMarch = give(trigMarch);
    s_.loadPhoton = ai.photonPower ? give(kPhotonP) : 0.0;
    s_.loadDrives = give(kDriveP * (level >= 1 ? 0.5 : 1.0));
    const double out = s_.loadLife + battCharge + s_.loadTrigLift + s_.loadCryo + s_.loadTrigMarch + s_.loadPhoton + s_.loadDrives;
    storeE_ = std::max(0.0, storeE_ - out * dt);
    // the analog battery: charged from the store, carries the analog contour and life support when the store is empty
    batt_ = std::min(kAnalogBatt, batt_ + (battCharge - kAnalogP) * dt);
    if (!storeOk) batt_ = std::max(0.0, batt_ - (kLifeP) * dt);

    // the snapshot
    s_.storeE = storeE_; s_.storeFrac = storeE_ / kStoreE; s_.storeOut = out; s_.analogBatt = batt_;
    s_.store = !storeOk ? Node{kFault, "накопитель пуст"} : level >= 2 ? Node{kLimit, "резерв посадки"} : level == 1 ? Node{kLimit, "экономия"}
             : Node{kRun, "заряжен от «Тантры»"};
    s_.bus = !storeOk ? (batt_ > 0 ? Node{kLimit, "только батарея аналогового контура"} : Node{kLost, "питания нет"}) : Node{kRun, "работа"};
    s_.photon = av_.photon; s_.analog = av_.analog; s_.mech = av_.mech; s_.mode = av_.mode;
    s_.active = av_.active; s_.decision = av_.decision;
    s_.thrustLift = s_.thrustMarch = 0;
    for (int i = 0; i < kUnits; ++i) {
        const Unit& u = prop_.u[i];
        s_.unit[i] = u.node; s_.B[i] = u.B; s_.margin[i] = u.margin; s_.coilT[i] = u.coilT; s_.thrust[i] = u.thrust; s_.thr[i] = pi.thr[i];
        s_.shed[i] = av_.shed[i];
        if (av_.shed[i] && u.stage != kStRun && !u.broken && u.node.state != kFault) s_.unit[i] = {kReady, "погашена: противолежащая отказала"};
        if (level >= 3 && i < kMarch && !u.broken) s_.unit[i] = {kOff, "снята: резерв накопителя"};
        (i < kMarch ? s_.thrustMarch : s_.thrustLift) += u.thrust;
    }
    // what the lift cups give at the nominal now: the running set, the shed ones and the failed ones left out
    double nominal = 0;
    for (int i = kMarch; i < kUnits; ++i)
        if (!av_.shed[i] && !prop_.u[i].broken && prop_.u[i].stage == kStRun && prop_.RowOut(prop_.u[i].side)) nominal += prop_.u[i].Fnom;
    s_.liftNominal = nominal;
    s_.twNominal = in.f.mass > 0 ? nominal / (in.f.mass * in.f.g) : 0.0;
    for (int r = 0; r < 2; ++r) { s_.row[r] = prop_.row[r].node; s_.cooler[r] = prop_.cooler[r].node; }
    for (int t = 0; t < kTanks; ++t) { s_.tank[t] = prop_.tank[t].node; s_.tankKg[t] = prop_.tank[t].kg; }
    s_.cryo = prop_.cryo; s_.cross = prop_.cross; s_.feed = prop_.feed; s_.charges = prop_.charges; s_.nose = prop_.nose; s_.rcs = prop_.rcs;
    s_.transfer = prop_.transfer; s_.transferFlow = prop_.transferFlow; s_.xcg = prop_.XCg(); s_.mass = prop_.MassKg();
    s_.argonKg = prop_.ArgonKg(); s_.argonFlow = prop_.argonFlow; s_.noseFlow = prop_.noseFlow; s_.chargesKg = prop_.chargesKg;
    for (int i = 0; i < kMarch; ++i) { s_.tvcY[i] = prop_.u[i].tvcY; s_.tvcP[i] = prop_.u[i].tvcP; }
    s_.elevon = out_.elevon; s_.flap = out_.flap; s_.noseValve = out_.noseValve;
    s_.tip = out_.tip; s_.gearDown = out_.gearDown;
}

}  // namespace tantra::lander
