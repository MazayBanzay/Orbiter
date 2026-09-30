#include "Radiation.h"

#include <algorithm>
#include <cmath>

namespace tantra {

namespace {
constexpr double kPi = 3.14159265358979;
}

double Radiation::DoseRate(double jetPower, double r, bool inBeam) const {
    if (jetPower <= 0.0) return 0.0;
    r = std::max(r, 1.0);
    const double perFlux = spec_.bodyArea / spec_.bodyMass;  // Gy/s per W/m^2 absorbed
    double rate = GammaPower(jetPower) / (4.0 * kPi * r * r) * spec_.gammaAbsorbed * perFlux;
    if (inBeam) {
        const double w = r * std::tan(spec_.jetHalfAngle);
        rate += ChargedPower(jetPower) / (kPi * w * w) * spec_.jetAbsorbed * perFlux;
    }
    return rate;
}

double Radiation::LethalRadius(double jetPower, double seconds) const {
    if (jetPower <= 0.0 || seconds <= 0.0) return 0.0;
    const double flux = spec_.lethalDose / seconds * spec_.bodyMass / (spec_.bodyArea * spec_.gammaAbsorbed);
    return std::sqrt(GammaPower(jetPower) / (4.0 * kPi * flux));
}

double Radiation::LethalBeamRange(double jetPower, double seconds) const {
    if (jetPower <= 0.0 || seconds <= 0.0) return 0.0;
    const double flux = spec_.lethalDose / seconds * spec_.bodyMass / (spec_.bodyArea * spec_.jetAbsorbed);
    const double t = std::tan(spec_.jetHalfAngle);
    return std::sqrt(ChargedPower(jetPower) / (kPi * t * t * flux));
}

double Radiation::InterceptedPower(double jetPower, double R, double d, bool beamOnPlanet) const {
    if (jetPower <= 0.0 || d <= 0.0) return 0.0;
    d = std::max(d, R * 1.0001);
    // Fraction of an isotropic source caught by a sphere: solid angle / 4 pi.
    const double capFraction = 0.5 * (1.0 - std::sqrt(1.0 - (R / d) * (R / d)));
    double p = GammaPower(jetPower) * capFraction;
    if (beamOnPlanet) {
        const double w = d * std::tan(spec_.jetHalfAngle);
        p += ChargedPower(jetPower) * std::min(1.0, (R * R) / (w * w));
    }
    return p;
}

Hazard Radiation::PlanetHazard(double intercepted) const {
    if (intercepted >= spec_.catastropheFraction * spec_.earthSolarInput) return Hazard::Catastrophe;
    if (intercepted >= spec_.harmFraction * spec_.earthSolarInput) return Hazard::Harm;
    return Hazard::None;
}

double Radiation::PlanetSafeLevel(double jetPowerFull, double R, double d, bool beamOnPlanet) const {
    const double full = InterceptedPower(jetPowerFull, R, d, beamOnPlanet);
    if (full <= 0.0) return 1.0;
    return std::min(1.0, spec_.harmFraction * spec_.earthSolarInput / full);
}

double Radiation::PersonSafeLevel(double jetPowerFull, double r, bool inBeam) const {
    const double full = DoseRate(jetPowerFull, r, inBeam) * spec_.hazardHorizon;
    if (full <= 0.0) return 1.0;
    return std::min(1.0, spec_.lethalDose / full);
}

DoseRecord& DoseLedger::Add(const std::string& name, double rate, double dt) {
    auto it = std::find_if(records_.begin(), records_.end(), [&](const DoseRecord& r) { return r.name == name; });
    if (it == records_.end()) {
        records_.push_back(DoseRecord{name});
        it = records_.end() - 1;
    }
    it->rate = rate;
    it->dose += rate * dt;
    return *it;
}

}  // namespace tantra
