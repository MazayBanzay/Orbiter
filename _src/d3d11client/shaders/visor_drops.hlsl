// D3D11Client: rain drops on the suit's visor (VisorDrops.cpp). Titan's methane: surface tension 17 mN/m, 450 kg/m3,
// a fifth of water's viscosity, n = 1.28, g = 1.35 m/s2 - a capillary length of ~5 mm: the drops spread wide and flat,
// merge into a film and run in rivulets; the air, 4.4 times as dense as Earth's, blows them off at a walking pace.
//
// The glass is simulated in its own coordinates, fixed to the suit (the helmet turns with the body; the view's own
// turning moves the eye's gaze over the glass, not the drops): a sphere round the eye (radius vGeo.x, centre vCenter
// from the eye, the suit's frame: x right, y up, z forward), mapped by the direction from the eye - azimuth (u, right)
// over +-vGeo.y, elevation (v, down) from vGeo.z to vLight.x - so a texel is the same few pixels all over the view
// (~0.1 mm on the glass in front, 3 cm from the eye; ~0.5 mm at the sides). Every drop is a particle (position in texels, volume in mm3,
// velocity): a cap of contact angle vPhys2.x, flattened to the capillary length's puddle when large.
//   CSClearGrid, CSInsert   the largest drop of each 8 x 8 texel cell (a 32-bit key: volume, index)
//   CSUpdate                 merging (a drop touching a larger one is absorbed), the forces - gravity's and the
//                            inertia's share along the glass, the air's drag, the contact line's pinning (hysteresis,
//                            the glass's heterogeneity, less on a wet trail) - the drop's slide, the film it takes up,
//                            the beads it leaves behind, evaporation
//   CSFinalize, CSSpawn      the absorbed volumes added; the new drops (impacts, CPU-sampled from the rain's flux)
//   CSFilm                   the film's slow evaporation
//   VSDrop + PSWipe/PSBead/PSCap   a moving drop wipes the film (multiply), beads go into it (max), the drops' caps
//                            over the film into the height map (max)
//   PSLens, PSBlurH/V        the height map's slopes, coverage, thickness; blurred by the pupil's footprint on the
//                            glass (Gaussian, then read bicubic: no texel blocks at ~15 pixels a texel)
//   PSDown/PSDownMS          the scene so far at half resolution (its mips: the blur seen through a drop)
//   PSInside                 the view through the glass: each pixel's ray meets the sphere, the drop there refracts
//                            the scene (thin prism, (n-1) x slope), out of focus by the pupil's size on the glass
//                            (3-5 cm from the eye), the rim dark (total reflection), the Sun and the lamps' light
//                            through it as soft discs
//   VSOut/PSOut              from outside: small drops on the suits' glass (procedural, in the glass's rest frame)
#include "common.hlsli"

static const float PI = 3.14159265;
static const uint NDROP = 4096;
static const uint GRIDC = 16;         // texels per merge cell

cbuffer Visor : register(b1)
{
    float4 vGrid;     // W, H, 1/W, 1/H
    float4 vGeo;      // x sphere radius [mm], y azimuth max [rad], z elevation at the top [rad], w refraction (1 = Snell)
    float4 vCenter;   // the sphere's centre from the eye, the suit's frame [m]; w dt [s]
    float4 vAcc;      // gravity minus the head's acceleration, the suit's frame [m/s2]; w time [s]
    float4 vAir;      // the air relative to the glass, the suit's frame [m/s]; w its density [kg/m3]
    float4 vPhys;     // x surface tension [N/m], y liquid density, z viscosity [Pa s], w pinning factor
    float4 vPhys2;    // x contact angle [rad], y capillary length [mm], z drop evaporation [mm2/s], w film evaporation [mm/s]
    float4 vMisc;     // x spawn count, y draw mode, z frame number, w pupil diameter [mm]
    float4 vCamX, vCamY, vCamZ;   // the suit frame's axes (global)
    float4 vProj;     // x tan half-width, y tan half-height, z scene copy mips, w its 1 / mip0 width
    float4 vLight;    // x elevation at the bottom [rad], y the Sun's angular spread through a drop [rad], z glint gain, w on
    float4 vCR, vCU, vCF;         // the camera's axes (right, up, forward) in the suit's frame
};

