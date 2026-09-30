// Tantra: Orbiter 2016 vessel adapter.
// All ship physics and systems logic lives in ../core; this layer only maps it
// onto the Orbiter API, XRSound and our crew module OrbiterCrew (TantraCrew).
#pragma once
#define STRICT
#include "orbitersdk.h"
#include "XRSound.h"
#include "TantraCrew.h"

#include "../core/Carriage.h"
#include "../core/Drive.h"
#include "../core/Ignition.h"
#include "../core/Params.h"
#include "../core/Spec.h"
#include "MeshLayout.h"

class TantraExhaust;  // TantraExhaust.h: anamezon exhaust visuals
class TantraSafety;   // TantraSafety.h: radiation safety interlock
class TantraGear;     // TantraGear.h: mesh animations

class Tantra : public VESSEL3 {
public:
    Tantra(OBJHANDLE hVessel, int flightmodel);
    ~Tantra();

    void clbkSetClassCaps(FILEHANDLE cfg) override;
    void clbkPostCreation() override;
    void clbkLoadStateEx(FILEHANDLE scn, void* status) override;
    void clbkSaveState(FILEHANDLE scn) override;
    void clbkPreStep(double simt, double simdt, double mjd) override;
    void clbkPostStep(double simt, double simdt, double mjd) override;
    int clbkConsumeBufferedKey(DWORD key, bool down, char* kstate) override;
    bool clbkDrawHUD(int mode, const HUDPAINTSPEC* hps, oapi::Sketchpad* skp) override;

    // 2D panel (TantraPanel.cpp)
    bool clbkLoadPanel2D(int id, PANELHANDLE hPanel, DWORD viewW, DWORD viewH) override;
    bool clbkPanelMouseEvent(int id, int event, int mx, int my, void* context) override;
    bool clbkPanelRedrawEvent(int id, int event, SURFHANDLE surf, void* context) override;
    static void ReleasePanelTexture();

private:
    enum class EngineSet { Planetary = 0, Anamezon = 1 };

    void LoadParams(FILEHANDLE cfg);
    void DefineMassAndShape();
    void DefinePropulsion();
    void DefineAttitude();
    void DefineAerodynamics();
    void DefineCrew();
    void DefineGear();
    void UpdateGear(double simdt);
    void SetSuspension(const VECTOR3* t, int n);  // elastic touchdown vertices (2016)
    bool Settled() const { return settleTimer_ <= 0.0 && settledFor_ >= 2.0; }  // gate for load / damage checks

    void BindMainGroup(EngineSet set);
    void RebindGroups();              // main: anamezon or stern ring; hover: pods swivelled down
    bool AnaIsMain() const;           // anamezon chambers hold the main throttle (only while feeding)
    void UpdatePods(double dt);
    void ActPods(bool hover);          // B: pods out and cups down / cups aft and pods in; Shift+B: out, cups aft
    void ActPodsTo(double deg);
    void UpdateCG(bool force);         // shifts the vessel frame to the CG of the mass budget
    void AimThroughCG();               // stern cups: the magnetic nozzles steer the jet through the CG (<= 7 deg)
    void DefineControlSurfaces(bool on);
    void UpdateControlSurfaces();
    double Zf(double s) const { return s - frameS_; }                     // hull station -> vessel frame z
    double MeshDZ() const { return tantra::spec::kOriginS - frameS_; }    // mesh frame -> vessel frame (z)
    double TrapZ() const;              // trap column centre (vessel frame z)
    void SelectActiveTrap();
    void ScaleAttitudeThrust();
    double HotStartLevelCap() const;  // 1 = no limit
    bool HotStartBlocked() const { return HotStartLevelCap() <= 0.0; }
    void ApplyThrottleLimits();
    void UpdateSound(tantra::IgnStage prevStage);
    void Message(const char* ru, const char* en, ...);

    // Actions shared by the keyboard and the panel.
    void ActToggleEngineSet();
    void ActSelectEngine(bool anamezon);
    void ActIgnitionTo(int stage);  // 0 off .. 3 feed
    void ActIgnitionStep();
    void ActToggleOverride();
    void ActToggleGLimit();
    void ActCycleGLimit();
    void ActNextTrap();
    void ActToggleAirlock();
    void ActEva();
    void ActSelectCrew(int delta);
    void ActErect();                  // carriage: stand the ship on its stern / lay it level
    void ActGear();                   // gear down / up (in flight)
    void ActGearSet();                // flight gear set: level (columns) / standing (stern legs)
    void ActCrests();                 // fold / unfold crests and sink the pods (interstellar)
    void ActHangar();
    void ActRovers();
    void ActPort();                   // maglev port table under the stern

