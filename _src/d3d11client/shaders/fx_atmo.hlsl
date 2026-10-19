// D3D11Client FX atmosphere: the sky, the aerial perspective and the planet's air seen from space, one full-screen pass
// per planet after the opaque scene (dual-source blending: out = in-scattered light + scene * transmittance).
// Along each view ray up to the scene depth (fx_atmo_sky.hlsli): Rayleigh and aerosol single scattering of the Sun, all
// higher orders from the multiple-scattering table; the ground where no tile is drawn; the Sun's disc.
// Model and data: src\FxAtmo.cpp, Config\D3D11Client\<planet>.atmo.cfg. This planet's medium and tables: b4, t8, t9.
#include "common.hlsli"
#include "fx_atmo_sky.hlsli"

cbuffer AtmoPass : register(b3)
{
    float4 aRow0, aRow1, aRow2;   // camera -> global rotation rows
    float4 aTan;      // tan half-fov x, y; near plane; 0
    float4 aCam;      // camera distance from the centre, camera altitude, 0, 0
    float4 aSun;      // x: the Sun's disc on screen (1); y: cap of its radiance; zw: the projection's offset (TAA jitter)
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
    PSOut o; o.add = 0; o.mul = 1;
    float3 rv = float3((i.ndc.x - aSun.z) * aTan.x, (i.ndc.y - aSun.w) * aTan.y, 1);
    rv = float3(dot(aRow0.xyz, rv), dot(aRow1.xyz, rv), dot(aRow2.xyz, rv));
    float rl = length(rv);
    float3 d = rv / rl;
    float3 org = -mCentre.xyz;                       // the camera from the planet centre
    float R = mCentre.w, Rt = mGeo.x, alt = aCam.y;
    float tc0 = -dot(org, d);
    float3 cx = cross(org, d);
    float K = dot(cx, cx) - R * R;                   // r(t)^2 - R^2 = K + (t - tc0)^2
    float disT = (Rt - R) * (Rt + R) - K;
    if (disT <= 0) discard;
    float sq = sqrt(disT);
    float t0 = max(tc0 - sq, 0.0), t1 = tc0 + sq;
    if (t1 <= 0) discard;
    float dz = tDepth.Load(int3(i.pos.xy, 0));
    float tS = dz > 0 ? rl * aTan.z / dz : 1e30;     // reverse Z, infinite far plane: depth = near / view z
    // the liquid's surface (it writes no depth: the vessels' parts below it stay visible through it): an end of the ray
    // like the scene's - the air up to it, the liquid's colour behind (never the ground fill below over it)
    if (gLiq2.w > 0.5) {
        float3 cL = gLiq0.xyz; float bL = dot(d, cL), discL = bL * bL - (dot(cL, cL) - gLiq0.w * gLiq0.w);
        if (discL > 0) { float tL = bL - sqrt(discL); if (tL > 0) tS = min(tS, tL); }
    }
    float tG = 1e30;                                 // inside the air: the ground (R - 1 km) where no tile is drawn
    if (alt < Rt - R) {
        float disG = -K - 1000 * (2 * R - 1000);
        if (disG > 0) { float t = tc0 - sqrt(disG); if (t > 0) tG = t; }
    }
    float tO = 1e30;                                 // from above: the altitude below which the air is opaque (Venus's clouds)
    if (mRay2.z > 0 && alt > mRay2.z) {
        float disO = mRay2.z * (2 * R + mRay2.z) - K;
        if (disO > 0) { float t = tc0 - sqrt(disO); if (t > t0) tO = t; }
    }
    bool opaque = tO < min(t1, min(tS, tG));
    bool scene = !opaque && tS < min(t1, tG);
    bool ground = !opaque && !scene && tG < t1;
    t1 = min(min(t1, tO), min(tS, tG));
    if (t1 <= t0) discard;

    float3 L, T, fwd;
    FxMarch(org, d, tc0, K, t0, t1, 48, L, T, fwd);
    if (ground) {   // beyond the drawn tiles: the ground lit by the Sun and the sky
        float q = K + (t1 - tc0) * (t1 - tc0);
        float r = sqrt(max(R * R + q, 1));
        float muS = dot(org + d * t1, mSunDir.xyz) / r;
        L += T * mGround.rgb / FX_PI * (FxTransToSun(r, muS) * saturate(muS) + FxIrradiance(r, muS));
        T = 0;
    }
    if (opaque) T = 0;
    float3 Ti = T * exp(min(fwd, 40));               // the aerosol's forward peak passes the image (delta scaling)
    L *= FX_PI;                                      // on screen: pi * radiance (a white surface under the Sun = 1)
    if (!scene && !ground && !opaque && aSun.x > 0.5) {         // the Sun's disc, limb-darkened, through the air
        float ar = mGeo.w, c = dot(d, mSunDir.xyz);
        float s2 = max(1 - c * c, 0);
        if (c > 0 && s2 < ar * ar) {
            float m = sqrt(1 - s2 / (ar * ar));
            L += min(T * (1 / (ar * ar)) * (0.4 + 0.6 * m), aSun.y);
        }
    }
    o.add = float4(L, 0);
    o.mul = float4(min(Ti, 1), 1);
    return o;
}
