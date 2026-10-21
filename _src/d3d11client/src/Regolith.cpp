// Soil thrown in a vacuum (the Moon, Io, any airless body) - off wheels, soles and rocket jets (shaders\regolith.hlsl).
// Without a gas nothing sorts the grains, nothing stirs them: every grain flies the parabola it left the rim (the sole,
// the jet's sheet) on, the 10-500 um ones alike - the dust trail of the Lunar Roving Vehicle (Apollo 16 film; Hsu &
// Horanyi 2012, Am. J. Phys. 80, 452) is a fan of parabolas, a "rooster tail" that falls back within seconds; no cloud,
// no hanging. Each source sends a row every Dt of simulated time - the soil it threw then; the GPU draws grains of every
// row aloft each frame (a fresh draw each frame), puts each where its parabola has it now (p0 + v t - g t^2 / 2, analytic:
// no simulation, no drift), drops those that landed or met the vessel (its hull, its wheels), and renders the soil's
// optical depth as a participating medium the way Frostbite renders its fog and particles (Hillaire 2015, after
// Wronski 2014): froxels (8 x 8 pixels x 64 slices over where the dust is), a time filter with reprojection, the Sun
// through a volumetric shadow of the dust and the hulls' shadow map, energy-conserving integration, one lookup a pixel.
// Fed from GpuParticles::SpawnSource (SOIL sources) and SpawnJet (the jets' eroded soil) when the air is not dense; the
// aggregates (sticky regolith shed off a tyre) stay particles.
#include "Client.h"
#include "Fx.h"
#include "ExhaustInternal.h"
#include <map>

extern XMMATRIX g_exViewProj;

namespace {
    const int NG = 192;                    // grains drawn a row each frame (at most: a light source draws fewer)
    const UINT RING = 512, MAXS = 24;      // rows kept per source; sources
    const UINT MAXB = 12, BOXN = 1 + 4 * MAXB;   // boxes a vessel; float4 a slot's boxes
    const int FZ = 64, TILE = 8;           // the froxels: slices, pixels across
    const int SX = 96, SY = 48, SZ = 96;   // the dust's shadow box (cells)
    const double FIX = 1e5;                // the extinction sums' fixed point [m]
    struct Row { XMFLOAT3 p0; float t0; XMFLOAT3 v0; float m; XMFLOAT3 n; float cone; XMFLOAT3 gs; float gy; float serial, lat, slot, burst; };   // 80 B
    struct CB { XMFLOAT4 m0, m1, m2; XMFLOAT4X4 vp; XMFLOAT4 t; XMFLOAT4 opt; XMFLOAT4 src; };
    struct VolCB { XMFLOAT4 box, dim, fz, fz2, camD; XMFLOAT4X4 prevVP; XMFLOAT4 prevFz; };
    struct Src { int slot = -1; UINT tail = 0, count = 0; double tNext = 0, last = 0; double land[RING] = {}; double serial = 0; double kgps = 0, dt = 1.0 / 60; };
    std::map<uintptr_t, Src> srcs;
    bool slotUsed[MAXS] = {};
    std::vector<Row> pending;                       // rows born this frame
    std::vector<UINT> pendIdx;                      // their places in the buffer
    double tBase = 0; bool haveBase = false; VECTOR3 lastAP = { 0, 0, 0 };
    std::vector<XMFLOAT4> slotBoxes[MAXS];          // each source's vessel's boxes this frame (local)
    std::vector<Row> mirror(MAXS * RING);           // the rows as the GPU has them (the dust's extent)
    struct Vol { ComPtr<ID3D11Texture3D> t; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav; };
    ComPtr<ID3D11ComputeShader> csVoxel, csShade, csColumn;
    ComPtr<ID3D11VertexShader> vsFull; ComPtr<ID3D11PixelShader> psApply;
    ComPtr<ID3D11BlendState> bsApply;
    ComPtr<ID3D11Buffer> rowBuf, boxBuf, listBuf, rgCB, volCB;
    ComPtr<ID3D11ShaderResourceView> rowSRV, boxSRV, listSRV;
    Vol frox, hist[2], integ, shadeSum, shade;       // the froxels (sums, filtered extinction x 2, integrated); the shadow box
    int fxW = 0, fxH = 0, histCur = 0; bool histValid = false;
    XMFLOAT4X4 prevVP = {}; VECTOR3 prevCam = { 0, 0, 0 }; double prevZn = 1, prevL = 1;
    UINT frameNo = 0;
    bool rgTried = false, rgOk = false;

