// Tantra core: leg systems beyond the kinematics (Tantra_Design/DESIGN_LOCAL.md, «Ноги Т8: передовая механика», T9).
// No Orbiter dependencies (tests/legs_test.cpp).
//   * main joints: superconducting motor-generators on magnetic bearings (10 T), a sliding catcher bearing takes
//     the load above their capacity or without power; lowering the ship returns its energy (regeneration);
//   * ankle: gas-hydraulic strut with a magnetorheological valve - constant force over the whole stroke;
//   * feet: umbrella rims settle into the ground (spudcan); the ground under them is sintered after landing
//     (field heating) - conforms, then locks;
//   * sensing: fibre strain gauges in the stages - the load of every leg against its rating.
// Legs: 0 blade port, 1 blade starboard, 2..5 stern legs, 6 kangaroo.
#pragma once

namespace tantra::legs {

constexpr int kLegCount = 7;
constexpr int kKangaroo = 6;

constexpr double kTesla = 10.0;
constexpr double kHipBearingArea = 9.0;          // blade hip drum R 1.5 x 3.0 m, projected [m^2]
constexpr double kSternBearingArea = 6.72;       // stern-leg hinge R 1.2 x 2.8 m
constexpr double kKangBearingArea = 5.76;        // kangaroo hip drum R 0.9 x 3.2 m
constexpr double kCatcherMu = 0.05;              // sliding catcher bearing
constexpr double kRegenEff = 0.9;

constexpr double kStrokeCarriage = 1.5, kStrokeStern = 1.0, kStrokeKang = 1.0;   // ankle struts [m]
constexpr double kStaticSag = 0.3;               // share of the stroke taken by the weight
constexpr double kSoftG = 1.5, kBreakG = 3.0;    // stroke deceleration over the weight: harmless / bottoming out [g]

constexpr double kMuConform = 0.5, kMuJammed = 0.7, kMuAnchored = 0.9;   // settling rim / settled / sintered root
constexpr double kJamTime = 2.0, kAnchorDelay = 5.0, kAnchorTime = 120.0;   // [s]: sintering takes minutes
constexpr double kAlarm = 0.85;                  // load / rating: "on the edge"

// Magnetic bearing capacity: magnetic pressure B^2 / 2 mu0 over the projected area [N].
double BearingCapacity(double area, double tesla = kTesla);

// Touchdown speeds the strut takes: the valve spreads the stop over the full stroke, a = v^2 / 2S.
struct TouchdownLimits { double vSoft, vBreak; };
TouchdownLimits Limits(double stroke);

// Loads of the seven legs [N]. weight is shared columnShare (blades + kangaroo) : rest (stern feet at feetX/feetY,
// ship frame, standing: the plane normal to the axis); of the front share, kangShare rests on the kangaroo foot.
// The horizontal wind force (fx starboard, fy "up" in the ship frame when standing / fz forward when lying) acts
// at height h.
struct LegLoadInput {
    double weight = 0.0, columnShare = 1.0, kangShare = 0.0, h = 0.0;
    double windSide = 0.0;       // across the blade legs (x) [N]
    double windX = 0.0, windY = 0.0;  // in the plane of the stern feet [N]
    double hipX = 19.0;          // blade legs at +-hipX
    double feetX[4] = {}, feetY[4] = {};
    double cosSplay = 0.96;      // stern legs lean off the vertical
};
void LegLoads(const LegLoadInput& in, double out[kLegCount]);

// Feet on the ground: the rim settles on contact, locks under load, the root is sintered once the ship rests.
class Soles {
public:
    // contact: feet on the ground; resting: no carriage motion and no lift-off thrust.
    void Update(double dt, bool contact, bool resting);
    double Mu() const;
    double Jam() const { return jam_; }          // 0 settling .. 1 locked
    double Anchors() const { return anchors_; }  // 0 .. 1 sintered root
private:
    double jam_ = 0.0, anchors_ = 0.0, rest_ = 0.0;
};

// Energy of the leg drives: the CG height h under the weight; lowering charges the store, lifting draws it.
class Regen {
public:
    void Update(double dt, double weight, double h, bool moving);
    double Returned() const { return back_; }    // [J] since the scenario start
    double Spent() const { return spent_; }      // [J]
    double Power() const { return power_; }      // [W] + returned, - drawn (last step)
private:
    double h_ = -1.0, back_ = 0.0, spent_ = 0.0, power_ = 0.0;
};

}  // namespace tantra::legs
