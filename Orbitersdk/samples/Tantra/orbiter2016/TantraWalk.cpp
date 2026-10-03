// TantraWalk: free walking inside the ship in the virtual cockpit view (test stage). See TantraWalk.h.
#include "TantraWalk.h"
#include "InteriorLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tantra::interior;

namespace {
constexpr double kRad = 0.28;        // body radius
constexpr double kEye = 1.65;        // eye height above the feet
constexpr double kSeatedEye = 1.2;   // eye height above the floor when sitting
constexpr double kStepUp = 0.45;
constexpr double kHead = 1.85;       // height of the body for the wall test
constexpr double kWalk = 1.7, kRun = 3.4;   // m/s
// The user's decision: the free walking camera (no body) is a leftover; people walk with their OrbiterCrew bodies.
// Off: the VC is the view from a bridge seat; F stands the person of that seat up. The code stays for debugging.
constexpr bool kFreeWalk = false;
double Clamp(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }
}  // namespace

void TantraWalk::Init(VESSEL* v) { v_ = v; }                     // the on-screen note is created on first use

void TantraWalk::EnsureNote() {
    if (note_) return;
    note_ = oapiCreateAnnotation(false, 0.7, _V(0.8, 1.0, 0.9));
    if (note_) oapiAnnotationSetPos(note_, 0.02, 0.84, 0.62, 0.97);
}

void TantraWalk::Shutdown() {
    if (note_) { oapiDelAnnotation(note_); note_ = nullptr; }
}

bool TantraWalk::ViewIsMine() const {
    return v_ && oapiCameraInternal() && oapiCockpitMode() == COCKPIT_VIRTUAL && oapiGetFocusObject() == v_->GetHandle();
}

bool TantraWalk::Active() const { return active_; }

void TantraWalk::OnLoadVC() {
    if (!kFreeWalk) { if (!seated_) SitAt(0); }                        // the commander's seat
    else if (x_ == 0.0 && z_ == 0.0 && feet_ == 1.0) Teleport(0);      // first time: the command bridge
    v_->SetCameraDefaultDirection(_V(0, 0, 1));
    v_->SetCameraRotationRange(0.98 * 3.14159265, 0.98 * 3.14159265, 1.4, 1.4);
    v_->SetCameraCatchAngle(0.0);
    oapiWriteLogV("Tantra walk: virtual cockpit loaded, walk mode on");
}

void TantraWalk::SitAt(int seat) {
    inBridge_ = true; seated_ = true; seat_ = seat; vy_ = 0.0;
    x_ = kSeats[seat].x; z_ = kSeats[seat].z + 0.35; feet_ = kBridgeFloorY;
    snprintf(noteText_, sizeof noteText_, "seated: %s", kSeats[seat].name);
}

void TantraWalk::Teleport(int place) {
    if (place < 0 || place >= kSpawnCount) return;
    const Spawn& s = kSpawns[place];
    inBridge_ = s.bridge != 0; seated_ = false; seat_ = -1;
    x_ = s.x; z_ = s.z; feet_ = s.feet; vy_ = 0.0;
    bool f = false; const double g = GroundAt(x_, z_, feet_ + 0.2, &f); if (f) feet_ = g;
    oapiWriteLogV("Tantra walk: teleport to %s (x %.2f z %.2f floor %.3f)", s.name, x_, z_, feet_);
    snprintf(noteText_, sizeof noteText_, "teleport: %s", s.name);
}

double TantraWalk::GroundAt(double x, double z, double feet, bool* found) const {
    double best = -1e9; bool ok = false;
    const Box* arr = inBridge_ ? kBridgeFloors : kFloors; const int n = inBridge_ ? kBridgeFloorsCount : kFloorsCount;
    for (int i = 0; i < n; i++) {
        const Box& b = arr[i];
        if (x >= b.x0 && x <= b.x1 && z >= b.z0 && z <= b.z1 && b.y1 <= feet + kStepUp + 1e-3 && b.y1 > best) { best = b.y1; ok = true; }
    }
    if (found) *found = ok;
    return best;
}

bool TantraWalk::Blocked(double x, double z, double feet) const {
    const Box* arr = inBridge_ ? kBridgeSolids : kSolids; const int n = inBridge_ ? kBridgeSolidsCount : kSolidsCount;
    for (int i = 0; i < n; i++) {
        const Box& b = arr[i];
        if (b.y1 <= feet + 0.35 || b.y0 >= feet + kHead) continue;
        const double dx = x - Clamp(x, b.x0, b.x1), dz = z - Clamp(z, b.z0, b.z1);
        if (dx * dx + dz * dz < kRad * kRad) return true;
    }
    return false;
}

