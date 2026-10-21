// D3D11Client: local lights - the vessels' point and spot lights (Orbiter LightEmitter: headlights, lamps, the cockpit's
// lights), filled in src\Lights.cpp. Included by common.hlsli; mesh.hlsl and planet.hlsl add LocalLight*() to their light.
//
// Photometry. Orbiter gives a light's colour, intensity, range and attenuation 1 / (a0 + a1 d + a2 d^2); the modules
// tuned those under the old clamped lighting, so the curve itself is not physical (a0 keeps a lamp as bright at 10 m as at
// 1 m). The client takes from it one luminous intensity - the curve's illuminance at a quarter of the range, times
// LightLux (Client.cfg, lux per Orbiter unit, 10 by default) - and lets it fall as 1 / d^2 (the inverse square law), with
// a window that brings it to zero at the range (Karis 2013: (1 - (d/R)^4)^2). A spot's cone: full intensity inside the
// umbra, a smooth edge out to the penumbra (Orbiter's angles are the cones' full apertures). The illuminance in lux is
// divided by the Sun's at this distance from it (128 klx at 1 AU): the client's unit, the Sun's irradiance = 1.
// Measured that way the MPU's headlight is ~50 000 cd (a car's high beam), the suit's work lamp ~2 400 cd.
//
// The list: the view frustum cut into 16 x 9 tiles x 24 slices (exponential in depth); each cluster has the lights whose
// bounds reach it (t18: offset, count into t19). A point is looked up by its position through the camera's unjittered
// view-projection (gLVP), so any pass finds its cluster - points outside the view (the liquid's mirror) test every light.
#ifndef LIGHTS_HLSLI
#define LIGHTS_HLSLI

struct LocalLightG
{
    float4 p;   // position (camera-centred global) [m]; w: range [m]
    float4 c;   // colour x intensity: the illuminance at 1 m [Sun = 1]; w: bits - 1 spot, 2 not on the camera's own cockpit
                //  (an outside light of the vessel the eye is in: the world through the visor, not the helmet), 4 the cockpit only
    float4 d;   // spot: its direction (global); w: cos of the penumbra's half aperture
    float4 a;   // x: cos of the umbra's half aperture, y: the lamp's radius [m] (nearest distance), z: its first shadow record
                // in tShadowRec (-1 none; a point light: 6, the cube's faces),
                // w: 1 = lights the air (the beams pass)
    float4 e;   // a rectangle lamp (c.w bit 8): its second half-axis ey (global) [m] (d.xyz the first, ex); w: its area [m2].
                // Its c.rgb is then its radiance in the client's units (pi x luminance / the Sun's illuminance; bit 16: two-sided)
};
cbuffer LocalLights : register(b9)
{
    float4x4 gLVP;          // camera-centred global -> clip, without the TAA jitter (the cluster grid)
    float4 gLGrid;          // x, y: tiles across, down; z: slices; w: number of lights
    float4 gLZ;             // x: slice 0's far end [m], y: slices per ln(z / x), z: 1 = on, w: the frame (0..63, the beams' jitter)
    float4x4 gSkyVP[12];    // the sky's visibility: camera-centred global -> each direction's map (4 x 4 atlas of 256)
    float4 gSkyDir[12];     // xyz: to the sky (global); w: 0
    float4 gSkyC;           // xyz: the maps' centre (camera-centred); w: their radius [m] (0: off)
    float4 gSkyK;           // x: texel [m], y: the atlas' texel (uv), z: the lamps' bounce fill (0 off, 1 on: airless bodies or an interior)
    float4 gVolC;           // the interior volume the camera is in (D3D11Lights.h): its centre (camera-centred); w: 1 = one
    float4 gVolX, gVolY, gVolZ;   // its axes (global, unit) and half extents (w) [m]
    float4 gVolK;           // x: the blend at its faces [m]
    float4 gRoom[32];       // up to 8 rooms (D3D11Lights Volume with a room id) whose panels shine: 4 each - the centre
                            // (camera-centred; w: the blend [m]), then the 3 axes (global, unit) with the half extents (w)
    float4 gOpen[32];       // up to 8 openings (gVolK.y of them) of the interior the camera is in: centre (camera-centred),
                            // then the axes (global, unit) with the half extents (w)
};
StructuredBuffer<LocalLightG> tLights : register(t17);
StructuredBuffer<uint2> tLCluster : register(t18);
StructuredBuffer<uint> tLIndex : register(t19);
Texture2D<float> tSpotShadow : register(t20);    // the lamps' shadow atlas (4096, 16-bit): 4 tiles of 1024, 48 of 512
// a lamp's shadow map (Lights.cpp): its matrix, its place in the atlas, its texel; a point light's cube: 6 records in a row
struct ShadowRec
{
    float4x4 vp;    // camera-centred global -> the map's clip
    float4 rect;    // xy: the tile's corner (uv), z: its size (uv), w: the atlas' texel (uv)
    float4 k;       // x: the map's texel at 1 m [m] (the normal offset)
    float4 ax, ay;  // a cube's face axes (global): +-x, +-y, +-z = faces 0..5 (az = cross(ax, ay))
};
StructuredBuffer<ShadowRec> tShadowRec : register(t26);   // (t22..t25: the smoke's, Smoke.cpp)
Texture2D<float> tSkyVis : register(t21);

