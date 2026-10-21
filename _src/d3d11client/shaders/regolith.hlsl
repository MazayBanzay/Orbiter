// D3D11Client: soil thrown in a vacuum, grain by grain on its exact parabola (src\Regolith.cpp).
//  - Each row: the soil a source threw at one moment; its grains where their parabolas have them now: p0 + v t - g t^2 / 2
//    (no air: nothing else acts); gone where they land (part of the ground) or where they meet the vessel (its hull,
//    its wheels).
//  - What is thrown (Hsu & Horanyi 2012, Am. J. Phys. 80, 452 - the Apollo 16 "Grand Prix" film): a wide spread of
//    speeds - the dust tip at ~1.1-1.7 x the rover's speed, 45-55 deg up, most of the mass slower and lower - the
//    superposition of all those parabolas is the rooster tail; thrown in bursts of different heights and densities.
//    The soil by mass (Sargeant et al. 2022, LPSC 2040, after Zeng et al. 2010): 469 um 38 %, 55 um 32 %, 12 um 30 % -
//    the fines hold most of the cross-section (kappa = 1.5 / (rho d)).
//  - Drawn grain by grain, sharp: each sample (a share of its row's soil - as many a row as its mass asks; the same
//    samples every frame, each flying on) adds its optical depth where it is - no grid, no time filter:
//      CSVoxel   - the grains' extinction into a coarse box in the world (the dust's own shadow);
//      CSShade   - the box's transmittance towards the Sun (a volumetric shadow map);
//      VSGrain/PSGrain - each sample a soft disc as wide as its row's samples' spacing (a whole sheet, sharp
//                  edges), its optical depth and its share of the Sun (the dust's shade, the hulls' shadow map) summed
//                  by the blend; hidden behind the scene, nothing;
//      PSApply   - per pixel: the transmittance exp(-tau), the light scattered (Hapke's w, a back-scattering phase),
//                  and the dust's shadow on the ground.
#include "exhaust_common.hlsli"

cbuffer Rg : register(b5)
{
    float4 rM0, rM1, rM2;   // local -> camera-centred global: rows; w the offset
    float4x4 rVP;           // camera-centred global -> clip
    float4 rT;              // x now (from the base) [s], y gravity [m/s2], z -, w -
    float4 rOpt;            // x the ground's albedo, y the grains' single-scattering albedo, z kappa [m2/kg], w focal [px]
    float4 rSrc;            // x the rows drawn, y the boxes kept a slot (each 1 + 4 x MAXB float4), z the frame, w the samples
};
cbuffer Vol : register(b6)
{
    float4 vBox;     // xyz the shade box's corner (local) [m], w its cell [m]
    float4 vDim;     // xyz its cells, w the fixed-point scale of the extinction sums
    float4 vFz2;     // w the debug view
};
#define MAXB 12
struct Row { float3 p0; float t0; float3 v0; float m; float3 n; float cone; float3 gs; float gy; float serial, lat, slot, burst; };   // burst: this moment's speed
StructuredBuffer<Row> tRows : register(t4);

float3 ToWorld(float3 p) { return float3(dot(rM0.xyz, p), dot(rM1.xyz, p), dot(rM2.xyz, p)) + float3(rM0.w, rM1.w, rM2.w); }
float3 ToLocal(float3 w) { w -= float3(rM0.w, rM1.w, rM2.w); return float3(dot(float3(rM0.x, rM1.x, rM2.x), w), dot(float3(rM0.y, rM1.y, rM2.y), w), dot(float3(rM0.z, rM1.z, rM2.z), w)); }
float3 DirToLocal(float3 d) { return float3(dot(float3(rM0.x, rM1.x, rM2.x), d), dot(float3(rM0.y, rM1.y, rM2.y), d), dot(float3(rM0.z, rM1.z, rM2.z), d)); }
float3 Rot(float3 v, float3 k, float a) { float c = cos(a), s = sin(a); return v * c + cross(k, v) * s + k * (dot(k, v) * (1 - c)); }
// Henyey-Greenstein relative to isotropic (4 pi p), capped
float HG(float g, float c) { float t = 1 + g * g - 2 * g * c; return min((1 - g * g) / (t * sqrt(t)), 25); }

