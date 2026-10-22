// Soil thrown in a vacuum (the Moon, Io, any airless body) - off the wheels and by the rocket jets (shaders\regolith.hlsl),
// as the mockup worked it out (regolith mockup, 2026-10-22): GPU particles, each a share of the soil's mass, flown
// ballistically, bouncing off every vehicle that has a dust body (its mesh as a distance field and its wheels:
// tools\build_dust_sdf.py, Config\D3D11Client\DustBodies.json), drawn as soft Gaussians summed into the optical depth,
// lit by the Sun through their own shade (a volumetric shadow map that also darkens the ground under them).
// Two processes, two sources:
//  - a wheel (RegolithFeed, from GpuParticles::SpawnSource - the SOIL sources): its rooster tail off the rim's lower rear
//    (10-75 deg past the contact) at k x the rim's velocity relative to the vehicle - the Apollo 16 film's dust tips left at
//    1.1-1.6 x the LRV's speed, 45-55 deg up (Hsu & Horanyi 2012, Am. J. Phys. 80, 452) - in bursts of different heights
//    and densities (40 ms every 0.18-0.43 s, 85 % of the mass, each its own k 0.7-1.6); a fifth of the soil, carried over
//    the top by the tread, leaves there at its velocity (Mullis 1971: the forward plume without a fender);
//  - a jet (RegolithJet, from GpuParticles::SpawnJet): the eroded soil out of the footprint's ring, low, each size at its
//    own speed (GpuParticles: Metzger et al. 2010's erosion rate, the grains dragged by the jet's pressure).
#include "Client.h"
#include "Fx.h"
#include "ExhaustInternal.h"
#include <map>
#include <random>
#include <string>

extern XMMATRIX g_exViewProj;

namespace {
    const UINT CAP = 524288;                 // particles in the ring
    const UINT MAXSPAWN = 16384;             // new ones a frame
    const int MAXV = 4;                      // vehicles with a dust body near the focus
    const int SX = 96, SY = 48, SZ = 96;     // the shade box (cells)
    const double FIX = 1e5;                  // the shade sums' fixed point
    const double RIM_R = 0.8;                // a wheel's radius (the MPU's; the sources carry none)
    // the soil by mass (Sargeant et al. 2022 after Zeng et al. 2010): diameter, mass share, optical cross-section
    // 1.5 / (rho d) [m2/kg] (grain density 3100 kg/m3)
    const double CD[3] = { 469e-6, 55e-6, 12e-6 }, CF[3] = { 0.38, 0.32, 0.30 };
    double CK(int c) { return 1.5 / (3100.0 * CD[c]); }

    struct Part { XMFLOAT3 p; float w; XMFLOAT3 v; float age; float grow; float gy; float alive; float pad; };   // 48 B
    struct CB
    {
        XMFLOAT4 m0, m1, m2; XMFLOAT4X4 vp; XMFLOAT4 t, opt, spawn, box, dim;
        XMFLOAT4 veh[MAXV * 7];
    };
    struct Vol { ComPtr<ID3D11Texture3D> t; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav; };
    struct Scr { ComPtr<ID3D11Texture2D> t; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11RenderTargetView> rtv; };
    // a vehicle's dust body: its mesh's distance field (vessel frame) and its wheels
    struct Body
    {
        bool ok = false;
        int dims[3] = {}; float mn[3] = {}, cell = 0, band = 0, R = 0.8f, W = 0.65f;
        std::vector<XMFLOAT3> hubs;
        ComPtr<ID3D11Texture3D> tex; ComPtr<ID3D11ShaderResourceView> srv;
    };
    std::map<std::string, Body> bodies;
    // a wheel's bursts
    struct Burst { double next = -1, end = -1, k = 1, m = 1; };
    std::map<uintptr_t, Burst> bursts;

    ComPtr<ID3D11ComputeShader> csSpawn, csStep, csVoxel, csShade, csDensity;
    ComPtr<ID3D11VertexShader> vsGrain, vsFull; ComPtr<ID3D11PixelShader> psGrain, psApply;
    ComPtr<ID3D11BlendState> bsApply, bsAdd;
    ComPtr<ID3D11Buffer> partBuf, spawnBuf, hubBuf, cbuf;
    ComPtr<ID3D11ShaderResourceView> partSRV, spawnSRV, hubSRV;
    ComPtr<ID3D11UnorderedAccessView> partUAV;
    Vol shadeSum, shade, count, dens;          // the shade box: extinction sums, transmittance; particle counts, number density
    Scr tauS, litS, tauQ, litQ;              // the small particles' targets (full resolution), the wide ones' (a quarter's)
    int scW = 0, scH = 0;
    std::vector<Part> spawn;                 // this frame's new particles
    UINT head = 0;                           // the ring's next slot
    double lastFed = -1e9;                   // when a source last threw
    double reach = 10, reachT = -1e9;        // how far the fastest dust thrown lately flies [m]; when it was set
    VECTOR3 aP0 = { 0, 0, 0 }; bool fresh = true;
    bool tried = false, ok = false;
    std::mt19937 rng(12345);
    double U() { return std::uniform_real_distribution<double>(0, 1)(rng); }
    double Gs() { double s = 0; for (int i = 0; i < 4; i++) s += U(); return (s - 2) * 1.732; }
    int Cls() { const double u = U(); return u < CF[0] ? 0 : u < CF[0] + CF[1] ? 1 : 2; }

