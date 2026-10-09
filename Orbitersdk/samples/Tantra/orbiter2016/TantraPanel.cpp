// Tantra: 2D control panels (Orbiter 2010 Panel2D), three levels after the DG-IV:
//   MAIN  (id 0, Ctrl+Up / Ctrl+Down from here): four MFDs + engine essentials;
//   UPPER (id 1): power and propulsion systems (the original Efremov post);
//   LOWER (id 2): undercarriage, carriage, hull, hangar, airlock.
//
// One texture (tools/gen_panel.py -> Textures\Tantra\panel.dds) holds the panel
// background, a cp1251 glyph atlas and sprites. Dynamic elements are drawn by
// blitting from that texture onto itself (the DeltaGlider technique), so Russian
// text works under any graphics client. Instruments follow Efremov's control post:
// the start lever, the pult slot with the four boron-nitride cylinders, the orange
// columns of anamezon with black needles, the rods of ion charges.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "PanelLayout.h"
#include "Tantra.h"

using namespace tantra;
using namespace tantra::panel;
namespace sp = tantra::spec;

namespace {

SURFHANDLE g_panelTex = nullptr;
// The atlas the panels are drawn from (fonts, palette swatches, sprites). Orbiter 2016: panel.dds itself (D3D9Client
// blitted a loaded texture into itself). Orbiter 2024: its D3D9Client blits only into a render target and never from a
// surface into itself (a critical error) - so panel.dds stays the atlas and the panels live in a render-target copy.
SURFHANDLE g_panelSrc = nullptr;
SURFHANDLE PanelSrc(SURFHANDLE s) { return g_panelSrc ? g_panelSrc : s; }

SURFHANDLE LoadPanelTexture() {
#if __has_include("gcCoreAPI.h")   // Orbiter 2024
    g_panelSrc = oapiLoadSurfaceEx("Tantra\\panel.dds", OAPISURFACE_TEXTURE);
    if (!g_panelSrc) return nullptr;
    SURFHANDLE t = oapiCreateSurfaceEx(kTexW, kTexH, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS);
    if (!t) { oapiDestroySurface(g_panelSrc); g_panelSrc = nullptr; return nullptr; }
    RECT all = {0, 0, kTexW, kTexH};
    oapiBlt(t, g_panelSrc, &all, &all);
    return t;
#else                               // Orbiter 2016
    return oapiLoadTexture("Tantra\\panel.dds");
#endif
}

const int* AreaRect(int id) { return kArea[id]; }
int AreaW(int id) { return kArea[id][2] - kArea[id][0]; }
int AreaH(int id) { return kArea[id][3] - kArea[id][1]; }

}  // namespace

SURFHANDLE Tantra::PanelTex() {           // the panels' texture, also for the bridge console (the VC)
    if (!g_panelTex) g_panelTex = LoadPanelTexture();
    return g_panelTex;
}

void Tantra::ReleasePanelTexture() {
#if __has_include("gcCoreAPI.h")
    if (g_panelTex) oapiDestroySurface(g_panelTex);
    if (g_panelSrc) oapiDestroySurface(g_panelSrc);
#else
    if (g_panelTex) oapiReleaseTexture(g_panelTex);
#endif
    g_panelTex = nullptr;
    g_panelSrc = nullptr;
}

// --- Loading -------------------------------------------------------------------------

// MFD units on the MAIN panel: the three button areas of each and its Orbiter MFD id.
struct MfdUnit { int left, right, bottom, mfd; };
const MfdUnit kMfdAreas[4] = {
    {M_MFD2_L, M_MFD2_R, M_MFD2_B, MFD_USER1}, {M_MFD0_L, M_MFD0_R, M_MFD0_B, MFD_LEFT},
    {M_MFD1_L, M_MFD1_R, M_MFD1_B, MFD_RIGHT}, {M_MFD3_L, M_MFD3_R, M_MFD3_B, MFD_USER1 + 1},
};

bool Tantra::clbkLoadPanel2D(int id, PANELHANDLE hPanel, DWORD viewW, DWORD) {
    if (id < 0 || id > 2) return false;
    if (!g_panelTex) g_panelTex = LoadPanelTexture();
    if (!g_panelTex) return false;
    const PanelGeom& pg = kPanel[id];

    // Background billboard: the panel's rectangle of the shared texture, 1:1 in panel coordinates.
    const float u0 = float(pg.texX) / kTexW, u1 = float(pg.texX + pg.w) / kTexW;
    const float v0 = float(pg.texY) / kTexH, v1 = float(pg.texY + pg.h) / kTexH;
    NTVERTEX vtx[4] = {
        {0, 0, 0, 0, 0, 0, u0, v0},
        {float(pg.w), 0, 0, 0, 0, 0, u1, v0},
        {float(pg.w), float(pg.h), 0, 0, 0, 0, u1, v1},
        {0, float(pg.h), 0, 0, 0, 0, u0, v1},
    };
    WORD idx[6] = {0, 1, 2, 2, 3, 0};

    if (panelMesh_) oapiDeleteMesh(panelMesh_);
    panelMesh_ = oapiCreateMesh(0, 0);
    MESHGROUP grp;
    std::memset(&grp, 0, sizeof grp);
    oapiAddMeshGroup(panelMesh_, &grp);
    oapiAddMeshGroupBlock(panelMesh_, 0, vtx, 4, idx, 6);
    if (id == PANEL_MAIN) {  // one square group per MFD, drawn over the background
        for (int i = 0; i < 4; ++i) {
            const float x0 = float(kMfdUnit[i][0] + 46), y0 = float(kMfdSY), x1 = x0 + kMfdS, y1 = y0 + kMfdS;
            NTVERTEX mv[4] = {{x0, y0, 0, 0, 0, 0, 0, 0}, {x1, y0, 0, 0, 0, 0, 1, 0},
                              {x0, y1, 0, 0, 0, 0, 0, 1}, {x1, y1, 0, 0, 0, 0, 1, 1}};
            WORD mi[6] = {0, 1, 2, 3, 2, 1};
            oapiAddMeshGroup(panelMesh_, &grp);
            oapiAddMeshGroupBlock(panelMesh_, 1 + i, mv, 4, mi, 6);
        }
    }

    const DWORD attach = id == PANEL_UPPER ? (PANEL_ATTACH_TOP | PANEL_MOVEOUT_TOP) : (PANEL_ATTACH_BOTTOM | PANEL_MOVEOUT_BOTTOM);
    SetPanelBackground(hPanel, &g_panelTex, 1, panelMesh_, pg.w, pg.h, 0, attach);
    const double scale = double(viewW) / pg.w;
    SetPanelScaling(hPanel, scale, (std::max)(scale, 1.0));
    if (id == PANEL_MAIN) oapiSetPanelNeighbours(-1, -1, PANEL_UPPER, PANEL_LOWER);
    else if (id == PANEL_UPPER) oapiSetPanelNeighbours(-1, -1, -1, PANEL_MAIN);
    else oapiSetPanelNeighbours(-1, -1, PANEL_MAIN, -1);
    SetCameraDefaultDirection(_V(0, 0, 1));

    if (id == PANEL_MAIN)
        for (int i = 0; i < 4; ++i) RegisterPanelMFDGeometry(hPanel, kMfdAreas[i].mfd, 0, 1 + i);

    const int click = PANEL_MOUSE_LBDOWN;
    for (int a = 0; a < A_COUNT; ++a) {
        if (kAreaPanel[a] != id) continue;
        RECT r = {kAreaLocal[a][0], kAreaLocal[a][1], kAreaLocal[a][2], kAreaLocal[a][3]};
        int mouse = PANEL_MOUSE_IGNORE, redraw = PANEL_REDRAW_ALWAYS;
        switch (a) {
            case A_LEVER: case A_SEL_PLAN: case A_SEL_ANA: case A_PODS_AFT: case A_PODS_DOWN: case A_STOP:
            case A_TRAPS: case A_TRAPSEL: case A_GLIM: case A_GSTEP: case A_OVERRIDE: case A_AIRLOCK: case A_EVA:
            case A_CREWSEL:
            case M_SEL_PLAN: case M_SEL_ANA: case M_START: case M_STOP: case M_GEAR:
            case L_GEAR: case L_SET_LEVEL: case L_SET_STAND: case L_ERECT: case L_PORT: case L_CRESTS:
            case L_PODS_AFT: case L_PODS_DOWN: case L_HANGAR: case L_ROVERS: case L_AIRLOCK: case L_EVA:
            case L_CREWSEL: case L_PT_LIFT: case L_PT_LOAD: case L_PT_DROP: case L_PT_STOP:
                mouse = click;
                break;
            default:
                break;
        }
        for (const MfdUnit& u : kMfdAreas) {
            if (a == u.left || a == u.right) mouse = PANEL_MOUSE_LBDOWN | PANEL_MOUSE_LBPRESSED | PANEL_MOUSE_ONREPLAY;
            if (a == u.bottom) { mouse = PANEL_MOUSE_LBDOWN | PANEL_MOUSE_ONREPLAY; redraw = PANEL_REDRAW_NEVER; }
        }
        RegisterPanelArea(hPanel, a, r, redraw, mouse, g_panelTex);
        panelCache_[a][0] = '\0';  // force a full redraw
    }
    return true;
}

