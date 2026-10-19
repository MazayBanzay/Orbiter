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
};
Texture2D tSurf : register(t0);
Texture2D tMic0 : register(t1);     // finest
Texture2D tMic1 : register(t2);
Texture2D tMic2 : register(t3);     // coarsest
SamplerState sAniso : register(s0);
SamplerState sClamp : register(s1);

struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD; };
struct VSOut { float4 pos : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD; float3 wp : WPOS; float3 lp : LPOS; float3 up : UPDIR; };

VSOut VS(VSIn v)
{
    VSOut o;
    o.pos = mul(float4(v.p, 1), gWVP);
    o.n = mul(v.n, (float3x3)gRot);
    o.wp = mul(float4(v.p, 1), gW).xyz;
    o.uv = v.uv * gUV.xy + gUV.zw;
    o.lp = v.p;
    o.up = normalize(gMicC.xyz + v.p);
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

float4 PS(VSOut i) : SV_Target
{
    float3 alb = gMisc.x > 0 ? tSurf.Sample(sClamp, i.uv).rgb : float3(0.5, 0.5, 0.5);
    float3 n = normalize(i.n);
    if (gMicF.w > 0) Micro(i, alb, n);
    float nl = saturate(dot(n, gSunDir.xyz)) * ShadowAt(i.wp);
    float3 col = alb * (nl * SunLight(i.wp) + SkyLight(n, i.wp));
    if (gSky0.w < 0.5 && !FxActive()) col += alb * 0.05 * smoothstep(-0.05, 0.1, dot(n, gSunDir.xyz));   // seen from space: a soft terminator
    return float4(ApplyHaze(col, i.wp), 1);
}
