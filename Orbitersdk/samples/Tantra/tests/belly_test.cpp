// Offline checks of the belly-landing autopilot (orbiter2016/TantraBellyLand): a point-mass ship flown by its laws - the pods
// (the table of their thrust direction by cup angle from UpdatePods' geometry, the cups slewing at 15 deg/s and giving nothing
// more than 3 deg off their command, the levels following at 3 /s), the march, the bank following in 2 s, the air's drag, a side
// wind, the lying gear in 12 s, the contact with the CG 14 m up. Hold, the targets changed in flight, the landing.
#include "../orbiter2016/TantraBellyLand.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace gd = tantra::guidance;

static int fails = 0;
static std::string U8(const std::wstring& w) {   // the journal's text for the UTF-8 console
    std::string o;
    for (wchar_t c : w) {
        const unsigned u = unsigned(c);
        if (u < 0x80) o += char(u);
        else if (u < 0x800) { o += char(0xC0 | (u >> 6)); o += char(0x80 | (u & 63)); }
        else { o += char(0xE0 | (u >> 12)); o += char(0x80 | ((u >> 6) & 63)); o += char(0x80 | (u & 63)); }
    }
    return o;
}
static void Check(bool ok, const char* what) { std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }

// UpdatePods' geometry (MeshLayout kPods axes, the jets' splay 15 out / 25 down, the pairs' shares for the CG at s 58.1);
// sign +1: Tantra::RotateAbout as written, -1: the swivel turned the other way (the cups' thrust up at about 64 deg)
static void PodTable(gd::BellyState& s, double sign, double& podMax) {
    const double ax[4][3] = {{-0.96403, 0.26579, 0}, {0.96403, 0.26579, 0}, {-0.96475, 0.26316, 0}, {0.96475, 0.26316, 0}};
    const double px[4] = {-11.12912, 11.12912, -10.99911, 10.99911};
    const double sA = 43.6, sF = 84.0, cg = 58.1, fore = (cg - sA) / (sF - sA), big = std::fmax(fore, 1 - fore);
    const double share[2] = {(1 - fore) / big, fore / big};
    const double so = 15 * gd::kD2R, sd = 25 * gd::kD2R;
    double wsum = 0;
    for (int p = 0; p < 4; ++p) wsum += share[p / 2];
    podMax = 1.4e9 / 4 * wsum;
    for (int i = 0; i < gd::kPodTab; ++i) {
        const double a = sign * i * 10 * gd::kD2R, c = std::cos(a), sn = std::sin(a);
        double F = 0, U = 0;
        for (int p = 0; p < 4; ++p) {
            const double sg = px[p] >= 0 ? 1 : -1;
            const double b[3] = {-sg * std::sin(so) * std::cos(sd), std::sin(sd), std::cos(so) * std::cos(sd)};
            const double k[3] = {ax[p][0] * sg, ax[p][1] * sg, ax[p][2] * sg};
            const double cr[3] = {k[1] * b[2] - b[1] * k[2], k[2] * b[0] - b[2] * k[0], k[0] * b[1] - b[0] * k[1]};
            const double d = k[0] * b[0] + k[1] * b[1] + k[2] * b[2];
            const double r[3] = {b[0] * c + cr[0] * sn + k[0] * d * (1 - c), b[1] * c + cr[1] * sn + k[1] * d * (1 - c), b[2] * c + cr[2] * sn + k[2] * d * (1 - c)};
            F += share[p / 2] * r[2]; U += share[p / 2] * r[1];
        }
        s.podTab[i][0] = F / wsum; s.podTab[i][1] = U / wsum;
    }
}

