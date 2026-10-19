// D3D11Client: planet surface tiles (texture from the tile or an ancestor, elevation baked into the vertices)
#include "common.hlsli"
cbuffer Tile : register(b1)
{
    float4x4 gWVP;
    float4 gSunL;       // direction to the Sun in the planet frame
    float4 gUV;         // scale u, scale v, offset u, offset v (ancestor texture)
    float4 gMisc;       // x: textured
    float4x4 gW;        // tile -> camera-centred global frame
    float4x4 gRot;      // planet frame -> global (normals)
    float4 gMic[3];     // micro levels: xyz the tile centre in repeats (fraction), w repeats per metre
    float4 gGround[3];  // ground classes: [0] the tile centre in the site grid [m] (xy), 1/extent (zw); [1] east, w: 1 = on; [2] north
    float4 gMicF;       // xyz: each level's fade-out distance [m]; w: 0 off, 1 colour detail, 2 normals (.ag) + luminance (.b)
    float4 gMicC;       // the tile centre in the planet frame [m]
    // ground relief (bodies with height layers: Titan)
    float4 gTes;        // x: pixels per radian, y: tessellated edge [px], z: displacement range [m], w: wetness (rain) 0..1
    float4 gElev;       // x: tile centre radius - R [m], y: tile centre radius [m], z: liquid level over R [m], w: 1 = liquid
    float4 gRelief;     // x: 1 = relief on this tile, y: level A in reach, z: level B in reach, w: units to flatten under
    float4 gMicI[2];    // levels A, B: the tile centre's whole repeats (mod 4096; for the per-cell randomness)
    float4 gUnits[4];   // landed units near the camera: camera-centred position, w: radius kept flat [m]
};
Texture2D tSurf : register(t0);
Texture2D tMic0 : register(t1);     // finest
Texture2D tMic1 : register(t2);
Texture2D tMic2 : register(t3);     // coarsest
Texture2D tHA : register(t4);       // relief A: r sand [-0.05, 0.05] m, g cobbles [0, 0.10] m, b gravel [0, 0.03] m
Texture2D tHB : register(t5);       // relief B: r rills [-0.8, 0.4] m, g cobble-field patches, b rill lines
Texture2D tGround : register(t6);   // ground class weights: r sand, g cobbles, b ice bedrock, a wet margin
SamplerState sAniso : register(s0);
SamplerState sClamp : register(s1);

struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD; };
struct VSOut
{
    float4 pos : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD; float3 wp : WPOS; float3 lp : LPOS; float3 up : UPDIR;
    float elev : ELEV; float3 pn : PNORMAL;
};

// height over the body's reference radius of a tile-local point (no float cancellation at a 2500 km radius)
float ElevAt(float3 p)
{
    float rc = gElev.y;
    float3 ch = normalize(gMicC.xyz);
    float a = 2 * rc * dot(ch, p) + dot(p, p);
    return gElev.x + a / (sqrt(rc * rc + a) + rc);
}

VSOut Vert(float3 p, float3 n, float2 uv, float3 lp, float elev)
{
    VSOut o;
    o.pos = mul(float4(p, 1), gWVP);
    o.n = mul(n, (float3x3)gRot);
    o.pn = n;
    o.wp = mul(float4(p, 1), gW).xyz;
    o.uv = uv * gUV.xy + gUV.zw;
    o.lp = lp;
    o.up = normalize(gMicC.xyz + lp);
    o.elev = elev;
    return o;
}

VSOut VS(VSIn v) { return Vert(v.p, v.n, v.uv, v.p, ElevAt(v.p)); }

