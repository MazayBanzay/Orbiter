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
    float4 gMicM[3];    // the levels' mean texel: the detail is relative to it, the average colour stays
    float4 gMicF;       // xyz: each level's fade-out distance [m]; w: 0 off, 1 colour detail, 2 normals (.ag) + luminance (.b)
    float4 gMicC;       // the tile centre in the planet frame [m]
    float4 gGm;         // ground material: xyz the tile centre in 10.24 m repeats (fraction), w repeats per metre (0 = off)
    float4 gEl;         // x: tile centre radius - R, y: its radius, z: liquid level over R, w: 1 = liquid
    float4 gGround[3];  // ground classes: [0] the tile centre in the site grid [m], 1/extent; [1] east, w 1 = on; [2] north
    float4 gMaskUV;     // the liquid mask (own or an ancestor's): scale u, v, offset u, v; on when gMisc.z > 0
    float4 gShore;      // the coast (with the mask's distance field): x its texel [m] (0: none), y the beach's slope,
                        // z the skirts' drop [m]
    float4 gShore2;     // y: without a field, the mask round the tile all land (+1e3) or all liquid (-1e3), 0 none; x, z, w: 0
};
Texture2D tSurf : register(t0);
Texture2D tMic0 : register(t1);     // finest
Texture2D tMic1 : register(t2);
Texture2D tMic2 : register(t3);     // coarsest
Texture2D tGc : register(t4);       // ground material, cobble field: r height [-0.02, 0.10] m, g b normal, a occlusion
Texture2D tGs : register(t5);       // ground material, sand
Texture2D tMask : register(t15);     // liquid mask: alpha 0 = liquid
Texture2D tGround : register(t6);   // ground class weights: r sand, g cobbles, b ice bedrock, a wet margin
Texture2D<float> tSdf : register(t16);   // the coast's signed distance in mask texels (+ land), Planet::CoastField
SamplerState sAniso : register(s0);
SamplerState sClamp : register(s1);

struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD; };
struct VSOut { float4 pos : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD; float3 wp : WPOS; float3 lp : LPOS; float3 up : UPDIR; float elev : ELEV; };

// height over the reference radius of a tile-local point (no float cancellation at a 2500 km radius)
float ElevAt(float3 p)
{
    float rc = gEl.y;
    float3 ch = normalize(gMicC.xyz);
    float a = 2 * rc * dot(ch, p) + dot(p, p);
    return gEl.x + a / (sqrt(rc * rc + a) + rc);
}

// ---- the coast. The elevation steps 0.1 m on a grid of metres, the liquid's level lies between two steps: the terrain
// would cross it along its own cells, and the shallows (seen through the liquid) would trace those cells too; the masks
// themselves draw the coast in steps of a few metres. So near the coast the ground is laid as a beach of the masks'
// distance field, smoothed over ~4 m (Planet::CoastField): rising from the coast on land (or the mesh where it stands
// higher), sinking from it under the liquid, the mesh again some metres off. The drawn coast is the field's zero along
// the triangles - a smooth curve - and no depth is written per pixel (that would cost the early depth test and the
// depth buffer's compression for the tile and the passes after it).
float CoastDist(float2 tuv)         // [m] from the coast at a tile uv, + on land (bilinear by hand: the VS has no sampler)
{
    float w, h; tSdf.GetDimensions(w, h);
    float2 p = (tuv * gMaskUV.xy + gMaskUV.zw) * w - 0.5, i0 = floor(p), f = p - i0;
    int2 a = clamp(int2(i0), 0, int(w) - 1), b = clamp(int2(i0) + 1, 0, int(w) - 1);
    float s = lerp(lerp(tSdf.Load(int3(a.x, a.y, 0)), tSdf.Load(int3(b.x, a.y, 0)), f.x),
                   lerp(tSdf.Load(int3(a.x, b.y, 0)), tSdf.Load(int3(b.x, b.y, 0)), f.x), f.y);
    return s * gShore.x;
}
// the bed under the liquid s (< 0) from the coast: 12 cm a metre down to 12 m out (the masks' fields reach ~16 m;
// past them, and in tiles all liquid, it stays there - the same at every tile's edge)
float CoastBed(float s) { return -0.02 - 0.12 * min(-s, 12); }
float CoastElev(float d, float s)   // the drawn height over the level for the mesh's d, s from the coast
{
    // within 1 m of the coast the beach alone (4 cm a metre), through zero at the field's zero: the drawn coast is that
    // smooth curve (linear in s across each triangle); the mesh again by 4 m - land: as it is, never under the level
    // (no pools where the mask has dry ground); liquid: the bed or the mesh where deeper, never over the level (no islets
    // of the mesh's steps where the mask has liquid). The mesh moves 1.5 m at most (a deeper hollow or a higher rock is
    // the data's own)
    float p = gShore.y * s;
    if (s >= 0) return lerp(p, min(max(d, 0.02), d + 1.5), smoothstep(1, 4, s));    // land: the mesh itself from 4 m in
    return lerp(p, max(min(d, CoastBed(s)), d - 1.5), smoothstep(1, 4, -s));
}

