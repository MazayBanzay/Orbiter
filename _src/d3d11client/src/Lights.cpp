// D3D11Client: local lights - the vessels' point and spot lights (Orbiter LightEmitter: headlights, helmet lamps, the
// cockpits' lamps) and beacons (BEACONLIGHTSPEC). Per frame (LightsFrame, after the Sun's shadows): the lights gathered
// from every visual, turned into an intensity that falls as 1/d^2 (shaders\lights.hlsli has the photometry), sorted by
// what they matter, put into a clustered list over the view (16 x 9 tiles x 24 slices, built here on the CPU), and the
// ones that matter most get shadow maps in one 4096 atlas (16-bit): 4 spots tiles of 1024, 12 more spots and 6 point
// lights' cubes in tiles of 512. A map is kept from frame to frame and drawn again only when its lamp or what stands in
// its reach moved (a base's lamps: once), at most 12 tiles a frame (the vessels as casters).
// mesh.hlsl and planet.hlsl add their light (b9, t17..t20 - LightsBind); after the air (LightsRenderAir) the beams in
// the air (lights_vol.hlsl PSBeams) and the lenses and beacons (VSSprite/PSSprite) are added to the scene.
// Client.cfg: LocalLights 0/1, LightLux (lux per Orbiter light unit, 10), LightShadows 0/1, LightBeams 0/1, Beacons 0/1.
#include "Client.h"
#include "Fx.h"
#include "LightsApi.h"
#include <algorithm>

namespace {
struct LightG { XMFLOAT4 p, c, d, a, e; };
struct LightsCB { XMFLOAT4X4 lvp; XMFLOAT4 grid, z; XMFLOAT4X4 skyVP[12]; XMFLOAT4 skyDir[12]; XMFLOAT4 skyC, skyK; XMFLOAT4 volC, volX, volY, volZ, volK; XMFLOAT4 room[32]; XMFLOAT4 open[32]; };
struct SpriteG { XMFLOAT4 p, c, d, e; };
struct SpriteCB { XMFLOAT4X4 vp; XMFLOAT4 view, right, up, fwd; };
constexpr int TX = 16, TY = 9, TZ = 24, NCL = TX * TY * TZ;
constexpr int SKY_N = 256, NSKY = 12;
constexpr int MAXL = 256, MAXIDX = 65536, MAXSPR = 256, MAXBEAM = 8;
constexpr int ATLAS = 4096, UNIT = 512, NBIG = 4, NSMALL = 48, MAXSPOTSH = 16, MAXCUBE = 6, MAXREC = 64, SHADOW_BUDGET = 12;
struct ShadowRecG { XMFLOAT4X4 vp; XMFLOAT4 rect, k, ax, ay; };
constexpr float Z0 = 0.5f, ZF = 5000.0f;
constexpr double SUN_LUX_1AU = 128000.0;     // the Sun's illuminance at 1 AU (AU: OrbiterAPI.h)

struct Cfg { bool loaded = false, on = true, shadows = true, beams = true, beacons = true, skyVis = true; double lux = 10, skyR = 10; } cfg;
void LoadCfg()
{
    cfg.loaded = true;
    FILE* f = fopen("Config\\D3D11Client\\Client.cfg", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* sc = strchr(line, ';'); if (sc) *sc = 0;
        char key[64]; double v;
        if (sscanf(line, " %63[A-Za-z] = %lf", key, &v) != 2) continue;
        std::string k = key;
        if (k == "LocalLights") cfg.on = v != 0;
        else if (k == "LightLux") cfg.lux = std::max(v, 0.0);
        else if (k == "LightShadows") cfg.shadows = v != 0;
        else if (k == "LightBeams") cfg.beams = v != 0;
        else if (k == "Beacons") cfg.beacons = v != 0;
        else if (k == "SkyOcclusion") cfg.skyVis = v != 0;
        else if (k == "SkyOcclusionRadius") cfg.skyR = std::clamp(v, 2.0, 60.0);
    }
    fclose(f);
}

ID3D11Device* g_dev = nullptr;
bool g_ok = false;
ComPtr<ID3D11Buffer> g_cb, g_sprCB, g_light, g_cl, g_idx, g_spr;
ComPtr<ID3D11ShaderResourceView> g_lightSRV, g_clSRV, g_idxSRV, g_sprSRV;
ComPtr<ID3D11Texture2D> g_spotTex; ComPtr<ID3D11DepthStencilView> g_spotDSV; ComPtr<ID3D11ShaderResourceView> g_spotSRV;
ComPtr<ID3D11Texture2D> g_skyTex; ComPtr<ID3D11DepthStencilView> g_skyDSV; ComPtr<ID3D11ShaderResourceView> g_skySRV;
ComPtr<ID3D11VertexShader> g_vsFull, g_vsSpr; ComPtr<ID3D11PixelShader> g_psBeams, g_psSpr;
ComPtr<ID3D11Buffer> g_beamCB; ComPtr<ID3D11RasterizerState> g_rsScissor;
// the beams at half resolution (1.2 ms at full with MSAA 8x, the master's measure 20.10.2026), added back bilinearly
ComPtr<ID3D11Texture2D> g_halfTex; ComPtr<ID3D11RenderTargetView> g_halfRTV; ComPtr<ID3D11ShaderResourceView> g_halfSRV; ComPtr<ID3D11PixelShader> g_psUp; UINT g_halfW = 0, g_halfH = 0;
std::vector<D3D11_RECT> g_beamRect;
// the eye's probe: 16 x 9 visible points (lights_vol.hlsl CSProbe), read back two frames later through a ring of 3
ComPtr<ID3D11ComputeShader> g_csProbe; ComPtr<ID3D11Buffer> g_probe, g_probeSt[3]; ComPtr<ID3D11UnorderedAccessView> g_probeUAV;
UINT g_probeN = 0; XMFLOAT4 g_probePts[144] = {}; bool g_probeOk = false;     // each beam's light: its cone's box on the screen
int g_nLights = 0, g_nBeams = 0, g_nSpr = 0, g_nSpot = 0, g_nCube = 0, g_nDrawn = 0;
UINT g_frame = 0;
// the sky maps, kept from frame to frame: each its eye and axes in the planet frame (its matrix rebuilt every frame for the
// camera of now), three of the twelve drawn again each frame (the master's measure 20.10.2026: 16 views of every vessel a
// frame cost up to 2.8 ms)
struct SkyMap { VECTOR3 eyeL, dL, vL, cellL; uint64_t sig = 0; bool valid = false; };
SkyMap g_skyMap[12]; int g_skyNext = 0; VECTOR3 g_skyCentreG = { 0, 0, 0 }; OBJHANDLE g_skyPlanet = nullptr;
ComPtr<ID3D11VertexShader> g_vsClearZ; ComPtr<ID3D11DepthStencilState> g_dsAlways;
double g_eLoc = 0; VECTOR3 g_eCol = { 1, 1, 1 };
double g_lumView = 0, g_lumP95 = 0; bool g_lumOk = false; bool g_airless = false;   // the body nearest the camera has no atmosphere      // the view's log-mean luminance (the probe's visible points, centre-weighted)      // the local lights' illuminance where they light the view [Sun = 1], its colour

ComPtr<ID3D11Buffer> MakeStructured(ID3D11Device* dev, UINT stride, UINT n, ComPtr<ID3D11ShaderResourceView>& srv)
{
    ComPtr<ID3D11Buffer> b;
    D3D11_BUFFER_DESC bd = { stride * n, D3D11_USAGE_DYNAMIC, D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_WRITE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, stride };
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &b))) return nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = DXGI_FORMAT_UNKNOWN; sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sd.Buffer.NumElements = n;
    dev->CreateShaderResourceView(b.Get(), &sd, &srv);
    return b;
}
template<class T> void Upload(ID3D11DeviceContext* ctx, ID3D11Buffer* b, const T* data, size_t n)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (!b || FAILED(ctx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    if (n) memcpy(m.pData, data, n * sizeof(T));
    ctx->Unmap(b, 0);
}
double SrgbToLin(double c) { c = std::clamp(c, 0.0, 1.0); return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
OBJHANDLE TheSun() { for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) return h; } return nullptr; }

struct Cand {
    LightG g = {}; double imp, Icd; VECTOR3 pos, dir, lp; double range, cosOut; bool spot; OBJHANDLE h; const void* key = nullptr; bool rect = false; int room = -1;
    // the shadow's tile, the beam
    int tile = -1; bool beam = false;
};
std::vector<Cand> g_cand;
// the modules' interior data (LightsApi.cpp), copied when it changes; the interior volume the camera is in
std::map<OBJHANDLE, std::vector<D3D11Lights::Volume>> g_apiVol;
std::map<OBJHANDLE, std::vector<D3D11Lights::AreaLight>> g_apiArea;
std::map<OBJHANDLE, std::map<int, bool>> g_apiCirc;
std::map<OBJHANDLE, std::vector<D3D11Lights::Opening>> g_apiOpen;
unsigned g_apiGen = 0;
bool g_inVol = false; double g_volEvMin = 0, g_volEvMax = 20, g_Esun = 127000;
// the lamps' shadow slots, kept from frame to frame: a lamp keeps its tiles while it is chosen; its map's eye and axes in the
// reference planet's frame (its matrix rebuilt for the camera of now), the signature of what it was drawn with
struct ShadowSlot {
    const void* key = nullptr; int kind = 0;          // 0 a big spot tile, 1 a small spot tile, 2 a cube (6 small tiles)
    int tiles[6] = { -1, -1, -1, -1, -1, -1 };
    bool valid = false, seen = false, hadDyn = false; uint64_t sig = 0;     // sig: the static casters' (and the lamp's)
    VECTOR3 eyeL = { 0, 0, 0 }, fwdL = { 0, 0, 1 }, upL = { 0, 1, 0 }; double fov = 1, nearZ = 0.12, farZ = 10;
};
// the static casters' maps (the building, the vehicles at rest) kept in their own atlas; each frame a tile with people in
// reach is the static one copied (a depth-writing pass) and the people drawn over it - as engines cache static shadows
ComPtr<ID3D11Texture2D> g_statTex; ComPtr<ID3D11DepthStencilView> g_statDSV; ComPtr<ID3D11ShaderResourceView> g_statSRV;
ComPtr<ID3D11PixelShader> g_psCopyZ;
std::vector<ShadowSlot> g_slots; OBJHANDLE g_shRef = nullptr;
ComPtr<ID3D11Buffer> g_rec; ComPtr<ID3D11ShaderResourceView> g_recSRV;
inline void Fnv64(uint64_t& h, int64_t v) { for (int i = 0; i < 8; i++) { h ^= (uint64_t)((v >> (i * 8)) & 0xFF); h *= 1099511628211ull; } }
std::vector<SpriteG> g_sprites;
std::vector<uint32_t> g_clusterData;     // (offset, count) per cluster
std::vector<uint32_t> g_index;
}

