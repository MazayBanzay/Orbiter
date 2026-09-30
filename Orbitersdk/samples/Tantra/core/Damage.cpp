#include "Damage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Aero.h"

namespace tantra::damage {

namespace {
constexpr double kSigma = 5.670e-8, kEps = 0.85, kSG = 1.7415e-4;  // Stefan-Boltzmann, emissivity, Sutton-Graves
constexpr double kNoseRn = 1.8;                                    // iridium nose tip radius [m]

// Zone: effective nose radius, exposure law, thermal lag, material limit.
struct ZoneSpec { double rn; double limit; double tau; };
const ZoneSpec kZone[kZoneCount] = {
    {kNoseRn, 2400.0, 25.0},  // nose: crystalline iridium shield (melts 2719 K)
    {12.0, 2200.0, 40.0},     // belly: flat bottom, boron-zirconium ceramic, stagnation-line equivalent
    {0.8, 2100.0, 15.0},      // crest leading edges (blunted 0.9 m plates, sweep 28 deg)
    {3.0, 2000.0, 20.0},      // dorsal fin (leeward at entry; windward only in sideslip or upside down)
    {11.5, 2000.0, 40.0},     // stern face and well (flying stern first)
    {1.0, 1300.0, 10.0},      // gear legs and pads in the flow
    {1.0, 1300.0, 10.0},      // pods and their door arms in the flow
};

// Part ratings.
constexpr double kCrestArea = 212.0, kCrestRating = 12.0e6;  // per crest, normal force [N]
constexpr double kFinArea = 611.0, kFinRating = 10.0e6;
constexpr double kPodQ = 60.0e3, kGearQ = 40.0e3, kDoorQ = 3.0e3;  // dynamic pressure the part takes [Pa]
constexpr double kHullG = 6.0, kHullGBreak = 9.0;
constexpr double kTdLegs = 3.0, kTdLegsBreak = 6.0, kTdBelly = 1.0, kTdBellyBreak = 5.0;  // [m/s]

const char* kRu[kPartCount] = {"щит носа", "днище", "гребень левый", "гребень правый", "перо",
                               "гондола 1", "гондола 2", "гондола 3", "гондола 4",
                               "нога лафета левая", "нога лафета правая", "кормовая нога 1", "кормовая нога 2",
                               "кормовая нога 3", "кормовая нога 4", "створки ангара", "створки отсеков",
                               "чаши кормы", "корпус"};
const char* kEn[kPartCount] = {"nose shield", "belly", "port crest", "starboard crest", "fin",
                               "pod 1", "pod 2", "pod 3", "pod 4",
                               "port carriage leg", "starboard carriage leg", "stern leg 1", "stern leg 2",
                               "stern leg 3", "stern leg 4", "hangar doors", "bay doors", "stern cups", "hull"};
}  // namespace

Model::Model() { Repair(); }

void Model::Repair() {
    for (double& i : integrity_) i = 1.0;
    for (double& t : temp_) t = 250.0;
    nEvents_ = 0;
}

double Model::Limit(int zone) const { return kZone[zone].limit; }
const char* Model::NameRu(int part) { return kRu[part]; }
const char* Model::NameEn(int part) { return kEn[part]; }

void Model::Hurt(int part, double amount, bool enabled) {
    if (!enabled || amount <= 0.0 || integrity_[part] <= 0.0) return;
    integrity_[part] = std::max(0.0, integrity_[part] - amount);
    if (integrity_[part] <= 0.0 && nEvents_ < 16) {
        const bool fatal = part == kHull || part == kNose || part == kBelly;
        events_[nEvents_++] = {part, fatal, kRu[part], kEn[part]};
    }
}

void Model::Step(double dt, const Flight& f, const Exposure& x, const Ground& g, bool enabled) {
    if (dt <= 0.0) return;
    q_ = 0.5 * f.rho * f.v * f.v;
    const double sa = std::sin(f.aoa), ca = std::cos(f.aoa), sb = std::fabs(std::sin(f.beta));
    const bool sternFirst = ca < 0.0;
    // --- heating: exposure of each zone to the flow (1 = stagnation)
    double expo[kZoneCount];
    expo[kZoneNose] = sternFirst ? 0.05 : std::max(0.05, ca);
    expo[kZoneBelly] = std::max(0.05, sa);                        // windward flat bottom
    expo[kZoneCrestEdge] = x.crests * (sternFirst ? 0.1 : 0.86);  // cos^1.2 of the 28 deg sweep
    expo[kZoneFin] = x.fin * std::max(0.05, std::max(-sa, sb));   // leeward unless flying upside down / yawed
    expo[kZoneStern] = sternFirst ? std::max(0.05, -ca) : 0.05;
    expo[kZoneGear] = x.gear;
    expo[kZonePods] = x.pods;
    const double base = kSG * std::sqrt(std::max(0.0, f.rho)) * f.v * f.v * f.v;  // per sqrt(Rn)
    for (int z = 0; z < kZoneCount; ++z) {
        flux_[z] = base / std::sqrt(kZone[z].rn) * expo[z];
        const double tEq = std::pow(flux_[z] / (kEps * kSigma) + std::pow(f.ambientT, 4.0), 0.25);
        temp_[z] += (tEq - temp_[z]) * std::min(1.0, dt / kZone[z].tau);
        const double over = (temp_[z] - kZone[z].limit) / kZone[z].limit;
        if (over > 0.0) {  // 10 % over the limit burns a part through in ~20 s
            const double hurt = 0.5 * over * dt;
            switch (z) {
                case kZoneNose: Hurt(kNose, hurt, enabled); break;
                case kZoneBelly: Hurt(kBelly, hurt, enabled); break;
                case kZoneCrestEdge: Hurt(kCrestPort, hurt, enabled); Hurt(kCrestStbd, hurt, enabled); break;
                case kZoneFin: Hurt(kFin, hurt, enabled); break;
                case kZoneStern: Hurt(kHull, 0.5 * hurt, enabled); Hurt(kSternCups, x.sternCups > 0.1 ? 4.0 * hurt : hurt, enabled); break;
                case kZoneGear: for (int p = kLegPort; p <= kSternLeg3; ++p) Hurt(p, hurt, enabled); break;
                case kZonePods: for (int p = kPod0; p <= kPod3; ++p) Hurt(p, hurt, enabled); break;
            }
        }
    }
    // --- loads on the exposed parts
    double cl, cd;
    tantra::aero::Plate(f.aoa, f.mach, 1.33, &cl, &cd);
    const double crestN = q_ * kCrestArea * std::hypot(cl, cd) * x.crests;
    crestLoad_[0] = crestLoad_[1] = crestN / kCrestRating;
    for (int side = 0; side < 2; ++side) {
        const double r = crestLoad_[side];
        if (r > 1.5) Hurt(kCrestPort + side, 1.0, enabled);
        else if (r > 1.0) Hurt(kCrestPort + side, (r - 1.0) * 0.5 * dt, enabled);
    }
    tantra::aero::Plate(f.beta, f.mach, 0.9, &cl, &cd);
    finLoad_ = q_ * kFinArea * std::hypot(cl, cd) * x.fin / kFinRating;
    if (finLoad_ > 1.5) Hurt(kFin, 1.0, enabled);
    else if (finLoad_ > 1.0) Hurt(kFin, (finLoad_ - 1.0) * 0.5 * dt, enabled);
    if (x.pods > 0.1 && q_ > kPodQ)
        for (int p = kPod0; p <= kPod3; ++p) Hurt(p, q_ > 1.5 * kPodQ ? 1.0 : (q_ / kPodQ - 1.0) * dt, enabled);
    if (x.gear > 0.1 && q_ > kGearQ)
        for (int p = kLegPort; p <= kSternLeg3; ++p) Hurt(p, (q_ / kGearQ - 1.0) * 0.3 * dt, enabled);
    if (x.hangar > 0.1 && q_ > kDoorQ) Hurt(kHangarDoors, q_ > 3.0 * kDoorQ ? 1.0 : 0.3 * dt, enabled);
    if (x.bays > 0.1 && q_ > kDoorQ) Hurt(kBayDoors, q_ > 3.0 * kDoorQ ? 1.0 : 0.3 * dt, enabled);
    // --- g-load on the hull
    if (f.gLoad > kHullGBreak) Hurt(kHull, 1.0, enabled);
    else if (f.gLoad > kHullG) Hurt(kHull, (f.gLoad - kHullG) * 0.1 * dt, enabled);
    // --- touchdown impact and leg overloads
    if (g.touchdown) {
        double v = g.vDown;  // what is left for the hull after the legs
        if (g.gearDown) {
            if (v > kTdLegsBreak) for (int p = kLegPort; p <= kSternLeg3; ++p) Hurt(p, 1.0, enabled);
            else if (v > kTdLegs) for (int p = kLegPort; p <= kSternLeg3; ++p) Hurt(p, (v - kTdLegs) / (kTdLegsBreak - kTdLegs), enabled);
            // the legs absorb the energy of a 6 m/s touchdown at most; the rest reaches the hull
            v = v > kTdLegsBreak ? std::sqrt(v * v - kTdLegsBreak * kTdLegsBreak) : 0.0;
        }
        if (v > kTdBellyBreak) Hurt(kHull, 1.0, enabled);
        else if (v > kTdBelly) { Hurt(kBelly, 0.2 * (v - kTdBelly), enabled); Hurt(kHull, 0.1 * (v - kTdBelly), enabled); }
    }
    if (g.contact && g.gearDown)
        for (int i = 0; i < 6; ++i) {
            const double r = g.legRatio[i];
            if (r > 1.5) Hurt(kLegPort + i, 1.0, enabled);
            else if (r > 1.0) Hurt(kLegPort + i, (r - 1.0) * 0.5 * dt, enabled);
        }
}

int Model::TakeEvents(Event* out, int max) {
    const int n = std::min(max, nEvents_);
    for (int i = 0; i < n; ++i) out[i] = events_[i];
    nEvents_ = 0;
    return n;
}

void Model::Save(char* buf, int size) const {
    int k = 0;
    for (int i = 0; i < kPartCount && k < size - 8; ++i) k += std::snprintf(buf + k, size - k, i ? " %.3f" : "%.3f", integrity_[i]);
}

void Model::Load(const char* s) {
    char* end = nullptr;
    for (int i = 0; i < kPartCount; ++i) {
        const double v = std::strtod(s, &end);
        if (end == s) break;
        integrity_[i] = std::clamp(v, 0.0, 1.0);
        s = end;
    }
}

}  // namespace tantra::damage
