// D3D11Client: the Sun (src\Sun.cpp). One quad round the Sun after the opaque scene, before the air: only where nothing
// was drawn (the sky's depth 0), and the air's pass then dims it along each ray (sunsets from the ground and from orbit).
// Eclipses come out by themselves: a planet or a moon in front hides the disc by depth and leaves the corona round it.
//
// White light (no filter, or the visor's grey filter, flag 0x100):
//  - the photosphere: linear limb darkening per channel (u = 0.58 / 0.69 / 0.81 at ~650 / 550 / 450 nm, Pierce &
//    Slaughter 1977; the whole disc's light white, its centre bluish, the limb red-brown); granulation (~1000 km cells,
//    once a pixel is small enough to show them); sunspot groups (umbra ~0.3, penumbra ~0.8 of the photosphere, the
//    umbra redder at ~4000 K) with their faculae, bright towards the limb;
//  - the chromosphere and prominences at the limb (H-alpha pink), ~1e-5 of the disc: seen only in an eclipse;
//  - the corona (K + F, Baumbach 1937: 1e-6 (0.0532 r^-2.5 + 1.425 r^-7 + 2.565 r^-17) of the disc's mean brightness):
//    near the minimum flattened along the equator with polar plumes, near the maximum round with streamers at all
//    latitudes (Ludendorff); also seen only with the disc hidden.
// H-alpha (a visor filter group with flag 0x200, 656.3 nm): the chromosphere - the network, plage round the groups,
// dark spots and filaments (the prominences seen on the disc), the spicule rim and bright prominences off the limb.
// Activity follows the solar cycle from the simulation date: cycle 25 (minimum 2019.96, maximum late 2024, 11 years),
// the spots' latitudes by Sporer's law (~28 deg at the start down to ~7 deg), the rotation differential (Snodgrass:
// 14.713 - 2.396 sin^2 - 1.787 sin^4 deg/day sidereal), the axis 7.25 deg from the ecliptic's pole (node 75.76 deg).
// The features are procedural (seeded by slot and lifetime): the Sun as it is in kind, not today's real spots.
#include "common.hlsli"

cbuffer SunPass : register(b3)
{
    float4 sDir;     // xyz: towards the Sun (camera space); w: the Sun's angular radius [rad]
    float4 sPole;    // xyz: the Sun's rotation axis (camera space); w: one pixel in solar radii
    float4 sBx;      // xyz: the solar equator's inertial x axis (camera space); w: 1 / sin(angular radius)
    float4 sBz;      // xyz: ... its z axis (prograde from x); w: -
    float4 sTan;     // tan half-fov x, y; the projection's offset (TAA jitter)
    float4 sPar;     // x: the disc's mean brightness (on screen); y: the outer radius drawn [solar radii]; z: the disc's radius [px]; w: -
    float4 sAct;     // x: activity 0..1; y: the spots' mean latitude [rad]; z: days since J2000; w: -
    float4 sMode;    // x: 1 = H-alpha; y: the cycle's phase 0..1; z: minutes (wrapped); w: an occulting disc's radius [solar radii] (0: none)
    float4 sQuad;    // the quad in NDC: centre xy, half size zw
    float4 sGlare;   // the glare: the Sun's centre in NDC (xy), its disc's radius [px], the glare's scale (0: none; a visor filter)
    float4 sScr;     // the scene's width, height [px], MSAA samples, -
    // the real Sun (tools\sun_fetch.py: SDO/HMI or SOHO/MDI, GONG H-alpha, SOHO/LASCO C2 near the simulation's date)
    float4 sRealWl;  // x: the white-light disc map's weight (0: none); y: days from its image to now; z: Earth's longitude
                     //    in the Sun's equatorial frame then [rad] (the map's central meridian); w: -
    float4 sRealHa;  // the same for the H-alpha disc map
    float4 sRealLimb;// x: the H-alpha limb map's weight (near Earth's line of sight, fresh); y: the corona map's weight
};
Texture2D tRealWl : register(t2);     // longitude from the image's central meridian -90..90 x latitude 90..-90: R contrast / 2, G seen
Texture2D tRealHa : register(t3);
Texture2D tRealLimb : register(t4);   // position angle 0..360 (from north through east) x height 0..0.12 radii: R x 4 of the centre
Texture2D tRealC2 : register(t5);     // position angle x radius 2.2..6.0: R brightness / its mean at that radius / 4
SamplerState sRealWrap : register(s0);
SamplerState sRealClamp : register(s1);
Texture2D<float> tSunDepth : register(t0);       // the scene's depth (single-sampled), or ...
Texture2DMS<float> tSunDepthMS : register(t1);   // ... per sample (MSAA: sample 0 is enough for the disc's visibility)

