// Tantra: the energy core's step (core/TantraCore) - after the plant's and the damage model's steps in clbkPreStep, so an
// impact of this step reaches the core at once; its consequences reach the plant (and the hull) through their own inputs.
#include "Tantra.h"

#include <algorithm>
#include <cmath>

void Tantra::CoreStep(double simdt) {
    namespace pl = tantra::plant;
    tantra::tcore::Inputs in;
    in.dt = simdt;
    in.plant = &plant_;
    in.out = &plantOut_;
    in.dmg = &damage_;
    in.dmgEnabled = GetDamageModel() != 0;
    in.structG = structG_;
    in.settling = settleTimer_ > 0.0;
    in.crestsOut = (std::max)(tuck_, carriage_.Pose().tuck) < 0.5;
    in.massLeft = plantOut_.mass == pl::kProducts ? -1.0 : GetPropellantMass(plantOut_.mass == pl::kArgon ? argon_ : iron_);
    core_.Step(in);
}

// Under warp over x10 on the ground the ship is frozen (Tantra::Coast) and nothing else is simulated; the energy core goes on
// (the user: «Ядро шагами должно считаться»): the plant without thrust (the stern cools, the windings, the fuel) and the core
// (the ВЭУ, the store charging, the cryo) in substeps of ~1 s, at most 200 a frame (then longer); no jolts (the ship rests).
void Tantra::CoreWarpStep(double simdt) {
    namespace pl = tantra::plant;
    if (simdt <= 0.0) return;
    const int n = (std::max)(1, (std::min)(200, int(std::ceil(simdt / 1.0))));
    const double h = simdt / n;
    pl::Env e;
    e.rho = GetAtmDensity();
    e.air = e.rho > 1e-5;
    e.level = 0.0;                                                        // no thrust while frozen
    e.crestsOut = (std::max)(tuck_, carriage_.Pose().tuck) < 0.5;
    e.noseT = damage_.Temperature(tantra::damage::kZoneNose);
    tantra::tcore::Inputs in;
    in.plant = &plant_; in.out = &plantOut_; in.dmg = &damage_; in.dmgEnabled = GetDamageModel() != 0;
    in.structG = 1.0; in.settling = true;                                 // resting: no jolts
    in.crestsOut = e.crestsOut;
    for (int i = 0; i < n; ++i) {
        plantOut_ = plant_.Step(h, e, nullptr);                           // (no random failures without thrust)
        in.dt = h;
        in.massLeft = plantOut_.mass == pl::kProducts ? -1.0 : GetPropellantMass(plantOut_.mass == pl::kArgon ? argon_ : iron_);
        core_.Step(in);
    }
}
