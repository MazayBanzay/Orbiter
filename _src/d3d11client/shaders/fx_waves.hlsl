// D3D11Client FX: the wave field on a liquid (Titan's seas), from the Titan demo (shaders\waves.hlsl there).
// Linear deep-water gravity waves evolved EXACTLY in Fourier space (omega^2 = g k), h + i*phi packed in one complex field
// (h: surface height, phi: velocity potential at the surface): h_t = k phi, phi_t = -g h - p/rho -> per mode a rotation
// by omega dt (no CFL limit, no numerical dispersion). Sources in the spatial domain between steps: the jets' stagnation
// pressure, the volume pushed aside by moving parts in the liquid (AtmoFX_SetWake), the drops falling back (spray), the
// window's edge (sponge). The field is periodic; the window follows the focus by whole cells (texture addressing wraps;
// cells entering the window start at rest). Coordinates: a tangent frame fixed to the planet at the window's anchor.
#define N 1024
#define LOGN 10
static const float PI = 3.14159265;

cbuffer WaveCB : register(b2)
{
    int4 wOrigin;      // new origin cell i0, j0; old origin i0, j0
    float4 wP;         // dt, g, rho, viscosity
    int4 wCounts;      // bodies, jets, inverse, columns
    float4 wGrid;      // cell size, sponge width [cells], 0, 0
    float4 wJets[8];   // x, z (tangent frame), radius, stagnation pressure [Pa]
    float4 wBodies[32 * 2];   // per body: now (x, z, radius, draft), before (x, z, radius, draft)
};

RWTexture2D<float2> uState : register(u0);
Texture2D<float2> tIn : register(t0);
RWTexture2D<float2> uOut : register(u1);
RWTexture2D<uint> uImpact : register(u2);
RWTexture2D<float4> uNormal : register(u3);

int WorldCell(int u, int i0) { return i0 + (((u - i0) % N) + N) % N; }

// a part in the liquid: the volume it occupies at the surface, a soft-edged disc
float DiscOcc(float2 p, float4 b)
{
    if (b.z <= 0) return 0;
    float s = max(0.15 * b.z, 0.05);
    return b.w * (1 - smoothstep(b.z - s, b.z + s, length(p - b.xy)));
}

[numthreads(8, 8, 1)]
void CSSource(uint3 id : SV_DispatchThreadID)
{
    int I = WorldCell(id.x, wOrigin.x), J = WorldCell(id.y, wOrigin.y);
    float2 z = uState[id.xy];
    // cells that just entered the window: at rest
    if (I < wOrigin.z || I >= wOrigin.z + N || J < wOrigin.w || J >= wOrigin.w + N) z = 0;
    float dx = wGrid.x, dt = wP.x, rho = wP.z;
    float2 p = (float2(I, J) + 0.5) * dx;

    // jets: the stagnation pressure holds the liquid down (the cavity, h0 = q / (rho g) in steady state)
    float press = 0;
    for (int j = 0; j < wCounts.y; j++) {
        float2 d = p - wJets[j].xy;
        press += wJets[j].w * exp(-dot(d, d) / (wJets[j].z * wJets[j].z));
    }
    z.y -= dt * press / rho;

    // parts in the liquid: the volume they push aside as they move (h += change of the occupied volume); a part at rest
    // is no source. Overlapping parts: the deepest.
    float o1 = 0, o0 = 0;
    for (int b = 0; b < wCounts.x; b++) {
        o1 = max(o1, DiscOcc(p, wBodies[b * 2 + 0]));
        o0 = max(o0, DiscOcc(p, wBodies[b * 2 + 1]));
    }
    z.x += o1 - o0;

    // drops falling back: their momentum (pressure impulse / rho), accumulated by the spray in 1e-7 m^2/s
    uint imp = uImpact[id.xy];
    if (imp) { z.y -= imp * 1e-7; uImpact[id.xy] = 0; }

    // the window's sponge
    int e = min(min(I - wOrigin.x, wOrigin.x + N - 1 - I), min(J - wOrigin.y, wOrigin.y + N - 1 - J));
    float s = saturate(1 - e / wGrid.y);
    z *= exp(-4.0 * s * s * dt);
    uState[id.xy] = z;
}

