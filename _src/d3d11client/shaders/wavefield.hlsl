// D3D11Client: the wave field on a liquid (Wake.cpp). A square window on the level around the focus vessel, N x N cells
// of dx; per cell the height now and one step ago. The surface follows the deep-liquid dispersion w^2 = g k exactly (in
// the kernel's reach) by Tessendorf's iWave:
//   h(t+dt) = [ h (2 - a) - h(t-dt) - g dt^2 G*h ] / (1 + a),   a = damping dt,
// with G the convolution kernel of sqrt(-laplacian) (13 x 13 cells, Wake.cpp). Long waves run faster than short ones,
// so a moving body leaves a true Kelvin wake (the 19.5 deg wedge, its transverse and diverging crests) and a body at
// rest that bobs sends out rings. The bodies press hollows into it (Stamp, height and the step before together: no
// kick), steps and splashes kick it (Stamp, the step before only); Shift moves the window by whole cells; Slope gives
// the shader the surface's gradient.
cbuffer Field : register(b0)
{
    float4 gP;        // x: N, y: dx [m], z: dt [s], w: damping a = alpha dt
    float4 gK;        // x: g dt^2 / dx (the kernel works in cells), y: -, z: sponge width [cells], w: number of stamps
    int4 gShift;      // xy: the shift in cells (Shift)
    float4 gStamp[64];   // x, y: centre [cells], z: amplitude (h: metres; kick: m/s), w: sigma [cells]; w < 0: a kick
    float4 gKer[43];     // the kernel, 13 x 13 row by row (169 values)
};
Texture2D<float2> tIn : register(t0);      // x: h now, y: h one step ago
RWTexture2D<float2> uOut : register(u0);
RWTexture2D<float4> uSlope : register(u1);

float2 At(int2 p) { int n = (int)gP.x; return (any(p < 0) || any(p >= n)) ? float2(0, 0) : tIn[p]; }
float Ker(int i) { return gKer[i >> 2][i & 3]; }

[numthreads(16, 16, 1)]
void CSStamp(uint3 id : SV_DispatchThreadID)
{
    int2 p = (int2)id.xy;
    if (any(p >= (int)gP.x)) return;
    float2 hv = tIn[p];
    int ns = (int)gK.w;
    for (int k = 0; k < ns; k++) {
        float4 s = gStamp[k];
        float sg = abs(s.w);
        float2 d = (float2(p) + 0.5 - s.xy) / sg;
        float r2 = dot(d, d);
        if (r2 > 16) continue;
        float g = exp(-0.5 * r2);
        if (s.w > 0) hv += s.z * g;                 // the surface moved (now and before alike: no velocity)
        else hv.y -= s.z * g * gP.z;                // a kick: velocity s.z (h - h_prev = v dt)
    }
    uOut[p] = hv;
}

[numthreads(16, 16, 1)]
void CSSim(uint3 id : SV_DispatchThreadID)
{
    int2 p = (int2)id.xy;
    int n = (int)gP.x;
    if (any(p >= n)) return;
    float2 hv = At(p);
    float conv = 0;
    [loop] for (int j = -6; j <= 6; j++) {
        [unroll] for (int i = -6; i <= 6; i++) conv += Ker((j + 6) * 13 + (i + 6)) * At(p + int2(i, j)).x;
    }
    float a = gP.w;
    float hn = (hv.x * (2 - a) - hv.y - gK.x * conv) / (1 + a);
    float hp = hv.x;
    // the sponge: waves die out before the window's edge (no reflection back in)
    float e = min(min(p.x, p.y), min(n - 1 - p.x, n - 1 - p.y));
    if (e < gK.z) { float s = pow(0.97, (gK.z - e) / gK.z * 4); hn *= s; hp *= s; }
    uOut[p] = float2(hn, hp);
}

[numthreads(16, 16, 1)]
void CSShift(uint3 id : SV_DispatchThreadID)
{
    int2 p = (int2)id.xy;
    if (any(p >= (int)gP.x)) return;
    uOut[p] = At(p + gShift.xy);     // the window moved by gShift cells: what comes in is calm
}

[numthreads(16, 16, 1)]
void CSSlope(uint3 id : SV_DispatchThreadID)
{
    int2 p = (int2)id.xy;
    if (any(p >= (int)gP.x)) return;
    float dx = gP.y;
    float gx = (At(p + int2(1, 0)).x - At(p - int2(1, 0)).x) / (2 * dx);
    float gy = (At(p + int2(0, 1)).x - At(p - int2(0, 1)).x) / (2 * dx);
    uSlope[p] = float4(gx, gy, At(p).x, 0);
}
