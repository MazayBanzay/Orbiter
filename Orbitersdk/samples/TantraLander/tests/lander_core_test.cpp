// Headless checks of the «Грань» 25,4 м systems core (core/LanderCore, LanderPropulsion, LanderAvionics). No Orbiter.
// The vehicle here is a minimal rigid body (height, roll, pitch, yaw, the mass taking the argon) - the ship's physics is
// Orbiter's job; the core only gives the thrusts, the angles and the nodes.
#include "../core/LanderCore.h"
#include "../core/LanderSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tantra::lander;
namespace sp = tantra::lander::spec;
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }
static const double kDeg = 180.0 / sp::kPi;

// the argon by mode (cg_modes): нос, корма, крыло [kg]
static void HoverArgon(Propulsion& P) { P.SetArgon(0.0, sp::kArgonEntry, 0.0); }   // висение после входа: 165,29 т, ~54 с
// the long tests (60–70 с висения) take a test reserve of 6 т in the aft tank - не режим cg_modes, только длительность теста
static void LongArgon(Propulsion& P) { P.SetArgon(0.0, sp::kArgonEntry + 6000.0, 0.0); }
static void OrbitArgon(Propulsion& P) { P.SetArgon(0.0, sp::kArgonEntry, 0.0); }   // cg_modes[1]

struct Rig {
    Core c;
    Inputs in;
    double m = 0, h = 0, vz = 0, roll = 0, rr = 0, pitch = 0, pr = 0, yaw = 0, yr = 0, alpha = 0, ar = 0;
    double cmx = 0.05, cmz = 0.10;            // the CM off the cups' centre the automat does not know [m]; the known part -
                                              // XCg − kXcgHover (25,4 м: the argon cannot bring the CG onto the rows' centre)
    bool vertical = true;                     // the hover body; false: the flight (yaw only), the height frozen
    double hMin = 1e9, attMax = 0, t = 0;
    // the inertias: the Н2 rig's (60,7 т) scaled to Т1Б-А by the mass (the same form: I ∝ m), to 25,4 м also by K² (I ∝ m·L²):
    // 212,0 / 60,7 × 1,285²; the json gives none. The rig starts in the hover state (165,29 т)
    static constexpr double kIs = 211.995 / 60.7 * 1.285 * 1.285;
    Rig() { in.master = true; in.f.Ixx = 6e5 * kIs; in.f.Izz = 2e6 * kIs; in.f.Iyy = 2.4e6 * kIs; HoverArgon(c.Prop()); m = c.Prop().MassKg(); }
    void Argon(double nose, double aft, double wing) { c.Prop().SetArgon(nose, aft, wing); m = c.Prop().MassKg(); }
    void Step(double dt) {
        in.dt = dt;
        in.f.mass = m; in.f.h = h; in.f.vz = vz; in.f.roll = roll; in.f.rollRate = rr; in.f.pitch = pitch; in.f.pitchRate = pr;
        in.f.yaw = yaw; in.f.yawRate = yr; in.f.alpha = alpha; in.f.alphaRate = ar;
        c.Step(in);
        for (double& s : in.shock) s = 0; in.shockCabin = 0;
        const Propulsion& P = c.Prop();
        if (vertical) {
            double Fz = 0, tr = 0, tp = 0;
            const double dx = P.XCg() - sp::kXcgHover + cmx;
            for (int i = sp::kMarch; i < sp::kUnits; ++i) {
                const Unit& u = P.u[i];
                Fz += u.thrust; tr += u.thrust * (u.x - dx); tp += u.thrust * (u.z - cmz);
            }
            const double az = Fz * std::cos(roll) * std::cos(pitch) / m - sp::kG0;
            vz += az * dt; h += vz * dt;
            if (h <= 0) { h = 0; if (vz < 0) vz = 0; }
            rr += tr / in.f.Ixx * dt; roll += rr * dt;
            pr += tp / in.f.Izz * dt; pitch += pr * dt;
            // the yaw (the attitude thrusters) is left out of the hover body
            if (t > 5.0) { hMin = std::min(hMin, h); attMax = std::max(attMax, std::max(std::fabs(roll), std::fabs(pitch)) * kDeg); }
        } else {
            double ty = 0;
            for (int i = 0; i < sp::kMarch; ++i) {
                const Unit& u = P.u[i];
                ty += -u.thrust * u.x + u.thrust * std::sin(u.tvcY / kDeg) * std::fabs(u.z);
            }
            yr += ty / in.f.Iyy * dt; yaw += yr * dt;
            attMax = std::max(attMax, std::fabs(yaw) * kDeg);
        }
        m -= (P.argonFlow + P.rcsFlow + P.noseFlow) * dt;
        t += dt;
    }
    void Run(double sec, double dt = 0.01) { for (double s = 0; s < sec - 1e-9; s += dt) Step(dt); }
};

