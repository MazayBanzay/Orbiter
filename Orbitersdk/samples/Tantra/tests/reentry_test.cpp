// Offline checks of the autopilot СХОД (orbiter2016/TantraReentry): the planner from a 300 km circular orbit of the Earth and of
// Mars for each regime and two masses (52,34 kt; 15 kt - the traps empty), the burn time for a site 2000 km downrange, and the
// live autopilot flying a point-mass ship (its commands applied, the same air and skin models) down to the landing's entry.
#include "../orbiter2016/TantraReentry.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

using namespace tantra;
using namespace tantra::reentry;
namespace ts = tantra::thermalscreen;
namespace gd = tantra::guidance;

static int fails = 0;
static void check(bool ok, const std::string& what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) ++fails; }
static std::string U8(const std::wstring& w) {   // the journal's text for the console (UTF-8)
    std::string s;
    for (wchar_t c : w) {
        unsigned u = unsigned(c);
        if (u < 0x80) s += char(u);
        else if (u < 0x800) { s += char(0xC0 | (u >> 6)); s += char(0x80 | (u & 0x3F)); }
        else { s += char(0xE0 | (u >> 12)); s += char(0x80 | ((u >> 6) & 0x3F)); s += char(0x80 | (u & 0x3F)); }
    }
    return s;
}
static V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 Mul(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
static V3 Crs(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static double Dt(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static double Ln(V3 a) { return std::sqrt(Dt(a, a)); }

struct World { const char* name; Planet pl; ts::Air air; };
static World Earth() {
    World w; w.name = "Земля"; w.pl.R = 6371e3; w.pl.mu = 3.986004e14; w.pl.omega = 7.2921e-5;   // the air: the mockup's exponential Earth
    return w;
}
static World Mars() {
    World w; w.name = "Марс"; w.pl.R = 3389.5e3; w.pl.mu = 4.2828e13; w.pl.omega = 7.0882e-5;
    for (int k = 0; k < ts::Air::kN; ++k) w.air.rho[k] = 0.020 * std::exp(-k * ts::Air::kStep / 11000.0);
    return w;
}
struct Ship { const char* name; double m, argon, iron; };
static const Ship kFull{"52,34 кт", 52.34e6, 6.2e6, 3.8e6}, kLight{"15 кт", 15.0e6, 3.0e6, 1.0e6};

// a circular orbit at 300 km over (lat 0, lon 0), the ascending node there, inclination inc: r, v inertial (the frame of now)
static void Orbit(const Planet& pl, double inc, V3* r, V3* v) {
    const double rr = pl.R + 300e3, vc = std::sqrt(pl.mu / rr);
    *r = {rr, 0, 0};
    *v = {0, vc * std::cos(inc * gd::kD2R), vc * std::sin(inc * gd::kD2R)};
}
static PlanIn In(const World& w, const Ship& s, int regime, double inc) {
    PlanIn in;
    in.pl = w.pl; in.air = w.air; in.regime = regime; in.wingSel = kWAuto; in.gLim = 3.0;
    Orbit(w.pl, inc, &in.r, &in.v);
    in.mass = s.m; in.argon = s.argon; in.iron = s.iron;
    in.expo.crests = in.expo.fin = regime == kRNose ? 1.0 : 0.0;
    return in;
}

static void Print(const ReentryPlan& P, double gLim, double ms) {
    std::printf("    импульс через %.0f с: dv %.1f м/с за %.1f с, перицентр %.1f км | вход %.0f м/с, %.2f° | план %.0f мс\n",
                P.tBurn, P.dvBurn, P.burnLen, P.hpT / 1e3, P.vEI, P.gamEI, ms);
    std::printf("    перегрузка %.2f g (предел %.1f) на %.1f км | торможение с %.1f км%s\n", P.nMax, gLim, P.nMaxH / 1e3,
                std::isfinite(P.brakeH) ? P.brakeH / 1e3 : -1.0, std::isfinite(P.flipH) ? (" | разворот с " + std::to_string(int(P.flipH / 1e3)) + " км").c_str() : "");
    std::printf("    зоны (пик / предел):");
    for (int z = 0; z < kZones; ++z) std::printf(" %s %.0f/%.0f", U8(kZoneRu[z]).c_str(), P.peakT[z], P.lim[z]);
    std::printf("\n    корма установки %.0f К | аргон %.2f кт, железо %.3f кт\n", P.sternTMax, P.argon / 1e6, P.iron / 1e6);
    std::printf("    вход посадки через %.0f с: %.0f м/с, верт. %.1f м/с, гориз. %.1f м/с, нос над горизонтом %.1f° | %.3f°, %.3f°%s%s\n",
                P.tHand, P.vHand, P.vzHand, P.vhHand, P.pitchHand, P.latHand * gd::kR2D, P.lonHand * gd::kR2D,
                P.ok ? "" : " | НЕВОЗМОЖНО: ", P.ok ? "" : U8(P.why).c_str());
}

static ReentryPlan Timed(const PlanIn& in, double* ms) {
    const auto t0 = std::chrono::steady_clock::now();
    ReentryPlan P = PlanReentry(in);
    *ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return P;
}

// the plan's own checks: what it promises is within the limits and ends at the landing's entry, stern down
static void Verify(const ReentryPlan& P, double gLim, const std::string& tag) {
    bool zones = true;
    for (int z = 0; z < kZones; ++z) zones = zones && P.peakT[z] <= P.lim[z] - kMarginK + 1e-6;
    check(P.nMax <= gLim + 0.05, tag + ": перегрузка в пределе");
    check(zones, tag + ": зоны ниже предела − 100 К");
    check(std::fabs(P.vzHand + kHandV) < 15.0 && P.vhHand < 30.0, tag + ": вход посадки ~5 км / −150 м/с");
    check(P.pitchHand > 75.0, tag + ": кормой вниз (нос выше 75°)");
}

// ---- the live autopilot on a point-mass ship ----
struct LiveShip {
    World w; Planet pl; V3 r, v; double t = 0, m, argon, iron; damage::Model skin; double pod = 0; int wing = 2; double sternT = 300;
    V3 noseI{0, 0, 1};                            // inertial
    double FmNow = 0, FpNow = 0;                  // N given last step
    ReentryState State(int regime) const {
        ReentryState s;
        s.simt = t; s.dt = 0.1; s.planet = pl; s.air = w.air;
        const V3 rF = {std::cos(-pl.omega * t) * r.x - std::sin(-pl.omega * t) * r.y, std::sin(-pl.omega * t) * r.x + std::cos(-pl.omega * t) * r.y, r.z};
        const double rr = Ln(r);
        s.lat = std::asin(rF.z / rr); s.lon = std::atan2(rF.y, rF.x); s.alt = rr - pl.R;
        const V3 u = Mul(r, 1 / rr), e = Mul(Crs({0, 0, 1}, u), 1 / Ln(Crs({0, 0, 1}, u))), n = Crs(u, e);
        const V3 vA = Add(v, Mul(Crs({0, 0, pl.omega}, r), -1));
        s.vE = Dt(vA, e); s.vN = Dt(vA, n); s.vU = Dt(vA, u);
        const double nz = Dt(noseI, u), ne = Dt(noseI, e), nn = Dt(noseI, n);
        s.pitch = std::asin(std::max(-1.0, std::min(1.0, nz))) * gd::kR2D; s.hdg = gd::N360(std::atan2(ne, nn) * gd::kR2D);
        const double va = Ln(vA);
        s.aoa = va > 1 ? std::acos(std::max(-1.0, std::min(1.0, Dt(noseI, vA) / va))) * gd::kR2D : 0.0;
        s.rho = w.air.Rho(s.alt); s.mach = va / 300.0; s.q = 0.5 * s.rho * va * va;
        s.mass = m; s.argon = argon; s.iron = iron;
        s.Fmarch = gd::MarchMax(); s.Fpods = pod >= 1 ? gd::PodsMax() : 0.0; s.FmNow = FmNow; s.FpNow = FpNow;
        s.sternT = sternT;
        for (int z = 0; z < kZones; ++z) { s.skinT[z] = skin.Temperature(z); s.skinLim[z] = skin.Limit(z); s.skinFlux[z] = skin.HeatFlux(z); }
        s.skin = skin; s.wingMode = wing; s.fold = wing == 2 ? 1 : 0; s.crestAvail = kCrestOf[wing]; s.podsOut = pod;
        s.expo.crests = s.expo.fin = wing == 2 ? 0 : 1; s.expo.pods = pod;
        (void)regime;
        return s;
    }
};

static void LiveRun(const World& w, const Ship& sh, int regime, double inc, int site) {
    std::printf("\n== живой автопилот: %s, %s, %s ==\n", w.name, sh.name, U8(kRegimes[regime]).c_str());
    LiveShip S; S.w = w; S.pl = w.pl; Orbit(w.pl, inc, &S.r, &S.v); S.m = sh.m; S.argon = sh.argon; S.iron = sh.iron;
    S.noseI = Mul(S.v, -1 / Ln(S.v));
    Reentry ap;
    std::vector<Site> sites;
    if (site >= 0) sites.push_back({L"цель", 0.0, 0.0});
    double sys = 0.0;
    ap.Step(S.State(regime), sys);
    while (ap.RegimeSel() != regime) ap.StepRegime(+1);
    if (site >= 0) {
        // the site: where a burn 270 s after the earliest lands (2000 km down the track of the earliest landing)
        PlanIn in = In(w, sh, regime, inc); in.tBurn = kLead + 270.0;
        const ReentryPlan Ph = PlanReentry(in);
        sites[0].lat = Ph.latHand; sites[0].lon = Ph.lonHand;
        ap.SetSites(sites); ap.StepSite(+1);
    }
    ap.Arm(sys); for (int i = 0; i < 60; ++i) { sys += 0.1; ap.Step(S.State(regime), sys); }
    check(ap.Armed(), "ВЗВЕСТИ: готовность подтверждена");
    if (!ap.Armed()) { for (const Check& c : ap.ChecksShown()) std::printf("    [%s] %s\n", c.ok ? "+" : "-", U8(c.t).c_str()); return; }
    ap.Start(sys); ap.Start(sys + 0.5);
    check(ap.Engaged(), "ПУСК подтверждён");
    const double dt = 0.1;
    double maxG = 0, peakT[kZones] = {}; bool hand = false;
    const double hpPlan = ap.Plan().tHand;
    for (int k = 0; k < int(4 * 3600 / dt) && !hand; ++k) {
        const ReentryState st = S.State(regime);
        sys += dt;
        ap.Step(st, sys);
        const ReentryCmd& c = ap.Cmd();
        if (c.handover) { hand = true; break; }
        // the ship: the attitude at once on the command, the thrust against its nose's direction (+z pushes forward)
        const double rr = Ln(S.r), h = rr - S.pl.R;
        const V3 u = Mul(S.r, 1 / rr), e = Mul(Crs({0, 0, 1}, u), 1 / Ln(Crs({0, 0, 1}, u))), n = Crs(u, e);
        if (c.attitude) S.noseI = Add(Add(Mul(e, c.nose.x), Mul(n, c.nose.y)), Mul(u, c.nose.z));
        if (c.wingMode >= 0) S.wing = c.wingMode;
        if (c.podsOut) S.pod = std::min(1.0, S.pod + dt / kPodT);
        const V3 vA = Add(S.v, Mul(Crs({0, 0, S.pl.omega}, S.r), -1));
        const double va = Ln(vA), rho = w.air.Rho(h), q = 0.5 * rho * va * va;
        const double aoa = st.aoa * gd::kD2R;
        double SL = 0, SD = 0; ts::AeroAreas(aoa, va / 300.0, kCrestOf[S.wing], 0.0, &SL, &SD);
        V3 a{0, 0, 0};
        if (va > 1) {
            const V3 vh = Mul(vA, 1 / va);
            V3 lift = Add(S.noseI, Mul(vh, -Dt(S.noseI, vh)));   // lift toward the nose's side of the airspeed
            const double ll = Ln(lift);
            a = Add(a, Mul(vh, -q * SD / S.m));
            if (ll > 1e-6) a = Add(a, Mul(lift, q * std::fabs(SL) / S.m / ll * (SL >= 0 ? 1 : -1)));
        }
        const int mass = c.massWanted;
        const double F = c.thrust && mass != gd::kNoMass ? c.march * gd::MarchMax() + (S.pod >= 1 ? c.pods * gd::PodsMax() : 0.0) : 0.0;
        a = Add(a, Mul(S.noseI, F / S.m));
        S.FmNow = c.thrust && mass != gd::kNoMass ? c.march * gd::MarchMax() : 0.0; S.FpNow = F - S.FmNow;
        const double felt = Ln(a) / gd::kG0;
        if (st.alt < kEI) maxG = std::max(maxG, felt);
        const V3 g = Mul(S.r, -S.pl.mu / (rr * rr * rr));
        S.v = Add(S.v, Mul(Add(a, g), dt)); S.r = Add(S.r, Mul(S.v, dt));
        if (F > 0) {
            const double vexM = mass == gd::kIron ? gd::CupOf(gd::kAMarch, gd::kBNom, gd::kPfMarch, true).v : gd::kVArgon;
            const double md = c.march * gd::MarchMax() / vexM + (S.pod >= 1 ? c.pods * gd::PodsMax() / gd::kVArgon : 0.0);
            if (mass == gd::kIron) S.iron -= md * dt; else S.argon -= md * dt;
            S.m -= md * dt;
        }
        damage::Flight f; f.rho = rho; f.v = va; f.mach = va / 300.0; f.aoa = aoa; f.gLoad = felt;
        damage::Exposure x; x.crests = x.fin = S.wing == 2 ? 0 : 1; x.pods = S.pod; x.gear = 0;
        damage::Ground gr;
        S.skin.Step(dt, f, x, gr, false);
        for (int z = 0; z < kZones; ++z) peakT[z] = std::max(peakT[z], S.skin.Temperature(z));
        S.t += dt;
        if (h < 0) break;
    }
    const ReentryState st = S.State(regime);
    const double vh = std::hypot(st.vE, st.vN);
    std::printf("    передача через %.0f с (план %.0f с): %.2f км, верт. %.1f м/с, гориз. %.1f м/с, нос %.1f° | dv с ПУСКА %.0f м/с\n",
                S.t, hpPlan, st.alt / 1e3, st.vU, vh, st.pitch, ap.Dv());
    std::printf("    перегрузка %.2f g | зоны:", maxG);
    for (int z = 0; z < kZones; ++z) std::printf(" %s %.0f", U8(kZoneRu[z]).c_str(), peakT[z]);
    std::printf(" | аргон %.2f кт, железо %.3f кт\n", (sh.argon - S.argon) / 1e6, (sh.iron - S.iron) / 1e6);
    if (site >= 0) {
        const V3 a{std::cos(st.lat) * std::cos(st.lon), std::cos(st.lat) * std::sin(st.lon), std::sin(st.lat)};
        const V3 b{std::cos(sites[0].lat) * std::cos(sites[0].lon), std::cos(sites[0].lat) * std::sin(sites[0].lon), std::sin(sites[0].lat)};
        const double miss = w.pl.R * std::atan2(Ln(Crs(a, b)), Dt(a, b));
        std::printf("    промах до площадки %.1f км\n", miss / 1e3);
        check(miss < 20e3, "живой: промах < 20 км");
    }
    int shown = 0;
    for (const auto& l : ap.Log().lines) { if (shown++ >= 14) break; std::printf("    %s %s\n", U8(l.tt).c_str(), U8(l.txt).c_str()); }
    bool zones = true;
    for (int z = 0; z < kZones; ++z) zones = zones && peakT[z] <= S.skin.Limit(z) - kMarginK + 15.0;
    check(hand, "живой: передача «ПОСАДКЕ НА КОРМУ»");
    check(std::fabs(st.alt - kHandH) < 300 && std::fabs(st.vU + kHandV) < 20 && vh < 40, "живой: ~5 км / −150 м/с");
    check(st.pitch > 75, "живой: кормой вниз");
    check(maxG <= ap.GLim() + 0.15, "живой: перегрузка в пределе");
    check(zones, "живой: зоны ниже предела − 100 К (допуск 15 К)");
}

int main() {
    // the forecast's additive thrust: 0 is the old forecast; a held braking shortens the entry and cools it
    {
        ts::ForecastIn f; f.path.h = 120e3; f.path.v = 7400; f.path.gamma = -2.0 * gd::kD2R; f.path.mass = 52.34e6; f.path.aoa = gd::kPi;
        f.path.crestAvail = 0.0; f.path.expo.crests = f.path.expo.fin = 0;
        const ts::Forecast a = ts::RunForecast(f);
        f.path.thrustAcc = 12.0;
        const ts::Forecast b = ts::RunForecast(f);
        std::printf("прогноз кормой 52 кт, 120 км 7,4 км/с −2°: без тяги корма %.0f К, %.2f g, конец %.0f с; с торможением 12 м/с² корма %.0f К, %.2f g, конец %.0f с\n",
                    a.peakT[damage::kZoneStern], a.nMax, a.endT, b.peakT[damage::kZoneStern], b.nMax, b.endT);
        check(b.peakT[damage::kZoneStern] < a.peakT[damage::kZoneStern] && b.endT < a.endT, "прогноз: тяга против скорости охлаждает и укорачивает вход");
        check(b.nMax >= 12.0 / 9.80665 - 1e-6, "прогноз: перегрузка учитывает тягу");
    }
    const World worlds[2] = {Earth(), Mars()};
    const Ship ships[2] = {kFull, kLight};
    for (const World& w : worlds)
        for (const Ship& s : ships)
            for (int rg = 0; rg < kRegimeCount; ++rg) {
                std::printf("\n== %s, %s, %s, ПО ТРАССЕ ==\n", w.name, s.name, U8(kRegimes[rg]).c_str());
                double ms = 0;
                const PlanIn in = In(w, s, rg, 51.6);
                const ReentryPlan P = Timed(in, &ms);
                Print(P, in.gLim, ms);
                const std::string tag = std::string(w.name) + " " + s.name + " " + U8(kRegimes[rg]);
                if (rg == kRStern) { check(P.ok, tag + ": план выполним"); Verify(P, in.gLim, tag); }
                else if (P.ok) Verify(P, in.gLim, tag);
                else {
                    check(!P.why.empty(), tag + ": режим запрещён с причиной (" + U8(P.why) + ")");
                }
            }
    // the site 2000 km down the track: where a burn 270 s after the earliest lands - the planner finds the burn from scratch
    for (const World& w : worlds)
        for (double inc : {51.6, 0.0}) {
            const int rg = kRStern;
            PlanIn in = In(w, kFull, rg, inc);
            const ReentryPlan P0 = PlanReentry(in);
            in.tBurn = kLead + 2000e3 / (std::sqrt(w.pl.mu / (w.pl.R + 300e3)) * w.pl.R / (w.pl.R + 300e3));
            const ReentryPlan Ph = PlanReentry(in);
            in.tBurn = kNaN; in.siteValid = true; in.siteLat = Ph.latHand; in.siteLon = Ph.lonHand;
            double ms = 0;
            const ReentryPlan P = Timed(in, &ms);
            const double d = w.pl.R * std::acos(std::min(1.0, std::sin(P0.latHand) * std::sin(Ph.latHand) + std::cos(P0.latHand) * std::cos(Ph.latHand) * std::cos(P0.lonHand - Ph.lonHand)));
            std::printf("\n== %s, i %.1f°, площадка в %.0f км по трассе от ближайшей посадки ==\n", w.name, inc, d / 1e3);
            std::printf("    импульс через %.0f с (скрытый %.0f с), промах %.2f км (вдоль %.2f, поперёк %.2f) | план %.0f мс\n",
                        P.tBurn, Ph.tBurn, P.miss / 1e3, P.missAlong / 1e3, P.missCross / 1e3, ms);
            check(P.ok && P.miss < 20e3, std::string(w.name) + " i " + std::to_string(int(inc)) + ": промах до площадки < 20 км");
        }
    // an independent site: 2000 km east along the equator of the earliest landing (an equatorial orbit: the track is the equator)
    for (const World& w : worlds) {
        PlanIn in = In(w, kFull, kRStern, 0.0);
        const ReentryPlan P0 = PlanReentry(in);
        in.siteValid = true; in.siteLat = 0.0; in.siteLon = P0.lonHand + 2000e3 / w.pl.R;
        double ms = 0;
        const ReentryPlan P = Timed(in, &ms);
        std::printf("\n== %s, экватор, площадка в 2000 км восточнее ближайшей посадки ==\n", w.name);
        std::printf("    импульс через %.0f с (ближайший %.0f с), промах %.2f км (вдоль %.2f, поперёк %.2f) | план %.0f мс\n",
                    P.tBurn, P0.tBurn, P.miss / 1e3, P.missAlong / 1e3, P.missCross / 1e3, ms);
        check(P.ok && P.miss < 20e3, std::string(w.name) + " экватор: промах до площадки < 20 км");
    }
    // the live autopilot: КОРМОЙ · ТЯГА on the Earth (52 kt, with the site) and on Mars (15 kt)
    LiveRun(Earth(), kFull, kRStern, 51.6, 0);
    LiveRun(Mars(), kLight, kRStern, 25.0, -1);
    std::printf("\n%s (%d)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
