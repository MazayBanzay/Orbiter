// Extensions for our modules: the gcCore interface (the subset our modules use) and the AtmoFX exports.
// Modules find them by the module name "D3D9Client.dll": a forwarding shim of that name (shim\, loaded by this client
// from Modules\D3D11Client\) passes the lookups to D3D11Client.dll, so no module needs to change.
#include "Client.h"
#include "gcCoreAPI.h"
#include "Wake.h"

ExtState g_ext;

// ------------------------------------------------------------------------------------------ gcCore
namespace gc {

static HWND __cdecl GetRenderWindow() { return g_client ? g_client->RenderWnd() : nullptr; }

static int __cdecl SetMatrix(gcCore::MatrixId id, OBJHANDLE hVessel, DWORD mesh, DWORD group, const oapi::FMATRIX4* pMat)
{
    if (!g_client || !pMat) return -1;
    DevMesh* m = g_client->VesselMesh(hVessel, mesh);
    if (!m) return -2;
    XMFLOAT4X4 M; memcpy(&M, pMat, sizeof M);
    switch (id) {
    case gcCore::MatrixId::GROUP:
        if (group >= m->grp.size()) return -3;
        m->grp[group].xf = M;                      // the group's transform (row vectors), replacing its animation state
        return 0;
    case gcCore::MatrixId::MESH:
        for (auto& g : m->grp) g.xf = M;
        return 0;
    default: return -4;
    }
}

static int __cdecl GetMatrix(gcCore::MatrixId id, OBJHANDLE hVessel, DWORD mesh, DWORD group, oapi::FMATRIX4* pMat)
{
    if (!g_client || !pMat) return -1;
    DevMesh* m = g_client->VesselMesh(hVessel, mesh);
    if (!m) return -2;
    if (group >= m->grp.size()) return -3;
    memcpy(pMat, &m->grp[group].xf, sizeof(XMFLOAT4X4));
    return 0;
}

static bool __cdecl RegisterRenderProc(__gcRenderProc proc, DWORD id, void* param)
{
    auto& v = g_ext.renderProcs;
    if (id == RENDERPROC_DELETE) {
        v.erase(std::remove_if(v.begin(), v.end(), [&](const ExtState::RenderProc& r) { return r.proc == (void*)proc; }), v.end());
        return true;
    }
    if (id != RENDERPROC_HUD_1ST && id != RENDERPROC_HUD_2ND) return false;    // others need the 3D scene hooks (later)
    v.push_back({ (void*)proc, id, param });
    return true;
}

static int __cdecl SketchpadVersion(oapi::Sketchpad*) { return 1; }
static SURFHANDLE __cdecl LoadSurface(const char* fname, DWORD flags) { return g_client ? g_client->clbkLoadTexture(fname, flags) : nullptr; }
static gcCore::PickGround __cdecl ScanScreen(int, int) { gcCore::PickGround p; memset(&p, 0, sizeof p); return p; }
static gcCore::PickGround __cdecl GetTileData(HPLANETMGR, double, double, int) { gcCore::PickGround p; memset(&p, 0, sizeof p); return p; }
static void* __cdecl Null() { return nullptr; }
}  // namespace gc
gcIPInterface* __cdecl IpiCreate(const char* file, const char* PSEntry, const char* VSEntry, const char* ppf);   // Ipi.cpp
void __cdecl IpiRelease(gcIPInterface* p);
namespace gc {   // methods not provided yet: return 0 / NULL (all of them __cdecl, so harmless)

}  // namespace gc

DLLCLBK void gcBindCoreMethod(void** ppFnc, const char* name)
{
    static const std::map<std::string, void*> table = {
        { "GetRenderWindow", (void*)gc::GetRenderWindow },
        { "SetMatrix", (void*)gc::SetMatrix },
        { "GetMatrix", (void*)gc::GetMatrix },
        { "RegisterRenderProc", (void*)gc::RegisterRenderProc },
        { "SketchpadVersion", (void*)gc::SketchpadVersion },
        { "LoadSurface", (void*)gc::LoadSurface },
        { "ScanScreen", (void*)gc::ScanScreen },
        { "GetTileData", (void*)gc::GetTileData },
        { "CreateIPInterface", (void*)IpiCreate },
        { "ReleaseIPInterface", (void*)IpiRelease },
    };
    auto it = table.find(name);
    *ppFnc = it != table.end() ? it->second : (void*)gc::Null;
}

