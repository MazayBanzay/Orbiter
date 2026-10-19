// D3D11Client: volumetric clouds (Clouds.cpp). Layers on spherical shells (Config\D3D11Client\Clouds.cfg), marched per
// pixel at half resolution through the scene's depth:
//  - shape: a weather field (cover, by a fBm on the sphere in the planet's own frame, drifting with the layer's wind) and
//    3D Perlin-Worley noise eroding it; the layer's vertical profile (a flat-based cumulus tower or a stratiform deck);
//  - light: the Sun through the air above (the FX atmosphere's transmittance) and through the cloud itself (a short march
//    towards the Sun, with the multiple-scattering octaves of Wrenninge 2013 / Hillaire 2016), the dual-lobe phase of
//    liquid drops; the sky's light round it (the FX atmosphere's harmonics), brighter on top;
//  - the air between the eye and the cloud: the FX atmosphere's own march, so a far deck sinks into the haze.
// Output (premultiplied): rgb = T_air * L_cloud + L_air * (1 - T_cloud), a = T_cloud: blended over the scene as
// dest * a + rgb - the scene behind dimmed through the cloud, the air in front kept.
#include "common.hlsli"
#include "fx_atmo_sky.hlsli"

#define NL 4
cbuffer Clouds : register(b1)
{
    float4x4 cInvVP;        // clip -> camera-centred global
    float4 cRot0, cRot1, cRot2;   // global -> planet frame (rows); w: the camera in the planet frame / 1000 (km)
    float4 cLay[NL * 3];    // per layer: (base r, top r, cover, extinction) (g, scale [m], wind [m/s], kind 0 deck 1 cumulus)
                            //            (wind direction in the planet frame, 0)
    float4 cP;              // x layers, y time [s], z frame, w steps
    float4 cSize;           // the half-res target's size, 1 / size
};
Texture3D tShape : register(t0);         // r: Perlin-Worley, g/b/a: Worley octaves (64^3)
Texture2D<float> tDepth : register(t1);  // the scene's depth (resolved), full resolution
SamplerState sWrap : register(s0);
SamplerState sClampC : register(s1);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint vid : SV_VertexID)
{
    VSOut o;
    float2 c = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(c * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = c;
    return o;
}

float3 ToPlanet(float3 v) { return float3(dot(cRot0.xyz, v), dot(cRot1.xyz, v), dot(cRot2.xyz, v)); }
float Remap(float x, float a, float b, float c, float d) { return c + (x - a) * (d - c) / max(b - a, 1e-5); }
float HGc(float g, float c) { float t = 1 + g * g - 2 * g * c; return (1 - g * g) / (t * sqrt(t)); }   // relative to isotropic

// the layer's density [1/m] at p (camera-centred global), its height fraction out
float Density(int l, float3 p, float r, float lod)
{
    float4 a = cLay[l * 3], b = cLay[l * 3 + 1], w = cLay[l * 3 + 2];
    float hf = (r - a.x) / (a.y - a.x);
    if (hf <= 0 || hf >= 1) return 0;
    // Titan's clouds at our site's latitude and season (Cassini ISS, PIA21450: 7 May 2017, 57 N): bright, feathery bands
    // drawn out east-west by the zonal wind - not cumulus heaps. So the noise is laid out along the latitude circles,
    // in (east, north, up) metres, the east stretched 6 times; the bands drift east with the wind.
    float3 P = ToPlanet(p) + float3(cRot0.w, cRot1.w, cRot2.w) * 1000.0;   // the planet frame (y: the spin axis)
    float rP = length(P);
    float lat = asin(clamp(P.y / rP, -1, 1)), lon = atan2(P.z, P.x);
    float east = lon * a.x * cos(lat) - b.z * cP.y;
    float3 pp = float3(east / 6.0, lat * a.x, (rP - a.x) * 2.0) / b.y;
    // the weather: a low-frequency fBm on the sphere - where the layer is, and how high its cells reach
    float wf = tShape.SampleLevel(sWrap, pp * 0.11, 0).r * 0.6 + tShape.SampleLevel(sWrap, pp * 0.27 + 0.31, 0).r * 0.3
             + tShape.SampleLevel(sWrap, pp * 0.71 + 0.73, 0).r * 0.1;
    // the noise sits at 0.5 +- ~0.15 (fBm, trilinear): stretched to ~0..1 first, or a "cover" of 0.45 left only its rare
    // peaks - an empty sky
    wf = saturate((wf - 0.5) * 3.0 + 0.5);
    // the coverage here: the layer's mean cover, more where the weather field is high
    float cover = saturate(a.z * 2.0 * wf);
    if (cover <= 0.01) return 0;
    float prof;
    if (b.w > 0.5) {      // the convective bands: a lens, thicker where the cell is strong
        float topH = 0.4 + 0.6 * cover;
        prof = smoothstep(0, 0.15, hf) * saturate((topH - hf) / (0.5 * topH));
    } else {              // a stratiform deck: soft at both ends
        prof = smoothstep(0, 0.2, hf) * smoothstep(1, 0.55, hf);
    }
    if (prof <= 0) return 0;
    // the shape (Nubis, Schneider 2015): Perlin-Worley eroded by the Worley fBm, shaped by the height profile, then cut
    // by the coverage: (shape - (1 - cover)) / cover, times the cover - sparse cells where the cover is low
    // the base shape at twice the feature size: whole cells (Titan's convective clouds: masses of tens of km), not spots
    float4 s = tShape.SampleLevel(sWrap, pp * 0.5, lod);
    float fbmW = s.g * 0.625 + s.b * 0.25 + s.a * 0.125;
    float sr = saturate((s.r - 0.5) * 2.5 + 0.5);                  // stretched as the weather: ~uniform 0..1
    float shape = saturate(Remap(sr, (1 - fbmW) * 0.35, 1, 0, 1)) * prof;
    // a fraction ~cover of the volume over the threshold, denser towards the cells' cores
    float d = saturate(Remap(shape, 1 - cover, 1, 0, 1) * 1.5);
    if (d <= 0) return 0;
    if (lod < 0.5) {
        // feathery: the fine Worley stretched along the band too, eating the edges into streaks
        float4 s2 = tShape.SampleLevel(sWrap, pp * float3(1.5, 4.0, 4.0) + 0.17, 0);
        float det = s2.g * 0.625 + s2.b * 0.25 + s2.a * 0.125;
        d = saturate(Remap(d, det * 0.45, 1, 0, 1));
    }
    return d * a.w;
}

float DensityAll(float3 p, float lod)
{
    float r = length(p - mCentre.xyz);
    float s = 0;
    [unroll] for (int l = 0; l < NL; l++) if (l < (int)cP.x) s += Density(l, p, r, lod);
    return s;
}

// the shells' span along the ray: [t0, t1] (both 0: none)
void Shells(float3 org, float3 d, out float t0, out float t1)
{
    float rmin = 1e30, rmax = 0;
    [unroll] for (int l = 0; l < NL; l++) if (l < (int)cP.x) { rmin = min(rmin, cLay[l * 3].x); rmax = max(rmax, cLay[l * 3].y); }
    float b = dot(org, d), c0 = dot(org, org);
    float ro = sqrt(c0);
    float dTop = b * b - (c0 - rmax * rmax);
    t0 = 0; t1 = 0;
    if (dTop < 0) return;
    float sTop = sqrt(dTop);
    float tTopIn = -b - sTop, tTopOut = -b + sTop;
    float dBot = b * b - (c0 - rmin * rmin);
    if (ro < rmin) {                       // under the layers: from the bottom shell out to the top one
        t0 = -b + sqrt(max(dBot, 0)); t1 = tTopOut;
    } else if (ro < rmax) {                // inside: to wherever it leaves (down into the bottom shell, or out at the top)
        t0 = 0;
        t1 = (dBot > 0 && -b - sqrt(dBot) > 0) ? -b - sqrt(dBot) : tTopOut;
    } else {                               // above: in at the top, out at the bottom or the top
        t0 = max(tTopIn, 0);
        t1 = (dBot > 0 && -b - sqrt(dBot) > 0) ? -b - sqrt(dBot) : tTopOut;
    }
    t1 = min(t1, t0 + 150000);             // farther the haze hides them anyway; 300 km in 40 steps left 7.5 km blots
}

struct MOut { float4 c : SV_Target0; float t : SV_Target1; };

MOut PSMarch(VSOut i)
{
    MOut o; o.c = float4(0, 0, 0, 1); o.t = 1e9;
    float2 uv = i.uv;
    float4 cc = float4(uv.x * 2 - 1, 1 - uv.y * 2, 1, 1);
    float4 wn = mul(cc, cInvVP);
    float3 d = normalize(wn.xyz / wn.w);
    // the scene's distance here (the nearest of the four full-res pixels under this half-res one: no cloud over a hill)
    float dw, dh; tDepth.GetDimensions(dw, dh);
    int2 px = int2(uv * float2(dw, dh));
    float zmax = max(max(tDepth.Load(int3(px, 0)), tDepth.Load(int3(px + int2(1, 0), 0))), max(tDepth.Load(int3(px + int2(0, 1), 0)), tDepth.Load(int3(px + 1, 0))));
    float sceneT = 1e30;
    if (zmax > 1e-9) { float4 w = mul(float4(cc.xy, zmax, 1), cInvVP); sceneT = length(w.xyz / w.w); }
    float3 org = -mCentre.xyz;             // the camera from the planet's centre
    float t0, t1; Shells(org, d, t0, t1);
    t1 = min(t1, sceneT);
    if (t1 <= t0) return o;
    int N = (int)cP.w;
    float dt = (t1 - t0) / N;
    float ign = frac(52.9829189 * frac(dot(i.pos.xy + cP.z * float2(5.588, 7.123), float2(0.06711056, 0.00583715))));
    float3 L = 0; float T = 1;
    float tw = 0, twSum = 0;
    float cs = dot(d, gSunDir.xyz);
    float ph = lerp(HGc(0.8, cs), HGc(-0.25, cs), 0.2);           // liquid drops: forward peak, a little back-scatter
    float3 sunDirW = gSunDir.xyz;
    [loop] for (int k = 0; k < N; k++) {
        float t = t0 + (k + ign) * dt;
        float3 p = d * t;
        float lod = saturate(t / 60000.0) * 2;
        float sig = DensityAll(p, lod);
        if (sig <= 0) continue;
        // the light towards the Sun through the cloud: 6 steps, growing
        float tauS = 0, ls = 150;
        [unroll] for (int j = 0; j < 4; j++) { tauS += DensityAll(p + sunDirW * (ls * (j + 0.5)), 1.5) * ls; ls *= 2.0; }
        float3 sun = FxActive() ? FxSunLight(p) : SunLight(p);
        // multiple-scattering octaves: each a weaker extinction and phase (Wrenninge 2013)
        // multiple scattering: the higher octaves' phase flattened towards isotropic (light scattered many times has
        // forgotten its direction) - with the drops' forward peak on every octave a sunlit top seen from above came out
        // as dark as the ground, while Titan's clouds are the brightest things on Cassini's images
        float msS = ph * exp(-tauS) + lerp(ph, 1, 0.7) * 0.5 * exp(-tauS * 0.25) + 0.25 * exp(-tauS * 0.0625);
        float3 up = normalize(p - mCentre.xyz);
        float r = length(p - mCentre.xyz);
        // the sky's light at the cloud's own height (the irradiance table there, not the camera's harmonics): the sky above
        // on its top, the ground's bounce below
        float3 amb = FxActive() ? FxSkyLight(up, p) * 0.75 + FxSkyLight(-up, p) * 0.25 : SkyLight(up, p);
        // the sky's light is shut out by the cloud above the point (on Titan nearly all the light is the sky's: this is
        // what makes a cloud's base grey and its top bright, the cloud seen at all against the haze)
        float tauU = DensityAll(p + up * 250, 1.5) * 250 + DensityAll(p + up * 900, 1.5) * 650 + DensityAll(p + up * 2200, 1.5) * 1300;
        float ambOcc = exp(-tauU * 0.7) * 0.85 + 0.15;
        float3 S = sun * msS + amb * ambOcc;
        float a = exp(-sig * dt);
        L += T * S * (1 - a);              // single scattering albedo ~1 (liquid methane, visible)
        tw += T * (1 - a) * t; twSum += T * (1 - a);
        T *= a;
        if (T < 0.01) break;
    }
    if (T > 0.999) return o;
    float tc = twSum > 0 ? tw / twSum : t0;
    // the air in front of the cloud (the FX atmosphere's own march over [0, tc])
    float3 La = 0, Ta = 1;
    if (FxActive()) {
        float3 fw; float tc0 = -dot(org, d); float3 cx = cross(org, d); float K = dot(cx, cx) - mCentre.w * mCentre.w;
        // only the stretch inside the air (from space most of the way is empty: 8 steps there missed the haze above
        // the cloud, and the deck replaced Titan's glowing haze with its own dim light - a black disc)
        float bt = dot(org, d), dTopA = bt * bt - (dot(org, org) - mGeo.x * mGeo.x);
        float tIn = dTopA > 0 ? max(0, -bt - sqrt(dTopA)) : 0;
        if (tc > tIn) FxMarch(org, d, tc0, K, tIn, tc, 16, La, Ta, fw);
        La *= FX_PI;
    }
    o.c = float4(Ta * L + La * (1 - T), T);
    o.t = tc;
    return o;
}

// the half-res clouds over the full-res scene: dropped where the scene is nearer than the cloud (a hill, a hull)
Texture2D tCloud : register(t2);
Texture2D<float> tCloudT : register(t3);
float4 PSComposite(VSOut i) : SV_Target
{
    float dw, dh; tDepth.GetDimensions(dw, dh);
    float z = tDepth.Load(int3(int2(i.pos.xy), 0));
    float2 uv = i.pos.xy / float2(dw, dh);
    // a 5-tap tent over the half-res march: the per-pixel dither of the steps averaged out (no grain on the cells' edges)
    float2 h = 1.0 / float2(dw, dh) * 2.0;
    float4 c = tCloud.SampleLevel(sClampC, uv, 0) * 0.4
             + (tCloud.SampleLevel(sClampC, uv + h * float2(1, 0.5), 0) + tCloud.SampleLevel(sClampC, uv + h * float2(-1, -0.5), 0)
              + tCloud.SampleLevel(sClampC, uv + h * float2(-0.5, 1), 0) + tCloud.SampleLevel(sClampC, uv + h * float2(0.5, -1), 0)) * 0.15;
    if (cSize.z > 0.5) return float4((1 - c.a).xxx / max(gHazeRGB.w * gAdapt.rgb, 1e-4), 0);   // CloudDbg: opacity in white
    if (c.a > 0.999) discard;
    float tcl = tCloudT.SampleLevel(sClampC, uv, 0);
    if (z > 1e-9) {
        float4 w = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, z, 1), cInvVP);
        if (length(w.xyz / w.w) < tcl * 0.9) discard;
    }
    return c;
}
