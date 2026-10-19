// AtmoFXMark.h - marks a module lays on the ground, drawn by the AtmoFX D3D9Client fork (header only, no link dependency):
// a wheel's rut, the pit a spinning wheel digs, a wet rut out of a sea. The client keeps each in the body's frame, lying on
// the drawn terrain (it follows the ground, not the vessel), and shows its depth by the light: the wall facing the sun
// lighter, the far one darker, a berm of thrown soil along the edge, over the disturbed soil's own darkening; a wet rut darker
// still, drying out over its life. It fades over the last fifth of its life. At most 3000 marks: the oldest go first; drawn
// within 300 m of the camera; none under a liquid. The stock client (or any other) has no such export: Available() is
// false and the calls do nothing.
//
//   AtmoFXMark::Add(GetHandle(), -1, hubBottom, _V(0, 0, 1), 0.6, 0.5, 0.04, AtmoFXMark::RUT, 600.0);   // a rut segment
//   AtmoFXMark::Add(GetHandle(), 8600 + i, hubBottom, _V(0, 0, 1), 1.3, 0.7, dig, AtmoFXMark::PIT, 900.0);   // its pit
//
// id: >= 0 - the mark with that id of this vessel is updated (a pit growing as the wheel digs; call it as often as you like,
// the client rebuilds it only when it changed by a couple of cm); < 0 - a new mark every call (a rut: one segment per
// 0.5-1 m of the wheel's way, its length that way plus nothing - the client overlaps the ends itself).
// pos: the mark's centre, vessel frame [m] (on the ground under the wheel); dir: along it, vessel frame (levelled by the
// client); length along, width across [m] (the trough; the berm reaches ~35 % more each side); depth [m]; kind: RUT / PIT /
// WETRUT; life [s] (each update of an id'd mark restarts it).
#pragma once
#include <windows.h>
#include "OrbiterAPI.h"

namespace AtmoFXMark {
	enum Kind { RUT = 0, PIT = 1, WETRUT = 2 };
	typedef void (*MarkFn)(OBJHANDLE, int, const double*, const double*, double, double, double, int, double);
	inline MarkFn Fn()
	{
		static MarkFn fn = nullptr;
		static bool looked = false;
		if (!looked) {
			HMODULE h = GetModuleHandleA("D3D9Client.dll");
			if (h) fn = (MarkFn)GetProcAddress(h, "AtmoFX_Mark");
			looked = (h != nullptr);
		}
		return fn;
	}
	inline bool Available() { return Fn() != nullptr; }
	inline void Add(OBJHANDLE hVessel, int id, const VECTOR3& pos, const VECTOR3& dir, double length, double width, double depth,
		int kind, double life)
	{
		if (MarkFn f = Fn()) {
			const double p[3] = { pos.x, pos.y, pos.z }, d[3] = { dir.x, dir.y, dir.z };
			f(hVessel, id, p, d, length, width, depth, kind, life);
		}
	}
}
