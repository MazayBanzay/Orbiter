// «Грань» 25,4 м - the interior for OrbiterCrew. See LanderInterior.h.
#define NOMINMAX
#include "LanderInterior.h"
#include "LanderCockpit.h"
#include "LanderCabinMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cb = tantra::lander::cabin;

namespace tantra::lander {
namespace {

inline LanderInterior* Self(void* c) { return static_cast<LanderInterior*>(c); }
constexpr double kFloorX = 1.45, kFloorZ0 = 6.49, kFloorZ1 = 7.41;   // the drum's floor in its own (built) frame
const char* const kSeatLabel[2] = {"ложемент командира", "ложемент второго пилота"};

}  // namespace

void LanderInterior::Init(VESSEL4* v, UINT cabinMesh, Cockpit* cockpit, const Host& host) {
    v_ = v; cabin_ = cabinMesh; ck_ = cockpit; host_ = host;
    att_ = v_->CreateAttachment(false, _V(0, 0, 0), _V(0, 0, 1), _V(0, 1, 0), "OCINT");   // the walking body hangs on it
    fns_.Attach = cAttach; fns_.Ground = cGround; fns_.Walls = cWalls; fns_.Zone = cZone; fns_.Gravity = cGravity;
    fns_.Count = cCount; fns_.Item = cItem; fns_.Use = cUse; fns_.Seat = cSeat; fns_.Viewing = cViewing;
    ext_.size = sizeof(OcInteriorExt);
    ext_.Origin = cOrigin; ext_.CanWalk = cCanWalk; ext_.Seated = cSeated; ext_.Click = cClick;
    ext_.SeatHands = cSeatHands; ext_.SeatGauges = cSeatGauges; ext_.Ceiling = cCeiling;
}

void LanderInterior::Register() {
    if (reg_) return;
    if (!api_.Load() || !api_.HasInterior() || !api_.UnregisterInterior) {
        oapiWriteLogV("Gran interior: OrbiterCrew without the interior interface - the cabin is the Orbiter VC only");
        return;
    }
    api_.RegisterInterior(v_->GetHandle(), &fns_, this);
    if (api_.SetInteriorExt) api_.SetInteriorExt(v_->GetHandle(), &ext_);
    reg_ = true;
    oapiWriteLogV("Gran interior: registered with OrbiterCrew (the drum, 2 helm couches)");
}

void LanderInterior::Unregister() {
    if (reg_ && api_.UnregisterInterior) api_.UnregisterInterior(v_->GetHandle());
    reg_ = false;
    api_.Unload();
}

// the default pilots: a ship whose scenario has no people seats CREW of them (≤ 2) at the start, the commander last (the
// focus and the camera are his: in the cockpit at once). Not inside a key handler (Orbiter fell there in «Тантра»)
void LanderInterior::Step(double dt, double frameZ, int crewSeed) {
    frameZ_ = frameZ;
    if (!reg_ || seeded_ || crewSeed <= 0 || !api_.CreatePerson || !api_.SetAboard || !api_.SitAt) return;
    seedT_ += dt;
    if (seedT_ < 1.0 || seedTries_ >= 10) return;
    seedT_ = 0; ++seedTries_;
    const int n = std::min(crewSeed, 2);
    static const char* name[2] = {"Командир", "Второй пилот"};
    static const char* role[2] = {"командир", "второй пилот"};
    static const double age[2] = {40, 34}, kg[2] = {78, 70};
    int id[2] = {0, 0};
    for (int k = n - 1; k >= 0; --k) {
        id[k] = api_.CreatePerson(name[k], role[k], age[k], kg[k]);
        if (!id[k]) continue;
        api_.SetAboard(id[k], v_->GetHandle());
        if (!api_.SitAt(id[k], v_->GetHandle(), k)) oapiWriteLogV("Gran interior: %s not seated (try %d)", name[k], seedTries_);
    }
    if (id[0] || id[n - 1]) {
        seeded_ = true;
        oapiWriteLogV("Gran interior: %d pilot(s) seated at the start", n);
    }
}

int LanderInterior::HelmFocused() const {
    if (!reg_ || !api_.Info) return -1;
    DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return -1;
    const OBJHANDLE f = oapiGetFocusObject();
    for (int s = 0; s < 2; ++s) {
        OcInfo in{};
        if (seatPerson_[s] && api_.Info(seatPerson_[s], &in) && in.vessel == f) return s;
    }
    return -1;
}

// ------------------------------------------------------------------------------------------------------------------------------
ATTACHMENTHANDLE LanderInterior::cAttach(void* c) { return Self(c)->att_; }

// the drum's floor (it turns with the drum: its height under the point, from the drum's own frame)
int LanderInterior::cGround(void* c, const VECTOR3* p, double stepUp, double* floorY) {
    const LanderInterior* t = Self(c);
    MATRIX3 R; VECTOR3 tr; t->ck_->Motion(-1, R, tr);
    const VECTOR3 b = tmul(R, *p - tr);                                  // the point in the drum's built frame
    if (std::fabs(b.x) > kFloorX || b.z < kFloorZ0 || b.z > kFloorZ1) return 0;
    const VECTOR3 f = mul(R, _V(b.x, cb::kDrumFloorY, b.z)) + tr;
    if (f.y > p->y + stepUp + 1e-3 || f.y < p->y - 0.5) return 0;
    if (floorY) *floorY = f.y;
    return 1;
}

// the drum's walls: the feet kept on its floor (the drum's own frame)
void LanderInterior::cWalls(void* c, const VECTOR3* from, VECTOR3* to, double radius, double) {
    const LanderInterior* t = Self(c);
    MATRIX3 R; VECTOR3 tr; t->ck_->Motion(-1, R, tr);
    VECTOR3 b = tmul(R, *to - tr);
    b.x = std::max(-kFloorX + radius, std::min(kFloorX - radius, b.x));
    b.z = std::max(kFloorZ0 + radius, std::min(kFloorZ1 - radius, b.z));
    *to = mul(R, b) + tr;
    (void)from;
}

// the turning parts: 1 the drum, 2 / 3 the pilots' modules (zone -> ship: p = R·q + t; q in the built cabin)
int LanderInterior::cZone(void* c, const VECTOR3* p, MATRIX3* R, VECTOR3* t) {
    const LanderInterior* s = Self(c);
    if (!s->ck_->InDrum(*p)) return 0;
    const int m = s->ck_->ModuleAt(*p);
    MATRIX3 r; VECTOR3 tt; s->ck_->Motion(m, r, tt);
    if (R) *R = r;
    if (t) *t = tt;
    return m < 0 ? 1 : 2 + m;
}

void LanderInterior::cGravity(void* c, const VECTOR3*, VECTOR3* g) { *g = -Self(c)->ck_->Felt(); }   // the felt gravity

int LanderInterior::cCount(void*) { return 2; }

int LanderInterior::cItem(void* c, int i, OcItem* out) {
    const LanderInterior* t = Self(c);
    if (i < 0 || i > 1 || !out) return 0;
    *out = OcItem{};
    out->id = i; out->kind = OC_HELM;                                    // both couches steer (the commander, the second pilot)
    out->pos = t->ck_->Hip(i); out->dir = t->ck_->Facing(i); out->radius = 1.4;
    std::snprintf(out->label, sizeof out->label, "%s", kSeatLabel[i]);
    return 1;
}

void LanderInterior::cUse(void*, int, int) {}

// the seat's pose now: the hips and the facing turn with the drum and the module's roll (read every frame)
void LanderInterior::cSeat(void* c, int id, VECTOR3* pos, VECTOR3* dir) {
    const LanderInterior* t = Self(c);
    if (id < 0 || id > 1) return;
    if (pos) *pos = t->ck_->Hip(id);
    if (dir) *dir = t->ck_->Facing(id);
}

void LanderInterior::cViewing(void* c, int on) {
    LanderInterior* t = Self(c);
    t->v_->SetMeshVisibilityMode(t->cabin_, on ? MESHVIS_ALWAYS : MESHVIS_VC);
    oapiWriteLogV("Gran interior: %s", on ? "a person looks inside - the cabin drawn as an outside mesh" : "the cabin back to the VC only");
}

void LanderInterior::cOrigin(void* c, VECTOR3* o) { *o = _V(0, 0, -Self(c)->frameZ_); }   // ship = interior (mesh) + this

int LanderInterior::cCanWalk(void* c, char* reason, int n) {
    const LanderInterior* t = Self(c);
    if (t->host_.MayWalk && t->host_.MayWalk(t->host_.ctx)) return 1;
    if (reason && n > 0) std::snprintf(reason, n, "%s", "в полёте - в ложементе, пристёгнуты");
    return 0;
}

void LanderInterior::cSeated(void* c, int seatId, int personId, int on) {
    LanderInterior* t = Self(c);
    if (seatId < 0 || seatId > 1) return;
    if (on) t->seatPerson_[seatId] = personId;
    else if (t->seatPerson_[seatId] == personId) t->seatPerson_[seatId] = 0;
    oapiWriteLogV("Gran interior: person %d %s couch %d", personId, on ? "sits in" : "leaves", seatId);
}

// the touch screens and keys: the click's ray from the camera (interior frame); a seated pilot or one standing in the drum
int LanderInterior::cClick(void* c, const VECTOR3* o, const VECTOR3* d, int) {
    const LanderInterior* t = Self(c);
    if (!t->ck_->InDrum(*o) && length(*o - _V(cb::kDrumC[0], cb::kDrumC[1], cb::kDrumC[2])) > 2.5) return 0;
    return t->ck_->Click(*o, *d) ? 1 : 0;
}

// the hands: the inboard on the yoke's inboard horn (a handle), the outboard on МАРШ (its knob from above); the
// commander's inboard is his right, the second pilot's his left
void LanderInterior::cSeatHands(void* c, int seatId, int, OcHand* left, OcHand* right) {
    const LanderInterior* t = Self(c);
    if (seatId < 0 || seatId > 1 || !left || !right || left->size < int(sizeof(OcHand)) || right->size < int(sizeof(OcHand))) return;
    VECTOR3 g0, g1, hubF; t->ck_->GripIn(seatId, g0, g1, hubF);
    const VECTOR3 a = unit(g1 - g0), up = t->ck_->Up(seatId), fwd = t->ck_->Facing(seatId);
    VECTOR3 n = -hubF; n = unit(n - a * dotp(n, a));                    // the palm into the grip, away from the pilot
    const double r = 0.022;
    OcHand* yokeHand = seatId == 0 ? right : left;
    OcHand* leverHand = seatId == 0 ? left : right;
    yokeHand->on = 1; yokeHand->what = 10 + seatId * 2; yokeHand->grip = OC_GRIP_HANDLE; yokeHand->radius = r;
    yokeHand->pos = (g0 + g1) * 0.5 - n * r; yokeHand->palm = n;
    yokeHand->fwd = seatId == 0 ? crossp(n, a) : crossp(a, n); yokeHand->elbowMinY = 0;
    leverHand->on = 1; leverHand->what = 11 + seatId * 2; leverHand->grip = OC_GRIP_BALL; leverHand->radius = 0.025;
    leverHand->pos = t->ck_->MarchKnob(seatId) + up * 0.02; leverHand->palm = -up; leverHand->fwd = fwd; leverHand->elbowMinY = 0;
}

int LanderInterior::cSeatGauges(void* c, int seatId, int, OcGauge* out, int max) {
    const LanderInterior* t = Self(c);
    if (seatId < 0 || seatId > 1 || !t->host_.Gauges) return 0;
    return t->host_.Gauges(t->host_.ctx, out, max);
}

double LanderInterior::cCeiling(void*, const VECTOR3*) { return cb::kDrumC[1] + 1.05; }

}  // namespace tantra::lander