    // Anamezon port and magnetic manipulator (TantraPort.cpp).
    enum class PortStep { Idle, Open, Down, Up, Engage, Place, Home, Close, Hold };
    void DefinePort();
    void UpdatePort(double dt);
    void ActPortLoad();               // take the cassette under the door into the next free slot
    void ActPortDrop();               // hand a trap out through the port and set it down / release it
    void ActPortLift();               // lift the level ship to the loading height and back
    void ActPortStop();
    void SetTrapPresent(int slot, bool on);
    OBJHANDLE FindContainer(int column, tantra::Vec3& centre) const;
    int NextLoadSlot() const;
    int NextDropSlot() const;
    void UpdateEmptyMass();

    // Panel drawing helpers (TantraPanel.cpp).
    void PanelText(SURFHANDLE s, int x, int y, const char* text, int font, int maxChars = 999);
    void PanelTextCentered(SURFHANDLE s, const int* area, int y, const char* text, int font);
    void PanelBig(SURFHANDLE s, int x, int y, const char* text);
    void PanelFill(SURFHANDLE s, int x, int y, int w, int h, int r, int g, int b);
    void PanelClear(SURFHANDLE s, int areaId);
    void PanelButton(SURFHANDLE s, int areaId, const char* text, int state);
    bool PanelChanged(int areaId, const char* key);
    bool RedrawMain(int id, SURFHANDLE s);
    bool RedrawLower(int id, SURFHANDLE s);
    double LocalG() const;  // surface gravity of the reference body at the current radius
    const char* L(const char* ru, const char* en) const { return russian_ ? ru : en; }

    tantra::ShipParams prm_;

    // Propellant: four keel traps (anamezon) and the ion-trigger charges.
    PROPELLANT_HANDLE trap_[tantra::spec::kTrapCount] = {};
    PROPELLANT_HANDLE ion_ = nullptr;
    int activeTrap_ = 0;

    THRUSTER_HANDLE ana_[tantra::spec::kAnaCount] = {};
    THRUSTER_HANDLE plan_[tantra::spec::kPlanCount] = {};   // central stern ring
    THRUSTER_HANDLE pod_[tantra::spec::kPodCups] = {};      // auxiliary pods
    VECTOR3 podExhPos_[tantra::spec::kPodCups] = {}, podExhDir_[tantra::spec::kPodCups] = {};
    EXHAUSTSPEC podExh_[tantra::spec::kPodCups] = {};
    bool podHover_ = false;
    double podOut_ = 0.0;                      // pods: 0 in the bays (doors shut) .. 1 hanging out
    bool podsWanted_ = false;                  // pilot wants the pods out
    bool podMachWarned_ = false;
    double podShare_[2] = {1.0, 1.0};          // thrust of the aft / fore pair (pitch balance)
    bool podAssist_ = false;                   // pods help the carriage stand the ship up / lay it down
    double podAssistLevel_ = 0.0;
    double gust_ = 0.0, windForce_ = 0.0;      // gust part of the wind [m/s], wind force on the broadside [N]
    double podSideForce_ = 0.0, podTilt_ = 0.0;  // gust compensation by the pods [N], cup deflection [rad]
    VECTOR3 podTiltV_ = {0, 0, 0};               // (unused: a lean of the jets makes a moment on the 9.8 m arm)
    VECTOR3 windFh_ = {0, 0, 0};                 // horizontal wind force (ship frame) [N]
    double podCouple_ = 0.0;                     // moment the pods apply against wind and sway [N m]
    bool podBalanceLost_ = false;                // pods cannot balance about the CG in this attitude
    void PodAssistLevels(double simdt, double podMax);
    double sway_ = 0.0, swayMax_ = 0.0, swayMaxT_ = 0.0;  // lateral sway of the CG [m], its recent peak
    unsigned windRng_ = 20260930u;
    bool orbiterWind_ = false;                 // wind comes from Orbiter (air data non-zero)
    double windSpeed_ = 0.0;                   // measured horizontal wind [m/s]
    double legLoad_ = 0.0, legRatio_ = 0.0;    // most loaded leg [N] and its share of the rating
    double tipWind_ = 0.0;                     // wind that would overturn the ship now [m/s]
    const char* legName_ = "";
    void UpdateWind(double dt);
    void GuardAgainstLaunch(double dt);  // a contact bounce must never throw the ship off the ground
    void Reland();                       // put the ship back down where it is (Orbiter's landed status)
    double sinceContact_ = 1e9, relandCool_ = 0.0;
    int relands_ = 0;
    void WatchTerrain();                 // Orbiter refines the terrain under a resting ship: follow it
    bool terrainInit_ = false;
    double terrainLng_[5] = {}, terrainLat_[5] = {}, terrainElev_[5] = {};
    void UpdateLegLoads(const VECTOR3& windForceH);
    double frameS_ = tantra::spec::kOriginS;   // station of the vessel frame origin (= CG)
    AIRFOILHANDLE foil_[4] = {};
    CTRLSURFHANDLE ctrl_[4] = {};              // elevator, two ailerons (elevons), body flap
    bool ctrlOn_ = false;
    double elevon_[2] = {0.0, 0.0}, bodyFlap_ = 0.5;  // mesh states (port, starboard; flap 0..1 = 0..25 deg)
    EngineSet engineSet_ = EngineSet::Planetary;
    double podAngle_ = 0.0, podTarget_ = 0.0;  // deg: 0 thrust forward, 90 thrust up
    int planGroup_ = -1;                       // main throttle: 0 anamezon, 1 planetary stern ring
    tantra::Ignition ignition_;
    tantra::Drive drive_;
    TantraExhaust* exhaust_ = nullptr;
    TantraSafety* safety_ = nullptr;
    bool safetyWarned_ = false;
    double hazardTimer_ = 0.0;  // repeats the danger warning while overridden
    double SafetyLevelCap() const;  // radiation: 1 = full feed endangers nobody
    double thrustAccel_ = 0.0;  // last step thrust acceleration [m/s^2]
    bool hotStartOverride_ = false;
    bool hotStartWarned_ = false;