struct VSOut { float4 pos : SV_Position; float2 ndc : NDC; };
VSOut VS(uint id : SV_VertexID)
{
    float2 c = float2((id & 1) ? 1 : -1, (id & 2) ? -1 : 1);
    VSOut o; o.ndc = sQuad.xy + c * sQuad.zw; o.pos = float4(o.ndc, 0, 1);
    return o;
}

uint HashU(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
float Hf(uint x) { return (HashU(x) & 0xFFFFFFu) / 16777216.0; }
float3 H3(int3 i) { uint h = HashU((uint)i.x * 73856093u ^ (uint)i.y * 19349663u ^ (uint)i.z * 83492791u); return float3(Hf(h), Hf(h + 1), Hf(h + 2)); }

float VNoise(float3 p)
{
    int3 i = (int3)floor(p); float3 f = frac(p); f = f * f * (3 - 2 * f);
    float n000 = H3(i).x, n100 = H3(i + int3(1, 0, 0)).x, n010 = H3(i + int3(0, 1, 0)).x, n110 = H3(i + int3(1, 1, 0)).x;
    float n001 = H3(i + int3(0, 0, 1)).x, n101 = H3(i + int3(1, 0, 1)).x, n011 = H3(i + int3(0, 1, 1)).x, n111 = H3(i + int3(1, 1, 1)).x;
    return lerp(lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y), lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y), f.z);
}
float Fbm(float3 p) { return 0.5 * VNoise(p) + 0.3 * VNoise(p * 2.03 + 7.1) + 0.2 * VNoise(p * 4.11 + 3.7); }

// cellular: F2 - F1 (small on the lanes between cells)
float Cells(float3 p)
{
    int3 i = (int3)floor(p); float3 f = frac(p);
    float d1 = 9, d2 = 9;
    [loop] for (int z = -1; z <= 1; z++) [loop] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
        int3 o = int3(x, y, z);
        float3 d = o + H3(i + o) - f; float dd = dot(d, d);
        if (dd < d1) { d2 = d1; d1 = dd; } else if (dd < d2) d2 = dd;
    }
    return sqrt(d2) - sqrt(d1);
}

static const float PI = 3.14159265;

// the Sun's inertial frame (its axis, the equator's node): latitude and longitude of the unit vector n (camera space)
void Helio(float3 n, out float lat, out float lam)
{
    lat = asin(clamp(dot(n, sPole.xyz), -1, 1));
    lam = atan2(dot(n, sBz.xyz), dot(n, sBx.xyz));
}
float3 RotVec(float lat, float lam) { return float3(cos(lat) * cos(lam), sin(lat), cos(lat) * sin(lam)); }
float3 ToCam(float lat, float lam) { return cos(lat) * (cos(lam) * sBx.xyz + sin(lam) * sBz.xyz) + sin(lat) * sPole.xyz; }
// the longitude a feature born at lon has turned to after days d at its latitude (differential rotation, sidereal; each
// feature turns only over its own life - counted from one epoch the shear wound every feature into a thin arc)
float Turned(float lon, float lat, float d)
{
    float s2 = sin(lat); s2 *= s2;
    return lon + radians(14.713 - 2.396 * s2 - 1.787 * s2 * s2) * d;
}

// one feature slot alive now: its seed, age 0..1 and life [days] (it lives tau days, then the slot takes a new one)
bool Slot(uint k, float tau0, float tau1, float pAlive, out uint sd, out float age, out float tau)
{
    tau = tau0 + tau1 * Hf(k * 7u + 1u);
    float tt = sAct.z / tau + Hf(k * 7u + 2u);
    uint e = (uint)floor(tt);
    age = frac(tt);
    sd = HashU(k * 0x9E3779B9u + e * 0x85EBCA6Bu);
    return Hf(sd) < pAlive;
}

