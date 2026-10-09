// TantraMechScreen: see TantraMechScreen.h. DrawTop() follows drawTop() of Tantra_Design/refine/mech_v3.html block by block
// (its coordinates and sizes; the blocks' title bands are the front kit's 36 px); DrawPult() is the v2 pult's drawLow()
// (Tantra_Design/tantra_mech_screen.html, the old canvas).
#include "TantraMechScreen.h"
#include "TantraFrontScreen.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace tantra::mechscreen {

using namespace tantra::scr;
using namespace tantra::scr::ui;
namespace fr = tantra::front;

namespace {

// core/Carriage.cpp: s a phase, s the load passing to the stern legs (4-5), s the wings' and the fin's fold around a move
constexpr double kPhaseTime = 20.0, kLoadTime = 8.0, kTuckTime = 8.0;
// mech_v3's palette (COL): the front kit's, and the glass of a key, the hull, the leg tracks, the n/a grey, the bars' well
constexpr unsigned kBg = fr::kBg, kFr = fr::kFr, kTx = fr::kTx, kT2 = fr::kDim, kLn = fr::kLn, kOr = fr::kOr, kYe = fr::kYe,
                   kRd = fr::kRd, kGr = fr::kGr, kWh = fr::kWh, kBl = fr::kBl, kGlass = 0x101c1a, kHull = 0x16241f,
                   kHullLn = 0x3f6a5f, kTrack = 0x1c302b, kLeg = 0x5b8cc8, kDis = 0x4b5754, kWell = 0x0d1715;
// the side view's ship (MeshLayout / CarriageGeometry): the kangaroo's hip and foot, the blades' parked station, the stern
// legs' hinge; the rear view: the wing root, the panels (kWingX0, kWingY, kWingB1, kWingB - kWingB1), the hull's top
constexpr double kKangHipS = 99.4, kKangHipY = -5.78, kKangFootFwd = 12.0, kTrackS0 = 53.4, kSternHingeS = 30.0;
constexpr double kWingX0 = 9.4, kWingY = 1.82, kWingB1 = 5.5, kWingB2 = 10.5, kHullTop = 10.85;

const wchar_t* const kPhName[8] = {L"лежит на лопастях и передней опоре", L"подъём на лопастях", L"цапфы под ЦМ, передняя опора в карман", L"поворот вокруг цапф",
                                   L"кормовые ноги выходят", L"нагрузка на корму", L"лопасти убираются", L"стоит на корме"};
const wchar_t* const kPhShort[6] = {L"подъём", L"цапфы", L"поворот", L"лапы", L"нагрузка", L"сбор"};
const wchar_t* const kWingTxt[3] = {L"развёрнуты 90°", L"подняты 30°", L"сложены"};
struct Pos { const wchar_t* t; const wchar_t* sub; };
const Pos kPos[5] = {{L"В ГОРИЗОНТ", L"лёжа на лопастях"}, {L"НА ТРЁХ", L"ось 32 м, на лопастях и передней опоре"}, {L"НА НОГИ", L"подъём до верхней точки"}, {L"75°", L"стела: подъём и поворот"},
                     {L"ВЗЛЁТНОЕ", L"на 4 кормовых ногах"}};
const wchar_t* const kLegName[7] = {L"Л", L"П", L"К1", L"К2", L"К3", L"К4", L"ПО"};
const int kLegRow[7] = {0, 1, 6, 2, 3, 4, 5};   // the table's order: the blades, the kangaroo, the stern legs

double Clamp01(double u) { return u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u; }
double Ease(double u) { u = Clamp01(u); return u * u * u * (10 + u * (-15 + 6 * u)); }
double EaseInv(double y) { double lo = 0, hi = 1; for (int i = 0; i < 40; ++i) { const double m = (lo + hi) / 2; if (Ease(m) < y) lo = m; else hi = m; } return (lo + hi) / 2; }
bool Near(double a, double b) { return std::fabs(a - b) < 2e-3; }
int PosAt(double P, double tp) { for (int i = 0; i < kPosCount; ++i) if (Near(P, PositionP(i, tp))) return i; return -1; }
std::wstring Pct(double f) { return fr::Num(f * 100, 0) + L" %"; }
unsigned RCol(double f) { return f > 1 ? kRd : f > 0.85 ? kYe : kGr; }   // a share of the rating (mech_v3 ratioCol)
std::pair<std::wstring, std::wstring> FmtW(double W) {
    if (W < 1e6) return {fr::Num(W / 1e3, W < 1e4 ? 1 : 0), L"кВт"};
    if (W < 1e9) return {fr::Num(W / 1e6, W < 1e7 ? 1 : 0), L"МВт"};
    return {fr::Num(W / 1e9, 2), L"ГВт"};
}

// the time to the target position as core/Carriage.cpp runs it: the wings' fold first (its tuck runs linearly, the pose shows
// it eased), then the phases (the turn the ship's own, the load 8 s, the rest 20 s), all at the drives' rate
double PhaseTime(int k, double turn) { return k == 2 ? turn : k == 4 ? kLoadTime : kPhaseTime; }
double TimeLeft(const View& v) {
    double t = v.P != v.PT ? (1.0 - EaseInv(Clamp01(v.wingFold))) * kTuckTime : 0.0, p = v.P;
    if (v.PT > p) while (p < v.PT - 1e-9) { const int k = int(std::floor(p + 1e-9)); const double e = (std::min)(v.PT, k + 1.0); t += (e - p) * PhaseTime(k, v.turnTime); p = e; }
    else while (p > v.PT + 1e-9) { const int k = int(std::ceil(p - 1e-9)) - 1; const double e = (std::max)(v.PT, double(k)); t += (p - e) * PhaseTime(k, v.turnTime); p = e; }
    return t / (std::max)(0.1, v.driveRate);
}

// the screen's pad: the front kit's Pad in the mockup's px, moved to where the screen sits on the surface
class G {
public:
    G(fr::Pad& p, double ox, double oy) : p_(p), ox_(ox), oy_(oy) {}
    void Fill(double x, double y, double w, double h, unsigned c) { p_.Fill(x + ox_, y + oy_, w, h, c); }
    void Stroke(double x, double y, double w, double h, unsigned c, double lw) { p_.Stroke(x + ox_, y + oy_, w, h, c, lw); }
    void Line(double x0, double y0, double x1, double y1, unsigned c, double lw) { p_.Line(x0 + ox_, y0 + oy_, x1 + ox_, y1 + oy_, c, lw); }
    void Dashed(double x0, double y0, double x1, double y1, unsigned c, double lw, double on, double off) {
        const double L = std::hypot(x1 - x0, y1 - y0);
        if (L < 0.5) return;
        const double ux = (x1 - x0) / L, uy = (y1 - y0) / L;
        for (double s = 0; s < L; s += on + off) { const double e = (std::min)(L, s + on); Line(x0 + ux * s, y0 + uy * s, x0 + ux * e, y0 + uy * e, c, lw); }
    }
    void Poly(std::vector<fr::P2> p, unsigned fill, unsigned edge = fr::kNone, double lw = 0.0) { for (auto& q : p) { q.x += ox_; q.y += oy_; } p_.Poly(p, fill, edge, lw); }
    void PolyLine(std::vector<fr::P2> p, unsigned c, double lw) { for (auto& q : p) { q.x += ox_; q.y += oy_; } p_.PolyLine(p, c, lw); }
    void Disc(double x, double y, double r, unsigned c) { p_.Disc(x + ox_, y + oy_, r, c); }
    void Ring(double x, double y, double r, unsigned c, double lw) { p_.Ring(x + ox_, y + oy_, r, c, lw); }
    void Arc(double x, double y, double r, double a0, double a1, unsigned c, double lw) { p_.Arc(x + ox_, y + oy_, r, a0, a1, c, lw); }
    void RRect(double x, double y, double w, double h, double r, unsigned fill, unsigned edge, double lw) { p_.RRect(x + ox_, y + oy_, w, h, r, fill, edge, lw); }
    double T(const std::wstring& s, double x, double y, unsigned c, double size = 17, int align = 0, bool bold = false) { return p_.T(s, x + ox_, y + oy_, c, size, align, bold); }
    double TS(const std::wstring& s, double x, double y, unsigned c, double size, bool bold, double sp, int align = 0) { return p_.TS(s, x + ox_, y + oy_, c, size, bold, sp, align); }
    void Block(double x, double y, double w, double h, const std::wstring& title) { p_.Block(x + ox_, y + oy_, w, h, title); }
    // text into mw: down to 15 px, then cut (mech_v3 TF)
    int TF(const std::wstring& s, double x, double y, unsigned c, double mw, int size = 17, int align = 0, bool bold = false) {
        const int sz = fr::Pad::FitSize(s, mw, size, 15);
        T(fr::Pad::FitTxt(s, mw, sz), x, y, c, sz, align, bold);
        return sz;
    }

private:
    fr::Pad& p_;
    double ox_, oy_;
};

double Cap(double size) { return fr::Pad::CapH(size); }

// THE key module of mech_v3: off / on (filled) / warn (red frame) / n/a (grey, no frame); one or two label lines (18 px bold),
// an optional 15 px sub line, centred by the cap height. A n/a key still answers (its command says why it cannot)
enum KeySt { kOff, kOn, kWarn, kNa };
void Key(G& g, std::vector<Hit>& hits, const std::wstring& label, double x, double y, double w, double h, int st, int cmd, unsigned col,
         const std::wstring& sub = L"", int size = 18) {
    unsigned fill, edge = fr::kNone, lc, sc;
    double bw = 1.5;
    if (st == kOn) { fill = col; edge = col; lc = kBg; sc = Mix(kBg, col, 0.78); }
    else if (st == kNa) { fill = 0x111817; lc = sc = kDis; }
    else { fill = kGlass; edge = Mix(col, kBg, 0.45); lc = col; sc = kT2; }
    if (st == kWarn) { edge = kRd; bw = 2.0; }
    g.RRect(x + bw / 2, y + bw / 2, w - bw, h - bw, 4, fill, edge, edge == fr::kNone ? 0.0 : bw);
    const double maxW = w - 16;
    std::vector<std::wstring> lines{label};
    int sz = size;
    if (fr::Pad::TW(label, sz) > maxW && sub.empty() && label.find(L' ') != std::wstring::npos) {
        double best = 1e9; size_t at = 0;
        for (size_t i = 0; i < label.size(); ++i)
            if (label[i] == L' ') { const double m = (std::max)(fr::Pad::TW(label.substr(0, i), sz), fr::Pad::TW(label.substr(i + 1), sz)); if (m < best) { best = m; at = i; } }
        lines = {label.substr(0, at), label.substr(at + 1)};
    }
    auto widest = [&](int s) { double m = 0; for (const auto& l : lines) m = (std::max)(m, fr::Pad::TW(l, s)); return m; };
    while (sz > 15 && widest(sz) > maxW) --sz;
    const double cL = Cap(sz), cS = Cap(15), gapL = std::round(cL * 0.6), gapS = 8;
    const double total = lines.size() * cL + (lines.size() - 1) * gapL + (sub.empty() ? 0.0 : gapS + cS);
    double yb = y + (h - total) / 2 + cL;
    for (size_t i = 0; i < lines.size(); ++i) {
        g.T(fr::Pad::FitTxt(lines[i], maxW, sz), x + w / 2, yb, lc, sz, 1, true);
        if (i + 1 < lines.size()) yb += cL + gapL;
    }
    if (!sub.empty()) g.TF(sub, x + w / 2, yb + gapS + cS, sc, w - 12, 15, 1, false);
    if (cmd >= 0) hits.push_back({x, y, w, h, cmd});
}
// a block with the state at the right of its title band
void BlockR(G& g, double x, double y, double w, double h, const std::wstring& title, const std::wstring& right = L"", unsigned rcol = kTx) {
    g.Block(x, y, w, h, title);
    if (right.empty()) return;
    const double tw = (std::min)(fr::Pad::TW(title, 18) + title.size() - 1.0, w - 32);
    g.TF(right, x + w - 16, fr::Pad::BandBase(y, 17), rcol, w - 32 - tw - 24, 17, 2, true);
}
void LampM(G& g, double x, double y, bool on, unsigned col = kGr, double r = 6) { g.Disc(x, y, r, on ? col : 0x1c2a27); g.Ring(x, y, r, 0x2e4d45, 1); }
// a number in its column: right-aligned, the unit in its own smaller column
void NumU(G& g, const std::wstring& v, double xr, double y, unsigned col, double size, const std::wstring& unit = L"", unsigned ucol = kT2) {
    g.T(v, xr, y, col, size, 2, true);
    if (!unit.empty()) g.T(unit, xr + 6, y, ucol, 15);
}

// the side view on the carriage's pose: the blade feet stay put (x 0), the hull pitches with the trunnions; the ground, the
// blades hanging from their hips, the kangaroo, the stern legs, the CG
void ShipThumb(G& g, double x0, double y0, double w, double h, double sc, const View& v) {
    const double ox = x0 + w * 0.3, oy = y0 + h - 8, ca = std::cos(v.theta), sa = std::sin(v.theta);
    const double hipH = v.trunnionH + (v.hipS - v.sCG) * sa;   // the trunnions over the ground (trunnionH is the CG's)
    auto P = [&](double x, double y) { return fr::P2{ox + x * sc, oy - y * sc}; };
    auto H2W = [&](double s, double z) { return P(v.sway + (s - v.hipS) * ca - z * sa, hipH + (s - v.hipS) * sa + z * ca); };
    if (v.grounded) { g.Fill(x0 + 2, oy, w - 4, 6, 0x1a2420); g.Line(x0 + 2, oy, x0 + w - 2, oy, 0x5a5038, 1); }
    const double fb = (std::max)(v.legOut[0], v.legOut[1]);
    if (fb > 0.05) { const fr::P2 hp = H2W(v.hipS, 0); g.Line(hp.x, hp.y, hp.x, hp.y + v.mastLen * sc, Mix(kLeg, kBg, (std::max)(0.15, fb)), 2.5); }
    std::vector<fr::P2> hull;
    for (const fr::P2& q : {fr::P2{0, 7}, fr::P2{120, 7}, fr::P2{178, 0}, fr::P2{140, -7.5}, fr::P2{0, -7.5}}) hull.push_back(H2W(q.x, q.y));
    g.Poly(hull, 0x3a4a52, 0x6a8090, 1);
    const fr::P2 cg = H2W(v.sCG, 0); g.Ring(cg.x, cg.y, 3, kRd, 1.5);
    const double fk = v.legOut[6];
    if (fk > 0.02) {
        const fr::P2 kh = H2W(kKangHipS, kKangHipY), kf = P(kKangHipS + kKangFootFwd - kTrackS0, 0);   // its foot stays planted
        g.Line(kh.x, kh.y, kh.x + (kf.x - kh.x) * fk, kh.y + (kf.y - kh.y) * fk, Mix(0x7a8db0, kBg, (std::max)(0.15, fk)), 2);
    }
    const double fs = Ease(v.legOut[2]);   // Carriage::BuildPose legStand
    if (fs > 0.01) {
        const fr::P2 spt = H2W(0, 0);
        for (int s : {-1, 1}) { const fr::P2 hg = H2W(kSternHingeS, -9.0 * s), ft{spt.x + s * 27 * sc, oy}; g.Line(hg.x, hg.y, hg.x + (ft.x - hg.x) * fs, hg.y + (ft.y - hg.y) * fs, kLeg, 2); }
    }
}

// one lit position scale under a row of keys: piecewise, each position's mark right under its key
void PosBar(G& g, const double kc[5], const View& v, double y, double h, double keyBottom) {
    double pv[5];
    for (int i = 0; i < 5; ++i) pv[i] = PositionP(i, v.tripodP);
    auto X = [&](double P) {
        if (P <= pv[0]) return kc[0];
        for (int k = 0; k < 4; ++k) if (P <= pv[k + 1]) return pv[k + 1] - pv[k] < 1e-6 ? kc[k + 1] : kc[k] + (P - pv[k]) / (pv[k + 1] - pv[k]) * (kc[k + 1] - kc[k]);
        return kc[4];
    };
    const double x0 = kc[0], x1 = kc[4];
    const bool moving = v.P != v.PT && !v.held;
    for (int i = 0; i < 5; ++i) {
        const bool here = Near(v.P, pv[i]) && v.P == v.PT, target = Near(v.PT, pv[i]) && v.P != v.PT;
        g.Line(kc[i], keyBottom, kc[i], y, here ? kGr : target ? kYe : 0x3a4a44, 2);
    }
    g.Fill(x0, y, x1 - x0, h, kWell); g.Stroke(x0, y, x1 - x0, h, kFr, 1);
    const unsigned lit = v.estop ? kRd : moving ? Mix(kYe, kBg, 0.72 + 0.28 * std::sin(v.t * 6)) : kGr;
    if (X(v.P) - x0 > 0.5) g.Fill(x0, y + 2, X(v.P) - x0, h - 4, lit);
    if (moving) {
        const double a = (std::min)(X(v.P), X(v.PT)), b = (std::max)(X(v.P), X(v.PT));
        g.Dashed(a, y + 1.5, b, y + 1.5, kYe, 1, 6, 5); g.Dashed(a, y + h - 1.5, b, y + h - 1.5, kYe, 1, 6, 5);
        g.Line(a, y + 1.5, a, y + h - 1.5, kYe, 1); g.Line(b, y + 1.5, b, y + h - 1.5, kYe, 1);
    }
    for (int i = 0; i < 5; ++i) {
        const bool here = Near(v.P, pv[i]), target = Near(v.PT, pv[i]) && moving;
        g.Disc(kc[i], y + h / 2, 5, here ? kGr : target ? kYe : kOr); g.Ring(kc[i], y + h / 2, 5, kBg, 1.5);
    }
}

// a support's state in the table (mech_v3 legInfo with the damage model and the feet)
struct LegSt { std::wstring w; unsigned c; bool bold; };
LegSt LegState(const View& v, int i) {
    const double f = v.legOut[i], R = v.legR[i];
    if (v.legInteg[i] <= 0.0) return {L"ПОТЕРЯНА", kRd, true};
    if (v.legBroken[i]) return {L"СЛОМАНА", kRd, true};
    if (f > 0.002 && f < 0.998) return {(v.legDir[i] > 0 ? L"выходит " : v.legDir[i] < 0 ? L"убирается " : L"выход ") + Pct(f), kYe, false};
    const bool contact = f >= 0.998 && v.grounded && (v.legN[i] > 0.0 || v.legTouch[i]);
    if (contact && R > 1.0) return {L"ПЕРЕГРУЗКА", kRd, true};
    if (v.legInteg[i] < 1.0) return {L"повреждена " + Pct(v.legInteg[i]), kYe, false};
    if (f <= 0.002) return {L"убрана", kT2, false};
    if (contact) return R >= 0.05 ? LegSt{L"под нагрузкой", kGr, true} : LegSt{L"касание", kBl, false};
    return {L"выпущена", kTx, false};
}

}  // namespace