    XMFLOAT3 F3(const VECTOR3& v) { return { (float)v.x, (float)v.y, (float)v.z }; }
    XMFLOAT4 F4(const VECTOR3& v, double w) { return { (float)v.x, (float)v.y, (float)v.z, (float)w }; }
    VECTOR3 Unit(const VECTOR3& v) { double l = length(v); return l > 0 ? v / l : _V(0, 1, 0); }
    VECTOR3 Rot(const VECTOR3& v, const VECTOR3& k, double a)     // Rodrigues: v about the unit axis k by a
    {
        const double c = cos(a), s = sin(a);
        return v * c + crossp(k, v) * s + k * (dotp(k, v) * (1 - c));
    }
    void Add(const VECTOR3& p, const VECTOR3& v, double w, double grow, double gy)
    {
        if (spawn.size() >= MAXSPAWN) return;
        Part q; q.p = F3(p); q.w = (float)w; q.v = F3(v); q.age = 0; q.grow = (float)grow; q.gy = (float)gy; q.alive = 1; q.pad = 0;
        spawn.push_back(q);
    }

    // the dust body of a vessel class (Config\D3D11Client\DustBodies.json: the class's "out" file), loaded once
    Body* BodyOf(D3D11Client* gc, const char* cls)
    {
        auto it = bodies.find(cls);
        if (it != bodies.end()) return it->second.ok ? &it->second : nullptr;
        Body& b = bodies[cls];
        std::string json;
        if (FILE* f = fopen("Config\\D3D11Client\\DustBodies.json", "rb")) { char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0) json.append(buf, n); fclose(f); }
        const size_t at = json.find(std::string("\"") + cls + "\"");
        if (at == std::string::npos) return nullptr;
        const size_t o = json.find("\"out\"", at), q0 = o == std::string::npos ? o : json.find('"', json.find(':', o) + 1);
        if (q0 == std::string::npos) return nullptr;
        std::string path = json.substr(q0 + 1, json.find('"', q0 + 1) - q0 - 1);
        for (char& c : path) if (c == '/') c = '\\';
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) { LOGC("regolith: dust body of %s: %s not found", cls, path.c_str()); return nullptr; }
        char mg[4] = {}; int nw = 0;
        bool good = fread(mg, 1, 4, f) == 4 && !memcmp(mg, "RDF1", 4) && fread(b.dims, 4, 3, f) == 3 && fread(b.mn, 4, 3, f) == 3 &&
                    fread(&b.cell, 4, 1, f) == 1 && fread(&b.band, 4, 1, f) == 1 && fread(&nw, 4, 1, f) == 1 && fread(&b.R, 4, 1, f) == 1 && fread(&b.W, 4, 1, f) == 1;
        if (good && nw > 0 && nw <= 8) { b.hubs.resize(nw); good = fread(b.hubs.data(), 12, nw, f) == (size_t)nw; }
        std::vector<uint8_t> data;
        if (good) { data.resize((size_t)b.dims[0] * b.dims[1] * b.dims[2]); good = fread(data.data(), 1, data.size(), f) == data.size(); }
        fclose(f);
        if (!good) { LOGC("regolith: dust body of %s: %s unreadable", cls, path.c_str()); return nullptr; }
        D3D11_TEXTURE3D_DESC d = { (UINT)b.dims[0], (UINT)b.dims[1], (UINT)b.dims[2], 1, DXGI_FORMAT_R8_UNORM, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_SUBRESOURCE_DATA sd = { data.data(), (UINT)b.dims[0], (UINT)(b.dims[0] * b.dims[1]) };
        if (FAILED(gc->Dev()->CreateTexture3D(&d, &sd, &b.tex)) || FAILED(gc->Dev()->CreateShaderResourceView(b.tex.Get(), nullptr, &b.srv))) return nullptr;
        b.ok = true;
        LOGC("regolith: dust body of %s: %d x %d x %d cells of %.2f m, %d wheels (R %.2f, W %.2f) - %s", cls, b.dims[0], b.dims[1], b.dims[2], b.cell, nw, b.R, b.W, path.c_str());
        return &b;
    }

    bool Init(D3D11Client* gc)
    {
        if (tried) return ok;
        tried = true;
        ID3D11Device* dev = gc->Dev();
        auto csh = [&](const char* e, ComPtr<ID3D11ComputeShader>& s) { auto b = FxCompile("regolith.hlsl", e, "cs_5_0"); return b && SUCCEEDED(dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s)); };
        auto bvg = FxCompile("regolith.hlsl", "VSGrain", "vs_5_0"), bpg = FxCompile("regolith.hlsl", "PSGrain", "ps_5_0");
        auto bvf = FxCompile("regolith.hlsl", "VSFull", "vs_5_0"), bpa = FxCompile("regolith.hlsl", "PSApply", "ps_5_0");
        if (!csh("CSSpawn", csSpawn) || !csh("CSStep", csStep) || !csh("CSVoxel", csVoxel) || !csh("CSShade", csShade) || !csh("CSDensity", csDensity) || !bvg || !bpg || !bvf || !bpa ||
            FAILED(dev->CreateVertexShader(bvg->GetBufferPointer(), bvg->GetBufferSize(), nullptr, &vsGrain)) ||
            FAILED(dev->CreatePixelShader(bpg->GetBufferPointer(), bpg->GetBufferSize(), nullptr, &psGrain)) ||
            FAILED(dev->CreateVertexShader(bvf->GetBufferPointer(), bvf->GetBufferSize(), nullptr, &vsFull)) ||
            FAILED(dev->CreatePixelShader(bpa->GetBufferPointer(), bpa->GetBufferSize(), nullptr, &psApply))) { LOGC("regolith: shaders FAILED"); return false; }
        {   // the particles (a ring), the frame's new ones, the hubs
            D3D11_BUFFER_DESC d = { CAP * (UINT)sizeof(Part), D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(Part) };
            if (FAILED(dev->CreateBuffer(&d, nullptr, &partBuf)) || FAILED(dev->CreateShaderResourceView(partBuf.Get(), nullptr, &partSRV)) ||
                FAILED(dev->CreateUnorderedAccessView(partBuf.Get(), nullptr, &partUAV))) return false;
            D3D11_BUFFER_DESC s = { MAXSPAWN * (UINT)sizeof(Part), D3D11_USAGE_DYNAMIC, D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_WRITE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(Part) };
            if (FAILED(dev->CreateBuffer(&s, nullptr, &spawnBuf)) || FAILED(dev->CreateShaderResourceView(spawnBuf.Get(), nullptr, &spawnSRV))) return false;
            D3D11_BUFFER_DESC h = { MAXV * 8 * 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_WRITE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16 };
            if (FAILED(dev->CreateBuffer(&h, nullptr, &hubBuf)) || FAILED(dev->CreateShaderResourceView(hubBuf.Get(), nullptr, &hubSRV))) return false;
        }
        D3D11_BUFFER_DESC c = { (UINT)((sizeof(CB) + 15) & ~15u), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(dev->CreateBuffer(&c, nullptr, &cbuf))) return false;
        auto vol = [&](Vol& v, DXGI_FORMAT f) {
            D3D11_TEXTURE3D_DESC d = { (UINT)SX, (UINT)SY, (UINT)SZ, 1, f, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
            return SUCCEEDED(dev->CreateTexture3D(&d, nullptr, &v.t)) && SUCCEEDED(dev->CreateShaderResourceView(v.t.Get(), nullptr, &v.srv)) &&
                   SUCCEEDED(dev->CreateUnorderedAccessView(v.t.Get(), nullptr, &v.uav));
        };
        if (!vol(shadeSum, DXGI_FORMAT_R32_UINT) || !vol(shade, DXGI_FORMAT_R16_FLOAT) || !vol(count, DXGI_FORMAT_R32_UINT) || !vol(dens, DXGI_FORMAT_R16_FLOAT)) return false;
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
        if (FAILED(dev->CreateBlendState(&bd, &bsApply))) return false;      // the dust's light + the scene x its transmittance
        bd.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_RED };
        if (FAILED(dev->CreateBlendState(&bd, &bsAdd))) return false;        // the particles summed
        LOGC("regolith: GPU particles (%u, %u new a frame at most), the vehicles' dust bodies, the shade box %d x %d x %d; GPU memory %.1f MB",
             CAP, MAXSPAWN, SX, SY, SZ, (CAP * sizeof(Part) + MAXSPAWN * sizeof(Part) + SX * SY * SZ * 12.0) / 1048576);
        ok = true;
        return true;
    }

    bool Screen(D3D11Client* gc)
    {
        const int w = gc->SceneW(), h = gc->SceneH();
        if (w == scW && h == scH && tauS.t) return true;
        scW = w; scH = h;
        auto mk = [&](Scr& v, int q) {
            v = Scr();
            D3D11_TEXTURE2D_DESC d = { (UINT)std::max(1, w / q), (UINT)std::max(1, h / q), 1, 1, DXGI_FORMAT_R16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0 };
            return SUCCEEDED(gc->Dev()->CreateTexture2D(&d, nullptr, &v.t)) && SUCCEEDED(gc->Dev()->CreateShaderResourceView(v.t.Get(), nullptr, &v.srv)) &&
                   SUCCEEDED(gc->Dev()->CreateRenderTargetView(v.t.Get(), nullptr, &v.rtv));
        };
        if (!mk(tauS, 1) || !mk(litS, 1) || !mk(tauQ, 4) || !mk(litQ, 4)) { scW = scH = 0; return false; }
        return true;
    }
}

