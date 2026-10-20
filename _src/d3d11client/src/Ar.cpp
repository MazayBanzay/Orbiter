// The augmented-reality pass: what the modules place in the world for the helmet display, drawn in the 3D scene.
//   - markers (AtmoFX_HudMarker: target, pad, glide-path gate, waypoint), while AtmoFX_HudFrame asks for MARKERS;
//   - paths (D3D11Hud::ArPath: routes, trajectories), for as long as they are set;
//   - terrain contours (AtmoFX_HudFrame TERRAIN): handed to the planet shader (ArTerrainOn), drawn on the ground.
// Lines are ribbons of a fixed pixel width built in the vertex shader (shaders\ar.hlsl), tested twice against the
// scene's depth: bright where seen, faint where hidden behind the terrain or hulls (as the AtmoFX D3D9 fork draws them).
#include "Ar.h"
#include "HudAr.h"

namespace {

struct Mark { int id; OBJHANDLE body; double lng, lat, alt, size, heading; int type; };
struct Path { OBJHANDLE body; std::vector<double> pts; float w; DWORD col; unsigned flags; };
struct ArV { float p[3], o[3]; float side, end, hw; UINT col; };

std::mutex mx;
int frameFlags = 0; float frameRGB[3] = { 0.35f, 1.0f, 0.45f }; float frameStep = 10; double frameT = -1e9;
std::vector<Mark> marks;
std::map<int, Path> paths;

struct ArRes {
    bool tried = false, ok = false;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps; ComPtr<ID3D11InputLayout> il;
    ComPtr<ID3D11Buffer> vb, cb; UINT vbCount = 0;
    ComPtr<ID3D11BlendState> add; ComPtr<ID3D11DepthStencilState> seen, hidden; ComPtr<ID3D11RasterizerState> rs;
} R;

bool FrameActive() { return oapiGetSysTime() - frameT < 0.5; }

bool Init(D3D11Client* c)
{
    if (R.tried) return R.ok;
    R.tried = true;
    ID3D11Device* dev = c->Dev();
    std::string path = c->ShaderDir() + "ar.hlsl"; std::wstring wp(path.begin(), path.end());
    ComPtr<ID3DBlob> vc, pc, err;
    if (FAILED(CachedCompileFromFile(wp.c_str(), nullptr, "VS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, &vc, &err))) { LOGC("ar.hlsl VS: %s", err ? (const char*)err->GetBufferPointer() : "not found"); return false; }
    err.Reset();
    if (FAILED(CachedCompileFromFile(wp.c_str(), nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, &pc, &err))) { LOGC("ar.hlsl PS: %s", err ? (const char*)err->GetBufferPointer() : "not found"); return false; }
    dev->CreateVertexShader(vc->GetBufferPointer(), vc->GetBufferSize(), nullptr, &R.vs);
    dev->CreatePixelShader(pc->GetBufferPointer(), pc->GetBufferSize(), nullptr, &R.ps);
    D3D11_INPUT_ELEMENT_DESC el[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ArV, p), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "OTHER", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ArV, o), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "SIDE", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(ArV, side), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "END", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(ArV, end), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "HALFW", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(ArV, hw), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ArV, col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(dev->CreateInputLayout(el, ARRAYSIZE(el), vc->GetBufferPointer(), vc->GetBufferSize(), &R.il))) return false;
    D3D11_BUFFER_DESC bd = { 96, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    dev->CreateBuffer(&bd, nullptr, &R.cb);
    D3D11_BLEND_DESC b = {}; auto& t = b.RenderTarget[0];
    t.BlendEnable = TRUE; t.SrcBlend = t.DestBlend = D3D11_BLEND_ONE; t.BlendOp = D3D11_BLEND_OP_ADD;
    t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    dev->CreateBlendState(&b, &R.add);
    D3D11_DEPTH_STENCIL_DESC d = {}; d.DepthEnable = TRUE; d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL; dev->CreateDepthStencilState(&d, &R.seen);      // reverse Z: nearer = greater
    d.DepthFunc = D3D11_COMPARISON_LESS; dev->CreateDepthStencilState(&d, &R.hidden);
    D3D11_RASTERIZER_DESC r = {}; r.FillMode = D3D11_FILL_SOLID; r.CullMode = D3D11_CULL_NONE; r.DepthClipEnable = FALSE;
    dev->CreateRasterizerState(&r, &R.rs);
    R.ok = R.vs && R.ps && R.il;
    LOGC("AR pass %s", R.ok ? "ready" : "NOT available");
    return R.ok;
}

// a point on a body (lng, lat, alt above the local ground) relative to the camera, and the local frame there
void Local(OBJHANDLE body, double lng, double lat, double alt, const VECTOR3& cam, VECTOR3& c, VECTOR3& up, VECTOR3& east, VECTOR3& north)
{
    MATRIX3 Rp; oapiGetRotationMatrix(body, &Rp);
    VECTOR3 pp; oapiGetGlobalPos(body, &pp);
    const double R = oapiGetSize(body) + oapiSurfaceElevation(body, lng, lat) + alt;
    const double cl = cos(lat), sl = sin(lat), cg = cos(lng), sg = sin(lng);
    up = mul(Rp, _V(cl * cg, sl, cl * sg));
    east = mul(Rp, _V(-sg, 0, cg));
    north = mul(Rp, _V(-sl * cg, cl, -sl * sg));
    c = pp + up * R - cam;
}

UINT Rgba(float r, float g, float b) { auto q = [](float v) { return (UINT)std::clamp((int)(v * 255 + 0.5f), 0, 255); }; return q(r) | (q(g) << 8) | (q(b) << 16) | 0xFF000000u; }

}  // namespace

// ------------------------------------------------------------------------------------------ the modules' side
DLLCLBK void AtmoFX_HudFrame(int flags, double r, double g, double b, double step)
{
    std::lock_guard<std::mutex> lk(mx);
    frameFlags = flags; frameRGB[0] = (float)r; frameRGB[1] = (float)g; frameRGB[2] = (float)b;
    frameStep = (float)(step > 0.5 ? step : 0.5); frameT = oapiGetSysTime();
}

DLLCLBK void AtmoFX_HudMarker(int id, OBJHANDLE hBody, double lng, double lat, double alt, int type, double size, double heading)
{
    std::lock_guard<std::mutex> lk(mx);
    for (size_t i = 0; i < marks.size(); i++) {
        if (marks[i].id != id) continue;
        if (type < 0) marks.erase(marks.begin() + i);
        else marks[i] = { id, hBody, lng, lat, alt, size, heading, type };
        return;
    }
    if (type >= 0) marks.push_back({ id, hBody, lng, lat, alt, size, heading, type });
}

void ArSetPath(int id, OBJHANDLE body, const double* lngLatAlt, int n, float widthPx, DWORD col, unsigned flags)
{
    std::lock_guard<std::mutex> lk(mx);
    if (n <= 1 || !lngLatAlt || !body) { paths.erase(id); return; }
    Path& p = paths[id];
    p.body = body; p.pts.assign(lngLatAlt, lngLatAlt + n * 3); p.w = std::max(widthPx, 0.5f); p.col = col; p.flags = flags;
}

bool ArTerrainOn(OBJHANDLE body, float* rgb, float* step)
{
    std::lock_guard<std::mutex> lk(mx);
    if (!FrameActive() || !(frameFlags & 1) || body != oapiCameraProxyGbody()) return false;
    rgb[0] = frameRGB[0]; rgb[1] = frameRGB[1]; rgb[2] = frameRGB[2]; *step = frameStep;
    return true;
}

// ------------------------------------------------------------------------------------------ in the frame
// test runs (D3D11CLIENT_TEST_AR=1): a target, a pad, a gate, a waypoint and a route laid out ahead of the focus vessel
static void ArTestScene()
{
    static int state = -1;
    if (state < 0) { char v[8] = {}; state = GetEnvironmentVariableA("D3D11CLIENT_TEST_AR", v, sizeof v) && v[0] == '1' ? 1 : 0; }
    if (state != 1) return;
    OBJHANDLE f = oapiGetFocusObject(); if (!f) return;
    VESSEL* ves = oapiGetVesselInterface(f); OBJHANDLE body = ves ? ves->GetSurfaceRef() : nullptr; if (!body) return;
    double lng, lat, rad; ves->GetEquPos(lng, lat, rad);
    double hdg = 0; oapiGetHeading(f, &hdg);
    const double R = oapiGetSize(body);
    auto at = [&](double ahead, double right, double& lo, double& la) {
        const double n = ahead * cos(hdg) - right * sin(hdg), e = ahead * sin(hdg) + right * cos(hdg);
        la = lat + n / R; lo = lng + e / (R * cos(lat));
    };
    AtmoFX_HudFrame(2, 0.35, 1.0, 0.45, 10.0);
    double lo, la;
    at(40, 0, lo, la);  AtmoFX_HudMarker(9001, body, lo, la, 0, 0, 3, 0);       // TARGET
    at(25, -12, lo, la); AtmoFX_HudMarker(9002, body, lo, la, 0, 1, 6, 0);      // PAD
    at(60, 8, lo, la);  AtmoFX_HudMarker(9003, body, lo, la, 6, 2, 8, hdg);     // GATE
    at(90, -20, lo, la); AtmoFX_HudMarker(9004, body, lo, la, 0, 3, 4, 0);      // WAYPOINT
    std::vector<double> pts;
    for (int i = 0; i <= 20; i++) { at(5 + i * 6.0, 10 * sin(i * 0.4), lo, la); pts.push_back(lo); pts.push_back(la); pts.push_back(1.0 + 0.2 * i); }
    ArSetPath(9001, body, pts.data(), (int)pts.size() / 3, 3.0f, 0xFF40A0FF, 0);
    static bool logged = false; if (!logged) { logged = true; LOGC("AR test scene laid out ahead of %s", ves->GetName()); }
}

void D3D11Client::RenderAr(const VECTOR3& cam, const MATRIX3& cr, const XMMATRIX& proj)
{
    ArTestScene();
    std::vector<ArV> v;
    const VECTOR3 fwd = _V(cr.m13, cr.m23, cr.m33);        // the camera's view axis (global)
    auto seg = [&](VECTOR3 a, VECTOR3 b, float hw, UINT col) {
        // cut at the near plane: a ribbon's ends must be in front of the camera
        double za = dotp(a, fwd), zb = dotp(b, fwd);
        const double zn = 0.15;
        if (za < zn && zb < zn) return;
        if (za < zn) a = b + (a - b) * ((zb - zn) / (zb - za));
        else if (zb < zn) b = a + (b - a) * ((za - zn) / (za - zb));
        const float A[3] = { (float)a.x, (float)a.y, (float)a.z }, B[3] = { (float)b.x, (float)b.y, (float)b.z };
        const float sd[6] = { -1, 1, 1, -1, 1, -1 }; const int en[6] = { 0, 0, 1, 0, 1, 1 };
        for (int i = 0; i < 6; i++) {
            ArV q; const float* P = en[i] ? B : A; const float* O = en[i] ? A : B;
            memcpy(q.p, P, 12); memcpy(q.o, O, 12); q.side = sd[i]; q.end = (float)en[i]; q.hw = hw; q.col = col;
            v.push_back(q);
        }
    };
    {
        std::lock_guard<std::mutex> lk(mx);
        const UINT hudCol = Rgba(frameRGB[0], frameRGB[1], frameRGB[2]);
        if (FrameActive() && (frameFlags & 2)) for (const Mark& m : marks) {
            if (!m.body || oapiGetObjectType(m.body) != OBJTP_PLANET) continue;
            VECTOR3 c, up, east, north; Local(m.body, m.lng, m.lat, m.alt, cam, c, up, east, north);
            if (length(c) > 50000.0) continue;
            const double s = m.size; const float hw = 1.5f;
            auto L = [&](double e0, double n0, double u0, double e1, double n1, double u1) { seg(c + east * e0 + north * n0 + up * u0, c + east * e1 + north * n1 + up * u1, hw, hudCol); };
            switch (m.type) {
            case 1: {       // PAD: corner brackets and a cross, on the ground
                const double h = s * 0.5, k = s * 0.18;
                for (int i = 0; i < 4; i++) {
                    const double se = (i & 1) ? h : -h, sn = (i & 2) ? h : -h;
                    L(se, sn, 0.05, se - (se > 0 ? k : -k), sn, 0.05); L(se, sn, 0.05, se, sn - (sn > 0 ? k : -k), 0.05);
                }
                L(-k, 0, 0.05, k, 0, 0.05); L(0, -k, 0.05, 0, k, 0.05);
                break;
            }
            case 2: {       // GATE: a frame's corners across the heading
                const double ch = cos(m.heading), sh = sin(m.heading), w = s * 0.5, hh = s * 0.3, k = s * 0.15;
                const VECTOR3 side = east * ch - north * sh;
                for (int i = 0; i < 4; i++) {
                    const double a = (i & 1) ? w : -w, b = (i & 2) ? hh : -hh;
                    const VECTOR3 p0 = c + side * a + up * b;
                    seg(p0, p0 - side * (a > 0 ? k : -k), hw, hudCol); seg(p0, p0 - up * (b > 0 ? k : -k), hw, hudCol);
                }
                break;
            }
            case 3: {       // WAYPOINT: an upright diamond
                const double h = s * 0.5;
                L(0, 0, 0, h, 0, h); L(h, 0, h, 0, 0, 2 * h); L(0, 0, 2 * h, -h, 0, h); L(-h, 0, h, 0, 0, 0);
                break;
            }
            default: {      // TARGET: a ring on the ground and a mast
                const int N = 32;
                for (int i = 0; i < N; i++) { const double a0 = i * 2 * PI / N, a1 = (i + 1) * 2 * PI / N; L(s * cos(a0), s * sin(a0), 0.05, s * cos(a1), s * sin(a1), 0.05); }
                L(0, 0, 0, 0, 0, 2 * s);
                break;
            }
            }
        }
        std::vector<Path> all;       // routes and trajectories: the modules' (D3D11Hud::ArPath, kept by HudApi.cpp) and the test's
        for (const HudArPath& h : HudArPaths()) all.push_back({ h.body, h.llh, std::max(h.width, 0.5f), h.col, h.flags });
        for (auto& kv : paths) all.push_back(kv.second);
        for (const Path& p : all) {
            if (oapiGetObjectType(p.body) != OBJTP_PLANET) continue;
            const int n = (int)p.pts.size() / 3;
            const UINT col = (p.col & 0xFF000000) ? p.col : (p.col | 0xFF000000);
            std::vector<VECTOR3> w(n);
            for (int i = 0; i < n; i++) { VECTOR3 up, e, no; Local(p.body, p.pts[i * 3], p.pts[i * 3 + 1], p.pts[i * 3 + 2], cam, w[i], up, e, no); }
            for (int i = 0; i + 1 < n; i++) seg(w[i], w[i + 1], p.w * 0.5f, col);
            if ((p.flags & 1) && n > 2) seg(w[n - 1], w[0], p.w * 0.5f, col);      // D3D11Hud::CLOSED
        }
    }
    if (v.empty() || !Init(this)) return;
    if (R.vbCount < v.size()) {
        R.vb.Reset(); R.vbCount = std::max<UINT>((UINT)v.size(), 4096);
        D3D11_BUFFER_DESC bd = { R.vbCount * (UINT)sizeof(ArV), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &R.vb))) { R.vbCount = 0; return; }
    }
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx->Map(R.vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    memcpy(ms.pData, v.data(), v.size() * sizeof(ArV)); ctx->Unmap(R.vb.Get(), 0);
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    struct { XMFLOAT4X4 vp; XMFLOAT4 scr; } cb;
    XMStoreFloat4x4(&cb.vp, XMMatrixTranspose(V * proj));
    UINT stride = sizeof(ArV), off = 0;
    ctx->IASetInputLayout(R.il.Get()); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 1, R.vb.GetAddressOf(), &stride, &off);
    ctx->VSSetShader(R.vs.Get(), nullptr, 0); ctx->PSSetShader(R.ps.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, R.cb.GetAddressOf()); ctx->PSSetConstantBuffers(1, 1, R.cb.GetAddressOf());
    ctx->OMSetBlendState(R.add.Get(), nullptr, ~0u); ctx->RSSetState(R.rs.Get());
    const float glow = 3.0f * (float)SceneH() / 1080.0f;
    for (int pass = 0; pass < 2; pass++) {      // hidden (faint) first, then seen (bright)
        cb.scr = { (float)SceneW(), (float)SceneH(), pass ? glow : 0.0f, pass ? 0.9f : 0.22f };
        ctx->UpdateSubresource(R.cb.Get(), 0, nullptr, &cb, 0, 0);
        ctx->OMSetDepthStencilState(pass ? R.seen.Get() : R.hidden.Get(), 0);
        ctx->Draw((UINT)v.size(), 0);
    }
}