// grain j of row r in draw 'salt': its start, its velocity (with the vessel's), its share of the shade (false: no grain
// leaves there now). Off a wheel the soil comes off the whole rear and top of the rim at once, in short bursts (a patch
// of the tread full of soil, a bump; src\Regolith.cpp): each point of a rolling rim moves over the ground at
// V (1 - cos phi, sin phi) (phi from the contact, up the rear) - a circle - so the soil of one burst, each grain at the
// burst's share f of its point's velocity, lies at a moment t later on a ring of radius f V t round a centre that went
// f V t the vessel's way and fell g t^2 / 2: the rings the Apollo 16 film shows growing, falling and left behind the
// rover (f < 1/2 puts their centres behind it). Between the bursts a little soil at any share 0.2-1.2. Across the tyre,
// up to ~15 deg sideways (the chevrons); coarse (469 um), medium (55 um) or fine (12 um) by the soil's mass shares
#define RIM_R 0.8          // the wheel's radius [m] (the MPU's; the client's API carries none)
bool Grain(Row r, uint j, uint salt, out float3 p, out float3 v, out float kap)
{
    uint h = ExHash((uint)r.serial * 9781u + j * 6271u + (uint)r.slot * 104729u + salt * 7919u);
    float s = (ExRand(h) + ExRand(h + 1) - 1) * 1.25, q = ExRand(h + 2) * 2 - 1;
    float lat = (ExRand(h + 3) + ExRand(h + 4) - 1) * 0.25;
    float uc = ExRand(h + 6);
    kap = uc < 0.38 ? 0.067 : uc < 0.70 ? 0.58 : 2.63;     // kappa_class / the mix's (1.0, 8.8, 40 m2/kg over 15.3)
    float3 rel = r.v0 - r.gs;
    float speed = length(rel);
    float3 a = rel / max(speed, 1e-4);
    if (r.cone >= 0) {                       // off a wheel: anywhere on the rim from 15 to 160 deg past the contact
        float phi = radians(15 + 145 * ExRand(h + 8));
        float th = phi - (radians(20) + r.cone);                        // from the arc's middle (where a points), up the rim
        float3 d = normalize(Rot(a, r.n, th) + r.n * tan(lat));
        float3 rh = cross(a, r.n);                                       // the hub -> the release point at the arc's middle
        float g1 = (ExRand(h + 9) + ExRand(h + 10) + ExRand(h + 11) - 1.5) * 0.24;   // (~ +-12 % round the burst's share)
        float sf = r.burst > 0 ? r.burst * (1 + g1) : 0.2 + 1.0 * ExRand(h + 5);
        v = (r.gs + d * speed) * sf;                                     // its point's velocity over the ground, the share
        p = r.p0 + r.n * (q * 0.3) + (Rot(rh, r.n, th) - rh) * RIM_R;    // across the tyre; round the rim
        return true;
    }
    // a ring round the axis: a sole's squeeze, a jet's sheet over the ground
    float sf = (0.2 + 1.4 * pow(ExRand(h + 5), 1.8)) * r.burst;
    float cone = -r.cone * (r.lat + (1 - r.lat) * abs(q));               // (lat: the ring's inner edge, a share of its cone)
    float3 b1 = normalize(cross(a, abs(a.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0)));
    float3 d = a * cos(cone) + Rot(b1, a, s * 3.14159265) * sin(cone);
    v = r.gs + d * speed * sf;
    p = r.p0;
    return true;
}
float LandT(float3 p, float3 v, float gy) { const float g = rT.y; return (v.y + sqrt(v.y * v.y + 2 * g * max(p.y - gy, 0))) / g; }

