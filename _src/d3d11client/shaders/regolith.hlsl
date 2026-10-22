// D3D11Client: soil thrown in a vacuum (src\Regolith.cpp) - the dust off the wheels and the rocket jets on the Moon (any
// airless body), as the mockup worked it out (regolith mockup, 2026-10-22). Particles on the GPU, each a share of the
// soil's mass (its optical weight kappa m), flown ballistically in the particles' local frame (y up):
//  - CSSpawn  - the frame's new particles (Regolith.cpp: the wheels' tails and the jets' sheets) into the ring;
//  - CSStep   - g down, in sub-steps of <= 8 ms; collisions with every vehicle that has a dust body (its mesh's static parts
//               as a distance field, its wheels as turning cylinders: tools\build_dust_sdf.py) - the step walked in 4 cm
//               pieces near the mesh, at the first within 5 cm while closing on it the dust bounces (restitution 0.25,
//               friction 0.45) relative to the moving vehicle; what reaches the ground stays there;
//  - CSVoxel / CSShade - the optical weights into a box round the focus, its transmittance towards the Sun per cell (a
//               volumetric shadow map: the dust's own shade and its shadow on the ground);
//  - VSGrain / PSGrain - each particle a small soft Gaussian (sigma growing with its age: the spread of the soil it stands
//               for) summed into two targets, the optical depth and its sunlit share; hidden behind the scene, nothing;
//  - PSApply  - per pixel: the transmittance exp(-tau), the light scattered (Hapke's w ~0.28, a back-scattering phase),
//               the dust's shadow on what lies behind it.
#include "exhaust_common.hlsli"

#define MAXV 4
cbuffer Rg : register(b5)
{
    float4 rM0, rM1, rM2;   // local -> camera-centred global: rows; w the offset
    float4x4 rVP;           // camera-centred global -> clip
    float4 rT;              // x the sub-step [s], y gravity [m/s2], z sub-steps, w the ring's capacity
    float4 rOpt;            // x the ground's albedo, y the grains' single-scattering albedo, z the draw: 1 the small (full
                            // resolution), 4 the wide (a quarter's), w focal [px]
    float4 rSpawn;          // x new particles, y the ring's head, z vehicles, w debug view
    float4 vBox;            // xyz the shade box's corner (local) [m], w its cell [m]
    float4 vDim;            // xyz its cells, w the fixed-point scale of the sums
    // per vehicle: local -> vessel (rows: rotation xyz, translation w) x 3; its velocity (local) and wheel speed (+ forward);
    // its field's corner (vessel) and cell; its field's cells and band; its wheels' radius, width
    float4 rVeh[MAXV * 7];
};
struct Part { float3 p; float w; float3 v; float age; float grow; float gy; float alive; float pad; };
RWStructuredBuffer<Part> uParts : register(u1);
StructuredBuffer<Part> tParts : register(t16);
StructuredBuffer<Part> tSpawn : register(t15);
StructuredBuffer<float4> tHubs : register(t12);      // vehicle * 8 + i: the hubs (vessel frame)
Texture3D<float> tBody0 : register(t17);
Texture3D<float> tBody1 : register(t18);
Texture3D<float> tBody2 : register(t19);
Texture3D<float> tBody3 : register(t20);

float3 ToWorld(float3 p) { return float3(dot(rM0.xyz, p), dot(rM1.xyz, p), dot(rM2.xyz, p)) + float3(rM0.w, rM1.w, rM2.w); }
float3 ToLocal(float3 w) { w -= float3(rM0.w, rM1.w, rM2.w); return float3(dot(float3(rM0.x, rM1.x, rM2.x), w), dot(float3(rM0.y, rM1.y, rM2.y), w), dot(float3(rM0.z, rM1.z, rM2.z), w)); }
float3 DirToLocal(float3 d) { return float3(dot(float3(rM0.x, rM1.x, rM2.x), d), dot(float3(rM0.y, rM1.y, rM2.y), d), dot(float3(rM0.z, rM1.z, rM2.z), d)); }
// Henyey-Greenstein relative to isotropic (4 pi p), capped
float HG(float g, float c) { float t = 1 + g * g - 2 * g * c; return min((1 - g * g) / (t * sqrt(t)), 25); }

// ------------------------------------------------------------------------------------------ 1: the new particles
[numthreads(64, 1, 1)]
void CSSpawn(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)rSpawn.x) return;
    uParts[((uint)rSpawn.y + id.x) % (uint)rT.w] = tSpawn[id.x];
}

