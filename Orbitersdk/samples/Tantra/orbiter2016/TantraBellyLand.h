// TantraBellyLand: the autopilot ПОСАДКА ЛЁЖА of the АВТОПИЛОТ screen (the user, 2026-10-05: «на брюхо автопилот посадки у нас
// есть? активация, высота + - и гор. скорость и посадка?», «они должны и управлять с горячей корректировкой») - the ship
// horizontal, held up by the pods' cups turned down, landing on its two blades and the front support. Plain C++ like
// TantraGuidance: the ship fills a state every step, applies the commands; no Orbiter calls.
//   ВКЛ       the readiness check, then it holds the altitude, the ground speed and the heading it found.
//   ВЫСОТА ±  10 m (coarse 100 m); ГОР. СКОРОСТЬ ± 5 m/s (coarse 20 m/s, 0 = hover); КУРС ± 5 deg (coarse 30 deg) - all in flight.
//   ПОСАДКА   the speed to 0, the lying gear out (before 150 m), descent 3 m/s (1 m/s below 50 m, only once the ground speed is
//             under 1 m/s), contact, the pods' thrust run down in 4 s, cut-off.
//   ОТМЕНА    let go (the RCS quiet, the levers the pilot's).
// The laws: the altitude by the pods' vertical thrust (a PD on the altitude and the vertical speed, a slow integral on the
// vertical speed for what the model misses); the ground speed by the march (forward) and by the cups' tilt (fine, braking); the
// side drift by a bank of at most 5 deg; pitch 0, the heading held - by the RCS (the ship's FlyAttitude). The cups' angle
// follows from the ship's own table of the pods' thrust direction by cup angle (UpdatePods' geometry), slewed at 6 deg/s - the
// pods give no thrust while the cups are more than 3 deg off their command (Tantra::podAimed_).
#pragma once

#include "TantraGuidance.h"

#include <string>
#include <vector>

