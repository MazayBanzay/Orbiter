// The Sketchpad on the GPU: the batch (Sketch.h) and its device resources. The surface's gamma-space render target
// is drawn into directly (the same bytes the DIB would hold), with a shared depth buffer that keeps one primitive's
// overlaps from blending twice. The pipeline is saved and restored around each draw: Sketchpads are used from module
// callbacks in the middle of the frame.
#include "Sketch.h"

namespace {

struct SkRes
{
    bool tried = false, ok = false;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps; ComPtr<ID3D11InputLayout> il;
    ComPtr<ID3D11Buffer> vb, cb; UINT vbCount = 0;
    ComPtr<ID3D11BlendState> bs[5][2];     // [blend state][alpha writable]
    ComPtr<ID3D11DepthStencilState> ds; ComPtr<ID3D11RasterizerState> rs;
    ComPtr<ID3D11SamplerState> sLin, sPt;
    ComPtr<ID3D11Texture2D> depth; ComPtr<ID3D11DepthStencilView> dsv; UINT dw = 0, dh = 0;
    // the text atlas: GDI's text, white on black, packed in shelves; sent before each draw
    static const UINT AW = 2048, AH = 1024;
    ComPtr<ID3D11Texture2D> atlas; ComPtr<ID3D11ShaderResourceView> atlasSRV;
    std::vector<DWORD> atlasCpu; UINT ax = 0, ay = 0, arow = 0, aused = 0;
};
SkRes R;

const float ZSTEP = 1.0f / (1 << 22);

bool InitRes(D3D11Client* c)
{
    if (R.tried) return R.ok;
    R.tried = true;
    ID3D11Device* dev = c->Dev();
    std::string path = c->ShaderDir() + "sketch.hlsl";
    std::wstring wp(path.begin(), path.end());
    ComPtr<ID3DBlob> vcode, pcode, err;
    if (FAILED(D3DCompileFromFile(wp.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "VS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vcode, &err))) {
        LOGC("sketch.hlsl VS: %s", err ? (const char*)err->GetBufferPointer() : "not found"); return false;
    }
    err.Reset();
    if (FAILED(D3DCompileFromFile(wp.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pcode, &err))) {
        LOGC("sketch.hlsl PS: %s", err ? (const char*)err->GetBufferPointer() : "not found"); return false;
    }
    dev->CreateVertexShader(vcode->GetBufferPointer(), vcode->GetBufferSize(), nullptr, &R.vs);
    dev->CreatePixelShader(pcode->GetBufferPointer(), pcode->GetBufferSize(), nullptr, &R.ps);
    D3D11_INPUT_ELEMENT_DESC el[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SkV, x), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SkV, u), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(SkV, p), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(SkV, q), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(SkV, col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(SkV, tint), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "KIND", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(SkV, kind), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "DEPTH", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(SkV, z), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(dev->CreateInputLayout(el, ARRAYSIZE(el), vcode->GetBufferPointer(), vcode->GetBufferSize(), &R.il))) { LOGC("sketch: input layout failed"); return false; }
    D3D11_BUFFER_DESC bd = { 64, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    dev->CreateBuffer(&bd, nullptr, &R.cb);
    for (int m = 0; m < 5; m++) for (int a = 0; a < 2; a++) {
        D3D11_BLEND_DESC b = {};
        auto& t = b.RenderTarget[0];
        UINT8 rgb = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        UINT8 al = a ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0;
        switch (m) {
        case 2: t.RenderTargetWriteMask = rgb | al; break;                 // copy: colour and alpha
        case 3: t.RenderTargetWriteMask = al ? al : 0; break;              // copy alpha
        case 4: t.RenderTargetWriteMask = rgb; break;                      // copy colour
        default:                                                           // alpha blend: the target's alpha kept
            t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_SRC_ALPHA; t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; t.BlendOp = D3D11_BLEND_OP_ADD;
            t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            t.RenderTargetWriteMask = rgb;
        }
        dev->CreateBlendState(&b, &R.bs[m][a]);
    }
    D3D11_DEPTH_STENCIL_DESC d = {}; d.DepthEnable = TRUE; d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; d.DepthFunc = D3D11_COMPARISON_GREATER;
    dev->CreateDepthStencilState(&d, &R.ds);
    D3D11_RASTERIZER_DESC r = {}; r.FillMode = D3D11_FILL_SOLID; r.CullMode = D3D11_CULL_NONE; r.DepthClipEnable = FALSE;
    dev->CreateRasterizerState(&r, &R.rs);
    D3D11_SAMPLER_DESC s = {}; s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD = D3D11_FLOAT32_MAX;
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; dev->CreateSamplerState(&s, &R.sLin);
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; dev->CreateSamplerState(&s, &R.sPt);
    D3D11_TEXTURE2D_DESC td = { SkRes::AW, SkRes::AH, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    dev->CreateTexture2D(&td, nullptr, &R.atlas);
    dev->CreateShaderResourceView(R.atlas.Get(), nullptr, &R.atlasSRV);
    R.atlasCpu.assign((size_t)SkRes::AW * SkRes::AH, 0);
    R.ok = R.vs && R.ps && R.atlas;
    LOGC("GPU Sketchpad %s", R.ok ? "ready" : "NOT available (the DIB path is used)");
    return R.ok;
}

bool EnsureDepth(ID3D11Device* dev, UINT w, UINT h)
{
    if (R.dsv && w <= R.dw && h <= R.dh) return true;
    R.dw = std::max(w, R.dw); R.dh = std::max(h, R.dh);
    R.depth.Reset(); R.dsv.Reset();
    D3D11_TEXTURE2D_DESC td = { R.dw, R.dh, 1, 1, DXGI_FORMAT_D32_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0 };
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &R.depth))) return false;
    return SUCCEEDED(dev->CreateDepthStencilView(R.depth.Get(), nullptr, &R.dsv));
}

}  // namespace