bool LightIsSpot(LocalLightG L) { return ((uint)L.c.w & 1) != 0; }
bool LightIsRect(LocalLightG L) { return ((uint)L.c.w & 8) != 0; }

// a rectangle lamp's light kept to its room (a.x: the room's slot in gRoom, < 0 none): 1 inside the box, 0 outside it,
// over the blend at its faces - the panels have no shadow maps, without this they shone through the walls into the next room
float RoomWeight(LocalLightG L, float3 wp)
{
    int r = (int)L.a.x;
    if (r < 0) return 1;
    float4 c = gRoom[r * 4], ax = gRoom[r * 4 + 1], ay = gRoom[r * 4 + 2], az = gRoom[r * 4 + 3];
    float3 q = wp - c.xyz;
    float3 o = abs(float3(dot(q, ax.xyz), dot(q, ay.xyz), dot(q, az.xyz))) - float3(ax.w, ay.w, az.w);
    return saturate(1 - max(o.x, max(o.y, o.z)) / max(c.w, 0.05));
}

// how deep wp lies in the interior volume the camera is in (1 inside, 0 outside, over gVolK.x at its faces): there the sky's
// light is taken out (it comes in through the openings, as the shadows and the sky's visibility give it)
float VolumeWeight(float3 wp)
{
    if (gVolC.w < 0.5) return 0;
    float3 r = wp - gVolC.xyz;
    float3 o = abs(float3(dot(r, gVolX.xyz), dot(r, gVolY.xyz), dot(r, gVolZ.xyz))) - float3(gVolX.w, gVolY.w, gVolZ.w);
    float dmax = max(o.x, max(o.y, o.z));
    return saturate(-dmax / max(gVolK.x, 0.01));
}

// the Sun inside the interior volume: only along rays that leave by a declared opening (a window, a door, a gate) - the
// ray from wp to the Sun against each opening's box (slabs); outside the volume (and over its blend) the Sun as it is
float InteriorSun(float3 wp)
{
    float w = VolumeWeight(wp);
    if (w <= 0) return 1;
    float through = 0;
    [loop] for (int i = 0; i < (int)gVolK.y; i++) {
        float3 c = gOpen[i * 4].xyz;
        float3 ax[3] = { gOpen[i * 4 + 1].xyz, gOpen[i * 4 + 2].xyz, gOpen[i * 4 + 3].xyz };
        float h[3] = { gOpen[i * 4 + 1].w, gOpen[i * 4 + 2].w, gOpen[i * 4 + 3].w };
        float t0 = 0, t1 = 1e5;
        [unroll] for (int k = 0; k < 3; k++) {
            float o = dot(wp - c, ax[k]), d = dot(gSunDir.xyz, ax[k]);
            if (abs(d) < 1e-6) { if (abs(o) > h[k]) t1 = -1; continue; }
            float ta = (-h[k] - o) / d, tb = (h[k] - o) / d;
            t0 = max(t0, min(ta, tb)); t1 = min(t1, max(ta, tb));
        }
        if (t1 >= t0) { through = 1; break; }
    }
    return lerp(1, through, w);
}

