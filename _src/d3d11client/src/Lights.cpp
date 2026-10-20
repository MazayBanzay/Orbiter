// D3D11Client: local lights - the vessels' point and spot lights (Orbiter LightEmitter: headlights, helmet lamps, the
// cockpits' lamps) and beacons (BEACONLIGHTSPEC). Per frame (LightsFrame, after the Sun's shadows): the lights gathered
// from every visual, turned into an intensity that falls as 1/d^2 (shaders\lights.hlsli has the photometry), sorted by
// what they matter, put into a clustered list over the view (16 x 9 tiles x 24 slices, built here on the CPU), and the
// four that matter most among the spots get a shadow map (one 2048 atlas, 2 x 2 tiles, the vessels as casters).
// mesh.hlsl and planet.hlsl add their light (b9, t17..t20 - LightsBind); after the air (LightsRenderAir) the beams in
// the air (lights_vol.hlsl PSBeams) and the lenses and beacons (VSSprite/PSSprite) are added to the scene.
// Client.cfg: LocalLights 0/1, LightLux (lux per Orbiter light unit, 10), LightShadows 0/1, LightBeams 0/1, Beacons 0/1.
#include "Client.h"
#include "Fx.h"
#include <algorithm>

namespace {
struct LightG { XMFLOAT4 p, c, d, a; };
struct LightsCB { XMFLOAT4X4 lvp; XMFLOAT4 grid, z; XMFLOAT4X4 spotVP[4]; XMFLOAT4 spotK[4]; XMFLOAT4X4 skyVP[12]; XMFLOAT4 skyDir[12]; XMFLOAT4 skyC, skyK; };
struct SpriteG { XMFLOAT4 p, c, d, e; };
struct SpriteCB { XMFLOAT4X4 vp; XMFLOAT4 view, right, up, fwd; };
constexpr int TX = 16, TY = 9, TZ = 24, NCL = TX * TY * TZ;
constexpr int SKY_N = 256, NSKY = 12;
constexpr int MAXL = 256, MAXIDX = 65536, MAXSPR = 256, SPOT_N = 1024, MAXSPOT = 4, MAXBEAM = 8;
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
std::vector<D3D11_RECT> g_beamRect;
// the eye's probe: 16 x 9 visible points (lights_vol.hlsl CSProbe), read back two frames later through a ring of 3
ComPtr<ID3D11ComputeShader> g_csProbe; ComPtr<ID3D11Buffer> g_probe, g_probeSt[3]; ComPtr<ID3D11UnorderedAccessView> g_probeUAV;
UINT g_probeN = 0; XMFLOAT4 g_probePts[144] = {}; bool g_probeOk = false;     // each beam's light: its cone's box on the screen
int g_nLights = 0, g_nBeams = 0, g_nSpr = 0, g_nSpot = 0;
UINT g_frame = 0;
double g_eLoc = 0; VECTOR3 g_eCol = { 1, 1, 1 };      // the local lights' illuminance where they light the view [Sun = 1], its colour

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
    LightG g; double imp, Icd; VECTOR3 pos, dir, lp; double range, cosOut; bool spot; OBJHANDLE h;
    // the shadow's tile, the beam
    int tile = -1; bool beam = false;
};
std::vector<Cand> g_cand;
std::vector<SpriteG> g_sprites;
std::vector<uint32_t> g_clusterData;     // (offset, count) per cluster
std::vector<uint32_t> g_index;
}

