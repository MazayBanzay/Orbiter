// TantraGuidance: see TantraGuidance.h. Guide() follows guidance(), SimStep() simStep(), PlanAscent() makePlan() of
// Tantra_Design/tantra_autopilot_screen.html; LandGuide() landGuide(), LandStep() landStep(), PlanLanding() planLand(); the
// keys follow onArm / onStart / onHold / onManual / onAbort and their landing twins. Where the mockup's model reads its own
// state the live guidance reads the ship's (contact for the lift-off and the touchdown, the cups' reaction mass, the thrust).
#include "TantraGuidance.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace tantra::guidance {

const wchar_t* const kPhases[kPhaseCount] = {L"ГОТОВНОСТЬ", L"ВЕРТИКАЛЬНЫЙ ПОДЪЁМ", L"РАЗВОРОТ НА КУРС", L"ПРОГРАММА ТАНГАЖА",
                                             L"СМЕНА РАБОЧЕЙ МАССЫ 30 км", L"РАЗГОН ДО АПОЦЕНТРА", L"БАЛЛИСТИКА", L"СКРУГЛЕНИЕ", L"НА ОРБИТЕ"};
const wchar_t* const kLPhases[kPhaseCount] = {L"ГОТОВНОСТЬ", L"ЗАХОД", L"ТОРМОЖЕНИЕ", L"ВЫПУСК НОГ", L"ВИСЕНИЕ", L"СПУСК", L"КАСАНИЕ",
                                              L"НАГРУЗКА", L"ОТСЕЧКА"};

