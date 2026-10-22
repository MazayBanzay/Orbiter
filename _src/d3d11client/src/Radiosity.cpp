// D3D11Client: real-time radiosity - Enlighten's scheme (Martin & Einarsson, "A Real-Time Radiosity Architecture for Video
// Games", SIGGRAPH 2010; Frostbite's GI). A vessel (a base's building) sends its precomputed light transport through
// D3D11Lights SetRadiosity (a .gi file: Tantra_Design/blender/gi_techpoint.py): its surfaces as patches, the form factors
// between them, its lamps' visibility from each, its rooms' probe grids. Every frame (shaders\radiosity.hlsl):
//   CSDirect - each patch's direct light: the vessel's lamps as they are switched now x their visibility, the Sun through
//              the openings (the cascades' shadow over it), the other vessels' lamps with their shadow maps;
//   CSBounce - two Jacobi steps B = albedo (Edir + sum F B), carried over from frame to frame (converges in a few frames);
//   CSProbe  - each probe's ambient cube from the patches it sees, into a 3D texture (t27) the scene reads (lights.hlsli
//              RoomSky: the room's indirect light, diffuse and mirrored, the AO and the specular occlusion act on it).
// Doors and gates are portals: what lies behind counts while they are open, their leaves while shut (SetPortals).
// One vessel at a time: the nearest to the camera with a file (300 m).
#include "Client.h"
#include "LightsApi.h"
#include <algorithm>

