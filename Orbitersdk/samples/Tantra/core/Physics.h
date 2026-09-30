// Tantra core: physics helpers. No Orbiter dependencies.
#pragma once
#include <algorithm>
#include <cmath>

namespace tantra {

constexpr double C_LIGHT = 299792458.0;  // m/s
constexpr double G0 = 9.80665;           // m/s^2

// Clamp beta away from 1 so the factors below stay finite.
inline double SafeBeta2(double beta) { return (std::min)(beta * beta, 0.999999999); }

inline double Gamma(double beta) { return 1.0 / std::sqrt(1.0 - SafeBeta2(beta)); }

// Relativistic rocket, thrust collinear with velocity, coordinate frame:
//   dv = (1 - beta^2) * ve * dm / m
// Orbiter integrates dv = Isp * dm / m with dm/dt = F / Isp, so scaling both
// Isp and max thrust by (1 - beta^2) keeps the mass flow and reproduces the
// relativistic mass ratio. Transverse thrust is only approximated.
inline double RocketFactor(double beta) { return 1.0 - SafeBeta2(beta); }

// Proper-time rate of the ship clock relative to the reference frame.
inline double ProperTimeRate(double beta) { return std::sqrt(1.0 - SafeBeta2(beta)); }

// Highest throttle level that keeps the acceleration at or below aLimit.
inline double GLimitLevel(double aLimit, double mass, double maxThrust) {
    if (maxThrust <= 0.0) return 0.0;
    return std::clamp(aLimit * mass / maxThrust, 0.0, 1.0);
}

}  // namespace tantra
