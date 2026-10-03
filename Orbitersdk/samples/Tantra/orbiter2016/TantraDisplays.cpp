// TantraDisplays: the commander's touch screens. See TantraDisplays.h.
#include "TantraDisplays.h"
#include "Tantra.h"
#include "InteriorLayout.h"
#include "PanelLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace tantra::interior;

namespace {

// surfaces: the height is fixed, the width follows the screen's aspect in InteriorLayout.h (kTouch[k].w / .h); the elbow displays
// and the engine console are laid out for 0.80 x 0.55 and 0.98 x 0.41, the flight terminal stretches with its width
constexpr int kSideH = 704, kTermH = 320, kEngH = 428;
const int kH[4] = {kSideH, kSideH, kTermH, kEngH};
int gW[4] = {1024, 1024, 1066, 1024};

// colours: Sketchpad 0xBBGGRR; atlas colours 0 cyan, 1 orange, 2 white, 3 red, 4 dim blue, 5 brown, 6 grey
constexpr DWORD kBezel = 0x342E2A, kGlass = 0x0A1103, kKeyOff = 0x26211D, kKeyOn = 0x3A5A2F, kEdge = 0x92877D;
constexpr DWORD kBarDim = 0x201810, kCyanF = 0xC8A040, kAmberF = 0x2890E0, kGreenF = 0x50C060, kRedF = 0x3030D0;
enum { cCyan = 0, cOrange = 1, cWhite = 2, cRed = 3, cDim = 4, cGrey = 6 };

struct R4 { int x0, y0, x1, y1; bool In(double x, double y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; } };

// one draw pass: the sketchpad, the atlas and a few brushes / pens made on demand
class Paint {
public:
    Paint(SURFHANDLE s, TantraGlyphs& gl) : g_(gl) { skp_ = oapiGetSketchpad(s); if (skp_) skp_->SetBackgroundMode(oapi::Sketchpad::BK_TRANSPARENT); }
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
        skp_->Rectangle(x0, y0, x1, y1);
    }
    void Box(const R4& r, DWORD fill, DWORD edge = 0xFFFFFFFF) { Box(r.x0, r.y0, r.x1, r.y1, fill, edge); }
    void Line(int x0, int y0, int x1, int y1, DWORD c, int w = 2) { skp_->SetPen(Pen(c, w)); skp_->Line(x0, y0, x1, y1); }
    void Circle(int cx, int cy, int r, DWORD c, int w = 2) { skp_->SetBrush(nullptr); skp_->SetPen(Pen(c, w)); skp_->Ellipse(cx - r, cy - r, cx + r, cy + r); }
    // text: (x, top) in pixels, px = capital height, align 0 left, 1 centre, 2 right
    void Text(double x, double top, const char* t, double px, int col, int align = 0) {
        if (g_.Ok()) { g_.DrawA(skp_, x, top, t, px >= 20 ? 3 : px >= 14 ? 2 : 1, col, px, align); return; }
        static const DWORD c[7] = {0xFFDC5A, 0x3CA0FF, 0xFFFFFF, 0x4040F0, 0xA58E5E, 0x527BA3, 0x9C8D7D};
        skp_->SetTextColor(c[col]);
        skp_->SetTextAlign(align == 0 ? oapi::Sketchpad::LEFT : align == 1 ? oapi::Sketchpad::CENTER : oapi::Sketchpad::RIGHT);
        skp_->Text(int(x), int(top), t, int(std::strlen(t)));
    }
    // an Arrow-like key: dark, engraved label, lit when on; warn = amber label
    void Key(const R4& r, const char* t, bool on, bool warn = false, bool dim = false) {
        Box(r, on ? kKeyOn : kKeyOff, kEdge);
        const double px = (std::min)(16.0, (r.y1 - r.y0) * 0.38);
        Text((r.x0 + r.x1) / 2.0, (r.y0 + r.y1) / 2.0 - px / 2, t, px, dim ? cGrey : on ? cWhite : warn ? cOrange : cCyan, 1);
    }
    // a bar: horizontal (x grows) or vertical (fills from the bottom)
    void Bar(const R4& r, double f, DWORD c, bool vertical = false) {
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        Box(r, kBarDim, 0x504030);
        if (vertical) { const int h = int((r.y1 - r.y0) * f); if (h > 1) Box(r.x0 + 1, r.y1 - h, r.x1 - 1, r.y1 - 1, c); }
        else { const int w = int((r.x1 - r.x0) * f); if (w > 1) Box(r.x0 + 1, r.y0 + 1, r.x0 + w, r.y1 - 1, c); }
    }

private:
    oapi::Brush* Brush(DWORD c) { for (auto& b : br_) if (b.first == c) return b.second; br_.push_back({c, oapiCreateBrush(c)}); return br_.back().second; }
    oapi::Pen* Pen(DWORD c, int w) { const DWORD k = c | (DWORD(w) << 24); for (auto& p : pn_) if (p.first == k) return p.second; pn_.push_back({k, oapiCreatePen(1, w, c)}); return pn_.back().second; }
    oapi::Sketchpad* skp_ = nullptr;
    TantraGlyphs& g_;
    std::vector<std::pair<DWORD, oapi::Brush*>> br_;
    std::vector<std::pair<DWORD, oapi::Pen*>> pn_;
};

