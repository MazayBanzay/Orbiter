// D3D11Client: the engines' smoke and condensate as a 3D flow (src\Smoke.cpp). A box of gas round the focus (a grid
// in the particles' local frame, y up), solved on the GPU each frame as an incompressible flow (Stam 1999, "Stable
// Fluids"): semi-Lagrangian advection of the velocity, the excess temperature and the visible mass; buoyancy
// (Boussinesq); vorticity confinement (Fedkiw, Stam & Jensen 2001) keeping the thin eddies the grid would smear;
// a pressure projection (Jacobi). The engines are sources: each jet drives its own free round jet's flow into the box
// (centreline 7 sqrt(J / rho) / z, half-velocity radius 0.094 z) with its heat, and puts its visible mass (the
// condensate where its mix passes the frost point; later soot, dust) where it forms. Drawn by ray-marching the box:
// extinction by the mass (k_ext), the Sun's light through the box's own shadow (a light volume), the sky's from above.
#include "exhaust_common.hlsli"

#define NSRC 8
cbuffer Smoke : register(b5)
{
    float4 kN;          // cells x, y, z; cell size h [m]
    float4 kDt;         // dt [s], vorticity confinement eps, temperature relaxation [1/s], mass decay [1/s]
    int4 kShift;        // the box moved by these cells since the last frame; w: sources
    float4 kAmb;        // x the ground's height in the box [m] (y), y gravity [m/s2], z ambient temperature [K], w time [s]
    float4 kSun;        // the Sun's direction in the box's frame; w extinction per kg [m2/kg]
    float4 kPhys;       // x the visible mass's settling speed [m/s], y 1 / the air's density [m3/kg], z the air's n - 1, w focal [px]
    float4 kSrc[NSRC * 3];   // per source: (exit in the box [m], exit radius) (direction, k = sqrt(J / rho)) (visible mass [kg/s], z_full, inject length, excess T)
    // drawing
    float4 kO;          // the box's corner from the camera (global); w: steps
    float4 kE1, kE2, kE3;   // the box's axes (global); w: its size along them [m]
    float4x4 kVP;
};

Texture3D<float4> tVel : register(t0);
Texture3D<float> tDen : register(t1);
Texture3D<float4> tCurl : register(t2);
Texture3D<float> tP : register(t3);
Texture3D<float> tDiv : register(t4);
RWTexture3D<float4> uVel : register(u0);
RWTexture3D<float> uDen : register(u1);
RWTexture3D<float4> uCurl : register(u2);
RWTexture3D<float> uOut : register(u3);
RWTexture3D<float2> uLight : register(u4);
SamplerState sLin3 : register(s3);

int3 Dim() { return int3(kN.xyz); }
bool Inside(int3 c) { return all(c >= 0) && all(c < Dim()); }
float3 CellPos(int3 c) { return (c + 0.5) * kN.w; }                 // [m] in the box
bool Solid(float3 p) { return p.y < kAmb.x; }                       // under the ground (or a liquid's surface)
float4 LoadV(int3 c) { return Inside(c) ? tVel.Load(int4(c, 0)) : 0; }
float3 SampleV(float3 g) { return tVel.SampleLevel(sLin3, g / kN.xyz, 0).xyz; }   // g in cells (continuous)

// ------------------------------------------------------------------------------------------ advection
[numthreads(8, 8, 8)]
void CSAdvect(uint3 id : SV_DispatchThreadID)
{
    int3 c = int3(id);
    if (!Inside(c)) return;
    float3 p = CellPos(c);
    if (Solid(p)) { uVel[c] = 0; uDen[c] = 0; return; }
    // the box moved: this cell was cell c + shift of the last frame's box; trace back along the velocity there (RK2)
    float3 g = c + kShift.xyz + 0.5;
    float dt = kDt.x, ih = 1 / kN.w;
    float3 v1 = SampleV(g);
    float3 gm = g - 0.5 * dt * ih * v1;
    float3 v2 = SampleV(gm);
    float3 gb = g - dt * ih * v2;
    const float gyc = kAmb.x * ih + kShift.y + 0.5;               // the ground in the last frame's cells (y)
    gb.y = max(gb.y, gyc);                                         // (a trace into the ground took mass from the impact)
    float3 gd = gb; gd.y = max(gd.y + dt * ih * kPhys.x, gyc);    // the mass also falls at its settling speed
    float4 vo = (any(gb < 0) || any(gb > kN.xyz)) ? 0 : tVel.SampleLevel(sLin3, gb / kN.xyz, 0);   // from outside: still gas
    float dO = (any(gd < 0) || any(gd > kN.xyz)) ? 0 : tDen.SampleLevel(sLin3, gd / kN.xyz, 0);
    uVel[c] = vo;
    uDen[c] = dO;
}