// ---- rectangle lamps (Lagarde & de Rousiers 2014, "Moving Frostbite to PBR", 4.7 and appendix E): the diffuse light by the
// polygon's exact form factor (Lambert's edge formula: E = L/2 sum theta_i n.c_i, the vector form factor dotted with the
// normal - not clipped at the horizon, as there), the specular by the most representative point (the reflected ray's hit on
// the rectangle, clamped into it) with the lobe widened by the lamp's size seen from the point and its energy normalised
// ((a/a')^2)
float3 RectCorner(LocalLightG L, int i)
{
    float sx = (i == 1 || i == 2) ? 1 : -1, sy = i >= 2 ? 1 : -1;
    return L.p.xyz + L.d.xyz * sx + L.e.xyz * sy;
}
// the vector form factor of the rectangle seen from wp (pointing at it; |F| = its irradiance / its radiance, x 1/pi in units)
float3 RectFormVector(LocalLightG L, float3 wp)
{
    float3 v[4];
    [unroll] for (int i = 0; i < 4; i++) v[i] = normalize(RectCorner(L, i) - wp);
    float3 F = 0;
    [unroll] for (int j = 0; j < 4; j++) {
        float3 a = v[j], b = v[(j + 1) & 3];
        float c = clamp(dot(a, b), -0.9999, 0.9999);
        float3 x = cross(a, b);
        float lx = length(x);
        if (lx > 1e-6) F += acos(c) * (x / lx);
    }
    F *= 0.5;
    if (dot(F, L.p.xyz - wp) < 0) F = -F;
    return F;
}
// is wp on the lamp's lit side (or the lamp two-sided)
float RectSide(LocalLightG L, float3 wp)
{
    float3 nL = normalize(cross(L.d.xyz, L.e.xyz));
    float s = dot(wp - L.p.xyz, nL);
    return ((uint)L.c.w & 16) != 0 ? 1 : saturate(s * 20);
}
// its illuminance on a surface of normal n at wp [client units]: radiance x n.F / pi
float3 RectIrradiance(LocalLightG L, float3 wp, float3 n)
{
    float3 v = L.p.xyz - wp;
    float r2 = L.p.w * L.p.w, d2 = dot(v, v);
    if (d2 >= r2) return 0;
    float w = saturate(1 - (d2 / r2) * (d2 / r2)); w *= w;
    float side = RectSide(L, wp) * RoomWeight(L, wp);
    if (side <= 0) return 0;
    return L.c.rgb * (max(dot(n, RectFormVector(L, wp)), 0) / 3.14159265 * w * side);
}

// set by mesh.hlsl for the camera's own cockpit (its helmet, its cabin): 1 there, 0 for the world
static float gLLOwn = 0;
bool LocalLightsOn() { return gLZ.z > 0.5 && gLGrid.w > 0.5; }

