// Earth's volumetric clouds near the planet (shaders\earthclouds.hlsl): inside the air, below the 2D layer's switch
// altitude (CloudLayerFade), from the same weather - the layer's coverage (Cloud.tree) and the moving cyclone's map
// (Cyclone.cpp). Half resolution, marched through the scene's depth, composited over the scene after the air.
// Titan's clouds are a separate system (Clouds.cpp) and stay as they are.
#include "Client.h"
#include "Fx.h"
#include "CloudLayer.h"
#include <cmath>

namespace {
    ID3D11Device* eDev = nullptr;
    bool eTried = false;
    ComPtr<ID3D11VertexShader> eVS; ComPtr<ID3D11PixelShader> eMarch, eComp;
    ComPtr<ID3D11Buffer> eCB;
    ComPtr<ID3D11Texture3D> eNoise; ComPtr<ID3D11ShaderResourceView> eNoiseSRV;
    ComPtr<ID3D11Texture2D> eTex, eTexT; ComPtr<ID3D11RenderTargetView> eRTV, eRTVT; ComPtr<ID3D11ShaderResourceView> eSRV, eSRVT;
    UINT eW = 0, eH = 0, eFrame = 0;
    ComPtr<ID3D11BlendState> eBlend;
    int eSteps = 96, eDbg = 0;

    // 3D noise, tiling: Perlin-Worley (r), Worley at three frequencies (g, b, a)
    float Hash(int x, int y, int z, int s) { unsigned h = (unsigned)(x * 73856093 ^ y * 19349663 ^ z * 83492791 ^ s * 2654435761u); h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return (h & 0xffffff) / 16777216.0f; }
    float Worley(float x, float y, float z, int cells, int seed)
    {
        float fx = x * cells, fy = y * cells, fz = z * cells;
        int ix = (int)floor(fx), iy = (int)floor(fy), iz = (int)floor(fz);
        float best = 1e9f;
        for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int cx = ix + dx, cy = iy + dy, cz = iz + dz;
            int wx = ((cx % cells) + cells) % cells, wy = ((cy % cells) + cells) % cells, wz = ((cz % cells) + cells) % cells;
            float px = cx + Hash(wx, wy, wz, seed), py = cy + Hash(wx, wy, wz, seed + 1), pz = cz + Hash(wx, wy, wz, seed + 2);
            best = std::min(best, (px - fx) * (px - fx) + (py - fy) * (py - fy) + (pz - fz) * (pz - fz));
        }
        return 1.0f - std::min(1.0f, sqrtf(best));
    }
    float Grad(int ix, int iy, int iz, int per, float x, float y, float z)
    {
        ix = ((ix % per) + per) % per; iy = ((iy % per) + per) % per; iz = ((iz % per) + per) % per;
        float a = Hash(ix, iy, iz, 7) * 6.2831853f, b = Hash(ix, iy, iz, 8) * 2 - 1, c = sqrtf(1 - b * b);
        return x * c * cosf(a) + y * c * sinf(a) + z * b;
    }
    float Perlin(float x, float y, float z, int per)
    {
        x *= per; y *= per; z *= per;
        int ix = (int)floor(x), iy = (int)floor(y), iz = (int)floor(z);
        float fx = x - ix, fy = y - iy, fz = z - iz;
        auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
        float u = fade(fx), v = fade(fy), w = fade(fz), r = 0;
        for (int k = 0; k < 8; k++) {
            int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
            r += Grad(ix + dx, iy + dy, iz + dz, per, fx - dx, fy - dy, fz - dz) * (dx ? u : 1 - u) * (dy ? v : 1 - v) * (dz ? w : 1 - w);
        }
        return r;
    }
    bool MakeNoise(ID3D11Device* dev)
    {
        const int S = 64;
        std::vector<uint8_t> px(S * S * S * 4);
        for (int z = 0; z < S; z++) for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
            float fx = (x + 0.5f) / S, fy = (y + 0.5f) / S, fz = (z + 0.5f) / S;
            float pf = 0, amp = 1, norm = 0;
            for (int o = 0; o < 4; o++) { pf += amp * Perlin(fx, fy, fz, 4 << o); norm += amp; amp *= 0.5f; }
            pf = pf / norm * 0.5f + 0.5f;
            float wf = Worley(fx, fy, fz, 4, 11) * 0.625f + Worley(fx, fy, fz, 8, 23) * 0.25f + Worley(fx, fy, fz, 16, 37) * 0.125f;
            float pw = std::clamp(pf + (wf - 0.5f) * 0.9f, 0.0f, 1.0f);
            uint8_t* q = &px[((z * S + y) * S + x) * 4];
            q[0] = (uint8_t)(pw * 255); q[1] = (uint8_t)(Worley(fx, fy, fz, 8, 23) * 255); q[2] = (uint8_t)(Worley(fx, fy, fz, 16, 37) * 255);
            q[3] = (uint8_t)(Worley(fx, fy, fz, 32, 41) * 255);
        }
        D3D11_TEXTURE3D_DESC td = { (UINT)S, (UINT)S, (UINT)S, 0, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                                    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, D3D11_RESOURCE_MISC_GENERATE_MIPS };
        if (FAILED(dev->CreateTexture3D(&td, nullptr, &eNoise))) return false;
        if (FAILED(dev->CreateShaderResourceView(eNoise.Get(), nullptr, &eNoiseSRV))) return false;
        ComPtr<ID3D11DeviceContext> ic; dev->GetImmediateContext(&ic);
        ic->UpdateSubresource(eNoise.Get(), 0, nullptr, px.data(), S * 4, S * S * 4);
        ic->GenerateMips(eNoiseSRV.Get());
        return true;
    }
}

