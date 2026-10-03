#include "Carriage.h"

#include <algorithm>
#include <cmath>

namespace tantra {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPhaseTime = 20.0;  // s per phase of the erection (2 min in all)
constexpr double kGearTime = 12.0;   // s to deploy or stow the gear

double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
double Ease(double t) { t = Clamp01(t); return t * t * (3.0 - 2.0 * t); }
double Lerp(double a, double b, double t) { return a + (b - a) * t; }
double StepTo(double v, double target, double maxStep) {
    return v < target ? std::min(target, v + maxStep) : std::max(target, v - maxStep);
}
Vec3 V(double x, double y, double z) { return Vec3{x, y, z}; }
Vec3 Add(Vec3 a, Vec3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
Vec3 Mul(Vec3 a, double k) { return V(a.x * k, a.y * k, a.z * k); }
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
    // CG tracking: on the blades alone (kangaroo up, stern legs not yet loaded) the trunnion drives feel the moment of
    // the CG off their axis and the carriages run along the rails until it is gone (to what the sensing resolves).
    // The estimate is kept while the ship stands on the blades; a new stand starts from the computed CG again.
    if (p_ >= 1.75 && p_ < 4.0) {
        const double want = geo_.cgError - geo_.cgSensor;
        cgTrim_ += (want - cgTrim_) * std::min(1.0, dt / geo_.cgTrackTime);
    } else if (p_ <= 1.0 || p_ >= 5.0) {
        cgTrim_ = 0.0;
    }
    if (gear_ <= 0.0 && gearT_ <= 0.0) p_ = pT_ = (set_ == FlightSet::Standing ? 6.0 : 0.0);  // flight set
    BuildPose(sCG);
}

int Carriage::Phase() const {
    if (p_ >= 6.0) return 7;
    if (p_ <= 0.0) return 0;
    return 1 + static_cast<int>(p_);
}

// Kangaroo leg: hip hipH above the ground, foot hub fwd metres ahead, knee aft with the knee offset g.kangKneeE off the
// hip-foot line; the shin runs out to reach. Rig states as in the mesh: hip 0 = along the belly aft .. 1 = kangHipMax
// down-forward; knee 0 = folded beside the thigh .. 1 = straight; foot rotation = shin lean / pi (keeps the foot level).
void Carriage::KangarooIK(const CarriageGeometry& g, double hipH, double fwd, CarriagePose& o) {
    const double footH = g.kangFootH;
    const double dy = std::max(1.0, hipH - footH);
    const double e = std::min(g.kangKneeE, g.kangThigh * 0.9);
    const double along = std::sqrt(g.kangThigh * g.kangThigh - e * e);
    const double aHF = std::atan2(fwd, dy);                   // hip->foot from straight down, forward positive
    const double aTh = aHF - std::atan2(e, along);            // thigh leans aft of that line
    const double sx = fwd - g.kangThigh * std::sin(aTh), sy = -dy + g.kangThigh * std::cos(aTh);   // shin vector (fwd, up)
    const double shin = std::sqrt(sx * sx + sy * sy);
    const double aSh = std::atan2(sx, -sy);
    const double knee = kPi - (aSh - aTh);                     // pi = straight
    o.kangHip = Clamp01((kPi / 2 + aTh) / (g.kangHipMaxDeg * kPi / 180.0));
    o.kangKnee = Clamp01(knee / kPi);
    o.kangExt = Clamp01((shin - 0.5 - g.kangShinMin) / (g.kangShinMax - g.kangShinMin));
    o.kangFoot = Clamp01(aSh / kPi);
}

void Carriage::BuildPose(double sCG) {
    const CarriageGeometry& g = geo_;
    CarriagePose& o = pose_;
    auto ph = [this](int k) { return Clamp01(p_ - k); };
    // CG height standing (ground at s -standClear). An empty ship (CG far forward) may be too tall for the blades:
    // then the lift stops at their reach and the stern legs, with 2.5 m to spare, take the ship that much lower.
    const double hNominal = sCG + (port_ ? 7.0 : g.standClear);
    const double hStand = std::min(hNominal, g.legMax + g.footH - 0.2);
    const bool standingSet = p_ >= 6.0;
    const bool levelSet = p_ <= 0.0;
    // trunnions under the CG for the turn: under the computed CG (off by cgError), then where the drives find it
    const double hipCG = std::min(g.trackS1, std::max(g.trackS0, sCG - g.cgError + cgTrim_));
    const double restLen = g.restAxisH - g.footH;                                     // blade hip -> ankle, lying

    // --- erection: heights and pitch ---
    // the crests and the fin stay as the crew set them: standing, the wings in line clear the stern legs by 30 deg
    // and the fin is the stabiliser of the nose-first climb (they fold for sub-light and a stern-first descent only)
    o.tuck = 0.0;
    o.trunnionH = Lerp(g.restAxisH, hStand, Ease((ph(0) - 0.1) / 0.9));
    if (standingSet) o.trunnionH = hStand;
    o.theta = 0.5 * kPi * Ease(ph(2));
    if (standingSet) o.theta = 0.5 * kPi;
    o.standDrop = hNominal - hStand;
    o.tripod = false;

    // --- blade legs: deploy from the pocket (t 0..1): hip out, blade swings down from along the hull while the hip
    // runs aft to the parked station, feet open; the length is set separately ---
    auto deploy = [&](double t) {
        o.slideOut = Ease(t / 0.2);
        const double sw = Ease((t - 0.2) / 0.35);
        o.mastPitch = 0.5 * kPi * (1.0 - sw);
        o.hipS = Lerp(g.stowS, g.trackS0, sw);
        o.footFold = 1.0 - Ease((t - 0.5) / 0.3);
    };
    // --- kangaroo: deploy (t 0..1): door, then the leg to its lying pose ---
    const double hipH0 = g.restAxisH + g.kangHipY;
    // door; the knee unfolds down and aft through the open pocket while the thigh still lies along the belly (the
    // shin must not sweep forward under the hangar doors); then the hip swings the leg down-forward, the shin runs
    // out and the foot opens. Folding is the same path backwards.
    auto kangDeploy = [&](double t, double hipH, double fwd = -1.0) {
        o.kangDoor = Ease(t / 0.15);
        CarriagePose ik;
        KangarooIK(g, hipH, fwd > 0.0 ? fwd : g.kangFootFwd, ik);
        // knee only to the vertical while the thigh lies along the belly (straight, the shin would run into the hull
        // aft of the pocket); the rest of the bend comes with the hip swing
        const double kn1 = 0.5 * Ease((t - 0.15) / 0.25), sw = Ease((t - 0.4) / 0.45);
        o.kangKnee = kn1 + (ik.kangKnee - 0.5) * sw;
        o.kangHip = ik.kangHip * sw;
        o.kangFoot = ik.kangFoot * sw;
        o.kangExt = ik.kangExt * Ease((t - 0.6) / 0.4);
        o.kangFold = 1.0 - Ease((t - 0.7) / 0.3);
    };
    auto kangStowed = [&]() { o.kangDoor = o.kangHip = o.kangKnee = o.kangExt = o.kangFoot = 0.0; o.kangFold = 1.0; };
    const double gr = gear_;
    o.legStand = 0.0;
    o.legRail = o.legFold = 1.0;
    if (levelSet) {
        deploy(gr);
        o.mastLen = Lerp(g.legMin, restLen, Ease((gr - 0.7) / 0.3));
        kangDeploy(gr, hipH0);
        o.tripod = gr >= 1.0;
    } else if (standingSet) {       // blades in their pockets, kangaroo home, the stern legs carry the ship
        deploy(0.0);
        o.mastLen = g.legMin;
        kangStowed();
        o.legStand = port_ ? 0.0 : Ease(gr);
        o.legRail = 1.0 - Ease(gr * 2.0);
        o.legFold = 1.0 - Ease(gr * 2.0 - 1.0);
    } else {
        o.slideOut = 1.0;
        o.footFold = 0.0;
        // trunnions: parked while lifting, then forward under the CG (1-2), then to the stow station (5-6)
        o.hipS = Lerp(g.trackS0, hipCG, Ease(ph(1) / 0.5));
        o.mastPitch = o.theta;
        // the hip may sit off the CG: its height follows the ship's pitch about the CG
        o.mastLen = o.trunnionH + (o.hipS - sCG) * std::sin(o.theta) - g.footH;
        // kangaroo: foot fixed on the ground ahead of its hip while lifting, folds once the trunnions are under the CG
        // the hull rides along the trunnions (the feet stay planted): the kangaroo foot keeps its place on the ground,
        // which is further ahead of its hip by the distance the hull has moved
        const double hipH = o.trunnionH + g.kangHipY;
        const double fwd = g.kangFootFwd + (o.hipS - g.trackS0);
        if (ph(1) < 0.5) {
            kangDeploy(1.0, hipH, fwd);
            o.tripod = true;
        } else {
            const double f = Ease((ph(1) - 0.5) / 0.5);          // fold = deploy backwards
            kangDeploy(1.0 - f, hipH, fwd);
        }
        // stern legs out (3-4): swing + run down, feet to the ankle and open
        o.legStand = port_ ? 0.0 : Ease(ph(3));
        o.legRail = 1.0 - Ease(ph(3) * 2.0);
        o.legFold = 1.0 - Ease(ph(3) * 2.0 - 1.0);
        // collect the blades (5-6): feet fold, stages in, hip to the stow station, into the pocket
        const double c = ph(5);
        o.footFold = Ease(c / 0.3);
        o.mastLen = Lerp(o.mastLen, g.legMin, Ease((c - 0.2) / 0.4));
        o.hipS = Lerp(o.hipS, g.stowS, Ease((c - 0.55) / 0.3));
        o.slideOut = 1.0 - Ease((c - 0.85) / 0.15);
    }

    // --- load sharing: tripod while the trunnions are parked, blades alone on the turn, stern legs standing ---
    const double footS = g.kangHipS + g.kangFootFwd;
    const double kangFull = Clamp01((sCG - g.trackS0) / (footS - g.trackS0));
    o.kangShare = 0.0;
    if (o.tripod) o.kangShare = kangFull;
    else if (!levelSet && !standingSet && p_ >= 1.0 && ph(1) < 0.5)
        o.kangShare = Clamp01((sCG - o.hipS) / (footS - o.hipS));
    o.columnShare = 1.0;
    if (standingSet || p_ >= 5.0) o.columnShare = 0.0;
    if (!levelSet && !standingSet && ph(4) > 0.0) o.columnShare = 1.0 - Ease(ph(4));

    // --- ground contacts (ship frame, origin = CG; trunnionH = CG height) ---
    const double h = o.trunnionH, th = o.theta, X = g.columnX;
    const Vec3 down = V(0, -std::cos(th), -std::sin(th)), fwd = V(0, -std::sin(th), std::cos(th));
    const Vec3 up = V(0, std::cos(th), std::sin(th));
    // blade feet: fore and aft rim point of each umbrella, on the ground under the hips
    auto pad = [&](double side, double end) {
        const Vec3 hip = V(side * X, 0, o.hipS - sCG);
        const double drop = h + Dot(hip, up);
        return Add(hip, Add(Mul(down, drop), Mul(fwd, end * g.footR)));
    };
    Vec3 col[4] = {pad(1, 1), pad(-1, 1), pad(1, -1), pad(-1, -1)};
    // kangaroo foot: fore and aft rim point ahead of its hip
    const double kz = g.kangHipS + g.kangFootFwd + (o.tripod || (p_ >= 1.0 && ph(1) < 0.5) ? (o.hipS - g.trackS0) : 0.0) - sCG;
    Vec3 kang[2] = {V(1.0, -h, kz + g.kangFootR), V(-1.0, -h, kz - g.kangFootR)};
    Vec3 rest[6] = {pad(1, 1), pad(-1, 1), kang[0], kang[1], pad(1, -1), pad(-1, -1)};
    Vec3 stand[4];
    for (int i = 0; i < 4; ++i) stand[i] = V(g.standFoot[i].x, g.standFoot[i].y, -h);  // coplanar with the feet while standing
    Vec3 belly[3] = {V(0, g.bellyY, g.bellySNose - sCG), V(-g.bellyX, g.bellyY, g.bellySTail - sCG),
                     V(g.bellyX, g.bellyY, g.bellySTail - sCG)};
    Orient(col, up);
    Orient(rest, V(0, 1, 0));
    Orient(stand, V(0, 0, 1));
    Orient(belly, V(0, 1, 0));

    auto use = [&](const Vec3* p, int n) {
        for (int i = 0; i < n; ++i) o.touch[i] = p[i];
        o.nTouch = n;
    };
    // Every set lies in the same ground plane at its switch, so switching does not move the ship.
    if (levelSet) { if (gr > 0.5) use(rest, 6); else use(belly, 3); }
    else if (standingSet) { if (gr > 0.5) use(stand, 4); else use(belly, 3); }
    else if (o.tripod) use(rest, 6);
    else if (p_ < 4.95) use(col, 4);
    else use(stand, 4);
    o.onColumns = !levelSet && !standingSet && !o.tripod && p_ < 5.0;
}

Carriage::Loads Carriage::Statics(double weight, double cgError) const {
    Loads l;
    const double onFront = pose_.columnShare * weight;
    l.kangaroo = onFront * pose_.kangShare;
    l.columnEach = (onFront - l.kangaroo) / 2.0;
    l.legs = (1.0 - pose_.columnShare) * weight;
    (void)cgError;
    l.driveMoment = (p_ > 1.75 && p_ < 4.0) ? weight * CgResidual() : 0.0;   // what the trunnion drives hold
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
