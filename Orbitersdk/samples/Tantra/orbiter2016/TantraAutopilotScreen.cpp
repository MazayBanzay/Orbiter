// TantraAutopilotScreen: see TantraAutopilotScreen.h. PageAscent() follows draw() of Tantra_Design/tantra_autopilot_screen.html
// block by block (drawTrajectory, drawCourse, drawTarget, drawNow, drawCommands, drawJournal, drawTrends), PageLanding() its
// drawLand(); the coordinates and the colours are the mockup's. The canvas' clip, dashes and rotations are done here by hand
// (the sketchpad has none): the paths are thinned to ~1.5 px, dashed along their length, cut to the plot's box.
#include "TantraAutopilotScreen.h"

#include <algorithm>
#include <cmath>

namespace tantra::apscreen {

using namespace tantra::scr;
using namespace tantra::scr::ui;
namespace gd = tantra::guidance;

namespace {

// the mockup's own shades (besides the palette)
constexpr unsigned cGrid = 0x13261f, cKeyOff = 0x0d1513, cKeyOffEdge = 0x22302c, cKeyOffTx = 0x34504a, cKeyBg = 0x121d1b, cKeyEdge = 0x5a4325,
                   cKeyOn = 0x3b2a14, cKeyOnTx = 0xffd29a, cArrowOff = 0x2b403b, cDial = 0x0d1a17, cTick = 0x2a4a43, cTarget = 0x7a5220,
                   cTargetTx = 0xb07a35, cEvTx = 0xc98a40, cBarG = 0x3fa58a, cTvc = 0xb8691f, cDone = 0x2c6a52, cWait = 0x3d6b64,
                   cCur = 0x12291f, cValBg = 0x0f1c19, cBarBg = 0x14211e, cNow = 0x3a6a60, cPlanV = 0x3d6a8a, cField = 0x5fa8d8;

double Clamp(double x, double a, double b) { return (std::max)(a, (std::min)(b, x)); }
double NiceCeil(double x) {
    const double p = std::pow(10.0, std::floor(std::log10(x)));
    for (double m : {1.0, 1.2, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0}) if (m * p >= x - 1e-9) return m * p;
    return 10 * p;
}
std::wstring Lat(double deg) { return Fmt(std::fabs(deg), 1) + (deg >= 0 ? L"° с.ш." : L"° ю.ш."); }
std::wstring Warp(double w) { return L"время ×" + Fmt(w, w < 1 ? 1 : 0); }
std::wstring fW(double w) { return w >= 1e9 ? Fmt(w / 1e9, 2) + L" ГВт" : Fmt(w / 1e6, 0) + L" МВт"; }
unsigned LvlCol(int lvl) { return lvl == gd::kBad ? cRd : lvl == gd::kWarn ? cYe : cGr; }

// the mockup's btn(): a key with its label shrunk to fit; a disabled key is dark and takes no touch
void Key(Canvas& g, std::vector<Hit>& hits, const std::wstring& s, double x, double y, double w, double h, bool on, int cmd, unsigned col, bool en) {
    int sz = 14;
    while (sz > 9 && g.Width(s, sz, 600) > w - 10) --sz;
    if (!en) {
        g.Fill(x, y, w, h, cKeyOff); g.Stroke(x, y, w, h, cKeyOffEdge, 1);
        g.T(s, x + w / 2, y + h / 2 + 5, cKeyOffTx, sz, 1, 600);
        return;
    }
    g.Fill(x, y, w, h, on ? cKeyOn : cKeyBg); g.Stroke(x, y, w, h, on ? col : cKeyEdge, on ? 2 : 1);
    g.T(s, x + w / 2, y + h / 2 + 5, on ? cKeyOnTx : col, sz, 1, 600);
    hits.push_back({x, y, w, h, cmd});
}
// a touch arrow: dir -1 / +1, double = the coarse step
void ArrowKey(Canvas& g, std::vector<Hit>& hits, double x, double y, double w, double h, int dir, bool dbl, int cmd, bool en) {
    g.Fill(x, y, w, h, en ? cKeyBg : cKeyOff); g.Stroke(x, y, w, h, en ? cKeyEdge : cKeyOffEdge, 1);
    const unsigned c = en ? cOr : cArrowOff;
    const int n = dbl ? 2 : 1;
    const double cy = y + h / 2, tw = 10, th = 8, span = n * tw + (n - 1) * 3, x0 = x + w / 2 - span / 2;
    for (int i = 0; i < n; ++i) {
        const double xa = x0 + i * (tw + 3);
        if (dir < 0) g.Shape({{xa, cy}, {xa + tw, cy - th}, {xa + tw, cy + th}}, c);
        else g.Shape({{xa + tw, cy}, {xa, cy - th}, {xa, cy + th}}, c);
    }
    if (en) hits.push_back({x, y, w, h, cmd});
}
// a bar centred on zero (f in -1..1)
void CBar(Canvas& g, double x, double y, double w, double h, double f, unsigned col) {
    g.Fill(x, y, w, h, cBarBg);
    const double c = x + w / 2, ff = Clamp(f, -1, 1);
    g.Fill((std::min)(c, c + ff * w / 2), y, std::fabs(ff) * w / 2, h, col);
    g.Stroke(x, y, w, h, cFr, 1);
    g.Line(c, y - 2, c, y + h + 2, cDim, 1);
}
void Diamond(Canvas& g, double x, double y) { g.Polyline({{x, y - 5}, {x + 5, y}, {x, y + 5}, {x - 5, y}, {x, y - 5}}, cOr, 1.5); }
void Legend(Canvas& g) {
    g.Dashed(30, 84, 58, 84, cDim, 1.6, 7, 5); g.T(L"план", 64, 88, cDim, 12);
    g.Line(104, 84, 132, 84, cWh, 2); g.T(L"факт", 138, 88, cWh, 12);
}
// the checklist's marks (the font has no ✓ ✗ ○): drawn at (x, baseline y) as the 12 px glyphs would stand
void Mark(Canvas& g, int st, double x, double y, unsigned col) {
    if (st == 0) { g.Line(x + 1, y - 4, x + 4, y - 1, col, 2); g.Line(x + 4, y - 1, x + 10, y - 9, col, 2); }
    else if (st == 1) { g.Line(x + 1, y - 9, x + 9, y - 1, col, 2); g.Line(x + 9, y - 9, x + 1, y - 1, col, 2); }
    else if (st == 2) g.T(L"…", x, y, col, 12, 0, 700);
    else g.Circle(x + 5, y - 4.5, 4, col, 1.2);
}

struct Box { double x0, y0, x1, y1; };
// Liang-Barsky: the part of a segment inside the box
bool ClipSeg(double& ax, double& ay, double& bx, double& by, const Box& r) {
    double t0 = 0, t1 = 1;
    const double dx = bx - ax, dy = by - ay;
    const double p[4] = {-dx, dx, -dy, dy}, q[4] = {ax - r.x0, r.x1 - ax, ay - r.y0, r.y1 - ay};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) { if (q[i] < 0) return false; continue; }
        const double t = q[i] / p[i];
        if (p[i] < 0) { if (t > t1) return false; if (t > t0) t0 = t; }
        else { if (t < t0) return false; if (t < t1) t1 = t; }
    }
    const double nax = ax + t0 * dx, nay = ay + t0 * dy, nbx = ax + t1 * dx, nby = ay + t1 * dy;
    ax = nax; ay = nay; bx = nbx; by = nby;
    return true;
}
// a path: thinned to ~1.5 px, solid (on <= 0) or dashed along its whole length (on, off), cut to the box
void Path(Canvas& g, const std::vector<Pt>& src, unsigned col, double lw, double on, double off, const Box* clip) {
    std::vector<Pt> p;
    for (const Pt& q : src) {
        if (!std::isfinite(q.x) || !std::isfinite(q.y)) continue;
        if (p.empty() || std::hypot(q.x - p.back().x, q.y - p.back().y) >= 1.5) p.push_back(q);
    }
    if (!src.empty() && !p.empty() && std::isfinite(src.back().x) && std::isfinite(src.back().y) && (src.back().x != p.back().x || src.back().y != p.back().y)) p.push_back(src.back());
    if (p.size() < 2) return;
    if (on <= 0) {
        std::vector<Pt> run;
        auto flush = [&]() { if (run.size() >= 2) g.Polyline(run, col, lw); run.clear(); };
        for (size_t i = 1; i < p.size(); ++i) {
            double ax = p[i - 1].x, ay = p[i - 1].y, bx = p[i].x, by = p[i].y;
            if (clip && !ClipSeg(ax, ay, bx, by, *clip)) { flush(); continue; }
            if (run.empty() || run.back().x != ax || run.back().y != ay) { flush(); run.push_back({ax, ay}); }
            run.push_back({bx, by});
            if (bx != p[i].x || by != p[i].y) flush();
        }
        flush();
        return;
    }
    bool dash = true; double rem = on;
    for (size_t i = 1; i < p.size(); ++i) {
        const double L = std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
        if (L <= 0) continue;
        const double ux = (p[i].x - p[i - 1].x) / L, uy = (p[i].y - p[i - 1].y) / L;
        double s = 0;
        while (s < L) {
            const double step = (std::min)(rem, L - s);
            if (dash) {
                double ax = p[i - 1].x + ux * s, ay = p[i - 1].y + uy * s, bx = p[i - 1].x + ux * (s + step), by = p[i - 1].y + uy * (s + step);
                if (!clip || ClipSeg(ax, ay, bx, by, *clip)) g.Line(ax, ay, bx, by, col, lw);
            }
            s += step; rem -= step;
            if (rem <= 1e-9) { dash = !dash; rem = dash ? on : off; }
        }
    }
}
bool In(const Box& r, double x, double y) { return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1; }
// a point of a figure turned by a (the canvas' rotate: clockwise on the screen) and put at (ox, oy)
Pt Rot(double x, double y, double a, double ox, double oy) { const double c = std::cos(a), s = std::sin(a); return {ox + x * c - y * s, oy + x * s + y * c}; }