    // the grains' optical cross-section per kilogram, over the soil's sizes by mass (469 um 38 %, 55 um 32 %, 12 um 30 %;
    // Sargeant et al. 2022 after Zeng et al. 2010; grain density 3100 kg/m3): sum f 1.5 / (rho d) - the fines carry most
    const double KAPPA = 1.5 / 3100.0 * (0.38 / 469e-6 + 0.32 / 55e-6 + 0.30 / 12e-6);
    // a smooth random of time (bursts: the lugs, the bumps), 0..1
    double BurstNoise(double x, uint32_t salt)
    {
        auto h = [&](int64_t i) { uint32_t v = (uint32_t)i * 2654435761u ^ salt * 40503u; v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; return (v & 0xffffff) / 16777216.0; };
        const double fi = floor(x), f = x - fi, s = f * f * (3 - 2 * f);
        return h((int64_t)fi) + (h((int64_t)fi + 1) - h((int64_t)fi)) * s;
    }

    bool MakeVol(ID3D11Device* dev, Vol& v, int x, int y, int z, DXGI_FORMAT f)
    {
        v = Vol();
        D3D11_TEXTURE3D_DESC d = { (UINT)x, (UINT)y, (UINT)z, 1, f, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
        return SUCCEEDED(dev->CreateTexture3D(&d, nullptr, &v.t)) && SUCCEEDED(dev->CreateShaderResourceView(v.t.Get(), nullptr, &v.srv)) &&
               SUCCEEDED(dev->CreateUnorderedAccessView(v.t.Get(), nullptr, &v.uav));
    }

    bool Init(D3D11Client* gc)
    {
        if (rgTried) return rgOk;
        rgTried = true;
        ID3D11Device* dev = gc->Dev();
        auto csh = [&](const char* e, ComPtr<ID3D11ComputeShader>& c) {
            auto b = FxCompile("regolith.hlsl", e, "cs_5_0");
            return b && SUCCEEDED(dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &c));
        };
        auto bf = FxCompile("regolith.hlsl", "VSFull", "vs_5_0"), bq = FxCompile("regolith.hlsl", "PSApply", "ps_5_0");
        if (!csh("CSVoxel", csVoxel) || !csh("CSShade", csShade) || !csh("CSColumn", csColumn) || !bf || !bq ||
            FAILED(dev->CreateVertexShader(bf->GetBufferPointer(), bf->GetBufferSize(), nullptr, &vsFull)) ||
            FAILED(dev->CreatePixelShader(bq->GetBufferPointer(), bq->GetBufferSize(), nullptr, &psApply))) { LOGC("regolith: shaders failed"); return false; }
        auto sbuf = [&](UINT n, UINT stride, D3D11_USAGE use, UINT cpu, ComPtr<ID3D11Buffer>& b, ComPtr<ID3D11ShaderResourceView>& s) {
            D3D11_BUFFER_DESC d = { n * stride, use, D3D11_BIND_SHADER_RESOURCE, cpu, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, stride };
            if (FAILED(dev->CreateBuffer(&d, nullptr, &b))) return false;
            D3D11_SHADER_RESOURCE_VIEW_DESC v = {}; v.Format = DXGI_FORMAT_UNKNOWN; v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; v.Buffer.NumElements = n;
            return SUCCEEDED(dev->CreateShaderResourceView(b.Get(), &v, &s));
        };
        if (!sbuf(MAXS * RING, sizeof(Row), D3D11_USAGE_DEFAULT, 0, rowBuf, rowSRV) ||
            !sbuf(MAXS * BOXN, 16, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE, boxBuf, boxSRV) ||
            !sbuf(MAXS * RING, 8, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE, listBuf, listSRV)) return false;
        D3D11_BUFFER_DESC c = { (UINT)((sizeof(CB) + 15) & ~15u), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(dev->CreateBuffer(&c, nullptr, &rgCB))) return false;
        c.ByteWidth = (UINT)((sizeof(VolCB) + 15) & ~15u);
        if (FAILED(dev->CreateBuffer(&c, nullptr, &volCB))) return false;
        if (!MakeVol(dev, shadeSum, SX, SY, SZ, DXGI_FORMAT_R32_UINT) || !MakeVol(dev, shade, SX, SY, SZ, DXGI_FORMAT_R16_FLOAT)) return false;
        // the dust over the scene: its light + the scene x its transmittance
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
        if (FAILED(dev->CreateBlendState(&bd, &bsApply))) return false;
        LOGC("regolith: vacuum soil grain by grain on its parabola, rendered in froxels (%d px x %d slices; %d grains a row a frame, %u rows a source, kappa %.1f m2/kg)",
             TILE, FZ, NG, RING, KAPPA);
        rgOk = true;
        return true;
    }

