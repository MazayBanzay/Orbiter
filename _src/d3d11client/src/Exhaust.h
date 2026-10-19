// Engine, heat and particle effects of the D3D11 client (native DX11, physically based), see Exhaust.cpp:
//   - the glow of hot metal (AtmoFX_SetHeatSource): blackbody emission of the scene's surfaces round each hot node;
//   - volumetric exhaust plumes (AtmoFX_SetJet): ray-marched per jet, density and temperature from the exit conditions
//     and the ambient pressure (wide plume in vacuum, shock cells - Mach diamonds - in thick air);
//   - GPU particles (GpuParticles.cpp): ground ejecta under the jets, spray and mist over liquids, splashes, the modules'
//     free particles (AtmoFX_Emit) against their boxes (AtmoFX_Box);
//   - heat shimmer: a screen-space refraction offset behind hot gas and hot nodes, before the tone mapping.
// The client calls the hooks below; the effects keep to their own files, shaders and register slots (b5, b6, t4..t6, s3).
#pragma once
#include "Client.h"

void ExhaustInit(D3D11Client* gc);          // after FxInit (the device and the shaders' folder exist)
// after FxRenderTransparent (the transparent vessels and the FX atmosphere are in the HDR scene), before RenderAr and the
// particle streams; the scene's RTV and DSV are bound and are bound again on return
void ExhaustRender(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj, double simDt);
void ExhaustRelease();                      // with FxRelease

// AtmoFX exports (Ext.cpp forwards its stubs here; meaning as in Orbitersdk\samples\AtmoFX\AtmoFXPart.h)
void ExhaustEmit(OBJHANDLE h, int id, const double* pos, const double* vel, double cone, double spread, double kgps,
                 int kind, int ground, double wet, double life, double jetJ);
void ExhaustBox(OBJHANDLE h, int id, const double* centre, const double* half);
