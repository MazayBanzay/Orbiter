// D3D11Client: vessel meshes (Orbiter materials, diffuse texture, the Sun, the sky light, the haze)
#include "common.hlsli"
cbuffer Obj : register(b1)
{
    float4x4 gWVP;
    float4x4 gW;        // to the camera-centred global frame
    float4 gSunM;       // direction to the Sun (global)
    float4 gDiff, gMAmb, gSpec, gEmis;   // spec.w = power
    float4 gFlags;      // x: texture
    float4x4 gCurNJ;    // this frame's WVP without the TAA jitter
    float4x4 gPrevNJ;   // last frame's
};
struct PSOut { float4 c : SV_Target0; float2 mv : SV_Target1; };
Texture2D tDiff : register(t0);
SamplerState sAniso : register(s0);

struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD; };
struct VSOut { float4 pos : SV_Position; float3 wp : WPOS; float3 n : NORMAL; float2 uv : TEXCOORD; float4 cc : CURCLIP; float4 pc : PREVCLIP; };

VSOut VS(VSIn v)
{
    VSOut o;
    o.pos = mul(float4(v.p, 1), gWVP);
    o.wp = mul(float4(v.p, 1), gW).xyz;
    o.n = mul(v.n, (float3x3)gW);
    o.uv = v.uv;
    o.cc = mul(float4(v.p, 1), gCurNJ);
    o.pc = mul(float4(v.p, 1), gPrevNJ);
    return o;
}

PSOut PS(VSOut i, bool front : SV_IsFrontFace)
{
    PSOut o;
    o.mv = (i.cc.xy / i.cc.w - i.pc.xy / i.pc.w) * float2(0.5, -0.5);   // uv now - uv then
    float4 base = gDiff;
    if (gFlags.x > 0) base *= tDiff.Sample(sAniso, i.uv);
    float3 n = normalize(i.n);
    float3 V = normalize(-i.wp);
    float3 L = gSunM.xyz;
    float nl = saturate(dot(n, L)) * ShadowAt(i.wp);
    float3 col = base.rgb * (nl * SunLight(i.wp) + SkyLight(n, i.wp) + 0.02 * gMAmb.rgb) + gEmis.rgb;
    if (gSpec.w > 0) {
        float3 H = normalize(V + L);
        col += gSpec.rgb * pow(saturate(dot(n, H)), max(gSpec.w, 1)) * nl * SunLight(i.wp);
    }
    o.c = float4(ApplyHaze(col, i.wp), base.a);
    return o;
}
