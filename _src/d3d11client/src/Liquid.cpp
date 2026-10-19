// Liquid surfaces (Titan's seas): a sphere of radius R + level around the point under the camera, drawn after the
// opaque scene. It reads that scene's colour and DEPTH (read-only depth view: the test and the read in one pass) for the
// refraction, the apparent depth and the column of liquid under each pixel; the depth test leaves it out over land.
// Level, extinction, colour, index: Config\GC\<planet>.atm.cfg (AtmoFX_Liquid*) or Config\D3D11Client\<planet>.cfg.
#include "Client.h"
#include "FxInternal.h"
#include "Wake.h"

static const int LR = 256, LS = 256;          // rings, segments
const std::vector<Planet*>* g_fxPlanets = nullptr;

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
    XMFLOAT4 wA;            // the wave field (FxWaves): its frame's origin from the camera; w: cell size [m]
    XMFLOAT4 wE1, wE2;      // its axes; w: the window's first cell (i0, j0)
    XMFLOAT4 wN;            // x: cells per side, y: 1 = on
};

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
    if (!lp || !g_cfg.liquids) return;
    const LiquidCfg& L = lp->liq;
    double Rl = lp->R + L.level;
    VECTOR3 up = cp - pp; double dc = length(up); up = up / dc;
    VECTOR3 base = up * -(dc - Rl);
    VECTOR3 a = fabs(up.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0);
    VECTOR3 e1 = crossp(up, a); e1 = e1 / length(e1);
    VECTOR3 e2 = crossp(up, e1);
    double h = std::max(dc - Rl, 1.0);
    double horizon = sqrt(2 * Rl * h + h * h);
    double rmax = std::clamp(horizon * 1.3, 2000.0, 400000.0);

    CopySceneColour(hdrCopy.Get());          // the scene so far (resolved when multisampled)
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
    if (dbg < 0) { char v[8] = {}; dbg = GetEnvironmentVariableA("D3D11CLIENT_LIQDBG", v, sizeof v) ? (float)atoi(v) : 0.0f; }
    c.misc = { (float)fmod(oapiGetSimTime(), 10000.0), 0.1f, dbg, 0 };
    const FxWaveView& wv = FxWaves();
    bool waves = wv.on && wv.planet == lp && wv.slopes;
    c.wA = { (float)wv.anchor.x, (float)wv.anchor.y, (float)wv.anchor.z, (float)wv.dx };
    c.wE1 = { (float)wv.e1.x, (float)wv.e1.y, (float)wv.e1.z, (float)wv.i0 };
    c.wE2 = { (float)wv.e2.x, (float)wv.e2.y, (float)wv.e2.z, (float)wv.j0 };
    c.wN = { (float)wv.n, waves ? 1.0f : 0.0f, 0, 0 };
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
    ctx->VSSetShader(vsLiquid.Get(), nullptr, 0);
    ctx->PSSetShader(psLiquid.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ctx->PSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ID3D11ShaderResourceView* wsrv = waves ? wv.slopes : nullptr;     // the wave field's slopes and heights (t2)
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
    }
    ctx->DrawIndexed(liqIdx, 0, 0);
    ID3D11ShaderResourceView* nul[4] = {};
    ctx->PSSetShaderResources(0, 4, nul);
    ctx->VSSetShaderResources(2, 1, nul);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
}
