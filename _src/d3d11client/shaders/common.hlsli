// D3D11Client: per-frame constants (b0) - the camera, the Sun, the atmosphere the camera is in
cbuffer Frame : register(b0)
{
    float4x4 gInvVP;    // clip -> camera-centred global frame
    float4 gSunDir;     // to the Sun (global); w: daylight at the camera (0 night .. 1 day)
    float4 gSky0;       // horizon colour; w: 1 inside an atmosphere
    float4 gSky1;       // zenith colour; w: sky strength (fades with altitude)
    float4 gAmb;        // sky light on surfaces; w: haze extinction [1/m] at the ground
    float4 gSun;        // direct sunlight colour; w: haze scale height [m]
    float4 gUp;         // local vertical at the camera (global); w: camera altitude [m]
    float4 gPlanet;     // planet centre from the camera; w: radius [m]
    float4 gHazeRGB;    // spectral weights of the extinction; w: exposure
    float4x4 gLightVP;  // camera-centred global -> shadow map
    float4 gShadow;     // x: on, y: texel
    float4 gAdapt;      // the eye's adaptation: rgb the colour balance (applied in the tone mapping), w the brightness
                        // factor included in gHazeRGB.w; things seen as they are (the HUD, displays) divide both out
    float4 gLiq0;       // the liquid under the camera: its planet's centre from the camera; w: the level's radius (0: none)
    float4 gLiq1;       // its in-scattered colour; w: extinction [1/m]
    float4 gLiq2;       // its channel weights; w: 1 = on
    float4x4 gLightVP2; // (unused: the cascades below)
    float4x4 gCasc[4];  // camera-centred global -> each shadow cascade (atlas tiles 2 x 2)
    float4 gCascFar;    // each cascade's far end, view distance [m]
    float4 gCascTex;    // each cascade's texel [m] (the normal offset)
};
// a NaN or an infinity, tested on the bits (the compiler drops isnan() under its default float rules)
bool NotFinite(float3 v) { return any((asuint(v) & 0x7FFFFFFF) >= 0x7F800000); }
Texture2D<float> tShadow : register(t7);
SamplerComparisonState sShadow : register(s2);
#include "fx_atmo_light.hlsli"     // the FX atmosphere's light (t8, b4): FxActive(), FxSunLight(wp), FxSkyLight(n, wp)

// the Sun's visibility at wp (camera-centred) through the cascaded shadow maps (Client.cpp RenderShadows): the cascade by
// the view distance, the point moved along the surface normal by a texel and a half of that cascade (normal offset: no
// self-shadowing acne, no light leaking at contacts), 3x3 hardware-compared taps; across the last 10 % of a cascade it
// blends into the next (no seam); beyond the last, lit
float CascadeTap(int ci, float3 p)
{
    float4 q = mul(float4(p, 1), gCasc[ci]);
    float2 uv = q.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.001) || any(uv > 0.999) || q.z > 1) return 1;
    const float t = gShadow.y;                       // 1 / SHADOW_N: a texel of a tile
    float2 a = (float2(ci & 1, ci >> 1) + uv) * 0.5;  // in the atlas
    float s = 0;
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++)
        s += tShadow.SampleCmpLevelZero(sShadow, a + float2(x, y) * t * 0.5, q.z);
    return s / 9;
}
float ShadowAtN(float3 wp, float3 n)
{
    if (gShadow.x < 0.5) return 1;
    float d = length(wp);
    int ci = d < gCascFar.x ? 0 : d < gCascFar.y ? 1 : d < gCascFar.z ? 2 : d < gCascFar.w ? 3 : 4;
    if (ci > 3) return 1;
    float far = gCascFar[ci], near = ci > 0 ? gCascFar[ci - 1] : 0;
    float s = CascadeTap(ci, wp + n * gCascTex[ci] * 1.5);
    float bl = saturate((d - lerp(near, far, 0.9)) / max(far - lerp(near, far, 0.9), 1e-3));
    if (bl > 0) s = lerp(s, ci < 3 ? CascadeTap(ci + 1, wp + n * gCascTex[min(ci + 1, 3)] * 1.5) : 1, bl);
    return s;
}
float ShadowAt(float3 wp) { return ShadowAtN(wp, float3(0, 0, 0)); }

float3 SkyColour(float3 dir)
{
    float e = dot(dir, gUp.xyz);
    float3 hor = gSky0.rgb * (0.8 + 0.4 * saturate(dot(dir, gSunDir.xyz)));
    float3 s = lerp(hor, gSky1.rgb, pow(saturate(e), 0.6));
    float c = saturate(dot(dir, gSunDir.xyz));
    s += gSun.rgb * (0.5 * pow(c, 24.0) + 0.3 * pow(c, 400.0));
    return s * gSunDir.w * gSky1.w;
}

// aerial perspective: Beer-Lambert along the view ray, the haze thinning with height; in-scattered light = the horizon sky
float3 ApplyHaze(float3 col, float3 wp)
{
    if (FxActive()) return col;
    if (gSky0.w < 0.5) return col;
    float dist = length(wp);
    float hp = length(wp - gPlanet.xyz) - gPlanet.w;
    float hm = max(0.0, 0.5 * (hp + gUp.w));
    float3 ext = gAmb.w * gHazeRGB.rgb * exp(-hm / max(gSun.w, 1.0));
    float3 tr = exp(-ext * dist);
    float3 ins = gSky0.rgb * gSunDir.w * gSky1.w;
    return col * tr + ins * (1 - tr);
}

// light on a surface with normal n: the direct Sun (white in space, the atmosphere's colour below it) and the sky
float3 SunLight(float3 wp) { if (FxActive()) return FxSunLight(wp); return gSky0.w > 0.5 ? gSun.rgb : float3(1, 1, 1); }
float3 SunLight() { return SunLight(float3(0, 0, 0)); }
float3 SkyLight(float3 n, float3 wp)
{
    if (FxActive()) return FxSkyLight(n, wp);
    if (gSky0.w < 0.5) return float3(0.02, 0.02, 0.02);
    float up = dot(n, gUp.xyz) * 0.5 + 0.5;
    return gAmb.rgb * lerp(0.35, 1.0, up) * gSunDir.w * gSky1.w;
}
float3 SkyLight(float3 n) { return SkyLight(n, float3(0, 0, 0)); }
