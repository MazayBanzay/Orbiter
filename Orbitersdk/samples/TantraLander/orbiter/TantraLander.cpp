// «Грань» 25,4 м - the Orbiter vessel module TantraLander.dll: Lander.msh (gen_lander.py ×1,285 with the fin) driven by the
// systems core (core/Lander*): the marches with УВТ and the afterburner, the six lift cups in two rows, the argon attitude
// thrusters, the transfer between the three tanks moving the CG, the wing tips / flap / elevons / fin / gear by mode, the
// gear on shock struts (touchdown springs + the struts' travel drawn), the aerodynamics by the mesh's own tables.
// The launch MFD («GRAN START»): the separation speed (space 2–3 m/s, the pilot sets it), the start from the cradle of
// «Тантра» (the child attachment «TLANDER»; the magnetic throw in an atmosphere is the cradle's own).
// Keys (the lander in focus): 1..8 the mode, G gear, B afterburner, K fin, O hatch, = / - the hover height (Shift ×10),
// E the emergency mode, P the pre-entry transfer, N the nose blow. The stick: flight - the elevons (AF) and RCS; hover -
// the tilt command (the automat holds it), the yaw.
// The cabin (LanderCockpit, LanderCabin.msh): the drum for two in the virtual cockpit, the four Orbiter MFDs, the two
// ships' MFDs with the pilots' system keys (their pages and touch buttons are here: the CockpitHost side).
#define ORBITER_MODULE
#define NOMINMAX
#include "Orbitersdk.h"
#include "LanderMesh.h"
#include "LanderCabinMesh.h"
#include "LanderCockpit.h"
#include "../core/LanderCore.h"
#include "../core/LanderSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ln = tantra::lander;
namespace sp = tantra::lander::spec;
namespace mm = tantra::lander::mesh;

namespace {

constexpr double kRad = PI / 180.0;
inline VECTOR3 V(const double p[3]) { return _V(p[0], p[1], p[2]); }
inline double Clamp(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
inline double Approach(double v, double t, double rate, double dt) { return v + Clamp(t - v, -rate * dt, rate * dt); }

// ---------------------------------------------------------------- aerodynamics
struct AeroCtx { double tip = 0, flap = 0, fin = 1; };   // tips deg, flap deg, the fin up 0..1

double Lerp(const double* x, int n, int stride, const double* y, double v) {
    if (v <= x[0]) return y[0];
    if (v >= x[n - 1]) return y[(n - 1) * stride];
    int i = 0;
    while (i < n - 2 && x[i + 1] < v) ++i;
    const double f = (v - x[i]) / (x[i + 1] - x[i]);
    return y[i * stride] + (y[(i + 1) * stride] - y[i * stride]) * f;
}

// Newton by the mesh: tips 0/60/90 interpolated, α 0..90 the main tables, −20..180 the extended one (tips 60)
void Hyp(double aDeg, double tip, double out[3]) {
    double sgn = 1.0;
    if (aDeg < -20.0) { aDeg = -aDeg; sgn = -1.0; }               // underside-up flow: mirrored (допущение)
    if (aDeg < 0.0 || aDeg > 90.0) {
        for (int c = 0; c < 3; ++c) out[c] = Lerp(mm::kExtAlpha, mm::kNx, 3, &mm::kExt60[0][c], aDeg);
    } else {
        const double t = Clamp(tip, 0.0, 90.0);
        const int i = t < 60.0 ? 0 : 1;
        const double f = i == 0 ? t / 60.0 : (t - 60.0) / 30.0;
        for (int c = 0; c < 3; ++c) {
            const double a = Lerp(mm::kAlpha, mm::kNa, 3, &mm::kHyp[i][0][c], aDeg);
            const double b = Lerp(mm::kAlpha, mm::kNa, 3, &mm::kHyp[i + 1][0][c], aDeg);
            out[c] = a + (b - a) * f;
        }
    }
    out[0] *= sgn; out[2] *= sgn;
}

double FlapDCm(double aDeg, double flap) {
    if (flap <= 0.0 || aDeg < 0.0 || aDeg > 90.0) return 0.0;
    int k = 0;
    while (k < mm::kNfl - 2 && mm::kFlapDeg[k + 1] < flap) ++k;
    const double f = Clamp((flap - mm::kFlapDeg[k]) / (mm::kFlapDeg[k + 1] - mm::kFlapDeg[k]), 0.0, 1.0);
    const double a = Lerp(mm::kAlpha, mm::kNa, 1, mm::kFlapDCm[k], aDeg), b = Lerp(mm::kAlpha, mm::kNa, 1, mm::kFlapDCm[k + 1], aDeg);
    return a + (b - a) * f;
}

// body + wing: subsonic Polhamus (the vortex lift of the rounded leading edge) at the planform centroid, Newton above M 3,
// blended between; all on kSref / kLref, the moment about kXref
void VLift(VESSEL*, double aoa, double M, double, void* ctx, double* cl, double* cm, double* cd) {
    const AeroCtx& a = *static_cast<AeroCtx*>(ctx);
    const double s = std::sin(aoa), c = std::cos(aoa), k = mm::kSplan / mm::kSref;
    const double CN = (mm::kKp * s * c + mm::kKv * s * std::fabs(s)) * k, CA = mm::kCD0 * k;
    const double clS = CN * c - CA * s, cdS = CN * s + CA * c;
    const double cmS = CN * (mm::kXacSub - mm::kXref) / mm::kLref;
    double h[3];
    const double aDeg = aoa / kRad;
    Hyp(aDeg, a.tip, h);
    h[2] += FlapDCm(aDeg, a.flap);
    const double w = Clamp((M - 0.9) / 2.1, 0.0, 1.0);
    *cl = clS + (h[0] - clS) * w;
    *cm = cmS + (h[2] - cmS) * w;
    *cd = cdS + (h[1] - cdS) * w + oapiGetWaveDrag(M, 0.75, 1.0, 1.1, 0.04) * (1.0 - w);
}

// the body's side force ahead of the CG (destabilising, Cnβ ≈ −0,03 with the fin folded; допущение by the fin calc)
void HBody(VESSEL*, double beta, double, double, void*, double* cl, double* cm, double* cd) {
    *cl = 0.5 * std::sin(beta) * std::cos(beta);
    *cm = 0.0;
    *cd = 0.02 + 0.6 * std::sin(beta) * std::sin(beta);
}

// the fin: CLα 3,0 (DATCOM, Aэфф = 1,55·A) × sidewash 1,04, stalls past ~25°; nothing when folded
void HFin(VESSEL*, double beta, double, double, void* ctx, double* cl, double* cm, double* cd) {
    const double up = static_cast<AeroCtx*>(ctx)->fin;
    *cl = up * Clamp(3.12 * std::sin(beta) * std::cos(beta), -1.15, 1.15);
    *cm = 0.0;
    *cd = up * (0.012 + 1.1 * std::sin(beta) * std::sin(beta));
}

const char* kModeKey[ln::kModeCount] = {"1", "2", "3", "4", "5", "6", "7", "8"};

}  // namespace

// ==============================================================================================================================
class Gran : public VESSEL4, public ln::CockpitHost {
public:
    // the launch MFD's side
    bool Attached() const { return att_ && GetAttachmentStatus(att_) != nullptr; }
    const char* ParentName() const { OBJHANDLE p = att_ ? GetAttachmentStatus(att_) : nullptr; return p ? oapiGetVesselInterface(p)->GetName() : ""; }
    double SepSpeed() const { return sepV_; }
    void StepSepSpeed(int dir) {
        const double st = sepV_ < 5.0 - 1e-9 || (sepV_ <= 5.0 + 1e-9 && dir < 0) ? 0.5 : 5.0;
        sepV_ = Clamp(sepV_ + dir * st, 0.5, 60.0);
    }
    const char* Separate();                   // nullptr - done, else the reason
    const ln::Snapshot& Snap() const { return core_.S(); }
    int Mode() const { return mode_; }
    Gran(OBJHANDLE h, int fmodel) : VESSEL4(h, fmodel) {}
    ~Gran() { if (font_) oapiReleaseFont(font_); }
    void clbkSetClassCaps(FILEHANDLE cfg) override;
    void clbkLoadStateEx(FILEHANDLE scn, void* vs) override;
    void clbkSaveState(FILEHANDLE scn) override;
    void clbkPostCreation() override;
    void clbkPreStep(double simt, double simdt, double mjd) override;
    int clbkConsumeBufferedKey(DWORD key, bool down, char* kstate) override;
    int clbkConsumeDirectKey(char* kstate) override;
    bool clbkDrawHUD(int mode, const HUDPAINTSPEC* hps, oapi::Sketchpad* skp) override;
    bool clbkLoadVC(int id) override { return cockpit_.LoadVC(id); }
    bool clbkVCMouseEvent(int id, int event, VECTOR3& p) override { return cockpit_.Mouse(id, event, p); }
    bool clbkVCRedrawEvent(int id, int event, SURFHANDLE surf) override { return cockpit_.Redraw(id, event, surf); }
    void clbkMFDMode(int, int) override { cockpit_.MfdMode(); }
    // the cabin's side
    bool KeyLit(int pilot, int key) const override;
    int PageLines(int pilot, int key, char out[][96], int max) const override;
    int PageButtons(int pilot, int key, const char* lbl[2]) const override;
    void PagePress(int pilot, int key, int button) override;
    void ModeStep(int dir) override;
    const char* ModeName() const override { return ln::ModeRu(mode_); }

private:
    void DefineAnimations();
    void SetTouch();
    void UpdateCG(bool force);
    void Sensors(ln::Flight& f, double dt);
    void Apply(double dt);
    void Animate(double dt);
    double ShipDist() const;
    bool HoverMode() const { return mode_ == ln::kHover || mode_ == ln::kVertLand || mode_ == ln::kTransition || mode_ == ln::kDock; }

