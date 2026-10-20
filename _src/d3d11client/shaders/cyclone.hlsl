// D3D11Client: a cyclone's weather map (Cyclone.cpp) - one square texture over the storm, the single source both cloud
// renderers read (the 2D layer from afar, the volumetric clouds near): r coverage, g convective fraction (0 stratiform ..
// 1 cumulonimbus), b cloud top [km / 16], a precipitation [mm/h / 30]. x east, y north (the top row north).
//
// The model, an extratropical cyclone of the northern hemisphere (Bjerknes' Norwegian model, Shapiro-Keyser's
// occlusion; the conveyor belts of Carlson / Browning), mirrored for the southern one:
//  - the cold front: from the low towards the south-south-west, bending west with distance; a narrow line (~60 km) of
//    cumulonimbus, heavy showers;
//  - the warm front: from the low towards the east-south-east; on its cold (poleward) side the warm conveyor belt rises
//    over it - the shield: nimbostratus with steady rain near the front (~300 km), altostratus, then cirrostratus and
//    cirrus up to ~1000 km ahead, the tops rising to ~10 km;
//  - the comma head: the shield wrapping round the north and west of the low (the occlusion, the bent-back front),
//    spiralling in; the dry slot: clear air wrapping in behind the cold front towards the centre;
//  - the warm sector between the fronts: stratocumulus, drizzle; the cold air behind: open cells of cumulus.
// The features turn about the low (a Rankine-like vortex, ~25 m/s at ~300 km) and the small scales are carried by that
// flow (two noise phases blended, each reset every few hours - the flow-map trick: no endless winding).
cbuffer Cyc : register(b1)
{
    float4 cA;      // x: half-size of the square [m], y: the cyclone's radius [m], z: stage (0 wave .. 1 occluded), w: hemisphere (+1 N, -1 S)
    float4 cB;      // x: time [s] (flow phase), y: max wind [m/s], z: radius of max wind [m], w: kind (0 extratropical, 1 tropical)
    float4 cC;      // x: the noise seed offset, yzw: -
};

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint vid : SV_VertexID)
{
    VSOut o;
    float2 c = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(c * float2(2, -2) + float2(-1, 1), 0, 1);
    o.uv = c;
    return o;
}

static const float PI = 3.14159265;

// gradient noise (hash-based: no texture), fBm with a rotation per octave (no grid-aligned "checkerboard")
float2 G2(float2 i) { float a = 6.2831853 * frac(sin(dot(i, float2(127.1, 311.7))) * 43758.5453); return float2(cos(a), sin(a)); }
float H2(float2 p) { p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }
float GNoise(float2 p)
{
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * f * (f * (f * 6 - 15) + 10);
    float a = dot(G2(i), f), b = dot(G2(i + float2(1, 0)), f - float2(1, 0));
    float c = dot(G2(i + float2(0, 1)), f - float2(0, 1)), d = dot(G2(i + 1), f - 1);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y) * 0.75 + 0.5;
}
float Fbm(float2 p, int oct)
{
    float s = 0, a = 0.5, n = 0;
    const float2x2 m = float2x2(0.80, -0.60, 0.60, 0.80);
    for (int k = 0; k < oct; k++) { s += a * GNoise(p); n += a; a *= 0.5; p = mul(m, p) * 2.03 + 17.1; }
    return s / n;
}
// cells (Worley) on a warped plane: 1 at a cell's centre, 0 at its rim - open-cell cumulus, uneven in size and spacing
float Cells(float2 p)
{
    p += (float2(Fbm(p * 0.35 + 3.1, 3), Fbm(p * 0.35 + 8.7, 3)) - 0.5) * 1.6;
    float2 i = floor(p), f = frac(p); float best = 9;
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
        float2 g = float2(x, y), o = float2(H2(i + g), H2(i + g + 31.7));
        float sz = 0.55 + 0.6 * H2(i + g + 7.3);                      // cells of different strength
        best = min(best, length(g + o - f) / sz);
    }
    return saturate(1 - best * 1.2);
}

float AngDiff(float a, float b) { float d = a - b; return d - 2 * PI * round(d / (2 * PI)); }