struct Drop
{
    float2 p;         // texels
    float vol;        // mm3
    float age;        // s
    float2 v;         // m/s along (u, v): right, down
    float trail;      // mm slid since the last bead
    uint flags;       // bit 0 alive
    float2 bead;      // a bead left this frame (texels) ...
    float beadVol;    // ... its volume [mm3] (0: none)
    float seed;
};

float Hash1(uint n) { n = (n << 13u) ^ n; n = n * (n * n * 15731u + 789221u) + 1376312589u; return float(n & 0x7fffffffu) / 2147483647.0; }
float Hash2(uint2 c) { return Hash1(c.x * 73856093u ^ c.y * 19349663u); }

// ---------------------------------------------------------------- the glass's geometry
// a texel: the direction from the eye, the point where it meets the glass [mm from the eye], the glass's normal
// (outward), its tangents along u (right) and v (down), a texel's size there [mm]
struct GPt { float3 P, n, eu, ev; float2 mm; };
float2 AzEl(float2 p) { return float2((p.x * vGrid.z * 2 - 1) * vGeo.y, lerp(vGeo.z, vLight.x, p.y * vGrid.w)); }
float2 TexOf(float az, float el) { return float2((az / vGeo.y * 0.5 + 0.5) * vGrid.x, (vGeo.z - el) / (vGeo.z - vLight.x) * vGrid.y); }
float3 Dir(float az, float el) { return float3(cos(el) * sin(az), sin(el), cos(el) * cos(az)); }
float3 Hit(float3 d)
{
    float3 c = vCenter.xyz * 1000;
    float b = dot(d, c);
    return (b + sqrt(max(b * b - dot(c, c) + vGeo.x * vGeo.x, 0))) * d;
}
GPt Glass(float2 p)
{
    GPt g;
    float2 a = AzEl(p);
    const float e = 2e-3;
    g.P = Hit(Dir(a.x, a.y));
    float3 du = (Hit(Dir(a.x + e, a.y)) - Hit(Dir(a.x - e, a.y))) / (2 * e);   // mm per radian
    float3 dv = (Hit(Dir(a.x, a.y + e)) - Hit(Dir(a.x, a.y - e))) / (2 * e);
    g.n = normalize(g.P - vCenter.xyz * 1000);
    g.eu = normalize(du); g.ev = -normalize(dv);
    g.mm = max(float2(length(du) * 2 * vGeo.y * vGrid.z, length(dv) * (vGeo.z - vLight.x) * vGrid.w), 0.02);
    return g;
}

// a drop's footprint radius R and its height h [mm] from its volume: a spherical cap of contact angle t; beyond the
// capillary length's limit (h = 2 lc sin(t/2)) a puddle of that height
void Cap(float vol, out float R, out float h)
{
    float t = vPhys2.x, c = cos(t), s = sin(t);
    float k = PI * (2 - 3 * c + c * c * c) / (3 * s * s * s);
    R = pow(max(vol, 1e-6) / k, 1.0 / 3);
    h = R * tan(0.5 * t);
    float hm = 2 * vPhys2.y * sin(0.5 * t);
    if (h > hm) { h = hm; R = sqrt(vol / (0.9 * PI * hm)); }
}
// the cap's profile at r = distance / R (a puddle has a flat top and a rounded edge)
float Profile(float r2, float R, float h)
{
    float cap = saturate(1 - r2);
    float flat = saturate(h / (2 * vPhys2.y * sin(0.5 * vPhys2.x)) - 0.6) * 2.5;   // 0 a cap .. 1 a puddle
    float pud = saturate((1 - r2 * r2 * r2) * 1.6);
    return h * lerp(cap, pud, saturate(flat));
}

// ---------------------------------------------------------------- simulation
RWStructuredBuffer<Drop> uDrops : register(u0);
RWStructuredBuffer<uint> uVolAdd : register(u1);
RWTexture2D<uint> uGrid : register(u2);
RWStructuredBuffer<uint> uCount : register(u3);
RWTexture2D<float> uFilm : register(u4);
StructuredBuffer<float4> tSpawn : register(t1);     // x, y texels; z volume [mm3]; w slot (-1: a bead in the film)
Texture2D<float> tFilmR : register(t2);

