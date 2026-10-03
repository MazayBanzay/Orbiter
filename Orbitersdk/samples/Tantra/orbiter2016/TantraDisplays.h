// TantraDisplays: the commander's touch screens on the bridge (the user's design, 2026-10-03, after the Arrow Freighter's cockpit;
// Tantra_Design/DESIGN_LOCAL.md "Место командира: схема как у Arrow Freighter"). Every key is a real function of the ship - the same
// as the keyboard and the 2D panels - pressed with the mouse right on the picture (the user's rule: everything is touch).
//   0, 1  the big upright displays at the elbows: modes by the keys along the bottom - MFD (two Orbiter MFDs), рабочее тело,
//         энергоустановка, экипаж, лифты, положение, корпус (the display rises / sinks by a button on its pedestal).
//         The MFDs: the 4 ExternMFDs of TantraInterior (2 per display). The system pages are live sections of the ship's 2D panels.
//   2     the flight terminal on the shelf: HUD and RCS keys left, the autopilots right (switched on from here only); in the middle
//         SPACE (thrust - set by a touch on its bar, fuel, horizon, readouts) or ATMOSPHERE (an aircraft screen: attitude, speed and
//         altitude tapes, heading tape, AoA, slip, g, pods, flight path): by itself below 100 km in air, or by the tabs.
//   3     the engine console on the instrument wall: the anamezon system (phases field - beam - feed, the four cups and the retro,
//         the traps) or, with the system off, the planetary engines (marching cup, pods); the system keys along the bottom.
// The clicks come from TantraInterior (the person's ray on the screen -> u, v); the surfaces replace the textures of the screens
// (InteriorLayout.h kTouch[k].slot). Text: the glyph atlas (TantraGlyphs), as D3D9Client draws no Cyrillic with system fonts.
#pragma once
#define STRICT
#include "orbitersdk.h"
#include "TantraGlyphs.h"

class Tantra;

class TantraDisplays {
public:
    enum Screen { kLeft = 0, kRight = 1, kTerminal = 2, kEngines = 3, kScreens = 4 };
    enum Mode { M_MFD, M_FLUID, M_PLANT, M_CREW, M_LIFTS, M_POSE, M_HULL, M_OFF, M_COUNT };

    void Init(Tantra* ship, UINT vcMeshIdx) { t_ = ship; vcMesh_ = vcMeshIdx; }
    void OnVisual(VISHANDLE vis);              // create the surfaces and put them into the texture slots
    void Step(double dt);                      // redraw (about 5 Hz; at once after a touch)
    bool Touch(int screen, double u, double v);// u right, v down, 0..1 over the screen's rectangle
    void Shutdown();

private:
    void DrawSide(int k);
    void DrawTerminal();
    void DrawEngines();
    bool TouchSide(int k, double x, double y);
    bool TouchTerminal(double x, double y);
    bool TouchEngines(double x, double y);
    void SetMainLevel(double f);              // thrust set-points from a touch on a scale
    void SetPodLevel(double f);

    Tantra* t_ = nullptr;
    UINT vcMesh_ = 0;
    SURFHANDLE s_[kScreens] = {};
    int mode_[2] = {M_MFD, M_MFD};
    double t_redraw_ = 0.0;
    int termMode_ = -1;                        // flight terminal: -1 automatic, 0 space, 1 atmosphere (a tab)
    bool lastAutoAtmo_ = false;
    TantraGlyphs glyphs_;
};
