// The FX atmosphere: physically based sky, aerial perspective and the air seen from space for every planet with air,
// from the ground to orbit (Hillaire 2020 with Bruneton's transmittance parametrisation). See FxAtmo.cpp.
#pragma once
#include "FxInternal.h"

// the medium of one planet's air: Config\D3D11Client\<planet>.atmo.cfg (with sources), or clear gas from the density
struct AtmoMedia
{
    double top = 0;                                   // altitude of the top of the air [m]
    VECTOR3 ray = { 0, 0, 0 };                        // Rayleigh scattering at the ground [1/m]
    double rayH = 8000, rayBreak = 0, rayH2 = 8000;   // its scale height; above rayBreak (> 0) the scale height rayH2
    VECTOR3 mieExt = { 0, 0, 0 }, mieSSA = { 0.9, 0.9, 0.9 }, mieG = { 0.7, 0.7, 0.7 };   // aerosol / dust / cloud
    double mieH = 1000, mieBase = -1, mieTop = -1;    // its scale height; a constant-density layer from base to top
    VECTOR3 absExt = { 0, 0, 0 };                     // absorber (ozone) extinction at its peak [1/m]
    double absCentre = 0, absHalf = 0;                // tent profile: peak altitude, half width
    VECTOR3 albedo = { 0.1, 0.1, 0.1 };               // ground albedo
    bool fromFile = false;

    bool Load(const Planet* p);        // the file; false: there is none
    bool ClearGas(const Planet* p);    // Earth's Rayleigh scaled by the density; false: no air worth drawing
    double RayDensity(double h) const;
    double MieDensity(double h) const;
};

// the shaders that build the tables (shared by all planets)
struct AtmoTableShaders { ComPtr<ID3D11ComputeShader> trans, ms, irr; };

// one planet's air on the GPU: its tables (built once) and its per-frame constants
class PlanetAtmosphere
{
public:
    PlanetAtmosphere(Planet* p, const AtmoMedia& m) : planet(p), media(m) {}
    bool Create(ID3D11Device* dev);
    void Update(ID3D11DeviceContext* ctx, const VECTOR3& camPos, const VECTOR3& sunPos);   // this frame's constants
    void BuildTables(ID3D11DeviceContext* ctx, const AtmoTableShaders& s);
    float StarVisibility(const VECTOR3& camPos, const VECTOR3& sunPos) const;           // the stars through this air
    VECTOR3 LightAt(const VECTOR3& camPos, const VECTOR3& sunPos, bool noon) const;     // on a horizontal surface at the camera (noon: the Sun overhead)
    double TopRadius() const { return planet->R + media.top; }

    Planet* const planet;
    const AtmoMedia media;
    bool tablesReady = false;
    double opaqueAlt = 0;                          // below it the air is opaque seen from above (0: never)
    ComPtr<ID3D11Buffer> cb;                       // cbuffer FxAtmoMedia (b4)
    ComPtr<ID3D11ShaderResourceView> lutSRV;       // transmittance + sky irradiance (t8)
    ComPtr<ID3D11ShaderResourceView> msSRV;        // multiple scattering (t9)

private:
    void ReadTables(ID3D11DeviceContext* ctx);
    std::vector<XMFLOAT4> cpuTrans, cpuIrr;        // CPU copies of the tables
    ComPtr<ID3D11Texture2D> trans, ms, irr, lut;
    ComPtr<ID3D11UnorderedAccessView> transUAV, msUAV, irrUAV;
    ComPtr<ID3D11ShaderResourceView> transSRV;
};

// all planets' air: the tables on first use, the light for the client's shaders, the sky pass
class Atmosphere
{
public:
    bool Init(D3D11Client* gc);
    void Frame(D3D11Client* gc, const VECTOR3& camPos);    // before the scene: tables, constants, the light (b4, t8, t9)
    void Render(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj);   // after the opaque scene
    void Release();

private:
    PlanetAtmosphere* Get(D3D11Client* gc, Planet* p);   // nullptr: no air
    void BindLight(ID3D11DeviceContext* ctx);              // the current planet's medium and tables for the client
    void LogDepth(D3D11Client* gc, float nearPlane);       // a check in Orbiter.log: the scene depth this pass reads
    void UpdateExposure(const VECTOR3& camPos);            // the eye's adaptation to the light at the camera

    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    AtmoTableShaders tables;
    ComPtr<ID3D11Buffer> cbPass, cbSpace;
    ComPtr<ID3D11BlendState> bsDual;
    std::map<Planet*, std::unique_ptr<PlanetAtmosphere>> planets;
    PlanetAtmosphere* current = nullptr;    // the planet whose light the client's shaders get this frame
    VECTOR3 sunPos = { 0, 0, 0 };
    double exposure = 1;
    VECTOR3 white = { 1, 1, 1 };     // the colour balance (UpdateExposure)
    LARGE_INTEGER lastT = {};
};