// ---------------------------------------------------------------- the sources
void RegolithFeed(const ExScene& sc, uintptr_t key, const VECTOR3& p0, const VECTOR3& ax, const VECTOR3& gs, double speed,
                  double cone, double kgps, double gy, const std::vector<XMFLOAT4>& boxes, double ringIn)
{
    (void)boxes; (void)ringIn;
    const double dt = std::min(sc.dt, 1.0 / 30);
    if (dt <= 0 || kgps <= 0 || speed < 0.2 || cone >= 0.8) return;   // (a ring: the jets come by RegolithJet)
    lastFed = sc.simt;
    // the wheel: the rim's velocity relative to the vessel at the arc's middle (20 deg + cone past the contact) is a; the axle
    // across it, level; the hub -> that point = a x axle
    const VECTOR3 a = Unit(ax);
    VECTOR3 n = crossp(a, _V(0, 1, 0)); n = length(n) < 1e-3 ? _V(1, 0, 0) : Unit(n);
    const VECTOR3 rh = Unit(crossp(a, n)), hub = p0 - rh * RIM_R;
    const double mid = 20 * RAD + cone;
    // its bursts
    Burst& b = bursts[key];
    const double t1 = sc.simt, t0 = t1 - dt;
    if (b.next < 0 || b.next < t0 - 1.0) b.next = t0;
    double ov = 0, kOv = 0;
    for (;;) {
        if (b.end > t0) { const double o = std::min(b.end, t1) - std::max(b.end - 0.04, t0); if (o > 0) { ov += o * b.m; kOv += o * b.m * b.k; } }
        if (b.next > t1) break;
        b.end = b.next + 0.04; b.k = 0.7 + 0.9 * U(); b.m = 0.4 + 1.2 * U(); b.next += 0.18 + 0.25 * U();
    }
    const double inF = 0.85 * (0.305 / 0.04);
    const double share = ov * inF + dt * 0.15;                     // this frame's share of a second's mass
    const double kBurst = ov > 0 ? kOv / ov : 0.8;
    {   // how far it flies (the shade box's size): the tips at up to 1.6 x the rim's speed, plus the vessel's way
        const double vm = 1.6 * speed + 0.3 * length(gs), g = sc.GM / (sc.R * sc.R), r = vm * vm / std::max(g, 0.1) + 10;
        if (r > reach || sc.simt - reachT > 10) { reach = r; reachT = sc.simt; }
    }
    // particles a second at the mean rate: more as the wheel spins faster (the dust flies wider: ~ v^2), never wider ones
    const double perS = 6000 * std::clamp(speed * speed / 16.0, 1.0, 3.0);
    const int cnt = (int)floor(perS * share + U());
    const double mp = kgps / perS;
    for (int q = 0; q < cnt; q++) {
        const int c = Cls();
        const bool carried = U() < 0.2;
        const double phi = carried ? (150 + 70 * U()) * RAD : (10 + 65 * U()) * RAD;
        const double inB = U() < ov * inF / std::max(share, 1e-9);
        const double k = carried ? 0.6 + 0.4 * U() : (inB ? kBurst : 0.8) * (1 + 0.12 * Gs());
        const double th = phi - mid;
        const VECTOR3 d = Unit(Rot(a, n, th) + n * (0.125 * Gs()));          // ~ +-15 deg across (the chevrons)
        const VECTOR3 at = hub + Rot(rh, n, th) * RIM_R + n * ((U() - 0.5) * 0.65);
        const double sp = k * speed;
        Add(_V(at.x, std::max(at.y, gy + 0.02), at.z), gs + d * sp, CK(c) * mp, 0.04 + 0.1 * sp, gy);
    }
}

