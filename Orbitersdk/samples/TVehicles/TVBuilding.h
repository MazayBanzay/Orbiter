// TVehicles\Building - the spaceport's technical point ТП-1 (Tantra_Design\base\TECHPOINT.md; mesh and TechPointGeo.h:
// Tantra_Design\tools\build_techpoint.py). A building as a vessel: the D3D11 client draws no base objects, and a vessel has
// what a base needs here - an interior OrbiterCrew walks in, collisions, lights, animations.
//   ПОМ. 101 the hall (reception and rest, the duty post БЩУ), 102 life support and the gas store, 103 the tool room: air
//   without end (101.3 kPa). ШК-1 (to the yard) and ШК-2 (to the hangar): airlocks as the Э.МПУ's and the Tantra's - the
//   chamber's pressure, depressurising (the pump НО) and pressurising (the valve КН), the two doors interlocked (one opens only
//   with the other shut and the pressures equal). ПОМ. 201 the hangar: unpressurised, the gate ВА-1 (two sliding leaves), the
//   lighting board ЩО-1 (work, duty, emergency, the floods).
//   The panels as the Tantra's lift panel: numbered push buttons with lit caps (dark / lit / blinking / fast: do this first), a
//   status screen (a header with the board's code, rows "parameter - value", '+' ready, '*' in progress, '!' fault; the
//   helmet display's glyphs). Inside the buttons are clicked with the mouse (OC_BUTTON); outside (the yard, the hangar)
//   the same boards are context nodes (F): OrbiterCrew clicks buttons for a person inside only.
//   Collisions as the Tantra's: the boxes written by the mesh generator; people inside - Ground / Walls, outside - OuterWalls,
//   the vehicles - tvOuterSolids (the Tantra's TantraSolid capsules, read by TVChassis::Obstacles).
//   The lights are the D3D11 client's local lights (LightEmitter): spots with shadows under the hangar's vault, points in the
//   rooms; each circuit switches its emitters and its lamps' glowing material.
#pragma once
#include "TechPointGeo.h"
#include "XRSound.h"
#include "../Tantra/orbiter2016/TantraGlyphs.h"
#include "D3D11Lights.h"

namespace tvb {
// the Tantra's outer solid (TantraSolids.h, TVChassis.h): the same layout, so TVChassis reads ours with its own type
struct Solid { int kind; VECTOR3 a, b; double r; VECTOR3 c, half; MATRIX3 R; };
constexpr double kHallKPa = 101.3, kKnee = 0.35;
inline double Clamp(double x, double a, double b) { return x < a ? a : x > b ? b : x; }

class Building;
inline std::vector<Building*>& All() { static std::vector<Building*> v; return v; }
// what the building costs a frame: OrbiterCrew's calls into it (walls, floors, items, nodes...) and its own step, in µs,
// written to Orbiter.log every 10 s («TVehicles Building ...: cost») - the user saw 30 fps inside, 2026-10-21
struct Cost { double us = 0; long calls = 0; };
inline Cost& CbCost() { static Cost c; return c; }
struct CostScope {
    LARGE_INTEGER t0; Cost& c;
    explicit CostScope(Cost& k) : c(k) { QueryPerformanceCounter(&t0); }
    ~CostScope() { LARGE_INTEGER t1, f; QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f); c.us += (t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart; ++c.calls; }
};

class Building : public VESSEL4 {
public:
    Building(OBJHANDLE h, int fm) : VESSEL4(h, fm) { All().push_back(this); for (bool& b : light_) b = true; light_[tp::kCDuty] = false; }
    ~Building() {
        auto& v = All(); v.erase(std::remove(v.begin(), v.end(), this), v.end());
        if (registered_ && api_.UnregisterInterior) api_.UnregisterInterior(GetHandle());
        for (SURFHANDLE& s : scr_) if (s) { oapiDestroySurface(s); s = nullptr; }
        delete snd_;
    }

    // ---------------- Orbiter ----------------
    void clbkSetClassCaps(FILEHANDLE) override {
        SetSize(40.0);
        SetEmptyMass(2.0e6);                                    // it stands: a heavy, stiff body on three wide points
        SetPMI(_V(200, 300, 200));
        SetCrossSections(_V(500, 900, 600));
        const double m = GetEmptyMass(), k = m * 1.62 / 0.005, c = 2.0 * std::sqrt(k * m);
        // the order as the Station's and the vehicles': front, rear left, rear right (the other way round Orbiter stands it on
        // its roof, under the ground - the first build did, 2026-10-21)
        TOUCHDOWNVTX t[4] = {{_V(2, 0, 13.5), k, c, 5, 5}, {_V(-21, 0, 1), k, c, 5, 5}, {_V(23, 0, -3.5), k, c, 5, 5}, {_V(0, 8.0, 4), k, c, 5, 5}};
        SetTouchdownPoints(t, 4);
        mesh_ = oapiLoadMeshGlobal("TVehicles\\TechPoint");
        meshIdx_ = AddMesh(mesh_);
        SetMeshVisibilityMode(meshIdx_, MESHVIS_ALWAYS);       // the people walk inside it: seen from within too
        att_ = CreateAttachment(false, _V(0, 0, 0), _V(0, 0, 1), _V(0, 1, 0), "OCINT");
        Animations();
        Lights();
    }
    void clbkPostCreation() override {
        snd_ = XRSound::CreateInstance("TVehiclesBuilding");
        if (snd_ && snd_->IsPresent()) {
            using PT = XRSound::PlaybackType;
            const char* f[] = {"click", "airlock", "hum", "lift", "alarm", "beep"};
            for (int i = 0; i < 6; ++i) { char p[64]; std::snprintf(p, sizeof p, "XRSound\\MPU\\%s.wav", f[i]); snd_->LoadWav(kSndClick + i, p, PT::Global); }
        }
        if (OBJHANDLE ref = GetSurfaceRef()) { const double R = oapiGetSize(ref); g_ = GGRAV * oapiGetMass(ref) / (R * R); }
        if (!api_.Load() || !api_.RegisterInterior) { oapiWriteLogV("TVehicles Building %s: OrbiterCrew not found - no interior", GetName()); return; }
        fns_ = OcInterior{};
        fns_.Attach = [](void* c) { return Self(c)->att_; };
        fns_.Ground = &Building::cGround;
        fns_.Walls = &Building::cWalls;
        fns_.Gravity = [](void* c, const VECTOR3*, VECTOR3* g) { *g = _V(0, -Self(c)->g_, 0); };
        fns_.Count = &Building::cCount;
        fns_.Item = &Building::cItem;
        fns_.Use = &Building::cUse;
        fns_.Seat = &Building::cSeat;
        api_.RegisterInterior(GetHandle(), &fns_, this);
        registered_ = true;
        if (api_.SetInteriorExt) {
            ext_ = OcInteriorExt{}; ext_.size = sizeof(OcInteriorExt);
            ext_.Seated = [](void*, int, int, int) {};
            ext_.OuterWalls = &Building::cOuterWalls;
            ext_.Cabin = &Building::cCabin;
            ext_.NodeCount = [](void*) { return int(kNodeN); };
            ext_.Node = &Building::cNode;
            ext_.Actions = &Building::cActions;
            ext_.Begin = [](void* c, int node, int act, int person) {
                OcAction a[8]; const int n = cActions(c, node, person, a, 8);
                for (int i = 0; i < n; ++i) if (a[i].id == act) return a[i].available;
                return 0;
            };
            ext_.End = [](void* c, int node, int act, int person, int done) {
                if (!done) return;
                OcAction a[8]; const int n = cActions(c, node, person, a, 8);
                for (int i = 0; i < n; ++i) if (a[i].id == act && a[i].available) { Self(c)->Act(node, act, person); return; }
            };
            ext_.Ceiling = [](void*, const VECTOR3* p) { return InZone(1, p->x, p->z) || InZone(2, p->x, p->z) ? tp::kLockCeilY : tp::kCeilY; };
            api_.SetInteriorExt(GetHandle(), &ext_);
        }
        oapiWriteLogV("TVehicles Building %s: registered (%d floors, %d solids inside, %d outside, %d vehicle capsules, %d panels, %d lights)",
                      GetName(), tp::kFloorsCount, tp::kSolidsInCount, tp::kSolidsOutCount, tp::kCapsCount, tp::kPanelN, tp::kLightN);
    }
    void clbkLoadStateEx(FILEHANDLE scn, void* vs) override {
        char* line;
        while (oapiReadScenario_nextline(scn, line)) {
            if (!std::strncmp(line, "TP_DOORS", 8)) {
                std::sscanf(line + 8, "%lf %lf %lf %lf", &door_[0], &door_[1], &door_[2], &door_[3]);
                for (int d = 0; d < tp::kDoorN; ++d) doorT_[d] = door_[d] = Clamp(door_[d], 0, 1);
            } else if (!std::strncmp(line, "TP_GATE", 7)) { std::sscanf(line + 7, "%lf", &gate_); gateT_ = gate_ = Clamp(gate_, 0, 1); }
            else if (!std::strncmp(line, "TP_LOCKP", 8)) std::sscanf(line + 8, "%lf %lf", &p_[0], &p_[1]);
            else if (!std::strncmp(line, "TP_LIGHTS", 9)) { unsigned m = 0; std::sscanf(line + 9, "%x", &m); for (int k = 0; k < tp::kCircuitN; ++k) light_[k] = (m >> k) & 1; emerg_ = (m >> 16) & 1; }
            else ParseScenarioLineEx(line, vs);
        }
    }
    void clbkSaveState(FILEHANDLE scn) override {
        VESSEL4::clbkSaveState(scn);
        char b[96];
        std::snprintf(b, sizeof b, "%.3f %.3f %.3f %.3f", door_[0], door_[1], door_[2], door_[3]); oapiWriteScenario_string(scn, const_cast<char*>("TP_DOORS"), b);
        std::snprintf(b, sizeof b, "%.3f", gate_); oapiWriteScenario_string(scn, const_cast<char*>("TP_GATE"), b);
        std::snprintf(b, sizeof b, "%.2f %.2f", p_[0], p_[1]); oapiWriteScenario_string(scn, const_cast<char*>("TP_LOCKP"), b);
        unsigned m = emerg_ ? 1u << 16 : 0u; for (int k = 0; k < tp::kCircuitN; ++k) if (light_[k]) m |= 1u << k;
        std::snprintf(b, sizeof b, "%x", m); oapiWriteScenario_string(scn, const_cast<char*>("TP_LIGHTS"), b);
    }
    void clbkPreStep(double, double simdt, double) override {
        const double dt = simdt > 0 ? (std::min)(simdt, 0.2) : 0.0;
        if (sysT_ == 0.0) { oapiWriteLogV("TVehicles Building %s: the Sun %.2f deg over the horizon (MJD %.4f)", GetName(), SunElevDeg(), oapiGetSimMJD()); Interiors(); }
        sysT_ += oapiGetSysStep();
        if (dt > 0) { Locks(dt); Gate(dt); }
        for (double* t : {&msgT_[0], &msgT_[1], &msgT_[2], &msgT_[3], &msgT_[4], &msgT_[5], &msgT_[6], &msgT_[7], &msgT_[8], &msgT_[9], &overrideT_[0], &overrideT_[1], &alertT_})
            *t = (std::max)(0.0, *t - oapiGetSysStep());
        Cost step; { CostScope cs_(step); ApplyLights(); Caps(); Screens(); }
        stepUs_ += step.us; ++frames_;
        if ((costT_ += oapiGetSysStep()) >= 10.0) {
            oapiWriteLogV("TVehicles Building %s: cost %.3f ms/frame (OrbiterCrew calls %.3f ms, %ld calls/frame; own step: lights, caps, screens %.3f ms)",
                          GetName(), (CbCost().us + stepUs_) / 1000.0 / frames_, CbCost().us / 1000.0 / frames_, CbCost().calls / (std::max)(1L, frames_), stepUs_ / 1000.0 / frames_);
            CbCost() = Cost{}; stepUs_ = 0; frames_ = 0; costT_ = 0;
        }
    }
    void clbkVisualCreated(VISHANDLE vis, int) override {
        dev_ = GetDevMesh(vis, meshIdx_);
        if (!dev_) return;
        for (int i = 0; i < tp::kPanelN; ++i) {
            const tp::Panel& p = tp::kPanels[i];
            if (p.screenSlot <= 0) continue;
            if (!scr_[i]) scr_[i] = oapiCreateSurfaceEx(p.w, p.h, OAPISURFACE_RENDERTARGET | OAPISURFACE_TEXTURE | OAPISURFACE_SKETCHPAD);
            if (scr_[i]) oapiSetTexture(dev_, p.screenSlot, scr_[i]);
        }
        for (int& s : capShown_) s = -1;
        for (int& s : circShown_) s = -1;
        scrT_ = 1e9;
    }
    void clbkVisualDestroyed(VISHANDLE, int) override { dev_ = nullptr; }

