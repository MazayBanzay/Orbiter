// Test runs only (tools\run_test.ps1 -Set "FxTest = ..."): the effects driven without a player, through the same AtmoFX
// exports the modules use, on the focus vessel. Tokens (any of them, separated by spaces or commas):
//   dust    - footsteps' sand: short puffs from either foot in turn
//   splash  - drops and wet clods thrown up and back, as a wheel through the shallows
//   jet     - a 800 N hydrogen jet straight down from 1 m (ground ejecta, or the cavity, spray and mist over a liquid)
//   wheels  - soil off four wheels as the EMPU's rear ones throw it (6 m/s rims, the arc 20-60 deg up, 2.5 kg/s each)
//   drive   - the same wheels rolling forward at 6 m/s (no slip: the source moves, the vessel stays), over 80 m and back
//   dirt    - the tholin film on the vessel up to 0.5 m
//   orbit   - the external camera pulled out to ~2000 km
//   far     - the external camera pulled back ~4x
//   sky     - the external camera turned up to the sky (clouds)
//   heat    - two hot nozzles (800 K over the air) on the back unit: their glow and the shimmer of the air
// Also "TestShot = <s>" (the screenshot's time; D3D11CLIENT_TEST_SHOT wins) in Config\D3D11Client\Client.cfg.
#include "Client.h"

extern "C" __declspec(dllexport) void AtmoFX_Emit(OBJHANDLE h, int id, const double* p, const double* v, double cone, double spread, double kgps, int kind, int ground, double wet, double life, double jetJ);
extern "C" __declspec(dllexport) void AtmoFX_SetJet(OBJHANDLE h, int id, const double* exitPos, const double* jetDir, double thrust, double ve, double exitT, int ground, double wet, double rExit);
extern "C" __declspec(dllexport) void AtmoFX_SetHeatSource(OBJHANDLE h, int id, double x, double y, double z, double r, double dT);
extern "C" __declspec(dllexport) void AtmoFX_SetDirt(OBJHANDLE h, double amount, double r, double g, double b, double topY);

static std::string FxTestKey(const char* key)
{
    std::string val;
    if (FILE* f = fopen("Config\\D3D11Client\\Client.cfg", "r")) {
        char line[256];
        const size_t n = strlen(key);
        while (fgets(line, sizeof line, f)) {
            const char* s = line; while (*s == ' ' || *s == '\t') s++;
            if (_strnicmp(s, key, n)) continue;
            s += n; while (*s == ' ' || *s == '\t') s++;
            if (*s != '=') continue;
            s++;
            val = s;
            size_t c = val.find(';'); if (c != std::string::npos) val = val.substr(0, c);
            while (!val.empty() && (val.back() == '\n' || val.back() == '\r' || val.back() == ' ')) val.pop_back();
        }
        fclose(f);
    }
    return val;
}

// a test-only number from Client.cfg (0 when absent): debug views (AtmoDbg)
double FxTestNum(const char* key)
{
    std::string v = FxTestKey(key);
    return v.empty() ? 0 : atof(v.c_str());
}

double FxTestShotTime()
{
    std::string v = FxTestKey("TestShot");
    return v.empty() ? -1 : atof(v.c_str());
}

