// D3D11Client FX atmosphere: the sky, the aerial perspective and the planet's air seen from space, one full-screen pass
// per planet after the opaque scene (dual-source blending: out = in-scattered light + scene * transmittance).
// Along each view ray up to the scene depth (fx_atmo_sky.hlsli): Rayleigh and aerosol single scattering of the Sun, all
// higher orders from the multiple-scattering table; the ground where no tile is drawn; the Sun's disc.
// Inside the air (the camera's planet) the rays are marched once a frame per screen column in CSVol (Hillaire 2020's
// aerial-perspective volume: one column per 12 x 12 pixels, 64 slices along the ray, cubic towards the camera) and the
// pixel takes the column at its scene distance (one lookup instead of 48 steps). From space the pixel marches itself.
// Model and data: src\FxAtmo.cpp, Config\D3D11Client\<planet>.atmo.cfg. This planet's medium and tables: b4, t8, t9.
#include "common.hlsli"
#include "fx_atmo_sky.hlsli"

cbuffer AtmoPass : register(b3)
{
    float4 aRow0, aRow1, aRow2;   // camera -> global rotation rows
    float4 aTan;      // tan half-fov x, y; near plane; 0
    float4 aCam;      // camera distance from the centre, camera altitude, 0, 0
    float4 aSun;      // x: the Sun's disc on screen (1); y: cap of its radiance; zw: the projection's offset (TAA jitter)
    float4 aVol;      // x: 1 = the aerial-perspective volume is used; y, z: 1 / the scene's size in pixels; w: slices
    float4 aGrid;     // the volume's columns, rows, slices
};
Texture2D<float> tDepth : register(t0);
Texture3D tApL : register(t1);      // the volume: in-scattered light up to each slice (on screen)
Texture3D tApT : register(t2);      // ... the transmittance (the aerosol's forward peak passed)
Texture2D tEndL : register(t3);     // each column's whole ray (to the top, the ground fill or the opaque layer)
Texture2D tEndT : register(t4);
SamplerState sClamp : register(s1);
RWTexture3D<float4> uApL : register(u0);
RWTexture3D<float4> uApT : register(u1);
RWTexture2D<float4> uEndL : register(u2);
RWTexture2D<float4> uEndT : register(u3);

struct VSOut { float4 pos : SV_Position; float2 ndc : NDC; };
VSOut VS(uint id : SV_VertexID)
{
    float2 p = float2((id << 1) & 2, id & 2);
    VSOut o; o.ndc = p * float2(2, -2) + float2(-1, 1); o.pos = float4(o.ndc, 0, 1);
    return o;
}

// the view ray through ndc and where it meets the air: false when it misses the air
struct AtmoRay { float3 org, d; float rl, tc0, K, t0, t1, tG, tO; };
bool MakeRay(float2 ndc, out AtmoRay r)
{
    float3 rv = float3((ndc.x - aSun.z) * aTan.x, (ndc.y - aSun.w) * aTan.y, 1);
    rv = float3(dot(aRow0.xyz, rv), dot(aRow1.xyz, rv), dot(aRow2.xyz, rv));
    r.rl = length(rv);
    r.d = rv / r.rl;
    r.org = -mCentre.xyz;                            // the camera from the planet centre
    float R = mCentre.w, Rt = mGeo.x, alt = aCam.y;
    r.tc0 = -dot(r.org, r.d);
    float3 cx = cross(r.org, r.d);
    r.K = dot(cx, cx) - R * R;                       // r(t)^2 - R^2 = K + (t - tc0)^2
    r.t0 = 0; r.t1 = 0; r.tG = 1e30; r.tO = 1e30;
    float disT = (Rt - R) * (Rt + R) - r.K;
    if (disT <= 0) return false;
    float sq = sqrt(disT);
    r.t0 = max(r.tc0 - sq, 0.0); r.t1 = r.tc0 + sq;
    if (r.t1 <= 0) return false;
    if (alt < Rt - R) {                              // inside the air: the ground (R - 1 km) where no tile is drawn
        float disG = -r.K - 1000 * (2 * R - 1000);
        if (disG > 0) { float t = r.tc0 - sqrt(disG); if (t > 0) r.tG = t; }
    }
    if (mRay2.z > 0 && alt > mRay2.z) {              // from above: the altitude below which the air is opaque (Venus)
        float disO = mRay2.z * (2 * R + mRay2.z) - r.K;
        if (disO > 0) { float t = r.tc0 - sqrt(disO); if (t > r.t0) r.tO = t; }
    }
    return true;
}

