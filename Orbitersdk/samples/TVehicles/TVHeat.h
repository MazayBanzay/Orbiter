// TVHeat - heat damage of a ground vehicle on hot ground and lava (the user, 2026-10-18): one model for every vehicle of
// TVehicles (МПУ and Э.МПУ now; any wheeled machine later fills the specs below). Orbiter-free: plain physics, testable
// on its own (TVehicles/tests/heat_test.cpp). The Orbiter side - where the ground is hot, which wheel stands on what -
// lives in the vehicle (TVChassis.h HeatStep).
//
// The approach is the suits' (OrbiterCrew LifeSupport.cpp, Suit::Step), copied, not called:
//   - each part is a heat node (heat capacity, its own surroundings) stepped implicitly - time acceleration safe;
//   - past its material's onset the damage grows e-fold per 60 K: 30 min to burn through at the onset, ~4 min at +120 K,
//     never faster than 5 s (as the suit's legs in the melt);
//   - the damage stays (saved in the scenario) and its stages bring faults that stay.
//
// The nodes and their numbers (SI; estimates are marked so):
//   TYRE (one per wheel): the airless wheel's spring band, spokes and tread. Material: a woven spring mesh of Mo-47.5Re
//     (molybdenum-rhenium: real, made for rocket thrusters; ductile down to ~77 K - the Io night does not break it - and
//     strong to ~1200 K) under tread chevrons of C/SiC (carbon fibre in silicon carbide; real, stable in vacuum to
//     ~1900 K). 120 kg of Mo-Re (0.21 kJ/kgK) + 30 kg of C/SiC (0.75) = 48 kJ/K. Onset 1150 K: the mesh's spring temper
//     creeps away under the load (~0.42 of Mo-Re's melting point) - the wheel flattens, drags, at the end it is dead.
//     Heat in: (1) the contact patch: a contact conductance hc in series with what the ground can give - a moving wheel
//     meets fresh ground every L/v seconds (e/sqrt(pi t), e the ground's effusivity), a standing one draws its spot
//     down (t grows) unless a melt under a thin crust feeds it (k/d). hc ~150 W/m2K: wire mesh on rough rock in vacuum at
//     ~15 kPa (estimate within the published 50-500 for metal/rock contacts without gas); (2) radiation: ~7.3 m2 of
//     wheel, emissivity ~0.5 (bare Mo-Re ~0.25, C/SiC ~0.85), half of it seeing the ground, half the sky; (3) in a melt:
//     ~300 W/m2K over the wetted part (the suits' figure for a viscous basalt melt); (4) out to the hub motor by radiation
//     from the hot rim (inner rim ~1 m2, effective emissivity ~0.3).
//   HUB MOTOR (one per wheel, its heat node is the vehicle's own motor model): SmCo magnets (rated 350 C) and glass-
//     polyimide windings (class 240). Onset 573 K: the insulation chars, the magnets start to lose for good. Heat from the
//     rim (above) and its radiators: they see half the ground and half the sky - over hot ground they cannot reject.
//   BELLY (one per vehicle): the underbody - the spine's belly skin and lower flanges. CNT fibre in a phthalonitrile
//     matrix (a real high-temperature resin, Tg > 720 K, rated ~580 K long-term); 1.0 t at 0.9 kJ/kgK = 0.9 MJ/K, ~25 m2
//     between the wheels, emissivity 0.85, facing the ground only (view factor ~1). Onset 650 K. Its inside: the
//     electronics bay and the battery bay above it, 25 W/K through the insulation (estimate). Stages (as the suit's
//     insulation, seals, breach): 0.3 - the drive's inverters in the spine fail (drive power halved), 0.5 - the battery
//     bay: the cells short inside (they lose 2 % of their charge a minute), 0.7 - the traction harness burns (no drive),
//     1.0 - the belly burns through (a cabin above it: its floor holed). The insulation over the cabin's floor goes with
//     it: the cabin's hull conductance x(1 + 4 damage), as the suit's insulation.
//   CRUST: the crew's rule (CrewMember.cpp: under 2 cm it breaks, 2..5 cm by a chance growing towards 2 cm, the same
//     per-cell hash) for a person of ~250 kg on Io (449 N). A thin plate on a fluid breaks under a concentrated load at
//     P ~ C sigma h^2 (Westergaard), so the thicknesses scale with sqrt(load / 449 N): one wheel of the Э.МПУ (3.5 kN)
//     breaks crusts under 5.6 cm, maybe under 14 cm. Through it the wheel settles 0.15..0.30 m (0.45 in the lake),
//     as the crew's feet. In the melt the wheel loses 70 % of its grip (only the crystal mush and the crust's edges to
//     push on) and drags through a melt of ~300 Pa s (basalt near its liquidus; estimate).
#pragma once
#include <algorithm>
#include <cmath>

