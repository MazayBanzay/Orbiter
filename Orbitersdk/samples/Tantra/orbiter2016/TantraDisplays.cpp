// TantraDisplays: the commander's touch screens. See TantraDisplays.h.
#include "TantraDisplays.h"
#include "Tantra.h"
#include "InteriorLayout.h"
#include "PanelLayout.h"
#include "TantraSafety.h"
#include "Sketchpad2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace tantra::interior;

namespace {

// surfaces: the height is fixed, the width follows the screen's aspect in InteriorLayout.h (kTouch[k].w / .h); the curved
// monitors are laid out for 1024 x 704 (0.755 x 0.52 m), the others stretch with their width; the engine console is drawn off
// screen (1024 x 428) and shown scaled on the right riser
// (all the layouts below are in DESIGN pixels: the surfaces are kSc times bigger, the paint scales everything)
constexpr int kEngH = 428;
const int kH[8] = {704, 704, 432, 300, 300, 230, 400, 400};
int gW[8] = {1024, 1024, 1021, 1059, 1059, 1017, 908, 908};
const double kSc[8] = {1.0, 1.0, 1.5, 1.6, 1.6, 1.5, 1.5, 1.5};

// colours: Sketchpad 0xBBGGRR; atlas colours 0 cyan, 1 orange, 2 white, 3 red, 4 dim blue, 5 brown, 6 grey
// the suit's theme (the user's reference: the suit HUD): a dark neutral field, thin orange lines, orange text, cyan values
constexpr DWORD kBezel = 0x1A1918, kGlass = 0x141312, kKeyOff = 0x1A1918, kKeyOn = 0x1C3048, kEdge = 0x285A96;
constexpr DWORD kOrangeL = 0x308AE8, kOrangeD = 0x24507E, kTrack = 0x302E2C;
constexpr DWORD kBarDim = kTrack, kCyanF = 0xC8A040, kAmberF = 0x2890E0, kGreenF = 0x50C060, kRedF = 0x3030D0;
enum { cCyan = 0, cOrange = 1, cWhite = 2, cRed = 3, cDim = 4, cGrey = 6 };

struct R4 { int x0, y0, x1, y1; bool In(double x, double y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; } };

// one draw pass: the sketchpad, the atlas and a few brushes / pens made on demand
class Paint {
public:
    Paint(SURFHANDLE s, TantraGlyphs& gl, double sc = 1.0) : g_(gl), k_(sc) { skp_ = oapiGetSketchpad(s); if (skp_) skp_->SetBackgroundMode(oapi::Sketchpad::BK_TRANSPARENT); }
    double K() const { return k_; }
    ~Paint() {
        if (!skp_) return;
        skp_->SetPen(nullptr); skp_->SetBrush(nullptr);
        for (auto& b : br_) oapiReleaseBrush(b.second);
        for (auto& p : pn_) oapiReleasePen(p.second);
        oapiReleaseSketchpad(skp_);
    }
    bool Ok() const { return skp_ != nullptr; }
    oapi::Sketchpad* Skp() { return skp_; }
    void Box(int x0, int y0, int x1, int y1, DWORD fill, DWORD edge = 0xFFFFFFFF) {
        skp_->SetBrush(Brush(fill)); skp_->SetPen(edge == 0xFFFFFFFF ? nullptr : Pen(edge, 1));
        skp_->Rectangle(S(x0), S(y0), S(x1), S(y1));
    }
    void Box(const R4& r, DWORD fill, DWORD edge = 0xFFFFFFFF) { Box(r.x0, r.y0, r.x1, r.y1, fill, edge); }
    void Line(int x0, int y0, int x1, int y1, DWORD c, int w = 2) { skp_->SetPen(Pen(c, (std::max)(1, int(w * k_ + .5)))); skp_->Line(S(x0), S(y0), S(x1), S(y1)); }
    void Circle(int cx, int cy, int r, DWORD c, int w = 2) { skp_->SetBrush(nullptr); skp_->SetPen(Pen(c, (std::max)(1, int(w * k_ + .5)))); skp_->Ellipse(S(cx - r), S(cy - r), S(cx + r), S(cy + r)); }
    int S(double v) const { return int(v * k_ + .5); }
    // text: (x, top) in pixels, px = capital height, align 0 left, 1 centre, 2 right
    void Text(double x, double top, const char* t, double px, int col, int align = 0) {
        x *= k_; top *= k_; px *= k_;
        if (g_.Ok()) { g_.DrawA(skp_, x, top, t, px >= 20 ? 3 : px >= 14 ? 2 : 1, col, px, align); return; }
        static const DWORD c[7] = {0xFFDC5A, 0x3CA0FF, 0xFFFFFF, 0x4040F0, 0xA58E5E, 0x527BA3, 0x9C8D7D};
        skp_->SetTextColor(c[col]);
        skp_->SetTextAlign(align == 0 ? oapi::Sketchpad::LEFT : align == 1 ? oapi::Sketchpad::CENTER : oapi::Sketchpad::RIGHT);
        skp_->Text(int(x), int(top), t, int(std::strlen(t)));
    }
    // a key, virtual as in the suit: a dark field in a thin orange line, the label in orange; on: a warm field, a bright 2 px
    // line, a light label; warn: a red line
    void Key(const R4& r, const char* t, bool on, bool warn = false, bool dim = false) {
        const int h = r.y1 - r.y0, w = r.x1 - r.x0;
        const DWORD line = warn ? 0x3C3CD8 : on ? kOrangeL : kOrangeD;
        Box(r, on ? kKeyOn : kKeyOff, line);
        if (on) Box(r.x0 + 1, r.y0 + 1, r.x1 - 1, r.y1 - 1, kKeyOn, line);
        if (!t || !*t) return;
        double px = (std::min)(15.0, h * 0.34);
        if (g_.Ok()) { const double tw = g_.Width(t, px >= 14 ? 2 : 1, px); if (tw > w - 10) px *= (w - 10) / tw; }
        Text((r.x0 + r.x1) / 2.0, r.y0 + (h - px) / 2, t, px, dim ? cGrey : on ? cWhite : warn ? cRed : cOrange, 1);
    }
    // a bar as in the suit: a thin dark track, the fill in the given colour; horizontal or vertical (from the bottom)
    void Bar(const R4& r, double f, DWORD c, bool vertical = false) {
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        Box(r, kTrack);
        if (vertical) { const int h = int((r.y1 - r.y0) * f); if (h > 1) Box(r.x0, r.y1 - h, r.x1, r.y1, c); }
        else { const int w = int((r.x1 - r.x0) * f); if (w > 1) Box(r.x0, r.y0, r.x0 + w, r.y1, c); }
    }
    // a panel as in the suit: a thin orange frame with brighter corner brackets
    void Panel(const R4& r) {
        Box(r, kGlass, kOrangeD);
        const int c = (std::min)(14, (std::min)(r.x1 - r.x0, r.y1 - r.y0) / 4);
        for (int sx = 0; sx < 2; sx++) for (int sy = 0; sy < 2; sy++) {
            const int x = sx ? r.x1 : r.x0, y = sy ? r.y1 : r.y0, dx = sx ? -c : c, dy = sy ? -c : c;
            Line(x, y, x + dx, y, kOrangeL, 2); Line(x, y, x, y + dy, kOrangeL, 2);
        }
    }

private:
    oapi::Brush* Brush(DWORD c) { for (auto& b : br_) if (b.first == c) return b.second; br_.push_back({c, oapiCreateBrush(c)}); return br_.back().second; }
    oapi::Pen* Pen(DWORD c, int w) { const DWORD k = c | (DWORD(w) << 24); for (auto& p : pn_) if (p.first == k) return p.second; pn_.push_back({k, oapiCreatePen(1, w, c)}); return pn_.back().second; }
    oapi::Sketchpad* skp_ = nullptr;
    TantraGlyphs& g_;
    double k_ = 1.0;
    std::vector<std::pair<DWORD, oapi::Brush*>> br_;
    std::vector<std::pair<DWORD, oapi::Pen*>> pn_;
};

// ---- the elbow displays: the mode keys along the bottom, the MFD blocks, the system pages (sections of panel.dds) ----
const char* const kModeName[TantraDisplays::M_COUNT] = {"MFD", "РАБ. ТЕЛО", "ЭНЕРГОУСТ.", "ЭКИПАЖ", "ЛИФТЫ", "МЕХАНИЗАЦИЯ", "КОРПУС", "НАСТР.", "ВЫКЛ"};
const char* const kModeTitle[TantraDisplays::M_COUNT] = {"MFD", "РАБОЧЕЕ ТЕЛО: АНАМЕЗОН, ЛОВУШКИ", "ЭНЕРГОУСТАНОВКА И КОМПЕНСАТОР",
                                                         "ЭКИПАЖ", "ЛИФТЫ: АНГАР, ПОРТ, ШЛЮЗ", "МЕХАНИЗАЦИЯ: ПОЛОЖЕНИЕ, ЛАФЕТ, ШАССИ, ГРЕБНИ, КИЛЬ, ГОНДОЛЫ", "КОРПУС", "НАСТРОЙКИ: MFD И HUD", ""};
R4 ModeKey(int i) { return {8 + i * 126, 622, 8 + i * 126 + 118, 696}; }   // 8 mode keys
// the settings page: rows (label, value, - / +), the palette keys
R4 SetKey(int row, int plus) { return {640 + plus * 150, 80 + row * 110, 640 + plus * 150 + 130, 80 + row * 110 + 80}; }
R4 PalKey(int i) { return {300 + i * 330, 410, 300 + i * 330 + 310, 490}; }
R4 LumKey() { return {300, 515, 560, 580}; }   // ПО СВЕТУ (the automatic brightness)
R4 ResKey(int i) { return {580 + i * 215, 515, 580 + i * 215 + 200, 580}; }
constexpr R4 kContent = {8, 44, 1016, 612};
struct Sec { int x0, y0, x1, y1; };                       // a section of panel.dds (texture px)
struct PageDef { int n; Sec s[3]; };
const PageDef kPage[TantraDisplays::M_COUNT] = {
    {0, {}},
    {2, {{10, 6, 338, 398}, {342, 6, 630, 398}}},          // рабочее тело: the anamezon store, the traps
    {2, {{638, 6, 910, 398}, {918, 6, 1206, 398}}},        // энергоустановка: the plant, the compensator
    {1, {{1214, 6, 1592, 398}}},                           // экипаж
    {1, {{1034, 1486, 1592, 1874}}},                       // лифты: hangar, port, airlock lift
    {3, {{10, 1486, 346, 1874}, {350, 1486, 706, 1874}, {714, 1486, 1024, 1874}}},  // механизация: pose mimic, carriage & gear, hull with crests/keel fold and pods
    {1, {{714, 1486, 1024, 1874}}},                        // корпус
    {0, {}},                                               // настройки
    {0, {}},
};
// where the sections of a page are drawn (aspect kept, side by side)
int PageRects(const PageDef& p, R4* out) {
    if (!p.n) return 0;
    double sw = 0, sh = 0;
    for (int i = 0; i < p.n; i++) { sw += p.s[i].x1 - p.s[i].x0; sh = (std::max)(sh, double(p.s[i].y1 - p.s[i].y0)); }
    const double gap = 16, aw = kContent.x1 - kContent.x0 - 16 - gap * (p.n - 1), ah = kContent.y1 - kContent.y0 - 16;
    const double k = (std::min)(aw / sw, ah / sh);
    double x = kContent.x0 + 8 + (aw - sw * k) / 2;
    for (int i = 0; i < p.n; i++) {
        const int w = int((p.s[i].x1 - p.s[i].x0) * k), h = int((p.s[i].y1 - p.s[i].y0) * k);
        const int y = kContent.y0 + 8 + int((ah - h) / 2);
        out[i] = {int(x), y, int(x) + w, y + h};
        x += w + gap;
    }
    return p.n;
}
// MFD blocks: two per display; the display square, the side label columns, the PWR / SEL / MNU keys
R4 MfdDisp(int b) { const int x0 = 8 + b * 504; return {x0 + 52, 50, x0 + 452, 450}; }
R4 MfdSide(int b, int i) { const int x0 = 8 + b * 504, row = i % 6; const int y = 50 + row * 400 / 6; return i < 6 ? R4{x0 + 4, y + 4, x0 + 48, y + 400 / 6 - 4} : R4{x0 + 456, y + 4, x0 + 500, y + 400 / 6 - 4}; }
R4 MfdBottom(int b, int j) { const int x0 = 8 + b * 504; return {x0 + 52 + j * 136, 462, x0 + 52 + j * 136 + 128, 504}; }

// the panel areas one can click (as the 2D panels register them)
bool ClickPanel(Tantra* t, double tx, double ty) {
    using namespace tantra::panel;
    static const int kClick[] = {A_LEVER, A_SEL_PLAN, A_SEL_ANA, A_PODS_AFT, A_PODS_DOWN, A_STOP, A_TRAPS, A_TRAPSEL, A_GLIM, A_GSTEP,
                                 A_OVERRIDE, A_AIRLOCK, A_EVA, A_CREWSEL, M_SEL_PLAN, M_SEL_ANA, M_START, M_STOP, L_GEAR, L_SET_LEVEL,
                                 L_SET_STAND, L_ERECT, L_PORT, L_CRESTS, L_PODS_AFT, L_PODS_DOWN, L_HANGAR, L_ROVERS, L_AIRLOCK, L_EVA,
                                 L_CREWSEL, L_PT_LIFT, L_PT_LOAD, L_PT_DROP, L_PT_STOP};
    for (int a : kClick) {
        const int* r = kArea[a];
        if (tx >= r[0] && tx <= r[2] && ty >= r[1] && ty <= r[3]) { t->clbkPanelMouseEvent(a, PANEL_MOUSE_LBDOWN, int(tx - r[0]), int(ty - r[1]), nullptr); return true; }
    }
    return false;
}

// ---- the engine console ----
constexpr R4 kEngGlass = {10, 8, 1014, 352};
R4 EngKey(int i) { return {10 + i * 168, 362, 10 + i * 168 + 160, 420}; }       // 6 system keys
R4 PhaseKey(int i) { return {26, 64 + i * 66, 246, 64 + i * 66 + 56}; }        // field, beam, feed
constexpr R4 kTrapKey = {742, 296, 1000, 340};
constexpr R4 kPodsAft = {600, 236, 790, 288}, kPodsDown = {800, 236, 990, 288};
constexpr R4 kMarchBar = {26, 86, 246, 104}, kPodBar = {300, 86, 560, 104};   // planetary: a touch on a bar sets the level
R4 CupBar(int i) { const int x = 276 + i * 88; return {x + 16, 86, x + 56, 280}; } // anamezon cups 1..4 (one common level)
bool InGrown(const R4& r, double x, double y, int m = 12) { return x >= r.x0 - m && x <= r.x1 + m && y >= r.y0 - m && y <= r.y1 + m; }
double AlongX(const R4& r, double x) { const double f = (x - r.x0) / double(r.x1 - r.x0); return f < 0 ? 0 : f > 1 ? 1 : f; }
double AlongUp(const R4& r, double y) { const double f = (r.y1 - y) / double(r.y1 - r.y0); return f < 0 ? 0 : f > 1 ? 1 : f; }

// sections of a page fitted into an area (aspect kept, side by side, centred)
int FitSections(const PageDef& p, const R4& a, R4* out) {
    if (!p.n) return 0;
    double sw = 0, sh = 0;
    for (int i = 0; i < p.n; i++) { sw += p.s[i].x1 - p.s[i].x0; sh = (std::max)(sh, double(p.s[i].y1 - p.s[i].y0)); }
    const double gap = 8, aw = a.x1 - a.x0 - gap * (p.n - 1), ah = a.y1 - a.y0;
    const double k = (std::min)(aw / sw, ah / sh);
    double x = a.x0 + (aw - sw * k) / 2;
    for (int i = 0; i < p.n; i++) {
        const int w = int((p.s[i].x1 - p.s[i].x0) * k), h = int((p.s[i].y1 - p.s[i].y0) * k);
        const int y = a.y0 + int((ah - h) / 2);
        out[i] = {int(x), y, int(x) + w, y + h};
        x += w + gap;
    }
    return p.n;
}
// the ship redraws the 2D panel areas a page shows (into panel.dds)
void RedrawSections(Tantra* t, SURFHANDLE pt, const PageDef& p) {
    if (!pt) return;
    using namespace tantra::panel;
    for (int a = 0; a < A_COUNT; a++) {
        if (a >= M_MFD2_L && a <= M_MFD3_B) continue;
        const int* ar = kArea[a];
        for (int i = 0; i < p.n; i++)
            if (ar[0] < p.s[i].x1 && ar[2] > p.s[i].x0 && ar[1] < p.s[i].y1 && ar[3] > p.s[i].y0) { t->clbkPanelRedrawEvent(a, PANEL_REDRAW_USER, pt, nullptr); break; }
    }
}
void BlitSections(SURFHANDLE s, SURFHANDLE pt, const PageDef& p, const R4* rr, int n, double k = 1.0) {
    if (!pt) return;
    for (int i = 0; i < n; i++) {
        RECT dr = {LONG(rr[i].x0 * k), LONG(rr[i].y0 * k), LONG(rr[i].x1 * k), LONG(rr[i].y1 * k)}, sr = {p.s[i].x0, p.s[i].y0, p.s[i].x1, p.s[i].y1};
        oapiBlt(s, pt, &dr, &sr);
    }
}
bool ClickSections(Tantra* t, const PageDef& p, const R4* rr, int n, double x, double y) {
    for (int i = 0; i < n; i++)
        if (rr[i].In(x, y)) {
            const double tx = p.s[i].x0 + (x - rr[i].x0) / double(rr[i].x1 - rr[i].x0) * (p.s[i].x1 - p.s[i].x0);
            const double ty = p.s[i].y0 + (y - rr[i].y0) / double(rr[i].y1 - rr[i].y0) * (p.s[i].y1 - p.s[i].y0);
            ClickPanel(t, tx, ty);
            return true;
        }
    return false;
}

// ---- the concave centre screen: ГЛАВНЫЙ ЭКРАН | ПОЛЁТ | ДВИГАТЕЛИ (its top an arch: the side zones keep their content low) ----
const int kHud[4] = {HUD_ORBIT, HUD_SURFACE, HUD_DOCKING, HUD_NONE};
const char* const kHudName[4] = {"ОРБИТА", "ПОВЕРХН.", "СТЫКОВКА", "ВЫКЛ"};
const int kRcs[3] = {RCS_ROT, RCS_LIN, RCS_NONE};
const char* const kRcsName[3] = {"ВРАЩЕНИЕ", "ЛИНЕЙНОЕ", "ВЫКЛ"};
R4 CZoneL() { return {0, 0, int(gW[2] * .245), kH[2]}; }
R4 CZoneC() { return {int(gW[2] * .25), 0, int(gW[2] * .755), kH[2]}; }
R4 CZoneR() { return {int(gW[2] * .76), 0, gW[2], kH[2]}; }
R4 CHud(int i) { const R4 z = CZoneL(); const int w = (z.x1 - z.x0 - 30) / 2; return {z.x0 + 10 + (i % 2) * (w + 10), 168 + (i / 2) * 50, z.x0 + 10 + (i % 2) * (w + 10) + w, 168 + (i / 2) * 50 + 42}; }
R4 CRcs(int i) { const R4 z = CZoneL(); const int w = (z.x1 - z.x0 - 30) / 2; return {z.x0 + 10 + (i % 2) * (w + 10), 316 + (i / 2) * 50, z.x0 + 10 + (i % 2) * (w + 10) + w, 316 + (i / 2) * 50 + 42}; }
R4 CGlass() { const R4 z = CZoneC(); return {z.x0 + 6, 8, z.x1 - 6, kH[2] - 8}; }
R4 CAck() { const R4 g = CGlass(); return {g.x1 - 170, kH[2] - 66, g.x1 - 14, kH[2] - 20}; }   // ПОДТВЕРДИТЬ (the alerts)
R4 CTab(int i) { const R4 g = CGlass(); return {g.x1 - 236 + i * 118, 12, g.x1 - 124 + i * 118, 40}; }   // КОСМОС, АТМОСФЕРА
R4 CThrust() { const R4 z = CZoneR(); return {z.x0 + 28, 182, z.x0 + 84, 398}; }                   // the thrust bar (touch: set)
R4 CFuel() { const R4 z = CZoneR(); return {z.x0 + 108, 182, z.x0 + 132, 398}; }
// a half-plane clip of a polygon: keep the points with a*x + b*y + c >= 0
int ClipHalf(const double* in, int n, double a, double b, double c, double* out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const double* p = in + 2 * i; const double* q = in + 2 * ((i + 1) % n);
        const double dp = a * p[0] + b * p[1] + c, dq = a * q[0] + b * q[1] + c;
        if (dp >= 0) { out[2 * m] = p[0]; out[2 * m + 1] = p[1]; m++; }
        if ((dp >= 0) != (dq >= 0)) { const double t = dp / (dp - dq); out[2 * m] = p[0] + t * (q[0] - p[0]); out[2 * m + 1] = p[1] + t * (q[1] - p[1]); m++; }
    }
    return m;
}