namespace {

double Clamp(double x, double a, double b) { return (std::max)(a, (std::min)(b, x)); }
double JsRound(double x) { return std::floor(x + 0.5); }   // Math.round: halves up
V3 operator+(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(const V3& a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Len(const V3& a) { return std::sqrt(Dot(a, a)); }
V3 Unit(const V3& a) { const double l = Len(a); return l > 0 ? a * (1.0 / l) : a; }
V3 RotZ(const V3& a, double ang) { const double c = std::cos(ang), s = std::sin(ang); return {c * a.x - s * a.y, s * a.x + c * a.y, a.z}; }
const V3 kZ{0, 0, 1};
const double kRho0 = 1.225, kHs = 8500.0;          // the landing planner's air (the mockup's RHO0, HS)
const double kCStern = kSternStore / 1400.0;       // J/K of the stern

// ============ the ascent ============

void SetPhase(Flight& s, int p) { s.phase = p; s.phT = 0.0; s.tailSet = false; }
void Ev(Flight& s, const Kin& k, int key, const std::wstring& txt, int lvl, Journal* log) {
    s.ev.push_back({key, s.t, txt, lvl, k.dr, k.h});
    if (log) log->Add(L"Т+" + Clock(s.t), txt, lvl);
}

// derive(s): r, v inertial (z = the planet's axis), rE the ground-fixed position (the stand at longitude 0); the air from the
// ship (st) or, in the planner, the mockup's exponential air and M = v / 300
Kin DeriveK(const Flight& s, const V3& r, const V3& v, const V3& rE, const AscentState* st) {
    const Planet& pl = s.pl;
    Kin k;
    k.rr = Len(r); k.h = k.rr - pl.R; k.u = r * (1.0 / k.rr);
    k.e = Unit(Cross(kZ, k.u)); k.n = Cross(k.u, k.e);
    const V3 W{0, 0, pl.omega};
    k.vAir = v - Cross(W, r); k.va = Len(k.vAir);
    k.vr = Dot(v, k.u); k.vhv = v - k.u * k.vr; k.vh = Len(k.vhv);
    k.rho = st ? st->rho : pl.rho0 * std::exp(-(std::max)(0.0, k.h) / pl.hs);
    k.gl = pl.mu / (k.rr * k.rr); k.lat = std::asin(Clamp(k.u.z, -1, 1));
    k.el = Elems(r, v, pl.mu, pl.R);
    // the target orbit's horizontal direction here (the inertial azimuth for this latitude) and the velocity across it; the
    // branch (northbound / southbound) is the launch's at first, then the one the ship flies (it flips past the track's
    // northernmost point)
    const double bN = std::asin(Clamp(std::cos(s.tg.inc * kD2R) / std::cos(k.lat), -1, 1)), bS = kPi - bN;
    const double azV = std::atan2(Dot(k.vhv, k.e), Dot(k.vhv, k.n));
    double b = s.tg.south ? bS : bN, bg = b;
    if (s.lifted && k.vh > 1500) {
        b = std::fabs(AngDiff(azV * kR2D, bN * kR2D)) <= std::fabs(AngDiff(azV * kR2D, bS * kR2D)) ? bN : bS;
        // near the track's northernmost (southernmost) point a plane change costs ~1/cos(u): the steering fades to the velocity
        const double sinI = std::sin(s.tg.inc * kD2R), q = std::sin(k.lat) / (std::max)(1e-6, sinI);
        const double cu = std::sqrt((std::max)(0.0, 1 - q * q));
        bg = azV + Clamp(cu / 0.25, 0, 1) * AngDiff(b * kR2D, azV * kR2D) * kD2R;
    }
    k.f = k.n * std::cos(bg) + k.e * std::sin(bg); k.p = Cross(k.u, k.f);
    const V3 vAirH = k.vAir - k.u * Dot(k.vAir, k.u);
    k.vgh = Len(vAirH);
    k.azT = N360(b * kR2D); k.vAlong = Dot(k.vhv, k.f); k.vPerp = Dot(k.vhv, k.p); k.azV = N360(azV * kR2D);
    // the ground-fixed position: downrange from the stand
    k.rE = rE; k.dr = pl.R * std::atan2(Len(Cross(rE, s.r0)), Dot(rE, s.r0));
    k.q = st ? st->q : 0.5 * k.rho * k.va * k.va;
    k.mach = st ? st->mach : k.va / pl.machA;
    k.gam = std::atan2(k.vr, k.vh) * kR2D;
    return k;
}
// the ship's state in the frame of this instant: x through the stand's meridian, z the axis (ground-fixed = inertial now)
Kin LiveKin(const Flight& s, const AscentState& st) {
    const double rr = st.planet.R + st.alt, dl = st.lon - s.lon0, cl = std::cos(st.lat), sl = std::sin(st.lat);
    const V3 u{cl * std::cos(dl), cl * std::sin(dl), sl}, e{-std::sin(dl), std::cos(dl), 0.0}, n = Cross(u, e), r = u * rr;
    const V3 v = e * (st.vE + st.planet.omega * rr * cl) + n * st.vN + u * st.vU;   // the ground's eastward speed back in
    return DeriveK(s, r, v, r, &st);
}

// the gravity turn: pitch from 90 deg down to the horizon by altitude (ascent()'s program, started where the roll ends)
double PitchProgram(const Flight& s, double h) {
    const double x = (std::max)(0.0, (h - s.h3) / (kTurnScales * s.pl.hs));
    return 90 * (1 - (std::min)(1.0, std::pow(x, 0.6)));
}
// the heading: thrust along the velocity still to be gained in the target plane ("launch azimuth" steering); it pre-corrects
// for the ground's eastward speed and nulls the velocity across the plane by the end of the burn
double YawCmd(const Flight& s, const Kin& k, double vGoal) {
    const double ga = (std::max)(150.0, vGoal - k.vAlong);
    const V3 vg = k.f * ga - k.p * Clamp(k.vPerp, -0.6 * ga, 0.6 * ga);
    return N360(std::atan2(Dot(vg, k.e), Dot(vg, k.n)) * kR2D);
}
// where to continue from when the autopilot is (re)engaged in flight
int PhaseFromState(Flight& s, const Kin& k) {
    const Elements& el = k.el;
    const double apoT = s.tg.apo * 1e3, periT = s.tg.peri * 1e3;
    if (!s.lifted) return 1;
    if (el.apo < apoT - 1e3) {
        if (!s.rolled) { if (k.h < 2000) return 1; s.rollCmd = s.td.azR; return 2; }
        if (s.h3 == 0.0) s.h3 = k.h;
        return s.mass == kArgon ? 3 : 5;
    }
    if (el.peri < (std::min)(periT, el.apo - kCircMargin) - 1e3) return k.vr > 0 ? 6 : 7;
    return 8;
}
// throttle for the last seconds before a cut-off: how fast x moves per unit of acceleration is measured at the current
// thrust, and the thrust is cut so the goal is met in about T_TAIL s (an exponential approach without overshoot)
double TailOff(Flight& s, double x, double goal, double dt) {
    const bool had = s.tailSet; const double prev = s.tailPrev, aNow = s.F / s.m;
    s.tailSet = true; s.tailPrev = x;
    if (!had || aNow < 0.05) return 1;
    const double sens = (x - prev) / dt / aNow;
    if (!(sens > 0)) return 1;
    const double r = (goal - x) / (kTTail * sens) / (std::max)(1e-9, s.Favail / s.m);
    return std::isfinite(r) ? Clamp(r, 0.01, 1) : 0.01;
}
double AvailAcc(const Flight& s) { return (std::max)(1e-6, (std::min)(s.Favail / s.m, s.tg.gLim * kG0)); }

void Guide(Flight& s, const Kin& k, double dt, Journal* log) {
    const Targets& tg = s.tg; const Elements& el = k.el; const Planet& pl = s.pl;
    const double apoT = tg.apo * 1e3, periT = tg.peri * 1e3;
    if (s.mode == kAbort) { s.thrCmd = 0; return; }   // engines to idle, attitude as it was
    if (s.mode != kAuto) return;                      // hold / manual: the last commands stay
    switch (s.phase) {
        case 1:                                       // straight up off the stand to 2 km and 150 m/s
            s.pitchCmd = 90; s.thrCmd = 1; s.hdgCmd = s.rollCmd;
            if (s.lifted && k.h >= 2000 && k.va >= 150) {
                SetPhase(s, 2); s.rollCmd = s.td.azR;
                Ev(s, k, kEvRoll, L"Разворот на курс: азимут пуска " + Fmt(s.td.azR, 1) + L"°", kOk, log);
            }
            break;
        case 2:                                       // roll the pitch plane onto the launch azimuth
            s.pitchCmd = 90; s.thrCmd = 1; s.rollCmd = s.td.azR; s.hdgCmd = s.rollCmd;
            if (std::fabs(AngDiff(s.hdg, s.rollCmd)) < 0.2) {
                s.rolled = true; s.h3 = k.h; SetPhase(s, 3);
                Ev(s, k, kEvPitch, L"Программа тангажа с " + Fmt(k.h / 1e3, 1) + L" км", kOk, log);
            }
            break;
        case 3: case 4: case 5: {                     // gravity turn; argon -> iron at 30 km; burn until the apoapsis is the target
            // above 30 km the pitch never goes below the climb to the insertion altitude: a vertical speed falling linearly to
            // zero as the horizontal speed reaches the transfer orbit's periapsis speed (the ship never sinks while it burns)
            double pc = PitchProgram(s, k.h);
            if (s.phase >= 4) {
                const double gNet = k.gl - k.vh * k.vh / k.rr, aAv = AvailAcc(s) * (std::max)(0.05, s.thrCmd);
                const double hIns = InsertAlt(tg), vIns = InsertSpeed(tg, pl);
                const double tgo = (std::max)(kTgoMin, (vIns - k.vh) / (aAv * (std::max)(0.3, std::cos(s.pitch * kD2R))));
                const double vrDes = 2 * (hIns - k.h) / tgo;
                const double hold = std::asin(Clamp(((vrDes - k.vr) / 10 + gNet) / aAv, -1, 1)) * kR2D;
                pc = (std::max)(pc, Clamp(hold, 0, 60));
            }
            s.pitchCmd = pc; s.hdgCmd = YawCmd(s, k, InsertSpeed(tg, pl));
            s.thrCmd = TailOff(s, el.apo, apoT, dt);
            if (s.phase == 3 && s.mass == kIron) SetPhase(s, 4);
            else if (s.phase == 4 && s.phT >= 3) SetPhase(s, 5);
            if (el.apo >= apoT - 30) {
                s.thrCmd = 0; SetPhase(s, 6);
                Ev(s, k, kEvMeco, L"Отсечка: орбита " + Fmt(el.peri / 1e3, 0) + L" × " + Fmt(el.apo / 1e3, 1) + L" км · до апоцентра " + Clock(el.tApo), kOk, log);
            }
            break;
        }
        case 6: {                                     // coast to the apoapsis; start the burn half its length before
            s.thrCmd = 0; s.pitchCmd = 0; s.hdgCmd = YawCmd(s, k, s.td.vOrb);
            const double ra = pl.R + el.apo, rpT = pl.R + (std::min)(periT, el.apo - kCircMargin), aT = (ra + rpT) / 2;
            s.dvCirc = (std::max)(0.0, std::sqrt(pl.mu * (2 / ra - 1 / aT)) - el.hh / ra);
            s.tBurn = s.dvCirc / AvailAcc(s);
            s.tToBurn = el.tApo - s.tBurn / 2;
            if (el.apo < apoT - 3e3 && k.h < 200e3) { SetPhase(s, 5); Ev(s, k, kEvTrim, L"Апоцентр просел — доразгон", kWarn, log); }
            else if (s.tToBurn <= 0 || (k.vr < 0 && el.tApo > 0.6 * el.period)) {
                SetPhase(s, 7);
                const bool high = el.apo - apoT > 5e3;    // e.g. after a hold through the cut-off: the apoapsis stays where it is
                Ev(s, k, kEvCirc, L"Скругление: Δv " + Fmt(s.dvCirc, 0) + L" м/с · ≈" + Fmt(s.tBurn, 0) + L" с" +
                   (high ? L" · апоцентр " + Fmt(el.apo / 1e3, 0) + L" км выше цели" : std::wstring()), high ? kWarn : kOk, log);
            }
            break;
        }
        case 7: {                                     // circularise: horizontal, the pitch holds the vertical speed at zero
            const double aAv = AvailAcc(s) * (std::max)(0.05, s.thrCmd);
            const double aUp = -k.vr / 20 + k.gl - k.vh * k.vh / k.rr;
            // the speed this point needs for an orbit with its apoapsis here and the periapsis on the target
            const double goal = (std::min)(periT, k.h - kCircMargin), vNeed = std::sqrt(pl.mu * (2 / k.rr - 2 / (k.rr + pl.R + goal)));
            const double dvRem = vNeed - k.vh;
            s.pitchCmd = Clamp(std::asin(Clamp(aUp / aAv, -1, 1)) * kR2D, -20, 30); s.hdgCmd = YawCmd(s, k, vNeed);
            s.thrCmd = Clamp(dvRem / (kTTail * AvailAcc(s)), 0.01, 1);
            if (el.peri >= goal - 30 || dvRem <= 0.02) {
                s.thrCmd = 0; SetPhase(s, 8); s.t8 = s.t;
                Ev(s, k, kEvOrbit, L"НА ОРБИТЕ: " + Fmt(el.peri / 1e3, 1) + L" × " + Fmt(el.apo / 1e3, 1) + L" км · i " + Fmt(el.inc, 2) + L"°", kOk, log);
            }
            break;
        }
        case 8: s.thrCmd = 0; s.pitchCmd = 0; s.hdgCmd = YawCmd(s, k, s.td.vOrb); break;
        default: break;
    }
}

// the attitude's slew rates and the TVC: the mockup's model slews to the commands; here they are what the ship is asked
void Steer(const Flight& s, double* pitchRate, double* hdgRate, double* tvcP, double* tvcY) {
    *pitchRate = Clamp(1.2 * (s.pitchCmd - s.pitch), -kAttRate, kAttRate);
    const double hr = s.pitch > 80 ? kRollRate : kAttRate;   // near vertical a heading change is a roll
    *hdgRate = Clamp(1.5 * AngDiff(s.hdgCmd, s.hdg), -hr, hr);
    *tvcP = Clamp(2 * (s.pitchCmd - s.pitch), -kTvcMax, kTvcMax);
    *tvcY = Clamp(2 * AngDiff(s.hdgCmd, s.hdg) * std::cos(s.pitch * kD2R), -kTvcMax, kTvcMax);
}

// ---- the planner's model: the mockup's point mass over the spherical rotating planet ----
struct Sim {
    Flight f;
    V3 r, v;
    double argon = 0, iron = 0, fuel = 0, dtNext = 0.05;
    ShipModel ship;
    Cup pod;                                          // one pod cup (argon, 12.1 T)
};
Kin SimKin(const Sim& S) { return DeriveK(S.f, S.r, S.v, RotZ(S.r, -S.f.pl.omega * S.f.t), nullptr); }

void SimStep(Sim& S, double dt) {
    Flight& s = S.f; const Planet& pl = s.pl; const Targets& tg = s.tg;
    const Kin k = SimKin(S);
    s.t += dt; s.phT += dt;
    // reaction mass, as ascent(): argon in the air below 30 km, iron above
    const bool air = k.rho > 1e-5 && k.h < 30e3;
    const int mass = air && S.argon > 0 ? kArgon : S.iron > 0 ? kIron : S.argon > 0 ? kArgon : kNoMass;
    if (mass != s.mass) {
        if (s.mass == kArgon && mass == kIron) Ev(s, k, kEvSwitch, Fmt(k.h / 1e3, 1) + L" км — рабочая масса: аргон → железо", kOk, nullptr);
        else if (mass == kNoMass) Ev(s, k, kEvDry, L"Рабочая масса кончилась", kBad, nullptr);
        s.mass = mass;
    }
    // the pods, as ascent(): below M 0.8 and 20 km; once retracted they stay in the bays
    if (s.pods && s.lifted && !(k.mach < 0.8 && k.h < 20e3 && k.rho > 1e-4)) { s.pods = false; Ev(s, k, kEvPods, L"М 0,8 — выдв. блоки убраны · " + Fmt(k.h / 1e3, 2) + L" км", kOk, nullptr); }
    Guide(s, k, dt, nullptr);
    // attitude slews to the commands (near vertical a heading change is a roll)
    s.pitch += Clamp(1.2 * (s.pitchCmd - s.pitch), -kAttRate, kAttRate) * dt;
    const double hr = s.pitch > 80 ? kRollRate : kAttRate;
    s.hdg = N360(s.hdg + Clamp(1.5 * AngDiff(s.hdgCmd, s.hdg), -hr, hr) * dt);
    s.tvcP = Clamp(2 * (s.pitchCmd - s.pitch), -kTvcMax, kTvcMax);
    s.tvcY = Clamp(2 * AngDiff(s.hdgCmd, s.hdg) * std::cos(s.pitch * kD2R), -kTvcMax, kTvcMax);
    // thrust: the march cup + 12 pod cups; the felt-g limit throttles both, as ascent() (the stern's heat never caps it here:
    // with chi 1e-5 the reaction mass carries away a thousand times the radiation)
    const bool hasC = mass != kNoMass;
    const Cup c = hasC ? CupOf(kAMarch, S.ship.B, kPfMarch * S.ship.power, mass == kIron) : Cup();
    const double Fm0 = hasC ? c.F : 0.0, Fp0 = s.pods ? kNPod * S.pod.F : 0.0, F0 = Fm0 + Fp0;
    s.Favail = F0; s.spool = (std::min)(1.0, s.spool + dt / 2);
    double thr = s.thrCmd * s.spool;
    if (F0 > 0 && F0 * thr / s.m > tg.gLim * kG0) thr = tg.gLim * kG0 * s.m / F0;
    const double F = F0 * thr;
    const double mdot = ((hasC ? c.mdot : 0.0) + (s.pods ? kNPod * S.pod.mdot : 0.0)) * thr;
    const double fuelRate = ((hasC ? c.fuel : 0.0) + (s.pods ? kNPod * S.pod.fuel : 0.0)) * thr;
    // forces: thrust along the attitude, drag against the air-relative velocity, central gravity
    const double th = s.pitch * kD2R, ps = s.hdg * kD2R;
    const V3 d = k.u * std::sin(th) + (k.n * std::cos(ps) + k.e * std::sin(ps)) * std::cos(th);
    const double Dr = 0.5 * k.rho * k.va * k.va * kCdA;
    V3 aNG = d * (F / s.m);
    if (k.va > 1e-3) aNG = aNG + k.vAir * (-Dr / s.m / k.va);
    const V3 W{0, 0, pl.omega};
    auto grav = [&](const V3& r) { const double l = Len(r); return r * (-pl.mu / (l * l * l)); };
    if (!s.lifted) {
        // on the stand: off when the thrust beats the weight (less the rotation's centrifugal part)
        const double cl = std::cos(k.lat);
        if (F > 0 && Dot(aNG, k.u) - k.gl + pl.omega * pl.omega * k.rr * cl * cl > 0) { s.lifted = true; s.tLift = s.t; Ev(s, k, kEvLiftoff, L"Отрыв от стола", kOk, nullptr); }
        else { S.r = RotZ(s.r0, pl.omega * s.t); S.v = Cross(W, S.r); }
    }
    if (s.lifted) {
        // velocity Verlet: thrust and drag held over the step, gravity at both ends (keeps the coast orbit exact)
        const V3 vh2 = S.v + (aNG + grav(S.r)) * (dt / 2);
        S.r = S.r + vh2 * dt;
        S.v = vh2 + (aNG + grav(S.r)) * (dt / 2);
    }
    // consumption
    if (mass == kArgon) S.argon = (std::max)(0.0, S.argon - mdot * dt); else if (mass == kIron) S.iron = (std::max)(0.0, S.iron - mdot * dt);
    s.m -= (mdot + fuelRate) * dt; S.fuel += fuelRate * dt; s.dv += F / s.m * dt;
    s.felt = F / s.m / kG0; s.maxG = (std::max)(s.maxG, s.felt);
    s.F = F; s.thr = thr;
    // max dynamic pressure
    if (s.lifted && !s.qDone) {
        if (k.q > s.maxQ) { s.maxQ = k.q; s.hQ = k.h; }
        else if (s.maxQ > 500 && k.q < 0.95 * s.maxQ) { s.qDone = true; Ev(s, k, kEvMaxQ, L"Макс. скоростной напор " + Fmt(s.maxQ / 1e3, 1) + L" кПа · " + Fmt(s.hQ / 1e3, 1) + L" км", kOk, nullptr); }
    }
    // ground impact
    if (s.lifted && Len(S.r) < pl.R && s.t - s.tLift > 1) {
        const double vi = k.va; S.r = Unit(S.r) * pl.R; S.v = Cross(W, S.r); s.mode = kCrash; s.thrCmd = 0;
        Ev(s, k, kEvCrash, L"ПАДЕНИЕ: удар о грунт " + Fmt(vi, 0) + L" м/с", kBad, nullptr);
    }
    // samples for the plots (1 s; sparser after an orbit is reached)
    if (s.t >= s.nextSample) {
        s.track.push_back({s.t, k.dr, k.h, Len(S.v), s.felt, k.rE});
        s.nextSample = s.t + (s.phase == 8 && s.t > s.t8 + 120 ? 10 : 1);
    }
    // coasting high: a longer step; burns and the approach to a burn: 0.05 s
    S.dtNext = s.lifted && F == 0 && k.h > 80e3 && !(s.phase == 6 && s.mode == kAuto && s.tToBurn < 3) ? 0.25 : 0.05;
}

// distance of the actual ground track from the planned one at the same mission time
double CrossFromPlan(const Flight& s, const Kin& k, const AscentPlan& P) {
    const std::vector<Sample>& tr = P.track;
    const int n = int(tr.size());
    if (!s.lifted || n < 2) return 0;
    if (s.t > tr[n - 1].t) return kNaN;               // past the end of the plan
    int i = (std::min)(n - 2, (std::max)(0, int(std::floor(s.t)) - 1));
    while (i > 0 && tr[i].t > s.t) i--;
    while (i < n - 2 && tr[i + 1].t < s.t) i++;
    const Sample& a = tr[i]; const Sample& b = tr[i + 1];
    const double f = Clamp((s.t - a.t) / (std::max)(1e-6, b.t - a.t), 0, 1);
    const V3 rp = a.rE + (b.rE - a.rE) * f, up = Unit(rp);
    V3 dir = b.rE - a.rE; dir = dir - up * Dot(dir, up);
    if (Len(dir) < 1) return 0;
    return Dot(k.rE - rp, Unit(Cross(up, dir)));
}

// ============ the landing ============

double FieldFor(double F) { return std::sqrt(2 * kMu0 * (std::max)(0.0, F) / kAMarch); }   // the field a march thrust needs
Cup MarchCupL(double B) { return CupOf(kAMarch, B, kPfMarch, false); }
Cup PodCup() { return CupOf(kAPod, kBNom, kPfMarch * kAPod / kAMarch, false); }
// the plant's split of a thrust demand: the pods keep a base share (side force), the march takes the rest up to 12.1 T, the
// pods the excess; the field the march share needs (auto range 6...12.1 T)
struct Alloc { double Fm, Fp, B; };
Alloc AllocL(double F) {
    const double base = (std::min)(F, kPodBase * PodsMax()), Fm = (std::min)(F - base, MarchMax());
    return {Fm, (std::min)(PodsMax(), F - Fm), Clamp(FieldFor(Fm), kBmin, kBmax)};
}
void SetPhaseL(LFlight& s, int p) { s.phase = p; s.phT = 0.0; }
void LEv(LFlight& s, int key, const std::wstring& txt, int lvl, Journal* log) {
    s.ev.push_back({key, s.t, txt, lvl, 0.0, s.z});
    if (log) log->Add(L"Т+" + Clock(s.t), txt, lvl);
}
double Tilt(const LFlight& s) { return std::hypot(s.ax[0].th, s.ax[1].th); }
// how long the thrust takes to rise to F: the field climbs 0.8 T/s (the march holds <= B^2/2mu0 x A), the march and the pods
// throttle at 20 and 25 %/s, the pods filling while the field climbs
double RampTime(const LFlight& s, double F) {
    for (double t = 0; t <= 20; t += 0.1) {
        const double Fm = (std::min)(FieldThrust((std::min)(kBmax, s.B + kBrate * t), kAMarch), s.Fm + kRateM * MarchMax() * t);
        const double Fp = (std::min)(PodsMax(), s.Fp + kRateP * PodsMax() * t);
        if (Fm + Fp >= F) return t;
    }
    return 20;
}

void LandGuide(LFlight& s, Journal* log) {
    const double hH = s.hHover; const int mode = s.mode;
    const double Fmax = (std::min)(MarchMax() + PodsMax(), kLGLim * kG0 * s.m), aAv = Fmax / s.m - s.g, aB = kBrakeK * aAv;
    s.aB = aB;
    if (mode == kIdle || mode == kManual || mode == kLanded || mode == kCrash) return;
    if (!s.air) {                                     // on the legs: the load goes over from the thrust to the legs
        for (Axis& a : s.ax) { a.thCmd = 0; a.dPcmd = 0; }
        if (mode != kAuto) return;
        if (s.phase == 6 && s.phT >= 1) { s.F0u = s.Fm + s.Fp; SetPhaseL(s, 7); LEv(s, kEvUnload, L"Нагрузка на ноги: тяга " + Force(s.F0u) + L" → 0 за 8 с", kOk, log); }
        if (s.phase == 7) {
            s.Fcmd = s.F0u * (std::max)(0.0, 1 - s.phT / kUnloadT);
            if (s.phT >= kUnloadT) { s.Fcmd = 0; SetPhaseL(s, 8); s.mode = kLanded; LEv(s, kEvCut, L"ОТСЕЧКА: двигатели выключены, корабль на ногах", kOk, log); }
        }
        return;
    }
    double azDes = 0, latW = 0.25, tiltMax = 2;
    const double xAbs = std::hypot(s.ax[0].x, s.ax[1].x), vxAbs = std::hypot(s.ax[0].vx, s.ax[1].vx);
    if (mode == kGoAround) { azDes = s.vz < 60 ? aAv + 5 : 0.5 * (60 - s.vz); latW = 0.12; }        // full thrust up, then climb at 60 m/s
    else if (mode == kHold) azDes = (std::min)(aAv, 1.0 * (Clamp(0.3 * (s.zHold - s.z), -3, 3) - s.vz));
    else switch (s.phase) {
        case 1: {                                     // approach: hold the descent rate, over the point; brake at the suicide-burn height
            azDes = 0.5 * (kLVz0 - s.vz); latW = 0.1; tiltMax = 5;
            s.tRamp = RampTime(s, s.m * (s.g + aB));
            s.dBrake = s.vz < 0 ? s.vz * s.vz / (2 * aB) - s.vz * s.tRamp / 2 : 0;
            if (s.z - hH <= s.dBrake) {
                SetPhaseL(s, 2);
                LEv(s, kEvBrake, L"Торможение с " + Fmt(s.z, 0) + L" м при " + Fmt(-s.vz, 0) + L" м/с: " + Fmt((aB + s.g) / kG0, 1) + L" g, подъём тяги " + Fmt(s.tRamp, 1) + L" с", kOk, log);
            }
            break;
        }
        case 2: {                                     // braking: the constant deceleration that stops at the hover height (+5 %)
            const double d = s.z - hH;
            azDes = d > 0.5 ? (std::min)(aAv, 1.05 * s.vz * s.vz / (2 * d)) : aAv; tiltMax = 3;
            // the thrust cannot drop at once (20 and 25 %/s): hand over to the hover while the ramp-down still kills the rest
            const double F = s.Fm + s.Fp, aNow = (std::max)(0.0, F / s.m - s.g);
            const double tDown = (std::max)(0.0, F - s.m * s.g) / (kRateM * MarchMax() + kRateP * PodsMax());
            if (s.vz > -(std::max)(2.0, aNow * tDown / 2)) {
                SetPhaseL(s, s.legs >= 1 ? 4 : 3);
                LEv(s, kEvHover, L"Висение " + Fmt(s.z, 0) + L" м" + (s.legs < 1 ? L" · ждём ноги" : L""), kOk, log);
            }
            break;
        }
        case 3: case 4: {                             // hover; the legs, then steady over the point
            const double vzT = s.z > hH ? -(std::min)(std::sqrt(2 * 1.5 * (s.z - hH)), 30.0) : (std::min)(3.0, 0.3 * (hH - s.z));   // settle softly from above
            azDes = 1.0 * (vzT - s.vz);
            if (s.phase == 3 && s.legs >= 1) SetPhaseL(s, 4);
            else if (s.phase == 4 && s.phT >= 4 && xAbs < 1.5 && vxAbs < 0.3 && std::fabs(s.vz) < 0.3 && Tilt(s) < 1) {
                SetPhaseL(s, 5); LEv(s, kEvDesc, L"Спуск " + Fmt(kVDesc, 0) + L" м/с с " + Fmt(s.z, 0) + L" м", kOk, log);
            }
            break;
        }
        case 5: {                                     // descent 2 m/s, the last 8 m at 1.5 m/s
            const bool near = s.z - kSternH * s.legs < 8;
            azDes = 1.0 * ((near ? -kVTouch : -kVDesc) - s.vz); tiltMax = s.z - kSternH < 15 ? 0.5 : 1.5;
            break;
        }
        default: break;
    }
    // the thrust whose vertical part gives azDes against gravity and drag
    s.Fcmd = (std::max)(0.0, (s.m * (s.g + azDes) - s.Dz) / (std::max)(0.5, std::cos(Tilt(s) * kD2R)));
    // over the point: a PD on the drift; the pods' nozzles first, the rest by tilting the hull (each horizontal axis)
    const double sp = std::sin(kTvcP * kD2R);
    for (Axis& a : s.ax) {
        const double ax = Clamp(-latW * latW * a.x - 2 * 0.9 * latW * a.vx, -1.5, 1.5), L = s.m * ax - a.Dx;
        a.dPcmd = s.Fp > 1e6 ? std::asin(Clamp(L / s.Fp, -sp, sp)) * kR2D : 0;
        const double Lrem = L - s.Fp * std::sin(a.dP * kD2R);
        a.thCmd = Clamp(std::asin(Clamp(Lrem / (std::max)(1e6, s.Fm + s.Fp), -0.2, 0.2)) * kR2D, -tiltMax, tiltMax);
    }
}
// the march TVC from a PD on the tilt (its moment about the CG), the pods' nozzles from the side demand; both 5 deg/s
void LandTvc(LFlight& s, double dt) {
    const double I = s.m * kGyr * kGyr, sm = std::sin(kTvcM * kD2R), wa = 0.6;
    for (Axis& a : s.ax) {
        const double Mp = kPodArm * s.Fp * std::sin(a.dP * kD2R), Mw = kCpArm * a.Dx;
        double dMcmd = 0;
        if (s.air && s.Fm > 5e6) {
            const double need = I * (wa * wa * (a.thCmd - a.th) * kD2R - 2 * 0.9 * wa * a.om) - Mp - Mw;
            dMcmd = -std::asin(Clamp(need / (s.Fm * kSCG), -sm, sm)) * kR2D;
        }
        a.dM += Clamp(dMcmd - a.dM, -kTvcRate * dt, kTvcRate * dt);
        a.dP += Clamp((s.air ? a.dPcmd : 0.0) - a.dP, -kTvcRate * dt, kTvcRate * dt);
    }
}
void Touchdown(LFlight& s, Journal* log) {
    const double vz = -s.vz, vx = std::hypot(s.ax[0].vx, s.ax[1].vx), tilt = Tilt(s);
    const bool legsOk = s.legs >= 1;
    s.td.valid = true; s.td.vz = vz; s.td.vx = vx; s.td.tilt = tilt; s.td.legsOk = legsOk; s.td.t = s.t;
    s.td.ok = legsOk && vz <= kNormVz && vx <= kNormVx && tilt <= kNormTilt;
    s.air = false;
    if (!legsOk || vz > 6) {
        s.mode = kCrash; s.Fcmd = 0;
        LEv(s, kEvCrash, !legsOk ? L"АВАРИЯ: ноги не на замках (" + Fmt(s.legs * 100, 0) + L" %) — корма о грунт " + Fmt(vz, 1) + L" м/с"
                                 : L"АВАРИЯ: удар " + Fmt(vz, 1) + L" м/с — ноги сломаны", kBad, log);
        return;
    }
    LEv(s, kEvTouch, L"Касание: верт " + Fmt(vz, 2) + L" м/с · гориз " + Fmt(vx, 2) + L" м/с · наклон " + Fmt(tilt, 2) + L"° — " + (s.td.ok ? L"в норме" : L"ВНЕ НОРМЫ"),
        s.td.ok ? kOk : kWarn, log);
    if (s.mode == kAuto) SetPhaseL(s, 6);
}
// the legs: the autopilot starts them at 400 m (the rules: not below 300 m, locked above 50 m); they run 20 s
void LegsOn(LFlight& s, Journal* log) {
    if (!s.legsOn && s.air && (s.mode == kAuto || s.mode == kHold) && s.z <= kLegAlt) {
        s.legsOn = true; LEv(s, kEvLegs, L"Выпуск ног с " + Fmt(s.z, 0) + L" м (20 с)", s.z >= kLegMin ? kOk : kWarn, log);
    }
}
void LegsDone(LFlight& s, Journal* log) {
    if (s.legsOn && !s.legsDone && s.legs >= 1) { s.legsDone = true; LEv(s, kEvLegsDone, L"Ноги на замках · " + Fmt(s.z, 0) + L" м", s.z >= kLegDone ? kOk : kWarn, log); }
}

// landStep(): the planner's model - stern height z, drift x along one axis, tilt th with its rate; the plant runs the field
// (AUTO 6..12.1 T at 0.8 T/s) and the capsule rates; argon only (30 km/s); the stern's heat as the plant screen
void LandStep(LFlight& s, double dt) {
    s.t += dt; s.phT += dt;
    Axis& a = s.ax[0];
    s.rho = kRho0 * std::exp(-(std::max)(0.0, s.z) / kHs); s.wNow = 0.0;   // the plan: no wind
    s.Dz = -0.5 * s.rho * s.vz * std::fabs(s.vz) * kCdA;
    const double rx = s.wNow - a.vx; a.Dx = 0.5 * s.rho * rx * std::fabs(rx) * kSideCdA;
    LegsOn(s, nullptr);
    if (s.legsOn && s.legs < 1) s.legs = (std::min)(1.0, s.legs + dt / kLegT);
    LegsDone(s, nullptr);
    LandGuide(s, nullptr);
    // the plant: AUTO runs the field and the capsule rate under the thrust demand; MANUAL holds the operator's field
    const double Fd = Clamp(s.Fcmd, 0, (std::min)(MarchMax() + PodsMax(), kLGLim * kG0 * s.m));
    const Alloc al = AllocL(Fd);
    s.Breq = s.fieldAuto ? al.B : s.Bhold;
    if (s.fieldAuto) s.B += Clamp(s.Breq - s.B, -kBrate * dt, kBrate * dt);
    const Cup cm = MarchCupL(s.B); const double cap = cm.F;
    s.FmC = (std::min)(al.Fm, cap); s.FpC = Clamp(Fd - s.FmC, 0, PodsMax());   // the pods carry what the field does not hold yet
    s.Fm = (std::min)(cap, s.Fm + Clamp(s.FmC - s.Fm, -kRateM * MarchMax() * dt, kRateM * MarchMax() * dt));
    s.Fp += Clamp(s.FpC - s.Fp, -kRateP * PodsMax() * dt, kRateP * PodsMax() * dt);
    LandTvc(s, dt);
    const double I = s.m * kGyr * kGyr, Mp = kPodArm * s.Fp * std::sin(a.dP * kD2R), Mw = kCpArm * a.Dx;
    const double aM = (a.th + a.dM) * kD2R, aP = (a.th + a.dP) * kD2R;
    const double Fx = s.Fm * std::sin(aM) + s.Fp * std::sin(aP) + a.Dx, Fz = s.Fm * std::cos(aM) + s.Fp * std::cos(aP) + s.Dz;
    if (s.air) {
        a.om += (-s.Fm * std::sin(a.dM * kD2R) * kSCG + Mp + Mw) / I * dt; a.th += a.om * dt * kR2D;
        a.vx += Fx / s.m * dt; s.vz += (Fz / s.m - s.g) * dt; a.x += a.vx * dt; s.z += s.vz * dt;
        if (s.z <= kSternH * s.legs) { Touchdown(s, nullptr); s.z = kSternH * s.legs; s.vz = 0; a.vx = 0; a.om = 0; }
    } else if (s.mode != kAuto && s.mode != kLanded && s.mode != kCrash && Fz > s.m * s.g) s.air = true;   // off the legs again
    // argon and fuel
    const Cup pc = PodCup();
    const double F = s.Fm + s.Fp, mdot = F / kVArgon, fuel = (cm.F > 0 ? s.Fm / cm.F * cm.fuel : 0.0) + s.Fp / pc.F * pc.fuel;
    s.argon = (std::max)(0.0, s.argon - mdot * dt); s.used += mdot * dt; s.m -= (mdot + fuel) * dt;
    s.felt = F / s.m / kG0; s.maxG = (std::max)(s.maxG, s.felt);
    // the stern: jet off the ground + the reaction's radiation against the jacket and the crests
    const double Pjet = F * kVArgon / 2, fg = 20 / (20 + (std::max)(0.0, s.z)), qG = Pjet * 0.002 * fg * fg;
    const double c1 = 15 / std::hypot(15.0, 2.2), c2 = 15 / std::hypot(15.0, 15.0);   // PM.sternFraction(15)
    const double qR = kChi * F * kVArgon / (2 * kEtaN) * (c1 - c2) / 2;
    const double regen = mdot * 1.5e6, rad = 0.9 * 5.670e-8 * std::pow(s.Ts, 4) * 2700;
    s.Ts = (std::max)(290.0, s.Ts + (qG + qR - regen - rad) * dt / kCStern); s.maxTs = (std::max)(s.maxTs, s.Ts);
    if (s.t >= s.nextS) { s.track.push_back({s.t, s.z, s.vz, a.x, F, s.B, s.felt}); s.nextS = s.t + 0.2; }
}

}  // namespace

// ============ the text and the shared helpers ============

std::wstring Fmt(double v, int d) {
    if (!std::isfinite(v)) return L"—";
    wchar_t b[64];
    std::swprintf(b, 64, L"%.*f", d, std::fabs(v));
    std::wstring s(b), ip = s, fp;
    const size_t dot = s.find(L'.');
    if (dot != std::wstring::npos) { ip = s.substr(0, dot); fp = s.substr(dot + 1); }
    std::wstring g;
    const int n = int(ip.size());
    for (int i = 0; i < n; ++i) { g += ip[i]; const int rest = n - 1 - i; if (rest > 0 && rest % 3 == 0) g += L' '; }
    bool neg = false;
    if (v < 0) for (wchar_t c : s) if (c >= L'1' && c <= L'9') neg = true;
    return (neg ? L"-" : L"") + g + (fp.empty() ? std::wstring() : L"," + fp);
}
std::wstring FmtS(double v, int d) { return (v > 0 ? L"+" : v < 0 ? L"−" : L"") + Fmt(std::fabs(v), d); }
std::wstring Clock(double s) {
    if (!std::isfinite(s)) return L"—";
    const long long t = (std::max)(0LL, (long long)JsRound(s)), h = t / 3600, m = t % 3600 / 60, x = t % 60;
    wchar_t b[48];
    if (h) std::swprintf(b, 48, L"%lld:%02lld:%02lld", h, m, x);
    else std::swprintf(b, 48, L"%02lld:%02lld", m, x);
    return b;
}
std::wstring Force(double F) { return F >= 1e9 ? Fmt(F / 1e9, 2) + L" ГН" : F >= 1e6 ? Fmt(F / 1e6, 0) + L" МН" : F >= 1e3 ? Fmt(F / 1e3, 0) + L" кН" : L"0"; }
std::wstring Flow(double m) { return m >= 1000 ? Fmt(m / 1000, 2) + L" т/с" : m >= 1 ? Fmt(m, 1) + L" кг/с" : m > 0 ? Fmt(m * 1000, 0) + L" г/с" : L"0"; }
std::wstring Km(double h) { return std::fabs(h) < 10e3 ? Fmt(h / 1e3, 2) + L" км" : Fmt(h / 1e3, 1) + L" км"; }
double AngDiff(double a, double b) { double x = std::fmod(a - b + 540.0, 360.0); if (x < 0) x += 360.0; return x - 180.0; }
double N360(double d) { double x = std::fmod(d, 360.0); if (x < 0) x += 360.0; return x; }

double FieldThrust(double B, double area) { return B * B / (2 * kMu0) * area; }
Cup CupOf(double area, double B, double PfMax, bool iron) {
    Cup c;
    const double Ffield = FieldThrust(B, area);
    // iron: 300 km/s, slower when the field allows more thrust than the power gives at that speed
    double v = kVArgon;
    if (iron) v = (std::max)(kVArgon, (std::min)(kVIron, Ffield > 0 ? 2 * kEtaN * PfMax * (1 - kChi) / Ffield : kVIron));
    double Pf = PfMax, F = 2 * kEtaN * Pf * (1 - kChi) / v;
    c.lim = 1;
    if (F > Ffield) { F = Ffield; Pf = F * v / (2 * kEtaN * (1 - kChi)); c.lim = 0; }
    c.F = F; c.v = v; c.mdot = F / v; c.Pf = Pf; c.fuel = Pf / kEFus;
    return c;
}
double MarchMax() { return CupOf(kAMarch, kBNom, kPfMarch, false).F; }
double PodsMax() { return kNPod * CupOf(kAPod, kBNom, kPfMarch * kAPod / kAMarch, false).F; }

// orbital elements from the state: apoapsis / periapsis heights, inclination, time to apoapsis
Elements Elems(const V3& r, const V3& v, double mu, double R) {
    Elements o;
    const double rr = Len(r), vv = Len(v);
    const V3 hv = Cross(r, v);
    const double hh = Len(hv);
    o.hh = hh;
    o.E = vv * vv / 2 - mu / rr;
    o.ecc = std::sqrt((std::max)(0.0, 1 + 2 * o.E * hh * hh / (mu * mu)));
    o.inc = std::acos(Clamp(hh > 0 ? hv.z / hh : 1.0, -1, 1)) * kR2D;
    if (o.E < 0) {
        o.a = -mu / (2 * o.E); o.apo = o.a * (1 + o.ecc) - R; o.peri = o.a * (1 - o.ecc) - R;
        const double n = std::sqrt(mu / (o.a * o.a * o.a)); o.period = 2 * kPi / n;
        if (o.ecc > 1e-4) {
            double nu = std::acos(Clamp((o.a * (1 - o.ecc * o.ecc) / rr - 1) / o.ecc, -1, 1));
            if (Dot(r, v) < 0) nu = 2 * kPi - nu;
            const double EA = 2 * std::atan(std::sqrt((1 - o.ecc) / (1 + o.ecc)) * std::tan(nu / 2));
            double M = EA - o.ecc * std::sin(EA);
            if (M < 0) M += 2 * kPi;
            o.tApo = std::fmod((kPi - M) + 2 * kPi, 2 * kPi) / n;
        }
    } else o.peri = hh * hh / mu / (1 + o.ecc) - R;
    return o;
}

TargetDerived Derive(const Targets& tg, double lat, const Planet& pl) {
    TargetDerived d;
    const double rp = pl.R + tg.peri * 1e3, ra = pl.R + tg.apo * 1e3, a = (rp + ra) / 2;
    d.lat = lat;
    d.vp = std::sqrt(pl.mu * (2 / rp - 1 / a)); d.va = std::sqrt(pl.mu * (2 / ra - 1 / a)); d.vOrb = std::sqrt(pl.mu / a);
    double b = std::asin(Clamp(std::cos(tg.inc * kD2R) / std::cos(lat), -1, 1));
    if (tg.south) b = kPi - b;
    d.rotV = pl.omega * pl.R * std::cos(lat);
    const double vE = d.vOrb * std::sin(b) - d.rotV, vN = d.vOrb * std::cos(b);
    d.period = 2 * kPi * std::sqrt(a * a * a / pl.mu);
    d.azI = N360(b * kR2D); d.azR = N360(std::atan2(vE, vN) * kR2D);
    return d;
}
// the insertion altitude: below the target, the burn ends there with the apoapsis on the target (a short circularisation at
// the apoapsis half an orbit later; above ~140 km the drag is nil for this ship)
double InsertAlt(const Targets& tg) { return Clamp((std::min)(tg.peri, tg.apo) * 1e3 - kHInsGap, kHInsMin, kHInsMax); }
double InsertSpeed(const Targets& tg, const Planet& pl) { const double rI = pl.R + InsertAlt(tg); return std::sqrt(pl.mu * (2 / rI - 2 / (rI + pl.R + tg.apo * 1e3))); }

// ============ the ascent's plan ============

AscentPlan PlanAscent(const Targets& tg, double lat, double alt0, const Planet& pl, const ShipModel& ship) {
    Sim S;
    Flight& s = S.f;
    s.tg = tg; s.pl = pl; s.td = Derive(tg, lat, pl);
    const double r0 = pl.R + alt0;
    s.r0 = {r0 * std::cos(lat), 0.0, r0 * std::sin(lat)}; s.lat0 = lat; s.lon0 = 0.0;
    S.r = s.r0; S.v = Cross(V3{0, 0, pl.omega}, s.r0);
    s.m = ship.m; S.argon = ship.argon; S.iron = ship.iron; S.ship = ship; S.pod = PodCup();
    s.pitch = 90; s.hdg = 0; s.pitchCmd = 90; s.hdgCmd = 0; s.rollCmd = 0;   // the model stands with its pitch plane to the north
    s.mass = kArgon; s.pods = true;
    s.mode = kAuto; SetPhase(s, 1); s.t = 0;
    int guard = 0;
    while (s.phase < 8 && s.mode != kCrash && s.t < 7200 && guard++ < 300000) SimStep(S, S.dtNext);
    AscentPlan P;
    P.ok = s.phase == 8;
    const Kin k = SimKin(S);
    P.tOrbit = s.t; P.dv = s.dv; P.argon = ship.argon - S.argon; P.iron = ship.iron - S.iron; P.fuel = S.fuel; P.el = k.el;
    P.maxQ = s.maxQ; P.maxG = s.maxG;
    P.why = P.ok ? L"" : s.mode == kCrash ? L"падение" : L"время вышло";
    const double t8 = s.t;
    while (P.ok && s.t < t8 + 60 && guard++ < 300000) SimStep(S, S.dtNext);   // a little of the orbit for the plot
    P.track = s.track; P.ev = s.ev; P.tEnd = s.t; P.drEnd = s.track.empty() ? 0.0 : s.track.back().dr;
    return P;
}

// ============ the live ascent ============

std::wstring Ascent::TLabel() const { return fl_.mode == kIdle ? std::wstring(L"Т−00:00") : L"Т+" + Clock(fl_.t); }

ShipModel Ascent::Model() const {
    ShipModel m;
    if (!have_) return m;
    m.m = st_.mass; m.argon = st_.argon; m.iron = st_.iron;
    m.B = st_.plantRun && st_.field > 0 ? st_.field : kBNom;   // a plant not on the run: planned as it will be (the check says so)
    m.power = st_.power > 0 ? st_.power : 1.0;
    return m;
}

void Ascent::NewFlight() {
    Flight f;
    f.tg = tg_; f.pl = st_.planet; f.td = Derive(tg_, st_.lat, st_.planet);
    f.lat0 = st_.lat; f.lon0 = st_.lon;
    const double r0 = st_.planet.R + st_.alt;
    f.r0 = {r0 * std::cos(st_.lat), 0.0, r0 * std::sin(st_.lat)};
    f.pitch = st_.pitch; f.hdg = st_.hdg;
    f.pitchCmd = 90; f.hdgCmd = f.rollCmd = st_.hdg;   // STAND_HDG: the pitch plane as the ship stands
    f.m = st_.mass; f.mass = st_.reactionMass; f.pods = true;
    fl_ = f;
}

// A target changed: the plan anew; flying, the live flight takes the new targets at once (its target plane from the stand).
void Ascent::Retarget() {
    Replan();
    if (fl_.mode == kIdle) return;
    fl_.tg = tg_; fl_.td = Derive(tg_, fl_.lat0, fl_.pl);
    Note(L"Цели изменены в полёте: i " + Fmt(tg_.inc, 1) + L"°, " + std::to_wstring(tg_.peri) + L" × " + std::to_wstring(tg_.apo) + L" км, предел " + Fmt(tg_.gLim, 1) + L" g", kOk);
}

void Ascent::FixInc() { const double lat = std::fabs(SiteLat()); tg_.inc = Clamp(tg_.inc, lat, 180 - lat); }

void Ascent::Replan() {
    FixInc();
    plan_ = PlanAscent(tg_, st_.lat, st_.alt, st_.planet, Model());
    if (fl_.mode == kIdle) NewFlight();
}

void Ascent::Tick(double now) {
    if (checkT_ >= 0 && now - checkT_ > checks_.size() * kCheckDt + 0.3) {
        checkT_ = -1;
        const Check* bad = nullptr;
        for (const Check& c : checks_) if (!c.ok) { bad = &c; break; }
        if (bad) { armed_ = false; Note(L"Готовность не подтверждена: " + bad->t, kBad); }
        else { armed_ = true; Note(fl_.mode == kIdle ? L"Автопилот взведён — ПУСК разрешён" : L"Автопилот взведён — ПУСК вернёт управление автомату", kOk); }
    }
}

std::vector<Check> Ascent::BuildChecks() const {
    if (fl_.mode != kIdle) {
        Flight c = fl_;
        const int ph = PhaseFromState(c, k_);
        return {{L"Установка на режиме", st_.plantRun},
                {L"Рабочая масса: аргон " + Fmt(st_.argon / 1e6, 2) + L" · железо " + Fmt(st_.iron / 1e6, 2) + L" кт", st_.argon + st_.iron > 0},
                {L"Продолжение с фазы " + Fmt(ph, 0), true}};
    }
    const TargetDerived& td = fl_.td; const AscentPlan& P = plan_;
    const ShipModel sm = Model();
    const double lat = std::fabs(SiteLat()), gS = st_.planet.mu / (st_.planet.R * st_.planet.R);
    const double tw = (CupOf(kAMarch, sm.B, kPfMarch * sm.power, false).F + PodsMax()) / (sm.m * gS);
    return {{L"Установка на режиме · поле " + Fmt(st_.field, 1) + L" Тл", st_.plantRun},
            {L"Тяга/вес на столе " + Fmt(tw, 2) + L" (чаша + 4 выдвижных блока)", tw > 1.2},
            {L"Аргон " + Fmt(sm.argon / 1e6, 2) + L" кт · по плану " + Fmt(P.argon / 1e6, 2) + L" кт", P.argon < sm.argon},
            {L"Железо " + Fmt(sm.iron / 1e6, 2) + L" кт · по плану " + Fmt(P.iron / 1e6, 2) + L" кт", P.iron < sm.iron},
            {L"Курс " + Fmt(td.azR, 1) + L"° · i " + Fmt(tg_.inc, 1) + L"° не ниже широты " + Fmt(lat, 1) + L"°", tg_.inc >= lat - 1e-9},
            {L"Перицентр " + Fmt(tg_.peri, 0) + L" км — выше атмосферы", tg_.peri >= 150},
            {P.ok ? L"План: орбита через " + Clock(P.tOrbit) + L" · Δv " + Fmt(P.dv / 1e3, 2) + L" км/с" : L"План: орбита не достигается (" + P.why + L")", P.ok}};
}

void Ascent::Step(const AscentState& st, double now) {
    const bool first = !have_;
    st_ = st; have_ = true;
    if (first) {
        Replan();
        Note(st.contact ? L"Корабль на столе. Задайте цель и нажмите ВЗВЕСТИ" : L"Задайте цель и нажмите ВЗВЕСТИ", kOk);
    }
    Tick(now);
    Flight& s = fl_;
    cmd_.attitude = cmd_.thrust = false;
    if (s.mode == kIdle) {                            // on the stand / not engaged: the flight follows the ship
        NewFlight();
        k_ = LiveKin(fl_, st);
        cross_ = 0;
        cmd_ = AscentCmd();
        cmd_.pods = cmd_.podsOut = armed_ || checkT_ >= 0;   // the pods come out (12 s) while the check runs: the plan counts them from the start
        cmd_.pitch = 90; cmd_.hdg = cmd_.roll = st.hdg;
        return;
    }
    s.pl = st.planet; s.m = st.mass; s.pitch = st.pitch; s.hdg = st.hdg;
    s.F = st.FmNow + st.FpNow;
    if (s.mode == kCrash) {                           // stopped (the step of the impact cut the engines): only what the screen shows
        k_ = LiveKin(s, st);
        cmd_.level = 0; cmd_.thrCmd = 0;
        return;
    }
    const double dt = st.dt;
    s.t += dt; s.phT += dt;
    k_ = LiveKin(s, st);
    const Kin& k = k_;
    // the reaction mass: the ship switches it itself (argon in the air below 30 km, iron above: UpdateReactionMass); the
    // phases and the journal follow what it runs on
    const bool air = k.rho > 1e-5 && k.h < 30e3;
    cmd_.massWanted = air && st.argon > 0 ? kArgon : st.iron > 0 ? kIron : st.argon > 0 ? kArgon : kNoMass;
    if (st.reactionMass != s.mass) {
        if (s.mass == kArgon && st.reactionMass == kIron) Ev(s, k, kEvSwitch, Fmt(k.h / 1e3, 1) + L" км — рабочая масса: аргон → железо", kOk, &log_);
        else if (st.reactionMass == kNoMass) Ev(s, k, kEvDry, L"Рабочая масса кончилась", kBad, &log_);
        s.mass = st.reactionMass;
    }
    // off the table: the ship's contact
    if (!s.lifted && !st.contact) { s.lifted = true; s.tLift = s.t; Ev(s, k, kEvLiftoff, L"Отрыв от стола", kOk, &log_); }
    // the pods: below M 0.8 and 20 km; once retracted they stay in the bays
    if (s.pods && s.lifted && !(k.mach < 0.8 && k.h < 20e3 && k.rho > 1e-4)) { s.pods = false; Ev(s, k, kEvPods, L"М 0,8 — выдв. блоки убраны · " + Fmt(k.h / 1e3, 2) + L" км", kOk, &log_); }
    s.Favail = st.Fmarch + (s.pods ? st.Fpods : 0.0);
    Guide(s, k, dt, &log_);
    Steer(s, &cmd_.pitchRate, &cmd_.hdgRate, &s.tvcP, &s.tvcY);
    // the throttle: the 2 s spool-up and the felt-g limit (march and pods alike)
    s.spool = (std::min)(1.0, s.spool + dt / 2);
    const double F0 = s.Favail;
    double thr = s.thrCmd * s.spool;
    int lim = s.mass == kNoMass ? kLimNoMass : st.plantLim == 1 ? kLimPower : st.plantLim == 2 ? kLimHeat : kLimField;
    if (F0 > 0 && F0 * thr / s.m > s.tg.gLim * kG0) { thr = s.tg.gLim * kG0 * s.m / F0; lim = kLimG; }
    if (thr <= 0) lim = kLimNone;
    s.thr = thr;
    // what the engines gave
    s.dv += s.F / s.m * dt;
    s.felt = s.F / s.m / kG0; s.maxG = (std::max)(s.maxG, s.felt);
    // max dynamic pressure
    if (s.lifted && !s.qDone) {
        if (k.q > s.maxQ) { s.maxQ = k.q; s.hQ = k.h; }
        else if (s.maxQ > 500 && k.q < 0.95 * s.maxQ) { s.qDone = true; Ev(s, k, kEvMaxQ, L"Макс. скоростной напор " + Fmt(s.maxQ / 1e3, 1) + L" кПа · " + Fmt(s.hQ / 1e3, 1) + L" км", kOk, &log_); }
    }
    // back on the ground
    bool crashNow = false;
    if (s.lifted && st.contact && s.t - s.tLift > 1) {
        s.mode = kCrash; s.thrCmd = 0; s.thr = 0; crashNow = true;
        Ev(s, k, kEvCrash, L"ПАДЕНИЕ: удар о грунт " + Fmt(k.va, 0) + L" м/с", kBad, &log_);
    }
    // samples for the plots (1 s; sparser after an orbit is reached)
    if (s.t >= s.nextSample) {
        s.track.push_back({s.t, k.dr, k.h, std::hypot(k.vh, k.vr), s.felt, k.rE});
        s.nextSample = s.t + (s.phase == 8 && s.t > s.t8 + 120 ? 10 : 1);
    }
    cross_ = CrossFromPlan(s, k, plan_);
    // the commands
    const int m = s.mode;
    cmd_.attitude = m == kAuto || m == kHold;
    cmd_.thrust = m == kAuto || m == kHold || m == kAbort || crashNow;
    cmd_.pods = m == kAuto || m == kHold;
    cmd_.podsOut = s.pods;
    cmd_.pitch = s.pitchCmd; cmd_.hdg = s.hdgCmd; cmd_.roll = s.rollCmd;
    cmd_.tvcPitch = s.tvcP; cmd_.tvcYaw = s.tvcY;
    cmd_.thrCmd = s.thrCmd; cmd_.level = m == kAbort || m == kCrash ? 0.0 : thr; cmd_.lim = lim;
}

// ---- the keys ----
void Ascent::StepInc(int dir, bool coarse) {
    if (!Retargetable()) return;
    tg_.inc = coarse ? JsRound(tg_.inc + dir * 5) : JsRound((tg_.inc + dir * 0.1) * 100) / 100;
    FixInc(); Retarget();
}
void Ascent::StepAz(int dir, bool coarse) {
    if (!Retargetable()) return;
    const double lat = SiteLat() * kD2R, b = (Derive(tg_, lat, st_.planet).azI + dir * (coarse ? 5.0 : 0.5)) * kD2R;
    tg_.inc = JsRound(std::acos(Clamp(std::cos(lat) * std::sin(b), -1, 1)) * kR2D * 100) / 100;
    tg_.south = !(std::cos(b) >= -1e-9);
    FixInc(); Retarget();
}
void Ascent::StepPeri(int dir, bool coarse) {
    if (!Retargetable()) return;
    tg_.peri = int(Clamp(tg_.peri + dir * (coarse ? 50 : 10), 150, 2000)); tg_.apo = (std::max)(tg_.apo, tg_.peri);
    Retarget();
}
void Ascent::StepApo(int dir, bool coarse) {
    if (!Retargetable()) return;
    tg_.apo = int(Clamp(tg_.apo + dir * (coarse ? 50 : 10), 150, 2000)); tg_.peri = (std::min)(tg_.peri, tg_.apo);
    Retarget();
}
void Ascent::StepGLim(int dir) {
    if (!Retargetable()) return;
    tg_.gLim = Clamp(tg_.gLim + dir * 0.5, 1.5, 5);
    Retarget();
}
void Ascent::Arm(double now) {
    if (!CanArm()) return;
    if (armed_) { armed_ = false; Note(L"Взвод снят", kWarn); return; }
    if (fl_.mode == kIdle && have_) Replan();         // the plan from where the ship is now
    checks_ = BuildChecks(); checkT_ = now; Note(L"Проверка готовности…", kWarn);
}
void Ascent::Start(double now) {
    if (!armed_) { Note(L"ПУСК заблокирован: сначала ВЗВЕСТИ", kWarn); return; }
    if (now - goT_ > kConfirm) { goT_ = now; Note(L"ПУСК: подтвердите в течение 3 с", kWarn); return; }
    goT_ = -99; armed_ = false;
    if (fl_.mode == kIdle) {
        NewFlight();
        Flight& s = fl_;
        s.mode = kAuto; SetPhase(s, 1); s.t = 0;
        if (have_ && !st_.contact) {                  // already off the table: the program goes on from where the flight is
            s.lifted = true; k_ = LiveKin(s, st_); SetPhase(s, PhaseFromState(s, k_));
            Note(L"Автопилот включён: " + Fmt(s.phase, 0) + L" " + kPhases[s.phase], kOk);
            return;
        }
        log_.Add(L"Т+00:00", L"ПУСК · цель " + Fmt(tg_.peri, 0) + L" × " + Fmt(tg_.apo, 0) + L" км, i " + Fmt(tg_.inc, 2) + L"°, азимут " + Fmt(s.td.azR, 1) + L"°", kOk);
    } else {
        Flight& s = fl_;
        s.mode = kAuto; SetPhase(s, PhaseFromState(s, k_));
        Note(L"Автопилот включён: " + Fmt(s.phase, 0) + L" " + kPhases[s.phase], kOk);
    }
}
void Ascent::Hold(double /*now*/) {
    if (!CanHold()) return;
    Flight& s = fl_;
    if (s.mode == kAuto) { s.mode = kHold; Note(L"Удержание: тангаж " + Fmt(s.pitchCmd, 1) + L"°, курс " + Fmt(s.hdgCmd, 1) + L"°, тяга " + Fmt(s.thrCmd * 100, 0) + L" %", kWarn); }
    else if (s.mode == kHold) { s.mode = kAuto; SetPhase(s, PhaseFromState(s, k_)); Note(L"Программа продолжена: " + Fmt(s.phase, 0) + L" " + kPhases[s.phase], kOk); }
}
void Ascent::Manual(double /*now*/) {
    if (!CanManual()) return;
    if (fl_.mode == kManual) { Note(L"Вернуть автомат: ВЗВЕСТИ → ПУСК", kWarn); return; }
    fl_.mode = kManual; armed_ = false; Note(L"РУЧНОЕ: управление передано пилоту (тяга и ориентация — как были)", kWarn);
}
void Ascent::Abort(double now) {
    if (!CanAbort()) return;
    if (now - abT_ > kConfirm) { abT_ = now; Note(L"ОТМЕНА: подтвердите в течение 3 с", kWarn); return; }
    abT_ = -99;
    if (fl_.mode == kIdle) { armed_ = false; Note(L"Пуск отменён, взвод снят", kBad); return; }
    fl_.mode = kAbort; fl_.thrCmd = 0; armed_ = false;
    Note(L"ОТМЕНА: двигатели на холостом ходу, управление — пилоту", kBad);
}
void Ascent::Reset(double /*now*/) {
    if (!CanReset()) return;
    armed_ = false; checkT_ = -1; goT_ = abT_ = -99; log_.lines.clear();
    fl_.mode = kIdle;
    if (have_) Replan();
    Note(!have_ || st_.contact ? L"Сброс: корабль на столе" : L"Сброс: автопилот выключен", kOk);
}

// ============ the landing's plan ============

LandPlan PlanLanding(const LFlight& start) {
    LFlight s = start;
    s.mode = kAuto; SetPhaseL(s, 1); s.t = 0; s.track.clear(); s.ev.clear(); s.nextS = 0; s.air = true;
    s.legsOn = s.legsDone = false; s.used = 0; s.maxG = 0; s.td = Touch(); s.maxTs = s.Ts; s.fieldAuto = true;
    s.ax[1] = Axis();
    while (s.mode == kAuto && s.t < 900) LandStep(s, kLDt);
    LandPlan P;
    const Event* tch = nullptr; const Event* brk = nullptr;
    for (const Event& e : s.ev) { if (e.key == kEvTouch && !tch) tch = &e; if (e.key == kEvBrake && !brk) brk = &e; }
    P.track = s.track; P.ev = s.ev;
    P.ok = s.mode == kLanded && s.td.valid && s.td.ok; P.td = s.td;
    P.tTouch = tch ? tch->t : kNaN; P.tEnd = s.t; P.used = s.used; P.maxG = s.maxG; P.maxTs = s.maxTs; P.brakeZ = brk ? brk->h : kNaN;
    P.z0 = start.z; P.vz0 = start.vz; P.x0 = start.ax[0].x; P.vx0 = start.ax[0].vx;
    return P;
}

// ============ the live landing ============

std::wstring Landing::TLabel() const { return fl_.mode == kIdle ? std::wstring(L"Т−00:00") : L"Т+" + Clock(fl_.t); }

int Landing::PhaseFromState() const { if (!fl_.air) return 8; if (fl_.z - fl_.hHover > 60) return 1; return fl_.legs >= 1 ? 4 : 3; }

double Landing::TimeToTouch() const {
    const LFlight& s = fl_;
    if (!s.air) return 0;
    const double hH = s.hHover, legsLeft = (1 - s.legs) * kLegT, desc = (hH - kSternH - 8) / kVDesc + 8 / kVTouch;
    const double tB = s.aB > 0 ? (std::max)(0.0, -s.vz) / s.aB : 0;
    switch (s.mode == kAuto ? s.phase : -1) {
        case 1: return (std::max)(0.0, (s.z - hH - s.dBrake) / (std::max)(1.0, -s.vz)) + tB + (std::max)(0.0, legsLeft - tB - 2) + 4 + desc;
        case 2: return tB + (std::max)(0.0, legsLeft - tB) + 4 + desc;
        case 3: return legsLeft + 4 + desc;
        case 4: return (std::max)(0.0, 4 - s.phT) + desc;
        case 5: return (std::max)(0.0, s.z - kSternH) / (std::max)(0.5, -s.vz);
        default: return kNaN;
    }
}

// the plan from where the ship is (the descent along the line to the point); on the ground or before the first state the
// mockup's scenario: 5 km, 150 m/s down, 300 m short at 25 m/s, in a steady descent
void Landing::Replan() {
    LFlight s;
    s.hHover = fl_.hHover;
    s.m = have_ ? st_.mass : 52.34e6; s.argon = have_ ? st_.argon : 6.2e6; s.g = have_ ? st_.g : kG0;
    s.Ts = s.maxTs = have_ ? st_.sternT : kLT0;
    if (have_ && !st_.contact) {
        const double d = std::hypot(st_.xE, st_.xN);
        s.z = st_.z; s.vz = st_.vz;
        s.ax[0].x = -d; s.ax[0].vx = d > 1 ? -(st_.vE * st_.xE + st_.vN * st_.xN) / d : 0.0;
        s.legs = st_.legs; s.Fm = st_.Fm; s.Fp = st_.Fp; s.B = st_.B;
    } else {
        s.z = kLZ0; s.vz = kLVz0; s.ax[0].x = kLX0; s.ax[0].vx = kLVx0;
        const double Dz = 0.5 * kRho0 * std::exp(-s.z / kHs) * s.vz * s.vz * kCdA;
        s.Fcmd = s.m * s.g - Dz;
        const Alloc a = AllocL(s.Fcmd);
        s.Fm = a.Fm; s.Fp = a.Fp; s.B = s.Breq = a.B;
    }
    plan_ = PlanLanding(s);
}

void Landing::Tick(double now) {
    if (checkT_ >= 0 && now - checkT_ > checks_.size() * kCheckDt + 0.3) {
        checkT_ = -1;
        const Check* bad = nullptr;
        for (const Check& c : checks_) if (!c.ok) { bad = &c; break; }
        if (bad) { armed_ = false; Note(L"Готовность не подтверждена: " + bad->t, kBad); }
        else { armed_ = true; Note(fl_.mode == kIdle ? L"Автопилот посадки взведён — ПУСК разрешён" : L"Взведён — ПУСК вернёт посадку автомату", kOk); }
    }
}

std::vector<Check> Landing::BuildChecks() const {
    const LFlight& s = fl_;
    if (s.mode != kIdle)
        return {{L"Поле: АВТО", s.fieldAuto}, {L"Аргон " + Fmt(s.argon / 1e6, 2) + L" кт", s.argon > 0.3e6}, {L"Продолжение с фазы " + Fmt(PhaseFromState(), 0), true}};
    const LandPlan& P = plan_;
    const double W = (std::max)(1.0, s.m * s.g), tw = (MarchMax() + PodsMax()) / W;
    return {{L"Поле: АВТО — поле и темп капсул ведёт установка", s.fieldAuto},
            {L"Тяга/вес " + Fmt(tw, 2) + L" (чаша " + Fmt(MarchMax() / W, 2) + L" + выдвижные блоки)", tw > 1.5},
            {L"Аргон " + Fmt(s.argon / 1e6, 2) + L" кт · по плану " + Fmt(P.used / 1e6, 2) + L" кт", P.used < s.argon},
            {L"Ноги убраны, приводы ног в норме", true},
            {P.ok ? L"План: касание Т+" + Clock(P.tTouch) + L" · " + Fmt(P.td.vz, 1) + L" м/с · пик " + Fmt(P.maxG, 1) + L" g" : std::wstring(L"План: посадка вне норм"), P.ok}};
}

void Landing::Step(const LandState& st, double now) {
    const bool first = !have_;
    st_ = st; have_ = true;
    // the wind's plane: along the wind (the mockup's x), across it; with no wind the line to the point
    wind_ = std::hypot(st.windE, st.windN);
    if (wind_ > 0.5) { aE_ = st.windE / wind_; aN_ = st.windN / wind_; }
    else if (first) { const double d = std::hypot(st.xE, st.xN); if (d > 1) { aE_ = -st.xE / d; aN_ = -st.xN / d; } }
    if (first) {
        Replan();
        Note(st.contact ? std::wstring(L"Поле — АВТО. Нажмите ВЗВЕСТИ")
                        : L"Снижение " + Fmt(st.z / 1e3, 1) + L" км, " + Fmt(-st.vz, 0) + L" м/с. Поле — АВТО. Нажмите ВЗВЕСТИ", kOk);
    }
    Tick(now);
    LFlight& s = fl_;
    const double cE = -aN_, cN = aE_;                 // across: 90 deg left of along
    s.g = st.g; s.rho = st.rho; s.m = st.mass; s.argon = st.argon; s.z = st.z; s.vz = st.vz; s.legs = st.legs;
    s.Fm = st.Fm; s.Fp = st.Fp; s.B = st.B; s.Ts = st.sternT; s.wNow = wind_;
    Axis& A = s.ax[0]; Axis& C = s.ax[1];
    A.x = st.xE * aE_ + st.xN * aN_; A.vx = st.vE * aE_ + st.vN * aN_; A.th = st.thE * aE_ + st.thN * aN_; A.om = st.omE * aE_ + st.omN * aN_; A.w = wind_;
    C.x = st.xE * cE + st.xN * cN; C.vx = st.vE * cE + st.vN * cN; C.th = st.thE * cE + st.thN * cN; C.om = st.omE * cE + st.omN * cN; C.w = 0;
    LandCmd out;
    if (s.mode == kIdle) {                            // not engaged: what the screen shows
        s.air = !st.contact; s.aB = (std::min)(MarchMax() + PodsMax(), kLGLim * kG0 * s.m) / s.m * kBrakeK - s.g * kBrakeK;
        cmd_ = out;
        return;
    }
    if (s.mode == kLanded || s.mode == kCrash) {      // done (the step of the cut / the impact gave the engines zero)
        cmd_ = out;
        return;
    }
    const double dt = st.dt;
    s.t += dt; s.phT += dt;
    s.Dz = -0.5 * s.rho * s.vz * std::fabs(s.vz) * kCdA;
    for (Axis& a : s.ax) { const double rx = a.w - a.vx; a.Dx = 0.5 * s.rho * rx * std::fabs(rx) * kSideCdA; }
    LegsOn(s, &log_); LegsDone(s, &log_);
    // the touchdown and off the legs again: the ship's contact
    if (s.air && st.contact) Touchdown(s, &log_);
    else if (!s.air && s.mode != kAuto && s.mode != kLanded && s.mode != kCrash && !st.contact) s.air = true;
    LandGuide(s, &log_);
    const bool ended = s.mode == kLanded || s.mode == kCrash;
    // the plant: the demand split, the field the march share needs (AUTO) or the operator's, the capsule rates from what is
    // given now; the march never over what the present field holds
    const double Fd = Clamp(s.Fcmd, 0, (std::min)(MarchMax() + PodsMax(), kLGLim * kG0 * s.m));
    const Alloc al = AllocL(Fd);
    s.Breq = s.fieldAuto ? al.B : s.Bhold;
    const double cap = MarchCupL(st.B).F;
    s.FmC = (std::min)(al.Fm, cap); s.FpC = Clamp(Fd - s.FmC, 0, PodsMax());
    const double Fm = (std::min)(cap, st.Fm + Clamp(s.FmC - st.Fm, -kRateM * MarchMax() * dt, kRateM * MarchMax() * dt));
    const double Fp = Clamp(st.Fp + Clamp(s.FpC - st.Fp, -kRateP * PodsMax() * dt, kRateP * PodsMax() * dt), 0, PodsMax());
    {   // the TVC acts on what will be given
        const double fm = s.Fm, fp = s.Fp;
        s.Fm = Fm; s.Fp = Fp; LandTvc(s, dt); s.Fm = fm; s.Fp = fp;
    }
    // argon, the load, the stern
    const double F = st.Fm + st.Fp;
    s.used += F / kVArgon * dt;
    s.felt = F / s.m / kG0; s.maxG = (std::max)(s.maxG, s.felt);
    s.maxTs = (std::max)(s.maxTs, st.sternT);
    if (s.t >= s.nextS) { s.track.push_back({s.t, s.z, s.vz, A.x, F, st.B, s.felt}); s.nextS = s.t + 0.2; }
    // the commands
    const int m = s.mode;
    out.thrust = m == kAuto || m == kHold || m == kGoAround || ended;
    out.attitude = (m == kAuto || m == kHold || m == kGoAround) && s.air;
    out.legs = s.legsOn;
    out.Fcmd = s.Fcmd; out.FmC = s.FmC; out.FpC = s.FpC; out.Breq = s.Breq;
    out.Fm = ended ? 0.0 : Fm; out.Fp = ended ? 0.0 : Fp;
    out.thE = A.thCmd * aE_ + C.thCmd * cE; out.thN = A.thCmd * aN_ + C.thCmd * cN;
    out.tvcME = A.dM * aE_ + C.dM * cE; out.tvcMN = A.dM * aN_ + C.dM * cN;
    out.tvcPE = A.dP * aE_ + C.dP * cE; out.tvcPN = A.dP * aN_ + C.dP * cN;
    cmd_ = out;
}

// ---- the keys ----
void Landing::StepHover(int dir) {
    if (!Retargetable()) return;
    fl_.hHover = Clamp(fl_.hHover + 5 * dir, 60, 80);
    if (Flying()) Note(L"Высота висения в полёте: " + Fmt(fl_.hHover, 0) + L" м", kOk);
    Replan();
}
void Landing::Field(bool autoMode, double /*now*/) {
    LFlight& s = fl_;
    if (s.fieldAuto == autoMode) return;
    s.fieldAuto = autoMode;
    if (autoMode) { Note(L"Поле: АВТО — поле и темп капсул ведёт установка", kOk); return; }
    s.Bhold = have_ ? st_.B : s.B;
    if (armed_ || checkT_ >= 0) { armed_ = false; checkT_ = -1; Note(L"Поле в РУЧН — взвод снят: посадка только в автоматическом режиме поля", kBad); }
    if (s.mode == kAuto || s.mode == kHold) { s.mode = kGoAround; Note(L"Поле переведено в РУЧН (" + Fmt(s.Bhold, 2) + L" Тл) — автопилот: УХОД, полная тяга вверх", kBad); }
    else Note(L"Поле: РУЧН · держится " + Fmt(s.Bhold, 2) + L" Тл", kWarn);
}
void Landing::Arm(double now) {
    if (!CanArm()) return;
    if (armed_) { armed_ = false; Note(L"Взвод снят", kWarn); return; }
    if (!fl_.fieldAuto) { Note(L"ВЗВОД ОТКАЗАН: посадка только в автоматическом режиме поля", kBad); return; }
    if (fl_.mode == kIdle) Replan();
    checks_ = BuildChecks(); checkT_ = now; Note(L"Проверка готовности…", kWarn);
}
void Landing::Start(double now) {
    if (!armed_) { Note(L"ПУСК заблокирован: сначала ВЗВЕСТИ", kWarn); return; }
    if (!fl_.fieldAuto) { armed_ = false; Note(L"ПУСК отказан: посадка только в автоматическом режиме поля", kBad); return; }
    if (now - goT_ > kConfirm) { goT_ = now; Note(L"ПУСК: подтвердите в течение 3 с", kWarn); return; }
    goT_ = -99; armed_ = false;
    LFlight& s = fl_;
    if (s.mode == kIdle) {                            // a fresh run (the mockup's newLand + startLand)
        s.t = 0; s.track.clear(); s.ev.clear(); s.nextS = 0; s.used = 0; s.maxG = 0; s.td = Touch(); s.maxTs = s.Ts;
        s.legsOn = s.legsDone = false; s.air = !st_.contact; s.dBrake = s.tRamp = 0; s.F0u = 0;
        for (Axis& a : s.ax) a.dM = a.dP = a.dPcmd = a.thCmd = 0;
        s.mode = kAuto; SetPhaseL(s, 1);
        log_.Add(L"Т+00:00", L"ПУСК посадки · висение " + Fmt(s.hHover, 0) + L" м · ветер " + Fmt(wind_, 0) + L" м/с", kOk);
    } else {
        s.mode = kAuto; SetPhaseL(s, PhaseFromState());
        Note(L"Автопилот посадки включён: " + Fmt(s.phase, 0) + L" " + kLPhases[s.phase], kOk);
    }
}
void Landing::GoAround(double /*now*/) {
    if (!CanGoAround()) return;
    fl_.mode = kGoAround; armed_ = false; Note(L"УХОД: полная тяга вверх", kBad);
}
void Landing::Abort(double now) {
    if (!CanAbort()) return;
    if (now - abT_ > kConfirm) { abT_ = now; Note(L"ОТМЕНА: подтвердите в течение 3 с", kWarn); return; }
    abT_ = -99;
    if (fl_.mode == kIdle) { armed_ = false; Note(L"Посадка отменена, взвод снят", kBad); return; }
    fl_.mode = kHold; fl_.zHold = (std::max)(fl_.z, fl_.hHover); armed_ = false;
    Note(L"ОТМЕНА посадки: остановка снижения, висение ~" + Fmt(fl_.zHold, 0) + L" м", kBad);
}
void Landing::Manual(double /*now*/) {
    if (!CanManual()) return;
    if (fl_.mode == kManual) { Note(L"Вернуть автомат: ВЗВЕСТИ → ПУСК", kWarn); return; }
    fl_.mode = kManual; armed_ = false; Note(L"РУЧНОЕ: управление передано пилоту (тяга и наклон — как были)", kWarn);
}
void Landing::Reset(double /*now*/) {
    if (!CanReset()) return;
    log_.lines.clear();
    const double hH = fl_.hHover; const bool fa = fl_.fieldAuto; const double bh = fl_.Bhold;
    fl_ = LFlight(); fl_.hHover = hH; fl_.fieldAuto = fa; fl_.Bhold = bh;
    if (have_) { fl_.z = st_.z; fl_.vz = st_.vz; fl_.m = st_.mass; fl_.argon = st_.argon; fl_.air = !st_.contact; fl_.Ts = fl_.maxTs = st_.sternT; }
    armed_ = false; checkT_ = -1; goT_ = abT_ = -99;
    Replan();
    Note(have_ && st_.contact ? std::wstring(L"Сброс: корабль на ногах") : L"Сброс: снижение " + Fmt(fl_.z / 1e3, 1) + L" км, " + Fmt(-fl_.vz, 0) + L" м/с", kOk);
}

}  // namespace tantra::guidance