// ------------------------------------------------------------------------------------------ 2: the step
float BodyAt(uint k, float3 uvw)
{
    if (k == 0) return tBody0.SampleLevel(sExLin, uvw, 0);
    if (k == 1) return tBody1.SampleLevel(sExLin, uvw, 0);
    if (k == 2) return tBody2.SampleLevel(sExLin, uvw, 0);
    return tBody3.SampleLevel(sExLin, uvw, 0);
}
// the distance to vehicle k's static mesh at pv (vessel frame) [m]; its band beyond
float Dist(uint k, float3 pv)
{
    float4 m = rVeh[k * 7 + 4], d = rVeh[k * 7 + 5];
    float3 g = (pv - m.xyz) / m.w;
    if (any(g < 0) || any(g > d.xyz - 1)) return d.w;
    return BodyAt(k, (g + 0.5) / d.xyz) * d.w;
}
float3 ToVessel(uint k, float3 p) { return float3(dot(rVeh[k * 7].xyz, p) + rVeh[k * 7].w, dot(rVeh[k * 7 + 1].xyz, p) + rVeh[k * 7 + 1].w, dot(rVeh[k * 7 + 2].xyz, p) + rVeh[k * 7 + 2].w); }
float3 DirToVessel(uint k, float3 v) { return float3(dot(rVeh[k * 7].xyz, v), dot(rVeh[k * 7 + 1].xyz, v), dot(rVeh[k * 7 + 2].xyz, v)); }
float3 DirFromVessel(uint k, float3 v) { return rVeh[k * 7].xyz * v.x + rVeh[k * 7 + 1].xyz * v.y + rVeh[k * 7 + 2].xyz * v.z; }

static const float RC = 0.05, E_BOUNCE = 0.25, MU = 0.45;
void Collide(uint k, float3 o, inout Part q)
{
    float3 po = ToVessel(k, o), pv = ToVessel(k, q.p);
    float4 vv = rVeh[k * 7 + 3];                     // the vessel's velocity (local), its wheels' surface speed
    float3 rel = DirToVessel(k, q.v - vv.xyz);       // the dust's velocity relative to it (vessel frame)
    float4 dw = rVeh[k * 7 + 5];
    bool hit = false;
    // its wheels: turning cylinders about x
    float R = rVeh[k * 7 + 6].x, W = rVeh[k * 7 + 6].y;
    [loop] for (uint i = 0; i < 8; i++) {
        float4 h = tHubs[k * 8 + i];
        if (h.w < 0.5) continue;                                         // (w: there is one)
        if (abs(pv.x - h.x) > W * 0.5) continue;
        float2 d = pv.yz - h.yz, d0 = po.yz - h.yz;
        float r = length(d);
        if (r >= R || length(d0) < R || r < 1e-4) continue;
        float2 n = d / r;
        float vn = dot(rel.yz, n);
        if (vn < 0) {
            float2 tread = float2(-d.y, d.x) / R * vv.w;                 // the tread's velocity there (rolling forward: +z on top)
            rel.yz = (rel.yz - 1.2 * vn * n) * 0.4 + 0.3 * tread;
            rel.x *= 0.4;
        }
        pv.yz = h.yz + n * (R + 0.01);
        hit = true;
    }
    // its static mesh: the step walked near it
    float d1 = Dist(k, pv), d0 = Dist(k, po);
    if (d1 < dw.w * 0.999 || d0 < dw.w * 0.999) {
        float L = length(pv - po);
        uint n = (uint)clamp(ceil(L / 0.04), 1, 24);
        [loop] for (uint s = 1; s <= n; s++) {
            float3 pp = lerp(po, pv, (float)s / n);
            float d = Dist(k, pp);
            if (d > RC) continue;
            const float e = 0.04;
            float3 gr = float3(Dist(k, pp + float3(e, 0, 0)) - Dist(k, pp - float3(e, 0, 0)), Dist(k, pp + float3(0, e, 0)) - Dist(k, pp - float3(0, e, 0)),
                               Dist(k, pp + float3(0, 0, e)) - Dist(k, pp - float3(0, 0, e)));
            float gl = length(gr);
            if (gl < 1e-6) continue;
            gr /= gl;
            float vn = dot(rel, gr);
            if (vn >= 0) continue;                                        // leaving it
            float3 t = rel - vn * gr;
            rel = t * (1 - MU) - E_BOUNCE * vn * gr;
            pv = pp + gr * (RC - d + 0.005);
            hit = true;
            break;
        }
    }
    if (hit) {   // back to the local frame
        float3 v0 = float3(rVeh[k * 7].w, rVeh[k * 7 + 1].w, rVeh[k * 7 + 2].w);
        q.p = DirFromVessel(k, pv - v0);
        q.v = vv.xyz + DirFromVessel(k, rel);
    }
}
[numthreads(64, 1, 1)]
void CSStep(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)rT.w) return;
    Part q = uParts[id.x];
    if (q.alive < 0.5) return;
    const float h = rT.x;
    const uint ns = (uint)rT.z, nv = (uint)rSpawn.z;
    [loop] for (uint s = 0; s < ns; s++) {
        float3 o = q.p;
        q.v.y -= rT.y * h;
        q.p += q.v * h;
        [loop] for (uint k = 0; k < nv; k++) Collide(k, o, q);
        if (q.p.y < q.gy) { q.alive = 0; break; }                         // down: the ground's again
    }
    q.age += h * ns;
    if (q.age > 40 || abs(q.p.x) > 3000 || abs(q.p.z) > 3000) q.alive = 0;
    uParts[id.x] = q;
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

