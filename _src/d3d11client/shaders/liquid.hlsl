// D3D11Client: a liquid surface (Titan's methane-ethane seas): Fresnel (n = 1.29: F0 0.016), the sky in the mirror along
// the reflected ray, the shore and the terrain reflected from the scene's depth (screen-space), the vessels from the
// mirror pass (looked up through its own depth), refraction marched along the bent ray through the scene's depth (the
// apparent depth and the parallax of the bed and of what stands in the liquid), Beer-Lambert per channel for the light
// down to the point and back up, the liquid's own colour scattered in, the Sun's glint (GGX), the haze.
// Multisampled scenes: PSMS reads the scene per sample and keeps only the samples that lie under the surface - a pixel
// on a hull's outline above the liquid holds both, and its averaged colour made a ghost outline around the hull.
#include "common.hlsli"
#include "fx_atmo_sky.hlsli"     // the FX atmosphere's sky in the mirror

cbuffer Liquid : register(b1)
{
    float4x4 gVP;
    float4x4 gLInvVP;
    float4 gBase;     // camera -> the liquid point under it; w: radius of the liquid sphere
    float4 gLUp;      // w: grid radius
    float4 gE1, gE2;
    float4 gColour;   // w: extinction [1/m]
    float4 gWts;      // w: refractive index
    float4 gLMisc;    // x: time, z: diagnostics, w: samples per pixel (PSMS)
    float4 gWA;       // the wave field (Wake.cpp): its corner (cell 0, 0) from the camera; w: cell size [m]
    float4 gWE1, gWE2;   // its axes
    float4 gWN;       // x: cells per side, y: 1 = on, z: 1 = the mirror's depth is bound, w: 1 = the bed's depth is bound
};
Texture2D tScene : register(t0);
Texture2D<float> tDepth : register(t1);
Texture2D<float4> tWave : register(t2);   // the wave field: slopes along e1, e2 (per metre), height, per cell
SamplerState sWrap : register(s0);
SamplerState sClamp : register(s1);
Texture2D tKelvin : register(t3);         // the Kelvin wake in units of its wavelength: rg = slope along / across (0.5 = 0)
Texture2D tRefl : register(t4);           // the vessels reflected in the plane (alpha: covered), the scene's size
Texture2DMS<float4> tSceneMS : register(t5);   // multisampled scene and depth (PSMS)
Texture2DMS<float> tDepthMS : register(t6);
Texture2D<float> tReflDepth : register(t11);    // the mirror pass's depth: how far the reflected things are
Texture2D<float> tBedDepth : register(t12);     // the terrain's depth alone (before the vessels): the liquid's bed
// the wakes (Wake.cpp): a: wavelength, slope amplitude, radius, time / front; b: centre from the camera, w the kind
// (0 Kelvin); f: the direction of motion
cbuffer Wakes : register(b5)
{
    float4 gWk[96];
    float4 gWkN;      // x: the number of sources
};

// the slope of the surface from all wakes at wp (a vector in the tangent plane: the normal is up - slope)
float3 WakeSlope(float3 wp, float3 up)
{
    float3 sv = 0;
    int n = (int)gWkN.x;
    [loop] for (int k = 0; k < n; k++) {
        float4 a = gWk[k * 3], b = gWk[k * 3 + 1], f = gWk[k * 3 + 2];
        float3 d = wp - b.xyz; d -= up * dot(d, up);
        if (b.w < 0.5) {                    // Kelvin: x across, y behind, in units of the wavelength
            float3 fw = f.xyz, side = cross(up, fw);
            float u = -dot(d, fw), v = dot(d, side), lam = a.x;
            float2 tc = float2(v / (13 * lam) * 0.5 + 0.5, u / (32 * lam));
            if (any(tc < 0) || any(tc > 1)) continue;
            float2 sl = (tKelvin.SampleLevel(sClamp, tc, 0).rg * 2 - 1) * a.y;
            float fade = saturate(u / max(a.z, 0.3)) * saturate((1 - tc.y) / 0.25);
            sv += (fw * sl.x + side * sl.y) * fade;
        }
    }
    return sv;
}