VSOut VS(VSIn v, uint vid : SV_VertexID)
{
    VSOut o;
    float3 p = v.p;
    float3 up = normalize(gMicC.xyz + v.p);
    float e = ElevAt(v.p);
    if (gShore.x > 0 || gShore2.y != 0) {               // a field, or a mask all land (+) or all liquid (-) round the tile
        float d = e - gEl.z;
        const bool skirt = vid >= 65 * 65;              // the skirts hang the same way under their edge
        if (skirt) d += gShore.z;
        float dd = CoastElev(d, gShore.x > 0 ? CoastDist(v.uv) : gShore2.y) - d;
        p += up * dd; e += dd;
    }
    o.pos = mul(float4(p, 1), gWVP);
    o.n = mul(v.n, (float3x3)gRot);
    o.wp = mul(float4(p, 1), gW).xyz;
    o.uv = v.uv * gUV.xy + gUV.zw;
    o.lp = p;
    o.up = up;
    o.elev = e;
    return o;
}

// ---- micro textures: planet-fixed triplanar projection (the planet's axes, weighted by the local vertical), each level
// tiled every 1/gMic.w metres. The tile-local position plus the tile centre's fraction keeps the float values small,
// so the pattern neither swims nor jitters, and it continues across tiles of any level.
// One texel's detail: colour relative to the mean (dc), and the normal's tilt in the planet frame (dn)
void MicroAcc(float4 s, float4 m, float k, float3 tu, float3 tv, inout float3 dc, inout float3 dn)
{
    if (gMicF.w > 1.5) {
        dc += k * (s.b / max(m.b, 0.02) - 1);
        float2 d = (s.ag - m.ag) * 2;               // .a: along the texture's u, .g: up the image (-v)
        dn += k * (tu * d.x + tv * d.y);
    } else dc += k * (s.rgb / max(m.rgb, 0.02) - 1);
}

void MicroLevel(Texture2D t, float3 c, float3 cx, float3 cy, float4 m, float f, float3 w, inout float3 dc, inout float3 dn)
{
    if (w.x > 0) MicroAcc(t.SampleGrad(sAniso, c.yz, cx.yz, cy.yz), m, f * w.x, float3(0, 1, 0), float3(0, 0, -1), dc, dn);
    if (w.y > 0) MicroAcc(t.SampleGrad(sAniso, c.zx, cx.zx, cy.zx), m, f * w.y, float3(0, 0, 1), float3(-1, 0, 0), dc, dn);
    if (w.z > 0) MicroAcc(t.SampleGrad(sAniso, c.xy, cx.xy, cy.xy), m, f * w.z, float3(1, 0, 0), float3(0, -1, 0), dc, dn);
}

float MicroFade(float F, float dist) { return F > 0 ? 1 - smoothstep(0.35 * F, F, dist) : 0; }

