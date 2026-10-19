// Wakes on liquids (Wake.cpp): the modules' wake nodes as wave sources for the liquid shader.
#pragma once
#include "Client.h"

// one source for the shader (= struct in shaders\liquid.hlsl): a: wavelength, slope amplitude, radius, time / front;
// b: centre on the level from the camera, w the kind (0 Kelvin wake, 1 rings around a part, 2 a packet after an impulse);
// f: the direction of motion (Kelvin)
struct WakeSrc { XMFLOAT4 a, b, f; };
static const int WAKE_MAX = 32;

void WakeSet(OBJHANDLE hv, int id, const double* pos, double radius, double strength);   // AtmoFX_SetWake
void WakeRipple(OBJHANDLE body, const VECTOR3& gpos, double radius, double amp);         // a drop, a splash
int WakeSources(OBJHANDLE body, double levelRadius, const VECTOR3& cam, WakeSrc* out, int maxN);
ComPtr<ID3D11ShaderResourceView> WakeLoadKelvin(ID3D11Device* dev);
