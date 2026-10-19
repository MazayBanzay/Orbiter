// D3D11Client GPU particles (src\GpuParticles.cpp): what wheels, feet, jets and the modules' sources throw - grains and
// dust, wet clods, drops and mist, ice crystals - flown and drawn on the GPU.
//  - The pool (65536) with a dead list (consume) and this frame's alive list (append, drawn indirectly).
//  - Positions in a frame fixed to the planet: origin on the mean sphere under the focus, y up.
//  - Emit: the newborn from the CPU; a grain or a clod takes the colour of the ground it came from - the scene's pixel
//    under its birth point divided by the light there (the albedo), so dust and mud are that ground's own colour.
//  - Flight: gravity (with buoyancy), Schiller-Naumann drag towards the gas (the jets' wall jets, the free jets, a puff's
//    gas, eddies; mist and fines also curl-noise turbulence, Bridson 2007); drops break up in flight to the Weber limit
//    d = 12 sigma / (rho_g u^2); nothing but gravity in vacuum. The surface under each stops it: grains bounce or settle,
//    clods stop, drops vanish into a liquid or leave a wet spot on the ground.
//  - Drawing: drops as streaks along their motion over the frame (a camera's exposure), lit as a cloud of clear drops -
//    strongly forward scattering (Henyey-Greenstein g 0.8) under the Sun and the sky; mist the same, soft; dust with the
//    ground's albedo, g 0.4; clods as lumps with their own normals; soft against the scene's depth.
#include "exhaust_common.hlsli"

struct Part
{
    float3 p; float age;
    float3 v; float life;
    float3 ug0; float tg;
    float3 gust; float gustT;
    float d, rhoP, s0, a0;
    float gy; uint flags; float seed; uint col;   // col: the ground's albedo, packed (sqrt, 10:10:10); 0 = the class's
};
// flags: bits 0-3 look, 4-7 albedo index, 8 settled, 9 on a liquid, 10 hard (a clod), 11 take the ground's colour
#define LOOK_GRAIN 0
#define LOOK_CLOD 1
#define LOOK_DROP 2
#define LOOK_FOG 3
#define LOOK_ICE 4
#define LOOK_SPOT 5
#define LOOK_SPLASH 6
#define LOOK_MIST 7
#define F_SETTLED 0x100u
#define F_LIQUID 0x200u
#define F_HARD 0x400u
#define F_GROUNDCOL 0x800u

cbuffer Sim : register(b5)
{
    float4 sTime;      // x dt, y sub-step, z sub-steps, w time
    float4 sGas;       // x gas density, y viscosity, z dense (0/1), w gravity [m/s2]
    float4 sGeo;       // x anchor radius, y new particles, z impacts, w free jets
    float4 sMisc;      // x boxes, y frame
    float4 sImp[16];   // per impact: (hit, uw) (r, 0, 0, 0)
    float4 sFree[16];  // per free jet: (origin, k = sqrt(J / rho)) (axis, speed cap)
    float4 sBox[64];   // per box: (centre, 0) (axis 0, half 0) (axis 1, half 1) (axis 2, half 2)
    float4 sM0, sM1, sM2;   // local -> camera-centred global: rows; w the offset
    float4x4 sVP;           // camera-centred global -> clip
    float4 sLiq;            // x surface tension [N/m], y the liquid's density, z its level (local), w 1 = the scene copy is bound
};
cbuffer Count : register(b6) { uint4 cDead; }

RWStructuredBuffer<Part> uPool : register(u0);
ConsumeStructuredBuffer<uint> uDeadC : register(u1);
AppendStructuredBuffer<uint> uDeadA : register(u1);
AppendStructuredBuffer<uint> uAlive : register(u2);
StructuredBuffer<Part> tNew : register(t4);
Texture2D tSceneC : register(t3);

float3 ToWorldS(float3 p) { return float3(dot(sM0.xyz, p), dot(sM1.xyz, p), dot(sM2.xyz, p)) + float3(sM0.w, sM1.w, sM2.w); }

