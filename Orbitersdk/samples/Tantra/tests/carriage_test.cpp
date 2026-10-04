// Offline check of core/Carriage (T9): erection 0 -> 6 -> 0 at several CG stations, touchdown plane orientation,
// coplanar contacts, continuity, tripod load shares, kangaroo IK reach. Build: tests\run_carriage_test.bat
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "../core/Carriage.h"
#include "../core/Spec.h"
#include MESH_LAYOUT_H

using namespace tantra;

static Vec3 Sub(Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 Cross(Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static double Len(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

static CarriageGeometry Geometry() {
    CarriageGeometry g;
    namespace m = tantra::mesh;      // the same numbers the adapter takes from MeshLayout.h / Spec.h
    g.restAxisH = m::kAxisH;
    g.standClear = -m::kStandGroundS;
    g.columnX = m::kHipXOut;
    g.footR = m::kFootR;
    g.footH = m::kFootH;
    g.legMin = m::kLegLMin;
    g.legMax = m::kLegLMax;
    g.trackS0 = m::kCarS0;
    g.trackS1 = m::kCarS1;
    g.stowS = m::kStowS;
    g.standR = m::kStandR;
    g.sternFootR = m::kLegFootR;
    for (int i = 0; i < 4; ++i) g.standFoot[i] = {m::kLegs[i].radial.x * m::kStandR, m::kLegs[i].radial.y * m::kStandR, 0.0};
    g.kangHipS = m::kKangHip.z + tantra::spec::kOriginS;
    g.kangHipY = m::kKangHip.y;
    g.kangThigh = m::kKangThigh;
    g.kangShinMin = m::kKangShinMin;
    g.kangShinMax = m::kKangShinMax;
    g.kangKneeE = m::kKangKneeE;
    g.kangFootFwd = m::kKangFootFwd;
    g.kangFootR = m::kKangFootR;
    g.kangHipMaxDeg = m::kKangHipMax * 57.2958;
    return g;
}

// Erect and lay the ship at one CG station; returns the number of failures.
int RunErect(double sCG) {
    std::printf("--- CG at s %.1f ---\n", sCG);
    Carriage c;
    const CarriageGeometry g = Geometry();
    c.SetGeometry(g);
    const double dt = 0.02;
    c.Update(0.0, sCG);
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++fails; }
    };
    // lying: tripod, trunnions parked, the kangaroo share by the lever arms
    {
        const CarriagePose& p = c.Pose();
        check(p.tripod && p.nTouch == 6, "lying on blades + kangaroo (6 contacts)");
        check(std::fabs(p.hipS - g.trackS0) < 1e-6, "trunnions parked at the track start while lying");
        const double want = (sCG - g.trackS0) / (g.kangHipS + g.kangFootFwd - g.trackS0);
        std::printf("lying: kangaroo share %.3f (lever arms %.3f), blade hip->ankle %.1f m, knee %.2f ext %.2f\n", p.kangShare, want, p.mastLen, p.kangKnee, p.kangExt);
        check(std::fabs(p.kangShare - want) < 1e-6, "kangaroo share = (sCG - sTrack) / (sFoot - sTrack)");
        check(p.kangExt >= 0.0 && p.kangExt <= 1.0 && p.kangKnee > 0.5 && p.kangKnee < 1.0, "kangaroo IK within the shin range, knee bent aft");
    }
    check(c.CommandErect(true, true), "erect command accepted");
    Vec3 prev[3];
    for (int i = 0; i < 3; ++i) prev[i] = c.Pose().touch[i];
    double maxStep = 0.0, t = 0.0, maxExt = 0.0, maxMast = 0.0;
    int lastPhase = -1;
    bool sawTurn = false;
    for (int dir = 0; dir < 2; ++dir) {
        if (dir == 1) check(c.CommandErect(false, true), "lower command accepted");
        for (int k = 0; k < 200000 && c.Busy(); ++k) {
            c.Update(dt, sCG);
            t += dt;
            const CarriagePose& p = c.Pose();
            maxExt = std::fmax(maxExt, p.kangExt);
            maxMast = std::fmax(maxMast, p.mastLen);
            // "up" of the touchdown triangle must point away from the ground: world up in ship frame.
            const Vec3 up = {0, std::cos(p.theta), std::sin(p.theta)};
            const Vec3 n = Cross(Sub(p.touch[2], p.touch[0]), Sub(p.touch[1], p.touch[0]));
            const double dotUp = (n.x * up.x + n.y * up.y + n.z * up.z) / Len(n);
            if (dotUp < 0.99) { std::printf("t=%.1f P=%.3f up.n=%.3f\n", t, c.Progress(), dotUp); check(false, "triangle up"); break; }
            // CG height above the contact plane equals the trunnion height.
            const double h = -(p.touch[0].x * n.x + p.touch[0].y * n.y + p.touch[0].z * n.z) / Len(n);
            if (std::fabs(h - p.trunnionH) > 0.05) { std::printf("t=%.1f P=%.3f h=%.2f trunnion=%.2f\n", t, c.Progress(), h, p.trunnionH); check(false, "CG height"); break; }
            bool off = false;
            for (int i = 3; i < p.nTouch; ++i) {
                const double hi = -(p.touch[i].x * n.x + p.touch[i].y * n.y + p.touch[i].z * n.z) / Len(n);
                if (std::fabs(hi - h) > 0.02) { std::printf("t=%.1f P=%.3f contact %d off plane %.3f\n", t, c.Progress(), i, hi - h); check(false, "coplanar contacts"); off = true; break; }
            }
            if (off) break;
            const Vec3 pn = Cross(Sub(prev[2], prev[0]), Sub(prev[1], prev[0]));
            const double ph = -(prev[0].x * pn.x + prev[0].y * pn.y + prev[0].z * pn.z) / Len(pn);
            const Vec3 d = {n.x / Len(n) - pn.x / Len(pn), n.y / Len(n) - pn.y / Len(pn), n.z / Len(n) - pn.z / Len(pn)};
            maxStep = std::fmax(maxStep, std::fabs(h - ph) + Len(d) * 100.0);
            for (int i = 0; i < 3; ++i) prev[i] = p.touch[i];
            // on the turn the trunnions sit under the CG and the kangaroo is home
            if (c.Progress() > 2.0 && c.Progress() < 3.0) {
                sawTurn = true;
                if (p.kangShare != 0.0 || p.kangDoor > 1e-6 || !p.onColumns) { check(false, "turn: blades alone, kangaroo home"); break; }
                // CG tracking: between the computed CG (off by cgError) and the CG the drives find (to cgSensor)
                const double lo = std::min(g.trackS1, std::max(g.trackS0, sCG - g.cgError));
                const double hi = std::min(g.trackS1, std::max(g.trackS0, sCG - g.cgSensor));
                if (p.hipS < lo - 1e-6 || p.hipS > hi + 1e-6) { check(false, "turn: trunnions under the CG"); break; }
                if (c.Progress() > 2.9 && c.CgResidual() > 0.05) { check(false, "turn: CG tracking nulls the drive moment"); break; }
            }
            if (c.Phase() != lastPhase) {
                lastPhase = c.Phase();
                std::printf("t=%6.1f s  phase %d  P=%.2f  pitch %5.1f  CG %5.1f m  hip s %5.1f  blade %5.1f m  kang share %.2f  stern %.2f  columns %.2f\n",
                            t, lastPhase, c.Progress(), p.theta * 57.2958, p.trunnionH, p.hipS, p.mastLen, p.kangShare, p.legStand, p.columnShare);
            }
        }
    }
    std::printf("max contact-plane change %.3f m per %.2f s step; kangaroo ext max %.9f; blade max %.1f m (limit %.1f)\n",
                maxStep, dt, maxExt, maxMast, g.legMax);
    check(maxStep < 0.25, "no jumps (turning moves the nose ~8 m/s; a jump would be metres)");
    check(maxExt <= 1.0 + 1e-6, "kangaroo shin reaches the top of the lift");   // sized to the top exactly: float noise only
    check(maxMast <= g.legMax + 1e-6, "blades within their extension");
    check(sawTurn, "turn phase seen");
    check(c.Progress() == 0.0, "back to lying");
    return fails;
}

int main() {
    int fails = 0;
    // T9 states (mass_t9): loaded 58.1, landing 58.6 / 64.7, empty 64.1; the track covers 53.4..71.6
    for (double sCG : {58.1, 60.0, 64.7, 67.8}) fails += RunErect(sCG);
    {   // airborne stow half way through the erection: finishes to the nearer end, then stows
        Carriage a;
        a.SetGeometry(Geometry());
        a.Load(3.6, 6.0, 1.0, 1.0, 0, 0);
        bool ok = a.CommandGear(false, false);
        for (int k = 0; k < 6000; ++k) a.Update(0.05, 58.1);
        ok = ok && a.Gear() == 0.0 && a.Progress() == 6.0 && a.Set() == Carriage::FlightSet::Standing;
        std::printf("airborne stow from P 3.6: gear %.2f  P %.2f  set %d\n", a.Gear(), a.Progress(), (int)a.Set());
        if (!ok) { std::printf("FAIL: airborne stow\n"); ++fails; }
    }
    std::printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