// ------------------------------------------------------------------------------------------ the pipeline, kept
PipeState::PipeState(ID3D11DeviceContext* ctx) : c(ctx)
{
    c->OMGetRenderTargets(4, rtv, &dsv);
    nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; c->RSGetViewports(&nvp, vp);
    c->VSGetShader(&vs, nullptr, nullptr); c->PSGetShader(&ps, nullptr, nullptr);
    c->IAGetInputLayout(&il); c->IAGetPrimitiveTopology(&top);
    c->IAGetVertexBuffers(0, 1, &vb, &vbStride, &vbOff); c->IAGetIndexBuffer(&ib, &ibFmt, &ibOff);
    c->OMGetBlendState(&bs, bf, &mask); c->OMGetDepthStencilState(&dss, &ref); c->RSGetState(&rs);
    c->PSGetShaderResources(0, 16, srv); c->PSGetSamplers(0, 16, smp);
    c->PSGetConstantBuffers(0, 8, pcb); c->VSGetConstantBuffers(0, 8, vcb);
}

PipeState::~PipeState()
{
    ID3D11ShaderResourceView* nul[16] = {};
    c->PSSetShaderResources(0, 16, nul);
    c->OMSetRenderTargets(4, rtv, dsv);
    if (nvp) c->RSSetViewports(nvp, vp);
    c->VSSetShader(vs, nullptr, 0); c->PSSetShader(ps, nullptr, 0);
    c->IASetInputLayout(il); c->IASetPrimitiveTopology(top);
    c->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOff); c->IASetIndexBuffer(ib, ibFmt, ibOff);
    c->OMSetBlendState(bs, bf, mask); c->OMSetDepthStencilState(dss, ref); c->RSSetState(rs);
    c->PSSetShaderResources(0, 16, srv); c->PSSetSamplers(0, 16, smp);
    c->PSSetConstantBuffers(0, 8, pcb); c->VSSetConstantBuffers(0, 8, vcb);
    auto rel = [](IUnknown* p) { if (p) p->Release(); };
    for (auto* p : rtv) rel(p);
    rel(dsv); rel(vs); rel(ps); rel(il); rel(vb); rel(ib); rel(bs); rel(dss); rel(rs);
    for (auto* p : srv) rel(p);
    for (auto* p : smp) rel(p);
    for (auto* p : pcb) rel(p);
    for (auto* p : vcb) rel(p);
}

