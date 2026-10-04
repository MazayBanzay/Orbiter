// TantraInterior: the Tantra's interior as given to OrbiterCrew (OcInterior, OrbiterCrewApi.h).
// A person walks inside with his or her own body (OrbiterCrew moves it); the ship only answers where the floors and the walls
// are (the collision boxes of tools/gen_mesh.py in InteriorLayout.h) and what can be used (the bridge seats).
// Sitting down hands the person to the ship's virtual cockpit (the body leaves, the focus goes to the ship);
// standing up gives the body back behind the seat.
#pragma once
#define STRICT
#include "orbitersdk.h"
#include "../../../OrbiterCrew/include/OrbiterCrewApi.h"
#include "ShipMfd.h"
#include "TantraGlyphs.h"
#include <vector>
#include <cmath>

class TantraCrew;

// The lift panel in the lift zone (three buttons) and the lift cabin that carries people: the ship's side of it is in Tantra.cpp.
struct LiftPanelHooks {
    void* ctx = nullptr;
    // which: 0..2 the lift zone's panel (1 suit check, 2 pressure, 3 the lift / call the cabin); 3..5 the cabin's panel (DOWN, UP, OUT)
    void (*Press)(void* ctx, int which, int personId) = nullptr;
    void (*Label)(void* ctx, int which, char* out, int n) = nullptr;              // the hint of the button (cp1251)
    void (*Cabin)(void* ctx, double* out, double* down, int* atGround) = nullptr;   // the cabin: arm travel 0..1, descent 0..1, standing on the ground
    int (*State)(void* ctx, int which) = nullptr;   // a button's light: 0 dark, 1 lit (done / ready), 2 blinking (running), 3 fast (do this first)
    void (*Status)(void* ctx, char* out, int n) = nullptr;   // the status screen: one line each, a leading '!' red, '+' green, '*' amber
    void (*CabStatus)(void* ctx, char* out, int n) = nullptr;   // the cabin's screen, the same way
    double (*DoorB)(void* ctx) = nullptr;                    // door B between the lift zone and the cell: 0 open .. 1 shut
    double (*CabDoor)(void* ctx) = nullptr;                  // the cabin's own doors: 0 open .. 1 shut
};

// The bridge console shows the ship's own 2D panels (panel.dds), live: the ship redraws them and handles their clicks.
struct ConsoleHooks {
    void* ctx = nullptr;
    SURFHANDLE (*Tex)(void* ctx) = nullptr;                          // the panels' texture (shared with the 2D panels)
    void (*Redraw)(void* ctx, int area) = nullptr;                   // redraw one panel area into it
    void (*Click)(void* ctx, int area, int mx, int my) = nullptr;    // a click in the area (mx, my: in the area)
};

class TantraInterior {
public:
    void SetConsole(const ConsoleHooks& hk) { console_ = hk; BuildPanelButtons(); }
    // The commander's touch screens (kTouch: 0 / 1 the curved monitors, 2 the concave centre, 3 / 4 the risers, 5 the attitude keys,
    // 6 / 7 the wing shelves): drawn and handled by
    // the ship's display module; here only the touch (u right, v down, 0..1) and the redraw step are passed on.
    struct TouchHooks { void* ctx = nullptr; void (*Step)(void* ctx, double dt) = nullptr; bool (*Touch)(void* ctx, int screen, double u, double v) = nullptr; };
    void SetTouch(const TouchHooks& hk) { touch_ = hk; }
    void SideFold(int k, int state);           // a curved monitor: 0 развёрнут, 1 свёрнут (leaning back, still seen), 2 сложен
    int SideFoldState(int k) const;
    bool SideDisplayUp(int k) const { return k >= 0 && k < 2 && sidePos_[k] < 0.02; }
    SURFHANDLE MfdDisplay(int i) const { return mfds_.Display(mfdPlaces_ + i); }   // the 4 MFDs of the curved monitors (0..3), after the mesh's
    const char* MfdLabel(int i, int b) const { return mfds_.Label(mfdPlaces_ + i, b); }
    void MfdPress(int i, int b) { mfds_.Press(mfdPlaces_ + i, b); }
    int MfdRes() const { return mfds_.Res(); }                      // the MFDs' resolution (px), 1024 / 2048 (the settings page)
    void SetMfdRes(int px) { mfds_.SetRes(px); }
    // meshDZ: the mesh frame -> ship frame offset (z) of the interior, read on every call (the CG moves)
    // crew: the items of TantraCrew (the lift and the way in from outside, ids from 100) are served together with ours
    void Init(VESSEL* ship, UINT vcMeshIdx, double (*meshDZ)(void*), void* dzCtx, TantraCrew* crew);
    void Register();                          // clbkPostCreation
    void Unregister();                        // destructor
    bool Available() const { return reg_; }
    // the ship's rule whether one may walk now (takeoff, landing, anamezon drive...); reason in cp1251
    void SetCanWalk(int (*fn)(void*, char*, int), void* ctx) { canWalk_ = fn; canCtx_ = ctx; }

