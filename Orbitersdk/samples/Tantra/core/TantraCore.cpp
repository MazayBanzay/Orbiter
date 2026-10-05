// Tantra core: Tantra_CORE, the ship's energy core - see TantraCore.h.
#include "TantraCore.h"

#include "Damage.h"
#include "Impact.h"
#include "Plant.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tantra::tcore {
namespace {

namespace pl = tantra::plant;
namespace dm = tantra::damage;

constexpr double kPi = 3.14159265358979323846;
constexpr double kAreaMarch = kPi * 2.2 * 2.2;     // m^2: the plant's march cup (core/Plant)
constexpr double kVArgon = 3.0e4;                  // m/s: argon's exhaust (core/Plant)
constexpr double kRhoMass[2] = {1400.0, 7874.0};   // kg/m^3: liquid argon, iron
constexpr double kPumpEta = 0.7;
constexpr double kRadAll = 2700.0;                 // m^2: the crests and the fin (core/Plant)

double Clamp01(double x) { return std::max(0.0, std::min(1.0, x)); }
// a part at its limit (integrity 0.85, core/Damage) still works fully; damaged (0.45) about half; torn off (0) not at all
double Health(double integrity) { return Clamp01(integrity / 0.85); }

// the reaction mass leaving the jacket with q J/kg taken: argon fed as a liquid at 87 K, iron as powder at 300 K
void Outlet(int mass, double q, double* tIn, double* tOut, double* vapour) {
    *vapour = 0.0;
    if (mass == pl::kArgon) {
        const double latent = 161e3, cpGas = 520.0;
        *tIn = 87.0;
        if (q < latent) { *tOut = 87.0; *vapour = q / latent; }
        else { *tOut = 87.0 + (q - latent) / cpGas; *vapour = 1.0; }
    } else if (mass == pl::kIron) {
        const double h1 = 450.0 * (1811.0 - 300.0), melt = 247e3, h2 = 820.0 * (3134.0 - 1811.0), boil = 6.09e6;
        *tIn = 300.0;
        if (q < h1) *tOut = 300.0 + q / 450.0;
        else if (q < h1 + melt) *tOut = 1811.0;
        else if (q < h1 + melt + h2) *tOut = 1811.0 + (q - h1 - melt) / 820.0;
        else if (q < h1 + melt + h2 + boil) { *tOut = 3134.0; *vapour = (q - h1 - melt - h2) / boil; }
        else { *tOut = 3134.0 + (q - h1 - melt - h2 - boil) / 450.0; *vapour = 1.0; }
    } else { *tIn = *tOut = 0.0; }
}

pl::Failure Hurt(int effect, double value, bool quench, double heatK, const char* ru, const char* en) {
    pl::Failure f;
    f.effect = effect; f.value = value; f.quench = quench; f.heatJump = heatK; f.ru = ru; f.en = en;
    return f;
}
constexpr int kNoEffect = -1;   // a failure that changes no factor of the plant (a message, the stern's heat)

}  // namespace

void Core::Note(pl::Plant* p, const char* ru, const char* en, int level) { p->Note(ru, en, level); }
void Core::Quench(pl::Plant* p, const char* ru, const char* en) { p->Inflict(Hurt(pl::kQuench, 1.0, true, 0.0, ru, en)); }

void Core::StartDump(const Inputs& in, bool torn) {
    pl::Plant* p = in.plant;
    // the fast path is the cup's magnetic nozzle: it needs the cup and the stern up to the core
    const bool cupPath = !destroyed_ && (!in.dmg || in.dmg->Integrity(dm::kMarchCup) > 0.0);
    if (torn && !cupPath) {
        if (storeE_ > kDischarge) {
            Note(p, "НЕУПРАВЛЯЕМЫЙ РАЗРЯД НАКОПИТЕЛЯ ПОЛЯ — КОРАБЛЬ ПОТЕРЯН", "UNCONTROLLED DISCHARGE OF THE FIELD STORE - THE SHIP IS LOST", 2);
            if (in.dmg) in.dmg->Inflict(dm::kHull, 1.0, in.dmgEnabled);
        } else Note(p, "Накопитель сорван, почти пуст — остаток рассеян", "The store torn off, nearly empty - the rest dissipated", 2);
        storeE_ = 0.0; dumpLeft_ = 0.0;
        return;
    }
    dumpSlow_ = !cupPath;
    dumpLeft_ = cupPath ? kDumpFast : kDumpSlow;
    dumpRate_ = storeE_ / dumpLeft_;
    const double heatK = storeE_ * (cupPath ? kDumpHeatFast : kDumpHeatSlow) / (p->Cfg().sternStore / 1400.0);
    p->Inflict(Hurt(kNoEffect, 1.0, false, heatK,
                    cupPath ? (torn ? "Накопитель сорван с люльки — аварийный сброс через чашу (5 с)"
                                    : "Накопитель повреждён ударом — аварийный сброс через чашу (5 с)")
                            : "Накопитель повреждён, чаши нет — сброс в нагрузки (60 с), корма греется",
                    cupPath ? "The store dumps through the cup (5 s)" : "The store dumps into its loads (60 s), the stern heats"));
}

