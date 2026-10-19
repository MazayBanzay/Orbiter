// AtmoFXHeat.h - hot nodes for the heat shimmer of the AtmoFX D3D9Client fork (header only, no link dependency).
// The stock client (or any other) simply has no such export: the calls then do nothing.
//
// Exhausts need nothing: every exhaust with a non-zero level shimmers by itself (in an atmosphere).
// Hot parts that are not exhausts (motors, radiators, a hull heated by lava) are declared here:
//
//   AtmoFXHeat::Set(GetHandle(), 1, _V(0.8, -0.3, -1.2), 0.25, nodeK - ambientK);   // on change (or each step)
//   AtmoFXHeat::Set(GetHandle(), 1, _V(0, 0, 0), 0, 0);                              // remove
//
// id: the module's own number for the node (unique per vessel); pos: vessel frame [m]; radius: size of the hot body [m];
// dT: its temperature above the ambient air [K]. The client keeps the node until it is removed or the vessel is deleted,
// so a call when the value changes is enough (calling every step is also fine). Strength follows the air: density * dT / T,
// nothing in vacuum.
#pragma once
#include <windows.h>
#include "OrbiterAPI.h"

namespace AtmoFXHeat {
	typedef void (*SetFn)(OBJHANDLE, int, double, double, double, double, double);
	inline SetFn Fn()
	{
		static SetFn fn = nullptr;
		static bool looked = false;
		if (!looked) {
			HMODULE h = GetModuleHandleA("D3D9Client.dll");
			if (h) fn = (SetFn)GetProcAddress(h, "AtmoFX_SetHeatSource");
			looked = (h != nullptr);		// look again later if the client is not loaded yet
		}
		return fn;
	}
	inline void Set(OBJHANDLE hVessel, int id, const VECTOR3& pos, double radius, double dT)
	{
		if (SetFn f = Fn()) f(hVessel, id, pos.x, pos.y, pos.z, radius, dT);
	}
}
