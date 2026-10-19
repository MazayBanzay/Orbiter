// D3D11Client: a liquid surface (Titan's methane-ethane seas): Fresnel (n = 1.29: F0 0.016), the sky in the mirror,
// refraction with the apparent depth walked along the bent ray in the scene depth, Beer-Lambert per channel for the
// column under each pixel, the liquid's own colour scattered in, the Sun's glint (GGX), the haze.
#include "common.hlsli"
#include "fx_atmo_sky.hlsli"     // the FX atmosphere's sky in the mirror

cbuffer Liquid : register(b1)
{
    float4x4 gVP;
    float4x4 gLInvVP;
    float4 gBase;     // camera -> the liquid point under it; w: radius of the liquid sphere
    float4 gLUp;      // w: grid radius
    float4 gE1, gE2;
    float4 gColour;   // w: extinction [1/m]
    float4 gWts;      // w: refractive index
    float4 gLMisc;    // x: time
    float4 gWA;       // the wave field (FxWaves): its frame's origin from the camera; w: cell size [m]
    float4 gWE1, gWE2;   // its axes; w: the window's first cell (i0, j0)
    float4 gWN;       // x: cells per side, y: 1 = on
};
Texture2D tScene : register(t0);
Texture2D<float> tDepth : register(t1);
Texture2D<float4> tWave : register(t2);   // slopes gx, gz, height, laplacian per cell (periodic)
SamplerState sWrap : register(s0);
SamplerState sClamp : register(s1);

// the wave field at wp (weighted 0 outside its window, faded over the last 32 cells)
float4 WaveAt(float3 wp, out float wgt)
{
    wgt = 0;
    if (gWN.y < 0.5) return 0;
    float3 d = wp - gWA.xyz;
    float2 q = float2(dot(d, gWE1.xyz), dot(d, gWE2.xyz)) / gWA.w;    // in cells
    float2 rel = q - float2(gWE1.w, gWE2.w);
    float e = min(min(rel.x, gWN.x - rel.x), min(rel.y, gWN.x - rel.y));
    wgt = saturate(e / 32);
    if (wgt <= 0) return 0;
    return tWave.SampleLevel(sWrap, q / gWN.x, 0) * wgt;
}

struct VSOut { float4 pos : SV_Position; float3 wp : WPOS; float3 n : NORMAL; };

VSOut VS(float2 g : POSITION)
{
    // ring radius grows geometrically from 0.3 m to the grid radius; the point on the sphere, relative to the camera,
    // built from small terms (no cancellation of planet-sized numbers)
    float r = g.x <= 0 ? 0 : 0.3 * pow(gLUp.w / 0.3, g.x);
    float th = g.y * 6.2831853;
    float phi = r / gBase.w;
    float3 t = gE1.xyz * cos(th) + gE2.xyz * sin(th);
    float3 d = gLUp.xyz * (-2 * sin(phi * 0.5) * sin(phi * 0.5)) + t * sin(phi);   // unit direction minus up
    float3 wp = gBase.xyz + gBase.w * d;
    VSOut o;
    o.n = normalize(gLUp.xyz * cos(phi) + t * sin(phi));
    float ww; float4 wv = WaveAt(wp, ww);
    wp += o.n * wv.z;                                  // the wave's height
    o.wp = wp;
    o.pos = mul(float4(wp, 1), gVP);
    return o;
}

float3 WorldAt(float2 uv, float dz)
{
    float4 c = float4(uv.x * 2 - 1, 1 - uv.y * 2, dz, 1);
    float4 w = mul(c, gLInvVP);
    return w.xyz / w.w;
}

float D_GGX(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1) + 1; return a2 / (3.14159 * d * d); }

float4 PS(VSOut i) : SV_Target
{
    float3 up = normalize(i.n);
    // faint ripples (the seas are mirror-smooth to radar, < ~1 cm; the wind < 1 m/s)
    float3 t1 = normalize(cross(up, gE1.xyz)), t2 = cross(t1, up);
    float2 q = float2(dot(i.wp, gE1.xyz), dot(i.wp, gE2.xyz));
    float tm = gLMisc.x;
    float2 sl = float2(sin(q.x * 2.1 + tm * 1.3) + sin(q.y * 3.7 - tm * 0.9), sin(q.y * 2.7 + tm * 1.1) + sin(q.x * 4.3 + tm * 0.7)) * 0.004;
    float3 n = normalize(up + t1 * sl.x + t2 * sl.y);
    float ww; float4 wv = WaveAt(i.wp, ww);            // the wave field's slopes inside its window
    if (ww > 0) n = normalize(lerp(n, normalize(up - wv.x * gWE1.xyz - wv.y * gWE2.xyz), ww));

    float dist = length(i.wp);
    float3 V = -i.wp / dist;
    float nv = saturate(dot(n, V)) + 1e-4;
    float F0 = pow((gWts.w - 1) / (gWts.w + 1), 2);
    float F = F0 + (1 - F0) * pow(1 - nv, 5);
    float3 R = reflect(-V, n);
    float3 refl = FxActive() ? FxSkyRadiance(i.wp, R, 12) : SkyColour(R);

    // refraction: walk the bent ray to the bed seen in the depth buffer
    float tw, th2; tScene.GetDimensions(tw, th2);
    float2 uv = i.pos.xy / float2(tw, th2);
    float3 T = refract(-V, n, 1.0 / gWts.w);
    float3 sp = WorldAt(uv, tDepth.SampleLevel(sClamp, uv, 0));
    float depthV = max(dot(i.wp - sp, up), 0);
    float2 suv = uv;
    [unroll] for (int k = 0; k < 3; k++) {
        float path = depthV / max(-dot(T, up), 0.05);
        float3 bp = i.wp + T * path;
        float4 c = mul(float4(bp, 1), gVP);
        float2 nuv = c.xy / c.w * float2(0.5, -0.5) + 0.5;
        if (any(nuv < 0) || any(nuv > 1)) break;
        float3 p2 = WorldAt(nuv, tDepth.SampleLevel(sClamp, nuv, 0));
        if (dot(p2 - i.wp, up) > 0.02) break;     // something above the surface there: keep the last
        suv = nuv;
        depthV = max(dot(i.wp - p2, up), 0);
    }
    float path = depthV / max(-dot(T, up), 0.05);
    float3 bed = tScene.SampleLevel(sClamp, suv, 0).rgb;
    float3 tr = exp(-gColour.w * gWts.rgb * path);
    float3 inl = gColour.rgb * SkyLight(up) * 0.35 * (1 - tr);
    float3 refr = bed * tr + inl;

    float3 L = gSunDir.xyz;
    float3 Hh = normalize(V + L);
    float nl = saturate(dot(n, L)) * ShadowAt(i.wp);
    float glint = D_GGX(saturate(dot(n, Hh)), 0.0064) * F0 * nl / (4 * nv + 1e-3);
    float3 col = lerp(refr, refl, F) + glint * SunLight();
    return float4(ApplyHaze(col, i.wp), 1);
}
