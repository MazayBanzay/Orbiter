// The wave field on a liquid (Titan's seas): an FFT field of linear deep-water gravity waves in a window around the focus,
// fed by the jets (AtmoFX_SetJet) and the parts in the liquid (AtmoFX_SetWake). See FxWaves.cpp, shaders\fx_waves.hlsl.
#pragma once
#include "FxInternal.h"

class WaveField
{
public:
    static const int N = 1024;             // cells per side
    static constexpr double DX = 0.25;     // cell size [m]: a 256 m window

    bool Init(D3D11Client* gc);
    void Frame(D3D11Client* gc, const VECTOR3& camPos, double simDt);    // sources, FFT, exact evolution, slopes
    void Release();
    const FxWaveView& View() const { return view; }   // for the liquid's shader (Liquid.cpp)
    ID3D11UnorderedAccessView* ImpactUAV() const { return impactUAV.Get(); }   // the spray's drops falling back

private:
    struct Tex { ComPtr<ID3D11Texture2D> t; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav; };
    bool MakeTex(ID3D11Device* dev, Tex& t, DXGI_FORMAT f);
    void Clear(ID3D11DeviceContext* ctx);
    Planet* FindLiquid(const VECTOR3& camPos) const;
    std::string HeightRange(ID3D11DeviceContext* ctx);

    ComPtr<ID3D11ComputeShader> csSource, csFFT, csEvolve, csNormals;
    ComPtr<ID3D11Buffer> cb[4];
    Tex stA, stB, slopes, impact;
    ComPtr<ID3D11UnorderedAccessView> impactUAV;
    bool cur = false;
    // the window: a tangent frame fixed to the planet (planet frame), the origin cell, last frame's origin
    Planet* planet = nullptr;
    VECTOR3 anchorP = { 0, 0, 0 }, e1P = { 0, 0, 0 }, e2P = { 0, 0, 0 };
    int i0 = 0, j0 = 0, oldI0 = 0, oldJ0 = 0;
    bool anchored = false;
    struct Body { float x, z, r, d; };
    std::map<ExtState::Key, Body> prevBodies;
    std::map<ExtState::Key, double> impulseSeen;   // the sim time of the last impulse taken per node
    int impulses = 0;
    int jetStat[6] = {};    // for the log: jets seen, thrust 0, stale, not downwards, too high, used
    FxWaveView view;
    double logT = 0;
};
