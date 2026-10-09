#include "TantraGear.h"

#include "../core/Spec.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace m = tantra::mesh;

namespace {

VECTOR3 ToV(const m::V& a) { return _V(a.x, a.y, a.z); }
double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

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
        // NB: a scaling component must not be a child (Orbiter 2016 crashes when the parent moves it) - gen_mesh keeps them root
        comp_[i] = v_->AddAnimationComponent(anim_[c.anim], c.s0, c.s1, trans_[i], c.parent >= 0 ? comp_[c.parent] : nullptr);
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

void TantraGear::Apply(const tantra::CarriagePose& p, double sCG, const Extras& ex) {
    const double tuck = (std::max)(p.tuck, ex.tuck);
    Set(m::ANIM_CREST_DORSAL, tuck);                                       // telescopic fin into its slot
    // Erection folds the wings too: inner panels up first, then the outer panels down along them (outboard).
    Set(m::ANIM_CREST_LATERAL, (std::max)(ex.wingIn, (std::min)(1.0, 2.0 * p.tuck)));
    Set(m::ANIM_WING_OUTER, (std::max)(ex.wingOut, (std::max)(0.0, 2.0 * p.tuck - 1.0)));
    Set(m::ANIM_POD_RETRACT, ex.podStow);
    Set(m::ANIM_ELEVON_PORT, ex.elevon[0]);
    Set(m::ANIM_ELEVON_STARBOARD, ex.elevon[1]);
    Set(m::ANIM_BODY_FLAP, ex.bodyFlap);
    Set(m::ANIM_POD_SWIVEL, ex.podSwivel);
    Set(m::ANIM_POD_CANT, ex.podCant);
    Set(m::ANIM_IRIS_ANA, ex.irisAna);
    Set(m::ANIM_IRIS_MARCH, ex.irisMarch);
    Set(m::ANIM_MARCH_SLIDE, ex.marchOut);
    Set(m::ANIM_IRIS_NOSE, ex.irisNose);
    Set(m::ANIM_HANGAR, ex.hangar);
    Set(m::ANIM_ROVER_LIFT, ex.rovers);
    Set(m::ANIM_AIRLOCK_DOOR, ex.lockDoor);       // main airlock crew lift (TantraLift)
    Set(m::ANIM_AIRLOCK_OUT, ex.lockOut);
    Set(m::ANIM_AIRLOCK_MAST, ex.lockMast);
    Set(m::ANIM_AIRLOCK_DOWN, ex.lockDown);

    // Blade legs: hip on the track (state 1 = the stow station), slide 1 = in the pocket, pitch 1 = along the hull
    // (aft), blade_ext 1 = stages in, foot_fold 1 = fan folded flat.
    (void)sCG;
    const double track = (p.hipS - m::kCarS0) / (m::kStowS - m::kCarS0);
    // each blade as long as its contact really is: the drive lag (Tantra::UpdateGear) shortens a side a little
    const double extP = (m::kLegLMax - (p.mastLen - ex.bladeLag[0])) / (m::kLegLMax - m::kLegLMin);
    const double extS = (m::kLegLMax - (p.mastLen - ex.bladeLag[1])) / (m::kLegLMax - m::kLegLMin);
    const double pitch = p.mastPitch / (0.5 * PI);
    Set(m::ANIM_TRACK_PORT, track);
    Set(m::ANIM_TRACK_STARBOARD, track);
    Set(m::ANIM_SLIDE_PORT, 1.0 - p.slideOut);
    Set(m::ANIM_SLIDE_STARBOARD, 1.0 - p.slideOut);
    Set(m::ANIM_PITCH_PORT, pitch);
    Set(m::ANIM_PITCH_STARBOARD, pitch);
    Set(m::ANIM_BLADE_EXT_PORT, extP);
    Set(m::ANIM_BLADE_EXT_STARBOARD, extS);
    Set(m::ANIM_FOOT_FOLD_PORT, p.footFold);
    Set(m::ANIM_FOOT_FOLD_STARBOARD, p.footFold);
    Set(m::ANIM_STRUT_CARRIAGE, (std::max)(ex.strut[0], ex.strut[1]));   // one animation for both sides

    // Kangaroo leg: rig states straight from the pose.
    Set(m::ANIM_KANG_DOOR, p.kangDoor);
    Set(m::ANIM_KANG_HIP, p.kangHip);
    Set(m::ANIM_KANG_KNEE, p.kangKnee);
    Set(m::ANIM_KANG_EXT, p.kangExt);
    Set(m::ANIM_KANG_FOOT, p.kangFoot);
    Set(m::ANIM_KANG_FOLD, p.kangFold);
    Set(m::ANIM_KANG_STRUT, ex.strut[6]);
    for (int l = 0; l < 7; ++l)                                            // the gas cells under the umbrellas
        for (int c = 0; c < m::kCellN; ++c) Set(m::kCellAnim[l][c], ex.cell[l][c]);

    // Anamezon port: doors, trap lifts and empty slots.
    Set(m::ANIM_BAY_DOORS, ex.bayDoors);
    static const int kHide[4] = {m::ANIM_TRAP0_HIDE, m::ANIM_TRAP1_HIDE, m::ANIM_TRAP2_HIDE, m::ANIM_TRAP3_HIDE};
    for (int i = 0; i < 4; ++i) Set(kHide[i], ex.trapHidden[i] ? 1.0 : 0.0);  // a cassette outside is a vessel
    Set(m::ANIM_LIFT0, (m::kLiftY0 - ex.liftY[0]) / m::kLiftTravel);
    Set(m::ANIM_LIFT1, (m::kLiftY0 - ex.liftY[1]) / m::kLiftTravel);

    // Stern legs (2026-10-09: from their body bays, the door first, the swing done half way): swing to the standing pose,
    // sections run out after it (shorter by the
    // standing drop when the blades could not lift an empty ship to the nominal height), the foot hub rides its
    // rail down to the ankle and the umbrella opens; the foot turns to the ground in the last quarter.
    static const int kSwing[4] = {m::ANIM_LEG0_SWING, m::ANIM_LEG1_SWING, m::ANIM_LEG2_SWING, m::ANIM_LEG3_SWING};
    static const int kExt[4] = {m::ANIM_LEG0_EXT, m::ANIM_LEG1_EXT, m::ANIM_LEG2_EXT, m::ANIM_LEG3_EXT};
    static const int kStand[4] = {m::ANIM_LEG0_FOOT_STAND, m::ANIM_LEG1_FOOT_STAND, m::ANIM_LEG2_FOOT_STAND, m::ANIM_LEG3_FOOT_STAND};
    static const int kRail[4] = {m::ANIM_LEG0_RAIL, m::ANIM_LEG1_RAIL, m::ANIM_LEG2_RAIL, m::ANIM_LEG3_RAIL};
    static const int kFold[4] = {m::ANIM_LEG0_FOLD, m::ANIM_LEG1_FOLD, m::ANIM_LEG2_FOLD, m::ANIM_LEG3_FOLD};
    static const int kStrut[4] = {m::ANIM_LEG0_STRUT, m::ANIM_LEG1_STRUT, m::ANIM_LEG2_STRUT, m::ANIM_LEG3_STRUT};
    for (int i = 0; i < 4; ++i) {
        const m::LegRig& L = m::kLegs[i];
        const double ws = p.legStand;
        Set(kSwing[i], ws * L.phiStand / L.phiMax);
        const double run = Clamp01((ws - m::kLegExtDelay) / (1.0 - m::kLegExtDelay));
        const double extStand = (std::max)(0.0, L.extStand - p.standDrop / std::cos(L.phiStand));   // phiStand: off the ship's axis
        Set(kExt[i], run * extStand / m::kLegExtMax);
        Set(kStand[i], ws);
        Set(kRail[i], p.legRail);
        Set(kFold[i], p.legFold);
        Set(kStrut[i], ex.strut[2 + i]);
    }
}