// the wave field at wp (weighted 0 outside its window, faded over the last 5 %)
float4 WaveAt(float3 wp, out float wgt)
{
    wgt = 0;
    if (gWN.y < 0.5) return 0;
    float3 d = wp - gWA.xyz;
    float2 q = float2(dot(d, gWE1.xyz), dot(d, gWE2.xyz)) / gWA.w;    // in cells
    // the cells under one pixel (taken before any branch): ripples finer than the pixel cannot be seen - read at one point
    // they were a random slope each, a moire of black bands on the water seen from 50+ m (2026-10-20). Their mean slope
    // there is ~0: they fade from 1 cell a pixel to gone at 4 (the field's heights stay)
    const float fp = max(length(ddx(q)), length(ddy(q)));
    const float k = saturate((4.0 - fp) / 3.0);
    float e = min(min(q.x, gWN.x - q.x), min(q.y, gWN.x - q.y));
    wgt = saturate(e / (0.05 * gWN.x));
    if (wgt <= 0) return 0;
    float4 w = tWave.SampleLevel(sClamp, q / gWN.x, 0);
    w.xy *= k;
    return w * wgt;
}

struct VSOut { float4 pos : SV_Position; float3 wp : WPOS; float3 n : NORMAL; };

VSOut VS(float2 g : POSITION)
{
    // ring radius grows geometrically from 0.3 m to the grid radius; the point on the sphere, relative to the camera,
    // built from small terms (no cancellation of planet-sized numbers)
    float r = g.x <= 0 ? 0 : 0.3 * pow(gLUp.w / 0.3, g.x);
    float th = g.y * 6.2831853;
    float phi = r / gBase.w;
    float3 t = gE1.xyz * cos(th) + gE2.xyz * sin(th);
    float3 d = gLUp.xyz * (-2 * sin(phi * 0.5) * sin(phi * 0.5)) + t * sin(phi);   // unit direction minus up
    float3 wp = gBase.xyz + gBase.w * d;
    VSOut o;
    o.n = normalize(gLUp.xyz * cos(phi) + t * sin(phi));
    o.wp = wp;            // the waves shape the normal only (moved geometry cut through the shallows in rings)
    o.pos = mul(float4(wp, 1), gVP);
    return o;
}

// ---- the deep deformations in the geometry (the jets' cavities, an impact's crater and its rebound, a hull's bow):
// the rings' triangles are tessellated where the wave field stands more than ~1.5 cm off the level, and moved with it.
// The ripples stay in the normals only (on the plain rings a fine field read at coarse nodes cut the shallows in rings);
// the move fades in over 1.5 .. 4 cm of height - a function of the point alone, so neighbouring patches meet. No move
// at the shore (as the slopes), and never through the bed.
struct CP { float3 wp : WPOS; float3 n : NORMAL; };
CP VSCP(float2 g : POSITION) { VSOut o = VS(g); CP c; c.wp = o.wp; c.n = o.n; return c; }

float WaveH(float3 wp)
{
    if (gWN.y < 0.5) return 0;
    float3 d = wp - gWA.xyz;
    float2 q = float2(dot(d, gWE1.xyz), dot(d, gWE2.xyz)) / gWA.w;
    float e = min(min(q.x, gWN.x - q.x), min(q.y, gWN.x - q.y));
    float wgt = saturate(e / (0.05 * gWN.x));
    if (wgt <= 0) return 0;
    return tWave.SampleLevel(sClamp, q / gWN.x, 0).z * wgt;
}

// the height for the geometry: over ~2 cells (a tent of 5 taps) - a cell's noise read at the tessellated nodes stood up
// as teeth along a crater's rim
float WaveHs(float3 wp)
{
    float o = 0.75 * gWA.w;
    float3 a = gWE1.xyz * o, b = gWE2.xyz * o;
    return 0.4 * WaveH(wp) + 0.15 * (WaveH(wp + a) + WaveH(wp - a) + WaveH(wp + b) + WaveH(wp - b));
}

float TessTarget(float3 p) { return max(1.5 * gWA.w, length(p) * 0.004); }     // ~a cell and a half, or ~4 px

// an edge's factor from its two ends only (the same for both patches that share it)
float EdgeTess(float3 a, float3 b)
{
    bool sw = a.x > b.x || (a.x == b.x && (a.y > b.y || (a.y == b.y && a.z > b.z)));
    float3 p0 = sw ? b : a, p1 = sw ? a : b;
    float hm = 0;
    [loop] for (int k = 0; k <= 8; k++) hm = max(hm, abs(WaveH(lerp(p0, p1, k / 8.0))));
    if (hm < 0.015) return 1;
    return clamp(length(p1 - p0) / TessTarget(0.5 * (p0 + p1)), 1, 64);
}

struct PC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };

