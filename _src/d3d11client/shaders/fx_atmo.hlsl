// D3D11Client FX: a planet's haze shell seen from outside or from high up (full screen, dual-source blending:
// out = in-scattered light + scene * transmittance). See src\FxAtmo.cpp for the model and the data.
#include "common.hlsli"

cbuffer Atmo : register(b3)
{
    float4 aRow0, aRow1, aRow2;   // camera -> global rotation rows
    float4 aTan;      // tan half-fov x, y; near plane; 0
    float4 aCentre;   // planet centre from the camera [m]; w: R
    float4 aCam;      // camera distance from the centre, camera altitude, shell top radius, haze scale height H
    float4 aSun;      // to the Sun (from the planet); w: HG asymmetry g
    float4 aExt;      // extinction at the ground [1/m] per channel
    float4 aSSA;      // single-scattering albedo; w: weight of the pass
    float4 aMS;       // multiple-scattering amplitude; w: 1 = relight the surface (the scene was lit as in open space)
    float4 aKappa;    // decay of the diffuse light per optical depth
    float4 aSurf;     // sunlight at the ground under the haze
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

// Chapman function for a ray going up (mu >= 0) at x = r / H: sqrt(pi x / 2) exp(y^2) erfc(y), y = mu sqrt(x / 2),
// with exp(y^2) erfc(y) ~ 2 / (sqrt(pi) (y + sqrt(y^2 + 4 / pi)))
float ChUp(float x, float mu)
{
    float y = mu * sqrt(0.5 * x);
    return sqrt(2 * x) / (y + sqrt(y * y + 1.2732395));
}

// optical depth towards the Sun from radius r (altitude h), in units of (extinction at the ground) * H;
// below the horizon the ray passes its lowest point (the planet's shadow when that is under the ground)
float TauSun(float r, float h, float mu)
{
    float H = aCam.w, R = aCentre.w;
    if (mu >= 0) return exp(-h / H) * ChUp(r / H, mu);
    float rt = r * sqrt(saturate(1 - mu * mu));
    float ht = rt - R;
    return 2 * exp(min(-ht / H, 40.0)) * sqrt(1.5707963 * rt / H) - exp(-h / H) * ChUp(r / H, -mu);
}

float PhaseHG(float c, float g) { float g2 = g * g; return (1 - g2) / pow(max(1 + g2 - 2 * g * c, 1e-4), 1.5); }

PSOut PS(VSOut i)
{
    PSOut o; o.add = 0; o.mul = 1;
    float3 rv = float3(i.ndc.x * aTan.x, i.ndc.y * aTan.y, 1);
    rv = float3(dot(aRow0.xyz, rv), dot(aRow1.xyz, rv), dot(aRow2.xyz, rv));
    float rl = length(rv);
    float3 d = rv / rl;
    float3 org = -aCentre.xyz;                       // the camera from the planet centre
    float R = aCentre.w, rc = aCam.x, alt = aCam.y, Rt = aCam.z, H = aCam.w;
    float b = dot(org, d);
    float disT = b * b - (rc - Rt) * (rc + Rt);
    if (disT <= 0) discard;
    float sq = sqrt(disT);
    float t0 = max(-b - sq, 0.0), t1 = -b + sq;
    if (t1 <= 0) discard;
    float dz = tDepth.Load(int3(i.pos.xy, 0));
    float tS = dz > 0 ? rl * aTan.z / dz : 1e30;     // reverse Z, infinite far plane: depth = near / view z
    float Rg = R - 1000;                             // the ground where no tile is drawn
    float disG = b * b - (alt + 1000) * (rc + Rg);
    if (disG > 0) { float tg = -b - sqrt(disG); if (tg > 0) t1 = min(t1, tg); }
    bool scene = tS < t1;
    t1 = min(t1, tS);
    if (t1 <= t0) discard;

    float c0 = alt * (rc + R);                       // r^2 - R^2 at the camera (no cancellation of planet-sized numbers)
    float g = aSun.w;
    float ph = PhaseHG(dot(d, aSun.xyz), g) * 0.25;  // single scattering in units of the Sun's I/F
    float3 T = 1, S = 0;
    float tc = clamp(-b, t0, t1);                    // the ray's lowest point: the samples crowd there
    const int NS = 24;
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
        float tm = 0.5 * (ta + tb);
        float q = c0 + tm * (2 * b + tm);
        float r = sqrt(max(R * R + q, 1));
        float h = q / (r + R);
        float mu = dot(org + d * tm, aSun.xyz) / r;
        float3 ts = aExt.rgb * (TauSun(r, h, mu) * H);
        float3 J = aSSA.rgb * ph * exp(-ts) + aMS.rgb * exp(-aKappa.rgb * ts);
        float3 a = exp(-aExt.rgb * exp(-h / H) * (tb - ta));
        S += T * J * (1 - a);
        T *= a;
    }
    // what is behind: the image comes through with the forward-scattering peak (delta scaling, f = SSA g^2)
    float3 Ti = pow(max(T, 1e-30), 1 - aSSA.rgb * g * g);
    float3 relight = 1;
    if (scene && aMS.w > 0.5) {
        // the scene was lit by the open Sun: the light that reaches it under the haze instead (the cfg's ground sunlight
        // at noon, less towards the terminator, none in the planet's shadow)
        float q = c0 + tS * (2 * b + tS);
        float r = sqrt(max(R * R + q, 1));
        float h = q / (r + R);
        if (h < 2 * H) {
            float mu = dot(org + d * tS, aSun.xyz) / r;
            float tv = exp(-h / H) * ChUp(r / H, 1);
            float ts = TauSun(r, h, mu);
            relight = aSurf.rgb * exp(-aKappa.rgb * aExt.rgb * H * max(ts - tv, 0));
            relight = lerp(1, relight, saturate(2 - h / H));
        }
    }
    float w = aSSA.w;
    o.add = float4(S * w, 0);
    o.mul = float4(lerp(1, Ti * relight, w), 1);
    return o;
}
