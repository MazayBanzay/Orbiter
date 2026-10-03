// TantraInterior: the provider of OcInterior for the Tantra. See TantraInterior.h.
#include "TantraInterior.h"
#include "InteriorLayout.h"
#include "TantraCrew.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace tantra::interior;

namespace {
constexpr int kSeatN = int(sizeof(kSeats) / sizeof(kSeats[0]));
constexpr double kKnee = 0.35;          // solids lower than feet + this are steps, not walls
constexpr double kHips = 0.5;           // hips above the floor in a seat
const char* const kSeatLabel[] = {"кресло командира", "пульт: правое кресло", "пульт: левое кресло", "кресло навигатора"};
double Clamp(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }
TantraInterior* Self(void* c) { return static_cast<TantraInterior*>(c); }
}  // namespace

void TantraInterior::Init(VESSEL* ship, UINT vcMeshIdx, double (*meshDZ)(void*), void* dzCtx, TantraCrew* crew) {
    v_ = ship; crew_ = crew;
    if (crew_) crew_->SetArrival(_V(kArrivalX, kArrivalY, kArrivalZ), _V(1, 0, 0));   // up the lift: in the airlock cell, facing inboard vcMesh_ = vcMeshIdx; dz_ = meshDZ; dzCtx_ = dzCtx;
    att_ = v_->CreateAttachment(false, _V(0, 0, 0), _V(0, 0, 1), _V(0, 1, 0), "OCINT");    // the walking body hangs on it
    fns_.Attach = cAttach; fns_.Ground = cGround; fns_.Walls = cWalls; fns_.Zone = nullptr; fns_.Gravity = nullptr;
    fns_.Count = cCount; fns_.Item = cItem; fns_.Use = cUse; fns_.Seat = cSeat; fns_.Viewing = cViewing;
    ext_.size = sizeof(OcInteriorExt); ext_.Origin = cOrigin; ext_.CanWalk = cCanWalk;
}

void TantraInterior::Register() {
    if (reg_) return;
    if (!api_.Load() || !api_.HasInterior() || !api_.UnregisterInterior) {
        oapiWriteLogV("Tantra interior: OrbiterCrew without the interior interface - people do not walk inside yet");
        return;
    }
    api_.RegisterInterior(v_->GetHandle(), &fns_, this);
    if (api_.SetInteriorExt) api_.SetInteriorExt(v_->GetHandle(), &ext_);   // the interior frame (CG shift) and the walking rule
    else oapiWriteLogV("Tantra interior: OrbiterCrew without ocSetInteriorExt - the interior frame drifts with the CG");
    reg_ = true;
    oapiWriteLogV("Tantra interior: registered with OrbiterCrew (%d solids, %d floors, %d seats)", kSolidsCount, kFloorsCount, kSeatN);
}

void TantraInterior::Unregister() {
    if (reg_) api_.UnregisterInterior(v_->GetHandle());
    reg_ = false;
    api_.Unload();
}

bool TantraInterior::InCapsule(double x, double y, double z) const {
    const double dy = y - kBridgeAxisY, dz = z - kBridgeAxisZ;
    return std::fabs(x) <= kBridgeHx && dy * dy + dz * dz <= kBridgeR * kBridgeR;
}

void TantraInterior::Resolve(double& x, double& z, double feet, double radius, double height, bool capsule) const {
    const Box* arr = capsule ? kBridgeSolids : kSolids; const int n = capsule ? kBridgeSolidsCount : kSolidsCount;
    for (int it = 0; it < 4; it++) {
        bool moved = false;
        for (int i = 0; i < n; i++) {
            const Box& b = arr[i];
            if (b.y1 <= feet + kKnee || b.y0 >= feet + height) continue;
            const double cx = Clamp(x, b.x0, b.x1), cz = Clamp(z, b.z0, b.z1);
            const double dx = x - cx, dz = z - cz, d2 = dx * dx + dz * dz;
            if (d2 >= radius * radius) continue;
            if (d2 > 1e-10) { const double d = std::sqrt(d2), k = (radius - d) / d; x += dx * k; z += dz * k; }
            else {                                                       // the centre is inside the box: leave by the nearest face
                const double l = x - b.x0, r = b.x1 - x, f = z - b.z0, a = b.z1 - z, m = (std::min)((std::min)(l, r), (std::min)(f, a));
                if (m == l) x = b.x0 - radius; else if (m == r) x = b.x1 + radius; else if (m == f) z = b.z0 - radius; else z = b.z1 + radius;
            }
            moved = true;
        }
        if (!moved) break;
    }
    if (capsule) {                                                       // the drum: flat end discs and the cylindrical vault
        const bool door = std::fabs(z - kDoorZ) <= kDoorHalfW - radius && feet + height <= kDoorTop;   // the starboard exit
        x = Clamp(x, -kBridgeHx + radius, door ? kDoorX1 : kBridgeHx - radius);
        const double dy = feet + height - kBridgeAxisY, rr = kBridgeR - radius;
        const double lim = std::sqrt((std::max)(0.0, rr * rr - dy * dy));
        z = Clamp(z, kBridgeAxisZ - lim, kBridgeAxisZ + lim);
    }
}

// ---- OcInterior callbacks: the interior frame = the mesh frame of TantraVC.msh (Origin gives the CG shift to the ship frame) ----
ATTACHMENTHANDLE TantraInterior::cAttach(void* c) { return Self(c)->att_; }