// ------------------------------------------------------------------------------------------ the frame's lights
void D3D11Client::LightsFrame(const VECTOR3& cp)
{
    if (!cfg.loaded) LoadCfg();
    g_nLights = g_nBeams = g_nSpr = g_nSpot = g_nCube = g_nDrawn = 0; g_eLoc = 0;
    g_frame++;
    if (g_dev != dev.Get()) {
        g_dev = dev.Get(); g_ok = false;
        g_cb.Reset(); g_sprCB.Reset(); g_light.Reset(); g_cl.Reset(); g_idx.Reset(); g_spr.Reset();
        g_spotTex.Reset(); g_spotDSV.Reset(); g_spotSRV.Reset(); g_skyTex.Reset(); g_skyDSV.Reset(); g_skySRV.Reset();
        D3D11_BUFFER_DESC cd = { (sizeof(LightsCB) + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&cd, nullptr, &g_cb);
        D3D11_BUFFER_DESC sd = { (sizeof(SpriteCB) + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&sd, nullptr, &g_sprCB);
        g_light = MakeStructured(dev.Get(), sizeof(LightG), MAXL, g_lightSRV);
        g_rec = MakeStructured(dev.Get(), sizeof(ShadowRecG), MAXREC, g_recSRV);
        g_statTex.Reset(); g_statDSV.Reset(); g_statSRV.Reset();
        {
            D3D11_TEXTURE2D_DESC st2 = { ATLAS, ATLAS, 1, 1, DXGI_FORMAT_R16_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
            if (SUCCEEDED(dev->CreateTexture2D(&st2, nullptr, &g_statTex))) {
                D3D11_DEPTH_STENCIL_VIEW_DESC dv = {}; dv.Format = DXGI_FORMAT_D16_UNORM; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                dev->CreateDepthStencilView(g_statTex.Get(), &dv, &g_statDSV);
                D3D11_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.Format = DXGI_FORMAT_R16_UNORM; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
                dev->CreateShaderResourceView(g_statTex.Get(), &sv, &g_statSRV);
            }
            auto bcz = CompileShaderFile("lights_vol.hlsl", "PSCopyZ", "ps_5_0");
            if (bcz) dev->CreatePixelShader(bcz->GetBufferPointer(), bcz->GetBufferSize(), nullptr, &g_psCopyZ);
        }
        g_slots.clear(); g_shRef = nullptr;
        g_cl = MakeStructured(dev.Get(), 8, NCL, g_clSRV);
        g_idx = MakeStructured(dev.Get(), 4, MAXIDX, g_idxSRV);
        g_spr = MakeStructured(dev.Get(), sizeof(SpriteG), MAXSPR, g_sprSRV);
        D3D11_TEXTURE2D_DESC st = { ATLAS, ATLAS, 1, 1, DXGI_FORMAT_R16_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (SUCCEEDED(dev->CreateTexture2D(&st, nullptr, &g_spotTex))) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dv = {}; dv.Format = DXGI_FORMAT_D16_UNORM; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            dev->CreateDepthStencilView(g_spotTex.Get(), &dv, &g_spotDSV);
            D3D11_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.Format = DXGI_FORMAT_R16_UNORM; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
            dev->CreateShaderResourceView(g_spotTex.Get(), &sv, &g_spotSRV);
        }
        D3D11_TEXTURE2D_DESC kt = { 4 * SKY_N, 4 * SKY_N, 1, 1, DXGI_FORMAT_R32_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (SUCCEEDED(dev->CreateTexture2D(&kt, nullptr, &g_skyTex))) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dv = {}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            dev->CreateDepthStencilView(g_skyTex.Get(), &dv, &g_skyDSV);
            D3D11_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
            dev->CreateShaderResourceView(g_skyTex.Get(), &sv, &g_skySRV);
        }
        auto bv = CompileShaderFile("lights_vol.hlsl", "VS", "vs_5_0"), bb = CompileShaderFile("lights_vol.hlsl", "PSBeams", "ps_5_0");
        auto bsv = CompileShaderFile("lights_vol.hlsl", "VSSprite", "vs_5_0"), bsp = CompileShaderFile("lights_vol.hlsl", "PSSprite", "ps_5_0");
        if (bv) dev->CreateVertexShader(bv->GetBufferPointer(), bv->GetBufferSize(), nullptr, &g_vsFull);
        if (bb) dev->CreatePixelShader(bb->GetBufferPointer(), bb->GetBufferSize(), nullptr, &g_psBeams);
        if (bsv) dev->CreateVertexShader(bsv->GetBufferPointer(), bsv->GetBufferSize(), nullptr, &g_vsSpr);
        if (bsp) dev->CreatePixelShader(bsp->GetBufferPointer(), bsp->GetBufferSize(), nullptr, &g_psSpr);
        { auto bz = CompileShaderFile("lights_vol.hlsl", "VSClearZ", "vs_5_0"); if (bz) dev->CreateVertexShader(bz->GetBufferPointer(), bz->GetBufferSize(), nullptr, &g_vsClearZ);
          D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_ALWAYS; dev->CreateDepthStencilState(&dd, &g_dsAlways);
          for (auto& m : g_skyMap) m.valid = false; }
        { auto bu = CompileShaderFile("lights_vol.hlsl", "PSUpsample", "ps_5_0"); if (bu) dev->CreatePixelShader(bu->GetBufferPointer(), bu->GetBufferSize(), nullptr, &g_psUp); }
        g_halfTex.Reset(); g_halfRTV.Reset(); g_halfSRV.Reset(); g_halfW = g_halfH = 0;
        D3D11_BUFFER_DESC bc = { 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&bc, nullptr, &g_beamCB);
        D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; rd.ScissorEnable = TRUE;
        dev->CreateRasterizerState(&rd, &g_rsScissor);
        {
            auto bc2 = CompileShaderFile("lights_vol.hlsl", "CSProbe", "cs_5_0");
            if (bc2) dev->CreateComputeShader(bc2->GetBufferPointer(), bc2->GetBufferSize(), nullptr, &g_csProbe);
            D3D11_BUFFER_DESC pb = { 144 * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16 };
            dev->CreateBuffer(&pb, nullptr, &g_probe);
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_UNKNOWN; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = 144;
            if (g_probe) dev->CreateUnorderedAccessView(g_probe.Get(), &ud, &g_probeUAV);
            D3D11_BUFFER_DESC sb = { 144 * 16, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0 };
            for (auto& b : g_probeSt) dev->CreateBuffer(&sb, nullptr, &b);
            g_probeN = 0; g_probeOk = false;
        }
        g_ok = g_cb && g_light && g_cl && g_idx && g_spotSRV;
        LOGC("local lights: %s (lux per unit %.1f, spot shadows %s, beams %s, beacons %s)", g_ok ? (cfg.on ? "on" : "off (LocalLights = 0)") : "NOT available",
             cfg.lux, cfg.shadows ? "on" : "off", cfg.beams && g_psBeams ? "on" : "off", cfg.beacons && g_psSpr ? "on" : "off");
    }
    LightsCB cb = {};
    XMStoreFloat4x4(&cb.lvp, XMMatrixTranspose(XMLoadFloat4x4(&curVPNoJit)));
    cb.grid = { (float)TX, (float)TY, (float)TZ, 0 };
    cb.z = { Z0, (float)((TZ - 1) / log(ZF / Z0)), 0, (float)(g_frame & 63) };
    if (!g_ok || !cfg.on) { Upload(ctx.Get(), g_cb.Get(), &cb, 1); return; }

    // ---- gather
    const OBJHANDLE hs = TheSun();
    VECTOR3 sp = _V(0, 0, 0); if (hs) oapiGetGlobalPos(hs, &sp);
    const double dsun = std::max(length(sp - cp), 1e9);
    const double Esun = SUN_LUX_1AU * (AU / dsun) * (AU / dsun);      // the Sun's illuminance here [lux]: the client's unit
    const OBJHANDLE own = oapiCameraInternal() ? oapiGetFocusObject() : nullptr;
    {   // airless here: the nearest body has no atmosphere (g_fxAtmo.active is on wherever the FX pass runs, the Moon too)
        OBJHANDLE hn = nullptr; double bn = 1e300;
        for (DWORD i = 0; i < oapiGetGbodyCount(); i++) {
            OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) != OBJTP_PLANET) continue;
            VECTOR3 q; oapiGetGlobalPos(h, &q); const double d = (length(cp - q) - oapiGetSize(h)) / oapiGetSize(h);
            if (d < bn) { bn = d; hn = h; }
        }
        g_airless = hn && !oapiPlanetHasAtmosphere(hn);
    }
    MATRIX3 cr; oapiCameraRotationMatrix(&cr);
    const VECTOR3 cfwd = _V(cr.m13, cr.m23, cr.m33);
    const double simt = oapiGetSimTime();
    g_cand.clear(); g_sprites.clear();
    for (auto& kv : visuals) {
        VESSEL* v = kv.second->v;
        if (!v) continue;
        const bool inside = kv.first == own;
        VECTOR3 gp; v->GetGlobalPos(gp);
        MATRIX3 R; v->GetRotationMatrix(R);
        const DWORD n = v->LightEmitterCount();
        for (DWORD i = 0; i < n; i++) {
            const LightEmitter* le = v->GetLightEmitter(i);
            if (!le || !le->IsActive()) continue;
            const LightEmitter::TYPE ty = le->GetType();
            if (ty != LightEmitter::LT_POINT && ty != LightEmitter::LT_SPOT) continue;
            const int vis = (int)le->GetVisibility();
            // outside the camera's vessel: its outside lights. Inside it: all its lights - the outside ones light the world
            // through the visor or the windows (not the helmet or the cabin: flag 2), the cockpit ones the cockpit (flag 4)
            if (!inside && !(vis & LightEmitter::VIS_EXTERNAL)) continue;
            const int own = inside ? ((vis & LightEmitter::VIS_COCKPIT) ? ((vis & LightEmitter::VIS_EXTERNAL) ? 0 : 4) : 2) : 0;
            const double I = le->GetIntensity();
            if (!(I > 0)) continue;
            const PointLight* pl = static_cast<const PointLight*>(le);
            const double range = pl->GetRange();
            const double* at = pl->GetAttenuation();
            if (!(range > 0.05) || !at) continue;
            // one luminous intensity from the module's curve: its illuminance at a quarter of the range, x d^2
            const double dref = range * 0.25, den = at[0] + at[1] * dref + at[2] * dref * dref;
            if (!(den > 0)) continue;
            const double Icd = cfg.lux * dref * dref / den * I;      // [cd]
            const COLOUR4& cd = le->GetDiffuseColour();
            const double cr0 = SrgbToLin(cd.r), cg0 = SrgbToLin(cd.g), cb0 = SrgbToLin(cd.b);
            if (cr0 + cg0 + cb0 <= 0) continue;
            const bool attached = le->GetObjectHandle() != nullptr;
            const VECTOR3 lp = le->GetPosition();
            Cand c; c.key = le;
            c.pos = attached ? (gp + mul(R, lp) - cp) : (lp - cp);
            c.spot = ty == LightEmitter::LT_SPOT;
            c.dir = _V(0, 0, 1);
            c.cosOut = -1;
            double cosIn = -1;
            if (c.spot) {
                VECTOR3 d = le->GetDirection();
                if (attached) d = mul(R, d);
                const double dl = length(d); if (dl < 1e-9) continue;
                c.dir = d / dl;
                const SpotLight* sl = static_cast<const SpotLight*>(le);
                const double pen = std::clamp(sl->GetPenumbra(), 1e-3, PI * 0.999), umb = std::clamp(sl->GetUmbra(), 0.0, pen);
                c.cosOut = cos(pen * 0.5); cosIn = std::max(cos(umb * 0.5), c.cosOut + 1e-4);
            }
            c.range = range; c.h = kv.first; c.Icd = Icd; c.lp = lp;
            const double dist = length(c.pos);
            if (dist - range > 30000) continue;
            const double I0 = Icd / Esun;                             // the illuminance at 1 m [Sun = 1]
            const double lampR = c.spot ? std::clamp(0.0003 * sqrt(Icd), 0.01, 0.15) : 0.05;
            c.g.p = { (float)c.pos.x, (float)c.pos.y, (float)c.pos.z, (float)range };
            c.g.c = { (float)(I0 * cr0), (float)(I0 * cg0), (float)(I0 * cb0), (float)((c.spot ? 1 : 0) | own) };
            c.g.d = { (float)c.dir.x, (float)c.dir.y, (float)c.dir.z, (float)c.cosOut };
            c.g.a = { (float)cosIn, (float)lampR, -1, 0 };
            // what it matters: its light where the eye is, roughly (the near ones win, then the strong far ones)
            const double reach = std::max(dist - range * 0.5, 1.0);
            c.imp = I0 * (cr0 + cg0 + cb0) * range * range / (reach * reach);
            g_cand.push_back(c);
        }
        // the beacons: drawn as they are (outside views)
        if (cfg.beacons && !inside) {
            for (DWORD i = 0;; i++) {
                const BEACONLIGHTSPEC* b = v->GetBeacon(i);
                if (!b) break;
                if (!b->active || !b->pos || !b->col || g_sprites.size() >= MAXSPR) continue;
                double on = 1;
                if (b->period > 0) { const double ph = fmod(simt + b->tofs, b->period); on = ph < b->duration ? 1 : 0; }
                if (on <= 0) continue;
                const VECTOR3 p = gp + mul(R, *b->pos) - cp;
                if (dotp(p, cfwd) < 0.05) continue;
                SpriteG s;
                // a beacon on a spot lamp (the suit's lens glow) shows only to the eye in front of the lamp: its axis
                VECTOR3 bd = _V(0, 0, 0); double bc = -2;
                for (DWORD j = 0; j < n; j++) {
                    const LightEmitter* le = v->GetLightEmitter(j);
                    if (!le || le->GetType() != LightEmitter::LT_SPOT || !le->GetObjectHandle()) continue;
                    if (length(le->GetPosition() - *b->pos) > 0.1) continue;
                    bd = mul(R, le->GetDirection()); const double l = length(bd); if (l > 0) bd = bd / l;
                    bc = cos(static_cast<const SpotLight*>(le)->GetPenumbra() * 0.5); break;
                }
                s.p = { (float)p.x, (float)p.y, (float)p.z, (float)std::max(b->size, 0.005) };
                s.c = { (float)SrgbToLin(b->col->x), (float)SrgbToLin(b->col->y), (float)SrgbToLin(b->col->z), (float)(1 + std::min<DWORD>(b->shape, 2)) };
                s.d = { (float)on, (float)bd.x, (float)bd.y, (float)bd.z }; s.e = { (float)bc, 0, 0, 0 };
                g_sprites.push_back(s);
            }
        }
    }
    // ---- the modules' rectangle lamps (D3D11Lights.h) and the interior volume the camera is in
    g_Esun = Esun;
    {
        const unsigned gen = LightsApiData(nullptr, nullptr, nullptr);
        if (gen != g_apiGen) { g_apiGen = LightsApiData(&g_apiVol, &g_apiArea, &g_apiCirc, &g_apiOpen); }
    }
    // the rooms the panels shine in (Volume.room = AreaLight.circuit): up to 8, the nearest to the camera first
    struct RoomK { OBJHANDLE h; int room; int slot; };
    std::vector<RoomK> rooms;
    {
        struct RC { double d; OBJHANDLE h; const D3D11Lights::Volume* v; };
        std::vector<RC> rc;
        for (auto& kv : g_apiVol) {
            VESSEL* v = oapiGetVesselInterface(kv.first); if (!v) continue;
            VECTOR3 gp; v->GetGlobalPos(gp); MATRIX3 R; v->GetRotationMatrix(R);
            for (const D3D11Lights::Volume& V : kv.second) {
                if (V.room <= 0) continue;
                const VECTOR3 cL = _V((V.box[0] + V.box[3]) / 2, (V.box[1] + V.box[4]) / 2, (V.box[2] + V.box[5]) / 2);
                rc.push_back({ length(gp + mul(R, cL) - cp), kv.first, &V });
            }
        }
        std::sort(rc.begin(), rc.end(), [](const RC& a, const RC& b) { return a.d < b.d; });
        for (size_t i = 0; i < rc.size() && rooms.size() < 8; i++) {
            VESSEL* v = oapiGetVesselInterface(rc[i].h);
            VECTOR3 gp; v->GetGlobalPos(gp); MATRIX3 R; v->GetRotationMatrix(R);
            const D3D11Lights::Volume& V = *rc[i].v;
            const double x0 = std::min(V.box[0], V.box[3]), x1 = std::max(V.box[0], V.box[3]);
            const double y0 = std::min(V.box[1], V.box[4]), y1 = std::max(V.box[1], V.box[4]);
            const double z0 = std::min(V.box[2], V.box[5]), z1 = std::max(V.box[2], V.box[5]);
            const VECTOR3 cG = gp + mul(R, _V((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)) - cp;
            const VECTOR3 ax = mul(R, _V(1, 0, 0)), ay = mul(R, _V(0, 1, 0)), az = mul(R, _V(0, 0, 1));
            const int k = (int)rooms.size();
            cb.room[k * 4 + 0] = { (float)cG.x, (float)cG.y, (float)cG.z, std::max(V.blend, 0.05f) };
            cb.room[k * 4 + 1] = { (float)ax.x, (float)ax.y, (float)ax.z, (float)((x1 - x0) / 2) };
            cb.room[k * 4 + 2] = { (float)ay.x, (float)ay.y, (float)ay.z, (float)((y1 - y0) / 2) };
            cb.room[k * 4 + 3] = { (float)az.x, (float)az.y, (float)az.z, (float)((z1 - z0) / 2) };
            rooms.push_back({ rc[i].h, V.room, k });
        }
    }
    for (auto& kv : g_apiArea) {
        if (!visuals.count(kv.first)) continue;
        VESSEL* v = oapiGetVesselInterface(kv.first); if (!v) continue;
        VECTOR3 gp; v->GetGlobalPos(gp); MATRIX3 R; v->GetRotationMatrix(R);
        const auto circ = g_apiCirc.find(kv.first);
        for (size_t ai = 0; ai < kv.second.size(); ai++) {
            const D3D11Lights::AreaLight& a = kv.second[ai];
            if (circ != g_apiCirc.end()) { auto it = circ->second.find(a.circuit); if (it != circ->second.end() && !it->second) continue; }
            const VECTOR3 ex = mul(R, _V(a.ex[0], a.ex[1], a.ex[2])), ey = mul(R, _V(a.ey[0], a.ey[1], a.ey[2]));
            const double area = 4 * length(ex) * length(ey);
            if (!(area > 1e-6) || !(a.lumen > 0)) continue;
            const double lum = 0.2126 * a.rgb[0] + 0.7152 * a.rgb[1] + 0.0722 * a.rgb[2];
            if (!(lum > 0)) continue;
            Cand c; c.rect = true; c.spot = false; c.key = &a; c.h = kv.first;
            c.pos = gp + mul(R, _V(a.c[0], a.c[1], a.c[2])) - cp;
            c.dir = crossp(ex, ey); c.dir = c.dir / std::max(length(c.dir), 1e-9);
            c.lp = _V(a.c[0], a.c[1], a.c[2]);
            // its radiance (Frostbite 4.7: L = flux / (pi A)) in the client's units: pi L / the Sun's illuminance = flux / (A Esun)
            const double Lc = a.lumen / (area * Esun);
            c.range = sqrt(a.lumen / (3.14159265 * 2.0));     // where its light falls under ~2 lx (a room's own lamps light it; the eye adapts to ~100 lx)
            c.cosOut = -1; c.Icd = a.lumen / 3.14159265;
            c.g.p = { (float)c.pos.x, (float)c.pos.y, (float)c.pos.z, (float)c.range };
            c.g.c = { (float)(Lc * a.rgb[0] / lum), (float)(Lc * a.rgb[1] / lum), (float)(Lc * a.rgb[2] / lum), (float)(8 | (a.twoSided ? 16 : 0)) };
            c.g.d = { (float)ex.x, (float)ex.y, (float)ex.z, 0 };
            int slot = -1;
            for (const RoomK& rk : rooms) if (rk.h == kv.first && rk.room == a.circuit) { slot = rk.slot; break; }
            c.g.a = { (float)slot, 0.05f, -1, 0 };
            c.g.e = { (float)ey.x, (float)ey.y, (float)ey.z, (float)area };
            c.room = slot;
            const double dist = length(c.pos), reach = std::max(dist - c.range * 0.5, 1.0);
            c.imp = (a.lumen / (3.14159265 * Esun)) * c.range * c.range / (reach * reach);
            g_cand.push_back(c);
            // its face drawn with its radiance (pre-exposed, blooming)
            if (g_sprites.size() < MAXSPR) {
                SpriteG sp;
                sp.p = { (float)c.pos.x, (float)c.pos.y, (float)c.pos.z, 0 };
                sp.c = { c.g.c.x, c.g.c.y, c.g.c.z, 4 };
                sp.d = { (float)ex.x, (float)ex.y, (float)ex.z, 0 };
                sp.e = { (float)ey.x, (float)ey.y, (float)ey.z, a.twoSided ? 1.0f : 0.0f };
                g_sprites.push_back(sp);
            }
        }
    }
    g_inVol = false;
    for (auto& kv : g_apiVol) {
        if (g_inVol) break;
        VESSEL* v = oapiGetVesselInterface(kv.first); if (!v) continue;
        VECTOR3 gp; v->GetGlobalPos(gp); MATRIX3 R; v->GetRotationMatrix(R);
        const VECTOR3 lc = tmul(R, cp - gp);
        for (const D3D11Lights::Volume& V : kv.second) {
            const double x0 = std::min(V.box[0], V.box[3]), x1 = std::max(V.box[0], V.box[3]);
            const double y0 = std::min(V.box[1], V.box[4]), y1 = std::max(V.box[1], V.box[4]);
            const double z0 = std::min(V.box[2], V.box[5]), z1 = std::max(V.box[2], V.box[5]);
            if (lc.x < x0 || lc.x > x1 || lc.y < y0 || lc.y > y1 || lc.z < z0 || lc.z > z1) continue;
            g_inVol = true; g_volEvMin = std::min(V.evMin, V.evMax); g_volEvMax = std::max(V.evMin, V.evMax);
            const VECTOR3 cL = _V((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), cG = gp + mul(R, cL) - cp;
            const VECTOR3 ax = mul(R, _V(1, 0, 0)), ay = mul(R, _V(0, 1, 0)), az = mul(R, _V(0, 0, 1));
            cb.volC = { (float)cG.x, (float)cG.y, (float)cG.z, 1 };
            cb.volX = { (float)ax.x, (float)ax.y, (float)ax.z, (float)((x1 - x0) / 2) };
            cb.volY = { (float)ay.x, (float)ay.y, (float)ay.z, (float)((y1 - y0) / 2) };
            cb.volZ = { (float)az.x, (float)az.y, (float)az.z, (float)((z1 - z0) / 2) };
            cb.volK = { std::max(V.blend, 0.05f), 0, 0, 0 };
            // the openings the Sun may come in by (this vessel's, the nearest 8)
            auto io = g_apiOpen.find(kv.first);
            if (io != g_apiOpen.end()) {
                std::vector<std::pair<double, const D3D11Lights::Opening*>> od;
                for (const auto& o : io->second) {
                    const VECTOR3 oc = _V((o.box[0] + o.box[3]) / 2, (o.box[1] + o.box[4]) / 2, (o.box[2] + o.box[5]) / 2);
                    od.push_back({ length(oc - lc), &o });
                }
                std::sort(od.begin(), od.end(), [](auto& a, auto& b) { return a.first < b.first; });
                int no = 0;
                for (auto& e : od) {
                    if (no >= 8) break;
                    const D3D11Lights::Opening& o = *e.second;
                    const double ox0 = std::min(o.box[0], o.box[3]), ox1 = std::max(o.box[0], o.box[3]);
                    const double oy0 = std::min(o.box[1], o.box[4]), oy1 = std::max(o.box[1], o.box[4]);
                    const double oz0 = std::min(o.box[2], o.box[5]), oz1 = std::max(o.box[2], o.box[5]);
                    const VECTOR3 oG = gp + mul(R, _V((ox0 + ox1) / 2, (oy0 + oy1) / 2, (oz0 + oz1) / 2)) - cp;
                    cb.open[no * 4 + 0] = { (float)oG.x, (float)oG.y, (float)oG.z, 1 };
                    cb.open[no * 4 + 1] = { (float)ax.x, (float)ax.y, (float)ax.z, (float)((ox1 - ox0) / 2) };
                    cb.open[no * 4 + 2] = { (float)ay.x, (float)ay.y, (float)ay.z, (float)((oy1 - oy0) / 2) };
                    cb.open[no * 4 + 3] = { (float)az.x, (float)az.y, (float)az.z, (float)((oz1 - oz0) / 2) };
                    no++;
                }
                cb.volK.y = (float)no;
            }
            break;
        }
    }
    std::sort(g_cand.begin(), g_cand.end(), [](const Cand& a, const Cand& b) { return a.imp > b.imp; });
    if (g_cand.size() > (size_t)MAXL) g_cand.resize(MAXL);

    // the light's bounds (the cone's sphere when smaller) seen by the camera, and near enough (shadows, beams: a few
    // hundred metres - the master pass's frame budget)
    const double tanY0 = tan(oapiCameraAperture()), tanX0 = tanY0 * (double)RW / std::max<double>(RH, 1);
    auto inView = [&](const Cand& c) {
        VECTOR3 ctr = c.pos; double r = c.range;
        if (c.spot) {
            const double ca = std::max(c.cosOut, 1e-3), sa = sqrt(std::max(1 - ca * ca, 0.0));
            VECTOR3 cc; double rr;
            if (ca < 0.7071) { cc = c.pos + c.dir * (r * ca); rr = r * sa; } else { cc = c.pos + c.dir * (r / (2 * ca)); rr = r / (2 * ca); }
            if (rr < r) { ctr = cc; r = rr; }
        }
        if (length(c.pos) > 300) return false;
        const VECTOR3 rg = _V(cr.m11, cr.m21, cr.m31), u = _V(cr.m12, cr.m22, cr.m32);
        const double px = dotp(ctr, rg), py = dotp(ctr, u), pz = dotp(ctr, cfwd);
        if (pz + r < 0) return false;
        // the four side planes of the frustum
        const double kx = 1 / sqrt(1 + tanX0 * tanX0), ky = 1 / sqrt(1 + tanY0 * tanY0);
        if ((px - pz * tanX0) * kx > r || (-px - pz * tanX0) * kx > r) return false;
        if ((py - pz * tanY0) * ky > r || (-py - pz * tanY0) * ky > r) return false;
        return true;
    };
    // ---- what the local lights add to the light the eye sees: their illuminance on the visible surfaces (the probe's 144
    // points of two frames ago, lights_vol.hlsl CSProbe), averaged over the view (the sky's points count as 0)
    {
        if (g_probeN >= 3 && g_probeSt[g_probeN % 3]) {
            D3D11_MAPPED_SUBRESOURCE pm;
            if (SUCCEEDED(ctx->Map(g_probeSt[g_probeN % 3].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &pm))) {
                memcpy(g_probePts, pm.pData, sizeof g_probePts); ctx->Unmap(g_probeSt[g_probeN % 3].Get(), 0); g_probeOk = true;
            }
        }
        double E = 0; VECTOR3 col = _V(0, 0, 0);
        if (g_probeOk) {
            double wsum = 0, lumLog = 0, lumW = 0; std::vector<double> lums; lums.reserve(144);
            for (int pi = 0; pi < 144; pi++) {     // the eye adapts mostly to the middle of the view: weights fall off to the edges
                const double px = ((pi % 16) + 0.5) / 16 - 0.5, py = ((pi / 16) + 0.5) / 9 - 0.5;
                wsum += exp(-(px * px + py * py) / (2 * 0.18 * 0.18));
            }
            for (int pi = 0; pi < 144; pi++) {
                const XMFLOAT4& q = g_probePts[pi];
                const double px = ((pi % 16) + 0.5) / 16 - 0.5, py = ((pi / 16) + 0.5) / 9 - 0.5;
                const double pw = exp(-(px * px + py * py) / (2 * 0.18 * 0.18)) / wsum;
                if (q.w < 0.5) continue;
                lumLog += pw * log(std::max((double)q.w - 1, 1e-7)); lumW += pw;
                lums.push_back((double)q.w - 1);
                const VECTOR3 x = _V(q.x, q.y, q.z);
                for (const Cand& c : g_cand) {
                    if (c.rect) continue;      // (a rectangle lamp: the image's metering sees it)
                    VECTOR3 v = c.pos - x; const double d2 = dotp(v, v);
                    if (d2 >= c.range * c.range || d2 < 0.01) continue;
                    double a = std::max(0.0, 1 - (d2 / (c.range * c.range)) * (d2 / (c.range * c.range))); a *= a;
                    if (c.spot) {
                        const double cd = -dotp(v, c.dir) / sqrt(d2), ci = c.g.a.x;
                        if (cd <= c.cosOut) continue;
                        double t = std::clamp((cd - c.cosOut) / std::max(ci - c.cosOut, 1e-4), 0.0, 1.0); t = t * t * (3 - 2 * t); a *= t * t;
                    }
                    const double k = 0.5 * a / std::max(d2, 0.25) * pw;        // 0.5: the surfaces facing it half on average
                    E += k * (0.2126 * c.g.c.x + 0.7152 * c.g.c.y + 0.0722 * c.g.c.z);
                    col = col + _V(c.g.c.x, c.g.c.y, c.g.c.z) * k;
                }
            }
            g_lumOk = lumW > 0.05;              // enough of the view is something (not the black sky)
            g_lumView = g_lumOk ? exp(lumLog / lumW) : 0;
            if (!lums.empty()) { std::sort(lums.begin(), lums.end()); g_lumP95 = lums[std::min(lums.size() - 1, (size_t)(lums.size() * 0.95))]; }
            else g_lumP95 = 0;
        }
        g_eLoc = E;
        const double cl = 0.2126 * col.x + 0.7152 * col.y + 0.0722 * col.z;
        g_eCol = cl > 0 ? col / cl : _V(1, 1, 1);
    }
    // ---- the lamps' shadows. Chosen by what they matter (the list is sorted): spots in view (not the floods over 150 deg)
    // - the first 4 a tile of 1024, the next 12 one of 512 -, point lights near and in view - the first 6 a cube of 512 tiles.
    // Each keeps its slot and its map while chosen; a map is drawn again only when its signature (the lamp, the casters
    // in its reach: where they are, how they turn, their animations; a person always) changed, 12 tiles a frame at most
    if (cfg.shadows && g_spotDSV && g_rec) {
        // the reference frame: the nearest planet (the maps stay put over its ground)
        OBJHANDLE hr = nullptr; double bestR = 1e300; VECTOR3 pr = _V(0, 0, 0);
        for (DWORD i = 0; i < oapiGetGbodyCount(); i++) {
            OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) != OBJTP_PLANET) continue;
            VECTOR3 q; oapiGetGlobalPos(h, &q); const double d = length(cp - q) - oapiGetSize(h);
            if (d < bestR) { bestR = d; hr = h; pr = q; }
        }
        MATRIX3 Rr = _M(1, 0, 0, 0, 1, 0, 0, 0, 1); if (hr) oapiGetRotationMatrix(hr, &Rr);
        if (hr != g_shRef) { g_slots.clear(); g_shRef = hr; }
        const VECTOR3 org = hr ? pr : _V(0, 0, 0);                      // the frame's origin (global)
        auto toL = [&](const VECTOR3& g) { return tmul(Rr, g); };           // a direction to the frame
        auto ptL = [&](const VECTOR3& camRel) { return tmul(Rr, camRel + cp - org); };   // a camera-centred point to the frame
        // the choice
        struct Pick { Cand* c; int kind; };
        std::vector<Pick> picks; int nBig = 0, nSmall = 0, nCubeP = 0;
        for (Cand& c : g_cand) {
            if (c.rect) continue;      // (rectangle lamps: no shadow map yet)
            if (c.spot) {
                if (c.cosOut < cos(75 * RAD) || c.range < 2 || !inView(c)) continue;
                if (nBig < NBIG) { picks.push_back({ &c, 0 }); nBig++; }
                else if (nSmall < MAXSPOTSH - NBIG) { picks.push_back({ &c, 1 }); nSmall++; }
            } else {
                if (c.range < 1.5 || length(c.pos) > 100 + c.range || nCubeP >= MAXCUBE || !inView(c)) continue;
                picks.push_back({ &c, 2 }); nCubeP++;
            }
        }
        // the slots: kept for the lamps chosen again, freed for the others, new ones from the free tiles
        auto findSlot = [&](const Pick& pk) -> ShadowSlot* {
            for (ShadowSlot& sl : g_slots) if (sl.key == pk.c->key && sl.kind == pk.kind) return &sl;
            return nullptr;
        };
        for (ShadowSlot& sl : g_slots) sl.seen = false;
        for (const Pick& pk : picks) if (ShadowSlot* sl = findSlot(pk)) sl->seen = true;
        g_slots.erase(std::remove_if(g_slots.begin(), g_slots.end(), [](const ShadowSlot& sl) { return !sl.seen; }), g_slots.end());
        bool bigUsed[NBIG] = {}, smallUsed[NSMALL] = {};
        for (const ShadowSlot& sl : g_slots) for (int t : sl.tiles) if (t >= 0) { if (sl.kind == 0) bigUsed[t] = true; else smallUsed[t] = true; }
        for (const Pick& pk : picks) {
            if (findSlot(pk)) continue;
            ShadowSlot ns; ns.key = pk.c->key; ns.kind = pk.kind;
            const int need = ns.kind == 2 ? 6 : 1; int got = 0;
            if (ns.kind == 0) { for (int t = 0; t < NBIG && !got; t++) if (!bigUsed[t]) { bigUsed[t] = true; ns.tiles[0] = t; got = 1; } }
            else for (int t = 0; t < NSMALL && got < need; t++) if (!smallUsed[t]) { smallUsed[t] = true; ns.tiles[got++] = t; }
            if (got < need) { if (ns.kind != 0) for (int k = 0; k < got; k++) smallUsed[ns.tiles[k]] = false; continue; }   // no room: no shadow
            g_slots.push_back(ns);
        }
        // the tiles' places in the atlas: the big ones along rows 0-1, the small ones from row 2
        auto tileRect = [](int kind, int t, int& x, int& y, int& n) {
            if (kind == 0) { x = t * 2 * UNIT; y = 0; n = 2 * UNIT; } else { x = (t % 8) * UNIT; y = (2 + t / 8) * UNIT; n = UNIT; }
        };
        ID3D11ShaderResourceView* nul = nullptr;
        ctx->PSSetShaderResources(20, 1, &nul);
        std::vector<ShadowRecG> recs; recs.reserve(MAXREC);
        int budget = SHADOW_BUDGET;
        const VECTOR3 axX = mul(Rr, _V(1, 0, 0)), axY = mul(Rr, _V(0, 1, 0));
        int dynBudget = 4;      // the tiles composed with people a frame, the lamps that matter most first (each a skinned figure drawn again: ~0.3 ms)
        const bool split = g_statDSV && g_statSRV && g_psCopyZ && g_vsClearZ && g_dsAlways;
        auto clearTile = [&]() {
            ctx->OMSetDepthStencilState(g_dsAlways.Get(), 0); ctx->RSSetState(rsNone.Get());
            ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ctx->VSSetShader(g_vsClearZ.Get(), nullptr, 0); ctx->PSSetShader(nullptr, nullptr, 0);
            ctx->Draw(3, 0);
        };
        for (const Pick& pk : picks) {
            ShadowSlot* sl = findSlot(pk);
            Cand& c = *pk.c;
            c.tile = -1; c.g.a.z = -1;
            if (!sl) continue;
            const bool cube = sl->kind == 2;
            const int nf = cube ? 6 : 1;
            // the casters in reach: the static ones (and their signature with the lamp's), the people
            const double half = cube ? 45 * RAD : acos(c.cosOut) + 2 * RAD, tanH = tan(half);
            std::vector<VesselVis*> stat, dyn;
            uint64_t sig = 1469598103934665603ull;
            const VECTOR3 eL = ptL(c.pos), dL = toL(c.dir);
            Fnv64(sig, (int64_t)llround(eL.x * 1000)); Fnv64(sig, (int64_t)llround(eL.y * 1000)); Fnv64(sig, (int64_t)llround(eL.z * 1000));
            Fnv64(sig, (int64_t)llround(dL.x * 1e4)); Fnv64(sig, (int64_t)llround(dL.y * 1e4)); Fnv64(sig, (int64_t)llround(dL.z * 1e4));
            Fnv64(sig, (int64_t)llround(c.range * 100));
            // the lamp's own vessel casts too (a building's walls and pillars for its hall lamps, a vehicle's body), except a
            // person (a helmet lamp's own helmet, arms and body - skinned, a frame late - cut its cone to a crescent)
            bool ownPerson = false;
            if (VESSEL* ov = oapiGetVesselInterface(c.h)) { const char* cn = ov->GetClassNameA(); ownPerson = cn && strstr(cn, "OrbiterCrew"); }
            for (auto& kv : visuals) {
                if (!kv.second->v || (kv.first == c.h && ownPerson)) continue;
                VECTOR3 q; oapiGetGlobalPos(kv.first, &q);
                const VECTOR3 rv = q - cp - c.pos;
                const double sz = oapiGetSize(kv.first);
                if (length(rv) > c.range + sz) continue;
                if (!cube) {
                    const double along = dotp(rv, c.dir);
                    if (along < -sz || length(rv - c.dir * along) > std::max(along, 0.0) * tanH + sz) continue;
                }
                VESSEL* v = kv.second->v;
                const char* cn = v->GetClassNameA();
                if (cn && strstr(cn, "OrbiterCrew")) {     // a person: drawn every frame over the static map - only near the lamp (8 m)
                    if (length(rv) < std::min(c.range, 8.0) + sz) dyn.push_back(kv.second);
                    continue;
                }
                stat.push_back(kv.second);
                const VECTOR3 qL = ptL(q - cp); MATRIX3 Rv; v->GetRotationMatrix(Rv);
                Fnv64(sig, (int64_t)(intptr_t)kv.first);
                Fnv64(sig, (int64_t)llround(qL.x * 1000)); Fnv64(sig, (int64_t)llround(qL.y * 1000)); Fnv64(sig, (int64_t)llround(qL.z * 1000));
                const VECTOR3 fx = toL(_V(Rv.m11, Rv.m21, Rv.m31)), fz = toL(_V(Rv.m13, Rv.m23, Rv.m33));
                Fnv64(sig, (int64_t)llround(fx.x * 1e4)); Fnv64(sig, (int64_t)llround(fx.y * 1e4)); Fnv64(sig, (int64_t)llround(fx.z * 1e4));
                Fnv64(sig, (int64_t)llround(fz.x * 1e4)); Fnv64(sig, (int64_t)llround(fz.y * 1e4)); Fnv64(sig, (int64_t)llround(fz.z * 1e4));
                ANIMATION* an = nullptr; const UINT na = v->GetAnimPtr(&an);
                for (UINT k = 0; k < na && an; k++) Fnv64(sig, (int64_t)llround(an[k].state * 1e4));
            }
            if (stat.empty() && dyn.empty()) { sl->valid = false; sl->hadDyn = false; continue; }   // nothing in reach: no shadow
            const bool statDirty = !sl->valid || sl->sig != sig;
            bool drawStat = false;
            if (statDirty && budget >= nf) {
                budget -= nf; g_nDrawn += nf; drawStat = true;
                sl->sig = sig; sl->valid = true;
                sl->eyeL = eL; sl->farZ = c.range; sl->nearZ = cube ? 0.25 : 0.20;     // the fixture round the lamp casts no shadow
                sl->fov = cube ? 2 * atan(1.04) : std::min(2 * half, 160 * RAD);
                sl->fwdL = dL; sl->upL = fabs(dL.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0);
            }
            if (!sl->valid) continue;                                   // waiting for the budget: no shadow this frame
            const int first = (int)recs.size();
            if (first + nf > MAXREC) continue;
            // the people: drawn over a copy of the static map (and once more after they left, to clear them)
            const bool compose = split && (!dyn.empty() || sl->hadDyn) && (drawStat || dynBudget >= nf);
            if (compose && !drawStat) dynBudget -= nf;
            sl->hadDyn = !dyn.empty() && compose;
            const VECTOR3 eyeG = mul(Rr, sl->eyeL) + org - cp;
            for (int f = 0; f < nf; f++) {
                VECTOR3 fw, up;
                if (!cube) { fw = mul(Rr, sl->fwdL); up = mul(Rr, sl->upL); }
                else {
                    static const VECTOR3 F[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
                    static const VECTOR3 U[6] = { {0,1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {0,1,0}, {0,1,0} };
                    fw = mul(Rr, F[f]); up = mul(Rr, U[f]);
                }
                XMMATRIX LVP = XMMatrixLookToLH(XMVectorSet((float)eyeG.x, (float)eyeG.y, (float)eyeG.z, 1), XMVectorSet((float)fw.x, (float)fw.y, (float)fw.z, 0),
                                                XMVectorSet((float)up.x, (float)up.y, (float)up.z, 0))
                             * XMMatrixPerspectiveFovLH((float)sl->fov, 1.0f, (float)sl->nearZ, (float)sl->farZ);
                int tx, ty, tn; tileRect(sl->kind, sl->tiles[f], tx, ty, tn);
                ShadowRecG r;
                XMStoreFloat4x4(&r.vp, XMMatrixTranspose(LVP));
                r.rect = { (float)tx / ATLAS, (float)ty / ATLAS, (float)tn / ATLAS, 1.0f / ATLAS };
                r.k = { (float)(2 * tan(sl->fov * 0.5) / tn), (float)sl->nearZ, (float)sl->farZ, 0 };
                r.ax = { (float)axX.x, (float)axX.y, (float)axX.z, 0 }; r.ay = { (float)axY.x, (float)axY.y, (float)axY.z, 0 };
                recs.push_back(r);
                D3D11_VIEWPORT vp = { (float)tx, (float)ty, (float)tn, (float)tn, 0, 1 };
                if (!split) {         // (no static atlas: everything into the one map)
                    if (drawStat || !dyn.empty()) {
                        ctx->OMSetRenderTargets(0, nullptr, g_spotDSV.Get()); ctx->RSSetViewports(1, &vp);
                        if (g_vsClearZ && g_dsAlways) clearTile();
                        for (VesselVis* vv : stat) RenderVessel(vv, cp, _M(1, 0, 0, 0, 1, 0, 0, 0, 1), XMMatrixIdentity(), false, &LVP);
                        for (VesselVis* vv : dyn) RenderVessel(vv, cp, _M(1, 0, 0, 0, 1, 0, 0, 0, 1), XMMatrixIdentity(), false, &LVP);
                    }
                    continue;
                }
                // the casters in this face's frustum (a cube's face: a 92 deg pyramid; a spot: its own cone was the test)
                auto inFace = [&](VesselVis* vv) {
                    if (!cube) return true;
                    VECTOR3 q; oapiGetGlobalPos(vv->hObj, &q);
                    const VECTOR3 rv = q - cp - c.pos; const double sz = oapiGetSize(vv->hObj);
                    const double fz = dotp(rv, fw); if (fz < -sz) return false;
                    const VECTOR3 rt = crossp(up, fw);
                    const double ex = std::max(fz, 0.0) * 1.04 + sz * 1.5;
                    return fabs(dotp(rv, rt)) <= ex && fabs(dotp(rv, up)) <= ex;
                };
                if (drawStat) {       // the static casters into the static atlas
                    ctx->OMSetRenderTargets(0, nullptr, g_statDSV.Get()); ctx->RSSetViewports(1, &vp);
                    clearTile();
                    for (VesselVis* vv : stat) if (inFace(vv)) RenderVessel(vv, cp, _M(1, 0, 0, 0, 1, 0, 0, 0, 1), XMMatrixIdentity(), false, &LVP);
                }
                if (drawStat || compose) {     // the working tile: the static map copied, the people over it
                    ID3D11ShaderResourceView* nulS = nullptr; ctx->PSSetShaderResources(0, 1, &nulS);
                    ctx->OMSetRenderTargets(0, nullptr, g_spotDSV.Get()); ctx->RSSetViewports(1, &vp);
                    ctx->OMSetDepthStencilState(g_dsAlways.Get(), 0); ctx->RSSetState(rsNone.Get());
                    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    ctx->VSSetShader(g_vsClearZ.Get(), nullptr, 0); ctx->PSSetShader(g_psCopyZ.Get(), nullptr, 0);
                    ctx->PSSetShaderResources(0, 1, g_statSRV.GetAddressOf());
                    ctx->Draw(3, 0);
                    ctx->PSSetShaderResources(0, 1, &nulS);
                    for (VesselVis* vv : dyn) if (inFace(vv)) RenderVessel(vv, cp, _M(1, 0, 0, 0, 1, 0, 0, 0, 1), XMMatrixIdentity(), false, &LVP);
                }
            }
            c.tile = first; c.g.a.z = (float)first;
            if (cube) g_nCube++; else g_nSpot++;
        }
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        if (!recs.empty()) Upload(ctx.Get(), g_rec.Get(), recs.data(), recs.size());
    }
    // ---- the sky's visibility round the camera: the vessels' depth from 12 directions over the sky (a cabin sees the sky
    // through its windows only; the ground under a vehicle less of it). Ortho maps of 2R round the camera (moved in whole
    // texels: no crawling), casters up to 60 m towards the sky
    if (cfg.skyVis && g_skyDSV && (g_fxAtmo.active || skyOn)) {
        OBJHANDLE hp = nullptr; double best = 1e300; VECTOR3 pp = _V(0, 0, 0);
        for (DWORD i = 0; i < oapiGetGbodyCount(); i++) {
            OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) != OBJTP_PLANET) continue;
            VECTOR3 q; oapiGetGlobalPos(h, &q); const double a = length(cp - q) - oapiGetSize(h);
            if (a < best) { best = a; hp = h; pp = q; }
        }
        double agl = best;     // the camera over the ground under it (the relief: mountains are kilometres over the mean radius)
        if (hp) { double lng, lat, rad; oapiGlobalToEqu(hp, cp, &lng, &lat, &rad); agl = rad - oapiGetSize(hp) - oapiSurfaceElevation(hp, lng, lat); }
        if (hp && agl < 2000) {     // near the ground only (a cabin, under a hull): higher up it costs 12 renders of the vessels for nothing (16 km: 17-40 ms, Storm_orbit 20.10.2026)
            // the maps' frame fixed to the planet (the ground turns with it: snapped in the inertial frame the texels slid over
            // everything at the planet's rotation and orbit speed - the shades trembled), and kept until the camera has moved
            // ~25 km (else the axes turn a little with every metre and the snapping moves with them)
            MATRIX3 Rp; oapiGetRotationMatrix(hp, &Rp);
            const VECTOR3 camL = tmul(Rp, cp - pp);                    // the camera in the planet frame
            static VECTOR3 upL = { 0, 0, 0 }; static OBJHANDLE upP = nullptr;
            const VECTOR3 nowL = camL / length(camL);
            if (upP != hp || dotp(nowL, upL) < 0.99995) { upL = nowL; upP = hp; }
            VECTOR3 e1L = crossp(fabs(upL.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0), upL); e1L = e1L / length(e1L);
            VECTOR3 e2L = crossp(upL, e1L);
            const VECTOR3 up = mul(Rp, upL), e1 = mul(Rp, e1L), e2 = mul(Rp, e2L);
            const double R = g_airless ? std::max(cfg.skyR, 30.0) : cfg.skyR, back = 60.0, texel = 2 * R / SKY_N;   // airless: a whole hall in reach (the bounce fill needs its enclosure); Titan as approved
            ID3D11ShaderResourceView* nul = nullptr;
            ctx->PSSetShaderResources(21, 1, &nul);
            if (g_skyPlanet != hp) { for (auto& m : g_skyMap) m.valid = false; g_skyPlanet = hp; }
            ctx->OMSetRenderTargets(0, nullptr, g_skyDSV.Get());
            // cached: the maps centred on a cell of R/4 in the planet frame round the camera, the static casters only (a person
            // does not shade the sky's light noticeably); a map drawn again when the camera passed into another cell or when
            // what stands there moved, three at most a frame (16 views of the base a frame cost ~10 ms, 21.10.2026)
            const double cell = R / 4;
            const VECTOR3 cellL = _V(floor(camL.x / cell + 0.5) * cell, floor(camL.y / cell + 0.5) * cell, floor(camL.z / cell + 0.5) * cell);
            const VECTOR3 cellG = mul(Rp, cellL) + pp - cp;
            std::vector<VesselVis*> skyCast;
            uint64_t ssig = 1469598103934665603ull;
            for (auto& kv : visuals) {
                VESSEL* v = kv.second->v; if (!v) continue;
                const char* cn = v->GetClassNameA();
                if (cn && strstr(cn, "OrbiterCrew")) continue;
                VECTOR3 q; oapiGetGlobalPos(kv.first, &q);
                const double sz = oapiGetSize(kv.first);
                if (length(q - cp - cellG) > R * 1.8 + back + sz) continue;
                skyCast.push_back(kv.second);
                const VECTOR3 qL = tmul(Rp, q - pp); MATRIX3 Rv; v->GetRotationMatrix(Rv);
                Fnv64(ssig, (int64_t)(intptr_t)kv.first);
                Fnv64(ssig, (int64_t)llround(qL.x * 100)); Fnv64(ssig, (int64_t)llround(qL.y * 100)); Fnv64(ssig, (int64_t)llround(qL.z * 100));
                const VECTOR3 fx = tmul(Rp, _V(Rv.m11, Rv.m21, Rv.m31));
                Fnv64(ssig, (int64_t)llround(fx.x * 1e3)); Fnv64(ssig, (int64_t)llround(fx.y * 1e3)); Fnv64(ssig, (int64_t)llround(fx.z * 1e3));
                ANIMATION* an = nullptr; const UINT na = v->GetAnimPtr(&an);
                for (UINT k = 0; k < na && an; k++) Fnv64(ssig, (int64_t)llround(an[k].state * 1e3));
            }
            int skyBudget = 3;
            bool anyInvalid = false; for (auto& m : g_skyMap) anyInvalid |= !m.valid;
            if (anyInvalid) skyBudget = NSKY;
            for (int k = 0; k < NSKY; k++) {
                SkyMap& sm = g_skyMap[k];
                const bool due = !sm.valid || sm.sig != ssig || length(sm.cellL - cellL) > 1e-3;
                const bool redraw = due && skyBudget > 0;
                if (redraw) {
                    skyBudget--;
                    // the zenith, 5 at 60 deg, 6 at 22 deg over the horizon
                    const double el = k == 0 ? 90 * RAD : k < 6 ? 60 * RAD : 22 * RAD;
                    const double az = k == 0 ? 0 : k < 6 ? (k - 1) * 72 * RAD : (k - 6) * 60 * RAD + 30 * RAD;
                    const VECTOR3 dL = upL * sin(el) + (e1L * cos(az) + e2L * sin(az)) * cos(el);
                    VECTOR3 aL = fabs(dotp(dL, upL)) < 0.95 ? upL : e1L;
                    VECTOR3 uL = crossp(aL, dL); uL = uL / length(uL); const VECTOR3 vL = crossp(dL, uL);
                    sm.eyeL = cellL + dL * (R + back); sm.dL = dL; sm.vL = vL; sm.cellL = cellL; sm.sig = ssig; sm.valid = true;
                }
                // its matrix for the camera of now
                const VECTOR3 eyeG = mul(Rp, sm.eyeL) + pp - cp, d = mul(Rp, sm.dL), v = mul(Rp, sm.vL);
                XMMATRIX LVP = XMMatrixLookToLH(XMVectorSet((float)eyeG.x, (float)eyeG.y, (float)eyeG.z, 1), XMVectorSet(-(float)d.x, -(float)d.y, -(float)d.z, 0), XMVectorSet((float)v.x, (float)v.y, (float)v.z, 0))
                             * XMMatrixOrthographicLH((float)(2 * R), (float)(2 * R), 0.0f, (float)(2 * R + back));
                XMStoreFloat4x4(&cb.skyVP[k], XMMatrixTranspose(LVP));
                cb.skyDir[k] = { (float)d.x, (float)d.y, (float)d.z, 0 };
                if (!redraw) continue;
                D3D11_VIEWPORT vp = { (float)((k & 3) * SKY_N), (float)((k >> 2) * SKY_N), (float)SKY_N, (float)SKY_N, 0, 1 };
                ctx->RSSetViewports(1, &vp);
                // the tile cleared alone (a triangle at depth 1)
                if (g_vsClearZ && g_dsAlways) {
                    ctx->OMSetDepthStencilState(g_dsAlways.Get(), 0); ctx->RSSetState(rsNone.Get());
                    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    ctx->VSSetShader(g_vsClearZ.Get(), nullptr, 0); ctx->PSSetShader(nullptr, nullptr, 0);
                    ctx->Draw(3, 0);
                } else ctx->ClearDepthStencilView(g_skyDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
                const VECTOR3 cg = eyeG - d * (R + back);       // the map's centre (camera-centred)
                for (VesselVis* vv : skyCast) {
                    VECTOR3 q; oapiGetGlobalPos(vv->hObj, &q);
                    VECTOR3 rq = q - cp - cg;
                    const double along = dotp(rq, d), sz = oapiGetSize(vv->hObj);
                    if (length(rq - d * along) > R * 1.42 + sz || along > R + back + sz || along < -(R + sz)) continue;
                    RenderVessel(vv, cp, _M(1, 0, 0, 0, 1, 0, 0, 0, 1), XMMatrixIdentity(), false, &LVP);
                }
            }
            g_skyCentreG = cellG;
            g_skyNext = (g_skyNext + 1) % 4;
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            cb.skyC = { (float)g_skyCentreG.x, (float)g_skyCentreG.y, (float)g_skyCentreG.z, (float)R };
            cb.skyK = { (float)texel, 1.0f / (4 * SKY_N), 0, 0 };
        }
    }
    // ---- the beams in the air: the strongest spots (and points) of some reach, in an air
    if (cfg.beams && g_psBeams && (g_fxAtmo.active || skyOn) && !g_airless) {     // no air, no beams (the Moon: a "white fog" at 5 m)
        for (Cand& c : g_cand) {
            if (g_nBeams >= MAXBEAM) break;
            if (c.rect || c.range < 4 || c.Icd < 300 || !inView(c)) continue;      // a lamp of some strength and reach (not the cabins' lamps)
            c.beam = true; c.g.a.w = 1; g_nBeams++;
        }
    }
    // ---- the lenses of the spots seen from outside
    if (cfg.beacons) {
        for (const Cand& c : g_cand) {
            if (!c.spot || c.h == own || g_sprites.size() >= MAXSPR) continue;
            if (dotp(c.pos, cfwd) < 0.05) continue;
            SpriteG s;
            const VECTOR3 p = c.pos + c.dir * 0.01;
            s.p = { (float)p.x, (float)p.y, (float)p.z, c.g.a.y };
            s.c = { c.g.c.x, c.g.c.y, c.g.c.z, 0 };
            s.d = c.g.d; s.e = { c.g.a.x, 0, 0, 0 };
            g_sprites.push_back(s);
        }
    }
    // the beams' lights first in the list (the beams pass reads the first ones)
    std::stable_partition(g_cand.begin(), g_cand.end(), [](const Cand& c) { return c.beam; });

    // ---- the clusters
    g_nLights = (int)g_cand.size();
    const VECTOR3 rgt = _V(cr.m11, cr.m21, cr.m31), upv = _V(cr.m12, cr.m22, cr.m32);
    const double tanY = tan(oapiCameraAperture()), tanX = tanY * (double)RW / std::max<double>(RH, 1);
    const double sx = 1 / tanX, sy = 1 / tanY, kz = (TZ - 1) / log((double)ZF / Z0);
    auto slice = [&](double z) { return z <= Z0 ? 0 : std::min(TZ - 1, 1 + (int)(log(z / Z0) * kz)); };
    struct Box { int x0, x1, y0, y1, s0, s1; };
    std::vector<Box> box(g_nLights);
    std::vector<uint32_t> cnt(NCL, 0);
    for (int li = 0; li < g_nLights; li++) {
        const Cand& c = g_cand[li];
        VECTOR3 ctr = c.pos; double r = c.range;
        if (c.spot) {     // the cone's bounding sphere, when smaller
            const double ca = std::max(c.cosOut, 1e-3), sa = sqrt(std::max(1 - ca * ca, 0.0));
            VECTOR3 cc; double rr;
            if (ca < 0.7071) { cc = c.pos + c.dir * (r * ca); rr = r * sa; } else { cc = c.pos + c.dir * (r / (2 * ca)); rr = r / (2 * ca); }
            if (rr < r) { ctr = cc; r = rr; }
        }
        if (c.rect && c.room >= 0) {     // a panel kept to its room: the room's bounding sphere, when smaller
            const XMFLOAT4& rc = cb.room[c.room * 4];
            const double hr = sqrt((double)cb.room[c.room * 4 + 1].w * cb.room[c.room * 4 + 1].w + (double)cb.room[c.room * 4 + 2].w * cb.room[c.room * 4 + 2].w
                                   + (double)cb.room[c.room * 4 + 3].w * cb.room[c.room * 4 + 3].w) + rc.w;
            if (hr < r) { ctr = _V(rc.x, rc.y, rc.z); r = hr; }
        }
        const double px = dotp(ctr, rgt), py = dotp(ctr, upv), pz = dotp(ctr, cfwd);
        Box b = { 0, TX - 1, 0, TY - 1, 0, 0 };
        if (pz + r < 0.05) { b.s0 = 1; b.s1 = 0; box[li] = b; continue; }     // behind the eye: no cluster
        b.s0 = slice(std::max(pz - r, 0.0)); b.s1 = slice(pz + r);
        if (pz - r > 0.05) {
            const double zn = pz - r, zf = pz + r;
            const double xa = std::min((px - r) / zn, (px - r) / zf) * sx, xb = std::max((px + r) / zn, (px + r) / zf) * sx;
            const double ya = std::min((py - r) / zn, (py - r) / zf) * sy, yb = std::max((py + r) / zn, (py + r) / zf) * sy;
            if (xb < -1 || xa > 1 || yb < -1 || ya > 1) { b.s0 = 1; b.s1 = 0; box[li] = b; continue; }
            b.x0 = std::clamp((int)floor((xa * 0.5 + 0.5) * TX), 0, TX - 1); b.x1 = std::clamp((int)floor((xb * 0.5 + 0.5) * TX), 0, TX - 1);
            b.y0 = std::clamp((int)floor((-yb * 0.5 + 0.5) * TY), 0, TY - 1); b.y1 = std::clamp((int)floor((-ya * 0.5 + 0.5) * TY), 0, TY - 1);
        }
        box[li] = b;
    }
    // the beams' boxes on the screen (the clusters' tiles: coarse, conservative)
    g_beamRect.clear();
    for (int li = 0; li < g_nBeams && li < g_nLights; li++) {
        const Box& b = box[li];
        if (b.s1 < b.s0) { g_beamRect.push_back({ 0, 0, 0, 0 }); continue; }
        g_beamRect.push_back({ (LONG)(b.x0 * (LONG)RW / TX), (LONG)(b.y0 * (LONG)RH / TY), (LONG)((b.x1 + 1) * (LONG)RW / TX), (LONG)((b.y1 + 1) * (LONG)RH / TY) });
    }
    // the index list, the most important lights first; a light that would overflow it is left out
    size_t total = 0;
    for (int li = 0; li < g_nLights; li++) {
        const Box& b = box[li];
        if (b.s1 < b.s0) continue;
        const size_t n = (size_t)(b.x1 - b.x0 + 1) * (b.y1 - b.y0 + 1) * (b.s1 - b.s0 + 1);
        if (total + n > (size_t)MAXIDX) { box[li].s0 = 1; box[li].s1 = 0; continue; }
        total += n;
        for (int s = b.s0; s <= b.s1; s++) for (int y = b.y0; y <= b.y1; y++) for (int x = b.x0; x <= b.x1; x++) cnt[(s * TY + y) * TX + x]++;
    }
    g_clusterData.assign(NCL * 2, 0);
    uint32_t ofs = 0;
    for (int i = 0; i < NCL; i++) { g_clusterData[i * 2] = ofs; ofs += cnt[i]; }
    g_index.assign(std::max<size_t>(total, 1), 0);
    for (int li = 0; li < g_nLights; li++) {
        const Box& b = box[li];
        if (b.s1 < b.s0) continue;
        for (int s = b.s0; s <= b.s1; s++) for (int y = b.y0; y <= b.y1; y++) for (int x = b.x0; x <= b.x1; x++) {
            const int ci = (s * TY + y) * TX + x;
            g_index[g_clusterData[ci * 2] + g_clusterData[ci * 2 + 1]++] = (uint32_t)li;
        }
    }
    std::vector<LightG> lg(g_nLights);
    for (int li = 0; li < g_nLights; li++) lg[li] = g_cand[li].g;
    Upload(ctx.Get(), g_light.Get(), lg.data(), lg.size());
    Upload(ctx.Get(), g_cl.Get(), g_clusterData.data(), g_clusterData.size());
    Upload(ctx.Get(), g_idx.Get(), g_index.data(), std::min(g_index.size(), (size_t)MAXIDX));
    g_nSpr = (int)g_sprites.size();
    if (g_nSpr) Upload(ctx.Get(), g_spr.Get(), g_sprites.data(), g_sprites.size());
    cb.grid.w = (float)g_nLights;
    cb.z.z = 1;
    cb.skyK.z = (g_airless || g_inVol) ? 1.0f : 0.0f;     // the lamps' bounce fill: airless bodies, declared interiors (Titan's open look stays as approved)
    Upload(ctx.Get(), g_cb.Get(), &cb, 1);
    if (diagFrames > 0 || (g_frame % 600) == 300) {
        LOGC("local lights: %d (beams %d, shadows: spots %d, point cubes %d, tiles drawn this frame %d; sprites %d), cluster entries %zu, Sun here %.0f lux",
             g_nLights, g_nBeams, g_nSpot, g_nCube, g_nDrawn, g_nSpr, total, Esun);
        for (int li = 0; li < std::min(g_nLights, 6); li++) {
            const Cand& c = g_cand[li];
            LOGC("local light %d: %s %s at %.1f m (vessel frame %.3f %.3f %.3f), range %.0f m, %.0f cd (%.3g Sun at 1 m), cone %.0f deg, shadow %d, beam %d", li,
                 oapiGetVesselInterface(c.h) ? oapiGetVesselInterface(c.h)->GetName() : "?", c.spot ? "spot" : "point", length(c.pos), c.lp.x, c.lp.y, c.lp.z, c.range,
                 c.Icd, (double)(c.g.c.x + c.g.c.y + c.g.c.z) / 3, c.spot ? 2 * acos(c.cosOut) * DEG : 360.0, c.tile, c.beam ? 1 : 0);
        }
    }
}

void D3D11Client::LightsBind()
{
    ID3D11ShaderResourceView* srv[5] = { g_lightSRV.Get(), g_clSRV.Get(), g_idxSRV.Get(), g_spotSRV.Get(), g_skySRV.Get() };
    if (!g_cb) return;
    ctx->PSSetConstantBuffers(9, 1, g_cb.GetAddressOf());
    ctx->PSSetShaderResources(17, 5, srv);
    ctx->PSSetShaderResources(26, 1, g_recSRV.GetAddressOf());     // the shadow records (t22..t25 are the smoke's)
}

// ------------------------------------------------------------------------------------------ after the air: beams, lenses, beacons
void D3D11Client::LightsRenderAir(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    if (!g_ok || !cfg.on || (!g_nBeams && !g_nSpr)) return;
    ResolveDepth();
    ID3D11ShaderResourceView* dsrv = depthSRV.Get();
    if (!dsrv) return;
    LightsBind();
    FxBindLight(this);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);     // the depth is read as a texture
    D3D11_VIEWPORT vp = { 0, 0, (float)RW, (float)RH, 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetBlendState(bsAdd.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->PSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
    ID3D11SamplerState* ss = sampShadow.Get();
    ctx->PSSetSamplers(2, 1, &ss);
    if (g_nBeams && g_vsFull && g_psBeams && g_psUp) {
        const UINT hw = (RW + 1) / 2, hh = (RH + 1) / 2;
        if (hw != g_halfW || hh != g_halfH || !g_halfRTV) {
            g_halfTex.Reset(); g_halfRTV.Reset(); g_halfSRV.Reset();
            D3D11_TEXTURE2D_DESC td = { hw, hh, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
            if (SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &g_halfTex))) { dev->CreateRenderTargetView(g_halfTex.Get(), nullptr, &g_halfRTV); dev->CreateShaderResourceView(g_halfTex.Get(), nullptr, &g_halfSRV); }
            g_halfW = hw; g_halfH = hh;
        }
        if (g_halfRTV && g_halfSRV) {
            // the beams into the half-size target (each scissored to its cone's box, halved), added up there
            const float zero[4] = { 0, 0, 0, 0 };
            ctx->ClearRenderTargetView(g_halfRTV.Get(), zero);
            ctx->OMSetRenderTargets(1, g_halfRTV.GetAddressOf(), nullptr);
            D3D11_VIEWPORT hvp = { 0, 0, (float)hw, (float)hh, 0, 1 };
            ctx->RSSetViewports(1, &hvp);
            ctx->VSSetShader(g_vsFull.Get(), nullptr, 0);
            ctx->PSSetShader(g_psBeams.Get(), nullptr, 0);
            ctx->PSSetShaderResources(0, 1, &dsrv);
            ctx->RSSetState(g_rsScissor.Get());
            ctx->PSSetConstantBuffers(10, 1, g_beamCB.GetAddressOf());
            for (size_t k = 0; k < g_beamRect.size(); k++) {
                const D3D11_RECT& r = g_beamRect[k];
                if (r.right <= r.left || r.bottom <= r.top) continue;
                const XMFLOAT4 bi = { (float)k, 2, 0, 0 };
                Upload(ctx.Get(), g_beamCB.Get(), &bi, 1);
                const D3D11_RECT rh = { r.left / 2, r.top / 2, (r.right + 1) / 2, (r.bottom + 1) / 2 };
                ctx->RSSetScissorRects(1, &rh);
                ctx->Draw(3, 0);
            }
            // ... and onto the scene
            ctx->RSSetState(rsNone.Get());
            ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
            ctx->RSSetViewports(1, &vp);
            ctx->PSSetShader(g_psUp.Get(), nullptr, 0);
            ctx->PSSetShaderResources(2, 1, g_halfSRV.GetAddressOf());
            ID3D11SamplerState* sc1 = sampClamp.Get(); ctx->PSSetSamplers(1, 1, &sc1);
            ctx->Draw(3, 0);
            ID3D11ShaderResourceView* n1 = nullptr; ctx->PSSetShaderResources(2, 1, &n1);
        }
    }
    if (g_nSpr && g_vsSpr && g_psSpr) {
        SpriteCB sc;
        XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
        XMStoreFloat4x4(&sc.vp, XMMatrixTranspose(V * proj));
        XMFLOAT4X4 P; XMStoreFloat4x4(&P, proj);
        sc.view = { (float)RW, (float)RH, P._22, P._43 };
        sc.right = { (float)cr.m11, (float)cr.m21, (float)cr.m31, 0 };
        sc.up = { (float)cr.m12, (float)cr.m22, (float)cr.m32, 0 };
        sc.fwd = { (float)cr.m13, (float)cr.m23, (float)cr.m33, 0 };
        Upload(ctx.Get(), g_sprCB.Get(), &sc, 1);
        // tested against the scene's depth (read-only view: the depth is also read as a texture), so a cockpit or a helmet
        // already in the depth hides them (the master pass lays the own vessel's opaque depth before these passes)
        if (dsvRO) ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsvRO.Get());
        ctx->OMSetDepthStencilState(dsRead.Get(), 0);
        ctx->VSSetShader(g_vsSpr.Get(), nullptr, 0);
        ctx->PSSetShader(g_psSpr.Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(1, 1, g_sprCB.GetAddressOf());
        ctx->VSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
        ctx->VSSetConstantBuffers(9, 1, g_cb.GetAddressOf());
        ID3D11ShaderResourceView* vs2[2] = { dsrv, g_sprSRV.Get() };
        ctx->VSSetShaderResources(0, 2, vs2);
        ctx->DrawInstanced(6, (UINT)g_nSpr, 0, 0);
        ID3D11ShaderResourceView* n2[2] = {};
        ctx->VSSetShaderResources(0, 2, n2);
    }
    ID3D11ShaderResourceView* nul = nullptr;
    ctx->PSSetShaderResources(0, 1, &nul);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
}

// ------------------------------------------------------------------------------------------ the eye's adaptation
// The FX atmosphere adapts the eye to the sky's light at the camera (exposure = 1 / its luminance, the colour balance to
// its colour). Where the local lights light the view - the headlights' ground at night, a lit cabin - the eye adapts to
// them too: exposure 1 / (Y_sky + E_local) - E_local weighted by the part of the view each light lights - and the colour
// balance moved towards the lights' own (von Kries ~55 %, as the FX does it for the sky) by their share of the light
// there is now (at night nearly all). Smoothed over ~1.5 s.
static const bool kAdaptColour = true;     // the colour balance too (the user: correct colour rendering, 20.10.2026)
void D3D11Client::LightsAdaptEye(XMFLOAT4& adapt, float& exposure)
{
    static double kExp = 1; static VECTOR3 kCol = { 1, 1, 1 }; static LARGE_INTEGER last = {};
    LARGE_INTEGER now, f; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
    const double dt = last.QuadPart ? std::min((now.QuadPart - last.QuadPart) / (double)f.QuadPart, 1.0) : 1.0;
    last = now;
    double tExp = 1; VECTOR3 tCol = { 1, 1, 1 };
    if (g_ok && cfg.on && g_eLoc > 0 && adapt.w > 0) {
        const double Ybase = 1.0 / adapt.w, Yeye = Ybase + g_eLoc;
        tExp = Ybase / Yeye;
        // the colour: by the local lights' share of the light there is now (at night the sky gives ~nothing: the eye adapts
        // to the lamps; the FX balance is the day's colour of this place)
        const double Ysky = g_fxAtmo.active ? 0.2126 * g_fxAtmo.light[0] + 0.7152 * g_fxAtmo.light[1] + 0.0722 * g_fxAtmo.light[2] : Ybase;
        const double w = g_eLoc / (g_eLoc + std::max(Ysky, 1e-9));
        VECTOR3 g;
        for (int i = 0; i < 3; i++) g.data[i] = pow(std::max(g_eCol.data[i], 1e-3), -0.55);
        const double gl = 0.2126 * pow(std::max(g_eCol.x, 1e-3), 0.45) + 0.7152 * pow(std::max(g_eCol.y, 1e-3), 0.45) + 0.0722 * pow(std::max(g_eCol.z, 1e-3), 0.45);
        const double cur[3] = { adapt.x, adapt.y, adapt.z };
        if (kAdaptColour) for (int i = 0; i < 3; i++) tCol.data[i] = exp(w * (log(g.data[i] / std::max(gl, 1e-6)) - log(std::max(cur[i], 1e-6))));
    }
    // at most one stop darker for the local lights (a lamp's bright patch before the helmet must not put out the world:
    // the eye adapts partly, the user 20.10.2026), and as quick as an eye (~0.3 s)
    tExp = std::max(tExp, 0.5);
    // airless bodies (the Moon, Mercury, Phobos: no FX atmosphere adapting the eye, the exposure fixed at the Sun): in a
    // shade, a hall, a hangar the eye opens up - towards the sunlit regolith's luminance (albedo ~0.12 x the Sun ~ 0.08) from
    // the view's measured log-mean (the probe: what is seen, not the black sky), up to +12 stops (a lit room ~150 lux is ~10
    // under the Sun's 127 klx: +9 left a hall at its limit), never darker than the Sun's
    double up = 1, ev = 0;
    if (g_ok && cfg.on && g_inVol && g_lumOk && g_lumView > 0) {
        // inside a declared interior (D3D11Lights Volume), on any body: the physical camera (Frostbite 5.1) - the view's average
        // luminance as EV100 = log2(L 100 / 12.5) [L in cd/m2: the client's radiance x Esun / pi], held within the room's
        // [evMin, evMax], exposed so that average lands where the sunlit regolith does (0.08); the brightest 5 % kept unburnt
        const double Lp = g_lumView * g_Esun / 3.14159265;
        ev = log2(std::max(Lp, 1e-6) * 100 / 12.5);
        const double evc = std::clamp(ev, g_volEvMin, g_volEvMax);
        const double Leff = pow(2.0, evc) * 12.5 / 100 * 3.14159265 / g_Esun;
        up = 0.08 / Leff / std::max((double)adapt.w, 1e-6);
        if (g_lumP95 > 0) up = std::min(up, 0.35 / (g_lumP95 * std::max((double)adapt.w, 1e-6)));
        up = std::clamp(up, 1.0 / 256, 4096.0);
        tExp = 1;          // (the image's metering takes the local lights in)
    } else if (g_ok && cfg.on && g_airless && g_lumOk && g_lumView > 0) {
        up = 0.08 / g_lumView;
        // ... but the lit parts not burnt out: the brightest 5 % of the view kept under ~0.35 before the tone curve (its
        // shoulder ~ 1 at x1.6) - a lamp-lit wall stays a wall, the dark corners stay dark (as a camera's metering does)
        if (g_lumP95 > 0) up = std::min(up, 0.35 / g_lumP95);
        up = std::clamp(up, 1.0, 4096.0);
    }
    tExp *= up;
    const double k = 1 - exp(-dt / 0.3);     // the eye's reaction ~0.3 s (the user: seconds were far too slow, 21.10.2026)
    kExp = exp(log(kExp) + (log(tExp) - log(kExp)) * k);
    for (int i = 0; i < 3; i++) kCol.data[i] = exp(log(kCol.data[i]) + (log(tCol.data[i]) - log(kCol.data[i])) * k);
    if ((g_frame % 600) == 300) LOGC("local lights: the eye - view luminance %.3g, p95 %.3g (airless: x %.1f; interior %d, EV100 %.1f), E local %.3g (colour %.2f %.2f %.2f), exposure x %.3f, balance x %.2f %.2f %.2f (from %.2f %.2f %.2f)",
                                      g_lumView, g_lumP95, up, g_inVol ? 1 : 0, ev, g_eLoc, g_eCol.x, g_eCol.y, g_eCol.z, kExp, kCol.x, kCol.y, kCol.z, adapt.x, adapt.y, adapt.z);
    adapt.x *= (float)kCol.x; adapt.y *= (float)kCol.y; adapt.z *= (float)kCol.z;
    adapt.w *= (float)kExp; exposure *= (float)kExp;
}

// the eye's probe: after the whole scene (the camera's own cockpit included), 16 x 9 visible points read back two frames
// later (LightsFrame turns them into the local lights' illuminance over the view)
void D3D11Client::LightsProbe(ID3D11ShaderResourceView* img)
{
    if (!g_ok || !cfg.on || !g_csProbe || !g_probeUAV || !img) return;
    ResolveDepth();
    ID3D11ShaderResourceView* dsrv = depthSRV.Get();
    if (!dsrv) return;
    ctx->OMSetRenderTargets(0, nullptr, nullptr);      // the image may be the scene's own target
    ctx->CSSetShader(g_csProbe.Get(), nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
    ID3D11ShaderResourceView* in2[2] = { dsrv, img };
    ctx->CSSetShaderResources(0, 2, in2);
    ctx->CSSetUnorderedAccessViews(0, 1, g_probeUAV.GetAddressOf(), nullptr);
    ctx->Dispatch(1, 1, 1);
    ID3D11UnorderedAccessView* nu = nullptr; ID3D11ShaderResourceView* ns = nullptr;
    ID3D11ShaderResourceView* ns2[2] = {};
    ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr); ctx->CSSetShaderResources(0, 2, ns2); ctx->CSSetShader(nullptr, nullptr, 0);
    (void)ns;
    ctx->CopyResource(g_probeSt[g_probeN % 3].Get(), g_probe.Get());
    g_probeN++;
}