double PositionP(int i, double tripodP) { static const double p75 = 2.0 + EaseInv(75.0 / 90.0); return i <= 0 ? 0.0 : i == 1 ? tripodP : i == 2 ? 1.0 : i == 3 ? p75 : 6.0; }

double LegOut(int leg, double P, double gear, bool port) {
    if (leg <= 1) return P <= 0 ? gear : P >= 6 ? 0.0 : 1.0 - Clamp01(P - 5.0);
    if (leg == 6) return P <= 0 ? gear : P >= 6 ? 0.0 : P < 1.5 ? 1.0 : 1.0 - Ease((P - 1.5) * 2.0);
    if (port) return 0.0;
    return P >= 6 ? gear : P <= 0 ? 0.0 : Clamp01(P - 3.0);
}

void Screen::Watch(const View& v) {
    if (!seen_) {
        seen_ = true; last_ = v;
        const int at = PosAt(v.P, v.tripodP);
        log_.Add(v.t, at >= 0 ? std::wstring(L"Положение «") + kPos[at].t + L"»" : L"Лафет между положениями", 0);
        return;
    }
    const View& o = last_;
    const bool movNow = v.P != v.PT, movWas = o.P != o.PT;
    if (v.PT != o.PT && movNow) {
        const int to = PosAt(v.PT, v.tripodP);
        log_.Add(v.t, std::wstring(L"«") + (to >= 0 ? kPos[to].t : L"?") + L"»: " + (v.PT > v.P ? L"подъём" : L"опускание"), 0);
    }
    if (movWas && !movNow) {
        const int at = PosAt(v.P, v.tripodP);
        log_.Add(v.t, at >= 0 ? std::wstring(L"Положение «") + kPos[at].t + L"»" : L"Лафет остановлен между положениями", at >= 0 ? 0 : 1);
    }
    if (v.estop != o.estop) log_.Add(v.t, v.estop ? L"АВАРИЙНЫЙ СТОП: приводы заперты" : L"Аварийный стоп снят", v.estop ? 2 : 0);
    if (v.balHold && !o.balHold) log_.Add(v.t, L"Раскачка: подъём на паузе (баланс)", 1);
    if (v.gearDown != o.gearDown) log_.Add(v.t, v.gearDown ? L"Шасси выпускается" : L"Шасси убирается", 0);
    if (v.wingMode != o.wingMode) log_.Add(v.t, std::wstring(L"Крылья: ") + kWingTxt[(std::max)(0, (std::min)(2, v.wingMode))], 0);
    if (v.radOut != o.radOut) log_.Add(v.t, v.radOut ? L"Радиаторы выставлены" : L"Радиаторы сложены: сброс 10 %", v.radOut ? 0 : 1);
    for (int i = 0; i < 7; ++i)
        if ((v.legInteg[i] <= 0.0 || v.legBroken[i]) && !(o.legInteg[i] <= 0.0 || o.legBroken[i]))
            log_.Add(v.t, std::wstring(L"Опора ") + kLegName[i] + (v.legInteg[i] <= 0.0 ? L" потеряна" : L" сломана"), 2);
    if ((v.hangarT > 0.5) != (o.hangarT > 0.5)) log_.Add(v.t, v.hangarT > 0.5 ? L"Ангар открывается" : L"Ангар закрывается", 0);
    if (v.port != o.port) log_.Add(v.t, v.port ? L"Стоянка: порт (стол)" : L"Стоянка: грунт", 0);
    if (v.liftStowed != o.liftStowed) log_.Add(v.t, v.liftStowed ? L"Лифт шлюза сложен" : L"Лифт шлюза выходит", 0);
    last_ = v;
}

