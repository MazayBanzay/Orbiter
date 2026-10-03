// Tantra: mesh animations for Orbiter 2016. Builds the rig from MeshLayout.h (generated together with the mesh by
// tools/gen_mesh.py) and drives it from core/Carriage and the ship systems. No logic lives here, only the mapping of
// states onto animations.
#pragma once
#include "orbitersdk.h"

#include "../core/Carriage.h"
#include "MeshLayout.h"

class TantraGear {
public:
    struct Extras {
        double tuck = 0.0;        // fin down (flight mode; the erection tuck comes with the pose)
        double wingIn = 0.0;      // inner wing panels: 0 deployed .. 1 folded up (kWingFoldDeg); the 30 deg mode in between
        double wingOut = 0.0;     // outer wing panels: 0 in line .. 1 folded back under the inner panel
        double podSwivel = 0.0;   // pods 0..1 (0 = cups aft, 1 = 180 deg)
        double podStow = 1.0;     // pods 1 = in the bays, covers shut .. 0 = out on the arms
        double elevon[2] = {tantra::mesh::kElevonUpDeg / (tantra::mesh::kElevonUpDeg + tantra::mesh::kElevonDownDeg),
                            tantra::mesh::kElevonUpDeg / (tantra::mesh::kElevonUpDeg + tantra::mesh::kElevonDownDeg)};
        double bodyFlap = 0.0;    // 0..1 = 0..25 deg trailing edge down
        double irisAna = 0.0;     // anamezon cups open 0..1
        double irisMarch = 0.0;   // well iris over the marching cup open 0..1
        double marchOut = 0.0;    // marching cup run out past the rims 0..1
        double irisNose = 0.0;    // nose retro cup petals open 0..1
        double hangar = 0.0;      // hangar doors open 0..1
        double rovers = 0.0;      // rover platform lowered 0..1
        double lockDoor = 0.0, lockOut = 0.0, lockMast = 0.0, lockDown = 0.0;   // main airlock crew lift (TantraLift)
        double bayDoors = 0.0;    // anamezon port doors 0..1
        bool trapHidden[4] = {};  // empty slot
        double liftY[2] = {tantra::mesh::kLiftY0, tantra::mesh::kLiftY0};  // fork heads (cassette centre, ship y)
        double strut[7] = {};     // ankle struts unloaded 0..1 (0 blade port, 1 starboard, 2..5 stern legs, 6 kangaroo)
        double bladeLag[2] = {};  // drive lag: that blade is shorter than commanded by this [m] (port, starboard)
    };

    TantraGear(VESSEL* v, UINT mesh);
    ~TantraGear();

    void Apply(const tantra::CarriagePose& pose, double sCG, const Extras& ex);

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
