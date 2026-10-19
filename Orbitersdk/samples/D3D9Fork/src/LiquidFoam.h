// AtmoFX fork: bubbles and foam from warm parts and leaks in a liquid (Titan's seas). Module side:
// Orbitersdk/samples/AtmoFX/AtmoFXFoam.h. Warm parts release dissolved N2 (and boil the methane when very hot); the
// bubbles rise at mm-bubble speed, sway, and burst into foam patches that spread and fade on the surface. Under the surface
// the liquid column (LiquidVolume) dims them; seen from above they fade with their depth.
#pragma once
#include "D3D9Client.h"

class Scene;

namespace LiquidFoam {
	void Render(LPDIRECT3DDEVICE9 dev, Scene* scn);
	void Release();
}

extern "C" __declspec(dllexport) void AtmoFX_SetFoam(OBJHANDLE hVessel, int id, const double* pos, double radius, double dT, double gasFlow);
