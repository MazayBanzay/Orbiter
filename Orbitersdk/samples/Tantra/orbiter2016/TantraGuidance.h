// TantraGuidance: the autopilot of the bridge's АВТОПИЛОТ screen (TantraAutopilotScreen) - the guidance of the mockup
// Tantra_Design/tantra_autopilot_screen.html (the user, 2026-10-04) as plain C++: no Orbiter calls, no drawing.
//   Ascent  - from the launch table to orbit. The targets (inclination or azimuth, periapsis, apoapsis, g limit) set by the
//             screen's arrows; ВЗВЕСТИ (the readiness check, 0.35 s a line) -> ПУСК -> ПОДТВЕРДИТЬ (3 s); the phases: straight
//             up to 2 km and 150 m/s, the roll of the pitch plane onto the launch azimuth, the pitch program over 15 scale heights,
//             the working-mass change at 30 km (from there the pitch never drops under the climb to the insertion altitude),
//             the burn until the apoapsis is the target (a throttle tail-off, 2 s), the coast, the circularisation started half
//             its burn before the apoapsis, on orbit. The heading steers the thrust along the velocity still to be gained in the
//             target plane; the throttle holds the g limit; the TVC (+-10 deg) and the slew rates are the mockup's.
//   Landing - on the stern (the 2-D model of the mockup, in the vertical plane of the wind, run here on both horizontal axes):
//             approach at the descent rate, braking at the suicide-burn height with the thrust's rise time, the legs at 400 m,
//             hover, descent 2 m/s (the last 8 m at 1.5), touchdown judged against the norms, the load to the legs in 8 s,
//             cut-off; the thrust split (the pods keep a fifth, the march the rest up to 12.1 T, the pods the excess), the field
//             the march share needs (the plant's AUTO), 20 / 25 %/s capsule rates; over the point a PD on the drift - the pods'
//             nozzles first, the rest by tilting the hull, the tilt held by the march TVC.
// The ship fills a state every step (clbkPreStep) and applies the commands - its own physics flies. The mockup's point-mass
// simulator lives on only in the planners (the dashed plan): the same laws run ahead over the mockup's spherical rotating
// planet, from where the ship is. Each part keeps its journal (the keys and the flight events, newest first): the screen is
// drawn a few times a second and may be hidden, the guidance sees every step.
#pragma once

#include <deque>
#include <limits>
#include <string>
#include <vector>

namespace tantra::guidance {

constexpr double kPi = 3.14159265358979323846, kD2R = kPi / 180.0, kR2D = 180.0 / kPi;
constexpr double kG0 = 9.81;                      // the unit of g (felt load, the g limits) - the mockup's G0
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ---- the ascent's laws (the mockup's constants) ----
constexpr double kTurnScales = 15.0;              // the pitch program: 90 deg -> horizon over 15 scale heights (x^0.6)
constexpr double kRollRate = 8.0, kAttRate = 4.0; // deg/s: near the vertical (pitch > 80) a heading change is a roll
constexpr double kTvcMax = 10.0;                  // deg: the march TVC (the engines screen)
constexpr double kTTail = 2.0;                    // s: the throttle tail-off before a cut-off
constexpr double kHInsGap = 100e3, kHInsMin = 140e3, kHInsMax = 200e3;   // m: the insertion altitude (InsertAlt)
constexpr double kTgoMin = 20.0;                  // s: the floor of the time-to-go in the climb law
constexpr double kCircMargin = 200.0;             // m: a circular orbit ends this close under the burn altitude
constexpr double kCheckDt = 0.35;                 // s a line of the readiness check
constexpr double kConfirm = 3.0;                  // s to confirm ПУСК / ОТМЕНА by a second press
constexpr int kPhaseCount = 9;
extern const wchar_t* const kPhases[kPhaseCount];    // ГОТОВНОСТЬ .. НА ОРБИТЕ
extern const wchar_t* const kLPhases[kPhaseCount];   // ГОТОВНОСТЬ .. ОТСЕЧКА

