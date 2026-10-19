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

// the gas at (r from the axis, z down the jet): absorption coefficient [1/m], temperature [K]
void Field(float3 q, out float kappa, out float T)
{
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
    [loop] for (int k = 0; k < n; k++) {
        float t = ta + (k + jit) * ds;
        float kap, T;
        Field(ro + rd * t, kap, T);
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