    // the froxel volumes for this screen (made again when it changes)
    bool Froxels(D3D11Client* gc)
    {
        const int w = (gc->SceneW() + TILE - 1) / TILE, h = (gc->SceneH() + TILE - 1) / TILE;
        if (w == fxW && h == fxH && frox.t) return true;
        ID3D11Device* dev = gc->Dev();
        fxW = w; fxH = h; histValid = false;
        if (!MakeVol(dev, frox, w, h, FZ, DXGI_FORMAT_R32_UINT) || !MakeVol(dev, hist[0], w, h, FZ, DXGI_FORMAT_R16_FLOAT) ||
            !MakeVol(dev, hist[1], w, h, FZ, DXGI_FORMAT_R16_FLOAT) || !MakeVol(dev, integ, w, h, FZ, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
            fxW = fxH = 0; LOGC("regolith: froxel volumes failed"); return false;
        }
        const double n = (double)w * h * FZ, nb = (double)SX * SY * SZ;
        LOGC("regolith: froxels %d x %d x %d, GPU memory %.1f MB (sums %.1f, filtered x 2 %.1f, integrated %.1f; shadow box %.1f; rows %.1f; none CPU-mapped)",
             w, h, FZ, (n * (4 + 2 + 2 + 8) + nb * 6 + MAXS * RING * sizeof(Row)) / 1048576, n * 4 / 1048576, n * 4 / 1048576, n * 8 / 1048576,
             nb * 6 / 1048576, MAXS * RING * sizeof(Row) / 1048576.0);
        return true;
    }

    void Clear()
    {
        srcs.clear(); pending.clear(); pendIdx.clear();
        for (bool& u : slotUsed) u = false;
        haveBase = false; histValid = false;
    }

    XMFLOAT3 F3(const VECTOR3& v) { return { (float)v.x, (float)v.y, (float)v.z }; }
    VECTOR3 Unit(const VECTOR3& v) { double l = length(v); return l > 0 ? v / l : _V(0, 1, 0); }
    VECTOR3 Rot(const VECTOR3& v, const VECTOR3& k, double a)     // Rodrigues: v about the unit axis k by a
    {
        const double c = cos(a), s = sin(a);
        return v * c + crossp(k, v) * s + k * (dotp(k, v) * (1 - c));
    }
    // when a grain thrown from p at v is back on the ground's level gy [s]
    double LandT(const VECTOR3& p, const VECTOR3& v, double gy, double g)
    {
        const double h = std::max(p.y - gy, 0.0);
        return (v.y + sqrt(v.y * v.y + 2 * g * h)) / g;
    }
}

// a SOIL source in a vacuum this frame: p0 the release point, ax the throw's mid direction (relative to the source),
// gs the vessel's ground velocity, speed the throw's, cone the arc's half-width (< 0.8 rad: along the rim, about the
// axle) or a ring's cone (wider: round the axis, its inner edge ringIn of it), kgps the soil thrown, gy the ground's
// level (all local), boxes the vessel's (local)
void RegolithFeed(const ExScene& sc, uintptr_t key, const VECTOR3& p0, const VECTOR3& ax, const VECTOR3& gs, double speed,
                  double cone, double kgps, double gy, const std::vector<XMFLOAT4>& boxes, double ringIn)
{
    if (!haveBase) { tBase = sc.simt; haveBase = true; lastAP = sc.aP; }
    if (length(sc.aP - lastAP) > 1e-3) { Clear(); tBase = sc.simt; haveBase = true; lastAP = sc.aP; }   // a new anchor frame
    if (sc.dt <= 0 || kgps <= 0 || speed < 0.2) { auto it = srcs.find(key); if (it != srcs.end()) it->second.last = sc.simt; return; }
    Src& s = srcs[key];
    if (s.slot < 0) {
        for (int i = 0; i < (int)MAXS; i++) if (!slotUsed[i]) { s.slot = i; slotUsed[i] = true; break; }
        if (s.slot < 0) { srcs.erase(key); return; }
        s.tNext = sc.simt;
    }
    s.last = sc.simt; s.kgps = kgps;
    slotBoxes[s.slot] = boxes;
    const double g = sc.GM / (sc.R * sc.R);
    const VECTOR3 a = Unit(ax);
    const bool ring = cone >= 0.8;
    VECTOR3 n = ring ? a : crossp(a, _V(0, 1, 0));
    n = length(n) < 1e-3 ? _V(1, 0, 0) : Unit(n);
    // a row every Dt: ~480 rows over the longest flight (the 512 kept), never coarser than 1/15 s
    const VECTOR3 vHi = gs + (ring ? a : Rot(a, n, cone)) * (speed * 1.9);   // (the fastest grains: 1.6 x the rim in a 1.2 x burst)
    const double Tf = LandT(p0, vHi, gy, g);
    const double Dt = std::clamp(Tf / 480.0, 1.0 / 120, 1.0 / 15);
    s.dt = Dt;
    {   // what the sources throw (every 5 s of real time, the fastest of them): the check of the heights and densities
        static double logT = -1e9, vMax = 0, kMax = 0, gMax = 0, tMax = 0;
        if (speed > vMax) { vMax = speed; kMax = kgps; gMax = length(gs); tMax = Tf; }
        if (oapiGetSysTime() - logT > 5.0) {
            logT = oapiGetSysTime();
            LOGC("regolith: fastest throw %.1f m/s off the source (vessel %.1f m/s over the ground), %.2f kg/s, aloft %.1f s, apex ~%.0f m; %d sources",
                 vMax, gMax, kMax, tMax, 0.5 * g * (0.5 * tMax) * (0.5 * tMax), (int)srcs.size());
            vMax = kMax = gMax = tMax = 0;
        }
    }
    if (s.tNext < sc.simt - 1.0) s.tNext = sc.simt;           // a stream starting again
    while (s.tNext <= sc.simt) {
        if (s.count >= RING) { s.tail = (s.tail + 1) % RING; s.count--; }   // the oldest row goes (should have landed)
        const UINT idx = (s.tail + s.count) % RING;
        Row r;
        const double age = sc.simt - s.tNext;                 // born a little before this frame: where the source was then
        const VECTOR3 pb = p0 - gs * age;
        r.p0 = F3(pb); r.t0 = (float)(s.tNext - tBase);
        // a burst: the soil comes off in pulses (the lugs, the bumps; the film shows the dust "produced intermittently with
        // different maximum heights and densities") - the mass 0.3-1.7 x, the speed 0.75-1.2 x, ~4 per second
        const double bm = BurstNoise(s.tNext * 4.0, 17u + s.slot), bv = BurstNoise(s.tNext * 3.0 + 50.0, 91u + s.slot);
        r.v0 = F3(gs + a * speed); r.m = (float)(kgps * Dt * (0.3 + 1.4 * bm));
        r.n = F3(n); r.cone = (float)(ring ? -cone : cone);
        r.gs = F3(gs); r.gy = (float)gy;
        r.serial = (float)fmod(s.serial, 65536.0); r.lat = ring ? (float)ringIn : 0.3f; r.slot = (float)s.slot; r.burst = (float)(0.75 + 0.45 * bv);
        // its last grain down: the row's extreme throws (the arc's ends and past them, sideways, at the tip's speed)
        double tl = 0;
        for (int k = 0; k < 6; k++) {
            const double sa = (k % 3 - 1) * 1.25, la = (k < 3 ? -1 : 1) * 0.25;
            const VECTOR3 d = ring ? Rot(a, Unit(crossp(a, _V(0.3, 0.1, 0.9))), cone * (k % 2 ? ringIn : 1.0))
                                   : Unit(Rot(a, n, cone * sa) + n * tan(la));
            tl = std::max(tl, LandT(pb, gs + d * (speed * 1.6 * r.burst), gy, g));
        }
        s.land[idx] = s.tNext + tl + 0.1;
        pending.push_back(r); pendIdx.push_back(s.slot * RING + idx); mirror[s.slot * RING + idx] = r;
        s.count++; s.serial += 1;
        s.tNext += Dt;
    }
}

void RegolithRender(D3D11Client* gc, const ExScene& sc, ID3D11ShaderResourceView* depth)
{
    // the rows all down go; sources not fed for a second and with nothing aloft are freed
    for (auto it = srcs.begin(); it != srcs.end();) {
        Src& s = it->second;
        while (s.count > 0 && s.land[s.tail] < sc.simt) { s.tail = (s.tail + 1) % RING; s.count--; }
        if (s.count == 0 && sc.simt - s.last > 1.0) { if (s.slot >= 0) slotUsed[s.slot] = false; it = srcs.erase(it); }
        else ++it;
    }
    // nothing aloft: no pass at all (no clears, no dispatches)
    if (srcs.empty() || !sc.anchored || !Init(gc) || !Froxels(gc)) { pending.clear(); pendIdx.clear(); histValid = false; return; }
    ID3D11DeviceContext* ctx = gc->Ctx();
    for (size_t i = 0; i < pending.size(); i++) {
        D3D11_BOX b = { pendIdx[i] * (UINT)sizeof(Row), 0, 0, (pendIdx[i] + 1) * (UINT)sizeof(Row), 1, 1 };
        ctx->UpdateSubresource(rowBuf.Get(), 0, &b, &pending[i], 0, 0);
    }
    pending.clear(); pendIdx.clear();
    const double g = sc.GM / (sc.R * sc.R);

    // ---- the grains to draw, and the dust's extent: every row's grains lie within ~(its speed x 1.2 x its burst) x t of
    // where the source was carried with them (the bulk: ~85 % of the mass; a grain still aloft at t rose at least g t / 2,
    // so it went at most sqrt(v^2 - (g t / 2)^2) across), less the fall
    const double tNow = sc.simt - tBase;
    VECTOR3 lo = _V(1e9, 1e9, 1e9), hi = _V(-1e9, -1e9, -1e9);
    std::vector<XMUINT2> list;
    for (auto& kv : srcs) {
        const Src& s = kv.second;
        if (s.count < 1) continue;
        const UINT ng = (UINT)std::clamp(NG * sqrt(std::max(s.kgps, 0.0) / 1.0), 24.0, (double)NG);
        for (UINT i = 0; i < s.count; i++) {
            const UINT ri = s.slot * RING + (s.tail + i) % RING;
            list.push_back({ ri, ng });
            if (i % 4 && i + 1 != s.count) continue;
            const Row& r = mirror[ri];
            const double t = std::max(tNow - r.t0, 0.0);
            const VECTOR3 gs = _V(r.gs.x, r.gs.y, r.gs.z), rel = _V(r.v0.x, r.v0.y, r.v0.z) - gs;
            const double vm = length(rel) * 1.2 * r.burst;
            const double radH = t * sqrt(std::max(vm * vm - 0.25 * g * g * t * t, 0.0)) + 0.6;
            const VECTOR3 c = _V(r.p0.x, r.p0.y, r.p0.z) + gs * t;
            const double top = c.y + vm * t - 0.5 * g * t * t + 0.6;
            if (top < r.gy) continue;
            lo = _V(std::min(lo.x, c.x - radH), std::min(lo.y, (double)r.gy - 0.5), std::min(lo.z, c.z - radH));
            hi = _V(std::max(hi.x, c.x + radH), std::max(hi.y, top), std::max(hi.z, c.z + radH));
        }
    }
    if (list.empty() || hi.x <= lo.x) { histValid = false; return; }
    {   // round the focus (the frame's origin lies on the mean sphere, the ground may be kilometres off it)
        VECTOR3 fg = sc.cp; if (OBJHANDLE hf = oapiGetFocusObject()) oapiGetGlobalPos(hf, &fg);
        const VECTOR3 f = sc.ToLocal(fg);
        lo = _V(std::max(lo.x, f.x - 300), std::max(lo.y, f.y - 300), std::max(lo.z, f.z - 300));
        hi = _V(std::min(hi.x, f.x + 300), std::min(hi.y, f.y + 300), std::min(hi.z, f.z + 300));
        if (hi.x <= lo.x || hi.y <= lo.y || hi.z <= lo.z) { histValid = false; return; }
    }
    // the shadow box's cell (steps of 2^(1/4) from 15 cm, its corner on its grid: no crawl as the cloud grows)
    const double hRaw = std::max({ (hi.x - lo.x) / SX, (hi.y - lo.y) / SY, (hi.z - lo.z) / SZ, 0.15 });
    const double hb = 0.15 * pow(2.0, ceil(4 * log2(hRaw / 0.15)) / 4);
    // the froxels' depth range: over the dust as the camera sees it (its box's nearest and farthest points)
    const VECTOR3 cam = sc.ToLocal(sc.cp);
    double dn = 0, df = 0;
    {
        const VECTOR3 q = _V(std::clamp(cam.x, lo.x, hi.x), std::clamp(cam.y, lo.y, hi.y), std::clamp(cam.z, lo.z, hi.z));
        dn = length(q - cam);
        for (int k = 0; k < 8; k++) df = std::max(df, length(_V(k & 1 ? hi.x : lo.x, k & 2 ? hi.y : lo.y, k & 4 ? hi.z : lo.z) - cam));
    }
    const double zn = std::max(0.3, 0.9 * dn), zf = std::clamp(df * 1.05, zn * 2, 3000.0);
    const double focal = 0.5 * gc->SceneH() / std::max(oapiCameraAperture() > 0 ? tan(oapiCameraAperture()) : 1.0, 1e-3);

    CB k = {};
    VECTOR3 c0 = mul(sc.Rp, sc.a1), c1 = mul(sc.Rp, sc.aU), c2 = mul(sc.Rp, sc.a2);
    VECTOR3 o = mul(sc.Rp, sc.aP) + sc.pp - sc.cp;
    k.m0 = { (float)c0.x, (float)c1.x, (float)c2.x, (float)o.x };
    k.m1 = { (float)c0.y, (float)c1.y, (float)c2.y, (float)o.y };
    k.m2 = { (float)c0.z, (float)c1.z, (float)c2.z, (float)o.z };
    XMFLOAT4X4 vpNow; XMStoreFloat4x4(&vpNow, XMMatrixTranspose(g_exViewProj));
    k.vp = vpNow;
    k.t = { (float)tNow, (float)g, 0, 0 };
    // the ground's albedo the grains have (the body's), their single-scattering albedo (Hapke: a loose surface darkens
    // itself by shadowing and multiple scattering among its grains - the grains alone are 2-3 x lighter), kappa
    char nm[64] = ""; if (sc.hBody) oapiGetObjectName(sc.hBody, nm, 63);
    double alb = !strcmp(nm, "Moon") ? 0.30 : !strcmp(nm, "Io") ? 0.45 : !strcmp(nm, "Mercury") ? 0.25 : 0.25;   // (as the client's tiles show it)
    k.opt = { (float)alb, (float)std::min(0.95, 2.2 * alb), (float)KAPPA, (float)focal };
    const UINT nl = (UINT)std::min<size_t>(list.size(), MAXS * RING);
    k.src = { (float)nl, (float)BOXN, (float)(++frameNo % 65536), 0 };
    VolCB v = {};
    v.box = { (float)(floor(lo.x / hb) * hb), (float)(floor(lo.y / hb) * hb), (float)(floor(lo.z / hb) * hb), (float)hb };
    v.dim = { (float)SX, (float)SY, (float)SZ, (float)FIX };
    v.fz = { (float)zn, (float)log(zf / zn), (float)fxW, (float)fxH };
    // the time filter: a tenth of the new frame (Frostbite 1/20; the dust moves fast: a longer memory trails it); none after a gap
    double FxTestNum(const char* key);
    static const float dbg = (float)FxTestNum("RegolithDbg");     // (test runs only: the debug view)
    v.fz2 = { (float)FZ, (float)TILE, histValid ? 0.1f : 1.0f, dbg };
    const VECTOR3 dc = sc.cp - prevCam;
    v.camD = { (float)dc.x, (float)dc.y, (float)dc.z, 0 };
    v.prevVP = histValid ? prevVP : vpNow;
    v.prevFz = { (float)(histValid ? prevZn : zn), (float)(histValid ? prevL : log(zf / zn)), 0, 0 };
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(rgCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, &k, sizeof k); ctx->Unmap(rgCB.Get(), 0);
    if (FAILED(ctx->Map(volCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, &v, sizeof v); ctx->Unmap(volCB.Get(), 0);
    if (FAILED(ctx->Map(listBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, list.data(), nl * 8); ctx->Unmap(listBuf.Get(), 0);
    if (FAILED(ctx->Map(boxBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    {
        XMFLOAT4* bx = (XMFLOAT4*)m.pData;
        for (UINT i = 0; i < MAXS; i++) {
            const std::vector<XMFLOAT4>& bv = slotBoxes[i];
            const UINT nb = (UINT)std::min<size_t>(bv.size() / 4, MAXB);
            bx[i * BOXN] = { (float)nb, 0, 0, 0 };
            for (UINT q = 0; q < nb * 4; q++) bx[i * BOXN + 1 + q] = bv[q];
        }
    }
    ctx->Unmap(boxBuf.Get(), 0);

    // ---- the compute passes: the frame's constants (b0), the FX light (b4, t8), the shadow map (t7, s2) and the linear
    // sampler (s3) as the pixel shaders have them
    ID3D11Buffer* b0 = nullptr; ctx->PSGetConstantBuffers(0, 1, &b0);
    ID3D11Buffer* b4 = nullptr; ctx->PSGetConstantBuffers(4, 1, &b4);
    ID3D11ShaderResourceView* t8 = nullptr; ctx->PSGetShaderResources(8, 1, &t8);
    ID3D11SamplerState* s3 = nullptr; ctx->PSGetSamplers(3, 1, &s3);
    ID3D11ShaderResourceView* sh = gc->ShadowSRV(); ID3D11SamplerState* shs = gc->ShadowSampler();
    ctx->CSSetConstantBuffers(0, 1, &b0); ctx->CSSetConstantBuffers(4, 1, &b4);
    ID3D11Buffer* cb56[2] = { rgCB.Get(), volCB.Get() }; ctx->CSSetConstantBuffers(5, 2, cb56);
    ctx->CSSetShaderResources(7, 1, &sh); ctx->CSSetShaderResources(8, 1, &t8);
    ctx->CSSetSamplers(2, 1, &shs); ctx->CSSetSamplers(3, 1, &s3);
    ID3D11UnorderedAccessView* nu[6] = {}; ID3D11ShaderResourceView* ns[4] = {};
    const UINT zero4[4] = {};
    ctx->ClearUnorderedAccessViewUint(frox.uav.Get(), zero4);
    ctx->ClearUnorderedAccessViewUint(shadeSum.uav.Get(), zero4);
    {   // 1: the grains into the froxels and the shadow box
        ctx->CSSetShaderResources(4, 1, rowSRV.GetAddressOf());
        ctx->CSSetShaderResources(12, 1, boxSRV.GetAddressOf());
        ctx->CSSetShaderResources(15, 1, listSRV.GetAddressOf());
        ID3D11UnorderedAccessView* u[2] = { frox.uav.Get(), shadeSum.uav.Get() };
        ctx->CSSetUnorderedAccessViews(1, 2, u, nullptr);
        ctx->CSSetShader(csVoxel.Get(), nullptr, 0);
        ctx->Dispatch((nl * NG + 63) / 64, 1, 1);
        ctx->CSSetUnorderedAccessViews(1, 2, nu, nullptr);
        ctx->CSSetShaderResources(4, 1, ns); ctx->CSSetShaderResources(12, 1, ns); ctx->CSSetShaderResources(15, 1, ns);
    }
    {   // 2: the dust's own shade towards the Sun
        ctx->CSSetShaderResources(22, 1, shadeSum.srv.GetAddressOf());
        ctx->CSSetUnorderedAccessViews(3, 1, shade.uav.GetAddressOf(), nullptr);
        ctx->CSSetShader(csShade.Get(), nullptr, 0);
        ctx->Dispatch(SX / 4, SY / 4, SZ / 4);
        ctx->CSSetUnorderedAccessViews(3, 1, nu, nullptr);
        ctx->CSSetShaderResources(22, 1, ns);
    }
    const int cur = histCur, prev = histCur ^ 1;
    {   // 3: the columns - the time filter, the light, the integration
        ID3D11ShaderResourceView* sr[3] = { frox.srv.Get(), hist[prev].srv.Get(), shade.srv.Get() };
        ctx->CSSetShaderResources(23, 3, sr);
        ID3D11UnorderedAccessView* u[2] = { hist[cur].uav.Get(), integ.uav.Get() };
        ctx->CSSetUnorderedAccessViews(4, 2, u, nullptr);
        ctx->CSSetShader(csColumn.Get(), nullptr, 0);
        ctx->Dispatch((fxW + 7) / 8, (fxH + 7) / 8, 1);
        ctx->CSSetUnorderedAccessViews(4, 2, nu, nullptr);
        ctx->CSSetShaderResources(23, 3, ns);
    }
    ctx->CSSetShader(nullptr, nullptr, 0);
    ctx->CSSetShaderResources(7, 1, ns); ctx->CSSetShaderResources(8, 1, ns);
    ID3D11Buffer* nb1[1] = {}; ctx->CSSetConstantBuffers(0, 1, nb1); ctx->CSSetConstantBuffers(4, 1, nb1);
    if (b0) b0->Release();
    if (b4) b4->Release();
    if (t8) t8->Release();
    if (s3) s3->Release();
    histCur ^= 1; histValid = true;
    prevVP = vpNow; prevCam = sc.cp; prevZn = zn; prevL = log(zf / zn);

    // ---- 4: over the scene (the exhaust pass binds no depth target: the depth is read, t5)
    ctx->OMSetBlendState(bsApply.Get(), nullptr, ~0u);
    ctx->RSSetState(gc->rsNone.Get());
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsFull.Get(), nullptr, 0); ctx->PSSetShader(psApply.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(5, 2, cb56);
    ctx->PSSetShaderResources(5, 1, &depth);
    ctx->PSSetShaderResources(25, 1, shade.srv.GetAddressOf());
    ctx->PSSetShaderResources(26, 1, integ.srv.GetAddressOf());
    ctx->Draw(3, 0);
    ctx->PSSetShaderResources(25, 2, ns);
    {   // the check (every 5 s): the depth range, the samples
        static double logT = -1e9;
        if (oapiGetSysTime() - logT > 5.0) {
            logT = oapiGetSysTime();
            LOGC("regolith: froxels over %.1f-%.0f m (%.2f m deep at 10 m), shadow box %.0f x %.0f x %.0f m (%.2f m cells); %u rows, ~%u grains a frame",
                 zn, zf, 10 * log(zf / zn) / FZ, SX * hb, SY * hb, SZ * hb, hb, nl, nl * NG);
        }
    }
}

void RegolithRelease()
{
    Clear();
    csVoxel.Reset(); csShade.Reset(); csColumn.Reset(); vsFull.Reset(); psApply.Reset(); bsApply.Reset();
    rowBuf.Reset(); boxBuf.Reset(); listBuf.Reset(); rgCB.Reset(); volCB.Reset(); rowSRV.Reset(); boxSRV.Reset(); listSRV.Reset();
    frox = Vol(); hist[0] = Vol(); hist[1] = Vol(); integ = Vol(); shadeSum = Vol(); shade = Vol();
    for (auto& b : slotBoxes) b.clear();
    fxW = fxH = 0;
    rgTried = rgOk = false;
}
