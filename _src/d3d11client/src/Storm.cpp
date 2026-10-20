// Earth's storm: one tropical cyclone, volumetric (shaders\storm.hlsl, Config\D3D11Client\Storm.cfg). A system of its
// own, written from scratch; Titan's clouds (Clouds.cpp) are not touched or shared.
//   - the 3D noises are made once on the GPU (compute), the weather map round the centre is redrawn as the storm turns;
//   - RenderStormShadow (before the air): the clouds' shadow multiplied into what is drawn;
//   - RenderStorm (after the air): the march at half resolution, then over the scene through its depth;
//   - StormPreStep: the turbulence in the cloud for the vessels flying through it (AddForce), from a small copy of the
//     map read back from the GPU.
#include "Client.h"
#include "Fx.h"
#include <cmath>
#include <random>
#include <future>

namespace {
    struct Cfg {
        bool on = true;
        std::string body = "Earth";
        double lat = 26.5, lon = -76.0;     // the centre [deg] at mjd0
        double mjd0 = 0;                    // 0: the session's start
        double speed = 5, heading = 305;    // the storm's own drift [m/s], heading from north [deg]
        double eye = 20000, wall = 24000, canopy = 200000, outer = 900000;      // [m]
        double base = 550, top = 15500, cbase = 10500, ctop = 15200;          // [m]
        double sigC = 0.025, sigS = 0.008, sigCan = 0.0018;                    // extinction [1/m]
        double bandPeriodH = 9;             // the band pattern's turn [h]
        int steps = 96;
        double detailKm = 70;               // the 3D detail fades out by this distance
        bool shadows = true, physics = true;
        bool rain = false;                  // the rain round the camera (StormRain.cpp): off until it is redone after a proper reference
        double gust = 1.0;                  // the turbulence's strength (1: as measured in hurricanes)
        int dbg = 0;                        // 1 opacity, 2 the map only, 3 the shadow alone
    } C;
    bool cfgRead = false;

    void ReadCfg()
    {
        cfgRead = true;
        FILE* f = fopen("Config\\D3D11Client\\Storm.cfg", "r");
        if (!f) { LOGC("storm: no Config\\D3D11Client\\Storm.cfg - off"); C.on = false; return; }
        char line[512], key[64], val[128];
        while (fgets(line, sizeof line, f)) {
            char* sc = strchr(line, ';'); if (sc) *sc = 0;
            if (sscanf(line, " %63[^= ] = %127s", key, val) != 2) continue;
            const double v = atof(val);
            if (!_stricmp(key, "Enabled")) C.on = v != 0;
            else if (!_stricmp(key, "Body")) C.body = val;
            else if (!_stricmp(key, "Lat")) C.lat = v;
            else if (!_stricmp(key, "Lon")) C.lon = v;
            else if (!_stricmp(key, "MJD0")) C.mjd0 = v;
            else if (!_stricmp(key, "Speed")) C.speed = v;
            else if (!_stricmp(key, "Heading")) C.heading = v;
            else if (!_stricmp(key, "EyeKm")) C.eye = v * 1e3;
            else if (!_stricmp(key, "EyewallKm")) C.wall = v * 1e3;
            else if (!_stricmp(key, "CanopyKm")) C.canopy = v * 1e3;
            else if (!_stricmp(key, "OuterKm")) C.outer = v * 1e3;
            else if (!_stricmp(key, "Base")) C.base = v;
            else if (!_stricmp(key, "Top")) C.top = v;
            else if (!_stricmp(key, "CanopyBase")) C.cbase = v;
            else if (!_stricmp(key, "CanopyTop")) C.ctop = v;
            else if (!_stricmp(key, "ExtConvective")) C.sigC = v;
            else if (!_stricmp(key, "ExtStratiform")) C.sigS = v;
            else if (!_stricmp(key, "ExtCanopy")) C.sigCan = v;
            else if (!_stricmp(key, "BandTurnHours")) C.bandPeriodH = std::max(0.5, v);
            else if (!_stricmp(key, "Steps")) C.steps = std::clamp((int)v, 24, 192);
            else if (!_stricmp(key, "DetailKm")) C.detailKm = std::max(1.0, v);
            else if (!_stricmp(key, "Shadows")) C.shadows = v != 0;
            else if (!_stricmp(key, "Physics")) C.physics = v != 0;
            else if (!_stricmp(key, "Rain")) C.rain = v != 0;
            else if (!_stricmp(key, "Gust")) C.gust = std::max(0.0, v);
            else if (!_stricmp(key, "Debug")) C.dbg = (int)v;
        }
        fclose(f);
        double FxTestNum(const char* key);
        if (int d = (int)FxTestNum("StormDbg")) C.dbg = d;          // a test run's Client.cfg key
        if (FxTestNum("StormOff") > 0.5) C.on = false;              // a test run's A/B: the storm off
        if (FxTestNum("StormRain") > 0.5) C.rain = true;            // a test run: the rain on (Storm.cfg keeps it off for now)
        if (FxTestNum("StormRainOff") > 0.5) C.rain = false;        // a test run's A/B: the storm without its rain
        if (FxTestNum("StormShadowOff") > 0.5) C.shadows = false;   // ... without the clouds' shadow pass
        LOGC("storm: %s over %s at %.2f %.2f, eye %.0f km, canopy %.0f km, outer %.0f km, %d steps, physics %s",
             C.on ? "on" : "off", C.body.c_str(), C.lat, C.lon, C.eye / 1e3, C.canopy / 1e3, C.outer / 1e3, C.steps, C.physics ? "on" : "off");
    }

    // ---- GPU resources
    ID3D11Device* sDev = nullptr;
    bool sTried = false, sNoiseDone = false;
    ComPtr<ID3D11VertexShader> sVS;
    ComPtr<ID3D11PixelShader> sMapPS, sMarchPS, sCompPS, sShadowPS, sTempPS;
    ComPtr<ID3D11Texture2D> hTex[2], hTexT[2]; ComPtr<ID3D11RenderTargetView> hRTV[2], hRTVT[2]; ComPtr<ID3D11ShaderResourceView> hSRV[2], hSRVT[2];
    UINT hCur = 0;
    ComPtr<ID3D11ComputeShader> sShapeCS, sDetailCS;
    ComPtr<ID3D11Buffer> sCB;
    ComPtr<ID3D11Texture3D> sShape, sDetail; ComPtr<ID3D11ShaderResourceView> sShapeSRV, sDetailSRV;
    ComPtr<ID3D11Texture2D> sMap; ComPtr<ID3D11RenderTargetView> sMapRTV; ComPtr<ID3D11ShaderResourceView> sMapSRV;
    ComPtr<ID3D11Texture2D> sMapCpu;    // mip 3 of the map for the physics
    ComPtr<ID3D11Texture2D> sTex, sTexT; ComPtr<ID3D11RenderTargetView> sRTV, sRTVT; ComPtr<ID3D11ShaderResourceView> sSRV, sSRVT;
    UINT sW = 0, sH = 0;
    ComPtr<ID3D11BlendState> sBlendComp, sBlendMul;
    ComPtr<ID3D11SamplerState> sTri;    // trilinear, wrapping: the 3D noises (the client's anisotropic one is for 2D)
    // the storm's own GPU time (shadow, march, temporal + composite), logged every 10 s
    ComPtr<ID3D11Query> qDis[4], qT[4][5]; bool qOk = false; UINT qFrame = 0; double gpuMs[4] = {}; double qLog = 0;
    double cpuMs[3] = {};      // CPU time: shadow pass, clouds pass, physics (smoothed)
    struct CpuTimer { double& acc; LARGE_INTEGER t0; CpuTimer(double& a) : acc(a) { QueryPerformanceCounter(&t0); }
        ~CpuTimer() { LARGE_INTEGER t1, f; QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f); acc = 0.95 * acc + 0.05 * (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart; } };
    void Stamp(ID3D11DeviceContext* ctx, int k) { if (qOk) ctx->End(qT[qFrame % 4][k].Get()); }
    const UINT MAP_N = 2048, CPU_N = MAP_N >> 3;
    double mapTime = -1e30; UINT mapFrame = 0, frame = 0;
    bool cpuPending = false;

