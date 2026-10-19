// The wave field (FxWaves.h), ported from the Titan demo: a 1024^2 x 0.25 m window around the focus on the liquid,
// linear deep-water gravity waves evolved exactly per Fourier mode (omega^2 = g k with the planet's surface gravity),
// sources between steps: the jets' stagnation pressure where their axis meets the liquid (AtmoFX_SetJet: q = T / (pi r^2),
// the jet widening at 12 deg from its exit radius), the volume moved by the parts in the liquid (AtmoFX_SetWake: a floating
// part of half-width r and strength s drafts ~ s r / 2, as in AtmoFXWake.h's example; strength > 1 is a one-off impulse -
// a foot or a wheel entering the liquid - taken as a velocity-potential impulse (s - 1) r sqrt(g r) over the part's disc,
// an estimate), the spray's drops (FxSpray).
// The window is a tangent frame fixed to the rotating planet at an anchor; it moves by whole cells with the focus and is
// re-anchored (the field restarts) when the focus has gone 20 km away. Liquid: methane-ethane-nitrogen, 470 kg/m3,
// kinematic viscosity 4.2e-7 m2/s (Titan's seas at 94 K; as in the demo). Not modelled: the shore (waves run on under land,
// unseen), shallow-water dispersion (tanh kh).
#include "FxWaves.h"

namespace {

struct WaveCB
{
    int origin[4];
    float P[4];
    int counts[4];
    float grid[4];
    XMFLOAT4 jets[8];
    XMFLOAT4 bodies[32 * 2];
};

const double RHO_L = 470.0, NU_L = 4.2e-7, JET_SPREAD = 0.2126;   // tan 12 deg

}  // namespace

bool WaveField::MakeTex(ID3D11Device* dev, Tex& t, DXGI_FORMAT f)
{
    D3D11_TEXTURE2D_DESC d = { N, N, 1, 1, f, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &t.t))) return false;
    dev->CreateShaderResourceView(t.t.Get(), nullptr, &t.srv);
    dev->CreateUnorderedAccessView(t.t.Get(), nullptr, &t.uav);
    return t.srv && t.uav;
}

bool WaveField::Init(D3D11Client* gc)
{
    ID3D11Device* dev = gc->Dev();
    auto cs = [&](const char* e, ComPtr<ID3D11ComputeShader>& s) {
        auto b = FxCompile("fx_waves.hlsl", e, "cs_5_0");
        return b && SUCCEEDED(dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s));
    };
    if (!cs("CSSource", csSource) || !cs("CSFFT", csFFT) || !cs("CSEvolve", csEvolve) || !cs("CSNormals", csNormals)) return false;
    for (auto& b : cb) {
        D3D11_BUFFER_DESC d = { (sizeof(WaveCB) + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(dev->CreateBuffer(&d, nullptr, &b))) return false;
    }
    if (!MakeTex(dev, stA, DXGI_FORMAT_R32G32_FLOAT) || !MakeTex(dev, stB, DXGI_FORMAT_R32G32_FLOAT) ||
        !MakeTex(dev, slopes, DXGI_FORMAT_R32G32B32A32_FLOAT) || !MakeTex(dev, impact, DXGI_FORMAT_R32_UINT)) return false;
    impactUAV = impact.uav;
    Clear(gc->Ctx());
    return true;
}

void WaveField::Release()
{
    csSource.Reset(); csFFT.Reset(); csEvolve.Reset(); csNormals.Reset();
    for (auto& b : cb) b.Reset();
    stA = {}; stB = {}; slopes = {}; impact = {}; impactUAV.Reset();
    anchored = false; planet = nullptr; prevBodies.clear(); view = {};
}

// the field's height range (a readback of the slopes texture, for the log only)
std::string WaveField::HeightRange(ID3D11DeviceContext* ctx)
{
    ComPtr<ID3D11Device> dev; ctx->GetDevice(&dev);
    D3D11_TEXTURE2D_DESC d = { N, N, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
    ComPtr<ID3D11Texture2D> st; if (FAILED(dev->CreateTexture2D(&d, nullptr, &st))) return "?";
    ctx->CopyResource(st.Get(), slopes.t.Get());
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) return "?";
    float mn = 1e9f, mx = -1e9f, smax = 0; int bad = 0;
    for (int y = 0; y < N; y++) {
        const XMFLOAT4* row = (const XMFLOAT4*)((const BYTE*)m.pData + (size_t)y * m.RowPitch);
        for (int x = 0; x < N; x++) {
            const XMFLOAT4& v = row[x];
            if (!std::isfinite(v.z)) { bad++; continue; }
            mn = std::min(mn, v.z); mx = std::max(mx, v.z); smax = std::max(smax, sqrtf(v.x * v.x + v.y * v.y));
        }
    }
    ctx->Unmap(st.Get(), 0);
    char b[128]; sprintf_s(b, "%.4f .. %.4f m, max slope %.4f, non-finite %d", mn, mx, smax, bad);
    return b;
}

