// Tantra: Orbiter 2016 vessel adapter.
#include "Tantra.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "TantraExhaust.h"
#include "InteriorLayout.h"
#include "TantraSafety.h"
#include "TantraGear.h"

#include "../core/Aero.h"
#include "../core/Physics.h"

using namespace tantra;
namespace sp = tantra::spec;

namespace {

// XRSound ids (ours must stay below 10000).
// Engines in two layers: outside (XRSound fades it by distance and air - nothing in vacuum) and inside (Global, our own
// volume: the planetary engines only as a dull rumble through the hull; the anamezon drive shakes the whole ship).
enum SoundSlot { SND_ANA_RUN = 1, SND_ION_RUN, SND_FIELD_UP, SND_BEAM_UP, SND_IGNITE, SND_SHUTDOWN = 7,
                 SND_MARCH_EXT, SND_MARCH_INT, SND_POD_EXT, SND_POD_INT, SND_ANA_INT, SND_SEAT,
                 // the carriage (tools/gen_lift_sounds.py): outside through the air, inside through the hull
                 SND_LIFT_HUM_EXT, SND_LIFT_HUM_INT, SND_LIFT_STRUT_EXT, SND_LIFT_STRUT_INT, SND_LIFT_CLUNK_EXT, SND_LIFT_CLUNK_INT,
                 SND_LIFT_BOOM_EXT, SND_LIFT_BOOM_INT, SND_LIFT_RAIL_EXT, SND_LIFT_RAIL_INT, SND_LIFT_STOW_EXT, SND_LIFT_STOW_INT,
                 SND_LIFT_STAGE_EXT, SND_LIFT_STAGE_INT, SND_LIFT_CUPS2_EXT, SND_LIFT_CUPS2_INT, SND_LIFT_CUPS4_EXT, SND_LIFT_CUPS4_INT,
                 SND_LIFT_BOLTS_EXT, SND_LIFT_BOLTS_INT, SND_LIFT_LEGS_EXT, SND_LIFT_LEGS_INT,
                 // breaking (tools/gen_crash_sounds.py): the hull hitting the ground, a leg's joint, a petal
                 SND_CRASH_EXT, SND_CRASH_INT, SND_LEG_BREAK_EXT, SND_LEG_BREAK_INT, SND_PETAL_SNAP_EXT, SND_PETAL_SNAP_INT };

// Crew from the novel.
// Ages, pulse and weight are placeholders the player can edit in the scenario.
struct CrewSeed { const char* name; int age; int pulse; int weight; const char* role; };
const CrewSeed kCrew[] = {
    {"Erg Noor", 44, 62, 82, "Capt"},        // expedition leader
    {"Niza Krit", 23, 68, 58, "Spe"},        // astronavigator, first flight
    {"Pel Lin", 36, 66, 74, "Spe"},          // astronavigator
    {"Pur Hiss", 52, 70, 80, "APhy"},        // astronomer
    {"Ingrid Ditra", 31, 67, 60, "APhy"},    // astronomer
    {"Eon Tal", 38, 64, 76, "Bio"},          // biologist
    {"Luma Lasvi", 35, 66, 57, "Doc"},       // physician
    {"Kay Ber", 33, 65, 75, "Eng"},          // electronic engineer
    {"Bina Led", 34, 67, 62, "Geo"},         // geologist
    {"Taron", 40, 63, 85, "Tech"},           // mechanic
    {"Ione Mar", 28, 66, 56, "Sci"},         // rhythm teacher, supplies, samples
    {"Mechanic Two", 37, 65, 80, "Tech"},    // unnamed in the novel
    {"Mechanic Three", 39, 64, 79, "Tech"},  // unnamed in the novel
    {"Astronomer Two", 45, 66, 70, "APhy"},  // unnamed in the novel
};

// Crew to the ground: the hangar floor platform lowers to the ground; the crew steps off beside it. The port
// airlock door serves in space.
const double kCrewLiftS = 110.0;   // hangar platform (T9 hangar s 99.6..120.6)
const VECTOR3 kEvaPos = {4.5, -sp::kAxisHeight + 1.5, sp::Z(kCrewLiftS)};

// Attitude micro-motor blocks.
const double kAttNoseZ = sp::Z(sp::kAttNoseS), kAttTailZ = sp::Z(sp::kAttTailS);
const double kAttNoseR = sp::kAttNoseR, kAttTailR = sp::kAttTailR;

// Right-handed rotation of `p` about the unit `axis` (the mesh rig convention).
VECTOR3 RotateAbout(const VECTOR3& p, const VECTOR3& axis, double ang) {
    const double c = std::cos(ang), s = std::sin(ang);
    return p * c + crossp(axis, p) * s + axis * (dotp(axis, p) * (1.0 - c));
}
VECTOR3 V3(const tantra::mesh::V& a) { return _V(a.x, a.y, a.z); }

// Hull stations of the airfoil references (T8): body (Newtonian centre of pressure near the planform / side
// centroids), telescopic dorsal fin (726 m2, centre of pressure s 30, 9.7 m up the fin), wings (2 x 240 m2,
// centre s 14 on the hinge line), gear and pods (under the fairings).
const double kAeroS[5] = {82.0, 80.6, 30.2, 14.2, 62.0};
const double kAeroY[5] = {0.0, 0.0, 19.0, 1.82, -12.0};

// --- Aerodynamic coefficients -------------------------------------------------

// Coefficient callbacks: core/Aero (checked by tests/aero_table.cpp) scaled by what is in the flow.
void BodyPitch(VESSEL*, double aoa, double M, double, void*, double* cl, double* cm, double* cd) {
    tantra::aero::BodyPitch(aoa, M, cl, cd);
    *cm = 0.0;
}
void BodyYaw(VESSEL*, double beta, double M, double, void*, double* cl, double* cm, double* cd) {
    tantra::aero::BodyYaw(beta, M, cl, cd);
    *cm = 0.0;
}
struct PlateData { double aspect; const double* avail; };  // avail: 0 folded / retracted .. 1 out
void PlateCoeff(VESSEL*, double aoa, double M, double, void* ctx, double* cl, double* cm, double* cd) {
    const PlateData* p = static_cast<const PlateData*>(ctx);
    tantra::aero::Plate(aoa, M, p->aspect, cl, cd);
    const double k = *p->avail;
    *cl *= k;
    *cd *= k;
    *cm = 0.0;
}
// Gear legs, pads and the swung-out pods: bluff bodies, drag only (Cd 1.1 on their frontal area, S = 1).
void GearDrag(VESSEL*, double, double M, double, void* ctx, double* cl, double* cm, double* cd) {
    *cl = 0.0;
    *cm = 0.0;
    *cd = 1.1 * (M > 1.0 ? 1.4 : 1.0) * *static_cast<const double*>(ctx);
}

}  // namespace

// ==============================================================================

Tantra::Tantra(OBJHANDLE hVessel, int flightmodel) : VESSEL3(hVessel, flightmodel) {}

Tantra::~Tantra() {
    delete exhaust_;
    delete safety_;
    delete gear_;
    delete sound_;
    screen_.Shutdown();
    disp_.Shutdown();
    walk_.Shutdown();
    interior_.Unregister();
    if (hudFont_) oapiReleaseFont(hudFont_);
    if (panelMesh_) oapiDeleteMesh(panelMesh_);
}

void Tantra::clbkSetClassCaps(FILEHANDLE cfg) {
    char lang[32] = "ru";
    if (cfg) oapiReadItem_string(cfg, const_cast<char*>("HudLanguage"), lang);
    russian_ = _strnicmp(lang, "en", 2) != 0;
    LoadParams(cfg);
    LoadPlantConfig();

    DefineMassAndShape();
    DefinePropulsion();
    DefineAttitude();
    DefineAerodynamics();
    DefineCrew();

    meshIdx_ = AddMesh(oapiLoadMeshGlobal("Tantra\\Tantra"));
    SetMeshVisibilityMode(meshIdx_, MESHVIS_ALWAYS);
    vcMeshIdx_ = AddMesh(oapiLoadMeshGlobal("Tantra\\TantraVC"));   // the interior: floors, walls, rooms, the bridge capsule
    SetMeshVisibilityMode(vcMeshIdx_, MESHVIS_VC);
    walk_.Init(this);
    interior_.Init(this, vcMeshIdx_, [](void* s) { return static_cast<Tantra*>(s)->MeshDZ(); }, this, &crew_);
    interior_.SetCanWalk([](void* s, char* r, int n) { return static_cast<Tantra*>(s)->CanWalk(r, n); }, this);
    walk_.SetStandHook([](void* s, int seat) { return static_cast<TantraInterior*>(s)->StandUp(seat); }, &interior_);
    {   // the lift panel in the lift zone and the cabin that carries people
        LiftPanelHooks hk;
        hk.ctx = this;
        hk.Press = [](void* s, int which, int person) { static_cast<Tantra*>(s)->PanelPress(which, person); };
        hk.Label = [](void* s, int which, char* out, int n) { static_cast<Tantra*>(s)->PanelLabel(which, out, n); };
        hk.State = [](void* s, int which) { return static_cast<const Tantra*>(s)->PanelState(which); };
        hk.Status = [](void* s, char* out, int n) { static_cast<const Tantra*>(s)->PanelStatus(out, n); };
        hk.CabStatus = [](void* s, char* out, int n) { static_cast<const Tantra*>(s)->CabStatus(out, n); };
        hk.DoorB = [](void* s) { return static_cast<const Tantra*>(s)->doorB_; };
        hk.CabDoor = [](void* s) { return static_cast<const Tantra*>(s)->cabDoor_; };
        hk.Cabin = [](void* s, double* out, double* down, int* ground) {
            const Tantra* t = static_cast<Tantra*>(s);
            *out = t->lift_.Out(); *down = t->lift_.Down(); *ground = t->lift_.AtGround() ? 1 : 0;
        };
        interior_.SetLiftPanel(hk);
    }
    {   // the bridge console: the 2D panels, live (redrawn by the ship, clicks to the same handlers)
        ConsoleHooks ch;
        ch.ctx = this;
        ch.Tex = [](void*) { return Tantra::PanelTex(); };
        ch.Redraw = [](void* s, int a) { static_cast<Tantra*>(s)->clbkPanelRedrawEvent(a, PANEL_REDRAW_USER, Tantra::PanelTex(), nullptr); };
        ch.Click = [](void* s, int a, int mx, int my) { static_cast<Tantra*>(s)->clbkPanelMouseEvent(a, PANEL_MOUSE_LBDOWN, mx, my, nullptr); };
        interior_.SetConsole(ch);
    }
    {   // the commander's touch screens: TantraInterior passes the touches and the steps
        disp_.Init(this, vcMeshIdx_);
        TantraInterior::TouchHooks th;
        th.ctx = &disp_;
        th.Step = [](void* c, double dt) { static_cast<TantraDisplays*>(c)->Step(dt); };
        th.Touch = [](void* c, int k, double u, double v) { return static_cast<TantraDisplays*>(c)->Touch(k, u, v); };
        interior_.SetTouch(th);
    }
    screen_.Init(this, vcMeshIdx_);
    screen_.SetHud([](void* c, SURFHANDLE s, int w, int h, const VECTOR3& d, const VECTOR3& u, double f) { static_cast<TantraDisplays*>(c)->DrawHud(s, w, h, d, u, f); }, &disp_);
    DefineGear();
    DefinePort();
}

// Carriage columns + stern legs, all mesh animations (TantraGear builds the rig from MeshLayout.h).
void Tantra::DefineGear() {
    tantra::CarriageGeometry g;
    namespace m = tantra::mesh;
    g.restAxisH = m::kAxisH;
    g.standClear = -m::kStandGroundS;
    g.columnX = m::kHipXOut;
    g.footR = m::kFootR;
    g.footH = m::kFootH;
    g.kangFootH = m::kKangFootH;
    g.legMin = m::kLegLMin;
    g.legMax = m::kLegLMax;
    g.cgError = 0.3;  // the ship's CG is never exactly on the trunnions during the turn
    g.trackS0 = m::kCarS0;
    g.trackS1 = m::kCarS1;
    g.stowS = m::kStowS;
    g.standR = m::kStandR;
    g.sternFootR = m::kLegFootR;
    for (int i = 0; i < 4; ++i) g.standFoot[i] = {m::kLegs[i].radial.x * m::kStandR, m::kLegs[i].radial.y * m::kStandR, 0.0};
    g.kangHipS = m::kKangHip.z + sp::kOriginS;
    g.kangHipY = m::kKangHip.y;
    g.kangThigh = m::kKangThigh;
    g.kangShinMin = m::kKangShinMin;
    g.kangShinMax = m::kKangShinMax;
    g.kangKneeE = m::kKangKneeE;
    g.kangFootFwd = m::kKangFootFwd;
    g.kangFootR = m::kKangFootR;
    g.kangHipMaxDeg = m::kKangHipMax * DEG;
    g.bellyY = sp::kAftBottom;
    g.bellySNose = 125.0;
    carriage_.SetGeometry(g);
    gear_ = new TantraGear(this, meshIdx_);
    carriage_.Update(0.0, frameS_);
}

// Every tunable can be overridden in Config\Vessels\Tantra.cfg ("Key = value").
void Tantra::LoadParams(FILEHANDLE cfg) {
    struct Item { const char* key; double* value; };
    const Item items[] = {
        {"DryMass", &prm_.dryMass},
        {"TrapStructMass", &prm_.trapStructMass},
        {"TrapFuelMass", &prm_.trapFuelMass},
        {"ArgonMass", &prm_.argonMass},
        {"IronMass", &prm_.ironMass},
        {"AnaThrust", &prm_.anaThrust},
        {"AnaExhaustC", &prm_.anaExhaustC},
        {"PelletRate", &prm_.pelletRate},
        {"MassToEnergy", &prm_.massToEnergy},
        {"ChamberFieldEnergy", &prm_.chamberFieldEnergy},
        {"FieldStoreCapacity", &prm_.fieldStoreCapacity},
        {"FieldRoundTrip", &prm_.fieldRoundTrip},
        {"RecoveryFraction", &prm_.recoveryFraction},
        {"CompWattPerNewton", &prm_.compWattPerNewton},
        {"CompMaxG", &prm_.compMaxG},
        {"PlantPower", &prm_.plantPower},
        {"Housekeeping", &prm_.housekeeping},
        {"AnaChargedFraction", &prm_.anaChargedFraction},
        {"AnaNozzleEfficiency", &prm_.anaNozzleEfficiency},
        {"AnaJetHalfAngle", &prm_.anaJetHalfAngle},
        {"HotStartBaseAlt", &prm_.hotStartBaseAlt},
        {"HotStartPerDecade", &prm_.hotStartPerDecade},
        {"HotStartRefPower", &prm_.hotStartRefPower},
        {"MarchThrust", &prm_.marchThrust},
        {"PodThrustTotal", &prm_.podThrustTotal},
        {"ArgonExhaust", &prm_.argonExhaust},
        {"IronExhaust", &prm_.ironExhaust},
        {"AttAngAccel", &prm_.attAngAccel},
        {"AttMaxThrust", &prm_.attMaxThrust},
        {"AttAxialAccel", &prm_.attAxialAccel},
        {"GLimitDefault", &prm_.gLimitDefault},
    };
    if (cfg) {
        for (const Item& it : items) {
            double v;
            if (oapiReadItem_float(cfg, const_cast<char*>(it.key), v)) *it.value = v;
        }
    }
    drive_.Configure(prm_.MakeDriveSpec(sp::kAnaCount));
    gLimit_ = prm_.gLimitDefault;
}

void Tantra::DefineMassAndShape() {
    SetSize(sp::kLength * 0.5);
    SetEmptyMass(prm_.dryMass + sp::kTrapCount * prm_.trapStructMass);
    SetPMI(_V(sp::kPmiPitch, sp::kPmiPitch, sp::kPmiRoll));
    SetCrossSections(_V(3800.0, 5200.0, 650.0));
    SetRotDrag(_V(0.3, 0.3, 0.1));

    // Touchdown points follow the carriage every step (UpdateGear); start lying on the blades.
    const double ground = -sp::kAxisHeight;
    const VECTOR3 t0[3] = {_V(sp::kColumnX, ground, 9.8), _V(-sp::kColumnX, ground, 9.8), _V(sp::kColumnX, ground, -9.8)};
    SetSuspension(t0, 3);

    SetCameraOffset(_V(0.0, 3.0, sp::Z(sp::kControlPostS)));  // central control post
}

void Tantra::DefinePropulsion() {
    namespace m = tantra::mesh;
    for (int i = 0; i < sp::kTrapCount; ++i) trap_[i] = CreatePropellantResource(prm_.trapFuelMass);
    argon_ = CreatePropellantResource(prm_.argonMass);
    iron_ = CreatePropellantResource(prm_.ironMass);

    const VECTOR3 fwd = _V(0, 0, 1);
    // Four chambers recessed in the armoured well at the stern.
    VECTOR3 mouths[sp::kAnaCount];
    for (int i = 0; i < sp::kAnaCount; ++i) {
        mouths[i] = _V(sp::kAnaCupX[i], sp::kAnaCupY[i], sp::Z(sp::kAnaFocusS));  // reaction at the cup focus
        ana_[i] = CreateThruster(mouths[i], fwd, prm_.anaThrust, trap_[0], prm_.AnaExhaust());
    }
    // Beam, constrictions, internal-shock knots and stern light (TantraExhaust.cpp).
    tantra::ExhaustSpec exh;
    exh.exhaustBeta = prm_.anaExhaustC;
    exh.chargedFraction = prm_.anaChargedFraction;
    exh.pelletRateMax = prm_.pelletRate;
    exhaust_ = new TantraExhaust(this, mouths, exh);
    safety_ = new TantraSafety(prm_.MakeRadiationSpec());
    // Nose retro cups (anamezon, R 2.2): jets forward, thrust aft - braking without turning the ship round.
    for (int i = 0; i < sp::kRetroCount; ++i) {
        const double x = (i == 0 ? 1.0 : -1.0) * m::kNoseCupX;
        retro_[i] = CreateThruster(_V(x, m::kNoseCupY, sp::Z(m::kNoseCupS)), _V(0, 0, -1), prm_.anaThrust * sp::kRetroAreaFrac, trap_[0],
                                   prm_.AnaExhaust());
    }
    // Marching planetary cup: in the central well, thrusts only run out past the anamezon rims (UpdateGear).
    march_ = CreateThruster(_V(0, m::kSternAxisY, sp::Z(m::kMarchLipS - m::kMarchTravel)), fwd, prm_.marchThrust, argon_, prm_.argonExhaust);
    AddExhaust(march_, 60.0, 3.5);
    // Four planetary pods x 3 cups in the flank bays (MeshLayout kPods). The cups are placed every step by
    // UpdatePods from the rig: arm run-out, pod swivel, jet splay, vessel frame.
    for (int p = 0; p < sp::kPodCount; ++p) {
        for (int c = 0; c < sp::kCupsPerPod; ++c) {
            const int k = p * sp::kCupsPerPod + c;
            pod_[k] = CreateThruster(V3(m::kPods[p].cup[c]), fwd, prm_.podThrustTotal / sp::kPodCups, argon_, prm_.argonExhaust);
            EXHAUSTSPEC& es = podExh_[k];
            es.th = pod_[k];
            es.level = nullptr;
            es.lpos = &podExhPos_[k];
            es.ldir = &podExhDir_[k];
            es.lsize = 24.0;
            es.wsize = 1.4;
            es.lofs = 0.0;
            es.modulate = 0.1;
            es.tex = nullptr;
            es.flags = 0;
            AddExhaust(&es);
        }
    }
    engineSet_ = EngineSet::Planetary;
    RebindGroups();
}

// Attitude by ion-trigger micro-motors fed from the same charges as the
// planetary engines. Their thrust is rescaled every step to the ship mass
// (see ScaleAttitudeThrust), so the handling is the same empty or loaded.
void Tantra::DefineAttitude() {
    const double isp = prm_.argonExhaust, F = prm_.attMaxThrust;
    const double zN = kAttNoseZ, zT = kAttTailZ, rN = kAttNoseR, rT = kAttTailR;
    THRUSTER_HANDLE* c = attCouple_;
    c[0] = CreateThruster(_V(0, -rN, zN), _V(0, 1, 0), F, argon_, isp);   // nose up
    c[1] = CreateThruster(_V(0, rN, zN), _V(0, -1, 0), F, argon_, isp);   // nose down
    c[2] = CreateThruster(_V(0, -rT, zT), _V(0, 1, 0), F, argon_, isp);   // tail up
    c[3] = CreateThruster(_V(0, rT, zT), _V(0, -1, 0), F, argon_, isp);   // tail down
    c[4] = CreateThruster(_V(rN, 0, zN), _V(-1, 0, 0), F, argon_, isp);   // nose left
    c[5] = CreateThruster(_V(-rN, 0, zN), _V(1, 0, 0), F, argon_, isp);   // nose right
    c[6] = CreateThruster(_V(rT, 0, zT), _V(-1, 0, 0), F, argon_, isp);   // tail left
    c[7] = CreateThruster(_V(-rT, 0, zT), _V(1, 0, 0), F, argon_, isp);   // tail right
    THRUSTER_HANDLE* r = attRoll_;
    r[0] = CreateThruster(_V(rT, 0, zT), _V(0, 1, 0), F, argon_, isp);    // bank left
    r[1] = CreateThruster(_V(-rT, 0, zT), _V(0, -1, 0), F, argon_, isp);
    r[2] = CreateThruster(_V(-rT, 0, zT), _V(0, 1, 0), F, argon_, isp);   // bank right
    r[3] = CreateThruster(_V(rT, 0, zT), _V(0, -1, 0), F, argon_, isp);
    attAxial_[0] = CreateThruster(_V(0, 0, zT), _V(0, 0, 1), F, argon_, isp);
    attAxial_[1] = CreateThruster(_V(0, 0, zN), _V(0, 0, -1), F, argon_, isp);

    auto group = [this](std::initializer_list<THRUSTER_HANDLE> th, THGROUP_TYPE type) {
        THRUSTER_HANDLE buf[4];
        int n = 0;
        for (THRUSTER_HANDLE h : th) buf[n++] = h;
        CreateThrusterGroup(buf, n, type);
    };
    group({c[0], c[3]}, THGROUP_ATT_PITCHUP);
    group({c[1], c[2]}, THGROUP_ATT_PITCHDOWN);
    group({c[4], c[7]}, THGROUP_ATT_YAWLEFT);
    group({c[5], c[6]}, THGROUP_ATT_YAWRIGHT);
    group({r[0], r[1]}, THGROUP_ATT_BANKLEFT);
    group({r[2], r[3]}, THGROUP_ATT_BANKRIGHT);
    group({c[0], c[2]}, THGROUP_ATT_UP);
    group({c[1], c[3]}, THGROUP_ATT_DOWN);
    group({c[4], c[6]}, THGROUP_ATT_LEFT);
    group({c[5], c[7]}, THGROUP_ATT_RIGHT);
    group({attAxial_[0]}, THGROUP_ATT_FORWARD);
    group({attAxial_[1]}, THGROUP_ATT_BACK);
}

void Tantra::ScaleAttitudeThrust() {
    const double m = GetMass();
    const double lever = kAttNoseZ - kAttTailZ;  // a couple acts on both levers
    const double couple = (std::min)(prm_.attMaxThrust, prm_.attAngAccel * sp::kPmiPitch * m / lever);
    const double roll = (std::min)(prm_.attMaxThrust, prm_.attAngAccel * sp::kPmiRoll * m / (2.0 * kAttTailR));
    const double axial = (std::min)(prm_.attMaxThrust, prm_.attAxialAccel * m);
    for (THRUSTER_HANDLE th : attCouple_) SetThrusterMax0(th, couple);
    for (THRUSTER_HANDLE th : attRoll_) SetThrusterMax0(th, roll);
    for (THRUSTER_HANDLE th : attAxial_) SetThrusterMax0(th, axial);
}

void Tantra::DefineAerodynamics() {
    namespace ae = tantra::aero;
    static PlateData fin = {0.9, nullptr}, crests = {1.5, nullptr};
    fin.avail = &aeroFin_;
    crests.avail = &aeroCrest_;
    // Airfoils act at hull stations (kAeroS / kAeroY); UpdateCG moves the refs with the vessel frame.
    // Body: normal force on the planform (centre of pressure near its centroid), axial on the frontal area.
    foil_[0] = CreateAirfoil3(LIFT_VERTICAL, _V(0, kAeroY[0], Zf(kAeroS[0])), BodyPitch, nullptr, ae::kLength, ae::kPlanform, 0.4);
    foil_[1] = CreateAirfoil3(LIFT_HORIZONTAL, _V(0, kAeroY[1], Zf(kAeroS[1])), BodyYaw, nullptr, ae::kLength, ae::kSide, 0.4);
    // Telescopic dorsal fin (726 m2) holds heading, wings (2 x 240 m2) hold pitch; scaled by what is out and how far
    // the wings are raised (cos^2 of the raise; nothing folded or with the fin down).
    foil_[2] = CreateAirfoil3(LIFT_HORIZONTAL, _V(0, kAeroY[2], Zf(kAeroS[2])), PlateCoeff, &fin, 34.5, 726.0, fin.aspect);
    foil_[3] = CreateAirfoil3(LIFT_VERTICAL, _V(0, kAeroY[3], Zf(kAeroS[3])), PlateCoeff, &crests, 16.8, 480.0, crests.aspect);
    // Gear and pods in the flow, under the hull: drag and the nose-down moment it brings.
    foil_[4] = CreateAirfoil3(LIFT_VERTICAL, _V(0, kAeroY[4], Zf(kAeroS[4])), GearDrag, &aeroGearArea_, 10.0, 1.0, 1.0);
}

// Elevons on the outer wing panels (2 x 35 m2, -30..+40 deg) and the body flap under the lower nacelles
// (14 x 7 m, 0..25 deg). Removed while the wings are folded.
void Tantra::DefineControlSurfaces(bool on) {
    if (on == ctrlOn_) return;
    for (CTRLSURFHANDLE& h : ctrl_) {
        if (h) DelControlSurface(h);
        h = nullptr;
    }
    if (on) {
        ctrl_[0] = CreateControlSurface3(AIRCTRL_ELEVATOR, 70.0, 1.2, _V(0, 1.82, Zf(3.0)), AIRCTRL_AXIS_XPOS, 1.0);
        ctrl_[1] = CreateControlSurface3(AIRCTRL_AILERON, 35.0, 1.2, _V(18.6, 1.82, Zf(3.0)), AIRCTRL_AXIS_XPOS, 1.0);
        ctrl_[2] = CreateControlSurface3(AIRCTRL_AILERON, 35.0, 1.2, _V(-18.6, 1.82, Zf(3.0)), AIRCTRL_AXIS_XNEG, 1.0);
        ctrl_[3] = CreateControlSurface3(AIRCTRL_ELEVATORTRIM, 98.0, 0.8, _V(0, -7.4, Zf(2.5)), AIRCTRL_AXIS_XPOS, 1.5);
    }
    ctrlOn_ = on;
}

// Mesh follows the control inputs: elevons = elevator +- aileron (trailing edge up for nose up), body
// flap from the elevator trim (neutral trim = 12.5 deg, the entry trim at 45..55 deg angle of attack).
void Tantra::UpdateControlSurfaces() {
    namespace m = tantra::mesh;
    const double up = m::kElevonUpDeg, dn = m::kElevonDownDeg;
    const double e = ctrlOn_ ? GetControlSurfaceLevel(AIRCTRL_ELEVATOR) : 0.0;
    const double a = ctrlOn_ ? GetControlSurfaceLevel(AIRCTRL_AILERON) : 0.0;
    const double t = ctrlOn_ ? GetControlSurfaceLevel(AIRCTRL_ELEVATORTRIM) : 1.0;
    auto state = [&](double cmd) {
        cmd = (std::max)(-1.0, (std::min)(1.0, cmd));
        const double deg = cmd > 0.0 ? -cmd * up : -cmd * dn;  // trailing edge down positive
        return (deg + up) / (up + dn);
    };
    elevon_[0] = state(e - a);
    elevon_[1] = state(e + a);
    bodyFlap_ = (std::max)(0.0, (std::min)(1.0, 0.5 * (1.0 - t)));
}

void Tantra::DefineCrew() {
    crew_.Init(this, kEvaPos, sp::kCrewSeats);
    for (const CrewSeed& c : kCrew) crew_.AddMember(c.name, c.age, c.pulse, c.weight, c.role);
}

void Tantra::clbkPostCreation() {
    interior_.Register();
    UpdateCG(true);   // the loaded propellant sets the CG before the first step
    UpdatePods(0.0);
    UpdateGear(0.0);  // mesh pose and touchdown points from the loaded state
    sound_ = XRSound::CreateInstance(this);
    if (!sound_ || !sound_->IsPresent()) return;
    using PT = XRSound::PlaybackType;
    sound_->LoadWav(SND_ANA_RUN, "XRSound\\Tantra\\ana_run.wav", PT::Global);
    sound_->LoadWav(SND_ION_RUN, "XRSound\\Tantra\\ion_run.wav", PT::BothViewFar);
    sound_->LoadWav(SND_FIELD_UP, "XRSound\\Tantra\\field_up.wav", PT::InternalOnly);
    sound_->LoadWav(SND_BEAM_UP, "XRSound\\Tantra\\beam_up.wav", PT::InternalOnly);
    sound_->LoadWav(SND_IGNITE, "XRSound\\Tantra\\ignite.wav", PT::BothViewMedium);
    sound_->LoadWav(SND_SHUTDOWN, "XRSound\\Tantra\\shutdown.wav", PT::InternalOnly);
    sound_->LoadWav(SND_MARCH_EXT, "XRSound\\Tantra\\march_ext.wav", PT::Global);
    sound_->LoadWav(SND_MARCH_INT, "XRSound\\Tantra\\march_int.wav", PT::Global);
    sound_->LoadWav(SND_POD_EXT, "XRSound\\Tantra\\pod_ext.wav", PT::Global);
    sound_->LoadWav(SND_POD_INT, "XRSound\\Tantra\\pod_int.wav", PT::Global);
    sound_->LoadWav(SND_ANA_INT, "XRSound\\Tantra\\ana_int.wav", PT::Global);
    sound_->LoadWav(SND_SEAT, "XRSound\\Tantra\\seat_servo.wav", PT::Global);
    {
        static const char* kLift[] = {"hum_ext", "hum_int", "strut_ext", "strut_int", "clunk_ext", "clunk_int",
                                      "boom_ext", "boom_int", "rail_ext", "rail_int", "stow_ext", "stow_int", "stage_ext", "stage_int",
                                      "cups2_ext", "cups2_int", "cups4_ext", "cups4_int",
                                      "bolts_ext", "bolts_int", "legs_ext", "legs_int"};
        char path[96];
        for (int i = 0; i < 22; ++i) {
            std::snprintf(path, sizeof path, "XRSound\\Tantra\\lift_%s.wav", kLift[i]);
            sound_->LoadWav(SND_LIFT_HUM_EXT + i, path, PT::Global);   // own loudness: inside / outside, air, distance
        }
        static const char* kCrash[] = {"crash_ext", "crash_int", "leg_break_ext", "leg_break_int", "petal_snap_ext", "petal_snap_int"};
        for (int i = 0; i < 6; ++i) {
            std::snprintf(path, sizeof path, "XRSound\\Tantra\\%s.wav", kCrash[i]);
            sound_->LoadWav(SND_CRASH_EXT + i, path, PT::Global);
        }
    }   // the bridge seats' micro-lift (the focus is the person: global)
    // Our own engine sounds replace the stock ones: the cup reflectors do not roar like rockets.
    sound_->SetDefaultSoundEnabled(XRSound::MainEngines, false);
    sound_->SetDefaultSoundEnabled(XRSound::RetroEngines, false);
    sound_->SetDefaultSoundEnabled(XRSound::HoverEngines, false);
    // Attitude motors are ion-trigger pulses, not gas jets.
    sound_->LoadWav(XRSound::RCSSustain, "XRSound\\Tantra\\att_sustain.wav", PT::BothViewClose);
    for (int id = XRSound::RCSAttackPlusX; id <= XRSound::RCSAttackMinusZ; ++id)
        sound_->LoadWav(id, "XRSound\\Tantra\\att_attack.wav", PT::BothViewClose);
}

// --- Persistence ---------------------------------------------------------------