// ---------------------------------------------------------------------------------------------- the ground relief
// Two height layers from the Huygens-site fields (25.6 m and 410 m repeats), laid on the planet-fixed projection with
// a random shift and turn per cell (no visible repeat), mixed by the terrain: cobbles where the tile is brighter than its
// surroundings, on slopes and in the cobble fields; sand elsewhere; damp ground on the shore band and in the rills.
// The same function drives the displacement (domain shader) and the shading normal (pixel shader).
uint Hash(int2 c)
{
    uint h = (uint)c.x * 73856093u ^ (uint)c.y * 19349663u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
float2 Rot(float2 v, float2 cs) { return float2(v.x * cs.x - v.y * cs.y, v.x * cs.y + v.y * cs.x); }

// the dominant planet axis of the local vertical picks the projection plane
float2 Plane(float3 c, float3 up) { float3 a = abs(up); return a.y >= a.x && a.y >= a.z ? c.zx : (a.x >= a.z ? c.yz : c.xy); }

// magnified (a texel over a pixel): cubic B-spline from 4 bilinear taps, so the slope (the shading) has no facets per
// texel; otherwise the anisotropic sample with the given gradients
float4 SampleSmooth(Texture2D t, float2 uv, float2 dx, float2 dy)
{
    const float N = 2048;
    if (max(dot(dx, dx), dot(dy, dy)) * N * N > 1) return t.SampleGrad(sAniso, uv, dx, dy);
    float2 p = uv * N - 0.5, i = floor(p), f = p - i;
    float2 f2 = f * f, f3 = f2 * f;
    float2 w0 = (1 - 3 * f + 3 * f2 - f3) / 6, w1 = (4 - 6 * f2 + 3 * f3) / 6, w2 = (1 + 3 * f + 3 * f2 - 3 * f3) / 6, w3 = f3 / 6;
    float2 g0 = w0 + w1, g1 = w2 + w3;
    float2 h0 = (i - 0.5 + w1 / g0) / N, h1 = (i + 1.5 + w3 / g1) / N;
    return g0.y * (g0.x * t.SampleLevel(sAniso, float2(h0.x, h0.y), 0) + g1.x * t.SampleLevel(sAniso, float2(h1.x, h0.y), 0)) +
           g1.y * (g0.x * t.SampleLevel(sAniso, float2(h0.x, h1.y), 0) + g1.x * t.SampleLevel(sAniso, float2(h1.x, h1.y), 0));
}

// one layer, 4 cells blended (sharp weights keep the contrast); lod < 0: pixel-shader sampling with the given gradients
float4 Stochastic(Texture2D t, float2 uv, float2 io, float2 dx, float2 dy, float lod)
{
    const float S = 2;                              // cells per repeat
    float2 g = uv * S, fl = floor(g), f = g - fl;
    int2 base = int2(fl) + int2(io * S);
    float2 sf = f * f * (3 - 2 * f);
    float4 acc = 0; float ws = 0;
    [unroll] for (int k = 0; k < 4; k++) {
        int2 o = int2(k & 1, k >> 1);
        uint h = Hash(base + o);
        float ang = (h & 1023) * (6.2831853 / 1024);
        float2 cs = float2(cos(ang), sin(ang));
        float2 off = float2((h >> 10) & 1023, (h >> 20) & 1023) / 1024;
        float w = (o.x ? sf.x : 1 - sf.x) * (o.y ? sf.y : 1 - sf.y);
        w = w * w * w * w;
        float2 t2 = Rot(uv, cs) + off;
        float4 s = lod < 0 ? SampleSmooth(t, t2, Rot(dx, cs), Rot(dy, cs)) : t.SampleLevel(sAniso, t2, lod);
        acc += s * w; ws += w;
    }
    return acc / ws;
}

struct Ground { float h; float wet; float cob; float stone; };    // stone: 0..1 on a cobble

// the relief at a point; alb: the tile's brightness against its surroundings (1 = average)
Ground Relief(float3 lp, float3 up, float elev, float slope, float alb, float fa, float fb, float lod, float3 lpdx, float3 lpdy)
{
    Ground gr; gr.h = 0; gr.wet = 0; gr.cob = 0; gr.stone = 0;
    float4 hb = float4(0.6667, 0.5, 0, 1), ha = 0;
    if (gRelief.z > 0) {
        float3 c = lp * gMic[1].w + gMic[1].xyz;
        float2 uvb = Plane(c, up);      // 410 m period: one sample, no visible repeat
        hb = lod < 0 ? SampleSmooth(tHB, uvb, Plane(lpdx, up) * gMic[1].w, Plane(lpdy, up) * gMic[1].w) : tHB.SampleLevel(sAniso, uvb, max(lod - 4, 0));
    }
    float level = gElev.w > 0 ? gElev.z : -1e9;
    float shore = gElev.w > 0 ? 1 - smoothstep(0.0, 3.0, elev - level) : 0;
    float cob, wetC = 0;
    float4 gw = 0;
    if (gGround[1].w > 0) {     // the site's ground classes (what suits and wheels stand on), 50 m cells blended
        float2 gp = gGround[0].xy + float2(dot(lp, gGround[1].xyz), dot(lp, gGround[2].xyz));
        float2 guv = float2(gp.x * gGround[0].z + 0.5, 0.5 - gp.y * gGround[0].w);
        if (all(guv > 0) && all(guv < 1)) gw = tGround.SampleLevel(sClamp, guv, 0);
    }
    if (dot(gw, 1) > 0.01) {
        // cobble beds, rubble on the ice, sparse cobbles in patches on the sand (the Huygens view); the fields' edges
        // broken up by the patch noise, not the 50 m cells
        float edge = (hb.g - 0.5) * 0.6;
        cob = saturate(gw.g * (1 + edge) + gw.b * (0.45 + edge) + gw.r * (0.45 + 0.45 * hb.g) + gw.a * 0.25);
        wetC = gw.a;
    } else {
        // no class data: the fields, brighter (icy) ground, slopes; a lag of gravel on the shore band
        cob = saturate(saturate(hb.g * 1.3 - 0.15) * 0.7 + saturate((alb - 1) * 1.5) * 0.5 + saturate((slope - 0.12) * 5) * 0.4 + shore * 0.3);
    }
    gr.cob = cob;
    if (gRelief.y > 0 && fa > 0) {
        float3 c = lp * gMic[0].w + gMic[0].xyz;
        ha = Stochastic(tHA, Plane(c, up), Plane(gMicI[0].xyz, up), Plane(lpdx, up) * gMic[0].w, Plane(lpdy, up) * gMic[0].w, lod);
        float sand = ha.r * 0.10 - 0.05;
        float th = (1 - cob) * 0.02;                                   // sparse fields keep only the big cobbles
        float cobbles = max(ha.g * 0.10 - th, 0) - 0.000565 * exp(-th / 0.0083);   // minus its mean: the detail averages 0
        float gravel = (ha.b - 0.002) * 0.03 * (0.3 + 0.7 * cob);
        gr.h += fa * (sand * (1 - 0.5 * cob) + cobbles + gravel);
        gr.stone = fa * saturate((ha.g * 0.10 - th) / 0.006);
    }
    gr.h += fb * 0.4 * (hb.r * 1.2 - 0.8 + 0.6 * (hb.b - 0.0416));
    // damp ground: the shore band, the rill beds, rain
    float shoreWet = gElev.w > 0 ? 1 - smoothstep(0.2, 2.0, elev - level) : 0;
    gr.wet = saturate(max(max(shoreWet, wetC * (0.75 + 0.25 * hb.g)), gTes.w));
    return gr;
}

// how much the ground may move here: none under the liquid or at its edge, none under landed units, fading out far away
float DispAllowed(float3 wp, float elev)
{
    float k = 1 - smoothstep(0.5 * gTes.z, gTes.z, length(wp));
    if (gElev.w > 0) k *= smoothstep(0.0, 1.0, elev - gElev.z);
    for (int i = 0; i < (int)gRelief.w; i++) {
        float d = length(wp - gUnits[i].xyz);
        k *= smoothstep(gUnits[i].w, gUnits[i].w + 1.5, d);
    }
    return k;
}

float SlopeOf(float3 pn, float3 up) { return acos(saturate(dot(normalize(pn), up))); }

// ---------------------------------------------------------------------------------------------- tessellation
struct HSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD; };
struct PatchC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };

