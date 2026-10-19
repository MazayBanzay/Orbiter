// D3D11Client FX atmosphere: the look-up tables of one planet, computed once (Hillaire 2020, "A Scalable and Production
// Ready Sky and Atmosphere Rendering Technique"; Bruneton 2017 for the transmittance parametrisation):
//   CSTrans  256x64  transmittance from (r, mu) to the top of the air
//   CSMS      32x32  multiple scattering: the 2nd and higher orders as an isotropic source per unit scattering, (r, muS)
//   CSIrr     64x16  sky irradiance on a horizontal surface, (r, muS) (single + multiple scattering)
// Frame of these kernels: planet centre at the origin, up = +y, the Sun in the x-y plane.
#include "fx_atmo_light.hlsli"

Texture2D tMS : register(t1);
RWTexture2D<float4> uOut : register(u0);

static const float FX_PI = 3.14159265;
static const int MS_N = 64;

float3 FxRayleigh(float h) { return mRay.rgb * FxRayDensity(h); }
float3 FxMieScatter(float h) { return mMieSca.rgb * FxMieDensity(h); }

float PhaseRayleigh(float c) { return 3.0 / (16.0 * FX_PI) * (1 + c * c); }
float3 PhaseMie(float c)   // Cornette-Shanks, per channel g
{
    float3 g = mMieG.rgb, g2 = g * g;
    return 3.0 / (8.0 * FX_PI) * (1 - g2) * (1 + c * c) / ((2 + g2) * pow(max(1 + g2 - 2 * g * c, 1e-4), 1.5));
}

float3 MSLookup(float r, float muS)
{
    return FxBilinear(tMS, float2((muS * 0.5 + 0.5) * (MS_N - 1), FxAltX(r) * (MS_N - 1)), int2(0, 0), int2(MS_N, MS_N)).rgb;
}

// distance along d from p to the top of the air, and to the ground (-1: misses it)
void Bounds(float3 p, float3 d, out float tTop, out float tGround)
{
    float R = mCentre.w, top = mGeo.x;
    float b = dot(p, d), c = dot(p, p);
    tTop = -b + sqrt(max(b * b - (c - top * top), 0));
    tGround = -1;
    float dg = b * b - (c - R * R);
    if (dg >= 0) { float t = -b - sqrt(dg); if (t >= 0) tGround = t; }
}

[numthreads(8, 8, 1)]
void CSTrans(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)FX_TW || id.y >= (uint)FX_TH) return;
    float R = mCentre.w, top = mGeo.x, Hh = mGeo.y;
    float xm = id.x / (float)(FX_TW - 1), xr = id.y / (float)(FX_TH - 1);
    float rho = Hh * xr, r = sqrt(rho * rho + R * R);
    float dmin = top - r, dmax = rho + Hh, d = dmin + xm * (dmax - dmin);
    float mu = d <= 0 ? 1 : clamp((Hh * Hh - rho * rho - d * d) / (2 * r * d), -1, 1);
    const int N = 600;
    float dt = d / N;
    float3 tau = 0;
    for (int i = 0; i < N; i++) {
        float t = (i + 0.5) * dt;
        float rr = sqrt(r * r + 2 * r * mu * t + t * t);
        tau += FxExtinction(rr - R) * dt;
    }
    uOut[id.xy] = float4(exp(-tau), 1);
}

// direction i of n on the sphere (spherical Fibonacci), up = +y
float3 SphereDir(int i, int n)
{
    float z = 1 - (2 * i + 1) / (float)n, s = sqrt(max(1 - z * z, 0)), ph = i * 2.3999632;
    return float3(s * cos(ph), z, s * sin(ph));
}

// Light that diffuses down through a thick column (clouds, thick haze) - what the series of isotropic orders cannot carry
// (Venus's clouds, tau ~30). Two-stream estimate: transport optical depth tau' = sum tau_i (1 - ssa_i g_i) (Rayleigh
// g = 0), absorption tau_a; total transmission ~ exp(-sqrt(3 tau_a tau')) / (1 + 0.75 tau'). Only where the column above
// is thick (tau' > 1): thin air (Earth) keeps the plain model. Returns the flux on a horizontal surface (Sun = 1).
float3 DiffuseBelow(float r, float muS)
{
    float R = mCentre.w, top = mGeo.x;
    const int N = 64;
    float dh = (top - r) / N;
    float3 tp = 0, ta = 0;
    for (int i = 0; i < N; i++) {
        float h = r - R + (i + 0.5) * dh;
        float3 mie = mMieExt.rgb * FxMieDensity(h), sca = FxMieScatter(h);
        tp += (FxRayleigh(h) + mie - sca * mMieG.rgb) * dh;
        ta += (mie - sca + mAbs.rgb * FxAbsDensity(h)) * dh;
    }
    float3 Tt = exp(-sqrt(3 * max(ta, 0) * tp)) / (1 + 0.75 * tp);
    float3 w = saturate((tp - 1) / 3);
    return w * Tt * saturate((muS + 0.1) / 1.1);
}