void Tantra::clbkSaveState(FILEHANDLE scn) {
    SaveDefaultState(scn);
    oapiWriteScenario_int(scn, const_cast<char*>("ENGSET"), static_cast<int>(engineSet_));
    oapiWriteScenario_int(scn, const_cast<char*>("IGNITION"), ignition_.SaveValue());
    oapiWriteScenario_int(scn, const_cast<char*>("ACTIVETRAP"), activeTrap_);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d %.2f", gLimitOn_ ? 1 : 0, gLimit_);
    oapiWriteScenario_string(scn, const_cast<char*>("GLIMIT"), buf);
    oapiWriteScenario_int(scn, const_cast<char*>("HOTSTART_OVERRIDE"), hotStartOverride_ ? 1 : 0);
    oapiWriteScenario_float(scn, const_cast<char*>("SHIPTIME"), properTime_);
    {
        char dmg[400];
        damage_.Save(dmg, sizeof dmg);
        oapiWriteScenario_string(scn, const_cast<char*>("DAMAGE"), dmg);
        damage_.SaveCrush(dmg, sizeof dmg);
        oapiWriteScenario_string(scn, const_cast<char*>("CRUSH"), dmg);
        feet_.Save(dmg, sizeof dmg);
        oapiWriteScenario_string(scn, const_cast<char*>("FEET"), dmg);
    }
    oapiWriteScenario_string(scn, const_cast<char*>("PLANT"), const_cast<char*>(plant_.Save().c_str()));
    std::snprintf(buf, sizeof buf, "%.1f %.1f %.3f %d", podAngle_, podTarget_, podOut_, podsWanted_ ? 1 : 0);
    oapiWriteScenario_string(scn, const_cast<char*>("PODS"), buf);
    if (!lift_.Stowed() || lift_.Lowering()) lift_.Save(scn);   // LIFT door out mast down lowering
    {
        double p, pT, g, gT;
        int set, port;
        carriage_.Save(p, pT, g, gT, set, port);
        char cb[128];
        std::snprintf(cb, sizeof cb, "%.4f %.1f %.3f %.0f %d %d", p, pT, g, gT, set, port);
        oapiWriteScenario_string(scn, const_cast<char*>("CARRIAGE"), cb);
        std::snprintf(cb, sizeof cb, "%d %.2f %.0f %.2f %.0f", crestsFolded_ ? 1 : 0, hangar_, hangarT_, rovers_, roversT_);
        oapiWriteScenario_string(scn, const_cast<char*>("DECK"), cb);
        oapiWriteScenario_int(scn, const_cast<char*>("WINGS"), wingMode_);
        std::snprintf(cb, sizeof cb, "%d %d %d %d", trapPresent_[0], trapPresent_[1], trapPresent_[2], trapPresent_[3]);
        oapiWriteScenario_string(scn, const_cast<char*>("TRAPSLOTS"), cb);
    }
    oapiWriteScenario_float(scn, const_cast<char*>("FIELDSTORE"), drive_.StoreFraction());
    crew_.Save(scn);
}

void Tantra::clbkLoadStateEx(FILEHANDLE scn, void* status) {
    char* line;
    while (oapiReadScenario_nextline(scn, line)) {
        if (crew_.LoadLine(line)) continue;
        int i = 0, on = 0;
        double d = 0.0;
        if (!_strnicmp(line, "ENGSET", 6) && std::sscanf(line + 6, "%d", &i) == 1) {
            engineSet_ = i ? EngineSet::Anamezon : EngineSet::Planetary;
        } else if (!_strnicmp(line, "IGNITION", 8) && std::sscanf(line + 8, "%d", &i) == 1) {
            ignition_.Load(i);
        } else if (!_strnicmp(line, "ACTIVETRAP", 10) && std::sscanf(line + 10, "%d", &i) == 1) {
            activeTrap_ = i;
        } else if (!_strnicmp(line, "GLIMIT", 6) && std::sscanf(line + 6, "%d %lf", &on, &d) == 2) {
            gLimitOn_ = on != 0;
            gLimit_ = d;
        } else if (!_strnicmp(line, "HOTSTART_OVERRIDE", 17) && std::sscanf(line + 17, "%d", &i) == 1) {
            hotStartOverride_ = i != 0;
        } else if (!_strnicmp(line, "SHIPTIME", 8) && std::sscanf(line + 8, "%lf", &d) == 1) {
            properTime_ = d;
        } else if (!_strnicmp(line, "PODS", 4) && std::sscanf(line + 4, "%lf %lf", &d, &podTarget_) >= 1) {
            podAngle_ = d;
            double out = 0.0;
            int want = 0;
            if (std::sscanf(line + 4, "%*lf %*lf %lf %d", &out, &want) == 2) {
                podOut_ = out;
                podsWanted_ = want != 0;
            } else {
                podOut_ = podAngle_ > 1.0 ? 1.0 : 0.0;  // older scenarios: swivelled pods are out
                podsWanted_ = podOut_ > 0.5;
            }
        } else if (!_strnicmp(line, "LIFT", 4) && (line[4] == ' ' || line[4] == '\t')) {
            lift_.Load(line + 4);
        } else if (!_strnicmp(line, "PLANT", 5) && (line[5] == ' ' || line[5] == '\t')) {
            plant_.Load(line + 5);
        } else if (!_strnicmp(line, "FEET", 4)) {
            feet_.Load(line + 4);
        } else if (!_strnicmp(line, "CRUSH", 5)) {
            damage_.LoadCrush(line + 5);
        } else if (!_strnicmp(line, "DAMAGE", 6)) {
            damage_.Load(line + 6);
            shipGone_ = damage_.Destroyed();  // the debris of a saved break-up are vessels of their own
        } else if (!_strnicmp(line, "FIELDSTORE", 10) && std::sscanf(line + 10, "%lf", &d) == 1) {
            drive_.SetStoreFraction(d);
        } else if (!_strnicmp(line, "CARRIAGE", 8)) {
            double p = 0, pT = 0, g = 1, gT = 1;
            int set = 0, port = 0;
            std::sscanf(line + 8, "%lf %lf %lf %lf %d %d", &p, &pT, &g, &gT, &set, &port);
            carriage_.Load(p, pT, g, gT, set, port);
        } else if (!_strnicmp(line, "TRAPSLOTS", 9)) {
            int t[4] = {1, 1, 1, 1};
            std::sscanf(line + 9, "%d %d %d %d", &t[0], &t[1], &t[2], &t[3]);
            for (int i = 0; i < 4; ++i) trapPresent_[i] = t[i] != 0;
            UpdateEmptyMass();
        } else if (!_strnicmp(line, "DECK", 4)) {
            int folded = 0;
            std::sscanf(line + 4, "%d %lf %lf %lf %lf", &folded, &hangar_, &hangarT_, &rovers_, &roversT_);
            crestsFolded_ = folded != 0;
            if (crestsFolded_) wingMode_ = 2;
        } else if (!_strnicmp(line, "WINGS", 5)) {
            std::sscanf(line + 5, "%d", &wingMode_);
            wingMode_ = (std::max)(0, (std::min)(2, wingMode_));
            crestsFolded_ = wingMode_ == 2;
        } else {
            ParseScenarioLineEx(line, status);
        }
    }
    BindMainGroup(engineSet_);
    UpdatePods(0.0);
    SelectActiveTrap();
    carriage_.Update(0.0, frameS_);
    tuck_ = crestsFolded_ ? 1.0 : 0.0;
    wingOut_ = wingMode_ == 2 ? 1.0 : 0.0;
    wingIn_ = wingMode_ == 2 ? 1.0 : (wingMode_ == 1 ? tantra::mesh::kWingRaiseDeg / tantra::mesh::kWingFoldDeg : 0.0);
    UpdateGear(0.0);
}

// --- Simulation ----------------------------------------------------------------

void Tantra::BindMainGroup(EngineSet set) {
    engineSet_ = set;
    planGroup_ = -1;  // force a rebind
    RebindGroups();
}

// Anamezon chambers take the main throttle only while they are selected AND feeding:
// a chamber that is off, or still raising its field and beam, gives no thrust at all.
// Otherwise the central planetary ring in the stern is the main engine. The auxiliary pods
// are hover engines while swivelled down (>= 45 deg) and not sunk into the shoulder.
bool Tantra::AnaIsMain() const { return engineSet_ == EngineSet::Anamezon && ignition_.FeedAvailable(); }

void Tantra::RebindGroups() {
    const bool anaMain = AnaIsMain();
    const int want = anaMain ? 0 : 1;
    const bool hover = podOut_ >= 1.0 && podAngle_ >= 45.0 && podAngle_ <= 135.0 && !podAssist_ && podAimed_;
    const bool mainIsAna = GetGroupThrusterCount(THGROUP_MAIN) == sp::kAnaCount;
    if (want != planGroup_ || anaMain != mainIsAna) {
        DelThrusterGroup(THGROUP_MAIN);
        for (THRUSTER_HANDLE th : ana_) SetThrusterLevel(th, 0.0);  // (re)start from zero feed
        SetThrusterLevel(march_, 0.0);
        if (anaMain) CreateThrusterGroup(ana_, sp::kAnaCount, THGROUP_MAIN);
        else CreateThrusterGroup(&march_, 1, THGROUP_MAIN);
        // the nose retro cups brake only while the anamezon drive feeds
        DelThrusterGroup(THGROUP_RETRO);
        for (THRUSTER_HANDLE th : retro_) SetThrusterLevel(th, 0.0);
        if (anaMain) CreateThrusterGroup(retro_, sp::kRetroCount, THGROUP_RETRO);
        planGroup_ = want;
    }
    if (hover != podHover_) {
        DelThrusterGroup(THGROUP_HOVER);
        for (THRUSTER_HANDLE th : pod_) SetThrusterLevel(th, 0.0);
        if (hover) CreateThrusterGroup(pod_, sp::kPodCups, THGROUP_HOVER);
        podHover_ = hover;
    }
}

// Pods: the telescopic arm runs the pod out of its flank bay (0..1 in kPodSwingTime), the pod turns about the arm
// axis only while fully out (0 cups aft .. 90 down .. 180 forward), and comes in only with the cups aft. The bays
// stay shut above Mach 0.8 and while the ship is being stood up. Cup positions follow the mesh rig exactly; the jets
// splay 15 deg out and 25 deg down away from the hull.
void Tantra::UpdatePods(double dt) {
    namespace m = tantra::mesh;
    const bool fast = GetAtmDensity() > 1e-6 && GetMachNumber() > sp::kPodMaxMach;
    // Standing up / laying down: the pods come out and hold their thrust world-vertical (cups swivel by 90 deg
    // minus the ship's pitch); the crests fold, the pods stay out (tantra_c148.html, «Подъём на корму»).
    const double P = carriage_.Progress();
    // Only while the carriage actually lifts, turns or lowers the ship (not while the crests fold, not once
    // it stands on the stern legs).
    // Out and aimed already while the crests fold (P 0..1), thrust only while the carriage lifts / turns.
    const bool assistPrep = sp::kPodAssistOn && !fast && tuck_ < 0.5 && GroundContact() && carriage_.Busy() && P > 0.0 && P < 4.9;
    podAssist_ = assistPrep && P > 1.0;
    const bool allowed = !fast && tuck_ < 0.5 && (assistPrep || carriage_.Pose().tuck < 0.5);
    if (podsWanted_ && fast && podOut_ < 1.0 && !podMachWarned_) {
        Message("Гондолы: створки откроются ниже М %.1f", "Pods: the doors open below Mach %.1f", sp::kPodMaxMach);
        podMachWarned_ = true;
    }
    if (!fast) podMachWarned_ = false;
    const bool out = (podsWanted_ || assistPrep) && allowed;
    const double assistSwivel = (std::max)(0.0, (std::min)(sp::kPodSwivelMaxDeg, 90.0 - carriage_.Pose().theta * DEG));
    const double swivelTarget = out && podOut_ >= 1.0 ? (assistPrep ? assistSwivel : podTarget_) : 0.0;
    podAimed_ = podOut_ >= 1.0 && std::fabs(podAngle_ - swivelTarget) < 3.0;  // doors out, cups on their angle
    const double step = sp::kPodSwivelRate * dt;
    if (podAngle_ < swivelTarget) podAngle_ = (std::min)(swivelTarget, podAngle_ + step);
    else if (podAngle_ > swivelTarget) podAngle_ = (std::max)(swivelTarget, podAngle_ - step);
    const double sw = dt / sp::kPodSwingTime;
    if (out) podOut_ = (std::min)(1.0, podOut_ + sw);
    else if (podAngle_ < 0.5) podOut_ = (std::max)(0.0, podOut_ - sw);

    const VECTOR3 dz = _V(0, 0, MeshDZ());
    const double swivel = podAngle_ / sp::kPodSwivelMaxDeg * m::kPodSwivelMax;
    for (int p = 0; p < sp::kPodCount; ++p) {
        const m::PodRig& r = m::kPods[p];
        const VECTOR3 pivot = V3(r.pivot), ax = V3(r.axis);
        const double sgn = pivot.x >= 0.0 ? 1.0 : -1.0;
        const VECTOR3 out = ax * (r.travel * podOut_);
        // thrust (on the ship) at 0: forward, leaning inboard and up - the jet splays out and down from the hull
        const double so = sp::kPodSplayOutDeg * RAD, sd = sp::kPodSplayDownDeg * RAD;
        const VECTOR3 base = _V(-sgn * std::sin(so) * std::cos(sd), std::sin(sd), std::cos(so) * std::cos(sd));
        const VECTOR3 dir = RotateAbout(base, ax * sgn, swivel);
        for (int c = 0; c < sp::kCupsPerPod; ++c) {
            const int k = p * sp::kCupsPerPod + c;
            podExhPos_[k] = RotateAbout(V3(r.cup[c]) - pivot, ax * sgn, swivel) + pivot + out + dz;
            podExhDir_[k] = dir;
            SetThrusterRef(pod_[k], podExhPos_[k]);
            SetThrusterDir(pod_[k], dir);
        }
    }
    // Pitch: the thrust centre of the two pairs sits on the CG.
    const double sA = m::kPods[0].s, sF = m::kPods[2].s;
    const double fore = (std::max)(0.0, (std::min)(1.0, (frameS_ - sA) / (sF - sA)));
    const double big = (std::max)(fore, 1.0 - fore);
    podShare_[0] = (1.0 - fore) / big;
    podShare_[1] = fore / big;
    RebindGroups();
}

void Tantra::ActPods(bool hover) {
    const bool outNow = podsWanted_ && podTarget_ >= 45.0;
    if (hover && outNow) {  // B again: cups aft, then in
        podsWanted_ = false;
        podTarget_ = 0.0;
        Message("Гондолы: чаши назад, в отсеки", "Pods: cups aft, into the bays");
        return;
    }
    podsWanted_ = true;
    podTarget_ = hover ? 90.0 : 0.0;
    Message(hover ? "Гондолы: наружу, чаши вниз (висение)" : "Гондолы: наружу, чаши назад (тяга вперёд)",
            hover ? "Pods: out, cups down (hover)" : "Pods: out, cups aft (forward thrust)");
}

void Tantra::ActPodsTo(double deg) {
    podTarget_ = (std::max)(0.0, (std::min)(sp::kPodSwivelMaxDeg, deg));
    if (podTarget_ > 0.0) podsWanted_ = true;
    Message("Гондолы планетарных: поворот на %.0f°", "Planetary pods: swivel to %.0f deg", podTarget_);
}

// Wind near the ground. The ship measures it (air data: groundspeed - airspeed); with Orbiter's wind on, its
// aerodynamics apply the force and we only read it; with Orbiter's wind off (or zero), our own gust model
// pushes the broadside. The ship sways on its elastic contacts; thrusting pods lean their jets (<= 7 deg)
// against the wind force and the sway velocity. HUD: wind, CG sway, compensation, leg loads.
void Tantra::UpdateWind(double dt) {
    windForce_ = podSideForce_ = windSpeed_ = 0.0;
    const double rho = GetAtmDensity();
    if (rho < 0.01 || GetAltitude(ALTMODE_GROUND) > 1000.0 || dt <= 0.0) {
        gust_ = sway_ = 0.0;
        windFh_ = _V(0, 0, 0);
        return;
    }
    VECTOR3 up;                                  // world up in the ship frame
    HorizonInvRot(_V(0, 1, 0), up);
    auto horiz = [&up](const VECTOR3& v) { return v - up * dotp(v, up); };
    // Air data: the air's velocity over the ground = groundspeed - airspeed (ship frame).
    VECTOR3 gs, as;
    GetGroundspeedVector(FRAME_LOCAL, gs);
    GetAirspeedVector(FRAME_LOCAL, as);
    const VECTOR3 wind = horiz(gs - as);
    windSpeed_ = length(wind);
    orbiterWind_ = orbiterWind_ || windSpeed_ > 0.3;
    VECTOR3 fh = _V(0, 0, 0);                    // horizontal wind force on the ship
    if (orbiterWind_) {
        VECTOR3 d, l;
        GetDragVector(d);
        GetLiftVector(l);
        fh = horiz(d + l);                       // Orbiter's aerodynamics already apply it
        windForce_ = length(fh);
    } else {
        auto uniform = [this]() {
            windRng_ = windRng_ * 1664525u + 1013904223u;
            return (windRng_ >> 8) * (1.0 / 16777216.0);
        };
        const double n = std::sqrt(-2.0 * std::log((std::max)(1e-9, uniform()))) * std::cos(2.0 * PI * uniform());
        const double hh = (std::min)(dt, 0.1);
        gust_ += -gust_ / sp::kGustTau * hh + sp::kGustSigma * std::sqrt(2.0 * hh / sp::kGustTau) * n;
        windSpeed_ = sp::kWindMean + gust_;
        windForce_ = 0.5 * rho * windSpeed_ * std::fabs(windSpeed_) * sp::kSideCd * sp::kSideArea;
        fh = horiz(_V(1, 0, 0)) * windForce_;    // broadside
        // standing still on its legs the ship does not feel it (< 0.1 % of the friction): no force, it rests
        if (!GroundContact() || carriage_.Busy()) { AddForce(fh, _V(0, 0, 0)); forceFrame_ = frame_; }
    }
    // Sway: the CG off its rest point, horizontally (ground contact frame).
    VECTOR3 vh = horiz(gs);
    sway_ = std::asin((std::max)(-1.0, (std::min)(1.0, up.x))) * carriage_.Pose().trunnionH;
    swayMaxT_ -= dt;
    if (std::fabs(sway_) > swayMax_ || swayMaxT_ <= 0.0) {
        swayMax_ = std::fabs(sway_);
        swayMaxT_ = 10.0;
    }
    windFh_ = fh;                                // the pods answer it with a moment (PodAssistLevels)
}

// Loads on the most loaded leg with the wind: weight less the pods' lift, plus the overturning moment of
// the horizontal wind force at the CG height, against the rating (strength and buckling, x1.5).
void Tantra::UpdateLegLoads(const VECTOR3& fh) {
    const tantra::CarriagePose& p = carriage_.Pose();
    namespace lg = tantra::legs;
    legLoad_ = legRatio_ = tipWind_ = colRatio_ = sternRatio_ = 0.0;
    for (int l = 0; l < lg::kLegCount; ++l) legN_[l] = legR_[l] = 0.0;
    if (!GroundContact() || carriage_.Gear() <= 0.0) {
        hipCatcher_ = false;
        return;
    }
    VECTOR3 up;
    HorizonInvRot(_V(0, 1, 0), up);
    double lift = 0.0;
    for (THRUSTER_HANDLE th : pod_) {
        VECTOR3 d;
        GetThrusterDir(th, d);
        lift += GetThrusterLevel(th) * GetThrusterMax0(th) * dotp(d, up);
    }
    const double W = (std::max)(0.0, GetMass() * LocalG() - lift);
    const double h = p.trunnionH;
    const double rho = GetAtmDensity();
    auto pcr = [](double I, double L) { return PI * PI * sp::kCntE * I / (L * L); };
    const double colShare = p.columnShare, sternShare = 1.0 - colShare;
    // Fibre strain gauges in the stages: the load of every leg - weight share plus the overturning moment of the
    // wind force (ship frame: across the blade legs, in the plane of the stern feet) at the CG height.
    lg::LegLoadInput in;
    in.weight = W;
    in.columnShare = colShare;
    in.kangShare = p.kangShare;
    in.h = h;
    in.windSide = fh.x;
    in.windX = fh.x;
    in.windY = fh.y;
    in.hipX = tantra::mesh::kHipXOut;
    for (int i = 0; i < 4; ++i) {
        in.feetX[i] = carriage_.Geometry().standFoot[i].x;
        in.feetY[i] = carriage_.Geometry().standFoot[i].y;
    }
    in.cosSplay = std::cos(16.0 * RAD);
    lg::LegLoads(in, legN_);
    // Ratings: blades as columns of their current length (weak axis), stern legs at full length, the kangaroo
    // thigh + shin as one column.
    const double capB = (std::min)(sp::kCntSigma * sp::kBladeA, pcr(sp::kBladeI, (std::max)(30.0, p.mastLen))) / sp::kSafety;
    const double capS = (std::min)(sp::kCntSigma * sp::kSternShinA, pcr(sp::kSternShinI, sp::kSternLegL)) / sp::kSafety;
    const tantra::CarriageGeometry& g = carriage_.Geometry();
    const double kangL = g.kangThigh + g.kangShinMin + p.kangExt * (g.kangShinMax - g.kangShinMin);
    const double capK = (std::min)(sp::kCntSigma * sp::kKangShinA, pcr(sp::kKangShinI, (std::max)(20.0, kangL))) / sp::kSafety;
    static const char* kNameRu[7] = {"лопасть левая", "лопасть правая", "кормовая нога 1", "кормовая нога 2",
                                     "кормовая нога 3", "кормовая нога 4", "нога-кенгуру"};
    static const char* kNameEn[7] = {"port blade", "starboard blade", "stern leg 1", "stern leg 2",
                                     "stern leg 3", "stern leg 4", "kangaroo leg"};
    int worstLeg = 0;
    for (int l = 0; l < lg::kLegCount; ++l) {
        legR_[l] = legN_[l] / (l < 2 ? capB : (l == lg::kKangaroo ? capK : capS));
        if (legR_[l] > legR_[worstLeg]) worstLeg = l;
        if (legR_[l] >= lg::kAlarm && !legAlarm_[l])
            Message("Датчики: %s - %.0f%% допуска, на грани излома", "Sensors: %s at %.0f%% of rating, close to buckling",
                    russian_ ? kNameRu[l] : kNameEn[l], 100.0 * legR_[l]);
        legAlarm_[l] = legR_[l] >= lg::kAlarm || (legAlarm_[l] && legR_[l] > lg::kAlarm - 0.05);
    }
    colRatio_ = (std::max)((std::max)(legR_[0], legR_[1]), legR_[lg::kKangaroo]);
    sternRatio_ = (std::max)((std::max)(legR_[2], legR_[3]), (std::max)(legR_[4], legR_[5]));
    legLoad_ = legN_[worstLeg];
    legRatio_ = legR_[worstLeg];
    legName_ = russian_ ? kNameRu[worstLeg] : kNameEn[worstLeg];
    // Hip magnetic bearings (10 T): above their capacity the drums sit on the sliding catchers.
    const bool catcher = colShare > 0.0 && (std::max)(legN_[0], legN_[1]) > lg::BearingCapacity(lg::kHipBearingArea);
    if (catcher != hipCatcher_)
        Message(catcher ? "Цапфы: магнитные подшипники перегружены - на страховочных, привод медленнее"
                        : "Цапфы: снова на магнитной подвеске",
                catcher ? "Trunnions: magnetic bearings over capacity - on the catchers, drives slower"
                        : "Trunnions: back on the magnetic bearings");
    hipCatcher_ = catcher;
    // Overturning: lying - the tripod (blade feet at +-19 across, kangaroo foot ahead); turning - the blade feet
    // alone (umbrella radius fore-aft); standing - the stern feet polygon.
    double tipF = 1e30;
    if (colShare > 0.0) {
        const double arm = p.tripod ? (std::min)(tantra::mesh::kHipXOut, g.kangHipS + g.kangFootFwd - frameS_) : (std::min)(tantra::mesh::kHipXOut, g.footR);
        tipF = (std::min)(tipF, colShare * W * arm / (std::max)(1.0, h));
    }
    if (sternShare > 0.0) tipF = (std::min)(tipF, sternShare * W * sp::kStandInradius / (std::max)(1.0, h));
    if (rho > 0.0 && tipF < 1e29) tipWind_ = std::sqrt(2.0 * tipF / (rho * sp::kSideCd * sp::kSideArea));
}

// Pods helping the carriage: every cup's thrust is world-vertical; its moment about the CG is r x up T.
// The levels are set so that the pods' total moment equals the one wanted: nothing of their own, minus the
// wind's overturning moment (force at the CG height), minus a damping of the sway rate. Borne out of
// differential thrust - port against starboard, fore against aft - never a sideways lean of the jets (the
// pods hang 9.8 m under the axis: a lean would itself be a moment). When the attitude leaves no balance
// (all pods on one side of the CG as the ship comes upright), the thrust fades out: the legs carry all.
void Tantra::PodAssistLevels(double simdt, double podMax) {
    const int n = sp::kPodCups;
    const double target = podAssist_ && podAimed_ && podMax > 0.0
                              ? (std::min)(1.0, sp::kPodAssistShare * GetMass() * LocalG() / podMax) : 0.0;
    const double was = podAssistLevel_;
    podAssistLevel_ += (std::max)(-0.2 * simdt, (std::min)(0.2 * simdt, target - podAssistLevel_));
    podCouple_ = 0.0;
    if (podAssistLevel_ <= 0.0) {
        if (was > 0.0)
            for (THRUSTER_HANDLE th : pod_) SetThrusterLevel(th, 0.0);
        podBalanceLost_ = false;
        return;
    }
    VECTOR3 up;
    HorizonInvRot(_V(0, 1, 0), up);
    VECTOR3 a[sp::kPodCups];
    for (int k = 0; k < n; ++k) {
        VECTOR3 r;
        GetThrusterRef(pod_[k], r);
        a[k] = crossp(up, r) * (GetThrusterMax0(pod_[k]) * torqueSign_);  // moment per unit level (F x r)
    }
    VECTOR3 w;
    GetAngularVel(w);
    w = w - up * dotp(w, up);
    const double h = carriage_.Pose().trunnionH;
    // Ground reaction -fh at -up*h (F x r) is h*(fh x up); the pods answer it, and damp the rate (in Orbiter's
    // own convention for torque and angular velocity). Compensation is dropped while the ship turns faster
    // than 0.3 rad/s: then the pods only keep their own moment at zero, which needs no sign at all.
    if (length(w) > 0.3) compOff_ = 5.0;
    VECTOR3 want = _V(0, 0, 0);
    if (compOff_ <= 0.0) want = crossp(up, windFh_) * (h * torqueSign_) - w * (GetMass() * sp::kPmiPitch * 0.6);
    // Least change of each cup's level from the common level L0 that gives exactly the wanted moment,
    // cups at their limits (0 or 1) held there (active set). No solution: lower L0; none at all: off.
    auto allocate = [&](double L0, double* lv) {
        bool fixed[sp::kPodCups] = {};
        for (int k = 0; k < n; ++k) lv[k] = L0;
        for (int it = 0; it <= n; ++it) {
            double M[3][3] = {}, r[3];
            VECTOR3 rhs = want;
            int free = 0;
            for (int k = 0; k < n; ++k) {
                rhs = rhs - a[k] * lv[k];
                if (fixed[k]) continue;
                ++free;
                const double v[3] = {a[k].x, a[k].y, a[k].z};
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) M[i][j] += v[i] * v[j];
            }
            if (!free) break;
            const double eps = 1e-9 * (M[0][0] + M[1][1] + M[2][2]);
            for (int i = 0; i < 3; ++i) M[i][i] += eps;
            r[0] = rhs.x, r[1] = rhs.y, r[2] = rhs.z;
            // 3x3 solve (Cramer)
            auto det = [](double m[3][3]) {
                return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
            };
            const double d = det(M);
            if (std::fabs(d) < 1e-300) break;
            double x[3];
            for (int c = 0; c < 3; ++c) {
                double t[3][3];
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) t[i][j] = j == c ? r[i] : M[i][j];
                x[c] = det(t) / d;
            }
            bool bad = false;
            for (int k = 0; k < n; ++k) {
                if (fixed[k]) continue;
                lv[k] += a[k].x * x[0] + a[k].y * x[1] + a[k].z * x[2];
                if (lv[k] < 0.0 || lv[k] > 1.0) {
                    lv[k] = (std::max)(0.0, (std::min)(1.0, lv[k]));
                    fixed[k] = bad = true;
                }
            }
            if (!bad) break;
        }
        VECTOR3 got = _V(0, 0, 0);
        for (int k = 0; k < n; ++k) got = got + a[k] * lv[k];
        return length(got - want) <= 0.05 * length(want) + 1e5;
    };
    double L0 = podAssistLevel_, lv[sp::kPodCups] = {};
    bool ok = false;
    for (int it = 0; it < 40 && !ok; ++it, L0 *= 0.85) ok = allocate(L0, lv);
    podBalanceLost_ = !ok;
    for (int k = 0; k < n; ++k) SetThrusterLevel(pod_[k], ok ? lv[k] : 0.0);
    podTorquePred_ = _V(0, 0, 0);
    if (ok)
        for (int k = 0; k < n; ++k) {
            VECTOR3 r;
            GetThrusterRef(pod_[k], r);
            podTorquePred_ = podTorquePred_ + crossp(up * (lv[k] * GetThrusterMax0(pod_[k]) * torqueSign_), r);  // believed convention
        }
    if (ok) podCouple_ = length(want);
    else podAssistLevel_ = (std::max)(0.0, podAssistLevel_ - 0.5 * simdt);  // fade out, the legs take it
}

// Mass budget -> CG station; the vessel frame follows it in steps of 0.2 m. ShiftCG moves the meshes,
// thrusters, attachments, lights and the camera; the airfoils, control surfaces, exhaust positions and
// touchdown points are ours to move.
void Tantra::UpdateCG(bool force) {
    double m = prm_.dryMass, ms = prm_.dryMass * sp::kDryCGS;
    for (int i = 0; i < sp::kTrapCount; ++i) {
        const double t = (trapPresent_[i] ? prm_.trapStructMass : 0.0) + GetPropellantMass(trap_[i]);
        m += t;
        ms += t * sp::kTrapCGS;
    }
    const double iron = GetPropellantMass(iron_), argon = GetPropellantMass(argon_);
    m += iron + argon;
    ms += iron * sp::kIronCGS;
    // the body tanks (aft) drain first: the insert keeps its share until the body is empty
    const double body = (std::min)(argon, sp::kArgonBodyShare * prm_.argonMass);
    ms += body * sp::kArgonBodyCGS + (argon - body) * sp::kArgonInsertCGS;
    const double s = ms / m, d = s - frameS_;
    if (!force && std::fabs(d) < 0.2) return;
    if (std::fabs(d) < 1e-6) return;
    ShiftCG(_V(0, 0, d));
    frameS_ = s;
    if (exhaust_) exhaust_->Shift(d);
    for (int i = 0; i < 5; ++i)
        if (foil_[i]) EditAirfoil(foil_[i], 0x01, _V(0, kAeroY[i], Zf(kAeroS[i])), nullptr, 0, 0, 0);
    if (ctrlOn_) {
        DefineControlSurfaces(false);
        DefineControlSurfaces(true);
    }
    nTouch_ = -1;  // touchdown points are rebuilt for the new frame
}

// Reaction mass of the planetary cups: argon below kMarchArgonAlt (inert, the jet/air mixing layer stays cool), iron
// above it and between the planets. The thrust stays; the Isp (and so the flow) follows the mass.
void Tantra::UpdateReactionMass() {
    const bool high = GetAtmDensity() < 1e-6 || GetAltitude(ALTMODE_GROUND) > sp::kMarchArgonAlt;
    const bool argonLeft = GetPropellantMass(argon_) > 1.0, ironLeft = GetPropellantMass(iron_) > 1.0;
    const bool useIron = (high && ironLeft) || !argonLeft;
    if (useIron == marchHigh_ && GetThrusterResource(march_) != nullptr) return;
    marchHigh_ = useIron;
    PROPELLANT_HANDLE ph = useIron ? iron_ : argon_;
    SetThrusterResource(march_, ph);
    for (THRUSTER_HANDLE th : pod_) SetThrusterResource(th, ph);
    for (THRUSTER_HANDLE th : attCouple_) SetThrusterResource(th, ph);
    for (THRUSTER_HANDLE th : attRoll_) SetThrusterResource(th, ph);
    for (THRUSTER_HANDLE th : attAxial_) SetThrusterResource(th, ph);
}

// The stern chambers and the ring steer their jets by the field of the magnetic nozzle so that the
// thrust line passes through the CG (the well sits 2.2 m over the axis; УВТ, no hinges).
void Tantra::AimThroughCG() {
    const double lim = std::cos(sp::kTvcMaxDeg * RAD);
    auto aim = [&](THRUSTER_HANDLE th) {
        VECTOR3 p;
        GetThrusterRef(th, p);
        VECTOR3 d = _V(-p.x, -p.y, (std::max)(1.0, -p.z));
        d = d / length(d);
        if (d.z < lim) {  // clamp to the nozzle's reach
            const double r = std::sqrt(d.x * d.x + d.y * d.y), k = std::sqrt(1.0 - lim * lim) / r;
            d = _V(d.x * k, d.y * k, lim);
        }
        SetThrusterDir(th, d);
    };
    for (THRUSTER_HANDLE th : ana_) aim(th);
    aim(march_);
}

