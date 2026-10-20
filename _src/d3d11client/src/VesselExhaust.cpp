// The vessels' own exhausts (VESSEL::AddExhaust: the stock engine flames, the jet pack's chambers), volumetric - each a
// cylinder down its jet, ray-marched (shaders\exhaust_vessel.hlsl). The D3D11 client drew none of them before
// (2026-10-20: "где объёмный факел истечения как у нас в dx9 клиенте?"). Drawn in the exhaust pass after the particles,
// soft against the scene's depth (t5, s3 bound by Exhaust.cpp), premultiplied.
#include "Client.h"
#include "Fx.h"
#include "ExhaustInternal.h"

extern XMMATRIX g_exViewProj;

namespace {
    struct CB { XMFLOAT4X4 wvp, toG; XMFLOAT4 cam, p, k; };
    ComPtr<ID3D11VertexShader> vxVS; ComPtr<ID3D11PixelShader> vxPS;
    ComPtr<ID3D11Buffer> vxCB; ComPtr<ID3D11BlendState> vxBS;
    ComPtr<ID3D11ShaderResourceView> vxDefTex;
    bool vxTried = false, vxOk = false;
    unsigned vxRng = 0x51ED27u;
    double R01() { vxRng ^= vxRng << 13; vxRng ^= vxRng >> 17; vxRng ^= vxRng << 5; return (vxRng & 0xFFFFFF) / 16777216.0; }

    bool Init(D3D11Client* gc)
    {
        if (vxTried) return vxOk;
        vxTried = true;
        ID3D11Device* dev = gc->Dev();
        auto bv = FxCompile("exhaust_vessel.hlsl", "VS", "vs_5_0"), bp = FxCompile("exhaust_vessel.hlsl", "PS", "ps_5_0");
        if (!bv || !bp || FAILED(dev->CreateVertexShader(bv->GetBufferPointer(), bv->GetBufferSize(), nullptr, &vxVS)) ||
            FAILED(dev->CreatePixelShader(bp->GetBufferPointer(), bp->GetBufferSize(), nullptr, &vxPS))) { LOGC("vessel exhausts: shaders failed"); return false; }
        D3D11_BUFFER_DESC c = { (UINT)((sizeof(CB) + 15) & ~15u), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(dev->CreateBuffer(&c, nullptr, &vxCB))) return false;
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
        if (FAILED(dev->CreateBlendState(&bd, &vxBS))) return false;
        vxDefTex = LoadDDSFile(dev, gc->Ctx(), "Textures\\Exhaust.dds", false);   // Orbiter's default exhaust texture
        LOGC("vessel exhausts: volumetric (%s default texture)", vxDefTex ? "with the" : "without the");
        vxOk = true;
        return true;
    }
}

void RenderVesselExhausts(D3D11Client* gc, const VECTOR3& cp)
{
    if (!Init(gc)) return;
    ID3D11DeviceContext* ctx = gc->Ctx();
    bool bound = false;
    const double t = oapiGetSimTime();
    for (auto& kv : gc->Visuals()) {
        VESSEL* v = kv.second ? kv.second->v : nullptr;
        if (!v) continue;
        const DWORD n = v->GetExhaustCount();
        if (!n) continue;
        VECTOR3 gp; v->GetGlobalPos(gp);
        MATRIX3 R; v->GetRotationMatrix(R);
        const VECTOR3 camV = tmul(R, cp - gp);                 // the camera in the vessel's frame
        for (DWORD i = 0; i < n; i++) {
            EXHAUSTSPEC es = {};
            if (!v->GetExhaustSpec(i, &es) || !es.lpos || !es.ldir || es.lsize <= 0 || es.wsize <= 0) continue;
            double level = v->GetExhaustLevel(i);
            if (es.modulate > 0) level *= (1.0 - es.modulate) + R01() * es.modulate;
            if (level <= 0.005) continue;
            const Surf* s = es.tex ? (const Surf*)es.tex : nullptr;
            ID3D11ShaderResourceView* tex = s && s->srv ? s->srv.Get() : vxDefTex.Get();
            if (!tex) continue;
            // the exhaust's frame in the vessel's: origin at the exit, z down the jet (against ldir)
            VECTOR3 e = -(*es.ldir); const double le = length(e); if (le <= 0) continue; e = e / le;
            const VECTOR3 o = (*es.lpos) - (*es.ldir) * es.lofs;
            VECTOR3 sx = crossp(e, fabs(e.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); sx = sx / length(sx);
            const VECTOR3 sy = crossp(e, sx);
            // exhaust frame -> camera-centred global (row vectors): rows R sx, R sy, R e, (gp - cp) + R o
            const VECTOR3 gx = mul(R, sx), gy = mul(R, sy), gz = mul(R, e), go = (gp - cp) + mul(R, o);
            XMMATRIX M((float)gx.x, (float)gx.y, (float)gx.z, 0, (float)gy.x, (float)gy.y, (float)gy.z, 0,
                       (float)gz.x, (float)gz.y, (float)gz.z, 0, (float)go.x, (float)go.y, (float)go.z, 1);
            const VECTOR3 dc = camV - o;
            CB k;
            XMStoreFloat4x4(&k.wvp, XMMatrixTranspose(M * g_exViewProj));
            XMStoreFloat4x4(&k.toG, XMMatrixTranspose(M));
            k.cam = { (float)dotp(dc, sx), (float)dotp(dc, sy), (float)dotp(dc, e), (float)es.wsize };
            k.p = { (float)es.lsize, (float)std::min(level, 1.0), (float)R01(), (float)fmod(t, 1000.0) };
            k.k = { 1.6f, 0, 0, 0 };
            D3D11_MAPPED_SUBRESOURCE m;
            if (FAILED(ctx->Map(vxCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) continue;
            memcpy(m.pData, &k, sizeof k); ctx->Unmap(vxCB.Get(), 0);
            if (!bound) {
                bound = true;
                ctx->OMSetBlendState(vxBS.Get(), nullptr, ~0u);
                ctx->RSSetState(gc->rsNone.Get());
                ctx->IASetInputLayout(nullptr);
                ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                ctx->VSSetShader(vxVS.Get(), nullptr, 0);
                ctx->PSSetShader(vxPS.Get(), nullptr, 0);
                ctx->VSSetConstantBuffers(5, 1, vxCB.GetAddressOf());
                ctx->PSSetConstantBuffers(5, 1, vxCB.GetAddressOf());
            }
            ctx->PSSetShaderResources(6, 1, &tex);
            ctx->Draw(32 * 12, 0);
        }
    }
    if (bound) { ID3D11ShaderResourceView* nul = nullptr; ctx->PSSetShaderResources(6, 1, &nul); }
}
