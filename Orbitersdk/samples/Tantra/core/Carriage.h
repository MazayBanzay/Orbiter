// Tantra core: undercarriage and erection sequence (T9, Tantra_Design/DESIGN_LOCAL.md 2026-10-02, docs/T9_PLAN.md).
// No Orbiter dependencies.
//
// Three gear groups:
//   * two blade legs («лопасти») hanging from trunnion carriages on a flank track. Lying and lifting, the
//     carriages are PARKED at the aft end of the track (s 53.4), the CG is ahead of them and the ship stands as a
//     statically determinate tripod on the blades and the kangaroo leg - the trunnion drives hold no pitch;
//   * the kangaroo leg out of a belly pocket (hip s 99.4): thigh, fork knee bending aft, telescopic shin, ball
//     ankle with an umbrella foot 12 m ahead of the hip. It takes (sCG - s_track) / (s_foot - s_track) of the weight;
//   * four stern legs on the nacelles: four sections each, umbrella feet on R 36 m; they carry the standing ship.
// Erection progress P runs 0 (lying) .. 6 (standing on the stern):
//   0-1 lift on the blades to the standing height (kangaroo foot fixed on the ground);
//   1-2 trunnions run forward under the CG (the kangaroo unloads), kangaroo folds into its pocket;
//   2-3 turn 90 deg about the trunnions (drive moment = weight x CG error only);
//   3-4 stern legs swing out, run down to the ground, feet open;  4-5 load to the stern legs;
//   5-6 blade feet fold flat, blades shorten, carriages run to the stow station and into the pockets.
// Lowering is the same path backwards. In flight the gear is stowed; deploying it prepares either the lying set
// (blades + kangaroo) or the standing set (stern legs) for a tail-first landing. The lying height (hull axis 31.4 m)
// is the trap loading height: there is no separate loading lift any more.
#pragma once
#include <algorithm>

namespace tantra {

struct Vec3 { double x = 0, y = 0, z = 0; };

struct CarriageGeometry {       // ship frame: x starboard, y up, z along the axis; z = s - sCG
    double restAxisH = 24.0;    // hull axis above ground while lying [m]
    double standClear = 22.5;   // stern plane (s 0) above ground when standing (marching cup lip 16 m up) [m]
    double columnX = 19.0;      // hip plane of the blade legs (deployed)
    double footR = 9.8;         // blade umbrella radius (rim on the ground)
    double footH = 3.8;         // ankle above the ground (blade and stern cup feet: post 3.2 + cone 0.6)
    double kangFootH = 2.1;     // kangaroo ankle above the ground
    double legMin = 30.5;       // hip -> ankle, stages in
    double legMax = 87.5;       // hip -> ankle, stages out
    double trackS0 = 53.4, trackS1 = 71.6;  // hip (trunnion) track: parked at S0 while lying; CG range
    double stowS = 79.0;        // hip station with the leg stowed (track continues to it)
    double cgError = 0.3;       // CG off the trunnion axis during the turn (real ship: never exactly on it) [m]
    double cgSensor = 0.02;     // what the drive-moment sensing leaves of it [m]
    double cgTrackTime = 4.0;   // the trunnions follow the measured CG with this time constant [s]
    double standR = 36.0;       // radius of the stern feet circle
    Vec3 standFoot[4] = {};     // stern feet while standing (x, y; z unused), set by the adapter
    double sternFootR = 8.9;
    // kangaroo leg
    double kangHipS = 99.4, kangHipY = -5.78;   // hip axis: station, height over the hull axis
    double kangThigh = 10.0, kangShinMin = 10.0, kangShinMax = 64.0;
    double kangKneeE = 5.0;     // knee off the hip-foot line (aft) [m]
    double kangFootFwd = 12.0;  // foot hub ahead of the hip (lying) [m]
    double kangFootR = 6.0, kangHipMaxDeg = 100.0;
    // belly contacts with the gear stowed
    double bellyY = -7.28, bellySNose = 120.0, bellySTail = 8.0, bellyX = 10.0;
};

struct CarriagePose {
    double theta = 0.0;         // ship pitch against the ground [rad]
    double trunnionH = 31.6;    // CG (ship origin) height above ground [m]
    double standDrop = 0.0;     // standing lower than nominal by this (blade reach): stern legs that much shorter [m]
    double tuck = 0.0;          // crests folded, fin down, pods in: 0..1
    // blade legs
    double slideOut = 0.0;      // hips out of the pockets 0..1
    double hipS = 79.0;         // hip station on the track [m]
    double mastLen = 30.5;      // hip -> ankle [m]
    double mastPitch = 1.5708;  // blade against the hull: 0 hanging down, pi/2 along it, aft [rad]
    double footFold = 1.0;      // blade feet: 0 open, 1 folded flat into the flank
    // stern legs
    double legStand = 0.0;      // swing + extension to the standing pose 0..1
    double legRail = 1.0;       // foot hub up the leg (stowed) 1 .. at the ankle 0
    double legFold = 1.0;       // stern feet: 1 bundle, 0 open
    // kangaroo leg (rig states 0..1, see MeshLayout: door, hip, knee, ext, foot, fold)
    double kangDoor = 0.0, kangHip = 0.0, kangKnee = 0.0, kangExt = 0.0, kangFoot = 0.0, kangFold = 1.0;
    // loads and contacts
    double columnShare = 0.0;   // weight carried by the blades (+ kangaroo) 0..1 (rest: stern legs)
    double kangShare = 0.0;     // of the blade+kangaroo share, the part on the kangaroo foot
    static constexpr int kMaxTouch = 8;
    Vec3 touch[kMaxTouch];      // ground contacts, all in one plane; the first three: "up" = (p3-p1) x (p2-p1)
    int nTouch = 3;
    bool onColumns = false;     // the ship moves on the blades alone (lift end, turn)
    bool tripod = false;        // lying / lifting on blades + kangaroo
};

class Carriage {
public:
    enum class FlightSet { Level = 0, Standing = 1 };

