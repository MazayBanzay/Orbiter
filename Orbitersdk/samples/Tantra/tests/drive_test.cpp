// Offline check of the drive model (no Orbiter). Build: see tests/run_tests.bat
#include <cstdio>
#include <initializer_list>
#include "../core/Drive.h"
int main() {
    tantra::Drive d;
    const double g = 9.80665;
    std::printf("pellet %.2f g, %.1f kt, jet power full %.2e W\n", d.PelletMass() * 1e3, d.PelletEnergy() / 4.184e12,
                d.JetPower(1.0));
    for (double m : {26.2e6, 1.5e6}) {
        double lv = d.LevelForResidual(5 * g, m, 1.0);
        double a = lv * 4 * d.Spec().maxThrust / m;
        d.Update(0.1, 1.0, lv, m, a);
        std::printf("mass %.1f kt: max level %.3f -> thrust accel %.0f g, compensated %.0f g, felt %.1f g\n", m / 1e6, lv,
                    a / g, d.CompensatedAccel() / g, d.ResidualAccel() / g);
    }
    tantra::Drive e;
    e.SetStoreFraction(0.0);
    double t = 0;
    while (!e.CanRaiseField(0.0)) { e.Update(3600, 0, 0, 1e6, 0); t += 3600; }
    std::printf("store recharge from empty to start-capable: %.1f h\n", t / 3600);
    return 0;
}
