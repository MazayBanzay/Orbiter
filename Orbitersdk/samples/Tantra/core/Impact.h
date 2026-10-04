// Tantra core: the hull hitting the ground (Tantra_Design/tantra_damage.html, DESIGN_LOCAL «Модель повреждений: удар
// о грунт»). No Orbiter dependencies (tests/impact_test.cpp).
//
// The ship is five zones along its length, each crushing at its own force; the ground holds its pressure under the
// growing contact patch. What is weaker gives: the structure crushes, or the ship goes into the ground. The impact
// energy goes into crushing the zones in turn (nose-first from the nose, stern-first from the stern); belly or side, a
// cylinder pressed into the ground over the length. The struts have already taken their share (core/Damage): what
// arrives here is the speed left for the hull.
// Equipment holds its own g: along the axis what the anamezon drive asks (~200 g, sustained), across far less (its
// mounts, the reactor in a damped cradle 38 g). The inertia absorber is in the living module only.
#pragma once

namespace tantra::impact {

enum Zone { kStern, kTraps, kHangar, kLiving, kNose, kZoneCount };   // from the stern
enum Mode { kNoseFirst, kSternFirst, kBelly, kSide };

struct ZoneSpec { double s0, s1, force; };          // stations [m], crushing force [N]
extern const ZoneSpec kZones[kZoneCount];
constexpr double kBellyDepth = 3.0;                // belly / side structure before the compartments [m]
constexpr double kBellyPressure = 4.0e6;           // its crushing pressure [Pa]

struct Input {
    Mode mode = kBelly;
    double mass = 15.0e6;      // [kg]
    double g = 9.81;           // local gravity [m/s^2]
    double v = 0.0;            // speed into the ground left for the hull [m/s]
    double soil = 6.0e6;       // the ground's bearing pressure [Pa] (sand 1.5, firm 6, rock 60 MPa, water 0.4)
    double crushed[kZoneCount] = {};   // already crushed [m] (an earlier impact)
    double belly = 0.0;        // already pressed in [m]
};
struct Result {
    double crushed[kZoneCount] = {};   // total after this impact [m]
    double belly = 0.0;        // [m]
    double penetration = 0.0;  // into the ground [m]
    double peakG = 0.0;        // the structure's deceleration [g]
    double duration = 0.0;     // [s]
};
Result Solve(const Input& in);

// crushed share of a zone 0..1; a graded state: 0 intact, 1 dented (<=0.4), 2 holed (<=0.7), 3 destroyed
double Share(const Result& r, int zone);
int Grade(double share);

// equipment: what it holds along the ship's axis and across
enum Equip { kTrapCassettes, kVeu, kStore, kPlant, kGyros, kArgon, kRovers, kAirlock, kLife, kAbsorber, kBridge, kEquipCount };
struct EquipSpec { int zone; double axialG, lateralG; const char* ru; const char* en; };
extern const EquipSpec kEquip[kEquipCount];
// 0 intact, 1 at its limit, 2 damaged, 3 torn off (or crushed with its compartment)
int EquipState(int e, double peakG, bool axial, double zoneShare);

// How a destroyed hull comes apart: cuts at the joints of the zones (s 21 / 88 / 121 / 145) and each zone's chunk
// deformed or not. A zone crushed through (>= 0.7) parts from both neighbours, crushed half (>= 0.4) it is deformed: flat
// from a belly / side blow, short from an axial one. A belly pressed in all the way breaks the keel at the hangar's huge
// opening (s 88 and 121, the weakest sections). The peak g bends the hull at its joints over their strength: the hangar
// joints first (25 / 30 g), the living module's to the nose (40 g), the stern block (60 g). Nothing cut: the hangar
// joints (the ship destroyed by heat or a trap). Strengths are design assumptions (DESIGN_LOCAL «Разлом корпуса»).
struct Breakup {
    bool cut[kZoneCount - 1] = {};   // joint k between zone k and k+1
    int crush[kZoneCount] = {};      // 0 whole, 1 flattened, 2 shortened
};
constexpr double kJointG[kZoneCount - 1] = {60.0, 30.0, 25.0, 40.0};
Breakup BreakLines(int mode, const double* share, double belly, double peakG);

}  // namespace tantra::impact
