// D3D11Client: the Sun shadows' static casters kept (Client.cpp RenderShadows). One triangle over the tile's viewport:
// PSCopy writes the cached depth of what stands still into the shadow atlas (the people and what moves are drawn over
// it), PSClear clears one tile of the static atlas (a depth view cannot be cleared by a rectangle in D3D11.0)

Texture2D<float> tStat : register(t0);

float4 VS(uint id : SV_VertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}

float PSCopy(float4 pos : SV_Position) : SV_Depth
{
    return tStat.Load(int3(pos.xy, 0));
}

float PSClear(float4 pos : SV_Position) : SV_Depth
{
    return 1.0;
}
