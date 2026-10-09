// «Грань» 25,4 м - the pilots' cabin in Orbiter (LanderCabin.msh, gen_lander_cabin.py, layout v10 approved 2026-10-09):
// the drum for two turning in pitch after the felt acceleration, each pilot's module (couch, yoke, outboard console, the
// panels with the MFDs) rolling ±15° after its lateral part; the big view screen is a hole in the drum (the real outside
// seen from the eye). Each pilot: two Orbiter MFDs (НАВ-1, НАВ-2) and the ship's MFD with the 12 keys of his systems
// (a key opens its page, the page's two buttons are touched on the screen); the mode switch on the pedestal (left click
// next, right click back). Positions: 0 the commander (left), 1 the second pilot (right).
// As in «Тантра» (the user, 2026-10-09): the person sits and walks with the body (OrbiterCrew, LanderInterior), the focus
// and the camera are the person's - the MFDs are real Orbiter MFDs stuck to the ship (ShipView MfdBank) drawn into their
// screens' textures, the screens are touched by the click's ray (Click). The Orbiter VC (F8 with the lander in focus) shows
// the same cabin from the commander's eye.
// The tracked points (the eyes, the hips, the hands' targets, the screens' corners) ride on Orbiter's own animation (local
// vertex lists in the same components), so they follow the mesh whatever the rotation's sign; the sign itself is checked
// on the first turn (the drum's up must go where the felt acceleration says) and flipped if Orbiter turns the other way.
#pragma once
#include "Orbitersdk.h"
#include "ShipMfd.h"

namespace tantra::lander {

class CockpitHost {                           // what the cabin asks of the vessel
public:
    virtual ~CockpitHost() = default;
    virtual bool KeyLit(int pilot, int key) const = 0;
    virtual int PageLines(int pilot, int key, char out[][96], int max) const = 0;   // the page's text lines
    virtual int PageButtons(int pilot, int key, const char* lbl[2]) const = 0;       // its touch buttons (0..2)
    virtual void PagePress(int pilot, int key, int button) = 0;
    virtual void ModeStep(int dir) = 0;
    virtual const char* ModeName() const = 0;
};

class Cockpit {
public:
    ~Cockpit();
    void Init(VESSEL4* v, UINT mesh, CockpitHost* host, int launchMfdMode);
    void Start() { bank_.Start(); }                          // clbkPostCreation
    void OnVisual(VISHANDLE vis);                            // clbkVisualCreated: the screens' surfaces into the mesh
    void OnVisualGone();
    void Shutdown() { bank_.Shutdown(); }
    bool LoadVC(int id);                                     // the Orbiter VC (the lander in focus)
    void Step(double dt, double frameZ);
    bool Mouse(int id, int event, const VECTOR3& p);         // a VC click
    bool Click(const VECTOR3& o, const VECTOR3& d);          // a click's ray, interior (mesh) frame: a screen or a key hit
    static const char* KeyName(int pilot, int key);

    // the geometry now (interior = mesh frame, Orbiter axes): the seats, the hands, the turning zones
    VECTOR3 Hip(int p) const { return pts_[p][kHip]; }
    VECTOR3 Facing(int p) const { return unit(pts_[p][kHipF] - pts_[p][kHip]); }
    VECTOR3 Eye(int p) const { return pts_[p][kEye]; }
    VECTOR3 Up(int p) const { return unit(pts_[p][kUp] - pts_[p][kEye]); }
    void GripIn(int p, VECTOR3& g0, VECTOR3& g1, VECTOR3& hubFacing) const { g0 = pts_[p][kG0]; g1 = pts_[p][kG1]; hubFacing = unit(pts_[p][kHubN] - pts_[p][kHub]); }
    VECTOR3 MarchKnob(int p) const { return pts_[p][kMarch]; }
    // the rigid motion of the drum (module -1) or a pilot's module (0, 1): p_ship = R·p_built + t
    void Motion(int module, MATRIX3& R, VECTOR3& t) const;
    bool InDrum(const VECTOR3& p) const;                     // a point (interior frame) inside the drum
    int ModuleAt(const VECTOR3& p) const;                    // the pilot's module a point belongs to (-1 none)
    const VECTOR3& Felt() const { return felt_; }            // the felt (specific) acceleration, vessel frame, m/s²
    double DrumDeg() const { return drum_; }
    double RollDeg() const { return roll_; }

    // the tracked points of a module (pts_[p][...])
    enum { kEye = 0, kLook, kUp, kMfd0 = 3, kShip = 27, kRollUp = 39, kHip = 40, kHipF, kG0, kG1, kHub, kHubN, kMarch, kScr0 = 47, kPts = 55 };
private:
    void Quads(bool force);
    void Camera(bool force);
    void Draw();                                             // the button labels and the ships' MFDs into their surfaces
    VECTOR3 At(int module, int i) const;                     // a tracked point in the vessel frame (module 2 = the drum's own list)
    static bool HitQuad(const VECTOR3& o, const VECTOR3& d, const VECTOR3& p1, const VECTOR3& p2, const VECTOR3& p3, double& t, double& u, double& v);

    VESSEL4* v_ = nullptr;
    CockpitHost* host_ = nullptr;
    UINT mesh_ = 0;
    shipview::MfdBank bank_;                                 // the 4 Orbiter MFDs (0 К НАВ-1, 1 К НАВ-2, 2 2-й НАВ-1, 3 2-й НАВ-2)
    SURFHANDLE lblSurf_ = nullptr, shipSurf_ = nullptr;
    UINT drumAnim_ = 0, rollAnim_ = 0;
    VECTOR3 pts_[2][kPts] = {};               // per module, carried by the roll (a child of the drum)
    VECTOR3 dpts_[3] = {};                    // the drum's: the mode switch, the axis centre, the centre + up
    double drum_ = 0, roll_ = 0;              // deg: the drum's up toward the nose; the modules' up toward the right
    double drumSign_ = 1, rollSign_ = -1;     // the animation state per degree (Orbiter's rotation sense, checked live)
    bool drumChecked_ = false, rollChecked_ = false;
    VECTOR3 felt_ = {0, 9.81, 0};
    double frameZ_ = 0;
    int pos_ = 0;                             // the VC position (the camera's pilot)
    bool loaded_ = false;
    VECTOR3 lastLook_ = {0, 0, 1};
    double quadDrum_ = 1e9, quadRoll_ = 1e9, quadZ_ = 1e9;
    int page_[2] = {6, 0};                    // the ship MFDs' pages: the commander's ШАССИ, the second's НАКОП
    double drawT_ = 1.0;
    oapi::Font* fLbl_ = nullptr;
    oapi::Font* fKey_ = nullptr;
    oapi::Font* fPage_ = nullptr;
    oapi::Font* fTitle_ = nullptr;
};

}  // namespace tantra::lander
