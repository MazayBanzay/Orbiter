// Offline check of the drive's effective exhaust and its radiation (no Orbiter).
#include <cstdio>

#include "../core/Params.h"
#include "../core/Radiation.h"

int main() {
    tantra::ShipParams prm;
    tantra::Drive drive(prm.MakeDriveSpec(4));
    tantra::Radiation rad(prm.MakeRadiationSpec());
    const double full = drive.JetPower(1.0);
    std::printf("effective exhaust %.3f c, fuel %.0f kg/s, jet power %.2e W (%.1f Gt TNT/s), pellet %.1f g (%.0f kt)\n",
                prm.AnaExhaust() / 2.998e8, 4 * prm.anaThrust / prm.AnaExhaust(), full, full / 4.184e18,
                drive.PelletMass() * 1e3, drive.PelletEnergy() / 4.184e12);
    std::printf("gammas %.2e W, charged jet %.2e W\n", rad.GammaPower(full), rad.ChargedPower(full));
    for (double t : {1.0, 3600.0, 86400.0})
        std::printf("lethal within %6.0f s: gammas to %.3g km, in the jet to %.3g km\n", t, rad.LethalRadius(full, t) / 1e3,
                    rad.LethalBeamRange(full, t) / 1e3);
    const double RE = 6.371e6;
    struct Case { const char* name; double d; bool beam; } cases[] = {
        {"Earth from LEO 750 km", RE + 7.5e5, false},
        {"Earth from 1 Mkm", 1e9, false},
        {"Earth from 1 Mkm, jet on it", 1e9, true},
        {"Earth from 1 AU, jet on it", 1.496e11, true},
    };
    for (const Case& c : cases) {
        const double p = rad.InterceptedPower(full, RE, c.d, c.beam);
        static const char* hz[] = {"none", "harm", "CATASTROPHE"};
        std::printf("%-28s intercepted %.2e W: %s, safe level %.2e\n", c.name, p,
                    hz[static_cast<int>(rad.PlanetHazard(p))], rad.PlanetSafeLevel(full, RE, c.d, c.beam));
    }
    for (double r : {1e3, 1e5, 1e7})
        std::printf("person at %.0e m: %.2e Gy/h (in jet %.2e), safe level %.2e\n", r, rad.DoseRate(full, r, false) * 3600,
                    rad.DoseRate(full, r, true) * 3600, rad.PersonSafeLevel(full, r, false));
}