uint2 GridSize() { return uint2((uint)vGrid.x / GRIDC, (uint)vGrid.y / GRIDC); }

[numthreads(64, 1, 1)]
void CSClearGrid(uint3 id : SV_DispatchThreadID)
{
    uint2 gs = GridSize();
    if (id.x < gs.x * gs.y) uGrid[uint2(id.x % gs.x, id.x / gs.x)] = 0;
    if (id.x == 0) uCount[0] = 0;
}

uint Key(float vol, uint i) { return (min((uint)(vol * 100) + 1, 0xFFFFFu) << 12) | (i & 0xFFFu); }

[numthreads(64, 1, 1)]
void CSInsert(uint3 id : SV_DispatchThreadID)
{
    Drop d = uDrops[id.x];
    if (!(d.flags & 1)) return;
    uint2 c = min((uint2)(d.p / GRIDC), GridSize() - 1);
    InterlockedMax(uGrid[c], Key(d.vol, id.x));
}

// the glass's own patchiness (coating wear, dust): the pinning varies by +-40 % over a few mm
float Hetero(float2 p)
{
    float2 q = p / 6;
    float2 i = floor(q), f = q - i; f = f * f * (3 - 2 * f);
    float a = Hash2((uint2)(i + 4096)), b = Hash2((uint2)(i + float2(1, 0) + 4096));
    float c = Hash2((uint2)(i + float2(0, 1) + 4096)), e = Hash2((uint2)(i + 1 + 4096));
    return lerp(lerp(a, b, f.x), lerp(c, e, f.x), f.y);
}

[numthreads(64, 1, 1)]
void CSUpdate(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x;
    Drop d = uDrops[i];
    d.beadVol = 0;
    if (!(d.flags & 1)) { uDrops[i] = d; return; }
    const float dt = vCenter.w;
    GPt gp = Glass(d.p); float2 mm = gp.mm;
    float R, h; Cap(d.vol, R, h);
    // merging: the largest drop of each neighbouring cell; touching a larger one, this one is absorbed by it
    uint2 gs = GridSize();
    int2 c0 = (int2)min((uint2)(d.p / GRIDC), gs - 1);
    const uint me = Key(d.vol, i);
    [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
        int2 c = c0 + int2(x, y);
        if (any(c < 0) || any(c >= (int2)gs)) continue;
        uint k = uGrid[c];
        if (k == 0 || k <= me) continue;
        uint j = k & 0xFFFu;
        Drop o = uDrops[j];
        if (!(o.flags & 1)) continue;
        float Ro, ho; Cap(o.vol, Ro, ho);
        float2 dm = (o.p - d.p) * mm;
        if (dot(dm, dm) < (R + Ro) * (R + Ro) * 0.8) {
            InterlockedAdd(uVolAdd[j], (uint)(d.vol * 1000));
            d.flags = 0; uDrops[i] = d; return;
        }
    }
    float3 eu = gp.eu, ev = gp.ev, n = gp.n;
    const float m = vPhys.y * d.vol * 1e-9;                          // kg
    float2 F = m * float2(dot(vAcc.xyz, eu), dot(vAcc.xyz, ev));    // N
    // the air: the flow along the glass (potential flow round a sphere on the windward side, a weak separated one
    // in its lee), drag on the drop's frontal area (2 R h), Cd ~ 1.2 for a bump in a boundary layer
    float un = dot(vAir.xyz, n);
    float3 ut = (vAir.xyz - un * n) * (un < 0 ? 1.5 : 0.35);
    float2 u2 = float2(dot(ut, eu), dot(ut, ev));
    float us = length(u2);
    if (us > 1e-4) F += 0.5 * vAir.w * 1.2 * us * u2 * (2 * R * h * 1e-6);
    // the contact line's pinning: sigma x width x (cos receding - cos advancing); methane wets glass well, little
    // hysteresis; the glass's patches vary it, a wet trail lowers it (the next drop follows the last one's path)
    float film = tFilmR[(uint2)d.p];
    float pin = vPhys.x * (2 * R * 1e-3) * vPhys.w * lerp(0.6, 1.4, Hetero(d.p)) * lerp(0.8, 1.2, d.seed);
    pin *= lerp(1.0, 0.45, saturate(film / 0.04));
    if (length(d.v) > 0.002) pin *= 0.7;                            // moving: the kinetic (lower) threshold
    float Fm = length(F);
    float2 vt = 0;
    if (Fm > pin) {
        float2 dir = F / Fm;
        // the path meanders over the patches: the slope of the pinning field turns it a little
        float2 g = float2(Hetero(d.p + float2(1, 0)) - Hetero(d.p - float2(1, 0)), Hetero(d.p + float2(0, 1)) - Hetero(d.p - float2(0, 1)));
        dir = normalize(dir - 0.8 * (g - dot(g, dir) * dir));
        // a sliding drop's viscous resistance ~ c mu v R (the wedge at its contact line)
        vt = dir * min((Fm - pin) / (45 * vPhys.z * R * 1e-3), 3.0);
    }
    d.v += (vt - d.v) * saturate(dt * 25);
    float2 step = d.v * 1000 * dt;                                   // mm
    float sl = length(step);
    d.p += step / mm;
    // the film it runs into it takes up (the trail's width, the film's thickness)
    if (sl > 1e-4) {
        float f2 = tFilmR[(uint2)clamp(d.p, 0, vGrid.xy - 1)];
        d.vol += 0.6 * f2 * 2 * R * sl;
        d.trail += sl;
        // ... and beads it leaves behind: the receding line breaks up (a few % of it every 1.5-3 radii)
        if (d.trail > R * (1.5 + 1.5 * d.seed) && d.vol > 0.3) {
            float bv = d.vol * lerp(0.015, 0.04, Hash1(i * 31u + (uint)vMisc.z));
            d.bead = d.p - normalize(step) * R * 0.9 / mm; d.beadVol = bv; d.vol -= bv; d.trail = 0;
        }
    }
    // evaporation (diffusion-limited: dV/dt ~ R), slow in the cold, methane-laden air
    d.vol -= vPhys2.z * R * dt;
    d.age += dt;
    float2 lim = vGrid.xy - 1;
    if (d.vol < 0.004 || any(d.p < 1) || any(d.p > lim)) {           // dried, or run off the glass's edge
        d.flags = 0;
    }
    if (d.flags & 1) InterlockedAdd(uCount[0], 1);
    uDrops[i] = d;
}

