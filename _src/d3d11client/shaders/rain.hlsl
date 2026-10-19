// D3D11Client: rain (Titan's methane rain; Rain.cpp). Every drop is procedural: its place in a box around the camera
// from a hash of its index, falling at its own terminal speed and wrapping round the box, the box locked to the ground
// (the camera's offset taken out on the CPU in double precision) - nothing is stored, nothing swims with the camera.
// Drawn as thin streaks along the fall (the frame's exposure), at least a pixel wide with the opacity scaled to keep the
// drop's own cross-section; lit by the sky they refract and reflect, with the Sun's glint in some; none under the liquid.
#include "common.hlsli"

cbuffer Rain : register(b1)
{
    float4x4 rVP;
    float4 rE1, rUp, rE2;   // the box's axes (camera-centred global); rUp.w: the camera's height over the liquid (1e9: none)
    float4 rOff;            // the camera's place in the box [m] (mod the box), w: the box's side [m]
    float4 rP;              // x time [s], y drops' mean diameter [m], z opacity scale, w exposure [s]
    float4 rDrift;          // the wind's drift (camera-centred global) [m/s], w: pixel angle [rad]
};

float Hash(uint n) { n = (n << 13u) ^ n; n = n * (n * n * 15731u + 789221u) + 1376312589u; return float(n & 0x7fffffffu) / 2147483647.0; }

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; nointerpolation float4 col : COLOR0; };

// terminal speed of a methane drop of diameter d in Titan's air: drag balance with Cd ~0.6 (Re of hundreds), Stokes below
float Vt(float d)
{
    float vN = sqrt(4 * 1.352 * d * (450 - 5.4) / (3 * 0.6 * 5.4));
    float vS = (450 - 5.4) * 1.352 * d * d / (18 * 6.2e-6);
    return min(vN, vS);
}

VSOut VS(uint vid : SV_VertexID, uint inst : SV_InstanceID)
{
    VSOut o = (VSOut)0;
    float B = rOff.w;
    // the drop: its size from an exponential spectrum (Marshall-Palmer shape) with the mean rP.y, at most 9.5 mm
    float u = max(Hash(inst * 7u + 3u), 1e-4);
    float d = min(-log(u) * rP.y, 9.5e-3);
    float v = Vt(d);
    float3 h = float3(Hash(inst * 7u + 1u), Hash(inst * 7u + 2u), Hash(inst * 7u + 5u));
    float3 q = h * B - rOff.xyz;                                   // in the box, ground-locked
    q.y -= v * rP.x;                                               // falling
    q = q - B * floor(q / B) - 0.5 * B;                            // wrapped round the camera
    float3 wp = rE1.xyz * q.x + rUp.xyz * q.y + rE2.xyz * q.z + rDrift.xyz * (frac(h.x * 13.1) * 0.0);
    float dist = length(wp);
    // none under the liquid's surface (its pass writes no depth), none at the camera
    if (q.y + rUp.w < 0 || dist < 0.4) { o.pos = float4(0, 0, -1, 1); return o; }
    float3 vel = -rUp.xyz * v + rDrift.xyz;
    float3 V = wp / dist;
    float3 vp = vel - dot(vel, V) * V;
    float L = length(vp) * rP.w;                                   // the streak over the exposure
    float w = max(0.5 * d, dist * rDrift.w * 0.7);                 // at least ~a pixel
    float3 ax = length(vp) > 1e-5 ? vp / length(vp) : -rUp.xyz;
    float3 side = normalize(cross(V, ax));
    static const float2 corner[6] = { float2(-1, -1), float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(1, -1) };
    float2 c = corner[vid];
    float hl = 0.5 * L + w;
    float3 p = wp - vel * (0.5 * rP.w) + ax * (c.y * hl) + side * (c.x * w);
    o.pos = mul(float4(p, 1), rVP);
    o.uv = c;
    // opacity: the drop's own cross-section spread over the sprite (d^2 over w * the streak's length), fading far off
    // (a drop's own cross-section over the sprite would be ~1 %: invisible, as single drops are to the eye; rain is seen
    // as the field of streaks - 10-30 % each, by size)
    float a = rP.z * (0.18 + 0.3 * saturate(d / 3e-3)) * saturate((40 - dist) / 15) * saturate((dist - 0.4) / 0.6);
    // the light: the sky round it (a clear drop shows the sky refracted and reflected), the Sun's glint in some
    float3 up = rUp.xyz;
    float3 sky = SkyLight(up, wp) * 0.9 + SkyLight(-up, wp) * 0.3;
    float glint = step(0.93, Hash(inst * 7u + 6u)) * saturate(dot(-V, gSunDir.xyz) * 0.5 + 0.5);
    o.col = float4(sky + SunLight(wp) * ShadowAt(wp) * glint * 0.8, a);
    return o;
}

float4 PS(VSOut i) : SV_Target
{
    float r = i.uv.x * i.uv.x;
    float a = i.col.a * saturate(1 - r) * saturate((1 - abs(i.uv.y)) * 4);
    if (a < 0.001) discard;
    return float4(i.col.rgb * a, a);
}