    ln::Core core_;
    int mode_ = ln::kRunway;
    int crew_ = 2;
    double hHold_ = 30.0;
    bool ab_ = false, emerg_ = false, preEntry_ = false, nose_ = false;
    int gearCmd_ = -1;                        // -1 by mode, 0 up, 1 down
    bool finCmd_ = false, hatchCmd_ = false;  // the fin folded (also in the hangar mode), the hatch open
    double tiltCmd_[2] = {}, yawHold_ = 0, yawErr_ = 0;
    double prevPitch_ = 0, prevTilt_ = 0, prevHdg_ = 0, prevAlpha_ = 0; bool havePrev_ = false;
    double rate_[3] = {};                     // filtered: nose-up, right-up, yaw (nose right) rad/s

    // the vessel
    UINT mesh_ = 0;
    PROPELLANT_HANDLE argon_ = nullptr;
    THRUSTER_HANDLE th_[8] = {}, rcs_[mm::kRcsCount] = {}, dummyMain_ = nullptr, dummyHover_ = nullptr;
    double rcsArm_ = 10.0;
    AIRFOILHANDLE foil_[3] = {};
    AeroCtx aero_;
    double frameZ_ = 0.0;                     // the CG's Z in the mesh frame (the vessel frame's origin)
    UINT anim_[mm::kAnimCount] = {}, comp_[3] = {};
    ANIMATIONCOMPONENT_HANDLE gearComp_[3] = {};
    double pos_[mm::kAnimCount] = {};         // spec degrees now
    double gearPos_ = 0.0, doorPos_ = 1.0;   // 0 down .. 1 up; doors 0 closed .. 1 open
    double finPos_ = 0.0, hatchPos_ = 0.0;
    double compr_[3] = {};                    // m the struts' travel used
    int touchState_ = -1; double touchMass_ = 0; double touchFrame_ = 1e9;
    double massLast_ = 0;
    oapi::Font* font_ = nullptr; int fontH_ = 0;
    double rcsFlowKg_ = 0;
    double numPitch_ = 0, numBank_ = 0, numYaw_ = 0;   // the numpad held now (the hover's tilt command), −1..1
    bool wasHover_ = false;
    ATTACHMENTHANDLE att_ = nullptr;          // the child point «TLANDER»: the belly's lowest point on the cradle
    UINT cabin_ = 0;                          // the pilots' cabin (the virtual cockpit only)
    MESHHANDLE cabinTpl_ = nullptr;
    ln::Cockpit cockpit_;
    ln::Flight lastF_;                        // the sensors' last reading (the radar page)
    double sepV_ = 2.5;                       // m/s the separation speed (the user: in space 2–3 m/s, set in the launch MFD)
};

// ------------------------------------------------------------------------------------------------------------------------------
void Gran::clbkSetClassCaps(FILEHANDLE) {
    SetSize(mm::kLength * 0.5);
    SetEmptyMass(core_.Prop().MassKg() - core_.Prop().ArgonKg());
    // PMI per unit mass, kg·m²/kg: a 25,4 × 7 × 4 m body with the wing, mass towards the middle (допущение)
    SetPMI(_V(38.6, 43.5, 9.7));
    SetCrossSections(_V(80.0, 285.0, 22.0));
    SetRotDrag(_V(0.4, 0.4, 0.2));
    SetCW(0.15, 0.3, 1.0, 1.0);
    SetMaxWheelbrakeForce(sp::kMtom * 9.81 * 0.4);       // 0,4 g on the main gear's brakes (GEAR.md)
    EnableTransponder(true);

    mesh_ = AddMesh(oapiLoadMeshGlobal("Tantra\\Lander"));
    SetMeshVisibilityMode(mesh_, MESHVIS_ALWAYS | MESHVIS_EXTPASS);
    // the pilots' cabin: only in the virtual cockpit (from inside the hull's faces are culled - the view screen is a hole)
    cabinTpl_ = oapiLoadMeshGlobal("Tantra\\LanderCabin");
    cabin_ = AddMesh(cabinTpl_);
    SetMeshVisibilityMode(cabin_, MESHVIS_VC);
    SetCameraOffset(V(tantra::lander::cabin::kEye[0]));

    argon_ = CreatePropellantResource(sp::kArgon, core_.Prop().ArgonKg());
    // the core's units: the marches (thrust up to the afterburner's ×2) and the lift cups (×1,3 the emergency mode)
    for (int i = 0; i < 2; ++i) {
        th_[i] = CreateThruster(V(mm::kMarchExit[i]), _V(0, 0, 1), 2.0 * sp::kMarchF, argon_, sp::kMarchV);
        AddExhaust(th_[i], 26.0, 2.6, V(mm::kMarchExit[i]), _V(0, 0, -1));
    }
    for (int k = 0; k < 6; ++k) {
        th_[2 + k] = CreateThruster(V(mm::kCup[k]), _V(0, 1, 0), 1.35 * sp::kLiftF, argon_, sp::kLiftV);
        AddExhaust(th_[2 + k], 9.0, 1.05, V(mm::kCup[k]), _V(0, -1, 0));
    }
    // the levers: Orbiter's main / hover controls read through two idle thrusters (the core decides the thrust)
    dummyMain_ = CreateThruster(_V(0, 0, 0), _V(0, 0, 1), 1e-3, argon_, 1e12);
    dummyHover_ = CreateThruster(_V(0, 0, 0), _V(0, 1, 0), 1e-3, argon_, 1e12);
    CreateThrusterGroup(&dummyMain_, 1, THGROUP_MAIN);
    CreateThrusterGroup(&dummyHover_, 1, THGROUP_HOVER);
    // the attitude thrusters (gen_lander rcs_pad): force against the exhaust
    for (int i = 0; i < mm::kRcsCount; ++i) {
        const VECTOR3 e = V(mm::kRcs[i].exhaust);
        rcs_[i] = CreateThruster(V(mm::kRcs[i].pos), -e, sp::kRcsF, argon_, sp::kRcsV);
        AddExhaust(rcs_[i], 2.2, 0.22, V(mm::kRcs[i].pos), e);
    }
    auto id = [](const char* s) { for (int i = 0; i < mm::kRcsCount; ++i) if (!std::strcmp(mm::kRcs[i].id, s)) return i; return 0; };
    auto grp = [&](THGROUP_TYPE t, std::initializer_list<const char*> names) {
        THRUSTER_HANDLE h[4]; int n = 0;
        for (const char* s : names) h[n++] = rcs_[id(s)];
        CreateThrusterGroup(h, n, t);
    };
    grp(THGROUP_ATT_PITCHUP, {"N_niz_L", "N_niz_R", "K_verh_L", "K_verh_R"});
    grp(THGROUP_ATT_PITCHDOWN, {"N_verh_L", "N_verh_R", "K_niz_L", "K_niz_R"});
    grp(THGROUP_ATT_YAWLEFT, {"N_bort_R", "K_bort_L"});
    grp(THGROUP_ATT_YAWRIGHT, {"N_bort_L", "K_bort_R"});
    grp(THGROUP_ATT_BANKLEFT, {"K_verh_L", "K_niz_R"});
    grp(THGROUP_ATT_BANKRIGHT, {"K_verh_R", "K_niz_L"});
    grp(THGROUP_ATT_UP, {"N_niz_L", "N_niz_R", "K_niz_L", "K_niz_R"});
    grp(THGROUP_ATT_DOWN, {"N_verh_L", "N_verh_R", "K_verh_L", "K_verh_R"});
    grp(THGROUP_ATT_LEFT, {"N_bort_R", "K_bort_R"});
    grp(THGROUP_ATT_RIGHT, {"N_bort_L", "K_bort_L"});
    grp(THGROUP_ATT_FORWARD, {"A_korma_L", "A_korma_R"});
    grp(THGROUP_ATT_BACK, {"A_nos_L", "A_nos_R"});
    rcsArm_ = std::fabs(mm::kRcs[id("N_bort_L")].pos[2] - mm::kRcs[id("K_bort_L")].pos[2]) * 0.5;

    // aerodynamics: the refs in the mesh frame here, moved with the CG by UpdateCG
    foil_[0] = CreateAirfoil3(LIFT_VERTICAL, _V(0, 0, mm::kXref), VLift, &aero_, mm::kLref, mm::kSref, 1.84);
    foil_[1] = CreateAirfoil3(LIFT_HORIZONTAL, _V(0, 0.3, 1.6), HBody, &aero_, mm::kLref, 80.0, 1.0);
    foil_[2] = CreateAirfoil3(LIFT_HORIZONTAL, V(mm::kFinAc), HFin, &aero_, mm::kFinMac, mm::kFinArea, 2.26);
    // the elevons: elevator (both) and ailerons; the body flap as the elevator trim (the core sets it by mode)
    CreateControlSurface3(AIRCTRL_ELEVATOR, 2.0 * mm::kElevonArea, 1.2, _V(0, mm::kElevonPos[1], mm::kElevonPos[2]), AIRCTRL_AXIS_XPOS, 0.6);
    CreateControlSurface3(AIRCTRL_AILERON, mm::kElevonArea, 1.2, _V(-mm::kElevonPos[0], mm::kElevonPos[1], mm::kElevonPos[2]), AIRCTRL_AXIS_XPOS, 0.6);
    CreateControlSurface3(AIRCTRL_AILERON, mm::kElevonArea, 1.2, _V(mm::kElevonPos[0], mm::kElevonPos[1], mm::kElevonPos[2]), AIRCTRL_AXIS_XNEG, 0.6);
    DefineAnimations();
    cockpit_.Init(this, cabin_, cabinTpl_, this);
    SetTouch();
    // the cradle of «Тантра» («TLANDER», TantraPort::DefineHangar: up, forward); ShiftCG carries it with the frame
    att_ = CreateAttachment(true, _V(0, mm::kBellyMinY, 0), _V(0, -1, 0), _V(0, 0, 1), "TLANDER");
}

const char* Gran::Separate() {
    if (!Attached()) return "не на ложементе";
    OBJHANDLE ph = GetAttachmentStatus(att_);
    VESSEL* pv = oapiGetVesselInterface(ph);
    for (DWORD i = 0; i < pv->AttachmentCount(false); ++i) {
        ATTACHMENTHANDLE a = pv->GetAttachmentHandle(false, i);
        if (pv->GetAttachmentStatus(a) == GetHandle()) return pv->DetachChild(a, sepV_) ? nullptr : "ложемент не отпустил";
    }
    return "точка ложемента не найдена";
}

void Gran::DefineAnimations() {
    for (int a = 0; a < mm::kAnimCount; ++a) {
        const mm::AnimDef& d = mm::kAnim[a];
        const double s0 = (0.0 - d.lo) / (d.hi - d.lo);
        anim_[a] = CreateAnimation(s0);
        // the gear: the strut alone turns here; the rod and the wheels ride on it (their own travel below)
        const bool gear = a == mm::A_gearN || a == mm::A_gearL || a == mm::A_gearR;
        static UINT grpBuf[mm::kAnimCount][4];
        const int n = gear ? 1 : d.n;
        for (int i = 0; i < n; ++i) grpBuf[a][i] = static_cast<UINT>(d.grp[i]);
        auto* t = new MGROUP_ROTATE(mesh_, grpBuf[a], n, V(d.ref), V(d.axis), static_cast<float>(-(d.hi - d.lo) * kRad));
        ANIMATIONCOMPONENT_HANDLE h = AddAnimationComponent(anim_[a], 0.0, 1.0, t);
        if (gear) gearComp_[a - mm::A_gearN] = h;
        pos_[a] = 0.0;
    }
    // the struts' travel: the rod and the wheels up along the strut, a child of the retraction
    for (int g = 0; g < 3; ++g) {
        static UINT grp[3][2];
        grp[g][0] = static_cast<UINT>(mm::kGear[g].shtok); grp[g][1] = static_cast<UINT>(mm::kGear[g].kolesa);
        comp_[g] = CreateAnimation(0.0);
        auto* t = new MGROUP_TRANSLATE(mesh_, grp[g], 2, _V(0, mm::kGear[g].stroke, 0));
        AddAnimationComponent(comp_[g], 0.0, 1.0, t, gearComp_[g]);
    }
}

// ------------------------------------------------------------------------------------------------------------------------------
void Gran::clbkLoadStateEx(FILEHANDLE scn, void* vs) {
    char* line;
    double nose = -1, aft = -1, wing = -1, store = -1;
    while (oapiReadScenario_nextline(scn, line)) {
        if (!_strnicmp(line, "MODE", 4)) std::sscanf(line + 4, "%d", &mode_);
        else if (!_strnicmp(line, "HHOLD", 5)) std::sscanf(line + 5, "%lf", &hHold_);
        else if (!_strnicmp(line, "CREW", 4)) std::sscanf(line + 4, "%d", &crew_);
        else if (!_strnicmp(line, "ARGON", 5)) std::sscanf(line + 5, "%lf %lf %lf", &nose, &aft, &wing);
        else if (!_strnicmp(line, "STORE", 5)) std::sscanf(line + 5, "%lf", &store);
        else if (!_strnicmp(line, "GEARCMD", 7)) std::sscanf(line + 7, "%d", &gearCmd_);
        else if (!_strnicmp(line, "FIN", 3)) { int v = 0; std::sscanf(line + 3, "%d", &v); finCmd_ = v != 0; finPos_ = finCmd_ ? 1.0 : 0.0; }
        else if (!_strnicmp(line, "SEPV", 4)) std::sscanf(line + 4, "%lf", &sepV_);
        else if (!_strnicmp(line, "HATCH", 5)) { int v = 0; std::sscanf(line + 5, "%d", &v); hatchCmd_ = v != 0; hatchPos_ = hatchCmd_ ? 1.0 : 0.0; }
        else ParseScenarioLineEx(line, vs);
    }
    mode_ = std::clamp(mode_, 0, ln::kModeCount - 1);
    if (nose >= 0) core_.Prop().SetArgon(nose, aft, wing);
    if (store >= 0) core_.SetStore(store * sp::kStoreE);
    const ln::WingCfg w = ln::WingConfig(mode_, 0.0);
    const bool down = gearCmd_ >= 0 ? gearCmd_ == 1 : w.gearDown;
    gearPos_ = down ? 0.0 : 1.0; doorPos_ = down ? 1.0 : 0.0;
    pos_[mm::A_tipL] = pos_[mm::A_tipR] = w.tip;
}

void Gran::clbkSaveState(FILEHANDLE scn) {
    VESSEL4::clbkSaveState(scn);
    char buf[128];
    oapiWriteScenario_int(scn, const_cast<char*>("MODE"), mode_);
    std::snprintf(buf, sizeof buf, "%.1f", hHold_); oapiWriteScenario_string(scn, const_cast<char*>("HHOLD"), buf);
    oapiWriteScenario_int(scn, const_cast<char*>("CREW"), crew_);
    std::snprintf(buf, sizeof buf, "%.1f %.1f %.1f", core_.Prop().tank[0].kg, core_.Prop().tank[1].kg, core_.Prop().tank[2].kg);
    oapiWriteScenario_string(scn, const_cast<char*>("ARGON"), buf);
    std::snprintf(buf, sizeof buf, "%.4f", core_.S().storeFrac); oapiWriteScenario_string(scn, const_cast<char*>("STORE"), buf);
    oapiWriteScenario_int(scn, const_cast<char*>("GEARCMD"), gearCmd_);
    oapiWriteScenario_int(scn, const_cast<char*>("FIN"), finCmd_ ? 1 : 0);
    oapiWriteScenario_int(scn, const_cast<char*>("HATCH"), hatchCmd_ ? 1 : 0);
    std::snprintf(buf, sizeof buf, "%.1f", sepV_); oapiWriteScenario_string(scn, const_cast<char*>("SEPV"), buf);
}

void Gran::clbkPostCreation() {
    SetPropellantMass(argon_, core_.Prop().ArgonKg());
    SetEmptyMass(core_.Prop().MassKg() - core_.Prop().ArgonKg());
    UpdateCG(true);
    Animate(0.0);
    SetTouch();
}

// ------------------------------------------------------------------------------------------------------------------------------
// the CG follows the core's mass budget and tanks; ShiftCG moves the mesh, the thrusters and the exhaust, we move the
// airfoils and the touchdown points
void Gran::UpdateCG(bool force) {
    const double z = core_.Prop().XCg(), d = z - frameZ_;
    if (!force && std::fabs(d) < 0.03) return;
    if (std::fabs(d) < 1e-6) return;
    ShiftCG(_V(0, 0, d));
    frameZ_ = z;
    EditAirfoil(foil_[0], 0x01, _V(0, 0, mm::kXref - frameZ_), nullptr, 0, 0, 0);
    EditAirfoil(foil_[1], 0x01, _V(0, 0.3, 1.6 - frameZ_), nullptr, 0, 0, 0);
    EditAirfoil(foil_[2], 0x01, _V(mm::kFinAc[0], mm::kFinAc[1], mm::kFinAc[2] - frameZ_), nullptr, 0, 0, 0);
    touchState_ = -1;
}

// the gear on shock struts: the spring gives a third of the stroke at the static load, damping 0,5 of critical (Orbiter's
// contacts are explicit: stiffer or more damped pumps energy in at long steps); the hull points carry a belly landing
void Gran::SetTouch() {
    const bool gear = gearPos_ < 0.02;
    const double m = GetMass();
    const int state = gear ? 1 : 0;
    if (state == touchState_ && std::fabs(m - touchMass_) < 0.05 * m && std::fabs(frameZ_ - touchFrame_) < 1e-6) return;
    const double g = 9.81, share[3] = {0.10, 0.45, 0.45};
    TOUCHDOWNVTX v[3 + mm::kHullPtCount];
    int n = 0;
    const double kHull = m * g / (4.0 * 0.05), cHull = 2.0 * 0.6 * std::sqrt(kHull * m / 4.0);
    if (gear) {
        for (int i = 0; i < 3; ++i) {
            const double F = share[i] * m * g, k = F / (mm::kGear[i].stroke / 3.0), c = 2.0 * 0.5 * std::sqrt(k * share[i] * m);
            const double* p = mm::kGear[i].contact;
            v[n++] = {_V(p[0], p[1], p[2] - frameZ_), k, c, 3.0, 0.03};
        }
    } else {
        // gear up: the belly's three lowest points first (nose, aft left, aft right) - Orbiter's landed frame
        const double* p = mm::kHullPt[0];
        double zMax = -1e9, zMin = 1e9; int iN = 0, iA = 0;
        for (int i = 0; i < mm::kHullPtCount; ++i) { if (mm::kHullPt[i][2] > zMax && mm::kHullPt[i][1] < -0.5) { zMax = mm::kHullPt[i][2]; iN = i; } if (mm::kHullPt[i][2] < zMin) { zMin = mm::kHullPt[i][2]; iA = i; } }
        (void)p;
        v[n++] = {_V(0, mm::kHullPt[iN][1], mm::kHullPt[iN][2] - frameZ_), kHull, cHull, 0.6, 0.5};
        v[n++] = {_V(-2.5, mm::kHullPt[iA][1], mm::kHullPt[iA][2] - frameZ_), kHull, cHull, 0.6, 0.5};
        v[n++] = {_V(2.5, mm::kHullPt[iA][1], mm::kHullPt[iA][2] - frameZ_), kHull, cHull, 0.6, 0.5};
    }
    for (int i = 0; i < mm::kHullPtCount; ++i)
        v[n++] = {_V(mm::kHullPt[i][0], mm::kHullPt[i][1], mm::kHullPt[i][2] - frameZ_), kHull, cHull, 0.6, 0.5};
    SetTouchdownPoints(v, n);
    touchState_ = state; touchMass_ = m; touchFrame_ = frameZ_;
}

// ------------------------------------------------------------------------------------------------------------------------------
double Gran::ShipDist() const {
    double best = 1e9;
    VECTOR3 me; GetGlobalPos(me);
    for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
        OBJHANDLE h = oapiGetVesselByIndex(i);
        if (h == GetHandle()) continue;
        VESSEL* v = oapiGetVesselInterface(h);
        const char* cn = v ? v->GetClassNameA() : nullptr;
        if (!cn || _stricmp(cn, "Tantra")) continue;
        VECTOR3 p; oapiGetGlobalPos(h, &p);
        best = std::min(best, length(p - me));
    }
    return best;
}

