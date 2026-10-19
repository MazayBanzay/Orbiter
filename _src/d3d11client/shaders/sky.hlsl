// D3D11Client: the sky of the atmosphere the camera is in (fullscreen, behind everything)
#include "common.hlsli"
struct VSOut { float4 pos : SV_Position; float2 ndc : NDC; };
VSOut VS(uint id : SV_VertexID)
{
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o; o.ndc = p * float2(2, -2) + float2(-1, 1); o.pos = float4(o.ndc, 0, 1);
    return o;
}
float4 PS(VSOut i) : SV_Target
{
    float4 w = mul(float4(i.ndc, 1e-7, 1), gInvVP);
    float3 dir = normalize(w.xyz / w.w);
    return float4(SkyColour(dir), 1);
}