    // ---- the storm's state this frame (planet frame)
    struct State {
        OBJHANDLE hP = nullptr; double R = 0;
        VECTOR3 c, e, n;            // centre, east, north (unit, planet frame)
        double t = 0;               // seconds since mjd0
        double hs = 1;              // +1 north
    } S;
    std::vector<uint8_t> cpuMap;    // CPU_N^2 RGBA, the map's mip 3
    double cpuHalf = 0;

    bool StateNow(OBJHANDLE hP)
    {
        if (!cfgRead) ReadCfg();
        if (!C.on || !hP) return false;
        char nm[64] = ""; oapiGetObjectName(hP, nm, 63);
        if (_stricmp(nm, C.body.c_str())) return false;
        const double mjd = oapiGetSimMJD();
        // MJD0 = 0: the storm stands at Lat/Lon when the session starts. The start is taken afresh each session (the client
        // stays loaded between them: a second scenario at another date drifted the storm by the dates' difference)
        static double autoT0 = -1, lastMjd = -1; static bool autoMode = C.mjd0 <= 0;
        if (autoMode && (autoT0 < 0 || mjd < lastMjd - 1e-6 || mjd > lastMjd + 1.0 || oapiGetSimTime() < 1.0 && fabs(mjd - autoT0) > 1e-4)) { autoT0 = mjd; C.mjd0 = mjd; }
        lastMjd = mjd;
        S.hP = hP; S.R = oapiGetSize(hP);
        S.t = (mjd - C.mjd0) * 86400.0;
        VECTOR3 c0, ce, cn;
        oapiEquToLocal(hP, C.lon * RAD, C.lat * RAD, 1.0, &c0);
        oapiEquToLocal(hP, (C.lon + 0.01) * RAD, C.lat * RAD, 1.0, &ce);
        oapiEquToLocal(hP, C.lon * RAD, (C.lat + 0.01) * RAD, 1.0, &cn);
        c0 = c0 / length(c0);
        VECTOR3 e0 = ce - c0; e0 = e0 - c0 * dotp(e0, c0); e0 = e0 / length(e0);
        VECTOR3 n0 = cn - c0; n0 = n0 - c0 * dotp(n0, c0); n0 = n0 / length(n0);
        // the drift: along the great circle of its heading, a few days' worth at most
        const double az = C.heading * RAD;
        const VECTOR3 dir = n0 * cos(az) + e0 * sin(az);
        const double del = std::clamp(C.speed * S.t, -2.0e6, 2.0e6) / S.R;
        S.c = c0 * cos(del) + dir * sin(del); S.c = S.c / length(S.c);
        // east and north again at the moved centre
        double lng, lt, rad;
        oapiLocalToEqu(hP, S.c * S.R, &lng, &lt, &rad);
        oapiEquToLocal(hP, lng + 0.0002, lt, 1.0, &ce);
        oapiEquToLocal(hP, lng, lt + 0.0002, 1.0, &cn);
        VECTOR3 cc; oapiEquToLocal(hP, lng, lt, 1.0, &cc); cc = cc / length(cc);
        S.e = ce - cc; S.e = S.e - S.c * dotp(S.e, S.c); S.e = S.e / length(S.e);
        S.n = cn - cc; S.n = S.n - S.c * dotp(S.n, S.c); S.n = S.n / length(S.n);
        S.hs = lt >= 0 ? 1.0 : -1.0;
        return true;
    }

    struct CB {
        XMFLOAT4X4 inv;
        XMFLOAT4 r0, r1, r2, cen, east, north, cenG, shape, hgt, sig, anim, p, size;
        XMFLOAT4X4 prevVP;
        XMFLOAT4 pr0, pr1, pr2, tmp, ext;
    };
    // last frame, for the temporal accumulation
    struct Prev { bool ok = false; XMFLOAT4X4 vp; MATRIX3 Rp; VECTOR3 ppRel; VECTOR3 cp; double t = 0; } prev;

