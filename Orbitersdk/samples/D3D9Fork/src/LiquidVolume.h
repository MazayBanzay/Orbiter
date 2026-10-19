// AtmoFX fork: the view from under a liquid surface (Titan's seas). Active while the camera is below the liquid level
// over a lowered bed. From above, the liquid is shaded by the terrain shader (NewPlanet.hlsl, AtmoFX_Liquid).
//  - the surface seen from below: a curved grid at R + Level around the camera (real depth, so the bed and hulls hide it);
//    sky inside Snell's window, total internal reflection outside it;
//  - the liquid column: full-screen fog layers at growing view depths, depth-tested (no depth reads in D3D9), so the
//    scene thins out as exp(-k d) with its own distance.
// Keys in Config\GC\<body>.atm.cfg: AtmoFX_LiquidLevel [m from the mean radius], AtmoFX_LiquidExt [1/m],
// AtmoFX_LiquidColor r g b (the colour of the light scattered by the liquid), AtmoFX_LiquidIndex (refractive index).
#pragma once
#include "D3D9Client.h"

class Scene;

namespace LiquidVolume {
	void Render(LPDIRECT3DDEVICE9 dev, Scene* scn);
	void Release();
	bool CameraSubmerged();		// for other effects (e.g. no ejecta drawing under the liquid)
	bool Level(OBJHANDLE hPlanet, double* level, double* ext);	// the body's liquid level [m] and extinction [1/m], false if none
	// the liquid surface drawn as a sheet with depth slices under it (camera above the level and low enough): its radius
	// around the camera [m], or false. The terrain shader then leaves the liquid optics to the sheet inside that radius.
	bool SheetRadius(OBJHANDLE hPlanet, const VECTOR3& camGlobal, double* radius);
}
