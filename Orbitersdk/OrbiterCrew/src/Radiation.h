// OrbiterCrew - the radiation environment and what a shield lets through.
//
// Three components, as dose rate (Sv/h) behind a thin layer (0.3 g/cm^2, a coverall or a thin suit):
//   electrons  trapped electrons (Earth's outer belt, Jupiter's belts): stopped by a few g/cm^2, leave bremsstrahlung
//   protons    trapped protons and heavy ions (Earth's inner belt, the Io torus): penetrating, slowly attenuated
//   gcr        galactic cosmic rays: ~1.8 mSv/day in interplanetary space, almost unshieldable; more at the solar minimum
//              (~2.4 mSv/day), less at the maximum, a few per cent less per AU towards the Sun (the radial gradient)
//   sep        solar energetic particles (protons, tens of MeV): events after flares and coronal mass ejections, ~1 a year
//              at the solar minimum and ~15 at the maximum (the cycle from the simulation date: cycle 25, minimum 2019.96,
//              11 years - as the D3D11 client's Sun). Each rises over hours and decays over a day; the dose at 1 AU behind
//              thin shielding is mostly a few mSv, one in a thousand ~1.5 Sv (August 1972). Towards the Sun the fluence
//              grows as r^-2.1 and the event shortens as r^0.6 (the peak ~r^-2.7: Helios, Lario et al. 2006). Only the
//              flares near the footpoint of the observer's Parker-spiral field line (~60 deg west at 1 AU) hit fully. Softer
//              than trapped protons: e-fold ~4 g/cm^2. Inside the magnetosphere only the polar shells (L > ~4) are reached
// Earth's belts follow the dipole L-shell (L = r / cos^2 magnetic latitude) and fade into the atmosphere below
// ~1000 km; the geomagnetic field and the planet's own body shade the GCR. Jupiter's belts follow the distance from
// the planet, log-interpolated through the moons' published surface dose rates (Io ~36 Sv/day, Europa ~5.4,
// Ganymede ~0.08, Callisto ~1e-4). An atmosphere shields by its column mass (pressure / g).
// Numbers are order-of-magnitude engineering values, chosen to reproduce the measured ISS, lunar, Martian and
// interplanetary rates and the Jovian moon environment.
#pragma once
#include <Orbitersdk.h>
#include <string>

namespace ocrew
{
	struct RadEnv
	{
		double electrons{}, protons{}, gcr{}, sep{};   // Sv/h behind 0.3 g/cm^2
		double sunAU{};                         // distance from the Sun
		std::string where;                      // for the HUD: "Earth L 1.6", "Jupiter 5.9 RJ", ...
	};

	class Radiation
	{
	public:
		RadEnv Sample(const VESSEL* v, double ambientKPa, double g) const;
		// dose rate (Sv/h) behind 'areal' g/cm^2 of passive shielding and an active field that divides the
		// charged components by 'fieldFactor' (1 = no field); 'brems': a high-Z liner's share of the bremsstrahlung left
		static double Dose(const RadEnv& e, double areal, double fieldFactor, double brems = 1.0);
	};
}
