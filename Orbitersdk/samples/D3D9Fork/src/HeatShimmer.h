// AtmoFX fork: heat shimmer - refraction of the background through warm air over exhausts and hot vehicle parts.
// Drawn after the vessels and exhaust particles, before the cockpit pass and the 2D overlays (HUD, MFDs stay sharp).
// Physics: the deflection grows with the ambient density and the relative temperature excess dT/T, so it is off in vacuum.
//
// Sources:
//  - every exhaust with a non-zero level, automatically (its position, direction, length and width);
//  - explicit hot nodes from any module, through the export below (look it up with GetProcAddress, see AtmoFXHeat.h).
#pragma once
#include "D3D9Client.h"

class Scene;

namespace HeatShimmer {
	void Render(LPDIRECT3DDEVICE9 dev, Scene* scn);
	void Release();
}

// hVessel: owner; id: the owner's own number for the node; x, y, z: node position in the vessel frame [m];
// radius: size of the hot body [m]; dT: its temperature above the ambient air [K] (<= 0 removes the node).
extern "C" __declspec(dllexport) void AtmoFX_SetHeatSource(OBJHANDLE hVessel, int id, double x, double y, double z, double radius, double dT);
