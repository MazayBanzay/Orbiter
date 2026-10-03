// TantraBridge: the commander's touch screens and the holo panel. See TantraBridge.h.
#include "TantraBridge.h"
#include "InteriorLayout.h"
#include "PanelLayout.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tantra::interior;

namespace {
constexpr int kScr = 512, kTabH = 56;                 // own screens: 512 x 512, the tab row on top
constexpr int kHoloW = 1024, kHoloH = 292;            // the holo panel (1.05 x 0.30 m)
struct Page { const char* tab; int x0, y0, x1, y1; }; // a section of panel.dds (texture px)
struct Screen { const char* title; int n; Page p[4]; };
// left rear, left front (mechanics), right rear, right front (engineering)
const Screen kScreens[4] = {
    {"ШАССИ И ПОДЪЁМ", 2, {{"ПОЛОЖЕНИЕ", 10, 1486, 346, 1874}, {"ЛАФЕТ, ШАССИ", 350, 1486, 706, 1874}}},
    {"АНГАР, ШЛЮЗ, ГРЕБНИ", 2, {{"АНГАР, ПОРТ", 1034, 1486, 1592, 1874}, {"КОРПУС", 714, 1486, 1024, 1874}}},
    {"ДВИГАТЕЛИ, ПИТАНИЕ", 4, {{"ПУСК", 846, 1070, 1194, 1436}, {"АНАМЕЗОН", 10, 6, 338, 398}, {"ЛОВУШКИ", 342, 6, 630, 398},
                               {"ЭНЕРГИЯ", 638, 6, 910, 398}}},
    {"ЖИЗНЬ, КОРПУС, ЭКИПАЖ", 3, {{"КОМПЕНСАТОР", 918, 6, 1206, 398}, {"ЭКИПАЖ", 1214, 6, 1592, 398}, {"КОРПУС", 714, 1486, 1024, 1874}}},
};
// where a page is drawn on the screen (aspect kept, under the tabs)
RECT PageRect(const Page& p) {
    const double sw = kScr - 16, sh = kScr - kTabH - 12, w = p.x1 - p.x0, h = p.y1 - p.y0, k = (std::min)(sw / w, sh / h);
    const int dw = int(w * k), dh = int(h * k), x = (kScr - dw) / 2, y = kTabH + 6 + (int(sh) - dh) / 2;
    return {x, y, x + dw, y + dh};
}
// the automation buttons of the holo panel (zone 4: x 768..1024), 2 columns x 4 rows
struct NavBtn { const char* name; int mode; };
const NavBtn kNav[8] = {{"KILLROT", NAVMODE_KILLROT}, {"HLEVEL", NAVMODE_HLEVEL}, {"PROGRADE", NAVMODE_PROGRADE}, {"RETRO", NAVMODE_RETROGRADE},
                        {"NORMAL", NAVMODE_NORMAL}, {"ANTINORM", NAVMODE_ANTINORMAL}, {"HOLDALT", NAVMODE_HOLDALT}, {"АВТОПОСАДКА", 0}};
RECT NavRect(int i) { const int c = i % 2, r = i / 2, x = 780 + c * 120, y = 48 + r * 58; return {x, y, x + 112, y + 48}; }
}  // namespace

// The panel areas one can click (as the 2D panels register them); the handler gets the point inside the area.
bool TantraBridge::ClickPanel(const BridgeHooks& hk, double tx, double ty) {
    using namespace tantra::panel;
    static const int kClick[] = {A_LEVER, A_SEL_PLAN, A_SEL_ANA, A_PODS_AFT, A_PODS_DOWN, A_STOP, A_TRAPS, A_TRAPSEL, A_GLIM, A_GSTEP,
                                 A_OVERRIDE, A_AIRLOCK, A_EVA, A_CREWSEL, M_SEL_PLAN, M_SEL_ANA, M_START, M_STOP, L_GEAR, L_SET_LEVEL,
                                 L_SET_STAND, L_ERECT, L_PORT, L_CRESTS, L_PODS_AFT, L_PODS_DOWN, L_HANGAR, L_ROVERS, L_AIRLOCK, L_EVA,
                                 L_CREWSEL, L_PT_LIFT, L_PT_LOAD, L_PT_DROP, L_PT_STOP};
    if (!hk.Click) return false;
    for (int a : kClick) {
        const int* r = kArea[a];
        if (tx >= r[0] && tx <= r[2] && ty >= r[1] && ty <= r[3]) { hk.Click(hk.ctx, a, int(tx - r[0]), int(ty - r[1])); return true; }
    }
    return false;
}

