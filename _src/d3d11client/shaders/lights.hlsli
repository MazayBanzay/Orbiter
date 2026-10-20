// D3D11Client: local lights - the vessels' point and spot lights (Orbiter LightEmitter: headlights, lamps, the cockpit's
// lights), filled in src\Lights.cpp. Included by common.hlsli; mesh.hlsl and planet.hlsl add LocalLight*() to their light.
//
// Photometry. Orbiter gives a light's colour, intensity, range and attenuation 1 / (a0 + a1 d + a2 d^2); the modules
// tuned those under the old clamped lighting, so the curve itself is not physical (a0 keeps a lamp as bright at 10 m as at
// 1 m). The client takes from it one luminous intensity - the curve's illuminance at a quarter of the range, times
// LightLux (Client.cfg, lux per Orbiter unit, 10 by default) - and lets it fall as 1 / d^2 (the inverse square law), with
// a window that brings it to zero at the range (Karis 2013: (1 - (d/R)^4)^2). A spot's cone: full intensity inside the
// umbra, a smooth edge out to the penumbra (Orbiter's angles are the cones' full apertures). The illuminance in lux is
// divided by the Sun's at this distance from it (128 klx at 1 AU): the client's unit, the Sun's irradiance = 1.
// Measured that way the MPU's headlight is ~50 000 cd (a car's high beam), the suit's work lamp ~2 400 cd.
//
// The list: the view frustum cut into 16 x 9 tiles x 24 slices (exponential in depth); each cluster has the lights whose
// bounds reach it (t18: offset, count into t19). A point is looked up by its position through the camera's unjittered
// view-projection (gLVP), so any pass finds its cluster - points outside the view (the liquid's mirror) test every light.
#ifndef LIGHTS_HLSLI
#define LIGHTS_HLSLI

struct LocalLightG
{
    float4 p;   // position (camera-centred global) [m]; w: range [m]
    float4 c;   // colour x intensity: the illuminance at 1 m [Sun = 1]; w: bits - 1 spot, 2 not on the camera's own cockpit
                //  (an outside light of the vessel the eye is in: the world through the visor, not the helmet), 4 the cockpit only
    float4 d;   // spot: its direction (global); w: cos of the penumbra's half aperture
    float4 a;   // x: cos of the umbra's half aperture, y: the lamp's radius [m] (nearest distance), z: shadow tile (-1 none),
                // w: 1 = lights the air (the beams pass)
};
cbuffer LocalLights : register(b9)
{
    float4x4 gLVP;          // camera-centred global -> clip, without the TAA jitter (the cluster grid)
    float4 gLGrid;          // x, y: tiles across, down; z: slices; w: number of lights
    float4 gLZ;             // x: slice 0's far end [m], y: slices per ln(z / x), z: 1 = on, w: the spot shadows' texel (atlas)
    float4x4 gSpotVP[4];    // camera-centred global -> each spot shadow tile's clip
    float4 gSpotK[4];       // x: texel size at 1 m [m] (the normal offset), y: near, z: far, w: 0
    float4x4 gSkyVP[12];    // the sky's visibility: camera-centred global -> each direction's map (4 x 4 atlas of 256)
    float4 gSkyDir[12];     // xyz: to the sky (global); w: 0
    float4 gSkyC;           // xyz: the maps' centre (camera-centred); w: their radius [m] (0: off)
    float4 gSkyK;           // x: texel [m], y: the atlas' texel (uv)
};
StructuredBuffer<LocalLightG> tLights : register(t17);
StructuredBuffer<uint2> tLCluster : register(t18);
StructuredBuffer<uint> tLIndex : register(t19);
Texture2D<float> tSpotShadow : register(t20);
Texture2D<float> tSkyVis : register(t21);

bool LightIsSpot(LocalLightG L) { return ((uint)L.c.w & 1) != 0; }
// set by mesh.hlsl for the camera's own cockpit (its helmet, its cabin): 1 there, 0 for the world
static float gLLOwn = 0;
bool LocalLightsOn() { return gLZ.z > 0.5 && gLGrid.w > 0.5; }