static void Units(const Snapshot& s) {
    for (int i = 0; i < sp::kUnits; ++i)
        std::printf("      %s%d %-7s %-46s B %5.2f Тл  запас %3.0f %%  T %5.1f К  тяга %6.1f кН%s\n", i < 2 ? "М" : "П", i < 2 ? i + 1 : i - 1,
                    StateRu(s.unit[i].state), s.unit[i].why, s.B[i], s.margin[i] * 100, s.coilT[i], s.thrust[i] / 1e3, s.shed[i] ? "  (погашена)" : "");
}
static void Head(const Snapshot& s) {
    std::printf("      контур: фотон %s (%s) | аналог %s (%s) | механика %s; режим %s (%s); решение: %s\n",
                StateRu(s.photon.state), s.photon.why, StateRu(s.analog.state), s.analog.why, StateRu(s.mech.state),
                StateRu(s.mode.state), s.mode.why, s.decision);
    std::printf("      накопитель %s (%s) %.1f %% %.1f МВт | криокулеры А %s (%s) Б %s (%s) | аргон %.0f кг %.1f кг/с\n",
                StateRu(s.store.state), s.store.why, s.storeFrac * 100, s.storeOut / 1e6, StateRu(s.cooler[0].state), s.cooler[0].why,
                StateRu(s.cooler[1].state), s.cooler[1].why, s.argonKg, s.argonFlow);
}
static int Running(const Snapshot& s) { int n = 0; for (int i = 2; i < 8; ++i) n += s.thrust[i] > 1e3; return n; }

static void Hover(Rig& r, double sec = 30.0) {
    r.in.mode = kHover; r.in.p.hHold = 30.0; r.in.deployLift = true;
    r.Run(sec);
}

