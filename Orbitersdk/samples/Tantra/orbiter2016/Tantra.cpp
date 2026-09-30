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

// Port airlock; the small lift stands on the ground 12.5 m off the axis.
const double kDoorZ = sp::Z(sp::kDoorS);
const VECTOR3 kEvaPos = {-12.5, -sp::kAxisHeight + 1.5, kDoorZ};

// Attitude micro-motor blocks.
const double kAttNoseZ = sp::Z(sp::kAttNoseS), kAttTailZ = sp::Z(sp::kAttTailS);
const double kAttNoseR = sp::kAttNoseR, kAttTailR = sp::kAttTailR;

// Cup offsets on the aft face of an auxiliary pod (one row of three), pod-local y.
const double kCupDY[sp::kCupsPerPod] = {-0.9, 0.0, 0.9};
const double kPodFaceDZ = -3.1;  // aft face relative to the pod centre

// --- Aerodynamic coefficients -------------------------------------------------

// Slender body. Axial drag on the aft frontal area, cross-flow drag on the side.
void BodyVertical(VESSEL*, double aoa, double M, double, void*, double* cl, double* cm, double* cd) {
    const double s = std::sin(aoa), c = std::cos(aoa);
    const double wave = M > 0.8 ? 0.35 * (std::min)(1.0, (M - 0.8) / 0.4) : 0.0;
    *cl = 0.15 * s * c;
    *cm = 0.0;
    *cd = 0.08 + wave + 4.4 * s * s;
}
void BodyHorizontal(VESSEL*, double beta, double, double, void*, double* cl, double* cm, double* cd) {
    const double s = std::sin(beta), c = std::cos(beta);
    *cl = 0.15 * s * c;
    *cm = 0.0;
    *cd = 4.4 * s * s;  // axial part already counted by the vertical body airfoil
}

