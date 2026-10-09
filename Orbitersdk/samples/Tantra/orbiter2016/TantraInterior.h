// TantraInterior: the Tantra's interior as given to OrbiterCrew (OcInterior, OrbiterCrewApi.h).
// A person walks inside with his or her own body (OrbiterCrew moves it); the ship only answers where the floors and the walls
// are (the collision boxes of tools/gen_mesh.py in InteriorLayout.h) and what can be used (the bridge seats).
// A person sits in a seat with the body (OrbiterCrew: OcInteriorExt::Seated), the seat then runs to its console; standing up,
// OrbiterCrew puts her behind the seat. The focus and the camera stay the person's all the time (the user's rule).
#pragma once
#define STRICT
#include "orbitersdk.h"
#include "../../../OrbiterCrew/include/OrbiterCrewApi.h"
#include "ShipMfd.h"
#include "TantraGlyphs.h"
#include "TantraScreenFont.h"
#include "TantraGost.h"
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
    void SideFold(int k, int state);           // a side glass: 0 РАБОТА (up), 1 ЛЕНТА (sunk to the front glass's top), 2 ВНИЗ (in its bay)
    int SideFoldState(int k) const;
    bool SideDisplayUp(int k) const { return k >= 0 && k < 2 && sidePos_[k] < 0.02; }
    bool SideDisplayUsable(int k) const;       // standing still in РАБОТА or ЛЕНТА: its part over the desk takes touches
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
    bool ViewerInBridge() const;              // the camera is inside the bridge capsule (internal view: the person's eyes or the VC)
    bool ScreenInView(int k) const;           // touch screen k (kTouch) inside the camera's view cone (screens out of it are not redrawn)
    void SetLiftPanel(const LiftPanelHooks& hk) { panel_ = hk; }
    // the lift: the people standing in the cabin now (ids), whether a person wears the suit (1 / 0 / -1 unknown), a person's name
    int CabinPeople(int* ids, int maxN) const;
    int SuitWorn(int id) const;
    bool PersonName(int id, char* out, int n) const;
    void Step(double dt);                     // clbkPreStep: the moving seats, the deferred stand-up, the lift cabin, the panel lights
    void OnVisual(VISHANDLE vis);              // the panel caps and the status screen (its render surface goes into slot kLiftStatusSlot)
    void OnVisualGone() { mesh_ = nullptr; spotShown_ = ballShown_ = uvtShown_ = -1; yokeKey_[0] = -1e9; quadKey_[0] = -1e9; }
    // ---- the commander's place (variant 7; TantraSeatCmd.cpp) ----
    double CmdSeatOffset() const;              // his seat along its facing from its empty place (travel + his adjustment), m
    double CmdSeatHeight() const;              // its pan's height from the nominal (m)
    bool CmdAtDesk() const;                    // he sits and the seat has arrived at the desk (the yoke comes out)
    bool SpotOn() const { return spotOn_; }    // the cursor unit's light spot is on
    bool SpotScreen(int& screen, double& u, double& v) const;   // the spot on a touch screen now: which, where (0..1 of its picture)
    bool UvtOn() const { return uvtOn_; }      // УВТ (the button on the yoke's right horn): the yoke drives the pods too
    double YokeOut() const { return yokeE_; }  // the yoke: 0 stowed .. 1 out
    // the throttle quadrant (TantraYoke.cpp), every frame from the displays: the levels set (0..1), the cups' angle set (deg),
    // the pods' guard cover open
    void SetQuadrant(double mainSet, double podSet, double cupsDeg, bool coverOpen) { qMainT_ = mainSet; qPodT_ = podSet; qWheelT_ = cupsDeg; qCoverT_ = coverOpen ? 1.0 : 0.0; }

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
    static void cSeatHands(void* c, int seatId, int personId, OcHand* left, OcHand* right);   // where his hands rest / hold
    static int cClick(void* c, const VECTOR3* origin, const VECTOR3* dir, int personId);   // the touch screens
    static void cOuterWalls(void* c, const VECTOR3* from, VECTOR3* to, double radius, double height);   // people outside: the cup feet (Tantra::OuterWalls)

    LiftPanelHooks panel_{};
    int PanelItem(int k, OcItem* out) const;
    int CabExitItem(OcItem* out) const;
    // the inner lift (lobby shaft, four stops since 2026-10-09: the lower level, the middle level with the cabins, the living
    // deck, the technical level - kILiftStopN): F at its door calls it, F at a stop lamp inside sends it; the cab carries the
    // people standing in it
    static constexpr int kILiftMax = 8;     // capacity of the per-stop arrays (InteriorLayout.h: kILiftStopN <= this)
    int ILiftItem(int k, OcItem* out) const;  // k 0..N-1 call at stop k (outside), N..2N-1 go to stop k-N (inside)
    void ILiftStep(double dt);
    void ILiftWalls(double& x, double& z, double feet, double radius, double height) const;
    bool ILiftAt(int stop) const { return std::fabs(iliftY_ - kILiftStopY(stop)) < 0.01 && iliftV_ == 0.0; }
    bool ILiftOpen(int stop) const { return iliftDoor_[stop] > 0.9; }
    static double kILiftStopY(int k);
    double iliftY_ = 0.0, iliftTarget_ = 0.0, iliftV_ = 0.0;
    bool iliftInit_ = false;
    UINT iliftAnim_ = 0, iliftDoorAnim_[kILiftMax] = {};
    double iliftDoor_[kILiftMax] = {};         // 0 shut .. 1 open
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
    UINT anim_[4] = {0, 0, 0, 0};            // the seats run to the console (0 back, 1 at the console)
    double seatPos_[4] = {0, 0, 0, 0}, seatTarget_[4] = {0, 0, 0, 0};
    // the commander's own travel at the console, set by the touch slider on his right armrest (kSeatAdjMin..Max, state 0..1)
    UINT adjAnim_ = 0; double adjPos_ = 0.333, adjTarget_ = 0.333, adjShown_ = -1.0;
    SURFHANDLE adjSurf_ = nullptr;
    double AdjOffset() const;
    void DrawAdj();                          // the seat's panel texture: the strips ХОД, ВЫСОТА and the cursor unit's key plate
    // the seat's height (variant 7): the pan on its telescopic pedestal, kSeatHgtMin..Max (state 0..1)
    UINT hgtAnim_ = 0; double hgtPos_ = 0.4, hgtTarget_ = 0.4;
    // the cursor unit and its light spot («солнечный зайчик»): the ball toggles it; while it is on the spot follows the mouse over
    // the screens (a light on the glass where the cursor points) and the screens take a click at any distance
    struct Pick { int kind = -1, idx = -1; double u = 0, v = 0, t = 1e9; VECTOR3 p{}, ex{}, up{}, n{}; };
    bool PickScreens(const VECTOR3& o, const VECTOR3& d, const VECTOR3* head, Pick& best) const;   // kind 0 panel piece, 1 MFD, 2 screen facet
    void PressAt(const Pick& b);
    bool HoverRay(VECTOR3& o, VECTOR3& d) const;   // the ray from the camera through the mouse cursor (interior frame); false: not over the view
    void SpotStep();
    void SeatPlate(double px, double py);    // a touch on the cursor unit's plate (its texture px)
    void WarpToGlass(int dir);               // ◄ ►: the cursor to the middle of the glass to the left / right
    bool spotOn_ = false;
    Pick spotHit_;
    int spotShown_ = -1, ballShown_ = -1;
    double curLit_[5] = {};                  // the cursor unit's keys: lit till then (system time)
    double panelKey_ = -1.0;                 // what the seat's panel shows (redrawn when it changes)
    tantra::ScreenFont pfont_;               // the panel's brushes and pens
    tantra::GostFont gost_;                  // its lettering
    // the yoke (TantraYoke.cpp): out of the desk when he sits at it; it only shows what the keyboard / the joystick does (or the
    // ship's own turning), and it shakes with the structure; its rigid parts move by their vertices (the mesh's own at the start)
    void YokeStep(double dt);
    struct YokeGrp { int grp; std::vector<NTVERTEX> v; };
    std::vector<YokeGrp> yokeRef_;
    double yokeE_ = 0.0, ykRoll_ = 0.0, ykPitch_ = 0.0;
    double vibE_[3] = {}, vibB_[3] = {}; unsigned vibSeed_ = 0x2545F491u;
    double attPitch_ = 0.0, attBank_ = 0.0; bool attInit_ = false;
    VECTOR3 hubH_ = {0, 0, 0}, hubX_ = {1, 0, 0}, hubY_ = {0, 1, 0}, hubZ_ = {0, 0, -1};
    double yokeKey_[8] = {};                 // what the vertices show (rewritten when it changes)
    bool uvtOn_ = false; int uvtShown_ = -1;
    void QuadStep(double dt);
    std::vector<YokeGrp> quadRef_;
    double qMainT_ = 0.0, qPodT_ = 0.0, qWheelT_ = 0.0, qCoverT_ = 0.0, qMain_ = 0.5, qPod_ = 0.5, qWheel_ = 0.0, qCover_ = 0.0;
    int leverDrag_ = -1; double leverSent_ = -1.0;   // a lever held by its grip (the mouse button down): 0 МАРШ, 1 ВЫДВ. БЛОКИ
    VECTOR3 LeverGrip(int k) const;                  // its grip's centre now (interior frame)
    void LeverDrag();                                // while held: the cursor's ray sets its lean = the thrust
    double quadKey_[4] = {-1e9, 0, 0, 0};
    VECTOR3 wheelC_ = {0, 0, 0};             // the cups' wheel now (touch: the next angle 0 / 90 / 180)
    double SeatX(int i) const;               // the seat's current place (interior frame)
    double SeatZ(int i) const;
    int mfdPlaces_ = 0;                      // the MFD places of the mesh (kMfdCount): the monitors' MFDs come after them
};
