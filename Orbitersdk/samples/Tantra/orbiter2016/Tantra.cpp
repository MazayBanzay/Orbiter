// Tantra: Orbiter 2016 vessel adapter.
#include "Tantra.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "TantraExhaust.h"
#include "TantraSafety.h"
#include "TantraGear.h"

#include "../core/Aero.h"
#include "../core/Physics.h"

using namespace tantra;
namespace sp = tantra::spec;

namespace {

// XRSound ids (ours must stay below 10000).
enum SoundSlot { SND_ANA_RUN = 1, SND_ION_RUN, SND_FIELD_UP, SND_BEAM_UP, SND_IGNITE, SND_SIREN, SND_SHUTDOWN };

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

// Crew to the ground: the hangar floor platform (s 81.5..98.5) lowers to the ground; the crew steps off
// beside it. The port airlock door serves in space.
const double kCrewLiftS = 90.0;
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

// Hull stations of the airfoil references: body (Newtonian centre of pressure near the planform centroid),
// dorsal fin 7, lateral crests.
const double kAeroS[5] = {55.0, 55.0, 23.0, 16.5, 45.0};
const double kAeroY[5] = {0.0, 0.0, 22.0, -4.0, -12.0};

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
    if (hudFont_) oapiReleaseFont(hudFont_);
    if (panelMesh_) oapiDeleteMesh(panelMesh_);
}

void Tantra::clbkSetClassCaps(FILEHANDLE cfg) {
    char lang[32] = "ru";
    if (cfg) oapiReadItem_string(cfg, const_cast<char*>("HudLanguage"), lang);
    russian_ = _strnicmp(lang, "en", 2) != 0;
    LoadParams(cfg);

    DefineMassAndShape();
    DefinePropulsion();
    DefineAttitude();
    DefineAerodynamics();
    DefineCrew();

    meshIdx_ = AddMesh(oapiLoadMeshGlobal("Tantra\\Tantra"));
    SetMeshVisibilityMode(meshIdx_, MESHVIS_ALWAYS);
    DefineGear();
    DefinePort();
}

// Carriage columns + stern legs, all mesh animations (TantraGear builds the rig from MeshLayout.h).
void Tantra::DefineGear() {
    tantra::CarriageGeometry g;
    g.restAxisH = sp::kAxisHeight;
    g.turnClear = sp::kTurnClear;
    g.standClear = sp::kStandClear;
    namespace m = tantra::mesh;
    g.columnX = m::kHipXOut;
    g.footHalf = m::kPadHalfL;
    g.footH = m::kFootH;
    g.legMin = m::kLegLMin;
    g.cgError = 0.3;  // the ship's CG is never exactly on the trunnions (tantra_c148: delta 0.3 m)
    g.trackS0 = m::kCarS0;
    g.trackS1 = m::kCarS1;
    g.stowS = m::kStowS;
    g.standR = m::kStandR;
    for (int i = 0; i < 4; ++i) g.standFoot[i] = {m::kLegs[i].radial.x * m::kStandR, m::kLegs[i].radial.y * m::kStandR, 0.0};
    TantraGear::LegRestFoot(g.legRestX, g.legRestS);
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
        {"IonChargeMass", &prm_.ionChargeMass},
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
        {"PlanThrustTotal", &prm_.planThrustTotal},
        {"PodThrustTotal", &prm_.podThrustTotal},
        {"PlanExhaust", &prm_.planExhaust},
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
    SetCrossSections(_V(3600.0, 4900.0, 650.0));
    SetRotDrag(_V(0.3, 0.3, 0.1));

    // Touchdown points follow the carriage every step (UpdateGear); start resting level.
    const double ground = -sp::kAxisHeight;
    const VECTOR3 t0[3] = {_V(sp::kColumnX, ground, sp::kColumnFootHalf), _V(-sp::kColumnX, ground, sp::kColumnFootHalf),
                           _V(sp::kColumnX, ground, -sp::kColumnFootHalf)};
    SetSuspension(t0, 3);

    SetCameraOffset(_V(0.0, 3.0, sp::Z(sp::kControlPostS)));  // central control post
}

