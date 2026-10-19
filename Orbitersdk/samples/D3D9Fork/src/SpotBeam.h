// AtmoFX fork: spotlights (vessel headlights) in the air and on liquids, from the scene's own light list:
//  - the beam: single scattering of the lamp's light by the gas and haze along the view ray inside the cone
//    (k_s from Config\GC\<body>.atm.cfg AtmoFX_BeamScatter [1/m], else Rayleigh-like by the air density; none in vacuum),
//    Henyey-Greenstein phase: brightest when looking along the beam. In daylight the sky outshines it, as it should;
//  - on a liquid surface (LiquidVolume level, camera above it): the lamp's mirror image, Fresnel-weighted.
// The lit spot on the ground is the client's own local lighting (LightConfiguration > 0).
#pragma once
#include "D3D9Client.h"

class Scene;

namespace SpotBeam {
	void Render(LPDIRECT3DDEVICE9 dev, Scene* scn);
	void Release();
}
