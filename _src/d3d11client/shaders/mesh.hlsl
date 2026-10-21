// D3D11Client: vessel meshes (Orbiter materials, diffuse texture, the Sun, the sky light, the haze)
#include "common.hlsli"
cbuffer Obj : register(b1)
{
    float4x4 gWVP;
    float4x4 gW;        // to the camera-centred global frame
    float4 gSunM;       // direction to the Sun (global)
    float4 gDiff, gMAmb, gSpec, gEmis;   // spec.w = power
    float4 gFlags;      // x: texture, y: cockpit mesh, z: 1 = not lit (the group's flag 4: a display, a light), w: 1 = seen as it is (cockpit)
    float4x4 gCurNJ;    // this frame's WVP without the TAA jitter
    float4x4 gPrevNJ;   // last frame's
    float4 gMaps;       // the texture's companion maps, as D3D9Client names them (1 = there): x _norm, y _spec, z _rghn, w _metl
    float4 gMaps2;      // x _emis; y 1 = skin (light scattered under the surface)
};
Texture2D tNorm : register(t1);     // tangent space normal: +x along u, +y along v
Texture2D tSpecM : register(t2);    // specular colour
Texture2D tRghn : register(t3);     // smoothness (G)
Texture2D tMetl : register(t4);     // metalness (R)
Texture2D tEmisM : register(t5);    // emission colour
struct PSOut { float4 c : SV_Target0; float2 mv : SV_Target1; };
cbuffer Mirror : register(b6)
{
    float4 gMirN;       // the liquid's mirror pass: the plane's normal; w: 1 = on (parts under the plane are cut)
    float4 gMirP;       // a point of the plane (camera-centred)
};
cbuffer Emis2 : register(b8)
{
    float4 gEmis2;      // the material's emission2 set by a module (oapiSetMaterialEx Emission), w = 1 once set: it multiplies the emission map
};
cbuffer Dirt : register(b7)
{
    float4 gDirtO;      // the vessel's origin (camera-centred); w: amount 0..1 (0: clean)
    float4 gDirtX, gDirtY, gDirtZ;   // its axes; gDirtY.w: topY [m]
    float4 gDirtC;      // the dirt's albedo
};
Texture2D tDiff : register(t0);
SamplerState sAniso : register(s0);

struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD; };
struct VSOut { float4 pos : SV_Position; float3 wp : WPOS; float3 n : NORMAL; float2 uv : TEXCOORD; float4 cc : CURCLIP; float4 pc : PREVCLIP; float2 liq : LIQ; };

// Things under a clear liquid seen from above (as the AtmoFX D3D9 fork): the image of a point at depth d lies on the
// vertical through it where the camera's ray, bent at the surface by Snell's law, seems to come from:
// d' = d cos(i) / (n cos(t)). Legs look shorter, the bottom higher, and both move against the waterline with the camera.
// c: the camera from the point, up: the local vertical, n: the refractive index
float LiqApparentDepth(float3 c, float3 up, float d, float n)
{
    float ch = dot(c, up);
    float Hc = ch - d;                                   // the camera over the level
    if (Hc <= 0.005) return d;
    float X = length(c - up * ch);
    float s = X * rsqrt(X * X + ch * ch + 1e-8);
    float n2 = n * n;
    [unroll] for (int k = 0; k < 6; k++) {               // Hc tan(i) + d tan(t) = X, sin(t) = sin(i) / n
        float u = 1 - s * s, w = 1 - s * s / n2;
        float f = Hc * s * rsqrt(u) + d * (s / n) * rsqrt(w) - X;
        float df = Hc * rsqrt(u * u * u) + (d / n) * rsqrt(w * w * w);
        s = clamp(s - f / df, 0.0, 0.9995);
    }
    float ci = sqrt(1 - s * s), ct = sqrt(1 - s * s / n2);
    return d * ci / (n * ct);
}

