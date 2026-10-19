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
};
Texture2D tSurf : register(t0);
Texture2D tMic0 : register(t1);     // finest
Texture2D tMic1 : register(t2);
Texture2D tMic2 : register(t3);     // coarsest
Texture2D tGc : register(t4);       // ground material, cobble field: r height [-0.02, 0.10] m, g b normal, a occlusion
Texture2D tGs : register(t5);       // ground material, sand
Texture2D tMask : register(t15);     // liquid mask: alpha 0 = liquid
Texture2D tGround : register(t6);   // ground class weights: r sand, g cobbles, b ice bedrock, a wet margin
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

VSOut VS(VSIn v)
{
    VSOut o;
    o.pos = mul(float4(v.p, 1), gWVP);
    o.n = mul(v.n, (float3x3)gRot);
    o.wp = mul(float4(v.p, 1), gW).xyz;
    o.uv = v.uv * gUV.xy + gUV.zw;
    o.lp = v.p;
    o.up = normalize(gMicC.xyz + v.p);
    o.elev = ElevAt(v.p);
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
}

// the coastline as the D3D9 client draws it: from the liquid mask, per pixel. The elevation data steps 0.1 m and the
// liquid level sits between two steps, so the terrain crosses it along the sample grid (a staircase). Within 15 cm of
// the level the depth written here says land or liquid as the mask does (the liquid pass decides by depth); the
// geometry itself is not moved, so whatever stands there stands at the physics height.
// the mask's alpha through a cubic B-spline (4 bilinear taps): its texels (metres wide, DXT5-blocked) would draw the
// coast as rounded steps; the spline makes it a smooth curve
float MaskCubic(float2 uv)
{
    float mw, mh; tMask.GetDimensions(mw, mh);
    float2 sz = float2(mw, mh);
    float2 st = uv * sz - 0.5, i0 = floor(st), f = st - i0;
    float2 f2 = f * f, f3 = f2 * f;
    float2 w0 = (-f3 + 3 * f2 - 3 * f + 1) / 6, w1 = (3 * f3 - 6 * f2 + 4) / 6;
    float2 w2 = (-3 * f3 + 3 * f2 + 3 * f + 1) / 6, w3 = f3 / 6;
    float2 g0 = w0 + w1, g1 = w2 + w3;
    float2 h0 = (i0 - 0.5 + w1 / g0) / sz, h1 = (i0 + 1.5 + w3 / g1) / sz;
    return g0.y * (g0.x * tMask.SampleLevel(sClamp, float2(h0.x, h0.y), 0).a + g1.x * tMask.SampleLevel(sClamp, float2(h1.x, h0.y), 0).a)
         + g1.y * (g0.x * tMask.SampleLevel(sClamp, float2(h0.x, h1.y), 0).a + g1.x * tMask.SampleLevel(sClamp, float2(h1.x, h1.y), 0).a);
}

float ShoreDepth(VSOut i)
{
    float depth = i.pos.z;
    if (gMisc.z <= 0 || gEl.w <= 0) return depth;
    float d = i.elev - gEl.z;
    // the band where the mask decides grows with the distance: the farther tiles' meshes stand off the full-resolution
    // ground by more (a coarser grid over 0.1 m steps), and there the terrain would cross the level along its grid
    float tol = min(0.15 + 0.004 * length(i.wp), 1.5);   // at most 1.5 m: farther the coarse masks would move the coast
    float w = 1 - smoothstep(tol * 0.67, tol, abs(d));
    if (w <= 0) return depth;
    float2 tuv = (i.uv - gUV.zw) / gUV.xy;
    float m = MaskCubic(tuv * gMaskUV.xy + gMaskUV.zw);
    float target = (smoothstep(0.35, 0.65, m) - 0.5) * 0.1;      // 5 cm above the level on land, below on liquid
    float4 cp = mul(float4(i.lp + normalize(i.up) * (target - d) * w, 1), gWVP);
    return cp.z / cp.w;
}

float4 Shade(VSOut i)
{
    float3 alb = gMisc.x > 0 ? tSurf.Sample(sClamp, i.uv).rgb : float3(0.5, 0.5, 0.5);
    float3 n = normalize(i.n);
    if (gMicF.w > 0) Micro(i, alb, n);
    float sunVis = 1, skyOcc = 1;
    if (gGm.w > 0) GroundDetail(i, alb, n, sunVis, skyOcc);
    float nl = saturate(dot(n, gSunDir.xyz)) * ShadowAtN(i.wp, normalize(i.n)) * sunVis;
    float3 col = alb * (nl * SunLight(i.wp) + SkyLight(n, i.wp) * skyOcc);
    if (gSky0.w < 0.5 && !FxActive()) col += alb * 0.05 * smoothstep(-0.05, 0.1, dot(n, gSunDir.xyz));   // seen from space: a soft terminator
    return float4(ApplyHaze(col, i.wp), 1);
}

// all tiles; the ones that reach the liquid level use PSD (writing depth disables the early depth test, so only there)
float4 PS(VSOut i) : SV_Target { return Shade(i); }
float4 PSD(VSOut i, out float depthOut : SV_Depth) : SV_Target { depthOut = ShoreDepth(i); return Shade(i); }
