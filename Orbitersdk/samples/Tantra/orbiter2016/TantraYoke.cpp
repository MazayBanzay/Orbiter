// TantraInterior - the commander's yoke (variant 7: Tantra_Design/bridge_variants/v7.js, the user, 2026-10-04). It comes out of
// the desk's inner face when he has sat down and his seat has arrived (1.6 s; back in 0.5 s when he gets up), its hub ahead of his
// hip at his seat's height and turned to his eye. It does not fly the ship: it only SHOWS what is done with the keyboard or the
// joystick (the bank turns the wheel, the pitch pulls the column toward him; read from the ship's own seat keys) and, with no hand on
// them, the ship's own turning as an airliner's yoke does under the autopilot (~10 deg of wheel per 1 deg/s of roll). It shakes
// with the structure as the crew's heads do (OrbiterCrew HeadSway): the engines at 12 Hz, ~1.5 mm per g of thrust, the air at 4 Hz,
// up to ~12 mm at 15 kPa, most near Mach 1; the column, held at the desk, takes 0.35 of it. The rigid parts (the column with its
// gaiter, the rod, the hub with the horns, the grips, the buttons and its face) move by their vertices from the mesh's own (built
// out at the nominal place, InteriorLayout.h kYoke*).
#include "TantraInterior.h"
#include "Tantra.h"
#include "InteriorLayout.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace tantra::interior;

namespace {
double Clamp(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }
// the rotation that takes the unit vector a to the unit vector b (about a x b)
MATRIX3 RotTo(const VECTOR3& a, const VECTOR3& b) {
    const VECTOR3 v = crossp(a, b);
    const double c = dotp(a, b);
    if (c < -0.9999) return _M(1, 0, 0, 0, -1, 0, 0, 0, -1);
    const double k = 1.0 / (1.0 + c);
    return _M(v.x * v.x * k + c, v.x * v.y * k - v.z, v.x * v.z * k + v.y,
              v.y * v.x * k + v.z, v.y * v.y * k + c, v.y * v.z * k - v.x,
              v.z * v.x * k - v.y, v.z * v.y * k + v.x, v.z * v.z * k + c);
}
// the rotation by t about the unit axis k
MATRIX3 AxisAngle(const VECTOR3& k, double t) {
    const double c = std::cos(t), s = std::sin(t), C = 1 - c;
    return _M(c + k.x * k.x * C, k.x * k.y * C - k.z * s, k.x * k.z * C + k.y * s,
              k.y * k.x * C + k.z * s, c + k.y * k.y * C, k.y * k.z * C - k.x * s,
              k.z * k.x * C - k.y * s, k.z * k.y * C + k.x * s, c + k.z * k.z * C);
}
VECTOR3 V(const double* a) { return _V(a[0], a[1], a[2]); }
}  // namespace