// ---- the elbow displays: the mode keys along the bottom, the MFD blocks, the system pages (sections of panel.dds) ----
const char* const kModeName[TantraDisplays::M_COUNT] = {"MFD", "РАБ. ТЕЛО", "ЭНЕРГОУСТ.", "ЭКИПАЖ", "ЛИФТЫ", "МЕХАНИЗАЦИЯ", "КОРПУС", "ВЫКЛ"};
const char* const kModeTitle[TantraDisplays::M_COUNT] = {"MFD", "РАБОЧЕЕ ТЕЛО: АНАМЕЗОН, ЛОВУШКИ", "ЭНЕРГОУСТАНОВКА И КОМПЕНСАТОР",
                                                         "ЭКИПАЖ", "ЛИФТЫ: АНГАР, ПОРТ, ШЛЮЗ", "МЕХАНИЗАЦИЯ: ПОЛОЖЕНИЕ, ЛАФЕТ, ШАССИ, ГРЕБНИ, КИЛЬ, ГОНДОЛЫ", "КОРПУС", ""};
R4 ModeKey(int i) { return {8 + i * 144, 622, 8 + i * 144 + 136, 696}; }   // 7 mode keys (the display rises / sinks by its pedestal button)
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

// ---- the flight terminal ----
R4 TermGlass() { return {262, 8, gW[2] - 262, 312}; }
R4 TermLeft(int i) { return {58, 8 + i * 43, 252, 8 + i * 43 + 37}; }          // 0..3 HUD, 4..6 RCS
R4 TermAuto(int i) { return {gW[2] - 252, 6 + i * 39, gW[2] - 8, 6 + i * 39 + 34}; }   // 8 autopilots
R4 TermTab(int i) { const R4 g = TermGlass(); return {g.x1 - 236 + i * 118, 12, g.x1 - 124 + i * 118, 40}; }    // КОСМОС, АТМОСФЕРА
R4 TermThrust() { const R4 g = TermGlass(); return {g.x0 + 16, 72, g.x0 + 178, 92}; }                         // the thrust bar (touch: set)
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
struct AutoKey { const char* name; int mode; };
const AutoKey kAuto[8] = {{"СТОП ВРАЩ.", NAVMODE_KILLROT}, {"ГОРИЗОНТ", NAVMODE_HLEVEL}, {"ПРОГРАД", NAVMODE_PROGRADE}, {"РЕТРОГРАД", NAVMODE_RETROGRADE},
                          {"НОРМАЛЬ +", NAVMODE_NORMAL}, {"НОРМАЛЬ -", NAVMODE_ANTINORMAL}, {"ВЫСОТА", NAVMODE_HOLDALT}, {"АВТОПОСАДКА", 0}};
const int kHud[4] = {HUD_ORBIT, HUD_SURFACE, HUD_DOCKING, HUD_NONE};
const char* const kHudName[4] = {"ОРБИТА", "ПОВЕРХН.", "СТЫКОВКА", "ВЫКЛ"};
const int kRcs[3] = {RCS_ROT, RCS_LIN, RCS_NONE};
const char* const kRcsName[3] = {"ВРАЩЕНИЕ", "ЛИНЕЙНОЕ", "ВЫКЛ"};

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

}  // namespace

// ============================================================================================================