void Tantra::SelectActiveTrap() {
    if (activeTrap_ < 0 || activeTrap_ >= sp::kTrapCount) activeTrap_ = 0;
    if (GetPropellantMass(trap_[activeTrap_]) > 0.0) {
        for (THRUSTER_HANDLE th : ana_) SetThrusterResource(th, trap_[activeTrap_]);
        return;
    }
    for (int k = 1; k <= sp::kTrapCount; ++k) {
        const int j = (activeTrap_ + k) % sp::kTrapCount;
        if (GetPropellantMass(trap_[j]) > 0.0) {
            const int empty = activeTrap_;
            activeTrap_ = j;
            for (THRUSTER_HANDLE th : ana_) SetThrusterResource(th, trap_[j]);
            Message("Ловушка %d пуста, подача из ловушки %d", "Trap %d empty, feeding from trap %d", empty + 1,
                    j + 1);
            return;
        }
    }
}

// Hot anamezon start deep in an atmosphere burns a strip of ground and can wreck
// the ship's own stern. The safe altitude grows with jet power (design study):
// h_min = 50 km + 10 km * lg(P / 1.2e17 W). Returns the allowed feed level.
double Tantra::HotStartLevelCap() const {
    if (hotStartOverride_ || GetAtmDensity() <= 0.0) return 1.0;
    const double full = drive_.JetPower(1.0);
    if (full <= 0.0) return 1.0;
    return (std::min)(1.0, prm_.HotStartPowerLimit(GetAltitude()) / full);
}

void Tantra::ApplyThrottleLimits() {
    if (gLimitOn_ && podHover_) {  // pods in hover
        const double cap = GLimitLevel(gLimit_ * G0, GetMass(), prm_.podThrustTotal * RocketFactor(beta_));
        if (GetThrusterGroupLevel(THGROUP_HOVER) > cap) SetThrusterGroupLevel(THGROUP_HOVER, cap);
    }
    double level = GetThrusterGroupLevel(THGROUP_MAIN);
    if (level <= 0.0) {
        hotStartWarned_ = false;
        safetyWarned_ = false;
        return;
    }
    if (!AnaIsMain() && planGroup_ != 1) return;
    const double f = RocketFactor(beta_);
    double maxThrust;
    if (AnaIsMain()) {
        if (!ignition_.FeedAvailable()) {
            SetThrusterGroupLevel(THGROUP_MAIN, 0.0);
            return;
        }
        const double radCap = SafetyLevelCap();
        if (level > radCap) {
            SetThrusterGroupLevel(THGROUP_MAIN, radCap);
            level = radCap;
            if (!safetyWarned_) {
                char ru[256], en[256];
                std::snprintf(ru, sizeof ru, "БЛОКИРОВКА РАДИАЦИИ: %s (Ctrl+J - снять под вашу ответственность)",
                              safety_->CapReasonRu().c_str());
                std::snprintf(en, sizeof en, "RADIATION INTERLOCK: %s (Ctrl+J to override at your own risk)",
                              safety_->CapReasonEn().c_str());
                Message("%s", "%s", ru, en);
                safetyWarned_ = true;
            }
            if (level <= 0.0) return;
        }
        const double hotCap = HotStartLevelCap();
        if (level > hotCap) {
            SetThrusterGroupLevel(THGROUP_MAIN, hotCap);
            level = hotCap;
            if (!hotStartWarned_) {
                Message("БЛОКИРОВКА: для полной тяги нужно %.0f км (Ctrl+J - снять)", "INTERLOCK: full thrust needs %.0f km (Ctrl+J to override)",
                        prm_.HotStartMinAltitude(drive_.JetPower(1.0)) / 1e3);
                hotStartWarned_ = true;
            }
            if (level <= 0.0) return;
        }
        if (gLimitOn_) {
            // The compensator cancels most of the thrust load; limit what it cannot.
            const double cap = drive_.LevelForResidual(gLimit_ * G0, GetMass() / f, ignition_.FieldLevel());
            if (level > cap) SetThrusterGroupLevel(THGROUP_MAIN, cap);
        }
        return;
    }
    maxThrust = prm_.marchThrust * f;
    if (gLimitOn_) {
        const double cap = GLimitLevel(gLimit_ * G0, GetMass(), maxThrust);
        if (level > cap) SetThrusterGroupLevel(THGROUP_MAIN, cap);
    }
}

// Watch lighting: dim warm downlights in the corridor, amber in the lift zone. D3D9Client lights each mesh with its four most
// relevant local lights (diffuse and specular, no shadows); the light pools and the corner shadows are painted in the mesh.
void Tantra::WatchLights() {
    using namespace tantra::interior;
    const int n = (std::min)(kWatchLightCount, 4);
    auto spec = [&](int i) -> const WatchLight& { return i < 4 ? kWatchLights[i] : kBridgeWatchLights[i - 4]; };
    auto used = [&](int i) { return i < n || i >= 4; };
    if (!watchLight_[4]) {
        for (int i = 0; i < 6; i++) {
            if (!used(i)) continue;
            const WatchLight& w = spec(i);
            const COLOUR4 dif = {float(w.r), float(w.g), float(w.b), 0.0f};
            const COLOUR4 spec = {float(w.r * 0.8), float(w.g * 0.8), float(w.b * 0.8), 0.0f};
            const COLOUR4 amb = {0.0f, 0.0f, 0.0f, 0.0f};
            watchLight_[i] = i < 4 ? AddPointLight(_V(w.x, w.y, w.z + MeshDZ()), 7.0, 0.6, 0.3, 0.15, dif, spec, amb)
                                   : AddPointLight(_V(w.x, w.y, w.z + MeshDZ()), 9.0, 0.4, 0.12, 0.05, dif, spec, amb);   // the bridge: lights the people too
            if (watchLight_[i]) watchLight_[i]->Activate(false);
        }
    }
    if (!cabLight_) {
        const COLOUR4 dif = {0.80f, 0.78f, 0.70f, 0.0f}, spec = {0.6f, 0.6f, 0.55f, 0.0f}, amb = {0.0f, 0.0f, 0.0f, 0.0f};
        cabLight_ = AddPointLight(_V(kCabLight[0], kCabLight[1], kCabLight[2] + MeshDZ()), 4.0, 0.5, 0.5, 0.4, dif, spec, amb);
        if (cabLight_) cabLight_->Activate(false);
    }
    const OBJHANDLE f = oapiGetFocusObject();
    const OBJHANDLE body = interior_.ViewerBody();
    const bool inside = oapiCameraInternal() && (f == GetHandle() ? oapiCockpitMode() == COCKPIT_VIRTUAL : body != nullptr);
    bool bridge = f == GetHandle();                              // the ship's own VC: the view from a bridge seat
    if (body) {                                                  // a person walking inside: where is he or she
        VECTOR3 g, l; oapiGetGlobalPos(body, &g); Global2Local(g, l);
        const double dy = l.y - kBridgeAxisY, dz = l.z - MeshDZ() - kBridgeAxisZ;
        bridge = std::fabs(l.x) <= kBridgeHx && dy * dy + dz * dz <= (kBridgeR + 1.2) * (kBridgeR + 1.2);   // the doorway counts as the bridge
    }
    for (int i = 0; i < 6; i++) {
        if (!watchLight_[i]) continue;
        const WatchLight& w = spec(i);
        watchLight_[i]->SetPosition(_V(w.x, w.y, w.z + MeshDZ()));
        const bool on = inside && (bridge ? (i <= 1 || i >= 4) : i < 4);   // 4 at a time: the bridge's 2 + the corridor's 2, or the way's 4
        if (watchLight_[i]->IsActive() != on) watchLight_[i]->Activate(on);
    }
    if (!cabSpot_[0]) {
        const COLOUR4 dif = {1.0f, 0.95f, 0.85f, 0.0f}, spec = {0.8f, 0.8f, 0.75f, 0.0f}, amb = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 2; k++) {
            cabSpot_[k] = AddSpotLight(_V(kCabSpot[k][0], kCabSpot[k][1], kCabSpot[k][2] + MeshDZ()), _V(kCabSpotDir[0], kCabSpotDir[1], kCabSpotDir[2]),
                                       40.0, 1.0, 0.02, 0.0008, 50.0 * RAD, 75.0 * RAD, dif, spec, amb);
            if (cabSpot_[k]) cabSpot_[k]->Activate(false);
        }
    }
    for (int k = 0; k < 2; k++) {
        if (!cabSpot_[k]) continue;
        cabSpot_[k]->SetPosition(_V(kCabSpot[k][0] - tantra::mesh::kLockOut * lift_.Out(), kCabSpot[k][1] - tantra::mesh::kLockDrop * lift_.Down(),
                                    kCabSpot[k][2] + MeshDZ()));
        const bool on = !lift_.Stowed() || lift_.Lowering();
        if (cabSpot_[k]->IsActive() != on) cabSpot_[k]->Activate(on);
    }
    if (cabLight_) {
        cabLight_->SetPosition(_V(kCabLight[0] - tantra::mesh::kLockOut * lift_.Out(), kCabLight[1] - tantra::mesh::kLockDrop * lift_.Down(),
                                  kCabLight[2] + MeshDZ()));
        const bool on = (inside && !bridge) || !lift_.Stowed();
        if (cabLight_->IsActive() != on) cabLight_->Activate(on);
    }
}

// A light at each person inside the hull (the nearest to the camera first): 0.6 m before the face at head height, 1.6 m reach.
void Tantra::PersonLights() {
    struct P { double d; VECTOR3 at; };
    P best[kPersonLights]; int n = 0;
    VECTOR3 cam; oapiCameraGlobalPos(&cam);
    const double dz = MeshDZ();
    for (DWORD i = 0, cnt = oapiGetVesselCount(); i < cnt; i++) {
        OBJHANDLE h = oapiGetVesselByIndex(i);
        if (!h || h == GetHandle()) continue;
        VESSEL* v = oapiGetVesselInterface(h);
        const char* cls = v ? v->GetClassNameA() : nullptr;
        if (!cls || std::strncmp(cls, "OrbiterCrew", 11) != 0) continue;
        VECTOR3 g, p; v->GetGlobalPos(g); Global2Local(g, p);
        const double mz = p.z - dz;                                        // the mesh (interior) frame
        if (std::fabs(p.x) > 7.0 || p.y < -3.0 || p.y > 12.0 || mz < 60.0 || mz > 140.0) continue;   // inside the hull
        VECTOR3 fw; v->GlobalRot(_V(0, 0, 1), fw);                         // the person's facing, in this ship's frame
        MATRIX3 R; GetRotationMatrix(R); fw = tmul(R, fw);
        VECTOR3 up; v->GlobalRot(_V(0, 1, 0), up); up = tmul(R, up);
        const VECTOR3 at = p + up * 1.5 + fw * 0.6;
        const double d = length(g - cam);
        int k = n < kPersonLights ? n++ : -1;
        if (k < 0) { for (int j = 0; j < kPersonLights; j++) if (best[j].d > d && (k < 0 || best[j].d > best[k].d)) k = j; if (k < 0) continue; }
        best[k] = {d, at};
    }
    for (int i = 0; i < kPersonLights; i++) {
        if (i < n) personLightPos_[i] = best[i].at;
        if (!personLight_[i] && i < n) {
            const COLOUR4 dif = {0.9f, 0.88f, 0.82f, 0}, spe = {0.2f, 0.2f, 0.2f, 0}, amb = {0.12f, 0.12f, 0.12f, 0};
            personLight_[i] = AddPointLight(personLightPos_[i], 1.6, 1.0, 0.5, 2.0, dif, spe, amb);
        }
        if (personLight_[i] && i < n) personLight_[i]->SetPosition(personLightPos_[i]);   // follow the person (the emitter keeps a copy)
        if (personLight_[i]) personLight_[i]->Activate(i < n);
    }
}

// The numpad in the commander's seat, classic Orbiter (the focus is on the person, so Orbiter gives the keys to his body; the
// ship reads them itself while he sits at the console and Orbiter's window is in front):
//   Numpad + / -   throttle up / down (below zero the retro, when the anamezon is the main); Ctrl+ full, Ctrl- zero; * cut
//   rotation: 2 / 8 pitch up / down, 4 / 6 yaw left / right, 1 / 3 bank left / right
//   linear:   2 / 8 up / down, 1 / 3 left / right, 6 / 9 forward / back;  Ctrl: 10 % thrust;  / switches rotation / linear
//   Numpad 5       killrot (the user allowed it in the seat)
void Tantra::SeatKeys() {
    auto release = [this]() {
        for (int g = THGROUP_ATT_PITCHUP; g <= THGROUP_ATT_BACK; g++)
            if (seatAttSet_ & (1u << g)) SetThrusterGroupLevel(THGROUP_TYPE(g), 0.0);
        seatAttSet_ = 0;
        if (seatSurf_) { SetControlSurfaceLevel(AIRCTRL_ELEVATOR, 0.0); SetControlSurfaceLevel(AIRCTRL_AILERON, 0.0); seatSurf_ = false; }
    };
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (interior_.TakenSeat() != 0 || !interior_.SeatedPerson() || pid != GetCurrentProcessId()) { release(); seatKeyPrev_ = 0; return; }
    auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    const bool ctrl = held(VK_CONTROL);
    enum { kPlus = 1, kMinus = 2, kStar = 4, kSlash = 8, k5 = 16 };
    const unsigned now = (held(VK_ADD) ? kPlus : 0) | (held(VK_SUBTRACT) ? kMinus : 0) | (held(VK_MULTIPLY) ? kStar : 0) |
                         (held(VK_DIVIDE) ? kSlash : 0) | (held(VK_NUMPAD5) ? k5 : 0);
    const unsigned edge = now & ~seatKeyPrev_;
    seatKeyPrev_ = now;
    {   // the power plant from the commander's seat: Shift + [ ] ; ' L M
        const unsigned pnow = held(VK_SHIFT) && !ctrl ? ((held(VK_OEM_4) ? 1u : 0u) | (held(VK_OEM_6) ? 2u : 0u) | (held(VK_OEM_1) ? 4u : 0u) |
                                                       (held(VK_OEM_7) ? 8u : 0u) | (held('L') ? 16u : 0u) | (held('M') ? 32u : 0u)) : 0u;
        const unsigned pe = pnow & ~plantKeyPrev_;
        plantKeyPrev_ = pnow;
        for (int b = 0; b < 6; ++b) if (pe & (1u << b)) PlantKey(b);
    }
    const double dt = (std::min)(oapiGetSysStep(), 0.1);
    // the throttle: one axis, main above zero and retro below
    const bool retro = GetGroupThrusterCount(THGROUP_RETRO) > 0;
    double thr = GetThrusterGroupLevel(THGROUP_MAIN) - (retro ? GetThrusterGroupLevel(THGROUP_RETRO) : 0.0);
    const double thr0 = thr;
    if (ctrl && (edge & kPlus)) thr = 1.0;
    else if (ctrl && (edge & kMinus)) thr = 0.0;
    else if (!ctrl && (now & kPlus)) thr += .3 * dt;
    else if (!ctrl && (now & kMinus)) thr -= .3 * dt;
    if (edge & kStar) thr = 0.0;
    if (!retro && thr < 0.0) thr = 0.0;
    thr = (std::max)(-1.0, (std::min)(1.0, thr));
    if (thr != thr0) {
        SetThrusterGroupLevel(THGROUP_MAIN, (std::max)(0.0, thr));
        if (retro) SetThrusterGroupLevel(THGROUP_RETRO, (std::max)(0.0, -thr));
    }
    if (edge & kSlash) SetAttitudeMode(GetAttitudeMode() == RCS_LIN ? RCS_ROT : RCS_LIN);
    if (edge & k5) ToggleNav(NAVMODE_KILLROT);
    // the attitude: the groups under the held keys
    struct K { int vk; THGROUP_TYPE rot, lin; };
    static const K kAtt[] = {{VK_NUMPAD2, THGROUP_ATT_PITCHUP, THGROUP_ATT_UP}, {VK_NUMPAD8, THGROUP_ATT_PITCHDOWN, THGROUP_ATT_DOWN},
                             {VK_NUMPAD4, THGROUP_ATT_YAWLEFT, THGROUP_USER}, {VK_NUMPAD6, THGROUP_ATT_YAWRIGHT, THGROUP_ATT_FORWARD},
                             {VK_NUMPAD1, THGROUP_ATT_BANKLEFT, THGROUP_ATT_LEFT}, {VK_NUMPAD3, THGROUP_ATT_BANKRIGHT, THGROUP_ATT_RIGHT},
                             {VK_NUMPAD9, THGROUP_USER, THGROUP_ATT_BACK}};
    const int mode = GetAttitudeMode();
    unsigned set = 0;
    for (const K& k : kAtt) {
        const THGROUP_TYPE g = mode == RCS_LIN ? k.lin : mode == RCS_ROT ? k.rot : THGROUP_USER;
        if (g == THGROUP_USER || !held(k.vk)) continue;
        SetThrusterGroupLevel(g, ctrl ? 0.1 : 1.0);
        set |= 1u << g;
    }
    for (int g = THGROUP_ATT_PITCHUP; g <= THGROUP_ATT_BACK; g++)                  // the keys let go: those groups off
        if ((seatAttSet_ & ~set) & (1u << g)) SetThrusterGroupLevel(THGROUP_TYPE(g), 0.0);
    seatAttSet_ = set;
    // the aerodynamic surfaces: Orbiter feeds the stick only to the focus (the person), so the helm moves them itself;
    // pitch on 2/8, bank on 1/3 (not in the linear RCS mode), the trim on Insert/Delete; the surfaces' own lag smooths it
    const double k = ctrl ? 0.3 : 1.0;
    const bool rot = mode != RCS_LIN;
    const double pitch = rot ? k * ((held(VK_NUMPAD2) ? 1.0 : 0.0) - (held(VK_NUMPAD8) ? 1.0 : 0.0)) : 0.0;
    const double bank = rot ? k * ((held(VK_NUMPAD3) ? 1.0 : 0.0) - (held(VK_NUMPAD1) ? 1.0 : 0.0)) : 0.0;
    SetControlSurfaceLevel(AIRCTRL_ELEVATOR, pitch);
    SetControlSurfaceLevel(AIRCTRL_AILERON, bank);
    seatSurf_ = true;
    const double trimRate = 0.2 * dt;
    if (held(VK_INSERT) || held(VK_DELETE)) {
        const double tr = GetControlSurfaceLevel(AIRCTRL_ELEVATORTRIM) + (held(VK_INSERT) ? trimRate : -trimRate);
        SetControlSurfaceLevel(AIRCTRL_ELEVATORTRIM, (std::max)(-1.0, (std::min)(1.0, tr)));
    }
}

void Tantra::clbkPreStep(double, double simdt, double) {
    interior_.Step(simdt);                        // the moving bridge seats, the deferred stand-up (OrbiterCrew)
    SeatKeys();
    PersonLights();
    if (sound_ && sound_->IsPresent()) {                                // the seats' micro-lift while one moves
        const bool mv = interior_.SeatMoving();
        if (mv && !sound_->IsWavPlaying(SND_SEAT)) sound_->PlayWav(SND_SEAT, true, 0.35f);
        else if (!mv && sound_->IsWavPlaying(SND_SEAT)) sound_->StopWav(SND_SEAT);
    }
    AutoFlightSet();
    screen_.SetViewer(interior_.ViewerBody());   // the bridge screen follows the viewer (gcAPI renders for the focus only)
    WatchLights();
    WatchTerrain();   // first: a refined terrain tile must not bury the pads for even one step
    UpdateCG(false);
    AimThroughCG();
    UpdateWind(simdt);
    {
        namespace dm = tantra::damage;
        const bool crestsOk = !damage_.Lost(dm::kCrestPort) || !damage_.Lost(dm::kCrestStbd);
        DefineControlSurfaces(wingOut_ < 0.5 && carriage_.Pose().tuck < 0.5 && crestsOk && !damage_.Destroyed());
    }
    UpdateControlSurfaces();
    VECTOR3 v;
    GetRelativeVel(GetGravityRef(), v);
    beta_ = length(v) / C_LIGHT;

    const double f = RocketFactor(beta_);
    for (THRUSTER_HANDLE th : ana_) {
        SetThrusterMax0(th, prm_.anaThrust * f);
        SetThrusterIsp(th, prm_.AnaExhaust() * f);
    }
    UpdateReactionMass();
    const double isp = (marchHigh_ ? prm_.ironExhaust : prm_.argonExhaust) * f;
    // the marching cup: its thrust and exhaust speed from the power plant (field, power, limiter, damage); it thrusts
    // only run out past the anamezon rims (interlock: never with the anamezon irises open)
    UpdatePlant(simdt, f);
    for (THRUSTER_HANDLE th : retro_) {  // nose retro cups: anamezon, only while the drive feeds
        SetThrusterMax0(th, AnaIsMain() && irisNose_ >= 0.99 ? prm_.anaThrust * sp::kRetroAreaFrac * f : 0.0);
        SetThrusterIsp(th, prm_.AnaExhaust() * f);
    }
    double podMax = 0.0;
    for (int k = 0; k < sp::kPodCups; ++k) {  // pods in the bays give no thrust; the pairs share it for pitch
        const double share = podAssist_ ? 1.0 : podShare_[k / (2 * sp::kCupsPerPod)];  // assist: moments set per cup
        const double mx = !podAimed_ ? 0.0 : prm_.podThrustTotal / sp::kPodCups * f * share;  // no thrust until aimed
        SetThrusterMax0(pod_[k], mx);
        SetThrusterIsp(pod_[k], isp);
        podMax += mx;
    }
    PodAssistLevels(simdt, podMax);
    ScaleAttitudeThrust();
    {   // damage: lost pods and cups give nothing; a destroyed ship has no engines at all
        namespace dm = tantra::damage;
        for (int k = 0; k < sp::kPodCups; ++k)
            if (damage_.Lost(dm::kPod0 + k / sp::kCupsPerPod)) SetThrusterMax0(pod_[k], 0.0);
        if (damage_.Lost(dm::kMarchCup)) SetThrusterMax0(march_, 0.0);
        if (damage_.Destroyed()) {
            for (THRUSTER_HANDLE th : ana_) SetThrusterMax0(th, 0.0);
            SetThrusterMax0(march_, 0.0);
            for (THRUSTER_HANDLE th : retro_) SetThrusterMax0(th, 0.0);
            for (THRUSTER_HANDLE th : pod_) SetThrusterMax0(th, 0.0);
            for (THRUSTER_HANDLE th : attCouple_) SetThrusterMax0(th, 0.0);
            for (THRUSTER_HANDLE th : attRoll_) SetThrusterMax0(th, 0.0);
            for (THRUSTER_HANDLE th : attAxial_) SetThrusterMax0(th, 0.0);
        }
    }
    UpdateGear(simdt);
    UpdatePort(simdt);
    UpdatePods(simdt);

    const IgnStage prev = ignition_.Stage();
    // The chamber field is fed from the field store; an empty store cannot hold it up.
    const bool fieldPowerOk = ignition_.FieldLevel() >= 1.0 || drive_.StoreEnergy() > 0.0;
    ignition_.Update(simdt, fieldPowerOk);
    RebindGroups();  // feed on/off moves the main throttle between anamezon and pods
    if (!fieldPowerOk && prev != IgnStage::Off)
        Message("Накопитель поля пуст: аварийный останов камер", "Field store empty: chamber emergency stop");
    SelectActiveTrap();
    if (safety_) {
        // Radiation of the running drive: ecosystems and crews around (docs/DESIGN.md).
        const double lv = AnaIsMain() ? GetThrusterGroupLevel(THGROUP_MAIN) : 0.0;
        safety_->Update(this, drive_.JetPower(1.0), lv, simdt);
        for (const TantraSafety::Notice& n : safety_->TakeNotices()) Message("%s", "%s", n.ru.c_str(), n.en.c_str());
        // Overridden and still dangerous: keep warning.
        hazardTimer_ -= simdt;
        if (hotStartOverride_ && lv > 0.0 && lv > safety_->LevelCap() && hazardTimer_ <= 0.0) {
            char ru[256], en[256];
            std::snprintf(ru, sizeof ru, "ОПАСНОСТЬ: %s - блокировка снята", safety_->CapReasonRu().c_str());
            std::snprintf(en, sizeof en, "DANGER: %s - interlock overridden", safety_->CapReasonEn().c_str());
            Message("%s", "%s", ru, en);
            hazardTimer_ = 10.0;
        }
    }
    ApplyThrottleLimits();

    const bool anaFeeding = AnaIsMain();
    const double feed = anaFeeding ? GetThrusterGroupLevel(THGROUP_MAIN) : 0.0;
    drive_.Update(simdt, ignition_.FieldLevel(), feed, GetMass(), thrustAccel_);
    if (exhaust_) {
        VECTOR3 drag;
        GetDragVector(drag);
        ATMPARAM atm;
        oapiGetAtm(GetHandle(), &atm);
        const double sound = atm.T > 0.0 ? std::sqrt(1.4 * 287.0 * atm.T) : 0.0;
        exhaust_->Update(ignition_.FieldLevel(), ignition_.BeamLevel(), feed, GetAtmDensity(), drive_.JetPower(feed),
                         length(drag), GetAirspeed(), sound);
    }
    UpdateSound(prev);
}

void Tantra::clbkPostStep(double, double simdt, double) {
    properTime_ += simdt * ProperTimeRate(beta_);

    // Settling after a start on the ground: count the time at rest on the contacts.
    if (settleTimer_ > 0.0) settleTimer_ -= simdt;
    VECTOR3 vg;
    const bool still = GroundContact() && GetGroundspeedVector(FRAME_HORIZON, vg) && length(vg) < 0.05;
    settledFor_ = still ? settledFor_ + simdt : (GroundContact() ? 0.0 : settledFor_);

    VECTOR3 t, l, d;
    GetThrustVector(t);
    GetLiftVector(l);
    GetDragVector(d);
    thrustAccel_ = length(t) / GetMass();
    // The compensator cancels this step's thrust, not the previous one's: with the one-step lag a throttle jump (or a
    // burn that starts with thrust already set) put the whole load on the hull for a step - 198 g smoothed to 13 g.
    drive_.Compensate(AnaIsMain() ? GetThrusterGroupLevel(THGROUP_MAIN) : 0.0, GetMass(), thrustAccel_);
    // Proper acceleration: all forces but gravity - thrust, air AND the ground (a hard contact is a g-spike);
    // the compensator takes off what it cancels of the anamezon thrust. Crew and structure feel the rest.
    VECTOR3 F, W;
    GetForceVector(F);
    GetWeightVector(W);
    const double proper = length(F - W) / GetMass();
    accelG_ = (std::max)(0.0, proper - drive_.CompensatedAccel()) / G0;
    if (mechOn_) accelG_ = LocalG() / G0;   // the ground mechanism places the hull: the springs' forces are not loads
    // The structure: smoothed over 0.15 s (a contact solver spike is not a load), not while settling after a load.
    structG_ += (accelG_ - structG_) * (std::min)(1.0, simdt / 0.15);

    if (crew_.Process(simdt) == TantraCrew::Event::Returned)
        Message("%s вернулся на борт", "%s is back aboard", crew_.LastName());

    UpdateLegLoads(windFh_);
    UpdateDamage(simdt);
    UpdateDamageVisual(false);
    GuardAgainstLaunch(simdt);
    if (compOff_ > 0.0) compOff_ -= simdt;
    // Check the torque convention against Orbiter while the pods make a clear moment of their own.
    if (length(podTorquePred_) > 5e6) {
        VECTOR3 M;
        GetTorqueVector(M);
        const double c = dotp(M, podTorquePred_) / (length(M) * length(podTorquePred_) + 1e-9);
        torqueDisagree_ = c < -0.5 ? torqueDisagree_ + 1 : (c > 0.5 ? 0 : torqueDisagree_);
        if (torqueDisagree_ >= 5) {
            torqueSign_ = -torqueSign_;
            torqueDisagree_ = 0;
            compOff_ = 5.0;
            Message("Бортовой компьютер: знак момента уточнён по Орбитеру", "Computer: torque sign corrected from Orbiter");
        }
    }
    if (messageTimer_ > 0.0) messageTimer_ -= simdt;
}

// Orbiter first puts a landed ship on coarse terrain, then loads finer elevation tiles: the ground under
// the pads jumps and the contacts, suddenly buried, would fire the ship off the planet. While the ship
// rests, the elevation is sampled at five points fixed on the ground (CG, both sides, fore and aft). A jump
// raises all touchdown points by it at once (they sit on the new ground, nothing is compressed), then the
// raise relaxes at 0.3 m/s: the ship is lifted onto the new ground smoothly. The SDK asks exactly this:
// touchdown points on the ground may change only gradually, never by a jump of the ship's state.
void Tantra::WatchTerrain() {
    OBJHANDLE ref = GetSurfaceRef();
    const double dt = oapiGetSimStep();
    if (terrainOfs_ != 0.0) {
        const double d = (std::min)(std::fabs(terrainOfs_), 0.3 * dt);
        terrainOfs_ += terrainOfs_ > 0.0 ? -d : d;
    }
    const bool resting = carriage_.Gear() >= 0.5 && (GroundContact() || sinceContact_ < 5.0);
    if (!ref || !resting) {
        terrainInit_ = false;
        return;
    }
    if (!terrainInit_) {
        const VECTOR3 pts[5] = {_V(0, 0, 0), _V(20, 0, 0), _V(-20, 0, 0), _V(0, 0, 40), _V(0, 0, -40)};
        for (int i = 0; i < 5; ++i) {
            VECTOR3 g;
            double rad;
            Local2Global(pts[i], g);
            oapiGlobalToEqu(ref, g, &terrainLng_[i], &terrainLat_[i], &rad);
            terrainElev_[i] = oapiSurfaceElevation(ref, terrainLng_[i], terrainLat_[i]);
        }
        terrainInit_ = true;
        return;
    }
    double sum = 0.0, big = 0.0;
    for (int i = 0; i < 5; ++i) {
        const double e = oapiSurfaceElevation(ref, terrainLng_[i], terrainLat_[i]);
        sum += e - terrainElev_[i];
        big = (std::max)(big, std::fabs(e - terrainElev_[i]));
        terrainElev_[i] = e;
    }
    // Orbiter 2024 swaps elevation tiles under the ship (hundreds of metres, up to kilometres, back and forth while the
    // camera moves): that is not the ground being refined - moving the pads by it threw the ship off the planet
    if (big > 5.0) {
        oapiWriteLogV("Tantra: terrain data jumped %+.1f m under the ship - a tile swap, ignored", sum / 5.0);
        return;
    }
    if (big > 0.02) {
        terrainOfs_ = (std::max)(-5.0, (std::min)(5.0, terrainOfs_ + sum / 5.0));
        Message("Рельеф уточнён (%+.1f м): опоры переходят на новый грунт", "Terrain refined (%+.1f m): gear moving onto it",
                sum / 5.0);
    }
}

