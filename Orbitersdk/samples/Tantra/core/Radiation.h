// Tantra core: radiation of the running anamezon drive and what it does to people
// and planets. No Orbiter dependencies: the Orbiter adapter feeds it positions and reads
// doses and hazards; for people it is a radiation source for the OrbiterCrew module
// (docs/PORTING_CREW.md).
//
// Two components (docs/DESIGN.md, "Радиация"):
//  * gammas from neutral pions (~1/3 of the annihilation energy), isotropic, flux
//    P_g / (4 pi r^2) - no field can hold or steer them;
//  * the charged jet itself, in a cone of half-angle ~5 deg (transverse momentum of
//    the annihilation and decay products), flux P_j / (pi (r tan a)^2).
// Doses are for an unshielded human; ship hulls and planetary atmospheres shield.
#pragma once
#include <string>
#include <vector>

namespace tantra {

struct RadiationSpec {
    double chargedFraction = 0.62;  // of the jet power: charged products (the rest is gammas)
    double jetHalfAngle = 0.087;    // rad (~5 deg) cone of the charged jet
    // Unshielded human: frontal area, absorbed fraction, mass.
    double bodyArea = 0.5;          // m^2
    double gammaAbsorbed = 0.5;     // of the incident gamma energy
    double jetAbsorbed = 0.12;      // muons/pions: ~60 MeV lost in ~30 g/cm^2 of body
    double bodyMass = 70.0;         // kg
    double lethalDose = 5.0;        // Gy (LD50 without treatment)
    double hazardHorizon = 3600.0;  // s: "lethal within this time" defines the danger zone
    // Planets with an ecosystem (O2 in the air): the atmosphere shields the surface,
    // but energy dumped into the upper air destroys ozone and heats the atmosphere.
    // Thresholds on intercepted power, as fractions of Earth's total solar input.
    double earthSolarInput = 1.74e17;  // W
    // Harm: ~1.7e13 W, a thousand strong X-class flares at once - sustained, it strips
    // ozone and disturbs the upper air. Catastrophe: 1% of sunlight arriving as ionising
    // radiation.
    double harmFraction = 1.0e-4;
    double catastropheFraction = 1.0e-2;
};

enum class Hazard { None = 0, Harm = 1, Catastrophe = 2 };

class Radiation {
public:
    explicit Radiation(const RadiationSpec& spec = RadiationSpec()) : spec_(spec) {}

    // jetPower: total jet power of the drive [W].
    double GammaPower(double jetPower) const { return jetPower * (1.0 - spec_.chargedFraction); }
    double ChargedPower(double jetPower) const { return jetPower * spec_.chargedFraction; }

    // Dose rate [Gy/s] for an unshielded human at distance r [m]; inBeam: inside the
    // jet cone (adds the jet's particle flux).
    double DoseRate(double jetPower, double r, bool inBeam) const;
    // Distance within which an unshielded human gets a lethal dose within `seconds`
    // from the gammas alone [m].
    double LethalRadius(double jetPower, double seconds) const;
    // Same along the jet axis (jet flux dominates) [m].
    double LethalBeamRange(double jetPower, double seconds) const;

    // Power intercepted by a planet of radius R at distance d [W]; beamOnPlanet: the
    // jet cone meets the planet (then the part of the cone it covers is added).
    double InterceptedPower(double jetPower, double R, double d, bool beamOnPlanet) const;
    Hazard PlanetHazard(double intercepted) const;
    // Highest feed level (0..1, of jetPowerFull) that keeps the planet below Harm.
    double PlanetSafeLevel(double jetPowerFull, double R, double d, bool beamOnPlanet) const;
    // Highest level that keeps a person at r from a lethal dose within the horizon.
    double PersonSafeLevel(double jetPowerFull, double r, bool inBeam) const;

    // Is a target at angle `offAxis` [rad] from the jet axis, of angular radius
    // `angularRadius` [rad], touched by the jet cone?
    bool InBeam(double offAxis, double angularRadius) const { return offAxis < spec_.jetHalfAngle + angularRadius; }

    const RadiationSpec& Spec() const { return spec_; }

private:
    RadiationSpec spec_;
};

// Accumulated dose per crewed object (other vessels, EVA crew). In Orbiter 2010 UMmu
// cannot touch another vessel's crew, so the ledger only reports. OrbiterCrew people
// accumulate dose themselves (docs/PORTING_CREW.md); the ledger is for other vessels only.
struct DoseRecord {
    std::string name;
    double dose = 0.0;      // Gy accumulated
    double rate = 0.0;      // Gy/s last step
    int reported = 0;       // highest threshold already announced (0, 1 = 1 Gy, 2 = lethal)
};

class DoseLedger {
public:
    // Adds rate*dt for `name`; returns the record.
    DoseRecord& Add(const std::string& name, double rate, double dt);
    const std::vector<DoseRecord>& Records() const { return records_; }
    void Clear() { records_.clear(); }

private:
    std::vector<DoseRecord> records_;
};

}  // namespace tantra