// the ground beyond the drawn tiles, lit by the Sun and the sky, at the ray's end t
float3 GroundFill(AtmoRay r, float t)
{
    float R = mCentre.w;
    float q = r.K + (t - r.tc0) * (t - r.tc0);
    float rr = sqrt(max(R * R + q, 1));
    float muS = dot(r.org + r.d * t, mSunDir.xyz) / rr;
    return mGround.rgb / FX_PI * (FxTransToSun(rr, muS) * saturate(muS) + FxIrradiance(rr, muS));
}

// the Sun's disc, limb-darkened, through the air (T: the transmittance to the top along d)
float3 SunDisc(float3 d, float3 T)
{
    float ar = mGeo.w, c = dot(d, mSunDir.xyz);
    float s2 = max(1 - c * c, 0);
    if (c <= 0 || s2 >= ar * ar) return 0;
    float m = sqrt(1 - s2 / (ar * ar));
    return min(T * (1 / (ar * ar)) * (0.4 + 0.6 * m), aSun.y);
}

// one step of the march (as FxMarch's loop): the air between ta and tb
void Step(AtmoRay r, float ta, float tb, float pr, float3 pm, inout float3 L, inout float3 T, inout float3 fwd)
{
    float R = mCentre.w;
    float tm = 0.5 * (ta + tb), dt = tb - ta;
    float q = r.K + (tm - r.tc0) * (tm - r.tc0);
    float rr = sqrt(max(R * R + q, 1));
    float h = max(q / (rr + R), -2000.0);
    float muS = dot(r.org + r.d * tm, mSunDir.xyz) / rr;
    float3 sr = mRay.rgb * FxRayDensity(h), sm = mMieSca.rgb * FxMieDensity(h);
    float3 st = max(FxExtinction(h), 1e-12);
    float3 Ts = FxTransToSun(rr, muS), ms = FxMSLookup(rr, muS);
    float3 S = sr * (Ts * pr + ms) + sm * (Ts * pm + ms);
    float3 a = exp(-st * dt);
    L += T * (S - S * a) / st;
    T *= a;
    fwd += sm * mMieG.rgb * mMieG.rgb * dt;
}

// the aerial-perspective volume: one thread per column, its ray marched through 64 slices (two steps each), cubic from
// the camera to the ray's end without the scene; the column's whole ray in the end textures
[numthreads(8, 8, 1)]
void CSVol(uint3 id : SV_DispatchThreadID)
{
    const uint W = (uint)aGrid.x, H = (uint)aGrid.y; const int S = (int)aGrid.z;
    if (id.x >= W || id.y >= H) return;
    float2 uv = (id.xy + 0.5) / float2(W, H);
    AtmoRay r;
    if (!MakeRay(float2(uv.x * 2 - 1, 1 - uv.y * 2), r)) {
        for (int k = 0; k < S; k++) { uApL[uint3(id.xy, k)] = 0; uApT[uint3(id.xy, k)] = 1; }
        uEndL[id.xy] = 0; uEndT[id.xy] = 1;
        return;
    }
    const bool opaque = r.tO < min(r.t1, r.tG);
    const bool ground = !opaque && r.tG < r.t1;
    const float tEnd = min(min(r.t1, r.tO), r.tG);
    float cs = dot(r.d, mSunDir.xyz);
    float pr = FxPhaseRayleigh(cs); float3 pm = FxPhaseMie(cs);
    float3 L = 0, T = 1, fwd = 0;
    uApL[uint3(id.xy, 0)] = 0; uApT[uint3(id.xy, 0)] = 1;
    float tPrev = r.t0;
    [loop] for (int k = 1; k < S; k++) {
        float u = (float)k / (S - 1);
        float tb = r.t0 + (tEnd - r.t0) * u * u * u;
        float tmid = 0.5 * (tPrev + tb);
        Step(r, tPrev, tmid, pr, pm, L, T, fwd);
        Step(r, tmid, tb, pr, pm, L, T, fwd);
        uApL[uint3(id.xy, k)] = float4(L * FX_PI, 0);
        uApT[uint3(id.xy, k)] = float4(T * exp(min(fwd, 40)), 0);
        tPrev = tb;
    }
    float3 Le = L;
    if (ground) Le += T * GroundFill(r, tEnd);
    uEndL[id.xy] = float4(Le * FX_PI, 0);
    uEndT[id.xy] = float4((ground || opaque) ? 0 : T * exp(min(fwd, 40)), 0);
}

