#include "ExhaustModel.h"

#include <algorithm>
#include <cmath>

namespace tantra {

namespace {
constexpr double kC = 299792458.0;
constexpr double kPi = 3.14159265358979;
// Proper decay lengths c*tau [m]: K0S, K+-, K0L, muon.
constexpr double kCtauK0S = 0.02684, kCtauKpm = 3.711, kCtauK0L = 15.34, kCtauMu = 658.6;

void Set(double* rgb, double r, double g, double b) { rgb[0] = r; rgb[1] = g; rgb[2] = b; }

void Mix(double* rgb, const double* a, const double* b, double t) {
    for (int i = 0; i < 3; ++i) rgb[i] = a[i] + (b[i] - a[i]) * t;
}
}  // namespace

static double BetaGamma(double beta) { return beta / std::sqrt(1.0 - beta * beta); }

double ExhaustModel::NodeDistance(int i) const {
    const double bg = BetaGamma(spec_.exhaustBeta);
    const double ctau[3] = {kCtauK0S, kCtauKpm, kCtauK0L};
    return bg * ctau[std::clamp(i, 0, 2)];
}

double ExhaustModel::MuonGlowLength() const {
    // Muons from kaon decays carry roughly twice the kaon's beta*gamma.
    return 2.0 * BetaGamma(spec_.exhaustBeta) * kCtauMu;
}

double ExhaustModel::FirstKnot(double feed) const {
    if (feed <= 0.0) return 0.0;
    const double ge = 1.0 / std::sqrt(1.0 - spec_.exhaustBeta * spec_.exhaustBeta);
    const double r = std::sqrt(spec_.gammaContrast);
    const double g1 = ge / r, g2 = ge * r;
    const double b1 = std::sqrt(1.0 - 1.0 / (g1 * g1)), b2 = std::sqrt(1.0 - 1.0 / (g2 * g2));
    const double bm = 0.5 * (b1 + b2), db = 0.5 * (b2 - b1);
    const double period = 1.0 / (feed * spec_.pelletRateMax);
    return kC * bm * bm * period / (2.0 * kPi * db);
}

double ExhaustModel::KnotSpacing(double feed) const {
    if (feed <= 0.0) return 0.0;
    return spec_.exhaustBeta * kC / (feed * spec_.pelletRateMax);
}

double ExhaustModel::FireballRadius(double power, double rho) {
    if (power <= 0.0 || rho <= 0.0) return 0.0;
    return std::clamp(1.15 * std::pow(power / rho, 0.2), 5.0, 30000.0);
}

HullWake EvaluateHullWake(double drag, double rho, double speed, double soundSpeed) {
    HullWake w;
    if (drag <= 0.0 || rho <= 1e-6 || speed <= 0.0) return w;
    const double mach = soundSpeed > 0.0 ? speed / soundSpeed : 0.0;
    if (mach < 3.0) return w;
    w.radius = std::pow(drag / rho, 0.25);
    const double q = 0.5 * rho * speed * speed * speed;  // stagnation energy flux [W/m^2]
    w.hot = std::clamp(std::log10(q / 1e8) / 3.0, 0.0, 1.0);
    w.trail = std::clamp(std::log10(drag / 1e6) / 3.0, 0.0, 1.0) * std::clamp((mach - 3.0) / 5.0, 0.0, 1.0);
    return w;
}

double ExhaustModel::ColumnRadius(double power) const {
    if (power <= 0.0) return 0.0;
    return std::clamp(std::pow(power * spec_.chargedFraction / spec_.absorptionColumn, 0.25), 5.0, 5000.0);
}

double ExhaustModel::DopplerFactor(double beta, double cosTheta) {
    const double g = 1.0 / std::sqrt(1.0 - beta * beta);
    return 1.0 / (g * (1.0 - beta * std::clamp(cosTheta, -1.0, 1.0)));
}

double ExhaustModel::BeamingLevel(double delta) {
    return std::clamp(0.7 + 0.12 * std::log10(std::pow(delta, 2.5)), 0.3, 1.0);
}

ExhaustFrame ExhaustModel::Evaluate(double fieldLevel, double beamLevel, double feed, double airDensity,
                                    double flicker, const AirPath& path, double jetPower) const {
    // Start-up happens inside the boron-nitride chambers and is seen in the pult slot.
    // Outside, a particle beam in vacuum is invisible (nothing to excite); only the
    // synchrotron light of decay electrons in the nozzle field shows faintly at the
    // mouths. In air the guide beam ionises a thin fluorescent thread.
    (void)fieldLevel;
    ExhaustFrame f;
    const double violet[3] = {0.78, 0.58, 1.0}, white[3] = {1.0, 0.95, 1.0};
    const double airGlow[3] = {0.95, 0.62, 0.55};  // N2 violet + Ne orange
    const double pulse = 0.9 + 0.2 * flicker;

    // Absorption in air: the beam ends where the air column along it reaches one
    // nuclear interaction length. Without a computed path, use the local density.
    const double absorb = path.absorbDist < 1e12 ? path.absorbDist
                          : airDensity > 1e-9   ? spec_.absorptionColumn / airDensity
                                                : 1e12;
    const bool inAir = absorb < 1e12;
    // The channel glows only where there is air: a ship in vacuum firing into an
    // atmosphere far below sees no glow near itself, only the ball where it ends.
    const bool shipInAir = inAir && airDensity > 1e-8;
    f.channel = shipInAir;

    if (feed <= 0.0) {
        if (beamLevel > 0.05) {
            f.cup.size = 1.0;
            Mix(f.cup.rgb, violet, white, 0.3);
            f.cup.level = 0.35 * beamLevel * pulse;
            if (shipInAir) {
                f.thread = 0.6 * beamLevel;
                f.threadLength = absorb;
            }
        }
        return f;
    }
    const double airT = std::clamp(std::log10(1.0 + airDensity * 1e4) / 4.0, 0.0, 1.0);  // 0 vacuum .. 1 dense

    // Chamber mouth: white-violet, flickering with the pellet pulses.
    f.cup.size = 2.0 + 4.0 * feed;
    Set(f.cup.rgb, white[0], white[1], white[2]);
    f.cup.level = (0.6 + 0.4 * feed) * pulse;

    // Neck and constrictions of the K-particle beam.
    for (int i = 0; i < 3; ++i) {
        Glow& n = f.nodes[i];
        n.dist = NodeDistance(i);
        n.size = (i == 0 ? 1.2 : 1.5 + 0.5 * i) * (0.6 + 0.4 * feed);
        Mix(n.rgb, violet, white, i == 0 ? 0.8 : 0.3);
        n.level = n.dist < absorb ? (0.5 + 0.5 * feed) * pulse * (i == 0 ? 1.0 : 0.7) : 0.0;
    }

    // Synchrotron light of decay electrons/positrons in the ~1000 T nozzle field
    // (gamma ~ 5 puts it near 430 nm): ends where the field ends.
    f.columnViolet = (0.45 + 0.55 * feed) * pulse;
    // Vacuum: afterglow of each pellet packet while it is still dense (e-fold
    // packetFade). Air: the ionised core of the channel (N2 / N2+ bands), fading as
    // the beam is absorbed. Both textures fade over the billboard: length = 3 e-folds.
    f.longGlowLength = shipInAir ? absorb : 3.0 * spec_.packetFade;
    // Air: N2 fluorescence ~ power left per metre of path, P/L (bright near the ground,
    // an aurora-like thread high up where L is hundreds of km).
    const double perMetre = jetPower / std::max(absorb, 1.0);
    f.longGlow = shipInAir ? std::clamp(std::log10(perMetre / 1e6) / 5.0, 0.1, 1.0) : 0.6 * feed;

    // Far field: internal shocks along the ship axis.
    f.firstKnot = FirstKnot(feed);
    f.knotSpacing = KnotSpacing(feed);
    for (int k = 0; k < spec_.maxKnots; ++k) {
        const double d = f.firstKnot + k * f.knotSpacing;
        if (d > spec_.knotRange || d > absorb) break;
        Glow g;
        g.dist = d;
        g.size = 40.0 + 80.0 * feed;
        Mix(g.rgb, violet, white, 0.5);
        g.level = feed * std::exp(-k / 5.0) * (0.85 + 0.3 * flicker);
        f.knots.push_back(g);
    }

    // Pellet packets: each burst leaves as a plasma shell at beta*c, one every
    // beta*c/f. It shines while dense and fades as it expands (e-fold packetFade), so
    // only the newest one is seen near the stern; they light up again far aft, in the
    // internal-shock knots above. Phase shifts frame to frame (faster than the frame rate).
    const double phase = flicker * f.knotSpacing;
    for (int k = 0; k < spec_.maxPackets; ++k) {
        const double d = (k + 1) * f.knotSpacing - phase;
        if (d > absorb) break;
        const double level = (0.4 + 0.6 * feed) * std::exp(-d / spec_.packetFade) * pulse;
        if (level < 0.05) break;
        Glow g;
        g.dist = d;
        g.size = std::min(30.0, std::max(4.0, 0.002 * d)) * (0.6 + 0.4 * feed);
        Mix(g.rgb, violet, white, 0.6);
        g.level = level;
        f.packets.push_back(g);
    }

    // Gamma halo: buried in the white-hot column in dense air, spread thin and faint in
    // near vacuum; seen best in the upper atmosphere (rho ~ 1e-7 .. 1e-3 kg/m^3).
    if (airDensity > 0.0 && jetPower > 0.0) {
        const double lg = std::log10(airDensity) + 5.5;
        const double window = std::exp(-lg * lg / (2.0 * 1.5 * 1.5));
        f.halo = std::clamp(std::log10(jetPower / 1e12) / 5.0, 0.0, 1.0) * window * pulse;
        f.haloRadius = std::clamp(spec_.gammaColumn / airDensity, 2000.0, 32000.0);
    }

    // Air: the beam deposits its power along the whole path, most at the stern
    // (~exp(-s/L)): a white-hot plasma column from the stern, whose blast throws out a
    // wake of hot air and, where it meets the ground, ejecta. A ship in vacuum firing
    // into an atmosphere far below has it out of reach: not drawn.
    if (shipInAir) {
        const double rho = std::max(path.depositDensity, 1e-9);
        const double range = path.ground ? spec_.absorptionColumn / rho : absorb;
        f.fireball.dist = range;
        f.fireball.size = ColumnRadius(jetPower);
        f.columnEnd = path.ground ? absorb : range;
        const double hot[3] = {1.0, 0.82, 0.62};  // white-orange air plasma
        Mix(f.fireball.rgb, hot, airGlow, 0.3 * airT);
        f.fireball.level = (0.7 + 0.3 * feed) * pulse;
        f.smoke = std::clamp(std::log10(rho / 1e-4) / 4.0, 0.0, 1.0) * (0.5 + 0.5 * feed);
        f.dust = path.ground ? 1.0 : 0.0;
    }

    // Light cast on the stern, hull and ground.
    Mix(f.lightRgb, violet, airGlow, airT * 0.5);
    f.light = (0.5 + 0.5 * feed) * pulse;
    if (airT > 0.0) Mix(f.nodes[2].rgb, f.nodes[2].rgb, airGlow, airT * 0.6);
    return f;
}

}  // namespace tantra