    // the seat taken by a person in Use (the ship then shows the VC from it); -1 = none
    int TakenSeat() const { return seat_; }
    bool SeatMoving() const { for (int i = 0; i < 4; i++) if (seatPos_[i] != seatTarget_[i]) return true; return adjPos_ != adjTarget_; }   // (the micro-lift's sound)
    int SeatedPerson() const { return person_; }
    // F in seat N: the person sitting there (or, if none came in, the crew member of that seat) stands up behind the seat
    // with his or her body; false = nobody to stand up (no OrbiterCrew interior, no member aboard for that seat)
    bool StandUp(int seat);
    OBJHANDLE ViewerBody() const;
    void SetLiftPanel(const LiftPanelHooks& hk) { panel_ = hk; }
    // the lift: the people standing in the cabin now (ids), whether a person wears the suit (1 / 0 / -1 unknown), a person's name
    int CabinPeople(int* ids, int maxN) const;
    int SuitWorn(int id) const;
    bool PersonName(int id, char* out, int n) const;
    void Step(double dt);                     // clbkPreStep: the moving seats, the deferred stand-up, the lift cabin, the panel lights
    void OnVisual(VISHANDLE vis);              // the panel caps and the status screen (its render surface goes into slot kLiftStatusSlot)
    void OnVisualGone() { mesh_ = nullptr; }

private:
    static ATTACHMENTHANDLE cAttach(void* c);
    static int cGround(void* c, const VECTOR3* p, double stepUp, double* floorY);
    static void cWalls(void* c, const VECTOR3* from, VECTOR3* to, double radius, double height);
    static int cCount(void* c);
    static int cItem(void* c, int i, OcItem* out);
    static void cUse(void* c, int id, int personId);
    static void cSeat(void* c, int id, VECTOR3* pos, VECTOR3* dir);
    static void cViewing(void* c, int on);
    static void cOrigin(void* c, VECTOR3* o);
    static int cCanWalk(void* c, char* reason, int n);
    static void cSeated(void* c, int seatId, int personId, int on);
    static int cClick(void* c, const VECTOR3* origin, const VECTOR3* dir, int personId);   // the touch screens
    static void cOuterWalls(void* c, const VECTOR3* from, VECTOR3* to, double radius, double height);   // people outside: the cup feet (Tantra::OuterWalls)

