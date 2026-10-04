// Tantra: what a person walking OUTSIDE the ship bumps into (OrbiterCrew asks through OcInteriorExt::OuterWalls).
// The cup feet standing on the ground are the obstacles at a person's height: each a solid ring of its radius with its
// skirt (the columns start at the ankles, 5 m up - over the heads). Their places follow the gear's pose (UpdateGear).
// Coordinates: the ship frame (as Walls inside); "up" is the world's vertical in it. The rim is a wall from outside: a
// step from outside to inside stops at the rim. Someone already inside (set down there) is not thrown out - he or she
// walks out freely.
#include "Tantra.h"

#include <algorithm>
#include <cmath>

void Tantra::OuterWalls(const VECTOR3& from, VECTOR3& to, double radius, double height) const {
    const VECTOR3 up = cupUp_;
    const double now = oapiGetSimTime();
    static double logT = -1e9;
    static bool first = true;
    if (first) {
        first = false;
        for (int l = 0; l < 7; ++l)
            if (cupOn_[l])
                oapiWriteLogV("Tantra: outer solids - cup %d at (%.1f %.1f %.1f) R %.2f; the person's feet at (%.1f %.1f %.1f)",
                              l, cupC_[l].x, cupC_[l].y, cupC_[l].z, cupR_[l], from.x, from.y, from.z);
    }
    for (int l = 0; l < 7; ++l) {
        if (!cupOn_[l]) continue;
        const VECTOR3 c = cupC_[l];
        // the person's feet against the cup's height: the rim on the ground, the skirt and the ribs up to ~2.5 m
        const double hFeet = dotp(to - c, up);
        if (hFeet + height < -0.3 || hFeet > 2.5) continue;
        auto horiz = [&up](VECTOR3 d) { return d - up * dotp(d, up); };
        const VECTOR3 dt = horiz(to - c), df = horiz(from - c);
        const double r = length(dt), rf = length(df), minR = cupR_[l] + radius;
        // outside and staying out; or deep inside already (set down there: walks out freely). At the rim itself (the
        // crew module puts the feet back on the ground after a stop - a millimetre inside) it is still outside.
        if (r >= minR || rf < cupR_[l] - 1.0) continue;
        // stopped at the rim along the line from the axis
        const VECTOR3 dir = r > 1e-3 ? dt / r : df / (std::max)(1e-6, rf);
        to = to + dir * (minR - r);
        if (now - logT > 1.0) {
            logT = now;
            oapiWriteLogV("Tantra: outer solids - a person stopped at the rim of cup %d (%.1f m from its axis)", l, rf);
        }
    }
}
