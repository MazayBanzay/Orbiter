// D3D11Client: ambient occlusion, ground truth style (after Jimenez et al. 2016, "Practical Realtime Strategies for
// Accurate Indirect Occlusion"; our own code). Ssao.cpp runs it after the opaque terrain and vessels, before the liquid,
// the air and the effects (those are never touched):
//   CSGTAO  at half resolution: per pixel 3 slices through the view direction, each searched 4 steps both ways for its
//           two horizons within a small world radius (the folds of a suit, the gaps between parts, a foot or a wheel on
//           the ground - the large scale is the sky visibility's, lights.hlsli), the visible arc integrated against the
//           cosine of the normal: the share of the sky the near geometry leaves. A fixed per-pixel rotation (no noise
//           that changes with the frame: no crawl without TAA), smoothed after
//   CSBlur  twice (across, then down): a 9-tap filter that keeps to one surface (depth-weighted): no halo, no grain
//   PSApply back at full resolution (the 4 nearest half-resolution texels, depth-weighted), into the scene by
//           multiplication - but only the sky's share of the light there: the Sun (its shadow, its angle) and the sky
//           are taken at the point as the frame's lighting has them; under a lit Sun the occlusion hardly shows, in the
//           shade and on Titan (the light mostly the sky's) fully
#include "common.hlsli"

cbuffer Gtao : register(b11)
{
    float4 aP;     // x, y: tan half-fov x, y; z: the near plane (reverse Z, infinite: view z = near / depth); w: radius [m]
    float4 aS;     // x, y: the scene's size; z, w: the AO's size
    float4 aK;     // x: strength; y: the largest radius on screen [pixels]; z: 1 = test view (the factor alone); w: -
    float4 aD;     // xy: the blur's step (1, 0) or (0, 1)
};
Texture2D<float> tDepth : register(t0);       // the scene's depth, single-sampled
Texture2D<float2> tAO : register(t1);         // occlusion (1 open), view z
RWTexture2D<float2> uAO : register(u0);

static const float PI = 3.14159265;

float3 ViewPos(int2 p)      // view space: x right, y up, z forward [m]; z 0: nothing drawn
{
    p = clamp(p, int2(0, 0), int2(aS.xy) - 1);
    float d = tDepth.Load(int3(p, 0));
    if (d <= 0) return 0;
    float z = aP.z / d;
    float2 ndc = (p + 0.5) / aS.xy * float2(2, -2) + float2(-1, 1);
    return float3(ndc.x * aP.x * z, ndc.y * aP.y * z, z);
}

// the normal from the nearer neighbour on each axis (a silhouette does not bend it)
float3 ViewNormal(int2 p, float3 P)
{
    float3 px1 = ViewPos(p + int2(1, 0)), px0 = ViewPos(p - int2(1, 0));
    float3 py1 = ViewPos(p + int2(0, 1)), py0 = ViewPos(p - int2(0, 1));
    float3 dx = (px1.z > 0 && (px0.z <= 0 || abs(px1.z - P.z) < abs(P.z - px0.z))) ? px1 - P : P - px0;
    float3 dy = (py1.z > 0 && (py0.z <= 0 || abs(py1.z - P.z) < abs(P.z - py0.z))) ? py1 - P : P - py0;
    float3 n = normalize(cross(dy, dx));
    return dot(n, -P) < 0 ? -n : n;
}

[numthreads(8, 8, 1)]
void CSGTAO(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)aS.z || id.y >= (uint)aS.w) return;
    int2 p = int2(id.xy) * 2;
    float3 P = ViewPos(p);
    if (P.z <= 0) { uAO[id.xy] = float2(1, 0); return; }
    float rpx = aP.w * (aS.y * 0.5) / (aP.y * P.z);        // the radius on screen [full-resolution pixels]
    if (rpx < 2) { uAO[id.xy] = float2(1, P.z); return; }   // far: the radius is under a couple of pixels
    float fade = saturate((rpx - 2) / 6);
    rpx = min(rpx, aK.y);
    float3 V = normalize(-P), N = ViewNormal(p, P);
    // a fixed rotation and step offset per pixel (interleaved gradient noise): the blur takes the pattern out
    float rot = frac(52.9829189 * frac(dot(float2(id.xy), float2(0.06711056, 0.00583715))));
    float jit = frac(rot * 4.7 + 0.31);
    const int SLICES = 3, STEPS = 4;
    float r2 = aP.w * aP.w, vis = 0;
    [unroll] for (int s = 0; s < SLICES; s++) {
        float phi = (s + rot) * (PI / SLICES);
        float2 dir = float2(cos(phi), sin(phi));            // on the screen (y down)
        float3 d3 = float3(dir.x, -dir.y, 0);                 // in view space
        float3 od = normalize(d3 - V * dot(d3, V));           // the slice's direction across the view ray
        float3 ax = cross(od, V);
        float3 pn = N - ax * dot(N, ax);                      // the normal projected into the slice
        float pl = length(pn);
        if (pl < 1e-4) { vis += 1; continue; }
        float cn = clamp(dot(pn, V) / pl, -1, 1);
        float n = (dot(pn, od) < 0 ? -1 : 1) * acos(cn);       // the normal's angle from the view ray in the slice
        float c0 = -1, c1 = -1;                               // the horizons' cosines (to the view ray): none yet
        [unroll] for (int j = 0; j < STEPS; j++) {
            float t = (j + jit) / STEPS;
            float2 o = dir * max(rpx * t * t, 1.0 + j);       // denser near the point
            float3 S1 = ViewPos(p + int2(round(o))), S0 = ViewPos(p - int2(round(o)));
            if (S1.z > 0) { float3 v = S1 - P; float vv = dot(v, v); c1 = max(c1, lerp(-1, dot(v, V) * rsqrt(vv + 1e-8), saturate(1 - vv / r2))); }
            if (S0.z > 0) { float3 v = S0 - P; float vv = dot(v, v); c0 = max(c0, lerp(-1, dot(v, V) * rsqrt(vv + 1e-8), saturate(1 - vv / r2))); }
        }
        float h0 = -acos(clamp(c0, -1, 1)), h1 = acos(clamp(c1, -1, 1));
        h0 = n + max(h0 - n, -PI / 2); h1 = n + min(h1 - n, PI / 2);
        float sn = sin(n);
        vis += pl * 0.25 * ((-cos(2 * h0 - n) + cn + 2 * h0 * sn) + (-cos(2 * h1 - n) + cn + 2 * h1 * sn));
    }
    float ao = saturate(vis / SLICES);
    ao = pow(ao, aK.x);
    uAO[id.xy] = float2(lerp(1, ao, fade), P.z);
}

