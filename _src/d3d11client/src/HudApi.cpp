// The modules' native drawing (Orbitersdk\include\D3D11Hud.h): GPU canvases with anti-aliased strokes, fills, ellipses,
// arcs, images and signed-distance text, a glow from the exact distance, and the LIGHT kind resolved to colour + alpha
// on End() (a see-through display without an image-processing shader). The table is handed out by D3D11Hud_GetApi,
// reached by the modules through the "D3D9Client.dll" shim. Shaders: shaders\hud.hlsl.
#include "Client.h"
#include "D3D11Hud.h"
#include "HudAr.h"

namespace {

struct HV
{
    float x, y, u, v;
    float p[4], q[4];
    UINT col;
    UINT kind;          // 0 fill, 1 line, 2 ellipse, 3 image, 4 glyph
    float z;
};

struct Res
{
    bool tried = false, ok = false;
    ComPtr<ID3D11VertexShader> vs, fullVs; ComPtr<ID3D11PixelShader> ps, lightPs, downPs; ComPtr<ID3D11InputLayout> il;
    ComPtr<ID3D11Buffer> vb, cb; UINT vbCount = 0;
    ComPtr<ID3D11BlendState> bs[4][2], bsNone;    // [blend][alpha writable]
    ComPtr<ID3D11DepthStencilState> ds, dsOff; ComPtr<ID3D11RasterizerState> rs, rsClip;
    ComPtr<ID3D11SamplerState> sLin;
    ComPtr<ID3D11Texture2D> depth; ComPtr<ID3D11DepthStencilView> dsv; UINT dw = 0, dh = 0;
};
Res R;
const float ZSTEP = 1.0f / (1 << 22);

bool Init()
{
    if (R.tried) return R.ok;
    R.tried = true;
    D3D11Client* c = g_client;
    if (!c) return false;
    ID3D11Device* dev = c->Dev();
    const std::string path = c->ShaderDir() + "hud.hlsl";
    const std::wstring wp(path.begin(), path.end());
    auto comp = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& code) {
        ComPtr<ID3DBlob> err;
        HRESULT hr = D3DCompileFromFile(wp.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
        if (FAILED(hr)) LOGC("hud.hlsl %s: %s", entry, err ? (const char*)err->GetBufferPointer() : "not found");
        return SUCCEEDED(hr);
    };
    ComPtr<ID3DBlob> v, p, fv, lp, dp;
    if (!comp("VS", "vs_5_0", v) || !comp("PS", "ps_5_0", p) || !comp("FullVS", "vs_5_0", fv) || !comp("LightPS", "ps_5_0", lp) || !comp("DownPS", "ps_5_0", dp)) return false;
    dev->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &R.vs);
    dev->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &R.ps);
    dev->CreateVertexShader(fv->GetBufferPointer(), fv->GetBufferSize(), nullptr, &R.fullVs);
    dev->CreatePixelShader(lp->GetBufferPointer(), lp->GetBufferSize(), nullptr, &R.lightPs);
    dev->CreatePixelShader(dp->GetBufferPointer(), dp->GetBufferSize(), nullptr, &R.downPs);
    D3D11_INPUT_ELEMENT_DESC el[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(HV, x), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(HV, u), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(HV, p), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(HV, q), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(HV, col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "KIND", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(HV, kind), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "DEPTH", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(HV, z), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(dev->CreateInputLayout(el, ARRAYSIZE(el), v->GetBufferPointer(), v->GetBufferSize(), &R.il))) return false;
    D3D11_BUFFER_DESC bd = { 64, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    dev->CreateBuffer(&bd, nullptr, &R.cb);
    for (int m = 0; m < 4; m++) for (int a = 0; a < 2; a++) {
        D3D11_BLEND_DESC b = {}; auto& t = b.RenderTarget[0];
        const UINT8 rgb = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        const UINT8 al = a ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0;
        switch (m) {
        case D3D11Hud::COPY: t.RenderTargetWriteMask = rgb | al; break;
        case D3D11Hud::COPY_ALPHA: t.RenderTargetWriteMask = al; break;
        case D3D11Hud::ADD:
            t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_SRC_ALPHA; t.DestBlend = D3D11_BLEND_ONE; t.BlendOp = D3D11_BLEND_OP_ADD;
            t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            t.RenderTargetWriteMask = rgb | al; break;
        default:    // over: colour by alpha, the alpha accumulated (the canvas is an overlay)
            t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_SRC_ALPHA; t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; t.BlendOp = D3D11_BLEND_OP_ADD;
            t.SrcBlendAlpha = D3D11_BLEND_ONE; t.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            t.RenderTargetWriteMask = rgb | al;
        }
        dev->CreateBlendState(&b, &R.bs[m][a]);
    }
    { D3D11_BLEND_DESC b = {}; b.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; dev->CreateBlendState(&b, &R.bsNone); }
    D3D11_DEPTH_STENCIL_DESC d = {}; d.DepthEnable = TRUE; d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; d.DepthFunc = D3D11_COMPARISON_GREATER;
    dev->CreateDepthStencilState(&d, &R.ds);
    d.DepthEnable = FALSE; dev->CreateDepthStencilState(&d, &R.dsOff);
    D3D11_RASTERIZER_DESC r = {}; r.FillMode = D3D11_FILL_SOLID; r.CullMode = D3D11_CULL_NONE; r.DepthClipEnable = FALSE;
    dev->CreateRasterizerState(&r, &R.rs);
    r.ScissorEnable = TRUE; dev->CreateRasterizerState(&r, &R.rsClip);
    D3D11_SAMPLER_DESC s = {}; s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD = D3D11_FLOAT32_MAX;
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; dev->CreateSamplerState(&s, &R.sLin);
    R.ok = true;
    LOGC("D3D11Hud: native canvases ready");
    return true;
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

// ------------------------------------------------------------------------------------------ fonts
struct Glyph { float x, y, w, h, ox, oy, adv; };
struct FontT
{
    SURFHANDLE tex = nullptr; float em = 48, spread = 8, aw = 1, ah = 1;
    std::unordered_map<unsigned, Glyph> g;
};

std::vector<unsigned> Utf8(const char* s)
{
    std::vector<unsigned> out;
    for (const unsigned char* p = (const unsigned char*)s; p && *p;) {
        unsigned c = *p, n = 0;
        if (c < 0x80) n = 0; else if ((c >> 5) == 6) { c &= 0x1F; n = 1; } else if ((c >> 4) == 14) { c &= 0x0F; n = 2; } else if ((c >> 3) == 30) { c &= 0x07; n = 3; } else { ++p; continue; }
        ++p;
        for (unsigned k = 0; k < n && *p; k++, p++) c = (c << 6) | (*p & 0x3F);
        out.push_back(c);
    }
    return out;
}

// ------------------------------------------------------------------------------------------ canvases
struct CanvasT
{
    int w = 0, h = 0; unsigned flags = 0;
    Surf* out = nullptr;                          // the result: the module's SURFHANDLE
    ComPtr<ID3D11Texture2D> work; ComPtr<ID3D11RenderTargetView> workRTV; ComPtr<ID3D11ShaderResourceView> workSRV;   // LIGHT / SUPERSAMPLE
    int ww = 0, wh = 0; float ss = 1;             // the drawing's size and its scale
    std::vector<HV> v;
    UINT prim = 0;
    int blend = D3D11Hud::BLEND;
    float m[6] = { 1, 0, 0, 0, 1, 0 }; bool xf = false; float xs = 1;
    bool clip = false; D3D11_RECT clipR = {};
    float glowW = 0, glowS = 0;
    Surf* src = nullptr; D3D11Hud::ImageFx fx = { { 1, 1, 1 }, 1, 0 };
    bool open = false;
    ID3D11RenderTargetView* Target() const { return workRTV ? workRTV.Get() : out->rtvLin.Get(); }
};

void Flush(CanvasT* c);

void T(const CanvasT* c, float x, float y, float& X, float& Y)
{
    if (c->xf) { X = c->m[0] * x + c->m[1] * y + c->m[2]; Y = c->m[3] * x + c->m[4] * y + c->m[5]; }
    else { X = x; Y = y; }
    X *= c->ss; Y *= c->ss;
}
float Z(const CanvasT* c) { return (c->prim + 1) * ZSTEP; }
void Prim(CanvasT* c)
{
    if (++c->prim >= (1u << 22) - 2) { Flush(c); g_client->Ctx()->ClearDepthStencilView(R.dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0); c->prim = 1; }
}
UINT Col(DWORD c) { return (c & 0xFF000000u) ? c : (c | 0xFF000000u); }   // alpha 0: opaque (Sketchpad's compatibility)
void Put(CanvasT* c, float x, float y, float u, float vv, const float* p, const float* q, DWORD col, UINT kind)
{
    static const float zero[4] = {};
    HV a; a.x = x; a.y = y; a.u = u; a.v = vv;
    memcpy(a.p, p ? p : zero, sizeof a.p); memcpy(a.q, q ? q : zero, sizeof a.q);
    a.col = Col(col); a.kind = kind; a.z = Z(c);
    c->v.push_back(a);
}
void Quad6(CanvasT* c, const float* X, const float* Y, const float* U, const float* V, const float* p, const float* q, DWORD col, UINT kind)
{
    static const int I[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i : I) Put(c, X[i], Y[i], U ? U[i] : 0, V ? V[i] : 0, p, q, col, kind);
}

// one stroke segment, already on the target (px)
void Seg(CanvasT* c, float ax, float ay, float bx, float by, float hw, float dash, DWORD col)
{
    float dx = bx - ax, dy = by - ay, L = sqrtf(dx * dx + dy * dy);
    if (L < 1e-4f) { dx = 1; dy = 0; } else { dx /= L; dy /= L; }
    const float gw = c->glowW * c->ss, E = hw + 1.5f + gw, nx = -dy * E, ny = dx * E, ex = dx * E, ey = dy * E;
    const float p[4] = { ax, ay, bx, by }, q[4] = { hw, dash, gw, c->glowS };
    const float X[4] = { ax - ex + nx, bx + ex + nx, bx + ex - nx, ax - ex - nx }, Y[4] = { ay - ey + ny, by + ey + ny, by + ey - ny, ay - ey - ny };
    Quad6(c, X, Y, nullptr, nullptr, p, q, col, 1);
}

void FillPoly(CanvasT* c, const std::vector<float>& P, DWORD col)
{
    const int n = (int)P.size() / 2;
    if (n < 3) return;
    std::vector<int> idx(n); for (int i = 0; i < n; i++) idx[i] = i;
    double area = 0; for (int i = 0, j = n - 1; i < n; j = i++) area += (double)P[j * 2] * P[i * 2 + 1] - (double)P[i * 2] * P[j * 2 + 1];
    const double sg = area >= 0 ? 1 : -1;
    auto cross = [&](int a, int b, int d) { return ((double)P[b * 2] - P[a * 2]) * ((double)P[d * 2 + 1] - P[a * 2 + 1]) - ((double)P[b * 2 + 1] - P[a * 2 + 1]) * ((double)P[d * 2] - P[a * 2]); };
    auto tri = [&](int a, int b, int d) { Put(c, P[a * 2], P[a * 2 + 1], 0, 0, nullptr, nullptr, col, 0); Put(c, P[b * 2], P[b * 2 + 1], 0, 0, nullptr, nullptr, col, 0); Put(c, P[d * 2], P[d * 2 + 1], 0, 0, nullptr, nullptr, col, 0); };
    int guard = 0;
    while (idx.size() > 3 && guard++ < 4 * n) {
        bool cut = false;
        for (size_t k = 0; k < idx.size(); k++) {
            const int a = idx[(k + idx.size() - 1) % idx.size()], b = idx[k], d = idx[(k + 1) % idx.size()];
            if (cross(a, b, d) * sg <= 0) continue;
            bool in = false;
            for (int q : idx) { if (q == a || q == b || q == d) continue; if (cross(a, b, q) * sg > 0 && cross(b, d, q) * sg > 0 && cross(d, a, q) * sg > 0) { in = true; break; } }
            if (in) continue;
            tri(a, b, d); idx.erase(idx.begin() + k); cut = true; break;
        }
        if (!cut) break;
    }
    for (size_t k = 1; k + 1 < idx.size(); k++) tri(idx[0], idx[k], idx[k + 1]);
}

void Flush(CanvasT* c)
{
    if (c->v.empty()) return;
    D3D11Client* cl = g_client; ID3D11Device* dev = cl->Dev(); ID3D11DeviceContext* ctx = cl->Ctx();
    if (c->src) cl->SurfToGpu(c->src);
    if (R.vbCount < c->v.size()) {
        R.vb.Reset(); R.vbCount = std::max<UINT>((UINT)c->v.size(), 32768);
        D3D11_BUFFER_DESC bd = { R.vbCount * (UINT)sizeof(HV), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &R.vb))) { R.vbCount = 0; c->v.clear(); return; }
    }
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx->Map(R.vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { c->v.clear(); return; }
    memcpy(ms.pData, c->v.data(), c->v.size() * sizeof(HV)); ctx->Unmap(R.vb.Get(), 0);
    static UINT frame = 0; frame++;
    struct { float size[4], fx[4], fx2[4], res[4]; } cb = {
        { 2.0f / c->ww, 2.0f / c->wh, ZSTEP * 0.5f, (c->blend == D3D11Hud::COPY || c->blend == D3D11Hud::COPY_ALPHA) ? 1.0f : 0.0f },
        { c->fx.gain[0], c->fx.gain[1], c->fx.gain[2], c->fx.gamma > 0 ? c->fx.gamma : 1.0f },
        { c->fx.noise, (float)(frame % 997) * 0.731f, c->src && !c->src->srvLin ? 1.0f : 0.0f, 0 }, { 0, 0, 0, 0 } };
    ctx->UpdateSubresource(R.cb.Get(), 0, nullptr, &cb, 0, 0);
    {
        PipeState keep(ctx);
        ID3D11ShaderResourceView* nul = nullptr; ctx->PSSetShaderResources(0, 1, &nul);
        ID3D11RenderTargetView* rt = c->Target();
        ctx->OMSetRenderTargets(1, &rt, R.dsv.Get());
        D3D11_VIEWPORT vp = { 0, 0, (float)c->ww, (float)c->wh, 0, 1 };
        ctx->RSSetViewports(1, &vp);
        const bool alphaOk = (c->flags & D3D11Hud::ALPHA) || (c->flags & D3D11Hud::LIGHT) || c->workRTV;
        ctx->OMSetBlendState(R.bs[std::clamp(c->blend, 0, 3)][alphaOk ? 1 : 0].Get(), nullptr, ~0u);
        ctx->OMSetDepthStencilState(R.ds.Get(), 0);
        if (c->clip) { D3D11_RECT r = { (LONG)(c->clipR.left * c->ss), (LONG)(c->clipR.top * c->ss), (LONG)(c->clipR.right * c->ss), (LONG)(c->clipR.bottom * c->ss) }; ctx->RSSetScissorRects(1, &r); ctx->RSSetState(R.rsClip.Get()); }
        else ctx->RSSetState(R.rs.Get());
        ctx->IASetInputLayout(R.il.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        UINT stride = sizeof(HV), off = 0;
        ctx->IASetVertexBuffers(0, 1, R.vb.GetAddressOf(), &stride, &off);
        ctx->VSSetShader(R.vs.Get(), nullptr, 0); ctx->PSSetShader(R.ps.Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, R.cb.GetAddressOf()); ctx->PSSetConstantBuffers(0, 1, R.cb.GetAddressOf());
        ID3D11ShaderResourceView* srv = c->src ? (c->src->srvLin ? c->src->srvLin.Get() : c->src->srv.Get()) : nullptr;
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->PSSetSamplers(0, 1, R.sLin.GetAddressOf());
        ctx->Draw((UINT)c->v.size(), 0);
    }
    c->v.clear();
}

// a full-target pass from the canvas's work texture into its result (the light resolve, the 2x down)
void Resolve(CanvasT* c)
{
    D3D11Client* cl = g_client; ID3D11DeviceContext* ctx = cl->Ctx();
    PipeState keep(ctx);
    ID3D11ShaderResourceView* nul = nullptr; ctx->PSSetShaderResources(0, 1, &nul);
    ctx->OMSetRenderTargets(1, c->out->rtvLin.GetAddressOf(), nullptr);
    D3D11_VIEWPORT vp = { 0, 0, (float)c->w, (float)c->h, 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetBlendState(R.bsNone.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(R.dsOff.Get(), 0);
    ctx->RSSetState(R.rs.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(R.fullVs.Get(), nullptr, 0);
    ctx->PSSetShader((c->flags & D3D11Hud::LIGHT) ? R.lightPs.Get() : R.downPs.Get(), nullptr, 0);
    ctx->PSSetShaderResources(0, 1, c->workSRV.GetAddressOf());
    ctx->PSSetSamplers(0, 1, R.sLin.GetAddressOf());
    ctx->Draw(3, 0);
}

// ------------------------------------------------------------------------------------------ the table's functions
D3D11Hud::Canvas CanvasCreate(int w, int h, unsigned flags)
{
    D3D11Client* cl = g_client;
    if (!cl || w <= 0 || h <= 0 || !Init()) return nullptr;
    CanvasT* c = new CanvasT; c->w = w; c->h = h; c->flags = flags;
    const DWORD attrib = OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS | ((flags & (D3D11Hud::ALPHA | D3D11Hud::LIGHT)) ? OAPISURFACE_ALPHA : 0);
    c->out = (Surf*)cl->clbkCreateSurfaceEx(w, h, attrib);
    if (!c->out || !cl->SurfToGpu(c->out) || !c->out->rtvLin) { if (c->out) cl->clbkReleaseSurface((SURFHANDLE)c->out); delete c; return nullptr; }
    c->ss = (flags & D3D11Hud::SUPERSAMPLE) ? 2.0f : 1.0f;
    c->ww = (int)(w * c->ss); c->wh = (int)(h * c->ss);
    if (flags & (D3D11Hud::LIGHT | D3D11Hud::SUPERSAMPLE)) {
        D3D11_TEXTURE2D_DESC td = { (UINT)c->ww, (UINT)c->wh, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(cl->Dev()->CreateTexture2D(&td, nullptr, &c->work))) { cl->clbkReleaseSurface((SURFHANDLE)c->out); delete c; return nullptr; }
        cl->Dev()->CreateRenderTargetView(c->work.Get(), nullptr, &c->workRTV);
        cl->Dev()->CreateShaderResourceView(c->work.Get(), nullptr, &c->workSRV);
    }
    return (D3D11Hud::Canvas)c;
}
void CanvasRelease(D3D11Hud::Canvas h)
{
    CanvasT* c = (CanvasT*)h;
    if (!c) return;
    if (g_client && c->out) g_client->clbkReleaseSurface((SURFHANDLE)c->out);
    delete c;
}
SURFHANDLE CanvasSurface(D3D11Hud::Canvas h) { CanvasT* c = (CanvasT*)h; return c ? (SURFHANDLE)c->out : nullptr; }

bool Begin(D3D11Hud::Canvas h, bool clear, DWORD clearCol)
{
    CanvasT* c = (CanvasT*)h;
    D3D11Client* cl = g_client;
    if (!c || !cl || !Init() || !EnsureDepth(cl->Dev(), c->ww, c->wh)) return false;
    c->v.clear(); c->prim = 0; c->blend = D3D11Hud::BLEND; c->xf = false; c->xs = 1; c->clip = false; c->glowW = c->glowS = 0;
    c->src = nullptr; c->fx = { { 1, 1, 1 }, 1, 0 }; c->open = true;
    ID3D11DeviceContext* ctx = cl->Ctx();
    ctx->ClearDepthStencilView(R.dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    if (clear || c->workRTV) {     // a work target always starts from the colour (it is resolved whole)
        const DWORD k = clear ? clearCol : 0;
        const bool alpha = (c->flags & (D3D11Hud::ALPHA | D3D11Hud::LIGHT)) != 0;
        const float f[4] = { (k & 255) / 255.0f, ((k >> 8) & 255) / 255.0f, ((k >> 16) & 255) / 255.0f, alpha ? (k >> 24) / 255.0f : 1.0f };
        ctx->ClearRenderTargetView(c->Target(), f);
    }
    return true;
}
void End(D3D11Hud::Canvas h)
{
    CanvasT* c = (CanvasT*)h;
    if (!c || !c->open) return;
    Flush(c);
    if (c->workRTV) Resolve(c);
    c->out->gpuNewer = true; g_client->SurfGpuWritten(c->out);
    c->open = false;
}

void SetBlend(D3D11Hud::Canvas h, int b) { CanvasT* c = (CanvasT*)h; if (c && b != c->blend) { Flush(c); c->blend = b; } }
void SetTransform(D3D11Hud::Canvas h, const float* m)
{
    CanvasT* c = (CanvasT*)h; if (!c) return;
    c->xf = m != nullptr;
    if (m) { memcpy(c->m, m, sizeof c->m); c->xs = sqrtf(fabsf(m[0] * m[4] - m[1] * m[3])); } else c->xs = 1;
}
void SetClip(D3D11Hud::Canvas h, const float* r)
{
    CanvasT* c = (CanvasT*)h; if (!c) return;
    Flush(c);
    c->clip = r != nullptr;
    if (r) c->clipR = { (LONG)floorf(r[0]), (LONG)floorf(r[1]), (LONG)ceilf(r[2]), (LONG)ceilf(r[3]) };
}
void SetGlow(D3D11Hud::Canvas h, float w, float s) { CanvasT* c = (CanvasT*)h; if (c) { c->glowW = std::max(0.0f, w); c->glowS = std::clamp(s, 0.0f, 1.0f); } }

void Polyline(D3D11Hud::Canvas h, const float* xy, int n, float width, DWORD col, unsigned flags, float dash)
{
    CanvasT* c = (CanvasT*)h; if (!c || !xy || n < 2) return;
    Prim(c);
    const float hw = std::max(0.25f, width * 0.5f * c->xs * c->ss), ds = (flags & D3D11Hud::DASHED) ? std::max(1.0f, dash) * c->xs * c->ss : 0;
    std::vector<float> P(n * 2);
    for (int i = 0; i < n; i++) T(c, xy[i * 2], xy[i * 2 + 1], P[i * 2], P[i * 2 + 1]);
    for (int i = 0; i + 1 < n; i++) Seg(c, P[i * 2], P[i * 2 + 1], P[i * 2 + 2], P[i * 2 + 3], hw, ds, col);
    if ((flags & D3D11Hud::CLOSED) && n > 2) Seg(c, P[n * 2 - 2], P[n * 2 - 1], P[0], P[1], hw, ds, col);
}
void Polygon(D3D11Hud::Canvas h, const float* xy, int n, DWORD fill)
{
    CanvasT* c = (CanvasT*)h; if (!c || !xy || n < 3) return;
    Prim(c);
    std::vector<float> P(n * 2);
    for (int i = 0; i < n; i++) T(c, xy[i * 2], xy[i * 2 + 1], P[i * 2], P[i * 2 + 1]);
    FillPoly(c, P, fill);
}
void Rect(D3D11Hud::Canvas h, float x0, float y0, float x1, float y1, DWORD fill, DWORD stroke, float width)
{
    CanvasT* c = (CanvasT*)h; if (!c) return;
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    if (fill) {
        Prim(c);
        float X[4], Y[4];
        T(c, x0, y0, X[0], Y[0]); T(c, x1, y0, X[1], Y[1]); T(c, x1, y1, X[2], Y[2]); T(c, x0, y1, X[3], Y[3]);
        Quad6(c, X, Y, nullptr, nullptr, nullptr, nullptr, fill, 0);
    }
    if (stroke && width > 0) {     // the outline inside the rectangle's edge, as GDI's
        const float i = width * 0.5f;
        const float xy[8] = { x0 + i, y0 + i, x1 - i, y0 + i, x1 - i, y1 - i, x0 + i, y1 - i };
        Polyline(h, xy, 4, width, stroke, D3D11Hud::CLOSED, 0);
    }
}
void Ellipse(D3D11Hud::Canvas h, float cx, float cy, float rx, float ry, DWORD fill, DWORD stroke, float width)
{
    CanvasT* c = (CanvasT*)h; if (!c) return;
    const bool turned = c->xf && (fabsf(c->m[1]) > 1e-6f || fabsf(c->m[3]) > 1e-6f);
    if (turned) {     // turned: a polygon and its outline
        std::vector<float> P; const int N = 64;
        for (int i = 0; i < N; i++) { P.push_back(cx + rx * cosf(6.2831853f * i / N)); P.push_back(cy + ry * sinf(6.2831853f * i / N)); }
        if (fill) Polygon(h, P.data(), N, fill);
        if (stroke && width > 0) Polyline(h, P.data(), N, width, stroke, D3D11Hud::CLOSED, 0);
        return;
    }
    float X, Y; T(c, cx, cy, X, Y);
    const float sx = (c->xf ? fabsf(c->m[0]) : 1.0f) * c->ss, sy = (c->xf ? fabsf(c->m[4]) : 1.0f) * c->ss;
    const float a = std::max(0.5f, rx * sx), b = std::max(0.5f, ry * sy);
    auto put = [&](float hw, float gw, DWORD col) {
        Prim(c);
        const float E = std::max(hw, 0.0f) + 1.5f + gw;
        const float p[4] = { X, Y, a, b }, q[4] = { hw, 0, gw, c->glowS };
        const float QX[4] = { X - a - E, X + a + E, X + a + E, X - a - E }, QY[4] = { Y - b - E, Y - b - E, Y + b + E, Y + b + E };
        Quad6(c, QX, QY, nullptr, nullptr, p, q, col, 2);
    };
    if (fill) put(-1, 0, fill);
    if (stroke && width > 0) put(std::max(0.25f, width * 0.5f * c->xs * c->ss), c->glowW * c->ss, stroke);
}
void Arc(D3D11Hud::Canvas h, float cx, float cy, float r, float a0, float a1, float width, DWORD col, unsigned flags, float dash)
{
    CanvasT* c = (CanvasT*)h; if (!c || r <= 0) return;
    const float span = a1 - a0;
    const int n = std::clamp((int)(fabsf(span) * r * c->ss / 3.0f) + 2, 3, 720);    // ~3 px a step: no visible facets
    std::vector<float> P;
    for (int i = 0; i < n; i++) { const float a = a0 + span * i / (n - 1); P.push_back(cx + r * cosf(a)); P.push_back(cy + r * sinf(a)); }
    Polyline(h, P.data(), n, width, col, flags & ~D3D11Hud::CLOSED, dash);
}
void Image(D3D11Hud::Canvas h, SURFHANDLE s, const float* sr, const float* dr, DWORD tint, const D3D11Hud::ImageFx* fx)
{
    CanvasT* c = (CanvasT*)h; Surf* src = (Surf*)s;
    if (!c || !src || !dr || src == c->out || !src->w || !src->h) return;
    const D3D11Hud::ImageFx f = fx ? *fx : D3D11Hud::ImageFx{ { 1, 1, 1 }, 1, 0 };
    if (src != c->src || memcmp(&f, &c->fx, sizeof f)) { Flush(c); c->src = src; c->fx = f; }
    Prim(c);
    const float s0 = sr ? sr[0] : 0, t0 = sr ? sr[1] : 0, s1 = sr ? sr[2] : (float)src->w, t1 = sr ? sr[3] : (float)src->h;
    float X[4], Y[4];
    T(c, dr[0], dr[1], X[0], Y[0]); T(c, dr[2], dr[1], X[1], Y[1]); T(c, dr[2], dr[3], X[2], Y[2]); T(c, dr[0], dr[3], X[3], Y[3]);
    const float U[4] = { s0 / src->w, s1 / src->w, s1 / src->w, s0 / src->w }, V[4] = { t0 / src->h, t0 / src->h, t1 / src->h, t1 / src->h };
    const bool srcAlpha = (src->attrib & OAPISURFACE_ALPHA) || !src->name.empty();
    const float p[4] = { srcAlpha ? 0.0f : 1.0f, 0, 0, 0 };
    Quad6(c, X, Y, U, V, p, nullptr, tint, 3);
}

D3D11Hud::Font FontLoad(const char* atlas, const char* table)
{
    D3D11Client* cl = g_client;
    if (!cl || !atlas || !table) return nullptr;
    FILE* f = fopen(table, "r");
    if (!f) { LOGC("D3D11Hud: font table %s not found", table); return nullptr; }
    FontT* ft = new FontT;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        float em, sp;
        if (sscanf(line, "SDF %f %f", &em, &sp) == 2) { ft->em = em; ft->spread = sp; continue; }
        unsigned cp; Glyph g;
        if (sscanf(line, "%u %f %f %f %f %f %f %f", &cp, &g.x, &g.y, &g.w, &g.h, &g.ox, &g.oy, &g.adv) == 8) ft->g[cp] = g;
    }
    fclose(f);
    ft->tex = cl->clbkLoadTexture(atlas, 0);
    if (!ft->tex) { LOGC("D3D11Hud: font atlas %s not found", atlas); delete ft; return nullptr; }
    DWORD w = 1, h = 1; cl->clbkGetSurfaceSize(ft->tex, &w, &h); ft->aw = (float)w; ft->ah = (float)h;
    LOGC("D3D11Hud: font %s, %u glyphs", atlas, (unsigned)ft->g.size());
    return (D3D11Hud::Font)ft;
}
void FontRelease(D3D11Hud::Font h) { FontT* f = (FontT*)h; if (!f) return; if (g_client && f->tex) g_client->clbkReleaseTexture(f->tex); delete f; }

float TextWidth(D3D11Hud::Font h, float size, const char* s)
{
    FontT* f = (FontT*)h; if (!f || !s) return 0;
    const float k = size / f->em; float w = 0;
    for (unsigned cp : Utf8(s)) { auto it = f->g.find(cp); if (it != f->g.end()) w += it->second.adv * k; }
    return w;
}
float Text(D3D11Hud::Canvas h, D3D11Hud::Font fh, float x, float y, float size, const char* s, DWORD col, int align)
{
    CanvasT* c = (CanvasT*)h; FontT* f = (FontT*)fh;
    if (!c || !f || !s || size <= 0) return 0;
    const float k = size / f->em, w = TextWidth(fh, size, s);
    const int ha = align & 3, va = align & 12;
    float px = x - (ha == D3D11Hud::CENTER ? w * 0.5f : ha == D3D11Hud::RIGHT ? w : 0.0f);
    float by = y;     // the baseline
    if (va == D3D11Hud::TOP) by = y + 0.80f * size; else if (va == D3D11Hud::MIDDLE) by = y + 0.36f * size; else if (va == D3D11Hud::BOTTOM) by = y - 0.22f * size;
    Surf* tex = (Surf*)f->tex;
    if (tex != c->src || c->fx.gain[0] != 1 || c->fx.gain[1] != 1 || c->fx.gain[2] != 1 || c->fx.gamma != 1 || c->fx.noise != 0) { Flush(c); c->src = tex; c->fx = { { 1, 1, 1 }, 1, 0 }; }
    Prim(c);
    // the distance's scale on the target: atlas units of 1/(2 spread) per 0.5 of the value -> px
    const float scale = k * c->xs * c->ss, q0 = 2 * f->spread * scale;
    const float gw = c->glowW * c->ss;
    for (unsigned cp : Utf8(s)) {
        auto it = f->g.find(cp);
        if (it == f->g.end()) continue;
        const Glyph& g = it->second;
        if (g.w > 0) {
            const float gx0 = px + g.ox * k, gy0 = by + g.oy * k, gx1 = gx0 + g.w * k, gy1 = gy0 + g.h * k;
            float X[4], Y[4];
            T(c, gx0, gy0, X[0], Y[0]); T(c, gx1, gy0, X[1], Y[1]); T(c, gx1, gy1, X[2], Y[2]); T(c, gx0, gy1, X[3], Y[3]);
            const float U[4] = { g.x / f->aw, (g.x + g.w) / f->aw, (g.x + g.w) / f->aw, g.x / f->aw }, V[4] = { g.y / f->ah, g.y / f->ah, (g.y + g.h) / f->ah, (g.y + g.h) / f->ah };
            const float q[4] = { q0, 0, gw, c->glowS };
            Quad6(c, X, Y, U, V, nullptr, q, col, 4);
        }
        px += g.adv * k;
    }
    return w;
}

// world-space paths: kept here, read by the scene's AR pass (Ar.cpp, HudArPaths)
std::mutex arMx;
std::map<int, HudArPath> arPaths;
void ArPath(int id, OBJHANDLE body, const double* llh, int n, float width, DWORD col, unsigned flags)
{
    std::lock_guard<std::mutex> lk(arMx);
    if (n <= 0 || !llh) { arPaths.erase(id); return; }
    HudArPath& p = arPaths[id];
    p.body = body; p.width = width; p.col = Col(col); p.flags = flags;
    p.llh.assign(llh, llh + n * 3);
}

const D3D11Hud::Api API = {
    D3D11Hud::VERSION, (int)sizeof(D3D11Hud::Api),
    CanvasCreate, CanvasRelease, CanvasSurface, Begin, End,
    SetBlend, SetTransform, SetClip, SetGlow,
    Polyline, Polygon, Rect, Ellipse, Arc, Image,
    FontLoad, FontRelease, Text, TextWidth,
    ArPath,
};

}  // namespace

// the paths for the scene's pass (a copy under the lock)
std::vector<HudArPath> HudArPaths()
{
    std::lock_guard<std::mutex> lk(arMx);
    std::vector<HudArPath> v; v.reserve(arPaths.size());
    for (auto& kv : arPaths) v.push_back(kv.second);
    return v;
}

DLLCLBK const D3D11Hud::Api* D3D11Hud_GetApi(int version)
{
    if (version < 1 || !g_client) return nullptr;
    LOGC("D3D11Hud: table version %d handed out (module asked %d)", D3D11Hud::VERSION, version);
    return &API;
}