// the record of light L's shadow map seen from wp: a spot's own, a point light's cube face (the major axis of wp - the lamp)
int ShadowRecFor(LocalLightG L, float3 wp)
{
    int r = (int)L.a.z;
    if (r < 0 || LightIsSpot(L)) return r;
    ShadowRec f = tShadowRec[r];
    float3 v = wp - L.p.xyz;
    float3 az = cross(f.ax.xyz, f.ay.xyz);
    float3 l = float3(dot(v, f.ax.xyz), dot(v, f.ay.xyz), dot(v, az)), al = abs(l);
    int face = al.x >= al.y && al.x >= al.z ? (l.x > 0 ? 0 : 1) : al.y >= al.z ? (l.y > 0 ? 2 : 3) : (l.z > 0 ? 4 : 5);
    return r + face;
}
// the map r's visibility at p: 3 x 3 hardware-compared bilinear taps inside its tile; outside the map: lit
float ShadowTap(int r, float3 p, int taps)
{
    ShadowRec R = tShadowRec[r];
    float4 q = mul(float4(p, 1), R.vp);
    if (q.w <= 1e-4) return 1;
    float3 c = q.xyz / q.w;
    float2 uv = c.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.001) || any(uv > 0.999) || c.z > 1) return 1;
    float2 lo = R.rect.xy + R.rect.w * 1.5, hi = R.rect.xy + R.rect.z - R.rect.w * 1.5;
    float2 a = clamp(R.rect.xy + uv * R.rect.z, lo, hi);
    if (taps <= 1) return tSpotShadow.SampleCmpLevelZero(sShadow, a, c.z);
    if (gSkyK.z < 0.5) {      // (with an air - Titan: the 3 x 3 grid as approved)
        float s9 = 0;
        [unroll] for (int j = -1; j <= 1; j++) [unroll] for (int i = -1; i <= 1; i++)
            s9 += tSpotShadow.SampleCmpLevelZero(sShadow, a + float2(i, j) * R.rect.w, c.z);
        return s9 / 9;
    }
    // airless: 4 hardware-compared bilinear taps half a texel round the point - a 3 x 3 tent's softness at less than half the cost
    const float h = 0.5 * R.rect.w;
    return 0.25 * (tSpotShadow.SampleCmpLevelZero(sShadow, a + float2(-h, -h), c.z) + tSpotShadow.SampleCmpLevelZero(sShadow, a + float2(h, -h), c.z)
                 + tSpotShadow.SampleCmpLevelZero(sShadow, a + float2(-h, h), c.z) + tSpotShadow.SampleCmpLevelZero(sShadow, a + float2(h, h), c.z));
}

// the illuminance [Sun = 1] from light L at wp (direction to it in Ldir), its shadow included (ng: the geometric normal)
float3 LocalLightE(LocalLightG L, float3 wp, float3 ng, out float3 Ldir)
{
    float3 v = L.p.xyz - wp;
    float d2 = dot(v, v);
    Ldir = v * rsqrt(max(d2, 1e-12));
    float r2 = L.p.w * L.p.w;
    if (d2 >= r2) return 0;
    float w = saturate(1 - (d2 / r2) * (d2 / r2)); w *= w;
    float3 E = L.c.rgb * w / max(d2, L.a.y * L.a.y);
    // the lamp's own housing and glass (closer than the spot shadow map's near plane) are not lit by it: the light leaves
    // through the lens - without this they took I / r^2 at centimetres (thousands of Suns) and the bloom made discs of them
    float dd = sqrt(d2);
    E *= LightIsSpot(L) ? smoothstep(0.12, 0.35, dd) : smoothstep(0.02, 0.10, dd);
    uint fl = (uint)L.c.w;
    if (gLLOwn > 0.5 ? (fl & 2) != 0 : (fl & 4) != 0) return 0;     // an outside light on its own helmet / a cockpit light on the world
    if (LightIsSpot(L)) {
        float cd = dot(-Ldir, L.d.xyz);
        if (cd <= L.d.w) return 0;
        float e = smoothstep(L.d.w, L.a.x, cd);
        E *= e * e;
    }
    if (L.a.z >= 0 && dot(E, 1) * gHazeRGB.w > 1e-3) {     // (a light too faint to see here: no shadow lookup)
        float d = sqrt(d2);
        int r = ShadowRecFor(L, wp);
        float tx = tShadowRec[r].k.x * d;
        // the receiver moved off its surface by the map's texel there, more at grazing light (a wall along the beam: the
        // texel spans more depth - the acne that made the hall's walls flicker)
        float gl = saturate(dot(ng, Ldir));
        float off = tx * (1.5 + 1.0 * (1 - gl));     // (4.5 texels at grazing pushed points through the joints: bright saw-teeth)
        E *= ShadowTap(r, wp + ng * off + Ldir * (tx * 0.75), 9);
    }
    return E;
}

