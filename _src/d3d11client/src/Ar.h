// The augmented-reality pass (Ar.cpp): world-space markers, terrain contours and paths drawn in the 3D scene with the
// scene's depth (bright where seen, faint where hidden). Fed by the AtmoFX HUD exports (Ext.cpp) and D3D11Hud::ArPath.
#pragma once
#include "Client.h"

// a world-space path: lng, lat [rad], alt above the local ground [m] per point; n = 0 removes it
void ArSetPath(int id, OBJHANDLE body, const double* lngLatAlt, int n, float widthPx, DWORD col, unsigned flags);
