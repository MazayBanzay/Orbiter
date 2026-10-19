// D3D11Client: the sky over the liquid as a small panorama (Liquid.cpp), once a frame: the FX atmosphere's radiance for
// every direction of the upper hemisphere round the local up, elevation on a square-root scale (fine at the horizon,
// where a mirror shows most). The liquid's mirror looks it up instead of marching the air per pixel.
#include "common.hlsli"
#include "fx_atmo_sky.hlsli"

cbuffer Pano : register(b1)
{
    float4 pUp, pE1, pE2;     // the local up and the horizontal axes (camera-centred global)
};

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint vid : SV_VertexID)
{
    VSOut o;
    float2 c = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(c * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = c;
    return o;
}

float4 PS(VSOut i) : SV_Target
{
    float az = (i.uv.x - 0.5) * 6.2831853;
    float el = max(i.uv.y * i.uv.y * 1.5707963, 0.004);
    float3 d = pUp.xyz * sin(el) + (pE1.xyz * cos(az) + pE2.xyz * sin(az)) * cos(el);
    float3 L = FxActive() ? FxSkyRadiance(float3(0, 0, 0), d, 12) : SkyColour(d);
    return float4(L, 1);
}