namespace {
struct GILight { XMFLOAT4 p, c, d, a, e; };             // = Lights.cpp LightG, lights.hlsli LocalLightG
struct GICB { XMFLOAT4 cnt, cnt2, portal[2], o, x, y, z, openB[32], openN, grid[24], gridN; };
struct LampDef { int type; float c[3], dir[3], ex[3], ey[3], range; };
struct GridDef { float lo[3], hi[3]; int dims[3], room, base; };

ID3D11Device* g_dev = nullptr;
OBJHANDLE g_v = nullptr; std::string g_file; bool g_ok = false; UINT g_fails = 0;
int g_nPatch = 0, g_nCluster = 0, g_nProbe = 0, g_nGrid = 0, g_texW = 0, g_texH = 0, g_texD = 0, g_nLights = 0;
std::vector<LampDef> g_lamps;
std::vector<GridDef> g_grids;
std::vector<GILight> g_lampNow;
float g_portal[8] = {};
VECTOR3 g_cp = { 0, 0, 0 };
ComPtr<ID3D11Buffer> g_bClus; ComPtr<ID3D11ShaderResourceView> g_sClus;
ComPtr<ID3D11Buffer> g_bPatch, g_bPP, g_bLV, g_bProbe, g_bPE, g_bLamp, g_bEdir, g_bB[2], g_cb;
ComPtr<ID3D11ShaderResourceView> g_sPatch, g_sPP, g_sLV, g_sProbe, g_sPE, g_sLamp, g_sEdir, g_sB[2], g_sTex;
ComPtr<ID3D11UnorderedAccessView> g_uEdir, g_uB[2], g_uTex;
ComPtr<ID3D11Texture3D> g_tex;
ComPtr<ID3D11ComputeShader> g_csDirect, g_csBounce, g_csCluster, g_csProbe;
double g_gpuMB = 0;
UINT g_frames = 0;

void Clear()
{
    g_ok = false; g_v = nullptr; g_file.clear(); g_lamps.clear(); g_grids.clear(); g_lampNow.clear();
    g_bPatch.Reset(); g_bPP.Reset(); g_bLV.Reset(); g_bProbe.Reset(); g_bPE.Reset(); g_bLamp.Reset(); g_bEdir.Reset(); g_bB[0].Reset(); g_bB[1].Reset();
    g_sPatch.Reset(); g_sPP.Reset(); g_sLV.Reset(); g_sProbe.Reset(); g_sPE.Reset(); g_sLamp.Reset(); g_sEdir.Reset(); g_sB[0].Reset(); g_sB[1].Reset(); g_sTex.Reset();
    g_uEdir.Reset(); g_uB[0].Reset(); g_uB[1].Reset(); g_uTex.Reset(); g_tex.Reset();
    g_bClus.Reset(); g_sClus.Reset();
    g_nPatch = g_nCluster = g_nProbe = g_nGrid = 0;
}

// a structured buffer: immutable with data, or default with a UAV (data null)
bool Make(ID3D11Device* dev, UINT stride, UINT n, const void* data, ComPtr<ID3D11Buffer>& b, ComPtr<ID3D11ShaderResourceView>& s, ComPtr<ID3D11UnorderedAccessView>* u = nullptr, bool dynamic = false)
{
    n = std::max(n, 1u);
    D3D11_BUFFER_DESC bd = { stride * n, data ? D3D11_USAGE_IMMUTABLE : dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT,
                             D3D11_BIND_SHADER_RESOURCE | (u ? D3D11_BIND_UNORDERED_ACCESS : 0u), dynamic ? D3D11_CPU_ACCESS_WRITE : 0u,
                             D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, stride };
    D3D11_SUBRESOURCE_DATA sd = { data, 0, 0 };
    if (FAILED(dev->CreateBuffer(&bd, data ? &sd : nullptr, &b))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd = {}; vd.Format = DXGI_FORMAT_UNKNOWN; vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; vd.Buffer.NumElements = n;
    if (FAILED(dev->CreateShaderResourceView(b.Get(), &vd, &s))) return false;
    if (u) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_UNKNOWN; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Buffer.NumElements = n;
        if (FAILED(dev->CreateUnorderedAccessView(b.Get(), &ud, u->GetAddressOf()))) return false;
    }
    return true;
}

bool Load(ID3D11Device* dev, const std::string& file)
{
    FILE* f = fopen(file.c_str(), "rb");
    if (!f) { LOGC("radiosity: %s not found", file.c_str()); return false; }
    std::vector<char> buf;
    fseek(f, 0, SEEK_END); const long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz > 0 && sz < 256 * 1024 * 1024) { buf.resize((size_t)sz); if (fread(buf.data(), 1, buf.size(), f) != buf.size()) buf.clear(); }
    fclose(f);
    size_t at = 0;
    auto take = [&](size_t n) -> const char* { if (at + n > buf.size()) return nullptr; const char* p = buf.data() + at; at += n; return p; };
    const char* h = take(4 + 10 * 4);
    if (!h || memcmp(h, "TPGI", 4)) { LOGC("radiosity: %s is not a radiosity file", file.c_str()); return false; }
    int H[10]; memcpy(H, h + 4, sizeof H);
    const int ver = H[0], nP = H[1], nPP = H[2], nL = H[3], nLV = H[4], nPo = H[5], nG = H[6], nPr = H[7], nPE = H[8], nC = H[9];
    if (ver != 2 || nC <= 0 || nC > 65535 || nP <= 0 || nP > 65535 || nPP < 0 || nL < 0 || nL > 1024 || nLV < 0 || nPo < 0 || nPo > 8 || nG <= 0 || nG > 8 || nPr <= 0 || nPE < 0) {
        LOGC("radiosity: %s - bad header (version %d, patches %d)", file.c_str(), ver, nP); return false;
    }
    struct PatchF { float p[3], n[3], a[3], area; int room; uint32_t ppO, ppN, lvO, lvN; };
    struct PatchG { XMFLOAT4 p, n, a; uint32_t r[4]; };
    const char* pp = take((size_t)nP * sizeof(PatchF)); const char* ppE = take((size_t)nPP * 8); const char* cl = take((size_t)nC * 8);
    const char* lp = take((size_t)nL * sizeof(LampDef)); const char* lvE = take((size_t)nLV * 8);
    const char* po = take((size_t)nPo * 24); const char* gp = take((size_t)nG * sizeof(GridDef));
    const char* pr = take((size_t)nPr * 12); const char* peE = take((size_t)nPE * 12);
    if (!pp || !ppE || !cl || !lp || !lvE || !po || !gp || !pr || !peE) { LOGC("radiosity: %s is cut short", file.c_str()); return false; }
    (void)po;
    std::vector<PatchG> patches(nP);
    for (int i = 0; i < nP; i++) {
        PatchF q; memcpy(&q, pp + (size_t)i * sizeof(PatchF), sizeof q);
        patches[i].p = { q.p[0], q.p[1], q.p[2], q.area }; patches[i].n = { q.n[0], q.n[1], q.n[2], (float)q.room };
        patches[i].a = { q.a[0], q.a[1], q.a[2], 0 };
        patches[i].r[0] = q.ppO; patches[i].r[1] = q.ppN; patches[i].r[2] = q.lvO; patches[i].r[3] = q.lvN;
        if ((uint64_t)q.ppO + q.ppN > (uint64_t)nPP || (uint64_t)q.lvO + q.lvN > (uint64_t)nLV) { LOGC("radiosity: %s - patch %d out of range", file.c_str(), i); return false; }
    }
    g_lamps.resize(nL); if (nL) memcpy(g_lamps.data(), lp, (size_t)nL * sizeof(LampDef));
    g_grids.resize(nG); memcpy(g_grids.data(), gp, (size_t)nG * sizeof(GridDef));
    std::vector<uint32_t> probes((size_t)nPr * 4);
    for (int i = 0; i < nPr; i++) {
        uint32_t q[3]; memcpy(q, pr + (size_t)i * 12, 12);
        if ((uint64_t)q[0] + q[1] > (uint64_t)nPE) { LOGC("radiosity: %s - probe %d out of range", file.c_str(), i); return false; }
        probes[i * 4] = q[0]; probes[i * 4 + 1] = q[1]; probes[i * 4 + 2] = q[2]; probes[i * 4 + 3] = 0;
    }
    // the probes' texture: the grids side by side along x, each face a slab of the depth
    g_texW = 0; g_texH = 1; g_texD = 1;
    for (const GridDef& g : g_grids) {
        if (g.dims[0] <= 0 || g.dims[1] <= 0 || g.dims[2] <= 0 || g.base < 0 || g.base + g.dims[0] * g.dims[1] * g.dims[2] > nPr) { LOGC("radiosity: %s - bad grid", file.c_str()); return false; }
        g_texW += g.dims[0]; g_texH = std::max(g_texH, g.dims[1]); g_texD = std::max(g_texD, g.dims[2]);
    }
    if (g_texW > 2048 || g_texH > 2048 || g_texD * 6 > 2048) { LOGC("radiosity: %s - grids too large", file.c_str()); return false; }
    bool ok = Make(dev, sizeof(PatchG), nP, patches.data(), g_bPatch, g_sPatch)
           && Make(dev, 8, nPP, nPP ? ppE : nullptr, g_bPP, g_sPP)
           && Make(dev, 8, nLV, nLV ? lvE : nullptr, g_bLV, g_sLV)
           && Make(dev, 16, nPr, probes.data(), g_bProbe, g_sProbe)
           && Make(dev, 12, nPE, nPE ? peE : nullptr, g_bPE, g_sPE)
           && Make(dev, sizeof(GILight), std::max(nL, 1), nullptr, g_bLamp, g_sLamp, nullptr, true)
           && Make(dev, 16, nP, nullptr, g_bEdir, g_sEdir, &g_uEdir)
           && Make(dev, 8, nC, cl, g_bClus, g_sClus)
           && Make(dev, 16, nP, nullptr, g_bB[0], g_sB[0], &g_uB[0])          // the patches' radiosity
           && Make(dev, 16, nC, nullptr, g_bB[1], g_sB[1], &g_uB[1]);         // the clusters'
    if (ok) {
        D3D11_TEXTURE3D_DESC td = { (UINT)g_texW, (UINT)g_texH, (UINT)(g_texD * 6), 1, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_USAGE_DEFAULT,
                                    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
        ok = SUCCEEDED(dev->CreateTexture3D(&td, nullptr, &g_tex))
          && SUCCEEDED(dev->CreateShaderResourceView(g_tex.Get(), nullptr, &g_sTex))
          && SUCCEEDED(dev->CreateUnorderedAccessView(g_tex.Get(), nullptr, &g_uTex));
    }
    if (!ok) { LOGC("radiosity: %s - GPU buffers not made", file.c_str()); return false; }
    g_nPatch = nP; g_nCluster = nC; g_nProbe = nPr; g_nGrid = nG; g_frames = 0;
    // the GPU's share (the 32-bit process' budget: the file's copy is freed on return)
    g_gpuMB = ((double)nP * (sizeof(PatchG) + 32) + (double)nPP * 8 + (double)nC * 24 + (double)nLV * 8 + (double)nPr * 16 + (double)nPE * 12
               + (double)nL * sizeof(GILight) + (double)g_texW * g_texH * g_texD * 6 * 8) / 1048576.0;
    LOGC("radiosity: %s - %d patches in %d clusters, %d form factors, %d lamps (%d visibility entries), %d portals, %d probe grids (%d probes, %d entries); file %.1f MB, GPU %.1f MB",
         file.c_str(), nP, nC, nPP, nL, nLV, nPo, nG, nPr, nPE, sz / 1048576.0, g_gpuMB);
    return true;
}
}