// the sensors as the core reads them. The core's hover names: «roll» drives the rows' difference (the rows stand along the
// body: the nose's pitch), «pitch» the cups' sides (the lateral tilt); in the flight modes «pitch» is the nose's pitch.
void Gran::Sensors(ln::Flight& f, double dt) {
    OBJHANDLE ref = GetSurfaceRef();
    VECTOR3 gp, pp; GetGlobalPos(gp); oapiGetGlobalPos(ref, &pp);
    VECTOR3 up = gp - pp; const double r = length(up); up /= r;
    MATRIX3 R; GetRotationMatrix(R);
    const VECTOR3 ul = tmul(R, up);
    const double noseUp = std::asin(Clamp(ul.z, -1.0, 1.0)), rightUp = std::asin(Clamp(ul.x, -1.0, 1.0));
    double hdg = 0; oapiGetHeading(GetHandle(), &hdg);
    const double alpha = GetAOA();
    if (havePrev_ && dt > 1e-6) {
        double dh = hdg - prevHdg_; if (dh > PI) dh -= 2 * PI; if (dh < -PI) dh += 2 * PI;
        const double k = std::min(1.0, dt / 0.15);
        rate_[0] += ((noseUp - prevPitch_) / dt - rate_[0]) * k;
        rate_[1] += ((rightUp - prevTilt_) / dt - rate_[1]) * k;
        rate_[2] += (dh / dt - rate_[2]) * k;
        f.alphaRate = (alpha - prevAlpha_) / dt;
    }
    prevPitch_ = noseUp; prevTilt_ = rightUp; prevHdg_ = hdg; prevAlpha_ = alpha; havePrev_ = true;

    auto pick = [](double a, double b) { return std::fabs(a) > std::fabs(b) ? a : b; };
    const double pitchIn = pick(numPitch_, GetManualControlLevel(THGROUP_ATT_PITCHUP, MANCTRL_ROTMODE) - GetManualControlLevel(THGROUP_ATT_PITCHDOWN, MANCTRL_ROTMODE));
    const double bankIn = pick(numBank_, GetManualControlLevel(THGROUP_ATT_BANKRIGHT, MANCTRL_ROTMODE) - GetManualControlLevel(THGROUP_ATT_BANKLEFT, MANCTRL_ROTMODE));
    const double yawIn = pick(numYaw_, GetManualControlLevel(THGROUP_ATT_YAWRIGHT, MANCTRL_ROTMODE) - GetManualControlLevel(THGROUP_ATT_YAWLEFT, MANCTRL_ROTMODE));
    if (HoverMode()) {
        // the stick commands a tilt (±8°) the automat holds - the hover moves sideways that way; the yaw - the heading hold
        tiltCmd_[0] = Approach(tiltCmd_[0], 8.0 * kRad * pitchIn, 6.0 * kRad, dt);
        tiltCmd_[1] = Approach(tiltCmd_[1], -8.0 * kRad * bankIn, 6.0 * kRad, dt);
        yawHold_ += yawIn * 10.0 * kRad * dt;
        double e = hdg - yawHold_; if (e > PI) e -= 2 * PI; if (e < -PI) e += 2 * PI;
        f.roll = noseUp - tiltCmd_[0]; f.rollRate = rate_[0];
        f.pitch = rightUp - tiltCmd_[1]; f.pitchRate = rate_[1];
        f.yaw = e; f.yawRate = rate_[2]; yawErr_ = e;
    } else {
        yawHold_ = hdg; tiltCmd_[0] = tiltCmd_[1] = 0;
        // the УВТ dampers: the rate error against the stick's command (rate command, the automat damps the rest)
        f.pitch = 0.0; f.pitchRate = rate_[0] - pitchIn * 0.12;
        f.roll = 0.0; f.rollRate = rate_[1];
        f.yaw = 0.0; f.yawRate = rate_[2] - yawIn * 0.08;
    }
    f.h = GetAltitude(ALTMODE_GROUND) + mm::kGear[1].contact[1];   // above the gear's standing height
    VECTOR3 gs; GetGroundspeedVector(FRAME_HORIZON, gs); f.vz = gs.y;
    f.alpha = alpha;
    f.mass = GetMass();
    f.g = GGRAV * oapiGetMass(ref) / (r * r);
    f.wingLift = GetLift();
    VECTOR3 pmi; GetPMI(pmi);
    f.Ixx = pmi.x * f.mass; f.Izz = pmi.z * f.mass; f.Iyy = pmi.y * f.mass;   // core «roll» = the nose's pitch (PMI x)
    f.atmosphere = oapiPlanetHasAtmosphere(ref);
    f.mach = GetMachNumber();
    f.shipDist = ShipDist();
    lastF_ = f;
}