// --- Mouse ---------------------------------------------------------------------------

bool Tantra::clbkPanelMouseEvent(int id, int event, int mx, int my, void*) {
    for (const MfdUnit& u : kMfdAreas) {  // MFD buttons (same handling as the DeltaGlider)
        if (id == u.left || id == u.right) {
            const int row = (my - kMfdBtnY0) / kMfdBtnPitch, inRow = (my - kMfdBtnY0) % kMfdBtnPitch;
            if (my < kMfdBtnY0 || row > 5 || inRow >= kMfdBtnH) return false;
            oapiProcessMFDButton(u.mfd, row + (id == u.right ? 6 : 0), event);
            return true;
        }
        if (id == u.bottom) {
            if (!(event & PANEL_MOUSE_LBDOWN)) return false;
            if (mx >= 10 && mx < 60) oapiToggleMFD_on(u.mfd);
            else if (mx >= 140 && mx < 190) oapiSendMFDKey(u.mfd, OAPI_KEY_F1);
            else if (mx >= 260 && mx < 310) oapiSendMFDKey(u.mfd, OAPI_KEY_GRAVE);
            else return false;
            return true;
        }
    }
    if (!(event & PANEL_MOUSE_LBDOWN)) return false;
    switch (id) {
        case M_START: ActIgnitionStep(); return true;
        case M_GEAR: oapiSetPanel(PANEL_LOWER); return true;
        case L_GEAR: ActGear(); return true;
        case L_SET_LEVEL:
            if (carriage_.Set() != tantra::Carriage::FlightSet::Level) ActGearSet();
            return true;
        case L_SET_STAND:
            if (carriage_.Set() != tantra::Carriage::FlightSet::Standing) ActGearSet();
            return true;
        case L_ERECT: ActErect(); return true;
        case L_PORT: ActPort(); return true;
        case L_CRESTS: ActCrests(); return true;
        case L_HANGAR: ActHangar(); return true;
        case L_ROVERS: ActRovers(); return true;
        case L_PT_LIFT: ActPortLift(); return true;
        case L_PT_LOAD: ActPortLoad(); return true;
        case L_PT_DROP: ActPortDrop(); return true;
        case L_PT_STOP: ActPortStop(); return true;
        case A_LEVER: {
            int best = 0;
            for (int i = 1; i < 4; ++i)
                if (std::abs(mx - (kLeverX[i] - kArea[A_LEVER][0])) < std::abs(mx - (kLeverX[best] - kArea[A_LEVER][0])))
                    best = i;
            ActIgnitionTo(best);
            return true;
        }
        case A_SEL_PLAN: case M_SEL_PLAN: ActSelectEngine(false); return true;
        case A_SEL_ANA: case M_SEL_ANA: ActSelectEngine(true); return true;
        case A_PODS_AFT: case L_PODS_AFT: ActPodsTo(0.0); return true;
        case A_PODS_DOWN: case L_PODS_DOWN: ActPodsTo(90.0); return true;
        case A_STOP: case M_STOP: ActIgnitionTo(0); return true;
        case A_TRAPS: {
            for (int i = 0; i < sp::kTrapCount; ++i) {
                const int x0 = kTrapX[i] - kArea[A_TRAPS][0];
                if (mx >= x0 - 6 && mx < x0 + kTrapW + 6) {
                    activeTrap_ = i;
                    SelectActiveTrap();
                    Message("Подача анамезона из ловушки %d", "Anamezon feed from trap %d", i + 1);
                    return true;
                }
            }
            return false;
        }
        case A_TRAPSEL: ActNextTrap(); return true;
        case A_GLIM: ActToggleGLimit(); return true;
        case A_GSTEP: ActCycleGLimit(); return true;
        case A_OVERRIDE: ActToggleOverride(); return true;
        case A_AIRLOCK: case L_AIRLOCK: ActToggleAirlock(); return true;
        case A_EVA: case L_EVA: if (mx < AreaW(id) / 2) ActLift(); else ActEva(); return true;   // left: crew lift, right: EVA
        case A_CREWSEL: case L_CREWSEL: ActSelectCrew(mx < AreaW(id) / 2 ? -1 : +1); return true;
    }
    return false;
}

