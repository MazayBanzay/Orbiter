// Offline checks of the planetary power plant (core/Plant) against the mockups (tantra_plant_screen.html).
#include "../core/Plant.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace tantra::plant;
static int fails = 0;
static void check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++fails; } }
static unsigned seed = 1;
static double rnd() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; }

struct Run { bool lost; double maxT, thrust; int failures; };
static Run run(const Config& cfg, double sec, bool air, double sternH, double level, void (*setup)(Plant&)) {
    Plant p(cfg);
    if (setup) setup(p);
    Env e; e.air = air; e.rho = air ? 1.225 : 0.0; e.sternH = sternH; e.level = level;
    Run r{false, 300.0, 0.0, 0};
    for (double t = 0; t < sec; t += 0.05) {
        const Output o = p.Step(0.05, e, rnd);
        r.maxT = std::max(r.maxT, p.SternT()); r.thrust = o.maxThrust;
        for (const Event& ev : p.TakeEvents()) if (ev.bad && ev.ru.find("Корма") == std::string::npos) ++r.failures;
        if (p.Lost()) { r.lost = true; break; }
    }
    return r;
}

int main() {
    Config cfg;
    {
        std::ifstream f("../../../../Config/Tantra/plant.cfg", std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        cfg.Parse(ss.str());
        std::printf("config: %zu failures, chi %.0e, risk0 %.1e k %.1f\n", cfg.fails.size(), cfg.chi, cfg.risk0, cfg.riskK);
        check(cfg.fails.size() == 10, "the config lists 10 failures");
    }
    // field cap: 12.1 T over the march cup = 886 MN
    const double F = Plant::FieldThrust(12.1, 3.14159265 * 2.2 * 2.2);
    std::printf("field thrust at 12.1 T: %.0f MN\n", F / 1e6);
    check(std::fabs(F - 886e6) < 5e6, "12.1 T holds 886 MN");
    // the risk curve: +20 / 50 / 90 % -> 0.06 / 0.65 / 11 % per minute
    auto risk = [&](double e) { return cfg.risk0 * (std::exp(cfg.riskK * e) - 1.0); };
    std::printf("risk per minute: +20 %% %.3f %%, +50 %% %.2f %%, +90 %% %.1f %%\n", risk(0.2) * 100, risk(0.5) * 100, risk(0.9) * 100);
    check(std::fabs(risk(0.9) - 0.108) < 0.01, "risk at +90 % ~ 11 %/min");
    // nominal launch on argon: cool, full field thrust
    Run r = run(cfg, 120, true, 22.5, 1.0, nullptr);
    std::printf("nominal launch 2 min: maxT %.0f K, thrust %.0f MN, lost %d\n", r.maxT, r.thrust / 1e6, r.lost);
    check(!r.lost && r.maxT < 400 && std::fabs(r.thrust - 886e6) < 5e6, "nominal launch: cool, 886 MN");
    // the limiter refuses iron in the air
    r = run(cfg, 30, true, 22.5, 1.0, [](Plant& p) { p.CycleMass(); p.CycleMass(); });   // iron
    check(!r.lost && r.maxT < 400, "limiter: iron in the air runs argon");
    // manual: iron on the pad burns the stern within seconds
    r = run(cfg, 30, true, 22.5, 1.0, [](Plant& p) { p.LimiterPress(0, true); p.LimiterPress(1, true); p.CycleMass(); p.CycleMass(); });
    std::printf("manual iron on the pad: lost %d, maxT %.0f K\n", r.lost, r.maxT);
    check(r.lost, "manual iron on the pad: the stern burns through");
    // manual +90 % on argon at the launch: the stern heats, mostly survives 2 min
    int lost = 0; double sumT = 0;
    for (int i = 0; i < 40; ++i) { seed = 100 + i; Run q = run(cfg, 120, true, 22.5, 1.0, [](Plant& p) { p.LimiterPress(0, true); p.LimiterPress(1, true); p.PowerStep(90); }); lost += q.lost; sumT += q.maxT; }
    std::printf("manual +90 %% launch, 40 runs: lost %d, mean maxT %.0f K\n", lost, sumT / 40);
    check(lost < 10 && sumT / 40 > 900, "+90 %: a hot stern, most survive");
    // manual 16 T in space: no heat, failures happen
    int withFail = 0;
    for (int i = 0; i < 40; ++i) { seed = 500 + i; Run q = run(cfg, 300, false, -1, 1.0, [](Plant& p) { p.LimiterPress(0, true); p.LimiterPress(1, true); p.FieldStep(3.9); }); withFail += q.failures > 0; }
    std::printf("manual 16 T in space 5 min, 40 runs: with failures %d\n", withFail);
    check(withFail > 4 && withFail < 30, "16 T: about a third fail");
    // save / load
    Plant a(cfg); a.LimiterPress(0, true); a.LimiterPress(1, true); a.FieldStep(1.0); a.PowerStep(20);
    Plant b(cfg); b.Load(a.Save());
    check(!b.Limiter() && std::fabs(b.FieldSet() - 13.1) < 1e-6 && std::fabs(b.PowerPct() - 120) < 1e-6, "save / load");
    std::printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
