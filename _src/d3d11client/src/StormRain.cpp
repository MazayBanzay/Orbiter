// Earth's rain under the storm (shaders\storm_rain.hlsl). Where and how hard: the storm's map (Storm.cpp, StormRainHere).
// The method follows the open references, in own code: Wicked Engine (GPU particles simulated in a compute shader in a
// box round the camera, streaks along the velocity relative to the camera, a refracting lens with a Fresnel rim, splashes
// where the drops hit, a blocker round the person in focus), Garg & Nayar / Tremblay (faint streaks: opacity ~ the share
// of the exposure a drop covers a pixel; brighter than a dark background), Marshall-Palmer sizes, Atlas fall speeds.
// Titan's rain (Rain.cpp) is a separate system.
#include "Client.h"
#include "Fx.h"

bool StormRainHere(const VECTOR3& cp, double& mmh, VECTOR3& wind, double& alt);
float StormCameraLight();

namespace {
    const UINT N_MAX = 196608;          // particles in the box at the heaviest rain (each stands for many real drops: the
                                        // opacity follows their number, so fewer particles keep the same density)
    const UINT SPLASH_N = 8192;         // splash slots (a ring)
    const double BOX = 26.0;            // the box's side [m]
    ID3D11Device* srDev = nullptr;
    bool srTried = false, srInit = false;
    ComPtr<ID3D11ComputeShader> csInit, csSim, csEnv;
    ComPtr<ID3D11Buffer> bEnv; ComPtr<ID3D11UnorderedAccessView> uEnv; ComPtr<ID3D11ShaderResourceView> sEnv;
    ComPtr<ID3D11VertexShader> vsDrop, vsSplash; ComPtr<ID3D11PixelShader> psDrop, psSplash;
    ComPtr<ID3D11Buffer> srCB, bDrops, bSplash, bCount;
    ComPtr<ID3D11UnorderedAccessView> uDrops, uSplash, uCount;
    ComPtr<ID3D11ShaderResourceView> sDrops, sSplash;
    ComPtr<ID3D11BlendState> srBS;
    ComPtr<ID3D11Texture2D> srScene; ComPtr<ID3D11ShaderResourceView> srSceneSRV; UINT srW = 0, srH = 0;
    OBJHANDLE srBody = nullptr; VECTOR3 srA = { 0, 0, 0 }, srE1, srUp, srE2;   // the anchor (planet frame): origin, axes
    double srRate = 0, srLastT = 0;
    VECTOR3 srPrevCP = { 0, 0, 0 }; double srPrevSim = -1; VECTOR3 srCamV = { 0, 0, 0 };
    UINT srFrame = 0;

    struct CB {
        XMFLOAT4X4 vp;
        XMFLOAT4 e1, up, e2, cam, wind, vcam, p, l, blk, scr, nearp;
    };