void Screen::DrawTop(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v) {
    hitsTop_.clear();
    if (!skp) return;
    (void)h;
    Watch(v);
    const double k = w > 0 ? w / 1600.0 : 1.0;
    kTop_ = k;
    fr::Pad pad(skp, font, gost_, k, nullptr);
    G g(pad, ox / k, oy / k);
    const double W = 1600, H = 800;
    const bool moving = v.P != v.PT && !v.held;
    const double tl = TimeLeft(v);
    // the wings' and the fin's motion: from what they were at the last draw (kept while the sim stands)
    const double inner = v.wingInMax > 0 ? v.wingInDeg / v.wingInMax : 0.0, outer = v.wingOutMax > 0 ? v.wingOutDeg / v.wingOutMax : 0.0;
    const double fold = (inner + outer) / 2;
    if (v.t > tPrev_ + 1e-6) {
        if (foldPrev_ >= 0.0) {
            const double df = fold - foldPrev_, dn = v.finOut - finPrev_;
            foldDir_ = std::fabs(df) > 1e-5 ? (df > 0 ? 1 : -1) : 0;
            finDir_ = std::fabs(dn) > 1e-4 ? (dn > 0 ? 1 : -1) : 0;
        }
        foldPrev_ = fold; finPrev_ = v.finOut; tPrev_ = v.t;
    }
    g.Fill(0, 0, W, H, kBg); g.Stroke(9, 9, W - 18, H - 18, kFr, 2);

    // ---- the title bar: the tabs and the state ----
    Key(g, hitsTop_, L"МЕХАНИЗАЦИЯ", 24, 24, 180, 48, kOn, kCmdTabMech, kTx);
    Key(g, hitsTop_, L"ТЕПЛО", 214, 24, 120, 48, v.hotSkin ? kWarn : kOff, kCmdTabThermal, v.hotSkin ? kRd : kOr);
    const int at = PosAt(v.P, v.tripodP), to = PosAt(v.PT, v.tripodP);
    std::wstring st; unsigned stCol;
    if (v.estop) { st = L"АВАРИЙНЫЙ СТОП · ПРИВОДЫ ЗАПЕРТЫ"; stCol = kRd; }
    else if (!v.grounded) { st = std::wstring(L"В ПОЛЁТЕ · ПОСАДКА ") + (v.setStand ? L"НА КОРМУ" : L"ЛЁЖА"); stCol = kTx; }
    else if (moving) { st = std::wstring(v.PT > v.P ? L"ПОДЪЁМ" : L"ОПУСКАНИЕ") + L" → «" + (to >= 0 ? kPos[to].t : L"?") + L"» · ОСТАЛОСЬ " + fr::Num(tl, 0) + L" с"; stCol = kYe; }
    else if (v.P != v.PT) { st = v.balHold ? L"ЛАФЕТ НА ПАУЗЕ · РАСКАЧКА" : L"ЛАФЕТ НА ПАУЗЕ"; stCol = kYe; }
    else if (at >= 0) { st = std::wstring(L"ПОЛОЖЕНИЕ «") + kPos[at].t + L"»"; stCol = kGr; }
    else { st = L"ЛАФЕТ ОСТАНОВЛЕН МЕЖДУ ПОЛОЖЕНИЯМИ"; stCol = kYe; }
    g.Disc(368, 48, 9, stCol);
    g.TF(st, 388, 48 + Cap(24) / 2, stCol, 1316 - 388, 24, 0, true);
    // the emergency stop: over the position keys, at the title bar's right (the v2 pult had it; no shelf draws that now)
    Key(g, hitsTop_, L"АВАРИЙНЫЙ СТОП", 1336, 24, 240, 48, v.estop ? kOn : kOff, kCmdEstop, kRd);

    // ---- row 1 left: the hull's pose ----
    BlockR(g, 24, 88, 844, 168, L"ПОЛОЖЕНИЕ КОРПУСА");
    ShipThumb(g, 40, 124, 280, 124, 0.5, v);
    {
        const bool blades = (std::max)(v.legOut[0], v.legOut[1]) > 0.002;
        const double hipH = v.trunnionH + (v.hipS - v.sCG) * std::sin(v.theta);
        struct Val { const wchar_t* n; std::wstring v, u; unsigned c; };
        const Val vals[6] = {{L"угол корпуса", fr::Num(v.theta * 180 / kPi, 0) + L"°", L"", kTx}, {L"ЦМ от цапф", fr::Num(v.cgRes, 2), L"м", kTx},
                             {L"цапфы над грунтом", blades && v.grounded ? fr::Num(hipH, 1) : L"—", L"м", kTx}, {L"раскачка", fr::Num(std::fabs(v.sway) * 100, 0), L"см", kTx},
                             {L"колонна", blades ? fr::Num(v.mastLen, 1) : L"—", L"м", kTx},
                             {L"привод цапф", fr::Num(v.driveMoment / 1e6, 0), L"МН·м", v.driveMoment > 1.5e9 ? kYe : kTx}};
        for (int i = 0; i < 6; ++i) {
            const int c = i % 2; const double y = 146 + (i / 2) * 32, xl = c ? 616 : 344, xr = c ? 800 : 560;
            g.TF(vals[i].n, xl, y, kT2, xr - xl - 70, 17);
            NumU(g, vals[i].v, xr, y, vals[i].v == L"—" ? kDis : vals[i].c, 22, vals[i].u);
        }
        const int ph = v.P >= 6 ? 7 : v.P <= 0 ? 0 : (std::min)(6, int(std::floor(v.P - 1e-9)) + 1);
        g.TF(L"фаза " + fr::Num(v.P, 2) + L" из 6 · " + (v.grounded ? kPhName[ph] : L"в полёте"), 344, 242, kTx, 852 - 344, 18);
    }

    // ---- row 1 right: the position keys on one lit scale ----
    BlockR(g, 884, 88, 692, 168, L"ПОЛОЖЕНИЯ");
    {
        double kc[5];
        const bool avail = v.grounded && v.gear >= 1 && !v.estop;
        for (int i = 0; i < kPosCount; ++i) {
            const double P = PositionP(i, v.tripodP);
            const bool here = Near(v.P, P) && v.P == v.PT, target = Near(v.PT, P) && v.P != v.PT;
            kc[i] = 900 + i * 110 + 50;
            Key(g, hitsTop_, kPos[i].t, 900 + i * 110, 136, 100, 56, here || target ? kOn : avail ? kOff : kNa, kCmdPos0 + i, here ? kGr : target ? kYe : kOr);
        }
        Key(g, hitsTop_, L"СТОП", 1460, 136, 100, 56, v.P != v.PT ? kOff : kNa, kCmdStop, kYe);
        PosBar(g, kc, v, 212, 14, 192);
    }

    // ---- row 2 left: the gear and the supports ----
    {
        const double gT = v.gearDown ? 1.0 : 0.0;
        const std::wstring gearTxt = v.gear >= 1 ? L"ВЫПУЩЕНО" : v.gear <= 0 ? L"УБРАНО" : (gT > v.gear ? L"ВЫХОДИТ " : L"УБИРАЕТСЯ ") + Pct(v.gear);
        const std::wstring setTxt = !v.grounded ? std::wstring(L"ПОЛЁТ · ") + (v.setStand ? L"НА КОРМУ" : L"ЛЁЖА")
                                    : v.P <= 0 ? L"ЛЁЖА" : v.P >= 6 ? (v.port ? L"НА СТОЛЕ" : L"НА КОРМЕ") : L"ЛАФЕТ";
        BlockR(g, 24, 272, 786, 296, L"ШАССИ · ОПОРЫ", setTxt + L" · ШАССИ " + gearTxt, v.gear >= 1 || v.gear <= 0 ? kTx : kYe);
        // the plan (stern left, nose right): the load rings, the names inside; the numbers in the table
        auto sx = [](double s) { return 56 + s * 1.4; };
        auto sy = [](double y) { return 362 + y * 1.2; };   // mech_v3 1.4 across: room for the gear keys under it
        std::vector<fr::P2> o;
        for (const fr::P2& q : {fr::P2{0, -13.5}, fr::P2{120, -13.5}, fr::P2{178, 0}, fr::P2{120, 13.5}, fr::P2{0, 13.5}, fr::P2{0, -13.5}}) o.push_back({sx(q.x), sy(q.y)});
        g.PolyLine(o, 0x2c4a44, 1.5);
        if (v.grounded) { g.Ring(sx(v.sCG), sy(0), 5, kRd, 1.5); g.T(L"ЦМ", sx(v.sCG) + 10, sy(0) + Cap(15) / 2, kRd, 15, 0, true); }
        struct Sup { int i; double s, y; };
        const Sup sup[7] = {{0, v.hipS, -26}, {1, v.hipS, 26}, {6, kKangHipS, 0}, {2, 2, -16}, {3, 2, 16}, {4, 28, -16}, {5, 28, 16}};
        for (const Sup& s : sup) {
            const double X = sx(s.s), Y = sy(s.y), r = 15, f = v.legR[s.i];
            const bool out = v.legOut[s.i] > 0.002, bad = v.legInteg[s.i] <= 0.0 || v.legBroken[s.i];
            g.Disc(X, Y, r + 3, kBg);
            g.Ring(X, Y, r, out ? kTrack : 0x141f1d, 5);
            if (f > 0.001) g.Arc(X, Y, r, -kPi / 2, -kPi / 2 + 2 * kPi * (std::min)(1.0, f), RCol(f), 5);
            g.T(kLegName[s.i], X, Y + Cap(15) / 2, bad ? kRd : f > 0.001 ? kWh : out ? kT2 : kDis, 15, 1, true);
        }
        // the weight, the trunnions, the soles, the sink
        const std::pair<std::wstring, unsigned> trn = v.hipCatcher ? std::make_pair(std::wstring(L"на страховочных"), kYe)
                                                     : std::make_pair(std::wstring(v.P > 0 && v.P < 6 ? L"на магнитах" : L"в покое"), kTx);
        const std::pair<std::wstring, unsigned> stp = !v.grounded ? std::make_pair(std::wstring(L"в воздухе"), kT2)
                                                     : v.anchors >= 0.99 ? std::make_pair(std::wstring(L"спечены"), kGr)
                                                     : v.anchors > 0.005 ? std::make_pair(L"спекаются " + Pct(v.anchors), kTx)
                                                     : v.jam >= 0.99 ? std::make_pair(std::wstring(L"заперты"), kTx) : std::make_pair(std::wstring(L"оседают"), kYe);
        const std::wstring soles = stp.first + (v.grounded ? L" · " + fr::Num(v.sink, 2) + L" м" : L"");   // + the loaded pads' sink
        g.T(L"вес", 40, 432, kT2, 17); NumU(g, fr::Num(v.weight / 1e6, 0), 286, 432, kTx, 22, L"МН");
        g.T(L"цапфы", 40, 464, kT2, 17); g.TF(trn.first, 320, 464, trn.second, 200, 18, 2, true);
        g.T(L"стопы", 40, 496, kT2, 17); g.TF(soles, 320, 496, v.grounded && v.sink > 0.5 ? kYe : stp.second, 220, 18, 2, true);
        // the gear's keys (the same commands as the keyboard N / Shift+N and the 2D panels): the landing set only with it stowed
        const std::wstring gSub = v.gear >= 1 ? L"выпущено" : v.gear <= 0 ? L"убрано" : (gT > v.gear ? L"выход " : L"уборка ") + Pct(v.gear);
        Key(g, hitsTop_, L"ШАССИ", 40, 508, 86, 48, v.gearDown ? kOn : kOff, kCmdGear, kOr, gSub);
        // the set the gear key brings out: the blades + the kangaroo (lying) or the four stern legs К1-К4 (tail-first); the ship has
        // no command for a single leg (Carriage: three groups)
        Key(g, hitsTop_, L"ЛОПАСТИ", 136, 508, 86, 48, !v.setStand ? kOn : v.gear > 0 ? kNa : kOff, kCmdSetLevel, kOr, L"Л П ПО · лёжа");
        Key(g, hitsTop_, L"К1–К4", 232, 508, 86, 48, v.setStand ? kOn : v.gear > 0 ? kNa : kOff, kCmdSetStand, kOr, L"на корму");
        // the table of the seven supports
        g.TS(L"ОПОРА", 336, 326, kT2, 15, true, 1); g.TS(L"СОСТОЯНИЕ", 388, 326, kT2, 15, true, 1);
        g.TS(L"% ДОПУСКА", 540, 326, kT2, 15, true, 1); g.TS(L"СИЛА, МН", 794, 326, kT2, 15, true, 1, 2);
        for (int r = 0; r < 7; ++r) {
            const int i = kLegRow[r];
            const double top = 336 + r * 32, yb = top + 23, R = v.legR[i], N = v.legN[i];
            const LegSt ls = LegState(v, i);
            const bool bad = v.legInteg[i] <= 0.0 || v.legBroken[i];
            g.T(kLegName[i], 336, yb, bad ? kRd : v.legOut[i] > 0.002 ? kWh : kT2, 20, 0, true);
            g.TF(ls.w, 388, yb, ls.c, 144, 18, 0, ls.bold);
            g.Fill(540, top + 11, 100, 10, kWell); g.Stroke(540, top + 11, 100, 10, kFr, 1);
            if (R > 0.001) g.Fill(541, top + 12, 98 * (std::min)(1.0, R), 8, RCol(R));
            g.Fill(540 + 85, top + 8, 1.5, 16, kLn);
            if (N > 0) { NumU(g, fr::Num(R * 100, 0), 694, yb, RCol(R), 22, L"%"); NumU(g, fr::Num(N / 1e6, 0), 766, yb, kTx, 22, L"МН"); }
            else { g.T(L"—", 694, yb, kDis, 22, 2, true); g.T(L"—", 766, yb, kDis, 22, 2, true); }
        }
    }

    // ---- row 2 middle: the wings and the fin, the rear view by the mesh's animation ----
    {
        const bool folding = foldDir_ != 0;
        std::pair<std::wstring, unsigned> wState = folding ? std::make_pair(std::wstring(foldDir_ > 0 ? L"СКЛАДЫВАЮТСЯ " : L"РАСКРЫВАЮТСЯ ") + Pct(fold), kYe)
                                                  : fold > 0.99 ? std::make_pair(std::wstring(v.wingGround ? L"СЛОЖЕНЫ · ГРУНТ" : L"СЛОЖЕНЫ"), kTx)
                                                  : v.wingMode == 1 ? std::make_pair(std::wstring(L"30° · ПОДНЯТЫ"), kTx) : std::make_pair(std::wstring(L"90° · РАЗВЁРНУТЫ"), kTx);
        BlockR(g, 826, 272, 400, 296, L"КРЫЛЬЯ И ПЕРО", wState.first, wState.second);
        const double cx = 1026, ay = 448, kk = 4.0;   // mech_v3 471, 4.7: smaller, the mode keys under it (the fold scale gave way)
        auto Pp = [&](double x, double y) { return fr::P2{cx + x * kk, ay - y * kk}; };
        g.T(L"вид с кормы", 842, 326, kT2, 15);
        std::vector<fr::P2> sec;
        for (const fr::P2& q : {fr::P2{0, -7.28}, fr::P2{7.62, -7.28}, fr::P2{13.38, -3.25}, fr::P2{11.58, 4.09}, fr::P2{7.51, 9.58}, fr::P2{0, kHullTop},
                                fr::P2{-7.51, 9.58}, fr::P2{-11.58, 4.09}, fr::P2{-13.38, -3.25}, fr::P2{-7.62, -7.28}}) sec.push_back(Pp(q.x, q.y));
        g.Poly(sec, kHull, kHullLn, 1.5);
        const double finH = v.finOut;   // the telescopic fin: two stages
        if (v.finInteg <= 0.0) g.T(L"перо потеряно", cx + 10, Pp(0, kHullTop).y - 10, kRd, 17, 0, true);
        else if (finH > 0.05) {
            const fr::P2 a = Pp(-0.8, kHullTop), b = Pp(0.8, kHullTop + finH);
            g.Fill(a.x, b.y, b.x - a.x, a.y - b.y, v.finInteg < 1.0 ? kYe : kOr);
            g.TF(L"перо " + fr::Num(finH, 0) + L" м" + (finDir_ < 0 ? L" ↓" : finDir_ > 0 ? L" ↑" : L""), cx + 0.8 * kk + 8, b.y + 12, kTx, 120, 17, 0, true);
        } else g.T(L"перо убрано", cx + 10, Pp(0, kHullTop).y - 10, kT2, 17, 0, true);
        if (v.podOut > 0.5) for (int s : {-1, 1}) { const fr::P2 c = Pp(s * 16, -2); g.Disc(c.x, c.y, 2.2 * kk, Mix(kOr, kBg, 0.55)); }
        const double a = v.wingInDeg, ar = a * kPi / 180, br = (a - v.wingOutDeg) * kPi / 180;
        for (int s : {-1, 1}) {
            const double integ = v.crestInteg[s < 0 ? 0 : 1], lx = cx + s * 150;
            if (integ <= 0.0) { g.T(L"потерян", lx, ay - 70, kRd, 17, 1, true); continue; }
            const fr::P2 root{kWingX0, kWingY}, hinge{root.x + kWingB1 * std::cos(ar), root.y + kWingB1 * std::sin(ar)},
                         tip{hinge.x + kWingB2 * std::cos(br), hinge.y + kWingB2 * std::sin(br)};
            const fr::P2 R0 = Pp(s * root.x, root.y), H1 = Pp(s * hinge.x, hinge.y), T1 = Pp(s * tip.x, tip.y);
            if (a > 2) {   // the horizontal and the angle's arc
                const fr::P2 e = Pp(s * (root.x + 9), root.y);
                g.Dashed(R0.x, R0.y, e.x, e.y, kLn, 1, 4, 4);
                if (s > 0) g.Arc(R0.x, R0.y, 6.5 * kk, -ar, 0, kT2, 1.5); else g.Arc(R0.x, R0.y, 6.5 * kk, kPi, kPi + ar, kT2, 1.5);
            }
            const unsigned wc = integ < 1.0 ? kYe : kOr;
            g.Line(R0.x, R0.y, H1.x, H1.y, wc, 5); g.Line(H1.x, H1.y, T1.x, T1.y, wc, 5);
            g.Disc(R0.x, R0.y, 2.5, wc); g.Disc(T1.x, T1.y, 2.5, wc); g.Disc(H1.x, H1.y, 2.5, kWh);
            // the angle at the block's side, clear of the panels' sweep
            g.T(fr::Num(a, 0) + L"°", lx, ay - 70, integ < 1.0 ? kYe : kWh, 24, 1, true);
            g.T(L"подъём", lx, ay - 70 + 8 + Cap(15), kT2, 15, 1);
        }
        // the mode keys: chosen directly (MechCommand steps the ship's cycle to it); 30 deg not with the stern legs out
        const bool no30 = v.setStand && v.gear > 0;
        const wchar_t* gnd = v.wingGround && fold > 0.5 ? L"грунт: сложены" : nullptr;
        Key(g, hitsTop_, L"КРЫЛЬЯ 90°", 842, 500, 116, 56, v.wingMode == 0 ? kOn : kOff, kCmdWing90, kOr, v.wingMode == 0 && gnd ? gnd : L"развёрнуты");
        Key(g, hitsTop_, L"КРЫЛЬЯ 30°", 968, 500, 116, 56, v.wingMode == 1 ? kOn : no30 ? kNa : kOff, kCmdWing30, kOr, v.wingMode == 1 && gnd ? gnd : L"подняты (вход)");
        Key(g, hitsTop_, L"СЛОЖИТЬ", 1094, 500, 116, 56, v.wingMode == 2 ? kOn : kOff, kCmdWingFold, kOr, L"крылья и перо");
    }

    // ---- row 2 right: the radiators (the crests and the fin) ----
    {
        BlockR(g, 1242, 272, 334, 296, L"РАДИАТОРЫ: ГРЕБНИ И ПЕРО");
        const std::pair<std::wstring, unsigned> rs = v.radHealth <= 0.0 ? std::make_pair(std::wstring(L"ПОТЕРЯНЫ"), kRd)
                                                    : !v.radOut ? std::make_pair(std::wstring(L"СЛОЖЕНЫ · СБРОС ×0,1"), kYe)
                                                    : v.radHealth < 0.999 ? std::make_pair(std::wstring(L"ВЫСТАВЛЕНЫ · ПОВРЕЖДЕНЫ"), kYe)
                                                    : std::make_pair(std::wstring(L"ВЫСТАВЛЕНЫ · РАБОТАЮТ"), kGr);
        LampM(g, 1266, 324, true, rs.second);
        g.TF(rs.first, 1282, 324 + Cap(19) / 2, rs.second, 1560 - 1282, 19, 0, true);
        const auto sh = FmtW(v.radiated);
        g.T(L"сброс тепла", 1258, 380, kT2, 17); NumU(g, sh.first, 1508, 392, v.radOut ? kWh : kYe, 40, sh.second);
        const unsigned tCol = v.sternT > v.tBoil ? kRd : v.sternT > v.tSafe ? kYe : kTx;
        g.T(L"температура", 1258, 436, kT2, 17); NumU(g, fr::Num((std::min)(v.sternT, 1800.0), 0), 1508, 436, tCol, 24, L"К");
        g.T(L"исправность", 1258, 468, kT2, 17);
        NumU(g, fr::Num(v.radHealth * 100, 0), 1508, 468, v.radHealth >= 0.999 ? kGr : v.radHealth >= 0.6 ? kYe : kRd, 24, L"%");
        const auto alt = FmtW(v.radOut ? v.radiated * 0.1 : v.radiated * 10.0);   // Plant: folded they shed a tenth
        g.T(v.radOut ? L"если сложить" : L"если выставить", 1258, 500, kT2, 17); NumU(g, alt.first, 1508, 500, kTx, 24, alt.second);
        std::wstring dmg;
        auto add = [&](const wchar_t* s) { dmg += (dmg.empty() ? L"" : L", ") + std::wstring(s); };
        if (v.crestInteg[0] < 1.0) add(L"гребень Л");
        if (v.crestInteg[1] < 1.0) add(L"гребень П");
        if (v.finInteg < 1.0) add(L"перо");
        if (!dmg.empty()) g.TF(L"повреждены: " + dmg, 1258, 532, kYe, 302, 17);
        else if (v.sternT > v.tSafe) g.TF(L"корма выше " + fr::Num(v.tSafe, 0) + L" К — держать выставленными", 1258, 532, kYe, 302, 17);
    }

    // ---- row 3: the hull's mechanisms, the warnings, the journal ----
    BlockR(g, 24, 584, 720, 192, L"КОРПУС · МЕХАНИЗМЫ");
    {
        // the keys (the same commands as the keyboard and the 2D panels; the pods as the left console's key), each lit by its state
        const double kx = 40, kw = 84, ky1 = 630, ky2 = 696;
        auto X = [&](int i) { return kx + i * (kw + 10); };
        const bool podHeld = v.podBlock != 0 && v.podOut < 0.99;
        const std::wstring podSub = podHeld && v.podsWanted ? (v.podBlock == 1 ? L"ждёт опоры" : L"q > 45 кПа")   // the reason in full in the warnings
                                    : v.podOut > 0.99 ? L"вышли" : v.podOut < 0.01 ? L"в отсеках" : L"выход " + Pct(v.podOut);
        Key(g, hitsTop_, L"ВЫДВ. БЛОКИ", X(0), ky1, kw, 56, podHeld && v.podsWanted ? kWarn : v.podsWanted ? kOn : kOff, kCmdPods, podHeld && v.podsWanted ? kRd : kOr, podSub);
        Key(g, hitsTop_, L"НАЗАД", X(1), ky1, kw, 56, v.podsWanted && v.podTarget < 45 ? kOn : kOff, kCmdPodsAft, kOr, L"сопла 0°");
        Key(g, hitsTop_, L"ВНИЗ", X(2), ky1, kw, 56, v.podsWanted && v.podTarget >= 45 ? kOn : kOff, kCmdPodsDown, kOr, L"сопла " + fr::Num(v.podAngle, 0) + L"°");
        Key(g, hitsTop_, L"АНГАР", X(3), ky1, kw, 56, v.hangarT > 0.5 ? kOn : kOff, kCmdHangar, kOr, v.hangar > 0.99 ? L"открыт" : v.hangar < 0.01 ? L"закрыт" : Pct(v.hangar));
        Key(g, hitsTop_, L"ВЕЗДЕХОДЫ", X(4), ky1, kw, 56, v.rovers > 0.5 ? kOn : v.hangar < 0.97 ? kNa : kOff, kCmdRovers, kOr,
            v.rovers > 0.99 ? L"вышли" : v.rovers < 0.01 ? L"убраны" : Pct(v.rovers));
        Key(g, hitsTop_, L"ПОРТ", X(0), ky2, kw, 56, v.port ? kOn : kOff, kCmdPort, kOr, v.port ? L"стол · " + Pct(v.bayDoors) : L"грунт");
        Key(g, hitsTop_, L"ШЛЮЗ", X(1), ky2, kw, 56, v.airlock ? kOn : kOff, kCmdAirlock, kOr, v.airlock ? L"открыт" : L"закрыт");
        Key(g, hitsTop_, L"ЛИФТ", X(2), ky2, kw, 56, !v.liftStowed ? kOn : kOff, kCmdLift, kOr,
            v.liftAtGround ? L"у грунта" : v.liftStowed ? L"сложен" : v.liftLowering ? L"вниз" : L"вверх");
        Key(g, hitsTop_, L"СТОЛ ВВЕРХ", X(3), ky2, kw, 56, v.portStep != 0 ? kOn : kOff, kCmdTableUp, kOr, L"погрузка");
        Key(g, hitsTop_, L"СТОЛ СТОП", X(4), ky2, kw, 56, v.portStep != 0 ? kOff : kNa, kCmdTableStop, kYe, L"держать");
        // the stern's cups: indicators only (the engines' pages drive them)
        struct It { const wchar_t* n; std::wstring v; bool lamp; };
        const It items[3] = {{L"чаши анамезона", v.irisAna > 0.5 ? L"ОТКРЫТЫ" : L"закрыты", v.irisAna < 0.5},
                             {L"ретро-чаши носа", v.irisNose > 0.5 ? L"ОТКРЫТЫ" : L"закрыты", v.irisNose < 0.5},
                             {L"маршевая", v.marchOut > 0.5 ? L"ВЫДВИНУТА" : L"в колодце", true}};
        for (int i = 0; i < 3; ++i) {
            const double y = 646 + i * 32, x = 516, xr = 728;
            LampM(g, x + 7, y - Cap(17) / 2, items[i].lamp);
            g.TF(items[i].n, x + 22, y, kT2, 104, 17);
            g.TF(items[i].v, xr, y, kTx, xr - x - 22 - 112, 17, 2, true);
        }
    }
    {
        std::vector<std::pair<std::wstring, unsigned>> warn;
        if (v.estop) warn.push_back({L"аварийный стоп — все приводы заперты", kRd});
        for (int i : kLegRow) if (v.legInteg[i] <= 0.0 || v.legBroken[i]) warn.push_back({std::wstring(L"опора ") + kLegName[i] + (v.legInteg[i] <= 0.0 ? L" потеряна" : L" сломана"), kRd});
        for (int i : kLegRow) if (v.legR[i] > 1.0) warn.push_back({std::wstring(L"перегрузка опоры ") + kLegName[i] + L" — выше допуска", kRd});
        if (v.radHealth <= 0.0) warn.push_back({L"радиаторы потеряны", kRd});
        if (!v.radOut && v.sternT > v.tSafe) warn.push_back({L"радиаторы сложены, корма " + fr::Num(v.sternT, 0) + L" К", v.sternT > v.tBoil ? kRd : kYe});
        if (v.P != v.PT && v.wingFold < 0.999 && !v.held) warn.push_back({L"крылья и перо складываются — лафет ждёт", kYe});
        if (v.podsWanted && v.podOut < 0.99 && v.podBlock == 1) warn.push_back({L"выдвижные блоки: корабль лежит или поднимается на опорах — выпуск невозможен", kYe});
        if (v.podsWanted && v.podOut < 0.99 && v.podBlock == 2) warn.push_back({L"выдвижные блоки: скоростной напор выше 45 кПа — створки закрыты", kYe});
        if (v.hangar > 0.01) warn.push_back({L"ангар открыт — положения заперты", kYe});
        if (!v.liftStowed) warn.push_back({L"лифт шлюза не сложен — положения заперты", kYe});
        if (v.gear < 1 && v.grounded) warn.push_back({L"шасси не выпущено — положения недоступны", kYe});
        if (v.balHold) warn.push_back({L"раскачка корпуса — подъём на паузе", kYe});
        if (v.hipCatcher) warn.push_back({L"цапфы на страховочных — привод медленнее", kYe});
        for (int i : kLegRow) if (v.legR[i] > 0.85 && v.legR[i] <= 1.0) warn.push_back({std::wstring(L"опора ") + kLegName[i] + L" у предела допуска (85 %)", kYe});
        if (std::fabs(v.sway) > 0.3) warn.push_back({L"раскачка колонн больше 30 см", kYe});
        if (v.P > 2 && v.P < 4 && v.P == v.PT) warn.push_back({L"без кормовых ног — только в безветрие", kYe});
        if (v.radHealth > 0.0 && v.radHealth < 0.999) warn.push_back({L"радиаторы повреждены — сброс −" + fr::Num((1 - v.radHealth) * 100, 0) + L" %", kYe});
        if (v.petals < v.petalsAll) warn.push_back({L"лепестки стоп: потеряно " + std::to_wstring(v.petalsAll - v.petals), kYe});
        std::stable_sort(warn.begin(), warn.end(), [](const auto& a, const auto& b) { return (a.second == kRd) > (b.second == kRd); });
        const size_t nw = warn.size();
        BlockR(g, 760, 584, 400, 192, L"ПРЕДУПРЕЖДЕНИЯ", nw ? std::to_wstring(nw) : L"", nw && warn[0].second == kRd ? kRd : kYe);
        if (!nw) { LampM(g, 783, 646 - Cap(17) / 2, true, kGr, 5); g.T(L"нет", 798, 646, kGr, 17); }
        const size_t shown = nw > 4 ? 3 : nw;
        for (size_t i = 0; i < shown; ++i) {
            const double y = 646 + i * 32.0;
            LampM(g, 783, y - Cap(17) / 2, true, warn[i].second, 5);
            g.TF(warn[i].first, 798, y, warn[i].second, 1144 - 798, 17);
        }
        if (nw > 4) g.T(L"… и ещё " + std::to_wstring(nw - 3), 798, 742, kT2, 17);
    }
    BlockR(g, 1176, 584, 400, 192, L"ЖУРНАЛ");
    for (size_t i = 0; i < log_.lines.size() && i < 4; ++i) {
        const double y = 646 + i * 32.0;
        g.T(Clock(log_.lines[i].t), 1240, y, kT2, 15, 2);
        g.TF(log_.lines[i].txt, 1252, y, Journal::Col(log_.lines[i].lvl), 1560 - 1252, 17);
    }
}