struct Sim {
    gd::BellyLand ap; gd::BellyState s;
    double t = 0, h = 500, vz = 0, vF = 60, vS = 0, bank = 0, ang = 0, tgt = 0, lv = 0, out = 1, gear = 0, rho = 1.225, wind = 3.0;
    bool wanted = true, gearDown = false, landed = false, contact = false;
    double hGear = -1, maxDev = 0;                 // the altitude when the gear locked
    double marchMax = 8.86e8, podMax = 0;
    void Fill(double dt) {
        s.simt = t; s.dt = dt; s.alt = h; s.vz = vz; s.vF = vF; s.vS = vS; s.hdg = 90; s.pitch = 0; s.bank = bank;
        s.mach = std::hypot(vF, vS) / 340.0; s.contact = contact;
        s.marchMax = marchMax; s.podMax = podMax; s.podMaxEst = false;
        s.podAngle = ang; s.podCmdAngle = tgt; s.podOut = out; s.podsWanted = wanted;
        s.podAimed = out >= 1 && std::fabs(ang - tgt) < 3.0; s.podLv = lv; s.podLvMax = 1.0;
        s.gearLying = true; s.gear = gear; s.gearDown = gearDown; s.wingsFolded = false; s.plantRun = true;
    }
    void Run(double T, double dt = 0.05) {
        for (double end = t + T; t < end - 1e-9; t += dt) {
            Fill(dt);
            ap.Step(s, t);
            const gd::BellyCmd& c = ap.Cmd();
            const bool aimedBefore = s.podAimed;
            if (ap.Engaged()) {
                if (c.pods) { wanted = true; tgt = c.podAngle; }
                if (c.gear && !gearDown) gearDown = true;
            }
            // the ship: the pods out, the cups slewing, the levels
            if (wanted) out = std::fmin(1.0, out + dt / 12.0);
            const double goal = out >= 1 ? tgt : 0.0;
            ang += std::fmax(-15 * dt, std::fmin(15 * dt, goal - ang));
            const double lvCmd = ap.Engaged() && c.thrust && out >= 1 ? c.podLv : (ap.Engaged() ? 0.0 : lv);
            lv += (lvCmd - lv) * std::fmin(1.0, 3 * dt);
            const double march = ap.Engaged() && c.thrust ? c.march : 0.0;
            if (gearDown) { gear = std::fmin(1.0, gear + dt / 12.0); if (gear >= 1 && hGear < 0) hGear = h; }
            bank += (( ap.Engaged() && c.attitude ? c.bank : 0.0) - bank) * std::fmin(1.0, dt / 2.0);
            double f, u; gd::BellyLand::PodDir(s, ang, f, u);
            const double Fp = (out >= 1 && std::fabs(ang - tgt) < 3.0 && aimedBefore ? podMax : 0.0) * lv;
            const double m = s.mass, cb = std::cos(bank * gd::kD2R), sb = std::sin(bank * gd::kD2R);
            double Fz = Fp * u * cb - m * s.g - 0.5 * rho * vz * std::fabs(vz) * 4000.0;
            double Fx = Fp * f + march * marchMax - 0.5 * rho * vF * std::fabs(vF) * 260.0;
            double Fy = Fp * u * sb + 0.5 * rho * (wind - vS) * std::fabs(wind - vS) * 3500.0;
            if (contact) { Fz = 0; vz = 0; vF *= 0.9; vS *= 0.9; Fx = Fy = 0; }
            vz += Fz / m * dt; vF += Fx / m * dt; vS += Fy / m * dt;
            h += vz * dt;
            if (!contact && h <= 14.0) { contact = true; }
            if (contact) h = 14.0;
        }
        Fill(dt);
    }
};

