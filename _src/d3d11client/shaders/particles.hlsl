// D3D11Client: Orbiter particle streams (camera-facing quads built on the CPU)
#include "common.hlsli"
cbuffer Part : register(b1)
{
    float4x4 gPVP;
    float4 gPFlags;     // x: emissive
};
Texture2D tTex : register(t0);
SamplerState sClamp : register(s1);
struct VSIn { float3 p : POSITION; float2 uv : TEXCOORD; float4 c : COLOR; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; float4 c : COLOR; float3 wp : WPOS; };
VSOut VS(VSIn v)
{
    VSOut o; o.pos = mul(float4(v.p, 1), gPVP); o.uv = v.uv; o.c = v.c; o.wp = v.p;
    return o;
}
float4 PS(VSOut i) : SV_Target
{
    float4 t = tTex.Sample(sClamp, i.uv);
    float a = t.a * i.c.a;
    if (gPFlags.x > 0.5) return float4(t.rgb * a, a);           // emissive: added
    float3 col = t.rgb * (SunLight(i.wp) * 0.6 + SkyLight(float3(0, 0, 0), i.wp) + 0.05);   // diffuse: the light around
    return float4(ApplyHaze(col, i.wp), a);
}