// the albedo modulated and the normal tilted by the levels in reach
void Micro(VSOut i, inout float3 alb, inout float3 n)
{
    float3 c0 = i.lp * gMic[0].w + gMic[0].xyz, c1 = i.lp * gMic[1].w + gMic[1].xyz, c2 = i.lp * gMic[2].w + gMic[2].xyz;
    float3 c0x = ddx(c0), c0y = ddy(c0), c1x = ddx(c1), c1y = ddy(c1), c2x = ddx(c2), c2y = ddy(c2);   // before any per-pixel branch
    float3 nf = abs(normalize(cross(ddx(i.lp), ddy(i.lp))));      // the face's own normal (planet frame)
    float dist = length(i.wp);
    float3 f = float3(MicroFade(gMicF.x, dist), MicroFade(gMicF.y, dist), MicroFade(gMicF.z, dist));
    if (!any(f > 0)) return;
    float3 w = i.up * i.up; w *= w; w *= w;         // sharp blend: mostly one projection
    w = max(w / dot(w, 1) - 0.02, 0); w /= dot(w, 1);
    float vn = rsqrt(dot(w, w));                    // blended detail keeps its contrast
    w *= saturate(nf * 3 - 1);                      // a projection fades on faces nearly parallel to its axis (steep walls, skirts)
    float3 dc = 0, dn = 0;
    if (f.x > 0) MicroLevel(tMic0, c0, c0x, c0y, gMicM[0], f.x, w, dc, dn);
    if (f.y > 0) MicroLevel(tMic1, c1, c1x, c1y, gMicM[1], f.y, w, dc, dn);
    if (f.z > 0) MicroLevel(tMic2, c2, c2x, c2y, gMicM[2], f.z, w, dc, dn);
    float kc = gMicF.w > 1.5 ? 1.2 : 0.67;          // luminance in .b is gamma-encoded: its contrast in linear light
    alb *= max(1 + kc * vn * dc, 0.05);
    if (gMicF.w > 1.5) {
        float3 g = mul(dn * vn, (float3x3)gRot);
        n = normalize(n + g - n * dot(n, g));
    }
}

// ---- the ground material (Huygens DISR, Orbitersdk/samples/TitanSite/tools/gen_huygens_ground.py): cobbles and sand
// as height + normal + occlusion maps fixed to the planet; near the camera a parallax-occlusion search finds where the
// view ray meets the cobbles and a short ray to the Sun shadows them; further out the normals and occlusion alone
static const float GH_LO = -0.02, GH_HI = 0.10;

SamplerState sLin : register(s3);    // trilinear, wrap: the parallax search (anisotropic taps there cost too much)

float GHeight(float2 uv, float2 dx, float2 dy, float wc)
{
    float h;
    if (wc < 0.03) h = tGs.SampleGrad(sLin, uv, dx, dy).r;              // one map where the ground is all of a kind
    else if (wc > 0.97) h = tGc.SampleGrad(sLin, uv, dx, dy).r;
    else h = lerp(tGs.SampleGrad(sLin, uv, dx, dy).r, tGc.SampleGrad(sLin, uv, dx, dy).r, wc);
    return GH_LO + (GH_HI - GH_LO) * h;
}

