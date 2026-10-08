// TantraThermalScreen: see TantraThermalScreen.h. Draw() follows draw() of Tantra_Design/tantra_thermal_screen.html block by block -
// drawShip() ThermalMap(), drawZones() ZoneTable(), drawCorridor() CorridorPlot(), drawFlight() FlightBox(), drawEngines()
// EngineBox(); the coordinates are the mockup's. RunForecast() (its predict()) lives in TantraEntryForecast.h.
#include "TantraThermalScreen.h"
#include "../core/Aero.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tantra::thermalscreen {

using namespace tantra::scr;
using namespace tantra::scr::ui;
namespace dm = tantra::damage;

namespace {

constexpr double kG0 = 9.80665;                   // g, the unit of the load
constexpr double kSound = 300.0;                  // the speed of sound of the forecast's Mach number (the mockup's) [m/s]
constexpr double kTop = 125e3;                    // the corridor's top: climbing out above it the entry is a skip [m]
constexpr double kEndV = 600.0, kEndH = 12e3;     // the entry is over slower than this [m/s] or below this [m]
constexpr double kAmbient = 250.0;                // the corridor's ambient temperature [K]
constexpr double kVMax = 12000.0, kHMax = 125e3;  // the corridor's axes
constexpr unsigned cEdge = 0x6a8090;              // the hull's outline

const wchar_t* const kZoneName[kZones] = {L"нос (иридий)", L"днище", L"кромки крыльев", L"перо · гребни", L"корма", L"ноги (створки)", L"гондолы"};
const wchar_t* const kZoneShort[kZones] = {L"нос", L"днище", L"кромки", L"перо", L"корма", L"ноги", L"гондолы"};   // the callouts: the first word
const wchar_t* const kWingTxt[3] = {L"90°", L"30°", L"сложены"};

double Lim(const View& v, int z) { return v.lim[z] > 0.0 ? v.lim[z] : 2000.0; }
std::wstring Int(double x) { return std::to_wstring(long(std::lround(x))); }   // the mockup's String(z.lim): no grouping
std::wstring GTxt(double g) { return std::fabs(g - std::round(g)) < 0.05 ? Fmt(g, 0) : Fmt(g, 1); }
int WingIdx(const View& v) { return v.wingFold >= 0.99 ? 2 : (std::max)(0, (std::min)(2, v.wingMode)); }
// the skin as shown: the stern zone (the nacelle cluster round the cup) is the plant's stern when that is hotter - as the plant
// screen's thermal map and the mockup's skinStep(sternHot)
void Shown(const View& v, double* sk) {
    for (int z = 0; z < kZones; ++z) sk[z] = v.skin[z];
    sk[dm::kZoneStern] = (std::max)(sk[dm::kZoneStern], v.sternT);
}
// an entry begins descending fast below the corridor's top (the mockup's scenarios began at 120 km)
bool Begins(const View& v) {
    const Path& p = v.path;
    return !v.onGround && p.h < kTop && p.h > kEndH && p.v > 1500.0 && p.gamma < 0.0;
}
std::wstring Where(const View& v, bool entry, bool climb) {   // the mockup's scenario name in the title
    if (entry) return L"ВХОД В АТМОСФЕРУ";
    if (v.onGround) return L"НА ГРУНТЕ";
    if (climb) return L"ПОДЪЁМ";
    return v.rho > 1e-5 ? L"АТМОСФЕРА" : L"КОСМОС";
}
std::wstring EntryLine(const View& v) {
    const Path& p = v.path;
    return L"Вход: " + Fmt(p.h / 1e3, 0) + L" км, " + Fmt(p.v, 0) + L" м/с, угол " + Fmt(std::fabs(p.gamma) * 180 / kPi, 1) + L"°";
}
std::wstring LostWhy(const View& v, const double* sk) {
    if (v.plantLost) return L"корма прогорела (" + Int(v.tLost) + L" К)";
    int w = -1; double r = 1.0;
    for (int z : {int(dm::kZoneNose), int(dm::kZoneBelly)}) if (sk[z] / Lim(v, z) > r) { r = sk[z] / Lim(v, z); w = z; }
    if (w >= 0) return std::wstring(kZoneName[w]) + L": " + Fmt(sk[w], 0) + L" К при пределе " + Int(Lim(v, w)) + L" К";
    return L"корпус разрушен";
}

// the glow of hot metal (the mockup's HEAT) and the canvas' gradient between two stops
unsigned HeatCol(double T) {
    struct Stop { double t; int r, g, b; };
    static const Stop kH[8] = {{250, 24, 50, 58}, {700, 44, 70, 76}, {950, 110, 30, 20}, {1250, 185, 50, 26}, {1600, 232, 112, 30},
                               {2000, 245, 192, 64}, {2400, 255, 242, 192}, {3000, 255, 255, 255}};
    int i = 1;
    while (i < 7 && T > kH[i].t) ++i;
    const Stop& a = kH[i - 1];
    const Stop& b = kH[i];
    const double f = (std::max)(0.0, (std::min)(1.0, (T - a.t) / (b.t - a.t)));
    auto ch = [f](int x, int y) { return int(std::lround(x + (y - x) * f)); };
    return Rgb(ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b));
}
unsigned ColLerp(unsigned c0, unsigned c1, double f) { return Mix(c1, c0, f); }

