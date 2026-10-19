// Liquid surfaces (Titan's seas): a sphere of radius R + level around the point under the camera, drawn after the
// opaque scene. It reads that scene's colour and DEPTH (read-only depth view: the test and the read in one pass) for the
// refraction, the apparent depth and the column of liquid under each pixel; the depth test leaves it out over land.
// Level, extinction, colour, index: Config\GC\<planet>.atm.cfg (AtmoFX_Liquid*) or Config\D3D11Client\<planet>.cfg.
#include "Client.h"
#include "Fx.h"
#include "FxInternal.h"
#include "Wake.h"

static const int LR = 256, LS = 256;          // rings, segments
const std::vector<Planet*>* g_fxPlanets = nullptr;

// multisampled scenes: the liquid reads the scene per sample (liquid.hlsl PSMS) from a multisampled copy; the mirror
// pass's depth is read too (the reflected things' distance)
namespace {
    ID3D11Device* lqDev = nullptr;
    ComPtr<ID3D11PixelShader> psLiquidMS;
    ComPtr<ID3D11Texture2D> hdrCopyMS; ComPtr<ID3D11ShaderResourceView> hdrCopyMSSRV;
    UINT msW = 0, msH = 0, msN = 0;
    ComPtr<ID3D11ShaderResourceView> reflDepthSRV;
    bool msTried = false;
    ComPtr<ID3D11Texture2D> bedTex; ComPtr<ID3D11ShaderResourceView> bedSRV;   // the terrain's depth alone (SaveLiquidBed)
    UINT bedW = 0, bedH = 0;
    bool bedSaved = false, liquidLast = false;
}

void D3D11Client::SaveLiquidBed()
{
    bedSaved = false;
    if (!liquidLast || !g_cfg.liquids) return;          // only while a liquid was drawn last frame
    if (lqDev != dev.Get()) return;
    ID3D11Texture2D* src = nullptr;
    if (samples > 1) { ResolveDepth(); src = depthRes.Get(); }
    else src = depthTex.Get();
    if (!src) return;
    D3D11_TEXTURE2D_DESC d; src->GetDesc(&d);
    if (!bedTex || bedW != d.Width || bedH != d.Height) {
        bedTex.Reset(); bedSRV.Reset();
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE; d.SampleDesc = { 1, 0 }; d.MiscFlags = 0; d.CPUAccessFlags = 0; d.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &bedTex))) return;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(bedTex.Get(), &sv, &bedSRV);
        bedW = d.Width; bedH = d.Height;
    }
    ctx->CopyResource(bedTex.Get(), src);
    bedSaved = true;
}