// the vessel with radiosity data nearest the camera (its file loaded when it changes)
OBJHANDLE D3D11Client::GIVessel(const VECTOR3& cp)
{
    g_cp = cp;
    if (g_dev != dev.Get()) { Clear(); g_dev = dev.Get(); g_csDirect.Reset(); g_csBounce.Reset(); g_csProbe.Reset(); g_fails = 0; }
    std::map<OBJHANDLE, std::string> files; std::map<OBJHANDLE, std::vector<float>> portals;
    LightsApiGI(&files, &portals);
    OBJHANDLE best = nullptr; double bd = 300.0; std::string bf;
    for (auto& kv : files) {
        if (!visuals.count(kv.first)) continue;
        VECTOR3 q; oapiGetGlobalPos(kv.first, &q);
        const double d = length(q - cp) - oapiGetSize(kv.first);
        if (d < bd) { bd = d; best = kv.first; bf = kv.second; }
    }
    if (best != g_v || bf != g_file) {
        Clear();
        if (best && g_fails < 3) {
            if (!g_csDirect) {
                auto a = CompileShaderFile("radiosity.hlsl", "CSDirect", "cs_5_0"), b = CompileShaderFile("radiosity.hlsl", "CSBounce", "cs_5_0"),
                     c = CompileShaderFile("radiosity.hlsl", "CSProbe", "cs_5_0"), d = CompileShaderFile("radiosity.hlsl", "CSCluster", "cs_5_0");
                if (a) dev->CreateComputeShader(a->GetBufferPointer(), a->GetBufferSize(), nullptr, &g_csDirect);
                if (b) dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &g_csBounce);
                if (c) dev->CreateComputeShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &g_csProbe);
                if (d) dev->CreateComputeShader(d->GetBufferPointer(), d->GetBufferSize(), nullptr, &g_csCluster);
                if (!g_cb) { D3D11_BUFFER_DESC cd = { (sizeof(GICB) + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 }; dev->CreateBuffer(&cd, nullptr, &g_cb); }
            }
            g_v = best; g_file = bf;
            g_ok = g_csDirect && g_csBounce && g_csCluster && g_csProbe && g_cb && Load(dev.Get(), bf);
            if (g_ok) { const float z[4] = {}; ctx->ClearUnorderedAccessViewFloat(g_uB[0].Get(), z); ctx->ClearUnorderedAccessViewFloat(g_uB[1].Get(), z); ctx->ClearUnorderedAccessViewFloat(g_uTex.Get(), z); }
            if (!g_ok) { g_fails++; LOGC("radiosity: off for %s (%s)", oapiGetVesselInterface(best) ? oapiGetVesselInterface(best)->GetName() : "?", g_csDirect ? "its data" : "the shaders"); }
        }
    }
    for (float& p : g_portal) p = 0;
    if (g_v) { auto it = portals.find(g_v); if (it != portals.end()) for (size_t i = 0; i < it->second.size() && i < 8; i++) g_portal[i] = std::clamp(it->second[i], 0.0f, 1.0f); }
    return g_ok ? g_v : nullptr;
}