void Core::Step(const Inputs& in) {
    pl::Plant* p = in.plant;
    if (!p || in.dt <= 0.0) return;
    const double dt = in.dt;
    const pl::Output* o = in.out;
    dm::Model* d = in.dmg;
    auto integ = [d](int part) { return d ? d->Integrity(part) : 1.0; };
    double now[8] = {integ(dm::kEqVeu), integ(dm::kEqStore), integ(dm::kEqPlant), integ(dm::kEqArgon),
                     integ(dm::kMarchCup), integ(dm::kCrestPort), integ(dm::kCrestStbd), integ(dm::kFin)};
    if (!init_) {   // a fresh start or a loaded scenario: what was hurt before is already in the plant's factors
        init_ = true;
        for (int i = 0; i < 8; ++i) seen_[i] = now[i];
        serial_ = d ? d->ImpactSerial() : 0;
        lastB_ = p->Field();
        gSlow_ = in.structG;
    }
    for (int i = 0; i < 8; ++i) if (now[i] > seen_[i]) seen_[i] = now[i];   // a station repaired it

    // --- the core as a point: the jolt over the sustained g, the hull's impacts, the stern's crushing
    if (in.settling) gSlow_ = in.structG;
    const double jolt = std::fabs(in.structG - gSlow_);
    gSlow_ += (in.structG - gSlow_) * std::min(1.0, dt / 2.0);
    double shock = jolt;
    if (d && d->ImpactSerial() != serial_) {
        serial_ = d->ImpactSerial();
        s_.lastShockG = d->LastImpact().peakG;
        shock = std::max(shock, s_.lastShockG);
    }
    s_.jolt = jolt;
    s_.sternCrush = d ? d->Crushed(impact::kStern) : 0.0;
    if (!destroyed_ && s_.sternCrush >= kStation) {
        destroyed_ = true;
        p->Inflict(Hurt(pl::kCoils, 0.0, true, 0.0, "Корма смята до ядра (12 м): установка, ВЭУ и накопитель разрушены",
                        "The stern crushed up to the core: the plant, the power plant and the store destroyed"));
        if (storeE_ > kDischarge) {
            Note(p, "НЕУПРАВЛЯЕМЫЙ РАЗРЯД НАКОПИТЕЛЯ ПОЛЯ — КОРАБЛЬ ПОТЕРЯН", "UNCONTROLLED DISCHARGE OF THE FIELD STORE - THE SHIP IS LOST", 2);
            if (d) d->Inflict(dm::kHull, 1.0, in.dmgEnabled);
        }
        storeE_ = 0.0; dumpLeft_ = 0.0;
    }
    auto crossed = [&](int i, double level) { return seen_[i] > level + 1e-6 && now[i] <= level + 1e-6; };

    // --- the equipment in its cradles and mounts (core/Damage sets their integrities from the impact's g)
    if (crossed(0, 0.0)) Note(p, "ВЭУ сорвана с люльки — корабль на накопителе до станции", "The power plant torn off - the ship on the store", 2);
    else if (crossed(0, 0.45)) Note(p, "ВЭУ повреждена ударом — мощность вдвое ниже до станции", "The power plant damaged - half power", 2);
    else if (crossed(0, 0.85)) Note(p, "ВЭУ: удар на пределе люльки — цела", "The power plant: a blow at its cradle's limit - intact", 1);
    if (crossed(1, 0.0)) StartDump(in, true);
    else if (crossed(1, 0.45)) StartDump(in, false);
    else if (crossed(1, 0.85)) Note(p, "Накопитель поля: удар на пределе люльки — цел", "The field store: a blow at its cradle's limit - intact", 1);
    if (crossed(2, 0.0))
        p->Inflict(Hurt(pl::kCoils, 0.0, true, 0.0, "Установка сорвана с креплений — маршевая потеряна до станции",
                        "The plant torn off its mounts - no march until a station"));
    else if (crossed(2, 0.45)) {
        p->Inflict(Hurt(pl::kCoils, 0.7, true, 0.0, "Установка повреждена ударом: обмотка -30 %, срыв поля",
                        "The plant damaged by the blow: windings -30 %, a quench"));
        p->Inflict(Hurt(pl::kDrivers, 0.75, false, 0.0, "Установка повреждена ударом: модуль триггера -25 %",
                        "The plant damaged by the blow: a trigger module -25 %"));
    } else if (crossed(2, 0.85)) Note(p, "Установка: удар на пределе креплений — цела", "The plant: a blow at its mounts' limit - intact", 1);
    if (crossed(3, 0.0)) Note(p, "Баки и насосы рабочей массы сорваны — только режим ПРОДУКТЫ", "The tanks and pumps torn off - PRODUCTS only", 2);
    else if (crossed(3, 0.45)) Note(p, "Насосы рабочей массы повреждены — расход ограничен до станции", "The pumps damaged - the flow capped", 2);
    else if (crossed(3, 0.85)) Note(p, "Баки и насосы: удар на пределе креплений — целы", "The tanks and pumps: a blow at their limit - intact", 1);
    for (int i : {0, 1, 2, 3}) seen_[i] = now[i];
    // the cup and the radiators wear gradually (heat, the air's loads): applied in steps of 5 %
    if (!destroyed_ && seen_[4] > 0.0 && (now[4] <= 0.0 || seen_[4] - now[4] >= 0.05)) {
        if (now[4] <= 0.0)
            p->Inflict(Hurt(pl::kCoils, 0.0, true, 0.0, "Маршевая чаша разрушена — маршевая потеряна до станции", "The march cup destroyed"));
        else {
            char ru[128];
            std::snprintf(ru, sizeof ru, "Чаша повреждена: каналы рубашки -%.0f %%", 100.0 * (1.0 - now[4] / seen_[4]));
            p->Inflict(Hurt(pl::kJacket, now[4] / seen_[4], false, 0.0, ru, "The cup damaged: the jacket's channels"));
        }
        seen_[4] = now[4];
    }
    {
        bool step = false;
        for (int i = 5; i < 8; ++i) step = step || (seen_[i] > 0.0 && (now[i] <= 0.0 || seen_[i] - now[i] >= 0.05));
        if (step) {
            const double a0 = kRadCrest * (seen_[5] + seen_[6]) + kRadFin * seen_[7];
            const double a1 = kRadCrest * (now[5] + now[6]) + kRadFin * now[7];
            if (a0 > 0.0 && a1 < a0) {
                char ru[128];
                std::snprintf(ru, sizeof ru, "Гребни и перо повреждены: радиаторы -%.0f %%", 100.0 * (1.0 - a1 / a0));
                p->Inflict(Hurt(pl::kRadiators, a1 / a0, false, 0.0, ru, "The crests and the fin damaged: the radiators"));
            }
            for (int i = 5; i < 8; ++i) seen_[i] = now[i];
        }
    }

    // --- the ВЭУ: the plasma goes out on a jolt (safe), relights after kVeuRestart s of calm
    const double veuH = destroyed_ ? 0.0 : Health(now[0]);
    if (shock > kPlasmaTripG && veuH > 0.0) {
        if (veuRestart_ <= 0.0)
            Note(p, "ВЭУ: плазма погасла от толчка — питание от накопителя, перезапуск 30 с", "The power plant's plasma out - on the store", 1);
        veuRestart_ = kVeuRestart;
    } else if (veuRestart_ > 0.0) {
        veuRestart_ -= dt;
        if (veuRestart_ <= 0.0) { veuRestart_ = 0.0; if (veuH > 0.0) Note(p, "ВЭУ перезапущена", "The power plant relit", 0); }
    }
    const bool plasma = veuH > 0.0 && veuRestart_ <= 0.0;
    const double veuAvail = plasma ? kVeuMax * veuH : 0.0;

    // --- the plant's state
    const int stage = p->StageNow();
    const bool run = stage == pl::kStRun, quench = stage == pl::kStQuench, gone = stage == pl::kStGone;
    const bool seq = stage == pl::kStCryo || stage == pl::kStFieldUp || stage == pl::kStTrigger || stage == pl::kStFeed;
    const double B = p->Field(), T = p->SternT();
    const double coils = p->Damage(pl::kCoils), cryoH = p->Damage(pl::kCryo);
    const double Bcap = (p->Limiter() ? pl::Plant::kBNom : pl::Plant::BRupture()) * std::sqrt(coils);
    const double Btarget = stage == pl::kStOff || quench || gone ? 0.0 : std::min(p->FieldSet(), Bcap);
    const int mass = o ? o->mass : pl::kArgon;
    const double mdot = o ? o->mdot : 0.0, fusion = o ? o->fusion : 0.0;

    // --- the cryo: the heat on the windings (the terms the plant warms them by) against the cooling
    const double b2 = (B / pl::Plant::kBNom) * (B / pl::Plant::kBNom);
    const double overK = 6.0 * std::max(0.0, b2 - 1.0) + std::max(0.0, T - p->Cfg().tSafe) / 60.0 + 0.3 * std::fabs(Btarget - B);
    const double cryoLoad = kCryoBase + kCryoPerK * overK, cryoCap = kCryoCap * cryoH;
    const double cryoPower = std::min(cryoLoad, cryoCap) * kCryoCOP;

    // --- the pumps
    const double pumpH = Health(now[3]);
    const double mdotNom = pl::Plant::FieldThrust(pl::Plant::kBNom, kAreaMarch) / kVArgon;
    const double pumpPower = mass == pl::kProducts ? 0.0 : mdot * kPumpDp / (kRhoMass[mass == pl::kIron ? 1 : 0] * kPumpEta);

    // --- the power: the ВЭУ serves the ship, the cryo and the pumps, charges the store; the store covers the rest
    const double storeH = destroyed_ ? 0.0 : Health(now[1]);
    const double capE = kStoreMax * storeH;
    const double need = in.shipLoad + cryoPower + pumpPower;
    const double supplied = std::min(veuAvail, need), deficit = need - supplied;
    double charge = dumpLeft_ > 0.0 ? 0.0 : std::min({kStoreCharge, veuAvail - supplied, std::max(0.0, capE - storeE_) / dt});
    charge = std::max(0.0, charge);
    double storeIn = charge, storeOut = deficit + in.sinterLoad;
    if (lastB_ < 0.0) lastB_ = B;
    const double dEf = kFieldE * (B * B - lastB_ * lastB_) / (pl::Plant::kBNom * pl::Plant::kBNom);   // the field's energy change
    if (dEf > 0.0) storeOut += dEf / dt;
    else if (!quench && !gone) storeIn += -0.9 * dEf / dt;                                           // a quench burns it in the coil
    lastB_ = B;
    if (stage == pl::kStTrigger) storeOut += pl::Plant::kFusionMax / kGainQ * 0.5;                   // the trigger's first charge
    storeE_ += (storeIn - storeOut) * dt;
    if (dumpLeft_ > 0.0) {
        storeE_ -= std::min(std::max(0.0, storeE_), dumpRate_ * dt);
        dumpLeft_ -= dt;
        if (dumpLeft_ <= 0.0) { dumpLeft_ = 0.0; storeE_ = 0.0; Note(p, "Сброс накопителя завершён", "The store's dump done", 1); }
    }
    bool powered = true;
    if (storeE_ <= 0.0) { storeE_ = 0.0; powered = deficit <= 1.0; }   // the sintering just stops (the ship reads storeE)
    if (dumpLeft_ <= 0.0) storeE_ = std::min(storeE_, capE);
    if (!powered && !unpowered_ && !destroyed_) Note(p, "НЕТ ПИТАНИЯ: ВЭУ не работает, накопитель пуст", "NO POWER: the power plant out, the store empty", 2);
    unpowered_ = !powered;
    const double veuPower = supplied + charge;
    const double mdotMax = powered ? mdotNom * kPumpMargin * pumpH : 0.0;
    p->SetFlowCap(mdotMax);

    // --- the windings: the current against the critical current at their temperature and field
    const double coilT = p->CoilT();
    const double I = kINom * B / pl::Plant::kBNom;
    const double tf = std::max(0.0, (1.0 - coilT / kTc) / (1.0 - 20.0 / kTc));
    const double Ic = kIc20 * std::pow(tf, 1.5) * std::pow(pl::Plant::kBNom / std::max(B, 4.0), kIcB);
    const double margin = Ic > 0.0 ? 1.0 - I / Ic : -1.0;
    if (B > 1.0 && !quench && !gone) {
        if (shock > kCoilJoltG) Quench(p, "Толчок сдвинул проводник обмотки — срыв поля", "A jolt moved the winding's conductor - a quench");
        else if (margin < 0.0) Quench(p, "Ток обмотки выше критического — срыв поля", "The winding's current over the critical - a quench");
        else if (!powered) Quench(p, "Нет питания криогеники — срыв поля", "No power for the cryo - a quench");
    }

    // --- the snapshot
    Snapshot& s = s_;
    const int st = p->StageNow();   // after the consequences
    const bool runN = st == pl::kStRun, quenchN = st == pl::kStQuench, goneN = st == pl::kStGone, lostCoils = p->CoilsLost();
    const double fuel = p->Fuel(), fuelFlow = fusion / pl::Plant::kEFus;
    const bool dip = p->Dipping();
    const double drivers = p->Damage(pl::kDrivers), jacketH = p->Damage(pl::kJacket), radH = p->Damage(pl::kRadiators);
    const double cupH = now[4], thrust = o ? o->thrust : 0.0, regen = o ? o->regen : 0.0;
    s.veuMax = kVeuMax * veuH; s.veuPower = veuPower; s.veuHealth = veuH; s.veuRestart = veuRestart_;
    s.veuFuelDay = kVeuFuelDay * veuPower / kVeuMax; s.veuPlasma = plasma;
    s.storeE = storeE_; s.storeMax = capE; s.storeIn = storeIn; s.storeOut = storeOut; s.storeHealth = storeH; s.dumpLeft = dumpLeft_;
    s.fuelKg = fuel; s.fuelFlow = fuelFlow; s.capsHz = fuelFlow / kCapsMass; s.capsHzMax = kCapsHzMax;
    s.capsE = kCapsMass * pl::Plant::kEFus; s.dip = dip;
    {
        double wDip = 0.0, wAll = 0.0;
        for (const pl::Failure& f : p->Cfg().fails) if (f.cause == pl::kPower) { wAll += f.weight; if (f.effect == pl::kDip) wDip += f.weight; }
        s.dipRisk = o && wAll > 0.0 ? o->riskPerMin[pl::kPower] * wDip / wAll : 0.0;
    }
    s.gainQ = kGainQ; s.pulseE = s.capsE / kGainQ; s.trigPower = fusion / kGainQ; s.drivers = drivers;
    s.B = B; s.Bset = p->FieldSet(); s.Bcap = Bcap; s.I = I; s.Ic = Ic; s.margin = margin; s.coilT = coilT; s.coils = coils;
    s.cryoCap = cryoCap; s.cryoLoad = cryoLoad; s.cryoPower = powered ? cryoPower : 0.0; s.cryoHealth = cryoH;
    s.massKind = mass; s.massLeft = in.massLeft; s.mdot = mdot; s.mdotMax = mdotMax; s.pumpPower = pumpPower; s.pumpHealth = pumpH;
    s.regen = regen; s.carry = mdot > 0.0 ? regen / mdot : 0.0; s.jacketHealth = jacketH;
    Outlet(mass, s.carry, &s.tIn, &s.tOut, &s.vapour);
    s.thrust = thrust; s.cupHealth = cupH; s.sternT = T; s.heatIn = o ? o->heatIn : 0.0;
    s.radiated = o ? o->radiated : 0.0; s.radArea = kRadAll * radH; s.radHealth = radH; s.radOut = in.crestsOut;
    s.destroyed = destroyed_;
    auto set = [](Node& n, int state, const char* why) { n.state = state; n.why = why; };

    if (destroyed_) set(s.veu, kLost, "ядро разрушено");
    else if (veuH <= 0.0) set(s.veu, kLost, "сорвана с люльки");
    else if (!plasma) set(s.veu, kFault, "плазма погасла, перезапуск");
    else if (veuH < 1.0) set(s.veu, kLimit, "повреждена, мощность снижена");
    else if (veuPower > 0.9 * veuAvail) set(s.veu, kLimit, "на полной мощности");
    else set(s.veu, kRun, "питает корабль");

    if (destroyed_) set(s.store, kLost, "ядро разрушено");
    else if (storeH <= 0.0) set(s.store, kLost, "сорван с люльки");
    else if (dumpLeft_ > 0.0) set(s.store, kFault, dumpSlow_ ? "аварийный сброс в нагрузки" : "аварийный сброс через чашу");
    else if (storeE_ <= 0.0) set(s.store, kFault, "пуст");
    else if (storeH < 1.0) set(s.store, kLimit, "повреждён, ёмкость снижена");
    else if (storeE_ < 0.1 * capE) set(s.store, kLimit, "заряд меньше 10 %");
    else if (storeOut > storeIn) set(s.store, kRun, "отдаёт энергию");
    else if (storeIn > 0.0) set(s.store, kRun, "заряжается");
    else set(s.store, kReady, "заряжен");

    if (fuel <= 0.0) set(s.fuel, kFault, "запас пуст");
    else if (fuel < 0.1 * pl::Plant::kFuelFull) set(s.fuel, kLimit, "меньше 10 % запаса");
    else if (fuelFlow > 0.0) set(s.fuel, kRun, "идёт на подачу");
    else if (runN || seq) set(s.fuel, kReady, "подача стоит");
    else set(s.fuel, kOff, "установка выключена");

    if (goneN) set(s.feed, kLost, "установка потеряна");
    else if (lostCoils) set(s.feed, kOff, "маршевая потеряна");
    else if (runN && fuel <= 0.0) set(s.feed, kFault, "топливо кончилось");
    else if (dip) set(s.feed, kLimit, "серия пропусков поджига");
    else if (fuelFlow > 0.0) set(s.feed, kRun, "капсулы идут в чашу");
    else if (st == pl::kStFeed) set(s.feed, kReady, "подача начинается");
    else if (runN) set(s.feed, kReady, "рычаг на нуле");
    else set(s.feed, kOff, seq ? "ждёт поле и триггер" : "установка выключена");

    if (goneN) set(s.trigger, kLost, "установка потеряна");
    else if (drivers < 1.0) set(s.trigger, kFault, "модуль сгорел, мощность снижена");
    else if (o && o->excess[pl::kPower] > 0.0) set(s.trigger, kLimit, "мощность сверх поля");
    else if (fusion > 0.0) set(s.trigger, kRun, "поджиг идёт");
    else if (st == pl::kStTrigger) set(s.trigger, kReady, "заряд триггера");
    else if (st == pl::kStFeed || runN) set(s.trigger, kReady, "заряжен, ждёт капсулы");
    else set(s.trigger, kOff, "установка выключена");

    if (goneN) set(s.coil, kLost, "установка потеряна");
    else if (lostCoils) set(s.coil, kLost, "обмотка потеряна");
    else if (quenchN) set(s.coil, kFault, "срыв поля, охлаждение");
    else if (coils < 1.0) set(s.coil, kFault, "повреждена, поле ограничено");
    else if (B > 1.0 && margin < 0.15) set(s.coil, kLimit, "мал запас по току");
    else if (o && o->excess[pl::kField] > 0.0) set(s.coil, kLimit, "выше номинала");
    else if (st == pl::kStFieldUp) set(s.coil, kReady, "подъём поля 0,8 Тл/с");
    else if (B < 0.05) set(s.coil, kOff, "поле снято");
    else if (std::fabs(B - Btarget) > 0.05) set(s.coil, kReady, "поле идёт к уставке");
    else set(s.coil, kRun, "поле на уставке");

    if (!powered) set(s.cryo, kFault, "нет питания");
    else if (cryoLoad > cryoCap) set(s.cryo, kFault, "не держит нагрузку");
    else if (cryoH < 1.0) set(s.cryo, kFault, "линия повреждена");
    else if (cryoLoad > 0.8 * cryoCap) set(s.cryo, kLimit, "нагрузка выше 80 %");
    else if (coilT > 23.0) set(s.cryo, kLimit, "обмотка выше 23 К");
    else if (st == pl::kStCryo) set(s.cryo, kReady, "проверка криогеники");
    else set(s.cryo, kRun, "держит обмотку холодной");

    if (mass == pl::kProducts) set(s.mass, kOff, "струя из продуктов");
    else if (pumpH <= 0.0) set(s.mass, kLost, "баки сорваны");
    else if (in.massLeft == 0.0) set(s.mass, kFault, "запас пуст");
    else if (p->Refused()) set(s.mass, kLimit, "в атмосфере только аргон");
    else if (mdot > 0.0) set(s.mass, kRun, "идёт в чашу");
    else if (runN || seq) set(s.mass, kReady, "подача стоит");
    else set(s.mass, kOff, "установка выключена");

    if (mass == pl::kProducts) set(s.pump, kOff, "стоят: режим ПРОДУКТЫ");
    else if (pumpH <= 0.0) set(s.pump, kLost, "сорваны");
    else if (!powered) set(s.pump, kFault, "нет питания");
    else if (pumpH < 1.0) set(s.pump, kFault, "повреждены, расход ограничен");
    else if (mdot > 0.9 * mdotMax) set(s.pump, kLimit, "на пределе расхода");
    else if (mdot > 0.0) set(s.pump, kRun, "качают");
    else if (runN) set(s.pump, kReady, "рычаг на нуле");
    else set(s.pump, kOff, "установка выключена");

    if (T > p->Cfg().tBreach) set(s.jacket, kFault, "прорвана, выше 1700 К");
    else if (jacketH < 1.0) set(s.jacket, kFault, "каналы повреждены");
    else if (T > p->Cfg().tBoil) set(s.jacket, kLimit, "кипение, отвод падает");
    else if (regen > 0.0) set(s.jacket, kRun, "масса уносит тепло");
    else set(s.jacket, kOff, "нет потока массы");

    if (destroyed_ || cupH <= 0.0) set(s.cup, kLost, "чаша разрушена");
    else if (goneN || lostCoils) set(s.cup, kLost, "маршевая потеряна");
    else if (cupH < 1.0) set(s.cup, kFault, "чаша повреждена");
    else if (thrust > 0.0 && o && std::string(o->limit) == "тепло") set(s.cup, kLimit, "тягу режет тепло кормы");
    else if (thrust > 0.0) set(s.cup, kRun, "тяга");
    else if (runN) set(s.cup, kReady, "на режиме, рычаг на нуле");
    else set(s.cup, kOff, "установка выключена");

    if (p->Lost()) set(s.stern, kLost, "корма прогорела");
    else if (T > p->Cfg().tBoil) set(s.stern, kFault, "выше 1100 К");
    else if (T > p->Cfg().tSafe) set(s.stern, kLimit, "выше безопасной 800 К");
    else if (s.heatIn > 0.0 || T > 310.0) set(s.stern, kRun, "в пределах");
    else set(s.stern, kOff, "холодная");

    if (radH < 0.999) set(s.rad, kFault, "панели повреждены");
    else if (!in.crestsOut) set(s.rad, kLimit, "гребни сложены — сброс 10 %");
    else if (s.radiated > 1e6) set(s.rad, kRun, "сбрасывают тепло");
    else set(s.rad, kReady, "холодные");
}

std::string Core::Save() const {
    char b[128];
    std::snprintf(b, sizeof b, "%.6e %.1f %.2f %d %d", storeE_, veuRestart_, dumpLeft_, dumpSlow_ ? 1 : 0, destroyed_ ? 1 : 0);
    return b;
}
void Core::Load(const std::string& s) {
    int slow = 0, gone = 0;
    double e = kStoreMax * 0.9, r = 0.0, dl = 0.0;
    std::sscanf(s.c_str(), "%lf %lf %lf %d %d", &e, &r, &dl, &slow, &gone);
    storeE_ = std::max(0.0, std::min(kStoreMax, e)); veuRestart_ = std::max(0.0, r); dumpLeft_ = std::max(0.0, dl);
    dumpSlow_ = slow != 0; destroyed_ = gone != 0;
    dumpRate_ = dumpLeft_ > 0.0 ? storeE_ / dumpLeft_ : 0.0;
    init_ = false;
}
void Core::Repair() {
    destroyed_ = false; dumpLeft_ = 0.0; veuRestart_ = 0.0; storeE_ = kStoreMax * 0.9; unpowered_ = false;
    init_ = false;
}

}  // namespace tantra::tcore