    LiftPanelHooks panel_{};
    int PanelItem(int k, OcItem* out) const;
    int CabExitItem(OcItem* out) const;
    // the inner lift (lobby shaft, three stops: the lower deck, the living deck, the technical level): F at its door calls it,
    // F at a stop lamp inside sends it; the cab carries the people standing in it
    int ILiftItem(int k, OcItem* out) const;  // k 0..2 call at stop k (outside), 3..5 go to stop k-3 (inside)
    void ILiftStep(double dt);
    void ILiftWalls(double& x, double& z, double feet, double radius, double height) const;
    bool ILiftAt(int stop) const { return std::fabs(iliftY_ - kILiftStopY(stop)) < 0.01 && iliftV_ == 0.0; }
    bool ILiftOpen(int stop) const { return iliftDoor_[stop] > 0.9; }
    static double kILiftStopY(int k);
    double iliftY_ = 0.0, iliftTarget_ = 0.0, iliftV_ = 0.0;
    bool iliftInit_ = false;
    UINT iliftAnim_ = 0, iliftDoorAnim_[3] = {0, 0, 0};
    double iliftDoor_[3] = {0, 0, 0};          // 0 shut .. 1 open
    struct IRider { int id; double x, z; VECTOR3 dir; };
    IRider irider_[8] = {};
    int nIRiders_ = 0;     // F at the cabin's door when it stands on the ground: out to the ground (as F at the lift brings one in)
    void CabinPose(double& sx, double& sy, bool& stowed, bool& atGround) const;   // the cabin's offset from its stowed place (mesh frame)
    void CabinWalls(double& x, double& z, double feet, double radius, double height) const;
    void CarryRiders();                       // the cabin carries the people in it (ocCarry) while it moves
    void PanelLights();                       // the caps of the lift panel: lit / dark / blinking (the panel shows its state: no HUD without a suit)
    DEVMESHHANDLE mesh_ = nullptr;
    shipview::MfdBank mfds_;
    ConsoleHooks console_{};
    struct PanelBtn { int area, mx, my; VECTOR3 pos, n; double r; };
    std::vector<PanelBtn> pbtn_;                 // the clickable spots of the panels on the console
    std::vector<int> pareas_;                    // the panel areas shown on the console (redrawn)
    double panelT_ = 0.0;
    void BuildPanelButtons();
    bool ClickPanel(double tx, double ty);   // a click at a point of panel.dds
    bool PanelPoint(int panel, double tx, double ty, VECTOR3* pos, VECTOR3* n) const;                  // the 12 console MFDs of the bridge (real Orbiter MFDs stuck to the ship)
    TouchHooks touch_{};
    UINT sideAnim_[2] = {0, 0};
    int scrBlink_ = -1;                     // the main screen's blinking light shown (PanelLights)
    double sidePos_[2] = {0, 0}, sideTarget_[2] = {0, 0};   // 0 up (in use), 1 down in the pedestal
    int capShown_[6] = {-1, -1, -1, -1, -1, -1};
    UINT cabOutAnim_ = 0, cabDownAnim_ = 0, doorBAnim_ = 0, cabDoorAnim_ = 0;   // the cabin's panel rides with the cabin; door B's leaves
    SURFHANDLE cabScr_ = nullptr; double cabScrT_ = 0.0;
    void DrawCabStatus();
    void DrawTable(SURFHANDLE surf, char* buf, int W, int H);   // the lift's screens: a header, rows "key\tvalue", then messages
    oapi::Pen* rule_ = nullptr;
    TantraGlyphs glyphs_;                    // screen text: the helmet display's glyph atlas (system fonts draw no Cyrillic here)
    SURFHANDLE status_ = nullptr; oapi::Font* fontB_ = nullptr; oapi::Font* fontS_ = nullptr; double statusT_ = 0.0;
    void DrawStatus();
    struct Rider { int id; double rx, z; VECTOR3 dir; };
    Rider riders_[8] = {};
    int nRiders_ = 0;
    bool wasMoving_ = false;
    double lastSx_ = 0.0, lastSy_ = 0.0;
    double DZ() const { return dz_(dzCtx_); }
    bool InCapsule(double x, double y, double z) const;      // mesh frame
    void Resolve(double& x, double& z, double feet, double radius, double height, bool capsule) const;

    VESSEL* v_ = nullptr;
    TantraCrew* crew_ = nullptr;
    UINT vcMesh_ = 0;
    double (*dz_)(void*) = nullptr;
    void* dzCtx_ = nullptr;
    OcApi api_;
    OcInterior fns_ = {};
    OcInteriorExt ext_ = {};
    int (*canWalk_)(void*, char*, int) = nullptr;
    void* canCtx_ = nullptr;
    ATTACHMENTHANDLE att_ = nullptr;
    bool reg_ = false;
    int seat_ = -1, person_ = 0;
    int standSeat_ = -1;                     // queued stand-up
    int riseId_ = 0, riseSeat_ = -1;         // a person getting up from a seat: carried behind it once she stands
    double riseT_ = 0.0, riseOn_ = 0.0;
    UINT anim_[4] = {0, 0, 0, 0};            // the seats run to the console (0 back, 1 at the console)
    double seatPos_[4] = {0, 0, 0, 0}, seatTarget_[4] = {0, 0, 0, 0};
    // the commander's own travel at the console, set by the touch slider on his right armrest (kSeatAdjMin..Max, state 0..1)
    UINT adjAnim_ = 0; double adjPos_ = 0.333, adjTarget_ = 0.333, adjShown_ = -1.0;
    SURFHANDLE adjSurf_ = nullptr;
    double AdjOffset() const;
    void DrawAdj();
    double SeatX(int i) const;               // the seat's current place (interior frame)
    double SeatZ(int i) const;
    int mfdPlaces_ = 0;                      // the MFD places of the mesh (kMfdCount): the monitors' MFDs come after them
};
