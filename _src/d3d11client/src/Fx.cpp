// Effects hooks (see Fx.h): they forward to one FxSystem (FxSystem.h), which owns the effects - the atmosphere
// (FxAtmo.cpp), later the waves on liquids and the spray and mist from jets.
#include "Fx.h"
#include "FxSystem.h"

ComPtr<ID3DBlob> FxCompile(const char* file, const char* entry, const char* target)
{
    std::string path = std::string("Modules\\D3D11Client\\") + file;
    std::wstring wp(path.begin(), path.end());
    ComPtr<ID3DBlob> code, err;
    HRESULT hr = CachedCompileFromFile(wp.c_str(), nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, &code, &err);
    if (err) LOGC("shader %s %s: %s", path.c_str(), entry, (const char*)err->GetBufferPointer());
    if (FAILED(hr)) return nullptr;
    return code;
}

// ------------------------------------------------------------------------------------------ FxSystem
// the effects' switches: Config\D3D11Client\Fx.cfg (Atmosphere = 0/1, Waves = 0/1; missing file or key: on)
static void ReadSwitches(bool& atmo, bool& waves)
{
    FILE* f = fopen("Config\\D3D11Client\\Fx.cfg", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* sc = strchr(line, ';'); if (sc) *sc = 0;
        char key[64]; int v;
        if (sscanf(line, " %63[A-Za-z0-9] = %d", key, &v) != 2) continue;
        if (!strcmp(key, "Atmosphere")) atmo = v != 0;
        else if (!strcmp(key, "Waves")) waves = v != 0;
    }
    fclose(f);
}

void FxSystem::Init(D3D11Client* gc)
{
    bool useAtmo = true, useWaves = true;
    ReadSwitches(useAtmo, useWaves);
    atmoOk = useAtmo && atmo.Init(gc);
    wavesOk = useWaves && waves.Init(gc);
    LOGC("effects: atmosphere %s, waves %s (Config\\D3D11Client\\Fx.cfg)", !useAtmo ? "off" : atmoOk ? "ready" : "FAILED",
         !useWaves ? "off" : wavesOk ? "ready" : "FAILED");
}

void FxSystem::Frame(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3&, const XMMATRIX&, double simDt)
{
    if (atmoOk) atmo.Frame(gc, camPos);     // the tables (once per planet), this frame's constants, the light for the client
    if (wavesOk) waves.Frame(gc, camPos, simDt);   // the wave field on a liquid near the camera
}

void FxSystem::RenderTransparent(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj)
{
    if (atmoOk) atmo.Render(gc, camPos, camRot, proj);
}

void FxSystem::Release()
{
    atmo.Release();
    waves.Release();
    atmoOk = wavesOk = false;
}

// ------------------------------------------------------------------------------------------ the client's hooks
static FxSystem fx;

void FxInit(D3D11Client* gc) { fx.Init(gc); }
void FxFrame(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj, double simDt) { fx.Frame(gc, camPos, camRot, proj, simDt); }
void FxRenderTransparent(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj) { fx.RenderTransparent(gc, camPos, camRot, proj); }
void FxRelease() { fx.Release(); }

const FxWaveView& FxWaves() { return fx.Waves().View(); }

void FxBindLight(D3D11Client* gc) { fx.BindLight(gc); }