// ---- the attitude keys (the shelf in front of him; the texture's top is the far edge) ----
struct AutoKey { const char* name; int mode; };
constexpr int kAutoN = 8, kNav = 7;                                     // Orbiter's 7 autopilots and РУЧН. (all off)
const AutoKey kAuto[kAutoN] = {{"СТОП ВРАЩ.", NAVMODE_KILLROT}, {"ГОРИЗОНТ", NAVMODE_HLEVEL}, {"ПРОГРАД", NAVMODE_PROGRADE}, {"РЕТРОГРАД", NAVMODE_RETROGRADE},
                               {"НОРМАЛЬ +", NAVMODE_NORMAL}, {"НОРМАЛЬ -", NAVMODE_ANTINORMAL}, {"ВЫСОТА", NAVMODE_HOLDALT}, {"РУЧН.", -1}};
R4 AutoKeyR(int i) { const int w = (gW[5] - 40) / kAutoN; return {20 + i * w + 8, 60, 20 + (i + 1) * w - 8, kH[5] - 30}; }

// ---- the risers: two pages side by side ----
// right riser: ЭНЕРГИЯ (the panel page) | the engine console at full height; left: МЕХАНИЗАЦИЯ (3 sections) | ПОЛОЖЕНИЕ
R4 EngRect() { const int h = kH[3] - 12, w = int(h * 1024.0 / kEngH); return {gW[3] - 6 - w, 6, gW[3] - 6, 6 + h}; }
R4 RiserHalf(int k, int i) {
    if (k == 3) { const R4 e = EngRect(); return i == 0 ? R4{6, 26, e.x0 - 10, kH[3] - 6} : e; }
    const int split = int(gW[4] * .64); return i == 0 ? R4{6, 26, split - 5, kH[4] - 6} : R4{split + 5, 6, gW[4] - 6, kH[4] - 6};
}
R4 PoseRcs(int i) { const R4 h = RiserHalf(4, 1); const int w = (h.x1 - h.x0 - 30) / 4; return {h.x0 + 6 + i * (w + 6), h.y1 - 62, h.x0 + 6 + i * (w + 6) + w, h.y1 - 8}; }

// ---- the wings' shelves ----
struct MechKey { const char* name; int act; };
enum { MK_GEAR, MK_LEVEL, MK_STAND, MK_ERECT, MK_CRESTS, MK_PODS_AFT, MK_PODS_DOWN, MK_HANGAR, MK_PORT, MK_AIRLOCK, MK_PT_LIFT, MK_PT_STOP };
const char* const kMechName[12] = {"ШАССИ", "ЛЁЖА", "СТОЯ", "ПОДЪЁМ", "ГРЕБНИ", "ГОНД. НАЗАД", "ГОНД. ВНИЗ", "АНГАР", "ПОРТ", "ШЛЮЗ", "СТОЛ ВВЕРХ", "СТОЛ СТОП"};
R4 MechKeyR(int i) { return {14 + (i % 6) * 92, 90 + (i / 6) * 130, 14 + (i % 6) * 92 + 80, 90 + (i / 6) * 130 + 104}; }
constexpr Sec kMimic = {10, 1486, 346, 1874};                             // the pose mimic of the LOWER panel: the check display
R4 CheckR() { return {574, 40, gW[6] - 10, kH[6] - 12}; }
const char* const kFoldName[3] = {"РАЗВЕРНУТЬ", "СВЕРНУТЬ", "СЛОЖИТЬ"};
R4 FoldKeyR(int i) { return {14, 90 + i * 96, 214, 90 + i * 96 + 80}; }
const char* const kCalcKey[20] = {"7", "8", "9", "/", "C", "4", "5", "6", "x", "кор", "1", "2", "3", "-", "%", "0", ",", "=", "+", "±"};
constexpr int kCalcX = 520, kCalcY = 150, kCalcKw = 62, kCalcKh = 46;  // the calculator: 5 x 4 keys of one size under its display
R4 CalcKeyR(int i) { return {kCalcX + (i % 5) * (kCalcKw + 6), kCalcY + (i / 5) * (kCalcKh + 6), kCalcX + (i % 5) * (kCalcKw + 6) + kCalcKw, kCalcY + (i / 5) * (kCalcKh + 6) + kCalcKh}; }

}  // namespace