    // Attitude micro-motors: pitch/yaw couples (also translation), roll pairs, axial.
    THRUSTER_HANDLE attCouple_[8] = {};
    THRUSTER_HANDLE attRoll_[4] = {};
    THRUSTER_HANDLE attAxial_[2] = {};

    // Relativity and loads.
    double beta_ = 0.0;
    double properTime_ = 0.0;  // ship clock [s]
    double accelG_ = 0.0;      // felt (non-gravitational) acceleration [g]
    bool gLimitOn_ = true;
    double gLimit_ = 5.0;

    // Undercarriage and animations.
    tantra::Carriage carriage_;
    TantraGear* gear_ = nullptr;
    UINT meshIdx_ = 0;
    VECTOR3 touch_[tantra::CarriagePose::kMaxTouch] = {};
    int nTouch_ = 0;
    double touchMass_ = 0.0;          // mass the suspension was last tuned for
    bool touchSettled_ = false;
    // Settling after a scenario start: overdamped suspension, no load/damage checks until the ship
    // has come to rest on its contacts (Orbiter puts a landed vessel down with uncompressed contacts).
    double settleTimer_ = 6.0;        // s of overdamped suspension
    double settledFor_ = 0.0;         // s at rest on the ground
    bool crestsFolded_ = false;       // manual (interstellar) fold
    double tuck_ = 0.0;               // crests/pods, flight mode part
    double hangar_ = 0.0, hangarT_ = 0.0, rovers_ = 0.0, roversT_ = 0.0;
    double irisAna_ = 0.0, irisPlan_ = 1.0, airlockUp_ = 0.0;

    // Anamezon port.
    double liftY_[2] = {}, liftYT_[2] = {};   // fork heads of the column lifts (cassette centre, ship y)
    ATTACHMENTHANDLE grip_[2] = {};
    PortStep portStep_ = PortStep::Idle;
    bool portLoading_ = true;          // current sequence: load (true) or drop
    int portSlot_ = -1;
    OBJHANDLE container_ = nullptr;    // container on the saddle
    bool trapPresent_[4] = {true, true, true, true};
    double bayDoors_ = 0.0, bayDoorsT_ = 0.0;

    // Crew (OrbiterCrew figures outside).
    TantraCrew crew_;
    int selectedCrew_ = 0;

    // XRSound.
    XRSound* sound_ = nullptr;

    // HUD.
    bool russian_ = true;
    oapi::Font* hudFont_ = nullptr;
    int hudFontHeight_ = 0;
    char messageRu_[256] = {};
    char messageEn_[256] = {};

    // 2D panel.
    MESHHANDLE panelMesh_ = nullptr;
    char panelCache_[128][256] = {};  // last drawn content per area, to skip redundant blits
    double messageTimer_ = 0.0;
};
