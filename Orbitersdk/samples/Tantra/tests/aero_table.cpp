// Prints the aerodynamic coefficients of core/Aero across Mach and angle of attack and checks them
// against the hypersonic panel model of tantra_c148.html (L/D at the entry angles) and basic physics.
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "../core/Aero.h"

using namespace tantra::aero;

int main() {
    const double D = 3.14159265358979323846 / 180.0;
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++fails; }
    };
    std::printf("Body, nose first (referred to %.0f m2 planform):\n  M     ", kPlanform);
    const double alphas[] = {0, 5, 10, 20, 30, 40, 50, 60, 70, 90};
    for (double a : alphas) std::printf("  a=%-3.0f CL/CD   ", a);
    std::printf("\n");
    for (double M : {0.3, 0.9, 1.1, 2.0, 5.0, 10.0, 25.0}) {
        std::printf("  %-5.1f ", M);
        for (double a : alphas) {
            double cl, cd;
            BodyPitch(a * D, M, &cl, &cd);
            std::printf("  %5.2f/%5.3f  ", cl, cd);
        }
        std::printf("\n");
    }
    // Hypersonic L/D against the mockup's Newtonian panel model (hull + crests + flap there; hull only here).
    const double mock[][2] = {{30, 1.43}, {40, 1.05}, {50, 0.71}, {60, 0.52}, {70, 0.33}};
    std::printf("Hypersonic L/D (M 25) vs mockup panel model:\n");
    for (auto& m : mock) {
        double cl, cd;
        BodyPitch(m[0] * D, 25.0, &cl, &cd);
        std::printf("  a=%2.0f  L/D %.2f  mockup %.2f\n", m[0], cl / cd, m[1]);
        check(std::fabs(cl / cd - m[1]) < 0.2 * m[1] + 0.05, "hypersonic L/D within 20% of the panel model");
    }
    // Zero-lift drag through the regimes, nose first and stern first.
    std::printf("Axial drag on the frontal area (%.0f m2):\n", kFrontal);
    for (double M : {0.3, 0.8, 0.95, 1.1, 1.5, 2.0, 3.0, 5.0, 10.0, 25.0})
        std::printf("  M %5.2f  nose %.3f  stern %.3f\n", M, AxialNoseFirst(M), AxialSternFirst(M));
    check(AxialNoseFirst(1.1) > 3.0 * AxialNoseFirst(0.5), "transonic drag rise");
    check(AxialNoseFirst(10) < AxialNoseFirst(2), "wave drag falls with Mach");
    check(AxialSternFirst(3) > 4.0 * AxialNoseFirst(3), "stern first is blunt");
    double cl, cd;
    BodyPitch(90 * D, 25.0, &cl, &cd);
    check(std::fabs(cd - 1.9) < 0.05 && std::fabs(cl) < 0.05, "broadside: Newtonian CD ~1.9, no lift");
    BodyPitch(180 * D, 0.5, &cl, &cd);
    check(cd > 0.1 && std::fabs(cl) < 0.01, "stern first: drag, no lift");
    // Plates.
    std::printf("Crest plate (A 1.33), CL at a=10 / 30 / 50 deg:\n");
    for (double M : {0.3, 1.5, 3.0, 6.0, 25.0}) {
        double c1, d1, c2, d2, c3, d3;
        Plate(10 * D, M, 1.33, &c1, &d1);
        Plate(30 * D, M, 1.33, &c2, &d2);
        Plate(50 * D, M, 1.33, &c3, &d3);
        std::printf("  M %4.1f  %.2f  %.2f  %.2f\n", M, c1, c2, c3);
    }
    Plate(10 * D, 0.3, 1.33, &cl, &cd);
    double cls;
    Plate(10 * D, 3.0, 1.33, &cls, &cd);
    check(cls < cl, "supersonic lift slope below subsonic");
    std::printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
