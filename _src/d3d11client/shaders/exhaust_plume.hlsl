// D3D11Client: a volumetric exhaust plume (src\Exhaust.cpp), ray-marched through a box round the jet, against the
// scene's depth. The gas field comes from the exit conditions (AtmoFX_SetJet: exit radius, thrust, exhaust velocity,
// exit temperature) and the ambient pressure, blended by the expansion ratio n = p_exit / p_ambient:
//  - thick air (n up to ~30): the jet matches the ambient pressure (radius rj = re n^(1/2 gamma)), a potential core of
//    length Lc ~ Dj (4.2 + 1.1 Me^2), shock cells of length 1.306 Dj sqrt(Me^2 - 1) whose crossings (Mach diamonds) are
//    hotter by the normal-shock temperature ratio (weaker with each cell and as n -> 1), then a turbulent mixing jet
//    widening ~5 deg with the exhaust's share decaying ~ Lc / x;
//  - vacuum (n > ~1000): a source flow (Simons): rho = rho_e (re / R)^2 cos^(2/(gamma-1))(pi theta / 2 theta_max) out to
//    the Prandtl-Meyer limit angle, the gas cooling adiabatically T = Te (rho / rho_e)^(gamma-1) - a wide plume that
//    stops glowing a few exit radii out.
// Emission: absorption coefficient kappa = k_abs x the exhaust's partial density, radiance kappa B(T) ds (emission and
// absorption in local thermal equilibrium), B relative to the Sun's times kE: the scene's units. Output through
// dual-source blending: + the emitted light, x the plume's transmittance.
// A hydrogen exhaust (H2 + H2O) has no soot and no visible Planck light: it glows by the blue continuum (220-600 nm,
// flat maximum 440 nm), emission ~ [OH]^2 (Fiala 2015; Vanpee & Mainiero 1979). [OH] over the exhaust's share c in its
// mix with the ambient gas comes from the chemical equilibrium of the mix (src\H2Chem.cpp): with oxygen in the air the
// free H2 burns where c is near stoichiometric - the flame sheet stands where the mixing field puts it, wrinkled by the
// field's eddies; without oxygen (Titan, Mars) only the hot core holds a little OH. In vacuum the frozen flow carries
// the exit's OH outwards, its density falling as the source flow's.
#include "exhaust_common.hlsli"

cbuffer Plume : register(b5)
{
    float4 pE;      // the exit from the camera; w exit radius re
    float4 pX;      // the plume frame's axes (global): x, y, z down the jet; pX.w kE
    float4 pY;      // w: time
    float4 pZ;      // w: steps
    float4 pBox;    // half-width, z0, z1, k_abs [m2/kg]
    float4 pGas;    // Te, T ambient, gamma, Me
    float4 pJet;    // rj, Lc, cell length, shock amplitude (T2/T1 - 1, weakened)
    float4 pVac;    // vacuum weight 0..1, theta_max, rho_e, level (flicker)
    float4 pAir;    // rho of the jet at the ambient pressure, spreading slope, seed, the eddies' convection speed [m/s]
    float4 pH2;     // hydrogen: x on, y [OH] at the exit [1e20 m^-3], z the frame's exposure [s], w the glow's reach [m]
    float4 pKc;     // the continuum's radiance per [OH]^2 [(1e20 m^-3)^2] per metre, per channel (x kE); w: the table's fs
    float4 pOH[8];  // [OH] [1e20 m^-3] over u = c / (c + fs), 32 entries at (i + 0.5) / 32
    float4x4 pVP;
};

struct VSOut { float4 pos : SV_Position; float3 wp : TEXCOORD0; nointerpolation float3 n : NORMAL; };

VSOut VS(uint vid : SV_VertexID)
{
    VSOut o;
    float3 n; float3 c = ExCube(vid, n);
    float3 l = float3(c.x * pBox.x, c.y * pBox.x, lerp(pBox.y, pBox.z, c.z * 0.5 + 0.5));
    o.wp = pE.xyz + pX.xyz * l.x + pY.xyz * l.y + pZ.xyz * l.z;
    o.pos = mul(float4(o.wp, 1), pVP);
    o.n = pX.xyz * n.x + pY.xyz * n.y + pZ.xyz * n.z;
    return o;
}

// [OH] of the mix at the exhaust's share c (the table, linear in u)
float OHMix(float c)
{
    float u = c / (c + pKc.w);
    float x = clamp(u * 32 - 0.5, 0, 31);
    uint i = (uint)x; uint i1 = min(i + 1, 31u);
    float a = pOH[i >> 2][i & 3], b = pOH[i1 >> 2][i1 & 3];
    return lerp(a, b, x - i);
}

static float gShT;   // this sample's moment within the frame's exposure [s] (the pass's time + a share of the shutter)

