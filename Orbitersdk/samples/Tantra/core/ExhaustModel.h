// Tantra core: what the anamezon exhaust looks like, from the drive physics.
// No Orbiter dependencies. Physics: docs/DESIGN.md ("Анамезон и выхлоп").
//
// Near field (per chamber), from the decay chain of the K-particle beam taken
// literally as kaons at the exhaust Lorentz factor (decay length = beta*gamma*c*tau):
//   bright neck at the cup (K0S, ~0.13 m) -> constriction ~18 m (K+-)
//   -> ~75 m (K0L, pi) -> faint muon glow over ~6-7 km.
// Far field (merged beam): knots of internal shocks where faster pellets overtake
// slower ones: first knot x_i = c*beta^2*P / (2*pi*dBeta), then every beta*c*P,
// with P = 1/pellet rate. Their radiation is what makes a burn visible from afar.
// In an atmosphere the beam is a particle beam, not a gas jet: it is absorbed over
// ~90 g/cm^2 of air and the channel glows (N2 violet-blue, Ne orange).
#pragma once
#include <vector>

namespace tantra {

struct ExhaustSpec {
    double exhaustBeta = 0.98;
    double gammaContrast = 1.5;     // gamma2/gamma1 between neighbouring pellets
    double chargedFraction = 0.62;  // of the jet power in charged products (rest: gammas)
    double pelletRateMax = 2.0e4;   // Hz per chamber at full feed
    // Air stops the jet over the muon range: kaons and pions decay (18-75 m, ~20 m)
    // long before they meet a nucleus (90 g/cm^2 is ~750 m at sea level), so the beam
    // in air is muons losing ~2 MeV per g/cm^2: ~0.5 GeV muons (gamma ~5) run ~260 g/cm^2,
    // depositing evenly along the way (not exponentially).
    double absorptionColumn = 2600; // kg/m^2
    double gammaColumn = 400;       // kg/m^2, attenuation length of annihilation gammas in air
    // Internal-shock knots: off. The annihilation products have a broad spectrum, so
    // the pellet packets smear into one flow within ~300 km, and the jet there is far too
    // thin for collective shocks. Kept switchable for a stylised look.
    int maxKnots = 0;
    int maxPackets = 20;            // pellet flashes drawn along the axis
    double packetFade = 3000.0;     // m, e-folding of a packet's afterglow as it expands
    double nozzleLength = 30.0;     // m, magnetic nozzle field region (synchrotron glow)
    double knotRange = 3.0e6;       // m, farthest knot worth drawing
};

struct Glow {
    double dist = 0;       // m behind the chamber mouth (knots: behind the stern)
    double size = 0;       // m, radius of the glowing blob
    double rgb[3] = {0, 0, 0};
    double level = 0;      // 0..1 brightness
};

// Where the beam ends in an atmosphere: found by integrating the air column along
// the beam (the adapter knows the planet); absorbDist >= 1e12 means it escapes.
struct AirPath {
    double absorbDist = 1e12;     // m behind the stern
    double depositDensity = 0.0;  // kg/m^3 at the absorption point
    bool ground = false;          // the beam reaches the surface
};

struct ExhaustFrame {
    Glow cup;                       // at each chamber mouth
    Glow nodes[3];                  // neck + two constrictions, per chamber
    double columnGrey = 0;          // (unused outside: the guide beam is invisible in vacuum)
    double thread = 0;              // guide beam in air: thin fluorescent thread 0..1
    double threadLength = 0;        // m, e-folding length of the thread (= absorption length)
    double columnViolet = 0;        // anamezon flow (stage 3)
    double longGlow = 0;            // afterglow (vacuum) / channel (ship in air) level
    bool channel = false;           // the ship itself is in air: the glow is the air channel
    double longGlowLength = 0;      // m (shortened by the atmosphere)
    std::vector<Glow> knots;        // internal shocks on the ship axis
    std::vector<Glow> packets;      // pellet micro-explosions: every beta*c/f behind the stern
    double firstKnot = 0, knotSpacing = 0;  // m, for instruments
    double light = 0;               // stern point light level
    double lightRgb[3] = {0, 0, 0};
    // Atmosphere: the beam dumps its power where it is absorbed.
    // Plasma column: the muons heat the air evenly along their range from the stern.
    // fireball.dist = range, fireball.size = radius.
    Glow fireball;
    double columnEnd = 0;           // m: where the column stops (the range, or the ground)
    double smoke = 0;               // shock-heated air / condensation wake 0..1
    double dust = 0;                // ground ejecta 0..1 (beam reaches the surface)
    // Gamma halo: annihilation gammas (via pi0 -> 2 gamma, ~1/3 of the energy) cannot be
    // confined by the field and leave the chambers in all directions; in air they ionise
    // N2, which glows blue-violet (N2+ 391/428 nm, as in aurorae or high-altitude nuclear
    // tests). Flux ~ 1/r^2, so the halo is sharply peaked at the ship.
    double halo = 0;                // 0..1
    double haloRadius = 0;          // m, gamma attenuation length (capped)
};

// The hull's own hypersonic wake: drag D [N] is the energy left in the air per metre
// of path [J/m], which drives a cylindrical blast r = (D/rho)^(1/4) t^(1/2) behind the
// ship; the shock-heated layer glows when the stagnation heating is high.
struct HullWake {
    double radius = 0;   // m, blast radius after 1 s
    double hot = 0;      // 0..1 glow of shock-heated air (stagnation heat flux)
    double trail = 0;    // 0..1 condensation / heated-air trail
};
HullWake EvaluateHullWake(double drag, double rho, double speed, double soundSpeed);

class ExhaustModel {
public:
    explicit ExhaustModel(const ExhaustSpec& spec = ExhaustSpec()) : spec_(spec) {}

    // fieldLevel/beamLevel: ignition sequence 0..1; feed: pellet feed 0..1 (throttle
    // while anamezon flows); airDensity: kg/m^3 around the ship (0 in vacuum);
    // flicker: 0..1 random value for this frame (pulse-to-pulse variation).
    // path: where the beam is absorbed (default: vacuum); jetPower: total jet power [W].
    ExhaustFrame Evaluate(double fieldLevel, double beamLevel, double feed, double airDensity, double flicker,
                          const AirPath& path = AirPath(), double jetPower = 0.0) const;

    // Radius of the plasma ball fed with power P [W] in air of density rho: Sedov
    // scaling R = 1.15 (E/rho)^(1/5) t^(2/5) with the energy of one second.
    static double FireballRadius(double power, double rho);
    // Radius of the plasma column: cylindrical blast r = (E'/rho)^(1/4) t^(1/2) with
    // E' = P_charged*1s / L and rho*L = the absorption column: depends on power only.
    double ColumnRadius(double power) const;

    // Relativistic Doppler factor of the jet seen at angle theta from its flow
    // direction (cosTheta = 1: looking at it from behind, up the jet).
    static double DopplerFactor(double beta, double cosTheta);
    // Visual brightness multiplier 0..1 for a jet with Doppler factor delta: a
    // continuous jet brightens as delta^2.5; the 10^5 range is compressed to what a
    // screen can show (behind = 1, side ~0.3, front ~0.1).
    static double BeamingLevel(double delta);

    const ExhaustSpec& Spec() const { return spec_; }

    // Decay-chain lengths at the exhaust Lorentz factor [m].
    double NodeDistance(int i) const;
    double MuonGlowLength() const;
    // Internal-shock geometry at pellet feed `feed` [m].
    double FirstKnot(double feed) const;
    double KnotSpacing(double feed) const;

private:
    ExhaustSpec spec_;
};

}  // namespace tantra
