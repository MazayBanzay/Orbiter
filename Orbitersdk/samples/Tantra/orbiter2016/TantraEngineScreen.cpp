// TantraEngineScreen: see TantraEngineScreen.h. Planetary() and Anamezon() follow pagePlanetary() and pageAnamezon() of
// Tantra_Design/tantra_engines_screen.html; the left column and the control column keep the mockup's coordinates, the middle
// column (the tables, the reckoning, the trends) spans what is between them.
#include "TantraEngineScreen.h"

#include <algorithm>
#include <cmath>

namespace tantra::enginescreen {

using namespace tantra::scr;
using namespace tantra::scr::ui;

namespace {

constexpr double kC = 299792458.0, kG0 = 9.80665;
const double kPodS[4] = {44, 44, 84, 84}, kPodX[4] = {-11.5, 11.5, -11.5, 11.5};

std::wstring fF(double F) { F = std::fabs(F); return F >= 1e9 ? Fmt(F / 1e9, 2) + L" ГН" : F >= 1e6 ? Fmt(F / 1e6, 0) + L" МН" : F >= 1e3 ? Fmt(F / 1e3, 0) + L" кН" : L"0"; }
std::wstring fW(double W) { return W >= 1e18 ? Fmt(W / 1e18, 1) + L" ЭВт" : W >= 1e15 ? Fmt(W / 1e15, 1) + L" ПВт" : W >= 1e12 ? Fmt(W / 1e12, 1) + L" ТВт" : W >= 1e9 ? Fmt(W / 1e9, 1) + L" ГВт" : W >= 1e6 ? Fmt(W / 1e6, 0) + L" МВт" : L"0"; }
std::wstring fM(double m) { return m >= 1000 ? Fmt(m / 1000, 2) + L" т/с" : m >= 1 ? Fmt(m, 1) + L" кг/с" : m > 0 ? Fmt(m * 1000, 0) + L" г/с" : L"0"; }
std::wstring fT(double s) { return !std::isfinite(s) ? L"—" : s >= 86400 ? Fmt(s / 86400, 1) + L" сут" : s >= 3600 ? Fmt(s / 3600, 1) + L" ч" : s >= 60 ? Fmt(s / 60, 1) + L" мин" : Fmt(s, 0) + L" с"; }
std::wstring Pct(double f) { return Fmt(f * 100, 0) + L" %"; }

// a cup seen from aft: the ring of its field, the fill of its output, a glow in it
void CupRing(Canvas& g, double x, double y, double r, double f, unsigned col) {
    g.Circle(x, y, r, 0x2c4a44, 6);
    if (f > 0.001) {
        g.Arc(x, y, r, -kPi / 2, -kPi / 2 + 2 * kPi * (std::min)(1.0, f), col, 6);
        const double a = (std::min)(1.0, f);
        g.Disc(x, y, r - 4, Mix(0xff9640, cBg, 0.35 * a));
        g.Disc(x, y, (r - 4) * 0.6, Mix(0xfff0dc, cBg, 0.6 * a));
    }
}
void Chamber(Canvas& g, double x, double y, double r, double field, double f, double t, const wchar_t* label) {
    g.Circle(x, y, r, 0x2a2440, 6);
    if (field > 0.001) g.Arc(x, y, r, -kPi / 2, -kPi / 2 + 2 * kPi * (std::min)(1.0, field), field >= 1 ? cVi : 0x7a68b0, 6);
    if (f > 0.001) {
        const double s = 0.55 + 0.45 * std::fabs(std::sin(t * 47 + x)), a = s * (std::min)(1.0, f + 0.2);
        g.Disc(x, y, r - 3, Mix(0xbea0ff, cBg, 0.5 * a));
        g.Disc(x, y, (r - 3) * 0.5, Mix(0xffffff, cBg, a));
    }
    g.T(label, x, y + 4, cWh, 12, 1, 700);
}
void SternView(Canvas& g, double cx, double cy, double k, double crest) {
    g.Ellipse(cx, cy, 13.5 * k, 9 * k, 0x16241f, 0x3f6a5f, 2);
    g.Line(cx - 13.5 * k, cy - 1 * k, cx - crest * k, cy - 1.5 * k, 0x2e4d45, 3);
    g.Line(cx + 13.5 * k, cy - 1 * k, cx + crest * k, cy - 1.5 * k, 0x2e4d45, 3);
    g.Line(cx, cy - 9 * k, cx, cy - (std::min)(20.0, crest) * k, 0x2e4d45, 3);
}

}  // namespace

bool Screen::BarValue(int cmd, double along, int* bar, double* value) const {
    int b = -1; double v = 0;
    if (cmd >= kCmdBar && cmd < kCmdBar + kBarCount) { b = cmd - kCmdBar; const BarDef& d = bars_[b]; v = d.min + along * (d.max - d.min); v = std::round(v / d.snap) * d.snap; }
    else if (cmd >= kCmdUp && cmd < kCmdUp + kBarCount) { b = cmd - kCmdUp; v = bars_[b].set + bars_[b].step; }
    else if (cmd >= kCmdDown && cmd < kCmdDown + kBarCount) { b = cmd - kCmdDown; v = bars_[b].set - bars_[b].step; }
    if (b < 0 || !bars_[b].live) return false;
    *bar = b; *value = (std::max)(bars_[b].min, (std::min)(bars_[b].max, v));
    return true;
}

// the touch scale (UCGO Arrow): the fill is the actual value from zero, the yellow arrow the set-point
void Screen::TBar(Canvas& g, int bar, double x, double w, const wchar_t* label, unsigned col, double min, double max, double step, double snap,
                  double set, double act, double zero, const std::wstring& actTxt, const std::wstring& sub, const std::wstring& lim, bool live,
                  const std::vector<std::pair<double, std::wstring>>& ticks) {
    const double y0 = 132, h = 380, y1 = y0 + h, cw = w + 22, cx = x + cw / 2;
    auto V = [&](double v) { return y1 - h * ((std::max)(min, (std::min)(max, v)) - min) / (max - min); };
    bars_[bar] = {min, max, step, snap, set, live};
    g.T(label, cx, 92, col, 14, 1, 700);
    auto arrowKey = [&](double y, bool up, int cmd) {
        g.Fill(x, y, cw, 26, 0x121d1b); g.Stroke(x, y, cw, 26, live ? 0x5a4325 : 0x2a3a36, 1);
        const double m = cx, a = up ? y + 7 : y + 19, b = up ? y + 19 : y + 7;
        g.Shape({{m, a}, {m - 9, b}, {m + 9, b}}, live ? cOr : cDim);
        if (live) hits_.push_back({x, y, cw, 26, cmd});
    };
    arrowKey(100, true, kCmdUp + bar);
    g.Fill(x, y0, w, h, 0x0d1715); g.Stroke(x, y0, w, h, cFr, 1);
    for (int k = 0; k <= 10; ++k) g.Fill(x, y0 + h * k / 10, k % 5 ? 6 : 12, 1, 0x2c4a44);
    for (const auto& t : ticks) g.T(t.second, x + 15, V(t.first) + 4, cDim, 10);
    const double base = V(zero), ya = V(act);
    if (std::fabs(base - ya) > 0.5) { g.Fill(x + 1, (std::min)(ya, base), w - 2, std::fabs(base - ya), col, 0.25); g.Fill(x + 4, (std::min)(ya, base), w - 8, std::fabs(base - ya), col, 0.85); }
    const double yp = V(set);
    g.Line(x, yp, x + w, yp, cYe, 1.5, 0.7);
    g.Shape({{x + w + 1, yp}, {x + w + 20, yp - 10}, {x + w + 20, yp + 10}}, live ? cYe : cDim);
    if (!lim.empty()) { g.Fill(x + 2, y0 + 2, w - 4, 18, cRd, 0.9); g.T(lim, x + w / 2, y0 + 15, 0xffffff, 10, 1, 700); }
    arrowKey(518, false, kCmdDown + bar);
    g.T(actTxt, cx, 568, lim.empty() ? cWh : cRd, 16, 1, 700);
    g.T(sub, cx, 588, cYe, 11, 1);
    if (live) hits_.push_back({x, y0 - 6, cw, h + 12, kCmdBar + bar, true});
}

void Screen::Trends(Canvas& g, double x, double y, double w, double h, bool ana) {
    Frame(g, x, y, w, h, L"ТРЕНДЫ");
    struct S { const wchar_t* n; unsigned c; double max; int k; };
    static const S kAna[3] = {{L"ускорение, g", cWh, 220, 0}, {L"ощущается", cVi, 10, 1}, {L"v/c ×100", cYe, 100, 2}};
    static const S kPlan[3] = {{L"тяга", cWh, 2.4e9, 0}, {L"перегрузка", cVi, 6, 1}, {L"УВТ", cYe, 10, 2}};
    const S* ser = ana ? kAna : kPlan;
    const double t1 = trend_.empty() ? 0 : trend_.back().t;
    for (int si = 0; si < 3; ++si) {
        const S& s = ser[si];
        std::vector<Pt> p;
        for (const TP& q : trend_) {
            const double val = s.k == 0 ? q.a : s.k == 1 ? q.b : q.c;
            p.push_back({x + w - 10 - (t1 - q.t) / 60 * (w - 20), y + h - 8 - (std::min)(1.0, (std::max)(0.0, std::fabs(val) / s.max)) * (h - 34)});
        }
        g.Polyline(p, s.c, 1.6);
    }
    for (int i = 0; i < 3; ++i) g.T(ser[i].n, x + 14 + i * 120, y + 24, ser[i].c, 11);
    g.T(L"60 с", x + w - 10, y + 24, cDim, 10, 2);
}
void Screen::JournalBox(Canvas& g, double x, double y, double w, double h) {
    Frame(g, x, y, w, h, L"ЖУРНАЛ");
    for (size_t i = 0; i < log_.lines.size() && i < 5; ++i)
        g.T(g.Fit(Clock(log_.lines[i].t) + L"  " + log_.lines[i].txt, w - 24, 12), x + 12, y + 28 + i * 18.0, Journal::Col(log_.lines[i].lvl), 12);
}

void Screen::Watch(const View& v) {
    if (!seen_) { seen_ = true; last_ = v; log_.Add(v.t, v.ana ? L"Пульт: анамезон" : L"Пульт: планетарные", 0); return; }
    const View& o = last_;
    static const wchar_t* const kSt[4] = {L"камеры выключены", L"поле камер", L"пучок поджига", L"камеры на режиме"};
    if (v.ana != o.ana) log_.Add(v.t, v.ana ? L"Главная тяга: анамезон" : L"Главная тяга: планетарные", 0);
    if (v.stage != o.stage) log_.Add(v.t, std::wstring(L"Анамезон: ") + kSt[(std::max)(0, (std::min)(3, v.stage))], 0);
    if (v.bypass != o.bypass) log_.Add(v.t, v.bypass ? L"БЛОКИРОВКИ СНЯТЫ КОМАНДИРОМ" : L"Блокировки восстановлены", v.bypass ? 2 : 0);
    if (v.plantRun != o.plantRun) log_.Add(v.t, v.plantRun ? L"Установка на режиме: маршевая готова" : L"Установка не на режиме", v.plantRun ? 0 : 1);
    if (v.mass != o.mass) log_.Add(v.t, std::wstring(L"Рабочая масса: ") + (v.mass == 0 ? L"аргон" : v.mass == 1 ? L"железо" : L"продукты"), 0);
    if ((v.capHot < 0.999) != (o.capHot < 0.999)) log_.Add(v.t, v.capHot < 0.999 ? L"Блокировка: горячий старт в атмосфере" : L"Горячий старт: без ограничения", 1);
    if ((v.capSafe < 0.999) != (o.capSafe < 0.999)) log_.Add(v.t, v.capSafe < 0.999 ? L"Блокировка: люди в зоне струи" : L"Зона струи свободна", 1);
    if (v.activeTrap != o.activeTrap) log_.Add(v.t, L"Подача из ловушки " + Fmt(v.activeTrap + 1, 0), 0);
    last_ = v;
}

void Screen::Draw(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v) {
    hits_.clear();
    for (BarDef& b : bars_) b.live = false;
    if (!skp) return;
    Watch(v);
    if (trendAna_ != v.ana) { trend_.clear(); trendAna_ = v.ana; }
    if (trend_.empty() || v.t - trend_.back().t >= 0.25 || v.t < trend_.back().t) {
        if (!trend_.empty() && v.t < trend_.back().t) trend_.clear();
        if (v.ana) trend_.push_back({v.t, std::fabs(v.Fs - v.Fr) / (std::max)(1.0, v.massKg) / kG0, v.residG, v.beta * 100});
        else trend_.push_back({v.t, std::hypot(v.Fx, v.Fy), v.feltG, std::fabs(v.tvc)});
        while (!trend_.empty() && v.t - trend_.front().t > 60) trend_.pop_front();
    }
    Canvas g(skp, font, ox, oy);
    const double W = w, H = h;
    g.Fill(0, 0, W, H, cBg); g.Stroke(5, 5, W - 10, H - 10, cFr, 2);
    g.T(L"ДВИГАТЕЛИ", 22, 38, cTx, 22, 0, 700);
    Btn(g, hits_, L"АНАМЕЗОН", 175, 14, 170, 38, v.ana, kCmdTabAna, cVi);
    Btn(g, hits_, L"ПЛАНЕТАРНЫЕ", 352, 14, 170, 38, !v.ana, kCmdTabPlan, cOr);
    if (v.bypass) g.T(L"БЛОКИРОВКИ СНЯТЫ", W - 22, 38, cRd, 18, 2, 700);
    if (v.ana) Anamezon(g, v, W, H); else Planetary(g, v, W, H);
}

void Screen::Planetary(Canvas& g, const View& v, double W, double H) {
    (void)H;
    const double xR = W - 500, mx = 560, mw = xR - 15 - mx;
    auto cX = [&](double x) { return mx + (x - 560) * mw / 525.0; };
    const double F = std::hypot(v.Fx, v.Fy);
    const bool run = v.Fm > 0 || v.podF > 0;
    const wchar_t* massN = v.mass == 0 ? L"аргон" : v.mass == 1 ? L"железо" : L"продукты";
    const wchar_t* envN = v.envKind == 0 ? L"грунт" : v.envKind == 1 ? L"атмосфера" : L"космос";
    const bool limG = v.gLimOn && !v.bypass && v.feltG >= v.gLim * 0.98 && run;
    const unsigned stc = limG ? cYe : run ? cGr : cDim;
    g.Disc(548, 32, 9, stc);
    g.T(std::wstring(run ? L"ТЯГА " + fF(F) : std::wstring(L"ТЯГИ НЕТ")) + L" · " + envN + L" · " + massN + L" · " + Fmt(v.massKg / 1e6, 2) + L" кт", 564, 38, stc, 15, 0, 700);
    // --- the stern: cups and vectors ---
    Frame(g, 15, 64, 530, 316, L"ЧАШИ · ВИД С КОРМЫ");
    const double cx = 280, cy = 228, k = 8.6;
    SternView(g, cx, cy, k, 25);
    for (const Pt& q : {Pt{-4.2, 5.2}, Pt{4.2, 5.2}, Pt{-4.2, -2}, Pt{4.2, -2}}) g.Circle(cx + q.x * k, cy - q.y * k, 2.4 * k, 0x3b3550, 2);
    g.T(L"анамезон: диафрагмы закрыты", cx, cy + 108, 0x6a6085, 11, 1);
    CupRing(g, cx, cy - 1.82 * k, 2.2 * k + 6, v.FmField > 0 ? v.Fm / v.FmField : 0.0, v.mass == 0 ? cOr : cVi);
    g.T(fF(v.Fm), cx, cy - 1.82 * k + 4, cWh, 12, 1, 700);
    if (v.Fm > 0 && std::fabs(v.tvc) > 0.05) g.Arrow(cx, cy - 1.82 * k, cx, cy - 1.82 * k - v.tvc * 6, cYe, 3);
    for (int i = 0; i < 4; ++i) {
        const double px = cx + kPodX[i] * k * 1.35, py = cy - (i < 2 ? 3.4 : -0.6) * k - 20;
        CupRing(g, px, py, 16, v.podsOk ? v.pAct : 0.0, cOr);
        g.T(L"Г" + Fmt(i + 1, 0), px, py + 4, cTx, 11, 1, 700);
    }
    g.T(L"УВТ маршевой " + Fmt(v.tvc, 1) + L"° из ±" + Fmt(v.tvcMax, 0) + L"° · сопла гондол " + Fmt(v.nozAct, 0) + L"° · " + (v.podsOk ? L"гондолы выдвинуты" : L"гондолы в отсеках"), 30, 368, cDim, 12);
    // --- the side: thrust lines about the CG ---
    Frame(g, 15, 396, 530, 254, L"ВЕКТОРЫ ТЯГИ · СБОКУ");
    const double sx0 = 52, sy0 = 515, sk = 2.55;
    if (v.envKind == 0) g.Line(25, sy0 + 60, 535, sy0 + 60, 0x5a4f35, 2);
    g.Shape({{sx0, sy0 - 18}, {sx0 + 140 * sk, sy0 - 18}, {sx0 + 165 * sk, sy0 - 12}, {sx0 + 178 * sk, sy0}, {sx0 + 165 * sk, sy0 + 12}, {sx0 + 140 * sk, sy0 + 18}, {sx0, sy0 + 18}}, 0x3a4650);
    const double cgx = sx0 + v.sCG * sk;
    g.Circle(cgx, sy0, 7, cRd, 2); g.T(L"ЦМ", cgx, sy0 - 12, cRd, 11, 1, 700);
    const double sc = 90 / 1.5e9;
    if (v.Fm > 0) {
        const double tr = v.tvc * kPi / 180, L = 40 + v.Fm * sc;
        g.Shape({{sx0, sy0 - 5}, {sx0 - 30, sy0 - 12}, {sx0 - 30, sy0 + 12}, {sx0, sy0 + 5}}, Mix(0xffc878, cBg, 0.7));
        g.Arrow(sx0, sy0 + 26, sx0 + L * std::cos(tr), sy0 + 26 - L * std::sin(tr) * 3, cOr, 4);
        g.T(L"маршевая", sx0, sy0 + 48, cOr, 11);
    }
    for (int i = 0; i < 4; i += 2) {
        const double x = sx0 + kPodS[i] * sk, pa = v.nozAct * kPi / 180, L = v.podF * 2 * sc * 1.4;
        if (v.podF > 0) g.Arrow(x, sy0 + 20, x + L * std::cos(pa), sy0 + 20 - L * std::sin(pa), cOr, 3);
        g.T(L"Г" + Fmt(i + 1, 0) + L"-" + Fmt(i + 2, 0), x, sy0 + 50, cDim, 11, 1);
    }
    if (F > 0) g.Arrow(cgx, sy0, cgx + v.Fx * sc * 0.5, sy0 - v.Fy * sc * 0.5, cWh, 3);
    g.T(std::fabs(v.pitchM) < 1e6 ? std::wstring(L"момент тангажа ≈ 0 (УВТ держит)") : L"момент тангажа " + Fmt(v.pitchM / 1e9, 2) + L" ГН·м — " + (v.pitchM > 0 ? L"нос вверх" : L"нос вниз"),
        30, 638, std::fabs(v.pitchM) < 1e6 ? cGr : cYe, 12);
    JournalBox(g, 15, 666, 530, 119);
    // --- the cups' output ---
    Frame(g, mx, 64, mw, 316, L"ВЫХОД ЧАШ");
    const double cols[6] = {cX(575), cX(668), cX(758), cX(828), cX(918), cX(1000)};
    const wchar_t* hd[6] = {L"", L"тяга", L"% поля", L"струя", L"расход", L"мощн. струи"};
    for (int i = 0; i < 6; ++i) g.T(hd[i], cols[i], 92, cDim, 12, 0, 700);
    auto row = [&](double y, const std::wstring& n, double Fv, double cap, double vx, double m, double P, unsigned col) {
        g.T(n, cols[0], y, col, 13, 0, 600); g.T(fF(Fv), cols[1], y, cWh, 13); g.T(Fmt(cap > 0 ? 100 * Fv / cap : 0, 0) + L" %", cols[2], y, cTx, 13);
        g.T(Fv > 0 ? Fmt(vx / 1e3, 0) + L" км/с" : L"—", cols[3], y, cTx, 13); g.T(fM(m), cols[4], y, cTx, 13); g.T(fW(P), cols[5], y, cTx, 13);
    };
    row(118, L"Маршевая", v.Fm, v.FmField, v.vM, v.mdotM, v.PjetM, cTx);
    for (int i = 0; i < 4; ++i) row(144 + i * 24, L"Гондола " + Fmt(i + 1, 0), v.podsOk ? v.podF : 0, v.podFCap, v.vP, v.podsOk && v.vP > 0 ? v.podF / v.vP : 0, v.podsOk ? v.podF * v.vP / 2 : 0, cTx);
    g.Line(cols[0], 252, mx + mw - 15, 252, cFr, 1);
    const double mdTot = v.mdotM + (v.podsOk && v.vP > 0 ? 4 * v.podF / v.vP : 0);
    row(274, L"ИТОГО", F, v.FmField + 4 * v.podFCap, mdTot > 0 ? F / mdTot : 0, mdTot, v.PjetM + (v.podsOk ? 4 * v.podF * v.vP / 2 : 0), cYe);
    g.T(L"маршевая: предел поля " + fF(v.FmField) + L" · гондола: 3 чаши, " + fF(v.podFCap) + L" вместе", cols[0], 306, cDim, 12);
    g.T(L"удельный импульс " + (mdTot > 0 ? Fmt(F / mdTot / kG0, 0) + L" с" : std::wstring(L"—")) + L" · вперёд " + fF((std::max)(0.0, v.Fx)) + (v.Fx < 0 ? L" · назад " + fF(-v.Fx) : L"") + L" · вверх " + fF(v.Fy), cols[0], 328, cDim, 12);
    if (limG) g.T(L"приводы держит предел перегрузки " + Fmt(v.gLim, 1) + L" g", cols[0], 352, cYe, 12, 0, 700);
    else if (!v.plantRun) g.T(L"установка не на режиме: маршевая без тяги (пуск — на экране установки)", cols[0], 352, cYe, 12, 0, 700);
    // --- the reckoning ---
    Frame(g, mx, 396, mw, 254, L"РАСЧЁТ");
    const double Wt = v.massKg * v.g, tw = v.g > 0 ? F / Wt : 0, a = F / (std::max)(1.0, v.massKg);
    const double res = v.mass == 0 ? v.argon : v.iron;
    struct R { const wchar_t* n; std::wstring val; unsigned c; };
    const R rows[7] = {{L"тяга / вес", v.g > 0 ? Fmt(tw, 2) : L"—", v.g > 0 && tw < 1 && F > 0 ? cYe : cTx},
                       {L"ускорение · перегрузка", Fmt(a, 2) + L" м/с² · " + Fmt(v.feltG, 2) + L" g", v.gLimOn && v.feltG > v.gLim ? cRd : cTx},
                       {L"висение лёжа: нужно гондол", v.g > 0 ? Fmt(v.hoverPods * 100, 0) + L" % (сопла вниз)" : L"—", v.hoverPods > 1 ? cRd : cTx},
                       {L"запас УВТ маршевой", Fmt(100 * (1 - std::fabs(v.tvc) / v.tvcMax), 0) + L" %", std::fabs(v.tvc) >= v.tvcMax - 0.1 ? cRd : cTx},
                       {L"аргон / железо", Fmt(v.argon / 1e6, 2) + L" / " + Fmt(v.iron / 1e6, 2) + L" кт", cTx},
                       {L"хватит на этой тяге", mdTot > 0 ? fT(res / mdTot) : L"—", cTx},
                       {L"Δv на остатке массы", Fmt(v.vM * std::log(v.massKg / (std::max)(1.0, v.massKg - res)) / 1e3, 1) + L" км/с", cTx}};
    for (int i = 0; i < 7; ++i) { g.T(rows[i].n, mx + 15, 428 + i * 30.0, cDim, 13); g.T(rows[i].val, mx + mw - 15, 428 + i * 30.0, rows[i].c, 15, 2, 700); }
    Trends(g, mx, 666, mw, 119, false);
    // --- the control: five touch scales ---
    Frame(g, xR, 64, 485, 721, L"УПРАВЛЕНИЕ");
    const std::vector<std::pair<double, std::wstring>> t01 = {{0, L"0"}, {0.5, L"50"}, {1, L"100"}};
    TBar(g, kBarMarch, xR + 14, 48, L"МАРШ", cOr, 0, 1, 0.05, 0.01, v.mSet, v.mAct, 0, Pct(v.mAct), L"уст. " + Pct(v.mSet),
         !v.plantRun ? L"УСТ" : limG && v.mSet > v.mAct + 0.005 ? L"ОГР g" : L"", true, t01);
    TBar(g, kBarPods, xR + 108, 48, L"ГОНД", cOr, 0, 1, 0.05, 0.01, v.pSet, v.podsOk ? v.pAct : 0, 0, Pct(v.podsOk ? v.pAct : 0), L"уст. " + Pct(v.pSet),
         !v.podsOk ? L"ЗАКР" : limG && v.pSet > v.pAct + 0.005 ? L"ОГР g" : L"", true, t01);
    TBar(g, kBarNozzle, xR + 202, 48, L"СОПЛА", cBl, 0, 180, 5, 1, v.nozSet, v.nozAct, 0, Fmt(v.nozAct, 0) + L"°",
         v.nozAct < 45 ? L"тяга вперёд" : v.nozAct < 135 ? L"тяга вверх" : L"торможение", L"", true, {{0, L"назад"}, {90, L"вниз"}, {180, L"вперёд"}});
    TBar(g, kBarTvc, xR + 296, 48, L"УВТ", cYe, -v.tvcMax, v.tvcMax, 1, 0.5, v.tvc, v.tvc, 0, Fmt(v.tvc, 1) + L"°", L"АВТО · через ЦМ", L"", false,
         {{-v.tvcMax, L"-" + Fmt(v.tvcMax, 0)}, {0, L"0"}, {v.tvcMax, L"+" + Fmt(v.tvcMax, 0)}});
    TBar(g, kBarGLim, xR + 390, 48, L"ПРЕД. g", cVi, 0, 8, 0.5, 0.5, v.gLim, v.feltG, 0, Fmt(v.feltG, 1) + L" g",
         v.bypass || !v.gLimOn ? L"СНЯТ" : L"предел " + Fmt(v.gLim, 1), L"", true, {{0, L"0"}, {4, L"4"}, {8, L"8"}});
    Btn(g, hits_, L"АВТО", xR + 14, 606, 104, 34, v.massMode < 0, kCmdMassAuto);
    Btn(g, hits_, L"АРГОН", xR + 126, 606, 104, 34, v.massMode == 0, kCmdMassArgon);
    Btn(g, hits_, L"ЖЕЛЕЗО", xR + 238, 606, 104, 34, v.massMode == 1, kCmdMassIron);
    Btn(g, hits_, L"УВТ АВТО", xR + 350, 606, 122, 34, true, -1);
    Btn(g, hits_, L"ОТСЕЧКА", xR + 14, 648, 160, 36, false, kCmdCut, cRd);
    Btn(g, hits_, v.bypass ? L"БЛОК. СНЯТЫ" : v.bypassArmed ? L"ПОДТВЕРДИТЬ" : L"ОБХОД БЛОК.", xR + 182, 648, 160, 36, v.bypass || v.bypassArmed, kCmdBypass, cRd);
    Btn(g, hits_, v.gLimOn ? L"ПРЕДЕЛ g ВКЛ" : L"ПРЕДЕЛ g ВЫКЛ", xR + 350, 648, 122, 36, v.gLimOn, kCmdGLimOnOff);
    g.T(L"БЛОКИРОВКИ", xR + 14, 712, cDim, 11, 0, 700);
    Lamp(g, xR + 20, 730, v.envKind != 2, cGr); g.T(L"в атмосфере только аргон, гондолы — в воздухе", xR + 34, 735, cTx, 12);
    Lamp(g, xR + 20, 752, v.gLimOn && !v.bypass, cGr); g.T(L"предел перегрузки " + Fmt(v.gLim, 1) + L" g", xR + 34, 757, cTx, 12);
    Lamp(g, xR + 20, 774, !v.bypass, v.bypass ? cRd : cGr); g.T(v.bypass ? L"ОБХОД: действует только физика" : L"обход не включён", xR + 34, 779, v.bypass ? cRd : cTx, 12);
}

void Screen::Anamezon(Canvas& g, const View& v, double W, double H) {
    (void)H;
    const double xR = W - 500, mx = 560, mw = xR - 15 - mx;
    auto cX = [&](double x) { return mx + (x - 560) * mw / 525.0; };
    static const wchar_t* const kSt[4] = {L"КАМЕРЫ ВЫКЛЮЧЕНЫ", L"ПОЛЕ КАМЕР", L"ПУЧОК ПОДЖИГА", L"НА РЕЖИМЕ"};
    const bool run = v.stage == 3;
    const bool limHot = v.capHot < 0.999 && !v.bypass, limSafe = v.capSafe < 0.999 && !v.bypass;
    const bool toStar = v.starAU < 50 && v.starAU > 0;
    const std::wstring st = std::wstring(kSt[(std::max)(0, (std::min)(3, v.stage))]) + (v.trans ? L" ›" : L"") + (run && (limHot || limSafe) ? L" · ОГРАНИЧЕНИЕ" : L"");
    const unsigned stc = run ? (limHot || limSafe ? cYe : cGr) : v.stage == 0 ? cDim : cYe;
    g.Disc(548, 32, 9, stc); g.T(st, 564, 38, stc, 15, 0, 700);
    // --- the chambers: the stern (4) and the nose (2) ---
    Frame(g, 15, 64, 530, 316, L"КАМЕРЫ · КОРМА К1–К4 · НОС Н1–Н2");
    const double cx = 150, cy = 214, k = 6.6;
    SternView(g, cx, cy, k, 19);
    const Pt pos[4] = {{-4.2, 5.2}, {4.2, 5.2}, {-4.2, -2}, {4.2, -2}};
    for (int i = 0; i < 4; ++i) Chamber(g, cx + pos[i].x * k, cy - pos[i].y * k, 2.6 * k, v.fieldL, v.fsAct, v.t, (L"К" + Fmt(i + 1, 0)).c_str());
    g.Circle(cx, cy - 1.82 * k, 2.2 * k, 0x2c4a44, 2);
    g.T(L"КОРМА · вид с кормы", cx, 108, cTx, 12, 1, 700); g.T(fF(v.Fs), cx, cy + 92, cWh, 13, 1, 700);
    const double nx = 415, ny = 214, nk = 11;
    g.Ellipse(nx, ny, 7.5 * nk, 6 * nk, 0x16241f, 0x3f6a5f, 2);
    for (int i = 0; i < 2; ++i) Chamber(g, nx + (i ? 3 : -3) * nk, ny, 2.2 * nk, v.fieldL, v.frAct, v.t, i ? L"Н2" : L"Н1");
    g.T(L"НОС · вид с носа", nx, 108, cTx, 12, 1, 700); g.T(fF(v.Fr), nx, cy + 92, cWh, 13, 1, 700);
    g.T(L"поле камер " + Fmt(1000 * v.fieldL, 0) + L" Тл · накопитель " + Pct(v.store) + L" · ретро-чаши: 0,54 тяги камеры, струи вперёд", 30, 368, cDim, 12);
    // --- the jets and the star ---
    Frame(g, 15, 396, 530, 254, L"СТРУИ И СИСТЕМА");
    const double sx = 280, sy = 530;
    const bool ahead = v.starCos > 0;
    const double starX = ahead ? 500 : 60, starY = 470;
    if (toStar) g.Circle(starX, starY, 95, cRd, 1, 0.35);
    g.Disc(starX, starY, 8, 0xe8c860);
    g.T(L"Солнце · " + Fmt(v.starAU, v.starAU < 10 ? 2 : 0) + L" а.е. · " + Fmt(std::acos((std::max)(-1.0, (std::min)(1.0, v.starCos))) * 180 / kPi, 0) + L"° от носа",
        starX, starY - 16, cYe, 11, ahead ? 2 : 0);
    g.Fill(sx - 60, sy - 8, 110, 16, 0x3a4650); g.Shape({{sx + 50, sy - 8}, {sx + 78, sy}, {sx + 50, sy + 8}}, 0x3a4650);
    auto jet = [&](double x0, int dir, double f, bool warn) {
        if (f > 0.001) {
            const double len = 170 * std::sqrt(f), half = std::tan(5 * kPi / 180) * len;
            g.Shape({{x0, sy - 4}, {x0 + dir * len, sy - half - 4}, {x0 + dir * len, sy + half + 4}, {x0, sy + 4}}, Mix(0xb4a0ff, cBg, 0.55));
            g.Shape({{x0, sy - 2}, {x0 + dir * len * 0.5, sy - 2}, {x0 + dir * len * 0.5, sy + 2}, {x0, sy + 2}}, Mix(0xe6d7ff, cBg, 0.9));
        }
        if (warn) { const double xx = x0 + dir * 50; g.Line(xx - 10, sy - 10, xx + 10, sy + 10, cRd, 3); g.Line(xx + 10, sy - 10, xx - 10, sy + 10, cRd, 3); }
    };
    jet(sx - 60, -1, v.fsAct, toStar && !ahead && v.fsAct > 0.001);
    jet(sx + 78, 1, v.frAct, toStar && ahead && v.frAct > 0.001);
    const double Fn = v.Fs - v.Fr;
    if (std::fabs(Fn) > 1) g.Arrow(sx, sy + 34, sx + (std::max)(-9.0, (std::min)(9.0, Fn / 1e10)) * 12, sy + 34, Fn >= 0 ? cWh : cOr, 3);
    g.T(Fn > 1 ? L"разгон" : Fn < -1 ? L"торможение (реверс)" : L"", sx, sy + 58, Fn >= 0 ? cWh : cOr, 12, 1);
    g.T(toStar ? L"ВНИМАНИЕ: внутри системы — струя не должна идти к звезде" : L"струи: частицы " + Fmt(v.vJet / kC, 2) + L" c, конус 5° · корма ← · нос →", 30, 638, toStar ? cYe : cDim, 12);
    JournalBox(g, 15, 666, 530, 119);
    // --- the output ---
    Frame(g, mx, 64, mw, 316, L"ВЫХОД КАМЕР");
    const double cols[6] = {cX(575), cX(622), cX(692), cX(778), cX(902), cX(1000)};
    const wchar_t* hd[6] = {L"", L"подача", L"капсулы", L"капсула", L"тяга", L"поле"};
    for (int i = 0; i < 6; ++i) g.T(hd[i], cols[i], 92, cDim, 12, 0, 700);
    auto row = [&](double y, const std::wstring& n, double f, double Fv, unsigned col) {
        const double rate = v.pelletRate * f, pm = v.pelletMass;
        g.T(n, cols[0], y, col, 13, 0, 700); g.T(Pct(f), cols[1], y, cTx, 13); g.T(Fmt(rate / 1e3, 1) + L" кГц", cols[2], y, cTx, 13);
        g.T(f > 0.001 ? Fmt(pm * 1e3, 1) + L" г · " + Fmt(pm * 0.8 * kC * kC / 4.184e12, 0) + L" кт" : L"—", cols[3], y, cTx, 13);
        g.T(fF(Fv), cols[4], y, cWh, 13); g.T(Fmt(1000 * v.fieldL, 0) + L" Тл", cols[5], y, cVi, 13);
    };
    for (int i = 0; i < 4; ++i) row(116 + i * 22, L"К" + Fmt(i + 1, 0), v.fsAct, v.chamberF * v.fsAct, cTx);
    for (int i = 0; i < 2; ++i) row(212 + i * 22, L"Н" + Fmt(i + 1, 0), v.frAct, v.retroF * v.frAct, cOr);
    g.Line(cols[0], 250, mx + mw - 15, 250, cFr, 1);
    g.T(L"корма — вперёд", cols[0], 270, cDim, 13); g.T(fF(v.Fs), cols[4], 270, cWh, 13, 0, 700);
    g.T(L"реверс — назад", cols[0], 290, cDim, 13); g.T(v.Fr > 0 ? L"−" + fF(v.Fr) : L"0", cols[4], 290, cOr, 13, 0, 700);
    g.T(L"СУММА ПО ОСИ", cols[0], 312, cYe, 13, 0, 700); g.T((Fn < 0 ? L"−" : L"") + fF(Fn), cols[4], 312, cYe, 14, 0, 700);
    g.T(L"истечение: частицы " + Fmt(v.vJet / kC, 2) + L" c · эффективная " + Fmt(v.vEff / kC, 3) + L" c (" + Fmt(v.vEff / 1e3, 0) + L" км/с)", cols[0], 340, cTx, 12);
    g.T(L"мощность струй " + fW(v.Pjet) + L" · расход анамезона " + fM(v.mdotA) + L" (" + Fmt(v.mdotA * 3.6, 1) + L" т/ч)", cols[0], 362, cTx, 12);
    // --- the reckoning ---
    Frame(g, mx, 396, mw, 254, L"РАСЧЁТ");
    const double a = Fn / (std::max)(1.0, v.massKg), rap = std::atanh((std::max)(-0.999999, (std::min)(0.999999, v.beta)));
    double fuel = 0; for (int i = 0; i < 4; ++i) fuel += v.traps[i];
    const double rapLeft = v.vEff / kC * std::log(v.massKg / (std::max)(1.0, v.massKg - fuel));
    const bool along = std::fabs(rap) < 1e-9 || (a >= 0) == (rap >= 0);
    const double stopT = !along && std::fabs(a) > 0 ? std::fabs(rap) * kC / std::fabs(a) : 1e300;
    const double vMax = std::tanh(std::fabs(rap) + rapLeft), vBrake = std::tanh((std::fabs(rap) + rapLeft) / 2);
    struct R { const wchar_t* n; std::wstring val; unsigned c; };
    const R rows[7] = {
        {L"ускорение по оси", std::wstring(a >= 0 ? L"+" : L"−") + Fmt(std::fabs(a) / kG0, 1) + L" g · " + (std::fabs(a) < 1e-6 ? L"—" : along ? L"разгон" : L"торможение"), cTx},
        {L"компенсатор гасит · ощущается", Fmt(v.compAcc / kG0, 1) + L" g · " + Fmt(v.residG, 2) + L" g", v.residG > 10 ? cRd : v.residG > v.gLim ? cYe : cGr},
        {L"скорость", std::wstring(v.beta < 0 ? L"−" : L"") + Fmt(std::fabs(v.beta), 4) + L" c · " + Fmt(std::fabs(v.beta) * kC / 1e3, 0) + L" км/с", cTx},
        {L"γ · время корабля / Земли", Fmt(v.gamma, 3) + L" · " + Fmt(1 / v.gamma, 3), cTx},
        {L"анамезон в ловушках", Fmt(fuel / 1e6, 2) + L" кт · активна " + Fmt(v.activeTrap + 1, 0), fuel <= 0 ? cYe : cTx},
        {along ? L"до пустых ловушек" : L"до остановки", along ? (v.mdotA > 0 ? fT(fuel / v.mdotA) : L"—") : fT(stopT), cTx},
        {L"предел: разгон / с торможением", Fmt(vMax, 3) + L" c / " + Fmt(vBrake, 3) + L" c", vBrake >= 0.42 ? cGr : cYe}};
    for (int i = 0; i < 7; ++i) { g.T(rows[i].n, mx + 15, 428 + i * 30.0, cDim, 13); g.T(rows[i].val, mx + mw - 15, 428 + i * 30.0, rows[i].c, 15, 2, 700); }
    Trends(g, mx, 666, mw, 119, true);
    // --- the control ---
    Frame(g, xR, 64, 485, 721, L"УПРАВЛЕНИЕ");
    const std::vector<std::pair<double, std::wstring>> t01 = {{0, L"0"}, {0.5, L"50"}, {1, L"100"}};
    const std::wstring limTxt = limSafe ? L"ЛЮДИ" : limHot ? L"АТМ" : L"";
    TBar(g, kBarFeed, xR + 24, 64, L"КОРМА", cVi, 0, 1, 0.05, 0.01, v.fsSet, v.fsAct, 0, Pct(v.fsAct), L"уст. " + Pct(v.fsSet),
         !run && v.fsSet > 0 ? L"НЕ ГОТОВ" : limTxt, true, t01);
    TBar(g, kBarRetro, xR + 178, 64, L"РЕВЕРС", cOr, 0, 1, 0.05, 0.01, v.frSet, v.frAct, 0, Pct(v.frAct), L"уст. " + Pct(v.frSet),
         !run && v.frSet > 0 ? L"НЕ ГОТОВ" : limTxt, true, t01);
    TBar(g, kBarResid, xR + 332, 64, L"ОЩУЩ. g", cGr, 0, 10, 0.5, 0.5, v.gLim, v.residG, 0, Fmt(v.residG, 2) + L" g", L"предел " + Fmt(v.gLim, 1) + L" g", L"", true,
         {{0, L"0"}, {5, L"5"}, {10, L"10"}});
    Btn(g, hits_, v.stage == 0 ? L"ПУСК КАМЕР" : L"СТОП КАМЕР", xR + 14, 606, 172, 36, v.stage != 0, kCmdIgnition, v.stage == 0 ? cGr : cRd);
    Btn(g, hits_, L"КОМПЕНСАТОР", xR + 194, 606, 150, 36, true, -1, cGr);
    Btn(g, hits_, L"ЛОВУШКА ›", xR + 352, 606, 120, 36, false, kCmdTrap);
    Btn(g, hits_, L"ОТСЕЧКА", xR + 14, 648, 172, 36, false, kCmdCut, cRd);
    Btn(g, hits_, v.bypass ? L"БЛОК. СНЯТЫ" : v.bypassArmed ? L"ПОДТВЕРДИТЬ" : L"ОБХОД БЛОК.", xR + 194, 648, 150, 36, v.bypass || v.bypassArmed, kCmdBypass, cRd);
    Btn(g, hits_, v.gLimOn ? L"ПРЕДЕЛ g ВКЛ" : L"ПРЕДЕЛ g ВЫКЛ", xR + 352, 648, 120, 36, v.gLimOn, kCmdGLimOnOff);
    // the start handle: field -> beam -> feed (each key sets the handle to its position)
    g.T(L"ПУСК ПО ФАЗАМ", xR + 14, 708, cDim, 11, 0, 700);
    const wchar_t* ph[3] = {L"ПОЛЕ", L"ПУЧОК", L"ПОДАЧА"};
    const double lv[3] = {v.fieldL, v.beamL, v.feedL};
    for (int i = 0; i < 3; ++i) {
        const double bx = xR + 14 + i * 154;
        Btn(g, hits_, ph[i], bx, 716, 146, 40, v.stage >= i + 1, kCmdPhase1 + i, cVi);
        ui::Bar(g, bx, 762, 146, 8, lv[i], i == 2 ? cOr : cGr);
    }
}

}  // namespace tantra::enginescreen