void WaveField::Clear(ID3D11DeviceContext* ctx)
{
    const float z[4] = {}; const UINT u[4] = {};
    ctx->ClearUnorderedAccessViewFloat(stA.uav.Get(), z);
    ctx->ClearUnorderedAccessViewFloat(stB.uav.Get(), z);
    ctx->ClearUnorderedAccessViewFloat(slopes.uav.Get(), z);
    ctx->ClearUnorderedAccessViewUint(impact.uav.Get(), u);
}

// the planet with a liquid the camera is near (within 20 km of its level)
Planet* WaveField::FindLiquid(const VECTOR3& cp) const
{
    if (!g_fxPlanets) return nullptr;
    for (Planet* p : *g_fxPlanets) {
        if (!p->liq.has) continue;
        VECTOR3 q; oapiGetGlobalPos(p->hObj, &q);
        double alt = length(cp - q) - (p->R + p->liq.level);
        if (alt > -50 && alt < 20000) return p;
    }
    return nullptr;
}

void WaveField::Frame(D3D11Client* gc, const VECTOR3& cp, double simDt)
{
    ID3D11DeviceContext* ctx = gc->Ctx();
    Planet* p = g_cfg.liquids ? FindLiquid(cp) : nullptr;
    if (!p) { view.on = false; anchored = false; return; }
    VECTOR3 Pp; oapiGetGlobalPos(p->hObj, &Pp);
    MATRIX3 Rp; oapiGetRotationMatrix(p->hObj, &Rp);
    double Rl = p->R + p->liq.level;
    // the window's centre: the focus when it is near the camera, else the camera
    VECTOR3 c = cp;
    if (OBJHANDLE f = oapiGetFocusObject()) { VECTOR3 fp; oapiGetGlobalPos(f, &fp); if (length(fp - cp) < 2000) c = fp; }
    VECTOR3 q = tmul(Rp, c - Pp);
    q = q * (Rl / length(q));
    if (!anchored || p != planet || length(q - anchorP) > 20000) {
        planet = p; anchorP = q;
        VECTOR3 up = q / length(q);
        VECTOR3 a = fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0);
        e1P = crossp(up, a); e1P = e1P / length(e1P);
        e2P = crossp(e1P, up);
        Clear(ctx);
        prevBodies.clear();
        anchored = false;
    }
    auto tangent = [&](const VECTOR3& pp) { VECTOR3 d = pp - anchorP; return std::make_pair(dotp(d, e1P), dotp(d, e2P)); };
    auto tc = tangent(q);
    oldI0 = i0; oldJ0 = j0;
    i0 = (int)floor(tc.first / DX) - N / 2;
    j0 = (int)floor(tc.second / DX) - N / 2;
    if (!anchored) { oldI0 = i0; oldJ0 = j0; anchored = true; }
    double dt = std::clamp(simDt, 0.0, 0.1);

    // the sources, in the window's tangent frame
    WaveCB w = {};
    double now = oapiGetSimTime();
    int nj = 0, nb = 0;
    std::map<ExtState::Key, Body> bodies;
    {
        std::lock_guard<std::mutex> lk(g_ext.mx);
        for (auto& kv : g_ext.jets) {
            const ExtState::Jet& j = kv.second;
            jetStat[0]++;
            if (j.thrust <= 0) { jetStat[1]++; continue; }
            if (now - j.t > 0.5) { jetStat[2]++; continue; }
            if (nj >= 8 || !oapiIsVessel(kv.first.h)) continue;
            VESSEL* v = oapiGetVesselInterface(kv.first.h);
            VECTOR3 ge, gd; v->Local2Global(j.pos, ge); v->GlobalRot(j.dir, gd);
            VECTOR3 pe = tmul(Rp, ge - Pp), de = tmul(Rp, gd);
            de = de / length(de);
            VECTOR3 up = pe / length(pe);
            double h = length(pe) - Rl, down = -dotp(de, up);
            if (down < 0.1 || h < -1) { jetStat[3]++; continue; }
            double s = std::max(h, 0.0) / down;
            if (s > 300) { jetStat[4]++; continue; }        // far above: the jet has spread out, no pressure worth it
            jetStat[5]++;
            VECTOR3 hit = pe + de * s;
            hit = hit * (Rl / length(hit));
            double r = std::max(j.rExit, 0.01) + s * JET_SPREAD;
            double qs = j.thrust / (PI * r * r);            // stagnation pressure of the jet at the surface
            auto t2 = tangent(hit);
            w.jets[nj++] = { (float)t2.first, (float)t2.second, (float)r, (float)qs };
        }
        for (auto& kv : g_ext.wakes) {
            const ExtState::Wake& k = kv.second;
            if (k.r <= 0 || !oapiIsVessel(kv.first.h)) continue;
            VESSEL* v = oapiGetVesselInterface(kv.first.h);
            VECTOR3 g; v->Local2Global(k.pos, g);
            VECTOR3 pe = tmul(Rp, g - Pp);
            double h = length(pe) - Rl;
            if (k.depth > 1) {        // a one-off impulse, once per call of the module
                double& seen = impulseSeen[kv.first];
                if (k.t > seen && nj < 8 && dt > 0 && fabs(h) < 2 * k.r + 0.5) {
                    double gs = 6.67430e-11 * oapiGetMass(p->hObj) / (p->R * p->R);
                    double phi = (k.depth - 1) * k.r * sqrt(gs * k.r);      // velocity-potential impulse [m2/s]
                    // a foot (r ~ 0.1 m) is smaller than the grid can carry (waves under ~4 cells are damped): the same total
                    // impulse spread over at least 2 cells
                    double R = std::max(k.r, 2 * DX);
                    phi *= (k.r * k.r) / (R * R);
                    auto t2 = tangent(pe);
                    w.jets[nj++] = { (float)t2.first, (float)t2.second, (float)R, (float)(phi * RHO_L / dt) };   // as a pressure for one step
                    impulses++;
                }
                seen = k.t;
                continue;
            }
            if (nb >= 32) continue;
            double draft = fabs(h) < k.r ? 0.5 * k.r * std::min(k.depth, 1.0) : 0.0;   // at the waterline only
            auto t2 = tangent(pe);
            double R = std::max(k.r, 2 * DX);              // narrower than the grid carries: the same volume over 2 cells
            Body b = { (float)t2.first, (float)t2.second, (float)R, (float)(draft * (k.r * k.r) / (R * R)) };
            bodies[kv.first] = b;
            auto pr = prevBodies.find(kv.first);
            Body o = pr != prevBodies.end() ? pr->second : b;          // first seen: no source
            w.bodies[nb * 2 + 0] = { b.x, b.z, b.r, b.d };
            w.bodies[nb * 2 + 1] = { o.x, o.z, o.r, o.d };
            nb++;
        }
    }
    prevBodies = bodies;
    // test runs only (environment D3D11CLIENT_FXWAVETEST=1): the demo's source - the «Модуль полёта» hovering 1.2 m over the
    // liquid under the focus, two nozzles of 87 N (weight / 2 each), exit radius 40.5 mm - since a headless run cannot fly it
    static int testSrc = -1;
    if (testSrc < 0) { char e[8] = ""; testSrc = GetEnvironmentVariableA("D3D11CLIENT_FXWAVETEST", e, sizeof e) && e[0] == '1' ? 1 : 0; }
    if (testSrc == 1 && nj + 2 <= 8) {
        double s = 1.2, r = 0.0405 + s * JET_SPREAD, qs = 87.0 / (PI * r * r);
        for (int k = 0; k < 2; k++) w.jets[nj++] = { (float)(tc.first + (k ? -0.6 : 0.6)), (float)tc.second, (float)r, (float)qs };
    }
    // log the sources now and then
    if (now - logT > 10) {
        logT = now;
        LOGC("waves on %s: window at cell %d,%d (%.0f m), dt %.3f s, g %.3f m/s2, jets %d, parts %d, impulses so far %d, height %s; "
             "jet calls since the last line %d (thrust 0: %d, stale: %d, not downwards: %d, over 300 m: %d, used: %d)",
             p->name.c_str(), i0, j0, N * DX, dt, 6.67430e-11 * oapiGetMass(p->hObj) / (p->R * p->R), nj, nb, impulses, HeightRange(ctx).c_str(),
             jetStat[0], jetStat[1], jetStat[2], jetStat[3], jetStat[4], jetStat[5]);
        for (int& k : jetStat) k = 0;
    }

    if (dt > 0) {
        double g = 6.67430e-11 * oapiGetMass(p->hObj) / (p->R * p->R);
        w.origin[0] = i0; w.origin[1] = j0; w.origin[2] = oldI0; w.origin[3] = oldJ0;
        w.P[0] = (float)dt; w.P[1] = (float)g; w.P[2] = (float)RHO_L; w.P[3] = (float)NU_L;
        w.counts[0] = nb; w.counts[1] = nj;
        w.grid[0] = (float)DX; w.grid[1] = 48;
        const int inv[4] = { 0, 0, 1, 1 }, col[4] = { 0, 1, 0, 1 };
        for (int k = 0; k < 4; k++) {
            w.counts[2] = inv[k]; w.counts[3] = col[k];
            D3D11_MAPPED_SUBRESOURCE ms;
            ctx->Map(cb[k].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &w, sizeof w); ctx->Unmap(cb[k].Get(), 0);
        }
        Tex* S0 = cur ? &stB : &stA;
        Tex* S1 = cur ? &stA : &stB;
        ID3D11UnorderedAccessView* nulU[4] = {};
        ID3D11ShaderResourceView* nulS = nullptr;
        ctx->CSSetConstantBuffers(2, 1, cb[0].GetAddressOf());
        ctx->CSSetShader(csSource.Get(), nullptr, 0);
        ID3D11UnorderedAccessView* u[3] = { S0->uav.Get(), nullptr, impact.uav.Get() };
        ctx->CSSetUnorderedAccessViews(0, 3, u, nullptr);
        ctx->Dispatch(N / 8, N / 8, 1);
        ctx->CSSetUnorderedAccessViews(0, 4, nulU, nullptr);
        auto pass = [&](ID3D11ComputeShader* sh, int slot, Tex* in, Tex* out, UINT gx, UINT gy) {
            ctx->CSSetShader(sh, nullptr, 0);
            ctx->CSSetConstantBuffers(2, 1, cb[slot].GetAddressOf());
            ctx->CSSetShaderResources(0, 1, in->srv.GetAddressOf());
            ctx->CSSetUnorderedAccessViews(1, 1, out->uav.GetAddressOf(), nullptr);
            ctx->Dispatch(gx, gy, 1);
            ctx->CSSetShaderResources(0, 1, &nulS);
            ctx->CSSetUnorderedAccessViews(1, 1, nulU, nullptr);
        };
        pass(csFFT.Get(), 0, S0, S1, N, 1);              // rows
        pass(csFFT.Get(), 1, S1, S0, N, 1);              // columns -> the spectrum in S0
        pass(csEvolve.Get(), 0, S0, S1, N / 8, N / 8);   // each mode rotated by omega dt
        pass(csFFT.Get(), 2, S1, S0, N, 1);              // inverse rows
        pass(csFFT.Get(), 3, S0, S1, N, 1);              // inverse columns -> the state in S1
        cur = !cur;
        ctx->CSSetShader(csNormals.Get(), nullptr, 0);
        ctx->CSSetConstantBuffers(2, 1, cb[0].GetAddressOf());
        ctx->CSSetShaderResources(0, 1, S1->srv.GetAddressOf());
        ctx->CSSetUnorderedAccessViews(3, 1, slopes.uav.GetAddressOf(), nullptr);
        ctx->Dispatch(N / 8, N / 8, 1);
        ctx->CSSetShaderResources(0, 1, &nulS);
        ctx->CSSetUnorderedAccessViews(3, 1, nulU, nullptr);
        ctx->CSSetShader(nullptr, nullptr, 0);
    }

    // for the liquid's shader: the frame in camera-centred global coordinates
    view.on = true;
    view.planet = p;
    view.anchor = mul(Rp, anchorP) + Pp - cp;
    view.e1 = mul(Rp, e1P); view.e2 = mul(Rp, e2P);
    view.dx = DX; view.i0 = i0; view.j0 = j0; view.n = N;
    view.slopes = slopes.srv.Get();
}