// ------------------------------------------------------------------------------------------ where a grain meets the vessel
// its trajectory in the vessel's frame (it flies v - gs relative to it), in 8 chords over its first 4 s, against the
// vessel's boxes (its hull, its wheels; this frame's, where the vessel was at the throw) - the box it starts in (its own
// wheel) does not stop it
StructuredBuffer<float4> tBoxes : register(t12);
bool SegBox(float3 a, float3 b, float3 h, out float f)
{
    float3 d = b - a, inv = 1 / (abs(d) > 1e-9 ? d : 1e-9);
    float3 ta = (-h - a) * inv, tb = (h - a) * inv;
    float3 lo = min(ta, tb), hi = max(ta, tb);
    float t0 = max(max(lo.x, lo.y), max(lo.z, 0)), t1 = min(min(hi.x, hi.y), min(hi.z, 1));
    f = t0;
    return t0 <= t1;
}
float DeathT(Row r, float3 p, float3 v, float tl)
{
    const float g = rT.y;
    float tMax = min(tl, 4.0), hstep = tMax / 8;
    float3 vr = v - r.gs;
    float age = rT.x - r.t0;
    uint bs = (uint)r.slot * (uint)rSrc.y;
    uint nb = min((uint)tBoxes[bs].x, MAXB);
    float death = 1e6;
    for (uint b = 0; b < nb; b++) {
        float4 c4 = tBoxes[bs + 1 + 4 * b], e0 = tBoxes[bs + 2 + 4 * b], e1 = tBoxes[bs + 3 + 4 * b], e2 = tBoxes[bs + 4 + 4 * b];
        float3 c = c4.xyz - r.gs * age, hb = float3(e0.w, e1.w, e2.w);
        float3 q0 = p - c;
        float3 A = float3(dot(q0, e0.xyz), dot(q0, e1.xyz), dot(q0, e2.xyz));
        if (all(abs(A) < hb)) continue;         // its own wheel
        for (int s = 0; s < 8; s++) {
            float t1 = (s + 1) * hstep;
            if (s * hstep >= death) break;
            float3 q1 = q0 + vr * t1 - float3(0, 0.5 * g * t1 * t1, 0);
            float3 B = float3(dot(q1, e0.xyz), dot(q1, e1.xyz), dot(q1, e2.xyz));
            float f;
            if (SegBox(A, B, hb, f)) { death = min(death, (s + f) * hstep); break; }
            A = B;
        }
    }
    return death;
}

// the Sun's shadow map at a point: the finest cascade that holds it, one compared tap
float RgShadow(float3 wp)
{
    if (gShadow.x < 0.5) return 1;
    [unroll] for (int ci = 0; ci < 4; ci++) {
        if (ci == 0 && gCascFar.x <= -1.5) continue;
        float4 q = mul(float4(wp, 1), gCasc[ci]);
        float2 uv = q.xy * float2(0.5, -0.5) + 0.5;
        if (any(uv < 0.004) || any(uv > 0.996) || q.z > 1 || q.z < 0) continue;
        float2 at = (float2(ci & 1, ci >> 1) + uv) * 0.5;
        return tShadow.SampleCmpLevelZero(sShadow, at, q.z);
    }
    return 1;
}

StructuredBuffer<uint2> tSplat : register(t15);     // (row, its first sample): each row's samples, as many as its mass asks

