// LavaThermal.h - reader for the Io lava-field thermal rasters (Config/Io/LavaField/LavaField.cfg), header-only.
// Generated data: Tantra_Design/io_lava/build.py. Not wired into OrbiterCrew yet - see README.md "Hooking into the suit".
//
//   static lava::Field io;  io.Load("Config\\Io\\LavaField\\LavaField.cfg");      // once (false if absent)
//   lava::Sample s;
//   if (io.At(latDeg, lngDeg, s) && s.lava) { s.contactK, s.radK, s.crustM, s.kind }
//   double tg = io.GroundRadiativeK(latDeg, lngDeg, 1.0);   // ground as a standing person sees it (0 = no lava near)
//
// Values are night-side surface temperatures of the frozen moment the tiles show. Outside lava (frost plain), the
// rasters hold 0: keep the body's day/night table (tvSurfaceTemps) there.
#pragma once
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)   // fopen/sscanf
#endif
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace lava
{
	struct Sample { bool lava = false; double contactK = 0, radK = 0, crustM = 0; int kind = 0; };

	enum Kind { FROST = 0, YEARS, MONTHS, DAYS, HOURS, TOE, MOLTEN, CONE };

	class Field
	{
		struct Raster
		{
			int n = 0; double latN = 0, lngW = 0, dlat = 0, dlng = 0, step = 0;
			std::vector<uint16_t> tc, tr, cr; std::vector<uint8_t> kd;
			bool Cell(double lat, double lng, int& r, int& c) const
			{
				r = (int)std::floor((latN - lat) / dlat); c = (int)std::floor((lng - lngW) / dlng);
				return r >= 0 && c >= 0 && r < n && c < n;
			}
		};
		std::vector<Raster> rs;   // finest last

	public:
		bool Load(const char* cfgPath)
		{
			rs.clear();
			FILE* f = std::fopen(cfgPath, "r");
			if (!f) return false;
			std::string dir(cfgPath);
			const size_t sl = dir.find_last_of("\\/");
			dir = sl == std::string::npos ? std::string() : dir.substr(0, sl + 1);
			char line[512];
			while (std::fgets(line, sizeof line, f))
			{
				if (std::strncmp(line, "RASTER ", 7)) continue;
				char file[128] = {}; Raster R;
				if (std::sscanf(line, "RASTER %127s N %d LAT_N %lf LNG_W %lf DLAT %lf DLNG %lf STEP_M %lf",
					file, &R.n, &R.latN, &R.lngW, &R.dlat, &R.dlng, &R.step) != 7) continue;
				FILE* d = std::fopen((dir + file).c_str(), "rb");
				if (!d) continue;
				const size_t N = (size_t)R.n * R.n;
				R.tc.resize(N); R.tr.resize(N); R.cr.resize(N); R.kd.resize(N);
				const bool ok = std::fread(R.tc.data(), 2, N, d) == N && std::fread(R.tr.data(), 2, N, d) == N
					&& std::fread(R.cr.data(), 2, N, d) == N && std::fread(R.kd.data(), 1, N, d) == N;
				std::fclose(d);
				if (ok) rs.push_back(std::move(R));
			}
			std::fclose(f);
			// finest last, so At() can stop at the first hit from the back
			for (size_t i = 1; i < rs.size(); ++i)
				for (size_t j = i; j > 0 && rs[j].step > rs[j - 1].step; --j) std::swap(rs[j], rs[j - 1]);
			return !rs.empty();
		}

		bool Loaded() const { return !rs.empty(); }

		// lat/lng in degrees, east-positive longitude
		bool At(double lat, double lng, Sample& s) const
		{
			for (auto it = rs.rbegin(); it != rs.rend(); ++it)
			{
				int r, c;
				if (!it->Cell(lat, lng, r, c)) continue;
				const size_t i = (size_t)r * it->n + c;
				s.contactK = it->tc[i] * 0.1; s.radK = it->tr[i] * 0.1; s.crustM = it->cr[i] * 0.001; s.kind = it->kd[i];
				s.lava = it->tc[i] > 0;
				return true;
			}
			return false;
		}

		// Effective radiative temperature of the ground seen from height h [m] above (lat, lng): T^4 averaged with the
		// view factor of a horizontal disc ring, dF = 2 h^2 r dr / (r^2 + h^2)^2, out to 30 h. Points off the lava count
		// as `backgroundK`. Returns 0 when no lava is within reach (keep the table then).
		double GroundRadiativeK(double lat, double lng, double h = 1.0, double backgroundK = 95.0) const
		{
			if (rs.empty()) return 0;
			const double R = 1.821e6, D = 180.0 / 3.14159265358979;
			const double cl = std::cos(lat / D);
			double sum4 = 0, wsum = 0; bool any = false;
			const int NR = 24, NA = 16;
			const double rmax = 30 * h;
			for (int k = 0; k < NR; ++k)
			{
				// rings equally spaced in view factor F(r) = r^2 / (r^2 + h^2)
				const double F0 = (double)k / NR * (rmax * rmax / (rmax * rmax + h * h)), F1 = (double)(k + 1) / NR * (rmax * rmax / (rmax * rmax + h * h));
				const double Fm = 0.5 * (F0 + F1), r = h * std::sqrt(Fm / (1 - Fm)), w = F1 - F0;
				for (int a = 0; a < NA; ++a)
				{
					const double az = (a + 0.5 * (k & 1)) * 2 * 3.14159265358979 / NA;
					Sample s;
					const double la = lat + r * std::cos(az) / R * D, ln = lng + r * std::sin(az) / (R * cl) * D;
					const double T = At(la, ln, s) && s.lava ? (any = true, s.radK) : backgroundK;
					sum4 += w / NA * T * T * T * T; wsum += w / NA;
				}
			}
			return any ? std::pow(sum4 / wsum, 0.25) : 0;
		}
	};
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
