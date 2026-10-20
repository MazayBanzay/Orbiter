// D3D11Client: Earth's storm - one tropical cyclone (Storm.cpp, Config\D3D11Client\Storm.cfg). Written from scratch for
// the client; Titan's clouds (clouds.hlsl) are a separate system and are not used here.
//
//  - the tiling 3D noises (Perlin-Worley + Worley octaves; Worley octaves) are made once on the GPU (storm_noise.hlsl);
//  - PSMap: the weather map round the storm's centre (azimuthal-equidistant, ~1 km a texel): r low/mid cover, g the
//    cloud tops, b how convective, a the cirrus canopy. Eye, eyewall, spiral rain bands, the central dense overcast and
//    its anticyclonic outflow streaks, the outer cumulus fields. The bands turn slowly as a whole; the small structure
//    is carried round by the vortex's own wind (two flow phases, cross-faded: no endless winding);
//  - PSShadow: the clouds' shadow on everything drawn so far (before the air): the Sun's ray through both layers;
//  - PSMarch: one march through the volume at half resolution. From afar the map alone shapes the cloud (the 2D look
//    from orbit); nearer the 3D noise cuts it into cells and eats the edges; inside a cloud the march starts at the eye;
//  - PSComposite: the half-res result over the full-res scene through its depth.
// Light: the Sun through the air (the FX atmosphere's transmittance) and through the cloud (a short march towards the Sun
// plus the canopy's own crossing), multiple-scattering octaves, the dual-lobe phase of the drops; the sky's light, shut
// out by the cloud above. The air between the eye and the cloud: the FX atmosphere's own march.
#include "common.hlsli"
#include "fx_atmo_sky.hlsli"

cbuffer Storm : register(b1)
{
    float4x4 sInvVP;   // clip -> camera-centred global
    float4 sRot0, sRot1, sRot2;   // global -> planet frame (rows); w: the camera in the planet frame [km]
    float4 sCen;       // the storm's centre (unit, planet frame); w: the planet's radius [m]
    float4 sEast;      // east at the centre (planet frame); w: the map's half size [m]
    float4 sNorth;     // north at the centre; w: the time [s] (wrapped)
    float4 sCenG;      // the centre (unit, global); w: cos of the storm cap's half angle
    float4 sShape;     // eye radius, eyewall width, canopy radius, outer radius [m]
    float4 sHgt;       // low base, highest top, canopy base, canopy top [m]
    float4 sSig;       // extinction [1/m]: convective core, stratiform, canopy; w: the hemisphere (+1 north, -1 south)
    float4 sAnim;      // x: the bands' angle [rad]; y, z: the flow phases' rotation times [s]; w: phase B's weight
    float4 sP;         // x: steps, y: frame, z: debug view, w: distance the 3D detail fades out at [m]
    float4 sSize;      // the half-res target: w, h, 1/w, 1/h
    float4x4 sPrevVP;  // last frame: its camera-centred global -> clip
    float4 sPr0, sPr1, sPr2;   // last frame: planet frame -> global (rows); w: the planet's centre from last frame's camera
    float4 sTmp;       // x: history valid, y: the new frame's weight
};

Texture3D tShape : register(t0);          // 128^3 over SHAPE_M: r Perlin-Worley, g b a Worley octaves
Texture3D tDetail : register(t1);         // 32^3 over DETAIL_M: Worley octaves
Texture2D tMap : register(t2);            // the weather map (mips)
Texture2D<float> tDepth : register(t3);   // the scene's depth (resolved)
SamplerState sWrap : register(s0);
SamplerState sClampS : register(s1);

static const float SHAPE_M = 9000.0;      // the shape noise's tile [m]
static const float DETAIL_M = 1300.0;     // the detail noise's tile [m]
static const float PI = 3.14159265;

// ------------------------------------------------------------------------------------------------ hashing and noise
uint Hash3(uint3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    return v.x ^ v.y ^ v.z;
}
float Remap(float x, float a, float b, float c, float d) { return c + (x - a) * (d - c) / max(b - a, 1e-5); }