// ---- the landing's laws (the mockup's LD) ----
constexpr double kLZ0 = 5000.0, kLVz0 = -150.0, kLX0 = -300.0, kLVx0 = 25.0, kLT0 = 360.0;   // the mockup's scenario (plan default)
constexpr double kSternH = 22.5;                  // m: the stern over the ground on the legs
constexpr double kSCG = 58.1, kGyr = 51.0;        // m: the CG over the stern; the radius of gyration (178 m hull)
constexpr double kPodArm = 5.9, kCpArm = 30.9;    // m: the pods' side force arm (pairs at s 44 and 84); the side pressure centre over the CG
constexpr double kCdA = 0.4 * 650.0, kSideCdA = 1.0 * 3500.0;   // m^2: axial drag (as the ascent); the side of the standing hull
constexpr double kBmin = 6.0, kBmax = 12.1, kBrate = 0.8;      // T, T, T/s: the plant's AUTO field range and slew
constexpr double kRateM = 0.20, kRateP = 0.25;    // the throttle slew, of the maximum per second (march, pods)
constexpr double kTvcM = 10.0, kTvcP = 7.0, kTvcRate = 5.0;     // deg: the march TVC, the pods' nozzles; deg/s their slew
constexpr double kLGLim = 5.0, kBrakeK = 0.7, kPodBase = 0.2;   // g interlock; braking plans 70 % of the deceleration; the pods' base share
constexpr double kLegT = 20.0, kLegAlt = 400.0, kLegMin = 300.0, kLegDone = 50.0;   // s, m: the legs' run and the rules
constexpr double kVDesc = 2.0, kVTouch = 1.5, kUnloadT = 8.0;   // m/s, m/s, s
constexpr double kNormVz = 2.0, kNormVx = 0.5, kNormTilt = 2.0; // the touchdown norms: m/s, m/s, deg
constexpr double kLDt = 0.02;                     // s: the landing planner's step

// ---- the plant's cups (tantra_plant_model.js, the numbers of core/Plant and Spec) ----
constexpr double kMu0 = 4e-7 * kPi, kBNom = 12.1, kEtaN = 0.9, kEFus = 7.0e13, kChi = 1e-5;
// (2026-10-09) no central march cup any more: «the march» of the autopilots is the four stern blocks on the charges, as the
// ship has them (Tantra::UpdatePlant: the plant's cup x 4 x Spec kSternPlanAreaRatio x (kSternPlanTesla / 12.1)^2, area and
// power alike - the plant's field B drives them); the pods' cups R 0.55 (ShipParams podThrustTotal: 55 MN a cup at 12.1 T)
constexpr double kSternK = 4.0 * (3.0 * 3.0) / (2.2 * 2.2) * (15.0 / 12.1) * (15.0 / 12.1);
constexpr double kAMarch = kPi * 2.2 * 2.2 * kSternK, kAPod = kPi * 0.55 * 0.55;
constexpr int kNPod = 12;                         // 4 pods x 3 cups
constexpr double kPfMarch = 2.2e14 * kSternK;     // W fusion at 100 %
constexpr double kVArgon = 3.0e4, kVIron = 3.0e5; // m/s: the exhaust of argon (in the air) and iron
constexpr double kSternStore = 1.0e12;            // J the stern soaks (C_STERN = store / 1400 K)
double FieldThrust(double B, double area);        // B^2 / 2mu0 x A
struct Cup { double F = 0, v = 0, mdot = 0, Pf = 0, fuel = 0; int lim = 0; };   // lim: 0 field, 1 power
Cup CupOf(double area, double B, double PfMax, bool iron);   // PM.cup: the reaction mass sets the speed, the field caps the thrust

enum Mode { kIdle = 0, kAuto, kHold, kManual, kAbort, kCrash, kGoAround, kLanded };
enum MassKind { kNoMass = -1, kArgon = 0, kIron = 1 };
enum Limit { kLimNone = 0, kLimG, kLimField, kLimPower, kLimHeat, kLimNoMass };   // what caps the thrust (ОГРАНИЧИВАЕТ)
enum Level { kOk = 0, kWarn = 1, kBad = 2 };
enum EvKey { kEvLiftoff, kEvRoll, kEvPitch, kEvPods, kEvMaxQ, kEvSwitch, kEvDry, kEvMeco, kEvTrim, kEvCirc, kEvOrbit, kEvCrash,
             kEvLegs, kEvLegsDone, kEvBrake, kEvHover, kEvDesc, kEvTouch, kEvUnload, kEvCut };

