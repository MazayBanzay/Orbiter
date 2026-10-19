// D3D11Client: 2D instrument panels (Orbiter panel meshes in panel pixels, scaled and shifted into the window)
cbuffer Panel : register(b1)
{
    float4 gXf;     // scale x, scale y, shift x, shift y
    float4 gMisc;   // 2/W, 2/H, alpha
};
Texture2D tTex : register(t0);
SamplerState sClamp : register(s1);
struct VSIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; };
VSOut VS(VSIn v)
{
    VSOut o;
    float2 s = v.p.xy * gXf.xy + gXf.zw;            // window pixels
    o.pos = float4(s.x * gMisc.x - 1, 1 - s.y * gMisc.y, 0, 1);
    o.uv = v.uv;
    return o;
}
float4 PS(VSOut i) : SV_Target
{
    float4 c = tTex.Sample(sClamp, i.uv);
    return float4(c.rgb, c.a * gMisc.z);
}