// 2D gradient noise and fBm for the map (not tiling; gradient, not value noise: no square cells seen from orbit)
float Grad2(int2 c, float2 f)
{
    uint h = Hash3(uint3(c + 32768, 17));
    float a = (h & 65535u) / 65535.0 * 6.2831853;
    return dot(float2(cos(a), sin(a)), f);
}
float Noise2(float2 p)
{
    int2 i = (int2)floor(p);
    float2 f = p - i;
    float2 u = f * f * f * (f * (f * 6 - 15) + 10);
    float a = Grad2(i, f), b = Grad2(i + int2(1, 0), f - float2(1, 0));
    float c = Grad2(i + int2(0, 1), f - float2(0, 1)), d = Grad2(i + int2(1, 1), f - float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float Fbm2(float2 p, int oct)      // ~0.5 +- 0.25
{
    float s = 0, a = 0.5, n = 0;
    [loop] for (int o = 0; o < oct; o++) { s += a * Noise2(p); n += a; p = mul(float2x2(1.6, 1.2, -1.2, 1.6), p) + 7.31; a *= 0.5; }
    return 0.5 + s / n * 0.9;
}

// ------------------------------------------------------------------------------------------------ fullscreen triangle
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint vid : SV_VertexID)
{
    VSOut o;
    float2 c = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(c * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = c;
    return o;
}

// ------------------------------------------------------------------------------------------------ the weather map
// the vortex's tangential wind [m/s] at r (a modified Rankine vortex: solid rotation inside the radius of the strongest
// wind, falling as r^-0.6 outside)
float VortexWind(float r)
{
    float rm = sShape.x + 0.5 * sShape.y, vmax = 62;
    return r < rm ? vmax * r / rm : vmax * pow(rm / r, 0.6);
}
// the map's channels at xy [m] from the centre (east, north)
float4 StormMap(float2 xy)
{
    float r = length(xy) + 1;
    float hs = sSig.w;                         // +1: the northern hemisphere (counter-clockwise)
    float phi = atan2(xy.y, xy.x) * hs;        // the angle with the flow
    float re = sShape.x, ww = sShape.y, rc = sShape.z, ro = sShape.w;
    // the small structure carried by the wind: each phase turned by omega(r) * its time, the two cross-faded
    float om = VortexWind(r) / r;
    float2 cs0, cs1;
    sincos(-om * sAnim.y * hs, cs0.y, cs0.x);
    sincos(-om * sAnim.z * hs, cs1.y, cs1.x);
    float2 xa = float2(xy.x * cs0.x - xy.y * cs0.y, xy.x * cs0.y + xy.y * cs0.x);
    float2 xb = float2(xy.x * cs1.x - xy.y * cs1.y, xy.x * cs1.y + xy.y * cs1.x);
    float nA = Fbm2(xa / 40000.0, 6), nB = Fbm2(xb / 40000.0 + 13.7, 6);
    float n = lerp(nA, nB, sAnim.w);           // the cells and gaps, 10-40 km
    float nAc = Fbm2(xa / 9000.0 + 3.1, 5), nBc = Fbm2(xb / 9000.0 + 5.9, 5);
    float nc = lerp(nAc, nBc, sAnim.w);        // the fine break-up, 2-9 km
    float warp = Fbm2(xy / 180000.0 + 2.3, 4) - 0.5;   // the bands' large wobble
    float lr = log(r / 50000.0);
    float ang = phi - sAnim.x;                 // the band pattern, turning slowly as a whole

    // the eye and the eyewall (the strongest convection: the towers up to the tropopause)
    float eyeIn = 1 - smoothstep(re * 0.85, re * 1.1, r);
    float wx = (r - re - 0.45 * ww) / (0.55 * ww); float wall = exp(-wx * wx);
    wall *= 0.8 + 0.4 * Fbm2(float2(ang * 3.0, lr * 4.0) + 1.7, 3);   // brighter and darker sectors
    // the spiral rain bands: phase constant along phi + k ln r (log spirals pitched ~17 degrees), two arms, the principal
    // one stronger on one side (the storm's motion and the shear make it lopsided)
    float k = 1 / tan(17 * PI / 180);
    float psi = ang + k * lr + warp * 2.0;
    float bandRaw = pow(saturate(0.5 + 0.5 * cos(2 * psi)), 4.0);
    float band2 = pow(saturate(0.5 + 0.5 * cos(3 * psi + 1.3 + warp * 3.0)), 8.0) * 0.5;
    float side = 0.75 + 0.35 * cos(ang - 0.6);
    // a ragged, lopsided outline (not a ring): the outer radius by direction, a long tail to the south-east
    float2 cdir0; sincos(phi, cdir0.y, cdir0.x);
    float fE = 0.7 + 0.6 * (Fbm2(cdir0 * 1.3 + 21.7, 4) - 0.5) + 0.3 * max(0, cos(phi + 0.8));
    float rE = r / max(fE, 0.3);
    float envB = smoothstep(re + 2 * ww, re + 4 * ww, r) * (1 - smoothstep(ro * 0.35, ro * 0.8, rE));
    float brk = r > rc * 0.8 ? 0.35 + 0.65 * smoothstep(0.4, 0.6, n * 0.6 + nc * 0.4) : 1;   // outside: chains of cells
    float bands = max(bandRaw, band2) * side * envB * brk;
    // the moat round the eyewall, the stratiform rain between the bands under the canopy
    float moat = 1 - 0.7 * smoothstep(re + ww, re + 1.6 * ww, r) * (1 - smoothstep(re + 2.5 * ww, re + 4 * ww, r));
    float strat = (1 - smoothstep(rc * 0.6, rc, r)) * smoothstep(re + ww, re + 3 * ww, r) * moat;
    // the outer cumulus, mostly along the arms (the inflow)
    float outerCu = smoothstep(rc * 0.7, rc * 1.1, r) * (1 - smoothstep(ro * 0.5, ro * 0.9, rE)) * (0.02 + 0.8 * bandRaw * side);

    float eyeCu = eyeIn * 0.3 * smoothstep(0.62, 0.75, nc);       // a few low stratocumulus at the eye's floor
    float wallC = saturate(wall * 1.5);
    float bandC = saturate(bands * (0.75 + 0.9 * (n - 0.4)) * 1.5);
    float stratC = strat * saturate(0.45 + 0.9 * (n - 0.5));
    float cuC = outerCu * smoothstep(0.5, 0.68, nc * 0.7 + n * 0.3);
    float cov = saturate(max(max(max(eyeCu, wallC), bandC), max(stratC, cuC)));
    // tops: the eyewall to the tropopause, the bands' towers 6.5-13 km, the stratiform rain 5-7.5 km, the cumulus 1.5-4 km
    float tW = lerp(12500, sHgt.y, saturate(wall * (0.6 + 0.8 * nc)));
    float tB = lerp(6500, 13000, saturate(bands * (0.5 + nc))) * (1 - 0.35 * smoothstep(rc, ro, r));
    float tS = lerp(5000, 7500, n);
    float tC = lerp(1500, 4200, saturate((nc - 0.45) * 3)) * (1 - 0.4 * smoothstep(rc, ro, r));
    float wSum = wallC + bandC + stratC + cuC + eyeCu + 1e-4;
    float top = (tW * wallC + tB * bandC + tS * stratC + tC * cuC + 1800 * eyeCu) / wSum;
    float conv = (wallC + 0.9 * bandC + 0.8 * cuC + 0.5 * eyeCu) / wSum;

    // the cirrus canopy (the central dense overcast) and its outflow: thin fibres spiralling out anticyclonically
    float2 cdir; sincos(ang, cdir.y, cdir.x);
    float edge = rc * (0.85 + 0.4 * (Fbm2(cdir * 1.5 + 9.1, 4) - 0.5) + 0.25 * warp);
    float cdo = 1 - smoothstep(edge * 0.85, edge * 1.05, r);
    float psiO = ang - 0.9 * k * lr + warp * 1.5;
    float streak = pow(saturate(0.5 + 0.5 * cos(23 * psiO + 9 * (Fbm2(xy / 60000.0 + 4.4, 3) - 0.5))), 1.5) * Fbm2(xy / 25000.0 + 1.1, 3);
    float outflow = smoothstep(edge * 0.8, edge, r) * (1 - smoothstep(edge * 1.15, edge * 1.8, r)) * streak;
    outflow *= smoothstep(0.35, 0.65, Fbm2(xy / 40000.0 + 8.2, 4));         // broken, not combed
    float can = saturate(max(cdo * (0.85 + 0.3 * (n - 0.5)), outflow * 0.22));
    can *= smoothstep(re * 1.3, re * 1.75, r);    // the hole over the eye: wider aloft than at the floor (the "stadium")
    return float4(cov, saturate(top / sHgt.y), saturate(conv), can);
}
float4 PSMap(VSOut i) : SV_Target
{
    float2 xy = (float2(i.uv.x, 1 - i.uv.y) * 2 - 1) * sEast.w;   // east right, north up
    return StormMap(xy);
}

// ------------------------------------------------------------------------------------------------ the volume
float3 ToPlanet(float3 v) { return float3(dot(sRot0.xyz, v), dot(sRot1.xyz, v), dot(sRot2.xyz, v)); }
float3 CamPl() { return float3(sRot0.w, sRot1.w, sRot2.w) * 1000.0; }

// a point (planet frame, m) -> map xy [m] (east, north) and its altitude
float2 MapXY(float3 P, out float h)
{
    float rP = length(P);
    h = rP - sCen.w;
    float3 u = P / rP;
    float c = dot(u, sCen.xyz);
    float3 cx = cross(u, sCen.xyz);
    float s = length(cx);
    float th = atan2(s, c);                    // the angle from the centre, well conditioned near it
    float k = sCen.w * th / max(s, 1e-9);
    return float2(dot(u, sEast.xyz), dot(u, sNorth.xyz)) * k;
}
float4 MapAt(float2 xy, float lod)
{
    float2 uv = xy / (2 * sEast.w) + 0.5;
    if (any(uv < 0) || any(uv > 1)) return 0;
    return tMap.SampleLevel(sClampS, float2(uv.x, 1 - uv.y), lod);
}

struct Dens { float s; float hfLow; float canopy; float alb; };
// the storm's own frame: east, north at the centre, and along the centre's vertical [m]
float3 Local(float3 P) { return float3(dot(P, sEast.xyz), dot(P, sNorth.xyz), dot(P, sCen.xyz)); }
float2 Rot(float2 v, float a) { float s, c; sincos(a, s, c); return float2(v.x * c - v.y * s, v.x * s + v.y * c); }
// the shape noise carried round by the vortex as the map's structure is (the same two flow phases, cross-faded)
float4 ShapeAt(float3 loc, float om, float3 scale, float3 ofs, float lod)
{
    float hs = sSig.w;
    float4 a = 0, b = 0;
    if (sAnim.w < 0.97) a = tShape.SampleLevel(sWrap, float3(Rot(loc.xy, -om * sAnim.y * hs), loc.z) / SHAPE_M * scale + ofs, lod);
    if (sAnim.w > 0.03) b = tShape.SampleLevel(sWrap, float3(Rot(loc.xy, -om * sAnim.z * hs), loc.z) / SHAPE_M * scale + ofs + 0.5, lod);
    return sAnim.w < 0.03 ? a : sAnim.w > 0.97 ? b : lerp(a, b, sAnim.w);
}
// the extinction [1/m] at the planet-frame point P; detail 0..1 (the fine erosion's share), foot: the pixel's size [m]
// (the cells stay while they are larger than it, the noises' mips follow it); the height fraction and albedo out
Dens Density(float3 P, float detail, float mapLod, float foot)
{
    Dens o; o.s = 0; o.hfLow = 0; o.canopy = 0; o.alb = 1;
    float cells = saturate(1.6 - foot / 1500);
    float shLod = clamp(log2(max(foot, 1) / 70), 0, 6);
    float h; float2 xy = MapXY(P, h);
    if (h > sHgt.w + 200 || h < -100) return o;
    float4 m = MapAt(xy, mapLod);
    if (all(m < 0.02)) return o;
    float rxy = length(xy) + 1;
    float om = VortexWind(rxy) / rxy;
    float3 loc = Local(P);
    // the eye's wall leans out with height
    float eyeR = sShape.x * (1 + 0.9 * saturate(h / sHgt.y));
    float eyeCut = h < 2300 ? 1 : smoothstep(eyeR * 0.9, eyeR * 1.1, rxy);
    // the low/mid layer
    // domes, not boxes: a cell's top is lower where the cover thins (the edges of a field are its low cells)
    float base = sHgt.x, top = max(m.g * sHgt.y, base + 300);
    top = base + (top - base) * lerp(lerp(0.75, 0.3, m.b), 1, sqrt(m.r));
    float hf = (h - base) / (top - base);
    o.hfLow = hf;
    float s = 0;
    if (m.r > 0.02 && hf > 0 && hf < 1) {
        float conv = m.b;
        // the vertical profile: a flat base; convective - rounded tops, wider where the cover is high; stratiform - a slab
        float gC = smoothstep(0, 0.06, hf) * (1 - smoothstep(lerp(0.35, 0.7, m.r), 1, hf));
        float gS = smoothstep(0, 0.12, hf) * smoothstep(1, 0.75, hf);
        float g = lerp(gS, gC, conv);
        // the cells: towers taller than wide where convective, flat sheets where stratiform
        float4 sh = ShapeAt(loc, om, float3(1, 1, lerp(2.5, 0.8, conv)), 0, shLod);
        float wf = sh.g * 0.625 + sh.b * 0.25 + sh.a * 0.125;
        float bc = saturate(Remap(sh.r, -(1 - wf) * lerp(0.25, 0.6, conv), 1, 0, 1)) * g;
        // the cover cuts the cells: sparse where it is low
        float d = saturate(Remap(bc, 1 - m.r, 1, 0, 1)) * m.r * smoothstep(0.12, 0.3, m.r);   // no cells where the cover is ~0
        d = lerp(saturate(m.r * 1.3 - 0.15) * g, d, cells);   // afar, past the cells' size: the map's own (soft) cover
        if (d > 0 && detail > 0.01) {
            // the edges eaten by the fine Worley: wispy at the base, billowing towards the tops
            float4 dt = tDetail.SampleLevel(sWrap, loc / DETAIL_M + float3(0, 0, sNorth.w * 0.002), 0);
            float df = dt.r * 0.625 + dt.g * 0.25 + dt.b * 0.125;
            df = lerp(df, 1 - df, saturate(hf * 4));
            d = saturate(Remap(d, df * 0.55 * detail, 1, 0, 1));
        }
        d = saturate((d - 0.04) / 0.96);                   // no wisps of a few pixels: a cloud has a body or is not there
        s += d * eyeCut * lerp(sSig.y, sSig.x, conv);
    }
    // the rain under the convective cells: grey shafts below the base
    if (h < base && m.r > 0.6 && m.b > 0.7) {      // only under the strong cells, faint from above
        // rain shafts: grey (large drops scatter little back to the eye), soft-edged, columns only ~2x taller than wide
        float4 sh = ShapeAt(loc, om, float3(1.2, 1.2, 0.5), 0.37, max(shLod, 1));
        float rain = saturate((m.r - 0.6) * 2.5) * saturate((m.g * sHgt.y - 7000) / 5000) * smoothstep(0.45, 0.75, sh.r);
        float sr = rain * 6e-5 * eyeCut * smoothstep(0, base * 0.3, h + 50);
        s += sr; o.alb = 0.45;
    }
    // the cirrus canopy: smooth, thinning outwards and with height
    // its top lower where it thins (the outflow's fibres sink, the dense overcast domes up over the core)
    // a lens like an anvil: towards its rim the base rises and the top sinks, the density fades - no sheet seen edge-on
    float cb = sHgt.z + (sHgt.w - sHgt.z) * 0.35 * (1 - m.a), ct = sHgt.z + (sHgt.w - sHgt.z) * (0.45 + 0.55 * m.a);
    float hc = (h - cb) / (ct - cb);
    if (m.a > 0.02 && hc > 0 && hc < 1) {
        float g2 = smoothstep(0, 0.25, hc) * smoothstep(1, 0.55, hc);
        float4 sh = ShapeAt(loc, om, float3(0.5, 0.5, 2.0), 0.61, max(shLod - 1, 0));
        float w = lerp(1, saturate(Remap(sh.g * 0.6 + sh.b * 0.4, 0.15, 0.85, 0, 1)), saturate(detail + 0.2) * 0.6);
        float d2 = saturate(pow(m.a, 1.5) * g2 * w * 1.15) * eyeCut;
        o.canopy = d2;
        s += d2 * sSig.z;
    }
    o.s = s;
    return o;
}
float DensityLo(float3 P, float detail) { return Density(P, detail * 0.5, 1, 300).s; }

// the optical depth towards the Sun from P (planet frame), sP the Sun's direction (planet frame): a short march through
// the near cloud, then the canopy's own crossing (it lies kilometres above the low clouds)
float SunTau(float3 P, float3 sP, float detail, int taps, float jit)
{
    float tau = 0, l = 60 * (0.75 + 0.5 * jit);
    float pos = 0;
    [loop] for (int j = 0; j < taps; j++) {
        pos += l * 0.5;
        tau += DensityLo(P + sP * pos, detail) * l;
        pos += l * 0.5; l *= 2.1;
    }
    float h = length(P) - sCen.w;
    float muS = dot(normalize(P), sP);
    float cm = 0.5 * (sHgt.z + sHgt.w);
    if (h < sHgt.z && muS > 0.02) {
        float d = (cm - h) / muS;
        if (d > pos) {
            float3 Q = P + sP * d;
            float hq; float4 m = MapAt(MapXY(Q, hq), 2);
            tau += m.a * sSig.z * (sHgt.w - sHgt.z) * 0.55 / muS;
        }
    }
    return tau;
}

float HG(float g, float c) { float t = 1 + g * g - 2 * g * c; return (1 - g * g) / (t * sqrt(t)); }   // relative to isotropic

// the span of the ray (from the planet's centre org, direction d, both global) inside the storm: the shells between the
// sea and the canopy's top, and the cone of the storm's cap round its centre
void StormSpan(float3 org, float3 d, out float t0, out float t1)
{
    t0 = 0; t1 = 0;
    float R = sCen.w, rTop = R + sHgt.w + 300, rBot = R - 50;
    float b = dot(org, d), c0 = dot(org, org);
    float dTop = b * b - (c0 - rTop * rTop);
    if (dTop < 0) return;
    float sTop = sqrt(dTop);
    float a0 = max(-b - sTop, 0), a1 = -b + sTop;
    float dBot = b * b - (c0 - rBot * rBot);
    if (dBot > 0) { float tb = -b - sqrt(dBot); if (tb > 0) a1 = min(a1, tb); }
    if (a1 <= a0) return;
    // the cone: dot(q, c)^2 = k |q|^2 with dot(q, c) > 0, q = org + t d
    float3 cG = sCenG.xyz; float kk = sCenG.w * sCenG.w;
    float A = dot(d, cG), B0 = dot(org, cG);
    float qa = A * A - kk, qb = 2 * (A * B0 - kk * b), qc = B0 * B0 - kk * c0;
    float ts[4]; int n = 0;
    ts[n++] = a0;
    float disc = qb * qb - 4 * qa * qc;
    if (abs(qa) > 1e-12 && disc > 0) {
        float sq = sqrt(disc);
        float r1 = (-qb - sq) / (2 * qa), r2 = (-qb + sq) / (2 * qa);
        float lo = min(r1, r2), hi = max(r1, r2);
        if (lo > a0 && lo < a1) ts[n++] = lo;
        if (hi > a0 && hi < a1) ts[n++] = hi;
    }
    ts[n++] = a1;
    float first = -1, last = -1;
    [unroll] for (int j = 0; j < 3; j++) {
        if (j + 1 >= n) break;
        float tm = 0.5 * (ts[j] + ts[j + 1]);
        float3 q = org + d * tm;
        float dq = dot(q, cG);
        bool inside = dq > 0 && dq * dq >= kk * dot(q, q);
        if (inside) { if (first < 0) first = ts[j]; last = ts[j + 1]; }
    }
    if (first < 0) return;
    t0 = first; t1 = min(last, first + 700000);
}

struct MOut { float4 c : SV_Target0; float t : SV_Target1; };

MOut PSMarch(VSOut i)
{
    MOut o; o.c = float4(0, 0, 0, 1); o.t = 1e9;
    float2 uv = i.uv;
    float4 cc = float4(uv.x * 2 - 1, 1 - uv.y * 2, 1, 1);
    float4 wn = mul(cc, sInvVP);
    float3 d = normalize(wn.xyz / wn.w);
    // the scene's distance: the farthest of the four full-res pixels (the composite drops the cloud in front of a hill)
    float dw, dh; tDepth.GetDimensions(dw, dh);
    int2 px = int2(uv * float2(dw, dh));
    float zmin = min(min(tDepth.Load(int3(px, 0)), tDepth.Load(int3(px + int2(1, 0), 0))), min(tDepth.Load(int3(px + int2(0, 1), 0)), tDepth.Load(int3(px + 1, 0))));
    float sceneT = 1e30;
    if (zmin > 1e-9) { float4 w = mul(float4(cc.xy, zmin, 1), sInvVP); sceneT = length(w.xyz / w.w); }
    float3 org = -mCentre.xyz;
    float t0, t1; StormSpan(org, d, t0, t1);
    t1 = min(t1, sceneT);
    if (t1 <= t0) return o;

    const int N = (int)sP.x;
    float L = t1 - t0;
    // the steps grow along the ray (fine at the eye, coarse afar): t(u) = t0 + L (e^(k u) - 1) / (e^k - 1)
    float dtMin = max(12.0, t0 * 0.003);
    float rho = L / (N * dtMin);
    float kx = 0;
    if (rho > 2) { kx = log(rho) + 1; [unroll] for (int it = 0; it < 6; it++) kx = log(1 + rho * kx); }
    float ek = exp(kx) - 1;
    // the step's dither: a hash of the pixel and the frame - no structure (the interleaved gradient's vertical stripes showed)
    float ign = (Hash3(uint3(i.pos.xy, (uint)sP.y)) & 0xffffffu) / 16777216.0;

    float3 dP = ToPlanet(d);
    float3 sunP = ToPlanet(gSunDir.xyz);
    float3 camP = CamPl();
    float cs = dot(d, gSunDir.xyz);
    // the drops' phase: a strong forward peak and a little back-scatter; the multiple-scattering octaves flatten it
    float ph0 = lerp(HG(0.8, cs), HG(-0.3, cs), 0.25);
    float ph1 = lerp(HG(0.4, cs), HG(-0.15, cs), 0.25);
    // the pixel's footprint decides how much 3D detail it can show: none from orbit (the map: 2D), all within a few km
    float3 Lc = 0; float T = 1;
    float tw = 0, twS = 0;
    float3 sunC = 0, ambUp = 0, ambDn = 0; bool lit = false;
    float tFront = -1;
    [loop] for (int k = 0; k < N; k++) {
        float u = (k + ign) / N;
        float t, dt;
        if (kx > 0) { float e = exp(kx * u); t = t0 + L * (e - 1) / ek; dt = L * kx * e / (ek * N); }
        else { t = t0 + L * u; dt = L / N; }
        float3 P = camP + dP * t;
        float foot = t * 0.0015;                                // ~the half-res pixel at 60 degrees
        float detail = saturate(1 - t / sP.w) * saturate(1.5 - foot / 40);
        // the noise prefiltered to what the step can resolve (no cell smaller than a third of the step: no sparkle)
        float fp = max(foot, dt / 3);
        float mlod = clamp(log2(max(fp * 2 / (2 * sEast.w / 2048), 1)), 0, 6);   // the map's mip by the pixel
        Dens D = Density(P, detail, mlod, fp);
        if (D.s <= 1e-7) continue;
        // inside the cloud: two more samples across the step (its extinction averaged, the light taken once)
        float sA = Density(camP + dP * (t - dt * 0.33), detail, mlod, fp).s, sB = Density(camP + dP * (t + dt * 0.33), detail, mlod, fp).s;
        D.s = (D.s + sA + sB) / 3;
        if (!lit) {         // the light at the cloud's first point (it hardly changes across one cloud)
            float3 wp = d * t;
            sunC = FxActive() ? FxSunLight(wp) : SunLight(wp);
            float3 up = normalize(wp - mCentre.xyz);
            ambUp = FxActive() ? FxSkyLight(up, wp) : SkyLight(up, wp);
            ambDn = FxActive() ? FxSkyLight(-up, wp) : SkyLight(-up, wp) * 0.3;
            lit = true;
        }
        float tauS = SunTau(P, sunP, detail, detail > 0.05 ? 5 : 3, frac(ign + k * 0.618034));
        // multiple scattering (Wrenninge 2013): each octave a weaker extinction, a flatter phase
        // plus the light diffused through the whole cloud: a thick cloud's transmission ~ 1 / (1 + 0.75 tau (1 - g)) (two-
        // stream), ~15 % through tau 50 - its inside is grey, not black
        float msS = ph0 * exp(-tauS) + ph1 * 0.5 * exp(-tauS * 0.25) + 0.35 / (1 + 0.11 * tauS);
        // the sky's light: from above, shut out by the cloud over the point; the sea's below
        float3 up = normalize(P);
        float tauU = DensityLo(P + up * 300, detail) * 300 + DensityLo(P + up * 1500, detail) * 1500;
        float h = length(P) - sCen.w;
        if (h < sHgt.z) { float hq; tauU += MapAt(MapXY(P + up * (sHgt.z - h + 1500), hq), 2).a * sSig.z * (sHgt.w - sHgt.z) * 0.5; }
        float hfA = saturate(D.hfLow);
        float3 amb = ambUp * lerp(0.6, 1, hfA) / (1 + 0.15 * tauU) + ambDn * 0.6;
        float3 S = sunC * msS + amb;
        float a = exp(-D.s * dt);
        // energy-conserving step (Hillaire 2016): the in-scattered light integrated across the step
        Lc += T * S * (1 - a) * D.alb;
        tw += T * (1 - a) * t; twS += T * (1 - a);
        if (tFront < 0 && T < 0.97) tFront = t;
        T *= a;
        if (T < 0.008) break;
    }
    if (T > 0.999) return o;
    float tc = twS > 0 ? tw / twS : t0;
    // the air in front of the cloud
    float3 La = 0, Ta = 1;
    if (FxActive()) {
        float3 fw; float tc0 = -dot(org, d); float3 cx = cross(org, d); float K = dot(cx, cx) - mCentre.w * mCentre.w;
        float bt = dot(org, d), dTopA = bt * bt - (dot(org, org) - mGeo.x * mGeo.x);
        float tIn = dTopA > 0 ? max(0, -bt - sqrt(dTopA)) : 0;
        if (tc > tIn) FxMarch(org, d, tc0, K, tIn, tc, 12, La, Ta, fw);
        La *= FX_PI;
    }
    o.c = float4(Ta * Lc + La * (1 - T), T);
    o.t = tFront > 0 ? tFront : tc;
    return o;
}

// ------------------------------------------------------------------------------------------------ temporal accumulation
// The march's dither changes every frame; here each half-res texel takes last frame's result where its cloud was (the
// point at the cloud's depth carried with the planet's rotation, into last frame's view), clamped to this frame's
// neighbourhood (no ghosts), blended 1:9 - the grain and the hatching average out.
Texture2D tCur : register(t4);
Texture2D<float> tCurT : register(t5);
Texture2D tHist : register(t6);
Texture2D<float> tHistT : register(t7);
struct TOut { float4 c : SV_Target0; float t : SV_Target1; };
TOut PSTemporal(VSOut i)
{
    int2 q = int2(i.pos.xy);
    int2 mxq = int2(sSize.xy) - 1;
    float4 c = tCur.Load(int3(q, 0));
    float t = tCurT.Load(int3(q, 0));
    float4 mn = c, mx = c, m1 = 0, m2 = 0;
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
        float4 v = tCur.Load(int3(clamp(q + int2(x, y), 0, mxq), 0));
        mn = min(mn, v); mx = max(mx, v); m1 += v; m2 += v * v;
    }
    // the box from the mean and the spread (variance clipping) - tighter than min/max, still lets the grain average
    m1 /= 9; float4 sd = sqrt(max(m2 / 9 - m1 * m1, 0));
    // the 3 x 3 min/max box, a little widened: the variance box was so tight the grain came through it
    mn -= 0.1 * sd; mx += 0.1 * sd;
    TOut o; o.c = c; o.t = t;
    if (sTmp.x < 0.5) return o;
    float2 uv = (q + 0.5) * sSize.zw;
    float4 cc = float4(uv.x * 2 - 1, 1 - uv.y * 2, 1, 1);
    float4 wn = mul(cc, sInvVP);
    float3 d = normalize(wn.xyz / wn.w);
    float tt = min(t, 2.0e7);
    float3 Ppl = ToPlanet(d * tt) + CamPl();
    float3 wp = float3(dot(sPr0.xyz, Ppl), dot(sPr1.xyz, Ppl), dot(sPr2.xyz, Ppl)) + float3(sPr0.w, sPr1.w, sPr2.w);
    float4 pc = mul(float4(wp, 1), sPrevVP);
    if (pc.w <= 1e-6) return o;
    float2 puv = float2(pc.x / pc.w * 0.5 + 0.5, 0.5 - pc.y / pc.w * 0.5);
    if (any(puv < 0) || any(puv > 1)) return o;
    // the history is clamped only as far as the view moves (standing still it converges fully; moving, no ghosts)
    float4 h0 = tHist.SampleLevel(sClampS, puv, 0);
    float mv = length((puv - uv) * sSize.xy);
    float4 h = lerp(h0, clamp(h0, mn, mx), saturate(mv * 0.7));
    o.c = lerp(h, c, lerp(0.05, sTmp.y * 2, saturate(mv * 0.3)));
    return o;
}

// ------------------------------------------------------------------------------------------------ over the scene
Texture2D tCloud : register(t4);
Texture2D<float> tCloudT : register(t5);
float4 PSComposite(VSOut i) : SV_Target
{
    float dw, dh; tDepth.GetDimensions(dw, dh);
    float z = tDepth.Load(int3(int2(i.pos.xy), 0));
    float2 uv = i.pos.xy / float2(dw, dh);
    float sceneT = 1e30;
    if (z > 1e-9) { float4 w = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, z, 1), sInvVP); sceneT = length(w.xyz / w.w); }
    // the four half-res texels round the pixel, weighted bilinearly and by whether their cloud lies in front of the scene
    float2 hp = uv * sSize.xy - 0.5;
    float2 f = frac(hp); int2 b = (int2)floor(hp);
    // a 4 x 4 tent (bilinear widened by a texel each way: the half-res grain smoothed, edges kept by the depth test)
    float4 acc = 0; float wsum = 0;
    [unroll] for (int y = -1; y < 3; y++) [unroll] for (int x = -1; x < 3; x++) {
        int2 q = clamp(b + int2(x, y), 0, int2(sSize.xy) - 1);
        float wx = saturate(1.5 - abs(x - f.x) * 0.75), wy = saturate(1.5 - abs(y - f.y) * 0.75);
        float wb = wx * wy + 1e-4;
        float tq = tCloudT.Load(int3(q, 0));
        if (tq < 1e8 && tq > sceneT * 1.02) continue;    // that texel's cloud lies behind what is drawn here (an empty one blends)
        acc += tCloud.Load(int3(q, 0)) * wb; wsum += wb;
    }
    if (wsum < 1e-3) discard;
    float4 c = acc / wsum;
    if (sP.z > 0.5 && sP.z < 1.5) return float4((1 - c.a).xxx / max(gHazeRGB.w * gAdapt.rgb, 1e-4), 0);   // StormDbg 1: opacity
    if (c.a > 0.999) discard;
    return c;
}