// alb, n (global), sun visibility and sky occlusion from the ground material
// what GroundDetail found near the camera, kept for the local lights' cobble shadows (CobbleVis, Lights.cpp's lamps)
static bool gdNear = false;
static float2 gdUV, gdDx, gdDy;
static float gdH, gdWc, gdK;
static float3 gdT, gdB, gdUp;
static float gdF = 1, gdFL = 1;      // GroundDetail's albedo factor, and the local lights' one in its place
void GroundDetail(VSOut i, inout float3 alb, inout float3 n, out float sunVis, out float skyOcc)
{
    sunVis = 1; skyOcc = 1;
    float3 up = normalize(i.up);
    float3 c = i.lp * gGm.w + gGm.xyz;
    float3 a = abs(up), T, B;
    float2 uv;
    if (a.y >= a.x && a.y >= a.z) { uv = c.zx; T = float3(0, 0, 1); B = float3(1, 0, 0); }
    else if (a.x >= a.z) { uv = c.yz; T = float3(0, 1, 0); B = float3(0, 0, 1); }
    else { uv = c.xy; T = float3(1, 0, 0); B = float3(0, 1, 0); }
    T = normalize(T - up * dot(T, up)); B = normalize(B - up * dot(B, up));
    float2 dx = ddx(uv), dy = ddy(uv);
    float dist = length(i.wp);
    float fade = 1 - smoothstep(25, 40, dist);
    float under = gEl.w > 0 ? smoothstep(-0.03, 0.01, i.elev - gEl.z) : 1;     // none under the liquid
    fade *= under;
    if (fade <= 0) return;
    // cobble fields, rubble on the ice, sparse cobbles on the sand (the site's classes, 50 m cells blended)
    float wc = 0.3;
    if (gGround[1].w > 0) {
        float2 gp = gGround[0].xy + float2(dot(i.lp, gGround[1].xyz), dot(i.lp, gGround[2].xyz));
        float2 guv = float2(gp.x * gGround[0].z + 0.5, 0.5 - gp.y * gGround[0].w);
        float4 gw = all(guv > 0) && all(guv < 1) ? tGround.SampleLevel(sClamp, guv, 0) : float4(1, 0, 0, 0);
        wc = saturate(gw.g + gw.b * 0.6 + gw.a * 0.3);
    }
    // the view ray in the map's frame (planet frame: gRot takes planet to global, its transpose back)
    float3 V = normalize(mul((float3x3)gRot, -i.wp));
    float3 vt = float3(dot(V, T), dot(V, B), max(dot(V, up), 0.12));
    float2 uvF = uv;
    float hF = 0;
    if (dist < 12) {
        const int STEPS = 10;
        float2 k = vt.xy / vt.z * gGm.w;                  // uv per metre of height along the ray
        float z = GH_HI, dz = (GH_HI - GH_LO) / STEPS;
        float2 p = uv + k * z, pp = p;
        float hp = GHeight(p, dx, dy, wc), zp = z;
        [loop] for (int s = 0; s < STEPS && hp < z; s++) { pp = p; zp = z; z -= dz; p -= k * dz; hp = GHeight(p, dx, dy, wc); }
        float hq = GHeight(pp, dx, dy, wc);               // refine between the last two steps
        float t = saturate((zp - hq) / max((zp - hq) - (z - hp), 1e-5));
        uvF = lerp(pp, p, t); hF = lerp(zp, z, t);
        gdNear = true; gdUV = uvF; gdH = hF; gdDx = dx; gdDy = dy; gdWc = wc; gdT = T; gdB = B; gdUp = up; gdK = smoothstep(12, 6, dist);
        // the cobbles' shadows: a short ray towards the Sun
        float3 L = gSunL.xyz;
        float lz = dot(L, up);
        if (lz > 0.03) {
            float2 kl = float2(dot(L, T), dot(L, B)) / lz * gGm.w;
            float occ = 0;
            [unroll] for (int s2 = 1; s2 <= 4; s2++) {
                float zz = hF + s2 * 0.018;
                occ = max(occ, GHeight(uvF + kl * (zz - hF), dx, dy, wc) - zz);
            }
            sunVis = lerp(1, saturate(1 - occ / 0.008), smoothstep(12, 6, dist));
        }
    } else hF = GHeight(uv, dx, dy, wc);
    float4 m;                                              // anisotropic only near the camera, one map where it can
    if (dist < 12) m = wc < 0.03 ? tGs.SampleGrad(sAniso, uvF, dx, dy) : wc > 0.97 ? tGc.SampleGrad(sAniso, uvF, dx, dy)
                                 : lerp(tGs.SampleGrad(sAniso, uvF, dx, dy), tGc.SampleGrad(sAniso, uvF, dx, dy), wc);
    else m = wc < 0.03 ? tGs.SampleGrad(sLin, uvF, dx, dy) : wc > 0.97 ? tGc.SampleGrad(sLin, uvF, dx, dy)
                       : lerp(tGs.SampleGrad(sLin, uvF, dx, dy), tGc.SampleGrad(sLin, uvF, dx, dy), wc);
    float2 nt = m.gb * 2 - 1;
    float3 pn = mul((float3x3)gRot, n);                    // the terrain normal in the planet frame
    float3 nd = normalize(pn + T * nt.x + B * nt.y);
    n = normalize(lerp(n, mul(nd, (float3x3)gRot), fade));
    skyOcc = lerp(1, m.a, fade);
    alb *= lerp(1, lerp(1, 1.6, saturate((hF - 0.004) / 0.012)) * lerp(0.75, 1, m.a), fade);   // ice cobbles brighter
    // (for the local lights: the factor taken back out - the ridges' x1.6 and the occlusion are the diffuse sky's; a lamp's
    // N.L and the cobbles' shadow make them, the ice a little lighter by its material, x1.1 - titan-surface, 20.10.2026)
    gdF = lerp(1, lerp(1, 1.6, saturate((hF - 0.004) / 0.012)) * lerp(0.75, 1, m.a), fade);
    gdFL = lerp(1, lerp(1, 1.1, saturate((hF - 0.004) / 0.012)), fade);
}

