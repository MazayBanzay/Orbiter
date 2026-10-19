// D3D11Client: the view from inside a liquid (Liquid.cpp RenderUnderwater). Full screen after the scene: along each view
// ray up to the scene's depth or the surface above, Beer-Lambert per channel and the liquid's own light scattered in
// (out = in-scattered + scene * transmittance, dual-source blending).
#include "common.hlsli"
cbuffer Under : register(b1)
{
    float4 uRow0, uRow1, uRow2;   // camera -> global rotation rows
    float4 uTan;      // tan half-fov x, y; near plane; 0
    float4 uC;        // the planet's centre from the camera; w: the level's radius
    float4 uCol;      // the liquid's in-scattered light; w: extinction [1/m]
    float4 uWts;      // channel weights
};
Texture2D<float> tDepth : register(t0);
struct VSOut { float4 pos : SV_Position; float2 ndc : NDC; };
VSOut VS(uint id : SV_VertexID)
{
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o; o.ndc = p * float2(2, -2) + float2(-1, 1); o.pos = float4(o.ndc, 0, 1);
    return o;
}
struct PSOut { float4 add : SV_Target0; float4 mul : SV_Target1; };
PSOut PS(VSOut i)
{
    float3 rv = float3(i.ndc.x * uTan.x, i.ndc.y * uTan.y, 1);
    rv = float3(dot(uRow0.xyz, rv), dot(uRow1.xyz, rv), dot(uRow2.xyz, rv));
    float rl = length(rv); float3 d = rv / rl;
    float dz = tDepth.Load(int3(i.pos.xy, 0));
    float t = dz > 0 ? rl * uTan.z / dz : 1e6;
    // the surface above: where the ray leaves the liquid's sphere (the camera is inside it)
    float b = dot(d, uC.xyz), disc = b * b - (dot(uC.xyz, uC.xyz) - uC.w * uC.w);
    if (disc > 0) { float tOut = b + sqrt(disc); if (tOut > 0) t = min(t, tOut); }
    float3 tr = exp(-uCol.w * uWts.rgb * t);
    PSOut o;
    o.add = float4(uCol.rgb * (1 - tr), 0);
    o.mul = float4(tr, 1);
    return o;
}
