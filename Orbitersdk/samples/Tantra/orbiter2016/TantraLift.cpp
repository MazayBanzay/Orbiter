#include "TantraLift.h"

#include "MeshLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace m = tantra::mesh;

namespace {
// High-speed lift (user: «лифт скоростной»): the door and the arm are quick, the mast and the cabin run fast with a
// comfortable acceleration (0.15 g) and brake to a stop at the ground / in the cell.
const double kDoorTime = 2.0;      // s: hull door panel swings out
const double kArmTime = 3.0;       // s: the telescopic arm carries the cabin 4.4 m out
const double kMastSpeed = 6.0;     // m/s: guide mast runs down to the ground
const double kCabinSpeed = 5.0;    // m/s: the cabin with the people
const double kAccel = 1.5;         // m/s^2

double Toward(double v, double t, double rate) { return v < t ? (std::min)(t, v + rate) : (std::max)(t, v - rate); }

// one axis in metres with a speed limit, acceleration and braking to the target
double Drive(double x, double target, double& v, double vmax, double dt) {
    const double rem = target - x;
    if (std::fabs(rem) < 1e-4) { v = 0.0; return target; }
    const double dir = rem > 0.0 ? 1.0 : -1.0;
    const double vBrake = std::sqrt(2.0 * kAccel * std::fabs(rem));
    const double vWant = dir * (std::min)(vmax, vBrake);
    v = Toward(v, vWant, kAccel * dt);
    if (v * dir <= 0.0) v = dir * (std::min)(0.05, std::fabs(rem) / dt);   // never stall short of the target
    const double nx = x + v * dt;
    if ((target - nx) * dir <= 0.0) { v = 0.0; return target; }
    return nx;
}
}  // namespace

bool TantraLift::Command(bool lower) {
    if (lower == lower_ && (lower ? AtGround() : Stowed())) return false;
    lower_ = lower;
    return true;
}

void TantraLift::Update(double dt, bool allowed, double groundY) {
    if (dt <= 0.0) return;
    groundY_ = groundY;
    // mast and descent follow the real ground: the mast from its pivot to the ground, the cabin floor down to the ground
    const double mastRun = m::kLockMastFull - m::kLockMastStub;               // extension at state 1
    mastT_ = (std::max)(0.0, (std::min)(1.0, (m::kLockMastTop - m::kLockMastStub - groundY) / mastRun));
    downT_ = (std::max)(0.0, (std::min)(1.0, (m::kLockDeckY - groundY) / m::kLockDrop));
    if (!allowed) {                        // the ship may not move with the lift out: it comes home first;
        if (Stowed()) return;              // stowed, it just waits (a command given meanwhile is kept)
        lower_ = false;
    }
    const double dDoor = dt / kDoorTime, dOut = dt / kArmTime;
    double mastM = mast_ * mastRun, downM = down_ * m::kLockDrop;
    if (lower_) {                          // door -> arm -> mast -> cabin, each after the previous
        if (door_ < 1.0) door_ = Toward(door_, 1.0, dDoor);
        else if (out_ < 1.0) out_ = Toward(out_, 1.0, dOut);
        else if (std::fabs(mast_ - mastT_) > 1e-6) mastM = Drive(mastM, mastT_ * mastRun, vMast_, kMastSpeed, dt);
        else downM = Drive(downM, downT_ * m::kLockDrop, vCab_, kCabinSpeed, dt);
    } else {                               // backwards
        if (down_ > 0.0) downM = Drive(downM, 0.0, vCab_, kCabinSpeed, dt);
        else if (mast_ > 0.0) mastM = Drive(mastM, 0.0, vMast_, kMastSpeed, dt);
        else if (out_ > 0.0) out_ = Toward(out_, 0.0, dOut);
        else door_ = Toward(door_, 0.0, dDoor);
    }
    mast_ = mastM / mastRun;
    down_ = downM / m::kLockDrop;
}

bool TantraLift::AtGround() const { return lower_ && down_ > 0.0 && down_ >= downT_ - 1e-6 && mast_ >= mastT_ - 1e-6; }

bool TantraLift::Moving() const { return !(lower_ ? AtGround() : Stowed()); }

VECTOR3 TantraLift::Foot() const {
    // in front of the cabin door (hull side), on the ground; z = 0: the ship puts its station (kAirlockS)
    return _V(m::kLockX1 - m::kLockOut + 1.0, groundY_ + 0.93, 0.0);
}

const char* TantraLift::Stage() const {
    if (Stowed()) return "stowed";
    if (AtGround()) return "at the ground";
    if (lower_) return door_ < 1.0 ? "door opening" : out_ < 1.0 ? "arm out" : mast_ < mastT_ ? "mast down" : "cabin down";
    return down_ > 0.0 ? "cabin up" : mast_ > 0.0 ? "mast up" : out_ > 0.0 ? "arm in" : "door closing";
}

void TantraLift::Save(FILEHANDLE scn) const {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.4f %.4f %.4f %.4f %d", door_, out_, mast_, down_, lower_ ? 1 : 0);
    oapiWriteScenario_string(scn, const_cast<char*>("LIFT"), buf);
}

bool TantraLift::Load(const char* line) {
    int lw = 0;
    if (std::sscanf(line, "%lf %lf %lf %lf %d", &door_, &out_, &mast_, &down_, &lw) < 4) return false;
    lower_ = lw != 0;
    return true;
}
