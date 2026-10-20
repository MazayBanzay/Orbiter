// D3D11Client: a planet's 2D cloud layer (CloudLayer.cpp) - the far view only, as Orbiter's own layer: a sphere at CloudAlt
// textured from Textures\<planet>\Archive\Cloud.tree (the coverage, alpha), turned by the layer's own rotation. Seen
// from near (inside the air, below the switch altitude) the volumetric clouds take over and this is not drawn at all.
//  - PSLayer: per pixel the ray meets the sphere; the coverage there, lit as the ground is (Sun through the air, the sky's
//    light; a cloud's albedo), the air in front of it from the FX atmosphere's march; over the scene through its depth
//  - PSShadow: the ground and the vessels under it: the ray from each pixel towards the Sun meets the layer, its coverage
//    dims the pixel (before the air is laid over the scene)
#include "common.hlsli"
#include "fx_atmo_sky.hlsli"

cbuffer CloudLayer : register(b1)
{
    float4x4 cInvVP;        // clip -> camera-centred global
    float4 cRot0, cRot1, cRot2;   // global -> planet frame (rows); w: the camera in the planet frame / 1000 (km)
    float4 cL;              // x: the layer's radius [m], y: cos, z: sin of the layer's rotation, w: the shadow's depth
    float4 cF;              // x: fade (0..1, the switch to the volumetric clouds), y: albedo, z: the texture's width, w: height
    float4 cCy;             // the cyclone overlay: xy its centre (lng, lat [rad]), z its half-size [m] (0: none), w: blend
    float4 cPx;             // x: a pixel's angle [rad]
};
Texture2D<float> tCov : register(t0);      // coverage, equirectangular (lng -180..180 left to right, north at the top)
Texture2D<float> tDepth : register(t1);    // the scene's depth (resolved)
Texture2D<float> tCyc : register(t2);      // the cyclone's coverage over its square (CloudLayer.cpp), optional
SamplerState sWrap : register(s0);
SamplerState sClampC : register(s1);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint vid : SV_VertexID)
{
    VSOut o;
    float2 c = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(c * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = c;
    return o;
}

float3 ToPlanet(float3 v) { return float3(dot(cRot0.xyz, v), dot(cRot1.xyz, v), dot(cRot2.xyz, v)); }
float3 CamPl() { return float3(cRot0.w, cRot1.w, cRot2.w) * 1000.0; }

// the layer's texture coordinates at a planet-frame point (the layer's own rotation about the axis taken off first)
float2 LayerUV(float3 P)
{
    float3 v = float3(P.x * cL.y - P.z * cL.z, P.y, P.x * cL.z + P.z * cL.y);
    float lng = atan2(v.z, v.x), lat = asin(clamp(v.y / length(v), -1, 1));
    return float2(lng / (2 * FX_PI) + 0.5, 0.5 - lat / FX_PI);
}

// the coverage at a planet-frame point, the texture's level from the footprint [m] of what is looked at
float Coverage(float3 P, float foot)
{
    float2 uv = LayerUV(P);
    float texel = 2 * FX_PI * cL.x / cF.z;                      // a texel at the equator [m]
    float lod = log2(max(foot / texel, 1));
    float c = tCov.SampleLevel(sWrap, uv, lod);
    if (cCy.z > 0) {        // the cyclone's own clouds over its square, the layer's faded out under it
        float3 n = normalize(P);
        float lng = atan2(n.z, n.x), lat = asin(clamp(n.y, -1, 1));
        float dl = lng - cCy.x; dl -= 2 * FX_PI * round(dl / (2 * FX_PI));
        float2 q = float2(dl * cos(cCy.y), lat - cCy.y) * cL.x / cCy.z;      // [-1, 1] over the square
        if (all(abs(q) < 1)) {
            float cy = tCyc.SampleLevel(sClampC, q * float2(0.5, -0.5) + 0.5, 0);
            float edge = saturate((1.35 - length(q) * 1.5) / 0.45);          // the storm's own circle (r < ~1.3 radii), not its square
            c = lerp(c, cy, edge * cCy.w);
        }
    }
    return c;
}

// the ray (camera-centred, unit d) against the layer's sphere: the first hit ahead, or -1
float HitLayer(float3 org, float3 d)
{
    float b = dot(org, d), c = dot(org, org) - cL.x * cL.x;
    float D = b * b - c;
    if (D < 0) return -1;
    float s = sqrt(D);
    float t = c > 0 ? -b - s : -b + s;      // from outside the near side, from inside the far one
    return t > 0 ? t : -1;
}

float4 PSLayer(VSOut i) : SV_Target
{
    float4 cc = float4(i.uv.x * 2 - 1, 1 - i.uv.y * 2, 1, 1);
    float4 wn = mul(cc, cInvVP);
    float3 d = normalize(wn.xyz / wn.w);
    float3 org = -mCentre.xyz;
    float t = HitLayer(org, d);
    if (t < 0) discard;
    // the planet itself in front (the far side of the layer behind the globe), a hull, a mountain over the layer
    float z = tDepth.Load(int3(i.pos.xy, 0));
    if (z > 1e-9) {
        float4 w = mul(float4(cc.xy, z, 1), cInvVP);
        if (length(w.xyz / w.w) < t) discard;
    }
    // the planet's body hides the far side of the layer (the ray through the globe)
    float bR = dot(org, d), cR = dot(org, org) - mCentre.w * mCentre.w, DR = bR * bR - cR;
    if (cR > 0 && DR > 0 && -bR - sqrt(DR) > 0 && -bR - sqrt(DR) < t) discard;
    float3 p = d * t;
    float3 P = ToPlanet(p) + CamPl();
    float3 up = normalize(p - mCentre.xyz);
    // the footprint of a pixel there: the distance over the screen's rows, stretched by the grazing angle
    float foot = t * cPx.x / max(abs(dot(d, up)), 0.08);
    float a = Coverage(P, foot) * cF.x;
    if (a < 0.003) discard;
    // lit as the ground: albedo * (Sun * cos + sky); from below the light that comes through (thinner: the same
    // coverage reads as a grey base, as Orbiter's layer from under it)
    bool below = dot(org, org) < cL.x * cL.x;
    float mu = dot(up, gSunDir.xyz);
    float3 sun = SunLight(p), sky = SkyLight(up, p);
    float wrap = saturate((mu + 0.05) / 1.05);                 // the layer's tops still catch the Sun a little past the terminator
    float3 L = below ? (sun * saturate(mu) * 0.55 + sky * 0.8) * (1 - 0.5 * a) * cF.y
                     : (sun * wrap + sky) * cF.y;
    // the air in front (the FX atmosphere's march from where the ray enters the air to the layer)
    float3 La = 0, Ta = 1;
    if (FxActive()) {
        float3 fw; float tc0 = -dot(org, d); float3 cx = cross(org, d); float K = dot(cx, cx) - mCentre.w * mCentre.w;
        float bt = dot(org, d), dTopA = bt * bt - (dot(org, org) - mGeo.x * mGeo.x);
        float tIn = dTopA > 0 ? max(0, -bt - sqrt(dTopA)) : 0;
        if (t > tIn) FxMarch(org, d, tc0, K, tIn, t, 12, La, Ta, fw);
        La *= FX_PI;
    }
    return float4((Ta * L + La) * a, 1 - a);
}

// the shadow on what was drawn: the ground, the vessels (before the air goes over the scene)
float4 PSShadow(VSOut i) : SV_Target
{
    float z = tDepth.Load(int3(i.pos.xy, 0));
    if (z <= 1e-9) discard;
    float4 cc = float4(i.uv.x * 2 - 1, 1 - i.uv.y * 2, z, 1);
    float4 w = mul(cc, cInvVP);
    float3 wp = w.xyz / w.w;
    float3 org = wp - mCentre.xyz;                       // the point from the planet's centre
    if (dot(org, org) >= cL.x * cL.x) discard;           // above the layer
    float3 s = gSunDir.xyz;
    float b = dot(org, s), c = dot(org, org) - cL.x * cL.x;
    float t = -b + sqrt(max(b * b - c, 0));
    float3 P = ToPlanet(wp + s * t) + CamPl();
    float foot = max(length(wp), 2000.0) * 0.002;
    float a = Coverage(P, foot) * cF.x;
    float k = 1 - a * cL.w;
    return float4(k, k, k, 1);
}