// a spot's profile at x = distance / radius: crisp when large on screen, spread to its area when under a pixel
float Blob(float x, float r0, float wpx)
{
    float crisp = exp(-pow(x / r0, 8));
    float s0 = 0.6 * r0, s2 = s0 * s0 + wpx * wpx;
    float soft = exp(-x * x / (2 * s2)) * (s0 * s0 / s2);
    return lerp(crisp, soft, saturate(wpx / r0 * 2));
}

// a disc map's contrast at the heliographic point (lat, lam: inertial longitude now) and how much it holds there: the
// point turned back by the differential rotation to the image's time, its longitude from the image's central meridian
float2 RealDisc(Texture2D t, float4 p, float lat, float lam)
{
    if (p.x <= 0) return float2(1, 0);
    float s2 = sin(lat); s2 *= s2;
    float lon = lam - radians(14.713 - 2.396 * s2 - 1.787 * s2 * s2) * p.y - p.z;
    lon = lon - 2 * PI * floor((lon + PI) / (2 * PI));            // -pi..pi
    if (abs(lon) > 0.5 * PI) return float2(1, 0);                  // the far side then
    float4 m = t.SampleLevel(sRealClamp, float2(lon / PI + 0.5, 0.5 - lat / PI), 0);
    return float2(m.r * 2, m.g * p.x);
}

#define NSPOT 36
#define NPROM 20
#define NSTREAM 9

// sunspots and faculae at the rotating-frame point r: the photosphere's factor (rgb) and the facular excess (0..)
void Spots(float3 r, float wpix, bool ha, out float3 f, out float fac, out float plage)
{
    f = 1; fac = 0; plage = 0;
    const float3 PEN = float3(0.84, 0.78, 0.72), UMB = float3(0.38, 0.24, 0.16);
    [loop] for (uint k = 0; k < NSPOT; k++) {
        uint sd; float age, tau;
        if (!Slot(k, 14, 45, sAct.x * 0.85 + 0.03, sd, age, tau)) continue;
        float hemi = Hf(sd + 1u) < 0.5 ? -1 : 1;
        float lat = hemi * max(sAct.y + radians(14) * (Hf(sd + 2u) - 0.5), radians(3));
        float lon = Turned(2 * PI * Hf(sd + 3u), lat, age * tau);
        float grow = sqrt(sin(PI * age));
        float sz = radians(0.6 + 3.0 * pow(Hf(sd + 4u), 2)) * grow;
        float sep = radians(3 + 7 * Hf(sd + 5u));
        float3 cg = RotVec(lat, lon - 0.5 * sep);
        float dg = length(r - cg);
        if (dg > sep * 1.5 + sz * 6 + 0.06) continue;      // far from this group (faculae reach a few radii)
        [unroll] for (int j = 0; j < 3; j++) {
            if (j == 2 && Hf(sd + 9u) < 0.5) continue;        // some groups have a third, fragment spot
            float lj = lon - (j == 0 ? 0 : (j == 1 ? sep : sep * 0.5));
            float laj = lat + hemi * (j == 0 ? 0 : radians(1.5 + 2 * Hf(sd + 6u)));   // Joy's law: the leader nearer the equator
            float rj = sz * (j == 0 ? 1 : (j == 1 ? 0.55 + 0.4 * Hf(sd + 7u) : 0.3));
            float x = length(r - RotVec(laj, lj)) / max(rj, 1e-5);
            float w = wpix / max(rj, 1e-5);
            float pen = Blob(x, 1.0, w), umb = Blob(x, 0.42, w);
            float3 v = lerp(1, PEN, pen);
            v = lerp(v, UMB, umb);
            f *= ha ? lerp(1, 0.35, umb) * lerp(1, 0.85, pen) : v;
            fac += 0.9 * exp(-pow((x - 2.2) / 1.6, 2));
            plage += exp(-x * x / 14);
        }
    }
}