// ------------------------------------------------------------------------------------------------------------------------------
void Gran::clbkPreStep(double, double simdt, double) {
    if (simdt <= 0) return;
    ln::Inputs in;
    in.mode = mode_; in.master = true; in.crew = crew_; in.preEntry = preEntry_;
    Sensors(in.f, simdt);
    in.p.march = GetThrusterGroupLevel(THGROUP_MAIN);
    in.p.collective = GetThrusterGroupLevel(THGROUP_HOVER);
    in.p.hHold = hHold_;
    in.p.afterburner = ab_;
    in.p.emergency = emerg_; in.p.hoverEmergency = emerg_;
    in.p.nose = nose_;
    // the argon the attitude thrusters took last step (Orbiter fires them) - from the aft tank, as the core feeds
    ln::Tank& aft = core_.Prop().tank[sp::kTAft];
    aft.kg = std::max(0.0, aft.kg - rcsFlowKg_);
    // the core in small steps (time warp)
    const int n = std::max(1, static_cast<int>(std::ceil(simdt / 0.02)));
    in.dt = simdt / n;
    for (int i = 0; i < n; ++i) core_.Step(in);
    Apply(simdt);
    Animate(simdt);
    UpdateCG(false);
    SetTouch();
    cockpit_.Step(simdt, frameZ_);
}

