// D3D11Client: Earth's volumetric clouds near the planet (EarthClouds.cpp) - drawn instead of the 2D layer inside the air
// (CloudLayer.cpp's switch), from the same weather: the layer's coverage (Cloud.tree, its rotation) everywhere, the
// cyclone's map (Cyclone.cpp: coverage, convective fraction, top, precipitation) over the storm.
// Per column the weather gives the cloud type and its vertical extent:
//   stratiform  (St, Sc, Ns, As): flat tops; nimbostratus based low where it rains; extinction ~0.02-0.04 /m
//               (inside: 50-150 m visibility - LWC 0.1-0.5 g/m3, r_e 6-10 um)
//   ice (Cs, Ci): high (the top above ~6 km, no rain), thin: ~1e-3 /m
//   convective (Cu, Cb): flat bases ~1 km, towers to the map's top, an anvil spreading under the tropopause; ~0.05 /m
//               (LWC 0.5-1 g/m3, r_e ~10 um: 20-50 m)
//   rain below the base where it falls: grey shafts (~2e-4 .. 2e-3 /m by intensity: a few km visibility in heavy rain)
// Shape: Perlin-Worley noise eroded by Worley (Schneider's Nubis), coverage remapped. Light: the Sun through the air
// (FX atmosphere) and through the cloud (a short march, the multiple-scattering octaves with a flattened phase), the
// sky's light at the point (darker towards the base, shut out under thick cloud), the air in front. Half resolution,
// marched through the scene's depth, composited like the Titan clouds.
#include "common.hlsli"
#include "fx_atmo_sky.hlsli"

cbuffer EarthClouds : register(b1)
{
    float4x4 cInvVP;
    float4 cRot0, cRot1, cRot2;   // global -> planet frame (rows); w: the camera in the planet frame / 1000 (km)
    float4 cL;      // x: planet radius [m], y: cos, z: sin of the layer's rotation, w: the coverage texture's width
    float4 cCy;     // the cyclone: lng, lat [rad], half-size [m] (0: none), -
    float4 cP;      // x: time [s], y: steps, z: frame, w: fade (0..1: the volume's share against the 2D layer)
    float4 cSize;   // half-res target w, h, debug, -
};
Texture3D tShape : register(t0);         // r Perlin-Worley, g/b/a Worley octaves (64^3)
Texture2D<float> tDepth : register(t1);
Texture2D<float> tCov : register(t2);    // the layer's coverage (equirectangular)
Texture2D tCyc : register(t3);           // the cyclone's map
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
float HGc(float g, float c) { float t = 1 + g * g - 2 * g * c; return (1 - g * g) / (t * sqrt(t)); }

static const float H_MIN = 300, H_MAX = 16500;   // the cloud shells above the radius [m]

struct Wx { float cov, conv, top, pr, base, ext, ice; };

// the weather in the column over a planet-frame point
Wx Weather(float3 P)
{
    Wx w;
    float3 n = normalize(P);
    // Orbiter's layer: its coverage (the layer turned by its rotation) - mostly low and middle cloud, its type unknown:
    // a mix, the tops growing with the coverage
    float3 v = float3(n.x * cL.y - n.z * cL.z, n.y, n.x * cL.z + n.z * cL.y);
    float2 uv = float2(atan2(v.z, v.x) / (2 * FX_PI) + 0.5, 0.5 - asin(clamp(v.y, -1, 1)) / FX_PI);
    float c = tCov.SampleLevel(sWrap, uv, 1);
    w.cov = c; w.conv = 0.45; w.top = lerp(1.6, 4.5, saturate(c * c)) * 1000; w.pr = 0;
    if (cCy.z > 0) {
        float lng = atan2(n.z, n.x), lat = asin(clamp(n.y, -1, 1));
        float dl = lng - cCy.x; dl -= 2 * FX_PI * round(dl / (2 * FX_PI));
        float2 q = float2(dl * cos(cCy.y), lat - cCy.y) * cL.x / cCy.z;
        if (all(abs(q) < 1)) {
            float4 m = tCyc.SampleLevel(sClampC, q * float2(0.5, -0.5) + 0.5, 0);
            float e = saturate((1.35 - length(q) * 1.5) / 0.45);
            w.cov = lerp(w.cov, m.r, e); w.conv = lerp(w.conv, m.g, e); w.top = lerp(w.top, m.b * 16000, e); w.pr = m.a * 30 * e;
        }
    }
    // the type: ice where the stratiform top is high and nothing falls (cirrostratus/cirrus ahead of the warm front)
    w.ice = saturate((w.top - 6500) / 2500) * (1 - w.conv) * saturate(1 - w.pr / 1.5);
    float baseStrat = lerp(1200, 500, saturate(w.pr / 4));           // nimbostratus low where it rains
    w.base = lerp(lerp(baseStrat, 1000, w.conv), w.top - 2200, w.ice);
    w.top = max(w.top, w.base + 400);
    w.ext = lerp(lerp(0.025, 0.05, w.conv), 0.0012, w.ice);
    return w;
}