// ------------------------------------------------------------------------------------------ vorticity
[numthreads(8, 8, 8)]
void CSCurl(uint3 id : SV_DispatchThreadID)
{
    int3 c = int3(id);
    if (!Inside(c)) return;
    float ih = 0.5 / kN.w;
    float3 vxp = LoadV(c + int3(1, 0, 0)).xyz, vxm = LoadV(c - int3(1, 0, 0)).xyz;
    float3 vyp = LoadV(c + int3(0, 1, 0)).xyz, vym = LoadV(c - int3(0, 1, 0)).xyz;
    float3 vzp = LoadV(c + int3(0, 0, 1)).xyz, vzm = LoadV(c - int3(0, 0, 1)).xyz;
    float3 w = float3((vyp.z - vym.z) - (vzp.y - vzm.y), (vzp.x - vzm.x) - (vxp.z - vxm.z), (vxp.y - vxm.y) - (vyp.x - vym.x)) * ih;
    uCurl[c] = float4(w, length(w));
}

float Hash31(float3 p) { p = frac(p * 0.1031); p += dot(p, p.yzx + 33.33); return frac((p.x + p.y) * p.z); }

// ------------------------------------------------------------------------------------------ forces and sources
[numthreads(8, 8, 8)]
void CSForces(uint3 id : SV_DispatchThreadID)
{
    int3 c = int3(id);
    if (!Inside(c)) return;
    float3 p = CellPos(c);
    if (Solid(p)) { uVel[c] = 0; uDen[c] = 0; return; }
    float dt = kDt.x, h = kN.w;
    float4 v = tVel.Load(int4(c, 0));
    float rho = tDen.Load(int4(c, 0));
    // vorticity confinement: f = eps h (N x w), N the unit gradient of |w|
    {
        float ih = 0.5 / h;
        float gx = tCurl.Load(int4(min(c + int3(1, 0, 0), Dim() - 1), 0)).w - tCurl.Load(int4(max(c - int3(1, 0, 0), 0), 0)).w;
        float gy = tCurl.Load(int4(min(c + int3(0, 1, 0), Dim() - 1), 0)).w - tCurl.Load(int4(max(c - int3(0, 1, 0), 0), 0)).w;
        float gz = tCurl.Load(int4(min(c + int3(0, 0, 1), Dim() - 1), 0)).w - tCurl.Load(int4(max(c - int3(0, 0, 1), 0), 0)).w;
        float3 eta = float3(gx, gy, gz) * ih;
        float le = length(eta);
        if (le > 1e-5) v.xyz += dt * kDt.y * h * cross(eta / le, tCurl.Load(int4(c, 0)).xyz);
    }
    // buoyancy of the warm mix (Boussinesq) and its heat mixing out into the air
    v.y += dt * kAmb.y * v.w / (max(kAmb.z, 20) + max(v.w, 0));   // (rho_a - rho) / rho of a warmer ideal gas: g T' / (Ta + T')
    v.y -= dt * kAmb.y * rho * kPhys.y;                          // the mass it carries weighs it down: the cold cloud sinks and spreads
    v.w *= exp(-dt * kDt.z);
    // the engines
    for (int s = 0; s < kShift.w; s++) {
        float4 a = kSrc[3 * s], b = kSrc[3 * s + 1], m = kSrc[3 * s + 2];
        float3 d = p - a.xyz;
        float z = dot(d, b.xyz);
        float Li = m.z;
        if (z < -h || z > Li + h) continue;
        float r = length(d - z * b.xyz);
        float zc = max(z, 2 * h);
        float bw = 0.094 * zc + a.w;                                   // the jet's half-velocity radius
        if (r > 3 * bw + h) continue;
        // its flow: the free jet, imposed where it is (the grid cannot resolve the nozzle's centimetres), with the
        // turbulence's seed - the shear layer's own instability then rolls it into eddies
        float wv = exp(-0.693 * r * r / (bw * bw)) * saturate(1 - (z - Li) / h);
        float uz = min(7 * b.w / zc, 0.9 * h / max(dt, 1e-4) * 6);
        // the shear layer's instability: the jet wobbles a few degrees at a few Hz - smooth in space and time (white
        // noise per cell only speckled the mass)
        float3 p1 = normalize(cross(b.xyz, abs(b.y) < 0.9 ? float3(0, 1, 0) : float3(1, 0, 0))), p2 = cross(b.xyz, p1);
        float tt = kAmb.w;
        float w1 = 0.06 * sin(6.2831853 * 3.1 * tt + s * 7) + 0.04 * sin(6.2831853 * 5.3 * tt + s * 3);
        float w2 = 0.06 * sin(6.2831853 * 2.7 * tt + s * 5 + 1.3) + 0.04 * sin(6.2831853 * 4.9 * tt + s * 2);
        float3 tgt = (b.xyz + p1 * w1 + p2 * w2) * uz;
        v.xyz = lerp(v.xyz, tgt, saturate(wv * 0.6));
        v.w = max(v.w, m.w * wv);
        // its visible mass: formed over 0..z_full with the share (z / z_full)^2 (pdf 2 z / z_full^2) across the jet's
        // Gaussian; what forms beyond the injected length is put in at its end
        float sg = max(0.6 * bw, 0.6 * h);
        float g2 = exp(-r * r / (2 * sg * sg)) / (6.2831853 * sg * sg);
        float zf = max(m.y, h);
        float dF = z < zf && z >= 0 ? 2 * z / (zf * zf) : 0;
        if (zf > Li && z > Li - h && z <= Li) dF += (1 - (Li / zf) * (Li / zf)) / h;
        rho += dt * m.x * dF * g2;
    }
    rho *= exp(-dt * kDt.w);
    uVel[c] = v;
    uDen[c] = rho;
}

