// D3D11Client: HDR -> back buffer (sRGB view); a gentle shoulder only, so Orbiter's textures keep their look
#include "common.hlsli"
Texture2D tHDR : register(t0);
struct VSOut { float4 pos : SV_Position; float2 uv : UV; };
VSOut VS(uint id : SV_VertexID)
{
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o; o.uv = p; o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 PS(VSOut i) : SV_Target
{
    float3 c = tHDR.Load(int3(i.pos.xy, 0)).rgb * gHazeRGB.w * gAdapt.rgb;   // exposure (Advanced, the eye's), colour balance
    c = c / (1 + max(c - 0.8, 0) * 0.5);
    return float4(c, 1);
}
