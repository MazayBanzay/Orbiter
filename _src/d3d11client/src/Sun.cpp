// The Sun (shaders\sun.hlsl): the photosphere, the chromosphere, prominences and the corona, drawn after the opaque scene
// and before the air (where the sky is: depth 0), so the air's pass dims it along each ray. The air's pass no longer adds
// its own disc while this one draws (g_sunPassOn, FxAtmo.cpp).
// Brightness: on screen a white surface under the Sun = 1, so the disc's mean is 1 / a^2 (a: its angular radius) -
// ~46 000 at 1 AU. The scene's FP16 target ends at 65504 (+INF turns black in post), so the whole Sun is scaled down
// together (never clipped: the limb darkening and the spots stay) to a centre of at most SUN_CAP. Through the visor's
// grey filter (10^-4.5) the disc is then ~1.
// The visor's H-alpha filter (a filter group with UsrFlag 0x200, Mesh.cpp) switches this pass to the H-alpha Sun.
#include "Client.h"

bool g_sunPassOn = false;       // FxAtmo.cpp: the air's pass leaves the disc to this pass
bool g_sunHAlphaSeen = false;   // Mesh.cpp: an H-alpha filter group was drawn (the frame before)
bool g_sunFilterSeen = false;   // Mesh.cpp: a filter group (the visor's sun spot) was drawn (the frame before)
float g_sunFilterT = 1.0f;      // ... its transmittance (the least of them; the spot's texture taken as opaque)

namespace {
    const double SUN_CAP = 3.0e4;   // the disc's centre at most (the master's limit with room for the haze on top)
    ID3D11Device* suDev = nullptr;
    ComPtr<ID3D11VertexShader> suVS; ComPtr<ID3D11PixelShader> suPS;
    ComPtr<ID3D11Buffer> suCB; ComPtr<ID3D11BlendState> suBS;
    bool suTried = false;
    struct SunCB { XMFLOAT4 dir, pole, bx, bz, tan, par, act, mode, quad, glare, scr, realWl, realHa, realLimb; };

    // the real Sun: map sets from tools\sun_fetch.py in Modules\D3D11Client\sun\<date>\, the nearest to the simulation's date
    struct RealSet { std::string dir; double mjd[3] = { -1, -1, -1 }, lon[3] = {}; };   // wl, ha, c2: image MJD, Earth's longitude [deg]
    std::vector<RealSet> realSets; bool realScanned = false;
    int realCur = -2;
    ComPtr<ID3D11ShaderResourceView> realSrv[4];    // wl, ha, limb, c2
    ID3D11Device* realDev = nullptr;

    void ScanReal()
    {
        realScanned = true;
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA("Modules\\D3D11Client\\sun\\*", &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
            RealSet s; s.dir = std::string("Modules\\D3D11Client\\sun\\") + fd.cFileName + "\\";
            if (FILE* f = fopen((s.dir + "meta.txt").c_str(), "r")) {
                char line[256];
                while (fgets(line, sizeof line, f)) {
                    char k[8] = {}; double m, l;
                    if (sscanf(line, "%7s %lf %lf", k, &m, &l) == 3) {
                        const int i = !strcmp(k, "wl") ? 0 : !strcmp(k, "ha") ? 1 : !strcmp(k, "c2") ? 2 : -1;
                        if (i >= 0) { s.mjd[i] = m; s.lon[i] = l; }
                    }
                }
                fclose(f);
                realSets.push_back(s);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        LOGC("sun: %u real image set(s) in Modules\\D3D11Client\\sun", (unsigned)realSets.size());
    }

    // a map file (tools\sun_fetch.py: width, height, channels as 3 uint32, then the bytes) as a texture with its mips
    ComPtr<ID3D11ShaderResourceView> LoadMap(ID3D11Device* dev, ID3D11DeviceContext* ctx, const std::string& fn)
    {
        ComPtr<ID3D11ShaderResourceView> srv;
        FILE* f = fopen(fn.c_str(), "rb");
        if (!f) return srv;
        uint32_t hd[3] = {};
        if (fread(hd, 4, 3, f) == 3 && hd[0] && hd[1] && hd[0] <= 4096 && hd[1] <= 4096 && (hd[2] == 1 || hd[2] == 2)) {
            std::vector<uint8_t> px((size_t)hd[0] * hd[1] * hd[2]);
            if (fread(px.data(), 1, px.size(), f) == px.size()) {
                D3D11_TEXTURE2D_DESC td = {}; td.Width = hd[0]; td.Height = hd[1]; td.MipLevels = 0; td.ArraySize = 1;
                td.Format = hd[2] == 1 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8_UNORM; td.SampleDesc.Count = 1;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
                ComPtr<ID3D11Texture2D> tex;
                if (SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &tex)) && SUCCEEDED(dev->CreateShaderResourceView(tex.Get(), nullptr, &srv))) {
                    ctx->UpdateSubresource(tex.Get(), 0, nullptr, px.data(), hd[0] * hd[2], 0);
                    ctx->GenerateMips(srv.Get());
                }
            }
        }
        fclose(f);
        return srv;
    }