// the phases' strip of both pages
void Program(Canvas& g, const wchar_t* const* names, int phase, unsigned lc) {
    for (int i = 0; i < gd::kPhaseCount; ++i) {
        const double y = 340 + i * 24.0;
        const bool cur = i == phase, done = i < phase;
        if (cur) { g.Fill(1134, y - 15, 240, 21, cCur); g.Stroke(1134, y - 15, 240, 21, lc, 1); }
        if (cur) g.Disc(1146, y - 4, 6, lc);
        else if (done) g.Disc(1146, y - 4, 6, cDone);
        else g.Circle(1146, y - 4, 6, cFr, 1.5);
        g.T(g.Fit(Fmt(i, 0) + L"  " + names[i], 224, 12, cur ? 700 : 500), 1160, y, cur ? cWh : done ? cDim : cWait, 12, 0, cur ? 700 : 500);
    }
}

}  // namespace

// ======================= the title bar =======================
void Screen::Header(Canvas& g, const View& v, const std::wstring& txt, unsigned lc, bool blink, const std::wstring& right, double W, double H) {
    g.Fill(0, 0, W, H, cBg); g.Stroke(5, 5, W - 10, H - 10, cFr, 2);
    g.T(L"АВТОПИЛОТ", 22, 38, cTx, 22, 0, 700);
    Key(g, hits_, L"ВЗЛЁТ НА ОРБИТУ", 184, 14, 190, 36, v.page == 0, kCmdPageAsc, cOr, true);
    Key(g, hits_, L"ПОСАДКА НА КОРМУ", 382, 14, 190, 36, v.page == 1, kCmdPageLand, cOr, true);
    g.Disc(600, 32, 9, blink ? lc : cBarBg); g.Circle(600, 32, 9, lc, 1.5);
    g.T(txt, 616, 38, lc, 16, 0, 700);
    g.T(right, W - 22, 38, cDim, 13, 2);
}

void Screen::Draw(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v) {
    hits_.clear();
    if (!skp || !v.asc || !v.land) return;
    Canvas g(skp, font, ox, oy);
    if (v.page == 1) PageLanding(g, v, w, h);
    else PageAscent(g, v, w, h);
}

// ======================= ВЗЛЁТ НА ОРБИТУ =======================
void Screen::PageAscent(Canvas& g, const View& v, double W, double H) {
    const gd::Ascent& A = *v.asc; const gd::Flight& s = A.F();
    const int m = s.mode;
    const bool checking = A.Checking(), armed = A.Armed();
    const unsigned lc = m == gd::kAuto ? cGr : m == gd::kHold ? cYe : m == gd::kManual ? cBl : m == gd::kAbort || m == gd::kCrash ? cRd
                        : armed ? cGr : checking ? cYe : cDim;
    std::wstring st;
    if (m == gd::kIdle) st = checking ? L"ПРОВЕРКА ГОТОВНОСТИ" : armed ? L"ГОТОВ К ПУСКУ" : L"НА СТОЛЕ · НЕ ВЗВЕДЁН";
    else if (m == gd::kAuto) st = s.phase == 8 ? std::wstring(L"НА ОРБИТЕ") : std::wstring(L"РАБОТАЕТ: ") + gd::kPhases[s.phase];
    else if (m == gd::kHold) st = std::wstring(L"УДЕРЖАНИЕ · ") + gd::kPhases[s.phase];
    else if (m == gd::kManual) st = L"РУЧНОЕ УПРАВЛЕНИЕ";
    else if (m == gd::kAbort) st = L"ОТМЕНА · ДВИГАТЕЛИ НА ХОЛОСТОМ";
    else st = L"ПАДЕНИЕ";
    const bool blink = (m == gd::kIdle && (armed || checking)) || m == gd::kAbort || m == gd::kCrash ? std::fmod(v.sysT, 1.0) < 0.6 : true;
    Header(g, v, st, lc, blink, A.TLabel() + L" · " + (v.site.empty() ? L"" : v.site + L" ") + Lat(A.SiteLat()) + L" · " + Warp(v.warp), W, H);
    Trajectory(g, v); Course(g, v); Target(g, v); Now(g, v); Commands(g, v, lc);
    JournalBox(g, A.Log(), 775, 108, 12); Trends(g, v);
}

void Screen::Trajectory(Canvas& g, const View& v) {
    const gd::Ascent& A = *v.asc; const gd::Flight& s = A.F(); const gd::Kin& k = A.K(); const gd::AscentPlan& P = A.Plan(); const gd::Targets& tg = A.Tg();
    Frame(g, 15, 62, 645, 340, L"ТРАЕКТОРИЯ");
    const double X0 = 64, X1 = 640, Y0 = 102, Y1 = 368;
    const double hMax = NiceCeil((std::max)(tg.apo * 1.1, 50.0)) * 1e3, drMax = NiceCeil((std::max)(P.drEnd / 1e3 * 1.03, 100.0)) * 1e3;
    // range on a square-root scale: the climb (first ~2 000 km) stays readable next to the long coast
    auto X = [&](double dr) { return X0 + (X1 - X0) * std::sqrt((std::max)(0.0, dr) / drMax); };
    auto Y = [&](double h) { return Y1 - (Y1 - Y0) * h / hMax; };
    // legend and the plan's summary
    Legend(g);
    g.T(P.ok ? L"ПЛАН: орбита Т+" + gd::Clock(P.tOrbit) + L" · Δv " + Fmt(P.dv / 1e3, 2) + L" км/с · аргон " + Fmt(P.argon / 1e6, 2) + L" кт · железо " + Fmt(P.iron / 1e6, 2) + L" кт"
             : L"ПЛАН: орбита не достигается (" + P.why + L")", 645, 88, P.ok ? cTx : cRd, 12, 2, 600);
    // grid
    for (int i = 0; i <= 5; ++i) {
        const double y = Y(hMax * i / 5);
        g.Line(X0, y, X1, y, cGrid, 1);
        g.T(Fmt(hMax * i / 5 / 1e3, 0), X0 - 5, y + 4, cDim, 11, 2);
    }
    std::vector<double> ticks = {0};
    for (double t : {100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0, 50000.0})
        if (t * 1e3 < drMax && X(t * 1e3) - X(ticks.back() * 1e3) >= 34) ticks.push_back(t);
    if (ticks.size() > 1 && X1 - X(ticks.back() * 1e3) < 44) ticks.pop_back();
    ticks.push_back(drMax / 1e3);
    for (size_t i = 0; i < ticks.size(); ++i) {
        const double x = X(ticks[i] * 1e3);
        g.Line(x, Y0, x, Y1, cGrid, 1);
        g.T(Fmt(ticks[i], 0), x, Y1 + 15, cDim, 11, i == 0 ? 0 : i == ticks.size() - 1 ? 2 : 1);
    }
    g.Stroke(X0, Y0, X1 - X0, Y1 - Y0, cFr, 1);
    g.T(L"высота, км", X0 + 6, Y0 + 14, cDim, 11); g.T(L"дальность по грунту, км (шкала √)", X1 - 6, Y1 - 6, cDim, 11, 2);
    const Box clip{X0, Y0 - 4, X1, Y1};
    // the target orbit
    for (double hh : tg.peri == tg.apo ? std::vector<double>{double(tg.apo)} : std::vector<double>{double(tg.peri), double(tg.apo)}) g.Dashed(X0, Y(hh * 1e3), X1, Y(hh * 1e3), cTarget, 1.2, 3, 4);
    g.T(tg.peri == tg.apo ? L"цель " + Fmt(tg.apo, 0) + L" км" : L"цель " + Fmt(tg.peri, 0) + L" × " + Fmt(tg.apo, 0) + L" км", X0 + 90, Y(tg.apo * 1e3) - 5, cTargetTx, 11);
    // the plan, dashed
    std::vector<Pt> pts;
    for (const gd::Sample& q : P.track) pts.push_back({X(q.dr), Y(q.h)});
    Path(g, pts, cDim, 1.6, 7, 5, &clip);
    // the plan's events
    struct Lbl { const wchar_t* t; double dx, dy; int al; };
    auto lbl = [](int key, Lbl* l) {
        switch (key) {
            case gd::kEvPods: *l = {L"М 0,8 · гондолы убраны", 10, -3, 0}; return true;
            case gd::kEvSwitch: *l = {L"30 км · аргон → железо", 10, 4, 0}; return true;
            case gd::kEvMeco: *l = {L"отсечка", 0, 16, 1}; return true;
            case gd::kEvCirc: *l = {L"скругление", -8, -8, 2}; return true;
            case gd::kEvOrbit: *l = {L"орбита", -2, 17, 2}; return true;
            default: return false;
        }
    };
    for (const gd::Event& e : P.ev) {
        Lbl L;
        if (!lbl(e.key, &L)) continue;
        const double x = X(e.dr), y = Y(e.h);
        if (!In(clip, x, y)) continue;
        Diamond(g, x, y);
        g.T(L.t, x + L.dx, y + L.dy, cEvTx, 11, L.al);
    }
    // the actual track and the events passed
    pts.clear();
    for (const gd::Sample& q : s.track) pts.push_back({X(q.dr), Y(q.h)});
    if (s.lifted) pts.push_back({X(k.dr), Y(k.h)});
    Path(g, pts, cWh, 2, 0, 0, &clip);
    for (const gd::Event& e : s.ev) { Lbl L; if (lbl(e.key, &L) && In(clip, X(e.dr), Y(e.h))) g.Disc(X(e.dr), Y(e.h), 3.5, cGr); }
    // the ship: a marker along the air-relative flight path on the plot (the local slope of the scale)
    const double sx = (X1 - X0) / (2 * std::sqrt((std::max)(k.dr, 1e3) * drMax)), sy = (Y1 - Y0) / hMax;
    const bool over = k.dr > drMax;
    const double px = over ? X1 : X(k.dr), py = Y((std::max)(0.0, k.h));
    const double ang = s.lifted ? std::atan2(-k.vr * sy, (std::max)(1e-6, k.vgh) * sx) : -kPi / 2;
    const unsigned mc = s.mode == gd::kCrash ? cRd : cOr;
    const Pt a = Rot(11, 0, ang, px, py), b = Rot(-7, -6, ang, px, py), c = Rot(-3, 0, ang, px, py), d = Rot(-7, 6, ang, px, py);
    g.Shape({a, b, c}, mc); g.Shape({a, c, d}, mc);
    g.Polyline({a, b, c, d, a}, cBg, 1.5);
    if (over) g.T(L"по орбите → " + Fmt(k.dr / 1e3, 0) + L" км", X1 - 14, py + 18, cOr, 11, 2, 600);
    // the readiness check (on the stand), bottom right of the plot where the profile never goes
    if (s.mode == gd::kIdle) {
        const double bx = 330, by = 232, bw = 304, bh = 118;
        const bool armed = A.Armed(), checking = A.Checking();
        g.Fill(bx, by, bw, bh, cBg); g.Stroke(bx, by, bw, bh, armed ? cGr : cFr, 1.5);
        const std::vector<gd::Check> list = A.ChecksShown();
        const int done = A.ChecksDone(v.sysT);
        g.T(armed ? L"ГОТОВНОСТЬ ПОДТВЕРЖДЕНА · ПУСК" : checking ? L"ПРОВЕРКА ГОТОВНОСТИ…" : L"ПРОВЕРКА ГОТОВНОСТИ · нажмите ВЗВЕСТИ", bx + 10, by + 16, armed ? cGr : cDim, 11, 0, 700);
        for (size_t i = 0; i < list.size(); ++i) {
            const double y = by + 32 + i * 13.5;
            const int ii = int(i), stt = ii < done ? (list[i].ok ? 0 : 1) : ii == done ? 2 : 3;   // ok, bad, run, wait
            const unsigned col = stt == 0 ? cGr : stt == 1 ? cRd : stt == 2 ? cYe : cWait;
            Mark(g, stt, bx + 12, y, col);
            g.T(g.Fit(list[i].t, bw - 34, 11), bx + 28, y, stt == 3 ? cDim : col == cGr ? cTx : col, 11);
        }
    }
}