void Tantra::DefinePropulsion() {
    for (int i = 0; i < sp::kTrapCount; ++i) trap_[i] = CreatePropellantResource(prm_.trapFuelMass);
    ion_ = CreatePropellantResource(prm_.ionChargeMass);

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
    // Twelve central ion-trigger cups in the stern ring, behind the baffle (vertical take-off,
    // interplanetary flight). Vectoring outward only - never across the well.
    for (int i = 0; i < sp::kPlanCount; ++i) {
        const double a = 2.0 * PI * (i + 0.5) / sp::kPlanCount;
        const VECTOR3 pos = _V(sp::kPlanR * std::cos(a), sp::kPlanR * std::sin(a), sp::Z(sp::kPlanS));
        plan_[i] = CreateThruster(pos, fwd, prm_.planThrustTotal / sp::kPlanCount, ion_, prm_.planExhaust);
        AddExhaust(plan_[i], 40.0, 1.8);
    }
    // Four planetary pods x 3 cups on the lower chines (MeshLayout kPods). The cups are placed every step
    // by UpdatePods from the rig: door arm swing, pod swivel, vessel frame.
    for (int p = 0; p < sp::kPodCount; ++p) {
        for (int c = 0; c < sp::kCupsPerPod; ++c) {
            const int k = p * sp::kCupsPerPod + c;
            pod_[k] = CreateThruster(V3(tantra::mesh::kPods[p].cup[c]), fwd, prm_.podThrustTotal / sp::kPodCups, ion_,
                                     prm_.planExhaust);
            EXHAUSTSPEC& es = podExh_[k];
            es.th = pod_[k];
            es.level = nullptr;
            es.lpos = &podExhPos_[k];
            es.ldir = &podExhDir_[k];
            es.lsize = 18.0;
            es.wsize = 1.0;
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
    const double isp = prm_.planExhaust, F = prm_.attMaxThrust;
    const double zN = kAttNoseZ, zT = kAttTailZ, rN = kAttNoseR, rT = kAttTailR;
    THRUSTER_HANDLE* c = attCouple_;
    c[0] = CreateThruster(_V(0, -rN, zN), _V(0, 1, 0), F, ion_, isp);   // nose up
    c[1] = CreateThruster(_V(0, rN, zN), _V(0, -1, 0), F, ion_, isp);   // nose down
    c[2] = CreateThruster(_V(0, -rT, zT), _V(0, 1, 0), F, ion_, isp);   // tail up
    c[3] = CreateThruster(_V(0, rT, zT), _V(0, -1, 0), F, ion_, isp);   // tail down
    c[4] = CreateThruster(_V(rN, 0, zN), _V(-1, 0, 0), F, ion_, isp);   // nose left
    c[5] = CreateThruster(_V(-rN, 0, zN), _V(1, 0, 0), F, ion_, isp);   // nose right
    c[6] = CreateThruster(_V(rT, 0, zT), _V(-1, 0, 0), F, ion_, isp);   // tail left
    c[7] = CreateThruster(_V(-rT, 0, zT), _V(1, 0, 0), F, ion_, isp);   // tail right
    THRUSTER_HANDLE* r = attRoll_;
    r[0] = CreateThruster(_V(rT, 0, zT), _V(0, 1, 0), F, ion_, isp);    // bank left
    r[1] = CreateThruster(_V(-rT, 0, zT), _V(0, -1, 0), F, ion_, isp);
    r[2] = CreateThruster(_V(-rT, 0, zT), _V(0, 1, 0), F, ion_, isp);   // bank right
    r[3] = CreateThruster(_V(rT, 0, zT), _V(0, -1, 0), F, ion_, isp);
    attAxial_[0] = CreateThruster(_V(0, 0, zT), _V(0, 0, 1), F, ion_, isp);
    attAxial_[1] = CreateThruster(_V(0, 0, zN), _V(0, 0, -1), F, ion_, isp);

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
    static PlateData fin = {0.9, nullptr}, crests = {1.33, nullptr};
    fin.avail = &aeroFin_;
    crests.avail = &aeroCrest_;
    // Airfoils act at hull stations (kAeroS / kAeroY); UpdateCG moves the refs with the vessel frame.
    // Body: normal force on the planform (centre of pressure near its centroid), axial on the frontal area.
    foil_[0] = CreateAirfoil3(LIFT_VERTICAL, _V(0, kAeroY[0], Zf(kAeroS[0])), BodyPitch, nullptr, ae::kLength, ae::kPlanform, 0.4);
    foil_[1] = CreateAirfoil3(LIFT_HORIZONTAL, _V(0, kAeroY[1], Zf(kAeroS[1])), BodyYaw, nullptr, ae::kLength, ae::kSide, 0.4);
    // Dorsal fin 7 (611 m2) holds heading, lateral crests (2 x 212 m2) hold pitch; nothing when folded.
    foil_[2] = CreateAirfoil3(LIFT_HORIZONTAL, _V(0, kAeroY[2], Zf(kAeroS[2])), PlateCoeff, &fin, 28.0, 611.0, fin.aspect);
    foil_[3] = CreateAirfoil3(LIFT_VERTICAL, _V(0, kAeroY[3], Zf(kAeroS[3])), PlateCoeff, &crests, 16.0, 424.0, crests.aspect);
    // Gear and pods in the flow, under the hull: drag and the nose-down moment it brings.
    foil_[4] = CreateAirfoil3(LIFT_VERTICAL, _V(0, kAeroY[4], Zf(kAeroS[4])), GearDrag, &aeroGearArea_, 10.0, 1.0, 1.0);
}

// Elevons on the outer 60 % of the lateral crests (2 x 36 m2, -30..+40 deg) and the body flap under the
// stern (18 x 5 m, 0..25 deg). Removed while the crests are folded.
void Tantra::DefineControlSurfaces(bool on) {
    if (on == ctrlOn_) return;
    for (CTRLSURFHANDLE& h : ctrl_) {
        if (h) DelControlSurface(h);
        h = nullptr;
    }
    if (on) {
        ctrl_[0] = CreateControlSurface3(AIRCTRL_ELEVATOR, 72.0, 1.2, _V(0, -4.0, Zf(12.5)), AIRCTRL_AXIS_XPOS, 1.0);
        ctrl_[1] = CreateControlSurface3(AIRCTRL_AILERON, 36.0, 1.2, _V(23.5, -4.0, Zf(13.0)), AIRCTRL_AXIS_XPOS, 1.0);
        ctrl_[2] = CreateControlSurface3(AIRCTRL_AILERON, 36.0, 1.2, _V(-23.5, -4.0, Zf(13.0)), AIRCTRL_AXIS_XNEG, 1.0);
        ctrl_[3] = CreateControlSurface3(AIRCTRL_ELEVATORTRIM, 90.0, 0.8, _V(0, -9.6, Zf(0.0)), AIRCTRL_AXIS_XPOS, 1.5);
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
    UpdateCG(true);   // the loaded propellant sets the CG before the first step
    UpdatePods(0.0);
    UpdateGear(0.0);  // mesh pose and touchdown points from the loaded state
    sound_ = XRSound::CreateInstance(this);
    if (!sound_ || !sound_->IsPresent()) return;
    using PT = XRSound::PlaybackType;
    sound_->LoadWav(SND_ANA_RUN, "XRSound\\Tantra\\ana_run.wav", PT::BothViewFar);
    sound_->LoadWav(SND_ION_RUN, "XRSound\\Tantra\\ion_run.wav", PT::BothViewFar);
    sound_->LoadWav(SND_FIELD_UP, "XRSound\\Tantra\\field_up.wav", PT::InternalOnly);
    sound_->LoadWav(SND_BEAM_UP, "XRSound\\Tantra\\beam_up.wav", PT::InternalOnly);
    sound_->LoadWav(SND_IGNITE, "XRSound\\Tantra\\ignite.wav", PT::BothViewMedium);
    sound_->LoadWav(SND_SIREN, "XRSound\\Tantra\\siren.wav", PT::BothViewFar);
    sound_->LoadWav(SND_SHUTDOWN, "XRSound\\Tantra\\shutdown.wav", PT::InternalOnly);
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
        char dmg[256];
        damage_.Save(dmg, sizeof dmg);
        oapiWriteScenario_string(scn, const_cast<char*>("DAMAGE"), dmg);
    }
    std::snprintf(buf, sizeof buf, "%.1f %.1f %.3f %d", podAngle_, podTarget_, podOut_, podsWanted_ ? 1 : 0);
    oapiWriteScenario_string(scn, const_cast<char*>("PODS"), buf);
    {
        double p, pT, g, gT;
        int set, port;
        carriage_.Save(p, pT, g, gT, set, port);
        char cb[128];
        std::snprintf(cb, sizeof cb, "%.4f %.1f %.3f %.0f %d %d", p, pT, g, gT, set, port);
        oapiWriteScenario_string(scn, const_cast<char*>("CARRIAGE"), cb);
        std::snprintf(cb, sizeof cb, "%d %.2f %.0f %.2f %.0f", crestsFolded_ ? 1 : 0, hangar_, hangarT_, rovers_, roversT_);
        oapiWriteScenario_string(scn, const_cast<char*>("DECK"), cb);
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
        } else {
            ParseScenarioLineEx(line, status);
        }
    }
    BindMainGroup(engineSet_);
    UpdatePods(0.0);
    SelectActiveTrap();
    carriage_.Update(0.0, frameS_);
    tuck_ = crestsFolded_ ? 1.0 : 0.0;
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
    const bool hover = podOut_ >= 1.0 && podAngle_ >= 45.0 && !podAssist_ && podAimed_;
    const bool mainIsAna = GetGroupThrusterCount(THGROUP_MAIN) == sp::kAnaCount;
    if (want != planGroup_ || anaMain != mainIsAna) {
        DelThrusterGroup(THGROUP_MAIN);
        for (THRUSTER_HANDLE th : ana_) SetThrusterLevel(th, 0.0);  // (re)start from zero feed
        for (THRUSTER_HANDLE th : plan_) SetThrusterLevel(th, 0.0);
        if (anaMain) CreateThrusterGroup(ana_, sp::kAnaCount, THGROUP_MAIN);
        else CreateThrusterGroup(plan_, sp::kPlanCount, THGROUP_MAIN);
        planGroup_ = want;
    }
    if (hover != podHover_) {
        DelThrusterGroup(THGROUP_HOVER);
        for (THRUSTER_HANDLE th : pod_) SetThrusterLevel(th, 0.0);
        if (hover) CreateThrusterGroup(pod_, sp::kPodCups, THGROUP_HOVER);
        podHover_ = hover;
    }
}

// Pods: the bay door is the swing arm (out 0..1 in kPodSwingTime), the pod turns only while fully out,
// and swings in only with the cups aft. The doors stay shut above Mach 0.8 and with the crests folded
// or the ship being stood up. Cup positions and directions follow the mesh rig exactly.
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
        const VECTOR3 hinge = V3(r.hinge), pivot = V3(r.pivot), ax = V3(r.swivelAxis), zax = _V(0, 0, 1);
        const double swing = r.swing * podOut_;
        auto place = [&](const VECTOR3& q) {
            const VECTOR3 a = RotateAbout(q - pivot, ax, swivel) + pivot;
            return RotateAbout(a - hinge, zax, swing) + hinge + dz;
        };
        VECTOR3 dir = RotateAbout(RotateAbout(_V(0, 0, 1), ax, swivel), zax, swing);  // cups face aft at 0
        for (int c = 0; c < sp::kCupsPerPod; ++c) {
            const int k = p * sp::kCupsPerPod + c;
            podExhPos_[k] = place(V3(r.cup[c]));
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
        AddForce(fh, _V(0, 0, 0));
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
    legLoad_ = legRatio_ = tipWind_ = colRatio_ = sternRatio_ = 0.0;
    if (!GroundContact() || carriage_.Gear() <= 0.0) return;
    VECTOR3 up;
    HorizonInvRot(_V(0, 1, 0), up);
    double lift = 0.0;
    for (THRUSTER_HANDLE th : pod_) {
        VECTOR3 d;
        GetThrusterDir(th, d);
        lift += GetThrusterLevel(th) * GetThrusterMax0(th) * dotp(d, up);
    }
    const double W = (std::max)(0.0, GetMass() * LocalG() - lift);
    const double F = length(fh), h = p.trunnionH, M = F * h;
    const double rho = GetAtmDensity();
    auto pcr = [](double I, double L) { return PI * PI * sp::kCntE * I / (L * L); };
    const double colShare = p.columnShare, sternShare = 1.0 - colShare;
    double worst = 0.0, ratio = 0.0, tipF = 1e30;
    if (colShare > 0.0) {       // two carriage legs, 38 m apart
        const double X = tantra::mesh::kHipXOut;
        const double n = colShare * W / 2.0 + M / (2.0 * X);
        const double cap = (std::min)(sp::kCntSigma * sp::kLafShinA, pcr(sp::kLafShinI, (std::max)(8.0, p.mastLen))) / sp::kSafety;
        worst = n;
        ratio = n / cap;
        colRatio_ = ratio;
        legName_ = russian_ ? "нога лафета" : "carriage leg";
        tipF = (std::min)(tipF, colShare * W * X / (std::max)(1.0, h));
    }
    if (sternShare > 0.0) {     // four stern legs, splay ~16 deg
        const double r = sp::kStandInradius;
        const double n = (sternShare * W / 4.0 + M / (2.0 * r)) / std::cos(16.0 * RAD);
        const double cap = (std::min)(sp::kCntSigma * sp::kSternShinA, pcr(sp::kSternShinI, 26.5)) / sp::kSafety;
        sternRatio_ = n / cap;
        if (n / cap > ratio) {
            worst = n;
            ratio = n / cap;
            legName_ = russian_ ? "кормовая нога" : "stern leg";
        }
        tipF = (std::min)(tipF, sternShare * W * r / (std::max)(1.0, h));
    }
    legLoad_ = worst;
    legRatio_ = ratio;
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
    const double ion = GetPropellantMass(ion_);
    m += ion;
    ms += ion * sp::kIonCGS;
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
    for (THRUSTER_HANDLE th : plan_) aim(th);
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
    maxThrust = prm_.planThrustTotal * f;
    if (gLimitOn_) {
        const double cap = GLimitLevel(gLimit_ * G0, GetMass(), maxThrust);
        if (level > cap) SetThrusterGroupLevel(THGROUP_MAIN, cap);
    }
}

void Tantra::clbkPreStep(double, double simdt, double) {
    WatchTerrain();   // first: a refined terrain tile must not bury the pads for even one step
    UpdateCG(false);
    AimThroughCG();
    UpdateWind(simdt);
    {
        namespace dm = tantra::damage;
        const bool crestsOk = !damage_.Lost(dm::kCrestPort) || !damage_.Lost(dm::kCrestStbd);
        DefineControlSurfaces(tuck_ < 0.5 && carriage_.Pose().tuck < 0.5 && crestsOk && !damage_.Destroyed());
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
    for (THRUSTER_HANDLE th : plan_) {
        SetThrusterMax0(th, prm_.planThrustTotal / sp::kPlanCount * f);
        SetThrusterIsp(th, prm_.planExhaust * f);
    }
    double podMax = 0.0;
    for (int k = 0; k < sp::kPodCups; ++k) {  // pods in the bays give no thrust; the pairs share it for pitch
        const double share = podAssist_ ? 1.0 : podShare_[k / (2 * sp::kCupsPerPod)];  // assist: moments set per cup
        const double mx = !podAimed_ ? 0.0 : prm_.podThrustTotal / sp::kPodCups * f * share;  // no thrust until aimed
        SetThrusterMax0(pod_[k], mx);
        SetThrusterIsp(pod_[k], prm_.planExhaust * f);
        podMax += mx;
    }
    PodAssistLevels(simdt, podMax);
    ScaleAttitudeThrust();
    {   // damage: lost pods and cups give nothing; a destroyed ship has no engines at all
        namespace dm = tantra::damage;
        for (int k = 0; k < sp::kPodCups; ++k)
            if (damage_.Lost(dm::kPod0 + k / sp::kCupsPerPod)) SetThrusterMax0(pod_[k], 0.0);
        if (damage_.Lost(dm::kSternCups))
            for (THRUSTER_HANDLE th : plan_) SetThrusterMax0(th, 0.0);
        if (damage_.Destroyed()) {
            for (THRUSTER_HANDLE th : ana_) SetThrusterMax0(th, 0.0);
            for (THRUSTER_HANDLE th : plan_) SetThrusterMax0(th, 0.0);
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
    // Proper acceleration: all forces but gravity - thrust, air AND the ground (a hard contact is a g-spike);
    // the compensator takes off what it cancels of the anamezon thrust. Crew and structure feel the rest.
    VECTOR3 F, W;
    GetForceVector(F);
    GetWeightVector(W);
    const double proper = length(F - W) / GetMass();
    accelG_ = (std::max)(0.0, proper - drive_.CompensatedAccel()) / G0;
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
    if (big > 0.02) {
        terrainOfs_ += sum / 5.0;
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
    x.sternCups = (std::max)(irisPlan_, irisAna_);
    dm::Ground g;
    VECTOR3 v;
    GetGroundspeedVector(FRAME_HORIZON, v);
    g.contact = GroundContact();
    g.touchdown = g.contact && !wasContact_;
    g.vDown = -lastVy_;
    g.gearDown = carriage_.Gear() > 0.5;
    g.legRatio[0] = g.legRatio[1] = colRatio_;
    for (int i = 2; i < 6; ++i) g.legRatio[i] = sternRatio_;
    if (!g.contact) lastVy_ = v.y;
    wasContact_ = g.contact;
    damage_.Step(dt, f, x, g, GetDamageModel() != 0);
    dm::Event ev[16];
    const int n = damage_.TakeEvents(ev, 16);
    for (int i = 0; i < n; ++i) {
        Message(ev[i].destroyed ? "РАЗРУШЕНИЕ: %s" : "Оторван: %s", ev[i].destroyed ? "DESTROYED: %s" : "Torn off: %s",
                russian_ ? ev[i].ru : ev[i].en, ev[i].en);
        BreakPart(ev[i].part);
    }
    if (damage_.Destroyed() && !shipGone_) BreakUp();
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
    char vname[64];
    std::snprintf(vname, sizeof vname, "%s_debris_%02d", GetName(), ++debrisCount_);
    oapiCreateVesselEx(vname, d->cls, &vs);
}

// A part torn off: its debris leaves from where the part is now.
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
        case dm::kLegPort: case dm::kLegStbd: {
            const tantra::CarriagePose& p = carriage_.Pose();
            const double side = part == dm::kLegPort ? -1.0 : 1.0;
            VECTOR3 c = _V(side * m::kHipXOut, -0.5 * p.mastLen, p.hipS - frameS_);  // the mast, hanging
            SpawnDebris(part == dm::kLegPort ? "leg_port" : "leg_starboard", &c, _V(side * 1.5, 0, 0), 0.3);
            break;
        }
        case dm::kSternLeg0: case dm::kSternLeg1: case dm::kSternLeg2: case dm::kSternLeg3: {
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
    SpawnDebris("hull_aft", nullptr, _V(0, 0, -6), 0.15);
    SpawnDebris("hull_mid", nullptr, _V(0, 4, 0), 0.25);
    SpawnDebris("hull_fore", nullptr, _V(0, -3, 5), 0.2);
    SpawnDebris("hull_nose", nullptr, _V(0, 2, 10), 0.4);
    for (int part = 0; part < dm::kPartCount; ++part)
        if (!damage_.Lost(part) && part != dm::kHull && part != dm::kNose && part != dm::kBelly && part != dm::kSternCups) {
            const bool stowed = (part >= dm::kPod0 && part <= dm::kPod3 && podOut_ < 0.5) ||
                                (part >= dm::kLegPort && part <= dm::kSternLeg3 && carriage_.Gear() < 0.5) ||
                                (part == dm::kHangarDoors && hangar_ < 0.5) || (part == dm::kBayDoors && bayDoors_ < 0.5);
            if (!stowed) BreakPart(part);  // stowed parts stay inside their chunk
        }
    UpdateDamageVisual(true);
}

void Tantra::clbkVisualCreated(VISHANDLE vis, int) {
    vis_ = vis;
    UpdateDamageVisual(true);
}

void Tantra::clbkVisualDestroyed(VISHANDLE vis, int) {
    if (vis == vis_) vis_ = nullptr;
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
            case dm::kPod0: g[n++] = m::GRP_POD_0; g[n++] = m::GRP_DOOR_POD_0; break;
            case dm::kPod1: g[n++] = m::GRP_POD_1; g[n++] = m::GRP_DOOR_POD_1; break;
            case dm::kPod2: g[n++] = m::GRP_POD_2; g[n++] = m::GRP_DOOR_POD_2; break;
            case dm::kPod3: g[n++] = m::GRP_POD_3; g[n++] = m::GRP_DOOR_POD_3; break;
            case dm::kLegPort:
            case dm::kLegStbd: {
                const bool port = part == dm::kLegPort;
                g[n++] = port ? m::GRP_ANKLE_PORT : m::GRP_ANKLE_STARBOARD;
                g[n++] = port ? m::GRP_PAD_PORT : m::GRP_PAD_STARBOARD;
                for (int i = 0; i < 10; ++i) g[n++] = (port ? m::GRP_SHIN_PORT_0 : m::GRP_SHIN_STARBOARD_0) + i;
                break;
            }
            case dm::kSternLeg0: case dm::kSternLeg1: case dm::kSternLeg2: case dm::kSternLeg3: {
                const int i = part - dm::kSternLeg0, stride = m::GRP_LEG1_THIGH - m::GRP_LEG0_THIGH;
                for (int k = 1; k < stride; ++k) g[n++] = m::GRP_LEG0_THIGH + i * stride + k;  // shins, ankle, pad
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
    for (int part = 0; part < dm::kPartCount && !shipGone_; ++part) {
        const bool lost = damage_.Lost(part);
        if (!force && lost == shownLost_[part]) continue;
        int g[16];
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
bool Tantra::TouchPointLost(int i, const tantra::CarriagePose& p) const {
    namespace dm = tantra::damage;
    int part = -1;
    if (p.nTouch == 6) {
        static const int kRest[6] = {dm::kLegStbd, dm::kLegPort, dm::kSternLeg3, dm::kSternLeg2, dm::kLegStbd, dm::kLegPort};
        part = kRest[i];
    } else if (p.nTouch == 4) {
        part = p.onColumns ? (p.touch[i].x >= 0.0 ? dm::kLegStbd : dm::kLegPort) : dm::kSternLeg0 + i;
    }
    return part >= 0 && damage_.Lost(part);
}

// The ship's computer watches the contacts: with the gear out, just off the ground, no engine able to
// lift it and yet climbing (or spinning on its pads) - that is a contact bounce, not flight. It puts
// the ship straight back down where it is, with the same heading, and settles the suspension again.
void Tantra::GuardAgainstLaunch(double dt) {
    const bool contact = GroundContact();
    sinceContact_ = contact ? 0.0 : sinceContact_ + dt;
    if (relandCool_ > 0.0) relandCool_ -= dt;
    VECTOR3 v, w, up;
    GetGroundspeedVector(FRAME_HORIZON, v);
    GetAngularVel(w);
    HorizonInvRot(_V(0, 1, 0), up);
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
            if (GroundContact()) sound_->PlayWav(SND_SIREN);  // deadly zone around the stern
        } else if (now < prev) sound_->PlayWav(SND_SHUTDOWN);
    }

    const double level = GetThrusterGroupLevel(THGROUP_MAIN);
    const bool anaOn = AnaIsMain() && level > 0.0;
    const double podLevel = (std::max)(podHover_ ? GetThrusterGroupLevel(THGROUP_HOVER) : 0.0, planGroup_ == 1 ? level : 0.0);
    const bool ionOn = podLevel > 0.0;
    // XRSound has no pitch control: loudness follows thrust (pitch layers are a later step).
    if (anaOn) sound_->PlayWav(SND_ANA_RUN, true, float(0.35 + 0.65 * level));
    else if (sound_->IsWavPlaying(SND_ANA_RUN)) sound_->StopWav(SND_ANA_RUN);
    if (ionOn) sound_->PlayWav(SND_ION_RUN, true, float(0.35 + 0.65 * podLevel));
    else if (sound_->IsWavPlaying(SND_ION_RUN)) sound_->StopWav(SND_ION_RUN);
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

void Tantra::ActEva() {
    if (GroundContact() && rovers_ < 0.99) {
        Message("Выход на грунт - с платформы ангара: откройте ангар (O) и опустите платформу (Shift+O)",
                "Ground EVA from the hangar platform: open the hangar (O) and lower the platform (Shift+O)");
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
    carriage_.Update(dt, frameS_);
    auto step = [dt](double v, double t, double rate) {
        return v < t ? (std::min)(t, v + rate * dt) : (std::max)(t, v - rate * dt);
    };
    tuck_ = step(tuck_, crestsFolded_ ? 1.0 : 0.0, 0.1);
    hangar_ = step(hangar_, hangarT_, 0.08);
    rovers_ = step(rovers_, hangar_ > 0.97 ? roversT_ : 0.0, 0.05);
    // Anamezon cups open with the chamber field; the planetary cups close while anamezon feeds.
    const bool anaActive = ignition_.Target() != IgnStage::Off || ignition_.FieldLevel() > 0.0;
    irisAna_ = step(irisAna_, anaActive ? 1.0 : 0.0, 0.25);
    irisPlan_ = step(irisPlan_, AnaIsMain() ? 0.0 : 1.0, 0.5);
    const bool level = carriage_.Progress() <= 0.0 && carriage_.Gear() >= 1.0 && GroundContact();
    airlockUp_ = step(airlockUp_, level ? 0.0 : 1.0, 0.1);

    TantraGear::Extras ex;
    ex.tuck = tuck_;
    ex.podSwivel = podAngle_ / sp::kPodSwivelMaxDeg;
    ex.podStow = 1.0 - podOut_;
    ex.elevon[0] = elevon_[0];
    ex.elevon[1] = elevon_[1];
    ex.bodyFlap = bodyFlap_;
    ex.irisAna = irisAna_;
    ex.irisPlan = irisPlan_;
    ex.hangar = hangar_;
    ex.rovers = rovers_;
    ex.airlockUp = airlockUp_;
    ex.bayDoors = bayDoors_;
    for (int i = 0; i < 4; ++i) ex.trapHidden[i] = !trapPresent_[i];
    ex.liftY[0] = liftY_[0];
    ex.liftY[1] = liftY_[1];
    if (gear_) gear_->Apply(carriage_.Pose(), frameS_, ex);

    const tantra::CarriagePose& p = carriage_.Pose();
    // Aerodynamic state: what the crests, fin, gear and pods present to the flow.
    {
        const double fold = (std::max)(tuck_, p.tuck);
        namespace dm = tantra::damage;
        aeroFin_ = (1.0 - fold) * (damage_.Lost(dm::kFin) ? 0.0 : 1.0);
        aeroCrest_ = (1.0 - fold) * ((damage_.Lost(dm::kCrestPort) ? 0.0 : 0.5) + (damage_.Lost(dm::kCrestStbd) ? 0.0 : 0.5));
        const double legs = carriage_.Gear();  // two carriage legs + two lower stern legs, pads
        aeroGearArea_ = legs * (2.0 * (6.0 * 8.0 + 5.4 * (std::max)(0.0, p.mastLen - 8.0) + 16.0 * 1.2) + 2.0 * 2.6 * 12.0) +
                        podOut_ * 4.0 * (3.0 * 1.8 + 0.3 * 6.4 * 3.8);
    }
    // Carriage drives: each side lags the trunnion motion (0.30 s / 0.42 s servo time) plus a little
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
                const double target = moving ? servo[i] * hDot + driveNoise_[i] : 0.0;  // a lagging leg is short: its pad sits higher
                driveLag_[i] += (target - driveLag_[i]) * (std::min)(1.0, dt / 0.7);
            }
        }
    }
    VECTOR3 t[tantra::CarriagePose::kMaxTouch];
    // Retune the suspension to the mass (cassettes in / out) and when the settling period ends.
    bool changed = std::fabs(GetMass() - touchMass_) > 0.05 * touchMass_ || (settleTimer_ <= 0.0) != touchSettled_;
    VECTOR3 upL = _V(0, 1, 0);
    if (simdt > 0.0) HorizonInvRot(_V(0, 1, 0), upL);  // not while the scenario loads: no horizon frame yet
    touchOfs_ = upL * terrainOfs_;  // refined terrain: points raised, relaxing (WatchTerrain)
    int nt = 0;
    for (int i = 0; i < p.nTouch; ++i) {
        if (TouchPointLost(i, p)) continue;  // a broken leg carries nothing: the ship settles onto the hull
        t[nt++] = _V(p.touch[i].x, p.touch[i].y, p.touch[i].z) + touchOfs_ +
               upL * (p.touch[i].x >= 0.0 ? driveLag_[1] : driveLag_[0]);  // side error of the drives
        if (nt - 1 >= nTouch_ || length(t[nt - 1] - touch_[nt - 1]) > 1e-4) changed = true;
    }
    if (nt != nTouch_) changed = true;
    if (changed) {
        SetSuspension(t, nt);
        for (int i = 0; i < nt; ++i) touch_[i] = t[i];
        nTouch_ = nt;
    }
    // The airlock lift stands on the ground under the door, whatever height the gear holds the ship at.
    crew_.SetLiftFoot(_V(kEvaPos.x, -p.trunnionH + 0.93, Zf(kCrewLiftS)));   // beside the lowered hangar platform
}

// Orbiter 2016 touchdown vertices: the pads of the current gear pose are elastic (MR-fluid cushions
// under the pads and the leg bands); the hull points around them only matter in a belly landing.
// Stiffness gives kSag of static compression at the design gravity, damping is a fraction of critical,
// so the ship settles and sways a little on its legs. Orbiter integrates the contacts explicitly: a spring
// or a damper that is too stiff for the time step pumps energy in and throws the ship off the planet (the
// first frames after loading have long steps, time warp even longer). So: hull points no stiffer than the
// legs, damping 2*zeta*omega kept under ~6 1/s (zeta 0.35 settling, 0.3 after), and GuardAgainstLaunch.
void Tantra::SetSuspension(const VECTOR3* t, int n) {
    const double m = GetMass(), kSag = 0.25, kGref = 1.7 * G0, kMu = 0.8;
    const double kZeta = settleTimer_ > 0.0 ? 0.35 : 0.3;
    touchSettled_ = settleTimer_ <= 0.0;
    const double k = m * kGref / ((std::max)(n, 1) * kSag);  // all legs broken: the hull points still carry
    const double c = 2.0 * kZeta * std::sqrt(k * m / (std::max)(n, 1));
    const double kHull = k, cHull = 2.0 * 0.35 * std::sqrt(kHull * m / 3.0);
    const double zs = Zf(0.0), zn = Zf(sp::kLength - 1.2);
    const VECTOR3 hull[] = {
        {0, 0, zn},                                                        // nose tip
        {0, 14.0, zs}, {0, -9.6, zs}, {14.0, -4.0, zs}, {-14.0, -4.0, zs},  // stern plane
        {0, 2.2, zs - 4.0}, {0, -9.3, zs - 4.0},                           // engine well rim
        {14.6, -4.0, Zf(30.0)}, {-14.6, -4.0, Zf(30.0)}, {0, 14.4, Zf(30.0)}, {0, -9.6, Zf(30.0)},
        {9.0, -3.8, Zf(100.0)}, {-9.0, -3.8, Zf(100.0)}, {0, 8.9, Zf(100.0)}, {0, -8.7, Zf(100.0)},
    };
    TOUCHDOWNVTX v[tantra::CarriagePose::kMaxTouch + sizeof hull / sizeof hull[0]];
    for (int i = 0; i < n; ++i) v[i] = {t[i], k, c, kMu, kMu};
    int nv = n;
    for (const VECTOR3& h : hull) v[nv++] = {h + touchOfs_, kHull, cHull, 0.5, 0.5};
    SetTouchdownPoints(v, nv);
    touchMass_ = m;
}

void Tantra::ActErect() {
    const bool up = carriage_.Target() < 3.0;
    if (!carriage_.CommandErect(up, GroundContact())) {
        Message("Лафет: только на грунте, шасси выпущено", "Carriage: on the ground with the gear down only");
        return;
    }
    if (up) Message("Лафет: подъём на корму (90 с), гребни и гондолы убираются", "Carriage: standing up on the stern (90 s)");
    else Message("Лафет: укладка в горизонт", "Carriage: laying the ship level");
}

void Tantra::ActGear() {
    const bool down = !carriage_.GearDown();
    if (!down && GroundContact()) {
        Message("Шасси нельзя убрать на грунте", "Gear cannot be stowed on the ground");
        return;
    }
    if (!carriage_.CommandGear(down, GroundContact())) {
        Message("Шасси: дождитесь конца подъёма", "Gear: wait for the carriage to finish");
        return;
    }
    const bool standing = carriage_.Set() == tantra::Carriage::FlightSet::Standing;
    if (down) Message(standing ? "Шасси: кормовые лапы (посадка на корму)" : "Шасси: колонны лафета и нижние лапы (посадка лёжа)",
                      standing ? "Gear down: stern legs (tail-first landing)" : "Gear down: carriage columns (level landing)");
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
            standing ? "Landing set: tail-first (stern legs)" : "Landing set: level (carriage columns)");
}

void Tantra::ActCrests() {
    crestsFolded_ = !crestsFolded_;
    Message(crestsFolded_ ? "Гребни сложены в ниши, перо убрано, гондолы в отсеках" : "Гребни раскрыты, перо выдвинуто",
            crestsFolded_ ? "Crests folded into their recesses, fin down, pods in the bays" : "Crests out, fin up");
}

void Tantra::ActHangar() {
    if (hangarT_ < 0.5 && !(carriage_.Progress() <= 0.0 && carriage_.Gear() >= 1.0)) {
        Message("Ангар открывается только лёжа на колоннах", "Hangar opens only while resting level");
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
    if (key == OAPI_KEY_D && shift && ctrl) {
        damage_.Repair();
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
                     : planGroup_ == 1 ? L("ПЛАНЕТАРНЫЕ", "PLANETARY") : L("нет (гондолы на висении)", "none (pods in hover)"));
    double pct[sp::kTrapCount];
    for (int i = 0; i < sp::kTrapCount; ++i) pct[i] = 100.0 * GetPropellantMass(trap_[i]) / prm_.trapFuelMass;
    line(L("Ловушки: %.0f%% %.0f%% %.0f%% %.0f%%  подача из %d", "Traps: %.0f%% %.0f%% %.0f%% %.0f%%  feed %d"), pct[0],
         pct[1], pct[2], pct[3], activeTrap_ + 1);
    line(L("Ионные заряды: %.1f%%", "Ion charges: %.1f%%"), 100.0 * GetPropellantMass(ion_) / prm_.ionChargeMass);
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
    {
        static const char* kPhRu[] = {"лежит", "подготовка", "подъём на колоннах", "поворот", "лапы выходят", "опускание",
                                      "сбор мачт", "стоит на корме"};
        static const char* kPhEn[] = {"level", "preparing", "lifting", "turning", "legs out", "lowering", "reeling in",
                                      "standing"};
        const tantra::CarriagePose& cp = carriage_.Pose();
        const int ph = carriage_.Phase();
        if (carriage_.Gear() <= 0.0)
            line(L("Шасси убрано (посадка: %s, N/Shift+N)", "Gear stowed (landing set: %s, N/Shift+N)"),
                 carriage_.Set() == tantra::Carriage::FlightSet::Standing ? L("на корму", "tail-first") : L("лёжа", "level"));
        else
            line(L("Лафет: %s  угол %.0f°  цапфы %.0f м%s", "Carriage: %s  pitch %.0f deg  trunnions %.0f m%s"),
                 russian_ ? kPhRu[ph] : kPhEn[ph], cp.theta * DEG, cp.trunnionH,
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
