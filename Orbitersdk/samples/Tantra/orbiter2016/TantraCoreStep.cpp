// Tantra: the energy core's step (core/TantraCore) - after the plant's and the damage model's steps in clbkPreStep, so an
// impact of this step reaches the core at once; its consequences reach the plant (and the hull) through their own inputs.
#include "Tantra.h"

#include <algorithm>

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