// The mean of [OH]^2 over the mixture fraction's turbulent fluctuations at this point: a Gaussian PDF of c round its
// local value (6-point Gauss-Hermite, clipped to 0..1). The light of a turbulent flame is the mean of OH^2(c), not
// OH^2 of the mean c: the sheet becomes a soft brush whose width is set by the fluctuations, not a sharp shell.
// rms c'/c on the axis ~0.22, rising towards the edge where the flow is intermittent (Panchapakesan & Lumley 1993)
float OH2Pdf(float c, float I)
{
    static const float x[3] = { 0.6167065, 1.8891759, 3.3242574 };     // probabilists' nodes (sqrt 2 x the physicists')
    static const float w[3] = { 0.4088285, 0.0886157, 0.0025557 };     // weights (both sides sum to 1)
    float s = c * I, m = 0;
    [unroll] for (int k = 0; k < 3; k++) {
        float a = OHMix(saturate(c + s * x[k])), b = OHMix(saturate(c - s * x[k]));
        m += w[k] * (a * a + b * b);
    }
    return m;
}

// the gas at (r from the axis, z down the jet): absorption coefficient [1/m], temperature [K], mean [OH]^2
// [(1e20 m^-3)^2]
void Field(float3 q, out float kappa, out float T, out float oh2)
{
    oh2 = 0;
    float r = length(q.xy), z = q.z;
    float Te = pGas.x, Ta = pGas.y, gam = pGas.z;
    float kabs = pBox.w;
    kappa = 0; T = Ta;
    // ---- thick air
    float ka = 0, Tair = Ta;
    if (pVac.x < 0.999 && z > 0) {
        float rj = pJet.x, Lc = pJet.y, cell = pJet.z;
        float rc = rj * saturate(1 - z / Lc);                    // the potential core
        float b = rj + pAir.y * z;                               // the mixing layer's outer edge
        float cax = z < Lc ? 1 : Lc / z;
        float f = r <= rc ? 1 : exp(-pow((r - rc) / max(b - rc, 1e-4) * 1.4, 2));
        // turbulence beyond the core's start: the eddies carried down the jet
        float turb = ExFbm(float3(q.xy / max(b, 1e-3) * 1.7, (z - pY.w * pAir.w) / max(b, 1e-3) * 0.9) + pAir.z * 17);
        float c = cax * f * lerp(1, 0.55 + 0.9 * turb, saturate(z / Lc * 1.5));
        // shock cells: the diamonds where the waves cross the axis, hotter by the shock's temperature ratio
        float ph = frac(z / cell);
        float w = rj * (1 - abs(2 * ph - 1));
        float dia = saturate(1 - r / max(w, 1e-4)) * smoothstep(0, 0.15, ph);
        float amp = pJet.w * exp(-z / (0.8 * Lc));
        float Tj = Te * (1 + amp * dia);
        Tair = Ta + (Tj - Ta) * saturate(c);
        ka = kabs * pAir.x * saturate(c);
        // hydrogen: the mean field's large eddies displace the gas (resolved in time: each passes at f ~ 0.6 u_c / b;
        // what the frame's exposure smears - f dt >> 1 near the nozzle - goes into the fluctuations' PDF instead, so
        // the picture is the shutter's average, as a camera's), the small ones only through the PDF
        if (pH2.x > 0.5) {
            float zc = max(z, 0.5 * rj);
            float caxH = min(1, Lc / zc);
            float uc = 2 * pAir.w * caxH;                          // the axis' speed ~ ve c_axis
            float fe = 0.6 * uc / max(b, 1e-3);                     // the energetic eddies' passage frequency [Hz]
            // the share the exposure still resolves: near the exit the eddies pass in microseconds (smeared, steady); down
            // the jet and at its edge the flow is slower and intermittent - the tip and the fringe flicker and tear, as a
            // camera sees a real jet. The phase advances no faster than the frames can show (no strobing)
            float far = saturate(max(zc / max(pH2.w, 1e-3), length(q.xy) / max(b, 1e-4) - 0.4));
            float vis = max(1 / (1 + fe * pH2.z), lerp(0.3, 1.0, far));
            float phE = gShT * min(fe, 0.45 / max(pH2.z, 1e-3));
            float3 nq = float3(q.xy / max(b, 1e-3) * 1.2, log(zc / max(rj, 1e-3)) / pAir.y * 0.45 - phE) + pAir.z * 13;
            float3 wv = float3(ExNoise(nq), ExNoise(nq + float3(5.2, 1.3, 7.7)), ExNoise(nq + float3(9.1, 3.7, 2.9)));
            wv += 0.5 * float3(ExNoise(nq * 2.1 + 3.3), ExNoise(nq * 2.1 + 8.1), ExNoise(nq * 2.1 + 1.7));
            wv = (wv / 1.5 - 0.5) * vis;
            float3 qw = q + float3(wv.xy * 0.8 * b, wv.z * 1.0 * b);
            float rw = length(qw.xy), zw = max(qw.z, 0);
            float rcw = rj * saturate(1 - zw / Lc), bw = rj + pAir.y * zw;
            float cw = (zw < Lc ? 1 : Lc / zw) * (rw <= rcw ? 1 : exp(-pow((rw - rcw) / max(bw - rcw, 1e-4) * 1.4, 2)));
            float eta = rw / max(bw, 1e-4);
            float I = min(0.22 * (1 + 3 * eta * eta), 1.5) * sqrt(1 - 0.6 * vis * vis) * saturate(zw / max(rj, 1e-4));
            oh2 = OH2Pdf(saturate(cw), I) * (1 - pVac.x);
        }
    }
    // ---- vacuum: a source flow from just inside the exit
    float kv = 0, Tv = Ta;
    if (pVac.x > 0.001) {
        float re = pE.w;
        float3 v = q - float3(0, 0, -0.5 * re);
        float R = length(v);
        float th = acos(clamp(v.z / max(R, 1e-6), -1, 1));
        float tm = pVac.y;
        float ang = th < tm ? pow(max(cos(1.5707963 * th / tm), 0), 2 / (gam - 1)) : 0;
        float rho = pVac.z * min(1, pow(re / max(R, 1e-6), 2)) * ang;
        kv = kabs * rho;
        Tv = max(Te * pow(max(rho / pVac.z, 1e-9), gam - 1), Ta);
        if (pH2.x > 0.5) { float o = pH2.y * (rho / pVac.z); oh2 += o * o * pVac.x; }   // frozen: the exit's OH share carried out
    }
    kappa = lerp(ka, kv, pVac.x) * pVac.w;
    T = lerp(Tair, Tv, pVac.x);
}