    // the vehicles' solids (tvOuterSolids): the fixed capsules and the gate's leaves where they are now
    int OuterSolids(Solid* out, int max) const {
        int n = 0;
        for (int i = 0; i < tp::kCapsCount && n < max; ++i, ++n) {
            Solid& s = out[n]; std::memset(&s, 0, sizeof s); s.kind = 1;
            s.a = _V(tp::kCaps[i].a[0], tp::kCaps[i].a[1], tp::kCaps[i].a[2]); s.b = _V(tp::kCaps[i].b[0], tp::kCaps[i].b[1], tp::kCaps[i].b[2]); s.r = tp::kCaps[i].r;
        }
        for (int k = 0; k < 2 && n < max; ++k) {
            double x0, x1; Leaf(k, x0, x1);
            Solid& s = out[n++]; std::memset(&s, 0, sizeof s); s.kind = 1; s.r = 0.35;
            const double z = 0.5 * (tp::kGateZ0 + tp::kGateZ1);
            s.a = _V(x0 + 0.3, 1.0, z); s.b = _V(x1 - 0.3, 1.0, z);
        }
        return n;
    }

private:
    static Building* Self(void* c) { return static_cast<Building*>(c); }
    bool En() const { return api_.English(); }
    const char* L(const char* ru, const char* en) const { return En() ? en : ru; }

    // ---------------- the animations: the doors' leaves, the gate's leaves ----------------
    UINT doorAnim_[tp::kDoorN] = {}, gateAnim_[2] = {};
    MGROUP_TRANSLATE* tr_[8] = {};
    UINT grpIdx_[8][2] = {};
    void Animations() {
        for (int d = 0; d < tp::kDoorN; ++d) {
            const tp::Door& D = tp::kDoors[d];
            const double s = (D.u1 - D.u0) * D.sign;
            grpIdx_[d][0] = UINT(D.grp);
            tr_[d] = new MGROUP_TRANSLATE(meshIdx_, grpIdx_[d], 1, D.run == 0 ? _V(s, 0, 0) : _V(0, 0, s));
            doorAnim_[d] = CreateAnimation(0.0);
            AddAnimationComponent(doorAnim_[d], 0, 1, tr_[d]);
        }
        const double half = 0.5 * (tp::kGateX1 - tp::kGateX0);
        for (int k = 0; k < 2; ++k) {
            grpIdx_[4 + k][0] = UINT(tp::kGateGrp[k]); grpIdx_[4 + k][1] = UINT(tp::kGateStripeGrp[k]);
            tr_[4 + k] = new MGROUP_TRANSLATE(meshIdx_, grpIdx_[4 + k], 2, _V(k == 0 ? -half : half, 0, 0));
            gateAnim_[k] = CreateAnimation(0.0);
            AddAnimationComponent(gateAnim_[k], 0, 1, tr_[4 + k]);
        }
    }
    void Leaf(int k, double& x0, double& x1) const {       // the gate's leaf k now: x range
        const double half = 0.5 * (tp::kGateX1 - tp::kGateX0), sh = (k == 0 ? -half : half) * gate_;
        x0 = (k == 0 ? tp::kGateX0 : tp::kGateX0 + half) + sh; x1 = x0 + half;
    }

