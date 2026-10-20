// Rain drops on the suit's visor (shaders\visor_drops.hlsl). Titan's methane rain on the glass: the drops land where the
// rain's flux meets it (more on the front as she walks, all over it in flight), spread flat (surface tension 17 mN/m,
// a capillary length of ~5 mm), merge, slide down the glass along gravity's share (the head's tilt and its jolts
// included), are blown back at a walking pace by the dense air (4.4 x Earth's), leave beads on their trails and evaporate
// slowly. Simulated on the GPU in the glass's own coordinates (a 768 x 480 grid over the directions from the eye),
// the particles' impacts sampled here from the rain's rate (Fx.cfg Rain, the same area as Rain.cpp).
//   inside (the eye in the helmet): the drops on the glass 3-5 cm away are out of focus - soft lenses that bend and blur
//     the scene behind them, dark rims, the Sun and the lamps through them as soft discs. Drawn after the scene and
//     before the own vessel's pass: the visor's tint, the helmet display's plates and the helmet's rim come over them.
//   outside: small drops on the glass of every suit in the rain (procedural, fixed to the glass), after the transparent
//     vessels and before the air.
// Client.cfg: VisorDrops 0/1 (on; Launchpad > Video > Advanced), VisorDropsInside 0/1, VisorDropsOutside 0/1, VisorDropsPupil <mm> (0.8: the eye's
// pupil, the defocus), VisorDropsRefraction <scale> (1), VisorDropsGlint <scale> (1), VisorDropsPin <cos r - cos a>
// (0.03: the contact line's hysteresis on the coating), VisorDropsWind <m/s> (0.6: the
// breeze at the ground, Huygens ~0.3-1 m/s: the rain slants onto the front of the glass), VisorDropsRain <scale> (1). The GPU time: a line in Orbiter.log every 10 s.
#include "Client.h"
#include "Fx.h"

namespace {

constexpr UINT GW = 768, GH = 480, GRIDC = 16, NDROP = 4096, MAXSPAWN = 512, SCENEMIPS = 5;
constexpr double RV = 0.13;                 // the glass's sphere [m] (Suit2's VisorB: +-13.6 cm wide, 2.6 cm before the eye) ...
constexpr double CZ = -0.10;                // ... its centre behind the eye [m]
constexpr double AZMAX = 100 * RAD, ELTOP = 85 * RAD, ELBOT = -60 * RAD;   // the glass seen from the eye (the suit's frame)
// liquid methane-ethane at 94 K; Titan's air. Methane wets bare glass (~20 deg: flat, nearly invisible lenses); the
// visor's oleophobic coating (a 30th-century fluorinated film) beads it at ~55 deg, with little hysteresis
constexpr double SIGMA = 0.017, RHO_L = 450, MU = 1.8e-4, RHO_AIR = 5.4, CONTACT = 55 * RAD, LCAP_MM = 5.0;
constexpr double EVAP_DROP = 0.002;         // mm2/s: a 4 mm drop lasts ~10 min
constexpr double EVAP_FILM = 0.0003;        // mm/s: the beads (0.05-0.2 mm) ~5 min

struct Cfg { bool loaded = false, on = true, inside = true, outside = true; double pupil = 0.8, refr = 1, glint = 1, pin = 0.03, rain = 1, wind = 0.6; } cfg;
void LoadCfg()
{
    cfg.loaded = true;
    FILE* f = fopen("Config\\D3D11Client\\Client.cfg", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char* sc = strchr(line, ';'); if (sc) *sc = 0;
            char key[64]; double v;
            if (sscanf(line, " %63[A-Za-z] = %lf", key, &v) != 2) continue;
            std::string k = key;
            if (k == "VisorDrops") cfg.on = v != 0;
            else if (k == "VisorDropsInside") cfg.inside = v != 0;
            else if (k == "VisorDropsOutside") cfg.outside = v != 0;
            else if (k == "VisorDropsPupil") cfg.pupil = std::clamp(v, 0.5, 9.0);
            else if (k == "VisorDropsRefraction") cfg.refr = std::clamp(v, 0.0, 5.0);
            else if (k == "VisorDropsGlint") cfg.glint = std::clamp(v, 0.0, 10.0);
            else if (k == "VisorDropsPin") cfg.pin = std::clamp(v, 0.0, 1.0);
            else if (k == "VisorDropsRain") cfg.rain = std::clamp(v, 0.0, 100.0);
            else if (k == "VisorDropsWind") cfg.wind = std::clamp(v, 0.0, 20.0);
        }
        fclose(f);
    }
    LOGC("visor drops: %s (inside %d, outside %d, pupil %.1f mm, refraction %.2f, glint %.2f, pinning %.2f, rain x%.2f)",
         cfg.on ? "on" : "off (VisorDrops = 0)", cfg.inside, cfg.outside, cfg.pupil, cfg.refr, cfg.glint, cfg.pin, cfg.rain);
}

// the rain where the camera is: Fx.cfg Rain [mm/h] over RainArea on Titan, as Rain.cpp reads them (its own copy: that
// file is the rain's, this one only follows it)
double rRate = -1, rLat = 59.3335, rLon = 40.49, rRad = 60.0, rFade = 15.0;
double RainAt(OBJHANDLE hP, const VECTOR3& cp)
{
    if (rRate < 0) {
        rRate = 0;
        for (const char* fn : { "Config\\D3D11Client\\Fx.cfg", "Config\\D3D11Client\\Client.cfg" })
            if (FILE* f = fopen(fn, "r")) {
                char line[256];
                while (fgets(line, sizeof line, f)) {
                    double v, a, b, c, d;
                    if (sscanf(line, " Rain = %lf", &v) == 1) rRate = std::max(0.0, v);
                    if (sscanf(line, " RainArea = %lf %lf %lf %lf", &a, &b, &c, &d) == 4) { rLat = a; rLon = b; rRad = std::max(c, 0.1); rFade = std::max(d, 0.01); }
                }
                fclose(f);
            }
        char e[32] = {};
        if (GetEnvironmentVariableA("D3D11CLIENT_RAIN", e, sizeof e)) rRate = std::max(0.0, atof(e));
    }
    if (rRate <= 0 || !hP) return 0;
    char nm[32] = ""; oapiGetObjectName(hP, nm, 31);
    if (_stricmp(nm, "Titan")) return 0;
    double lng, lat, rad; oapiGlobalToEqu(hP, cp, &lng, &lat, &rad);
    const double R = oapiGetSize(hP);
    if (rad - R > 3000) return 0;
    const double la0 = rLat * RAD, lo0 = rLon * RAD;
    const double c = sin(lat) * sin(la0) + cos(lat) * cos(la0) * cos(lng - lo0);
    const double dkm = R * acos(std::clamp(c, -1.0, 1.0)) / 1000.0;
    return rRate * std::clamp((rRad - dkm) / rFade, 0.0, 1.0) * cfg.rain;
}
// the drops' mean diameter at that rate (Rain.cpp's spectrum) and a drop's terminal speed in Titan's air (rain.hlsl)
double DropMean(double I) { return std::clamp(0.4e-3 * pow(std::max(I, 0.01), 0.21), 0.15e-3, 2.5e-3); }
double Vt(double d)
{
    const double vN = sqrt(4 * 1.352 * d * (450 - 5.4) / (3 * 0.6 * 5.4));
    const double vS = (450 - 5.4) * 1.352 * d * d / (18 * 6.2e-6);
    return std::min(vN, vS);
}