// the cluster of wp: (offset, count) into tLIndex; count = 0xFFFFFFFF: wp outside the grid - test every light
uint2 LocalCluster(float3 wp)
{
    float4 q = mul(float4(wp, 1), gLVP);
    if (q.w <= 0.05) return uint2(0, 0xFFFFFFFF);
    float2 ndc = q.xy / q.w;
    if (any(abs(ndc) > 1)) return uint2(0, 0xFFFFFFFF);
    uint2 tile = min((uint2)((ndc * float2(0.5, -0.5) + 0.5) * gLGrid.xy), (uint2)gLGrid.xy - 1);
    uint s = q.w <= gLZ.x ? 0 : min((uint)(1 + log(q.w / gLZ.x) * gLZ.y), (uint)gLGrid.z - 1);
    return tLCluster[(s * (uint)gLGrid.y + tile.y) * (uint)gLGrid.x + tile.x];
}

// how much of the sky a point sees past the vessels round the camera (a cabin's walls see it through the windows only;
// the ground under a vehicle less of it): the vessels' depth seen from 12 directions over the sky, each tested at the
// point, weighted by how the surface faces them (a cosine, softened). Within gSkyC.w of the centre, fading out at the edge
float SkyVisibility(float3 wp, float3 ng)
{
    if (gSkyC.w <= 0) return 1;
    float3 rc = wp - gSkyC.xyz;
    float fade = saturate((gSkyC.w - length(rc)) / (0.2 * gSkyC.w));
    if (fade <= 0) return 1;
    float3 p = wp + ng * (gSkyK.x * 1.5);
    float s = 0, ws = 0;
    // the 6 directions the surface faces most (the others weigh little: (n.d / 2 + 1/2)^2 under ~0.3): half the taps
    float wk[12]; [unroll] for (int j = 0; j < 12; j++) { float t = saturate(dot(ng, gSkyDir[j].xyz) * 0.5 + 0.5); wk[j] = t * t; }
    float th = 0;
    if (gSkyK.z > 0.5) { float m1 = 0; [unroll] for (int j = 0; j < 12; j++) m1 = max(m1, wk[j]); th = m1 * 0.3; }   // (airless only: Titan's look as approved)
    [unroll] for (int i = 0; i < 12; i++) {
        float w = wk[i];
        if (w <= max(th, 0.001)) continue;
        float4 q = mul(float4(p, 1), gSkyVP[i]);
        float2 uv = q.xy * float2(0.5, -0.5) + 0.5;
        float v = 1;
        if (all(uv > 0.004) && all(uv < 0.996) && q.z < 1)
            v = tSkyVis.SampleCmpLevelZero(sShadow, (float2(i & 3, i >> 2) + uv) * 0.25, q.z - 0.6 / (2 * gSkyC.w + 60));   // what lies within 0.6 m of the point is the GTAO's (Ssao.cpp): a body does not shade itself here - no shimmer as it moves over the maps' texels
        s += v * w; ws += w;
    }
    return ws > 0 ? lerp(1, s / ws, fade) : 1;
}