void Screen::Course(Canvas& g, const View& v) {
    const gd::Ascent& A = *v.asc; const gd::Flight& s = A.F(); const gd::Kin& k = A.K(); const gd::TargetDerived& td = A.Td(); const gd::Targets& tg = A.Tg();
    Frame(g, 15, 418, 645, 142, L"КУРС");
    const double cx = 90, cy = 490, R0 = 56;
    g.Disc(cx, cy, R0, cDial); g.Circle(cx, cy, R0, cFr, 1.5);
    for (int a = 0; a < 360; a += 10) {
        const double r1 = a % 90 == 0 ? R0 - 10 : a % 30 == 0 ? R0 - 7 : R0 - 4, c = std::sin(a * gd::kD2R), d = -std::cos(a * gd::kD2R);
        g.Line(cx + c * R0, cy + d * R0, cx + c * r1, cy + d * r1, a % 30 == 0 ? cDim : cTick, 1);
    }
    auto needle = [&](double az, unsigned col, double r, double wdt, bool dash) {
        const double x = cx + std::sin(az * gd::kD2R) * r, y = cy - std::cos(az * gd::kD2R) * r;
        if (dash) g.Dashed(cx, cy, x, y, col, wdt, 4, 3); else g.Line(cx, cy, x, y, col, wdt);
    };
    needle(td.azR, cOr, R0 - 2, 1.5, true);
    needle(k.azT, cOr, R0, 2.5, false);
    if (k.vh > 1) needle(k.azV, cBl, R0 - 8, 2, false);
    {   // the ship's heading: an arrow
        const double a = s.hdg * gd::kD2R;
        const Pt t = Rot(0, -R0 + 26, a, cx, cy), r = Rot(6, 10, a, cx, cy), m = Rot(0, 4, a, cx, cy), l = Rot(-6, 10, a, cx, cy);
        g.Shape({t, r, m}, cWh); g.Shape({t, m, l}, cWh);
    }
    g.Disc(cx, cy, 2.5, cBg);
    const wchar_t* lt[4] = {L"С", L"В", L"Ю", L"З"};
    for (int i = 0; i < 4; ++i) { const double a = i * 90 * gd::kD2R; g.T(lt[i], cx + std::sin(a) * (R0 - 17), cy - std::cos(a) * (R0 - 17) + 4, cDim, 11, 1, 700); }
    // readouts
    struct R { const wchar_t* n; std::wstring val; unsigned c; };
    const double omega = A.State().planet.omega;
    const R rows1[5] = {{L"азимут цели (инерц.)", Fmt(k.azT, 1) + L"°", cOr}, {L"азимут пуска (вращение)", Fmt(td.azR, 1) + L"°", cEvTx},
                        {L"курс корабля", Fmt(s.hdg, 1) + L"°", cWh}, {L"азимут скорости (инерц.)", k.vh > 1 ? Fmt(k.azV, 1) + L"°" : L"—", cBl},
                        {L"широта · вращение", Fmt(k.lat * gd::kR2D, 2) + L"° · +" + Fmt(omega * k.rr * std::cos(k.lat), 0) + L" м/с", cTx}};
    const double di = k.el.inc - tg.inc, cross = A.Cross();
    const R rows2[5] = {{L"наклонение", Fmt(k.el.inc, 2) + L"°", cWh}, {L"цель", Fmt(tg.inc, 2) + L"°", cOr},
                        {L"ошибка Δi", gd::FmtS(di, 2) + L"°", std::fabs(di) < 0.05 ? cGr : std::fabs(di) < 2 ? cYe : cTx},
                        {L"боковая скорость", gd::FmtS(k.vPerp, 0) + L" м/с", std::fabs(k.vPerp) < 5 ? cGr : cTx},
                        {L"откл. от трассы плана", std::isfinite(cross) ? gd::FmtS(cross / 1e3, 1) + L" км" : L"— (план пройден)", !std::isfinite(cross) ? cDim : std::fabs(cross) < 1e3 ? cGr : cYe}};
    for (int i = 0; i < 5; ++i) {
        const double y = 446 + i * 22.0;
        g.T(rows1[i].n, 166, y, cDim, 12); g.T(rows1[i].val, 410, y, rows1[i].c, 13, 2, 700);
        g.T(rows2[i].n, 428, y, cDim, 12); g.T(rows2[i].val, 645, y, rows2[i].c, 13, 2, 700);
    }
}

void Screen::Target(Canvas& g, const View& v) {
    const gd::Ascent& A = *v.asc; const gd::Targets& tg = A.Tg(); const gd::TargetDerived& td = A.Td();
    const bool en = A.Editable();
    Frame(g, 675, 62, 435, 300, L"ЦЕЛЬ");
    if (!en) g.T(A.F().mode == gd::kIdle ? L"ВЗВЕДЕНО — ЦЕЛЬ ЗАФИКСИРОВАНА" : L"ЦЕЛЬ ЗАФИКСИРОВАНА", 1100, 67, cDim, 11, 2, 700);
    // the site is where the ship stands: shown, not set
    struct Row { const wchar_t* l; std::wstring v; int fine, coarse; };   // the commands of the - keys (+ is the next one); -1 none
    const Row rows[6] = {{L"ПЛОЩАДКА", (v.site.empty() ? L"" : v.site + L" · ") + Lat(A.SiteLat()), -1, -1},
                         {L"НАКЛОНЕНИЕ ОРБИТЫ", Fmt(tg.inc, 2) + L"°", kCmdIncDn, kCmdIncDn5},
                         {L"АЗИМУТ · ИНЕРЦ. / ПУСКА", Fmt(td.azI, 1) + L"° / " + Fmt(td.azR, 1) + L"°", kCmdAzDn, kCmdAzDn5},
                         {L"ПЕРИЦЕНТР", Fmt(tg.peri, 0) + L" км", kCmdPeriDn, kCmdPeriDn50},
                         {L"АПОЦЕНТР", Fmt(tg.apo, 0) + L" км", kCmdApoDn, kCmdApoDn50},
                         {L"ПРЕДЕЛ ПЕРЕГРУЗКИ", Fmt(tg.gLim, 1) + L" g", kCmdGLimDn, -1}};
    for (int i = 0; i < 6; ++i) {
        const Row& r = rows[i];
        const double y = 76 + i * 40.0, h = 34;
        if (r.coarse >= 0) { ArrowKey(g, hits_, 688, y, 44, h, -1, r.fine >= 0, r.coarse, en); ArrowKey(g, hits_, 1052, y, 44, h, 1, r.fine >= 0, r.coarse + 1, en); }
        if (r.fine >= 0) { ArrowKey(g, hits_, 736, y, 44, h, -1, false, r.fine, en); ArrowKey(g, hits_, 1004, y, 44, h, 1, false, r.fine + 1, en); }
        g.Fill(786, y, 214, h, cValBg); g.Stroke(786, y, 214, h, cFr, 1);
        g.T(r.l, 794, y + 12, cDim, 10, 0, 700); g.T(g.Fit(r.v, 206, 16, 700), 893, y + 29, en ? cWh : cTx, 16, 1, 700);
    }
    // the target speed, computed
    const bool circ = tg.peri == tg.apo;
    g.T(L"СКОРОСТЬ НА ОРБИТЕ (расчёт)", 690, 334, cDim, 12, 0, 700);
    g.T(circ ? Fmt(td.va / 1e3, 3) + L" км/с" : Fmt(td.va / 1e3, 3) + L" – " + Fmt(td.vp / 1e3, 3) + L" км/с", 1095, 334, cWh, 16, 2, 700);
    g.T(std::wstring(circ ? L"круговая" : L"в апоцентре – в перицентре") + L" · период " + Fmt(td.period / 60, 1) + L" мин · вращение +" + Fmt(td.rotV, 0) + L" м/с", 690, 353, cDim, 11);
}

