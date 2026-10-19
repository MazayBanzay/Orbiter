// Rain (Titan's methane rain). Cassini/Huygens: no rain at the landing (January 2005), damp ground and a drizzle of
// ~50 mm a year likely (Tokano et al. 2006); downpours near the equinox (Turtle et al. 2011: surface darkening after
// storms). The drops: up to 9.5 mm (Lorenz 1993: larger break up), falling slowly in the dense, low-gravity air
// (~0.5 m/s at 1 mm, ~1.6 m/s at the largest) - rain on Titan drifts down more than it streaks.
// Rate: Config\D3D11Client\Fx.cfg "Rain = <mm per hour>" (0: none, the default); D3D11CLIENT_RAIN overrides it (tests).
// Where: "RainArea = <lat> <lon> <radius km> <fade km>" - only round that point (default: our Kraken Mare site, 59.33 N
// 40.49 E, 60 km, fading over the last 15: the northern polar seas, where Cassini saw summer rain - Dhingra et al. 2019).
// Drawn procedurally (shaders\rain.hlsl) in a 40 m box round the camera, locked to the ground; the drops that fall into
// a liquid ring its surface (the wave field, Wake.cpp).
#include "Client.h"
#include "Wake.h"
#include "Fx.h"

namespace {
    ID3D11Device* rnDev = nullptr;
    ComPtr<ID3D11VertexShader> rnVS; ComPtr<ID3D11PixelShader> rnPS;
    ComPtr<ID3D11Buffer> rnCB; ComPtr<ID3D11BlendState> rnBS;
    bool rnTried = false;
    double rnRate = -1;                       // mm/h
    double rnLat = 59.3335, rnLon = 40.49, rnRad = 60.0, rnFade = 15.0;   // the rain's area [deg, deg, km, km]
    OBJHANDLE rnBody = nullptr; VECTOR3 rnA = { 0, 0, 0 }, rnE1, rnUp, rnE2;   // the anchor (planet frame) and its axes
    unsigned rnRng = 0x2545F491u;
    double U01() { rnRng ^= rnRng << 13; rnRng ^= rnRng >> 17; rnRng ^= rnRng << 5; return (rnRng & 0xFFFFFF) / 16777216.0; }

    double RainRate()
    {
        if (rnRate >= 0) return rnRate;
        rnRate = 0;
        for (const char* fn : { "Config\\D3D11Client\\Fx.cfg", "Config\\D3D11Client\\Client.cfg" })   // (Client.cfg: test runs' -Set)
            if (FILE* f = fopen(fn, "r")) {
                char line[256];
                while (fgets(line, sizeof line, f)) {
                    double v, a, b, c, d;
                    if (sscanf(line, " Rain = %lf", &v) == 1) rnRate = std::max(0.0, v);
                    if (sscanf(line, " RainArea = %lf %lf %lf %lf", &a, &b, &c, &d) == 4) { rnLat = a; rnLon = b; rnRad = std::max(c, 0.1); rnFade = std::max(d, 0.01); }
                }
                fclose(f);
            }
        char e[32] = {};
        if (GetEnvironmentVariableA("D3D11CLIENT_RAIN", e, sizeof e)) rnRate = std::max(0.0, atof(e));
        LOGC("rain: %.2f mm/h on Titan within %.0f km of %.3f N %.3f E (Config\\D3D11Client\\Fx.cfg Rain, RainArea)", rnRate, rnRad, rnLat, rnLon);
        return rnRate;
    }
}