// 9 taps along aD.xy, weighted by a Gaussian and by staying on the same surface (relative depth)
[numthreads(8, 8, 1)]
void CSBlur(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)aS.z || id.y >= (uint)aS.w) return;
    float2 c = tAO.Load(int3(id.xy, 0));
    if (c.y <= 0) { uAO[id.xy] = c; return; }
    float sum = 0, ws = 0;
    [unroll] for (int k = -4; k <= 4; k++) {
        int2 q = clamp(int2(id.xy) + int2(aD.xy) * k, int2(0, 0), int2(aS.zw) - 1);
        float2 v = tAO.Load(int3(q, 0));
        if (v.y <= 0) continue;
        float w = exp(-k * k / 8.0) * exp(-abs(v.y - c.y) / c.y * 60);
        sum += v.x * w; ws += w;
    }
    uAO[id.xy] = float2(ws > 0 ? sum / ws : c.x, c.y);
}

struct VSOutA { float4 pos : SV_Position; };

float4 PSApply(VSOutA i) : SV_Target
{
    int2 p = int2(i.pos.xy);
    float d0 = tDepth.Load(int3(p, 0));
    if (d0 <= 0) discard;
    float z0 = aP.z / d0;
    // the 4 nearest half-resolution texels, bilinear but kept to the same surface
    float2 h = (p + 0.5) * 0.5 - 0.5;
    int2 b = int2(floor(h)); float2 f = h - b;
    float sum = 0, ws = 0;
    [unroll] for (int k = 0; k < 4; k++) {
        int2 o = int2(k & 1, k >> 1);
        float2 v = tAO.Load(int3(clamp(b + o, int2(0, 0), int2(aS.zw) - 1), 0));
        if (v.y <= 0) continue;
        float w = (o.x ? f.x : 1 - f.x) * (o.y ? f.y : 1 - f.y) * exp(-abs(v.y - z0) / z0 * 60) + 1e-4;
        sum += v.x * w; ws += w;
    }
    float ao = ws > 0 ? sum / ws : 1;
    if (aK.z > 0.5) return float4(ao.xxx * 0.5 / max(gHazeRGB.w * gAdapt.rgb, 1e-4), 1);   // test view
    if (ao > 0.995) discard;
    // the sky's share of the light here: the point and its normal in the frame's (camera-centred) space
    float3 P = ViewPos(p);
    float3 Nv = ViewNormal(p, P);
    float2 ndc = (p + 0.5) / aS.xy * float2(2, -2) + float2(-1, 1);
    float4 w4 = mul(float4(ndc, d0, 1), gInvVP); float3 wp = w4.xyz / w4.w;
    float3 n;                       // the view-space normal in the frame's axes: the camera's right, up (a step on the
    {                               // screen at that depth) and forward
        float4 bx = mul(float4(ndc + float2(1e-3, 0), d0, 1), gInvVP), by = mul(float4(ndc + float2(0, 1e-3), d0, 1), gInvVP);
        float3 ex = normalize(bx.xyz / bx.w - wp), ey = normalize(by.xyz / by.w - wp);
        float3 ez = normalize(cross(ex, ey));
        if (dot(ez, wp) < 0) ez = -ez;
        n = normalize(Nv.x * ex + Nv.y * ey + Nv.z * ez);
    }
    float3 sun = SunLight(wp) * ShadowAtN(wp, n) * saturate(dot(n, gSunDir.xyz));
    float3 sky = SkyLight(n, wp);
    float ls = dot(sun, 1.0 / 3), lk = dot(sky, 1.0 / 3);
    float share = lk / max(ls + lk, 1e-9);
    float m = 1 - share * (1 - ao);
    if (m > 0.995) discard;
    return float4(m, m, m, 1);
}