void Screen::Now(Canvas& g, const View& v) {
    const gd::Ascent& A = *v.asc; const gd::Flight& s = A.F(); const gd::Kin& k = A.K(); const gd::AscentState& st = A.State();
    Frame(g, 675, 378, 435, 182, L"СЕЙЧАС");
    const gd::Elements& el = k.el;
    const bool up = s.lifted && s.mode != gd::kCrash, inAir = k.rho > 1e-6;
    struct R { const wchar_t* n; std::wstring val; unsigned c; };
    const R col1[7] = {{L"высота", gd::Km(k.h), cWh}, {L"верт. скорость", Fmt(k.vr, 0) + L" м/с", cWh}, {L"гориз. скорость", Fmt(k.vh / 1e3, 3) + L" км/с", cWh},
                       {L"число Маха", inAir ? L"М " + Fmt(k.mach, 2) : L"— (вакуум)", k.mach >= 0.8 && s.pods ? cYe : cTx},
                       {L"скоростной напор", Fmt(k.q / 1e3, 1) + L" кПа", cTx}, {L"перегрузка (тяга)", Fmt(s.felt, 2) + L" g", s.felt >= s.tg.gLim - 0.01 ? cYe : cTx},
                       {L"Δv израсходовано", Fmt(s.dv / 1e3, 3) + L" км/с", cTx}};
    const R col2[7] = {{L"апоцентр", up ? (std::isfinite(el.apo) ? gd::Km(el.apo) : L"уход") : L"—", cWh},
                       {L"перицентр", up ? (el.peri > 0 ? gd::Km(el.peri) : L"ниже грунта") : L"—", el.peri > 0 ? cWh : cDim},
                       {L"наклонение", Fmt(el.inc, 2) + L"°", cWh}, {L"до апоцентра", up && el.ecc > 1e-3 ? gd::Clock(el.tApo) : L"—", cTx},
                       {L"аргон осталось", Fmt(st.argon / 1e6, 3) + L" кт", cTx}, {L"железо осталось", Fmt(st.iron / 1e6, 3) + L" кт", cTx},
                       {L"масса корабля", Fmt(st.mass / 1e6, 3) + L" кт", cTx}};
    for (int i = 0; i < 7; ++i) {
        const double y = 402 + i * 22.0;
        g.T(col1[i].n, 688, y, cDim, 12); g.T(col1[i].val, 884, y, col1[i].c, 14, 2, 700);
        g.T(col2[i].n, 904, y, cDim, 12); g.T(col2[i].val, 1097, y, col2[i].c, 14, 2, 700);
    }
}

void Screen::Commands(Canvas& g, const View& v, unsigned lc) {
    const gd::Ascent& A = *v.asc; const gd::Flight& s = A.F(); const gd::Kin& k = A.K(); const gd::AscentState& st = A.State(); const gd::AscentCmd& cm = A.Cmd();
    Frame(g, 1125, 62, 460, 498, L"КОМАНДЫ АВТОПИЛОТА");
    // ---- pitch: commanded vs actual, and the flight path angle ----
    const double cx = 1150, cy = 262, Rr = 110;
    auto P = [&](double a, double r) { return Pt{cx + r * std::cos(a * gd::kD2R), cy - r * std::sin(a * gd::kD2R)}; };
    g.T(L"ТАНГАЖ", 1140, 86, cDim, 12, 0, 700);
    g.T(L"команда " + Fmt(s.pitchCmd, 1) + L"°", 1140, 105, cOr, 14, 0, 700);
    g.T(L"факт " + Fmt(s.pitch, 1) + L"°", 1140, 123, cWh, 14, 0, 700);
    {
        std::vector<Pt> sec = {{cx, cy}};
        for (int a = 90; a >= -10; a -= 5) sec.push_back(P(a, Rr));
        g.Shape(sec, cDial);
        g.Arc(cx, cy, Rr, -kPi / 2, 10 * gd::kD2R, cFr, 2);
    }
    for (int a = -10; a <= 90; a += 10) {
        const Pt p1 = P(a, Rr), p2 = P(a, a % 30 == 0 ? Rr - 11 : Rr - 6);
        g.Line(p1.x, p1.y, p2.x, p2.y, a == 0 ? cDim : cTick, 1);
        if (a % 30 == 0) { const Pt pl = a == 90 ? Pt{cx - 13, cy - Rr + 8} : P(a, Rr + 12); g.T(Fmt(a, 0), pl.x, pl.y + 4, cDim, 10, 1); }
    }
    g.Line(cx, cy, cx + Rr, cy, cTick, 1);
    if (s.lifted && k.vh + std::fabs(k.vr) > 50) {           // the inertial flight path angle
        const Pt p1 = P(Clamp(k.gam, -10, 90), Rr - 2), p2 = P(Clamp(k.gam, -10, 90), Rr - 22);
        g.Line(p1.x, p1.y, p2.x, p2.y, cBl, 3);
    }
    { const double a = Clamp(s.pitchCmd, -10, 90); g.Shape({P(a, Rr + 1), P(a + 4, Rr + 11), P(a - 4, Rr + 11)}, cOr); }
    { const Pt p = P(Clamp(s.pitch, -10, 90), Rr - 12); g.Line(cx, cy, p.x, p.y, cWh, 3); }
    g.Disc(cx, cy, 4, cWh);
    g.T(s.lifted ? L"траектория " + Fmt(k.gam, 1) + L"°" : L"траектория —", cx + 26, cy + 26, cBl, 11);
    // ---- engines and steering ----
    const double bx = 1300, bw = 270;
    const bool run = s.mode != gd::kIdle && s.mode != gd::kCrash;
    auto row = [&](double y, const wchar_t* label, const std::wstring& val, unsigned col) { g.T(label, bx, y, cDim, 12); g.T(val, bx + bw, y, col, 13, 2, 700); };
    const double cmdThr = s.mode == gd::kIdle ? 0.0 : s.thrCmd * s.spool;
    const double thrM = st.Fmarch > 0 ? st.FmNow / st.Fmarch : 0.0, thrP = st.Fpods > 0 ? st.FpNow / st.Fpods : 0.0;   // what each gives
    row(88, L"МАРШЕВАЯ ЧАША · тяга", run ? Fmt(thrM * 100, 0) + L" % · " + gd::Force(st.FmNow) : L"0 %", cWh);
    Bar(g, bx, 95, bw, 9, run ? thrM : 0.0, cBarG); g.Fill(bx + bw * Clamp(cmdThr, 0, 1) - 1.5, 92, 3, 15, cOr);
    // the game's pods swing out of their bays in 12 s: until then "в отсеках" (the mockup's stand has them out already)
    row(122, L"ГОНДОЛЫ 4 × 3 ЧАШИ", !s.pods ? std::wstring(L"УБРАНЫ (М 0,8)") : st.Fpods <= 0 ? std::wstring(L"в отсеках")
        : run ? Fmt(thrP * 100, 0) + L" % · " + gd::Force(st.FpNow) : std::wstring(L"выпущены · 0 %"), s.pods && st.Fpods > 0 ? cWh : cDim);
    Bar(g, bx, 129, bw, 9, s.pods && run ? thrP : 0.0, cBarG);
    row(156, L"УВТ ТАНГАЖ (±10°)", gd::FmtS(s.tvcP, 1) + L"°", std::fabs(s.tvcP) > 8 ? cYe : cWh); CBar(g, bx, 163, bw, 9, s.tvcP / gd::kTvcMax, cTvc);
    row(190, L"УВТ РЫСКАНЬЕ (±10°)", gd::FmtS(s.tvcY, 1) + L"°", std::fabs(s.tvcY) > 8 ? cYe : cWh); CBar(g, bx, 197, bw, 9, s.tvcY / gd::kTvcMax, cTvc);
    row(224, L"КРЕН · КУРС: команда / факт", Fmt(s.hdgCmd, 1) + L"° / " + Fmt(s.hdg, 1) + L"°", cWh);
    row(250, L"РАБОЧАЯ МАССА", s.mass == gd::kArgon ? L"АРГОН · " + gd::Flow(st.mdot) : s.mass == gd::kIron ? L"ЖЕЛЕЗО · " + gd::Flow(st.mdot) : L"НЕТ", s.mass == gd::kIron ? cBl : cOr);
    const int lim = cm.lim;
    row(272, L"ОГРАНИЧИВАЕТ", lim == gd::kLimG ? L"перегрузка " + Fmt(s.tg.gLim, 1) + L" g" : lim == gd::kLimField ? L"поле " + Fmt(st.field, 1) + L" Тл"
        : lim == gd::kLimPower ? L"мощность" : lim == gd::kLimHeat ? L"тепло" : lim == gd::kLimNoMass ? L"нет массы" : L"—", lim == gd::kLimG ? cYe : cTx);
    const wchar_t* att = s.mode == gd::kIdle ? L"на столе" : s.mode == gd::kAuto ? L"по программе" : s.mode == gd::kHold ? L"удержание" : s.mode == gd::kManual ? L"пилот"
                         : s.mode == gd::kAbort ? L"как была" : L"—";
    row(292, L"ОРИЕНТАЦИЯ", att, cTx);
    g.Line(1135, 302, 1575, 302, cFr, 1);
    // ---- the phase strip ----
    g.T(L"ПРОГРАММА ПОЛЁТА", 1140, 320, cDim, 11, 0, 700);
    Program(g, gd::kPhases, s.phase, lc);
    // ---- buttons ----
    const double bX = 1388, bW = 182, bH = 34;
    const bool armed = A.Armed(), checking = A.Checking(), goArmed = A.GoArmed(v.sysT), abArmed = A.AbArmed(v.sysT);
    Key(g, hits_, armed ? L"ВЗВЕДЁН" : checking ? L"ПРОВЕРКА…" : L"ВЗВЕСТИ", bX, 310, bW, bH, armed || checking, kCmdArm, cOr, A.CanArm());
    Key(g, hits_, goArmed ? L"ПОДТВЕРДИТЬ" : L"ПУСК", bX, 351, bW, bH, goArmed, kCmdStart, cGr, A.CanStart());
    Key(g, hits_, s.mode == gd::kHold ? L"ПРОДОЛЖИТЬ" : L"УДЕРЖАНИЕ", bX, 392, bW, bH, s.mode == gd::kHold, kCmdHold, cYe, A.CanHold());
    Key(g, hits_, L"РУЧНОЕ", bX, 433, bW, bH, s.mode == gd::kManual, kCmdManual, cBl, A.CanManual());
    Key(g, hits_, abArmed ? L"ПОДТВЕРДИТЬ" : L"ОТМЕНА", bX, 474, bW, bH, abArmed || s.mode == gd::kAbort, kCmdAbort, cRd, A.CanAbort());
    Key(g, hits_, L"СБРОС", bX, 515, bW, bH, false, kCmdReset, cDim, A.CanReset());
}

