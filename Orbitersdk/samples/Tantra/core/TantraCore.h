// Tantra core: Tantra_CORE - the ship's energy core as a real system (the user, 2026-10-04: «Всю бутафорию перенести в реальный
// модуль "Tantra_CORE". Сделать все показания - реальной реакцией на параметры ядра. Физически ядро - точка в Тантре, и при
// перегреве или кинетических ударах - закономерный итог.» Tantra_Design/refine/CORE.md). No Orbiter dependencies
// (tests/core_test.cpp).
//
// The cup and the stern stay the plant model's (core/Plant: the field, the power, the limiter, the stern's heat, the random
// failures); around it, the nodes the screen used to invent, each with its state, its values and its reason:
//   ВЭУ (the ship's 2 GW p-11B direct-conversion power plant), НАКОПИТЕЛЬ ПОЛЯ (7e14 J), ТОПЛИВО, ПОДАЧА КАПСУЛ, ИОННЫЙ ТРИГГЕР,
//   ОБМОТКА (REBCO: the current, the critical current, the margin), КРИОГЕНИКА (the heat on the windings against the cooling),
//   РАБОЧАЯ МАССА, НАСОСЫ, РУБАШКА (in / out of the reaction mass), ЧАША, КОРМА, РАДИАТОРЫ (the crests and the fin).
// The core is a point in the ship (the stern block, station kStation from the stern): it takes the jolts of the felt g, the hull
// impacts (core/Damage: the equipment's integrities from the impact's g) and the stern zone's crushing. What breaks follows
// from it, sent to the plant as its own failures (Plant::Inflict) and to the ship's hull (Damage::Inflict) - nothing scripted.
//
// Consequences (design numbers: DESIGN_LOCAL «ВЭУ», «Модель повреждений»; «допущение» marks mine, the user decides):
//  * a jolt or an impact over kPlasmaTripG: the ВЭУ's plasma goes out (safe), the ship runs from the store, restart kVeuRestart s;
//  * over kCoilJoltG: the conductor moves - a quench; a current over the critical current (warm windings, a field over ~16 T): a quench;
//  * the cradles (ВЭУ, store) at their limit / damaged / torn off: full / half power / lost; the store dumps when damaged, through
//    the cup's nozzle (or, without the cup, its own loads: slow, the stern heats); torn off without the path: the ship is lost;
//  * the installation damaged / torn off: the windings and the trigger hurt / the march lost until a station;
//  * the tanks and pumps: their flow caps the thrust (argon, iron; the products need none);
//  * the crests and the fin broken: their radiator area is gone; the cup hurt: the jacket's channels;
//  * the stern crushed up to the core's station: the core is destroyed - the store discharges unless (nearly) empty;
//  * no power at all (ВЭУ out, the store empty): the cryo and the pumps stop - a quench, no reaction mass.
#pragma once

#include <string>

namespace tantra::plant { class Plant; struct Output; }
namespace tantra::damage { class Model; }

namespace tantra::tcore {

enum State { kOff = 0, kReady, kRun, kLimit, kFault, kLost };   // a node's lamp: выкл / готов / работа / предел / отказ / потерян
struct Node { int state = kOff; const char* why = ""; };       // why: the reason in Russian (a literal)

// what the ship hands the core every step (after the plant's and the damage model's steps)
struct Inputs {
    double dt = 0.0;
    plant::Plant* plant = nullptr;           // the core sends it its consequences (Inflict, Note, SetFlowCap)
    const plant::Output* out = nullptr;      // the plant's output this step
    damage::Model* dmg = nullptr;            // the integrities, the impacts, the crushing; the ship's loss (null: intact)
    bool dmgEnabled = true;                  // Orbiter's damage setting (passed to Damage::Inflict)
    double structG = 0.0;                    // the felt g of the structure now
    bool settling = false;                   // the suspension settles after a load: no jolts
    bool crestsOut = true;                   // the crests and the fin out (the radiators)
    double massLeft = -1.0;                  // kg of the reaction mass in use left in the tanks (< 0: not tracked)
    double shipLoad = 50e6;                  // W the rest of the ship draws (life support, lights, the bridge, the traps)
    double sinterLoad = 0.0;                 // W the feet draw sintering the ground (from the store)
};

struct Snapshot {
    Node veu, store, fuel, feed, trigger, coil, cryo, mass, pump, jacket, cup, stern, rad;
    // ВЭУ
    double veuMax = 0.0, veuPower = 0.0, veuHealth = 1.0, veuRestart = 0.0, veuFuelDay = 0.0;   // W, W, 0..1, s, kg a day
    bool veuPlasma = true;
    // НАКОПИТЕЛЬ ПОЛЯ
    double storeE = 0.0, storeMax = 0.0, storeIn = 0.0, storeOut = 0.0, storeHealth = 1.0, dumpLeft = 0.0;   // J, J, W, W, 0..1, s
    // ТОПЛИВО, ПОДАЧА КАПСУЛ
    double fuelKg = 0.0, fuelFlow = 0.0, capsHz = 0.0, capsHzMax = 0.0, capsE = 0.0, dipRisk = 0.0;   // kg, kg/s, Hz, Hz, J, /min
    bool dip = false;                        // a series of misfires now (the plant's failure)
    // ИОННЫЙ ТРИГГЕР
    double gainQ = 0.0, pulseE = 0.0, trigPower = 0.0, drivers = 1.0;    // -, J, W, 0..1
    // ОБМОТКА
    double B = 0.0, Bset = 0.0, Bcap = 0.0, I = 0.0, Ic = 0.0, margin = 0.0, coilT = 20.0, coils = 1.0;   // T, T, T, A, A, -, K, 0..1
    // КРИОГЕНИКА
    double cryoCap = 0.0, cryoLoad = 0.0, cryoPower = 0.0, cryoHealth = 1.0;   // W at 20 K, W at 20 K, W of electricity, 0..1
    // РАБОЧАЯ МАССА, НАСОСЫ
    int massKind = 0; double massLeft = -1.0, mdot = 0.0, mdotMax = 0.0, pumpPower = 0.0, pumpHealth = 1.0;   // kg, kg/s, kg/s, W
    // РУБАШКА
    double tIn = 87.0, tOut = 87.0, regen = 0.0, carry = 0.0, vapour = 0.0, jacketHealth = 1.0;   // K, K, W, J/kg, share, 0..1
    // ЧАША, КОРМА
    double thrust = 0.0, cupHealth = 1.0, sternT = 300.0, heatIn = 0.0;   // N, 0..1, K, W
    // РАДИАТОРЫ
    double radiated = 0.0, radArea = 0.0, radHealth = 1.0; bool radOut = true;   // W, m^2 working, 0..1
    // the core as a point
    double jolt = 0.0, lastShockG = 0.0, sternCrush = 0.0;   // g over the sustained level, the last impact's g, m crushed from the stern
    bool destroyed = false;                  // the stern crushed up to the core
};

class Core {
public:
    void Step(const Inputs& in);
    const Snapshot& S() const { return s_; }
    std::string Save() const;                // "storeE veuRestart dumpLeft dumpSlow destroyed"
    void Load(const std::string& s);
    void Repair();                           // a station

