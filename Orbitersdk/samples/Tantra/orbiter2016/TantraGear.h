// Tantra: mesh animations for Orbiter 2010. Builds the rig from MeshLayout.h (generated
// together with the mesh by tools/gen_mesh.py) and drives it from core/Carriage and the
// ship systems. No logic lives here, only the mapping of states onto animations.
#pragma once
#include "orbitersdk.h"

#include "../core/Carriage.h"
#include "MeshLayout.h"

class TantraGear {
public:
    struct Extras {
        double tuck = 0.0;        // crests folded / pods sunk (max of erection and flight mode)
        double podSwivel = 0.0;   // pods 0..1 (0 = thrust forward, 1 = 100 deg)
        double podStow = 1.0;     // pods 1 = in the bays, doors shut .. 0 = hanging out
        double elevon[2] = {tantra::mesh::kElevonUpDeg / (tantra::mesh::kElevonUpDeg + tantra::mesh::kElevonDownDeg),
                            tantra::mesh::kElevonUpDeg / (tantra::mesh::kElevonUpDeg + tantra::mesh::kElevonDownDeg)};
        double bodyFlap = 0.0;    // 0..1 = 0..25 deg trailing edge down
        double irisAna = 0.0;     // anamezon cups open 0..1
        double irisPlan = 1.0;    // planetary cups open 0..1
        double hangar = 0.0;      // hangar doors open 0..1
        double rovers = 0.0;      // rover platform lowered 0..1
        double airlockUp = 1.0;   // airlock lift raised 0..1
        double bayDoors = 0.0;    // anamezon port doors 0..1
        bool trapHidden[4] = {};  // empty slot
        double liftY[2] = {tantra::mesh::kLiftY0, tantra::mesh::kLiftY0};  // fork heads (cassette centre, ship y)
    };

    TantraGear(VESSEL* v, UINT mesh);
    ~TantraGear();

    void Apply(const tantra::CarriagePose& pose, double sCG, const Extras& ex);

    // Lower-leg feet while resting level, from the rig (for the touchdown points).
    static void LegRestFoot(double& x, double& s);

private:
    void Set(int anim, double state);

    VESSEL* v_;
    UINT anim_[tantra::mesh::ANIM_COUNT] = {};
    double state_[tantra::mesh::ANIM_COUNT] = {};
    MGROUP_TRANSFORM* trans_[tantra::mesh::kRigCount] = {};
    ANIMATIONCOMPONENT_HANDLE comp_[tantra::mesh::kRigCount] = {};
    UINT group_[tantra::mesh::kRigCount] = {};
    VECTOR3 pivot_[tantra::mesh::kRigCount] = {};  // dummy vertex for pivot-only components
};