// Damage: inputs from Orbiter every step, effects applied in PreStep (aero, thrust, touchdown points).
// Orbiter's Launchpad damage switch is honoured: off = temperatures and loads shown, nothing breaks.
void Tantra::UpdateDamage(double dt) {
    namespace dm = tantra::damage;
    dm::Flight f;
    f.rho = GetAtmDensity();
    f.v = GetAirspeed();
    f.mach = GetMachNumber();
    f.aoa = GetAOA();
    f.beta = GetSlipAngle();
    f.gLoad = settleTimer_ > 0.0 ? 0.0 : structG_;
    ATMPARAM atm;
    oapiGetAtm(GetHandle(), &atm);
    f.ambientT = atm.T > 0.0 ? atm.T : 250.0;
    const tantra::CarriagePose& p = carriage_.Pose();
    dm::Exposure x;
    x.crests = x.fin = 1.0 - (std::max)(tuck_, p.tuck);
    x.pods = podOut_;
    x.gear = carriage_.Gear();
    x.hangar = hangar_;
    x.bays = bayDoors_;
    x.sternCups = (std::max)(marchOut_, irisAna_);
    dm::Ground g;
    VECTOR3 v;
    GetGroundspeedVector(FRAME_HORIZON, v);
    g.contact = GroundContact();
    g.touchdown = g.contact && !wasContact_;
    g.vDown = -lastVy_;
    g.gearDown = carriage_.Gear() > 0.5;
    for (int i = 0; i < 7; ++i) g.legRatio[i] = legR_[i];   // fibre sensors, every leg on its own
    g.jointModel = true;                                    // the legs break joint by joint (core/Foot, UpdateGear)
    {   // the MR struts spread the stop over their stroke
        const bool tail = carriage_.Set() == tantra::Carriage::FlightSet::Standing;
        const tantra::legs::TouchdownLimits lim =
            tantra::legs::Limits(tail ? tantra::legs::kStrokeStern : tantra::legs::kStrokeCarriage);
        g.vSoft = lim.vSoft;
        g.vBreak = lim.vBreak;
    }
    // what hits the ground (core/Impact): from the ship's attitude - nose down, stern down, on its belly or a side
    if (g.touchdown) {
        VECTOR3 up;
        HorizonInvRot(_V(0, 1, 0), up);                    // the world's up in the ship frame
        namespace im = tantra::impact;
        if (up.z < -0.6) g.impactMode = im::kNoseFirst;     // the nose points down
        else if (up.z > 0.6) g.impactMode = im::kSternFirst;
        else g.impactMode = std::fabs(up.x) > std::fabs(up.y) || up.y < 0.0 ? im::kSide : im::kBelly;
        const bool standing = carriage_.Set() == tantra::Carriage::FlightSet::Standing;
        g.legsInPlay = (standing && g.impactMode == im::kSternFirst) || (!standing && g.impactMode == im::kBelly);
        g.mass = GetMass();
        g.localG = LocalG();
        g.soil = 6.0e6;                                    // firm ground (Orbiter knows no soil types)
        double anamezon = 0.0;
        for (int i = 0; i < sp::kTrapCount; ++i) if (trap_[i]) anamezon += GetPropellantMass(trap_[i]);
        g.trapsLoaded = anamezon > 1000.0;
    }
    if (!g.contact) lastVy_ = v.y;
    wasContact_ = g.contact;
    damage_.Step(dt, f, x, g, GetDamageModel() != 0);
    ReportImpact();
    dm::Event ev[16];
    const int n = damage_.TakeEvents(ev, 16);
    for (int i = 0; i < n; ++i) {
        Message(ev[i].destroyed ? "РАЗРУШЕНИЕ: %s" : "Оторван: %s", ev[i].destroyed ? "DESTROYED: %s" : "Torn off: %s",
                russian_ ? ev[i].ru : ev[i].en, ev[i].en);
        BreakPart(ev[i].part);
    }
    if (damage_.Destroyed() && !shipGone_) BreakUp();
    // the dead frame left after a break-up on the ground stays where the ship died: sliding on its hull points at speed
    // over the terrain, Orbiter's stiff contact springs threw it off the planet at 10^9 m/s (and the camera with it)
    if (shipGone_ && GetSurfaceRef() && (GroundContact() || GetAltitude(ALTMODE_GROUND) < 200.0)) {
        VESSELSTATUS2 st;
        std::memset(&st, 0, sizeof st);
        st.version = 2;
        GetStatusEx(&st);
        if (st.status != 1) LandNow(false);
    }
    if (damage_.Destroyed() && !destroyedWarned_) {
        Message("Корабль разрушен: двигатели и системы мертвы (Ctrl+Shift+D - ремонт для отладки)",
                "Ship destroyed: engines and systems dead (Ctrl+Shift+D - debug repair)");
        destroyedWarned_ = true;
    }
    if (!damage_.Destroyed()) destroyedWarned_ = false;
}

// Spawn a debris vessel. posLocal: where it is now (ship frame), else its place in the mesh (centroid).
void Tantra::SpawnDebris(const char* name, const VECTOR3* posLocal, const VECTOR3& pushLocal, double tumble) {
    namespace m = tantra::mesh;
    const m::DebrisDef* d = nullptr;
    for (const m::DebrisDef& e : m::kDebris)
        if (!std::strcmp(e.name, name)) d = &e;
    if (!d) return;
    const VECTOR3 p = posLocal ? *posLocal : V3(d->centre) + _V(0, 0, MeshDZ());
    OBJHANDLE ref = GetGravityRef();
    VECTOR3 g, shipG, rpos, rvel, push, w;
    Local2Global(p, g);
    GetGlobalPos(shipG);
    GetRelativePos(ref, rpos);
    GetRelativeVel(ref, rvel);
    GlobalRot(pushLocal, push);
    GetAngularVel(w);
    MATRIX3 R;
    GetRotationMatrix(R);
    VESSELSTATUS2 vs;
    std::memset(&vs, 0, sizeof vs);
    vs.version = 2;
    vs.rbody = ref;
    vs.status = 0;
    vs.rpos = rpos + (g - shipG);
    vs.rvel = rvel + push;
    vs.arot = _V(std::atan2(R.m23, R.m33), -std::asin((std::max)(-1.0, (std::min)(1.0, R.m13))), std::atan2(R.m12, R.m11));
    auto rnd = [this]() {
        windRng_ = windRng_ * 1664525u + 1013904223u;
        return ((windRng_ >> 8) / 16777216.0) * 2.0 - 1.0;
    };
    vs.vrot = w + _V(rnd(), rnd(), rnd()) * tumble;
    // on the ground (or just over it) the part falls where it broke and lies there: a free body spawned at the ground
    // was thrown off by its own contact springs - into space
    OBJHANDLE sref = GetSurfaceRef();
    if (sref && (GroundContact() || GetAltitude(ALTMODE_GROUND) < 60.0)) {
        double lng, lat, rad;
        oapiGlobalToEqu(sref, g, &lng, &lat, &rad);
        double hdg = 0.0;
        oapiGetHeading(GetHandle(), &hdg);
        vs.rbody = sref;
        vs.status = 1;
        vs.surf_lng = lng;
        vs.surf_lat = lat;
        vs.surf_hdg = hdg + 0.4 * rnd();
        vs.arot = _V(0.0, 0.0, 0.0);
        vs.vrot = _V(d->cogH, 0.0, 0.0);
        vs.rvel = _V(0.0, 0.0, 0.0);
    }
    char vname[64];
    std::snprintf(vname, sizeof vname, "%s_debris_%02d", GetName(), ++debrisCount_);
    oapiCreateVesselEx(vname, d->cls, &vs);
    oapiWriteLogV("Tantra: debris %s (%s) %s, the ship %.1f m over the ground, %.1f m/s", vname, name,
                  vs.status == 1 ? "laid on the ground" : "free", GetAltitude(ALTMODE_GROUND), GetGroundspeed());
}

// A part torn off: its debris leaves from where the part is now.
// After a hull impact: what the crew learns - how hard, what is crushed, what tore off its mounts, how the people
// are (in the living module the inertia absorber takes up to its limit while it holds; elsewhere the full blow).
void Tantra::ReportImpact() {
    namespace dm = tantra::damage;
    namespace im = tantra::impact;
    dm::ImpactReport r;
    if (!damage_.TakeImpact(&r)) return;
    static const char* kModeRu[4] = {"носом", "кормой", "днищем", "бортом"};
    static const char* kModeEn[4] = {"nose first", "stern first", "on the belly", "on a side"};
    static const char* kGradeRu[4] = {"цел", "вмятины", "пробит", "разрушен"};
    static const char* kGradeEn[4] = {"intact", "dented", "holed", "destroyed"};
    static const char* kStateRu[4] = {"цел", "на пределе", "повреждён", "сорван"};
    static const char* kStateEn[4] = {"intact", "at its limit", "damaged", "torn off"};
    if (r.trapBreach) {
        Message("РАЗРЫВ ЛОВУШКИ: анамезон вне поля", "TRAP BREACH: anamezon out of the field");
        oapiWriteLogV("Tantra: impact %s %.1f m/s, %.0f g - TRAP BREACH", kModeEn[r.mode], r.v, r.peakG);
        return;
    }
    // people: the absorber (living module, 30 g off) while it holds; outside the module, the full blow
    auto people = [](double g, bool ru) {
        if (g > 80.0) return ru ? "гибель" : "killed";
        if (g > 40.0) return ru ? "тяжёлые травмы" : "severe injuries";
        if (g > 15.0) return ru ? "травмы" : "injuries";
        if (g > 6.0) return ru ? "ушибы" : "bruises";
        return ru ? "без травм" : "unhurt";
    };
    const bool livingCrushed = r.zoneGrade[im::kLiving] >= 3;
    const double gIn = r.absorberAlive ? (std::max)(0.0, r.peakG - 30.0) : r.peakG;
    char zones[256] = "", equip[384] = "", zonesEn[256] = "", equipEn[384] = "";
    static const char* kZoneRu[im::kZoneCount] = {"корма", "блок ловушек", "ангар", "жилые отсеки", "нос-щит"};
    static const char* kZoneEn[im::kZoneCount] = {"stern", "trap block", "hangar", "living module", "nose shield"};
    for (int z = im::kZoneCount - 1; z >= 0; --z) {
        if (!r.zoneGrade[z]) continue;
        std::snprintf(zones + std::strlen(zones), sizeof zones - std::strlen(zones), "%s%s: %s", zones[0] ? ", " : "", kZoneRu[z], kGradeRu[r.zoneGrade[z]]);
        std::snprintf(zonesEn + std::strlen(zonesEn), sizeof zonesEn - std::strlen(zonesEn), "%s%s: %s", zonesEn[0] ? ", " : "", kZoneEn[z], kGradeEn[r.zoneGrade[z]]);
    }
    if (r.bellyGrade) {
        std::snprintf(zones + std::strlen(zones), sizeof zones - std::strlen(zones), "%s%s: %s", zones[0] ? ", " : "", r.mode == im::kSide ? "борт" : "днище", kGradeRu[r.bellyGrade]);
        std::snprintf(zonesEn + std::strlen(zonesEn), sizeof zonesEn - std::strlen(zonesEn), "%s%s: %s", zonesEn[0] ? ", " : "", r.mode == im::kSide ? "side" : "belly", kGradeEn[r.bellyGrade]);
    }
    for (int e = 1; e < im::kEquipCount; ++e) {
        if (r.equipState[e] < 2) continue;
        std::snprintf(equip + std::strlen(equip), sizeof equip - std::strlen(equip), "%s%s %s", equip[0] ? ", " : "", im::kEquip[e].ru, kStateRu[r.equipState[e]]);
        std::snprintf(equipEn + std::strlen(equipEn), sizeof equipEn - std::strlen(equipEn), "%s%s %s", equipEn[0] ? ", " : "", im::kEquip[e].en, kStateEn[r.equipState[e]]);
    }
    CrashSound(SND_CRASH_EXT, (std::min)(1.0, 0.35 + r.v / 12.0));
    Message("Удар %s %.1f м/с: %.0f g. Люди в модуле: %s, вне модуля: %s",
            "Impact %s %.1f m/s: %.0f g. People in the module: %s, outside it: %s",
            russian_ ? kModeRu[r.mode] : kModeEn[r.mode], r.v, r.peakG,
            livingCrushed ? (russian_ ? "гибель (модуль раздавлен)" : "killed (module crushed)") : people(gIn, russian_), people(r.peakG, russian_));
    if (zones[0]) Message("Корпус: %s", "Hull: %s", russian_ ? zones : zonesEn, zonesEn);
    if (equip[0]) Message("Оборудование: %s", "Equipment: %s", russian_ ? equip : equipEn, equipEn);
    oapiWriteLogV("Tantra: impact %s %.1f m/s, %.0f g, %.0f ms, into the ground %.1f m | hull: %s | equipment: %s",
                  kModeEn[r.mode], r.v, r.peakG, r.duration * 1e3, r.penetration, zonesEn[0] ? zonesEn : "intact", equipEn[0] ? equipEn : "intact");
}

// The feet's failures (core/Foot) to the crew and the log; a lost ankle or stage joint loses the leg.
void Tantra::FootEvents() {
    namespace ft = tantra::foot;
    namespace dm = tantra::damage;
    ft::Event ev[32];
    const int n = feet_.TakeEvents(ev, 32);
    for (int k = 0; k < n; ++k) {
        const ft::Event& e = ev[k];
        char ru[200], en[200];
        ft::Feet::Describe(e, dm::Model::NameRu(dm::kLegPort + e.leg), true, ru, sizeof ru);
        ft::Feet::Describe(e, dm::Model::NameEn(dm::kLegPort + e.leg), false, en, sizeof en);
        Message("%s", "%s", russian_ ? ru : en);
        oapiWriteLogV("Tantra: foot - %s", en);
        if (e.kind == ft::kRibBroken && e.cell >= 0) {
            static const char* kPetal[3] = {"petal_blade", "petal_stern", "petal_kang"};
            SpawnDebris(kPetal[ft::KindOf(e.leg)], &padPos_[e.leg][e.cell], _V(0, 1.0, 0), 0.6);
        }
        if (e.kind == ft::kAnkleBroken || e.kind == ft::kCollarBroken) {
            damage_.Inflict(dm::kLegPort + e.leg, 1.0, true);
            CrashSound(SND_LEG_BREAK_EXT, 1.0);
        } else if (k == 0 || ev[k - 1].kind != e.kind) {
            CrashSound(SND_PETAL_SNAP_EXT, 0.8);               // one snap per burst of petals
        }
    }
}

// A leg lost at one of its joints (core/Foot): at the ankle the foot falls, at the lowest joint the last stage with
// the foot; they leave from where the foot stands. Whole legs (torn off by the flow, burnt) go in BreakPart.
bool Tantra::LegJointDebris(int l) {
    static const char* kFoot[7] = {"foot_port_whole", "foot_starboard_whole", "sfoot0_whole", "sfoot1_whole", "sfoot2_whole", "sfoot3_whole", "kfoot_whole"};
    static const char* kLower[7] = {"leg_port_lower", "leg_starboard_lower", "sternleg_0_lower", "sternleg_1_lower", "sternleg_2_lower",
                                    "sternleg_3_lower", "kangleg_lower"};
    if (!feet_.CollarLost(l) && !feet_.AnkleLost(l)) return false;
    const VECTOR3* at = cupOn_[l] ? &cupC_[l] : nullptr;
    SpawnDebris(feet_.CollarLost(l) ? kLower[l] : kFoot[l], at, _V(0, 0.5, 0), 0.3);
    return true;
}

void Tantra::BreakPart(int part) {
    namespace dm = tantra::damage;
    namespace m = tantra::mesh;
    const VECTOR3 out = _V(0, 1, 0);
    switch (part) {
        case dm::kCrestPort: SpawnDebris("crest_port", nullptr, _V(-3, 1, -2), 0.6); break;
        case dm::kCrestStbd: SpawnDebris("crest_starboard", nullptr, _V(3, 1, -2), 0.6); break;
        case dm::kFin: SpawnDebris("fin", nullptr, _V(0, 3, -2), 0.5); break;
        case dm::kPod0: case dm::kPod1: case dm::kPod2: case dm::kPod3: {
            const int i = part - dm::kPod0;
            VECTOR3 c = _V(0, 0, 0);
            for (int k = 0; k < sp::kCupsPerPod; ++k) c = c + podExhPos_[i * sp::kCupsPerPod + k];
            c = c / sp::kCupsPerPod;
            const char* names[4] = {"pod_0", "pod_1", "pod_2", "pod_3"};
            SpawnDebris(names[i], podOut_ > 0.5 ? &c : nullptr, _V(c.x > 0 ? 2.0 : -2.0, -1, -1), 1.0);
            break;
        }
        case dm::kLegPort: case dm::kLegStbd: case dm::kSternLeg0: case dm::kSternLeg1: case dm::kSternLeg2: case dm::kSternLeg3:
        case dm::kKangLeg:
            if (LegJointDebris(part - dm::kLegPort)) break;   // the foot at the ankle / the last stage at the lowest joint
            if (part == dm::kKangLeg) goto kang;
            if (part >= dm::kSternLeg0) goto stern;
            {
            const tantra::CarriagePose& p = carriage_.Pose();
            const double side = part == dm::kLegPort ? -1.0 : 1.0;
            VECTOR3 c = _V(side * m::kHipXOut, -0.5 * p.mastLen, p.hipS - frameS_);  // the blade, hanging
            SpawnDebris(part == dm::kLegPort ? "leg_port" : "leg_starboard", &c, _V(side * 1.5, 0, 0), 0.3);
            break;
        }
        kang: {
            const tantra::CarriageGeometry& g = carriage_.Geometry();
            VECTOR3 c = _V(0, g.kangHipY - 0.5 * (g.kangThigh + g.kangShinMin), Zf(g.kangHipS + 0.5 * g.kangFootFwd));
            SpawnDebris("kangleg", &c, _V(0, -1, 1), 0.3);
            break;
        }
        stern: {
            const int i = part - dm::kSternLeg0;
            const m::LegRig& L = m::kLegs[i];
            VECTOR3 h = V3(L.hinge) + _V(0, 0, MeshDZ());
            VECTOR3 f = _V(L.radial.x * m::kStandR, L.radial.y * m::kStandR, Zf(m::kStandGroundS));
            VECTOR3 c = (h + f) * 0.5;
            const char* names[4] = {"sternleg_0", "sternleg_1", "sternleg_2", "sternleg_3"};
            SpawnDebris(names[i], &c, _V(L.radial.x, L.radial.y, 0) * 1.5, 0.3);
            break;
        }
        case dm::kHangarDoors:
            SpawnDebris("door_top_port", nullptr, _V(-2, 3, -3), 1.0);
            SpawnDebris("door_top_starboard", nullptr, _V(2, 3, -3), 1.0);
            SpawnDebris("door_bottom_port", nullptr, _V(-2, -2, -3), 1.0);
            SpawnDebris("door_bottom_starboard", nullptr, _V(2, -2, -3), 1.0);
            break;
        case dm::kBayDoors:
            SpawnDebris("bay_door_port", nullptr, _V(-2, -2, -3), 1.0);
            SpawnDebris("bay_door_starboard", nullptr, _V(2, -2, -3), 1.0);
            break;
        default: break;
    }
    (void)out;
}

// The hull fails: it breaks into four chunks flying apart, with whatever was still attached; the ship
// becomes a dead, invisible frame (Ctrl+Shift+D repairs it for debugging).
void Tantra::BreakUp() {
    namespace dm = tantra::damage;
    shipGone_ = true;
    for (AIRFOILHANDLE& h : foil_) {  // the invisible frame left behind must not fly like the ship
        if (h) DelAirfoil(h);
        h = nullptr;
    }
    // the hull parts along the lines the impact broke (core/Impact BreakLines): zones not cut apart stay together and
    // move as one piece; a zone crushed half or more is a crushed chunk (flattened by a belly blow, shortened along the
    // axis); the pieces separate along the axis, a belly blow throws them up by what the blow left
    {
        namespace im = tantra::impact;
        double share[im::kZoneCount];
        for (int z = 0; z < im::kZoneCount; ++z) share[z] = damage_.Crushed(z) / (im::kZones[z].s1 - im::kZones[z].s0);
        const dm::ImpactReport& ir = damage_.LastImpact();
        const im::Breakup b = im::BreakLines(ir.mode, share, damage_.Belly(), ir.peakG);
        static const char* kChunk[im::kZoneCount] = {"hull_stern", "hull_traps", "hull_hangar", "hull_living", "hull_nose"};
        int pieces = 1;
        for (bool c : b.cut) pieces += c ? 1 : 0;
        int piece = 0;
        char cuts[64] = "";
        for (int z = 0; z < im::kZoneCount; ++z) {
            if (z > 0 && b.cut[z - 1]) {
                ++piece;
                std::snprintf(cuts + std::strlen(cuts), sizeof cuts - std::strlen(cuts), " s%.0f", im::kZones[z].s0);
            }
            char name[40];
            std::snprintf(name, sizeof name, "%s%s", kChunk[z], b.crush[z] == 1 ? "_flat" : b.crush[z] == 2 ? "_short" : "");
            const double along = pieces > 1 ? (piece - 0.5 * (pieces - 1)) * 1.5 : 0.0;
            const double up = ir.mode == im::kBelly || ir.mode == im::kSide ? 0.08 * ir.v : 0.0;
            SpawnDebris(name, nullptr, _V(0, up, along), pieces > 1 ? 0.12 : 0.03);
        }
        oapiWriteLogV("Tantra: break-up - %d pieces, cut at%s (impact %d, %.0f g, belly %.1f m)", pieces, cuts[0] ? cuts : " nothing",
                      ir.mode, ir.peakG, damage_.Belly());
    }
    for (int part = 0; part < dm::kPartCount; ++part)
        if (!damage_.Lost(part) && part != dm::kHull && part != dm::kNose && part != dm::kBelly && part != dm::kMarchCup) {
            const bool stowed = (part >= dm::kPod0 && part <= dm::kPod3 && podOut_ < 0.5) ||
                                (part >= dm::kLegPort && part <= dm::kKangLeg && carriage_.Gear() < 0.5) ||
                                (part == dm::kHangarDoors && hangar_ < 0.5) || (part == dm::kBayDoors && bayDoors_ < 0.5);
            if (!stowed) BreakPart(part);  // stowed parts stay inside their chunk
        }
    UpdateDamageVisual(true);
}

void Tantra::clbkVisualCreated(VISHANDLE vis, int) {
    vis_ = vis;
    screen_.OnVisual(vis);
    interior_.OnVisual(vis);
    disp_.OnVisual(vis);
    UpdateDamageVisual(true);
}

void Tantra::clbkVisualDestroyed(VISHANDLE vis, int) {
    if (vis == vis_) { vis_ = nullptr; screen_.OnVisualGone(); interior_.OnVisualGone(); }
}

void Tantra::UpdateDamageVisual(bool force) {
    namespace dm = tantra::damage;
    namespace m = tantra::mesh;
    if (!vis_) return;
    DEVMESHHANDLE mesh = GetDevMesh(vis_, meshIdx_);
    if (!mesh) return;
    // --- the whole ship gone (break-up): hide every group; back: unhide and re-apply the part flags
    if (force || shipGone_ != shownGone_) {
        GROUPEDITSPEC all = {};
        all.flags = shipGone_ ? GRPEDIT_ADDUSERFLAG : GRPEDIT_DELUSERFLAG;
        all.UsrFlag = 0x3;
        for (DWORD gi = 0; gi < m::GRP_COUNT; ++gi) oapiEditMeshGroup(mesh, gi, &all);
        shownGone_ = shipGone_;
        force = true;
    }
    // --- lost parts
    auto groups = [](int part, int* g) {
        int n = 0;
        switch (part) {
            case dm::kCrestPort: g[n++] = m::GRP_CREST_PORT; g[n++] = m::GRP_ELEVON_PORT; break;
            case dm::kCrestStbd: g[n++] = m::GRP_CREST_STARBOARD; g[n++] = m::GRP_ELEVON_STARBOARD; break;
            case dm::kFin: g[n++] = m::GRP_FIN; break;
            case dm::kPod0: g[n++] = m::GRP_POD_0; g[n++] = m::GRP_DOOR_POD_0; g[n++] = m::GRP_ARM_POD_0; break;
            case dm::kPod1: g[n++] = m::GRP_POD_1; g[n++] = m::GRP_DOOR_POD_1; g[n++] = m::GRP_ARM_POD_1; break;
            case dm::kPod2: g[n++] = m::GRP_POD_2; g[n++] = m::GRP_DOOR_POD_2; g[n++] = m::GRP_ARM_POD_2; break;
            case dm::kPod3: g[n++] = m::GRP_POD_3; g[n++] = m::GRP_DOOR_POD_3; g[n++] = m::GRP_ARM_POD_3; break;
            case dm::kLegPort:
            case dm::kLegStbd: {   // stages 1-2, ankle, umbrella (hub, ribs, fabric) - stage 0 stays on the hip
                const bool port = part == dm::kLegPort;
                g[n++] = port ? m::GRP_ANKLE_PORT : m::GRP_ANKLE_STARBOARD;
                g[n++] = (port ? m::GRP_BLADE_PORT_0 : m::GRP_BLADE_STARBOARD_0) + 1;
                g[n++] = (port ? m::GRP_BLADE_PORT_0 : m::GRP_BLADE_STARBOARD_0) + 2;
                const int hub = port ? m::GRP_FOOT_PORT_HUB : m::GRP_FOOT_STARBOARD_HUB;
                for (int i = 0; i <= 17 && n < 24; ++i) g[n++] = hub + i;
                break;
            }
            case dm::kSternLeg0: case dm::kSternLeg1: case dm::kSternLeg2: case dm::kSternLeg3: {
                const int i = part - dm::kSternLeg0, stride = m::GRP_LEG1_SEC0 - m::GRP_LEG0_SEC0;
                for (int k = 1; k < stride; ++k) g[n++] = m::GRP_LEG0_SEC0 + i * stride + k;  // sections 1-3, ankle
                const int hub = m::GRP_SFOOT0_HUB + i * (m::GRP_SFOOT1_HUB - m::GRP_SFOOT0_HUB);
                for (int k = 0; k <= 17 && n < 24; ++k) g[n++] = hub + k;
                break;
            }
            case dm::kKangLeg: {
                for (int k = 1; k < 7; ++k) g[n++] = m::GRP_KANG_SHIN_0 + k;
                g[n++] = m::GRP_KANG_ANKLE;
                for (int k = 0; k <= 17 && n < 26; ++k) g[n++] = m::GRP_KFOOT_HUB + k;
                break;
            }
            case dm::kHangarDoors:
                g[n++] = m::GRP_DOOR_TOP_PORT; g[n++] = m::GRP_DOOR_TOP_STARBOARD;
                g[n++] = m::GRP_DOOR_BOTTOM_PORT; g[n++] = m::GRP_DOOR_BOTTOM_STARBOARD;
                break;
            case dm::kBayDoors: g[n++] = m::GRP_BAY_DOOR_PORT; g[n++] = m::GRP_BAY_DOOR_STARBOARD; break;
            default: break;
        }
        return n;
    };
    // the legs: lost below the hip (torn off whole), the last stage with the foot (the lowest joint), the foot (the
    // ankle), single petals broken off (their struts stay on the collar)
    for (int l = 0; l < 7 && !shipGone_; ++l) {
        const bool whole = damage_.Lost(dm::kLegPort + l) && !feet_.CollarLost(l) && !feet_.AnkleLost(l);
        const bool lower = feet_.CollarLost(l);
        const bool foot = whole || lower || feet_.AnkleLost(l);
        unsigned long long key = (whole ? 1ull : 0ull) | (lower ? 2ull : 0ull) | (foot ? 4ull : 0ull);
        for (int c = 0; c < 12; ++c) if (feet_.RibLost(l, c)) key |= 1ull << (8 + c);
        if (!force && key == shownLeg_[l]) continue;
        shownLeg_[l] = key;
        auto hide = [&](int gi, bool h) {
            GROUPEDITSPEC ges = {};
            ges.flags = h ? GRPEDIT_ADDUSERFLAG : GRPEDIT_DELUSERFLAG;
            ges.UsrFlag = 0x3;
            oapiEditMeshGroup(mesh, static_cast<DWORD>(gi), &ges);
        };
        const int n = m::kLegSegN[l];
        for (int k = 0; k < n; ++k) hide(m::kLegSeg[l][k], whole || (lower && k == n - 1));
        hide(m::kAnkleGrp[l], whole || lower);
        for (int k = 0; k < m::kFootGrpN; ++k) {
            const int rel = k - m::kPetalFirst, c = rel >= 0 ? rel / m::kPetalStride : -1, in = rel >= 0 ? rel % m::kPetalStride : -1;
            const bool petal = c >= 0 && c < 12 && in != m::kPetalStrut && feet_.RibLost(l, c);
            hide(m::kFootFirst[l] + k, foot || petal);
        }
    }
    for (int part = 0; part < dm::kPartCount && !shipGone_; ++part) {
        if (part >= dm::kLegPort && part <= dm::kKangLeg) continue;   // the legs above
        const bool lost = damage_.Lost(part);
        if (!force && lost == shownLost_[part]) continue;
        int g[28];
        const int n = groups(part, g);
        GROUPEDITSPEC ges = {};
        ges.flags = lost ? GRPEDIT_ADDUSERFLAG : GRPEDIT_DELUSERFLAG;
        ges.UsrFlag = 0x3;  // do not render, no ground shadow
        for (int k = 0; k < n; ++k) oapiEditMeshGroup(mesh, static_cast<DWORD>(g[k]), &ges);
        shownLost_[part] = lost;
    }
    // --- heat glow: iridium nose/shoulder (material 1) by the nose, crests and fin (material 2) by the edges
    struct Mat { float dr, dg, db, sr, sg, sb, pow; };
    static const Mat kMat[2] = {{0.78f, 0.76f, 0.70f, 0.95f, 0.95f, 0.95f, 80.0f}, {0.46f, 0.26f, 0.18f, 0.15f, 0.15f, 0.15f, 10.0f}};
    const double temps[2] = {damage_.Temperature(dm::kZoneNose), damage_.Temperature(dm::kZoneCrestEdge)};
    for (int i = 0; i < 2; ++i) {
        const double T = temps[i];
        if (!force && std::fabs(T - glowT_[i]) < 15.0) continue;
        glowT_[i] = T;
        const double t = (std::max)(0.0, (std::min)(1.0, (T - 800.0) / 1200.0));  // 800 K dark red .. 2000 K yellow-white
        MATERIAL mat;
        const Mat& b = kMat[i];
        mat.diffuse = {b.dr, b.dg, b.db, 1.0f};
        mat.ambient = {b.dr, b.dg, b.db, 1.0f};
        mat.specular = {b.sr, b.sg, b.sb, 1.0f};
        mat.emissive = {static_cast<float>((std::min)(1.0, 1.4 * t)), static_cast<float>(t * t * 0.9),
                        static_cast<float>(t * t * t * 0.6), 1.0f};
        mat.power = b.pow;
        oapiSetMaterial(mesh, 1 + i, &mat);
    }
}

// Which part carries touchdown point i of the carriage pose (sets: rest 6, columns 4, stand 4, belly 3).
// By where the point is, not by its index: the carriage reorders the first three points so that they face up
// (Orbiter's touchdown plane) - a fixed table mixed the port blade with the kangaroo.
int Tantra::TouchLeg(int i, const tantra::CarriagePose& p) const {
    if (p.nTouch == 6) {                                   // lying: the blades at +-19 m, the kangaroo on the axis
        const double x = p.touch[i].x;
        return std::fabs(x) < 6.0 ? 6 : (x > 0.0 ? 1 : 0);
    }
    if (p.nTouch == 4) {
        if (p.onColumns) return p.touch[i].x >= 0.0 ? 1 : 0;
        // standing: the stern foot nearest to the point
        const tantra::CarriageGeometry& g = carriage_.Geometry();
        int best = 0;
        double bd = 1e30;
        for (int k = 0; k < 4; ++k) {
            const double dx = p.touch[i].x - g.standFoot[k].x, dy = p.touch[i].y - g.standFoot[k].y;
            if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = k; }
        }
        return 2 + best;
    }
    return -1;
}

bool Tantra::TouchPointLost(int i, const tantra::CarriagePose& p) const {
    const int leg = TouchLeg(i, p);  // damage parts: kLegPort, kLegStbd, kSternLeg0..3, kKangLeg in this order
    return leg >= 0 && damage_.Lost(tantra::damage::kLegPort + leg);
}