[numthreads(64, 1, 1)]
void CSFinalize(uint3 id : SV_DispatchThreadID)
{
    uint a = uVolAdd[id.x];
    if (a) { Drop d = uDrops[id.x]; if (d.flags & 1) { d.vol += a * 0.001; uDrops[id.x] = d; } uVolAdd[id.x] = 0; }
}

[numthreads(64, 1, 1)]
void CSSpawn(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)vMisc.x) return;
    float4 s = tSpawn[id.x];
    if (s.w < 0) return;
    uint k = (uint)s.w;
    Drop o = uDrops[k];
    Drop d = (Drop)0;
    if (o.flags & 1) { d.bead = o.p; d.beadVol = o.vol; }            // the slot's old drop stays as a bead in the film
    d.p = s.xy; d.vol = s.z; d.flags = 1;
    d.seed = Hash1(k * 977u + (uint)vMisc.z * 13u);
    uDrops[k] = d;
}

[numthreads(8, 8, 1)]
void CSFilm(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= (uint2)vGrid.xy)) return;
    uFilm[id.xy] = max(uFilm[id.xy] - vPhys2.w * vCenter.w, 0);
}

// ---------------------------------------------------------------- the drops drawn into the film and the height map
StructuredBuffer<Drop> tDrops : register(t0);

struct DOut { float4 pos : SV_Position; float2 c : TEXCOORD0; nointerpolation float2 rh : TEXCOORD1; };