// ---- FFT: one row (or column) per group, radix-2 Stockham in groupshared memory
groupshared float2 sh[2][N];

float2 CMul(float2 a, float2 b) { return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }

[numthreads(N / 2, 1, 1)]
void CSFFT(uint3 g : SV_GroupID, uint t : SV_GroupIndex)
{
    bool cols = wCounts.w != 0;
    float sgn = wCounts.z != 0 ? 1.0 : -1.0;
    uint2 c0 = cols ? uint2(g.x, t) : uint2(t, g.x);
    uint2 c1 = cols ? uint2(g.x, t + N / 2) : uint2(t + N / 2, g.x);
    sh[0][t] = tIn[c0];
    sh[0][t + N / 2] = tIn[c1];
    GroupMemoryBarrierWithGroupSync();
    uint src = 0;
    [unroll] for (uint Ns = 1; Ns < N; Ns <<= 1) {
        uint k = t & (Ns - 1);
        float2 a = sh[src][t], b = sh[src][t + N / 2];
        float ang = sgn * PI * k / Ns;
        float sn, cs; sincos(ang, sn, cs);
        b = CMul(b, float2(cs, sn));
        uint o = (t - k) * 2 + k;
        sh[1 - src][o] = a + b;
        sh[1 - src][o + Ns] = a - b;
        src = 1 - src;
        GroupMemoryBarrierWithGroupSync();
    }
    float sc = wCounts.z != 0 ? 1.0 / N : 1.0;
    uOut[c0] = sh[src][t] * sc;
    uOut[c1] = sh[src][t + N / 2] * sc;
}

// ---- exact evolution of each mode
[numthreads(8, 8, 1)]
void CSEvolve(uint3 id : SV_DispatchThreadID)
{
    uint2 m = uint2((N - id.x) % N, (N - id.y) % N);
    float2 Z = tIn[id.xy], Zm = tIn[m];
    float2 Zc = float2(Zm.x, -Zm.y);
    float2 H = 0.5 * (Z + Zc);                          // spectrum of h
    float2 P = 0.5 * (Z - Zc); P = float2(P.y, -P.x);   // spectrum of phi: (Z - conj Z(-k)) / 2i
    float dx = wGrid.x;
    float kx = (id.x < N / 2 ? (float)id.x : (float)id.x - N) * 2 * PI / (N * dx);
    float kz = (id.y < N / 2 ? (float)id.y : (float)id.y - N) * 2 * PI / (N * dx);
    float k = sqrt(kx * kx + kz * kz);
    float dt = wP.x, g = wP.y;
    float2 H2, P2;
    if (k > 0) {
        float w = sqrt(g * k);
        float s, c; sincos(w * dt, s, c);
        H2 = H * c + P * (k / w) * s;
        P2 = P * c - H * (g / w) * s;
        float vd = exp(-2 * wP.w * k * k * dt);         // viscous decay
        // the grid's own limit: waves shorter than ~4 cells are not resolved - damped before they alias
        float kc = 0.5 * PI / dx;
        vd *= exp(-dt * 6.0 * pow(k / kc, 8));
        H2 *= vd; P2 *= vd;
    } else { H2 = H; P2 = P - H * g * dt; }
    uOut[id.xy] = H2 + float2(-P2.y, P2.x);             // Z' = H' + i P'
}

// ---- slopes for the surface: gx, gz, h, laplacian
[numthreads(8, 8, 1)]
void CSNormals(uint3 id : SV_DispatchThreadID)
{
    uint2 xp = uint2((id.x + 1) % N, id.y), xm = uint2((id.x + N - 1) % N, id.y);
    uint2 zp = uint2(id.x, (id.y + 1) % N), zm = uint2(id.x, (id.y + N - 1) % N);
    float dx = wGrid.x;
    float h = tIn[id.xy].x;
    float gx = (tIn[xp].x - tIn[xm].x) / (2 * dx);
    float gz = (tIn[zp].x - tIn[zm].x) / (2 * dx);
    float lap = (tIn[xp].x + tIn[xm].x + tIn[zp].x + tIn[zm].x - 4 * h) / (dx * dx);
    uNormal[id.xy] = float4(gx, gz, h, lap);
}