void Screen::JournalBox(Canvas& g, const gd::Journal& j, double w, double txtX, int txtSize) {
    Frame(g, 15, 576, w, 212, L"ЖУРНАЛ");
    int i = 0;
    for (const gd::Line& l : j.lines) {
        if (i >= 9) break;
        const double y = 600 + i * 20.0;
        const unsigned col = LvlCol(l.lvl);
        g.T(l.tt, 28, y, cDim, 12, 0, 600);
        g.T(g.Fit(l.txt, 15 + w - 12 - txtX, txtSize, i == 0 ? 700 : 500), txtX, y, i == 0 ? col : col == cGr ? cTx : col, txtSize, 0, i == 0 ? 700 : 500);
        ++i;
    }
}

void Screen::Trends(Canvas& g, const View& v) {
    const gd::Ascent& A = *v.asc; const gd::Flight& s = A.F(); const gd::AscentPlan& P = A.Plan();
    Frame(g, 805, 576, 780, 212, L"ТРЕНДЫ ПОЛЁТА");
    const double x0 = 846, x1 = 1532, y0 = 606, y1 = 764;
    const double tMax = (std::max)({60.0, P.tEnd, s.t}), hMax = NiceCeil((std::max)(A.Tg().apo * 1.1, 50.0)), vMax = 8, gMax = 5;
    double step = 7200;
    for (double c : {30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1200.0, 1800.0, 3600.0}) if (tMax / c <= 7) { step = c; break; }
    auto X = [&](double t) { return x0 + (x1 - x0) * t / tMax; };
    for (int i = 0; i <= 4; ++i) {
        const double y = y1 - (y1 - y0) * i / 4;
        g.Line(x0, y, x1, y, cGrid, 1);
        g.T(Fmt(hMax * i / 4, 0), x0 - 5, y + 4, cWh, 10, 2); g.T(Fmt(vMax * i / 4, 0), x1 + 5, y + 4, cBl, 10, 0);
    }
    for (double t = 0; t <= tMax + 1e-6; t += step) {
        const double x = X(t);
        g.Line(x, y0, x, y1, cGrid, 1);
        g.T(Fmt(t / 60, step < 60 ? 1 : 0) + L" мин", x, y1 + 15, cDim, 10, 1);
    }
    g.Stroke(x0, y0, x1 - x0, y1 - y0, cFr, 1);
    g.T(L"высота, км", 850, 598, cWh, 11); g.T(L"скорость (инерц.), км/с", 930, 598, cBl, 11); g.T(L"перегрузка, g (0–5)", 1080, 598, cOr, 11);
    g.Dashed(1205, 594, 1230, 594, cDim, 1, 6, 4); g.T(L"высота по плану", 1236, 598, cDim, 11);
    auto line = [&](const std::vector<gd::Sample>& tr, int what, double max, unsigned col, double wdt, double on, double off) {
        std::vector<Pt> p;
        for (const gd::Sample& q : tr) {
            const double f = what == 0 ? q.h / 1e3 : what == 1 ? q.felt : q.v / 1e3;
            p.push_back({X(q.t), y1 - (y1 - y0) * Clamp(f / max, 0, 1)});
        }
        Path(g, p, col, wdt, on, off, nullptr);
    };
    line(P.track, 0, hMax, cDim, 1.4, 6, 4);
    line(s.track, 1, gMax, cOr, 1.2, 0, 0);
    line(s.track, 2, vMax, cBl, 1.6, 0, 0);
    line(s.track, 0, hMax, cWh, 1.8, 0, 0);
    if (s.mode != gd::kIdle) g.Line(X(s.t), y0, X(s.t), y1, cNow, 1);
}

// ======================= ПОСАДКА НА КОРМУ =======================
void Screen::PageLanding(Canvas& g, const View& v, double W, double H) {
    const gd::Landing& L = *v.land; const gd::LFlight& s = L.F();
    const int m = s.mode;
    const bool checking = L.Checking(), armed = L.Armed();
    std::wstring st; unsigned lc;
    if (m == gd::kIdle) { if (checking) { st = L"ПРОВЕРКА ГОТОВНОСТИ"; lc = cYe; } else if (armed) { st = L"ГОТОВ К ПОСАДКЕ"; lc = cGr; } else { st = L"СНИЖЕНИЕ · НЕ ВЗВЕДЁН"; lc = cDim; } }
    else if (m == gd::kAuto) { st = std::wstring(L"РАБОТАЕТ: ") + gd::kLPhases[s.phase]; lc = cGr; }
    else if (m == gd::kLanded) { const bool ok = s.td.valid && s.td.ok; st = ok ? L"ПОСАДКА ЗАВЕРШЕНА" : L"ПОСАДКА ВНЕ НОРМ"; lc = ok ? cGr : cYe; }
    else if (m == gd::kGoAround) { st = L"УХОД · ПОЛНАЯ ТЯГА ВВЕРХ"; lc = cRd; }
    else if (m == gd::kHold) { st = L"ОТМЕНА · ВИСЕНИЕ"; lc = cYe; }
    else if (m == gd::kManual) { st = L"РУЧНОЕ УПРАВЛЕНИЕ"; lc = cBl; }
    else { st = L"АВАРИЯ"; lc = cRd; }
    const bool blink = (m == gd::kIdle && (armed || checking)) || m == gd::kGoAround || m == gd::kCrash ? std::fmod(v.sysT, 1.0) < 0.6 : true;
    Header(g, v, st, lc, blink, L.TLabel() + L" · ветер " + Fmt(L.Wind(), 0) + L" м/с · поле " + (s.fieldAuto ? L"АВТО" : L"РУЧН") + L" · " + Warp(v.warp), W, H);
    LProfile(g, v); LSide(g, v); LTarget(g, v); LNow(g, v); LCommands(g, v, lc);
    JournalBox(g, L.Log(), 520, 100, 11); LHeat(g, v); LTrends(g, v);
}

