// The chemistry and optics of a hydrogen (H2 + H2O) exhaust for the plumes (Exhaust.cpp, H2Chem.cpp):
//   - chemical equilibrium of H/O/N mixtures (H2 H2O OH H O O2 N2; NASA 7-coefficient polynomials, GRI-Mech 3.0 data)
//     by element potentials;
//   - the jet's mixing with the ambient gas as a table over the exhaust's mass fraction c: the mix's temperature from its
//     enthalpy (the exhaust's static enthalpy, the air's, and the jet's dissipated kinetic energy ve^2/2 c (1 - c), Pr = 1)
//     and its OH number density in equilibrium - with oxygen in the air the free H2 burns where c is near stoichiometric
//     (a Burke-Schumann flame sheet emerges from the mixing field itself), without it the mix only cools;
//   - the visible light of a soot-free H2 flame: the "blue continuum" (Fiala 2015, TUM: 220-600 nm, flat maximum at
//     440 nm, emissivity ~ [OH]^2; Vanpee & Mainiero 1979: 1.53e-21 cm3/(molecule s nm) at 375 nm, 3000 K) - its rate
//     per channel of the client's linear sRGB, relative to the Sun's radiance;
//   - water condensing out of the plume by the Schmidt-Appleman mixing line (Schumann 1996).
#pragma once

namespace H2Chem {

const int NT = 32;                 // table entries over u = c / (c + fs) in [0, 1)

struct MixTable
{
    double fs = 0;                 // the exhaust's stoichiometric share in its mix with the air (0: no oxygen - then the
                                   // table's scale for u is fixed to 0.03)
    float oh[NT] = {};             // the OH number density [1e20 m^-3] at u_i
    float T[NT] = {};              // the mix's temperature [K] at u_i
};

// the exhaust: mass fraction of free hydrogen yH2 (the rest water), static temperature Te [K], speed ve [m/s];
// the ambient gas: pressure pa [Pa], temperature Ta, oxygen mass fraction yO2 (the rest taken as N2)
void Mix(MixTable& t, double yH2, double Te, double ve, double pa, double Ta, double yO2);
// the heat one kg of exhaust gives off cooling from (Te, equilibrium at p) to Ta, its H atoms recombined [J/kg]
double ExhaustHeat(double yH2, double Te, double p, double Ta);
// the OH mole fraction of the exhaust in equilibrium at (T, p)
double ExhaustOH(double yH2, double T, double p);
// the blue continuum's rate per channel: radiance [the scene's units / kE] = K[c] x [OH]^2 [(1e20 m^-3)^2] x path [m];
// the Sun-relative per channel normalisation as ExBlackbodyRel (the Sun white), times the pass's kE
void ContinuumRGB(double K[3]);
// ... its luminance per the same [OH]^2 path [cd/m2 per (1e20 m^-3)^2 m] (for the log)
double ContinuumLuminance();

// water condensing out of the mixing plume (Schmidt-Appleman). W kg of water and Q J of heat per kg of exhaust (the
// afterburning included); the ambient gas: pa, Ta, cp [J/kg/K], its molar mass Ma [kg/kmol], the water's partial
// pressure ea [Pa]. Returns the largest share of the plume's water above saturation along the mixing line (0: no trail)
// and the exhaust's mass fraction cStar where it first exceeds half of that
double Condense(double W, double Q, double pa, double Ta, double cp, double Ma, double ea, double& cStar);
// saturation vapour pressure of water [Pa]: over liquid (Murphy & Koop 2005; supercooled down to 123 K) / over ice
double EsLiquid(double T);
double EsIce(double T);

}
