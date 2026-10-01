// OrbiterCrew - the radiation environment (see Radiation.h).
#include "Radiation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ocrew
{
	namespace
	{
		const double GCR_FREE = 1.8e-3 / 24;    // Sv/h, interplanetary space (MSL RAD cruise)
		double Smooth(double e0, double e1, double x) { const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3 - 2 * t); }
		double Gauss(double x, double mu, double s) { const double d = (x - mu) / s; return std::exp(-0.5 * d * d); }

		// distance and latitude of the vessel in a body's equatorial frame
		bool Near(const VESSEL* v, const char* body, double& r, double& lat, double& radius)
		{
			OBJHANDLE h = oapiGetGbodyByName(const_cast<char*>(body));
			if (!h) return false;
			VECTOR3 gp; v->GetGlobalPos(gp);
			double lng; oapiGlobalToEqu(h, gp, &lng, &lat, &r);
			radius = oapiGetSize(h);
			return true;
		}

		// Jupiter: total dose rate (Sv/h, thin shielding) against distance in Jupiter radii, through the moons' values
		double JupiterRate(double rj)
		{
			static const double R[] = { 2.0, 5.9, 9.4, 15.0, 26.3, 60.0 };
			// open-space values: twice the published surface rates (on a moon its body shades half the sky)
			static const double D[] = { 16.0, 3.0, 0.45, 6.6e-3, 8.4e-6, 1e-9 };
			if (rj <= R[0]) return D[0];
			for (int i = 0; i < 5; ++i)
				if (rj <= R[i + 1])
				{
					const double t = (rj - R[i]) / (R[i + 1] - R[i]);
					return std::exp(std::log(D[i]) * (1 - t) + std::log(D[i + 1]) * t);
				}
			return 0;
		}
	}

	RadEnv Radiation::Sample(const VESSEL* v, double ambientKPa, double g) const
	{
		RadEnv e;
		double gcrFactor = 1.0;
		char buf[64];

		// ---- Earth: Van Allen belts on the dipole L-shell, faded into the atmosphere; geomagnetic GCR shading ----
		double r, lat, rad;
		if (Near(v, "Earth", r, lat, rad) && r < 20 * rad)
		{
			const double R = r / rad, c = std::cos(lat), L = R / (std::max)(c * c, 1e-3);
			const double altKm = (r - rad) / 1000;
			const double atm = Smooth(700, 1500, altKm);                         // the belts' inner edge sits in the atmosphere
			const double inside = 1 - Smooth(9, 11, R);                          // magnetopause
			e.protons += 0.03 * Gauss(L, 1.6, 0.35) * atm * inside;              // inner belt: 10-100 MeV protons
			e.electrons += 0.4 * Gauss(L, 4.5, 1.1) * Smooth(2.3, 3.3, L) * atm * inside;   // outer belt: MeV electrons
			if (R < 11) gcrFactor *= std::clamp(0.2 + 0.8 * (L - 1) / 6, 0.2, 1.0);   // geomagnetic cut-off shades low L
			std::snprintf(buf, sizeof buf, "Earth L %.1f", L); e.where = buf;
		}

		// ---- Jupiter: the belts and the Io plasma torus ----
		if (Near(v, "Jupiter", r, lat, rad))
		{
			const double rj = r / rad;
			const double tot = JupiterRate(rj);
			e.electrons += 0.7 * tot; e.protons += 0.3 * tot;                     // mostly electrons; ions in the torus
			if (rj < 60) { std::snprintf(buf, sizeof buf, "Jupiter %.1f RJ", rj); e.where = buf; }
		}

		// ---- the body under her shades half the sky; an atmosphere shields by its column mass ----
		OBJHANDLE ref = v->GetSurfaceRef();
		if (ref)
		{
			VECTOR3 rp; v->GetRelativePos(ref, rp);
			const double alt = length(rp) - oapiGetSize(ref);
			if (alt < 0.2 * oapiGetSize(ref))
			{
				const double shade = 0.5 + 0.5 * Smooth(0, 0.2 * oapiGetSize(ref), alt);
				e.electrons *= shade; e.protons *= shade;
				// GCR: half the sky, plus albedo neutrons from an airless regolith (Chang'e-4: ~1.4 mSv/day on the Moon)
				gcrFactor *= ambientKPa < 0.01 ? (std::min)(1.0, shade + 0.25) : shade;
			}
		}
		if (ambientKPa > 0.01 && g > 0.1)
		{
			const double column = ambientKPa * 1000 / g / 10;                    // g/cm^2 of air above
			const double a = std::exp(-column / 180);
			e.electrons *= std::exp(-column / 0.4); e.protons *= a; gcrFactor *= a;
		}
		e.gcr = GCR_FREE * gcrFactor;
		if (e.where.empty()) e.where = "interplanetary";
		return e;
	}

	double Radiation::Dose(const RadEnv& e, double areal, double fieldFactor)
	{
		const double x = (std::max)(0.0, areal - 0.3);
		const double f = (std::max)(1.0, fieldFactor);
		const double el = e.electrons * (std::exp(-x / 0.35) / f + 0.01 * std::exp(-x / 15));   // bremsstrahlung is not deflected
		const double pr = e.protons * std::exp(-x / 12) / f;
		const double gc = e.gcr * (std::max)(0.6, 1 - 0.03 * x) / (f > 1 ? 1.5 : 1.0);
		return el + pr + gc;
	}
}