// ============================================================================================================

// The light at the ship (the auto setting): 1 day, 0 night - the sun's elevation over the local horizon, dusk between
// (above 300 km: sunlit). The displays follow it: brighter and harder by day, softer at night.
double TantraDisplays::DayLight() const {
    OBJHANDLE pl = t_->GetSurfaceRef(), sun = oapiGetGbodyByIndex(0);
    if (!pl || !sun || pl == sun) return 1.0;
    VECTOR3 sp, pp, ss; t_->GetGlobalPos(sp); oapiGetGlobalPos(pl, &pp); oapiGetGlobalPos(sun, &ss);
    if (length(sp - pp) - oapiGetSize(pl) > 300e3) return 1.0;
    const double e = dotp(unit(sp - pp), unit(ss - sp));
    return e < -0.1 ? 0.0 : e > 0.15 ? 1.0 : (e + 0.1) / 0.25;
}
double TantraDisplays::MfdGain() const { return autoLum_ ? 1.1 + 0.7 * DayLight() : mfdGain_; }
double TantraDisplays::MfdGamma() const { return autoLum_ ? 0.85 - 0.15 * DayLight() : mfdGamma_; }
double TantraDisplays::HudGain() const { return autoLum_ ? 0.75 + 0.45 * DayLight() : hudGain_; }

void TantraDisplays::OnVisual(VISHANDLE vis) {
    DEVMESHHANDLE dm = t_->GetDevMesh(vis, vcMesh_);
    const DWORD f = OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_NOMIPMAPS;
    int bound = 0;
    for (int k = 0; k < kScreens && k < kTouchCount; k++) {               // the width from the screen's aspect (InteriorLayout.h)
        if (!s_[k] && kTouch[k].h > 0) {
            const int w = int(kH[k] * kTouch[k].w / kTouch[k].h + 0.5);
            if (k <= kRight) { if (std::abs(w - gW[k]) > gW[k] / 30) oapiWriteLogV("Tantra displays: monitor %d aspect %.3f differs from the layout (%d px wide, laid out for %d)", k, kTouch[k].w / kTouch[k].h, w, gW[k]); }
            else gW[k] = (std::max)(w, 480);
        }
        if (!s_[k]) s_[k] = oapiCreateSurfaceEx(int(gW[k] * kSc[k] + .5), int(kH[k] * kSc[k] + .5), f);
        if (dm && s_[k] && oapiSetTexture(dm, kTouch[k].slot, s_[k])) bound++;
    }
    if (!eng_) eng_ = oapiCreateSurfaceEx(1024, kEngH, f);
    oapiWriteLogV("Tantra displays: %d of %d touch screens bound", bound, int(kScreens));
    t_redraw_ = 0.0;
}

void TantraDisplays::Step(double dt) {
    Alerts();
    if ((t_redraw_ -= dt) > 0.0) return;
    t_redraw_ = 0.2;
    DrawSide(kLeft); DrawSide(kRight); DrawCentre(); DrawEngines(); DrawRiser(kRiserR); DrawRiser(kRiserL); DrawKeys(); DrawWing(kWingL); DrawWing(kWingR);
}

bool TantraDisplays::Touch(int screen, double u, double v) {
    if (screen < 0 || screen >= kScreens) return false;
    const double x = u * gW[screen], y = v * kH[screen];
    bool r = false;
    switch (screen) {
        case kLeft: case kRight: r = TouchSide(screen, x, y); break;
        case kCentre: r = TouchCentre(x, y); break;
        case kRiserR: case kRiserL: r = TouchRiser(screen, x, y); break;
        case kKeys: r = TouchKeys(x, y); break;
        default: r = TouchWing(screen, x, y); break;
    }
    t_redraw_ = 0.0;                                                      // show the result at once
    return r;
}

void TantraDisplays::Shutdown() {
    for (SURFHANDLE& s : s_) if (s) { oapiDestroySurface(s); s = nullptr; }
    if (eng_) { oapiDestroySurface(eng_); eng_ = nullptr; }
}

// ---- the concave centre screen -----------------------------------------------------------------------------

void TantraDisplays::DrawCentre() {
    SURFHANDLE s = s_[kCentre];
    if (!s) return;
    Tantra* t = t_;
    const bool autoAtmo = t->GetAltitude() < 100e3 && t->GetAtmPressure() > 0.0;
    if (autoAtmo != lastAutoAtmo_) { lastAutoAtmo_ = autoAtmo; termMode_ = -1; }
    const bool atmo = termMode_ < 0 ? autoAtmo : termMode_ == 1;
    oapiClearSurface(s, 0xFF000000 | kGlass);
    Paint P(s, glyphs_, kSc[kCentre]);
    if (!P.Ok()) return;
    char b[128];
    // ГЛАВНЫЙ ЭКРАН: what the big screen shows over the view (the HUD) and the RCS mode
    const R4 zl = CZoneL(), zr = CZoneR(), G = CGlass();
    P.Line(zl.x1 + 2, 30, zl.x1 + 2, kH[2] - 8, 0x48381D, 3); P.Line(zr.x0 - 3, 30, zr.x0 - 3, kH[2] - 8, 0x48381D, 3);
    P.Text((zl.x0 + zl.x1) / 2.0, 112, "ГЛАВНЫЙ ЭКРАН", 16, cWhite, 1);
    P.Text(zl.x0 + 12, 146, "HUD", 12, cGrey);
    const int hud = hudMode_, rcs = t->GetAttitudeMode();
    for (int i = 0; i < 4; i++) P.Key(CHud(i), kHudName[i], hud == kHud[i]);
    P.Text(zl.x0 + 12, 294, "РСУ", 12, cGrey);
    for (int i = 0; i < 3; i++) P.Key(CRcs(i), kRcsName[i], rcs == kRcs[i]);
    // ДВИГАТЕЛИ: the thrust (touch the bar), the fuel, the g limit
    const bool ana = t->engineSet_ == Tantra::EngineSet::Anamezon;
    const double thr = t->GetThrusterGroupLevel(THGROUP_MAIN), mx = t->GetMaxFuelMass(), fuel = mx > 0 ? t->GetFuelMass() / mx : 0.0;
    P.Text((zr.x0 + zr.x1) / 2.0, 100, "ДВИГАТЕЛИ", 16, cWhite, 1);
    static const char* const kStage[4] = {"ВЫКЛ", "ПОЛЕ", "ЛУЧ", "ПОДАЧА"};
    if (ana) std::snprintf(b, sizeof b, "анамезон: %s", kStage[int(t->ignition_.Stage()) & 3]);
    else std::snprintf(b, sizeof b, "планетарные (%s)", t->marchHigh_ ? "железо" : "аргон");
    P.Text((zr.x0 + zr.x1) / 2.0, 128, b, 11, ana ? cOrange : cCyan, 1);
    P.Bar(CThrust(), thr, kCyanF, true); P.Bar(CFuel(), fuel, fuel < .1 ? kRedF : kGreenF, true);
    std::snprintf(b, sizeof b, "%.0f%%", thr * 100); P.Text((CThrust().x0 + CThrust().x1) / 2.0, 404, b, 13, cWhite, 1);
    P.Text((CThrust().x0 + CThrust().x1) / 2.0, 162, "тяга", 11, cGrey, 1);
    P.Text((CFuel().x0 + CFuel().x1) / 2.0, 162, "топл.", 11, cGrey, 1);
    std::snprintf(b, sizeof b, "g %.2f", t->accelG_); P.Text(zr.x0 + 146, 200, b, 13, t->gLimitOn_ && t->accelG_ > t->gLimit_ * .9 ? cRed : cCyan);
    std::snprintf(b, sizeof b, "пред. %s", t->gLimitOn_ ? "" : "выкл"); if (t->gLimitOn_) std::snprintf(b, sizeof b, "пред. %.1f", t->gLimit_);
    P.Text(zr.x0 + 146, 226, b, 11, cGrey);
    // ПОЛЁТ
    P.Box(G, 0x0D1606, 0x303830);
    P.Key(CTab(0), "КОСМОС", !atmo); P.Key(CTab(1), "АТМОСФЕРА", atmo);
    int ap = -1; for (int i = 0; i < kNav; i++) if (t->GetNavmodeState(kAuto[i].mode)) { ap = i; break; }
    std::snprintf(b, sizeof b, "автопилот: %s", ap >= 0 ? kAuto[ap].name : "ручное");
    P.Box(G.x0 + 8, kH[2] - 70, G.x1 - 8, kH[2] - 16, 0x1A1008, 0x404840);
    if (const int lv = AlertLevel()) {                                    // an alert: the newest active one and ПОДТВЕРДИТЬ
        const Alert* a = nullptr; for (const Alert& x : alerts_) if (x.active) { a = &x; break; }
        P.Box(G.x0 + 8, kH[2] - 70, G.x0 + 14, kH[2] - 16, lv == 2 ? 0x4646FF : 0xFFE246);
        P.Text(G.x0 + 26, kH[2] - 64, lv == 2 ? "ОПАСНОСТЬ" : "ВНИМАНИЕ", 12, lv == 2 ? cRed : cCyan);
        if (a) P.Text(G.x0 + 26, kH[2] - 42, a->text, 12, cWhite);
        P.Key(CAck(), "ПОДТВЕРДИТЬ", AlertUnacked() && std::fmod(oapiGetSimTime(), 1.0) < .62, true);
    } else {
    P.Box(G.x0 + 8, kH[2] - 70, G.x0 + 14, kH[2] - 16, ap >= 0 ? 0x2890E0 : 0x605850);
    P.Text(G.x0 + 26, kH[2] - 60, b, 14, ap >= 0 ? cOrange : cGrey);
    std::snprintf(b, sizeof b, "РСУ: %s", rcs == RCS_ROT ? "вращение" : rcs == RCS_LIN ? "линейное" : "выкл"); P.Text(G.x1 - 22, kH[2] - 60, b, 12, cGrey, 2);
    }
    const double pitch = t->GetPitch(), bank = t->GetBank();
    const double ca = std::cos(bank), sa = std::sin(bank);
    VECTOR3 hv; t->GetHorizonAirspeedVector(hv);
    double hdg = 0.0; oapiGetHeading(t->GetHandle(), &hdg);
    const double alt = t->GetAltitude(), tas = t->GetAirspeed();
    if (!atmo) {                                                          // SPACE: a horizon, the readouts
        P.Text(G.x0 + 16, 18, "ПОЛЁТ · КОСМОС", 16, cWhite);
        const int cx = (G.x0 + G.x1) / 2, cy = 196, r = 92;
        P.Circle(cx, cy, r, 0x60C060);
        const double k = r / 0.7;                                         // 0.7 rad from the centre to the rim
        for (int deg = -30; deg <= 30; deg += 10) {
            const double off = (pitch - deg * RAD) * k;
            if (std::fabs(off) > r * 0.9) continue;
            const double hw = deg == 0 ? r * 0.95 : r * 0.3, px = -sa * off, py = ca * off;
            P.Line(int(cx + px - ca * hw), int(cy + py - sa * hw), int(cx + px + ca * hw), int(cy + py + sa * hw), deg == 0 ? 0x60E070 : 0x407040, deg == 0 ? 2 : 1);
        }
        P.Line(cx - 46, cy, cx - 14, cy, 0x3CA0FF, 3); P.Line(cx + 14, cy, cx + 46, cy, 0x3CA0FF, 3);
        const int lx = G.x0 + 16, rx = G.x1 - 150;
        std::snprintf(b, sizeof b, "V   %.0f м/с", tas); P.Text(lx, 70, b, 14, cCyan);
        std::snprintf(b, sizeof b, "M   %.2f", t->GetMachNumber()); P.Text(lx, 100, b, 13, cCyan);
        std::snprintf(b, sizeof b, "АТАКИ %.0f°", t->GetAOA() * DEG); P.Text(lx, 130, b, 13, cCyan);
        std::snprintf(b, sizeof b, alt > 10000 ? "H   %.1f км" : "H   %.0f м", alt > 10000 ? alt / 1000 : alt); P.Text(rx, 70, b, 14, cCyan);
        std::snprintf(b, sizeof b, "Vy  %+.1f м/с", hv.y); P.Text(rx, 100, b, 13, hv.y < -5 ? cRed : cCyan);
        std::snprintf(b, sizeof b, "КУРС %03.0f°", hdg * DEG); P.Text(rx, 130, b, 13, cCyan);
        return;
    }
    // ATMOSPHERE: a big attitude indicator, speed tape left, altitude tape right, heading tape on top
    P.Text(G.x0 + 16, 18, "ПОЛЁТ · АТМОСФЕРА", 16, cWhite);
    const int cx = (G.x0 + G.x1) / 2, top = 96, bot = 336, cy = (top + bot) / 2, half = (bot - top) / 2;
    const int ax0 = cx - half, ax1 = cx + half;
    const double k = half / (25.0 * RAD);                                 // 25 deg from the centre to the edge
    P.Box(ax0, top, ax1, bot, 0x7A4A20, 0x606860);
    {
        const double sq[8] = {double(ax0), double(top), double(ax1), double(top), double(ax1), double(bot), double(ax0), double(bot)};
        double out[16];
        const double hx = cx - sa * pitch * k, hy = cy + ca * pitch * k;  // a point on the horizon line
        const int m = ClipHalf(sq, 4, -sa, ca, -(-sa * hx + ca * hy), out);   // ground side: n = (-sin, cos), y down
        if (m >= 3) {
            IVECTOR2 pt[8]; for (int i = 0; i < m; i++) { pt[i].x = P.S(out[2 * i]); pt[i].y = P.S(out[2 * i + 1]); }
            oapi::Brush* br = oapiCreateBrush(0x1C3A5A);
            P.Skp()->SetBrush(br); P.Skp()->SetPen(nullptr); P.Skp()->Polygon(pt, m); P.Skp()->SetBrush(nullptr);
            oapiReleaseBrush(br);
        }
    }
    for (int deg = -20; deg <= 20; deg += 5) {                           // the pitch ladder
        const double off = (pitch - deg * RAD) * k;
        if (std::fabs(off) > half * 0.92) continue;
        const double hw = deg == 0 ? half * 1.0 : (deg % 10) ? half * 0.16 : half * 0.32, px = -sa * off, py = ca * off;
        P.Line(int(cx + px - ca * hw), int(cy + py - sa * hw), int(cx + px + ca * hw), int(cy + py + sa * hw), 0xF0F0F0, deg == 0 ? 2 : 1);
    }
    const double gs = std::hypot(hv.x, hv.z), fpa = std::atan2(hv.y, (std::max)(gs, 1.0)), slip = t->GetSlipAngle();
    const double lim = half * .9;
    const int fx = int(cx + (std::max)(-lim, (std::min)(lim, slip * k))), fy = int(cy - (std::max)(-lim, (std::min)(lim, (fpa - pitch) * k)));
    P.Circle(fx, fy, 8, 0x60E070); P.Line(fx - 18, fy, fx - 8, fy, 0x60E070); P.Line(fx + 8, fy, fx + 18, fy, 0x60E070); P.Line(fx, fy - 8, fx, fy - 15, 0x60E070);
    P.Line(cx - 60, cy, cx - 20, cy, 0x3CA0FF, 4); P.Line(cx + 20, cy, cx + 60, cy, 0x3CA0FF, 4); P.Line(cx - 3, cy, cx + 3, cy, 0x3CA0FF, 4);
    {
        const int y0 = 56, y1 = 84, w = ax1 - ax0; const double hd = hdg * DEG, ppd = w / 60.0;     // the heading tape, 60 deg across
        P.Box(ax0, y0, ax1, y1, 0x101410, 0x404840);
        for (int d = int(std::floor((hd - 30) / 5)) * 5; d <= hd + 30; d += 5) {
            const int x = int(cx + (d - hd) * ppd); if (x < ax0 + 2 || x > ax1 - 2) continue;
            P.Line(x, y1 - (d % 10 ? 6 : 12), x, y1, 0xD0D0D0, 1);
            if (d % 10 == 0) { char h[8]; std::snprintf(h, sizeof h, "%03d", ((d % 360) + 360) % 360); P.Text(x, y0 + 3, h, 11, cWhite, 1); }
        }
        P.Line(cx, y0, cx, y1, 0x3CA0FF, 2);
    }
    auto tape = [&](int x0, int x1, double val, double ppu, double tick, int every, bool right) {
        P.Box(x0, top, x1, bot, 0x101410, 0x404840);
        const double lo = val - (cy - top) / ppu, hi = val + (bot - cy) / ppu;
        for (double v = std::floor(lo / tick) * tick; v <= hi; v += tick) {
            const int y = int(cy - (v - val) * ppu); if (y < top + 2 || y > bot - 2) continue;
            const bool big = std::fmod(std::fabs(std::round(v / tick)), double(every)) < 0.5;
            P.Line(right ? x0 : x1 - (big ? 12 : 6), y, right ? x0 + (big ? 12 : 6) : x1, y, 0xD0D0D0, 1);
            if (big) { char l[16]; std::snprintf(l, sizeof l, "%.0f", v); P.Text(right ? x0 + 16 : x1 - 16, y - 6, l, 10, cWhite, right ? 0 : 2); }
        }
        char c[16]; std::snprintf(c, sizeof c, "%.0f", val);
        P.Box(x0 + 2, cy - 13, x1 - 2, cy + 13, 0x000000, 0x3CA0FF); P.Text((x0 + x1) / 2.0, cy - 8, c, 13, cWhite, 1);
    };
    const bool fast = tas > 600, low = alt < 3000;
    tape(G.x0 + 10, ax0 - 12, tas, fast ? 0.2 : 2.0, fast ? 50 : 5, 2, false);
    tape(ax1 + 12, G.x1 - 10, alt, low ? 0.25 : 0.01, low ? 50 : 1000, 5, true);
    std::snprintf(b, sizeof b, "АТАКИ %.1f°  СКОЛЬЖ. %.1f°  ГОНДОЛЫ %.0f°  ГЛИССАДА %+.1f°", t->GetAOA() * DEG, slip * DEG, t->podAngle_, fpa * DEG);
    P.Text(cx, 342, b, 10, cCyan, 1);
}