void Screen::DrawPult(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v) {
    hitsPult_.clear();
    if (!skp) return;
    Canvas g(skp, font, ox, oy);
    const double W = w, H = h;
    const bool moving = v.P != v.PT && !v.held;
    g.Fill(0, 0, W, H, cBg); g.Stroke(5, 5, W - 10, H - 10, cFr, 2);
    g.T(L"МЕХАНИЗАЦИЯ · ПУЛЬТ", 22, 36, cTx, 18, 0, 700);
    // ---- the position scale: four keys on one lit bar ----
    Frame(g, 15, 54, W - 30, 192, L"ПОЛОЖЕНИЕ");
    const double x0 = 115, x1 = W - 300, by = 198;
    auto X = [&](double P) { return x0 + (x1 - x0) * P / 6.0; };
    g.Fill(x0, by, x1 - x0, 16, 0x0d1715); g.Stroke(x0, by, x1 - x0, 16, cFr, 1);
    const double pulse = 0.7 + 0.3 * std::sin(v.t * 6.0);
    const unsigned lit = v.estop ? cRd : moving ? Mix(0xe8be50, cBg, pulse) : cGr;
    if (X(v.P) - x0 > 0.5) { g.Fill(x0, by - 3, X(v.P) - x0, 22, lit, 0.18); g.Fill(x0, by + 2, X(v.P) - x0, 12, lit); }
    if (moving) {   // the run still to go
        const double a = (std::min)(X(v.P), X(v.PT)), b = (std::max)(X(v.P), X(v.PT));
        g.Dashed(a, by + 2, b, by + 2, cYe, 1, 6, 5); g.Dashed(a, by + 14, b, by + 14, cYe, 1, 6, 5);
    }
    for (int k = 0; k <= 6; ++k) {
        g.Fill(X(k) - 1, by + 16, 2, 7, 0x2c4a44);
        if (k < 6) g.T(kPhShort[k], (X(k) + X(k + 1)) / 2, by + 36, int(std::floor(v.P + 1e-9)) == k && moving ? cYe : cDim, 11, 1);
    }
    const double tl = TimeLeft(v);
    for (int i = 0; i < kPosCount; ++i) {
        const double P = PositionP(i, v.tripodP);
        const bool here = Near(v.P, P) && v.P == v.PT, target = Near(v.PT, P) && v.P != v.PT;
        const double bw = 190, bx = (std::max)(22.0, X(P) - bw / 2);
        const unsigned col = here ? cGr : target ? cYe : cOr;
        g.Line(X(P), 162, X(P), by, here ? cGr : target ? cYe : 0x3a4a44, 2);
        g.Disc(X(P), by + 8, 5, col);
        Btn(g, hitsPult_, kPos[i].t, bx, 70, bw, 92, here || target, kCmdPos0 + i, col,
            here ? L"ПОЛОЖЕНИЕ" : target ? L"идём · " + Fmt(tl, 0) + L" с" : kPos[i].sub, 16);
    }
    Btn(g, hitsPult_, L"СТОП", W - 180, 70, 150, 92, false, kCmdStop, cYe, L"держать позу", 16);
    // ---- the other groups: two rows of keys ----
    const double gy = 262, gh = H - 14 - gy, rh = (gh - 18 - 12 - 12) / 2, r1 = gy + 18, r2 = r1 + rh + 12;
    Frame(g, 15, gy, 470, gh, L"ШАССИ");
    Btn(g, hitsPult_, L"ШАССИ", 30, r1, 140, rh, v.gearDown, kCmdGear, cOr, v.gear >= 1 ? L"выпущено" : v.gear <= 0 ? L"убрано" : L"…");
    Btn(g, hitsPult_, L"ЛЁЖА", 180, r1, 140, rh, !v.setStand, kCmdSetLevel, cOr, L"посадка");
    Btn(g, hitsPult_, L"НА КОРМУ", 330, r1, 140, rh, v.setStand, kCmdSetStand, cOr, L"посадка");
    Btn(g, hitsPult_, L"АВАРИЙНЫЙ СТОП", 30, r2, 440, rh, v.estop, kCmdEstop, cRd, v.estop ? L"снять" : L"все приводы");
    Frame(g, 500, gy, 560, gh, L"КОРПУС");
    const int wm = (std::max)(0, (std::min)(2, v.wingMode));
    Btn(g, hitsPult_, L"КРЫЛЬЯ", 515, r1, 170, rh, wm == 0, kCmdWings, cOr, v.wingFold >= 0.99 && wm != 2 ? L"сложены: грунт" : kWingTxt[wm]);
    Btn(g, hitsPult_, L"ВЫДВ. БЛОКИ", 695, r1, 170, rh, v.podsWanted, kCmdPods, cOr, v.podOut > 0.99 ? L"выдвинуты" : v.podOut < 0.01 ? L"в отсеках" : L"…");
    Btn(g, hitsPult_, L"ВЕЗДЕХОДЫ", 875, r1, 170, rh, v.rovers > 0.5, kCmdRovers, cOr, v.rovers > 0.99 ? L"выдвинуты" : v.rovers < 0.01 ? L"убраны" : L"…");
    Btn(g, hitsPult_, L"НАЗАД", 515, r2, 262, rh, v.podTarget < 45, kCmdPodsAft, cOr, L"сопла выдвижных блоков 0° · тяга вперёд");
    Btn(g, hitsPult_, L"ВНИЗ", 783, r2, 262, rh, v.podTarget >= 45, kCmdPodsDown, cOr, L"сопла выдвижных блоков 90° · висение");
    Frame(g, 1075, gy, W - 1090, gh, L"АНГАР · ПОРТ · ШЛЮЗ");
    Btn(g, hitsPult_, L"АНГАР", 1090, r1, 155, rh, v.hangarT > 0.5, kCmdHangar, cOr, v.hangar > 0.99 ? L"открыт" : v.hangar < 0.01 ? L"закрыт" : L"…");
    Btn(g, hitsPult_, L"ПОРТ", 1253, r1, 155, rh, v.port, kCmdPort, cOr, v.port ? L"на столе" : L"грунт");
    Btn(g, hitsPult_, L"ШЛЮЗ", 1416, r1, 155, rh, v.airlock, kCmdAirlock, cOr, v.airlock ? L"открыт" : L"закрыт");
    Btn(g, hitsPult_, L"СТОЛ ВВЕРХ", 1090, r2, 236, rh, v.portStep != 0, kCmdTableUp, cOr, L"к погрузке и обратно");
    Btn(g, hitsPult_, L"СТОЛ СТОП", 1335, r2, 236, rh, false, kCmdTableStop, cYe, L"держать");
}

}  // namespace tantra::mechscreen
