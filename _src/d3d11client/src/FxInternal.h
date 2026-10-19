// Shared between the effects' files (Fx*.cpp, Liquid.cpp): not used by the rest of the client.
#pragma once
#include "Client.h"

extern const std::vector<Planet*>* g_fxPlanets;   // the client's planets (set by CreateLiquid, which has the access)

ComPtr<ID3DBlob> FxCompile(const char* file, const char* entry, const char* target);   // Modules\D3D11Client\<file>

// the wave field as the liquid's shader needs it (FxWaves.cpp fills it; Liquid.cpp reads it through FxWaves())
struct FxWaveView
{
    bool on = false;
    Planet* planet = nullptr;
    VECTOR3 anchor = { 0, 0, 0 }, e1 = { 0, 0, 0 }, e2 = { 0, 0, 0 };   // camera-centred global: the frame's origin and axes
    double dx = 0.25;                     // cell size [m]
    int i0 = 0, j0 = 0, n = 1024;         // the window's first cell, cells per side
    ID3D11ShaderResourceView* slopes = nullptr;   // gx, gz, h, laplacian per cell (periodic)
};
const FxWaveView& FxWaves();