// the lamps' light bounced round an enclosed room (a hall, a hangar, a lock): what a radiosity estimate gives a closed room
// of reflectance rho, E = rho / (1 - rho) x I / (d^2 + R^2) - rho 0.4 (painted walls), R 2 m softening it near the lamp;
// for a spot its flux's share (1 - cos(penumbra / 2)) / 2. Light from all round (no normal, no shadow); the caller weights it
// by how enclosed the point is (1 - the sky's visibility) - in the open the lamps' bounce is the ground's, nearly nothing
// AN APPROXIMATION: no light transport (Frostbite's GI is precomputed - lightmaps, probes); a room-averaged first bounce
float3 LocalFill(float3 wp)
{
    if (!LocalLightsOn() || gSkyK.z < 0.5) return 0;
    float3 s = 0;
    uint2 cl = LocalCluster(wp);
    uint cnt = cl.y == 0xFFFFFFFF ? min((uint)gLGrid.w, 64) : cl.y;
    [loop] for (uint i = 0; i < cnt; i++) {
        LocalLightG L = tLights[cl.y == 0xFFFFFFFF ? i : tLIndex[cl.x + i]];
        uint fl = (uint)L.c.w;
        if (gLLOwn > 0.5 ? (fl & 2) != 0 : (fl & 4) != 0) continue;
        float3 v = L.p.xyz - wp;
        float d2 = dot(v, v), r2 = L.p.w * L.p.w;
        if (d2 >= r2) continue;
        float w = saturate(1 - (d2 / r2) * (d2 / r2)); w *= w;
        float share = LightIsSpot(L) ? 0.5 * (1 - L.d.w) : LightIsRect(L) ? 0.5 : 1;
        s += (LightIsRect(L) ? L.c.rgb * (L.e.w / 3.14159265) : L.c.rgb) * (0.667 * share * w / (d2 + 4));
    }
    return s;
}

float D_GGX_L(float nh, float a) { float a2 = a * a; float d = nh * nh * (a2 - 1) + 1; return a2 / (3.14159265 * d * d + 1e-7); }

// what the local lights add to a surface: albedo (diffuse), F0 and a (GGX roughness^2) for the specular, n the shading
// normal, ng the geometric one, V to the eye. In the client's units (as the Sun's term: albedo x E, spec x E x pi)
float3 RectShade(LocalLightG L, float3 wp, float3 n, float3 V, float3 albedo, float3 F0, float a)
{
    uint fl = (uint)L.c.w;
    if (gLLOwn > 0.5 ? (fl & 2) != 0 : (fl & 4) != 0) return 0;
    float3 E = RectIrradiance(L, wp, n);
    if (dot(E, 1) * gHazeRGB.w < 1e-4) return 0;
    // the specular: the most representative point
    float3 nL = normalize(cross(L.d.xyz, L.e.xyz));
    float3 R = reflect(-V, n);
    float dn = dot(R, nL);
    float3 q;
    float t = abs(dn) > 1e-4 ? dot(L.p.xyz - wp, nL) / dn : -1;
    q = t > 0 ? wp + R * t : wp;                          // the reflected ray's hit on the plane (else the point's projection)
    float hx = length(L.d.xyz), hy = length(L.e.xyz);
    float3 ux = L.d.xyz / hx, uy = L.e.xyz / hy, rq = q - L.p.xyz;
    float3 pstar = L.p.xyz + ux * clamp(dot(rq, ux), -hx, hx) + uy * clamp(dot(rq, uy), -hy, hy);
    float3 lv = pstar - wp; float d2 = max(dot(lv, lv), 1e-4); float3 Ld = lv * rsqrt(d2);
    float nl = saturate(dot(n, Ld));
    float3 diff = albedo * E;
    if (nl <= 0) return diff;
    float d = sqrt(d2);
    float aP = saturate(a + sqrt(L.e.w / 3.14159265) / (2 * d));     // the lobe widened by the lamp's size at d
    float norm = (a / max(aP, 1e-4)) * (a / max(aP, 1e-4));
    float cosE = abs(dot(-Ld, nL)) * (((fl & 16) != 0) ? 1 : step(0, dot(-Ld, nL)));
    float3 Es = L.c.rgb * (L.e.w * cosE / (3.14159265 * d2)) * RoomWeight(L, wp);   // the panel's intensity towards wp over d^2
    float3 H = normalize(V + Ld);
    float nv = saturate(dot(n, V)) + 1e-4, nh = saturate(dot(n, H));
    float3 Fs = F0 + (1 - F0) * pow(1 - saturate(dot(V, H)), 5);
    float k = aP * 0.5, G = (nl / (nl * (1 - k) + k)) * (nv / (nv * (1 - k) + k));
    float3 spec = D_GGX_L(nh, aP) * norm * G * Fs / (4 * nl * nv + 1e-4);
    return diff * (1 - Fs) + spec * Es * nl * 3.14159265;
}