void TantraInterior::YokeStep(double dt) {
    if (kYokeColGrp < 0) return;
    if (yokeRef_.empty()) {                                              // the mesh's own vertices: the yoke as built
        MESHHANDLE tm = v_->GetMeshTemplate(vcMesh_);
        if (!tm) return;
        const int ids[8] = {kYokeColGrp, kYokeRodGrp, kYokeHubGrps[0], kYokeHubGrps[1], kYokeHubGrps[2], kYokeHubGrps[3], kYokeHubGrps[4], kYokeHubGrps[5]};
        for (int id : ids) {
            MESHGROUP* g = id >= 0 ? oapiMeshGroup(tm, DWORD(id)) : nullptr;
            if (!g || !g->Vtx) { yokeRef_.clear(); return; }
            yokeRef_.push_back({id, std::vector<NTVERTEX>(g->Vtx, g->Vtx + g->nVtx)});
        }
    }
    dt = (std::min)(dt, 0.1);
    const bool want = CmdAtDesk();                                       // out when he sits at the desk
    const double tgt = want ? 1.0 : 0.0;
    yokeE_ += (tgt > yokeE_ ? 1 : -1) * (std::min)(std::fabs(tgt - yokeE_), dt / (want ? 1.6 : 0.5));
    const double e = yokeE_ * yokeE_ * (3 - 2 * yokeE_);
    // the hands: the focus is the person, so the ship reads the numpad itself (Tantra::SeatKeys) and sets the aerodynamic
    // surfaces from it - the bank (Num 1 / 3) on the ailerons, the pitch (Num 2 / 8) on the elevator; without them the ship's
    // own turning (the autopilots move the RCS, not the surfaces)
    double r = v_->GetControlSurfaceLevel(AIRCTRL_AILERON), p = v_->GetControlSurfaceLevel(AIRCTRL_ELEVATOR);
    {
        VECTOR3 rt, fw; v_->HorizonRot(_V(1, 0, 0), rt); v_->HorizonRot(_V(0, 0, 1), fw);
        const double pitch = std::asin(Clamp(fw.y, -1, 1)), bank = -std::asin(Clamp(rt.y, -1, 1));   // bank > 0: the right wing down
        if (attInit_ && dt > 1e-4 && std::fabs(r) + std::fabs(p) < 0.02) {
            r = Clamp((bank - attBank_) / dt * DEG * 0.25, -1, 1);
            p = Clamp((pitch - attPitch_) / dt * DEG * 0.25, -1, 1);
        }
        attPitch_ = pitch; attBank_ = bank; attInit_ = true;
    }
    ykRoll_ += (Clamp(r, -1, 1) - ykRoll_) * (std::min)(1.0, dt * 6);
    ykPitch_ += (Clamp(p, -1, 1) - ykPitch_) * (std::min)(1.0, dt * 6);
    // the shaking
    VECTOR3 T; v_->GetThrustVector(T);
    const double engG = length(T) / (std::max)(1.0, v_->GetMass()) / 9.81, q = v_->GetDynPressure(), M = v_->GetMachNumber();
    const double eng = 0.0015 * Clamp(engG, 0, 3), trans = 1 + 1.5 * std::exp(-((M - 1) / 0.25) * ((M - 1) / 0.25)), buf = 0.012 * Clamp(q / 15e3, 0, 1.5) * trans;
    auto rnd = [&]() { unsigned s = vibSeed_; s ^= s << 13; s ^= s >> 17; s ^= s << 5; vibSeed_ = s; return (s & 0xFFFFFFu) / double(0x800000) - 1.0; };
    double vib[3];
    for (int i = 0; i < 3; ++i) {
        vibE_[i] += (rnd() - vibE_[i]) * (std::min)(1.0, 2 * PI * 12 * dt);
        vibB_[i] += (rnd() - vibB_[i]) * (std::min)(1.0, 2 * PI * 4 * dt);
        vib[i] = 0.35 * (vibE_[i] * 2.5 * eng + vibB_[i] * 2.5 * buf);
    }
    // the hub: stowed low in front of the desk's inner face, under its edge (out of his view of the screens); out ahead of his
    // hip at his seat's height; pulled toward him with the pitch; turned to his eye; the wheel turns with the roll (the right
    // wing down: its top to his right)
    const VECTOR3 B = V(kYokeB), H0 = V(kYokeH0), U0 = V(kYokeU0), Y0 = V(kYokeY0), Z0 = V(kYokeZ0);
    const double hc = CmdSeatHeight(), hipZ = SeatZ(0), Fy = kBridgeFloorY;
    const double zst = B.z - kYokeStow, z = zst - e * (zst - (hipZ + kYokeHub[0]));
    VECTOR3 H = _V(B.x, Fy + kYokeHub[2] + e * (kYokeHub[1] + hc - kYokeHub[2]), z - ykPitch_ * 0.04 * e);
    H = H + _V(vib[0], vib[1], vib[2]) * e;
    const VECTOR3 u = unit(H - B);
    VECTOR3 n = _V(B.x, Fy + 0.72 + hc + kYokeEye[0], hipZ + kYokeEye[1]) - H;
    n.x = 0.0; n = unit(n);
    const MATRIX3 Rc = RotTo(U0, u), Rt = RotTo(Z0, n);
    const double wheel = ykRoll_ * 0.7 * e;
    MATRIX3 Rr = AxisAngle(n, wheel);
    if (wheel != 0.0 && mul(Rr, mul(Rt, Y0)).x * wheel < 0.0) Rr = AxisAngle(n, -wheel);
    const MATRIX3 Rh = mul(Rr, Rt);
    hubH_ = H; hubX_ = mul(Rh, _V(1, 0, 0)); hubY_ = mul(Rh, Y0); hubZ_ = mul(Rh, Z0);
    if (mesh_) {
        const double key[8] = {H.x, H.y, H.z, u.y, u.z, n.y, n.z, wheel};
        bool same = true;
        for (int i = 0; i < 8; ++i) if (std::fabs(key[i] - yokeKey_[i]) > 1e-5) same = false;
        if (!same) {
            std::memcpy(yokeKey_, key, sizeof key);
            static std::vector<NTVERTEX> out;
            for (const YokeGrp& g : yokeRef_) {
                const bool col = g.grp == kYokeColGrp, rod = g.grp == kYokeRodGrp;
                const MATRIX3& R = col || rod ? Rc : Rh;
                const VECTOR3 o0 = col ? B : H0, o1 = col ? B : H;
                out.assign(g.v.begin(), g.v.end());
                for (NTVERTEX& v : out) {
                    const VECTOR3 pp = o1 + mul(R, _V(v.x, v.y, v.z) - o0), nn = mul(R, _V(v.nx, v.ny, v.nz));
                    v.x = float(pp.x); v.y = float(pp.y); v.z = float(pp.z); v.nx = float(nn.x); v.ny = float(nn.y); v.nz = float(nn.z);
                }
                GROUPEDITSPEC es = {}; es.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; es.nVtx = DWORD(out.size()); es.vIdx = nullptr; es.Vtx = out.data();
                oapiEditMeshGroup(mesh_, DWORD(g.grp), &es);
            }
        }
        if (kYokeUvtLitGrp >= 0 && uvtShown_ != int(uvtOn_)) {         // УВТ lit while on
            uvtShown_ = uvtOn_;
            GROUPEDITSPEC es = {}; es.flags = GRPEDIT_SETUSERFLAG; es.UsrFlag = uvtOn_ ? 0 : 2;
            oapiEditMeshGroup(mesh_, DWORD(kYokeUvtLitGrp), &es);
        }
    }
}

