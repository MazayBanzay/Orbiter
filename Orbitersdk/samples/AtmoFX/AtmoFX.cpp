// AtmoFX: "level 1" gas-giant atmosphere visuals with the stock D3D9 client (no client changes).
// The vessel AtmoProbe is an invisible observation probe that hangs at a fixed altitude above a fixed point
// of the planet (state is overridden every step) and carries:
//   - cloud decks: curved annuli, textures pre-hazed per distance band (the client's haze is not relied on);
//   - cumulus impostors: one mesh group of camera-facing quads (baked sprites), re-faced and sorted far-to-near;
//   - lightning under the deck: glow quads + point lights that light the decks from below;
//   - storm cells for the orbit view: flash quads on the night side, one group per cell.
// Everything lives in one runtime mesh in the "W" frame: origin at the anchor point on the planet surface,
// y up; the mesh is offset by (0,-h,0) in the vessel frame. Textures come from Meshes\AtmoFX\atmofx_tex.msh
// (made by tools\gen_assets.py; runtime meshes cannot add textures).
// Keys: Numpad8/2 climb/descend (Shift x10), Numpad4/6 drift north slower/faster, Numpad5 stop drift,
// K clouds on/off (FPS check), J lightning on/off. Scenario TOUR start target rate [km, km, km/s]: an automatic descent.
#define ORBITER_MODULE
#define NOMINMAX
#include "Orbitersdk.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace {

const double PI = 3.14159265358979323846;
const DWORD HIDE = 2;   // MESHGROUP UsrFlag: do not render

// texture slots in atmofx_tex.msh (gen_assets.py)
enum { TEX_CU = 0, TEX_NH3 = 1, TEX_NH4 = 5, TEX_FLASH = 9 };
const double BAND[5] = {0, 40e3, 120e3, 300e3, 0};   // haze band edges [m]; last = deck radius

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed) {}
    double u() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0; }
    double r(double a, double b) { return a + (b - a) * u(); }
    double g() { double a = u() + 1e-12, b = u(); return std::sqrt(-2 * std::log(a)) * std::cos(2 * PI * b); }
};

struct Sprite { VECTOR3 p; double w; int var; };
struct Flash { DWORD grp; double next = 0, until = -1; LightEmitter* light = nullptr; VECTOR3 n; bool shown = false; };

inline VECTOR3 unitv(const VECTOR3& v) { return v / length(v); }

class AtmoProbe : public VESSEL3 {
public:
    AtmoProbe(OBJHANDLE h, int fmodel) : VESSEL3(h, fmodel) {}
    ~AtmoProbe() { if (mesh_) oapiDeleteMesh(mesh_); }

    void clbkSetClassCaps(FILEHANDLE cfg) override {
        SetSize(1000); SetClipRadius(1.0); SetEmptyMass(1000);
        SetVisibilityLimit(1e-9, 1e-9);
        SetCameraOffset(_V(0, 0, 0));
        SetCameraDefaultDirection(unitv(_V(0, -0.22, 1)));
        auto rd = [&](const char* k, double& v) { double x; if (oapiReadItem_float(cfg, (char*)k, x)) v = x; };
        rd("ProbeAlt", hAlt_); rd("DeckAlt", aNH3_); rd("LowDeckAlt", aNH4_); rd("GlowAlt", aGlow_);
        rd("StormAlt", aStorm_); rd("DeckRadius", deckR_); rd("SpriteRadius", spriteR_); rd("SunElev", sunElev_);
        rd("DeckTile", tile3_); rd("LowDeckTile", tile4_); rd("SpriteScale", spriteScale_); rd("CloudLayerAlt", cloudLayer_); rd("Drift", drift_);
        int n; if (oapiReadItem_int(cfg, (char*)"Sprites", n)) nSpr_ = std::max(0, std::min(n, 2000));
        if (oapiReadItem_int(cfg, (char*)"StormCells", n)) nCells_ = std::max(0, std::min(n, 400));
        hAlt_ *= 1e3; aNH3_ *= 1e3; aNH4_ *= 1e3; aGlow_ *= 1e3; aStorm_ *= 1e3; deckR_ *= 1e3; spriteR_ *= 1e3;
        tile3_ *= 1e3; tile4_ *= 1e3; cloudLayer_ *= 1e3; drift_ *= 1e3;
    }

