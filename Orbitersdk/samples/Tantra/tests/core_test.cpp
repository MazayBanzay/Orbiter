// Offline checks of the energy core (core/TantraCore) with the real plant (core/Plant) and damage model (core/Damage).
#include "../core/TantraCore.h"
#include "../core/Damage.h"
#include "../core/Impact.h"
#include "../core/Plant.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace tantra;
static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }
static unsigned seed = 7;
static double rnd() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; }
static const char* kSt[] = {"выкл", "готов", "работа", "предел", "отказ", "потерян"};

struct Rig {
    plant::Plant p;
    damage::Model d;
    tcore::Core c;
    plant::Env env;
    plant::Output out;
    double g = 1.0;
    void Step(double dt, damage::Ground* gr = nullptr) {
        out = p.Step(dt, env, rnd);
        damage::Flight f; f.gLoad = g;
        damage::Exposure x;
        damage::Ground none;
        d.Step(dt, f, x, gr ? *gr : none, true);
        tcore::Inputs in;
        in.dt = dt; in.plant = &p; in.out = &out; in.dmg = &d; in.structG = g; in.crestsOut = env.crestsOut;
        c.Step(in);
    }
    void Run(double sec, double dt = 0.05) { for (double t = 0; t < sec; t += dt) Step(dt); }
    void Journal(int n) {
        int i = 0;
        for (const auto& l : p.Journal()) { if (i++ >= n) break; std::printf("        | %s\n", l.ru.c_str()); }
    }
};

static void Nodes(const tcore::Snapshot& s) {
    std::printf("      ВЭУ %s (%s) %.2f ГВт | накоп %s (%s) %.3g Дж | обмотка %s (%s) запас %.0f %% | крио %s | насосы %s (%s) | чаша %s\n",
                kSt[s.veu.state], s.veu.why, s.veuPower / 1e9, kSt[s.store.state], s.store.why, s.storeE, kSt[s.coil.state], s.coil.why,
                s.margin * 100.0, kSt[s.cryo.state], kSt[s.pump.state], s.pump.why, kSt[s.cup.state]);
}

