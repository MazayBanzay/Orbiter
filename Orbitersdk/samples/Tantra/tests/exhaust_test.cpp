// Offline check of the exhaust model (no Orbiter).
#include <cmath>
#include <cmath>
#include <cstdio>
#include "../core/ExhaustModel.h"
int main() {
    tantra::ExhaustModel m;
    std::printf("decay nodes: %.2f m, %.1f m, %.1f m; muon glow %.0f m\n", m.NodeDistance(0), m.NodeDistance(1),
                m.NodeDistance(2), m.MuonGlowLength());
    for (double feed : {1.0, 0.5, 0.1}) {
        auto f = m.Evaluate(1, 1, feed, 0.0, 0.5);
        std::printf("feed %.0f%%: first knot %.0f km, spacing %.1f km, knots drawn %zu\n", feed * 100, f.firstKnot / 1e3,
                    f.knotSpacing / 1e3, f.knots.size());
    }
    for (double rho : {1.5, 0.113, 0.0085}) {
        auto f = m.Evaluate(1, 1, 0.4, rho, 0.5);
        std::printf("air %.4f kg/m3: glow length %.0f m, knots %zu\n", rho, f.longGlowLength, f.knots.size());
    }
    // Beam pointed down from above: stopped where the air column reaches the muon range
    // (exponential air, H = 8 km, rho0 = 1.225).
    const double col = m.Spec().absorptionColumn;
    const double h = 8000.0 * std::log(1.225 * 8000.0 / col), rhoH = 1.225 * std::exp(-h / 8000.0);
    std::printf("vertical beam ends at %.1f km altitude, rho %.3f\n", h / 1e3, rhoH);
    for (double p : {1.2e12, 1.2e14, 1.2e17}) {
        tantra::AirPath path;
        path.absorbDist = 5000.0;
        path.depositDensity = rhoH;
        auto f = m.Evaluate(1, 1, 1.0, 0.01, 0.5, path, p);
        std::printf("P %.1e W: plasma column R %.0f m, range %.0f m, ends %.0f m, smoke %.2f, packets %zu\n", p,
                    f.fireball.size, f.fireball.dist, f.columnEnd, f.smoke, f.packets.size());
    }
    std::printf("sea level, P 1.2e12 W: R %.0f m\n", tantra::ExhaustModel::FireballRadius(1.2e12, 1.225));
    for (double deg : {0.0, 30.0, 90.0, 180.0}) {
        const double d = tantra::ExhaustModel::DopplerFactor(0.98, std::cos(deg * 3.14159265 / 180.0));
        std::printf("view %3.0f deg from behind: Doppler %.2f, brightness x%.1e, visual level %.2f\n", deg, d,
                    std::pow(d, 2.5), tantra::ExhaustModel::BeamingLevel(d));
    }
    for (double fl : {0.0, 0.5, 0.95}) {
        auto f = m.Evaluate(1, 1, 1.0, 0.0, fl);
        std::printf("flicker %.2f: packets shown %zu, first at %.1f km\n", fl, f.packets.size(),
                    f.packets.empty() ? 0.0 : f.packets[0].dist / 1e3);
    }
    tantra::AirPath air;
    air.absorbDist = 750.0;
    air.depositDensity = 1.2;
    auto gb = m.Evaluate(1, 1, 0.0, 1.2, 0.5, air, 0.0);
    std::printf("guide beam in air: thread %.2f over %.0f m; in vacuum: thread %.2f, mouth glow %.2f\n", gb.thread,
                gb.threadLength, m.Evaluate(1, 1, 0.0, 0.0, 0.5).thread, m.Evaluate(1, 1, 0.0, 0.0, 0.5).cup.level);
}
