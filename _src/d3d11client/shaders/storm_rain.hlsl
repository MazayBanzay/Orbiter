// D3D11Client: Earth's rain under the storm (StormRain.cpp). The method follows the modern open references (own code):
//  - Wicked Engine (MIT): GPU particles simulated in a compute shader in a box round the camera; each drop drawn as a
//    streak stretched along its velocity relative to the camera; a refracting "lens" with a cylinder normal across the
//    streak, Fresnel rim, forward scattering; a soft depth fade; splashes born where a drop hits the ground; a blocker
//    round the person in focus;
//  - Garg & Nayar (2004, 2005), Tremblay et al. (IJCV 2020): a drop covers a pixel for only ~D/v of the exposure, so a
//    streak is faint (its opacity ~ (D/v)/T) - the rain's density comes from their number; a drop gathers ~165 degrees
//    of the environment, so it is brighter than a dark background;
//  - the drops' sizes: Marshall-Palmer (Lambda = 4.1 R^-0.21 /mm), their fall: the Atlas et al. (1973) fit.
// Positions live in an "anchor" frame fixed to the ground (x, z horizontal, y up, metres), its origin moved with the
// camera on the CPU (doubles there): the floats stay small.
#include "common.hlsli"

cbuffer StormRain : register(b1)
{
    float4x4 rVP;          // camera-centred global -> clip
    float4 rE1, rUp, rE2;  // the anchor's axes (camera-centred global)
    float4 rCam;           // xyz: the camera in the anchor frame [m]; w: the box's side [m]
    float4 rWind;          // xyz: the wind in the anchor frame [m/s]; w: the step [s]
    float4 rVcam;          // xyz: the camera's velocity in the anchor frame [m/s]; w: time [s]
    float4 rP;             // x: rate [mm/h], y: Lambda [1/mm], z: active drops, w: a pixel's angle [rad]
    float4 rL;             // x: the daylight's share under the clouds, y: exposure [s], z: the ground's height (anchor y) [m], w: frame seed
    float4 rBlk;           // xyz: the person in focus (anchor frame), w: their radius [m] (0: none)
    float4 rScr;           // xy: the scene's size [px]; z: the blocker's height above xyz [m]; w: splash slots
    float4 rNear;          // x: the projection's near distance (reverse Z: depth = near / view z); y: real drops per particle
};

struct Drop { float3 p; float d; };     // anchor-frame position [m], diameter [m]
struct Splash { float3 p; float t0; };  // where it hit, when

uint Hash(uint n) { n = (n << 13u) ^ n; n = n * (n * n * 15731u + 789221u) + 1376312589u; return n; }
float H01(uint n) { return (Hash(n) & 0xffffffu) / 16777216.0; }

// a diameter [m] from the Marshall-Palmer spectrum, 0.5-6 mm (larger ones break up)
float DrawD(uint seed) { return clamp(-log(max(H01(seed), 1e-5)) / rP.y, 0.5, 6.0) * 1e-3; }
// terminal fall speed [m/s] (Atlas et al. 1973), D in m
float Vt(float d) { return max(9.65 - 10.3 * exp(-600.0 * d), 0.5); }
// the gusts: a slow 3D modulation of the wind carried with it (sheets of rain in a storm), ~+-35 %
float Gust(float3 p, float t)
{
    float3 q = p - rWind.xyz * t;
    return 1 + 0.22 * sin(dot(q, float3(0.071, 0.013, 0.053)) + t * 0.9) + 0.13 * sin(dot(q, float3(-0.031, 0.02, 0.117)) - t * 1.7);
}
float3 DropVel(float3 p, float d) { return rWind.xyz * Gust(p, rVcam.w) - float3(0, Vt(d), 0); }

// ------------------------------------------------------------------------------------------------ simulation
RWStructuredBuffer<Drop> uDrops : register(u0);
RWStructuredBuffer<Splash> uSplash : register(u1);
RWByteAddressBuffer uCount : register(u2);

float3 BoxCentre() { return rCam.xyz + float3(0, 0.15, 0) * rCam.w; }

[numthreads(256, 1, 1)]
void CSInit(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    float3 h = float3(H01(i * 7u + 1u), H01(i * 7u + 2u), H01(i * 7u + 3u)) - 0.5;
    Drop q; q.p = BoxCentre() + h * rCam.w; q.d = DrawD(i * 7u + 4u);
    uDrops[i] = q;
}

[numthreads(256, 1, 1)]
void CSSim(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    Drop q = uDrops[i];
    float dt = rWind.w;
    q.p += DropVel(q.p, q.d) * dt;
    // the ground: a splash where it hits (near the camera), the drop back at the box's top with a new size
    if (q.p.y < rL.z) {
        float3 hit = float3(q.p.x, rL.z, q.p.z);
        float2 dh = hit.xz - rCam.xz;
        if (dot(dh, dh) < 18.0 * 18.0 && (Hash(i + asuint(rL.w)) & 3u) == 0u) {
            uint slot; uCount.InterlockedAdd(0, 1, slot);
            Splash s; s.p = hit; s.t0 = rVcam.w;
            uSplash[slot % (uint)rScr.w] = s;
        }
        q.p.y += rCam.w;
        q.d = DrawD(i * 7u + asuint(rL.w));
    }
    // wrapped round the camera: the box follows it (no emitter; nothing swims with the view)
    float3 c = BoxCentre();
    float3 rel = q.p - c;
    q.p = c + rel - rCam.w * floor(rel / rCam.w + 0.5);
    uDrops[i] = q;
}