void D3D11Client::RenderRain(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    const double I0 = RainRate();
    if (I0 <= 0) return;
    OBJHANDLE hP = oapiCameraProxyGbody();
    if (!hP) return;
    char nm[32] = ""; oapiGetObjectName(hP, nm, 31);
    if (_stricmp(nm, "Titan")) return;
    VECTOR3 pp; oapiGetGlobalPos(hP, &pp);
    MATRIX3 Rp; oapiGetRotationMatrix(hP, &Rp);
    const double R = oapiGetSize(hP);
    const VECTOR3 cP = tmul(Rp, cp - pp);                        // the camera, planet frame
    if (length(cP) - R > 3000) return;                           // rain is near the ground
    // only over its area: the great-circle distance from the area's centre, faded at its edge
    double I = 0;
    {
        double lng, lat, rad; oapiGlobalToEqu(hP, cp, &lng, &lat, &rad);
        const double la0 = rnLat * RAD, lo0 = rnLon * RAD;
        const double c = sin(lat) * sin(la0) + cos(lat) * cos(la0) * cos(lng - lo0);
        const double dkm = R * acos(std::clamp(c, -1.0, 1.0)) / 1000.0;
        I = I0 * std::clamp((rnRad - dkm) / rnFade, 0.0, 1.0);
        static double tLog = -1e9; const double now = oapiGetSysTime();
        if (now - tLog > 30) { tLog = now; LOGC("rain: %.1f km from the area's centre, %.2f mm/h here", dkm, I); }
    }
    if (I <= 0) return;
    if (rnDev != dev.Get()) { rnDev = dev.Get(); rnVS.Reset(); rnPS.Reset(); rnCB.Reset(); rnBS.Reset(); rnTried = false; rnBody = nullptr; }
    if (!rnVS && !rnTried) {
        rnTried = true;
        auto b1 = CompileShaderFile("rain.hlsl", "VS", "vs_5_0"), b2 = CompileShaderFile("rain.hlsl", "PS", "ps_5_0");
        if (b1 && b2) {
            dev->CreateVertexShader(b1->GetBufferPointer(), b1->GetBufferSize(), nullptr, &rnVS);
            dev->CreatePixelShader(b2->GetBufferPointer(), b2->GetBufferSize(), nullptr, &rnPS);
            D3D11_BUFFER_DESC bd = { 160, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
            dev->CreateBuffer(&bd, nullptr, &rnCB);
            D3D11_BLEND_DESC bl = {}; auto& t = bl.RenderTarget[0];
            t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; t.BlendOp = D3D11_BLEND_OP_ADD;
            t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            dev->CreateBlendState(&bl, &rnBS);
        }
        LOGC("rain: shaders %s", rnVS && rnPS ? "ready" : "FAILED");
    }
    if (!rnVS || !rnPS || !rnCB) return;
    // the box's anchor: a point on the ground near the camera with its axes, renewed 5 km away (small numbers in floats)
    if (rnBody != hP || length(cP - rnA) > 5000) {
        rnBody = hP; rnA = cP;
        rnUp = cP / length(cP);
        rnE1 = crossp(rnUp, fabs(rnUp.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); rnE1 = rnE1 / length(rnE1);
        rnE2 = crossp(rnUp, rnE1);
    }
    const double B = 40.0;
    const VECTOR3 dA = cP - rnA;
    auto md = [B](double x) { double m = fmod(x, B); return m < 0 ? m + B : m; };
    // the liquid under the camera, if any: drops under its level are not drawn; those falling into it ring it
    double camOverLiq = 1e9;
    for (Planet* p : planets) if (p->hObj == hP && p->liq.has) camOverLiq = length(cP) - (R + p->liq.level);
    const VECTOR3 e1 = mul(Rp, rnE1), up = mul(Rp, rnUp), e2 = mul(Rp, rnE2);
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    struct { XMFLOAT4X4 vp; XMFLOAT4 e1, up, e2, off, p, drift; } k;
    XMStoreFloat4x4(&k.vp, XMMatrixTranspose(V * proj));
    k.e1 = { (float)e1.x, (float)e1.y, (float)e1.z, 0 };
    k.up = { (float)up.x, (float)up.y, (float)up.z, (float)camOverLiq };
    k.e2 = { (float)e2.x, (float)e2.y, (float)e2.z, 0 };
    k.off = { (float)md(dotp(dA, rnE1)), (float)md(dotp(dA, rnUp)), (float)md(dotp(dA, rnE2)), (float)B };
    // drizzle has small drops, downpours large ones (the mean of the spectrum grows with the rate, Marshall-Palmer-like)
    const double dMean = std::clamp(0.4e-3 * pow(std::max(I, 0.01), 0.21), 0.15e-3, 2.5e-3);
    const double pix = 2.0 * tan(oapiCameraAperture()) / std::max((double)RH, 1.0);
    k.p = { (float)fmod(oapiGetSimTime(), 3600.0), (float)dMean, (float)std::clamp(0.5 + 0.25 * I, 0.5, 2.0), 1.0f / 60.0f };
    k.drift = { 0, 0, 0, (float)pix };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(rnCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &k, sizeof k); ctx->Unmap(rnCB.Get(), 0);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
    ctx->OMSetBlendState(rnBS.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    FxBindLight(this);
    ctx->VSSetShader(rnVS.Get(), nullptr, 0); ctx->PSSetShader(rnPS.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, rnCB.GetAddressOf()); ctx->PSSetConstantBuffers(1, 1, rnCB.GetAddressOf());
    ctx->VSSetShaderResources(7, 1, shadowSRV.GetAddressOf());
    ctx->VSSetSamplers(2, 1, sampShadow.GetAddressOf());
    const UINT n = (UINT)std::clamp(I * 20000.0, 4000.0, 90000.0);
    ctx->DrawInstanced(6, n, 0, 0);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);

    // the drops that fall into the liquid: rings round the focus vessel (the wave field's window)
    if (camOverLiq < 1500 && oapiGetSimStep() > 0) {
        OBJHANDLE hF = oapiGetFocusObject();
        VECTOR3 fg; oapiGetGlobalPos(hF, &fg);
        const VECTOR3 fu = (fg - pp) / length(fg - pp);
        VECTOR3 f1 = crossp(fu, fabs(fu.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); f1 = f1 / length(f1);
        const VECTOR3 f2 = crossp(fu, f1);
        const int nk = (int)std::min(120.0, I * 40.0 + U01());
        for (int i = 0; i < nk; i++) {
            const double rr = 20.0 * sqrt(U01()), az = 2 * PI * U01();
            const double d = std::min(-log(std::max(U01(), 1e-4)) * dMean, 9.5e-3);
            WakeRipple(hP, fg + (f1 * cos(az) + f2 * sin(az)) * rr, std::max(0.015, 4 * d), std::clamp(d / 1e-3, 0.2, 2.0));
        }
    }
}