std::vector<Pt> Quad(Pt a, Pt b, Pt c, int n) {   // the canvas' quadraticCurveTo as points
    std::vector<Pt> r;
    for (int i = 0; i <= n; ++i) {
        const double t = double(i) / n, u = 1 - t;
        r.push_back({u * u * a.x + 2 * u * t * b.x + t * t * c.x, u * u * a.y + 2 * u * t * b.y + t * t * c.y});
    }
    return r;
}
// a convex polygon cut by an axis-parallel line, one side kept (Sutherland-Hodgman); four cuts make a box of it
std::vector<Pt> ClipHalf(const std::vector<Pt>& p, bool yAxis, double at, bool keepAbove) {
    std::vector<Pt> o;
    const size_t n = p.size();
    for (size_t i = 0; i < n; ++i) {
        const Pt& a = p[i];
        const Pt& b = p[(i + 1) % n];
        const double da = (yAxis ? a.y : a.x) - at, db = (yAxis ? b.y : b.x) - at;
        const bool ia = keepAbove ? da >= 0.0 : da <= 0.0, ib = keepAbove ? db >= 0.0 : db <= 0.0;
        if (ia) o.push_back(a);
        if (ia != ib) { const double t = da / (da - db); o.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}); }
    }
    return o;
}
std::vector<Pt> ClipBox(std::vector<Pt> p, double x0, double y0, double x1, double y1) {
    p = ClipHalf(p, false, x0, true); p = ClipHalf(p, false, x1, false);
    p = ClipHalf(p, true, y0, true); p = ClipHalf(p, true, y1, false);
    return p;
}
double Cross(const Pt& a, const Pt& b, const Pt& c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }
// any simple polygon (the canvas fills concave paths, the pad's Polygon only convex ones safely): cut into triangles by ears
void FillPoly(Canvas& g, const std::vector<Pt>& p, unsigned col, double a) {
    if (p.size() < 3) return;
    double area = 0.0;
    for (size_t i = 0; i < p.size(); ++i) { const Pt& u = p[i]; const Pt& w = p[(i + 1) % p.size()]; area += u.x * w.y - w.x * u.y; }
    const double sg = area >= 0.0 ? 1.0 : -1.0;
    std::vector<size_t> idx(p.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    while (idx.size() > 3) {
        const size_t n = idx.size();
        bool cut = false;
        for (size_t k = 0; k < n && !cut; ++k) {
            const size_t ka = (k + n - 1) % n, kc = (k + 1) % n;
            const Pt& A = p[idx[ka]];
            const Pt& B = p[idx[k]];
            const Pt& C = p[idx[kc]];
            if (Cross(A, B, C) * sg <= 0.0) continue;              // a reflex (or straight) corner: no ear
            bool inside = false;
            for (size_t m = 0; m < n && !inside; ++m) {
                if (m == k || m == ka || m == kc) continue;
                const Pt& Q = p[idx[m]];
                inside = Cross(A, B, Q) * sg > 0.0 && Cross(B, C, Q) * sg > 0.0 && Cross(C, A, Q) * sg > 0.0;
            }
            if (inside) continue;
            g.Shape({A, B, C}, col, a);
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(k));
            cut = true;
        }
        if (!cut) break;                                            // degenerate: the rest as it is
    }
    std::vector<Pt> rest;
    for (size_t i : idx) rest.push_back(p[i]);
    g.Shape(rest, col, a);
}
void Outline(Canvas& g, std::vector<Pt> p, unsigned c, double lw) {   // the canvas' closePath + stroke
    if (p.size() < 2) return;
    p.push_back(p.front());
    g.Polyline(p, c, lw);
}
// a dashed path as the canvas' setLineDash: the pattern runs on along the whole path; phase - where in the pattern it starts
void DashPath(Canvas& g, const std::vector<Pt>& p, unsigned c, double lw, double on, double off, double phase, double a = 1.0) {
    const double per = on + off;
    if (per <= 0.0) return;
    double s0 = 0.0;
    for (size_t i = 1; i < p.size(); ++i) {
        const double dx = p[i].x - p[i - 1].x, dy = p[i].y - p[i - 1].y, L = std::hypot(dx, dy);
        if (L < 1e-9) continue;
        double s = s0;
        while (s < s0 + L - 1e-9) {
            double ph = std::fmod(s + phase, per);
            if (ph < 0.0) ph += per;
            if (ph < on) {
                const double e = (std::min)(s0 + L, s + on - ph), u0 = (s - s0) / L, u1 = (e - s0) / L;
                g.Line(p[i - 1].x + dx * u0, p[i - 1].y + dy * u0, p[i - 1].x + dx * u1, p[i - 1].y + dy * u1, c, lw, a);
                s = (std::max)(e, s + 1e-6);
            } else s += (std::max)(per - ph, 1e-6);
        }
        s0 += L;
    }
}
void Bar0(Canvas& g, double x, double y, double w, double h, double f, unsigned col) {   // the mockup's bar(): no frame
    g.Fill(x, y, w, h, 0x14211e);
    g.Fill(x, y, w * (std::max)(0.0, (std::min)(1.0, f)), h, col);
}
void Cell(Canvas& g, double x, double y, const std::wstring& name, double T) {   // a chamber or a cup: its name, its temperature
    const unsigned c = ZoneCol(T, 2000);
    g.Fill(x, y, 76, 30, 0x14211e);
    g.Stroke(x, y, 76, 30, c, 2);
    g.T(name, x + 6, y + 20, cDim, 11);
    g.T(Fmt(T, 0), x + 72, y + 20, c, 13, 2, 700);
}

// AeroAreas(): TantraEntryForecast.h (the forecast's and the corridor's)