bool TantraDisplays::TouchCentre(double x, double y) {
    Tantra* t = t_;
    for (int i = 0; i < 2; i++) if (CTab(i).In(x, y)) { termMode_ = i; return true; }
    if (AlertLevel() && CAck().In(x, y)) { AckAlerts(); return true; }
    for (int i = 0; i < 4; i++) if (CHud(i).In(x, y)) { hudMode_ = kHud[i]; return true; }   // the ship's own HUD (Orbiter's follows the focus, the person)
    for (int i = 0; i < 3; i++) if (CRcs(i).In(x, y)) { t->SetAttitudeMode(kRcs[i]); return true; }
    if (InGrown(CThrust(), x, y)) { SetMainLevel(AlongUp(CThrust(), y)); return true; }
    return false;
}

// The thrust set by a touch on a scale: the same set-points as the keys (the main group drives the anamezon cups or the
// marching cup; the pods follow it unless they hover, then the hover group). The ship applies its caps (g, safety) each step.
void TantraDisplays::SetMainLevel(double f) { t_->SetThrusterGroupLevel(THGROUP_MAIN, f); }
void TantraDisplays::SetPodLevel(double f) {
    if (t_->GetGroupThrusterCount(THGROUP_HOVER) > 0) t_->SetThrusterGroupLevel(THGROUP_HOVER, f);
    else t_->SetThrusterGroupLevel(THGROUP_MAIN, f);
}

// ---- the HUD on the big screen's picture ----------------------------------------------------------------------
// The suit computer's way (Архитектор - скафандр, 2026-10-03): a logical grid 1280 x 720 scaled by k = H / 720 and centred on the
// picture; the palette CP / CA / CW / CD / CR (the user's «оранж + циан», or swapped); every solid line over a wider faint copy of
// itself (glow); soft dark bands at the top and the bottom instead of boxes; the alerts - lamps of the systems, the word, the ribbon
// with the time, a one-off message under it; blinking until acknowledged (the ПОДТВЕРДИТЬ key of the centre screen).

namespace {
struct Pal { DWORD P, A, W, D, R; int tP, tA, tD; };                      // line colours 0xAABBGGRR without alpha; atlas colours
const Pal kPal[2] = {{0x2894FF, 0xFFE246, 0xFFF8F2, 0x527BA3, 0x4646FF, cOrange, cCyan, 5},
                     {0xFFE246, 0x2894FF, 0xFFF8F2, 0xA58E5E, 0x4646FF, cCyan, cOrange, cDim}};
const char* const kLamp[6] = {"ПЕРЕГР", "ТОПЛ", "АНАМ", "РАД", "ЗЕМЛЯ", "АТАКА"};
// a number with a comma (the atlas is cp1251: no real minus, the hyphen stays)
void Num(char* b, size_t n, const char* fmt, double v) {
    std::snprintf(b, n, fmt, v);
    for (char* c = b; *c; c++) if (*c == '.') *c = ',';
}
}  // namespace

void TantraDisplays::Say(const char* text, int level) { std::snprintf(say_, sizeof say_, "%s", text); sayLevel_ = level; sayT_ = oapiGetSimTime(); }

void TantraDisplays::AckAlerts() { for (Alert& a : alerts_) a.ack = true; }

int TantraDisplays::AlertLevel() const { int m = 0; for (const Alert& a : alerts_) if (a.active) m = (std::max)(m, a.level); return m; }
bool TantraDisplays::AlertUnacked() const { for (const Alert& a : alerts_) if (a.active && !a.ack) return true; return false; }

