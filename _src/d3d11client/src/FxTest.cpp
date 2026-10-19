// Test runs only (tools\run_test.ps1 -Set "FxTest = ..."): the effects driven without a player, through the same AtmoFX
// exports the modules use, on the focus vessel. Tokens (any of them, separated by spaces or commas):
//   dust    - footsteps' sand: short puffs from either foot in turn
//   splash  - drops and wet clods thrown up and back, as a wheel through the shallows
//   jet     - a 800 N hydrogen jet straight down from 1 m (ground ejecta, or the cavity, spray and mist over a liquid)
//   dirt    - the tholin film on the vessel up to 0.5 m
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

double FxTestShotTime()
{
    std::string v = FxTestKey("TestShot");
    return v.empty() ? -1 : atof(v.c_str());
}

void FxTestFrame()
{
    static bool parsed = false, dust = false, splash = false, jet = false, dirt = false, heat = false;
    if (!parsed) {
        parsed = true;
        std::string v = FxTestKey("FxTest");
        for (auto& ch : v) ch = (char)tolower(ch);
        dust = v.find("dust") != std::string::npos; splash = v.find("splash") != std::string::npos;
        jet = v.find("jet") != std::string::npos; dirt = v.find("dirt") != std::string::npos; heat = v.find("heat") != std::string::npos;
        if (!v.empty()) LOGC("fx test: %s", v.c_str());
    }
    if (!dust && !splash && !jet && !dirt && !heat) return;
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
    if (splash) {
        const double p[3] = { 1.0, -alt + 0.3, -0.6 }, vel[3] = { 0, 2.2, -1.6 };
        AtmoFX_Emit(h, 9002, p, vel, 0.35, 0.5, 3.0, 3, 0, 0.0, 0.0, 0.0);
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
