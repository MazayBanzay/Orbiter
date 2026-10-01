// Tantra core: leg systems beyond the kinematics (Tantra_Design/DESIGN_LOCAL.md, «Ноги Т8: передовая механика»).
// No Orbiter dependencies (tests/legs_test.cpp).
//   * main joints: superconducting motor-generators on magnetic bearings (10 T), a sliding catcher bearing takes
//     the load above their capacity or without power; lowering the ship returns its energy (regeneration);
//   * ankle: gas-hydraulic strut with a magnetorheological valve - constant force over the whole stroke;
//   * pad sole: granular jamming under internal pressure (conforms, then locks) plus anchors on CNT muscles;
//   * sensing: fibre strain gauges in the bands - the load of every leg against its rating.
#pragma once

namespace tantra::legs {

constexpr double kTesla = 10.0;
constexpr double kHipBearingArea = 6.37;         // hip drum R 1.3 x 2.45 m, projected [m^2]
constexpr double kSternBearingArea = 6.72;       // stern-leg hinge R 1.2 x 2.8 m
constexpr double kCatcherMu = 0.05;              // sliding catcher bearing
constexpr double kRegenEff = 0.9;

constexpr double kStrokeCarriage = 1.5, kStrokeStern = 1.0;   // ankle struts [m]
constexpr double kStaticSag = 0.3;               // share of the stroke taken by the weight
constexpr double kSoftG = 1.5, kBreakG = 3.0;    // stroke deceleration over the weight: harmless / bottoming out [g]

constexpr double kMuConform = 0.5, kMuJammed = 0.7, kMuAnchored = 0.9;
constexpr double kJamTime = 2.0, kAnchorDelay = 5.0, kAnchorTime = 1.0;   // [s]
constexpr double kAlarm = 0.85;                  // load / rating: "on the edge"

// Magnetic bearing capacity: magnetic pressure B^2 / 2 mu0 over the projected area [N].
double BearingCapacity(double area, double tesla = kTesla);

// Touchdown speeds the strut takes: the valve spreads the stop over the full stroke, a = v^2 / 2S.
struct TouchdownLimits { double vSoft, vBreak; };
TouchdownLimits Limits(double stroke);

// Loads of the six legs [N]: 0 carriage port, 1 carriage starboard, 2..5 stern legs (feet at feetX/feetY,
// ship frame, standing: the plane normal to the axis). weight is shared columnShare : rest. The horizontal wind
// force (fx starboard, fy "up" in the ship frame when standing / fz forward when level) acts at height h.
struct LegLoadInput {
    double weight = 0.0, columnShare = 1.0, h = 0.0;
    double windSide = 0.0;       // across the carriage legs (x) [N]
    double windX = 0.0, windY = 0.0;  // in the plane of the stern feet [N]
    double hipX = 19.0;          // carriage legs at +-hipX
    double feetX[4] = {}, feetY[4] = {};
    double cosSplay = 0.96;      // stern legs lean off the vertical
    bool lowerPairOnly = false;  // lying level: the stern share rests on the lower pair (legs 2, 3), no moment
};
void LegLoads(const LegLoadInput& in, double out[6]);

// Pad soles and anchors: conform on contact, lock under load, anchors out once the ship rests.
class Soles {
public:
    // contact: pads on the ground; resting: no carriage motion and no lift-off thrust.
    void Update(double dt, bool contact, bool resting);
    double Mu() const;
    double Jam() const { return jam_; }          // 0 conforming .. 1 locked
    double Anchors() const { return anchors_; }  // 0 in .. 1 out
private:
    double jam_ = 0.0, anchors_ = 0.0, rest_ = 0.0;
};

// Energy of the carriage drives: the CG height h under the weight; lowering charges the store, lifting draws it.
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
