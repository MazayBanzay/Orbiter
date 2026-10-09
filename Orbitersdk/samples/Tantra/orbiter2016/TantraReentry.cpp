// TantraReentry: see TantraReentry.h. The laws (Regulate, the burn's tail-off, the phases) run both live (Reentry::Step, on the
// ship's state) and in the planner (PlanReentry, on its point mass); the forecasts are the thermal page's (RunForecast).
#include "TantraReentry.h"

#include <algorithm>
#include <cmath>

namespace tantra::reentry {

using guidance::Fmt;
using guidance::Clock;
using guidance::Km;
using guidance::AngDiff;
using guidance::kPi;
using guidance::kD2R;
using guidance::kR2D;
using guidance::kG0;
using guidance::kOk;
using guidance::kWarn;
using guidance::kBad;

const wchar_t* const kRPhases[kRPhaseCount] = {L"ГОТОВНОСТЬ", L"ОЖИДАНИЕ ТОЧКИ СХОДА", L"РАЗВОРОТ НА ТОРМОЖЕНИЕ", L"ИМПУЛЬС СХОДА",
                                               L"РАЗВОРОТ НА ВХОД", L"ВХОД", L"РАЗВОРОТ КОРМОЙ ВНИЗ", L"ТОРМОЖЕНИЕ", L"ВХОД ПОСАДКИ"};
const wchar_t* const kRegimes[kRegimeCount] = {L"КОРМОЙ · ТЯГА", L"КОРМОЙ · БАЛЛИСТИКА", L"НОСОМ · КРЫЛЬЯ"};
const wchar_t* const kWingSel[kWingSelCount] = {L"АВТО", L"90°", L"30°", L"СЛОЖЕНЫ"};
const wchar_t* const kZoneRu[kZones] = {L"нос", L"днище", L"кромки крыльев", L"перо", L"корма", L"ноги", L"выдвижные блоки"};
const wchar_t* const kLimRu[6] = {L"—", L"перегрузка", L"нагрев", L"корма установки", L"торможение", L"рабочая масса"};

namespace {

namespace ts = tantra::thermalscreen;
namespace dm = tantra::damage;

double Clamp(double x, double a, double b) { return (std::max)(a, (std::min)(b, x)); }
V3 operator+(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(const V3& a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Len(const V3& a) { return std::sqrt(Dot(a, a)); }
V3 Unit(const V3& a) { const double l = Len(a); return l > 0 ? a * (1.0 / l) : a; }
V3 RotZ(const V3& a, double ang) { const double c = std::cos(ang), s = std::sin(ang); return {c * a.x - s * a.y, s * a.x + c * a.y, a.z}; }
V3 RotAx(const V3& a, const V3& k, double ang) {   // Rodrigues: a turned about the unit k
    const double c = std::cos(ang), s = std::sin(ang);
    return a * c + Cross(k, a) * s + k * (Dot(k, a) * (1 - c));
}
double Ang(const V3& a, const V3& b) { return std::atan2(Len(Cross(a, b)), Dot(a, b)); }
V3 Fixed(double lat, double lon) { return {std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)}; }
const V3 kZ{0, 0, 1};

ts::Body BodyOf(const Planet& pl) { ts::Body b; b.R = pl.R; b.g0 = pl.mu / (pl.R * pl.R); return b; }
// the corridor's periapsis: where the air is `share` of the surface density (20 km .. the entry interface - 10 km)
double HpOf(const ts::Air& air, double share) { return Clamp(air.Alt(air.Rho(0.0) * share), 20e3, kEI - 10e3); }
int ShipWing(int sel) { return sel == kW90 ? 0 : sel == kW30 ? 1 : 2; }
double ExhaustOf(int mass) { return mass == guidance::kIron ? guidance::CupOf(guidance::kAMarch, guidance::kBNom, guidance::kPfMarch, true).v : guidance::kVArgon; }
// what the cups run on: argon in the air below 30 km (the limiter allows nothing else there), iron above
int MassFor(double rho, double h, double argon, double iron) {
    const bool air = rho > 1e-5 && h < 30e3;
    if (air) return argon > 0 ? guidance::kArgon : guidance::kNoMass;
    return iron > 0 ? guidance::kIron : argon > 0 ? guidance::kArgon : guidance::kNoMass;
}
dm::Exposure ExpoFor(dm::Exposure x, int wing, double pods) {   // the wings and the fin fold together (tuck_)
    x.crests = x.fin = wing == 2 ? 0.0 : 1.0; x.pods = pods; x.gear = 0.0;
    return x;
}

// ---- the regulation's look-ahead: the descent at a constant level ----
struct Look {
    double m = 1.0, aUse = 0.0, gCap = 30.0;      // kg, m/s^2: the usable acceleration (the engines under the g limit), the g limit
    const ts::Air* air = nullptr; ts::Body b;
    bool glide = false; double bank = 0.0, crest = 0.0;   // НОСОМ: nose first at 40 deg to the flip (M 2, 15 km), kFlipT unpowered
};
double LookDt(double h) { return h > 100e3 ? 8.0 : h > 40e3 ? 4.0 : h > 15e3 ? 2.0 : 1.0; }
// from (h, v, gamma) at the level L of the usable acceleration (the thrust against the airspeed, the load under gCap): the airspeed
// at the alignment's height; < 0 - it stops above, by how much (/100 m); range: the ground covered. Planar, non-rotating
double TermRun(double L, double h, double v, double gam, const Look& k, double* range = nullptr) {
    const double R = k.b.R, aoaN = kAoaNose * kD2R, cb = std::cos(k.bank * kD2R);
    bool glide = k.glide; double flip = k.glide ? kFlipT : 0.0, s = 0.0;
    const double h0 = h;
    for (int i = 0; i < 20000 && h > kAlignH; ++i) {
        if (v < 30.0) { if (range) *range = s; return -(h - kAlignH) / 100.0; }
        if (gam > 0.0 && h > h0 + 1000.0) break;  // back up past the start: it does not come down at this level
        if (glide && v / kSoundPlan < kFlipMach && h < kFlipH) glide = false;
        const double dt = LookDt(h);
        const double rr = R + h, gl = k.b.g0 * (R / rr) * (R / rr), q = h < 150e3 ? 0.5 * k.air->Rho(h) * v * v : 0.0;
        double SL = 0.0, SD = 0.0;
        if (q > 1e-3) { if (glide) ts::AeroAreas(aoaN, v / kSoundPlan, k.crest, 0.0, &SL, &SD); else ts::AeroAreas(kPi, v / kSoundPlan, 0.0, 0.0, &SL, &SD); }
        const double aD = q * SD / k.m, aL = glide ? q * SL / k.m * cb : 0.0;
        double aT = 0.0;
        if (!glide) { if (flip > 0.0) flip -= dt; else aT = Clamp(L * k.aUse, 0.0, (std::max)(0.0, k.gCap - aD)); }
        v += (-aD - aT - gl * std::sin(gam)) * dt;
        const double vv = (std::max)(30.0, v);
        gam = Clamp(gam + (aL / vv - (gl / vv - vv / rr) * std::cos(gam)) * dt, -kPi / 2, kPi / 2);
        h += vv * std::sin(gam) * dt;
        s += vv * std::cos(gam) * R / rr * dt;
    }
    if (range) *range = s;
    return v;
}
// the constant level (of the usable) that reaches kAlignV at kAlignH; > 1: short of it even at full (1 + the excess / 1000 m/s).
// The bisection starts round the last answer (the need changes slowly)
double TermLevel(double h, double v, double gam, const Look& k, double hint) {
    if (k.aUse <= 0.0) return 2.0;
    if (h <= kAlignH) return 0.0;
    auto f = [&](double L) { return TermRun(L, h, v, gam, k); };
    const double f1 = f(1.0);
    if (f1 > kAlignV) return 1.0 + (std::min)(1.0, (f1 - kAlignV) / 1000.0);
    double lo = 0.0, hi = 1.0;
    bool bracket = false;
    if (hint > 0.03 && hint < 0.97) {
        const double a = hint - 0.03, b = hint + 0.03;
        if (f(a) > kAlignV && f(b) <= kAlignV) { lo = a; hi = b; bracket = true; }
    }
    if (!bracket && f(0.0) <= kAlignV) return 0.0;
    while (hi - lo > 0.004) { const double mid = 0.5 * (lo + hi); if (f(mid) > kAlignV) lo = mid; else hi = mid; }
    return hi;
}

// ---- the forecasts' verdict ----
struct Verdict { bool ok = false; int hot = -1; double margin = -1e9, g = 0.0; };
Verdict Judge(const ts::Forecast& f, const dm::Model& skin, double gLim) {
    Verdict v;
    for (int z = 0; z < kZones; ++z) {
        const double mz = skin.Limit(z) - kMarginK - f.peakT[z];
        if (mz < v.margin || v.hot < 0) { v.margin = mz; v.hot = z; }
    }
    v.g = f.nMax;
    v.ok = v.margin >= 0.0 && f.nMax <= gLim + 1e-6 && !f.skip;
    return v;
}

// what the regulation reads (live: from the ship; the planner: from its model)
struct RegIn {
    int regime = kRStern, wingSel = kWAuto; double gLim = 3.0;
    bool rangeCtl = true, site = false;
    double h = 0, va = 0, gam = 0, m = 1;         // m, m/s, rad (the airspeed's), kg
    V3 vL;                                        // m/s: the airspeed in the local horizon (east, north, up)
    double gl = 9.81;                             // m/s^2: the local gravity
    double aMarch = 0, aPods = 0;                 // m/s^2 at level 1 (the pods 0 unless out)
    double aAero = 0;                             // m/s^2: the aerodynamic load now
    double rangeGo = 0, xAng = 0;                 // m to the site over the ground; rad: the site off the track (+ right)
    const ts::Air* air = nullptr; ts::Body body;
    const dm::Model* skin = nullptr; dm::Exposure expo; double gearArea = 0;
    double hotMargin = 1e9;                       // K: the hottest zone under its limit - margin now
    double sternT = 300, tSafe = 800;
    double t = 0, evalDt = 1.0;
};
double AUse(const RegIn& in) { return (std::max)(1e-6, (std::min)(in.aMarch + in.aPods, in.gLim * kG0)); }

ts::Forecast Fc(const RegIn& in, double aoaDeg, double bankDeg, int wing, double thrustAcc) {
    ts::ForecastIn f;
    ts::Path& p = f.path;
    p.h = in.h; p.v = in.va; p.gamma = in.gam; p.mass = in.m;
    p.aoa = aoaDeg * kD2R; p.bank = bankDeg * kD2R;
    p.crestAvail = kCrestOf[wing]; p.gearArea = in.gearArea;
    p.expo = ExpoFor(in.expo, wing, in.expo.pods);
    p.air = *in.air; p.body = in.body; p.thrustAcc = thrustAcc;
    f.skin = *in.skin;
    return ts::RunForecast(f);
}

constexpr double kLv[7] = {0.0, 0.1, 0.2, 0.35, 0.5, 0.7, 1.0};   // КОРМОЙ · ТЯГА: the candidate levels (of the usable)
constexpr double kBankAlt[4] = {60.0, 80.0, 100.0, 120.0};          // НОСОМ: the steeper banks when the lift would skip out

// phase 4 (coasting down, the stern regimes): the look-ahead only - the braking may start above the air; 5 the entry: the
// forecasts' candidates once an evaluation; 7 the braking, then the alignment; 8 (handed over) the alignment holds 150 m/s down
void Regulate(Reg& g, const RegIn& in, int phase) {
    const double gCap = in.gLim * kG0, aUse = AUse(in);
    if (in.t >= g.nextEval) {
        g.nextEval = in.t + in.evalDt;
        Look k; k.m = in.m; k.aUse = aUse; k.gCap = gCap; k.air = in.air; k.b = in.body;
        if (in.regime == kRNose && phase == 5) { k.glide = true; k.bank = std::fabs(g.bank); k.crest = kCrestOf[g.wing]; }
        if (!g.align) g.lTerm = TermLevel(in.h, in.va, in.gam, k, g.lTerm);
        if (in.regime == kRStern && (phase == 5 || phase == 7) && !g.align && in.h < kEI) {
            // the cheapest held level whose forecast keeps every zone under its limit - margin and the load under the g limit; the
            // search starts next to the last choice
            int last = 0;
            for (int i = 0; i < 7; ++i) if (kLv[i] <= g.lHeat + 1e-9) last = i;
            auto pass = [&](int i, Verdict* out) { const Verdict v = Judge(Fc(in, 180.0, 0.0, 2, kLv[i] * aUse), *in.skin, in.gLim); *out = v; return v.ok; };
            Verdict v, w;
            int i = (std::max)(0, last - 1);
            if (pass(i, &v)) { while (i > 0 && pass(i - 1, &w)) { --i; v = w; } }
            else { while (i < 6) { ++i; if (pass(i, &v)) break; } }
            g.lHeat = kLv[i]; g.fcOk = v.ok; g.fcHot = v.hot; g.fcMargin = v.margin; g.fcG = v.g;
        } else if (in.regime == kRNose && phase == 5) {
            // the bank: the range to the site (the look-ahead), its side by the site's bearing with reversals; then the forecasts -
            // the wings (АВТО: 30 / 90 deg, the larger margin), the bank eased toward 0 while out of the limits, steeper when the
            // lift would skip out
            double bank = in.rangeCtl ? 0.0 : kNomBank;
            if (in.rangeCtl && in.site) {
                Look kr = k; kr.glide = true; kr.crest = kCrestOf[g.wing];
                auto rng = [&](double b) { double s = 0.0; kr.bank = b; TermRun(kTermTrig, in.h, in.va, in.gam, kr, &s); return s; };
                if (rng(0.0) <= in.rangeGo) bank = 0.0;
                else if (rng(80.0) >= in.rangeGo) bank = 80.0;
                else {
                    double lo = 0.0, hi = 80.0;
                    for (int i = 0; i < 6; ++i) { const double mid = 0.5 * (lo + hi); if (rng(mid) > in.rangeGo) lo = mid; else hi = mid; }
                    bank = 0.5 * (lo + hi);
                }
                if (in.xAng * g.bankSign < -3.0 * kD2R) g.bankSign = -g.bankSign;
            }
            const int nw = in.wingSel == kWAuto ? 2 : 1;
            const int wc[2] = {in.wingSel == kWAuto ? 1 : ShipWing(in.wingSel), 0};
            double cand[7] = {bank, 0.5 * bank, 0.0, kBankAlt[0], kBankAlt[1], kBankAlt[2], kBankAlt[3]};
            Verdict best; int bw = wc[0]; double bb = bank; bool have = false;
            for (int j = 0; j < 7; ++j) {
                if (j >= 3 && best.ok) break;
                if (j == 1 && bank <= 0.0) continue;
                if (j == 2 && bank <= 0.0 && have) continue;
                for (int kk = 0; kk < nw; ++kk) {
                    const Verdict v = Judge(Fc(in, kAoaNose, cand[j] * g.bankSign, wc[kk], 0.0), *in.skin, in.gLim);
                    const bool better = !have || (v.ok && !best.ok) || (v.ok == best.ok && v.margin > best.margin);
                    if (better) { best = v; bw = wc[kk]; bb = cand[j]; have = true; }
                }
                if (best.ok && j < 3) break;
            }
            g.bank = bb * g.bankSign; g.wing = bw;
            g.fcOk = best.ok; g.fcHot = best.hot; g.fcMargin = best.margin; g.fcG = best.g;
        }
    }
    // the level
    double L = 0.0; int lim = kRLimNone;
    if (in.regime == kRStern && (phase == 5 || phase == 7) && !g.align) {
        L = g.lHeat; if (L > 0) lim = kRLimHeat;
        if (in.hotMargin < 0.0) { L = (std::min)(1.0, L + (std::min)(0.3, -in.hotMargin / 200.0)); lim = kRLimHeat; }   // between the forecasts
    }
    if ((phase == 7 || phase == 8) && !g.align && (in.h < kAlignH + 300.0 || in.va < kAlignV)) g.align = true;
    if (phase == 8) g.align = true;
    if (g.align) {
        // the velocity onto 150 m/s straight down: its error over kAlignTau, the gravity and the drag cancelled; the hull follows
        const V3 vDes{0, 0, -kHandV};
        const V3 vh = Unit(in.vL);
        V3 aT = (vDes - in.vL) * (1.0 / kAlignTau) + V3{0, 0, in.gl} + vh * in.aAero;
        const double mag = Len(aT);
        g.dirL = mag > 1e-6 ? aT * (1.0 / mag) : V3{0, 0, 1};
        if (g.dirL.z < 0.3) { g.dirL.z = 0.3; g.dirL = Unit(g.dirL); }   // never nose down
        L = mag / aUse; lim = kRLimBrake;
    } else if (phase == 7 && g.lTerm >= L) { L = g.lTerm; lim = kRLimBrake; }
    if (phase == 6 || phase == 4) L = 0.0;
    // the g limit: the thrust and the air together
    const double Lg = (gCap - in.aAero) / aUse;
    if (L > Lg) { L = (std::max)(0.0, Lg); lim = kRLimG; }
    // the plant's stern: under tSafe (the limiter cuts the power above it too)
    if (L > 0 && in.sternT > in.tSafe - 50.0) { L *= Clamp((in.tSafe - in.sternT) / 50.0, 0.0, 1.0); lim = kRLimStern; }
    g.level = Clamp(L, 0.0, 1.0); g.lim = g.level > 0 || lim == kRLimG ? lim : kRLimNone;
}

// the burn's tail-off: the level that lowers the periapsis to its target in about 2 s (sens: m of periapsis per m/s, retrograde)
double BurnLevel(const V3& r, const V3& v, const Planet& pl, double hpT, double aMax) {
    const Elements e0 = guidance::Elems(r, v, pl.mu, pl.R);
    const double vv = Len(v);
    const Elements e1 = guidance::Elems(r, v * (1.0 - 1.0 / vv), pl.mu, pl.R);
    const double sens = (std::max)(1.0, e0.peri - e1.peri), dvRem = (e0.peri - hpT) / sens;
    return Clamp(dvRem / (guidance::kTTail * (std::max)(1e-6, aMax)), 0.02, 1.0);
}

// the attitude in the local horizon frame (x east, y north, z up) from the airspeed's direction
void Attitude(int att, const V3& vAirL, const V3& vInL, double aoaDeg, double bankDeg, V3* nose, V3* up) {
    *up = V3{};
    if (att == kAttRetro) { *nose = Unit(vInL) * -1.0; return; }
    const V3 vh = Unit(vAirL);
    if (att == kAttStern) { *nose = vh * -1.0; return; }
    V3 l = kZ - vh * Dot(vh, kZ);
    l = Len(l) > 1e-6 ? Unit(l) : Unit(Cross(Cross(vh, V3{0, 1, 0}), vh));
    const V3 s = Cross(vh, l);                    // right of the airspeed
    const double sg = bankDeg * kD2R, al = aoaDeg * kD2R;
    const V3 Lh = l * std::cos(sg) + s * std::sin(sg);
    *nose = vh * std::cos(al) + Lh * std::sin(al);
    *up = Lh * std::cos(al) - vh * std::sin(al);
}

// ============ the planner's model ============
struct SimOut { ReentryPlan P; V3 rHandI, rHandF, rEIF; bool reached = false; };

struct Sim {
    PlanIn in; ts::Body body;
    V3 r, v; double t = 0.0, phT = 0.0; int phase = 1;
    double m = 0, argon = 0, iron = 0; Reg reg; dm::Model skin; double sternT = 300;
    double pod = 0.0; bool podCmd = false;
    double tBurn = 0, hpT = 0, dvBurn = 0, burnT0 = kNaN, burnT1 = kNaN, flipT0 = 0, evalDt = 4.0;
    double Fm = 0, Fp = 0;
    bool rangeCtl = true, done = false, crash = false, dry = false, eiSeen = false;
    double felt = 0, nextS = 0;
    V3 r0F, siteF;
    SimOut out;
};

void PEv(Sim& S, int key, const std::wstring& txt, int lvl, double h, double dr) { S.out.P.ev.push_back({key, S.t, txt, lvl, dr, h}); }

void SimStep(Sim& S, double dt) {
    const Planet& pl = S.in.pl; ReentryPlan& P = S.out.P;
    // where the ship is
    const double rr = Len(S.r), h = rr - pl.R;
    const V3 u = S.r * (1.0 / rr), e = Unit(Cross(kZ, u)), n = Cross(u, e);
    const V3 W{0, 0, pl.omega};
    const V3 vAir = S.v - Cross(W, S.r);
    const double va = Len(vAir), vr = Dot(vAir, u), gam = std::asin(Clamp(vr / (std::max)(1e-3, va), -1, 1));
    const double rho = S.in.air.Rho(h), mach = va / kSoundPlan, q = 0.5 * rho * va * va;
    const V3 rF = RotZ(S.r, -pl.omega * S.t);
    const double dr = pl.R * Ang(rF, S.r0F);
    const int mass = MassFor(rho, h, S.argon, S.iron);
    const bool stern = S.in.regime != kRNose || S.phase >= 7;
    const int wing = S.in.regime == kRNose ? S.reg.wing : 2;
    // the phases
    double aoaDeg = stern ? 180.0 : kAoaNose, bankDeg = 0.0;
    double thrN = 0.0; V3 thrDir;
    if (S.phase == 1 && S.t >= S.tBurn - 1e-9) {
        S.phase = 3; S.burnT0 = S.t;
        PEv(S, kEvBurn, L"Импульс схода: ретроградно до перицентра " + Km(S.hpT), kOk, h, dr);
    }
    if (S.phase == 3) {
        const double aMax = (std::min)(S.Fm / S.m, S.in.gLim * kG0);
        const Elements el = guidance::Elems(S.r, S.v, pl.mu, pl.R);
        if (el.peri <= S.hpT + 100.0 || S.iron + S.argon <= 0) {
            S.phase = 4; S.burnT1 = S.t;
            PEv(S, kEvBurnEnd, L"Конец импульса: Δv " + Fmt(S.dvBurn, 1) + L" м/с · перицентр " + Km(el.peri), kOk, h, dr);
        } else {
            const double lvl = BurnLevel(S.r, S.v, pl, S.hpT, aMax);
            thrN = lvl * aMax * S.m; thrDir = Unit(S.v) * -1.0;
        }
    }
    if (!S.eiSeen && S.phase >= 4 && h < kEI) {
        if (S.phase == 4) S.phase = 5;
        S.eiSeen = true;
        P.tEI = S.t; P.vEI = va; P.gamEI = gam * kR2D; P.wing = wing; S.out.rEIF = rF;
        PEv(S, kEvEI, L"Вход в атмосферу: " + Fmt(va, 0) + L" м/с, угол " + Fmt(gam * kR2D, 2) + L"°", kOk, h, dr);
    }
    if (S.phase >= 5 || (S.phase == 4 && S.in.regime != kRNose && vr < 0.0)) {
        RegIn in;
        in.regime = S.in.regime; in.wingSel = S.in.wingSel; in.gLim = S.in.gLim; in.rangeCtl = S.rangeCtl; in.site = S.in.siteValid;
        in.h = h; in.va = va; in.gam = gam; in.m = S.m;
        in.vL = {Dot(vAir, e), Dot(vAir, n), vr}; in.gl = pl.mu / (rr * rr);
        in.aMarch = mass == guidance::kNoMass ? 0.0 : S.Fm / S.m; in.aPods = S.pod >= 1.0 && mass != guidance::kNoMass ? S.Fp / S.m : 0.0;
        double SL = 0, SD = 0;
        ts::AeroAreas(aoaDeg * kD2R, mach, stern ? kCrestOf[2] : kCrestOf[wing], 0.0, &SL, &SD);
        in.aAero = q * std::hypot(SL, SD) / S.m;
        if (S.in.siteValid) {
            const V3 sI = RotZ(S.siteF, pl.omega * S.t);
            in.rangeGo = pl.R * Ang(u, sI);
            const V3 toS = sI - u * Dot(sI, u), vh = vAir - u * vr;
            in.xAng = std::atan2(Dot(Cross(vh, toS), u) * -1.0, Dot(vh, toS)) ;   // + right of the track
        }
        in.air = &S.in.air; in.body = S.body; in.skin = &S.skin; in.expo = S.in.expo; in.expo.pods = S.pod; in.gearArea = 0.0;
        double hot = 1e9;
        for (int z = 0; z < kZones; ++z) hot = (std::min)(hot, S.skin.Limit(z) - kMarginK - S.skin.Temperature(z));
        in.hotMargin = hot; in.sternT = S.sternT; in.tSafe = S.in.tSafe; in.t = S.t; in.evalDt = S.phase == 4 ? 10.0 : S.evalDt;
        Regulate(S.reg, in, S.phase);
        // the transitions
        if ((S.phase == 5 || S.phase == 4) && S.in.regime != kRNose && S.reg.lTerm >= kTermTrig) {
            S.phase = 7; P.brakeH = h;
            PEv(S, kEvBrake, L"Торможение с " + Km(h) + L" при " + Fmt(va, 0) + L" м/с", kOk, h, dr);
            Regulate(S.reg, in, S.phase);
        } else if (S.phase == 5 && S.in.regime == kRNose && ((mach < kFlipMach && h < kFlipH) || S.reg.lTerm >= kTermTrig * 0.9)) {
            S.phase = 6; S.flipT0 = S.t; P.flipH = h;
            PEv(S, kEvFlip, L"Разворот кормой вниз с " + Km(h) + L", М " + Fmt(mach, 1), kOk, h, dr);
        } else if (S.phase == 6 && S.t - S.flipT0 >= kFlipT) {
            S.phase = 7; P.brakeH = h; S.reg.nextEval = -1e9;
            PEv(S, kEvBrake, L"Торможение с " + Km(h) + L" при " + Fmt(va, 0) + L" м/с", kOk, h, dr);
            in.regime = kRStern; Regulate(S.reg, in, S.phase);
        }
        if (S.phase == 6) aoaDeg = kAoaNose + (180.0 - kAoaNose) * Clamp((S.t - S.flipT0) / kFlipT, 0, 1);
        if (S.phase == 5 && S.in.regime == kRNose) bankDeg = S.reg.bank;
        // the pods: out when slow and low enough (their q rating), 12 s
        if (S.phase == 7 && !S.podCmd && wing != 2 && q < kPodQ && h < kPodH && mach < kPodMach) { S.podCmd = true; PEv(S, kEvPods, L"Выдвижные блоки на выпуск · " + Km(h), kOk, h, dr); }
        if (S.podCmd) S.pod = (std::min)(1.0, S.pod + dt / kPodT);
        const double aFull = in.aMarch + in.aPods;
        if (S.reg.level > 0 && aFull > 0) {
            thrN = S.reg.level * AUse(in) * S.m;
            thrDir = S.reg.align ? Unit(e * S.reg.dirL.x + n * S.reg.dirL.y + u * S.reg.dirL.z) : Unit(vAir) * -1.0;
        }
    }
    // the forces: the air (drag against the airspeed, lift turned by the bank), the thrust, central gravity
    V3 aNG;
    if (va > 1e-3 && rho > 0) {
        double SL = 0, SD = 0;
        const double crest = stern ? kCrestOf[2] : kCrestOf[wing];
        ts::AeroAreas(aoaDeg * kD2R, mach, crest, 0.0, &SL, &SD);
        const V3 vh = vAir * (1.0 / va);
        V3 l = u - vh * Dot(vh, u); l = Len(l) > 1e-6 ? Unit(l) : e;
        const V3 s = Cross(vh, l), Lh = l * std::cos(bankDeg * kD2R) + s * std::sin(bankDeg * kD2R);
        aNG = vh * (-q * SD / S.m) + Lh * (q * SL / S.m);
    }
    if (thrN > 0 && mass == guidance::kNoMass) { thrN = 0; if (!S.dry) { S.dry = true; PEv(S, kEvDry, L"Рабочая масса кончилась", kBad, h, dr); } }
    aNG = aNG + thrDir * (thrN / S.m);
    auto grav = [&](const V3& x) { const double l = Len(x); return x * (-pl.mu / (l * l * l)); };
    const V3 vh2 = S.v + (aNG + grav(S.r)) * (dt / 2);
    S.r = S.r + vh2 * dt;
    S.v = vh2 + (aNG + grav(S.r)) * (dt / 2);
    // consumption: the march share and the pods share at the same level
    if (thrN > 0) {
        const double Fall = S.Fm + (S.pod >= 1.0 ? S.Fp : 0.0), ls = thrN / Fall;
        const double mdot = ls * S.Fm / ExhaustOf(mass) + ls * (S.pod >= 1.0 ? S.Fp : 0.0) / guidance::kVArgon;
        if (mass == guidance::kArgon) { S.argon -= mdot * dt; P.argon += mdot * dt; if (S.argon < 0) S.argon = 0; }
        else { S.iron -= mdot * dt; P.iron += mdot * dt; if (S.iron < 0) S.iron = 0; }
        S.m -= mdot * dt;
        if (S.phase == 3) S.dvBurn += thrN / S.m * dt;
    }
    S.felt = Len(aNG) / kG0;                      // the felt load: the air and the thrust
    if (S.felt > P.nMax) { P.nMax = S.felt; P.nMaxH = h; }
    // the skin (core/Damage's own step, damage off) and the plant's stern
    {
        dm::Flight f; f.rho = rho; f.v = va; f.mach = mach; f.aoa = aoaDeg * kD2R; f.gLoad = S.felt;
        dm::Ground gr;
        dm::Exposure x = ExpoFor(S.in.expo, wing, S.pod); x.sternCups = thrN > 0 ? 1.0 : 0.0;
        S.skin.Step(dt, f, x, gr, false);
        for (int z = 0; z < kZones; ++z) P.peakT[z] = (std::max)(P.peakT[z], S.skin.Temperature(z));
        const double F = thrN, vex = ExhaustOf(mass), mdot = F > 0 ? F / vex : 0.0;
        const double c1 = 15 / std::hypot(15.0, 2.2), c2 = 15 / std::hypot(15.0, 15.0);
        const double qIn = kSternCouple * S.skin.HeatFlux(dm::kZoneStern) * aero::kFrontal + guidance::kChi * F * vex / (2 * guidance::kEtaN) * (c1 - c2) / 2;
        const double rad = 0.9 * 5.670e-8 * std::pow(S.sternT, 4) * 2700 * (wing == 2 ? 0.1 : 1.0);
        S.sternT = (std::max)(290.0, S.sternT + (qIn - mdot * 1.5e6 - rad) * dt / (guidance::kSternStore / 1400.0));
        P.sternTMax = (std::max)(P.sternTMax, S.sternT);
    }
    S.t += dt; S.phT += dt;
    // the track
    if (S.t >= S.nextS && S.phase >= 3) {
        P.track.push_back({S.t, dr, h, va, S.felt, S.phase});
        S.nextS = S.t + (S.phase >= 5 ? 2.0 : 10.0);
    }
    // the end: the landing's entry (interpolated onto 5 km), the ground
    const double h2 = Len(S.r) - pl.R;
    if (S.phase >= 5 && h2 <= kHandH) {
        S.done = true; S.out.reached = true;
        const V3 u2 = Unit(S.r), e2 = Unit(Cross(kZ, u2)), n2 = Cross(u2, e2);
        const V3 vA = S.v - Cross(W, S.r);
        const double vz = Dot(vA, u2), vhh = std::hypot(Dot(vA, e2), Dot(vA, n2));
        P.tHand = S.t; P.vHand = Len(vA); P.vzHand = vz; P.vhHand = vhh;
        P.pitchHand = std::atan2(-vz, vhh) * kR2D;   // the nose against the airspeed: up when falling
        const V3 rF2 = RotZ(S.r, -pl.omega * S.t);
        P.latHand = std::asin(Clamp(rF2.z / Len(rF2), -1, 1)); P.lonHand = std::atan2(rF2.y, rF2.x);
        S.out.rHandI = S.r; S.out.rHandF = rF2;
        if (S.phase < 8) S.phase = 8;
        PEv(S, kEvHand, L"ВХОД ПОСАДКИ: " + Km(h2) + L", " + Fmt(vz, 0) + L" м/с верт., " + Fmt(vhh, 0) + L" м/с гориз.", vhh < 50 && std::fabs(vz + kHandV) < 40 ? kOk : kWarn, h2, dr);
    } else if (h2 <= 0.0) {
        S.done = true; S.crash = true;
        PEv(S, kEvCrashR, L"ПАДЕНИЕ: удар о грунт " + Fmt(va, 0) + L" м/с", kBad, 0.0, dr);
    }
}

SimOut RunSim(const PlanIn& in, double tBurn, double hpT, bool rangeCtl, double tMax = 6 * 3600.0) {
    Sim S;
    S.in = in; S.body = BodyOf(in.pl);
    S.r = in.r; S.v = in.v; S.m = in.mass; S.argon = in.argon; S.iron = in.iron; S.skin = in.skin; S.sternT = in.sternT;
    S.reg = in.reg; S.reg.nextEval = -1e9; S.phase = in.phase <= 1 ? 1 : in.phase;
    S.tBurn = tBurn; S.hpT = hpT; S.rangeCtl = rangeCtl; S.dvBurn = in.dvDone;
    S.evalDt = in.regime == kRNose ? 10.0 : 4.0;
    S.Fm = in.Fmarch > 0 ? in.Fmarch : guidance::MarchMax();
    S.Fp = in.Fpods > 0 ? in.Fpods : guidance::PodsMax();
    S.pod = in.expo.pods; S.podCmd = S.pod > 0.5;
    S.r0F = in.r; S.siteF = Fixed(in.siteLat, in.siteLon);
    if (in.regime == kRNose && in.phase <= 4) S.reg.wing = in.wingSel == kWAuto ? 1 : ShipWing(in.wingSel);
    ReentryPlan& P = S.out.P;
    P.regime = in.regime; P.hpT = hpT; P.tBurn = in.phase <= 1 ? tBurn : kNaN; P.site = in.siteValid;
    for (int z = 0; z < kZones; ++z) { P.peakT[z] = in.skin.Temperature(z); P.lim[z] = in.skin.Limit(z); }
    for (int guard = 0; !S.done && S.t < tMax && guard < 400000; ++guard) {
        const double h = Len(S.r) - in.pl.R;
        double dt;
        if (S.phase == 1) dt = (std::min)(5.0, (std::max)(1e-3, S.tBurn - S.t));
        else if (S.phase == 3) dt = 0.1;
        else if (S.phase == 4) dt = h > kEI + 30e3 ? 5.0 : 1.0;
        else dt = h > 30e3 ? 1.0 : 0.5;
        SimStep(S, dt);
    }
    P.dvBurn = S.dvBurn;
    P.burnLen = std::isfinite(S.burnT0) && std::isfinite(S.burnT1) ? S.burnT1 - S.burnT0 : 0.0;
    // the verdict
    P.margin = 1e9;
    for (int z = 0; z < kZones; ++z) { const double mz = P.lim[z] - kMarginK - P.peakT[z]; if (mz < P.margin) { P.margin = mz; P.hot = z; } }
    if (S.out.reached && in.siteValid) {
        const V3 sF = S.siteF, hF = Unit(S.out.rHandF);
        P.miss = in.pl.R * Ang(hF, sF);
        V3 d = Unit(S.out.rHandF) - Unit(S.out.rEIF); d = d - hF * Dot(d, hF);
        if (Len(d) > 1e-9) {
            d = Unit(d);
            P.missAlong = in.pl.R * std::atan2(Dot(sF, d), Dot(sF, hF));
            P.missCross = in.pl.R * std::asin(Clamp(Dot(sF, Cross(d, hF)), -1, 1));
        }
    }
    if (!S.out.reached) P.why = S.crash ? L"падение до входа посадки" : L"вход посадки не достигнут (касательные проходы по атмосфере)";
    else if (in.regime == kRNose && std::isfinite(P.flipH) && P.flipH >= kEI - 15e3)
        P.why = L"НОСОМ не годится для " + Fmt(in.mass / 1e6, 1) + L" кт: планирование не успевает затормозить — разворот кормой сразу на входе (" + Km(P.flipH) + L"), выберите КОРМОЙ";
    else if (S.dry) P.why = L"рабочая масса кончилась";
    else if (P.margin < 0) P.why = std::wstring(kZoneRu[P.hot]) + L": " + Fmt(P.peakT[P.hot], 0) + L" К при пределе " + Fmt(P.lim[P.hot], 0) + L" − 100 К";
    else if (P.nMax > in.gLim + 0.05) P.why = L"перегрузка " + Fmt(P.nMax, 2) + L" g выше предела " + Fmt(in.gLim, 1) + L" g";
    else if (P.argon > in.argon + 1.0) P.why = L"не хватает аргона";
    else if (std::fabs(P.vzHand + kHandV) > 40.0 || P.vhHand > 60.0) P.why = L"вход посадки вне нормы: " + Fmt(P.vzHand, 0) + L" м/с верт., " + Fmt(P.vhHand, 0) + L" м/с гориз.";
    P.ok = P.why.empty();
    return S.out;
}

}  // namespace

// ============ the plan ============

ReentryPlan PlanReentry(const PlanIn& in0) {
    PlanIn in = in0;
    if (in.phase >= 3) {                          // in flight past the burn: from where the ship is
        if (!std::isfinite(in.hpT)) in.hpT = HpOf(in.air, in.regime == kRNose ? kRhoNose : kRhoStern);
        return RunSim(in, 0.0, in.hpT, true).P;
    }
    const Elements el = guidance::Elems(in.r, in.v, in.pl.mu, in.pl.R);
    const double tb0 = std::isfinite(in.tBurn) ? (std::max)(0.0, in.tBurn) : kLead;
    // the corridor: КОРМОЙ · БАЛЛИСТИКА scans the periapsis for the forecast with the largest margin within the g limit
    if (!std::isfinite(in.hpT)) {
        if (in.regime == kRBallistic) {
            // one pass: a periapsis that only grazes the air (hours of passes) is no corridor
            const double per0 = std::isfinite(el.period) ? el.period : 5400.0;
            double best = -1e18, hp = HpOf(in.air, kRhoStern);
            for (double sh : kRhoScan) {
                const double h = HpOf(in.air, sh);
                const SimOut o = RunSim(in, tb0, h, false, tb0 + 0.75 * per0 + 1800.0);
                const double score = (o.reached ? 0.0 : -1e9) + (std::min)(o.P.margin, 1e4) - 500.0 * (std::max)(0.0, o.P.nMax - in.gLim);
                if (score > best) { best = score; hp = h; }
            }
            in.hpT = hp;
        } else in.hpT = HpOf(in.air, in.regime == kRNose ? kRhoNose : kRhoStern);
    }
    if (std::isfinite(in.tBurn) || !in.siteValid) return RunSim(in, tb0, in.hpT, true).P;
    // the burn time for the site: a reference descent from the earliest burn, turned along the orbit (and the planet under it)
    // over the next three orbits - the smallest miss; then the descent itself, its miss along the track nulled by the secant
    const SimOut ref = RunSim(in, tb0, in.hpT, false);
    if (!ref.reached) { ReentryPlan P = ref.P; return P; }
    const double T0 = ref.P.tHand, per = std::isfinite(el.period) ? el.period : 5400.0, nb = 2 * kPi / per;
    const V3 hh = Unit(Cross(in.r, in.v)), sF = Fixed(in.siteLat, in.siteLon);
    // the smallest miss; but the earliest pass that comes close wins over a slightly closer later one
    double bestD = 0.0, bestM = 1e18, winD = -1.0, winM = 1e18, win0 = -1.0;
    for (double d = 0.0; d <= 3.0 * per; d += 10.0) {
        const V3 p = RotAx(ref.rHandI, hh, nb * d), pF = RotZ(p, -in.pl.omega * (T0 + d));
        const double ms = Ang(Unit(pF), sF) * in.pl.R;
        if (ms < bestM) { bestM = ms; bestD = d; }
        if (win0 < 0.0 && ms < 0.5 * kMissOk) win0 = d;
        if (win0 >= 0.0 && d - win0 < 0.25 * per && ms < winM) { winM = ms; winD = d; }
    }
    if (winD >= 0.0) bestD = winD;
    double tb = tb0 + bestD;
    SimOut o = RunSim(in, tb, in.hpT, true);
    double tPrev = tb, fPrev = o.P.missAlong;
    if (o.reached && std::fabs(fPrev) > 2e3) {
        double t2 = tb + 20.0;
        for (int it = 0; it < 4; ++it) {
            const SimOut o2 = RunSim(in, t2, in.hpT, true);
            if (!o2.reached) break;
            const double f2 = o2.P.missAlong;
            o = o2; tb = t2;
            if (std::fabs(f2) < 2e3 || std::fabs(f2 - fPrev) < 1.0) break;
            const double tn = (std::max)(kLead, t2 - f2 * (t2 - tPrev) / (f2 - fPrev));
            tPrev = t2; fPrev = f2; t2 = tn;
        }
    }
    ReentryPlan P = o.P;
    if (P.ok && std::isfinite(P.miss) && P.miss > kMissOk) {
        P.nextPass = tb + per;
        P.ev.push_back({kEvHot, 0.0, L"Площадка вне досягаемости на этом витке: промах " + Fmt(P.miss / 1e3, 0) + L" км (плоскость орбиты)", kWarn, 0.0, 0.0});
    }
    return P;
}

// ============ the live autopilot ============

std::wstring Reentry::TLabel() const {
    if (std::isfinite(tBurnStart_)) return L"Т+" + Clock(st_.simt - tBurnStart_);
    if (std::isfinite(tBurnAbs_)) return L"Т−" + Clock((std::max)(0.0, tBurnAbs_ - st_.simt));
    return L"Т−00:00";
}

std::wstring Reentry::SiteName() const { return site_ < 0 || site_ >= int(sites_.size()) ? std::wstring(L"ПО ТРАССЕ") : sites_[site_].name; }

void Reentry::SetSites(const std::vector<Site>& s) {
    sites_ = s;
    if (site_ >= int(sites_.size())) site_ = -1;
}

void Reentry::SetPhase(int p) { phase_ = p; phT_ = 0.0; }

PlanIn Reentry::MakeIn(bool scan) const {
    PlanIn in;
    const ReentryState& st = st_;
    in.pl = st.planet; in.air = st.air; in.regime = regime_; in.wingSel = wings_; in.gLim = gLim_;
    const double rr = st.planet.R + st.alt, cl = std::cos(st.lat), sl = std::sin(st.lat);
    const V3 u{cl * std::cos(st.lon), cl * std::sin(st.lon), sl}, e{-std::sin(st.lon), std::cos(st.lon), 0.0}, n = Cross(u, e);
    in.r = u * rr;
    in.v = e * (st.vE + st.planet.omega * rr * cl) + n * st.vN + u * st.vU;
    in.mass = st.mass; in.argon = st.argon; in.iron = st.iron;
    in.Fmarch = st.plantRun ? st.Fmarch : 0.0; in.Fpods = st.Fpods;
    in.skin = st.skin; in.expo = st.expo; in.expo.pods = st.podsOut; in.gearArea = st.gearArea;
    in.sternT = st.sternT; in.tSafe = st.tSafe;
    if (site_ >= 0 && site_ < int(sites_.size())) { in.siteValid = true; in.siteLat = sites_[site_].lat; in.siteLon = sites_[site_].lon; }
    const bool flying = mode_ != guidance::kIdle && phase_ >= 1;
    if (flying && phase_ >= 3) { in.phase = phase_ >= 5 ? phase_ : 4; in.hpT = hpT_; in.reg = reg_; in.dvDone = dv_; }
    else if (!flying && el_.peri < kEI) { in.phase = st.alt < kEI ? 5 : 4; in.reg = reg_; }
    else {
        in.phase = 1;
        if (!scan && std::isfinite(tBurnAbs_)) { in.tBurn = (std::max)(0.0, tBurnAbs_ - st.simt); in.hpT = hpT_; }
    }
    return in;
}

void Reentry::Replan(bool scan) {
    plan_ = PlanReentry(MakeIn(scan));
    planT_ = st_.simt;
    if (mode_ == guidance::kIdle || phase_ <= 1) {
        tBurnAbs_ = std::isfinite(plan_.tBurn) ? planT_ + plan_.tBurn : kNaN;
        hpT_ = plan_.hpT;
    }
}

void Reentry::Tick(double now) {
    if (checkT_ >= 0 && now - checkT_ > checks_.size() * guidance::kCheckDt + 0.3) {
        checkT_ = -1;
        const Check* bad = nullptr;
        for (const Check& c : checks_) if (!c.ok) { bad = &c; break; }
        if (bad) { armed_ = false; Note(L"Готовность не подтверждена: " + bad->t, kBad); }
        else { armed_ = true; Note(mode_ == guidance::kIdle ? L"СХОД взведён — ПУСК разрешён" : L"СХОД взведён — ПУСК вернёт управление автомату", kOk); }
    }
}

std::vector<Check> Reentry::BuildChecks() const {
    const ReentryState& st = st_; const ReentryPlan& P = plan_;
    std::vector<Check> c;
    if (mode_ != guidance::kIdle && phase_ >= 1) c.push_back({L"Продолжение с фазы " + Fmt(phase_, 0) + L" " + kRPhases[phase_], true});
    else c.push_back({L"На орбите: перицентр " + Km(el_.peri) + L" — выше атмосферы (" + Km(kEI) + L")", el_.peri > kEI + 10e3 || el_.peri < kEI});
    c.push_back({L"Установка на режиме", st.plantRun});
    c.push_back({L"Обмотка: запас по току " + Fmt(st.coilMargin * 100, 0) + L" % (нужно ≥ 15 %)", st.coilMargin >= 0.15});
    c.push_back({L"Криогеника держит", st.cryoOk});
    c.push_back({L"Насосы рабочей массы целы", st.pumpsOk});
    c.push_back({L"ВЭУ работает", st.veuOk});
    c.push_back({L"Накопитель поля не пуст", st.storeOk});
    c.push_back({L"Аргон " + Fmt(st.argon / 1e6, 2) + L" кт · по плану " + Fmt(P.argon / 1e6, 2) + L" кт", P.argon <= st.argon});
    c.push_back({L"Железо " + Fmt(st.iron / 1e6, 2) + L" кт · по плану " + Fmt(P.iron / 1e6, 2) + L" кт", P.iron <= st.iron});
    const int hz = P.hot >= 0 ? P.hot : 0;
    c.push_back({L"Прогноз: " + std::wstring(kZoneRu[hz]) + L" " + Fmt(P.peakT[hz], 0) + L" К (предел " + Fmt(P.lim[hz], 0) + L" − 100) · " +
                 Fmt(P.nMax, 2) + L" g из " + Fmt(gLim_, 1), P.margin >= 0 && P.nMax <= gLim_ + 0.05});
    if (P.site) c.push_back({L"Площадка " + SiteName() + L": промах " + Fmt(P.miss / 1e3, 1) + L" км", std::isfinite(P.miss) && P.miss <= kMissOk});
    else c.push_back({L"ПО ТРАССЕ: вход посадки " + Fmt(P.latHand * kR2D, 2) + L"°, " + Fmt(P.lonHand * kR2D, 2) + L"°", true});
    c.push_back({P.ok ? L"План: " + std::wstring(kRegimes[regime_]) + L" · вход посадки через " + Clock(P.tHand) : L"План: " + P.why, P.ok});
    return c;
}

void Reentry::Step(const ReentryState& st, double now) {
    const bool first = !have_;
    st_ = st; have_ = true;
    {
        PlanIn tmp = MakeIn(false);
        el_ = guidance::Elems(tmp.r, tmp.v, st.planet.mu, st.planet.R);
    }
    if (first) { Replan(true); Note(L"Выберите режим, площадку и нажмите ВЗВЕСТИ", kOk); }
    Tick(now);
    // the live geometry (local horizon: x east, y north, z up)
    const double rr = st.planet.R + st.alt;
    const V3 vAirL{st.vE, st.vN, st.vU}, vInL{st.vE + st.planet.omega * rr * std::cos(st.lat), st.vN, st.vU};
    const double va = Len(vAirL), gam = std::asin(Clamp(st.vU / (std::max)(1e-3, va), -1, 1));
    double SL = 0, SD = 0;
    ts::AeroAreas(st.aoa * kD2R, st.mach > 0 ? st.mach : va / kSoundPlan, st.crestAvail, st.gearArea, &SL, &SD);
    const double aAero = st.q * std::hypot(SL, SD) / (std::max)(1.0, st.mass);
    const double Fnow = st.FmNow + st.FpNow;
    felt_ = std::hypot(st.q * SD / (std::max)(1.0, st.mass) + (std::fabs(st.aoa) > 90 ? Fnow / (std::max)(1.0, st.mass) : 0.0), st.q * SL / (std::max)(1.0, st.mass)) / kG0;
    cmd_.attitude = cmd_.thrust = false;
    if (mode_ == guidance::kHold) { cmd_.attitude = cmd_.thrust = true; return; }   // the last commands stay
    if (!Engaged()) { if (mode_ == guidance::kIdle) cmd_ = ReentryCmd(); return; }
    // ---- auto ----
    const double dt = st.dt;
    phT_ += dt;
    if (std::isfinite(tBurnStart_)) dv_ += Fnow / (std::max)(1.0, st.mass) * dt;
    maxG_ = (std::max)(maxG_, felt_);
    for (int z = 0; z < kZones; ++z) {
        peakT_[z] = (std::max)(peakT_[z], st.skinT[z]);
        if (st.skinLim[z] > 0 && st.skinT[z] > st.skinLim[z] - kMarginK && !warnedHot_[z]) {
            warnedHot_[z] = true; Note(std::wstring(kZoneRu[z]) + L": " + Fmt(st.skinT[z], 0) + L" К — ближе 100 К к пределу " + Fmt(st.skinLim[z], 0) + L" К", kWarn);
        }
    }
    if (felt_ > gLim_ + 0.1 && !warnedG_) { warnedG_ = true; Note(L"Перегрузка " + Fmt(felt_, 2) + L" g выше предела " + Fmt(gLim_, 1) + L" g", kWarn); }
    const double aMarchNow = st.Fmarch / (std::max)(1.0, st.mass);
    ReentryCmd& c = cmd_;
    c.handover = phase_ == 8;
    c.massWanted = MassFor(st.rho, st.alt, st.argon, st.iron);
    c.wingMode = regime_ == kRNose ? (wings_ == kWAuto ? reg_.wing : ShipWing(wings_)) : 2;
    auto aimed = [&](const V3& nose) {
        const V3 z{std::cos(st.pitch * kD2R) * std::sin(st.hdg * kD2R), std::cos(st.pitch * kD2R) * std::cos(st.hdg * kD2R), std::sin(st.pitch * kD2R)};
        return Ang(z, nose) * kR2D < kAttTol;
    };
    auto setAtt = [&](int att, double aoa, double bank) {
        c.attitude = true; c.att = att;
        Attitude(att, vAirL, vInL, aoa, bank, &c.nose, &c.up);
        c.pitch = std::asin(Clamp(c.nose.z, -1, 1)) * kR2D; c.hdg = guidance::N360(std::atan2(c.nose.x, c.nose.y) * kR2D); c.bank = bank;
    };
    c.thrust = true; c.march = c.pods = 0.0; c.lim = kRLimNone;
    switch (phase_) {
        case 1: {                                 // the countdown to the burn; the plan from where the ship is now and then
            c.attitude = false;
            if (st.simt - planT_ > kReplanDt) Replan(false);
            if (std::isfinite(tBurnAbs_) && tBurnAbs_ - st.simt <= kTurnLead) { SetPhase(2); Note(L"Разворот на торможение: кормой по скорости", kOk); }
            break;
        }
        case 2:                                   // the turn: the nose against the velocity
            setAtt(kAttRetro, 0, 0);
            if (st.simt >= tBurnAbs_ && aimed(c.nose)) {
                if (st.simt > tBurnAbs_ + 30.0) Note(L"Импульс позже расчёта на " + Fmt(st.simt - tBurnAbs_, 0) + L" с — промах пересчитан", kWarn);
                SetPhase(3); tBurnStart_ = st.simt;
                Note(L"Импульс схода: до перицентра " + Km(hpT_), kOk);
            }
            break;
        case 3: {                                 // the burn: retrograde until the periapsis is the corridor's (a 2 s tail-off)
            setAtt(kAttRetro, 0, 0);
            PlanIn tmp = MakeIn(false);
            const double aMax = (std::min)(aMarchNow, gLim_ * kG0);
            if (el_.peri <= hpT_ + 100.0) {
                SetPhase(4); c.march = 0.0;
                Note(L"Конец импульса: Δv " + Fmt(dv_, 1) + L" м/с · перицентр " + Km(el_.peri) + L" · орбита " + Km(el_.peri) + L" × " + Km(el_.apo), kOk);
                Replan(false);
            } else if (aMarchNow > 0) c.march = BurnLevel(tmp.r, tmp.v, st.planet, hpT_, aMax) * aMax / aMarchNow;
            break;
        }
        default: break;
    }
    if (phase_ >= 4 && !eiSeen_ && st.alt < kEI) {
        eiSeen_ = true;
        if (phase_ == 4) SetPhase(5);
        Note(L"Вход в атмосферу: " + Fmt(va, 0) + L" м/с, угол " + Fmt(gam * kR2D, 2) + L"° · " + kRegimes[regime_], kOk);
    }
    if (phase_ == 4) {                            // to the entry attitude, coasting
        if (regime_ == kRNose) setAtt(kAttNose, kAoaNose, 0.0); else setAtt(kAttStern, 0, 0);
        if (st.simt - planT_ > kReplanDt) Replan(false);
    }
    if (phase_ >= 5 || (phase_ == 4 && regime_ != kRNose && st.vU < 0.0)) {
        RegIn in;
        in.regime = regime_; in.wingSel = wings_; in.gLim = gLim_; in.rangeCtl = true;
        in.h = st.alt; in.va = va; in.gam = gam; in.m = st.mass;
        in.vL = vAirL; in.gl = st.planet.mu / (rr * rr);
        const bool noMass = c.massWanted == guidance::kNoMass;
        in.aMarch = noMass ? 0.0 : aMarchNow;
        in.aPods = noMass || st.podsOut < 1.0 ? 0.0 : st.Fpods / (std::max)(1.0, st.mass);
        in.aAero = aAero;
        if (site_ >= 0 && site_ < int(sites_.size())) {
            in.site = true;
            const V3 P0 = Fixed(st.lat, st.lon), S0 = Fixed(sites_[site_].lat, sites_[site_].lon);
            in.rangeGo = st.planet.R * Ang(P0, S0);
            const V3 e{-std::sin(st.lon), std::cos(st.lon), 0.0}, n = Cross(P0, e);
            const V3 toS = S0 - P0 * Dot(S0, P0);
            const double az = std::atan2(Dot(toS, e), Dot(toS, n)), azV = std::atan2(st.vE, st.vN);
            in.xAng = AngDiff(az * kR2D, azV * kR2D) * kD2R;
        }
        in.air = &st_.air; in.body = BodyOf(st.planet); in.skin = &st_.skin; in.expo = st.expo; in.expo.pods = st.podsOut; in.gearArea = st.gearArea;
        double hot = 1e9;
        for (int z = 0; z < kZones; ++z) if (st.skinLim[z] > 0) hot = (std::min)(hot, st.skinLim[z] - kMarginK - st.skinT[z]);
        in.hotMargin = hot; in.sternT = st.sternT; in.tSafe = st.tSafe; in.t = st.simt; in.evalDt = 1.0;
        Regulate(reg_, in, phase_);
        const double mach = st.mach > 0 ? st.mach : va / kSoundPlan;
        if ((phase_ == 5 || phase_ == 4) && regime_ != kRNose && reg_.lTerm >= kTermTrig) {
            SetPhase(7); reg_.braking = true;
            Note(L"Торможение с " + Km(st.alt) + L" при " + Fmt(va, 0) + L" м/с" + (st.alt > kEI ? L" — выше атмосферы" : L""), kOk);
            Regulate(reg_, in, phase_);
        } else if (phase_ == 5 && regime_ == kRNose && ((mach < kFlipMach && st.alt < kFlipH) || reg_.lTerm >= kTermTrig * 0.9)) {
            SetPhase(6); Note(L"Разворот кормой вниз с " + Km(st.alt) + L", М " + Fmt(mach, 1), kOk);
        } else if (phase_ == 6 && (aimed(Unit(vAirL) * -1.0) || phT_ > 2 * kFlipT)) {
            SetPhase(7); reg_.nextEval = -1e9; reg_.braking = true; in.regime = kRStern;
            Note(L"Торможение с " + Km(st.alt) + L" при " + Fmt(va, 0) + L" м/с", kOk); Regulate(reg_, in, phase_);
        } else if (phase_ == 7 && st.alt <= kHandH) {
            SetPhase(8); c.handover = true;
            Note(L"ВХОД ПОСАДКИ: " + Km(st.alt) + L", " + Fmt(st.vU, 0) + L" м/с верт., " + Fmt(std::hypot(st.vE, st.vN), 0) +
                 L" м/с гориз. — передача «ПОСАДКЕ НА КОРМУ»", kOk);
        }
        if (phase_ == 5 && regime_ == kRNose) setAtt(kAttNose, kAoaNose, reg_.bank);
        else if (reg_.align) {
            c.attitude = true; c.att = kAttAlign; c.nose = reg_.dirL; c.up = V3{};
            c.pitch = std::asin(Clamp(c.nose.z, -1, 1)) * kR2D; c.hdg = guidance::N360(std::atan2(c.nose.x, c.nose.y) * kR2D); c.bank = 0.0;
        } else setAtt(kAttStern, 0, 0);
        if (phase_ >= 6 && st.wingMode != 2 && st.fold < 0.5 && st.q < kPodQ && st.alt < kPodH && mach < kPodMach) c.podsOut = true;   // the last braking on the pods too (wings out only)
        if (c.podsOut && podT_ <= 0.0) { podT_ = st.simt; Note(L"Выдвижные блоки на выпуск · " + Km(st.alt), kOk); }
        if (phase_ == 5 && regime_ == kRNose) c.wingMode = wings_ == kWAuto ? reg_.wing : ShipWing(wings_);
        // the level: of the usable acceleration -> of the thrust available
        const double aFull = in.aMarch + in.aPods, use = AUse(in);
        const double ls = aFull > 0 ? reg_.level * use / aFull : 0.0;
        c.march = ls; c.pods = in.aPods > 0 ? ls : 0.0; c.lim = noMass && reg_.level > 0 ? kRLimMass : reg_.lim;
        if (reg_.lim == kRLimStern && !warnedStern_) { warnedStern_ = true; Note(L"Корма установки " + Fmt(st.sternT, 0) + L" К у предела " + Fmt(st.tSafe, 0) + L" К — тяга снижена", kWarn); }
        if (phase_ >= 5 && phase_ <= 7 && st.simt - planT_ > kReplanDt) Replan(false);
    }
}

// ---- the keys ----
void Reentry::StepRegime(int dir) {
    if (!Editable()) return;
    regime_ = (regime_ + (dir >= 0 ? 1 : kRegimeCount - 1)) % kRegimeCount;
    if (have_) Replan(true);
}
void Reentry::StepWings(int dir) {
    if (Engaged() && regime_ != kRNose) { Note(L"Кормой крылья сложены всегда", kWarn); return; }
    wings_ = (wings_ + (dir >= 0 ? 1 : kWingSelCount - 1)) % kWingSelCount;
    if (Engaged()) Note(L"Крылья: " + std::wstring(kWingSel[wings_]), kOk);
    else if (have_ && Editable()) Replan(true);
}
void Reentry::StepGLim(int dir) {
    if (!Editable()) return;
    gLim_ = Clamp(gLim_ + dir * 0.5, 1.5, 5.0);
    if (have_) Replan(true);
}
void Reentry::StepSite(int dir) {
    if (!Editable()) return;
    const int n = int(sites_.size()) + 1;         // -1 .. size-1
    site_ = ((site_ + 1 + (dir >= 0 ? 1 : n - 1)) % n) - 1;
    if (have_) Replan(true);
}
void Reentry::Arm(double now) {
    if (!CanArm()) return;
    if (armed_) { armed_ = false; Note(L"Взвод снят", kWarn); return; }
    if (mode_ == guidance::kIdle && have_) Replan(true);
    checks_ = BuildChecks(); checkT_ = now; Note(L"Проверка готовности…", kWarn);
}
void Reentry::Start(double now) {
    if (!armed_) { Note(L"ПУСК заблокирован: сначала ВЗВЕСТИ", kWarn); return; }
    if (now - goT_ > guidance::kConfirm) { goT_ = now; Note(L"ПУСК: подтвердите в течение 3 с", kWarn); return; }
    goT_ = -99; armed_ = false;
    const bool fresh = mode_ == guidance::kIdle;
    mode_ = guidance::kAuto;
    if (fresh) {
        dv_ = 0; maxG_ = 0; podT_ = 0; tBurnStart_ = kNaN; reg_ = Reg(); eiSeen_ = warnedG_ = warnedStern_ = false;
        for (int z = 0; z < kZones; ++z) { peakT_[z] = st_.skinT[z]; warnedHot_[z] = false; }
    }
    if (el_.peri >= kEI && !std::isfinite(tBurnStart_)) {
        if (!std::isfinite(tBurnAbs_) || tBurnAbs_ < st_.simt - 5.0) { Replan(false); if (!std::isfinite(tBurnAbs_)) tBurnAbs_ = st_.simt + kLead; }
        SetPhase(tBurnAbs_ - st_.simt <= kTurnLead ? 2 : 1);
        log_.Add(TLabel(), L"ПУСК · " + std::wstring(kRegimes[regime_]) + L" · " + SiteName() + L" · импульс через " + Clock(tBurnAbs_ - st_.simt), kOk);
    } else {
        if (!std::isfinite(tBurnStart_)) tBurnStart_ = st_.simt;
        SetPhase(reg_.braking || phase_ >= 7 ? 7 : st_.alt >= kEI ? 4 : (phase_ == 6 ? 6 : 5));
        Note(L"Автопилот включён: " + Fmt(phase_, 0) + L" " + kRPhases[phase_], kOk);
    }
}
void Reentry::Hold(double /*now*/) {
    if (!CanHold()) return;
    if (mode_ == guidance::kAuto) { mode_ = guidance::kHold; Note(L"Удержание: ориентация и тяга " + Fmt(cmd_.march * 100, 0) + L" % — как были", kWarn); }
    else { mode_ = guidance::kAuto; Note(L"Программа продолжена: " + Fmt(phase_, 0) + L" " + kRPhases[phase_], kOk); }
}
void Reentry::Manual(double /*now*/) {
    if (!CanManual()) return;
    if (mode_ == guidance::kManual) { Note(L"Вернуть автомат: ВЗВЕСТИ → ПУСК", kWarn); return; }
    mode_ = guidance::kManual; armed_ = false; Note(L"РУЧНОЕ: управление передано пилоту", kWarn);
}
void Reentry::Abort(double now) {
    if (!CanAbort()) return;
    if (now - abT_ > guidance::kConfirm) { abT_ = now; Note(L"ОТМЕНА: подтвердите в течение 3 с", kWarn); return; }
    abT_ = -99; armed_ = false;
    if (mode_ == guidance::kIdle) { Note(L"Сход отменён, взвод снят", kBad); return; }
    if (phase_ <= 2) {                            // before the burn: out, the ship stays on its orbit
        mode_ = guidance::kAbort; cmd_.march = cmd_.pods = 0;
        Note(L"ОТМЕНА до импульса: сход прекращён, корабль на орбите", kBad);
        return;
    }
    regime_ = kRBallistic;                        // after it: the ballistic entry, the stern first
    if (phase_ == 3) { SetPhase(4); Note(L"ОТМЕНА: импульс прерван — баллистический вход кормой вперёд", kBad); }
    else Note(L"ОТМЕНА: баллистический вход кормой вперёд (тяга — только торможение)", kBad);
    if (phase_ == 6 || phase_ == 5) SetPhase(phase_ == 6 ? 7 : 5);
    reg_.nextEval = -1e9;
}
void Reentry::Reset(double /*now*/) {
    if (!CanReset()) return;
    armed_ = false; checkT_ = -1; goT_ = abT_ = -99; log_.lines.clear();
    mode_ = guidance::kIdle; phase_ = 0; tBurnStart_ = kNaN; reg_ = Reg(); dv_ = 0; maxG_ = 0;
    if (have_) Replan(true);
    Note(L"Сброс: автопилот СХОД выключен", kOk);
}

void Reentry::HandedOver() {
    if (!Engaged()) return;
    mode_ = guidance::kLanded; armed_ = false;
    Note(L"Управление передано «ПОСАДКЕ НА КОРМУ»", kOk);
}

}  // namespace tantra::reentry