    ComPtr<ID3D11VertexShader> glVS; ComPtr<ID3D11PixelShader> glPS;
    bool glReady = false;          // this frame's constants are in suCB (RenderSun ran and the Sun is ahead)

    XMFLOAT4 F4(const VECTOR3& v, double w) { return { (float)v.x, (float)v.y, (float)v.z, (float)w }; }
    VECTOR3 Unit(const VECTOR3& v) { return v / length(v); }
}

void D3D11Client::RenderSun(const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj)
{
    double FxTestNum(const char* key);
    static const double tFilt = FxTestNum("SunFilter");          // test runs: the Sun as through a grey filter of this transmittance
    static const bool tHa = FxTestNum("SunHAlpha") > 0.5;         // ... the H-alpha Sun
    static const double tOcc = FxTestNum("SunOcculter");         // ... a coronagraph's occulting disc [solar radii]
    const bool ha = g_sunHAlphaSeen || tHa;
    const double filterT = tFilt > 0 ? 0.0 : g_sunFilterSeen ? g_sunFilterT : 1.0;   // the glare through a visor's filter
    g_sunHAlphaSeen = false; g_sunFilterSeen = false; g_sunFilterT = 1.0f;
    glReady = false;
    g_sunPassOn = false;
    OBJHANDLE hS = nullptr;
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) { hS = h; break; } }
    if (!hS) return;
    if (suDev != dev.Get()) { suDev = dev.Get(); suVS.Reset(); suPS.Reset(); suCB.Reset(); suBS.Reset(); glVS.Reset(); glPS.Reset(); suTried = false; }
    if (!suVS && !suTried) {
        suTried = true;
        auto b1 = CompileShaderFile("sun.hlsl", "VS", "vs_5_0"), b2 = CompileShaderFile("sun.hlsl", "PS", "ps_5_0");
        if (b1 && b2) {
            dev->CreateVertexShader(b1->GetBufferPointer(), b1->GetBufferSize(), nullptr, &suVS);
            dev->CreatePixelShader(b2->GetBufferPointer(), b2->GetBufferSize(), nullptr, &suPS);
            auto b3 = CompileShaderFile("sun.hlsl", "VSG", "vs_5_0"), b4 = CompileShaderFile("sun.hlsl", "PSG", "ps_5_0");
            if (b3 && b4) {
                dev->CreateVertexShader(b3->GetBufferPointer(), b3->GetBufferSize(), nullptr, &glVS);
                dev->CreatePixelShader(b4->GetBufferPointer(), b4->GetBufferSize(), nullptr, &glPS);
            }
            D3D11_BUFFER_DESC bd = { (sizeof(SunCB) + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
            dev->CreateBuffer(&bd, nullptr, &suCB);
            D3D11_BLEND_DESC bl = {}; auto& t = bl.RenderTarget[0];
            t.BlendEnable = TRUE; t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_ONE; t.BlendOp = D3D11_BLEND_OP_ADD;
            t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE; t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
            dev->CreateBlendState(&bl, &suBS);
        }
        LOGC("sun: shaders %s", suVS && suPS && suCB && suBS ? "ready" : "FAILED");
    }
    if (!suVS || !suPS || !suCB || !suBS) return;
    g_sunPassOn = true;           // from here the disc is this pass's, seen or not

    VECTOR3 sp; oapiGetGlobalPos(hS, &sp);
    const VECTOR3 toS = sp - cp;
    const double dist = length(toS), Rs = oapiGetSize(hS);
    if (dist <= Rs * 1.01) return;
    const double a = asin(Rs / dist);
    const VECTOR3 sd = toS / dist;
    const VECTOR3 sc = tmul(cr, sd);                              // camera space (global = cr * camera)
    XMFLOAT4X4 P; XMStoreFloat4x4(&P, proj);
    const double tanX = 1.0 / P._11, tanY = 1.0 / P._22;
    const double pix = 2.0 * tanY / std::max((double)RH, 1.0);    // one pixel [rad]
    const double rhoMax = 8.0;                                    // the corona drawn out to 8 solar radii
    const double ext = std::min(rhoMax * a + 6 * pix, 1.5);
    if (sc.z < -sin(ext)) return;                                 // behind the camera
    // the glare's constants (drawn later, over the whole scene: RenderSunGlare) - for a Sun ahead of the camera
    const bool ahead = sc.z > 0.05;
    const float gx = ahead ? (float)(sc.x / sc.z / tanX + P._31) : 0, gy = ahead ? (float)(sc.y / sc.z / tanY + P._32) : 0;

    // the quad round it in NDC (the whole screen when it is large or near the edge of the view)
    float qx = 0, qy = 0, hx = 1.5f, hy = 1.5f;
    bool onScreen = true;
    if (sc.z > 0.3 && ext < 0.35) {
        qx = (float)(sc.x / sc.z / tanX + P._31); qy = (float)(sc.y / sc.z / tanY + P._32);
        hx = (float)(tan(ext) / sc.z / tanX * 1.25); hy = (float)(tan(ext) / sc.z / tanY * 1.25);
        if (fabs(qx) - hx > 1 || fabs(qy) - hy > 1) onScreen = false;   // off the screen (its glare may still reach in)
    }

    // the Sun's axis: 7.25 deg from the ecliptic's pole, the equator's ascending node at 75.76 deg (J2000; Orbiter's frame:
    // x the equinox, y the ecliptic's north, z at longitude 90 deg)
    const double lp = (75.76 - 90.0) * RAD, bp = (90.0 - 7.25) * RAD;
    const VECTOR3 pole = _V(cos(bp) * cos(lp), sin(bp), cos(bp) * sin(lp));
    const VECTOR3 bx = Unit(_V(1, 0, 0) - pole * pole.x);
    VECTOR3 bz = _V(0, 0, 1) - pole * pole.z; bz = Unit(bz - bx * dotp(bz, bx));

    // the date: rotation and the cycle
    const double t = oapiGetSimMJD() - 51544.5;                    // days since J2000
    const double year = 2000.0 + t / 365.25;
    double ph = fmod((year - 2019.96) / 11.0, 1.0); if (ph < 0) ph += 1;     // cycle 25's minimum: Dec 2019
    const double act = pow(sin(PI * pow(ph, 0.844)), 2);                 // the maximum ~4.8 years after the minimum
    const double spotLat = (28.0 - 21.0 * ph) * RAD;                       // Sporer's law

    // brightness: the disc's mean 1 / a^2 on screen, the whole Sun scaled to keep its peak under SUN_CAP
    const double discPx = a / pix;
    const double mean = 1.0 / (a * a);
    const double peak = discPx < 1.5 ? mean * discPx * discPx / 0.72 : mean / (1 - 0.81 / 3);
    const double K = std::min(1.0, SUN_CAP / peak);

    SunCB k;
    k.dir = F4(sc, a);
    k.pole = F4(tmul(cr, pole), pix / sin(a));
    k.bx = F4(tmul(cr, bx), 1.0 / sin(a));
    k.bz = F4(tmul(cr, bz), 0);
    k.tan = { (float)tanX, (float)tanY, P._31, P._32 };
    k.par = { (float)(mean * K * (tFilt > 0 ? tFilt : 1.0)), (float)rhoMax, (float)discPx, 0 };
    k.act = { (float)act, (float)spotLat, (float)t, 0 };
    k.mode = { ha ? 1.0f : 0.0f, (float)ph, (float)fmod(t * 1440.0, 10000.0), (float)tOcc };
    k.quad = { qx, qy, hx, hy };
    static const double tGlare = FxTestNum("SunGlare");           // test runs: the glare's scale (default 1)
    k.glare = { gx, gy, (float)discPx, ahead ? (float)(filterT * (tGlare > 0 ? tGlare : 1.0)) : 0.0f };
    k.scr = { (float)RW, (float)RH, (float)samples, 0 };
    // the real Sun: the set nearest the simulation's date (within 20 days), its maps' times and where Earth was
    k.realWl = k.realHa = k.realLimb = { 0, 0, 0, 0 };
    {
        const double now = oapiGetSimMJD();
        if (!realScanned) ScanReal();
        if (realDev != dev.Get()) { realDev = dev.Get(); realCur = -2; for (auto& s : realSrv) s.Reset(); }
        int best = -1; double bd = 20;
        for (size_t i = 0; i < realSets.size(); i++)
            for (double m : realSets[i].mjd) if (m > 0 && fabs(m - now) < bd) { bd = fabs(m - now); best = (int)i; }
        if (best != realCur) {
            realCur = best;
            for (auto& s : realSrv) s.Reset();
            if (best >= 0) {
                const char* fn[4] = { "wl.bin", "ha.bin", "halimb.bin", "c2.bin" };
                for (int i = 0; i < 4; i++) realSrv[i] = LoadMap(dev.Get(), ctx.Get(), realSets[best].dir + fn[i]);
                LOGC("sun: real images %s (white light %s, H-alpha %s, corona %s)", realSets[best].dir.c_str(),
                     realSrv[0] ? "yes" : "no", realSrv[1] ? "yes" : "no", realSrv[3] ? "yes" : "no");
            }
        }
        if (realCur >= 0) {
            const RealSet& s = realSets[realCur];
            auto eqLon = [&](double lonDeg) { const VECTOR3 e = _V(cos(lonDeg * RAD), 0, sin(lonDeg * RAD)); return atan2(dotp(e, bz), dotp(e, bx)); };
            auto along = [&](double lonDeg) {     // the observer near Earth's line of sight then (the limb, the corona)
                const VECTOR3 e = _V(cos(lonDeg * RAD), 0, sin(lonDeg * RAD));
                const double c = dotp(-sd, e);
                const double lo = cos(25 * RAD), hi = cos(8 * RAD), x = std::clamp((c - lo) / (hi - lo), 0.0, 1.0);
                return x * x * (3 - 2 * x);
            };
            if (realSrv[0] && s.mjd[0] > 0 && fabs(now - s.mjd[0]) < 20) k.realWl = { 1, (float)(now - s.mjd[0]), (float)eqLon(s.lon[0]), 0 };
            if (realSrv[1] && s.mjd[1] > 0 && fabs(now - s.mjd[1]) < 20) k.realHa = { 1, (float)(now - s.mjd[1]), (float)eqLon(s.lon[1]), 0 };
            // the limb's prominences change over days and the Sun turns: fresh only; the corona's streamers last longer
            if (realSrv[2] && s.mjd[1] > 0) k.realLimb.x = (float)(along(s.lon[1]) * exp(-fabs(now - s.mjd[1]) / 2.0));
            if (realSrv[3] && s.mjd[2] > 0) k.realLimb.y = (float)(along(s.lon[2]) * exp(-fabs(now - s.mjd[2]) / 7.0));
        }
    }
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(suCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &k, sizeof k); ctx->Unmap(suCB.Get(), 0);

    static double tLog = -1e9; const double now = oapiGetSysTime();
    if (now - tLog > 30) {
        tLog = now;
        LOGC("sun: %.3f AU, disc %.4f deg (%.1f px), activity %.2f (cycle 25 phase %.2f, spots at %.0f deg), scale %.3g%s",
             dist / 1.495978707e11, 2 * a * DEG, discPx, act, ph, spotLat * DEG, K, ha ? ", H-alpha" : "");
    }

    glReady = ahead && glVS && glPS;
    if (!onScreen) return;
    ctx->OMSetBlendState(suBS.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);                  // reverse Z: the quad at depth 0 passes only on the sky
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->VSSetShader(suVS.Get(), nullptr, 0); ctx->PSSetShader(suPS.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(3, 1, suCB.GetAddressOf()); ctx->PSSetConstantBuffers(3, 1, suCB.GetAddressOf());
    {
        ID3D11ShaderResourceView* rs[4] = { realSrv[0].Get(), realSrv[1].Get(), realSrv[2].Get(), realSrv[3].Get() };
        ctx->PSSetShaderResources(2, 4, rs);
        ID3D11SamplerState* sm[2] = { sampAniso.Get(), sampClamp.Get() };
        ctx->PSSetSamplers(0, 2, sm);
    }
    ctx->Draw(4, 0);
    { ID3D11ShaderResourceView* nul[4] = {}; ctx->PSSetShaderResources(2, 4, nul); }
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
}

// the glare over the finished scene, the camera's own vessel included: a cockpit's roof over the Sun hides it, through a
// window it shows (a visor's filter over the Sun takes it away, sGlare.w): one full-screen pass, the disc's visibility
// read from the depth in its VS
void D3D11Client::RenderSunGlare()
{
    static double tLog = -1e9; const double now = oapiGetSysTime();
    const bool log = now - tLog > 30; if (log) tLog = now;
    if (log) LOGC("sun glare: %s (shaders %s, depth %s)", glReady ? "drawn" : "not drawn", glVS && glPS ? "ready" : "MISSING",
                  samples > 1 ? (depthMSSRV ? "per sample" : "MISSING") : (depthSRV ? "single" : "MISSING"));
    if (!glReady) return;
    glReady = false;
    ID3D11ShaderResourceView* d0 = samples > 1 ? nullptr : depthSRV.Get();
    ID3D11ShaderResourceView* d1 = samples > 1 ? depthMSSRV.Get() : nullptr;
    if (!d0 && !d1) return;
    ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11DepthStencilView> dsv;
    ctx->OMGetRenderTargets(1, &rtv, &dsv);
    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);      // the depth is read
    ctx->OMSetBlendState(suBS.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(glVS.Get(), nullptr, 0); ctx->PSSetShader(glPS.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(3, 1, suCB.GetAddressOf()); ctx->PSSetConstantBuffers(3, 1, suCB.GetAddressOf());
    ID3D11ShaderResourceView* d[2] = { d0, d1 };
    ctx->VSSetShaderResources(0, 2, d);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul[2] = {};
    ctx->VSSetShaderResources(0, 2, nul);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());
}
