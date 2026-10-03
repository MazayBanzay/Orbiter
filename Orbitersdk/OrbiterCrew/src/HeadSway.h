#pragma once
// The head on the neck aboard a ship: the eyes (the first-person camera) lag the cabin's accelerations and pick up
// its vibration. Physics, not an effect: the point the eyes sit at accelerates with the ship (its thrust, lift and
// drag, plus the rotation about the ship's CG: alpha x r + w x (w x r)); the neck is a damped spring (1.6 Hz,
// damping 0.45, the seat's headrest takes the steady part - so only the changes of the load move the head).
// Vibration: the engines buzz through the structure (12 Hz, ~1.5 mm per g of thrust); the air buffets the hull with
// the dynamic pressure (4 Hz, up to ~12 mm at 15 kPa), most at the sound barrier - an atmospheric entry shakes hardest.
#include "OrbiterAPI.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace oc {

class HeadSway {
public:
	// dt: step; f: specific force at the eyes in the person frame [m/s^2];
	// engineG: thrust acceleration [g]; q: dynamic pressure [Pa]; mach. Returns the eye offset in the person frame [m].
	VECTOR3 Step(double dt, const VECTOR3& f, double engineG, double q, double mach)
	{
		if (dt <= 0) return off;
		dt = (std::min)(dt, 0.05);
		if (!primed) { slow = f; primed = true; }
		slow += (f - slow) * (std::min)(1.0, dt / 1.5);         // the steady load: the headrest and the neck hold it
		const VECTOR3 d = f - slow;
		const double w = 2 * PI * 1.6, z = 0.45, k = 0.5;
		// the head lags: pushed against the load change (semi-implicit, stable at any frame rate)
		v += (x * (-w * w) - v * (2 * z * w) - d * k) * dt;
		x += v * dt;
		for (int i = 0; i < 3; i++) {
			const double c = 0.06;
			double& xi = i == 0 ? x.x : i == 1 ? x.y : x.z;
			if (xi > c) xi = c; else if (xi < -c) xi = -c;
		}
		// vibration: band-limited noise per axis
		const double eng = 0.0015 * std::clamp(engineG, 0.0, 3.0);
		const double trans = 1.0 + 1.5 * std::exp(-std::pow((mach - 1.0) / 0.25, 2));
		const double buf = 0.012 * std::clamp(q / 15e3, 0.0, 1.5) * trans;
		Noise(nE, 12.0, dt);
		Noise(nB, 4.0, dt);
		off = x + nE * (2.5 * eng) + nB * (2.5 * buf);
		return off;
	}
	void Reset() { x = v = off = nE = nB = _V(0, 0, 0); primed = false; }

private:
	VECTOR3 x{}, v{}, slow{}, off{}, nE{}, nB{};
	bool primed = false;
	uint32_t seed = 0x2545F491u;
	double Rand() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (seed & 0xFFFFFF) / double(0x800000) - 1.0; }
	void Noise(VECTOR3& n, double fc, double dt)
	{
		const double a = (std::min)(1.0, 2 * PI * fc * dt);
		n.x += (Rand() - n.x) * a; n.y += (Rand() - n.y) * a; n.z += (Rand() - n.z) * a;
	}
};

}  // namespace oc