void TantraDisplays::OnVisual(VISHANDLE vis) {
    DEVMESHHANDLE dm = t_->GetDevMesh(vis, vcMesh_);
    const DWORD f = OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_NOMIPMAPS;
    int bound = 0;
    for (int k = 0; k < kScreens; k++) {                                  // the width from the screen's aspect (InteriorLayout.h)
        if (!s_[k] && kTouch[k].h > 0) {
            const int w = int(kH[k] * kTouch[k].w / kTouch[k].h + 0.5);
            if (k == kTerminal) gW[k] = (std::max)(w, 800);
            else if (std::abs(w - gW[k]) > gW[k] / 30)
                oapiWriteLogV("Tantra displays: screen %d aspect %.3f differs from the layout (%d px wide, laid out for %d)", k, kTouch[k].w / kTouch[k].h, w, gW[k]);
        }
        if (!s_[k]) s_[k] = oapiCreateSurfaceEx(gW[k], kH[k], f);
        if (dm && s_[k] && oapiSetTexture(dm, kTouch[k].slot, s_[k])) bound++;
    }
    oapiWriteLogV("Tantra displays: %d of %d touch screens bound", bound, int(kScreens));
    t_redraw_ = 0.0;
}

void TantraDisplays::Step(double dt) {
    if ((t_redraw_ -= dt) > 0.0) return;
    t_redraw_ = 0.2;
    DrawSide(kLeft); DrawSide(kRight); DrawTerminal(); DrawEngines();
}

bool TantraDisplays::Touch(int screen, double u, double v) {
    if (screen < 0 || screen >= kScreens) return false;
    const double x = u * gW[screen], y = v * kH[screen];
    bool r = false;
    if (screen <= kRight) r = TouchSide(screen, x, y);
    else if (screen == kTerminal) r = TouchTerminal(x, y);
    else r = TouchEngines(x, y);
    t_redraw_ = 0.0;                                                      // show the result at once
    return r;
}

void TantraDisplays::Shutdown() {
    for (SURFHANDLE& s : s_) if (s) { oapiDestroySurface(s); s = nullptr; }
}

// ---- elbow displays ----------------------------------------------------------------------------------------