void D3D11Client::CreateLiquid()
{
    g_fxPlanets = &planets;                   // for the effects (the list fills in clbkPostCreation)
    // nodes: ring i (0 = the point under the camera) x segment j; u = ring fraction, v = angle fraction
    std::vector<XMFLOAT2> v;
    for (int i = 0; i <= LR; i++) for (int j = 0; j <= LS; j++) v.push_back({ (float)i / LR, (float)j / LS });
    std::vector<uint32_t> ix;
    for (int i = 0; i < LR; i++) for (int j = 0; j < LS; j++) {
        uint32_t a = i * (LS + 1) + j, b = a + 1, c = a + LS + 1, d = c + 1;
        ix.insert(ix.end(), { a, c, d, a, d, b });
    }
    liqIdx = (UINT)ix.size();
    D3D11_BUFFER_DESC bd = { (UINT)(v.size() * 8), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA sd = { v.data(), 0, 0 };
    dev->CreateBuffer(&bd, &sd, &liqVB);
    bd = { (UINT)(ix.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    sd = { ix.data(), 0, 0 };
    dev->CreateBuffer(&bd, &sd, &liqIB);
    // the scene's copy; the depth views (depthSRV single-sampled, dsvRO read-only) come from the client (Client.cpp,
    // they depend on the anti-aliasing mode)
    D3D11_TEXTURE2D_DESC td = { SceneW(), SceneH(), 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    dev->CreateTexture2D(&td, nullptr, &hdrCopy);
    dev->CreateShaderResourceView(hdrCopy.Get(), nullptr, &hdrCopySRV);
}

struct LiquidCB
{
    XMFLOAT4X4 vp;          // camera-centred global -> clip
    XMFLOAT4X4 invVP;
    XMFLOAT4 base;          // from the camera to the liquid point under it; w: radius of the liquid sphere
    XMFLOAT4 up, e1, e2;    // w of up: grid radius [m]
    XMFLOAT4 colour;        // in-scattered colour; w: extinction [1/m]
    XMFLOAT4 wts;           // channel weights; w: refractive index
    XMFLOAT4 misc;          // x: time, y: depth reconstruction n (near plane)
    XMFLOAT4 wA;            // the wave field (Wake.cpp): its corner from the camera; w: cell size [m]
    XMFLOAT4 wE1, wE2;      // its axes; w: the window's first cell (i0, j0)
    XMFLOAT4 wN;            // x: cells per side, y: 1 = on
};

// the vessels reflected in the liquid's plane (the tangent plane at the point under the camera), into reflTex at the
// scene's size: same camera, the world mirrored (p' = p - 2 ((p - base) . up) up), the parts under the plane cut. The
// liquid then looks the reflection up at its own pixel (a plane mirror's image lies on the same screen point), bent by
// its slopes. Objects only: the sky in the mirror comes from the atmosphere itself (FxSkyRadiance / SkyColour).
bool D3D11Client::RenderMirror(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj, const VECTOR3& base, const VECTOR3& up)
{
    if (visuals.empty()) return false;
    if (!reflTex || reflW != SceneW() || reflH != SceneH()) {
        reflW = SceneW(); reflH = SceneH();
        reflTex.Reset(); reflRTV.Reset(); reflSRV.Reset(); reflDepth.Reset(); reflDSV.Reset();
        D3D11_TEXTURE2D_DESC td = { reflW, reflH, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &reflTex))) return false;
        dev->CreateRenderTargetView(reflTex.Get(), nullptr, &reflRTV);
        dev->CreateShaderResourceView(reflTex.Get(), nullptr, &reflSRV);
        td.Format = DXGI_FORMAT_R32_TYPELESS; td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &reflDepth))) return false;
        D3D11_DEPTH_STENCIL_VIEW_DESC dvd = {}; dvd.Format = DXGI_FORMAT_D32_FLOAT; dvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dev->CreateDepthStencilView(reflDepth.Get(), &dvd, &reflDSV);
        D3D11_SHADER_RESOURCE_VIEW_DESC svd = {}; svd.Format = DXGI_FORMAT_R32_FLOAT; svd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; svd.Texture2D.MipLevels = 1;
        reflDepthSRV.Reset(); dev->CreateShaderResourceView(reflDepth.Get(), &svd, &reflDepthSRV);
        D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_BACK; rd.FrontCounterClockwise = TRUE; rd.DepthClipEnable = TRUE;
        dev->CreateRasterizerState(&rd, &rsMirror);
        D3D11_BUFFER_DESC bd = { 32, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&bd, nullptr, &mirrorCB);
    }
    // the mirror in camera-centred coordinates, row vectors: p' = p (I - 2 u u^T) + 2 (base . u) u
    const double bu = dotp(base, up);
    XMFLOAT4X4 M = {};
    const double u[3] = { up.x, up.y, up.z };
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) M.m[r][c] = (float)((r == c ? 1.0 : 0.0) - 2 * u[r] * u[c]);
    M.m[3][0] = (float)(2 * bu * up.x); M.m[3][1] = (float)(2 * bu * up.y); M.m[3][2] = (float)(2 * bu * up.z); M.m[3][3] = 1;
    mirrorM = XMLoadFloat4x4(&M);
    float clr[4] = { 0, 0, 0, 0 };
    ctx->ClearRenderTargetView(reflRTV.Get(), clr);
    ctx->ClearDepthStencilView(reflDSV.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    ComPtr<ID3D11RenderTargetView> oR; ComPtr<ID3D11DepthStencilView> oD; ctx->OMGetRenderTargets(1, &oR, &oD);
    ctx->OMSetRenderTargets(1, reflRTV.GetAddressOf(), reflDSV.Get());
    struct { XMFLOAT4 n, p; } mc = { { (float)up.x, (float)up.y, (float)up.z, 1 }, { (float)base.x, (float)base.y, (float)base.z, 0 } };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(mirrorCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &mc, sizeof mc); ctx->Unmap(mirrorCB.Get(), 0);
    ctx->PSSetConstantBuffers(6, 1, mirrorCB.GetAddressOf());
    mirrorPass = true;
    for (auto& kv : visuals) RenderVessel(kv.second, cp, cr, proj, false);
    for (auto& kv : visuals) RenderVessel(kv.second, cp, cr, proj, true);
    mirrorPass = false;
    mc.n.w = 0;
    ctx->Map(mirrorCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &mc, sizeof mc); ctx->Unmap(mirrorCB.Get(), 0);
    ctx->OMSetRenderTargets(1, oR.GetAddressOf(), oD.Get());
    return true;
}