// ---- the mockup's text: ru-RU numbers (a decimal comma), the clock, the forces ----
std::wstring Fmt(double v, int d);                // fmt
std::wstring FmtS(double v, int d);               // fmtS: + / − / nothing
std::wstring Clock(double s);                     // fT: mm:ss, h:mm:ss
std::wstring Force(double F);                     // fF: ГН / МН / кН
std::wstring Flow(double m);                      // fM: т/с / кг/с / г/с
std::wstring Km(double h);                        // fKm
double AngDiff(double a, double b);               // adiff: a - b wrapped to -180..180 (deg)
double N360(double d);

struct V3 { double x = 0.0, y = 0.0, z = 0.0; };

// the planet: the guidance's own numbers; the atmosphere's two are the planners' (and the pitch program's scale)
struct Planet {
    double R = 6371e3;                            // m: the mean radius (oapiGetSize)
    double mu = kG0 * 6371e3 * 6371e3;            // m^3/s^2 (GGRAV * oapiGetMass)
    double omega = 465.0 / 6371e3;                // rad/s: the rotation (2 pi / oapiGetPlanetPeriod) - 465 m/s on the equator
    double rho0 = 1.225, hs = 8500.0;             // kg/m^3, m: the planners' exponential air; hs also scales the pitch program
    double machA = 300.0;                         // m/s: the planner's speed of sound (M = v / 300)
};

struct Elements { double E = 0.0, a = kNaN, ecc = 0.0, apo = kInf, peri = 0.0, inc = 0.0, tApo = kNaN, hh = 0.0, period = kNaN; };
Elements Elems(const V3& r, const V3& v, double mu, double R);   // inertial r, v (z = the planet's axis); heights over R

struct Event { int key; double t; std::wstring txt; int lvl; double dr, h; };   // h: altitude (ascent) / stern height (landing)
struct Line { std::wstring tt, txt; int lvl; };
struct Check { std::wstring t; bool ok; };
struct Journal {                                  // the mockup's JOURNAL / LJ: newest first, 40 lines
    std::deque<Line> lines;
    void Add(const std::wstring& tt, const std::wstring& txt, int lvl) { lines.push_front({tt, txt, lvl}); if (lines.size() > 40) lines.pop_back(); }
};

// =============================================== THE ASCENT ===============================================

struct Targets {
    double inc = 51.6;                            // deg
    bool south = false;                           // the southbound branch of the launch azimuth
    int peri = 300, apo = 300;                    // km
    double gLim = 3.0;                            // g
};
// inertial azimuth sin(Az) = cos(i) / cos(lat); the launch azimuth corrects it for the ground's speed (the mockup's tgDerive)
struct TargetDerived { double lat = 0, vp = 0, va = 0, vOrb = 0, period = 0, azI = 0, azR = 0, rotV = 0; };
TargetDerived Derive(const Targets& tg, double latRad, const Planet& pl);
double InsertAlt(const Targets& tg);              // m: under the target, 140..200 km: the burn ends there, the apoapsis on the target
double InsertSpeed(const Targets& tg, const Planet& pl);   // m/s at the insertion altitude on the transfer orbit (vis-viva)

// filled by the ship every step
struct AscentState {
    double simt = 0.0, dt = 0.0;                  // s: the sim time, the step
    Planet planet;
    double lat = 0.0, lon = 0.0;                  // rad: the equatorial position (GetEquPos)
    double alt = 0.0;                             // m over the mean radius (GetAltitude(ALTMODE_MEANRAD))
    double vE = 0.0, vN = 0.0, vU = 0.0;          // m/s: the velocity over the rotating ground, local horizon (east, north, up)
    double pitch = 90.0;                          // deg: the hull axis (+z) over the horizon
    double hdg = 0.0;                             // deg: the azimuth of the pitch plane - where the nose tips when pitched down
                                                  // (the horizontal part of +z - y in the horizon frame: defined at the vertical)
    double rho = 0.0, mach = 0.0, q = 0.0;        // the air: kg/m^3, Mach, Pa
    bool contact = true;                          // GroundContact()
    double mass = 52.34e6, argon = 6.2e6, iron = 3.8e6;   // kg
    int reactionMass = kArgon;                    // what the cups run on now (marchHigh_): kArgon, kIron, kNoMass
    double Fmarch = 0.0, Fpods = 0.0;             // N at level 1 now: the march (0 off the run), the pods (0 in the bays / not aimed)
    double FmNow = 0.0, FpNow = 0.0;              // N now
    double mdot = 0.0;                            // kg/s of reaction mass now
    bool plantRun = true;                         // the plant on the run (Plant::Running)
    double field = kBNom, power = 1.0;            // T, fraction of the nominal power (the planner's cups)
    int plantLim = 0;                             // what caps the march (plant Output::limit): 0 поле, 1 мощность, 2 тепло
};