// Low-aspect fins: linear lift plus cross-flow normal force.
struct FinData { double aspect; };
FinData kDorsalFin = {0.9};
FinData kLateralFins = {1.33};
void Fin(VESSEL*, double aoa, double M, double, void* ctx, double* cl, double* cm, double* cd) {
    const double A = static_cast<FinData*>(ctx)->aspect;
    const double k = 2.0 * PI * A / (A + 2.0);
    const double s = std::sin(aoa), c = std::cos(aoa);
    *cl = k * s * c + 1.8 * s * std::fabs(s) * c;
    *cm = 0.0;
    *cd = 0.015 + 1.8 * std::fabs(s * s * s) + (M > 0.8 ? 0.02 : 0.0);
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
    g.trackS0 = m::kCarS0;
    g.trackS1 = m::kCarS1;
    g.stowS = m::kStowS;
    g.standR = m::kStandR;
    for (int i = 0; i < 4; ++i) g.standFoot[i] = {m::kLegs[i].radial.x * m::kStandR, m::kLegs[i].radial.y * m::kStandR, 0.0};
    TantraGear::LegRestFoot(g.legRestX, g.legRestS);
    carriage_.SetGeometry(g);
    gear_ = new TantraGear(this, meshIdx_);
    carriage_.Update(0.0, sp::kOriginS);
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
    // Two small swivelling pods on the shoulder (hover of the light ship). Thrust acts on the CG
    // line (see Spec.h); the flame is drawn at the real cups, which move with the pod.
    for (int p = 0; p < sp::kPodCount; ++p) {
        const double side = p == 0 ? -1.0 : 1.0;
        for (int c = 0; c < sp::kCupsPerPod; ++c) {
            const int k = p * sp::kCupsPerPod + c;
            pod_[k] = CreateThruster(_V(side * sp::kPodX, kCupDY[c], 0.0), fwd, prm_.podThrustTotal / sp::kPodCups, ion_,
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
    const double frontal = PI * sp::kAftHalfWidth * 0.5 * (sp::kAftTop - sp::kAftBottom);
    CreateAirfoil3(LIFT_VERTICAL, _V(0, 0, 0), BodyVertical, nullptr, sp::kLength, frontal, 0.1);
    CreateAirfoil3(LIFT_HORIZONTAL, _V(0, 0, 0), BodyHorizontal, nullptr, sp::kLength, frontal, 0.1);
    // Crests of equilibrium: dorsal fin holds heading, lateral fins hold pitch.
    CreateAirfoil3(LIFT_HORIZONTAL, _V(0, 24.0, sp::Z(22.0)), Fin, &kDorsalFin, 28.0, 650.0, kDorsalFin.aspect);
    CreateAirfoil3(LIFT_VERTICAL, _V(0, -1.0, sp::Z(16.5)), Fin, &kLateralFins, 16.5, 445.0, kLateralFins.aspect);
}

void Tantra::DefineCrew() {
    crew_.Init(this, kEvaPos, sp::kCrewSeats);
    for (const CrewSeed& c : kCrew) crew_.AddMember(c.name, c.age, c.pulse, c.weight, c.role);
}

void Tantra::clbkPostCreation() {
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
    std::snprintf(buf, sizeof buf, "%.1f %.1f", podAngle_, podTarget_);
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
    carriage_.Update(0.0, sp::kOriginS);
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
    const bool hover = podAngle_ >= 45.0 && tuck_ < 0.5 && carriage_.Pose().tuck < 0.5;
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

void Tantra::UpdatePods(double dt) {
    const double step = sp::kPodSwivelRate * dt;
    if (podAngle_ < podTarget_) podAngle_ = (std::min)(podTarget_, podAngle_ + step);
    else if (podAngle_ > podTarget_) podAngle_ = (std::max)(podTarget_, podAngle_ - step);
    const double a = podAngle_ * RAD, cant = sp::kPodCantDeg * RAD;
    const double sunk = (std::max)(tuck_, carriage_.Pose().tuck);
    const double x = sp::kPodX + (sp::kPodXIn - sp::kPodX) * sunk;
    for (int p = 0; p < sp::kPodCount; ++p) {
        const double side = p == 0 ? -1.0 : 1.0;
        // Cups canted outboard: the jet leaves outward, the thrust leans inward.
        const VECTOR3 dir = _V(-side * std::sin(cant), std::sin(a) * std::cos(cant), std::cos(a) * std::cos(cant));
        for (int c = 0; c < sp::kCupsPerPod; ++c) {
            const int k = p * sp::kCupsPerPod + c;
            SetThrusterDir(pod_[k], dir);
            // Cup on the aft face, turned with the pod about its lateral axis (mesh: pod_swivel).
            const double cy = kCupDY[c], cz = kPodFaceDZ;
            podExhPos_[k] = _V(side * x, cy * std::cos(a) - cz * std::sin(a), sp::Z(sp::kPodS) + cy * std::sin(a) + cz * std::cos(a));
            podExhDir_[k] = dir;
        }
    }
    RebindGroups();
}

void Tantra::ActPodsTo(double deg) {
    podTarget_ = (std::max)(0.0, (std::min)(sp::kPodSwivelMaxDeg, deg));
    Message("Гондолы планетарных: поворот на %.0f°", "Planetary pods: swivel to %.0f deg", podTarget_);
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
    for (THRUSTER_HANDLE th : pod_) {  // sunk pods give no thrust
        const double sunk = (std::max)(tuck_, carriage_.Pose().tuck);
        SetThrusterMax0(th, sunk > 0.5 ? 0.0 : prm_.podThrustTotal / sp::kPodCups * f);
        SetThrusterIsp(th, prm_.planExhaust * f);
    }
    ScaleAttitudeThrust();
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
    const double total = length(t + l + d) / GetMass();
    accelG_ = (std::max)(0.0, total - drive_.CompensatedAccel()) / G0;  // what the crew feels

    if (crew_.Process(simdt) == TantraCrew::Event::Returned)
        Message("%s вернулся на борт", "%s is back aboard", crew_.LastName());

    if (messageTimer_ > 0.0) messageTimer_ -= simdt;
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
    switch (crew_.Eva(selectedCrew_)) {
        case TantraCrew::EvaResult::Ok:
            Message("%s вышел наружу", "%s is outside", crew_.LastName());
            selectedCrew_ = 0;
            break;
        case TantraCrew::EvaResult::NoOne: Message("На борту никого нет", "Nobody aboard"); break;
        case TantraCrew::EvaResult::AirlockClosed: Message("Шлюз закрыт - сначала откройте", "Airlock closed - open it first"); break;
        case TantraCrew::EvaResult::NotLanded: Message("Выход - только на грунте (подъёмник шлюза)", "EVA from the ground lift only"); break;
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
    carriage_.Update(dt, sp::kOriginS);
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
    ex.irisAna = irisAna_;
    ex.irisPlan = irisPlan_;
    ex.hangar = hangar_;
    ex.rovers = rovers_;
    ex.airlockUp = airlockUp_;
    ex.bayDoors = bayDoors_;
    for (int i = 0; i < 4; ++i) ex.trapHidden[i] = !trapPresent_[i];
    ex.liftY[0] = liftY_[0];
    ex.liftY[1] = liftY_[1];
    if (gear_) gear_->Apply(carriage_.Pose(), sp::kOriginS, ex);

    const tantra::CarriagePose& p = carriage_.Pose();
    VECTOR3 t[tantra::CarriagePose::kMaxTouch];
    // Retune the suspension to the mass (cassettes in / out) and when the settling period ends.
    bool changed = std::fabs(GetMass() - touchMass_) > 0.05 * touchMass_ || p.nTouch != nTouch_ ||
                   (settleTimer_ <= 0.0) != touchSettled_;
    for (int i = 0; i < p.nTouch; ++i) {
        t[i] = _V(p.touch[i].x, p.touch[i].y, p.touch[i].z);
        if (i >= nTouch_ || length(t[i] - touch_[i]) > 1e-4) changed = true;
    }
    if (changed) {
        SetSuspension(t, p.nTouch);
        for (int i = 0; i < p.nTouch; ++i) touch_[i] = t[i];
        nTouch_ = p.nTouch;
    }
    // The airlock lift stands on the ground under the door, whatever height the gear holds the ship at.
    crew_.SetLiftFoot(_V(kEvaPos.x, -p.trunnionH + 0.93, kEvaPos.z));
}

// Orbiter 2016 touchdown vertices: the pads of the current gear pose are elastic (MR-fluid cushions
// under the pads and the leg bands), the hull points around them are stiff and only matter in a crash.
// Stiffness gives kSag of static compression at the design gravity, damping is a fraction of critical,
// so the ship settles and sways a little on its legs. Right after the scenario is loaded (or the ship is
// put down somewhere) the damping is overdamped for a few seconds: Orbiter drops the ship onto its
// contacts and it must not bounce.
void Tantra::SetSuspension(const VECTOR3* t, int n) {
    const double m = GetMass(), kSag = 0.25, kGref = 1.7 * G0, kMu = 0.8;
    const double kZeta = settleTimer_ > 0.0 ? 1.5 : 0.3;
    touchSettled_ = settleTimer_ <= 0.0;
    const double k = m * kGref / (n * kSag);
    const double c = 2.0 * kZeta * std::sqrt(k * m / n);
    const double kHull = 20.0 * k * n / 3.0, cHull = 2.0 * 0.7 * std::sqrt(kHull * m / 3.0);
    const double zs = sp::kSternZ, zn = sp::kNoseZ;
    const VECTOR3 hull[] = {
        {0, 0, zn},                                         // nose tip
        {0, 13.0, zs}, {0, -9.0, zs}, {12.0, 0, zs}, {-12.0, 0, zs},       // stern plane
        {0, 0, zs - 4.0},                                  // engine well rim, on the axis
        {14.0, 0, sp::Z(30.0)}, {-14.0, 0, sp::Z(30.0)}, {0, 14.0, sp::Z(30.0)}, {0, -9.5, sp::Z(30.0)},
        {8.6, 0, sp::Z(100.0)}, {-8.6, 0, sp::Z(100.0)}, {0, 8.6, sp::Z(100.0)}, {0, -8.6, sp::Z(100.0)},
    };
    TOUCHDOWNVTX v[tantra::CarriagePose::kMaxTouch + sizeof hull / sizeof hull[0]];
    for (int i = 0; i < n; ++i) v[i] = {t[i], k, c, kMu, kMu};
    int nv = n;
    for (const VECTOR3& h : hull) v[nv++] = {h, kHull, cHull, 0.5, 0.5};
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
    Message(crestsFolded_ ? "Гребни сложены в тень брони, гондолы утоплены" : "Гребни раскрыты, гондолы выдвинуты",
            crestsFolded_ ? "Crests folded, pods sunk" : "Crests out, pods out");
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
    if (key == OAPI_KEY_B && !ctrl) { ActPodsTo(shift ? 0.0 : 90.0); return 1; }
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
    line(L("Гондолы: %.0f° (цель %.0f°) - %s", "Pods: %.0f deg (target %.0f) - %s"), podAngle_, podTarget_,
         podHover_ ? L("висение", "HOVER") : L("не в группе", "idle"));
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