void Gran::Apply(double dt) {
    const ln::Snapshot& s = core_.S();
    const ln::Propulsion& P = core_.Prop();
    for (int i = 0; i < 8; ++i) {
        const ln::Unit& u = P.u[i];
        const double mx = i < 2 ? 2.0 * sp::kMarchF : 1.35 * sp::kLiftF;
        SetThrusterIsp(th_[i], std::max(1000.0, u.v));
        SetThrusterLevel(th_[i], Clamp(u.thrust / mx, 0.0, 1.0));
    }
    for (int i = 0; i < 2; ++i) {   // the УВТ: tvcP nose-down positive at the stern, tvcY nose-left
        const double y = -std::sin(P.u[i].tvcY * kRad), p = -std::sin(P.u[i].tvcP * kRad);
        SetThrusterDir(th_[i], unit(_V(y, p, std::sqrt(std::max(0.0, 1.0 - y * y - p * p)))));
    }
    // the yaw hold in the hover: the core's torque by the yaw thrusters
    if (HoverMode()) {
        // the core's hover yaw law (LanderAvionics::Hover: -Iyy·gain·(2ψ + 3ψ')), here by the yaw thrusters
        const double gain = s.active == ln::kPhoton ? 1.0 : 0.7;
        VECTOR3 pmi; GetPMI(pmi);
        const double rcs = -pmi.y * GetMass() * gain * (2.0 * yawErr_ + 3.0 * rate_[2]);
        const double maxT = 2.0 * sp::kRcsF * rcsArm_;
        SetThrusterGroupLevel(THGROUP_ATT_YAWRIGHT, Clamp(rcs / maxT, 0.0, 1.0));
        SetThrusterGroupLevel(THGROUP_ATT_YAWLEFT, Clamp(-rcs / maxT, 0.0, 1.0));
    }
    if (wasHover_ && !HoverMode()) {
        SetThrusterGroupLevel(THGROUP_ATT_YAWRIGHT, 0.0); SetThrusterGroupLevel(THGROUP_ATT_YAWLEFT, 0.0);
        SetAttitudeMode(RCS_ROT);
    }
    if (!wasHover_ && HoverMode()) SetAttitudeMode(RCS_NONE);
    wasHover_ = HoverMode();
    // the argon: the core's tanks are the truth (Orbiter took the same F/v from its resource)
    SetPropellantMass(argon_, core_.Prop().ArgonKg());
    SetEmptyMass(core_.Prop().MassKg() - core_.Prop().ArgonKg());
    double rf = 0;
    for (THRUSTER_HANDLE h : rcs_) rf += GetThrusterLevel(h) * sp::kRcsF / sp::kRcsV;
    rcsFlowKg_ = rf * dt;
    // the elevons: the entry and ballistic automats fly them (the mesh's sign: negative - nose up); else the pilot (AF)
    const bool auto_ = (mode_ == ln::kEntry || mode_ == ln::kBallistic) && s.mode.state == ln::kRun;
    if (auto_) SetControlSurfaceLevel(AIRCTRL_ELEVATOR, Clamp(-s.elevon / 25.0, -1.0, 1.0));
    aero_.flap = s.flap;                 // the body flap: its moment by the Newton tables (gen_lander TRIM_FLAP)
}

