// AtmoFX fork: volumetric cloud deck for Jupiter, ray-marched in a full-screen pass after the planet
// has been drawn (surface, stock cloud shell). Data: Textures\AtmoFX\noise3d.dds (cloud shape noise,
// 3D) and Textures\AtmoFX\jup_weather.dds (flow map: rgb albedo, a coverage). Shader:
// Modules\D3D9Client\AtmoFX_Volume.fx. Settings: Config\AtmoFX_Volume.cfg (read once).
#pragma once
#include <d3d9.h>
#include <d3dx9.h>
#include "OrbiterAPI.h"

class Scene;

namespace AtmoVolume {
	// draws the deck if hObj is the configured planet and the camera is near enough; returns true if drawn
	bool Render(LPDIRECT3DDEVICE9 dev, OBJHANDLE hObj, Scene* scn, double cloudRot);
	void Release();
}
