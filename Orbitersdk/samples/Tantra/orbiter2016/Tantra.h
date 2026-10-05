// Tantra: Orbiter 2016 vessel adapter.
// All ship physics and systems logic lives in ../core; this layer only maps it
// onto the Orbiter API, XRSound and our crew module OrbiterCrew (TantraCrew).
#pragma once
#define STRICT
#include "orbitersdk.h"
#include "XRSound.h"
#include "TantraCrew.h"
#include "TantraLift.h"
#include "../core/Plant.h"
#include "../core/TantraCore.h"
#include "TantraWalk.h"
#include "TantraScreen.h"
#include "TantraInterior.h"
#include "TantraDisplays.h"

#include "../core/Carriage.h"
#include "../core/Damage.h"
#include "../core/Impact.h"
#include "../core/Foot.h"
#include "TantraSolids.h"
#include "../core/Drive.h"
#include "../core/Ignition.h"
#include "../core/Legs.h"
#include "../core/Params.h"
#include "../core/Spec.h"
#include "MeshLayout.h"

class TantraExhaust;  // TantraExhaust.h: anamezon exhaust visuals
class TantraSafety;   // TantraSafety.h: radiation safety interlock
class TantraGear;     // TantraGear.h: mesh animations

class Tantra : public VESSEL3 {
    friend class TantraDisplays;   // the commander's touch screens read the ship's state and call its Act*() like the 2D panels
public:
    Tantra(OBJHANDLE hVessel, int flightmodel);
    ~Tantra();
    // the power plant, for its screen on the bridge
    const tantra::plant::Plant& PlantState() const { return plant_; }
    const tantra::plant::Output& PlantOut() const { return plantOut_; }
    const tantra::tcore::Snapshot& CoreState() const { return core_.S(); }   // the energy core's nodes (core/TantraCore)
    double MarchLevel() const { return march_ ? GetThrusterLevel(march_) : 0.0; }
    double ArgonMass() const { return GetPropellantMass(argon_); }
    double IronMass() const { return GetPropellantMass(iron_); }
    void PlantPress(int k) { PlantKey(k); }

    void clbkSetClassCaps(FILEHANDLE cfg) override;
    void clbkPostCreation() override;
    void clbkVisualCreated(VISHANDLE vis, int refcount) override;
    void clbkVisualDestroyed(VISHANDLE vis, int refcount) override;
    void clbkLoadStateEx(FILEHANDLE scn, void* status) override;
    void clbkSaveState(FILEHANDLE scn) override;
    void clbkPreStep(double simt, double simdt, double mjd) override;
    void clbkPostStep(double simt, double simdt, double mjd) override;
    int clbkConsumeBufferedKey(DWORD key, bool down, char* kstate) override;
    void clbkNavMode(int mode, bool active) override;        // Orbiter's autopilots are not used in the Tantra: switched off at once
    int clbkConsumeDirectKey(char* kstate) override;        // walk mode keys (TantraWalk)
    bool clbkVCMouseEvent(int id, int event, VECTOR3& p) override;   // the screen button
    bool clbkLoadVC(int id) override;                        // the interior as a virtual cockpit (TantraVC.msh)
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
    void RebindGroups();              // main: anamezon or marching cup; hover: pods swivelled down; retro: nose cups
    void UpdateReactionMass();        // argon below 30 km, iron above: resource and Isp of the planetary cups
    bool AnaIsMain() const;
    int CanWalk(char* reason, int n) const;   // may the people walk inside now (OrbiterCrew); 0 + reason if not           // anamezon chambers hold the main throttle (only while feeding)
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
    void ActLift();                // Shift+A: main airlock crew lift down to the ground / up
    void ActEva();
    void ActSelectCrew(int delta);
    void ActErect();                  // carriage: stand the ship on its stern / lay it level
    void ActGear();                   // gear down / up (in flight)
    void ActGearSet();                // flight gear set: level (columns) / standing (stern legs)
    void ActCrests();                 // wings 90 -> 30 -> folded (+ fin down, pods in: interstellar) -> 90
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

