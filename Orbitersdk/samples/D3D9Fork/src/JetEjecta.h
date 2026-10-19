// AtmoFX fork: ground ejecta under rocket jets (the flight module and anything else that calls the export).
// Physics per body: the jet's stagnation pressure on the ground (spreading ~12 deg in dense gas, wide in vacuum), the grain
// threshold (Shao & Lu 2000) in the body's gas, the grains flown with the body's gravity and gas drag (Schiller-Naumann)
// in a radial wall jet; no drag in vacuum (ballistic sheets, as under the Apollo landers). Particles live in the planet's
// rotating frame, so they stay with the ground. Drawn as lit soft sprites, sorted, before the heat shimmer.
// Module side: Orbitersdk/samples/AtmoFX/AtmoFXJet.h.
#pragma once
#include "D3D9Client.h"

class Scene;

namespace JetEjecta {
	void Render(LPDIRECT3DDEVICE9 dev, Scene* scn);
	void Release();
}

extern "C" __declspec(dllexport) void AtmoFX_SetJet(OBJHANDLE hVessel, int id, const double* exitPos, const double* jetDir,
	double thrust, double ve, double exitT, int ground, double wet, double rExit);
