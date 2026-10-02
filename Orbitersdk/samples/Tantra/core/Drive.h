// Tantra core: pulsed anamezon drive, field store and inertia compensator.
// No Orbiter dependencies. Model and numbers: docs/DESIGN.md, "Анамезонный двигатель".
//
// Physics picture (canon + advanced known ideas):
//  * Anamezon pellets are extracted from the magnetic traps and injected into the
//    magnetic nozzle focus behind the stern (Mini-Mag Orion / beamed-core lineage).
//    Thrust is throttled by the pellet rate; each pellet is a micro-explosion.
//  * The ~1000 T chamber/nozzle field holds ~1.6e14 J per chamber. The power plant
//    cannot raise that in seconds, so the energy lives in a superconducting field
//    store charged slowly by the plant; start-up moves it into the chambers and
//    shutdown returns most of it (inductive round trip, Helion-style).
//  * During a burn every pulse pushes against the field; inductive recovery
//    powers the inertia compensator (the canonical "artificial gravity apparatus").
#pragma once

namespace tantra {

struct DriveSpec {
    int chambers = 4;
    double maxThrust = 2.17e10;        // N per chamber (canon ~200 g at ~44 kt)
    double exhaust = 0.98 * 299792458.0;
    double massToEnergy = 0.80;        // fraction of pellet mass released (ve = 0.98 c)
    double maxPelletRate = 2.0e4;      // Hz per chamber at full thrust (~edge of hearing)
    double chamberFieldEnergy = 1.56e14;  // J per chamber: (1000 T)^2/2mu0 * 392 m^3
    double storeCapacity = 7.0e14;     // J, field store
    double roundTrip = 0.95;           // field energy returned on shutdown
    double recoveryFraction = 1.0e-3;  // share of jet power recovered inductively
    double compWattPerNewton = 1.0e5;  // compensator power per newton cancelled
    double compMaxAccel = 200.0 * 9.80665;  // m/s^2 the apparatus can cancel
    double plantPower = 2.0e9;         // W, onboard power plant (VEU)
    double housekeeping = 3.0e7;       // W, traps upkeep + life support + systems
};

class Drive {
public:
    explicit Drive(const DriveSpec& spec = DriveSpec()) : spec_(spec), store_(spec.storeCapacity) {}

    const DriveSpec& Spec() const { return spec_; }
    // Replace the parameters (e.g. after reading the config); keeps the store charge fraction.
    void Configure(const DriveSpec& spec) {
        const double f = StoreFraction();
        spec_ = spec;
        store_ = f * spec_.storeCapacity;
    }

    // fieldLevel: 0..1 chamber field (from the ignition sequence); level: 0..1 feed.
    // mass: ship mass [kg]; thrustAccel: current thrust acceleration [m/s^2].
    void Update(double dt, double fieldLevel, double level, double mass, double thrustAccel);

    // Can the store bring the chambers up from their present field level?
    bool CanRaiseField(double fieldLevel) const;

    double StoreEnergy() const { return store_; }
    double StoreFraction() const { return store_ / spec_.storeCapacity; }
    void SetStoreFraction(double f);

    // Pellet train of one chamber at feed level `level`.
    double PelletRate(double level) const { return level * spec_.maxPelletRate; }
    double PelletMass() const;
    double PelletEnergy() const { return PelletMass() * spec_.massToEnergy * 8.987551787e16; }
    double JetPower(double level) const;  // all chambers [W]

    double RecoveredPower() const { return recovered_; }
    double CompensatedAccel() const { return compensated_; }
    double ResidualAccel() const { return residual_; }

    // The apparatus cancels the load of the thrust of THIS step: it acts on the inertial field, not on a command that
    // has to arrive first. Call it after the step's thrust is known (Update only sees the previous step's).
    void Compensate(double level, double mass, double thrustAccel);

    // Highest feed level that keeps the uncompensated acceleration at or below gLimit.
    double LevelForResidual(double residualLimit, double mass, double fieldLevel) const;

private:
    double CompensationCapacity(double level, double mass) const;

    DriveSpec spec_;
    double store_;
    double chamberField_ = 0.0;  // J in all chambers
    double recovered_ = 0.0, compensated_ = 0.0, residual_ = 0.0;
};

}  // namespace tantra
