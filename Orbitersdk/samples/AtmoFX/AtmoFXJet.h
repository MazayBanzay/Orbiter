// AtmoFXJet.h - ground ejecta under a rocket jet, rendered by the AtmoFX D3D9Client fork (header only, no link dependency).
// The stock client (or any other) has no such export: the calls then do nothing.
//
// Call every frame for every nozzle while its jet runs (thrust > 0); thrust = 0 removes it. A nozzle not refreshed for
// 0.5 s of simulation time is dropped by the client (a paused simulation keeps it).
//
//   AtmoFXJet::Set(GetHandle(), 0, exitPos, jetDir, thrustN, 10000.0, 3000.0, AtmoFXJet::DRY, wet, 0.06);
//
// exitPos: nozzle exit, vessel frame [m]; jetDir: direction the gas flows (down the jet), vessel frame;
// thrust [N]; ve: exhaust velocity [m/s]; exitT: gas temperature at the exit [K]; ground: the surface under the vessel;
// wet: its moisture 0..1; rExit: exit radius [m].
// The client finds the ground along the jet, the stagnation pressure ~T/(pi r^2) with the jet's spreading, the threshold
// of the grains in the body's gas, and flies the grains with the body's gravity and gas drag (nothing in vacuum but gravity).
#pragma once
#include <windows.h>
#include "OrbiterAPI.h"

namespace AtmoFXJet {
	enum Ground { DRY = 0, WET = 1, LIQUID = 2, REGOLITH = 3, ICE = 4 };
	typedef void (*SetFn)(OBJHANDLE, int, const double*, const double*, double, double, double, int, double, double);
	inline SetFn Fn()
	{
		static SetFn fn = nullptr;
		static bool looked = false;
		if (!looked) {
			HMODULE h = GetModuleHandleA("D3D9Client.dll");
			if (h) fn = (SetFn)GetProcAddress(h, "AtmoFX_SetJet");
			looked = (h != nullptr);
		}
		return fn;
	}
	inline void Set(OBJHANDLE hVessel, int id, const VECTOR3& exitPos, const VECTOR3& jetDir, double thrust, double ve,
		double exitT, int ground, double wet, double rExit)
	{
		if (SetFn f = Fn()) {
			const double p[3] = { exitPos.x, exitPos.y, exitPos.z }, d[3] = { jetDir.x, jetDir.y, jetDir.z };
			f(hVessel, id, p, d, thrust, ve, exitT, ground, wet, rExit);
		}
	}
}