// --- Drawing helpers -----------------------------------------------------------------

bool Tantra::PanelChanged(int areaId, const char* key) {
    if (std::strcmp(panelCache_[areaId], key) == 0) return false;
    std::snprintf(panelCache_[areaId], sizeof panelCache_[areaId], "%s", key);
    return true;
}

// Fills are stretch-blits of the nearest palette swatch: D3D9Client does not colour-fill the
// lower half of the 2048-px texture where the MAIN and LOWER panels live.
void Tantra::PanelFill(SURFHANDLE s, int x, int y, int w, int h, int r, int g, int b) {
    if (w <= 0 || h <= 0) return;
    int best = 0, bestD = 1 << 30;
    for (int i = 0; i < kPaletteN; ++i) {
        const int dr = kPalette[i][0] - r, dg = kPalette[i][1] - g, db = kPalette[i][2] - b;
        const int d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; best = i; }
    }
    RECT src = {kPaletteX + best * 8 + 2, kPaletteY + 2, kPaletteX + best * 8 + 6, kPaletteY + 6};
    RECT tgt = {x, y, x + w, y + h};
    oapiBlt(s, PanelSrc(s), &tgt, &src);
}

void Tantra::PanelClear(SURFHANDLE s, int a) {
    PanelFill(s, kArea[a][0], kArea[a][1], AreaW(a), AreaH(a), kDisplayBg[0], kDisplayBg[1], kDisplayBg[2]);
}

void Tantra::PanelText(SURFHANDLE s, int x, int y, const char* text, int font, int maxChars) {
    for (int n = 0; *text && n < maxChars; ++text, ++n) {
        const int c = static_cast<unsigned char>(*text);
        if (c >= 32) {
            const int i = c - 32;
            oapiBlt(s, PanelSrc(s), x, y, kFontOrigin[font][0] + (i % kAtlasCols) * kCellW,
                    kFontOrigin[font][1] + (i / kAtlasCols) * kCellH, kCellW, kCellH);
        }
        x += kCellW;
    }
}

void Tantra::PanelTextCentered(SURFHANDLE s, const int* r, int y, const char* text, int font) {
    const int maxChars = (r[2] - r[0]) / kCellW;
    const int n = (std::min)(static_cast<int>(std::strlen(text)), maxChars);
    PanelText(s, r[0] + ((r[2] - r[0]) - n * kCellW) / 2, y, text, font, maxChars);
}

void Tantra::PanelBig(SURFHANDLE s, int x, int y, const char* text) {
    for (; *text; ++text, x += kBigW) {
        const char* p = std::strchr(kBigChars, *text);
        if (p) oapiBlt(s, PanelSrc(s), x, y, kBigX + int(p - kBigChars) * kBigW, kBigY, kBigW, kBigH);
    }
}

// state: 0 normal (amber), 1 on (green), 2 warning (red)
void Tantra::PanelButton(SURFHANDLE s, int a, const char* text, int state) {
    char key[128];
    std::snprintf(key, sizeof key, "%d|%s", state, text);
    if (!PanelChanged(a, key)) return;
    static const int border[3][3] = {{120, 90, 40}, {60, 170, 90}, {190, 60, 45}};
    const int* r = AreaRect(a);
    PanelFill(s, r[0], r[1], AreaW(a), AreaH(a), border[state][0], border[state][1], border[state][2]);
    PanelFill(s, r[0] + 2, r[1] + 2, AreaW(a) - 4, AreaH(a) - 4, kDisplayBg[0], kDisplayBg[1], kDisplayBg[2]);
    const int font = state == 1 ? FONT_GREEN : state == 2 ? FONT_RED : FONT_AMBER;
    PanelTextCentered(s, r, r[1] + (AreaH(a) - kCellH) / 2, text, font);
}

// --- Redraw --------------------------------------------------------------------------

