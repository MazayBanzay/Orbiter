// D3D11Client: ambient occlusion (shaders\gtao.hlsl: ground truth style, our own code). After the opaque terrain and
// vessels, before the liquid, the air, the effects and the camera's own cockpit (those are never touched): at half
// resolution, smoothed across and down keeping to each surface, then multiplied into the scene - only the sky's share
// of the light at each point (the Sun and its shadows are not dimmed). The small scale only (0.6 m: folds, gaps,
// contacts); the large scale is the sky visibility's (lights.hlsli). Client.cfg / Advanced: AmbientOcclusion.
// Test runs: D3D11CLIENT_AODBG = 1 draws the occlusion alone.
#include "Client.h"

namespace {
struct AoCB { XMFLOAT4 p, s, k, d; };
ComPtr<ID3D11ComputeShader> g_csAO, g_csBlur;
ComPtr<ID3D11PixelShader> g_ps;
ComPtr<ID3D11BlendState> g_bsMul;
ComPtr<ID3D11Buffer> g_cb;
ComPtr<ID3D11Texture2D> g_tex[2];
ComPtr<ID3D11UnorderedAccessView> g_uav[2];
ComPtr<ID3D11ShaderResourceView> g_srv[2];
UINT g_w = 0, g_h = 0;
bool g_tried = false;
}

void D3D11Client::RenderAO(const XMMATRIX& proj)
{
    if (!g_cfg.ao || !RW || !RH) return;
    if (!g_tried) {
        g_tried = true;
        auto ba = CompileShaderFile("gtao.hlsl", "CSGTAO", "cs_5_0");
        auto bb = CompileShaderFile("gtao.hlsl", "CSBlur", "cs_5_0");
        auto bp = CompileShaderFile("gtao.hlsl", "PSApply", "ps_5_0");
        if (ba) dev->CreateComputeShader(ba->GetBufferPointer(), ba->GetBufferSize(), nullptr, &g_csAO);
        if (bb) dev->CreateComputeShader(bb->GetBufferPointer(), bb->GetBufferSize(), nullptr, &g_csBlur);
        if (bp) dev->CreatePixelShader(bp->GetBufferPointer(), bp->GetBufferSize(), nullptr, &g_ps);
        D3D11_BLEND_DESC bd = {};
        auto& rt = bd.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_ZERO; rt.DestBlend = D3D11_BLEND_SRC_COLOR; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ZERO; rt.DestBlendAlpha = D3D11_BLEND_ONE; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        dev->CreateBlendState(&bd, &g_bsMul);
        D3D11_BUFFER_DESC cd = { sizeof(AoCB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&cd, nullptr, &g_cb);
        LOGC("ambient occlusion: %s", g_csAO && g_csBlur && g_ps && g_bsMul && g_cb ? "on (GTAO, half resolution, 3 slices x 2 x 4 steps, 0.6 m, sky light only)" : "NOT available");
    }
    if (!g_csAO || !g_csBlur || !g_ps || !g_bsMul || !g_cb || !vsAA) return;
    const UINT w = (RW + 1) / 2, h = (RH + 1) / 2;
    if (w != g_w || h != g_h || !g_srv[0]) {
        for (int k = 0; k < 2; k++) {
            g_tex[k].Reset(); g_uav[k].Reset(); g_srv[k].Reset();
            D3D11_TEXTURE2D_DESC td = { w, h, 1, 1, DXGI_FORMAT_R16G16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
            if (FAILED(dev->CreateTexture2D(&td, nullptr, &g_tex[k]))) return;
            dev->CreateUnorderedAccessView(g_tex[k].Get(), nullptr, &g_uav[k]);
            dev->CreateShaderResourceView(g_tex[k].Get(), nullptr, &g_srv[k]);
        }
        g_w = w; g_h = h;
    }
    static const bool dbg = getenv("D3D11CLIENT_AODBG") != nullptr;
    ResolveDepth();          // MSAA: the depth single-sampled
    XMFLOAT4X4 P; XMStoreFloat4x4(&P, proj);
    AoCB c;
    c.p = { 1.0f / P._11, 1.0f / P._22, P._43, 0.6f };
    c.s = { (float)RW, (float)RH, (float)w, (float)h };
    c.k = { 1.0f, 64.0f, dbg ? 1.0f : 0.0f, 0 };
    auto setCB = [&](float dx, float dy) {
        c.d = { dx, dy, 0, 0 };
        D3D11_MAPPED_SUBRESOURCE m;
        ctx->Map(g_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, &c, sizeof c); ctx->Unmap(g_cb.Get(), 0);
    };
    ID3D11UnorderedAccessView* nu = nullptr; ID3D11ShaderResourceView* ns[2] = {};
    // the depth is read as a texture: off the output while this runs
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
    ctx->CSSetConstantBuffers(11, 1, g_cb.GetAddressOf());
    setCB(0, 0);
    ctx->CSSetShader(g_csAO.Get(), nullptr, 0);
    ctx->CSSetShaderResources(0, 1, depthSRV.GetAddressOf());
    ctx->CSSetUnorderedAccessViews(0, 1, g_uav[0].GetAddressOf(), nullptr);
    ctx->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
    ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr);
    ctx->CSSetShader(g_csBlur.Get(), nullptr, 0);
    for (int pass = 0; pass < 2; pass++) {        // across (0 -> 1), down (1 -> 0)
        setCB(pass ? 0.0f : 1.0f, pass ? 1.0f : 0.0f);
        ctx->CSSetShaderResources(1, 1, g_srv[pass].GetAddressOf());
        ctx->CSSetUnorderedAccessViews(0, 1, g_uav[1 - pass].GetAddressOf(), nullptr);
        ctx->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
        ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr);
        ctx->CSSetShaderResources(1, 1, &ns[0]);
    }
    ctx->CSSetShaderResources(0, 2, ns);
    ctx->CSSetShader(nullptr, nullptr, 0);

    D3D11_VIEWPORT vp = { 0, 0, (float)RW, (float)RH, 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetBlendState(dbg ? bsOpaque.Get() : g_bsMul.Get(), nullptr, ~0u);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsAA.Get(), nullptr, 0);
    ctx->PSSetShader(g_ps.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(11, 1, g_cb.GetAddressOf());
    ID3D11ShaderResourceView* srv[2] = { depthSRV.Get(), g_srv[0].Get() };
    ctx->PSSetShaderResources(0, 2, srv);
    ctx->Draw(3, 0);
    ctx->PSSetShaderResources(0, 2, ns);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
}
