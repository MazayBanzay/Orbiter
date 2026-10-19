// AtmoFX fork: Titan's methane clouds (convective cells and high E-W streaks), ray-marched in a
// reduced-resolution pass after the planet has been drawn. Data: Textures\AtmoFX\noise3d.dds (shared
// with AtmoVolume). Shader: Modules\D3D9Client\TitanFX_Volume.fx(o). Settings: Config\TitanFX_Volume.cfg
// (re-read per session). Kept apart from AtmoVolume (Jupiter, Io plume), which another work stream owns.
#pragma once
#include <d3d9.h>
#include <d3dx9.h>
#include "OrbiterAPI.h"

class Scene;

namespace TitanVolume {
	// draws the clouds if hObj is the configured body and the camera is low enough; returns true if drawn
	bool Render(LPDIRECT3DDEVICE9 dev, OBJHANDLE hObj, Scene* scn);
	void Release();
}
