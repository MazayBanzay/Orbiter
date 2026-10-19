// AtmoFX fork: conformal helmet HUD inside the scene (module side: Orbitersdk/samples/AtmoFX/AtmoFXHud.h).
//  - terrain contours: set on the camera's planet as vPlanet::HudTerrain() and drawn by the terrain shader;
//  - markers (target, pad, glide-path gate, waypoint): line geometry in the scene, depth-tested twice
//    (bright where visible, faint where hidden).
#pragma once
#include "D3D9Client.h"

class Scene;

namespace HudOverlay {
	void Render(LPDIRECT3DDEVICE9 dev, Scene* scn);
	void Release();
	bool TerrainOn(float* rgb, float* step);		// the contour request for the terrain shader (false: off)
}

extern "C" __declspec(dllexport) void AtmoFX_HudFrame(int flags, double r, double g, double b, double step);
extern "C" __declspec(dllexport) void AtmoFX_HudMarker(int id, OBJHANDLE hBody, double lng, double lat, double alt, int type, double size, double heading);