void RegolithJet(const ExScene& sc, const VECTOR3& hit, const VECTOR3& lean, double r, double kgps, const double v[3], double gy)
{
    const double dt = std::min(sc.dt, 1.0 / 30);
    if (dt <= 0 || kgps <= 0) return;
    lastFed = sc.simt;
    {   // how far the coarse grains fly (the fines leave fast and thin)
        const double g = sc.GM / (sc.R * sc.R), fly = v[0] * v[0] / std::max(g, 0.1) + 2 * r;
        if (fly > reach || sc.simt - reachT > 10) { reach = fly; reachT = sc.simt; }
    }
    const double perS = 30000, mp = kgps / perS;
    const int cnt = (int)floor(perS * dt + U());
    const double el[3][2] = { { 5, 15 }, { 2, 8 }, { 1, 6 } };
    const double ln = length(lean);
    const VECTOR3 ld = ln > 1e-6 ? lean / ln : _V(1, 0, 0);
    for (int q = 0; q < cnt; q++) {
        const int c = Cls();
        double az;      // more of it downstream of the tilt: density 1 + lean cos(az - the tilt's way)
        do { az = 2 * PI * U(); } while (U() * (1 + ln) > 1 + ln * cos(az));
        const VECTOR3 rd = ld * cos(az) + crossp(_V(0, 1, 0), ld) * sin(az);
        const double e = (el[c][0] + (el[c][1] - el[c][0]) * U()) * RAD, sp = v[c] * (0.7 + 0.6 * U());
        const VECTOR3 at = hit + rd * (r * (0.5 + 0.5 * U()));
        Add(_V(at.x, gy + 0.03, at.z), rd * (sp * cos(e)) + _V(0, sp * sin(e), 0), CK(c) * mp, 0.03 + 0.06 * sp, gy);
    }
}