// ------------------------------------------------------------------------------------------ 3: the dust's own shade
RWTexture3D<uint> uShadeSum : register(u2);
RWTexture3D<uint> uCount : register(u4);
[numthreads(64, 1, 1)]
void CSVoxel(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)rT.w) return;
    Part q = tParts[id.x];
    if (q.alive < 0.5) return;
    float3 gb = (q.p - vBox.xyz) / vBox.w - 0.5;
    int3 c0 = int3(floor(gb)); float3 f = gb - c0;
    float val = q.w / (vBox.w * vBox.w * vBox.w) * vDim.w;
    int3 cn = int3(floor(gb + 0.5));
    if (all(cn >= 0) && all(cn < int3(vDim.xyz))) InterlockedAdd(uCount[cn], 1);
    [unroll] for (int k = 0; k < 8; k++) {
        int3 o = int3(k & 1, (k >> 1) & 1, k >> 2), c = c0 + o;
        if (any(c < 0) || any(c >= int3(vDim.xyz))) continue;
        float3 w3 = o ? f : 1 - f;
        uint add = (uint)min(val * w3.x * w3.y * w3.z + 0.5, 4e8);
        if (add) InterlockedAdd(uShadeSum[c], add);
    }
}
// the box's transmittance towards the Sun per cell, from the summed extinction
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
    [loop] for (int i = 0; i < 128; i++) {
        q += sunL;                                                      // a cell a step
        int3 cc = int3(floor(q));
        if (any(cc < 0) || any(cc >= int3(vDim.xyz))) break;
        tau += tShadeSum.Load(int4(cc, 0)) / vDim.w * vBox.w;
        if (tau > 8) break;
    }
    uShade[c] = exp(-tau);
}

// the number density round each cell [1/m3]: the mean over 3 x 3 x 3 cells (1.5 of them each way)
Texture3D<uint> tCount : register(t23);
RWTexture3D<float> uDens : register(u5);
[numthreads(4, 4, 4)]
void CSDensity(uint3 id : SV_DispatchThreadID)
{
    int3 c = int3(id);
    if (any(c >= int3(vDim.xyz))) return;
    uint n = 0;
    [unroll] for (int k = 0; k < 27; k++) {
        int3 q = clamp(c + int3(k % 3, (k / 3) % 3, k / 9) - 1, 0, int3(vDim.xyz) - 1);
        n += tCount.Load(int4(q, 0));
    }
    uDens[c] = n / (27 * vBox.w * vBox.w * vBox.w);
}
// the box's lookups fade out over its last 4 cells (no edge drawn on the ground)
float BoxFade(float3 ub) { float3 d = min(ub, 1 - ub) * vDim.xyz; return saturate(min(min(d.x, d.y), d.z) / 4); }

