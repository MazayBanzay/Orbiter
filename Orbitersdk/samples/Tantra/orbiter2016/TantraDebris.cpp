// Tantra debris (TantraDebris.dll): a piece of the broken ship as a rigid body. Orbiter has no collisions between vessels,
// but each piece meets the ground: it falls, tumbles, scrapes over the soil with friction and comes to rest - then it is
// handed to Orbiter's landed state (a body on stiff contact springs would tremble forever, and time warp would throw it
// off the planet). (2026-10-09 rework) The contact points are the piece's own extreme points (TdPoints in Debris_*.cfg,
// written by tools/gen_mesh.py; the corners of its bounding box held a leg lying on its side 10 m up on its foot's box),
// the springs sized to its mass (4 cm under its weight on three points), damped near critically; a piece created partly
// in the ground is first lifted onto the surface. A hull chunk carries the ship's stowed parts as labelled groups (cfg
// Parts): the ship tells it (clbkGeneric kMsgHide, a bit mask) which of them left as pieces of their own - those it hides.
#define ORBITER_MODULE
#include "orbitersdk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constexpr double kG0 = 9.81;
constexpr int kMsgHide = 0x7A01;     // the ship's message: prm = the mask of the parts to hide (Tantra::SpawnDebris)

class TantraDebris : public VESSEL4 {
public:
    TantraDebris(OBJHANDLE h, int fmodel) : VESSEL4(h, fmodel) {}

    void clbkSetClassCaps(FILEHANDLE cfg) override {
        char buf[2048];
        pts_.clear();
        int n = 0;
        if (oapiReadItem_int(cfg, const_cast<char*>("TdCount"), n)) {   // one point a line (TdP00 = x y z): no long lines
            for (int i = 0; i < n && i < 32; ++i) {
                char key[16];
                std::snprintf(key, sizeof key, "TdP%02d", i);
                VECTOR3 v;
                if (oapiReadItem_vec(cfg, key, v)) pts_.push_back(v);
            }
        } else if (oapiReadItem_string(cfg, const_cast<char*>("TdBox"), buf)) {   // older pieces: the box corners
            VECTOR3 lo, hi;
            std::sscanf(buf, "%lf %lf %lf %lf %lf %lf", &lo.x, &lo.y, &lo.z, &hi.x, &hi.y, &hi.z);
            pts_ = {{0.5 * (lo.x + hi.x), lo.y, hi.z}, {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z},
                    {lo.x, lo.y, hi.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
        }
        if (pts_.size() < 3) pts_ = {{0, -1, 1}, {-1, -1, -1}, {1, -1, -1}, {0, 1, 0}};
        parts_.clear();
        if (oapiReadItem_string(cfg, const_cast<char*>("Parts"), buf)) {   // "bit:g0,g1 bit:g2 ..."
            const char* p = buf;
            while (*p) {
                while (*p == ' ') ++p;
                if (!*p) break;
                char* e = nullptr;
                const int bit = static_cast<int>(std::strtol(p, &e, 10));
                if (e == p || *e != ':') break;
                p = e + 1;
                for (;;) {
                    const int gi = static_cast<int>(std::strtol(p, &e, 10));
                    if (e == p) break;
                    parts_.push_back({bit, gi});
                    p = e;
                    if (*p != ',') break;
                    ++p;
                }
            }
        }
        Contacts();
    }

    void clbkPostCreation() override { Contacts(); }

    void clbkLoadStateEx(FILEHANDLE scn, void* status) override {
        char* line;
        while (oapiReadScenario_nextline(scn, line)) {
            if (!_strnicmp(line, "HIDE", 4)) hide_ = static_cast<unsigned>(std::strtoul(line + 4, nullptr, 10));
            else ParseScenarioLineEx(line, status);
        }
    }

    void clbkSaveState(FILEHANDLE scn) override {
        VESSEL4::clbkSaveState(scn);
        if (hide_) oapiWriteScenario_int(scn, const_cast<char*>("HIDE"), static_cast<int>(hide_));
    }

    int clbkGeneric(int msgid, int prm, void* context) override {
        (void)context;
        if (msgid == kMsgHide) { hide_ = static_cast<unsigned>(prm); ApplyHide(); return 1; }
        return 0;
    }

    void clbkVisualCreated(VISHANDLE vis, int refcount) override { (void)refcount; vis_ = vis; ApplyHide(); }
    void clbkVisualDestroyed(VISHANDLE vis, int refcount) override { (void)refcount; if (vis == vis_) vis_ = nullptr; }

    void clbkPreStep(double simt, double simdt, double mjd) override {
        (void)simt; (void)mjd;
        if (simdt <= 0.0) return;
        if (std::fabs(GetMass() - massK_) > 0.1 * massK_) Contacts();    // the ship gave it its real mass
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
    // the piece's own points: the first three span its underside (Orbiter's ground plane), the rest its hull
    void Contacts() {
        const double m = (std::max)(1000.0, GetMass());
        massK_ = m;
        const double k = m * kG0 / (3.0 * 0.04), c = 2.0 * 0.6 * std::sqrt(k * m / 3.0);
        std::vector<TOUCHDOWNVTX> t;
        for (const VECTOR3& p : pts_) t.push_back({p, k, c, 0.7, 0.7});
        SetTouchdownPoints(t.data(), static_cast<DWORD>(t.size()));
    }

    void ApplyHide() {
        if (!vis_ || parts_.empty()) return;
        DEVMESHHANDLE mesh = GetDevMesh(vis_, 0);
        if (!mesh) return;
        for (const Part& p : parts_) {
            GROUPEDITSPEC s = {};
            s.flags = (hide_ >> p.bit) & 1u ? GRPEDIT_ADDUSERFLAG : GRPEDIT_DELUSERFLAG;
            s.UsrFlag = 0x3;
            oapiEditMeshGroup(mesh, static_cast<DWORD>(p.group), &s);
        }
    }

    // lift the piece so that none of its points is under the ground (the springs would fire it off)
    void Lift(OBJHANDLE ref) {
        const double R = oapiGetSize(ref);
        double deepest = 0.0;
        for (const VECTOR3& q : pts_) {
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

    struct Part { int bit, group; };
    std::vector<VECTOR3> pts_;
    std::vector<Part> parts_;
    unsigned hide_ = 0;
    VISHANDLE vis_ = nullptr;
    double still_ = 0.0, sinceContact_ = 1e9, massK_ = 1.0;
    bool lifted_ = false;
};

}  // namespace

DLLCLBK VESSEL* ovcInit(OBJHANDLE hvessel, int flightmodel) { return new TantraDebris(hvessel, flightmodel); }
DLLCLBK void ovcExit(VESSEL* vessel) { delete static_cast<TantraDebris*>(vessel); }
