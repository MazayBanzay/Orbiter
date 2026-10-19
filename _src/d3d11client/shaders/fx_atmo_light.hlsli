// D3D11Client FX atmosphere: the medium of one planet and the light at a point (sunlight through the air, skylight).
// Owned by the FX work (src\FxAtmo.cpp). Included by the FX atmosphere shaders and, through common.hlsli, by the
// client's shaders: FxActive() is true only when the client lets the FX atmosphere light the scene.
// Units: the Sun's irradiance outside the air = 1 (the client's white Sun); radiances on screen = pi * physical.
#ifndef FX_ATMO_LIGHT
#define FX_ATMO_LIGHT

cbuffer FxAtmoMedia : register(b4)
{
    float4 mCentre;   // planet centre from the camera [m]; w: bottom radius R
    float4 mGeo;      // x: top radius, y: sqrt(top^2 - R^2), z: 1 = the FX atmosphere lights the scene, w: the Sun's angular radius
    float4 mSunDir;   // to the Sun from the planet; w: 0
    float4 mRay;      // Rayleigh scattering at the ground [1/m]; w: scale height
    float4 mMieExt;   // aerosol extinction at the ground (or in the cloud layer) [1/m]; w: scale height
    float4 mMieSca;   // aerosol scattering; w: base of a constant-density layer (< 0: none, plain exponential)
    float4 mMieG;     // aerosol HG asymmetry per channel; w: top of the layer
    float4 mAbs;      // absorber (ozone) extinction at its peak [1/m]; w: altitude of the peak
    float4 mGround;   // ground albedo; w: half width of the absorber's tent profile
    float4 mRay2;     // x: altitude where the Rayleigh scale height changes (0: never), y: the scale height above it;
                      // z: the altitude below which the air is opaque seen from above (vertical tau > 20; 0: none)
};
Texture2D tFxLut : register(t8);   // rows 0..63: transmittance to the top (256 wide); rows 64..79, columns 0..63: sky irradiance

static const int FX_TW = 256, FX_TH = 64, FX_IW = 64, FX_IH = 32;

// the altitude coordinate of the irradiance and multiple-scattering tables: sqrt of the height fraction, so the rows crowd
// near the ground where the air is (Titan: half the rows below 175 km of 700)
float FxAltX(float r) { return sqrt(saturate((r - mCentre.w) / (mGeo.x - mCentre.w))); }
float FxAltR(float x) { return mCentre.w + (mGeo.x - mCentre.w) * x * x; }

bool FxActive() { return mGeo.z > 0.5; }

// bilinear filtering by hand (no sampler register taken from the client)
float4 FxBilinear(Texture2D t, float2 f, int2 org, int2 size)
{
    f = clamp(f, 0, size - 1);
    int2 i0 = (int2)floor(f), i1 = min(i0 + 1, size - 1);
    float2 w = f - i0;
    float4 a = t.Load(int3(org + i0, 0)), b = t.Load(int3(org + int2(i1.x, i0.y), 0));
    float4 c = t.Load(int3(org + int2(i0.x, i1.y), 0)), d = t.Load(int3(org + i1, 0));
    return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y);
}

// ---- the medium at altitude h
float FxMieDensity(float h)
{
    float H = mMieExt.w, base = mMieSca.w, top = mMieG.w;
    if (base < 0) return exp(-h / H);
    if (h < base) return exp(-(base - h) / 1000.0);     // the layer's lower edge, ~1 km
    if (h <= top) return 1;
    return exp(-(h - top) / H);
}
float FxRayDensity(float h)
{
    if (mRay2.x <= 0 || h <= mRay2.x) return exp(-h / mRay.w);
    return exp(-mRay2.x / mRay.w - (h - mRay2.x) / mRay2.y);
}
float FxAbsDensity(float h) { return mGround.w > 0 ? max(0, 1 - abs(h - mAbs.w) / mGround.w) : 0; }
float3 FxExtinction(float h)
{
    return mRay.rgb * FxRayDensity(h) + mMieExt.rgb * FxMieDensity(h) + mAbs.rgb * FxAbsDensity(h);
}

// ---- transmittance from (r, mu) to the top of the air (Bruneton's parametrisation)
float3 FxTrans(float r, float mu)
{
    float R = mCentre.w, top = mGeo.x, Hh = mGeo.y;
    r = clamp(r, R, top);
    float rho = sqrt(max(r * r - R * R, 0));
    float d = max(-r * mu + sqrt(max(top * top - r * r * (1 - mu * mu), 0)), 0);
    float dmin = top - r, dmax = rho + Hh;
    float xm = (d - dmin) / max(dmax - dmin, 1e-3), xr = rho / Hh;
    return FxBilinear(tFxLut, float2(xm * (FX_TW - 1), xr * (FX_TH - 1)), int2(0, 0), int2(FX_TW, FX_TH)).rgb;
}
// ... towards the Sun: zero below the planet's horizon (soft over the Sun's disc)
float3 FxTransToSun(float r, float muS)
{
    float s = mCentre.w / max(r, mCentre.w);
    float cosH = -sqrt(max(1 - s * s, 0));
    float e = max(mGeo.w, 1e-4);
    return FxTrans(r, muS) * smoothstep(-e, e, muS - cosH);
}
// sky irradiance on a horizontal surface at (r, muS), the Sun's irradiance = 1
float3 FxIrradiance(float r, float muS)
{
    return FxBilinear(tFxLut, float2((muS * 0.5 + 0.5) * (FX_IW - 1), FxAltX(r) * (FX_IH - 1)), int2(0, FX_TH), int2(FX_IW, FX_IH)).rgb;
}

// ---- the light at a point wp (camera-centred global frame)
float3 FxSunLight(float3 wp)
{
    float3 p = wp - mCentre.xyz;
    float r = length(p);
    if (r >= mGeo.x) return 1;
    return FxTransToSun(r, dot(p, mSunDir.xyz) / r);
}
float3 FxSkyLight(float3 n, float3 wp)
{
    float3 p = wp - mCentre.xyz;
    float r = length(p);
    if (r >= mGeo.x) return 0.02;                                         // the client's faint fill light in space
    float space = 0.02 * saturate((r - mCentre.w) / (mGeo.x - mCentre.w));   // ... fading in towards the top of the air
    float3 up = p / r;
    float mu = dot(up, mSunDir.xyz);
    float3 E = FxIrradiance(r, mu);
    float3 G = mGround.rgb * (E + FxTransToSun(r, mu) * saturate(mu));     // light from the ground below
    float k = dot(n, up);
    return E * (0.5 + 0.5 * k) + G * (0.5 - 0.5 * k) + space;
}

#endif