int TantraInterior::cGround(void* c, const VECTOR3* p, double stepUp, double* floorY) {
    const TantraInterior* t = Self(c);
    const double x = p->x, y = p->y, z = p->z;
    const bool cap = t->InCapsule(x, y + 1.0, z);
    const Box* arr = cap ? kBridgeFloors : kFloors; const int n = cap ? kBridgeFloorsCount : kFloorsCount;
    double best = -1e9; int ok = 0;
    for (int i = 0; i < n; i++) {
        const Box& b = arr[i];
        if (x >= b.x0 && x <= b.x1 && z >= b.z0 && z <= b.z1 && b.y1 <= y + stepUp + 1e-3 && b.y1 > best) { best = b.y1; ok = 1; }
    }
    if (ok && floorY) *floorY = best;
    return ok;
}

void TantraInterior::cWalls(void* c, const VECTOR3* from, VECTOR3* to, double radius, double height) {
    const TantraInterior* t = Self(c);
    const double feet = from->y;
    const bool cap = t->InCapsule(from->x, feet + 1.0, from->z);
    double x = from->x, z = from->z;
    double nx = to->x, nz = z;                                           // axis by axis: the body slides along the walls
    t->Resolve(nx, nz, feet, radius, height, cap); x = nx;
    nx = x; nz = to->z;
    t->Resolve(nx, nz, feet, radius, height, cap);
    to->x = nx; to->z = nz;
}

int TantraInterior::cCount(void* c) { return kSeatN + (Self(c)->crew_ ? Self(c)->crew_->CrewItemCount() : 0); }

int TantraInterior::cItem(void* c, int i, OcItem* out) {
    const TantraInterior* t = Self(c);
    if (i >= kSeatN && t->crew_) return t->crew_->CrewItem(i - kSeatN, out);    // the lift, the way in
    if (i < 0 || i >= kSeatN || !out) return 0;
    out->id = i; out->kind = OC_SEAT;
    out->pos = _V(kSeats[i].x, kBridgeFloorY, kSeats[i].z);
    out->dir = _V(0, 0, 1);                                              // every bridge seat faces the bow
    out->radius = 1.0;
    snprintf(out->label, sizeof out->label, "%s", kSeatLabel[i]);
    return 1;
}

void TantraInterior::cUse(void* c, int id, int personId) {
    TantraInterior* t = Self(c);
    if (t->crew_ && t->crew_->CrewUse(id, personId)) return;
    if (id < 0 || id >= kSeatN || t->seat_ >= 0) return;
    t->seat_ = id; t->person_ = personId;
    oapiWriteLogV("Tantra interior: person %d sits down in the seat '%s'", personId, kSeats[id].name);
    t->api_.LeaveInterior(personId, id);                                 // the body leaves, the focus goes to the ship
    t->attachCam_ = true;                                                // the cockpit view on the next step (F8 if it opens the 2D panel)
}

void TantraInterior::cSeat(void* c, int id, VECTOR3* pos, VECTOR3* dir) {
    if (id < 0 || id >= kSeatN) return;
    if (pos) *pos = _V(kSeats[id].x, kBridgeFloorY + kHips, kSeats[id].z);
    if (dir) *dir = _V(0, 0, 1);
}

void TantraInterior::cViewing(void* c, int on) {
    TantraInterior* t = Self(c);
    t->v_->SetMeshVisibilityMode(t->vcMesh_, on ? MESHVIS_ALWAYS : MESHVIS_VC);
    oapiWriteLogV("Tantra interior: %s", on ? "a person looks inside: the interior is drawn as an outside mesh" : "interior back to the VC only");
}

OBJHANDLE TantraInterior::ViewerBody() const {
    if (!reg_ || !api_.ShipOf) return nullptr;
    OBJHANDLE f = oapiGetFocusObject();
    if (!f || f == v_->GetHandle()) return nullptr;
    const int id = api_.PersonOfBody(f);
    return (id && api_.ShipOf(id) == v_->GetHandle()) ? f : nullptr;
}

void TantraInterior::cOrigin(void* c, VECTOR3* o) { *o = _V(0, 0, Self(c)->DZ()); }

int TantraInterior::cCanWalk(void* c, char* reason, int n) {
    const TantraInterior* t = Self(c);
    return t->canWalk_ ? t->canWalk_(t->canCtx_, reason, n) : 1;
}

bool TantraInterior::StandUp(int seat) {
    if (!reg_ || seat < 0 || seat >= kSeatN) return false;
    standSeat_ = seat;                                                   // done in Step(): never inside the key handler (Orbiter crashed)
    return true;
}

void TantraInterior::Step() {
    if (attachCam_) { attachCam_ = false; oapiCameraAttach(v_->GetHandle(), 1); }
    if (standSeat_ < 0) return;
    const int seat = standSeat_;
    standSeat_ = -1;
    int p = (seat_ == seat) ? person_ : 0;
    if (!p && crew_) p = crew_->PersonOf(seat);                          // seat N -> crew slot N (commander, starboard, port, navigator)
    if (!p) { oapiWriteLogV("Tantra interior: nobody of the crew for the seat '%s'", kSeats[seat].name); return; }
    seat_ = -1; person_ = 0;
    const VECTOR3 pos = _V(kSeats[seat].x, kBridgeFloorY, kSeats[seat].z - 0.95), dir = _V(0, 0, 1);   // behind the seat, facing the screen
    oapiWriteLogV("Tantra interior: person %d stands up from the seat '%s'", p, kSeats[seat].name);
    api_.EnterInterior(p, &pos, &dir);                                   // the body stands there, the focus goes to it
}
