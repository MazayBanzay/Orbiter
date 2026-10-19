// The atmosphere seen from space or from high up: one full-screen pass per planet with an atmosphere, after the opaque
// scene. Along each view ray through the haze shell: Beer-Lambert extinction (applied to what is behind, from the scene
// depth) and the light the haze scatters towards the camera - single scattering of the direct Sun (Henyey-Greenstein,
// the Sun's path through the haze by the Chapman function: terminator, twilight, the night side) plus a multiply-scattered
// part sized so that a deep haze reflects its semi-infinite albedo (van de Hulst's similarity formula). Inside the
// atmosphere the pass fades out as the client's own sky and haze take over (the same fade with height as theirs).
// Data: Config\D3D11Client\<planet>.cfg - HazeExt, HazeRGB, ScaleHeight, Top, Sun (the client's keys) and
//   HazeH (haze scale height of the shell), HazeSSA (single-scattering albedo R G B), HazeG (asymmetry) - ours.
#include "FxInternal.h"

namespace {

struct AtmoCB
{
    XMFLOAT4 row0, row1, row2;   // camera -> global rotation rows
    XMFLOAT4 tan;                // tan half-fov x, y; near plane; 0
    XMFLOAT4 centre;             // planet centre from the camera [m]; w: R
    XMFLOAT4 cam;                // camera distance from the centre, camera altitude, shell top radius, haze scale height
    XMFLOAT4 sun;                // to the Sun from the planet; w: g
    XMFLOAT4 ext;                // extinction at the ground [1/m] per channel; w: 0
    XMFLOAT4 ssa;                // single-scattering albedo; w: weight of the pass
    XMFLOAT4 ms;                 // multiple-scattering amplitude; w: 1 = the scene was lit as in open space (relight it)
    XMFLOAT4 kappa;              // decay of the diffuse light per optical depth; w: 0
    XMFLOAT4 surf;               // sunlight at the ground under the haze (cfg Sun); w: 0
};

struct Extra { double H = 0, g = 0.3; VECTOR3 ssa = { 0, 0, 0 }, ext = { 0, 0, 0 }; bool haveSSA = false; };

ComPtr<ID3D11VertexShader> vs;
ComPtr<ID3D11PixelShader> ps;
ComPtr<ID3D11Buffer> cb;
ComPtr<ID3D11BlendState> bsDual;
std::map<Planet*, Extra> extras;

Extra LoadExtra(Planet* p)
{
    Extra e;
    e.H = p->atm.scaleH;
    FILE* f = fopen(("Config\\D3D11Client\\" + p->name + ".cfg").c_str(), "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            char* sc = strchr(line, ';'); if (sc) *sc = 0;
            char key[64]; double a, b, c;
            int n = sscanf(line, " %63[A-Za-z0-9] = %lf %lf %lf", key, &a, &b, &c);
            if (n < 2) continue;
            std::string k = key;
            if (k == "HazeH") e.H = a;
            else if (k == "HazeG") e.g = a;
            else if (k == "HazeSSA") { e.ssa = _V(a, n > 2 ? b : a, n > 3 ? c : a); e.haveSSA = true; }
        }
        fclose(f);
    }
    if (e.haveSSA)      // our haze data: the client's extinction at the ground and its spectral weights
        e.ext = p->atm.hazeRGB * p->atm.hazeExt;
    else {              // no data: clear gas, Rayleigh scattering (no absorption, lambda^-4 at 610/550/465 nm), Earth's
                        // 1.16e-5 /m at 550 nm scaled by the density at the ground (estimate for other gases)
        e.ssa = _V(1, 1, 1); e.g = 0;
        e.ext = _V(0.66, 1.0, 1.96) * (1.16e-5 * p->atm.rho0 / 1.225);
    }
    LOGC("atmosphere from space, %s: H %.0f km, extinction %.2g %.2g %.2g /m, SSA %.2f %.2f %.2f, g %.2f%s", p->name.c_str(), e.H / 1000,
         e.ext.x, e.ext.y, e.ext.z, e.ssa.x, e.ssa.y, e.ssa.z, e.g, e.haveSSA ? " (haze data)" : " (Rayleigh, estimate)");
    return e;
}

}  // namespace