uint PackCol(float3 c) { c = sqrt(saturate(c)); uint3 q = (uint3)(c * 1023 + 0.5); return max(1u, q.r | (q.g << 10) | (q.b << 20)); }
float3 UnpackCol(uint u) { float3 c = float3(u & 1023, (u >> 10) & 1023, (u >> 20) & 1023) / 1023.0; return c * c; }

// ------------------------------------------------------------------------------------------ init: all dead
[numthreads(256, 1, 1)]
void CSInit(uint3 id : SV_DispatchThreadID)
{
    Part q = (Part)0;
    uPool[id.x] = q;
    uDeadA.Append(id.x);
}

// ------------------------------------------------------------------------------------------ emit
// the ground's albedo under a local point: the scene's colour there over the light it gets (0: not seen)
float3 GroundAlbedo(float3 pl)
{
    float3 wp = ToWorldS(pl);
    float4 c = mul(float4(wp, 1), sVP);
    if (c.w <= 0) return 0;
    float2 ndc = c.xy / c.w;
    if (any(abs(ndc) > 0.98)) return 0;
    uint w, h; tSceneC.GetDimensions(w, h);
    int2 pix = int2((ndc.x * 0.5 + 0.5) * w, (0.5 - ndc.y * 0.5) * h);
    float3 sw; float sd = ExSceneDist(pix + 0.5, sw);
    float dist = length(wp);
    if (abs(sd - dist) > 0.04 * dist + 0.15) return 0;          // something else in front (the wheel, the hull)
    float3 up = normalize(float3(sM0.y, sM1.y, sM2.y));
    float3 L = SunLight(wp) * ShadowAt(wp) * saturate(dot(up, gSunDir.xyz)) + SkyLight(up, wp);
    float3 C = tSceneC.Load(int3(pix, 0)).rgb;
    float3 a = C / max(L, 1e-6);
    return (any(a > 1.5) || all(a < 1e-4)) ? 0 : saturate(a);
}

[numthreads(64, 1, 1)]
void CSEmit(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)sGeo.y || id.x >= cDead.x) return;
    uint i = uDeadC.Consume();
    Part q = tNew[id.x];
    if ((q.flags & F_GROUNDCOL) && sLiq.w > 0.5) {
        // the ground under its birth point, else round it (the wheel itself may hide the patch)
        float3 g = float3(q.p.x, q.gy - dot(q.p.xz, q.p.xz) / (2 * sGeo.x), q.p.z);
        float3 a = GroundAlbedo(g);
        static const float2 ring[4] = { float2(1.2, 0), float2(-1.2, 0), float2(0, 1.2), float2(0, -1.2) };
        [unroll] for (int k = 0; k < 4; k++) if (all(a <= 0)) a = GroundAlbedo(g + float3(ring[k].x, 0, ring[k].y));
        if (any(a > 0)) q.col = PackCol(a * ((q.flags & F_HARD) ? 0.75 : 1.0));   // a clod: the ground wetted, darker
    }
    uPool[i] = q;
}

// ------------------------------------------------------------------------------------------ flight
float Height(float3 p) { return p.y + dot(p.xz, p.xz) / (2 * sGeo.x); }
float3 UpAt(float3 p) { return normalize(float3(p.x / sGeo.x, 1, p.z / sGeo.x)); }

// curl of a vector potential made of three value noises (divergence-free swirls, Bridson 2007), ~unit amplitude
float3 Curl(float3 p)
{
    const float e = 0.15;
    float3 dx = float3(e, 0, 0), dy = float3(0, e, 0), dz = float3(0, 0, e);
    float3 o1 = float3(31.4, 7.1, 13.9), o2 = float3(-17.3, 23.7, 5.3);
    float dPz_dy = ExNoise(p + o2 + dy) - ExNoise(p + o2 - dy), dPy_dz = ExNoise(p + o1 + dz) - ExNoise(p + o1 - dz);
    float dPx_dz = ExNoise(p + dz) - ExNoise(p - dz), dPz_dx = ExNoise(p + o2 + dx) - ExNoise(p + o2 - dx);
    float dPy_dx = ExNoise(p + o1 + dx) - ExNoise(p + o1 - dx), dPx_dy = ExNoise(p + dy) - ExNoise(p - dy);
    return float3(dPz_dy - dPy_dz, dPx_dz - dPz_dx, dPy_dx - dPx_dy) / (2 * e) * 0.35;
}