// the zone model's own coefficients at an attitude, read off core/Damage (its nose radii, exposure laws and emissivity are private
// to Damage.cpp): one step far longer than any thermal lag (the skin settles to its equilibrium) at a known flow and no ambient -
// each zone's flux per sqrt(rho) V^3 (Sutton-Graves' k / sqrt(Rn) x the exposure) and, from q = eps sigma T^4, eps sigma
struct Coef { double c[kZones] = {}; double es = 0.0; };
Coef Probe(double aoa, const dm::Exposure& x) {
    Coef k;
    dm::Model m;
    dm::Flight f;
    dm::Ground gr;
    f.rho = 1e-4; f.v = 4000.0; f.mach = f.v / kSound; f.aoa = aoa; f.ambientT = 0.0;
    m.Step(1e6, f, x, gr, false);
    const double base = std::sqrt(f.rho) * f.v * f.v * f.v;
    double best = 0.0;
    for (int z = 0; z < kZones; ++z) {
        const double q = m.HeatFlux(z), T = m.Temperature(z);
        k.c[z] = q / base;
        if (q > best && T > 1.0) { best = q; k.es = q / (T * T * T * T); }
    }
    return k;
}

// the engines' temperatures the plant does not keep - the same derived values as the plant screen's thermal map
struct EngT { double cup = 0, jacket = 0, pods = 0, rad = 0, ana = 0, retro[2] = {}; bool cupHot = false, flow = false; };
EngT EngineTemps(const View& v, const double* sk) {
    EngT e;
    const double thr = v.plantRun ? v.thr : 0.0;
    e.cupHot = thr > 0.0;
    e.cup = v.sternT + 250.0 * thr;                                       // the cup's wall: the stern and the burn behind it
    e.flow = v.plantRun && v.mdot > 0.0 && v.reactMass != 2;             // the jacket: the reaction mass through it
    const double jIn = v.reactMass == 0 ? 87.0 : 300.0;
    e.jacket = e.flow ? jIn + (std::min)(2600.0, v.heatIn / (std::max)(1.0, v.mdot * (v.reactMass == 0 ? 520.0 : 450.0))) : 0.0;
    e.pods = v.pods == 2 ? 600.0 + 500.0 * v.podLevel : (std::max)(290.0, sk[dm::kZonePods]);
    e.rad = (std::max)(v.sternT * 0.55, sk[dm::kZoneFin]);               // the crests and the fin: the stern's heat or their own skin
    e.ana = (std::max)(290.0, sk[dm::kZoneStern] * 0.9);                 // the anamezon chambers soak the stern
    for (int i = 0; i < 2; ++i) e.retro[i] = 290.0 + (std::max)(0.0, sk[dm::kZoneNose] - 290.0) * (0.35 + 0.03 * i);   // the nose
    return e;
}

}  // namespace

// ---- the atmosphere and the forecast: TantraEntryForecast.h ----

// ---- the screen ----
bool Screen::Climbing(const View& v) const {   // the mockup's «СТАРТ С ЗЕМЛИ»: up through the air on the planetary engines
    return !v.onGround && !entry_ && v.rho > 1e-5 && v.path.gamma > 0.0 && (v.thr > 0.0 || v.pods == 2);
}

void Screen::Track(const View& v) {
    double sk[kZones];
    Shown(v, sk);
    const Path& p = v.path;
    if (seen_ && v.t < lastT_ - 1.0) seen_ = false;                      // the clock went back: another scenario
    if (!seen_) {
        seen_ = true; lastT_ = histT_ = v.t; heat_ = 0.0; edgeHurt_ = v.edgeIntegrity < 0.999;
        hist_.clear(); track_.clear(); fc_ = Forecast();
        for (int z = 0; z < kZones; ++z) { prev_[z] = sk[z]; dT_[z] = 0.0; crossed_[z] = sk[z] > Lim(v, z); }
        entry_ = Begins(v);
        log_.Add(v.t, entry_ ? EntryLine(v) : Where(v, false, Climbing(v)), 0);   // the mockup's reset(): where it starts
        last_ = v;
        return;
    }
    const double dt = v.t - lastT_;
    if (dt > 0.0) {
        for (int z = 0; z < kZones; ++z) { dT_[z] += ((sk[z] - prev_[z]) / dt - dT_[z]) * (std::min)(1.0, dt / 2.0); prev_[z] = sk[z]; }
        if (entry_) heat_ += v.flux[dm::kZoneNose] * dt;
        lastT_ = v.t;
    }
    // the entry: descending fast through the corridor's top it begins; slow, low or on the ground it is over; climbing back out
    // above the top it was a skip
    if (!entry_) {
        if (Begins(v)) { entry_ = true; heat_ = 0.0; track_.clear(); fc_ = Forecast(); log_.Add(v.t, EntryLine(v), 0); }
    } else if (v.onGround || p.v < kEndV || p.h < kEndH) {
        entry_ = false; fc_ = Forecast();
        log_.Add(v.t, L"Вход завершён: " + Fmt(p.h / 1e3, 0) + L" км, " + Fmt(p.v, 0) + L" м/с", 0);
    } else if (p.h > kTop && p.gamma > 0.0) {
        entry_ = false; fc_ = Forecast();
        log_.Add(v.t, L"Отскок: корабль ушёл обратно в космос", 2);
    }
    // the history every 2 s (5 minutes) and the flown track (airborne under the corridor's top)
    if (v.t - histT_ >= 2.0) {
        histT_ = v.t;
        std::array<double, kZones> row;
        for (int z = 0; z < kZones; ++z) row[z] = sk[z];
        hist_.push_back(row);
        while (hist_.size() > 150) hist_.pop_front();
        if (!v.onGround && p.h < kTop) { track_.push_back({p.v, p.h}); while (track_.size() > 1800) track_.pop_front(); }
    }
    // the events (the mockup's alarm() calls)
    const View& o = last_;
    for (int z = 0; z < kZones; ++z) {
        const double lim = Lim(v, z);
        if (sk[z] > lim) {
            if (!crossed_[z]) { crossed_[z] = true; log_.Add(v.t, std::wstring(kZoneName[z]) + L": выше предела " + Int(lim) + L" К", 2); }
        } else if (sk[z] < lim - 100.0) crossed_[z] = false;
    }
    if (v.edgeIntegrity < 0.999 && !edgeHurt_ && sk[dm::kZoneCrestEdge] > Lim(v, dm::kZoneCrestEdge)) {
        edgeHurt_ = true;
        log_.Add(v.t, L"Кромки крыльев оплавлены — повреждение", 2);
    }
    if (v.edgeIntegrity >= 0.999) edgeHurt_ = false;                     // repaired
    if (v.hullLost && !o.hullLost) {
        const bool burnt = sk[dm::kZoneNose] > Lim(v, dm::kZoneNose) || sk[dm::kZoneBelly] > Lim(v, dm::kZoneBelly);
        log_.Add(v.t, burnt ? L"ОБШИВКА ПРОГОРЕЛА — КОРАБЛЬ ПОТЕРЯН" : L"КОРПУС РАЗРУШЕН — КОРАБЛЬ ПОТЕРЯН", 2);
    }
    if (v.plantLost && !o.plantLost) log_.Add(v.t, L"КОРМА ПРОГОРЕЛА — КОРАБЛЬ ПОТЕРЯН", 2);
    if (WingIdx(v) != WingIdx(o)) log_.Add(v.t, std::wstring(L"Крылья: ") + kWingTxt[WingIdx(v)], 0);
    if (o.onGround && !v.onGround) {                                      // off the ground: a new track
        track_.clear();
        std::wstring how;
        if (v.thr > 0.0) how = v.reactMass == 0 ? L"маршевый на аргоне" : v.reactMass == 1 ? L"маршевый на железе" : L"маршевый";
        if (v.pods == 2) how += how.empty() ? L"гондолы" : L", гондолы";
        if (!how.empty()) log_.Add(v.t, L"Старт: " + how, 0);
    }
    last_ = v;
}

