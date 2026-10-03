// TantraDisplays: the commander's touch screens on the bridge console «Полумесяц φ» (the user's choice, 2026-10-03;
// Tantra_Design/bridge_fork_mockup.html, DESIGN_LOCAL.md). Every key is a real function of the ship - the same as the keyboard
// and the 2D panels - pressed with the mouse right on the picture (the user's rule: everything is touch).
//   0, 1  the curved monitors at his sides (they fold back: развёрнут / свёрнут / сложен): modes by the keys along the bottom -
//         MFD (two Orbiter MFDs), рабочее тело, энергоустановка, экипаж, лифты, механизация, корпус.
//   2     the concave centre screen: ГЛАВНЫЙ ЭКРАН (HUD and RCS modes) | ПОЛЁТ (SPACE or ATMOSPHERE, by itself below 100 km in air,
//         or by the tabs) | ДВИГАТЕЛИ (the thrust - set by a touch on its bar, the fuel, the g limit).
//   3     the right wing's riser: ЭНЕРГИЯ (the plant, the compensator) | the engine console (anamezon phases, cups, traps or the
//         planetary engines).
//   4     the left wing's riser: МЕХАНИЗАЦИЯ (pose, carriage and gear, hull) | ПОЛОЖЕНИЕ (attitude, rates, RCS).
//   5     the attitude keys on the shelf in front of him: the autopilots (switched on from here only), РУЧН. switches them off.
//   6     the left wing's shelf: the mechanisation keys and the check display (the pose mimic).
//   7     the right wing's shelf: the monitors' fold keys and the calculator.
// And the HUD right on the big screen's picture (TantraScreen draws it over the front camera's image every step): the horizon, the
// pitch bars every 10 deg, the velocity vector, the waterline, the heading tape, speed and altitude; its mode by the HUD keys of the
// centre screen (Orbiter draws no VC HUD here: the focus is the person).
// The clicks come from TantraInterior (the person's ray on a flat piece of a screen -> u, v of the screen); the surfaces replace
// the textures of the screens (InteriorLayout.h kTouch[k].slot). Text: the glyph atlas (TantraGlyphs).
#pragma once
#define STRICT
#include "orbitersdk.h"
#include "TantraGlyphs.h"
#include <vector>

class Tantra;

class TantraDisplays {
public:
    enum Screen { kLeft = 0, kRight = 1, kCentre = 2, kRiserR = 3, kRiserL = 4, kKeys = 5, kWingL = 6, kWingR = 7, kScreens = 8 };
    enum Mode { M_MFD, M_FLUID, M_PLANT, M_CREW, M_LIFTS, M_POSE, M_HULL, M_SET, M_OFF, M_COUNT };

    void Init(Tantra* ship, UINT vcMeshIdx) { t_ = ship; vcMesh_ = vcMeshIdx; }
    void OnVisual(VISHANDLE vis);              // create the surfaces and put them into the texture slots
    void Step(double dt);                      // redraw (about 5 Hz; at once after a touch)
    bool Touch(int screen, double u, double v);// u right, v down, 0..1 over the screen
    void Shutdown();
    void DrawHud(SURFHANDLE s, int w, int h, const VECTOR3& camDir, const VECTOR3& camUp, double vfovDeg);   // over the front picture
    void AckAlerts();                          // the alerts acknowledged: they stop blinking

private:
    void DrawSide(int k);
    void DrawCentre();
    void DrawEngines();                        // into eng_ (shown on the right riser)
    void DrawRiser(int k);
    void DrawKeys();
    void DrawWing(int k);
    bool TouchSide(int k, double x, double y);
    bool TouchCentre(double x, double y);
    bool TouchEngines(double x, double y);
    bool TouchRiser(int k, double x, double y);
    bool TouchKeys(double x, double y);
    bool TouchWing(int k, double x, double y);
    void CalcKey(int i);
    void Alerts();                             // the ship's alerts and events (every step)
    void Say(const char* text, int level);     // a one-off message under the ribbon (6 s): 0 plain, 1 ВНИМАНИЕ, 2 ОПАСНОСТЬ
    int AlertLevel() const;
    bool AlertUnacked() const;
    void SetMainLevel(double f);              // thrust set-points from a touch on a scale
    void SetPodLevel(double f);

    Tantra* t_ = nullptr;
    UINT vcMesh_ = 0;
    SURFHANDLE s_[kScreens] = {};
    SURFHANDLE eng_ = nullptr;                 // the engine console, drawn off screen and shown scaled on the right riser
    int mode_[2] = {M_MFD, M_MFD};
    double t_redraw_ = 0.0;
    int hudMode_ = HUD_SURFACE;                // the HUD on the big screen: the ship's own mode (HUD_NONE = off)
    struct Alert { char key[8]; int lamp, level; char text[96], tod[12]; bool ack, active, seen; };   // level 1 ВНИМАНИЕ, 2 ОПАСНОСТЬ
    std::vector<Alert> alerts_;
    char say_[96] = ""; int sayLevel_ = 0; double sayT_ = -1e9;
    bool wasGround_ = true, wasHigh_ = false, alertInit_ = false; int lastNav_ = -1, lastStage_ = -1;
    int palette_ = 0;                          // the HUD's palette: 0 «оранж + циан», 1 «циан + оранж»
    double hudGain_ = 1.0, mfdGain_ = 1.6, mfdGamma_ = 0.7;   // the settings page of the monitors
    bool autoLum_ = true;                      // the brightness follows the light at the ship (day / dusk / night)
    double DayLight() const;
    double MfdGain() const, MfdGamma() const, HudGain() const;   // the values in use (auto or by hand)
    int termMode_ = -1;                        // ПОЛЁТ: -1 automatic, 0 space, 1 atmosphere (a tab)
    bool lastAutoAtmo_ = false;
    char calc_[24] = "0";                      // the calculator: the display, the stored operand and operation
    double calcAcc_ = 0.0;
    int calcOp_ = 0;
    bool calcNew_ = true;
    TantraGlyphs glyphs_;
};
