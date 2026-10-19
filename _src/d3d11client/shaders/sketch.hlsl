// D3D11Client: the Sketchpad on the GPU (GpuPad.cpp). Drawn into a surface's gamma-space (UNORM) view, as D3D9Client
// draws its surfaces. Each vertex carries its primitive's kind; coverage is anti-aliased from the distance to the shape.
// One primitive (a polyline, a rectangle's outline) has one depth: GREATER lets its overlaps blend once, later
// primitives always over earlier ones. A pixel only partly covered takes half a step less, so the full cover wins.
cbuffer Sk : register(b0)
{
    float4 gSize;    // xy: 2 / target size
    float4 gGamma;   // rgb: gamma (texture kinds), w: 1 on
    float4 gMode;    // x: 1 = hard edges (the copy blend states), y: the depth half step, z: 1 = the source is read through an sRGB view
    float4 gKey;     // rgb: the colour key (kind 5), 0..1
};
Texture2D tSrc : register(t0);
Texture2D tText : register(t1);
SamplerState sLin : register(s0);
SamplerState sPt : register(s1);

struct VIn {
    float2 pos : POSITION; float2 uv : TEXCOORD0; float4 p : TEXCOORD1; float2 q : TEXCOORD2;
    float4 col : COLOR; float4 tint : TEXCOORD3; uint kind : KIND; float z : DEPTH;
};
struct VOut {
    float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 p : TEXCOORD1; float2 q : TEXCOORD2;
    float4 col : COLOR; float4 tint : TEXCOORD3; nointerpolation uint kind : KIND; float2 px : TEXCOORD4;
};

VOut VS(VIn v)
{
    VOut o;
    o.pos = float4(v.pos * gSize.xy * float2(1, -1) + float2(-1, 1), v.z, 1);
    o.uv = v.uv; o.p = v.p; o.q = v.q; o.col = v.col; o.tint = v.tint; o.kind = v.kind; o.px = v.pos;
    return o;
}

struct POut { float4 c : SV_Target; float d : SV_Depth; };

POut PS(VOut i)
{
    float cov = 1;
    float4 c = i.col;
    if (i.kind == 1) {              // a line: a capsule from p.xy to p.zw, half width q.x, dashes of q.y
        float2 a = i.p.xy, ab = i.p.zw - i.p.xy;
        float L2 = dot(ab, ab);
        float t = L2 > 0 ? saturate(dot(i.px - a, ab) / L2) : 0;
        cov = saturate(i.q.x + 0.5 - length(a + ab * t - i.px));
        if (i.q.y > 0 && fmod(t * sqrt(L2), i.q.y * 2) > i.q.y) cov = 0;
    } else if (i.kind == 2) {       // an ellipse: centre p.xy, radii p.zw; q.x < 0 filled, else the outline's half width
        float2 X = i.px - i.p.xy, r = i.p.zw;
        float k0 = length(X / r), k1 = length(X / (r * r));
        float d = k1 > 1e-6 ? k0 * (k0 - 1) / k1 : -min(r.x, r.y);
        cov = i.q.x < 0 ? saturate(0.5 - d) : saturate(i.q.x + 0.5 - abs(d));
    } else if (i.kind == 3 || i.kind == 5) {     // a surface (StretchRect, blits): tinted, gamma
        float4 t = i.kind == 5 ? tSrc.SampleLevel(sPt, i.uv, 0) : (i.q.x > 0.5 ? tSrc.SampleLevel(sPt, i.uv, 0) : tSrc.SampleLevel(sLin, i.uv, 0));
        if (i.kind == 5 && all(abs(t.rgb - gKey.rgb) < 0.6 / 255)) discard;
        if (gMode.z > 0.5) t.rgb = t.rgb <= 0.0031308 ? t.rgb * 12.92 : 1.055 * pow(abs(t.rgb), 1 / 2.4) - 0.055;   // back to gamma space
        if (i.q.y > 0.5) t.a = 1;    // a source without alpha
        t *= i.tint;
        if (gGamma.w > 0) t.rgb = pow(saturate(t.rgb), gGamma.rgb);
        c *= t;
    } else if (i.kind == 4) {       // text: GDI's coverage (white on black) in the atlas
        float3 t = tText.SampleLevel(sPt, i.uv, 0).rgb;
        cov = max(t.r, max(t.g, t.b));
    }
    if (cov <= 0.002) discard;
    if (gMode.x > 0.5) { if (cov < 0.5) discard; cov = 1; }
    POut o;
    o.c = float4(saturate(c.rgb), saturate(c.a * cov));
    o.d = cov >= 0.999 ? i.pos.z : i.pos.z - gMode.y;
    return o;
}
