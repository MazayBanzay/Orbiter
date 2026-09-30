// Scenarios for core/Damage: design entry survives, steep entry burns, overloads break the right parts.
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "../core/Damage.h"

using namespace tantra::damage;

namespace {
const double D = 3.14159265358979323846 / 180.0;
int fails = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++fails;
}
void run(Model& m, double t, const Flight& f, const Exposure& x, const Ground& g, bool en = true) {
    for (double s = 0; s < t; s += 0.1) m.Step(0.1, f, x, g, en);
}
void temps(const Model& m) {
    std::printf("     nose %.0f K  belly %.0f K  crest edges %.0f K  fin %.0f K  stern %.0f K   (limits %.0f %.0f %.0f %.0f)\n",
                m.Temperature(kZoneNose), m.Temperature(kZoneBelly), m.Temperature(kZoneCrestEdge), m.Temperature(kZoneFin),
                m.Temperature(kZoneStern), m.Limit(kZoneNose), m.Limit(kZoneBelly), m.Limit(kZoneCrestEdge), m.Limit(kZoneFin));
}
}  // namespace

int main() {
    Exposure clean;  // crests and fin out, everything else in
    Ground air;
    {   // design entry from low orbit: 2.9 kt, beta ~1000 kg/m2, peak heating near 65 km, 50 deg angle of attack
        Model m;
        Flight f;
        f.rho = 1.6e-4, f.v = 6500, f.mach = 22, f.aoa = 50 * D, f.gLoad = 1.5;
        run(m, 300, f, clean, air);
        temps(m);
        check(!m.Destroyed() && m.Integrity(kCrestPort) == 1.0, "design entry peak for 5 min: no damage");
    }
    {   // steep entry: ten times the density at 7 km/s
        Model m;
        Flight f;
        f.rho = 1.6e-3, f.v = 7200, f.mach = 24, f.aoa = 50 * D, f.gLoad = 4;
        run(m, 120, f, clean, air);
        temps(m);
        check(m.Integrity(kCrestPort) < 1.0 || m.Integrity(kBelly) < 1.0, "steep entry: edges / belly overheat");
    }
    {   // pods left out on a supersonic dash at sea level
        Model m;
        Flight f;
        f.rho = 1.225, f.v = 410, f.mach = 1.2, f.aoa = 2 * D;
        Exposure x = clean;
        x.pods = 1.0;
        run(m, 1, f, x, air);
        check(m.Lost(kPod0) && m.Lost(kPod3), "pods out at Mach 1.2 near the ground: torn off");
    }
    {   // crests pulled hard subsonic: 60 kPa at 30 deg
        Model m;
        Flight f;
        f.rho = 1.225, f.v = 313, f.mach = 0.92, f.aoa = 30 * D;
        run(m, 5, f, clean, air);
        std::printf("     crest load %.2f of rating\n", m.CrestLoad(0));
        check(m.Integrity(kCrestPort) < 1.0, "crests at 60 kPa, 30 deg: overloaded");
    }
    {   // hangar opened in flight
        Model m;
        Flight f;
        f.rho = 0.5, f.v = 150, f.mach = 0.5;
        Exposure x = clean;
        x.hangar = 1.0;
        run(m, 5, f, x, air);
        check(m.Lost(kHangarDoors), "hangar doors open at 5.6 kPa: torn off within seconds");
    }
    {   // touchdowns
        Flight f;
        for (double v : {2.0, 4.0, 8.0}) {
            Model m;
            Ground g;
            g.contact = g.touchdown = g.gearDown = true;
            g.vDown = v;
            m.Step(0.02, f, clean, g, true);
            std::printf("     touchdown %.0f m/s: legs %.2f\n", v, m.Integrity(kLegPort));
        }
        Model a, b, c;
        Ground g;
        g.contact = g.touchdown = g.gearDown = true;
        g.vDown = 2.0;
        a.Step(0.02, f, clean, g, true);
        g.vDown = 4.0;
        b.Step(0.02, f, clean, g, true);
        g.vDown = 8.0;
        c.Step(0.02, f, clean, g, true);
        check(a.Integrity(kLegPort) == 1.0 && b.Integrity(kLegPort) < 1.0 && c.Lost(kLegPort), "touchdown 2 ok / 4 damaged / 8 broken");
        check(!b.Destroyed() && b.Integrity(kHull) == 1.0, "4 m/s on the legs: hull untouched");
        Model h;
        g.vDown = 7.0;
        h.Step(0.02, f, clean, g, true);
        check(h.Lost(kLegPort) && h.Integrity(kHull) < 1.0 && !h.Destroyed(), "7 m/s on the legs: legs gone, hull damaged (3.6 m/s left)");
        Model e;
        g.vDown = 10.0;
        e.Step(0.02, f, clean, g, true);
        check(e.Destroyed(), "10 m/s on the legs: legs gone, hull breaks (8 m/s left)");
        Model d;
        g.gearDown = false;
        g.vDown = 6.0;
        d.Step(0.02, f, clean, g, true);
        check(d.Destroyed(), "belly landing at 6 m/s: hull lost");
    }
    {   // leg overload on a heavy planet
        Model m;
        Flight f;
        Ground g;
        g.contact = g.gearDown = true;
        g.legRatio[0] = 1.2;
        run(m, 5, f, clean, g);
        check(m.Integrity(kLegPort) < 1.0 && !m.Lost(kLegPort), "leg at 120 % of rating: damaged, not yet broken");
        g.legRatio[0] = 1.6;
        m.Step(0.02, f, clean, g, true);
        check(m.Lost(kLegPort), "leg at 160 %: broken");
    }
    {   // g-load and the damage switch
        Model m;
        Flight f;
        f.gLoad = 10;
        m.Step(0.02, f, clean, air, false);
        check(!m.Destroyed(), "damage off in Orbiter: nothing breaks");
        m.Step(0.02, f, clean, air, true);
        check(m.Destroyed(), "10 g: hull lost");
        char buf[256];
        m.Save(buf, sizeof buf);
        Model n;
        n.Load(buf);
        check(n.Destroyed(), "state saved and loaded");
    }
    std::printf(fails ? "%d FAILED\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