// vMisc.y: 0 the moving drops' wipe, 1 their beads, 2 the spawned beads, 3 the drops' caps
DOut VSDrop(uint vid : SV_VertexID, uint inst : SV_InstanceID)
{
    DOut o = (DOut)0;
    o.pos = float4(0, 0, -1, 1);
    const int mode = (int)vMisc.y;
    float2 p, vel = 0; float vol;
    if (mode == 2) {
        if (inst >= (uint)vMisc.x) return o;
        float4 s = tSpawn[inst];
        if (s.w >= 0) return o;
        p = s.xy; vol = s.z;
    } else {
        Drop d = tDrops[inst];
        if (mode == 1) { if (d.beadVol <= 0) return o; p = d.bead; vol = d.beadVol; }
        else {
            if (!(d.flags & 1)) return o;
            if (mode == 0 && length(d.v) < 0.002) return o;
            p = d.p; vol = d.vol; vel = d.v;
        }
    }
    float2 mm = Glass(p).mm;
    float R, h; Cap(vol, R, h);
    // a moving drop draws out along its path (the same area): a streak at speed, slanted as gravity and the air pull it
    float sp = length(vel);
    float e = 1 + min(sp * 6, 3);
    float2 a = sp > 1e-4 ? vel / sp : float2(0, 1), b = float2(-a.y, a.x);
    float La = R * sqrt(e), Lb = R / sqrt(e);
    if (mode == 0) { La *= 1.1; Lb *= 1.1; }
    static const float2 cr[6] = { float2(-1, -1), float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(1, -1) };
    float2 c = cr[vid] * 1.15;
    float2 q = p + (c.x * La * a + c.y * Lb * b) / mm;
    o.pos = float4(q.x * vGrid.z * 2 - 1, 1 - q.y * vGrid.w * 2, 0, 1);
    o.c = c; o.rh = float2(R, h);
    return o;
}
float PSWipe(DOut i) : SV_Target { float r2 = dot(i.c, i.c); return lerp(0.08, 1.0, smoothstep(0.6, 1.0, r2)); }
float PSBead(DOut i) : SV_Target { float r2 = dot(i.c, i.c); if (r2 >= 1) discard; return Profile(r2, i.rh.x, i.rh.y); }
float PSCap(DOut i) : SV_Target { float r2 = dot(i.c, i.c); if (r2 >= 1) discard; return Profile(r2, i.rh.x, i.rh.y); }

// ---------------------------------------------------------------- full-screen helpers
struct FOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
FOut VSFull(uint vid : SV_VertexID)
{
    FOut o;
    float2 t = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = t;
    return o;
}
float PSCopyFilm(FOut i) : SV_Target { return tFilmR[(uint2)i.pos.xy]; }

Texture2D<float> tHeight : register(t3);
// the slopes (mm per mm, along u and v), the coverage, the thickness [mm]
float4 PSLens(FOut i) : SV_Target
{
    int2 q = (int2)i.pos.xy, L = (int2)vGrid.xy - 1;
    float hc = tHeight[q];
    float hx0 = tHeight[clamp(q - int2(1, 0), 0, L)], hx1 = tHeight[clamp(q + int2(1, 0), 0, L)];
    float hy0 = tHeight[clamp(q - int2(0, 1), 0, L)], hy1 = tHeight[clamp(q + int2(0, 1), 0, L)];
    float2 mm = Glass(i.pos.xy).mm;
    float2 s = float2(hx1 - hx0, hy1 - hy0) / (2 * mm);
    float cov = smoothstep(0.004, 0.03, hc);
    return float4(s * cov, cov, hc);
}

// the eye's defocus: a Gaussian of the pupil's footprint on the glass (sigma ~ 0.35 x its diameter), separable
Texture2D<float4> tLensIn : register(t6);
float4 Blur(int2 q, int2 dir)
{
    float2 mm = Glass(q + 0.5).mm;
    float sig = clamp(0.35 * vMisc.w / (dir.x ? mm.x : mm.y), 0.5, 10);
    int r = (int)min(ceil(2.5 * sig), 24);
    int2 L = (int2)vGrid.xy - 1;
    float4 s = 0; float ws = 0;
    [loop] for (int k = -r; k <= r; k++) {
        float w = exp(-0.5 * k * k / (sig * sig));
        s += w * tLensIn[clamp(q + dir * k, 0, L)]; ws += w;
    }
    return s / ws;
}
float4 PSBlurH(FOut i) : SV_Target { return Blur((int2)i.pos.xy, int2(1, 0)); }
float4 PSBlurV(FOut i) : SV_Target { return Blur((int2)i.pos.xy, int2(0, 1)); }

