// D3D11Client engine/heat/particle effects (src\Exhaust.cpp, src\GpuParticles.cpp): shared shader helpers.
// Registers of these effects: b5 (pass constants), b6 (counts), t4..t6, s3 - none of the client's.
#ifndef EXHAUST_COMMON
#define EXHAUST_COMMON
#include "common.hlsli"

Texture2D<float> tExDepth : register(t5);     // the scene's single-sampled depth (reverse Z)
Texture2D<float4> tExBB : register(t6);       // blackbody: row 0, 256 columns over log T 200..20000 K: ln(B_rgb(T) / B_rgb(Sun))
SamplerState sExLin : register(s3);           // linear, clamp

// the scene point behind pixel 'pix' (camera-centred global); its distance (1e30: the sky)
float ExSceneDist(float2 pix, out float3 wp)
{
    float z = tExDepth.Load(int3(pix, 0));
    uint w, h; tExDepth.GetDimensions(w, h);
    float2 ndc = float2(pix.x / w * 2 - 1, 1 - pix.y / h * 2);
    if (z <= 0) { float4 q = mul(float4(ndc, 1e-7, 1), gInvVP); wp = normalize(q.xyz / q.w) * 1e30; return 1e30; }
    float4 q = mul(float4(ndc, z, 1), gInvVP);
    wp = q.xyz / q.w;
    return length(wp);
}
// the view ray through pixel 'pix' (unit, camera-centred global)
float3 ExViewDir(float2 pix)
{
    uint w, h; tExDepth.GetDimensions(w, h);
    float2 ndc = float2(pix.x / w * 2 - 1, 1 - pix.y / h * 2);
    float4 q = mul(float4(ndc, 1e-3, 1), gInvVP);
    return normalize(q.xyz / q.w);
}

// a blackbody's radiance at temperature T [K], relative to the Sun's (per channel, so the Sun is white); times the
// pass's kE (pi / the Sun's solid angle) it is the radiance in the scene's units
float3 ExBlackbodyRel(float T)
{
    if (T < 250) return 0;
    float x = saturate(log(T / 200.0) / log(100.0));
    float3 l = tExBB.SampleLevel(sExLin, float2((x * 255 + 0.5) / 256, 0.5), 0).rgb;
    return exp(l);
}

// hashes and value noise (no textures)
uint ExHash(uint x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
float ExRand(uint x) { return (ExHash(x) & 0xffffff) / 16777216.0; }
float ExHash3(float3 p) { p = frac(p * 0.3183099 + 0.1); p *= 17.0; return frac(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float ExNoise(float3 x)
{
    float3 i = floor(x), f = frac(x);
    f = f * f * (3 - 2 * f);
    return lerp(lerp(lerp(ExHash3(i), ExHash3(i + float3(1, 0, 0)), f.x), lerp(ExHash3(i + float3(0, 1, 0)), ExHash3(i + float3(1, 1, 0)), f.x), f.y),
                lerp(lerp(ExHash3(i + float3(0, 0, 1)), ExHash3(i + float3(1, 0, 1)), f.x), lerp(ExHash3(i + float3(0, 1, 1)), ExHash3(i + float3(1, 1, 1)), f.x), f.y), f.z);
}
float ExFbm(float3 x) { return 0.5 * ExNoise(x) + 0.25 * ExNoise(x * 2.03 + 17.1) + 0.125 * ExNoise(x * 4.01 + 31.7) + 0.0625 * ExNoise(x * 8.05 + 5.3); }

// the light the air lets through to the camera from distance 'dist' (the FX atmosphere's extinction at the camera's
// height; what lies behind was already dimmed by the atmosphere's pass) and the light it adds
float3 ExAirTrans(float dist)
{
    if (!FxActive()) return 1;
    float3 c = -mCentre.xyz; float h = length(c) - mCentre.w;
    return exp(-FxExtinction(max(h, 0)) * dist);
}

// a unit cube (-1..1) from SV_VertexID (36 vertices); n: the face's outward normal. The proxies keep only the faces
// turned away from the camera (each pixel once, also with the camera inside), whatever the winding
float3 ExCube(uint vid, out float3 n)
{
    uint f = vid / 6, k = vid % 6;
    static const uint tri[6] = { 0, 1, 2, 0, 2, 3 };
    static const float2 sq[4] = { float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, 1) };
    float2 c = sq[tri[k]];
    float s = (f & 1) ? 1 : -1;
    uint ax = f / 2;
    float3 p = ax == 0 ? float3(s, c.x, c.y) : ax == 1 ? float3(c.x, s, c.y) : float3(c.x, c.y, s);
    n = ax == 0 ? float3(s, 0, 0) : ax == 1 ? float3(0, s, 0) : float3(0, 0, s);
    return p;
}

#endif