PC HSConst(InputPatch<CP, 3> ip)
{
    PC o; o.e[0] = o.e[1] = o.e[2] = o.i = 1;
    if (gWN.y < 0.5) return o;
    // only patches over the field's window, near enough to see a cavity
    float3 c = (ip[0].wp + ip[1].wp + ip[2].wp) / 3;
    float sz = max(length(ip[1].wp - ip[0].wp), max(length(ip[2].wp - ip[1].wp), length(ip[0].wp - ip[2].wp)));
    if (length(c) > 400) return o;
    float3 d = c - gWA.xyz;
    float2 q = float2(dot(d, gWE1.xyz), dot(d, gWE2.xyz)) / gWA.w;
    float m = sz / gWA.w + 2;
    if (q.x < -m || q.y < -m || q.x > gWN.x + m || q.y > gWN.x + m) return o;
    o.e[0] = EdgeTess(ip[1].wp, ip[2].wp);
    o.e[1] = EdgeTess(ip[2].wp, ip[0].wp);
    o.e[2] = EdgeTess(ip[0].wp, ip[1].wp);
    // inside: a hollow may lie within the patch, away from its edges
    float hm = 0;
    [unroll] for (int j = 1; j <= 3; j++) [unroll] for (int i = 1; i <= 3 - j + 1; i++) {
        float u = i / 5.0, v = j / 5.0;
        hm = max(hm, abs(WaveH(ip[0].wp * (1 - u - v) + ip[1].wp * u + ip[2].wp * v)));
    }
    float fi = hm < 0.015 ? 1 : clamp(sz / TessTarget(c), 1, 64);
    o.i = max(fi, max(o.e[0], max(o.e[1], o.e[2])));
    return o;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("HSConst")]
CP HS(InputPatch<CP, 3> ip, uint i : SV_OutputControlPointID) { return ip[i]; }

float LevelH(float3 p);
float3 WorldAt(float2 uv, float dz);
float2 UvOf(float3 p);

[domain("tri")]
VSOut DS(PC pc, float3 b : SV_DomainLocation, const OutputPatch<CP, 3> ip)
{
    float3 wp = ip[0].wp * b.x + ip[1].wp * b.y + ip[2].wp * b.z;
    float3 n = normalize(ip[0].n * b.x + ip[1].n * b.y + ip[2].n * b.z);
    float h = WaveHs(wp);
    float disp = h * smoothstep(0.015, 0.04, abs(h));
    if (disp != 0 && gWN.w > 0.5) {
        // the terrain's depth along this point's line of sight: no move at the shore, and never below the bed
        float2 uv = UvOf(wp);
        if (all(uv >= 0) && all(uv <= 1)) {
            float bw, bh; tBedDepth.GetDimensions(bw, bh);
            float shoreD = max(-LevelH(WorldAt(uv, tBedDepth.Load(int3(clamp(int2(uv * float2(bw, bh)), int2(0, 0), int2(bw, bh) - 1), 0)))), 0);
            disp *= saturate((shoreD - 0.02) / 0.08);
            disp = max(disp, -max(shoreD - 0.02, 0));
        }
    }
    wp += n * disp;
    VSOut o;
    o.n = n; o.wp = wp;
    o.pos = mul(float4(wp, 1), gVP);
    return o;
}

float3 WorldAt(float2 uv, float dz)
{
    // reverse-Z: depth 0 is the sky (infinitely far: w = 0) - taken 1e7 m out along the view ray, never a NaN
    float4 c = float4(uv.x * 2 - 1, 1 - uv.y * 2, max(dz, 1e-9), 1);
    float4 w = mul(c, gLInvVP);
    if (dz <= 1e-9 || abs(w.w) < 1e-12) { float4 f = mul(float4(c.xy, 1, 1), gLInvVP); return normalize(f.xyz / f.w) * 1e7; }
    return w.xyz / w.w;
}
float2 UvOf(float3 p) { float4 c = mul(float4(p, 1), gVP); return c.xy / c.w * float2(0.5, -0.5) + 0.5; }

// height of p over the liquid's level (camera-centred; the sphere's sag kept: x^2 / 2R)
float LevelH(float3 p)
{
    float3 d = p - gBase.xyz;
    float a = dot(d, gLUp.xyz);
    return a + (dot(d, d) - a * a) / (2 * gBase.w);
}

float D_GGX(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1) + 1; return a2 / (3.14159 * d * d); }