// ------------------------------------------------------------------------------------------ pressure
[numthreads(8, 8, 8)]
void CSDiv(uint3 id : SV_DispatchThreadID)
{
    int3 c = int3(id);
    if (!Inside(c)) return;
    // the walls: the ground is solid (no flow through it), the box's sides are open (the flow passes out)
    float3 v0 = tVel.Load(int4(c, 0)).xyz;
    float3 xp = Inside(c + int3(1, 0, 0)) ? tVel.Load(int4(c + int3(1, 0, 0), 0)).xyz : v0;
    float3 xm = Inside(c - int3(1, 0, 0)) ? tVel.Load(int4(c - int3(1, 0, 0), 0)).xyz : v0;
    float3 yp = Inside(c + int3(0, 1, 0)) ? tVel.Load(int4(c + int3(0, 1, 0), 0)).xyz : v0;
    float3 ym = Inside(c - int3(0, 1, 0)) && !Solid(CellPos(c - int3(0, 1, 0))) ? tVel.Load(int4(c - int3(0, 1, 0), 0)).xyz : float3(v0.x, -v0.y, v0.z);
    float3 zp = Inside(c + int3(0, 0, 1)) ? tVel.Load(int4(c + int3(0, 0, 1), 0)).xyz : v0;
    float3 zm = Inside(c - int3(0, 0, 1)) ? tVel.Load(int4(c - int3(0, 0, 1), 0)).xyz : v0;
    uOut[c] = Solid(CellPos(c)) ? 0 : ((xp.x - xm.x) + (yp.y - ym.y) + (zp.z - zm.z)) * (0.5 / kN.w);
}

float LoadP(int3 c, float self)
{
    if (!Inside(c)) return 0;                          // open sides: the ambient pressure
    if (Solid(CellPos(c))) return self;                // the ground: no gradient through it
    return tP.Load(int4(c, 0));
}

[numthreads(8, 8, 8)]
void CSJacobi(uint3 id : SV_DispatchThreadID)
{
    int3 c = int3(id);
    if (!Inside(c)) return;
    if (Solid(CellPos(c))) { uOut[c] = 0; return; }
    float s = tP.Load(int4(c, 0));
    float sum = LoadP(c + int3(1, 0, 0), s) + LoadP(c - int3(1, 0, 0), s) + LoadP(c + int3(0, 1, 0), s) +
                LoadP(c - int3(0, 1, 0), s) + LoadP(c + int3(0, 0, 1), s) + LoadP(c - int3(0, 0, 1), s);
    uOut[c] = (sum - tDiv.Load(int4(c, 0)) * kN.w * kN.w) / 6;
}