// a prominence's emission at the plane-of-sky point q (solar radii, x east-west, y north), or its filament on the disc
// (front: what lies in front of the disc); E2 / N2: the plane of the sky's axes; S: towards the Sun
float Prominences(float2 q, float3 E2, float3 N2, float3 S, bool onDisc, out float filament)
{
    float em = 0; filament = 0;
    [loop] for (uint k = 0; k < NPROM; k++) {
        uint sd; float age, tau;
        if (!Slot(k + 101u, 10, 50, 0.45 + 0.45 * sAct.x, sd, age, tau)) continue;
        float hemi = Hf(sd + 1u) < 0.5 ? -1 : 1;
        bool polar = Hf(sd + 2u) < 0.3 + 0.3 * sAct.x;      // the polar crown (towards the maximum) or the active belts
        float lat = hemi * (polar ? radians(48 + 18 * Hf(sd + 3u)) : sAct.y + radians(12 * Hf(sd + 3u)));
        float lon = Turned(2 * PI * Hf(sd + 4u), lat, age * tau);
        float len = radians(5 + 16 * Hf(sd + 5u));
        float H = (0.03 + 0.09 * pow(Hf(sd + 6u), 1.5)) * sqrt(sin(PI * age));
        float psi = 2 * PI * Hf(sd + 7u);
        float3 n = ToCam(lat, lon);
        float3 nE = normalize(cross(sPole.xyz, n) + 1e-6), nN = cross(n, nE);
        float3 t = cos(psi) * nE + sin(psi) * nN;           // the filament's direction along the surface
        float3 tp = cross(n, t);                             // ... across it: the filament bends (an S or an arc)
        float bend = (Hf(sd + 8u) - 0.5) * 0.7, bend2 = (Hf(sd + 9u) - 0.5) * 0.5;
        // quick reject: the whole prominence lies within ~len + H of its middle
        float2 m = float2(dot(n, E2), dot(n, N2));
        if (length(q - m) > len + H + 0.03) continue;
        float z = dot(n, -S);                                // > 0: in front of the disc
        if (onDisc && z <= 0) continue;
        // the sheet: 9 threads from its foot line up to its top, the pixel taken on the thread at its nearest point along
        // the foot line (between threads, interpolated: a continuous sheet, not a row of beads)
        float2 Fp = 0, Tp = 0; float dBest = 1e9, vBest = 0, sBest = 0;
        [loop] for (int j = 0; j < 9; j++) {
            float u = j / 4.0 - 1;
            float3 f = normalize(n + u * len * t + len * (bend * (u * u - 0.33) + bend2 * u * u * u) * tp);
            float h = H * sqrt(max(1 - u * u, 0)) * (0.65 + 0.7 * Hf(sd + 11u + j));
            float2 F = float2(dot(f, E2), dot(f, N2)), T = F * (1 + h);
            if (j > 0) {
                float2 e = F - Fp; float s0 = saturate(dot(q - Fp, e) / max(dot(e, e), 1e-12));
                float2 Fs = lerp(Fp, F, s0), Ts = lerp(Tp, T, s0), ft = Ts - Fs;
                float v = saturate(dot(q - Fs, ft) / max(dot(ft, ft), 1e-12));
                float d = length(q - Fs - v * ft);
                if (d < dBest) { dBest = d; vBest = v; sBest = (j - 1 + s0) / 8.0; }
            }
            Fp = F; Tp = T;
        }
        float w = onDisc ? (len * 0.025 + 0.003) * (0.4 + 1.2 * Fbm(float3(sBest * 6, k * 1.7, 0))) : 0.004 + 0.02 * H;
        float sheet = exp(-dBest * dBest / (w * w));
        if (onDisc)       // a thin, ragged dark thread, tapered at its ends, broken in places
            filament = max(filament, sheet * smoothstep(0, 0.25, sBest) * smoothstep(1, 0.75, sBest) * saturate(0.3 + 1.2 * Fbm(float3(sBest * 9, k * 2.3, 1))));
        else {
            float turb = 0.35 + 1.0 * Fbm(float3(sBest * 40, vBest * 6, sMode.z * 0.02 + k * 3.1));     // threads along the height
            em += sheet * turb * (1 - 0.55 * vBest) * smoothstep(0, 0.12, vBest + 0.03);
        }
    }
    return em;
}

