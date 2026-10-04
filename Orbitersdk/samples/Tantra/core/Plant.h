// Tantra core: the planetary power plant (ion-trigger cascade fusion in magnetic-nozzle cups). No Orbiter
// dependencies. Model and numbers: Tantra_Design/tantra_plant.html, tantra_plant_screen.html, DESIGN_LOCAL.md
// «Силовая установка». Failures and limits: Config/Tantra/plant.cfg (text, read at start).
//
//  * Thrust of a cup is capped by the field pressure: F <= B^2/2mu0 x area (12.1 T x 15.2 m^2 = 886 MN, the march).
//  * Below the field cap, F = 2 P_jet / v: the reaction mass sets the exhaust speed (argon 30 km/s in the air,
//    iron 300 km/s - lower when the field allows more thrust -, the bare fusion products up to ~10 000 km/s).
//  * The power is set in % of the nominal: what the field holds. The limiter keeps it there, keeps 12.1 T, allows
//    only argon in the air and cuts the power when the stern runs hot; the commander removes it by hand (twice).
//  * Over the limits: the stern heats gradually (the plasma through an opened field, the jet off the ground, the
//    mixing layer in the air, the reaction's own radiation), and every part over its safe limit risks a failure
//    per minute that grows exponentially with the excess. Failures leave lasting damage until a station.
//  * ПУСК / СТОП as on the mockup's screen: cryo check -> the field up (0.8 T/s) -> the trigger charged -> the capsules
//    fed -> on the run. A quench drops the field; the windings cool, then the plant needs ПУСК again.
#pragma once

#include <deque>
#include <string>
#include <vector>

namespace tantra::plant {

enum Mass { kArgon = 0, kIron = 1, kProducts = 2 };
enum Cause { kField = 0, kPower = 1, kHeat = 2, kCauseCount };
enum Effect { kQuench, kCoils, kCoilCap, kDip, kDrivers, kJacket, kRadiators, kCryo };
enum Stage { kStOff = 0, kStCryo, kStFieldUp, kStTrigger, kStFeed, kStRun, kStQuench, kStGone };

struct Failure {
    int cause = kField;
    double weight = 1.0;
    int effect = kQuench;
    double value = 1.0;          // the factor left of the part (coils, drivers, jacket, radiators, cryo) or the cap
    double heatJump = 0.0;       // K added to the stern at once
    bool quench = false;         // the field drops too
    std::string ru, en;
};

struct Config {
    double chi = 1e-5;                       // share of the reaction energy in penetrating radiation («clean» ion trigger)
    double risk0 = 2e-4, riskK = 7.0;        // failures per minute at an excess e: risk0 * (exp(k e) - 1)
    double tSafe = 800, tBoil = 1100, tSoft = 1400, tBreach = 1700, tLost = 2300;   // stern temperatures [K]
    double sternStore = 1.0e12;              // J the stern structure takes from 300 to 1700 K (~1 kt)
    double leak = 0.0045;                    // overpower heat: P_jet x leak x (beta - 1)^2
    double reflect[3] = {0.002, 0.004, 0.01};// jet power back-scattered off the ground at the stern (argon, iron, products)
    double airLayer[3] = {0.0, 0.002, 0.003};// jet power the mixing layer shines on the hull at sea level
    double quenchCool = 20.0;                // s for the windings to cool after a quench
    double limiterHot = 250.0;               // K over the safe temperature at which the limiter has cut the power to zero
    std::vector<Failure> fails;
    // the text of plant.cfg (already in the program's charset); missing keys keep the defaults; an empty list of
    // failures gets the built-in one
    void Parse(const std::string& text);
    void Defaults();
};

struct Env {
    bool air = false;            // in an atmosphere (argon only there, the mixing layer)
    double rho = 0.0;            // air density [kg/m^3]
    double sternH = -1.0;        // stern above the ground [m]; < 0 - far from any ground
    double level = 0.0;          // the march thruster level Orbiter runs it at (0..1)
    bool crestsOut = true;       // the crests and the fin out: the radiators work (folded - a tenth)
    double noseT = 0.0;          // the nose skin [K] - for the screen's trend only
};

struct Output {
    double maxThrust = 0.0;      // N at level 1 (0 unless on the run)
    double exhaust = 3.0e4;      // m/s
    int mass = kArgon;           // the reaction mass in use
    const char* limit = "поле";  // what caps the thrust: поле / мощность / тепло
    double fusion = 0.0;         // W at the current level
    double heatIn = 0.0, cooling = 0.0;
    double sources[5] = {};      // radiation, overpower, ground, air, plasma on the cup [W]
    double excess[kCauseCount] = {};
    double riskPerMin[kCauseCount] = {};
    // the screen's reckoning
    double cupThrust = 0.0;      // what the cup gives at level 1 in this state, on the run or not [N]
    double fieldThrust = 0.0;    // what the field holds: B^2/2mu0 x A [N]
    double powerThrust = 0.0;    // what the set power gives at this exhaust speed: 2 eta P / v [N]
    double thrust = 0.0;         // N now (at the level)
    double mdot = 0.0;           // kg/s now
    double jetPower = 0.0;       // W now
    double regen = 0.0, radiated = 0.0;   // the heat the reaction mass carries away, the crests shed [W]
    double beta = 1.0;           // the power asked over what the field holds
    double level = 0.0;
};

struct Event { std::string ru, en; bool bad = true; int level = 2; };   // bad: a failure or the heat; level 0 ok, 1 warning, 2 alarm
struct LogLine { double t; std::string ru, en; int level; };
struct TrendPt { double t, F, B, T, nose; };

class Plant {
public:
    explicit Plant(const Config& c = Config()) : cfg_(c) { if (cfg_.fails.empty()) cfg_.Defaults(); }
    void Configure(const Config& c) { cfg_ = c; if (cfg_.fails.empty()) cfg_.Defaults(); }
    const Config& Cfg() const { return cfg_; }

