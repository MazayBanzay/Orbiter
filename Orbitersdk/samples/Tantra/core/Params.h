// Tantra core: tunable ship parameters. No Orbiter dependencies.
// Defaults are the agreed design values (docs/DESIGN.md, Tantra_Design/DESIGN_LOCAL.md); every field can be
// overridden from Config\Vessels\Tantra.cfg without rebuilding (see the key table in orbiter2016/Tantra.cpp,
// LoadParams). Geometry stays in Spec.h because it must match the mesh.
#pragma once
#include <cmath>

#include "Drive.h"
#include "Physics.h"
#include "Radiation.h"

namespace tantra {

constexpr double kRadChargedFraction = 0.62;   // charged share of the annihilation energy (the dose model)

struct ShipParams {
    // Masses [kg]
    // (2026-10-09, Spec mass budget) dry operating: structure and systems 5 832 t, hangar cargo except the lander and
    // the MPU (separate vessels), crew, consumables; its CG Spec kDryCGS
    double dryMass = 5.99e6;
    double trapStructMass = 641.0e3;   // per trap: the container is the magnetic trap - coils, magnets, shell (fibre ~12 GPa)
    double trapFuelMass = 45.2e6;      // anamezon per trap (2026-10-09 canon: 0.899 c in 55 h with braking -> 181 kt in 4)
    double argonMass = 23.9e6;         // ion charges, dense (body tanks 15.4 kt, aft tank 8.5 kt): the take-off arc at 30 km/s
    double ironMass = 4.0e6;           // planetary reaction mass in space; 3.3 m of solid iron in the screen at sub-light

    // Anamezon drive (also feeds DriveSpec)
    double anaThrust = 2.29e9;         // N per chamber: speed model 1 (ApplySpeedModel)
    double anaExhaustC = 0.98;         // speed of the jet particles as a fraction of c
    double pelletRate = 8.9e2;         // Hz per chamber at full thrust (~8.8 g pellets)
    // (2026-10-09) the canon's assumption, as Sapiga takes it: the whole mass turns into the jet at 0.98 c. Really ~1/3
    // of the energy leaves as neutral pions and gammas (0.80 x 0.62 x 0.85 gave 0.41 c); the dose model keeps that
    // real gamma share (kRadChargedFraction).
    double massToEnergy = 1.0;
    double anaChargedFraction = 1.0;   // of the released energy, into the jet
    double anaNozzleEfficiency = 1.0;  // momentum of the jet that ends up axial
    double anaJetHalfAngle = 0.087;    // rad, ~5 deg cone of the jet past the nozzle
    double chamberFieldEnergy = 1.56e14;
    double fieldStoreCapacity = 7.0e14;
    double fieldRoundTrip = 0.95;
    double recoveryFraction = 1.0e-3;
    double compWattPerNewton = 1.0e5;
    double compMaxG = 20.0;
    double plantPower = 2.0e9;
    double housekeeping = 3.0e7;

    // Hot-start interlock: h_min = base + perDecade * lg(P / refPower), never below base.
    double hotStartBaseAlt = 50.0e3;   // m
    double hotStartPerDecade = 10.0e3; // m per factor of 10 in jet power
    double hotStartRefPower = 1.2e17;  // W

    // Planetary engines (p-11B pulsed fusion, magnetic confinement and acceleration of the reaction mass):
    // the marching cup in the stern well (R 2.2, 12.1 T) and four pods x 3 cups (350 MN each).
    double marchThrust = 8.86e8;       // N (T/W 1.81 loaded at 1 g; 0.95 at 1.9 g - the pods help there)
    double podThrustTotal = 6.65e8;    // N, four pods x 3 cups R 0.55 (55 MN a cup; user's decision 5, 2026-10-09)
    double argonExhaust = 3.0e4;       // m/s in air (mixing-layer heating limits the jet speed)
    double argonGroundExhaust = 7.0e3; // m/s below kArgonGroundAlt over the ground: the jet's power on the ground 4x less
    double argonDenseExhaust = 5.5e3;  // m/s below kArgonDenseAlt in a dense atmosphere (Titan, Earth)
    double ironExhaust = 3.0e5;        // m/s above 30 km