std::pair<std::wstring, unsigned> Screen::Advice(const View& v) const {   // what the screen recommends from the forecast
    if (v.hullLost || v.plantLost) return {L"корабль потерян", cRd};
    if (!entry_ || !fc_.valid)
        return {Climbing(v) ? L"обшивка: нагрев на подъёме мал, следить за кормой и рубашкой" : L"обшивка остывает излучением", cDim};
    const Forecast& p = fc_;
    std::vector<int> over;
    for (int z = 0; z < kZones; ++z) if (p.peakT[z] > Lim(v, z)) over.push_back(z);
    auto has = [&](int z) { return std::find(over.begin(), over.end(), z) != over.end(); };
    const bool hot = has(dm::kZoneNose) || has(dm::kZoneBelly);
    if (v.path.v > 8500 && hot)
        return {L"ПРОГНОЗ: нос " + Fmt(p.peakT[dm::kZoneNose], 0) + L" К — с " + Fmt(v.path.v / 1e3, 1) + L" км/с не входить: двигателями до орбитальной", cRd};
    if (p.skip) return {L"ПРОГНОЗ: ОТСКОК — круче вход или крен: подъёмную силу вниз", cRd};
    if (has(dm::kZoneCrestEdge) && WingIdx(v) != 2)
        return {L"ПРОГНОЗ: кромки " + Fmt(p.peakT[dm::kZoneCrestEdge], 0) + L" К > " + Int(Lim(v, dm::kZoneCrestEdge)) + L" — СЛОЖИТЬ КРЫЛЬЯ", cRd};
    if (hot)
        return {L"ПРОГНОЗ: " + std::wstring(kZoneName[over[0]]) + L" " + Fmt(p.peakT[over[0]], 0) + L" К > " + Int(Lim(v, over[0])) + L" — положе вход или больше атака", cRd};
    if (p.nMax > v.gLimit) return {L"ПРОГНОЗ: перегрузка " + Fmt(p.nMax, 1) + L" g > " + GTxt(v.gLimit) + L" — положе вход", cYe};
    if (!over.empty()) return {L"ПРОГНОЗ: " + std::wstring(kZoneName[over[0]]) + L" выше предела", cYe};
    return {L"ПРОГНОЗ: вход в пределах по всем зонам", cGr};
}