void D3D11Client::RenderEarthClouds(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    OBJHANDLE hP = oapiCameraProxyGbody();
    if (!hP) return;
    double alt = 0;
    ID3D11ShaderResourceView* cov = CloudLayerCoverage(hP, &alt);
    if (!cov) return;                                   // (no layer on this body: no Earth clouds - Titan has its own)
    VECTOR3 pp; oapiGetGlobalPos(hP, &pp);
    MATRIX3 Rp; oapiGetRotationMatrix(hP, &Rp);
    const double R = oapiGetSize(hP);
    const VECTOR3 camPl = tmul(Rp, cp - pp);
    const double share = 1.0 - CloudLayerFade(length(camPl) - R);
    if (share <= 0.001) return;
    if (eDev != dev.Get()) {
        eDev = dev.Get(); eTried = false; eVS.Reset(); eMarch.Reset(); eComp.Reset(); eCB.Reset(); eNoise.Reset(); eNoiseSRV.Reset();
        eTex.Reset(); eTexT.Reset(); eRTV.Reset(); eRTVT.Reset(); eSRV.Reset(); eSRVT.Reset(); eW = eH = 0; eBlend.Reset();
    }
    if (!eVS && !eTried) {
        eTried = true;
        if (FILE* g = fopen("Config\\D3D11Client\\Client.cfg", "r")) {
            char ln[256];
            while (fgets(ln, sizeof ln, g)) { int n; if (sscanf(ln, " EarthCloudSteps = %d", &n) == 1) eSteps = std::clamp(n, 16, 256); sscanf(ln, " CloudDbg = %d", &eDbg); }
            fclose(g);
        }
        auto b0 = CompileShaderFile("earthclouds.hlsl", "VS", "vs_5_0"), b1 = CompileShaderFile("earthclouds.hlsl", "PSMarch", "ps_5_0"),
             b2 = CompileShaderFile("earthclouds.hlsl", "PSComposite", "ps_5_0");
        if (b0 && b1 && b2) {
            dev->CreateVertexShader(b0->GetBufferPointer(), b0->GetBufferSize(), nullptr, &eVS);
            dev->CreatePixelShader(b1->GetBufferPointer(), b1->GetBufferSize(), nullptr, &eMarch);
            dev->CreatePixelShader(b2->GetBufferPointer(), b2->GetBufferSize(), nullptr, &eComp);
            D3D11_BUFFER_DESC bd = { sizeof(XMFLOAT4) * (4 + 3 + 4), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
            dev->CreateBuffer(&bd, nullptr, &eCB);
            D3D11_BLEND_DESC bl = {}; auto& t = bl.RenderTarget[0];
            t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_SRC_ALPHA; t.BlendOp = D3D11_BLEND_OP_ADD;
            t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
            dev->CreateBlendState(&bl, &eBlend);
            if (!MakeNoise(dev.Get())) eNoiseSRV.Reset();
        }
        LOGC("earth clouds: shaders %s, noise %s, %d steps", eVS && eMarch && eComp ? "ready" : "FAILED", eNoiseSRV ? "ready" : "FAILED", eSteps);
    }
    if (!eVS || !eMarch || !eComp || !eNoiseSRV || !eCB) return;
    const UINT w = std::max(1u, RW / 2), h = std::max(1u, RH / 2);
    if (!eTex || w != eW || h != eH) {
        eTex.Reset(); eTexT.Reset(); eRTV.Reset(); eRTVT.Reset(); eSRV.Reset(); eSRVT.Reset();
        D3D11_TEXTURE2D_DESC td = { w, h, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &eTex))) return;
        td.Format = DXGI_FORMAT_R32_FLOAT;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &eTexT))) return;
        dev->CreateRenderTargetView(eTex.Get(), nullptr, &eRTV); dev->CreateShaderResourceView(eTex.Get(), nullptr, &eSRV);
        dev->CreateRenderTargetView(eTexT.Get(), nullptr, &eRTVT); dev->CreateShaderResourceView(eTexT.Get(), nullptr, &eSRVT);
        eW = w; eH = h;
    }
    const double* rot = (const double*)oapiGetObjectParam(hP, OBJPRM_PLANET_CLOUDROTATION);
    const double cr0 = rot ? *rot : 0.0;
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    struct { XMFLOAT4X4 inv; XMFLOAT4 r0, r1, r2, L, Cy, P, size; } k = {};
    XMStoreFloat4x4(&k.inv, XMMatrixTranspose(XMMatrixInverse(nullptr, V * proj)));
    k.r0 = { (float)Rp.m11, (float)Rp.m21, (float)Rp.m31, (float)(camPl.x / 1000.0) };
    k.r1 = { (float)Rp.m12, (float)Rp.m22, (float)Rp.m32, (float)(camPl.y / 1000.0) };
    k.r2 = { (float)Rp.m13, (float)Rp.m23, (float)Rp.m33, (float)(camPl.z / 1000.0) };
    k.L = { (float)R, (float)cos(cr0), (float)sin(cr0), 0 };
    CycloneLayerConstants(hP, &k.Cy.x);
    ID3D11ShaderResourceView* cyc = CycloneMap();
    if (!cyc) k.Cy.z = 0;
    k.P = { (float)fmod(oapiGetSimTime(), 1e5), (float)eSteps, (float)(eFrame++ % 64), (float)share };
    k.size = { (float)w, (float)h, (float)eDbg, 0 };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(eCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &k, sizeof k); ctx->Unmap(eCB.Get(), 0);

    ResolveDepth();
    ComPtr<ID3D11RenderTargetView> oR; ComPtr<ID3D11DepthStencilView> oD; ctx->OMGetRenderTargets(1, &oR, &oD);
    UINT nvp = 1; D3D11_VIEWPORT ovp; ctx->RSGetViewports(&nvp, &ovp);
    ID3D11RenderTargetView* rt[2] = { eRTV.Get(), eRTVT.Get() };
    ctx->OMSetRenderTargets(2, rt, nullptr);
    D3D11_VIEWPORT hv = { 0, 0, (float)w, (float)h, 0, 1 }; ctx->RSSetViewports(1, &hv);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    FxBindLight(this);
    ctx->VSSetShader(eVS.Get(), nullptr, 0); ctx->PSSetShader(eMarch.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, eCB.GetAddressOf());
    ID3D11ShaderResourceView* sr[4] = { eNoiseSRV.Get(), depthSRV.Get(), cov, cyc };
    ctx->PSSetShaderResources(0, 4, sr);
    ID3D11SamplerState* ss[2] = { sampAniso.Get(), sampClamp.Get() };
    ctx->PSSetSamplers(0, 2, ss);
    ctx->Draw(3, 0);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
    if (nvp) ctx->RSSetViewports(1, &ovp);
    ctx->OMSetBlendState(eBlend.Get(), nullptr, ~0u);
    ctx->PSSetShader(eComp.Get(), nullptr, 0);
    ID3D11ShaderResourceView* sc[2] = { eSRV.Get(), eSRVT.Get() };
    ctx->PSSetShaderResources(4, 2, sc);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul[6] = {};
    ctx->PSSetShaderResources(0, 6, nul);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, oR.GetAddressOf(), oD.Get());
}