float3 GasVelocity(float3 p, Part q)
{
    float3 u = 0;
    int ni = (int)sGeo.z, nf = (int)sGeo.w;
    for (int k = 0; k < ni; k++) {          // the wall jets spreading from where a jet meets the ground
        float3 hit = sImp[2 * k].xyz; float uw = sImp[2 * k].w, r = sImp[2 * k + 1].x;
        float3 dp = p - hit;
        float z = max(dp.y, 0);
        float2 hz = dp.xz; float rh = length(hz);
        if (rh < 1e-3) { u.y += 0.3 * uw; continue; }
        float ur = uw * r / max(rh, r);
        float prof = exp(-z / (0.12 * rh + 0.15));
        u += float3(hz.x / rh, 0.12, hz.y / rh) * ur * prof;
    }
    for (int j = 0; j < nf; j++) {          // free round jets: centreline 7 k / x, Gaussian, half-velocity radius 0.094 x
        float3 o = sFree[2 * j].xyz, ax = sFree[2 * j + 1].xyz;
        float kk = sFree[2 * j].w, cap = sFree[2 * j + 1].w;
        float3 dp = p - o; float x = dot(dp, ax);
        if (x < 0.01) continue;
        float r2 = dot(dp, dp) - x * x, b = 0.094 * x;
        u += ax * min(7.0 * kk / x, cap) * exp(-0.693 * r2 / (b * b));
    }
    if (q.tg > 0) u += q.ug0 * exp(-q.age / q.tg);
    return u;
}

void PushOutOfBoxes(inout float3 p, inout float3 v, uint flags)
{
    int nb = (int)sMisc.x;
    for (int b = 0; b < nb; b++) {
        float3 c = sBox[4 * b].xyz;
        float3 dp = p - c, l, hh;
        float3 ax0 = sBox[4 * b + 1].xyz, ax1 = sBox[4 * b + 2].xyz, ax2 = sBox[4 * b + 3].xyz;
        hh = float3(sBox[4 * b + 1].w, sBox[4 * b + 2].w, sBox[4 * b + 3].w);
        l = float3(dot(dp, ax0), dot(dp, ax1), dot(dp, ax2));
        float3 pen = hh - abs(l);
        if (any(pen <= 0)) continue;
        float3 n; float depth;
        if (pen.x < pen.y && pen.x < pen.z) { n = ax0 * sign(l.x); depth = pen.x; }
        else if (pen.y < pen.z) { n = ax1 * sign(l.y); depth = pen.y; }
        else { n = ax2 * sign(l.z); depth = pen.z; }
        p += n * (depth + 1e-3);
        float vn = dot(v, n);
        if (vn < 0) {
            uint look = flags & 15;
            bool stick = look == LOOK_CLOD || look == LOOK_DROP;
            v -= n * vn * (stick ? 1.0 : 1.3);     // a dry grain bounces a little, a clod or a drop sticks
            v *= stick ? 0.3 : 0.6;                 // ... and slides off
        }
    }
}