// ---------------------------------------------------------------- the scene so far at half size (its mips: the blur)
Texture2D<float4> tScene : register(t14);
Texture2DMS<float4> tSceneMS : register(t15);
float4 PSDown(FOut i) : SV_Target
{
    int2 q = (int2)i.pos.xy * 2;
    float4 s = tScene[q] + tScene[q + int2(1, 0)] + tScene[q + int2(0, 1)] + tScene[q + int2(1, 1)];
    s *= 0.25; s.rgb = NotFinite(s.rgb) ? 0 : min(s.rgb, 6e4);
    return float4(s.rgb, 1);
}
float4 PSDownMS(FOut i) : SV_Target
{
    int2 q = (int2)i.pos.xy * 2;
    float4 s = tSceneMS.Load(q, 0);      // one sample: the mips blur it anyway
    s.rgb = NotFinite(s.rgb) ? 0 : min(s.rgb, 6e4);
    return float4(s.rgb, 1);
}

// ---------------------------------------------------------------- the view through the glass
Texture2D<float4> tLens : register(t4);
Texture2D<float4> tSceneC : register(t5);
SamplerState sLin : register(s3);

// a B-spline (bicubic) read in 4 bilinear taps: smooth between the glass's texels, which the eye sees ~15 pixels wide
float4 Bicubic(Texture2D<float4> t, float2 uv)
{
    float2 p = uv * vGrid.xy - 0.5, i = floor(p), f = p - i;
    float2 f2 = f * f, f3 = f2 * f;
    float2 w0 = (1 - 3 * f + 3 * f2 - f3) / 6, w1 = (4 - 6 * f2 + 3 * f3) / 6, w2 = (1 + 3 * f + 3 * f2 - 3 * f3) / 6, w3 = f3 / 6;
    float2 g0 = w0 + w1, g1 = w2 + w3;
    float2 h0 = (i - 0.5 + w1 / g0) * vGrid.zw, h1 = (i + 1.5 + w3 / g1) * vGrid.zw;
    return g0.y * (g0.x * t.SampleLevel(sLin, float2(h0.x, h0.y), 0) + g1.x * t.SampleLevel(sLin, float2(h1.x, h0.y), 0))
         + g1.y * (g0.x * t.SampleLevel(sLin, float2(h0.x, h1.y), 0) + g1.x * t.SampleLevel(sLin, float2(h1.x, h1.y), 0));
}

float3 ToGlobal(float3 c) { return c.x * vCamX.xyz + c.y * vCamY.xyz + c.z * vCamZ.xyz; }

// the lamps' light passing through a drop: a lobe round the ray's continuation (its width the defocus), each lamp
// as lights.hlsli gives it (its cone, range, shadow)
float3 LampsThrough(float3 wp, float3 dir, float a)
{
    if (!LocalLightsOn()) return 0;
    float3 s = 0, Ld;
    uint2 cl = LocalCluster(wp);
    uint cnt = cl.y == 0xFFFFFFFF ? min((uint)gLGrid.w, 4) : min(cl.y, 4);
    [loop] for (uint k = 0; k < cnt; k++) {
        LocalLightG L = tLights[cl.y == 0xFFFFFFFF ? k : tLIndex[cl.x + k]];
        float3 E = LocalLightE(L, wp, -dir, Ld);
        if (!any(E > 0)) continue;
        s += E * D_GGX_L(saturate(dot(dir, Ld)), a);
    }
    return s;
}