// The ship's alerts (every step): the conditions -> the list; the same key updates its record (a higher level blinks again),
// a new one goes to the front, one no longer present is "снято" (kept, up to 40). And the events as one-off messages.
void TantraDisplays::Alerts() {
    Tantra* t = t_;
    struct Now { const char* key; int lamp, level; char text[96]; };
    Now now[8]; int n = 0;
    auto add = [&](const char* key, int lamp, int level, const char* fmt, double v1 = 0, double v2 = 0) {
        if (n >= 8) return; Now& c = now[n++]; c.key = key; c.lamp = lamp; c.level = level; std::snprintf(c.text, sizeof c.text, fmt, v1, v2);
        for (char* p = c.text; *p; p++) if (*p == '.') *p = ','; };
    const double g = t->accelG_, alt = t->GetAltitude(), mx = t->GetMaxFuelMass(), fuel = mx > 0 ? t->GetFuelMass() / mx : 1.0;
    VECTOR3 hv; t->GetHorizonAirspeedVector(hv);
    const bool ground = t->GroundContact();
    if (t->gLimitOn_ && g > t->gLimit_) add("g", 0, 2, "ПЕРЕГРУЗКА %.1f g ВЫШЕ ПРЕДЕЛА %.1f", g, t->gLimit_);
    else if (t->gLimitOn_ ? g > t->gLimit_ * .9 : g > 3.0) add("g", 0, 1, "ПЕРЕГРУЗКА %.1f g", g);
    if (fuel < .10) add("fuel", 1, 2, "ТОПЛИВО %.0f %%", fuel * 100); else if (fuel < .25) add("fuel", 1, 1, "ТОПЛИВО %.0f %%", fuel * 100);
    if (t->activeTrap_ >= 0 && t->activeTrap_ < tantra::spec::kTrapCount && t->trapPresent_[t->activeTrap_] && t->trap_[t->activeTrap_]) {
        const double m = t->GetPropellantMaxMass(t->trap_[t->activeTrap_]), f = m > 0 ? t->GetPropellantMass(t->trap_[t->activeTrap_]) / m : 1.0;
        if (f < .05) add("ana", 2, 2, "ЛОВУШКА %.0f: АНАМЕЗОН %.0f %%", t->activeTrap_ + 1.0, f * 100); else if (f < .15) add("ana", 2, 1, "ЛОВУШКА %.0f: АНАМЕЗОН %.0f %%", t->activeTrap_ + 1.0, f * 100);
    }
    if (t->safety_) {
        double r = 0; for (const TantraSafety::CrewRisk& c : t->safety_->Crews()) r = (std::max)(r, c.rate * 3600.0);
        if (r >= 1.0) add("rad", 3, 2, "РАДИАЦИЯ %.1f Гр/ч", r); else if (r >= 0.1) add("rad", 3, 1, "РАДИАЦИЯ %.2f Гр/ч", r);
    }
    if (!ground && hv.y < -1.0) {                                         // the ground: time to it at this sink rate
        const double tt = alt / -hv.y;
        if (tt < 12 && alt < 3000) add("gnd", 4, 2, "ЗЕМЛЯ ЧЕРЕЗ %.0f С · Vy %.0f М/С", tt, hv.y); else if (tt < 30 && alt < 6000) add("gnd", 4, 1, "ЗЕМЛЯ ЧЕРЕЗ %.0f С", tt);
    }
    if (t->GetAtmPressure() > 1000 && t->GetAirspeed() > 50 && !ground) {
        const double a = std::fabs(t->GetAOA() * DEG);
        if (a > 35) add("aoa", 5, 2, "УГОЛ АТАКИ %.0f°", a); else if (a > 20) add("aoa", 5, 1, "УГОЛ АТАКИ %.0f°", a);
    }
    char tod[16]; { const double s = std::fmod(oapiGetSimTime(), 86400.0); std::snprintf(tod, sizeof tod, "%02d:%02d:%02d", int(s / 3600), int(s / 60) % 60, int(s) % 60); }
    for (Alert& a : alerts_) a.seen = false;
    for (int i = 0; i < n; i++) {
        Alert* f = nullptr;
        for (Alert& a : alerts_) if (a.active && !std::strcmp(a.key, now[i].key)) { f = &a; break; }
        if (f) { if (now[i].level > f->level) f->ack = false; f->level = now[i].level; f->lamp = now[i].lamp; std::snprintf(f->text, sizeof f->text, "%s", now[i].text); f->seen = true; }
        else {
            Alert a = {}; std::snprintf(a.key, sizeof a.key, "%s", now[i].key); a.lamp = now[i].lamp; a.level = now[i].level;
            std::snprintf(a.text, sizeof a.text, "%s", now[i].text); std::snprintf(a.tod, sizeof a.tod, "%s", tod); a.active = a.seen = true;
            alerts_.insert(alerts_.begin(), a);
            if (alerts_.size() > 40) alerts_.pop_back();
        }
    }
    for (Alert& a : alerts_) if (a.active && !a.seen) a.active = false;
    // the events: one-off messages under the ribbon
    const int st = int(t->ignition_.Stage()) & 3;
    int nav = 0; static const int kNavs[7] = {NAVMODE_KILLROT, NAVMODE_HLEVEL, NAVMODE_PROGRADE, NAVMODE_RETROGRADE, NAVMODE_NORMAL, NAVMODE_ANTINORMAL, NAVMODE_HOLDALT};
    static const char* const kNavName[8] = {"РУЧНОЕ", "СТОП ВРАЩЕНИЯ", "ГОРИЗОНТ", "ПРОГРАД", "РЕТРОГРАД", "НОРМАЛЬ +", "НОРМАЛЬ -", "ВЫСОТА"};
    for (int i = 0; i < 7; i++) if (t->GetNavmodeState(kNavs[i])) { nav = i + 1; break; }
    const bool high = alt > 100e3;
    if (alertInit_) {
        char b[96];
        if (wasGround_ && !ground) Say("ВНИМАНИЕ — ВЗЛЁТ", 1);
        if (!wasGround_ && ground) Say("ПОСАДКА", 0);
        if (!wasHigh_ && high) Say("ВЫХОД ИЗ АТМОСФЕРЫ", 0);
        if (wasHigh_ && !high && t->GetAtmPressure() > 0) Say("ВНИМАНИЕ — ВХОД В АТМОСФЕРУ", 1);
        if (nav != lastNav_) { std::snprintf(b, sizeof b, "АВТОПИЛОТ: %s", kNavName[nav]); Say(b, 0); }
        if (st != lastStage_) { static const char* const kSt[4] = {"АНАМЕЗОН: ВЫКЛ", "АНАМЕЗОН: ПОЛЕ", "АНАМЕЗОН: ЛУЧ", "ОПАСНОСТЬ — ПОДАЧА АНАМЕЗОНА · ВСЕ В КРЕСЛАХ"}; Say(kSt[st], st == 3 ? 2 : st ? 1 : 0); }
    }
    wasGround_ = ground; wasHigh_ = high; lastNav_ = nav; lastStage_ = st; alertInit_ = true;
}

// Projected with the front camera itself (ship frame = mesh frame for directions): every mark lands on what the picture shows.
// The horizon and the pitch bars come from the local horizon frame (HorizonInvRot), so they lie right at any pitch and bank.
void TantraDisplays::DrawHud(SURFHANDLE s, int w, int h, const VECTOR3& cd, const VECTOR3& cu, double vfovDeg) {
    if (!s) return;
    const int mode = hudMode_;
    const double simt = oapiGetSimTime();
    const bool blink = std::fmod(simt, 1.0) < 0.62;
    const int lvl = AlertLevel();
    if (mode == HUD_NONE && !lvl && simt - sayT_ > 6.0) return;           // off: only an alert or a message is still shown
    Tantra* t = t_;
    const Pal& C = kPal[palette_ & 1];
    const VECTOR3 cr = crossp(cu, cd);                                    // the camera's right (left-handed frame)
    const double F = 0.5 * h / std::tan(0.5 * vfovDeg * RAD);
    auto proj = [&](const VECTOR3& d, double& x, double& y) {
        const double z = dotp(d, cd); if (z <= 1e-3) return false;
        x = 0.5 * w + F * dotp(d, cr) / z; y = 0.5 * h - F * dotp(d, cu) / z; return x > -w && x < 2 * w && y > -h && y < 2 * h; };
    auto horiz = [&](double az, double el, double& x, double& y) {         // a direction in the local horizon frame (x east, y up, z north)
        VECTOR3 l; t->HorizonInvRot(_V(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el)), l); return proj(l, x, y); };
    Paint P(s, glyphs_);
    if (!P.Ok()) return;
    oapi::Sketchpad2* S2 = dynamic_cast<oapi::Sketchpad2*>(P.Skp());
    const double k = h / 720.0, C0 = w / 2.0 - 640 * k, boost = (std::min)(1.5, HudGain());
    auto X = [&](double gx) { return C0 + gx * k; };
    auto Y = [&](double gy) { return gy * k; };
    // a line in the suit's way: the glow (wider, faint) under the line; widths in grid units
    auto L = [&](double x0, double y0, double x1, double y1, DWORD col, double wd, double a = 1.0, bool glow = true, bool dash = false) {
        const float px = float((std::max)(1.0, wd * k * 0.8 * (1 + 0.6 * boost)));
        if (!S2) { P.Line(int(x0), int(y0), int(x1), int(y1), col & 0xFFFFFF, int(px)); return; }
        if (glow && !dash) { S2->QuickPen((col & 0xFFFFFF) | (DWORD(0.2 * a * 255) << 24), float((wd + 2.4) * k * 0.8 * (1 + 0.6 * boost)), 1); S2->Line(int(x0), int(y0), int(x1), int(y1)); }
        S2->QuickPen((col & 0xFFFFFF) | (DWORD((std::min)(1.0, a) * 255) << 24), px, dash ? 2 : 1); S2->Line(int(x0), int(y0), int(x1), int(y1));
    };
    auto Tx = [&](double x, double y, const char* str, double sz, int col, int align) { P.Text(x, y, str, sz * k, col, align); };
    // the soft bands: dark at the top and the bottom, fading (cosine) toward the middle
    if (S2) {
        const double a = 0.10 + 0.40 * (std::min)(1.0, boost);
        for (int i = 0; i < 48; i++) {
            const double f = 0.5 + 0.5 * std::cos(PI * i / 48.0);
            const double yt = 140 * k * i / 48.0, hb = 140 * k / 48.0 + 1;
            S2->QuickBrush(DWORD(0.85 * a * f * 255) << 24); S2->QuickPen(0); P.Skp()->Rectangle(0, int(yt), w, int(yt + hb));
        }
        S2->QuickBrush(0);
    }
    char b[128];
    double hdg = 0.0; oapiGetHeading(t->GetHandle(), &hdg);
    double cx0 = 0.5 * w, cy0 = 0.5 * h; const bool axis = proj(_V(0, 0, 1), cx0, cy0);
    if (mode != HUD_NONE) {
        const double pdeg = F * RAD;                                      // px per degree at the centre
        for (int deg = -30; deg <= 60; deg += 10) {                       // the horizon and the pitch bars every 10 deg (dashed below)
            const double el = deg * RAD;
            if (deg == 0) {
                for (int sd = -1; sd <= 1; sd += 2)
                    for (int a = 3; a < 40; a += 2) { double x0, y0, x1, y1; if (horiz(hdg + sd * a * RAD, 0, x0, y0) && horiz(hdg + sd * (a + 2) * RAD, 0, x1, y1)) L(x0, y0, x1, y1, C.P, 1.6); }
                continue;
            }
            for (int sd = -1; sd <= 1; sd += 2) {
                double x0, y0, x1, y1;
                if (!horiz(hdg + sd * 4 * RAD, el, x0, y0) || !horiz(hdg + sd * 11 * RAD, el, x1, y1)) continue;
                if (deg > 0) L(x0, y0, x1, y1, C.P, 1.2);
                else for (int q = 0; q < 4; q++) { const double a = q / 4.0, c2 = a + 0.15; L(x0 + (x1 - x0) * a, y0 + (y1 - y0) * a, x0 + (x1 - x0) * c2, y0 + (y1 - y0) * c2, C.P, 1.2, 1.0, false, false); }
                double tx, ty; if (horiz(hdg + sd * 4 * RAD, el - (deg > 0 ? 1 : -1) * RAD, tx, ty)) L(x0, y0, tx, ty, C.P, 1.2);
                std::snprintf(b, sizeof b, "%d", std::abs(deg));
                P.Text(x1 + (x1 - x0) * 0.25, y1 + (y1 - y0) * 0.25 - 6 * k, b, 11 * k, C.tP, 1);
            }
        }
        if (axis) {                                                       // the ship's waterline
            const double a = 18 * k;
            L(cx0 - 2.2 * a, cy0, cx0 - a, cy0, C.P, 1.6); L(cx0 - a, cy0, cx0 - a * .5, cy0 + a * .5, C.P, 1.6);
            L(cx0 + 2.2 * a, cy0, cx0 + a, cy0, C.P, 1.6); L(cx0 + a, cy0, cx0 + a * .5, cy0 + a * .5, C.P, 1.6);
        }
        VECTOR3 v; t->GetAirspeedVector(FRAME_LOCAL, v);                  // the velocity vector
        double vx, vy;
        if (length(v) > 1.0 && proj(v / length(v), vx, vy)) {
            const double r = 11 * k;
            for (int i = 0; i < 24; i++) { const double a0 = i * PI / 12, a1 = (i + 1) * PI / 12; L(vx + r * std::cos(a0), vy + r * std::sin(a0), vx + r * std::cos(a1), vy + r * std::sin(a1), C.P, 1.4); }
            L(vx - 3 * r, vy, vx - r, vy, C.P, 1.4); L(vx + r, vy, vx + 3 * r, vy, C.P, 1.4); L(vx, vy - r, vx, vy - 2 * r, C.P, 1.4);
        }
        {                                                                 // the heading tape along the top
            const double hd = hdg * DEG, y1 = Y(52), cxh = axis ? cx0 : 0.5 * w;
            for (int d = int(std::floor((hd - 30) / 5)) * 5; d <= hd + 30; d += 5) {
                const double xx = cxh + (d - hd) * pdeg; if (xx < X(200) || xx > X(1080)) continue;
                L(xx, y1 - (d % 10 ? 6 : 12) * k, xx, y1, C.P, 1.0, d % 10 ? .6 : 1.0, false);
                if (d % 10 == 0) { std::snprintf(b, sizeof b, "%03d", ((d % 360) + 360) % 360); Tx(xx, 56, b, 9.5, C.tP, 1); }
            }
            L(cxh - 7 * k, Y(30), cxh, Y(40), C.P, 1.4); L(cxh + 7 * k, Y(30), cxh, Y(40), C.P, 1.4);
        }
        // the readouts: label over value (the suit's pair), speed left, altitude right
        const double spd = t->GetAirspeed(), alt = t->GetAltitude();
        auto pair = [&](double gx, double gy, const char* lab, const char* val, int al, int vc) { Tx(X(gx), gy, lab, 9.5, C.tD, al); P.Text(X(gx), Y(gy + 17), val, 15 * k, vc, al); };
        if (spd > 2000) Num(b, sizeof b, "%.2f км/с", spd / 1000); else Num(b, sizeof b, "%.0f м/с", spd);
        pair(300, 190, "СКОРОСТЬ", b, 2, C.tP);
        { VECTOR3 hv; t->GetHorizonAirspeedVector(hv); Num(b, sizeof b, "%+.1f м/с", hv.y); }
        pair(300, 236, "ВЕРТИКАЛЬНАЯ", b, 2, C.tP);
        if (alt > 10000) Num(b, sizeof b, "%.1f км", alt / 1000); else Num(b, sizeof b, "%.0f м", alt);
        pair(980, 190, "ВЫСОТА", b, 0, C.tP);
        Num(b, sizeof b, "%.2f g", t->accelG_);
        pair(980, 236, t->gLimitOn_ ? "ПЕРЕГРУЗКА / ПРЕДЕЛ" : "ПЕРЕГРУЗКА", b, 0, t->gLimitOn_ && t->accelG_ > t->gLimit_ * .9 ? cRed : C.tP);
        // the nearest base: a thin ring and a leader line to its name and distance (the suit's target mark)
        OBJHANDLE ref = t->GetSurfaceRef();
        if (ref) {
            double lng, lat, rad; t->GetEquPos(lng, lat, rad);
            OBJHANDLE best = nullptr; double bd = 2.0e6;
            for (DWORD i = 0; i < oapiGetBaseCount(ref); i++) {
                OBJHANDLE bs = oapiGetBaseByIndex(ref, i); double bl, bb; oapiGetBaseEquPos(bs, &bl, &bb);
                const double d = oapiGetSize(ref) * std::acos((std::max)(-1.0, (std::min)(1.0, std::sin(lat) * std::sin(bb) + std::cos(lat) * std::cos(bb) * std::cos(bl - lng))));
                if (d < bd) { bd = d; best = bs; }
            }
            if (best) {
                VECTOR3 gb, gs; oapiGetGlobalPos(best, &gb); t->GetGlobalPos(gs);
                MATRIX3 R; t->GetRotationMatrix(R); VECTOR3 d = tmul(R, gb - gs); const double dl = length(d);
                char nm[64]; oapiGetObjectName(best, nm, sizeof nm);
                if (bd > 10000) Num(b, sizeof b, "%.0f км", bd / 1000); else Num(b, sizeof b, "%.1f км", bd / 1000);
                double bx, by;
                if (dl > 1 && proj(d / dl, bx, by) && bx > 0 && bx < w && by > Y(150) && by < h) {
                    const double r = 9 * k;
                    for (int i = 0; i < 24; i++) { const double a0 = i * PI / 12, a1 = (i + 1) * PI / 12; L(bx + r * std::cos(a0), by + r * std::sin(a0), bx + r * std::cos(a1), by + r * std::sin(a1), C.A, 1.2); }
                    L(bx + r * .7, by - r * .7, bx + 40 * k, by - 40 * k, C.A, 1.0); L(bx + 40 * k, by - 40 * k, bx + 120 * k, by - 40 * k, C.A, 1.0);
                    P.Text(bx + 44 * k, by - 58 * k, nm, 11 * k, C.tA, 0); P.Text(bx + 44 * k, by - 36 * k, b, 9.5 * k, C.tA, 0);
                } else {                                                  // not in the picture: its name and the bearing at the bottom of the band
                    const double brg = std::atan2(dotp(d, cr), dotp(d, cd)) * DEG;
                    char line[128]; std::snprintf(line, sizeof line, "БАЗА %s · %s · %s%.0f°", nm, b, brg >= 0 ? "вправо " : "влево ", std::fabs(brg));
                    Tx(X(640), 168, line, 10.5, C.tA, 1);
                }
            }
        }
    }
    // the alerts: the main stripes at the edges of the view, the lamps of the systems, the word, the ribbon, the message
    const DWORD mc = lvl == 2 ? C.R : C.A;
    const bool unacked = AlertUnacked();
    for (int sd = 0; sd < 2; sd++) {
        const double x = X(sd ? 1268 : 6);
        if (lvl && (!unacked || blink)) { if (S2) { S2->QuickBrush(mc | 0xFF000000); S2->QuickPen(0); P.Skp()->Rectangle(int(x), int(Y(250)), int(x + 6 * k), int(Y(440))); S2->QuickBrush(0); } }
        else L(x, Y(250), x, Y(440), C.D, 1.0, 0.3, false);
    }
    for (int i = 0; i < 6; i++) {
        int ll = 0; for (const Alert& a : alerts_) if (a.active && a.lamp == i) ll = (std::max)(ll, a.level);
        const double lx = X(640 - 155 + i * 62), ly = Y(104), r = 4 * k;
        const DWORD lc = ll == 2 ? C.R : ll == 1 ? C.A : C.D;
        if (S2) { S2->QuickBrush((lc & 0xFFFFFF) | (ll ? 0xFF000000 : 0x60000000)); S2->QuickPen(0); P.Skp()->Ellipse(int(lx - r), int(ly - r), int(lx + r), int(ly + r)); S2->QuickBrush(0); }
        P.Text(lx, Y(84), kLamp[i], 8.5 * k, ll == 2 ? cRed : ll == 1 ? C.tA : C.tD, 1);
    }
    if (lvl) {
        const char* word = lvl == 2 ? "ОПАСНОСТЬ" : "ВНИМАНИЕ";
        if (S2) { S2->QuickBrush((mc & 0xFFFFFF) | (DWORD((unacked && blink ? 0.25 : 0.1) * 255) << 24)); S2->QuickPen((mc & 0xFFFFFF) | 0xFF000000, float(1.4 * k), 1);
                  P.Skp()->Rectangle(int(X(560)), int(Y(112)), int(X(720)), int(Y(132))); S2->QuickBrush(0); }
        Tx(X(640), 115, word, 13, lvl == 2 ? cRed : C.tA, 1);
    }
    int rows = 0;
    for (const Alert& a : alerts_) {                                      // the ribbon: the active ones first, then the cleared
        if (rows >= 3) break;
        if (!a.active) continue;
        std::snprintf(b, sizeof b, "%s  %s %s%s", a.tod, a.level == 2 ? "!" : "*", a.text, a.ack ? "" : "  ·  подтвердите на центральном экране");
        Tx(X(640), 148 + rows * 15, b, 10.5, a.level == 2 ? cRed : C.tA, 1); rows++;
    }
    for (const Alert& a : alerts_) {
        if (rows >= 3) break;
        if (a.active) continue;
        std::snprintf(b, sizeof b, "%s  %s  ·  снято", a.tod, a.text);
        Tx(X(640), 148 + rows * 15, b, 10.5, C.tD, 1); rows++;
    }
    if (simt - sayT_ < 6.0 && say_[0]) Tx(X(640), 148 + rows * 15 + 4, say_, 12.5, sayLevel_ == 2 ? cRed : sayLevel_ == 1 ? C.tA : cWhite, 1);
}

