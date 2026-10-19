// D3D11Client: the MSAA resolve of the HDR scene (AA.cpp FinishScene). A plain average in linear HDR leaves the bright
// edges stepped (one sample of the sky at 20x the ground outweighs the rest: after the tone curve the pixel is as bright
// as an unsmoothed one). Each sample is weighted by 1 / (1 + its brightness on screen) (Karis), so the average is taken
// as the eye sees it after the tone mapping: the edges come out as smooth as MSAA can make them.
#include "common.hlsli"
Texture2DMS<float4> tMS : register(t0);

struct VSOut { float4 pos : SV_Position; float2 uv : UV; };

float4 PS(VSOut i) : SV_Target
{
    uint w, h, n; tMS.GetDimensions(w, h, n);
    float3 e = gHazeRGB.w * gAdapt.rgb;            // the exposure the post pass applies
    float3 sum = 0; float ws = 0;
    for (uint k = 0; k < n; k++) {
        float3 c = tMS.Load(int2(i.pos.xy), k).rgb;
        if (NotFinite(c)) c = 0; c = max(c, 0);
        float3 s = c * e;
        float wgt = 1 / (1 + max(s.r, max(s.g, s.b)));
        sum += c * wgt; ws += wgt;
    }
    return float4(sum / max(ws, 1e-6), 1);
}