// ---- the scene under the surface at a pixel: its depth (for the march) and its colour
// single-sampled: the depth and the colour as they are; multisampled: only the samples under the level are averaged
// (an outline pixel of a hull above the liquid is part hull, part bed: the hull's samples are not what lies beneath)
float DepthAt(float2 uv)       // point-sampled: a filtered depth between a hull and what lies behind it is no surface
{
    float w0, h0; tDepth.GetDimensions(w0, h0);
    return tDepth.Load(int3(clamp(int2(uv * float2(w0, h0)), int2(0, 0), int2(w0, h0) - 1), 0));
}

bool UnderMS(int2 px, float2 uvc, out float3 col, out float dz)
{
    int ns = (int)gLMisc.w;
    float3 c = 0; float z = 0; int k = 0;
    [loop] for (int s = 0; s < ns; s++) {
        float d = tDepthMS.Load(px, s);
        if (LevelH(WorldAt(uvc, d)) < 0.02) { c += tSceneMS.Load(px, s).rgb; z += d; k++; }
    }
    col = k > 0 ? c / k : 0; dz = k > 0 ? z / k : 0;
    return k > 0;
}

float3 SceneUnder(float2 uv, bool ms)
{
    if (!ms) return tScene.SampleLevel(sClamp, uv, 0).rgb;
    float w0, h0; tScene.GetDimensions(w0, h0);
    // bilinear over the four pixels around, each from its samples under the level
    float2 p = uv * float2(w0, h0) - 0.5;
    int2 b = (int2)floor(p); float2 f = p - b;
    float3 acc = 0; float wsum = 0;
    [unroll] for (int j = 0; j < 2; j++) [unroll] for (int i = 0; i < 2; i++) {
        int2 q = clamp(b + int2(i, j), int2(0, 0), int2(w0, h0) - 1);
        float w = (i ? f.x : 1 - f.x) * (j ? f.y : 1 - f.y);
        float3 c; float z;
        if (UnderMS(q, (q + 0.5) / float2(w0, h0), c, z)) { acc += c * w; wsum += w; }
    }
    return wsum > 1e-4 ? acc / wsum : tScene.SampleLevel(sClamp, uv, 0).rgb;
}

// the scene ABOVE the level as the reflected ray meets it (the shore, the hills, a hull): marched in the camera's depth
// (screen-space), exponential steps out to 3 km, refined by bisection; w = confidence (0: left the screen, nothing hit)
float4 ReflectSS(float3 wp, float3 R)
{
    float t0 = 0, t = 0.4;
    bool front = false;                               // a hit only where the ray goes from in front of the scene to behind it
    [loop] for (int k = 0; k < 28; k++) {
        float3 p = wp + R * t;
        float2 uv = UvOf(p);
        if (any(uv < 0) || any(uv > 1)) return 0;
        float3 s = WorldAt(uv, DepthAt(uv));
        float ds = length(s), dp = length(p);
        if (dp <= ds) front = true;
        else if (front && LevelH(s) > -0.05) {        // passed behind something above the level: refine
            float a = t0, b2 = t;
            [unroll] for (int r = 0; r < 5; r++) {
                float m = 0.5 * (a + b2);
                float3 pm = wp + R * m; float2 um = UvOf(pm);
                if (length(pm) > length(WorldAt(um, DepthAt(um)))) b2 = m; else a = m;
            }
            float3 ph = wp + R * b2; float2 uh = UvOf(ph);
            float3 sh = WorldAt(uh, DepthAt(uh));
            if (length(ph) - length(sh) > max(0.15, 0.03 * b2)) { front = false; t0 = t; t *= 1.35; continue; }   // went behind a thin thing
            float2 e = min(uh, 1 - uh);
            float conf = saturate(min(e.x, e.y) / 0.06);                  // fade at the screen's edges
            return float4(tScene.SampleLevel(sClamp, uh, 0).rgb, conf);
        }
        t0 = t; t *= 1.35;
        if (t > 3000) break;
    }
    return 0;
}