void TantraBridge::OnVisual(VISHANDLE vis) {
    DEVMESHHANDLE dm = v_->GetDevMesh(vis, vcMesh_);
    const DWORD f = OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_NOMIPMAPS;
    int bound = 0;
    for (int k = 0; k < kDispCount && k < 4; k++) {
        if (!scr_[k]) scr_[k] = oapiCreateSurfaceEx(kScr, kScr, f);
        if (dm && scr_[k] && oapiSetTexture(dm, kDisp[k].slot, scr_[k])) bound++;
    }
    if (!holo_) holo_ = oapiCreateSurfaceEx(kHoloW, kHoloH, f);
    if (dm && holo_ && oapiSetTexture(dm, kHolo.slot, holo_)) bound++;
    if (!fTab_) { fTab_ = oapiCreateFont(22, true, "Arial", FONT_BOLD); fBig_ = oapiCreateFont(34, true, "Arial", FONT_BOLD); fSmall_ = oapiCreateFont(20, true, "Arial"); }
    oapiWriteLogV("Tantra bridge: touch screens bound %d of %d", bound, (kDispCount < 4 ? kDispCount : 4) + 1);
    t_ = 0.0;
}

void TantraBridge::DrawScreen(int k) {
    SURFHANDLE s = scr_[k];
    if (!s) return;
    const Screen& sc = kScreens[k];
    oapiClearSurface(s, 0xFF0A0F0D);
    const Page& pg = sc.p[page_[k]];
    if (hk_.PanelTex) {                                                  // the page: a live section of the panels
        RECT tr = PageRect(pg), sr = {pg.x0, pg.y0, pg.x1, pg.y1};
        if (SURFHANDLE t = hk_.PanelTex(hk_.ctx)) oapiBlt(s, t, &tr, &sr);
    }
    oapi::Sketchpad* skp = oapiGetSketchpad(s);
    if (!skp) return;
    const int w = kScr / sc.n;
    oapi::Brush* on = oapiCreateBrush(0x506048); oapi::Brush* off = oapiCreateBrush(0x1C2420);
    oapi::Pen* edge = oapiCreatePen(1, 2, 0x4C5A50);
    skp->SetPen(edge); skp->SetFont(fTab_); skp->SetTextAlign(oapi::Sketchpad::CENTER, oapi::Sketchpad::BASELINE);
    skp->SetBackgroundMode(oapi::Sketchpad::BK_TRANSPARENT);
    for (int i = 0; i < sc.n; i++) {                                     // the tabs
        skp->SetBrush(i == page_[k] ? on : off);
        skp->Rectangle(i * w + 2, 2, (i + 1) * w - 2, kTabH - 4);
        if (glyphs_.Ok()) glyphs_.DrawA(skp, i * w + w / 2, kTabH - 18 - 14, sc.p[i].tab, 2, i == page_[k] ? 1 : 4, 14, 1);   // atlas: Cyrillic
        else { skp->SetTextColor(i == page_[k] ? 0x60E0FF : 0x3090C0); skp->Text(i * w + w / 2, kTabH - 18, sc.p[i].tab, int(std::strlen(sc.p[i].tab))); }
    }
    skp->SetPen(nullptr); skp->SetBrush(nullptr);
    oapiReleasePen(edge); oapiReleaseBrush(on); oapiReleaseBrush(off);
    oapiReleaseSketchpad(skp);
}

