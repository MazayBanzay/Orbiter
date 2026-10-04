// Offline checks of core/Impact against the mockup (Tantra_Design/tantra_damage.html).
#include "../core/Impact.h"

#include <cstdio>

using namespace tantra::impact;

static int fails = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++fails; }
}

static Result hit(Mode mode, double mass, double v, double soil) {
    Input in;
    in.mode = mode; in.mass = mass; in.v = v; in.soil = soil;
    const Result r = Solve(in);
    std::printf("mode %d  m %.1f kt  v %5.0f m/s  soil %4.1f MPa | peak %5.1f g  %6.0f ms  pen %6.1f m  belly %.2f  crushed",
                (int)mode, mass / 1e6, v, soil / 1e6, r.peakG, r.duration * 1e3, r.penetration, r.belly);
    for (int z = 0; z < kZoneCount; ++z) std::printf(" %.1f", r.crushed[z]);
    std::printf("\n");
    return r;
}

int main() {
    const double light = 15.0e6, full = 52.34e6;
    // a belly landing on the bare hull, sand 15 m/s: ~16 g (mockup)
    Result r = hit(kBelly, light, 15.0, 1.5e6);
    check(r.peakG > 10.0 && r.peakG < 25.0, "belly on sand at 15 m/s: 10..25 g");
    // the same on rock at 20 m/s: the belly is crushed, ~37 g
    r = hit(kBelly, light, 20.0, 60.0e6);
    check(r.belly > 0.5 && r.belly < 3.0, "belly on rock at 20 m/s: the belly crushed 0.5..3 m");
    check(r.peakG > 25.0 && r.peakG < 60.0, "belly on rock at 20 m/s: 25..60 g");
    // nose-first into rock at 100 m/s: the shield takes it (crushed, not through)
    r = hit(kNoseFirst, light, 100.0, 60.0e6);
    check(Grade(Share(r, kNose)) >= 1 && Share(r, kLiving) < 0.05, "nose into rock at 100 m/s: the shield crushed, the living module intact");
    // full traps, nose-first at 400 m/s: the hull before the cassettes is gone, the cassettes hold (rigid core)
    r = hit(kNoseFirst, full, 400.0, 60.0e6);
    check(Grade(Share(r, kLiving)) == 3, "nose at 400 m/s: the living module destroyed");
    check(Share(r, kTraps) < 0.1 && r.peakG < 190.0, "nose at 400 m/s: the trap cassettes hold (under 190 g)");
    // soft ground: the ship goes in rather than crushing
    r = hit(kNoseFirst, light, 60.0, 1.5e6);
    check(r.penetration > 5.0 && Share(r, kNose) < 0.05, "nose into sand at 60 m/s: goes into the ground");
    // equipment: across the reactor holds 38 g, along the axis 200
    check(EquipState(kVeu, 37.0, false, 0.0) == 1, "reactor at 37 g across: at its limit");
    check(EquipState(kVeu, 50.0, false, 0.0) == 2, "reactor at 50 g across: damaged");
    check(EquipState(kVeu, 100.0, true, 0.0) == 0, "reactor at 100 g along the axis: intact");
    check(EquipState(kGyros, 16.0, false, 0.0) == 3, "gyroscopes at 16 g across: torn off");
    check(EquipState(kBridge, 5.0, false, 0.9) == 3, "a crushed compartment takes its equipment");
    {   // the hull's break lines
        namespace im = tantra::impact;
        double sh[5] = {0, 0, 0, 0, 0};
        im::Breakup b = im::BreakLines(im::kBelly, sh, 0.5, 10.0);       // a weak blow: the default cut at the hangar
        check(!b.cut[0] && b.cut[1] && b.cut[2] && !b.cut[3], "weak blow: the default cuts at the hangar");
        b = im::BreakLines(im::kBelly, sh, 3.0, 28.0);                    // belly through, 28 g: hangar joints + none else
        check(b.cut[1] && b.cut[2] && !b.cut[0] && !b.cut[3], "belly through at 28 g: the hangar joints only");
        double sn[5] = {0, 0, 0, 0.2, 0.9};
        b = im::BreakLines(im::kNoseFirst, sn, 0.0, 20.0);                // nose crushed through: it parts, shortened
        check(b.cut[3] && b.crush[4] == 2 && !b.cut[0] && !b.cut[1] && !b.cut[2], "nose crushed through: parts, shortened");
        b = im::BreakLines(im::kBelly, sh, 1.0, 70.0);                     // 70 g: every joint
        check(b.cut[0] && b.cut[1] && b.cut[2] && b.cut[3], "70 g: every joint");
    }
    std::printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