void Screen::LProfile(Canvas& g, const View& v) {
    const gd::Landing& L = *v.land; const gd::LFlight& s = L.F(); const gd::LandPlan& P = L.Plan();
    Frame(g, 15, 62, 645, 340, L"ПРОФИЛЬ ПОСАДКИ");
    const double X0 = 64, X1 = 596, Y0 = 102, Y1 = 368, zMax = 6000, vMax = 160;
    const double tMax = (std::max)(P.tEnd + 5, s.t + 5);
    auto X = [&](double t) { return X0 + (X1 - X0) * t / tMax; };
    auto Yz = [&](double z) { return Y1 - (Y1 - Y0) * std::sqrt(Clamp(z, 0, zMax) / zMax); };
    auto Yv = [&](double vv) { return Y1 - (Y1 - Y0) * Clamp(vv, 0, vMax) / vMax; };
    Legend(g);
    g.T(L"высота кормы, м (шкала √)", 184, 88, cWh, 12); g.T(L"скорость снижения, м/с", 360, 88, cBl, 12);
    g.T(P.ok ? L"ПЛАН: касание Т+" + gd::Clock(P.tTouch) : std::wstring(L"ПЛАН: вне норм"), 645, 88, P.ok ? cTx : cRd, 12, 2, 600);
    for (double z : {0.0, 100.0, 300.0, 1000.0, 2000.0, 4000.0, 6000.0}) { const double y = Yz(z); g.Line(X0, y, X1, y, cGrid, 1); g.T(Fmt(z, 0), X0 - 5, y + 4, cDim, 11, 2); }
    for (double vv = 0; vv <= vMax; vv += 40) g.T(Fmt(vv, 0), X1 + 5, Yv(vv) + 4, cBl, 10, 0);
    double step = 300;
    for (double c : {5.0, 10.0, 20.0, 30.0, 60.0, 120.0}) if (tMax / c <= 9) { step = c; break; }
    for (double t = 0; t <= tMax; t += step) { const double x = X(t); g.Line(x, Y0, x, Y1, cGrid, 1); g.T(Fmt(t, 0) + L" с", x, Y1 + 15, cDim, 11, 1); }
    g.Stroke(X0, Y0, X1 - X0, Y1 - Y0, cFr, 1);
    // the hover height and the stern on the legs
    for (double z : {s.hHover, gd::kSternH}) g.Dashed(X0, Yz(z), X1, Yz(z), cTarget, 1, 3, 4);
    g.T(L"висение " + Fmt(s.hHover, 0) + L" м", X0 + 6, Yz(s.hHover) - 4, cTargetTx, 10); g.T(L"корма на ногах 22,5 м", X0 + 6, Yz(gd::kSternH) + 12, cTargetTx, 10);
    auto line = [&](const std::vector<gd::LSample>& tr, bool speed, unsigned col, double wdt, double on, double off) {
        std::vector<Pt> p;
        for (const gd::LSample& q : tr) p.push_back({X(q.t), speed ? Yv(-q.vz) : Yz(q.z)});
        Path(g, p, col, wdt, on, off, nullptr);
    };
    line(P.track, true, cPlanV, 1.4, 7, 5); line(P.track, false, cDim, 1.6, 7, 5);
    for (const gd::Event& e : P.ev) {
        const wchar_t* t = nullptr; double dx = 0, dy = 0; int al = 0;
        switch (e.key) {
            case gd::kEvBrake: t = L"торможение"; dx = 6; dy = -6; al = 0; break;
            case gd::kEvLegs: t = L"ноги"; dx = 6; dy = 12; al = 0; break;
            case gd::kEvHover: t = L"висение"; dx = 0; dy = -8; al = 1; break;
            case gd::kEvDesc: t = L"спуск"; dx = 0; dy = -8; al = 1; break;
            case gd::kEvTouch: t = L"касание"; dx = -4; dy = 15; al = 2; break;
            case gd::kEvCut: t = L"отсечка"; dx = 4; dy = -8; al = 0; break;
            default: break;
        }
        if (!t) continue;
        const double x = X(e.t), y = Yz(e.h);
        Diamond(g, x, y);
        g.T(t, x + dx, y + dy, cEvTx, 11, al);
    }
    if (!s.track.empty()) { line(s.track, true, cBl, 1.6, 0, 0); line(s.track, false, cWh, 2, 0, 0); }
    g.Disc(X(s.t), Yz(s.z), 5, s.mode == gd::kCrash ? cRd : cOr);
}

void Screen::LSide(Canvas& g, const View& v) {
    const gd::Landing& L = *v.land; const gd::LFlight& s = L.F(); const gd::Axis& a = s.ax[0];
    Frame(g, 15, 418, 645, 142, L"НАД ТОЧКОЙ · ВЕТЕР · НАКЛОН");
    // a side view in the plane of the wind: the pad, the ship's drift (1 px = 1 m, +-120 m) and its tilt shown x5
    const double gx = 180, gy = 540, px = gx + Clamp(a.x, -120, 120);
    g.Line(40, gy, 320, gy, 0x3f6a5f, 2);
    g.Fill(gx - 18, gy - 3, 36, 6, 0x2c5a50); g.T(L"стол", gx, gy + 14, cDim, 10, 1);
    const double oy = gy - 8 - (s.air ? (std::min)(30.0, (std::max)(0.0, s.z - gd::kSternH) * 0.4) : 0.0), rot = a.th * 5 * gd::kD2R;
    auto P = [&](double x, double y) { return Rot(x, y, rot, px, oy); };
    for (int sx : {-1, 1}) { const Pt p1 = P(sx * 6.0, -6), p2 = P(sx * (6 + 10 * s.legs), 2 + 4 * s.legs); g.Line(p1.x, p1.y, p2.x, p2.y, 0x88aaaa, 2); }
    g.Shape({P(-8, -4), P(8, -4), P(6, -60), P(0, -80), P(-6, -60)}, 0x33424a, 1.0, 0x5a7080, 1);
    const double fl = (s.Fm + s.Fp) / gd::MarchMax();
    if (fl > 0.01) {   // the jet: the canvas' gradient (warm white -> orange, fading) as bands over the ground
        const double a0 = (std::min)(0.9, 0.3 + fl * 0.4);
        auto hw = [&](double y) { return 5 + (y + 4) / 30 * (4 + 6 * fl); };
        for (int j = 0; j < 6; ++j) {
            const double ya = -4 + 5.0 * j, yb = ya + 5, t = (ya + 2.5 + 4) / 34;
            const unsigned c = Rgb(255, int(220 - 80 * t), int(180 - 120 * t));
            g.Shape({P(-hw(ya), ya), P(hw(ya), ya), P(hw(yb), yb), P(-hw(yb), yb)}, Mix(c, cBg, a0 * (1 - t)));
        }
    }
    if (std::fabs(a.x) > 120) g.T((a.x > 0 ? L"→ " : L"← ") + Fmt(std::fabs(a.x), 0) + L" м", a.x > 0 ? 318 : 42, gy - 50, cOr, 11, a.x > 0 ? 2 : 0, 700);
    // the wind
    const double wl = 6 + 4 * s.wNow;
    g.Line(44, 440, 44 + wl, 440, cBl, 2); g.Shape({{50 + wl, 440}, {42 + wl, 435}, {42 + wl, 445}}, cBl);
    g.T(L"ветер " + Fmt(s.wNow, 1) + L" м/с", 44, 458, cBl, 11); g.T(L"наклон ×5", 318, 440, cDim, 10, 2);
    struct R { const wchar_t* n; std::wstring val; unsigned c; };
    const R rows[5] = {{L"смещение от точки", gd::FmtS(a.x, 1) + L" м", std::fabs(a.x) < 1.5 ? cGr : cTx},
                       {L"гориз. скорость", gd::FmtS(a.vx, 2) + L" м/с", std::fabs(a.vx) <= gd::kNormVx ? cGr : cTx},
                       {L"боковая сила ветра", Fmt(a.Dx / 1e3, 0) + L" кН", cTx},
                       {L"наклон: команда / факт", gd::FmtS(a.thCmd, 2) + L"° / " + gd::FmtS(a.th, 2) + L"°", std::fabs(a.th) <= gd::kNormTilt ? cWh : cYe},
                       {L"УВТ марш / гондолы", gd::FmtS(a.dM, 1) + L"° / " + gd::FmtS(a.dP, 1) + L"°", cTx}};
    for (int i = 0; i < 5; ++i) { const double y = 446 + i * 22.0; g.T(rows[i].n, 360, y, cDim, 12); g.T(rows[i].val, 645, y, rows[i].c, 13, 2, 700); }
}

void Screen::LTarget(Canvas& g, const View& v) {
    const gd::Landing& L = *v.land; const gd::LFlight& s = L.F(); const gd::LandPlan& P = L.Plan();
    const bool en = L.Editable();
    Frame(g, 675, 62, 435, 300, L"ПОСАДКА · УСЛОВИЯ");
    // the field mode: the condition of the automatic landing
    g.T(L"ПОЛЕ МАРШЕВОЙ ЧАШИ", 690, 92, cDim, 12, 0, 700);
    Key(g, hits_, L"АВТО", 860, 74, 110, 32, s.fieldAuto, kCmdFieldAuto, cGr, true);
    Key(g, hits_, L"РУЧН", 980, 74, 110, 32, !s.fieldAuto, kCmdFieldManual, cRd, true);
    g.T(s.fieldAuto ? L"поле и темп капсул ведёт установка — автопилот задаёт только тягу" : L"поле держит оператор — посадка автоматом невозможна", 690, 124, s.fieldAuto ? cTx : cRd, 11);
    // the wind is measured (shown); the hover height is set
    struct Row { const wchar_t* l; std::wstring v; int fine; };
    const Row rows[2] = {{L"ВЕТЕР", Fmt(L.Wind(), 0) + L" м/с", -1}, {L"ВЫСОТА ВИСЕНИЯ (КОРМА)", Fmt(s.hHover, 0) + L" м", kCmdHoverDn}};
    for (int i = 0; i < 2; ++i) {
        const double y = 138 + i * 40.0, h = 34;
        if (rows[i].fine >= 0) { ArrowKey(g, hits_, 736, y, 44, h, -1, false, rows[i].fine, en); ArrowKey(g, hits_, 1004, y, 44, h, 1, false, rows[i].fine + 1, en); }
        g.Fill(786, y, 214, h, cValBg); g.Stroke(786, y, 214, h, cFr, 1);
        g.T(rows[i].l, 794, y + 12, cDim, 10, 0, 700); g.T(rows[i].v, 893, y + 29, en ? cWh : cTx, 16, 1, 700);
    }
    struct Info { const wchar_t* l; std::wstring v; };
    const Info info[5] = {
        {L"НАЧАЛО", Fmt(P.z0 / 1e3, 1) + L" км · снижение " + Fmt(-P.vz0, 0) + L" м/с · " + Fmt(-P.x0, 0) + L" м до точки, " + Fmt(P.vx0, 0) + L" м/с"},
        {L"НОРМЫ КАСАНИЯ", L"верт ≤ " + Fmt(gd::kNormVz, 0) + L" м/с · гориз ≤ " + Fmt(gd::kNormVx, 1) + L" м/с · наклон ≤ " + Fmt(gd::kNormTilt, 0) + L"°"},
        {L"ПРАВИЛА", L"ноги 20 с: начать ≥ 300 м, на замках ≥ 50 м · спуск 2 м/с · 5 g"},
        {L"ПЛАН (без ветра)", P.ok ? L"торможение с " + Fmt(P.brakeZ, 0) + L" м · касание Т+" + gd::Clock(P.tTouch) + L" · " + Fmt(P.td.vz, 2) + L" м/с" : std::wstring(L"вне норм")},
        {L"", P.ok ? L"аргон " + Fmt(P.used / 1e3, 0) + L" т · пик " + Fmt(P.maxG, 2) + L" g · корма до " + Fmt(P.maxTs, 0) + L" К" : std::wstring()}};
    for (int i = 0; i < 5; ++i) { const double y = 238 + i * 23.0; g.T(info[i].l, 690, y, cDim, 11, 0, 700); g.T(info[i].v, 1095, y, cTx, 12, 2); }
}