    // ---------------- the airlocks ----------------
    // lock 0 = ШК-1 (doors 0 outer, 1 inner), lock 1 = ШК-2 (doors 2 inner, 3 outer); the other door of d is d ^ 1
    double door_[tp::kDoorN] = {}, doorT_[tp::kDoorN] = {};
    double p_[2] = {kHallKPa, kHallKPa};
    int mode_[2] = {}, seq_[2] = {};                      // mode 1 depressurising, 2 pressurising; seq: the call, 1 out, 2 in
    double overrideT_[2] = {};                            // the pump's refusal (a person without a suit): pressed again - done
    static int LockOf(int d) { return d < 2 ? 0 : 1; }
    static bool Outer(int d) { return d == tp::kLock1Outer || d == tp::kLock2Outer; }
    static int OuterOf(int l) { return l == 0 ? tp::kLock1Outer : tp::kLock2Outer; }
    static int InnerOf(int l) { return l == 0 ? tp::kLock1Inner : tp::kLock2Inner; }
    double POut() const { return GetAtmPressure() * 1e-3; }
    double SideP(int d) const { return Outer(d) ? POut() : kHallKPa; }
    bool Shut(int d) const { return door_[d] <= 0.0 && doorT_[d] <= 0.0; }
    // why door d cannot open now (nullptr: it can)
    const char* CannotOpen(int d) const {
        const int l = LockOf(d);
        if (mode_[l]) return L("идёт цикл шлюза", "the airlock is cycling");
        if (!Shut(d ^ 1)) return L("вторая дверь открыта", "the other door is open");
        if (std::fabs(p_[l] - SideP(d)) > 2.0) return L("давление не выровнено", "pressure not equalised");
        return nullptr;
    }
    bool OpenDoor(int d, int panel) {
        if (const char* r = CannotOpen(d)) { Msg(panel, "%s", r); Sfx(kSndBeep); alertT_ = 2.0; return false; }
        doorT_[d] = 1.0; Sfx(kSndLift, 0.6); return true;
    }
    void CloseDoor(int d) { if (doorT_[d] > 0.0) { doorT_[d] = 0.0; Sfx(kSndLift, 0.6); } }
    int PeopleIn(int zone, bool unsuited, char* names, int n) const {
        if (names && n) names[0] = 0;
        if (!api_.PersonOfBody || !api_.ShipOf || !api_.InteriorPos) return 0;
        int k = 0;
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
            const int id = api_.PersonOfBody(oapiGetVesselByIndex(i));
            if (!id || api_.ShipOf(id) != GetHandle()) continue;
            VECTOR3 ft; double hdg;
            if (!api_.InteriorPos(id, &ft, &hdg) || !InZone(zone, ft.x, ft.z)) continue;
            if (unsuited && (!api_.SuitWorn || api_.SuitWorn(id) == 1)) continue;
            ++k;
            OcInfo in{};
            if (names && api_.Info && api_.Info(id, &in)) std::snprintf(names + std::strlen(names), n - std::strlen(names), "%s%s", names[0] ? ", " : "", in.name);
        }
        return k;
    }
    static bool InZone(int z, double x, double zz) { const double* b = tp::kZone[z]; return x >= b[0] - 0.05 && x <= b[1] + 0.05 && zz >= b[2] - 0.05 && zz <= b[3] + 0.05; }
    bool Pump(int l, int dir, int panel) {                 // dir 1 depressurise, 2 pressurise
        if (!Shut(OuterOf(l)) || !Shut(InnerOf(l))) { Msg(panel, "%s", L("сначала закройте двери", "shut both doors first")); Sfx(kSndBeep); alertT_ = 2.0; return false; }
        if (dir == 1) {
            char who[96];
            if (PeopleIn(l + 1, true, who, sizeof who) && overrideT_[l] <= 0.0) {
                overrideT_[l] = 15.0; alertT_ = 3.0; Sfx(kSndAlarm);
                Msg(panel, L("БЕЗ СКАФАНДРА: %s - 2 ещё раз: на свой риск", "NO SUIT: %s - press 2 again at your own risk"), who);
                return false;
            }
            overrideT_[l] = 0.0;
        }
        mode_[l] = dir; Sfx(kSndAirlock);
        return true;
    }
    void Locks(double dt) {
        for (int d = 0; d < tp::kDoorN; ++d) {
            const double dd = doorT_[d] - door_[d];
            if (std::fabs(dd) > 1e-9) door_[d] += (dd > 0 ? 1 : -1) * (std::min)(std::fabs(dd), dt / 1.2);   // 1.2 s
            SetAnimation(doorAnim_[d], door_[d]);
        }
        bool hum = false;
        for (int l = 0; l < 2; ++l) {
            const int o = OuterOf(l), in = InnerOf(l);
            // the call: shut the far side's door (fd), equalise to the near side (nd), open its door
            if (seq_[l]) {
                const int nd = seq_[l] == 1 ? o : in, fd = nd ^ 1;
                if (!Shut(fd)) CloseDoor(fd);
                else if (doorT_[nd] <= 0.0 && !mode_[l]) {
                    if (std::fabs(p_[l] - SideP(nd)) > 2.0) { if (!Pump(l, Outer(nd) ? 1 : 2, l)) seq_[l] = 0; }
                    else { doorT_[nd] = 1.0; Sfx(kSndLift, 0.6); }
                }
                if (door_[nd] >= 1.0) seq_[l] = 0;
            }
            if (!Shut(o)) { p_[l] = POut(); mode_[l] = 0; }
            else if (!Shut(in)) { p_[l] = kHallKPa; mode_[l] = 0; }
            else if (mode_[l] == 1) {                       // the pump НО: an exponential fall, the last kPa vented
                p_[l] -= (std::max)((p_[l] - POut()) / 0.5, 40.0) * dt;   // (~2 s: a demo, the user 2026-10-21)
                if (p_[l] <= POut() + 0.3) { p_[l] = POut(); mode_[l] = 0; Sfx(kSndBeep); }
                hum = true;
            } else if (mode_[l] == 2) {                     // the valve КН: 2 s from vacuum
                p_[l] += kHallKPa / 2.0 * dt;
                if (p_[l] >= kHallKPa) { p_[l] = kHallKPa; mode_[l] = 0; Sfx(kSndBeep); }
                hum = true;
            }
        }
        Loop(kSndHum, hum, 0.5);
        // «войти в шлюз» / «выйти наружу» from the menu: done as soon as the outer door has opened
        for (int l = 0; l < 2; ++l) {
            if (door_[OuterOf(l)] < 1.0) continue;
            if (enter_[l]) { const int p = enter_[l]; enter_[l] = 0; EnterLock(l, p); }
            if (exit_[l]) { const int p = exit_[l]; exit_[l] = 0; ExitLock(l, p); }
        }
        bool moving = false; for (int d = 0; d < tp::kDoorN; ++d) moving |= std::fabs(doorT_[d] - door_[d]) > 1e-6;
        doorsMoving_ = moving;
    }
    bool doorsMoving_ = false;
    int enter_[2] = {}, exit_[2] = {};                    // a person waiting for the outer door to go in / out
    void EnterLock(int l, int person) {
        OcInfo in{};
        if (!api_.Info(person, &in) || in.where != 1 || !in.vessel || !api_.EnterShip) return;
        const tp::Way& w = tp::kWay[l];
        const VECTOR3 at = _V(w.in[0], w.in[1], w.in[2]), dir = _V(w.dirIn[0], w.dirIn[1], w.dirIn[2]);
        api_.EnterShip(in.vessel, GetHandle(), &at, &dir);
        oapiWriteLogV("TVehicles Building %s: %s enters %s", GetName(), in.name, l == 0 ? "ШК-1" : "ШК-2");
    }
    void ExitLock(int l, int person) {
        OcInfo in{};
        if (!api_.Info(person, &in) || !api_.ExitTo || api_.ShipOf(person) != GetHandle()) return;
        const tp::Way& w = tp::kWay[l];
        VESSELSTATUS2 vs; if (!StatusAt(_V(w.out[0], 0.0, w.out[2]), _V(-w.dirIn[0], 0, -w.dirIn[2]), vs)) return;
        std::string name = in.name; for (char& ch : name) if (ch == ' ') ch = '_';
        api_.ExitTo(person, name.c_str(), &vs);
        oapiWriteLogV("TVehicles Building %s: %s goes out of %s", GetName(), in.name, l == 0 ? "ШК-1" : "ШК-2");
    }

    // ---------------- the gate ВА-1: 8 s end to end ----------------
    double gate_ = 0.0, gateT_ = 0.0;
    void Gate(double dt) {
        const double d = gateT_ - gate_;
        if (std::fabs(d) > 1e-9) gate_ += (d > 0 ? 1 : -1) * (std::min)(std::fabs(d), dt / 8.0);
        for (int k = 0; k < 2; ++k) SetAnimation(gateAnim_[k], gate_);
        Loop(kSndLift, std::fabs(gateT_ - gate_) > 1e-6 || doorsMoving_, 0.7);
    }
    void GateCmd(int k, int panel) {
        if (k == 0) { gateT_ = 1.0; Msg(panel, "%s", L("ворота открываются", "the gate opens")); }
        else if (k == 1) { gateT_ = 0.0; Msg(panel, "%s", L("ворота закрываются", "the gate closes")); }
        else { gateT_ = gate_; Msg(panel, "%s", L("привод ворот остановлен", "the gate's drive stopped")); }
        Sfx(kSndClick);
    }