// what the autopilot commands (applied by the ship while the flags say so)
struct AscentCmd {
    bool attitude = false;                        // fly pitch / hdg (auto, hold)
    bool thrust = false;                          // set the engines to `level` (auto, hold, abort; once 0 on a crash)
    bool pods = false;                            // apply podsOut (from ВЗВЕСТИ while the check runs and armed, then auto / hold)
    bool podsOut = false;                         // the pods out with the cups aft (thrust along the hull); in the bays for good after M 0.8
    double pitch = 90.0, hdg = 0.0;               // deg: the commanded hull pitch, the azimuth of the pitch plane
    double roll = 0.0;                            // deg: the roll target of the vertical phases (the launch azimuth once rolling)
    double pitchRate = 0.0, hdgRate = 0.0;        // deg/s: the slew to the commands (1.2 /s and 4 deg/s; 1.5 /s and 8 deg/s at the vertical)
    double tvcPitch = 0.0, tvcYaw = 0.0;          // deg: the march TVC (2 x the error, +-10)
    double thrCmd = 0.0;                          // the guidance's throttle 0..1
    double level = 0.0;                           // the engines' level: thrCmd x the 2 s spool-up, under the g limit (march and pods alike)
    int massWanted = kArgon;                      // argon below 30 km in the air, iron above (the ship's UpdateReactionMass does it)
    int lim = kLimNone;
};

struct Sample { double t, dr, h, v, felt; V3 rE; };   // the track: downrange, altitude, inertial speed, felt g, ground-fixed position

// the mockup's derive(s): everything the guidance and the screen read of where the ship is
struct Kin {
    double rr = 0, h = 0; V3 u, e, n, vAir; double va = 0, vr = 0; V3 vhv; double vh = 0, vgh = 0, rho = 0, gl = 0, lat = 0;
    Elements el; V3 f, p;                         // the target plane's horizontal direction here, across it
    double azT = 0, vAlong = 0, vPerp = 0, azV = 0; V3 rE; double dr = 0, q = 0, mach = 0, gam = 0;
};

// the mockup's sim state minus the physics: what the laws read and write (the live flight and the plan's)
struct Flight {
    Targets tg; TargetDerived td; Planet pl;
    int mode = kIdle, phase = 0;
    double t = 0.0, phT = 0.0;                    // s since ПУСК, s in the phase
    bool lifted = false; double tLift = 0.0;
    bool pods = true; double spool = 0.0;         // the pods still out (until M 0.8); the 2 s spool-up
    double h3 = 0.0; bool rolled = false; int mass = kArgon;
    double pitch = 90.0, hdg = 0.0;               // deg: the attitude (the ship's / the model's)
    double pitchCmd = 90.0, hdgCmd = 0.0, rollCmd = 0.0, thrCmd = 0.0, tvcP = 0.0, tvcY = 0.0;
    double m = 0.0, Favail = 0.0, F = 0.0, thr = 0.0, felt = 0.0;   // kg; N at level 1, N now; the level; g
    double dv = 0.0, maxQ = 0.0, hQ = 0.0; bool qDone = false; double maxG = 0.0;
    double dvCirc = 0.0, tBurn = 0.0, tToBurn = kInf;
    bool tailSet = false; double tailPrev = 0.0;  // the tail-off's last apoapsis (reset with the phase)
    std::vector<Sample> track; double nextSample = 0.0;
    std::vector<Event> ev; double t8 = kNaN;
    double lat0 = 0.0, lon0 = 0.0; V3 r0;         // the stand
};