    void clbkLoadStateEx(FILEHANDLE scn, void* vs) override {
        char* line;
        while (oapiReadScenario_nextline(scn, line)) {
            if (!_strnicmp(line, "ALTKM", 5)) { sscanf(line + 5, "%lf", &hAlt_); hAlt_ *= 1e3; altFromScn_ = true; }
            else if (!_strnicmp(line, "SUNELEV", 7)) sscanf(line + 7, "%lf", &sunElev_);
            else if (!_strnicmp(line, "TOUR", 4)) { double a, b, r; if (sscanf(line + 4, "%lf %lf %lf", &a, &b, &r) == 3) { hAlt_ = a * 1e3; tourTarget_ = b * 1e3; tourRate_ = std::fabs(r) * 1e3; tourOn_ = true; } }
            else if (!_strnicmp(line, "DRIFT", 5)) { sscanf(line + 5, "%lf", &drift_); drift_ *= 1e3; }
            else if (!_strnicmp(line, "LAUNCH", 6)) {   // LAUNCH <vessel> <alt km> <airspeed km/s> <flight-path deg> <range km before the anchor>
                if (sscanf(line + 6, "%63s %lf %lf %lf %lf", launchName_, &launchAlt_, &launchSpd_, &launchFpa_, &launchRange_) >= 4) launchPending_ = true;
            }
            else if (!_strnicmp(line, "CLOUDS", 6)) { int on = 1; sscanf(line + 6, "%d", &on); cloudsOn_ = on != 0; }   // 0: mesh decks off (volumetric shader test)
            else if (!_strnicmp(line, "MAPANCHOR", 9)) { if (sscanf(line + 9, "%lf %lf", &mapU_, &mapV_) == 2) mapAnchor_ = true; }   // u v on the stock cloud map
            else if (!_strnicmp(line, "ANCHOR", 6)) { if (sscanf(line + 6, "%lf %lf", &lng_, &lat_) == 2) { lng_ *= PI / 180; lat_ *= PI / 180; haveAnchor_ = true; } }
            else ParseScenarioLineEx(line, vs);
        }
    }
    void clbkSaveState(FILEHANDLE scn) override {
        VESSEL3::clbkSaveState(scn);
        char b[96];
        sprintf(b, "%.6f %.6f", lng_ * 180 / PI, lat_ * 180 / PI); oapiWriteScenario_string(scn, (char*)"ANCHOR", b);
        oapiWriteScenario_float(scn, (char*)"ALTKM", hAlt_ / 1e3);
        oapiWriteScenario_float(scn, (char*)"SUNELEV", -999);   // keep the anchor as saved
    }

