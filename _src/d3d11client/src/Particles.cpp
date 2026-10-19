// Orbiter's particle streams (exhausts, contrails, vapour, reentry): emitted on the CPU in the global frame by the
// stream's spec (rate, speed, spread, lifetime, growth, slowdown in the air, level and density maps), drawn as
// camera-facing quads - emissive ones added, diffuse ones blended and lit by the Sun and the sky. The client owns the
// streams: a stream detached by its vessel ends when its last particle has gone.
#include "Client.h"

static double Rnd() { return rand() / (double)RAND_MAX; }

PStream::PStream(oapi::GraphicsClient* gc, PARTICLESTREAMSPEC* pss) : oapi::ParticleStream(gc, pss)
{
    if (pss) spec = *pss;
    else {
        memset(&spec, 0, sizeof spec);
        spec.srcsize = 1; spec.srcrate = 10; spec.v0 = 10; spec.srcspread = 0.1; spec.lifetime = 2; spec.growthrate = 1;
        spec.ltype = PARTICLESTREAMSPEC::DIFFUSE; spec.levelmap = PARTICLESTREAMSPEC::LVL_LIN; spec.atmsmap = PARTICLESTREAMSPEC::ATM_FLAT;
    }
}

double PStream::LevelAlpha(double L) const
{
    switch (spec.levelmap) {
    case PARTICLESTREAMSPEC::LVL_FLAT: return 1;
    case PARTICLESTREAMSPEC::LVL_LIN: return L;
    case PARTICLESTREAMSPEC::LVL_SQRT: return sqrt(std::max(L, 0.0));
    case PARTICLESTREAMSPEC::LVL_PLIN: return std::clamp((L - spec.lmin) / std::max(spec.lmax - spec.lmin, 1e-9), 0.0, 1.0);
    case PARTICLESTREAMSPEC::LVL_PSQRT: return sqrt(std::clamp((L - spec.lmin) / std::max(spec.lmax - spec.lmin, 1e-9), 0.0, 1.0));
    }
    return L;
}

void PStream::Update(double dt)
{
    // the particles move on (and slow down in the air towards the air's motion: the reference body's)
    for (auto& p : ps) {
        p.age += dt;
        if (p.rho > 0 && spec.atmslowdown > 0) {
            double k = exp(-spec.atmslowdown * dt);
            p.vel = p.air + (p.vel - p.air) * k;
        }
        p.pos += p.vel * dt;
        p.size += spec.growthrate * dt;
    }
    while (!ps.empty() && ps.front().age > ps.front().life) ps.pop_front();
    if (!level || !hRef || !oapiIsVessel(hRef)) return;
    double L = *level;
    if (L <= 0) return;
    double alpha = LevelAlpha(L);
    VESSEL* v = oapiGetVesselInterface(hRef);
    double rho = v->GetAtmDensity();
    switch (spec.atmsmap) {
    case PARTICLESTREAMSPEC::ATM_PLIN: alpha *= std::clamp((rho - spec.amin) / std::max(spec.amax - spec.amin, 1e-12), 0.0, 1.0); break;
    case PARTICLESTREAMSPEC::ATM_PLOG: alpha *= rho > spec.amin ? std::clamp(log(rho / spec.amin) / std::max(log(spec.amax / spec.amin), 1e-9), 0.0, 1.0) : 0.0; break;
    default: break;
    }
    if (alpha < 0.01) return;
    MATRIX3 R; v->GetRotationMatrix(R);
    VECTOR3 vp, vv; v->GetGlobalPos(vp); v->GetGlobalVel(vv);
    VECTOR3 air = vv;
    if (OBJHANDLE hb = v->GetSurfaceRef()) oapiGetGlobalVel(hb, &air);
    VECTOR3 src = vp + mul(R, pos ? *pos : lpos);
    VECTOR3 d = mul(R, dir ? *dir : ldir);
    acc += spec.srcrate * dt;
    while (acc >= 1.0 && ps.size() < 4000) {
        acc -= 1.0;
        P p;
        double sp = spec.v0 * (1 + spec.srcspread * (Rnd() * 2 - 1));
        VECTOR3 jitter = _V(Rnd() - 0.5, Rnd() - 0.5, Rnd() - 0.5) * (spec.srcspread * spec.v0);
        p.vel = vv + d * sp + jitter;
        double frac = Rnd();                       // spread the births over the step
        p.pos = src + (p.vel - vv) * (frac * dt);
        p.air = air; p.rho = rho;
        p.size = spec.srcsize; p.age = 0; p.life = spec.lifetime * (0.7 + 0.6 * Rnd());
        p.alpha = alpha;
        ps.push_back(p);
    }
}