// the world's dirt (AtmoFX_SetDirt): thick below topY in the vessel's frame, fading above it over ~0.6 m in ragged spots
// and downward runs; a light dusting on the surfaces facing up. Value noise in the vessel's own metres (it stays put on it)
float DirtHash(float3 p) { return frac(sin(dot(p, float3(12.9898, 78.233, 37.719))) * 43758.5453); }
float DirtNoise(float3 p)
{
    float3 i = floor(p), f = frac(p); f = f * f * (3 - 2 * f);
    float a = lerp(DirtHash(i), DirtHash(i + float3(1, 0, 0)), f.x), b = lerp(DirtHash(i + float3(0, 1, 0)), DirtHash(i + float3(1, 1, 0)), f.x);
    float c = lerp(DirtHash(i + float3(0, 0, 1)), DirtHash(i + float3(1, 0, 1)), f.x), d = lerp(DirtHash(i + float3(0, 1, 1)), DirtHash(i + float3(1, 1, 1)), f.x);
    return lerp(lerp(a, b, f.y), lerp(c, d, f.y), f.z);
}
float DirtCover(float3 wp, float3 n)
{
    if (gDirtO.w <= 0) return 0;
    float3 d = wp - gDirtO.xyz;
    float3 q = float3(dot(d, gDirtX.xyz), dot(d, gDirtY.xyz), dot(d, gDirtZ.xyz));
    float nz = DirtNoise(q * 3) * 0.6 + DirtNoise(q * 11) * 0.4;
    float streak = DirtNoise(float3(q.x * 7, q.y * 0.7, q.z * 7));
    float h = (q.y - gDirtY.w) / 0.6 + (nz - 0.5) * 0.9 - streak * 0.6;
    float low = (1 - smoothstep(0, 1, h)) * (0.7 + 0.3 * nz);
    float top = 0.3 * smoothstep(0.6, 0.95, dot(n, gDirtY.xyz)) * saturate(nz * 1.6 - 0.3);
    return saturate(max(low, top)) * gDirtO.w;
}

VSOut VS(VSIn v)
{
    VSOut o;
    float3 p = v.p;
    float3 wp = mul(float4(p, 1), gW).xyz;
    o.liq = float2(0, 1);
    // the parts under a liquid are drawn where they are: the liquid's surface comes after the vessels (Liquid.cpp) and
    // shows them through itself, bent, dimmed and under its waves (no moved vertices: no seam at the waterline)
    o.pos = mul(float4(p, 1), gWVP);
    o.wp = mul(float4(p, 1), gW).xyz;
    o.n = mul(v.n, (float3x3)gW);
    o.uv = v.uv;
    o.cc = mul(float4(p, 1), gCurNJ);
    o.pc = mul(float4(p, 1), gPrevNJ);
    return o;
}

// the normal map without stored tangents: the frame from the screen derivatives of the position and the uv (Schüler,
// "Followup: normal mapping without precomputed tangents"); T along u, B along v
float3 PerturbNormal(float3 n, float3 wp, float2 uv, float2 txy)
{
    float3 dp1 = ddx(wp), dp2 = ddy(wp);
    float2 duv1 = ddx(uv), duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float m = max(dot(T, T), dot(B, B));
    if (m < 1e-20) return n;
    float im = rsqrt(m);
    float tz = sqrt(saturate(1 - dot(txy, txy)));
    float3 r = T * im * txy.x + B * im * txy.y + n * tz;
    return dot(r, r) > 1e-12 ? normalize(r) : n;
}

// specular anti-aliasing (Kaplanyan & Hoffman 2016): how much the normal turns across the pixel, as extra roughness
// (squared). A normal-mapped or curved glossy surface would otherwise sparkle - dots no MSAA sample can smooth
float SpecAAKernel(float3 n)
{
    float3 du = ddx(n), dv = ddy(n);
    return min(0.5 * (dot(du, du) + dot(dv, dv)), 0.18);
}

float D_GGX_M(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1) + 1; return a2 / (3.14159265 * d * d + 1e-7); }

// what a glossy surface mirrors: the sky's light above the horizon, the lit ground below it (sun on it plus the sky's
// light, albedo ~0.2), with a horizon line as sharp as the surface is smooth - a visor or a polished plate reads as glass
// and metal by that line; the sky's harmonics alone gave it one flat colour (2026-10-19)
float3 EnvReflect(float3 R, float3 wp, float rough)
{
    float3 up = gUp.xyz;
    float h = dot(R, up);
    float3 sky = SkyLight(R, wp);
    float3 gnd = 0.2 * (SunLight(wp) * saturate(dot(up, gSunM.xyz)) + SkyLight(up, wp));
    float wdt = 0.015 + 0.35 * rough;
    return lerp(gnd, sky * (1.0 + 0.6 * saturate(1 - h * 4)), smoothstep(-wdt, wdt, h));   // the sky brighter near the horizon
}