// THE THROTTLE QUADRANT: the levers stand where their thrust is set (upright = half, leaning ±kLeverSwing: back at 0, forward at
// МАКС), the cups' wheel on the pods' lever turns with the cups' angle set (geared: 0.05 rad per degree), the guard cover over the
// pods' key flips up toward him while it is open. Rigid parts moved by their vertices, as the yoke's.
// A lever's grip now: the arm leans about its pivot (as QuadStep draws it); МАРШ's palm head higher than ВЫДВ. БЛОКИ's bar.
VECTOR3 TantraInterior::LeverGrip(int k) const {
    const double a = ((k == 0 ? qMain_ : qPod_) - 0.5) * 2 * kLeverSwing, h = k == 0 ? 0.15 : 0.145;
    return V(kLeverPivot[k]) + _V(0, std::cos(a) * h, std::sin(a) * h);
}

// While the left mouse button holds a lever (taken by its grip): the cursor's ray meets the lever's plane (x of its pivot), the
// angle there is the lean; it goes to the left console's slot as a touch at that level (the same path as the slot's touch).
void TantraInterior::LeverDrag() {
    if (leverDrag_ < 0) return;
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) { leverDrag_ = -1; return; }
    VECTOR3 o, d;
    if (!HoverRay(o, d) || std::fabs(d.x) < 1e-6) return;
    const VECTOR3 P = V(kLeverPivot[leverDrag_]);
    const double tt = (P.x - o.x) / d.x;
    if (tt <= 0) return;
    const VECTOR3 q = o + d * tt - P;
    const double a = std::atan2(q.z, q.y), f = Clamp(a / (2 * kLeverSwing) + 0.5, 0.0, 1.0);
    if (std::fabs(f - leverSent_) < 0.005) return;
    leverSent_ = f;
    const double cx = leverDrag_ == 0 ? 0.19 : 0.105;                   // the slot on the left console's field (TantraConsoles SlotL)
    if (touch_.Touch) touch_.Touch(touch_.ctx, 4, cx / 0.29, (0.03 + 0.20 * (1.0 - f)) / 0.54);
}