// the camera inside a liquid: the column between it and the scene (terrain, vessels, the surface from below)
void D3D11Client::RenderUnderwater(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    Planet* lp = nullptr; VECTOR3 pp = _V(0, 0, 0);
    for (Planet* p : planets) {
        if (!p->liq.has || !g_cfg.liquids) continue;
        VECTOR3 q; oapiGetGlobalPos(p->hObj, &q);
        const double dc = length(cp - q), Rl = p->R + p->liq.level;
        if (dc < LiquidLevelFor(Rl, dc) && dc > Rl - 5000) { lp = p; pp = q; break; }
    }
    if (!lp) return;
    if (!uwVS) {
        auto b1 = CompileShaderFile("underwater.hlsl", "VS", "vs_5_0"), b2 = CompileShaderFile("underwater.hlsl", "PS", "ps_5_0");
        if (!b1 || !b2) return;
        dev->CreateVertexShader(b1->GetBufferPointer(), b1->GetBufferSize(), nullptr, &uwVS);
        dev->CreatePixelShader(b2->GetBufferPointer(), b2->GetBufferSize(), nullptr, &uwPS);
        D3D11_BLEND_DESC bd = {}; auto& t = bd.RenderTarget[0];
        t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_SRC1_COLOR; t.BlendOp = D3D11_BLEND_OP_ADD;
        t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        dev->CreateBlendState(&bd, &uwBlend);
        D3D11_BUFFER_DESC cbd = { 128, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&cbd, nullptr, &uwCB);
    }
    if (!uwVS || !uwPS || !uwCB) return;
    ResolveDepth();
    XMFLOAT4X4 P; XMStoreFloat4x4(&P, proj);
    const LiquidCfg& L = lp->liq;
    const VECTOR3 c = pp - cp;
    // the liquid's own light from the sky above (as the surface's in-scattered term)
    VECTOR3 up = cp - pp; up = up / length(up);
    const VECTOR3 sky = g_fxAtmo.active ? _V(g_fxAtmo.light[0], g_fxAtmo.light[1], g_fxAtmo.light[2]) : lp->atm.ambient;   // the light the liquid scatters
    struct { XMFLOAT4 r0, r1, r2, tan, C, col, wts; } k = {
        { (float)cr.m11, (float)cr.m12, (float)cr.m13, 0 }, { (float)cr.m21, (float)cr.m22, (float)cr.m23, 0 }, { (float)cr.m31, (float)cr.m32, (float)cr.m33, 0 },
        { 1.0f / P._11, 1.0f / P._22, P._43, 0 }, { (float)c.x, (float)c.y, (float)c.z, (float)(lp->R + L.level) },
        { (float)(L.colour.x * 0.06 * sky.x), (float)(L.colour.y * 0.06 * sky.y), (float)(L.colour.z * 0.06 * sky.z), (float)L.ext },
        { (float)L.wts.x, (float)L.wts.y, (float)L.wts.z, 0 } };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(uwCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &k, sizeof k); ctx->Unmap(uwCB.Get(), 0);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
    ctx->OMSetBlendState(uwBlend.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(uwVS.Get(), nullptr, 0); ctx->PSSetShader(uwPS.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, uwCB.GetAddressOf()); ctx->PSSetConstantBuffers(1, 1, uwCB.GetAddressOf());
    ctx->PSSetShaderResources(0, 1, depthSRV.GetAddressOf());
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul = nullptr; ctx->PSSetShaderResources(0, 1, &nul);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
}

void D3D11Client::RenderLiquid(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj, float tanY)
{
    // the planet with a liquid nearest under the camera
    Planet* lp = nullptr; VECTOR3 pp = _V(0, 0, 0); double best = 1e300;
    for (Planet* p : planets) {
        if (!p->liq.has) continue;
        VECTOR3 q; oapiGetGlobalPos(p->hObj, &q);
        double alt = length(cp - q) - (p->R + p->liq.level);
        if (alt > -5 && alt < 300000 && alt < best) { best = alt; lp = p; pp = q; }
    }
    {   // diagnostics, once in a while: which liquid, how high the camera is over it
        static double tLog = -1e9; const double now = oapiGetSysTime();
        if (now - tLog > 10) {
            tLog = now;
            if (lp) LOGC("liquid: %s level %.2f m, camera %.2f m over it, %s", lp->name.c_str(), lp->liq.level, best, g_cfg.liquids ? "drawn" : "off");
            else LOGC("liquid: none under the camera");
        }
    }
    liquidLast = lp && g_cfg.liquids;
    if (!lp || !g_cfg.liquids) return;
    const LiquidCfg& L = lp->liq;
    double Rl = lp->R + L.level;
    VECTOR3 up = cp - pp; double dc = length(up); up = up / dc;
    // a camera at the level (Orbiter keeps it on the surface over the liquid) or a little under it: the surface is drawn
    // just below the eye, never above it (LiquidLevelFor: the same rule for the frame's constants)
    Rl = LiquidLevelFor(Rl, dc);
    VECTOR3 base = up * -(dc - Rl);
    VECTOR3 a = fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0);
    VECTOR3 e1 = crossp(up, a); e1 = e1 / length(e1);
    VECTOR3 e2 = crossp(up, e1);
    double h = std::max(dc - Rl, 1.0);
    double horizon = sqrt(2 * Rl * h + h * h);
    double rmax = std::clamp(horizon * 1.3, 2000.0, 400000.0);

    // the vessels in the mirror, while the camera is low enough over the liquid to see them in it
    const bool mirror = dc - Rl < 3000 && RenderMirror(cp, cr, proj, base, up);
    CopySceneColour(hdrCopy.Get());          // the scene so far (resolved when multisampled)
    // multisampled: a copy per sample too, and the per-sample shader
    if (lqDev != dev.Get()) { lqDev = dev.Get(); psLiquidMS.Reset(); hdrCopyMS.Reset(); hdrCopyMSSRV.Reset(); msW = msH = msN = 0; msTried = false; }
    bool msOn = false;
    if (samples > 1 && depthMSSRV) {
        if (!psLiquidMS && !msTried) {
            msTried = true;
            if (auto b = CompileShaderFile("liquid.hlsl", "PSMS", "ps_5_0")) dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psLiquidMS);
            LOGC("liquid: per-sample shading %s", psLiquidMS ? "on" : "failed");
        }
        D3D11_TEXTURE2D_DESC hd; hdrTex->GetDesc(&hd);
        if (!hdrCopyMS || msW != hd.Width || msH != hd.Height || msN != hd.SampleDesc.Count) {
            hdrCopyMS.Reset(); hdrCopyMSSRV.Reset();
            hd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (SUCCEEDED(dev->CreateTexture2D(&hd, nullptr, &hdrCopyMS))) dev->CreateShaderResourceView(hdrCopyMS.Get(), nullptr, &hdrCopyMSSRV);
            msW = hd.Width; msH = hd.Height; msN = hd.SampleDesc.Count;
        }
        if (psLiquidMS && hdrCopyMS) { ctx->CopyResource(hdrCopyMS.Get(), hdrTex.Get()); msOn = true; }
    }
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    XMMATRIX VP = V * proj;
    LiquidCB c;
    XMStoreFloat4x4(&c.vp, XMMatrixTranspose(VP));
    XMStoreFloat4x4(&c.invVP, XMMatrixTranspose(XMMatrixInverse(nullptr, VP)));
    c.base = { (float)base.x, (float)base.y, (float)base.z, (float)Rl };
    c.up = { (float)up.x, (float)up.y, (float)up.z, (float)rmax };
    c.e1 = { (float)e1.x, (float)e1.y, (float)e1.z, 0 };
    c.e2 = { (float)e2.x, (float)e2.y, (float)e2.z, 0 };
    c.colour = { (float)L.colour.x, (float)L.colour.y, (float)L.colour.z, (float)L.ext };
    c.wts = { (float)L.wts.x, (float)L.wts.y, (float)L.wts.z, (float)L.index };
    static float dbg = -1;
    if (dbg < 0) {
        char v[8] = {}; dbg = GetEnvironmentVariableA("D3D11CLIENT_LIQDBG", v, sizeof v) ? (float)atoi(v) : 0.0f;
        if (dbg == 0) if (FILE* f = fopen("Config\\D3D11Client\\Client.cfg", "r")) {      // test runs: -Set "LiqDbg = n"
            char line[128]; int k;
            while (fgets(line, sizeof line, f)) if (sscanf(line, " LiqDbg = %d", &k) == 1) dbg = (float)k;
            fclose(f);
        }
    }
    c.misc = { (float)fmod(oapiGetSimTime(), 10000.0), 0.1f, dbg, (float)(msOn ? samples : 1) };
    WakeFieldView wv;          // the wave field around the focus vessel (Wake.cpp)
    const bool waves = WakeField(this, lp->hObj, Rl, cp, oapiGetSimStep(), wv) && wv.on;
    c.wA = { (float)wv.origin.x, (float)wv.origin.y, (float)wv.origin.z, (float)wv.dx };
    c.wE1 = { (float)wv.e1.x, (float)wv.e1.y, (float)wv.e1.z, 0 };
    c.wE2 = { (float)wv.e2.x, (float)wv.e2.y, (float)wv.e2.z, 0 };
    c.wN = { (float)wv.n, waves ? 1.0f : 0.0f, mirror && reflDepthSRV ? 1.0f : 0.0f, bedSaved ? 1.0f : 0.0f };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(cbObj.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &c, sizeof c); ctx->Unmap(cbObj.Get(), 0);

    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsvRO.Get());
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(ilLiquid.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = 8, off = 0;
    ctx->IASetVertexBuffers(0, 1, liqVB.GetAddressOf(), &stride, &off);
    ctx->IASetIndexBuffer(liqIB.Get(), DXGI_FORMAT_R32_UINT, 0);
    FxBindLight(this);                       // the sky's tables, whatever a pass before left in their slots
    ctx->VSSetShader(vsLiquid.Get(), nullptr, 0);
    ctx->PSSetShader(msOn ? psLiquidMS.Get() : psLiquid.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ctx->PSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ID3D11ShaderResourceView* wsrv = waves ? wv.srv : nullptr;     // the wave field's slopes and heights (t2)
    ID3D11ShaderResourceView* srv[3] = { hdrCopySRV.Get(), depthSRV.Get(), wsrv };
    ctx->PSSetShaderResources(0, 3, srv);
    ctx->VSSetShaderResources(2, 1, &wsrv);
    ctx->VSSetSamplers(0, 1, sampAniso.GetAddressOf());
    // the wakes: their sources (b5) and the Kelvin table (t3)
    {
        if (!wakeCB) {
            D3D11_BUFFER_DESC bd = { sizeof(XMFLOAT4) * (WAKE_MAX * 3 + 1), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
            dev->CreateBuffer(&bd, nullptr, &wakeCB);
            kelvinSRV = WakeLoadKelvin(dev.Get());
        }
        WakeSrc src[WAKE_MAX];
        int nw = WakeSources(lp->hObj, Rl, cp, src, WAKE_MAX);
        if (!kelvinSRV) for (int k = 0; k < nw; k++) if (src[k].b.w < 0.5f) src[k].a.y = 0;   // no table: no Kelvin wake
        D3D11_MAPPED_SUBRESOURCE wm;
        if (wakeCB && SUCCEEDED(ctx->Map(wakeCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &wm))) {
            memcpy(wm.pData, src, sizeof(WakeSrc) * nw);
            XMFLOAT4 cnt = { (float)nw, 0, 0, 0 };
            memcpy((BYTE*)wm.pData + sizeof(XMFLOAT4) * WAKE_MAX * 3, &cnt, sizeof cnt);
            ctx->Unmap(wakeCB.Get(), 0);
        }
        ctx->PSSetConstantBuffers(5, 1, wakeCB.GetAddressOf());
        ctx->PSSetShaderResources(3, 1, kelvinSRV.GetAddressOf());
        ID3D11ShaderResourceView* rs[3] = { mirror ? reflSRV.Get() : nullptr,             // the vessels' reflection (t4)
            msOn ? hdrCopyMSSRV.Get() : nullptr, msOn ? depthMSSRV.Get() : nullptr };     // per sample (t5, t6)
        ctx->PSSetShaderResources(4, 3, rs);
        ID3D11ShaderResourceView* rd[2] = { mirror ? reflDepthSRV.Get() : nullptr,        // the mirror's depth (t11)
            bedSaved ? bedSRV.Get() : nullptr };                                          // the terrain's depth (t12)
        ctx->PSSetShaderResources(11, 2, rd);
    }
    ctx->DrawIndexed(liqIdx, 0, 0);
    ID3D11ShaderResourceView* nul[7] = {};
    ctx->PSSetShaderResources(0, 7, nul);
    ctx->PSSetShaderResources(11, 2, nul);
    ctx->VSSetShaderResources(2, 1, nul);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
}
