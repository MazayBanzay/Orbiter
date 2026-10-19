// D3D11Client: the stars (unit directions in the global frame), 2 x 2 pixel quads, brightness from the catalogue
cbuffer Stars : register(b1)
{
    float4x4 gSVP;
    float4 gSMisc;     // 2/W, 2/H, visibility
};
struct Star { float3 dir; float b; float3 col; float pad; };
StructuredBuffer<Star> tStars : register(t0);
struct VSOut { float4 pos : SV_Position; float3 c : COLOR; float2 q : QUAD; };
VSOut VS(uint vid : SV_VertexID)
{
    static const float2 C[6] = { float2(-1, -1), float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(1, -1) };
    Star s = tStars[vid / 6];
    float2 q = C[vid % 6];
    float4 p = mul(float4(s.dir * 1e6, 1), gSVP);
    p.xy += q * gSMisc.xy * 1.2 * p.w;
    p.z = 0;                                    // at infinity (reverse Z)
    VSOut o; o.pos = p; o.q = q;
    o.c = s.col * saturate(s.b * 1.4) * gSMisc.z;
    return o;
}
float4 PS(VSOut i) : SV_Target
{
    float f = saturate(1 - dot(i.q, i.q));
    return float4(i.c * f, f);
}
