// TantraReentry: the autopilot СХОД - the deorbit and the atmospheric entry down to the landing's entry (5 km, -150 m/s, stern
// down), where it hands over to «ПОСАДКА НА КОРМУ». The concept: Tantra_Design/refine/REENTRY.md. Plain C++ like TantraGuidance:
// no Orbiter calls, no drawing; the ship fills a ReentryState every step (clbkPreStep) and applies the ReentryCmd - its own physics
// flies. The planner runs the same laws ahead: a point mass over the rotating planet (the ship's air table, core/Aero by the
// regime's attitude, the regime's thrust law) from now to the landing's entry.
//   Regimes (before ПУСК): КОРМОЙ · ТЯГА ★ (stern first, wings folded, the planetary engines hold the g and the heat, then brake),
//   КОРМОЙ · БАЛЛИСТИКА (stern first, no thrust until the braking: the periapsis alone sets the load and the heat - the planner
//   scans it), НОСОМ · КРЫЛЬЯ (the nose shield first at 40 deg, wings 30 / 90 deg, the bank steers the range; below ~M 2 / 15 km
//   the flip stern down and the braking). Wings АВТО ★ / 90° / 30° / СЛОЖЕНЫ (in flight too); the g limit 1,5..5; the site: one
//   of the ship's bases or ПО ТРАССЕ.
//   Phases: ГОТОВНОСТЬ -> ОЖИДАНИЕ ТОЧКИ СХОДА -> РАЗВОРОТ НА ТОРМОЖЕНИЕ -> ИМПУЛЬС СХОДА (retrograde until the periapsis is the
//   corridor's) -> РАЗВОРОТ НА ВХОД -> ВХОД (the regulation, once a second: candidates through thermalscreen::RunForecast, the
//   cheapest within the zones' limits - 100 K and the g limit; between them feedback on the live g and the hottest zone) ->
//   [РАЗВОРОТ КОРМОЙ ВНИЗ] -> ТОРМОЖЕНИЕ (the constant level that reaches 150 m/s at 5 km) -> ВХОД ПОСАДКИ (handover).
// The attitude command is a direction in the local horizon frame (x east, y north, z up): where the hull's +z (the nose) and +y
// (the dorsal side) should point - the ship turns its own axes toward them with the RCS (the error is a cross product in its
// frame), as РАД±. The angles (pitch of +z over the horizon, its azimuth, the bank) come along for the screen.
#pragma once

#include "TantraEntryForecast.h"
#include "TantraGuidance.h"

#include <string>
#include <vector>