unsigned rng = 0x9E3779B9u;
double U01() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0; }

struct VCB { XMFLOAT4 grid, geo, center, acc, air, phys, phys2, misc, camX, camY, camZ, proj, light, cR, cU, cF; };
struct OCB { XMFLOAT4X4 wvp, w; XMFLOAT4 wet; };
struct DropCPU { float d[12]; };     // 48 bytes (the shader's Drop)

struct Res
{
    ID3D11Device* dev = nullptr;
    bool tried = false, ok = false;
    ComPtr<ID3D11ComputeShader> csClear, csInsert, csUpdate, csFinal, csSpawn, csFilm;
    ComPtr<ID3D11VertexShader> vsDrop, vsFull, vsOut;
    ComPtr<ID3D11PixelShader> psWipe, psBead, psCap, psCopyFilm, psLens, psBlurH, psBlurV, psDown, psDownMS, psInside, psOut;
    ComPtr<ID3D11InputLayout> ilOut;
    ComPtr<ID3D11Buffer> cb, ocb, drops, volAdd, count, spawn, countRead[4];
    ComPtr<ID3D11UnorderedAccessView> dropsUAV, volUAV, countUAV, gridUAV, filmUAV;
    ComPtr<ID3D11ShaderResourceView> dropsSRV, spawnSRV, filmSRV, hSRV, lensSRV, lensBSRV, sceneSRV;
    ComPtr<ID3D11Texture2D> grid, film, hTex, lens, lensB, scene;
    ComPtr<ID3D11RenderTargetView> filmRTV, hRTV, lensRTV, lensBRTV, sceneRTV;
    ComPtr<ID3D11BlendState> bsMul, bsMax, bsPremul, bsNone;
    ComPtr<ID3D11SamplerState> sLin;
    UINT sceneW = 0, sceneH = 0;
    ComPtr<ID3D11Query> qDis[4], qTs[4][8];
} R;

// the simulation's state on the CPU
struct Sim
{
    OBJHANDLE focus = nullptr;
    UINT ring = 0;
    double acc = 0;                 // impacts owed (fractional)
    double lastWet = -1e9;          // sim time of the last impact
    VECTOR3 prevRel = { 0, 0, 0 }, prevVel = { 0, 0, 0 }, aCam = { 0, 0, 0 }; int velN = 0;
    bool submerged = false, flood = false, reset = true;
    double impactsPerS = 0, airSpeed = 0, rain = 0;
    UINT alive = 0;
    bool insideDrawn = false;
    std::vector<XMFLOAT4> spawns;
} S;

std::map<OBJHANDLE, double> wetOut;              // the outside view: each suit's glass wetness 0..1
struct RestVB { ComPtr<ID3D11Buffer> vb; ID3D11Buffer* meshVB = nullptr; DWORD used = 0; };
std::map<const DevMesh*, RestVB> restVB;         // the glass groups' rest positions (the drops stay on the glass)

UINT frameK = 0;                                 // the timing ring
double msOut = 0, msIn = 0, msPart[4] = {};    // the inside's parts: simulation, lens, scene copy, the view
bool partsOk[4] = {};
bool outTimed = false;
LARGE_INTEGER tLogV = {};

bool IsCrew(VESSEL* v) { const char* c = v ? v->GetClassName() : nullptr; return c && !_strnicmp(c, "OrbiterCrew", 11); }
bool GlassGroup(const DevMesh* m, const DGroup& g)
{
    if (!g.ni || (g.usrFlag & (4 | 8 | 0x100))) return false;
    if (g.mtrl == (DWORD)-1 || g.mtrl >= m->mtrl.size()) return false;
    const float a = m->mtrl[g.mtrl].diffuse.a;
    return a > 0.01f && a < 0.99f;
}

ComPtr<ID3D11Buffer> MakeSB(ID3D11Device* dev, UINT stride, UINT n, bool uav, bool dyn, const void* init = nullptr)
{
    D3D11_BUFFER_DESC bd = { stride * n, dyn ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0u),
                             dyn ? D3D11_CPU_ACCESS_WRITE : 0u, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, stride };
    D3D11_SUBRESOURCE_DATA sd = { init, 0, 0 };
    ComPtr<ID3D11Buffer> b; dev->CreateBuffer(&bd, init ? &sd : nullptr, &b); return b;
}
bool MakeTex(ID3D11Device* dev, UINT w, UINT h, DXGI_FORMAT f, UINT mips, bool uav, ComPtr<ID3D11Texture2D>& t, ComPtr<ID3D11ShaderResourceView>* srv, ComPtr<ID3D11RenderTargetView>* rtv)
{
    D3D11_TEXTURE2D_DESC td = { w, h, mips, 1, f, { 1, 0 }, D3D11_USAGE_DEFAULT,
                                (srv ? D3D11_BIND_SHADER_RESOURCE : 0u) | (rtv ? D3D11_BIND_RENDER_TARGET : 0u) | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0u), 0,
                                mips > 1 ? (UINT)D3D11_RESOURCE_MISC_GENERATE_MIPS : 0u };
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &t))) return false;
    if (srv) dev->CreateShaderResourceView(t.Get(), nullptr, &*srv);
    if (rtv) { D3D11_RENDER_TARGET_VIEW_DESC rd = {}; rd.Format = f; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; dev->CreateRenderTargetView(t.Get(), &rd, &*rtv); }
    return true;
}

}   // namespace