void TantraBridge::DrawHolo() {
    if (!holo_) return;
    HoloData d; if (hk_.Holo) hk_.Holo(hk_.ctx, &d);
    oapiClearSurface(holo_, 0xFF041014);
    oapi::Sketchpad* skp = oapiGetSketchpad(holo_);
    if (!skp) return;
    const DWORD C = 0xFFDC5A, O = 0x3CA0FF, G = 0x60E070, R = 0x4040F0;  // BGR: cyan, amber, green, red
    oapi::Pen* line = oapiCreatePen(1, 2, 0xC08030);
    oapi::Brush* fillC = oapiCreateBrush(0x806020); oapi::Brush* fillDim = oapiCreateBrush(0x201810);
    skp->SetPen(line); skp->SetBackgroundMode(oapi::Sketchpad::BK_TRANSPARENT);
    for (int z = 1; z < 4; z++) skp->Line(z * 256, 8, z * 256, kHoloH - 8);
    auto txt = [&](int x, int y, const char* t, DWORD c, oapi::Font* f, int align = 0) {
        if (glyphs_.Ok()) {                                              // the atlas: D3D9 system fonts draw no Cyrillic on a surface
            const int sz = f == fBig_ ? 3 : f == fTab_ ? 2 : 1; const double px = f == fBig_ ? 22 : f == fTab_ ? 14 : 13;
            glyphs_.DrawA(skp, x, y + 2, t, sz, TantraGlyphs::Colour(c), px, align);
            return;
        }
        skp->SetFont(f); skp->SetTextColor(c);
        skp->SetTextAlign(align == 0 ? oapi::Sketchpad::LEFT : align == 1 ? oapi::Sketchpad::CENTER : oapi::Sketchpad::RIGHT);
        skp->Text(x, y, t, int(std::strlen(t)));
    };
    auto bar = [&](int x, int y, int w, int h, double f, DWORD c) {
        skp->SetBrush(fillDim); skp->Rectangle(x, y, x + w, y + h);
        oapi::Brush* b = oapiCreateBrush(c); skp->SetBrush(b);
        const int fw = int(w * (f < 0 ? 0 : f > 1 ? 1 : f)); if (fw > 2) skp->Rectangle(x, y, x + fw, y + h);
        skp->SetBrush(nullptr); oapiReleaseBrush(b);
    };
    char b[64];
    // 1 engines
    txt(128, 6, "ДВИГАТЕЛИ", C, fTab_, 1);
    txt(12, 44, d.anamezon ? "АНАМЕЗОН" : "МАРШЕВЫЕ", d.anamezon ? O : C, fSmall_);
    txt(12, 84, "ТЯГА", C, fSmall_); std::snprintf(b, sizeof b, "%3.0f %%", d.thrust * 100); txt(244, 76, b, C, fBig_, 2);
    bar(12, 120, 232, 18, d.thrust, 0xFFC040);
    txt(12, 160, "ТОПЛИВО", C, fSmall_); std::snprintf(b, sizeof b, "%3.0f %%", d.fuel * 100); txt(244, 152, b, d.fuel < .1 ? R : C, fBig_, 2);
    bar(12, 196, 232, 18, d.fuel, d.fuel < .1 ? 0x4040F0 : 0x60E070);
    // 2 anamezon
    txt(384, 6, "АНАМЕЗОН", C, fTab_, 1);
    const char* st = d.anaFeed ? "ПОДАЧА" : d.beam >= .5 ? "ЛУЧ" : d.field >= .3 ? "ПОЛЕ" : "ВЫКЛ";
    txt(268, 44, "РЕЖИМ", C, fSmall_); txt(500, 38, st, d.anaFeed ? O : d.field >= .3 ? G : C, fBig_, 2);
    txt(268, 92, "ПОЛЕ", C, fSmall_); bar(340, 96, 160, 16, d.field, 0x60E070);
    txt(268, 128, "ЛУЧ", C, fSmall_); bar(340, 132, 160, 16, d.beam, 0x3CA0FF);
    txt(268, 172, d.anamezon ? (d.anaFeed ? "анамезон: подача в камеры" : "анамезон выбран") : "анамезон не выбран", d.anamezon ? O : 0x808060, fSmall_);
    // 3 flight
    txt(640, 6, "ПОЛЁТ", C, fTab_, 1);
    {                                                                   // the horizon: pitch ladder line, bank
        const int cx = 574, cy = 150, r = 46;
        skp->SetBrush(fillDim); skp->Ellipse(cx - r, cy - r, cx + r, cy + r); skp->SetBrush(nullptr);
        const double pp = d.pitch * 60.0, ca = std::cos(d.bank), sa = std::sin(d.bank);
        skp->Line(int(cx - ca * r), int(cy + pp - sa * r), int(cx + ca * r), int(cy + pp + sa * r));
        skp->Line(cx - 10, cy, cx + 10, cy);
    }
    std::snprintf(b, sizeof b, "H %.0f м", d.alt); txt(630, 44, b, C, fSmall_);
    std::snprintf(b, sizeof b, "Vy %+.1f м/с", d.vs); txt(630, 72, b, d.vs < -5 ? R : C, fSmall_);
    std::snprintf(b, sizeof b, "V %.0f м/с", d.speed); txt(630, 100, b, C, fSmall_);
    std::snprintf(b, sizeof b, "M %.2f", d.mach); txt(630, 128, b, C, fSmall_);
    std::snprintf(b, sizeof b, "g %.2f", d.g); txt(630, 156, b, C, fSmall_);
    std::snprintf(b, sizeof b, "АТАКИ %.0f°", d.aoa * 57.2958); txt(630, 184, b, C, fSmall_);
    std::snprintf(b, sizeof b, "КУРС %03.0f°", d.hdg * 57.2958); txt(530, 212, b, C, fSmall_);
    // 4 automation
    txt(896, 6, "АВТОМАТИКА", C, fTab_, 1);
    for (int i = 0; i < 8; i++) {
        const RECT r = NavRect(i); const bool active = kNav[i].mode && (d.navOn & (1 << kNav[i].mode));
        skp->SetBrush(active ? fillC : fillDim); skp->Rectangle(r.left, r.top, r.right, r.bottom); skp->SetBrush(nullptr);
        txt((r.left + r.right) / 2, r.top + 12, kNav[i].name, kNav[i].mode ? (active ? 0xFFFFE0 : C) : 0x606050, fSmall_, 1);
    }
    skp->SetPen(nullptr); oapiReleasePen(line); oapiReleaseBrush(fillC); oapiReleaseBrush(fillDim);
    oapiReleaseSketchpad(skp);
}