float3 LocalLightShade(LocalLightG L, float3 wp, float3 n, float3 ng, float3 V, float3 albedo, float3 F0, float a)
{
    if (LightIsRect(L)) return RectShade(L, wp, n, V, albedo, F0, a);
    float3 Ld;
    float3 E = LocalLightE(L, wp, ng, Ld);
    float nl = saturate(dot(n, Ld));
    if (nl <= 0 || dot(E, 1) * gHazeRGB.w < 1e-3) return 0;    // too faint to see here (under 1/1000 of white): no GGX
    float3 H = normalize(V + Ld);
    float nv = saturate(dot(n, V)) + 1e-4, nh = saturate(dot(n, H));
    float3 Fs = F0 + (1 - F0) * pow(1 - saturate(dot(V, H)), 5);
    float k = a * 0.5, G = (nl / (nl * (1 - k) + k)) * (nv / (nv * (1 - k) + k));
    float3 spec = D_GGX_L(nh, a) * G * Fs / (4 * nl * nv + 1e-4);
    return E * nl * (albedo * (1 - Fs) + spec * 3.14159265);
}

// one lamp's bounce fill at wp (LocalFill's term)
float3 LightFillOne(LocalLightG L, float3 wp)
{
    uint fl = (uint)L.c.w;
    if (gLLOwn > 0.5 ? (fl & 2) != 0 : (fl & 4) != 0) return 0;
    float3 v = L.p.xyz - wp;
    float d2 = dot(v, v), r2 = L.p.w * L.p.w;
    if (d2 >= r2) return 0;
    float w = saturate(1 - (d2 / r2) * (d2 / r2)); w *= w;
    float share = LightIsSpot(L) ? 0.5 * (1 - L.d.w) : 1;
    float3 I = LightIsRect(L) ? L.c.rgb * (L.e.w / 3.14159265) * RoomWeight(L, wp) : L.c.rgb;   // a rectangle: its intensity along its normal (one hemisphere), in its room
    if (LightIsRect(L)) share = 0.5;
    return I * (0.667 * share * w / (d2 + 4));
}
// the lamps on a surface; encl > 0 (how enclosed the point is, on airless bodies) adds their bounce fill in the same walk
float3 LocalLights(float3 wp, float3 n, float3 ng, float3 V, float3 albedo, float3 F0, float a, float encl)
{
    if (!LocalLightsOn()) return 0;
    float3 s = 0;
    uint2 cl = LocalCluster(wp);
    if (cl.y == 0xFFFFFFFF) {
        uint nAll = min((uint)gLGrid.w, 64);
        [loop] for (uint i = 0; i < nAll; i++) s += LocalLightShade(tLights[i], wp, n, ng, V, albedo, F0, a);
    } else {
        const bool fill = gSkyK.z > 0.5 && encl > 0.01;
        float3 f = 0;
        [loop] for (uint i = 0; i < cl.y; i++) {
            LocalLightG L = tLights[tLIndex[cl.x + i]];
            s += LocalLightShade(L, wp, n, ng, V, albedo, F0, a);
            if (fill) f += LightFillOne(L, wp);
        }
        s += albedo * f * encl;
    }
    return s;
}
// the diffuse part only (the terrain): albedo x the illuminance on the surface
float3 LocalLightsDiffuse(float3 wp, float3 n, float3 ng, float3 albedo)
{
    if (!LocalLightsOn()) return 0;
    float3 s = 0, Ld;
    uint2 cl = LocalCluster(wp);
    uint cnt = cl.y == 0xFFFFFFFF ? min((uint)gLGrid.w, 64) : cl.y;
    [loop] for (uint i = 0; i < cnt; i++) {
        LocalLightG L = tLights[cl.y == 0xFFFFFFFF ? i : tLIndex[cl.x + i]];
        if (LightIsRect(L)) { s += RectIrradiance(L, wp, n); continue; }
        float3 E = LocalLightE(L, wp, ng, Ld);
        s += E * saturate(dot(n, Ld));
    }
    return albedo * s;
}

#endif
