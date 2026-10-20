// Spray off wheels in a liquid ("rooster tails"): a flipbook baked from a real liquid simulation (Blender's Mantaflow
// FLIP, tools\bake_splash_blender.py: liquid methane shed off a rim's rear quarter at 2.4 m/s in Titan's gravity, a fan of
// parcels each along the rim's tangent - the sheet stretches, tears into ligaments and drops and falls back), 64 frames
// over the whole flight, seen from the side (RGB the normal, A the coverage; tools\pack_splash.py ->
// Textures\D3D11Client\splash_wheel.dds). As current engines do it: a card per throw from the source, the stream made of
// overlapping throws; scaled by Froude similarity to the throw's speed and the body's gravity (lengths ~ v^2/g, times ~
// v/g), standing in the vertical plane of the throw; lit as a clear liquid (shaders\spray.hlsl). The drops of the GPU
// particles fly with it. Fed by GpuParticles::SpawnSource for a DROP source at a liquid's surface (TVChassis' wheels).
#include "Client.h"
#include "Fx.h"
#include "ExhaustInternal.h"
#include <map>

extern XMMATRIX g_exViewProj;

namespace {
    struct Card { VECTOR3 p0, h; double age, life, L, a; float mir; };
    struct Feed { double acc = 0, last = 0; };
    struct GCard { XMFLOAT3 p0; float frame; XMFLOAT3 h; float L; XMFLOAT4 misc; };   // h: the throw's horizontal direction; misc: opacity, mirror
    struct CB { XMFLOAT4 m0, m1, m2; XMFLOAT4X4 vp; XMFLOAT4 tint; };
    const UINT MAXC = 512, GRID = 8;
    const double THROW = 0.2;                 // one card per this much of the stream [s]
    const double U_BAKE = 2.4, G_BAKE = 1.352, T_BAKE = 64.0 / 15.0, L_BAKE = 6.0;   // the bake: rim speed, gravity, its 64 frames' time, the frame's size
    const double M_BAKE = 48 * 0.08 * 0.06 * 0.07 * 450;   // the liquid thrown in the bake [kg] (48 parcels of methane, ~7 kg)
    std::vector<Card> cards;
    std::map<uintptr_t, Feed> feeds;
    ComPtr<ID3D11VertexShader> spVS; ComPtr<ID3D11PixelShader> spPS;
    ComPtr<ID3D11Buffer> spCB, spBuf; ComPtr<ID3D11ShaderResourceView> spBufSRV, spTex;
    ComPtr<ID3D11BlendState> spBS;
    ComPtr<ID3D11Texture2D> spCopy; ComPtr<ID3D11ShaderResourceView> spCopySRV; UINT spW = 0, spH = 0;   // the scene behind (refraction)
    bool spTried = false, spOk = false;
    unsigned spRng = 0x9E3779B9u;
    double R01() { spRng ^= spRng << 13; spRng ^= spRng >> 17; spRng ^= spRng << 5; return (spRng & 0xFFFFFF) / 16777216.0; }

