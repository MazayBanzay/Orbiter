// Wakes on liquids (Wake.cpp): the modules' wake nodes as wave sources for the liquid shader.
#pragma once
#include "Client.h"

// one Kelvin wake for the shader (= shaders\liquid.hlsl): a: wavelength, slope amplitude, radius; b: centre on the level
// from the camera; f: the direction of motion
struct WakeSrc { XMFLOAT4 a, b, f; };
static const int WAKE_MAX = 32;

// the wave field's window for the liquid shader: its corner (cell 0, 0) from the camera, axes, cell size, the slopes
// (gx, gy per metre, h) texture
struct WakeFieldView { bool on = false; VECTOR3 origin = { 0, 0, 0 }, e1 = { 0, 0, 0 }, e2 = { 0, 0, 0 }; double dx = 0; int n = 0; ID3D11ShaderResourceView* srv = nullptr; };
bool WakeField(D3D11Client* c, OBJHANDLE body, double levelRadius, const VECTOR3& cam, double simDt, WakeFieldView& view);

void WakeSet(OBJHANDLE hv, int id, const double* pos, double radius, double strength);   // AtmoFX_SetWake
void WakeRipple(OBJHANDLE body, const VECTOR3& gpos, double radius, double amp);         // a drop, a splash
void WakeForget(OBJHANDLE hv);       // a vessel deleted: its nodes go (their hollows are not filled: the vessel is gone)
int WakeSources(OBJHANDLE body, double levelRadius, const VECTOR3& cam, WakeSrc* out, int maxN);
ComPtr<ID3D11ShaderResourceView> WakeLoadKelvin(ID3D11Device* dev);
