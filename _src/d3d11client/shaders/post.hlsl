// D3D11Client: HDR -> back buffer (sRGB view); a gentle shoulder only, so Orbiter's textures keep their look
#include "common.hlsli"
Texture2D tHDR : register(t0);
Texture2D tBloom : register(t1);     // the glow (Bloom.cpp), half size; none: black
SamplerState sClamp : register(s1);
struct VSOut { float4 pos : SV_Position; float2 uv : UV; };
VSOut VS(uint id : SV_VertexID)
{
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o; o.uv = p; o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 PS(VSOut i) : SV_Target
{
    float3 c = tHDR.Load(int3(i.pos.xy, 0)).rgb;
    c += tBloom.SampleLevel(sClamp, i.uv, 0).rgb * 0.01;   // the glow of five levels: 1 % of the light around
    c *= gHazeRGB.w * gAdapt.rgb;   // exposure (Advanced, the eye's), colour balance
    // a filmic curve (ACES fitted, Narkowicz): the mid tones where they were (x 0.8 in: 0.18 -> 0.2), more contrast
    // below, the highlights rolled off softly instead of clipping
    c *= 0.8;
    c = saturate(c * (2.51 * c + 0.03) / (c * (2.43 * c + 0.59) + 0.14));
    return float4(c, 1);
}
