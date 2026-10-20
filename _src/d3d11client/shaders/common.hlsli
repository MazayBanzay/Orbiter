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
    float4 gCascRange;  // each cascade's depth range [m] (the blocker's distance for the penumbra)
};
// a NaN or an infinity, tested on the bits (the compiler drops isnan() under its default float rules)
bool NotFinite(float3 v) { return any((asuint(v) & 0x7FFFFFFF) >= 0x7F800000); }
Texture2D<float> tShadow : register(t7);
SamplerComparisonState sShadow : register(s2);
#include "fx_atmo_light.hlsli"     // the FX atmosphere's light (t8, b4): FxActive(), FxSunLight(wp), FxSkyLight(n, wp)
#include "lights.hlsli"            // the vessels' local lights (b9, t17..t20, Lights.cpp): LocalLights(...), LocalLightsDiffuse(...)

// the Sun's visibility at wp (camera-centred). Tile 0 of the atlas: the map fitted to the focus vessel (as D3D9Client's
// one shadow map) - used wherever the point lies in its box; tiles 1..3: cascades over the view's depth for the rest.
// The point is moved along the surface normal by a texel and a half of its map (no self-shadowing acne, no leak at
// contacts). Percentage-closer soft shadows (NVIDIA PCSS, as the DX11 samples do it): a blocker search gives the
// distance from the receiver to what shades it, the penumbra is that distance times the Sun's angular diameter (sharp
// where a foot or a wheel meets the ground, softer away from it); the filter is a 4 x 4 grid of hardware-compared
// bilinear taps (a smooth 5 x 5 tent, no random rotation: no grain), widened to the penumbra. Across the last 10 % of a
// cascade it blends into the next
// the PCF grid at a (atlas) for depth z: 4 x 4 compared bilinear taps k texels apart
float CascadePCF(float2 a, float z, float k)
{
    const float t = gShadow.y;
    float s = 0;
    [unroll] for (int j = 0; j < 4; j++) [unroll] for (int i = 0; i < 4; i++)
        s += tShadow.SampleCmpLevelZero(sShadow, a + (float2(i, j) - 1.5) * k * t * 0.5, z);
    return s / 16;
}
// a light of angular diameter ang [rad] seen from p through cascade ci (-1: p not in it). The blocker search spans sr
// texels each way (wide enough for the widest penumbra the map is asked for), kmax caps the filter's spacing
float CascadeVis(int ci, float3 p, float ang, float sr, float kmax)
{
    float4 q = mul(float4(p, 1), gCasc[ci]);
    float2 uv = q.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.004) || any(uv > 0.996) || q.z > 1 || q.z < 0) return -1;     // not in this map
    const float t = gShadow.y;                       // 1 / SHADOW_N: a texel of a tile
    float2 a = (float2(ci & 1, ci >> 1) + uv) * 0.5;  // in the atlas
    const float N2 = 2 / t;                           // the atlas in texels
    // blocker search: the mean depth of what lies nearer the light (4 x 4 point loads over +-sr texels)
    float zb = 0, nb = 0;
    [unroll] for (int y = 0; y < 4; y++) [unroll] for (int x = 0; x < 4; x++) {
        float z = tShadow.Load(int3(a * N2 + (float2(x, y) - 1.5) * (sr / 1.5), 0));
        if (z < q.z - 1e-5) { zb += z; nb++; }
    }
    if (nb < 0.5) return 1;                           // nothing in front: lit (most of the screen: the loads only)
    zb /= nb;
    // the penumbra in texels: the blocker's distance [m] x the light's angular diameter / the texel [m]; the grid spans
    // about 3 k + 2 texels
    float pen = (q.z - zb) * gCascRange[ci] * ang / max(gCascTex[ci], 1e-6);
    return CascadePCF(a, q.z, clamp(pen / 3, 1, kmax));
}
float CascadeTap(int ci, float3 p) { return CascadeVis(ci, p, gShadow.w, 3, 4); }
float ShadowAtN(float3 wp, float3 n)
{
    if (gShadow.x < 0.5) return 1;
    if (gCascFar.x > -1.5) {                         // the focus map, where it covers the point
        float s0 = CascadeTap(0, wp + n * gCascTex.x * 1.5);
        if (s0 >= 0) return s0;
    }
    float d = length(wp);
    int ci = d < gCascFar.y ? 1 : d < gCascFar.z ? 2 : d < gCascFar.w ? 3 : 4;
    if (ci > 3) return 1;
    float far = gCascFar[ci], near = ci > 1 ? gCascFar[ci - 1] : 0;
    float s = CascadeTap(ci, wp + n * gCascTex[ci] * 1.5);
    s = s < 0 ? 1 : s;
    float bl = saturate((d - lerp(near, far, 0.9)) / max(far - lerp(near, far, 0.9), 1e-3));
    if (bl > 0) {
        float s2 = ci < 3 ? CascadeTap(ci + 1, wp + n * gCascTex[min(ci + 1, 3)] * 1.5) : 1;
        s = lerp(s, s2 < 0 ? 1 : s2, bl);
    }
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

// ---- the aureole: in a hazy air (Titan) most of the light is scattered, and the aerosol scatters it forward, so the sky
// round the Sun is the brightest of it - a wide light the objects shade too: dark and sharp where a foot meets the
// ground, a soft fading shade further from it (the direct beam's own shadow is sharp everywhere: the Sun's disc). From
// the air's own aerosol (Henyey-Greenstein g, its share of the scattering): x the cone's angular diameter [rad] (the
// phase function's half-intensity cone), yzw the sky light's share inside it per channel (single scattering: the
// aerosol's part of the scattering in each colour - on Titan the red light is mostly the aerosol's, the blue Rayleigh's)
float4 Aureole()
{
    if (!FxActive()) return 0;
    float g = clamp(dot(mMieG.rgb, 1.0 / 3), 0.05, 0.95);
    float c = clamp((1 + g * g - (1 - g) * (1 - g) * 1.5874) / (2 * g), -1, 1);   // P(theta) = P(0) / 2
    float F = (1 - g * g) / (2 * g) * (1 / (1 - g) - rsqrt(1 + g * g - 2 * g * c));   // HG's energy inside the cone
    float3 sh = mMieSca.rgb / max(mMieSca.rgb + mRay.rgb, 1e-12);
    return float4(min(2 * acos(c), 1.2), saturate(F * sh));
}
// the aureole's visibility at wp (the view cascades: its penumbrae are centimetres to metres wide, the focus map's
// millimetre texels would need too many taps)
float AureoleVis(float3 wp, float3 n, float ang)
{
    if (gShadow.x < 0.5) return 1;
    float d = length(wp);
    int ci = d < gCascFar.y ? 1 : d < gCascFar.z ? 2 : 3;
    if (ci > 2) return 1;                             // beyond ~50 m its shade is lost in the coarse texels and the haze
    float s = CascadeVis(ci, wp + n * gCascTex[ci] * 1.5, ang, 9, 8);
    return s < 0 ? 1 : s;
}
// the sky's light on a surface, the aureole's part shaded by what stands between it and the Sun
float3 SkyLightShaded(float3 n, float3 wp, float3 ng)
{
    float3 sky = SkyLight(n, wp);
    float4 au = Aureole();
    if (max(au.y, max(au.z, au.w)) < 0.01 || gShadow.z > 9.5) return sky;     // (test runs: SHADOWDBG 10, 11 profile without it)
    float3 w = au.yzw * saturate(dot(n, gSunDir.xyz) * 0.5 + 0.5);   // the cone lights the Sun-facing side
    return sky * (1 - w * (1 - AureoleVis(wp, ng, au.x)));
}