struct PSOut { float4 add : SV_Target0; float4 mul : SV_Target1; };

PSOut PS(VSOut i)
{
    PSOut o; o.add = 0; o.mul = 1;
    AtmoRay r;
    if (!MakeRay(i.ndc, r)) discard;
    float dz = tDepth.Load(int3(i.pos.xy, 0));
    float tS = dz > 0 ? r.rl * aTan.z / dz : 1e30;   // reverse Z, infinite far plane: depth = near / view z
    // the liquid's surface (it writes no depth): an end of the ray like the scene's
    if (gLiq2.w > 0.5) {
        float3 cL = gLiq0.xyz; float bL = dot(r.d, cL), discL = bL * bL - (dot(cL, cL) - gLiq0.w * gLiq0.w);
        if (discL > 0) { float tL = bL - sqrt(discL); if (tL > 0) tS = min(tS, tL); }
    }
    bool opaque = r.tO < min(r.t1, min(tS, r.tG));
    bool scene = !opaque && tS < min(r.t1, r.tG);
    bool ground = !opaque && !scene && r.tG < r.t1;
    const float tEnd = min(min(r.t1, r.tO), r.tG);
    float t1 = min(tEnd, tS);
    if (t1 <= r.t0) discard;

    if (aVol.x > 0.5) {         // inside the air: the column's march at this distance
        float2 uv = i.pos.xy * aVol.yz;
        if (scene) {
            float u = pow(saturate((tS - r.t0) / max(tEnd - r.t0, 1e-3)), 1.0 / 3.0);
            float3 w = float3(uv, u * (aVol.w - 1) / aVol.w + 0.5 / aVol.w);
            o.add = float4(tApL.SampleLevel(sClamp, w, 0).rgb, 0);
            o.mul = float4(min(tApT.SampleLevel(sClamp, w, 0).rgb, 1), 1);
        } else {
            o.add = float4(tEndL.SampleLevel(sClamp, uv, 0).rgb, 0);
            o.mul = float4(min(tEndT.SampleLevel(sClamp, uv, 0).rgb, 1), 1);
            if (!ground && !opaque && aSun.x > 0.5) {
                float rc = length(r.org);
                o.add.rgb += SunDisc(r.d, FxTrans(rc, dot(r.org, r.d) / rc));
            }
        }
        return o;
    }

    float3 L, T, fwd;
    FxMarch(r.org, r.d, r.tc0, r.K, r.t0, t1, 48, L, T, fwd);
    if (ground) { L += T * GroundFill(r, t1); T = 0; }
    if (opaque) T = 0;
    float3 Ti = T * exp(min(fwd, 40));               // the aerosol's forward peak passes the image (delta scaling)
    L *= FX_PI;                                      // on screen: pi * radiance (a white surface under the Sun = 1)
    if (!scene && !ground && !opaque && aSun.x > 0.5) L += SunDisc(r.d, T);
    o.add = float4(L, 0);
    o.mul = float4(min(Ti, 1), 1);
    return o;
}