// the vortex's angular speed at r [rad/s] (Rankine-like, outside decaying as r^-0.6)
float Omega(float r)
{
    float v = r < cB.z ? cB.y * r / cB.z : cB.y * pow(cB.z / r, 0.6);
    return v / max(r, 1000);
}

// the small-scale field at X [m], carried by the vortex over a time phase tl [s]
float2 Carried(float2 X, float tl)
{
    float r = length(X), a = -Omega(r) * tl;         // back along the flow (counter-clockwise in the north)
    float c = cos(a), s = sin(a);
    return float2(c * X.x - s * X.y, s * X.x + c * X.y);
}

float4 Extratropical(float2 X, float2 Xn)
{
    float Rc = cA.y, st = cA.z;
    float r = length(X), rn = r / Rc;
    float th = atan2(X.y, X.x);                      // from east, counter-clockwise
    // noise in the flow (km units)
    float2 wq = Xn / 90000.0 + cC.x;
    wq += (float2(Fbm(wq * 0.5 + 1.7, 4), Fbm(wq * 0.5 + 9.2, 4)) - 0.5) * 2.0;     // domain warp: billowed, streaked edges
    float n1 = Fbm(wq, 5), n2 = Fbm(wq * 3.6 + 7.3, 5), cell = Cells(Xn / 38000.0 + cC.x) * (0.6 + 0.8 * n1);
    // --- fronts: their angle at a distance (curving with it), the distance [m] to each across the arc
    float thCF = radians(-75) - radians(55) * saturate(rn);                 // cold front: SSW, bending west
    float thWF = radians(-5) - radians(25) * saturate(rn) - radians(25) * st;   // warm front: ESE (swung south as it occludes)
    float dCF = r * AngDiff(th, thCF);               // > 0: east of the cold front (the warm sector side)
    float dWF = r * AngDiff(th, thWF);               // > 0: north of the warm front (the shield side)
    float rFr = saturate(1.3 - rn * 0.75);           // the fronts weaken far from the low
    float cov = 0, conv = 0, top = 0, pr = 0;
    // the shield over the warm front (poleward, from the front up to ~1000 km ahead)
    if (dWF > -40000 && AngDiff(th, thWF) < radians(150)) {
        float dd = max(dWF, 0);
        float shield = saturate(1 - dd / (900000.0 * (0.6 + 0.6 * rFr))) * smoothstep(-40000, 20000, dWF);
        float ns = saturate(1 - dd / 300000.0);                             // nimbostratus near the front: rain
        float c = shield * lerp(0.75, 1.0, n1) * saturate(1.6 - rn * 0.55);
        cov = max(cov, saturate(c * 1.25 - 0.1 + (n2 - 0.5) * 0.3 * (1 - ns)));
        top = max(top, lerp(4.0, 9.5, saturate(dd / 400000.0)) * step(0.01, c));     // tops rising ahead (cirrus far)
        pr = max(pr, ns * shield * 6 * (0.6 + 0.8 * n2));
        conv = lerp(conv, 0.1, step(0.01, c));
    }
    // the comma head: the shield wrapping round the north and west of the low, spiralling in (stronger as it occludes)
    {
        float spiral = th - log(max(rn, 0.02) / 0.6) / tan(radians(18));   // a logarithmic spiral, arms 18 deg off the circle
        float arm = 0.5 + 0.5 * cos(spiral);
        float head = saturate(1 - rn / (0.65 + 0.2 * st)) * saturate(1.3 - abs(AngDiff(th, radians(140))) / radians(110));   // north and west of the low
        float wrap = saturate((AngDiff(th, thCF) - radians(10)) / radians(40)) * saturate(1 - rn / 0.75);   // from the warm side round
        float c = saturate(max(head, wrap) * (0.55 + 0.6 * arm) * lerp(0.7, 1.1, n1));
        cov = max(cov, c);
        top = max(top, 8.0 * step(0.05, c));
        pr = max(pr, c * 3 * saturate(1 - rn / 0.5));
    }
    // the dry slot: clear air wrapping in behind the cold front towards the centre
    {
        float slot = exp(-pow(AngDiff(th, thCF - radians(35) + radians(40) * saturate(1 - rn)) / radians(14), 2)) * saturate(rn * 4) * saturate(1.2 - rn);
        cov *= 1 - 0.9 * slot;
        pr *= 1 - slot;
    }
    // the warm sector: stratocumulus between the fronts (south-east of the low)
    if (dCF > 0 && dWF < 0) {
        float sc = saturate((n2 - 0.35) * 1.8) * 0.65 * saturate(1.4 - rn * 0.6);
        if (sc > cov) { cov = sc; top = 2.2; conv = 0.25; pr = max(pr, 0.3 * sc); }
    }
    // the cold front: a narrow line of cumulonimbus, showers; a little cloud ahead of it
    {
        float w = 55000.0 * (0.8 + 0.5 * n1);
        float lineCF = exp(-pow((dCF - 15000) / w, 2)) * rFr * saturate(rn * 6);
        float cb = saturate(lineCF * (0.75 + 0.5 * cell));
        if (cb > 0.05) {
            cov = max(cov, cb);
            conv = max(conv, cb);
            top = max(top, lerp(6.0, 12.5, cb));
            pr = max(pr, cb * cb * 25);
        }
    }
    // the cold air behind: open cells of cumulus (west of the cold front)
    if (dCF < -60000) {
        float cu = saturate((cell - 0.45) * 2.5) * saturate(0.9 - rn * 0.35) * smoothstep(-60000, -200000, dCF);
        if (cu > cov) { cov = cu; conv = 0.8; top = lerp(2.5, 6.0, cell); pr = max(pr, cu * 2); }
    }
    cov *= saturate((1.25 - rn) / 0.4);      // the storm's edge, well inside the square (the 2D layer blends into Orbiter's own there)
    return float4(saturate(cov), saturate(conv), top / 16.0, saturate(pr / 30.0));
}

