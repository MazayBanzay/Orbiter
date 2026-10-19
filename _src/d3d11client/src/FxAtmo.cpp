// The FX atmosphere (FxAtmo.h): per planet, computed once on the GPU, transmittance, multiple scattering and sky
// irradiance tables (shaders\fx_atmo_lut.hlsl). Per frame: one full-screen pass per planet in view after the opaque
// scene, from its depth (shaders\fx_atmo.hlsl); and the light at any point for the client's shaders
// (shaders\fx_atmo_light.hlsli: sunlight through the air, skylight) for the planet the camera is at. The client stands
// its own sky and haze aside while g_fxAtmo.active (Client.cpp UpdateFrameCB).
#include "FxAtmo.h"

namespace {

struct MediaCB    // = cbuffer FxAtmoMedia (fx_atmo_light.hlsli)
{
    XMFLOAT4 centre, geo, sunDir, ray, mieExt, mieSca, mieG, abs, ground, ray2;
};
struct PassCB     // = cbuffer AtmoPass (fx_atmo.hlsl)
{
    XMFLOAT4 row0, row1, row2, tan, cam, sun;
};

const int TW = 256, TH = 64, IW = 64, IH = 32, MSN = 64;   // = FX_TW, FX_TH, FX_IW, FX_IH (fx_atmo_light.hlsli), MS_N

VECTOR3 SunGlobal()
{
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) {
        OBJHANDLE h = oapiGetGbodyByIndex(i);
        if (oapiGetObjectType(h) == OBJTP_STAR) { VECTOR3 p; oapiGetGlobalPos(h, &p); return p; }
    }
    return _V(0, 0, 0);
}

}  // namespace

// ------------------------------------------------------------------------------------------ AtmoMedia
bool AtmoMedia::Load(const Planet* p)
{
    FILE* f = fopen(("Config\\D3D11Client\\" + p->name + ".atmo.cfg").c_str(), "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char* sc = strchr(line, ';'); if (sc) *sc = 0;
        char key[64]; double a, b, c;
        int n = sscanf(line, " %63[A-Za-z0-9] = %lf %lf %lf", key, &a, &b, &c);
        if (n < 2) continue;
        std::string k = key;
        VECTOR3 v = _V(a, n > 2 ? b : a, n > 3 ? c : a);
        if (k == "Top") top = a;
        else if (k == "RayleighScatter") ray = v;
        else if (k == "RayleighH") rayH = a;
        else if (k == "RayleighBreak") rayBreak = a;
        else if (k == "RayleighH2") rayH2 = a;
        else if (k == "MieExt") mieExt = v;
        else if (k == "MieSSA") mieSSA = v;
        else if (k == "MieG") mieG = v;
        else if (k == "MieH") mieH = a;
        else if (k == "MieLayer") { mieBase = a; mieTop = n > 2 ? b : a; }
        else if (k == "AbsorbExt") absExt = v;
        else if (k == "AbsorbCentre") absCentre = a;
        else if (k == "AbsorbHalfWidth") absHalf = a;
        else if (k == "GroundAlbedo") albedo = v;
    }
    fclose(f);
    if (top <= 0) top = 12 * rayH;
    fromFile = true;
    return true;
}

bool AtmoMedia::ClearGas(const Planet* p)
{
    // Earth's Rayleigh scattering (Bruneton 2017, 680/550/440 nm) scaled by the density at the ground (estimate)
    double H = p->atm.scaleH;
    if (p->atm.rho0 < 1e-4 || H < 100) return false;        // no air worth drawing (Io)
    if (p->R > 2e7) return false;     // gas giants: Orbiter's "ground" is their cloud deck - no data here, drawn as before
    ray = _V(5.802e-6, 13.558e-6, 33.1e-6) * (p->atm.rho0 / 1.225);
    rayH = H;
    top = std::min(p->atm.top > 0 ? p->atm.top : 1e9, 12 * H);
    return true;
}

