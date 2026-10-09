// «Грань» 25,4 м - the pilots' cabin in Orbiter. See LanderCockpit.h.
#define NOMINMAX
#include "LanderCockpit.h"
#include "LanderCabinMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cb = tantra::lander::cabin;

namespace tantra::lander {
namespace {

constexpr double kDeg = PI / 180.0;
constexpr int kMfdArea0 = 10, kMfdScrArea0 = 24, kShipArea0 = 30, kModeArea = 40;
constexpr double kDrumRange = 180.0, kRollRange = 30.0;                  // deg over the animation's 0..1
const char* const kKeys[2][12] = {
    {"М1", "М2", "РЯД П", "РЯД З", "РСУ", "ФОРСАЖ", "ШАССИ", "КИЛЬ", "ЛЮК", "ЩИТОК", "АВАР", "АП"},
    {"НАКОП", "ШИНА", "КРИО А", "КРИО Б", "ФОТОН", "АНАЛОГ", "ПЕРЕК А", "ПЕРЕК Б", "ПЕРЕКР", "ВДУВ", "РЕЗЕРВ", "РЛС"}};

inline VECTOR3 V(const double p[3]) { return _V(p[0], p[1], p[2]); }
inline double Clamp(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
inline double Approach(double v, double t, double rate, double dt) { return v + Clamp(t - v, -rate * dt, rate * dt); }
MATRIX3 RotX(double a) { const double c = std::cos(a), s = std::sin(a); return _M(1, 0, 0, 0, c, -s, 0, s, c); }   // (0,1,0) -> (0, c, s)
MATRIX3 RotZ(double a) { const double c = std::cos(a), s = std::sin(a); return _M(c, -s, 0, s, c, 0, 0, 0, 1); }   // (0,1,0) -> (-s, c, 0)

}  // namespace

const char* Cockpit::KeyName(int pilot, int key) { return kKeys[pilot & 1][key < 0 || key > 11 ? 0 : key]; }

Cockpit::~Cockpit() {
    for (oapi::Font* f : {fLbl_, fKey_, fPage_, fTitle_}) if (f) oapiReleaseFont(f);
    if (lblSurf_) oapiDestroySurface(lblSurf_);
    if (shipSurf_) oapiDestroySurface(shipSurf_);
}

// ------------------------------------------------------------------------------------------------------------------------------
// the drum turns about X through its axis; each module rolls about Z through its axis, a child of the drum; the tracked
// points are local vertex lists in the same components
void Cockpit::Init(VESSEL4* v, UINT mesh, CockpitHost* host, int launchMfdMode) {
    v_ = v; mesh_ = mesh; host_ = host;
    for (int p = 0; p < 2; ++p) {
        VECTOR3* q = pts_[p];
        q[kEye] = V(cb::kEye[p]); q[kLook] = q[kEye] + V(cb::kLook[p]); q[kUp] = q[kEye] + _V(0, 0.5, 0);
        for (int j = 0; j < 2; ++j) {
            const int mi = 2 * p + j;
            for (int c = 0; c < 4; ++c) {
                q[kMfd0 + j * 12 + c] = V(cb::kMfdLeft[mi][c]);
                q[kMfd0 + j * 12 + 4 + c] = V(cb::kMfdRight[mi][c]);
                q[kMfd0 + j * 12 + 8 + c] = V(cb::kMfdBottom[mi][c]);
                q[kScr0 + j * 4 + c] = V(cb::kMfdScreen[mi][c]);
            }
        }
        for (int c = 0; c < 4; ++c) {
            q[kShip + c] = V(cb::kShipScreen[p][c]); q[kShip + 4 + c] = V(cb::kShipLeft[p][c]); q[kShip + 8 + c] = V(cb::kShipRight[p][c]);
        }
        q[kRollUp] = V(cb::kRollRef[p]) + _V(0, 0.5, 0);
        q[kHip] = V(cb::kHip[p]); q[kHipF] = q[kHip] + _V(0, 0, 0.5);
        q[kG0] = V(cb::kGrip[p][0]); q[kG1] = V(cb::kGrip[p][1]);
        q[kHub] = V(cb::kHub[p]); q[kHubN] = V(cb::kHubN[p]);
        q[kMarch] = V(cb::kMarch[p]);
    }
    dpts_[0] = V(cb::kMode); dpts_[1] = V(cb::kDrumC); dpts_[2] = V(cb::kDrumC) + _V(0, 1, 0);

    static UINT drumGrp[sizeof(cb::kDrumGroups) / sizeof(cb::kDrumGroups[0])];
    const UINT nd = static_cast<UINT>(sizeof(drumGrp) / sizeof(drumGrp[0]));
    for (UINT i = 0; i < nd; ++i) drumGrp[i] = cb::kDrumGroups[i];
    drumAnim_ = v_->CreateAnimation(0.5);
    ANIMATIONCOMPONENT_HANDLE drum = v_->AddAnimationComponent(drumAnim_, 0.0, 1.0,
        new MGROUP_ROTATE(mesh_, drumGrp, nd, V(cb::kDrumC), _V(1, 0, 0), static_cast<float>(kDrumRange * kDeg)));
    v_->AddAnimationComponent(drumAnim_, 0.0, 1.0,
        new MGROUP_ROTATE(LOCALVERTEXLIST, MAKEGROUPARRAY(dpts_), 3, V(cb::kDrumC), _V(1, 0, 0), static_cast<float>(kDrumRange * kDeg)), drum);
    rollAnim_ = v_->CreateAnimation(0.5);
    static UINT modGrp[2][cb::kModuleCount[0] > cb::kModuleCount[1] ? cb::kModuleCount[0] : cb::kModuleCount[1]];
    for (int p = 0; p < 2; ++p) {
        for (int i = 0; i < cb::kModuleCount[p]; ++i) modGrp[p][i] = cb::kModuleGroups[p][i];
        ANIMATIONCOMPONENT_HANDLE m = v_->AddAnimationComponent(rollAnim_, 0.0, 1.0,
            new MGROUP_ROTATE(mesh_, modGrp[p], static_cast<UINT>(cb::kModuleCount[p]), V(cb::kRollRef[p]), _V(0, 0, 1), static_cast<float>(kRollRange * kDeg)), drum);
        v_->AddAnimationComponent(rollAnim_, 0.0, 1.0,
            new MGROUP_ROTATE(LOCALVERTEXLIST, MAKEGROUPARRAY(pts_[p]), kPts, V(cb::kRollRef[p]), _V(0, 0, 1), static_cast<float>(kRollRange * kDeg)), m);
    }
    v_->SetAnimation(drumAnim_, 0.5); v_->SetAnimation(rollAnim_, 0.5);
    // the Orbiter MFDs stuck to the ship (their surfaces go into the screens' texture slots)
    bank_.Init(v_, mesh_);
    bank_.Add(static_cast<DWORD>(cb::kMfdTex[0]), MFD_SURFACE);
    bank_.Add(static_cast<DWORD>(cb::kMfdTex[1]), MFD_MAP);
    bank_.Add(static_cast<DWORD>(cb::kMfdTex[2]), MFD_ORBIT);
    bank_.Add(static_cast<DWORD>(cb::kMfdTex[3]), launchMfdMode >= 0 ? launchMfdMode : MFD_HSI);
}

void Cockpit::OnVisual(VISHANDLE vis) {
    DEVMESHHANDLE dm = v_->GetDevMesh(vis, mesh_);
    const DWORD attr = OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_NOMIPMAPS;
    if (!lblSurf_) lblSurf_ = oapiCreateSurfaceEx(cb::kLblW, cb::kLblH, attr);
    if (!shipSurf_) shipSurf_ = oapiCreateSurfaceEx(cb::kShipW, cb::kShipH, attr);
    if (dm) {
        if (lblSurf_) oapiSetTexture(dm, cb::kLblTex, lblSurf_);
        if (shipSurf_) oapiSetTexture(dm, cb::kShipTex, shipSurf_);
    }
    bank_.OnVisualCreated(vis);
    drawT_ = 1.0;
}

void Cockpit::OnVisualGone() { bank_.OnVisualDestroyed(); }

VECTOR3 Cockpit::At(int module, int i) const {
    const VECTOR3 p = module < 2 ? pts_[module][i] : dpts_[i];
    return _V(p.x, p.y, p.z - frameZ_);                   // ShiftCG moved the meshes by -frameZ; the lists are mesh-local
}

// the motion of the drum (module < 0) or of a pilot's module, from the tracked points (whatever Orbiter's rotation sense)
void Cockpit::Motion(int module, MATRIX3& R, VECTOR3& t) const {
    const VECTOR3 du = dpts_[2] - dpts_[1];
    const MATRIX3 Rd = RotX(std::atan2(du.z, du.y));
    if (module < 0 || module > 1) { R = Rd; t = dpts_[1] - mul(Rd, V(cb::kDrumC)); return; }
    const VECTOR3 ur = tmul(Rd, Up(module));              // the module's up in the drum's own frame: (-sin c, cos c, 0)
    R = mul(Rd, RotZ(std::atan2(-ur.x, ur.y)));
    t = pts_[module][kHip] - mul(R, V(cb::kHip[module]));
}

bool Cockpit::InDrum(const VECTOR3& p) const {
    return std::fabs(p.x) <= 1.5 && std::hypot(p.y - cb::kDrumC[1], p.z - cb::kDrumC[2]) <= 1.10;
}

int Cockpit::ModuleAt(const VECTOR3& p) const {
    if (!InDrum(p)) return -1;
    if (std::fabs(p.x + 0.62) < 0.47) return 0;
    if (std::fabs(p.x - 0.62) < 0.47) return 1;
    return -1;
}

// ------------------------------------------------------------------------------------------------------------------------------
bool Cockpit::LoadVC(int id) {
    if (id < 0 || id > 1) return false;
    pos_ = id;
    for (int mi = 0; mi < 4; ++mi) {
        for (int part = 0; part < 3; ++part) oapiVCRegisterArea(kMfdArea0 + mi * 3 + part, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN | PANEL_MOUSE_ONREPLAY);
        oapiVCRegisterArea(kMfdScrArea0 + mi, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN | PANEL_MOUSE_ONREPLAY);
    }
    for (int p = 0; p < 2; ++p)
        for (int part = 0; part < 3; ++part) oapiVCRegisterArea(kShipArea0 + p * 3 + part, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN | PANEL_MOUSE_ONREPLAY);
    oapiVCRegisterArea(kModeArea, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN | PANEL_MOUSE_RBDOWN | PANEL_MOUSE_ONREPLAY);
    oapiVCSetNeighbours(id == 1 ? 0 : -1, id == 0 ? 1 : -1, -1, -1);   // Ctrl + ←/→ moves to the other couch
    v_->SetCameraRotationRange(0.9 * PI, 0.9 * PI, 0.45 * PI, 0.45 * PI);
    loaded_ = true;
    Quads(true);
    Camera(true);
    return true;
}

// the VC's click areas follow the turned mesh (the tracked corners)
void Cockpit::Quads(bool force) {
    if (!loaded_) return;
    if (!force && std::fabs(drum_ - quadDrum_) < 0.2 && std::fabs(roll_ - quadRoll_) < 0.2 && std::fabs(frameZ_ - quadZ_) < 1e-3) return;
    quadDrum_ = drum_; quadRoll_ = roll_; quadZ_ = frameZ_;
    for (int mi = 0; mi < 4; ++mi) {
        const int p = mi / 2, j = mi % 2;
        for (int part = 0; part < 3; ++part) {
            const int b = kMfd0 + j * 12 + part * 4;
            oapiVCSetAreaClickmode_Quadrilateral(kMfdArea0 + mi * 3 + part, At(p, b), At(p, b + 1), At(p, b + 2), At(p, b + 3));
        }
        const int s = kScr0 + j * 4;
        oapiVCSetAreaClickmode_Quadrilateral(kMfdScrArea0 + mi, At(p, s), At(p, s + 1), At(p, s + 2), At(p, s + 3));
    }
    for (int p = 0; p < 2; ++p)
        for (int part = 0; part < 3; ++part) {
            const int b = kShip + part * 4;
            oapiVCSetAreaClickmode_Quadrilateral(kShipArea0 + p * 3 + part, At(p, b), At(p, b + 1), At(p, b + 2), At(p, b + 3));
        }
    oapiVCSetAreaClickmode_Spherical(kModeArea, At(2, 0), 0.06);
}

// the VC camera: the eye rides with the couch; the default look turns with it while the pilot looks the default way
void Cockpit::Camera(bool force) {
    if (!loaded_) return;
    const VECTOR3 eye = At(pos_, kEye);
    VECTOR3 look = At(pos_, kLook) - eye; look /= length(look);
    v_->SetCameraOffset(eye);
    if (force) { v_->SetCameraDefaultDirection(look); lastLook_ = look; return; }
    if (dotp(look, lastLook_) > std::cos(0.3 * kDeg)) return;
    if (!oapiCameraInternal() || oapiCockpitMode() != COCKPIT_VIRTUAL || oapiGetFocusObject() != v_->GetHandle()) return;
    VECTOR3 g; oapiCameraGlobalDir(&g);
    MATRIX3 R; v_->GetRotationMatrix(R);
    const VECTOR3 cur = tmul(R, g);
    if (dotp(cur, lastLook_) > std::cos(2.0 * kDeg)) { v_->SetCameraDefaultDirection(look); lastLook_ = look; }
}

// ------------------------------------------------------------------------------------------------------------------------------
// the drum after the felt (specific) acceleration in the plane of symmetry: its up along it at about 1 g (sitting), into
// the backs (the couch's normal, 40° on from the up) as it grows to 2,2 g and over; the modules' roll after its lateral part
void Cockpit::Step(double dt, double frameZ) {
    frameZ_ = frameZ;
    // the sense of Orbiter's rotation, checked on the first real turn (the tracked up must lean where it was sent)
    if (!drumChecked_ && std::fabs(drum_) > 3.0) {
        const VECTOR3 up = dpts_[2] - dpts_[1];
        if (std::fabs(up.z) > 0.02) { if (up.z * drum_ < 0) drumSign_ = -drumSign_; drumChecked_ = true; }
    }
    if (!rollChecked_ && std::fabs(roll_) > 3.0) {
        const VECTOR3 up = pts_[0][kUp] - pts_[0][kEye];
        if (std::fabs(up.x) > 0.01) { if (up.x * roll_ < 0) rollSign_ = -rollSign_; rollChecked_ = true; }
    }
    VECTOR3 F, W; v_->GetForceVector(F); v_->GetWeightVector(W);
    const double m = v_->GetMass();
    VECTOR3 f = (F - W) / m;
    if ((v_->GroundContact() || v_->GetAttachmentStatus(v_->GetAttachmentHandle(true, 0))) && length(f) < 0.5) f = -W / m;   // landed or held: the support carries the weight
    felt_ = f;
    double drumT = drum_, rollT = 0.0;
    const double fyz = std::hypot(f.y, f.z), fn = length(f) / 9.81;
    if (fyz > 0.8) {
        const double back = 40.0 * Clamp((fn - 1.2) / 1.0, 0.0, 1.0);
        drumT = Clamp(std::atan2(f.z, f.y) / kDeg - back, -90.0, 90.0);
    }
    if (length(f) > 0.8) {
        const double c = std::cos(drum_ * kDeg), s = std::sin(drum_ * kDeg);
        rollT = Clamp(std::atan2(f.x, f.y * c + f.z * s) / kDeg, -15.0, 15.0);
    }
    drum_ = Approach(drum_, drumT, 12.0, dt);
    roll_ = Approach(roll_, rollT, 15.0, dt);
    v_->SetAnimation(drumAnim_, Clamp(0.5 + drumSign_ * drum_ / kDrumRange, 0.0, 1.0));
    v_->SetAnimation(rollAnim_, Clamp(0.5 + rollSign_ * roll_ / kRollRange, 0.0, 1.0));
    Quads(false);
    Camera(false);
    bank_.Step();
    drawT_ += dt;
    if (drawT_ > 0.25) { drawT_ = 0; Draw(); }
}

// ------------------------------------------------------------------------------------------------------------------------------
// a click's ray against the screens and keys (interior = mesh frame): the nearest hit acts
bool Cockpit::HitQuad(const VECTOR3& o, const VECTOR3& d, const VECTOR3& p1, const VECTOR3& p2, const VECTOR3& p3, double& t, double& u, double& v) {
    const VECTOR3 eu = p2 - p1, ev = p3 - p1, n = crossp(eu, ev);
    const double dn = dotp(d, n);
    if (std::fabs(dn) < 1e-12) return false;
    t = dotp(p1 - o, n) / dn;
    if (t <= 0) return false;
    const VECTOR3 q = o + d * t - p1;
    u = dotp(q, eu) / dotp(eu, eu); v = dotp(q, ev) / dotp(ev, ev);
    return u >= 0 && u <= 1 && v >= 0 && v <= 1;
}

bool Cockpit::Click(const VECTOR3& o, const VECTOR3& dir) {
    const VECTOR3 d = unit(dir);
    double best = 1e9; int kind = -1, a = 0, b = 0; double bu = 0, bv = 0;
    auto test = [&](const VECTOR3* q, int k, int i0, int i1) {
        double t, u, v;
        if (HitQuad(o, d, q[0], q[1], q[2], t, u, v) && t < best) { best = t; kind = k; a = i0; b = i1; bu = u; bv = v; }
    };
    for (int p = 0; p < 2; ++p) {
        const VECTOR3* q = pts_[p];
        for (int j = 0; j < 2; ++j) {
            test(q + kScr0 + j * 4, 0, 2 * p + j, 0);                                   // the Orbiter MFD's screen: touch
            for (int part = 0; part < 3; ++part) test(q + kMfd0 + j * 12 + part * 4, 1, 2 * p + j, part);   // its buttons
        }
        for (int part = 0; part < 3; ++part) test(q + kShip + part * 4, 2, p, part);   // the ship's MFD: screen, keys
    }
    {   // the mode switch
        const VECTOR3 c = dpts_[0] - o;
        const double tc = dotp(c, d), dd = dotp(c, c) - tc * tc;
        if (tc > 0 && dd < 0.06 * 0.06 && tc < best) { best = tc; kind = 3; }
    }
    if (best > 3.0) return false;                                                       // nothing within reach of the eye
    switch (kind) {
        case 0: bank_.Touch(a, bu, bv); return true;
        case 1: bank_.Press(a, b < 2 ? b * 6 + static_cast<int>(Clamp(std::floor(bv * 6.0), 0.0, 5.0)) : 12 + static_cast<int>(Clamp(std::floor(bu * 3.0), 0.0, 2.0))); return true;
        case 2:
            if (b == 0) {
                const char* lbl[2] = {nullptr, nullptr};
                const int nb = host_->PageButtons(a, page_[a], lbl);
                if (bv > 0.74 && nb > 0) { const int k = bu < 0.5 ? 0 : 1; if (k < nb) host_->PagePress(a, page_[a], k); }
            } else page_[a] = (b == 1 ? 0 : 6) + static_cast<int>(Clamp(std::floor(bv * 6.0), 0.0, 5.0));
            drawT_ = 1.0;
            return true;
        case 3: host_->ModeStep(1); drawT_ = 1.0; return true;
        default: return false;
    }
}

bool Cockpit::Mouse(int id, int event, const VECTOR3& p) {
    if (!(event & (PANEL_MOUSE_LBDOWN | PANEL_MOUSE_RBDOWN))) return false;
    if (id >= kMfdArea0 && id < kMfdArea0 + 12) {
        const int mi = (id - kMfdArea0) / 3, part = (id - kMfdArea0) % 3;
        bank_.Press(mi, part < 2 ? part * 6 + static_cast<int>(Clamp(std::floor(p.y * 6.0), 0.0, 5.0)) : 12 + static_cast<int>(Clamp(std::floor(p.x * 3.0), 0.0, 2.0)));
        return true;
    }
    if (id >= kMfdScrArea0 && id < kMfdScrArea0 + 4) { bank_.Touch(id - kMfdScrArea0, p.x, p.y); return true; }
    if (id >= kShipArea0 && id < kShipArea0 + 6) {
        const int pl = (id - kShipArea0) / 3, part = (id - kShipArea0) % 3;
        if (part == 0) {
            const char* lbl[2] = {nullptr, nullptr};
            const int nb = host_->PageButtons(pl, page_[pl], lbl);
            if (p.y > 0.74 && nb > 0) { const int b = p.x < 0.5 ? 0 : 1; if (b < nb) host_->PagePress(pl, page_[pl], b); }
        } else page_[pl] = (part == 1 ? 0 : 6) + static_cast<int>(Clamp(std::floor(p.y * 6.0), 0.0, 5.0));
        drawT_ = 1.0;
        return true;
    }
    if (id == kModeArea) { host_->ModeStep(event & PANEL_MOUSE_RBDOWN ? -1 : 1); drawT_ = 1.0; return true; }
    return false;
}

// ------------------------------------------------------------------------------------------------------------------------------
// the button labels (the MFDs' bezels) and the ships' MFDs (the page, its touch buttons, the 12 keys lit while their
// systems run), drawn into the surfaces bound in OnVisual
void Cockpit::Draw() {
    if (!fLbl_) {
        fLbl_ = oapiCreateFont(19, true, const_cast<char*>("Arial Cyr"), FONT_BOLD);
        fKey_ = oapiCreateFont(16, true, const_cast<char*>("Arial Cyr"), FONT_BOLD);
        fPage_ = oapiCreateFont(23, true, const_cast<char*>("Arial Cyr"));
        fTitle_ = oapiCreateFont(26, true, const_cast<char*>("Arial Cyr"), FONT_BOLD);
    }
    for (int which = 0; which < 2; ++which) {
        SURFHANDLE surf = which == 0 ? lblSurf_ : shipSurf_;
        if (!surf) continue;
        oapi::Sketchpad* skp = oapiGetSketchpad(surf);
        if (!skp) continue;
        skp->SetBackgroundMode(oapi::Sketchpad::BK_TRANSPARENT);
        oapi::Pen* pen = oapiCreatePen(0, 1, 0);
        oapi::Pen* oldPen = skp->SetPen(pen);
        auto fill = [&](int x0, int y0, int x1, int y1, DWORD c) {
            oapi::Brush* b = oapiCreateBrush(c); oapi::Brush* ob = skp->SetBrush(b);
            skp->Rectangle(x0, y0, x1, y1); skp->SetBrush(ob); oapiReleaseBrush(b);
        };
        auto text = [&](int x, int y, const char* s, DWORD col) { skp->SetTextColor(col); skp->Text(x, y, s, static_cast<int>(std::strlen(s))); };
        if (which == 0) {
            fill(0, 0, cb::kLblW, cb::kLblH, 0x1A1814);
            skp->SetFont(fLbl_);
            skp->SetTextAlign(oapi::Sketchpad::CENTER, oapi::Sketchpad::TOP);
            const int cols = cb::kLblW / cb::kLblCW;
            for (int mi = 0; mi < 4; ++mi)
                for (int b = 0; b < 15; ++b) {
                    const int cell = mi * 15 + b, x = (cell % cols) * cb::kLblCW, y = (cell / cols) * cb::kLblCH;
                    const char* s = bank_.Label(mi, b);
                    if (s && *s) text(x + cb::kLblCW / 2, y + 6, s, 0xE0E6E2);
                }
        } else {
            for (int pl = 0; pl < 2; ++pl) {
                const int x0 = pl * 512, w = cb::kShipScrW, h = cb::kShipScrH;
                fill(x0, 0, x0 + w, h, 0x07140A);
                skp->SetTextAlign(oapi::Sketchpad::LEFT, oapi::Sketchpad::TOP);
                skp->SetFont(fTitle_);
                char t[128];
                std::snprintf(t, sizeof t, "%s  ·  режим: %s", KeyName(pl, page_[pl]), host_->ModeName());
                text(x0 + 14, 8, t, 0xE8F0EA);
                skp->SetFont(fPage_);
                char lines[9][96];
                const int n = host_->PageLines(pl, page_[pl], lines, 9);
                for (int i = 0; i < n; ++i) text(x0 + 14, 46 + i * 26, lines[i], 0x6AC5A8);
                const char* lbl[2] = {nullptr, nullptr};
                const int nb = host_->PageButtons(pl, page_[pl], lbl);
                for (int b = 0; b < nb; ++b) {
                    const int bx0 = x0 + (b == 0 ? 12 : w / 2 + 6), bx1 = x0 + (b == 0 ? w / 2 - 6 : w - 12), by0 = static_cast<int>(h * 0.76), by1 = h - 10;
                    fill(bx0, by0, bx1, by1, 0x2A4A1C);
                    skp->SetTextAlign(oapi::Sketchpad::CENTER, oapi::Sketchpad::TOP);
                    text((bx0 + bx1) / 2, by0 + 18, lbl[b], 0xF0F4F0);
                    skp->SetTextAlign(oapi::Sketchpad::LEFT, oapi::Sketchpad::TOP);
                }
                skp->SetFont(fKey_);
                skp->SetTextAlign(oapi::Sketchpad::CENTER, oapi::Sketchpad::TOP);
                for (int k = 0; k < 12; ++k) {
                    const int kx = x0 + (k % 8) * 64, ky = 352 + (k / 8) * 40;
                    const bool lit = host_->KeyLit(pl, k);
                    fill(kx, ky, kx + 64, ky + 32, lit ? 0x2E7A3A : 0x26221E);
                    if (k == page_[pl]) { fill(kx, ky, kx + 64, ky + 3, 0x40C0F0); fill(kx, ky + 29, kx + 64, ky + 32, 0x40C0F0); }
                    text(kx + 32, ky + 8, kKeys[pl][k], lit ? 0xE8FFF0 : 0xA0A8A4);
                }
                skp->SetTextAlign(oapi::Sketchpad::LEFT, oapi::Sketchpad::TOP);
            }
        }
        skp->SetPen(oldPen);
        oapiReleasePen(pen);
        oapiReleaseSketchpad(skp);
    }
}

}  // namespace tantra::lander