float4 Shade(VSOut i, bool ms)
{
    float3 up = normalize(i.n);
    float tw, th2; tScene.GetDimensions(tw, th2);
    float2 uv0 = i.pos.xy / float2(tw, th2);
    float3 t1 = normalize(cross(up, gE1.xyz));
    // the surface's slope: the bodies' waves only (Titan's air barely moves, < 1 m/s: no wind sea)
    float3 sv = WakeSlope(i.wp, up);
    float ww; float4 wf = WaveAt(i.wp, ww);
    sv += (gWE1.xyz * wf.x + gWE2.xyz * wf.y);
    // the scene straight under this pixel (only what lies beneath the level)
    float dz0; float3 col0;
    bool any0;
    if (ms) any0 = UnderMS(int2(i.pos.xy), uv0, col0, dz0);
    else { dz0 = DepthAt(uv0); col0 = 0; any0 = true; }
    float3 sp0 = WorldAt(uv0, dz0);
    float bedD = max(-LevelH(sp0), 0);                // the column under this point (to what stands in it, too)
    // no waves on the ground at the shore: the terrain's own depth there (a leg or a wheel in the liquid is no shore)
    float shoreD = bedD;
    if (gWN.w > 0.5) {
        float bw, bh; tBedDepth.GetDimensions(bw, bh);
        shoreD = max(-LevelH(WorldAt(uv0, tBedDepth.Load(int3(clamp(int2(uv0 * float2(bw, bh)), int2(0, 0), int2(bw, bh) - 1), 0)))), 0);
    }
    sv *= saturate((shoreD - 0.02) / 0.08);
    float3 n = normalize(up - sv);

    float dist = length(i.wp);
    float3 V = -i.wp / dist;
    if (dot(V, up) < 0) {
        // seen from below (the camera in the liquid): the sky through Snell's window, the liquid's own light mirrored
        // outside it (total internal reflection); the column up to here is the underwater pass's (Liquid.cpp)
        float3 nd = -n;
        float3 T = refract(-V, nd, gWts.w);
        float3 deep = gColour.rgb * SkyLight(up) * 0.06;
        float ci = saturate(dot(V, nd));
        if (dot(T, T) < 1e-6) return float4(deep, 1);
        float ct = saturate(dot(T, -nd)), n1 = gWts.w;
        float rs = (n1 * ci - ct) / (n1 * ci + ct), rp = (ci - n1 * ct) / (ci + n1 * ct);
        float Fu = saturate(0.5 * (rs * rs + rp * rp));
        float3 sky = FxActive() ? FxSkyRadiance(i.wp, normalize(T), 12) : SkyColour(normalize(T));
        return float4(sky * (1 - Fu) + deep * Fu, 1);
    }
    float nv = saturate(dot(n, V)) + 1e-4;
    float F0 = pow((gWts.w - 1) / (gWts.w + 1), 2);
    float F = F0 + (1 - F0) * pow(1 - nv, 5);
    float3 R = reflect(-V, n);
    if (dot(R, up) < 0.002) R = normalize(R + up * (0.002 - dot(R, up)));   // a steep wave's ray kept over the level

    // ---- reflection: the sky along the ray itself (the haze's own brightness towards the horizon and the Sun), the
    // scene above the level where the ray meets it on screen, the vessels from the mirror pass
    float3 refl = FxActive() ? FxSkyRadiance(i.wp, R, 10) : SkyColour(R);
    float3 skyR = refl;
    float4 ss = ReflectSS(i.wp, R);
    refl = lerp(refl, ss.rgb, ss.a);
    float mirA = 0;
    {
        // the mirror pass drew the world mirrored in the level, seen by this camera: a plane mirror's image lies at this
        // very pixel; under a tilt the real thing the ray meets (at the distance the mirror's depth gives) is found where
        // its image is: project its mirror image
        float L = dist;
        if (gWN.z > 0.5) {
            float dm = tReflDepth.SampleLevel(sClamp, uv0, 0);
            if (dm > 0) L = length(WorldAt(uv0, dm) - i.wp);
        }
        float3 Q = i.wp + R * L;
        float3 Qm = Q - 2 * gLUp.xyz * dot(Q - gBase.xyz, gLUp.xyz);
        float2 ru = UvOf(Qm);
        float4 ro = tRefl.SampleLevel(sClamp, saturate(ru), 0);
        refl = lerp(refl, ro.rgb, saturate(ro.a));
        mirA = saturate(ro.a);
    }

    // ---- refraction: the bent ray marched through the scene's depth to what lies beneath (the bed, a leg, a wheel):
    // its image is where the ray seems to come from - shallower, and moving against the waterline with the camera
    float3 T = refract(-V, n, 1.0 / gWts.w);
    float2 huv = uv0; float path = 0; float hitDepth = bedD;
    {
        // out to the terrain's bed (not just to a leg under this pixel: the ray may pass it), 25 m at most
        float colD = max(max(shoreD, bedD), 0.05);
        float maxT = min(25.0, colD / max(-dot(T, up), 0.05) * 1.5 + 0.5);
        float ta = 0, tb = 0;
        bool hit = false;
        // the last point of the ray seen on screen under the level and in front of the scene: where an occluder above
        // the level (a hull out of the liquid) hides the rest, the image is taken there - never from straight under the
        // pixel, which would show a second, unshifted copy of what stands in the liquid
        float2 lastUv = uv0; float lastT = 0;
        // a hit: the ray goes from in front of the scene to just behind it (within a thin shell); deep behind is a body in
        // front of the ray (a submerged hull between it and the camera) - what lies there is hidden, the ray goes on
        bool inFront = true;
        [loop] for (int k = 1; k <= 24; k++) {
            float t = maxT * (k / 24.0) * (k / 24.0);
            float3 p = i.wp + T * t;
            float2 uv = UvOf(p);
            if (any(uv < 0) || any(uv > 1)) break;
            float3 s = WorldAt(uv, DepthAt(uv));
            if (LevelH(s) > 0.02) { inFront = false; continue; }   // hidden on screen by something above the level
            float dp = length(p), ds = length(s);
            if (dp < ds) { ta = t; lastUv = uv; lastT = t; inFront = true; continue; }
            if (inFront && dp - ds < 0.15 + 0.05 * t) { tb = t; hit = true; break; }
            inFront = false;                                   // behind a body: no image from it
        }
        if (hit) {
            [unroll] for (int r = 0; r < 5; r++) {
                float m = 0.5 * (ta + tb);
                float3 pm = i.wp + T * m; float2 um = UvOf(pm);
                float3 sm = WorldAt(um, DepthAt(um));
                if (length(pm) >= length(sm) || LevelH(sm) > 0.02) tb = m; else ta = m;
            }
            if (length(i.wp + T * tb) - length(WorldAt(UvOf(i.wp + T * tb), DepthAt(UvOf(i.wp + T * tb)))) > 0.3 + 0.05 * tb) tb = ta;
            path = tb;
            huv = UvOf(i.wp + T * tb);
            hitDepth = max(-LevelH(i.wp + T * tb), 0);
        }
        else {
            huv = lastUv;
            float3 sl = WorldAt(lastUv, DepthAt(lastUv));
            path = max(lastT, length(sl - i.wp));
            hitDepth = max(-LevelH(sl), 0);
        }
    }
    float3 bed = SceneUnder(huv, ms);
    float3 tr = exp(-gColour.w * gWts.rgb * (path + hitDepth));      // the view path, and the light down to the point
    float3 inl = gColour.rgb * SkyLight(up) * 0.06 * (1 - tr);
    float3 refr = bed * tr + inl;

    float3 L = gSunDir.xyz;
    float3 Hh = normalize(V + L);
    float nl = saturate(dot(n, L)) * ShadowAt(i.wp);
    float FH = F0 + (1 - F0) * pow(1 - saturate(dot(V, Hh)), 5);
    float glint = min(D_GGX(saturate(dot(n, Hh)), 0.04) * FH / (4 * max(nl, nv) + 1e-3), 30) * nl;
    float3 col = lerp(refr, refl, F) + glint * SunLight();
    // diagnostics (D3D11CLIENT_LIQDBG): 1 the mirror only, 2 Fresnel, 3 the refraction only, 4 the column, 5 slopes
    if (gLMisc.z > 7.5) return float4(skyR, 1);                                                         // 8: the sky along R
    if (gLMisc.z > 6.5) return float4(float3(mirA, 0, 0) / max(gHazeRGB.w * gAdapt.rgb, 1e-4), 1);     // 7: mirror coverage
    if (gLMisc.z > 5.5) return float4(float3(0, ss.a, 0) / max(gHazeRGB.w * gAdapt.rgb, 1e-4), 1);     // 6: screen-space hits
    if (gLMisc.z > 4.5) return float4(float3(length(sv) * 5, ww, gWN.y) / max(gHazeRGB.w * gAdapt.rgb, 1e-4), 1);
    if (gLMisc.z > 0.5) {
        float3 dbg = gLMisc.z < 1.5 ? refl : gLMisc.z < 2.5 ? F.xxx / max(gHazeRGB.w * gAdapt.rgb, 1e-4) : gLMisc.z < 3.5 ? refr : (saturate(path / 20)).xxx / max(gHazeRGB.w * gAdapt.rgb, 1e-4);
        return float4(dbg, 1);
    }
    return float4(ApplyHaze(col, i.wp), 1);
}

float4 PS(VSOut i) : SV_Target { return Shade(i, false); }
float4 PSMS(VSOut i) : SV_Target { return Shade(i, true); }
