// Tantra: the main airlock crew lift to the ground (port flank, s 129; mesh and rig in tools/gen_mesh.py:
// anims airlock_door, airlock_out, airlock_mast, airlock_down).
// Lowering: the hull door swings out, the four-stage telescopic arm carries the closed cabin (the airlock chamber itself)
// 4.4 m out, the guide mast runs down to the ground, the cabin rides down it with the people. Raising: the same backwards.
// Only with the ship lying on its gear (axis horizontal): the mast and the descent follow the real height of the door
// sill above the ground. Its foot is where the crew steps off and boards.
#pragma once
#include "Orbitersdk.h"

class TantraLift {
public:
    // Command: true = lower to the ground, false = raise and stow. Returns false if nothing to do.
    bool Command(bool lower);
    // dt: step [s]; allowed: lying on the gear, the carriage at rest; groundY: ground under the lift in the ship frame.
    void Update(double dt, bool allowed, double groundY);

    double Door() const { return door_; }
    double Out() const { return out_; }
    double Mast() const { return mast_; }
    double Down() const { return down_; }
    bool Lowering() const { return lower_; }
    bool Stowed() const { return door_ <= 0.0 && out_ <= 0.0 && mast_ <= 0.0 && down_ <= 0.0; }
    bool AtGround() const;                         // platform down on the ground, the people can step off
    bool Moving() const;
    double CabSpeed() const { return vCab_; }       // m/s, + down
    double CabHeight() const;                       // the cabin floor above the ground, m
    VECTOR3 Foot() const;                          // ship frame x, y where the crew steps off and boards (z: station kAirlockS)
    const char* Stage() const;                     // short status for the HUD

    void Save(FILEHANDLE scn) const;
    bool Load(const char* line);                   // "LIFT door out mast down lower"

private:
    double door_ = 0.0, out_ = 0.0, mast_ = 0.0, down_ = 0.0;
    double mastT_ = 1.0, downT_ = 1.0, groundY_ = -24.0;
    double vMast_ = 0.0, vCab_ = 0.0;                // m/s
    bool lower_ = false;
};
