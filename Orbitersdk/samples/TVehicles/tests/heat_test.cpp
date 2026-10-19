// heat_test - TVHeat.h's numbers for the README: how long a vehicle lasts on the Io lava field and on hot ground.
// Build: tests\run_heat_test.bat (reads Config\Io\LavaField\LavaField.cfg for the real crust temperatures).
#include "../TVHeat.h"
#include "../LavaThermal.h"
#include <cstdio>
#include <cstring>

using namespace tvheat;

// the hub motor as TVChassis::MotorThermal has it in vacuum (one of eight): 3e5/8 J/K, 60/8 W/K to the structure,
// 20/8 m2 of radiators at 0.85; its losses at a steady cruise
struct Motor { double T = 300.0, dmg = 0.0; bool trip = false; };
static void MotorStep(Motor& m, double sinkK, double lossW, double rimW, double dt) {
    const double Ci = 3.0e5 / 8, G = 60.0 / 8, eA = 0.85 * SIG * 20.0 / 8;
    const int n = (int)std::ceil(dt / 2.0); const double h = dt / n;
    for (int k = 0; k < n; ++k) {
        const double T = m.T, Q = G * (T - sinkK) + eA * (std::pow(T, 4) - std::pow(sinkK, 4)), dQ = G + 4 * eA * T * T * T;
        m.T += h * ((m.trip ? 0.0 : lossW) + rimW - Q) / Ci / (1.0 + h * dQ / Ci);
    }
    if (m.T > 473.0) m.trip = true;
    if (m.T < 420.0) m.trip = false;
    m.dmg = (std::min)(1.0, m.dmg + DamageRate(m.T, kMotorOnsetK) * dt);
}

struct Case {
    const char* name; double groundK, radWheelK, radBellyK, radSinkK, crustM; bool lava, breaks; double meltDepth, speed, tEnd;
};

static void Run(const Case& c, double loadN) {
    TyreSpec ts; BellySpec bs;
    // start: settled on the frost plain at night (Io: ground 90 K)
    double Tt = 90.0, Tb = 200.0; Motor mo;
    { TyreEnv e; e.groundK = 90; e.radK = 90; double r;
      for (int k = 0; k < 200; ++k) { Tt = TyreStep(ts, e, Tt, 3600, r); BellyEnv be; be.radK = 90; Tb = BellyStep(bs, be, Tb, 3600); MotorStep(mo, RadiatorSinkK(90), 0, r, 3600); } }
    std::printf("\n== %s (wheel load %.0f N)\n   start: tyre %.0f K, belly %.0f K, motor %.0f K\n", c.name, loadN, Tt, Tb, mo.T);
    double dT = 0, dB = 0; int stage = 0; bool tOn = false, tDead = false, mTrip = false, mOn = false, mDead = false, bOn = false;
    const double dt = 1.0;
    double standT = 0.0;
    for (double t = 0; t <= c.tEnd; t += dt) {
        TyreEnv e; e.groundK = c.groundK; e.radK = c.radWheelK; e.lava = c.lava; e.crustM = c.crustM; e.speed = c.speed;
        e.standT = standT; standT += dt; e.melt = c.breaks ? c.meltDepth * (1 - std::exp(-t / 2.0)) : 0.0; e.motorK = mo.T;
        e.patchA = 0.65 * 0.35; e.patchL = 0.35;
        double rim; Tt = TyreStep(ts, e, Tt, dt, rim);
        dT = (std::min)(1.0, dT + DamageRate(Tt, ts.onsetK) * dt);
        BellyEnv be; be.radK = c.radBellyK; Tb = BellyStep(bs, be, Tb, dt);
        dB = (std::min)(1.0, dB + DamageRate(Tb, bs.onsetK) * dt);
        const double loss = c.speed > 0.1 ? 330.0 : 15.0;
        MotorStep(mo, RadiatorSinkK(c.radSinkK), loss, rim, dt);
        auto at = [&](const char* what) { std::printf("   %7.0f s (%5.1f min): %s   [tyre %.0f K, belly %.0f K, motor %.0f K]\n", t, t / 60, what, Tt, Tb, mo.T); };
        if (!tOn && Tt > ts.onsetK) { tOn = true; at("tyre past its onset (1150 K)"); }
        if (!tDead && dT >= 0.85) { tDead = true; at("tyre wrecked (85 %: locked, drags)"); }
        if (!mTrip && mo.trip) { mTrip = true; at("hub motor tripped on heat (473 K)"); }
        if (!mOn && mo.T > kMotorOnsetK) { mOn = true; at("hub motor past its onset (573 K)"); }
        if (!mDead && mo.dmg >= 0.5) { mDead = true; at("hub motor burnt (50 %: dead)"); }
        if (!bOn && Tb > bs.onsetK) { bOn = true; at("belly past its onset (650 K)"); }
        if (stage < 1 && dB >= kStageInverters) { stage = 1; at("belly 30 %: inverters lost (drive halved)"); }
        if (stage < 2 && dB >= kStageBattery) { stage = 2; at("belly 50 %: battery bay - cells shorting"); }
        if (stage < 3 && dB >= kStageHarness) { stage = 3; at("belly 70 %: traction harness - no drive"); }
        if (stage < 4 && dB >= 1.0) { stage = 4; at("belly burnt through - cabin floor holed"); }
    }
    std::printf("   after %.0f min: tyre %.0f K (%.0f %%), belly %.0f K (%.0f %%), motor %.0f K (%.0f %%)%s\n", c.tEnd / 60, Tt, dT * 100, Tb, dB * 100, mo.T, mo.dmg * 100, mo.trip ? " TRIPPED" : "");
}