    bool SprayInit(D3D11Client* gc)
    {
        if (spTried) return spOk;
        spTried = true;
        ID3D11Device* dev = gc->Dev();
        auto bv = FxCompile("spray.hlsl", "VS", "vs_5_0"), bp = FxCompile("spray.hlsl", "PS", "ps_5_0");
        if (!bv || !bp || FAILED(dev->CreateVertexShader(bv->GetBufferPointer(), bv->GetBufferSize(), nullptr, &spVS)) ||
            FAILED(dev->CreatePixelShader(bp->GetBufferPointer(), bp->GetBufferSize(), nullptr, &spPS))) { LOGC("spray: shaders failed"); return false; }
        spTex = LoadDDSFile(dev, gc->Ctx(), "Textures\\D3D11Client\\splash_wheel.dds", false);
        if (!spTex) { LOGC("spray: Textures\\D3D11Client\\splash_wheel.dds missing (tools\\bake_splash_blender.py, pack_splash.py)"); return false; }
        D3D11_BUFFER_DESC d = { MAXC * sizeof(GCard), D3D11_USAGE_DYNAMIC, D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_WRITE,
                                D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(GCard) };
        if (FAILED(dev->CreateBuffer(&d, nullptr, &spBuf))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC s = {}; s.Format = DXGI_FORMAT_UNKNOWN; s.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        s.Buffer.NumElements = MAXC;
        if (FAILED(dev->CreateShaderResourceView(spBuf.Get(), &s, &spBufSRV))) return false;
        D3D11_BUFFER_DESC c = { (UINT)((sizeof(CB) + 15) & ~15u), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(dev->CreateBuffer(&c, nullptr, &spCB))) return false;
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
        if (FAILED(dev->CreateBlendState(&bd, &spBS))) return false;
        LOGC("spray: sheets on (flipbook %ux%u frames)", GRID, GRID);
        spOk = true;
        return true;
    }
}

// a DROP source at a liquid's surface this frame: p, v in the anchor frame [m, m/s], its mass rate [kg/s]
void SprayFeed(const ExScene& sc, uintptr_t key, const VECTOR3& p, const VECTOR3& v, double kgps)
{
    const double sp = length(v);
    if (sc.dt <= 0 || sp < 0.8 || kgps < 0.05) return;
    Feed& f = feeds[key];
    if (sc.simt - f.last > 0.3) f.acc = R01();      // a fresh stream: its first card at a random phase
    f.last = sc.simt;
    f.acc += sc.dt / THROW;
    const double g = sc.GM / std::max(dotp(sc.aP, sc.aP), 1.0);
    while (f.acc >= 1 && cards.size() < MAXC) {
        f.acc -= 1;
        Card c;
        // Froude similarity to the bake: the throw's speed (+-10 % per throw) against the bake's, its gravity against Titan's
        const double u = sp * (0.9 + 0.2 * R01());
        const double kL = (u / U_BAKE) * (u / U_BAKE) * (G_BAKE / g), kT = (u / U_BAKE) * (G_BAKE / g);
        c.L = L_BAKE * kL; c.life = T_BAKE * kT;
        VECTOR3 hz = _V(v.x, 0, v.z);
        c.h = length(hz) > 0.05 * sp ? hz / length(hz) : _V(1, 0, 0);
        c.p0 = p;
        // its opacity: the mass of this throw against the bake's scaled by similarity (L^3) - less liquid spread over the
        // same shapes is a thinner sheet, fewer and smaller drops (coverage ~ sqrt: the torn sheet keeps its outline)
        const double f = kgps * THROW / (M_BAKE * kL * kL * kL);
        c.a = std::clamp(1.6 * sqrt(f), 0.04, 1.0);
        c.age = 0;
        c.mir = R01() < 0.5 ? -1.0f : 1.0f;
        // a little off the stream's direction per throw (the chevrons, the rim's wobble)
        const double yaw = (R01() - 0.5) * 0.35, cy = cos(yaw), sy = sin(yaw);
        c.h = _V(c.h.x * cy - c.h.z * sy, 0, c.h.x * sy + c.h.z * cy);
        cards.push_back(c);
    }
}

void SprayRender(D3D11Client* gc, const ExScene& sc, ID3D11ShaderResourceView* depth)
{
    for (auto it = feeds.begin(); it != feeds.end();) { if (sc.simt - it->second.last > 1.0) it = feeds.erase(it); else ++it; }
    for (Card& c : cards) c.age += sc.dt;
    cards.erase(std::remove_if(cards.begin(), cards.end(), [](const Card& c) { return c.age >= c.life; }), cards.end());
    if (cards.empty() || !sc.anchored || !SprayInit(gc)) return;
    ID3D11DeviceContext* ctx = gc->Ctx();
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(spBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    GCard* g = (GCard*)m.pData;
    const UINT n = (UINT)std::min<size_t>(cards.size(), MAXC);
    const float NF = (float)(GRID * GRID - 1);
    for (UINT i = 0; i < n; i++) {
        const Card& c = cards[i];
        const double t = c.age / c.life;
        g[i].p0 = { (float)c.p0.x, (float)c.p0.y, (float)c.p0.z }; g[i].frame = (float)(t * NF);
        g[i].h = { (float)c.h.x, (float)c.h.y, (float)c.h.z }; g[i].L = (float)c.L;
        g[i].misc = { (float)(c.a * std::min(1.0, t / 0.03) * std::min(1.0, (1 - t) / 0.1)), c.mir, 0, 0 };
    }
    ctx->Unmap(spBuf.Get(), 0);
    CB k;
    VECTOR3 c0 = mul(sc.Rp, sc.a1), c1 = mul(sc.Rp, sc.aU), c2 = mul(sc.Rp, sc.a2);
    VECTOR3 o = mul(sc.Rp, sc.aP) + sc.pp - sc.cp;
    k.m0 = { (float)c0.x, (float)c1.x, (float)c2.x, (float)o.x };
    k.m1 = { (float)c0.y, (float)c1.y, (float)c2.y, (float)o.y };
    k.m2 = { (float)c0.z, (float)c1.z, (float)c2.z, (float)o.z };
    XMStoreFloat4x4(&k.vp, XMMatrixTranspose(g_exViewProj));
    // the liquid seen through: Titan's methane-ethane, clear with a faint yellow of dissolved organics; water clear
    k.tint = sc.titan ? XMFLOAT4{ 0.92f, 0.86f, 0.70f, (float)GRID } : XMFLOAT4{ 0.90f, 0.94f, 0.95f, (float)GRID };
    if (FAILED(ctx->Map(spCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, &k, sizeof k); ctx->Unmap(spCB.Get(), 0);

    // the scene so far, for what the sheet bends (a clear liquid is seen mostly by what it refracts and mirrors)
    if (spW != gc->SceneW() || spH != gc->SceneH() || !spCopy) {
        spW = gc->SceneW(); spH = gc->SceneH(); spCopy.Reset(); spCopySRV.Reset();
        D3D11_TEXTURE2D_DESC td = { spW, spH, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (SUCCEEDED(gc->Dev()->CreateTexture2D(&td, nullptr, &spCopy))) gc->Dev()->CreateShaderResourceView(spCopy.Get(), nullptr, &spCopySRV);
    }
    if (!spCopySRV) return;
    {
        ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11DepthStencilView> dsv;
        ctx->OMGetRenderTargets(1, &rtv, &dsv);
        gc->CopySceneColour(spCopy.Get());
        ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
    }
    FxBindLight(gc);
    ctx->OMSetBlendState(spBS.Get(), nullptr, ~0u);
    ctx->RSSetState(gc->rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(spVS.Get(), nullptr, 0);
    ctx->PSSetShader(spPS.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(5, 1, spCB.GetAddressOf());
    ctx->PSSetConstantBuffers(5, 1, spCB.GetAddressOf());
    ctx->VSSetShaderResources(4, 1, spBufSRV.GetAddressOf());
    ID3D11ShaderResourceView* ps[2] = { depth, spTex.Get() };
    ctx->PSSetShaderResources(5, 2, ps);
    ctx->PSSetShaderResources(3, 1, spCopySRV.GetAddressOf());
    ctx->DrawInstanced(6, n, 0, 0);
    ID3D11ShaderResourceView* nul[2] = {};
    ctx->VSSetShaderResources(4, 1, nul);
    ctx->PSSetShaderResources(6, 1, nul);
    ctx->PSSetShaderResources(3, 1, nul);
}