void TantraWalk::ResolveWalls(double& x, double& z, double feet) const {
    const Box* arr = inBridge_ ? kBridgeSolids : kSolids; const int n = inBridge_ ? kBridgeSolidsCount : kSolidsCount;
    for (int it = 0; it < 4; it++) {
        bool moved = false;
        for (int i = 0; i < n; i++) {
            const Box& b = arr[i];
            if (b.y1 <= feet + 0.35 || b.y0 >= feet + kHead) continue;
            const double cx = Clamp(x, b.x0, b.x1), cz = Clamp(z, b.z0, b.z1);
            double dx = x - cx, dz = z - cz; const double d2 = dx * dx + dz * dz;
            if (d2 >= kRad * kRad) continue;
            if (d2 > 1e-10) { const double d = std::sqrt(d2), k = (kRad - d) / d; x += dx * k; z += dz * k; }
            else {                                              // the centre is inside the box: leave by the nearest face
                const double l = x - b.x0, r = b.x1 - x, f = z - b.z0, a = b.z1 - z, m = (std::min)((std::min)(l, r), (std::min)(f, a));
                if (m == l) x = b.x0 - kRad; else if (m == r) x = b.x1 + kRad; else if (m == f) z = b.z0 - kRad; else z = b.z1 + kRad;
            }
            moved = true;
        }
        if (!moved) break;
    }
    if (inBridge_) {                                            // the drum itself: flat end discs and the cylindrical vault
        x = Clamp(x, -kBridgeHx + 0.3, kBridgeHx - 0.3);
        const double dy = feet + 1.8 - kBridgeAxisY, lim = std::sqrt((std::max)(0.0, (kBridgeR - 0.3) * (kBridgeR - 0.3) - dy * dy));
        z = Clamp(z, kBridgeAxisZ - lim, kBridgeAxisZ + lim);
    }
}

const char* TantraWalk::RoomAt(double x, double z, double feet) const {
    const RoomRect* best = nullptr; double ba = 1e18;
    for (int i = 0; i < kRoomCount; i++) {
        const RoomRect& r = kRooms[i];
        if (x < r.x0 || x > r.x1 || z < r.z0 || z > r.z1 || feet < r.y0 - 0.3 || feet > r.y1 + 0.1) continue;
        const double a = double(r.x1 - r.x0) * double(r.z1 - r.z0);
        if (a < ba) { ba = a; best = &r; }
    }
    return best ? best->name : "-";
}

void TantraWalk::Move(double dx, double dz) {
    // axis by axis so that the walker slides along the walls
    double nx = x_ + dx, nz = z_;
    ResolveWalls(nx, nz, feet_); x_ = nx;
    nx = x_; nz = z_ + dz;
    ResolveWalls(nx, nz, feet_); z_ = nz;
}

void TantraWalk::Interact() {
    if (seated_ && standHook_ && standHook_(standCtx_, seat_)) {       // a person of OrbiterCrew: the body stands up behind the seat
        seated_ = false; seat_ = -1;
        return;
    }
    if (!kFreeWalk) { snprintf(noteText_, sizeof noteText_, "nobody of the crew to stand up here"); return; }
    if (seated_) {                                              // stand up BEHIND the seat
        z_ = kSeats[seat_].z - 0.95; seated_ = false; seat_ = -1;
        oapiWriteLogV("Tantra walk: stands up");
        return;
    }
    if (inBridge_) {
        int best = -1; double bd = 1.4;
        for (int i = 0; i < 4; i++) { const double d = std::hypot(x_ - kSeats[i].x, z_ - kSeats[i].z); if (d < bd) { bd = d; best = i; } }
        if (best >= 0) {
            seated_ = true; seat_ = best; x_ = kSeats[best].x; z_ = kSeats[best].z + 0.35; vy_ = 0.0;   // eyes right at the console
            oapiWriteLogV("Tantra walk: sits down in the seat '%s'", kSeats[best].name);
            snprintf(noteText_, sizeof noteText_, "seated: %s", kSeats[best].name);
        } else snprintf(noteText_, sizeof noteText_, "no seat within reach");
        return;
    }
    snprintf(noteText_, sizeof noteText_, "nothing to use here");
}

void TantraWalk::ApplyCamera(double meshDZ) {
    const double eye = seated_ ? kSeatedEye : kEye;
    const double base = inBridge_ ? feet_ : feet_;
    v_->SetCameraOffset(_V(x_, base + eye, z_ + meshDZ));
}

void TantraWalk::Report(const char* extra) {
    const double t = oapiGetSysTime();
    if (t - lastLog_ > 1.0) {
        lastLog_ = t;
        oapiWriteLogV("Tantra walk: %s | x %.2f z %.2f floor %.3f %s%s", RoomAt(x_, z_, feet_), x_, z_, feet_, inBridge_ ? "(capsule) " : "", extra);
    }
}