void Screen::Draw(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v) {
    hits_.clear();
    if (!skp) return;
    Track(v);
    Canvas g(skp, font, ox, oy);
    const double W = w, H = h;
    double sk[kZones];
    Shown(v, sk);
    const bool lost = v.hullLost || v.plantLost;
    g.Fill(0, 0, W, H, cBg); g.Stroke(5, 5, W - 10, H - 10, cFr, 2);
    // the title bar: the tabs (where the mechanisation page has them), the state, where the ship is
    Btn(g, hits_, L"МЕХАНИЗАЦИЯ", 18, 14, 190, 38, false, kCmdTabMech);
    Btn(g, hits_, L"ТЕПЛО", 214, 14, 110, 38, true, kCmdTabThermal);
    double worst = v.sternT / (std::max)(1.0, v.tLost) * 1.2;
    for (int z = 0; z < kZones; ++z) worst = (std::max)(worst, sk[z] / Lim(v, z));
    const wchar_t* st = lost ? L"ПОТЕРЯН" : worst >= 1 ? L"ПЕРЕГРЕВ" : worst >= 0.9 ? L"У ПРЕДЕЛА" : worst >= 0.75 ? L"ВНИМАНИЕ" : L"НОРМА";
    const unsigned stCol = lost || worst >= 1 ? cRd : worst >= 0.9 ? cOr : worst >= 0.75 ? cYe : cGr;
    g.Disc(350, 31, 9, stCol); g.T(st, 366, 38, stCol, 18, 0, 700);
    g.T(Where(v, entry_, Climbing(v)) + L" · " + Fmt(v.path.mass / 1e6, 1) + L" кт · крылья " + kWingTxt[WingIdx(v)] + L" · " + Clock(v.t),
        W - 22, 38, cDim, 13, 2);
    ThermalMap(g, v, sk);
    ZoneTable(g, v, sk, W);
    CorridorPlot(g, v, H);
    FlightBox(g, v, H);
    EngineBox(g, v, sk, W, H);
    if (lost) {
        const double cx = W / 2;
        g.Fill(cx - 500, 250, 1000, 200, Rgb(60, 0, 0), 0.75);
        g.Stroke(cx - 500, 250, 1000, 200, cRd, 3);
        g.T(L"КОРАБЛЬ ПОТЕРЯН", cx, 330, 0xffd0c8, 42, 1, 800);
        g.T(LostWhy(v, sk), cx, 380, 0xffd0c8, 20, 1, 600);
    }
}

// ---- the skin's thermal map: the hull side on, nose right, belly down ----
void Screen::ThermalMap(Canvas& g, const View& v, const double* sk) {
    Frame(g, 15, 62, 785, 338, L"ОБШИВКА · ТЕРМОКАРТА");
    const double k = 3.3, ox = 110, oy = 225;
    auto P = [&](double s, double z) { return Pt{ox + s * k, oy - z * k}; };
    const Path& p = v.path;
    // the free stream at the angle of attack (the ship drawn level, the flow from ahead and below), its dashes running with it;
    // the lines kept inside the frame (at a high angle the mockup's lowest ones started below it)
    if (entry_ || Climbing(v) || (!v.onGround && v.q > 100.0)) {
        const double a = p.aoa, vx = -std::cos(a), vy = -std::sin(a), yLow = 396;
        for (int i = 0; i < 9; ++i) {
            double sx = 760 - 40 * std::sin(a) * i / 3, sy = 120 + i * 30 + 100 * std::sin(a), ph = i * 37 - v.t * 60;
            const double ex = sx + vx * 90, ey = sy + vy * 90;
            if (sy > yLow) {
                if (ey >= yLow) continue;
                const double u = (sy - yLow) / (sy - ey);
                ph += 90 * u; sx += (ex - sx) * u; sy = yLow;
            }
            DashPath(g, {{sx, sy}, {ex, ey}}, cBl, 1, 20, 12, ph, 0.25);
        }
        g.T(L"поток · атака " + Fmt(p.aoa * 180 / kPi, 0) + L"°", 770, 124, cBl, 11, 2);   // the mockup's y 92 lay under the colour scale
    }
    // the plasma sheath on the windward side, thickest at the nose, and the bow shock ahead of it
    const double fn = v.flux[dm::kZoneNose];
    if (!v.onGround && fn > 2e4) {
        const double al = (std::min)(0.5, fn / 2e6);
        const Pt p0 = P(0, -11), p1 = P(140, -11), p2 = P(178, 0), p3{p2.x + 10, p2.y + 8};
        std::vector<Pt> sh = {p0, p1, p2};
        for (const Pt& c : Quad(p3, {p1.x + 20, p1.y + 26}, {p0.x, p0.y + 14}, 16)) sh.push_back(c);
        FillPoly(g, sh, Rgb(255, 150, 70), al);
        g.Polyline(Quad({p2.x + 6, p2.y - 40}, {p2.x + 34, p2.y + 22}, {p0.x + 40, p0.y + 46}, 24), Rgb(255, 200, 140), 1.5, al);
    }
    // the hull: along it the stern, the belly and the nose (the mockup's gradient, in bands), the leeward top darkened (its
    // vertical gradient, in bands): each cell of the two the hull cut to it
    const unsigned cS = HeatCol(sk[dm::kZoneStern]), cB = HeatCol(sk[dm::kZoneBelly]), cN = HeatCol(sk[dm::kZoneNose]);
    auto along = [&](double u) { return u < 0.08 ? ColLerp(cS, cB, u / 0.08) : u <= 0.75 ? cB : ColLerp(cB, cN, (u - 0.75) / 0.25); };
    auto lee = [](double w) { return w < 0.6 ? 0.65 - 0.55 * w / 0.6 : 0.1 * (1 - (w - 0.6) / 0.4); };
    std::vector<Pt> hull;
    for (const Pt& q : {Pt{0, 10}, Pt{120, 10}, Pt{178, 0}, Pt{140, -11}, Pt{0, -11}}) hull.push_back(P(q.x, q.y));
    const double xa = P(0, 0).x, xb = P(178, 0).x, ya = P(0, 10).y, yb = P(0, -11).y;
    g.Shape(hull, Mix(cBg, cB, lee(0.5)));                                // under the cells: no seams between them
    static const double kU[14] = {0, 0.02, 0.04, 0.06, 0.08, 0.75, 0.78, 0.81, 0.84, 0.875, 0.91, 0.94, 0.97, 1.0};
    static const double kW[9] = {0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.8, 1.0};
    for (int i = 0; i < 13; ++i)
        for (int j = 0; j < 8; ++j) {
            const std::vector<Pt> c = ClipBox(hull, xa + (xb - xa) * kU[i], ya + (yb - ya) * kW[j], xa + (xb - xa) * kU[i + 1], ya + (yb - ya) * kW[j + 1]);
            if (c.size() >= 3) g.Shape(c, Mix(cBg, along((kU[i] + kU[i + 1]) / 2), lee((kW[j] + kW[j + 1]) / 2)));
        }
    Outline(g, hull, cEdge, 1.2);
    // the fin (the crests), the wing at its setting, the stern face
    g.Shape({P(4, 10), P(48, 10), P(37, 33), P(6, 33)}, HeatCol(sk[dm::kZoneFin]), 1.0, cEdge, 1.2);
    static const double kWz[3][2] = {{-8, -6}, {-9, -2}, {-4, -1}};
    const double* wz = kWz[WingIdx(v)];
    const std::vector<Pt> wing = {P(0, wz[0]), P(26, wz[0] + 1.5), P(16, wz[1] - 1), P(0, wz[1] - 1)};
    FillPoly(g, wing, HeatCol(sk[dm::kZoneCrestEdge]), 1.0);
    Outline(g, wing, cEdge, 1.2);
    const Pt sf = P(0, 10), sb = P(-1.5, -11);
    g.Fill(sb.x, sf.y, 5, sb.y - sf.y, cS);
    // the callouts: from the zone's place to its box
    struct Co { double s, z, lx, ly; };
    static const Co kCo[kZones] = {{178, 0, 690, 150}, {95, -11, 470, 352}, {18, -6, 120, 352}, {25, 33, 150, 100}, {0, 0, 32, 165},
                                   {70, -11, 300, 352}, {60, -4, 330, 125}};
    for (int i = 0; i < kZones; ++i) {
        const Co& c = kCo[i];
        const Pt q = P(c.s, c.z);
        const unsigned col = ZoneCol(sk[i], Lim(v, i));
        g.Line(q.x, q.y, c.lx + 40, c.ly - 5, cTx, 1, 0.35);
        g.Disc(q.x, q.y, 4, col);
        g.Fill(c.lx - 2, c.ly - 17, 92, 30, cBg, 0.85);
        g.T(kZoneShort[i], c.lx + 2, c.ly - 4, cDim, 10);
        g.T(Fmt(sk[i], 0) + L" К", c.lx + 2, c.ly + 10, col, 13, 0, 700);
    }
    // the colour scale (2 px steps drawn 3 wide: no gaps whichever edge the pad's fill leaves out)
    const double bx = 515, by = 82, bw = 260;
    for (int i = 0; i < 260; i += 2) g.Fill(bx + i, by, (std::min)(3.0, bw - i), 8, HeatCol(250 + 2750.0 * (i + 1) / bw));
    for (int t0 = 500; t0 <= 2500; t0 += 500) {
        const double x = bx + (t0 - 250) / 2750.0 * bw;
        g.Fill(x, by + 8, 1, 4, cDim);
        g.T(Fmt(t0, 0), x, by + 23, cDim, 10, 1);
    }
    g.T(L"К", bx + bw + 6, by + 8, cDim, 10);
}

