// TantraInterior: the provider of OcInterior for the Tantra. See TantraInterior.h.
#include "TantraInterior.h"
#include "Tantra.h"
#include "InteriorLayout.h"
#include "MeshLayout.h"
#include "TantraCrew.h"
#include "PanelLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tantra::interior;

namespace {
constexpr int kSeatN = int(sizeof(kSeats) / sizeof(kSeats[0]));
constexpr double kKnee = 0.35;          // solids lower than feet + this are steps, not walls
constexpr int kPanelBtnId0 = 1000;        // the panel spots on the console: 1000 + index
constexpr int kMfdId0 = 400;             // the MFD buttons: 400 + mfd * 16 + button
constexpr int kPanelId0 = 200;           // the lift panel's items (ids 100 and up belong to TantraCrew first, so these are served before it)
constexpr double kHips = 0.72;          // pelvis above the floor in a seat (cushion top 0.635 + 0.085)
const char* const kSeatLabel[] = {"кресло командира", "пульт: правое кресло", "пульт: левое кресло", "кресло навигатора"};
double Clamp(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }
TantraInterior* Self(void* c) { return static_cast<TantraInterior*>(c); }
}  // namespace

void TantraInterior::Init(VESSEL* ship, UINT vcMeshIdx, double (*meshDZ)(void*), void* dzCtx, TantraCrew* crew) {
    v_ = ship; crew_ = crew;
    vcMesh_ = vcMeshIdx; dz_ = meshDZ; dzCtx_ = dzCtx;
    if (crew_) crew_->SetArrival(_V(kArrivalX, kArrivalY, kArrivalZ), _V(1, 0, 0));   // up the lift: in the airlock cell, facing inboard
    for (int i = 0; i < kSeatN; i++) {                                   // the seat runs kSeatTravel along its facing to its console
        static UINT grp[4][12]; UINT n = 0;
        for (int k = 0; k < 12; k++) if (kSeatGroups[i][k] >= 0) grp[i][n++] = UINT(kSeatGroups[i][k]);
        anim_[i] = v_->CreateAnimation(0.0);
        v_->AddAnimationComponent(anim_[i], 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, grp[i], n, _V(kSeats[i].fx * kSeatTravel[i], 0, kSeats[i].fz * kSeatTravel[i])));
        if (i == 0) {                                                    // the commander's own travel at the console (his slider):
            const double span = kSeatAdjMax - kSeatAdjMin;               // the mesh is built at offset 0 = state -min/span
            adjAnim_ = v_->CreateAnimation(-kSeatAdjMin / span);
            v_->AddAnimationComponent(adjAnim_, 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, grp[0], n, _V(kSeats[0].fx * span, 0, kSeats[0].fz * span)));
            adjPos_ = adjTarget_ = -kSeatAdjMin / span;
            static UINT up[12]; UINT nu = 0;                             // its height: all but the base (the sled, the pedestal's outer tube)
            for (UINT k = 0; k < n; k++) if (int(grp[0][k]) != kSeat0Base) up[nu++] = grp[0][k];
            const double hs = kSeatHgtMax - kSeatHgtMin;
            hgtAnim_ = v_->CreateAnimation(-kSeatHgtMin / hs);               // the mesh is built at height 0
            v_->AddAnimationComponent(hgtAnim_, 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, up, nu, _V(0, hs, 0)));
            hgtPos_ = hgtTarget_ = (kSeatHgtStart - kSeatHgtMin) / hs;
        }
    }
    {   // the cabin's panel rides with the cabin (arm out along -x, down along -y); door B's two leaves slide to the middle
        static UINT ride[16]; UINT n = 0;
        for (int k = 0; k < kCabRideCount && n < 16; k++) if (kCabRide[k] >= 0) ride[n++] = UINT(kCabRide[k]);
        cabOutAnim_ = v_->CreateAnimation(0.0);
        v_->AddAnimationComponent(cabOutAnim_, 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, ride, n, _V(-tantra::mesh::kLockOut, 0, 0)));
        cabDownAnim_ = v_->CreateAnimation(0.0);
        v_->AddAnimationComponent(cabDownAnim_, 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, ride, n, _V(0, -tantra::mesh::kLockDrop, 0)));
        static UINT la[2], lb[2]; UINT na = 0, nb = 0;
        for (int k = 0; k < 2; k++) { if (kDoorBGroups[k] >= 0) la[na++] = UINT(kDoorBGroups[k]); if (kDoorBGroups[2 + k] >= 0) lb[nb++] = UINT(kDoorBGroups[2 + k]); }
        cabDoorAnim_ = v_->CreateAnimation(0.0);                         // the cabin's doors (they ride with the cabin too)
        static UINT ca[3], cb[1]; UINT nca = 0;
        for (int k = 0; k < 3; k++) if (kCabDoorA[k] >= 0) ca[nca++] = UINT(kCabDoorA[k]);
        if (nca) v_->AddAnimationComponent(cabDoorAnim_, 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, ca, nca, _V(0, 0, -kCabDoorTravel)));
        if (kCabDoorB >= 0) { cb[0] = UINT(kCabDoorB); v_->AddAnimationComponent(cabDoorAnim_, 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, cb, 1, _V(0, 0, kCabDoorTravel))); }
        doorBAnim_ = v_->CreateAnimation(0.0);
        if (na) v_->AddAnimationComponent(doorBAnim_, 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, la, na, _V(0, 0, kDoorBTravel)));
        if (nb) v_->AddAnimationComponent(doorBAnim_, 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, lb, nb, _V(0, 0, -kDoorBTravel)));
    }
    {   // the inner lift's cab: 0 = the lower stop, 1 = the technical level (the top stop)
        static_assert(kILiftStopN <= kILiftMax, "the inner lift has more stops than its arrays hold");
        static UINT ig[2 + kILiftMax]; UINT n = 0;
        for (int k = 0; k < 2; k++) if (kILiftGroups[k] >= 0) ig[n++] = UINT(kILiftGroups[k]);
        for (int k = 0; k < kILiftStopN; k++) if (kILiftSend[k] >= 0) ig[n++] = UINT(kILiftSend[k]);
        static UINT dg[kILiftMax], dn[kILiftMax];
        for (int k = 0; k < kILiftStopN; k++) {                          // the shaft doors: two leaves slide apart over the wall
            iliftDoorAnim_[k] = v_->CreateAnimation(0.0);
            if (kILiftDoor[k] >= 0) { dg[k] = UINT(kILiftDoor[k]); v_->AddAnimationComponent(iliftDoorAnim_[k], 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, &dg[k], 1, _V(0, 0, -kILiftDoorHw))); }
            if (kILiftDoorN[k] >= 0) { dn[k] = UINT(kILiftDoorN[k]); v_->AddAnimationComponent(iliftDoorAnim_[k], 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, &dn[k], 1, _V(0, 0, kILiftDoorHw))); }
        }
        iliftAnim_ = v_->CreateAnimation(0.0);
        if (n) v_->AddAnimationComponent(iliftAnim_, 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, ig, n, _V(0, kILiftStop[kILiftStopN - 1] - kILiftStop[0], 0)));
        iliftY_ = iliftTarget_ = kILiftStop[kILiftHome];                 // waiting at the living deck
        iliftInit_ = true;
    }
    mfds_.Init(v_, vcMesh_);
    for (int k = 0; k < kMfdCount; k++) mfds_.Add(kMfd[k].slot, kMfd[k].mode);
    mfdPlaces_ = kMfdCount;
    for (int k = 0; k < 2; k++) {                                        // the side glasses (variant 7) go down along their slant into their bays
        static UINT grp[2][2]; UINT n = 0;
        for (int j = 0; j < 2; j++) if (kSideDispGrp[k][j] >= 0) grp[k][n++] = UINT(kSideDispGrp[k][j]);
        sideAnim_[k] = v_->CreateAnimation(0.0);
        v_->AddAnimationComponent(sideAnim_[k], 0.0, 1.0, new MGROUP_TRANSLATE(vcMesh_, grp[k], n, _V(kSideRise[k][0], kSideRise[k][1], kSideRise[k][2])));
        sidePos_[k] = sideTarget_[k] = 0.0;                             // they start up, in РАБОТА (the user, 2026-10-05: «базовый режим - вот такой»)
        v_->SetAnimation(sideAnim_[k], sidePos_[k]);
    }
    static const int kPanelMfd[6] = {MFD_ORBIT, MFD_SURFACE, MFD_HSI, MFD_DOCKING, MFD_TRANSFER, MFD_MAP};
    for (int i = 0; i < 6; i++) mfds_.Add(0, kPanelMfd[i]);   // the 6 MFDs of the side panels, three each (drawn into them, no mesh slot)
    att_ = v_->CreateAttachment(false, _V(0, 0, 0), _V(0, 0, 1), _V(0, 1, 0), "OCINT");    // the walking body hangs on it
    fns_.Attach = cAttach; fns_.Ground = cGround; fns_.Walls = cWalls; fns_.Zone = nullptr; fns_.Gravity = nullptr;
    fns_.Count = cCount; fns_.Item = cItem; fns_.Use = cUse; fns_.Seat = cSeat; fns_.Viewing = cViewing;
    ext_.size = sizeof(OcInteriorExt); ext_.Origin = cOrigin; ext_.CanWalk = cCanWalk;
    ext_.Seated = cSeated;
    ext_.Click = cClick;
    ext_.SeatHands = cSeatHands;                                        // the seated person's hands on the ball, the armrest, the yoke
    ext_.OuterWalls = cOuterWalls;                                      // people walking outside bump into the cup feet                                                // every screen of the bridge is touch                                              // the person sits in the seat with the body (OrbiterCrew)
}

