// A moving cyclone (shaders\cyclone.hlsl): its weather map - one square texture over the storm, regenerated every frame
// on the GPU (512 x 512, ~0.05 ms) - read by the 2D cloud layer from afar (CloudLayer.cpp) and by the volumetric clouds
// near (EarthClouds.cpp). The storm moves: its centre travels along a great circle from where it was at MJD0, its
// stage advances (wave -> occluded), the features turn about the low.
//   Config\D3D11Client\Cyclone.cfg:
//   Cyclone = <body> <extratropical|tropical> <lat deg> <lng deg> <MJD0> <speed m/s> <heading deg> <radius km> <stage 0..1>
//             <stage per day> <max wind m/s> <radius of max wind km>
#include "Client.h"
#include "CloudLayer.h"
#include <cmath>

namespace {
    struct Cyc { std::string body; bool tropical = false; double lat0 = 0, lng0 = 0, mjd0 = 0, speed = 0, heading = 0, radius = 1e6,
                 stage0 = 0.5, stageRate = 0, vmax = 25, rmax = 3e5; };
    std::vector<Cyc> cycs;
    bool cfgRead = false;
    const Cyc* cur = nullptr;               // the camera's body's storm this frame
    double curLat = 0, curLng = 0, curStage = 0, curHalf = 0;
    ID3D11Device* cDev = nullptr;
    bool cTried = false;
    ComPtr<ID3D11VertexShader> cVS; ComPtr<ID3D11PixelShader> cPS; ComPtr<ID3D11Buffer> cCB;
    ComPtr<ID3D11Texture2D> cTex; ComPtr<ID3D11RenderTargetView> cRTV; ComPtr<ID3D11ShaderResourceView> cSRV;
    const UINT CN = 512;

    void ReadCfg()
    {
        cfgRead = true;
        FILE* f = fopen("Config\\D3D11Client\\Cyclone.cfg", "r");
        if (!f) return;
        char line[512];
        while (fgets(line, sizeof line, f)) {
            char* sc = strchr(line, ';'); if (sc) *sc = 0;
            char body[64], kind[32]; Cyc c;
            if (sscanf(line, " Cyclone = %63s %31s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf", body, kind, &c.lat0, &c.lng0, &c.mjd0, &c.speed,
                       &c.heading, &c.radius, &c.stage0, &c.stageRate, &c.vmax, &c.rmax) == 12) {
                c.body = body; c.tropical = !_stricmp(kind, "tropical");
                c.lat0 *= RAD; c.lng0 *= RAD; c.heading *= RAD; c.radius *= 1000; c.rmax *= 1000;
                cycs.push_back(c);
            }
        }
        fclose(f);
        LOGC("cyclone: %u storm(s) (Config\\D3D11Client\\Cyclone.cfg)", (unsigned)cycs.size());
    }

    // the centre after travelling d [m] along a great circle from (lat0, lng0) with the initial heading (from north)
    void Travel(double lat0, double lng0, double hdg, double d, double R, double& lat, double& lng)
    {
        double a = d / R;
        lat = asin(sin(lat0) * cos(a) + cos(lat0) * sin(a) * cos(hdg));
        lng = lng0 + atan2(sin(hdg) * sin(a) * cos(lat0), cos(a) - sin(lat0) * sin(lat));
        lng = remainder(lng, 2 * PI);
    }
}

