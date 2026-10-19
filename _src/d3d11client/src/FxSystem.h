// The effects as one object: the client's four hooks (Fx.h) forward to it; it owns each effect and calls them in order.
#pragma once
#include "FxAtmo.h"
#include "FxWaves.h"

class FxSystem
{
public:
    void Init(D3D11Client* gc);
    void Frame(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj, double simDt);
    void RenderTransparent(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj);
    void Release();
    const WaveField& Waves() const { return waves; }

private:
    Atmosphere atmo;
    WaveField waves;
    bool atmoOk = false, wavesOk = false;
};
