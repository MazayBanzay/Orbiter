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

		// the solar cycle from the date: 0 at the minimum, 1 at the maximum (cycle 25: minimum 2019.96, maximum ~4.8 years on)
		double SolarActivity(double mjd)
		{
			const double year = 2000.0 + (mjd - 51544.5) / 365.25;
			double ph = std::fmod((year - 2019.96) / 11.0, 1.0); if (ph < 0) ph += 1;
			const double s = std::sin(PI * std::pow(ph, 0.844));
			return s * s;
		}
		unsigned Hash(unsigned x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
		double U(unsigned h, unsigned k) { return (Hash(h * 0x9E3779B9u + k * 0x85EBCA6Bu + 0x632BE5ABu) & 0xFFFFFF) / 16777216.0; }

		// solar energetic particles now (Sv/h behind 0.3 g/cm^2) at rAU from the Sun and ecliptic longitude lon [rad]: the
		// events of the last 10 days, the same for everyone at that time and place (seeded by the day)
		double SepRate(double mjd, double rAU, double lon, double act)
		{
			const double perDay = (1.0 + 14.0 * act) / 365.25;
			const double tScale = std::pow((std::max)(rAU, 0.02), 0.6), fScale = std::pow((std::max)(rAU, 0.02), -2.1);
			const double tauR = 4.0 / 24 * tScale, tauD = 20.0 / 24 * tScale;     // days
			const double foot = lon + 2.865e-6 * rAU * 1.495978707e11 / 4.5e5;   // the field line's footpoint (450 km/s wind)
			double sum = 0;
			const long d0 = static_cast<long>(std::floor(mjd));
			for (long k = d0 - 10; k <= d0; ++k)
			{
				const unsigned h = Hash(static_cast<unsigned>(k));
				if (U(h, 0) >= perDay) continue;
				const double t = mjd - (k + U(h, 1));                               // days since the flare
				if (t <= 0) continue;
				const double D1 = (std::min)(3.0, 3e-3 * std::pow((std::max)(1 - U(h, 2), 1e-9), -0.9));   // Sv at 1 AU, connected
				double dl = std::remainder(2 * PI * U(h, 3) - foot, 2 * PI);
				const double conn = 0.12 + 0.88 * std::exp(-dl * dl / (0.79 * 0.79));   // ~45 deg either side
				const double shape = t < tauR ? t / tauR : std::exp(-(t - tauR) / tauD);
				sum += D1 * conn * fScale / (0.5 * tauR + tauD) * shape / 24;         // Sv/h
			}
			return sum;
		}
	}

	RadEnv Radiation::Sample(const VESSEL* v, double ambientKPa, double g) const
	{
		RadEnv e;
		double gcrFactor = 1.0, sepFactor = 1.0;
		char buf[64];

		// ---- the Sun: the cycle, the distance, the particle events ----
		const double mjd = oapiGetSimMJD(), act = SolarActivity(mjd);
		{
			VECTOR3 gp, sp; v->GetGlobalPos(gp); oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sp);
			const VECTOR3 d = gp - sp;
			e.sunAU = length(d) / 1.495978707e11;
			e.sep = SepRate(mjd, e.sunAU, std::atan2(d.z, d.x), act);
			gcrFactor *= (1.25 - 0.45 * act) / 0.935 * std::clamp(1 + 0.03 * (e.sunAU - 1), 0.85, 1.5);   // 1 = MSL's cruise
		}

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
			if (R < 11) sepFactor *= Smooth(3, 6, L);                             // the softer solar protons: the polar shells only
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
				e.electrons *= shade; e.protons *= shade; sepFactor *= shade;
				// GCR: half the sky, plus albedo neutrons from an airless regolith (Chang'e-4: ~1.4 mSv/day on the Moon)
				gcrFactor *= ambientKPa < 0.01 ? (std::min)(1.0, shade + 0.25) : shade;
			}
		}
		if (ambientKPa > 0.01 && g > 0.1)
		{
			const double column = ambientKPa * 1000 / g / 10;                    // g/cm^2 of air above
			const double a = std::exp(-column / 180);
			e.electrons *= std::exp(-column / 0.4); e.protons *= a; gcrFactor *= a;
			sepFactor *= std::exp(-column / 20);
		}
		e.gcr = GCR_FREE * gcrFactor;
		e.sep *= sepFactor;
		if (e.where.empty()) { std::snprintf(buf, sizeof buf, "Sun %.2f AU", e.sunAU); e.where = buf; }
		if (e.sep > 1e-4) e.where += " SPE";     // a solar particle event under way (>0.1 mSv/h behind thin shielding)
		return e;
	}

	double Radiation::Dose(const RadEnv& e, double areal, double fieldFactor, double brems)
	{
		const double x = (std::max)(0.0, areal - 0.3);
		const double f = (std::max)(1.0, fieldFactor);
		const double el = e.electrons * (std::exp(-x / 0.35) / f + 0.01 * brems * std::exp(-x / 15));   // bremsstrahlung is not deflected
		const double pr = e.protons * std::exp(-x / 12) / f;
		const double gc = e.gcr * (std::max)(0.6, 1 - 0.03 * x) / (f > 1 ? 1.5 : 1.0);
		const double sp = e.sep * std::exp(-x / 4.0) / f;
		return el + pr + gc + sp;
	}
}