// ------------------------------------------------------------------------------------------------------------------------------
// the mesh by the core: the tips, the flap, the elevons, the rows and their doors, the УВТ, the gear with its doors and the
// struts' travel, the fin, the hatch
void Gran::Animate(double dt) {
    const ln::Snapshot& s = core_.S();
    const ln::Propulsion& P = core_.Prop();
    const bool now = dt <= 0.0;
    auto go = [&](int a, double deg, double rate) {
        pos_[a] = now ? deg : Approach(pos_[a], deg, rate, dt);
        const mm::AnimDef& d = mm::kAnim[a];
        SetAnimation(anim_[a], Clamp((pos_[a] - d.lo) / (d.hi - d.lo), 0.0, 1.0));
    };
    go(mm::A_tipL, s.tip, 9.0); go(mm::A_tipR, s.tip, 9.0);
    aero_.tip = pos_[mm::A_tipL];
    go(mm::A_flap, s.flap, 10.0);
    // the elevons: the automats' angle, else the pilot's elevator ± aileron (25° full); negative - the trailing edge up
    const bool auto_ = (mode_ == ln::kEntry || mode_ == ln::kBallistic) && s.mode.state == ln::kRun;
    const double el = auto_ ? s.elevon : -25.0 * GetControlSurfaceLevel(AIRCTRL_ELEVATOR);
    const double ai = 25.0 * GetControlSurfaceLevel(AIRCTRL_AILERON);
    go(mm::A_elevL, Clamp(el + ai, -25.0, 25.0), 40.0); go(mm::A_elevR, Clamp(el - ai, -25.0, 25.0), 40.0);
    // the rows: the core's row 0 (u.x < 0) is the rear one (mesh «Z»), row 1 the front («P»); 0° out, 180° stowed
    go(mm::A_rowR, 180.0 * (1.0 - P.row[0].frame), 1e3); go(mm::A_rowF, 180.0 * (1.0 - P.row[1].frame), 1e3);
    go(mm::A_panelR, -95.0 * std::min(1.0, P.row[0].door + (P.row[0].frame > 0 ? 1.0 : 0.0)), 1e3);
    go(mm::A_panelF, -95.0 * std::min(1.0, P.row[1].door + (P.row[1].frame > 0 ? 1.0 : 0.0)), 1e3);
    go(mm::A_marchL, Clamp(P.u[0].tvcP, -15.0, 15.0), 60.0); go(mm::A_marchR, Clamp(P.u[1].tvcP, -15.0, 15.0), 60.0);
    // the gear: by mode or the pilot's G; never up with the weight on the wheels; the doors open first
    bool down = gearCmd_ >= 0 ? gearCmd_ == 1 : s.gearDown;
    if (!down && GroundContact() && gearPos_ < 0.02) down = true;
    if (now) { gearPos_ = down ? 0.0 : 1.0; doorPos_ = down ? 1.0 : 0.0; }
    else {
        const bool moving = down ? gearPos_ > 0.0 : gearPos_ < 1.0;
        if (moving) { doorPos_ = Approach(doorPos_, 1.0, 0.5, dt); if (doorPos_ >= 0.999) gearPos_ = Approach(gearPos_, down ? 0.0 : 1.0, 1.0 / 8.0, dt); }
        else doorPos_ = Approach(doorPos_, down ? 1.0 : 0.0, 0.5, dt);
    }
    for (int a : {mm::A_gearN, mm::A_gearL, mm::A_gearR}) { pos_[a] = 90.0 * gearPos_; SetAnimation(anim_[a], gearPos_); }
    // the doors in two halves (hinges at both edges): each to its open angle (the limit that is not 0) by doorPos_
    for (int a : {mm::A_doorN, mm::A_doorL, mm::A_doorR, mm::A_doorN2, mm::A_doorL2, mm::A_doorR2}) {
        const mm::AnimDef& d = mm::kAnim[a];
        pos_[a] = (std::fabs(d.hi) > std::fabs(d.lo) ? d.hi : d.lo) * doorPos_;
        SetAnimation(anim_[a], Clamp((pos_[a] - d.lo) / (d.hi - d.lo), 0.0, 1.0));
    }
    // the struts' travel: how deep the wheel's point sits under the ground (Orbiter's spring) = the shock's compression
    OBJHANDLE ref = GetSurfaceRef();
    for (int g = 0; g < 3; ++g) {
        double c = 0.0;
        if (gearPos_ < 0.02) {
            const double* p = mm::kGear[g].contact;
            VECTOR3 gl; Local2Global(_V(p[0], p[1], p[2] - frameZ_), gl);
            double lng, lat, rad; oapiGlobalToEqu(ref, gl, &lng, &lat, &rad);
            const double alt = rad - oapiGetSize(ref) - oapiSurfaceElevation(ref, lng, lat);
            c = Clamp(-alt, 0.0, mm::kGear[g].stroke);
        }
        compr_[g] = now ? c : compr_[g] + (c - compr_[g]) * std::min(1.0, dt / 0.05);
        SetAnimation(comp_[g], compr_[g] / mm::kGear[g].stroke);
    }
    // the fin: folded in the hangar mode or by K; the hatch by O
    const bool fold = finCmd_ || mode_ == ln::kDock;
    finPos_ = now ? (fold ? 1.0 : 0.0) : Approach(finPos_, fold ? 1.0 : 0.0, 1.0 / mm::kFinFoldTime, dt);
    pos_[mm::A_fin] = 90.0 * finPos_; SetAnimation(anim_[mm::A_fin], finPos_);
    aero_.fin = std::cos(finPos_ * PI / 2) * std::cos(finPos_ * PI / 2);
    hatchPos_ = now ? (hatchCmd_ ? 1.0 : 0.0) : Approach(hatchPos_, hatchCmd_ ? 1.0 : 0.0, 0.25, dt);
    pos_[mm::A_hatch] = 100.0 * hatchPos_; SetAnimation(anim_[mm::A_hatch], hatchPos_);
}

// ------------------------------------------------------------------------------------------------------------------------------
int Gran::clbkConsumeBufferedKey(DWORD key, bool down, char* kstate) {
    if (!down || KEYMOD_CONTROL(kstate) || KEYMOD_ALT(kstate)) return 0;
    const bool shift = KEYMOD_SHIFT(kstate);
    static const DWORD num[8] = {OAPI_KEY_1, OAPI_KEY_2, OAPI_KEY_3, OAPI_KEY_4, OAPI_KEY_5, OAPI_KEY_6, OAPI_KEY_7, OAPI_KEY_8};
    for (int i = 0; i < 8; ++i)
        if (key == num[i] && !shift) {
            mode_ = i; gearCmd_ = -1;
            if (HoverMode()) { SetAttitudeMode(RCS_NONE); double hd = 0; oapiGetHeading(GetHandle(), &hd); yawHold_ = hd; }
            return 1;
        }
    switch (key) {
    case OAPI_KEY_G: { const bool nowDown = gearPos_ < 0.5; gearCmd_ = nowDown ? 0 : 1; return 1; }
    case OAPI_KEY_B: ab_ = !ab_; return 1;
    case OAPI_KEY_K: finCmd_ = !finCmd_; return 1;
    case OAPI_KEY_O: hatchCmd_ = !hatchCmd_; return 1;
    case OAPI_KEY_E: emerg_ = !emerg_; return 1;
    case OAPI_KEY_P: preEntry_ = !preEntry_; return 1;
    case OAPI_KEY_N: nose_ = !nose_; return 1;
    case OAPI_KEY_EQUALS: hHold_ += shift ? 50.0 : 5.0; return 1;
    case OAPI_KEY_MINUS: hHold_ = std::max(0.0, hHold_ - (shift ? 50.0 : 5.0)); return 1;
    }
    return 0;
}

int Gran::clbkConsumeDirectKey(char* kstate) {
    // the numpad as the hover's stick (Orbiter's own: 2/8 pitch, 4/6 bank, 1/3 yaw); read, not consumed
    numPitch_ = (KEYDOWN(kstate, OAPI_KEY_NUMPAD2) ? 1.0 : 0.0) - (KEYDOWN(kstate, OAPI_KEY_NUMPAD8) ? 1.0 : 0.0);
    numBank_ = (KEYDOWN(kstate, OAPI_KEY_NUMPAD6) ? 1.0 : 0.0) - (KEYDOWN(kstate, OAPI_KEY_NUMPAD4) ? 1.0 : 0.0);
    numYaw_ = (KEYDOWN(kstate, OAPI_KEY_NUMPAD3) ? 1.0 : 0.0) - (KEYDOWN(kstate, OAPI_KEY_NUMPAD1) ? 1.0 : 0.0);
    return 0;
}

// ------------------------------------------------------------------------------------------------------------------------------
// the ships' MFDs: a key lights while its system runs; its page - the state in lines and up to two touch buttons
bool Gran::KeyLit(int pilot, int key) const {
    const ln::Snapshot& s = core_.S();
    if (pilot == 0) switch (key) {
        case 0: return s.unit[0].state == ln::kRun;
        case 1: return s.unit[1].state == ln::kRun;
        case 2: return s.row[1].state == ln::kRun;
        case 3: return s.row[0].state == ln::kRun;
        case 4: return s.rcs.state == ln::kRun || s.rcs.state == ln::kReady;
        case 5: return ab_;
        case 6: return gearPos_ < 0.02;
        case 7: return finPos_ < 0.02;
        case 8: return hatchPos_ > 0.02;
        case 9: return s.flap > 0.5;
        case 10: return emerg_;
        default: return s.mode.state == ln::kRun;
    }
    switch (key) {
        case 0: return s.store.state == ln::kRun;
        case 1: return s.bus.state == ln::kRun;
        case 2: return s.cooler[0].state == ln::kRun;
        case 3: return s.cooler[1].state == ln::kRun;
        case 4: return s.active == ln::kPhoton;
        case 5: return s.active == ln::kAnalog;
        case 6: case 7: return s.transfer.state == ln::kRun;
        case 8: return s.cross.state == ln::kRun;
        case 9: return nose_;
        case 10: return false;
        default: return true;
    }
}