namespace tantra::reentry {

using guidance::V3;
using guidance::Planet;
using guidance::Elements;
using guidance::Journal;
using guidance::Check;
using guidance::Event;
using guidance::kNaN;
using guidance::kInf;
constexpr int kZones = thermalscreen::kZones;

// ---- the laws ----
constexpr double kEI = 120e3;                     // m: the entry interface (the forecast's corridor top is 125 km)
constexpr double kHandH = guidance::kLZ0, kHandV = -guidance::kLVz0;   // 5000 m, 150 m/s: the landing's entry
constexpr double kMarginK = 100.0;                // K under each zone's limit (REENTRY.md)
constexpr double kAoaNose = 40.0;                 // deg: НОСОМ
constexpr double kTurnLead = 120.0;               // s: the turn to the burn attitude starts this long before the burn
constexpr double kLead = 150.0;                   // s: the earliest burn from now
constexpr double kAttTol = 5.0;                   // deg: the hull on its attitude
constexpr double kTermTrig = 0.8;                 // the braking starts when the level reaching the alignment is this share of the usable
constexpr double kAlignH = 9000.0, kAlignV = 250.0, kAlignTau = 6.0;   // m, m/s, s: the braking aims there; then the thrust steers the
                                                  // velocity onto 150 m/s straight down (its error over kAlignTau, gravity and drag
                                                  // cancelled) - the hull follows the thrust, stern down
constexpr double kFlipMach = 2.0, kFlipH = 15e3, kFlipT = 25.0;   // НОСОМ: the flip stern down (M, m) and its length (s)
constexpr double kPodQ = 40e3, kPodH = 20e3, kPodMach = 0.8, kPodT = 12.0;   // the pods out (Pa, m, M) and their run (s): the ship
                                                  // opens the bays only under M 0,8 and with the wings not folded (UpdatePods)
constexpr double kNomBank = 40.0;                 // deg: НОСОМ's bank of the burn-time scan (range margin both ways)
constexpr double kMissOk = 50e3;                  // m: the site counts as reached
constexpr double kReplanDt = 60.0;                // s: the plan from where the ship is, in flight
constexpr double kSoundPlan = 300.0;              // m/s: the planner's speed of sound (the forecast's)
constexpr double kCrestOf[3] = {1.0, 0.75, 0.0034};   // aeroCrest_ of the ship's wing modes 90 deg / 30 deg / folded
// the corridor: the periapsis where the air is this share of the surface density (КОРМОЙ · ТЯГА, НОСОМ; БАЛЛИСТИКА scans them)
constexpr double kRhoStern = 1e-3, kRhoNose = 3e-4;
constexpr double kRhoScan[7] = {1e-5, 3e-5, 1e-4, 3e-4, 1e-3, 3e-3, 1e-2};
constexpr double kSternCouple = 0.02;             // допущение: the share of the stern zone's flux on the frontal area the plant's stern takes

enum Regime { kRStern = 0, kRBallistic, kRNose, kRegimeCount };
enum WingSel { kWAuto = 0, kW90, kW30, kWFold, kWingSelCount };
enum Att { kAttNone = 0, kAttRetro, kAttStern, kAttNose, kAttAlign };   // retro: nose against the inertial velocity (the burn);
                                                  // align: nose along the thrust demand (the last braking, stern down)
enum RLim { kRLimNone = 0, kRLimG, kRLimHeat, kRLimStern, kRLimBrake, kRLimMass };   // what sets the thrust (the screen)
enum REv { kEvBurn = 100, kEvBurnEnd, kEvEI, kEvPeakG, kEvBrake, kEvPods, kEvFlip, kEvHand, kEvHot, kEvGLim, kEvDry, kEvCrashR, kEvWing };
constexpr int kRPhaseCount = 9;
extern const wchar_t* const kRPhases[kRPhaseCount];      // ГОТОВНОСТЬ .. ВХОД ПОСАДКИ
extern const wchar_t* const kRegimes[kRegimeCount];      // КОРМОЙ · ТЯГА ..
extern const wchar_t* const kWingSel[kWingSelCount];     // АВТО ..
extern const wchar_t* const kZoneRu[kZones];             // нос, днище, ..
extern const wchar_t* const kLimRu[6];                   // —, перегрузка, нагрев, корма, торможение, рабочая масса

struct Site { std::wstring name; double lat = 0.0, lon = 0.0; };   // rad (a base of the planet)

// filled by the ship every step
struct ReentryState {
    double simt = 0.0, dt = 0.0;                  // s
    Planet planet;                                // R, mu, omega (its rho0 / hs unused: the air is the table)
    thermalscreen::Air air;                       // the planet's density by altitude (the thermal page's table; empty: Earth)
    double lat = 0.0, lon = 0.0;                  // rad: the equatorial position
    double alt = 0.0;                             // m over the mean radius
    double vE = 0.0, vN = 0.0, vU = 0.0;          // m/s: the velocity over the rotating ground (= the airspeed), east / north / up
    double pitch = 0.0, hdg = 0.0, bank = 0.0;    // deg: the hull's +z over the horizon, its azimuth, the bank (+ right)
    double aoa = 0.0;                             // deg: the angle of attack (180 stern first)
    double rho = 0.0, mach = 0.0, q = 0.0;        // the air: kg/m^3, Mach, Pa
    bool contact = false;
    double mass = 52.34e6, argon = 6.2e6, iron = 3.8e6;   // kg
    int reactionMass = guidance::kIron;           // what the cups run on now: guidance::kArgon, kIron, kNoMass
    double Fmarch = 0.0, Fpods = 0.0;             // N at level 1 now: the march (0 off the run), the pods (0 in the bays)
    double FmNow = 0.0, FpNow = 0.0;              // N now
    // the plant (core/Plant)
    bool plantRun = true;
    double sternT = 300.0, tSafe = 800.0;         // K: Plant::SternT, Config::tSafe
    bool limiter = true;                          // Plant::Limiter
    // the skin (core/Damage)
    double skinT[kZones] = {}, skinLim[kZones] = {}, skinFlux[kZones] = {};   // K, K, W/m^2
    damage::Model skin;                           // a copy of Tantra::damage_ (the forecasts step it)
    damage::Exposure expo;                        // what the hull presents (as the thermal page's Path::expo)
    int wingMode = 2;                             // 0 = 90 deg, 1 = 30 deg, 2 = folded
    double fold = 1.0;                            // the folding now 0..1 (tuck_)
    double crestAvail = 0.0;                      // aeroCrest_
    double gearArea = 0.0;                        // aeroGearArea_ [m^2]
    double podsOut = 0.0;                         // the pods 0 in the bays .. 1 out (podOut_)
    // the energy core (core/TantraCore Snapshot)
    double coilMargin = 0.42;                     // the winding's margin on the critical current (0.15 needed)
    bool cryoOk = true, pumpsOk = true, veuOk = true, storeOk = true;
};

// what the autopilot commands (applied by the ship while the flags say so)
struct ReentryCmd {
    bool attitude = false;                        // turn the hull to `nose` / `up`
    int att = kAttNone;
    V3 nose{0, 0, 1};                             // unit, local horizon (x east, y north, z up): where the hull's +z should point
    V3 up;                                        // unit, local horizon: where the hull's +y should point; zero: the roll is free
    double pitch = 0.0, hdg = 0.0, bank = 0.0;    // deg: the same as angles
    bool thrust = false;                          // set the march and the pods to their levels
    double march = 0.0, pods = 0.0;               // levels 0..1 of the thrust available now (Fmarch, Fpods)
    bool podsOut = false;                         // the pods out of their bays (apply while `thrust`)
    int wingMode = -1;                            // wanted: 0 = 90 deg, 1 = 30 deg, 2 = folded; -1 no change
    int massWanted = guidance::kIron;             // argon in the air below 30 km, iron above (the ship's UpdateReactionMass does it)
    bool handover = false;                        // the landing's entry reached: start «ПОСАДКА НА КОРМУ»
    int lim = kRLimNone;                          // what sets the thrust: RLim
};

// the regulation's memory (the live entry and the planner's)
struct Reg {
    double nextEval = -1e9;
    double lHeat = 0.0, lTerm = 0.0;              // the level (of the usable) the forecasts want; the one reaching the alignment
    bool braking = false, align = false;
    V3 dirL{0, 0, 1};                             // the thrust's direction while aligning (local horizon)
    double bank = 0.0; int bankSign = 1;          // deg (НОСОМ)
    int wing = 2;                                 // the ship's wing mode chosen
    double level = 0.0;                           // of the usable acceleration: min(the thrust, the g limit)
    int lim = kRLimNone;
    bool fcOk = true; int fcHot = -1; double fcMargin = 0.0, fcG = 0.0;   // the last forecast: within limits, the hottest zone, K, g
};

struct RSample { double t, dr, h, v, g; int phase; };   // s from the plan's start, downrange [m], altitude [m], airspeed [m/s], felt g
struct ReentryPlan {
    bool ok = false; std::wstring why;
    int regime = kRStern;
    double tBurn = kNaN, dvBurn = 0.0, burnLen = 0.0, hpT = 0.0;   // s from now (NaN: no burn), m/s, s, m: the corridor's periapsis
    double tEI = kNaN, vEI = 0.0, gamEI = 0.0;    // s, m/s, deg
    double nMax = 0.0, nMaxH = 0.0;               // g (aerodynamic and the thrust) and where [m]
    double peakT[kZones] = {}, lim[kZones] = {};  // K
    int hot = -1; double margin = 0.0;            // the zone closest to its limit - margin, K left (< 0: over)
    double sternTMax = 0.0;                       // K: the plant's stern
    double argon = 0.0, iron = 0.0;               // kg used
    double tHand = kNaN, vHand = 0.0, vzHand = 0.0, vhHand = 0.0, pitchHand = 0.0;   // s, m/s airspeed, vertical, horizontal; deg
    double latHand = 0.0, lonHand = 0.0;          // rad: where the landing begins (planet-fixed)
    bool site = false; double miss = kNaN, missAlong = 0.0, missCross = 0.0;   // m: to the site (along + beyond, across + right)
    double brakeH = kNaN, flipH = kNaN;           // m: where the braking / the flip began
    int wing = 2;                                 // the ship's wing mode at the entry
    double nextPass = kNaN;                       // s: the site's next chance when it is out of reach (a warning)
    std::vector<RSample> track; std::vector<Event> ev;
};

// what the planner starts from
struct PlanIn {
    Planet pl; thermalscreen::Air air;
    int regime = kRStern, wingSel = kWAuto; double gLim = 3.0;
    V3 r, v;                                      // inertial, the frame of now (x through longitude 0, z the axis)
    double mass = 52.34e6, argon = 6.2e6, iron = 3.8e6;
    double Fmarch = 0.0, Fpods = 0.0;             // N at level 1 (0: the plant's nominal - planned as it will be)
    damage::Model skin; damage::Exposure expo; double gearArea = 0.0;
    double sternT = 300.0, tSafe = 800.0;
    bool siteValid = false; double siteLat = 0.0, siteLon = 0.0;
    double tBurn = kNaN;                          // s from now; NaN: the planner chooses it (the site) / the earliest (ПО ТРАССЕ)
    double hpT = kNaN;                            // m: the corridor's periapsis; NaN: the regime's
    int phase = 1;                                // where the flight is: 1 before the burn, 4 after it, 5.. in the air
    Reg reg;                                      // the regulation's memory when it starts in the air
    double dvDone = 0.0;                          // m/s of the burn already given (phase >= 4)
};
ReentryPlan PlanReentry(const PlanIn& in);

class Reentry {
public:
    // ---- every step (clbkPreStep) ----
    void Step(const ReentryState& st, double sysNow);
    const ReentryCmd& Cmd() const { return cmd_; }
    bool Engaged() const { return mode_ == guidance::kAuto || mode_ == guidance::kHold; }
    // ---- the ship's bases (the site list) ----
    void SetSites(const std::vector<Site>& s);
    // ---- the keys (sysNow: the system time - the checks and the confirmations run in it) ----
    bool Editable() const { return mode_ == guidance::kIdle && !armed_ && checkT_ < 0; }
    void StepRegime(int dir);                     // КОРМОЙ · ТЯГА / КОРМОЙ · БАЛЛИСТИКА / НОСОМ · КРЫЛЬЯ (before ПУСК)
    void StepWings(int dir);                      // АВТО / 90° / 30° / СЛОЖЕНЫ (in flight too)
    void StepGLim(int dir);                       // 0,5 g, 1,5..5
    void StepSite(int dir);                       // ПО ТРАССЕ, the bases
    void Arm(double now);                         // ВЗВЕСТИ / снять взвод
    void Start(double now);                       // ПУСК, ПОДТВЕРДИТЬ within 3 s
    void Hold(double now);                        // УДЕРЖАНИЕ / ПРОДОЛЖИТЬ
    void Manual(double now);                      // РУЧНОЕ
    void Abort(double now);                       // ОТМЕНА, ПОДТВЕРДИТЬ within 3 s: before the burn - out; after - БАЛЛИСТИКА, stern first
    void Reset(double now);                       // СБРОС
    void HandedOver();                            // «ПОСАДКА НА КОРМУ» took the ship (after the handover flag): СХОД lets go
    bool CanArm() const { return ((mode_ == guidance::kIdle || mode_ == guidance::kManual || mode_ == guidance::kAbort) && checkT_ < 0) || armed_; }
    bool CanStart() const { return armed_; }
    bool CanHold() const { return Engaged() && phase_ < 8; }
    bool CanManual() const { return Engaged() || mode_ == guidance::kManual; }
    bool CanAbort() const { return (mode_ == guidance::kIdle && armed_) || (Engaged() && phase_ < 8); }
    bool CanReset() const { return !Engaged(); }
    // ---- what the screen shows ----
    int Mode() const { return mode_; }
    int Phase() const { return phase_; }
    int RegimeSel() const { return regime_; }
    int WingsSel() const { return wings_; }
    double GLim() const { return gLim_; }
    int SiteIdx() const { return site_; }         // -1: ПО ТРАССЕ
    std::wstring SiteName() const;
    const std::vector<Site>& Sites() const { return sites_; }
    const ReentryState& State() const { return st_; }
    const ReentryPlan& Plan() const { return plan_; }
    double PlanAge() const { return st_.simt - planT_; }   // s since the plan's start (its track's t)
    const Reg& Regulation() const { return reg_; }
    const Elements& El() const { return el_; }
    double TimeToBurn() const { return tBurnAbs_ - st_.simt; }   // s (NaN: none planned / done)
    double Dv() const { return dv_; }             // m/s given since ПУСК
    double MaxG() const { return maxG_; }
    double Felt() const { return felt_; }         // g now (the aerodynamic load and the thrust)
    double PeakT(int z) const { return peakT_[z]; }
    bool Armed() const { return armed_; }
    bool Checking() const { return checkT_ >= 0; }
    bool GoArmed(double now) const { return now - goT_ < guidance::kConfirm; }
    bool AbArmed(double now) const { return now - abT_ < guidance::kConfirm; }
    std::vector<Check> ChecksShown() const { return checkT_ >= 0 || armed_ ? checks_ : BuildChecks(); }
    int ChecksDone(double now) const { return checkT_ >= 0 ? int((now - checkT_) / guidance::kCheckDt) : armed_ ? 99 : -1; }
    const Journal& Log() const { return log_; }
    std::wstring TLabel() const;                  // Т−mm:ss to the burn, Т+mm:ss after it

private:
    PlanIn MakeIn(bool scan) const;
    void Replan(bool scan);
    void Tick(double now);
    std::vector<Check> BuildChecks() const;
    void SetPhase(int p);
    void Note(const std::wstring& txt, int lvl) { log_.Add(TLabel(), txt, lvl); }
    int regime_ = kRStern, wings_ = kWAuto, site_ = -1;
    double gLim_ = 3.0;
    std::vector<Site> sites_;
    int mode_ = guidance::kIdle, phase_ = 0;
    ReentryState st_;
    ReentryCmd cmd_;
    ReentryPlan plan_;
    Reg reg_;
    Elements el_;
    Journal log_;
    bool have_ = false, armed_ = false, warnedHot_[kZones] = {}, warnedG_ = false, warnedStern_ = false, eiSeen_ = false;
    double checkT_ = -1.0, goT_ = -99.0, abT_ = -99.0;
    double tBurnAbs_ = kNaN, tBurnStart_ = kNaN, phT_ = 0.0, planT_ = -1e9, dv_ = 0.0, maxG_ = 0.0, felt_ = 0.0, hpT_ = 0.0;
    double podT_ = 0.0;
    double peakT_[kZones] = {};
    std::vector<Check> checks_;
};

}  // namespace tantra::reentry
