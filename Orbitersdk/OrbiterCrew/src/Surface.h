// OrbiterCrew - standing and walking on a planetary surface.
// A person on the ground is kept in Orbiter's "landed" state and moved kinematically: the feet decide the motion,
// not thrusters. Positions are handled as vectors in the planet frame (Orbiter's frame: left-handed, y = north pole):
// the local horizon at a point is (up, north, east), the body axes are x = right, y = up, z = forward.
// A person stays upright on slopes (gravity decides "up", not the terrain normal).
#pragma once
#include <Orbitersdk.h>
#include <cmath>

namespace ocrew::surface
{
	struct Horizon { VECTOR3 up, north, east; };

	inline Horizon At(double lng, double lat)
	{
		const double cl = std::cos(lat), sl = std::sin(lat), cg = std::cos(lng), sg = std::sin(lng);
		return { _V(cl * cg, sl, cl * sg), _V(-sl * cg, cl, -sl * sg), _V(-sg, 0, cg) };
	}

	// Euler angles of a local->reference rotation, in the convention of VESSELSTATUS2::arot
	inline VECTOR3 Euler(const MATRIX3& M) { return _V(std::atan2(M.m23, M.m33), -std::asin(M.m13), std::atan2(M.m12, M.m11)); }

	inline MATRIX3 FromAxes(const VECTOR3& x, const VECTOR3& y, const VECTOR3& z)
	{
		return _M(x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z);
	}

	inline VECTOR3 Forward(const Horizon& h, double hdg) { return h.north * std::cos(hdg) + h.east * std::sin(hdg); }

	// standing upright at status.surf_lng/lat/hdg, the body origin 'elevation' metres above the ground
	inline void Stand(VESSELSTATUS2& s, double elevation)
	{
		const Horizon h = At(s.surf_lng, s.surf_lat);
		const VECTOR3 f = Forward(h, s.surf_hdg);
		s.status = 1;
		s.arot = Euler(FromAxes(crossp(h.up, f), h.up, f));
		s.vrot.x = elevation;
	}

	// lying on the back, head towards the heading
	inline void Lie(VESSELSTATUS2& s, double elevation)
	{
		const Horizon h = At(s.surf_lng, s.surf_lat);
		const VECTOR3 f = Forward(h, s.surf_hdg);
		s.status = 1;
		s.arot = Euler(FromAxes(crossp(f, h.up), f, h.up));
		s.vrot.x = elevation;
	}

	// move 'fwd' metres along the heading and 'right' metres to the right on a sphere of 'radius';
	// the heading is carried along (parallel transport), so walking straight follows a great circle
	inline void Walk(VESSELSTATUS2& s, double radius, double fwd, double right)
	{
		const Horizon h = At(s.surf_lng, s.surf_lat);
		const VECTOR3 f = Forward(h, s.surf_hdg), r = crossp(h.up, f);
		const VECTOR3 p = unit(h.up + (f * fwd + r * right) / radius);
		s.surf_lng = std::atan2(p.z, p.x);
		s.surf_lat = std::asin(p.y);
		const Horizon h2 = At(s.surf_lng, s.surf_lat);
		const VECTOR3 f2 = unit(f - p * dotp(f, p));
		double hdg = std::atan2(dotp(f2, h2.east), dotp(f2, h2.north));
		if (hdg < 0) hdg += PI2;
		s.surf_hdg = hdg;
	}
}
