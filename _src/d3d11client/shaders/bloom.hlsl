// D3D11Client: bloom (Bloom.cpp) - the bright parts of the HDR image glow softly over their surroundings, as a lens and
// an eye show them: the Sun in the haze, a glint on the liquid, a lamp, an exhaust. Down: a 13-tap filter halving the
// image five times (the first with Karis' average: a single hot pixel does not flicker); up: a 3 x 3 tent added back
// level by level. The post pass adds the result to the image before the exposure.
cbuffer Bloom : register(b0)
{
    float4 bTex;      // xy: 1 / the source's size; z: 1 = Karis' average (the first step down)
};
Texture2D tSrc : register(t0);
SamplerState sClamp : register(s1);
struct VSOut { float4 pos : SV_Position; float2 uv : UV; };

float3 S(float2 uv, float2 o) { return min(tSrc.SampleLevel(sClamp, uv + o * bTex.xy, 0).rgb, 64); }
float W(float3 c) { return bTex.z > 0.5 ? 1 / (1 + dot(c, float3(0.2126, 0.7152, 0.0722))) : 1; }

float4 PSDown(VSOut i) : SV_Target
{
    float2 uv = i.uv;
    float3 a = S(uv, float2(-2, -2)), b = S(uv, float2(0, -2)), c = S(uv, float2(2, -2));
    float3 d = S(uv, float2(-1, -1)), e = S(uv, float2(1, -1));
    float3 f = S(uv, float2(-2, 0)), g = S(uv, float2(0, 0)), h = S(uv, float2(2, 0));
    float3 j = S(uv, float2(-1, 1)), k = S(uv, float2(1, 1));
    float3 l = S(uv, float2(-2, 2)), m = S(uv, float2(0, 2)), n = S(uv, float2(2, 2));
    float3 g0 = (d + e + j + k) * 0.25, g1 = (a + b + f + g) * 0.25, g2 = (b + c + g + h) * 0.25;
    float3 g3 = (f + g + l + m) * 0.25, g4 = (g + h + m + n) * 0.25;
    float w0 = 0.5 * W(g0), w1 = 0.125 * W(g1), w2 = 0.125 * W(g2), w3 = 0.125 * W(g3), w4 = 0.125 * W(g4);
    return float4((g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4), 1);
}

float4 PSUp(VSOut i) : SV_Target
{
    float2 uv = i.uv;
    float3 s = S(uv, float2(-1, -1)) + 2 * S(uv, float2(0, -1)) + S(uv, float2(1, -1))
             + 2 * S(uv, float2(-1, 0)) + 4 * S(uv, float2(0, 0)) + 2 * S(uv, float2(1, 0))
             + S(uv, float2(-1, 1)) + 2 * S(uv, float2(0, 1)) + S(uv, float2(1, 1));
    return float4(s / 16, 1);
}