// ------------------------------------------------------------------------------------------------ the streaks
StructuredBuffer<Drop> tDrops : register(t1);
StructuredBuffer<Splash> tSplash : register(t2);
Texture2D tScene : register(t0);          // the scene behind (refraction)
Texture2D<float> tDepthS : register(t3);  // the scene's depth (the soft fade)

// the environment a drop shows (~165 degrees of it): the brightest of a few spots along the top of the view - the sky
// and the cloud base there. Once a frame (CSEnv), not per vertex
RWStructuredBuffer<float4> uEnv : register(u3);
StructuredBuffer<float4> tEnv : register(t4);
[numthreads(1, 1, 1)]
void CSEnv(uint3 id : SV_DispatchThreadID)
{
    float3 m = 0;
    [unroll] for (int y = 0; y < 3; y++) [unroll] for (int k = 0; k < 8; k++) {
        float3 c = tScene.Load(int3(int2(rScr.x * (k + 0.5) / 8, rScr.y * (0.05 + 0.08 * y)), 0)).rgb;
        if (dot(c, 0.333) > dot(m, 0.333)) m = c;
    }
    float Y = dot(m, float3(0.2126, 0.7152, 0.0722));
    uEnv[0] = float4(Y * float3(0.95, 0.97, 1.0), 1);
}

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;               // x across the streak -1..1, y along it 0 (head) .. 1 (tail)
    nointerpolation float4 env : COLOR0; // rgb environment light, a opacity
    nointerpolation float3 side : TEXCOORD1;   // across the streak (global)
    nointerpolation float3 V : TEXCOORD2;      // the view direction (global)
    nointerpolation float4 scr : TEXCOORD3;    // xy the streak's screen normal (px), z its width (px), w its distance
};

static const float2 kCorner[4] = { float2(-1, 0), float2(1, 0), float2(-1, 1), float2(1, 1) };   // a strip of two triangles

VSOut Cull() { VSOut o = (VSOut)0; o.pos = float4(0, 0, -1, 1); return o; }

VSOut VSDrop(uint vid : SV_VertexID, uint inst : SV_InstanceID)
{
    if (inst >= (uint)rP.z) return Cull();
    Drop q = tDrops[inst];
    float3 rel = q.p - rCam.xyz;
    float3 wp = rE1.xyz * rel.x + rUp.xyz * rel.y + rE2.xyz * rel.z;
    float dist = length(wp);
    if (dist < 0.25 || dist > 0.5 * rCam.w) return Cull();
    // the person in focus stands in the rain's shadow of their own body (Wicked's head blocker)
    if (rBlk.w > 0) {
        float3 b = q.p - rBlk.xyz;
        if (dot(b.xz, b.xz) < rBlk.w * rBlk.w && b.y < rScr.z && b.y > -0.2) return Cull();
    }
    float3 v = DropVel(q.p, q.d) - rVcam.xyz;          // relative to the camera
    float3 vw = rE1.xyz * v.x + rUp.xyz * v.y + rE2.xyz * v.z;
    float T = rL.y;
    float3 p0 = wp, p1 = wp - vw * T;                   // the head and where it was an exposure ago
    float4 c0 = mul(float4(p0, 1), rVP), c1 = mul(float4(p1, 1), rVP);
    if (c0.w < 0.05 || c1.w < 0.05) return Cull();
    float2 s0 = c0.xy / c0.w * float2(0.5, -0.5) * rScr.xy, s1 = c1.xy / c1.w * float2(0.5, -0.5) * rScr.xy;
    float2 ax = s1 - s0; float lpx = length(ax);
    ax = lpx > 1e-3 ? ax / lpx : float2(0, 1);
    float2 nx = float2(-ax.y, ax.x);
    float dpx = q.d / max(dist * rP.w, 1e-6);           // the drop's own width [px]
    float wpx = max(dpx, 1.0);
    float2 c = kCorner[vid];
    float4 cl = lerp(c0, c1, c.y);
    // across: the width (+ half a pixel for the edge's smoothing); along: capped by half a width at both ends
    float2 off = nx * (c.x * (0.5 * wpx + 0.5)) + ax * ((c.y * 2 - 1) * (0.5 * wpx + 0.5));
    cl.xy += off / (rScr.xy * float2(0.5, -0.5)) * cl.w;
    VSOut o;
    o.pos = cl;
    o.uv = c;
    // the opacity: the share of the exposure the drop covers a pixel (Garg & Nayar) x 4 for the eye, a thin line's
    // coverage below a pixel, faded towards the box's edge and at the camera
    float vr = max(length(v), 0.5);
    float a1 = saturate((q.d / vr) / T);                // one real drop's share of the exposure (Garg & Nayar)
    float a = (1 - pow(1 - a1, rNear.y)) * saturate(dpx / wpx + 0.25);   // the many it stands for, thin-line coverage
    a *= saturate((0.5 * rCam.w - dist) / (0.15 * rCam.w)) * saturate((dist - 0.25) / 0.5);
    o.env = float4(tEnv[0].rgb, min(a, 0.55));
    float3 V = wp / dist;
    float3 axW = length(vw) > 1e-4 ? vw / length(vw) : -rUp.xyz;
    o.side = normalize(cross(V, axW) + 1e-6);
    o.V = V;
    o.scr = float4(nx, wpx, dist);
    return o;
}