// the vessel's lamps this frame: own = per light 4 floats (its position in the vessel frame, 1 = a rectangle) + its LightG
// (20 floats); matched to the file's lamps by position (twins at one place - a duty lamp and its red emergency one - add)
void D3D11Client::GILamps(const std::vector<float>& own, int nLights)
{
    g_nLights = nLights;
    if (!g_ok) return;
    g_lampNow.assign(g_lamps.size(), GILight{});
    const size_t n = own.size() / 24;
    for (size_t k = 0; k < g_lamps.size(); k++) {
        const LampDef& L = g_lamps[k];
        for (size_t i = 0; i < n; i++) {
            const float* e = &own[i * 24];
            if ((e[3] > 0.5f) != (L.type == 2)) continue;
            const float dx = e[0] - L.c[0], dy = e[1] - L.c[1], dz = e[2] - L.c[2];
            if (dx * dx + dy * dy + dz * dz > 0.03f * 0.03f) continue;
            GILight g; memcpy(&g, e + 4, sizeof g);
            if (g_lampNow[k].p.w <= 0) g_lampNow[k] = g;
            else { g_lampNow[k].c.x += g.c.x; g_lampNow[k].c.y += g.c.y; g_lampNow[k].c.z += g.c.z; }
        }
        g_lampNow[k].a.z = -1;            // (no shadow map: the visibility is precomputed)
    }
}