PSOut PS(VSOut i, bool front : SV_IsFrontFace)
{
    PSOut o;
    if (gMirN.w > 0.5 && dot(i.wp - gMirP.xyz, gMirN.xyz) < 0) discard;   // under the liquid: nothing to reflect
    o.mv = (i.cc.xy / i.cc.w - i.pc.xy / i.pc.w) * float2(0.5, -0.5);   // uv now - uv then
    float4 tex = gFlags.x > 0 ? tDiff.Sample(sAniso, i.uv) : float4(1, 1, 1, 1);
    float4 base = gDiff * tex;
    if (gMaps2.z > 0.5) {      // a filter (group flag 0x100, the visor's sun spot): what lies behind times its transmittance,
        // blended Dest x Src. The texture's alpha (0.7 the spot's core) scales the density. Dense only over the Sun's disc
        // (out to 1.5 radii): there it is what brings the disc down to a bright white (gMaps2.y [log2], Sun.cpp - at most
        // welding glass, 10^-4.5); round it a light shade (one stop) - the spot's core is ~4 discs wide and at welding
        // glass the sky in it went black: a black disc with a dot (2026-10-20)
        float a = saturate(tex.a * gDiff.a / 0.7);
        float3 sd = normalize(gSunM.xyz), dir = normalize(i.wp);
        float sn = length(cross(dir, sd)), px = max(fwidth(sn), 1e-6);   // sin of the angle from the Sun's centre (precise when small)
        float rd = max(gMaps2.w, 1e-6);
        float core = dot(dir, sd) > 0 ? 1 - smoothstep(1.3 * rd, 1.6 * rd + 1.5 * px, sn) : 0;
        float T = exp2(-a * lerp(1.0, max(gMaps2.y, 1.0), core));
        o.c = float4(T, T, T, 1);
        return o;
    }
    if (gFlags.z > 0.5) {      // not lit: its own colour (a display, a lamp); the cockpit's ones keep their brightness to the eye
        float3 c = (base.rgb + gEmis.rgb * tex.rgb) / max(gAdapt.w * gAdapt.rgb, 1e-4);   // its own light: as it is on screen
        o.c = float4(c, base.a);
        return o;
    }
    gLLOwn = gFlags.w;      // the camera's own cockpit: the local lights that light it (lights.hlsli)
    float3 n = normalize(i.n);
    // the dirt: matte, its own albedo over the material (lighter on dark composite, darker on white)
    float dirtK = DirtCover(i.wp, n);
    base.rgb = lerp(base.rgb, gDirtC.rgb, dirtK);
    float3 V = normalize(-i.wp);
    float3 L = gSunM.xyz;
    if (gMaps.x > 0.5) {       // the normal map: seams, panels, folds, bevels catch the light
        float2 t = tNorm.Sample(sAniso, i.uv).rg * 2 - 1;
        n = PerturbNormal(n, i.wp, i.uv, t * (1 - dirtK * 0.7));
    }
    float sunSh = ShadowAtN(i.wp, normalize(i.n)) * InteriorSun(i.wp);     // the geometric normal: the offset against acne; no Sun inside a declared interior (D3D11Lights)
    float skyV = SkyVisibility(i.wp, normalize(i.n)); // the sky past the vessels round the camera: a cabin sees it through its windows (lights.hlsli)
    skyV *= 1 - VolumeWeight(i.wp);                    // none inside a declared interior volume (D3D11Lights.h)
    if (gMaps.z > 0.5 || gMaps.w > 0.5) {
        // the material maps (as D3D9Client's): smoothness and metalness -> GGX for the Sun, the sky's harmonics for the
        // light around (diffuse by the normal, specular by the mirrored direction, rough ones blurred by the SH anyway)
        float smooth = gMaps.z > 0.5 ? tRghn.Sample(sAniso, i.uv).g : 0.5;
        float metal = gMaps.w > 0.5 ? tMetl.Sample(sAniso, i.uv).r : 0.0;
        smooth *= 1 - dirtK; metal *= 1 - dirtK;
        float rough = max(1 - smooth, 0.06), a = rough * rough;
        a = sqrt(saturate(a * a + SpecAAKernel(n)));     // the normal's spread within the pixel widens the lobe
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), base.rgb, metal);
        float3 albedo = base.rgb * (1 - metal);
        float3 H = normalize(V + L);
        float nl0 = saturate(dot(n, L)), nv = saturate(dot(n, V)) + 1e-4, nh = saturate(dot(n, H));
        float3 Fs = F0 + (1 - F0) * pow(1 - saturate(dot(V, H)), 5);
        float k = a * 0.5, G = (nl0 / (nl0 * (1 - k) + k)) * (nv / (nv * (1 - k) + k));
        float3 spec = D_GGX_M(nh, a) * G * Fs / (4 * nl0 * nv + 1e-4);
        float3 sun = SunLight(i.wp) * sunSh;
        float3 fill = (FxActive() ? 0.02 / max(gHazeRGB.w * gAdapt.rgb, 1e-3) : 0.02) * gMAmb.rgb;
        float3 Fa = F0 + (max(1 - rough, F0) - F0) * pow(saturate(1 - nv), 5);      // Fresnel for the light around (rough: less)
        // skin (the texture's name says so: gMaps2.y): light scattered under it - the lit side's edge softens and
        // warms (red wraps furthest round the terminator, blue least), as the light comes back out reddened
        float3 dif = nl0.xxx;
        if (gMaps2.y > 0.5) { float d0 = dot(n, L); dif = saturate((d0 + float3(0.45, 0.22, 0.15)) / float3(1.45, 1.22, 1.15)); }
        float3 col = albedo * (dif * sun + SkyLightShaded(n, i.wp, normalize(i.n)) * skyV + fill) + spec * nl0 * sun * 3.14159265
                   + Fa * EnvReflect(reflect(-V, n), i.wp, rough) * skyV
                   + gEmis.rgb * tex.rgb / max(gAdapt.w * gAdapt.rgb, 1e-4);
        col += LocalLights(i.wp, n, normalize(i.n), V, albedo, F0, a, 1 - skyV);   // headlights, lamps (lights.hlsli), their bounce in an enclosed room
        // the emission map as D3D9 shows it: max(lit, map x emission2) - emission2 the module's glow colour (the jet pack's
        // chambers: 0 when cold), 1 when no module sets it
        if (gMaps2.x > 0.5) col = max(col, tEmisM.Sample(sAniso, i.uv).rgb * (gEmis2.w > 0.5 ? gEmis2.rgb : 1.0) / max(gAdapt.w * gAdapt.rgb, 1e-4));
        if (NotFinite(col)) col = 0; col = max(col, 0);     // never a negative or NaN colour (it would spread through the resolve)
        o.c = float4(ApplyHaze(col, i.wp), base.a);
        return o;
    }
    float nl = saturate(dot(n, L)) * sunSh;
    float3 col = base.rgb * (nl * SunLight(i.wp) + SkyLightShaded(n, i.wp, normalize(i.n)) * skyV + (FxActive() ? 0.02 / max(gHazeRGB.w * gAdapt.rgb, 1e-3) : 0.02) * gMAmb.rgb) + gEmis.rgb * tex.rgb / max(gAdapt.w * gAdapt.rgb, 1e-4);   // the faint fill: neutral and constant on screen under the eye's adaptation; the emission through the texture, as D3D9 lights it
    float4 specM = gMaps.y > 0.5 ? tSpecM.Sample(sAniso, i.uv) : float4(1, 1, 1, 1);    // the specular map scales the material's
    if (gSpec.w > 0 || gMaps.y > 0.5) {
        float3 H = normalize(V + L);
        float pw = gSpec.w > 0 ? gSpec.w : 20;
        { float a2 = 2 / (pw + 2) + SpecAAKernel(n); pw = max(2 / a2 - 2, 1); }   // the same for the Phong lobe
        float3 sc = (gSpec.w > 0 ? gSpec.rgb : float3(1, 1, 1)) * specM.rgb;
        col += sc * (1 - dirtK) * pow(saturate(dot(n, H)), max(pw, 1)) * nl * SunLight(i.wp);
        // the sky in the glossy surface (the sky light's harmonics in the mirrored direction: a soft reflection), by
        // Fresnel from the material's specular colour; the glossier, the more of it
        // (in the air only: in space the black sky would darken every glossy rim)
        if (FxActive()) {
            float3 R = reflect(-V, n);
            float3 F0 = saturate(sc) * 0.5;
            float3 F = F0 + (1 - F0) * pow(1 - saturate(dot(n, V)), 5);
            float gloss = saturate(log2(max(pw, 1)) / 7);
            col = lerp(col, SkyLight(R, i.wp) * skyV, F * gloss * (1 - dirtK) * 0.6);
        }
    }
    {   // the local lights (lights.hlsli): the Phong material as GGX - the lobe's width from its power (a^2 = 2 / (n + 2)),
        // F0 from its specular colour
        float pwL = gSpec.w > 0 ? gSpec.w : 20;
        float aL = sqrt(saturate(2 / (pwL + 2) + SpecAAKernel(n)));
        float3 F0L = (gSpec.w > 0 || gMaps.y > 0.5) ? saturate((gSpec.w > 0 ? gSpec.rgb : 1) * specM.rgb) * 0.25 * (1 - dirtK) : 0.02;
        col += LocalLights(i.wp, n, normalize(i.n), V, base.rgb, F0L, aL, 1 - skyV);   // and their bounce in an enclosed room
    }
    // under the liquid (seen from above it): the light goes down to the point and back up the refracted view ray
    // (as the D3D9 fork: exp(-ext (d + d / cos t)) per channel), the dark liquid body over what is lost
    if (gMaps2.x > 0.5) col = max(col, tEmisM.Sample(sAniso, i.uv).rgb * (gEmis2.w > 0.5 ? gEmis2.rgb : 1.0) / max(gAdapt.w * gAdapt.rgb, 1e-4));   // D3D9: max(lit, map x emission2)
    if (NotFinite(col)) col = 0; col = max(col, 0);
    o.c = float4(ApplyHaze(col, i.wp), base.a);
    return o;
}