// the cloud's density [1/m] at a planet-frame point (h: above the radius)
float Density(Wx w, float3 P, float h, float lod, bool detail)
{
    if (w.cov < 0.02 || h <= w.base || h >= w.top) return 0;
    float hf = (h - w.base) / (w.top - w.base);
    // the vertical profile: stratiform flat, densest low (Ns -> As); convective towers, an anvil at the top of a Cb
    float strat = smoothstep(0, 0.08, hf) * smoothstep(1, 0.8, hf) * lerp(1, 0.35, hf * (1 - w.ice));
    float conv = saturate(hf / 0.07) * saturate((1 - hf) / 0.35);
    float anvil = w.top > 9000 ? smoothstep(0.78, 0.9, hf) * smoothstep(1, 0.95, hf) : 0;
    float prof = lerp(strat, max(conv, anvil * 1.2), w.conv);
    // the shape: Perlin-Worley at ~6 km cells, drifting; stratiform flattened (stretched horizontally)
    float3 pk = P / 1000.0;                                          // km
    pk.x += cP.x * 0.008;                                            // a drift of ~8 m/s (the steering flow)
    float flat = lerp(3.0, 1.0, w.conv);
    float3 up = normalize(P);
    float3 ps = pk / 6.0;
    ps += up * dot(ps, up) * (flat - 1);                             // vertical frequency up: thinner sheets
    float4 s = tShape.SampleLevel(sWrap, ps, lod);
    float fbmW = s.g * 0.625 + s.b * 0.25 + s.a * 0.125;
    float sr = saturate((s.r - 0.5) * 2.2 + 0.5);
    float shape = saturate(Remap(sr, (1 - fbmW) * 0.4, 1, 0, 1)) * prof;
    float cov = saturate(w.cov * 1.1);
    float d = saturate(Remap(shape, 1 - cov, 1, 0, 1) * 1.6);
    if (d <= 0) return 0;
    if (detail) {
        float4 s2 = tShape.SampleLevel(sWrap, pk / 1.1 + 0.37, 0);
        float det = s2.g * 0.625 + s2.b * 0.25 + s2.a * 0.125;
        det = lerp(det, 1 - det, saturate(hf * 3));                  // wispy at the base, billowy above
        d = saturate(Remap(d, det * lerp(0.35, 0.25, w.conv), 1, 0, 1));
    }
    return d * w.ext;
}

// the rain under the base [1/m]: shafts by the intensity, streaked
float RainDensity(Wx w, float3 P, float h)
{
    if (w.pr < 0.1 || h >= w.base) return 0;
    float3 pk = P / 1000.0;
    float sh = tShape.SampleLevel(sWrap, pk / 3.0 + float3(0.1, 0, 0.3), 2).r;
    float shafts = saturate((sh - 0.35) * 2.5);
    return 2.2e-4 * pow(w.pr, 0.65) * shafts * saturate(h / 300);
}

struct MOut { float4 c : SV_Target0; float t : SV_Target1; };