[numthreads(256, 1, 1)]
void CSSim(uint3 id : SV_DispatchThreadID)
{
    Part q = uPool[id.x];
    if (q.life <= 0) return;
    float dt = sTime.x;
    if (dt > 0) {
        float rhoG = sGas.x, mu = sGas.y;
        bool dense = sGas.z > 0.5;
        uint look = q.flags & 15;
        bool flat = look == LOOK_SPOT || look == LOOK_SPLASH;
        bool airborne = look == LOOK_MIST || look == LOOK_FOG || q.d < 3e-5;   // carried by the air's eddies
        if (!(q.flags & F_SETTLED) && !flat) {
            int ns = (int)sTime.z; float h = sTime.y;
            for (int s = 0; s < ns; s++) {
                float3 up = UpAt(q.p);
                float3 a = -up * sGas.w * (1 - rhoG / q.rhoP);       // gravity less buoyancy
                if (dense) {
                    q.gustT -= h;
                    float3 ug = GasVelocity(q.p, q);
                    if (q.gustT <= 0) {     // a new eddy: cubed uniform per axis, 0.3 of the local flow
                        uint sd = ExHash(id.x * 977u + (uint)(sTime.w * 60.0) * 131u + s);
                        float3 r3 = float3(ExRand(sd), ExRand(sd + 1), ExRand(sd + 2)) * 2 - 1;
                        q.gust = r3 * r3 * r3 * 0.3 * (length(ug) + 0.05);
                        q.gustT = 0.2 + 0.4 * ExRand(sd + 3);
                    }
                    float3 uf = ug + q.gust;
                    if (airborne && s == 0) uf += Curl(q.p * 0.8 + float3(0, 0, sTime.w * 0.1)) * (0.15 + 0.3 * length(ug));
                    float3 rel = q.v - uf;
                    float rv = length(rel);
                    // a drop breaks up in the gas to the Weber limit (bag break-up, We ~12): more, smaller drops, the cloud
                    // denser for the same mass (opacity ~ m / (rho d))
                    if (look == LOOK_DROP && sLiq.x > 0) {
                        float dMax = 12 * sLiq.x / max(rhoG * rv * rv, 1e-9);
                        if (q.d > dMax) { q.a0 = min(0.9, q.a0 * q.d / max(dMax, 1e-6)); q.d = max(dMax, 2e-5); }
                        if (q.d < 5e-5) q.flags = (q.flags & ~15u) | LOOK_MIST;   // atomised: mist
                    }
                    float Re = rhoG * rv * q.d / mu;
                    float f = max(1 + 0.15 * pow(max(Re, 1e-6), 0.687), 0.01833 * Re);   // Schiller-Naumann, Newton's Cd 0.44 above Re ~1000
                    float tau = q.rhoP * q.d * q.d / (18 * mu);
                    q.v = uf + rel * exp(-h * f / tau);
                }
                q.v += a * h;
                q.p += q.v * h;
                PushOutOfBoxes(q.p, q.v, q.flags);
                // the surface under it
                float hh = Height(q.p);
                if (hh < q.gy) {
                    float3 n = UpAt(q.p);
                    q.p += n * (q.gy - hh);
                    float sp = length(q.v);
                    if (look == LOOK_DROP || look == LOOK_MIST) {
                        if ((q.flags & F_LIQUID) || look == LOOK_MIST) {   // into the liquid (or mist settling): gone
                            q.life = q.age;
                        } else {                    // onto dry ground: a wet spot soaking in
                            q.flags = (q.flags & ~15u) | LOOK_SPOT;
                            q.s0 = clamp(q.s0 * 0.6 + 0.03, 0.03, 0.4); q.a0 = 0.25;
                            q.life = q.age + 5 + 4 * frac(q.seed * 7.3);
                        }
                        q.v = 0;
                        break;
                    }
                    bool bounce = look != LOOK_FOG && !(q.flags & F_HARD) && q.d > 1e-3 && sp > 1;
                    if (bounce) { q.v = (q.v - 2 * dot(q.v, n) * n) * 0.3; }
                    else {
                        q.flags |= F_SETTLED; q.v = 0;
                        q.life = min(q.life, q.age + (look == LOOK_FOG ? 0.3 : (q.flags & F_HARD) ? 2.5 : 1.5));
                        break;
                    }
                }
            }
        }
        q.age += dt;
    }
    if (q.age >= q.life) {
        q.life = 0;
        uPool[id.x] = q;
        uDeadA.Append(id.x);
        return;
    }
    uPool[id.x] = q;
    uAlive.Append(id.x);
}