// The ship's computer watches the contacts: with the gear out, just off the ground, no engine able to
// lift it and yet climbing (or spinning on its pads) - that is a contact bounce, not flight. It puts
// the ship straight back down where it is, with the same heading, and settles the suspension again.
void Tantra::GuardAgainstLaunch(double dt) {
    {   // off the ground soon after a contact: what the ship does, once a second (Orbiter.log)
        static double logT = -1e9;
        const double now = oapiGetSimTime();
        if (!GroundContact() && sinceContact_ < 120.0 && now - logT > 1.0) {
            logT = now;
            VECTOR3 gs;
            GetGroundspeedVector(FRAME_HORIZON, gs);
            oapiWriteLogV("Tantra: airborne %.0f s after contact - alt %.1f m, vertical %+.1f m/s, speed %.1f m/s, thrust %.2f g, main %.3f, carriage %.2f",
                          sinceContact_, GetAltitude(ALTMODE_GROUND), gs.y, length(gs), thrustAccel_ / G0, GetThrusterGroupLevel(THGROUP_MAIN),
                          carriage_.Progress());
        }
    }
    const bool contact = GroundContact();
    sinceContact_ = contact ? 0.0 : sinceContact_ + dt;
    if (relandCool_ > 0.0) relandCool_ -= dt;
    VECTOR3 v, w, up;
    GetGroundspeedVector(FRAME_HORIZON, v);
    GetAngularVel(w);
    HorizonInvRot(_V(0, 1, 0), up);
    if (shipGone_) { bounceDamp_ = 0.0; return; }        // a dead frame is held where the ship died (clbkPreStep), no pushing
    const bool engines = thrustAccel_ > 0.8 * LocalG();
    if (carriage_.Gear() >= 0.5 && relandCool_ <= 0.0 && !engines) {
        const bool thrown = !contact && sinceContact_ < 5.0 && v.y > 3.0;
        const bool spinning = contact && !carriage_.Busy() && length(w) > 0.35;
        if (thrown || spinning) {
            bounceDamp_ = 3.0;
            relandCool_ = 5.0;
            ++relands_;
            Message("Опоры: отскок, компьютер гасит скорость и вращение", "Gear: bounce, the computer damps it out");
        }
    }
    if (bounceDamp_ <= 0.0) return;
    bounceDamp_ -= dt;
    if (length(w) > 3.0) {  // damping cannot make it spin this fast: the sign is wrong - flip it, stop
        torqueSign_ = -torqueSign_;
        bounceDamp_ = 0.0;
        return;
    }
    // Upward speed and rotation brought to zero in ~0.5 s by forces (<= 3 g), physics stays continuous.
    const double m = GetMass(), cap = 3.0 * LocalG() * m;
    const double vUp = (std::max)(0.0, v.y);
    AddForce(up * (-(std::min)(cap, m * vUp / 0.5)), _V(0, 0, 0));
    const VECTOR3 I = _V(sp::kPmiPitch, sp::kPmiPitch, sp::kPmiRoll) * m;
    VECTOR3 T = _V(-I.x * w.x, -I.y * w.y, -I.z * w.z) / 0.7;
    const double L = 20.0;  // couple: force at +p, its opposite at the CG
    for (int ax = 0; ax < 3; ++ax) {
        VECTOR3 Ta = _V(ax == 0 ? T.x : 0, ax == 1 ? T.y : 0, ax == 2 ? T.z : 0);
        if (length(Ta) < 1.0) continue;
        const VECTOR3 p = ax == 2 ? _V(L, 0, 0) : _V(0, 0, L);
        const VECTOR3 F = crossp(p, Ta) * (torqueSign_ / (L * L));  // F x p = T (Orbiter's convention)
        AddForce(F, p);
        AddForce(F * -1.0, _V(0, 0, 0));
    }
}

void Tantra::UpdateSound(IgnStage prev) {
    if (!sound_ || !sound_->IsPresent()) return;
    const IgnStage now = ignition_.Stage();
    if (now != prev) {
        if (now == IgnStage::Field && prev == IgnStage::Off) sound_->PlayWav(SND_FIELD_UP);
        else if (now == IgnStage::Beam && prev == IgnStage::Field) sound_->PlayWav(SND_BEAM_UP);
        else if (now == IgnStage::Feed) {
            sound_->PlayWav(SND_IGNITE);
        } else if (now < prev) sound_->PlayWav(SND_SHUTDOWN);
    }

    const double level = GetThrusterGroupLevel(THGROUP_MAIN);
    const double ana = AnaIsMain() ? level : 0.0;
    const double march = march_ ? GetThrusterLevel(march_) : 0.0;
    double pods = 0.0;
    for (THRUSTER_HANDLE th : pod_)
        if (th) pods = (std::max)(pods, GetThrusterLevel(th));
    // where the listener is: in the cockpit view of the ship, or a person walking inside it (the focus on the body)
    const OBJHANDLE focus = oapiGetFocusObject();
    // muffled only while the camera itself is inside: the cockpit view of the ship, or the eyes of a person on board;
    // any outside camera (the pilot's outside view V included) hears the ship from where the camera is
    const bool inside = oapiCameraInternal() && (focus == GetHandle() || crew_.ListenerInside(focus));
    // a sealed armoured hull: from inside no wind, no plasma, no boom - whatever view the camera has (a person inside
    // looking at the ship from outside still hears what he or she hears)
    if (int(inside) != soundInside_) {
        soundInside_ = int(inside);
        for (XRSound::DefaultSoundID id : {XRSound::LandedWind, XRSound::FlightWind, XRSound::ReentryPlasma, XRSound::SonicBoom})
            sound_->SetDefaultSoundEnabled(id, !inside);
    }
    // one layer per engine and place; volume follows thrust (XRSound has no pitch control)
    auto layer = [this](int id, bool on, double vol) {
        if (on && vol > 0.01) sound_->PlayWav(id, true, float((std::min)(1.0, vol)));
        else if (sound_->IsWavPlaying(id)) sound_->StopWav(id);
    };
    // outside: the listener's own distance to the stern and the air around the ship (vacuum: nothing reaches the ears)
    double outAtt = 0.0;
    if (!inside) {
        VECTOR3 ear, stern;
        oapiCameraGlobalPos(&ear);                 // the camera: the person's eyes, or the outside view
        Local2Global(_V(0, 0, Zf(0.0)), stern);
        const double d = length(ear - stern);
        const double air = (std::min)(1.0, std::sqrt((std::max)(0.0, GetAtmDensity()) / 1.2));
        outAtt = air / (1.0 + d / 400.0);          // a big engine: full at the ship, half at 400 m, a quarter at 1.2 km
    }
    // anamezon: outside a roar where there is air; inside the drive is felt through the whole structure
    layer(SND_ANA_RUN, !inside && ana > 0.0, (0.35 + 0.65 * ana) * outAtt);
    layer(SND_ANA_INT, inside && ana > 0.0, 0.45 + 0.55 * ana);
    // planetary: outside the full roar of the jets; inside only a dull rumble of the engine through the hull
    layer(SND_MARCH_EXT, !inside && march > 0.0, (0.40 + 0.60 * march) * outAtt);
    layer(SND_MARCH_INT, inside && march > 0.0, 0.10 + 0.25 * march);
    layer(SND_POD_EXT, !inside && pods > 0.0, (0.35 + 0.65 * pods) * outAtt);
    layer(SND_POD_INT, inside && pods > 0.0, 0.08 + 0.20 * pods);
    if (sound_->IsWavPlaying(SND_ION_RUN)) sound_->StopWav(SND_ION_RUN);   // the old common planetary loop: replaced
    LiftSound(inside);
}

// The carriage is a 52 kt machine: it is heard. Inside through the structure (always, dull, the hull rings), outside
// through the air (its density, the listener's distance to the CG - nothing in vacuum). The drives hum with the
// speed of the motion (deeper when it creeps); every change of state - a start, a stop, a lock, the hull moving onto
// its rails, a turn beginning, the stern legs' cups on the ground, the weight passing to the struts, a blade going
// home into its pocket - is a blow through the whole structure.
// Something breaking, heard where the listener is: inside through the hull at full strength, outside through the air
// (nothing in vacuum) fading with the distance like the carriage's blows.
void Tantra::CrashSound(int slotExt, double level) {
    if (!sound_) return;
    const OBJHANDLE focus = oapiGetFocusObject();
    const bool inside = oapiCameraInternal() && (focus == GetHandle() || crew_.ListenerInside(focus));
    double vol = level;
    if (!inside) {
        VECTOR3 ear, cg;
        oapiCameraGlobalPos(&ear);
        GetGlobalPos(cg);
        vol *= (std::min)(1.0, std::sqrt((std::max)(0.0, GetAtmDensity()) / 1.2)) / (1.0 + length(ear - cg) / 1000.0);
    }
    if (vol < 0.02) return;
    sound_->PlayWav(slotExt + (inside ? 1 : 0), false, static_cast<float>((std::min)(1.0, vol)));
}

// Dust from under the feet (in an atmosphere; the particle streams fade with its density): a cloud when the feet
// touch down, as hard as the sink rate, and a steady trail while the legs work the moving ship. Each foot's stream
// sits at its cup and is laid anew when the cup has moved (the carriage moves the hull over the planted feet).
void Tantra::FootDust(double dt, const VECTOR3& up) {
    if (dt <= 0.0) return;
    static SURFHANDLE tex = oapiRegisterParticleTexture(const_cast<char*>("Tantra_dust"));
    const bool contact = GroundContact();
    VECTOR3 gs;
    GetGroundspeedVector(FRAME_HORIZON, gs);
    const double sink = (std::max)(0.0, -gs.y);
    for (int l = 0; l < 7; ++l) {
        if (contact && !dustContact_ && cupOn_[l]) footDustPulse_[l] = (std::max)(footDustPulse_[l], (std::min)(1.0, 0.3 + sink / 3.0));
        footDustPulse_[l] *= (std::max)(0.0, 1.0 - dt / 2.5);
        const double work = cupOn_[l] && carriage_.Busy() ? 0.25 + 0.35 * liftLevel_ : 0.0;
        footDustLv_[l] = (std::max)(footDustPulse_[l], work);
        if (!cupOn_[l]) { footDustLv_[l] = 0.0; continue; }
        const VECTOR3 at = cupC_[l] + up * 0.5;
        if (footDust_[l] && length(at - footDustAt_[l]) < 3.0) continue;
        if (footDust_[l]) DelExhaustStream(footDust_[l]);
        PARTICLESTREAMSPEC dust = {0, 0.5 * cupR_[l], 8.0, 4.0, 0.6, 9.0, 0.6 * cupR_[l], 1.5, PARTICLESTREAMSPEC::DIFFUSE,
                                   PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_PLIN, 0.0, 0.05, tex};
        footDust_[l] = AddParticleStream(&dust, at, up, &footDustLv_[l]);
        footDustAt_[l] = at;
    }
    dustContact_ = contact;
}

void Tantra::LiftSound(bool inside) {
    const double dt = oapiGetSimStep();
    const tantra::CarriagePose& p = carriage_.Pose();
    const double P = carriage_.Progress(), gear = carriage_.Gear();
    const bool busy = carriage_.Busy();
    if (liftP_ < -0.5 || dt <= 0.0) {                        // the first step: no blows for the loaded state
        liftP_ = P; liftGear_ = gear; liftBusy_ = busy;
        liftH_ = p.trunnionH; liftHip_ = p.hipS; liftTh_ = p.theta; liftMast_ = p.mastLen;
        return;
    }
    // the speed of the machine: trunnions up/down, along the rails, the turn (the nose ~60 m out), the telescopes
    const double v = (std::fabs(p.trunnionH - liftH_) + std::fabs(p.hipS - liftHip_) + 60.0 * std::fabs(p.theta - liftTh_) +
                      0.3 * std::fabs(p.mastLen - liftMast_)) / dt + 8.0 * std::fabs(gear - liftGear_) / dt;
    liftLevel_ += ((std::min)(1.0, v / 3.0) - liftLevel_) * (std::min)(1.0, dt / 0.6);   // the machines spin up / down
    // loudness where the listener is
    double outA = 0.0, outB = 0.0;                           // the steady sounds; the blows (heard twice as far)
    if (!inside) {
        VECTOR3 ear, cg;
        oapiCameraGlobalPos(&ear);
        GetGlobalPos(cg);
        const double d = length(ear - cg), air = (std::min)(1.0, std::sqrt((std::max)(0.0, GetAtmDensity()) / 1.2));
        outA = air / (1.0 + d / 500.0);
        outB = air / (1.0 + d / 1000.0);
    }
    const int io = inside ? 1 : 0;                           // _EXT / _INT pairs
    const double vol = inside ? 1.0 : outA;
    auto loop = [&](int base, bool on, double v0) {
        const int id = base + io, other = base + 1 - io;
        if (sound_->IsWavPlaying(other)) sound_->StopWav(other);
        if (on && v0 * vol > 0.01) sound_->PlayWav(id, true, float((std::min)(1.0, v0 * vol)));
        else if (sound_->IsWavPlaying(id)) sound_->StopWav(id);
    };
    // the hum: while the carriage or the gear works; the pitch with the speed (heavy machines turn slowly)
    const bool working = busy || liftLevel_ > 0.03;
    loop(SND_LIFT_HUM_EXT, working, 0.35 + 0.50 * liftLevel_);   // under the blows: they stand out
    if (working) sound_->SetPlaybackSpeed(SND_LIFT_HUM_EXT + io, float(0.72 + 0.45 * liftLevel_));
    // the struts groan while the weight passes between the blades and the stern legs
    loop(SND_LIFT_STRUT_EXT, busy && P > 4.02 && P < 4.98 && GroundContact(), 0.85);
    // the four stern legs swinging out of the nacelles and running down (or back)
    loop(SND_LIFT_LEGS_EXT, busy && P > 3.02 && P < 3.9, 0.9);
    // blows - not under time warp (a burst of them would be noise)
    const bool warp = oapiGetTimeAcceleration() > 2.0;
    auto blow = [&](int base, double v0) {
        const double vb = inside ? 1.0 : outB;
        if (warp || v0 * vb < 0.01) return;
        sound_->PlayWav(base + io, false, float((std::min)(1.0, v0 * vb)));
    };
    auto crossed = [&](double x) { return (liftP_ < x && P >= x) || (liftP_ > x && P <= x); };
    const bool up = P > liftP_;
    if (busy && !liftBusy_) blow(SND_LIFT_CLUNK_EXT, 1.0);                       // brakes off, the drives take it
    if (!busy && liftBusy_) blow(SND_LIFT_CLUNK_EXT, 1.0);                       // the machine stops and locks
    if ((up && crossed(1.0)) || (!up && crossed(2.0))) blow(SND_LIFT_RAIL_EXT, 1.0);   // the hull starts along the rails
    if ((up && crossed(2.0)) || (!up && crossed(1.0))) blow(SND_LIFT_CLUNK_EXT, 1.0);  // ... and stops on them
    if (up && crossed(1.5)) blow(SND_LIFT_CLUNK_EXT, 0.85);                       // the kangaroo lets go
    if (!up && crossed(1.5)) blow(SND_LIFT_BOOM_EXT, 1.0);                        // ... and sets its cup down
    if (crossed(3.0)) blow(SND_LIFT_CLUNK_EXT, 1.0);                             // the turn ends: the trunnions lock
    if (up && crossed(3.9)) blow(SND_LIFT_CUPS4_EXT, 1.0);                       // the four stern cups on the ground
    if (!up && crossed(3.9)) blow(SND_LIFT_CLUNK_EXT, 0.7);                      // ... and off it
    if ((up && crossed(5.75)) || (!up && crossed(5.95))) blow(SND_LIFT_STOW_EXT, 1.0);  // a blade into / out of its pocket
    if (!up && crossed(5.0)) blow(SND_LIFT_CUPS2_EXT, 1.0);                      // lowering: the blades' cups on the ground
    // the stern legs' telescopes: four sections each, every one to its stop
    for (double s : {3.35, 3.55, 3.75})
        if (crossed(s)) { blow(SND_LIFT_STAGE_EXT, 1.0); break; }
    // the locking bolts - the protection of every joint that carries the ship: the stern legs' stowage locks open
    // (and close again when they are home), the legs lock under the load once the ship stands on them, the whole
    // stand locks when the blades are home (and opens before the lowering)
    if (crossed(3.02) || crossed(4.95) || crossed(5.99)) blow(SND_LIFT_BOLTS_EXT, 1.0);
    // landing on the legs from flight: the cups strike as hard as the descent was fast
    const bool contact = GroundContact();
    if (contact && !liftContact_ && gear >= 0.99 && !busy && sinceContactPrev_ > 3.0)
        blow(carriage_.Set() == tantra::Carriage::FlightSet::Standing ? SND_LIFT_CUPS4_EXT : SND_LIFT_CUPS2_EXT,
             (std::max)(0.4, (std::min)(1.0, liftVz_ / 3.0)));
    {
        VECTOR3 gs;
        GetGroundspeedVector(FRAME_HORIZON, gs);
        if (!contact) liftVz_ = (std::max)(0.0, -gs.y);   // the sink rate just before the touch
    }
    sinceContactPrev_ = contact ? 0.0 : sinceContactPrev_ + dt;
    liftContact_ = contact;
    if ((liftGear_ < 1.0 && gear >= 1.0) || (liftGear_ > 0.0 && gear <= 0.0)) blow(SND_LIFT_CLUNK_EXT, 0.7);   // gear locked
    // the blade telescopes: six stages, each reaching its stop and locking (out) or unlocking (in) on the way
    for (double s : {32.0, 45.3, 58.5, 71.8})
        if ((liftMast_ < s && p.mastLen >= s) || (liftMast_ > s && p.mastLen <= s)) { blow(SND_LIFT_STAGE_EXT, 1.0); break; }
    liftP_ = P; liftGear_ = gear; liftBusy_ = busy;
    liftH_ = p.trunnionH; liftHip_ = p.hipS; liftTh_ = p.theta; liftMast_ = p.mastLen;
}

// --- Actions (shared by keyboard and panel) ---------------------------------------

void Tantra::ActToggleEngineSet() { ActSelectEngine(engineSet_ == EngineSet::Planetary); }

void Tantra::ActSelectEngine(bool anamezon) {
    const EngineSet want = anamezon ? EngineSet::Anamezon : EngineSet::Planetary;
    if (want != engineSet_) {
        SetThrusterGroupLevel(THGROUP_MAIN, 0.0);
        BindMainGroup(want);
    }
    if (anamezon) {
        Message("Анамезон выбран: тяга на рукояти только после ПОДАЧИ (J)", "Anamezon selected: throttle only after FEED (J)");
    } else if (podAngle_ >= 45.0 || podTarget_ >= 45.0) {
        Message("Маршевые: ПЛАНЕТАРНЫЕ (кольцо кормы), гондолы на висении", "Main: PLANETARY stern ring, pods on hover");
    } else {
        Message("Маршевые: ПЛАНЕТАРНЫЕ", "Main: PLANETARY");
    }
}

void Tantra::ActIgnitionTo(int stage) {
    if (stage <= 0) {
        ignition_.Shutdown();
        Message("Анамезонные моторы: останов", "Anamezon engines: shutdown");
        return;
    }
    if (ignition_.Target() == IgnStage::Off && !drive_.CanRaiseField(ignition_.FieldLevel())) {
        Message("Накопитель поля %.0f%% - мало для подъёма поля камер",
                "Field store %.0f%% - too low to raise the chamber field", 100.0 * drive_.StoreFraction());
        return;
    }
    ignition_.SetTarget(static_cast<IgnStage>((std::min)(stage, static_cast<int>(IgnStage::Feed))));
    char ru[128], en[128];
    std::snprintf(ru, sizeof ru, "Анамезонные моторы: цель - %s", StageName(ignition_.Target(), true));
    std::snprintf(en, sizeof en, "Anamezon engines: target - %s", StageName(ignition_.Target(), false));
    Message("%s", "%s", ru, en);
}

void Tantra::ActIgnitionStep() { ActIgnitionTo(static_cast<int>(ignition_.Target()) + 1); }

// One override for both interlocks (altitude and radiation): the commander's decision.
void Tantra::ActToggleOverride() {
    hotStartOverride_ = !hotStartOverride_;
    if (hotStartOverride_)
        Message("Блокировки СНЯТЫ начальником: опасность для экосистем и экипажей на вашей ответственности",
                "Interlocks OVERRIDDEN: danger to ecosystems and crews is on you");
    else Message("Блокировки высоты и радиации включены", "Altitude and radiation interlocks armed");
}

double Tantra::SafetyLevelCap() const {
    if (hotStartOverride_ || !safety_) return 1.0;
    return safety_->LevelCap();
}

void Tantra::ActToggleGLimit() {
    gLimitOn_ = !gLimitOn_;
    if (gLimitOn_) Message("Ограничитель перегрузки ВКЛ, %.1f g", "G-limiter ON, %.1f g", gLimit_);
    else Message("Ограничитель перегрузки ВЫКЛ", "G-limiter OFF");
}

void Tantra::ActCycleGLimit() {
    static const double steps[] = {1.5, 3.0, 5.0, 10.0};
    double next = steps[0];
    for (double v : steps) {
        if (v > gLimit_ + 1e-6) { next = v; break; }
    }
    gLimit_ = next;
    Message("Предел ощущаемой перегрузки %.1f g", "Felt load limit %.1f g", gLimit_);
}

void Tantra::ActNextTrap() {
    activeTrap_ = (activeTrap_ + 1) % sp::kTrapCount;
    SelectActiveTrap();
    Message("Подача анамезона из ловушки %d", "Anamezon feed from trap %d", activeTrap_ + 1);
}

void Tantra::ActToggleAirlock() {
    crew_.SetAirlockOpen(!crew_.AirlockOpen());
    if (crew_.AirlockOpen()) Message("Шлюз открыт", "Airlock open");
    else Message("Шлюз закрыт", "Airlock closed");
}

void Tantra::ActLift() {
    const bool lower = !lift_.Lowering();
    if (lower) {
        const bool level = carriage_.Progress() <= 0.0 && carriage_.Gear() >= 1.0 && GroundContact() && !carriage_.Busy();
        if (!level) {
            Message("Лифт - только лёжа на опорах (лафет в покое)", "Lift: lying on the gear only, the carriage at rest");
            return;
        }
    }
    if (!lift_.Command(lower)) return;
    if (lower) Message("Лифт: дверь, консоль, мачта до грунта, кабина вниз", "Lift: door, arm out, mast to the ground, cabin down");
    else Message("Лифт: кабина вверх, мачта, консоль, дверь", "Lift: cabin up, mast, arm in, door");
}

double Tantra::OutsideP() const { return (std::max)(0.0, (std::min)(1.0, GetAtmPressure() / 101325.0)); }

bool Tantra::PressureEqual() const { return std::fabs(zonePressure_ - OutsideP()) < 0.03; }

bool Tantra::LiftGo(bool lower) {
    if (lower) {
        const bool level = carriage_.Progress() <= 0.0 && carriage_.Gear() >= 1.0 && GroundContact() && !carriage_.Busy();
        if (!level) { Message("Лифт - только лёжа на опорах (лафет в покое)", "Lift: lying on the gear only, the carriage at rest"); return false; }
    }
    if (!lift_.Command(lower)) return false;
    return true;
}

void Tantra::LiftCall() {
    if (lift_.Stowed() && !lift_.Lowering() && trip_ == 0) { trip_ = 1; doorBT_ = 1.0; Message("Лифт: кабина идёт вниз", "Lift: the cabin is coming down"); }
}

void Tantra::PanelStep(double dt) {
    if (panelMsgT_ > 0.0) panelMsgT_ -= dt;
    if (alertT_ > 0.0 && (alertT_ -= dt) <= 0.0) alertBtn_ = -1;
    if (overrideT_ > 0.0) overrideT_ -= dt;
    if (panelErrT_ > 0.0 && (panelErrT_ -= dt) <= 0.0) panelErr_[0] = 0;
    if (suitMsgT_ > 0.0 && (suitMsgT_ -= dt) <= 0.0) { suitMsg_[0] = 0; suitOk_ = -1; }
    const bool home = lift_.Stowed() && !lift_.Lowering();          // the cabin stands in the cell
    if (!home) { doorBT_ = 1.0; zonePressureTarget_ = OutsideP(); }   // away: door B shut, the cell open to the outside
    switch (trip_) {
        case 1:                                                          // going down: door B shuts, the air goes, then the cabin
            if (doorB_ >= 1.0) {
                zonePressureTarget_ = OutsideP();
                if (PressureEqual() || OutsideAirOk()) cabDoorT_ = 1.0;     // then the cabin's own doors
                if (cabDoor_ >= 1.0) {
                    if (LiftGo(true)) trip_ = 3;
                    else { trip_ = 0; std::snprintf(panelErr_, sizeof panelErr_, "ОТКАЗ: лифт только лёжа на опорах"); panelErrT_ = 10.0; }
                }
            }
            break;
        case 3: if (lift_.AtGround() || !lift_.Lowering()) { trip_ = 0; cabDoorT_ = 0.0; } break;   // down: the doors open
        case 4: cabDoorT_ = 1.0; if (cabDoor_ >= 1.0) { if (LiftGo(false)) trip_ = 2; else { trip_ = 0; cabDoorT_ = 0.0; } } break;
        case 2:                                                          // going up: home, the air back, door B opens
            if (home) { zonePressureTarget_ = 1.0; if (zonePressure_ >= 0.999) { doorBT_ = 0.0; cabDoorT_ = 0.0; trip_ = 0; } }
            break;
        default:
            if (home) { zonePressureTarget_ = 1.0; if (zonePressure_ >= 0.999) { doorBT_ = 0.0; cabDoorT_ = 0.0; } }   // at rest in the cell: the air in, the doors open
            else if (lift_.AtGround()) cabDoorT_ = 0.0;
            break;
    }
    const double dc = cabDoorT_ - cabDoor_;
    if (std::fabs(dc) > 1e-9) cabDoor_ += (dc > 0 ? 1.0 : -1.0) * (std::min)(std::fabs(dc), dt / 1.5);   // 1.5 s
    const double dd = doorBT_ - doorB_;
    if (std::fabs(dd) > 1e-9) doorB_ += (dd > 0 ? 1.0 : -1.0) * (std::min)(std::fabs(dd), dt / 2.0);   // 2 s
    const double d = zonePressureTarget_ - zonePressure_;
    if (std::fabs(d) > 1e-9) zonePressure_ += (d > 0 ? 1.0 : -1.0) * (std::min)(std::fabs(d), 0.125 * dt);   // ~8 s for one atmosphere
}

void Tantra::PanelPress(int which, int personId) {
    messageRu_[0] = 0;
    struct After { Tantra* t; int w; ~After() { if (t->messageRu_[0]) { std::snprintf(t->panelMsg_, sizeof t->panelMsg_, "%s", t->messageRu_); t->panelMsgBtn_ = w; t->panelMsgT_ = 6.0; } } } after{this, which};
    const bool home = lift_.Stowed() && !lift_.Lowering();
    switch (which) {
        case 0: {                                                        // the lift zone: is my suit on
            if (OutsideAirOk()) { suitOk_ = 1; std::snprintf(suitMsg_, sizeof suitMsg_, "не нужен - воздух снаружи пригоден"); }
            else {
                const int w = interior_.SuitWorn(personId);
                suitOk_ = w == 1 ? 1 : 0;
                std::snprintf(suitMsg_, sizeof suitMsg_, "%s", w == 1 ? "надет, герметичен" : w == 0 ? "НЕ НАДЕТ" : "нет данных");
            }
            suitMsgT_ = 20.0;
            Message("Скафандр: %s", "Suit: %s", suitMsg_);
            break;
        }
        case 1:
            Message("Давление шлюза: %.2f атм, снаружи %.2f атм. Выравнивание - само, по кнопке ВНИЗ в кабине", "Lock pressure %.2f atm, outside %.2f atm",
                    zonePressure_, OutsideP());
            break;
        case 2:                                                          // the lift zone: the cabin from the bottom
            if (lift_.AtGround() && trip_ == 0) { trip_ = 4; Message("Лифт: вызов, кабина поднимается", "Lift: called, the cabin rises"); }
            else if (home) Message("Кабина здесь: войдите в неё и нажмите ВНИЗ", "The cabin is here: step in and press DOWN");
            else Message("Кабина в пути", "The cabin is on its way");
            break;
        case 3: {                                                        // the cabin: DOWN
            if (!home) { Message(lift_.AtGround() ? "Кабина внизу: ВЫХОД - на грунт, ВВЕРХ - подъём" : "Кабина в пути", "The cabin is not up"); break; }
            if (trip_) break;
            int ids[8]; const int n = interior_.CabinPeople(ids, 8);
            if (!OutsideAirOk()) {
                char who[96] = {0};
                for (int i = 0; i < n; i++)
                    if (interior_.SuitWorn(ids[i]) != 1) {
                        char nm[48] = {0}; if (!interior_.PersonName(ids[i], nm, sizeof nm)) std::snprintf(nm, sizeof nm, "?");
                        std::snprintf(who + std::strlen(who), sizeof who - std::strlen(who), "%s%s", who[0] ? ", " : "", nm);
                    }
                if (who[0] && overrideT_ <= 0.0) {
                    alertBtn_ = 3; alertT_ = 3.0; overrideT_ = 15.0;
                    std::snprintf(panelErr_, sizeof panelErr_, "БЕЗ СКАФАНДРА: %s - ВНИЗ ещё раз: на свой риск", who); panelErrT_ = 15.0;
                    Message("Лифт: без скафандра - %s", "Lift: no suit - %s", who);
                    break;
                }
                if (who[0]) { std::snprintf(panelErr_, sizeof panelErr_, "РИСК: без скафандра - %s", who); panelErrT_ = 20.0; }
            }
            overrideT_ = 0.0;
            trip_ = 1; doorBT_ = 1.0;
            Message("Лифт: дверь шлюза закрывается", "Lift: door B shuts");
            break;
        }
        case 4:                                                          // the cabin: UP
            if (lift_.AtGround() && trip_ == 0) { trip_ = 4; Message("Лифт: подъём", "Lift: up"); }
            else if (home) Message("Кабина наверху: выход - пешком в зону лифта", "The cabin is up: walk out");
            break;
        case 5:                                                          // the cabin: OUT to the ground
            if (!lift_.AtGround()) { Message(home ? "Кабина наверху: выход - пешком в зону лифта" : "Кабина в пути", "Not at the ground"); break; }
            switch (crew_.EvaPerson(personId)) {
                case TantraCrew::EvaResult::Ok: Message("%s вышел на грунт", "%s steps out", crew_.LastName()); break;
                case TantraCrew::EvaResult::NotLanded: Message("Выход - только на грунте", "EVA on the ground only"); break;
                case TantraCrew::EvaResult::AirlockClosed: Message("Шлюз закрыт", "Airlock closed"); break;
                default: Message("Выход невозможен", "EVA not possible"); break;
            }
            break;
        default: break;
    }
}

// The air outside is fit to breathe (the user's rule: on the Earth one goes out without a suit; the checks pass by themselves)
bool Tantra::OutsideAirOk() const {
    OBJHANDLE ref = GetSurfaceRef(); char nm[64] = {0};
    if (ref) oapiGetObjectName(ref, nm, 63);
    return std::strcmp(nm, "Earth") == 0 && GetAtmPressure() > 70000.0;
}

// The lift zone's screen (TantraInterior draws it): lines, '!' red, '+' green, '*' amber.
void Tantra::PanelStatus(char* out, int n) const {
    const bool home = lift_.Stowed() && !lift_.Lowering();
    const bool pm = std::fabs(zonePressureTarget_ - zonePressure_) > 1e-6;
    const char* door = doorB_ <= 0.0 ? "+ОТКРЫТА" : doorB_ >= 1.0 ? " ЗАКРЫТА" : "*ДВИЖЕТСЯ";
    const char* cab = lift_.AtGround() ? " ВНИЗУ" : home ? (trip_ ? "*ПОДГОТОВКА К СПУСКУ" : "+В ЯЧЕЙКЕ") : lift_.Lowering() ? "*СПУСК" : "*ПОДЪЁМ";
    char suit[96];
    if (suitMsg_[0]) std::snprintf(suit, sizeof suit, "%c%s", suitOk_ == 1 ? '+' : '!', suitMsg_);
    else std::snprintf(suit, sizeof suit, "%s", OutsideAirOk() ? "+НЕ ТРЕБУЕТСЯ" : " НЕ ПРОВЕРЕН");
    char msg[200];
    if (panelErr_[0]) std::snprintf(msg, sizeof msg, "!%s", panelErr_);
    else std::snprintf(msg, sizeof msg, "%s", lift_.AtGround() ? " КАБИНА ВНИЗУ: ВЫЗОВ - КНОПКА 3" : home && !trip_ ? " ВХОД В КАБИНУ - ДВЕРЬ ШЛЮЗА" : " ");
    std::snprintf(out, size_t(n), "ШЛЮЗ 1  ·  ЗОНА ЛИФТА\tП-1.2\nДАВЛЕНИЕ ШЛЮЗА\t%c%.2f АТМ\nСНАРУЖИ\t %.2f АТМ%s\nДВЕРЬ ШЛЮЗА\t%s\nКАБИНА\t%s\nСКАФАНДР\t%s\n%s",
                  pm ? '*' : ' ', zonePressure_, OutsideP(), OutsideAirOk() ? ", ВОЗДУХ" : "", door, cab, suit, msg);
}

// The cabin's screen: what the cabin does and the command to give.
void Tantra::CabStatus(char* out, int n) const {
    const bool home = lift_.Stowed() && !lift_.Lowering();
    const char* st;
    if (lift_.AtGround()) st = "+ВНИЗУ";
    else if (trip_ == 1 && doorB_ < 1.0) st = "*ДВЕРЬ ШЛЮЗА ЗАКРЫВАЕТСЯ";
    else if (trip_ == 1) st = "*ВЫРАВНИВАНИЕ ДАВЛЕНИЯ";
    else if (!home) st = lift_.Lowering() ? "*СПУСК" : "*ПОДЪЁМ";
    else if (trip_ == 2 || zonePressure_ < 0.999) st = "*НАДДУВ ШЛЮЗА";
    else if (doorB_ > 0.0) st = "*ДВЕРЬ ШЛЮЗА ОТКРЫВАЕТСЯ";
    else st = "+ГОТОВА";
    int ids[8]; const int people = interior_.CabinPeople(ids, 8);
    char msg[200];
    if (panelErr_[0]) std::snprintf(msg, sizeof msg, "!%s", panelErr_);
    else std::snprintf(msg, sizeof msg, "%s", lift_.AtGround() ? " ВЫХОД НА ГРУНТ  ·  ПОДЪЁМ" : home && !trip_ && doorB_ <= 0.0 ? " СПУСК - КНОПКА СПУСК" : " ");
    std::snprintf(out, size_t(n), "КАБИНА ЛИФТА\tЛ-1\nСОСТОЯНИЕ\t%s\nВЫСОТА\t %.1f М\nСКОРОСТЬ\t %.1f М/С\nДАВЛЕНИЕ\t %.2f АТМ  (СНАР. %.2f)\nЛЮДЕЙ\t %d\n%s",
                  st, lift_.CabHeight(), std::fabs(lift_.CabSpeed()), zonePressure_, OutsideP(), people, msg);
}