// ------------------------------------------------------------------------------------------------ the shadow
// the factor the lit scene is multiplied by: the direct Sun through both layers, the sky's light under the canopy
float4 PSShadow(VSOut i) : SV_Target
{
    float dw, dh; tDepth.GetDimensions(dw, dh);
    float z = tDepth.Load(int3(int2(i.pos.xy), 0));
    if (z <= 1e-9) return 1;
    float2 uv = i.pos.xy / float2(dw, dh);
    float4 w = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, z, 1), sInvVP);
    float3 wp = w.xyz / w.w;
    float3 P = CamPl() + ToPlanet(wp);
    float h; float2 xy = MapXY(P, h);
    if (h > sHgt.w || length(xy) > sShape.w * 1.05) return 1;
    float3 sunP = ToPlanet(gSunDir.xyz);
    float muS = dot(normalize(P), sunP);
    if (muS < 0.01) return 1;
    // the Sun's ray through the low layer (four points between its base and its tops) and through the canopy
    float tau = 0;
    float4 m0 = MapAt(xy, 1);
    float top = max(m0.g * sHgt.y, sHgt.x + 300);
    float dist = length(wp);
    float detail = saturate(1 - dist / 40000.0);
    [unroll] for (int j = 0; j < 4; j++) {
        float hj = lerp(sHgt.x, top, (j + 0.5) / 4);
        if (hj <= h) continue;
        float s = (hj - h) / muS;
        tau += Density(P + sunP * s, detail, 1, 200).s * (top - sHgt.x) / 4 / muS;
    }
    float hc = 0.5 * (sHgt.z + sHgt.w);
    float hq; float4 mc = MapAt(MapXY(P + sunP * ((hc - h) / muS), hq), 1);
    float tauC = h < sHgt.z ? mc.a * sSig.z * (sHgt.w - sHgt.z) * 0.55 / muS : 0;
    float tauA = tau + tauC;
    float Tsun = exp(-tauA);
    // what the cloud does not let through directly it lets through diffused (two-stream: 1 / (1 + 0.75 tau (1 - g)));
    // under a hurricane's full depth ~5-10 % of the daylight - grey, not night
    float Tdif = 1 / (1 + 0.11 * tauA);
    float skyT = 1 / (1 + 0.11 * tauA * muS);   // the sky's light through the column overhead
    // the share of the lit scene that is the Sun's (a clear sky gives ~20 % of the light on the ground)
    float k = lerp(0.8, 0.9, saturate(muS));
    float f = k * (Tsun + 0.85 * max(Tdif - Tsun, 0)) + (1 - k) * skyT;
    if (sP.z > 2.5) return float4(Tsun.xxx, 1);   // StormDbg 3: the shadow alone
    return float4(f.xxx, 1);
}