void Screen::LNow(Canvas& g, const View& v) {
    const gd::Landing& L = *v.land; const gd::LFlight& s = L.F();
    Frame(g, 675, 378, 435, 182, L"СЕЙЧАС");
    const double F = s.Fm + s.Fp, Wt = s.m * s.g, tt = L.TimeToTouch();
    const double Favail = (std::min)(gd::MarchMax() + gd::PodsMax(), gd::kLGLim * gd::kG0 * s.m);
    const double vx = std::hypot(s.ax[0].vx, s.ax[1].vx), th = std::hypot(s.ax[0].th, s.ax[1].th);
    struct R { const wchar_t* n; std::wstring val; unsigned c; };
    const R col1[7] = {{L"высота кормы", Fmt(s.z, s.z < 100 ? 1 : 0) + L" м", cWh}, {L"верт. скорость", gd::FmtS(s.vz, 2) + L" м/с", cWh},
                       {L"гориз. скорость", Fmt(vx, 2) + L" м/с", vx <= gd::kNormVx ? cGr : cTx}, {L"наклон", Fmt(th, 2) + L"°", th <= gd::kNormTilt ? cGr : cYe},
                       {L"перегрузка (тяга)", Fmt(s.felt, 2) + L" g", s.felt > 4.5 ? cYe : cTx}, {L"до касания", std::isfinite(tt) ? gd::Clock(tt) : L"—", cWh},
                       {L"ноги", s.legs >= 1 ? L"на замках" : s.legsOn ? Fmt(s.legs * 100, 0) + L" %" : L"убраны", s.legs >= 1 ? cGr : s.legsOn ? cYe : cTx}};
    const R col2[7] = {{L"тяга / вес", gd::Force(F) + L" / " + gd::Force(Wt), cWh}, {L"запас по тяге", F > 1e6 ? L"×" + Fmt(Favail / (std::max)(F, Wt), 2) : L"—", cTx},
                       {L"аргон израсходовано", Fmt(s.used / 1e3, 0) + L" т", cTx}, {L"расход аргона", gd::Flow(F / gd::kVArgon), cTx},
                       {L"аргон осталось", Fmt(s.argon / 1e6, 3) + L" кт", cTx}, {L"масса корабля", Fmt(s.m / 1e6, 3) + L" кт", cTx},
                       {L"пик перегрузки", Fmt(s.maxG, 2) + L" g", cTx}};
    for (int i = 0; i < 7; ++i) {
        const double y = 402 + i * 22.0;
        g.T(col1[i].n, 688, y, cDim, 12); g.T(col1[i].val, 884, y, col1[i].c, 14, 2, 700);
        g.T(col2[i].n, 904, y, cDim, 12); g.T(col2[i].val, 1097, y, col2[i].c, 14, 2, 700);
    }
}

void Screen::LCommands(Canvas& g, const View& v, unsigned lc) {
    const gd::Landing& L = *v.land; const gd::LFlight& s = L.F(); const gd::LandPlan& P = L.Plan();
    Frame(g, 1125, 62, 460, 498, L"КОМАНДЫ АВТОПИЛОТА");
    const double bx = 1140, bw = 430, Lm = gd::MarchMax(), Lp = gd::PodsMax(), F = s.Fm + s.Fp, Fmax = Lm + Lp, Wt = s.m * s.g;
    auto row = [&](double y, const wchar_t* label, const std::wstring& val, unsigned col) { g.T(label, bx, y, cDim, 12); g.T(val, bx + bw, y, col, 13, 2, 700); };
    auto tick = [&](double x, double y, double h, unsigned col) { g.Fill(x - 1.5, y - 3, 3, h + 6, col); };
    row(88, L"ТЯГА · команда / факт", gd::Force((std::min)(s.Fcmd, Fmax)) + L" / " + gd::Force(F), cWh);
    Bar(g, bx, 95, bw, 10, F / Fmax, cBarG); tick(bx + bw * Clamp(s.Fcmd / Fmax, 0, 1), 95, 10, cOr); tick(bx + bw * Wt / Fmax, 95, 10, cWh);
    g.T(L"вес", bx + bw * Wt / Fmax + 4, 117, cWh, 10);
    row(132, L"МАРШЕВАЯ ЧАША (20 %/с)", gd::Force(s.Fm) + L" · " + Fmt(s.Fm / Lm * 100, 0) + L" %", cWh);
    Bar(g, bx, 139, bw, 9, s.Fm / Lm, cBarG); tick(bx + bw * Clamp(s.FmC / Lm, 0, 1), 139, 9, cOr);
    row(166, L"ГОНДОЛЫ 4 × 3 (25 %/с)", gd::Force(s.Fp) + L" · " + Fmt(s.Fp / Lp * 100, 0) + L" %", cWh);
    Bar(g, bx, 173, bw, 9, s.Fp / Lp, cBarG); tick(bx + bw * Clamp(s.FpC / Lp, 0, 1), 173, 9, cOr);
    // the field bar: actual B, needed B, the 6 T floor and the 12.1 T limiter
    auto BX = [&](double B) { return bx + bw * B / 14; };
    row(200, s.fieldAuto ? L"ПОЛЕ B · факт / потребное (≤ 0,8 Тл/с)" : L"ПОЛЕ B · РУЧН: держит оператор", Fmt(s.B, 2) + L" / " + (s.fieldAuto ? Fmt(s.Breq, 2) : L"—") + L" Тл",
        s.fieldAuto ? cWh : cRd);
    Bar(g, bx, 207, bw, 12, s.B / 14, s.fieldAuto ? 0x2f6f9a : 0x7a3020);
    if (s.fieldAuto) tick(BX(s.Breq), 207, 12, cOr);
    g.Line(BX(gd::kBmin), 203, BX(gd::kBmin), 223, cDim, 1); g.T(L"6", BX(gd::kBmin), 233, cDim, 10, 1);
    g.Line(BX(gd::kBmax), 201, BX(gd::kBmax), 225, cRd, 2); g.T(L"12,1 огр.", BX(gd::kBmax), 233, cRd, 10, 1);
    const double Favail = (std::min)(Fmax, gd::kLGLim * gd::kG0 * s.m), capNow = gd::FieldThrust(s.B, gd::kAMarch) + Lp, tUp = (gd::kBmax - s.B) / gd::kBrate;
    row(250, L"ЗАПАС ПО ТЯГЕ", L"×" + Fmt((std::min)(capNow, Favail) / (std::max)(F, Wt), 2) + L" сейчас · " +
        (s.fieldAuto ? L"×" + Fmt(Favail / (std::max)(F, Wt), 2) + L" через " + Fmt(tUp, 1) + L" с" : std::wstring(L"поле держит оператор")), cWh);
    row(272, L"ТОРМОЖЕНИЕ: начало / подъём тяги", s.mode == gd::kIdle ? L"по плану " + Fmt(P.brakeZ, 0) + L" м"
        : s.mode == gd::kAuto && s.phase == 1 ? Fmt(s.hHover + s.dBrake, 0) + L" м / " + Fmt(s.tRamp, 1) + L" с" : L"план " + Fmt((s.aB + s.g) / gd::kG0, 2) + L" g", cWh);
    row(292, L"НОГИ (20 с)", s.legs >= 1 ? std::wstring(L"на замках") : s.legsOn ? Fmt(s.legs * 100, 0) + L" % · " + Fmt((1 - s.legs) * gd::kLegT, 0) + L" с" : std::wstring(L"убраны"),
        s.legs >= 1 ? cGr : s.legsOn ? cYe : cTx);
    g.Line(1135, 302, 1575, 302, cFr, 1);
    g.T(L"ПРОГРАММА ПОСАДКИ", 1140, 320, cDim, 11, 0, 700);
    const int ph = s.mode == gd::kAuto || s.mode == gd::kLanded || s.mode == gd::kIdle ? s.phase : -1;
    Program(g, gd::kLPhases, s.phase, lc);
    if (ph < 0 && s.mode != gd::kCrash) g.T(L"программа приостановлена", 1374, 552, cYe, 10, 2);
    const double bX = 1388, bW = 182, bH = 34;
    const bool armed = L.Armed(), checking = L.Checking(), goArmed = L.GoArmed(v.sysT), abArmed = L.AbArmed(v.sysT);
    Key(g, hits_, armed ? L"ВЗВЕДЁН" : checking ? L"ПРОВЕРКА…" : L"ВЗВЕСТИ", bX, 310, bW, bH, armed || checking, kCmdLArm, cOr, L.CanArm());
    Key(g, hits_, goArmed ? L"ПОДТВЕРДИТЬ" : L"ПУСК", bX, 351, bW, bH, goArmed, kCmdLStart, cGr, L.CanStart());
    Key(g, hits_, L"УХОД", bX, 392, bW, bH, s.mode == gd::kGoAround, kCmdLGoAround, cRd, L.CanGoAround());
    Key(g, hits_, abArmed ? L"ПОДТВЕРДИТЬ" : L"ОТМЕНА", bX, 433, bW, bH, abArmed || s.mode == gd::kHold, kCmdLAbort, cYe, L.CanAbort());
    Key(g, hits_, L"РУЧНОЕ", bX, 474, bW, bH, s.mode == gd::kManual, kCmdLManual, cBl, L.CanManual());
    Key(g, hits_, L"СБРОС", bX, 515, bW, bH, false, kCmdLReset, cDim, L.CanReset());
}