bool Tantra::clbkPanelRedrawEvent(int id, int, SURFHANDLE s, void*) {
    char key[256], buf[128];
    const int* r = AreaRect(id);
    const bool anamezon = engineSet_ == EngineSet::Anamezon;
    const double level = GetThrusterGroupLevel(THGROUP_MAIN);
    const bool anaMain = AnaIsMain();
    const bool feeding = anaMain && level > 0.0;

    switch (id) {
        case A_CHAMBERS: {  // the pult slot: four boron-nitride cylinders
            int kind = 0;
            if (ignition_.FieldLevel() >= 0.3) kind = ignition_.BeamLevel() >= 0.5 ? (feeding ? 3 : 2) : 1;
            const int frame = kind ? int(oapiGetSimTime() * 8.0) % 4 : 0;
            std::snprintf(key, sizeof key, "%d %d", kind, frame);
            if (!PanelChanged(id, key)) return false;
            const int sprite = kind ? 1 + (kind - 1) * 4 + frame : 0;
            for (int i = 0; i < 4; ++i)
                oapiBlt(s, PanelSrc(s), kChTileX[i], kChTileY, kChX + sprite * kChW, kChY, kChW, kChH);
            return true;
        }
        case A_LEVER: {  // start handle: off - field - beam - feed
            const int cur = static_cast<int>(ignition_.Stage()), tgt = static_cast<int>(ignition_.Target());
            std::snprintf(key, sizeof key, "%d %d", cur, tgt);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            const int yc = (r[1] + r[3]) / 2;
            PanelFill(s, kLeverX[0], yc - 2, kLeverX[3] - kLeverX[0], 4, 90, 70, 40);
            for (int i = 0; i < 4; ++i) PanelFill(s, kLeverX[i] - 1, yc - 8, 3, 16, 120, 95, 50);
            PanelFill(s, kLeverX[tgt] - 12, r[3] - 5, 24, 3, 80, 220, 120);  // target mark
            oapiBlt(s, PanelSrc(s), kLeverX[cur] - kKnobW / 2, yc - kKnobH / 2, kKnobX, kKnobY, kKnobW, kKnobH);
            return true;
        }
        case A_SEL_PLAN: case M_SEL_PLAN:  // radio pair: which engines the main throttle drives
            PanelButton(s, id, "ПЛАНЕТАРНЫЕ", anamezon ? 0 : 1);
            return true;
        case A_SEL_ANA: case M_SEL_ANA:
            PanelButton(s, id, "АНАМЕЗОН", anamezon ? 1 : 0);
            return true;
        case A_PODS_AFT: case L_PODS_AFT:
            PanelButton(s, id, "НАЗАД 0°", podTarget_ < 45.0 ? 1 : 0);
            return true;
        case A_PODS_DOWN: case L_PODS_DOWN:
            PanelButton(s, id, "ВНИЗ 90°", podTarget_ >= 45.0 ? 1 : 0);
            return true;
        case A_PODS_IND: case L_PODS_IND: {
            const double hov = podHover_ ? GetThrusterGroupLevel(THGROUP_HOVER) : 0.0;
            const char* mode = podHover_ ? "ВИСЕНИЕ (клавиши висения)" : "НЕ В ГРУППЕ";
            std::snprintf(key, sizeof key, "%.0f %d %.0f", podAngle_, podHover_ ? 1 : 0, hov * 100);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            if (podHover_) std::snprintf(buf, sizeof buf, "%3.0f°  %s  %.0f%%", podAngle_, mode, hov * 100);
            else std::snprintf(buf, sizeof buf, "%3.0f°  %s", podAngle_, mode);
            PanelText(s, r[0] + 8, r[1] + (AreaH(id) - kCellH) / 2, buf, podHover_ ? FONT_GREEN : FONT_AMBER,
                      (AreaW(id) - 16) / kCellW);
            return true;
        }
        case A_STOP: case M_STOP:
            PanelButton(s, id, "ОСТАНОВ", ignition_.Target() != IgnStage::Off ? 2 : 0);
            return true;
        case A_TRAPS: {  // orange columns with black needles
            double pct[sp::kTrapCount];
            int n = std::snprintf(key, sizeof key, "%d", activeTrap_);
            for (int i = 0; i < sp::kTrapCount; ++i) {
                pct[i] = 100.0 * GetPropellantMass(trap_[i]) / prm_.trapFuelMass;
                n += std::snprintf(key + n, sizeof key - n, " %.1f", pct[i]);
            }
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            const int h = kTrapY1 - kTrapY0;
            for (int i = 0; i < sp::kTrapCount; ++i) {
                const int x = kTrapX[i];
                if (i == activeTrap_) PanelFill(s, x - 4, kTrapY0 - 4, kTrapW + 8, h + 8, 60, 170, 90);
                PanelFill(s, x - 2, kTrapY0 - 2, kTrapW + 4, h + 4, 50, 45, 38);
                PanelFill(s, x, kTrapY0, kTrapW, h, 22, 20, 18);
                const int fill = int(h * (std::max)(0.0, (std::min)(100.0, pct[i])) / 100.0 + 0.5);
                PanelFill(s, x, kTrapY1 - fill, kTrapW, fill, 230, 140, 40);
                PanelFill(s, x - 6, kTrapY1 - fill - 3, kTrapW + 12, 6, 0, 0, 0);  // black needle
                std::snprintf(buf, sizeof buf, "%.0f%%", pct[i]);
                const int rr[4] = {x - 8, 0, x + kTrapW + 8, 0};
                PanelTextCentered(s, rr, kTrapY1 + 6, buf, i == activeTrap_ ? FONT_GREEN : FONT_AMBER);
            }
            return true;
        }
        case A_TRAPSEL:
            std::snprintf(buf, sizeof buf, "ПОДАЧА ИЗ ЛОВУШКИ %d", activeTrap_ + 1);
            PanelButton(s, id, buf, 0);
            return true;
        case A_STORE: {
            const double f = drive_.StoreFraction();
            std::snprintf(key, sizeof key, "%.3f", f);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            const int w = AreaW(id) - 12;
            PanelFill(s, r[0] + 6, r[1] + 6, w, 22, 30, 36, 34);
            PanelFill(s, r[0] + 6, r[1] + 6, int(w * f), 22, f < 0.9 ? 200 : 80, f < 0.9 ? 160 : 220, 70);
            std::snprintf(buf, sizeof buf, "%.1f%%  %.2e Дж", 100.0 * f, drive_.StoreEnergy());
            PanelText(s, r[0] + 8, r[1] + 32, buf, drive_.CanRaiseField(0.0) ? FONT_GREEN : FONT_RED);
            return true;
        }
        case A_IONRODS: {  // rods of ion charges
            const double pct = 100.0 * (GetPropellantMass(argon_) + GetPropellantMass(iron_)) / (prm_.argonMass + prm_.ironMass);
            std::snprintf(key, sizeof key, "%.1f", pct);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            const int h = kRodY1 - kRodY0;
            for (int i = 0; i < kRodN; ++i) {
                const int x = kRodX0 + i * (kRodW + kRodGap);
                PanelFill(s, x, kRodY0, kRodW, h, 30, 34, 32);
                const double part = (std::max)(0.0, (std::min)(1.0, (pct - i * 10.0) / 10.0));
                const int fh = int(h * part + 0.5);
                PanelFill(s, x, kRodY1 - fh, kRodW, fh, 110, 170, 230);
            }
            std::snprintf(buf, sizeof buf, "%.1f%%  %.0f т", pct, (GetPropellantMass(argon_) + GetPropellantMass(iron_)) / 1000.0);
            PanelText(s, r[0] + 8, kRodY1 + 4, buf, FONT_AMBER);
            return true;
        }
        case A_PLANT: {
            const double lv = feeding ? level : 0.0;
            std::snprintf(key, sizeof key, "%.2e %.2f", drive_.RecoveredPower(), drive_.PelletRate(lv));
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            std::snprintf(buf, sizeof buf, "ВЭУ %.1f ГВт", prm_.plantPower / 1e9);
            PanelText(s, r[0] + 8, r[1] + 4, buf, FONT_AMBER);
            std::snprintf(buf, sizeof buf, "Возврат %.1e Вт", drive_.RecoveredPower());
            PanelText(s, r[0] + 8, r[1] + 28, buf, lv > 0 ? FONT_GREEN : FONT_AMBER);
            std::snprintf(buf, sizeof buf, "Гранулы 4 x %.1f кГц", drive_.PelletRate(lv) / 1e3);
            PanelText(s, r[0] + 8, r[1] + 52, buf, lv > 0 ? FONT_WHITE : FONT_AMBER);
            std::snprintf(buf, sizeof buf, "%.1f г = %.0f кт", drive_.PelletMass() * 1e3, drive_.PelletEnergy() / 4.184e12);
            PanelText(s, r[0] + 8, r[1] + 76, buf, FONT_AMBER);
            return true;
        }
        case A_COMP: {  // compensator: how many g it cancels, what the crew feels
            const double cancel = drive_.CompensatedAccel() / G0, felt = accelG_, thrust = thrustAccel_ / G0;
            std::snprintf(key, sizeof key, "%.0f %.2f %.0f %d", cancel, felt, thrust, gLimitOn_ ? 1 : 0);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            std::snprintf(buf, sizeof buf, "%3.0f g", cancel);
            PanelBig(s, r[0] + (AreaW(id) - 5 * kBigW) / 2, r[1] + 6, buf);
            std::snprintf(buf, sizeof buf, "ОЩУЩАЕТСЯ %.2f g", felt);
            PanelText(s, r[0] + 10, r[1] + 58, buf, gLimitOn_ && felt > gLimit_ + 0.05 ? FONT_RED : FONT_GREEN);
            std::snprintf(buf, sizeof buf, "ПО ТЯГЕ   %.1f g", thrust);
            PanelText(s, r[0] + 10, r[1] + 82, buf, FONT_AMBER);
            std::snprintf(buf, sizeof buf, "ПРЕДЕЛ АППАРАТА %.0f g", prm_.compMaxG);
            PanelText(s, r[0] + 10, r[1] + 106, buf, FONT_AMBER);
            return true;
        }
        case A_GLIM:
            PanelButton(s, id, gLimitOn_ ? "ОГРАНИЧИТЕЛЬ: ВКЛ" : "ОГРАНИЧИТЕЛЬ: ВЫКЛ", gLimitOn_ ? 1 : 2);
            return true;
        case A_GSTEP:
            std::snprintf(buf, sizeof buf, "ПРЕДЕЛ %.1f g  »", gLimit_);
            PanelButton(s, id, buf, 0);
            return true;
        case A_OVERRIDE:
            PanelButton(s, id, hotStartOverride_ ? "БЛОКИРОВКИ СНЯТЫ" : "БЛОКИРОВКИ: ВКЛ",
                        hotStartOverride_ ? 2 : 1);
            return true;
        case A_ALT: {
            const bool atm = GetAtmDensity() > 0.0;
            const double alt = GetAltitude() / 1e3;
            const double need = prm_.HotStartMinAltitude(drive_.JetPower(1.0)) / 1e3;
            std::snprintf(key, sizeof key, "%d %.1f %.0f %d", atm, alt, need, hotStartOverride_);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            std::snprintf(buf, sizeof buf, "ВЫСОТА %.1f км", alt);
            PanelText(s, r[0] + 10, r[1] + 6, buf, FONT_AMBER);
            if (atm) std::snprintf(buf, sizeof buf, "полная тяга с %.0f км", need);
            else std::snprintf(buf, sizeof buf, "вне атмосферы");
            PanelText(s, r[0] + 10, r[1] + 30, buf, atm && alt < need && !hotStartOverride_ ? FONT_RED : FONT_GREEN);
            return true;
        }
        case A_READOUT: {  // the velocity equaliser dial
            const double tau = properTime_;
            std::snprintf(key, sizeof key, "%.6f %d %.0f %.1f %.1f %d", beta_, int(tau / 60), level * 1000, GetMass() / 1e5,
                          thrustAccel_ / G0, anaMain * 2 + (planGroup_ == 1));
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            int y = r[1] + 6;
            auto row = [&](int font, const char* fmt, auto... args) {
                std::snprintf(buf, sizeof buf, fmt, args...);
                PanelText(s, r[0] + 10, y, buf, font);
                y += 25;
            };
            row(FONT_WHITE, "v/c    %.6f", beta_);
            row(FONT_AMBER, "гамма  %.5f", Gamma(beta_));
            row(FONT_AMBER, "Корабельное время %dд %02dч %02dм", int(tau / 86400), int(std::fmod(tau, 86400) / 3600),
                int(std::fmod(tau, 3600) / 60));
            if (anaMain || planGroup_ == 1)
                row(anaMain ? FONT_GREEN : FONT_AMBER, "%s: тяга %.1f%%", anaMain ? "Анамезон" : "Планетарные", level * 100);
            else
                row(FONT_AMBER, "Рукоять тяги: нет");
            row(FONT_AMBER, "Масса %.2f кт", GetMass() / 1e6);
            row(FONT_AMBER, "Тяговое ускорение %.1f g", thrustAccel_ / G0);
            return true;
        }
        case A_AIRLOCK: case L_AIRLOCK: {
            const bool open = crew_.AirlockOpen();
            PanelButton(s, id, open ? "ШЛЮЗ ОТКРЫТ" : "ШЛЮЗ ЗАКРЫТ", open ? 1 : 0);
            return true;
        }
        case A_EVA: case L_EVA: {   // left half: the airlock crew lift (Shift+A), right half: EVA (E)
            const double prog = (lift_.Door() + lift_.Out() + lift_.Mast() + lift_.Down()) / 4.0;
            if (lift_.AtGround()) std::snprintf(buf, sizeof buf, "ЛИФТ ВВЕРХ | ВЫХОД");
            else if (lift_.Stowed() && !lift_.Lowering()) std::snprintf(buf, sizeof buf, "ЛИФТ ВНИЗ | ВЫХОД");
            else std::snprintf(buf, sizeof buf, "ЛИФТ %3.0f%% | ВЫХОД", prog * 100.0);
            PanelButton(s, id, buf, lift_.AtGround() ? 1 : 0);
            return true;
        }
        case A_CREWSEL: case L_CREWSEL: {
            const int total = crew_.Total();
            if (total > 0) std::snprintf(buf, sizeof buf, "«  %s (%s)  »", crew_.Name(selectedCrew_), crew_.Role(selectedCrew_));
            else std::snprintf(buf, sizeof buf, "на борту никого");
            PanelButton(s, id, buf, 0);
            return true;
        }
        case A_CREWINFO: {
            const int total = crew_.Total();
            const int age = total ? crew_.Age(selectedCrew_) : 0;
            const int pulse = total ? crew_.Pulse(selectedCrew_) : 0;
            std::snprintf(key, sizeof key, "%d %d %d %d", total, selectedCrew_, age, pulse);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            std::snprintf(buf, sizeof buf, "На борту %d из %d", total, sp::kCrewSeats);
            PanelText(s, r[0] + 10, r[1] + 6, buf, FONT_AMBER);
            if (total) {
                std::snprintf(buf, sizeof buf, "Возраст %d, пульс %d", age, pulse);
                PanelText(s, r[0] + 10, r[1] + 30, buf, pulse > 0 ? FONT_GREEN : FONT_RED);
            }
            return true;
        }
        case A_MSG: case M_MSG: case L_MSG: {
            const char* text = messageTimer_ > 0.0 ? messageRu_ : "";
            if (!PanelChanged(id, text)) return false;
            PanelClear(s, id);
            PanelText(s, r[0] + 8, r[1] + (AreaH(id) - kCellH) / 2, text, FONT_WHITE, (AreaW(id) - 16) / kCellW);
            return true;
        }
    }
    if (kAreaPanel[id] == PANEL_MAIN) return RedrawMain(id, s);
    if (kAreaPanel[id] == PANEL_LOWER) return RedrawLower(id, s);
    return false;
}