// where the storm is now and its map; false: no storm on this body
bool CycloneUpdate(D3D11Client* c, ID3D11Device* dev, ID3D11DeviceContext* ctx, OBJHANDLE body)
{
    if (!cfgRead) ReadCfg();
    cur = nullptr;
    if (cycs.empty() || !body) { CloudLayerSetCyclone(nullptr); return false; }
    char nm[64] = ""; oapiGetObjectName(body, nm, 63);
    for (const Cyc& y : cycs) if (!_stricmp(y.body.c_str(), nm)) { cur = &y; break; }
    if (!cur) { CloudLayerSetCyclone(nullptr); return false; }
    if (cDev != dev) { cDev = dev; cTried = false; cVS.Reset(); cPS.Reset(); cCB.Reset(); cTex.Reset(); cRTV.Reset(); cSRV.Reset(); }
    if (!cVS && !cTried) {
        cTried = true;
        auto b0 = c->CompileShaderFile("cyclone.hlsl", "VS", "vs_5_0"), b1 = c->CompileShaderFile("cyclone.hlsl", "PSGen", "ps_5_0");
        if (b0 && b1) {
            dev->CreateVertexShader(b0->GetBufferPointer(), b0->GetBufferSize(), nullptr, &cVS);
            dev->CreatePixelShader(b1->GetBufferPointer(), b1->GetBufferSize(), nullptr, &cPS);
            D3D11_BUFFER_DESC bd = { sizeof(XMFLOAT4) * 3, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
            dev->CreateBuffer(&bd, nullptr, &cCB);
            D3D11_TEXTURE2D_DESC td = { CN, CN, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
            if (SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &cTex))) {
                dev->CreateRenderTargetView(cTex.Get(), nullptr, &cRTV);
                dev->CreateShaderResourceView(cTex.Get(), nullptr, &cSRV);
            }
        }
        LOGC("cyclone: shaders %s", cVS && cPS && cSRV ? "ready" : "FAILED");
    }
    if (!cVS || !cPS || !cSRV || !cCB) { CloudLayerSetCyclone(nullptr); return false; }
    const double R = oapiGetSize(body);
    const double mjd = oapiGetSimMJD();
    const double dtS = (mjd - cur->mjd0) * 86400.0;
    Travel(cur->lat0, cur->lng0, cur->heading, cur->speed * dtS, R, curLat, curLng);
    curStage = std::clamp(cur->stage0 + cur->stageRate * (mjd - cur->mjd0), 0.0, 1.0);
    curHalf = cur->radius * 1.5;
    struct { XMFLOAT4 a, b, c; } k;
    k.a = { (float)curHalf, (float)cur->radius, (float)curStage, curLat >= 0 ? 1.0f : -1.0f };
    k.b = { (float)fmod(dtS, 1e7), (float)cur->vmax, (float)cur->rmax, cur->tropical ? 1.0f : 0.0f };
    k.c = { 3.7f, 0, 0, 0 };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(cCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &k, sizeof k); ctx->Unmap(cCB.Get(), 0);
    ComPtr<ID3D11RenderTargetView> oR; ComPtr<ID3D11DepthStencilView> oD; ctx->OMGetRenderTargets(1, &oR, &oD);
    UINT nvp = 1; D3D11_VIEWPORT ovp; ctx->RSGetViewports(&nvp, &ovp);
    ctx->OMSetRenderTargets(1, cRTV.GetAddressOf(), nullptr);
    D3D11_VIEWPORT v = { 0, 0, (float)CN, (float)CN, 0, 1 }; ctx->RSSetViewports(1, &v);
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(cVS.Get(), nullptr, 0); ctx->PSSetShader(cPS.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, cCB.GetAddressOf());
    ctx->Draw(3, 0);
    ctx->OMSetRenderTargets(1, oR.GetAddressOf(), oD.Get());
    if (nvp) ctx->RSSetViewports(1, &ovp);
    CloudLayerSetCyclone(cSRV.Get());
    static int logged = 0;
    if (!logged++) LOGC("cyclone: %s %s at %.2f %.2f, stage %.2f, radius %.0f km", nm, cur->tropical ? "tropical" : "extratropical",
                        curLat * DEG, curLng * DEG, curStage, cur->radius / 1000);
    return true;
}

// for the 2D layer's constants: the storm's centre (lng, lat), its square's half-size, the blend (all 0 without one)
void CycloneLayerConstants(OBJHANDLE body, float* cy4)
{
    cy4[0] = cy4[1] = cy4[2] = cy4[3] = 0;
    if (!cur) return;
    cy4[0] = (float)curLng; cy4[1] = (float)curLat; cy4[2] = (float)curHalf; cy4[3] = 1.0f;
}

ID3D11ShaderResourceView* CycloneMap() { return cur ? cSRV.Get() : nullptr; }