struct ShipModel {                                // what the planner flies (the mockup's SHIP and its cups)
    double m = 52.34e6, argon = 6.2e6, iron = 3.8e6;
    double B = kBNom, power = 1.0;                // the march cup's field and the plant's power (fraction)
};
struct AscentPlan {
    bool ok = false; double tOrbit = 0, dv = 0, argon = 0, iron = 0, fuel = 0; Elements el; double maxQ = 0, maxG = 0;
    std::wstring why;
    std::vector<Sample> track; std::vector<Event> ev; double tEnd = 0, drEnd = 0;
};
// the mockup's makePlan(): the point-mass model with the same laws, from the stand (lat, alt over R) to orbit + 60 s
AscentPlan PlanAscent(const Targets& tg, double latRad, double alt0, const Planet& pl, const ShipModel& ship);

class Ascent {
public:
    // ---- every step (clbkPreStep) ----
    void Step(const AscentState& st, double sysNow);
    const AscentCmd& Cmd() const { return cmd_; }
    bool Engaged() const { return fl_.mode == kAuto || fl_.mode == kHold || fl_.mode == kAbort; }
    // ---- the keys (sysNow: the system time - the checks and the confirmations run in it) ----
    bool Editable() const { return fl_.mode == kIdle && !armed_ && checkT_ < 0; }
    bool Retargetable() const { return Editable() || Engaged() || fl_.mode == kManual; }   // the targets change in flight too (the user: «горячая корректировка»)
    void StepInc(int dir, bool coarse);           // 0,1° / 5°
    void StepAz(int dir, bool coarse);            // 0,5° / 5° of the inertial azimuth (sets the inclination and the branch)
    void StepPeri(int dir, bool coarse);          // 10 / 50 km, 150..2000, the apoapsis not under it
    void StepApo(int dir, bool coarse);
    void StepGLim(int dir);                       // 0,5 g, 1,5..5
    void Arm(double now);                         // ВЗВЕСТИ / снять взвод
    void Start(double now);                       // ПУСК, ПОДТВЕРДИТЬ within 3 s
    void Hold(double now);                        // УДЕРЖАНИЕ / ПРОДОЛЖИТЬ
    void Manual(double now);                      // РУЧНОЕ
    void Abort(double now);                       // ОТМЕНА, ПОДТВЕРДИТЬ within 3 s
    void Reset(double now);                       // СБРОС (not while the autopilot flies)
    // which keys work (the mockup's enabled conditions)
    bool CanArm() const { return ((fl_.mode == kIdle || fl_.mode == kManual || fl_.mode == kAbort) && checkT_ < 0) || armed_; }
    bool CanStart() const { return armed_; }
    bool CanHold() const { return (fl_.mode == kAuto || fl_.mode == kHold) && fl_.phase < 8; }
    bool CanManual() const { return fl_.mode == kAuto || fl_.mode == kHold || fl_.mode == kAbort || fl_.mode == kManual; }
    bool CanAbort() const { return (fl_.mode == kIdle && armed_) || ((fl_.mode == kAuto || fl_.mode == kHold || fl_.mode == kManual) && fl_.phase < 8); }
    bool CanReset() const { return fl_.mode != kAuto && fl_.mode != kHold; }
    // ---- what the screen shows ----
    const Flight& F() const { return fl_; }
    const Kin& K() const { return k_; }
    const AscentState& State() const { return st_; }
    const AscentPlan& Plan() const { return plan_; }
    const Targets& Tg() const { return tg_; }
    const TargetDerived& Td() const { return fl_.td; }
    double Cross() const { return cross_; }       // m: off the plan's ground track (NaN past its end)
    double SiteLat() const { return (fl_.mode == kIdle ? st_.lat : fl_.lat0) * kR2D; }
    bool Armed() const { return armed_; }
    bool Checking() const { return checkT_ >= 0; }
    bool GoArmed(double now) const { return now - goT_ < kConfirm; }
    bool AbArmed(double now) const { return now - abT_ < kConfirm; }
    std::vector<Check> ChecksShown() const { return checkT_ >= 0 || armed_ ? checks_ : BuildChecks(); }
    int ChecksDone(double now) const { return checkT_ >= 0 ? int((now - checkT_) / kCheckDt) : armed_ ? 99 : -1; }
    const Journal& Log() const { return log_; }
    std::wstring TLabel() const;                  // Т−00:00 on the stand, Т+mm:ss

private:
    void Replan();
    void Retarget();                              // after a target key: the plan, and in flight the live targets
    void FixInc();
    void Tick(double now);                        // the mockup's uiTick: the check's verdict
    std::vector<Check> BuildChecks() const;
    void NewFlight();
    ShipModel Model() const;
    void Note(const std::wstring& txt, int lvl) { log_.Add(TLabel(), txt, lvl); }
    Targets tg_;
    Flight fl_;
    Kin k_;
    AscentState st_;
    AscentCmd cmd_;
    AscentPlan plan_;
    Journal log_;
    bool have_ = false, armed_ = false;
    double checkT_ = -1.0, goT_ = -99.0, abT_ = -99.0, cross_ = 0.0;
    std::vector<Check> checks_;
};

