// The engines' smoke and condensate as a 3D flow (shaders\smoke.hlsl): a box of gas round the focus, solved on the GPU
// each frame (advection, vorticity confinement, buoyancy, pressure projection) and drawn by ray-marching. One solver for
// every engine: each is a source with its own momentum (thrust over the air's density), heat and visible mass (Exhaust.cpp
// fills them; first the jet pack's water condensing out of its hydrogen exhaust). Switches: Exhaust.cfg Smoke, SmokeCell,
// SmokeConfine, SmokeIters.
#include "ExhaustInternal.h"
#include "Fx.h"

namespace {

XMFLOAT4 F4(const VECTOR3& v, double w) { return { (float)v.x, (float)v.y, (float)v.z, (float)w }; }
VECTOR3 Unit(const VECTOR3& v) { double l = length(v); return l > 0 ? v / l : _V(0, 1, 0); }

const int NX = 128, NY = 64, NZ = 128, NSRC = 8;

struct CB
{
    XMFLOAT4 n, dt; XMINT4 shift; XMFLOAT4 amb, sun, phys;
    XMFLOAT4 src[NSRC * 4];
    XMFLOAT4 o, e1, e2, e3;
    XMFLOAT4X4 vp;
};

struct Vol { ComPtr<ID3D11Texture3D> t; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav; };

struct SmokeFx
{
    bool tried = false, ok = false;
    double h = 0.2, h0 = 0.2, eps = 0.4; int iters = 30; bool on = true;
    ComPtr<ID3D11ComputeShader> csAdv, csCurl, csForce, csDiv, csJac, csProj, csLight, csOcc;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps, psShim;
    ComPtr<ID3D11Buffer> cb; ComPtr<ID3D11BlendState> bs; ComPtr<ID3D11SamplerState> samp;
    Vol vel[2], den[2], curl, div, pr[2], light, occ;
    bool fresh = true;                 // cleared on the next update
    bool have = false;                 // a box is placed
    int ox = 0, oy = 0, oz = 0;        // the box's corner in the local frame [cells]
    VECTOR3 aP0 = { 0, 0, 0 };         // the local frame it was placed in
    std::map<std::pair<OBJHANDLE, int>, VECTOR3> lastExit;   // each source's exit last frame (local frame)
    double lastSrc = -1e9, decay = 0;  // when a source last ran; the visible mass's decay [1/s]
    CB k = {};
    bool live = false;

