// D3D11Client: the vessels' own exhausts (VESSEL::AddExhaust; VesselExhaust.cpp), volumetric. Each exhaust is a
// cylinder (radius wsize, length lsize) from its exit down the jet; each pixel of its far face marches the view ray
// through it (as our D3D9 fork's ExhaustVolume, the jet pack's 0x100 exhausts, did - here for every exhaust). The
// density and colour come from the exhaust texture as the stock layout has it (u 0.01..0.24 across = r from -R to +R,
// v 0..1 along): the art made for the stock ribbon becomes the jet's radial profile - the same from the side, a full
// disc end-on. Beyond the D3D9 fork: soft against the scene's depth (the march stops at what stands in the jet), the
// level's flicker per frame, a turbulence that runs down the jet, and an emission kept constant on screen under the
// eye's adaptation (an exhaust is a light source: its look must not follow the exposure of the scene round it).
#include "exhaust_common.hlsli"

cbuffer Ex : register(b5)
{
    float4x4 eWVP;      // exhaust frame -> clip (camera-centred global in between)
    float4x4 eToG;      // exhaust frame -> camera-centred global (rows)
    float4 eCam;        // the camera in the exhaust frame; w: radius R
    float4 eP;          // x length L, y level, z seed, w time [s]
    float4 eK;          // x emission scale, yzw 0
};
Texture2D tExTex : register(t6);

struct VSOut { float4 pos : SV_Position; float3 p : TEXCOORD0; };

// the unit cylinder (x, y on the unit circle, z 0..1): 32 segments x (2 side triangles + 2 cap triangles)
VSOut VS(uint vid : SV_VertexID)
{
    const uint SEG = 32;
    uint k = vid / 12, j = vid % 12;
    float a0 = 6.2831853 * k / SEG, a1 = 6.2831853 * (k + 1) / SEG;
    float3 p0 = float3(cos(a0), sin(a0), 0), p1 = float3(cos(a1), sin(a1), 0);
    float3 q0 = float3(p0.xy, 1), q1 = float3(p1.xy, 1);
    float3 t[12] = { p0, p1, q1, p0, q1, q0, float3(0, 0, 0), p1, p0, float3(0, 0, 1), q0, q1 };
    float3 u = t[j];
    VSOut o;
    o.p = float3(u.xy * eCam.w, u.z * eP.x);
    o.pos = mul(float4(o.p, 1), eWVP);
    return o;
}

float Hash(float2 v) { return frac(sin(dot(v, float2(12.9898, 78.233)) + eP.z * 43.17) * 43758.5453); }

float4 PS(VSOut i) : SV_Target
{
    const float R = eCam.w, L = eP.x;
    float3 cam = eCam.xyz;
    float3 d = i.p - cam; float tp = length(d); d /= tp;
    // the ray against the cylinder x^2 + y^2 <= R^2, 0 <= z <= L
    float a = dot(d.xy, d.xy), b = dot(cam.xy, d.xy), c = dot(cam.xy, cam.xy) - R * R;
    float t0 = -1e9, t1 = 1e9;
    if (a > 1e-8) {
        float disc = b * b - a * c;
        if (disc <= 0) discard;
        float s = sqrt(disc); t0 = (-b - s) / a; t1 = (-b + s) / a;
    } else if (c > 0) discard;
    if (abs(d.z) > 1e-8) {
        float z0 = -cam.z / d.z, z1 = (L - cam.z) / d.z;
        t0 = max(t0, min(z0, z1)); t1 = min(t1, max(z0, z1));
    } else if (cam.z < 0 || cam.z > L) discard;
    t0 = max(t0, 0);
    if (t1 <= t0) discard;
    // only the far face draws (each pixel once, also with the camera inside the jet): this pixel is the exit point
    if (abs(tp - t1) > 0.25 * (t1 - t0) + 0.01 * R) discard;
    // soft against the scene: the march stops where something stands in the jet (the ray's length is the same in the
    // exhaust's frame as in the camera-centred global one: a rigid transform)
    float3 sw; float dS = ExSceneDist(i.pos.xy, sw);
    float tEnd = min(t1, dS);
    if (tEnd <= t0) discard;

    const int N = 32;
    float dt = (t1 - t0) / N, sum = 0, jit = Hash(i.pos.xy);
    float3 col = 0;
    [loop] for (int k = 0; k < N; k++) {
        float tk = t0 + (k + jit) * dt;
        if (tk > tEnd) break;
        float3 q = cam + d * tk;
        float r = length(q.xy) / R, v = q.z / L;
        float4 tx = tExTex.SampleLevel(sExLin, float2(0.01 + 0.23 * (0.5 + 0.5 * r), saturate(v)), 0);
        // turbulence running down the jet (cells ~R across, faster near the exit), more at its edge and tail
        float n = ExNoise(float3(q.xy / (0.7 * R), (q.z - eP.w * 3.0 * L) / (1.2 * R)));
        float turb = lerp(1.0, 0.55 + 0.9 * n, saturate(0.3 + 0.7 * r + 0.5 * v));
        float w = tx.a * turb * dt;
        sum += w; col += tx.rgb * w;
    }
    // side-on through the axis a chord gives about the stock ribbon's own alpha there
    float alpha = (1 - exp(-sum / (0.9 * R))) * eP.y;
    float3 c3 = col / max(sum, 1e-6);
    // an exhaust is a light: its colour on screen kept under the eye's adaptation, a little brighter than the art
    float3 em = c3 * eK.x / max(gAdapt.w * gAdapt.rgb, 1e-4);
    return float4(em * alpha, alpha);
}