groupshared float3 gsL[64], gsF[64];

[numthreads(64, 1, 1)]
void CSMS(uint3 g : SV_GroupID, uint t : SV_GroupIndex)
{
    float R = mCentre.w, top = mGeo.x;
    float muS = g.x / (float)(MS_N - 1) * 2 - 1;
    float r = FxAltR(g.y / (float)(MS_N - 1));
    r = clamp(r, R + 1, top - 1);
    float3 sunD = float3(sqrt(max(1 - muS * muS, 0)), muS, 0);
    float3 p0 = float3(0, r, 0);
    float3 d = SphereDir(t, 64);
    float tTop, tG; Bounds(p0, d, tTop, tG);
    float tMax = tG >= 0 ? tG : tTop;
    const int N = 40;
    float3 T = 1, L = 0, F = 0;
    float prev = 0;
    for (int i = 0; i < N; i++) {
        float t1 = tMax * (i + 1) / N, dt = t1 - prev, tm = prev + 0.5 * dt; prev = t1;
        float3 p = p0 + d * tm;
        float rr = length(p), h = rr - R;
        float3 ss = FxRayleigh(h) + FxMieScatter(h), st = max(FxExtinction(h), 1e-12);
        float3 a = exp(-st * dt);
        float3 S = FxTransToSun(rr, dot(p, sunD) / rr) * ss / (4 * FX_PI);
        L += T * (S - S * a) / st;
        F += T * (ss - ss * a) / st;
        T *= a;
    }
    if (tG >= 0) {   // the ground's diffuse reflection of the Sun
        float3 p = p0 + d * tG;
        float mu = dot(normalize(p), sunD);
        L += T * FxTransToSun(R, mu) * saturate(mu) * mGround.rgb / FX_PI;
    }
    gsL[t] = L; gsF[t] = F;
    GroupMemoryBarrierWithGroupSync();
    for (uint s = 32; s > 0; s >>= 1) {
        if (t < s) { gsL[t] += gsL[t + s]; gsF[t] += gsF[t + s]; }
        GroupMemoryBarrierWithGroupSync();
    }
    if (t == 0) {
        float3 L2 = gsL[0] / 64, f = min(gsF[0] / 64, 0.99);   // the isotropic phase and the solid angle: a mean
        // all orders: a geometric series; under a thick column the diffused light as an isotropic field (radiance E / pi)
        uOut[g.xy] = float4(L2 / (1 - f) + DiffuseBelow(r, muS) / FX_PI, 1);
    }
}

// the sky radiance (physical, Sun = 1) along d from p0 up to the top or the ground
float3 SkyRadiance(float3 p0, float3 d, float3 sunD, int N)
{
    float R = mCentre.w;
    float tTop, tG; Bounds(p0, d, tTop, tG);
    float tMax = tG >= 0 ? tG : tTop;
    float c = dot(d, sunD);
    float pr = PhaseRayleigh(c); float3 pm = PhaseMie(c);
    float3 T = 1, L = 0;
    float prev = 0;
    for (int i = 0; i < N; i++) {
        float u1 = (i + 1) / (float)N;
        float t1 = tMax * u1 * u1, dt = t1 - prev, tm = prev + 0.5 * dt; prev = t1;
        float3 p = p0 + d * tm;
        float rr = length(p), h = rr - R;
        float muS = dot(p, sunD) / rr;
        float3 sr = FxRayleigh(h), sm = FxMieScatter(h), st = max(FxExtinction(h), 1e-12);
        float3 Ts = FxTransToSun(rr, muS), ms = MSLookup(rr, muS);
        float3 S = sr * (Ts * pr + ms) + sm * (Ts * pm + ms);
        float3 a = exp(-st * dt);
        L += T * (S - S * a) / st;
        T *= a;
    }
    return L;
}

[numthreads(64, 1, 1)]
void CSIrr(uint3 g : SV_GroupID, uint t : SV_GroupIndex)
{
    float R = mCentre.w, top = mGeo.x;
    float muS = g.x / (float)(FX_IW - 1) * 2 - 1;
    float r = clamp(FxAltR(g.y / (float)(FX_IH - 1)), R + 1, top - 1);
    float3 sunD = float3(sqrt(max(1 - muS * muS, 0)), muS, 0);
    // cosine-weighted directions over the upper hemisphere: E = pi * mean(L)
    float u = (t + 0.5) / 64.0, ph = t * 2.3999632, s = sqrt(u);
    float3 d = float3(s * cos(ph), sqrt(1 - u), s * sin(ph));
    gsL[t] = SkyRadiance(float3(0, r, 0), d, sunD, 32);
    GroupMemoryBarrierWithGroupSync();
    for (uint k = 32; k > 0; k >>= 1) {
        if (t < k) gsL[t] += gsL[t + k];
        GroupMemoryBarrierWithGroupSync();
    }
    if (t == 0) uOut[g.xy] = float4(FX_PI * gsL[0] / 64, 1);
}
