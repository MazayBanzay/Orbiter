// AtmoFX fork: volumetric exhaust for add-ons that opt in with EXHAUSTSPEC.flags & EXHAUST_VOLUME (0x100) - the jet is
// ray-marched through a cylinder instead of drawn as the stock camera-facing ribbon (which is a line seen end-on).
// Shader: Modules\D3D9Client\ExhaustVolume.fx. The stock exhausts are untouched.
#pragma once
#include <d3d9.h>
#include <d3dx9.h>
#include "OrbiterAPI.h"

#define EXHAUST_VOLUME 0x100

class Scene;

namespace ExhaustVolume {
	// pW: the vessel's world matrix; camLocal: the camera in the vessel's frame; returns false if it could not draw
	// (the caller then draws the stock ribbon)
	bool Render(LPDIRECT3DDEVICE9 dev, const D3DXMATRIX* pW, const VECTOR3& camLocal, const EXHAUSTSPEC* es, SURFHANDLE def, Scene* scn);
	void Release();
}