int Gran::PageLines(int pilot, int key, char out[][96], int max) const {
    const ln::Snapshot& s = core_.S();
    int n = 0;
    auto L = [&](const char* fmt, ...) {
        if (n >= max) return;
        va_list ap; va_start(ap, fmt); std::vsnprintf(out[n++], 96, fmt, ap); va_end(ap);
    };
    auto tanks = [&]() { L("баки: нос %.0f, корма %.0f, крыло %.0f кг", s.tankKg[0], s.tankKg[1], s.tankKg[2]); };
    if (pilot == 0) switch (key) {
        case 0: case 1:
            L("состояние: %s", ln::StateRu(s.unit[key].state)); L("%s", s.unit[key].why);
            L("тяга %.0f кН (рукоять МАРШ)", s.thrust[key] / 1e3);
            L("поле %.1f Тл, обмотка %.1f К", s.B[key], s.coilT[key]); L("запас по току %.0f %%", s.margin[key] * 100.0);
            L("УВТ: тангаж %.1f°, рыскание %.1f°", s.tvcP[key], s.tvcY[key]); break;
        case 2: case 3: {
            const int r = key == 2 ? 1 : 0;
            L("ряд %s: %s", key == 2 ? "передний" : "задний", ln::StateRu(s.row[r].state)); L("%s", s.row[r].why);
            L("чаши: тяга %.0f кН, Т/В %.2f", s.thrustLift / 1e3, s.twNominal);
            L("высота висения %.0f м (рукоять ЧАШИ)", hHold_); break;
        }
        case 4:
            L("РСУ: %s", ln::StateRu(s.rcs.state)); L("%s", s.rcs.why);
            L("аргон %.0f кг, расход %.1f кг/с", s.argonKg, s.argonFlow); tanks(); break;
        case 5:
            L("форсаж: %s", ab_ ? "запрошен" : "выключен"); L("%s", s.unit[0].why);
            L("обмотки: М1 %.1f К, М2 %.1f К", s.coilT[0], s.coilT[1]);
            L("запас по току: М1 %.0f %%, М2 %.0f %%", s.margin[0] * 100.0, s.margin[1] * 100.0);
            if (lastF_.shipDist < 1e8) L("до «Тантры» %.0f м (ближе %.0f м форсаж запрещён)", lastF_.shipDist, sp::kAfterSafeDist);
            break;
        case 6:
            L("шасси: %s", gearPos_ < 0.02 ? "выпущено" : gearPos_ > 0.98 ? "убрано" : "в движении");
            L("обжатие: ПО %.2f, Л %.2f, П %.2f м", compr_[0], compr_[1], compr_[2]);
            L("команда: %s", gearCmd_ < 0 ? "по режиму" : gearCmd_ ? "выпустить" : "убрать"); break;
        case 7: L("киль: %s", finPos_ < 0.02 ? "поднят" : finPos_ > 0.98 ? "сложен" : "в движении"); break;
        case 8: L("люк: %s", hatchPos_ > 0.98 ? "открыт" : hatchPos_ < 0.02 ? "закрыт" : "в движении"); break;
        case 9: L("щиток %.1f° (по режиму)", s.flap); L("элевоны %.1f°", s.elevon); L("концы крыла %.0f°", s.tip); break;
        case 10: L("аварийный режим: %s", emerg_ ? "включён" : "выключен"); break;
        default:
            L("автомат: %s", ln::StateRu(s.mode.state)); L("%s", s.decision);
            L("высота висения %.0f м", hHold_); L("высота %.0f м, вертикальная %.1f м/с", lastF_.h, lastF_.vz); break;
    }
    else switch (key) {
        case 0:
            L("накопитель %.1f %%, отдача %.0f МВт", s.storeFrac * 100.0, s.storeOut / 1e6);
            L("состояние: %s", ln::StateRu(s.store.state)); L("%s", s.store.why);
            if (s.shedWhy && *s.shedWhy) L("отключено: %s", s.shedWhy);
            break;
        case 1:
            L("шина: %s", ln::StateRu(s.bus.state)); L("%s", s.bus.why);
            L("ЖО %.0f, триггеры чаш %.0f, маршей %.0f кВт", s.loadLife / 1e3, s.loadTrigLift / 1e3, s.loadTrigMarch / 1e3);
            L("крио %.0f, фотоника %.0f, приводы %.0f кВт", s.loadCryo / 1e3, s.loadPhoton / 1e3, s.loadDrives / 1e3); break;
        case 2: case 3:
            L("криокулер %s: %s", key == 2 ? "А" : "Б", ln::StateRu(s.cooler[key - 2].state)); L("%s", s.cooler[key - 2].why);
            L("крио: %s", ln::StateRu(s.cryo.state)); L("обмотки: М1 %.1f К, М2 %.1f К", s.coilT[0], s.coilT[1]); break;
        case 4: case 5:
            L("в работе: %s", s.active == ln::kPhoton ? "фотонный контур" : s.active == ln::kAnalog ? "аналоговый контур" : "механика");
            L("фотонный: %s", ln::StateRu(s.photon.state)); L("%s", s.photon.why);
            L("аналоговый: %s, батарея %.0f МДж", ln::StateRu(s.analog.state), s.analogBatt / 1e6); break;
        case 6: case 7: case 8:
            L("перекачка: %s", ln::StateRu(s.transfer.state)); L("%s", s.transfer.why);
            L("поток %.1f кг/с, ЦМ %.2f м", s.transferFlow, s.xcg); tanks();
            L("перекрёстная: %s", ln::StateRu(s.cross.state));
            L("перед входом: %s", preEntry_ ? "нос заполняется" : "выключено"); break;
        case 9:
            L("вдув аргона в нос: %s", nose_ ? "включён" : "выключен");
            L("клапан %s, расход %.1f кг/с", s.noseValve ? "открыт" : "закрыт", s.noseFlow); break;
        case 10: L("заряды: %.0f кг, %s", s.chargesKg, ln::StateRu(s.charges.state)); L("резерв носа 0,5 т двигателям не отпущен"); break;
        default:
            L("высота над грунтом %.0f м", lastF_.h); L("вертикальная %.1f м/с", lastF_.vz);
            if (lastF_.shipDist < 1e8) L("до «Тантры» %.0f м", lastF_.shipDist); else L("«Тантра» не видна");
            break;
    }
    return n;
}

int Gran::PageButtons(int pilot, int key, const char* lbl[2]) const {
    if (pilot == 0) switch (key) {
        case 5: lbl[0] = ab_ ? "ФОРСАЖ ВЫКЛ" : "ФОРСАЖ ВКЛ"; return 1;
        case 6: lbl[0] = "ВЫПУСТИТЬ"; lbl[1] = "УБРАТЬ"; return 2;
        case 7: lbl[0] = "ПОДНЯТЬ"; lbl[1] = "СЛОЖИТЬ"; return 2;
        case 8: lbl[0] = "ОТКРЫТЬ"; lbl[1] = "ЗАКРЫТЬ"; return 2;
        case 10: lbl[0] = emerg_ ? "АВАР. ВЫКЛ" : "АВАР. ВКЛ"; return 1;
        case 11: lbl[0] = "ВЫСОТА +5 м"; lbl[1] = "ВЫСОТА -5 м"; return 2;
        default: return 0;
    }
    switch (key) {
        case 6: case 7: lbl[0] = preEntry_ ? "НОС: ВЫКЛ" : "НОС ПЕРЕД ВХОДОМ"; return 1;
        case 9: lbl[0] = nose_ ? "ВДУВ ВЫКЛ" : "ВДУВ ВКЛ"; return 1;
        default: return 0;
    }
}

void Gran::PagePress(int pilot, int key, int b) {
    if (pilot == 0) switch (key) {
        case 5: ab_ = !ab_; break;
        case 6: gearCmd_ = b == 0 ? 1 : 0; break;
        case 7: finCmd_ = b == 1; break;
        case 8: hatchCmd_ = b == 0; break;
        case 10: emerg_ = !emerg_; break;
        case 11: hHold_ = b == 0 ? hHold_ + 5.0 : std::max(0.0, hHold_ - 5.0); break;
        default: break;
    }
    else switch (key) {
        case 6: case 7: preEntry_ = !preEntry_; break;
        case 9: nose_ = !nose_; break;
        default: break;
    }
}

void Gran::ModeStep(int dir) {
    mode_ = (mode_ + dir + ln::kModeCount) % ln::kModeCount;
    gearCmd_ = -1;
    if (HoverMode()) { SetAttitudeMode(RCS_NONE); double hd = 0; oapiGetHeading(GetHandle(), &hd); yawHold_ = hd; }
}