    void SetGeometry(const CarriageGeometry& g) { geo_ = g; }
    const CarriageGeometry& Geometry() const { return geo_; }

    // Commands. Erect/lower only on the ground with the gear down; gear only when lying (P = 0)
    // or standing (P = 6).
    bool CommandErect(bool up, bool landed);
    // T9: the lying height is the loading height - kept for the port code, always "already there".
    bool CommandLoadHeight(bool, bool) { return false; }
    bool AtLoadHeight() const { return p_ <= 0.0 && gear_ >= 1.0; }
    // Airborne, a stow command first finishes the carriage motion to its nearer end, then stows the gear.
    bool CommandGear(bool down, bool landed = true);
    void SetFlightSet(FlightSet s) { if (gear_ <= 0.0) set_ = s; }
    void SetPort(bool on) { port_ = on; }       // port: the stern stands on a maglev table, no legs

    // dt: step [s] (the caller limits it so touchdown points move gradually); sCG: current CG station [m].
    void Update(double dt, double sCG);

    const CarriagePose& Pose() const { return pose_; }
    double Progress() const { return p_; }
    double Target() const { return pT_; }
    double Gear() const { return gear_; }
    bool GearDown() const { return gearT_ > 0.5; }
    FlightSet Set() const { return set_; }
    bool Port() const { return port_; }
    double CgResidual() const { return (std::max)(geo_.cgSensor, geo_.cgError - cgTrim_); }   // CG off the trunnions now [m]
    bool Busy() const { return p_ != pT_ || gear_ != gearT_; }
    bool Standing() const { return p_ >= 6.0 || (p_ <= 0.0 && set_ == FlightSet::Standing && gear_ > 0.0); }
    int Phase() const;           // 0 lying, 1..6 in progress / standing (7)

    // Persistence.
    void Load(double p, double pT, double gear, double gearT, int set, int port);
    void Save(double& p, double& pT, double& gear, double& gearT, int& set, int& port) const;

    // Quasi-static loads for the displays.
    struct Loads { double columnEach, kangaroo, legs, driveMoment, columnFL2; };
    Loads Statics(double weight, double cgError) const;

    // Kangaroo leg rig states for a hip hipH above the ground and the foot fwd metres ahead (planar IK; the foot
    // stays level). Shared with the mesh generator (gen_mesh.kang_states).
    static void KangarooIK(const CarriageGeometry& g, double hipH, double fwd, CarriagePose& o);

private:
    void BuildPose(double sCG);

    CarriageGeometry geo_;
    CarriagePose pose_;
    double p_ = 0.0, pT_ = 0.0;
    double cgTrim_ = 0.0;       // trunnion correction found from the drive moment on the blades (CG tracking) [m]
    double gear_ = 1.0, gearT_ = 1.0;
    FlightSet set_ = FlightSet::Level;
    bool port_ = false;
    double sCG_ = 58.0;
};

}  // namespace tantra
