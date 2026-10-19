// D3D11Client: the modules' native canvases (HudApi.cpp, Orbitersdk\include\D3D11Hud.h). Drawn in gamma space.
// Each vertex carries its primitive's kind; coverage is anti-aliased from the exact distance to the shape, and the glow
// (a soft halo) fades out from the shape's edge over gw px at gs times its alpha. One call's overlaps blend once: a
// fully covered pixel takes the call's depth, a partly covered one (edges, the halo) half a step less, depth GREATER.
cbuffer Hud : register(b0)
{
    float4 gSize;    // xy: 2 / target size, z: the depth half step, w: 1 = hard edges (copy states)
    float4 gFx;      // rgb: gain, w: gamma (images)
    float4 gFx2;     // x: noise amplitude, y: a seed changing every frame, z: 1 = the source is read through an sRGB view
    float4 gRes;     // the light resolve: xy 1 / size
};
Texture2D tSrc : register(t0);
SamplerState sLin : register(s0);

struct VIn {
    float2 pos : POSITION; float2 uv : TEXCOORD0; float4 p : TEXCOORD1; float4 q : TEXCOORD2;
    float4 col : COLOR; uint kind : KIND; float z : DEPTH;
};
struct VOut {
    float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 p : TEXCOORD1; float4 q : TEXCOORD2;
    float4 col : COLOR; nointerpolation uint kind : KIND; float2 px : TEXCOORD3;
};

VOut VS(VIn v)
{
    VOut o;
    o.pos = float4(v.pos * gSize.xy * float2(1, -1) + float2(-1, 1), v.z, 1);
    o.uv = v.uv; o.p = v.p; o.q = v.q; o.col = v.col; o.kind = v.kind; o.px = v.pos;
    return o;
}

float Hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233)) + gFx2.y) * 43758.5453); }

struct POut { float4 c : SV_Target; float d : SV_Depth; };

// q: x = half width (lines, outlines; < 0 filled shapes), y = dash length (0 none), z = glow width px, w = glow strength
POut PS(VOut i)
{
    float4 c = i.col;
    float d = -1e9;          // the signed distance to the shape's edge, px (< 0 inside)
    bool img = false;
    if (i.kind == 0) d = -1e9;                       // a filled triangle: inside
    else if (i.kind == 1) {                          // a line: a capsule p.xy - p.zw of half width q.x
        float2 a = i.p.xy, ab = i.p.zw - i.p.xy;
        float L2 = dot(ab, ab);
        float t = L2 > 0 ? saturate(dot(i.px - a, ab) / L2) : 0;
        d = length(a + ab * t - i.px) - i.q.x;
        if (i.q.y > 0 && fmod(t * sqrt(L2), i.q.y * 2) > i.q.y) discard;
    } else if (i.kind == 2) {                        // an ellipse: centre p.xy, radii p.zw; q.x < 0 filled, else the outline
        float2 X = i.px - i.p.xy, r = i.p.zw;
        float k0 = length(X / r), k1 = length(X / (r * r));
        float e = k1 > 1e-6 ? k0 * (k0 - 1) / k1 : -min(r.x, r.y);
        d = i.q.x < 0 ? e : abs(e) - i.q.x;
    } else if (i.kind == 3) {                        // a surface: tinted, gain, gamma, grain
        float4 t = tSrc.SampleLevel(sLin, i.uv, 0);
        if (gFx2.z > 0.5) t.rgb = t.rgb <= 0.0031308 ? t.rgb * 12.92 : 1.055 * pow(abs(t.rgb), 1 / 2.4) - 0.055;
        if (i.p.x > 0.5) t.a = 1;                    // a source without alpha
        t.rgb = pow(saturate(t.rgb * gFx.rgb), gFx.w);
        if (gFx2.x > 0) t.rgb = saturate(t.rgb + (Hash(i.px) - 0.5) * 2 * gFx2.x);
        c *= t; img = true;
    } else if (i.kind == 4) {                        // a glyph: the distance in the atlas's alpha, q.x = px per unit of it
        float v = tSrc.SampleLevel(sLin, i.uv, 0).a;
        d = (0.5 - v) * i.q.x;
    }
    float cov = img ? 1 : saturate(0.5 - d);
    float a = cov;
    if (!img && i.q.z > 0 && d > -0.5) {             // the halo outside the edge
        float g = i.q.w * saturate(1 - max(d, 0) / i.q.z);
        a = cov + (1 - cov) * g * g;
    }
    if (a <= 0.003) discard;
    if (gSize.w > 0.5) { if (cov < 0.5) discard; cov = 1; a = 1; }
    POut o;
    o.c = float4(saturate(c.rgb), saturate(c.a * a));
    o.d = cov >= 0.999 ? i.pos.z : i.pos.z - gSize.z;
    return o;
}

// the light resolve (LIGHT canvases): light on black -> colour at full strength + alpha = the brightest channel
struct FOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
FOut FullVS(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    FOut o; o.uv = uv; o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 LightPS(FOut i) : SV_Target
{
    float3 c = tSrc.SampleLevel(sLin, i.uv, 0).rgb;
    float a = max(c.r, max(c.g, c.b));
    return a < 0.002 ? float4(0, 0, 0, 0) : float4(c / a, a);
}
// the supersampled canvases: 2x2 down (the linear sampler at the shared corner of four texels)
float4 DownPS(FOut i) : SV_Target
{
    return tSrc.SampleLevel(sLin, i.uv, 0);
}