HSIn VST(VSIn v) { HSIn o; o.p = v.p; o.n = v.n; o.uv = v.uv; return o; }

float EdgeFactor(float3 a, float3 b)
{
    float3 m = mul(float4(0.5 * (a + b), 1), gW).xyz;
    float d = max(length(m), 0.5);
    if (d > gTes.z) return 1;
    return clamp(length(b - a) * gTes.x / (d * gTes.y), 1, 16);
}

PatchC PatchConst(InputPatch<HSIn, 3> ip)
{
    PatchC o;
    o.e[0] = EdgeFactor(ip[1].p, ip[2].p);
    o.e[1] = EdgeFactor(ip[2].p, ip[0].p);
    o.e[2] = EdgeFactor(ip[0].p, ip[1].p);
    o.i = (o.e[0] + o.e[1] + o.e[2]) / 3;
    return o;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConst")]
HSIn HS(InputPatch<HSIn, 3> ip, uint id : SV_OutputControlPointID) { return ip[id]; }

[domain("tri")]
VSOut DS(PatchC pc, float3 b : SV_DomainLocation, const OutputPatch<HSIn, 3> ip)
{
    float3 p = ip[0].p * b.x + ip[1].p * b.y + ip[2].p * b.z;
    float3 n = normalize(ip[0].n * b.x + ip[1].n * b.y + ip[2].n * b.z);
    float2 uv = ip[0].uv * b.x + ip[1].uv * b.y + ip[2].uv * b.z;
    float elev = ElevAt(p);
    float3 up = normalize(gMicC.xyz + p);
    float3 wp = mul(float4(p, 1), gW).xyz;
    float k = DispAllowed(wp, elev);
    float3 q = p;
    if (k > 0) {
        float dist = max(length(wp), 0.5);
        float lod = max(log2(dist / gTes.x * 2048 * gMic[0].w), 0);    // level A texels per pixel there
        float2 tuv = uv * gUV.xy + gUV.zw;
        float3 sc = tSurf.SampleLevel(sClamp, tuv, 3).rgb, sa = tSurf.SampleLevel(sClamp, tuv, 7).rgb;
        float alb = gMisc.x > 0 ? dot(sc, 1) / max(dot(sa, 1), 1e-3) : 1;
        float fa = 1 - smoothstep(0.5 * gMicF.x, gMicF.x, dist), fb = 1 - smoothstep(0.5 * gMicF.y, gMicF.y, dist);
        Ground gr = Relief(p, up, elev, SlopeOf(n, up), alb, fa, fb, lod, 0, 0);
        q = p + up * (gr.h * k);
    }
    return Vert(q, n, uv, p, elev);
}

// the relief's shading: the normal from the height's screen derivatives (surface gradient), cobbles brighter (ice),
// damp ground darker and glossy; nothing under the liquid
void ReliefShade(VSOut i, inout float3 alb, inout float3 n, out float wet)
{
    float3 lpdx = ddx(i.lp), lpdy = ddy(i.lp);
    float dist = length(i.wp);
    float fa = 1 - smoothstep(0.5 * gMicF.x, gMicF.x, dist), fb = 1 - smoothstep(0.5 * gMicF.y, gMicF.y, dist);
    // the ground's brightness against its surroundings, both smoothed (single texels of the tile would show as squares)
    float3 sc = gMisc.x > 0 ? tSurf.SampleLevel(sClamp, i.uv, 3).rgb : 0.5, sa = gMisc.x > 0 ? tSurf.SampleLevel(sClamp, i.uv, 7).rgb : 0.5;
    float a = dot(sc, 1) / max(dot(sa, 1), 1e-3);
    Ground gr = Relief(i.lp, i.up, i.elev, SlopeOf(i.pn, i.up), a, fa, fb, -1, lpdx, lpdy);
    float under = gElev.w > 0 ? smoothstep(-0.05, 0.05, i.elev - gElev.z) : 1;
    // surface gradient bump in the planet frame
    float3 pn = normalize(i.pn);
    float3 r1 = cross(lpdy, pn), r2 = cross(pn, lpdx);
    float det = dot(lpdx, r1);
    float3 grad = sign(det) * (ddx(gr.h) * r1 + ddy(gr.h) * r2);
    float3 bn = normalize(abs(det) * pn - grad * under);
    n = normalize(mul(bn, (float3x3)gRot));
    alb *= lerp(1, 1.8, gr.stone * under);         // water-ice cobbles brighter than the organic sand (DISR)
    wet = gr.wet * under;
    alb *= lerp(1, 0.55, wet);
}

float4 PS(VSOut i) : SV_Target
{
    float3 alb = gMisc.x > 0 ? tSurf.Sample(sClamp, i.uv).rgb : float3(0.5, 0.5, 0.5);
    float3 n = normalize(i.n);
    float wet = 0;
    if (gRelief.x > 0) ReliefShade(i, alb, n, wet);
    float nl = saturate(dot(n, gSunDir.xyz)) * ShadowAt(i.wp);
    float3 col = alb * (nl * SunLight(i.wp) + SkyLight(n, i.wp));
    if (wet > 0) {      // damp ground: a sheen of the Sun and the sky
        float3 v = -normalize(i.wp);
        float3 h = normalize(v + gSunDir.xyz);
        float fr = 0.02 + 0.98 * pow(1 - saturate(dot(n, v)), 5);
        col += wet * (0.6 * pow(saturate(dot(n, h)), 60) * nl * SunLight(i.wp) + fr * 0.5 * SkyLight(reflect(-v, n), i.wp));
    }
    if (gSky0.w < 0.5 && !FxActive()) col += alb * 0.05 * smoothstep(-0.05, 0.1, dot(n, gSunDir.xyz));   // seen from space: a soft terminator
    return float4(ApplyHaze(col, i.wp), 1);
}