int Tantra::PanelState(int which) const {
    if (which == alertBtn_ && alertT_ > 0.0) return 3;                   // refused: this one
    const bool home = lift_.Stowed() && !lift_.Lowering();
    switch (which) {
        case 0: return suitOk_ == 1 ? 1 : 0;
        case 1: return std::fabs(zonePressureTarget_ - zonePressure_) > 1e-6 ? 2 : 0;
        case 2: return lift_.AtGround() && trip_ == 0 ? 1 : (lift_.Moving() || trip_) ? 2 : 0;
        case 3: return trip_ == 1 || trip_ == 3 ? 2 : (home && trip_ == 0 && doorB_ <= 0.0) ? 1 : 0;
        case 4: return trip_ == 2 && !home ? 2 : (lift_.AtGround() && trip_ == 0) ? 1 : 0;
        case 5: return lift_.AtGround() ? 1 : 0;
        default: return 0;
    }
}

void Tantra::PanelLabel(int which, char* out, int n) const {
    static const char* const kLab[6] = {"1 - проверка скафандра", "2 - давление шлюза", "3 - вызов кабины", "ВНИЗ - спуск", "ВВЕРХ - подъём", "ВЫХОД - на грунт"};
    const char* s = (which >= 0 && which < 6) ? kLab[which] : "";
    if (which == panelMsgBtn_ && panelMsgT_ > 0.0 && panelMsg_[0]) std::snprintf(out, size_t(n), "%s", panelMsg_);
    else std::snprintf(out, size_t(n), "%s", s);
}

void Tantra::ActEva() {
    if (GroundContact() && rovers_ < 0.99 && !lift_.AtGround()) {
        Message("Выход на грунт: опустите лифт шлюза (Shift+A) или платформу ангара (O, Shift+O)",
                "Ground EVA: lower the airlock lift (Shift+A) or the hangar platform (O, Shift+O)");
        return;
    }
    switch (crew_.Eva(selectedCrew_)) {
        case TantraCrew::EvaResult::Ok:
            Message("%s вышел наружу", "%s is outside", crew_.LastName());
            selectedCrew_ = 0;
            break;
        case TantraCrew::EvaResult::NoOne: Message("На борту никого нет", "Nobody aboard"); break;
        case TantraCrew::EvaResult::AirlockClosed: Message("Шлюз закрыт - сначала откройте", "Airlock closed - open it first"); break;
        case TantraCrew::EvaResult::NotLanded: Message("Выход - только на грунте (платформа ангара)", "EVA from the hangar platform on the ground only"); break;
        case TantraCrew::EvaResult::NoCrewModule: Message("Модуль экипажа (OrbiterCrew) не найден", "Crew module (OrbiterCrew) not found"); break;
        default: Message("Выход невозможен", "EVA not possible"); break;
    }
}

void Tantra::ActSelectCrew(int delta) {
    const int total = crew_.Total();
    if (total == 0) {
        Message("На борту никого нет", "Nobody aboard");
        return;
    }
    selectedCrew_ = (std::max)(0, (std::min)(total - 1, selectedCrew_ + delta));
    Message("Выбран: %s (%s), %d лет", "Selected: %s (%s), age %d", crew_.Name(selectedCrew_), crew_.Role(selectedCrew_),
            crew_.Age(selectedCrew_));
}

// --- Undercarriage, crests, hangar ------------------------------------------------

// One place maps the carriage and the ship systems onto the mesh and the touchdown points.
void Tantra::UpdateGear(double simdt) {
    // The carriage moves the touchdown points; keep every step small (time warp included).
    const double dt = (std::min)(simdt, 0.05);
    UpdateWarpFreeze(dt);
    UpdateBalance(dt);
    carriage_.SetGrounded(GroundContact() || sinceContact_ < 3.0, !tuckSet_ && simdt > 0.0);
    // the turn: 45 s on Earth and heavier; the feet hold the turn's reaction by the weight, the inertia is the same
    // everywhere - lighter worlds turn slower (sqrt). On the Moon the ship lifts off lying on the pods anyway.
    if (simdt > 0.0) carriage_.SetTurnTime((std::min)(150.0, 45.0 * std::sqrt((std::max)(1.0, G0 / (std::max)(0.1, LocalG())))));
    if (simdt > 0.0) tuckSet_ = true;
    carriage_.Update(balHold_ || frozen_ ? 0.0 : balRamp_ * (hipCatcher_ ? 0.6 * dt : dt), frameS_);  // paused while swaying or frozen; slower on the catchers
    auto step = [dt](double v, double t, double rate) {
        return v < t ? (std::min)(t, v + rate * dt) : (std::max)(t, v - rate * dt);
    };
    tuck_ = step(tuck_, crestsFolded_ ? 1.0 : 0.0, 0.1);
    // Wings: folding - the inner panels stand up first, then the outer panels fold down along them (outboard);
    // unfolding - the outer panels out in line first, then the inner panels down to their mode. (Lowering the
    // inner panels with the outer ones folded would swing those into the hull.)
    {
        namespace m = tantra::mesh;
        const double inT = wingMode_ == 2 ? 1.0 : (wingMode_ == 1 ? m::kWingRaiseDeg / m::kWingFoldDeg : 0.0);
        const double outT = wingMode_ == 2 ? 1.0 : 0.0;
        if (outT > wingOut_) {
            wingIn_ = step(wingIn_, inT, 0.1);
            if (wingIn_ >= 0.98) wingOut_ = step(wingOut_, outT, 0.12);
        } else {
            wingOut_ = step(wingOut_, outT, 0.12);
            if (wingOut_ <= 0.02) wingIn_ = step(wingIn_, inT, 0.1);
        }
    }
    hangar_ = step(hangar_, hangarT_, 0.08);
    rovers_ = step(rovers_, hangar_ > 0.97 ? roversT_ : 0.0, 0.05);
    // Stern interlocks. A: the anamezon irises open only with the marching cup home and its iris shut; B: the well
    // iris opens and the cup runs out only with the anamezon irises shut. The cup wants out whenever the planetary
    // set is selected and the ship is not standing on the legs with the engines idle.
    const bool anaActive = ignition_.Target() != IgnStage::Off || ignition_.FieldLevel() > 0.0;
    const bool wantMarch = !anaActive && engineSet_ == EngineSet::Planetary;
    const bool anaAllowed = marchOut_ <= 0.0 && irisMarch_ <= 0.0;
    irisAna_ = step(irisAna_, anaActive && anaAllowed ? 1.0 : 0.0, 0.25);
    const bool marchAllowed = irisAna_ <= 0.0;
    if (wantMarch && marchAllowed) {
        irisMarch_ = step(irisMarch_, 1.0, 0.5);
        if (irisMarch_ >= 1.0) marchOut_ = step(marchOut_, 1.0, 0.1);
    } else {
        marchOut_ = step(marchOut_, 0.0, 0.1);
        if (marchOut_ <= 0.0) irisMarch_ = step(irisMarch_, 0.0, 0.5);
    }
    // nose petals: open while the retro group is used (anamezon feeding)
    const bool retroUsed = AnaIsMain() && GetThrusterGroupLevel(THGROUP_RETRO) > 0.0;
    irisNose_ = step(irisNose_, retroUsed || (AnaIsMain() && irisNose_ > 0.0 && GetThrusterGroupLevel(THGROUP_RETRO) > 0.0) ? 1.0 : 0.0, 0.3);
    const bool level = carriage_.Progress() <= 0.0 && carriage_.Gear() >= 1.0 && GroundContact();
    airlockUp_ = step(airlockUp_, level ? 0.0 : 1.0, 0.1);
    // main airlock crew lift: only lying on the gear with the carriage at rest; it follows the real height of the sill
    // lying on the gear at rest; a bounce of the contacts while settling (a moment off the ground) is not a reason to go home
    const bool liftOk = carriage_.Progress() <= 0.0 && carriage_.Gear() >= 1.0 && !carriage_.Busy() && !frozen_ &&
                        (GroundContact() || sinceContact_ < 3.0);
    lift_.Update(simdt, liftOk, -carriage_.Pose().trunnionH);
    PanelStep(simdt);
    if (lift_.AtGround() && !liftWasDown_) {
        crew_.SetAirlockOpen(true);
        Message("Кабина лифта внизу: выход на грунт (E), вход - у двери кабины (F)", "Lift cabin at the ground: EVA with E, board at its door with F");
    } else if (!lift_.AtGround() && liftWasDown_ && !lift_.Lowering()) {
        Message("Лифт поднимается", "Lift rising");
    }
    liftWasDown_ = lift_.AtGround();
    {   // every stage change of the crew lift goes to Orbiter.log (debugging without the screen)
        static const char* lastStage = nullptr;
        const char* st = lift_.Stage();
        if (st != lastStage) {
            oapiWriteLogV("Tantra: lift %s (door %.2f arm %.2f mast %.2f cabin %.2f)", st, lift_.Door(), lift_.Out(), lift_.Mast(), lift_.Down());
            lastStage = st;
        }
    }

    TantraGear::Extras ex;
    ex.lockDoor = lift_.Door();
    ex.lockOut = lift_.Out();
    ex.lockMast = lift_.Mast();
    ex.lockDown = lift_.Down();
    ex.tuck = tuck_;
    ex.wingIn = wingIn_;
    ex.wingOut = wingOut_;
    ex.podSwivel = podAngle_ / sp::kPodSwivelMaxDeg;
    ex.podStow = 1.0 - podOut_;
    ex.elevon[0] = elevon_[0];
    ex.elevon[1] = elevon_[1];
    ex.bodyFlap = bodyFlap_;
    ex.irisAna = irisAna_;
    ex.irisMarch = irisMarch_;
    ex.marchOut = marchOut_;
    ex.irisNose = irisNose_;
    ex.hangar = hangar_;
    ex.rovers = rovers_;
    ex.bayDoors = bayDoors_;
    for (int i = 0; i < 4; ++i) ex.trapHidden[i] = !trapPresent_[i];
    ex.liftY[0] = liftY_[0];
    ex.liftY[1] = liftY_[1];
    const tantra::CarriagePose& p = carriage_.Pose();
    // Leg systems: the legs of the current ground set touch with their struts out (unloaded), the weight pushes the
    // rods in (the touchdown points follow at 0.45 m/s); soles conform, lock, then the anchors go in.
    bool inSet[7] = {};
    {
        for (int i = 0; i < p.nTouch; ++i) {
            const int l = TouchLeg(i, p);
            if (l >= 0) inSet[l] = true;
        }
        const bool down = carriage_.Gear() >= 0.5, contact = down && GroundContact();
        const double gLoc = simdt > 0.0 ? LocalG() : G0;       // while the scenario loads there is no gravity ref yet
        const bool resting = thrustAccel_ < 0.3 * gLoc;        // lift-off thrust pulls the anchors first
        solesCar_.Update(dt, contact && (inSet[0] || inSet[1] || inSet[6]), resting);
        solesStern_.Update(dt, contact && (inSet[2] || inSet[3] || inSet[4] || inSet[5]), resting);
        for (int l = 0; l < 7; ++l) {
            // Two struts in series (2026-10-04): the leg's own MR ankle strut (the user's design) and each petal's
            // strut (core/Foot). A pad's compression (last step's depth) is shared: the ankle takes the leg's common
            // part (its mean over the leg's petals, half of it - equal strokes, equal springs), each petal the rest -
            // its own ground. Unloaded both hang out; a failed petal strut: the petal on its stop.
            const double ext = l < 2 ? tantra::mesh::kStrutExtC : tantra::mesh::kStrutExtS;
            const double stroke = l < 2 ? tantra::mesh::kStrokeC : tantra::mesh::kStrokeS;
            const double pose = (stroke - ext) / stroke;
            const double ankle = down && inSet[l] ? (std::min)(stroke, 0.5 * legPen_[l]) : ext;   // compression of the ankle strut
            strut_[l] = step(strut_[l], ext - ankle, GroundContact() ? 3.0 : 0.5);            // travel from the static sag
            ex.strut[l] = down && inSet[l] ? (strut_[l] + stroke - ext) / stroke : pose;
            for (int c = 0; c < tantra::mesh::kCellN; ++c) {
                double st = pose;                                   // folded or not in the set: the mesh pose (the fold needs it)
                if (down && inSet[l]) {
                    if (feet_.CellLost(l, c) || feet_.RibLost(l, c)) st = 0.0;
                    else {
                        // while the legs carry the moving ship, every petal works on its own little: the load wanders
                        if (carriage_.Busy() && GroundContact() && dt > 0.0) {
                            windRng_ = windRng_ * 1664525u + 1013904223u;
                            const double u = ((windRng_ >> 8) / 16777216.0) * 2.0 - 1.0;
                            microJ_[l][c] += -microJ_[l][c] * dt / 0.5 + 0.12 * std::sqrt(dt) * u;
                        } else microJ_[l][c] *= (std::max)(0.0, 1.0 - dt / 0.5);
                        const double own = (std::max)(0.0, (std::min)(stroke, cellPen_[l][c] - ankle + microJ_[l][c]));
                        cellD_[l][c] = step(cellD_[l][c], ext - own, 3.0);
                        st = (cellD_[l][c] + stroke - ext) / stroke;
                    }
                } else cellD_[l][c] = 0.0;
                ex.cell[l][c] = st;
            }
        }
        regen_.Update(dt, GetMass() * gLoc, p.trunnionH, carriage_.Busy() && contact);
    }

    // Aerodynamic state: what the crests, fin, gear and pods present to the flow.
    {
        const double fold = (std::max)(tuck_, p.tuck);
        namespace dm = tantra::damage;
        aeroFin_ = (1.0 - fold) * (damage_.Lost(dm::kFin) ? 0.0 : 1.0);
        const double raise = (std::max)(wingIn_, (std::max)(0.0, 2.0 * p.tuck - 1.0)) * tantra::mesh::kWingFoldDeg * RAD;
        const double inLine = 1.0 - (std::max)(wingOut_, (std::min)(1.0, 2.0 * p.tuck));
        aeroCrest_ = std::cos(raise) * std::cos(raise) * (0.45 + 0.55 * inLine) *
                     ((damage_.Lost(dm::kCrestPort) ? 0.0 : 0.5) + (damage_.Lost(dm::kCrestStbd) ? 0.0 : 0.5));
        const double legs = carriage_.Gear();  // two blades with their umbrellas, the kangaroo leg
        aeroGearArea_ = legs * (2.0 * (6.0 * (std::max)(0.0, p.mastLen - 2.0) + 0.3 * PI * 9.8 * 9.8) + 2.2 * 10.0 + 1.6 * 20.0 + 0.3 * PI * 36.0) +
                        podOut_ * 4.0 * (5.2 * 1.9 + 6.3 * 2.5);
    }
    // Blade drives: each side lags the trunnion motion (0.30 s / 0.42 s servo time) plus a little
    // band-limited noise while moving; the lag becomes a height error of that side's pads.
    {
        const double h = p.trunnionH;
        const double hDot = (lastTrunnionH_ >= 0.0 && dt > 0.0) ? (h - lastTrunnionH_) / dt : 0.0;
        lastTrunnionH_ = h;
        const bool moving = simdt > 0.0 && carriage_.Busy() && GroundContact();
        auto gauss = [this]() {
            driveRng_ = driveRng_ * 1664525u + 1013904223u;
            const double u1 = ((driveRng_ >> 8) + 1.0) / 16777217.0;
            driveRng_ = driveRng_ * 1664525u + 1013904223u;
            const double u2 = (driveRng_ >> 8) / 16777216.0;
            return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * PI * u2);
        };
        const double servo[2] = {0.30, 0.42};
        for (int i = 0; i < 2; ++i) {
            if (dt > 0.0) {
                driveNoise_[i] += -driveNoise_[i] * dt / 1.5 + (moving ? 0.03 * std::sqrt(2.0 * dt / 1.5) * gauss() : 0.0);
                // a lagging leg is short: its pad sits higher (not under the ground mechanism: the drives run in step)
                const double target = moving && !mechOn_ ? servo[i] * hDot + driveNoise_[i] : 0.0;
                driveLag_[i] += (target - driveLag_[i]) * (std::min)(1.0, dt / 0.7);
            }
        }
    }
    ex.bladeLag[0] = driveLag_[0];
    ex.bladeLag[1] = driveLag_[1];
    if (gear_) {
        // the hull sways about the trunnions over standing blades: the blades turn against it, the feet stay put
        tantra::CarriagePose pv = p;
        if (mechOn_) pv.mastPitch -= mechSway_;
        gear_->Apply(pv, frameS_, ex);
    }
    VECTOR3 t[kMaxPads];
    // Retune the suspension to the mass (cassettes in / out) and when the settling period ends.
    bool changed = std::fabs(GetMass() - touchMass_) > 0.05 * touchMass_ || (settleTimer_ <= 0.0) != touchSettled_;
    VECTOR3 upL = _V(0, 1, 0);
    if (simdt > 0.0) HorizonInvRot(_V(0, 1, 0), upL);  // not while the scenario loads: no horizon frame yet
    touchOfs_ = upL * terrainOfs_;  // refined terrain: points raised, relaxing (WatchTerrain)
    int nt = 0;
    double mu[kMaxPads], muLng[kMaxPads];
    int legOf[kMaxPads], cellOf[kMaxPads];
    double padVin[kMaxPads] = {}, padPer[kMaxPads] = {};   // compression speed; the pad's static load
    // While the ship moves on the columns (lift, turn, lowering) it turns about the trunnions and the legs turn in them:
    // the pads stand still. Orbiter takes the touchdown points as fixed to the hull, so the turn would drag them across
    // the ground at w * 70 m and friction (0.9 x weight at 70 m) would hold the ship back until it falls. Along the
    // hull the pads therefore carry no friction while the carriage moves; across it they keep their full grip.
    const bool swivel = (p.onColumns || p.columnShare > 0.0) && carriage_.Busy();
    {
        // Every foot of the set stands on its 12 separate petals (core/Foot): a pad under each petal's centre of
        // pressure on a ring round the foot's centre (the carriage gives the centre; the petals' directions come from
        // the mesh in the deployed pose and turn with the hull's pitch on the columns). A petal's pad sits its strut's
        // static sag under the sole; a failed strut leaves the petal on its stop (rigid); a petal broken off leaves
        // nothing. A lost leg (ankle, stage joint) carries nothing: the ship settles onto the hull.
        VECTOR3 centre[7];
        int cnt[7] = {};
        for (int l = 0; l < 7; ++l) centre[l] = _V(0, 0, 0);
        for (int i = 0; i < p.nTouch; ++i) {
            const int l = TouchLeg(i, p);
            if (l >= 0) { centre[l] += _V(p.touch[i].x, p.touch[i].y, p.touch[i].z); ++cnt[l]; }
        }
        for (int l = 0; l < 7; ++l) if (cnt[l]) centre[l] = centre[l] / cnt[l];
        const double th = p.onColumns ? p.theta : 0.0;
        auto cellPos = [&](int l, int c) {
            const tantra::mesh::V& d = tantra::mesh::kCellDir[l][c];
            const VECTOR3 dir = _V(d.x, d.y * std::cos(th) - d.z * std::sin(th), d.y * std::sin(th) + d.z * std::cos(th));
            return centre[l] + dir * tantra::mesh::kCellRc[l < 2 ? 0 : (l == 6 ? 2 : 1)];
        };
        bool used[7][12] = {};
        auto legGone = [&](int l) { return damage_.Lost(tantra::damage::kLegPort + l); };
        auto push = [&](int l, int c, const VECTOR3& pt) {
            if (nt >= kMaxPads) return;
            // a petal whose strut failed sits on its end stop: rigid, the rest of the stroke higher than the sole
            const bool bare = l >= 0 && feet_.CellLost(l, c);
            const double ext = l < 2 ? tantra::mesh::kStrutExtC : tantra::mesh::kStrutExtS;
            const double str = l < 2 ? tantra::mesh::kStrokeC : tantra::mesh::kStrokeS;
            // the ankle strut's and the petal strut's static sags; on its stop the petal strut has none left
            const double rod = l < 0 ? 0.0 : bare ? ext - (str - ext) : 2.0 * ext;
            mu[nt] = l < 0 ? 0.5 : (l < 2 || l == 6 ? solesCar_ : solesStern_).Mu();
            muLng[nt] = (swivel && l >= 0 && l < 2) || (carriage_.Busy() && l == 6 && p.columnShare > 0.0) ? 0.01 : mu[nt];
            const double lag = l == 0 ? driveLag_[0] : (l == 1 ? driveLag_[1] : 0.0);   // blade drives only
            t[nt] = pt + touchOfs_ - upL * rod + upL * lag;
            legOf[nt] = l;
            cellOf[nt] = c;
            if (nt >= nTouch_ || length(t[nt] - touch_[nt]) > 1e-4 || std::fabs(mu[nt] - touchMu_[nt]) > 0.02 ||
                std::fabs(muLng[nt] - touchMuLng_[nt]) > 0.02)
                changed = true;
            ++nt;
            if (l >= 0) { used[l][c] = true; padPos_[l][c] = pt; }
        };
        // first the carriage's own first three, each as the cell of its leg nearest to it: Orbiter's ground plane
        // and its winding stay as the carriage made them
        for (int i = 0; i < p.nTouch; ++i) {
            const int l = TouchLeg(i, p);
            const VECTOR3 pi = _V(p.touch[i].x, p.touch[i].y, p.touch[i].z);
            if (l < 0) { push(-1, -1, pi); continue; }       // the belly set: hull points
            if (i >= 3 || legGone(l)) continue;
            int best = -1;
            double bd = -1e30;
            for (int c = 0; c < tantra::mesh::kCellN; ++c) {
                if (used[l][c] || feet_.RibLost(l, c)) continue;
                const double d = dotp(cellPos(l, c) - centre[l], pi - centre[l]);
                if (d > bd) { bd = d; best = c; }
            }
            if (best >= 0) push(l, best, cellPos(l, best));
        }
        for (int l = 0; l < 7; ++l) {
            if (!cnt[l] || legGone(l)) continue;
            for (int c = 0; c < tantra::mesh::kCellN; ++c)
                if (!used[l][c] && !feet_.RibLost(l, c)) push(l, c, cellPos(l, c));
        }
    }
    if (nt != nTouch_) changed = true;
    // The ankle struts (core/Legs, DESIGN_LOCAL «Голеностоп»): a gas spring that the weight compresses by 30 % of the
    // stroke (blades 1.5 m, stern and kangaroo 1.0 m) and an MR valve: compressing, it holds a constant force - the
    // weight plus 1.5 g - over the whole stroke (a = v^2/2S, no peak at the end); extending it is shut (no rebound).
    // Orbiter's contact is a linear spring-damper per pad: the valve is emulated by setting each pad's damping from its
    // compression speed every step (semi-active, as the real valve does). At rest the ship is held in Orbiter's landed
    // state and nothing of this runs.
    bool tunedNow = false;                   // the pads' springs are this step's: their forces are real (core/Foot)
    if (simdt > 0.0 && nt > 0 && !restLock_ && !frozen_ && !mechOn_) {
        tunedNow = true;
        VECTOR3 vL, w;
        GetGroundspeedVector(FRAME_LOCAL, vL);
        GetAngularVel(w);
        const double m = GetMass(), W = m * LocalG(), per = W / nt, mEff = m / nt;
        // each cell's static load: its leg's share of the weight (lying: the kangaroo its share, the blades the rest;
        // otherwise the legs of the set alike) over the leg's pads - the gas pressure is set per leg
        int padsOf[7] = {};
        bool kangIn = false;
        for (int i = 0; i < nt; ++i) if (legOf[i] >= 0) { ++padsOf[legOf[i]]; kangIn = kangIn || legOf[i] == 6; }
        int legsIn = 0;
        for (int l = 0; l < 7; ++l) legsIn += padsOf[l] > 0;
        const double kangW = kangIn ? carriage_.Statics(W, 0.0).kangaroo : 0.0;
        auto legW = [&](int l) {
            if (kangIn) return l == 6 ? kangW : (W - kangW) / (std::max)(1, legsIn - 1);
            return W / (std::max)(1, legsIn);
        };
        bool retune = false;
        for (int i = 0; i < nt; ++i) {
            const int l = legOf[i];
            const double stroke = l < 0 ? 0.0 : l < 2 ? tantra::legs::kStrokeCarriage : l == 6 ? tantra::legs::kStrokeKang : tantra::legs::kStrokeStern;
            const double perCell = l < 0 ? per : (std::max)(0.02 * W / 12.0, legW(l) / (std::max)(1, padsOf[l]));
            const VECTOR3 vp = vL + crossp(w, t[i]);
            const double vin = -dotp(vp, upL);                                   // + compressing
            padVin[i] = vin;
            padPer[i] = perCell;
            double kk, cc;
            if (stroke <= 0.0) { kk = per / 0.04; cc = 1.4 * std::sqrt(kk * mEff); }       // a hull pad: steel
            else if (feet_.CellLost(l, cellOf[i])) {                              // the petal on its stop: the ankle strut only
                kk = perCell / (tantra::legs::kStaticSag * stroke);
                cc = (std::min)(1.4 * std::sqrt(kk * mEff), 0.8 * mEff / (std::max)(1e-3, simdt));
            } else {
                kk = perCell / (tantra::legs::kStaticSag * 2.0 * stroke);            // ankle and petal struts in series
                const double crit = 2.0 * std::sqrt(kk * mEff);
                const double cMax = (std::min)(3.0 * crit, 0.8 * mEff / (std::max)(1e-3, simdt));   // stable steps
                if (vin > 0.05) cc = (std::min)(cMax, (std::max)(0.8 * crit, tantra::legs::kSoftG * perCell / vin));
                else if (vin < -0.02) cc = (std::min)(cMax, 2.0 * crit);               // the valve shut: no rebound
                else cc = 0.8 * crit;
            }
            if (std::fabs(cc - touchC_[i]) > 0.15 * touchC_[i] || std::fabs(kk - touchK_[i]) > 0.05 * touchK_[i]) retune = true;
            touchK_[i] = kk;
            touchC_[i] = cc;
        }
        touchTuned_ = true;
        if (retune) changed = true;
    }
    // at rest (landed): the struts' small settling must not hand it back to the springs every step
    if ((restLock_ || frozen_) && !carriage_.Busy() && nt == nTouch_) changed = false;
    if (changed) {
        for (int i = 0; i < nt; ++i) {
            touchMu_[i] = mu[i];
            touchMuLng_[i] = muLng[i];
        }
        SetSuspension(t, nt);
        for (int i = 0; i < nt; ++i) touch_[i] = t[i];
        nTouch_ = nt;
    }
    // the strut compression per leg: how deep its pads are in the ground (the rods show it next step)
    if (simdt > 0.0 && GetSurfaceRef()) {
        const OBJHANDLE ref = GetSurfaceRef();
        const double R0 = oapiGetSize(ref);
        for (int l = 0; l < 7; ++l) penLeg_[l] = 0.0;
        for (int l = 0; l < 7; ++l) for (int c = 0; c < 12; ++c) cellPen_[l][c] = 0.0;
        namespace ft = tantra::foot;
        ft::LegIn li[ft::kLegs];
        // only while the ship stands on its springs (not held landed, frozen, or placed by the ground mechanism while the
        // carriage moves - then a pad's depth is no force) and the springs were tuned for these very pads this step
        const bool live = tunedNow && !restLock_ && !frozen_ && !mechOn_ && !carriage_.Busy() && settleTimer_ <= 0.0 &&
                          GroundContact() && carriage_.Gear() >= 0.5;
        for (int i = 0; i < nt; ++i) {
            const int l = legOf[i];
            if (l < 0) continue;
            VECTOR3 g;
            Local2Global(t[i], g);
            double lg, lt, rd;
            oapiGlobalToEqu(ref, g, &lg, &lt, &rd);
            double pen = (std::max)(0.0, R0 + oapiSurfaceElevation(ref, lg, lt) - rd);
            if (pen > 4.0) pen = 0.0;    // deeper than any cell can go: an elevation tile swap, not the ground (WatchTerrain)
            penLeg_[l] = (std::max)(penLeg_[l], pen);
            const int c = cellOf[i];
            cellPen_[l][c] = pen;
            ft::CellIn& ci = li[l].cell[c];
            ci.on = pen > 0.0;
            ci.pen = pen;
            ci.vin = padVin[i];
            ci.stroke = 2.0 * (l < 2 ? tantra::legs::kStrokeCarriage : l == 6 ? tantra::legs::kStrokeKang : tantra::legs::kStrokeStern);
            // the real force on the petal: the gas spring plus the MR valve, which holds at most the weight share + 1.5 g
            // while compressing (the damping set for Orbiter's contact is a numerical emulation, not a force to break on)
            ci.force = touchK_[i] * pen + (std::min)(touchC_[i] * (std::max)(0.0, padVin[i]), tantra::legs::kSoftG * padPer[i]);
            li[l].inPlay = live;
        }
        for (int l = 0; l < 7; ++l) {
            li[l].ratio = legR_[l];
            double sum = 0.0;
            int n = 0;
            for (int c = 0; c < 12; ++c) if (!feet_.CellLost(l, c) && !feet_.RibLost(l, c)) { sum += cellPen_[l][c]; ++n; }
            legPen_[l] = n ? sum / n : 0.0;
        }
        feet_.Step(dt, li, GetDamageModel() != 0);
        FootEvents();
    }
    // the ground mechanism plants the soles themselves (the pads are the static sag under them)
    VECTOR3 tm[kMaxPads];
    for (int i = 0; i < nt; ++i) {
        const int l = legOf[i];
        tm[i] = t[i] + upL * (l < 0 ? 0.0 : 2.0 * (l < 2 ? tantra::mesh::kStrutExtC : tantra::mesh::kStrutExtS));   // both struts' sag
    }
    GroundMechanism(simdt, tm, legOf, nt);
    // the cup feet on the ground, for people walking outside (TantraOuter.cpp): each leg's sole, its radius
    {
        int cnt[7] = {};
        for (int l = 0; l < 7; ++l) cupC_[l] = _V(0, 0, 0);
        for (int i = 0; i < nt; ++i) if (legOf[i] >= 0 && legOf[i] < 7) { cupC_[legOf[i]] += tm[i]; ++cnt[legOf[i]]; }
        const bool onGround = GroundContact() || restLock_;
        for (int l = 0; l < 7; ++l) {
            cupOn_[l] = onGround && cnt[l] > 0;
            if (cnt[l]) cupC_[l] = cupC_[l] / cnt[l];
            cupR_[l] = l < 2 ? tantra::mesh::kFootR : l == 6 ? tantra::mesh::kKangFootR : tantra::mesh::kLegFootR;
        }
        cupUp_ = upL;
    }
    FootDust(dt, upL);
    // The airlock lift stands on the ground under the door, whatever height the gear holds the ship at.
    if (lift_.AtGround()) {                   // the main airlock lift is down: step off / board there
        const VECTOR3 f = lift_.Foot();
        crew_.SetLiftFoot(_V(f.x, f.y, Zf(tantra::mesh::kAirlockS)));
    } else {
        crew_.SetLiftFoot(_V(kEvaPos.x, -p.trunnionH + 0.93, Zf(kCrewLiftS)));   // beside the lowered hangar platform
    }
    {   // outside at the foot of the lift while the cabin is up: F calls it down
        const VECTOR3 f = lift_.Foot();
        const bool level = carriage_.Progress() <= 0.0 && carriage_.Gear() >= 1.0 && !carriage_.Busy();
        crew_.SetLiftCall(_V(f.x, f.y, Zf(tantra::mesh::kAirlockS)), level && lift_.Stowed() && !lift_.Lowering() && trip_ == 0,
                          [](void* s) { static_cast<Tantra*>(s)->LiftCall(); }, this);
    }
}

