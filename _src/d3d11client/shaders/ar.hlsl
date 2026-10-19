// D3D11Client: the augmented-reality lines in the scene (Ar.cpp): markers and paths as screen-space ribbons of a fixed
// pixel width, anti-aliased, with a soft glow. Drawn twice against the scene's depth: bright where seen, faint where hidden.
// Added light; divided by the exposure so they keep their brightness on screen whatever the eye's adaptation.
#include "common.hlsli"
cbuffer Ar : register(b1)
{
    float4x4 gVPa;       // camera-centred global -> clip (this frame's)
    float4 gScr;         // xy: the scene's size in pixels, z: glow width px, w: brightness (visible 0.9 / hidden 0.22)
};
struct VIn { float3 p : POSITION; float3 o : OTHER; float side : SIDE; float end : END; float hw : HALFW; float4 col : COLOR; };
struct VOut { float4 pos : SV_Position; float t : DIST; float hw : HALFW; float4 col : COLOR; };

VOut VS(VIn v)
{
    VOut o;
    float4 c = mul(float4(v.p, 1), gVPa), d = mul(float4(v.o, 1), gVPa);
    float2 sc = c.xy / c.w * gScr.xy * 0.5, sd = d.xy / d.w * gScr.xy * 0.5;
    float2 dir = sd - sc; float L = length(dir);
    dir = L > 1e-4 ? dir / L : float2(1, 0);
    float2 n = float2(-dir.y, dir.x);
    float ext = v.hw + gScr.z + 1;                  // the ribbon's half extent: the line, its glow, a pixel for the edge
    float2 off = n * v.side * ext - dir * ext;      // this end pushed outwards (round-ish caps)
    o.pos = c + float4(off / (gScr.xy * 0.5) * c.w, 0, 0);
    o.t = v.side * ext; o.hw = v.hw; o.col = v.col;
    return o;
}

float4 PS(VOut i) : SV_Target
{
    float d = abs(i.t);
    float core = saturate(i.hw + 0.5 - d);
    float glow = gScr.z > 0 ? 0.35 * exp(-3 * saturate((d - i.hw) / gScr.z)) * saturate(1 - (d - i.hw) / gScr.z) : 0;
    float k = max(core, glow) * gScr.w / max(gHazeRGB.w, 1e-4);
    return float4(i.col.rgb * k, 1);
}
