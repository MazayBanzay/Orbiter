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

struct ShipParams {
    // Masses [kg]
    double dryMass = 4.203e6;          // T9: hull, legs, pods, hangar kit, nose cups, planetary installation (Spec kDryCGS)
    double trapStructMass = 164.0e3;   // per trap (30 T virial bound)
    double trapFuelMass = 9370.0e3;    // anamezon per trap (4.8 t/m^3)
    double argonMass = 6.2e6;          // planetary reaction mass below 30 km (liquid, 87 K)
    double ironMass = 3.8e6;           // planetary reaction mass above 30 km and between the planets

    // Anamezon drive (also feeds DriveSpec)
    double anaThrust = 2.17e10;        // N per chamber (~200 g at 44 kt)
    double anaExhaustC = 0.98;         // speed of the jet particles as a fraction of c
    double pelletRate = 2.0e4;         // Hz per chamber at full thrust
    double massToEnergy = 0.80;
    // Only the charged products can be turned into thrust; neutral pions give gammas
    // that leave in all directions. The magnetic nozzle does not collimate perfectly.
    double anaChargedFraction = 0.62;  // of the released energy
    double anaNozzleEfficiency = 0.85; // momentum of the charged jet that ends up axial
    double anaJetHalfAngle = 0.087;    // rad, ~5 deg cone of the jet past the nozzle
    double chamberFieldEnergy = 1.56e14;
    double fieldStoreCapacity = 7.0e14;
    double fieldRoundTrip = 0.95;
    double recoveryFraction = 1.0e-3;
    double compWattPerNewton = 1.0e5;
    double compMaxG = 200.0;
    double plantPower = 2.0e9;
    double housekeeping = 3.0e7;

    // Hot-start interlock: h_min = base + perDecade * lg(P / refPower), never below base.
    double hotStartBaseAlt = 50.0e3;   // m
    double hotStartPerDecade = 10.0e3; // m per factor of 10 in jet power
    double hotStartRefPower = 1.2e17;  // W

    // Planetary engines (p-11B pulsed fusion, magnetic confinement and acceleration of the reaction mass):
    // the marching cup in the stern well (R 2.2, 12.1 T) and four pods x 3 cups (350 MN each).
    double marchThrust = 8.86e8;       // N (T/W 1.81 loaded at 1 g; 0.95 at 1.9 g - the pods help there)
    double podThrustTotal = 1.4e9;     // N, four pods
    double argonExhaust = 3.0e4;       // m/s near the ground (mixing-layer heating limits the jet speed)
    double ironExhaust = 3.0e5;        // m/s above 30 km

    // Attitude micro-motors
    double attAngAccel = 0.01;         // rad/s^2
    double attMaxThrust = 4.0e6;       // N per motor
    double attAxialAccel = 0.2;        // m/s^2

    // Operations
    double gLimitDefault = 5.0;        // g, felt (uncompensated)

    // Effective exhaust speed (thrust / fuel flow): the momentum of the axial charged jet
    // per unit of fuel mass, v = beta * (energy per kg / c) * charged * nozzle. With the
    // defaults 0.98 * 0.80 * 0.62 * 0.85 = 0.41 c (pion-rocket studies: 0.33-0.6 c).
    double AnaExhaust() const {
        return anaExhaustC * massToEnergy * anaChargedFraction * anaNozzleEfficiency * C_LIGHT;
    }
    double AnaJetSpeed() const { return anaExhaustC * C_LIGHT; }

    RadiationSpec MakeRadiationSpec() const {
        RadiationSpec r;
        r.chargedFraction = anaChargedFraction;
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