// ------------------------------------------------------------------------------------------ resources
static bool VisorInit(D3D11Client* c, ID3D11Device* dev)
{
    if (R.dev != dev) { R = Res(); R.dev = dev; S = Sim(); restVB.clear(); }
    if (R.tried) return R.ok;
    R.tried = true;
    if (!cfg.loaded) LoadCfg();
    auto blob = [&](const char* e, const char* t) { return c->CompileShaderFile("visor_drops.hlsl", e, t); };
    bool ok = true;
    auto cs = [&](const char* e, ComPtr<ID3D11ComputeShader>& s) { auto b = blob(e, "cs_5_0"); if (!b) { ok = false; return; } dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s); };
    auto ps = [&](const char* e, ComPtr<ID3D11PixelShader>& s) { auto b = blob(e, "ps_5_0"); if (!b) { ok = false; return; } dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s); };
    auto vs = [&](const char* e, ComPtr<ID3D11VertexShader>& s) { auto b = blob(e, "vs_5_0"); if (!b) { ok = false; return b; } dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s); return b; };
    cs("CSClearGrid", R.csClear); cs("CSInsert", R.csInsert); cs("CSUpdate", R.csUpdate); cs("CSFinalize", R.csFinal); cs("CSSpawn", R.csSpawn); cs("CSFilm", R.csFilm);
    vs("VSDrop", R.vsDrop); vs("VSFull", R.vsFull);
    auto vo = vs("VSOut", R.vsOut);
    ps("PSWipe", R.psWipe); ps("PSBead", R.psBead); ps("PSCap", R.psCap); ps("PSCopyFilm", R.psCopyFilm); ps("PSLens", R.psLens); ps("PSBlurH", R.psBlurH); ps("PSBlurV", R.psBlurV);
    ps("PSDown", R.psDown); ps("PSDownMS", R.psDownMS); ps("PSInside", R.psInside); ps("PSOut", R.psOut);
    if (!ok) { LOGC("visor drops: shaders FAILED (visor_drops.hlsl)"); return false; }
    D3D11_INPUT_ELEMENT_DESC el[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    dev->CreateInputLayout(el, 3, vo->GetBufferPointer(), vo->GetBufferSize(), &R.ilOut);
    { D3D11_BUFFER_DESC bd = { sizeof(VCB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 }; dev->CreateBuffer(&bd, nullptr, &R.cb); }
    { D3D11_BUFFER_DESC bd = { sizeof(OCB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 }; dev->CreateBuffer(&bd, nullptr, &R.ocb); }
    std::vector<DropCPU> zero(NDROP); memset(zero.data(), 0, zero.size() * sizeof(DropCPU));
    R.drops = MakeSB(dev, sizeof(DropCPU), NDROP, true, false, zero.data());
    std::vector<UINT> z(NDROP, 0);
    R.volAdd = MakeSB(dev, 4, NDROP, true, false, z.data());
    R.count = MakeSB(dev, 4, 4, true, false, z.data());
    R.spawn = MakeSB(dev, 16, MAXSPAWN, false, true);
    if (!R.drops || !R.volAdd || !R.count || !R.spawn) { LOGC("visor drops: buffers FAILED"); return false; }
    dev->CreateUnorderedAccessView(R.drops.Get(), nullptr, &R.dropsUAV);
    dev->CreateShaderResourceView(R.drops.Get(), nullptr, &R.dropsSRV);
    dev->CreateUnorderedAccessView(R.volAdd.Get(), nullptr, &R.volUAV);
    dev->CreateUnorderedAccessView(R.count.Get(), nullptr, &R.countUAV);
    dev->CreateShaderResourceView(R.spawn.Get(), nullptr, &R.spawnSRV);
    for (auto& b : R.countRead) { D3D11_BUFFER_DESC bd = { 16, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0 }; dev->CreateBuffer(&bd, nullptr, &b); }
    if (!MakeTex(dev, GW / GRIDC, GH / GRIDC, DXGI_FORMAT_R32_UINT, 1, true, R.grid, nullptr, nullptr)) return false;
    dev->CreateUnorderedAccessView(R.grid.Get(), nullptr, &R.gridUAV);
    if (!MakeTex(dev, GW, GH, DXGI_FORMAT_R32_FLOAT, 1, true, R.film, &R.filmSRV, &R.filmRTV)) return false;
    dev->CreateUnorderedAccessView(R.film.Get(), nullptr, &R.filmUAV);
    if (!MakeTex(dev, GW, GH, DXGI_FORMAT_R16_FLOAT, 1, false, R.hTex, &R.hSRV, &R.hRTV)) return false;
    if (!MakeTex(dev, GW, GH, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, false, R.lens, &R.lensSRV, &R.lensRTV)) return false;
    if (!MakeTex(dev, GW, GH, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, false, R.lensB, &R.lensBSRV, &R.lensBRTV)) return false;
    auto bs = [&](D3D11_BLEND src, D3D11_BLEND dst, D3D11_BLEND_OP op, ComPtr<ID3D11BlendState>& s, bool on = true) {
        D3D11_BLEND_DESC b = {}; auto& t = b.RenderTarget[0];
        t.BlendEnable = on; t.SrcBlend = src; t.DestBlend = dst; t.BlendOp = op;
        t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; dev->CreateBlendState(&b, &s);
    };
    bs(D3D11_BLEND_ZERO, D3D11_BLEND_SRC_COLOR, D3D11_BLEND_OP_ADD, R.bsMul);
    bs(D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_MAX, R.bsMax);
    bs(D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, R.bsPremul);
    bs(D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, R.bsNone, false);
    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    dev->CreateSamplerState(&sd, &R.sLin);
    for (int i = 0; i < 4; i++) {
        D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 }; dev->CreateQuery(&qd, &R.qDis[i]);
        qd.Query = D3D11_QUERY_TIMESTAMP; for (int k = 0; k < 8; k++) dev->CreateQuery(&qd, &R.qTs[i][k]);
    }
    float clr[4] = { 0, 0, 0, 0 };
    c->Ctx()->ClearRenderTargetView(R.filmRTV.Get(), clr);
    R.ok = true;
    LOGC("visor drops: ready (%u x %u glass grid, %u drops)", GW, GH, NDROP);
    return true;
}

static void SetCB(ID3D11DeviceContext* ctx, const VCB& k)
{
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(ctx->Map(R.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { memcpy(ms.pData, &k, sizeof k); ctx->Unmap(R.cb.Get(), 0); }
}

// ------------------------------------------------------------------------------------------ outside: the suits' glass
void D3D11Client::RenderVisorDropsOutside(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    if (!cfg.loaded) LoadCfg();
    if (!cfg.on || !g_cfg.visorDrops) return;     // Client.cfg VisorDrops (Launchpad > Video > Advanced)
    if (!VisorInit(this, dev.Get())) return;
    const UINT fi = frameK % 4;
    ctx->Begin(R.qDis[fi].Get());
    ctx->End(R.qTs[fi][0].Get());
    outTimed = true;
    struct TsEnd { ID3D11DeviceContext* c; UINT fi; ~TsEnd() { c->End(R.qTs[fi][1].Get()); } } tsEnd{ ctx.Get(), fi };
    if (!cfg.outside) return;
    OBJHANDLE hP = oapiCameraProxyGbody();
    const double I = RainAt(hP, cp);
    const double dt = std::min(oapiGetSimStep(), 0.1);
    // each suit's glass wets in ~40 s of rain (more in a downpour), dries over ~10 min
    for (auto& kv : visuals) {
        if (!IsCrew(kv.second->v)) continue;
        double& w = wetOut[kv.first];
        const double target = I > 0 ? std::clamp(0.35 + 0.2 * I, 0.35, 1.0) : 0.0;
        w += (target - w) * std::min(1.0, dt / (target > w ? 40.0 : 600.0));
    }
    for (auto it = wetOut.begin(); it != wetOut.end();) it = visuals.count(it->first) ? std::next(it) : wetOut.erase(it);
    const OBJHANDLE own = oapiCameraInternal() ? oapiGetFocusObject() : nullptr;
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    const XMMATRIX VP = V * proj;
    bool bound = false;
    for (auto& kv : visuals) {
        auto wi = wetOut.find(kv.first);
        if (wi == wetOut.end() || wi->second < 0.02 || kv.first == own) continue;
        VesselVis* vv = kv.second;
        if (!vv->v) continue;
        VECTOR3 gp; vv->v->GetGlobalPos(gp);
        MATRIX3 Rm; vv->v->GetRotationMatrix(Rm);
        const VECTOR3 rel = gp - cp;
        if (length(rel) > 60) continue;           // a few pixels' drops: none beyond
        for (UINT mi = 0; mi < vv->mesh.size(); mi++) {
            DevMesh* m = vv->mesh[mi];
            if (!m || !m->vb || !(vv->v->GetMeshVisibilityMode(mi) & MESHVIS_EXTERNAL)) continue;
            bool any = false;
            for (const DGroup& g : m->grp) if (!(g.usrFlag & 2) && GlassGroup(m, g)) { any = true; break; }
            if (!any) continue;
            // the glass's rest positions: its vertices as first seen (the skin moves them; the drops stay where they fell)
            RestVB& rb = restVB[m];
            if (!rb.vb || rb.meshVB != m->vb.Get()) {
                UINT nv = 0; for (const DGroup& g : m->grp) nv = std::max(nv, g.vb0 + g.nv);
                std::vector<XMFLOAT3> rp(std::max(nv, 1u), XMFLOAT3(0, 0, 0));
                for (const DGroup& g : m->grp) if (GlassGroup(m, g)) for (UINT k = 0; k < g.nv && k < g.vtx.size(); k++) rp[g.vb0 + k] = XMFLOAT3(g.vtx[k].x, g.vtx[k].y, g.vtx[k].z);
                D3D11_BUFFER_DESC bd = { (UINT)(rp.size() * sizeof(XMFLOAT3)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
                D3D11_SUBRESOURCE_DATA sd = { rp.data(), 0, 0 };
                rb.vb.Reset(); dev->CreateBuffer(&bd, &sd, &rb.vb); rb.meshVB = m->vb.Get();
            }
            rb.used = frameNo;
            if (!rb.vb) continue;
            if (!bound) {
                bound = true;
                ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
                ctx->OMSetBlendState(R.bsPremul.Get(), nullptr, ~0u);
                ctx->OMSetDepthStencilState(dsRead.Get(), 0);
                ctx->RSSetState(rsBack.Get());
                ctx->IASetInputLayout(R.ilOut.Get());
                ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                ctx->VSSetShader(R.vsOut.Get(), nullptr, 0); ctx->PSSetShader(R.psOut.Get(), nullptr, 0);
                ctx->VSSetConstantBuffers(0, 1, cbFrame.GetAddressOf()); ctx->PSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
                ctx->VSSetConstantBuffers(2, 1, R.ocb.GetAddressOf()); ctx->PSSetConstantBuffers(2, 1, R.ocb.GetAddressOf());
                FxBindLight(this);
                LightsBind();
                ctx->PSSetShaderResources(7, 1, shadowSRV.GetAddressOf());
                ID3D11SamplerState* s2 = sampShadow.Get(); ctx->PSSetSamplers(2, 1, &s2);
            }
            VECTOR3 ofs = _V(0, 0, 0); vv->v->GetMeshOffset(mi, ofs);
            const VECTOR3 t = rel + mul(Rm, ofs);
            XMMATRIX W0 = RotToXM(Rm); W0.r[3] = XMVectorSet((float)t.x, (float)t.y, (float)t.z, 1);
            ID3D11Buffer* vbs[2] = { m->vb.Get(), rb.vb.Get() }; UINT st[2] = { sizeof(NTVERTEX), sizeof(XMFLOAT3) }, of[2] = { 0, 0 };
            ctx->IASetVertexBuffers(0, 2, vbs, st, of);
            ctx->IASetIndexBuffer(m->ib.Get(), DXGI_FORMAT_R16_UINT, 0);
            for (const DGroup& g : m->grp) {
                if ((g.usrFlag & 2) || !GlassGroup(m, g)) continue;
                const XMMATRIX W = XMLoadFloat4x4(&g.xf) * W0;
                OCB k;
                XMStoreFloat4x4(&k.wvp, XMMatrixTranspose(W * VP));
                XMStoreFloat4x4(&k.w, XMMatrixTranspose(W));
                k.wet = { (float)wi->second, (float)fmod(oapiGetSimTime(), 3600.0), 0.0035f, (float)(((uintptr_t)kv.first >> 4) & 0xFFFF) };
                D3D11_MAPPED_SUBRESOURCE ms;
                if (SUCCEEDED(ctx->Map(R.ocb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { memcpy(ms.pData, &k, sizeof k); ctx->Unmap(R.ocb.Get(), 0); }
                ctx->DrawIndexed(g.ni, g.ib0, g.vb0);
            }
        }
    }
    if (bound) {
        ID3D11Buffer* nb[2] = {}; UINT z[2] = {};
        ctx->IASetVertexBuffers(0, 2, nb, z, z);
        ctx->OMSetBlendState(bsAlpha.Get(), nullptr, ~0u);
    }
    if ((frameNo & 255) == 0)      // the rest buffers of meshes gone
        for (auto it = restVB.begin(); it != restVB.end();) it = frameNo - it->second.used > 600 ? restVB.erase(it) : std::next(it);
}

// ------------------------------------------------------------------------------------------ inside: the simulation and the view
void D3D11Client::RenderVisorDropsInside(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    if (!cfg.loaded) LoadCfg();
    if (!cfg.on || !g_cfg.visorDrops || !R.ok) return;
    const UINT fi = frameK % 4;
    ctx->End(R.qTs[fi][2].Get());
    struct TsEnd { ID3D11DeviceContext* c; UINT fi; ~TsEnd() { c->End(R.qTs[fi][3].Get()); c->End(R.qDis[fi].Get()); } };
    {
        TsEnd tsEnd{ ctx.Get(), fi };
        [&]() {
    // whose glass: the focus vessel, a crew member in a suit (a glass group on her meshes, or the helmet display's plates
    // - a VC-only mesh whose group 0 is the visor's sun filter, flag 0x100)
    const OBJHANDLE hF = oapiGetFocusObject();
    auto vit = visuals.find(hF);
    VesselVis* vv = vit != visuals.end() ? vit->second : nullptr;
    // OrbiterCrew sets UsrFlag 0x400 on the plate's group 0 while the glass is really before the eyes (suit on, helmet
    // closed, her own eyes); once seen, only that counts (an open helmet has the plate too)
    static bool closedFlagKnown = false;
    bool suited = false, plate = false, closed = false;
    if (vv && IsCrew(vv->v)) {
        for (UINT mi = 0; mi < vv->mesh.size(); mi++) {
            DevMesh* m = vv->mesh[mi];
            if (!m) continue;
            const WORD mode = vv->v->GetMeshVisibilityMode(mi);
            if ((mode & MESHVIS_VC) && !(mode & MESHVIS_EXTERNAL) && !m->grp.empty() && (m->grp[0].usrFlag & 0x100)) {
                plate = true;
                if (m->grp[0].usrFlag & 0x400) closed = closedFlagKnown = true;
            }
            for (const DGroup& g : m->grp) if (GlassGroup(m, g)) { suited = true; break; }
        }
        suited = suited || plate;
    }
    if (!suited) { S.focus = nullptr; return; }
    if (S.focus != hF) { S = Sim(); S.focus = hF; }
    const bool firstPerson = cfg.inside && oapiCameraInternal() && (!closedFlagKnown || closed);
    OBJHANDLE hP = oapiCameraProxyGbody();
    const double I = RainAt(hP, cp);
    S.rain = I;
    const double simT = oapiGetSimTime();
    // the liquid round the head: under it the glass is all wet; coming out, it drains (a flood of large drops)
    bool sub = false;
    if (hP) {
        VECTOR3 pp; oapiGetGlobalPos(hP, &pp); MATRIX3 Rp; oapiGetRotationMatrix(hP, &Rp);
        const double Rpl = oapiGetSize(hP);
        for (Planet* p : planets) if (p->hObj == hP && p->liq.has) sub = length(cp - pp) < Rpl + p->liq.level + 0.02;
    }
    if (S.submerged && !sub) S.flood = true;
    S.submerged = sub;
    const bool active = I > 0 || S.flood || simT - S.lastWet < 1500 || S.alive > 0 || S.reset;
    if (!active || sub) return;

    // ---- the forces' inputs, in the camera's frame (x right, y up, z forward: the helmet turns with the view)
    const double dt = std::clamp(oapiGetSimStep(), 0.0, 0.05);
    VECTOR3 up = _V(0, 1, 0), pp = _V(0, 0, 0); double gAcc = 0;
    if (hP) { oapiGetGlobalPos(hP, &pp); up = (cp - pp) / length(cp - pp); gAcc = 6.674e-11 * oapiGetMass(hP) / dotp(cp - pp, cp - pp); }
    VECTOR3 airV = _V(0, 0, 0);                                 // the suit's velocity through the air (global)
    if (vv->v) vv->v->GetAirspeedVector(FRAME_GLOBAL, airV);
    if (testMode) {   // test runs: -Set "VisorDropsTestAir = <m/s>" - as if walking or flying forward (no player at the keys)
        double FxTestNum(const char* key);
        static const double ta = FxTestNum("VisorDropsTestAir");
        if (ta != 0) airV = airV + _V(cr.m13, cr.m23, cr.m33) * ta;
    }
    S.airSpeed = length(airV);
    // the head's acceleration (its jolts as she walks): the camera's path relative to the planet, smoothed
    if (dt > 1e-4) {
        const VECTOR3 rel = cp - pp;
        if (S.velN > 0) {
            const VECTOR3 vel = (rel - S.prevRel) / dt;
            if (S.velN > 1) {
                VECTOR3 a = (vel - S.prevVel) / dt;
                const double am = length(a); if (am > 25) a = a * (25 / am);
                S.aCam = S.aCam + (a - S.aCam) * 0.3;
            }
            S.prevVel = vel;
        }
        S.prevRel = rel; S.velN = std::min(S.velN + 1, 3);
    }
    // the breeze (Titan's near-surface wind, gusting and veering over minutes), horizontal: the rain drifts with it
    if (cfg.wind > 0) {
        VECTOR3 e1 = crossp(up, fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); e1 = e1 / length(e1);
        const VECTOR3 e2 = crossp(up, e1);
        const double a = 0.6 + 1.5 * sin(simT * 0.021), g = 1 + 0.35 * sin(simT * 0.53) * sin(simT * 0.21 + 1);
        airV = airV - (e1 * cos(a) + e2 * sin(a)) * (cfg.wind * g);
    }
    // the glass is the suit's (the helmet turns with the body): everything in its frame - the camera's turning only
    // moves the gaze over it
    MATRIX3 Rs; vv->v->GetRotationMatrix(Rs);
    const VECTOR3 gEff = -up * gAcc - S.aCam;
    auto toSuit = [&](const VECTOR3& v) { return tmul(Rs, v); };
    const VECTOR3 gC = toSuit(gEff), airC = toSuit(-airV), downC = toSuit(-up);

    // ---- impacts: the rain's flux through the glass (each patch's normal against the drops' velocity relative to it)
    S.spawns.clear();
    const double rv_mm = RV * 1000;
    // the glass as the shader maps it (visor_drops.hlsl Glass): texel -> the direction from the eye -> the sphere
    auto dirOf = [](double az, double el) { return _V(cos(el) * sin(az), sin(el), cos(el) * cos(az)); };
    auto hit = [&](const VECTOR3& d) { const VECTOR3 c = _V(0, 0, CZ * 1000); const double b = dotp(d, c); return d * (b + sqrt(std::max(b * b - dotp(c, c) + rv_mm * rv_mm, 0.0))); };
    struct GP { VECTOR3 n, eu, ev; double mmU, mmV, dA; };
    auto glass = [&](double u, double v) {      // u, v in texels
        const double az = (u / GW * 2 - 1) * AZMAX, el = ELTOP + (ELBOT - ELTOP) * v / GH, e = 2e-3;
        const VECTOR3 P = hit(dirOf(az, el));
        const VECTOR3 du = (hit(dirOf(az + e, el)) - hit(dirOf(az - e, el))) / (2 * e), dv = (hit(dirOf(az, el + e)) - hit(dirOf(az, el - e))) / (2 * e);
        GP g; g.n = P - _V(0, 0, CZ * 1000); g.n = g.n / length(g.n);
        g.eu = du / length(du); g.ev = -dv / length(dv);
        g.mmU = length(du) * 2 * AZMAX / GW; g.mmV = length(dv) * (ELTOP - ELBOT) / GH;
        g.dA = length(crossp(du, dv)) * 1e-6;     // m2 per rad2
        return g;
    };
    auto addDrop = [&](float x, float y, double volmm3) {
        if (S.spawns.size() >= MAXSPAWN || x < 1 || y < 1 || x > GW - 2 || y > GH - 2) return;
        if (volmm3 < 0.02) { S.spawns.push_back({ x, y, (float)volmm3, -1.0f }); return; }   // a speck: a bead in the film
        S.spawns.push_back({ x, y, (float)std::min(volmm3, 400.0), (float)(S.ring++ % NDROP) });
    };
    if (S.reset) {   // a new glass: no drops yet (the GPU's buffer still holds the last glass's)
        float clr[4] = { 0, 0, 0, 0 }; ctx->ClearRenderTargetView(R.filmRTV.Get(), clr);
        std::vector<DropCPU> zero(NDROP); memset(zero.data(), 0, zero.size() * sizeof(DropCPU));
        ctx->UpdateSubresource(R.drops.Get(), 0, nullptr, zero.data(), 0, 0);
        S.reset = false;
    }
    if (I > 0 && dt > 0) {
        const double dMean = DropMean(I);
        const double q = I / 3.6e6;                                  // m3 of rain per m2 per s
        const double Vmean = PI * dMean * dMean * dMean;             // <pi d^3 / 6> for an exponential spectrum
        const double nd = q / (Vmean * Vt(1.5 * dMean));             // drops per m3 of air
        const VECTOR3 vrel = downC * Vt(1.5 * dMean) + airC;         // the drops relative to the glass (mean size)
        double flux = 0, wmax = 1e-12;
        for (int j = 0; j < 20; j++) for (int i = 0; i < 32; i++) {
            const GP g = glass((i + 0.5) / 32 * GW, (j + 0.5) / 20 * GH);
            const double w = std::max(0.0, -dotp(vrel, g.n)) * g.dA;
            flux += w; wmax = std::max(wmax, w);
        }
        flux *= (2 * AZMAX / 32) * ((ELTOP - ELBOT) / 20);          // m3/s swept (per unit density)
        S.impactsPerS = nd * flux;
        if (testMode) { static int nDiag = 0; if (nDiag++ < 3) { const GP g0 = glass(GW * 0.5, GH * 0.3);
            LOGC("visor drops diag: dt %.4f I %.2f nd %.3g vrel %.3f %.3f %.3f n %.3f %.3f %.3f dA %.3g mm %.3f %.3f flux %.3g", dt, I, nd, vrel.x, vrel.y, vrel.z, g0.n.x, g0.n.y, g0.n.z, g0.dA, g0.mmU, g0.mmV, flux); } }
        S.acc += nd * flux * dt * (0.5 + U01());
        wmax *= 1.3;
        while (S.acc >= 1 && S.spawns.size() < MAXSPAWN - 12) {
            S.acc -= 1;
            double u = 0, v = 0; GP g = {}; bool hitOk = false;
            for (int t = 0; t < 16 && !hitOk; t++) {                 // the place: rejection-sampled by the flux on the area
                u = U01() * GW; v = U01() * GH;
                g = glass(u, v);
                hitOk = U01() * wmax < -dotp(vrel, g.n) * g.dA;
            }
            if (!hitOk) continue;
            const VECTOR3 n = g.n;
            const double d = std::min(-log(std::max(U01(), 1e-4)) * dMean, 9.5e-3);
            const VECTOR3 vr = downC * Vt(d) + airC;
            const double un = -dotp(vr, n);
            if (un <= 0) continue;
            const double vol = PI * d * d * d / 6 * 1e9;             // mm3
            const XMFLOAT2 tp((float)u, (float)v);
            // a splash (Mundo: K = We^1/2 Re^1/4 > 57): low surface tension - methane splashes at a walk for the large drops
            const double We = RHO_L * d * un * un / SIGMA, Re = RHO_L * un * d / MU;
            const double K = sqrt(We) * pow(Re, 0.25);
            if (K > 57) {
                const int ns = (int)std::clamp(K / 12, 3.0, 10.0);
                addDrop(tp.x, tp.y, vol * 0.35);
                const VECTOR3 tv = vr + n * un;                       // the tangential part: the crown's lean
                const double txu = dotp(tv, g.eu), txv = dotp(tv, g.ev);
                const double ang0 = atan2(txv, txu), lean = std::min(length(tv) / (un + 1e-3), 3.0);
                const double mmU = g.mmU, mmV = g.mmV;
                for (int k = 0; k < ns; k++) {
                    const double a = ang0 + (U01() - 0.5) * (2 * PI) / (1 + lean);
                    const double r = d * 1000 * (0.8 + 0.03 * K) * (0.3 + U01());
                    addDrop(tp.x + (float)(r * cos(a) / mmU), tp.y + (float)(r * sin(a) / mmV), vol * 0.65 / ns * (0.5 + U01()));
                }
            } else addDrop(tp.x, tp.y, vol);
        }
        if (!S.spawns.empty()) S.lastWet = simT;
    } else S.impactsPerS = 0;
    if (S.flood) {   // out of the liquid: the glass drains in large drops and sheets
        S.flood = false; S.lastWet = simT;
        for (int k = 0; k < 400 && S.spawns.size() < MAXSPAWN; k++) {
            const XMFLOAT2 tp((float)(U01() * GW), (float)(U01() * GH));
            addDrop(tp.x, tp.y, k < 250 ? 2 + 30 * U01() * U01() : 0.01);
        }
    }

    // ---- the constants
    VCB k = {};
    k.grid = { (float)GW, (float)GH, 1.0f / GW, 1.0f / GH };
    k.geo = { (float)rv_mm, (float)AZMAX, (float)ELTOP, (float)cfg.refr };
    k.center = { 0, 0, (float)CZ, (float)dt };
    k.acc = { (float)gC.x, (float)gC.y, (float)gC.z, (float)fmod(simT, 3600.0) };
    k.air = { (float)airC.x, (float)airC.y, (float)airC.z, (float)RHO_AIR };
    k.phys = { (float)SIGMA, (float)RHO_L, (float)MU, (float)cfg.pin };
    k.phys2 = { (float)CONTACT, (float)LCAP_MM, (float)EVAP_DROP, (float)EVAP_FILM };
    k.misc = { (float)S.spawns.size(), 0, (float)(frameNo & 0xFFFFF), (float)cfg.pupil };
    k.camX = { (float)Rs.m11, (float)Rs.m21, (float)Rs.m31, 0 };     // the suit's axes (global)
    k.camY = { (float)Rs.m12, (float)Rs.m22, (float)Rs.m32, 0 };
    k.camZ = { (float)Rs.m13, (float)Rs.m23, (float)Rs.m33, 0 };
    {   // the camera's axes in the suit's frame
        const VECTOR3 a = toSuit(_V(cr.m11, cr.m21, cr.m31)), b = toSuit(_V(cr.m12, cr.m22, cr.m32)), c = toSuit(_V(cr.m13, cr.m23, cr.m33));
        k.cR = { (float)a.x, (float)a.y, (float)a.z, 0 }; k.cU = { (float)b.x, (float)b.y, (float)b.z, 0 }; k.cF = { (float)c.x, (float)c.y, (float)c.z, 0 };
    }
    const float tanY = (float)tan(oapiCameraAperture());
    k.proj = { tanY * RW / std::max(RH, 1u), tanY, (float)SCENEMIPS, 0 };
    k.light = { (float)ELBOT, 0.035f, (float)cfg.glint, 1 };
    if (testMode) { double FxTestNum(const char* key); static const bool tv = FxTestNum("VisorDropsTestView") > 0.5; if (tv) k.light.w = 2; }   // test runs: the view pass's cost without its shading
    SetCB(ctx.Get(), k);
    ID3D11Buffer* cbs[1] = { R.cb.Get() };
    ctx->CSSetConstantBuffers(1, 1, cbs); ctx->VSSetConstantBuffers(1, 1, cbs); ctx->PSSetConstantBuffers(1, 1, cbs);
    if (!S.spawns.empty()) {
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(ctx->Map(R.spawn.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { memcpy(ms.pData, S.spawns.data(), S.spawns.size() * sizeof(XMFLOAT4)); ctx->Unmap(R.spawn.Get(), 0); }
    }
    ID3D11ShaderResourceView* nsrv[8] = {};
    ID3D11UnorderedAccessView* nuav[5] = {};
    ID3D11RenderTargetView* nrtv = nullptr;
    ctx->OMSetRenderTargets(1, &nrtv, nullptr);

    // ---- the simulation (paused: none)
    if (dt > 0) {
        ID3D11UnorderedAccessView* u[5] = { R.dropsUAV.Get(), R.volUAV.Get(), R.gridUAV.Get(), R.countUAV.Get(), nullptr };
        ctx->CSSetUnorderedAccessViews(0, 5, u, nullptr);
        ID3D11ShaderResourceView* s3[3] = { nullptr, R.spawnSRV.Get(), R.filmSRV.Get() };
        ctx->CSSetShaderResources(0, 3, s3);
        ctx->CSSetShader(R.csClear.Get(), nullptr, 0); ctx->Dispatch((GW / GRIDC * GH / GRIDC + 63) / 64, 1, 1);
        ctx->CSSetShader(R.csInsert.Get(), nullptr, 0); ctx->Dispatch(NDROP / 64, 1, 1);
        ctx->CSSetShader(R.csUpdate.Get(), nullptr, 0); ctx->Dispatch(NDROP / 64, 1, 1);
        ctx->CSSetShader(R.csFinal.Get(), nullptr, 0); ctx->Dispatch(NDROP / 64, 1, 1);
        if (!S.spawns.empty()) { ctx->CSSetShader(R.csSpawn.Get(), nullptr, 0); ctx->Dispatch(((UINT)S.spawns.size() + 63) / 64, 1, 1); }
        ctx->CSSetShaderResources(0, 3, nsrv);
        ID3D11UnorderedAccessView* uf[5] = { nullptr, nullptr, nullptr, nullptr, R.filmUAV.Get() };
        ctx->CSSetUnorderedAccessViews(0, 5, uf, nullptr);
        ctx->CSSetShader(R.csFilm.Get(), nullptr, 0); ctx->Dispatch((GW + 7) / 8, (GH + 7) / 8, 1);
        ctx->CSSetUnorderedAccessViews(0, 5, nuav, nullptr);
        ctx->CSSetShader(nullptr, nullptr, 0);
        ctx->CopyResource(R.countRead[fi].Get(), R.count.Get());
        ctx->End(R.qTs[fi][4].Get());
        // the drops wipe the film they run over, their beads and the specks go into it
        D3D11_VIEWPORT gvp = { 0, 0, (float)GW, (float)GH, 0, 1 };
        ctx->RSSetViewports(1, &gvp);
        ctx->OMSetRenderTargets(1, R.filmRTV.GetAddressOf(), nullptr);
        ctx->OMSetDepthStencilState(dsRead.Get(), 0);   // (no depth target bound: the test passes)
        ctx->RSSetState(rsNone.Get());
        ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11ShaderResourceView* vsrv[2] = { R.dropsSRV.Get(), R.spawnSRV.Get() };
        ctx->VSSetShaderResources(0, 2, vsrv);
        ctx->VSSetShader(R.vsDrop.Get(), nullptr, 0);
        auto pass = [&](int mode, ID3D11PixelShader* ps, ID3D11BlendState* b, UINT n) {
            k.misc.y = (float)mode; SetCB(ctx.Get(), k);
            ctx->OMSetBlendState(b, nullptr, ~0u); ctx->PSSetShader(ps, nullptr, 0);
            if (n) ctx->DrawInstanced(6, n, 0, 0);
        };
        pass(0, R.psWipe.Get(), R.bsMul.Get(), NDROP);
        pass(1, R.psBead.Get(), R.bsMax.Get(), NDROP);
        pass(2, R.psBead.Get(), R.bsMax.Get(), (UINT)S.spawns.size());
    }
    // the drops alive (read 3 frames late)
    if (frameNo > 4) {
        D3D11_MAPPED_SUBRESOURCE ms;
        if (ctx->Map(R.countRead[(fi + 1) % 4].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &ms) == S_OK) { S.alive = *(const UINT*)ms.pData; ctx->Unmap(R.countRead[(fi + 1) % 4].Get(), 0); }
    }

    S.insideDrawn = false;
    partsOk[fi] = false;
    if (firstPerson && oapiCameraInternal()) {
        // ---- the height map: the film, the drops' caps over it; its slopes and coverage, their mips (the defocus)
        D3D11_VIEWPORT gvp = { 0, 0, (float)GW, (float)GH, 0, 1 };
        ctx->RSSetViewports(1, &gvp);
        ctx->OMSetRenderTargets(1, R.hRTV.GetAddressOf(), nullptr);
        ctx->OMSetDepthStencilState(dsRead.Get(), 0);
        ctx->RSSetState(rsNone.Get());
        ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->PSSetShaderResources(2, 1, R.filmSRV.GetAddressOf());
        ctx->VSSetShader(R.vsFull.Get(), nullptr, 0); ctx->PSSetShader(R.psCopyFilm.Get(), nullptr, 0);
        ctx->OMSetBlendState(R.bsNone.Get(), nullptr, ~0u);
        ctx->Draw(3, 0);
        k.misc.y = 3; SetCB(ctx.Get(), k);
        ID3D11ShaderResourceView* vsrv[2] = { R.dropsSRV.Get(), R.spawnSRV.Get() };
        ctx->VSSetShaderResources(0, 2, vsrv);
        ctx->VSSetShader(R.vsDrop.Get(), nullptr, 0); ctx->PSSetShader(R.psCap.Get(), nullptr, 0);
        ctx->OMSetBlendState(R.bsMax.Get(), nullptr, ~0u);
        ctx->DrawInstanced(6, NDROP, 0, 0);
        ctx->VSSetShaderResources(0, 2, nsrv);
        ctx->OMSetRenderTargets(1, R.lensRTV.GetAddressOf(), nullptr);
        ctx->PSSetShaderResources(3, 1, R.hSRV.GetAddressOf());
        ctx->VSSetShader(R.vsFull.Get(), nullptr, 0); ctx->PSSetShader(R.psLens.Get(), nullptr, 0);
        ctx->OMSetBlendState(R.bsNone.Get(), nullptr, ~0u);
        ctx->Draw(3, 0);
        ctx->PSSetShaderResources(2, 2, nsrv);
        // the pupil's defocus: lens -> lensB (along u) -> lens (along v)
        ctx->OMSetRenderTargets(1, R.lensBRTV.GetAddressOf(), nullptr);
        ctx->PSSetShaderResources(6, 1, R.lensSRV.GetAddressOf());
        ctx->PSSetShader(R.psBlurH.Get(), nullptr, 0); ctx->Draw(3, 0);
        ctx->PSSetShaderResources(6, 1, nsrv);
        ctx->OMSetRenderTargets(1, R.lensRTV.GetAddressOf(), nullptr);
        ctx->PSSetShaderResources(6, 1, R.lensBSRV.GetAddressOf());
        ctx->PSSetShader(R.psBlurV.Get(), nullptr, 0); ctx->Draw(3, 0);
        ctx->PSSetShaderResources(6, 1, nsrv);
        ctx->OMSetRenderTargets(1, &nrtv, nullptr);
        ctx->End(R.qTs[fi][5].Get());

        // ---- the scene behind the glass at a quarter size, its mips
        const UINT sw = std::max(RW / 4, 1u), sh = std::max(RH / 4, 1u);   // a quarter: what is seen through a drop is blurred anyway
        if (!R.scene || R.sceneW != sw || R.sceneH != sh) {
            R.scene.Reset(); R.sceneSRV.Reset(); R.sceneRTV.Reset();
            MakeTex(dev.Get(), sw, sh, DXGI_FORMAT_R16G16B16A16_FLOAT, SCENEMIPS, false, R.scene, &R.sceneSRV, &R.sceneRTV);
            R.sceneW = sw; R.sceneH = sh;
        }
        if (R.scene) {
            D3D11_VIEWPORT svp = { 0, 0, (float)sw, (float)sh, 0, 1 };
            ctx->RSSetViewports(1, &svp);
            ctx->OMSetRenderTargets(1, R.sceneRTV.GetAddressOf(), nullptr);
            ctx->PSSetShaderResources(samples > 1 ? 15 : 14, 1, hdrSRV.GetAddressOf());
            ctx->PSSetShader(samples > 1 ? R.psDownMS.Get() : R.psDown.Get(), nullptr, 0);
            ctx->Draw(3, 0);
            ctx->OMSetRenderTargets(1, &nrtv, nullptr);
            ctx->PSSetShaderResources(14, 2, nsrv);
            ctx->GenerateMips(R.sceneSRV.Get());
            ctx->End(R.qTs[fi][6].Get());

            // ---- the view through the glass
            D3D11_VIEWPORT vp = { 0, 0, (float)RW, (float)RH, 0, 1 };
            ctx->RSSetViewports(1, &vp);
            ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
            ctx->OMSetBlendState(R.bsPremul.Get(), nullptr, ~0u);
            ctx->PSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
            FxBindLight(this);
            LightsBind();
            ctx->PSSetShaderResources(7, 1, shadowSRV.GetAddressOf());
            ID3D11SamplerState* s2 = sampShadow.Get(); ctx->PSSetSamplers(2, 1, &s2);
            ctx->PSSetSamplers(3, 1, R.sLin.GetAddressOf());
            ID3D11ShaderResourceView* ls[2] = { R.lensSRV.Get(), R.sceneSRV.Get() };
            ctx->PSSetShaderResources(4, 2, ls);
            ctx->VSSetShader(R.vsFull.Get(), nullptr, 0); ctx->PSSetShader(R.psInside.Get(), nullptr, 0);
            ctx->Draw(3, 0);
            ctx->PSSetShaderResources(4, 2, nsrv);
            S.insideDrawn = true;
            partsOk[fi] = dt > 0;
        }
    }
    // the pipeline as the frame expects it after this pass
    D3D11_VIEWPORT vp = { 0, 0, (float)RW, (float)RH, 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->VSSetShaderResources(0, 2, nsrv);
        }();
    }
    // ---- the GPU time (read 3 frames late), a line every 10 s
    if (frameK >= 4) {
        const UINT pf = (frameK - 3) % 4;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
        if (ctx->GetData(R.qDis[pf].Get(), &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !dj.Disjoint) {
            UINT64 t[4]; bool ok = true;
            for (int i = 0; i < 4; i++) ok = ok && ctx->GetData(R.qTs[pf][i].Get(), &t[i], 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            if (ok) {
                msOut = 0.9 * msOut + 0.1 * (double)(t[1] - t[0]) * 1000.0 / dj.Frequency;
                msIn = 0.9 * msIn + 0.1 * (double)(t[3] - t[2]) * 1000.0 / dj.Frequency;
            }
            UINT64 u[3]; bool okp = ok && partsOk[pf];
            for (int i = 0; i < 3; i++) okp = okp && ctx->GetData(R.qTs[pf][4 + i].Get(), &u[i], 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            if (okp) {
                const UINT64 e[5] = { t[2], u[0], u[1], u[2], t[3] };
                for (int i = 0; i < 4; i++) msPart[i] = 0.9 * msPart[i] + 0.1 * (double)(e[i + 1] - e[i]) * 1000.0 / dj.Frequency;
            }
        }
    }
    frameK++;
    LARGE_INTEGER now, f; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
    if ((now.QuadPart - tLogV.QuadPart) / (double)f.QuadPart > 10.0) {
        tLogV = now;
        LOGC("visor drops: %s | rain %.2f mm/h, impacts %.0f/s, air %.1f m/s, drops %u | GPU ms %.3f (outside %.3f, inside %.3f: simulation %.3f, lens %.3f, scene copy %.3f, view %.3f)",
             S.focus ? (S.insideDrawn ? "inside drawn" : "glass simulated") : "no suit in focus", S.rain, S.impactsPerS, S.airSpeed, S.alive,
             msOut + msIn, msOut, msIn, msPart[0], msPart[1], msPart[2], msPart[3]);
    }
}
