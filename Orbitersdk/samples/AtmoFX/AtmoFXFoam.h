// AtmoFXFoam.h - bubbles and foam from warm parts in a liquid (Titan's seas), drawn by the AtmoFX D3D9Client fork
// (header only, no link dependency). The stock client has no such export: the calls then do nothing.
//
//   AtmoFXFoam::Set(GetHandle(), 3, wheelPos, 0.45, wheelK - liquidK);                 // a warm motor-wheel
//   AtmoFXFoam::Set(GetHandle(), 20, leakPos, 0.05, 0.0, 0.002);                       // a leak: 2 l/s of cabin gas
//   AtmoFXFoam::Set(GetHandle(), 3, _V(0,0,0), 0, 0);                                  // remove
//
// id: the module's own number for the node (unique per vessel); pos: the centre of the submerged part, vessel frame [m];
// radius: its size [m]; dT: its temperature above the liquid [K] (dissolved N2 comes out, film boiling when very hot);
// gasFlow: gas escaping from the node [m^3/s at the local pressure] (a leak), 0 for a plain warm part.
// The client keeps the node until it is removed (dT = 0 and gasFlow = 0) or the vessel is deleted, so a call when the value
// changes is enough. It finds the liquid level itself (AtmoFX_LiquidLevel in Config\GC\<body>.atm.cfg): nodes above
// the liquid make nothing. Bubbles rise with the speed of mm bubbles in liquid methane and burst into foam at the surface.
#pragma once
#include <windows.h>
#include "OrbiterAPI.h"

namespace AtmoFXFoam {
	typedef void (*SetFn)(OBJHANDLE, int, const double*, double, double, double);
	inline SetFn Fn()
	{
		static SetFn fn = nullptr;
		static bool looked = false;
		if (!looked) {
			HMODULE h = GetModuleHandleA("D3D9Client.dll");
			if (h) fn = (SetFn)GetProcAddress(h, "AtmoFX_SetFoam");
			looked = (h != nullptr);
		}
		return fn;
	}
	inline void Set(OBJHANDLE hVessel, int id, const VECTOR3& pos, double radius, double dT, double gasFlow = 0.0)
	{
		if (SetFn f = Fn()) {
			const double p[3] = { pos.x, pos.y, pos.z };
			f(hVessel, id, p, radius, dT, gasFlow);
		}
	}
}