DLLCLBK void* gcGetCoreAPI() { return nullptr; }

// ------------------------------------------------------------------------------------------ AtmoFX (data kept for the effects)
static double Now() { return oapiGetSimTime(); }

DLLCLBK void AtmoFX_SetJet(OBJHANDLE h, int id, const double* exitPos, const double* jetDir, double thrust, double ve, double exitT, int ground, double wet, double rExit)
{
    std::lock_guard<std::mutex> lk(g_ext.mx);
    ExtState::Jet& j = g_ext.jets[{ h, id }];
    j.pos = _V(exitPos[0], exitPos[1], exitPos[2]); j.dir = _V(jetDir[0], jetDir[1], jetDir[2]);
    j.thrust = thrust; j.ve = ve; j.T = exitT; j.ground = ground; j.wet = wet; j.rExit = rExit; j.t = Now();
}

DLLCLBK void AtmoFX_SetWake(OBJHANDLE h, int id, const double* pos, double r, double depth)
{
    WakeSet(h, id, pos, r, depth);     // the liquid's wave sources (Wake.cpp)
}

DLLCLBK void AtmoFX_SetHeatSource(OBJHANDLE h, int id, double x, double y, double z, double r, double dT)
{
    std::lock_guard<std::mutex> lk(g_ext.mx);
    if (dT <= 0) { g_ext.heat.erase({ h, id }); return; }
    ExtState::Heat& s = g_ext.heat[{ h, id }];
    s.pos = _V(x, y, z); s.r = r; s.dT = dT;
}

DLLCLBK void AtmoFX_SetFoam(OBJHANDLE, int, const double*, double, double, double) {}
DLLCLBK void AtmoFX_SetDirt(OBJHANDLE, double, double, double, double, double) {}
DLLCLBK void AtmoFX_Emit(OBJHANDLE, int, const double*, const double*, double, double, double, int, int, double, double, double) {}
DLLCLBK void AtmoFX_Box(OBJHANDLE, int, const double*, const double*) {}
DLLCLBK void AtmoFX_Mark(OBJHANDLE, int, const double*, const double*, double, double, double, int, double) {}
DLLCLBK int AtmoFX_MarkCount(OBJHANDLE) { return 0; }
DLLCLBK int AtmoFX_MarkGet(OBJHANDLE, int, double*, char*, int) { return 0; }
DLLCLBK void AtmoFX_MarkPut(OBJHANDLE, const double*, const char*) {}
// AtmoFX_HudFrame, AtmoFX_HudMarker: Ar.cpp (drawn in the scene)

// the HUD render procs of the modules, on the main window's 2D layer
void D3D11Client::CallRenderProcs()
{
    if (g_ext.renderProcs.empty()) return;
    oapi::Sketchpad* skp = clbkGetSketchpad(nullptr);
    if (!skp) return;
    for (auto& r : g_ext.renderProcs) ((__gcRenderProc)r.proc)(skp, r.param);
    clbkReleaseSketchpad(skp);
}

DevMesh* D3D11Client::VesselMesh(OBJHANDLE h, DWORD idx)
{
    auto it = visuals.find(h);
    if (it == visuals.end() || idx >= it->second->mesh.size()) return nullptr;
    return it->second->mesh[idx];
}

// the forwarding shim: modules look the extensions up as "D3D9Client.dll"
void D3D11Client::LoadShim()
{
    if (GetModuleHandleA("D3D9Client.dll")) return;     // the real one is loaded: leave it alone
    HMODULE h = LoadLibraryA("Modules\\D3D11Client\\D3D9Client.dll");
    LOGC("extension shim %s", h ? "loaded (gcCore, AtmoFX)" : "NOT loaded");
}