    // ---------------- the lights ----------------
    struct Lamp { LightEmitter* e; int circuit; bool red; };
    std::vector<Lamp> lamps_;
    bool light_[tp::kCircuitN];
    bool emerg_ = false;
    int circShown_[tp::kCircuitN + 1] = {};
    // the Orbiter light's attenuation for a given luminous intensity (cd) at a quarter of its range: the D3D11 client takes
    // one intensity per light from it (Lights.cpp: the curve at range/4 x (range/4)^2 x LightLux 10)
    static double Att2(double cd, double range) { const double d = range / 4, a0 = 1e-3; return (std::max)(1e-6, (10.0 * d * d / cd - a0) / (d * d)); }
    void Lights() {
        static const double cd[tp::kCircuitN] = {900, 700, 600, 600, 9000, 160, 26000, 22000, 60};   // hall .. beacon
        for (int i = 0; i < tp::kLightN; ++i) {
            const tp::Light& l = tp::kLights[i];
            const VECTOR3 p = _V(l.pos[0], l.pos[1], l.pos[2]), d = _V(l.dir[0], l.dir[1], l.dir[2]);
            const COLOUR4 c = {float(l.col[0]), float(l.col[1]), float(l.col[2]), 0}, none = {0, 0, 0, 0};
            const double a2 = Att2(cd[l.circuit] * (l.circuit == tp::kCYard && l.range < 10 ? 0.002 : 1.0), l.range);
            LightEmitter* e = l.spot ? AddSpotLight(p, d, l.range, 1e-3, 0.0, a2, l.umbra * RAD, l.penumbra * RAD, c, c, none)
                                     : AddPointLight(p, l.range, 1e-3, 0.0, a2, c, c, none);
            e->SetVisibility(LightEmitter::VIS_ALWAYS);
            lamps_.push_back({e, l.circuit, false});
            if (l.circuit == tp::kCDuty) {                  // the emergency lighting: a red lamp beside each duty lamp
                const COLOUR4 r = {1.0f, 0.12f, 0.06f, 0};
                LightEmitter* er = AddPointLight(p, l.range, 1e-3, 0.0, Att2(120.0, l.range), r, r, none);
                er->SetVisibility(LightEmitter::VIS_ALWAYS);
                lamps_.push_back({er, l.circuit, true});
            }
        }
    }
    // the D3D11 client's interiors (D3D11Lights.h): the rooms as volumes and the panels as area lamps. With them the
    // rooms' and the locks' point emitters stay off (the panels light them, as area lights); without (another client,
    // an older D3D11 build) the point emitters light the rooms as before
    const D3D11Lights::Api* dl_ = nullptr;
    bool areaShown_[8] = {}; int areaKnown_ = 0;
    static int RoomCircuit(int room) { return room == 1 ? tp::kCHall : room == 2 || room == 3 ? tp::kCRooms : room == 4 ? tp::kCLock1 : room == 5 ? tp::kCLock2 : -1; }
    static bool AreaCircuit(int c) { return c == tp::kCHall || c == tp::kCRooms || c == tp::kCLock1 || c == tp::kCLock2; }
    void Interiors() {
        dl_ = D3D11Lights::Get();
        if (!dl_) { oapiWriteLogV("TVehicles Building %s: no D3D11 interiors API - the rooms keep their point lamps", GetName()); return; }
        D3D11Lights::Volume v[tp::kVolN];
        for (int i = 0; i < tp::kVolN; ++i) {
            const tp::Vol& V = tp::kVols[i];
            for (int k = 0; k < 6; ++k) v[i].box[k] = V.box[k];
            v[i].evMin = V.evMin; v[i].evMax = V.evMax; v[i].blend = V.blend; v[i].room = V.room;
        }
        D3D11Lights::AreaLight a[tp::kAreaN];
        for (int i = 0; i < tp::kAreaN; ++i) {
            const tp::Area& A = tp::kAreas[i];
            for (int k = 0; k < 3; ++k) { a[i].c[k] = A.c[k]; a[i].ex[k] = A.ex[k]; a[i].ey[k] = A.ey[k]; a[i].rgb[k] = A.rgb[k]; }
            a[i].lumen = A.lumen; a[i].twoSided = 0; a[i].circuit = A.room;
        }
        const int nv = dl_->SetInteriorVolumes(GetHandle(), v, tp::kVolN), na = dl_->SetAreaLights(GetHandle(), a, tp::kAreaN);
        oapiWriteLogV("TVehicles Building %s: D3D11 interiors: %d volumes, %d area lamps handed over", GetName(), nv, na);
        areaKnown_ = 0;
    }
    // the openings the Sun comes in through (D3D11Lights Opening: inside a volume it lights a point only along a ray through
    // one of them): the hall's two viewports always, each lock door's open part, the gate's gap - sent again when they move
    long openSig_ = -1;
    void Openings() {
        if (dl_->version < 2 || !dl_->SetOpenings) return;          // (an older client's table ends before SetOpenings)
        long sig = (long)std::lround(gate_ * 50.0);
        for (int d = 0; d < tp::kDoorN; ++d) sig = sig * 31 + (long)std::lround(door_[d] * 20.0);
        if (sig == openSig_) return;
        openSig_ = sig;
        D3D11Lights::Opening o[16]; int n = 0;
        auto add = [&](float x0, float y0, float z0, float x1, float y1, float z1, int room) {
            if (n >= 16) return;
            o[n].box[0] = x0; o[n].box[1] = y0; o[n].box[2] = z0; o[n].box[3] = x1; o[n].box[4] = y1; o[n].box[5] = z1; o[n].room = room; ++n;
        };
        for (int i = 0; i < tp::kWinN; ++i) { const tp::Box& b = tp::kWinOpen[i]; add(b.x0, b.y0, b.z0, b.x1, b.y1, b.z1, 1); }
        for (int d = 0; d < tp::kDoorN; ++d) {
            if (door_[d] < 0.02) continue;
            const tp::Door& D = tp::kDoors[d];
            const double w = (D.u1 - D.u0) * door_[d];
            const double a = D.sign > 0 ? D.u0 : D.u1 - w, b = D.sign > 0 ? D.u0 + w : D.u1;   // the part the leaf has left
            const float y0 = float(tp::kFloorY), y1 = float(tp::kFloorY + tp::kDoorH), c0 = float(D.c0 - 0.15), c1 = float(D.c1 + 0.15);
            const int room = LockOf(d) == 0 ? 4 : 5;
            if (D.run == 0) add(float(a), y0, c0, float(b), y1, c1, room); else add(c0, y0, float(a), c1, y1, float(b), room);
            if (!Outer(d)) { if (D.run == 0) add(float(a), y0, c0, float(b), y1, c1, 1); else add(c0, y0, float(a), c1, y1, float(b), 1); }   // and the hall's side
        }
        if (gate_ > 0.01) {
            const double mid = 0.5 * (tp::kGateX0 + tp::kGateX1), half = 0.5 * (tp::kGateX1 - tp::kGateX0) * gate_;
            add(float(mid - half), 0.0f, float(tp::kGateZ0 - 0.1), float(mid + half), float(tp::kGateH), float(tp::kHangar[2] + tp::kHangarWall + 0.1), 6);
        }
        dl_->SetOpenings(GetHandle(), o, n);
    }
    bool CircuitOn(int c) const {
        if (c == tp::kCBeacon) return Cycling() && std::fmod(sysT_, 1.0) < 0.5;
        if (c == tp::kCWork && emerg_) return false;
        return light_[c];
    }
    bool Cycling() const { return mode_[0] || mode_[1] || seq_[0] || seq_[1] || doorsMoving_ || std::fabs(gateT_ - gate_) > 1e-6; }
    void ApplyLights() {
        for (Lamp& l : lamps_) {
            const bool on = l.red ? emerg_ : (l.circuit == tp::kCDuty ? light_[tp::kCDuty] && !emerg_ : CircuitOn(l.circuit) && !(dl_ && AreaCircuit(l.circuit)));
            if (l.e->IsActive() != on) l.e->Activate(on);
        }
        if (dl_)                                                  // the area lamps' circuits (rooms 1..5)
            for (int room = 1; room <= 5; ++room) {
                const bool on = CircuitOn(RoomCircuit(room));
                if (areaKnown_ & (1 << room) && areaShown_[room] == on) continue;
                dl_->AreaLightOn(GetHandle(), room, on ? 1 : 0); areaShown_[room] = on; areaKnown_ |= 1 << room;
            }
        if (dl_) Openings();
        if (!dev_) return;
        for (int c = 0; c < tp::kCircuitN; ++c) {
            int st = CircuitOn(c) ? 1 : 0;
            if (c == tp::kCDuty) st = emerg_ ? 2 : light_[c] ? 1 : 0;
            if (st == circShown_[c]) continue;
            circShown_[c] = st;
            MATERIAL m{};
            const COLOUR4 on = c == tp::kCBeacon ? COLOUR4{1.0f, 0.45f, 0.05f, 1} : c == tp::kCDuty ? COLOUR4{1.0f, 0.78f, 0.5f, 1} : COLOUR4{1.0f, 0.96f, 0.88f, 1};
            const COLOUR4 red = {1.0f, 0.12f, 0.06f, 1}, off = {0.18f, 0.18f, 0.17f, 1};
            const COLOUR4 e = st == 2 ? red : st ? on : COLOUR4{0, 0, 0, 1};
            m.diffuse = st ? e : off; m.ambient = m.diffuse; m.specular = {0.1f, 0.1f, 0.1f, 1}; m.emissive = e; m.power = 10;
            oapiSetMaterial(dev_, (DWORD)tp::kCircuitMat[c], &m);
        }
    }

