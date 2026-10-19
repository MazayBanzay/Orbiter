// D3D11Client: a texture over the whole target (the 2D layer, readbacks)
Texture2D tSrc : register(t0);
SamplerState sClamp : register(s1);
struct VSOut { float4 pos : SV_Position; float2 uv : UV; };
VSOut VS(uint id : SV_VertexID)
{
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o; o.uv = p; o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 PS(VSOut i) : SV_Target { return tSrc.SampleLevel(sClamp, i.uv, 0); }