void TantraInterior::QuadStep(double dt) {
    LeverDrag();
    if (kLeverGrps[0][0] < 0) return;
    if (quadRef_.empty()) {
        MESHHANDLE tm = v_->GetMeshTemplate(vcMesh_);
        if (!tm) return;
        const int ids[6] = {kLeverGrps[0][0], kLeverGrps[0][1], kLeverGrps[1][0], kLeverGrps[1][1], kLeverGrps[1][2], kCoverGrp};
        for (int id : ids) {
            MESHGROUP* g = id >= 0 ? oapiMeshGroup(tm, DWORD(id)) : nullptr;
            if (!g || !g->Vtx) { quadRef_.clear(); return; }
            quadRef_.push_back({id, std::vector<NTVERTEX>(g->Vtx, g->Vtx + g->nVtx)});
        }
    }
    dt = (std::min)(dt, 0.1);
    const double k = (std::min)(1.0, dt * 10);
    qMain_ += (Clamp(qMainT_, 0, 1) - qMain_) * k; qPod_ += (Clamp(qPodT_, 0, 1) - qPod_) * k;
    qWheel_ += (qWheelT_ - qWheel_) * (std::min)(1.0, dt * 4);
    qCover_ += (qCoverT_ - qCover_) * (std::min)(1.0, dt * 8);
    const VECTOR3 ax = _V(1, 0, 0);
    auto lean = [&](double a) {                                          // a > 0: the top forward (+z)
        MATRIX3 R = AxisAngle(ax, a);
        if (a != 0.0 && mul(R, _V(0, 1, 0)).z * a < 0.0) R = AxisAngle(ax, -a);
        return R;
    };
    const MATRIX3 R0 = lean((qMain_ - 0.5) * 2 * kLeverSwing), R1 = lean((qPod_ - 0.5) * 2 * kLeverSwing);
    const MATRIX3 Rw = AxisAngle(ax, -qWheel_ * 0.05);
    MATRIX3 Rc = AxisAngle(ax, qCover_ * 1.9);                          // the cover: up toward him (its body lies forward of the hinge)
    if (qCover_ > 0.0 && mul(Rc, _V(0, 0, 1)).y < 0.0) Rc = AxisAngle(ax, -qCover_ * 1.9);
    const VECTOR3 P0 = V(kLeverPivot[0]), P1 = V(kLeverPivot[1]), WC = V(kWheelC), HC = V(kCoverHinge);
    wheelC_ = P1 + mul(R1, WC - P1);
    if (!mesh_) return;
    const double key[4] = {qMain_, qPod_, qWheel_, qCover_};
    bool same = true;
    for (int i = 0; i < 4; ++i) if (std::fabs(key[i] - quadKey_[i]) > 1e-5) same = false;
    if (same) return;
    std::memcpy(quadKey_, key, sizeof key);
    static std::vector<NTVERTEX> out;
    for (const YokeGrp& g : quadRef_) {
        out.assign(g.v.begin(), g.v.end());
        const bool main = g.grp == kLeverGrps[0][0] || g.grp == kLeverGrps[0][1], wheel = g.grp == kLeverGrps[1][2], cover = g.grp == kCoverGrp;
        for (NTVERTEX& v : out) {
            VECTOR3 p = _V(v.x, v.y, v.z), n = _V(v.nx, v.ny, v.nz);
            if (cover) { p = HC + mul(Rc, p - HC); n = mul(Rc, n); }
            else if (main) { p = P0 + mul(R0, p - P0); n = mul(R0, n); }
            else {
                if (wheel) { p = WC + mul(Rw, p - WC); n = mul(Rw, n); }
                p = P1 + mul(R1, p - P1); n = mul(R1, n);
            }
            v.x = float(p.x); v.y = float(p.y); v.z = float(p.z); v.nx = float(n.x); v.ny = float(n.y); v.nz = float(n.z);
        }
        GROUPEDITSPEC es = {}; es.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; es.nVtx = DWORD(out.size()); es.vIdx = nullptr; es.Vtx = out.data();
        oapiEditMeshGroup(mesh_, DWORD(g.grp), &es);
    }
}