// ---- the risers ----------------------------------------------------------------------------------------------

void TantraDisplays::DrawRiser(int k) {
    SURFHANDLE s = s_[k];
    if (!s) return;
    Tantra* t = t_;
    const R4 h0 = RiserHalf(k, 0), h1 = RiserHalf(k, 1);
    const PageDef& p = kPage[k == kRiserR ? M_PLANT : M_POSE];
    R4 rr[3]; const int n = FitSections(p, h0, rr);
    RedrawSections(t, Tantra::PanelTex(), p);
    oapiClearSurface(s, 0xFF000000 | kBezel);
    {
        Paint P(s, glyphs_, kSc[k]);
        if (!P.Ok()) return;
        P.Box(h0.x0, 6, h0.x1, h0.y1, kGlass, 0x303830); if (k == kRiserL) P.Box(h1.x0, 6, h1.x1, h1.y1, kGlass, 0x303830);
        P.Text(h0.x0 + 10, 9, k == kRiserR ? "ЭНЕРГИЯ" : "МЕХАНИЗАЦИЯ", 12, cOrange);
        if (k == kRiserL) {                                                           // ПОЛОЖЕНИЕ: the attitude and its rates, the RCS
            char b[64];
            P.Text(h1.x0 + 10, 9, "ПОЛОЖЕНИЕ", 12, cOrange);
            VECTOR3 w; t->GetAngularVel(w);
            double hdg = 0.0; oapiGetHeading(t->GetHandle(), &hdg);
            const double val[3] = {t->GetPitch() * DEG, t->GetBank() * DEG, hdg * DEG}, rate[3] = {w.x * DEG, w.z * DEG, w.y * DEG};
            static const char* const kAx[3] = {"ТАНГАЖ", "КРЕН", "КУРС"};
            const int cw = (h1.x1 - h1.x0) / 3;
            for (int i = 0; i < 3; i++) {
                const double cx = h1.x0 + cw * (i + .5);
                P.Text(cx, 40, kAx[i], 12, cGrey, 1);
                std::snprintf(b, sizeof b, i == 2 ? "%03.0f°" : "%+.1f°", val[i]); P.Text(cx, 64, b, 20, cWhite, 1);
                std::snprintf(b, sizeof b, "%+.2f °/с", rate[i]); P.Text(cx, 110, b, 12, std::fabs(rate[i]) > 2 ? cOrange : cCyan, 1);
                const R4 bar = {int(cx - cw * .4), 140, int(cx + cw * .4), 150};
                P.Box(bar, kBarDim, 0x504030); const int m = (bar.x0 + bar.x1) / 2, d = int((std::max)(-1.0, (std::min)(1.0, rate[i] / 5.0)) * (bar.x1 - bar.x0 - 4) / 2);
                if (d) P.Box((std::min)(m, m + d), bar.y0 + 1, (std::max)(m, m + d), bar.y1 - 1, std::fabs(rate[i]) > 2 ? kAmberF : kCyanF);
                P.Line(m, bar.y0 - 3, m, bar.y1 + 3, 0xD0D0D0, 1);
            }
            const int rcs = t->GetAttitudeMode();
            for (int i = 0; i < 3; i++) P.Key(PoseRcs(i), kRcsName[i], rcs == kRcs[i]);
            P.Key(PoseRcs(3), "СТОП ВРАЩ.", t->GetNavmodeState(NAVMODE_KILLROT));
        }
    }
    BlitSections(s, Tantra::PanelTex(), p, rr, n, kSc[k]);
    if (k == kRiserR && eng_) { const R4 e = EngRect(); const double c = kSc[k]; RECT dr = {LONG(e.x0 * c), LONG(e.y0 * c), LONG(e.x1 * c), LONG(e.y1 * c)}, sr = {0, 0, 1024, kEngH}; oapiBlt(s, eng_, &dr, &sr); }
}

bool TantraDisplays::TouchRiser(int k, double x, double y) {
    const R4 h0 = RiserHalf(k, 0);
    const PageDef& p = kPage[k == kRiserR ? M_PLANT : M_POSE];
    R4 rr[3]; const int n = FitSections(p, h0, rr);
    if (ClickSections(t_, p, rr, n, x, y)) return true;
    if (k == kRiserR) {
        const R4 e = EngRect();
        if (InGrown(e, x, y, 4)) return TouchEngines((x - e.x0) / double(e.x1 - e.x0) * 1024, (y - e.y0) / double(e.y1 - e.y0) * kEngH);
        return false;
    }
    for (int i = 0; i < 3; i++) if (PoseRcs(i).In(x, y)) { t_->SetAttitudeMode(kRcs[i]); return true; }
    if (PoseRcs(3).In(x, y)) { t_->ToggleNav(NAVMODE_KILLROT); return true; }
    return false;
}

// ---- the attitude keys -----------------------------------------------------------------------------------------

void TantraDisplays::DrawKeys() {
    SURFHANDLE s = s_[kKeys];
    if (!s) return;
    oapiClearSurface(s, 0xFF000000 | 0x1C1816);
    Paint P(s, glyphs_, kSc[kKeys]);
    if (!P.Ok()) return;
    P.Text(gW[5] / 2.0, 14, "ОРИЕНТАЦИЯ · АВТОПИЛОТЫ", 15, cCyan, 1);
    bool any = false;
    for (int i = 0; i < kNav; i++) if (t_->GetNavmodeState(kAuto[i].mode)) any = true;
    for (int i = 0; i < kAutoN; i++) {
        const int m = kAuto[i].mode;
        P.Key(AutoKeyR(i), kAuto[i].name, m > 0 ? t_->GetNavmodeState(m) : !any, i == 0);
    }
}

bool TantraDisplays::TouchKeys(double x, double y) {
    for (int i = 0; i < kAutoN; i++)
        if (AutoKeyR(i).In(x, y)) {
            const int m = kAuto[i].mode;
            if (m > 0) t_->ToggleNav(m);                                 // the autopilots are switched on from here only
            else { for (int j = 0; j < kNav; j++) if (t_->GetNavmodeState(kAuto[j].mode)) t_->ToggleNav(kAuto[j].mode); }   // РУЧН.: all off
            return true;
        }
    return false;
}

// ---- the wings' shelves ----------------------------------------------------------------------------------------

