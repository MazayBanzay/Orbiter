// Shared between Exhaust.cpp and GpuParticles.cpp (not used by the rest of the client).
#pragma once
#include "Exhaust.h"
#include "FxInternal.h"
#include <array>

extern XMMATRIX g_exViewProj;      // this frame's camera-centred global -> clip (with the TAA jitter), Exhaust.cpp

// ground classes of the AtmoFX API (AtmoFXJet.h / AtmoFXPart.h)
enum ExGround { EXG_DRY = 0, EXG_WET = 1, EXG_LIQUID = 2, EXG_REGOLITH = 3, EXG_ICE = 4 };

// the body the effects live on this frame (the focus's surface reference) and the gas there
struct ExScene
{
    VECTOR3 cp = { 0, 0, 0 };          // camera, global
    double simt = 0, dt = 0;           // simulation time, this frame's step (0 when paused)
    double kE = 1;                     // radiance scale: pi / (the Sun's solid angle here) - a blackbody's B(T) relative to
                                       // the Sun's, times this, is the radiance in the client's units (the Sun's irradiance = 1)
    Planet* planet = nullptr;          // the client's planet (atmosphere and liquid config), may be null
    OBJHANDLE hBody = nullptr;
    VECTOR3 pp = { 0, 0, 0 };          // its centre, global
    MATRIX3 Rp;                        // planet frame -> global
    double R = 0, GM = 0;
    // the gas at the focus (Orbiter's atmosphere model)
    double rhoG = 0, Tg = 0, pa = 0, mu = 1.7e-5, Rgas = 296.8;
    bool dense = false;                // rhoG > 1e-3 kg/m3: drag, gusts, gas flows; else only gravity
    double Rliq = 0;                   // the liquid's surface radius (0: none)
    double rhoL = 520, sigma = 0.017;  // the liquid (Titan's methane / ethane)
    bool titan = false;
    // the particles' frame: a point on the mean sphere under the focus, axes e1, up, e2 fixed to the planet
    bool anchored = false;
    VECTOR3 aP = { 0, 0, 0 }, a1 = { 1, 0, 0 }, aU = { 0, 1, 0 }, a2 = { 0, 0, 1 };   // planet frame
    // local (anchor) <-> camera-centred global
    VECTOR3 ToLocal(const VECTOR3& g) const { VECTOR3 q = tmul(Rp, g - pp) - aP; return _V(dotp(q, a1), dotp(q, aU), dotp(q, a2)); }
    VECTOR3 DirToLocal(const VECTOR3& d) const { VECTOR3 q = tmul(Rp, d); return _V(dotp(q, a1), dotp(q, aU), dotp(q, a2)); }
    VECTOR3 LocalToGlobal(const VECTOR3& l) const { return mul(Rp, aP + a1 * l.x + aU * l.y + a2 * l.z) + pp; }
};

// one jet this frame (AtmoFX_SetJet), in the global frame, with its derived gas dynamics
struct ExJet
{
    OBJHANDLE h = nullptr; int id = 0;
    VECTOR3 eg, dg;                    // exit, direction of the gas (global)
    double thrust = 0, ve = 0, T = 0, wet = 0, rExit = 0; int ground = 0;
    VECTOR3 vg = { 0, 0, 0 };          // the vessel's velocity over the ground (planet frame)
    // derived (Exhaust.cpp: JetGas)
    double gamma = 1.3, Rs = 3800, mdot = 0, rhoE = 0, pE = 0, Me = 1, n = 1e9, Lc = 1, rj = 0, cell = 1, shock = 1, vacW = 1;
    double hitH = -1;                  // height of the exit over the surface along the vertical (< 0: no surface reached)
    double yH2 = 0;                    // the exhaust's free hydrogen (mass fraction; the rest water)
    // a hydrogen exhaust (H2 + H2O, gas constant > 450 J/kg/K): no soot, no Planck light - it glows by the blue continuum
    // (~ [OH]^2, H2Chem.h): OH in the mix with the ambient gas over the exhaust's share c (u = c / (c + fsU)), at the exit
    bool h2 = false;
    double fsU = 0.03;                 // the table's scale: the stoichiometric share (with oxygen), else 0.03
    float ohMix[32] = {};              // [OH] [1e20 m^-3] over u (H2Chem::MixTable)
    float tMix[32] = {};               // the mix's temperature [K] over u
    double glowZ = 0;                  // down to where the axis' mix still glows (OH^2 > 1 % of its peak) [m]
    double ohE = 0;                    // [OH] at the exit [1e20 m^-3] (the vacuum source flow carries it, frozen)
    double flameZ = 0;                 // where the axis reaches the stoichiometric mix [m] (the box's reach; 0: no flame)
    double cond = 0, condZ = 0;        // the share of its water that condenses (Schmidt-Appleman) and where it is emitted [m]
    bool liquidBelow = false;
};

// a hot node (AtmoFX_SetHeatSource), global
struct ExHeat { VECTOR3 g; double r = 0, dT = 0, Tair = 0; OBJHANDLE h = nullptr; };