// ---- the zones' table, the closest zone, the advice ----
void Screen::ZoneTable(Canvas& g, const View& v, const double* sk, double W) {
    Frame(g, 815, 62, W - 830, 338, L"ЗОНЫ ОБШИВКИ");
    static const wchar_t* const kHead[8] = {L"зона", L"T, К", L"предел", L"запас", L"q, кВт/м²", L"dT/dt", L"5 мин", L"прогноз пик"};
    static const double X[8] = {835, 1060, 1130, 1200, 1290, 1360, 1375, 1572};
    for (int i = 0; i < 8; ++i) g.T(kHead[i], X[i], 92, cDim, 11, i == 0 || i == 6 ? 0 : 2, 700);
    const bool fc = entry_ && fc_.valid;
    for (int i = 0; i < kZones; ++i) {
        const double y = 122 + i * 33, Tk = sk[i], lim = Lim(v, i), m = (lim - Tk) / lim, q = v.flux[i];
        const unsigned c = ZoneCol(Tk, lim);
        g.Disc(X[0] + 4, y - 5, 5, c);
        g.T(kZoneName[i], X[0] + 16, y, cTx, 13);
        g.T(Fmt(Tk, 0), X[1], y, c, 15, 2, 700);
        g.T(Int(lim), X[2], y, cDim, 13, 2);
        g.T(Fmt(m * 100, 0) + L" %", X[3], y, c, 13, 2, 700);
        g.T(q >= 1e3 ? Fmt(q / 1e3, 0) : q > 1 ? L"<1" : L"0", X[4], y, cTx, 13, 2);
        g.T((dT_[i] >= 0 ? L"+" : L"") + Fmt(dT_[i], 1), X[5], y, std::fabs(dT_[i]) < 0.05 ? cDim : dT_[i] > 0 ? cOr : cBl, 13, 2);
        {   // 5 minutes of the zone (a point every 2 s), its limit dashed
            const double x = X[6], sy = y - 18, sw = 95, sh = 24, top = (std::max)(lim * 1.1, 600.0), ly = sy + sh - sh * lim / top;
            g.Fill(x, sy, sw, sh, 0x0f1a18);
            g.Dashed(x, ly, x + sw, ly, cRd, 1, 3, 3, 0.5);
            if (hist_.size() >= 2) {
                std::vector<Pt> pts;
                for (size_t n = 0; n < hist_.size(); ++n) pts.push_back({x + sw * double(n) / 149.0, sy + sh - sh * (std::min)(1.0, hist_[n][i] / top)});
                g.Polyline(pts, cTx, 1.3);
            }
        }
        if (fc) {
            const double pT = fc_.peakT[i];
            const bool ahead = pT > Tk + 1;
            g.T(ahead ? Fmt(pT, 0) + L" · " + Fmt(fc_.peakAt[i], 0) + L" с" : L"пройден", X[7], y, ahead ? ZoneCol(pT, lim) : cDim, 13, 2, 700);
        } else g.T(L"—", X[7], y, cDim, 13, 2);
        Bar0(g, X[0] + 16, y + 5, 205, 3, Tk / lim, c);
    }
    int wz = 0; double wr = -1.0;
    for (int i = 0; i < kZones; ++i) { const double r = sk[i] / Lim(v, i); if (r > wr) { wr = r; wz = i; } }
    g.T(std::wstring(L"ближе всех к пределу: ") + kZoneName[wz] + L" — " + Fmt(wr * 100, 0) + L" % предела", 835, 368, ZoneCol(sk[wz], Lim(v, wz)), 13, 0, 700);
    const std::pair<std::wstring, unsigned> adv = Advice(v);
    g.T(g.Fit(adv.first, W - 860, 13, 700), 835, 390, adv.second, 13, 0, 700);
}