    // ---------------- the panels: the buttons, their caps, the screens ----------------
    // panels (TechPointGeo.h order): 0 МЩУ ШК-1, 1 МЩУ ШК-2, 2 call ШК-1 (yard), 3 call ШК-1 (hall), 4 call ШК-2 (hall),
    // 5 call ШК-2 (hangar), 6 ЩУ ВА-1 (hangar), 7 ЩО-1, 8 ЩУ ВА-1 (yard), 9 БЩУ ТП
    enum { kPLock1 = 0, kPLock2, kPCall1Out, kPCall1In, kPCall2In, kPCall2Out, kPGateIn, kPLights, kPGateOut, kPDuty };
    static bool Inside(int panel) { return panel == kPLock1 || panel == kPLock2 || panel == kPCall1In || panel == kPCall2In || panel == kPDuty; }
    char msg_[tp::kPanelN][96] = {};
    double msgT_[tp::kPanelN] = {};
    double alertT_ = 0.0; int alertBtn_ = -1;
    char suitMsg_[2][64] = {}; int suitOk_[2] = {-1, -1};
    template <class... A> void Msg(int panel, const char* fmt, A... a) {
        if (panel < 0 || panel >= tp::kPanelN) return;
        std::snprintf(msg_[panel], sizeof msg_[panel], fmt, a...); msgT_[panel] = 8.0;
    }
    void Press(int panel, int k, int person) {
        Sfx(kSndClick);
        alertBtn_ = tp::kPanels[panel].btn0 + k;
        switch (panel) {
            case kPLock1: case kPLock2: {
                const int l = panel;
                if (k == 0) {                                  // the suit check (as the Tantra's lift zone)
                    const int w = api_.SuitWorn ? api_.SuitWorn(person) : -1;
                    suitOk_[l] = w == 1 ? 1 : 0;
                    std::snprintf(suitMsg_[l], sizeof suitMsg_[l], "%s", w == 1 ? L("надет, герметичен", "on, sealed") : w == 0 ? L("НЕ НАДЕТ", "NOT ON") : L("нет данных", "no data"));
                    Msg(panel, L("скафандр: %s", "suit: %s"), suitMsg_[l]);
                } else if (k == 1) { seq_[l] = 0; if (Pump(l, 1, panel)) Msg(panel, "%s", L("откачка: насос НО включён", "depressurising: the pump is on")); }
                else if (k == 2) { seq_[l] = 0; if (Pump(l, 2, panel)) Msg(panel, "%s", L("наддув: клапан КН открыт", "pressurising: the valve is open")); }
                else if (k == 3 || k == 4) {
                    const int d = k == 3 ? OuterOf(l) : InnerOf(l);
                    seq_[l] = 0;
                    if (doorT_[d] > 0.0) { CloseDoor(d); Msg(panel, "%s", L("дверь закрывается", "the door closes")); }
                    else if (OpenDoor(d, panel)) Msg(panel, "%s", L("дверь открывается", "the door opens"));
                } else if (k == 5) {                           // the emergency stop: everything stays where it is
                    mode_[l] = 0; seq_[l] = 0;
                    for (int d : {OuterOf(l), InnerOf(l)}) doorT_[d] = door_[d];
                    Msg(panel, "%s", L("АВАРИЙНЫЙ СТОП", "EMERGENCY STOP")); Sfx(kSndAlarm);
                }
                break;
            }
            case kPCall1Out: Call(0, 1, panel); break;
            case kPCall1In: Call(0, 2, panel); break;
            case kPCall2In: Call(1, 2, panel); break;
            case kPCall2Out: Call(1, 1, panel); break;
            case kPGateIn: case kPGateOut: GateCmd(k, panel); break;
            case kPLights: {
                const int c[5] = {tp::kCWork, tp::kCDuty, -1, tp::kCGate, tp::kCYard};
                if (k == 2) emerg_ = !emerg_;
                else if (k >= 0 && k < 5) light_[c[k]] = !light_[c[k]];
                Msg(panel, "%s", k == 2 ? (emerg_ ? L("аварийное освещение включено", "emergency lighting on") : L("аварийное освещение отключено", "emergency lighting off"))
                                        : light_[c[k]] ? L("включено", "on") : L("отключено", "off"));
                break;
            }
            case kPDuty:
                if (k == 0) light_[tp::kCHall] = !light_[tp::kCHall];
                else if (k == 1) light_[tp::kCRooms] = !light_[tp::kCRooms];
                else if (k == 2) light_[tp::kCYard] = !light_[tp::kCYard];
                else if (k == 3) Call(0, 2, panel);
                else if (k == 4) Call(1, 2, panel);
                else { for (double& t : msgT_) t = 0.0; alertT_ = 0.0; overrideT_[0] = overrideT_[1] = 0.0; Msg(panel, "%s", L("сигнализация сброшена", "the alarm reset")); }
                break;
            default: break;
        }
    }
    void Call(int l, int side, int panel) {
        seq_[l] = side; overrideT_[l] = 0.0;
        Msg(panel, "%s", side == 1 ? L("шлюз: выравнивание к наружной стороне", "airlock: equalising to the outside") : L("шлюз: выравнивание к холлу", "airlock: equalising to the hall"));
    }
    // a cap's light: 0 dark, 1 lit (done / ready), 2 blinking (running), 3 fast (refused: this one)
    int CapState(int b) const {
        const tp::Button& B = tp::kButtons[b];
        if (b == alertBtn_ && alertT_ > 0.0) return 3;
        const int k = B.k;
        switch (B.panel) {
            case kPLock1: case kPLock2: {
                const int l = B.panel;
                if (k == 0) return suitOk_[l] == 1 ? 1 : 0;
                if (k == 1) return mode_[l] == 1 ? 2 : overrideT_[l] > 0.0 ? 3 : (Shut(OuterOf(l)) && Shut(InnerOf(l)) && p_[l] > POut() + 2.0) ? 1 : 0;
                if (k == 2) return mode_[l] == 2 ? 2 : (Shut(OuterOf(l)) && Shut(InnerOf(l)) && p_[l] < kHallKPa - 2.0) ? 1 : 0;
                const int d = k == 3 ? OuterOf(l) : k == 4 ? InnerOf(l) : -1;
                if (d >= 0) return std::fabs(doorT_[d] - door_[d]) > 1e-6 ? 2 : door_[d] >= 1.0 ? 1 : CannotOpen(d) ? 0 : 1;
                return 0;
            }
            case kPCall1Out: case kPCall1In: return seq_[0] ? 2 : door_[B.panel == kPCall1Out ? tp::kLock1Outer : tp::kLock1Inner] >= 1.0 ? 1 : 0;
            case kPCall2In: case kPCall2Out: return seq_[1] ? 2 : door_[B.panel == kPCall2In ? tp::kLock2Inner : tp::kLock2Outer] >= 1.0 ? 1 : 0;
            case kPGateIn: case kPGateOut: {
                const bool mv = std::fabs(gateT_ - gate_) > 1e-6;
                if (k == 0) return mv && gateT_ > gate_ ? 2 : gate_ >= 1.0 ? 1 : 0;
                if (k == 1) return mv && gateT_ < gate_ ? 2 : gate_ <= 0.0 ? 1 : 0;
                return mv ? 1 : 0;
            }
            case kPLights: { const int c[5] = {tp::kCWork, tp::kCDuty, -1, tp::kCGate, tp::kCYard}; return k == 2 ? (emerg_ ? 2 : 0) : light_[c[k]] ? 1 : 0; }
            case kPDuty: {
                if (k == 0) return light_[tp::kCHall]; if (k == 1) return light_[tp::kCRooms]; if (k == 2) return light_[tp::kCYard];
                if (k == 3) return seq_[0] ? 2 : 0; if (k == 4) return seq_[1] ? 2 : 0;
                return alertT_ > 0.0 || overrideT_[0] > 0.0 || overrideT_[1] > 0.0 ? 3 : 0;
            }
            default: return 0;
        }
    }
    int capShown_[tp::kButtonN] = {};
    void Caps() {
        if (!dev_) return;
        for (int b = 0; b < tp::kButtonN; ++b) {
            const int st = CapState(b);
            const bool lit = st == 1 || (st == 2 && std::fmod(sysT_, 1.0) < 0.5) || (st == 3 && std::fmod(sysT_, 0.25) < 0.125);
            const bool stop = std::strstr(tp::kButtons[b].ru, "стоп") != nullptr;
            const int key = lit ? (st == 3 ? 3 : st == 2 ? 2 : 1) : 0;
            if (key == capShown_[b]) continue;
            capShown_[b] = key;
            MATERIAL m{};
            const COLOUR4 dark = stop ? COLOUR4{0.45f, 0.10f, 0.08f, 1} : COLOUR4{0.30f, 0.32f, 0.30f, 1};
            const COLOUR4 col = key == 3 ? COLOUR4{1.0f, 0.15f, 0.08f, 1} : key == 2 ? COLOUR4{1.0f, 0.62f, 0.12f, 1} : stop ? COLOUR4{1.0f, 0.2f, 0.12f, 1} : COLOUR4{0.35f, 1.0f, 0.55f, 1};
            m.diffuse = key ? col : dark; m.ambient = m.diffuse; m.specular = {0.3f, 0.3f, 0.3f, 1}; m.power = 20;
            m.emissive = key ? col : COLOUR4{0, 0, 0, 1};
            oapiSetMaterial(dev_, (DWORD)tp::kButtons[b].mat, &m);
        }
    }
    // the screens (as the Tantra's lift panel): near-black, a grey header with the board's code, a hairline, two columns
    SURFHANDLE scr_[tp::kPanelN] = {};
    double scrT_ = 0.0;
    TantraGlyphs glyphs_;
    oapi::Pen* rule_ = nullptr;
    void Screens() {
        scrT_ += oapiGetSysStep();
        if (!dev_ || scrT_ < 0.25 || !glyphs_.Ok()) return;
        scrT_ = 0.0;
        VECTOR3 cam, me; oapiCameraGlobalPos(&cam); GetGlobalPos(me);
        if (length(cam - me) > 90.0) return;               // nobody looks: not redrawn
        char buf[1024];
        for (int i = 0; i < tp::kPanelN; ++i) {
            if (!scr_[i]) continue;
            Status(i, buf, sizeof buf);
            DrawTable(scr_[i], buf, tp::kPanels[i].w, tp::kPanels[i].h);
        }
    }
    const char* DoorState(int d) const {
        if (std::fabs(doorT_[d] - door_[d]) > 1e-6) return doorT_[d] > door_[d] ? L("*ОТКРЫВАЕТСЯ", "*OPENING") : L("*ЗАКРЫВАЕТСЯ", "*CLOSING");
        return door_[d] >= 1.0 ? L("+ОТКРЫТА", "+OPEN") : door_[d] <= 0.0 ? L(" ЗАКРЫТА", " SHUT") : L("!ОСТАНОВЛЕНА", "!STOPPED");
    }
    void LockStatus(int l, int panel, char* out, int n) const {
        const char* pump = mode_[l] == 1 ? L("*ОТКАЧКА", "*PUMPING DOWN") : mode_[l] == 2 ? L("*НАДДУВ", "*PRESSURISING") : seq_[l] ? L("*ЦИКЛ ПО ВЫЗОВУ", "*CYCLE ON CALL") : L(" ОСТАНОВЛЕНЫ", " IDLE");
        char suit[80];
        if (suitMsg_[l][0]) std::snprintf(suit, sizeof suit, "%c%s", suitOk_[l] == 1 ? '+' : '!', suitMsg_[l]); else std::snprintf(suit, sizeof suit, " %s", L("НЕ ПРОВЕРЕН", "NOT CHECKED"));
        char msg[120];
        if (msgT_[panel] > 0.0) std::snprintf(msg, sizeof msg, "%c%s", overrideT_[l] > 0.0 || alertT_ > 0.0 ? '!' : ' ', msg_[panel]);
        else std::snprintf(msg, sizeof msg, " %s", L("1 СКАФАНДР  2 ОТКАЧКА  3 НАДДУВ  4/5 ДВЕРИ", "1 SUIT  2 DOWN  3 UP  4/5 DOORS"));
        std::snprintf(out, size_t(n), "%s\t%s\n%s\t%c%.1f %s\n%s\t %.1f %s\n%s\t %.1f %s\n%s\t%s\n%s\t%s\n%s\t%s\n%s\t%s\n%s",
                      l == 0 ? L("ШК-1  ·  ШЛЮЗОВАЯ КАМЕРА", "ШК-1  ·  AIRLOCK") : L("ШК-2  ·  ШЛЮЗОВАЯ КАМЕРА", "ШК-2  ·  AIRLOCK"), l == 0 ? "МЩУ-1" : "МЩУ-2",
                      L("ДАВЛЕНИЕ КАМЕРЫ", "CHAMBER"), mode_[l] ? '*' : ' ', p_[l], L("КПА", "KPA"),
                      L("ХОЛЛ (ПОМ. 101)", "HALL"), kHallKPa, L("КПА", "KPA"),
                      l == 0 ? L("СНАРУЖИ (ДВОР)", "OUTSIDE (YARD)") : L("АНГАР (ПОМ. 201)", "HANGAR"), POut(), L("КПА", "KPA"),
                      L("ДВЕРЬ НАРУЖНАЯ", "OUTER DOOR"), DoorState(OuterOf(l)), L("ДВЕРЬ ВНУТРЕННЯЯ", "INNER DOOR"), DoorState(InnerOf(l)),
                      L("НАСОС НО / КЛАПАН КН", "PUMP / VALVE"), pump, L("СКАФАНДР", "SUIT"), suit, msg);
    }
    void Status(int i, char* out, int n) const {
        auto msgOr = [&](const char* dflt) { static char b[120]; if (msgT_[i] > 0.0) std::snprintf(b, sizeof b, " %s", msg_[i]); else std::snprintf(b, sizeof b, " %s", dflt); return b; };
        switch (i) {
            case kPLock1: case kPLock2: LockStatus(i, i, out, n); return;
            case kPCall1Out: case kPCall1In: case kPCall2In: case kPCall2Out: {
                const int l = i <= kPCall1In ? 0 : 1, d = (i == kPCall1Out) ? tp::kLock1Outer : (i == kPCall1In) ? tp::kLock1Inner : (i == kPCall2In) ? tp::kLock2Inner : tp::kLock2Outer;
                std::snprintf(out, size_t(n), "%s\t%s\n%s\t%c%.0f\n%s\t%s", l == 0 ? "ШК-1" : "ШК-2", L("ВЫЗОВ", "CALL"), L("КПА", "KPA"), mode_[l] ? '*' : ' ', p_[l], L("ДВЕРЬ", "DOOR"), DoorState(d));
                return;
            }
            case kPGateIn: case kPGateOut: {
                const bool mv = std::fabs(gateT_ - gate_) > 1e-6;
                std::snprintf(out, size_t(n), "%s\t%s\n%s\t%c%.0f %%\n%s\t%s\n%s\t+%s\n%s\t%s\n%s",
                              L("ВОРОТА АНГАРА ВА-1", "HANGAR GATE ВА-1"), "ЩУ ВА-1", L("ПОЛОЖЕНИЕ", "POSITION"), mv ? '*' : gate_ >= 1.0 ? '+' : ' ', gate_ * 100.0,
                              L("ПРИВОД", "DRIVE"), mv ? (gateT_ > gate_ ? L("*ОТКРЫВАНИЕ", "*OPENING") : L("*ЗАКРЫВАНИЕ", "*CLOSING")) : L(" ОСТАНОВЛЕН", " STOPPED"),
                              L("ДАВЛЕНИЕ", "PRESSURE"), L("ВАКУУМ С ОБЕИХ СТОРОН", "VACUUM BOTH SIDES"),
                              L("ПРОЖЕКТОР ВОРОТ", "GATE FLOOD"), light_[tp::kCGate] ? L("+ВКЛ", "+ON") : L(" ОТКЛ", " OFF"),
                              msgOr(L("1 ОТКРЫТЬ  2 ЗАКРЫТЬ  3 СТОП", "1 OPEN  2 CLOSE  3 STOP")));
                return;
            }
            case kPLights: {
                auto s = [&](bool on) { return on ? L("+ВКЛ", "+ON") : L(" ОТКЛ", " OFF"); };
                std::snprintf(out, size_t(n), "%s\t%s\n%s\t%s\n%s\t%s\n%s\t%s\n%s\t%s\n%s\t%s\n%s",
                              L("ЩИТ ОСВЕЩЕНИЯ АНГАРА", "HANGAR LIGHTING BOARD"), "ЩО-1",
                              L("1 РАБОЧЕЕ", "1 WORK"), emerg_ && light_[tp::kCWork] ? L("!ОТКЛ АВАРИЕЙ", "!OFF (EMERG.)") : s(light_[tp::kCWork]),
                              L("2 ДЕЖУРНОЕ", "2 DUTY"), s(light_[tp::kCDuty]), L("3 АВАРИЙНОЕ", "3 EMERGENCY"), emerg_ ? L("!ВКЛ", "!ON") : L(" ОТКЛ", " OFF"),
                              L("4 ПРОЖЕКТОР ВОРОТ", "4 GATE FLOOD"), s(light_[tp::kCGate]), L("5 ПРОЖЕКТОР ДВОРА", "5 YARD FLOOD"), s(light_[tp::kCYard]),
                              msgOr(L("ПОМ. 201  ·  ВАКУУМ", "ROOM 201  ·  VACUUM")));
                return;
            }
            case kPDuty: {
                // the duty post: the whole point at a glance, and the Sun here (the stand's lighting: its elevation)
                double se = SunElevDeg();
                std::snprintf(out, size_t(n), "%s\t%s\n%s\t%c%.1f %s  %s\n%s\t%c%.1f %s  %s\n%s\t%c%.0f %%\n%s\t%s %s %s\n%s\t%c%+.1f°\n%s\t %.0f %s\n%s",
                              L("ТЕХНИЧЕСКИЙ ПУНКТ ТП-1  ·  ПОСТ ДЕЖУРНОГО", "TECHNICAL POINT ТП-1  ·  DUTY POST"), "БЩУ ТП",
                              "ШК-1", mode_[0] ? '*' : ' ', p_[0], L("КПА", "KPA"), Cycle(0),
                              "ШК-2", mode_[1] ? '*' : ' ', p_[1], L("КПА", "KPA"), Cycle(1),
                              L("ВОРОТА ВА-1", "GATE ВА-1"), std::fabs(gateT_ - gate_) > 1e-6 ? '*' : ' ', gate_ * 100.0,
                              L("ОСВЕЩЕНИЕ", "LIGHTING"), light_[tp::kCHall] ? L("ХОЛЛ", "HALL") : "-", light_[tp::kCWork] && !emerg_ ? L("АНГАР", "HANGAR") : emerg_ ? L("АВАРИЙНОЕ", "EMERG.") : "-", light_[tp::kCYard] ? L("ДВОР", "YARD") : "-",
                              L("СОЛНЦЕ НАД ГОРИЗОНТОМ", "SUN ELEVATION"), se < 0 ? '!' : se < 10 ? '*' : '+', se,
                              L("ВОЗДУХ ХОЛЛА", "HALL AIR"), kHallKPa, L("КПА  O2 21 %", "KPA  O2 21 %"),
                              msgOr(L("1 ХОЛЛ  2 ПОМЕЩЕНИЯ  3 ДВОР  4/5 ВЫЗОВ ШК  6 СБРОС", "1 HALL  2 ROOMS  3 YARD  4/5 CALL  6 RESET")));
                return;
            }
            default: out[0] = 0;
        }
    }
    const char* Cycle(int l) const { return mode_[l] == 1 ? L("*ОТКАЧКА", "*DOWN") : mode_[l] == 2 ? L("*НАДДУВ", "*UP") : !Shut(OuterOf(l)) ? L("+ОТКРЫТ НАРУЖУ", "+OPEN OUT") : !Shut(InnerOf(l)) ? L("+ОТКРЫТ В ХОЛЛ", "+OPEN IN") : L(" ЗАКРЫТ", " SHUT"); }
    double SunElevDeg() const {
        OBJHANDLE sun = oapiGetGbodyByIndex(0); if (!sun) return 0;
        VECTOR3 s, me; oapiGetGlobalPos(sun, &s); GetGlobalPos(me);
        VECTOR3 up; GlobalRot(_V(0, 1, 0), up);
        return std::asin(Clamp(dotp(unit(s - me), up), -1, 1)) * DEG;
    }
    void DrawTable(SURFHANDLE surf, char* buf, int W, int H) {
        oapiClearSurface(surf, 0xFF0B0D0F);
        oapi::Sketchpad* skp = oapiGetSketchpad(surf);
        if (!skp) return;
        if (!rule_) rule_ = oapiCreatePen(1, 1, 0x50585E);
        const double px = H >= 400 ? 26.0 : H >= 200 ? 17.0 : 15.0, row = px * 1.85, xv = W * 0.47;
        double y = 10.0; int line = 0; bool msgs = false;
        auto colOf = [](const char*& t) { int c = 2; if (*t == '!') { c = 3; t++; } else if (*t == '+') { c = 0; t++; } else if (*t == '*') { c = 1; t++; } else if (*t == ' ') t++; return c; };
        for (char* p = buf; p && *p; line++) {
            char* nl = std::strchr(p, '\n'); if (nl) *nl = 0;
            char* tab = std::strchr(p, '\t'); if (tab) *tab = 0;
            if (line == 0) {
                glyphs_.Draw(skp, 14, y, p, 2, 6, px * 0.9);
                if (tab) glyphs_.DrawA(skp, W - 14, y, tab + 1, 2, 6, px * 0.9, 2);
                y += px * 1.6;
                skp->SetPen(rule_); skp->Line(10, int(y), W - 10, int(y));
                y += px * 0.9;
            } else if (tab) {
                glyphs_.Draw(skp, 16, y, p, 2, 6, px);
                const char* v = tab + 1; const int cv = colOf(v);
                glyphs_.Draw(skp, xv, y, v, 2, cv, px);
                y += row;
            } else if (*p) {
                if (!msgs) { msgs = true; skp->SetPen(rule_); skp->Line(10, int(y - px * 0.45), W - 10, int(y - px * 0.45)); }
                const char* t = p; const int cv = colOf(t);
                glyphs_.Draw(skp, 16, y, t, 2, cv, px * 0.9);
                y += row;
            }
            p = nl ? nl + 1 : nullptr;
        }
        oapiReleaseSketchpad(skp);
    }