// ------------------------------------------------------------------------------------------ the batch
bool SkBatch::Begin(Surf* s)
{
    D3D11Client* c = g_client;
    if (!c || !s || !InitRes(c)) return false;
    if (!c->SurfToGpu(s) || !s->rtvLin) return false;
    if (!EnsureDepth(c->Dev(), s->w, s->h)) return false;
    tgt = s; prim = 0; v.clear(); blend = 1; src = nullptr; gammaOn = false;
    c->Ctx()->ClearDepthStencilView(R.dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    return true;
}

void SkBatch::End()
{
    Flush();
    if (tgt) { tgt->gpuNewer = true; g_client->SurfGpuWritten(tgt); }
    tgt = nullptr;
}

void SkBatch::SetBlend(int mode)
{
    if (mode < 1 || mode > 4) mode = 1;
    if (mode != blend) { Flush(); blend = mode; }
}
void SkBatch::SetSource(Surf* s) { if (s != src) { Flush(); src = s; } }
void SkBatch::SetGamma(bool on, const float* g)
{
    if (on == gammaOn && (!on || (g[0] == gam[0] && g[1] == gam[1] && g[2] == gam[2]))) return;
    Flush(); gammaOn = on; if (on) { gam[0] = g[0]; gam[1] = g[1]; gam[2] = g[2]; }
}
void SkBatch::SetKey(DWORD k)
{
    float n[3] = { ((k >> 16) & 255) / 255.0f, ((k >> 8) & 255) / 255.0f, (k & 255) / 255.0f };
    if (n[0] != key[0] || n[1] != key[1] || n[2] != key[2]) { Flush(); key[0] = n[0]; key[1] = n[1]; key[2] = n[2]; }
}

void SkBatch::Prim()
{
    if (++prim >= (1u << 22) - 2) {    // the depth range used up: draw, start again
        Flush();
        g_client->Ctx()->ClearDepthStencilView(R.dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
        prim = 1;
    }
}
float SkBatch::Z() const { return (prim + 1) * ZSTEP; }

void SkBatch::Put(float x, float y, float u, float vv, const float* p, const float* q, DWORD col, const float* tint, UINT kind)
{
    static const float zero[4] = { 0, 0, 0, 0 }, one[4] = { 1, 1, 1, 1 };
    SkV a;
    a.x = x; a.y = y; a.u = u; a.v = vv;
    memcpy(a.p, p ? p : zero, sizeof a.p); a.q[0] = q ? q[0] : 0; a.q[1] = q ? q[1] : 0;
    a.col = col; memcpy(a.tint, tint ? tint : one, sizeof a.tint); a.kind = kind; a.z = Z();
    v.push_back(a);
}

void SkBatch::Seg(float ax, float ay, float bx, float by, float r, float dash, DWORD col)
{
    float dx = bx - ax, dy = by - ay, L = sqrtf(dx * dx + dy * dy);
    if (L < 1e-4f) { dx = 1; dy = 0; } else { dx /= L; dy /= L; }
    const float E = r + 1.5f, nx = -dy * E, ny = dx * E, ex = dx * E, ey = dy * E;
    const float p[4] = { ax, ay, bx, by }, q[2] = { r, dash };
    const float X[4] = { ax - ex + nx, bx + ex + nx, bx + ex - nx, ax - ex - nx }, Y[4] = { ay - ey + ny, by + ey + ny, by + ey - ny, ay - ey - ny };
    static const int I[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i : I) Put(X[i], Y[i], 0, 0, p, q, col, nullptr, 1);
}

void SkBatch::Ellipse(float cx, float cy, float a, float b, float hw, DWORD col)
{
    a = std::max(a, 0.5f); b = std::max(b, 0.5f);
    const float E = std::max(hw, 0.0f) + 1.5f;
    const float p[4] = { cx, cy, a, b }, q[2] = { hw, 0 };
    const float X[4] = { cx - a - E, cx + a + E, cx + a + E, cx - a - E }, Y[4] = { cy - b - E, cy - b - E, cy + b + E, cy + b + E };
    static const int I[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i : I) Put(X[i], Y[i], 0, 0, p, q, col, nullptr, 2);
}

void SkBatch::Tri(float x0, float y0, float x1, float y1, float x2, float y2, DWORD col)
{
    Put(x0, y0, 0, 0, nullptr, nullptr, col, nullptr, 0);
    Put(x1, y1, 0, 0, nullptr, nullptr, col, nullptr, 0);
    Put(x2, y2, 0, 0, nullptr, nullptr, col, nullptr, 0);
}

void SkBatch::Quad(const float* xy, const float* uv, DWORD col, const float* tint, UINT kind, bool point, bool noAlpha)
{
    const float q[2] = { point ? 1.0f : 0.0f, noAlpha ? 1.0f : 0.0f };
    static const int I[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i : I) Put(xy[i * 2], xy[i * 2 + 1], uv[i * 2], uv[i * 2 + 1], nullptr, q, col, tint, kind);
}

void SkBatch::Text(float x, float y, int w, int h, const DWORD* bits, int pitch, DWORD col)
{
    if (w <= 0 || h <= 0 || w > (int)SkRes::AW || h > (int)SkRes::AH) return;
    if (R.ax + w > SkRes::AW) { R.ax = 0; R.ay += R.arow; R.arow = 0; }
    if (R.ay + h > SkRes::AH) { Flush(); R.ax = R.ay = R.arow = 0; R.aused = 0; }
    const UINT ox = R.ax, oy = R.ay;
    for (int j = 0; j < h; j++) memcpy(&R.atlasCpu[(size_t)(oy + j) * SkRes::AW + ox], (const BYTE*)bits + (size_t)j * pitch, w * 4);
    R.ax += w + 1; R.arow = std::max(R.arow, (UINT)h + 1); R.aused = std::max(R.aused, oy + h);
    const float u0 = ox / (float)SkRes::AW, v0 = oy / (float)SkRes::AH, u1 = (ox + w) / (float)SkRes::AW, v1 = (oy + h) / (float)SkRes::AH;
    const float xy[8] = { x, y, x + w, y, x + w, y + h, x, y + h }, uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
    Quad(xy, uv, col, nullptr, 4, true, false);
}

void SkBatch::Flush()
{
    if (v.empty() || !tgt) { v.clear(); return; }
    D3D11Client* c = g_client;
    ID3D11Device* dev = c->Dev(); ID3D11DeviceContext* ctx = c->Ctx();
    if (src) c->SurfToGpu(src);       // a source drawn on its DIB goes up first
    if (R.vbCount < v.size()) {
        R.vb.Reset(); R.vbCount = std::max<UINT>((UINT)v.size(), 16384);
        D3D11_BUFFER_DESC bd = { R.vbCount * (UINT)sizeof(SkV), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &R.vb))) { R.vbCount = 0; v.clear(); return; }
    }
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx->Map(R.vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { v.clear(); return; }
    memcpy(ms.pData, v.data(), v.size() * sizeof(SkV)); ctx->Unmap(R.vb.Get(), 0);
    if (R.aused) {
        D3D11_BOX box = { 0, 0, 0, SkRes::AW, R.aused, 1 };
        ctx->UpdateSubresource(R.atlas.Get(), 0, &box, R.atlasCpu.data(), SkRes::AW * 4, 0);
    }
    struct { float size[4], gamma[4], mode[4], key[4]; } cb = {
        { 2.0f / tgt->w, 2.0f / tgt->h, 0, 0 }, { gam[0], gam[1], gam[2], gammaOn ? 1.0f : 0.0f },
        { blend >= 2 ? 1.0f : 0.0f, ZSTEP * 0.5f, src && !src->srvLin ? 1.0f : 0.0f, 0 }, { key[0], key[1], key[2], 0 } };
    ctx->UpdateSubresource(R.cb.Get(), 0, nullptr, &cb, 0, 0);
    {
        PipeState keep(ctx);
        ID3D11ShaderResourceView* nul[2] = {};
        ctx->PSSetShaderResources(0, 2, nul);
        ctx->OMSetRenderTargets(1, tgt->rtvLin.GetAddressOf(), R.dsv.Get());
        D3D11_VIEWPORT vp = { 0, 0, (float)tgt->w, (float)tgt->h, 0, 1 };
        ctx->RSSetViewports(1, &vp);
        const bool alphaOk = (tgt->attrib & OAPISURFACE_ALPHA) != 0;
        ctx->OMSetBlendState(R.bs[blend][alphaOk ? 1 : 0].Get(), nullptr, ~0u);
        ctx->OMSetDepthStencilState(R.ds.Get(), 0);
        ctx->RSSetState(R.rs.Get());
        ctx->IASetInputLayout(R.il.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        UINT stride = sizeof(SkV), off = 0;
        ctx->IASetVertexBuffers(0, 1, R.vb.GetAddressOf(), &stride, &off);
        ctx->VSSetShader(R.vs.Get(), nullptr, 0); ctx->PSSetShader(R.ps.Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, R.cb.GetAddressOf()); ctx->PSSetConstantBuffers(0, 1, R.cb.GetAddressOf());
        ID3D11ShaderResourceView* srv[2] = { src ? (src->srvLin ? src->srvLin.Get() : src->srv.Get()) : nullptr, R.atlasSRV.Get() };
        ctx->PSSetShaderResources(0, 2, srv);
        ID3D11SamplerState* sm[2] = { R.sLin.Get(), R.sPt.Get() };
        ctx->PSSetSamplers(0, 2, sm);
        ctx->Draw((UINT)v.size(), 0);
    }
    v.clear();
    R.ax = R.ay = R.arow = 0; R.aused = 0;
}