// ------------------------------------------------------------------------------------------ drawing
cbuffer Draw : register(b5)
{
    float4 dM0, dM1, dM2;   // local -> camera-centred global: rows; w the offset
    float4 dCamR, dCamU;    // the camera's right and up (global); dCamR.w time; dCamU.w the frame's exposure time [s]
    float4 dGeo;            // x anchor radius, y dense, z the liquid's level (local; -1e9 none), w focal length [px]
    float4x4 dVP;           // camera-centred global -> clip
    float4 dAlb[16];        // albedo per class
};
StructuredBuffer<Part> tPool : register(t4);
StructuredBuffer<uint> tAlive : register(t3);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float3 wp : TEXCOORD1;
    nointerpolation float4 col : COLOR0;     // albedo, alpha
    nointerpolation float4 misc : TEXCOORD2; // x look, y seed, z half-size, w HG asymmetry
    nointerpolation float3 sun : TEXCOORD3;  // the Sun's light there (shadowed)
    nointerpolation float3 sky : TEXCOORD4;  // the sky's light on an upward face
    nointerpolation float3 gnd : TEXCOORD5;  // ... on a downward face (the ground's bounce)
};

float3 ToWorld(float3 p) { return float3(dot(dM0.xyz, p), dot(dM1.xyz, p), dot(dM2.xyz, p)) + float3(dM0.w, dM1.w, dM2.w); }
float3 DirToWorld(float3 d) { return float3(dot(dM0.xyz, d), dot(dM1.xyz, d), dot(dM2.xyz, d)); }

// Henyey-Greenstein relative to isotropic (4 pi p), capped
float HG(float g, float c) { float t = 1 + g * g - 2 * g * c; return min((1 - g * g) / (t * sqrt(t)), 25); }

