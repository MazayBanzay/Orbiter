#include "Drive.h"

#include <algorithm>

namespace tantra {

double Drive::PelletMass() const {
    // Fuel flow per chamber at full thrust divided by the full pellet rate.
    return spec_.maxThrust / spec_.exhaust / spec_.maxPelletRate;
}

double Drive::JetPower(double level) const {
    const double fuelFlow = level * spec_.chambers * spec_.maxThrust / spec_.exhaust;
    return fuelFlow * spec_.massToEnergy * 8.987551787e16;
}

void Drive::SetStoreFraction(double f) { store_ = std::clamp(f, 0.0, 1.0) * spec_.storeCapacity; }

bool Drive::CanRaiseField(double fieldLevel) const {
    const double need = (1.0 - fieldLevel) * spec_.chambers * spec_.chamberFieldEnergy;
    return store_ >= need;
}

double Drive::CompensationCapacity(double level, double mass) const {
    if (mass <= 0.0) return 0.0;
    const double power = spec_.recoveryFraction * JetPower(level) +
                         (std::max)(0.0, spec_.plantPower - spec_.housekeeping);
    return (std::min)(spec_.compMaxAccel, power / spec_.compWattPerNewton / mass);
}

void Drive::Update(double dt, double fieldLevel, double level, double mass, double thrustAccel) {
    // Move field energy between the store and the chambers.
    const double target = fieldLevel * spec_.chambers * spec_.chamberFieldEnergy;
    const double delta = target - chamberField_;
    if (delta > 0.0) store_ -= delta;
    else store_ -= delta * spec_.roundTrip;  // delta < 0: energy comes back, minus losses
    chamberField_ = target;

    // The power plant trickle-charges the store with whatever the systems leave.
    store_ += (std::max)(0.0, spec_.plantPower - spec_.housekeeping) * dt;
    store_ = std::clamp(store_, 0.0, spec_.storeCapacity);

    recovered_ = spec_.recoveryFraction * JetPower(level);
    compensated_ = (std::min)(thrustAccel, CompensationCapacity(level, mass));
    residual_ = (std::max)(0.0, thrustAccel - compensated_);
}

double Drive::LevelForResidual(double residualLimit, double mass, double fieldLevel) const {
    if (fieldLevel < 1.0 || mass <= 0.0) return 0.0;
    // a(level) = level * Fmax / m grows linearly; compensation capacity grows with the
    // recovered power until it hits the apparatus limit. Bisect on the residual.
    const double fmax = spec_.chambers * spec_.maxThrust;
    auto residual = [&](double lv) {
        const double a = lv * fmax / mass;
        return a - (std::min)(a, CompensationCapacity(lv, mass));
    };
    if (residual(1.0) <= residualLimit) return 1.0;
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 40; ++i) {
        const double mid = 0.5 * (lo + hi);
        (residual(mid) <= residualLimit ? lo : hi) = mid;
    }
    return lo;
}

}  // namespace tantra