struct PSOut { float4 add : SV_Target0; float4 mul : SV_Target1; };

PSOut PS(VSOut i)
{
    if (dot(i.n, i.wp) < 0) discard;       // keep the faces turned away (one pass per pixel, also from inside)
    float3 dir = normalize(i.wp);
    float3 sp; float sd = ExSceneDist(i.pos.xy, sp);
    // the ray in the plume's frame
    float3 o = -pE.xyz;
    float3 ro = float3(dot(o, pX.xyz), dot(o, pY.xyz), dot(o, pZ.xyz));
    float3 rd = float3(dot(dir, pX.xyz), dot(dir, pY.xyz), dot(dir, pZ.xyz));
    float3 bmin = float3(-pBox.x, -pBox.x, pBox.y), bmax = float3(pBox.x, pBox.x, pBox.z);
    float3 inv = 1 / (abs(rd) > 1e-8 ? rd : 1e-8);
    float3 t0 = (bmin - ro) * inv, t1 = (bmax - ro) * inv;
    float ta = max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0));
    float tb = min(min(max(t0.x, t1.x), max(t0.y, t1.y)), min(max(t0.z, t1.z), sd));
    if (tb <= ta) discard;
    // all the steps where the gas is: the stretch of the ray within the plume's reach of the axis (round its closest
    // approach), not the whole box - a thin jet seen across would fall between the steps
    {
        float3 ax = float3(0, 0, 1);
        float sa2 = max(1 - rd.z * rd.z, 1e-6);
        float tc = (dot(-ro, rd) + ro.z * rd.z) / sa2;           // closest approach of the ray to the axis (z line)
        float3 pc = ro + rd * tc;
        float reach = pVac.x > 0.5 ? pBox.x : 2.5 * (pJet.x + pAir.y * clamp(pc.z, 0, pBox.z));
        float d2 = dot(pc.xy, pc.xy);
        if (d2 > reach * reach) discard;
        float w = sqrt(reach * reach - d2) / sqrt(sa2);
        ta = max(ta, tc - w); tb = min(tb, tc + w);
        if (tb <= ta) discard;
    }
    int n = (int)abs(pZ.w);
    float ds = (tb - ta) / n;
    // interleaved gradient noise (Jimenez 2014): an even spread of the steps' offsets, moving per frame only under TAA
    float jit = frac(52.9829189 * frac(dot(i.pos.xy, float2(0.06711056, 0.00583715))) + (pZ.w < 0 ? frac(pY.w * 60 * 0.618034) : 0));
    float3 L = 0; float tr = 1;
    float kE = pX.w;
    // the moment within the frame's exposure (another low-discrepancy share per pixel; TAA averages the shutter)
    gShT = pY.w + frac(jit * 1.6180339 + 0.37) * pH2.z;
    [loop] for (int k = 0; k < n; k++) {
        float t = ta + (k + jit) * ds;
        float kap, T, oh2;
        Field(ro + rd * t, kap, T, oh2);
        // the blue continuum: optically thin chemiluminescence, ~ [OH]^2 (the level's flicker on the gas's density)
        if (oh2 > 0) L += tr * kE * pKc.rgb * (oh2 * ds) * pVac.w * pVac.w;
        if (kap <= 0) continue;
        float a = exp(-kap * ds);
        L += tr * (1 - a) * kE * ExBlackbodyRel(T);     // emission of a slab in LTE: B (1 - e^-tau)
        tr *= a;
        if (tr < 1e-3) break;
    }
    PSOut r;
    r.add = float4(L * ExAirTrans(ta), 0);
    r.mul = float4(tr, tr, tr, 1);
    return r;
}
