// Offline checks of core/Legs (T9): bearing capacity, strut limits, leg load distribution with the kangaroo,
// soles (settling rim, sintered root), regeneration.
#include "../core/Legs.h"
#include "../core/Spec.h"

#include <cmath>
#include <cstdio>

using namespace tantra::legs;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++fails;
}

int main() {
    const double g = 9.80665, W = 52.3e6 * g;  // loaded T9 at launch
    const double hip = BearingCapacity(kHipBearingArea);
    const double perBlade = W * (1.0 - 0.085) / 2.0;
    std::printf("     hip bearing %.0f MN, loaded 1 g per blade %.0f MN (%.2f)\n", hip / 1e6, perBlade / 1e6, perBlade / hip);
    check(perBlade < hip && 1.7 * perBlade > hip, "hip levitates at 1 g loaded, lands on the catcher at 1.7 g");
    const TouchdownLimits lc = Limits(kStrokeCarriage), ls = Limits(kStrokeStern);
    std::printf("     touchdown lying %.1f / %.1f, tail %.1f / %.1f m/s\n", lc.vSoft, lc.vBreak, ls.vSoft, ls.vBreak);
    check(std::fabs(lc.vSoft - 6.64) < 0.05 && std::fabs(ls.vBreak - 7.67) < 0.05, "strut limits");

    LegLoadInput in;
    in.weight = W;
    in.columnShare = 1.0;
    in.kangShare = 0.085;
    in.h = 31.7;
    in.windSide = 5e6;
    double n[kLegCount];
    LegLoads(in, n);
    check(std::fabs(n[0] + n[1] + n[kKangaroo] - W) < 1.0 && n[1] > n[0] && std::fabs(n[kKangaroo] - 0.085 * W) < 1.0,
          "tripod: blades + kangaroo = weight, kangaroo 8.5 %, wind loads the lee blade");
    in.kangShare = 0.0;
    in.h = 80.6;
    LegLoads(in, n);
    check(std::fabs(n[0] + n[1] - W) < 1.0 && n[kKangaroo] == 0.0, "turn: blades alone");
    in.columnShare = 0.0;
    const double r = 36.0, c = std::cos(0.5236), s = std::sin(0.5236);
    const double fx[4] = {r * c, -r * c, -r * c, r * c}, fy[4] = {r * s, r * s, -r * s, -r * s};
    for (int i = 0; i < 4; ++i) { in.feetX[i] = fx[i]; in.feetY[i] = fy[i]; }
    in.cosSplay = 1.0;
    in.windX = 4e6;
    in.windY = 0.0;
    LegLoads(in, n);
    double sum = 0.0, mx = 0.0;
    for (int i = 0; i < 4; ++i) { sum += n[2 + i]; mx += n[2 + i] * fx[i]; }
    check(std::fabs(sum - W) < 1.0 && std::fabs(mx - 4e6 * 80.6) < 1e3 && n[2] > n[3], "stern legs: sum and moment");

    Soles so;
    so.Update(0.1, true, true);
    const double mu0 = so.Mu();
    for (int i = 0; i < 1400; ++i) so.Update(0.1, true, true);
    check(mu0 < 0.55 && std::fabs(so.Mu() - kMuAnchored) < 1e-9, "feet: settling 0.5 -> sintered root 0.9 within ~2 min");
    so.Update(0.1, true, false);
    so.Update(1.0, true, false);
    check(so.Mu() > kMuJammed && so.Mu() < kMuAnchored, "feet: lift-off thrust - the root lets go slowly, still locked");

    Regen re;
    re.Update(0.1, W, 80.0, true);
    for (int i = 1; i <= 490; ++i) re.Update(0.1, W, 80.0 - 0.1 * i, true);
    std::printf("     lowering 49 m returns %.1f GJ\n", re.Returned() / 1e9);
    check(std::fabs(re.Returned() - 0.9 * W * 49.0) < 1e6, "regeneration 90 %");
    {   // design case: launch mass on a 2.5 g planet, every support x1.5 within strength and buckling
        namespace sp = tantra::spec;
        const double Wd = sp::kLaunchMass * g * sp::kDesignG;
        const double kang = (58.1 - 53.4) / (111.4 - 53.4);                 // launch CG, parked trunnions, kangaroo foot
        // (the column values are the stepped telescopes' - Spec.h)
        auto pcr = [](double I, double L) { return 9.8696 * sp::kCntE * I / (L * L); };
        auto ratio = [&](double N, double A, double I, double L) {
            return N * sp::kSafety / std::fmin(sp::kCntSigma * A, pcr(I, L));
        };
        const double rB = ratio(Wd / 2.0, sp::kBladeA, sp::kBladeI, sp::kBladeColumnL);
        const double rK = ratio(Wd * kang, sp::kKangShinA, sp::kKangShinI, sp::kKangColumnL);
        const double rS = ratio(Wd / 4.0 * (sp::kDesignG + sp::kStrutExtraG) / sp::kDesignG, sp::kSternShinA, sp::kSternShinI, sp::kSternLegL);
        std::printf("     2.5 g x1.5: blade on the turn %.2f, kangaroo lifted %.2f, stern leg landing %.2f\n", rB, rK, rS);
        check(rB <= 1.0 && rK <= 1.0 && rS <= 1.0, "every leg holds the launch mass at 2.5 g with the 1.5 margin");
    }
    {   // cup feet on LOOSE sand at touchdown (no sintering yet): Terzaghi, local shear (phi 30 -> 21 deg), 1550 kg/m3,
        // the skirt as embedment; the soil's weight grows with g like the load, so the margin holds on any planet
        namespace sp = tantra::spec;
        const double gd = g * sp::kDesignG, Wd = sp::kLaunchMass * gd, gam = 1550.0 * gd;
        const double ph = std::atan(2.0 / 3.0 * std::tan(30.0 * 3.14159265 / 180.0)), t = std::tan(ph);
        const double Nq = std::exp(3.14159265 * t) * std::pow(std::tan(3.14159265 / 4 + ph / 2), 2), Ng = 2.0 * (Nq + 1.0) * t;
        auto fs = [&](double F, double R) {
            const double p = F / (3.14159265 * R * R);
            return (gam * sp::kFootSkirt * Nq * (1.0 + t) + 0.3 * gam * 2.0 * R * Ng) / p;
        };
        const double kang = (58.1 - 53.4) / (111.4 - 53.4);
        const double fB = fs(Wd / 2.0, sp::kFootRBlade), fS = fs(Wd / 4.0 * (sp::kDesignG + sp::kStrutExtraG) / sp::kDesignG, sp::kFootRStern),
                     fK = fs(Wd * kang, sp::kFootRKang);
        std::printf("     loose sand, touchdown: blade foot %.2f, stern foot (landing) %.2f, kangaroo foot %.2f\n", fB, fS, fK);
        check(fB >= 1.5 && fS >= 1.5 && fK >= 1.5, "every cup foot holds on loose sand at touchdown with the 1.5 margin");
    }
    std::printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
