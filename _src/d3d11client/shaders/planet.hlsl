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
};
Texture2D tSurf : register(t0);
SamplerState sAniso : register(s0);
SamplerState sClamp : register(s1);

struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD; };
struct VSOut { float4 pos : SV_Position; float3 n : NORMAL; float2 uv : TEXCOORD; float3 wp : WPOS; };

VSOut VS(VSIn v)
{
    VSOut o;
    o.pos = mul(float4(v.p, 1), gWVP);
    o.n = mul(v.n, (float3x3)gRot);
    o.wp = mul(float4(v.p, 1), gW).xyz;
    o.uv = v.uv * gUV.xy + gUV.zw;
    return o;
}

float4 PS(VSOut i) : SV_Target
{
    float3 alb = gMisc.x > 0 ? tSurf.Sample(sClamp, i.uv).rgb : float3(0.5, 0.5, 0.5);
    float3 n = normalize(i.n);
    float nl = saturate(dot(n, gSunDir.xyz)) * ShadowAt(i.wp);
    float3 col = alb * (nl * SunLight(i.wp) + SkyLight(n, i.wp));
    if (gSky0.w < 0.5 && !FxActive()) col += alb * 0.05 * smoothstep(-0.05, 0.1, dot(n, gSunDir.xyz));   // seen from space: a soft terminator
    return float4(ApplyHaze(col, i.wp), 1);
}