// the corona's shape (1 on average) at position angle sinB (the projected latitude) and radius b
float Corona(float2 q, float b, float3 E2, float3 N2, float3 S)
{
    float sinB = q.y / b, cosB = q.x / b;
    // near the minimum: the streamer belt along the equator, plumes at the poles
    float belt = exp(-pow(sinB / (0.30 + 0.25 / b), 2));
    float pa = atan2(sinB, cosB);
    float plumes = smoothstep(0.75, 0.95, abs(sinB)) * (0.5 + 1.2 * pow(VNoise(float3(pa * 60, b * 0.3, 1.7)), 3));
    float smin = 0.30 + 1.9 * belt + 0.35 * plumes;
    // near the maximum: helmet streamers at all latitudes, rotating with the Sun
    float smax = 0.55;
    [loop] for (uint k = 0; k < NSTREAM; k++) {
        uint sd; float age, tau;
        if (!Slot(k + 211u, 20, 40, 0.9, sd, age, tau)) continue;
        float lat = (Hf(sd + 1u) - 0.5) * 2 * radians(70) * sAct.x;
        float3 n = ToCam(lat, Turned(2 * PI * Hf(sd + 2u), lat, age * tau));
        float2 m = float2(dot(n, E2), dot(n, N2));
        float lm = length(m); if (lm < 0.05) continue;
        float da = acos(clamp(dot(q / b, m / lm), -1, 1));
        float w = 0.10 * pow(1.6 / max(b, 1), 0.6) + 0.025;        // the helmet narrows into a ray outwards
        smax += (0.6 + 0.8 * Hf(sd + 3u)) * lm * exp(-da * da / (w * w)) * sin(PI * age);
    }
    float s = lerp(smin, smax, saturate(sAct.x * 1.2));
    // fine radial striation, curving slightly with height
    float str = VNoise(float3(pa * 90 + 0.4 * b, b * 0.15, 5.3));
    return s * (0.75 + 0.5 * str);
}

// ---- the glare: the light the eye (or a lens) scatters round a bright source, over everything in the view ----------
// Veiling glare, CIE (Stiles-Holladay / Vos 2003): L_veil = 10 E / theta^2 [cd/m2 per lux, theta in degrees] - on screen
// (a white surface under the Sun = 1, i.e. pi L / E) 31.4 / theta^2 times the part of the Sun's light reaching the eye:
// the disc's visible share (its depth sampled at 13 points: hidden behind a ship, a planet) and the air's transmittance
// towards the Sun. With it the ciliary corona - fine radial needles from the lens' fibres - and the lenticular halo, a
// faint coloured ring at ~2 deg (red outside). A visor filter over the Sun takes the glare with it (sGlare.w = 0).
struct GOut { float4 pos : SV_Position; float2 ndc : NDC; nointerpolation float k : K; };
float DiscSeen(float2 ndc)
{
    float2 px = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * sScr.xy;
    int2 p = (int2)clamp(px, 0, sScr.xy - 1);
    float d = sScr.z > 1.5 ? tSunDepthMS.Load(p, 0) : tSunDepth.Load(int3(p, 0));
    return d <= 0 ? 1.0 : 0.0;                    // reverse Z: depth 0 is the sky
}
GOut VSG(uint id : SV_VertexID)
{
    float2 p = float2((id << 1) & 2, id & 2);
    GOut o; o.ndc = p * float2(2, -2) + float2(-1, 1); o.pos = float4(o.ndc, 0, 1);
    // the disc's visible share
    float2 rN = sGlare.z * 2 / sScr.xy;           // its radius in NDC
    float vis = DiscSeen(sGlare.xy);
    [unroll] for (int k = 0; k < 12; k++) {
        float a = k * (2 * PI / 12), f = (k & 1) ? 0.85 : 0.45;
        vis += DiscSeen(sGlare.xy + float2(cos(a), sin(a)) * rN * f);
    }
    vis /= 13;
    o.k = vis * sGlare.w;
    return o;
}
float4 PSG(GOut i) : SV_Target
{
    if (i.k <= 0) discard;
    float3 v = normalize(float3((i.ndc.x - sTan.z) * sTan.x, (i.ndc.y - sTan.w) * sTan.y, 1));
    float th = degrees(acos(clamp(dot(v, sDir.xyz), -1, 1)));
    float th0 = max(degrees(sDir.w), 0.05);
    if (th < th0) discard;                         // the disc itself is the Sun pass's
    float veil = 31.4 / (th * th + th0 * th0);
    // the ciliary corona: needles away from the Sun, ~150 round, flickering slowly with the eye's tremor
    float2 dd = v.xy / max(v.z, 1e-3) - sDir.xy / max(sDir.z, 1e-3);
    float ang = atan2(dd.y, dd.x);
    float n = VNoise(float3(ang * 24, 0.5, sMode.z * 0.3)) * 0.6 + VNoise(float3(ang * 60, 3.1, sMode.z * 0.5)) * 0.4;
    float needles = pow(saturate(n), 6) * 1.5 * smoothstep(14, 1, th);
    // the lenticular halo: a ring at ~2 deg, its colours spread (red outside, blue inside)
    float3 rad = float3(2.25, 2.0, 1.78);
    float3 halo = 0.06 * exp(-pow((th - rad) / 0.18, 2));
    float3 g = veil * ((1 + needles) * float3(1, 0.98, 0.95) + halo * th * th / 4);
    // the air between the eye and the Sun (inside a planet's air; in space 1) - read here: the light's table (t8, b4) is
    // bound for the pixel shaders only (in the VS it read 0: no glare at all in any air)
    float3 T = 1;
    float rc = length(mCentre.xyz);
    if (FxActive() && rc < mGeo.x) T = FxTransToSun(rc, dot(-mCentre.xyz, mSunDir.xyz) / rc);
    g *= i.k * T;
    if (NotFinite(g.x + g.y + g.z)) g = 0;
    return float4(min(g, 3.0e4), 0);
}

