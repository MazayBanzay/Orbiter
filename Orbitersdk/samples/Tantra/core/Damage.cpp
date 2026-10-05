#include "Damage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Aero.h"
#include "Impact.h"

namespace tantra::damage {

namespace {
constexpr double kSigma = 5.670e-8, kEps = 0.85, kSG = 1.7415e-4;  // Stefan-Boltzmann, emissivity, Sutton-Graves
constexpr double kNoseRn = 3.0;                                    // iridium nose tip radius [m] (T8: blunted)

// Zone: effective nose radius, exposure law, thermal lag, material limit.
struct ZoneSpec { double rn; double limit; double tau; };
const ZoneSpec kZone[kZoneCount] = {
    {kNoseRn, 2400.0, 25.0},  // nose: crystalline iridium shield (melts 2719 K)
    {12.0, 2200.0, 40.0},     // belly: flat bottom, boron-zirconium ceramic, stagnation-line equivalent
    {0.45, 2100.0, 15.0},     // wing leading edges (R 0.45 m, sweep 41 deg; Li heat pipes keep them ~1100 K)
    {3.0, 2000.0, 20.0},      // dorsal fin (leeward at entry; windward only in sideslip or upside down)
    {10.9, 2000.0, 40.0},     // stern: the nacelle cluster (flying stern first)
    {1.0, 1300.0, 10.0},      // gear legs and pads in the flow
    {1.0, 1300.0, 10.0},      // pods and their door arms in the flow
};

// Part ratings.
constexpr double kCrestArea = 240.0, kCrestRating = 14.0e6;  // per wing, normal force [N]
constexpr double kFinArea = 726.0, kFinRating = 12.0e6;
constexpr double kPodQ = 60.0e3, kGearQ = 40.0e3, kDoorQ = 3.0e3;  // dynamic pressure the part takes [Pa]
constexpr double kHullG = 6.0, kHullGBreak = 9.0;
constexpr double kTdBelly = 1.0, kTdBellyBreak = 5.0;  // [m/s]; the legs' limits come with Ground (ankle struts)

const char* kRu[kPartCount] = {"щит носа", "днище", "крыло левое", "крыло правое", "перо",
                               "гондола 1", "гондола 2", "гондола 3", "гондола 4",
                               "лопасть левая", "лопасть правая", "кормовая нога 1", "кормовая нога 2",
                               "кормовая нога 3", "кормовая нога 4", "передняя опора", "створки ангара", "створки отсеков",
                               "маршевая чаша", "корпус",
                               "жилые отсеки", "ангар", "блок ловушек", "корма",
                               "ВЭУ", "накопитель поля", "планетарная установка", "гироскопы", "баки аргона",
                               "вездеходы", "шлюз и лифт", "жизнеобеспечение", "гаситель инерции", "пульты мостика"};
const char* kEn[kPartCount] = {"nose shield", "belly", "port wing", "starboard wing", "fin",
                               "pod 1", "pod 2", "pod 3", "pod 4",
                               "port blade leg", "starboard blade leg", "stern leg 1", "stern leg 2",
                               "stern leg 3", "stern leg 4", "kangaroo leg", "hangar doors", "bay doors", "marching cup", "hull",
                               "living module", "hangar", "trap block", "stern",
                               "power plant", "field store", "planetary installation", "gyroscopes", "argon tanks",
                               "rovers", "airlock and lift", "life support", "inertia absorber", "bridge consoles"};
}  // namespace

Model::Model() { Repair(); }

void Model::Repair() {
    for (double& i : integrity_) i = 1.0;
    for (double& c : crushed_) c = 0.0;
    belly_ = 0.0;
    impact_ = ImpactReport();
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
    expo[kZoneCrestEdge] = x.crests * (sternFirst ? 0.1 : 0.71);  // cos^1.2 of the 41 deg sweep
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
                case kZoneStern: Hurt(kHull, 0.5 * hurt, enabled); Hurt(kMarchCup, x.sternCups > 0.1 ? 4.0 * hurt : hurt, enabled); break;
                case kZoneGear: for (int p = kLegPort; p <= kKangLeg; ++p) Hurt(p, hurt, enabled); break;
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
        for (int p = kLegPort; p <= kKangLeg; ++p) Hurt(p, (q_ / kGearQ - 1.0) * 0.3 * dt, enabled);
    if (x.hangar > 0.1 && q_ > kDoorQ) Hurt(kHangarDoors, q_ > 3.0 * kDoorQ ? 1.0 : 0.3 * dt, enabled);
    if (x.bays > 0.1 && q_ > kDoorQ) Hurt(kBayDoors, q_ > 3.0 * kDoorQ ? 1.0 : 0.3 * dt, enabled);
    // --- g-load on the hull
    if (f.gLoad > kHullGBreak) Hurt(kHull, 1.0, enabled);
    else if (f.gLoad > kHullG) Hurt(kHull, (f.gLoad - kHullG) * 0.1 * dt, enabled);
    // --- touchdown impact and leg overloads
    if (g.touchdown) {
        double v = g.vDown;  // what is left for the hull after the legs
        if (g.gearDown && (g.legsInPlay || g.impactMode < 0)) {   // the legs take it only when the ship lands on them
            if (g.jointModel) {}   // the cells, ribs, ankles and joints take it (core/Foot)
            else if (v > g.vBreak) for (int p = kLegPort; p <= kKangLeg; ++p) Hurt(p, 1.0, enabled);
            else if (v > g.vSoft) for (int p = kLegPort; p <= kKangLeg; ++p) Hurt(p, (v - g.vSoft) / (g.vBreak - g.vSoft), enabled);
            // the struts absorb the energy of a vBreak touchdown at most (stroke bottomed out); the rest reaches the hull
            v = v > g.vBreak ? std::sqrt(v * v - g.vBreak * g.vBreak) : 0.0;
        }
        if (g.impactMode >= 0) {
            if (v > kTdBelly) HullImpact(g, v, enabled);      // the hull itself: zones, equipment, people (core/Impact)
        } else if (v > kTdBellyBreak) Hurt(kHull, 1.0, enabled);
        else if (v > kTdBelly) { Hurt(kBelly, 0.2 * (v - kTdBelly), enabled); Hurt(kHull, 0.1 * (v - kTdBelly), enabled); }
    }
    if (g.contact && g.gearDown && !g.jointModel)
        for (int i = 0; i < 7; ++i) {
            const double r = g.legRatio[i];
            if (r > 1.5) Hurt(kLegPort + i, 1.0, enabled);
            else if (r > 1.0) Hurt(kLegPort + i, (r - 1.0) * 0.5 * dt, enabled);
        }
}

// The hull hits the ground with what the legs left: the zones crush in turn (or the ship goes into the ground), the
// equipment meets its g (along the axis what the drive asks, across its mounts), the trap cassettes hold their anamezon
// or not. Damage accumulates: an earlier crush stays crushed.
void Model::HullImpact(const Ground& g, double v, bool enabled) {
    namespace im = tantra::impact;
    im::Input in;
    in.mode = static_cast<im::Mode>(g.impactMode);
    in.mass = g.mass;
    in.g = g.localG;
    in.v = v;
    in.soil = g.soil;
    for (int z = 0; z < im::kZoneCount; ++z) in.crushed[z] = crushed_[z];
    in.belly = belly_;
    const im::Result r = im::Solve(in);
    const bool axial = in.mode == im::kNoseFirst || in.mode == im::kSternFirst;
    ImpactReport rep;
    rep.happened = true;
    rep.mode = g.impactMode;
    rep.v = v;
    rep.peakG = r.peakG;
    rep.penetration = r.penetration;
    rep.duration = r.duration;
    rep.axial = axial;
    if (enabled) {
        // the zones: their crushed share is their damage (nose and belly also take heat elsewhere)
        static const int kZonePart[im::kZoneCount] = {kSternZone, kTrapBlock, kHangar, kLiving, kNose};
        for (int z = 0; z < im::kZoneCount; ++z) {
            const double len = im::kZones[z].s1 - im::kZones[z].s0;
            const double before = std::min(1.0, crushed_[z] / len), after = im::Share(r, z);
            crushed_[z] = r.crushed[z];
            Hurt(kZonePart[z], after - before, enabled);
            if (z == im::kStern) Hurt(kMarchCup, after - before, enabled);
            if (z == im::kHangar) Hurt(kHangarDoors, after - before, enabled);
        }
        const double b0 = std::min(1.0, belly_ / im::kBellyDepth), b1 = std::min(1.0, r.belly / im::kBellyDepth);
        belly_ = r.belly;
        Hurt(kBelly, 0.99 * (b1 - b0), enabled);   // pressed in all the way the compartments are open, the ship not gone
    }
    for (int z = 0; z < im::kZoneCount; ++z) rep.zoneGrade[z] = im::Grade(im::Share(r, z));
    rep.bellyGrade = im::Grade(std::min(1.0, r.belly / im::kBellyDepth));
    // the equipment against its g
    static const int kEquipPart[im::kEquipCount] = {kTrapBlock, kEqVeu, kEqStore, kEqPlant, kEqGyros, kEqArgon, kEqRovers,
                                                    kEqAirlock, kEqLife, kEqAbsorber, kEqBridge};
    static const double kStateIntegrity[4] = {1.0, 0.85, 0.45, 0.0};
    for (int e = 0; e < im::kEquipCount; ++e) {
        const int st = im::EquipState(e, r.peakG, axial, im::Share(r, im::kEquip[e].zone));
        rep.equipState[e] = st;
        if (e == im::kTrapCassettes) continue;            // the trap block: its own share above, the breach below
        const int part = kEquipPart[e];
        if (integrity_[part] > kStateIntegrity[st]) Hurt(part, integrity_[part] - kStateIntegrity[st], enabled);
    }
    rep.absorberAlive = rep.equipState[im::kAbsorber] < 3 && integrity_[kEqAbsorber] > 0.0;
    // the anamezon: over the field's hold, or the cassettes crushed - the end of the ship and everything around it
    const double hold = axial ? im::kEquip[im::kTrapCassettes].axialG : im::kEquip[im::kTrapCassettes].lateralG;
    if (g.trapsLoaded && (r.peakG > hold || im::Share(r, im::kTraps) > 0.85)) {
        rep.trapBreach = true;
        Hurt(kHull, 1.0, enabled);
    }
    impact_ = rep;
    ++impactSerial_;
}

bool Model::TakeImpact(ImpactReport* out) {
    if (!impact_.happened) return false;
    *out = impact_;
    impact_.happened = false;
    return true;
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

void Model::SaveCrush(char* buf, int size) const {
    std::snprintf(buf, size, "%.2f %.2f %.2f %.2f %.2f %.2f", crushed_[0], crushed_[1], crushed_[2], crushed_[3], crushed_[4], belly_);
}

void Model::LoadCrush(const char* s) {
    double c[6] = {};
    if (std::sscanf(s, "%lf %lf %lf %lf %lf %lf", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]) == 6) {
        for (int i = 0; i < 5; ++i) crushed_[i] = std::max(0.0, c[i]);
        belly_ = std::max(0.0, c[5]);
    }
}

}  // namespace tantra::damage
