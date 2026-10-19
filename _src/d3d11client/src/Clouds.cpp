// Volumetric clouds (shaders\clouds.hlsl). Layers per body from Config\D3D11Client\Clouds.cfg:
//   Layer = <body> <deck|cumulus> <base m> <top m> <cover 0..1> <extinction 1/m> <feature size m> <wind m/s> <wind az deg>
//   Steps = <march steps>
// Marched at half resolution after the air (FX) pass, composited over the scene through its depth.
// Titan's layers follow Cassini/Huygens - see the config's notes.
#include "Client.h"
#include "Fx.h"
#include <cmath>

namespace {
    struct Layer { std::string body; bool cumulus; double base, top, cover, ext, scale, wind, az; };
    std::vector<Layer> layers;
    int steps = 40;
    int dbg = 0;     // CloudDbg (Clouds.cfg or a test run's Client.cfg): 1 = the clouds' opacity in white
    bool cfgRead = false;

    ID3D11Device* clDev = nullptr;
    bool clTried = false;
    ComPtr<ID3D11VertexShader> clVS; ComPtr<ID3D11PixelShader> clMarch, clComp;
    ComPtr<ID3D11Buffer> clCB;
    ComPtr<ID3D11Texture3D> clNoise; ComPtr<ID3D11ShaderResourceView> clNoiseSRV;
    ComPtr<ID3D11Texture2D> clTex, clTexT; ComPtr<ID3D11RenderTargetView> clRTV, clRTVT; ComPtr<ID3D11ShaderResourceView> clSRV, clSRVT;
    UINT clW = 0, clH = 0;
    ComPtr<ID3D11BlendState> clBlend;
    UINT clFrame = 0;

    void ReadCfg()
    {
        cfgRead = true;
        // test runs may point at another file (Client.cfg "CloudCfg = Clouds_test.cfg"); the user's own stays as it is
        char fn[160] = "Config\\D3D11Client\\Clouds.cfg";
        if (FILE* g = fopen("Config\\D3D11Client\\Client.cfg", "r")) {
            char ln[256], nm2[96];
            while (fgets(ln, sizeof ln, g)) if (sscanf(ln, " CloudCfg = %95s", nm2) == 1) sprintf_s(fn, "Config\\D3D11Client\\%s", nm2);
            fclose(g);
        }
        FILE* f = fopen(fn, "r");
        if (!f) { LOGC("clouds: no Config\\D3D11Client\\Clouds.cfg"); return; }
        char line[512];
        while (fgets(line, sizeof line, f)) {
            char body[64], kind[32]; Layer L; int n;
            if (sscanf(line, " Steps = %d", &n) == 1) { steps = std::clamp(n, 8, 128); continue; }
            if (sscanf(line, " Layer = %63s %31s %lf %lf %lf %lf %lf %lf %lf", body, kind, &L.base, &L.top, &L.cover, &L.ext, &L.scale, &L.wind, &L.az) == 9) {
                L.body = body; L.cumulus = !_stricmp(kind, "cumulus");
                if (L.top > L.base && L.scale > 1) layers.push_back(L);
            }
        }
        fclose(f);
        if (FILE* g = fopen("Config\\D3D11Client\\Client.cfg", "r")) { while (fgets(line, sizeof line, g)) sscanf(line, " CloudDbg = %d", &dbg); fclose(g); }
        LOGC("clouds: %u layer(s), %d steps (Config\\D3D11Client\\Clouds.cfg)", (unsigned)layers.size(), steps);
    }