float4 PS(VSOut i) : SV_Target
{
    float3 v = normalize(float3((i.ndc.x - sTan.z) * sTan.x, (i.ndc.y - sTan.w) * sTan.y, 1));
    float3 S = sDir.xyz;
    float c = dot(v, S);
    if (c <= 0) discard;
    const float D = sBx.w, dr = sPole.w;
    float3 E2, N2;
    N2 = normalize(sPole.xyz - dot(sPole.xyz, S) * S);
    E2 = cross(N2, S);
    float2 q = float2(dot(v, E2), dot(v, N2)) * D / c;    // the plane of the sky [solar radii]
    float b = length(cross(v, S)) * D;                    // the impact parameter [solar radii]
    if (b > sPar.y) discard;
    const bool ha = sMode.x > 0.5;
    const float Bm = sPar.x;
    float3 col = 0;
    if (sMode.w > 0 && b < max(sMode.w, 1 + 1.5 * sPole.w)) return 0;    // a coronagraph's occulting disc (past the limb's pixel)

    // a disc under a pixel: its light in a small spot (the same energy)
    if (sPar.z < 1.5) {
        float sg = 0.6 * dr;
        col = Bm / (2 * sg * sg) * exp(-b * b / (2 * sg * sg)) * (ha ? float3(0.25, 0.02, 0.01) : 1);
        return float4(col, 0);
    }

    float cov = saturate((1 - b) / dr + 0.5);             // the limb within this pixel
    if (cov > 0) {
        float t = D * c - sqrt(max(1 - b * b, 0));
        float3 n = normalize(v * t - S * D);              // the surface's normal
        float mu = saturate(dot(n, -v));
        float lat, lam; Helio(n, lat, lam);
        float3 r = RotVec(lat, lam);
        float wpix = dr / max(mu, 0.08);                  // a pixel on the surface here [solar radii], foreshortened
        float3 f; float fac, plage;
        Spots(r, wpix, ha, f, fac, plage);
        float3 disc;
        const float2 realWl = RealDisc(tRealWl, sRealWl, lat, lam);
        const float2 real = ha ? RealDisc(tRealHa, sRealHa, lat, lam) : realWl;
        if (!ha) {
            static const float3 U = float3(0.58, 0.69, 0.81);
            float3 I0 = Bm / (1 - U / 3);
            disc = I0 * (1 - U * (1 - mu));
            // granulation: ~1000 km cells (0.0014 radii) that live ~8 minutes
            float gcell = 0.0014;
            float gw = saturate((gcell / wpix - 1.5) * 0.5);
            if (gw > 0) {
                float g = Cells(float3(r / gcell * 0.7) + float3(0, 0, sMode.z / 8));
                disc *= lerp(1, 0.86 + 0.24 * smoothstep(0.0, 0.35, g), gw);
            }
            float limb = pow(1 - mu, 1.5);
            float net = VNoise(r * 25);                   // the network's faculae, near the limb only
            float3 proc = f * (1 + limb * (0.28 * saturate(fac) + 0.08 * net * (0.4 + sAct.x)));
            // the real disc (HMI 617 nm, MDI 677 nm): its contrast, deeper towards blue in the spots (cooler: ~4000 K)
            float3 cr = pow(max(real.x, 0.02), float3(0.95, 1.1, 1.35));
            disc *= lerp(proc, cr, real.y);
        } else {
            float Iha = 0.8 * Bm * (1 - 0.3 * (1 - mu));
            // the chromospheric network (supergranules ~30 000 km), its cells warped, soft-edged
            float3 rw = r + 0.022 * (float3(VNoise(r * 60), VNoise(r * 60 + 5.3), VNoise(r * 60 + 9.1)) - 0.5);
            float nw = Cells(rw / 0.035);
            float net = 0.93 + 0.14 * (1 - smoothstep(0.0, 0.6, nw)) * saturate(0.0275 / wpix - 0.5);
            // mottles and fibrils: dark and bright threads ~2000 km, changing over minutes
            float fib = 0.8 + 0.4 * Fbm(rw * 300 + float3(0, 0, sMode.z / 20));
            float fil; Prominences(q, E2, N2, S, true, fil);
            // where the white-light image holds, its spots stand for the made-up ones (and no made-up plage or filaments)
            float wlw = ha ? realWl.y : 0;
            float spot = lerp(f.x, pow(saturate(realWl.x), 2.5), wlw);
            float proc = net * fib * (1 + 1.1 * saturate(plage) * (1 - wlw)) * spot * (1 - 0.75 * fil * (1 - wlw));
            disc = Iha * lerp(proc, real.x * (0.9 + 0.2 * fib), real.y) * float3(1, 0.07, 0.03);   // GONG's own, with fine threads
        }
        col += disc * cov;
    }
    if (cov < 1) {
        float o = 1 - cov;
        float h = max(b - 1, 0);
        // the chromosphere's rim and its spicules (up to ~10 000 km), then the prominences
        float pa = atan2(q.y, q.x);
        float spic = exp(-h / 0.0035) + 0.6 * exp(-h / 0.011) * VNoise(float3(pa * 700, 0, sMode.z * 0.2));
        float fil;
        float prom = b < 1.25 ? Prominences(q, E2, N2, S, false, fil) : 0;
        // the real limb and corona (seen from near Earth, fresh): the position angle from north through east - east is
        // where the rotation brings features in (the opposite of the visible surface's motion)
        float paNE = 0;
        if (sRealLimb.x > 0 || sRealLimb.y > 0) {
            float3 n0 = -S;
            float3 W3 = -dot(n0, sBz.xyz) * sBx.xyz + dot(n0, sBx.xyz) * sBz.xyz;   // the surface's motion at the disc's centre
            float2 east = -normalize(float2(dot(W3, E2), dot(W3, N2)) + 1e-9);
            paNE = atan2(dot(q, east), q.y);
            paNE += paNE < 0 ? 2 * PI : 0;
        }
        if (sRealLimb.x > 0 && h < 0.12) {
            float rl = tRealLimb.SampleLevel(sRealWrap, float2(paNE / (2 * PI), h / 0.12), 0).r / 4;
            prom = lerp(prom, max(rl - 0.01, 0) * 6, sRealLimb.x);
        }
        float3 pink = float3(1.0, 0.30, 0.48);
        if (ha) col += o * Bm * (0.45 * spic + 0.9 * prom) * float3(1, 0.07, 0.03);
        else {
            col += o * Bm * 1.5e-5 * (spic + 0.8 * prom) * pink;
            float k = 0.0532 * pow(b, -2.5) + 1.425 * pow(b, -7) + 2.565 * pow(b, -17);
            float fade = smoothstep(sPar.y, sPar.y * 0.6, b);
            float shape = Corona(q, max(b, 1), E2, N2, S);
            float wc = sRealLimb.y * smoothstep(2.2, 2.7, b) * smoothstep(6.0, 5.5, b);
            if (wc > 0) shape = lerp(shape, tRealC2.SampleLevel(sRealWrap, float2(paNE / (2 * PI), (b - 2.2) / 3.8), 0).r * 4, wc);
            col += o * Bm * 1e-6 * k * shape * fade * float3(1, 0.97, 0.92);
        }
    }
    if (NotFinite(col.x + col.y + col.z)) col = 0;
    return float4(col, 0);
}