MOut PSMarch(VSOut i)
{
    MOut o; o.c = float4(0, 0, 0, 1); o.t = 1e9;
    float4 cc = float4(i.uv.x * 2 - 1, 1 - i.uv.y * 2, 1, 1);
    float4 wn = mul(cc, cInvVP);
    float3 d = normalize(wn.xyz / wn.w);
    float dw, dh; tDepth.GetDimensions(dw, dh);
    int2 px = int2(i.uv * float2(dw, dh));
    float zmax = max(max(tDepth.Load(int3(px, 0)), tDepth.Load(int3(px + int2(1, 0), 0))), max(tDepth.Load(int3(px + int2(0, 1), 0)), tDepth.Load(int3(px + 1, 0))));
    float sceneT = 1e30;
    if (zmax > 1e-9) { float4 w = mul(float4(cc.xy, zmax, 1), cInvVP); sceneT = length(w.xyz / w.w); }
    float3 org = -mCentre.xyz;
    float R = cL.x;
    // the span through the shells [R + H_MIN, R + H_MAX]
    float b = dot(org, d), c0 = dot(org, org), ro = sqrt(c0);
    float rT = R + H_MAX, rB = R;
    float dT = b * b - (c0 - rT * rT);
    if (dT < 0) return o;
    float tT0 = -b - sqrt(dT), tT1 = -b + sqrt(dT);
    float dB = b * b - (c0 - rB * rB);
    float tGround = (dB > 0 && -b - sqrt(dB) > 0) ? -b - sqrt(dB) : 1e30;
    float t0 = max(ro > rT ? tT0 : 0, 0), t1 = min(min(tT1, tGround), sceneT);
    t1 = min(t1, t0 + (ro > rT ? 600000 : 220000));                   // farther the haze has them (from space: the limb's long paths)
    if (t1 <= t0) return o;
    float ign = frac(52.9829189 * frac(dot(i.pos.xy + cP.z * float2(5.588, 7.123), float2(0.06711056, 0.00583715))));
    float cs = dot(d, gSunDir.xyz);
    float ph = lerp(HGc(0.85, cs), HGc(-0.2, cs), 0.25);             // water drops: the forward peak, some back-scatter
    float3 sd = gSunDir.xyz;
    float3 Lsum = 0; float T = 1, tw = 0, twS = 0;
    float t = t0 + ign * 60;
    int N = (int)cP.y;
    float3 camPl = float3(cRot0.w, cRot1.w, cRot2.w) * 1000.0;
    [loop] for (int k = 0; k < N && t < t1; k++) {
        float3 p = d * t;                          // camera-centred
        float3 P = ToPlanet(p) + camPl;
        float r = length(P), h = r - R;
        Wx w = Weather(P);
        // the step: short in cloud and near the camera, long in the empty columns and far away
        float near = clamp(t * 0.012, 40, 900);
        bool empty = w.cov < 0.02 && w.pr < 0.1;
        float dt = max(empty ? near * 4 : near, (t1 - t0) / N);     // (the whole span within the steps: the limb's long paths)
        dt = min(dt, t1 - t);
        float lod = saturate(t / 60000.0) * 2;
        float sig = Density(w, P, h, lod, t < 40000);
        float sigR = RainDensity(w, P, h);
        float sgt = sig + sigR;
        if (sgt > 1e-6) {
            float3 upG = normalize(p - mCentre.xyz);
            // towards the Sun through the cloud: 4 steps, doubling
            float tauS = 0, ls = 120;
            float3 sdP = ToPlanet(sd);
            [unroll] for (int j = 0; j < 4; j++) {
                float3 Pj = P + sdP * (ls * (j + 0.5));
                tauS += Density(Weather(Pj), Pj, length(Pj) - R, 1.5, false) * ls; ls *= 2.2;
            }
            float msS = ph * exp(-tauS) + lerp(ph, 1, 0.7) * 0.5 * exp(-tauS * 0.25) + 0.25 * exp(-tauS * 0.0625);
            float3 sun = SunLight(p) * saturate(dot(upG, sd) * 6 + 0.3);
            float hf = saturate((h - w.base) / max(w.top - w.base, 1));
            float3 sky = SkyLight(upG, p);                                         // the sky's light there
            float occl = lerp(0.35, 1.0, hf) * lerp(1.0, 0.55, saturate(w.cov * w.pr / 4));   // darker low, darker under rain
            float3 S = sun * msS + sky * occl;
            float3 Sr = sky * 0.6 + sun * 0.15;                                     // rain: grey, mostly the sky's light
            float3 Sm = (S * sig + Sr * sigR) / sgt;
            float a = exp(-sgt * dt);
            Lsum += T * Sm * (1 - a);
            tw += T * (1 - a) * t; twS += T * (1 - a);
            T *= a;
            if (T < 0.01) break;
        }
        t += dt;
    }
    if (T > 0.999) return o;
    float tc = twS > 0 ? tw / twS : t0;
    float3 La = 0, Ta = 1;
    if (FxActive()) {
        float3 fw; float tc0 = -dot(org, d); float3 cx = cross(org, d); float K = dot(cx, cx) - mCentre.w * mCentre.w;
        float bt = dot(org, d), dTopA = bt * bt - (dot(org, org) - mGeo.x * mGeo.x);
        float tIn = dTopA > 0 ? max(0, -bt - sqrt(dTopA)) : 0;
        if (tc > tIn) FxMarch(org, d, tc0, K, tIn, tc, 12, La, Ta, fw);
        La *= FX_PI;
    }
    float op = (1 - T) * cP.w;
    o.c = float4(Ta * Lsum * cP.w + La * op, 1 - op);
    o.t = tc;
    return o;
}

Texture2D tCloud : register(t4);
Texture2D<float> tCloudT : register(t5);
float4 PSComposite(VSOut i) : SV_Target
{
    float dw, dh; tDepth.GetDimensions(dw, dh);
    float z = tDepth.Load(int3(int2(i.pos.xy), 0));
    float2 uv = i.pos.xy / float2(dw, dh);
    float2 h = 1.0 / float2(dw, dh) * 2.0;
    float4 c = tCloud.SampleLevel(sClampC, uv, 0) * 0.4
             + (tCloud.SampleLevel(sClampC, uv + h * float2(1, 0.5), 0) + tCloud.SampleLevel(sClampC, uv + h * float2(-1, -0.5), 0)
              + tCloud.SampleLevel(sClampC, uv + h * float2(-0.5, 1), 0) + tCloud.SampleLevel(sClampC, uv + h * float2(0.5, -1), 0)) * 0.15;
    if (cSize.z > 0.5) return float4((1 - c.a).xxx / max(gHazeRGB.w * gAdapt.rgb, 1e-4), 0);
    if (c.a > 0.999) discard;
    float tcl = tCloudT.SampleLevel(sClampC, uv, 0);
    if (z > 1e-9) {
        float4 w = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, z, 1), cInvVP);
        if (length(w.xyz / w.w) < tcl * 0.9) discard;
    }
    return c;
}
