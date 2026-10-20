// A planet's 2D cloud layer for the far view (shaders\cloudlayer.hlsl), as Orbiter's own: a sphere at the body's CloudAlt,
// its coverage from Textures\<planet>\Archive\Cloud.tree (the alpha of the DXT5 tiles), turned by the layer's rotation
// (OBJPRM_PLANET_CLOUDROTATION), its shadows on the ground (OBJPRM_PLANET_CLOUDSHADOWCOL). The tiles of one level are
// gathered into one equirectangular coverage texture (level 8: 16384 x 8192, ~2.4 km a texel; mips for the view from
// afar), read a few per frame within a time budget (no thread to outlive the DLL). Near the planet - inside the air,
// below the switch altitude - the volumetric clouds (EarthClouds) are drawn instead: never both at full strength.
#include "Client.h"
#include "Fx.h"
#include "CloudLayer.h"
#include <cmath>

namespace {
    const int CL_LVL = 8;                       // the level gathered (16 x 32 tiles of 512)
    struct LayerState {
        OBJHANDLE body = nullptr;
        bool tried = false, ok = false; unsigned gen = 0;
        std::unique_ptr<TreeArchive> tree;
        ComPtr<ID3D11Texture2D> tex; ComPtr<ID3D11ShaderResourceView> srv;
        int rows = 0, cols = 0, next = 0, sinceMips = 0;
        bool mipsDone = false;
        double alt = 0, shadow = 0;
    };
    LayerState ls;
    ID3D11Device* lDev = nullptr;
    bool lTried = false;
    ComPtr<ID3D11VertexShader> lVS; ComPtr<ID3D11PixelShader> lPS, lShPS;
    ComPtr<ID3D11Buffer> lCB;
    ComPtr<ID3D11BlendState> lBlend, lMul;
    ComPtr<ID3D11ShaderResourceView> lCycSRV;   // the cyclone's coverage (CycloneCoverage), when one is set

    bool DecodeAlpha(const std::vector<BYTE>& b, int& w, int& h, std::vector<uint8_t>& a)
    {
        if (b.size() < 128 || memcmp(b.data(), "DDS ", 4) || memcmp(b.data() + 84, "DXT5", 4)) return false;
        memcpy(&h, b.data() + 12, 4); memcpy(&w, b.data() + 16, 4);
        if (w <= 0 || h <= 0 || (w & 3) || (h & 3) || b.size() < 128 + (size_t)(w / 4) * (h / 4) * 16) return false;
        a.assign((size_t)w * h, 0);
        const BYTE* p = b.data() + 128;
        for (int by = 0; by < h / 4; by++) for (int bx = 0; bx < w / 4; bx++, p += 16) {
            int a0 = p[0], a1 = p[1]; uint8_t pal[8] = { (uint8_t)a0, (uint8_t)a1 };
            if (a0 > a1) for (int k = 1; k < 7; k++) pal[k + 1] = (uint8_t)(((7 - k) * a0 + k * a1) / 7);
            else { for (int k = 1; k < 5; k++) pal[k + 1] = (uint8_t)(((5 - k) * a0 + k * a1) / 5); pal[6] = 0; pal[7] = 255; }
            uint64_t bits = 0;
            for (int k = 0; k < 6; k++) bits |= (uint64_t)p[2 + k] << (8 * k);
            for (int k = 0; k < 16; k++) a[(size_t)(by * 4 + k / 4) * w + bx * 4 + (k & 3)] = pal[(bits >> (3 * k)) & 7];
        }
        return true;
    }

    double NowMs() { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1e3 / (double)f.QuadPart; }
}

void CloudLayerSetCyclone(ID3D11ShaderResourceView* srv) { lCycSRV = srv; }

// the coverage texture of the body's layer (null until it exists): the volumetric clouds read it as their weather map
ID3D11ShaderResourceView* CloudLayerCoverage(OBJHANDLE body, double* alt)
{
    if (body != ls.body || !ls.ok) return nullptr;
    if (alt) *alt = ls.alt;
    return ls.srv.Get();
}

