// TantraMechScreen: see TantraMechScreen.h. DrawTop() follows drawTop() and DrawPult() drawLow() of
// Tantra_Design/tantra_mech_screen.html block by block; the coordinates are the mockup's.
#include "TantraMechScreen.h"

#include <algorithm>
#include <cmath>

namespace tantra::mechscreen {

using namespace tantra::scr;
using namespace tantra::scr::ui;

namespace {

constexpr double kSCG = 58.1, kTrack0 = 53.4, kFootH = 5.2, kAxis0 = 24.0, kHTop = kSCG + 22.5;
const wchar_t* const kPhName[8] = {L"лежит на лопастях и кенгуру", L"подъём на лопастях", L"цапфы под ЦМ, кенгуру в карман", L"поворот вокруг цапф",
                                   L"кормовые ноги выходят", L"нагрузка на корму", L"лопасти убираются", L"стоит на корме"};
const wchar_t* const kPhShort[6] = {L"подъём", L"цапфы", L"поворот", L"лапы", L"нагрузка", L"сбор"};
const wchar_t* const kWingTxt[3] = {L"развёрнуты 90°", L"подняты 30°", L"сложены"};
const double kWingDeg[3] = {0.0, 30.0, 85.0};
struct Pos { const wchar_t* t; const wchar_t* sub; };
const Pos kPos[5] = {{L"В ГОРИЗОНТ", L"лёжа на лопастях"}, {L"НА ТРЁХ", L"ось 32 м, на лопастях и кенгуру"}, {L"НА НОГИ", L"подъём до верхней точки"}, {L"75°", L"стела: подъём и поворот"},
                     {L"ВЗЛЁТНОЕ", L"на 4 кормовых ногах"}};

double Ease(double u) { u = (std::max)(0.0, (std::min)(1.0, u)); return u * u * u * (10 + u * (-15 + 6 * u)); }
double EaseInv(double y) { double lo = 0, hi = 1; for (int i = 0; i < 40; ++i) { const double m = (lo + hi) / 2; if (Ease(m) < y) lo = m; else hi = m; } return (lo + hi) / 2; }
bool Near(double a, double b) { return std::fabs(a - b) < 2e-3; }
int PosAt(double P, double tp) { for (int i = 0; i < kPosCount; ++i) if (Near(P, PositionP(i, tp))) return i; return -1; }

// the schematic pose of the mockup (the thumbnail): trunnion height, hip station, hull angle, kangaroo, stern legs
struct Pose { double H, hip, th, kang, stern, load, stow, mast; };
Pose PoseOf(double P) {
    auto ph = [&](double k) { return (std::max)(0.0, (std::min)(1.0, P - k)); };
    Pose p;
    p.H = P >= 6 ? kHTop : kAxis0 + (kHTop - kAxis0) * Ease(ph(0));
    p.hip = kTrack0 + (kSCG - 0.3 - kTrack0) * Ease(ph(1));
    p.th = kPi / 2 * Ease(ph(2));
    p.kang = P < 1.5 ? 1 : P < 2 ? 1 - Ease((P - 1.5) * 2) : 0;
    p.stern = Ease(ph(3)); p.load = Ease(ph(4)); p.stow = Ease(ph(5)); p.mast = p.H - kFootH;
    return p;
}
double PhaseTime(int k, double turn) { static const double ph[6] = {20, 20, 45, 20, 8, 20}; return k == 2 ? turn : ph[(std::max)(0, (std::min)(5, k))]; }
double TimeLeft(const View& v, double turn) {
    double t = v.P != v.PT ? (1.0 - v.wingFold) * 8.0 : 0.0, p = v.P;
    if (v.PT > p) while (p < v.PT - 1e-9) { const int k = int(std::floor(p + 1e-9)); const double e = (std::min)(v.PT, k + 1.0); t += (e - p) * PhaseTime(k, turn); p = e; }
    else while (p > v.PT + 1e-9) { const int k = int(std::ceil(p - 1e-9)) - 1; const double e = (std::max)(v.PT, double(k)); t += (p - e) * PhaseTime(k, turn); p = e; }
    return t;
}

void ShipThumb(Canvas& g, double x0, double y0, double w, double h, double sc, const View& v) {
    const Pose p = PoseOf(v.P);
    const double sw = v.sway * 2.5, ox = x0 + w * 0.3, oy = y0 + h - 10;
    auto P = [&](double x, double y) { return Pt{ox + x * sc, oy - y * sc}; };
    g.Fill(x0 + 2, oy, w - 4, 8, 0x1a2420); g.Line(x0 + 2, oy, x0 + w - 2, oy, 0x5a5038, 1);
    const double tx = sw, ty = p.H, ca = std::cos(p.th), sa = std::sin(p.th);
    auto H2W = [&](double s, double z) { return P(tx + (s - p.hip) * ca - z * sa, ty + (s - p.hip) * sa + z * ca); };
    if (v.gear > 0.05) { const Pt f = P(0, kFootH), t2 = P(tx, ty); g.Line(f.x, f.y, t2.x, t2.y, Mix(0x5b8cc8, cBg, (std::max)(0.12, 1 - p.stow)), 2.5); }
    std::vector<Pt> hull; for (const auto& q : {Pt{0, 7}, Pt{120, 7}, Pt{178, 0}, Pt{140, -7.5}, Pt{0, -7.5}}) hull.push_back(H2W(q.x, q.y));
    g.Shape(hull, 0x3a4a52, 1.0, 0x6a8090, 1);
    const Pt cg = H2W(kSCG, 0); g.Circle(cg.x, cg.y, 3, cRd, 1.5);
    if (p.kang > 0.02 && v.gear > 0.5) { const Pt kh = H2W(99.4, -7.5), kf = P(99.4 - kTrack0 + 12, 0); g.Line(kh.x, kh.y, kf.x, kf.y, Mix(0x7a8db0, cBg, p.kang), 2); }
    if (p.stern > 0.01) {
        const Pt spt = H2W(0, 0);
        for (int s : {-1, 1}) { const Pt hg = H2W(30, -9 * s); const Pt ft{spt.x + s * 27 * sc, oy}; g.Line(hg.x, hg.y, hg.x + (ft.x - hg.x) * p.stern, hg.y + (ft.y - hg.y) * p.stern, 0x5b8cc8, 2); }
    }
}

}  // namespace

double PositionP(int i, double tripodP) { static const double p75 = 2.0 + EaseInv(75.0 / 90.0); return i <= 0 ? 0.0 : i == 1 ? tripodP : i == 2 ? 1.0 : i == 3 ? p75 : 6.0; }

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
    if ((v.hangarT > 0.5) != (o.hangarT > 0.5)) log_.Add(v.t, v.hangarT > 0.5 ? L"Ангар открывается" : L"Ангар закрывается", 0);
    if (v.port != o.port) log_.Add(v.t, v.port ? L"Стоянка: порт (стол)" : L"Стоянка: грунт", 0);
    if (v.liftStowed != o.liftStowed) log_.Add(v.t, v.liftStowed ? L"Лифт шлюза сложен" : L"Лифт шлюза выходит", 0);
    last_ = v;
}

