// Effects on top of the base client (waves on liquids, spray and mist from jets on liquids, ...): owned by the FX work.
// The client calls these four hooks; everything else of the effects lives in Fx*.cpp / Liquid.cpp and their shaders.
#pragma once
#include "Client.h"

void FxInit(D3D11Client* gc);                     // after the device, the shaders and the liquid resources exist
void FxFrame(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj, double simDt);   // per frame, before the scene (compute: waves, particle simulation)
void FxRenderTransparent(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj);    // after the liquid, before the particle streams (spray, mist)
void FxBindLight(D3D11Client* gc);                 // the atmosphere's light and tables again (b4, t8, t9, t13): a pass that needs them binds them itself
void FxRelease();                                 // the render window goes
