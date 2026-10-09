#include "LanderPropulsion.h"
#include "LanderSpec.h"

#include <algorithm>
#include <cmath>

namespace tantra::lander {
using namespace spec;

const char* StateRu(int s) {
    static const char* k[] = {"выкл", "готов", "работа", "предел", "отказ", "потерян"};
    return (s >= 0 && s <= 5) ? k[s] : "?";
}

Propulsion::Propulsion() {
    for (int i = 0; i < kUnits; ++i) {
        Unit& un = u[i];
        if (i < kMarch) {
            un.march = true; un.Fnom = kMarchF; un.vNom = kMarchV; un.S = kMarchS;
            un.x = i == 0 ? -kMarchX : kMarchX; un.line = kLineMarch; un.C = kCMarch; un.ramp = kRampMarch;
            un.side = i;
        } else {
            const int k = i - kMarch;             // 0..5: left row 0..2, right row 3..5
            un.Fnom = kLiftF; un.vNom = kLiftV; un.S = kLiftS;
            un.x = k < 3 ? -kLiftX : kLiftX; un.z = kLiftZ[k % 3]; un.line = kLineLift; un.C = kCLift; un.ramp = kRampLift;
            un.side = k < 3 ? 0 : 1;
        }
        un.v = un.vNom;
        un.node = {kOff, "не запущен"};
    }
    SetArgon(kTankNose0, kTankAft0, kTankWing0);   // the start: cg_modes[0]
    chargesKg = kCharges;
    for (int i = 0; i < kMarch; ++i) u[i].z = kMarchPivotX - XCg();
    cooler[0].running = true;
}

double Propulsion::MassKg() const {
    double m = chargesKg + ArgonKg();
    for (const Item& it : kBudget) m += it.t * 1e3;
    return m;
}

double Propulsion::XCg() const {
    double m = chargesKg, mx = chargesKg * kChargesX;
    for (const Item& it : kBudget) { m += it.t * 1e3; mx += it.t * 1e3 * it.x; }
    for (int t = 0; t < kTanks; ++t) { m += tank[t].kg; mx += tank[t].kg * kTankX[t]; }
    return mx / m;
}

// the nose tank's argon the schedule wants: the entry - the aft one's argon in the nose up to 4,85 т (cg_modes[2], the
// ballistic too: balance_A); the hover - the CG on the rows' centre −2,50 (cg_modes[5]); the feed - all into the aft one
double Propulsion::NoseTarget(int trim) const {
    const Tank& N = tank[kTNose]; const Tank& A = tank[kTAft];
    const double S = N.kg + (A.leak ? 0.0 : A.kg);   // «leak», not «isolated»: an empty tank still takes the argon
    switch (trim) {
    case kTrimEntry: return std::min(kTankCap[kTNose], S);
    case kTrimHover: {
        const double M = MassKg(), Mx = XCg() * M;
        const double rest = Mx - N.kg * kTankX[kTNose] - A.kg * kTankX[kTAft];
        const double n = (kXcgHover * M - rest - S * kTankX[kTAft]) / (kTankX[kTNose] - kTankX[kTAft]);
        return std::clamp(n, 0.0, std::min(kTankCap[kTNose], S));
    }
    case kTrimFeed: return 0.0;
    default: return N.kg;
    }
}

// the transfer: two paths (А основной, Б - второй насос/клапан), 40 кг/с each; the wing goes into the aft tank first
void Propulsion::Transfer(const PropIn& in) {
    const double dt = in.dt;
    for (int p = 0; p < 2; ++p) pathOk[p] = !in.transferFail[p];
    const int n = (pathOk[0] ? 1 : 0) + (pathOk[1] ? 1 : 0);
    double cap = n * kPumpFlow * dt;
    Tank& N = tank[kTNose]; Tank& A = tank[kTAft]; Tank& W = tank[kTWing];
    transferFlow = 0;
    bool wing = false;
    if (in.trim != kTrimHold && cap > 0 && !A.leak) {
        if (!W.leak && W.kg > 0) {
            const double d = std::min({W.kg, cap, kTankCap[kTAft] - A.kg});
            if (d > 0) { W.kg -= d; A.kg += d; cap -= d; wing = true; }
        }
        const double want = NoseTarget(in.trim) - N.kg;
        if (!N.leak && std::fabs(want) > kTrimDead && cap > 0) {
            const double d = want > 0 ? std::min({want, cap, A.kg, kTankCap[kTNose] - N.kg})
                                      : -std::min({-want, cap, N.kg, kTankCap[kTAft] - A.kg});
            N.kg += d; A.kg -= d; transferFlow = d / dt;
        }
    }
    transfer = n == 0 ? Node{kFault, "перекачка: оба пути отказали"}
             : n == 1 ? Node{kLimit, pathOk[0] ? "путь Б отказал: перекачка путём А, 40 кг/с" : "путь А отказал: перекачка вторым насосом (Б), 40 кг/с"}
             : transferFlow > 0 ? Node{kRun, "перекачка в носовой"} : transferFlow < 0 ? Node{kRun, "перекачка в кормовой"}
             : wing ? Node{kRun, "перекачка из крыла в кормовой"} : Node{kReady, "готова, два пути"};
}

double Propulsion::Usable(bool release) const {
    double a = 0;
    for (const Tank& t : tank) if (!t.leak) a += t.kg;   // the reserve 0,5 т stays (in the aft tank: cg_modes[4])
    return std::max(0.0, a - (release ? 0.0 : kNoseReserve));
}

double Propulsion::LiftAvail(bool emergency) const {
    double f = 0;
    for (int i = kMarch; i < kUnits; ++i) {
        const Unit& un = u[i];
        if (un.broken || !RowOut(un.side)) continue;
        f += emergency ? un.Fnom * un.vNom / kEmV : un.Fnom;
    }
    return f;
}

void Propulsion::Quench(Unit& un, const char* why) {
    if (un.stage == kStBroken || un.stage == kStQuench) return;
    un.stage = kStQuench; un.coilT += kQuenchJump; un.emergency = false;
    un.node = {kFault, why};
}

void Propulsion::Stall(Unit& un, const char* why) {
    if (un.stage != kStRun && un.stage != kStTrigger) return;
    un.stage = kStStall; un.timer = kRestart;
    un.node = {kFault, why};
}

void Propulsion::StepUnit(int i, const PropIn& in, double coolShare, bool argonOk) {
    Unit& un = u[i];
    const double dt = in.dt;
    // shocks on the unit: the plasma, the conductor, the structure
    const double g = in.shock[i];
    if (g >= kShockBreak && !un.broken) { un.broken = true; un.stage = kStBroken; un.node = {kLost, "удар: чаша разрушена"}; }
    else if (g >= kShockQuench) Quench(un, "удар: сдвиг проводника, переход обмотки");
    else if (g >= kShockStall) {
        if (g >= kShockStall * 1.5) un.injOk[un.injector] = false;   // the injector's charge line is torn
        Stall(un, "удар: срыв поля, плазма ушла из чаши");
    }
    if (un.broken) { un.thrust = un.mdot = un.pJet = un.pTrig = 0; un.B = std::max(0.0, un.B - 25.0 * dt); return; }

    const bool canRun = in.armed && (un.march || RowOut(un.side));
    const bool want = canRun && in.thr[i] > 1e-4;
    // the emergency mode: asked, on the run and not blocked by the windings' margin
    if (un.emBlocked && un.coilT < 21.0) un.emBlocked = false;
    un.emergency = want && in.em[i] && un.stage == kStRun && !un.emBlocked;
    if (un.emergency) un.emTime += dt; else if (!in.em[i]) un.emTime = 0.0;
    un.v = un.emergency ? kEmV : un.vNom;
    const double Pnom = un.Fnom * un.vNom / 2.0;
    un.thrustSet = want ? std::min(1.0, in.thr[i]) * 2.0 * Pnom / un.v : 0.0;
    // the field the thrust needs (≥ 12,1 T; the march up to 16 T), the lift cups stay at 12,1 T
    const double pfield = un.thrustSet / std::max(1e-6, un.S);
    un.Bset = !canRun ? 0.0 : un.march ? std::min(kBmax, std::max(kB, std::sqrt(2.0 * kMu0 * pfield))) : kB;

    // the windings: heat against the cold line; the current against the critical current
    un.coilHeat = kCoilBase + kChi * un.pJet * kCoilRadShare * (un.emergency ? kEmHeat : 1.0);
    un.coilCool = un.line * coolShare;
    un.coilT = std::max(20.0, un.coilT + (un.coilHeat - un.coilCool) / un.C * dt);
    const double warm = std::max(1e-6, 1.0 - (un.coilT - 20.0) / (kTc - 20.0));
    un.ratio = un.B > 0.05 ? kIratio * std::pow(un.B / kB, 1.0 + kIcB) / warm : 0.0;
    un.margin = 1.0 - un.ratio;
    if (un.ratio >= 1.0) Quench(un, un.coilT > 21.0 ? "перегрев обмотки: ток выше критического" : "поле выше критического");
    if (un.emergency && un.margin < kEmMargin) {
        un.emergency = false; un.emBlocked = true; un.v = un.vNom;
        un.thrustSet = std::min(1.0, in.thr[i]) * un.Fnom;
    }

    switch (un.stage) {
    case kStOff:
        // a put-out cup keeps its field (hot standby, «готов»); stowed or disarmed - the field goes down
        if (canRun) un.B = un.B < kB ? std::min(kB, un.B + un.ramp * dt) : un.B; else un.B = std::max(0.0, un.B - un.ramp * dt);
        if (want) { un.stage = kStFieldUp; un.node = {kReady, "подъём поля"}; }
        else un.node = !in.armed ? Node{kOff, "главный выключатель"} : !canRun ? Node{kOff, "чаша убрана"} : Node{kReady, "готов"};
        break;
    case kStFieldUp:
        if (!canRun) { un.stage = kStOff; break; }
        un.B = std::min(std::max(un.Bset, kB), un.B + un.ramp * dt);
        if (un.B >= kB - 1e-6) { un.stage = kStTrigger; un.timer = kTriggerT; un.node = {kReady, "заряд триггера"}; }
        break;
    case kStTrigger:
        un.timer -= dt;
        if (!want) { un.stage = kStOff; break; }
        if (in.trigSupply < 0.5) { un.node = {kFault, "нет питания триггера: накопитель"}; un.timer = kTriggerT; break; }
        if (un.timer <= 0) { un.stage = kStRun; un.node = {kRun, "работа"}; }
        break;
    case kStRun:
        if (un.B < un.Bset) un.B = std::min(un.Bset, un.B + un.ramp * dt); else un.B = std::max(un.Bset, un.B - un.ramp * dt);
        if (!want) { un.stage = kStOff; un.node = {kReady, canRun ? "останов" : "чаша убрана"}; break; }
        if (in.trigSupply < 0.5) { Stall(un, "нет питания триггера: накопитель"); break; }
        if (!argonOk) { Stall(un, "нет аргона: остался резерв носа"); break; }
        if (chargesKg <= 0) { Stall(un, "нет ионных зарядов"); break; }
        if (!un.injOk[un.injector]) { Stall(un, "отказ инжектора зарядов"); break; }
        un.node = un.emergency ? Node{kLimit, un.B > kB + 0.1 ? "аварийный режим 11,5 км/с, поле выше 12,1 Тл" : "аварийный режим 11,5 км/с"}
                : un.emBlocked ? Node{kLimit, "аварийный режим снят: запас по току 20 %"}
                : un.coilT > 30.0 ? Node{kLimit, "обмотка тёплая"} : Node{kRun, "работа"};
        break;
    case kStStall:
        un.timer -= dt;
        if (un.timer <= 0) {
            const int other = 1 - un.injector;
            if (un.injOk[other]) un.injector = other;        // the restart goes from the second injector
            if (!un.injOk[un.injector]) { un.node = {kFault, "оба инжектора зарядов отказали"}; un.timer = 1e9; break; }
            if (!want) un.stage = kStOff;
            else if (in.trigSupply >= 0.5 && argonOk && chargesKg > 0) {
                un.stage = kStTrigger; un.timer = kTriggerT; un.node = {kReady, "перезапуск: второй инжектор"};
            } else un.timer = 0.5;
        }
        break;
    case kStQuench:
        un.B = std::max(0.0, un.B - 25.0 * dt);              // the field dumps in ~0,5 s
        if (un.B <= 0 && un.coilT < 22.0) {
            if (want) { un.stage = kStFieldUp; un.node = {kReady, "после перехода: подъём поля"}; }
            else un.stage = kStOff;
        }
        break;
    }

    // the thrust: the field cap F ≤ B²/2μ0·S, then F = 2P/v; a stall lets it fall with kStallTau (~1 s to zero)
    un.fieldCap = un.B * un.B / (2.0 * kMu0) * un.S;
    const double target = un.stage == kStRun ? std::min(un.thrustSet, un.fieldCap) : 0.0;
    // the automat's own stop (a cup put out) is as fast as a throttle change; a lost plasma decays with kStallTau
    const double tau = (un.stage == kStRun || un.stage == kStOff) ? 0.1 : kStallTau;
    un.thrust += (target - un.thrust) * std::min(1.0, dt / tau);
    if (un.thrust < 0.005 * un.Fnom && target <= 0) un.thrust = 0.0;
    un.mdot = un.stage == kStRun ? un.thrust / un.v : 0.0;   // a stalled plasma decays without a feed
    un.pJet = un.thrust * un.v / 2.0;
    un.pTrig = (un.stage == kStRun || un.stage == kStTrigger) ? std::max(un.pJet, 0.05 * Pnom) / kQ : 0.0;
    if (un.march) {
        const bool ok = un.tvcWinding[0] || un.tvcWinding[1];
        const double rate = (un.tvcWinding[0] && un.tvcWinding[1]) ? 30.0 : 15.0;
        auto drive = [&](double& t, double w) { w = std::clamp(w, -kTvcMax, kTvcMax); if (ok) t += std::clamp(w - t, -rate * dt, rate * dt); };
        drive(un.tvcP, in.tvcP[i]); drive(un.tvcY, in.tvcY[i]);
    }
}

void Propulsion::Step(const PropIn& in) {
    const double dt = in.dt;
    // the rows: the door, then the frame; a winding of the drive lost - the other one, at half speed
    for (int r = 0; r < 2; ++r) {
        Row& w = row[r];
        if (in.shockFrame[r] >= kShockFrame) { if (w.winding[0]) w.winding[0] = false; else w.winding[1] = false; }
        const int windings = (w.winding[0] ? 1 : 0) + (w.winding[1] ? 1 : 0);
        const double speed = windings == 2 ? 1.0 : windings == 1 ? 0.5 : 0.0;
        if (in.deploy[r]) {
            if (w.door < 1) w.door = std::min(1.0, w.door + speed * dt / kDoorT);
            else w.frame = std::min(1.0, w.frame + speed * dt / kFrameT);
        } else {
            bool hot = false;
            for (int k = 0; k < 3; ++k) hot = hot || u[kMarch + r * 3 + k].thrust > 0;
            if (!hot) { if (w.frame > 0) w.frame = std::max(0.0, w.frame - speed * dt / kFrameT); else w.door = std::max(0.0, w.door - speed * dt / kDoorT); }
        }
        if (windings == 0) w.node = {kFault, "рама заклинена: обе обмотки привода"};
        else if (windings == 1) w.node = {kLimit, "рама на второй обмотке привода"};
        else if (w.frame >= 1) w.node = {kRun, "ряд выпущен"};
        else if (w.door > 0) w.node = {kReady, in.deploy[r] ? "выпуск: створка, рама" : "уборка"};
        else w.node = {kOff, "ряд убран, створка закрыта"};
    }

    // the cryocoolers: A runs, B is the hot standby; B starts when A fails, loses power or the load is over A
    for (int c = 0; c < 2; ++c) {
        if (in.shockCooler[c] >= kShockCooler) cooler[c].ok = false;
        cooler[c].powered = in.coolerPower[c];
    }
    double load = 0;
    for (const Unit& un : u) load += std::min(un.line, un.coilHeat + (un.coilT - 20.0) * un.C * 0.05);
    const bool aOk = cooler[0].ok && cooler[0].powered, bOk = cooler[1].ok && cooler[1].powered;
    cooler[0].running = aOk;
    cooler[1].running = bOk && (!aOk || load > kCooler);
    const int n = (cooler[0].running ? 1 : 0) + (cooler[1].running ? 1 : 0);
    const double cap = n * kCooler;
    const double share = load > 0 ? std::min(1.0, cap / load) : (cap > 0 ? 1.0 : 0.0);
    coolerPowerDemand = 0;
    for (int c = 0; c < 2; ++c) {
        Cooler& k = cooler[c];
        k.load = k.running ? std::min(kCooler, load / n) : 0.0;
        coolerPowerDemand += k.load * kCOP;
        k.node = !k.ok ? Node{kFault, "удар: криокулер разрушен"} : !k.powered ? Node{kOff, "снят с питания"}
               : k.running ? (k.load >= kCooler * 0.999 ? Node{kLimit, "на пределе холода"} : Node{kRun, "работа"}) : Node{kReady, "горячий резерв"};
    }
    cryo = cap <= 0 ? Node{kFault, "нет холода: оба криокулера"} : share < 1.0 ? Node{kLimit, "холода меньше нагрузки: обмотки греются"}
         : n == 2 ? Node{kRun, "два криокулера"} : Node{kRun, "работа"};

    // the tanks: a holed tank is isolated (its argon is lost); the engines feed from the aft one, the cross-feed brings the
    // nose and the wing ones when the aft one is cut off or empty (the feed rate from one tank: «не опр.» - emergencies)
    for (int t = 0; t < kTanks; ++t) {
        Tank& k = tank[t];
        if (in.shockTank[t] >= kShockTank) k.leak = true;
        if (k.leak) k.kg = std::max(0.0, k.kg - kTankLeak * dt);
        k.isolated = k.leak || k.kg <= 0;
        k.node = k.leak ? Node{kFault, "удар: течь бака, бак отсечён"} : k.kg <= 0 ? Node{kOff, "пуст"} : Node{kRun, "подача"};
    }
    Transfer(in);
    for (int i = 0; i < kMarch; ++i) u[i].z = kMarchPivotX - XCg();   // the УВТ arm follows the CG
    crossOpen = tank[kTAft].isolated && (tank[kTNose].kg > 0 || tank[kTWing].kg > 0);
    cross = crossOpen ? Node{kRun, tank[kTNose].kg > 0 ? "перекрёстная подача из носового бака" : "перекрёстная подача из бака крыла"}
          : Node{kReady, "закрыта"};
    const bool argonOk = Usable(in.reserveRelease) > 0.0;
    feed = argonOk ? Node{kRun, "подача"} : Node{kFault, Usable(true) > 0 ? "остался только резерв носа" : "аргон кончился"};

    // the units
    powerDemand = 0; argonFlow = 0;
    for (int i = 0; i < kUnits; ++i) {
        StepUnit(i, in, share, argonOk);
        // the trigger, and the field's energy while it ramps (B²/2μ0 x the cup's volume per second of the ramp)
        powerDemand += u[i].pTrig;
        if (u[i].stage == kStFieldUp) powerDemand += u[i].B * u[i].ramp / kMu0 * (u[i].march ? 0.25 : 0.063);
        argonFlow += u[i].mdot;
    }
    // the attitude thrusters and the nose
    rcsFlow = argonOk ? std::min(4.0 * kRcsF, std::fabs(in.rcs) / 3.0) / kRcsV : 0.0;
    rcs = argonOk ? (rcsFlow > 0 ? Node{kRun, "работа"} : Node{kReady, "готов"}) : Node{kFault, "нет аргона"};
    noseFlow = in.noseValve && Usable(true) > 0 ? kNoseFlow : 0.0;
    nose = noseFlow > 0 ? Node{kRun, "вдув аргона в пористый нос"} : in.noseValve ? Node{kFault, "аргона нет"} : Node{kReady, "клапан закрыт"};
    // take the argon from the tanks that feed
    double need = (argonFlow + rcsFlow + noseFlow) * dt;
    for (int t : {kTAft, kTNose, kTWing}) {
        Tank& k = tank[t];
        if (k.isolated || need <= 0) continue;
        const double d = std::min(k.kg, need); k.kg -= d; need -= d;
    }
    chargesKg = std::max(0.0, chargesKg - argonFlow * kChargeShare * dt);
    charges = chargesKg > 0 ? Node{kRun, "есть"} : Node{kFault, "ионные заряды кончились"};
}

}  // namespace tantra::lander