// ------------------------------------------------------------------------------------------ the frame's lights
void D3D11Client::LightsFrame(const VECTOR3& cp)
{
    if (!cfg.loaded) LoadCfg();
    g_nLights = g_nBeams = g_nSpr = g_nSpot = 0; g_eLoc = 0;
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
        g_cl = MakeStructured(dev.Get(), 8, NCL, g_clSRV);
        g_idx = MakeStructured(dev.Get(), 4, MAXIDX, g_idxSRV);
        g_spr = MakeStructured(dev.Get(), sizeof(SpriteG), MAXSPR, g_sprSRV);
        D3D11_TEXTURE2D_DESC st = { 2 * SPOT_N, 2 * SPOT_N, 1, 1, DXGI_FORMAT_R32_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (SUCCEEDED(dev->CreateTexture2D(&st, nullptr, &g_spotTex))) {
            D3D11_DEPTH_STENCIL_VIEW_DESC dv = {}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            dev->CreateDepthStencilView(g_spotTex.Get(), &dv, &g_spotDSV);
            D3D11_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
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
    cb.z = { Z0, (float)((TZ - 1) / log(ZF / Z0)), 0, 1.0f / (2 * SPOT_N) };
    for (int k = 0; k < 4; k++) { XMStoreFloat4x4(&cb.spotVP[k], XMMatrixIdentity()); cb.spotK[k] = { 0, 0, 0, 0 }; }
    cb.spotK[0].w = (float)(g_frame & 63);
    if (!g_ok || !cfg.on) { Upload(ctx.Get(), g_cb.Get(), &cb, 1); return; }

    // ---- gather
    const OBJHANDLE hs = TheSun();
    VECTOR3 sp = _V(0, 0, 0); if (hs) oapiGetGlobalPos(hs, &sp);
    const double dsun = std::max(length(sp - cp), 1e9);
    const double Esun = SUN_LUX_1AU * (AU / dsun) * (AU / dsun);      // the Sun's illuminance here [lux]: the client's unit
    const OBJHANDLE own = oapiCameraInternal() ? oapiGetFocusObject() : nullptr;
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
            Cand c;
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
            double wsum = 0;
            for (int pi = 0; pi < 144; pi++) {     // the eye adapts mostly to the middle of the view: weights fall off to the edges
                const double px = ((pi % 16) + 0.5) / 16 - 0.5, py = ((pi / 16) + 0.5) / 9 - 0.5;
                wsum += exp(-(px * px + py * py) / (2 * 0.18 * 0.18));
            }
            for (int pi = 0; pi < 144; pi++) {
                const XMFLOAT4& q = g_probePts[pi];
                const double px = ((pi % 16) + 0.5) / 16 - 0.5, py = ((pi / 16) + 0.5) / 9 - 0.5;
                const double pw = exp(-(px * px + py * py) / (2 * 0.18 * 0.18)) / wsum;
                if (q.w < 0.5) continue;
                const VECTOR3 x = _V(q.x, q.y, q.z);
                for (const Cand& c : g_cand) {
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
        }
        g_eLoc = E;
        const double cl = 0.2126 * col.x + 0.7152 * col.y + 0.0722 * col.z;
        g_eCol = cl > 0 ? col / cl : _V(1, 1, 1);
    }
    // ---- the spots' shadows: the four that matter most (not the wide floods: > 150 deg)
    if (cfg.shadows && g_spotDSV) {
        ID3D11ShaderResourceView* nul = nullptr;
        ctx->PSSetShaderResources(20, 1, &nul);
        ctx->ClearDepthStencilView(g_spotDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        ctx->OMSetRenderTargets(0, nullptr, g_spotDSV.Get());
        for (Cand& c : g_cand) {
            if (g_nSpot >= MAXSPOT) break;
            if (!c.spot || c.cosOut < cos(75 * RAD) || c.range < 2 || !inView(c)) continue;
            const int k = g_nSpot++;
            c.tile = k; c.g.a.z = (float)k;
            const double half = acos(c.cosOut) + 2 * RAD;
            const float fov = (float)std::min(2 * half, 160 * RAD), nearZ = 0.12f, farZ = (float)c.range;
            XMVECTOR eye = XMVectorSet((float)c.pos.x, (float)c.pos.y, (float)c.pos.z, 1);
            XMVECTOR look = XMVectorSet((float)c.dir.x, (float)c.dir.y, (float)c.dir.z, 0);
            XMVECTOR up = fabs(c.dir.y) < 0.9 ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(1, 0, 0, 0);
            XMMATRIX LVP = XMMatrixLookToLH(eye, look, up) * XMMatrixPerspectiveFovLH(fov, 1.0f, nearZ, farZ);
            XMStoreFloat4x4(&cb.spotVP[k], XMMatrixTranspose(LVP));
            cb.spotK[k] = { 2 * tanf(fov * 0.5f) / SPOT_N, nearZ, farZ, k == 0 ? (float)(g_frame & 63) : 0.0f };
            D3D11_VIEWPORT vp = { (float)((k & 1) * SPOT_N), (float)((k >> 1) * SPOT_N), (float)SPOT_N, (float)SPOT_N, 0, 1 };
            ctx->RSSetViewports(1, &vp);
            for (auto& kv : visuals) {
                // not the lamp's own vessel: a helmet lamp's own helmet, arms and body (skinned, a frame late) cut its
                // cone down to a thin crescent on the ground; a headlight's housing is its own too (as engines do: the
                // light's owner casts no shadow from it)
                if (kv.first == c.h) continue;
                VECTOR3 q; oapiGetGlobalPos(kv.first, &q);
                if (length(q - cp - c.pos) > c.range + oapiGetSize(kv.first)) continue;
                RenderVessel(kv.second, cp, _M(1, 0, 0, 0, 1, 0, 0, 0, 1), XMMatrixIdentity(), false, &LVP);
            }
        }
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
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
        if (hp && best < 50000) {
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
            const double R = cfg.skyR, back = 60.0, texel = 2 * R / SKY_N;
            ID3D11ShaderResourceView* nul = nullptr;
            ctx->PSSetShaderResources(21, 1, &nul);
            ctx->ClearDepthStencilView(g_skyDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
            ctx->OMSetRenderTargets(0, nullptr, g_skyDSV.Get());
            for (int k = 0; k < NSKY; k++) {
                // the zenith, 5 at 60 deg, 6 at 22 deg over the horizon
                const double el = k == 0 ? 90 * RAD : k < 6 ? 60 * RAD : 22 * RAD;
                const double az = k == 0 ? 0 : k < 6 ? (k - 1) * 72 * RAD : (k - 6) * 60 * RAD + 30 * RAD;
                const VECTOR3 d = up * sin(el) + (e1 * cos(az) + e2 * sin(az)) * cos(el);
                VECTOR3 a = fabs(dotp(d, up)) < 0.95 ? up : e1;
                VECTOR3 u = crossp(a, d); u = u / length(u); VECTOR3 v = crossp(d, u);
                // the centre in whole texels of the direction's frame, counted in the planet frame (no crawl over the ground)
                const VECTOR3 uL = tmul(Rp, u), vL = tmul(Rp, v);          // the axes in the planet frame (fixed there)
                const double cu = dotp(camL, uL), cv = dotp(camL, vL);
                const VECTOR3 c = u * (floor(cu / texel) * texel - cu) + v * (floor(cv / texel) * texel - cv);
                XMVECTOR eye = XMVectorSet((float)(c.x + d.x * (R + back)), (float)(c.y + d.y * (R + back)), (float)(c.z + d.z * (R + back)), 1);
                XMMATRIX LVP = XMMatrixLookToLH(eye, XMVectorSet(-(float)d.x, -(float)d.y, -(float)d.z, 0), XMVectorSet((float)v.x, (float)v.y, (float)v.z, 0))
                             * XMMatrixOrthographicLH((float)(2 * R), (float)(2 * R), 0.0f, (float)(2 * R + back));
                XMStoreFloat4x4(&cb.skyVP[k], XMMatrixTranspose(LVP));
                cb.skyDir[k] = { (float)d.x, (float)d.y, (float)d.z, 0 };
                D3D11_VIEWPORT vp = { (float)((k & 3) * SKY_N), (float)((k >> 2) * SKY_N), (float)SKY_N, (float)SKY_N, 0, 1 };
                ctx->RSSetViewports(1, &vp);
                for (auto& kv : visuals) {
                    VECTOR3 q; oapiGetGlobalPos(kv.first, &q);
                    VECTOR3 rq = q - cp - c;
                    const double along = dotp(rq, d), sz = oapiGetSize(kv.first);
                    if (length(rq - d * along) > R * 1.42 + sz || along > R + back + sz || along < -(R + sz)) continue;
                    RenderVessel(kv.second, cp, _M(1, 0, 0, 0, 1, 0, 0, 0, 1), XMMatrixIdentity(), false, &LVP);
                }
            }
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            cb.skyC = { 0, 0, 0, (float)R };
            cb.skyK = { (float)texel, 1.0f / (4 * SKY_N), 0, 0 };
        }
    }
    // ---- the beams in the air: the strongest spots (and points) of some reach, in an air
    if (cfg.beams && g_psBeams && (g_fxAtmo.active || skyOn)) {
        for (Cand& c : g_cand) {
            if (g_nBeams >= MAXBEAM) break;
            if (c.range < 4 || c.Icd < 300 || !inView(c)) continue;      // a lamp of some strength and reach (not the cabins' lamps)
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
    Upload(ctx.Get(), g_cb.Get(), &cb, 1);
    if (diagFrames > 0 || (g_frame % 600) == 300) {
        LOGC("local lights: %d (beams %d, spot shadows %d, sprites %d), cluster entries %zu, Sun here %.0f lux", g_nLights, g_nBeams, g_nSpot, g_nSpr, total, Esun);
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
    if (g_nBeams && g_vsFull && g_psBeams) {
        ctx->VSSetShader(g_vsFull.Get(), nullptr, 0);
        ctx->PSSetShader(g_psBeams.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &dsrv);
        ctx->RSSetState(g_rsScissor.Get());
        ctx->PSSetConstantBuffers(10, 1, g_beamCB.GetAddressOf());
        for (size_t k = 0; k < g_beamRect.size(); k++) {
            const D3D11_RECT& r = g_beamRect[k];
            if (r.right <= r.left || r.bottom <= r.top) continue;
            const XMFLOAT4 bi = { (float)k, 0, 0, 0 };
            Upload(ctx.Get(), g_beamCB.Get(), &bi, 1);
            ctx->RSSetScissorRects(1, &r);
            ctx->Draw(3, 0);
        }
        ctx->RSSetState(rsNone.Get());
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
    // the eye adapts partly, the user 20.10.2026), and slower than the sky's adaptation (~3 s)
    tExp = std::max(tExp, 0.5);
    const double k = 1 - exp(-dt / 3.0);
    kExp = exp(log(kExp) + (log(tExp) - log(kExp)) * k);
    for (int i = 0; i < 3; i++) kCol.data[i] = exp(log(kCol.data[i]) + (log(tCol.data[i]) - log(kCol.data[i])) * k);
    if ((g_frame % 600) == 300) LOGC("local lights: the eye - E local %.3g (colour %.2f %.2f %.2f), exposure x %.3f, balance x %.2f %.2f %.2f (from %.2f %.2f %.2f)",
                                      g_eLoc, g_eCol.x, g_eCol.y, g_eCol.z, kExp, kCol.x, kCol.y, kCol.z, adapt.x, adapt.y, adapt.z);
    adapt.x *= (float)kCol.x; adapt.y *= (float)kCol.y; adapt.z *= (float)kCol.z;
    adapt.w *= (float)kExp; exposure *= (float)kExp;
}

// the eye's probe: after the whole scene (the camera's own cockpit included), 16 x 9 visible points read back two frames
// later (LightsFrame turns them into the local lights' illuminance over the view)
void D3D11Client::LightsProbe()
{
    if (!g_ok || !cfg.on || !g_nLights || !g_csProbe || !g_probeUAV) return;
    ResolveDepth();
    ID3D11ShaderResourceView* dsrv = depthSRV.Get();
    if (!dsrv) return;
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
    ctx->CSSetShader(g_csProbe.Get(), nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
    ctx->CSSetShaderResources(0, 1, &dsrv);
    ctx->CSSetUnorderedAccessViews(0, 1, g_probeUAV.GetAddressOf(), nullptr);
    ctx->Dispatch(1, 1, 1);
    ID3D11UnorderedAccessView* nu = nullptr; ID3D11ShaderResourceView* ns = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr); ctx->CSSetShaderResources(0, 1, &ns); ctx->CSSetShader(nullptr, nullptr, 0);
    ctx->CopyResource(g_probeSt[g_probeN % 3].Get(), g_probe.Get());
    g_probeN++;
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
}