// ---------------------------------------------------------------- the frame
void RegolithRender(D3D11Client* gc, const ExScene& sc, ID3D11ShaderResourceView* depth)
{
    if (!sc.anchored || sc.dense) { spawn.clear(); return; }
    if (sc.simt - lastFed > 45 && spawn.empty()) return;            // nothing thrown for long: all down
    if (!Init(gc) || !Screen(gc)) { spawn.clear(); return; }
    ID3D11DeviceContext* ctx = gc->Ctx();
    if (fresh || length(sc.aP - aP0) > 1e-3) {                       // a new frame of reference: the old particles go
        const UINT z4[4] = {};
        ctx->ClearUnorderedAccessViewUint(partUAV.Get(), z4);
        fresh = false; aP0 = sc.aP; head = 0;
    }
    OBJHANDLE fo = oapiGetFocusObject();
    VECTOR3 fg = sc.cp; if (fo) oapiGetGlobalPos(fo, &fg);
    const VECTOR3 fl = sc.ToLocal(fg);
    const double g = sc.GM / (sc.R * sc.R);
    const double dt = std::min(sc.dt, 1.0 / 30);
    const int ns = dt > 0 ? std::clamp((int)ceil(dt / 0.008), 1, 8) : 0;

    CB k = {};
    const VECTOR3 c0 = mul(sc.Rp, sc.a1), c1 = mul(sc.Rp, sc.aU), c2 = mul(sc.Rp, sc.a2);
    const VECTOR3 o = mul(sc.Rp, sc.aP) + sc.pp - sc.cp;
    k.m0 = { (float)c0.x, (float)c1.x, (float)c2.x, (float)o.x };
    k.m1 = { (float)c0.y, (float)c1.y, (float)c2.y, (float)o.y };
    k.m2 = { (float)c0.z, (float)c1.z, (float)c2.z, (float)o.z };
    XMStoreFloat4x4(&k.vp, XMMatrixTranspose(g_exViewProj));
    k.t = { (float)(ns ? dt / ns : 0), (float)g, (float)ns, (float)CAP };
    char nm[64] = ""; if (sc.hBody) oapiGetObjectName(sc.hBody, nm, 63);
    const double alb = !strcmp(nm, "Moon") ? 0.30 : !strcmp(nm, "Io") ? 0.45 : 0.25, w = !strcmp(nm, "Io") ? 0.45 : 0.28;
    const double focal = 0.5 * gc->SceneH() / std::max(oapiCameraAperture() > 0 ? tan(oapiCameraAperture()) : 1.0, 1e-3);
    k.opt = { (float)alb, (float)w, 0, (float)focal };
    // the vehicles near the focus with a dust body
    std::vector<XMFLOAT4> hubs(MAXV * 8, XMFLOAT4(0, 0, 0, 0));
    ID3D11ShaderResourceView* bodySRV[MAXV] = {};
    int nv = 0;
    for (DWORD i = 0; i < oapiGetVesselCount() && nv < MAXV; i++) {
        OBJHANDLE h = oapiGetVesselByIndex(i);
        VESSEL* v = oapiGetVesselInterface(h);
        if (!v) continue;
        VECTOR3 vp; v->GetGlobalPos(vp);
        if (length(vp - fg) > 400) continue;
        Body* b = BodyOf(gc, v->GetClassName());
        if (!b) continue;
        // local -> vessel: the local axes' images in the vessel's frame (its columns), the local origin's place
        MATRIX3 Rv; v->GetRotationMatrix(Rv);
        const VECTOR3 e0 = tmul(Rv, c0), e1 = tmul(Rv, c1), e2 = tmul(Rv, c2);
        VECTOR3 t; v->Global2Local(sc.LocalToGlobal(_V(0, 0, 0)), t);
        XMFLOAT4* r = &k.veh[nv * 7];
        r[0] = { (float)e0.x, (float)e1.x, (float)e2.x, (float)t.x };
        r[1] = { (float)e0.y, (float)e1.y, (float)e2.y, (float)t.y };
        r[2] = { (float)e0.z, (float)e1.z, (float)e2.z, (float)t.z };
        VECTOR3 gsG; v->GetGroundspeedVector(FRAME_GLOBAL, gsG);
        const VECTOR3 vl = sc.DirToLocal(gsG);
        r[3] = F4(vl, tmul(Rv, gsG).z);                              // (its wheels' surface speed: rolling, + forward)
        r[4] = { b->mn[0], b->mn[1], b->mn[2], b->cell };
        r[5] = { (float)b->dims[0], (float)b->dims[1], (float)b->dims[2], b->band };
        r[6] = { b->R, b->W, (float)b->hubs.size(), 0 };
        for (size_t q = 0; q < b->hubs.size() && q < 8; q++) hubs[nv * 8 + q] = { b->hubs[q].x, b->hubs[q].y, b->hubs[q].z, 1 };
        bodySRV[nv] = b->srv.Get();
        nv++;
    }
    const UINT nsp = (UINT)std::min<size_t>(spawn.size(), MAXSPAWN);
    k.spawn = { (float)nsp, (float)head, (float)nv, 0 };
    {   // the shade box round the focus, from the ground up: as wide as the dust thrown lately flies (cells of 0.5-4 m)
        double FxTestNum(const char* key);
        static const float dbg = (float)FxTestNum("RegolithDbg");
        k.spawn.w = dbg;
        const double hb = 0.5;                                    // (coarser cells drew the shadow in steps)
        const VECTOR3 lo = _V(floor((fl.x - SX * hb / 2) / hb) * hb, floor((fl.y - 4) / hb) * hb, floor((fl.z - SZ * hb / 2) / hb) * hb);
        k.box = F4(lo, hb);
        k.dim = { (float)SX, (float)SY, (float)SZ, (float)FIX };
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, &k, sizeof k); ctx->Unmap(cbuf.Get(), 0);
    if (FAILED(ctx->Map(hubBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, hubs.data(), hubs.size() * 16); ctx->Unmap(hubBuf.Get(), 0);
    if (nsp) {
        if (FAILED(ctx->Map(spawnBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        memcpy(m.pData, spawn.data(), nsp * sizeof(Part)); ctx->Unmap(spawnBuf.Get(), 0);
    }
    spawn.clear();

    // ---- the compute passes: the frame's constants (b0), the FX light (b4, t8), the shadow map (t7, s2) and the linear
    // sampler (s3) as the pixel shaders have them
    ID3D11Buffer* b0 = nullptr; ctx->PSGetConstantBuffers(0, 1, &b0);
    ID3D11Buffer* b4 = nullptr; ctx->PSGetConstantBuffers(4, 1, &b4);
    ID3D11ShaderResourceView* t8 = nullptr; ctx->PSGetShaderResources(8, 1, &t8);
    ID3D11SamplerState* s3 = nullptr; ctx->PSGetSamplers(3, 1, &s3);
    ID3D11ShaderResourceView* sh = gc->ShadowSRV(); ID3D11SamplerState* shs = gc->ShadowSampler();
    ctx->CSSetConstantBuffers(0, 1, &b0); ctx->CSSetConstantBuffers(4, 1, &b4); ctx->CSSetConstantBuffers(5, 1, cbuf.GetAddressOf());
    ctx->CSSetShaderResources(7, 1, &sh); ctx->CSSetShaderResources(8, 1, &t8);
    ctx->CSSetSamplers(2, 1, &shs); ctx->CSSetSamplers(3, 1, &s3);
    ID3D11UnorderedAccessView* nu[4] = {}; ID3D11ShaderResourceView* ns4[4] = {};
    ctx->CSSetUnorderedAccessViews(1, 1, partUAV.GetAddressOf(), nullptr);
    if (nsp) {          // 1: the new particles into the ring
        ctx->CSSetShaderResources(15, 1, spawnSRV.GetAddressOf());
        ctx->CSSetShader(csSpawn.Get(), nullptr, 0);
        ctx->Dispatch((nsp + 63) / 64, 1, 1);
        ctx->CSSetShaderResources(15, 1, ns4);
        head = (head + nsp) % CAP;
    }
    if (ns) {           // 2: the step (the vehicles' bodies, their hubs)
        ctx->CSSetShaderResources(12, 1, hubSRV.GetAddressOf());
        ctx->CSSetShaderResources(17, MAXV, bodySRV);
        ctx->CSSetShader(csStep.Get(), nullptr, 0);
        ctx->Dispatch((CAP + 63) / 64, 1, 1);
        ctx->CSSetShaderResources(12, 1, ns4); ctx->CSSetShaderResources(17, MAXV, ns4);
    }
    ctx->CSSetUnorderedAccessViews(1, 1, nu, nullptr);
    {   // 3: the dust's own shade
        const UINT z4[4] = {};
        ctx->ClearUnorderedAccessViewUint(shadeSum.uav.Get(), z4);
        ctx->ClearUnorderedAccessViewUint(count.uav.Get(), z4);
        ctx->CSSetShaderResources(16, 1, partSRV.GetAddressOf());
        ctx->CSSetUnorderedAccessViews(2, 1, shadeSum.uav.GetAddressOf(), nullptr);
        ctx->CSSetUnorderedAccessViews(4, 1, count.uav.GetAddressOf(), nullptr);
        ctx->CSSetShader(csVoxel.Get(), nullptr, 0);
        ctx->Dispatch((CAP + 63) / 64, 1, 1);
        ctx->CSSetUnorderedAccessViews(2, 1, nu, nullptr); ctx->CSSetUnorderedAccessViews(4, 1, nu, nullptr); ctx->CSSetShaderResources(16, 1, ns4);

        ctx->CSSetShaderResources(22, 1, shadeSum.srv.GetAddressOf());
        ctx->CSSetUnorderedAccessViews(3, 1, shade.uav.GetAddressOf(), nullptr);
        ctx->CSSetShader(csShade.Get(), nullptr, 0);
        ctx->Dispatch(SX / 4, SY / 4, SZ / 4);
        ctx->CSSetUnorderedAccessViews(3, 1, nu, nullptr); ctx->CSSetShaderResources(22, 1, ns4);
    }
    ctx->CSSetShader(nullptr, nullptr, 0);
    ctx->CSSetShaderResources(7, 1, ns4); ctx->CSSetShaderResources(8, 1, ns4);
    ID3D11Buffer* nb1[1] = {}; ctx->CSSetConstantBuffers(0, 1, nb1); ctx->CSSetConstantBuffers(4, 1, nb1); ctx->CSSetConstantBuffers(5, 1, nb1);
    if (b4) b4->Release();
    if (t8) t8->Release();

    // ---- 4: the particles as soft Gaussians, summed into the optical depth and its sunlit share (the scene's target kept)
    ID3D11RenderTargetView* oldRT[2] = {}; ID3D11DepthStencilView* oldDS = nullptr;
    ctx->OMGetRenderTargets(2, oldRT, &oldDS);
    ID3D11Buffer* oldVB0 = nullptr; ctx->VSGetConstantBuffers(0, 1, &oldVB0);
    ID3D11SamplerState* oldVS[2] = {}; ctx->VSGetSamplers(2, 2, oldVS);
    const float z4f[4] = {};
    ctx->ClearRenderTargetView(tauS.rtv.Get(), z4f); ctx->ClearRenderTargetView(litS.rtv.Get(), z4f);
    ctx->ClearRenderTargetView(tauQ.rtv.Get(), z4f); ctx->ClearRenderTargetView(litQ.rtv.Get(), z4f);
    D3D11_VIEWPORT oldVp[1]; UINT nVp = 1; ctx->RSGetViewports(&nVp, oldVp);
    ctx->OMSetBlendState(bsAdd.Get(), nullptr, ~0u);
    ctx->RSSetState(gc->rsNone.Get());
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->VSSetShader(vsGrain.Get(), nullptr, 0); ctx->PSSetShader(psGrain.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &b0); ctx->VSSetConstantBuffers(5, 1, cbuf.GetAddressOf()); ctx->PSSetConstantBuffers(5, 1, cbuf.GetAddressOf());
    ctx->VSSetShaderResources(5, 1, &depth); ctx->VSSetShaderResources(7, 1, &sh);
    ctx->VSSetShaderResources(16, 1, partSRV.GetAddressOf()); ctx->VSSetShaderResources(24, 1, dens.srv.GetAddressOf()); ctx->VSSetShaderResources(25, 1, shade.srv.GetAddressOf());
    ctx->VSSetSamplers(2, 1, &shs); ctx->VSSetSamplers(3, 1, &s3);
    ctx->PSSetShaderResources(5, 1, &depth);
    for (int pass = 0; pass < 1; pass++) {     // (one pass at the full resolution: the particles are small)
        const int q = pass ? 4 : 1;
        k.opt.z = (float)q;
        if (SUCCEEDED(ctx->Map(cbuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &k, sizeof k); ctx->Unmap(cbuf.Get(), 0); }
        ID3D11RenderTargetView* rt2[2] = { pass ? tauQ.rtv.Get() : tauS.rtv.Get(), pass ? litQ.rtv.Get() : litS.rtv.Get() };
        ctx->OMSetRenderTargets(2, rt2, nullptr);
        D3D11_VIEWPORT vp = { 0, 0, (float)std::max(1, scW / q), (float)std::max(1, scH / q), 0, 1 };
        ctx->RSSetViewports(1, &vp);
        ctx->DrawInstanced(4, CAP, 0, 0);
    }
    if (nVp) ctx->RSSetViewports(1, oldVp);
    ctx->VSSetShaderResources(5, 1, ns4); ctx->VSSetShaderResources(7, 1, ns4); ctx->VSSetShaderResources(16, 1, ns4); ctx->VSSetShaderResources(24, 1, ns4); ctx->VSSetShaderResources(25, 1, ns4);
    ctx->VSSetConstantBuffers(0, 1, &oldVB0); ctx->VSSetSamplers(2, 2, oldVS);
    if (oldVB0) oldVB0->Release();
    for (auto* q : oldVS) if (q) q->Release();
    ctx->OMSetRenderTargets(2, oldRT, oldDS);
    for (auto* r : oldRT) if (r) r->Release();
    if (oldDS) oldDS->Release();
    if (b0) b0->Release();
    if (s3) s3->Release();

    // ---- 5: over the scene (the exhaust pass binds no depth target: the depth is read, t5)
    ctx->OMSetBlendState(bsApply.Get(), nullptr, ~0u);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsFull.Get(), nullptr, 0); ctx->PSSetShader(psApply.Get(), nullptr, 0);
    ctx->PSSetShaderResources(5, 1, &depth);
    ID3D11ShaderResourceView* ps5[5] = { shade.srv.Get(), tauS.srv.Get(), litS.srv.Get(), tauQ.srv.Get(), litQ.srv.Get() };
    ctx->PSSetShaderResources(25, 5, ps5);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* ns5[5] = {};
    ctx->PSSetShaderResources(25, 5, ns5);
    {   // the check (every 5 s)
        static double logT = -1e9;
        if (oapiGetSysTime() - logT > 5.0) {
            logT = oapiGetSysTime();
            LOGC("regolith: %u new particles this frame, the ring's head at %u of %u; %d vehicles with a dust body; %d sub-steps; shade box cells %.2f m (reach %.0f m)",
                 nsp, head, CAP, nv, ns, k.box.w, reach);
        }
    }
}

void RegolithRelease()
{
    bodies.clear(); bursts.clear(); spawn.clear();
    csSpawn.Reset(); csStep.Reset(); csVoxel.Reset(); csShade.Reset(); csDensity.Reset();
    vsGrain.Reset(); vsFull.Reset(); psGrain.Reset(); psApply.Reset(); bsApply.Reset(); bsAdd.Reset();
    partBuf.Reset(); spawnBuf.Reset(); hubBuf.Reset(); cbuf.Reset(); partSRV.Reset(); spawnSRV.Reset(); hubSRV.Reset(); partUAV.Reset();
    shadeSum = Vol(); shade = Vol(); count = Vol(); dens = Vol(); tauS = Scr(); litS = Scr(); tauQ = Scr(); litQ = Scr();
    scW = scH = 0; head = 0;
    tried = ok = false; fresh = true;
}