double AtmoMedia::RayDensity(double h) const
{
    return (rayBreak <= 0 || h <= rayBreak) ? exp(-h / rayH) : exp(-rayBreak / rayH - (h - rayBreak) / rayH2);
}

double AtmoMedia::MieDensity(double h) const
{
    if (mieBase < 0) return exp(-h / mieH);
    if (h < mieBase) return exp(-(mieBase - h) / 1000.0);
    return h <= mieTop ? 1.0 : exp(-(h - mieTop) / mieH);
}

// ------------------------------------------------------------------------------------------ PlanetAtmosphere
bool PlanetAtmosphere::Create(ID3D11Device* dev)
{
    // the altitude below which the air is opaque seen from above: vertical optical depth from the top > 20 (green)
    const AtmoMedia& md = media;
    double tau = 0, dh = 100;
    for (double h = md.top; h > 0; h -= dh) {
        tau += (md.ray.y * md.RayDensity(h - dh / 2) + md.mieExt.y * md.MieDensity(h - dh / 2) +
                (md.absHalf > 0 ? md.absExt.y * std::max(0.0, 1 - fabs(h - dh / 2 - md.absCentre) / md.absHalf) : 0)) * dh;
        if (tau > 20) { opaqueAlt = h; break; }
    }
    D3D11_BUFFER_DESC bd = { (sizeof(MediaCB) + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    dev->CreateBuffer(&bd, nullptr, &cb);
    auto tex = [&](UINT w, UINT h, UINT bind, ComPtr<ID3D11Texture2D>& t) {
        D3D11_TEXTURE2D_DESC td = { w, h, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, bind, 0, 0 };
        dev->CreateTexture2D(&td, nullptr, &t);
    };
    UINT rw = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    tex(TW, TH, rw, trans); tex(MSN, MSN, rw, ms); tex(IW, IH, rw, irr); tex(TW, TH + IH, D3D11_BIND_SHADER_RESOURCE, lut);
    if (!cb || !trans || !ms || !irr || !lut) return false;
    dev->CreateUnorderedAccessView(trans.Get(), nullptr, &transUAV);
    dev->CreateUnorderedAccessView(ms.Get(), nullptr, &msUAV);
    dev->CreateUnorderedAccessView(irr.Get(), nullptr, &irrUAV);
    dev->CreateShaderResourceView(trans.Get(), nullptr, &transSRV);
    dev->CreateShaderResourceView(ms.Get(), nullptr, &msSRV);
    dev->CreateShaderResourceView(lut.Get(), nullptr, &lutSRV);
    const AtmoMedia& m = media;
    LOGC("atmosphere %s: top %.0f km, Rayleigh %.3g %.3g %.3g /m H %.1f km%s, aerosol ext %.3g %.3g %.3g /m SSA %.4g %.4g %.4g g %.2f %.2f %.2f H %.1f km%s, absorber %.3g %.3g %.3g /m, albedo %.2f %.2f %.2f - %s",
         planet->name.c_str(), m.top / 1000, m.ray.x, m.ray.y, m.ray.z, m.rayH / 1000, m.rayBreak > 0 ? " (two-step)" : "",
         m.mieExt.x, m.mieExt.y, m.mieExt.z, m.mieSSA.x, m.mieSSA.y, m.mieSSA.z, m.mieG.x, m.mieG.y, m.mieG.z, m.mieH / 1000,
         m.mieBase >= 0 ? " (layer)" : "", m.absExt.x, m.absExt.y, m.absExt.z, m.albedo.x, m.albedo.y, m.albedo.z,
         m.fromFile ? (planet->name + ".atmo.cfg").c_str() : "clear gas from the density (estimate)");
    if (opaqueAlt > 0) LOGC("atmosphere %s: opaque from above below %.1f km", planet->name.c_str(), opaqueAlt / 1000);
    return transUAV && msUAV && irrUAV && transSRV && msSRV && lutSRV;
}

void PlanetAtmosphere::Update(ID3D11DeviceContext* ctx, const VECTOR3& cp, const VECTOR3& sunPos)
{
    const AtmoMedia& m = media;
    VECTOR3 pp; oapiGetGlobalPos(planet->hObj, &pp);
    VECTOR3 c = pp - cp;
    VECTOR3 sd = sunPos - pp; double sdist = length(sd); sd = sd / sdist;
    double R = planet->R, top = TopRadius();
    MediaCB k;
    k.centre = { (float)c.x, (float)c.y, (float)c.z, (float)R };
    k.geo = { (float)top, (float)sqrt(top * top - R * R), 1.0f, (float)(6.957e8 / sdist) };   // the Sun's angular radius
    k.sunDir = { (float)sd.x, (float)sd.y, (float)sd.z, 0 };
    k.ray = { (float)m.ray.x, (float)m.ray.y, (float)m.ray.z, (float)m.rayH };
    VECTOR3 sca = _V(m.mieExt.x * m.mieSSA.x, m.mieExt.y * m.mieSSA.y, m.mieExt.z * m.mieSSA.z);
    k.mieExt = { (float)m.mieExt.x, (float)m.mieExt.y, (float)m.mieExt.z, (float)m.mieH };
    k.mieSca = { (float)sca.x, (float)sca.y, (float)sca.z, (float)m.mieBase };
    k.mieG = { (float)m.mieG.x, (float)m.mieG.y, (float)m.mieG.z, (float)m.mieTop };
    k.abs = { (float)m.absExt.x, (float)m.absExt.y, (float)m.absExt.z, (float)m.absCentre };
    k.ground = { (float)m.albedo.x, (float)m.albedo.y, (float)m.albedo.z, (float)m.absHalf };
    k.ray2 = { (float)m.rayBreak, (float)m.rayH2, (float)opaqueAlt, 0 };
    D3D11_MAPPED_SUBRESOURCE ms2;
    ctx->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms2); memcpy(ms2.pData, &k, sizeof k); ctx->Unmap(cb.Get(), 0);
}

void PlanetAtmosphere::BuildTables(ID3D11DeviceContext* ctx, const AtmoTableShaders& s)
{
    ID3D11ShaderResourceView* nulS = nullptr;
    ID3D11UnorderedAccessView* nulU = nullptr;
    ctx->CSSetConstantBuffers(4, 1, cb.GetAddressOf());
    // transmittance
    ctx->CSSetShader(s.trans.Get(), nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 1, transUAV.GetAddressOf(), nullptr);
    ctx->Dispatch(TW / 8, TH / 8, 1);
    ctx->CSSetUnorderedAccessViews(0, 1, &nulU, nullptr);
    // multiple scattering (reads the transmittance)
    ctx->CSSetShaderResources(8, 1, transSRV.GetAddressOf());
    ctx->CSSetShader(s.ms.Get(), nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 1, msUAV.GetAddressOf(), nullptr);
    ctx->Dispatch(MSN, MSN, 1);
    ctx->CSSetUnorderedAccessViews(0, 1, &nulU, nullptr);
    // sky irradiance (reads both)
    ctx->CSSetShaderResources(1, 1, msSRV.GetAddressOf());
    ctx->CSSetShader(s.irr.Get(), nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 1, irrUAV.GetAddressOf(), nullptr);
    ctx->Dispatch(IW, IH, 1);
    ctx->CSSetUnorderedAccessViews(0, 1, &nulU, nullptr);
    ctx->CSSetShaderResources(1, 1, &nulS);
    ctx->CSSetShaderResources(8, 1, &nulS);
    ctx->CSSetShader(nullptr, nullptr, 0);
    // one texture for the client's shaders: transmittance on top, irradiance below
    ctx->CopySubresourceRegion(lut.Get(), 0, 0, 0, 0, trans.Get(), 0, nullptr);
    ctx->CopySubresourceRegion(lut.Get(), 0, 0, TH, 0, irr.Get(), 0, nullptr);
    tablesReady = true;
    ReadTables(ctx);
}

// the tables back to the CPU, once: the light at the camera (exposure) and a check in Orbiter.log
void PlanetAtmosphere::ReadTables(ID3D11DeviceContext* ctx)
{
    ComPtr<ID3D11Device> dev; ctx->GetDevice(&dev);
    auto read = [&](ID3D11Texture2D* t, UINT w, UINT h, std::vector<XMFLOAT4>& out) {
        D3D11_TEXTURE2D_DESC d = { w, h, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
        ComPtr<ID3D11Texture2D> st; if (FAILED(dev->CreateTexture2D(&d, nullptr, &st))) return false;
        ctx->CopyResource(st.Get(), t);
        D3D11_MAPPED_SUBRESOURCE m; if (FAILED(ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
        out.resize((size_t)w * h);
        for (UINT y = 0; y < h; y++) memcpy(&out[(size_t)y * w], (BYTE*)m.pData + (size_t)y * m.RowPitch, w * sizeof(XMFLOAT4));
        ctx->Unmap(st.Get(), 0);
        return true;
    };
    std::vector<XMFLOAT4>& T = cpuTrans, & I = cpuIrr, M;
    if (!read(trans.Get(), TW, TH, T) || !read(irr.Get(), IW, IH, I) || !read(ms.Get(), MSN, MSN, M)) { T.clear(); I.clear(); return; }
    // at the ground (row 0): x = 0 is the zenith (d = top - R), x = TW-1 the horizon; irradiance and MS at muS = 1
    const XMFLOAT4& tz = T[0], & th = T[TW - 1], & e = I[IW - 1], & ms0 = M[MSN - 1];
    LOGC("atmosphere %s tables: transmittance at the ground to the zenith %.3g %.3g %.3g, to the horizon %.3g %.3g %.3g; sky irradiance at noon %.3g %.3g %.3g; multiple scattering %.3g %.3g %.3g",
         planet->name.c_str(), tz.x, tz.y, tz.z, th.x, th.y, th.z, e.x, e.y, e.z, ms0.x, ms0.y, ms0.z);
}

// bilinear lookup in a CPU copy of a table (f in texels)
static VECTOR3 Bilinear(const std::vector<XMFLOAT4>& t, int w, int h, double fx, double fy)
{
    fx = std::clamp(fx, 0.0, w - 1.0); fy = std::clamp(fy, 0.0, h - 1.0);
    int x0 = (int)fx, y0 = (int)fy, x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    double ax = fx - x0, ay = fy - y0;
    auto at = [&](int x, int y) { const XMFLOAT4& v = t[(size_t)y * w + x]; return _V(v.x, v.y, v.z); };
    return (at(x0, y0) * (1 - ax) + at(x1, y0) * ax) * (1 - ay) + (at(x0, y1) * (1 - ax) + at(x1, y1) * ax) * ay;
}

// the light on a horizontal surface at the camera (the Sun outside the air = 1): the direct Sun through the air plus the
// sky - the same tables and parametrisation as fx_atmo_light.hlsli
VECTOR3 PlanetAtmosphere::LightAt(const VECTOR3& cp, const VECTOR3& sunPos, bool noon) const
{
    VECTOR3 pp; oapiGetGlobalPos(planet->hObj, &pp);
    VECTOR3 up = cp - pp; double r = length(up); up = up / r;
    VECTOR3 sd = sunPos - pp; sd = sd / length(sd);
    double R = planet->R, top = TopRadius(), mu = noon ? 1.0 : dotp(up, sd);
    if (r >= top || cpuTrans.empty()) return _V(1, 1, 1);     // in space: the open Sun
    r = std::max(r, R);
    double Hh = sqrt(top * top - R * R), rho = sqrt(std::max(r * r - R * R, 0.0));
    double d = std::max(-r * mu + sqrt(std::max(top * top - r * r * (1 - mu * mu), 0.0)), 0.0);
    double dmin = top - r, dmax = rho + Hh;
    VECTOR3 Ts = Bilinear(cpuTrans, TW, TH, (d - dmin) / std::max(dmax - dmin, 1e-3) * (TW - 1), rho / Hh * (TH - 1));
    double cosH = -sqrt(std::max(1 - (R / r) * (R / r), 0.0));
    if (mu < cosH) Ts = _V(0, 0, 0);
    VECTOR3 E = Bilinear(cpuIrr, IW, IH, (mu * 0.5 + 0.5) * (IW - 1), sqrt(std::clamp((r - R) / (top - R), 0.0, 1.0)) * (IH - 1));   // = FxAltX
    return Ts * std::max(mu, 0.0) + E;
}

// CPU estimate for the stars: the vertical transmittance and how much the sky scatters above the camera
float PlanetAtmosphere::StarVisibility(const VECTOR3& cp, const VECTOR3& sunPos) const
{
    const AtmoMedia& m = media;
    VECTOR3 pp; oapiGetGlobalPos(planet->hObj, &pp);
    VECTOR3 up = cp - pp; double r = length(up); up = up / r;
    double h0 = r - planet->R;
    if (h0 >= m.top) return 1;
    double tauG = 0, scaB = 0;
    const int N = 200;
    double dh = (m.top - h0) / N;
    for (int i = 0; i < N; i++) {
        double h = h0 + (i + 0.5) * dh;
        tauG += (m.ray.y * m.RayDensity(h) + m.mieExt.y * m.MieDensity(h)) * dh;
        scaB += (m.ray.z * m.RayDensity(h) + m.mieExt.z * m.mieSSA.z * m.MieDensity(h)) * dh;
    }
    VECTOR3 sd = sunPos - pp; sd = sd / length(sd);
    double day = std::clamp((dotp(up, sd) + 0.12) / 0.17, 0.0, 1.0);    // the sky stays bright to ~7 deg below the horizon
    return (float)(exp(-tauG) * (1 - day * std::min(scaB * 30, 1.0)));
}

// ------------------------------------------------------------------------------------------ Atmosphere
bool Atmosphere::Init(D3D11Client* gc)
{
    ID3D11Device* dev = gc->Dev();
    auto bv = FxCompile("fx_atmo.hlsl", "VS", "vs_5_0");
    auto bp = FxCompile("fx_atmo.hlsl", "PS", "ps_5_0");
    auto b1 = FxCompile("fx_atmo_lut.hlsl", "CSTrans", "cs_5_0");
    auto b2 = FxCompile("fx_atmo_lut.hlsl", "CSMS", "cs_5_0");
    auto b3 = FxCompile("fx_atmo_lut.hlsl", "CSIrr", "cs_5_0");
    if (!bv || !bp || !b1 || !b2 || !b3) return false;
    dev->CreateVertexShader(bv->GetBufferPointer(), bv->GetBufferSize(), nullptr, &vs);
    dev->CreatePixelShader(bp->GetBufferPointer(), bp->GetBufferSize(), nullptr, &ps);
    dev->CreateComputeShader(b1->GetBufferPointer(), b1->GetBufferSize(), nullptr, &tables.trans);
    dev->CreateComputeShader(b2->GetBufferPointer(), b2->GetBufferSize(), nullptr, &tables.ms);
    dev->CreateComputeShader(b3->GetBufferPointer(), b3->GetBufferSize(), nullptr, &tables.irr);
    D3D11_BUFFER_DESC d = { (sizeof(PassCB) + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    dev->CreateBuffer(&d, nullptr, &cbPass);
    // open space: the FX light gives the white Sun everywhere (top radius 0)
    MediaCB sp = {}; sp.geo = { 0, 0, 1, 0.0047f }; sp.ray.w = sp.mieExt.w = 1;
    D3D11_BUFFER_DESC sd = { (sizeof(MediaCB) + 15) & ~15u, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA sdd = { &sp, 0, 0 };
    dev->CreateBuffer(&sd, &sdd, &cbSpace);
    // dual-source blending: colour = in-scattered light + scene * transmittance (per channel)
    D3D11_BLEND_DESC bd = {};
    auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_SRC1_COLOR; rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ZERO; rt.DestBlendAlpha = D3D11_BLEND_ONE; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    dev->CreateBlendState(&bd, &bsDual);
    return vs && ps && tables.trans && tables.ms && tables.irr && cbPass && cbSpace && bsDual;
}

void Atmosphere::Release()
{
    planets.clear();
    current = nullptr;
    vs.Reset(); ps.Reset(); tables = {}; cbPass.Reset(); cbSpace.Reset(); bsDual.Reset();
    g_fxAtmo.active = false;
}

PlanetAtmosphere* Atmosphere::Get(D3D11Client* gc, Planet* p)
{
    auto it = planets.find(p);
    if (it != planets.end()) return it->second.get();
    std::unique_ptr<PlanetAtmosphere> a;
    AtmoMedia m;
    if (p->atm.has && (m.Load(p) || m.ClearGas(p))) {
        a.reset(new PlanetAtmosphere(p, m));
        if (!a->Create(gc->Dev())) { LOGC("atmosphere %s: GPU resources FAILED", p->name.c_str()); a.reset(); }
    }
    PlanetAtmosphere* r = a.get();
    planets[p] = std::move(a);
    return r;
}

void Atmosphere::BindLight(ID3D11DeviceContext* ctx)
{
    ID3D11Buffer* cb = current ? current->cb.Get() : cbSpace.Get();
    ID3D11ShaderResourceView* srv[2] = { current ? current->lutSRV.Get() : nullptr, current ? current->msSRV.Get() : nullptr };
    ctx->VSSetConstantBuffers(4, 1, &cb);
    ctx->PSSetConstantBuffers(4, 1, &cb);
    ctx->PSSetShaderResources(8, 2, srv);
}

void Atmosphere::Frame(D3D11Client* gc, const VECTOR3& cp)
{
    ID3D11DeviceContext* ctx = gc->Ctx();
    sunPos = SunGlobal();
    current = nullptr;
    double best = 1e300;
    if (g_fxPlanets) for (Planet* p : *g_fxPlanets) {
        PlanetAtmosphere* a = Get(gc, p);
        if (!a) continue;
        a->Update(ctx, cp, sunPos);
        if (!a->tablesReady) a->BuildTables(ctx, tables);
        VECTOR3 pp; oapiGetGlobalPos(p->hObj, &pp);
        double rel = (length(cp - pp) - p->R) / p->R;   // the nearest relative to its size
        if (rel < 20 && rel < best) { best = rel; current = a; }
    }
    BindLight(ctx);
    g_fxAtmo.active = true;
    g_fxAtmo.starVis = current ? current->StarVisibility(cp, sunPos) : 1.0f;
    UpdateExposure(cp);
}

// the eye's adaptation: exposure = 1 / the light on a horizontal surface at the camera (its luminance) - 1 under Earth's
// noon sky and in space - but no further than to this place's own noon (so twilight and night stay dark: under Venus's
// clouds the eye adapts to the ~1 % daylight, not to the night), smoothed over ~1.5 s of real time
template<class S> static auto SetExposure(S& s, float e, int) -> decltype(s.exposure = e, void()) { s.exposure = e; }
template<class S> static void SetExposure(S&, float, long) {}     // the client has no exposure hook yet

// the colour balance: the eye adapts partly to the colour of the light (von Kries, ~55 %: Titan's red light still looks
// warm, not grey). gain = w^-k with w the light's chromaticity (luminance 1), normalised so the light's own luminance
// stays; computed from the noon light at this place (the day's colour, not the twilight's)
static const double ADAPT_K = 0.55;

void Atmosphere::UpdateExposure(const VECTOR3& cp)
{
    double target = 1;
    VECTOR3 gainT = _V(1, 1, 1);
    auto lum = [](const VECTOR3& e) { return 0.2126 * e.x + 0.7152 * e.y + 0.0722 * e.z; };
    if (current) {
        VECTOR3 E = current->LightAt(cp, sunPos, false), En = current->LightAt(cp, sunPos, true);
        double Y = lum(E), Ynoon = lum(En);
        target = std::clamp(1.0 / std::max(Y, 1e-9), 1.0, std::max(1.0, 1.5 / std::max(Ynoon, 1e-9)));
        if (Ynoon > 1e-9) {
            VECTOR3 w = En / Ynoon, a, g;
            for (int i = 0; i < 3; i++) { double wi = std::max(w.data[i], 1e-4); g.data[i] = pow(wi, -ADAPT_K); a.data[i] = pow(wi, 1 - ADAPT_K); }
            const double ya = lum(a);
            gainT = g / std::max(ya, 1e-6);
            for (int i = 0; i < 3; i++) gainT.data[i] = std::clamp(gainT.data[i], 0.05, 20.0);
        }
    }
    LARGE_INTEGER now, f; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
    double dt = lastT.QuadPart ? (now.QuadPart - lastT.QuadPart) / (double)f.QuadPart : 1e3;
    lastT = now;
    double k = 1 - exp(-dt / 1.5);       // in log space: the eye adapts by ratios
    exposure = exp(log(exposure) + (log(target) - log(exposure)) * k);
    for (int i = 0; i < 3; i++) white.data[i] = exp(log(white.data[i]) + (log(gainT.data[i]) - log(white.data[i])) * k);
    SetExposure(g_fxAtmo, (float)exposure, 0);
    for (int i = 0; i < 3; i++) g_fxAtmo.white[i] = (float)white.data[i];
}

// a check in Orbiter.log, once, 12 s into the session: the scene depth this pass reads (5 points across the screen)
void Atmosphere::LogDepth(D3D11Client* gc, float nearPlane)
{
    static LARGE_INTEGER t0 = {}; static bool done = false;
    LARGE_INTEGER now, f; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
    if (!t0.QuadPart) t0 = now;
    if (done || (now.QuadPart - t0.QuadPart) < 12 * f.QuadPart) return;
    done = true;
    ID3D11DeviceContext* ctx = gc->Ctx();
    ComPtr<ID3D11Resource> res; gc->depthSRV->GetResource(&res);
    ComPtr<ID3D11Texture2D> tex; res.As(&tex);
    D3D11_TEXTURE2D_DESC d; tex->GetDesc(&d);
    D3D11_SHADER_RESOURCE_VIEW_DESC sv; gc->depthSRV->GetDesc(&sv);
    LOGC("atmosphere pass: depth %ux%u, %u sample(s), format %d, view format %d, dimension %d", d.Width, d.Height, d.SampleDesc.Count, (int)d.Format, (int)sv.Format, (int)sv.ViewDimension);
    if (d.SampleDesc.Count != 1) return;
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> st; if (FAILED(gc->Dev()->CreateTexture2D(&d, nullptr, &st))) return;
    ctx->CopyResource(st.Get(), tex.Get());
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) return;
    char buf[512]; int n = 0;
    const float px[5][2] = { { 0.5f, 0.5f }, { 0.5f, 0.8f }, { 0.5f, 0.95f }, { 0.2f, 0.7f }, { 0.5f, 0.2f } };
    for (auto& p : px) {
        UINT x = (UINT)(p[0] * (d.Width - 1)), y = (UINT)(p[1] * (d.Height - 1));
        float z = *(const float*)((const BYTE*)m.pData + (size_t)y * m.RowPitch + x * 4);
        n += sprintf_s(buf + n, sizeof buf - n, " (%.2f,%.2f) d=%.3g z=%.4g m;", p[0], p[1], z, z > 0 ? nearPlane / z : -1.0f);
    }
    ctx->Unmap(st.Get(), 0);
    LOGC("atmosphere pass: scene depth at%s", buf);
    if (current) {
        VECTOR3 pp; oapiGetGlobalPos(current->planet->hObj, &pp);
        VECTOR3 cp; oapiCameraGlobalPos(&cp);
        VECTOR3 up = cp - pp; double alt = length(up) - current->planet->R; up = up / length(up);
        VECTOR3 sd = sunPos - pp; sd = sd / length(sd);
        VECTOR3 E = current->LightAt(cp, sunPos, false), En = current->LightAt(cp, sunPos, true);
        LOGC("atmosphere pass: %s, camera altitude %.0f m, Sun elevation %.1f deg, light %.3g %.3g %.3g (noon %.3g %.3g %.3g), exposure %.2f, colour balance %.2f %.2f %.2f, stars %.2f",
             current->planet->name.c_str(), alt, asin(std::clamp(dotp(up, sd), -1.0, 1.0)) * DEG, E.x, E.y, E.z, En.x, En.y, En.z, exposure, white.x, white.y, white.z, g_fxAtmo.starVis);
    }
}

void Atmosphere::Render(D3D11Client* gc, const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    if (!gc->depthSRV) return;
    ID3D11DeviceContext* ctx = gc->Ctx();
    XMFLOAT4X4 P; XMStoreFloat4x4(&P, proj);
    float tanX = 1.0f / P._11, tanY = 1.0f / P._22;
    double diag = atan(sqrt((double)tanX * tanX + (double)tanY * tanY));
    VECTOR3 fwd = _V(cr.m13, cr.m23, cr.m33);

    // the planets whose air is in view, the farthest first
    std::vector<std::pair<double, PlanetAtmosphere*>> list;
    for (auto& kv : planets) {
        PlanetAtmosphere* a = kv.second.get();
        if (!a || !a->tablesReady) continue;
        VECTOR3 pp; oapiGetGlobalPos(a->planet->hObj, &pp);
        VECTOR3 c = pp - cp; double dist = length(c);
        double Rt = a->TopRadius();
        if (dist > Rt) {
            double ang = acos(std::clamp(dotp(c, fwd) / dist, -1.0, 1.0));
            if (ang > diag + asin(Rt / dist) + 0.01) continue;
        }
        list.push_back({ dist, a });
    }
    if (list.empty()) return;
    std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.first > b.first; });
    gc->ResolveDepth();     // MSAA: depthSRV is resolved only on request (the liquid does it, but only near a liquid)
    LogDepth(gc, P._43);

    ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11DepthStencilView> dsv;
    ctx->OMGetRenderTargets(1, &rtv, &dsv);
    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);     // the depth is read as a texture
    ctx->OMSetBlendState(bsDual.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(gc->dsRead.Get(), 0);
    ctx->RSSetState(gc->rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(3, 1, cbPass.GetAddressOf());
    ctx->PSSetConstantBuffers(3, 1, cbPass.GetAddressOf());
    ctx->PSSetShaderResources(0, 1, gc->depthSRV.GetAddressOf());

    for (auto& it : list) {
        PlanetAtmosphere* a = it.second;
        VECTOR3 pp; oapiGetGlobalPos(a->planet->hObj, &pp);
        double rc = length(pp - cp), alt = rc - a->planet->R;
        PassCB k;
        k.row0 = { (float)cr.m11, (float)cr.m12, (float)cr.m13, 0 };
        k.row1 = { (float)cr.m21, (float)cr.m22, (float)cr.m23, 0 };
        k.row2 = { (float)cr.m31, (float)cr.m32, (float)cr.m33, 0 };
        k.tan = { tanX, tanY, P._43, 0 };
        k.cam = { (float)rc, (float)alt, 0, 0 };
        k.sun = { 1.0f, 64.0f, P._31, P._32 };           // the Sun's disc, its radiance capped; TAA's sub-pixel offset
        D3D11_MAPPED_SUBRESOURCE m;
        ctx->Map(cbPass.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, &k, sizeof k); ctx->Unmap(cbPass.Get(), 0);
        ID3D11ShaderResourceView* srv[2] = { a->lutSRV.Get(), a->msSRV.Get() };
        ctx->PSSetShaderResources(8, 2, srv);
        ctx->PSSetConstantBuffers(4, 1, a->cb.GetAddressOf());
        ctx->Draw(3, 0);
    }

    // back to the current planet's light for what follows
    ID3D11ShaderResourceView* nul = nullptr;
    ctx->PSSetShaderResources(0, 1, &nul);
    BindLight(ctx);
    ctx->OMSetBlendState(gc->bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
}