// sample 'id' of all: its row (a search over the rows' first samples), its index in it, the row's count
bool SampleOf(uint id, out Row r, out uint j, out uint ng)
{
    uint n = (uint)rSrc.x, total = (uint)rSrc.w;
    r = (Row)0; j = 0; ng = 0;
    if (id >= total || n == 0) return false;
    uint lo = 0, hi = n - 1;
    [loop] while (lo < hi) { uint mid = (lo + hi + 1) >> 1; if (tSplat[mid].y <= id) lo = mid; else hi = mid - 1; }
    uint2 e = tSplat[lo];
    uint next = lo + 1 < n ? tSplat[lo + 1].y : total;
    r = tRows[e.x]; j = id - e.y; ng = next - e.y;
    return true;
}
// a sample where it is now: its place (local), its cross-section [m2], its velocity and age (its spread); false: none
bool GrainNow(uint id, out float3 pl, out float X, out float spread)
{
    pl = 0; X = 0; spread = 0;
    Row r; uint j, ng;
    if (!SampleOf(id, r, j, ng)) return false;
    float3 p, v; float kap;
    if (!Grain(r, j, 0u, p, v, kap)) return false;          // the same grains every frame: each one flies on, seen moving
    const float g = rT.y;
    float t = rT.x - r.t0;
    float tl = LandT(p, v, r.gy);
    if (t < 0 || t >= tl) return false;
    if (t >= DeathT(r, p, v, tl)) return false;
    pl = p + v * t - float3(0, 0.5 * g * t * t, 0);
    X = rOpt.z * kap * r.m / ng * saturate((tl - t) / 0.05);
    // the spacing of the row's samples there: its soil spread over a sheet ~(0.3 + 2.5 u t) long, (0.6 + 0.5 u t) across
    float u = length(v - r.gs) + 0.3 * length(r.gs);
    spread = sqrt((0.3 + 2.5 * u * t) * (0.6 + 0.5 * u * t) / ng);
    return true;
}

// ------------------------------------------------------------------------------------------ 1: the grains into the shade box
RWTexture3D<uint> uShadeSum : register(u2);
RWStructuredBuffer<uint4> uSamp : register(u5);    // each sample for the draw: its place (local), its cross-section x 1e4 and spacing (halves)
[numthreads(64, 1, 1)]
void CSVoxel(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)rSrc.w) return;
    float3 pl; float X, sp;
    bool ok = GrainNow(id.x, pl, X, sp);
    uSamp[id.x] = uint4(asuint(pl), ok ? f32tof16(X * 1e4) | (f32tof16(sp) << 16) : 0);
    if (!ok) return;
    float3 gb = (pl - vBox.xyz) / vBox.w - 0.5;
    int3 c0 = int3(floor(gb)); float3 f = gb - c0;
    float val = X / (vBox.w * vBox.w * vBox.w) * vDim.w;
    [unroll] for (int q = 0; q < 8; q++) {
        int3 o = int3(q & 1, (q >> 1) & 1, q >> 2), c = c0 + o;
        if (any(c < 0) || any(c >= int3(vDim.xyz))) continue;
        float3 w3 = o ? f : 1 - f;
        uint add = (uint)min(val * w3.x * w3.y * w3.z + 0.5, 4e8);
        if (add) InterlockedAdd(uShadeSum[c], add);
    }
}

// ------------------------------------------------------------------------------------------ 2: the dust's own shade
// the box's transmittance towards the Sun per cell (a volumetric shadow map), from the summed extinction
Texture3D<uint> tShadeSum : register(t22);
RWTexture3D<float> uShade : register(u3);
[numthreads(4, 4, 4)]
void CSShade(uint3 id : SV_DispatchThreadID)
{
    int3 c = int3(id);
    if (any(c >= int3(vDim.xyz))) return;
    float3 sunL = normalize(DirToLocal(gSunDir.xyz));
    float3 q = c + 0.5;
    float tau = 0;
    [loop] for (int i = 0; i < 64; i++) {
        q += sunL;                                                      // a cell a step
        int3 cc = int3(floor(q));
        if (any(cc < 0) || any(cc >= int3(vDim.xyz))) break;
        tau += tShadeSum.Load(int4(cc, 0)) / vDim.w * vBox.w;
    }
    uShade[c] = exp(-tau);
}

