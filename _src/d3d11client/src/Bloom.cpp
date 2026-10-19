// D3D11Client: bloom (shaders\bloom.hlsl). The HDR image halved five times, then summed back up level by level; the
// post pass adds the half-size result to the image before the exposure. Client.cfg: Bloom.
#include "Client.h"

namespace {
const int LEVELS = 5;
struct BloomCB { XMFLOAT4 tex; };
ComPtr<ID3D11PixelShader> g_down, g_up;
ComPtr<ID3D11BlendState> g_add;
ComPtr<ID3D11Buffer> g_cb;
ComPtr<ID3D11Texture2D> g_tex[LEVELS];
ComPtr<ID3D11RenderTargetView> g_rtv[LEVELS];
ComPtr<ID3D11ShaderResourceView> g_srv[LEVELS];
UINT g_w[LEVELS] = {}, g_h[LEVELS] = {}, g_srcW = 0, g_srcH = 0;
bool g_tried = false;
}

ID3D11ShaderResourceView* D3D11Client::RenderBloom(ID3D11ShaderResourceView* src)
{
    if (!g_cfg.bloom || !src) return nullptr;
    if (!g_tried) {
        g_tried = true;
        auto bd = CompileShaderFile("bloom.hlsl", "PSDown", "ps_5_0");
        auto bu = CompileShaderFile("bloom.hlsl", "PSUp", "ps_5_0");
        if (bd) dev->CreatePixelShader(bd->GetBufferPointer(), bd->GetBufferSize(), nullptr, &g_down);
        if (bu) dev->CreatePixelShader(bu->GetBufferPointer(), bu->GetBufferSize(), nullptr, &g_up);
        D3D11_BLEND_DESC b = {};
        b.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
        dev->CreateBlendState(&b, &g_add);
        D3D11_BUFFER_DESC cd = { 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&cd, nullptr, &g_cb);
        LOGC("bloom: %s", g_down && g_up && g_add && g_cb ? "on (5 levels)" : "NOT available");
    }
    if (!g_down || !g_up || !g_add || !g_cb || !vsAA) return nullptr;
    UINT sw = 0, sh = 0;
    { ComPtr<ID3D11Resource> r; src->GetResource(&r); ComPtr<ID3D11Texture2D> t;
      if (r && SUCCEEDED(r.As(&t))) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); sw = d.Width; sh = d.Height; } }
    if (!sw || !sh) return nullptr;
    if (sw != g_srcW || sh != g_srcH || !g_srv[0]) {
        g_srcW = sw; g_srcH = sh;
        UINT w = sw, h = sh;
        for (int i = 0; i < LEVELS; i++) {
            w = std::max(w / 2, 1u); h = std::max(h / 2, 1u);
            g_w[i] = w; g_h[i] = h;
            g_tex[i].Reset(); g_rtv[i].Reset(); g_srv[i].Reset();
            D3D11_TEXTURE2D_DESC td = { w, h, 1, 1, DXGI_FORMAT_R11G11B10_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                                        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0 };
            if (FAILED(dev->CreateTexture2D(&td, nullptr, &g_tex[i]))) return nullptr;
            dev->CreateRenderTargetView(g_tex[i].Get(), nullptr, &g_rtv[i]);
            dev->CreateShaderResourceView(g_tex[i].Get(), nullptr, &g_srv[i]);
        }
    }
    ID3D11ShaderResourceView* nul = nullptr;
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsAA.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, g_cb.GetAddressOf());
    ctx->PSSetSamplers(1, 1, sampClamp.GetAddressOf());
    auto pass = [&](ID3D11ShaderResourceView* in, UINT inW, UINT inH, int out, bool karis) {
        BloomCB c = { { 1.0f / inW, 1.0f / inH, karis ? 1.0f : 0.0f, 0 } };
        D3D11_MAPPED_SUBRESOURCE m;
        ctx->Map(g_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, &c, sizeof c); ctx->Unmap(g_cb.Get(), 0);
        ctx->PSSetShaderResources(0, 1, &nul);
        ctx->OMSetRenderTargets(1, g_rtv[out].GetAddressOf(), nullptr);
        D3D11_VIEWPORT vp = { 0, 0, (float)g_w[out], (float)g_h[out], 0, 1 };
        ctx->RSSetViewports(1, &vp);
        ctx->PSSetShaderResources(0, 1, &in);
        ctx->Draw(3, 0);
    };
    // down: the image halved level by level
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->PSSetShader(g_down.Get(), nullptr, 0);
    pass(src, sw, sh, 0, true);
    for (int i = 1; i < LEVELS; i++) pass(g_srv[i - 1].Get(), g_w[i - 1], g_h[i - 1], i, false);
    // up: each level's tent-filtered glow added to the level above it
    ctx->OMSetBlendState(g_add.Get(), nullptr, ~0u);
    ctx->PSSetShader(g_up.Get(), nullptr, 0);
    for (int i = LEVELS - 1; i > 0; i--) pass(g_srv[i].Get(), g_w[i], g_h[i], i - 1, false);
    ctx->PSSetShaderResources(0, 1, &nul);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->PSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());     // the frame's constants back in b0
    return g_srv[0].Get();
}
