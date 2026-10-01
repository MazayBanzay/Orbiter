// Offline checks of core/Legs: bearing capacity, strut limits, leg load distribution, soles, regeneration.
#include "../core/Legs.h"

#include <cmath>
#include <cstdio>

using namespace tantra::legs;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++fails;
}

int main() {
    const double g = 9.80665, W = 44322e3 * g;  // loaded T8
    const double hip = BearingCapacity(kHipBearingArea);
    std::printf("     hip bearing %.0f MN, loaded 1 g %.0f MN (%.2f)\n", hip / 1e6, W / 2e6, W / 2 / hip);
    check(W / 2 < hip && 1.7 * W / 2 > hip, "hip levitates at 1 g loaded, lands on the catcher at 1.7 g");
    const TouchdownLimits lc = Limits(kStrokeCarriage), ls = Limits(kStrokeStern);
    std::printf("     touchdown level %.1f / %.1f, tail %.1f / %.1f m/s\n", lc.vSoft, lc.vBreak, ls.vSoft, ls.vBreak);
    check(std::fabs(lc.vSoft - 6.64) < 0.05 && std::fabs(ls.vBreak - 7.67) < 0.05, "strut limits");

    LegLoadInput in;
    in.weight = W;
    in.columnShare = 1.0;
    in.h = 70.0;
    in.windSide = 5e6;
    double n[6];
    LegLoads(in, n);
    check(std::fabs(n[0] + n[1] - W) < 1.0 && n[1] > n[0], "carriage legs: sum = weight, wind loads the lee leg");
    in.columnShare = 0.0;
    const double r = 33.0, c = std::cos(0.5236), s = std::sin(0.5236);
    const double fx[4] = {r * c, -r * c, -r * c, r * c}, fy[4] = {r * s, r * s, -r * s, -r * s};
    for (int i = 0; i < 4; ++i) { in.feetX[i] = fx[i]; in.feetY[i] = fy[i]; }
    in.cosSplay = 1.0;
    in.windX = 4e6;
    in.windY = 0.0;
    LegLoads(in, n);
    double sum = 0.0, mx = 0.0;
    for (int i = 0; i < 4; ++i) { sum += n[2 + i]; mx += n[2 + i] * fx[i]; }
    check(std::fabs(sum - W) < 1.0 && std::fabs(mx - 4e6 * 70.0) < 1e3 && n[2] > n[3], "stern legs: sum and moment");

    Soles so;
    so.Update(0.1, true, true);
    const double mu0 = so.Mu();
    for (int i = 0; i < 100; ++i) so.Update(0.1, true, true);
    check(mu0 < 0.55 && std::fabs(so.Mu() - kMuAnchored) < 1e-9, "sole: conform 0.5 -> anchored 0.9");
    so.Update(0.1, true, false);
    so.Update(1.0, true, false);
    check(std::fabs(so.Mu() - kMuJammed) < 1e-9, "sole: anchors in before lift-off, still locked");

    Regen re;
    re.Update(0.1, W, 70.0, true);
    for (int i = 1; i <= 560; ++i) re.Update(0.1, W, 70.0 - 0.1 * i, true);
    std::printf("     lowering 56 m returns %.1f GJ\n", re.Returned() / 1e9);
    check(std::fabs(re.Returned() - 0.9 * W * 56.0) < 1e6, "regeneration 90 %");
    std::printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