VSOut VS(uint vid : SV_VertexID, uint inst : SV_InstanceID)
{
    VSOut o = (VSOut)0;
    Part q = tPool[tAlive[inst]];
    uint look = q.flags & 15;
    bool dense = dGeo.y > 0.5;
    float t = q.age, life = q.life;
    float s = q.s0, a = q.a0;
    bool fines = q.d < 5e-5;
    if (look == LOOK_SPOT) { s = q.s0; a = q.a0 * saturate((life - t) / 2.0) * min(1, 4 * t); }
    else if (q.flags & F_HARD) { a = q.a0 * min(1, 30 * t) * ((q.flags & F_SETTLED) ? saturate((life - t) / 2) : 1); }
    else if (look == LOOK_DROP) { a = q.a0 * min(1, 20 * t) * saturate((life - t) / 0.5); }
    else if (look == LOOK_FOG && life < 6) {
        s = min(q.s0 + (dense ? 0.3 : 0.6) * t, 3);
        float x = t / life; a = q.a0 * min(1, 20 * t) * max(0, 1 - x * x);
    } else {
        // a puff spreading: the same mass over a larger area - opacity falls as (s0 / s)^2
        s = min(q.s0 + (dense ? 0.10 : 0.02) * t * (fines ? 2 : 1), 4);
        float fade = min(3, 0.5 * life);
        a = q.a0 * min(1, 8 * t) * saturate((life - t) / fade) * min(1, pow(1.5 * q.s0 / s, 2));
        if (q.flags & F_SETTLED) a *= saturate((life - t) / 1.5);
    }
    float3 wp = ToWorld(q.p);
    float3 upW = normalize(DirToWorld(float3(0, 1, 0)));
    bool flat = look == LOOK_SPOT;
    if (!flat && !(q.flags & F_SETTLED) && look != LOOK_DROP) {   // a sprite does not poke into the ground
        float h = dot(q.p.xz, q.p.xz) / (2 * dGeo.x) + q.p.y - q.gy;
        float lift = (q.flags & F_HARD ? 0.8 : 0.6) * s - h;
        if (lift > 0) wp += upW * lift;
    }
    float dist = length(wp);
    a *= saturate((dist - 0.5 * s) / (1.5 * s));          // near the camera: fade out
    a *= saturate((450 - s * dGeo.w / max(dist, 1e-3)) / 250);   // a sprite over ~a fifth of the screen: faded out (overdraw)
    // nothing under a liquid's surface (its pass writes no depth to hide it)
    if (!flat && q.p.y + dot(q.p.xz, q.p.xz) / (2 * dGeo.x) < dGeo.z - 0.02) a = 0;
    if (dist < 0.3 || dist > 4000 || a < 0.003) { o.pos = float4(0, 0, -1, 1); return o; }
    static const float2 corner[6] = { float2(-1, -1), float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(1, -1) };
    float2 c = corner[vid];
    float3 V = wp / dist;
    float3 R, U; float sx = s, sy = s;
    if (flat) {
        R = normalize(DirToWorld(float3(1, 0, 0))); U = normalize(DirToWorld(float3(0, 0, 1)));
        wp += upW * 0.03;
    } else if (look == LOOK_DROP) {
        // one drop, drawn as its streak over the frame's exposure along its velocity (as real-time spray and rain are
        // drawn): never under ~a pixel wide - a drop smaller than that keeps its share of the light (opacity d / width)
        float3 vw = DirToWorld(q.v);
        float3 vp = vw - dot(vw, V) * V;
        float L = length(vp) * dCamU.w;
        R = L > 1e-5 ? vp / length(vp) : dCamR.xyz;
        U = normalize(cross(V, R));
        s = max(0.5 * q.d, 0.7 * dist / max(dGeo.w, 1));
        sx = s + 0.5 * L; sy = s;
        wp -= vw * (0.5 * dCamU.w);
        a *= sqrt(saturate(q.d / (2 * s))) * sqrt(s / sx);
    } else {
        float ang = q.seed * 6.2832 + ((q.flags & F_HARD) ? q.age * (2 + 6 * frac(q.seed * 13.7)) : 0);   // clods tumble
        float cs = cos(ang), sn = sin(ang);
        R = dCamR.xyz * cs + dCamU.xyz * sn; U = -dCamR.xyz * sn + dCamU.xyz * cs;
    }
    float3 p = wp + R * c.x * sx + U * c.y * sy;
    o.pos = mul(float4(p, 1), dVP);
    o.uv = c * 0.5 + 0.5;
    o.wp = p;
    uint ai = (q.flags >> 4) & 15;
    float3 alb = dAlb[ai].rgb;
    if (q.col) alb = UnpackCol(q.col);                       // the ground's own colour
    else if (FxActive() && ai <= 1) alb = mGround.rgb * (ai == 1 ? 0.6 : 1.0);
    float tr = ExAirTrans(dist).g;
    o.col = float4(alb, a);
    float g = (look == LOOK_DROP || look == LOOK_MIST || look == LOOK_FOG) ? 0.8 : look == LOOK_ICE ? 0.6 : fines ? 0.5 : 0.3;
    o.misc = float4(look, q.seed, s, g);
    o.sun = SunLight(wp) * ShadowAt(wp) * tr;
    o.sky = SkyLight(upW, wp) * tr;
    o.gnd = SkyLight(-upW, wp) * tr;
    return o;
}