    void clbkPostCreation() override {
        planet_ = GetSurfaceRef();
        R_ = oapiGetSize(planet_);
        if (mapAnchor_) {   // a point of the stock cloud map (e.g. the Great Red Spot), in the cloud layer's current phase
            const double crot = *(double*)oapiGetObjectParam(planet_, OBJPRM_PLANET_CLOUDROTATION);
            lat_ = (0.5 - mapV_) * PI;
            lng_ = std::fmod((mapU_ - 0.5) * 2 * PI - crot + 3 * PI, 2 * PI) - PI;
            haveAnchor_ = true;
            VECTOR3 sunG, pc, g; oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sunG); oapiGetGlobalPos(planet_, &pc);
            oapiEquToGlobal(planet_, lng_, lat_, R_, &g);
            oapiWriteLogV("AtmoFX: map anchor u %.3f v %.3f -> lng %.2f lat %.2f (cloud rotation %.3f rad), sun elevation there %.0f deg",
                          mapU_, mapV_, lng_ * 180 / PI, lat_ * 180 / PI, crot, std::asin(dotp(unitv(g - pc), unitv(sunG - pc))) * 180 / PI);
        }
        if (!haveAnchor_) {
            double rad; GetEquPos(lng_, lat_, rad);
            if (sunElev_ > -90) PickSunLongitude();
        }
        BuildMesh();
        meshOfs_ = _V(0, -hAlt_, 0);
        meshIdx_ = AddMesh(mesh_, &meshOfs_);
        SetMeshVisibilityMode(meshIdx_, MESHVIS_ALWAYS);
        for (auto& f : glows_) {
            const VECTOR3& p = glowPos_[&f - glows_.data()];
            f.light = AddPointLight(p - _V(0, hAlt_, 0), 90e3, 1.0, 0.0, 2.0e-9,
                                    COLOUR4{0.75f, 0.85f, 1.0f, 0}, COLOUR4{0, 0, 0, 0}, COLOUR4{0, 0, 0, 0});
            f.light->Activate(false);
        }
        if (hAlt_ > 20000e3) { SetCameraDefaultDirection(unitv(_V(0, -1, 0.05))); oapiCameraSetCockpitDir(0, 0, false); }
        Place(0);
        char nm[64]; oapiGetObjectName(planet_, nm, 64);
        oapiWriteLogV("AtmoFX: %s anchor lng %.2f lat %.2f, probe alt %.1f km, decks %.0f/%.0f km, %d sprites, %d storm cells, %u groups",
                      nm, lng_ * 180 / PI, lat_ * 180 / PI, hAlt_ / 1e3, aNH3_ / 1e3, aNH4_ / 1e3, nSpr_, nCells_, oapiMeshGroupCount(mesh_));
    }

    void clbkVisualCreated(VISHANDLE vis, int) override {
        dev_ = GetDevMesh(vis, meshIdx_);
        std::fill(applied_.begin(), applied_.end(), 0xFFFFFFFFu);
        lastUpd_ = -1e9;
    }
    void clbkVisualDestroyed(VISHANDLE, int) override { dev_ = nullptr; }

    int clbkConsumeDirectKey(char* ks) override {
        climb_ = 0;
        const double k = (KEYDOWN(ks, OAPI_KEY_LSHIFT) || KEYDOWN(ks, OAPI_KEY_RSHIFT)) ? 10 : 1;
        if (KEYDOWN(ks, OAPI_KEY_NUMPAD8)) { climb_ = 300 * k; RESETKEY(ks, OAPI_KEY_NUMPAD8); }
        if (KEYDOWN(ks, OAPI_KEY_NUMPAD2)) { climb_ = -300 * k; RESETKEY(ks, OAPI_KEY_NUMPAD2); }
        return 0;
    }
    int clbkConsumeBufferedKey(DWORD key, bool down, char*) override {
        if (!down) return 0;
        if (key == OAPI_KEY_K) { cloudsOn_ = !cloudsOn_; lastUpd_ = -1e9; return 1; }
        if (key == OAPI_KEY_J) { lightningOn_ = !lightningOn_; return 1; }
        if (key == OAPI_KEY_NUMPAD6) { drift_ += 200; return 1; }
        if (key == OAPI_KEY_NUMPAD4) { drift_ -= 200; return 1; }
        if (key == OAPI_KEY_NUMPAD5) { drift_ = 0; return 1; }
        if (key == OAPI_KEY_L) { stormsAll_ = !stormsAll_; return 1; }
        return 0;
    }

    void clbkPostStep(double simt, double simdt, double) override {
        const double sysdt = oapiGetSysStep();
        if (launchPending_) { launchPending_ = false; LaunchVessel(); }
        if (climb_ != 0) { tourOn_ = false; hAlt_ = std::max(1e3, hAlt_ + climb_ * sysdt); }
        if (tourOn_) {
            const double step = tourRate_ * sysdt;   // real time: time acceleration does not spin the sun away
            if (std::fabs(tourTarget_ - hAlt_) <= step) { hAlt_ = tourTarget_; tourOn_ = false; }
            else hAlt_ += (tourTarget_ > hAlt_ ? step : -step);
        }
        oz_ += drift_ * simdt;
        const VECTOR3 ofs = _V(0, -hAlt_, -oz_);   // the mesh stays with the planet while the probe moves
        if (length(ofs - meshOfs_) > 0) {
            ShiftMesh(meshIdx_, ofs - meshOfs_); meshOfs_ = ofs;
            for (size_t i = 0; i < glows_.size(); ++i) if (glows_[i].light) glows_[i].light->SetPosition(glowPos_[i] + ofs);
        }
        Place(simdt);

        VECTOR3 camG, me; oapiCameraGlobalPos(&camG); GetGlobalPos(me);
        VECTOR3 camL; Global2Local(camG, camL);
        const VECTOR3 camW = camL - meshOfs_;
        const double camDist = length(camG - me);
        const double camAlt = length(camW - _V(0, -R_, 0)) - R_;
        const bool nearCam = camAlt < cloudLayer_ && std::hypot(camW.x, camW.z) < deckR_ * 0.8;   // under the stock cloud layer
        const bool orbitView = camAlt > aStorm_ + 3000e3;
        const bool camClose = camDist < 2500e3;
        if (camClose != sizeNear_) { SetSize(camClose ? 1000.0 : 5e6); sizeNear_ = camClose; }

        // sun direction in W
        VECTOR3 sunG, pl; oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sunG); oapiGetGlobalPos(planet_, &pl);
        MATRIX3 Rv; GetRotationMatrix(Rv);
        const VECTOR3 sunW = unitv(tmul(Rv, sunG - pl));

        // ---- flicker
        const double t = oapiGetSimTime();
        auto flick = [&](Flash& f, double gapMin, double gapMax, double onMin = 0.05, double onMax = 0.22) {
            if (t > f.next) { f.until = t + rng_.r(onMin, onMax); f.next = (rng_.u() < 0.35) ? f.until + rng_.r(0.05, 0.15) : t + rng_.r(gapMin, gapMax); }
            return t < f.until;
        };
        for (auto& c : cells_) {
            const VECTOR3 p = _V(0, -R_, 0) + c.n * (R_ + aStorm_);
            const bool night = dotp(c.n, sunW) < -0.08, facing = dotp(c.n, camW - p) > 0;
            const bool on = stormsAll_ || flick(c, 0.3, 4.0, 0.10, 0.45);
            Want(c.grp, lightningOn_ && cloudsOn_ && orbitView && night && facing && on);
        }
        for (auto& f : glows_) {
            const bool on = lightningOn_ && cloudsOn_ && nearCam && flick(f, 1.0, 6.0);
            Want(f.grp, on);
            if (f.light && on != f.shown) f.light->Activate(on);
            f.shown = on;
        }
        for (DWORD g : deckGrps_) Want(g, cloudsOn_ && nearCam);
        Want(cuGrp_, cloudsOn_ && nearCam && nSpr_ > 0);

        // ---- impostors: re-face and sort when the camera moved or turned enough
        if (cloudsOn_ && nearCam && dev_ && nSpr_ > 0) {
            const double moved = length(camW - lastCam_);
            if (t - lastUpd_ > 0.5 || moved > 400.0) { UpdateSprites(camW, sunW); lastCam_ = camW; lastUpd_ = t; }
        }
        ApplyFlags();

        fpsAvg_ = fpsAvg_ * 0.95 + (sysdt > 0 ? 1.0 / sysdt : 0) * 0.05;
        // a status line in Orbiter.log every 5 s (what the screen showed, for remote checks)
        logT_ += sysdt;
        if (logT_ > 5.0) {
            logT_ = 0;
            int cellsLit = 0; for (auto& c : cells_) cellsLit += (want_[c.grp] == 0);
            oapiWriteLogV("AtmoFX: fps %.0f, cam alt %.0f km, cam-probe %.0f km, %s, clouds %s, lightning %s, visual %s, sun %.0f deg, storm cells lit %d, sprite upd %.2f ms",
                          fpsAvg_, camAlt / 1e3, camDist / 1e3, orbitView ? "orbit view" : (nearCam ? "in the clouds" : "between"),
                          cloudsOn_ ? "on" : "off", lightningOn_ ? "on" : "off", dev_ ? "yes" : "NO",
                          std::asin(std::max(-1.0, std::min(1.0, sunW.y))) * 180 / PI, cellsLit, updMs_);
        }
        sprintf(oapiDebugString(), "AtmoFX  alt %.0f km%s (stock clouds %.0f, deck %.0f)  drift %.1f km/s [Num4/5/6]  climb [Num8/2]  clouds %s [K]  lightning %s [J]%s  sun %.0f deg  fps~%.0f",
                hAlt_ / 1e3, tourOn_ ? " descending" : "", cloudLayer_ / 1e3, aNH3_ / 1e3, drift_ / 1e3, cloudsOn_ ? "on" : "off", lightningOn_ ? "on" : "off", stormsAll_ ? "  ALL STORMS LIT [L]" : "  [L] all storms",
                std::asin(std::max(-1.0, std::min(1.0, sunW.y))) * 180 / PI, fpsAvg_);
    }