// the spot's shadow at p (tile t of the 2 x 2 atlas, 1024 each): 3 x 3 hardware-compared bilinear taps
float SpotShadow(int t, float3 p)
{
    float4 q = mul(float4(p, 1), gSpotVP[t]);
    if (q.w <= 1e-4) return 1;
    float3 c = q.xyz / q.w;
    float2 uv = c.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.002) || any(uv > 0.998) || c.z > 1) return 1;
    float2 a = (float2(t & 1, t >> 1) + uv) * 0.5;
    const float k = gLZ.w;
    float s = 0;
    [unroll] for (int j = -1; j <= 1; j++) [unroll] for (int i = -1; i <= 1; i++)
        s += tSpotShadow.SampleCmpLevelZero(sShadow, a + float2(i, j) * k, c.z);
    return s / 9;
}

// the illuminance [Sun = 1] from light L at wp (direction to it in Ldir), its shadow included (ng: the geometric normal)
float3 LocalLightE(LocalLightG L, float3 wp, float3 ng, out float3 Ldir)
{
    float3 v = L.p.xyz - wp;
    float d2 = dot(v, v);
    Ldir = v * rsqrt(max(d2, 1e-12));
    float r2 = L.p.w * L.p.w;
    if (d2 >= r2) return 0;
    float w = saturate(1 - (d2 / r2) * (d2 / r2)); w *= w;
    float3 E = L.c.rgb * w / max(d2, L.a.y * L.a.y);
    // the lamp's own housing and glass (closer than the spot shadow map's near plane) are not lit by it: the light leaves
    // through the lens - without this they took I / r^2 at centimetres (thousands of Suns) and the bloom made discs of them
    float dd = sqrt(d2);
    E *= LightIsSpot(L) ? smoothstep(0.12, 0.35, dd) : smoothstep(0.02, 0.10, dd);
    uint fl = (uint)L.c.w;
    if (gLLOwn > 0.5 ? (fl & 2) != 0 : (fl & 4) != 0) return 0;     // an outside light on its own helmet / a cockpit light on the world
    if (LightIsSpot(L)) {
        float cd = dot(-Ldir, L.d.xyz);
        if (cd <= L.d.w) return 0;
        float e = smoothstep(L.d.w, L.a.x, cd);
        E *= e * e;
    }
    if (L.a.z >= 0) {
        int t = (int)L.a.z;
        float d = sqrt(d2);
        E *= SpotShadow(t, wp + ng * (gSpotK[t].x * d * 1.5) + Ldir * (gSpotK[t].x * d * 0.5));
    }
    return E;
}

// the cluster of wp: (offset, count) into tLIndex; count = 0xFFFFFFFF: wp outside the grid - test every light
uint2 LocalCluster(float3 wp)
{
    float4 q = mul(float4(wp, 1), gLVP);
    if (q.w <= 0.05) return uint2(0, 0xFFFFFFFF);
    float2 ndc = q.xy / q.w;
    if (any(abs(ndc) > 1)) return uint2(0, 0xFFFFFFFF);
    uint2 tile = min((uint2)((ndc * float2(0.5, -0.5) + 0.5) * gLGrid.xy), (uint2)gLGrid.xy - 1);
    uint s = q.w <= gLZ.x ? 0 : min((uint)(1 + log(q.w / gLZ.x) * gLZ.y), (uint)gLGrid.z - 1);
    return tLCluster[(s * (uint)gLGrid.y + tile.y) * (uint)gLGrid.x + tile.x];
}