[numthreads(8, 8, 8)]
void CSProject(uint3 id : SV_DispatchThreadID)
{
    int3 c = int3(id);
    if (!Inside(c)) return;
    float4 v = tVel.Load(int4(c, 0));
    if (Solid(CellPos(c))) { uVel[c] = 0; return; }
    float s = tP.Load(int4(c, 0));
    float3 gp = float3(LoadP(c + int3(1, 0, 0), s) - LoadP(c - int3(1, 0, 0), s),
                       LoadP(c + int3(0, 1, 0), s) - LoadP(c - int3(0, 1, 0), s),
                       LoadP(c + int3(0, 0, 1), s) - LoadP(c - int3(0, 0, 1), s)) * (0.5 / kN.w);
    v.xyz -= gp;
    uVel[c] = v;
}

// ------------------------------------------------------------------------------------------ the light volume
// (half the grid's resolution): the transmittance towards the Sun and towards the sky (straight up) per cell
[numthreads(8, 8, 8)]
void CSLight(uint3 id : SV_DispatchThreadID)
{
    int3 lc = int3(id), ld = Dim() / 2;
    if (any(lc >= ld)) return;
    float3 p = (lc + 0.5) * 2 * kN.w;
    float3 size = kN.xyz * kN.w;
    float st = 2 * kN.w, k = kSun.w;
    float tauS = 0, tauU = 0;
    float3 q = p;
    [loop] for (int i = 0; i < 32; i++) {
        q += kSun.xyz * st;
        if (any(q < 0) || any(q > size)) break;
        tauS += tDen.SampleLevel(sLin3, q / size, 0) * k * st;
    }
    q = p;
    [loop] for (int j = 0; j < 24; j++) {
        q.y += st;
        if (q.y > size.y) break;
        tauU += tDen.SampleLevel(sLin3, q / size, 0) * k * st;
    }
    uLight[lc] = float2(exp(-tauS), exp(-tauU));
}

// ------------------------------------------------------------------------------------------ drawing
Texture3D<float> tDenR : register(t22);
Texture3D<float2> tLightR : register(t23);

struct VSOut { float4 pos : SV_Position; float3 wp : TEXCOORD0; nointerpolation float3 n : NORMAL; };

VSOut VSDraw(uint vid : SV_VertexID)
{
    VSOut o;
    float3 n; float3 c = ExCube(vid, n) * 0.5 + 0.5;
    o.wp = kO.xyz + kE1.xyz * (c.x * kE1.w) + kE2.xyz * (c.y * kE2.w) + kE3.xyz * (c.z * kE3.w);
    o.pos = mul(float4(o.wp, 1), kVP);
    o.n = kE1.xyz * n.x + kE2.xyz * n.y + kE3.xyz * n.z;
    return o;
}

float HGrel(float g, float c) { float t = 1 + g * g - 2 * g * c; return min((1 - g * g) / (t * sqrt(t)), 25); }

float4 PSDraw(VSOut i) : SV_Target
{
    if (dot(i.n, i.wp) < 0) discard;          // the far faces: each pixel once, also from inside the box
    float3 dir = normalize(i.wp);
    float3 sp; float sd = ExSceneDist(i.pos.xy, sp);
    // the ray in the box's frame
    float3 o = -kO.xyz;
    float3 ro = float3(dot(o, kE1.xyz), dot(o, kE2.xyz), dot(o, kE3.xyz));
    float3 rd = float3(dot(dir, kE1.xyz), dot(dir, kE2.xyz), dot(dir, kE3.xyz));
    float3 size = float3(kE1.w, kE2.w, kE3.w);
    float3 inv = 1 / (abs(rd) > 1e-8 ? rd : 1e-8);
    float3 t0 = (0 - ro) * inv, t1 = (size - ro) * inv;
    float ta = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0));
    float tb = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), min(max(t0.z, t1.z), sd));
    if (tb <= ta) discard;
    int n = (int)kO.w;
    float ds = (tb - ta) / n;
    float jit = frac(52.9829189 * frac(dot(i.pos.xy, float2(0.06711056, 0.00583715))) + frac(kAmb.w * 60 * 0.618034));
    // the light at the box: the Sun's (the FX atmosphere's, shadowed by the terrain at the box) and the sky's
    float3 mid = kO.xyz + 0.5 * (kE1.xyz * kE1.w + kE2.xyz * kE2.w + kE3.xyz * kE3.w);
    float3 upW = kE2.xyz;
    float3 Esun = SunLight(mid);
    float3 Esky = SkyLight(upW, mid), Egnd = SkyLight(-upW, mid);
    float ph = HGrel(0.75, dot(dir, gSunDir.xyz));
    const float alb = 0.97;                     // ice: it scatters what it takes
    float3 L = 0; float tr = 1;
    [loop] for (int k = 0; k < n; k++) {
        float t = ta + (k + jit) * ds;
        float3 q = ro + rd * t;
        float3 uvw = q / size;
        float m = tDenR.SampleLevel(sLin3, uvw, 0);
        if (m <= 1e-7) continue;
        float sig = m * kSun.w;
        float a = 1 - exp(-sig * ds);
        float2 lt = tLightR.SampleLevel(sLin3, uvw, 0);
        // single scattering of the Sun through the cloud's own shade; the ambient from above (the sky) and below (the
        // ground's light) through what lies over it - as the medium thickens, many scatterings: towards the plain mean
        float3 Li = Esun * lt.x * lerp(ph, 1.0, 0.5 * (1 - lt.x)) + lerp(Egnd, Esky * lt.y, 0.6);
        L += tr * a * alb * Li;
        tr *= 1 - a;
        if (tr < 2e-3) break;
    }
    if (tr > 0.998) discard;
    float3 at = ExAirTrans(ta);
    return float4(L * at, 1 - tr);
}

