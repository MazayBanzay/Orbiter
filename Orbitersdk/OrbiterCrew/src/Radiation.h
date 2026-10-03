// OrbiterCrew - the radiation environment and what a shield lets through.
//
// Three components, as dose rate (Sv/h) behind a thin layer (0.3 g/cm^2, a coverall or a thin suit):
//   electrons  trapped electrons (Earth's outer belt, Jupiter's belts): stopped by a few g/cm^2, leave bremsstrahlung
//   protons    trapped protons and heavy ions (Earth's inner belt, the Io torus): penetrating, slowly attenuated
//   gcr        galactic cosmic rays: ~1.8 mSv/day in interplanetary space, almost unshieldable
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
		double electrons{}, protons{}, gcr{};   // Sv/h behind 0.3 g/cm^2
		std::string where;                      // for the HUD: "Earth L 1.6", "Jupiter 5.9 RJ", ...
	};

	class Radiation
	{
	public:
		RadEnv Sample(const VESSEL* v, double ambientKPa, double g) const;
		// dose rate (Sv/h) behind 'areal' g/cm^2 of passive shielding and an active field that divides the
		// charged components by 'fieldFactor' (1 = no field)
		static double Dose(const RadEnv& e, double areal, double fieldFactor);
	};
}