    // ---------------- the sounds (XRSound, the MPU's own wavs) ----------------
    enum { kSndClick = 1, kSndAirlock, kSndHum, kSndLift, kSndAlarm, kSndBeep };
    XRSound* snd_ = nullptr;
    float Near() const {                                  // only for whoever is near (the camera within 40 m)
        VECTOR3 cam, me; oapiCameraGlobalPos(&cam); GetGlobalPos(me);
        return float(Clamp(1.0 - length(cam - me) / 60.0, 0.0, 1.0));
    }
    void Sfx(int id, double vol = 1.0) { if (snd_ && snd_->IsPresent() && Near() > 0.05f) snd_->PlayWav(id, false, float(vol) * Near()); }
    void Loop(int id, bool on, double vol) {
        if (!snd_ || !snd_->IsPresent()) return;
        const bool playing = snd_->IsWavPlaying(id);
        if (on && Near() > 0.05f) snd_->PlayWav(id, true, float(vol) * Near()); else if (playing) snd_->StopWav(id);
    }

    // ---------------- OrbiterCrew: the interior ----------------
    double g_ = 1.62, sysT_ = 0.0, costT_ = 0.0, stepUs_ = 0.0; long frames_ = 0;
    OcApi api_; OcInterior fns_{}; OcInteriorExt ext_{}; ATTACHMENTHANDLE att_ = nullptr; bool registered_ = false;
    MESHHANDLE mesh_ = nullptr; UINT meshIdx_ = 0; DEVMESHHANDLE dev_ = nullptr;
    static int cGround(void*, const VECTOR3* p, double stepUp, double* floorY) {
        CostScope cs_(CbCost());
        double best = -1e9; int ok = 0;
        for (int i = 0; i < tp::kFloorsCount; ++i) {
            const tp::Box& b = tp::kFloors[i];
            if (p->x >= b.x0 && p->x <= b.x1 && p->z >= b.z0 && p->z <= b.z1 && b.y1 <= p->y + stepUp + 1e-3 && b.y1 > best) { best = b.y1; ok = 1; }
        }
        if (ok && best < p->y - 0.5) return 0;
        if (ok && floorY) *floorY = best;
        return ok;
    }
    // one box against a person of this radius at (x, z), her body feet..feet+height (as TantraInterior::Resolve)
    static bool Push(double& x, double& z, double x0, double y0, double z0, double x1, double y1, double z1, double feet, double radius, double height) {
        if (y1 <= feet + kKnee || y0 >= feet + height) return false;
        const double cx = Clamp(x, x0, x1), cz = Clamp(z, z0, z1), dx = x - cx, dz = z - cz, d2 = dx * dx + dz * dz;
        if (d2 >= radius * radius) return false;
        if (d2 > 1e-10) { const double d = std::sqrt(d2), k = (radius - d) / d; x += dx * k; z += dz * k; }
        else {
            const double l = x - x0, r = x1 - x, f = z - z0, a = z1 - z, m = (std::min)((std::min)(l, r), (std::min)(f, a));
            if (m == l) x = x0 - radius; else if (m == r) x = x1 + radius; else if (m == f) z = z0 - radius; else z = z1 + radius;
        }
        return true;
    }
    // a door's leaf as a box, when it is not open: inside the outer doors always (one goes out with F), the inner ones shut
    void DoorBox(int d, double& x0, double& z0, double& x1, double& z1) const {
        const tp::Door& D = tp::kDoors[d];
        if (D.run == 0) { x0 = D.u0; x1 = D.u1; z0 = D.c0; z1 = D.c1; } else { x0 = D.c0; x1 = D.c1; z0 = D.u0; z1 = D.u1; }
    }
    void Resolve(double& x, double& z, double feet, double radius, double height, bool outside) const {
        for (int it = 0; it < 4; ++it) {
            bool moved = false;
            const tp::Box* arr = outside ? tp::kSolidsOut : tp::kSolidsIn; const int n = outside ? tp::kSolidsOutCount : tp::kSolidsInCount;
            for (int i = 0; i < n; ++i) { const tp::Box& b = arr[i]; moved |= Push(x, z, b.x0, b.y0, b.z0, b.x1, b.y1, b.z1, feet, radius, height); }
            if (!outside)
                for (int d = 0; d < tp::kDoorN; ++d)
                    if (Outer(d) || door_[d] < 0.9) { double x0, z0, x1, z1; DoorBox(d, x0, z0, x1, z1); moved |= Push(x, z, x0, tp::kFloorY, z0, x1, tp::kFloorY + tp::kDoorH, z1, feet, radius, height); }
            if (outside)
                for (int k = 0; k < 2; ++k) { double x0, x1; Leaf(k, x0, x1); moved |= Push(x, z, x0, 0.0, tp::kGateZ0, x1, tp::kGateH, tp::kGateZ1, feet, radius, height); }
            if (!moved) break;
        }
    }
    static void cWalls(void* c, const VECTOR3* from, VECTOR3* to, double radius, double height) {
        CostScope cs_(CbCost());
        const Building* b = Self(c);
        double x = from->x, z = from->z, nx = to->x, nz = z;       // axis by axis: the body slides along the walls
        b->Resolve(nx, nz, from->y, radius, height, false); x = nx;
        nx = x; nz = to->z; b->Resolve(nx, nz, from->y, radius, height, false);
        to->x = nx; to->z = nz;
    }
    static void cOuterWalls(void* c, const VECTOR3* from, VECTOR3* to, double radius, double height) {
        CostScope cs_(CbCost());
        const Building* b = Self(c);
        double x = from->x, z = from->z, nx = to->x, nz = z;
        b->Resolve(nx, nz, from->y, radius, height, true); x = nx;
        nx = x; nz = to->z; b->Resolve(nx, nz, from->y, radius, height, true);
        to->x = nx; to->z = nz;
    }
    static int cCabin(void* c, const VECTOR3* pos, OcCabin* out) {
        CostScope cs_(CbCost());
        const Building* b = Self(c);
        double p = kHallKPa; int supplied = 1;
        if (pos && InZone(1, pos->x, pos->z)) { p = b->p_[0]; supplied = 0; }
        else if (pos && InZone(2, pos->x, pos->z)) { p = b->p_[1]; supplied = 0; }
        out->p = p; out->T = 294.0; out->ppO2 = 0.212 * p; out->ppCO2 = 0.04 * p / kHallKPa;
        out->doseSvh = 1e-7; out->supplied = supplied;               // the hall: water and food at hand (the galley)
        return 1;
    }
    // items: the ways through the outer doors (outside: OC_AIRLOCK, inside: OC_EXIT, while the door is open), the seats,
    // the inside panels' buttons (OC_BUTTON, the mouse)
    enum { kItemWayIn0 = 1, kItemWayOut0 = 3, kItemSeat0 = 10, kItemBtn0 = 100 };
    static int cCount(void*) { return 4 + tp::kSeatN + tp::kButtonN; }
    static int cItem(void* c, int i, OcItem* out) {
        CostScope cs_(CbCost());
        const Building* b = Self(c);
        *out = OcItem{};
        if (i < 4) {
            const int l = i & 1, d = OuterOf(l); const tp::Way& w = tp::kWay[l];
            // the way through goes by the doors' context menus («войти в шлюз», «выйти во двор»): OrbiterCrew's own hint of
            // these items stood on the ground, small and unreadable (the user, 2026-10-21) - kept out of reach
            if (b->door_[d] < 0.9 || true) { out->id = 0; out->kind = OC_DOOR; out->pos = _V(0, -50, 0); out->radius = 0.01; return 1; }
            if (i < 2) {
                out->id = kItemWayIn0 + l; out->kind = OC_AIRLOCK; out->pos = _V(w.out[0], w.out[1], w.out[2]); out->dir = _V(w.dirIn[0], w.dirIn[1], w.dirIn[2]); out->radius = 1.6;
                std::snprintf(out->label, sizeof out->label, "%s", l == 0 ? b->L("войти в ШК-1", "enter airlock ШК-1") : b->L("войти в ШК-2", "enter airlock ШК-2"));
            } else {
                out->id = kItemWayOut0 + l; out->kind = OC_EXIT; out->pos = _V(w.in[0], w.in[1], w.in[2]); out->dir = _V(-w.dirIn[0], 0, -w.dirIn[2]); out->radius = 1.2;
                std::snprintf(out->label, sizeof out->label, "%s", l == 0 ? b->L("выйти во двор", "go out to the yard") : b->L("выйти в ангар", "go out into the hangar"));
            }
            return 1;
        }
        i -= 4;
        if (i < tp::kSeatN) {
            const tp::Seat& s = tp::kSeats[i];
            out->id = kItemSeat0 + i; out->kind = OC_SEAT; out->pos = _V(s.x, tp::kFloorY, s.z); out->dir = _V(s.fx, 0, s.fz); out->radius = 0.9;
            std::snprintf(out->label, sizeof out->label, "%s", b->L(s.ru, s.en));
            return 1;
        }
        i -= tp::kSeatN;
        if (i < tp::kButtonN) {
            const tp::Button& B = tp::kButtons[i];
            if (!Inside(B.panel)) { out->id = 0; out->kind = OC_DOOR; out->pos = _V(0, -50, 0); out->radius = 0.01; return 1; }   // the outside boards: context nodes
            out->id = kItemBtn0 + i; out->kind = OC_BUTTON; out->pos = _V(B.pos[0], B.pos[1], B.pos[2]); out->dir = _V(-B.n[0], -B.n[1], -B.n[2]); out->radius = 0.035;
            std::snprintf(out->label, sizeof out->label, "%s", b->L(B.ru, B.en));
            return 1;
        }
        return 0;
    }
    static void cUse(void* c, int id, int person) {
        Building* b = Self(c);
        if (id >= kItemBtn0 && id < kItemBtn0 + tp::kButtonN) { const tp::Button& B = tp::kButtons[id - kItemBtn0]; b->Press(B.panel, B.k, person); return; }
        OcInfo in{};
        if (!b->api_.Info(person, &in)) return;
        if (id == kItemWayIn0 || id == kItemWayIn0 + 1) {
            const int l = id - kItemWayIn0; const tp::Way& w = tp::kWay[l];
            if (b->door_[OuterOf(l)] < 0.9 || in.where != 1 || !in.vessel || !b->api_.EnterShip) return;
            const VECTOR3 at = _V(w.in[0], w.in[1], w.in[2]), dir = _V(w.dirIn[0], w.dirIn[1], w.dirIn[2]);
            b->api_.EnterShip(in.vessel, b->GetHandle(), &at, &dir);
            oapiWriteLogV("TVehicles Building %s: %s enters %s", b->GetName(), in.name, l == 0 ? "ШК-1" : "ШК-2");
        } else if (id == kItemWayOut0 || id == kItemWayOut0 + 1) {
            const int l = id - kItemWayOut0; const tp::Way& w = tp::kWay[l];
            if (b->door_[OuterOf(l)] < 0.9 || !b->api_.ExitTo) return;
            VESSELSTATUS2 vs; if (!b->StatusAt(_V(w.out[0], 0.0, w.out[2]), _V(-w.dirIn[0], 0, -w.dirIn[2]), vs)) return;
            std::string name = in.name; for (char& ch : name) if (ch == ' ') ch = '_';
            b->api_.ExitTo(person, name.c_str(), &vs);
            oapiWriteLogV("TVehicles Building %s: %s goes out of %s", b->GetName(), in.name, l == 0 ? "ШК-1" : "ШК-2");
        }
    }
    static void cSeat(void*, int id, VECTOR3* pos, VECTOR3* dir) {
        const int k = id - kItemSeat0;
        if (k < 0 || k >= tp::kSeatN) return;
        const tp::Seat& s = tp::kSeats[k];
        if (pos) *pos = _V(s.x, s.hips, s.z);
        if (dir) *dir = _V(s.fx, 0, s.fz);
    }
    bool StatusAt(const VECTOR3& local, const VECTOR3& face, VESSELSTATUS2& vs) const {
        OBJHANDLE ref = GetSurfaceRef();
        if (!ref) return false;
        VECTOR3 gp; Local2Global(local, gp);
        double lng, lat, rad, hdg;
        oapiGlobalToEqu(ref, gp, &lng, &lat, &rad);
        oapiGetHeading(GetHandle(), &hdg);
        std::memset(&vs, 0, sizeof vs);
        vs.version = 2; vs.rbody = ref; vs.status = 1; vs.arot.x = 10.0;     // landed: Orbiter stands the body on its own points
        vs.surf_lng = lng; vs.surf_lat = lat;
        vs.surf_hdg = std::fmod(hdg + std::atan2(face.x, face.z) + 2.0 * PI2, PI2);
        return true;
    }