// the GPU particle system (GpuParticles.cpp)
// spray sheets off wheels in a liquid (Spray.cpp): fed by the DROP sources, drawn after the particles
void SprayFeed(const ExScene& sc, uintptr_t key, const VECTOR3& pLocal, const VECTOR3& vLocal, double kgps);
void SprayRender(D3D11Client* gc, const ExScene& sc, ID3D11ShaderResourceView* depth);
// the vessels' own exhausts (AddExhaust), volumetric (VesselExhaust.cpp): in the exhaust pass, t5/s3 bound
void RenderVesselExhausts(D3D11Client* gc, const VECTOR3& cp);
void SprayRelease();             // with the device (Spray.cpp)
// the engines' smoke and condensate as a 3D flow (Smoke.cpp): one source per engine this frame
struct SmokeSrc
{
    VECTOR3 eg, dg;                    // exit and the gas's direction (global)
    double rExit = 0.04;
    double kJ = 0;                     // sqrt(momentum flux / the air's density) [m2/s]: the free jet's 7 kJ / z
    double mVis = 0;                   // visible mass formed [kg/s] (condensate; later soot)
    double zFull = 1;                  // it forms over 0..zFull with the share (z / zFull)^2 [m]
    double inject = 1;                 // the length the jet's own flow is imposed over [m] (then the solver carries it)
    double Tex = 0;                    // the mix's excess temperature there [K] (buoyancy)
    double decay = 0;                  // the visible mass's loss [1/s] (evaporation; 0 on Titan)
};
bool SmokeEnabled();             // Exhaust.cfg Smoke (default on)
void SmokeUpdate(D3D11Client* gc, const ExScene& sc, const std::vector<SmokeSrc>& src);   // after the particles' update
void SmokeRender(D3D11Client* gc);                     // in the exhaust pass (t5 depth, s3 bound)
bool SmokeLive();                // the flow ran this frame
void SmokeShimmer(D3D11Client* gc);                    // its hot gas's refraction into the bound offset target (additive)
void SmokeRelease();             // with the device
void VesselExhaustRelease();     // with the device (VesselExhaust.cpp)

class GpuParticles
{
public:
    bool Init(D3D11Client* gc);
    void Release();
    // spawning on the CPU (the jets' ejecta and spray, the free sources), then the GPU's emit and flight
    void Update(D3D11Client* gc, ExScene& sc, const std::vector<ExJet>& jets);
    // drawn into the bound HDR target (no DSV: soft against the depth texture)
    void Render(D3D11Client* gc, const ExScene& sc, ID3D11ShaderResourceView* depth);
    void Emit(OBJHANDLE h, int id, const double* pos, const double* vel, double cone, double spread, double kgps,
              int kind, int ground, double wet, double life, double jetJ);
    void Box(OBJHANDLE h, int id, const double* c, const double* half);
    UINT AliveLast() const { return aliveRead; }
    UINT SpawnedLast() const { return spawnedLast; }

    // one particle on the GPU (particles_gpu.hlsl: struct Part), 96 bytes
    struct GPart {
        XMFLOAT3 p; float age;
        XMFLOAT3 v; float life;
        XMFLOAT3 ug0; float tg;      // a puff's gas velocity at birth and its decay time
        XMFLOAT3 gust; float gustT;
        float d, rhoP, s0, a0;       // grain diameter [m], density [kg/m3], sprite half-size [m], opacity
        float gy; UINT flags; float seed; float spare;   // the surface's height under it (local frame), look/albedo/state
    };

private:
    struct Source {                  // AtmoFX_Emit
        int kind = 0, ground = 0; double pos[3], vel[3], cone = 0, spread = 0, kgps = 0, wet = 0, life = 0, jetJ = 0;
        double t = 0; double acc[8] = {};
    };
    struct BoxDef { double c[3], h[3]; };
    struct JetAcc { double acc[8] = {}; double t = 0; };
    std::mutex mx;                   // the exports may come from module threads? (they come from the main thread; cheap anyway)
    std::map<ExtState::Key, Source> sources;
    std::map<ExtState::Key, BoxDef> boxes;
    std::map<ExtState::Key, JetAcc> jetAcc;
    std::vector<GPart> spawn;
    unsigned rng = 2463534242u;
    double Rnd();                    // 0..1
    double U(double a, double b) { return a + (b - a) * Rnd(); }
    void SpawnJet(ExScene& sc, const ExJet& j, JetAcc& acc, std::vector<XMFLOAT4>& impacts);
    void SpawnCrowns(ExScene& sc);   // crown splashes of impacts (ParticlesCrown)
    void SpawnSource(ExScene& sc, OBJHANDLE h, Source& s, std::vector<XMFLOAT4>& freeJets);
    void Push(const ExScene& sc, const VECTOR3& pLocal, const VECTOR3& vLocal, double d, double rhoP, double s0, double a0,
              double life, double gyLocal, UINT flags, const VECTOR3& ug0 = _V(0, 0, 0), double tg = 1);

    ComPtr<ID3D11Buffer> pool, dead, alive, newBuf, args, cbSim, cbCount, cbDraw, countRead[3];
    ComPtr<ID3D11UnorderedAccessView> poolUAV, deadUAV, aliveUAV;
    ComPtr<ID3D11ShaderResourceView> poolSRV, aliveSRV, newSRV;
    ComPtr<ID3D11ComputeShader> csInit, csEmit, csSim;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11BlendState> bsPremul;
    ComPtr<ID3D11SamplerState> samp;
    bool needInit = true;
    UINT frame = 0, aliveRead = 0, spawnedLast = 0;
    double simTime = 0;
    std::vector<XMFLOAT4> impactsCB, freeCB;
    std::vector<XMFLOAT4> boxCB;
    UINT nImp = 0, nFree = 0, nBox = 0;
    void UpdateBoxes(const ExScene& sc);
    void AnchorAt(ExScene& sc);
    bool anch = false; OBJHANDLE aBody = nullptr;
    VECTOR3 aP = { 0, 0, 0 }, a1 = { 1, 0, 0 }, aU = { 0, 1, 0 }, a2 = { 0, 0, 1 };
    double logT = 0;
    bool skip = false;              // no particles on this body (Titan, for now)
public:
    static const UINT POOL = 65536, NEW_MAX = 4096;
};