    // ---- 3D noise, tiling: Perlin-Worley (r) and Worley at three frequencies (g, b, a) - as in Schneider's Nubis
    float Hash(int x, int y, int z, int s) { unsigned h = (unsigned)(x * 73856093 ^ y * 19349663 ^ z * 83492791 ^ s * 2654435761u); h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return (h & 0xffffff) / 16777216.0f; }
    float Worley(float x, float y, float z, int cells, int seed)
    {
        float fx = x * cells, fy = y * cells, fz = z * cells;
        int ix = (int)floor(fx), iy = (int)floor(fy), iz = (int)floor(fz);
        float best = 1e9f;
        for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int cx = ix + dx, cy = iy + dy, cz = iz + dz;
            int wx = ((cx % cells) + cells) % cells, wy = ((cy % cells) + cells) % cells, wz = ((cz % cells) + cells) % cells;
            float px = cx + Hash(wx, wy, wz, seed), py = cy + Hash(wx, wy, wz, seed + 1), pz = cz + Hash(wx, wy, wz, seed + 2);
            float d = (px - fx) * (px - fx) + (py - fy) * (py - fy) + (pz - fz) * (pz - fz);
            best = std::min(best, d);
        }
        return 1.0f - std::min(1.0f, sqrtf(best));
    }
    float Grad(int ix, int iy, int iz, int per, float x, float y, float z)
    {
        ix = ((ix % per) + per) % per; iy = ((iy % per) + per) % per; iz = ((iz % per) + per) % per;
        float a = Hash(ix, iy, iz, 7) * 6.2831853f, b = Hash(ix, iy, iz, 8) * 2 - 1, c = sqrtf(1 - b * b);
        return x * c * cosf(a) + y * c * sinf(a) + z * b;
    }
    float Perlin(float x, float y, float z, int per)
    {
        x *= per; y *= per; z *= per;
        int ix = (int)floor(x), iy = (int)floor(y), iz = (int)floor(z);
        float fx = x - ix, fy = y - iy, fz = z - iz;
        auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
        float u = fade(fx), v = fade(fy), w = fade(fz);
        float r = 0;
        for (int k = 0; k < 8; k++) {
            int dx = k & 1, dy = (k >> 1) & 1, dz = (k >> 2) & 1;
            float g = Grad(ix + dx, iy + dy, iz + dz, per, fx - dx, fy - dy, fz - dz);
            r += g * (dx ? u : 1 - u) * (dy ? v : 1 - v) * (dz ? w : 1 - w);
        }
        return r;
    }
    bool MakeNoise(ID3D11Device* dev)
    {
        const int S = 64;
        std::vector<uint8_t> px(S * S * S * 4);
        for (int z = 0; z < S; z++) for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
            float fx = (x + 0.5f) / S, fy = (y + 0.5f) / S, fz = (z + 0.5f) / S;
            float pf = 0, amp = 1, norm = 0;
            for (int o = 0; o < 4; o++) { pf += amp * Perlin(fx, fy, fz, 4 << o); norm += amp; amp *= 0.5f; }
            pf = pf / norm * 0.5f + 0.5f;
            float w1 = Worley(fx, fy, fz, 4, 11), w2 = Worley(fx, fy, fz, 8, 23), w3 = Worley(fx, fy, fz, 16, 37);
            float wf = w1 * 0.625f + w2 * 0.25f + w3 * 0.125f;
            float pw = std::clamp(pf + (wf - 0.5f) * 0.9f, 0.0f, 1.0f);          // Perlin-Worley: billows with the cells' edges
            uint8_t* q = &px[((z * S + y) * S + x) * 4];
            q[0] = (uint8_t)(pw * 255); q[1] = (uint8_t)(w2 * 255); q[2] = (uint8_t)(w3 * 255); q[3] = (uint8_t)(Worley(fx, fy, fz, 32, 41) * 255);
        }
        D3D11_TEXTURE3D_DESC td = { (UINT)S, (UINT)S, (UINT)S, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_SUBRESOURCE_DATA sd = { px.data(), (UINT)(S * 4), (UINT)(S * S * 4) };
        if (FAILED(dev->CreateTexture3D(&td, &sd, &clNoise))) return false;
        return SUCCEEDED(dev->CreateShaderResourceView(clNoise.Get(), nullptr, &clNoiseSRV));
    }
}