    bool MakeBuf(ID3D11Device* dev, UINT n, UINT stride, ComPtr<ID3D11Buffer>& b, ComPtr<ID3D11UnorderedAccessView>& u, ComPtr<ID3D11ShaderResourceView>* s)
    {
        D3D11_BUFFER_DESC bd = { n * stride, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, stride };
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &b))) return false;
        if (FAILED(dev->CreateUnorderedAccessView(b.Get(), nullptr, &u))) return false;
        return !s || SUCCEEDED(dev->CreateShaderResourceView(b.Get(), nullptr, s->GetAddressOf()));
    }

    bool Init(D3D11Client* gc, ID3D11Device* dev)
    {
        srTried = true;
        auto b1 = gc->CompileShaderFile("storm_rain.hlsl", "CSInit", "cs_5_0"), b2 = gc->CompileShaderFile("storm_rain.hlsl", "CSSim", "cs_5_0");
        auto b3 = gc->CompileShaderFile("storm_rain.hlsl", "VSDrop", "vs_5_0"), b4 = gc->CompileShaderFile("storm_rain.hlsl", "PSDrop", "ps_5_0");
        auto b5 = gc->CompileShaderFile("storm_rain.hlsl", "VSSplash", "vs_5_0"), b6 = gc->CompileShaderFile("storm_rain.hlsl", "PSSplash", "ps_5_0");
        auto b7 = gc->CompileShaderFile("storm_rain.hlsl", "CSEnv", "cs_5_0");
        if (!b1 || !b2 || !b3 || !b4 || !b5 || !b6 || !b7) { LOGC("storm rain: shaders FAILED"); return false; }
        dev->CreateComputeShader(b1->GetBufferPointer(), b1->GetBufferSize(), nullptr, &csInit);
        dev->CreateComputeShader(b2->GetBufferPointer(), b2->GetBufferSize(), nullptr, &csSim);
        dev->CreateComputeShader(b7->GetBufferPointer(), b7->GetBufferSize(), nullptr, &csEnv);
        dev->CreateVertexShader(b3->GetBufferPointer(), b3->GetBufferSize(), nullptr, &vsDrop);
        dev->CreatePixelShader(b4->GetBufferPointer(), b4->GetBufferSize(), nullptr, &psDrop);
        dev->CreateVertexShader(b5->GetBufferPointer(), b5->GetBufferSize(), nullptr, &vsSplash);
        dev->CreatePixelShader(b6->GetBufferPointer(), b6->GetBufferSize(), nullptr, &psSplash);
        D3D11_BUFFER_DESC cd = { (UINT)sizeof(CB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&cd, nullptr, &srCB);
        if (!MakeBuf(dev, N_MAX, 16, bDrops, uDrops, &sDrops) || !MakeBuf(dev, SPLASH_N, 16, bSplash, uSplash, &sSplash) || !MakeBuf(dev, 1, 16, bEnv, uEnv, &sEnv)) { LOGC("storm rain: buffers FAILED"); return false; }
        D3D11_BUFFER_DESC rd = { 16, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0 };
        if (FAILED(dev->CreateBuffer(&rd, nullptr, &bCount))) return false;
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = 4; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        dev->CreateUnorderedAccessView(bCount.Get(), &ud, &uCount);
        D3D11_BLEND_DESC bl = {}; auto& t = bl.RenderTarget[0];   // premultiplied: the refracted scene replaces what lies behind
        t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; t.BlendOp = D3D11_BLEND_OP_ADD;
        t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        dev->CreateBlendState(&bl, &srBS);
        LOGC("storm rain: ready (%u drops, %u splash slots, %.0f m box)", N_MAX, SPLASH_N, BOX);
        return true;
    }
}

static double g_cpuRain = 0, g_cpuHere = 0;     // CPU ms (smoothed): the whole pass, the storm-map query in it
static double QpcMs() { LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return t.QuadPart * 1000.0 / f.QuadPart; }

