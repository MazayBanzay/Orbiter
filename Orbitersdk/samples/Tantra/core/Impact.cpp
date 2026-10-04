#include "Impact.h"

#include <algorithm>
#include <cmath>

namespace tantra::impact {

namespace {
constexpr double kPi = 3.14159265358979;
}

// stations from the stern; forces: the mean load-bearing capacity while the zone crushes. The trap cassettes hold their
// anamezon's inertia at 190 g (9 370 t x 190 g ~ 17.5 GN each): a rigid core the rest crushes around.
const ZoneSpec kZones[kZoneCount] = {
    {0.0, 21.0, 7.0e9},      // stern: planetary installation, nacelles
    {21.0, 88.0, 70.0e9},    // trap cassettes (4)
    {88.0, 121.0, 1.8e9},    // hangar and the insert
    {121.0, 145.0, 1.5e9},   // living module and the bridge
    {145.0, 178.0, 6.0e9},   // the iridium nose shield
};

const EquipSpec kEquip[kEquipCount] = {
    {kTraps, 190.0, 2000.0, "кассеты ловушек", "trap cassettes"},
    {kStern, 200.0, 38.0, "ВЭУ (реактор)", "power plant (reactor)"},
    {kStern, 200.0, 38.0, "накопитель поля", "field store"},
    {kStern, 200.0, 20.0, "планетарная установка", "planetary installation"},
    {kTraps, 200.0, 10.0, "гироскопы", "gyroscopes"},
    {kHangar, 200.0, 20.0, "баки аргона и насосы", "argon tanks and pumps"},
    {kHangar, 200.0, 10.0, "вездеходы в ангаре", "rovers in the hangar"},
    {kHangar, 200.0, 20.0, "шлюз и лифт", "airlock and lift"},
    {kLiving, 200.0, 15.0, "жизнеобеспечение", "life support"},
    {kLiving, 200.0, 25.0, "гаситель инерции", "inertia absorber"},
    {kLiving, 200.0, 30.0, "пульты мостика", "bridge consoles"},
};

Result Solve(const Input& in) {
    Result r;
    for (int z = 0; z < kZoneCount; ++z) r.crushed[z] = in.crushed[z];
    r.belly = in.belly;
    const double m = in.mass, W = m * in.g;
    double v = in.v, t = 0.0;
    const double dt = std::max(1e-5, std::min(1e-3, 0.5 / (in.v + 1.0)));
    while (v > 0.0 && t < 30.0) {
        double F;
        if (in.mode == kNoseFirst || in.mode == kSternFirst) {
            // the zone at the front of the crush, in turn
            int z = -1;
            if (in.mode == kNoseFirst) {
                for (int k = kZoneCount - 1; k >= 0; --k) if (r.crushed[k] < kZones[k].s1 - kZones[k].s0) { z = k; break; }
            } else {
                for (int k = 0; k < kZoneCount; ++k) if (r.crushed[k] < kZones[k].s1 - kZones[k].s0) { z = k; break; }
            }
            const double Fs = z >= 0 ? kZones[z].force : 1e13;
            // the patch: the nose cone widens as it goes in; the stern cluster is broad
            const double rp = in.mode == kNoseFirst ? std::min(12.0, 1.5 + (r.crushed[kNose] + r.penetration) * 0.35) : 14.0;
            const double Fg = in.soil * kPi * rp * rp;
            if (Fg < Fs) { F = Fg; r.penetration += v * dt; }
            else { F = Fs; if (z >= 0) r.crushed[z] += v * dt; }
        } else {
            // a cylinder (R ~12 m) pressed in along ~150 m: the patch widens with the depth
            const double d = r.belly + r.penetration;
            const double w = std::min(24.0, 2.0 * std::sqrt(2.0 * 12.0 * std::max(0.01, d)));
            const double A = w * (in.mode == kBelly ? 150.0 : 140.0);
            const double Fs = kBellyPressure * A, Fg = in.soil * A;
            if (Fg < Fs) { F = Fg; r.penetration += v * dt; } else { F = Fs; r.belly += v * dt; }
        }
        const double a = (F - W) / m;
        if (a <= 0.0 && v < 0.05) break;
        v -= a * dt;
        t += dt;
        r.peakG = std::max(r.peakG, F / m / 9.80665);
    }
    r.duration = t;
    return r;
}

double Share(const Result& r, int zone) { return std::min(1.0, r.crushed[zone] / (kZones[zone].s1 - kZones[zone].s0)); }

int Grade(double share) { return share <= 0.1 ? 0 : share <= 0.4 ? 1 : share <= 0.7 ? 2 : 3; }

Breakup BreakLines(int mode, const double* share, double belly, double peakG) {
    Breakup b;
    const bool axial = mode == kNoseFirst || mode == kSternFirst;
    for (int z = 0; z < kZoneCount; ++z) {
        if (share[z] >= 0.4) b.crush[z] = axial ? 2 : 1;
        if (share[z] >= 0.7) {
            if (z > 0) b.cut[z - 1] = true;
            if (z < kZoneCount - 1) b.cut[z] = true;
        }
    }
    if (!axial && belly >= kBellyDepth) b.cut[kTraps] = b.cut[kHangar] = true;
    for (int k = 0; k < kZoneCount - 1; ++k)
        if (peakG > kJointG[k]) b.cut[k] = true;
    bool any = false;
    for (bool c : b.cut) any = any || c;
    if (!any) b.cut[kTraps] = b.cut[kHangar] = true;
    return b;
}

int EquipState(int e, double peakG, bool axial, double zoneShare) {
    if (zoneShare > 0.7) return 3;
    const double lim = axial ? kEquip[e].axialG : kEquip[e].lateralG;
    if (peakG <= 0.7 * lim) return 0;
    if (peakG <= lim) return 1;
    if (peakG <= 1.5 * lim) return 2;
    return 3;
}

}  // namespace tantra::impact