// how much of the sky a point sees past the vessels round the camera (a cabin's walls see it through the windows only;
// the ground under a vehicle less of it): the vessels' depth seen from 12 directions over the sky, each tested at the
// point, weighted by how the surface faces them (a cosine, softened). Within gSkyC.w of the centre, fading out at the edge
float SkyVisibility(float3 wp, float3 ng)
{
    if (gSkyC.w <= 0) return 1;
    float3 rc = wp - gSkyC.xyz;
    float fade = saturate((gSkyC.w - length(rc)) / (0.2 * gSkyC.w));
    if (fade <= 0) return 1;
    float3 p = wp + ng * (gSkyK.x * 1.5);
    float s = 0, ws = 0;
    [unroll] for (int i = 0; i < 12; i++) {
        float w = saturate(dot(ng, gSkyDir[i].xyz) * 0.5 + 0.5); w *= w;
        if (w <= 0.001) continue;
        float4 q = mul(float4(p, 1), gSkyVP[i]);
        float2 uv = q.xy * float2(0.5, -0.5) + 0.5;
        float v = 1;
        if (all(uv > 0.004) && all(uv < 0.996) && q.z < 1)
            v = tSkyVis.SampleCmpLevelZero(sShadow, (float2(i & 3, i >> 2) + uv) * 0.25, q.z);
        s += v * w; ws += w;
    }
    return ws > 0 ? lerp(1, s / ws, fade) : 1;
}

float D_GGX_L(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1) + 1; return a2 / (3.14159265 * d * d + 1e-7); }

// what the local lights add to a surface: albedo (diffuse), F0 and a (GGX roughness^2) for the specular, n the shading
// normal, ng the geometric one, V to the eye. In the client's units (as the Sun's term: albedo x E, spec x E x pi)
float3 LocalLightShade(LocalLightG L, float3 wp, float3 n, float3 ng, float3 V, float3 albedo, float3 F0, float a)
{
    float3 Ld;
    float3 E = LocalLightE(L, wp, ng, Ld);
    float nl = saturate(dot(n, Ld));
    if (nl <= 0 || !any(E > 0)) return 0;
    float3 H = normalize(V + Ld);
    float nv = saturate(dot(n, V)) + 1e-4, nh = saturate(dot(n, H));
    float3 Fs = F0 + (1 - F0) * pow(1 - saturate(dot(V, H)), 5);
    float k = a * 0.5, G = (nl / (nl * (1 - k) + k)) * (nv / (nv * (1 - k) + k));
    float3 spec = D_GGX_L(nh, a) * G * Fs / (4 * nl * nv + 1e-4);
    return E * nl * (albedo * (1 - Fs) + spec * 3.14159265);
}

float3 LocalLights(float3 wp, float3 n, float3 ng, float3 V, float3 albedo, float3 F0, float a)
{
    if (!LocalLightsOn()) return 0;
    float3 s = 0;
    uint2 cl = LocalCluster(wp);
    if (cl.y == 0xFFFFFFFF) {
        uint nAll = min((uint)gLGrid.w, 64);
        [loop] for (uint i = 0; i < nAll; i++) s += LocalLightShade(tLights[i], wp, n, ng, V, albedo, F0, a);
    } else {
        [loop] for (uint i = 0; i < cl.y; i++) s += LocalLightShade(tLights[tLIndex[cl.x + i]], wp, n, ng, V, albedo, F0, a);
    }
    return s;
}
// the diffuse part only (the terrain): albedo x the illuminance on the surface
float3 LocalLightsDiffuse(float3 wp, float3 n, float3 ng, float3 albedo)
{
    if (!LocalLightsOn()) return 0;
    float3 s = 0, Ld;
    uint2 cl = LocalCluster(wp);
    uint cnt = cl.y == 0xFFFFFFFF ? min((uint)gLGrid.w, 64) : cl.y;
    [loop] for (uint i = 0; i < cnt; i++) {
        LocalLightG L = tLights[cl.y == 0xFFFFFFFF ? i : tLIndex[cl.x + i]];
        float3 E = LocalLightE(L, wp, ng, Ld);
        s += E * saturate(dot(n, Ld));
    }
    return albedo * s;
}

#endif