// Time warp on the ground: Orbiter integrates the elastic contacts explicitly, so at large steps the ship on its
// legs pulses, drifts and finally leaves the planet. Above kWarpFreeze the ship is set down as "landed" where it
// stands (Orbiter then holds it fixed to the ground), the erection waits and the balance device rests; back at
// normal warp everything goes on from the same pose.
// Put the ship into Orbiter's landed state where it stands (held rigidly by the planet, no springs). Orbiter 2024
// reads arot as the rotation ship -> planet frame (Euler angles of its Matrix::Set) and vrot.x as the CG height over
// the ground (Vesselstatus.cpp SetState2/GetState2); GetStatusEx in flight gives the global attitude and the spin
// there - passed on as they were, they turned the ship over. equilibrium: the CG height from our own pads and their
// static sag (a scenario loaded "Landed" is set by Orbiter on its first three points only - too deep: it jumped).
void Tantra::LandNow(bool equilibrium) {
    if (!GetSurfaceRef()) return;
    VESSELSTATUS2 vs;
    std::memset(&vs, 0, sizeof vs);
    vs.version = 2;
    GetStatusEx(&vs);
    double lng, lat, rad;
    GetEquPos(lng, lat, rad);
    vs.status = 1;
    vs.surf_lng = lng;
    vs.surf_lat = lat;
    oapiGetHeading(GetHandle(), &vs.surf_hdg);
    MATRIX3 Rs, Rp, L;
    GetRotationMatrix(Rs);
    oapiGetRotationMatrix(GetSurfaceRef(), &Rp);
    const double* a = Rp.data; const double* b = Rs.data; double* l = L.data;   // L = Rp^T Rs (row-sorted)
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) l[3 * i + j] = a[i] * b[j] + a[3 + i] * b[3 + j] + a[6 + i] * b[6 + j];
    vs.arot = _V(std::atan2(L.m23, L.m33), -std::asin((std::max)(-1.0, (std::min)(1.0, L.m13))), std::atan2(L.m12, L.m11));
    double h = GetAltitude(ALTMODE_GROUND);
    if (equilibrium && nTouch_ >= 3) {
        // Orbiter tilted the ship by its first three points only: turn it so that the plane of all the pads lies on
        // the ground (least squares along the axis nearest the vertical), CG height = the pads' mean depth - the sag
        VECTOR3 up;
        HorizonInvRot(_V(0, 1, 0), up);
        const double ua[3] = {std::fabs(up.x), std::fabs(up.y), std::fabs(up.z)};
        const int k = ua[0] > ua[1] ? (ua[0] > ua[2] ? 0 : 2) : (ua[1] > ua[2] ? 1 : 2), u = (k + 1) % 3, w = (k + 2) % 3;
        double S[3][3] = {}, R[3] = {};                              // normal equations: c_k = a c_u + b c_w + d
        for (int i = 0; i < nTouch_; ++i) {
            const double* c = touch_[i].data;
            const double x[3] = {c[u], c[w], 1.0};
            for (int r = 0; r < 3; ++r) { R[r] += x[r] * c[k]; for (int q = 0; q < 3; ++q) S[r][q] += x[r] * x[q]; }
        }
        const double det = S[0][0] * (S[1][1] * S[2][2] - S[1][2] * S[2][1]) - S[0][1] * (S[1][0] * S[2][2] - S[1][2] * S[2][0]) +
                           S[0][2] * (S[1][0] * S[2][1] - S[1][1] * S[2][0]);
        if (std::fabs(det) > 1e-9) {
            auto solve = [&](int col) {                              // Cramer
                double M[3][3];
                for (int r = 0; r < 3; ++r) for (int q = 0; q < 3; ++q) M[r][q] = q == col ? R[r] : S[r][q];
                return (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                        M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0])) / det;
            };
            const double ca = solve(0), cb = solve(1);
            VECTOR3 n;
            n.data[k] = 1.0; n.data[u] = -ca; n.data[w] = -cb;
            n = n / length(n);
            if (dotp(n, up) < 0.0) n = -n;
            const double ang = std::acos((std::min)(1.0, dotp(n, up)));
            if (ang > 1e-6 && ang < 45.0 * RAD) {                    // turn the hull: Q n = up (Rodrigues), Rs' = Rs Q
                VECTOR3 ax = crossp(n, up);
                ax = ax / length(ax);
                const double cs = std::cos(ang), sn = std::sin(ang), t = 1.0 - cs;
                MATRIX3 Q = _M(t * ax.x * ax.x + cs, t * ax.x * ax.y - sn * ax.z, t * ax.x * ax.z + sn * ax.y,
                               t * ax.x * ax.y + sn * ax.z, t * ax.y * ax.y + cs, t * ax.y * ax.z - sn * ax.x,
                               t * ax.x * ax.z - sn * ax.y, t * ax.y * ax.z + sn * ax.x, t * ax.z * ax.z + cs);
                Rs = mul(Rs, Q);
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) l[3 * i + j] = a[i] * b[j] + a[3 + i] * b[3 + j] + a[6 + i] * b[6 + j];
                vs.arot = _V(std::atan2(L.m23, L.m33), -std::asin((std::max)(-1.0, (std::min)(1.0, L.m13))), std::atan2(L.m12, L.m11));
            }
            up = n;
        }
        double depth = 0.0;                                          // the pads' mean depth below the CG
        for (int i = 0; i < nTouch_; ++i) depth += -dotp(touch_[i], up);
        // the struts' static sag: 30 % of the stroke (blades 1.5 m and kangaroo 1.0 m lying; stern legs 1.0 m standing)
        // (two struts in series: the ankle's and the petal's - twice the stroke)
        const double sag = 2.0 * tantra::legs::kStaticSag * (carriage_.Progress() >= 6.0 ? tantra::legs::kStrokeStern : 0.75 * tantra::legs::kStrokeCarriage + 0.25 * tantra::legs::kStrokeKang);
        h = depth / nTouch_ - sag * LocalG() / G0;
    }
    vs.vrot = _V(h, 0.0, 0.0);
    DefSetStateEx(&vs);
    oapiWriteLogV("Tantra: at rest - Orbiter's landed state (%s, CG %.2f m over the ground)", equilibrium ? "after loading" : "still", h);
}

// The ship at rest on the ground is handed to Orbiter's landed state: Orbiter 2024 never returns a body resting on
// its touchdown springs to it by itself, and on the springs the ship trembled (explicit integration, friction that
// fades at low speed). Rest = in contact, carriage idle, no thrust, still for 2 s; at once after a scenario loads on
// the ground, and under time warp (frozen_: the erection pauses too). Orbiter releases it by itself as soon as an
// engine, a force or new touchdown points act; then the springs carry it until it is still again.
void Tantra::UpdateWarpFreeze(double dt) {
    // not while the scenario loads (UpdateGear(0)): no surface reference and no horizon frame yet
    if (dt <= 0.0 || !GetSurfaceRef()) return;
    const double warp = oapiGetTimeAcceleration();
    const bool contact = GroundContact();
    const bool quiet = contact && thrustAccel_ < 0.3 * LocalG() && !carriage_.Busy() && !mechOn_;
    VESSELSTATUS2 st;
    std::memset(&st, 0, sizeof st);
    st.version = 2;
    GetStatusEx(&st);
    const bool landed = st.status == 1;
    ++frame_;
    if (!landed && restLock_ && oapiGetSimTime() - restLostLog_ > 1.0) {   // why Orbiter let it go (to the log)
        restLostLog_ = oapiGetSimTime();
        oapiWriteLogV("Tantra: rest lost - main %.3f hover %.3f retro %.3f, touchdown set %d frames ago, own force %d frames ago, pads %d",
                      GetThrusterGroupLevel(THGROUP_MAIN), GetThrusterGroupLevel(THGROUP_HOVER), GetThrusterGroupLevel(THGROUP_RETRO),
                      frame_ - suspFrame_, frame_ - forceFrame_, nTouch_);
    }
    if (!landed) restLock_ = false;                                  // Orbiter let it go (engine, force, new points)
    VECTOR3 v, w;
    GetGroundspeedVector(FRAME_HORIZON, v);
    GetAngularVel(w);
    restTimer_ = quiet && length(v) < 0.05 && length(w) < 0.003 ? restTimer_ + dt : 0.0;
    if (!firstRest_ && dt > 0.0) {                                   // the first step after loading
        firstRest_ = true;
        if (landed && quiet) { LandNow(true); restLock_ = true; restTimer_ = 0.0; }
    }
    if (!restLock_ && !landed && quiet && restTimer_ > 2.0) { LandNow(false); restLock_ = true; restTimer_ = 0.0; }
    const bool want = contact && warp > sp::kWarpFreeze && thrustAccel_ < 0.3 * LocalG();
    if (want && !frozen_) {
        if (!landed && !restLock_) LandNow(false);
        frozen_ = true;
        Message("Ускорение времени x%.0f: корабль зафиксирован на грунте, подъём на паузе", "Time warp x%.0f: ship frozen on the ground, erection paused", warp);
    } else if (!want && frozen_) {
        frozen_ = false;
        Message("Нормальное время: фиксация снята", "Normal time: ship released");
    }
}

// Ground mechanism. While the carriage moves on the ground (lift, turn, lowering) the ship is a mechanism, not a free
// body on springs: the feet that stand on the ground stay where they stand, the trunnions run along the rails over
// them («горизонтальный лифт») and the hull turns about the trunnions. Orbiter fixes the touchdown points to the hull
// and integrates them as stiff springs with friction - the turn drags the pads, the steps jerk, time warp throws the
// ship off. So here the hull is placed kinematically every frame:
//  - every leg standing on the ground is planted: its contact point is kept at its place on the planet;
//  - the attitude is the commanded one: the plane of the contacts lies on the ground, the heading stays;
//  - the sway is the hull rocking about the trunnion axis, its own dynamics (inverted pendulum held by the trunnion
//    drives = the balance device), excited by the accelerations of the motion and the load transfers; the balance
//    device pauses the erection when it grows and lets it go on when the ship is calm.
// When the carriage stops, Orbiter takes the ship over from the same pose, at rest.
void Tantra::GroundMechanism(double dt, const VECTOR3* t, const int* legOf, int nt) {
    const tantra::CarriagePose& p = carriage_.Pose();
    OBJHANDLE ref = dt > 0.0 ? GetSurfaceRef() : nullptr;
    const bool want = ref && !frozen_ && carriage_.Gear() >= 1.0 && carriage_.Busy() && nt >= 3 &&
                      (mechOn_ || GroundContact());
    if (!want) {
        mechOn_ = false;
        mechSway_ = mechRate_ = mechKick_ = 0.0;
        for (int k = 0; k < 8; ++k) planted_[k] = false;
        mechLastTheta_ = -1.0;
        return;
    }
    // per-leg contact point in the hull (legs: -1 hull, 0..1 blades, 2..5 stern, 6 kangaroo)
    VECTOR3 legPt[8];
    int legN[8] = {0};
    for (int k = 0; k < 8; ++k) legPt[k] = _V(0, 0, 0);
    for (int i = 0; i < nt; ++i) {
        const int k = legOf[i] + 1;
        if (k < 0 || k >= 8) continue;
        legPt[k] += t[i];
        ++legN[k];
    }
    for (int k = 0; k < 8; ++k)
        if (legN[k]) legPt[k] /= legN[k];
    if (!mechOn_) {                               // take over from the present pose
        VECTOR3 sh;
        HorizonRot(_V(1, 0, 0), sh);
        sh.y = 0.0;
        if (length(sh) < 1e-6) sh = _V(1, 0, 0);
        mechSide_ = sh / length(sh);
        for (int k = 0; k < 8; ++k) {
            planted_[k] = legN[k] > 0;
            if (!planted_[k]) continue;
            VECTOR3 g;
            Local2Global(legPt[k], g);
            oapiGlobalToEqu(ref, g, &plLng_[k], &plLat_[k], &plRad_[k]);
            // on the surface itself, not where the springs happen to hold it (after a scenario load metres deep)
            plRad_[k] = oapiGetSize(ref) + oapiSurfaceElevation(ref, plLng_[k], plLat_[k]);
        }
        mechOn_ = true;
        mechSway_ = mechRate_ = mechKick_ = 0.0;
        colX_ = colVX_ = colY_ = colVY_ = colLastHipRate_ = 0.0;
        mechLastTheta_ = -1.0;
    }
    // --- sway: the hull about the trunnions, held by the drives (the balance device) ---
    // sway only on the blades alone; on three or four feet (kangaroo, stern legs) the ship stands rigid
    const bool onBlades = p.onColumns && !p.tripod && p.columnShare > 0.99;
    double thRate = 0.0, hipRate = 0.0;
    if (mechLastTheta_ >= 0.0) {
        thRate = (p.theta - mechLastTheta_) / dt;
        hipRate = (p.hipS - mechLastHip_) / dt;
        // The hull (I ~ 3e10 kg m2) is turned by the trunnion drives and held by them and the gyros: it lags its
        // command only by what its inertia asks of the drive stiffness - delta = theta'' / omega^2, tenths of a degree.
        // (The CG is over the trunnions: the rail travel and the load transfers do not turn it.)
        const double acc = (thRate - mechThRate_) / dt;
        mechKick_ += ((std::max)(-0.05, (std::min)(0.05, acc)) - mechKick_) * (std::min)(1.0, dt / 0.3);
    }
    mechLastTheta_ = p.theta;
    mechLastHip_ = p.hipS;
    mechThRate_ = thRate;
    mechHipRate_ = hipRate;
    mechShare_ = p.columnShare;
    {
        const double w = sp::kMechSwayOmega, z = sp::kMechSwayZeta;
        const int n = (std::max)(1, (int)std::ceil(dt / 0.02));
        const double h = dt / n;
        for (int k = 0; k < n; ++k) {
            const double a = onBlades ? -w * w * mechSway_ - 2.0 * z * w * mechRate_ - mechKick_
                                      : -mechSway_ * 4.0 - 4.0 * mechRate_;   // off the blades: the legs hold it rigidly
            mechRate_ += a * h;
            mechSway_ += mechRate_ * h;
        }
    }
    // --- attitude: the plane of the contacts on the ground, the heading kept ---
    const VECTOR3 cmdUp = _V(0, std::cos(p.theta), std::sin(p.theta));
    const VECTOR3 q0 = _V(p.touch[0].x, p.touch[0].y, p.touch[0].z);
    const VECTOR3 q1 = _V(p.touch[1].x, p.touch[1].y, p.touch[1].z);
    const VECTOR3 q2 = _V(p.touch[2].x, p.touch[2].y, p.touch[2].z);
    VECTOR3 n = crossp(q2 - q0, q1 - q0);
    if (length(n) < 1e-9) n = cmdUp;
    n = n / length(n);
    if (dotp(n, cmdUp) < 0.0) n = n * -1.0;
    VECTOR3 sH = _V(1, 0, 0) - n * n.x;
    sH = sH / length(sH);
    const VECTOR3 fH = crossp(sH, n);
    const VECTOR3 U = _V(0, 1, 0), sW = mechSide_, dW = crossp(sW, U);
    MATRIX3 R;
    GetRotationMatrix(R);
    VECTOR3 e[3], E[3];
    HorizonInvRot(_V(1, 0, 0), e[0]);
    HorizonInvRot(_V(0, 1, 0), e[1]);
    HorizonInvRot(_V(0, 0, 1), e[2]);
    for (int k = 0; k < 3; ++k) E[k] = mul(R, e[k]);           // horizon axes in the global frame
    auto toGlobal = [&](const VECTOR3& v) {                     // hull -> global, unswayed
        const VECTOR3 hz = sW * dotp(sH, v) + U * dotp(n, v) + dW * dotp(fH, v);
        return E[0] * hz.x + E[1] * hz.y + E[2] * hz.z;
    };
    const VECTOR3 c0 = toGlobal(_V(1, 0, 0)), c1 = toGlobal(_V(0, 1, 0)), c2 = toGlobal(_V(0, 0, 1));
    const MATRIX3 G = _M(c0.x, c1.x, c2.x, c0.y, c1.y, c2.y, c0.z, c1.z, c2.z);
    // --- position: the planted feet stay where they stand ---
    VECTOR3 org = _V(0, 0, 0);
    int no = 0;
    for (int k = 0; k < 8; ++k) {
        if (!legN[k] || !planted_[k]) continue;
        VECTOR3 tg;
        oapiEquToGlobal(ref, plLng_[k], plLat_[k], plRad_[k], &tg);
        org += tg - mul(G, legPt[k]);
        ++no;
    }
    if (no == 0) GetGlobalPos(org);
    else org /= no;
    for (int k = 0; k < 8; ++k) {                              // legs that came down are planted where they touch
        if (!legN[k]) { planted_[k] = false; continue; }
        if (planted_[k]) continue;
        const VECTOR3 g = org + mul(G, legPt[k]);
        oapiGlobalToEqu(ref, g, &plLng_[k], &plLat_[k], &plRad_[k]);
        plRad_[k] = oapiGetSize(ref) + oapiSurfaceElevation(ref, plLng_[k], plLat_[k]);   // set down on the surface
        planted_[k] = true;
    }
    // --- the columns bend: the trunnions (and the hull on them) sway along and across, the feet stand ---
    // 80 m CNT columns are springs (period ~10 s loaded): the hull's own accelerations - the run along the rails
    // and the turn - and the wind push their tops; the ankle drives, the MR struts and the gyros damp it
    // (tantra_leg_dynamics.html). Along: the hull's horizontal fore-aft; across: its side.
    {
        const double m = GetMass(), gl = LocalG();
        const double L = (std::max)(10.0, p.trunnionH - tantra::mesh::kFootH);
        auto column = [](double EI, double Lc, double Ka, double Kt, double& ti) {   // end springs (slope-deflection)
            const double a = 2.0 * EI / Lc, psi = 1.0 / Lc, det = (2.0 * a + Ka) * (2.0 * a + Kt) - a * a;
            ti = 3.0 * a * psi * (a + Kt) / det;
            const double tj = 3.0 * a * psi * (a + Ka) / det;
            return -(a * (2.0 * ti + tj - 3.0 * psi) + a * (ti + 2.0 * tj - 3.0 * psi)) / Lc;
        };
        const double E = sp::kCntE, Ka = 5.0e10, Ca = 5.0e10;   // ankle drive (N m/rad) + MR and drive damping (N m s/rad)
        double tiX, tiY;
        double kx = 2.0 * column(E * 7.2, L, Ka, sp::kBalanceKp / 2.0, tiX) - m * gl / L;
        double ky = 2.0 * column(E * sp::kBladeI, L, Ka, 1e16, tiY) - m * gl / L;
        const double stiff = onBlades ? 0.0 : 1.0;               // kangaroo or stern legs carry: the ship stands rigid
        kx = (std::max)(kx, 1e5) + stiff * 3e9;
        ky = (std::max)(ky, 1e5) + stiff * 3e9;
        const double cxx = 2.0 * Ca * tiX * tiX + 2.0 * 0.26 * std::sqrt(kx * m), cyy = 2.0 * Ca * tiY * tiY + 2.0 * 0.26 * std::sqrt(ky * m);
        const double hipAcc = dt > 0.0 ? (hipRate - colLastHipRate_) / dt : 0.0;
        colLastHipRate_ = hipRate;
        const double Ip = sp::kPmiPitch * m;
        // under time warp the steps are too long for a 10 s mode and its excitations: no forcing, it settles
        const bool real = oapiGetTimeAcceleration() <= 1.5;
        const double fx = onBlades && real ? m * (std::max)(-0.5, (std::min)(0.5, hipAcc)) - Ip * mechKick_ / L + dotp(windFh_, fH) : 0.0;
        const double fy = onBlades && real ? dotp(windFh_, sH) : 0.0;
        if (!real) { const double d = (std::min)(1.0, dt / 2.0); colX_ -= colX_ * d; colY_ -= colY_ * d; colVX_ = colVY_ = 0.0; }
        const int ns = (std::max)(1, (int)std::ceil(dt / 0.02));
        const double hs = dt / ns;
        for (int k = 0; k < ns && real; ++k) {
            colVX_ += (fx - kx * colX_ - cxx * colVX_) / m * hs; colX_ += colVX_ * hs;
            colVY_ += (fy - ky * colY_ - cyy * colVY_) / m * hs; colY_ += colVY_ * hs;
        }
        colX_ = (std::max)(-3.0, (std::min)(3.0, colX_));
        colY_ = (std::max)(-3.0, (std::min)(3.0, colY_));
        org += mul(G, fH * colX_ + sH * colY_);
    }
    // --- the sway turns the hull about the trunnion axis ---
    const double c = std::cos(mechSway_), sn = std::sin(mechSway_);
    const MATRIX3 Rx = _M(1, 0, 0, 0, c, -sn, 0, sn, c);
    const MATRIX3 Gs = mul(G, Rx);
    const VECTOR3 piv = onBlades ? _V(0, 0, p.hipS - frameS_) : _V(0, 0, 0);
    org += mul(G, piv) - mul(Gs, piv);
    // --- set it: at rest on the turning planet ---
    VESSELSTATUS2 vs;
    std::memset(&vs, 0, sizeof vs);
    vs.version = 2;
    GetStatusEx(&vs);
    if (vs.status != 0) return;                                // landed (warp freeze): Orbiter holds it
    VECTOR3 rg, gs;
    oapiGetGlobalPos(ref, &rg);
    if (GetGroundspeedVector(FRAME_GLOBAL, gs)) vs.rvel = vs.rvel - gs;
    vs.rpos = org - rg;
    vs.vrot = _V(0, 0, 0);
    DefSetStateEx(&vs);
    SetRotationMatrix(Gs);
    mechLog_ += dt;
    if (mechLog_ >= 5.0) {
        mechLog_ = 0.0;
        // feet against the terrain: height of each leg's contact over the surface (negative = in the ground)
        char gaps[160] = "";
        const double R0 = oapiGetSize(ref);
        for (int k = 0; k < 8; ++k) {
            if (!legN[k]) continue;
            const VECTOR3 gpt = org + mul(Gs, legPt[k]);
            double lg, lt, rd;
            oapiGlobalToEqu(ref, gpt, &lg, &lt, &rd);
            const double gap = rd - R0 - oapiSurfaceElevation(ref, lg, lt);
            char b1[24];
            std::snprintf(b1, sizeof b1, " L%d %.2f", k - 1, gap);
            std::strncat(gaps, b1, sizeof gaps - std::strlen(gaps) - 1);
        }
        oapiWriteLogV("Tantra: mech P %.2f theta %.1f hipS %.1f h %.1f share %.2f sway %.2f deg rate %.3f columns %.1f/%.1f cm planted %d gaps%s",
                      carriage_.Progress(), p.theta * DEG, p.hipS, p.trunnionH, p.columnShare, mechSway_ * DEG, mechRate_,
                      colX_ * 100.0, colY_ * 100.0, no, gaps);
    }
}

// Balance device. The hull on the blades is an inverted pendulum on a 20 m base at up to 80 m: Orbiter's contact
// springs alone cannot hold it. The real ship holds the hull at the commanded angle with the trunnion drives (pitch)
// and the two blade drives (roll); the erection controller pauses while the hull sways and goes on once it is calm.
// Here: angle and rate errors against the commanded erection pitch, a PD moment clipped to the drive capacity,
// applied as a couple (F x r = torque in Orbiter's convention, sign checked like the pods').
void Tantra::UpdateBalance(double dt) {
    const tantra::CarriagePose& p = carriage_.Pose();
    balTorque_[0] = balTorque_[1] = 0.0;
    const bool onBlades = GroundContact() && carriage_.Gear() >= 1.0 && p.columnShare > 0.0 && dt > 0.0 && !frozen_ && !restLock_;
    if (!onBlades) {
        balErr_[0] = balErr_[1] = balRate_[0] = balRate_[1] = 0.0;
        balBias_[0] = balBias_[1] = 0.0;
        balHold_ = false;
        balCalm_ = 0.0;
        return;
    }
    if (mechOn_) {   // the ground mechanism: the sway is its own (no forces - the hull is placed)
        balErr_[0] = mechSway_;
        balErr_[1] = 0.0;
        balRate_[0] = mechRate_;
        balRate_[1] = 0.0;
        const double err = std::fabs(mechSway_), rate = std::fabs(mechRate_);
        if (carriage_.Busy()) {
            if (!balHold_) balRamp_ = (std::min)(1.0, balRamp_ + dt / sp::kBalanceRampTime);
            if (!balHold_ && (err > sp::kBalancePauseDeg * RAD || rate > sp::kBalancePauseRate)) {
                balHold_ = true;
                balCalm_ = 0.0;
                balRamp_ = 0.0;
                Message("Равновесие: раскачка %.1f°, %.3f рад/с - подъём остановлен, выравниваю", "Balance: sway %.1f deg, %.3f rad/s - erection paused, levelling",
                        err * DEG, rate);
            } else if (balHold_) {
                balCalm_ = (err < sp::kBalanceResumeDeg * RAD && rate < sp::kBalanceResumeRate) ? balCalm_ + dt : 0.0;
                if (balCalm_ >= sp::kBalanceCalmTime) {
                    balHold_ = false;
                    Message("Равновесие: корабль спокоен, подъём продолжается", "Balance: calm, the erection goes on");
                }
            }
        } else {
            balHold_ = false;
        }
        return;
    }
    VECTOR3 up, w;
    HorizonInvRot(_V(0, 1, 0), up);
    GetAngularVel(w);
    const VECTOR3 cmd = _V(0, std::cos(p.theta), std::sin(p.theta));      // where "up" should be in the ship frame
    const VECTOR3 ax = crossp(cmd, up);                                    // rotation from commanded to actual
    // The hull leans a little on its feet statically (unequal sag of the contacts): the device holds and judges the
    // dynamic part only - a slow zero (20 s) follows the static lean, the sway is what is left over it.
    const double raw[2] = {ax.x, ax.z};                                    // pitch (about x), roll (about z)
    for (int k = 0; k < 2; ++k) {
        balBias_[k] += (raw[k] - balBias_[k]) * (std::min)(1.0, dt / sp::kBalanceBiasTime);
        balErr_[k] = raw[k] - balBias_[k];
    }
    static double lastTheta = -1.0;
    const double wCmd = lastTheta >= 0.0 ? (p.theta - lastTheta) / dt : 0.0;   // commanded pitch rate (about x)
    lastTheta = p.theta;
    balRate_[0] = w.x + wCmd * torqueSign_;      // sway rate = actual minus commanded (sign as the pods' convention)
    balRate_[1] = w.z;
    // pause / resume of the erection
    const double err = (std::max)(std::fabs(balErr_[0]), std::fabs(balErr_[1]));
    const double rate = (std::max)(std::fabs(balRate_[0]), std::fabs(balRate_[1]));
    if (carriage_.Busy()) {
        balOver_ = rate > sp::kBalancePauseRate ? balOver_ + dt : 0.0;      // a sustained sway, not one jolt
        if (!balHold_) balRamp_ = (std::min)(1.0, balRamp_ + dt / sp::kBalanceRampTime);
        if (!balHold_ && (err > sp::kBalancePauseDeg * RAD || balOver_ > sp::kBalanceRateHold)) {
            balHold_ = true;
            balCalm_ = 0.0;
            balRamp_ = 0.0;                         // resume from standstill, gently
            Message("Равновесие: раскачка %.1f°, %.3f рад/с - подъём остановлен, выравниваю", "Balance: sway %.1f deg, %.3f rad/s - erection paused, levelling",
                    err * DEG, rate);
        } else if (balHold_) {
            balCalm_ = (err < sp::kBalanceResumeDeg * RAD && rate < sp::kBalanceResumeRate) ? balCalm_ + dt : 0.0;
            if (balCalm_ >= sp::kBalanceCalmTime) {
                balHold_ = false;
                Message("Равновесие: корабль спокоен, подъём продолжается", "Balance: calm, the erection goes on");
            }
        }
    } else {
        balHold_ = false;
    }
    // holding moments: pitch by the trunnion drives, roll by the blade drives
    const double L = 30.0;
    for (int k = 0; k < 2; ++k) {
        double T = -(sp::kBalanceKp * balErr_[k] + sp::kBalanceKd * balRate_[k]);
        T = (std::max)(-sp::kBalanceMoment, (std::min)(sp::kBalanceMoment, T));
        balTorque_[k] = T;
        if (std::fabs(T) < 1.0) continue;
        const VECTOR3 Ta = k == 0 ? _V(T, 0, 0) : _V(0, 0, T);
        const VECTOR3 r = k == 0 ? _V(0, 0, L) : _V(L, 0, 0);
        const VECTOR3 F = crossp(r, Ta) * (torqueSign_ / (L * L));          // F x r = T
        AddForce(F, r);
        AddForce(F * -1.0, _V(0, 0, 0));
        forceFrame_ = frame_;
    }
}

// Orbiter 2016 touchdown vertices: the pads of the current gear pose are elastic (the MR-valve ankle struts and
// the leg bands); the hull points around them only matter in a belly landing. Friction per pad from its sole
// (core/Legs: conforming 0.5, locked 0.7, anchored 0.9).
// Stiffness gives kSag of static compression at the design gravity, damping is a fraction of critical,
// so the ship settles and sways a little on its legs. Orbiter integrates the contacts explicitly: a spring
// or a damper that is too stiff for the time step pumps energy in and throws the ship off the planet (the
// first frames after loading have long steps, time warp even longer). So: hull points no stiffer than the
// legs, damping 2*zeta*omega kept under ~6 1/s (zeta 0.35 settling, 0.3 after), and GuardAgainstLaunch.
void Tantra::SetSuspension(const VECTOR3* t, int n) {
    const double m = GetMass(), kSag = 0.25, kGref = 1.7 * G0;
    // the legs' struts: their MR valves close after the stroke - no rebound: damped nearly critically (the ship settles
    // and stays; was 0.3, it rocked on its legs)
    const double kZeta = 0.8;
    touchSettled_ = settleTimer_ <= 0.0;
    const double k = m * kGref / ((std::max)(n, 1) * kSag);  // all legs broken: the hull points still carry
    const double c = 2.0 * kZeta * std::sqrt(k * m / (std::max)(n, 1));
    // the hull is steel on the ground, not a strut: a few centimetres (4 cm if six points carry the ship), damped -
    // stiffer would not be stable for Orbiter's explicit contact steps (frame-long steps, time warp)
    const double kHull = m * G0 / (6.0 * 0.04), cHull = 2.0 * 0.7 * std::sqrt(kHull * m / 6.0);
    const double zs = Zf(0.0), zn = Zf(sp::kLength - 1.2);
    const VECTOR3 hull[] = {
        {0, 0, zn},                                                        // nose tip
        {0, 10.5, zs}, {0, -6.9, zs}, {8.9, 1.82, zs}, {-8.9, 1.82, zs},    // stern: the nacelle cluster
        {4.8, 1.82 - 8.9, zs - 2.0}, {-4.8, 1.82 - 8.9, zs - 2.0},         // lower nacelle rims
        {13.55, -3.04, Zf(60.0)}, {-13.55, -3.04, Zf(60.0)}, {0, 10.92, Zf(60.0)}, {0, -7.28, Zf(60.0)},  // fairings
        {11.0, -2.27, Zf(131.0)}, {-11.0, -2.27, Zf(131.0)}, {0, 8.16, Zf(131.0)}, {0, -5.44, Zf(131.0)},
    };
    // and the hull itself all along: rings of its real section every ~8 m, stern to nose tip (gen_mesh: kHullPts) -
    // the nose's underside, the belly, the stern no longer go through the ground between a few points
    namespace m_ = tantra::mesh;
    TOUCHDOWNVTX v[kMaxPads + sizeof hull / sizeof hull[0] + m_::kHullPtN];
    for (int i = 0; i < n; ++i)
        v[i] = {t[i], touchTuned_ ? touchK_[i] : k, touchTuned_ ? touchC_[i] : c, touchMu_[i], touchMuLng_[i]};
    int nv = n;
    for (const VECTOR3& h : hull) v[nv++] = {h + touchOfs_, kHull, cHull, 0.5, 0.5};
    for (int i = 0; i < m_::kHullPtN; ++i)
        v[nv++] = {_V(m_::kHullPts[i][0], m_::kHullPts[i][1], Zf(m_::kHullPts[i][2])) + touchOfs_, kHull, cHull, 0.5, 0.5};
    SetTouchdownPoints(v, nv);
    suspFrame_ = frame_;
    touchMass_ = m;
}

void Tantra::ActErect() {
    if (!lift_.Stowed()) {
        Message("Сначала поднимите лифт шлюза (Shift+A)", "Raise the airlock lift first (Shift+A)");
        return;
    }
    const bool up = carriage_.Target() < 3.0;
    if (!carriage_.CommandErect(up, GroundContact())) {
        Message("Лафет: только на грунте, шасси выпущено", "Carriage: on the ground with the gear down only");
        return;
    }
    if (up && wingMode_ == 1) { wingMode_ = 0; crestsFolded_ = false; }   // raised wings would meet the stern legs
    if (up) Message("Подъём во взлётное положение (2 мин): лопасти, цапфы под ЦМ, поворот, кормовые ноги", "Standing up on the stern (2 min): blades, trunnions under the CG, turn, stern legs");
    else Message("Укладка: лопасти, поворот, нога-кенгуру, лёжа", "Laying the ship down: blades, turn, kangaroo leg, lying");
}