void TantraInterior::Register() {
    mfds_.Start();                                                       // the console MFDs work with or without OrbiterCrew
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
    mfds_.Shutdown();
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
    if (!capsule) CabinWalls(x, z, feet, radius, height);
    if (!capsule) ILiftWalls(x, z, feet, radius, height);           // the inner lift's doors are shut where the cab is not                // the lift cabin's walls (they move with it and carry the people in it)
    if (!capsule && panel_.DoorB && panel_.DoorB(panel_.ctx) > 0.5 && feet < kCabRoof) {   // door B shut: a wall across its opening
        const double x0 = -6.93, x1 = -6.62, z0 = kCabZ - kDoorBHw, z1 = kCabZ + kDoorBHw;
        const double cx = Clamp(x, x0, x1), cz = Clamp(z, z0, z1), dx = x - cx, dz = z - cz, d2 = dx * dx + dz * dz;
        if (d2 < radius * radius) {
            if (d2 > 1e-10) { const double d = std::sqrt(d2), k = (radius - d) / d; x += dx * k; z += dz * k; }
            else x = x < -6.85 ? x0 - radius : x1 + radius;
        }
    }
    if (capsule) {                                                       // the seats where they are now
        for (int it = 0; it < 2; it++)
            for (int i = 0; i < kSeatN; i++) {
                if (feet + kKnee >= kBridgeFloorY + kSeatBox[3] || feet + height <= kBridgeFloorY) continue;
                const double fx = kSeats[i].fx, fz = kSeats[i].fz, zm = .5 * (kSeatBox[1] + kSeatBox[2]), hz = .5 * (kSeatBox[2] - kSeatBox[1]);
                const double bx = SeatX(i) + fx * zm, bz = SeatZ(i) + fz * zm;   // the box along the facing: its axis-aligned bounds
                const double ax = std::fabs(fz) * kSeatBox[0] + std::fabs(fx) * hz, az = std::fabs(fx) * kSeatBox[0] + std::fabs(fz) * hz;
                const double x0 = bx - ax, x1 = bx + ax, z0 = bz - az, z1 = bz + az;
                const double cx = Clamp(x, x0, x1), cz = Clamp(z, z0, z1), dx = x - cx, dz = z - cz, d2 = dx * dx + dz * dz;
                if (d2 >= radius * radius) continue;
                if (d2 > 1e-10) { const double d = std::sqrt(d2), k = (radius - d) / d; x += dx * k; z += dz * k; }
                else { x = bx + fx * (hz + radius); z = bz + fz * (hz + radius); }   // inside: out to the front (one sits down in front of it)
            }
    }
    if (capsule) {                                                       // the drum: flat end discs and the cylindrical vault
        x = Clamp(x, -kBridgeHx + radius, kBridgeHx - radius);
        const bool door = std::fabs(x) <= kDoorHalfW - radius && feet + height <= kDoorTop;   // the exit at the back, on the centre line
        const double dy = feet + height - kBridgeAxisY, rr = kBridgeR - radius;
        const double lim = std::sqrt((std::max)(0.0, rr * rr - dy * dy));
        z = Clamp(z, door ? kDoorZ1 : kBridgeAxisZ - lim, kBridgeAxisZ + lim);
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
    if (!cap && t->panel_.Cabin) {                                       // the lift cabin's floor goes with the cabin
        double sx, sy; bool st, ag; t->CabinPose(sx, sy, st, ag);
        const double w = 0.08;
        if (x >= kCabX0 + sx + w && x <= kCabX1 + sx - w && z >= kCabZ - kCabHz + w && z <= kCabZ + kCabHz - w) {
            const double top = kCabFloor + sy;
            if (top <= y + stepUp + 1e-3 && top > best) { best = top; ok = 1; }
        }
    }
    if (!cap && x >= kILiftX0 && x <= kILiftX1 && z >= kILiftZ0 && z <= kILiftZ1) {   // the inner lift's cab floor
        const double top = t->iliftY_;
        if (top <= y + stepUp + 1e-3 && top > best) { best = top; ok = 1; }
    }
    if (ok && best < y - 0.5) return 0;                                  // no floor within a step down: no falling into a well or a shaft
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

int TantraInterior::cCount(void* c) { return kSeatN + (Self(c)->crew_ ? Self(c)->crew_->CrewItemCount() : 0) + (Self(c)->panel_.Press ? 6 : 0) + (Self(c)->panel_.Press ? 1 : 0) + 2 * kILiftStopN; }   // + the inner lift's items: a call and a send per stop (before the cabin's way out, which is last)   // (the screens are touch: cClick)

int TantraInterior::cItem(void* c, int i, OcItem* out) {
    const TantraInterior* t = Self(c);
    if (t->panel_.Press && i == cCount(c) - 1) return t->CabExitItem(out);   // the last item: the way out of the cabin
    {
        const int b0 = cCount(c) - (t->panel_.Press ? 1 : 0) - 2 * kILiftStopN;
        if (i >= b0 && i < b0 + 2 * kILiftStopN) return t->ILiftItem(i - b0, out);     // the inner lift
    }
    const int nCrew = t->crew_ ? t->crew_->CrewItemCount() : 0;
    const int nPanel = t->panel_.Press ? 6 : 0;
    if (false) {                                                          // (the panel spots: touch now, see cClick)
        const int k = i - kSeatN - nCrew - nPanel - kMfdCount * 15;
        if (k < 0 || k >= int(t->pbtn_.size()) || !out) return 0;
        const PanelBtn& b = t->pbtn_[k];
        *out = OcItem{}; out->id = kPanelBtnId0 + k; out->kind = OC_BUTTON; out->pos = b.pos; out->dir = -b.n; out->radius = b.r;
        snprintf(out->label, sizeof out->label, "пульт");
        return 1;
    }
    if (i >= kSeatN + nCrew) return t->PanelItem(i - kSeatN - nCrew, out);       // the lift panel
    if (i >= kSeatN && t->crew_) return t->crew_->CrewItem(i - kSeatN, out);    // the lift, the way in
    if (i < 0 || i >= kSeatN || !out) return 0;
    out->id = i; out->kind = (i == 0) ? OC_HELM : OC_SEAT;   // one person steers, the commander: there V shows the ship
    out->pos = _V(kSeats[i].x, kBridgeFloorY, kSeats[i].z);
    out->dir = _V(kSeats[i].fx, 0, kSeats[i].fz);                       // the seat's facing (the crew's are turned to their bays)
    out->radius = 1.0;
    snprintf(out->label, sizeof out->label, "%s", kSeatLabel[i]);
    return 1;
}

void TantraInterior::cUse(void* c, int id, int personId) {
    TantraInterior* t = Self(c);
    oapiWriteLogV("Tantra interior: person %d uses item %d", personId, id);
    if (id >= 300 && id < 300 + 2 * kILiftStopN) {                      // the inner lift: call (300..) or send (300 + N..)
        const int st = (id - 300) % kILiftStopN;
        if (std::fabs(t->iliftTarget_ - kILiftStop[st]) > 0.01 && t->iliftV_ == 0.0) {
            t->iliftTarget_ = kILiftStop[st];
            oapiWriteLogV("Tantra interior: inner lift to stop %d", st);
        }
        return;
    }
    if (id >= kPanelBtnId0 && id < kPanelBtnId0 + int(t->pbtn_.size())) {
        const PanelBtn& b = t->pbtn_[id - kPanelBtnId0];
        if (t->console_.Click) t->console_.Click(t->console_.ctx, b.area, b.mx, b.my);
        t->panelT_ = 0.0;                                                // redraw at once
        return;
    }
    if (id >= kMfdId0 && id < kMfdId0 + kMfdCount * 16) { t->mfds_.Press((id - kMfdId0) / 16, (id - kMfdId0) % 16); return; }
    if (id >= kPanelId0 && id < kPanelId0 + 6) {
        if (!t->panel_.Press) return;
        t->panel_.Press(t->panel_.ctx, id - kPanelId0, personId);
        return;
    }
    if (t->crew_ && t->crew_->CrewUse(id, personId)) return;
    // (the seats: OrbiterCrew seats the person itself - OcInteriorExt::Seated; the old way, the body leaving into the seat and
    // the ship taking the focus, is gone: the user's rule)
}

void TantraInterior::cSeat(void* c, int id, VECTOR3* pos, VECTOR3* dir) {
    if (id < 0 || id >= kSeatN) return;
    if (pos) *pos = _V(Self(c)->SeatX(id), kBridgeFloorY + kHips + (id == 0 ? Self(c)->CmdSeatHeight() : 0.0), Self(c)->SeatZ(id));   // where the seat is now (it moves)
    if (dir) *dir = _V(kSeats[id].fx, 0, kSeats[id].fz);
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
    return (id && api_.ShipOf && api_.ShipOf(id) == v_->GetHandle()) ? f : nullptr;
}

bool TantraInterior::ViewerInBridge() const {
    if (!v_ || !oapiCameraInternal()) return false;
    VECTOR3 g, p;
    oapiCameraGlobalPos(&g);
    v_->Global2Local(g, p);
    return InCapsule(p.x, p.y, p.z - DZ());   // the drum turns about its axis: the test does not depend on its turn
}

// A screen in the view: the angle from the camera's axis to the screen's centre within the view's half diagonal (the window
// wider than 16:9 allowed) plus the screen's own angular radius and 5 deg - a screen at the edge still counts. The side glasses
// sunk (ЛЕНТА / ВНИЗ) are taken where they are.
bool TantraInterior::ScreenInView(int k) const {
    if (!v_ || k < 0 || k >= kTouchCount) return true;
    VECTOR3 g, o; oapiCameraGlobalPos(&g); v_->Global2Local(g, o); o.z -= DZ();
    MATRIX3 Rc, Rs; oapiCameraRotationMatrix(&Rc); v_->GetRotationMatrix(Rs);
    const VECTOR3 fwd = tmul(Rs, mul(Rc, _V(0, 0, 1)));                 // the camera's axis in the interior frame
    const TouchPlace& q = kTouch[k];
    VECTOR3 c = _V(q.c[0], q.c[1], q.c[2]);
    if (k < 2) { const double* r = kSideRise[k]; c += _V(r[0], r[1], r[2]) * sidePos_[k]; }
    const VECTOR3 to = c - o;
    const double dist = length(to);
    if (dist < 0.05) return true;
    const double ap = std::tan((std::max)(0.05, oapiCameraAperture()));
    const double half = std::atan(ap * std::sqrt(1.0 + 2.4 * 2.4));      // the half diagonal (up to a 2.4 : 1 window)
    const double rad = std::atan(0.5 * std::hypot(q.w, q.h) / dist);
    const double ang = std::acos((std::max)(-1.0, (std::min)(1.0, dotp(to, fwd) / dist)));
    return ang < half + rad + 5.0 * RAD;
}

void TantraInterior::cOrigin(void* c, VECTOR3* o) { *o = _V(0, 0, Self(c)->DZ()); }

void TantraInterior::cSeated(void* c, int seatId, int personId, int on) {
    TantraInterior* t = Self(c);
    if (seatId < 0 || seatId >= kSeatN) return;
    // the camera stays the person's (the user's rule): the seat only runs to the console (on) or back from it (off)
    if (on) { t->seat_ = seatId; t->person_ = personId; t->seatTarget_[seatId] = 1.0; }
    else {
        t->seatTarget_[seatId] = 0.0; if (t->person_ == personId) { t->seat_ = -1; t->person_ = 0; }
        // (she gets up behind the seat: OrbiterCrew puts her there itself, against the walls - the user: «За кресло должен встать экипаж»)
    }
    oapiWriteLogV("Tantra interior: person %d %s the seat %d", personId, on ? "sits in" : "leaves", seatId);
}

// The commander's hands (OrbiterCrew reaches them with the arm and holds them, OcInteriorExt::SeatHands): the left palm on
// its armrest behind the cursor unit, the fingers forward; the right on its armrest's front end, ahead of the ХОД / ВЫСОТА strips; with the yoke out
// both on its horns, the thumbs up, the fingers across the horn along the arm (OrbiterCrew turns the hand round the horn to
// her forearm). The ball and the rest go with the seat (its travel, adjustment, height); the horns with the hub (TantraYoke).
void TantraInterior::cSeatHands(void* c, int seatId, int, OcHand* left, OcHand* right) {
    TantraInterior* t = Self(c);
    if (seatId != 0 || !left || !right || left->size < int(sizeof(OcHand)) || right->size < int(sizeof(OcHand))) return;
    const VECTOR3 f = _V(kSeats[0].fx, 0, kSeats[0].fz), rt = _V(kSeats[0].fz, 0, -kSeats[0].fx), up = _V(0, 1, 0);
    const VECTOR3 mv = f * t->CmdSeatOffset() + up * t->CmdSeatHeight();
    const double elbowMin = kHandRest[1] + mv.y + 0.05;               // the elbows over the armrests' tops (+ the forearm)
    auto put = [elbowMin](OcHand* h, int what, const VECTOR3& pos, const VECTOR3& palm, const VECTOR3& fwd, int grip, double r) {
        h->on = 1; h->what = what; h->pos = pos; h->palm = palm; h->fwd = fwd; h->grip = grip; h->radius = r; h->elbowMinY = elbowMin;
    };
    if (t->yokeE_ >= 0.999) {                                           // the yoke out: on its horns
        const VECTOR3 hips = _V(t->SeatX(0), kBridgeFloorY + kHips + t->CmdSeatHeight(), t->SeatZ(0));
        for (int s = 0; s < 2; ++s) {
            const double sx = s == 0 ? -1 : 1, r = 0.020;
            const VECTOR3 gt = t->hubH_ + t->hubX_ * (sx * 0.243) + t->hubY_ * -0.058, gb = t->hubH_ + t->hubX_ * (sx * 0.268) + t->hubY_ * -0.168;
            const VECTOR3 a = unit(gb - gt), gm = gt + (gb - gt) * 0.35;   // high on the grip: the thumb at the buttons by the bend
            // the user's own grip (his video, 2026-10-05): the handle through the fist, the palm forward and in toward the
            // hub, the back of the hand to him, the fingers round the front, the thumb on top
            VECTOR3 n = t->hubZ_ * -0.95 + t->hubX_ * (-sx * 0.3);
            n = unit(n - a * dotp(n, a));
            const VECTOR3 fw = s == 0 ? crossp(a, n) : crossp(n, a);
            (void)hips;
            put(s == 0 ? left : right, 10 + s, gm - n * r, n, fw, OC_GRIP_HANDLE, r);
        }
        return;
    }
    // the left palm on its armrest just behind the cursor unit, the fingertips short of it: the unit stays in sight (the user,
    // 2026-10-05: «ПУЛЬТ КРЕСЛА НЕ ВИДНО» - the hand on the ball hid it under the forearm)
    put(left, 1, _V(-kHandRest[0], kHandRest[1], kSeats[0].z + 0.0) + mv, -up, f, OC_GRIP_FLAT, 0.0);
    put(right, 2, _V(kHandRest[0], kHandRest[1], kHandRest[2]) + mv, -up, f, OC_GRIP_FLAT, 0.0);
}

int TantraInterior::cCanWalk(void* c, char* reason, int n) {
    const TantraInterior* t = Self(c);
    return t->canWalk_ ? t->canWalk_(t->canCtx_, reason, n) : 1;
}

bool TantraInterior::StandUp(int seat) {
    if (!reg_ || seat < 0 || seat >= kSeatN) return false;
    standSeat_ = seat;                                                   // done in Step(): never inside the key handler (Orbiter crashed)
    return true;
}

double TantraInterior::SeatX(int i) const { return kSeats[i].x + kSeats[i].fx * (kSeatTravel[i] * seatPos_[i] + (i == 0 ? AdjOffset() : 0.0)); }
double TantraInterior::SeatZ(int i) const { return kSeats[i].z + kSeats[i].fz * (kSeatTravel[i] * seatPos_[i] + (i == 0 ? AdjOffset() : 0.0)); }
double TantraInterior::AdjOffset() const { return kSeatAdjMin + (kSeatAdjMax - kSeatAdjMin) * adjPos_; }

// (DrawAdj, the seat's panel: TantraSeatCmd.cpp)
void TantraInterior::SideFold(int k, int state) { if (k >= 0 && k < 2) sideTarget_[k] = state <= 0 ? 0.0 : state == 1 ? kSideRibbon : 1.0; }
bool TantraInterior::SideDisplayUsable(int k) const {
    return k >= 0 && k < 2 && sidePos_[k] == sideTarget_[k] && sideTarget_[k] < 1.0;
}
int TantraInterior::SideFoldState(int k) const { return k < 0 || k > 1 ? 0 : sideTarget_[k] <= 0.0 ? 0 : sideTarget_[k] < 1.0 ? 1 : 2; }

void TantraInterior::CabinPose(double& sx, double& sy, bool& stowed, bool& atGround) const {
    double out = 0.0, down = 0.0; int ag = 0;
    if (panel_.Cabin) panel_.Cabin(panel_.ctx, &out, &down, &ag);
    sx = -tantra::mesh::kLockOut * out; sy = -tantra::mesh::kLockDrop * down;
    stowed = out <= 0.001 && down <= 0.001; atGround = ag != 0;
}

void TantraInterior::CabinWalls(double& x, double& z, double feet, double radius, double height) const {
    if (!panel_.Cabin) return;
    double sx, sy; bool stowed, ag; CabinPose(sx, sy, stowed, ag);
    const double y0 = kCabFloor + sy - 0.05, y1 = kCabRoof + sy;
    if (feet + height <= y0 || feet >= y1) return;
    const double X0 = kCabX0 + sx, X1 = kCabX1 + sx, Z0 = kCabZ - kCabHz, Z1 = kCabZ + kCabHz, w = 0.08;
    struct B { double x0, x1, z0, z1; } b[7];
    int n = 0;
    b[n++] = {X0, X1, Z0, Z0 + w};                                      // aft end
    b[n++] = {X0, X1, Z1 - w, Z1};                                      // forward end
    b[n++] = {X0, X0 + w, Z0, Z1};                                      // outboard wall
    b[n++] = {X1 - w, X1, Z0, kCabZ - kCabDoorHw};                      // hull-side wall round the door
    b[n++] = {X1 - w, X1, kCabZ + kCabDoorHw, Z1};
    if ((!stowed && !ag) || (panel_.CabDoor && panel_.CabDoor(panel_.ctx) > 0.5))
        b[n++] = {X1 - 0.17, X1 - 0.14, kCabZ - kCabDoorHw, kCabZ + kCabDoorHw};   // the cabin's doors shut (and a guard while it moves)
    b[n++] = {X1 - 0.13, X1 - 0.09, kCabZ + 0.66, kCabZ + 1.4};         // the cabin's own door leaf, slid open inside the hull-side wall
    for (int it = 0; it < 3; it++) {
        bool moved = false;
        for (int i = 0; i < n; i++) {
            const double cx = Clamp(x, b[i].x0, b[i].x1), cz = Clamp(z, b[i].z0, b[i].z1);
            const double dx = x - cx, dz = z - cz, d2 = dx * dx + dz * dz;
            if (d2 >= radius * radius) continue;
            if (d2 > 1e-10) { const double d = std::sqrt(d2), k = (radius - d) / d; x += dx * k; z += dz * k; }
            else {
                const double l = x - b[i].x0, r = b[i].x1 - x, f = z - b[i].z0, a = b[i].z1 - z, mn = (std::min)((std::min)(l, r), (std::min)(f, a));
                if (mn == l) x = b[i].x0 - radius; else if (mn == r) x = b[i].x1 + radius; else if (mn == f) z = b[i].z0 - radius; else z = b[i].z1 + radius;
            }
            moved = true;
        }
        if (!moved) break;
    }
}

int TantraInterior::CabExitItem(OcItem* out) const {
    if (!out) return 0;
    double sx, sy; bool st, ag; CabinPose(sx, sy, st, ag);
    if (!ag) return 0;
    *out = OcItem{};
    out->id = kPanelId0 + 5;                                             // the same action as the cabin's OUT button
    out->kind = OC_DOOR;                                                 // F, like the way in
    out->pos = _V(kCabX1 - 0.3 + sx, kCabFloor + sy, kCabZ);
    out->dir = _V(1, 0, 0);
    out->radius = 1.5;
    snprintf(out->label, sizeof out->label, "выйти на грунт");
    return 1;
}

int TantraInterior::PanelItem(int k, OcItem* out) const {
    if (!out || !panel_.Press || k < 0 || k > 5) return 0;
    *out = OcItem{};
    out->id = kPanelId0 + k;
    out->kind = OC_BUTTON;                                               // push buttons: pressed with the MOUSE
    out->dir = _V(0, 0, -1);                                             // both panels face aft (the lift zone, the inside of the cabin)
    out->radius = k < 3 ? kZoneCapR : kCabCapR;                          // the cap itself (40 mm / 60 mm square)
    if (k < 3) out->pos = _V(kPanelBtnX[k], kPanelBtnCY, kPanelBtnCZ);
    else {                                                               // the cabin's panel: where the cabin is now
        double sx, sy; bool st, ag; CabinPose(sx, sy, st, ag);
        out->pos = _V(kCabBtn[k - 3][0] + sx, kCabBtn[k - 3][1] + sy, kCabBtn[k - 3][2]);
    }
    out->label[0] = 0;
    if (panel_.Label) panel_.Label(panel_.ctx, k, out->label, int(sizeof out->label));
    return 1;
}

// The cabin carries the people standing in it (OrbiterCrew ocCarry, every frame while it moves); at the bottom they step out
// with the panel's "out to the ground" (ocExitTo). Riders are taken when the cabin starts from the cell or from the ground.
void TantraInterior::CarryRiders() {
    if (!panel_.Cabin || !api_.Carry || !api_.PersonOfBody || !api_.ShipOf) return;
    double sx, sy; bool st, ag; CabinPose(sx, sy, st, ag);
    const bool moving = !st && !ag;
    if (moving && !wasMoving_) {                                         // just started: who stands in the cabin
        nRiders_ = 0;
        const double dz = DZ();
        MATRIX3 Rs; v_->GetRotationMatrix(Rs);
        for (DWORD i = 0; i < oapiGetVesselCount() && nRiders_ < 8; i++) {
            OBJHANDLE h = oapiGetVesselByIndex(i);
            const int id = api_.PersonOfBody(h);
            if (!id || api_.ShipOf(id) != v_->GetHandle()) continue;
            double x, feet, z; VECTOR3 f;
            VECTOR3 ft; double hdg = 0.0;
            if (api_.InteriorPos) {                                          // OrbiterCrew knows the feet in the interior frame
                if (!api_.InteriorPos(id, &ft, &hdg)) continue;
                x = ft.x; feet = ft.y; z = ft.z; f = _V(std::sin(hdg), 0, std::cos(hdg));
            } else {                                                         // older CrewMember: from the body's position
                VECTOR3 g, l; oapiGetGlobalPos(h, &g); v_->Global2Local(g, l);
                x = l.x; feet = l.y - 0.93; z = l.z - dz;                    // the body's origin is the pelvis, the feet 0.93 below
                MATRIX3 Rb; oapiGetVesselInterface(h)->GetRotationMatrix(Rb);
                f = tmul(Rs, mul(Rb, _V(0, 0, 1))); f.y = 0.0;
            }
            const double lx0 = kCabX0 + lastSx_, lx1 = kCabX1 + lastSx_, fl = kCabFloor + lastSy_;
            if (x < lx0 || x > lx1 || z < kCabZ - kCabHz || z > kCabZ + kCabHz || std::fabs(feet - fl) > 0.5) continue;
            const double n = length(f);
            riders_[nRiders_++] = {id, x - lastSx_, z, n > 1e-6 ? f / n : _V(1, 0, 0)};
            oapiWriteLogV("Tantra interior: person %d rides the lift cabin", id);
        }
    }
    if (moving)
        for (int i = 0; i < nRiders_; i++) {
            const VECTOR3 pos = _V(riders_[i].rx + sx, kCabFloor + sy, riders_[i].z);
            api_.Carry(riders_[i].id, v_->GetHandle(), &pos, &riders_[i].dir);
        }
    else nRiders_ = 0;
    wasMoving_ = moving; lastSx_ = sx; lastSy_ = sy;
}

void TantraInterior::OnVisual(VISHANDLE vis) {
    mfds_.OnVisualCreated(vis);
    if (console_.Tex) {                                                  // the console's panels: the very texture the ship draws into
        DEVMESHHANDLE dm = v_->GetDevMesh(vis, vcMesh_);
        SURFHANDLE tx = console_.Tex(console_.ctx);
        const bool ok = dm && tx && oapiSetTexture(dm, kPanelSlot, tx);
        oapiWriteLogV("Tantra interior: console panels %s (%d spots, %d areas)", ok ? "bound" : "NOT bound", int(pbtn_.size()), int(pareas_.size()));
        panelT_ = 0.0;
    }
    mesh_ = v_->GetDevMesh(vis, vcMesh_);
    for (int& c : capShown_) c = -1;
    if (!status_) status_ = oapiCreateSurfaceEx(512, 336, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_NOMIPMAPS);
    if (!fontB_) { fontB_ = oapiCreateFont(34, true, "Arial Cyr", FONT_BOLD); fontS_ = oapiCreateFont(23, true, "Arial Cyr"); }   // "Arial Cyr": D3D9Client makes Western-charset fonts, this name maps to the Cyrillic set
    if (mesh_ && status_) {
        const bool ok = oapiSetTexture(mesh_, kLiftStatusSlot, status_);
        oapiWriteLogV("Tantra interior: lift status screen %s", ok ? "bound" : "NOT bound");
    }
    if (!cabScr_) cabScr_ = oapiCreateSurfaceEx(512, 256, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_NOMIPMAPS);
    if (mesh_ && cabScr_) {
        const bool ok = oapiSetTexture(mesh_, kCabScreenSlot, cabScr_);
        oapiWriteLogV("Tantra interior: lift cabin screen %s", ok ? "bound" : "NOT bound");
    }
    if (!adjSurf_) adjSurf_ = oapiCreateSurfaceEx(kSeatPanelW, kSeatPanelH, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_NOMIPMAPS);
    if (mesh_ && adjSurf_) oapiSetTexture(mesh_, kSeatAdjSlot, adjSurf_);
    adjShown_ = -1.0; panelKey_ = -1.0; spotShown_ = ballShown_ = uvtShown_ = -1; yokeKey_[0] = -1e9; quadKey_[0] = -1e9;
    statusT_ = 0.0; cabScrT_ = 0.0;
}

void TantraInterior::DrawCabStatus() {
    if (!cabScr_ || !panel_.CabStatus || !glyphs_.Ok()) return;
    char buf[640] = {0}; panel_.CabStatus(panel_.ctx, buf, int(sizeof buf));
    DrawTable(cabScr_, buf, 512, 256);
}

int TantraInterior::CabinPeople(int* ids, int maxN) const {
    if (!panel_.Cabin || !api_.PersonOfBody || !api_.ShipOf || !api_.InteriorPos) return 0;
    double sx, sy; bool st, ag; CabinPose(sx, sy, st, ag);
    int n = 0;
    for (DWORD i = 0; i < oapiGetVesselCount() && n < maxN; i++) {
        const int id = api_.PersonOfBody(oapiGetVesselByIndex(i));
        if (!id || api_.ShipOf(id) != v_->GetHandle()) continue;
        VECTOR3 ft; double hdg = 0.0;
        if (!api_.InteriorPos(id, &ft, &hdg)) continue;
        if (ft.x < kCabX0 + sx || ft.x > kCabX1 + sx || ft.z < kCabZ - kCabHz || ft.z > kCabZ + kCabHz || std::fabs(ft.y - (kCabFloor + sy)) > 0.5) continue;
        ids[n++] = id;
    }
    return n;
}

int TantraInterior::SuitWorn(int id) const { return api_.SuitWorn ? api_.SuitWorn(id) : -1; }

bool TantraInterior::PersonName(int id, char* out, int n) const {
    OcInfo in{};
    if (!api_.Info || !api_.Info(id, &in)) return false;
    snprintf(out, size_t(n), "%s", in.name);
    return true;
}

void TantraInterior::DrawTable(SURFHANDLE surf, char* buf, int W, int H) {
    // A technical display: near-black, a small grey header with the panel's code, a hairline, the parameters in two columns
    // (the key grey, the value white; '+' normal / ready = cyan, '*' in progress = amber, '!' fault = red), then the messages.
    oapiClearSurface(surf, 0xFF0B0D0F);
    oapi::Sketchpad* skp = oapiGetSketchpad(surf);
    if (!skp) return;
    if (!rule_) rule_ = oapiCreatePen(1, 1, 0x50585E);
    const double px = H >= 300 ? 17.0 : 15.0, row = px * 1.85, xv = W * 0.47;
    double y = 10.0; int line = 0; bool msgs = false;
    auto colOf = [](const char*& t) { int c = 2; if (*t == '!') { c = 3; t++; } else if (*t == '+') { c = 0; t++; } else if (*t == '*') { c = 1; t++; } else if (*t == ' ') t++; return c; };
    for (char* p = buf; p && *p; line++) {
        char* nl = std::strchr(p, '\n'); if (nl) *nl = 0;
        char* tab = std::strchr(p, '\t'); if (tab) *tab = 0;
        if (line == 0) {                                                 // the header: the panel's name left, its code right
            glyphs_.Draw(skp, 14, y, p, 2, 6, px * 0.9);
            if (tab) glyphs_.Draw(skp, W - 14 - px * 0.6 * double(std::strlen(tab + 1)), y, tab + 1, 2, 6, px * 0.9);
            y += px * 1.6;
            skp->SetPen(rule_); skp->Line(10, int(y), W - 10, int(y));
            y += px * 0.9;
        } else if (tab) {                                                // a parameter
            glyphs_.Draw(skp, 16, y, p, 2, 6, px);
            const char* v = tab + 1; const int cv = colOf(v);
            glyphs_.Draw(skp, xv, y, v, 2, cv, px);
            y += row;
        } else if (*p) {                                                 // the messages under a hairline
            if (!msgs) { msgs = true; skp->SetPen(rule_); skp->Line(10, int(y - px * 0.45), W - 10, int(y - px * 0.45)); }
            const char* t = p; const int cv = colOf(t);
            glyphs_.Draw(skp, 16, y, t, 2, cv, px);
            y += row;
        }
        p = nl ? nl + 1 : nullptr;
    }
    oapiReleaseSketchpad(skp);
}

void TantraInterior::DrawStatus() {
    if (!status_ || !panel_.Status || !glyphs_.Ok()) return;
    char buf[640] = {0}; panel_.Status(panel_.ctx, buf, int(sizeof buf));
    DrawTable(status_, buf, 512, 336);
}


bool TantraInterior::PanelPoint(int panel, double tx, double ty, VECTOR3* pos, VECTOR3* n) const {
    for (int i = 0; i < kPanelPieceCount; i++) {
        const PanelPiece& q = kPanelPieces[i];
        if (q.panel != panel || tx < q.tx0 || tx > q.tx1 || ty < q.ty0 || ty > q.ty1) continue;
        const double u = ((tx - q.tx0) / double(q.tx1 - q.tx0) - .5) * q.w, v = (.5 - (ty - q.ty0) / double(q.ty1 - q.ty0)) * q.h;
        *pos = _V(q.c[0] + q.ex[0] * u + q.up[0] * v + q.n[0] * .004, q.c[1] + q.ex[1] * u + q.up[1] * v + q.n[1] * .004,
                  q.c[2] + q.ex[2] * u + q.up[2] * v + q.n[2] * .004);
        *n = _V(q.n[0], q.n[1], q.n[2]);
        return true;
    }
    return false;
}

// The panels' areas on the console: which are redrawn, and where one can click (as the 2D panels register them).
void TantraInterior::BuildPanelButtons() {
    using namespace tantra::panel;
    pbtn_.clear(); pareas_.clear();
    static const int kClick[] = {A_LEVER, A_SEL_PLAN, A_SEL_ANA, A_PODS_AFT, A_PODS_DOWN, A_STOP, A_TRAPS, A_TRAPSEL, A_GLIM, A_GSTEP,
                                 A_OVERRIDE, A_AIRLOCK, A_EVA, A_CREWSEL, M_SEL_PLAN, M_SEL_ANA, M_START, M_STOP, L_GEAR, L_SET_LEVEL,
                                 L_SET_STAND, L_ERECT, L_PORT, L_CRESTS, L_PODS_AFT, L_PODS_DOWN, L_HANGAR, L_ROVERS, L_AIRLOCK, L_EVA,
                                 L_CREWSEL, L_PT_LIFT, L_PT_LOAD, L_PT_DROP, L_PT_STOP};
    static const int kMfdAreas[] = {M_MFD2_L, M_MFD2_R, M_MFD2_B, M_MFD0_L, M_MFD0_R, M_MFD0_B, M_MFD1_L, M_MFD1_R, M_MFD1_B,
                                    M_MFD3_L, M_MFD3_R, M_MFD3_B};
    const double mpp = kPanelPieceCount ? kPanelPieces[0].w / double(kPanelPieces[0].tx1 - kPanelPieces[0].tx0) : .001;
    for (int a = 0; a < A_COUNT; a++) {
        const int* r = kArea[a];
        VECTOR3 p, n;
        const bool onPiece = PanelPoint(kAreaPanel[a], .5 * (r[0] + r[2]), .5 * (r[1] + r[3]), &p, &n);
        bool skip = false;
        for (int m : kMfdAreas) if (a == m) skip = true;
        if (skip) continue;
        pareas_.push_back(a);                                            // (the commander's screens may show it)
        if (!onPiece) continue;
        bool click = false;
        for (int c : kClick) if (a == c) click = true;
        if (!click) continue;
        auto add = [&](double tx, double ty, double hw, double hh) {    // a spot: texture point -> the console
            VECTOR3 q, nn;
            if (!PanelPoint(kAreaPanel[a], tx, ty, &q, &nn)) return;
            pbtn_.push_back({a, int(tx - r[0]), int(ty - r[1]), q, nn, (std::max)(.008, .45 * (std::min)(hw, hh) * mpp)});
        };
        const double cy = .5 * (r[1] + r[3]), w = r[2] - r[0], h = r[3] - r[1];
        if (a == A_LEVER) for (int i = 0; i < 4; i++) add(kLeverX[i], cy, 30, h);
        else if (a == A_TRAPS) for (int i = 0; i < 4; i++) add(kTrapX[i] + .5 * kTrapW, cy, kTrapW, h);
        else if (a == A_EVA || a == L_EVA || a == A_CREWSEL || a == L_CREWSEL) { add(r[0] + .25 * w, cy, .5 * w, h); add(r[0] + .75 * w, cy, .5 * w, h); }
        else add(.5 * (r[0] + r[2]), cy, w, h);
    }
}

// A left click (OrbiterCrew: the ray from the camera through the cursor, interior frame): the touch screens. The nearest
// screen the ray meets, within reach of the person's head, takes it; u right, v down over the screen.
int TantraInterior::cClick(void* c, const VECTOR3* o, const VECTOR3* d, int personId) {
    TantraInterior* t = Self(c);
    VECTOR3 head = *o; bool haveHead = false;
    if (t->api_.InteriorPos) { VECTOR3 ft; double hdg; if (t->api_.InteriorPos(personId, &ft, &hdg)) { head = ft + _V(0, 1.45, 0); haveHead = true; } }
    // with the light spot on the seated commander reaches every screen (the spot shows where): no reach limit then
    const bool anyDist = t->spotOn_ && t->seat_ == 0 && personId == t->person_;
    Pick best;
    t->PickScreens(*o, *d, haveHead && !anyDist ? &head : nullptr, best);
    // the seat's controls: the strips (5 ХОД, 6 ВЫСОТА) on the left side console, the cursor unit's key plate (7) and its ball (8)
    // riding with the seat
    const VECTOR3 f = _V(kSeats[0].fx, 0, kSeats[0].fz), rt = _V(kSeats[0].fz, 0, -kSeats[0].fx);
    const VECTOR3 mv = f * t->CmdSeatOffset() + _V(0, t->CmdSeatHeight(), 0);
    int sk = -1; double su = 0, sv = 0, st = best.kind >= 0 ? best.t : 1e9;
    auto flat = [&](int kind, const double* c0, double w, double dd, bool onSeat) {   // a plate: u across, v from the front
        const VECTOR3 C = _V(c0[0], c0[1], c0[2]) + (onSeat ? mv : _V(0, 0, 0)), N = _V(0, 1, 0);
        const double den = dotp(*d, N);
        if (den > -1e-6) return;
        const double tt = dotp(C - *o, N) / den;
        if (tt <= 0 || tt >= st) return;
        const VECTOR3 q = *o + *d * tt - C;
        const double u = dotp(q, rt) / w + .5, v = .5 - dotp(q, f) / dd;           // true places on the plate / the strip
        const double mu = 0.008 / w, mv_ = 0.008 / dd;                                 // taken 8 mm round them, clamped to the edge
        if (u < -mu || u > 1 + mu || v < -mv_ || v > 1 + mv_) return;
        sk = kind; su = Clamp(u, 0.0, 1.0); sv = Clamp(v, 0.0, 1.0); st = tt;
    };
    flat(5, kSeatAdjC, kSeatStripWid, kSeatStripLen, false);              // the strips: on the left side console (fixed)
    flat(6, kSeatHgtC, kSeatStripWid, kSeatStripLen, false);
    flat(7, kCurPlateC, kCurPlateW, kCurPlateD, true);
    if (t->yokeE_ > 0.9) {                                              // the yoke's hub (it moves): its keys' face (9), its screen (10)
        auto hub = [&](int kind, const double* r5) {
            const VECTOR3 C = t->hubH_ + t->hubX_ * r5[0] + t->hubY_ * r5[1] + t->hubZ_ * r5[2], N = t->hubZ_;
            const double den = dotp(*d, N);
            if (den > -1e-6) return;
            const double tt = dotp(C - *o, N) / den;
            if (tt <= 0 || tt >= st) return;
            const VECTOR3 q = *o + *d * tt - C;
            const double u = dotp(q, t->hubX_) / r5[3] + .5, v = .5 - dotp(q, t->hubY_) / r5[4];
            if (u < 0 || u > 1 || v < 0 || v > 1) return;
            sk = kind; su = u; sv = v; st = tt;
        };
        hub(9, kHubKeys); hub(10, kHubScr);
        const VECTOR3 Bt = t->hubH_ + t->hubX_ * kHubUvt[0] + t->hubY_ * kHubUvt[1] + t->hubZ_ * kHubUvt[2], oc = *o - Bt;   // УВТ (11)
        const double r = kHubUvt[3] * 1.8, bb = dotp(oc, *d), disc = bb * bb - (dotp(oc, oc) - r * r);
        if (disc >= 0) { const double tt = -bb - std::sqrt(disc); if (tt > 0 && tt < st) { sk = 11; st = tt; } }
    }
    for (int k = 0; k < 2; ++k) {                                        // the levers' grips (14, 15): held, they follow the cursor
        const VECTOR3 oc = *o - t->LeverGrip(k);
        const double r = 0.04, bb = dotp(oc, *d), disc = bb * bb - (dotp(oc, oc) - r * r);
        if (disc >= 0) { const double tt = -bb - std::sqrt(disc); if (tt > 0 && tt < st) { sk = 14 + k; st = tt; } }
    }
    if (t->qCover_ < 0.5) {                                              // the pods' key's cover, shut (16): a touch on it = on its key
        const VECTOR3 Cc = _V(kCoverHinge[0], kCoverHinge[1] + 0.014, kCoverHinge[2] + 0.025), oc = *o - Cc;
        const double r = 0.034, bb = dotp(oc, *d), disc = bb * bb - (dotp(oc, oc) - r * r);
        if (disc >= 0) { const double tt = -bb - std::sqrt(disc); if (tt > 0 && tt < st) { sk = 16; st = tt; } }
    }
    {                                                                    // the cups' wheel on the pods' lever (12)
        const VECTOR3 oc = *o - t->wheelC_;
        const double r = 0.03, bb = dotp(oc, *d), disc = bb * bb - (dotp(oc, oc) - r * r);
        if (disc >= 0) { const double tt = -bb - std::sqrt(disc); if (tt > 0 && tt < st) { sk = 12; st = tt; } }
    }
    {                                                                    // the ball stands out of the plate (taken a little bigger)
        const VECTOR3 B = _V(kCurBall[0], kCurBall[1], kCurBall[2]) + mv, oc = *o - B;
        const double r = kCurBall[3] * 1.8, bb = dotp(oc, *d), disc = bb * bb - (dotp(oc, oc) - r * r);   // (under the palm: taken bigger)
        if (disc >= 0) { const double tt = -bb - std::sqrt(disc); if (tt > 0 && tt < st + 0.05) { sk = 8; st = tt; } }
    }
    if (sk >= 0) {
        const double py = sv * kSeatPanelH, frac = Clamp((kSeatPanelH - 28.0 - py) / (kSeatPanelH - 51.0), 0.0, 1.0);   // the strips' scales (TantraSeatCmd.cpp)
        if (sk == 5) t->adjTarget_ = frac;                                // a touch on a strip = the place / the height
        else if (sk == 6) t->hgtTarget_ = frac;
        else if (sk == 7) t->SeatPlate(2 * kSeatStripWid * kSeatPanelPx + su * kCurPlateW * kSeatPanelPx, py);
        else if (sk == 9 || sk == 10) {                                  // the hub's texture (screen 5): the face on top, the screen under it
            const double px0 = sk == 9 ? 0.0 : (kHubTexW - kHubScr[3] * kHubTexPx) / 2, py0 = sk == 9 ? 0.0 : kHubKeys[4] * kHubTexPx;
            const double* r5 = sk == 9 ? kHubKeys : kHubScr;
            if (t->touch_.Touch) t->touch_.Touch(t->touch_.ctx, 5, (px0 + su * r5[3] * kHubTexPx) / kHubTexW, (py0 + sv * r5[4] * kHubTexPx) / kHubTexH);
        }
        else if (sk == 11) t->uvtOn_ = !t->uvtOn_;
        else if (sk == 12) { if (t->touch_.Touch) t->touch_.Touch(t->touch_.ctx, 8, 3.0, 0.0); }   // the cups: the next angle
        else if (sk == 14 || sk == 15) { t->leverDrag_ = sk - 14; t->leverSent_ = -1.0; t->LeverDrag(); }   // a lever taken by its grip
        else if (sk == 16) { if (t->touch_.Touch) t->touch_.Touch(t->touch_.ctx, 4, 0.040 / 0.29, 0.118 / 0.54); }   // the cover: as a touch on its key (lifts it)
        else { t->spotOn_ = !t->spotOn_; t->panelKey_ = -1.0; }          // the ball: the light spot on / off
        oapiWriteLogV("Tantra interior: seat control %d at %.2f %.2f", sk, su, sv);
        return 1;
    }
    if (best.kind < 0) return 0;
    t->PressAt(best);
    oapiWriteLogV("Tantra interior: touch screen %d/%d at %.2f %.2f%s", best.kind, best.idx, best.u, best.v, anyDist ? " (light spot)" : "");
    return 1;
}

// The screens under a ray (interior frame): the 2D panel pieces (kind 0), the MFD places (1), the touch screens' flat pieces
// (2); the nearest one, within reach of the head if a head is given. Its point, its axes and its normal (toward the ray) too.
bool TantraInterior::PickScreens(const VECTOR3& o, const VECTOR3& d, const VECTOR3* head, Pick& best) const {
    best = Pick();
    auto test = [&](int kind, int idx, const double* cc, const double* ex, const double* up, const double* nn, double w, double h) {
        const VECTOR3 C = _V(cc[0], cc[1], cc[2]), EX = _V(ex[0], ex[1], ex[2]), UP = _V(up[0], up[1], up[2]), N = _V(nn[0], nn[1], nn[2]);
        const double den = dotp(d, N);
        if (den > -1e-6) return;                                         // from behind or edge-on
        const double tt = dotp(C - o, N) / den;
        if (tt <= 0 || tt >= best.t) return;
        const VECTOR3 p = o + d * tt, q = p - C;
        const double u = dotp(q, EX) / w + .5, v = .5 - dotp(q, UP) / h;
        if (u < 0 || u > 1 || v < 0 || v > 1) return;
        if (head && length(p - *head) > 1.7) return;                     // within arm's reach (seated or standing, leaning in)
        best.kind = kind; best.idx = idx; best.u = u; best.v = v; best.t = tt; best.p = p; best.ex = EX; best.up = UP; best.n = N;
    };
    for (int k = 0; k < kPanelPieceCount; k++) { const PanelPiece& q = kPanelPieces[k]; test(0, k, q.c, q.ex, q.up, q.n, q.w, q.h); }
    for (int k = 0; k < kMfdCount; k++) { const MfdPlace& q = kMfd[k]; test(1, k, q.c, q.ex, q.up, q.n, q.s, q.s); }
    for (int k = 0; k < kTouchFacetCount; k++) {                         // the touch screens in flat pieces (the curved ones too)
        const TouchFacet& q = kTouchFacets[k];
        if (q.screen < 2) {                                              // a side glass: still in РАБОТА / ЛЕНТА, moved with it, over the desk
            if (!SideDisplayUsable(q.screen)) continue;
            const double* r = kSideRise[q.screen]; const double s = sidePos_[q.screen];
            const double c[3] = {q.c[0] + r[0] * s, q.c[1] + r[1] * s, q.c[2] + r[2] * s};
            const Pick keep = best;
            test(2, k, c, q.ex, q.up, q.n, q.w, q.h);
            if (best.t != keep.t && best.p.y < kDeskTopY + 0.005) best = keep;   // (its part in the desk)
            continue;
        }
        test(2, k, q.c, q.ex, q.up, q.n, q.w, q.h);
    }
    return best.kind >= 0;
}

void TantraInterior::PressAt(const Pick& b) {
    switch (b.kind) {
        case 0: {                                                        // a section of the 2D panels: the point on panel.dds
            const PanelPiece& q = kPanelPieces[b.idx];
            ClickPanel(q.tx0 + b.u * (q.tx1 - q.tx0), q.ty0 + b.v * (q.ty1 - q.ty0));
            panelT_ = 0.0;
            break;
        }
        case 1: mfds_.Touch(b.idx, b.u, b.v); break;
        case 2: {                                                        // a piece of a screen: the point on the whole screen
            const TouchFacet& q = kTouchFacets[b.idx];
            if (touch_.Touch) touch_.Touch(touch_.ctx, q.screen, q.u0 + b.u * (q.u1 - q.u0), q.v0 + b.v * (q.v1 - q.v0));
            break;
        }
        default: break;
    }
}

// A click at a point of panel.dds (texture px): the panel area under it, as the 2D panels register them.
bool TantraInterior::ClickPanel(double tx, double ty) {
    using namespace tantra::panel;
    static const int kClick[] = {A_LEVER, A_SEL_PLAN, A_SEL_ANA, A_PODS_AFT, A_PODS_DOWN, A_STOP, A_TRAPS, A_TRAPSEL, A_GLIM, A_GSTEP,
                                 A_OVERRIDE, A_AIRLOCK, A_EVA, A_CREWSEL, M_SEL_PLAN, M_SEL_ANA, M_START, M_STOP, L_GEAR, L_SET_LEVEL,
                                 L_SET_STAND, L_ERECT, L_PORT, L_CRESTS, L_PODS_AFT, L_PODS_DOWN, L_HANGAR, L_ROVERS, L_AIRLOCK, L_EVA,
                                 L_CREWSEL, L_PT_LIFT, L_PT_LOAD, L_PT_DROP, L_PT_STOP};
    if (!console_.Click) return false;
    for (int a : kClick) {
        const int* r = kArea[a];
        if (tx >= r[0] && tx <= r[2] && ty >= r[1] && ty <= r[3]) { console_.Click(console_.ctx, a, int(tx - r[0]), int(ty - r[1])); return true; }
    }
    return false;
}

void TantraInterior::PanelLights() {
    if (!mesh_) return;
    const double t = oapiGetSysTime();
    if (kGrpScrBlink >= 0) {                                             // the main screen's status light: blinks (1.2 s)
        const int on = std::fmod(t, 1.2) < 0.25;
        if (on != scrBlink_) {
            scrBlink_ = on;
            GROUPEDITSPEC e = {}; e.flags = GRPEDIT_SETUSERFLAG; e.UsrFlag = on ? 0 : 2;
            oapiEditMeshGroup(mesh_, DWORD(kGrpScrBlink), &e);
        }
    }
    if (!panel_.State) return;
    for (int k = 0; k < 6; k++) {
        const int gl = k < 3 ? kLiftBtnLit[k] : kCabBtnLit[k - 3], gd = k < 3 ? kLiftBtnDim[k] : kCabBtnDim[k - 3];
        if (gl < 0 || gd < 0) continue;
        int st = panel_.State(panel_.ctx, k);
        const int on = st == 1 ? 1 : st == 2 ? (std::fmod(t, 1.0) < 0.5) : st == 3 ? (std::fmod(t, 0.3) < 0.15) : 0;
        if (on == capShown_[k]) continue;
        capShown_[k] = on;
        GROUPEDITSPEC e = {}; e.flags = GRPEDIT_SETUSERFLAG;
        e.UsrFlag = on ? 0 : 2; oapiEditMeshGroup(mesh_, DWORD(gl), &e);     // user flag 2: not drawn
        e.UsrFlag = on ? 2 : 0; oapiEditMeshGroup(mesh_, DWORD(gd), &e);
    }
}

double TantraInterior::kILiftStopY(int k) { return kILiftStop[k]; }

int TantraInterior::ILiftItem(int k, OcItem* out) const {
    if (!out || k < 0 || k >= 2 * kILiftStopN) return 0;
    static const char* const kStop[4] = {"нижний уровень", "средний уровень (каюты)", "жилая палуба", "технический уровень"};
    static_assert(kILiftStopN == 4, "name the inner lift's stops");
    const int st = k % kILiftStopN;
    *out = OcItem{};
    out->id = 300 + k;
    out->kind = OC_BUTTON;                                               // pressed with the mouse
    out->dir = _V(1, 0, 0);                                              // the cab's buttons face east
    out->radius = 0.09;
    if (k < kILiftStopN) {                                               // outside, on the forward end of the shaft: call the cab to stop st
        out->dir = _V(0, 0, 1);
        out->pos = _V(kILiftCallPos[st][0], kILiftCallPos[st][1], kILiftCallPos[st][2]);
        snprintf(out->label, sizeof out->label, "вызвать лифт");
    } else {                                                             // inside the cab: a button per stop on the west wall
        out->pos = _V(kILiftSendPos[st][0], kILiftSendPos[st][1] + (iliftY_ - kILiftStop[0]), kILiftSendPos[st][2]);
        snprintf(out->label, sizeof out->label, "лифт: %s", kStop[st]);
    }
    return 1;
}

void TantraInterior::ILiftWalls(double& x, double& z, double feet, double radius, double height) const {
    for (int st = 0; st < kILiftStopN; st++) {                           // a shut door: the opening in the east wall of the shaft
        if (ILiftOpen(st)) continue;
        if (x < kILiftDoorX - 0.05) continue;                            // from inside the shaft one always gets out (never trapped)
        const double y0 = kILiftStop[st], y1 = y0 + 2.1;
        if (feet + height <= y0 || feet >= y1) continue;
        const double bx0 = kILiftDoorX - 0.04, bx1 = kILiftDoorX + 0.04, bz0 = kILiftDoorZ - kILiftDoorHw, bz1 = kILiftDoorZ + kILiftDoorHw;
        const double cx = Clamp(x, bx0, bx1), cz = Clamp(z, bz0, bz1), dx = x - cx, dz = z - cz, d2 = dx * dx + dz * dz;
        if (d2 >= radius * radius) continue;
        if (d2 > 1e-10) { const double d = std::sqrt(d2), k = (radius - d) / d; x += dx * k; z += dz * k; }
        else x = (x < kILiftDoorX) ? bx0 - radius : bx1 + radius;
    }
}

void TantraInterior::ILiftStep(double dt) {
    if (!iliftInit_ || dt <= 0.0) return;
    bool shut = true;
    for (int k = 0; k < kILiftStopN; k++) {                              // a door opens where the cab stands still, otherwise shuts (1 s)
        const double want = (ILiftAt(k) && std::fabs(iliftTarget_ - kILiftStop[k]) < 0.01) ? 1.0 : 0.0;
        const double d = want - iliftDoor_[k];
        if (d != 0.0) { iliftDoor_[k] += d > 0 ? (std::min)(d, dt) : (std::max)(d, -dt); v_->SetAnimation(iliftDoorAnim_[k], iliftDoor_[k]); }
        if (iliftDoor_[k] > 0.0) shut = false;
    }
    if (!shut && iliftV_ == 0.0) return;                                 // it starts only with every door shut
    const double rem = iliftTarget_ - iliftY_;
    const bool start = iliftV_ == 0.0 && std::fabs(rem) > 1e-4;
    if (start && api_.Carry && api_.InteriorPos && api_.PersonOfBody && api_.ShipOf) {   // who stands in the cab rides with it
        nIRiders_ = 0;
        for (DWORD i = 0; i < oapiGetVesselCount() && nIRiders_ < 8; i++) {
            const int id = api_.PersonOfBody(oapiGetVesselByIndex(i));
            if (!id || api_.ShipOf(id) != v_->GetHandle()) continue;
            VECTOR3 ft; double hdg = 0.0;
            if (!api_.InteriorPos(id, &ft, &hdg)) continue;
            if (ft.x < kILiftX0 || ft.x > kILiftX1 || ft.z < kILiftZ0 || ft.z > kILiftZ1 || std::fabs(ft.y - iliftY_) > 0.5) continue;
            irider_[nIRiders_++] = {id, ft.x, ft.z, _V(std::sin(hdg), 0, std::cos(hdg))};
        }
    }
    if (std::fabs(rem) > 1e-4) {                                         // 2 m/s, 1 m/s^2 up and down
        const double dir = rem > 0 ? 1.0 : -1.0;
        const double vWant = dir * (std::min)(2.0, std::sqrt(2.0 * 1.0 * std::fabs(rem)));
        iliftV_ += (std::max)(-1.0 * dt, (std::min)(1.0 * dt, vWant - iliftV_));
        if (iliftV_ * dir <= 0.0) iliftV_ = dir * 0.05;
        double ny = iliftY_ + iliftV_ * dt;
        if ((iliftTarget_ - ny) * dir <= 0.0) { ny = iliftTarget_; iliftV_ = 0.0; }
        iliftY_ = ny;
        for (int i = 0; i < nIRiders_; i++) {
            const VECTOR3 pos = _V(irider_[i].x, iliftY_, irider_[i].z);
            api_.Carry(irider_[i].id, v_->GetHandle(), &pos, &irider_[i].dir);
        }
        if (iliftV_ == 0.0) nIRiders_ = 0;
    }
    v_->SetAnimation(iliftAnim_, (iliftY_ - kILiftStop[0]) / (kILiftStop[kILiftStopN - 1] - kILiftStop[0]));
}

void TantraInterior::Step(double dt) {
    const double rdt = oapiGetSysStep() / (oapiGetTimeAcceleration() > 10.0 ? 4.0 : 1.0);   // the screens' timers: real time, rarer over x10
    ILiftStep(dt);
    PanelLights();
    mfds_.Step();
    if (touch_.Step) touch_.Step(touch_.ctx, dt);
    for (int k = 0; k < 2; k++) {                                        // the elbow displays: ~1 s to sink / rise
        const double d = sideTarget_[k] - sidePos_[k];
        if (d == 0.0) continue;
        const double st = (std::min)(std::fabs(d), (std::min)(dt, 0.1) * 1.0);
        sidePos_[k] += d > 0 ? st : -st;
        v_->SetAnimation(sideAnim_[k], sidePos_[k]);
    }
    if (console_.Redraw && mesh_ && (panelT_ -= rdt) <= 0.0) {             // the console's panels, ~10 times a second
        panelT_ = 0.1;
        for (int a : pareas_) console_.Redraw(console_.ctx, a);
    }
    if ((statusT_ -= rdt) <= 0.0) { statusT_ = 0.25; DrawStatus(); }   // (real time: the screens as at x1 whatever the warp)
    if ((cabScrT_ -= rdt) <= 0.0) { cabScrT_ = 0.25; DrawCabStatus(); }
    if (panel_.Cabin) {                                                  // the cabin's panel where the cabin is; door B's leaves
        double out = 0.0, down = 0.0; int ag = 0; panel_.Cabin(panel_.ctx, &out, &down, &ag);
        v_->SetAnimation(cabOutAnim_, out);
        v_->SetAnimation(cabDownAnim_, down);
    }
    if (panel_.DoorB) v_->SetAnimation(doorBAnim_, panel_.DoorB(panel_.ctx));
    if (panel_.CabDoor) v_->SetAnimation(cabDoorAnim_, panel_.CabDoor(panel_.ctx));
    CarryRiders();
    if (crew_ && panel_.Cabin) {                                         // a person coming up the lift stands in the cabin wherever it is now
        double sx, sy; bool st, ag; CabinPose(sx, sy, st, ag);
        crew_->SetArrival(_V(kArrivalX + sx, kCabFloor + sy, kArrivalZ), _V(1, 0, 0));
    }
    {                                                                    // the commander's place: 0.25 m/s, the height 0.05 m/s
        const double d = adjTarget_ - adjPos_;
        if (d != 0.0) {
            const double st = (std::min)(std::fabs(d), (std::min)(dt, 0.1) * 0.25 / (kSeatAdjMax - kSeatAdjMin));
            adjPos_ += d > 0 ? st : -st;
            v_->SetAnimation(adjAnim_, adjPos_);
        }
        const double dh = hgtTarget_ - hgtPos_;
        if (dh != 0.0) {
            const double st = (std::min)(std::fabs(dh), (std::min)(dt, 0.1) * 0.05 / (kSeatHgtMax - kSeatHgtMin));
            hgtPos_ += dh > 0 ? st : -st;
            v_->SetAnimation(hgtAnim_, hgtPos_);
        }
        SpotStep();                                                      // the light spot, the ball, the seat's panel
        YokeStep(dt);                                                    // the yoke (TantraYoke.cpp)
        QuadStep(dt);                                                    // the throttle quadrant, the pods' guard cover
    }
    for (int i = 0; i < kSeatN; i++) {                                   // 0.7 s from the back to the console
        const double d = seatTarget_[i] - seatPos_[i];
        if (d == 0.0) continue;
        const double st = (std::min)(std::fabs(d), (std::min)(dt, 0.1) / 0.7);   // the whole run in 0.7 s (the user's limit)
        seatPos_[i] += d > 0 ? st : -st;
        v_->SetAnimation(anim_[i], seatPos_[i]);
    }
    if (standSeat_ < 0) return;
    const int seat = standSeat_;
    standSeat_ = -1;
    if (seat_ == seat && person_ && api_.Stand) {                        // seated with the body: OrbiterCrew stands up and takes the view
        oapiWriteLogV("Tantra interior: person %d stands up from the seat '%s'", person_, kSeats[seat].name);
        if (api_.Stand) api_.Stand(person_);      // optional export: an older CrewMember.dll has none (null call = crash)
        return;
    }
    int p = (seat_ == seat) ? person_ : 0;
    if (!p && crew_) p = crew_->PersonOf(seat);                          // seat N -> crew slot N (commander, starboard, port, navigator)
    if (!p) { oapiWriteLogV("Tantra interior: nobody of the crew for the seat '%s'", kSeats[seat].name); return; }
    seat_ = -1; person_ = 0;
    const VECTOR3 dir = _V(kSeats[seat].fx, 0, kSeats[seat].fz);       // behind the seat, facing its console
    const VECTOR3 pos = _V(kSeats[seat].x, kBridgeFloorY, kSeats[seat].z) - dir * 0.95;
    oapiWriteLogV("Tantra interior: person %d stands up from the seat '%s'", p, kSeats[seat].name);
    if (api_.EnterInterior) api_.EnterInterior(p, &pos, &dir);                                   // the body stands there, the focus goes to it
}

// People walking outside on the ground: the ship's cup feet stop them (Tantra::OuterWalls, ship frame).
void TantraInterior::cOuterWalls(void* c, const VECTOR3* from, VECTOR3* to, double radius, double height) {
    if (!from || !to) return;
    static_cast<const Tantra*>(Self(c)->v_)->OuterWalls(*from, *to, radius, height);
}