int main() {
    std::printf("== belly landing autopilot ==\n");
    // the pods' lift against the weight of the 52 kt ship
    for (int sg : {-1, 1}) {
        for (double g : {9.81, 3.71}) {
            Sim z; z.s.g = g; PodTable(z.s, sg, z.podMax); z.Fill(0.05);
            z.ap.Step(z.s, 0);
            std::printf("     table %s, g %.2f: pods %.3g N at full, lift max %.3g N, weight %.3g N, x%.2f, vertical cup %.1f deg\n",
                        sg < 0 ? "swivel reversed" : "as UpdatePods", g, z.podMax, z.ap.LiftMax(), z.ap.Weight(), z.ap.LiftMax() / z.ap.Weight(), z.ap.VertAngle());
        }
    }
    {   // the table exactly as UpdatePods computes it: the thrust goes down as the cups turn - the check refuses
        Sim z; PodTable(z.s, 1, z.podMax); z.Fill(0.05); z.ap.Step(z.s, 0); z.ap.Engage(0);
        Check(!z.ap.Engaged(), "UpdatePods' table as written: ВКЛ refused (no cup angle lifts the ship)");
        if (!z.ap.Log().lines.empty()) std::printf("     %s\n", U8(z.ap.Log().lines.front().txt).c_str());
    }
    // ---- Earth: hold, the targets changed in flight, the landing ----
    {
        Sim z; PodTable(z.s, -1, z.podMax);
        z.ang = z.tgt = 0; z.Fill(0.05); z.ap.Step(z.s, 0);
        z.ang = z.tgt = z.ap.VertAngle(); z.lv = z.s.mass * 9.81 / (z.podMax * 0.97);
        z.Run(0.1); z.ap.Engage(0);
        Check(z.ap.Engaged(), "Earth: ВКЛ at 500 m, 60 m/s");
        z.Run(60);
        std::printf("     t %.0f: alt %.2f m (500), vF %.2f m/s (60), vS %.2f, cups %.1f deg, pods %.2f, march %.3f\n", z.t, z.h, z.vF, z.vS, z.ang, z.lv, z.ap.Cmd().march);
        Check(std::fabs(z.h - 500) < 3 && std::fabs(z.vF - 60) < 1.5, "Earth: holds 500 m and 60 m/s");
        z.ap.StepAlt(1, true); z.Run(1); z.ap.StepAlt(1, false); z.ap.StepAlt(1, false);
        z.Run(90);
        std::printf("     t %.0f: alt %.2f m (620), vz %.2f, vF %.2f\n", z.t, z.h, z.vz, z.vF);
        Check(std::fabs(z.h - 620) < 2 && std::fabs(z.vF - 60) < 1.5, "Earth: ВЫСОТА +100 +10 +10 in flight -> 620 m, the speed held");
        z.ap.StepSpd(-1, true); z.ap.StepSpd(-1, true); z.Run(5); z.ap.StepSpd(-1, true);
        z.Run(110);
        std::printf("     t %.0f: alt %.2f m, vF %.2f m/s (0), vS %.2f, cups %.1f deg, march %.3f\n", z.t, z.h, z.vF, z.vS, z.ang, z.ap.Cmd().march);
        Check(std::fabs(z.vF) < 0.5 && std::fabs(z.h - 620) < 3, "Earth: ГОР. СКОРОСТЬ -20 x3 in flight -> hover, the altitude held");
        z.ap.StepSpd(1, false); z.ap.StepAlt(-1, true); z.ap.StepAlt(-1, true); z.ap.StepAlt(-1, true); z.ap.StepAlt(-1, true);
        z.Run(120);
        std::printf("     t %.0f: alt %.2f m (220), vF %.2f m/s (5)\n", z.t, z.h, z.vF);
        Check(std::fabs(z.h - 220) < 3 && std::fabs(z.vF - 5) < 0.7, "Earth: +5 m/s, ВЫСОТА -100 x4 -> 220 m at 5 m/s");
        z.ap.Land(0);
        Check(z.ap.Mode() == gd::BellyLand::kLand, "Earth: ПОСАДКА");
        while (z.h > 100 && z.t < 2000) z.Run(0.5);
        z.ap.StepAlt(1, false);                                   // a hot correction during the descent: back to the hold
        Check(z.ap.Mode() == gd::BellyLand::kHoldM, "Earth: ВЫСОТА + during the descent -> hold");
        z.Run(30);
        std::printf("     t %.0f: alt %.2f m (hold %.0f)\n", z.t, z.h, z.ap.AltT());
        Check(std::fabs(z.h - z.ap.AltT()) < 2, "Earth: holds the new altitude");
        z.ap.Land(0);
        for (int i = 0; i < 4000 && z.ap.Mode() != gd::BellyLand::kLandedM; ++i) z.Run(0.25);
        std::printf("     t %.0f: touchdown vz %.2f m/s, vh %.2f m/s, gear locked at %.0f m, phase %d\n", z.t, z.ap.TdVz(), z.ap.TdVh(), z.hGear, z.ap.Phase());
        Check(z.ap.Mode() == gd::BellyLand::kLandedM, "Earth: landed, cut-off");
        Check(z.ap.TdVz() <= 1.5, "Earth: touchdown vertical <= 1.5 m/s");
        Check(z.ap.TdVh() <= 1.0, "Earth: touchdown horizontal <= 1 m/s");
        Check(z.hGear >= 140, "Earth: the lying gear on its locks above ~150 m");
        Check(z.ap.Cmd().podLv == 0.0 && !z.ap.Engaged(), "Earth: the pods at 0, the autopilot let go");
        int n = 0;
        for (const gd::Line& l : z.ap.Log().lines) if (n++ < 6) std::printf("       %s %s\n", U8(l.tt).c_str(), U8(l.txt).c_str());
    }
    // ---- Mars: 300 m, 30 m/s, straight to ПОСАДКА ----
    {
        Sim z; z.s.g = 3.71; z.rho = 0.02; z.h = 300; z.vF = 30; PodTable(z.s, -1, z.podMax);
        z.Fill(0.05); z.ap.Step(z.s, 0);
        z.ang = z.tgt = z.ap.VertAngle(); z.lv = z.s.mass * 3.71 / (z.podMax * 0.97);
        z.Run(0.1); z.ap.Engage(0); z.Run(20);
        Check(z.ap.Engaged() && std::fabs(z.h - 300) < 3, "Mars: ВКЛ, holds 300 m");
        z.ap.Land(0);
        for (int i = 0; i < 4000 && z.ap.Mode() != gd::BellyLand::kLandedM; ++i) z.Run(0.25);
        std::printf("     t %.0f: touchdown vz %.2f m/s, vh %.2f m/s, gear locked at %.0f m\n", z.t, z.ap.TdVz(), z.ap.TdVh(), z.hGear);
        Check(z.ap.Mode() == gd::BellyLand::kLandedM && z.ap.TdVz() <= 1.5 && z.ap.TdVh() <= 1.0, "Mars: landed within the norms");
    }
    // ---- the pods in the bays: ВКЛ at 7 km, they come out while the ship falls, then it catches itself ----
    {
        Sim z; z.h = 7000; z.vF = 50; z.out = 0; z.wanted = false; PodTable(z.s, -1, z.podMax);
        z.Fill(0.05); z.ap.Step(z.s, 0); z.ap.Engage(0);
        Check(z.ap.Engaged(), "pods in: ВКЛ at 7 km accepted (the fall and the stop fit)");
        double lo = z.h;
        for (int i = 0; i < 600; ++i) { z.Run(0.5); lo = std::fmin(lo, z.h); }
        std::printf("     t %.0f: alt %.0f m (lowest %.0f), vz %.2f m/s, phase %d\n", z.t, z.h, lo, z.vz, z.ap.Phase());
        Check(lo > 500 && std::fabs(z.vz) < 6 && z.ap.Phase() == 1, "pods in: caught, climbing back to the target");
        Sim y; y.h = 2000; y.vF = 50; y.out = 0; y.wanted = false; PodTable(y.s, -1, y.podMax);
        y.Fill(0.05); y.ap.Step(y.s, 0); y.ap.Engage(0);
        Check(!y.ap.Engaged(), "pods in: ВКЛ at 2 km refused (the fall would not stop in time)");
    }
    std::printf(fails ? "FAILED: %d\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