void Screen::DrawTop(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v) {
    hitsTop_.clear();
    if (!skp) return;
    Watch(v);
    Canvas g(skp, font, ox, oy);
    const double W = w, H = h;
    const Pose p = PoseOf(v.P);
    const bool moving = v.P != v.PT && !v.held;
    g.Fill(0, 0, W, H, cBg); g.Stroke(5, 5, W - 10, H - 10, cFr, 2);
    // the title bar: the tabs and the state
    Btn(g, hitsTop_, L"МЕХАНИЗАЦИЯ", 18, 14, 190, 38, true, kCmdTabMech);
    Btn(g, hitsTop_, L"ТЕПЛО", 214, 14, 110, 38, false, kCmdTabThermal, v.hotSkin && std::fmod(v.t, 1.0) < 0.5 ? cRd : cOr);
    const int at = PosAt(v.P, v.tripodP), to = PosAt(v.PT, v.tripodP);
    const double turn = 45.0;
    std::wstring st; unsigned stCol;
    if (v.estop) { st = L"АВАРИЙНЫЙ СТОП · приводы заперты"; stCol = cRd; }
    else if (moving) { st = std::wstring(v.PT > v.P ? L"ПОДЪЁМ" : L"ОПУСКАНИЕ") + L" → «" + (to >= 0 ? kPos[to].t : L"?") + L"» · " + Fmt(TimeLeft(v, turn), 0) + L" с"; stCol = cYe; }
    else if (v.P != v.PT) { st = L"ЛАФЕТ НА ПАУЗЕ"; stCol = cYe; }
    else if (at >= 0) { st = std::wstring(L"ПОЛОЖЕНИЕ «") + kPos[at].t + L"»"; stCol = cGr; }
    else { st = L"ЛАФЕТ ОСТАНОВЛЕН МЕЖДУ ПОЛОЖЕНИЯМИ"; stCol = cYe; }
    g.Disc(350, 31, 9, stCol); g.T(st, 366, 38, stCol, 17, 0, 700);
    g.T(std::wstring(L"шасси ") + (v.gear >= 1 ? L"выпущено" : v.gear <= 0 ? L"убрано" : L"…") + L" · " + (v.port ? L"ПОРТ (стол)" : L"грунт") + L" · посадка " + (v.setStand ? L"на корму" : L"лёжа"), W - 22, 38, cDim, 13, 2);
    // ---- band 1 (~20 %): the pose and the gear ----
    Frame(g, 15, 62, W - 30, 150, L"ПОЛОЖЕНИЕ · ШАССИ");
    ShipThumb(g, 25, 74, 330, 132, 0.55, v);
    const std::wstring vals[6][2] = {{L"угол корпуса", Fmt(v.theta * 180 / kPi, 0) + L"°"}, {L"цапфы", Fmt(v.trunnionH, 1) + L" м"}, {L"колонна", Fmt(v.mastLen, 1) + L" м"},
                                     {L"ЦМ от цапф", Fmt(v.cgRes, 2) + L" м"}, {L"раскачка", Fmt(std::fabs(v.sway) * 100, 0) + L" см"}, {L"фаза", Fmt(v.P, 2) + L" / 6"}};
    for (int i = 0; i < 6; ++i) { const double x = 380 + (i / 2) * 205, y = 98 + (i % 2) * 28; g.T(vals[i][0], x, y, cDim, 13); g.T(vals[i][1], x + 185, y, cTx, 15, 2, 700); }
    g.T(kPhName[v.P >= 6 ? 7 : int(std::floor(v.P + 1e-9))], 380, 162, cTx, 14, 0, 600);
    if (moving) g.T(L"до положения " + Fmt(TimeLeft(v, turn), 0) + L" с", 380, 188, cYe, 13);
    {   // the position keys (on the panel the pult has no shelf of its own): four positions on a lit bar, СТОП
        const double bx = 1000, by = 72, bw = (575 - 5 * 6) / 6.0, bh = 88, tl = TimeLeft(v, turn);
        for (int i = 0; i < kPosCount + 1; ++i) {
            const double x = bx + i * (bw + 6);
            if (i == kPosCount) { Btn(g, hitsTop_, L"СТОП", x, by, bw, bh, false, kCmdStop, cYe, L"держать", 15); continue; }
            const double P = PositionP(i, v.tripodP);
            const bool here = Near(v.P, P) && v.P == v.PT, target = Near(v.PT, P) && v.P != v.PT;
            Btn(g, hitsTop_, kPos[i].t, x, by, bw, bh, here || target, kCmdPos0 + i, here ? cGr : target ? cYe : cOr, here ? L"положение" : target ? L"идём · " + Fmt(tl, 0) + L" с" : L"", 15);
        }
        const double x0 = bx, x1 = bx + 575, yb = by + bh + 14;
        g.Fill(x0, yb, x1 - x0, 12, 0x0d1715); g.Stroke(x0, yb, x1 - x0, 12, cFr, 1);
        g.Fill(x0, yb + 2, (x1 - x0) * v.P / 6.0, 8, v.estop ? cRd : moving ? cYe : cGr);
        for (int i = 0; i < kPosCount; ++i) g.Disc(x0 + (x1 - x0) * PositionP(i, v.tripodP) / 6.0, yb + 6, 4, Near(v.P, PositionP(i, v.tripodP)) ? cGr : cOr);
    }
    // ---- band 2 left: the supports ----
    Frame(g, 15, 228, 790, 330, L"ОПОРЫ · НАГРУЗКИ");
    g.T(L"опорная схема (корма ←, нос →), % допуска", 32, 254, cDim, 11);
    auto sx = [](double s) { return 60 + s * 2.85; };
    auto sy = [](double y) { return 400 + y * 3; };
    {
        std::vector<Pt> o; for (const auto& q : {Pt{0, -13.5}, Pt{120, -13.5}, Pt{178, 0}, Pt{120, 13.5}, Pt{0, 13.5}, Pt{0, -13.5}}) o.push_back({sx(q.x), sy(q.y)});
        g.Polyline(o, 0x2c4a44, 1.5);
        g.Dashed(sx(0), sy(0), sx(178), sy(0), 0x2c4a44, 1, 4, 4);
        g.Circle(sx(kSCG), sy(0), 5, cRd, 1.5); g.T(L"ЦМ", sx(kSCG), sy(0) - 10, cRd, 10, 1);
    }
    struct Sup { const wchar_t* n; double s, y; int leg; bool sh; };
    const Sup sup[7] = {{L"Л", p.hip, -32, 0, false}, {L"П", p.hip, 32, 1, false}, {L"Кн", 99.4, 0, 6, false}, {L"К1", 4, -24, 2, true}, {L"К2", 4, 24, 3, true},
                        {L"К3", 24, -24, 4, true}, {L"К4", 24, 24, 5, true}};
    for (const Sup& s : sup) {
        const double X = sx(s.s), Y = sy(s.y), r = 22, f = v.legR[s.leg], F = v.legN[s.leg];
        g.Circle(X, Y, r, 0x1f332f, 7);
        if (f > 0.001) g.Arc(X, Y, r, -kPi / 2, -kPi / 2 + 2 * kPi * (std::min)(1.0, f), RatioCol(f), 7);
        g.T(s.n, X, Y + 5, f > 0.001 ? cWh : cDim, 13, 1, 700);
        const std::wstring lab = f > 0.001 ? (s.sh ? Fmt(f * 100, 0) + L" %" : Fmt(F / 1e6, 0) + L" МН · " + Fmt(f * 100, 0) + L" %") : s.sh ? L"—" : L"без нагрузки";
        g.T(lab, X, Y + (s.y < 0 ? -r - 8 : r + 17), f > 0.001 ? RatioCol(f) : cDim, 11, 1, 600);
    }
    double sternEach = 0; for (int i = 2; i < 6; ++i) sternEach += v.legN[i] / 4;
    struct LR { const wchar_t* n; std::wstring val; unsigned c; };
    const LR lr[8] = {{L"вес", Fmt(v.weight / 1e6, 0) + L" МН", cTx}, {L"лопасти, кажд.", Fmt((std::max)(v.legN[0], v.legN[1]) / 1e6, 0) + L" МН", cTx},
                      {L"кенгуру", Fmt(v.legN[6] / 1e6, 0) + L" МН", cTx}, {L"корм. ноги, кажд.", Fmt(sternEach / 1e6, 0) + L" МН", cTx},
                      {L"привод цапф", Fmt(v.driveMoment / 1e6, 0) + L" МН·м", v.driveMoment > 1.5e9 ? cYe : cTx}, {L"изгиб лопастей", Fmt(v.bend * 100, 0) + L" %", RatioCol(v.bend)},
                      {L"цапфы", L"захват", cGr}, {L"грунт", L"осадка в норме", cGr}};
    for (int i = 0; i < 8; ++i) { const double y = 290 + i * 30; g.T(lr[i].n, 600, y, cDim, 12); g.T(lr[i].val, 792, y, lr[i].c, 13, 2, 700); }
    // ---- band 2 right: the hull's mechanisms ----
    Frame(g, 820, 228, W - 835, 330, L"КОРПУС · МЕХАНИЗМЫ");
    const int wm = (std::max)(0, (std::min)(2, v.wingMode));
    const std::wstring wTxt = v.wingFold > 0.01 && v.wingFold < 0.99 ? (v.P != v.PT || v.grounded ? L"складываются…" : L"раскрываются…") : v.wingFold >= 0.99 && wm != 2 ? L"сложены (грунт)" : kWingTxt[wm];
    struct HR { const wchar_t* n; std::wstring val; bool lamp; };
    auto pct = [](double f) { return Fmt(f * 100, 0) + L" %"; };
    const HR hull[9] = {
        {L"крылья", wTxt, v.wingFold < 0.01 && wm == 0},
        {L"перо (киль)", v.wingFold > 0.5 || wm == 2 ? L"убрано" : L"выдвинуто", !(v.wingFold > 0.5 || wm == 2)},
        {L"гондолы", v.podOut > 0.99 ? L"выдвинуты · сопла " + Fmt(v.podAngle, 0) + L"°" : v.podOut < 0.01 ? L"в отсеках" : L"выход " + pct(v.podOut), v.podOut > 0.99},
        {L"чаши анамезона", v.irisAna > 0.5 ? L"ОТКРЫТЫ" : L"закрыты", v.irisAna < 0.5},
        {L"ретро-чаши носа", v.irisNose > 0.5 ? L"ОТКРЫТЫ" : L"закрыты", v.irisNose < 0.5},
        {L"маршевая", v.marchOut > 0.5 ? L"ВЫДВИНУТА" : L"в колодце", true},
        {L"порт", v.port ? L"на столе · створки " + pct(v.bayDoors) : L"не используется", true},
        {L"ангар · вездеходы", (v.hangar > 0.99 ? std::wstring(L"открыт") : v.hangar < 0.01 ? std::wstring(L"закрыт") : pct(v.hangar)) + (v.rovers > 0.5 ? L" · выдвинуты" : L""), v.hangar < 0.01},
        {L"шлюз · лифт", v.liftAtGround ? L"кабина у грунта" : v.liftStowed ? L"сложен" : L"в движении", v.liftStowed}};
    for (int i = 0; i < 9; ++i) { const double y = 262 + i * 32; Lamp(g, 840, y - 5, hull[i].lamp); g.T(hull[i].n, 856, y, cDim, 13); g.T(hull[i].val, 1250, y, cTx, 14, 2, 700); }
    {   // the rear view: wings, fin, pods
        const double cx = 1420, cy = 410, k = 4.3;
        g.Ellipse(cx, cy, 13.5 * k, 9 * k, 0x16241f, 0x3f6a5f, 2);
        const double wa = (kWingDeg[wm] + (85.0 - kWingDeg[wm]) * v.wingFold) * kPi / 180;
        for (int s : {-1, 1}) g.Line(cx + s * 13 * k, cy, cx + s * (13 + 16 * std::cos(wa)) * k, cy - 16 * std::sin(wa) * k, cOr, 3);
        const double fin = wm == 2 ? 0.0 : 1.0 - v.wingFold;
        if (fin > 0.01) g.Line(cx, cy - 9 * k, cx, cy - (9 + 13 * fin) * k, cOr, 3);
        if (v.podOut > 0.5) for (int s : {-1, 1}) g.Disc(cx + s * 16 * k, cy + 2 * k, 2.2 * k, cOr);
        g.T(L"вид с кормы", cx, cy + 62, cDim, 11, 1);
    }
    // ---- band 3: the warnings and the journal ----
    Frame(g, 15, 574, 780, 210, L"ПРЕДУПРЕЖДЕНИЯ");
    std::vector<std::pair<std::wstring, unsigned>> warn;
    if (v.P != v.PT && v.wingFold < 0.999) warn.push_back({L"крылья и перо складываются — лафет ждёт", cYe});
    if (v.hangar > 0.01) warn.push_back({L"ангар открыт — положения заперты", cYe});
    if (!v.liftStowed) warn.push_back({L"лифт шлюза не сложен — положения заперты", cYe});
    if (v.gear < 1 && v.grounded) warn.push_back({L"шасси не выпущено — положения недоступны", cYe});
    if (v.balHold) warn.push_back({L"раскачка корпуса — подъём на паузе", cYe});
    if (v.bend > 0.85) warn.push_back({L"изгиб лопастей у предела", cRd});
    for (int i = 0; i < 7; ++i) if (v.legR[i] > 1.0) { warn.push_back({L"перегрузка опоры — выше допуска", cRd}); break; }
    if (std::fabs(v.sway) > 0.3) warn.push_back({L"раскачка колонн больше 30 см", cYe});
    if (v.P > 2 && v.P < 4 && v.P == v.PT) warn.push_back({L"стоит на лопастях без кормовых ног — только в безветрие", cYe});
    if (v.estop) warn.push_back({L"аварийный стоп — все приводы заперты", cRd});
    if (warn.empty()) warn.push_back({L"нет", cGr});
    for (size_t i = 0; i < warn.size() && i < 7; ++i) g.T(warn[i].first, 32, 606 + i * 24.0, warn[i].second, 14);
    Frame(g, 810, 574, W - 825, 210, L"ЖУРНАЛ");
    for (size_t i = 0; i < log_.lines.size() && i < 8; ++i) g.T(g.Fit(Clock(log_.lines[i].t) + L"  " + log_.lines[i].txt, W - 860, 13), 828, 604 + i * 22.0, Journal::Col(log_.lines[i].lvl), 13);
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
    const double tl = TimeLeft(v, 45.0);
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
    Btn(g, hitsPult_, L"ГОНДОЛЫ", 695, r1, 170, rh, v.podsWanted, kCmdPods, cOr, v.podOut > 0.99 ? L"выдвинуты" : v.podOut < 0.01 ? L"в отсеках" : L"…");
    Btn(g, hitsPult_, L"ВЕЗДЕХОДЫ", 875, r1, 170, rh, v.rovers > 0.5, kCmdRovers, cOr, v.rovers > 0.99 ? L"выдвинуты" : v.rovers < 0.01 ? L"убраны" : L"…");
    Btn(g, hitsPult_, L"НАЗАД", 515, r2, 262, rh, v.podTarget < 45, kCmdPodsAft, cOr, L"сопла гондол 0° · тяга вперёд");
    Btn(g, hitsPult_, L"ВНИЗ", 783, r2, 262, rh, v.podTarget >= 45, kCmdPodsDown, cOr, L"сопла гондол 90° · висение");
    Frame(g, 1075, gy, W - 1090, gh, L"АНГАР · ПОРТ · ШЛЮЗ");
    Btn(g, hitsPult_, L"АНГАР", 1090, r1, 155, rh, v.hangarT > 0.5, kCmdHangar, cOr, v.hangar > 0.99 ? L"открыт" : v.hangar < 0.01 ? L"закрыт" : L"…");
    Btn(g, hitsPult_, L"ПОРТ", 1253, r1, 155, rh, v.port, kCmdPort, cOr, v.port ? L"на столе" : L"грунт");
    Btn(g, hitsPult_, L"ШЛЮЗ", 1416, r1, 155, rh, v.airlock, kCmdAirlock, cOr, v.airlock ? L"открыт" : L"закрыт");
    Btn(g, hitsPult_, L"СТОЛ ВВЕРХ", 1090, r2, 236, rh, v.portStep != 0, kCmdTableUp, cOr, L"к погрузке и обратно");
    Btn(g, hitsPult_, L"СТОЛ СТОП", 1335, r2, 236, rh, false, kCmdTableStop, cYe, L"держать");
}

}  // namespace tantra::mechscreen