void D3D11Client::RenderClouds(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    if (!cfgRead) ReadCfg();
    OBJHANDLE hP = oapiCameraProxyGbody();
    if (!hP || layers.empty()) return;
    char nm[64] = ""; oapiGetObjectName(hP, nm, 63);
    std::vector<const Layer*> act;
    for (const Layer& L : layers) if (!_stricmp(L.body.c_str(), nm) && act.size() < 4) act.push_back(&L);
    if (act.empty()) return;
    if (clDev != dev.Get()) {
        clDev = dev.Get(); clTried = false; clVS.Reset(); clMarch.Reset(); clComp.Reset(); clCB.Reset(); clNoise.Reset(); clNoiseSRV.Reset();
        clTex.Reset(); clTexT.Reset(); clRTV.Reset(); clRTVT.Reset(); clSRV.Reset(); clSRVT.Reset(); clW = clH = 0; clBlend.Reset();
    }
    if (!clVS && !clTried) {
        clTried = true;
        auto b0 = CompileShaderFile("clouds.hlsl", "VS", "vs_5_0"), b1 = CompileShaderFile("clouds.hlsl", "PSMarch", "ps_5_0"), b2 = CompileShaderFile("clouds.hlsl", "PSComposite", "ps_5_0");
        if (b0 && b1 && b2) {
            dev->CreateVertexShader(b0->GetBufferPointer(), b0->GetBufferSize(), nullptr, &clVS);
            dev->CreatePixelShader(b1->GetBufferPointer(), b1->GetBufferSize(), nullptr, &clMarch);
            dev->CreatePixelShader(b2->GetBufferPointer(), b2->GetBufferSize(), nullptr, &clComp);
            D3D11_BUFFER_DESC bd = { sizeof(XMFLOAT4) * (4 + 3 + 12 + 2), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
            dev->CreateBuffer(&bd, nullptr, &clCB);
            D3D11_BLEND_DESC bl = {}; auto& t = bl.RenderTarget[0];
            t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_SRC_ALPHA; t.BlendOp = D3D11_BLEND_OP_ADD;
            t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
            dev->CreateBlendState(&bl, &clBlend);
            if (!MakeNoise(dev.Get())) clNoiseSRV.Reset();
        }
        LOGC("clouds: shaders %s, noise %s", clVS && clMarch && clComp ? "ready" : "FAILED", clNoiseSRV ? "ready" : "FAILED");
    }
    if (!clVS || !clMarch || !clComp || !clNoiseSRV || !clCB) return;
    const UINT w = std::max(1u, RW / 2), h = std::max(1u, RH / 2);
    if (!clTex || w != clW || h != clH) {
        clTex.Reset(); clTexT.Reset(); clRTV.Reset(); clRTVT.Reset(); clSRV.Reset(); clSRVT.Reset();
        D3D11_TEXTURE2D_DESC td = { w, h, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &clTex))) return;
        td.Format = DXGI_FORMAT_R32_FLOAT;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &clTexT))) return;
        dev->CreateRenderTargetView(clTex.Get(), nullptr, &clRTV); dev->CreateShaderResourceView(clTex.Get(), nullptr, &clSRV);
        dev->CreateRenderTargetView(clTexT.Get(), nullptr, &clRTVT); dev->CreateShaderResourceView(clTexT.Get(), nullptr, &clSRVT);
        clW = w; clH = h;
    }
    // constants
    VECTOR3 pp; oapiGetGlobalPos(hP, &pp);
    MATRIX3 Rp; oapiGetRotationMatrix(hP, &Rp);
    const double R = oapiGetSize(hP);
    const VECTOR3 camPl = tmul(Rp, cp - pp);
    // the camera's local east and north in the planet frame (Orbiter: +y the axis): the winds' directions
    VECTOR3 upL = camPl / length(camPl);
    VECTOR3 east = crossp(_V(0, 1, 0), upL); if (length(east) < 1e-6) east = _V(1, 0, 0); east = east / length(east);
    VECTOR3 north = crossp(upL, east);
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    struct { XMFLOAT4X4 inv; XMFLOAT4 r0, r1, r2; XMFLOAT4 lay[12]; XMFLOAT4 p, size; } k = {};
    XMStoreFloat4x4(&k.inv, XMMatrixTranspose(XMMatrixInverse(nullptr, V * proj)));
    k.r0 = { (float)Rp.m11, (float)Rp.m21, (float)Rp.m31, (float)(camPl.x / 1000.0) };
    k.r1 = { (float)Rp.m12, (float)Rp.m22, (float)Rp.m32, (float)(camPl.y / 1000.0) };
    k.r2 = { (float)Rp.m13, (float)Rp.m23, (float)Rp.m33, (float)(camPl.z / 1000.0) };
    for (size_t i = 0; i < act.size(); i++) {
        const Layer& L = *act[i];
        const double az = L.az * RAD;
        const VECTOR3 wd = east * sin(az) + north * cos(az);       // azimuth from north, towards east
        k.lay[i * 3] = { (float)(R + L.base), (float)(R + L.top), (float)L.cover, (float)L.ext };
        k.lay[i * 3 + 1] = { 0.85f, (float)L.scale, (float)L.wind, L.cumulus ? 1.0f : 0.0f };
        k.lay[i * 3 + 2] = { (float)wd.x, (float)wd.y, (float)wd.z, 0 };
    }
    k.p = { (float)act.size(), (float)fmod(oapiGetSimTime(), 1e5), (float)(clFrame++ % 64), (float)steps };
    k.size = { (float)w, (float)h, (float)dbg, 1.0f / h };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(clCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &k, sizeof k); ctx->Unmap(clCB.Get(), 0);

    ResolveDepth();
    ComPtr<ID3D11RenderTargetView> oR; ComPtr<ID3D11DepthStencilView> oD; ctx->OMGetRenderTargets(1, &oR, &oD);
    UINT nvp = 1; D3D11_VIEWPORT ovp; ctx->RSGetViewports(&nvp, &ovp);
    // the march, half resolution
    ID3D11RenderTargetView* rt[2] = { clRTV.Get(), clRTVT.Get() };
    ctx->OMSetRenderTargets(2, rt, nullptr);
    D3D11_VIEWPORT hv = { 0, 0, (float)w, (float)h, 0, 1 }; ctx->RSSetViewports(1, &hv);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    FxBindLight(this);
    ctx->VSSetShader(clVS.Get(), nullptr, 0); ctx->PSSetShader(clMarch.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, clCB.GetAddressOf());
    ID3D11ShaderResourceView* sr[2] = { clNoiseSRV.Get(), depthSRV.Get() };
    ctx->PSSetShaderResources(0, 2, sr);
    ID3D11SamplerState* ss[2] = { sampAniso.Get(), sampClamp.Get() };
    ctx->PSSetSamplers(0, 2, ss);
    ctx->Draw(3, 0);
    // over the scene
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
    if (nvp) ctx->RSSetViewports(1, &ovp);
    ctx->OMSetBlendState(clBlend.Get(), nullptr, ~0u);
    ctx->PSSetShader(clComp.Get(), nullptr, 0);
    ID3D11ShaderResourceView* sc[2] = { clSRV.Get(), clSRVT.Get() };
    ctx->PSSetShaderResources(2, 2, sc);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul[4] = {};
    ctx->PSSetShaderResources(0, 4, nul);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, oR.GetAddressOf(), oD.Get());
}