int main(int argc, char** argv) {
    const char* cfg = argc > 1 ? argv[1] : "Config\\Io\\LavaField\\LavaField.cfg";
    lava::Field f;
    if (!f.Load(cfg)) { std::printf("no lava field at %s\n", cfg); return 1; }
    // a 2 cm crust (18..22 mm) in the site raster, not molten: its contact and radiative temperatures as the field has them
    const double lat0 = -1.785279, lng0 = -153.616336;
    double bl = 0, bg = 0, best = 1e9; lava::Sample bs;
    for (int i = -600; i <= 600; i += 2) for (int j = -600; j <= 600; j += 2) {
        lava::Sample s; const double la = lat0 + i * 7.866e-6, ln = lng0 + j * 7.87e-6;
        if (!f.At(la, ln, s) || !s.lava || s.kind == 6 || s.crustM < 0.018 || s.crustM > 0.022) continue;
        const double d = std::hypot((double)i, (double)j); if (d < best) { best = d; bl = la; bg = ln; bs = s; }
    }
    if (best > 1e8) { std::printf("no 2 cm crust found\n"); return 1; }
    const double rW = f.GroundRadiativeK(bl, bg, 0.8, 95), rB = f.GroundRadiativeK(bl, bg, 0.51, 95), rS = f.GroundRadiativeK(bl, bg, 1.0, 95);
    std::printf("2 cm crust at %.6f %.6f: crust %.1f mm, kind %d, contact %.0f K, radiative %.0f K (0.8 m) %.0f K (0.51 m) %.0f K (1 m)\n",
                bl, bg, bs.crustM * 1e3, bs.kind, bs.contactK, rW, rB, rS);
    double hB, hM;
    for (double load : {3500.0, 2020.0}) { CrustLimits(load, hB, hM); std::printf("load %.0f N: breaks under %.1f cm, maybe under %.1f cm\n", load, hB * 100, hM * 100); }
    const double tEnd = 4 * 3600;
    // the Э.МПУ (15.6 t with fuel, 3.5 kN a wheel on Io) parked on it: the crust breaks, the wheels settle 0.22 m in a 1450 K melt
    Run({"EMPU parked on a 2 cm crust over a 1450 K melt", bs.contactK, rW, rB, rS, bs.crustM, true, true, 0.22, 0.0, tEnd}, 3500);
    // and if it held (a crust the wheel does not break: what the crust alone does)
    Run({"the same crust if it held (no melt contact)", bs.contactK, rW, rB, rS, bs.crustM, true, false, 0.0, 0.0, tEnd}, 3500);
    Run({"driving across 600 K ground at 30 km/h (a ~14 cm crust)", 600, 600, 600, 600, 0.14, true, false, 0, 8.3, tEnd}, 3500);
    Run({"parked on 600 K ground", 600, 600, 600, 600, 0.14, true, false, 0, 0.0, tEnd}, 3500);
    Run({"parked on 700 K ground (a ~6 cm crust, ~2 h old)", 700, 700, 700, 700, 0.06, true, false, 0, 0.0, tEnd}, 3500);
    Run({"driving over 1050 K crust (4 mm, a fresh toe's margin) - if it held", 1055, 1055, 1055, 1055, 0.004, true, false, 0, 8.3, 1800}, 3500);
    return 0;
}
