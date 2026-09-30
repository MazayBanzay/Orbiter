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

    // --- the gear itself (flight deploy/stow) ---
    const double gr = gear_;
    double lid = 0, slide = 0, mastFrac = 0, legRest = 0, legStand = 0;
    if (levelSet) {  // columns out and down to the resting length, lower legs across
        lid = Ease(gr / 0.25);
        slide = Ease((gr - 0.2) / 0.3);
        mastFrac = Ease((gr - 0.45) / 0.55);
        legRest = Ease((gr - 0.3) / 0.7);
    } else if (standingSet) {
        legStand = port_ ? 0.0 : Ease(gr);
    }

    // --- erection ---
    o.tuck = Ease(ph(0));
    o.trunnionH = Lerp(g.restAxisH, hRot, Ease(ph(1)));
    if (ph(4) > 0.0) o.trunnionH = Lerp(hRot, hStand, Ease(ph(4)));
    if (standingSet) o.trunnionH = hStand;
    o.theta = 0.5 * kPi * Ease(ph(2));
    if (standingSet) o.theta = 0.5 * kPi;

    const double restLen = g.restAxisH - g.footT;
    if (levelSet) {
        o.lid = lid;
        o.slideOut = slide;
        o.mastLen = Lerp(g.mastMin, restLen, mastFrac);
        o.mastPitch = 0.0;
        o.legRest = legRest;
        o.legStand = 0.0;
    } else if (standingSet) {
        o.lid = 0.0;
        o.slideOut = 0.0;
        o.mastLen = g.mastMin;
        o.mastPitch = 0.0;
        o.legRest = 0.0;
        o.legStand = legStand;
        o.tuck = 1.0;
    } else {
        const double c = ph(5);  // collect: reel in, swing the mast home, carriage in, lid shut
        o.lid = 1.0 - Ease((c - 0.9) / 0.1);
        o.slideOut = 1.0 - Ease((c - 0.75) / 0.15);
        o.mastLen = Lerp(o.trunnionH - g.footT, g.mastMin, Ease(c / 0.5));
        o.mastPitch = o.theta * (1.0 - Ease((c - 0.5) / 0.25));
        o.legRest = 1.0 - Ease(ph(1) * 4.0);  // lower legs home in the first quarter of the lift
        o.legStand = port_ ? 0.0 : Ease(ph(3));
    }

    // --- load sharing and touchdown points (ship frame, origin = CG = trunnion) ---
    o.columnShare = 1.0;
    if (levelSet) o.columnShare = gr >= 1.0 ? 0.95 : 1.0;
    else if (standingSet || p_ >= 5.0) o.columnShare = 0.0;
    else if (p_ < 1.0) o.columnShare = 0.95;
    else if (ph(4) > 0.8) o.columnShare = 1.0 - (ph(4) - 0.8) / 0.2;

    const double h = o.trunnionH, th = o.theta;
    const Vec3 down = V(0, -std::cos(th), -std::sin(th)), fwd = V(0, -std::sin(th), std::cos(th));
    const Vec3 up = V(0, std::cos(th), std::sin(th));
    Vec3 col[3] = {Add(Mul(down, h), Mul(fwd, g.footHalf)),
                   Add(V(-g.columnX, 0, 0), Add(Mul(down, h), Mul(fwd, -g.footHalf))),
                   Add(V(g.columnX, 0, 0), Add(Mul(down, h), Mul(fwd, -g.footHalf)))};
    Vec3 rest[3] = {V(0, -h, g.footHalf), V(-g.legRestX, -h, g.legRestS - sCG), V(g.legRestX, -h, g.legRestS - sCG)};
    Vec3 stand[3];
    for (int i = 0; i < 3; ++i) {
        const double a = kPi / 2 + i * 2 * kPi / 3;
        stand[i] = V(g.standR * std::cos(a), g.standR * std::sin(a), -h);  // coplanar with the columns while lowering
    }
    Vec3 belly[3] = {V(0, g.bellyY, g.bellySNose - sCG), V(-g.bellyX, g.bellyY, g.bellySTail - sCG),
                     V(g.bellyX, g.bellyY, g.bellySTail - sCG)};
    Orient(col, up);
    Orient(rest, V(0, 1, 0));
    Orient(stand, V(0, 0, 1));
    Orient(belly, V(0, 1, 0));

    o.onColumns = false;
    for (int i = 0; i < 3; ++i) {
        Vec3 p;
        if (levelSet) p = gr > 0.5 ? rest[i] : belly[i];
        else if (standingSet) p = gr > 0.5 ? stand[i] : belly[i];
        // Rest/columns and columns/legs triangles lie in the same ground plane at the switch,
        // so switching between them does not move the ship.
        else if (p_ < 1.0) p = rest[i];
        else if (p_ < 4.95) p = col[i];
        else p = stand[i];
        o.touch[i] = p;
    }
    o.onColumns = !levelSet && !standingSet && p_ < 5.0;
}

Carriage::Loads Carriage::Statics(double weight, double cgError) const {
    Loads l;
    l.columnEach = pose_.columnShare * weight / 2.0;
    l.legs = (1.0 - pose_.columnShare) * weight;
    l.driveMoment = (p_ > 2.0 && p_ < 3.0) ? weight * cgError : 0.0;
    const double L = pose_.trunnionH;
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
