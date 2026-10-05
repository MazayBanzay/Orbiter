// Tantra: what a person walking OUTSIDE the ship bumps into (OrbiterCrew asks through OcInteriorExt::OuterWalls).
// The cup feet standing on the ground are the obstacles at a person's height: each a solid ring of its radius with its
// skirt (the columns start at the ankles, 5 m up - over the heads). Their places follow the gear's pose (UpdateGear).
// Coordinates: the ship frame (as Walls inside); "up" is the world's vertical in it. The rim is a wall from outside: a
// step from outside to inside stops at the rim. Someone already inside (set down there) is not thrown out - he or she
// walks out freely.
#include "Tantra.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

// The supports at the ground as solids for other vessels (TantraSolids.h): every leg standing on the ground as a capsule
// from its foot to its upper anchor (blade: the trunnion; stern leg: its hinge; kangaroo: its hip), every foot as a short
// vertical capsule of its cup's radius, the lift cabin as a box while it is down. Ship frame.
int Tantra::OuterSolids(TantraSolid* out, int max) const {
    namespace m = tantra::mesh;
    int n = 0;
    auto cap = [&](const VECTOR3& a, const VECTOR3& b, double r) {
        if (n >= max) return;
        TantraSolid& s = out[n++];
        std::memset(&s, 0, sizeof s);
        s.kind = 1; s.a = a; s.b = b; s.r = r;
    };
    const VECTOR3 up = cupUp_;
    const tantra::CarriagePose& p = carriage_.Pose();
    for (int l = 0; l < 7; ++l) {
        if (!cupOn_[l]) continue;
        const VECTOR3 sole = cupC_[l];
        cap(sole, sole + up * 0.9, cupR_[l]);                                        // the cup on the ground
        const double footH = l == 6 ? m::kKangFootH : m::kFootH;
        const VECTOR3 ankle = sole + up * footH;
        VECTOR3 top;
        double r;
        if (l < 2) { top = _V((l == 0 ? -1.0 : 1.0) * m::kHipXOut, 0.0, p.hipS - frameS_); r = 3.4; }   // blade 6.7 x 2.8
        else if (l < 6) { const m::LegRig& L = m::kLegs[l - 2]; top = _V(L.hinge.x, L.hinge.y, L.hinge.z + MeshDZ()); r = 1.6; }
        else { top = _V(m::kKangHip.x, m::kKangHip.y, m::kKangHip.z + MeshDZ()); r = 1.2; }
        cap(ankle, top, r);
    }
    if (lift_.AtGround() && n < max) {                                                // the lift cabin standing on the ground
        TantraSolid& s = out[n++];
        std::memset(&s, 0, sizeof s);
        s.kind = 2;
        const VECTOR3 f = lift_.Foot();
        s.c = _V(f.x, f.y, Zf(m::kAirlockS)) + up * 1.3;
        s.half = _V(1.6, 1.3, 1.6);
        s.R = _M(1, 0, 0, 0, 1, 0, 0, 0, 1);
    }
    return n;
}

extern "C" __declspec(dllexport) int tantraOuterSolids(OBJHANDLE ship, TantraSolid* out, int max) {
    if (!ship || !out || max <= 0 || !oapiIsVessel(ship)) return 0;
    VESSEL* v = oapiGetVesselInterface(ship);
    if (!v || !v->GetClassNameA() || _stricmp(v->GetClassNameA(), "Tantra")) return 0;
    return static_cast<const Tantra*>(v)->OuterSolids(out, max);
}