    // controls
    bool FieldStep(double dT);                    // the field setpoint; false: the limiter holds it at 12.1 T
    bool PowerStep(double dPct);                  // the power in % of the nominal; false: the limiter holds it at 100
    void CycleMass();                             // auto -> argon -> iron -> products -> auto
    void SetMassMode(int m) { massMode_ = m < -1 || m > kProducts ? -1 : m; }   // -1 auto, else Mass
    // the limiter: on at once; off only on the second press within 4 s. Returns the message to show.
    const char* LimiterPress(double now, bool russian);
    const char* StartStop(bool russian);          // ПУСК (from off) / СТОП (on the way or on the run)
    void Note(const std::string& ru, const std::string& en, int level) { Log(ru, en, level); }   // a control's message
    void SetProductsExhaust(double v) { productsV_ = v; }

    // one step: the sequence, the field ramps, the stern heats and cools, the parts may fail (rnd: uniform 0..1 numbers)
    Output Step(double dt, const Env& e, double (*rnd)());

    bool Limiter() const { return lim_; }
    bool LimiterArmed(double now) const { return lim_ && now - armT_ <= 4.0; }   // the first press waits for the second
    double FieldSet() const { return Bset_; }
    double Field() const { return B_; }
    double PowerPct() const { return P_; }
    int MassMode() const { return massMode_; }    // -1 auto, else Mass
    double SternT() const { return T_; }
    double CoilT() const { return coilT_; }
    bool Quenched() const { return stage_ == kStQuench; }
    bool Lost() const { return lost_; }
    int StageNow() const { return stage_; }
    double StageTime() const { return stageT_; }
    bool Running() const { return stage_ == kStRun; }
    bool CoilsLost() const { return coilsLost_; }
    bool Refused() const { return refused_; }     // the limiter put argon in place of the mass asked for (in the air)
    double Fuel() const { return fuel_; }         // kg of p-11B capsules
    double Damage(int effect) const;              // the factor left of coils / drivers / jacket / radiators / cryo
    std::vector<Event> TakeEvents() { std::vector<Event> e; e.swap(events_); return e; }
    const std::deque<LogLine>& Journal() const { return journal_; }   // newest first
    const std::deque<TrendPt>& Trend() const { return trend_; }       // the last 60 s, oldest first
    double Clock() const { return now_; }         // s of simulation since the start

    // persistence: "Bset P lim mode T coilT coils drivers jacket radiators cryo lost stage fuel"
    std::string Save() const;
    void Load(const std::string& s);
    void Repair();                                 // a station: all lasting damage gone

    static constexpr double kBNom = 12.1;
    static constexpr double kFusionMax = 2.2e14;  // W fusion at 100 % of the plant
    static constexpr double kEFus = 7.0e13;       // J per kg of p-11B
    static constexpr double kFuelFull = 2.0e6;    // kg of capsules in the stores (2 kt)
    static double FieldThrust(double B, double area);
    static double BRupture();

private:
    void Fail(const Failure& f);
    void Log(const std::string& ru, const std::string& en, int level, bool fault = false);   // fault: a failure or the heat, not a control
    void SetStage(int s) { stage_ = s; stageT_ = 0.0; }
    Config cfg_;
    double Bset_ = kBNom, B_ = kBNom, P_ = 100.0, productsV_ = 7.0e6;
    int massMode_ = -1;
    bool lim_ = true, lost_ = false, refused_ = false, coilsLost_ = false;
    int stage_ = kStRun;                             // a ship comes with its plant on the run
    double stageT_ = 0.0;
    double armT_ = -1e9, now_ = 0.0;
    double T_ = 300.0, coilT_ = 20.0, dipT_ = 0.0, fuel_ = kFuelFull;
    double coils_ = 1.0, drivers_ = 1.0, jacket_ = 1.0, radiators_ = 1.0, cryo_ = 1.0;
    bool crossed_[4] = {};
    std::vector<Event> events_;
    std::deque<LogLine> journal_;
    std::deque<TrendPt> trend_;
};

}  // namespace tantra::plant
