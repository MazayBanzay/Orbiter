// D3D11Client: soil thrown in a vacuum, grain by grain on its exact parabola (src\Regolith.cpp).
//  - Each row: the soil a source threw at one moment; its grains where their parabolas have them now: p0 + v t - g t^2 / 2
//    (no air: nothing else acts); gone where they land (part of the ground) or where they meet the vessel (its hull,
//    its wheels).
//  - What is thrown (Hsu & Horanyi 2012, Am. J. Phys. 80, 452 - the Apollo 16 "Grand Prix" film): a wide spread of
//    speeds - the dust tip at ~1.1-1.7 x the rover's speed, 45-55 deg up, most of the mass slower and lower - the
//    superposition of all those parabolas is the rooster tail; thrown in bursts of different heights and densities.
//    The soil by mass (Sargeant et al. 2022, LPSC 2040, after Zeng et al. 2010): 469 um 38 %, 55 um 32 %, 12 um 30 % -
//    the fines hold most of the cross-section (kappa = 1.5 / (rho d)).
//  - A grain (10-500 um) is far under a pixel: no grain is seen, only the soil's optical depth. It is rendered as a
//    participating medium the way Frostbite renders its fog and particles (Hillaire 2015, after Wronski 2014):
//      CSVoxel   - each frame a fresh draw of grains from every row (new samples of the same distribution, so the time
//                  average below converges on it) adds its extinction into a frustum-aligned grid (froxels: 8 x 8
//                  pixels x 64 slices, exponential in depth over where the dust is), trilinear, atomic, normalized by
//                  the froxel's volume; and into a coarse box in the world (the dust's own shadow);
//      CSShade   - the box's transmittance towards the Sun (a volumetric shadow map);
//      CSColumn  - per froxel column, front to back: the extinction blended with its reprojected history (the time
//                  filter that takes the sampling noise out), lit by the Sun through the dust's shade and the hulls'
//                  shadow map, integrated energy-conserving across each froxel (Hillaire's fix of the light leak);
//      PSApply   - one lookup per pixel at the scene's depth: the light in front and the transmittance, and the dust's
//                  shadow on the ground.
#include "exhaust_common.hlsli"

cbuffer Rg : register(b5)
{
    float4 rM0, rM1, rM2;   // local -> camera-centred global: rows; w the offset
    float4x4 rVP;           // camera-centred global -> clip
    float4 rT;              // x now (from the base) [s], y gravity [m/s2], z -, w -
    float4 rOpt;            // x the ground's albedo, y the grains' single-scattering albedo, z kappa [m2/kg], w focal [px]
    float4 rSrc;            // x the samples' list length, y the boxes kept a slot (each 1 + 4 x MAXB float4), z the frame
};
cbuffer Vol : register(b6)
{
    float4 vBox;     // xyz the shade box's corner (local) [m], w its cell [m]
    float4 vDim;     // xyz its cells, w the fixed-point scale of the extinction sums
    float4 vFz;      // x near [m], y ln(far / near), z froxels across, w froxels down
    float4 vFz2;     // x slices, y the froxel's size [px], z the history's weight of the new frame (1: none), w debug view
    float4 vCamD;    // xyz the camera's move since the last frame (global) [m]
    float4x4 vPrevVP;   // last frame's camera-centred global -> clip
    float4 vPrevFz;  // last frame's x near, y ln(far / near)
};
#define NG 192
#define MAXB 12
struct Row { float3 p0; float t0; float3 v0; float m; float3 n; float cone; float3 gs; float gy; float serial, lat, slot, burst; };   // burst: this moment's speed
StructuredBuffer<Row> tRows : register(t4);

float3 ToWorld(float3 p) { return float3(dot(rM0.xyz, p), dot(rM1.xyz, p), dot(rM2.xyz, p)) + float3(rM0.w, rM1.w, rM2.w); }
float3 ToLocal(float3 w) { w -= float3(rM0.w, rM1.w, rM2.w); return float3(dot(float3(rM0.x, rM1.x, rM2.x), w), dot(float3(rM0.y, rM1.y, rM2.y), w), dot(float3(rM0.z, rM1.z, rM2.z), w)); }
float3 DirToLocal(float3 d) { return float3(dot(float3(rM0.x, rM1.x, rM2.x), d), dot(float3(rM0.y, rM1.y, rM2.y), d), dot(float3(rM0.z, rM1.z, rM2.z), d)); }
float3 Rot(float3 v, float3 k, float a) { float c = cos(a), s = sin(a); return v * c + cross(k, v) * s + k * (dot(k, v) * (1 - c)); }
// Henyey-Greenstein relative to isotropic (4 pi p), capped
float HG(float g, float c) { float t = 1 + g * g - 2 * g * c; return min((1 - g * g) / (t * sqrt(t)), 25); }

