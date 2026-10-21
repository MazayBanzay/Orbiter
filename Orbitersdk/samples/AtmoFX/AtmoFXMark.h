// AtmoFXMark.h - marks a module lays on the ground, drawn by the AtmoFX D3D9Client fork (header only, no link dependency):
// a wheel's rut, the pit a spinning wheel digs, a wet rut out of a sea. The client keeps each in the body's frame, lying on
// the drawn terrain (it follows the ground, not the vessel), and shows its depth by the light: the wall facing the sun
// lighter, the far one darker, a berm of thrown soil along the edge, the tread's chevrons pressed in - in the disturbed soil's
// colour (a little darker; a wet rut 15 % more, drying out over its life). Marks stay: only past 6000 do the oldest go; drawn
// within 300 m of the camera; none under a liquid. A module keeps its own in the scenario: Save() in clbkSaveState, Load()
// for each ATMOFX_MARK line in clbkLoadStateEx. The stock client (or any other) has no such export: Available() is false
// and the calls do nothing.
//
//   AtmoFXMark::Add(GetHandle(), -1, hubBottom, _V(0, 0, 1), 0.6, 0.5, 0.04, AtmoFXMark::RUT, 600.0);   // a rut segment
//   AtmoFXMark::Add(GetHandle(), 8600 + i, hubBottom, _V(0, 0, 1), 1.3, 0.7, dig, AtmoFXMark::PIT, 900.0);   // its pit
//
// id: >= 0 - the mark with that id of this vessel is updated (a pit growing as the wheel digs; call it as often as you like,
// the client rebuilds it only when it changed by a couple of cm); < 0 - a new mark every call (a rut: one segment per
// 0.5-1 m of the wheel's way, its length that way plus nothing - the client overlaps the ends itself).
// pos: the mark's centre, vessel frame [m] (on the ground under the wheel); dir: along it, vessel frame (levelled by the
// client); length along, width across [m] (the trough; the berm reaches ~35 % more each side); depth [m]; kind: RUT / PIT /
// WETRUT; life [s]: the time a wet rut takes to dry (marks themselves stay; each update of an id'd mark restarts it).
#pragma once
#include <windows.h>
#include <cstdio>
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
	inline FARPROC Proc(const char* name) { HMODULE h = GetModuleHandleA("D3D9Client.dll"); return h ? GetProcAddress(h, name) : nullptr; }
	inline void Add(OBJHANDLE hVessel, int id, const VECTOR3& pos, const VECTOR3& dir, double length, double width, double depth,
		int kind, double life)
	{
		if (MarkFn f = Fn()) {
			const double p[3] = { pos.x, pos.y, pos.z }, d[3] = { dir.x, dir.y, dir.z };
			f(hVessel, id, p, d, length, width, depth, kind, life);
		}
	}
	// clbkSaveState: this vessel's marks (its newest maxN) as ATMOFX_MARK lines
	inline void Save(OBJHANDLE hVessel, FILEHANDLE scn, int maxN = 3000)
	{
		typedef int (*CountFn)(OBJHANDLE); typedef int (*GetFn)(OBJHANDLE, int, double*, char*, int);
		CountFn cnt = (CountFn)Proc("AtmoFX_MarkCount"); GetFn get = (GetFn)Proc("AtmoFX_MarkGet");
		if (!cnt || !get) return;
		const int n = cnt(hVessel);
		for (int i = n > maxN ? n - maxN : 0; i < n; i++) {
			double r[10]; char body[64] = "";
			if (!get(hVessel, i, r, body, sizeof body)) break;
			char line[256];
			std::snprintf(line, sizeof line, "%s %d %d %.10f %.10f %.4f %.3f %.3f %.4f %.0f %.0f", body, (int)r[0], (int)r[1], r[2], r[3], r[4],
				r[5], r[6], r[7], r[8], r[9]);
			oapiWriteScenario_string(scn, const_cast<char*>("ATMOFX_MARK"), line);
		}
	}
	// clbkLoadStateEx: one ATMOFX_MARK line (what follows the key) back to the client; false if it is not one
	inline bool Load(OBJHANDLE hVessel, const char* line)
	{
		typedef void (*PutFn)(OBJHANDLE, const double*, const char*);
		PutFn put = (PutFn)Proc("AtmoFX_MarkPut");
		char body[64]; int id, kind; double r[10];
		if (std::sscanf(line, " %63s %d %d %lf %lf %lf %lf %lf %lf %lf %lf", body, &id, &kind, &r[2], &r[3], &r[4], &r[5], &r[6], &r[7], &r[8], &r[9]) != 11)
			return false;
		r[0] = id; r[1] = kind;
		if (put) put(hVessel, r, body);
		return true;
	}
}