// ------------------------------------------------------------------------------------------------------------------------------
bool Gran::clbkDrawHUD(int mode, const HUDPAINTSPEC* hps, oapi::Sketchpad* skp) {
    VESSEL4::clbkDrawHUD(mode, hps, skp);
    // D3D9Client builds HUD fonts with the Western charset; "Arial Cyr" maps the request onto Arial's Cyrillic set (as Tantra)
    const int h = hps->H / 48 + 4;
    if (!font_ || fontH_ != h) { if (font_) oapiReleaseFont(font_); font_ = oapiCreateFont(h, true, const_cast<char*>("Arial Cyr")); fontH_ = h; }
    oapi::Font* old = skp->SetFont(font_);
    const ln::Snapshot& s = core_.S();
    const ln::Propulsion& P = core_.Prop();
    char buf[320];
    int y = hps->H / 60 * 16;
    auto line = [&](const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
        skp->Text(10, y, buf, static_cast<int>(std::strlen(buf))); y += h + 2;
    };
    line("«Грань»  режим %s [%s]  %s", ln::ModeRu(mode_), kModeKey[mode_], ln::StateRu(s.mode.state));
    line("автомат: %s", s.decision);
    line("контур: %s  накопитель %.1f %%  %.0f МВт", s.active == ln::kPhoton ? "фотонный" : s.active == ln::kAnalog ? "аналоговый" : "механика",
         s.storeFrac * 100.0, s.storeOut / 1e6);
    line("масса %.1f т  ЦМ %.2f м  аргон %.0f кг (нос %.0f, корма %.0f, крыло %.0f)  %.0f кг/с", s.mass / 1e3, s.xcg, s.argonKg,
         s.tankKg[0], s.tankKg[1], s.tankKg[2], s.argonFlow);
    line("маршевые %.0f кН  %s / %s%s", s.thrustMarch / 1e3, ln::StateRu(s.unit[0].state), ln::StateRu(s.unit[1].state),
         ab_ ? (P.u[0].emergency ? "  ФОРСАЖ" : "  форсаж запрошен") : "");
    if (ab_ || P.u[0].emBlocked) line("   %s; обмотка %.1f К, запас по току %.0f %%", s.unit[0].why, s.coilT[0], s.margin[0] * 100.0);
    line("чаши %.0f кН (Т/В %.2f)  ряды: %s / %s  высота висения %.0f м%s", s.thrustLift / 1e3, s.twNominal, s.row[1].why, s.row[0].why, hHold_,
         emerg_ ? "  АВАРИЙНЫЙ" : "");
    line("шасси %s  обжатие ПО %.2f  Л %.2f  П %.2f м  киль %s  люк %s", gearPos_ < 0.02 ? "выпущено" : gearPos_ > 0.98 ? "убрано" : "...",
         compr_[0], compr_[1], compr_[2], finPos_ < 0.02 ? "поднят" : finPos_ > 0.98 ? "сложен" : "...",
         hatchPos_ > 0.98 ? "открыт" : hatchPos_ < 0.02 ? "закрыт" : "...");
    line("1 висение 2 переход 3 полёт 4 вход 5 баллистика 6 полоса 7 верт.посадка 8 ангар | G шасси B форсаж K киль O люк =/- высота E авар. P перекачка N нос");
    if (old) skp->SetFont(old);
    return true;
}

// ==============================================================================================================================
// the launch MFD: the state on the cradle, the separation speed (+ / -), the start (GO). In space the cradle lets go at the
// set speed; in an atmosphere the speed must carry the lander clear while its engines come up (the cups need ~9,7 s from
// the command to lift more than the weight - the core) - shown, the pilot decides
class LaunchMFD : public MFD2 {
public:
    LaunchMFD(DWORD w, DWORD h, VESSEL* v) : MFD2(w, h, v) {
        const char* cn = v->GetClassNameA();
        gran_ = cn && !_stricmp(cn, "TantraLander") ? static_cast<Gran*>(v) : nullptr;
        font_ = oapiCreateFont(h / 16, true, const_cast<char*>("Arial Cyr"));
    }
    ~LaunchMFD() override { oapiReleaseFont(font_); }
    char* ButtonLabel(int bt) override {
        static char* lbl[3] = {const_cast<char*>("+"), const_cast<char*>("-"), const_cast<char*>("GO")};
        return bt < 3 ? lbl[bt] : nullptr;
    }
    int ButtonMenu(const MFDBUTTONMENU** menu) const override {
        static const MFDBUTTONMENU m[3] = {{"Speed up", nullptr, '='}, {"Speed down", nullptr, '-'}, {"Start", "separation", 'G'}};
        if (menu) *menu = m;
        return 3;
    }
    bool ConsumeButton(int bt, int event) override {
        if (!(event & PANEL_MOUSE_LBDOWN) || !gran_) return false;
        if (bt == 0) gran_->StepSepSpeed(1);
        else if (bt == 1) gran_->StepSepSpeed(-1);
        else if (bt == 2) { const char* why = gran_->Separate(); last_ = why ? why : "отделение выполнено"; }
        else return false;
        InvalidateDisplay();
        return true;
    }
    bool ConsumeKeyBuffered(DWORD key) override {
        if (key == OAPI_KEY_EQUALS) return ConsumeButton(0, PANEL_MOUSE_LBDOWN);
        if (key == OAPI_KEY_MINUS) return ConsumeButton(1, PANEL_MOUSE_LBDOWN);
        if (key == OAPI_KEY_G) return ConsumeButton(2, PANEL_MOUSE_LBDOWN);
        return false;
    }
    bool Update(oapi::Sketchpad* skp) override {
        Title(skp, "GRAN START");
        skp->SetFont(font_);
        skp->SetTextColor(0x00FF00);
        char b[160];
        const int dy = H / 13;
        int y = H / 8;
        auto line = [&](const char* fmt, ...) {
            va_list ap; va_start(ap, fmt); std::vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
            skp->Text(W / 24, y, b, static_cast<int>(std::strlen(b))); y += dy;
        };
        if (!gran_) { line("не «Грань»"); return true; }
        const bool atm = oapiPlanetHasAtmosphere(pV->GetSurfaceRef()) && pV->GetAtmDensity() > 1e-4;
        line("«Грань»: старт с ложемента");
        line(gran_->Attached() ? "на ложементе: %s" : "свободна%s", gran_->ParentName());
        line("скорость отделения %.1f м/с", gran_->SepSpeed());
        if (atm) {
            double g = 9.81;
            OBJHANDLE ref = pV->GetSurfaceRef(); VECTOR3 gp, pp; pV->GetGlobalPos(gp); oapiGetGlobalPos(ref, &pp);
            const double r = length(gp - pp); g = GGRAV * oapiGetMass(ref) / (r * r);
            const double v = gran_->SepSpeed();
            line("атмосфера: магнитный выброс вверх");
            line("подъём %.0f м, в воздухе %.1f с", v * v / (2 * g), 2 * v / g);
            line("чаши до тяги > веса: ~9,7 с%s", 2 * v / g < 9.7 ? "  - МАЛО" : "");
        } else {
            line("космос: плавный отход (2-3 м/с)");
        }
        const ln::Snapshot& s = gran_->Snap();
        line("режим: %s", ln::ModeRu(gran_->Mode()));
        line("накопитель %.0f %%  аргон %.0f кг", s.storeFrac * 100.0, s.argonKg);
        if (last_[0]) line("%s", last_);
        return true;
    }
    static OAPI_MSGTYPE MsgProc(UINT msg, UINT, WPARAM wparam, LPARAM lparam) {
        if (msg == OAPI_MSG_MFD_OPENEDEX) {
            const MFDMODEOPENSPEC* o = reinterpret_cast<const MFDMODEOPENSPEC*>(wparam);
            return reinterpret_cast<OAPI_MSGTYPE>(new LaunchMFD(o->w, o->h, reinterpret_cast<VESSEL*>(lparam)));
        }
        return 0;
    }
private:
    Gran* gran_ = nullptr;
    oapi::Font* font_ = nullptr;
    const char* last_ = "";
};

static int gLaunchMfd = -1;
DLLCLBK void InitModule(HINSTANCE) {
    static char name[] = "GRAN START";
    MFDMODESPECEX spec;
    spec.name = name; spec.key = OAPI_KEY_J; spec.context = nullptr; spec.msgproc = LaunchMFD::MsgProc;
    gLaunchMfd = oapiRegisterMFDMode(spec);
}
DLLCLBK void ExitModule(HINSTANCE) { if (gLaunchMfd >= 0) oapiUnregisterMFDMode(gLaunchMfd); }

DLLCLBK VESSEL* ovcInit(OBJHANDLE h, int fmodel) { return new Gran(h, fmodel); }
DLLCLBK void ovcExit(VESSEL* v) { delete static_cast<Gran*>(v); }