void Tantra::ActGear() {
    const bool down = !carriage_.GearDown();
    if (!down && GroundContact()) {
        Message("Шасси нельзя убрать на грунте", "Gear cannot be stowed on the ground");
        return;
    }
    if (down && GroundContact() && carriage_.Gear() <= 0.0) {
        // on the belly the blades have no room to swing down and the cup feet none to open (they open in the air)
        Message("Шасси выпускается только в воздухе: на брюхе лопастям и чашам некуда раскрыться",
                "Gear deploys in the air only: on the belly the blades and the cup feet have no room to open");
        return;
    }
    if (!carriage_.CommandGear(down, GroundContact())) {
        Message("Шасси: дождитесь конца подъёма", "Gear: wait for the carriage to finish");
        return;
    }
    const bool standing = carriage_.Set() == tantra::Carriage::FlightSet::Standing;
    if (down) Message(standing ? "Шасси: кормовые ноги (посадка на корму)" : "Шасси: лопасти и нога-кенгуру (посадка лёжа)",
                      standing ? "Gear down: stern legs (tail-first landing)" : "Gear down: blades and the kangaroo leg (lying landing)");
    else Message("Шасси убрано в броню", "Gear stowed");
}

void Tantra::ActGearSet() {
    if (carriage_.Gear() > 0.0) {
        Message("Выбор посадки - только при убранном шасси", "Landing set can be changed with the gear stowed only");
        return;
    }
    const bool standing = carriage_.Set() != tantra::Carriage::FlightSet::Standing;
    carriage_.SetFlightSet(standing ? tantra::Carriage::FlightSet::Standing : tantra::Carriage::FlightSet::Level);
    Message(standing ? "Посадка: на корму (кормовые лапы)" : "Посадка: лёжа (колонны лафета)",
            standing ? "Landing set: tail-first (stern legs)" : "Landing set: lying (blades + kangaroo)");
}

// Tail-first flight. After the takeoff from the stand the stern legs stow by themselves 50 m up (feet clear of the
// ground blast and of a settle-back: below that a thrust dip may still put the ship back on them), climbing at
// 1 m/s at least; once per stand, the crew can lower them again. Coming down stern-first through the air (faster
// than 30 m/s) the crests fold and the fin goes down - in the reversed flow they would rock the ship (DESIGN).
void Tantra::AutoFlightSet() {
    const bool standingSet = carriage_.Set() == tantra::Carriage::FlightSet::Standing;
    if (GroundContact()) {
        liftoffAlt_ = GetAltitude(ALTMODE_GROUND);
        autoGearArmed_ = standingSet && carriage_.Standing();
        return;
    }
    VECTOR3 v;
    GetGroundspeedVector(FRAME_HORIZON, v);
    if (autoGearArmed_ && standingSet && carriage_.GearDown() && sinceContact_ > 3.0 &&
        GetAltitude(ALTMODE_GROUND) - liftoffAlt_ > 50.0 && v.y > 1.0) {
        autoGearArmed_ = false;
        if (carriage_.CommandGear(false, false))
            Message("50 м: кормовые ноги убираются в броню", "50 m: the stern legs stow into the armour");
    }
    VECTOR3 a;
    GetAirspeedVector(FRAME_LOCAL, a);
    const double as = length(a);
    if (!crestsFolded_ && GetAtmDensity() > 1e-4 && as > 30.0 && a.z < -0.7 * as) {
        wingMode_ = 2;
        crestsFolded_ = true;
        Message("Спуск кормой вперёд: крылья сложены, перо убрано", "Stern-first descent: wings folded, fin down");
    }
}

void Tantra::ActCrests() {
    wingMode_ = (wingMode_ + 1) % 3;
    // the raised wings (30 deg) would meet the upper stern legs (21-30 deg off the horizontal): not with them out
    if (wingMode_ == 1 && carriage_.Set() == tantra::Carriage::FlightSet::Standing && carriage_.Gear() > 0.0) wingMode_ = 2;
    crestsFolded_ = wingMode_ == 2;
    static const char* ru[3] = {"Крылья 90°: развёрнуты, перо выдвинуто", "Крылья 30°: подняты (вход), перо выдвинуто",
                                "Крылья сложены, перо убрано, гондолы в отсеках (субсвет)"};
    static const char* en[3] = {"Wings 90: deployed, fin up", "Wings 30: raised (entry), fin up", "Wings folded, fin down, pods in (sub-light)"};
    Message(ru[wingMode_], en[wingMode_]);
}

void Tantra::ActHangar() {
    if (hangarT_ < 0.5 && !(carriage_.Progress() <= 0.0 && carriage_.Gear() >= 1.0)) {
        Message("Ангар открывается только лёжа на лопастях", "Hangar opens only while lying on the blades");
        return;
    }
    hangarT_ = hangarT_ < 0.5 ? 1.0 : 0.0;
    if (hangarT_ < 0.5) roversT_ = 0.0;
    Message(hangarT_ > 0.5 ? "Ангар: створки открываются" : "Ангар: створки закрываются",
            hangarT_ > 0.5 ? "Hangar doors opening" : "Hangar doors closing");
}

void Tantra::ActRovers() {
    if (hangar_ < 0.97) {
        Message("Сначала откройте ангар (O)", "Open the hangar first (O)");
        return;
    }
    roversT_ = roversT_ < 0.5 ? 1.0 : 0.0;
    Message(roversT_ > 0.5 ? "Платформа роверов опускается" : "Платформа роверов поднимается",
            roversT_ > 0.5 ? "Rover platform lowering" : "Rover platform rising");
}

void Tantra::ActPort() {
    if (carriage_.Busy() || (carriage_.Progress() > 0.0 && carriage_.Progress() < 6.0)) return;
    carriage_.SetPort(!carriage_.Port());
    Message(carriage_.Port() ? "Порт: корма встанет на сверхпроводящий стол" : "Порт: дикая планета, кормовые лапы",
            carriage_.Port() ? "Port: stern on the maglev table" : "No port: stern legs");
}

// --- Input -----------------------------------------------------------------------
// Orbiter 2010 reserves plain A F H I L R T X Z; UMmu (Dan's convention) takes
// E 1 2 A S. Ship keys: K engines, J start/stop (Ctrl+J interlock override),
// G g-limiter, Y feed trap, B pods, N gear (Shift+N landing set), U carriage erect/lay
// (Ctrl+U port table), C crests, O hangar (Shift+O rovers).

int Tantra::clbkConsumeBufferedKey(DWORD key, bool down, char* kstate) {
    if (!down) return 0;
    const bool shift = KEYMOD_SHIFT(kstate) != 0, ctrl = KEYMOD_CONTROL(kstate) != 0;
    if (key == OAPI_KEY_K && !shift && !ctrl) { ActToggleEngineSet(); return 1; }
    if (shift && !ctrl) {   // the power plant (until its screen on the bridge): field, power, limiter, reaction mass
        const int pk = key == OAPI_KEY_LBRACKET ? 0 : key == OAPI_KEY_RBRACKET ? 1 : key == OAPI_KEY_SEMICOLON ? 2 :
                       key == OAPI_KEY_APOSTROPHE ? 3 : key == OAPI_KEY_L ? 4 : key == OAPI_KEY_M ? 5 : -1;
        if (pk >= 0) { PlantKey(pk); return 1; }
    }
    if (key == OAPI_KEY_D && shift && ctrl) {
        damage_.Repair();
        feet_.Repair();
        shipGone_ = false;
        if (!foil_[0]) DefineAerodynamics();
        UpdateDamageVisual(true);
        Message("Отладка: повреждения сняты", "Debug: damage repaired");
        return 1;
    }
    if (key == OAPI_KEY_J) {
        if (ctrl) ActToggleOverride();
        else if (shift) ActIgnitionTo(0);
        else ActIgnitionStep();
        return 1;
    }
    if (key == OAPI_KEY_G && !ctrl) {
        if (shift) ActCycleGLimit();
        else ActToggleGLimit();
        return 1;
    }
    if (key == OAPI_KEY_Y && !shift && !ctrl) { ActNextTrap(); return 1; }
    if (key == OAPI_KEY_B && !ctrl) { ActPods(!shift); return 1; }
    if (key == OAPI_KEY_N && !ctrl) { if (shift) ActGearSet(); else ActGear(); return 1; }
    if (key == OAPI_KEY_U && !shift) { if (ctrl) ActPort(); else ActErect(); return 1; }
    if (key == OAPI_KEY_C && !shift && !ctrl) { ActCrests(); return 1; }
    if (key == OAPI_KEY_O && !ctrl) { if (shift) ActRovers(); else ActHangar(); return 1; }
    if (key == OAPI_KEY_O && ctrl) { if (shift) ActPortDrop(); else ActPortLoad(); return 1; }
    if (key == OAPI_KEY_A && shift && !ctrl) { ActLift(); return 1; }
    if (shift || ctrl) return 0;
    switch (key) {  // UMmu keys follow DanSteph's convention
        case OAPI_KEY_E: ActEva(); return 1;
        case OAPI_KEY_1: ActSelectCrew(+1); return 1;
        case OAPI_KEY_2: ActSelectCrew(-1); return 1;
        case OAPI_KEY_A: ActToggleAirlock(); return 1;
        case OAPI_KEY_S: {
            const int total = crew_.Total();
            Message("На борту %d, свободно мест: %d", "%d aboard, %d seats free", total, sp::kCrewSeats - total);
            return 1;
        }
    }
    return 0;
}

// --- Messages ------------------------------------------------------------------------
// Both languages are kept: the HUD (Latin only under D3D9Client) shows English,
// the 2D panel draws Russian from its own cp1251 glyph atlas. Both formats take
// the same argument list; the "%s","%s" form passes a ready ru and en string.

// ---- the planetary power plant (core/Plant): the march cup's real thrust, the heat of the stern, the failures ----
namespace {
double PlantRnd() { return std::rand() / (RAND_MAX + 1.0); }
}
// Config\Tantra\plant.cfg is UTF-8 for people to edit; the messages go out in the program's charset (cp1251)
void Tantra::LoadPlantConfig() {
    tantra::plant::Config c;
    FILE* fp = std::fopen("Config\\Tantra\\plant.cfg", "rb");
    if (fp) {
        std::string t;
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, fp)) > 0) t.append(b, n);
        std::fclose(fp);
        const int wn = MultiByteToWideChar(CP_UTF8, 0, t.data(), (int)t.size(), nullptr, 0);
        std::wstring w(wn, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, t.data(), (int)t.size(), &w[0], wn);
        for (wchar_t& ch : w) if (ch == 0x2212) ch = L'-';            // the minus sign is not in cp1251
        const int an = WideCharToMultiByte(1251, 0, w.data(), wn, nullptr, 0, nullptr, nullptr);
        std::string a(an, '\0');
        WideCharToMultiByte(1251, 0, w.data(), wn, &a[0], an, nullptr, nullptr);
        c.Parse(a);
        oapiWriteLogV("Tantra: plant.cfg - %d failures, chi %.1e, risk %.1e x exp(%.1f e)", (int)c.fails.size(), c.chi, c.risk0, c.riskK);
    } else {
        c.Defaults();
        oapiWriteLog(const_cast<char*>("Tantra: Config\\Tantra\\plant.cfg not found - the built-in failures"));
    }
    plant_.Configure(c);
}
void Tantra::UpdatePlant(double simdt, double f) {
    namespace pl = tantra::plant;
    namespace m = tantra::mesh;
    pl::Env e;
    e.rho = GetAtmDensity();
    const double alt = GetAltitude(ALTMODE_GROUND);
    e.air = e.rho > 1e-5 && alt < sp::kMarchArgonAlt;
    if (alt < 3000.0) {   // the stern over the ground: the jet's back-scatter
        VECTOR3 h;
        HorizonRot(_V(0, m::kSternAxisY, Zf(0.0)), h);
        e.sternH = (std::max)(0.0, alt + h.y);
    }
    e.level = AnaIsMain() ? 0.0 : GetThrusterLevel(march_);
    e.crestsOut = (std::max)(tuck_, carriage_.Pose().tuck) < 0.5;
    e.noseT = damage_.Temperature(tantra::damage::kZoneNose);
    plantOut_ = plant_.Step(simdt > 0.0 ? simdt : 0.0, e, simdt > 0.0 ? &PlantRnd : nullptr);
    const bool cupOut = marchOut_ >= 0.99 && irisAna_ <= 0.01;
    SetThrusterMax0(march_, cupOut ? plantOut_.maxThrust * f : 0.0);
    SetThrusterIsp(march_, plantOut_.exhaust * f);
    PROPELLANT_HANDLE ph = plantOut_.mass == pl::kArgon ? argon_ : iron_;   // the bare products: from the iron store for now
    if (GetThrusterResource(march_) != ph && GetPropellantMass(ph) > 1.0) SetThrusterResource(march_, ph);
    for (const pl::Event& ev : plant_.TakeEvents()) Message("%s", "%s", ev.ru.c_str(), ev.en.c_str());
    if (plantStage_ == pl::kStRun && plant_.StageNow() != pl::kStRun && march_) SetThrusterLevel(march_, 0.0);   // off the run: the lever to zero
    plantStage_ = plant_.StageNow();
    if (plant_.Lost() && !damage_.Destroyed()) damage_.Inflict(tantra::damage::kHull, 10.0, GetDamageModel() != 0);
}
void Tantra::PlantKey(int k) {
    // the plant itself puts the limiter's, ПУСК / СТОП and the held steps into its journal and the messages (UpdatePlant)
    switch (k) {
        case 0: plant_.FieldStep(-0.5); break;
        case 1: if (!plant_.FieldStep(0.5)) return; break;
        case 2: plant_.PowerStep(-10.0); break;
        case 3: if (!plant_.PowerStep(10.0)) return; break;
        case 4: plant_.LimiterPress(oapiGetSysTime(), russian_); return;
        case 5: plant_.CycleMass(); break;
        case 6: { const int st = plant_.StageNow(); const char* s = plant_.StartStop(russian_);
                  if (st == plant_.StageNow()) Message("%s", "%s", s, s);   // refused: say why (a change is in the journal)
                  return; }
        case 7: case 8: case 9: case 10: plant_.SetMassMode(k - 8); break;
        default: return;
    }
    static const char* massRu[4] = {"авто", "аргон", "железо", "продукты"};
    static const char* massEn[4] = {"auto", "argon", "iron", "products"};
    const int mm = plant_.MassMode() + 1;
    Message("Установка: поле %.1f Тл · мощность %.0f %% ном. · %s · %s", "Plant: field %.1f T · power %.0f %% nom. · %s · %s",
            plant_.FieldSet(), plant_.PowerPct(), massRu[mm], plant_.Limiter() ? "АВТОМАТ" : "РУЧНОЙ");
}

void Tantra::Message(const char* ru, const char* en, ...) {
    va_list ap, ap2;
    va_start(ap, en);
    va_copy(ap2, ap);
    if (std::strcmp(ru, "%s") == 0 && std::strcmp(en, "%s") == 0) {
        const char* r = va_arg(ap, const char*);
        const char* e = va_arg(ap, const char*);
        std::snprintf(messageRu_, sizeof messageRu_, "%s", r);
        std::snprintf(messageEn_, sizeof messageEn_, "%s", e);
    } else {
        std::vsnprintf(messageRu_, sizeof messageRu_, ru, ap);
        std::vsnprintf(messageEn_, sizeof messageEn_, en, ap2);
    }
    va_end(ap2);
    va_end(ap);
    messageTimer_ = 8.0;
    oapiWriteLogV("Tantra: %s", messageEn_);   // every ship message also goes to Orbiter.log (debugging without the screen)
}

bool Tantra::clbkDrawHUD(int mode, const HUDPAINTSPEC* hps, oapi::Sketchpad* skp) {
    VESSEL3::clbkDrawHUD(mode, hps, skp);

    // D3D9Client builds HUD fonts with the Western charset. "Arial Cyr" is a
    // Windows font substitute that maps that request onto Arial's Cyrillic set.
    const int height = hps->H / 45 + 4;
    if (russian_ && (!hudFont_ || hudFontHeight_ != height)) {
        if (hudFont_) oapiReleaseFont(hudFont_);
        hudFont_ = oapiCreateFont(height, true, const_cast<char*>("Arial Cyr"));
        hudFontHeight_ = height;
    }
    oapi::Font* oldFont = hudFont_ ? skp->SetFont(hudFont_) : nullptr;

    char buf[256];
    const int x = 10, dy = height + 2;
    int y = hps->H / 60 * 17;
    auto line = [&](const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        skp->Text(x, y, buf, static_cast<int>(std::strlen(buf)));
        y += dy;
    };

    if (engineSet_ == EngineSet::Anamezon) {
        line(L("Анамезон: %s%s%s", "Anamezon: %s%s%s"), StageName(ignition_.Stage(), russian_),
             ignition_.Transitioning() ? L(" (переход)", " (changing)") : "",
             HotStartLevelCap() < 1.0 ? L("  [БЛОКИРОВКА ВЫСОТЫ]", "  [ALTITUDE INTERLOCK]")
             : SafetyLevelCap() < 1.0 ? L("  [БЛОКИРОВКА РАДИАЦИИ]", "  [RADIATION INTERLOCK]")
                                      : "");
    }
    line(L("Рукоять тяги: %s", "Main throttle: %s"),
         AnaIsMain() ? L("АНАМЕЗОН", "ANAMEZON")
                     : planGroup_ == 1 ? L("МАРШЕВАЯ ПЛАНЕТАРНАЯ", "MARCHING PLANETARY") : L("нет (гондолы на висении)", "none (pods in hover)"));
    double pct[sp::kTrapCount];
    for (int i = 0; i < sp::kTrapCount; ++i) pct[i] = 100.0 * GetPropellantMass(trap_[i]) / prm_.trapFuelMass;
    line(L("Ловушки: %.0f%% %.0f%% %.0f%% %.0f%%  подача из %d", "Traps: %.0f%% %.0f%% %.0f%% %.0f%%  feed %d"), pct[0],
         pct[1], pct[2], pct[3], activeTrap_ + 1);
    line(L("Рабочее тело: аргон %.1f%%  железо %.1f%%  %s", "Reaction mass: argon %.1f%%  iron %.1f%%  %s"),
         100.0 * GetPropellantMass(argon_) / prm_.argonMass, 100.0 * GetPropellantMass(iron_) / prm_.ironMass,
         marchHigh_ ? L("[железо]", "[iron]") : L("[аргон]", "[argon]"));
    line(L("Маршевая чаша: %s  носовые тормозные: %s", "Marching cup: %s  nose retro cups: %s"),
         marchOut_ >= 0.99 ? L("ВЫДВИНУТА", "OUT") : marchOut_ > 0.0 || irisMarch_ > 0.0 ? L("выдвигается...", "moving...") : L("в колодце", "home"),
         irisNose_ >= 0.99 ? L("ОТКРЫТЫ", "OPEN") : irisNose_ > 0.0 ? L("...", "...") : L("закрыты", "shut"));
    line(L("Гондолы: %s  поворот %.0f°  %s", "Pods: %s  swivel %.0f deg  %s"),
         podOut_ <= 0.0 ? L("в отсеках", "in the bays") : podOut_ < 1.0 ? L("выход...", "swinging...") : L("снаружи", "out"),
         podAngle_,
         podOut_ >= 1.0 && !podAimed_ ? L("наведение чаш...", "aiming the cups...")
         : podAssist_ ? L("ПОМОЩЬ ЛАФЕТУ", "ASSISTING THE CARRIAGE") : podHover_ ? L("висение", "HOVER") : "");
    if (podAssist_ || windForce_ != 0.0)
        line(L("Ветер %.0f м/с [%s] %.1f МН  раскачка ЦМ %.2f м (пик %.2f)  гондолы: %.0f%% веса, момент %.0f МН·м",
               "Wind %.0f m/s [%s] %.1f MN  CG sway %.2f m (peak %.2f)  pods: %.0f%% of weight, moment %.0f MN m"),
             windSpeed_, orbiterWind_ ? L("Орбитер", "Orbiter") : L("модель", "model"), windForce_ / 1e6, sway_, swayMax_,
             100.0 * [&] { double t = 0; for (THRUSTER_HANDLE th : pod_) t += GetThrusterLevel(th) * GetThrusterMax0(th); return t; }() /
                 (GetMass() * LocalG()),
             podCouple_ / 1e6);
    if (podAssist_ && podBalanceLost_)
        line(L("Гондолы: баланс вокруг ЦМ невозможен в этом положении - тяга снимается, держат ноги",
               "Pods: no balance about the CG in this attitude - thrust fades, the legs carry the ship"));
    {
        namespace dm = tantra::damage;
        const double hot = (std::max)(damage_.Temperature(dm::kZoneNose), damage_.Temperature(dm::kZoneCrestEdge));
        if (GetAtmDensity() > 1e-7 || hot > 500.0)
            line(L("Нагрев: нос %.0f/%.0f K  днище %.0f/%.0f  кромки %.0f/%.0f  шасси %.0f  гондолы %.0f  q %.1f кПа  %.1f g  гребни %.0f%%",
                   "Heat: nose %.0f/%.0f K  belly %.0f/%.0f  edges %.0f/%.0f  gear %.0f  pods %.0f  q %.1f kPa  %.1f g  crests %.0f%%"),
                 damage_.Temperature(dm::kZoneNose), damage_.Limit(dm::kZoneNose), damage_.Temperature(dm::kZoneBelly),
                 damage_.Limit(dm::kZoneBelly), damage_.Temperature(dm::kZoneCrestEdge), damage_.Limit(dm::kZoneCrestEdge),
                 damage_.Temperature(dm::kZoneGear), damage_.Temperature(dm::kZonePods), damage_.DynamicPressure() / 1e3, accelG_,
                 100.0 * damage_.CrestLoad(0));
        char list[256] = "";
        int k = 0;
        for (int i = 0; i < dm::kPartCount && k < 200; ++i)
            if (damage_.Integrity(i) < 1.0)
                k += std::snprintf(list + k, sizeof list - k, "%s%s %.0f%%", k ? ", " : "", russian_ ? dm::Model::NameRu(i) : dm::Model::NameEn(i),
                                   100.0 * damage_.Integrity(i));
        if (k) line(L("Повреждения: %s%s", "Damage: %s%s"), list, GetDamageModel() ? "" : L("  [повреждения выключены в Launchpad]", "  [damage off in the Launchpad]"));
    }
    if (legLoad_ > 0.0)
        line(L("Нагрузка: %s %.0f МН = %.0f%% допуска (x1,5)  опрокинет ветер %.0f м/с",
               "Load: %s %.0f MN = %.0f%% of rating (x1.5)  overturning wind %.0f m/s"),
             legName_, legLoad_ / 1e6, 100.0 * legRatio_, tipWind_);
    if (GroundContact() && carriage_.Gear() > 0.0) {
        const double sj = (std::max)(solesCar_.Jam(), solesStern_.Jam()), sa = (std::max)(solesCar_.Anchors(), solesStern_.Anchors());
        line(L("Опоры (датчики): лопасти %.0f/%.0f%%  кенгуру %.0f%%  корма %.0f/%.0f/%.0f/%.0f%%  грунт %s  цапфы %s",
               "Legs (sensors): blades %.0f/%.0f%%  kangaroo %.0f%%  stern %.0f/%.0f/%.0f/%.0f%%  ground %s  trunnions %s"),
             100.0 * legR_[0], 100.0 * legR_[1], 100.0 * legR_[6], 100.0 * legR_[2], 100.0 * legR_[3], 100.0 * legR_[4], 100.0 * legR_[5],
             sa > 0.99 ? L("спечён", "sintered") : sj > 0.99 ? L("осел", "settled") : L("оседает", "settling"),
             hipCatcher_ ? L("на страховочных", "on the catchers") : L("магнитная подвеска", "magnetic"));
    }
    if (GroundContact() && carriage_.Gear() >= 1.0 && carriage_.Pose().columnShare > 0.0)
        line(L("Равновесие: тангаж %+.2f° крен %+.2f°  скорость %.3f рад/с  момент цапф %.2f ГН·м%s",
               "Balance: pitch %+.2f deg roll %+.2f deg  rate %.3f rad/s  trunnion moment %.2f GN m%s"),
             balErr_[0] * DEG, balErr_[1] * DEG, (std::max)(std::fabs(balRate_[0]), std::fabs(balRate_[1])), balTorque_[0] / 1e9,
             balHold_ ? L("  [ПАУЗА: раскачка]", "  [PAUSED: swaying]") : "");
    if (regen_.Returned() + regen_.Spent() > 1e6)
        line(L("Приводы ног (сверхпроводящие): затрачено %.1f ГДж, возвращено при опускании %.1f ГДж",
               "Leg drives (superconducting): spent %.1f GJ, returned while lowering %.1f GJ"),
             regen_.Spent() / 1e9, regen_.Returned() / 1e9);
    {
        static const char* kPhRu[] = {"лежит (треножник)", "подъём на лопастях", "цапфы под ЦМ, кенгуру в карман", "поворот",
                                      "кормовые ноги выходят", "нагрузка на корму", "лопасти убираются", "стоит на корме"};
        static const char* kPhEn[] = {"lying (tripod)", "lifting on the blades", "trunnions to the CG, kangaroo in", "turning",
                                      "stern legs out", "load to the stern", "blades stowing", "standing"};
        const tantra::CarriagePose& cp = carriage_.Pose();
        const int ph = carriage_.Phase();
        if (carriage_.Gear() <= 0.0)
            line(L("Шасси убрано (посадка: %s, N/Shift+N)", "Gear stowed (landing set: %s, N/Shift+N)"),
                 carriage_.Set() == tantra::Carriage::FlightSet::Standing ? L("на корму", "tail-first") : L("лёжа", "level"));
        else
            line(L("Опоры: %s  угол %.0f°  ЦМ %.0f м  цапфы s %.1f%s", "Gear: %s  pitch %.0f deg  CG %.0f m  trunnions s %.1f%s"),
                 russian_ ? kPhRu[ph] : kPhEn[ph], cp.theta * DEG, cp.trunnionH, cp.hipS,
                 carriage_.Port() ? L("  [порт]", "  [port]") : "");
    }
    {
        const bool feeding = AnaIsMain();
        const double lv = feeding ? GetThrusterGroupLevel(THGROUP_MAIN) : 0.0;
        line(L("Накопитель поля %.0f%%  гранулы 4x%.1f кГц по %.1f г (%.0f кт)", "Field store %.0f%%  pellets 4x%.1f kHz, %.1f g (%.0f kt)"),
             100.0 * drive_.StoreFraction(), drive_.PelletRate(lv) / 1e3, drive_.PelletMass() * 1e3,
             drive_.PelletEnergy() / 4.184e12);
        line(L("Возврат %.1e Вт  компенсатор гасит %.0f g", "Recovered %.1e W  compensator cancels %.0f g"),
             drive_.RecoveredPower(), drive_.CompensatedAccel() / G0);
        if (safety_ && engineSet_ == EngineSet::Anamezon) {
            // Danger zone at the current feed (or at full while idle), and who is in it.
            const double p = drive_.JetPower(lv > 0.0 ? lv : 1.0);
            line(L("Радиация%s: смерть за 1 ч до %.0f тыс. км, в луче до %.0f млн км",
                   "Radiation%s: lethal in 1 h within %.0fk km, in the jet to %.0fM km"),
                 lv > 0.0 ? "" : L(" (на полной)", " (at full)"), safety_->LethalRadius(p) / 1e6,
                 safety_->LethalBeamRange(p) / 1e9);
            for (const TantraSafety::PlanetRisk& r : safety_->Planets())
                if (r.hazard != tantra::Hazard::None)
                    line(L("  %s: %s, %.1e Вт%s", "  %s: %s, %.1e W%s"), r.name.c_str(),
                         r.hazard == tantra::Hazard::Catastrophe ? L("КАТАСТРОФА для экосистемы", "ECOSYSTEM CATASTROPHE")
                                                                : L("вред экосистеме", "ecosystem harm"),
                         r.intercepted, r.inBeam ? L(" (в луче)", " (in the jet)") : "");
            int shown = 0;
            for (const TantraSafety::CrewRisk& c : safety_->Crews())
                if (c.rate * 3600.0 >= 0.1 && shown++ < 4)
                    line(L("  %s: %.1f Гр/ч, набрано %.1f Гр%s", "  %s: %.1f Gy/h, total %.1f Gy%s"), c.name.c_str(),
                         c.rate * 3600.0, c.dose, c.inBeam ? L(" (в луче)", " (in the jet)") : "");
        }
        const tantra::ExhaustFrame& ef = exhaust_ ? exhaust_->Frame() : tantra::ExhaustFrame();
        if (feeding && ef.fireball.size > 0.0)
            line(L("Луч в воздухе: пробег %.2f км%s, плазменный столб R %.0f м",
                   "Beam in air: range %.2f km%s, plasma column R %.0f m"),
                 ef.fireball.dist / 1e3, ef.dust > 0.0 ? L(" (бьёт в грунт)", " (hits ground)") : "", ef.fireball.size);
    }
    const double tau = properTime_;
    line(L("v/c %.6f  гамма %.4f  корабельное время %dд %02dч %02dм", "v/c %.6f  gamma %.4f  ship time %dd %02dh %02dm"),
         beta_, Gamma(beta_), int(tau / 86400), int(std::fmod(tau, 86400) / 3600), int(std::fmod(tau, 3600) / 60));
    line(L("Ощущаемая перегрузка %.2f g  ограничитель %s %.1f g", "Felt load %.2f g  limiter %s %.1f g"), accelG_,
         gLimitOn_ ? L("ВКЛ", "ON") : L("ВЫКЛ", "OFF"), gLimit_);
    if (messageTimer_ > 0.0) {
        y += dy / 2;
        line("%s", russian_ ? messageRu_ : messageEn_);
    }
    if (oldFont) skp->SetFont(oldFont);
    return true;
}

// ==============================================================================

DLLCLBK VESSEL* ovcInit(OBJHANDLE hvessel, int flightmodel) { return new Tantra(hvessel, flightmodel); }

DLLCLBK void ovcExit(VESSEL* vessel) {
    if (vessel) delete static_cast<Tantra*>(vessel);
}

// ---- Interior as a virtual cockpit: free walking (TantraWalk). The VC view is selected with the cockpit view key (F8 toggles panel / VC).
bool Tantra::clbkLoadVC(int id) {
    if (id != 0) return false;
    walk_.OnLoadVC();
    screen_.OnLoadVC(MeshDZ());
    return true;
}

bool Tantra::clbkVCMouseEvent(int id, int event, VECTOR3&) { return screen_.OnMouse(id, event); }

int Tantra::clbkConsumeDirectKey(char* kstate) {
    if (interior_.TakenSeat() >= 0 && walk_.Seat() != interior_.TakenSeat()) walk_.SitAt(interior_.TakenSeat());
    const int r = walk_.Keys(kstate, MeshDZ());
    screen_.Update(MeshDZ());
    return r;
}

// The user's rule for walking inside (Orbiter's physics does not act on the people inside: the ship decides):
// not during takeoff and landing, not on the anamezon drive; in cruise yes; on the ground only lying horizontally (+-10 deg).
int Tantra::CanWalk(char* reason, int n) const {
    auto no = [&](const char* r) { if (reason && n > 0) snprintf(reason, n, "%s", r); return 0; };
    if (engineSet_ == EngineSet::Anamezon) {
        for (int i = 0; i < tantra::spec::kAnaCount; i++)
            if (ana_[i] && GetThrusterLevel(ana_[i]) > 0.0) return no("Анамезонный двигатель: все в креслах");
    }
    const double lim = 10.0 * PI / 180.0;
    if (GroundContact()) {
        if (std::fabs(GetPitch()) > lim || std::fabs(GetBank()) > lim) return no("Корабль не горизонтален: ходить нельзя");
        return 1;
    }
    if (GetAltitude() < 100e3) return no("Взлёт или посадка: все в креслах");
    return 1;
}

// The user's decision: no Orbiter autopilots (killrot, prograde, hold altitude...) in the Tantra. Any that gets switched on
// (a key, an MFD, a scenario) is switched off at once.
void Tantra::clbkNavMode(int mode, bool active) {
    if (!active) { navAllowed_ &= ~(1 << mode); return; }
    if (navAllowed_ & (1 << mode)) return;                              // from the holo panel: allowed (the user's decision)
    DeactivateNavmode(mode);
    oapiWriteLogV("Tantra: Orbiter autopilot %d refused (only from the holo panel)", mode);
}

// An Orbiter autopilot switched from the flight terminal (the only way: the keys stay refused - the user's decision).
void Tantra::ToggleNav(int mode) {
    if (GetNavmodeState(mode)) DeactivateNavmode(mode);
    else { navAllowed_ |= 1 << mode; ActivateNavmode(mode); }
}