// the surface textures paint the masks' stepped coast (the liquid black, a pale rim): near the field's smooth coast the
// colour is taken 6 m further inland, up the field's gradient - the dry ground's own; under the liquid that beach darkens
// to a tenth over 4 m (the bed the textures paint dark); the texture's own again 8..10 m inland and 10..12 m out
float3 CoastAlbedo(VSOut i, float3 alb)
{
    float2 tuv = (i.uv - gUV.zw) / gUV.xy;
    float s = CoastDist(tuv);
    if (s > 10 || s < -12) return alb;
    float w, h; tSdf.GetDimensions(w, h);
    float2 e = 1 / (w * gMaskUV.xy);                    // a field texel in tile uv
    float2 g = float2(CoastDist(tuv + float2(e.x, 0)) - s, CoastDist(tuv + float2(0, e.y)) - s) / e;   // [m per uv]
    float2 dir = g / max(dot(g, g), 1e-6);              // uv per metre of s
    float2 dx = ddx(i.uv), dy = ddy(i.uv);
    float3 land = tSurf.SampleGrad(sClamp, (tuv + dir * (6 - min(s, 0))) * gUV.xy + gUV.zw, dx, dy).rgb;   // 6 m inland
    if (s >= 0) return lerp(alb, land, 1 - smoothstep(8, 10, s));
    return lerp(alb, land * lerp(1, 0.1, smoothstep(0, 4, -s)), 1 - smoothstep(10, 12, -s));   // the beach going under, darkening
}

// the cobbles' shadow towards a local light (global direction Lg): the same short ray over the height map as the Sun's
// in GroundDetail (its 0.018 m steps and 0.008 m softness are the cobbles' geometry), near the camera only
float CobbleVis(float3 Lg)
{
    if (!gdNear) return 1;
    float3 L = mul((float3x3)gRot, Lg);                  // to the planet frame (as V in GroundDetail)
    float lz = dot(L, gdUp);
    if (lz <= 0.03) return 1 - gdK;
    float2 kl = float2(dot(L, gdT), dot(L, gdB)) / lz * gGm.w;
    float occ = 0;
    [unroll] for (int s2 = 1; s2 <= 4; s2++) {
        float zz = gdH + s2 * 0.018;
        occ = max(occ, GHeight(gdUV + kl * (zz - gdH), gdDx, gdDy, gdWc) - zz);
    }
    return lerp(1, saturate(1 - occ / 0.008), gdK);
}
// the local lights on the ground (lights.hlsli's list and photometry), each shadowed by the cobbles
float3 LocalLightsGround(float3 wp, float3 n, float3 ng, float3 alb)
{
    if (!LocalLightsOn()) return 0;
    float3 s = 0, Ld;
    uint2 cl = LocalCluster(wp);
    uint cnt = cl.y == 0xFFFFFFFF ? min((uint)gLGrid.w, 64) : cl.y;
    [loop] for (uint k = 0; k < cnt; k++) {
        LocalLightG L = tLights[cl.y == 0xFFFFFFFF ? k : tLIndex[cl.x + k]];
        float3 E = LocalLightE(L, wp, ng, Ld);
        if (!any(E > 0)) continue;
        s += E * saturate(dot(n, Ld)) * CobbleVis(Ld);
    }
    return alb * s;
}