double Tantra::LocalG() const {
    OBJHANDLE ref = GetGravityRef();
    if (!ref) return G0;  // not yet known (scenario loading)
    VECTOR3 pos;
    GetRelativePos(ref, pos);
    const double r = length(pos);
    return r > 0.0 ? GGRAV * oapiGetMass(ref) / (r * r) : G0;
}

// --- MAIN panel: MFD button labels and the engine essentials -----------------------------

bool Tantra::RedrawMain(int id, SURFHANDLE s) {
    char key[256], buf[128];
    const int* r = AreaRect(id);
    for (const MfdUnit& u : kMfdAreas) {
        if (id != u.left && id != u.right) continue;
        const int side = id == u.right ? 1 : 0;
        const char* lbl[6];
        int n = 0;
        for (int i = 0; i < 6; ++i) {
            lbl[i] = oapiMFDButtonLabel(u.mfd, i + side * 6);
            n += std::snprintf(key + n, sizeof key - n, "%s|", lbl[i] ? lbl[i] : "");
        }
        if (!PanelChanged(id, key)) return false;
        for (int i = 0; i < 6; ++i) {
            const int y = r[1] + kMfdBtnY0 + i * kMfdBtnPitch;
            PanelFill(s, r[0], y, AreaW(id), kMfdBtnH, kDisplayBg[0], kDisplayBg[1], kDisplayBg[2]);
            if (lbl[i]) {
                const int rr[4] = {r[0], 0, r[2], 0};
                PanelTextCentered(s, rr, y + (kMfdBtnH - kCellH) / 2, lbl[i], FONT_AMBER);
            }
        }
        return true;
    }
    const double level = GetThrusterGroupLevel(THGROUP_MAIN);
    const bool anaMain = AnaIsMain();
    switch (id) {
        case M_START: {
            const IgnStage tgt = ignition_.Target();
            std::snprintf(buf, sizeof buf, "ПУСК » %s", StageName(tgt, true));
            PanelButton(s, id, buf, tgt == IgnStage::Feed ? 1 : 0);
            return true;
        }
        case M_THROTTLE: {
            const double hov = podHover_ ? GetThrusterGroupLevel(THGROUP_HOVER) : 0.0;
            std::snprintf(key, sizeof key, "%.0f %.0f %d", level * 1000, hov * 1000, anaMain);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            const int w = AreaW(id) - 16;
            std::snprintf(buf, sizeof buf, "%s %.1f%%", anaMain ? "АНАМЕЗОН" : "ПЛАНЕТАРНЫЕ", level * 100);
            PanelText(s, r[0] + 8, r[1] + 2, buf, anaMain ? FONT_GREEN : FONT_AMBER);
            PanelFill(s, r[0] + 8, r[1] + 22, w, 5, 30, 36, 34);
            PanelFill(s, r[0] + 8, r[1] + 22, int(w * level), 5, anaMain ? 190 : 230, anaMain ? 120 : 150, anaMain ? 255 : 60);
            std::snprintf(buf, sizeof buf, "ВЫДВ. БЛОКИ %s %.0f%%", podHover_ ? "ВИСЕНИЕ" : "-", hov * 100);
            PanelText(s, r[0] + 8, r[1] + 30, buf, podHover_ ? FONT_GREEN : FONT_AMBER);
            PanelFill(s, r[0] + 8, r[1] + 51, int(w * hov), 4, 110, 170, 230);
            return true;
        }
        case M_G: {
            std::snprintf(key, sizeof key, "%.2f %d %.1f", accelG_, gLimitOn_, gLimit_);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            std::snprintf(buf, sizeof buf, "%5.2fg", accelG_);
            PanelBig(s, r[0] + 10, r[1] + 16, buf);
            std::snprintf(buf, sizeof buf, "ОЩУЩАЕТСЯ");
            PanelText(s, r[0] + 176, r[1] + 10, buf, FONT_AMBER);
            std::snprintf(buf, sizeof buf, "предел %s %.1f", gLimitOn_ ? "ВКЛ" : "ВЫКЛ", gLimit_);
            PanelText(s, r[0] + 176, r[1] + 38, buf, gLimitOn_ && accelG_ > gLimit_ + 0.05 ? FONT_RED : FONT_GREEN);
            return true;
        }
        case M_FLIGHT: {
            std::snprintf(key, sizeof key, "%.6f %.1f %.0f %.2f", beta_, GetAltitude() / 100, GetAirspeed(), GetMass() / 1e6);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            std::snprintf(buf, sizeof buf, "v/c %.6f", beta_);
            PanelText(s, r[0] + 8, r[1] + 4, buf, FONT_WHITE);
            std::snprintf(buf, sizeof buf, "высота %.1f км", GetAltitude() / 1e3);
            PanelText(s, r[0] + 8, r[1] + 28, buf, FONT_AMBER);
            std::snprintf(buf, sizeof buf, "масса %.2f кт", GetMass() / 1e6);
            PanelText(s, r[0] + 8, r[1] + 52, buf, FONT_AMBER);
            return true;
        }
        case M_GEAR: {
            static const char* kPh[] = {"ЛЁЖА", "ПОДГОТОВКА", "ПОДЪЁМ", "ПОВОРОТ", "ЛАПЫ", "ОПУСКАНИЕ", "СБОР", "НА КОРМЕ"};
            if (carriage_.Gear() <= 0.0) std::snprintf(buf, sizeof buf, "ШАССИ УБРАНО  » ниже");
            else std::snprintf(buf, sizeof buf, "ШАССИ: %s  » ниже", kPh[carriage_.Phase()]);
            PanelButton(s, id, buf, carriage_.Busy() ? 2 : (carriage_.Gear() > 0.0 ? 1 : 0));
            return true;
        }
    }
    return false;
}