// ------------------------------------------------------------------------------------------ drawing


void D3D11Client::UpdateRenderParticles(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj, double dt)
{
    for (size_t i = 0; i < streams.size();) {
        PStream* s = streams[i];
        s->Update(dt);
        if (s->Expired()) { delete s; streams.erase(streams.begin() + i); }
        else i++;
    }
    VECTOR3 right = _V(cr.m11, cr.m21, cr.m31), up = _V(cr.m12, cr.m22, cr.m32);
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    for (int pass = 0; pass < 2; pass++) {     // 0 diffuse (blended), 1 emissive (added)
        for (PStream* s : streams) {
            bool emis = s->spec.ltype == PARTICLESTREAMSPEC::EMISSIVE;
            if ((pass == 1) != emis || s->ps.empty()) continue;
            std::vector<PVert>& v = pverts; v.clear();
            for (auto& p : s->ps) {
                VECTOR3 c = p.pos - cp;
                double fade = 1 - p.age / p.life;
                float a = (float)(p.alpha * fade * (emis ? 1.0 : 0.6));
                if (a < 0.004f) continue;
                VECTOR3 r = right * p.size, u = up * p.size;
                VECTOR3 q[4] = { c - r - u, c - r + u, c + r + u, c + r - u };
                XMFLOAT2 uvs[4] = { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };
                int order[6] = { 0, 1, 2, 0, 2, 3 };
                for (int k : order) v.push_back({ { (float)q[k].x, (float)q[k].y, (float)q[k].z }, uvs[k], { 1, 1, 1, a } });
            }
            if (v.empty()) continue;
            UINT bytes = (UINT)(v.size() * sizeof(PVert));
            if (bytes > partVBSize) {
                partVB.Reset(); partVBSize = std::max(bytes * 2, 1u << 20);
                D3D11_BUFFER_DESC bd = { partVBSize, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
                dev->CreateBuffer(&bd, nullptr, &partVB);
            }
            D3D11_MAPPED_SUBRESOURCE ms;
            ctx->Map(partVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, v.data(), bytes); ctx->Unmap(partVB.Get(), 0);
            struct { XMFLOAT4X4 vp; XMFLOAT4 flags; } cb;
            XMStoreFloat4x4(&cb.vp, XMMatrixTranspose(V * proj));
            cb.flags = { emis ? 1.0f : 0.0f, 0, 0, 0 };
            ctx->Map(cbObj.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &cb, sizeof cb); ctx->Unmap(cbObj.Get(), 0);
            ctx->OMSetBlendState(emis ? bsAdd.Get() : bsAlpha.Get(), nullptr, ~0u);
            ctx->OMSetDepthStencilState(dsRead.Get(), 0);
            ctx->RSSetState(rsNone.Get());
            ctx->IASetInputLayout(ilPart.Get());
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            UINT stride = sizeof(PVert), off = 0;
            ctx->IASetVertexBuffers(0, 1, partVB.GetAddressOf(), &stride, &off);
            ctx->VSSetShader(vsPart.Get(), nullptr, 0);
            ctx->PSSetShader(psPart.Get(), nullptr, 0);
            ctx->VSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
            ctx->PSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
            Surf* t = (Surf*)s->spec.tex;
            ID3D11ShaderResourceView* srv = t && t->srv ? t->srv.Get() : partTex.Get();
            ctx->PSSetShaderResources(0, 1, &srv);
            ctx->Draw((UINT)v.size(), 0);
        }
    }
}

void D3D11Client::CreateParticleTexture()
{
    // the default particle: a soft round blob
    const int N = 64;
    std::vector<uint32_t> px(N * N);
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        double dx = (x + 0.5) / N * 2 - 1, dy = (y + 0.5) / N * 2 - 1;
        double r = sqrt(dx * dx + dy * dy);
        double a = r < 1 ? pow(1 - r, 1.6) : 0;
        uint32_t A = (uint32_t)(a * 255 + 0.5);
        px[y * N + x] = (A << 24) | 0xFFFFFF;
    }
    D3D11_TEXTURE2D_DESC td = { N, N, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    D3D11_SUBRESOURCE_DATA sd = { px.data(), N * 4, 0 };
    ComPtr<ID3D11Texture2D> tx;
    if (SUCCEEDED(dev->CreateTexture2D(&td, &sd, &tx))) dev->CreateShaderResourceView(tx.Get(), nullptr, &partTex);
}
