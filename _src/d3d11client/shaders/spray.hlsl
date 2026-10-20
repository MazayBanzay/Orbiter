// D3D11Client: spray off wheels in a liquid (Spray.cpp). Each card is one throw: the frames of a liquid simulation baked
// from the side (tools\bake_splash_blender.py, Mantaflow FLIP in Titan's gravity; tools\pack_splash.py: 64 frames, 8 x 8,
// RGB the normal in the bake camera's frame, A the coverage), two frames blended; the card stands in the vertical plane
// of the throw, scaled by Froude similarity on the CPU. Lit as a clear liquid: the scene behind refracted through it,
// the sky and the Sun mirrored by Fresnel on the baked normal, the light bounced among its torn parts; soft against
// the scene's depth (the liquid's surface hides what falls back into it).
#include "exhaust_common.hlsli"

cbuffer Spray : register(b5)
{
    float4 sM0, sM1, sM2;   // local -> camera-centred global: rows; w the offset
    float4x4 sVP;           // camera-centred global -> clip
    float4 sTint;           // the liquid's colour seen through it (rgb), w: the frames per row of the atlas
};

struct Card { float3 p0; float frame; float3 h; float L; float4 misc; };   // h: the throw's horizontal direction (local); misc: x opacity, y mirror
StructuredBuffer<Card> tCards : register(t4);
Texture2D tSheet : register(t6);       // (t6: tExBB is not used here)
Texture2D tBehind : register(t3);      // the scene so far (single-sampled copy)

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;               // x along the flight 0..1, y across 0..1
    float3 wp : TEXCOORD1;
    nointerpolation float4 fr : TEXCOORD2;     // x frame, y opacity, z mirror
    nointerpolation float3 ax : TEXCOORD3;     // the card's axes (global): along, across
    nointerpolation float3 sd : TEXCOORD4;
};

float3 ToGlobal(float3 l) { return float3(dot(sM0.xyz, l), dot(sM1.xyz, l), dot(sM2.xyz, l)); }

VSOut VS(uint vid : SV_VertexID, uint inst : SV_InstanceID)
{
    Card c = tCards[inst];
    static const float2 corner[6] = { float2(0, 0), float2(0, 1), float2(1, 1), float2(0, 0), float2(1, 1), float2(1, 0) };
    float2 k = corner[vid];
    // the card stands in the
    // vertical plane of the throw (its horizontal direction h and the local up)
    float3 off = float3(sM0.w, sM1.w, sM2.w);
    float3 p0 = ToGlobal(c.p0) + off;
    float3 hw = normalize(ToGlobal(c.h)), up = normalize(ToGlobal(float3(0, 1, 0)));
    // turned about the vertical to face the eye (a cylindrical billboard, as engines draw side-baked flipbooks): its
    // horizontal axis the screen's, on the throw's side; seen along the throw the fan narrows, never to a line
    float3 Vc = normalize(p0);
    float3 rt = cross(up, Vc); rt = length(rt) > 1e-4 ? normalize(rt) : hw;
    float side = dot(rt, hw);
    float3 ax = rt * (side < 0 ? -1 : 1);
    float squeeze = 0.45 + 0.55 * abs(side);
    float s = c.L / 6.0;                                    // the bake's frame: 6 m square, the hub 1.6 m from its left, 0.3 m up
    float3 wp = p0 + ax * ((k.x * 6.0 - 1.6) * s * squeeze) + up * ((k.y * 6.0 - 0.3) * s);
    float3 V = normalize(wp);
    float3 sd = up;
    VSOut o;
    o.pos = mul(float4(wp, 1), sVP);
    o.uv = float2(k.x, 1 - k.y);
    o.wp = wp;
    o.fr = float4(c.frame, c.misc.x, c.misc.y, 0);
    o.ax = ax; o.sd = up;
    return o;
}

float4 Frame(float f, float2 uv)
{
    const float G = sTint.w;
    float2 cell = float2(fmod(f, G), floor(f / G));
    float2 q = (cell + clamp(uv, 0.004, 0.996)) / G;
    return tSheet.SampleLevel(sExLin, q, 0);
}

float4 PS(VSOut i) : SV_Target
{
    const float NF = sTint.w * sTint.w;
    float f = clamp(i.fr.x, 0, NF - 1.001);
    float f0 = floor(f), w = f - f0;
    float4 s = lerp(Frame(f0, i.uv), Frame(f0 + 1, i.uv), w);
    float cov = s.a * i.fr.y * saturate((0.97 - i.uv.y) / 0.04);   // (what lies on the bake's floor is the liquid it fell back into)
    if (cov < 0.003) discard;
    float3 V = normalize(i.wp);
    float3 nf = -V;                                         // the card faces the eye
    float3 nb = float3(s.r * 2 - 1, s.g * 2 - 1, 1 - 2 * s.b);    // the bake's camera: x along the throw, y up, z to the eye
    float3 N = normalize(i.ax * nb.x + i.sd * nb.y + nf * nb.z);
    float2 nxy = nb.xy;
    float cosv = saturate(dot(N, -V));
    float F = 0.02 + 0.98 * pow(1 - cosv, 5);
    float3 Rd = reflect(V, N);
    float3 L = gSunDir.xyz;
    float3 sun = SunLight(i.wp) * ShadowAt(i.wp);
    // what it mirrors: the sky about the reflected direction, the Sun's glint (a clear liquid's surface: sharp)
    float3 refl = SkyLight(Rd, i.wp) * 0.5 + sun * pow(saturate(dot(Rd, L)), 300) * 6;
    // what it carries through: the scene behind, bent by the sheet (a thin film shifts it by ~ (n - 1) * thickness * the
    // normal's tilt; the screen-space offset scaled to read), tinted by the liquid as it thickens
    float th = saturate(1 - abs(nb.z) * 0.7);
    uint W, H; tBehind.GetDimensions(W, H);
    float2 off = nxy * (10 + 30 * th) * float2(1, -1);
    float3 bg = tBehind.Load(int3(clamp(i.pos.xy + off, 0, float2(W - 1, H - 1)), 0)).rgb;
    float3 thru = bg * lerp(1, sTint.rgb, th) + sTint.rgb * sun * 0.25 * th * pow(saturate(dot(-V, -L) * 0.5 + 0.5), 4);
    float a = cov;                                         // the scene behind is composed here: the sheet replaces it
    // and what it scatters: a torn sheet and its drops are many small surfaces - the light bounced among them whitens it
    // (white water); the more broken (thinner, more drops), the more
    float3 amb = SkyLight(float3(sM0.y, sM1.y, sM2.y), i.wp);
    float3 white = sTint.rgb * (amb * 0.30 + sun * 0.20) * (0.35 + 0.65 * (1 - th));
    float3 c = (lerp(thru, refl, F) + white * 0.6) * a;    // premultiplied
    // soft against the scene's depth (the sheet rising out of the liquid)
    float3 sw; float dS = ExSceneDist(i.pos.xy, sw);
    float fade = saturate((dS - length(i.wp)) / 0.08);
    a *= fade; c *= fade;
    if (a < 0.002) discard;
    return float4(c, a);
}