// ------------------------------------------------------------------------------------------ 3: the grains onto the screen
// each sample a small soft disc as wide as the spacing of its row's samples there (so the sheet is whole, its edges and
// rings sharp: no blur past that), its optical depth X / area summed by the blend into two targets: the optical depth
// and its sunlit share (the dust's own shade, the hulls' shadow); hidden behind the scene, nothing
Texture3D<float> tShade : register(t25);
struct GOut { float4 pos : SV_Position; float2 q : TEXCOORD0; float2 ts : TEXCOORD1; float d : TEXCOORD2; };
StructuredBuffer<uint4> tSamp : register(t16);
GOut VSGrain(uint c : SV_VertexID, uint id : SV_InstanceID)
{
    GOut o = (GOut)0;
    o.pos = float4(2, 2, 2, 1);
    uint4 e = tSamp[id];
    if (e.w == 0) return o;
    float3 pl = asfloat(e.xyz);
    float X = f16tof32(e.w & 0xffff) * 1e-4, sp = f16tof32(e.w >> 16);
    float3 wp = ToWorld(pl);
    float4 cl = mul(float4(wp, 1), rVP);
    if (cl.w <= 0.05) return o;
    float d = length(wp);
    uint w, h; tExDepth.GetDimensions(w, h);
    float rpx = clamp(1.2 * sp * rOpt.w / d, 1.0, 4.0);                 // its disc's radius [px]
    float2 cq = float2(c & 1 ? 1 : -1, c & 2 ? 1 : -1);            // (a strip of 4)
    o.pos = cl;
    o.pos.xy += cq * rpx * float2(2.0 / w, 2.0 / h) * cl.w;
    o.q = cq;
    float px = rOpt.w / d;                                              // pixels a metre there
    float tau = X * px * px * 3 / (3.14159265 * rpx * rpx);             // the disc's centre (the kernel 3/(pi R^2) (1 - r^2)^2)
    float3 ub = (pl - vBox.xyz) / (vDim.xyz * vBox.w);
    float sh = (all(ub > 0) && all(ub < 1) ? tShade.SampleLevel(sExLin, ub, 0) : 1) * RgShadow(wp);
    o.ts = float2(tau, sh);
    o.d = cl.z / cl.w;                                                  // its depth (reverse Z: nearer is larger)
    return o;
}
struct GTarg { float tau : SV_Target0; float lit : SV_Target1; };
GTarg PSGrain(GOut i)
{
    float r2 = dot(i.q, i.q);
    if (r2 >= 1) discard;
    if (i.d < tExDepth.Load(int3(i.pos.xy, 0))) discard;               // behind the scene
    float k = (1 - r2) * (1 - r2);
    GTarg o; o.tau = i.ts.x * k; o.lit = i.ts.x * i.ts.y * k;
    return o;
}

// ------------------------------------------------------------------------------------------ 4: over the scene
Texture2D<float> tTau : register(t26);
Texture2D<float> tLit : register(t27);
float4 VSFull(uint vid : SV_VertexID) : SV_Position
{
    float2 uv = float2((vid << 1) & 2, vid & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 PSApply(float4 pos : SV_Position) : SV_Target
{
    int2 p = int2(pos.xy);
    float tau = tTau.Load(int3(p, 0)), lit = tLit.Load(int3(p, 0));
    float3 sw; float sd = ExSceneDist(pos.xy, sw);
    // the ground (or hull) behind, in the dust's shadow - its share lit by the Sun
    float gsh = 1;
    if (sd < 1e7) {
        float3 ub = (ToLocal(sw) - vBox.xyz) / (vDim.xyz * vBox.w);
        if (all(ub > 0) && all(ub < 1)) gsh = lerp(1, tShade.SampleLevel(sExLin, ub, 0), 0.95);
    }
    if (tau < 1e-5 && gsh > 0.998) discard;
    float3 dir = normalize(sw);
    float3 Esun = SunLight(float3(0, 0, 0));
    float ph = HG(-0.3, dot(dir, gSunDir.xyz));
    // single scattering w E p in the scene's units (pi x physical): w ph / 4 per unit of optical depth, the share of it
    // the Sun reaches; the ground's light from below
    float3 S = Esun * (rOpt.y * ph * 0.25) * (lit / max(tau, 1e-9)) + rOpt.y * rOpt.x * 0.08 * Esun * 0.25;
    float T = exp(-tau);
    if (vFz2.w > 0.5) return float4(saturate(tau * 4), saturate(lit * 4), 0, 0.3);   // (test runs, RegolithDbg = 1)
    return float4(S * (1 - T), T * gsh);         // the blend: this + the scene x T
}