void FxTestFrame()
{
    static bool parsed = false, dust = false, splash = false, jet = false, dirt = false, heat = false, sky = false, orbit = false, farCam = false, nearCam = false, behind = false, wheels = false, drive = false;
    if (!parsed) {
        parsed = true;
        std::string v = FxTestKey("FxTest");
        for (auto& ch : v) ch = (char)tolower(ch);
        wheels = v.find("wheels") != std::string::npos; drive = v.find("drive") != std::string::npos;
        dust = v.find("dust") != std::string::npos; splash = v.find("splash") != std::string::npos;
        jet = v.find("jet") != std::string::npos; dirt = v.find("dirt") != std::string::npos; heat = v.find("heat") != std::string::npos; sky = v.find("sky") != std::string::npos; orbit = v.find("orbit") != std::string::npos; farCam = v.find("far") != std::string::npos; nearCam = v.find("near") != std::string::npos; behind = v.find("behind") != std::string::npos;
        if (!v.empty()) LOGC("fx test: %s", v.c_str());
    }
    if (orbit) {      // the external camera pulled out to ~2000 km (from the scenario's few metres), once, after 3 s
        static bool done = false;
        if (!done && oapiGetSimTime() > 3) { done = true; oapiCameraScaleDist(5.0e5); LOGC("fx test: camera out to orbit"); }
    }
    if (behind) {     // the external camera turned round its target by ~150 deg (to look along the exhausts), once, after 3 s
        static bool done = false;
        if (!done && oapiGetSimTime() > 3) { done = true; oapiCameraRotAzimuth(2.6); LOGC("fx test: camera round"); }
    }
    if (nearCam) {    // the external camera pulled in ~20x (a distant scenario view to tens of metres), once, after 3 s
        static bool done = false;
        if (!done && oapiGetSimTime() > 3) { done = true; oapiCameraScaleDist(0.05); LOGC("fx test: camera in"); }
    }
    if (farCam) {        // the external camera pulled back ~4x (a few metres to ~12 m), once, after 3 s
        static bool done = false;
        if (!done && oapiGetSimTime() > 3) { done = true; oapiCameraScaleDist(4.0); LOGC("fx test: camera back"); }
    }
    if (sky) {        // the camera turned up to the sky (the external camera's polar angle), once, after 3 s
        static bool done = false;
        if (!done && oapiGetSimTime() > 3) { done = true; oapiCameraRotPolar(1.2); LOGC("fx test: camera up to the sky"); }
    }
    {   // "TestCamDist = <m>": the external camera at that distance from its target; "TestCamPolar = <rad>": turned by that
        // (both once, after 3 s; for views of the clouds from a chosen height)
        static bool done = false;
        if (!done && oapiGetSimTime() > 3) {
            done = true;
            const double dist = FxTestNum("TestCamDist"), pol = FxTestNum("TestCamPolar");
            if (dist > 0 && oapiCameraTargetDist() > 0) { oapiCameraScaleDist(dist / oapiCameraTargetDist()); LOGC("fx test: camera at %.0f m", dist); }
            if (pol != 0) { oapiCameraRotPolar(pol); LOGC("fx test: camera turned %.2f rad", pol); }
            const double pit = FxTestNum("TestCockpitPitch");     // the cockpit camera looking up by that [rad]
            if (pit != 0 && oapiCameraInternal()) { oapiCameraSetCockpitDir(0, pit); LOGC("fx test: cockpit view up %.2f rad", pit); }
        }
    }
    if (!dust && !splash && !jet && !dirt && !heat && !wheels && !drive) return;
    OBJHANDLE h = oapiGetFocusObject();
    if (!h || !oapiIsVessel(h)) return;
    VESSEL* v = oapiGetVesselInterface(h);
    const double t = oapiGetSimTime();
    const double alt = v->GetAltitude(ALTMODE_GROUND);
    if (dust) {        // a step every 0.6 s, the feet in turn, a puff for 0.15 s
        const int k = (int)floor(t / 0.6);
        const bool on = t - k * 0.6 < 0.15;
        const double x = (k & 1) ? 0.12 : -0.12;
        const double p[3] = { x, -alt + 0.03, 0.1 }, vel[3] = { 0, 0.6, -0.9 };
        AtmoFX_Emit(h, 9001, p, vel, 0.6, 0.6, on ? 0.08 : 0.0, 0, 0, 0.25, 0.0, 0.0);
    }
    if (wheels || drive) {   // the rim's tangent 40 deg up and back, the arc +-20 deg (TVChassis DustClient on regolith)
        const double xs[4] = { 1.85, -1.85, 1.85, -1.85 }, zs[4] = { -1.65, -1.65, -3.3, -3.3 };
        const double ph = 40 * PI / 180, R = 0.8;
        // driving: the hubs carried forward at the rim's speed (the throw then has the vessel's 6 m/s added)
        const double z0 = drive ? -40 + fmod(6 * t, 80.0) : 0, vz = drive ? 6 : 0;
        for (int i = 0; i < 4; i++) {
            const double p[3] = { xs[i], -alt + R - R * cos(ph), z0 + zs[i] - R * sin(ph) }, vel[3] = { 0, 6 * sin(ph), vz - 6 * cos(ph) };
            AtmoFX_Emit(h, 9030 + i, p, vel, 20 * PI / 180, 0.35, 2.5, 0, 3, 0.0, 0.0, 0.0);
        }
    }
    if (splash) {
        // thrown sideways (across the external camera's view, which looks along the vessel's z): the throw's profile
        const double p[3] = { 1.2, -alt + 0.3, 1.5 }, vel[3] = { 1.4, 1.8, 0 };
        AtmoFX_Emit(h, 9002, p, vel, 0.35, 0.5, 8.0, 3, 0, 0.0, 0.0, 0.0);     // ~ a wading wheel (TVChassis: 0.1 rho A r)
        const double pc[3] = { -1.0, -alt + 0.3, -0.6 }, vc[3] = { 0, 1.8, -1.4 };
        AtmoFX_Emit(h, 9003, pc, vc, 0.25, 0.4, 0.6, 0, 1, 0.9, 0.0, 0.0);
    }
    if (jet) {
        const double e[3] = { 0, -alt + 1.0, -0.3 }, d[3] = { 0, -1, 0 };
        AtmoFX_SetJet(h, 9010, e, d, 800.0, 2500.0, 900.0, 0, 0.25, 0.04);
    }
    if (heat) {        // two nozzles on the back unit at ~900 K (the module's own glow source)
        AtmoFX_SetHeatSource(h, 9020, 0.18, 0.15, -0.35, 0.05, 800.0);
        AtmoFX_SetHeatSource(h, 9021, -0.18, 0.15, -0.35, 0.05, 800.0);
    }
    if (dirt) {
        static bool once = false;
        if (!once) { once = true; AtmoFX_SetDirt(h, 0.8, 0.30, 0.20, 0.12, 0.5); }
    }
}