// grain j of row r in draw 'salt': its start, its velocity (with the vessel's), its share of the shade. The soil leaves
// the lugs over the arc and a little past its ends (triangular), across the tyre, up to ~15 deg sideways (the
// chevrons), at 0.2-1.6 x the rim's speed - most of it slow (it slides off the tread), the lugs' flung tip fast - times
// this moment's burst; coarse (469 um), medium (55 um) or fine (12 um) by the soil's mass shares
void Grain(Row r, uint j, uint salt, out float3 p, out float3 v, out float kap)
{
    uint h = ExHash((uint)r.serial * 9781u + j * 6271u + (uint)r.slot * 104729u + salt * 7919u);
    float s = (ExRand(h) + ExRand(h + 1) - 1) * 1.25, q = ExRand(h + 2) * 2 - 1;
    float lat = (ExRand(h + 3) + ExRand(h + 4) - 1) * 0.25;
    float sf = (0.2 + 1.4 * pow(ExRand(h + 5), 1.8)) * r.burst;
    float uc = ExRand(h + 6);
    kap = uc < 0.38 ? 0.067 : uc < 0.70 ? 0.58 : 2.63;     // kappa_class / the mix's (1.0, 8.8, 40 m2/kg over 15.3)
    float3 rel = r.v0 - r.gs;
    float speed = length(rel);
    float3 a = rel / max(speed, 1e-4);
    float3 d, off = 0;
    if (r.cone >= 0) {                       // along the rim's arc: turned about the axle; the chevrons throw it out
        d = normalize(Rot(a, r.n, r.cone * s) + r.n * tan(lat));
        off = r.n * (q * r.lat);
    } else {                                 // a ring round the axis: a sole's squeeze, a jet's sheet over the ground
        float cone = -r.cone * (r.lat + (1 - r.lat) * abs(q));            // (lat: the ring's inner edge, a share of its cone)
        float3 b1 = normalize(cross(a, abs(a.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0)));
        d = a * cos(cone) + Rot(b1, a, s * 3.14159265) * sin(cone);
    }
    v = r.gs + d * speed * sf;
    p = r.p0 + off;
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

// the froxel grid: depth slices exponential from near to far
float SliceOf(float d) { return log(max(d, 1e-3) / vFz.x) / vFz.y * vFz2.x; }          // continuous [slices]
float DepthOf(float s) { return vFz.x * exp(s / vFz2.x * vFz.y); }

// ------------------------------------------------------------------------------------------ 1: the grains into the grids
StructuredBuffer<uint2> tSplat : register(t15);     // (row, grains drawn of it)
RWTexture3D<uint> uFrox : register(u1);
RWTexture3D<uint> uShadeSum : register(u2);
[numthreads(64, 1, 1)]
void CSVoxel(uint3 id : SV_DispatchThreadID)
{
    uint k = id.x / NG, j = id.x % NG;
    if (k >= (uint)rSrc.x) return;
    uint2 e = tSplat[k];
    if (j >= e.y) return;
    Row r = tRows[e.x];
    float3 p, v; float kap; Grain(r, j, (uint)rSrc.z, p, v, kap);
    const float g = rT.y;
    float t = rT.x - r.t0;
    float tl = LandT(p, v, r.gy);
    if (t < 0 || t >= tl) return;
    if (t >= DeathT(r, p, v, tl)) return;
    float3 pl = p + v * t - float3(0, 0.5 * g * t * t, 0);
    float X = rOpt.z * kap * r.m / e.y * saturate((tl - t) / 0.05);     // its cross-section [m2]
    // the shade box (the dust's own shadow): its cell's share
    float3 gb = (pl - vBox.xyz) / vBox.w - 0.5;
    {
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
    // the froxels: where the camera sees it; its extinction is its cross-section over the froxel's volume there
    float3 wp = ToWorld(pl);
    float4 cl = mul(float4(wp, 1), rVP);
    if (cl.w <= 0) return;
    float2 ndc = cl.xy / cl.w;
    if (any(abs(ndc) > 1.05)) return;
    float d = length(wp);
    float sl = SliceOf(d);
    if (sl < -0.5 || sl > vFz2.x - 0.5) return;
    float3 gf = float3((ndc.x * 0.5 + 0.5) * vFz.z, (0.5 - ndc.y * 0.5) * vFz.w, sl) - 0.5;
    float side = vFz2.y * d / rOpt.w;                                   // the froxel's width there [m]
    float thick = d * vFz.y / vFz2.x;                                   // its depth [m]
    float val = X / (side * side * thick) * vDim.w;
    int3 c0 = int3(floor(gf)); float3 f = gf - c0;
    int3 dim = int3(vFz.z, vFz.w, vFz2.x);
    [unroll] for (int q = 0; q < 8; q++) {
        int3 o = int3(q & 1, (q >> 1) & 1, q >> 2), c = c0 + o;
        if (any(c < 0) || any(c >= dim)) continue;
        float3 w3 = o ? f : 1 - f;
        uint add = (uint)min(val * w3.x * w3.y * w3.z + 0.5, 4e8);
        if (add) InterlockedAdd(uFrox[c], add);
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

// ------------------------------------------------------------------------------------------ 3: the columns
Texture3D<uint> tFrox : register(t23);
Texture3D<float> tHist : register(t24);           // last frame's filtered extinction
Texture3D<float> tShade : register(t25);
RWTexture3D<float> uExtOut : register(u4);        // this frame's filtered extinction (the next one's history)
RWTexture3D<float4> uInteg : register(u5);        // the light in front of each slice's far side, the transmittance to it
[numthreads(8, 8, 1)]
void CSColumn(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)vFz.z || id.y >= (uint)vFz.w) return;
    float2 ndc = float2((id.x + 0.5) / vFz.z * 2 - 1, 1 - (id.y + 0.5) / vFz.w * 2);
    float4 q = mul(float4(ndc, 1e-3, 1), gInvVP);
    float3 dir = normalize(q.xyz / q.w);
    float3 Esun = SunLight(float3(0, 0, 0));
    float ph = HG(-0.3, dot(dir, gSunDir.xyz));
    // single scattering w E p in the scene's units (pi x physical): w ph / 4 per unit of optical depth; the ground's
    // light from below
    float3 Lsun = Esun * (rOpt.y * ph * 0.25), Lamb = rOpt.y * rOpt.x * 0.08 * Esun * 0.25;
    float3 L = 0; float T = 1;
    float3 boxSize = vDim.xyz * vBox.w;
    const float a = vFz2.z;
    [loop] for (uint s = 0; s < (uint)vFz2.x; s++) {
        float d0 = DepthOf(s), d1 = DepthOf(s + 1.0), dm = DepthOf(s + 0.5);
        float3 wp = dir * dm;
        float ext = tFrox.Load(int4(id.x, id.y, s, 0)) / vDim.w;
        // the time filter: last frame's value where this froxel was then (the camera moved; the dust's own motion
        // smears a few frames - a camera's exposure)
        if (a < 0.999) {
            float3 wpp = wp + vCamD.xyz;
            float4 cp = mul(float4(wpp, 1), vPrevVP);
            if (cp.w > 0) {
                float2 np = cp.xy / cp.w;
                float sp = log(max(length(wpp), 1e-3) / vPrevFz.x) / vPrevFz.y * vFz2.x;
                float3 uvw = float3(np.x * 0.5 + 0.5, 0.5 - np.y * 0.5, (sp + 0.5) / vFz2.x);
                if (all(uvw > 0) && all(uvw < 1)) ext = lerp(tHist.SampleLevel(sExLin, uvw, 0), ext, a);
            }
        }
        uExtOut[int3(id.x, id.y, s)] = ext;
        if (ext > 1e-5) {
            float3 ql = ToLocal(wp) - vBox.xyz;
            float3 ub = ql / boxSize;
            float sh = all(ub > 0) && all(ub < 1) ? tShade.SampleLevel(sExLin, ub, 0) : 1;
            float3 S = Lsun * sh * RgShadow(wp) + Lamb;
            float tr = exp(-ext * (d1 - d0));
            L += T * S * (1 - tr);                    // across the froxel with its own transmittance (no light leak)
            T *= tr;
        }
        uInteg[int3(id.x, id.y, s)] = float4(L, T);
    }
}

// ------------------------------------------------------------------------------------------ 4: over the scene
Texture3D<float4> tInteg : register(t26);
float4 VSFull(uint vid : SV_VertexID) : SV_Position
{
    float2 uv = float2((vid << 1) & 2, vid & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 PSApply(float4 pos : SV_Position) : SV_Target
{
    float3 sw; float sd = ExSceneDist(pos.xy, sw);
    uint w, h; tExDepth.GetDimensions(w, h);
    float2 uv = pos.xy / float2(w, h);
    // the integrated value at the scene's depth (each slice holds it at its far side); in front of the first, a share of it
    float sl = SliceOf(min(sd, 1e7)) - 1.0;
    float4 iv;
    if (sl < 0) {
        float4 i0 = tInteg.SampleLevel(sExLin, float3(uv, 0.5 / vFz2.x), 0);
        float f = saturate(sl + 1);
        iv = lerp(float4(0, 0, 0, 1), i0, f);
    } else iv = tInteg.SampleLevel(sExLin, float3(uv, (min(sl, vFz2.x - 1) + 0.5) / vFz2.x), 0);
    // the ground (or hull) behind, in the dust's shadow - its share lit by the Sun (in a vacuum nearly all)
    float gsh = 1;
    if (sd < 1e7) {
        float3 ub = (ToLocal(sw) - vBox.xyz) / (vDim.xyz * vBox.w);
        if (all(ub > 0) && all(ub < 1)) gsh = lerp(1, tShade.SampleLevel(sExLin, ub, 0), 0.95);
    }
    float T = iv.a * gsh;
    if (vFz2.w > 0.5) {                          // (test runs, RegolithDbg = 1: red where the dust takes light, green its light, blue its extinction raw)
        float raw = tInteg.SampleLevel(sExLin, float3(uv, 1 - 0.5 / vFz2.x), 0).a;
        return float4(saturate(1 - iv.a) * 4, saturate(dot(iv.rgb, 1) * 4), saturate(1 - raw) * 4, 0.3);
    }
    if (T > 0.998 && dot(iv.rgb, 1) < 1e-4) discard;
    return float4(iv.rgb, T);                    // the blend: this + the scene x T
}
