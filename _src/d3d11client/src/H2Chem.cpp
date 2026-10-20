// The chemistry and optics of a hydrogen exhaust (see H2Chem.h).
#include "H2Chem.h"
#include <cmath>
#include <algorithm>

namespace H2Chem {
namespace {

const double RU = 8.314462618, KB = 1.380649e-23, PI = 3.14159265358979;
enum { S_O, S_O2, S_H, S_H2, S_OH, S_H2O, S_N2, NS };
// NASA 7-coefficient polynomials (GRI-Mech 3.0 thermo30.dat): [species][0 = 1000..3500 K, 1 = 200..1000 K][7]
const double NASA[NS][2][7] = {
    { { 2.56942078E+00,-8.59741137E-05, 4.19484589E-08,-1.00177799E-11, 1.22833691E-15, 2.92175791E+04, 4.78433864E+00 },
      { 3.16826710E+00,-3.27931884E-03, 6.64306396E-06,-6.12806624E-09, 2.11265971E-12, 2.91222592E+04, 2.05193346E+00 } },
    { { 3.28253784E+00, 1.48308754E-03,-7.57966669E-07, 2.09470555E-10,-2.16717794E-14,-1.08845772E+03, 5.45323129E+00 },
      { 3.78245636E+00,-2.99673416E-03, 9.84730201E-06,-9.68129509E-09, 3.24372837E-12,-1.06394356E+03, 3.65767573E+00 } },
    { { 2.50000001E+00,-2.30842973E-11, 1.61561948E-14,-4.73515235E-18, 4.98197357E-22, 2.54736599E+04,-4.46682914E-01 },
      { 2.50000000E+00, 7.05332819E-13,-1.99591964E-15, 2.30081632E-18,-9.27732332E-22, 2.54736599E+04,-4.46682853E-01 } },
    { { 3.33727920E+00,-4.94024731E-05, 4.99456778E-07,-1.79566394E-10, 2.00255376E-14,-9.50158922E+02,-3.20502331E+00 },
      { 2.34433112E+00, 7.98052075E-03,-1.94781510E-05, 2.01572094E-08,-7.37611761E-12,-9.17935173E+02, 6.83010238E-01 } },
    { { 3.09288767E+00, 5.48429716E-04, 1.26505228E-07,-8.79461556E-11, 1.17412376E-14, 3.85865700E+03, 4.47669610E+00 },
      { 3.99201543E+00,-2.40131752E-03, 4.61793841E-06,-3.88113333E-09, 1.36411470E-12, 3.61508056E+03,-1.03925458E-01 } },
    { { 3.03399249E+00, 2.17691804E-03,-1.64072518E-07,-9.70419870E-11, 1.68200992E-14,-3.00042971E+04, 4.96677010E+00 },
      { 4.19864056E+00,-2.03643410E-03, 6.52040211E-06,-5.48797062E-09, 1.77197817E-12,-3.02937267E+04,-8.49032208E-01 } },
    { { 2.92664000E+00, 1.48797680E-03,-5.68476000E-07, 1.00970380E-10,-6.75335100E-15,-9.22797700E+02, 5.98052800E+00 },
      { 3.29867700E+00, 1.40824040E-03,-3.96322200E-06, 5.64151500E-09,-2.44485400E-12,-1.02089990E+03, 3.95037200E+00 } },
};
const double MW[NS] = { 15.999, 31.998, 1.008, 2.016, 17.007, 18.015, 28.013 };
const int EL[NS][3] = { { 0, 1, 0 }, { 0, 2, 0 }, { 1, 0, 0 }, { 2, 0, 0 }, { 1, 1, 0 }, { 2, 1, 0 }, { 0, 0, 2 } };   // H, O, N

const double* Coef(int s, double T) { return NASA[s][T >= 1000 ? 0 : 1]; }
double H_RT(int s, double T)
{
    if (T < 200) { const double* a = Coef(s, 200); double cp = a[0] + 200 * (a[1] + 200 * (a[2] + 200 * (a[3] + 200 * a[4])));
                   return (H_RT(s, 200) * 200 - cp * (200 - T)) / T; }   // below the fit: cp held at its 200 K value
    const double* a = Coef(s, T);
    return a[0] + T * (a[1] / 2 + T * (a[2] / 3 + T * (a[3] / 4 + T * a[4] / 5))) + a[5] / T;
}
double S_R(int s, double T)
{
    const double* a = Coef(s, T);
    return a[0] * log(T) + T * (a[1] + T * (a[2] / 2 + T * (a[3] / 3 + T * a[4] / 4))) + a[6];
}

struct Comp { double x[NS] = {}; };

// equilibrium mole fractions of the elements b (moles H, O, N, any common scale) at (T, p): Gibbs minimum by element
// potentials, Newton on (lambda_H, lambda_O, lambda_N, ln N)
Comp Equil(const double b[3], double T, double p)
{
    Comp c;
    T = std::clamp(T, 250.0, 3500.0);
    const double lnP = log(std::max(p, 1.0) / 101325.0);
    double g[NS];
    for (int s = 0; s < NS; s++) g[s] = H_RT(s, T) - S_R(s, T);
    bool use[NS]; int act[3], na = 0;
    for (int k = 0; k < 3; k++) if (b[k] > 0) act[na++] = k;
    for (int s = 0; s < NS; s++) { use[s] = true; for (int k = 0; k < 3; k++) if (EL[s][k] && b[k] <= 0) use[s] = false; }
    // the start: the cold mix's species (water, the excess H2 or O2, N2) - from the element potentials that give them,
    // ln x_s = -g_s - ln P + sum a lambda (a far start does not converge at low pressures)
    double lam[3] = { 0, 0, 0 }, lnN = 0;
    {
        const double H = b[0], O = b[1], N = b[2];
        const double w = std::min(H / 2, O), h2 = (H - 2 * w) / 2, o2 = (O - w) / 2, n2 = N / 2, tot = std::max(w + h2 + o2 + n2, 1e-30);
        auto lx = [&](double n) { return log(std::max(n / tot, 1e-8)); };
        if (h2 >= o2) { lam[0] = 0.5 * (g[S_H2] + lnP + lx(h2)); lam[1] = g[S_H2O] + lnP + lx(w) - 2 * lam[0]; }
        else { lam[1] = 0.5 * (g[S_O2] + lnP + lx(o2)); lam[0] = 0.5 * (g[S_H2O] + lnP + lx(w) - lam[1]); }
        lam[2] = 0.5 * (g[S_N2] + lnP + lx(n2));
        lnN = log(tot);
    }
    double n[NS] = {};
    for (int it = 0; it < 400; it++) {
        double N = exp(lnN);
        for (int s = 0; s < NS; s++) {
            if (!use[s]) { n[s] = 0; continue; }
            double e = -g[s] - lnP + lnN;
            for (int k = 0; k < 3; k++) e += EL[s][k] * lam[k];
            n[s] = exp(std::min(e, 300.0));
        }
        const int m = na + 1;
        double J[4][5] = {};
        double sumN = 0; for (int s = 0; s < NS; s++) sumN += n[s];
        for (int i = 0; i < na; i++) {
            int k = act[i];
            double r = -b[k];
            for (int s = 0; s < NS; s++) r += EL[s][k] * n[s];
            for (int j = 0; j < na; j++) { int l = act[j]; double v = 0; for (int s = 0; s < NS; s++) v += EL[s][k] * EL[s][l] * n[s]; J[i][j] = v; }
            double v = 0; for (int s = 0; s < NS; s++) v += EL[s][k] * n[s];
            J[i][m - 1] = v; J[m - 1][i] = v;
            J[i][m] = -r;
        }
        J[m - 1][m - 1] = sumN - N; J[m - 1][m] = -(sumN - N);
        for (int cI = 0; cI < m; cI++) {       // Gauss-Jordan with partial pivoting
            int pv = cI; for (int q = cI + 1; q < m; q++) if (fabs(J[q][cI]) > fabs(J[pv][cI])) pv = q;
            if (pv != cI) for (int z = 0; z <= m; z++) std::swap(J[cI][z], J[pv][z]);
            double d = J[cI][cI]; if (fabs(d) < 1e-300) d = 1e-300;
            for (int q = 0; q < m; q++) if (q != cI) { double f = J[q][cI] / d; for (int z = cI; z <= m; z++) J[q][z] -= f * J[cI][z]; }
        }
        double dx[4], mx = 0;
        for (int i = 0; i < m; i++) { double d = J[i][i]; dx[i] = J[i][m] / (fabs(d) < 1e-300 ? 1e-300 : d); mx = std::max(mx, fabs(dx[i])); }
        const double sc = std::min(1.0, 2.0 / std::max(mx, 1e-30));
        for (int i = 0; i < na; i++) lam[act[i]] += sc * dx[i];
        lnN += sc * dx[m - 1];
        if (mx < 1e-9) break;
    }
    double tot = 0; for (int s = 0; s < NS; s++) tot += n[s];
    for (int s = 0; s < NS; s++) c.x[s] = tot > 0 ? n[s] / tot : 0;
    return c;
}

// below ~1200 K nothing is dissociated: the mix is water, the excess H2 or O2, and N2 (no OH)
Comp Cold(const double b[3])
{
    Comp c;
    double H = b[0], O = b[1], N = b[2];
    double w = std::min(H / 2, O), h2 = (H - 2 * w) / 2, o2 = (O - w) / 2, n2 = N / 2;
    double tot = w + h2 + o2 + n2;
    if (tot <= 0) return c;
    c.x[S_H2O] = w / tot; c.x[S_H2] = h2 / tot; c.x[S_O2] = o2 / tot; c.x[S_N2] = n2 / tot;
    return c;
}
Comp State(const double b[3], double T, double p) { return T < 1200 ? Cold(b) : Equil(b, T, p); }

double HMass(const Comp& c, double T)      // J/kg
{
    double h = 0, M = 0;
    for (int s = 0; s < NS; s++) { h += c.x[s] * H_RT(s, T) * RU * T; M += c.x[s] * MW[s]; }
    return M > 0 ? h / (M * 1e-3) : 0;
}

void ExhaustEl(double yH2, double b[3]) { b[0] = 2 * yH2 / 2.016 + 2 * (1 - yH2) / 18.015; b[1] = (1 - yH2) / 18.015; b[2] = 0; }
void AirEl(double yO2, double b[3]) { b[0] = 0; b[1] = 2 * yO2 / 31.998; b[2] = 2 * (1 - yO2) / 28.013; }

// CIE 1931 (the Wyman, Sloan & Shirley 2013 fit, as the blackbody table in Exhaust.cpp) to linear sRGB
double Cie(double x, double m, double s1, double s2) { double s = x < m ? s1 : s2; double t = (x - m) / s; return exp(-0.5 * t * t); }
void Xyz(double l, double& X, double& Y, double& Z)
{
    X = 1.056 * Cie(l, 599.8, 37.9, 31.0) + 0.362 * Cie(l, 442.0, 16.0, 26.7) - 0.065 * Cie(l, 501.1, 20.4, 26.2);
    Y = 0.821 * Cie(l, 568.8, 46.9, 40.5) + 0.286 * Cie(l, 530.9, 16.3, 31.1);
    Z = 1.217 * Cie(l, 437.0, 11.8, 36.0) + 0.681 * Cie(l, 459.0, 26.0, 13.8);
}
void ToRgb(double X, double Y, double Z, double o[3])
{
    o[0] = 3.2406 * X - 1.5372 * Y - 0.4986 * Z; o[1] = -0.9689 * X + 1.8758 * Y + 0.0415 * Z; o[2] = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
}
// the continuum's spectral radiance per [OH]^2 (m^-6) per metre of path [W/(m2 sr nm)]: the Vanpee-Mainiero rate at
// 375 nm in the Fiala spectrum's shape (220-600 nm, flat maximum at 440 nm: a super-Gaussian of half-width 120 nm)
double Cont(double l)
{
    auto S = [](double x) { double t = (x - 440.0) / 120.0; return exp(-t * t * t * t); };
    const double k = 1.53e-21 * 1e-6 * S(l) / S(375.0);       // m3/s/nm
    const double E = 6.62607015e-34 * 2.99792458e8 / (l * 1e-9);
    return k * E / (4 * PI);
}
double Planck(double l, double T)  // W/(m2 sr nm)
{
    double lm = l * 1e-9, e = 0.0143877735 / (lm * T);
    return e > 700 ? 0 : 1.191042953e-16 / pow(lm, 5) / (exp(e) - 1) * 1e-9;
}

}  // namespace

double EsIce(double T)
{
    T = std::max(T, 20.0);
    return exp(9.550426 - 5723.265 / T + 3.53068 * log(T) - 0.00728332 * T);
}
double EsLiquid(double T)
{
    if (T < 123) return EsIce(T);
    T = std::min(T, 373.0);
    return exp(54.842763 - 6763.22 / T - 4.210 * log(T) + 0.000367 * T +
               tanh(0.0415 * (T - 218.8)) * (53.878 - 1331.22 / T - 9.44523 * log(T) + 0.014025 * T));
}

double ExhaustHeat(double yH2, double Te, double p, double Ta)
{
    double b[3]; ExhaustEl(yH2, b);
    return HMass(State(b, Te, std::max(p, 1e3)), Te) - HMass(Cold(b), std::max(Ta, 20.0));
}

double ExhaustOH(double yH2, double T, double p)
{
    double b[3]; ExhaustEl(yH2, b);
    return State(b, T, p).x[S_OH];
}

void Mix(MixTable& t, double yH2, double Te, double ve, double pa, double Ta, double yO2)
{
    double be[3], ba[3];
    ExhaustEl(yH2, be); AirEl(yO2, ba);
    t.fs = yO2 > 0 ? 1.0 / (1.0 + 7.937 * yH2 / yO2) : 0.0;           // 7.94 kg of O2 burn 1 kg of H2
    const double fsU = t.fs > 0 ? t.fs : 0.03;
    const double he = HMass(State(be, Te, std::max(pa, 1e3)), Te);
    Comp ca; ca.x[S_O2] = yO2 / 31.998; ca.x[S_N2] = (1 - yO2) / 28.013;
    { double s = ca.x[S_O2] + ca.x[S_N2]; ca.x[S_O2] /= s; ca.x[S_N2] /= s; }
    const double ha = HMass(ca, std::max(Ta, 20.0));
    for (int i = 0; i < NT; i++) {
        const double u = (i + 0.5) / NT, c = std::min(fsU * u / (1 - u), 1.0);
        double b[3]; for (int k = 0; k < 3; k++) b[k] = c * be[k] + (1 - c) * ba[k];
        const double h = c * he + (1 - c) * ha + 0.5 * ve * ve * c * (1 - c);
        double lo = std::max(Ta, 30.0), hi = 3500;
        Comp x;
        for (int it = 0; it < 36; it++) {
            double T = 0.5 * (lo + hi);
            x = State(b, T, pa);
            if (HMass(x, T) > h) hi = T; else lo = T;
            if (hi - lo < 2) break;
        }
        const double T = 0.5 * (lo + hi);
        if (T >= 1200) x = Equil(b, T, pa);
        t.T[i] = (float)T;
        t.oh[i] = (float)(x.x[S_OH] * pa / (KB * T) * 1e-20);
    }
}

void ContinuumRGB(double K[3])
{
    double c[3] = {}, s[3] = {};
    for (double l = 380; l <= 780; l += 2.5) {
        double X, Y, Z, r[3]; Xyz(l, X, Y, Z);
        ToRgb(X * Cont(l), Y * Cont(l), Z * Cont(l), r); for (int i = 0; i < 3; i++) c[i] += r[i];
        const double B = Planck(l, 5772);
        ToRgb(X * B, Y * B, Z * B, r); for (int i = 0; i < 3; i++) s[i] += r[i];
    }
    for (int i = 0; i < 3; i++) K[i] = std::max(c[i], 0.0) / s[i] * 1e40;   // per (1e20 m^-3)^2
}

double ContinuumLuminance()
{
    double y = 0;
    for (double l = 380; l <= 780; l += 2.5) { double X, Y, Z; Xyz(l, X, Y, Z); y += Y * Cont(l) * 2.5; }
    return 683.0 * y * 1e40;
}

double Condense(double W, double Q, double pa, double Ta, double cp, double Ma, double ea, double& cStar)
{
    cStar = 0;
    if (W <= 0 || Q <= 0 || pa <= 0) return 0;
    double best = 0, fc[64], cc[64];
    // the dilute mixing line: up to where the mix is ~200 K over the ambient (beyond, no water condenses)
    const double cMax = std::min(0.1, 200.0 * cp / Q), l0 = -8.0, l1 = log10(std::max(cMax, 2e-8));
    for (int i = 0; i < 64; i++) {
        const double c = pow(10.0, l0 + (l1 - l0) * i / 63.0);
        const double T = Ta + c * Q / cp;
        const double e = ea + c * W * (Ma / 18.015) * pa;            // dilute: the water's mole fraction x the pressure
        const double es = EsLiquid(T);                                 // droplets first (liquid saturation; ice < 123 K)
        cc[i] = c; fc[i] = e > es ? (e - es) / e : 0;
        best = std::max(best, fc[i]);
    }
    if (best > 0) for (int i = 63; i >= 0; i--) if (fc[i] >= 0.5 * best) { cStar = cc[i]; break; }
    return best;
}

}