    // Propellant: four keel traps (anamezon), argon (below 30 km) and iron (above) for the planetary engines.
    PROPELLANT_HANDLE trap_[tantra::spec::kTrapCount] = {};
    PROPELLANT_HANDLE argon_ = nullptr, iron_ = nullptr;
    int activeTrap_ = 0;

    THRUSTER_HANDLE ana_[tantra::spec::kAnaCount] = {};
    THRUSTER_HANDLE march_ = nullptr;                       // marching planetary cup in the stern well
    THRUSTER_HANDLE retro_[tantra::spec::kRetroCount] = {};  // nose retro anamezon cups
    THRUSTER_HANDLE pod_[tantra::spec::kPodCups] = {};      // auxiliary pods
    VECTOR3 podExhPos_[tantra::spec::kPodCups] = {}, podExhDir_[tantra::spec::kPodCups] = {};
    EXHAUSTSPEC podExh_[tantra::spec::kPodCups] = {};
    bool podHover_ = false;
    double podOut_ = 0.0;                      // pods: 0 in the bays (doors shut) .. 1 hanging out
    bool podsWanted_ = false;                  // pilot wants the pods out
    bool podMachWarned_ = false;
    double podShare_[2] = {1.0, 1.0};          // thrust of the aft / fore pair (pitch balance)
    bool podAssist_ = false;                   // pods help the carriage stand the ship up / lay it down
    bool podAimed_ = false;                    // doors fully out and every cup on its commanded angle
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
    double colRatio_ = 0.0, sternRatio_ = 0.0; // carriage legs / stern legs: load over rating
    // Damage (core/Damage): heating by zones, loads on the exposed parts, g, touchdowns, leg overloads.
    tantra::damage::Model damage_;
    bool wasContact_ = false;
    double lastVy_ = 0.0;                      // vertical speed of the last airborne step [m/s]
    bool destroyedWarned_ = false;
    double structG_ = 0.0;                     // structural load factor, smoothed [g]
    void UpdateDamage(double dt);
    // Damage on the mesh: lost parts are not rendered (group flag 0x2, no shadow 0x1), hot shields glow.
    void UpdateDamageVisual(bool force);
    VISHANDLE vis_ = nullptr;
    // Broken parts leave the ship as vessels of their own (MeshLayout kDebris): where they were, with the
    // ship's velocity plus a separation push and a tumble. The destroyed hull breaks into four chunks.
    void SpawnDebris(const char* name, const VECTOR3* posLocal, const VECTOR3& pushLocal, double tumble);
    void BreakPart(int part);
    void BreakUp();
    int debrisCount_ = 0;
    bool shipGone_ = false, shownGone_ = false;
    bool shownLost_[tantra::damage::kPartCount] = {};
    unsigned long long shownLeg_[7] = {~0ull, ~0ull, ~0ull, ~0ull, ~0ull, ~0ull, ~0ull};   // what each leg showed lost (bits)
    VECTOR3 padPos_[7][12] = {};               // each petal's pad in the ship frame (where its debris leaves)
    double glowT_[2] = {-1.0, -1.0};
    bool TouchPointLost(int i, const tantra::CarriagePose& p) const;
    int TouchLeg(int i, const tantra::CarriagePose& p) const;  // leg carrying touchdown point i: 0 blade port,
                                                                // 1 starboard, 2..5 stern legs, 6 kangaroo; -1 hull
    // Leg systems (core/Legs): MR ankle struts, jamming soles with anchors, fibre sensors, magnetic bearings.
    tantra::legs::Soles solesCar_, solesStern_;
    tantra::legs::Regen regen_;
    double strut_[7] = {};                     // ankle strut rods: travel from the static sag [m] (+ out, - in)
    double penLeg_[7] = {};                    // each leg's pads under the ground = its strut compression [m]
    double legN_[7] = {}, legR_[7] = {};       // sensed load [N] and its share of the rating, per leg (6 = kangaroo)
    bool legAlarm_[7] = {};
    bool hipCatcher_ = false;                  // hip magnetic bearings over capacity: running on the catchers
    // the gear's pads: 12 gas cells per foot in the ground set (core/Foot), or the hull points of a belly set
    static constexpr int kMaxPads = 64;
    double touchMu_[kMaxPads] = {0.7, 0.7, 0.7, 0.7, 0.7, 0.7, 0.7, 0.7};
    double touchMuLng_[kMaxPads] = {0.7, 0.7, 0.7, 0.7, 0.7, 0.7, 0.7, 0.7};  // along the hull
    tantra::foot::Feet feet_;                  // cells, ribs, ankles, stage joints
    double cellPen_[7][12] = {};               // each cell's compression from unloaded (its pad's depth) [m]
    double cellD_[7][12] = {};                 // its petal strut shown from the mesh pose [m] (+ out, - in)
    double legPen_[7] = {};                    // each leg's mean pad compression [m]: the ankle strut takes its share
    double microJ_[7][12] = {};                // the petals' small working motion while the legs carry the moving ship [m]
    PSTREAM_HANDLE footDust_[7] = {};          // dust thrown up by the feet (touchdown, the legs working)
    double footDustLv_[7] = {}, footDustPulse_[7] = {};
    VECTOR3 footDustAt_[7] = {};
    bool dustContact_ = false;
    void FootDust(double dt, const VECTOR3& up);
    int suspFrame_ = 0, forceFrame_ = 0, frame_ = 0;   // which frame last called SetTouchdownPoints / AddForce (rest diagnostics)
    double restLostLog_ = -1e9;
    void FootEvents();
    VESSELSTATUS2 frozenVs_ = {};           // the landed state the ship is held in under time warp
    OBJHANDLE lastTrapsChunk_ = nullptr;      // the trap-block piece of a break-up (the camera follows it)
    bool LegJointDebris(int leg);              // a leg lost at a joint: its foot / lowest stage as debris
    double tipWind_ = 0.0;                     // wind that would overturn the ship now [m/s]
    // Balance device: hull angle and rate against the commanded erection angle, the holding moment, the pause.
    void UpdateBalance(double dt);
    double balErr_[2] = {0.0, 0.0}, balRate_[2] = {0.0, 0.0};   // pitch (about x), roll (about z) [rad], [rad/s]
    double balTorque_[2] = {0.0, 0.0};                           // applied [N m]
    double balBias_[2] = {0.0, 0.0};                             // slow zero: the static lean of the hull on its feet [rad]
    bool balHold_ = false;                                       // erection paused: swaying
    void UpdateWarpFreeze(double dt);
    void ReportImpact();
public:
    // people walking outside bump into the cup feet (OrbiterCrew: OcInteriorExt::OuterWalls), ship frame
    void OuterWalls(const VECTOR3& from, VECTOR3& to, double radius, double height) const;
    int OuterSolids(TantraSolid* out, int max) const;   // the supports at the ground for other vessels (TantraSolids.h)
private:
    VECTOR3 cupC_[7] = {}, cupUp_ = {0, 1, 0};
    double cupR_[7] = {};
    bool cupOn_[7] = {};                                         // a hull impact: what the crew learns
    void LiftSound(bool inside);
    void CrashSound(int slotExt, double level);                  // breaking: the _EXT slot, its _INT follows (0..1)                                  // the carriage heard (UpdateSound)
    double liftP_ = -1.0, liftGear_ = 0.0, liftH_ = 0.0, liftHip_ = 0.0, liftTh_ = 0.0, liftMast_ = 0.0, liftLevel_ = 0.0;
    bool liftBusy_ = false, liftContact_ = true;
    double liftVz_ = 0.0, sinceContactPrev_ = 0.0;                                     // high time warp on the ground: freeze (landed status)
    bool frozen_ = false;
    // ground mechanism: the hull placed over the planted feet while the carriage moves on the ground
    void GroundMechanism(double dt, const VECTOR3* t, const int* legOf, int nt);
    bool mechOn_ = false;
    VECTOR3 mechSide_ = {1, 0, 0};
    bool planted_[8] = {};
    double plLng_[8] = {}, plLat_[8] = {}, plRad_[8] = {};
    double mechSway_ = 0.0, mechRate_ = 0.0, mechKick_ = 0.0, mechLog_ = 0.0;
    double colX_ = 0.0, colVX_ = 0.0, colY_ = 0.0, colVY_ = 0.0, colLastHipRate_ = 0.0;   // column tops' sway [m]
    double mechLastTheta_ = -1.0, mechLastHip_ = 0.0, mechThRate_ = 0.0, mechHipRate_ = 0.0, mechShare_ = 0.0;
    double balCalm_ = 0.0;                                       // s calm so far
    double balRamp_ = 1.0;                                       // soft start of the erection after a pause, 0..1
    double balOver_ = 0.0;                                       // s the sway rate has been over the limit
    const char* legName_ = "";
    void UpdateWind(double dt);
    void GuardAgainstLaunch(double dt);  // a contact bounce must never throw the ship off the ground
    double bounceDamp_ = 0.0;            // s left of the bounce damping (forces, no state reset)
    // Orbiter's frames are left-handed: a force F at r gives the torque F x r (GetTorqueVector,
    // GetAngularVel use it). torqueSign_ = +1 assumes that; the ship checks it against GetTorqueVector
    // while the pods thrust and flips it if Orbiter disagrees.
    double torqueSign_ = 1.0;
    VECTOR3 podTorquePred_ = {0, 0, 0};  // pods' torque predicted last step (F x r)
    int torqueDisagree_ = 0;
    double compOff_ = 0.0;               // s: wind/sway compensation off (the ship began to turn)
    double sinceContact_ = 1e9, relandCool_ = 0.0;
    int relands_ = 0;
    void WatchTerrain();                 // Orbiter refines the terrain under a resting ship: follow it
    bool terrainInit_ = false;
    double terrainLng_[5] = {}, terrainLat_[5] = {}, terrainElev_[5] = {};
    double terrainOfs_ = 0.0;
    VECTOR3 touchOfs_ = {0, 0, 0};            // touchdown points raised to a refined terrain, relaxing to 0 [m]
    void UpdateLegLoads(const VECTOR3& windForceH);
    double frameS_ = tantra::spec::kOriginS;   // station of the vessel frame origin (= CG)
    AIRFOILHANDLE foil_[5] = {};             // body pitch, body yaw, dorsal fin, lateral crests, gear/pods
    // Aerodynamic state read by the coefficient callbacks: availability of the fin and crests (folded or
    // retracted = 0) and the frontal area of the gear and pods in the flow [m^2].
    double aeroFin_ = 1.0, aeroCrest_ = 1.0, aeroGearArea_ = 0.0;
    // Carriage drives: the two legs never lift in step. Each side lags the commanded trunnion motion by its own
    // servo time and carries a little noise; the error is worked off in ~2 s once the motion stops.
    double driveLag_[2] = {0.0, 0.0}, driveNoise_[2] = {0.0, 0.0}, lastTrunnionH_ = -1.0;
    unsigned driveRng_ = 11235813u;
    CTRLSURFHANDLE ctrl_[4] = {};              // elevator, two ailerons (elevons), body flap
    bool ctrlOn_ = false;
    double elevon_[2] = {0.0, 0.0}, bodyFlap_ = 0.5;  // mesh states (port, starboard; flap 0..1 = 0..25 deg)
    EngineSet engineSet_ = EngineSet::Planetary;
    double podAngle_ = 0.0, podTarget_ = 0.0;  // deg: 0 thrust forward, 90 thrust up
    // the pods' own thrust set by their lever (the bridge's ГОНДОЛЫ) and УВТ: the yoke drives them differentially
    // (TantraVectoring.cpp; the bridge session, agreed with the fork 2026-10-04)
    void PodVectoring(double simdt);
    double podCmd_ = 0.0;                      // the lever: 0..1 (locked at 0 until the pods are fully out)
    double podLv_[tantra::spec::kPodCups] = {};   // each cup's level as driven (it follows at 3 /s)
    int planGroup_ = -1;                       // main throttle: 0 anamezon, 1 marching planetary cup
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
    UINT vcMeshIdx_ = 0;
    TantraWalk walk_;
    TantraInterior interior_; // the interior for OrbiterCrew: people walk inside with their bodies
    TantraDisplays disp_;     // the commander's touch screens (elbow displays, flight terminal, engine console)
    TantraScreen screen_;     // bridge: big screen (outside view), its button, console MFD
    LightEmitter* watchLight_[6] = {};   // 0..3 the way to the lift, 4..5 the bridge capsule
    LightEmitter* cabLight_ = nullptr;
    LightEmitter* cabSpot_[2] = {};       // the cabin's two floodlights (down and out), on while the cabin is out of the cell   // the lift cabin's ceiling light (rides with the cabin; on while someone is inside or it is out)   // watch lighting of the way from the bridge to the lift (InteriorLayout.h kWatchLights)
    void WatchLights();                  // positions (they follow the CG shift) and on/off: only while someone looks inside
    VECTOR3 touch_[kMaxPads] = {};
    int nTouch_ = 0;
    double touchMass_ = 0.0;          // mass the suspension was last tuned for
    double touchK_[kMaxPads] = {}, touchC_[kMaxPads] = {};   // per pad: the cell's gas spring and its valve
    bool touchTuned_ = false;
    bool touchSettled_ = false;
    // Settling after a scenario start: overdamped suspension, no load/damage checks until the ship
    // has come to rest on its contacts (Orbiter puts a landed vessel down with uncompressed contacts).
    double settleTimer_ = 6.0;        // s of overdamped suspension
    double settledFor_ = 0.0;         // s at rest on the ground
    bool crestsFolded_ = false;       // interstellar: wings folded, fin down, pods in (wingMode_ == 2)
    int wingMode_ = 0;                // 0 = 90 (deployed), 1 = raised 30 deg, 2 = folded
    double wingIn_ = 0.0, wingOut_ = 0.0;   // inner panels 0..1 (of kWingFoldDeg), outer panels 0..1 (folded under)
    double tuck_ = 0.0;               // crests/pods, flight mode part
    double hangar_ = 0.0, hangarT_ = 0.0, rovers_ = 0.0, roversT_ = 0.0;
    double irisAna_ = 0.0, irisMarch_ = 0.0, marchOut_ = 0.0, irisNose_ = 0.0, airlockUp_ = 0.0;
    bool marchHigh_ = false;          // marching cup and pods run on iron (above kMarchArgonAlt), else argon

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
    TantraLift lift_;              // main airlock crew lift (port flank s 129)
    bool liftWasDown_ = false;
    // The lift (the user, 2026-10-03): one walks into the cabin, goes down with it by its DOWN button, steps out at the bottom by
    // its OUT button; coming back the same the other way. The cabin is the lock: going down, door B (lift zone <-> cell) shuts,
    // the cell is equalised with the outside (not on the Earth), the cabin goes; coming up, the cell is filled, door B opens.
    // The checks (everybody in the cabin in the suit) are on DOWN; a second press goes at one's own risk. An empty cabin goes
    // only when called from the other end (the lift zone's button 3 / F at the foot of the lift outside).
    void PanelPress(int which, int personId);   // 0..2 the lift zone's panel, 3..5 the cabin's DOWN, UP, OUT
    void PanelLabel(int which, char* out, int n) const;
    int PanelState(int which) const;      // the light of a panel button (TantraInterior LiftPanelHooks::State)
    static SURFHANDLE PanelTex();
    int navAllowed_ = 0;                  // bits (1 << NAVMODE_*): the autopilots switched on from the holo panel (keys stay refused)
    // a light at each person inside (other vessels inside the hull are in its shadow: their faces were black). Small and near
    // the body, so the interior keeps its gloom; it follows the person through the bridge, the corridors, the lifts.
    static constexpr int kPersonLights = 4;
    LightEmitter* personLight_[kPersonLights] = {};
    VECTOR3 personLightPos_[kPersonLights] = {};   // vessel frame (the emitters keep a reference)
    void PersonLights();
    void SeatKeys();                      // the numpad in the commander's seat (the focus is the person): throttle, attitude, killrot
    unsigned seatKeyPrev_ = 0;            // the keys held last step (edges)
    unsigned seatAttSet_ = 0;             // the attitude groups the numpad drives now (bits THGROUP_ATT_*)
    bool seatSurf_ = false;               // the helm holds the control surfaces (released on leaving the seat)
    tantra::plant::Plant plant_;                  // the planetary power plant: field, limiter, heat, failures
    tantra::plant::Output plantOut_;
    tantra::tcore::Core core_;                    // the energy core around the plant: nodes, power, the shocks' consequences
    void CoreStep(double simdt);                  // after UpdateDamage (TantraCoreStep.cpp)
    unsigned plantKeyPrev_ = 0;
    int plantStage_ = -1;                          // the plant's stage last step (the march lever drops off the run)
    void LoadPlantConfig();
    void UpdatePlant(double simdt, double f);
    void PlantKey(int k);                          // 0 field-, 1 field+, 2 power-, 3 power+, 4 limiter, 5 reaction mass,
                                                   // 6 ПУСК/СТОП, 7..10 mass auto / argon / iron / products
    bool restLock_ = false, firstRest_ = false;   // at rest in Orbiter's landed state (UpdateWarpFreeze)
    double restTimer_ = 0.0;
    void LandNow(bool equilibrium);
    bool tuckSet_ = false;                // the wings' ground fold set at once on the first step
    double liftoffAlt_ = 0.0;             // origin height above the ground at the last contact (stern-first takeoff)
    bool autoGearArmed_ = false;          // the stern legs stow by themselves once 50 m up (after each stand)
    void AutoFlightSet();                 // the stern legs after a tail-first takeoff; crests on a stern-first descent
    void ToggleNav(int mode);             // an Orbiter autopilot from the flight terminal (TantraDisplays)         // the 2D panels' texture (loaded on first use), shared with the bridge console
    void PanelStatus(char* out, int n) const;    // the lift zone's screen
    void CabStatus(char* out, int n) const;      // the cabin's screen
    void PanelStep(double dt);
    bool LiftGo(bool lower);              // the lift itself, with the ship's rule (lying on the gear, the carriage at rest)
    void LiftCall();                      // F at the foot of the lift outside: the cabin comes down (it is up)
    bool OutsideAirOk() const;
    double OutsideP() const;              // the outside pressure, fraction of the normal one (0..1)
    bool PressureEqual() const;
    int alertBtn_ = -1; double alertT_ = 0.0;   // a refused press: this button blinks fast
    double overrideT_ = 0.0;              // after a refusal: a second press of DOWN within this time goes at one's own risk
    char panelErr_[160] = {0}; double panelErrT_ = 0.0;   // the last refusal / risk (both screens)
    char suitMsg_[96] = {0}; double suitMsgT_ = 0.0; int suitOk_ = -1;   // the lift zone's button 1: the presser's suit
    double zonePressure_ = 1.0, zonePressureTarget_ = 1.0;   // the air of the cell (the cabin in it), fraction of the normal pressure
    double doorB_ = 0.0, doorBT_ = 0.0;   // door B: 0 open .. 1 shut
    double cabDoor_ = 0.0, cabDoorT_ = 0.0;   // the cabin's own doors: 0 open .. 1 shut (shut while it travels)
    int trip_ = 0;                        // 0 none, 1 going down (door B, air, the cabin's doors, then the cabin), 2 going up, 3 riding down,
                                          // 4 about to go up (the cabin's doors shut first)
    char panelMsg_[160] = {0};             // the answer of the last pressed button: shown in its hint for a while (the person sees no ship HUD)
    int panelMsgBtn_ = -1;
    double panelMsgT_ = 0.0;
    int selectedCrew_ = 0;

    // XRSound.
    XRSound* sound_ = nullptr;
    int soundInside_ = -1;          // listener inside the hull (1) / outside (0): the outside default sounds follow it

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