// the scene's constants (lights.hlsli gGI*): the vessel's frame, the grids, the texture
void D3D11Client::GIShade(XMFLOAT4* out)
{
    memset(out, 0, sizeof(XMFLOAT4) * 29);
    if (!g_ok || !g_v) return;
    VESSEL* v = oapiGetVesselInterface(g_v); if (!v) return;
    VECTOR3 gp; v->GetGlobalPos(gp); MATRIX3 R; v->GetRotationMatrix(R);
    const VECTOR3 o = gp - g_cp, ax = mul(R, _V(1, 0, 0)), ay = mul(R, _V(0, 1, 0)), az = mul(R, _V(0, 0, 1));
    out[0] = { (float)o.x, (float)o.y, (float)o.z, 1 };
    out[1] = { (float)ax.x, (float)ax.y, (float)ax.z, 0 }; out[2] = { (float)ay.x, (float)ay.y, (float)ay.z, 0 }; out[3] = { (float)az.x, (float)az.y, (float)az.z, 0 };
    int x0 = 0;
    for (int g = 0; g < g_nGrid && g < 8; g++) {
        const GridDef& G = g_grids[g];
        const float cs[3] = { (G.hi[0] - G.lo[0]) / G.dims[0], (G.hi[1] - G.lo[1]) / G.dims[1], (G.hi[2] - G.lo[2]) / G.dims[2] };
        out[4 + g * 3] = { G.lo[0], G.lo[1], G.lo[2], (float)x0 };
        out[5 + g * 3] = { cs[0], cs[1], cs[2], (float)G.base };
        out[6 + g * 3] = { (float)G.dims[0], (float)G.dims[1], (float)G.dims[2], (float)G.room };
        x0 += G.dims[0];
    }
    out[28] = { 1.0f / g_texW, 1.0f / g_texH, (float)g_texD, (float)std::min(g_nGrid, 8) };
}

ID3D11ShaderResourceView* D3D11Client::GIProbeSRV() { return g_ok ? g_sTex.Get() : nullptr; }