void D3D11Client::RenderStormRain(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    const double tq0 = QpcMs();
    struct Tm { double t0; ~Tm() { g_cpuRain = 0.95 * g_cpuRain + 0.05 * (QpcMs() - t0); } } tmAll{ tq0 };
    double mmh, agl; VECTOR3 wind;
    const bool here = StormRainHere(cp, mmh, wind, agl);
    g_cpuHere = 0.95 * g_cpuHere + 0.05 * (QpcMs() - tq0);
    const double now = oapiGetSysTime(), dtr = srLastT > 0 ? std::clamp(now - srLastT, 0.0, 0.5) : 0.0;
    srLastT = now;
    srRate += ((here ? mmh : 0.0) - srRate) * (1 - exp(-dtr / 2.0));    // eases in and out over a few seconds
    if (srRate < 0.05) return;
    OBJHANDLE hP = oapiCameraProxyGbody();
    if (!hP) return;
    if (srDev != dev.Get()) {
        srDev = dev.Get(); srTried = false; srInit = false; csInit.Reset(); csSim.Reset(); csEnv.Reset(); bEnv.Reset(); uEnv.Reset(); sEnv.Reset(); vsDrop.Reset(); psDrop.Reset(); vsSplash.Reset(); psSplash.Reset();
        srCB.Reset(); bDrops.Reset(); bSplash.Reset(); bCount.Reset(); uDrops.Reset(); uSplash.Reset(); uCount.Reset(); sDrops.Reset(); sSplash.Reset();
        srBS.Reset(); srScene.Reset(); srSceneSRV.Reset(); srW = srH = 0; srBody = nullptr;
    }
    if (!srTried) Init(this, dev.Get());
    if (!csSim || !vsDrop || !psDrop || !srCB || !uDrops) return;
    VECTOR3 pp; oapiGetGlobalPos(hP, &pp);
    MATRIX3 Rp; oapiGetRotationMatrix(hP, &Rp);
    const VECTOR3 cP = tmul(Rp, cp - pp);
    // the anchor: fixed to the ground; renewed (and the drops re-made) when the camera is 30 km off it
    bool reinit = !srInit;
    if (srBody != hP || length(cP - srA) > 30000) {
        srBody = hP; srA = cP;
        srUp = cP / length(cP);
        srE1 = crossp(srUp, fabs(srUp.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)); srE1 = srE1 / length(srE1);
        srE2 = crossp(srUp, srE1);
        reinit = true;
    }
    const VECTOR3 dA = cP - srA;
    const VECTOR3 camA = _V(dotp(dA, srE1), dotp(dA, srUp), dotp(dA, srE2));
    // the camera's velocity over the ground (the streaks are the drops' motion relative to it)
    const double st = oapiGetSimTime();
    double simDt = 0;
    if (srPrevSim >= 0 && st > srPrevSim && st - srPrevSim < 1.0) {
        simDt = st - srPrevSim;
        const VECTOR3 v = (cP - srPrevCP) / simDt;
        srCamV = srCamV + (v - srCamV) * 0.3;
    } else srCamV = _V(0, 0, 0);
    srPrevCP = cP; srPrevSim = st;
    const VECTOR3 wl = tmul(Rp, wind);
    const VECTOR3 e1 = mul(Rp, srE1), up = mul(Rp, srUp), e2 = mul(Rp, srE2);
    // the person in focus (a vessel standing or moving near the ground): no drops inside their outline
    XMFLOAT4 blk = { 0, 0, 0, 0 }; float blkH = 0;
    if (OBJHANDLE hf = oapiGetFocusObject()) {
        VESSEL* v = oapiGetVesselInterface(hf);
        VECTOR3 fg; oapiGetGlobalPos(hf, &fg);
        const VECTOR3 fP = tmul(Rp, fg - pp) - srA;
        const double sz = v ? v->GetSize() : 0;
        if (sz > 0 && sz < 3 && length(fP - dA) < BOX) {
            blk = { (float)dotp(fP, srE1), (float)(dotp(fP, srUp) - sz * 0.9), (float)dotp(fP, srE2), (float)(sz * 0.45) };
            blkH = (float)(sz * 1.9);
        }
    }
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    CB k = {};
    XMStoreFloat4x4(&k.vp, XMMatrixTranspose(V * proj));
    k.e1 = { (float)e1.x, (float)e1.y, (float)e1.z, 0 };
    k.up = { (float)up.x, (float)up.y, (float)up.z, 0 };
    k.e2 = { (float)e2.x, (float)e2.y, (float)e2.z, 0 };
    k.cam = { (float)camA.x, (float)camA.y, (float)camA.z, (float)BOX };
    k.wind = { (float)dotp(wl, srE1), (float)dotp(wl, srUp), (float)dotp(wl, srE2), (float)std::min(simDt, 0.05) };
    k.vcam = { (float)dotp(srCamV, srE1), (float)dotp(srCamV, srUp), (float)dotp(srCamV, srE2), (float)fmod(st, 3600.0) + 1.0f };
    const double lambda = 4.1 * pow(std::max(srRate, 0.1), -0.21);     // Marshall-Palmer [1/mm]
    const UINT active = (UINT)(N_MAX * std::clamp(srRate / 60.0, 0.03, 1.0));
    const double pix = 2.0 * tan(oapiCameraAperture()) / std::max((double)RH, 1.0);
    k.p = { (float)srRate, (float)lambda, (float)active, (float)pix };
    k.l = { StormCameraLight(), 1.0f / 60.0f, (float)(camA.y - agl), (float)(srFrame++ * 2654435761u % 1000003u) };
    k.blk = blk;
    k.scr = { (float)RW, (float)RH, blkH, (float)SPLASH_N };
    // how many real drops each particle stands for: Marshall-Palmer's number density N0 / Lambda (N0 = 8000 /m3/mm) over
    // the particles' own density in the box - its streak's opacity is the sum of theirs (Garg & Nayar per drop)
    const double nReal = 8000.0 / lambda, nSim = active / (BOX * BOX * BOX);
    k.nearp = { oapiCameraInternal() ? 0.015f : 0.1f, (float)std::max(nReal / std::max(nSim, 1e-6), 1.0), 0, 0 };
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx->Map(srCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    memcpy(ms.pData, &k, sizeof k); ctx->Unmap(srCB.Get(), 0);

    // the simulation (the drops made afresh when the anchor is new)
    ComPtr<ID3D11RenderTargetView> oR; ComPtr<ID3D11DepthStencilView> oD; ctx->OMGetRenderTargets(1, &oR, &oD);
    ctx->CSSetConstantBuffers(1, 1, srCB.GetAddressOf());
    ctx->CSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
    ID3D11UnorderedAccessView* ua[3] = { uDrops.Get(), uSplash.Get(), uCount.Get() };
    ctx->CSSetUnorderedAccessViews(0, 3, ua, nullptr);
    if (reinit) {
        ctx->CSSetShader(csInit.Get(), nullptr, 0); ctx->Dispatch(N_MAX / 256, 1, 1);
        UINT zero[4] = {}; ctx->ClearUnorderedAccessViewUint(uSplash.Get(), zero);
        srInit = true;
    }
    if (simDt > 0) { ctx->CSSetShader(csSim.Get(), nullptr, 0); ctx->Dispatch(N_MAX / 256, 1, 1); }
    ID3D11UnorderedAccessView* nu[3] = {}; ctx->CSSetUnorderedAccessViews(0, 3, nu, nullptr);
    ctx->CSSetShader(nullptr, nullptr, 0);

    // the scene behind the drops (refraction) and its depth (the soft fade)
    if (!srScene || srW != RW || srH != RH) {
        srScene.Reset(); srSceneSRV.Reset();
        D3D11_TEXTURE2D_DESC td = { RW, RH, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &srScene))) dev->CreateShaderResourceView(srScene.Get(), nullptr, &srSceneSRV);
        srW = RW; srH = RH;
    }
    if (!srScene) return;
    CopySceneColour(srScene.Get());
    {   // the light the drops gather, once a frame
        ctx->CSSetConstantBuffers(1, 1, srCB.GetAddressOf());
        ctx->CSSetShaderResources(0, 1, srSceneSRV.GetAddressOf());
        ctx->CSSetUnorderedAccessViews(3, 1, uEnv.GetAddressOf(), nullptr);
        ctx->CSSetShader(csEnv.Get(), nullptr, 0); ctx->Dispatch(1, 1, 1);
        ID3D11UnorderedAccessView* n0 = nullptr; ID3D11ShaderResourceView* s0 = nullptr;
        ctx->CSSetUnorderedAccessViews(3, 1, &n0, nullptr); ctx->CSSetShaderResources(0, 1, &s0); ctx->CSSetShader(nullptr, nullptr, 0);
    }
    // drawn without the depth buffer bound (its view is read for the soft fade, which also hides drops behind things)
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
    D3D11_VIEWPORT vp = { 0, 0, (float)RW, (float)RH, 0, 1 }; ctx->RSSetViewports(1, &vp);
    ctx->OMSetBlendState(srBS.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    FxBindLight(this);
    ctx->VSSetConstantBuffers(1, 1, srCB.GetAddressOf()); ctx->PSSetConstantBuffers(1, 1, srCB.GetAddressOf());
    ID3D11ShaderResourceView* sv[5] = { srSceneSRV.Get(), sDrops.Get(), sSplash.Get(), depthSRV.Get(), sEnv.Get() };
    ctx->VSSetShaderResources(0, 5, sv); ctx->PSSetShaderResources(0, 5, sv);
    ctx->VSSetShader(vsDrop.Get(), nullptr, 0); ctx->PSSetShader(psDrop.Get(), nullptr, 0);
    ctx->DrawInstanced(4, active, 0, 0);
    ctx->VSSetShader(vsSplash.Get(), nullptr, 0); ctx->PSSetShader(psSplash.Get(), nullptr, 0);
    ctx->DrawInstanced(4, SPLASH_N, 0, 0);
    ID3D11ShaderResourceView* nul[5] = {}; ctx->VSSetShaderResources(0, 5, nul); ctx->PSSetShaderResources(0, 5, nul);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, oR.GetAddressOf(), oD.Get());
    static double tLog = -1e9;
    if (now - tLog > 20) { tLog = now; LOGC("storm rain: %.1f mm/h here, %u drops, wind %.0f m/s, camera %.0f m/s, %.1f m over the ground | CPU ms %.2f (map query %.2f)", srRate, active, length(wind), length(srCamV), agl, g_cpuRain, g_cpuHere); }
}