void Screen::LHeat(Canvas& g, const View& v) {
    const gd::Landing& L = *v.land; const gd::LFlight& s = L.F();
    Frame(g, 550, 576, 450, 212, L"КОРМА · ТЕПЛО");
    const double Ts = L.State().sternT, maxTs = (std::max)(s.maxTs, Ts);
    const double net = v.qGround + v.qRad - v.qRegen - v.qCrests, cStern = gd::kSternStore / 1400.0;
    struct R { const wchar_t* n; std::wstring val; unsigned c; };
    const R rows[5] = {{L"струя от грунта (PM.groundHeat)", L"+" + fW(v.qGround), v.qGround > 1e9 ? cYe : cTx}, {L"излучение реакции (χ 0,001 %)", L"+" + fW(v.qRad), cTx},
                       {L"рубашка: аргон × 1,5 МДж/кг", L"−" + fW(v.qRegen), cGr}, {L"гребни-радиаторы", L"−" + fW(v.qCrests), cGr},
                       {L"баланс", (net > 0 ? L"+" : L"−") + fW(std::fabs(net)), net > 0 ? cRd : cGr}};
    for (int i = 0; i < 5; ++i) { const double y = 600 + i * 19.0; g.T(rows[i].n, 564, y, cDim, 12); g.T(rows[i].val, 986, y, rows[i].c, 13, 2, 700); }
    // the stern thermometer: safe 800 K, burn-through 2300 K
    const double x0 = 564, w0 = 422, Tm = 2400;
    auto TX = [&](double t) { return x0 + w0 * Clamp((t - 290) / (Tm - 290), 0, 1); };
    struct Band { double a, b; unsigned c; };
    for (const Band& b : {Band{290, 800, 0x2c7a5a}, Band{800, 1100, 0x9a8a2a}, Band{1100, 1700, 0xb8691f}, Band{1700, 2300, 0xa8322a}, Band{2300, Tm, 0x5a1010}})
        g.Fill(TX(b.a), 708, TX(b.b) - TX(b.a), 10, b.c);
    g.Fill(TX(Ts) - 2, 704, 4, 18, cWh);
    g.T(L"800 К", TX(800), 734, cDim, 10, 1); g.T(L"прогар 2300 К", TX(2300), 734, cRd, 10, 1);
    g.T(L"корма " + Fmt(Ts, 0) + L" К · пик " + Fmt(maxTs, 0) + L" К", 564, 758, Ts > 800 ? cRd : cWh, 14, 0, 700);
    g.T(net > 0 ? L"до 800 К: " + Fmt((std::max)(0.0, (800 - Ts) * cStern / net), 0) + L" с" : std::wstring(L"до 800 К: не дойдёт"), 986, 758, net > 0 ? cYe : cGr, 13, 2, 700);
    const double f = 20 / (20 + (std::max)(0.0, s.z));
    g.T(L"высота кормы " + Fmt(s.z, 1) + L" м · доля отражения (20/(20+h))² = " + Fmt(f * f, 3), 564, 778, cDim, 11);
}

void Screen::LTrends(Canvas& g, const View& v) {
    const gd::Landing& L = *v.land; const gd::LFlight& s = L.F(); const gd::LandPlan& P = L.Plan();
    Frame(g, 1015, 576, 570, 212, L"ТЯГА · ПОЛЕ · ПЕРЕГРУЗКА");
    const double x0 = 1050, x1 = 1540, y0 = 606, y1 = 764, tMax = (std::max)(P.tEnd + 5, s.t + 5);
    auto X = [&](double t) { return x0 + (x1 - x0) * t / tMax; };
    for (int i = 0; i <= 4; ++i) {
        const double y = y1 - (y1 - y0) * i / 4;
        g.Line(x0, y, x1, y, cGrid, 1);
        g.T(Fmt(2.5 * i / 4, 2), x0 - 5, y + 4, cWh, 10, 2); g.T(Fmt(14.0 * i / 4, 1), x1 + 5, y + 4, cField, 10, 0);
    }
    g.Stroke(x0, y0, x1 - x0, y1 - y0, cFr, 1);
    g.T(L"тяга, ГН", 1050, 598, cWh, 11); g.T(L"поле B, Тл", 1120, 598, cField, 11); g.T(L"перегрузка, g (0–5)", 1200, 598, cOr, 11); g.T(L"— — тяга по плану", 1330, 598, cDim, 11);
    auto line = [&](const std::vector<gd::LSample>& tr, int what, double max, unsigned col, double wdt, double on, double off) {
        std::vector<Pt> p;
        for (const gd::LSample& q : tr) {
            const double f = what == 0 ? q.F / 1e9 : what == 1 ? q.B : q.g;
            p.push_back({X(q.t), y1 - (y1 - y0) * Clamp(f / max, 0, 1)});
        }
        Path(g, p, col, wdt, on, off, nullptr);
    };
    line(P.track, 0, 2.5, cDim, 1.4, 6, 4);
    if (!s.track.empty()) { line(s.track, 2, 5, cOr, 1.2, 0, 0); line(s.track, 1, 14, cField, 1.6, 0, 0); line(s.track, 0, 2.5, cWh, 1.8, 0, 0); }
    const double yB = y1 - (y1 - y0) * gd::kBNom / 14;
    g.Dashed(x0, yB, x1, yB, cRd, 1, 3, 4);
    g.T(L"12,1 Тл", x1 - 4, yB - 3, cRd, 10, 2);
    double step = 20;
    while (tMax / step > 12) step *= 2;                     // the mockup's 20 s, coarser for a long descent (the labels never pile up)
    for (double t = 0; t <= tMax; t += step) g.T(Fmt(t, 0) + L" с", X(t), y1 + 15, cDim, 10, 1);
}

// ======================= the keys =======================
void Press(int cmd, gd::Ascent& asc, gd::Landing& land, double now, int* page) {
    switch (cmd) {
        case kCmdPageAsc: if (page) *page = 0; break;
        case kCmdPageLand: if (page) *page = 1; break;
        case kCmdIncDn: asc.StepInc(-1, false); break;
        case kCmdIncUp: asc.StepInc(1, false); break;
        case kCmdIncDn5: asc.StepInc(-1, true); break;
        case kCmdIncUp5: asc.StepInc(1, true); break;
        case kCmdAzDn: asc.StepAz(-1, false); break;
        case kCmdAzUp: asc.StepAz(1, false); break;
        case kCmdAzDn5: asc.StepAz(-1, true); break;
        case kCmdAzUp5: asc.StepAz(1, true); break;
        case kCmdPeriDn: asc.StepPeri(-1, false); break;
        case kCmdPeriUp: asc.StepPeri(1, false); break;
        case kCmdPeriDn50: asc.StepPeri(-1, true); break;
        case kCmdPeriUp50: asc.StepPeri(1, true); break;
        case kCmdApoDn: asc.StepApo(-1, false); break;
        case kCmdApoUp: asc.StepApo(1, false); break;
        case kCmdApoDn50: asc.StepApo(-1, true); break;
        case kCmdApoUp50: asc.StepApo(1, true); break;
        case kCmdGLimDn: asc.StepGLim(-1); break;
        case kCmdGLimUp: asc.StepGLim(1); break;
        case kCmdArm: asc.Arm(now); break;
        case kCmdStart: asc.Start(now); break;
        case kCmdHold: asc.Hold(now); break;
        case kCmdManual: asc.Manual(now); break;
        case kCmdAbort: asc.Abort(now); break;
        case kCmdReset: asc.Reset(now); break;
        case kCmdFieldAuto: land.Field(true, now); break;
        case kCmdFieldManual: land.Field(false, now); break;
        case kCmdHoverDn: land.StepHover(-1); break;
        case kCmdHoverUp: land.StepHover(1); break;
        case kCmdLArm: land.Arm(now); break;
        case kCmdLStart: land.Start(now); break;
        case kCmdLGoAround: land.GoAround(now); break;
        case kCmdLAbort: land.Abort(now); break;
        case kCmdLManual: land.Manual(now); break;
        case kCmdLReset: land.Reset(now); break;
        default: break;
    }
}

}  // namespace tantra::apscreen
