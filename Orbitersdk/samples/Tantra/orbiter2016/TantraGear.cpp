#include "TantraGear.h"

#include <algorithm>
#include <cmath>

namespace m = tantra::mesh;

namespace {

VECTOR3 ToV(const m::V& a) { return _V(a.x, a.y, a.z); }
double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// Right-handed rotation of `p` about unit `axis` (Orbiter's MGROUP_ROTATE convention).
VECTOR3 Rotate(const VECTOR3& p, const VECTOR3& axis, double ang) {
    const double c = std::cos(ang), s = std::sin(ang);
    return p * c + crossp(axis, p) * s + axis * (dotp(axis, p) * (1.0 - c));
}

}  // namespace

TantraGear::TantraGear(VESSEL* v, UINT mesh) : v_(v) {
    for (int a = 0; a < m::ANIM_COUNT; ++a) {
        anim_[a] = v_->CreateAnimation(m::kAnimDef[a]);  // the mesh is built at this state
        state_[a] = m::kAnimDef[a];
    }
    for (int i = 0; i < m::kRigCount; ++i) {
        const m::RigComp& c = m::kRig[i];
        UINT meshIdx = mesh, *grp, ngrp = 1;
        if (c.group >= 0) {
            group_[i] = static_cast<UINT>(c.group);
            grp = &group_[i];
        } else {  // pure pivot: an explicit one-vertex list carries the parent's transform to its children
            meshIdx = LOCALVERTEXLIST;
            pivot_[i] = ToV(c.ref);
            grp = MAKEGROUPARRAY(&pivot_[i]);
        }
        switch (c.kind) {
            case m::RIG_ROTATE:
                trans_[i] = new MGROUP_ROTATE(meshIdx, grp, ngrp, ToV(c.ref), ToV(c.vec), static_cast<float>(c.angle));
                break;
            case m::RIG_TRANSLATE:
                trans_[i] = new MGROUP_TRANSLATE(meshIdx, grp, ngrp, ToV(c.vec));
                break;
            default:
                trans_[i] = new MGROUP_SCALE(meshIdx, grp, ngrp, ToV(c.ref), ToV(c.vec));
                break;
        }
        comp_[i] = v_->AddAnimationComponent(anim_[c.anim], 0.0, 1.0, trans_[i], c.parent >= 0 ? comp_[c.parent] : nullptr);
    }
}

TantraGear::~TantraGear() {
    for (MGROUP_TRANSFORM* t : trans_) delete t;
}

void TantraGear::Set(int anim, double state) {
    state = Clamp01(state);
    if (std::fabs(state - state_[anim]) < 1e-5 && state_[anim] >= 0.0) return;
    state_[anim] = state;
    v_->SetAnimation(anim_[anim], state);
}

void TantraGear::LegRestFoot(double& x, double& s) {
    for (const m::LegRig& L : m::kLegs) {
        if (!L.lower || L.hinge.x < 0.0) continue;
        const VECTOR3 tip = ToV(L.hinge) + Rotate(_V(0, 0, m::kLegCasing + L.extRest), ToV(L.axis), L.phiRest);
        x = tip.x;
        s = tip.z + 38.0;  // mesh origin at s = 38
        return;
    }
}

void TantraGear::Apply(const tantra::CarriagePose& p, double sCG, const Extras& ex) {
    const double tuck = (std::max)(p.tuck, ex.tuck);
    Set(m::ANIM_CREST_DORSAL, tuck);
    Set(m::ANIM_CREST_LATERAL, tuck);
    Set(m::ANIM_POD_RETRACT, tuck);
    Set(m::ANIM_POD_SWIVEL, ex.podSwivel);
    Set(m::ANIM_IRIS_ANA, ex.irisAna);
    Set(m::ANIM_IRIS_PLAN, ex.irisPlan);
    Set(m::ANIM_HANGAR, ex.hangar);
    Set(m::ANIM_ROVER_LIFT, ex.rovers);
    Set(m::ANIM_AIRLOCK_LIFT, ex.airlockUp);

    // Carriages: track follows the CG, slide state 1 = home, mast pitch counter-rotates the hull.
    const double track = (sCG - m::kCarS0) / (m::kCarS1 - m::kCarS0);
    const double len = (m::kMastLMax - p.mastLen) / (m::kMastLMax - m::kMastSeg);  // telescope, 1 = collapsed
    const double pitch = p.mastPitch / (0.5 * PI);
    Set(m::ANIM_LID_PORT, p.lid);
    Set(m::ANIM_LID_STARBOARD, p.lid);
    Set(m::ANIM_TRACK_PORT, track);
    Set(m::ANIM_TRACK_STARBOARD, track);
    Set(m::ANIM_SLIDE_PORT, 1.0 - p.slideOut);
    Set(m::ANIM_SLIDE_STARBOARD, 1.0 - p.slideOut);
    Set(m::ANIM_PITCH_PORT, pitch);
    Set(m::ANIM_PITCH_STARBOARD, pitch);
    Set(m::ANIM_MAST_LEN_PORT, len);
    Set(m::ANIM_MAST_LEN_STARBOARD, len);

    // Anamezon port: doors, trap lifts and empty slots, manipulator joints.
    Set(m::ANIM_BAY_DOORS, ex.bayDoors);
    static const int kHide[4] = {m::ANIM_TRAP0_HIDE, m::ANIM_TRAP1_HIDE, m::ANIM_TRAP2_HIDE, m::ANIM_TRAP3_HIDE};
    for (int i = 0; i < 4; ++i) Set(kHide[i], ex.trapHidden[i] ? 1.0 : 0.0);  // a cassette outside is a vessel
    Set(m::ANIM_LIFT0, (m::kLiftY0 - ex.liftY[0]) / m::kLiftTravel);
    Set(m::ANIM_LIFT1, (m::kLiftY0 - ex.liftY[1]) / m::kLiftTravel);

    // Stern legs: resting pose (lower pair) and standing pose (all four) never overlap.
    static const int kSwing[4] = {m::ANIM_LEG0_SWING, m::ANIM_LEG1_SWING, m::ANIM_LEG2_SWING, m::ANIM_LEG3_SWING};
    static const int kExt[4] = {m::ANIM_LEG0_EXT, m::ANIM_LEG1_EXT, m::ANIM_LEG2_EXT, m::ANIM_LEG3_EXT};
    static const int kRest[4] = {m::ANIM_LEG0_FOOT_REST, m::ANIM_LEG1_FOOT_REST, m::ANIM_LEG2_FOOT_REST, m::ANIM_LEG3_FOOT_REST};
    static const int kStand[4] = {m::ANIM_LEG0_FOOT_STAND, m::ANIM_LEG1_FOOT_STAND, m::ANIM_LEG2_FOOT_STAND,
                                  m::ANIM_LEG3_FOOT_STAND};
    for (int i = 0; i < 4; ++i) {
        const m::LegRig& L = m::kLegs[i];
        const double wr = L.lower ? p.legRest : 0.0, ws = p.legStand;
        Set(kSwing[i], (wr * L.phiRest + ws * L.phiStand) / L.phiStand);
        Set(kExt[i], (wr * L.extRest + ws * L.extStand) / m::kLegExtMax);
        Set(kRest[i], wr);
        Set(kStand[i], ws);
    }
}