int TantraWalk::Keys(char* kstate, double meshDZ) {
    const double sys = oapiGetSysTime();
    double dt = sys - lastSys_; lastSys_ = sys;
    if (dt <= 0.0 || dt > 0.2) dt = 0.016;
    active_ = ViewIsMine();
    static const int kKeys[] = {OAPI_KEY_W, OAPI_KEY_A, OAPI_KEY_S, OAPI_KEY_D, OAPI_KEY_E, OAPI_KEY_F, OAPI_KEY_1, OAPI_KEY_2, OAPI_KEY_3, OAPI_KEY_4, OAPI_KEY_5, OAPI_KEY_6};
    auto down = [&](int k) { return (kstate[k] & 0x80) != 0; };
    auto edge = [&](int k) { return down(k) && !(prev_[k] & 0x80); };
    if (!active_) {
        for (int k : kKeys) prev_[k] = kstate[k];
        if (note_) oapiAnnotationSetText(note_, const_cast<char*>(""));
        return 0;
    }
    static const int kDigits[6] = {OAPI_KEY_1, OAPI_KEY_2, OAPI_KEY_3, OAPI_KEY_4, OAPI_KEY_5, OAPI_KEY_6};
    if (!kFreeWalk && !seated_) SitAt(0);
    if (kFreeWalk) for (int i = 0; i < 6; i++) if (edge(kDigits[i])) Teleport(i);
    if (edge(OAPI_KEY_E) || edge(OAPI_KEY_F)) Interact();      // F: the action key of OrbiterCrew (also stands a person up)

    // heading from the camera direction (the mouse look is Orbiter's own)
    VECTOR3 g; oapiCameraGlobalDir(&g);
    MATRIX3 R; v_->GetRotationMatrix(R);
    const VECTOR3 l = tmul(R, g);
    if (std::hypot(l.x, l.z) > 0.2) yaw_ = std::atan2(l.x, l.z);
    const double fx = std::sin(yaw_), fz = std::cos(yaw_), rx = std::cos(yaw_), rz = -std::sin(yaw_);
    if (kFreeWalk && !seated_) {
        double mx = 0.0, mz = 0.0;
        if (down(OAPI_KEY_W)) { mx += fx; mz += fz; }
        if (down(OAPI_KEY_S)) { mx -= fx; mz -= fz; }
        if (down(OAPI_KEY_D)) { mx += rx; mz += rz; }
        if (down(OAPI_KEY_A)) { mx -= rx; mz -= rz; }
        const double len = std::hypot(mx, mz);
        if (len > 1e-6) {
            const double sp = ((kstate[OAPI_KEY_LSHIFT] & 0x80) || (kstate[OAPI_KEY_RSHIFT] & 0x80)) ? kRun : kWalk;
            Move(mx / len * sp * dt, mz / len * sp * dt);
        }
        // the floor: steps up to 0.45 m, falls into holes
        bool found = false; const double gnd = GroundAt(x_, z_, feet_, &found);
        if (found && feet_ <= gnd + 1e-3) { feet_ = gnd; vy_ = 0.0; }
        else { vy_ -= 9.81 * dt; feet_ += vy_ * dt; if (found && feet_ <= gnd) { feet_ = gnd; vy_ = 0.0; } }
        if (feet_ < -12.0) { oapiWriteLogV("Tantra walk: fell out of the ship, back to the lower lobby"); Teleport(2); }
    }
    if (kFreeWalk) EnsureNote();                                // the note is the free walking camera's only (debug)
    for (int k : kKeys) {                                       // free walking: these keys are ours; seated: only E and F (the ship flies)
        prev_[k] = kstate[k];
        if (kFreeWalk || k == OAPI_KEY_E || k == OAPI_KEY_F) kstate[k] = 0;
    }
    ApplyCamera(meshDZ);
    char extra[96]; extra[0] = 0; if (seated_) snprintf(extra, sizeof extra, "seated(%s)", kSeats[seat_].name);
    Report(extra);
    if (kFreeWalk && sys - lastNote_ > 0.2 && note_) {
        lastNote_ = sys;
        char buf[320];
        if (kFreeWalk) snprintf(buf, sizeof buf, "WALK  %s  (x %.1f  z %.1f  floor %.2f)\n%s\nW A S D move, Shift run, E use/sit/stairs/ladder, 1-6 teleport (1 bridge)", RoomAt(x_, z_, feet_), x_, z_, feet_, noteText_);
        else snprintf(buf, sizeof buf, "%s\nF - встать (член экипажа этого кресла)", noteText_);
        oapiAnnotationSetText(note_, buf);
    }
    return 0;
}