// ---- the entry corridor: altitude over speed ----
void Screen::CorridorPlot(Canvas& g, const View& v, double H) {
    Frame(g, 15, 415, 625, H - 430, L"КОРИДОР ВХОДА · ВЫСОТА / СКОРОСТЬ");
    const Path& p = v.path;
    const double x0 = 70, x1 = 620, y0 = 745, y1 = 440;
    auto X = [&](double s) { return x0 + (x1 - x0) * (std::min)(s, kVMax) / kVMax; };
    auto Y = [&](double h) { return y0 - (y0 - y1) * (std::min)(1.0, (std::max)(0.0, h) / kHMax); };
    for (int s = 0; s <= 12000; s += 2000) { g.Line(X(s), y0, X(s), y1, 0x16302a, 1); g.T(Fmt(s / 1000, 0), X(s), y0 + 16, cDim, 10, 1); }
    for (int h = 0; h <= 120000; h += 20000) { g.Line(x0, Y(h), x1, Y(h), 0x16302a, 1); g.T(Fmt(h / 1000, 0), x0 - 6, Y(h) + 4, cDim, 10, 2); }
    g.T(L"км/с", x1, y0 + 30, cDim, 10, 2); g.T(L"км", x0 - 6, y1 - 8, cDim, 10, 2);
    // the limits at the attitude now: below each line the zone's equilibrium temperature passes its limit
    const Coef zc = Probe(p.aoa, p.expo);
    struct Ln { int z; unsigned c; const wchar_t* name; };
    static const Ln kLn[3] = {{dm::kZoneNose, cRd, L"нос"}, {dm::kZoneBelly, cOr, L"днище"}, {dm::kZoneCrestEdge, cYe, L"кромки"}};
    for (const Ln& ln : kLn) {
        const double c = zc.c[ln.z], qLim = zc.es * (std::pow(Lim(v, ln.z), 4.0) - std::pow(kAmbient, 4.0));
        if (c <= 1e-15 || qLim <= 0.0) continue;                          // not in the flow (the wings folded)
        std::vector<Pt> pts;
        for (int s = 2500; s <= 12000; s += 100) {
            const double vv = s, r = qLim / (c * vv * vv * vv), hh = p.air.Alt(r * r);
            if (hh < 0 || hh > kHMax) continue;
            pts.push_back({X(vv), Y(hh)});
        }
        g.Polyline(pts, ln.c, 1.6);
        if (!pts.empty()) g.T(ln.name, pts.back().x - 4, pts.back().y - 6, ln.c, 11, 2, 700);
    }
    // the load limit: above the line the air is too thin to load the ship past it at this attitude
    const double F = v.gLimit * (std::max)(1.0, p.mass) * kG0;
    auto gAlt = [&](double s) {
        double SL = 0.0, SD = 0.0;
        AeroAreas(p.aoa, s / kSound, p.crestAvail, p.gearArea, &SL, &SD);
        const double S = std::hypot(SL, SD);
        return S > 0.0 ? p.air.Alt(2 * F / S / (s * s)) : -1.0;
    };
    std::vector<Pt> gl;
    for (int s = 1500; s <= 12000; s += 100) { const double hh = gAlt(s); if (hh < 0 || hh > kHMax) continue; gl.push_back({X(s), Y(hh)}); }
    DashPath(g, gl, cBl, 1.6, 6, 4, 0);
    g.T(GTxt(v.gLimit) + L" g", X(3000), Y(gAlt(3000)) + 16, cBl, 11, 1, 700);
    // the forecast and the flown track
    if (entry_ && fc_.valid) {
        std::vector<Pt> f = {{X(p.v), Y(p.h)}};
        for (const FcPt& q : fc_.track) f.push_back({X(q.x), Y(q.y)});
        DashPath(g, f, cYe, 1.5, 4, 4, 0, 0.7);
    }
    std::vector<Pt> tr;
    for (const Pt& q : track_) tr.push_back({X(q.x), Y(q.y)});
    g.Polyline(tr, cTx, 2.2);
    g.Disc(X(p.v), Y(p.h), 5, cWh);
    g.T(L"ниже линии зона перегревается в равновесии · — — прогноз", 80, 458, cDim, 11);
}

