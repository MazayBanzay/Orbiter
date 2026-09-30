// Offline check of core/Carriage: erection 0 -> 6 -> 0, touchdown triangle orientation and
// smoothness, trunnion height and pitch at key phases. Build: tests\run_carriage_test.bat
#include <cmath>
#include <cstdio>

#include "../core/Carriage.h"

using namespace tantra;

static Vec3 Sub(Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 Cross(Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static double Len(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

int main() {
    Carriage c;
    CarriageGeometry g;
    g.legRestX = 17.0;
    g.legRestS = 5.0;
    const double a[4][2] = {{0.694, 0.720}, {-0.694, 0.720}, {-0.817, -0.577}, {0.817, -0.577}};  // stern corners
    for (int i = 0; i < 4; ++i) g.standFoot[i] = {a[i][0] * g.standR, a[i][1] * g.standR, 0.0};
    c.SetGeometry(g);
    const double sCG = 38.0, dt = 0.02;
    c.Update(0.0, sCG);
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++fails; }
    };
    check(c.CommandErect(true, true), "erect command accepted");
    Vec3 prev[3];
    for (int i = 0; i < 3; ++i) prev[i] = c.Pose().touch[i];
    double maxStep = 0.0;
    double t = 0.0;
    int lastPhase = -1;
    for (int dir = 0; dir < 2; ++dir) {
        if (dir == 1) check(c.CommandErect(false, true), "lower command accepted");
        for (int k = 0; k < 200000 && c.Busy(); ++k) {
            c.Update(dt, sCG);
            t += dt;
            const CarriagePose& p = c.Pose();
            // "up" of the touchdown triangle must point away from the ground: world up in ship frame.
            const Vec3 up = {0, std::cos(p.theta), std::sin(p.theta)};
            const Vec3 n = Cross(Sub(p.touch[2], p.touch[0]), Sub(p.touch[1], p.touch[0]));
            const double dotUp = (n.x * up.x + n.y * up.y + n.z * up.z) / Len(n);
            if (dotUp < 0.99) { std::printf("t=%.1f P=%.3f up.n=%.3f\n", t, c.Progress(), dotUp); check(false, "triangle up"); break; }
            // CG height above the contact plane equals the trunnion height.
            const double h = -(p.touch[0].x * n.x + p.touch[0].y * n.y + p.touch[0].z * n.z) / Len(n);
            if (std::fabs(h - p.trunnionH) > 0.05) { std::printf("t=%.1f P=%.3f h=%.2f trunnion=%.2f\n", t, c.Progress(), h, p.trunnionH); check(false, "CG height"); break; }
            // Every contact of the set lies in the plane of the first three (one ground).
            for (int i = 3; i < p.nTouch; ++i) {
                const double hi = -(p.touch[i].x * n.x + p.touch[i].y * n.y + p.touch[i].z * n.z) / Len(n);
                if (std::fabs(hi - h) > 0.02) { std::printf("t=%.1f P=%.3f contact %d off plane %.3f\n", t, c.Progress(), i, hi - h); check(false, "coplanar contacts"); break; }
            }
            // Orbiter cares about the contact plane (height and tilt of the ship), not the points
            // sliding inside it: compare plane distance and normal with the previous step.
            const Vec3 pn = Cross(Sub(prev[2], prev[0]), Sub(prev[1], prev[0]));
            const double ph = -(prev[0].x * pn.x + prev[0].y * pn.y + prev[0].z * pn.z) / Len(pn);
            const Vec3 d = {n.x / Len(n) - pn.x / Len(pn), n.y / Len(n) - pn.y / Len(pn), n.z / Len(n) - pn.z / Len(pn)};
            maxStep = std::fmax(maxStep, std::fabs(h - ph) + Len(d) * 70.0);  // tilt as motion of the nose tip
            for (int i = 0; i < 3; ++i) prev[i] = p.touch[i];
            if (c.Phase() != lastPhase) {
                lastPhase = c.Phase();
                std::printf("t=%6.1f s  phase %d  P=%.2f  pitch %5.1f  trunnion %5.1f m  mast %5.1f m  legs rest %.2f stand %.2f  columns %.2f\n",
                            t, lastPhase, c.Progress(), p.theta * 57.2958, p.trunnionH, p.mastLen, p.legRest, p.legStand, p.columnShare);
            }
        }
    }
    std::printf("max contact-plane change %.3f m per %.2f s step (continuous motion; jump if > 0.25 m)\n", maxStep, dt);
    check(maxStep < 0.25, "no jumps (turning moves the nose ~8 m/s; a jump would be metres)");
    check(c.Progress() == 0.0, "back to level");
    // Took off at the loading height: stowing in flight lowers the carriage first, then the gear.
    {
        Carriage a;
        a.Load(Carriage::kLoadP, Carriage::kLoadP, 1.0, 1.0, 0, 0);
        check(a.CommandGear(false, false), "airborne stow accepted at the loading height");
        for (int k = 0; k < 4000; ++k) a.Update(0.05, 38.0);
        check(a.Gear() == 0.0 && a.Progress() == 0.0, "airborne stow from the loading height ends stowed");
        std::printf("airborne stow: gear %.2f  P %.2f\n", a.Gear(), a.Progress());
    }
    std::printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