namespace tantra::guidance {

constexpr int kPodTab = 19;                       // the pods' direction table: cup angle 0, 10 .. 180 deg
constexpr int kBPhaseCount = 7;
extern const wchar_t* const kBPhases[kBPhaseCount];   // ГОТОВНОСТЬ .. ОТСЕЧКА

// ---- the laws ----
constexpr double kBAltStep = 10.0, kBAltCoarse = 100.0, kBAltMin = 30.0, kBAltMax = 10000.0;   // m
constexpr double kBSpdStep = 5.0, kBSpdCoarse = 20.0, kBSpdMax = 150.0;                       // m/s (the pods' bays shut at M 0,8)
constexpr double kBHdgStep = 5.0, kBHdgCoarse = 30.0;                                         // deg
constexpr double kBClimb = 5.0;                   // m/s: the climb / descent to a new altitude in the hold
constexpr double kBDescHi = 3.0, kBDescLo = 1.0, kBLoAlt = 50.0;   // m/s, m/s, m: the landing's descent (CG over the ground)
constexpr double kBGearAlt = 150.0;               // m: the lying gear on its locks above this, else the descent waits
constexpr double kBTouchVh = 1.0;                 // m/s: under 50 m only with the ground speed under this
constexpr double kBUnloadT = 4.0;                 // s: the pods' thrust run down after the contact
constexpr double kBTiltFwd = 15.0, kBTiltFwdNoMarch = 30.0, kBTiltAft = 35.0;   // deg: the pods' thrust tilt off the vertical
constexpr double kBCupRate = 6.0;                 // deg/s: the cups' command slew (the ship turns them at 15, aims within 3)
constexpr double kBAccMax = 1.5;                  // m/s^2: the horizontal acceleration / braking
constexpr double kBBankMax = 5.0;                 // deg: the bank against the side drift
constexpr double kBThrustMargin = 1.1;            // the pods' lift >= 1.1 x the weight
constexpr double kBNormVz = 1.5, kBNormVh = 1.0;  // the touchdown norms: m/s, m/s

// filled by the ship every step (the horizon: the ground speed split along the ship's heading and to its right)
struct BellyState {
    double simt = 0.0, dt = 0.0;
    double g = kG0;                               // m/s^2: the local gravity
    double alt = 0.0, vz = 0.0;                   // m: the CG over the ground; m/s vertical (+ up)
    double vF = 0.0, vS = 0.0;                    // m/s over the ground: along the heading, to the right
    double hdg = 0.0, pitch = 0.0, bank = 0.0;    // deg: the heading, the nose over the horizon, the right wing down +
    double mach = 0.0;
    double mass = 52.34e6;                        // kg
    bool contact = false;
    // the march (the stern cup, thrust along the hull)
    double marchMax = 0.0, marchLv = 0.0;         // N at level 1 now (0 unless run out on the run), the level now
    // the pods: 12 cups; the table of their mean thrust direction (forward, up in the ship's axes, of the full thrust) by cup angle
    double podMax = 0.0;                          // N at level 1, aimed (the shares of the pairs for the pitch included)
    bool podMaxEst = false;                       // podMax estimated (the pods not out and aimed now)
    double podTab[kPodTab][2] = {};
    double podAngle = 0.0, podCmdAngle = 0.0;     // deg: the cups now, their command
    double podOut = 0.0;                          // 0 in the bays .. 1 out
    bool podAimed = false, podsWanted = false;
    double podLv = 0.0;                           // the cups' mean level now
    double podLvMax = 1.0;                        // the lever's ceiling (0,85 with УВТ on)
    // the lying gear (the blades and the front support)
    bool gearLying = true;                        // the flight set is the lying one (not the stern legs)
    double gear = 0.0;                            // 0 stowed .. 1 on its locks
    bool gearDown = false;                        // commanded down
    bool wingsFolded = false;                     // the pods only come out with the wings out
    bool plantRun = true;
    bool otherAp = false;                         // another autopilot engaged now
};

struct BellyCmd {
    bool attitude = false, thrust = false;        // fly the attitude / set the levels (hold, landing, the run-down)
    bool pods = false;                            // the pods out, the cups to podAngle
    bool gear = false;                            // the lying gear out (the set changed to lying first if the gear is stowed)
    double pitch = 0.0, bank = 0.0, hdg = 0.0;    // deg
    double march = 0.0;                           // the march's level 0..1
    double podLv = 0.0;                           // the pods' lever 0..1
    double podAngle = 90.0;                       // deg: the cups' command (slewed)
    // for the screen: the forces asked and the split
    double Fz = 0.0, Fx = 0.0;                    // N: vertical, forward (horizon)
    double FpUp = 0.0, FpFwd = 0.0, Fm = 0.0;     // N: the pods' up and forward parts, the march
    double vzCmd = 0.0, axCmd = 0.0;              // m/s, m/s^2
    bool satUp = false;                           // the pods at their ceiling (the lift short)
};

class BellyLand {
public:
    enum Mode { kOff = 0, kHoldM, kLand, kLandedM };
    // ---- every step ----
    void Step(const BellyState& st, double sysNow);
    const BellyCmd& Cmd() const { return cmd_; }
    bool Engaged() const { return mode_ == kHoldM || mode_ == kLand; }
    // ---- the keys (all of them in flight: the targets change and the autopilot follows at once) ----
    void Engage(double now);                      // ВКЛ: the check, then hold what it finds
    void StepAlt(int dir, bool coarse);
    void StepSpd(int dir, bool coarse);
    void StepHdg(int dir, bool coarse);
    void Land(double now);                        // ПОСАДКА
    void Release(const std::wstring& why);        // ОТМЕНА / РУЧНОЕ, or another autopilot engaged
    bool CanLand() const { return mode_ == kHoldM; }
    // ---- what the screen shows ----
    int Mode() const { return mode_; }
    int Phase() const { return phase_; }
    double AltT() const { return altT_; }
    double SpdT() const { return spdT_; }
    double HdgT() const { return hdgT_; }
    const BellyState& State() const { return st_; }
    const std::vector<Check>& Checks() const { return checks_; }
    const Journal& Log() const { return log_; }
    double LiftMax() const;                       // N: the pods' most vertical thrust within the tilt window, at the full lever
    double Weight() const { return st_.mass * st_.g; }
    double PrepTime() const;                      // s until the pods are out and aimed (0 ready)
    double VertAngle() const;                     // deg: the cup angle whose thrust is vertical (NaN: none)
    double TdVz() const { return tdVz_; }         // the touchdown (NaN until one)
    double TdVh() const { return tdVh_; }
    std::wstring TLabel() const;

    // the pods' table: the thrust direction (forward, up) at a cup angle; the cup angle for a thrust elevation (deg from forward)
    static void PodDir(const BellyState& s, double ang, double& fwd, double& up);
    static double CupFor(const BellyState& s, double elev);

private:
    std::vector<Check> BuildChecks(bool* ok) const;
    void Note(const std::wstring& txt, int lvl) { log_.Add(TLabel(), txt, lvl); }
    void Laws();
    BellyState st_;
    BellyCmd cmd_;
    Journal log_;
    std::vector<Check> checks_;
    int mode_ = kOff, phase_ = 0;
    double altT_ = 500.0, spdT_ = 0.0, hdgT_ = 0.0;
    double t0_ = 0.0, tPh_ = 0.0, tTouch_ = 0.0;  // sim time: engaged, the phase began, the contact
    double iVz_ = 0.0, iAx_ = 0.0;                // the integrals
    double cupCmd_ = 90.0, lvTouch_ = 0.0;
    double tdVz_ = kNaN, tdVh_ = kNaN;
    bool have_ = false, warnLift_ = false, gearAsked_ = false;
};

}  // namespace tantra::guidance