float HGc(float g, float c) { float t = 1 + g * g - 2 * g * c; return (1 - g * g) / (t * sqrt(t)); }   // / isotropic

float4 PSDrop(VSOut i) : SV_Target
{
    float x = clamp(i.uv.x, -1, 1);
    float along = smoothstep(0.0, 0.12, i.uv.y) * smoothstep(1.0, 0.65, i.uv.y);   // a bright head, a fading tail
    float edge = saturate(1 - x * x);
    float a = i.env.a * along * edge;
    // the soft fade where it meets what is drawn (reverse Z, near 0.1 m: distance ~ 0.1 / z)
    float zs = tDepthS.Load(int3(int2(i.pos.xy), 0));
    if (zs > 1e-9) { float sceneZ = rNear.x / zs; a *= saturate((sceneZ - i.pos.w) / 0.3); }   // both view depths
    if (a < 0.002) discard;
    // a water cylinder across the streak: its normal, the Fresnel at it, the refraction through it
    float3 n = normalize(x * i.side - sqrt(edge) * i.V);
    float cv = saturate(dot(n, -i.V));
    float F = 0.02 + 0.98 * pow(1 - cv, 5);
    float refrPx = clamp(i.scr.z * 2.5, 2, 14);
    int2 p = clamp(int2(i.pos.xy + i.scr.xy * (x * refrPx)), 0, int2(rScr.xy) - 1);
    float3 behind = tScene.Load(int3(p, 0)).rgb;
    // what it shows: the refracted scene, the environment it gathers, the rim's reflection, the Sun scattered forward
    float3 sun = SunLight(i.V * i.scr.w) * rL.x * HGc(0.8, dot(i.V, gSunDir.xyz)) * 0.06;
    float3 col = behind * (1 - F) * 0.85 + i.env.rgb * (0.35 + 1.2 * F) + sun;
    return float4(col * a, a);
}

// ------------------------------------------------------------------------------------------------ the splashes
struct SOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; nointerpolation float4 col : COLOR0; };

SOut VSSplash(uint vid : SV_VertexID, uint inst : SV_InstanceID)
{
    SOut o = (SOut)0; o.pos = float4(0, 0, -1, 1);
    Splash s = tSplash[inst];
    float age = rVcam.w - s.t0;
    const float life = 0.14;
    if (age < 0 || age > life || s.t0 <= 0) return o;
    float k = age / life;
    float3 rel = s.p - rCam.xyz;
    float3 wp = rE1.xyz * rel.x + rUp.xyz * rel.y + rE2.xyz * rel.z;
    float dist = length(wp);
    if (dist > 18) return o;
    // an upright crown facing the eye: 4-8 cm, rising and widening over its life
    float sz = (0.035 + 0.045 * k) * (0.7 + 0.6 * H01(inst * 3u + 1u));
    float3 V = wp / max(dist, 1e-3);
    float3 rt = normalize(cross(rUp.xyz, V) + 1e-6);
    float2 c = kCorner[vid];
    float3 p = wp + rt * (c.x * sz) + rUp.xyz * (c.y * sz * 0.9);
    o.pos = mul(float4(p, 1), rVP);
    o.uv = c;
    float a = (1 - k) * (1 - k) * saturate((18 - dist) / 6) * saturate(rP.x / 10) * 0.6;
    o.col = float4(tEnv[0].rgb * 1.1, a);
    return o;
}

float4 PSSplash(SOut i) : SV_Target
{
    // a crown: a thin bowl rim at the bottom, a few spikes of water flung up from it
    float x = i.uv.x, y = i.uv.y;
    float rim = smoothstep(0.35, 0.0, y) * smoothstep(1.0, 0.7, abs(x)) * smoothstep(0.2, 0.6, abs(x) + y * 0.8);
    float sp = pow(saturate(cos(x * 9.5)), 12) * smoothstep(1.0, 0.2, y) * smoothstep(0.0, 0.2, y) * smoothstep(1.0, 0.6, abs(x));
    float a = i.col.a * saturate(rim + sp * 0.8);
    if (a < 0.003) discard;
    return float4(i.col.rgb * a, a);
}