// the frame's work (Lights.cpp LightsBind: after the Sun's and the lamps' shadows, the frame's constants; b9, t17..t21, t26
// bound to the compute stage by the caller)
void D3D11Client::GIRun()
{
    if (!g_ok || !g_v) return;
    VESSEL* v = oapiGetVesselInterface(g_v); if (!v) return;
    if (!g_lampNow.empty()) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(g_bLamp.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, g_lampNow.data(), g_lampNow.size() * sizeof(GILight)); ctx->Unmap(g_bLamp.Get(), 0); }
    }
    GICB cb = {};
    cb.cnt = { (float)g_nPatch, (float)g_lamps.size(), (float)g_nProbe, (float)g_nLights };
    cb.cnt2 = { (float)g_nCluster, 0, 0, 0 };
    cb.portal[0] = { g_portal[0], g_portal[1], g_portal[2], g_portal[3] }; cb.portal[1] = { g_portal[4], g_portal[5], g_portal[6], g_portal[7] };
    XMFLOAT4 sh[29]; GIShade(sh);
    cb.o = sh[0]; cb.x = sh[1]; cb.y = sh[2]; cb.z = sh[3];
    for (int i = 0; i < 24; i++) cb.grid[i] = sh[4 + i];
    cb.gridN = { (float)std::min(g_nGrid, 8), (float)g_texD, 0, 0 };
    {   // the openings the Sun may come in by (D3D11Lights SetOpenings), in the vessel frame
        std::map<OBJHANDLE, std::vector<D3D11Lights::Opening>> op;
        LightsApiData(nullptr, nullptr, nullptr, &op);
        int n = 0;
        auto it = op.find(g_v);
        if (it != op.end()) for (const auto& o : it->second) {
            if (n >= 16) break;
            cb.openB[n * 2] = { std::min(o.box[0], o.box[3]), std::min(o.box[1], o.box[4]), std::min(o.box[2], o.box[5]), (float)o.room };
            cb.openB[n * 2 + 1] = { std::max(o.box[0], o.box[3]), std::max(o.box[1], o.box[4]), std::max(o.box[2], o.box[5]), 0 };
            n++;
        }
        cb.openN = { (float)n, 0, 0, 0 };
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(g_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, &cb, sizeof cb); ctx->Unmap(g_cb.Get(), 0);

    ID3D11ShaderResourceView* nul8[13] = {};
    ID3D11UnorderedAccessView* nulU[4] = {};
    ID3D11ShaderResourceView* nulS = nullptr;
    ctx->PSSetShaderResources(27, 1, &nulS);                       // (the texture is written below)
    ctx->CSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
    ctx->CSSetConstantBuffers(10, 1, g_cb.GetAddressOf());
    ctx->CSSetShaderResources(7, 1, shadowSRV.GetAddressOf());
    ctx->CSSetSamplers(2, 1, sampShadow.GetAddressOf());
    const UINT groupsP = (UINT)((g_nPatch + 63) / 64), groupsC = (UINT)((g_nCluster + 63) / 64), groupsR = (UINT)((g_nProbe + 63) / 64);
    // the direct light
    { ID3D11ShaderResourceView* s[4] = { g_sPatch.Get(), nullptr, g_sLV.Get(), g_sLamp.Get() }; ctx->CSSetShaderResources(0, 4, s); }
    ctx->CSSetUnorderedAccessViews(0, 1, g_uEdir.GetAddressOf(), nullptr);
    ctx->CSSetShader(g_csDirect.Get(), nullptr, 0);
    ctx->Dispatch(groupsP, 1, 1);
    ctx->CSSetUnorderedAccessViews(0, 1, nulU, nullptr);
    // two bounces: the patches from the clusters (the last frame's the start), the clusters from the patches
    ctx->CSSetShaderResources(1, 1, g_sPP.GetAddressOf());
    ctx->CSSetShaderResources(6, 1, g_sEdir.GetAddressOf());
    ctx->CSSetShaderResources(11, 1, g_sClus.GetAddressOf());
    for (int it = 0; it < 2; it++) {
        ctx->CSSetShader(g_csBounce.Get(), nullptr, 0);
        ctx->CSSetShaderResources(10, 1, g_sB[1].GetAddressOf());
        ctx->CSSetUnorderedAccessViews(1, 1, g_uB[0].GetAddressOf(), nullptr);
        ctx->Dispatch(groupsP, 1, 1);
        ctx->CSSetUnorderedAccessViews(1, 1, nulU, nullptr);
        ctx->CSSetShaderResources(10, 1, &nulS);
        ctx->CSSetShader(g_csCluster.Get(), nullptr, 0);
        ctx->CSSetShaderResources(12, 1, g_sB[0].GetAddressOf());
        ctx->CSSetUnorderedAccessViews(3, 1, g_uB[1].GetAddressOf(), nullptr);
        ctx->Dispatch(groupsC, 1, 1);
        ctx->CSSetUnorderedAccessViews(3, 1, nulU, nullptr);
        ctx->CSSetShaderResources(12, 1, &nulS);
    }
    // the probes
    ctx->CSSetShader(g_csProbe.Get(), nullptr, 0);
    { ID3D11ShaderResourceView* s[2] = { g_sProbe.Get(), g_sPE.Get() }; ctx->CSSetShaderResources(4, 2, s); }
    ctx->CSSetShaderResources(10, 1, g_sB[1].GetAddressOf());
    ctx->CSSetUnorderedAccessViews(2, 1, g_uTex.GetAddressOf(), nullptr);
    ctx->Dispatch(groupsR, 1, 1);
    ctx->CSSetUnorderedAccessViews(0, 4, nulU, nullptr);
    ctx->CSSetShaderResources(0, 13, nul8);
    ctx->CSSetShader(nullptr, nullptr, 0);
    g_frames++;
    if (g_frames == 1 || (g_frames % 1800) == 0)
        LOGC("radiosity: %s - frame %u, portals %.2f %.2f %.2f %.2f %.2f, lamps lit %d of %zu", v->GetName(), g_frames, g_portal[0], g_portal[1], g_portal[2], g_portal[3], g_portal[4],
             (int)std::count_if(g_lampNow.begin(), g_lampNow.end(), [](const GILight& l) { return l.c.x + l.c.y + l.c.z > 0; }), g_lampNow.size());
}
