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
};
Texture2D<float> tShadow : register(t7);
SamplerComparisonState sShadow : register(s2);
#include "fx_atmo_light.hlsli"     // the FX atmosphere's light (t8, b4): FxActive(), FxSunLight(wp), FxSkyLight(n, wp)

// the Sun's visibility at wp: 3x3 PCF in the shadow map (1 outside it)
float ShadowAt(float3 wp)
{
    if (gShadow.x < 0.5) return 1;
    float4 p = mul(float4(wp, 1), gLightVP);
    float2 uv = p.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0) || any(uv > 1) || p.z > 1) return 1;
    float s = 0;
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++)
        s += tShadow.SampleCmpLevelZero(sShadow, uv + float2(x, y) * gShadow.y, p.z);
    return s / 9;
}

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
