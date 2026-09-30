// Tantra core: undercarriage and erection sequence («лафет»). No Orbiter dependencies,
// so the same logic serves the 2010 adapter and a later 2016 port.
//
// Two gear sets:
//   * carriage columns: two band masts on trunnion carriages at the CG line. They hold the
//     ship level, lift it and turn it about the trunnions (the CG), so the turning moment is
//     only m*g*(CG error) - no engines are used;
//   * four stern legs on long feet: the lower pair backs the stern while level, all four
//     carry the standing ship.
// Erection progress P runs 0 (level) .. 6 (standing):
//   0-1 prepare (crests tucked, pods sunk), 1-2 lift on the columns, 2-3 turn 90 deg,
//   3-4 stern legs out, 4-5 lower onto the legs, 5-6 masts reeled in, carriages home.
// In flight the gear is stowed; deploying it prepares either the level set (columns + lower
// legs) or the standing set (four stern legs) for a tail-first landing.
#pragma once

namespace tantra {

struct Vec3 { double x = 0, y = 0, z = 0; };

struct CarriageGeometry {       // ship frame: x starboard, y up, z along the axis; z = s - sCG
    double restAxisH = 20.0;    // hull axis above ground while level [m]
    double turnClear = 16.0;    // stern clearance while turning [m]
    double standClear = 12.0;   // stern above ground when standing on the legs [m]
    double columnX = 17.5;      // trunnion plane
    double footHalf = 13.0;     // half length of the column feet
    double standR = 21.0;       // radius of the stern feet circle
    double legRestX = 17.7;     // lower-leg feet while level (lateral), set by the adapter
    double legRestS = 5.0;      // their station s
    double bellyY = -9.5;       // belly contact points with the gear stowed
    double bellySNose = 110.0, bellySTail = 8.0, bellyX = 12.0;
    double footT = 1.2;         // foot thickness
    double mastMin = 0.3;       // reeled-in mast
};

struct CarriagePose {
    double theta = 0.0;         // ship pitch against the ground [rad]
    double trunnionH = 20.0;    // trunnion (CG) height above ground [m]
    double tuck = 0.0;          // crests folded / pods sunk, 0..1
    double lid = 0.0;           // flank lids open 0..1
    double slideOut = 0.0;      // carriages out of the hull 0..1
    double mastLen = 0.3;       // trunnion -> foot top [m]
    double mastPitch = 0.0;     // mast counter-rotation against the hull [rad]
    double legRest = 0.0;       // lower legs in the level pose 0..1
    double legStand = 0.0;      // all legs in the standing pose 0..1
    double columnShare = 0.0;   // weight carried by the columns 0..1 (rest: legs)
    Vec3 touch[3];              // Orbiter touchdown points, "up" = (p3-p1) x (p2-p1)
    bool onColumns = false;     // columns are the ground contact
};

class Carriage {
public:
    enum class FlightSet { Level = 0, Standing = 1 };

    void SetGeometry(const CarriageGeometry& g) { geo_ = g; }
    const CarriageGeometry& Geometry() const { return geo_; }

    // Commands. Erect/lower only on the ground with the gear down; gear only when level (P = 0)
    // or standing (P = 6).
    bool CommandErect(bool up, bool landed);
    // Loading height: the level ship is lifted on the columns (axis ~32 m) so that a trap
    // container fits under the belly for the anamezon port. P stops at kLoadP.
    static constexpr double kLoadP = 1.4;
    bool CommandLoadHeight(bool on, bool landed);
    bool AtLoadHeight() const { return p_ == kLoadP && pT_ == kLoadP; }
    // Airborne, a stow command first finishes the carriage motion to its nearer end (e.g. the ship
    // left the ground at the loading height), then stows the gear.
    bool CommandGear(bool down, bool landed = true);
    void SetFlightSet(FlightSet s) { if (gear_ <= 0.0) set_ = s; }
    void SetPort(bool on) { port_ = on; }       // port: the stern stands on a maglev table, no legs

    // dt: step [s] (the caller limits it so touchdown points move gradually);
    // sCG: current CG station [m] (trunnion follows it).
    void Update(double dt, double sCG);

    const CarriagePose& Pose() const { return pose_; }
    double Progress() const { return p_; }
    double Target() const { return pT_; }
    double Gear() const { return gear_; }
    bool GearDown() const { return gearT_ > 0.5; }
    FlightSet Set() const { return set_; }
    bool Port() const { return port_; }
    bool Busy() const { return p_ != pT_ || gear_ != gearT_; }
    bool Standing() const { return p_ >= 6.0 || (p_ <= 0.0 && set_ == FlightSet::Standing && gear_ > 0.0); }
    int Phase() const;           // 0 level, 1..6 in progress / standing (7)

    // Persistence.
    void Load(double p, double pT, double gear, double gearT, int set, int port);
    void Save(double& p, double& pT, double& gear, double& gearT, int& set, int& port) const;

    // Quasi-static loads for the displays.
    struct Loads { double columnEach, legs, driveMoment, columnFL2; };
    Loads Statics(double weight, double cgError) const;

private:
    void BuildPose(double sCG);

    CarriageGeometry geo_;
    CarriagePose pose_;
    double p_ = 0.0, pT_ = 0.0;
    double gear_ = 1.0, gearT_ = 1.0;
    FlightSet set_ = FlightSet::Level;
    bool port_ = false;
    double sCG_ = 38.0;
};

}  // namespace tantra