void TantraDisplays::DrawWing(int k) {
    SURFHANDLE s = s_[k];
    if (!s) return;
    PageDef mimic = {1, {kMimic}};
    R4 rr[1]; int n = 0;
    if (k == kWingL) { const R4 c = CheckR(); n = FitSections(mimic, {c.x0 + 6, c.y0 + 30, c.x1 - 6, c.y1 - 6}, rr); RedrawSections(t_, Tantra::PanelTex(), mimic); }
    oapiClearSurface(s, 0xFF000000 | 0x1C1816);
    Tantra* t = t_;
    {
        Paint P(s, glyphs_, kSc[k]);
        if (!P.Ok()) return;
        if (k == kWingL) {
            P.Text(14, 40, "МЕХАНИЗАЦИЯ", 16, cOrange);
            const bool lit[12] = {t->carriage_.GearDown(), t->carriage_.Set() == tantra::Carriage::FlightSet::Level, t->carriage_.Set() == tantra::Carriage::FlightSet::Standing,
                                  t->carriage_.Standing(), t->wingMode_ != 0, t->podTarget_ < 45, t->podTarget_ >= 45, t->hangarT_ > .5, t->carriage_.Port(),
                                  t->airlockUp_ > .5, t->portStep_ != Tantra::PortStep::Idle, false};
            for (int i = 0; i < 12; i++) P.Key(MechKeyR(i), kMechName[i], lit[i], i == MK_PT_STOP || i == MK_CRESTS);
            const R4 c = CheckR();
            P.Box(c, 0x0A1206, 0x3A5A2C); P.Text(c.x0 + 12, c.y0 + 6, "ПРОВЕРКА", 14, cCyan);
        } else {
            P.Text(14, 40, "МОНИТОРЫ", 16, cCyan);
            const int st = t->interior_.SideFoldState(0);
            for (int i = 0; i < 3; i++) P.Key(FoldKeyR(i), kFoldName[i], st == i);
            const int cx1 = kCalcX + 5 * (kCalcKw + 6) - 6;
            P.Box(kCalcX, 92, cx1, 138, 0x061006, 0x3A5A2C);
            char d[32]; std::snprintf(d, sizeof d, "%s", calc_); for (char* c = d; *c; c++) if (*c == '.') *c = ',';
            P.Text(cx1 - 10, 102, d, 20, cWhite, 2);
            for (int i = 0; i < 20; i++) P.Key(CalcKeyR(i), kCalcKey[i], false, i == 4);
        }
    }
    if (n) BlitSections(s, Tantra::PanelTex(), mimic, rr, n, kSc[k]);
}

bool TantraDisplays::TouchWing(int k, double x, double y) {
    Tantra* t = t_;
    if (k == kWingL) {
        for (int i = 0; i < 12; i++)
            if (MechKeyR(i).In(x, y)) {
                switch (i) {
                    case MK_GEAR: t->ActGear(); break;
                    case MK_LEVEL: if (t->carriage_.Set() != tantra::Carriage::FlightSet::Level) t->ActGearSet(); break;
                    case MK_STAND: if (t->carriage_.Set() != tantra::Carriage::FlightSet::Standing) t->ActGearSet(); break;
                    case MK_ERECT: t->ActErect(); break;
                    case MK_CRESTS: t->ActCrests(); break;
                    case MK_PODS_AFT: t->ActPodsTo(0.0); break;
                    case MK_PODS_DOWN: t->ActPodsTo(90.0); break;
                    case MK_HANGAR: t->ActHangar(); break;
                    case MK_PORT: t->ActPort(); break;
                    case MK_AIRLOCK: t->ActToggleAirlock(); break;
                    case MK_PT_LIFT: t->ActPortLift(); break;
                    case MK_PT_STOP: t->ActPortStop(); break;
                }
                return true;
            }
        return false;
    }
    for (int i = 0; i < 3; i++) if (FoldKeyR(i).In(x, y)) { t->interior_.SideFold(0, i); t->interior_.SideFold(1, i); return true; }
    for (int i = 0; i < 20; i++) if (CalcKeyR(i).In(x, y)) { CalcKey(i); return true; }
    return false;
}

// the calculator: 7 8 9 / C | 4 5 6 x кор | 1 2 3 - % | 0 , = + ±
void TantraDisplays::CalcKey(int i) {
    const char* k = kCalcKey[i];
    const double cur = std::atof(calc_);
    auto show = [&](double v) { std::snprintf(calc_, sizeof calc_, std::fabs(v) < 1e15 ? "%.10g" : "%.6e", v); };
    auto apply = [&](double a, double b2) { switch (calcOp_) { case 1: return a + b2; case 2: return a - b2; case 3: return a * b2; case 4: return b2 != 0 ? a / b2 : 0.0; } return b2; };
    if (k[0] >= '0' && k[0] <= '9' && !k[1]) {
        if (calcNew_ || !std::strcmp(calc_, "0")) { calc_[0] = k[0]; calc_[1] = 0; calcNew_ = false; }
        else if (std::strlen(calc_) < 14) { const size_t n = std::strlen(calc_); calc_[n] = k[0]; calc_[n + 1] = 0; }
        return;
    }
    if (!std::strcmp(k, ",")) { if (calcNew_) { std::strcpy(calc_, "0."); calcNew_ = false; } else if (!std::strchr(calc_, '.')) std::strcat(calc_, "."); return; }
    if (!std::strcmp(k, "C")) { std::strcpy(calc_, "0"); calcAcc_ = 0; calcOp_ = 0; calcNew_ = true; return; }
    if (!std::strcmp(k, "±")) { show(-cur); return; }
    if (!std::strcmp(k, "кор")) { show(std::sqrt((std::max)(0.0, cur))); calcNew_ = true; return; }
    if (!std::strcmp(k, "%")) { show(calcOp_ ? calcAcc_ * cur / 100 : cur / 100); calcNew_ = true; return; }
    const int op = !std::strcmp(k, "+") ? 1 : !std::strcmp(k, "-") ? 2 : !std::strcmp(k, "x") ? 3 : !std::strcmp(k, "/") ? 4 : 0;
    if (calcOp_ && !calcNew_) { calcAcc_ = apply(calcAcc_, cur); show(calcAcc_); }
    else if (!calcOp_) calcAcc_ = cur;
    calcOp_ = op; calcNew_ = true;                                       // '=' leaves no operation
}

// ---- the curved monitors ----------------------------------------------------------------------------------------

void TantraDisplays::DrawSide(int k) {
    SURFHANDLE s = s_[k];
    if (!s) return;
    const int mode = mode_[k];
    oapiClearSurface(s, 0xFF000000 | kBezel);
    R4 rr[3]; int n = 0;
    if (mode != M_OFF && mode != M_MFD && mode != M_SET) {               // the page: live sections of the ship's 2D panels
        const PageDef& p = kPage[mode];
        n = PageRects(p, rr);
        if (SURFHANDLE pt = Tantra::PanelTex()) {
            using namespace tantra::panel;
            for (int a = 0; a < A_COUNT; a++) {                            // let the ship redraw what the page shows
                if (a >= M_MFD2_L && a <= M_MFD3_B) continue;
                const int* ar = kArea[a];
                for (int i = 0; i < n; i++)
                    if (ar[0] < p.s[i].x1 && ar[2] > p.s[i].x0 && ar[1] < p.s[i].y1 && ar[3] > p.s[i].y0) { t_->clbkPanelRedrawEvent(a, PANEL_REDRAW_USER, pt, nullptr); break; }
            }
        }
    }
    {
        Paint P(s, glyphs_);
        if (!P.Ok()) return;
        if (mode != M_OFF) P.Panel({kContent.x0, 8, kContent.x1, kContent.y1});
        if (mode == M_MFD) {
            for (int b = 0; b < 2; b++) {
                const int id = 2 * k + b;
                char t[16]; std::snprintf(t, sizeof t, "MFD %d", id + 1);
                P.Text(8 + b * 504 + 252, 16, t, 16, cGrey, 1);
                const R4 d = MfdDisp(b);
                P.Box(d, 0x000000, kOrangeD);
                for (int i = 0; i < 12; i++) {
                    const R4 r = MfdSide(b, i);
                    const char* l = t_->interior_.MfdLabel(id, i);
                    P.Key(r, l, false);
                }
                static const char* const kb[3] = {"PWR", "SEL", "MNU"};
                for (int j = 0; j < 3; j++) P.Key(MfdBottom(b, j), kb[j], false, j == 0);
            }
        } else if (mode == M_SET) {                                      // the settings: the MFDs' brightness and contrast, the HUD
            P.Text(24, 16, kModeTitle[mode], 17, cOrange);
            char v[32];
            static const char* const kRow[3] = {"ЯРКОСТЬ MFD", "КОНТРАСТ MFD", "ЯРКОСТЬ HUD"};
            const double val[3] = {MfdGain(), 1.0 / MfdGamma(), HudGain()};
            for (int r = 0; r < 3; r++) {
                P.Text(40, 104 + r * 110, kRow[r], 18, cCyan);
                std::snprintf(v, sizeof v, "%.2f", val[r]); for (char* c = v; *c; c++) if (*c == '.') *c = ',';
                P.Text(600, 104 + r * 110, v, 20, cWhite, 2);
                P.Key(SetKey(r, 0), "-", false); P.Key(SetKey(r, 1), "+", false);
            }
            P.Text(40, 436, "ПАЛИТРА HUD", 18, cCyan);
            P.Key(PalKey(0), "ОРАНЖ + ЦИАН", palette_ == 0); P.Key(PalKey(1), "ЦИАН + ОРАНЖ", palette_ == 1);
            P.Text(40, 546, "АВТО / MFD", 18, cCyan);
            P.Key(LumKey(), autoLum_ ? "ПО СВЕТУ: ВКЛ" : "ПО СВЕТУ: ВЫКЛ", autoLum_);
            for (int i = 0; i < 2; i++) P.Key(ResKey(i), i ? "MFD 2048" : "MFD 1024", t_->interior_.MfdRes() == (i ? 2048 : 1024));
            P.Text(512, 598, autoLum_ ? "яркость и контраст следуют освещению: день, сумерки, ночь" : "MFD мониторов: усиление и гамма при выводе; HUD главного экрана", 12, cGrey, 1);
        } else if (mode != M_OFF) {
            P.Text(24, 16, kModeTitle[mode], 17, cOrange);
            if (mode == M_FLUID) {                                       // the planetary engines' working fluid, in words
                char t[96];
                const double ar = t_->argon_ ? t_->GetPropellantMass(t_->argon_) : 0.0, fe = t_->iron_ ? t_->GetPropellantMass(t_->iron_) : 0.0;
                std::snprintf(t, sizeof t, "аргон %.0f т   железо %.0f т   (маршевый: %s)", ar / 1000, fe / 1000, t_->marchHigh_ ? "железо" : "аргон");
                P.Text(1000, 18, t, 13, cCyan, 2);
            }
        }
        for (int i = 0; i < M_OFF; i++) P.Key(ModeKey(i), kModeName[i], i == mode);
    }
    if (mode == M_MFD) {                                                 // the MFD displays (after the sketchpad is released)
        // the MFD's surface is no texture - the sketchpad can't read it ("Source is not a texture": the MFD stayed white).
        // So each is blitted into a texture first; the brightness (gain, gamma: the settings page) is applied from that.
        static SURFHANDLE tmp[4] = {};
        SURFHANDLE src[2] = {};
        const int res = t_->interior_.MfdRes();
        const RECT msr = {0, 0, res, res};                                // (a null source rect crashes oapiBlt)
        static int tmpRes = 0;
        if (tmpRes != res) { for (SURFHANDLE& q : tmp) if (q) { oapiDestroySurface(q); q = nullptr; } tmpRes = res; }
        for (int b = 0; b < 2; b++)
            if (SURFHANDLE m = t_->interior_.MfdDisplay(2 * k + b)) {
                const int ti = (2 * k + b) & 3;
                if (!tmp[ti]) tmp[ti] = oapiCreateSurfaceEx(res, res, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS);
                RECT r = msr;
                if (tmp[ti]) { oapiBlt(tmp[ti], m, &r, &r); src[b] = tmp[ti]; }
                else { const R4 d = MfdDisp(b); RECT dr = {d.x0 + 2, d.y0 + 2, d.x1 - 2, d.y1 - 2}; oapiBlt(s, m, &dr, &r); }
            }
        oapi::Sketchpad* sk = oapiGetSketchpad(s);
        oapi::Sketchpad3* s3 = sk ? dynamic_cast<oapi::Sketchpad3*>(sk) : nullptr;
        const double gG = MfdGain(), gM = MfdGamma();
        if (s3) { const FVECTOR4 br(float(gG), float(gG), float(gG * 1.04), 1.0f), gm(float(gM), float(gM), float(gM), 1.0f);
                  s3->SetBrightness(&br); s3->SetRenderParam(SKP3_PRM_GAMMA, &gm); }
        RECT dst[2];
        for (int b = 0; b < 2; b++) { const R4 d = MfdDisp(b); dst[b] = {d.x0 + 2, d.y0 + 2, d.x1 - 2, d.y1 - 2}; }
        for (int b = 0; b < 2; b++) if (src[b] && s3) { RECT r = msr; s3->StretchRect(src[b], &r, &dst[b]); }
        if (s3) { s3->SetBrightness(nullptr); s3->SetRenderParam(SKP3_PRM_GAMMA, nullptr); }
        if (sk) oapiReleaseSketchpad(sk);
        for (int b = 0; b < 2; b++) if (src[b] && !s3) { RECT r = msr; oapiBlt(s, src[b], &dst[b], &r); }
    } else if (n) {
        if (SURFHANDLE pt = Tantra::PanelTex())
            for (int i = 0; i < n; i++) {
                const Sec& c = kPage[mode].s[i];
                RECT dr = {rr[i].x0, rr[i].y0, rr[i].x1, rr[i].y1}, sr = {c.x0, c.y0, c.x1, c.y1};
                oapiBlt(s, pt, &dr, &sr);
            }
    }
}