// the scene copy read bicubic at a mip level (a bilinear read of a coarse mip showed its texels as squares)
float3 SceneAt(float2 uv, float lod)
{
    uint w, h, n; tSceneC.GetDimensions(0, w, h, n);
    float l = clamp(round(lod), 0, n - 1);
    float2 sz = float2(max(w >> (uint)l, 1u), max(h >> (uint)l, 1u));
    float2 p = uv * sz - 0.5, i = floor(p), f = p - i;
    float2 f2 = f * f, f3 = f2 * f;
    float2 w0 = (1 - 3 * f + 3 * f2 - f3) / 6, w1 = (4 - 6 * f2 + 3 * f3) / 6, w2 = (1 + 3 * f + 3 * f2 - 3 * f3) / 6, w3 = f3 / 6;
    float2 g0 = w0 + w1, g1 = w2 + w3;
    float2 h0 = (i - 0.5 + w1 / g0) / sz, h1 = (i + 1.5 + w3 / g1) / sz;
    return g0.y * (g0.x * tSceneC.SampleLevel(sLin, float2(h0.x, h0.y), l).rgb + g1.x * tSceneC.SampleLevel(sLin, float2(h1.x, h0.y), l).rgb)
         + g1.y * (g0.x * tSceneC.SampleLevel(sLin, float2(h0.x, h1.y), l).rgb + g1.x * tSceneC.SampleLevel(sLin, float2(h1.x, h1.y), l).rgb);
}

float4 PSInside(FOut i) : SV_Target
{
    float2 ndc = float2(i.uv.x * 2 - 1, 1 - i.uv.y * 2);
    float3 dc = normalize(float3(ndc.x * vProj.x, ndc.y * vProj.y, 1));
    float3 d = normalize(dc.x * vCR.xyz + dc.y * vCU.xyz + dc.z * vCF.xyz);   // the ray in the suit's frame
    float az = atan2(d.x, d.z), el = asin(clamp(d.y, -1, 1));
    if (abs(az) > vGeo.y || el > vGeo.z || el < vLight.x) discard;
    float2 tex = TexOf(az, el);
    float4 L = Bicubic(tLens, tex * vGrid.zw);
    float cov = saturate(L.z * 1.3);
    if (cov < 0.004) discard;
    if (vLight.w > 1.5) return float4(0.3 * cov, 0.2 * cov, 0.1 * cov, cov);   // test runs (VisorDropsTestView): the blend alone
    // the glass here: one hit on the sphere, the tangents of the azimuth and elevation directions in its plane (Glass()
    // takes five hits for the exact texel size - not needed here)
    GPt g;
    g.P = Hit(d);
    g.n = normalize(g.P - vCenter.xyz * 1000);
    float3 au = float3(cos(az), 0, -sin(az)), ae = float3(-sin(el) * sin(az), cos(el), -sin(el) * cos(az));
    g.eu = normalize(au - dot(au, g.n) * g.n); g.ev = -normalize(ae - dot(ae, g.n) * g.n);
    float3 skyN = SkyLight(ToGlobal(g.n));                            // the sky's light round the glass, once
    float2 s = L.xy / max(L.z, 0.05);                                 // the slope where the drop is
    // the drop's outer face: the glass's normal tilted by the height's gradient; the ray leaves the liquid there by
    // Snell (1.28 -> 1): a lens a few mm across - the scene beyond inverted and squeezed, and past the critical angle
    // (51 deg) totally reflected: the dark ring at the rim
    float3 Nd = normalize(g.n - vGeo.w * (s.x * g.eu + s.y * g.ev));
    float3 dr = refract(d, -Nd, 1.28);
    float tir = dot(dr, dr) < 1e-4 ? 1 : 0;
    if (tir > 0.5) dr = d;
    float3 dcr = float3(dot(dr, vCR.xyz), dot(dr, vCU.xyz), dot(dr, vCF.xyz));
    float sl = length(s);
    float3 sc;
    if (dcr.z > 0.05) {
        float2 nd = float2(dcr.x / dcr.z / vProj.x, dcr.y / dcr.z / vProj.y);
        float2 uv = float2(nd.x * 0.5 + 0.5, 0.5 - nd.y * 0.5);
        // what lies beyond, through a lens of a few cm focal length: out of focus, the more the steeper
        sc = SceneAt(saturate(uv), 1.0 + 3.0 * saturate(sl * 1.5));
        float edge = saturate(min(min(uv.x, 1 - uv.x), min(uv.y, 1 - uv.y)) * 20);   // off the copy's edge: the sky
        sc = lerp(skyN * 0.6, sc, edge);
    } else sc = skyN * 0.6;
    // totally reflected: the helmet's dark inside (a faint glow of the sky scattered in it); the Fresnel losses
    float T = 0.955 * (1 - tir);
    float3 col = sc * T + tir * skyN * 0.04;
    // the Sun and the lamps through it: soft discs (the defocus) where the turned ray points at them
    if (vLight.w > 0.5) {
        float3 dg = normalize(ToGlobal(dr));
        float3 wp = ToGlobal(g.P) * 0.001;
        float a = vLight.y;
        float3 sunL = SunLight(wp);           // (the glass at the eye: a shadow map lookup per pixel cost too much)
        float cs = saturate(dot(dg, gSunDir.xyz));
        col += vLight.z * sunL * D_GGX_L(cs, a) * a * a * PI * saturate(sl * 3 + 0.2);
        col += vLight.z * LampsThrough(wp, dg, a) * a * a * PI * saturate(sl * 3 + 0.2);
    }
    return float4(col * cov, cov);
}