float4 PS(VSOut i) : SV_Target
{
    float2 d = i.uv * 2 - 1;
    float r2 = dot(d, d);
    if (r2 > 1) discard;
    uint look = (uint)i.misc.x;
    float seed = i.misc.y, t = dCamR.w;
    float n1 = ExNoise(float3(i.uv * 3.0 + seed * 7.3, seed * 3.1 + t * 0.03));
    float a = i.col.a;
    float3 alb = i.col.rgb;
    float3 V = normalize(i.wp);
    float3 Lsun = gSunDir.xyz;
    float3 c;
    if (look == LOOK_SPOT) {          // a wet spot only darkens
        a *= saturate((0.70 + 0.35 * n1 - r2) * 4) * 0.5;
        return float4(0, 0, 0, a);
    }
    if (look == LOOK_CLOD) {
        // a lump: a bumpy sphere's normal, lit as the ground it came from, opaque inside a ragged outline
        float edge = 0.55 + 0.35 * n1;
        if (r2 > edge) discard;
        float3 nl = float3(d.x, d.y, -sqrt(saturate(1 - r2 / edge)));
        float n2 = ExNoise(float3(d * 4 + seed * 11, seed * 5));
        nl = normalize(nl + float3(n2 - 0.5, n1 - 0.5, 0) * 0.8);
        float3 R = normalize(cross(float3(0, 1, 0), V)); if (any(isnan(R))) R = float3(1, 0, 0);
        float3 U = cross(V, R);
        float3 N = R * nl.x + U * nl.y + V * nl.z;
        float upk = dot(N, normalize(float3(dM0.y, dM1.y, dM2.y))) * 0.5 + 0.5;
        c = alb * (i.sun * saturate(dot(N, Lsun)) + lerp(i.gnd, i.sky, upk));
        a = 1;
    } else {
        // the sprite as a soft ball: a normal over its disc (its own axes as the vertex shader turned them; towards the eye)
        float ang = seed * 6.2832;
        float cs = cos(ang), sn = sin(ang);
        float3 Rw = look == LOOK_DROP ? dCamR.xyz : dCamR.xyz * cs + dCamU.xyz * sn;
        float3 Uw = look == LOOK_DROP ? dCamU.xyz : -dCamR.xyz * sn + dCamU.xyz * cs;
        float3 N = normalize(Rw * d.x + Uw * d.y - V * sqrt(saturate(1 - r2)));
        float3 upW = normalize(float3(dM0.y, dM1.y, dM2.y));
        float ph = HG(i.misc.w, dot(V, Lsun));          // scattering angle: the light along -Lsun, out along -V
        if (look == LOOK_DROP) {
            // a clear drop's streak: a capsule (rounded ends), a bright core where it refracts the sky behind it and
            // mirrors the Sun (a glint that comes and goes as it tumbles), the sky and the ground round its rim
            float across = abs(d.y), along = abs(d.x);
            a *= pow(saturate(1 - across * across), 1.5) * saturate((1 - along) * 5);
            float core = pow(saturate(1 - across), 4);
            float glint = step(0.7, frac(seed * 91.7 + t * 1.7)) * pow(saturate(dot(reflect(V, N), Lsun) * 0.5 + 0.5), 8);
            c = alb * (0.9 * i.sky + 0.35 * i.gnd + i.sun * ph * 0.3) * (0.75 + 0.5 * core) + i.sun * glint * core * 3;
        } else {
            // the outline torn by a finer octave (no round sprite edges); mist and fog thinner per sprite, more of them
            float n2 = ExNoise(float3(i.uv * 7.0 + seed * 13.1, seed * 5.7 + t * 0.05));
            a *= pow(saturate(1 - r2 * (0.7 + 0.8 * n1 + 0.6 * n2)), 2) * saturate(0.25 + 1.2 * n1 * (0.6 + 0.8 * n2));
            if (look == LOOK_MIST || look == LOOK_FOG) a = min(a, 0.35);
            // a cloud of grains: thin, it scatters singly (the phase function: bright against the Sun); thick, it is a body
            // lit on its sunward side and in its own shadow on the far one, the sky on its top, the ground's bounce below
            // the grains' single-scattering albedo is 2-3 x the ground's own (Hapke: a loose surface darkens itself by
            // shadowing and multiple scattering among its grains) - a dust cloud is lighter than the soil it rose from
            alb = saturate(alb * 2.2);
            float thick = saturate(i.col.a * 1.5);
            float lamb = saturate(dot(N, Lsun) * 0.6 + 0.4);              // wrapped: the light leaks round its edge
            float self = lerp(1, (0.6 + 0.4 * lamb) * (0.75 + 0.25 * n1), 0.7 * thick);
            float3 sunT = i.sun * lerp(ph, 1.0, 0.6 * thick) * self;
            float upk = dot(N, upW) * 0.5 + 0.5;
            float3 amb = lerp(i.gnd, i.sky, upk) * lerp(1, 0.85 + 0.15 * n1, thick);
            c = alb * (sunT + amb);
        }
    }
    if (a < 0.002) discard;
    // soft against the scene's depth
    float3 sw; float sd = ExSceneDist(i.pos.xy, sw);
    a *= saturate((sd - length(i.wp)) / max(0.5 * i.misc.z, 0.05));
    if (a < 0.002) discard;
    return float4(c * a, a);
}