    bool MakeVol(ID3D11Device* dev, Vol& v, int x, int y, int z, DXGI_FORMAT f)
    {
        D3D11_TEXTURE3D_DESC d = { (UINT)x, (UINT)y, (UINT)z, 1, f, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
        return SUCCEEDED(dev->CreateTexture3D(&d, nullptr, &v.t)) && SUCCEEDED(dev->CreateShaderResourceView(v.t.Get(), nullptr, &v.srv)) &&
               SUCCEEDED(dev->CreateUnorderedAccessView(v.t.Get(), nullptr, &v.uav));
    }

    void ReadCfg()
    {
        FILE* f = fopen("Config\\D3D11Client\\Exhaust.cfg", "r");
        if (!f) return;
        char line[512];
        while (fgets(line, sizeof line, f)) {
            char* sc = strchr(line, ';'); if (sc) *sc = 0;
            char key[64]; double v;
            if (sscanf(line, " %63[A-Za-z0-9] = %lf", key, &v) != 2) continue;
            if (!strcmp(key, "Smoke")) on = v != 0;
            else if (!strcmp(key, "SmokeCell")) h = h0 = std::clamp(v, 0.05, 1.0);
            else if (!strcmp(key, "SmokeConfine")) eps = std::clamp(v, 0.0, 2.0);
            else if (!strcmp(key, "SmokeIters")) iters = std::clamp((int)v, 4, 120) & ~1;
        }
        fclose(f);
    }

    bool Init(D3D11Client* gc)
    {
        if (tried) return ok;
        tried = true;
        ReadCfg();
        if (!on) { LOGC("smoke flow: off (Exhaust.cfg Smoke = 0)"); return false; }
        ID3D11Device* dev = gc->Dev();
        auto csh = [&](const char* e, ComPtr<ID3D11ComputeShader>& s) { auto b = FxCompile("smoke.hlsl", e, "cs_5_0"); return b && SUCCEEDED(dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s)); };
        auto bv = FxCompile("smoke.hlsl", "VSDraw", "vs_5_0"), bp = FxCompile("smoke.hlsl", "PSDraw", "ps_5_0"), bs2 = FxCompile("smoke.hlsl", "PSShimmer", "ps_5_0");
        if (!csh("CSAdvect", csAdv) || !csh("CSCurl", csCurl) || !csh("CSForces", csForce) || !csh("CSDiv", csDiv) ||
            !csh("CSJacobi", csJac) || !csh("CSProject", csProj) || !csh("CSLight", csLight) || !csh("CSOcc", csOcc) || !bv || !bp ||
            FAILED(dev->CreateVertexShader(bv->GetBufferPointer(), bv->GetBufferSize(), nullptr, &vs)) ||
            FAILED(dev->CreatePixelShader(bp->GetBufferPointer(), bp->GetBufferSize(), nullptr, &ps)) || !bs2 ||
            FAILED(dev->CreatePixelShader(bs2->GetBufferPointer(), bs2->GetBufferSize(), nullptr, &psShim))) { LOGC("smoke flow: shaders FAILED"); return false; }
        D3D11_BUFFER_DESC c = { (UINT)((sizeof(CB) + 15) & ~15u), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(dev->CreateBuffer(&c, nullptr, &cb))) return false;
        bool r = true;
        for (int i = 0; i < 2; i++) {
            r = r && MakeVol(dev, vel[i], NX, NY, NZ, DXGI_FORMAT_R16G16B16A16_FLOAT) && MakeVol(dev, den[i], NX, NY, NZ, DXGI_FORMAT_R32_FLOAT) &&
                MakeVol(dev, pr[i], NX, NY, NZ, DXGI_FORMAT_R32_FLOAT);
        }
        r = r && MakeVol(dev, curl, NX, NY, NZ, DXGI_FORMAT_R16G16B16A16_FLOAT) && MakeVol(dev, div, NX, NY, NZ, DXGI_FORMAT_R32_FLOAT) &&
            MakeVol(dev, light, NX / 2, NY / 2, NZ / 2, DXGI_FORMAT_R16G16_FLOAT) && MakeVol(dev, occ, NX / 8, NY / 8, NZ / 8, DXGI_FORMAT_R16G16_FLOAT);
        if (!r) { LOGC("smoke flow: volumes FAILED"); return false; }
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
        if (FAILED(dev->CreateBlendState(&bd, &bs))) return false;
        D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(dev->CreateSamplerState(&sd, &samp))) return false;
        LOGC("smoke flow: %dx%dx%d cells of %.2f m (%.1f x %.1f x %.1f m), confinement %.2f, %d pressure iterations",
             NX, NY, NZ, h, NX * h, NY * h, NZ * h, eps, iters);
        ok = true;
        return true;
    }

    void Clear(ID3D11DeviceContext* ctx)
    {
        const float z[4] = {};
        for (int i = 0; i < 2; i++) { ctx->ClearUnorderedAccessViewFloat(vel[i].uav.Get(), z); ctx->ClearUnorderedAccessViewFloat(den[i].uav.Get(), z); ctx->ClearUnorderedAccessViewFloat(pr[i].uav.Get(), z); }
        ctx->ClearUnorderedAccessViewFloat(light.uav.Get(), z);
    }

    void Pass(ID3D11DeviceContext* ctx, ID3D11ComputeShader* s, std::initializer_list<ID3D11ShaderResourceView*> srv,
              std::initializer_list<std::pair<int, ID3D11UnorderedAccessView*>> uav, int gx, int gy, int gz)
    {
        ID3D11ShaderResourceView* sr[5] = {}; int i = 0;
        for (auto v : srv) if (i < 5) sr[i++] = v;
        ctx->CSSetShaderResources(0, 5, sr);
        ID3D11UnorderedAccessView* u[6] = {};
        for (auto& p : uav) u[p.first] = p.second;
        ctx->CSSetUnorderedAccessViews(0, 6, u, nullptr);
        ctx->CSSetShader(s, nullptr, 0);
        ctx->Dispatch(gx, gy, gz);
        ID3D11UnorderedAccessView* nu[6] = {}; ID3D11ShaderResourceView* ns[5] = {};
        ctx->CSSetUnorderedAccessViews(0, 6, nu, nullptr);
        ctx->CSSetShaderResources(0, 5, ns);
    }