// ---------------------------------------------------------------- from outside: drops on the suits' glass
cbuffer VisorOut : register(b2)
{
    float4x4 oWVP, oW;
    float4 oWet;      // x wetness 0..1, y time, z cell size [m], w a vessel's seed
};
struct OIn { float3 p : POSITION; float3 n : NORMAL; float3 rest : TEXCOORD1; };
struct OOut { float4 pos : SV_Position; float3 wp : TEXCOORD0; float3 n : TEXCOORD1; float3 rest : TEXCOORD2; };
OOut VSOut(OIn v)
{
    OOut o;
    o.pos = mul(float4(v.p, 1), oWVP);
    o.wp = mul(float4(v.p, 1), oW).xyz;
    o.n = normalize(mul(v.n, (float3x3)oW));
    o.rest = v.rest;
    return o;
}
float Hash3(int3 c) { return Hash1((uint)c.x * 73856093u ^ (uint)c.y * 19349663u ^ (uint)c.z * 83492791u ^ (uint)oWet.w); }

float4 PSOut(OOut i, bool front : SV_IsFrontFace) : SV_Target
{
    float3 n = normalize(i.n) * (front ? 1 : -1);
    float3 V = normalize(-i.wp);
    // the nearest drop: one per cell at most (the wetter, the more), jittered, its size random; the cells in the
    // glass's rest frame (drops stay put as the head turns)
    float cs = oWet.z;
    float3 q = i.rest / cs;
    int3 c0 = (int3)floor(q);
    float best = 1e9; float3 off = 0; float br = 0;
    [unroll] for (int z = -1; z <= 1; z++) [unroll] for (int y = -1; y <= 1; y++) [unroll] for (int x = -1; x <= 1; x++) {
        int3 c = c0 + int3(x, y, z);
        float h0 = Hash3(c);
        if (h0 > oWet.x * 0.9) continue;
        float3 ctr = c + float3(Hash3(c + 101), Hash3(c + 211), Hash3(c + 307));
        float r = lerp(0.18, 0.55, Hash3(c + 401));
        float3 dq = q - ctr;
        float dd = length(dq) / r;
        if (dd < best) { best = dd; off = dq / r; br = r; }
    }
    if (best >= 1) discard;
    // the cap's normal: tilted outward along the offset (in the surface's plane), steepest at the rim
    float3 o3 = off - dot(off, n) * n;
    float rr = saturate(length(o3));
    float3 nd = normalize(n + o3 * 0.9);
    float cov = smoothstep(1.0, 0.8, best);
    float f0 = 0.0155;                                              // ((1.28 - 1) / 2.28)^2
    float nv = saturate(dot(nd, V));
    float F = f0 + (1 - f0) * pow(1 - nv, 5);
    float3 R = reflect(-V, nd);
    float3 env = SkyLight(R, i.wp);
    float3 col = env * F;
    // the Sun's glint and the lamps'
    float3 H = normalize(V + gSunDir.xyz);
    float spec = D_GGX_L(saturate(dot(nd, H)), 0.004) * F * saturate(dot(nd, gSunDir.xyz)) * 0.25;
    col += SunLight(i.wp) * ShadowAt(i.wp) * spec;
    col += LocalLights(i.wp, nd, n, V, 0, f0, 0.004);
    // a drop on the glass darkens what is seen through it at its rim (total reflection), barely at its centre
    float a = cov * lerp(0.12, 0.7, rr * rr);
    col = ApplyHaze(col, i.wp);
    return float4(col * cov, a);
}
