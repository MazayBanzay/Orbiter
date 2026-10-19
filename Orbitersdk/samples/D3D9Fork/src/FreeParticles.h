// AtmoFX fork: free particles from module sources - the grains a wheel throws, the ice crystals of vented water, the
// condensate fog where a cold gas jet chills the air (methane on Titan, water on Earth, CO2 on Mars). Physics as the jets'
// ground ejecta (JetEjecta): the body's gravity, the gas drag (Schiller-Naumann) with buoyancy, turbulent gusts, each grain
// down onto the ground it left; plus the free turbulent jet a source may blow (its momentum flux), which carries the fines.
// Nothing in vacuum but gravity. Drawn as lit, sorted sprites (shaders\FreeParticles.fx): puffs, clods, drops, wet spots.
// Module side: Orbitersdk/samples/AtmoFX/AtmoFXPart.h.
// Also the marks the modules lay on the ground - ruts, pits, wet ruts (AtmoFX_Mark; module side AtmoFXMark.h): on the drawn
// terrain, their depth shown by the light.
#pragma once
#include "D3D9Client.h"

class Scene;

namespace FreeParticles {
	void Render(LPDIRECT3DDEVICE9 dev, Scene* scn);
	void Release();
}

extern "C" __declspec(dllexport) void AtmoFX_Mark(OBJHANDLE hVessel, int id, const double* pos, const double* dir, double length,
	double width, double depth, int kind, double life);
extern "C" __declspec(dllexport) void AtmoFX_Emit(OBJHANDLE hVessel, int id, const double* pos, const double* vel, double cone,
	double spread, double kgps, int kind, int ground, double wet, double life, double jetJ);