    // the design numbers
    static constexpr double kStation = 12.0;          // m from the stern (допущение: the ВЭУ, the store and the windings together)
    static constexpr double kVeuMax = 2.0e9;          // W
    static constexpr double kVeuFuelDay = 3.0;        // kg of boron a day at full power
    static constexpr double kVeuRestart = 30.0;       // s (допущение)
    static constexpr double kStoreMax = 7.0e14;       // J
    static constexpr double kStoreCharge = 1.0e9;     // W the ВЭУ charges it at, at most (допущение)
    static constexpr double kFieldE = 7.0e9;          // J the cup's field holds at 12.1 T (the store gives it, 90 % comes back)
    static constexpr double kDumpFast = 5.0, kDumpSlow = 60.0;   // s: through the cup's nozzle / its own loads (допущение)
    static constexpr double kDumpHeatFast = 1e-4, kDumpHeatSlow = 1e-3;   // the share of the dump the stern takes (допущение)
    static constexpr double kDischarge = 1.0e13;      // J: more than this torn loose without a dump path - the ship is lost
    static constexpr double kCapsMass = 0.010;        // kg a capsule (допущение: 314 Hz at 100 %)
    static constexpr double kCapsHzMax = 700.0;       // the press's top rate, over 200 % of the plant (допущение)
    static constexpr double kGainQ = 120.0;           // the cascade's gain (the mockup)
    static constexpr double kINom = 212e3;            // A at 12.1 T (the mockup)
    static constexpr double kIc20 = kINom / 0.58;     // A: the critical current at 20 K, 12.1 T (42 % margin at the nominal)
    static constexpr double kTc = 92.0;               // K: REBCO
    static constexpr double kIcB = 0.5;               // Ic ~ B^-kIcB in the field (допущение)
    static constexpr double kCryoCap = 50e3;          // W at 20 K (допущение)
    static constexpr double kCryoBase = 20e3;         // W at 20 K: conduction, the current leads (допущение)
    static constexpr double kCryoPerK = 2.5e3;        // W at 20 K per K the windings sit over 20 K (допущение)
    static constexpr double kCryoCOP = 60.0;          // W of electricity per W taken at 20 K
    static constexpr double kPumpMargin = 1.5;        // the pumps' top flow over the field's nominal argon flow (допущение)
    static constexpr double kPumpDp = 10e6;           // Pa (допущение)
    static constexpr double kPlasmaTripG = 3.0;       // the ВЭУ's plasma goes out over this jolt
    static constexpr double kCoilJoltG = 10.0;        // a jolt that moves the conductor: a quench (допущение)
    static constexpr double kRadCrest = 1000.0, kRadFin = 700.0;   // m^2 of the 2700 (допущение)

private:
    void Note(plant::Plant* p, const char* ru, const char* en, int level);
    void Quench(plant::Plant* p, const char* ru, const char* en);
    void StartDump(const Inputs& in, bool torn);
    Snapshot s_;
    bool init_ = false, destroyed_ = false, dumpSlow_ = false, unpowered_ = false;
    double storeE_ = kStoreMax * 0.9, veuRestart_ = 0.0, dumpLeft_ = 0.0, dumpRate_ = 0.0;
    double gSlow_ = 1.0, lastB_ = -1.0;
    int serial_ = 0;
    double seen_[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};    // the integrities last seen: ВЭУ, store, plant, argon, cup, crests, fin, -, -
};

}  // namespace tantra::tcore
