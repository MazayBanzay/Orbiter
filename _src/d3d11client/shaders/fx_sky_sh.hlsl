// D3D11Client FX atmosphere: the light of the real sky around the camera as L2 spherical harmonics (FxAtmo.cpp, every
// frame). The sky's radiance in 1024 directions over the whole sphere (the ground below the horizon with its own lit
// colour: the bounce), projected on the 9 harmonics and convolved with the cosine lobe (Ramamoorthi & Hanrahan), so the
// light on a surface of any normal is 9 products (FxSkyLight, fx_atmo_light.hlsli). Forward scattering in the haze, the
// bright Sun-side sky, the dark ground: all of it reaches the slopes as it should.
#include "fx_atmo_sky.hlsli"

RWStructuredBuffer<float4> uSH : register(u0);    // 0..8: the irradiance coefficients (rgb); 9: x = the camera's radius, w = 1 valid
groupshared float3 gs[256][9];

void Basis(float3 d, out float b[9])
{
    b[0] = 0.282095;
    b[1] = 0.488603 * d.y; b[2] = 0.488603 * d.z; b[3] = 0.488603 * d.x;
    b[4] = 1.092548 * d.x * d.y; b[5] = 1.092548 * d.y * d.z; b[6] = 0.315392 * (3 * d.z * d.z - 1);
    b[7] = 1.092548 * d.x * d.z; b[8] = 0.546274 * (d.x * d.x - d.y * d.y);
}

[numthreads(256, 1, 1)]
void CS(uint3 gt : SV_GroupThreadID)
{
    const int N = 1024;
    float3 acc[9];
    [unroll] for (int k = 0; k < 9; k++) acc[k] = 0;
    for (int i = (int)gt.x; i < N; i += 256) {
        // a Fibonacci sphere: even directions
        float z = 1 - (i + 0.5) * 2.0 / N, rr = sqrt(max(1 - z * z, 0));
        float ph = i * 2.39996323;
        float3 d = float3(rr * cos(ph), rr * sin(ph), z);
        // above the local horizon the sky (radiance, the Sun's irradiance = 1); below it the ground right under the
        // camera, lit by the Sun through the air and by the sky (the planet's mean radius may lie above a camera at a sea
        // below it: the ground is taken where the camera stands)
        float3 org = -mCentre.xyz; float rc = length(org); float3 up = org / rc;
        float3 L;
        if (dot(d, up) >= 0) L = FxSkyRadianceEx(float3(0, 0, 0), d, 10, true) / FX_PI;
        else { float muS = dot(up, mSunDir.xyz); L = mGround.rgb / FX_PI * (FxTransToSun(rc, muS) * saturate(muS) + FxIrradiance(rc, muS)); }
        float b[9]; Basis(d, b);
        [unroll] for (int k = 0; k < 9; k++) acc[k] += L * b[k];
    }
    [unroll] for (int k = 0; k < 9; k++) gs[gt.x][k] = acc[k];
    GroupMemoryBarrierWithGroupSync();
    for (uint s = 128; s > 0; s >>= 1) {
        if (gt.x < s) { [unroll] for (int k = 0; k < 9; k++) gs[gt.x][k] += gs[gt.x + s][k]; }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gt.x == 0) {
        const float A[9] = { 3.141593, 2.094395, 2.094395, 2.094395, 0.785398, 0.785398, 0.785398, 0.785398, 0.785398 };
        const float w = 4 * FX_PI / N;
        [unroll] for (int k = 0; k < 9; k++) uSH[k] = float4(gs[0][k] * w * A[k], 0);
        uSH[9] = float4(length(mCentre.xyz), 0, 0, 1);
    }
}