// =============================================== THE LANDING ===============================================

// filled by the ship every step; the horizontal axes are east / north, the guidance turns them into the wind's plane
struct LandState {
    double simt = 0.0, dt = 0.0;
    double g = kG0;                               // m/s^2: the local gravity
    double rho = 1.225;                           // kg/m^3
    double z = kLZ0, vz = kLVz0;                  // m: the stern over the ground; m/s vertical (+ up)
    double xE = 0.0, xN = 0.0;                    // m: from the landing point (east, north)
    double vE = 0.0, vN = 0.0;                    // m/s over the ground
    double thE = 0.0, thN = 0.0;                  // deg: the hull tilted from the vertical toward east / north (the nose's lean)
    double omE = 0.0, omN = 0.0;                  // rad/s: their rates
    double windE = 0.0, windN = 0.0;              // m/s: the wind (where it blows to)
    double mass = 52.34e6, argon = 6.2e6;         // kg
    double Fm = 0.0, Fp = 0.0;                    // N now: the march, the pods
    double B = kBNom;                             // T: the march cup's field now (Plant::Field)
    double legs = 0.0;                            // the stern legs 0 stowed .. 1 on their locks
    bool contact = false;                         // GroundContact()
    double sternT = kLT0;                         // K: the stern (Plant::SternT) - its peak since ПУСК is kept
};

struct LandCmd {
    bool thrust = false, attitude = false;        // apply them (auto, hold, go-around; once 0 at the cut / a crash)
    bool legs = false;                            // the stern legs out (from 400 m)
    double Fcmd = 0.0;                            // N: the thrust demand
    double Fm = 0.0, Fp = 0.0;                    // N to give now: the march, the pods (the split, the field's cap, 20 / 25 %/s)
    double FmC = 0.0, FpC = 0.0;                  // N: the split before the rates (the screen's orange ticks)
    double Breq = kBmin;                          // T: the field the march share needs (AUTO) / the one held (РУЧН)
    double thE = 0.0, thN = 0.0;                  // deg: the tilt command
    double tvcME = 0.0, tvcMN = 0.0;              // deg: the march jet's deflection (5 deg/s), toward east / north
    double tvcPE = 0.0, tvcPN = 0.0;              // deg: the pods' jets' lateral deflection (5 deg/s)
};

struct Axis { double x = 0, vx = 0, th = 0, om = 0, w = 0, Dx = 0, dP = 0, dPcmd = 0, dM = 0, thCmd = 0; };   // one horizontal axis
struct Touch { bool valid = false; double vz = 0, vx = 0, tilt = 0, t = 0; bool legsOk = false, ok = false; };
struct LSample { double t, z, vz, x, F, B, g; };

