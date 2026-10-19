// AtmoFX fork: wakes on a liquid surface (Titan's seas). Module side: Orbitersdk/samples/AtmoFX/AtmoFXWake.h.
// Drawn as patches lying on the liquid level that carry their own wave slopes and re-shade the surface there
// (sky reflection by Fresnel over the deep liquid colour), so the terrain shader is not touched:
//  - Kelvin wake: the linear deep-water solution for a moving pressure patch, precomputed in units of the wavelength
//    lambda = 2 pi v^2 / g (Textures\AtmoFX\kelvin_wake.png), scaled and turned along the motion;
//  - rings: around bobbing parts, and expanding packets after an impulse.
#pragma once
#include "D3D9Client.h"

class Scene;

namespace LiquidWake {
	void Render(LPDIRECT3DDEVICE9 dev, Scene* scn);
	void Release();
	// a small ring where something falls into the liquid (client-internal, e.g. drops): hBody the planet, gpos global [m] on
	// or near the level, radius of the splash [m], amp 0..4 (1 = a drop of that size). Cheap; at most 96 rings live at a time
	// (the oldest are dropped), and rings farther than 300 m from the camera are ignored. Main render thread only.
	void Ripple(OBJHANDLE hBody, const VECTOR3& gpos, double radius, double amp);
}

extern "C" __declspec(dllexport) void AtmoFX_SetWake(OBJHANDLE hVessel, int id, const double* pos, double radius, double strength);
