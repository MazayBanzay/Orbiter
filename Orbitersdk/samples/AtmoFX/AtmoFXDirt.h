// AtmoFXDirt.h - the world's dirt on a vessel (Titan's tholin film, lunar/martian dust, Io's sulphur), drawn by the
// AtmoFX D3D9Client fork in the mesh shaders (header only, no link dependency). The stock client has no such export:
// the calls then do nothing.
//
//   AtmoFXDirt::Set(GetHandle(), 0.6, 0.30, 0.22, 0.12, -0.4);   // amount 0..1, dirt albedo r g b, topY [m]
//   AtmoFXDirt::Set(GetHandle(), 0, 0, 0, 0, 0);                 // clean (amount 0 removes)
//
// amount: how dirty (accumulate and wash it yourself; 1 = thick); r g b: the dirt's albedo (it replaces the material's,
// so it is lighter on dark composite and darker on white); topY: height in the vessel frame (+y up) up to which the dirt
// is thick. Above it fades out over ~0.6 m with ragged spots and downward runs; surfaces facing +y get a light dusting.
// Dirt is matte: speculars, reflections and metalness are cut where it lies. Kept until changed or the vessel is
// deleted; call when the values change.
#pragma once
#include <windows.h>
#include "OrbiterAPI.h"

namespace AtmoFXDirt {
	typedef void (*SetFn)(OBJHANDLE, double, double, double, double, double);
	inline SetFn Fn()
	{
		static SetFn fn = nullptr;
		static bool looked = false;
		if (!looked) {
			HMODULE h = GetModuleHandleA("D3D9Client.dll");
			if (h) fn = (SetFn)GetProcAddress(h, "AtmoFX_SetDirt");
			looked = (h != nullptr);
		}
		return fn;
	}
	inline void Set(OBJHANDLE hVessel, double amount, double r, double g, double b, double topY)
	{
		if (SetFn f = Fn()) f(hVessel, amount, r, g, b, topY);
	}
}
