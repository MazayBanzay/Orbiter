// TantraEntryForecast: the entry forecast of the thermal page (ТЕПЛО) as plain C++ - no Orbiter calls, no drawing - so that the
// autopilot СХОД (TantraReentry) and the offline tests run the very same forecast. Split out of TantraThermalScreen (which includes
// this header: its types and RunForecast() keep their names in tantra::thermalscreen). Header-only (inline): the ship's build
// needs no new source file.
// The forecast is the mockup's predict() with the game's models in place of its stand-ins: the point-mass entry at the held
// attitude with core/Aero's coefficients (the airfoils of Tantra::DefineAerodynamics), the skin by a copy of the ship's own
// core/Damage model stepped forward; a held thrust against the airspeed (Path::thrustAcc) brakes it - 0 is the old forecast.
#pragma once
#include "../core/Aero.h"
#include "../core/Damage.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tantra::thermalscreen {

constexpr int kZones = tantra::damage::kZoneCount;   // nose, belly, wing edges, fin, stern, gear, pods

// the atmosphere of the forecast and the corridor: Orbiter's own density by altitude when the ship fills the table
// (oapiGetPlanetAtmParams at its longitude and latitude, every 2 km), else the mockup's Earth, 1.225 e^(-h / 8500) - nothing
// above 200 km either way; between the table's points log-linear, above its top extrapolated with its top scale height
struct Air {
    static constexpr int kN = 66;              // 0 .. 130 km
    static constexpr double kStep = 2000.0;    // [m]
    double rho[kN] = {};                       // [kg/m^3]; rho[0] == 0: no table
    double Rho(double h) const;                // the density at an altitude [kg/m^3]
    double Alt(double rho) const;              // the altitude of a density [m] (< 0: denser than at the ground)
};
struct Body { double R = 6371e3, g0 = 9.81; };   // the planet: mean radius [m], surface gravity [m/s^2] (the mockup's Earth)

// the ship on its path, as the forecast and the corridor need it
struct Path {
    double h = 0.0, v = 0.0, gamma = 0.0;      // altitude [m], airspeed [m/s], flight path angle of the airspeed [rad] (< 0 down)
    double mass = 0.0;                         // [kg]
    double aoa = 0.0, bank = 0.0;              // [rad]: the forecast holds them (the pilot or the autopilot keeps the attitude)
    double crestAvail = 1.0;                   // the wings in the flow: Tantra::aeroCrest_ (0 folded .. 1 out at 90 deg)
    double gearArea = 0.0;                     // the gear and the pods in the flow: Tantra::aeroGearArea_ [m^2]
    tantra::damage::Exposure expo;             // what the hull presents to the flow - as Tantra::UpdateDamage builds it
    Air air;
    Body body;
    double thrustAcc = 0.0;                    // a thrust held against the airspeed [m/s^2] (the autopilot СХОД's candidates); 0: none
};

// the forecast: the ship's state and a copy of its zone model run forward to the end of the entry (the mockup's predict())
struct ForecastIn {
    Path path;
    tantra::damage::Model skin;                // a copy of Tantra::damage_ (its temperatures now); stepped with damage off
    double ambientT = 250.0;                   // the air's temperature for the radiative equilibrium [K] (the mockup's)
};
struct FcPt { double x, y; };                  // a point of the forecast's track
struct Forecast {
    bool valid = false;
    double peakT[kZones] = {}, peakAt[kZones] = {};   // each zone's highest temperature ahead [K] and in how long [s] (0: now)
    double nMax = 0.0, nMaxAt = 0.0;           // the load's peak (aerodynamic and the held thrust) [g] and in how long [s]
    double qMax = 0.0, qMaxAt = 0.0;           // the nose's heat flux peak [W/m^2] and in how long [s]
    double endH = 0.0, endV = 0.0, endT = 0.0; // where the entry ends (slower than 600 m/s or below 12 km) and in how long [s]
    bool skip = false;                         // it ends climbing out above 125 km: a skip
    std::vector<FcPt> track;                   // every 4 s: x the speed [m/s], y the altitude [m]
};
// up to 6000 steps of 1 s (to the end of any entry): 1 .. 8 ms; pure - no Orbiter calls
Forecast RunForecast(const ForecastIn& in);

// lift and drag areas (coefficient x reference area) at an angle of attack: the body on its planform, the wings, the gear and the
// pods - what the ship's airfoils give in the pitch plane [m^2]
void AeroAreas(double aoa, double mach, double crest, double gearArea, double* SL, double* SD);

// ---- the definitions (inline: the header is the whole module - no source file to add to the build) ----

namespace fcdetail {
constexpr double kG0 = 9.80665;                   // g, the unit of the load
constexpr double kSound = 300.0;                  // the speed of sound of the forecast's Mach number (the mockup's) [m/s]
constexpr double kTop = 125e3;                    // the corridor's top: climbing out above it the entry is a skip [m]
constexpr double kEndV = 600.0, kEndH = 12e3;     // the entry is over slower than this [m/s] or below this [m]
// the forecast's step and reach: the mockup's 0.5 s x 4000 (2000 s) ended its own entries, the game's lift (L/D ~1.1 against the
// mockup's ~0.5) glides 1100 .. 5100 s from 120 km with the heat peaks up to 3500 s in: 1 s x 6000 - the same peaks (within 2 K
// and 1 s of the half-second step: the zones' lags are 10 s and more) to the end of any entry, 1 .. 8 ms a run
constexpr double kDt = 1.0;                       // [s]
constexpr int kSteps = 6000;                      // every 4th step on its track (the mockup's every 4 s)
// the airfoils of Tantra::DefineAerodynamics beyond the body: the wings 2 x 240 m^2 at aspect 1.5 (scaled by aeroCrest_), the
// gear and the pods drag only (Cd 1.1, x1.4 supersonic, on aeroGearArea_) - the vessel's own numbers, not exported by it
constexpr double kCrestArea = 480.0, kCrestAspect = 1.5, kGearCd = 1.1;
}  // namespace fcdetail

