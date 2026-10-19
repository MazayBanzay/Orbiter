// Anti-aliasing (Launchpad > Video > Advanced): off, MSAA 2/4/8x, FXAA-class post, TAA.
//   The scene target (hdrTex/hdrRTV, depthTex/dsv/dsvRO) is multisampled for MSAA; outTex is always single-sampled and
//   is what the tone mapping reads (the resolve, or TAA's output). depthSRV is always a single-sampled depth (resolved
//   for MSAA). TAA jitters the projection (Halton 2,3 over 8 frames) and reprojects the history through the depth with
//   the camera's motion; vessel motion vectors (and DLSS on top) come later.
#include "Client.h"

enum { AA_OFF, AA_MSAA2, AA_MSAA4, AA_MSAA8, AA_FXAA, AA_TAA };

UINT D3D11Client::SceneSamples() const
{
    switch (g_cfg.aa) { case AA_MSAA2: return 2; case AA_MSAA4: return 4; case AA_MSAA8: return 8; default: return 1; }
}

void D3D11Client::CreateSceneTargets()
{
    float sc = g_cfg.aa == AA_TAA ? std::clamp(g_cfg.renderScale, 0.5f, 1.0f) : 1.0f;
    RW = std::max(64u, (UINT)(W * sc + 0.5f)); RH = std::max(64u, (UINT)(H * sc + 0.5f));
    UINT ns = SceneSamples();
    UINT q = 0;
    while (ns > 1 && (FAILED(dev->CheckMultisampleQualityLevels(DXGI_FORMAT_R16G16B16A16_FLOAT, ns, &q)) || !q)) ns /= 2;
    samples = ns;
    D3D11_TEXTURE2D_DESC td = { RW, RH, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { ns, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    dev->CreateTexture2D(&td, nullptr, &hdrTex);
    dev->CreateRenderTargetView(hdrTex.Get(), nullptr, &hdrRTV);
    dev->CreateShaderResourceView(hdrTex.Get(), nullptr, &hdrSRV);
    td.Format = DXGI_FORMAT_R32_TYPELESS; td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    dev->CreateTexture2D(&td, nullptr, &depthTex);
    D3D11_DEPTH_STENCIL_VIEW_DESC dv = {}; dv.Format = DXGI_FORMAT_D32_FLOAT;
    dv.ViewDimension = ns > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    dev->CreateDepthStencilView(depthTex.Get(), &dv, &dsv);
    dv.Flags = D3D11_DSV_READ_ONLY_DEPTH;
    dev->CreateDepthStencilView(depthTex.Get(), &dv, &dsvRO);
    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.Format = DXGI_FORMAT_R32_FLOAT;
    if (ns > 1) {
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        dev->CreateShaderResourceView(depthTex.Get(), &sv, &depthMSSRV);
        D3D11_TEXTURE2D_DESC rd = { RW, RH, 1, 1, DXGI_FORMAT_R32_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        dev->CreateTexture2D(&rd, nullptr, &depthRes);
        dev->CreateRenderTargetView(depthRes.Get(), nullptr, &depthResRTV);
        dev->CreateShaderResourceView(depthRes.Get(), nullptr, &depthSRV);
    } else {
        sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(depthTex.Get(), &sv, &depthSRV);
    }
    // the single-sampled outputs: the resolve / TAA (two for the history), the tone-mapped image for FXAA
    D3D11_TEXTURE2D_DESC od = { W, H, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    for (int k = 0; k < 2; k++) {
        dev->CreateTexture2D(&od, nullptr, &outTex[k]);
        dev->CreateRenderTargetView(outTex[k].Get(), nullptr, &outRTV[k]);
        dev->CreateShaderResourceView(outTex[k].Get(), nullptr, &outSRV[k]);
    }
    if (g_cfg.aa == AA_TAA) {     // motion vectors: uv now - uv then, cleared to "none" (1000)
        D3D11_TEXTURE2D_DESC md = { RW, RH, 1, 1, DXGI_FORMAT_R16G16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        dev->CreateTexture2D(&md, nullptr, &mvTex);
        dev->CreateRenderTargetView(mvTex.Get(), nullptr, &mvRTV);
        dev->CreateShaderResourceView(mvTex.Get(), nullptr, &mvSRV);
    }
    od.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
    dev->CreateTexture2D(&od, nullptr, &ldrTex);
    D3D11_RENDER_TARGET_VIEW_DESC rv = {}; rv.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    dev->CreateRenderTargetView(ldrTex.Get(), &rv, &ldrRTV);
    D3D11_SHADER_RESOURCE_VIEW_DESC lv = {}; lv.Format = DXGI_FORMAT_B8G8R8A8_UNORM; lv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; lv.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(ldrTex.Get(), &lv, &ldrSRV);
    rv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    dev->CreateRenderTargetView(bbTex.Get(), &rv, &bbRTVLin);
    LOGC("anti-aliasing: %s, %u sample(s), scene %ux%u of %ux%u", g_cfg.aa == AA_FXAA ? "FXAA" : g_cfg.aa == AA_TAA ? "TAA" : ns > 1 ? "MSAA" : "off", ns, RW, RH, W, H);
}

// TAA: this frame's sub-pixel offset of the projection (NDC units)
XMFLOAT2 D3D11Client::Jitter() const
{
    if (g_cfg.aa != AA_TAA) return { 0, 0 };
    auto halton = [](int i, int b) { double f = 1, r = 0; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; };
    int k = (int)(frameNo % (RW < W ? 16 : 8)) + 1;
    return { (float)((halton(k, 2) - 0.5) * 2.0 / RW), (float)((halton(k, 3) - 0.5) * 2.0 / RH) };
}

static void FullScreen(ID3D11DeviceContext* ctx)
{
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->Draw(3, 0);
}

// the scene colour so far into a single-sampled texture; the depth resolved into depthSRV (liquids, TAA)
void D3D11Client::CopySceneColour(ID3D11Texture2D* dst)
{
    if (samples > 1) ctx->ResolveSubresource(dst, 0, hdrTex.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
    else ctx->CopyResource(dst, hdrTex.Get());
    ResolveDepth();
}

void D3D11Client::ResolveDepth()
{
    if (samples <= 1 || !depthResRTV) return;
    ComPtr<ID3D11RenderTargetView> oR; ComPtr<ID3D11DepthStencilView> oD; ctx->OMGetRenderTargets(1, &oR, &oD);
    ctx->OMSetRenderTargets(1, depthResRTV.GetAddressOf(), nullptr);
    D3D11_VIEWPORT svp = { 0, 0, (float)RW, (float)RH, 0, 1 };
    ctx->RSSetViewports(1, &svp);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->VSSetShader(vsAA.Get(), nullptr, 0);
    ctx->PSSetShader(psResolveDepth.Get(), nullptr, 0);
    ctx->PSSetShaderResources(0, 1, depthMSSRV.GetAddressOf());
    FullScreen(ctx.Get());
    ID3D11ShaderResourceView* nul = nullptr; ctx->PSSetShaderResources(0, 1, &nul);
    ctx->OMSetRenderTargets(1, oR.GetAddressOf(), oD.Get());
}

struct AACB { XMFLOAT4X4 invVP, prevVP; XMFLOAT4 camDelta, p, scene, pm[3]; };

// after the scene: the single-sampled image the tone mapping reads (returns its view)
ID3D11ShaderResourceView* D3D11Client::FinishScene(const VECTOR3& cp, const XMMATRIX& vpJit, const XMMATRIX& vpNoJit)
{
    ID3D11ShaderResourceView* nul[3] = {};
    if (g_cfg.aa != AA_TAA) {
        if (samples > 1) {
            // the samples averaged as they look after the tone curve (resolve.hlsl): smooth bright edges
            static ComPtr<ID3D11PixelShader> psRes; static ID3D11Device* resDev = nullptr;
            if (resDev != dev.Get()) {
                resDev = dev.Get(); psRes.Reset();
                if (auto b = CompileShaderFile("resolve.hlsl", "PS", "ps_5_0")) dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psRes);
                LOGC("MSAA resolve: %s", psRes ? "weighted by the brightness on screen" : "plain average (resolve.hlsl failed)");
            }
            if (!psRes) { ctx->ResolveSubresource(outTex[0].Get(), 0, hdrTex.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT); return outSRV[0].Get(); }
            ctx->OMSetRenderTargets(1, outRTV[0].GetAddressOf(), nullptr);
            D3D11_VIEWPORT ovp = { 0, 0, (float)RW, (float)RH, 0, 1 };
            ctx->RSSetViewports(1, &ovp);
            ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
            ctx->OMSetDepthStencilState(dsRead.Get(), 0);
            ctx->RSSetState(rsNone.Get());
            ctx->VSSetShader(vsAA.Get(), nullptr, 0);
            ctx->PSSetShader(psRes.Get(), nullptr, 0);
            ctx->PSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
            ctx->PSSetShaderResources(0, 1, hdrSRV.GetAddressOf());
            FullScreen(ctx.Get());
            ctx->PSSetShaderResources(0, 1, nul);
            return outSRV[0].Get();
        }
        return hdrSRV.Get();
    }
    // TAA: out[cur] = blend(this frame, history out[prev] reprojected)
    int cur = (int)(frameNo & 1), prev = 1 - cur;
    AACB c;
    XMStoreFloat4x4(&c.invVP, XMMatrixTranspose(XMMatrixInverse(nullptr, vpJit)));
    XMStoreFloat4x4(&c.prevVP, XMMatrixTranspose(XMLoadFloat4x4(&taaPrevVP)));
    // the camera's motion relative to the nearest body (the ground and the planet move with it; vessels carry their own vectors)
    OBJHANDLE ref = nullptr; double best = 1e300; VECTOR3 refPos = _V(0, 0, 0);
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) {
        OBJHANDLE h = oapiGetGbodyByIndex(i); VECTOR3 q; oapiGetGlobalPos(h, &q);
        double dd = length(cp - q) - oapiGetSize(h);
        if (dd < best) { best = dd; ref = h; refPos = q; }
    }
    VECTOR3 rel = cp - refPos;
    VECTOR3 d = (ref == taaRefBody) ? rel - taaPrevRel : _V(0, 0, 0);
    bool valid = taaHasHistory && ref == taaRefBody && length(d) < 1e5;
    taaRefBody = ref; taaPrevRel = rel;
    c.camDelta = { (float)d.x, (float)d.y, (float)d.z, 0 };
    // a point fixed to the body: camera-centred now -> camera-centred a frame ago, the body's rotation included
    //   p_prev = M p + t,  M = Rb_prev Rb_now^T,  t = (B_prev - C_prev) + M (C_now - B_now)
    static MATRIX3 prevRb; static VECTOR3 prevRel; static OBJHANDLE prevRef = nullptr;
    MATRIX3 Rb; if (ref) oapiGetRotationMatrix(ref, &Rb); else Rb = _M(1, 0, 0, 0, 1, 0, 0, 0, 1);
    MATRIX3 M = _M(1, 0, 0, 0, 1, 0, 0, 0, 1);
    VECTOR3 t = _V(0, 0, 0);
    if (prevRef == ref && ref) {
        for (int r = 0; r < 3; r++) for (int k = 0; k < 3; k++) {
            double s = 0;
            for (int j = 0; j < 3; j++) s += prevRb.data[r * 3 + j] * Rb.data[k * 3 + j];     // prevRb * Rb^T
            M.data[r * 3 + k] = s;
        }
        t = mul(M, rel) - prevRel;
    }
    prevRb = Rb; prevRel = rel; prevRef = ref;
    for (int r = 0; r < 3; r++) c.pm[r] = { (float)M.data[r * 3], (float)M.data[r * 3 + 1], (float)M.data[r * 3 + 2], (float)(r == 0 ? t.x : r == 1 ? t.y : t.z) };
    c.p = { 0.9f, valid ? 1.0f : 0.0f, 1.0f / W, 1.0f / H };
    XMFLOAT2 j = Jitter();
    c.scene = { (float)RW, (float)RH, j.x * RW * 0.5f, -j.y * RH * 0.5f };   // the jitter in scene pixels (y down)
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(cbObj.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &c, sizeof c); ctx->Unmap(cbObj.Get(), 0);
    ctx->OMSetRenderTargets(1, outRTV[cur].GetAddressOf(), nullptr);
    D3D11_VIEWPORT ovp = { 0, 0, (float)W, (float)H, 0, 1 };
    ctx->RSSetViewports(1, &ovp);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->VSSetShader(vsAA.Get(), nullptr, 0);
    ctx->PSSetShader(psTAA.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ID3D11ShaderResourceView* srv[4] = { hdrSRV.Get(), depthSRV.Get(), outSRV[prev].Get(), mvSRV.Get() };
    ctx->PSSetShaderResources(0, 4, srv);
    ctx->PSSetSamplers(1, 1, sampClamp.GetAddressOf());
    FullScreen(ctx.Get());
    ID3D11ShaderResourceView* nul4[4] = {};
    ctx->PSSetShaderResources(0, 4, nul4);
    XMStoreFloat4x4(&taaPrevVP, vpNoJit); taaPrevCam = cp; taaHasHistory = true;
    return outSRV[cur].Get();
}

// the tone mapping (post.hlsl) of the image into the back buffer, through FXAA when chosen
void D3D11Client::ToneMapAndAA(ID3D11ShaderResourceView* src)
{
    bool fxaa = g_cfg.aa == AA_FXAA;
    ID3D11ShaderResourceView* nul = nullptr;
    ID3D11ShaderResourceView* bloom = RenderBloom(src);     // the glow, added in the post pass (t1)
    D3D11_VIEWPORT ovp = { 0, 0, (float)W, (float)H, 0, 1 };
    ctx->RSSetViewports(1, &ovp);
    ctx->OMSetRenderTargets(1, fxaa ? ldrRTV.GetAddressOf() : bbRTV.GetAddressOf(), nullptr);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->VSSetShader(vsPost.Get(), nullptr, 0);
    ctx->PSSetShader(psPost.Get(), nullptr, 0);
    {   // the post pass's own constants (b2): the sharpening
        static ComPtr<ID3D11Buffer> cbPost; static ID3D11Device* pd = nullptr;
        if (pd != dev.Get()) { pd = dev.Get(); cbPost.Reset(); D3D11_BUFFER_DESC bd = { 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 }; dev->CreateBuffer(&bd, nullptr, &cbPost); }
        D3D11_MAPPED_SUBRESOURCE pm;
        if (cbPost && SUCCEEDED(ctx->Map(cbPost.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &pm))) {
            // TAA averages the frames: its output takes a stronger sharpening (as FSR follows its reconstruction with RCAS)
            float v[4] = { g_cfg.aa == AA_TAA ? std::min(1.0f, g_cfg.sharpen + 0.5f) : g_cfg.sharpen, 0, 0, 0 };
            memcpy(pm.pData, v, 16); ctx->Unmap(cbPost.Get(), 0);
            ctx->PSSetConstantBuffers(2, 1, cbPost.GetAddressOf());
        }
        ctx->PSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
    }
    ctx->PSSetShaderResources(0, 1, &src);
    ctx->PSSetShaderResources(1, 1, &bloom);
    ctx->PSSetSamplers(1, 1, sampClamp.GetAddressOf());
    FullScreen(ctx.Get());
    ctx->PSSetShaderResources(0, 1, &nul);
    ctx->PSSetShaderResources(1, 1, &nul);
    if (!fxaa) return;
    AACB c = {}; c.p = { 0, 0, 1.0f / W, 1.0f / H };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(cbObj.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &c, sizeof c); ctx->Unmap(cbObj.Get(), 0);
    ctx->OMSetRenderTargets(1, bbRTVLin.GetAddressOf(), nullptr);     // the image is already sRGB-encoded
    ctx->VSSetShader(vsAA.Get(), nullptr, 0);
    ctx->PSSetShader(psFXAA.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ctx->PSSetShaderResources(0, 1, ldrSRV.GetAddressOf());
    ctx->PSSetSamplers(1, 1, sampClamp.GetAddressOf());
    FullScreen(ctx.Get());
    ctx->PSSetShaderResources(0, 1, &nul);
    ctx->OMSetRenderTargets(1, bbRTV.GetAddressOf(), nullptr);
}
