// D3D11Client: HDR -> back buffer (sRGB view): the exposure, a filmic tone curve, an adaptive sharpening
#include "common.hlsli"
Texture2D tHDR : register(t0);
Texture2D tBloom : register(t1);     // the glow (Bloom.cpp), half size; none: black
SamplerState sClamp : register(s1);
cbuffer Post : register(b2)
{
    float4 gPost;     // x: sharpening 0..1 (Client.cfg Sharpen)
};
struct VSOut { float4 pos : SV_Position; float2 uv : UV; };
VSOut VS(uint id : SV_VertexID)
{
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o; o.uv = p; o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

// a filmic curve: ACES (RRT + sRGB ODT) as fitted by S. Hill, with its colour matrices - more contrast, the highlights
// rolled off softly and turning towards white (a white suit under Titan's orange light stays white, not orange); the
// mid tones where they were (x 1.6 in: 0.18 -> 0.2)
float3 ToneMap(float3 c)
{
    if (NotFinite(c)) c = 0; c = max(c, 0);     // a broken pixel stays black, never a colour the curve turns blue
    c *= gHazeRGB.w * gAdapt.rgb * 1.6;           // exposure (Advanced, the eye's), colour balance
    const float3x3 inM = { 0.59719, 0.35458, 0.04823, 0.07600, 0.90834, 0.01566, 0.02840, 0.13383, 0.83777 };
    const float3x3 outM = { 1.60475, -0.53108, -0.07367, -0.10208, 1.10813, -0.00605, -0.00327, -0.07276, 1.07602 };
    c = mul(inM, c);
    c = (c * (c + 0.0245786) - 0.000090537) / (c * (0.983729 * c + 0.4329510) + 0.238081);
    return saturate(mul(outM, c));
}

float3 Px(int2 p, float2 uv)
{
    return tHDR.Load(int3(p, 0)).rgb + tBloom.SampleLevel(sClamp, uv, 0).rgb * 0.01;   // the glow: 1 % of the light around
}

float4 PS(VSOut i) : SV_Target
{
    int2 p = int2(i.pos.xy);
    float3 c = ToneMap(Px(p, i.uv));
    if (gPost.x <= 0) return float4(c, 1);
    // adaptive sharpening (after the tone curve, where the eye judges contrast): a cross of four neighbours subtracted
    // with a weight that is strong on soft detail and fades where the local contrast is already high or the pixel near
    // black or white (no halos, no grain lifted from flat areas). The resolve, the TAA and the texture filtering soften
    // the image; this gives the fine detail (seams, cobbles, edges) back
    float3 n = ToneMap(tHDR.Load(int3(p + int2(0, -1), 0)).rgb), s = ToneMap(tHDR.Load(int3(p + int2(0, 1), 0)).rgb);
    float3 w = ToneMap(tHDR.Load(int3(p + int2(-1, 0), 0)).rgb), e = ToneMap(tHDR.Load(int3(p + int2(1, 0), 0)).rgb);
    float3 mn = min(c, min(min(n, s), min(w, e))), mx = max(c, max(max(n, s), max(w, e)));
    float3 amp = sqrt(saturate(min(mn, 1 - mx) / max(mx, 1e-4)));
    float3 k = -amp * lerp(0.125, 0.2, gPost.x);
    float3 o = (c + k * (n + s + w + e)) / (1 + 4 * k);
    return float4(saturate(o), 1);
}
