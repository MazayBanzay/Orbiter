// TantraEngineScreen: see TantraEngineScreen.h. Draw() follows pageEng() of Tantra_Design/refine/front_v3.html and its blocks
// (planPictures / anaPictures, planTable / anaTable, planReckon / anaReckon, planControl / anaControl, journalBlock, trendBlock)
// with the mockup's coordinates; every number is the ship's (View, TantraDisplays::FillEngineView).
#include "TantraEngineScreen.h"

#include <algorithm>
#include <cmath>

namespace tantra::enginescreen {

using namespace tantra::front;

namespace {

constexpr double kC = 299792458.0, kG0 = 9.80665, kPi = 3.14159265358979323846, kRad = kPi / 180.0;
const double kPodS[4] = {44, 44, 84, 84}, kPodX[4] = {-11.5, 11.5, -11.5, 11.5};   // the pods: stations, sides (m)

struct NU { std::wstring n, u; };   // a number and its unit (the unit in its own smaller column)
NU fFu(double F) { F = std::fabs(F); return F >= 1e9 ? NU{Num(F / 1e9, 2), L"ГН"} : F >= 1e6 ? NU{Num(F / 1e6, 0), L"МН"} : F >= 1e3 ? NU{Num(F / 1e3, 0), L"кН"} : NU{L"0", L"Н"}; }
NU fWu(double P) {
    return P >= 1e18 ? NU{Num(P / 1e18, 1), L"ЭВт"} : P >= 1e15 ? NU{Num(P / 1e15, 1), L"ПВт"} : P >= 1e12 ? NU{Num(P / 1e12, 1), L"ТВт"}
         : P >= 1e9 ? NU{Num(P / 1e9, 1), L"ГВт"} : P >= 1e6 ? NU{Num(P / 1e6, 0), L"МВт"} : NU{L"0", L"Вт"};
}
NU fMu(double m) { return m >= 1000 ? NU{Num(m / 1000, 2), L"т/с"} : m >= 1 ? NU{Num(m, 1), L"кг/с"} : m > 0 ? NU{Num(m * 1000, 0), L"г/с"} : NU{L"0", L"кг/с"}; }
std::wstring J(const NU& a) { return a.n + L" " + a.u; }
std::wstring fT(double s) {
    return !std::isfinite(s) ? L"—" : s >= 86400 ? Num(s / 86400, 1) + L" сут" : s >= 3600 ? Num(s / 3600, 1) + L" ч" : s >= 60 ? Num(s / 60, 1) + L" мин" : Num(s, 0) + L" с";
}
std::wstring Pct(double f) { return Num(f * 100) + L" %"; }

// the blocks' places: pictures and output up top, the reckoning (and the journal) bottom left, the control right of the hub on
// full height, the trends under the hub
struct Layout { Rect hb; double ctlX, ctlW, aW, bX, bW, rcW; bool wide; };
Layout Lay(double W) {
    Layout L;
    L.hb = HubRect(W);
    L.ctlX = L.hb.x1 + kGap; L.ctlW = W - kM - L.hb.x1 - kGap;
    const double upW = L.ctlX - kGap - kM;
    L.aW = std::round(upW * 0.565 / 8) * 8; L.bX = kM + L.aW + kGap; L.bW = L.ctlX - kGap - L.bX;
    const double lowW = L.hb.x0 - kGap - kM;
    L.wide = lowW >= 900; L.rcW = L.wide ? lowW - kGap - 400 : lowW;
    return L;
}

// a cup seen from aft: the ring of its field, the fill of its output, a glow in it
void CupRing(Pad& g, double x, double y, double r, double f, unsigned col) {
    g.Ring(x, y, r, 0x2c4a44, 5);
    if (f > 0.001) {
        const double a = (std::min)(1.0, f);
        g.Arc(x, y, r, -kPi / 2, -kPi / 2 + 2 * kPi * a, col, 5);
        g.Disc(x, y, r - 4, MixC(kBg, 0xff9640, 0.35 * a));
        g.Disc(x, y, (r - 4) * 0.55, MixC(kBg, 0xfff0dc, 0.6 * a));
    }
}
// an anamezon chamber: its field ring (violet), the flash of its pulses (the feed)
void Chamber(Pad& g, double x, double y, double r, double field, double f, double t, const std::wstring& label) {
    g.Ring(x, y, r, 0x2a2440, 4);
    if (field > 0.001) g.Arc(x, y, r, -kPi / 2, -kPi / 2 + 2 * kPi * (std::min)(1.0, field), field >= 1 ? kVi : 0x7a68b0, 4);
    if (f > 0.001) {
        const double s = 0.55 + 0.45 * std::fabs(std::sin(t * 47 + x)), a = s * (std::min)(1.0, f + 0.2);
        g.Disc(x, y, r - 3, MixC(kBg, 0xbea0ff, 0.5 * a));
        g.Disc(x, y, (r - 3) * 0.5, MixC(kBg, 0xffffff, a));
    }
    g.T(label, x, y + Pad::CapH(15) / 2, kWh, 15, 1, true);
}
void SternHull(Pad& g, double cx, double cy, double k, double crest, double top) {
    g.Ellipse(cx, cy, 13.5 * k, 9 * k, 0x16241f, 0x3f6a5f, 2);
    g.Line(cx - 13.5 * k, cy - k, cx - crest * k, cy - 1.5 * k, 0x2e4d45, 3);
    g.Line(cx + 13.5 * k, cy - k, cx + crest * k, cy - 1.5 * k, 0x2e4d45, 3);
    g.Line(cx, cy - 9 * k, cx, (std::max)(top, cy - (std::min)(20.0, crest) * k), 0x2e4d45, 3);
}
// an arrow no longer than lmax (the thrust vectors: the real forces can be far over the picture's scale)
void ArrowMax(Pad& g, double x0, double y0, double dx, double dy, double lmax, unsigned c, double lw) {
    const double L = std::hypot(dx, dy);
    if (L > lmax && L > 0) { dx *= lmax / L; dy *= lmax / L; }
    g.Arrow(x0, y0, x0 + dx, y0 + dy, c, lw);
}

// a table turned on its side: rows the quantities, columns the sources; numbers right, units in their own smaller column
struct Cell { std::wstring n, u; unsigned c = kWh; bool on = true; };
Cell C(const NU& a, unsigned c = kWh) { return {a.n, a.u, c, true}; }
Cell C(const std::wstring& n, const std::wstring& u, unsigned c = kWh) { return {n, u, c, true}; }
const Cell kEmpty = {L"", L"", kWh, false};
struct Row { std::wstring l; Cell c[3]; };
void Table(Pad& g, double x, double y, double w, double h, const wchar_t* title, const wchar_t* const head[3], const std::vector<Row>& rows) {
    if (w < 240) return;
    g.Block(x, y, w, h, title);
    const double lw = (std::max)(120.0, std::round((w - 32) * 0.28)), cw = (w - 32 - lw) / 3, top = y + kBandH;
    auto colX = [&](int i) { return x + 16 + lw + i * cw; };
    for (int i = 0; i < 3; ++i) g.T(head[i], colX(i) + cw - 6, top + 26, kDim, Pad::FitSize(head[i], cw - 10, 17), 2, true);
    g.Line(x + 16, top + 36, x + w - 16, top + 36, kFr, 1);
    g.Line(colX(2) - 4, top + 8, colX(2) - 4, y + h - 8, kFr, 1);
    for (size_t j = 0; j < rows.size(); ++j) {
        const double yb = top + 36 + 26 + j * 32.0;
        g.T(rows[j].l, x + 16, yb, kDim, Pad::FitSize(rows[j].l, lw - 6, 17));
        for (int i = 0; i < 3; ++i) {
            const Cell& c = rows[j].c[i];
            if (!c.on) continue;
            const double ux = colX(i) + cw - 6, uw = Pad::TW(c.u, 15);
            g.T(c.u, ux, yb, kDim, 15, 2);
            g.T(c.n, ux - uw - 5, yb, c.c, Pad::FitSize(c.n, cw - uw - 14, 20), 2, true);
        }
    }
}
// the reckoning: the label left, the value right, in columns; the value shrinks (not below 18) before it would touch its label,
// then the label (not below 15)
struct Item { std::wstring l, v; unsigned c = kWh; };
void Grid(Pad& g, double x, double y, double w, double h, const wchar_t* title, const std::vector<Item>& items, int cols, double step) {
    if (w < 200) return;
    g.Block(x, y, w, h, title);
    const int rows = int((items.size() + cols - 1) / cols);
    const double cw = (w - 32 - (cols - 1) * 32) / cols;
    for (int c = 1; c < cols; ++c) g.Line(x + 16 + c * (cw + 32) - 16, y + kBandH + 10, x + 16 + c * (cw + 32) - 16, y + h - 10, kFr, 1);
    for (size_t i = 0; i < items.size(); ++i) {
        const int c = int(i) / rows, r = int(i) % rows;
        const double cx = x + 16 + c * (cw + 32), yb = y + kBandH + 8 + step / 2 + Pad::CapH(17) / 2 + r * step;
        // the unit: the last word when it holds no digit and no sign (its own smaller column)
        std::wstring num = items[i].v, unit;
        const size_t sp = num.find_last_of(L' ');
        if (sp != std::wstring::npos && sp + 1 < num.size()) {
            const std::wstring tail = num.substr(sp + 1);
            bool ok = true;
            for (wchar_t ch : tail) if ((ch >= L'0' && ch <= L'9') || ch == L'+' || ch == L'−') ok = false;
            if (ok) { unit = tail; num = num.substr(0, sp); while (!num.empty() && num.back() == L' ') num.pop_back(); }
        }
        const double uw = unit.empty() ? 0.0 : Pad::TW(unit, 15) + 5;
        double ls = 17, lw = Pad::TW(items[i].l, ls);
        int vs = Pad::FitSize(num, cw - lw - 16 - uw, 22, 18);
        if (lw + 16 + uw + Pad::TW(num, vs) > cw) { ls = Pad::FitSize(items[i].l, cw - 16 - uw - Pad::TW(num, vs), 17, 15); lw = Pad::TW(items[i].l, ls); }
        if (lw + 16 + uw + Pad::TW(num, vs) > cw) vs = Pad::FitSize(num, cw - lw - 16 - uw, vs, 15);
        const std::wstring lab = Pad::FitTxt(items[i].l, cw - 16 - uw - Pad::TW(num, vs), ls);
        g.T(lab, cx, yb, kDim, ls);
        if (!unit.empty()) g.T(unit, cx + cw, yb, kDim, 15, 2);
        g.T(num, cx + cw - uw, yb, items[i].c, vs, 2, true);
    }
}

}  // namespace

// ---- the journal and the trends ----
void Screen::Watch(const View& v) {
    if (!seen_) { seen_ = true; last_ = v; log_.Add(v.t, v.ana ? L"Пульт: анамезон" : L"Пульт: планетарные", 0); return; }
    const View& o = last_;
    static const wchar_t* const kSt[4] = {L"камеры выключены", L"поле камер", L"пучок поджига", L"камеры на режиме"};
    if (v.ana != o.ana) log_.Add(v.t, v.ana ? L"Главная тяга: анамезон" : L"Главная тяга: планетарные", 0);
    if (v.stage != o.stage) log_.Add(v.t, std::wstring(L"Анамезон: ") + kSt[(std::max)(0, (std::min)(3, v.stage))], v.stage == 3 ? 0 : 1);
    if (v.bypass != o.bypass) log_.Add(v.t, v.bypass ? L"БЛОКИРОВКИ СНЯТЫ КОМАНДИРОМ" : L"Блокировки восстановлены", v.bypass ? 2 : 0);
    if (v.plantRun != o.plantRun) log_.Add(v.t, v.plantRun ? L"Установка на режиме: маршевая готова" : L"Установка не на режиме", v.plantRun ? 0 : 1);
    if (v.mass != o.mass) log_.Add(v.t, std::wstring(L"Рабочая масса: ") + (v.mass == 0 ? L"аргон" : v.mass == 1 ? L"железо" : L"продукты"), 0);
    if (v.gLimOn != o.gLimOn) log_.Add(v.t, v.gLimOn ? L"Предел перегрузки включён" : L"Предел перегрузки выключен", v.gLimOn ? 0 : 1);
    if ((v.capHot < 0.999) != (o.capHot < 0.999)) log_.Add(v.t, v.capHot < 0.999 ? L"Блокировка: горячий старт в атмосфере" : L"Горячий старт: без ограничения", 1);
    if ((v.capSafe < 0.999) != (o.capSafe < 0.999)) log_.Add(v.t, v.capSafe < 0.999 ? L"Блокировка: люди в зоне струи" : L"Зона струи свободна", 1);
    if (v.activeTrap != o.activeTrap) log_.Add(v.t, L"Подача из ловушки " + Num(v.activeTrap + 1), 0);
    last_ = v;
}
void Screen::Sample(const View& v) {
    Watch(v);
    if (trendAna_ != v.ana) { trend_.clear(); trendAna_ = v.ana; }
    if (!trend_.empty() && v.t < trend_.back().t) trend_.clear();   // the sim time went back (a scenario reload)
    if (trend_.empty() || v.t - trend_.back().t >= 0.25) {
        if (v.ana) trend_.push_back({v.t, std::fabs(v.Fs - v.Fr) / (std::max)(1.0, v.massKg) / kG0, v.residG, std::fabs(v.beta) * 100});
        else trend_.push_back({v.t, std::hypot(v.Fx, v.Fy), v.feltG, std::fabs(v.tvc)});
        while (!trend_.empty() && v.t - trend_.front().t > 60) trend_.pop_front();
    }
}
void Screen::JournalLines(Pad& g, double x, double y, double w, int n, double step) {
    for (size_t i = 0; i < log_.lines.size() && int(i) < n; ++i) {
        const scr::Journal::Line& e = log_.lines[i];
        const unsigned c = e.lvl >= 2 ? kRd : e.lvl == 1 ? kYe : kTx;
        const double yb = y + i * step, tw = g.T(Clock(e.t), x, yb, kDim, 15);
        g.T(Pad::FitTxt(e.txt, w - tw - 10, 15), x + tw + 10, yb, c, 15);
    }
}
// the trends (60 s); with the journal's last lines on top when the journal has no place of its own
void Screen::TrendBlock(Pad& g, double x, double y, double w, double h, int jn, bool ana) {
    if (w < 200) return;
    g.Block(x, y, w, h, jn ? L"ЖУРНАЛ И ТРЕНДЫ" : L"ТРЕНДЫ 60 с");
    struct S { const wchar_t* n; unsigned c; double mx; int k; };
    static const S kAna[3] = {{L"g", kWh, 220, 0}, {L"ощущ.", kVi, 10, 1}, {L"v/c", kYe, 100, 2}};
    static const S kPlan[3] = {{L"тяга", kWh, 2.4e9, 0}, {L"g", kVi, 6, 1}, {L"УВТ", kYe, 10, 2}};
    const S* ser = ana ? kAna : kPlan;
    double lx = x + w - 16;
    for (int i = 2; i >= 0; --i) lx -= g.T(ser[i].n, lx, Pad::BandBase(y, 15), ser[i].c, 15, 2, true) + 12;
    double top = y + kBandH + 8;
    if (jn) { JournalLines(g, x + 16, top + 18, w - 32, jn, 22); top += jn * 22 + 10; g.Line(x + 16, top - 4, x + w - 16, top - 4, kFr, 1); }
    const double px = x + 16, pw = w - 32, py = top + 4, ph = y + h - 10 - py;
    if (ph < 12) return;
    g.Fill(px, py, pw, ph, 0x0b1513); g.Stroke(px, py, pw, ph, kFr, 1);
    for (int k = 1; k < 4; ++k) g.Line(px + 1, py + ph * k / 4, px + pw - 1, py + ph * k / 4, 0x13241f, 1);
    if (trend_.size() < 2) return;
    const double t1 = trend_.back().t;
    for (int si = 0; si < 3; ++si) {
        std::vector<P2> p;
        for (const TP& q : trend_) {
            const double val = ser[si].k == 0 ? q.a : ser[si].k == 1 ? q.b : q.c;
            p.push_back({px + pw - 2 - (t1 - q.t) / 60 * (pw - 4), py + ph - 2 - Clamp(val / ser[si].mx, 0, 1) * (ph - 4)});
        }
        g.PolyLine(p, ser[si].c, 2);
    }
}

// ---- the touch scale (UCGO Arrow): the fill is the actual value from zero, the yellow arrow the set-point; ▲ ▼ keys beside it ----
void Screen::Scale(Pad& g, const ScaleDef& o) {
    const double bw = o.w >= 170 ? 56 : 44, kw = 56, tot = bw + 22 + 6 + kw, x0 = std::round(o.x + (o.w - tot) / 2), y0 = 212, y1 = 457, h = y1 - y0, mid = o.x + o.w / 2;
    auto V = [&](double v) { return y1 - h * (Clamp(v, o.min, o.max) - o.min) / (o.max - o.min); };
    g.T(o.label, mid, 152, o.col, Pad::FitSize(o.label, o.w - 6, 17), 1, true);
    g.T(o.valTxt, mid, 180, o.lim.empty() ? kWh : kRd, Pad::FitSize(o.valTxt, o.w - 6, 24, 18), 1, true);
    if (!o.sub.empty()) g.T(Pad::FitTxt(o.sub, o.w - 6, 15), mid, 202, kYe, 15, 1);
    g.Fill(x0, y0, bw, h, 0x0d1715); g.Stroke(x0, y0, bw, h, kFr, 1.5);
    for (int k = 1; k < 10; ++k) g.Line(x0, y0 + h * k / 10, x0 + (k == 5 ? 12 : 6), y0 + h * k / 10, kLn, 1.5);
    const double base = V(o.zero), ya = V(o.act), f0 = (std::min)(ya, base), fh = std::fabs(base - ya);
    if (fh > 0.5) { g.Fill(x0 + 2, f0, bw - 4, fh, MixC(kBg, o.col, 0.3)); g.Fill(x0 + 6, f0, bw - 12, fh, o.col); }
    const double yp = V(o.set);
    g.Line(x0, yp, x0 + bw, yp, MixC(kBg, kYe, 0.7), 1.5);
    g.Poly({{x0 + bw + 1, yp}, {x0 + bw + 20, yp - 10}, {x0 + bw + 20, yp + 10}}, o.live ? kYe : kLn);
    if (!o.lim.empty()) { g.Fill(x0 + 2, y0 + 2, bw + 18, 24, kRd); g.T(o.lim, x0 + 2 + (bw + 18) / 2, y0 + 14 + Pad::CapH(15) / 2, 0xffffff, 15, 1, true); }
    auto snap = [&](double v) { return Clamp(std::round(v / o.snap) * o.snap, o.min, o.max); };
    g.Key(x0 + bw + 28, y0, kw, 48, L"▲", o.live ? kKeyOff : kKeyNa, kCmdBar + o.bar, snap(o.set + o.step));
    g.Key(x0 + bw + 28, y1 - 48, kw, 48, L"▼", o.live ? kKeyOff : kKeyNa, kCmdBar + o.bar, snap(o.set - o.step));
    if (o.live) {
        front::Hit d = {x0, y0 - 6, bw + 22, h + 12, kCmdBar + o.bar};
        d.drag = true; d.y0 = y0; d.y1 = y1; d.lo = o.min; d.hi = o.max; d.snap = o.snap;
        g.AddHit(d);
    }
}
// the bottom row, under the hand: ОТСЕЧКА, ОБХОД БЛОК. (two presses), ПРЕДЕЛ g
void Screen::KeyRow2(Pad& g, const View& v, double x, double w) {
    const double kw = std::floor((w - 20) / 3 / 10) * 10, y = kLY1 - 8 - 48;
    g.Key(x, y, kw, 48, L"ОТСЕЧКА", kKeyWarn, kCmdCut);
    g.Key(x + kw + 10, y, kw, 48, v.bypass ? L"БЛОК. СНЯТЫ" : v.bypassArmed ? L"ПОДТВЕРДИТЬ" : L"ОБХОД БЛОК.",
          v.bypass ? kKeyWarnOn : v.bypassArmed ? (v.blink ? kKeyWarnOn : kKeyWarn) : kKeyOff, kCmdBypass);
    g.Key(x + 2 * (kw + 10), y, kw, 48, v.gLimOn ? L"ПРЕДЕЛ g ВКЛ" : L"ПРЕДЕЛ g ВЫКЛ", v.gLimOn ? kKeyOn : kKeyOff, kCmdGLimOnOff);
}

// ================= the page =================
void Screen::Draw(oapi::Sketchpad* skp, ScreenFont& font, GostFont& gost, double k, double W, const View& v) {
    hits_.clear();
    if (!skp) return;
    Pad g(skp, font, gost, k, &hits_);
    g.Fill(0, 0, W, kDesignH, kBg); g.Stroke(4, 4, W - 8, kDesignH - 8, kFr, 2);
    // ---- the header: the drive (АНАМЕЗОН / ПЛАНЕТАРНЫЕ), its state in one line ----
    g.Key(kM, 24, 200, 48, L"АНАМЕЗОН", v.ana ? kKeyOn : kKeyOff, kCmdTabAna);
    g.Key(kM + 210, 24, 200, 48, L"ПЛАНЕТАРНЫЕ", v.ana ? kKeyOff : kKeyOn, kCmdTabPlan);
    std::wstring st; unsigned stc;
    if (v.ana) {
        static const wchar_t* const kSt[4] = {L"КАМЕРЫ ВЫКЛЮЧЕНЫ", L"ПОЛЕ КАМЕР", L"ПУЧОК ПОДЖИГА", L"НА РЕЖИМЕ"};
        st = kSt[(std::max)(0, (std::min)(3, v.stage))];
        if (v.stage == 3) {
            const bool limSafe = v.capSafe < 0.999 && !v.bypass, limHot = v.capHot < 0.999 && !v.bypass;
            const bool limA = v.gLimOn && v.residG >= v.gLim * 0.98;
            const double Fn = v.Fs - v.Fr;
            if (limSafe) { st += L" · блокировка: люди в зоне струи"; stc = kYe; }
            else if (limHot) { st += L" · блокировка: горячий старт в атмосфере"; stc = kYe; }
            else if (limA) { st += L" · держит предел " + Num(v.gLim, 1) + L" g"; stc = kYe; }
            else { st += std::wstring(L" · тяга по оси ") + (Fn < 0 ? L"−" : L"") + J(fFu(Fn)); stc = kGr; }
        } else {
            if (v.trans) st += v.ignTarget > v.stage ? L" · пуск идёт" : L" · останов идёт";
            stc = v.stage == 0 && !v.trans ? kDim : kYe;
        }
    } else {
        const double F = std::hypot(v.Fx, v.Fy);
        const bool run = v.Fm > 0 || (v.podsOk && v.podF > 0);
        static const wchar_t* const kEnv[3] = {L"грунт", L"атмосфера", L"космос"};
        static const wchar_t* const kMass[3] = {L"аргон", L"железо", L"продукты"};
        if (!v.plantRun) { st = L"УСТАНОВКА НЕ НА РЕЖИМЕ: маршевая без тяги"; stc = kYe; }
        else {
            st = (run ? L"ТЯГА " + J(fFu(F)) : std::wstring(L"ТЯГИ НЕТ")) + L" · " + kEnv[(std::max)(0, (std::min)(2, v.envKind))] + L" · "
               + kMass[(std::max)(0, (std::min)(2, v.mass))] + L" · " + Num(v.massKg / 1e6, 2) + L" кт";
            const bool limG = v.gLimOn && v.feltG >= v.gLim * 0.98 && run;
            if (limG) { st += L" · держит предел " + Num(v.gLim, 1) + L" g"; stc = kYe; } else stc = run ? kGr : kDim;
        }
    }
    const double sx = kM + 466;
    double sEnd = TabsX0(W) - 24;
    if (v.bypass) { g.T(L"БЛОКИРОВКИ СНЯТЫ", sEnd, 48 + Pad::CapH(20) / 2, kRd, 20, 2, true); sEnd -= Pad::TW(L"БЛОКИРОВКИ СНЯТЫ", 20) + 24; }
    g.Lamp(kM + 442, 48, stc);
    if (sEnd - sx > 40) g.T(Pad::FitTxt(st, sEnd - sx, 20), sx, 48 + Pad::CapH(20) / 2, stc, 20, 0, true);
    // ---- the blocks ----
    if (v.ana) Anamezon(g, v, W); else Planetary(g, v, W);
    // the journal and the trends are history: under the hub (seen with the yoke stowed); on the wide glass the journal has its own place
    const Layout L = Lay(W);
    if (L.wide) {
        g.Block(kM + L.rcW + kGap, kLY0, 400, kLY1 - kLY0, L"ЖУРНАЛ");
        JournalLines(g, kM + L.rcW + kGap + 16, kLY0 + kBandH + 30, 400 - 32, 6, 30);
        TrendBlock(g, L.hb.x0, kLY0, L.hb.x1 - L.hb.x0, kLY1 - kLY0, 0, v.ana);
    } else TrendBlock(g, L.hb.x0, kLY0, L.hb.x1 - L.hb.x0, kLY1 - kLY0, 3, v.ana);
}

void Screen::Planetary(Pad& g, const View& v, double W) {
    const Layout L = Lay(W);
    const double F = std::hypot(v.Fx, v.Fy), Fp = v.podsOk ? v.podF : 0.0, mdTot = v.mdotM + (v.podsOk && v.vP > 0 ? v.nPods * v.podF / v.vP : 0.0);
    const bool run = v.Fm > 0 || Fp > 0, limG = v.gLimOn && v.feltG >= v.gLim * 0.98 && run;
    // ---- ЧАШИ И ВЕКТОРЫ: from aft (the hull, the closed anamezon diaphragms, the march cup, the 4 pods) | side on (the thrust lines) ----
    if (L.aW >= 300) {
        const double x = kM, y = 88, w = L.aW, h = kUY1 - 88;
        g.Block(x, y, w, h, L"ЧАШИ И ВЕКТОРЫ");
        const double c0 = y + kBandH, sw = std::round(w * 0.44), dx = x + sw;
        g.Line(dx, c0 + 8, dx, y + h - 8, kFr, 1.5);
        g.T(L"с кормы", x + 16, c0 + 26, kDim, 17, 0, true); g.T(L"сбоку", dx + 16, c0 + 26, kDim, 17, 0, true);
        const double cx = x + sw / 2, cy = c0 + 112, k = (std::min)(5.4, (sw - 24) / 50);
        SternHull(g, cx, cy, k, 25, c0 + 40);
        for (const P2& q : {P2{-4.2, 5.2}, P2{4.2, 5.2}, P2{-4.2, -2}, P2{4.2, -2}}) g.Ring(cx + q.x * k, cy - q.y * k, 2.4 * k, 0x3b3550, 2);
        CupRing(g, cx, cy - 1.82 * k, 2.2 * k + 6, v.FmField > 0 ? v.Fm / v.FmField : 0.0, v.mass == 0 ? kOr : kVi);
        if (v.Fm > 0 && std::fabs(v.tvc) > 0.05) ArrowMax(g, cx, cy - 1.82 * k, 0, -v.tvc * 5, 60, kYe, 3);
        for (int i = 0; i < 4; ++i) {
            const double px = cx + kPodX[i] * k * 1.35, py = cy + (i < 2 ? -6.2 : 0.6) * k;
            CupRing(g, px, py, 15, v.podsOk ? v.pAct : 0.0, kOr);
            g.T(L"Г" + Num(i + 1), px, py + Pad::CapH(15) / 2, kTx, 15, 1, true);
        }
        const double sx0 = dx + 30, sk = (x + w - 24 - sx0) / 178, sy0 = c0 + 104, sc = 70 / 1.5e9;
        g.Poly({{sx0, sy0 - 16}, {sx0 + 140 * sk, sy0 - 16}, {sx0 + 165 * sk, sy0 - 11}, {sx0 + 178 * sk, sy0}, {sx0 + 165 * sk, sy0 + 11}, {sx0 + 140 * sk, sy0 + 16}, {sx0, sy0 + 16}}, 0x3a4650);
        const double cgx = sx0 + v.sCG * sk;
        g.Ring(cgx, sy0, 7, kRd, 2); g.T(L"ЦМ", cgx, sy0 - 24, kRd, 15, 1, true);
        if (v.Fm > 0) {
            const double tr = v.tvc * kRad, Lm = (std::min)(150.0, 30 + v.Fm * sc);
            g.Poly({{sx0, sy0 - 5}, {sx0 - 22, sy0 - 11}, {sx0 - 22, sy0 + 11}, {sx0, sy0 + 5}}, MixC(kBg, 0xffc878, 0.7));
            ArrowMax(g, sx0, sy0 + 28, Lm * std::cos(tr), -Lm * std::sin(tr) * 3, 160, kOr, 4);
        }
        for (int i = 0; i < 4; i += 2) {
            const double px = sx0 + kPodS[i] * sk, pa = v.nozAct * kRad, Lp = 16 + 40 * (v.podFCap > 0 ? Fp / v.podFCap : 0.0);
            if (Fp > 0) g.Arrow(px, sy0 + 22, px + Lp * std::cos(pa), sy0 + 22 - Lp * std::sin(pa), kOr, 3);
            g.T(L"Г" + Num(i + 1) + L"–" + Num(i + 2), px, sy0 + 62, kDim, 15, 1);
        }
        if (F > 0) ArrowMax(g, cgx, sy0, v.Fx * sc, -v.Fy * sc, 150, kWh, 3);
        const bool m0 = std::fabs(v.pitchM) < 1e6;
        const std::wstring mt = m0 ? std::wstring(L"момент тангажа 0: УВТ держит") : L"момент тангажа " + Num(v.pitchM / 1e9, 2) + L" ГН·м, " + (v.pitchM > 0 ? L"нос вверх" : L"нос вниз");
        g.T(Pad::FitTxt(mt, x + w - 16 - (dx + 16), 15), dx + 16, y + h - 14, m0 ? kGr : kYe, 15);
    }
    // ---- ВЫХОД ЧАШ: the march, a pod (each), the total ----
    {
        static const wchar_t* const kHead[3] = {L"МАРШ.", L"ГОНДОЛА", L"ИТОГО"};
        const double capTot = v.FmField + v.nPods * v.podFCap;
        std::vector<Row> rows = {
            {L"тяга", {C(fFu(v.Fm)), C(fFu(Fp)), C(fFu(F), kYe)}},
            {L"доля поля", {C(Num(v.FmField > 0 ? 100 * v.Fm / v.FmField : 0.0), L"%"), C(Num(v.podFCap > 0 ? 100 * Fp / v.podFCap : 0.0), L"%"), C(Num(capTot > 0 ? 100 * F / capTot : 0.0), L"%", kYe)}},
            {L"струя", {v.Fm > 0 ? C(Num(v.vM / 1e3), L"км/с") : C(L"—", L""), Fp > 0 ? C(Num(v.vP / 1e3), L"км/с") : C(L"—", L""), mdTot > 0 ? C(Num(F / mdTot / 1e3), L"км/с", kYe) : C(L"—", L"")}},
            {L"расход", {C(fMu(v.mdotM)), C(fMu(v.vP > 0 ? Fp / v.vP : 0.0)), C(fMu(mdTot), kYe)}},
            {L"мощность струи", {C(fWu(v.PjetM)), C(fWu(Fp * v.vP / 2)), C(fWu(v.PjetM + v.nPods * Fp * v.vP / 2), kYe)}}};
        Table(g, L.bX, 88, L.bW, kUY1 - 88, L"ВЫХОД ЧАШ", kHead, rows);
    }
    // ---- РАСЧЁТ ----
    {
        const double Wt = v.massKg * v.g, tw = v.g > 0 && Wt > 0 ? F / Wt : 0.0, a = F / (std::max)(1.0, v.massKg), res = v.mass == 0 ? v.argon : v.iron;
        const double isp = mdTot > 0 ? F / mdTot / kG0 : 0.0, dv = v.vM * std::log(v.massKg / (std::max)(1.0, v.massKg - res)) / 1e3;
        Grid(g, kM, kLY0, L.rcW, kLY1 - kLY0, L"РАСЧЁТ", {
            {L"тяга / вес", v.g > 0 ? Num(tw, 2) : L"—", v.g > 0 && tw < 1 && F > 0 ? kYe : kWh},
            {L"ускорение", Num(a, 2) + L" м/с²"},
            {L"висение (сопла вниз): гондолы", v.g > 0 ? Num(v.hoverPods * 100) + L" %" : L"—", v.hoverPods > 1 ? kRd : kWh},
            {L"запас УВТ маршевой", Num(100 * (1 - std::fabs(v.tvc) / (std::max)(0.1, v.tvcMax))) + L" %", std::fabs(v.tvc) >= v.tvcMax - 0.1 ? kRd : kWh},
            {L"аргон / железо", Num(v.argon / 1e6, 2) + L" / " + Num(v.iron / 1e6, 2) + L" кт"},
            {L"хватит на этой тяге", mdTot > 0 ? fT(res / mdTot) : L"—"},
            {L"запас скорости", Num(dv, 1) + L" км/с"},
            {L"удельный импульс", mdTot > 0 ? Num(isp) + L" с" : L"—"}}, 2, 48);
    }
    // ---- УПРАВЛЕНИЕ: five touch scales, the reaction mass, the bottom row ----
    if (L.ctlW >= 480) {
        const double x = L.ctlX, y = 88, w = L.ctlW, h = kLY1 - 88, ix = x + 16, iw = w - 32, sw = iw / 5;
        g.Block(x, y, w, h, L"УПРАВЛЕНИЕ");
        Scale(g, {kBarMarch, ix, sw, L"МАРШ", kOr, 0, 1, 0.05, 0.01, v.mSet, v.mAct, 0, Pct(v.mAct), L"уст. " + Pct(v.mSet),
                  !v.plantRun ? L"УСТ" : limG && v.mSet > v.mAct + 0.005 ? L"ОГР g" : L"", true});
        Scale(g, {kBarPods, ix + sw, sw, L"ГОНДОЛЫ", kOr, 0, 1, 0.05, 0.01, v.pSet, v.podsOk ? v.pAct : 0.0, 0, Pct(v.podsOk ? v.pAct : 0.0), L"уст. " + Pct(v.pSet),
                  !v.podsOk ? L"ЗАКР" : limG && v.pSet > v.pAct + 0.005 ? L"ОГР g" : L"", true});
        Scale(g, {kBarNozzle, ix + 2 * sw, sw, L"СОПЛА", kBl, 0, 180, 5, 1, v.nozSet, v.nozAct, 0, Num(v.nozAct) + L"°",
                  v.nozAct < 45 ? L"тяга вперёд" : v.nozAct < 135 ? L"тяга вверх" : L"торможение", L"", true});
        Scale(g, {kBarTvc, ix + 3 * sw, sw, L"УВТ", kYe, -v.tvcMax, v.tvcMax, 1, 0.5, v.tvc, v.tvc, 0, NumS(v.tvc, 1) + L"°", L"авто, через ЦМ", L"", false});
        Scale(g, {kBarGLim, ix + 4 * sw, sw, L"ПРЕД. g", kVi, 0, 8, 0.5, 0.5, v.gLim, v.feltG, 0, Num(v.feltG, 1) + L" g",
                  !v.gLimOn ? std::wstring(L"СНЯТ") : L"предел " + Num(v.gLim, 1), L"", true});
        // row 1: the working mass (АВТО - by the medium) and the interlock it lives under
        const double y1 = kLY1 - 8 - 48 - 10 - 48, kw = (std::min)(160.0, std::floor((iw - 96 - 240) / 3 / 10) * 10);
        g.T(L"МАССА", ix, y1 + 24 + Pad::CapH(17) / 2, kDim, 17, 0, true);
        static const struct { const wchar_t* n; int m; int cmd; } kMass[3] = {{L"АВТО", -1, kCmdMassAuto}, {L"АРГОН", 0, kCmdMassArgon}, {L"ЖЕЛЕЗО", 1, kCmdMassIron}};
        for (int i = 0; i < 3; ++i) g.Key(ix + 80 + i * (kw + 10), y1, kw, 48, kMass[i].n, v.massMode == kMass[i].m ? kKeyOn : kKeyOff, kMass[i].cmd);
        const double lx = ix + 80 + 3 * (kw + 10) + 14;
        g.Lamp(lx + 10, y1 + 24, v.argonLock ? kGr : 0x2a3a36, 8);
        g.T(Pad::FitTxt(L"в атмосфере только аргон", x + w - 16 - (lx + 28), 17), lx + 28, y1 + 24 + Pad::CapH(17) / 2, v.argonLock ? kTx : kDim, 17);
        KeyRow2(g, v, ix, iw);
    }
}

void Screen::Anamezon(Pad& g, const View& v, double W) {
    const Layout L = Lay(W);
    const double Fn = v.Fs - v.Fr;
    // ---- КАМЕРЫ И СТРУИ: the stern К1-К4, the nose Н1-Н2, the jets and the star ----
    if (L.aW >= 300) {
        const double x = kM, y = 88, w = L.aW, h = kUY1 - 88;
        g.Block(x, y, w, h, L"КАМЕРЫ И СТРУИ");
        const double c0 = y + kBandH, w1 = std::round(w * 0.36), w2 = std::round(w * 0.27), x1 = x + w1, x2 = x1 + w2;
        g.Line(x1, c0 + 8, x1, y + h - 8, kFr, 1.5); g.Line(x2, c0 + 8, x2, y + h - 8, kFr, 1.5);
        g.T(L"корма К1–К4", x + 16, c0 + 26, kDim, 17, 0, true); g.T(L"нос Н1–Н2", x1 + 16, c0 + 26, kDim, 17, 0, true); g.T(L"струи", x2 + 16, c0 + 26, kDim, 17, 0, true);
        const double cy = c0 + 100, k = (std::min)(5.6, (w1 - 24) / 40), cx = x + w1 / 2;
        SternHull(g, cx, cy, k, 19, c0 + 40);
        const P2 pos[4] = {{-4.2, 5.2}, {4.2, 5.2}, {-4.2, -2}, {4.2, -2}};
        for (int i = 0; i < 4; ++i) Chamber(g, cx + pos[i].x * k, cy - pos[i].y * k, 2.7 * k, v.fieldL, v.fsAct, v.t, L"К" + Num(i + 1));
        g.T(J(fFu(v.Fs)), cx, y + h - 14, kWh, 17, 1, true);
        const double nx = x1 + w2 / 2, nk = (std::min)(9.0, (w2 - 24) / 16);
        g.Ellipse(nx, cy, 7.5 * nk, 6 * nk, 0x16241f, 0x3f6a5f, 2);
        for (int i = 0; i < 2; ++i) Chamber(g, nx + (i ? 3 : -3) * nk, cy, 2.2 * nk, v.fieldL, v.frAct, v.t, i ? L"Н2" : L"Н1");
        g.T(v.Fr > 0 ? L"−" + J(fFu(v.Fr)) : std::wstring(L"0"), nx, y + h - 14, kOr, 17, 1, true);
        // the jets and the star: the stern jet goes aft, the retro jets ahead; the star where it is
        const double w3 = x + w - x2, sx = x2 + w3 / 2, sy = cy - 6;
        const bool ahead = v.starCos > 0, toStar = v.starAU > 0 && v.starAU < 50;
        g.Fill(sx - 34, sy - 7, 54, 14, 0x3a4650); g.Poly({{sx + 20, sy - 7}, {sx + 36, sy}, {sx + 20, sy + 7}}, 0x3a4650);
        auto jet = [&](double x0, int dir, double f, bool warn) {
            if (f > 0.001) {
                const double len = (std::max)(10.0, w3 / 2 - 50) * std::sqrt((std::min)(1.0, f)), hf = std::tan(5 * kRad) * len;
                g.Poly({{x0, sy - 4}, {x0 + dir * len, sy - hf - 4}, {x0 + dir * len, sy + hf + 4}, {x0, sy + 4}}, MixC(kBg, 0xb4a0ff, 0.55));
            }
            if (warn) { const double xx = x0 + dir * 30; g.Line(xx - 9, sy - 9, xx + 9, sy + 9, kRd, 3); g.Line(xx + 9, sy - 9, xx - 9, sy + 9, kRd, 3); }   // toward the star
        };
        jet(sx - 34, -1, v.fsAct, toStar && !ahead && v.fsAct > 0.001);
        jet(sx + 36, 1, v.frAct, toStar && ahead && v.frAct > 0.001);
        if (v.starAU > 0) {
            const double starX = ahead ? x + w - 22 : x2 + 22;
            g.Disc(starX, sy + 44, 7, 0xe8c860);
            const std::wstring s = L"звезда " + Num(v.starAU, v.starAU < 10 ? 2 : 0) + L" а.е.";
            g.T(Pad::FitTxt(s, w3 - 50, 15), ahead ? starX - 14 : starX + 14, sy + 44 + Pad::CapH(15) / 2, kYe, 15, ahead ? 2 : 0);
        }
        if (std::fabs(Fn) > 1) g.Arrow(sx, sy + 82, sx + Clamp(Fn / 1e10, -9, 9) * 9, sy + 82, Fn >= 0 ? kWh : kOr, 3);
        g.T(Fn > 1 ? L"разгон" : Fn < -1 ? L"торможение" : L"тяги нет", sx, y + h - 14, Fn >= 0 ? kWh : kOr, 15, 1);
    }
    // ---- ВЫХОД КАМЕР: a stern chamber, a retro cup, along the axis ----
    {
        static const wchar_t* const kHead[3] = {L"КАМЕРА", L"РЕТРО", L"ПО ОСИ"};
        const double fs = v.fsAct, fr = v.frAct, rate = v.pelletRate, pm = v.pelletMass;
        const NU fn = fFu(Fn);
        std::vector<Row> rows = {
            {L"подача", {C(Num(fs * 100), L"%"), C(Num(fr * 100), L"%"), kEmpty}},
            {L"капсулы", {C(Num(rate * fs / 1e3, 1), L"кГц"), C(Num(rate * fr / 1e3, 1), L"кГц"), C(Num(rate * (v.nAna * fs + v.nRetro * fr) / 1e3, 1), L"кГц", kYe)}},
            {L"капсула", {fs > 0.001 ? C(Num(pm * 1e3, 1), L"г") : C(L"—", L""), fr > 0.001 ? C(Num(pm * 1e3, 1), L"г") : C(L"—", L""), kEmpty}},
            {L"тяга", {C(fFu(v.chamberF * fs)), C(fFu(v.retroF * fr)), C((Fn < 0 ? L"−" : L"") + fn.n, fn.u, kYe)}},
            {L"поле", {C(Num(1000 * v.fieldL), L"Тл", kVi), C(Num(1000 * v.fieldL), L"Тл", kVi), kEmpty}}};
        Table(g, L.bX, 88, L.bW, kUY1 - 88, L"ВЫХОД КАМЕР", kHead, rows);
    }
    // ---- РАСЧЁТ ----
    {
        double fuel = 0; for (int i = 0; i < 4; ++i) fuel += v.traps[i];
        const double m = (std::max)(1.0, v.massKg), a = Fn / m, rap = std::atanh(Clamp(v.beta, -0.999999, 0.999999));
        const double rapLeft = v.vEff / kC * std::log(m / (std::max)(1.0, m - fuel));
        const bool along = std::fabs(rap) < 1e-9 || (a >= 0) == (rap >= 0);
        const double stopT = !along && std::fabs(a) > 0 ? std::fabs(rap) * kC / std::fabs(a) : HUGE_VAL;
        const double vMax = std::tanh(std::fabs(rap) + rapLeft), vBr = std::tanh((std::fabs(rap) + rapLeft) / 2);
        Grid(g, kM, kLY0, L.rcW, kLY1 - kLY0, L"РАСЧЁТ", {
            {L"ускорение по оси", std::wstring(a < 0 ? L"−" : L"+") + Num(std::fabs(a) / kG0, 1) + L" g" + (std::fabs(a) < 1e-6 ? L"" : along ? L" разгон" : L" торм.")},
            {L"компенсатор гасит", Num(v.compAcc / kG0, 1) + L" g", kGr},
            {L"скорость", Num(std::fabs(v.beta), 4) + L" c · " + Num(std::fabs(v.beta) * kC / 1e3) + L" км/с"},
            {L"фактор Лоренца", Num(v.gamma, 3)},
            {L"время корабля / Земли", Num(1 / (std::max)(1.0, v.gamma), 3)},
            {L"анамезон в ловушках", Num(fuel / 1e6, 2) + L" кт · Л" + Num((std::min)(4, v.activeTrap + 1)), fuel <= 0 ? kYe : kWh},
            {along ? L"до пустых ловушек" : L"до остановки", along ? (v.mdotA > 0 ? fT(fuel / v.mdotA) : L"—") : fT(stopT)},
            {L"предел: разгон / с торм.", Num(vMax, 3) + L" / " + Num(vBr, 3) + L" c", vBr >= 0.42 ? kGr : kYe},
            {L"истечение: частицы / эфф.", Num(v.vJet / kC, 2) + L" / " + Num(v.vEff / kC, 3) + L" c"},
            {L"мощность струй · расход", J(fWu(v.Pjet)) + L" · " + J(fMu(v.mdotA))}}, 2, 32);
    }
    // ---- УПРАВЛЕНИЕ: the feed scales, the felt g; the start by phases; start / stop, the trap, the compensator; the bottom row ----
    if (L.ctlW >= 480) {
        const double x = L.ctlX, y = 88, w = L.ctlW, h = kLY1 - 88, ix = x + 16, iw = w - 32, pw = w >= 900 ? 300 : 258, sw = (iw - pw - 16) / 3;
        const bool run = v.stage == 3, limSafe = v.capSafe < 0.999 && !v.bypass, limHot = v.capHot < 0.999 && !v.bypass;
        const bool limA = run && v.gLimOn && v.residG >= v.gLim * 0.98;
        auto limOf = [&](double set, double act) -> std::wstring {
            if (!run && set > 0) return L"НЕ ГОТОВ";
            if (set > act + 0.005) { if (limSafe) return L"ЛЮДИ"; if (limHot) return L"АТМ"; if (limA) return L"ОГР g"; }
            return L"";
        };
        g.Block(x, y, w, h, L"УПРАВЛЕНИЕ");
        Scale(g, {kBarFeed, ix, sw, L"КОРМА", kVi, 0, 1, 0.05, 0.01, v.fsSet, v.fsAct, 0, Pct(v.fsAct), L"уст. " + Pct(v.fsSet), limOf(v.fsSet, v.fsAct), true});
        Scale(g, {kBarRetro, ix + sw, sw, L"РЕВЕРС", kOr, 0, 1, 0.05, 0.01, v.frSet, v.frAct, 0, Pct(v.frAct), L"уст. " + Pct(v.frSet), limOf(v.frSet, v.frAct), true});
        Scale(g, {kBarResid, ix + 2 * sw, sw, L"ОЩУЩ. g", kGr, 0, 10, 0.5, 0.5, v.gLim, v.residG, 0, Num(v.residG, 2) + L" g",
                  !v.gLimOn ? std::wstring(L"СНЯТ") : L"предел " + Num(v.gLim, 1), L"", true});
        // the start handle by phases: each key sets the handle to its position; the bar under it - the phase's progress
        const double px = ix + 3 * sw + 16;
        g.Line(px - 8, y + kBandH + 10, px - 8, kLY1 - 8 - 48 - 10 - 48 - 12, kFr, 1);
        g.T(L"ПУСК ПО ФАЗАМ", px, 152, kDim, 17, 0, true);
        static const wchar_t* const kPh[3] = {L"ПОЛЕ", L"ПУЧОК", L"ПОДАЧА"};
        const double lv[3] = {v.fieldL, v.beamL, v.feedL};
        for (int i = 0; i < 3; ++i) {
            const double ky = 168 + i * 72;
            g.Key(px, ky, pw - 72, 48, kPh[i], v.stage >= i + 1 ? kKeyOn : kKeyOff, kCmdPhase1 + i);
            g.Fill(px, ky + 54, pw - 72, 8, kTrack); g.Fill(px, ky + 54, (pw - 72) * Clamp(lv[i], 0, 1), 8, i == 2 ? kOr : kGr);
            g.T(Num(lv[i] * 100) + L" %", px + pw, ky + 24 + Pad::CapH(17) / 2, kTx, 17, 2, true);
        }
        g.T(L"накопитель поля " + Num(100 * v.store) + L" %", px, 412 + Pad::CapH(17) / 2, kTx, 17);
        // row 1: start / stop, the next trap, the compensator (a lamp: it is not a key)
        const double y1 = kLY1 - 8 - 48 - 10 - 48, kw = std::floor((iw - 20) / 3 / 10) * 10;
        const bool ignOff = v.stage == 0 && v.ignTarget == 0;
        g.Key(ix, y1, kw, 48, ignOff ? L"ПУСК КАМЕР" : L"СТОП КАМЕР", ignOff ? kKeyOff : kKeyOn, kCmdIgnition);
        g.Key(ix + kw + 10, y1, kw, 48, L"ЛОВУШКА ▶", kKeyOff, kCmdTrap);
        const double lx = ix + 2 * (kw + 10);
        g.Lamp(lx + 12, y1 + 24, kGr, 8);
        g.T(L"компенсатор вкл", lx + 30, y1 + 24 + Pad::CapH(17) / 2, kTx, 17);
        KeyRow2(g, v, ix, iw);
    }
}

}  // namespace tantra::enginescreen