private:
    // ---------------- placement: hang at hAlt_ over (lng_, lat_), y up, z north
    void Place(double simdt) {
        VECTOR3 pc, gp, gn;
        oapiGetGlobalPos(planet_, &pc);
        const double lat = lat_ + oz_ / R_;
        oapiEquToGlobal(planet_, lng_, lat, R_ + hAlt_, &gp);
        oapiEquToGlobal(planet_, lng_, lat + 1e-4, R_ + hAlt_, &gn);
        const VECTOR3 up = unitv(gp - pc);
        VECTOR3 north = gn - gp; north = unitv(north - up * dotp(north, up));
        const VECTOR3 x = crossp(up, north);
        const MATRIX3 Rm = _M(x.x, up.x, north.x, x.y, up.y, north.y, x.z, up.z, north.z);
        const VECTOR3 rel = gp - pc;
        VECTOR3 vel = _V(0, 0, 0);
        if (havePrev_ && simdt > 0) vel = (rel - prevRel_) / simdt;
        prevRel_ = rel; havePrev_ = true;
        VESSELSTATUS2 vs; memset(&vs, 0, sizeof vs); vs.version = 2;
        GetStatusEx(&vs);
        vs.rbody = planet_; vs.status = 0; vs.rpos = rel; vs.rvel = vel; vs.vrot = _V(0, 0, 0);
        vs.nfuel = 0; vs.fuel = nullptr; vs.nthruster = 0; vs.thruster = nullptr; vs.ndockinfo = 0; vs.dockinfo = nullptr;
        vs.flag = 0;
        DefSetStateEx(&vs);
        SetRotationMatrix(Rm);
    }

    // put another vessel on an entry path: launchRange_ km west of the anchor, flying east (with the rotation) at
    // launchSpd_ km/s relative to the atmosphere, launchFpa_ deg below the horizon, nose along the flow
    void LaunchVessel() {
        OBJHANDLE hv = oapiGetVesselByName(launchName_);
        if (!hv) { oapiWriteLogV("AtmoFX: LAUNCH: vessel %s not found", launchName_); return; }
        VESSEL* v = oapiGetVesselInterface(hv);
        const double r = R_ + launchAlt_ * 1e3, lat = lat_;
        const double lng = lng_ - launchRange_ * 1e3 / (R_ * std::cos(lat));
        VECTOR3 pc, gp, ge, gn;
        oapiGetGlobalPos(planet_, &pc);
        oapiEquToGlobal(planet_, lng, lat, r, &gp);
        oapiEquToGlobal(planet_, lng + 1e-5, lat, r, &ge);
        oapiEquToGlobal(planet_, lng, lat + 1e-5, r, &gn);
        const VECTOR3 up = unitv(gp - pc);
        VECTOR3 east = ge - gp; east = unitv(east - up * dotp(east, up));
        VECTOR3 north = gn - gp; north = unitv(north - up * dotp(north, up));
        const double omega = 2 * PI / oapiGetPlanetPeriod(planet_);
        const VECTOR3 vRot = east * (omega * r * std::cos(lat));
        const double fpa = launchFpa_ * PI / 180;
        const VECTOR3 dir = unitv(east * std::cos(fpa) - up * std::sin(fpa));
        const VECTOR3 vel = vRot + dir * (launchSpd_ * 1e3);
        VESSELSTATUS2 vs; memset(&vs, 0, sizeof vs); vs.version = 2;
        v->GetStatusEx(&vs);
        vs.rbody = planet_; vs.status = 0; vs.rpos = gp - pc; vs.rvel = vel; vs.vrot = _V(0, 0, 0);
        vs.nfuel = 0; vs.fuel = nullptr; vs.nthruster = 0; vs.thruster = nullptr; vs.ndockinfo = 0; vs.dockinfo = nullptr; vs.flag = 0;
        v->DefSetStateEx(&vs);
        // nose (+z) along the airflow, wings level (+y away from the planet)
        const VECTOR3 z = dir, y = unitv(up - z * dotp(up, z)), x = crossp(y, z);
        v->SetRotationMatrix(_M(x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z));
        oapiWriteLogV("AtmoFX: LAUNCH %s: alt %.0f km, %.1f km/s relative to the air (%.1f inertial), path %.1f deg, %.0f km west of the anchor",
                      launchName_, launchAlt_, launchSpd_, length(vel) / 1e3, -launchFpa_, launchRange_);
    }

    void PickSunLongitude() {
        VECTOR3 sunG, pc; oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sunG); oapiGetGlobalPos(planet_, &pc);
        const VECTOR3 s = unitv(sunG - pc);
        double best = 1e9, bestLng = lng_;
        for (int i = 0; i < 3600; ++i) {
            const double lg = -PI + 2 * PI * i / 3600.0;
            VECTOR3 g; oapiEquToGlobal(planet_, lg, lat_, R_, &g);
            const double e = std::asin(dotp(unitv(g - pc), s)) * 180 / PI;
            // afternoon side preferred: elevation falling to the east
            VECTOR3 g2; oapiEquToGlobal(planet_, lg + 0.01, lat_, R_, &g2);
            const double e2 = std::asin(dotp(unitv(g2 - pc), s)) * 180 / PI;
            const double cost = std::fabs(e - sunElev_) + (e2 < e ? 0 : 0.5);
            if (cost < best) { best = cost; bestLng = lg; }
        }
        lng_ = bestLng;
    }

    // ---------------- geometry
    MESHGROUP NewGroup(std::vector<NTVERTEX>& v, std::vector<WORD>& ix, DWORD tex, DWORD mtrl) {
        MESHGROUP g{}; g.Vtx = v.data(); g.nVtx = (DWORD)v.size(); g.Idx = ix.data(); g.nIdx = (DWORD)ix.size();
        g.TexIdx = tex; g.MtrlIdx = mtrl; g.UsrFlag = HIDE;
        return g;
    }
    DWORD Add(std::vector<NTVERTEX>& v, std::vector<WORD>& ix, DWORD tex, DWORD mtrl) {
        MESHGROUP g = NewGroup(v, ix, tex, mtrl);
        const DWORD gi = oapiAddMeshGroup(mesh_, &g);
        if (applied_.size() <= gi) applied_.resize(gi + 1, 0xFFFFFFFFu), want_.resize(gi + 1, HIDE);
        return gi;
    }
    static void Quad2(std::vector<WORD>& ix, WORD a, WORD b, WORD c, WORD d) {   // both windings
        const WORD q[12] = {a, b, c, a, c, d, a, c, b, a, d, c};
        ix.insert(ix.end(), q, q + 12);
    }
    VECTOR3 OnSphere(double alt, double s, double th, VECTOR3* nrm) const {
        const double Rr = R_ + alt, a = s / Rr;
        const VECTOR3 n = _V(std::sin(a) * std::cos(th), std::cos(a), std::sin(a) * std::sin(th));
        if (nrm) *nrm = n;
        return _V(0, -R_, 0) + n * Rr;
    }
    void DeckBand(double alt, double r0, double r1, double tile, DWORD tex, DWORD mtrl) {
        const int segs = 120, rings = std::max(4, (int)((r1 - r0) / 12e3) + 4);
        std::vector<NTVERTEX> v; std::vector<WORD> ix;
        for (int i = 0; i <= rings; ++i) {
            const double f = (double)i / rings, s = r0 + (r1 - r0) * f * f * 0.3 + (r1 - r0) * f * 0.7;
            for (int j = 0; j < segs; ++j) {
                const double th = 2 * PI * j / segs;
                VECTOR3 n; const VECTOR3 p = OnSphere(alt, s, th, &n);
                NTVERTEX q{}; q.x = (float)p.x; q.y = (float)p.y; q.z = (float)p.z;
                q.nx = (float)n.x; q.ny = (float)n.y; q.nz = (float)n.z;
                q.tu = (float)(p.x / tile); q.tv = (float)(p.z / tile);
                v.push_back(q);
            }
        }
        for (int i = 0; i < rings; ++i) for (int j = 0; j < segs; ++j) {
            const int j2 = (j + 1) % segs;
            Quad2(ix, (WORD)(i * segs + j), (WORD)(i * segs + j2), (WORD)((i + 1) * segs + j2), (WORD)((i + 1) * segs + j));
        }
        deckGrps_.push_back(Add(v, ix, tex, mtrl));
    }
    void FlatQuad(std::vector<NTVERTEX>& v, std::vector<WORD>& ix, const VECTOR3& c, const VECTOR3& n, double size) {
        VECTOR3 a = std::fabs(n.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0);
        const VECTOR3 e1 = unitv(crossp(n, a)), e2 = crossp(n, e1);
        const WORD b = (WORD)v.size();
        const double uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int k = 0; k < 4; ++k) {
            const VECTOR3 p = c + e1 * ((uv[k][0] - 0.5) * size) + e2 * ((uv[k][1] - 0.5) * size);
            NTVERTEX q{}; q.x = (float)p.x; q.y = (float)p.y; q.z = (float)p.z; q.nx = (float)n.x; q.ny = (float)n.y; q.nz = (float)n.z;
            q.tu = (float)uv[k][0]; q.tv = (float)uv[k][1]; v.push_back(q);
        }
        Quad2(ix, b, (WORD)(b + 1), (WORD)(b + 2), (WORD)(b + 3));
    }

    void BuildMesh() {
        mesh_ = oapiLoadMesh("AtmoFX\\atmofx_tex");
        if (!mesh_) { oapiWriteLog((char*)"AtmoFX: Meshes\\AtmoFX\\atmofx_tex.msh not found"); mesh_ = oapiCreateMesh(0, nullptr); }
        applied_.assign(oapiMeshGroupCount(mesh_), 0xFFFFFFFFu); want_.assign(applied_.size(), HIDE);
        MATERIAL lit{}; lit.diffuse = {1, 1, 1, 1}; lit.ambient = {0.45f, 0.45f, 0.45f, 1}; lit.specular = {0, 0, 0, 1}; lit.emissive = {0.06f, 0.06f, 0.06f, 1}; lit.power = 1;
        MATERIAL glow{}; glow.diffuse = {0, 0, 0, 1}; glow.ambient = {0, 0, 0, 1}; glow.specular = {0, 0, 0, 1}; glow.emissive = {0.8f, 0.9f, 1.0f, 1}; glow.power = 1;
        const DWORD mLit = oapiAddMaterial(mesh_, &lit), mGlow = oapiAddMaterial(mesh_, &glow);
        double band[5]; std::memcpy(band, BAND, sizeof band); band[4] = deckR_;
        Rng rng(7);

        // draw order = group order: low deck, glows, deck, impostors, storm cells
        for (int b = 0; b < 4; ++b) DeckBand(aNH4_, band[b], band[b + 1], tile4_, TEX_NH4 + b, mLit);
        for (int i = 0; i < 4; ++i) {
            const double s = rng.r(30e3, 160e3), th = rng.r(0, 2 * PI);
            VECTOR3 n; const VECTOR3 p = OnSphere(aGlow_, s, th, &n);
            std::vector<NTVERTEX> v; std::vector<WORD> ix; FlatQuad(v, ix, p, n, rng.r(25e3, 45e3));
            Flash f; f.grp = Add(v, ix, TEX_FLASH, mGlow); glows_.push_back(f); glowPos_.push_back(p);
        }
        for (int b = 0; b < 4; ++b) DeckBand(aNH3_, band[b], band[b + 1], tile3_, TEX_NH3 + b, mLit);

        // impostors: clusters on the upper deck
        spr_.clear();
        const int nClus = std::max(1, nSpr_ / 7);
        std::vector<VECTOR3> cl;
        for (int c = 0; c < nClus; ++c) { const double s = spriteR_ * std::pow(rng.u(), 0.8), th = rng.r(0, 2 * PI); cl.push_back(_V(s * std::cos(th), 0, s * std::sin(th))); }
        for (int i = 0; i < nSpr_; ++i) {
            const VECTOR3& c = cl[i % nClus];
            const double x = c.x + rng.g() * 9e3, z = c.z + rng.g() * 9e3, s = std::hypot(x, z);
            Sprite sp; sp.w = rng.r(9e3, 30e3) * spriteScale_ * (rng.u() < 0.06 ? 2.0 : 1.0); sp.var = (int)(rng.u() * 4) & 3;
            const VECTOR3 base = OnSphere(aNH3_, s, std::atan2(z, x), nullptr);
            sp.p = base - _V(0, sp.w * 0.18, 0);
            spr_.push_back(sp);
        }
        {
            std::vector<NTVERTEX> v(nSpr_ * 4); std::vector<WORD> ix;
            for (int i = 0; i < nSpr_; ++i) Quad2(ix, (WORD)(i * 4), (WORD)(i * 4 + 1), (WORD)(i * 4 + 2), (WORD)(i * 4 + 3));
            vbuf_.resize(v.size());
            FillSprites(v.data(), _V(0, hAlt_, 0), _V(0, 1, 0));   // initial faces: also gives the group's bounds
            if (nSpr_ > 0) cuGrp_ = Add(v, ix, TEX_CU, mLit);
        }

        // storm cells over the whole sphere (shown on the night side, facing the camera)
        for (int c = 0; c < nCells_; ++c) {
            const double lat = std::asin(rng.r(-0.8, 0.8)), lng = rng.r(0, 2 * PI);
            const VECTOR3 n = _V(std::cos(lat) * std::cos(lng), std::sin(lat), std::cos(lat) * std::sin(lng));
            std::vector<NTVERTEX> v; std::vector<WORD> ix;
            const int k = 2 + (int)(rng.u() * 5);
            VECTOR3 e1 = unitv(crossp(n, std::fabs(n.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0))), e2 = crossp(n, e1);
            for (int q = 0; q < k; ++q) {
                const VECTOR3 d = unitv(n + e1 * (rng.g() * 0.012) + e2 * (rng.g() * 0.006));
                const VECTOR3 p = _V(0, -R_, 0) + d * (R_ + aStorm_);
                FlatQuad(v, ix, p, d, rng.r(1500e3, 4500e3));
            }
            Flash f; f.grp = Add(v, ix, TEX_FLASH, mGlow); f.n = n; f.next = rng.r(0, 5); cells_.push_back(f);
        }
    }

    void FillSprites(NTVERTEX* out, const VECTOR3& cam, const VECTOR3& sunW) {
        std::vector<std::pair<double, int>> ord(nSpr_);
        for (int i = 0; i < nSpr_; ++i) ord[i] = {length(spr_[i].p + _V(0, spr_[i].w * 0.5, 0) - cam), i};
        std::sort(ord.begin(), ord.end(), [](auto& a, auto& b) { return a.first > b.first; });
        const VECTOR3 nrm = unitv(_V(0, 1, 0) + sunW * 1.5);   // baked shading: light them mostly from the sun side
        for (int k = 0; k < nSpr_; ++k) {
            const Sprite& s = spr_[ord[k].second];
            const double d = ord[k].first;
            NTVERTEX* q = out + k * 4;
            VECTOR3 f = _V(cam.x - s.p.x, 0, cam.z - s.p.z);
            const double fl = length(f);
            f = fl > 1 ? f / fl : _V(0, 0, 1);
            const VECTOR3 r = _V(f.z, 0, -f.x) * (s.w * 0.5), up = _V(0, s.w, 0);
            const bool hide = d < s.w * 0.55;   // too close: a flat card would show
            const VECTOR3 P[4] = {s.p - r, s.p + r, s.p + r + up, s.p - r + up};
            const int row = d < 40e3 ? 0 : d < 120e3 ? 1 : d < 300e3 ? 2 : 3;
            const float u0 = s.var * 0.25f + 0.002f, u1 = (s.var + 1) * 0.25f - 0.002f;
            const float vt = row * 0.25f + 0.002f, vb = (row + 1) * 0.25f - 0.002f;
            const float U[4] = {u0, u1, u1, u0}, V[4] = {vb, vb, vt, vt};
            for (int j = 0; j < 4; ++j) {
                const VECTOR3 p = hide ? s.p : P[j];
                q[j].x = (float)p.x; q[j].y = (float)p.y; q[j].z = (float)p.z;
                q[j].nx = (float)nrm.x; q[j].ny = (float)nrm.y; q[j].nz = (float)nrm.z;
                q[j].tu = U[j]; q[j].tv = V[j];
            }
        }
    }
    void UpdateSprites(const VECTOR3& cam, const VECTOR3& sunW) {
        LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
        FillSprites(vbuf_.data(), cam, sunW);
        GROUPEDITSPEC ges{}; ges.flags = GRPEDIT_VTX; ges.Vtx = vbuf_.data(); ges.nVtx = (DWORD)vbuf_.size(); ges.vIdx = nullptr;
        oapiEditMeshGroup(dev_, cuGrp_, &ges);
        QueryPerformanceCounter(&b);
        updMs_ = (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
    }

    void Want(DWORD g, bool show) { if (g < want_.size()) want_[g] = show ? 0 : HIDE; }
    void ApplyFlags() {
        if (!dev_) return;
        for (DWORD g = 0; g < want_.size(); ++g) if (applied_[g] != want_[g]) {
            GROUPEDITSPEC ges{}; ges.flags = GRPEDIT_SETUSERFLAG; ges.UsrFlag = want_[g];
            oapiEditMeshGroup(dev_, g, &ges);
            applied_[g] = want_[g];
        }
    }

    // parameters (km / deg in the cfg; m / rad here)
    double hAlt_ = 455, aNH3_ = 433, aNH4_ = 418, aGlow_ = 425, aStorm_ = 510, deckR_ = 1200, spriteR_ = 350;
    double tile3_ = 45, tile4_ = 90, spriteScale_ = 1.0, sunElev_ = 30, cloudLayer_ = 480, drift_ = 0;
    double tourTarget_ = 0, tourRate_ = 0, oz_ = 0;
    bool tourOn_ = false, stormsAll_ = false, launchPending_ = false;
    char launchName_[64] = "";
    bool mapAnchor_ = false;
    double mapU_ = 0, mapV_ = 0;
    double launchAlt_ = 0, launchSpd_ = 0, launchFpa_ = 0, launchRange_ = 0;
    VECTOR3 meshOfs_ = _V(0, 0, 0);
    int nSpr_ = 300, nCells_ = 140;
    bool altFromScn_ = false, haveAnchor_ = false, sizeNear_ = true;

    OBJHANDLE planet_ = nullptr;
    double R_ = 0, lng_ = 0, lat_ = 0;
    MESHHANDLE mesh_ = nullptr;
    UINT meshIdx_ = 0;
    DEVMESHHANDLE dev_ = nullptr;
    std::vector<DWORD> applied_, want_, deckGrps_;
    std::vector<Flash> glows_, cells_;
    std::vector<VECTOR3> glowPos_;
    std::vector<Sprite> spr_;
    std::vector<NTVERTEX> vbuf_;
    DWORD cuGrp_ = 0xFFFFFFFFu;
    VECTOR3 lastCam_ = _V(0, 0, 0), prevRel_ = _V(0, 0, 0);
    double lastUpd_ = -1e9, climb_ = 0, fpsAvg_ = 0, updMs_ = 0, logT_ = 0;
    bool havePrev_ = false, cloudsOn_ = true, lightningOn_ = true;
    Rng rng_{12345};
};

} // namespace

DLLCLBK VESSEL* ovcInit(OBJHANDLE h, int fmodel) { return new AtmoProbe(h, fmodel); }
DLLCLBK void ovcExit(VESSEL* v) { delete static_cast<AtmoProbe*>(v); }