int main() {
    std::printf("== 1. запуск и висение (g Земли, без экипажа), 165,29 т + 6 т запаса теста в кормовом, удержание 30 м\n");
    {
        Rig r; r.Argon(0.0, sp::kArgonEntry + 6000.0, 0.0);
        r.in.mode = kHover; r.in.p.hHold = 30.0;
        r.Run(6.5);
        const Snapshot s0 = r.c.S();
        std::printf("      t 6,5 с: ряды %s / %s, поле П1 %.1f Тл\n", StateRu(s0.row[0].state), StateRu(s0.row[1].state), s0.B[2]);
        r.Run(53.5);
        const Snapshot& s = r.c.S();
        Head(s); Units(s);
        std::printf("      высота %.2f м, vz %.3f м/с, тяга чаш %.1f кН / вес %.1f кН, Т/В номинал %.2f, ориентация макс %.2f°, ЦМ %.3f м\n",
                    r.h, r.vz, s.thrustLift / 1e3, r.m * sp::kG0 / 1e3, s.twNominal, r.attMax, s.xcg);
        check(s.row[0].state == kRun && s.row[1].state == kRun, "оба ряда выпущены (створка, рама)");
        check(Running(s) == 6, "шесть подъёмных чаш работают");
        check(std::fabs(r.h - 30.0) < 0.5 && std::fabs(r.vz) < 0.1, "висение на 30 м");
        check(std::fabs(s.liftNominal / (sp::kMentry * sp::kG0) - sp::kTwHover) < 0.01, "Т/В на номинале при 165,29 т: 1,725 (tw)");
        check(std::fabs(s.liftNominal / (sp::kMtom * sp::kG0) - sp::kTwStart) < 0.01, "Т/В на номинале при 212,0 т: 1,345 (tw[0])");
        check(std::fabs(s.xcg - sp::kXcgHover) < 0.02, "перекачка держит ЦМ висения −2,86 м (центр рядов)");
        check(std::fabs(s.margin[2] - 0.42) < 0.01, "запас по току 42 % при 12,1 Тл и 20 К");
        check(std::fabs(s.argonFlow - r.m * sp::kG0 / sp::kLiftV) < 0.03 * s.argonFlow, "расход аргона ṁ = m·g/15 км/с (~110 кг/с)");
        check(s.photon.state == kRun && s.active == kPhoton && s.cooler[1].state == kReady, "ведёт фотонный контур, криокулер Б в горячем резерве");
    }

    std::printf("== 2. отказ одной подъёмной чаши в висении (удар 50 g по П1)\n");
    {
        Rig r; Hover(r);
        const double h0 = r.h; r.hMin = 1e9; r.attMax = 0;
        r.in.shock[2] = 50.0;
        r.Run(20.0);
        const Snapshot& s = r.c.S();
        Head(s); Units(s);
        std::printf("      высота мин %.2f м (было %.2f), сейчас %.2f; ориентация макс %.2f°; Т/В номинал %.2f\n", r.hMin, h0, r.h, r.attMax, s.twNominal);
        check(s.unit[2].state == kLost, "П1 потеряна: удар разрушил чашу");
        check(s.shed[7] && s.thrust[7] == 0.0, "противолежащая П6 погашена");
        check(Running(s) == 4, "висят четыре чаши");
        check(s.twNominal >= sp::kTwOneOutHover && std::fabs(s.twNominal - 4 * sp::kLiftF / (r.m * sp::kG0)) < 0.01,
              "Т/В на номинале ≥ 1,15 (tw.TW_fail при 165,29 т), четыре чаши");
        check(r.attMax < 2.0, "ориентация держится (< 2°)");
        check(std::fabs(r.h - 30.0) < 0.5 && h0 - r.hMin < 3.0, "высота восстановлена, просадка < 3 м");
    }
    std::printf("== 2б. срыв поля в чаше (удар 7 g по П2): гашение противолежащей, перезапуск вторым инжектором\n");
    {
        Rig r; Hover(r);
        r.hMin = 1e9; r.attMax = 0;
        r.in.shock[3] = 7.0;
        r.Run(0.4);
        const Snapshot a = r.c.S();
        std::printf("      0,4 с: П2 %s (%s) тяга %.0f кН; П5 погашена %d\n", StateRu(a.unit[3].state), a.unit[3].why, a.thrust[3] / 1e3, a.shed[6]);
        r.Run(1.0);
        std::printf("      1,4 с: П2 тяга %.1f кН\n", r.c.S().thrust[3] / 1e3);
        check(r.c.S().thrust[3] < 0.06 * sp::kLiftF, "тяга сорванной чаши ~0 за ~1 с");
        check(a.shed[6], "противолежащая П5 погашена за 0,3 с");
        r.Run(10.0);
        const Snapshot& s = r.c.S();
        Units(s);
        std::printf("      просадка %.2f м, ориентация макс %.2f°\n", 30.0 - r.hMin, r.attMax);
        check(s.unit[3].state == kRun && Running(s) == 6, "П2 перезапущена вторым инжектором, П5 вернулась: шесть чаш");
        check(30.0 - r.hMin < 3.0, "высота падает меньше чем на 3 м с 30 м (json)");
        check(r.attMax < 3.0, "ориентация держится (< 3°) при срыве и перезапуске");
    }
    std::printf("== 2в. две пары вне работы, аварийный режим 11,5 км/с; χ 0,001 %%: излучение обмотку не греет\n");
    {
        Rig r; LongArgon(r.c.Prop()); r.m = r.c.Prop().MassKg(); Hover(r);
        r.in.p.emergency = true;
        r.in.shock[2] = 50.0; r.in.shock[3] = 50.0;
        double tEm = -1, tOff = -1, marginMin = 1;
        for (int k = 0; k < 4000; ++k) {
            r.Step(0.01);
            const Unit& u = r.c.Prop().u[4];
            if (tEm < 0 && u.emergency) tEm = r.t;
            if (tEm > 0 && tOff < 0 && !u.emergency) tOff = r.t;
            if (tEm > 0) marginMin = std::min(marginMin, u.margin);
        }
        const Snapshot& s = r.c.S();
        Units(s);
        std::printf("      аварийный режим П3 с %.1f с, снят %.1f с; обмотка %.2f К, запас мин %.0f %%, высота %.1f м\n", tEm, tOff, s.coilT[4], marginMin * 100, r.h);
        check(tEm > 0 && tOff < 0 && r.t - tEm >= 30.0, "аварийный режим держится ≥ 30 с: обмотка не греется (предел режима в А - не определено)");
        check(marginMin > sp::kEmMargin && s.unit[4].state == kLimit, "запас по току > 20 %, без перехода обмотки");
    }

    std::printf("== 3. срыв поля маршевого на наборе (h 20 км): УВТ 15° не проводит струю одного через ЦМ (x 2,31 м), второй на малом газе\n");
    {
        Rig r; r.vertical = false; r.in.mode = kFlight; r.in.p.march = 1.0; r.in.f.h = 20e3; r.h = 20e3; r.in.deployLift = false;
        r.Run(20.0);
        const double F0 = r.c.S().thrustMarch;
        r.attMax = 0;
        r.in.shock[0] = 8.0;
        r.Run(0.6);
        const Snapshot a = r.c.S();
        std::printf("      0,6 с: М1 %s (%s), М2 УВТ рыск %.1f°, решение: %s\n", StateRu(a.unit[0].state), a.unit[0].why, a.tvcY[1], a.decision);
        r.Run(5.0);
        const Snapshot& s = r.c.S();
        Head(s); Units(s);
        std::printf("      тяга маршевых %.0f кН (было %.0f), рыскание макс %.2f°\n", s.thrustMarch / 1e3, F0 / 1e3, r.attMax);
        const double need = std::atan(sp::kMarchX / std::fabs(r.c.Prop().u[0].z)) * kDeg;
        std::printf("      нужно УВТ %.1f° (atan 2,31 / %.2f м), есть ±15°; М2 тяга в отказе %.0f кН\n", need, std::fabs(r.c.Prop().u[0].z), a.thrust[1] / 1e3);
        check(need > sp::kTvcMax && std::strstr(a.decision, "малом газе") != nullptr && a.thrust[1] < 0.3 * sp::kMarchF,
              "один маршевый ось не держит (нужно > 15°): второй идёт вниз за отказавшим, на малом газе до перезапуска");
        check(s.unit[0].state == kRun && std::fabs(s.thrustMarch - F0) < 0.02 * F0, "М1 перезапущен вторым инжектором: пара на полной тяге");
        check(r.attMax < 3.0, "ось держится (< 3°)");
        // the march lost for good below 40 km: the return by gliding
        r.in.shock[1] = 50.0;
        r.Run(12.0);
        std::printf("      М2 разрушен ударом 50 g: %s\n", r.c.S().decision);
        check(std::strstr(r.c.S().decision, "возврат планированием") != nullptr, "маршевый не перезапустился ниже 40 км: возврат планированием");
    }

    std::printf("== 4. отказ фотонного контура в висении (удар 12 g по приборному отсеку)\n");
    {
        Rig r; LongArgon(r.c.Prop()); r.m = r.c.Prop().MassKg(); Hover(r);
        r.in.shockCabin = 12.0;
        r.hMin = 1e9; r.attMax = 0;
        r.Run(30.0);
        const Snapshot& s = r.c.S();
        Head(s);
        std::printf("      высота %.2f м (мин %.2f), ориентация макс %.2f°\n", r.h, r.hMin, r.attMax);
        check(s.photon.state == kFault && s.active == kAnalog && s.analog.state == kRun, "фотонный отказал (причина), ведёт аналоговый");
        check(std::fabs(r.h - 30.0) < 1.0 && r.attMax < 2.0, "висение держит аналоговый автомат");
        r.in.mode = kDock; r.Run(0.1);
        std::printf("      режим стыковки: %s (%s)\n", StateRu(r.c.S().mode.state), r.c.S().mode.why);
        check(r.c.S().mode.state == kLimit, "стыковка/захват аналогу недоступна: ручное с причиной");
        // the analog one also loses a pair as it should, with its 0,3 s
        r.in.mode = kHover; r.in.shock[4] = 50.0; r.Run(10.0);
        check(r.c.S().shed[5] && Running(r.c.S()) == 4 && std::fabs(r.h - 30.0) < 1.0, "аналоговый тоже гасит противолежащую и держит висение");
    }

    std::printf("== 5. баллистический спуск днищем (аргон входа в носовом): α 58,3° при элевонах −25°, вдув в нос\n");
    {
        Rig r; r.vertical = false; r.in.master = false; r.in.deployLift = false; r.in.mode = kBallistic;
        r.Argon(sp::kArgonEntry, 0.0, 0.0);
        r.alpha = 55.0 / kDeg;
        r.Run(1.0);
        const Snapshot a = r.c.S();
        std::printf("      α 55°: элевоны %.1f°, ЦМ %.3f м, %s\n", a.elevon, a.xcg, a.decision);
        check(a.mode.state == kRun && !a.noseValve && a.elevon <= sp::kBallElevon + 0.5 && std::strstr(a.decision, "58°") != nullptr,
              "режим «баллистический спуск днищем»: ЦМ ≥ −2,97, элевоны на возврат к α 58°, клапан закрыт");
        r.alpha = sp::kBallAlphaE25 / kDeg; r.Run(1.0);
        check(std::fabs(r.c.S().elevon - sp::kBallElevon) < 0.5, "α 58,3°: элевоны на балансировке −25° (balance)");
        const double ar0 = r.c.S().argonKg;
        r.in.p.nose = true; r.Run(10.0);
        const Snapshot& s = r.c.S();
        std::printf("      по команде: нос %s (%s), %.1f кг/с, аргон %.0f -> %.0f кг; %s\n", StateRu(s.nose.state), s.nose.why, s.noseFlow, ar0, s.argonKg, s.decision);
        check(s.noseValve && std::fabs(ar0 - s.argonKg - 50.0) < 0.5, "вдув по команде: 5 кг/с (0,5 т ≈ 100 с)");
        r.in.p.nose = false; r.alpha = 10.0 / kDeg; r.Run(1.0);
        std::printf("      α 10° (носом вперёд): %s\n", r.c.S().decision);
        check(r.c.S().noseValve && r.c.S().noseFlow > 0, "носом вперёд: аналоговый автомат открывает вдув сам");
        // the engines never take the nose reserve
        Rig e; e.Argon(0.0, 550.0, 0.0); Hover(e, 20.0);
        std::printf("      баки 550 кг, 20 с висения: подача %s (%s), чаши работают %d, аргон %.1f кг\n", StateRu(e.c.S().feed.state),
                    e.c.S().feed.why, Running(e.c.S()), e.c.S().argonKg);
        check(e.c.S().feed.state == kFault && Running(e.c.S()) == 0 && std::fabs(e.c.S().argonKg - 500.0) < 1.0, "двигатели не берут резерв носа 0,5 т");
    }

    std::printf("== 6. опустошение накопителя: что снимается по приоритетам (переход, маршевые 50 %%)\n");
    {
        Rig r; LongArgon(r.c.Prop()); r.m = r.c.Prop().MassKg(); r.in.mode = kTransition; r.in.p.march = 0.5; r.in.p.hHold = 30.0;
        r.Run(30.0);
        const double steps[] = {0.25, 0.15, 0.08, 0.03};
        std::printf("      %-8s | %-9s %-9s | %-8s %-8s | %-6s %-6s | %s\n", "накоп.", "фотон", "аналог", "крио А", "крио Б", "марш", "чаши", "снятие");
        int prevPhoton = -1; bool ok = true;
        for (double f : steps) {
            r.c.SetStore(f * sp::kStoreE); r.Run(3.0);
            const Snapshot& s = r.c.S();
            std::printf("      %6.1f %% | %-9s %-9s | %-8s %-8s | %4.0f кН %d | %s\n", s.storeFrac * 100, StateRu(s.photon.state), StateRu(s.analog.state),
                        StateRu(s.cooler[0].state), StateRu(s.cooler[1].state), s.thrustMarch / 1e3, Running(s), s.shedWhy);
            if (f == 0.15) ok = ok && s.cooler[1].state == kOff && s.photon.state == kRun;
            if (f == 0.08) ok = ok && s.photon.state == kOff && s.active == kAnalog && s.thrustMarch > 0;
            if (f == 0.03) ok = ok && s.thrustMarch == 0 && Running(s) == 6 && std::fabs(r.h - 30.0) < 1.5;
            prevPhoton = s.photon.state;
        }
        (void)prevPhoton;
        check(ok, "< 20 % криокулер Б, < 10 % фотоника (ведёт аналог), < 5 % маршевые; чаши держат висение");
        r.c.SetStore(4e7); r.Run(3.0);
        const Snapshot& s = r.c.S();
        Head(s); Units(s);
        std::printf("      шина %s (%s), батарея аналога %.1f МДж, высота %.1f м\n", StateRu(s.bus.state), s.bus.why, s.analogBatt / 1e6, r.h);
        check(s.store.state == kFault && Running(s) == 0 && std::strstr(s.unit[2].why, "питания") != nullptr, "накопитель пуст: триггеры без питания, тяга ноль");
        check(s.cryo.state == kFault && s.active == kAnalog && s.bus.state == kLimit, "криогеника встала, аналог и жизнеобеспечение на батарее");
        r.Run(60.0);
        std::printf("      через 60 с: П1 %s (%s), обмотка %.1f К\n", StateRu(r.c.S().unit[2].state), r.c.S().unit[2].why, r.c.S().coilT[2]);
    }

    std::printf("== 7. отказ криокулера А и течь кормового бака в висении (удары 30 g)\n");
    {
        Rig r; r.Argon(sp::kTankNose0, sp::kTankAft0, sp::kTankWing0); Hover(r);   // the start state, 212,0 т (Т/В 1,345)
        r.in.shockCooler[0] = 30.0; r.in.shockTank[sp::kTAft] = 30.0;
        r.Run(30.0);
        const Snapshot& s = r.c.S();
        Head(s);
        std::printf("      кормовой %s (%s), носовой %s %.0f кг, перекрёстная подача %s (%s), высота %.2f м\n", StateRu(s.tank[1].state), s.tank[1].why,
                    StateRu(s.tank[0].state), s.tankKg[0], StateRu(s.cross.state), s.cross.why, r.h);
        check(s.cooler[0].state == kFault && s.cooler[1].state == kRun && s.cryo.state == kRun, "криокулер Б принял нагрузку");
        check(s.tank[1].state == kFault && s.cross.state == kRun && Running(s) == 6 && std::fabs(r.h - 30.0) < 0.5,
              "кормовой отсечён, перекрёстная подача из носового: все шесть чаш, полная тяга");
    }

    std::printf("== 8. ЦМ по трём бакам в каждом режиме cg_modes (допуск 0,02 м, 0,02 т)\n");
    {
        struct M { const char* name; double nose, aft, wing, m, x; };
        const M modes[] = {
            {"старт", sp::kTankNose0, sp::kTankAft0, sp::kTankWing0, 211.995, sp::kXcgStart},
            {"орбита", 0, 6300, 0, 165.286, sp::kXcgOrbit},
            {"вход - планирование (всё в носовом)", 6300, 0, 0, 165.286, sp::kXcgGlide},
            {"вход - всё в кормовом (отказ перекачки)", 0, 6300, 0, 165.286, sp::kXcgBallOld},
            {"посадка", 0, 500, 0, 159.486, sp::kXcgLand},
            {"висение (центр рядов)", 1151, 5149, 0, 165.286, sp::kXcgHover},
        };
        bool ok = true;
        for (const M& k : modes) {
            Propulsion P; P.SetArgon(k.nose, k.aft, k.wing);
            const double x = P.XCg(), m = P.MassKg() / 1e3;
            const bool good = std::fabs(x - k.x) < 0.02 && std::fabs(m - k.m) < 0.02;
            std::printf("      %-36s %6.2f т  ЦМ %7.3f м (cg_modes %6.3f) %s\n", k.name, m, x, k.x, good ? "" : "<-");
            ok = ok && good;
        }
        check(ok, "ЦМ и масса по mass_budget + три бака совпадают с cg_modes");
        check(std::fabs(Propulsion().MassKg() - sp::kMtom) < 20.0, "старт: m0 212,0 т (masses_t.m0)");
    }

    std::printf("== 9. перекачка в носовой бак перед входом (орбита, 6,3 т в кормовом)\n");
    {
        Rig r; r.vertical = false; r.in.master = false; r.in.deployLift = false; r.in.mode = kFlight; OrbitArgon(r.c.Prop());
        const double x0 = r.c.Prop().XCg();
        r.in.preEntry = true;
        double tDone = -1;
        for (int k = 0; k < 12000 && tDone < 0; ++k) { r.Step(0.01); if (r.c.S().tankKg[0] >= sp::kArgonEntry - sp::kTrimDead) tDone = r.t; }
        r.Run(2.0);
        const Snapshot& s = r.c.S();
        std::printf("      ЦМ %.3f → %.3f м за %.1f с; носовой %.0f кг, кормовой %.0f кг; перекачка %s (%s)\n", x0, s.xcg, tDone, s.tankKg[0], s.tankKg[1],
                    StateRu(s.transfer.state), s.transfer.why);
        check(tDone > 75.0 && tDone < 83.0, "6,3 т двумя путями за ~79 с (80 кг/с)");
        check(std::fabs(s.xcg - sp::kXcgGlide) < 0.02 && s.tankKg[1] < 10.0, "ЦМ входа −2,51 (cg_modes): весь аргон в носовом, кормовой пуст");
        r.in.preEntry = false; r.in.mode = kEntry; r.alpha = sp::kGlideAlpha / kDeg; r.Run(1.0);
        std::printf("      вход: щиток %.0f°, элевоны %.1f°, %s\n", r.c.S().flap, r.c.S().elevon, r.c.S().decision);
        check(r.c.S().flap == sp::kGlideFlap && std::fabs(r.c.S().elevon) < 0.5 && std::fabs(r.c.S().xcg - sp::kXcgGlide) < 0.02,
              "планирование: щиток 15°, элевоны 0 при α 32,8°, ЦМ держится");
    }

    std::printf("== 10. отказ основного пути перекачки (А): второй насос/клапан (Б) доводит аргон в носовой\n");
    {
        Rig r; r.vertical = false; r.in.master = false; r.in.deployLift = false; r.in.mode = kBallistic; OrbitArgon(r.c.Prop());
        r.alpha = sp::kBallAlphaE25 / kDeg;
        r.in.transferFail[0] = true;
        r.Run(1.0);
        const Snapshot a = r.c.S();
        std::printf("      1 с: перекачка %s (%s), ЦМ %.3f; %s\n", StateRu(a.transfer.state), a.transfer.why, a.xcg, a.decision);
        r.Run(170.0);
        const Snapshot& s = r.c.S();
        std::printf("      через 171 с: ЦМ %.3f м, носовой %.0f кг; %s\n", s.xcg, s.tankKg[0], s.decision);
        check(a.transfer.state == kLimit && std::strstr(a.transfer.why, "вторым насосом") != nullptr && a.xcg >= sp::kXBallNeedE25,
              "путь А отказал: перекачка путём Б 40 кг/с; ЦМ всего в кормовом −2,94 ≥ −2,97 - балансировка уже есть");
        check(std::fabs(s.xcg - sp::kXcgGlide) < 0.02 && std::strstr(s.decision, "58°") != nullptr,
              "путь Б: 6,3 т в носовом (~158 с), ЦМ −2,51 - устойчивая балансировка α 58°");
        Rig b; b.vertical = false; b.in.master = false; b.in.deployLift = false; b.in.mode = kBallistic; OrbitArgon(b.c.Prop());
        b.alpha = sp::kBallAlphaE25 / kDeg; b.in.transferFail[0] = b.in.transferFail[1] = true;
        b.Run(130.0);
        std::printf("      оба пути: перекачка %s (%s), ЦМ %.3f; %s\n", StateRu(b.c.S().transfer.state), b.c.S().transfer.why, b.c.S().xcg, b.c.S().decision);
        check(b.c.S().transfer.state == kFault && std::fabs(b.c.S().xcg - sp::kXcgBallOld) < 0.02 && std::strstr(b.c.S().decision, "балансировки нет") == nullptr,
              "оба пути отказали: всё в кормовом, ЦМ −2,94 ≥ −2,97 - балансировка остаётся (у Т1Б-А её не было)");
    }

    std::printf("== 11. висение с экипажем: на Земле (атмосфера) только аварийно, на безатмосферном теле - штатно\n");
    {
        Rig r; LongArgon(r.c.Prop()); r.m = r.c.Prop().MassKg(); r.in.f.atmosphere = true; r.in.crew = sp::kCrewMax; Hover(r, 10.0);
        const Snapshot a = r.c.S();
        std::printf("      Земля, %d чел.: режим %s (%s); %s; чаш работает %d, высота %.1f м\n", sp::kCrewMax, StateRu(a.mode.state), a.mode.why, a.decision, Running(a), r.h);
        check(a.mode.state == kLimit && Running(a) == 0 && r.h == 0.0 && std::strstr(a.decision, "полосу") != nullptr, "Земля, экипаж: висение запрещено, посадка на полосу");
        r.in.p.hoverEmergency = true; r.Run(50.0);
        std::printf("      аварийная команда: %s; высота %.2f м\n", r.c.S().decision, r.h);
        check(Running(r.c.S()) == 6 && std::fabs(r.h - 30.0) < 0.5, "аварийное висение с экипажем по команде: держит 30 м");
        Rig m; m.in.f.atmosphere = false; m.in.crew = sp::kCrewMax; Hover(m, 40.0);
        check(m.c.S().mode.state == kRun && Running(m.c.S()) == 6, "безатмосферное тело, экипаж: висение штатно");
        check(Avionics::HoverAllowed(true, 0, false), "Земля без экипажа: висение разрешено (правило касается экипажа)");
    }

    std::printf("== 12. накопитель по формуле store_A и пик триггеров маршевых\n");
    {
        Rig r; r.vertical = false; r.in.mode = kFlight; r.in.p.march = 1.0; r.in.f.h = 20e3; r.h = 20e3; r.in.deployLift = false;
        r.Argon(sp::kTankNose0, sp::kTankAft0, sp::kTankWing0);
        r.Run(10.0);
        const Snapshot& s = r.c.S();
        std::printf("      E_треб %.1f ГДж, с запасом 25 %% %.2f ГДж, принято %.0f ГДж; триггеры маршевых %.1f МВт; Т/В маршевых %.2f\n",
                    sp::kStoreNeed / 1e9, sp::kStoreDesign / 1e9, sp::kStoreE / 1e9, s.loadTrigMarch / 1e6, s.thrustMarch / (sp::kMtom * sp::kG0));
        check(std::fabs(sp::kStoreNeed / 1e9 - 268.22) < 0.05 && std::fabs(sp::kStoreDesign / 1e9 - 357.63) < 0.05 && sp::kStoreE >= sp::kStoreDesign,
              "формула: (E_выхода + E_висения)/Q/(1 − 0,25) = 357,63 ГДж ≤ 358 ГДж");
        check(std::fabs(s.loadTrigMarch / 1e6 - 450.4) < 3.0 && s.loadTrigMarch <= sp::kStoreP, "два маршевых на полной: P_струи/Q = 450 МВт ≤ 570 МВт накопителя");
        check(std::fabs(2 * sp::kMarchF / (sp::kMtom * sp::kG0) - sp::kTwMains) < 0.01, "Т/В маршевых на старте 1,30 (tw[0])");
    }

    std::printf("== 13. крыло и шасси по режимам (как в меше gen_lander.py MODES)\n");
    {
        struct { int mode; double mach, tip; bool gear; const char* what; } T[] = {
            {kHover, 0, 0, true, "висение: концевые 0°, шасси выпущено"}, {kTransition, 0.3, 0, false, "переход: 0°, шасси убрано"},
            {kFlight, 0.6, 0, false, "полёт дозвук: 0°"}, {kFlight, 3.0, 60, false, "полёт выше M 1: киль 60°"},
            {kEntry, 20, 60, false, "вход: киль 60°"}, {kBallistic, 20, 90, false, "баллистика: сложены 90°"},
            {kRunway, 0.3, 0, true, "полоса (glide): 0°, шасси выпущено"}, {kVertLand, 0, 0, true, "вертикальная посадка: 0°, шасси"},
            {kDock, 0, 90, false, "ангар/захват: сложены 90°"}};
        for (auto& t : T) {
            const WingCfg w = WingConfig(t.mode, t.mach);
            check(w.tip == t.tip && w.gearDown == t.gear, t.what);
        }
        Rig r; r.vertical = false; r.in.mode = kRunway; r.in.f.mach = 0.3; r.in.deployLift = false; r.Run(0.5);
        const Snapshot& s = r.c.S();
        check(s.flap == sp::kRunwayFlap && s.tip == 0.0 && s.gearDown, "полоса: щиток 10° (≤ 10° при касании), крыло 0°, шасси");
    }

    std::printf("== 14. форсаж маршевых: тяга ×2, расход ×4, обмотка греется, автомат снимает по запасу тока; у «Тантры» запрещён\n");
    {
        Rig r; r.vertical = false; r.in.mode = kFlight; r.in.p.march = 1.0; r.in.f.h = 20e3; r.h = 20e3; r.in.deployLift = false;
        r.Argon(sp::kTankNose0, sp::kTankAft0, sp::kTankWing0);
        r.Run(15.0);
        const double F0 = r.c.S().thrustMarch, m0 = r.c.S().argonFlow;
        r.in.p.afterburner = true; r.in.f.shipDist = 200.0; r.Run(2.0);
        const Snapshot n = r.c.S();
        std::printf("      у «Тантры» (200 м): %s, тяга %.0f кН\n", n.decision, n.thrustMarch / 1e3);
        check(std::strstr(n.decision, "запрещён") != nullptr && std::fabs(n.thrustMarch - F0) < 0.02 * F0, "у «Тантры» форсаж не включается, причина в решении");
        r.in.f.shipDist = 1e9; r.Run(5.0);
        const Snapshot a = r.c.S();
        std::printf("      форсаж 5 с: тяга %.0f кН (было %.0f), расход %.0f кг/с (было %.0f), поле %.1f Тл, запас %.0f %%, обмотка %.2f К\n",
                    a.thrustMarch / 1e3, F0 / 1e3, a.argonFlow, m0, a.B[0], a.margin[0] * 100, a.coilT[0]);
        check(a.thrustMarch > 1.9 * F0 && a.argonFlow > 3.5 * m0, "тяга ×1,99 (поле 16 Тл), расход ×4 (19,3 км/с вместо 40)");
        double tEnd = -1;
        for (int k = 0; k < 30000 && tEnd < 0; ++k) { r.Step(0.01); if (!r.c.Prop().u[0].emergency) tEnd = r.t; }
        const Snapshot& e = r.c.S();
        std::printf("      форсаж снят через %.0f с: %s; обмотка %.2f К, запас %.0f %%, тяга %.0f кН\n", tEnd - 17.0, e.unit[0].why, e.coilT[0], e.margin[0] * 100, e.thrustMarch / 1e3);
        r.Run(2.0);
        check(tEnd > 0 && r.c.S().unit[0].state != kFault && std::fabs(r.c.S().thrustMarch - F0) < 0.05 * F0, "автомат снял форсаж по запасу тока до перехода обмотки, тяга вернулась к номиналу");
    }

    std::printf("\n%s: %d проверок не прошли\n", fails ? "ОШИБКА" : "ВСЁ ПРОШЛО", fails);
    return fails ? 1 : 0;
}
