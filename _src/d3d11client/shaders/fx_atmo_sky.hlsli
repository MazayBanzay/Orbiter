// D3D11Client FX atmosphere: the light the air scatters along a ray (single scattering of the Sun through the
// transmittance table, all higher orders from the multiple-scattering table). Shared by the sky pass (fx_atmo.hlsl) and
// the liquid's reflection (liquid.hlsl). The medium and the tables of the planet the camera is at: b4, t8, t9 (FxFrame).
#ifndef FX_ATMO_SKY
#define FX_ATMO_SKY
#include "fx_atmo_light.hlsli"

Texture2D tFxMS : register(t9);     // multiple scattering per unit scattering, (r, muS), 32 x 32

static const float FX_PI = 3.14159265;
static const int FX_MSN = 64;

float FxPhaseRayleigh(float c) { return 3.0 / (16.0 * FX_PI) * (1 + c * c); }
float3 FxPhaseMie(float c)          // Cornette-Shanks, per channel g
{
    float3 g = mMieG.rgb, g2 = g * g;
    return 3.0 / (8.0 * FX_PI) * (1 - g2) * (1 + c * c) / ((2 + g2) * pow(max(1 + g2 - 2 * g * c, 1e-4), 1.5));
}
float3 FxMSLookup(float r, float muS)
{
    return FxBilinear(tFxMS, float2((muS * 0.5 + 0.5) * (FX_MSN - 1), FxAltX(r) * (FX_MSN - 1)), int2(0, 0), int2(FX_MSN, FX_MSN)).rgb;
}

// The air along d from org (from the planet centre) between t0 and t1, the samples crowded at the ray's lowest point.
// The ray's geometry from its closest approach (tc0 = -dot(org, d), K = |org x d|^2 - R^2: r(t)^2 - R^2 = K + (t - tc0)^2)
// stays well conditioned for a planet a million km away as for a camera a metre above the ground.
// L: in-scattered radiance (physical, the Sun = 1), T: transmittance, fwd: the aerosol's forward-peak optical depth.
void FxMarch(float3 org, float3 d, float tc0, float K, float t0, float t1, int NS, out float3 L, out float3 T, out float3 fwd)
{
    float R = mCentre.w;
    float cs = dot(d, mSunDir.xyz);
    float pr = FxPhaseRayleigh(cs); float3 pm = FxPhaseMie(cs);
    T = 1; L = 0; fwd = 0;
    float tc = clamp(tc0, t0, t1);
    int n1 = tc > t0 ? (t1 > tc ? NS / 2 : NS) : 0;
    int n2 = NS - n1;
    [loop] for (int k = 0; k < NS; k++) {
        float ta, tb;
        if (k < n1) {
            float u0 = 1 - (float)k / n1, u1 = 1 - (float)(k + 1) / n1;
            ta = t0 + (tc - t0) * (1 - u0 * u0); tb = t0 + (tc - t0) * (1 - u1 * u1);
        } else {
            float u0 = (float)(k - n1) / n2, u1 = (float)(k - n1 + 1) / n2;
            ta = tc + (t1 - tc) * u0 * u0; tb = tc + (t1 - tc) * u1 * u1;
        }
        float tm = 0.5 * (ta + tb), dt = tb - ta;
        float q = K + (tm - tc0) * (tm - tc0);
        float r = sqrt(max(R * R + q, 1));
        float h = max(q / (r + R), -2000.0);   // no air lies deeper (Titan's seas -617 m): a ray never samples the planet's inside
        float muS = dot(org + d * tm, mSunDir.xyz) / r;
        float3 sr = mRay.rgb * FxRayDensity(h), sm = mMieSca.rgb * FxMieDensity(h);
        float3 st = max(FxExtinction(h), 1e-12);
        float3 Ts = FxTransToSun(r, muS), ms = FxMSLookup(r, muS);
        float3 S = sr * (Ts * pr + ms) + sm * (Ts * pm + ms);
        float3 a = exp(-st * dt);
        L += T * (S - S * a) / st;
        T *= a;
        fwd += sm * mMieG.rgb * mMieG.rgb * dt;
    }
}

// the sky seen from wp (camera-centred global frame) towards dir, on screen (pi * radiance; a white surface under the
// Sun = 1), the ground below the horizon included; 0 outside the air
float3 FxSkyRadiance(float3 wp, float3 dir, int NS)
{
    float R = mCentre.w, Rt = mGeo.x;
    float3 org = wp - mCentre.xyz;
    // a mirror's ray below the local horizon would meet the surface itself (a steep wave): taken at the horizon
    float3 up = normalize(org);
    float du = dot(dir, up);
    if (du < 0.01) dir = normalize(dir + up * (0.01 - du));
    float tc0 = -dot(org, dir);
    float3 cx = cross(org, dir);
    float K = dot(cx, cx) - R * R;
    float disT = (Rt - R) * (Rt + R) - K;
    if (disT <= 0) return 0;
    float sq = sqrt(disT);
    float t0 = max(tc0 - sq, 0.0), t1 = tc0 + sq;
    if (t1 <= t0) return 0;
    bool ground = false;
    if (-K > 0) { float tg = tc0 - sqrt(-K); if (tg > 0 && tg < t1) { t1 = tg; ground = true; } }
    float3 L, T, fwd;
    FxMarch(org, dir, tc0, K, t0, t1, NS, L, T, fwd);
    if (ground) {
        float r = R;
        float muS = dot(org + dir * t1, mSunDir.xyz) / r;
        L += T * mGround.rgb / FX_PI * (FxTransToSun(r, muS) * saturate(muS) + FxIrradiance(r, muS));
    }
    return L * FX_PI;
}

#endif
