// Tantra: radiation safety of the anamezon drive for Orbiter 2010.
// Feeds core/Radiation with the positions of planets and vessels, finds the planets
// with an ecosystem and the crews put at risk, and works out a removable interlock:
// the highest feed level that keeps every ecosystem below harm and every unshielded
// crew below a lethal dose within an hour. Other vessels' crews cannot be touched
// directly, so their doses are only accumulated and announced; OrbiterCrew people get
// the drive as a radiation source in their environment (docs/PORTING_CREW.md).
#pragma once
#include "orbitersdk.h"

#include <string>
#include <vector>

#include "../core/Radiation.h"

class TantraSafety {
public:
    struct PlanetRisk {
        std::string name;
        double distance = 0.0;      // m
        double intercepted = 0.0;   // W at the current level
        tantra::Hazard hazard = tantra::Hazard::None;
        bool inBeam = false;
        double safeLevel = 1.0;     // highest level keeping it below harm
    };
    struct CrewRisk {
        std::string name;
        double distance = 0.0;      // m
        double rate = 0.0;          // Gy/s, unshielded, at the current level
        double dose = 0.0;          // Gy accumulated
        bool inBeam = false;
        double safeLevel = 1.0;
    };
    struct Notice {
        std::string ru, en;
    };

    explicit TantraSafety(const tantra::RadiationSpec& spec) : rad_(spec) {}

    // jetPowerFull: jet power at full feed [W]; level: current feed (0 when the
    // anamezon drive is not feeding). Recomputes positions every 0.5 s, doses every step.
    void Update(VESSEL* self, double jetPowerFull, double level, double dt);

    // Highest feed level that endangers no ecosystem and no crew (1 = full is safe).
    double LevelCap() const { return cap_; }
    // What sets the cap, for the interlock message (empty if nothing).
    const std::string& CapReasonRu() const { return reasonRu_; }
    const std::string& CapReasonEn() const { return reasonEn_; }

    const std::vector<PlanetRisk>& Planets() const { return planets_; }
    const std::vector<CrewRisk>& Crews() const { return crews_; }
    double LethalRadius(double jetPower) const { return rad_.LethalRadius(jetPower, rad_.Spec().hazardHorizon); }
    double LethalBeamRange(double jetPower) const {
        return rad_.LethalBeamRange(jetPower, rad_.Spec().hazardHorizon);
    }
    const tantra::DoseLedger& Ledger() const { return ledger_; }

    // Dose thresholds crossed since the last call (1 Gy, lethal).
    std::vector<Notice> TakeNotices();

private:
    void Scan(VESSEL* self, double jetPowerFull, double level);

    tantra::Radiation rad_;
    tantra::DoseLedger ledger_;
    std::vector<PlanetRisk> planets_;
    std::vector<CrewRisk> crews_;
    std::vector<Notice> notices_;
    double cap_ = 1.0;
    std::string reasonRu_, reasonEn_;
    double scanTimer_ = 0.0;
};