// ------------------------------------------------------------------------------------------ 4: the particles on the screen
Texture3D<float> tShade : register(t25);
Texture3D<float> tDens : register(t24);
struct GOut { float4 pos : SV_Position; float2 q : TEXCOORD0; float2 ts : TEXCOORD1; float d : TEXCOORD2; };
GOut VSGrain(uint c : SV_VertexID, uint id : SV_InstanceID)
{
    GOut o = (GOut)0;
    o.pos = float4(2, 2, 2, 1);
    Part p = tParts[id];
    if (p.alive < 0.5) return o;
    float3 wp = ToWorld(p.p);
    float4 cl = mul(float4(wp, 1), rVP);
    if (cl.w <= 0.05) return o;
    float d = length(wp);
    uint w, h; tExDepth.GetDimensions(w, h);
    // its sigma [m]: the spread of the soil it stands for, growing with its age - small and sharp (never a blurred clump:
    // the user, 2026-10-22); where the dust spreads thin there are more of them, not wider ones
    float3 ub = (p.p - vBox.xyz) / (vDim.xyz * vBox.w);
    bool inBox = all(ub > 0) && all(ub < 1);
    float sg = min(0.03 + p.grow * p.age * 0.08, 4.0);
    float px = clamp(sg * rOpt.w / d, 0.7, 48.0);                       // ... on the screen [px]
    float sm = px * d / rOpt.w;                                          // ... as drawn (the column kept)
    float2 cq = float2(c & 1 ? 1 : -1, c & 2 ? 1 : -1);                 // (a strip of 4: +-3 sigma)
    o.pos = cl;
    o.pos.xy += cq * 3 * px * float2(2.0 / w, 2.0 / h) * cl.w;
    o.q = cq * 3;
    float sh = (inBox ? lerp(1, tShade.SampleLevel(sExLin, ub, 0), BoxFade(ub)) : 1) * RgShadow(wp);
    o.ts = float2(p.w / (6.2831853 * sm * sm), sh);                     // the Gaussian's peak optical depth; the Sun's share
    o.d = cl.z / cl.w;                                                   // its depth (reverse Z: nearer is larger)
    return o;
}
struct GTarg { float tau : SV_Target0; float lit : SV_Target1; };
GTarg PSGrain(GOut i)
{
    float r2 = dot(i.q, i.q);
    if (r2 >= 9) discard;
    if (i.d < tExDepth.Load(int3(i.pos.xy * rOpt.z, 0))) discard;      // behind the scene
    float t = i.ts.x * exp(-0.5 * r2);
    GTarg o; o.tau = t; o.lit = t * i.ts.y;
    return o;
}

// ------------------------------------------------------------------------------------------ 5: over the scene
Texture2D<float> tTau : register(t26);
Texture2D<float> tLit : register(t27);
Texture2D<float> tTauQ : register(t28);      // the wide ones' (a quarter's resolution)
Texture2D<float> tLitQ : register(t29);
float4 VSFull(uint vid : SV_VertexID) : SV_Position
{
    float2 uv = float2((vid << 1) & 2, vid & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
float4 PSApply(float4 pos : SV_Position) : SV_Target
{
    int2 p = int2(pos.xy);
    uint w, h; tTau.GetDimensions(w, h);
    float2 uv = pos.xy / float2(w, h);
    float tau = tTau.Load(int3(p, 0)) + tTauQ.SampleLevel(sExLin, uv, 0), lit = tLit.Load(int3(p, 0)) + tLitQ.SampleLevel(sExLin, uv, 0);
    float3 sw; float sd = ExSceneDist(pos.xy, sw);
    // what lies behind (the ground, a hull) in the dust's shadow: the Sun through the dust there
    float gsh = 1;
    if (sd < 1e7) {
        float3 ub = (ToLocal(sw) - vBox.xyz) / (vDim.xyz * vBox.w);
        if (all(ub > 0) && all(ub < 1)) gsh = lerp(1, tShade.SampleLevel(sExLin, ub, 0), BoxFade(ub));
        gsh += (1 - gsh) * rOpt.y * 0.5;           // the dust above scatters a share of what it takes down onto it
    }
    if (tau < 1e-5 && gsh > 0.998) discard;
    float3 dir = normalize(sw);
    float3 Esun = SunLight(float3(0, 0, 0));
    float ph = HG(-0.3, dot(dir, gSunDir.xyz));
    // single scattering w E p in the scene's units (pi x physical): w ph / 4 per unit of optical depth, the share of it
    // the Sun reaches; the sunlit ground's light from below
    float sunUp = saturate(dot(normalize(float3(rM0.y, rM1.y, rM2.y)), gSunDir.xyz));
    float sl = saturate(lit / max(tau, 1e-9));
    sl += (1 - sl) * rOpt.y * 0.5;                 // in its own shade, the light the dust nearer the Sun scatters on
    float3 S = Esun * (rOpt.y * ph * 0.25) * sl + Esun * (rOpt.y * rOpt.x * sunUp * 0.5 * 0.25);
    float T = exp(-tau);
    if (rSpawn.w > 0.5) return float4(saturate(tau * 4), saturate(lit * 4), 0, 0.3);   // (test runs, RegolithDbg = 1)
    return float4(S * (1 - T), T * gsh);         // the blend: this + the scene x T
}