    void Update(D3D11Client* gc, const ExScene& sc, const std::vector<SmokeSrc>& src)
    {
        live = false;
        if (!sc.anchored || !sc.dense) { have = false; return; }
        if (!src.empty()) lastSrc = sc.simt;
        if (!have && src.empty()) return;
        if (sc.simt - lastSrc > 90) { have = false; return; }       // all gone (or settled) long ago
        if (!Init(gc)) return;
        ID3D11DeviceContext* ctx = gc->Ctx();
        // the box: round the focus, its floor a little under the ground (the local frame's y is up)
        OBJHANDLE f = oapiGetFocusObject();
        if (!f || !oapiIsVessel(f)) return;
        VESSEL* v = oapiGetVesselInterface(f);
        VECTOR3 fg; v->GetGlobalPos(fg);
        const VECTOR3 fl = sc.ToLocal(fg);
        {   // the cell by the focus's size: the box ~6 of its radii across (a suit: 0.2 m cells, 26 m; the DG: ~0.5 m, 60 m)
            const double hw = std::clamp(std::max(h0, 6 * oapiGetSize(f) / NX), h0, 2.0);
            if (fabs(hw - h) > 1e-6 * hw) { h = hw; have = false; }
        }
        const double Rground = length(fg - sc.pp) - v->GetAltitude(ALTMODE_GROUND);
        double gyL = Rground - length(sc.aP);
        if (sc.Rliq > 0) gyL = std::max(gyL, sc.Rliq - length(sc.aP));
        // the box stays put on the ground (what the jets left keeps drifting there); it moves on - by whole cells, the
        // gas carried over - only when the focus comes within a quarter of its edge (or leaves the floor's band)
        int nx = ox, ny = oy, nz = oz;
        const double fx = fl.x / h - ox, fz = fl.z / h - oz, fyc = fl.y / h - oy;
        if (!have || fx < NX / 4 || fx > 3 * NX / 4) nx = (int)floor(fl.x / h) - NX / 2;
        if (!have || fz < NZ / 4 || fz > 3 * NZ / 4) nz = (int)floor(fl.z / h) - NZ / 2;
        const int nyWant = (int)floor(std::max(gyL - 3 * h, fl.y - 0.3 * NY * h) / h);
        if (!have || fyc < 3 || fyc > 0.75 * NY || abs(nyWant - oy) > NY / 4) ny = nyWant;
        int sx = nx - ox, sy = ny - oy, sz = nz - oz;
        if (!have || length(sc.aP - aP0) > 1e-3 || abs(sx) >= NX / 2 || abs(sy) >= NY / 2 || abs(sz) >= NZ / 2) { fresh = true; sx = sy = sz = 0; }
        ox = nx; oy = ny; oz = nz; aP0 = sc.aP; have = true;
        if (fresh) { Clear(ctx); fresh = false; }
        const VECTOR3 O = _V(ox * h, oy * h, oz * h);
        // the constants
        k.n = { (float)NX, (float)NY, (float)NZ, (float)h };
        const double dt = std::min(sc.dt, 1.0 / 30);
        for (const SmokeSrc& s : src) decay = s.decay;
        k.dt = { (float)dt, (float)eps, 0.7f, (float)decay };   // (heat: mixes out into the air in ~1.5 s beyond the resolved eddies)
        k.shift = { sx, sy, sz, (int)std::min<size_t>(src.size(), NSRC) };
        const double g = sc.GM / (sc.R * sc.R);
        k.amb = { (float)(gyL - O.y), (float)g, (float)std::max(sc.Tg, 20.0), (float)fmod(sc.simt, 1000.0) };
        VECTOR3 sun = _V(0, 0, 0);
        for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE hb = oapiGetGbodyByIndex(i); if (oapiGetObjectType(hb) == OBJTP_STAR) { oapiGetGlobalPos(hb, &sun); break; } }
        const VECTOR3 sl = Unit(sc.DirToLocal(Unit(sun - fg)));
        k.sun = { (float)sl.x, (float)sl.y, (float)sl.z, 1700.0f };   // ice of ~1 um: 1700 m2/kg (Schumann 1996)
        {   // the crystals' Stokes settling (r ~ 1 um: ~1e-4 m/s - the cloud's own weight moves it more), the air's
            // density for that weight, its refractivity n - 1 = 2.3e-4 rho (N2, air, CO2), the focal length [px]
            const double r = 1e-6, mu = sc.mu > 0 ? sc.mu : 1.7e-5;
            const double ws = 2.0 / 9.0 * (917.0 - sc.rhoG) * g * r * r / mu;
            const double focal = 0.5 * gc->SceneH() / std::max(oapiCameraAperture() > 0 ? tan(oapiCameraAperture()) : 1.0, 1e-3);
            k.phys = { (float)ws, (float)(1.0 / std::max(sc.rhoG, 1e-3)), (float)(2.3e-4 * sc.rhoG), (float)focal };
        }
        for (int i = 0; i < NSRC * 4; i++) k.src[i] = {};
        std::map<std::pair<OBJHANDLE, int>, VECTOR3> seen;
        std::vector<SmokeSrc> top(src.begin(), src.end());   // the strongest eight (by momentum)
        std::sort(top.begin(), top.end(), [](const SmokeSrc& a, const SmokeSrc& b) { return a.kJ > b.kJ; });
        for (size_t i = 0; i < top.size() && i < NSRC; i++) {
            const SmokeSrc& s = top[i];
            const VECTOR3 el = sc.ToLocal(s.eg), e = el - O, d = Unit(sc.DirToLocal(s.dg));
            k.src[4 * i] = F4(e, s.rExit);
            k.src[4 * i + 1] = F4(d, s.kJ);
            k.src[4 * i + 2] = { (float)s.mVis, (float)s.zFull, (float)s.inject, (float)s.Tex };
            // where its exit was a frame ago (the local frame): the mass is laid along the way, a step per half cell
            auto it = lastExit.find({ s.h, s.id });
            VECTOR3 ep = it != lastExit.end() && length(it->second - el) < 20 ? it->second : el;
            const int K = std::clamp((int)ceil(length(el - ep) / (0.5 * h)), 1, 16);
            k.src[4 * i + 3] = F4(ep - O, K);
            seen[{ s.h, s.id }] = el;
        }
        lastExit.swap(seen);
        // camera-centred drawing frame
        const VECTOR3 e1 = mul(sc.Rp, sc.a1), e2 = mul(sc.Rp, sc.aU), e3 = mul(sc.Rp, sc.a2);
        k.o = F4(sc.LocalToGlobal(O) - sc.cp, 48);
        k.e1 = F4(e1, NX * h); k.e2 = F4(e2, NY * h); k.e3 = F4(e3, NZ * h);
        XMStoreFloat4x4(&k.vp, XMMatrixTranspose(g_exViewProj));
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ctx->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        memcpy(m.pData, &k, sizeof k); ctx->Unmap(cb.Get(), 0);
        ctx->CSSetConstantBuffers(5, 1, cb.GetAddressOf());
        ctx->CSSetSamplers(3, 1, samp.GetAddressOf());
        const int gx = NX / 8, gy = NY / 8, gz = NZ / 8;
        if (dt > 0) {
            // state: vel[1], den[0]
            Pass(ctx, csAdv.Get(), { vel[1].srv.Get(), den[0].srv.Get() }, { { 0, vel[0].uav.Get() }, { 1, den[1].uav.Get() } }, gx, gy, gz);
            Pass(ctx, csCurl.Get(), { vel[0].srv.Get() }, { { 2, curl.uav.Get() } }, gx, gy, gz);
            Pass(ctx, csForce.Get(), { vel[0].srv.Get(), den[1].srv.Get(), curl.srv.Get() }, { { 0, vel[1].uav.Get() }, { 1, den[0].uav.Get() } }, gx, gy, gz);
            Pass(ctx, csDiv.Get(), { vel[1].srv.Get() }, { { 3, div.uav.Get() } }, gx, gy, gz);
            ExGpuMark(2);
            for (int it = 0; it < iters; it++) {
                const int a = it & 1, b = a ^ 1;
                Pass(ctx, csJac.Get(), { nullptr, nullptr, nullptr, pr[a].srv.Get(), div.srv.Get() }, { { 3, pr[b].uav.Get() } }, gx, gy, gz);
            }
            ExGpuMark(3);
            Pass(ctx, csProj.Get(), { vel[1].srv.Get(), nullptr, nullptr, pr[0].srv.Get() }, { { 0, vel[0].uav.Get() } }, gx, gy, gz);
            ctx->CopyResource(vel[1].t.Get(), vel[0].t.Get());
        }
        Pass(ctx, csLight.Get(), { nullptr, den[0].srv.Get() }, { { 4, light.uav.Get() } }, NX / 16, NY / 16, NZ / 16);
        Pass(ctx, csOcc.Get(), { vel[1].srv.Get(), den[0].srv.Get() }, { { 5, occ.uav.Get() } }, NX / 32, NY / 32, NZ / 32);
        ctx->CSSetShader(nullptr, nullptr, 0);
        live = true;
    }

    void Render(D3D11Client* gc)
    {
        if (!live) return;
        ID3D11DeviceContext* ctx = gc->Ctx();
        FxBindLight(gc);
        ctx->OMSetBlendState(bs.Get(), nullptr, ~0u);
        ctx->RSSetState(gc->rsNone.Get());
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.Get(), nullptr, 0); ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(5, 1, cb.GetAddressOf()); ctx->PSSetConstantBuffers(5, 1, cb.GetAddressOf());
        ID3D11ShaderResourceView* s[4] = { den[0].srv.Get(), light.srv.Get(), nullptr, occ.srv.Get() };
        ctx->PSSetShaderResources(22, 4, s);
        ctx->PSSetSamplers(3, 1, samp.GetAddressOf());
        ctx->Draw(36, 0);
        ID3D11ShaderResourceView* n[4] = {};
        ctx->PSSetShaderResources(22, 4, n);
    }

    void Shimmer(D3D11Client* gc)
    {
        if (!live) return;
        ID3D11DeviceContext* ctx = gc->Ctx();
        ctx->VSSetShader(vs.Get(), nullptr, 0); ctx->PSSetShader(psShim.Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(5, 1, cb.GetAddressOf()); ctx->PSSetConstantBuffers(5, 1, cb.GetAddressOf());
        ID3D11ShaderResourceView* s[2] = { vel[1].srv.Get(), occ.srv.Get() };
        ctx->PSSetShaderResources(24, 2, s);
        ctx->PSSetSamplers(3, 1, samp.GetAddressOf());
        ctx->Draw(36, 0);
        ID3D11ShaderResourceView* n[2] = {};
        ctx->PSSetShaderResources(24, 2, n);
    }

    void Release()
    {
        csAdv.Reset(); csCurl.Reset(); csForce.Reset(); csDiv.Reset(); csJac.Reset(); csProj.Reset(); csLight.Reset(); csOcc.Reset();
        vs.Reset(); ps.Reset(); psShim.Reset(); cb.Reset(); bs.Reset(); samp.Reset();
        for (int i = 0; i < 2; i++) { vel[i] = Vol(); den[i] = Vol(); pr[i] = Vol(); }
        curl = Vol(); div = Vol(); light = Vol(); occ = Vol();
        tried = ok = have = live = false; fresh = true;
    }
};

SmokeFx smoke;

}  // namespace

bool SmokeEnabled()
{
    static int on = -1;
    if (on < 0) {
        on = 1;
        if (FILE* f = fopen("Config\\D3D11Client\\Exhaust.cfg", "r")) {
            char line[512];
            while (fgets(line, sizeof line, f)) { double v; if (sscanf(line, " Smoke = %lf", &v) == 1) on = v != 0; }
            fclose(f);
        }
    }
    return on != 0;
}
void SmokeUpdate(D3D11Client* gc, const ExScene& sc, const std::vector<SmokeSrc>& src) { smoke.Update(gc, sc, src); }
void SmokeRender(D3D11Client* gc) { smoke.Render(gc); }
void SmokeRelease() { smoke.Release(); }
bool SmokeLive() { return smoke.live; }
void SmokeShimmer(D3D11Client* gc) { smoke.Shimmer(gc); }
