// Tantra core: damage model. Heating by zones, dynamic pressure and loads on the exposed parts, g-load,
// touchdown impacts and leg overloads. No Orbiter dependencies (tests/damage_test.cpp).
//
// Heating: convective stagnation heating after Sutton-Graves, q = k sqrt(rho/Rn) V^3 (k = 1.7415e-4 SI
// for air), scaled per zone by its effective nose radius and exposure; the skin tends to its radiative
// equilibrium temperature with a thermal lag. Damage grows while a zone is over its material limit.
// Loads: the part's normal force from its aerodynamics against its rating; x1.5 breaks it at once.
#pragma once

namespace tantra::damage {

enum Part {
    kNose, kBelly, kCrestPort, kCrestStbd, kFin,
    kPod0, kPod1, kPod2, kPod3,
    kLegPort, kLegStbd, kSternLeg0, kSternLeg1, kSternLeg2, kSternLeg3, kKangLeg,
    kHangarDoors, kBayDoors, kMarchCup, kHull,
    kPartCount
};

enum Zone { kZoneNose, kZoneBelly, kZoneCrestEdge, kZoneFin, kZoneStern, kZoneGear, kZonePods, kZoneCount };

struct Flight {
    double rho = 0.0;        // air density [kg/m^3]
    double v = 0.0;          // airspeed [m/s]
    double mach = 0.0;
    double aoa = 0.0;        // [rad]
    double beta = 0.0;       // sideslip [rad]
    double gLoad = 0.0;      // felt (proper) acceleration of the structure, ground contact included [g]
    double ambientT = 250.0; // [K]
};

struct Exposure {            // 0 closed / stowed .. 1 open / out
    double crests = 1.0, fin = 1.0, pods = 0.0, gear = 0.0, hangar = 0.0, bays = 0.0, sternCups = 0.0;  // sternCups: marching cup out / anamezon irises open
};

struct Ground {
    bool contact = false;
    bool touchdown = false;  // contact began this step
    double vDown = 0.0;      // vertical speed at touchdown [m/s], positive down
    double vSoft = 3.0, vBreak = 6.0;  // the gear set's ankle struts take these (core/Legs Limits) [m/s]
    bool gearDown = false;
    double legRatio[7] = {}; // load / rating: blade port, stbd, stern legs 0..3, kangaroo
};

struct Event { int part; bool destroyed; const char* ru; const char* en; };

class Model {
public:
    Model();
    // enabled = Orbiter's damage setting: temperatures and loads are always computed, parts break only then.
    void Step(double dt, const Flight& f, const Exposure& x, const Ground& g, bool enabled);
    void Repair();

    double Integrity(int part) const { return integrity_[part]; }  // 1 intact .. 0 lost
    bool Lost(int part) const { return integrity_[part] <= 0.0; }
    bool Destroyed() const { return integrity_[kHull] <= 0.0 || integrity_[kNose] <= 0.0 || integrity_[kBelly] <= 0.0; }
    double Temperature(int zone) const { return temp_[zone]; }
    double Limit(int zone) const;
    double HeatFlux(int zone) const { return flux_[zone]; }       // [W/m^2]
    double CrestLoad(int side) const { return crestLoad_[side]; } // normal force / rating
    double FinLoad() const { return finLoad_; }
    double DynamicPressure() const { return q_; }

    int TakeEvents(Event* out, int max);  // part failures since the last call
    static const char* NameRu(int part);
    static const char* NameEn(int part);

    // persistence: integrities as "a b c ..." (kPartCount numbers)
    void Save(char* buf, int size) const;
    void Load(const char* s);

private:
    void Hurt(int part, double amount, bool enabled);
    double integrity_[kPartCount];
    double temp_[kZoneCount];
    double flux_[kZoneCount] = {};
    double crestLoad_[2] = {}, finLoad_ = 0.0, q_ = 0.0;
    Event events_[16];
    int nEvents_ = 0;
};

}  // namespace tantra::damage