// --- LOWER panel: undercarriage and hull -----------------------------------------------

bool Tantra::RedrawLower(int id, SURFHANDLE s) {
    char key[256], buf[128];
    const int* r = AreaRect(id);
    const tantra::CarriagePose& cp = carriage_.Pose();
    static const char* kPh[] = {"лежит на лопастях и передней опоре", "подъём на лопастях", "цапфы под ЦМ, передняя опора в карман",
                                "поворот вокруг цапф", "кормовые ноги выходят", "нагрузка на корму",
                                "лопасти убираются", "стоит на корме"};
    switch (id) {
        case L_MIMIC: {
            int pose = MIMIC_LEVEL;
            const double P = carriage_.Progress();
            if (carriage_.Gear() < 1.0 && P <= 0.0) pose = carriage_.Gear() <= 0.0 ? MIMIC_FLIGHT : MIMIC_LEVEL;
            else if (P <= 0.0) pose = MIMIC_LEVEL;
            else if (P < 2.0) pose = P < 1.0 ? MIMIC_LEVEL : MIMIC_LIFTED;
            else if (P < 3.0) pose = cp.theta < 0.26 * PI ? MIMIC_TURN30 : (cp.theta < 0.42 * PI ? MIMIC_TURN60 : MIMIC_VERTCOLS);
            else if (P < 4.0) pose = MIMIC_VERTCOLS;
            else if (P < 5.0) pose = carriage_.Port() ? MIMIC_VERTCOLS : MIMIC_LEGSOUT;
            else pose = carriage_.Port() ? MIMIC_PORT : MIMIC_STAND;
            if (P >= 6.0 && carriage_.Gear() <= 0.0) pose = MIMIC_FLIGHT;
            std::snprintf(key, sizeof key, "%d", pose);
            if (!PanelChanged(id, key)) return false;
            oapiBlt(s, PanelSrc(s), r[0], r[1], kMimicX + (pose % 3) * kMimicW, kMimicY + (pose / 3) * kMimicW, kMimicW, kMimicW);
            return true;
        }
        case L_POSE: {
            std::snprintf(key, sizeof key, "%.0f %.1f %.1f %.2f", cp.theta * DEG, cp.trunnionH, cp.mastLen, carriage_.Progress());
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            std::snprintf(buf, sizeof buf, "угол %3.0f°", cp.theta * DEG);
            PanelText(s, r[0] + 6, r[1] + 6, buf, FONT_WHITE);
            std::snprintf(buf, sizeof buf, "цапфы %.1f м", cp.trunnionH);
            PanelText(s, r[0] + 6, r[1] + 32, buf, FONT_AMBER);
            std::snprintf(buf, sizeof buf, "нога %.1f м", cp.mastLen);
            PanelText(s, r[0] + 6, r[1] + 58, buf, FONT_AMBER);
            std::snprintf(buf, sizeof buf, "фаза %.2f / 6", carriage_.Progress());
            PanelText(s, r[0] + 6, r[1] + 84, buf, FONT_AMBER);
            std::snprintf(buf, sizeof buf, "%s", carriage_.Port() ? "ПОРТ" : "грунт");
            PanelText(s, r[0] + 6, r[1] + 110, buf, carriage_.Port() ? FONT_GREEN : FONT_AMBER);
            return true;
        }
        case L_PHASE: {
            const char* t = carriage_.Gear() <= 0.0 ? "шасси убрано" : kPh[carriage_.Phase()];
            if (!PanelChanged(id, t)) return false;
            PanelClear(s, id);
            PanelTextCentered(s, r, r[1] + (AreaH(id) - kCellH) / 2, t, carriage_.Busy() ? FONT_WHITE : FONT_GREEN);
            return true;
        }
        case L_PROG: {
            std::snprintf(key, sizeof key, "%.3f", carriage_.Progress());
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            const int w = AreaW(id) - 8;
            PanelFill(s, r[0] + 4, r[1] + 6, int(w * carriage_.Progress() / 6.0), AreaH(id) - 12, 80, 170, 110);
            for (int k = 1; k < 6; ++k) PanelFill(s, r[0] + 4 + w * k / 6, r[1] + 3, 1, AreaH(id) - 6, 120, 95, 50);
            return true;
        }
        case L_GEAR: {
            const double g = carriage_.Gear();
            const char* t = g >= 1.0 ? "ШАССИ ВЫПУЩЕНО" : (g <= 0.0 ? "ШАССИ УБРАНО" : (carriage_.GearDown() ? "ВЫПУСК..." : "УБОРКА..."));
            PanelButton(s, id, t, g >= 1.0 ? 1 : (g <= 0.0 ? 0 : 2));
            return true;
        }
        case L_SET_LEVEL:
            PanelButton(s, id, "ЛЁЖА", carriage_.Set() == tantra::Carriage::FlightSet::Level ? 1 : 0);
            return true;
        case L_SET_STAND:
            PanelButton(s, id, "НА КОРМУ", carriage_.Set() == tantra::Carriage::FlightSet::Standing ? 1 : 0);
            return true;
        case L_ERECT: {
            const bool up = carriage_.Target() >= 3.0;
            const char* t = carriage_.Busy() ? (up ? "ИДЁТ ПОДЪЁМ..." : "ИДЁТ УКЛАДКА...")
                                             : (carriage_.Progress() >= 6.0 ? "УЛОЖИТЬ В ГОРИЗОНТ" : "ПОСТАВИТЬ НА КОРМУ");
            PanelButton(s, id, t, carriage_.Busy() ? 2 : 0);
            return true;
        }
        case L_PORT:
            PanelButton(s, id, carriage_.Port() ? "ПОРТ: МАГНИТНЫЙ СТОЛ" : "ПОРТ: НЕТ (ГРУНТ)", carriage_.Port() ? 1 : 0);
            return true;
        case L_LOADS: {
            const double W = GetMass() * LocalG();
            const tantra::Carriage::Loads ld = carriage_.Statics(W, 0.3);
            const double sj = (std::max)(solesCar_.Jam(), solesStern_.Jam());
            const double sa = (std::max)(solesCar_.Anchors(), solesStern_.Anchors());
            std::snprintf(key, sizeof key, "%.0f %.0f %.0f %.0f %.0f%.0f%.0f%.0f%.0f%.0f %d%.1f%.1f %.1f %.1f", ld.columnEach / 1e5,
                          ld.legs / 1e5, ld.driveMoment / 1e5, W / 1e6, 100 * legR_[0], 100 * legR_[1], 100 * legR_[2], 100 * legR_[3],
                          100 * legR_[4], 100 * legR_[5], hipCatcher_, sj, sa, regen_.Returned() / 1e8, regen_.Spent() / 1e8);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            const int dy = 21, y0 = r[1] + 4;
            std::snprintf(buf, sizeof buf, "вес        %8.1f МН", W / 1e6);
            PanelText(s, r[0] + 8, y0, buf, FONT_WHITE);
            std::snprintf(buf, sizeof buf, "лопасть x2 %7.1f МН  пер. опора %5.1f", ld.columnEach / 1e6, ld.kangaroo / 1e6);
            PanelText(s, r[0] + 8, y0 + dy, buf, FONT_AMBER);
            std::snprintf(buf, sizeof buf, "%s %8.1f МН", carriage_.Port() ? "стол      " : "корм.ноги ", ld.legs / 1e6);
            PanelText(s, r[0] + 8, y0 + 2 * dy, buf, FONT_AMBER);
            std::snprintf(buf, sizeof buf, "привод цапф %6.0f МН·м", ld.driveMoment / 1e6);
            PanelText(s, r[0] + 8, y0 + 3 * dy, buf, FONT_AMBER);
            {   // column buckling: F L^2 against pi^2 E I / safety of the band mast (Spec.h)
                const double flLim = PI * PI * tantra::spec::kCntE * tantra::spec::kBladeI / tantra::spec::kSafety;
                std::snprintf(buf, sizeof buf, "изгиб лопастей %5.0f %%", 100.0 * ld.columnFL2 / flLim);
                PanelText(s, r[0] + 8, y0 + 4 * dy, buf, ld.columnFL2 > flLim ? FONT_RED : FONT_GREEN);
            }
            {   // fibre sensors: every leg against its rating
                double worst = 0.0;
                for (double v : legR_) worst = (std::max)(worst, v);
                std::snprintf(buf, sizeof buf, "опоры %2.0f %2.0f|%2.0f %2.0f %2.0f %2.0f%%", 100 * legR_[0], 100 * legR_[1],
                              100 * legR_[2], 100 * legR_[3], 100 * legR_[4], 100 * legR_[5]);
                PanelText(s, r[0] + 8, y0 + 5 * dy, buf, worst >= 1.0 ? FONT_RED : worst >= tantra::legs::kAlarm ? FONT_AMBER : FONT_GREEN);
            }
            std::snprintf(buf, sizeof buf, "цапфы %s грунт %s", hipCatcher_ ? "страх." : "магн. ",
                          sa > 0.99 ? "спечён" : sj > 0.99 ? "осел" : "осед.");
            PanelText(s, r[0] + 8, y0 + 6 * dy, buf, hipCatcher_ ? FONT_AMBER : FONT_GREEN);
            return true;
        }
        case L_CRESTS:
            PanelButton(s, id, wingMode_ == 2 ? "КРЫЛЬЯ СЛОЖЕНЫ" : (wingMode_ == 1 ? "КРЫЛЬЯ 30°" : "КРЫЛЬЯ 90°"), wingMode_ ? 1 : 0);
            return true;
        case L_IRIS: {
            std::snprintf(key, sizeof key, "%.2f %.2f %.2f %.2f", irisAna_, irisMarch_, marchOut_, tuck_);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            std::snprintf(buf, sizeof buf, "чаши анамезона: %s", irisAna_ > 0.99 ? "ОТКРЫТЫ" : irisAna_ < 0.01 ? "закрыты" : "...");
            PanelText(s, r[0] + 8, r[1] + 6, buf, irisAna_ > 0.99 ? FONT_GREEN : FONT_AMBER);
            std::snprintf(buf, sizeof buf, "маршевая: %s", marchOut_ > 0.99 ? "ВЫДВИНУТА" : marchOut_ > 0.0 || irisMarch_ > 0.0 ? "..." : "в колодце");
            PanelText(s, r[0] + 8, r[1] + 32, buf, marchOut_ > 0.99 ? FONT_GREEN : FONT_AMBER);
            std::snprintf(buf, sizeof buf, "выдвижные блоки: %s", (std::max)(tuck_, cp.tuck) > 0.5 ? "утоплены" : "выдвинуты");
            PanelText(s, r[0] + 8, r[1] + 58, buf, FONT_AMBER);
            return true;
        }
        case L_HANGAR:
            PanelButton(s, id, hangarT_ > 0.5 ? "АНГАР ОТКРЫТ" : "АНГАР ЗАКРЫТ", hangarT_ > 0.5 ? 1 : 0);
            return true;
        case L_ROVERS:
            PanelButton(s, id, roversT_ > 0.5 ? "РОВЕРЫ ВНИЗУ" : "РОВЕРЫ НАВЕРХУ", roversT_ > 0.5 ? 1 : 0);
            return true;
        case L_PT_LIFT:
            PanelButton(s, id, carriage_.AtLoadHeight() ? "ВЫСОТА ЗАГРУЗКИ (ЛЁЖА)" : "ЗАГРУЗКА: ТОЛЬКО ЛЁЖА", carriage_.AtLoadHeight() ? 1 : 0);
            return true;
        case L_PT_LOAD:
            PanelButton(s, id, "ПРИНЯТЬ КАССЕТУ", portStep_ != PortStep::Idle && portLoading_ ? 2 : 0);
            return true;
        case L_PT_DROP:
            PanelButton(s, id, "ВЫГРУЗИТЬ ЛОВУШКУ", portStep_ != PortStep::Idle && !portLoading_ ? 2 : 0);
            return true;
        case L_PT_STOP:
            PanelButton(s, id, "СТОП / ВИЛКИ ДОМОЙ", 0);
            return true;
        case L_PT_ST: {
            static const char* kStep[] = {"готов", "створки", "вилки вниз к цапфам", "подъём в гнездо", "захват в гнезде",
                                          "опускание", "вилки домой", "створки закрываются", "удержание"};
            std::snprintf(key, sizeof key, "%d %d%d%d%d %.2f", int(portStep_), trapPresent_[0], trapPresent_[1],
                          trapPresent_[2], trapPresent_[3], bayDoors_);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            std::snprintf(buf, sizeof buf, "гнёзда  В %s %s   Н %s %s", trapPresent_[3] ? "•" : "–", trapPresent_[2] ? "•" : "–",
                          trapPresent_[1] ? "•" : "–", trapPresent_[0] ? "•" : "–");
            PanelText(s, r[0] + 8, r[1] + 4, buf, FONT_AMBER);
            std::snprintf(buf, sizeof buf, "порт: %s  створки %.0f%%", kStep[int(portStep_)], bayDoors_ * 100);
            PanelText(s, r[0] + 8, r[1] + 26, buf, portStep_ == PortStep::Idle ? FONT_GREEN : FONT_WHITE);
            return true;
        }
        case L_DECK: {
            std::snprintf(key, sizeof key, "%.2f %.2f %.2f", hangar_, rovers_, airlockUp_);
            if (!PanelChanged(id, key)) return false;
            PanelClear(s, id);
            std::snprintf(buf, sizeof buf, "створки %.0f%%   платформа %.0f%%", hangar_ * 100, rovers_ * 100);
            PanelText(s, r[0] + 8, r[1] + 8, buf, FONT_AMBER);
            std::snprintf(buf, sizeof buf, "платформа ангара: %s", rovers_ > 0.99 ? "у грунта" : rovers_ < 0.01 ? "поднята" : "...");
            PanelText(s, r[0] + 8, r[1] + 34, buf, FONT_AMBER);
            return true;
        }
    }
    return false;
}

DLLCLBK void ExitModule(HINSTANCE) { Tantra::ReleasePanelTexture(); }