bool TantraDisplays::TouchSide(int k, double x, double y) {
    for (int i = 0; i < M_OFF; i++) if (ModeKey(i).In(x, y)) { mode_[k] = i; return true; }
    const int mode = mode_[k];
    if (mode == M_SET) {
        auto clamp = [](double v, double a, double b2) { return v < a ? a : v > b2 ? b2 : v; };
        for (int r = 0; r < 3; r++)
            for (int pl = 0; pl < 2; pl++)
                if (SetKey(r, pl).In(x, y)) {
                    const double d = pl ? 1 : -1;
                    if (autoLum_) { mfdGain_ = MfdGain(); mfdGamma_ = MfdGamma(); hudGain_ = HudGain(); autoLum_ = false; }   // by hand: auto off
                    if (r == 0) mfdGain_ = clamp(mfdGain_ + .2 * d, 1.0, 3.0);
                    else if (r == 1) mfdGamma_ = clamp(mfdGamma_ - .05 * d, .4, 1.0);
                    else hudGain_ = clamp(hudGain_ + .1 * d, .4, 1.5);
                    return true;
                }
        for (int i = 0; i < 2; i++) if (PalKey(i).In(x, y)) { palette_ = i; return true; }
        if (LumKey().In(x, y)) { autoLum_ = !autoLum_; return true; }
        for (int i = 0; i < 2; i++) if (ResKey(i).In(x, y)) { t_->interior_.SetMfdRes(i ? 2048 : 1024); return true; }
        return true;
    }
    if (mode == M_MFD) {
        for (int b = 0; b < 2; b++) {
            const int id = 2 * k + b;
            for (int i = 0; i < 12; i++) if (MfdSide(b, i).In(x, y)) { t_->interior_.MfdPress(id, i); return true; }
            for (int j = 0; j < 3; j++) if (MfdBottom(b, j).In(x, y)) { t_->interior_.MfdPress(id, 12 + j); return true; }
        }
        return true;
    }
    R4 rr[3]; const PageDef& p = kPage[mode]; const int n = PageRects(p, rr);
    for (int i = 0; i < n; i++)
        if (rr[i].In(x, y)) {
            const double tx = p.s[i].x0 + (x - rr[i].x0) / double(rr[i].x1 - rr[i].x0) * (p.s[i].x1 - p.s[i].x0);
            const double ty = p.s[i].y0 + (y - rr[i].y0) / double(rr[i].y1 - rr[i].y0) * (p.s[i].y1 - p.s[i].y0);
            ClickPanel(t_, tx, ty);
            return true;
        }
    return true;
}

// ---- the engine console ------------------------------------------------------------------------------------

void TantraDisplays::DrawEngines() {
    SURFHANDLE s = eng_;
    if (!s) return;
    Tantra* t = t_;
    oapiClearSurface(s, 0xFF000000 | kBezel);
    Paint P(s, glyphs_);
    if (!P.Ok()) return;
    P.Box(kEngGlass, kGlass, 0x303830);
    const bool ana = t->engineSet_ == Tantra::EngineSet::Anamezon;
    char b[96];
    if (ana) {
        P.Text(26, 18, "ПУЛЬТ ДВИГАТЕЛЕЙ · АНАМЕЗОН", 17, cOrange);
        const int st = int(t->ignition_.Stage());
        static const char* const kStage[4] = {"ВЫКЛ", "ПОЛЕ", "ЛУЧ", "ПОДАЧА"};
        std::snprintf(b, sizeof b, "фаза: %s%s", kStage[st & 3], t->ignition_.Transitioning() ? "  >" : "");
        P.Text(1000, 20, b, 14, st ? cOrange : cCyan, 2);
        // the phases: field -> beam -> feed (each key sets the start handle to that position)
        const double lv[3] = {t->ignition_.FieldLevel(), t->ignition_.BeamLevel(), t->ignition_.FeedLevel()};
        static const char* const kPh[3] = {"ПОЛЕ", "ЛУЧ", "ПОДАЧА"};
        for (int i = 0; i < 3; i++) {
            const R4 r = PhaseKey(i);
            P.Key({r.x0, r.y0, r.x1, r.y1 - 14}, kPh[i], st >= i + 1);
            P.Bar({r.x0, r.y1 - 10, r.x1, r.y1}, lv[i], i == 2 ? kAmberF : kGreenF);
        }
        P.Text(136, 268, "СТОП — клавиша внизу", 12, cGrey, 1);
        P.Text(496, 316, "тяга: касание столбика (общая для ДВ. 1–4)", 11, cGrey, 1);
        // the four cups and the retro: thrust of each (they run together on one start sequence)
        static const char* const kCup[5] = {"ДВ. 1", "ДВ. 2", "ДВ. 3", "ДВ. 4", "РЕТРО"};
        for (int i = 0; i < 5; i++) {
            double l = 0.0;
            if (i < 4) { if (t->ana_[i]) l = t->GetThrusterLevel(t->ana_[i]); }
            else { int n = 0; for (THRUSTER_HANDLE h : t->retro_) if (h) { l += t->GetThrusterLevel(h); n++; } if (n) l /= n; }
            const int x = 276 + i * 88;
            P.Text(x + 36, 60, kCup[i], 13, i == 4 ? cOrange : cCyan, 1);
            P.Bar(i < 4 ? CupBar(i) : R4{x + 16, 86, x + 56, 280}, l, i == 4 ? kAmberF : kCyanF, true);
            std::snprintf(b, sizeof b, "%.0f%%", l * 100); P.Text(x + 36, 290, b, 13, cWhite, 1);
        }
        // the traps: the anamezon stock, the one feeding is lit
        P.Text(870, 60, "ЛОВУШКИ", 14, cCyan, 1);
        for (int i = 0; i < tantra::spec::kTrapCount; i++) {
            const int x = 752 + i * 62;
            double f = 0.0;
            if (t->trapPresent_[i] && t->trap_[i]) { const double m = t->GetPropellantMaxMass(t->trap_[i]); f = m > 0 ? t->GetPropellantMass(t->trap_[i]) / m : 0.0; }
            P.Bar({x, 86, x + 40, 260}, f, kAmberF, true);
            if (i == t->activeTrap_) P.Box(x - 4, 82, x + 44, 84, 0xFFDC5A);
            std::snprintf(b, sizeof b, t->trapPresent_[i] ? "%d" : "—", i + 1); P.Text(x + 20, 268, b, 13, i == t->activeTrap_ ? cWhite : cGrey, 1);
        }
        P.Key(kTrapKey, "ПОДАЧА ИЗ ЛОВУШКИ >", false);
    } else {
        P.Text(26, 18, "ПУЛЬТ ДВИГАТЕЛЕЙ · ПЛАНЕТАРНЫЕ", 17, cCyan);
        // the marching cup in the stern well
        const double lm = t->march_ ? t->GetThrusterLevel(t->march_) : 0.0;
        P.Text(26, 60, "МАРШЕВЫЙ", 14, cCyan);
        std::snprintf(b, sizeof b, "%3.0f %%", lm * 100); P.Text(246, 60, b, 14, cWhite, 2);
        P.Bar(kMarchBar, lm, kCyanF);
        std::snprintf(b, sizeof b, "рабочее тело: %s", t->marchHigh_ ? "железо (выше 30 км)" : "аргон"); P.Text(26, 116, b, 12, cGrey);
        // the pods
        double lp = 0.0; int n = 0; for (THRUSTER_HANDLE h : t->pod_) if (h) { lp += t->GetThrusterLevel(h); n++; } if (n) lp /= n;
        P.Text(300, 60, "ГОНДОЛЫ", 14, cCyan);
        std::snprintf(b, sizeof b, "%3.0f %%", lp * 100); P.Text(560, 60, b, 14, cWhite, 2);
        P.Bar(kPodBar, lp, kCyanF);
        std::snprintf(b, sizeof b, "выпуск %.0f %%   сопла %.0f° (цель %.0f°)", t->podOut_ * 100, t->podAngle_, t->podTarget_); P.Text(300, 116, b, 12, cGrey);
        P.Text(600, 210, "ГОНДОЛЫ: СОПЛА", 13, cCyan);
        P.Text(26, 140, "тяга: касание шкалы", 11, cGrey);
        P.Key(kPodsAft, "НАЗАД (ТЯГА ВПЕРЁД)", t->podTarget_ < 45);
        P.Key(kPodsDown, "ВНИЗ (ЗАВИСАНИЕ)", t->podTarget_ >= 45);
        // the propellant
        const double ar = t->argon_ ? t->GetPropellantMass(t->argon_) : 0.0, arM = t->argon_ ? t->GetPropellantMaxMass(t->argon_) : 1.0;
        const double fe = t->iron_ ? t->GetPropellantMass(t->iron_) : 0.0, feM = t->iron_ ? t->GetPropellantMaxMass(t->iron_) : 1.0;
        P.Text(600, 60, "АРГОН", 13, cCyan); P.Bar({700, 62, 990, 76}, arM > 0 ? ar / arM : 0, kGreenF);
        P.Text(600, 90, "ЖЕЛЕЗО", 13, cCyan); P.Bar({700, 92, 990, 106}, feM > 0 ? fe / feM : 0, kGreenF);
        std::snprintf(b, sizeof b, "УВТ: магнитные сопла ±%.0f°, гондолы ±%.0f° — автоматически", tantra::spec::kTvcMaxDeg, tantra::spec::kPodTvcDeg);
        P.Text(26, 160, b, 12, cGrey);
    }
    // the system keys
    P.Key(EngKey(0), "АНАМЕЗОН", ana, true);
    P.Key(EngKey(1), "ПЛАНЕТАРНЫЕ", !ana);
    std::snprintf(b, sizeof b, "ПРЕДЕЛ G %s", t->gLimitOn_ ? "ВКЛ" : "ВЫКЛ"); P.Key(EngKey(2), b, t->gLimitOn_);
    std::snprintf(b, sizeof b, "ПРЕДЕЛ %.1f g >", t->gLimit_); P.Key(EngKey(3), b, false);
    P.Key(EngKey(4), "ОБХОД БЛОК.", t->hotStartOverride_, true);
    P.Key(EngKey(5), ana ? "СТОП АНАМЕЗОН" : "—", false, ana, !ana);
}

bool TantraDisplays::TouchEngines(double x, double y) {
    Tantra* t = t_;
    const bool ana = t->engineSet_ == Tantra::EngineSet::Anamezon;
    if (EngKey(0).In(x, y)) { t->ActSelectEngine(true); return true; }
    if (EngKey(1).In(x, y)) { t->ActSelectEngine(false); return true; }
    if (EngKey(2).In(x, y)) { t->ActToggleGLimit(); return true; }
    if (EngKey(3).In(x, y)) { t->ActCycleGLimit(); return true; }
    if (EngKey(4).In(x, y)) { t->ActToggleOverride(); return true; }
    if (EngKey(5).In(x, y)) { if (ana) t->ActIgnitionTo(0); return true; }
    if (ana) {
        for (int i = 0; i < 4; i++) if (InGrown(CupBar(i), x, y, 8)) { SetMainLevel(AlongUp(CupBar(i), y)); return true; }   // one common level
        for (int i = 0; i < 3; i++) if (PhaseKey(i).In(x, y)) { t->ActIgnitionTo(i + 1); return true; }
        if (kTrapKey.In(x, y)) { t->ActNextTrap(); return true; }
    } else {
        if (InGrown(kMarchBar, x, y)) { SetMainLevel(AlongX(kMarchBar, x)); return true; }
        if (InGrown(kPodBar, x, y)) { SetPodLevel(AlongX(kPodBar, x)); return true; }
        if (kPodsAft.In(x, y)) { t->ActPodsTo(0.0); return true; }
        if (kPodsDown.In(x, y)) { t->ActPodsTo(90.0); return true; }
    }
    return false;
}
