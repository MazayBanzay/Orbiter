// D3D11Client: the glow of hot metal (src\Exhaust.cpp). For each hot node (AtmoFX_SetHeatSource: a point, the hot body's
// radius, its temperature over the air) a box round it is drawn; each pixel takes the scene's own surface point from the
// depth, its temperature from its distance to the node (the hot body at full dT, cooling over about one radius beyond),
// and adds its thermal emission: emissivity x Planck's B(T) in the scene's units (relative to the Sun's, so a dull red at
// 900 K, orange at 1300 K, yellow-white near 1800 K; nothing below ~700 K). Additive, the mesh shader untouched.
#include "exhaust_common.hlsli"

cbuffer Glow : register(b5)
{
    float4 gNode[96];      // per node: (centre from the camera, radius) (T air, dT, emissivity, 0) (jet axis, 1 = a nozzle)
    float4 gGlow;          // x kE (pi / the Sun's solid angle here), y nodes
    float4x4 gVP;          // camera-centred global -> clip
};

struct VSOut
{
    float4 pos : SV_Position;
    float3 wp : TEXCOORD0;
    nointerpolation float3 n : NORMAL;
    nointerpolation uint node : NODE;
};

VSOut VS(uint vid : SV_VertexID, uint inst : SV_InstanceID)
{
    VSOut o;
    float3 n; float3 c = ExCube(vid, n);
    float4 nd = gNode[3 * inst];
    o.wp = nd.xyz + c * nd.w * (gNode[3 * inst + 2].w > 0.5 ? 3.5 : 2.0);
    o.pos = mul(float4(o.wp, 1), gVP);
    o.n = n; o.node = inst;
    return o;
}

float4 PS(VSOut i) : SV_Target
{
    float3 sp; float sd = ExSceneDist(i.pos.xy, sp);
    // the surface's normal from the depth (before any discard: the derivatives need the whole quad)
    float3 sn = cross(ddx(sp), ddy(sp));
    sn = dot(sn, sn) > 1e-20 ? normalize(sn) : 0;
    if (dot(sn, sp) > 0) sn = -sn;                       // towards the camera
    if (dot(i.n, i.wp) < 0) discard;     // a face turned towards the camera (the camera is at the origin)
    if (sd > 1e29) discard;
    float4 nd = gNode[3 * i.node], th = gNode[3 * i.node + 1], ax = gNode[3 * i.node + 2];
    float T;
    if (ax.w > 0.5) {
        // a nozzle: what the gas touches - the bell's inner wall (from the exit plane up into it, its normal towards the
        // axis) and the central spike standing out of the exit along the axis. The housing's outer wall, though as wide
        // as the exit at the lip, faces away from the axis and stays cool
        float3 v = sp - nd.xyz;
        float z = -dot(v, ax.xyz);                        // into the nozzle from the exit plane (< 0: out along the jet)
        float3 rv = v + ax.xyz * z;
        float r = length(rv);
        if (z < -2 * nd.w || z > 3 * nd.w || r > nd.w * 1.02) discard;
        float facing = r > 1e-5 ? dot(sn, rv / r) : 0;  // < 0: the surface looks at the axis (the bell's inside)
        bool spike = z < 0.2 * nd.w && r < 0.75 * nd.w;  // the plug in the jet: everything near the axis at and past the exit
        bool bell = z >= -0.1 * nd.w && facing < -0.15;
        if (!spike && !bell) discard;
        T = th.x + th.y * (spike ? 1.0 : lerp(0.8, 1.0, saturate(z / (2 * nd.w))));
    } else {
        float d = length(sp - nd.xyz) / nd.w;
        if (d > 1.15) discard;
        // a hot part: full dT, falling off sharply at its edge (thin walls conduct little)
        T = th.x + th.y * (1 - smoothstep(0.8, 1.15, d));
    }
    float3 L = gGlow.x * th.z * ExBlackbodyRel(T);
    return float4(L * ExAirTrans(sd), 0);
}
