// The surface temperatures of the bodies, by the Sun's elevation - one table for the vehicles and (exported by TVehicles.dll,
// tvSurfaceTemps) for OrbiterCrew. Day and night only (the user, 2026-10-08): no seasons, no latitude, no weather; the
// values are the equatorial day-night extremes of the bodies' surface and of the air 1.5 m over it.
// Airless bodies (the Moon, Io): the surface follows the radiative balance, T^4 = Tn^4 + (Td^4 - Tn^4) sin(elev).
// Bodies with air (Earth, Mars, Titan): a softer curve, T = Tn + (Td - Tn) sqrt(sin(elev)) - the air holds the night up.
#pragma once
#include <cmath>
#include <cstring>

namespace tvthermal {

struct Body { const char* name; double gDay, gNight, aDay, aNight; bool air; };
// K. Earth 32 / 7 C ground, 27 / 11 C air; the Moon 390 / 95 K (regolith, noon / night); Mars 290 / 175 K ground, 245 / 185 K
// air (-28 / -88 C); Titan 94 / 93 K under a 146 kPa haze (a degree of difference); Io 130 / 90 K (the volcanoes aside)
constexpr Body kBodies[] = {
    {"Earth", 305.0, 280.0, 300.0, 284.0, true},
    {"Moon",  390.0,  95.0,   0.0,   0.0, false},
    {"Mars",  290.0, 175.0, 245.0, 185.0, true},
    {"Titan",  94.0,  93.0,  94.0,  93.0, true},
    {"Io",    130.0,  90.0,   0.0,   0.0, false},
};

struct Temps { bool known; bool air; double ground, airT; };

// sinElev: the Sun over the local horizon (-1..1). known = false: the body is not in the table
inline Temps SurfaceTemps(const char* body, double sinElev) {
    Temps t{false, false, 0.0, 0.0};
    if (!body) return t;
    for (const Body& b : kBodies) {
        if (std::strcmp(b.name, body)) continue;
        const double s = sinElev > 0.0 ? sinElev : 0.0;
        t.known = true; t.air = b.air;
        if (b.air) {
            const double f = std::sqrt(s);
            t.ground = b.gNight + (b.gDay - b.gNight) * f;
            t.airT = b.aNight + (b.aDay - b.aNight) * f;
        } else {
            const double n4 = std::pow(b.gNight, 4), d4 = std::pow(b.gDay, 4);
            t.ground = std::pow(n4 + (d4 - n4) * s, 0.25);
            t.airT = 0.0;
        }
        return t;
    }
    return t;
}

}  // namespace tvthermal
