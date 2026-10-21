// D3D11Client: the modules' interior API (Orbitersdk\include\D3D11Lights.h) - interior volumes and area lamps per vessel.
// The table is handed out by D3D11Lights_GetApi, reached through the "D3D9Client.dll" shim. The data is kept here, read
// by Lights.cpp (the eye inside a volume, the sky light taken out, the area lamps' shading) through LightsApiData().
#include "Client.h"
#include "D3D11Lights.h"
#include "LightsApi.h"

namespace {
std::mutex g_mx;
std::map<OBJHANDLE, std::vector<D3D11Lights::Volume>> g_vol;
std::map<OBJHANDLE, std::vector<D3D11Lights::AreaLight>> g_area;
std::map<OBJHANDLE, std::map<int, bool>> g_circuit;     // a circuit's state (absent: on)
std::map<OBJHANDLE, std::vector<D3D11Lights::Opening>> g_open;
unsigned g_gen = 1;

int SetInteriorVolumes(OBJHANDLE h, const D3D11Lights::Volume* v, int n)
{
    if (!h) return 0;
    std::lock_guard<std::mutex> lk(g_mx);
    n = std::clamp(n, 0, 64);
    if (!v || !n) g_vol.erase(h); else g_vol[h].assign(v, v + n);
    g_gen++;
    LOGC("D3D11Lights: %s - %d interior volume(s)", oapiGetVesselInterface(h) ? oapiGetVesselInterface(h)->GetName() : "?", n);
    return n;
}
int SetAreaLights(OBJHANDLE h, const D3D11Lights::AreaLight* a, int n)
{
    if (!h) return 0;
    std::lock_guard<std::mutex> lk(g_mx);
    n = std::clamp(n, 0, 256);
    if (!a || !n) g_area.erase(h); else g_area[h].assign(a, a + n);
    g_gen++;
    LOGC("D3D11Lights: %s - %d area lamp(s)", oapiGetVesselInterface(h) ? oapiGetVesselInterface(h)->GetName() : "?", n);
    return n;
}
int AreaLightOn(OBJHANDLE h, int circuit, int on)
{
    if (!h) return 0;
    std::lock_guard<std::mutex> lk(g_mx);
    g_circuit[h][circuit] = on != 0;
    g_gen++;
    int k = 0;
    auto it = g_area.find(h);
    if (it != g_area.end()) for (const auto& a : it->second) if (a.circuit == circuit) k++;
    return k;
}
int SetOpenings(OBJHANDLE h, const D3D11Lights::Opening* o, int n)
{
    if (!h) return 0;
    std::lock_guard<std::mutex> lk(g_mx);
    n = std::clamp(n, 0, 64);
    if (!o || !n) g_open.erase(h); else g_open[h].assign(o, o + n);
    g_gen++;
    LOGC("D3D11Lights: %s - %d opening(s)", oapiGetVesselInterface(h) ? oapiGetVesselInterface(h)->GetName() : "?", n);
    return n;
}
const D3D11Lights::Api API = { D3D11Lights::VERSION, SetInteriorVolumes, SetAreaLights, AreaLightOn, SetOpenings };
}

DLLCLBK const D3D11Lights::Api* D3D11Lights_GetApi(int version)
{
    if (version < 1 || !g_client) return nullptr;
    LOGC("D3D11Lights: table version %d handed out (module asked %d)", D3D11Lights::VERSION, version);
    return &API;
}

// a copy of what the modules sent (the vessels still alive are checked by the caller), and its generation (changes on
// every call): Lights.cpp copies it only when the generation moved
unsigned LightsApiData(std::map<OBJHANDLE, std::vector<D3D11Lights::Volume>>* vol, std::map<OBJHANDLE, std::vector<D3D11Lights::AreaLight>>* area,
                       std::map<OBJHANDLE, std::map<int, bool>>* circuits, std::map<OBJHANDLE, std::vector<D3D11Lights::Opening>>* openings)
{
    std::lock_guard<std::mutex> lk(g_mx);
    if (openings) *openings = g_open;
    if (vol) *vol = g_vol;
    if (area) *area = g_area;
    if (circuits) *circuits = g_circuit;
    return g_gen;
}
void LightsApiForget(OBJHANDLE h)
{
    std::lock_guard<std::mutex> lk(g_mx);
    g_vol.erase(h); g_area.erase(h); g_circuit.erase(h); g_open.erase(h); g_gen++;
}
