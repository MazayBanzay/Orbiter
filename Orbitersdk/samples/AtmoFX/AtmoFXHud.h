// AtmoFXHud.h - conformal helmet HUD drawn inside the scene by the AtmoFX D3D9Client fork (header only, no link dependency).
// The stock client has no such exports: the calls then do nothing (keep the 2D HUD as the fallback).
//
// Per frame, while the helmet HUD is on (the client stops drawing 0.5 s after the last call):
//   AtmoFXHud::Frame(AtmoFXHud::TERRAIN | AtmoFXHud::MARKERS, _V(0.35, 1.0, 0.45), 10.0);   // colour, contour step [m]
//
// Markers live until removed (type REMOVE) and are drawn only while Frame() asks for MARKERS:
//   AtmoFXHud::Marker(1, hTitan, lng, lat, 0.0, AtmoFXHud::PAD, 6.0);               // a landing pad, 6 m, on the ground
//   AtmoFXHud::Marker(10, hTitan, lng, lat, 15.0, AtmoFXHud::GATE, 8.0, hdg);        // a glide-path gate 15 m above the ground
//   AtmoFXHud::Marker(1, hTitan, 0, 0, 0, AtmoFXHud::REMOVE, 0);
//
// TERRAIN: contour lines of the real terrain (from the elevation tiles), a brighter line every 5th step and the shoreline
// at the body's liquid level; they lie on the ground, so they are conformal and hidden by hills by themselves.
// MARKERS: drawn in the scene with depth: bright where visible, faint where hidden behind terrain or hulls.
// lng, lat [rad]; alt: above the local ground [m]; size [m]; heading [rad] (gates face along it).
#pragma once
#include <windows.h>
#include "OrbiterAPI.h"

namespace AtmoFXHud {
	enum Flags { TERRAIN = 1, MARKERS = 2 };
	enum Type { REMOVE = -1, TARGET = 0, PAD = 1, GATE = 2, WAYPOINT = 3 };
	typedef void (*FrameFn)(int, double, double, double, double);
	typedef void (*MarkerFn)(int, OBJHANDLE, double, double, double, int, double, double);
	inline HMODULE Client() { return GetModuleHandleA("D3D9Client.dll"); }
	inline void Frame(int flags, const VECTOR3& rgb, double contourStep)
	{
		static FrameFn fn = nullptr; static bool looked = false;
		if (!looked) { if (HMODULE h = Client()) { fn = (FrameFn)GetProcAddress(h, "AtmoFX_HudFrame"); looked = true; } }
		if (fn) fn(flags, rgb.x, rgb.y, rgb.z, contourStep);
	}
	inline void Marker(int id, OBJHANDLE hBody, double lng, double lat, double alt, int type, double size, double heading = 0.0)
	{
		static MarkerFn fn = nullptr; static bool looked = false;
		if (!looked) { if (HMODULE h = Client()) { fn = (MarkerFn)GetProcAddress(h, "AtmoFX_HudMarker"); looked = true; } }
		if (fn) fn(id, hBody, lng, lat, alt, type, size, heading);
	}
}
