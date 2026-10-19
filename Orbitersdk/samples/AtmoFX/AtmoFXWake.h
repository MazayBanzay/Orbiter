// AtmoFXWake.h - wakes on a liquid surface (Titan's seas), drawn by the AtmoFX D3D9Client fork (header only, no link
// dependency). The stock client has no such export: the calls then do nothing.
//
//   AtmoFXWake::Set(GetHandle(), 1, _V(0, -0.8, 0), 1.6, 1.0);     // the floating hull: waterline centre, half-width, strength
//   AtmoFXWake::Set(GetHandle(), 5, wheelPos, 0.45, 0.5);           // a wheel wading
//   AtmoFXWake::Set(GetHandle(), 9, entryPos, 1.0, 3.0);            // strength > 1: a one-off impulse (entry splash rings)
//   AtmoFXWake::Set(GetHandle(), 1, _V(0,0,0), 0, 0);               // remove
//
// id: the module's own number (unique per vessel); pos: centre of the part at the waterline, vessel frame [m];
// radius: its half-width at the waterline [m]; strength: how hard it presses on the liquid, 0..1 (floating ~1).
// The client takes the speed over the ground itself and draws, per linear deep-water theory:
//  - the Kelvin wake behind (wavelength 2 pi v^2 / g, the 19.47 deg wedge), when faster than ~0.11 m/s;
//  - rings around a part that bobs, and expanding rings after an impulse (strength > 1).
// Nodes are kept until removed or the vessel is deleted; a call when the values change is enough.
#pragma once
#include <windows.h>
#include "OrbiterAPI.h"

namespace AtmoFXWake {
	typedef void (*SetFn)(OBJHANDLE, int, const double*, double, double);
	inline SetFn Fn()
	{
		static SetFn fn = nullptr;
		static bool looked = false;
		if (!looked) {
			HMODULE h = GetModuleHandleA("D3D9Client.dll");
			if (h) fn = (SetFn)GetProcAddress(h, "AtmoFX_SetWake");
			looked = (h != nullptr);
		}
		return fn;
	}
	inline void Set(OBJHANDLE hVessel, int id, const VECTOR3& pos, double radius, double strength)
	{
		if (SetFn f = Fn()) {
			const double p[3] = { pos.x, pos.y, pos.z };
			f(hVessel, id, p, radius, strength);
		}
	}
}