// ---- the flight, the forecast to the end of the entry, the events ----
void Screen::FlightBox(Canvas& g, const View& v, double H) {
    Frame(g, 655, 415, 345, H - 430, L"ПОЛЁТ · ПРОГНОЗ");
    const Path& p = v.path;
    const std::wstring rows[10][2] = {
        {L"высота", Fmt(p.h / 1e3, 1) + L" км"}, {L"скорость", Fmt(p.v, 0) + L" м/с"}, {L"траект.", Fmt(p.gamma * 180 / kPi, 1) + L"°"},
        {L"Мах", v.mach > 0 ? Fmt(v.mach, 1) : L"—"}, {L"напор", Fmt(v.q / 1e3, 1) + L" кПа"}, {L"перегр.", Fmt(v.gLoad, 2) + L" g"},
        {L"атака", Fmt(p.aoa * 180 / kPi, 0) + L"°"}, {L"крен", Fmt(p.bank * 180 / kPi, 0) + L"°"},
        {L"поток нос", Fmt(v.flux[dm::kZoneNose] / 1e3, 0) + L" кВт/м²"}, {L"нагрузка", Fmt(heat_ / 1e6, 0) + L" МДж/м²"}};
    for (int i = 0; i < 10; ++i) {
        const double x = 670 + (i % 2) * 165, y = 444 + (i / 2) * 24;
        g.T(rows[i][0], x, y, cDim, 12);
        g.T(rows[i][1], x + 155, y, cTx, 13, 2, 700);
    }
    g.T(L"ПРОГНОЗ ДО КОНЦА ВХОДА", 670, 584, cDim, 11, 0, 700);
    if (entry_ && fc_.valid) {
        const Forecast& f = fc_;
        struct R { const wchar_t* n; std::wstring val; unsigned c; };
        const R r[4] = {
            {L"пик потока нос", Fmt(f.qMax / 1e3, 0) + L" кВт/м² · " + Fmt(f.qMaxAt, 0) + L" с", cTx},
            {L"пик перегрузки", Fmt(f.nMax, 1) + L" g · " + Fmt(f.nMaxAt, 0) + L" с", f.nMax > v.gLimit ? cYe : cTx},
            {L"конец входа", f.skip ? std::wstring(L"ОТСКОК") : Fmt(f.endH / 1e3, 0) + L" км · " + Fmt(f.endV, 0) + L" м/с", f.skip ? cRd : cTx},
            {L"через", Fmt(f.endT, 0) + L" с", cTx}};
        for (int i = 0; i < 4; ++i) { g.T(r[i].n, 670, 606 + i * 22, cDim, 12); g.T(r[i].val, 985, 606 + i * 22, r[i].c, 13, 2, 700); }
    } else g.T(entry_ ? L"—" : Climbing(v) ? L"вход не идёт — подъём" : L"вход не идёт", 670, 606, cDim, 12);
    g.T(L"СОБЫТИЯ", 670, 704, cDim, 11, 0, 700);
    for (size_t i = 0; i < log_.lines.size() && i < 3; ++i)
        g.T(g.Fit(Clock(log_.lines[i].t) + L"  " + log_.lines[i].txt, 315, 12), 670, 726 + 20.0 * double(i), Journal::Col(log_.lines[i].lvl), 12);
}

// ---- the engines' temperatures ----
void Screen::EngineBox(Canvas& g, const View& v, const double* sk, double W, double H) {
    Frame(g, 1015, 415, W - 1030, H - 430, L"ДВИГАТЕЛИ · ТЕМПЕРАТУРЫ");
    const EngT e = EngineTemps(v, sk);
    struct Row { const wchar_t* n; double T, lim; std::wstring note; };
    const Row rows[6] = {
        {L"корма (конструкция)", v.sternT, v.tSafe, L"безопасно до " + Int(v.tSafe) + L", прогар " + Int(v.tLost)},
        {L"чаша маршевая, стенка", e.cup, 2000, e.cupHot ? L"в работе" : L"холодная"},
        {L"рубашка, выход", e.jacket, 1100, e.flow ? (v.reactMass == 0 ? L"аргон 87 К на входе" : L"железо 300 К на входе") : L"нет протока"},
        {L"обмотка поля", v.coilT, 26, L"REBCO, 20 К номинал"},
        {L"гондолы 1–4", e.pods, 1300, v.pods == 2 ? L"в работе" : L"выключены"},
        {L"радиаторы: гребни, перо", e.rad, 1500, L"излучают"}};
    for (int i = 0; i < 6; ++i) {
        const Row& r = rows[i];
        const double y = 448 + i * 40;
        const bool ok = r.T > 0;
        const unsigned c = ok ? ZoneCol(r.T, r.lim) : cDim;
        g.T(r.n, 1032, y, cTx, 13);
        g.T(r.note, 1032, y + 15, cDim, 10);
        g.T(ok ? Fmt(r.T, r.lim < 100 ? 1 : 0) + L" К" : L"—", 1460, y, c, 15, 2, 700);
        g.T(L"/ " + Int(r.lim), 1570, y, cDim, 12, 2);
        if (ok) Bar0(g, 1250, y + 6, 210, 5, r.T / r.lim, c);
    }
    // the anamezon chambers and the nose retro cups: cells
    g.T(L"камеры анамезона (корма)", 1032, 700, cTx, 13);
    g.T(L"ретро-чаши носа", 1032, 752, cTx, 13);
    for (int i = 0; i < 4; ++i) Cell(g, 1250 + i * 82, 682, L"К" + std::to_wstring(i + 1), e.ana);
    for (int i = 0; i < 2; ++i) Cell(g, 1250 + i * 82, 734, L"Н" + std::to_wstring(i + 1), e.retro[i]);
    g.T(L"предел 2000 К · холодные", 1580, 778, cDim, 10, 2);
}

}  // namespace tantra::thermalscreen