// the layer's set-up and its tiles, a few each frame
static bool LayerPrepare(ID3D11Device* dev, ID3D11DeviceContext* ctx, OBJHANDLE body)
{
    if (body != ls.body || ls.gen != g_devGen) { ls = LayerState(); ls.body = body; ls.gen = g_devGen; }   // (a new device too)
    if (!ls.tried) {
        ls.tried = true;
        const bool* has = (const bool*)oapiGetObjectParam(body, OBJPRM_PLANET_HASCLOUDS);
        if (!has || !*has) return false;
        const double* alt = (const double*)oapiGetObjectParam(body, OBJPRM_PLANET_CLOUDALT);
        const float* sc = (const float*)oapiGetObjectParam(body, OBJPRM_PLANET_CLOUDSHADOWCOL);
        ls.alt = alt ? *alt : 7000.0;
        ls.shadow = sc ? 1.0 - *sc : 0.3;
        char nm[64] = ""; oapiGetObjectName(body, nm, 63);
        ls.tree.reset(new TreeArchive);
        std::string path = std::string("Textures\\") + nm + "\\Archive\\Cloud.tree";
        if (!ls.tree->Open(path)) { LOGC("cloud layer: %s: no %s", nm, path.c_str()); ls.tree.reset(); return false; }
        ls.rows = 1 << (CL_LVL - 4); ls.cols = 2 << (CL_LVL - 4);
        D3D11_TEXTURE2D_DESC td = { (UINT)(ls.cols * 512), (UINT)(ls.rows * 512), 0, 1, DXGI_FORMAT_R8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT,
                                    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, D3D11_RESOURCE_MISC_GENERATE_MIPS };
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &ls.tex)) || FAILED(dev->CreateShaderResourceView(ls.tex.Get(), nullptr, &ls.srv))) {
            LOGC("cloud layer: %s: texture %ux%u FAILED", nm, td.Width, td.Height); ls.tex.Reset(); ls.srv.Reset(); return false;
        }
        float zero[4] = {};
        ComPtr<ID3D11RenderTargetView> rtv;
        for (UINT m = 0; m < 15; m++) {     // clear every level: tiles not yet read show no cloud, not garbage
            D3D11_RENDER_TARGET_VIEW_DESC rd = {}; rd.Format = td.Format; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; rd.Texture2D.MipSlice = m;
            rtv.Reset();
            if (FAILED(dev->CreateRenderTargetView(ls.tex.Get(), &rd, &rtv))) break;
            ctx->ClearRenderTargetView(rtv.Get(), zero);
        }
        ls.ok = true;
        LOGC("cloud layer: %s: alt %.0f m, shadow %.2f, level %d (%dx%d tiles) from %s", nm, ls.alt, ls.shadow, CL_LVL, ls.cols, ls.rows, path.c_str());
    }
    if (!ls.ok) return false;
    // the tiles: within ~3 ms a frame (a tile: inflate + alpha decode ~1-2 ms), the mips refreshed every 64 and at the end
    if (ls.next < ls.rows * ls.cols) {
        double t0 = NowMs();
        std::vector<BYTE> b; std::vector<uint8_t> a;
        while (ls.next < ls.rows * ls.cols && NowMs() - t0 < 3.0) {
            int ilat = ls.next / ls.cols, ilng = ls.next % ls.cols;
            ls.next++; ls.sinceMips++;
            int w = 0, h = 0;
            if (!ls.tree->Read(CL_LVL, ilat, ilng, b) || !DecodeAlpha(b, w, h, a) || w != 512 || h != 512) continue;
            D3D11_BOX box = { (UINT)(ilng * 512), (UINT)(ilat * 512), 0, (UINT)(ilng * 512 + 512), (UINT)(ilat * 512 + 512), 1 };
            ctx->UpdateSubresource(ls.tex.Get(), 0, &box, a.data(), 512, 0);
        }
        if (ls.sinceMips >= 64 || ls.next >= ls.rows * ls.cols) { ctx->GenerateMips(ls.srv.Get()); ls.sinceMips = 0; }
        if (ls.next >= ls.rows * ls.cols) { ls.mipsDone = true; LOGC("cloud layer: all %d tiles in", ls.rows * ls.cols); }
    }
    return true;
}