    // the shaders are compiled on a worker thread (a cold shader cache took ~20 s, and the first frame waited for it);
    // the storm shows when they are ready
    struct Blobs { ComPtr<ID3DBlob> vs, pm, pk, pc, ps, pt, c1, c2; };
    std::future<Blobs> sCompile;
    bool Init(D3D11Client* gc, ID3D11Device* dev)
    {
        if (!sCompile.valid()) {
            sCompile = std::async(std::launch::async, [gc]() {
                Blobs b;
                b.vs = gc->CompileShaderFile("storm.hlsl", "VS", "vs_5_0");
                b.pm = gc->CompileShaderFile("storm.hlsl", "PSMap", "ps_5_0");
                b.pk = gc->CompileShaderFile("storm.hlsl", "PSMarch", "ps_5_0");
                b.pc = gc->CompileShaderFile("storm.hlsl", "PSComposite", "ps_5_0");
                b.ps = gc->CompileShaderFile("storm.hlsl", "PSShadow", "ps_5_0");
                b.pt = gc->CompileShaderFile("storm.hlsl", "PSTemporal", "ps_5_0");
                b.c1 = gc->CompileShaderFile("storm_noise.hlsl", "CSShape", "cs_5_0");
                b.c2 = gc->CompileShaderFile("storm_noise.hlsl", "CSDetail", "cs_5_0");
                return b;
            });
            return false;
        }
        if (sCompile.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
        sTried = true;
        Blobs B = sCompile.get();
        auto& vs = B.vs; auto& pm = B.pm; auto& pk = B.pk; auto& pc = B.pc; auto& ps = B.ps; auto& pt = B.pt; auto& c1 = B.c1; auto& c2 = B.c2;
        if (!vs || !pm || !pk || !pc || !ps || !pt || !c1 || !c2) { LOGC("storm: shaders FAILED"); return false; }
        dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &sVS);
        dev->CreatePixelShader(pm->GetBufferPointer(), pm->GetBufferSize(), nullptr, &sMapPS);
        dev->CreatePixelShader(pk->GetBufferPointer(), pk->GetBufferSize(), nullptr, &sMarchPS);
        dev->CreatePixelShader(pc->GetBufferPointer(), pc->GetBufferSize(), nullptr, &sCompPS);
        dev->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &sShadowPS);
        dev->CreatePixelShader(pt->GetBufferPointer(), pt->GetBufferSize(), nullptr, &sTempPS);
        dev->CreateComputeShader(c1->GetBufferPointer(), c1->GetBufferSize(), nullptr, &sShapeCS);
        dev->CreateComputeShader(c2->GetBufferPointer(), c2->GetBufferSize(), nullptr, &sDetailCS);
        D3D11_BUFFER_DESC bd = { (UINT)sizeof(CB), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&bd, nullptr, &sCB);
        // the noises
        auto mk3 = [&](UINT n, ComPtr<ID3D11Texture3D>& t, ComPtr<ID3D11ShaderResourceView>& srv) {
            // with its full mip chain: sampled by the pixel's footprint (one level only, the far cloud read single 70 m voxels: sand)
            D3D11_TEXTURE3D_DESC td = { n, n, n, 0, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET, 0, D3D11_RESOURCE_MISC_GENERATE_MIPS };
            if (FAILED(dev->CreateTexture3D(&td, nullptr, &t))) return false;
            return SUCCEEDED(dev->CreateShaderResourceView(t.Get(), nullptr, &srv));
        };
        if (!mk3(128, sShape, sShapeSRV) || !mk3(32, sDetail, sDetailSRV)) { LOGC("storm: noise textures FAILED"); return false; }
        // the weather map, with mips (the view from orbit samples the coarser ones)
        D3D11_TEXTURE2D_DESC md = { MAP_N, MAP_N, 0, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT,
                                    D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_GENERATE_MIPS };
        if (FAILED(dev->CreateTexture2D(&md, nullptr, &sMap))) { LOGC("storm: map FAILED"); return false; }
        D3D11_RENDER_TARGET_VIEW_DESC rv = {}; rv.Format = md.Format; rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D; rv.Texture2D.MipSlice = 0;
        dev->CreateRenderTargetView(sMap.Get(), &rv, &sMapRTV);
        dev->CreateShaderResourceView(sMap.Get(), nullptr, &sMapSRV);
        D3D11_TEXTURE2D_DESC cd = { CPU_N, CPU_N, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0 };
        dev->CreateTexture2D(&cd, nullptr, &sMapCpu);
        // blends: the march over the scene (premultiplied: dest * T + L), the shadow (dest * factor)
        D3D11_BLEND_DESC bl = {}; auto& t = bl.RenderTarget[0];
        t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_SRC_ALPHA; t.BlendOp = D3D11_BLEND_OP_ADD;
        t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
        dev->CreateBlendState(&bl, &sBlendComp);
        t.SrcBlend = D3D11_BLEND_ZERO; t.DestBlend = D3D11_BLEND_SRC_COLOR;
        dev->CreateBlendState(&bl, &sBlendMul);
        D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        dev->CreateSamplerState(&sd, &sTri);
        D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 }, qt = { D3D11_QUERY_TIMESTAMP, 0 };
        qOk = true;
        for (int i = 0; i < 4; i++) { qOk &= SUCCEEDED(dev->CreateQuery(&qd, &qDis[i])); for (int j = 0; j < 5; j++) qOk &= SUCCEEDED(dev->CreateQuery(&qt, &qT[i][j])); }
        LOGC("storm: ready (map %ux%u, noise 128^3 + 32^3)", MAP_N, MAP_N);
        return true;
    }

    void Release()
    {
        if (sCompile.valid()) sCompile.wait();      // a compile still running finishes first (its result is dropped)
        sCompile = std::future<Blobs>();
        sTried = false; sNoiseDone = false; sVS.Reset(); sMapPS.Reset(); sMarchPS.Reset(); sCompPS.Reset(); sShadowPS.Reset();
        sShapeCS.Reset(); sDetailCS.Reset(); sCB.Reset(); sShape.Reset(); sDetail.Reset(); sShapeSRV.Reset(); sDetailSRV.Reset();
        sMap.Reset(); sMapRTV.Reset(); sMapSRV.Reset(); sMapCpu.Reset(); sTex.Reset(); sTexT.Reset(); sRTV.Reset(); sRTVT.Reset();
        sSRV.Reset(); sSRVT.Reset(); sW = sH = 0; sTempPS.Reset(); prev.ok = false;
        for (int i = 0; i < 2; i++) { hTex[i].Reset(); hTexT[i].Reset(); hRTV[i].Reset(); hRTVT[i].Reset(); hSRV[i].Reset(); hSRVT[i].Reset(); } sBlendComp.Reset(); sBlendMul.Reset(); sTri.Reset(); mapTime = -1e30; cpuPending = false;
    }

    // the constants for this frame
    void Constants(ID3D11DeviceContext* ctx, const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj, UINT w, UINT h)
    {
        VECTOR3 pp; oapiGetGlobalPos(S.hP, &pp);
        MATRIX3 Rp; oapiGetRotationMatrix(S.hP, &Rp);
        const VECTOR3 camPl = tmul(Rp, cp - pp);
        const VECTOR3 cG = mul(Rp, S.c);
        XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
        CB k = {};
        XMStoreFloat4x4(&k.inv, XMMatrixTranspose(XMMatrixInverse(nullptr, V * proj)));
        k.r0 = { (float)Rp.m11, (float)Rp.m21, (float)Rp.m31, (float)(camPl.x / 1000.0) };
        k.r1 = { (float)Rp.m12, (float)Rp.m22, (float)Rp.m32, (float)(camPl.y / 1000.0) };
        k.r2 = { (float)Rp.m13, (float)Rp.m23, (float)Rp.m33, (float)(camPl.z / 1000.0) };
        const double half = C.outer * 1.1;
        k.cen = { (float)S.c.x, (float)S.c.y, (float)S.c.z, (float)S.R };
        k.east = { (float)S.e.x, (float)S.e.y, (float)S.e.z, (float)half };
        k.north = { (float)S.n.x, (float)S.n.y, (float)S.n.z, (float)fmod(S.t, 3.0e4) };
        k.cenG = { (float)cG.x, (float)cG.y, (float)cG.z, (float)cos(C.outer * 1.12 / S.R) };
        k.shape = { (float)C.eye, (float)C.wall, (float)C.canopy, (float)C.outer };
        k.hgt = { (float)C.base, (float)C.top, (float)C.cbase, (float)C.ctop };
        k.sig = { (float)C.sigC, (float)C.sigS, (float)C.sigCan, (float)S.hs };
        // the bands turn as a whole; the small structure flows with the vortex in two phases of Tc, cross-faded
        const double Tc = 1800.0;     // the flow phases' period (shorter pulsed visibly at a high time acceleration)
        const double fa = fmod(S.t, Tc) / Tc, fb = fmod(S.t + Tc * 0.5, Tc) / Tc;
        k.anim = { (float)fmod(S.t * 2 * PI / (C.bandPeriodH * 3600.0), 2 * PI), (float)((fa - 0.5) * Tc), (float)((fb - 0.5) * Tc), (float)fabs(2 * fa - 1) };
        k.p = { (float)C.steps, (float)(frame % 1024), (float)C.dbg, (float)(C.detailKm * 1e3) };
        k.size = { (float)w, (float)h, 1.0f / std::max(1u, w), 1.0f / std::max(1u, h) };
        // last frame (a jump of the camera - a new target, a time warp - starts the history afresh)
        const bool hist = prev.ok && length(cp - prev.cp) < 5000.0;
        if (hist) {
            k.prevVP = prev.vp;
            const MATRIX3& Q = prev.Rp;
            k.pr0 = { (float)Q.m11, (float)Q.m12, (float)Q.m13, (float)prev.ppRel.x };
            k.pr1 = { (float)Q.m21, (float)Q.m22, (float)Q.m23, (float)prev.ppRel.y };
            k.pr2 = { (float)Q.m31, (float)Q.m32, (float)Q.m33, (float)prev.ppRel.z };
        }
        static const float off[4][2] = { { 0, 0 }, { 1, 1 }, { 1, 0 }, { 0, 1 } };
        k.tmp = { hist ? 1.0f : 0.0f, 0.08f, off[frame & 3][0], off[frame & 3][1] };
        k.ext = { hist ? (float)std::clamp(S.t - prev.t, 0.0, 600.0) : 0.0f, 0, 0, 0 };
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(ctx->Map(sCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { memcpy(ms.pData, &k, sizeof k); ctx->Unmap(sCB.Get(), 0); }
        cpuHalf = half;
    }
    void KeepPrev(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
    {
        VECTOR3 pp; oapiGetGlobalPos(S.hP, &pp);
        oapiGetRotationMatrix(S.hP, &prev.Rp);
        prev.ppRel = pp - cp; prev.cp = cp; prev.t = S.t;
        XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
        XMStoreFloat4x4(&prev.vp, XMMatrixTranspose(V * proj));
        prev.ok = true;
    }

    // the noises once, the map when the storm has moved on (half a sim second, or a couple of seconds of real time)
    void Prepare(D3D11Client* gc, ID3D11DeviceContext* ctx)
    {
        static double lastSys = -1;      // once a frame (the shadow pass and the clouds both ask)
        const double sys = oapiGetSysTime();
        if (sys == lastSys) return;
        lastSys = sys;
        frame++;
        if (!sNoiseDone) {
            ctx->CSSetShader(sShapeCS.Get(), nullptr, 0);
            ComPtr<ID3D11UnorderedAccessView> u1, u2;
            gc->Dev()->CreateUnorderedAccessView(sShape.Get(), nullptr, &u1);
            gc->Dev()->CreateUnorderedAccessView(sDetail.Get(), nullptr, &u2);
            ctx->CSSetUnorderedAccessViews(0, 1, u1.GetAddressOf(), nullptr);
            ctx->Dispatch(128 / 4, 128 / 4, 128 / 4);
            ctx->CSSetShader(sDetailCS.Get(), nullptr, 0);
            ctx->CSSetUnorderedAccessViews(0, 1, u2.GetAddressOf(), nullptr);
            ctx->Dispatch(32 / 4, 32 / 4, 32 / 4);
            ID3D11UnorderedAccessView* nu0 = nullptr; ctx->CSSetUnorderedAccessViews(0, 1, &nu0, nullptr);
            ctx->GenerateMips(sShapeSRV.Get()); ctx->GenerateMips(sDetailSRV.Get());
            ID3D11UnorderedAccessView* nu = nullptr; ctx->CSSetUnorderedAccessViews(0, 1, &nu, nullptr);
            ctx->CSSetShader(nullptr, nullptr, 0);
            sNoiseDone = true;
        }
        // the copy for the physics, read when the GPU is done with it
        if (cpuPending) {
            D3D11_MAPPED_SUBRESOURCE ms;
            if (ctx->Map(sMapCpu.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &ms) == S_OK) {
                cpuMap.resize(CPU_N * CPU_N * 4);
                for (UINT y = 0; y < CPU_N; y++) memcpy(&cpuMap[y * CPU_N * 4], (uint8_t*)ms.pData + y * ms.RowPitch, CPU_N * 4);
                ctx->Unmap(sMapCpu.Get(), 0);
                cpuPending = false;
            }
        }
        // the map follows the storm smoothly: every frame while time runs faster (in steps it jerked), else every half sim second
        const bool warp = oapiGetTimeAcceleration() > 1.5;
        if (!warp && fabs(S.t - mapTime) < 0.5 && frame - mapFrame < 240) return;
        if (S.t == mapTime && mapTime > -1e29) return;
        mapTime = S.t; mapFrame = frame;
        ComPtr<ID3D11RenderTargetView> oR; ComPtr<ID3D11DepthStencilView> oD; ctx->OMGetRenderTargets(1, &oR, &oD);
        UINT nvp = 1; D3D11_VIEWPORT ovp; ctx->RSGetViewports(&nvp, &ovp);
        ID3D11ShaderResourceView* nul[6] = {}; ctx->PSSetShaderResources(0, 6, nul);
        ctx->OMSetRenderTargets(1, sMapRTV.GetAddressOf(), nullptr);
        D3D11_VIEWPORT mv = { 0, 0, (float)MAP_N, (float)MAP_N, 0, 1 }; ctx->RSSetViewports(1, &mv);
        ctx->OMSetBlendState(gc->bsOpaque.Get(), nullptr, ~0u);
        ctx->OMSetDepthStencilState(gc->dsRead.Get(), 0);
        ctx->RSSetState(gc->rsNone.Get());
        ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(sVS.Get(), nullptr, 0); ctx->PSSetShader(sMapPS.Get(), nullptr, 0);
        ctx->PSSetConstantBuffers(1, 1, sCB.GetAddressOf());
        ctx->Draw(3, 0);
        ctx->OMSetRenderTargets(1, oR.GetAddressOf(), oD.Get());
        if (nvp) ctx->RSSetViewports(1, &ovp);
        ctx->GenerateMips(sMapSRV.Get());
        if (sMapCpu && !cpuPending) { ctx->CopySubresourceRegion(sMapCpu.Get(), 0, 0, 0, 0, sMap.Get(), 3, nullptr); cpuPending = true; }
    }

    bool Ready(D3D11Client* gc)
    {
        if (sDev != gc->Dev()) { Release(); sDev = gc->Dev(); }
        if (!sTried) Init(gc, gc->Dev());
        return sVS && sMarchPS && sCompPS && sTempPS && sShadowPS && sMapPS && sCB && sShapeSRV && sDetailSRV && sMapSRV;
    }

    // is the camera near enough for the storm to matter (in the air or in orbit over that side of the planet)
    bool InView(const VECTOR3& cp)
    {
        VECTOR3 pp; oapiGetGlobalPos(S.hP, &pp);
        MATRIX3 Rp; oapiGetRotationMatrix(S.hP, &Rp);
        const VECTOR3 cg = mul(Rp, S.c);
        const VECTOR3 rc = cp - pp;
        const double d = length(rc);
        if (d > 2.0e8) return false;
        // the storm's side of the planet: the cap's rim must be above the camera's horizon
        const double cosHz = S.R / d;     // the horizon's angle from the camera's nadir, as seen from the centre
        const double ang = acos(std::clamp(dotp(rc / d, cg), -1.0, 1.0));
        return ang < acos(std::min(cosHz, 1.0)) + C.outer * 1.15 / S.R;
    }
}

// ------------------------------------------------------------------------------------------------ the shadow
void D3D11Client::RenderStormShadow(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    CpuTimer tm(cpuMs[0]);
    if (!StateNow(oapiCameraProxyGbody()) || !C.shadows) return;
    if (!InView(cp) || !Ready(this)) return;
    if (qOk) { ctx->Begin(qDis[qFrame % 4].Get()); Stamp(ctx.Get(), 0); }
    Constants(ctx.Get(), cp, cr, proj, sW, sH);
    Prepare(this, ctx.Get());
    ResolveDepth();
    ComPtr<ID3D11RenderTargetView> oR; ComPtr<ID3D11DepthStencilView> oD; ctx->OMGetRenderTargets(1, &oR, &oD);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
    D3D11_VIEWPORT vp = { 0, 0, (float)RW, (float)RH, 0, 1 }; ctx->RSSetViewports(1, &vp);
    ctx->OMSetBlendState(sBlendMul.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    FxBindLight(this);
    ctx->VSSetShader(sVS.Get(), nullptr, 0); ctx->PSSetShader(sShadowPS.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, sCB.GetAddressOf());
    ID3D11ShaderResourceView* sr[4] = { sShapeSRV.Get(), sDetailSRV.Get(), sMapSRV.Get(), depthSRV.Get() };
    ctx->PSSetShaderResources(0, 4, sr);
    ID3D11SamplerState* ss[2] = { sTri.Get(), sampClamp.Get() };
    ctx->PSSetSamplers(0, 2, ss);
    ctx->Draw(3, 0);
    Stamp(ctx.Get(), 1);
    ID3D11ShaderResourceView* nul[4] = {}; ctx->PSSetShaderResources(0, 4, nul);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, oR.GetAddressOf(), oD.Get());
}

// ------------------------------------------------------------------------------------------------ the clouds
void D3D11Client::RenderStorm(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    CpuTimer tm(cpuMs[1]);
    if (!StateNow(oapiCameraProxyGbody())) return;
    if (!InView(cp) || !Ready(this)) return;
    const UINT w = std::max(1u, RW / 2), h = std::max(1u, RH / 2);
    if (!sTex || w != sW || h != sH) {
        sTex.Reset(); sTexT.Reset(); sRTV.Reset(); sRTVT.Reset(); sSRV.Reset(); sSRVT.Reset();
        // the march: a quarter of the half-res texels a frame (one of each 2 x 2 block, in turn)
        D3D11_TEXTURE2D_DESC td = { (w + 1) / 2, (h + 1) / 2, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &sTex))) return;
        td.Format = DXGI_FORMAT_R32G32_FLOAT;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &sTexT))) return;
        dev->CreateRenderTargetView(sTex.Get(), nullptr, &sRTV); dev->CreateShaderResourceView(sTex.Get(), nullptr, &sSRV);
        dev->CreateRenderTargetView(sTexT.Get(), nullptr, &sRTVT); dev->CreateShaderResourceView(sTexT.Get(), nullptr, &sSRVT);
        td.Width = w; td.Height = h;
        for (int i = 0; i < 2; i++) {
            td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            if (FAILED(dev->CreateTexture2D(&td, nullptr, &hTex[i]))) return;
            td.Format = DXGI_FORMAT_R32G32_FLOAT;
            if (FAILED(dev->CreateTexture2D(&td, nullptr, &hTexT[i]))) return;
            dev->CreateRenderTargetView(hTex[i].Get(), nullptr, &hRTV[i]); dev->CreateShaderResourceView(hTex[i].Get(), nullptr, &hSRV[i]);
            dev->CreateRenderTargetView(hTexT[i].Get(), nullptr, &hRTVT[i]); dev->CreateShaderResourceView(hTexT[i].Get(), nullptr, &hSRVT[i]);
        }
        sW = w; sH = h; prev.ok = false;
    }
    Constants(ctx.Get(), cp, cr, proj, w, h);
    Prepare(this, ctx.Get());
    ResolveDepth();
    ComPtr<ID3D11RenderTargetView> oR; ComPtr<ID3D11DepthStencilView> oD; ctx->OMGetRenderTargets(1, &oR, &oD);
    UINT nvp = 1; D3D11_VIEWPORT ovp; ctx->RSGetViewports(&nvp, &ovp);
    // the march at half resolution
    const bool shadowStamped = C.shadows;
    if (qOk && !shadowStamped) { ctx->Begin(qDis[qFrame % 4].Get()); Stamp(ctx.Get(), 0); Stamp(ctx.Get(), 1); }
    Stamp(ctx.Get(), 2);
    ID3D11RenderTargetView* rt[2] = { sRTV.Get(), sRTVT.Get() };
    ctx->OMSetRenderTargets(2, rt, nullptr);
    D3D11_VIEWPORT qv = { 0, 0, (float)((w + 1) / 2), (float)((h + 1) / 2), 0, 1 }; ctx->RSSetViewports(1, &qv);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    FxBindLight(this);
    ctx->VSSetShader(sVS.Get(), nullptr, 0); ctx->PSSetShader(sMarchPS.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, sCB.GetAddressOf());
    ID3D11ShaderResourceView* sr[4] = { sShapeSRV.Get(), sDetailSRV.Get(), sMapSRV.Get(), depthSRV.Get() };
    ctx->PSSetShaderResources(0, 4, sr);
    ID3D11SamplerState* ss[2] = { sTri.Get(), sampClamp.Get() };
    ctx->PSSetSamplers(0, 2, ss);
    ctx->Draw(3, 0);
    Stamp(ctx.Get(), 3);
    // the temporal accumulation: this frame + last frame's history -> the other history buffer
    const UINT hn = hCur ^ 1;
    ID3D11RenderTargetView* rth[2] = { hRTV[hn].Get(), hRTVT[hn].Get() };
    ctx->OMSetRenderTargets(2, rth, nullptr);
    D3D11_VIEWPORT hv = { 0, 0, (float)w, (float)h, 0, 1 }; ctx->RSSetViewports(1, &hv);
    ctx->PSSetShader(sTempPS.Get(), nullptr, 0);
    ID3D11ShaderResourceView* st[4] = { sSRV.Get(), sSRVT.Get(), hSRV[hCur].Get(), hSRVT[hCur].Get() };
    ctx->PSSetShaderResources(4, 4, st);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul4[4] = {}; ctx->PSSetShaderResources(4, 4, nul4);
    hCur = hn;
    KeepPrev(cp, cr, proj);
    // over the scene, full resolution
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), nullptr);
    D3D11_VIEWPORT fv = { 0, 0, (float)RW, (float)RH, 0, 1 }; ctx->RSSetViewports(1, &fv);
    ctx->OMSetBlendState(sBlendComp.Get(), nullptr, ~0u);
    ctx->PSSetShader(sCompPS.Get(), nullptr, 0);
    ID3D11ShaderResourceView* sc[2] = { hSRV[hCur].Get(), hSRVT[hCur].Get() };
    ctx->PSSetShaderResources(4, 2, sc);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul[8] = {}; ctx->PSSetShaderResources(0, 8, nul);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, oR.GetAddressOf(), oD.Get());
    if (nvp) ctx->RSSetViewports(1, &ovp);
    static bool logged = false;
    if (!logged) { logged = true; LOGC("storm: drawn (half res %ux%u)", w, h); }
    if (qOk) {
        Stamp(ctx.Get(), 4); ctx->End(qDis[qFrame % 4].Get());
        if (qFrame >= 3) {      // the frame 3 back is done by now
            const UINT f = (qFrame - 3) % 4;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj; UINT64 t[5]; bool ok = ctx->GetData(qDis[f].Get(), &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !dj.Disjoint;
            for (int j = 0; j < 5 && ok; j++) ok = ctx->GetData(qT[f][j].Get(), &t[j], 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            if (ok) for (int j = 0; j < 4; j++) gpuMs[j] = 0.9 * gpuMs[j] + 0.1 * (double)(t[j + 1] - t[j]) * 1000.0 / dj.Frequency;
        }
        qFrame++;
        const double now = oapiGetSysTime();
        if (now - qLog > 10) { qLog = now; LOGC("storm: GPU ms shadow %.2f, march %.2f, temporal + composite %.2f (the air between shadow and march: %.2f) | CPU ms shadow %.2f, clouds %.2f, physics %.2f", gpuMs[0], gpuMs[2], gpuMs[3], gpuMs[1], cpuMs[0], cpuMs[1], cpuMs[2]); }
    }
}

// ------------------------------------------------------------------------------------------------ flying through
// The turbulence a vessel meets in the cloud: an Ornstein-Uhlenbeck gust per axis (the correlation length ~250 m, as the
// Dryden model's in convective cloud), its strength from where the vessel is in the storm - severe in the eyewall
// (sigma_w ~10-15 m/s, the NOAA P-3 flights), moderate in the rain bands, light in the canopy, calm in the eye -
// with the convective cores' mean updraft. The gust becomes a force as the change of the air's momentum flux on the
// vessel's cross-section (0.5 rho V A |g|), applied a little off the centre of mass: the airframe also rolls in it.
namespace {
    struct Gust { VECTOR3 g = { 0, 0, 0 }; double side = 0; bool in = false; };
    std::map<OBJHANDLE, Gust> gusts;
    std::mt19937 rng(12345);
    bool preStepSeen = false;
    double lastSimT = -1;

    float CpuMap(double x, double y, int ch)
    {
        const double u = (x / (2 * cpuHalf) + 0.5) * CPU_N - 0.5, v = ((1 - (y / (2 * cpuHalf) + 0.5))) * CPU_N - 0.5;
        if (u < 0 || v < 0 || u > CPU_N - 1 || v > CPU_N - 1) return 0;
        const int i = (int)u, j = (int)v, i1 = std::min(i + 1, (int)CPU_N - 1), j1 = std::min(j + 1, (int)CPU_N - 1);
        const double fu = u - i, fv = v - j;
        auto at = [&](int a, int b) { return cpuMap[(b * CPU_N + a) * 4 + ch] / 255.0; };
        return (float)((at(i, j) * (1 - fu) + at(i1, j) * fu) * (1 - fv) + (at(i, j1) * (1 - fu) + at(i1, j1) * fu) * fv);
    }

    // how much of the storm's turbulence there is at P (planet frame), 0..1, and the mean updraft [m/s]
    double Intensity(const VECTOR3& P, double& updraft)
    {
        updraft = 0;
        const double r = length(P), h = r - S.R;
        const VECTOR3 u = P / r;
        const double c = dotp(u, S.c), s = length(crossp(u, S.c));
        const double k = S.R * atan2(s, c) / std::max(s, 1e-12);
        const double x = dotp(u, S.e) * k, y = dotp(u, S.n) * k;
        const double rxy = sqrt(x * x + y * y);
        if (rxy > C.outer * 1.05) return 0;
        const double cov = CpuMap(x, y, 0), top = std::max(CpuMap(x, y, 1) * C.top, C.base + 300), conv = CpuMap(x, y, 2), can = CpuMap(x, y, 3);
        const double eyeR = C.eye * (1 + 0.9 * std::clamp(h / C.top, 0.0, 1.0));
        const double eyeCut = h < 2300 ? 1 : std::clamp((rxy - eyeR * 0.9) / (eyeR * 0.2), 0.0, 1.0);
        double I = 0;
        const double hf = (h - C.base) / (top - C.base);
        if (hf > 0 && hf < 1) {
            I += cov * (0.25 + 0.75 * conv) * sqrt(sin(PI * hf));
            updraft += 7.0 * cov * conv * sin(PI * hf);     // the cores' mean updraft, strongest mid-cloud
        }
        else if (hf <= 0 && cov > 0.3 && conv > 0.5) {
            I += 0.4 * cov * conv;                           // the rain's downdrafts and gust fronts under the cells
            updraft -= 3.0 * cov * conv;
        }
        const double hc = (h - C.cbase) / (C.ctop - C.cbase);
        if (hc > 0 && hc < 1) I += 0.2 * can;
        updraft *= eyeCut;
        return std::clamp(I * eyeCut, 0.0, 1.0);
    }

    void Physics(double simdt)
    {
        if (!C.on || !C.physics || cpuMap.empty() || simdt <= 0 || simdt > 0.5) return;
        OBJHANDLE hP = oapiGetGbodyByName(const_cast<char*>(C.body.c_str()));
        if (!hP || !StateNow(hP)) return;
        VECTOR3 pp; oapiGetGlobalPos(hP, &pp);
        MATRIX3 Rp; oapiGetRotationMatrix(hP, &Rp);
        std::normal_distribution<double> N01(0.0, 1.0);
        for (DWORD i = 0; i < oapiGetVesselCount(); i++) {
            OBJHANDLE hv = oapiGetVesselByIndex(i);
            VESSEL* v = oapiGetVesselInterface(hv);
            if (!v || v->GetSurfaceRef() != hP || v->GroundContact()) { gusts.erase(hv); continue; }
            if (v->GetAltitude() > C.ctop + 500) { gusts.erase(hv); continue; }
            VECTOR3 gp; v->GetGlobalPos(gp);
            const VECTOR3 P = tmul(Rp, gp - pp);
            double w0; const double I = Intensity(P, w0);
            Gust& G = gusts[hv];
            const double V = v->GetAirspeed(), rho = v->GetAtmDensity();
            const double sig = C.gust * 13.0 * I;                         // sigma of the gust [m/s]
            const double tau = 250.0 / std::max(V, 20.0);                 // its correlation time [s]
            const double a = std::min(simdt / tau, 1.0), q = sig * sqrt(2 * a);
            G.g.x += -G.g.x * a + q * N01(rng); G.g.y += -G.g.y * a + q * N01(rng); G.g.z += -G.g.z * a + q * 1.3 * N01(rng);
            G.side += -G.side * a + sqrt(2 * a) * N01(rng);
            const bool in = I > 0.05;
            if (in != G.in) { G.in = in; LOGC("storm: %s %s the cloud (alt %.0f m, intensity %.2f)", v->GetName(), in ? "enters" : "leaves", v->GetAltitude(), I); }
            if (sig <= 0.01 && length(G.g) < 0.1 && fabs(w0) < 0.1) continue;
            // the gust in the local horizon (east, north, up) at the vessel -> global
            const VECTOR3 up = P / length(P);
            VECTOR3 e = crossp(_V(0, 1, 0), up); if (length(e) < 1e-6) e = _V(1, 0, 0); e = e / length(e);
            const VECTOR3 n = crossp(up, e);
            const VECTOR3 gPl = e * G.g.x + n * G.g.y + up * (G.g.z + w0);
            const VECTOR3 gGl = mul(Rp, gPl);
            const double size = v->GetSize(), A = size * size * 0.6;
            VECTOR3 F = gGl * (0.5 * rho * std::max(V, 5.0) * A);
            const double m = v->GetMass(), Fmax = 2.0 * 9.81 * m;
            if (length(F) > Fmax) F = F * (Fmax / length(F));
            MATRIX3 Rv; v->GetRotationMatrix(Rv);
            const VECTOR3 Fl = tmul(Rv, F);
            v->AddForce(Fl, _V(std::clamp(G.side, -2.0, 2.0) * size * 0.08, 0, 0));
        }
    }
}

// Making a scenario with a vessel in flight (a test run only, env STORM_MAKE_SCN = "file;vessel;alt m;speed m/s;heading deg;
// description"): the landed vessel is lifted, set level on its heading at that airspeed, and the state written by Orbiter
// itself (a flight state in the ecliptic frame is not something to work out by hand)
static void MakeScenario(double simt)
{
    static int stage = 0; static char buf[512] = {};
    if (stage == 0) { stage = GetEnvironmentVariableA("STORM_MAKE_SCN", buf, sizeof buf) ? 1 : 3; return; }
    if (stage >= 3) return;
    char fn[160] = "", vn[64] = "", desc[256] = ""; double alt = 0, spd = 0, hdg = 0;
    if (sscanf(buf, "%159[^;];%63[^;];%lf;%lf;%lf;%255[^\n]", fn, vn, &alt, &spd, &hdg, desc) < 5) { stage = 3; return; }
    OBJHANDLE hv = oapiGetVesselByName(vn); if (!hv) { stage = 3; return; }
    VESSEL* v = oapiGetVesselInterface(hv);
    if (stage == 1 && simt > 1.0) {
        OBJHANDLE hp = v->GetSurfaceRef();
        VECTOR3 gp, pp; v->GetGlobalPos(gp); oapiGetGlobalPos(hp, &pp);
        double lng, lat, rad; oapiGlobalToEqu(hp, gp, &lng, &lat, &rad);
        VECTOR3 g0, ge, gn; oapiEquToGlobal(hp, lng, lat, rad, &g0); oapiEquToGlobal(hp, lng + 1e-5, lat, rad, &ge); oapiEquToGlobal(hp, lng, lat + 1e-5, rad, &gn);
        VECTOR3 up = (g0 - pp) / length(g0 - pp);
        VECTOR3 east = ge - g0; east = east - up * dotp(east, up); east = east / length(east);
        VECTOR3 north = gn - g0; north = north - up * dotp(north, up); north = north / length(north);
        const VECTOR3 fwd = north * cos(hdg * RAD) + east * sin(hdg * RAD);
        VESSELSTATUS2 vs; memset(&vs, 0, sizeof vs); vs.version = 2; v->GetStatusEx(&vs);
        const double r = length(vs.rpos);
        vs.rpos = vs.rpos * ((r + alt) / r);
        vs.rvel = vs.rvel * ((r + alt) / r) + fwd * spd;
        vs.vrot = _V(0, 0, 0); vs.status = 0;
        v->DefSetStateEx(&vs);
        VECTOR3 right = crossp(up, fwd);
        MATRIX3 Rm = _M(right.x, up.x, fwd.x, right.y, up.y, fwd.y, right.z, up.z, fwd.z);
        const double det = right.x * (up.y * fwd.z - up.z * fwd.y) - up.x * (right.y * fwd.z - right.z * fwd.y) + fwd.x * (right.y * up.z - right.z * up.y);
        if (det < 0) { right = right * -1.0; Rm = _M(right.x, up.x, fwd.x, right.y, up.y, fwd.y, right.z, up.z, fwd.z); }
        v->SetRotationMatrix(Rm);
        stage = 2;
        LOGC("storm: scenario %s - %s set at %.0f m, %.0f m/s, heading %.0f", fn, vn, alt, spd, hdg);
    } else if (stage == 2 && simt > 2.0) {
        const bool ok = oapiSaveScenario(fn, desc);
        LOGC("storm: scenario %s %s (alt %.0f m, airspeed %.0f m/s)", fn, ok ? "saved" : "NOT saved", v->GetAltitude(), v->GetAirspeed());
        stage = 3;
    }
}

void StormPreStep(double simt, double simdt, double)
{
    preStepSeen = true;
    MakeScenario(simt);
    if (simt == lastSimT) return;
    lastSimT = simt;
    CpuTimer tm(cpuMs[2]);
    Physics(simdt);
}
// called every frame: the physics from here only if Orbiter never calls the client's clbkPreStep (and only once per step)
void StormFrameFallback()
{
    static int frames = 0;
    if (preStepSeen || ++frames < 60) return;
    const double t = oapiGetSimTime();
    if (t == lastSimT) return;     // paused: no force piled up for the next step
    lastSimT = t;
    Physics(oapiGetSimStep());
}

// the share of the daylight that reaches the camera under the storm's clouds (1: clear; ~0.05-0.1 under a hurricane's
// full depth), for the eye's adaptation: the column overhead from the map read back for the physics, diffused through it
// (two-stream: 1 / (1 + 0.75 tau (1 - g))), smoothed over ~1.5 s
float StormCameraLight()
{
    static float f = 1; static double lastT = 0;
    if (!C.on || cpuMap.empty() || !S.hP) return f = 1;
    OBJHANDLE hp = oapiCameraProxyGbody();
    if (hp != S.hP) return f = 1;
    VECTOR3 cp, pp; oapiCameraGlobalPos(&cp); oapiGetGlobalPos(S.hP, &pp);
    MATRIX3 Rp; oapiGetRotationMatrix(S.hP, &Rp);
    const VECTOR3 P = tmul(Rp, cp - pp);
    const double r = length(P), h = r - S.R;
    double target = 1;
    if (h < C.ctop + 500) {
        const VECTOR3 u = P / r;
        const double c = dotp(u, S.c), sn = length(crossp(u, S.c));
        const double k = S.R * atan2(sn, c) / std::max(sn, 1e-12);
        const double x = dotp(u, S.e) * k, y = dotp(u, S.n) * k;
        const double cov = CpuMap(x, y, 0), top = std::max(CpuMap(x, y, 1) * C.top, C.base + 300), conv = CpuMap(x, y, 2), can = CpuMap(x, y, 3);
        double tau = 0;
        if (h < top) tau += cov * cov * (C.sigS + (C.sigC - C.sigS) * conv) * (top - std::max(h, C.base)) * 0.35;
        if (h < C.ctop) tau += can * C.sigCan * (C.ctop - std::max(h, C.cbase)) * 0.55;
        target = 1.0 / (1.0 + 0.11 * tau);
    }
    const double t = oapiGetSysTime(), dt = lastT > 0 ? std::clamp(t - lastT, 0.0, 1.0) : 1.0;
    lastT = t;
    f = (float)(f + (target - f) * (1 - exp(-dt / 1.5)));
    return f;
}

// the rain at the camera (StormRain.cpp): its rate [mm/h] from the map read back for the physics - the convective cores
// pour (the eyewall 40-60 mm/h, the bands 10-30), the stratiform rain drizzles, the eye is dry - and the storm's wind there
// (global frame): the vortex's swirl (counter-clockwise in the north) with the inflow's ~20 degrees, weaker near the sea
static bool StormAt(const VECTOR3& cp, double& mmh, VECTOR3& wind, double& alt);
bool StormRainHere(const VECTOR3& cp, double& mmh, VECTOR3& wind, double& alt)
{
    if (!C.rain) { mmh = 0; wind = _V(0, 0, 0); alt = 1e9; return false; }
    return StormAt(cp, mmh, wind, alt);
}
// the storm's rain and wind for the sound module (StormSound.dll): at the camera; returns 0 outside the storm
extern "C" __declspec(dllexport) int StormSoundState(double* mmh, double* wind, double* agl)
{
    VECTOR3 cp; oapiCameraGlobalPos(&cp);
    double r = 0, a = 1e9; VECTOR3 w = _V(0, 0, 0);
    const bool ok = StormAt(cp, r, w, a);
    if (mmh) *mmh = r; if (wind) *wind = length(w); if (agl) *agl = a;
    return ok || length(w) > 1 ? 1 : 0;
}
static bool StormAt(const VECTOR3& cp, double& mmh, VECTOR3& wind, double& alt)
{
    mmh = 0; wind = _V(0, 0, 0); alt = 1e9;
    if (!C.on || cpuMap.empty() || !S.hP || oapiCameraProxyGbody() != S.hP) return false;
    VECTOR3 pp; oapiGetGlobalPos(S.hP, &pp);
    MATRIX3 Rp; oapiGetRotationMatrix(S.hP, &Rp);
    const VECTOR3 P = tmul(Rp, cp - pp);
    const double r = length(P), h = r - S.R;
    alt = h;
    const VECTOR3 u = P / r;
    const double c = dotp(u, S.c), sn = length(crossp(u, S.c));
    const double k = S.R * atan2(sn, c) / std::max(sn, 1e-12);
    const double x = dotp(u, S.e) * k, y = dotp(u, S.n) * k, rxy = sqrt(x * x + y * y) + 1;
    if (rxy > C.outer) return false;
    const double cov = CpuMap(x, y, 0), top = std::max(CpuMap(x, y, 1) * C.top, C.base + 300), conv = CpuMap(x, y, 2);
    if (h > top) return false;
    const double eyeR = C.eye * 1.05;
    const double eyeCut = std::clamp((rxy - eyeR * 0.9) / (eyeR * 0.2), 0.0, 1.0);
    const double depth = std::clamp((top - 2500.0) / 5000.0, 0.0, 1.0);   // showers from cumulus over ~2.5 km, downpours from the deep cells
    mmh = cov * cov * (conv * 55.0 + (1 - conv) * 6.0) * depth * eyeCut;
    static double tl = -1e9; if (oapiGetSysTime() - tl > 20) { tl = oapiGetSysTime(); LOGC("storm rain map: r %.1f km, cover %.2f, top %.0f m, conv %.2f, eye %.2f -> %.1f mm/h", rxy / 1e3, cov, top, conv, eyeCut, mmh); }
    // the wind: the modified Rankine vortex of the map's flow, 80 % of it near the sea, the inflow turning it inwards
    const double rm = C.eye + 0.5 * C.wall, vmax = 62;
    double vt = rxy < rm ? vmax * rxy / rm : vmax * pow(rm / rxy, 0.6);
    vt *= std::clamp(0.8 + 0.2 * h / 2000.0, 0.8, 1.0);
    // the surface layer: the wind falls off logarithmically towards the ground (z0 ~3 cm over grass, ~0.2 mm over the
    // sea; the 10 m wind ~0.8 of the gradient wind above, at a man's height ~0.6 of that)
    {
        double lng, lat, rad; oapiGlobalToEqu(S.hP, cp, &lng, &lat, &rad);
        const double agl = std::max(rad - S.R - std::max(oapiSurfaceElevation(S.hP, lng, lat), 0.0), 0.5);
        const double z0 = 0.03;
        if (agl < 300) vt *= std::clamp(log(agl / z0) / log(300 / z0), 0.3, 1.0);
        alt = agl;                                            // the rain's box wants the height over the ground
    }
    const double inflow = h < 1500 ? 20 * RAD : 5 * RAD;
    const double ex = x / rxy, ey = y / rxy;                  // outward
    const double tx = -ey * S.hs, ty = ex * S.hs;             // with the swirl
    const double wx = vt * (tx * cos(inflow) - ex * sin(inflow)), wy = vt * (ty * cos(inflow) - ey * sin(inflow));
    VECTOR3 east = S.e - u * dotp(S.e, u); east = east / length(east);
    VECTOR3 north = S.n - u * dotp(S.n, u); north = north / length(north);
    wind = mul(Rp, east * wx + north * wy);
    return mmh > 0.05;
}