float4 Shade(VSOut i)
{
    float3 alb = gMisc.x > 0 ? tSurf.Sample(sClamp, i.uv).rgb : float3(0.5, 0.5, 0.5);
    if (gShore.x > 0 && gMisc.x > 0) alb = CoastAlbedo(i, alb);
    if (abs(gShadow.z - 6) < 0.5) return float4(alb * 0.5 / max(gHazeRGB.w * gAdapt.rgb, 1e-4), 1);   // test view: the albedo alone
    float3 n = normalize(i.n);
    if (gMicF.w > 0 && gShadow.z < 11.5) Micro(i, alb, n);     // (test runs: SHADOWDBG 12, 13 profile without it)
    float sunVis = 1, skyOcc = 1;
    if (gGm.w > 0 && gShadow.z < 10.5) GroundDetail(i, alb, n, sunVis, skyOcc);   // (test runs: SHADOWDBG 11 profiles without it)
    float nl = saturate(dot(n, gSunDir.xyz)) * (gShadow.z > 12.5 ? 1 : ShadowAtN(i.wp, normalize(i.n))) * sunVis;   // (13: without the shadow)
    float3 sky = SkyLightShaded(n, i.wp, normalize(i.n)) * skyOcc * SkyVisibility(i.wp, normalize(i.n));   // under a vehicle less sky (lights.hlsli)
    float3 col = alb * (nl * SunLight(i.wp) + sky);
    {   // headlights, lamps on the ground: the material's albedo (no sky occlusion in it), the detail normal softened (a near,
        // low light: the full one turns the cobbles to 0/1 patches), the cobbles' shadows towards each lamp
        float3 nL = normalize(lerp(normalize(i.n), n, 0.6));
        col += LocalLightsGround(i.wp, nL, normalize(i.n), alb / max(gdF, 0.05) * gdFL);
    }
    if (gShadow.z > 1.5 && gShadow.z < 9.5) {
        float sl = dot(saturate(dot(n, gSunDir.xyz)) * sunVis * SunLight(i.wp), 1.0 / 3), kl = dot(SkyLight(n, i.wp) * skyOcc, 1.0 / 3);
        float4 au = Aureole();
        float3 c = gShadow.z < 2.5 ? float3(sl, kl, 0) / max(sl + kl, 1e-9)     // =2: red the Sun's share of the light, green the sky's
                 : gShadow.z < 3.5 ? (dot(nl * SunLight(i.wp), 1.0 / 3) + dot(sky, 1.0 / 3)) / max(sl + kl, 1e-9)   // =3: the light left by all the shadows
                 : float3(nl, AureoleVis(i.wp, normalize(i.n), au.x), au.y * 4);    // =4: the Sun's factor, the aureole's visibility, its red share x 4
        return float4(c * 0.5 / max(gHazeRGB.w * gAdapt.rgb, 1e-4), 1);
    }
    if (gSky0.w < 0.5 && !FxActive()) col += alb * 0.05 * smoothstep(-0.05, 0.1, dot(n, gSunDir.xyz));   // seen from space: a soft terminator
    return float4(ApplyHaze(col, i.wp), 1);
}

// test runs (Client.cfg TileDbg = 1): each tile in its level's colour, unlit (gMisc.w)
float4 TileDbg(VSOut i)
{
    uint k = (uint)gMisc.y % 6;
    float3 c = k == 0 ? float3(1, 0, 0) : k == 1 ? float3(0, 1, 0) : k == 2 ? float3(0, 0, 1) : k == 3 ? float3(1, 1, 0) : k == 4 ? float3(0, 1, 1) : float3(1, 0, 1);
    return float4(c * 0.05, 1);
}
// test view (D3D11CLIENT_SHADOWDBG): the Sun's visibility alone, white lit, black in shadow
float4 ShadowDbg(VSOut i)
{
    float s = ShadowAtN(i.wp, normalize(i.n));
    float d = length(i.wp);      // the cascade by colour: focus map grey, 1 red, 2 green, 3 blue
    float3 c = d < gCascFar.y ? float3(1, 0.4, 0.4) : d < gCascFar.z ? float3(0.4, 1, 0.4) : d < gCascFar.w ? float3(0.4, 0.4, 1) : float3(1, 1, 1);
    return float4(s * c * 0.5 / max(gHazeRGB.w * gAdapt.rgb, 1e-4), 1);
}
float4 PS(VSOut i) : SV_Target { if (gShadow.z > 0.5 && gShadow.z < 1.5) return ShadowDbg(i); return abs(gMisc.w - 1) < 0.5 ? TileDbg(i) : Shade(i); }