    // ---------------- the context nodes (F): the doors, the gate, the outside boards ----------------
    enum { kNDoor1OutX = 0, kNDoor1OutI, kNDoor1In, kNDoor2In, kNDoor2OutI, kNDoor2OutX, kNGateYard, kNGateHangar,
           kNBoard0, kNodeN = kNBoard0 + 8 };                 // the boards: call ШК-1 (yard), call ШК-2 (hangar), ЩУ ВА-1 (hangar), ЩО-1, ЩУ ВА-1 (yard),
    // and inside the same as with the mouse: МЩУ ШК-1, МЩУ ШК-2, БЩУ (F at a board lists its buttons)
    static constexpr int kBoardPanel[8] = {kPCall1Out, kPCall2Out, kPGateIn, kPLights, kPGateOut, kPLock1, kPLock2, kPDuty};
    static int NodeDoor(int node) {
        switch (node) { case kNDoor1OutX: case kNDoor1OutI: return tp::kLock1Outer; case kNDoor1In: return tp::kLock1Inner;
                        case kNDoor2In: return tp::kLock2Inner; case kNDoor2OutI: case kNDoor2OutX: return tp::kLock2Outer; default: return -1; }
    }
    static int cNode(void* c, int i, OcNode* o) {
        CostScope cs_(CbCost());
        const Building* b = Self(c);
        if (i < 0 || i >= kNodeN) return 0;
        o->id = i; o->reach = 1.8; o->inside = 0;
        const int d = NodeDoor(i);
        if (d >= 0) {
            const tp::Door& D = tp::kDoors[d];
            const double um = 0.5 * (D.u0 + D.u1), cm = 0.5 * (D.c0 + D.c1);
            const bool outerSide = i == kNDoor1OutX || i == kNDoor2OutX;
            o->inside = outerSide ? 0 : 1;
            // which face: the yard / the hangar side for the outer ones from outside, the lock side from inside
            double off = 0.0;
            if (d == tp::kLock1Outer) off = outerSide ? -0.35 : 0.35;
            else if (d == tp::kLock2Outer) off = outerSide ? 0.45 : -0.35;
            else if (d == tp::kLock1Inner) off = 0.0;
            else off = 0.0;
            const double y = tp::kFloorY + 1.3;
            o->pos = D.run == 0 ? _V(um, y, cm + off) : _V(cm + off, y, um);
            std::snprintf(o->label, sizeof o->label, "%s", d == tp::kLock1Outer ? b->L("наружная дверь ШК-1", "ШК-1 outer door") : d == tp::kLock1Inner ? b->L("внутренняя дверь ШК-1", "ШК-1 inner door")
                                                         : d == tp::kLock2Inner ? b->L("внутренняя дверь ШК-2", "ШК-2 inner door") : b->L("наружная дверь ШК-2", "ШК-2 outer door"));
            return 1;
        }
        if (i == kNGateYard || i == kNGateHangar) {
            o->pos = _V(0.5 * (tp::kGateX0 + tp::kGateX1), 1.5, i == kNGateYard ? tp::kGateZ0 - 0.2 : tp::kHangar[2] + tp::kHangarWall + 0.2); o->reach = 4.0;
            std::snprintf(o->label, sizeof o->label, "%s", b->L("ворота ангара ВА-1", "hangar gate ВА-1"));
            return 1;
        }
        const int p = kBoardPanel[i - kNBoard0];
        const tp::Panel& P = tp::kPanels[p];
        o->pos = _V(P.c[0] + P.n[0] * 0.05, P.c[1], P.c[2] + P.n[2] * 0.05); o->reach = 1.4; o->inside = Inside(p) ? 1 : 0;
        std::snprintf(o->label, sizeof o->label, "%s", P.name);
        return 1;
    }
    enum { kActOpen = 1, kActClose = 2, kActStop = 3, kActEnter = 4, kActExit = 5, kActBtn0 = 10 };
    static int cActions(void* c, int node, int, OcAction* out, int max) {
        CostScope cs_(CbCost());
        const Building* b = Self(c);
        int n = 0;
        const int d = NodeDoor(node);
        if (d >= 0) {
            const int l = LockOf(d);
            // the way through first: outside «войти в шлюз», in the lock «выйти наружу», at the inner door «пройти в холл»
            // (the lock cycles by itself to that side and opens; F once is enough)
            if (node == kNDoor1OutX || node == kNDoor2OutX) tvAct(out, n, max, kActEnter, l == 0 ? b->L("войти в шлюз ШК-1", "enter airlock ШК-1") : b->L("войти в шлюз ШК-2", "enter airlock ШК-2"), true, nullptr, 0.3);
            else if (Outer(d)) tvAct(out, n, max, kActExit, l == 0 ? b->L("выйти во двор", "go out to the yard") : b->L("выйти в ангар", "go out into the hangar"), true, nullptr, 0.3);
            else tvAct(out, n, max, kActBtn0, b->L("пройти: шлюз <-> холл", "pass: airlock <-> hall"), true, nullptr, 0.3);
            if (b->doorT_[d] > 0.0) tvAct(out, n, max, kActClose, b->L("закрыть дверь", "close the door"), true, nullptr, 0.4);
            else { const char* r = b->CannotOpen(d); tvAct(out, n, max, kActOpen, b->L("открыть дверь", "open the door"), r == nullptr, r, 0.4); }
            return n;
        }
        if (node == kNGateYard || node == kNGateHangar) {
            const bool mv = std::fabs(b->gateT_ - b->gate_) > 1e-6;
            tvAct(out, n, max, kActOpen, b->L("открыть ворота", "open the gate"), b->gate_ < 1.0, b->L("открыты", "open"), 0.5);
            tvAct(out, n, max, kActClose, b->L("закрыть ворота", "close the gate"), b->gate_ > 0.0, b->L("закрыты", "shut"), 0.5);
            if (mv) tvAct(out, n, max, kActStop, b->L("остановить", "stop"), true, nullptr, 0.3);
            return n;
        }
        const int p = kBoardPanel[node - kNBoard0];
        const tp::Panel& P = tp::kPanels[p];
        for (int k = 0; k < P.nBtn; ++k) { const tp::Button& B = tp::kButtons[P.btn0 + k]; tvAct(out, n, max, kActBtn0 + k, b->L(B.ru, B.en), true, nullptr, 0.4); }
        return n;
    }
    void Act(int node, int act, int person) {
        const int d = NodeDoor(node);
        const int panel = d == tp::kLock1Outer ? kPCall1Out : d == tp::kLock2Outer ? kPCall2Out : d >= 0 ? (LockOf(d) == 0 ? kPLock1 : kPLock2) : -1;
        if (d >= 0) {
            const int l = LockOf(d);
            if (act == kActEnter) { Sfx(kSndClick); if (door_[d] >= 1.0) EnterLock(l, person); else { enter_[l] = person; Call(l, 1, panel); } return; }
            if (act == kActExit) { Sfx(kSndClick); if (door_[d] >= 1.0) ExitLock(l, person); else { exit_[l] = person; Call(l, 1, panel); } return; }
            if (act == kActBtn0) { Call(l, door_[InnerOf(l)] >= 1.0 ? 1 : 2, panel); Sfx(kSndClick); return; }
            seq_[LockOf(d)] = 0;
            if (act == kActClose) CloseDoor(d); else if (act == kActOpen) OpenDoor(d, panel);
            return;
        }
        if (node == kNGateYard || node == kNGateHangar) { GateCmd(act == kActOpen ? 0 : act == kActClose ? 1 : 2, node == kNGateYard ? kPGateOut : kPGateIn); return; }
        Press(kBoardPanel[node - kNBoard0], act - kActBtn0, person);
    }
};
}  // namespace tvb

// the vehicles' collisions with the buildings (TVChassis::Obstacles reads it as Tantra.dll's tantraOuterSolids): -> how many
extern "C" __declspec(dllexport) int tvOuterSolids(OBJHANDLE ship, tvb::Solid* out, int max) {
    for (tvb::Building* b : tvb::All()) if (b->GetHandle() == ship) return b->OuterSolids(out, max);
    return 0;
}