bool FxAtmoInit(D3D11Client* gc)
{
    ID3D11Device* dev = gc->Dev();
    auto bv = FxCompile("fx_atmo.hlsl", "VS", "vs_5_0");
    auto bp = FxCompile("fx_atmo.hlsl", "PS", "ps_5_0");
    if (!bv || !bp) return false;
    dev->CreateVertexShader(bv->GetBufferPointer(), bv->GetBufferSize(), nullptr, &vs);
    dev->CreatePixelShader(bp->GetBufferPointer(), bp->GetBufferSize(), nullptr, &ps);
    D3D11_BUFFER_DESC d = { (sizeof(AtmoCB) + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    dev->CreateBuffer(&d, nullptr, &cb);
    // dual-source blending: colour = in-scattered light + scene * transmittance (per channel)
    D3D11_BLEND_DESC bd = {};
    auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_SRC1_COLOR; rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ZERO; rt.DestBlendAlpha = D3D11_BLEND_ONE; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    dev->CreateBlendState(&bd, &bsDual);
    return vs && ps && cb && bsDual;
}

void FxAtmoRelease()
{
    vs.Reset(); ps.Reset(); cb.Reset(); bsDual.Reset();
    extras.clear();
}

void FxAtmoRender(D3D11Client* gc, const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    if (!g_fxPlanets || !gc->depthSRV) return;
    ID3D11DeviceContext* ctx = gc->Ctx();
    VECTOR3 sp = _V(0, 0, 0);
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) { oapiGetGlobalPos(h, &sp); break; } }
    XMFLOAT4X4 P; XMStoreFloat4x4(&P, proj);
    float tanX = 1.0f / P._11, tanY = 1.0f / P._22;
    double diag = atan(sqrt((double)tanX * tanX + (double)tanY * tanY));
    VECTOR3 fwd = _V(cr.m13, cr.m23, cr.m33);

    // the planets whose shell is in view, the farthest first
    std::vector<std::pair<double, Planet*>> list;
    for (Planet* p : *g_fxPlanets) {
        if (!p->atm.has) continue;
        if (!extras.count(p)) extras[p] = LoadExtra(p);
        if (extras[p].H < 100 || extras[p].ext.y <= 0) continue;   // no air worth a pass (Io)
        VECTOR3 pp; oapiGetGlobalPos(p->hObj, &pp);
        VECTOR3 c = pp - cp; double dist = length(c);
        double Rt = p->R + std::min(p->atm.top, 15 * extras[p].H);
        if (dist > Rt) {
            double ang = acos(std::clamp(dotp(c, fwd) / dist, -1.0, 1.0));
            if (ang > diag + asin(Rt / dist) + 0.01) continue;
        }
        list.push_back({ dist, p });
    }
    if (list.empty()) return;
    std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.first > b.first; });

    ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11DepthStencilView> dsv;
    ctx->OMGetRenderTargets(1, &rtv, &dsv);
    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);     // the depth is read as a texture
    ctx->OMSetBlendState(bsDual.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(gc->dsRead.Get(), 0);
    ctx->RSSetState(gc->rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(3, 1, cb.GetAddressOf());
    ctx->PSSetConstantBuffers(3, 1, cb.GetAddressOf());
    ctx->PSSetShaderResources(0, 1, gc->depthSRV.GetAddressOf());

    for (auto& it : list) {
        Planet* p = it.second;
        const AtmCfg& a = p->atm;
        const Extra& e = extras[p];
        VECTOR3 pp; oapiGetGlobalPos(p->hObj, &pp);
        VECTOR3 c = pp - cp;
        double rc = length(c), alt = rc - p->R;
        double Rt = p->R + std::min(a.top, 15 * e.H);
        VECTOR3 sd = sp - pp; sd = sd / length(sd);
        // inside the atmosphere the client draws the sky and the haze, fading with height: this pass takes the rest
        double w = alt < a.top ? 1 - exp(-std::max(alt, 0.0) / (a.scaleH * 2.5)) : 1.0;
        double g = e.g, pb = (1 - g * g) / pow(1 + g, 3);     // HG phase (mean 1) straight back
        double ssa[3] = { e.ssa.x, e.ssa.y, e.ssa.z }, ms[3], kap[3];
        double tv[3] = { e.ext.x * e.H, e.ext.y * e.H, e.ext.z * e.H };   // vertical optical depth
        for (int k = 0; k < 3; k++) {
            double om = std::clamp(ssa[k], 0.0, 0.9999);
            double s = sqrt((1 - om) / (1 - om * g));
            double Rinf = (1 - s) * (1 - 0.139 * s) / (1 + 1.17 * s);   // spherical albedo of a semi-infinite haze
            kap[k] = std::max(sqrt(3 * (1 - om) * (1 - om * g)), 0.05);   // diffusion: the decay of the diffuse light
            ms[k] = std::max(Rinf - om * pb / 8, 0.0) * (1 + kap[k]);       // the nadir reflectance comes out as Rinf
            ms[k] *= 1 - exp(-tv[k]);                                       // a thin layer scatters in proportion
        }
        double ext = g_cfg.haze;
        AtmoCB k;
        k.row0 = { (float)cr.m11, (float)cr.m12, (float)cr.m13, 0 };
        k.row1 = { (float)cr.m21, (float)cr.m22, (float)cr.m23, 0 };
        k.row2 = { (float)cr.m31, (float)cr.m32, (float)cr.m33, 0 };
        k.tan = { tanX, tanY, P._43, 0 };
        k.centre = { (float)c.x, (float)c.y, (float)c.z, (float)p->R };
        k.cam = { (float)rc, (float)alt, (float)Rt, (float)e.H };
        k.sun = { (float)sd.x, (float)sd.y, (float)sd.z, (float)g };
        k.ext = { (float)(ext * e.ext.x), (float)(ext * e.ext.y), (float)(ext * e.ext.z), 0 };
        k.ssa = { (float)ssa[0], (float)ssa[1], (float)ssa[2], (float)w };
        k.ms = { (float)ms[0], (float)ms[1], (float)ms[2], gc->skyOn ? 0.0f : 1.0f };
        k.kappa = { (float)kap[0], (float)kap[1], (float)kap[2], 0 };
        k.surf = { (float)a.sun.x, (float)a.sun.y, (float)a.sun.z, 0 };
        D3D11_MAPPED_SUBRESOURCE m;
        ctx->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, &k, sizeof k); ctx->Unmap(cb.Get(), 0);
        ctx->Draw(3, 0);
    }

    ID3D11ShaderResourceView* nul = nullptr;
    ctx->PSSetShaderResources(0, 1, &nul);
    ctx->OMSetBlendState(gc->bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
}
