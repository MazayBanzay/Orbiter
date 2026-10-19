// D3D11Client: ambient occlusion from the scene's depth (shaders\ssao.hlsl). After the opaque terrain and vessels, before
// the liquid and the air: the ground under a vessel, a foot in the soil and the folds of the relief darken; what is drawn
// after (the liquid, the air, the effects, the camera's own cockpit) is not touched. Client.cfg: AmbientOcclusion.
#include "Client.h"

namespace {
struct AoCB { XMFLOAT4 p, s, k; };
ComPtr<ID3D11ComputeShader> g_cs;
ComPtr<ID3D11PixelShader> g_ps;
ComPtr<ID3D11BlendState> g_bsMul;
ComPtr<ID3D11Buffer> g_cb;
ComPtr<ID3D11Texture2D> g_tex;
ComPtr<ID3D11UnorderedAccessView> g_uav;
ComPtr<ID3D11ShaderResourceView> g_srv;
UINT g_w = 0, g_h = 0;
bool g_tried = false;
}

void D3D11Client::RenderAO(const XMMATRIX& proj)
{
    if (!g_cfg.ao || !RW || !RH) return;
    if (!g_tried) {
        g_tried = true;
        auto bc = CompileShaderFile("ssao.hlsl", "CSAO", "cs_5_0");
        auto bp = CompileShaderFile("ssao.hlsl", "PSApply", "ps_5_0");
        if (bc) dev->CreateComputeShader(bc->GetBufferPointer(), bc->GetBufferSize(), nullptr, &g_cs);
        if (bp) dev->CreatePixelShader(bp->GetBufferPointer(), bp->GetBufferSize(), nullptr, &g_ps);
        D3D11_BLEND_DESC bd = {};
        auto& rt = bd.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_ZERO; rt.DestBlend = D3D11_BLEND_SRC_COLOR; rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ZERO; rt.DestBlendAlpha = D3D11_BLEND_ONE; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        dev->CreateBlendState(&bd, &g_bsMul);
        D3D11_BUFFER_DESC cd = { (sizeof(AoCB) + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&cd, nullptr, &g_cb);
        LOGC("ambient occlusion: %s", g_cs && g_ps && g_bsMul && g_cb ? "on (half resolution, 6 x 3 samples, 1.5 m)" : "NOT available");
    }
    if (!g_cs || !g_ps || !g_bsMul || !g_cb || !vsAA) return;
    const UINT w = (RW + 1) / 2, h = (RH + 1) / 2;
    if (w != g_w || h != g_h || !g_srv) {
        g_tex.Reset(); g_uav.Reset(); g_srv.Reset();
        D3D11_TEXTURE2D_DESC td = { w, h, 1, 1, DXGI_FORMAT_R16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &g_tex))) return;
        dev->CreateUnorderedAccessView(g_tex.Get(), nullptr, &g_uav);
        dev->CreateShaderResourceView(g_tex.Get(), nullptr, &g_srv);
        g_w = w; g_h = h;
    }
    ResolveDepth();          // MSAA: the depth single-sampled
    XMFLOAT4X4 P; XMStoreFloat4x4(&P, proj);
    AoCB c;
    c.p = { 1.0f / P._11, 1.0f / P._22, P._43, 1.5f };
    c.s = { (float)RW, (float)RH, (float)w, (float)h };
    c.k = { 1.0f, 96.0f, 0, 0 };
    D3D11_MAPPED_SUBRESOURCE m;
    ctx->Map(g_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, &c, sizeof c); ctx->Unmap(g_cb.Get(), 0);

    // the depth is read as a texture: off the output while this runs
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
    ctx->CSSetShader(g_cs.Get(), nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, g_cb.GetAddressOf());
    ctx->CSSetShaderResources(0, 1, depthSRV.GetAddressOf());
    ctx->CSSetUnorderedAccessViews(0, 1, g_uav.GetAddressOf(), nullptr);
    ctx->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
    ID3D11UnorderedAccessView* nu = nullptr; ID3D11ShaderResourceView* ns = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr);
    ctx->CSSetShaderResources(0, 1, &ns);
    ctx->CSSetShader(nullptr, nullptr, 0);

    D3D11_VIEWPORT vp = { 0, 0, (float)RW, (float)RH, 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetBlendState(g_bsMul.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsAA.Get(), nullptr, 0);
    ctx->PSSetShader(g_ps.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, g_cb.GetAddressOf());
    ID3D11ShaderResourceView* srv[2] = { depthSRV.Get(), g_srv.Get() };
    ctx->PSSetShaderResources(0, 2, srv);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* n2[2] = {};
    ctx->PSSetShaderResources(0, 2, n2);
    ctx->PSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());     // the frame's constants back in b0
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
}