    // Attitude micro-motors
    double attAngAccel = 0.01;         // rad/s^2
    double attMaxThrust = 4.0e6;       // N per motor
    double attAxialAccel = 0.2;        // m/s^2

    // Operations
    double gLimitDefault = 5.0;        // g, felt (uncompensated)

    // Speed model (2026-10-09): one drive core and the same traps - the models differ ONLY in the drive's power (the user).
    // The traps' load is the canon's (the user: «Канон.»): 0.899 c in 55 h from the dark planet with braking at home, jet
    // 0.98 c: mass ratio 19.9 -> 4 x 45.2 kt anamezon, 217 kt with the charges at lift-off. The ship rose FULL from the dark
    // planet at 2.5 g: planetary engines on an arc - the marching cup and the four stern blocks on ion charges (8.1 GN,
    // T/W 1.52, 1.22 with a stern block out) - then the anamezon. Loading at Triton is only for economy and safety.
    //   0 Efremov: 0.899 c in 55 h (the canon's inertia damper on the whole ship, compensator to 550 g);
    //   1 lower power: the same fuel and speed, ~4 g at departure, weeks to speed.
    int speedModel = 1;
    void ApplySpeedModel() {
        trapFuelMass = 45.2e6;
        if (speedModel == 1) {
            anaThrust = 2.29e9; pelletRate = 8.9e2; compMaxG = 20.0;
        } else {
            anaThrust = 5.45e10; pelletRate = 2.1e4; compMaxG = 550.0;   // 55 h to 0.899 c; 8.8 g pellets
        }
    }

    // Effective exhaust speed (thrust / fuel flow): the momentum of the axial jet per unit of fuel mass,
    // v = beta * (energy per kg / c) * charged * nozzle = 0.98 c with the canon's assumption above.
    double AnaExhaust() const {
        return anaExhaustC * massToEnergy * anaChargedFraction * anaNozzleEfficiency * C_LIGHT;
    }
    double AnaJetSpeed() const { return anaExhaustC * C_LIGHT; }

    RadiationSpec MakeRadiationSpec() const {
        RadiationSpec r;
        r.chargedFraction = kRadChargedFraction;   // the real share: neutral pions give gammas in all directions
        r.jetHalfAngle = anaJetHalfAngle;
        return r;
    }

    DriveSpec MakeDriveSpec(int chambers) const {
        DriveSpec d;
        d.chambers = chambers;
        d.maxThrust = anaThrust;
        d.exhaust = AnaExhaust();
        d.massToEnergy = massToEnergy;
        d.maxPelletRate = pelletRate;
        d.chamberFieldEnergy = chamberFieldEnergy;
        d.storeCapacity = fieldStoreCapacity;
        d.roundTrip = fieldRoundTrip;
        d.recoveryFraction = recoveryFraction;
        d.compWattPerNewton = compWattPerNewton;
        d.compMaxAccel = compMaxG * G0;
        d.plantPower = plantPower;
        d.housekeeping = housekeeping;
        return d;
    }

    // Highest jet power allowed at this altitude inside an atmosphere.
    double HotStartPowerLimit(double altitude) const {
        if (altitude < hotStartBaseAlt) return 0.0;
        return hotStartRefPower * std::pow(10.0, (altitude - hotStartBaseAlt) / hotStartPerDecade);
    }
    double HotStartMinAltitude(double jetPower) const {
        if (jetPower <= hotStartRefPower) return hotStartBaseAlt;
        return hotStartBaseAlt + hotStartPerDecade * std::log10(jetPower / hotStartRefPower);
    }
};

}  // namespace tantra