float4 Tropical(float2 X, float2 Xn)
{
    float Rc = cA.y, r = length(X), rn = r / Rc;
    float re = cB.z * 0.6;                                   // the eye
    float th = atan2(X.y, X.x);
    float n1 = Fbm(Xn / 40000.0 + cC.x, 5), n2 = Fbm(Xn / 12000.0 + cC.x + 3.1, 4);
    float eye = smoothstep(re * 0.7, re * 1.1, r);
    float wall = exp(-pow((r - re * 1.6) / (re * 0.6), 2));
    float spiral = th - log(max(r, re) / re) / tan(radians(15));
    float bands = saturate((0.5 + 0.5 * cos(2 * spiral + n1 * 3)) * 1.6 - 0.4) * saturate(1.2 - rn);
    float cdo = saturate(1 - rn / 0.45) * eye;               // the central dense overcast (cirrus over it all)
    float cov = saturate(max(max(wall, bands * (0.7 + 0.4 * n2)), cdo * (0.85 + 0.2 * n2))) * eye;
    float conv = saturate(max(wall, bands));
    float top = lerp(9, 16, saturate(max(wall, cdo)));
    float pr = wall * 60 + bands * 15;
    cov *= saturate((1.3 - rn) / 0.3);
    return float4(cov, conv, top / 16.0, saturate(pr / 30.0));
}

float4 PSGen(VSOut i) : SV_Target
{
    float2 q = float2(i.uv.x * 2 - 1, 1 - i.uv.y * 2);       // east, north
    float2 X = q * cA.x;
    X.y *= cA.w;                                              // the south: mirrored (clockwise there)
    // the flow phases: two copies of the small scales, each carried for up to P and reset, blended by a triangle
    const float P = 4 * 3600.0;
    float t0 = fmod(cB.x, P), t1 = fmod(cB.x + 0.5 * P, P);
    float w0 = 1 - abs(t0 / P * 2 - 1);
    float2 Xa = Carried(X, t0) + 13000.0 * floor(cB.x / P), Xb = Carried(X, t1) + 29000.0 * floor((cB.x + 0.5 * P) / P);
    float4 a = cB.w > 0.5 ? Tropical(X, Xa) : Extratropical(X, Xa);
    float4 b = cB.w > 0.5 ? Tropical(X, Xb) : Extratropical(X, Xb);
    return lerp(b, a, w0);
}
