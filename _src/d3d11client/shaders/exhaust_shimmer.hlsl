// D3D11Client: heat shimmer (src\Exhaust.cpp), before the tone mapping. Hot gas has a lower density, hence a lower
// refractive index: delta n = K rho_air dT / (T_air + dT) (K = (n - 1) / rho ~ 2.3e-4 m3/kg for N2 / CO2 / air). Its
// turbulent eddies (size ~0.3 of the hot region's width w) bend the rays seen through it by ~ delta n sqrt(L / l) ~ 5 delta n
// (rad); on screen the background behind moves by that angle x the focal length in pixels x (1 - d_hot / d_background):
// nothing for what lies in front of the hot gas, nothing in vacuum.
// Pass 1 (VSProxy/PSProxy): camera-facing cylindrical quads along the hot columns (a hot node's buoyant plume, a jet's hot
// gas) add their offsets (uv) into an R16G16 target. Pass 2 (VSFull/PSComposite): the scene copy read at the offset uv.
#include "exhaust_common.hlsli"

cbuffer Shimmer : register(b5)
{
    float4 sCol[96];       // per column: (start from the camera, half-width there) (end, half-width) (angle amplitude [rad], speed [m/s], seed, kind)
    float4 sMisc;          // x focal length [px], y time, z 1/width, w 1/height (the scene's)
    float4x4 sVP;
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;       // across (0..1), along (0..1)
    float3 wp : TEXCOORD1;
    nointerpolation uint col : COLUMN;
};

VSOut VSProxy(uint vid : SV_VertexID, uint inst : SV_InstanceID)
{
    VSOut o;
    float4 a = sCol[3 * inst], b = sCol[3 * inst + 1];
    static const float2 corner[6] = { float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1) };
    float2 c = corner[vid];
    float3 ax = b.xyz - a.xyz;
    float3 mid = 0.5 * (a.xyz + b.xyz);
    float3 side = cross(ax, mid);
    side = length(side) > 1e-9 ? normalize(side) : normalize(cross(ax, float3(0.31, 0.93, 0.17)));
    float3 base = lerp(a.xyz, b.xyz, c.y);
    float w = lerp(a.w, b.w, c.y);
    o.wp = base + side * w * (c.x * 2 - 1);
    o.pos = mul(float4(o.wp, 1), sVP);
    o.uv = c;
    o.col = inst;
    return o;
}

float4 PSProxy(VSOut i) : SV_Target
{
    float4 a = sCol[3 * i.col], b = sCol[3 * i.col + 1], m = sCol[3 * i.col + 2];
    float u = i.uv.x, v = i.uv.y;
    float across = pow(1 - (2 * u - 1) * (2 * u - 1), 2);
    float along = m.w < 0.5 ? smoothstep(0, 0.08, v) * (1 - smoothstep(0.45, 1, v))     // a jet: from the exit, fading down it
                            : smoothstep(0, 0.15, v) * (1 - smoothstep(0.35, 1, v));    // a hot node's rising column
    float mask = across * along;
    if (mask < 0.003) discard;
    float3 sp; float sd = ExSceneDist(i.pos.xy, sp);
    float dp = length(i.wp);
    if (sd <= dp) discard;                         // the scene is in front of the hot gas
    float k = 1 - dp / sd;
    // eddies of ~0.3 w carried along the column at its speed
    float len = length(b.xyz - a.xyz), w = 0.5 * (a.w + b.w);
    float s = 1 / max(0.3 * w, 1e-3);
    float t = sMisc.y;
    float3 q = float3((u - 0.5) * 2 * w * s, (v * len - t * m.y) * s, t * 0.35 * m.y * s + m.z * 31);
    float2 nv = float2(ExNoise(q) - 0.5, ExNoise(q + float3(17.3, 5.1, 9.7)) - 0.5) * 3.4;   // ~unit variance
    float2 offPx = nv * m.x * mask * sMisc.x * k;
    return float4(offPx * sMisc.zw, 0, 0);
}

Texture2D tScene : register(t4);
Texture2D<float2> tOffset : register(t3);

float4 VSFull(uint vid : SV_VertexID) : SV_Position
{
    float2 p = float2((vid << 1) & 2, vid & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 PSComposite(float4 pos : SV_Position) : SV_Target
{
    float2 off = tOffset.Load(int3(pos.xy, 0));
    if (dot(off, off) * (1 / (sMisc.z * sMisc.z)) < 0.0009) discard;     // under ~0.03 px: leave the scene as it is
    float2 uv = pos.xy * sMisc.zw + off;
    return float4(tScene.SampleLevel(sExLin, uv, 0).rgb, 1);
}
