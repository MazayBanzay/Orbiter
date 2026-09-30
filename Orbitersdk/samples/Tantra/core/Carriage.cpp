#include "Carriage.h"

#include <algorithm>
#include <cmath>

namespace tantra {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPhaseTime = 20.0;  // s per phase of the erection (2 min in all)
constexpr double kGearTime = 10.0;   // s to deploy or stow the gear

double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
double Ease(double t) { t = Clamp01(t); return t * t * (3.0 - 2.0 * t); }
double Lerp(double a, double b, double t) { return a + (b - a) * t; }
double StepTo(double v, double target, double maxStep) {
    return v < target ? std::min(target, v + maxStep) : std::max(target, v - maxStep);
}
Vec3 V(double x, double y, double z) { return Vec3{x, y, z}; }
Vec3 Add(Vec3 a, Vec3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
Vec3 Mul(Vec3 a, double k) { return V(a.x * k, a.y * k, a.z * k); }
Vec3 Mix(Vec3 a, Vec3 b, double t) { return Add(Mul(a, 1.0 - t), Mul(b, t)); }
Vec3 Sub(Vec3 a, Vec3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
Vec3 Cross(Vec3 a, Vec3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
double Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Orders a triangle so that (p3 - p1) x (p2 - p1) points along `up` (Orbiter convention).
void Orient(Vec3 t[3], Vec3 up) {
    if (Dot(Cross(Sub(t[2], t[0]), Sub(t[1], t[0])), up) < 0.0) std::swap(t[1], t[2]);
}

}  // namespace

bool Carriage::CommandErect(bool up, bool landed) {
    if (!landed || gear_ < 1.0 || gearT_ < 1.0) return false;
    pT_ = up ? 6.0 : 0.0;
    return true;
}

bool Carriage::CommandLoadHeight(bool on, bool landed) {
    if (!landed || gear_ < 1.0 || gearT_ < 1.0 || p_ > kLoadP) return false;
    pT_ = on ? kLoadP : 0.0;
    return true;
}

bool Carriage::CommandGear(bool down, bool landed) {
    if (!landed && !down && p_ > 0.0 && p_ < 6.0) {
        pT_ = p_ < 3.0 ? 0.0 : 6.0;
        set_ = pT_ >= 6.0 ? FlightSet::Standing : FlightSet::Level;
        gearT_ = 0.0;
        return true;
    }
    if (p_ != pT_ || (p_ > 0.0 && p_ < 6.0)) return false;
    gearT_ = down ? 1.0 : 0.0;
    return true;
}

void Carriage::Update(double dt, double sCG) {
    sCG_ = sCG;
    const bool settling = p_ != pT_;  // carriage still moving: the gear waits for it
    if (!(settling && gearT_ < gear_)) gear_ = StepTo(gear_, gearT_, dt / kGearTime);
    if (gear_ >= 1.0 && (gearT_ >= 1.0 || settling)) p_ = StepTo(p_, pT_, dt / kPhaseTime);
    if (gear_ <= 0.0 && gearT_ <= 0.0) p_ = pT_ = (set_ == FlightSet::Standing ? 6.0 : 0.0);  // flight set
    BuildPose(sCG);
}

int Carriage::Phase() const {
    if (p_ >= 6.0) return 7;
    if (p_ <= 0.0) return 0;
    return 1 + static_cast<int>(p_);
}

void Carriage::BuildPose(double sCG) {
    const CarriageGeometry& g = geo_;
    CarriagePose& o = pose_;
    auto ph = [this](int k) { return Clamp01(p_ - k); };
    const double hRot = std::max(g.restAxisH, sCG + g.turnClear);
    const double hStand = sCG + (port_ ? 7.0 : g.standClear);
    const bool standingSet = p_ >= 6.0;
    const bool levelSet = p_ <= 0.0;
    const double hipS = std::min(g.trackS1, std::max(g.trackS0, sCG));  // the trunnion follows the CG on its track

    // --- erection ---
    o.tuck = Ease(ph(0));
    o.trunnionH = Lerp(g.restAxisH, hRot, Ease(ph(1)));
    if (ph(4) > 0.0) o.trunnionH = Lerp(hRot, hStand, Ease(ph(4)));
    if (standingSet) o.trunnionH = hStand;
    o.theta = 0.5 * kPi * Ease(ph(2));
    if (standingSet) o.theta = 0.5 * kPi;

    // --- carriage legs ---
    // Deploy from the pocket (t 0..1): hip out, leg swings down from along the hull while the hip runs
    // to the CG, pad unfolds; the shin length is set separately.
    auto deploy = [&](double t) {
        o.slideOut = Ease(t / 0.2);
        const double sw = Ease((t - 0.2) / 0.35);
        o.mastPitch = 0.5 * kPi * (1.0 - sw);
        o.hipS = Lerp(g.stowS, hipS, sw);
        o.padFold = 1.0 - Ease((t - 0.5) / 0.25);
    };
    const double gr = gear_;
    const double restLen = g.restAxisH - g.footH;
    o.legRest = o.legStand = 0.0;
    if (levelSet) {
        deploy(gr);
        o.mastLen = Lerp(g.legMin, restLen, Ease((gr - 0.7) / 0.3));
        o.legRest = Ease((gr - 0.3) / 0.7);
    } else if (standingSet) {       // carriage legs in their pockets, the stern legs carry the ship
        deploy(0.0);
        o.mastLen = g.legMin;
        o.legStand = port_ ? 0.0 : Ease(gr);
        o.tuck = 1.0;
    } else {                        // legs hang plumb under the trunnions while the ship turns
        o.slideOut = 1.0;
        o.mastPitch = o.theta;
        o.hipS = hipS;
        o.padFold = 0.0;
        o.mastLen = o.trunnionH - g.footH;
        const double c = ph(5);     // collect: shin in, pad folded, hip up the track, into the pocket
        o.mastLen = Lerp(o.mastLen, g.legMin, Ease(c / 0.4));
        o.padFold = Ease((c - 0.35) / 0.25);
        o.hipS = Lerp(hipS, g.stowS, Ease((c - 0.55) / 0.25));
        o.slideOut = 1.0 - Ease((c - 0.8) / 0.2);
        o.legRest = 1.0 - Ease(ph(1) * 4.0);   // lower stern legs home in the first quarter of the lift
        o.legStand = port_ ? 0.0 : Ease(ph(3));
    }

    // --- load sharing and ground contacts (ship frame, origin = CG = trunnion) ---
    o.columnShare = 1.0;
    if (levelSet) o.columnShare = gr >= 1.0 ? 0.95 : 1.0;
    else if (standingSet || p_ >= 5.0) o.columnShare = 0.0;
    else if (p_ < 1.0) o.columnShare = 0.95;
    else if (ph(4) > 0.8) o.columnShare = 1.0 - (ph(4) - 0.8) / 0.2;

    const double h = o.trunnionH, th = o.theta, X = g.columnX, fh = g.footHalf;
    const Vec3 down = V(0, -std::cos(th), -std::sin(th)), fwd = V(0, -std::sin(th), std::cos(th));
    // Pads of the carriage legs: fore and aft end of each pad, on the ground under the hips.
    auto pad = [&](double side, double end) { return Add(V(side * X, 0, 0), Add(Mul(down, h), Mul(fwd, end * fh))); };
    Vec3 col[4] = {pad(1, 1), pad(-1, 1), pad(1, -1), pad(-1, -1)};
    Vec3 rest[6] = {pad(1, 1), pad(-1, 1), V(g.legRestX, -h, g.legRestS - sCG), V(-g.legRestX, -h, g.legRestS - sCG),
                    pad(1, -1), pad(-1, -1)};
    Vec3 stand[4];
    for (int i = 0; i < 4; ++i) stand[i] = V(g.standFoot[i].x, g.standFoot[i].y, -h);  // coplanar with the pads while lowering
    Vec3 belly[3] = {V(0, g.bellyY, g.bellySNose - sCG), V(-g.bellyX, g.bellyY, g.bellySTail - sCG),
                     V(g.bellyX, g.bellyY, g.bellySTail - sCG)};
    Orient(col, V(0, std::cos(th), std::sin(th)));
    Orient(rest, V(0, 1, 0));
    Orient(stand, V(0, 0, 1));
    Orient(belly, V(0, 1, 0));

    auto use = [&](const Vec3* p, int n) {
        for (int i = 0; i < n; ++i) o.touch[i] = p[i];
        o.nTouch = n;
    };
    // Rest/pads and pads/stern-legs sets lie in the same ground plane at the switch, so switching between
    // them does not move the ship.
    if (levelSet) { if (gr > 0.5) use(rest, 6); else use(belly, 3); }
    else if (standingSet) { if (gr > 0.5) use(stand, 4); else use(belly, 3); }
    else if (p_ < 1.0) use(rest, 6);
    else if (p_ < 4.95) use(col, 4);
    else use(stand, 4);
    o.onColumns = !levelSet && !standingSet && p_ < 5.0;
}

Carriage::Loads Carriage::Statics(double weight, double cgError) const {
    Loads l;
    l.columnEach = pose_.columnShare * weight / 2.0;
    l.legs = (1.0 - pose_.columnShare) * weight;
    l.driveMoment = (p_ > 2.0 && p_ < 3.0) ? weight * cgError : 0.0;
    const double L = pose_.mastLen;
    l.columnFL2 = l.columnEach * L * L;
    return l;
}

void Carriage::Load(double p, double pT, double gear, double gearT, int set, int port) {
    p_ = std::min(6.0, std::max(0.0, p));
    pT_ = pT >= 3.0 ? 6.0 : 0.0;
    gear_ = Clamp01(gear);
    gearT_ = gearT > 0.5 ? 1.0 : 0.0;
    set_ = set ? FlightSet::Standing : FlightSet::Level;
    port_ = port != 0;
    BuildPose(sCG_);
}

void Carriage::Save(double& p, double& pT, double& gear, double& gearT, int& set, int& port) const {
    p = p_;
    pT = pT_;
    gear = gear_;
    gearT = gearT_;
    set = set_ == FlightSet::Standing ? 1 : 0;
    port = port_ ? 1 : 0;
}

}  // namespace tantra
