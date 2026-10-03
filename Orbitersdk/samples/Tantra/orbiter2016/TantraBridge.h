// TantraBridge: the commander's own touch screens and the holo panel of the bridge (the user's rule: every screen is touch).
//   - four screens on the commander's side panels, each with pages (tabs along the top): the pages are sections of the
//     ship's own 2D panels (panel.dds, redrawn live by the ship); a touch on a page is a click on that panel area;
//   - the holo panel on the shelf in front of him, four zones: 1 engines, 2 anamezon, 3 flight, 4 automation (the Orbiter
//     autopilots: allowed from here only).
#pragma once
#define STRICT
#include "orbitersdk.h"
#include "TantraGlyphs.h"

struct HoloData {
    bool anamezon = false, anaFeed = false;   // the engine set; anamezon feeding
    double thrust = 0, fuel = 0;              // main thrust level, propellant fraction
    double field = 0, beam = 0;               // the ignition: field and beam levels 0..1
    double alt = 0, vs = 0, speed = 0, mach = 0, g = 0, aoa = 0, hdg = 0, pitch = 0, bank = 0;   // m, m/s, m/s, -, g, rad, rad
    int navOn = 0;                            // bits (1 << NAVMODE_*) of the autopilots running
};

struct BridgeHooks {
    void* ctx = nullptr;
    SURFHANDLE (*PanelTex)(void* ctx) = nullptr;                      // the 2D panels' texture
    void (*Click)(void* ctx, int area, int mx, int my) = nullptr;     // a click in a panel area
    void (*Holo)(void* ctx, HoloData* out) = nullptr;                 // the values for the holo panel
    void (*Nav)(void* ctx, int mode) = nullptr;                       // toggle an Orbiter autopilot (NAVMODE_*)
};

class TantraBridge {
public:
    void Init(VESSEL* v, UINT vcMesh, const BridgeHooks& hk) { v_ = v; vcMesh_ = vcMesh; hk_ = hk; }
    void OnVisual(VISHANDLE vis);
    void Step(double dt);                       // redraw the screens (~4 Hz) and the holo panel
    bool Touch(int screen, double u, double v); // screen 0..3: own screens, 4: the holo panel; u right, v down (0..1)
    static bool ClickPanel(const BridgeHooks& hk, double tx, double ty);   // a click at a point of panel.dds (texture px)
    void Shutdown();

private:
    void DrawScreen(int k);
    void DrawHolo();
    VESSEL* v_ = nullptr;
    UINT vcMesh_ = 0;
    BridgeHooks hk_{};
    SURFHANDLE scr_[4] = {}, holo_ = nullptr;
    int page_[4] = {0, 0, 0, 0};
    double t_ = 0.0;
    oapi::Font *fTab_ = nullptr, *fBig_ = nullptr, *fSmall_ = nullptr;
    TantraGlyphs glyphs_;                     // screen text from the helmet display's atlas (Cyrillic)
};
