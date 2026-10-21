// AtmoFXPart.h - free particles flown and drawn by the AtmoFX D3D9Client fork (header only, no link dependency): the grains a
// wheel throws, the ice crystals of vented water, the condensate fog where a cold gas jet chills the air. The client flies
// them in the body's gravity and gas (drag, buoyancy, gusts; the source's own jet carries the fines), each down onto the ground
// it left, and draws them lit, as the jets' ground ejecta. The stock client (or any other) has no such export: Available()
// is false and the calls do nothing - keep the module's own fallback (Orbiter's particle streams) for that case.
//
// Call every frame for every source while it runs; kgps = 0 removes it. A source not refreshed for 0.5 s of simulation time
// is dropped by the client (a paused simulation keeps it).
//
//   AtmoFXPart::Emit(GetHandle(), 0, pos, vel, 0.4, 0.5, 0.05, AtmoFXPart::SOIL, AtmoFXPart::DRY, 0.1, 0, 0);
//
// pos: the source point, vessel frame [m]; vel: the mean velocity of what leaves it RELATIVE TO THE VESSEL, vessel frame [m/s]
// (the client adds the vessel's own motion over the ground); cone: half-angle of the spread about vel [rad]; spread: speed
// scatter 0..1 (each particle |vel| * (1 - spread * rnd)); kgps: mass rate of what is SEEN (grains, crystals, condensate)
// [kg/s]; kind: SOIL / ICE / FOG / DROP; ground, wet: for SOIL, the ground's class (as AtmoFXJet) and its moisture 0..1 - the client
// splits the soil by them: WET ground all clods and wet sand, DRY ground its wet share (2 * wet, up to 0.9) as clods and the
// rest dry sand with a little silt, REGOLITH fine and loose; a gas puff (life > 0) lifts only the dry part, never clods;
// life [s]: FOG - its evaporation as it mixes, ICE - its sublimation (0: until it settles), SOIL - a gas puff that threw
// the grains (a sole's squeeze, a blast) and carries the fines along vel, dying out over that time (0: none, e.g. a wheel);
// jetJ [N]: the momentum flux of a gas jet the source blows along vel (a thruster's thrust), which carries the fines in a dense
// gas (0: none, e.g. a wheel).
// DROP: liquid drops thrown out of a sea (Titan's methane: 520 kg/m3, 17 mN/m) - by a wheel fording, a bow wave: kgps the
// mass of the drops, ground / wet / life / jetJ 0. Each drop no bigger than the gas lets it fly whole (We < 12); no
// evaporation (the air is saturated over the sea). Where it falls into the liquid (the body's AtmoFX_LiquidLevel, above the
// ground there) it makes capillary rings, onto dry ground a wet spot that soaks in over a few seconds.
#pragma once
#include <windows.h>
#include "OrbiterAPI.h"

namespace AtmoFXPart {
	enum Kind { SOIL = 0, ICE = 1, FOG = 2, DROP = 3 };
	enum Ground { DRY = 0, WET = 1, LIQUID = 2, REGOLITH = 3, ICEGROUND = 4 };
	typedef void (*EmitFn)(OBJHANDLE, int, const double*, const double*, double, double, double, int, int, double, double, double);
	inline EmitFn Fn()
	{
		static EmitFn fn = nullptr;
		static bool looked = false;
		if (!looked) {
			HMODULE h = GetModuleHandleA("D3D9Client.dll");
			if (h) fn = (EmitFn)GetProcAddress(h, "AtmoFX_Emit");
			looked = (h != nullptr);
		}
		return fn;
	}
	inline bool Available() { return Fn() != nullptr; }
	// the vessel's solids the particles do not pass: boxes in its own axes (centre, half sizes, vessel frame [m]); call when
	// they change (kept until removed - half 0 - or the vessel is deleted). A particle in one is pushed out by the nearest
	// face: a dry grain bounces a little and slides, a clod or a drop sticks and slides off; a sprite there is drawn no bigger
	// than its distance to the box (it does not poke into the hull)
	inline void Box(OBJHANDLE hVessel, int id, const VECTOR3& center, const VECTOR3& half)
	{
		typedef void (*BoxFn)(OBJHANDLE, int, const double*, const double*);
		static BoxFn fn = nullptr; static bool looked = false;
		if (!looked) { HMODULE h = GetModuleHandleA("D3D9Client.dll"); if (h) fn = (BoxFn)GetProcAddress(h, "AtmoFX_Box"); looked = (h != nullptr); }
		if (fn) { const double c[3] = { center.x, center.y, center.z }, hh[3] = { half.x, half.y, half.z }; fn(hVessel, id, c, hh); }
	}
	inline void Emit(OBJHANDLE hVessel, int id, const VECTOR3& pos, const VECTOR3& vel, double cone, double spread, double kgps,
		int kind, int ground, double wet, double life, double jetJ)
	{
		if (EmitFn f = Fn()) {
			const double p[3] = { pos.x, pos.y, pos.z }, v[3] = { vel.x, vel.y, vel.z };
			f(hVessel, id, p, v, cone, spread, kgps, kind, ground, wet, life, jetJ);
		}
	}
}