// the mockup's landing sim state minus the physics (the live one and the plan's)
struct LFlight {
    int mode = kIdle, phase = 0; double t = 0.0, phT = 0.0;
    double hHover = 70.0;
    double g = kG0, rho = 1.225, Dz = 0.0;
    double m = 0.0, argon = 0.0, used = 0.0;
    double z = 0.0, vz = 0.0; Axis ax[2];         // 0 along the wind (the mockup's x), 1 across
    double legs = 0.0; bool legsOn = false, air = true, legsDone = false;
    double Fm = 0.0, Fp = 0.0, B = kBmin, Breq = kBmin, Fcmd = 0.0, FmC = 0.0, FpC = 0.0;
    bool fieldAuto = true; double Bhold = 0.0;
    double dBrake = 0.0, tRamp = 0.0, aB = 0.0, maxG = 0.0, felt = 0.0, F0u = 0.0, zHold = 0.0, wNow = 0.0;
    double Ts = kLT0, maxTs = kLT0;
    Touch td;
    std::vector<LSample> track; double nextS = 0.0;
    std::vector<Event> ev;
};
struct LandPlan {
    std::vector<LSample> track; std::vector<Event> ev;
    bool ok = false; Touch td; double tTouch = kNaN, tEnd = 0, used = 0, maxG = 0, maxTs = 0, brakeZ = kNaN;
    double z0 = kLZ0, vz0 = kLVz0, x0 = kLX0, vx0 = kLVx0;   // where it starts
};
// the mockup's planLand(): the 2-D model with no wind from a start (z, vz, x, vx, mass, argon, thrusts, field, legs)
LandPlan PlanLanding(const LFlight& start);
double MarchMax();                                // N: the march at 12.1 T (L_MARCH_MAX)
double PodsMax();                                 // N: 12 pod cups (L_PODS_MAX)

class Landing {
public:
    void Step(const LandState& st, double sysNow);
    const LandCmd& Cmd() const { return cmd_; }
    bool Engaged() const { return fl_.mode == kAuto || fl_.mode == kHold || fl_.mode == kGoAround; }
    // ---- the keys ----
    bool Editable() const { return fl_.mode == kIdle && !armed_ && checkT_ < 0; }
    bool Retargetable() const { return Editable() || Flying(); }   // the hover height changes in flight too
    void StepHover(int dir);                      // ВЫСОТА ВИСЕНИЯ: 5 m, 60..80
    void Field(bool autoMode, double now);        // the field switch АВТО / РУЧН (the mockup's onField)
    void Arm(double now);
    void Start(double now);
    void GoAround(double now);                    // УХОД
    void Abort(double now);                       // ОТМЕНА -> hover
    void Manual(double now);
    void Reset(double now);
    bool Flying() const { return fl_.mode == kAuto || fl_.mode == kHold || fl_.mode == kManual || fl_.mode == kGoAround; }
    bool CanArm() const { return ((fl_.mode == kIdle || fl_.mode == kManual || fl_.mode == kGoAround || fl_.mode == kHold) && checkT_ < 0) || armed_; }
    bool CanStart() const { return armed_; }
    bool CanGoAround() const { return Flying() && fl_.air && fl_.mode != kGoAround; }
    bool CanAbort() const { return (fl_.mode == kIdle && armed_) || ((fl_.mode == kAuto || fl_.mode == kManual || fl_.mode == kGoAround) && fl_.air); }
    bool CanManual() const { return Flying(); }
    bool CanReset() const { return !Engaged(); }
    // ---- what the screen shows ----
    const LFlight& F() const { return fl_; }
    const LandState& State() const { return st_; }
    const LandPlan& Plan() const { return plan_; }
    double TimeToTouch() const;                   // tToTouch
    double Wind() const { return wind_; }         // m/s measured
    bool Armed() const { return armed_; }
    bool Checking() const { return checkT_ >= 0; }
    bool GoArmed(double now) const { return now - goT_ < kConfirm; }
    bool AbArmed(double now) const { return now - abT_ < kConfirm; }
    std::vector<Check> ChecksShown() const { return checkT_ >= 0 || armed_ ? checks_ : BuildChecks(); }
    int ChecksDone(double now) const { return checkT_ >= 0 ? int((now - checkT_) / kCheckDt) : armed_ ? 99 : -1; }
    const Journal& Log() const { return log_; }
    std::wstring TLabel() const;

private:
    void Replan();
    void Tick(double now);
    std::vector<Check> BuildChecks() const;
    int PhaseFromState() const;
    void Note(const std::wstring& txt, int lvl) { log_.Add(TLabel(), txt, lvl); }
    LFlight fl_;
    LandState st_;
    LandCmd cmd_;
    LandPlan plan_;
    Journal log_;
    double wind_ = 0.0, aE_ = 1.0, aN_ = 0.0;     // the wind's plane: the along axis (east, north)
    bool have_ = false, armed_ = false;
    double checkT_ = -1.0, goT_ = -99.0, abT_ = -99.0;
    std::vector<Check> checks_;
};

}  // namespace tantra::guidance