// ------------------------------------------------------------------------------------------ the heat haze
// The hot gas's refraction: n - 1 = K rho, the gas at the ambient pressure, rho ~ 1 / T, so n' = -(n0 - 1) T' / (Ta + T').
// A ray through it turns by the integral of the gradient of n across it (alpha = int grad_perp n ds); the scene behind
// is seen displaced by that angle. Marched through the box against the depth (only what lies behind the gas moves),
// written as a screen offset (uv) into the shimmer pass's target, added to its other sources.
Texture3D<float4> tVelR : register(t24);

float NPrime(float3 q, float3 size)
{
    float T = tVelR.SampleLevel(sLin3, q / size, 0).w;
    return -kPhys.z * max(T, 0) / (kAmb.z + max(T, 0));
}

float4 PSShimmer(VSOut i) : SV_Target
{
    if (dot(i.n, i.wp) < 0) discard;
    float3 dir = normalize(i.wp);
    float3 sp; float sd = ExSceneDist(i.pos.xy, sp);
    float3 o = -kO.xyz;
    float3 ro = float3(dot(o, kE1.xyz), dot(o, kE2.xyz), dot(o, kE3.xyz));
    float3 rd = float3(dot(dir, kE1.xyz), dot(dir, kE2.xyz), dot(dir, kE3.xyz));
    float3 size = float3(kE1.w, kE2.w, kE3.w);
    float3 inv = 1 / (abs(rd) > 1e-8 ? rd : 1e-8);
    float3 t0 = (0 - ro) * inv, t1 = (size - ro) * inv;
    float ta = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0));
    float tb = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), min(max(t0.z, t1.z), sd));
    if (tb <= ta) discard;
    const int n = 40;
    float ds = (tb - ta) / n, e = kN.w;
    float jit = frac(52.9829189 * frac(dot(i.pos.xy, float2(0.06711056, 0.00583715))) + frac(kAmb.w * 60 * 0.618034));
    float3 a = 0;                                     // the integral of grad n along the ray (box frame) [rad]
    [loop] for (int k = 0; k < n; k++) {
        float3 q = ro + rd * (ta + (k + jit) * ds);
        float c0 = NPrime(q, size);
        if (c0 > -1e-9) continue;
        float3 gr = float3(NPrime(q + float3(e, 0, 0), size) - NPrime(q - float3(e, 0, 0), size),
                           NPrime(q + float3(0, e, 0), size) - NPrime(q - float3(0, e, 0), size),
                           NPrime(q + float3(0, 0, e), size) - NPrime(q - float3(0, 0, e), size)) / (2 * e);
        a += gr * ds;
    }
    a -= rd * dot(a, rd);                              // across the ray only
    float3 aw = kE1.xyz * a.x + kE2.xyz * a.y + kE3.xyz * a.z;   // global
    // the angle as a shift on screen: a point 10 m out along the ray and the same turned by alpha
    float4 p1 = mul(float4(dir * 10, 1), kVP), p2 = mul(float4((dir + aw) * 10, 1), kVP);
    float2 d = (p2.xy / p2.w - p1.xy / p1.w) * float2(0.5, -0.5);
    if (dot(d, d) < 1e-14) discard;
    return float4(d, 0, 0);
}