inline void AeroAreas(double aoa, double mach, double crest, double gearArea, double* SL, double* SD) {
    using namespace fcdetail;
    namespace ae = tantra::aero;
    double cl = 0.0, cd = 0.0;
    ae::BodyPitch(aoa, mach, &cl, &cd);
    double L = ae::kPlanform * cl, D = ae::kPlanform * cd;
    if (crest > 0.0) { ae::Plate(aoa, mach, kCrestAspect, &cl, &cd); L += kCrestArea * crest * cl; D += kCrestArea * crest * cd; }
    D += kGearCd * (mach > 1.0 ? 1.4 : 1.0) * gearArea;
    *SL = L; *SD = D;
}

// ---- the atmosphere ----
inline double Air::Rho(double h) const {
    if (h > 200e3) return 0.0;
    if (rho[0] <= 0.0) return 1.225 * std::exp(-h / 8500.0);
    if (h <= 0.0) return rho[0];
    const double u = h / kStep;
    const int i = (std::min)(kN - 2, int(u));
    const double a = rho[i], b = rho[i + 1], f = u - i;
    if (a <= 0.0 || b <= 0.0) return (std::max)(0.0, a + (b - a) * f);
    return a * std::pow(b / a, f);                                         // f > 1 above the table: its top scale height
}
inline double Air::Alt(double r) const {
    if (r <= 0.0) return 1e9;
    if (rho[0] <= 0.0) return -8500.0 * std::log(r / 1.225);
    if (r > rho[0]) return -1.0;
    for (int i = 0; i < kN - 1; ++i) {
        const double a = rho[i], b = rho[i + 1];
        if (r < b && i < kN - 2) continue;                                 // not yet between them (the top pair extrapolates)
        if (a <= 0.0) return 1e9;
        if (b <= 0.0) return (i + (a - r) / a) * kStep;                    // the planet's air ends here
        if (b >= a) return i * kStep;
        return (i + std::log(r / a) / std::log(b / a)) * kStep;
    }
    return 1e9;
}

// ---- the forecast: predict() of the mockup ----
inline Forecast RunForecast(const ForecastIn& in) {
    using namespace fcdetail;
    namespace dm = tantra::damage;
    Forecast r;
    const Path& p = in.path;
    dm::Model skin = in.skin;
    const double m = (std::max)(1.0, p.mass), R = (std::max)(1.0, p.body.R), g0 = p.body.g0, cb = std::cos(p.bank);
    const double aT = (std::max)(0.0, p.thrustAcc);
    double h = p.h, v = p.v, gam = p.gamma, t = 0.0;
    for (int z = 0; z < kZones; ++z) { r.peakT[z] = skin.Temperature(z); r.peakAt[z] = 0.0; }
    {   // the load and the nose's flux now: the peaks start from them
        double SL = 0.0, SD = 0.0;
        AeroAreas(p.aoa, v / kSound, p.crestAvail, p.gearArea, &SL, &SD);
        const double q = 0.5 * p.air.Rho(h) * v * v;
        r.nMax = std::hypot(q * SD + aT * m, q * SL) / m / kG0;
        r.qMax = skin.HeatFlux(dm::kZoneNose);
    }
    dm::Flight f;
    f.aoa = p.aoa; f.ambientT = in.ambientT;
    dm::Ground ground;
    bool done = v < kEndV || h < kEndH, skip = false;
    for (int k = 0; k < kSteps && !done; ++k) {
        // the point mass at the held attitude, the lift turned by the bank (flightStep: v, then gamma with the new v, then h)
        const double rr = R + h, gl = g0 * (R / rr) * (R / rr), q = 0.5 * p.air.Rho(h) * v * v;
        double SL = 0.0, SD = 0.0;
        AeroAreas(p.aoa, v / kSound, p.crestAvail, p.gearArea, &SL, &SD);
        const double D = q * SD, L = q * SL;
        v += (-D / m - aT - gl * std::sin(gam)) * kDt;
        const double vv = (std::max)(50.0, v);
        gam += (L * cb / (m * vv) - (gl / vv - v / rr) * std::cos(gam)) * kDt;
        h += v * std::sin(gam) * kDt;
        const double n = std::hypot(D + aT * m, L) / m / kG0;
        if (v < kEndV || h < kEndH) done = true;
        if (h > kTop && gam > 0.0) { done = true; skip = true; }
        // the skin: core/Damage's own step at the new state (damage off: nothing breaks in a forecast)
        f.rho = p.air.Rho(h); f.v = (std::max)(0.0, v); f.mach = f.v / kSound;
        skin.Step(kDt, f, p.expo, ground, false);
        t += kDt;
        for (int z = 0; z < kZones; ++z) if (skin.Temperature(z) > r.peakT[z]) { r.peakT[z] = skin.Temperature(z); r.peakAt[z] = t; }
        if (n > r.nMax) { r.nMax = n; r.nMaxAt = t; }
        const double qn = skin.HeatFlux(dm::kZoneNose);
        if (qn > r.qMax) { r.qMax = qn; r.qMaxAt = t; }
        if (k % 4 == 0) r.track.push_back({v, h});
    }
    r.endH = h; r.endV = v; r.endT = t; r.skip = skip; r.valid = true;
    return r;
}


}  // namespace tantra::thermalscreen