void TantraBridge::Step(double dt) {
    if ((t_ -= dt) > 0.0) return;
    t_ = 0.25;
    for (int k = 0; k < kDispCount && k < 4; k++) DrawScreen(k);
    DrawHolo();
}

bool TantraBridge::Touch(int screen, double u, double v) {
    if (screen == 4) {                                                   // the holo panel: the automation buttons
        const double x = u * kHoloW, y = v * kHoloH;
        for (int i = 0; i < 8; i++) {
            const RECT r = NavRect(i);
            if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
                if (kNav[i].mode && hk_.Nav) hk_.Nav(hk_.ctx, kNav[i].mode);
                t_ = 0.0;
                return true;
            }
        }
        return false;
    }
    if (screen < 0 || screen > 3) return false;
    const Screen& sc = kScreens[screen];
    const double x = u * kScr, y = v * kScr;
    if (y < kTabH) {                                                     // a tab: the page
        const int i = int(x / (kScr / sc.n));
        if (i >= 0 && i < sc.n) { page_[screen] = i; t_ = 0.0; }
        return true;
    }
    const Page& pg = sc.p[page_[screen]];
    const RECT r = PageRect(pg);
    if (x < r.left || x > r.right || y < r.top || y > r.bottom) return true;
    const double tx = pg.x0 + (x - r.left) / double(r.right - r.left) * (pg.x1 - pg.x0);
    const double ty = pg.y0 + (y - r.top) / double(r.bottom - r.top) * (pg.y1 - pg.y0);
    ClickPanel(hk_, tx, ty);
    t_ = 0.0;
    return true;
}

void TantraBridge::Shutdown() {
    for (SURFHANDLE& s : scr_) if (s) { oapiDestroySurface(s); s = nullptr; }
    if (holo_) { oapiDestroySurface(holo_); holo_ = nullptr; }
    for (oapi::Font** f : {&fTab_, &fBig_, &fSmall_}) if (*f) { oapiReleaseFont(*f); *f = nullptr; }
}