int main() {
    std::printf("== 1. штатный режим, космос, железо, рычаг 100 %%\n");
    {
        Rig r; r.env.level = 1.0;
        const double e0 = r.c.S().storeE;
        r.Run(120.0);
        const auto& s = r.c.S();
        Nodes(s);
        std::printf("      тяга %.0f МН, капсулы %.0f Гц, триггер %.2f ТВт (Q %.0f), ток %.0f кА из %.0f, обмотка %.1f К, рубашка %.0f->%.0f К (пар %.0f %%)\n",
                    s.thrust / 1e6, s.capsHz, s.trigPower / 1e12, s.gainQ, s.I / 1e3, s.Ic / 1e3, s.coilT, s.tIn, s.tOut, s.vapour * 100);
        check(std::fabs(s.margin - 0.42) < 0.02, "запас по току 42 % при 12,1 Тл и 20 К");
        check(s.coil.state == tcore::kRun && s.veu.state == tcore::kRun, "обмотка и ВЭУ работают");
        check(s.storeE > e0 && s.store.state == tcore::kRun, "накопитель заряжается от ВЭУ");
        check(s.capsHz > 150 && s.capsHz < 320, "частота капсул из расхода топлива");
        check(!r.p.Quenched(), "без срыва");
    }
    std::printf("== 2. пуск с нуля: поле и первый заряд триггера из накопителя\n");
    {
        Rig r; r.p.Load("12.1 100 1 -1 300 20 1 1 1 1 1 0 0 2000000"); r.env.level = 0.0;
        r.Run(1.0);
        const double e0 = r.c.S().storeE;
        r.p.StartStop(true);
        double t = 0; while (!r.p.Running() && t < 60) { r.Step(0.05); t += 0.05; }
        const double used = e0 + 1e9 * t - r.c.S().storeE;
        std::printf("      на режиме через %.1f с, из накопителя %.3g Дж\n", t, used);
        check(r.p.Running(), "установка вышла на режим");
        check(used > 1.5e12 && used < 2.2e12, "накопитель отдал поле (7 ГДж) и заряд триггера (~1,8 ТДж)");
    }
    std::printf("== 3. без ограничителя поле вверх: срыв по критическому току\n");
    {
        Rig r; r.env.level = 0.0;
        r.Run(1.0);
        r.p.LimiterPress(0.0, true); r.p.LimiterPress(r.p.Clock(), true);
        for (int i = 0; i < 50; ++i) r.p.FieldStep(0.1);
        double Bq = 0; double t = 0;
        while (!r.p.Quenched() && t < 60) { r.Step(0.05); t += 0.05; Bq = r.p.Field(); }
        std::printf("      срыв при %.2f Тл через %.1f с, обмотка %.1f К\n", Bq, t, r.p.CoilT());
        r.Journal(2);
        check(r.p.Quenched() && Bq > 15.5 && Bq < 17.2, "срыв поля между 15,5 и 17,1 Тл");
    }
    std::printf("== 4. перегрев: корма 2000 К, обмотка 45 К -> ток выше критического\n");
    {
        Rig r; r.p.Load("12.1 100 1 -1 2000 45 1 1 1 1 1 0 5 2000000");
        r.Step(0.05);
        r.Journal(2);
        check(r.p.Quenched(), "срыв поля от тёплой обмотки");
    }
    std::printf("== 5. толчки: 5 g - плазма ВЭУ гаснет, 12 g - ещё и срыв поля\n");
    {
        Rig r; r.env.level = 0.5;
        r.Run(5.0);
        r.g = 6.0; r.Step(0.05); r.g = 1.0;
        const auto s = r.c.S();
        Nodes(s);
        check(!s.veuPlasma && s.store.state == tcore::kRun && !r.p.Quenched(), "плазма погасла, корабль на накопителе, поле держится");
        r.Run(31.0);
        check(r.c.S().veuPlasma, "ВЭУ перезапущена через 30 с");
        r.Run(5.0);
        r.g = 13.0; r.Step(0.05); r.g = 1.0;
        r.Journal(3);
        check(r.p.Quenched(), "толчок 12 g сорвал поле");
    }
    std::printf("== 6. удары днищем (плотный грунт): итог по перегрузке\n");
    for (double v : {8.0, 12.0, 16.0, 22.0, 30.0}) {
        Rig r; r.env.level = 0.0;
        r.Run(1.0);
        damage::Ground gr; gr.touchdown = true; gr.contact = true; gr.impactMode = impact::kBelly; gr.vDown = v; gr.soil = 6.0e6;
        r.Step(0.05, &gr);
        r.Run(10.0);
        const auto& s = r.c.S();
        std::printf("   %4.0f м/с: %.1f g, ВЭУ %.2f накоп %.2f устан %.2f баки %.2f, корабль %s\n", v, s.lastShockG, r.d.Integrity(damage::kEqVeu),
                    r.d.Integrity(damage::kEqStore), r.d.Integrity(damage::kEqPlant), r.d.Integrity(damage::kEqArgon),
                    r.d.Integrity(damage::kHull) > 0 ? "цел" : "ПОТЕРЯН");
        Nodes(s);
        r.Journal(6);
        if (s.lastShockG > 3.0) check(s.veu.state != tcore::kRun, "удар выше 3 g гасит плазму ВЭУ");
        if (s.lastShockG > 38.0 * 1.5) check(s.veu.state == tcore::kLost && s.store.state == tcore::kLost, "выше 57 g ВЭУ и накопитель сорваны");
        else if (s.lastShockG > 38.0) check(s.storeHealth < 1.0 && s.storeE < 1e12 + 1e9 * 10, "выше 38 g накопитель повреждён и сброшен");
    }
    std::printf("== 7. кормой в скалу: смятие до станции ядра (12 м)\n");
    {
        Rig r; r.env.level = 0.0;
        r.Run(1.0);
        double v = 20.0;
        for (; v < 400.0; v += 10.0) {
            impact::Input in; in.mode = impact::kSternFirst; in.v = v; in.soil = 60e6;
            if (impact::Solve(in).crushed[impact::kStern] >= tcore::Core::kStation) break;
        }
        damage::Ground gr; gr.touchdown = true; gr.contact = true; gr.impactMode = impact::kSternFirst; gr.vDown = v; gr.soil = 60e6;
        r.Step(0.05, &gr);
        r.Run(1.0);
        std::printf("      %.0f м/с: смято %.1f м, %.0f g\n", v, r.c.S().sternCrush, r.c.S().lastShockG);
        r.Journal(4);
        check(r.c.S().destroyed, "ядро разрушено");
        check(r.d.Integrity(damage::kHull) <= 0.0, "заряженный накопитель: корабль потерян");
    }
    std::printf("== 8. насосы повреждены: тяга на аргоне ограничена расходом\n");
    {
        Rig r; r.env.level = 1.0; r.env.air = true; r.env.rho = 1.0;
        r.Run(5.0);
        const double f0 = r.out.thrust;
        r.d.Inflict(damage::kEqArgon, 0.55, true);
        r.Run(5.0);
        const auto& s = r.c.S();
        std::printf("      тяга %.0f -> %.0f МН, расход %.0f из %.0f т/с, насосы %s (%s)\n", f0 / 1e6, r.out.thrust / 1e6, s.mdot / 1e3,
                    s.mdotMax / 1e3, kSt[s.pump.state], s.pump.why);
        check(std::fabs(r.out.thrust - s.mdotMax * 3.0e4) < 1e6 && r.out.thrust < f0, "тяга = расход насосов x 30 км/с");
    }
    std::printf("== 9. обесточен: ВЭУ сорвана, накопитель сорван (сброс через чашу)\n");
    {
        Rig r; r.env.level = 0.3;
        r.Run(5.0);
        r.d.Inflict(damage::kEqVeu, 1.0, true);
        r.d.Inflict(damage::kEqStore, 1.0, true);
        r.Run(8.0);
        const auto& s = r.c.S();
        Nodes(s);
        r.Journal(6);
        check(r.d.Integrity(damage::kHull) > 0.0, "сброс через чашу: корабль цел");
        check(r.p.Quenched() && s.mdotMax == 0.0 && s.cryo.state == tcore::kFault, "нет питания: срыв поля, насосы стоят");
    }
    std::printf("== 10. сохранение\n");
    {
        Rig r; r.Run(1.0);
        r.g = 6.0; r.Step(0.05); r.g = 1.0;
        const std::string s = r.c.Save();
        tcore::Core c2; c2.Load(s);
        check(c2.Save() == s, ("сохранение и загрузка: " + s).c_str());
    }
    std::printf(fails ? "\n%d FAILED\n" : "\nALL OK\n", fails);
    return fails ? 1 : 0;
}
