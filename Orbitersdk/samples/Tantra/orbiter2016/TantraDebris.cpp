// Tantra debris (TantraDebris.dll): a piece of the broken ship as a rigid body. Orbiter has no collisions between vessels,
// but each piece meets the ground: it falls, tumbles, scrapes over the soil with friction and comes to rest - then it is
// handed to Orbiter's landed state (a body on stiff contact springs would tremble forever, and time warp would throw it
// off the planet). The contact points are the corners of its bounding box (TdBox in Debris_*.cfg, written by
// tools/gen_mesh.py), the springs sized to its mass (4 cm under its weight), damped near critically; a piece created
// partly in the ground is first lifted onto the surface.
#define ORBITER_MODULE
#include "orbitersdk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr double kG0 = 9.81;

class TantraDebris : public VESSEL4 {
public:
    TantraDebris(OBJHANDLE h, int fmodel) : VESSEL4(h, fmodel) {}

    void clbkSetClassCaps(FILEHANDLE cfg) override {
        char buf[256];
        if (oapiReadItem_string(cfg, const_cast<char*>("TdBox"), buf))
            std::sscanf(buf, "%lf %lf %lf %lf %lf %lf", &lo_.x, &lo_.y, &lo_.z, &hi_.x, &hi_.y, &hi_.z);
        Contacts();
    }

    void clbkPostCreation() override { Contacts(); }

    void clbkPreStep(double simt, double simdt, double mjd) override {
        (void)simt; (void)mjd;
        if (simdt <= 0.0) return;
        OBJHANDLE ref = GetSurfaceRef();
        if (!ref) return;
        VESSELSTATUS2 st;
        std::memset(&st, 0, sizeof st);
        st.version = 2;
        GetStatusEx(&st);
        if (st.status == 1) return;                                  // lying still: Orbiter holds it
        if (!lifted_) { Lift(ref); lifted_ = true; return; }          // born in the ground: onto the surface first
        VECTOR3 v, w;
        GetGroundspeedVector(FRAME_HORIZON, v);
        GetAngularVel(w);
        const bool contact = GroundContact();
        // the ground stops a piece, it does not throw it back: Orbiter's contact is explicit springs, and a piece
        // hitting the ground at hundreds of m/s bounced off them into the sky. Off the ground just after a contact and
        // going up: that rise is taken out (a real piece digs in, crumples and scrapes on)
        sinceContact_ = contact ? 0.0 : sinceContact_ + simdt;
        if (sinceContact_ < 3.0 && v.y > 0.5) {
            VECTOR3 up;
            HorizonInvRot(_V(0, 1, 0), up);
            const double m = GetMass();
            AddForce(up * (-(std::min)(m * v.y / 0.2, 20.0 * m * kG0)), _V(0, 0, 0));
        }
        still_ = contact && length(v) < 0.3 && length(w) < 0.05 ? still_ + simdt : 0.0;
        if (still_ > 2.0 || (contact && oapiGetTimeAcceleration() > 10.0)) Rest(ref);
    }

private:
    // the corners of the box: the bottom four first (Orbiter's ground plane: front, left rear, right rear), then the top
    void Contacts() {
        const double m = (std::max)(1000.0, GetMass());
        const double k = m * kG0 / (4.0 * 0.04), c = 2.0 * 0.6 * std::sqrt(k * m / 4.0);
        const VECTOR3 p[8] = {{0.5 * (lo_.x + hi_.x), lo_.y, hi_.z}, {lo_.x, lo_.y, lo_.z}, {hi_.x, lo_.y, lo_.z}, {hi_.x, lo_.y, hi_.z},
                              {lo_.x, lo_.y, hi_.z}, {lo_.x, hi_.y, lo_.z}, {hi_.x, hi_.y, lo_.z}, {hi_.x, hi_.y, hi_.z}};
        TOUCHDOWNVTX t[9];
        for (int i = 0; i < 8; ++i) t[i] = {p[i], k, c, 0.7, 0.7};
        t[8] = {{lo_.x, hi_.y, hi_.z}, k, c, 0.7, 0.7};
        SetTouchdownPoints(t, 9);
    }

    // lift the piece so that no corner is under the ground (the springs would fire it off)
    void Lift(OBJHANDLE ref) {
        const double R = oapiGetSize(ref);
        const VECTOR3 c[8] = {{lo_.x, lo_.y, lo_.z}, {hi_.x, lo_.y, lo_.z}, {lo_.x, hi_.y, lo_.z}, {hi_.x, hi_.y, lo_.z},
                              {lo_.x, lo_.y, hi_.z}, {hi_.x, lo_.y, hi_.z}, {lo_.x, hi_.y, hi_.z}, {hi_.x, hi_.y, hi_.z}};
        double deepest = 0.0;
        for (const VECTOR3& q : c) {
            VECTOR3 g;
            Local2Global(q, g);
            double lng, lat, rad;
            oapiGlobalToEqu(ref, g, &lng, &lat, &rad);
            deepest = (std::max)(deepest, R + oapiSurfaceElevation(ref, lng, lat) - rad);
        }
        if (deepest <= 0.02) return;
        VESSELSTATUS2 st;
        std::memset(&st, 0, sizeof st);
        st.version = 2;
        GetStatusEx(&st);
        VECTOR3 gp, pp;
        GetGlobalPos(gp);
        oapiGetGlobalPos(ref, &pp);
        VECTOR3 up = gp - pp;
        up = up / length(up);
        st.rpos = st.rpos + up * (deepest + 0.05);
        DefSetStateEx(&st);
    }

    // at rest: Orbiter's landed state in the attitude it lies in, its centre at its height
    void Rest(OBJHANDLE ref) {
        VESSELSTATUS2 vs;
        std::memset(&vs, 0, sizeof vs);
        vs.version = 2;
        GetStatusEx(&vs);
        double lng, lat, rad;
        GetEquPos(lng, lat, rad);
        vs.status = 1;
        vs.rbody = ref;
        vs.surf_lng = lng;
        vs.surf_lat = lat;
        oapiGetHeading(GetHandle(), &vs.surf_hdg);
        MATRIX3 Rs, Rp, L;
        GetRotationMatrix(Rs);
        oapiGetRotationMatrix(ref, &Rp);
        const double* a = Rp.data; const double* b = Rs.data; double* l = L.data;   // L = Rp^T Rs
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) l[3 * i + j] = a[i] * b[j] + a[3 + i] * b[3 + j] + a[6 + i] * b[6 + j];
        vs.arot = _V(std::atan2(L.m23, L.m33), -std::asin((std::max)(-1.0, (std::min)(1.0, L.m13))), std::atan2(L.m12, L.m11));
        vs.vrot = _V(GetAltitude(ALTMODE_GROUND), 0.0, 0.0);
        DefSetStateEx(&vs);
        still_ = 0.0;
    }

    VECTOR3 lo_ = {-1, -1, -1}, hi_ = {1, 1, 1};
    double still_ = 0.0, sinceContact_ = 1e9;
    bool lifted_ = false;
};

}  // namespace

DLLCLBK VESSEL* ovcInit(OBJHANDLE hvessel, int flightmodel) { return new TantraDebris(hvessel, flightmodel); }
DLLCLBK void ovcExit(VESSEL* vessel) { delete static_cast<TantraDebris*>(vessel); }