void TantraDisplays::DrawSide(int k) {
    SURFHANDLE s = s_[k];
    if (!s) return;
    const int mode = mode_[k];
    oapiClearSurface(s, 0xFF000000 | kBezel);
    R4 rr[3]; int n = 0;
    if (mode != M_OFF && mode != M_MFD) {                                // the page: live sections of the ship's 2D panels
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
        if (mode != M_OFF) P.Box(kContent.x0, 8, kContent.x1, kContent.y1, kGlass, 0x303830);
        if (mode == M_MFD) {
            for (int b = 0; b < 2; b++) {
                const int id = 2 * k + b;
                char t[16]; std::snprintf(t, sizeof t, "MFD %d", id + 1);
                P.Text(8 + b * 504 + 252, 16, t, 16, cGrey, 1);
                const R4 d = MfdDisp(b);
                P.Box(d, 0x000000, 0x2C5A3A);
                for (int i = 0; i < 12; i++) {
                    const R4 r = MfdSide(b, i);
                    const char* l = t_->interior_.MfdLabel(id, i);
                    P.Box(r, kKeyOff, 0x404840);
                    if (l && *l) P.Text((r.x0 + r.x1) / 2.0, (r.y0 + r.y1) / 2.0 - 7, l, 13, cCyan, 1);
                }
                static const char* const kb[3] = {"PWR", "SEL", "MNU"};
                for (int j = 0; j < 3; j++) P.Key(MfdBottom(b, j), kb[j], false, j == 0);
            }
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
        for (int b = 0; b < 2; b++)
            if (SURFHANDLE m = t_->interior_.MfdDisplay(2 * k + b)) {
                const R4 d = MfdDisp(b); RECT dr = {d.x0 + 2, d.y0 + 2, d.x1 - 2, d.y1 - 2};
                RECT msr = {0, 0, 256, 256};                         // (a null source rect crashes oapiBlt)
                oapiBlt(s, m, &dr, &msr);
            }
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

// ---- the flight terminal -----------------------------------------------------------------------------------

void TantraDisplays::DrawTerminal() {
    SURFHANDLE s = s_[kTerminal];
    if (!s) return;
    Tantra* t = t_;
    // the mode: by itself the atmosphere below 100 km where there is air, the space above; a tab overrides it until the
    // automatic choice changes again
    const bool autoAtmo = t->GetAltitude() < 100e3 && t->GetAtmPressure() > 0.0;
    if (autoAtmo != lastAutoAtmo_) { lastAutoAtmo_ = autoAtmo; termMode_ = -1; }
    const bool atmo = termMode_ < 0 ? autoAtmo : termMode_ == 1;
    oapiClearSurface(s, 0xFF000000 | kBezel);
    Paint P(s, glyphs_);
    if (!P.Ok()) return;
    // left: HUD and RCS; right: the autopilots
    P.Text(30, 12, "HUD", 13, cGrey, 1); P.Text(30, 12 + 4 * 43, "РСУ", 13, cGrey, 1);
    const int hud = oapiGetHUDMode(), rcs = t->GetAttitudeMode();
    for (int i = 0; i < 4; i++) P.Key(TermLeft(i), kHudName[i], hud == kHud[i]);
    for (int i = 0; i < 3; i++) P.Key(TermLeft(4 + i), kRcsName[i], rcs == kRcs[i]);
    for (int i = 0; i < 8; i++) P.Key(TermAuto(i), kAuto[i].name, kAuto[i].mode && t->GetNavmodeState(kAuto[i].mode), false, !kAuto[i].mode);
    const R4 G = TermGlass();
    P.Box(G, kGlass, 0x303830);
    P.Key(TermTab(0), "КОСМОС", !atmo); P.Key(TermTab(1), "АТМОСФЕРА", atmo);
    char b[96];
    int ap = -1; for (int i = 0; i < 7; i++) if (t->GetNavmodeState(kAuto[i].mode)) { ap = i; break; }
    std::snprintf(b, sizeof b, "автопилот: %s", ap >= 0 ? kAuto[ap].name : "ВЫКЛ");
    P.Text(G.x1 - 12, 290, b, 12, ap >= 0 ? cOrange : cGrey, 2);
    const double pitch = t->GetPitch(), bank = t->GetBank();
    const double ca = std::cos(bank), sa = std::sin(bank);
    VECTOR3 hv; t->GetHorizonAirspeedVector(hv);
    double hdg = 0.0; oapiGetHeading(t->GetHandle(), &hdg);
    const double alt = t->GetAltitude(), tas = t->GetAirspeed();
    const double thr = t->GetThrusterGroupLevel(THGROUP_MAIN);
    if (!atmo) {                                                          // SPACE: thrust, fuel, the system, a horizon, the readouts
        const bool ana = t->engineSet_ == Tantra::EngineSet::Anamezon;
        const double mx = t->GetMaxFuelMass(), fuel = mx > 0 ? t->GetFuelMass() / mx : 0.0;
        P.Text(G.x0 + 16, 18, "ПОЛЁТ · КОСМОС", 16, cWhite);
        std::snprintf(b, sizeof b, "ТЯГА  %3.0f %%  (касание шкалы)", thr * 100); P.Text(G.x0 + 16, 52, b, 13, cCyan); P.Bar(TermThrust(), thr, kCyanF);
        std::snprintf(b, sizeof b, "ТОПЛИВО  %3.0f %%", fuel * 100); P.Text(G.x0 + 16, 106, b, 13, fuel < .1 ? cRed : cCyan);
        P.Bar({G.x0 + 16, 126, G.x0 + 178, 138}, fuel, fuel < .1 ? kRedF : kGreenF);
        static const char* const kStage[4] = {"ВЫКЛ", "ПОЛЕ", "ЛУЧ", "ПОДАЧА"};
        if (ana) std::snprintf(b, sizeof b, "АНАМЕЗОН: %s", kStage[int(t->ignition_.Stage()) & 3]);
        else std::snprintf(b, sizeof b, "ПЛАНЕТАРНЫЕ (%s)", t->marchHigh_ ? "железо" : "аргон");
        P.Text(G.x0 + 16, 152, b, 13, ana ? cOrange : cCyan);
        std::snprintf(b, sizeof b, "РСУ: %s", rcs == RCS_ROT ? "ВРАЩЕНИЕ" : rcs == RCS_LIN ? "ЛИНЕЙНОЕ" : "ВЫКЛ"); P.Text(G.x0 + 16, 290, b, 12, cGrey);
        const int cx = (G.x0 + G.x1) / 2 - 10, cy = 176, r = 96;
        P.Circle(cx, cy, r, 0x60C060);
        const double k = r / 0.7;                                         // 0.7 rad from the centre to the rim
        for (int deg = -30; deg <= 30; deg += 10) {
            const double off = (pitch - deg * RAD) * k;
            if (std::fabs(off) > r * 0.9) continue;
            const double hw = deg == 0 ? r * 0.95 : r * 0.3, px = -sa * off, py = ca * off;
            P.Line(int(cx + px - ca * hw), int(cy + py - sa * hw), int(cx + px + ca * hw), int(cy + py + sa * hw), deg == 0 ? 0x60E070 : 0x407040, deg == 0 ? 2 : 1);
        }
        P.Line(cx - 46, cy, cx - 14, cy, 0x3CA0FF, 3); P.Line(cx + 14, cy, cx + 46, cy, 0x3CA0FF, 3);
        const int rx = G.x1 - 156;
        std::snprintf(b, sizeof b, alt > 10000 ? "ВЫС   %.1f км" : "ВЫС   %.0f м", alt > 10000 ? alt / 1000 : alt); P.Text(rx, 54, b, 13, cCyan);
        std::snprintf(b, sizeof b, "Vy    %+.1f м/с", hv.y); P.Text(rx, 82, b, 13, hv.y < -5 ? cRed : cCyan);
        std::snprintf(b, sizeof b, "V     %.0f м/с", tas); P.Text(rx, 110, b, 13, cCyan);
        std::snprintf(b, sizeof b, "M     %.2f", t->GetMachNumber()); P.Text(rx, 138, b, 13, cCyan);
        std::snprintf(b, sizeof b, "g     %.2f", t->accelG_); P.Text(rx, 166, b, 13, t->gLimitOn_ && t->accelG_ > t->gLimit_ * .9 ? cRed : cCyan);
        std::snprintf(b, sizeof b, "АТАКИ %.0f°", t->GetAOA() * DEG); P.Text(rx, 194, b, 13, cCyan);
        std::snprintf(b, sizeof b, "КУРС  %03.0f°", hdg * DEG); P.Text(rx, 222, b, 13, cCyan);
        return;
    }
    // ATMOSPHERE: the aircraft screen - a big attitude indicator, speed tape left, altitude tape right, heading tape on top
    P.Text(G.x0 + 16, 18, "ПОЛЁТ · АТМОСФЕРА", 16, cWhite);
    const int cx = (G.x0 + G.x1) / 2, top = 84, bot = 296, cy = (top + bot) / 2, half = (bot - top) / 2;
    const int ax0 = cx - half, ax1 = cx + half;
    const double k = half / (25.0 * RAD);                                 // 25 deg from the centre to the edge
    // sky, then the ground: the part of the square below the horizon line (rotated by the bank, moved by the pitch)
    P.Box(ax0, top, ax1, bot, 0x7A4A20, 0x606860);
    {
        const double sq[8] = {double(ax0), double(top), double(ax1), double(top), double(ax1), double(bot), double(ax0), double(bot)};
        double out[16];
        const double hx = cx - sa * pitch * k, hy = cy + ca * pitch * k;  // a point on the horizon line
        const int m = ClipHalf(sq, 4, -sa, ca, -(-sa * hx + ca * hy), out);   // ground side: n = (-sin, cos), y down
        if (m >= 3) {
            IVECTOR2 pt[8]; for (int i = 0; i < m; i++) { pt[i].x = long(out[2 * i]); pt[i].y = long(out[2 * i + 1]); }
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
    // the flight path marker (where the ship goes): the flight path angle against the pitch, the slip sideways
    const double gs = std::hypot(hv.x, hv.z), fpa = std::atan2(hv.y, (std::max)(gs, 1.0)), slip = t->GetSlipAngle();
    const double lim = half * .9;
    const int fx = int(cx + (std::max)(-lim, (std::min)(lim, slip * k))), fy = int(cy - (std::max)(-lim, (std::min)(lim, (fpa - pitch) * k)));
    P.Circle(fx, fy, 8, 0x60E070); P.Line(fx - 18, fy, fx - 8, fy, 0x60E070); P.Line(fx + 8, fy, fx + 18, fy, 0x60E070); P.Line(fx, fy - 8, fx, fy - 15, 0x60E070);
    P.Line(cx - 60, cy, cx - 20, cy, 0x3CA0FF, 4); P.Line(cx + 20, cy, cx + 60, cy, 0x3CA0FF, 4); P.Line(cx - 3, cy, cx + 3, cy, 0x3CA0FF, 4);
    // the heading tape (top)
    {
        const int y0 = 48, y1 = 76, w = ax1 - ax0; const double hd = hdg * DEG, ppd = w / 60.0;     // 60 deg across
        P.Box(ax0, y0, ax1, y1, 0x101410, 0x404840);
        for (int d = int(std::floor((hd - 30) / 5)) * 5; d <= hd + 30; d += 5) {
            const int x = int(cx + (d - hd) * ppd); if (x < ax0 + 2 || x > ax1 - 2) continue;
            P.Line(x, y1 - (d % 10 ? 6 : 12), x, y1, 0xD0D0D0, 1);
            if (d % 10 == 0) { char h[8]; std::snprintf(h, sizeof h, "%03d", ((d % 360) + 360) % 360); P.Text(x, y0 + 3, h, 11, cWhite, 1); }
        }
        P.Line(cx, y0, cx, y1, 0x3CA0FF, 2);
    }
    // the speed tape (left) and the altitude tape (right)
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
    std::snprintf(b, sizeof b, "V м/с   M %.2f", t->GetMachNumber()); P.Text(G.x0 + 10, 66, b, 11, cCyan);
    std::snprintf(b, sizeof b, "H м   Vy %+.1f", hv.y); P.Text(G.x1 - 10, 66, b, 11, hv.y < -5 ? cRed : cCyan, 2);
    // the line under the attitude indicator: angle of attack, slip, g, the pods' nozzles, the flight path angle (glide)
    std::snprintf(b, sizeof b, "АТАКИ %.1f°   СКОЛЬЖ. %.1f°   g %.2f   ГОНДОЛЫ %.0f°   ГЛИССАДА %+.1f°", t->GetAOA() * DEG, slip * DEG, t->accelG_, t->podAngle_, fpa * DEG);
    P.Text(cx, 300, b, 10, t->gLimitOn_ && t->accelG_ > t->gLimit_ * .9 ? cRed : cCyan, 1);
}

bool TantraDisplays::TouchTerminal(double x, double y) {
    Tantra* t = t_;
    for (int i = 0; i < 2; i++) if (TermTab(i).In(x, y)) { termMode_ = i; return true; }
    for (int i = 0; i < 4; i++) if (TermLeft(i).In(x, y)) { oapiSetHUDMode(kHud[i]); return true; }
    for (int i = 0; i < 3; i++) if (TermLeft(4 + i).In(x, y)) { t->SetAttitudeMode(kRcs[i]); return true; }
    for (int i = 0; i < 8; i++)
        if (TermAuto(i).In(x, y)) {
            const int m = kAuto[i].mode;
            if (!m) return true;                                         // autolanding: not in the ship yet
            t->ToggleNav(m);                                             // the autopilots are switched on from here only
            return true;
        }
    const bool atmo = termMode_ < 0 ? lastAutoAtmo_ : termMode_ == 1;
    if (!atmo && InGrown(TermThrust(), x, y)) { SetMainLevel(AlongX(TermThrust(), x)); return true; }
    return false;
}

// The thrust set by a touch on a scale: the same set-points as the keys (the main group drives the anamezon cups or the
// marching cup; the pods follow it unless they hover, then the hover group). The ship applies its caps (g, safety) each step.
void TantraDisplays::SetMainLevel(double f) { t_->SetThrusterGroupLevel(THGROUP_MAIN, f); }
void TantraDisplays::SetPodLevel(double f) {
    if (t_->GetGroupThrusterCount(THGROUP_HOVER) > 0) t_->SetThrusterGroupLevel(THGROUP_HOVER, f);
    else t_->SetThrusterGroupLevel(THGROUP_MAIN, f);
}

// ---- the engine console ------------------------------------------------------------------------------------

void TantraDisplays::DrawEngines() {
    SURFHANDLE s = s_[kEngines];
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
