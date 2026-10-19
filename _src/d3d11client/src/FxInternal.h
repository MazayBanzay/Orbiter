// Shared between the effects' files (Fx*.cpp, Liquid.cpp): not used by the rest of the client.
#pragma once
#include "Client.h"

extern const std::vector<Planet*>* g_fxPlanets;   // the client's planets (set by CreateLiquid, which has the access)

ComPtr<ID3DBlob> FxCompile(const char* file, const char* entry, const char* target);   // Modules\D3D11Client\<file>

// the atmosphere seen from outside or from high up (FxAtmo.cpp)
bool FxAtmoInit(D3D11Client* gc);
void FxAtmoRender(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj);
void FxAtmoRelease();