namespace tvheat {

constexpr double SIG = 5.670e-8, PI_ = 3.14159265358979, TSKY = 2.7;

// the damage rate past the onset, 1/s: e-fold per 60 K, 30 min at the onset, never faster than 5 s
inline double DamageRate(double T, double onsetK) {
    return T > onsetK ? (std::min)(0.2, std::exp((T - onsetK) / 60.0) / 1800.0) : 0.0;
}

struct TyreSpec {
    double C = 4.8e4;          // J/K
    double area = 7.3, eps = 0.5, fGround = 0.5;   // radiating area m2, emissivity, the share seeing the ground
    double sunArea = 1.5, alpha = 0.5;              // m2 facing the Sun on average, its absorptance
    double hc = 150.0;         // W/m2K, the contact conductance tread-ground
    double hMelt = 300.0;      // W/m2K, in a viscous melt
    double rimArea = 1.0, rimEps = 0.3;             // the inner rim to the hub motor
    double R = 0.8, b = 0.65;  // wheel radius, width m
    double onsetK = 1150.0;    // Mo-Re spring mesh
};
struct BellySpec {
    double C = 9.0e5, area = 25.0, eps = 0.85;
    double UAin = 25.0, Tin = 295.0;   // to the bays above it
    double hMelt = 300.0;
    double onsetK = 650.0;             // CNT / phthalonitrile
};
constexpr double kMotorOnsetK = 573.0;  // hub motors: glass-polyimide windings, SmCo magnets
// the belly's stages
constexpr double kStageInverters = 0.3, kStageBattery = 0.5, kStageHarness = 0.7;
constexpr double kBatteryLoss = 0.02 / 60.0;   // of the charge per second once the battery bay is reached

// the ground under a wheel: what it is and what it can give
constexpr double kEffLava = 1860.0;   // J/m2 K s^0.5: vesicular basalt (k 1.2, rho 2400, cp 1200 - the lava field's own values)
constexpr double kEffSoil = 100.0;    // regolith, frost plains (lunar regolith ~50-80)
constexpr double kKLava = 1.2;        // W/mK, the crust (feeds a standing wheel from the melt below)

// the area a wheel of radius R and width b wets sunk z into a melt: both sides' segments and the tread's arc
inline double WetArea(double R, double b, double z) {
    z = (std::min)((std::max)(z, 0.0), 2.0 * R);
    if (z <= 0.0) return 0.0;
    const double a = std::acos((R - z) / R);
    const double seg = R * R * a - (R - z) * std::sqrt((std::max)(0.0, 2.0 * R * z - z * z));
    return 2.0 * seg + 2.0 * R * a * b;
}

// the crew's crust rule, scaled to the load: thicknesses that break for sure / that may break, m
constexpr double kCrewLoadN = 449.0;   // ~250 kg (a person in the heavy suit) on Io
inline void CrustLimits(double loadN, double& hBreak, double& hMaybe) {
    const double k = std::sqrt((std::max)(loadN, 1.0) / kCrewLoadN);
    hBreak = 0.02 * k; hMaybe = 0.05 * k;
}
// the same per-cell hash as the crew's (the site raster's 0.25 m cells), 0..1
inline double CellHash(double latDeg, double lngDeg) {
    double u = std::sin(std::floor(latDeg / 7.866e-6) * 12.9898 + std::floor(lngDeg / 7.870e-6) * 78.233) * 43758.5453;
    return u - std::floor(u);
}
inline bool CrustBreaks(int kind, double crustM, double u, double hBreak, double hMaybe) {
    if (kind == 6) return true;                                         // molten: a skylight, the lake's seams
    if (crustM < hBreak) return true;
    if (crustM < hMaybe) return u < (hMaybe - crustM) / (hMaybe - hBreak);
    return false;
}
// how deep a wheel settles into the melt (as the crew's feet): 0.15..0.30 m in a flow, 0.45 m in the lake or a skylight
inline double MeltDepth(int kind, bool lake, double u) { return kind == 6 || lake ? 0.45 : 0.15 + 0.15 * u; }

// what one wheel meets this step
struct TyreEnv {
    double groundK = 100.0;    // the ground under the patch
    double radK = 100.0;       // the ground the wheel sees (radiative, T^4 by the view factor)
    double meltK = 1450.0, melt = 0.0;   // the melt's temperature; how deep the wheel is in it, m
    double patchA = 0.2, patchL = 0.3;   // the contact patch m2, its length m
    double speed = 0.0, standT = 1e9;    // m/s along the wheel; s standing on this spot
    bool lava = false; double crustM = 0.0;
    double sunW = 0.0;         // W/m2 of sunlight on the wheel's sunlit side (0 at night)
    double motorK = 300.0;     // the hub motor behind the rim
};
// the contact conductance of the patch, W/m2K: hc in series with what the ground gives in the contact's time
inline double PatchH(const TyreSpec& s, const TyreEnv& e) {
    const double eff = e.lava ? kEffLava : kEffSoil;
    const double t = e.speed > 0.05 ? e.patchL / e.speed : (std::max)(e.standT, 0.05);
    double hg = eff / std::sqrt(PI_ * t);
    if (e.lava && e.crustM > 0.0) hg = (std::max)(hg, kKLava / (std::max)(e.crustM, 0.005));   // the melt feeds it
    return 1.0 / (1.0 / s.hc + 1.0 / hg);
}
// the tyre's heat flow (W, + in) at T and its derivative; rimW: what goes on to the motor
inline double TyreQ(const TyreSpec& s, const TyreEnv& e, double T, double& dQ, double& rimW) {
    const double T3 = T * T * T, T4 = T3 * T;
    double q = s.eps * SIG * s.area * (s.fGround * std::pow(e.radK, 4) + (1.0 - s.fGround) * std::pow(TSKY, 4) - T4) + s.alpha * s.sunArea * e.sunW;
    dQ = -4.0 * s.eps * SIG * s.area * T3;
    if (e.melt > 0.005) {
        const double hA = s.hMelt * WetArea(s.R, s.b, e.melt);
        q += hA * (e.meltK - T); dQ -= hA;
    } else {
        const double hA = PatchH(s, e) * e.patchA;
        q += hA * (e.groundK - T); dQ -= hA;
    }
    rimW = s.rimEps * SIG * s.rimArea * (T4 - std::pow(e.motorK, 4));
    q -= rimW; dQ -= 4.0 * s.rimEps * SIG * s.rimArea * T3;
    return q;
}
// one step (implicit, sub-steps of at most 2 s): the new temperature; rimW: the mean heat to the motor, W
inline double TyreStep(const TyreSpec& s, const TyreEnv& e, double T, double dt, double& rimW) {
    const int n = (int)(std::min)(600.0, std::ceil(dt / 2.0)); const double h = dt / (std::max)(n, 1);
    double rimSum = 0.0;
    for (int k = 0; k < n; ++k) {
        double dQ, r; const double q = TyreQ(s, e, T, dQ, r);
        T += h * q / (s.C - h * dQ); rimSum += r;
        T = (std::max)(T, TSKY);
    }
    rimW = n > 0 ? rimSum / n : 0.0;
    return T;
}

// the belly: it sees the ground (radK at its height), the bays above it, a melt it sits in (wetFrac of its area)
struct BellyEnv { double radK = 100.0, meltK = 1450.0, wetFrac = 0.0; };
inline double BellyStep(const BellySpec& s, const BellyEnv& e, double T, double dt) {
    const int n = (int)(std::min)(600.0, std::ceil(dt / 2.0)); const double h = dt / (std::max)(n, 1);
    for (int k = 0; k < n; ++k) {
        const double T3 = T * T * T;
        double q = s.eps * SIG * s.area * (std::pow(e.radK, 4) - T3 * T) + s.UAin * (s.Tin - T);
        double dQ = -4.0 * s.eps * SIG * s.area * T3 - s.UAin;
        if (e.wetFrac > 0.0) { const double hA = s.hMelt * s.area * e.wetFrac; q += hA * (e.meltK - T); dQ -= hA; }
        T += h * q / (s.C - h * dQ);
        T = (std::max)(T, TSKY);
    }
    return T;
}

// the radiators of a vehicle: half of their view the ground (radK), half the sky -> the temperature they reject to
inline double RadiatorSinkK(double groundRadK) { return std::pow(0.5 * std::pow(groundRadK, 4) + 0.5 * std::pow(TSKY, 4), 0.25); }

// the melt's drag on a sunk wheel, N (along its motion): a viscous melt sheared over the wetted area across ~0.1 m
constexpr double kMeltVisc = 300.0, kMeltGrip = 0.3;
inline double MeltDrag(double R, double b, double z, double v) {
    const double F = kMeltVisc * std::fabs(v) * WetArea(R, b, z) / 0.1;
    return (std::min)(F, 4.0e4);
}

}  // namespace tvheat