// how much the 2D layer shows at the camera's altitude: only from far out (the planet a small disc); from orbit and
// everywhere nearer the clouds are volumetric (EarthClouds) - a short blend between, no doubled clouds
double CloudLayerFade(double camAlt) { return std::clamp((camAlt - 3.0e6) / 1.0e6, 0.0, 1.0); }   // volumetric up to ~3000 km (orbit included)

static bool LayerShaders(D3D11Client* cl, ID3D11Device* dev)
{
    if (lDev != dev) { lDev = dev; lTried = false; lVS.Reset(); lPS.Reset(); lShPS.Reset(); lCB.Reset(); lBlend.Reset(); lMul.Reset(); }
    if (!lVS && !lTried) {
        lTried = true;
        auto b0 = cl->CompileShaderFile("cloudlayer.hlsl", "VS", "vs_5_0"), b1 = cl->CompileShaderFile("cloudlayer.hlsl", "PSLayer", "ps_5_0"),
             b2 = cl->CompileShaderFile("cloudlayer.hlsl", "PSShadow", "ps_5_0");
        if (b0 && b1 && b2) {
            dev->CreateVertexShader(b0->GetBufferPointer(), b0->GetBufferSize(), nullptr, &lVS);
            dev->CreatePixelShader(b1->GetBufferPointer(), b1->GetBufferSize(), nullptr, &lPS);
            dev->CreatePixelShader(b2->GetBufferPointer(), b2->GetBufferSize(), nullptr, &lShPS);
            D3D11_BUFFER_DESC bd = { sizeof(XMFLOAT4) * (4 + 3 + 4), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
            dev->CreateBuffer(&bd, nullptr, &lCB);
            D3D11_BLEND_DESC bl = {}; auto& t = bl.RenderTarget[0];
            t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_SRC_ALPHA; t.BlendOp = D3D11_BLEND_OP_ADD;
            t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
            dev->CreateBlendState(&bl, &lBlend);
            t.SrcBlend = D3D11_BLEND_ZERO; t.DestBlend = D3D11_BLEND_SRC_COLOR;          // the shadow: dest * k
            dev->CreateBlendState(&bl, &lMul);
        }
        LOGC("cloud layer: shaders %s", lVS && lPS && lShPS ? "ready" : "FAILED");
    }
    return lVS && lPS && lShPS && lCB;
}

// the constants for both passes; false: nothing to draw
static bool LayerConstants(D3D11Client* c, ID3D11DeviceContext* ctx, OBJHANDLE hP, const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj,
                           UINT H, double* fadeOut, bool shadowPass = false)
{
    VECTOR3 pp; oapiGetGlobalPos(hP, &pp);
    MATRIX3 Rp; oapiGetRotationMatrix(hP, &Rp);
    const double R = oapiGetSize(hP);
    const VECTOR3 camPl = tmul(Rp, cp - pp);
    const double fade = CloudLayerFade(length(camPl) - R);
    if (fadeOut) *fadeOut = fade;
    const double* rot = (const double*)oapiGetObjectParam(hP, OBJPRM_PLANET_CLOUDROTATION);
    const double cr0 = rot ? *rot : 0.0;
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    struct { XMFLOAT4X4 inv; XMFLOAT4 r0, r1, r2, L, F, Cy, Px; } k = {};
    XMStoreFloat4x4(&k.inv, XMMatrixTranspose(XMMatrixInverse(nullptr, V * proj)));
    k.r0 = { (float)Rp.m11, (float)Rp.m21, (float)Rp.m31, (float)(camPl.x / 1000.0) };
    k.r1 = { (float)Rp.m12, (float)Rp.m22, (float)Rp.m32, (float)(camPl.y / 1000.0) };
    k.r2 = { (float)Rp.m13, (float)Rp.m23, (float)Rp.m33, (float)(camPl.z / 1000.0) };
    k.L = { (float)(R + ls.alt), (float)cos(cr0), (float)sin(cr0), (float)ls.shadow };
    k.F = { shadowPass ? 1.0f : (float)fade, 0.75f, (float)(ls.cols * 512), (float)(ls.rows * 512) };
    CycloneLayerConstants(hP, &k.Cy.x);
    if (!lCycSRV) k.Cy.z = 0;
    XMFLOAT4X4 pm; XMStoreFloat4x4(&pm, proj);
    k.Px = { (float)(2.0 / std::max(pm._22, 1e-3f) / std::max(H, 1u)), 0, 0, 0 };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(lCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &k, sizeof k); ctx->Unmap(lCB.Get(), 0);
    return true;
}

// the layer's shadows on the scene drawn so far (before the air); the volumetric clouds near the planet keep the same
// coverage, so their ground shadows come from here as well
void D3D11Client::RenderCloudShadow(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    OBJHANDLE hP = oapiCameraProxyGbody();
    CycloneUpdate(this, dev.Get(), ctx.Get(), hP);      // the storm's map for this frame (the layer and the volume read it)
    if (!hP || !LayerShaders(this, dev.Get()) || !LayerPrepare(dev.Get(), ctx.Get(), hP)) return;
    if (ls.shadow <= 0.01) return;
    LayerConstants(this, ctx.Get(), hP, cp, cr, proj, RH, nullptr, true);     // (full strength: the shadow stays near too)
    ResolveDepth();
    ComPtr<ID3D11RenderTargetView> oR; ComPtr<ID3D11DepthStencilView> oD; ctx->OMGetRenderTargets(1, &oR, &oD);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
    ctx->OMSetBlendState(lMul.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    FxBindLight(this);
    ctx->VSSetShader(lVS.Get(), nullptr, 0); ctx->PSSetShader(lShPS.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, lCB.GetAddressOf());
    ID3D11ShaderResourceView* sr[3] = { ls.srv.Get(), depthSRV.Get(), lCycSRV.Get() };
    ctx->PSSetShaderResources(0, 3, sr);
    ID3D11SamplerState* ss[2] = { sampAniso.Get(), sampClamp.Get() };
    ctx->PSSetSamplers(0, 2, ss);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul[3] = {};
    ctx->PSSetShaderResources(0, 3, nul);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, oR.GetAddressOf(), oD.Get());
}

// the layer itself, after the air
void D3D11Client::RenderCloudLayer(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    OBJHANDLE hP = oapiCameraProxyGbody();
    if (!hP || !LayerShaders(this, dev.Get()) || !LayerPrepare(dev.Get(), ctx.Get(), hP)) return;
    double fade = 0;
    LayerConstants(this, ctx.Get(), hP, cp, cr, proj, RH, &fade);
    if (fade <= 0.001) return;
    ResolveDepth();
    ComPtr<ID3D11RenderTargetView> oR; ComPtr<ID3D11DepthStencilView> oD; ctx->OMGetRenderTargets(1, &oR, &oD);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
    ctx->OMSetBlendState(lBlend.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    FxBindLight(this);
    ctx->VSSetShader(lVS.Get(), nullptr, 0); ctx->PSSetShader(lPS.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, lCB.GetAddressOf());
    ID3D11ShaderResourceView* sr[3] = { ls.srv.Get(), depthSRV.Get(), lCycSRV.Get() };
    ctx->PSSetShaderResources(0, 3, sr);
    ID3D11SamplerState* ss[2] = { sampAniso.Get(), sampClamp.Get() };
    ctx->PSSetSamplers(0, 2, ss);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul[3] = {};
    ctx->PSSetShaderResources(0, 3, nul);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, oR.GetAddressOf(), oD.Get());
}
