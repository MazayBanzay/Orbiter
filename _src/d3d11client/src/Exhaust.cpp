// Engine, heat and particle effects of the D3D11 client (see Exhaust.h). Per frame, in ExhaustRender (after the
// transparent vessels and the FX atmosphere, before the particle streams):
//   1. the body and its gas at the focus, the jets (AtmoFX_SetJet, refreshed within 0.5 s) and hot nodes in global terms;
//   2. the GPU particles: the newborn uploaded, emit + flight in compute (GpuParticles.cpp);
//   3. the scene depth (single-sampled), the glow of hot metal (additive);
//   4. heat shimmer: the scene copied, the hot columns' offsets drawn, the scene re-read through them;
//   5. the plumes (dual-source: + emission, x transmittance), the particles (premultiplied alpha), soft against the depth.
// Switches and constants: Config\D3D11Client\Exhaust.cfg. Shaders: exhaust_*.hlsl, particles_gpu.hlsl.
#include "ExhaustInternal.h"
#include "H2Chem.h"

XMMATRIX g_exViewProj;
bool g_exH2Flame = true;           // the hydrogen jets drawn by their chemistry here (VesselExhaust.cpp skips their 0x100 art)

namespace {

const double GGRAV = 6.67430e-11, PI = 3.14159265358979;
const double K_REFR = 2.3e-4;      // (n - 1) / rho for N2, air, CO2 [m3/kg]

struct Cfg
{
    bool glow = true, plumes = true, particles = true, shimmer = true;
    double kabs = 20;              // the exhaust's visible absorption per kg [m2/kg]: heavy (hydrocarbon) exhaust - soot, radicals
    bool h2Flame = true;           // hydrogen jets by their chemistry (H2Flame): the blue continuum ~ [OH]^2 of the mix with the
                                   // ambient gas, the afterburning where it has oxygen, the water's condensate (Schmidt-Appleman);
                                   // off: the jet pack's 0x100 art volume (VesselExhaust.cpp) as in the D3D9 fork
    double rh = 0.6;               // Earth's air: relative humidity for the condensation trail (Orbiter has no humidity)
    double condMin = 0.02;         // the least share of the plume's water above saturation that makes a trail
    double emis = 0.8;             // the hot metal's emissivity
    int steps = 40;                // the plume's ray-march steps
    struct TJet { double p[3], d[3], thrust, ve, T, rExit, wet; int ground; };
    struct THeat { double p[3], r, dT; };
    std::vector<TJet> testJets;
    std::vector<THeat> testHeat;
    void Load()
    {
        Read("Config\\D3D11Client\\Exhaust.cfg", false);
        // test jets / hot nodes only from the file the exhaust tests name (never from the shared cfg: other sessions'
        // test runs would get them)
        if (const char* t = getenv("D3D11CLIENT_EXHAUST_TEST")) Read(t, true);
    }
    void Read(const char* path, bool test)
    {
        FILE* f = fopen(path, "r");
        if (!f) return;
        char line[512];
        while (fgets(line, sizeof line, f)) {
            char* sc = strchr(line, ';'); if (sc) *sc = 0;
            char key[64]; char rest[400] = "";
            if (sscanf(line, " %63[A-Za-z0-9] = %399[^\n]", key, rest) != 2) continue;
            double v = atof(rest);
            if (!strcmp(key, "Glow")) glow = v != 0;
            else if (!strcmp(key, "Plumes")) plumes = v != 0;
            else if (!strcmp(key, "Particles")) particles = v != 0;
            else if (!strcmp(key, "Shimmer")) shimmer = v != 0;
            else if (!strcmp(key, "Kabs")) kabs = std::max(0.0, v);
            else if (!strcmp(key, "H2Flame")) h2Flame = v != 0;
            else if (!strcmp(key, "Humidity")) rh = std::clamp(v, 0.0, 1.0);
            else if (!strcmp(key, "CondMin")) condMin = std::clamp(v, 0.0, 1.0);
            else if (!strcmp(key, "Emissivity")) emis = std::clamp(v, 0.0, 1.0);
            else if (!strcmp(key, "Steps")) steps = std::clamp((int)v, 8, 128);
            else if (!strcmp(key, "TestJet") && test) {
                TJet j = {};
                if (sscanf(rest, "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %d %lf", &j.p[0], &j.p[1], &j.p[2], &j.d[0], &j.d[1], &j.d[2],
                           &j.thrust, &j.ve, &j.T, &j.rExit, &j.ground, &j.wet) >= 10) testJets.push_back(j);
            } else if (!strcmp(key, "TestHeat") && test) {
                THeat h = {};
                if (sscanf(rest, "%lf %lf %lf %lf %lf", &h.p[0], &h.p[1], &h.p[2], &h.r, &h.dT) == 5) testHeat.push_back(h);
            }
        }
        fclose(f);
    }
};

struct GlowCB { XMFLOAT4 node[96]; XMFLOAT4 glow; XMFLOAT4X4 vp; };
struct PlumeCB { XMFLOAT4 e, x, y, z, box, gas, jet, vac, air, h2, kc, oh[8]; XMFLOAT4X4 vp; };
struct ShimCB { XMFLOAT4 col[96]; XMFLOAT4 misc; XMFLOAT4X4 vp; };

XMFLOAT4 F4(const VECTOR3& v, double w) { return { (float)v.x, (float)v.y, (float)v.z, (float)w }; }
VECTOR3 Unit(const VECTOR3& v) { double l = length(v); return l > 0 ? v / l : _V(0, 0, 1); }

// ------------------------------------------------------------------------------------------ the blackbody table
// Planck's spectral radiance integrated against the CIE 1931 observer (Wyman, Sloan & Shirley 2013 fit) to linear sRGB,
// per channel relative to the Sun's (5772 K): ln of it over log T, 200..20000 K
double Cie(double x, double m, double s1, double s2) { double s = x < m ? s1 : s2; double t = (x - m) / s; return exp(-0.5 * t * t); }
void BlackbodyRGB(double T, double out[3])
{
    double X = 0, Y = 0, Z = 0;
    for (double l = 380; l <= 780; l += 2.5) {
        double x = 1.056 * Cie(l, 599.8, 37.9, 31.0) + 0.362 * Cie(l, 442.0, 16.0, 26.7) - 0.065 * Cie(l, 501.1, 20.4, 26.2);
        double y = 0.821 * Cie(l, 568.8, 46.9, 40.5) + 0.286 * Cie(l, 530.9, 16.3, 31.1);
        double z = 1.217 * Cie(l, 437.0, 11.8, 36.0) + 0.681 * Cie(l, 459.0, 26.0, 13.8);
        double lm = l * 1e-9;
        double e = 0.0143877735 / (lm * T);
        double B = e > 700 ? 0 : 1.191042953e-16 / pow(lm, 5) / (exp(e) - 1);
        X += x * B; Y += y * B; Z += z * B;
    }
    out[0] = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
    out[1] = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
    out[2] = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
    for (int i = 0; i < 3; i++) out[i] = std::max(out[i], Y * 1e-4);   // a deep red's negative channels: a trace
}

// the gas dynamics of a jet from its exit conditions and the ambient pressure (see exhaust_plume.hlsl)
void JetGas(ExJet& j, double pa, double rhoA)
{
    const double g = 1.3;
    j.gamma = g;
    const double ve = std::max(j.ve, 100.0), Te = std::max(j.T, 300.0), re = std::max(j.rExit, 0.005);
    // the gas constant of the exhaust from ve and Te (an expansion from ~2 Te): hydrogen ~4000, steam/CO2 300..600 J/kg/K
    j.Rs = std::clamp(ve * ve * (g - 1) / (2 * g * Te), 8314.0 / 44, 8314.0 / 2);
    j.mdot = j.thrust / ve;
    j.rhoE = j.mdot / (ve * PI * re * re);
    j.pE = j.rhoE * j.Rs * Te;
    j.Me = std::max(1.05, ve / sqrt(g * j.Rs * Te));
    j.n = pa > 1e-3 ? j.pE / pa : 1e12;
    j.rj = re * pow(std::clamp(j.n, 0.2, 30.0), 1 / (2 * g));
    j.cell = 1.306 * 2 * j.rj * sqrt(j.Me * j.Me - 1);
    j.Lc = 2 * j.rj * (4.2 + 1.1 * j.Me * j.Me);
    // the normal shock's static temperature ratio at Me, weakened as n -> 1 (a matched jet has no shocks)
    const double M2 = j.Me * j.Me;
    const double t21 = (2 * g * M2 - (g - 1)) * ((g - 1) * M2 + 2) / ((g + 1) * (g + 1) * M2);
    j.shock = (t21 - 1) * 0.6 * std::min(1.0, fabs(log(std::max(j.n, 1e-6))));
    const double ln = log10(std::max(j.n, 1e-6));
    double w = std::clamp((ln - 1.5) / 1.5, 0.0, 1.0); j.vacW = w * w * (3 - 2 * w);
    // a light jet in a heavy gas mixes out in proportion to its effective diameter D sqrt(rho_jet / rho_air)
    // (Thring & Newby 1953; Chen & Rodi): hot hydrogen (0.013 kg/m3 at 1.5 bar, 3000 K) in Titan's cold nitrogen
    // (5.4 kg/m3) - x0.05: its hot core is centimetres long, not metres
    if (rhoA > 1e-3 && pa > 1e-3) {
        const double rhoJ = pa / (j.Rs * Te);
        j.Lc *= std::clamp(sqrt(rhoJ / rhoA), 0.03, 1.0);
    }
}

double PrandtlMeyer(double M, double g)
{
    double a = sqrt((g + 1) / (g - 1)), m = sqrt(std::max(M * M - 1, 0.0));
    return a * atan(m / a) - atan(m);
}

class ExhaustFx
{
public:
    bool Init(D3D11Client* gc);
    void Render(D3D11Client* gc, const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj, double simDt);
    void Release();
    GpuParticles parts;
private:
    Cfg cfg;
    bool ok = false, partsOk = false;
    ComPtr<ID3D11VertexShader> vsGlow, vsPlume, vsProxy, vsFull;
    ComPtr<ID3D11PixelShader> psGlow, psPlume, psProxy, psComp;
    ComPtr<ID3D11Buffer> cbGlow, cbPlume, cbShim;
    ComPtr<ID3D11BlendState> bsAdd, bsDual, bsOpaque;
    ComPtr<ID3D11SamplerState> sLin;
    ComPtr<ID3D11ShaderResourceView> bbSRV;
    ComPtr<ID3D11Texture2D> copyTex, offTex;
    ComPtr<ID3D11ShaderResourceView> copySRV, offSRV;
    ComPtr<ID3D11RenderTargetView> offRTV;
    UINT tw = 0, th = 0;
    ComPtr<ID3D11Query> qDis[4], qT0[4], qT1[4];
    double gpuMs = 0; UINT qFrame = 0;
    LARGE_INTEGER tLog = {};
    double fpsAcc = 0; int fpsN = 0;
    std::vector<ExJet> jets;
    std::vector<ExHeat> heat;
    struct Exit { VECTOR3 eg, dg; double rExit; };
    std::map<OBJHANDLE, std::vector<Exit>> lastExitLocal, lastExit;   // the jets' exits seen per vessel (its frame / global)
    std::set<std::pair<OBJHANDLE, int>> steamOn, steamPrev;           // the condensate's emitters this frame / the last
    // the hydrogen jets' mixing tables (H2Chem::Mix: ~1 ms each) per jet, made again when the gas or the exhaust changes
    struct MixKey { double yH2 = -1, Te = 0, ve = 0, pa = 0, Ta = 0, yO2 = -1; };
    struct MixCache { MixKey k; H2Chem::MixTable t; };
    std::map<std::pair<OBJHANDLE, int>, MixCache> mixCache;
    double kc[3] = {}, kcLum = 0;    // the blue continuum per [OH]^2 path (H2Chem::ContinuumRGB)
    void H2Jet(ExJet& j, VESSEL* v, const VECTOR3& eL, const VECTOR3& dL, const ExScene& sc);
    std::vector<SmokeSrc> smoke;       // the 3D flow's sources this frame (Smoke.cpp)
    bool Scene(ExScene& sc, const VECTOR3& cp, double simDt);
    void Collect(const ExScene& sc);
    void Targets(D3D11Client* gc);
    void DrawGlow(D3D11Client* gc, const ExScene& sc);
    bool DrawShimmer(D3D11Client* gc, const ExScene& sc, const XMMATRIX& proj, ID3D11RenderTargetView* rtv);
    void DrawPlumes(D3D11Client* gc, const ExScene& sc);
    void Log(const ExScene& sc);
};

bool ExhaustFx::Init(D3D11Client* gc)
{
    cfg = Cfg(); cfg.Load();
    ID3D11Device* dev = gc->Dev();
    auto vsh = [&](const char* f, const char* e, ComPtr<ID3D11VertexShader>& s) { auto b = FxCompile(f, e, "vs_5_0"); return b && SUCCEEDED(dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s)); };
    auto psh = [&](const char* f, const char* e, ComPtr<ID3D11PixelShader>& s) { auto b = FxCompile(f, e, "ps_5_0"); return b && SUCCEEDED(dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s)); };
    if (!vsh("exhaust_glow.hlsl", "VS", vsGlow) || !psh("exhaust_glow.hlsl", "PS", psGlow) ||
        !vsh("exhaust_plume.hlsl", "VS", vsPlume) || !psh("exhaust_plume.hlsl", "PS", psPlume) ||
        !vsh("exhaust_shimmer.hlsl", "VSProxy", vsProxy) || !psh("exhaust_shimmer.hlsl", "PSProxy", psProxy) ||
        !vsh("exhaust_shimmer.hlsl", "VSFull", vsFull) || !psh("exhaust_shimmer.hlsl", "PSComposite", psComp)) return false;
    auto cbuf = [&](UINT size, ComPtr<ID3D11Buffer>& b) {
        D3D11_BUFFER_DESC d = { (size + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        return SUCCEEDED(dev->CreateBuffer(&d, nullptr, &b));
    };
    if (!cbuf(sizeof(GlowCB), cbGlow) || !cbuf(sizeof(PlumeCB), cbPlume) || !cbuf(sizeof(ShimCB), cbShim)) return false;
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
    if (FAILED(dev->CreateBlendState(&bd, &bsAdd))) return false;
    bd.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_SRC1_COLOR, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
    if (FAILED(dev->CreateBlendState(&bd, &bsDual))) return false;
    bd.RenderTarget[0] = { FALSE, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
    if (FAILED(dev->CreateBlendState(&bd, &bsOpaque))) return false;
    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(dev->CreateSamplerState(&sd, &sLin))) return false;
    // the blackbody table
    {
        double sun[3]; BlackbodyRGB(5772, sun);
        std::vector<XMFLOAT4> px(256);
        for (int i = 0; i < 256; i++) {
            double T = 200 * pow(100.0, i / 255.0), c[3];
            BlackbodyRGB(T, c);
            px[i] = { (float)log(std::max(c[0] / sun[0], 1e-37)), (float)log(std::max(c[1] / sun[1], 1e-37)), (float)log(std::max(c[2] / sun[2], 1e-37)), 0 };
        }
        D3D11_TEXTURE2D_DESC td = { 256, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, { 1, 0 }, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_SUBRESOURCE_DATA d = { px.data(), 256 * sizeof(XMFLOAT4), 0 };
        ComPtr<ID3D11Texture2D> t;
        if (FAILED(dev->CreateTexture2D(&td, &d, &t)) || FAILED(dev->CreateShaderResourceView(t.Get(), nullptr, &bbSRV))) return false;
    }
    D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
    for (int i = 0; i < 4; i++) {
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; dev->CreateQuery(&qd, &qDis[i]);
        qd.Query = D3D11_QUERY_TIMESTAMP; dev->CreateQuery(&qd, &qT0[i]); dev->CreateQuery(&qd, &qT1[i]);
    }
    partsOk = cfg.particles && parts.Init(gc);
    ok = true;
    H2Chem::ContinuumRGB(kc); kcLum = H2Chem::ContinuumLuminance();
    g_exH2Flame = cfg.h2Flame;
    LOGC("exhaust effects: glow %s, plumes %s, shimmer %s, particles %s (k_abs %.1f m2/kg; H2 jets %s, blue continuum %.3g cd/m2 per (1e20/m3)^2 m; Config\\D3D11Client\\Exhaust.cfg)%s",
         cfg.glow ? "on" : "off", cfg.plumes ? "on" : "off", cfg.shimmer ? "on" : "off",
         !cfg.particles ? "off" : partsOk ? "on" : "FAILED", cfg.kabs, cfg.h2Flame ? "by their chemistry" : "as art (0x100)", kcLum,
         cfg.testJets.size() || cfg.testHeat.size() ? " - TEST jets/heat on the focus" : "");
    return true;
}

void ExhaustFx::Release()
{
    parts.Release();
    vsGlow.Reset(); vsPlume.Reset(); vsProxy.Reset(); vsFull.Reset(); psGlow.Reset(); psPlume.Reset(); psProxy.Reset(); psComp.Reset();
    cbGlow.Reset(); cbPlume.Reset(); cbShim.Reset(); bsAdd.Reset(); bsDual.Reset(); bsOpaque.Reset(); sLin.Reset(); bbSRV.Reset();
    copyTex.Reset(); offTex.Reset(); copySRV.Reset(); offSRV.Reset(); offRTV.Reset(); tw = th = 0;
    for (int i = 0; i < 4; i++) { qDis[i].Reset(); qT0[i].Reset(); qT1[i].Reset(); }
    ok = partsOk = false;
}

// the body at the focus and its gas; false: nothing to do (no focus vessel)
bool ExhaustFx::Scene(ExScene& sc, const VECTOR3& cp, double simDt)
{
    sc.cp = cp; sc.simt = oapiGetSimTime(); sc.dt = simDt;
    OBJHANDLE f = oapiGetFocusObject();
    if (!f || !oapiIsVessel(f)) return false;
    VESSEL* v = oapiGetVesselInterface(f);
    sc.hBody = v->GetSurfaceRef();
    if (!sc.hBody) return false;
    oapiGetGlobalPos(sc.hBody, &sc.pp);
    oapiGetRotationMatrix(sc.hBody, &sc.Rp);
    sc.R = oapiGetSize(sc.hBody);
    sc.GM = oapiGetMass(sc.hBody) * GGRAV;
    char nm[64] = ""; oapiGetObjectName(sc.hBody, nm, 63);
    sc.titan = !strcmp(nm, "Titan");
    sc.planet = nullptr;
    if (g_fxPlanets) for (Planet* p : *g_fxPlanets) if (p->hObj == sc.hBody) sc.planet = p;
    sc.rhoG = v->GetAtmDensity(); sc.pa = v->GetAtmPressure(); sc.Tg = v->GetAtmTemperature();
    if (const ATMCONST* ac = oapiGetPlanetAtmConstants(sc.hBody)) {
        sc.Rgas = ac->R;
        if (sc.rhoG > 0 && sc.pa > 0) { double T = sc.pa / (sc.rhoG * ac->R); if (T > 2 && T < 3000) sc.Tg = T; }
    }
    double T = sc.Tg > 20 ? sc.Tg : 94.0;
    sc.mu = 1.663e-5 * pow(T / 273.0, 1.5) * 380.0 / (T + 107.0);     // Sutherland (N2)
    sc.dense = sc.rhoG > 1e-3;
    sc.Rliq = sc.planet && sc.planet->liq.has && g_cfg.liquids ? sc.R + sc.planet->liq.level : 0;
    // the Sun's solid angle here: the radiance scale
    VECTOR3 sp = _V(0, 0, 0); double rs = 6.957e8;
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) { oapiGetGlobalPos(h, &sp); rs = oapiGetSize(h); break; } }
    const double d = std::max(length(sp - cp), rs * 2);
    const double omega = PI * (rs / d) * (rs / d);
    sc.kE = PI / omega;
    return true;
}

void ExhaustFx::Collect(const ExScene& sc)
{
    jets.clear(); heat.clear(); smoke.clear();
    steamPrev.swap(steamOn); steamOn.clear();
    {
        std::lock_guard<std::mutex> lk(g_ext.mx);
        for (auto& kv : g_ext.jets) {
            const ExtState::Jet& s = kv.second;
            if (s.thrust <= 0 || sc.simt - s.t > 0.5 || !oapiIsVessel(kv.first.h)) continue;
            ExJet j; j.h = kv.first.h; j.id = kv.first.id;
            j.eg = s.pos; j.dg = s.dir; j.thrust = s.thrust; j.ve = s.ve; j.T = s.T; j.wet = s.wet; j.rExit = std::max(s.rExit, 0.005); j.ground = s.ground;
            jets.push_back(j);
        }
        for (auto& kv : g_ext.heat) {
            if (!oapiIsVessel(kv.first.h) || kv.second.dT <= 0 || kv.second.r <= 0) continue;
            ExHeat h; h.h = kv.first.h; h.g = kv.second.pos; h.r = kv.second.r; h.dT = kv.second.dT;
            heat.push_back(h);
        }
    }
    OBJHANDLE f = oapiGetFocusObject();
    if (f && oapiIsVessel(f)) {      // test runs: jets and hot nodes on the focus (Exhaust.cfg)
        int id = 9000;
        for (auto& t : cfg.testJets) {
            ExJet j; j.h = f; j.id = id++;
            j.eg = _V(t.p[0], t.p[1], t.p[2]); j.dg = _V(t.d[0], t.d[1], t.d[2]);
            j.thrust = t.thrust; j.ve = t.ve; j.T = t.T; j.rExit = t.rExit; j.ground = t.ground; j.wet = t.wet;
            jets.push_back(j);
        }
        for (auto& t : cfg.testHeat) { ExHeat h; h.h = f; h.g = _V(t.p[0], t.p[1], t.p[2]); h.r = t.r; h.dT = t.dT; heat.push_back(h); }
    }
    // vessel frame -> global; the gas round each
    for (ExJet& j : jets) {
        VESSEL* vv = oapiGetVesselInterface(j.h);
        auto& lst = lastExitLocal[j.h];
        bool have = false;
        for (auto& e : lst) if (length(e.eg - j.eg) < 1e-3) { e.dg = j.dg; e.rExit = j.rExit; have = true; }
        if (!have && lst.size() < 8) lst.push_back({ j.eg, j.dg, j.rExit });
        (void)vv;
        VESSEL* v = oapiGetVesselInterface(j.h);
        const VECTOR3 eL = j.eg, dL = Unit(j.dg);                  // (the vessel's frame: the steam's source below)
        VECTOR3 p, d; v->Local2Global(j.eg, p); v->GlobalRot(Unit(j.dg), d);
        j.eg = p; j.dg = Unit(d);
        JetGas(j, v->GetAtmPressure(), v->GetAtmDensity());
        j.yH2 = std::clamp((j.Rs / 8314.0 - 1.0 / 18) / (0.5 - 1.0 / 18), 0.0, 1.0);
        j.h2 = cfg.h2Flame && j.Rs > 450;              // lighter than water: the exhaust carries free hydrogen
        if (j.h2) H2Jet(j, v, eL, dL, sc);
    }
    for (const auto& e : steamPrev)            // a jet stopped: its steam stops (what is in the air drifts off)
        if (!steamOn.count(e)) { const double z[3] = {}; parts.Emit(e.first, e.second, z, z, 0, 0, 0, 2, 0, 0, 0, 0); }
    lastExit.clear();
    for (auto it = lastExitLocal.begin(); it != lastExitLocal.end();) {
        if (!oapiIsVessel(it->first)) { it = lastExitLocal.erase(it); continue; }
        VESSEL* v = oapiGetVesselInterface(it->first);
        for (auto& e : it->second) { VECTOR3 p, d; v->Local2Global(e.eg, p); v->GlobalRot(Unit(e.dg), d); lastExit[it->first].push_back({ p, Unit(d), e.rExit }); }
        ++it;
    }
    for (ExHeat& h : heat) {
        VESSEL* v = oapiGetVesselInterface(h.h);
        VECTOR3 p; v->Local2Global(h.g, p); h.g = p;
        h.Tair = v->GetAtmTemperature();
        if (v->GetAtmDensity() <= 0) h.Tair = 0;     // no air: dT is the metal's own temperature (above ~0 K)
    }
}

// A hydrogen jet's chemistry (H2Chem.h): the OH of its mix with the ambient gas over the exhaust's share, at the exit,
// the reach of its flame, and the water condensing out of it.
void ExhaustFx::H2Jet(ExJet& j, VESSEL* v, const VECTOR3& eL, const VECTOR3& dL, const ExScene& sc)
{
    const double pa = v->GetAtmPressure(), ra = v->GetAtmDensity(), Te = std::max(j.T, 300.0);
    double Ta = v->GetAtmTemperature();
    double yO2 = 0, gam = 1.4, Rg = 287.0;
    OBJHANDLE hb = v->GetAtmRef(); char bn[64] = "";
    if (hb) {
        oapiGetObjectName(hb, bn, 64);
        if (const ATMCONST* ac = oapiGetPlanetAtmConstants(hb)) {
            Rg = ac->R > 0 ? ac->R : Rg; gam = ac->gamma > 1 ? ac->gamma : gam;
            if (ac->O2pp > 0 && ac->p0 > 0) yO2 = std::clamp(ac->O2pp / ac->p0 * 31.998 / (8314.46 / Rg), 0.0, 1.0);
            if (ra > 0 && pa > 0) { double T = pa / (ra * Rg); if (T > 2 && T < 3000) Ta = T; }
        }
        if (!yO2 && !strcmp(bn, "Earth")) yO2 = 0.232;   // (a planet module without its O2 partial pressure)
    }
    if (pa < 1e-3) yO2 = 0;
    // the mixing table (cached: the jet pack's throttle changes neither the gas nor the exhaust's state)
    const MixKey key = { j.yH2, Te, j.ve, pa, Ta, yO2 };
    MixCache& mc = mixCache[{ j.h, j.id }];
    auto near1 = [](double a, double b, double r) { return fabs(a - b) <= r * std::max(fabs(a), fabs(b)) + 1e-9; };
    if (!near1(mc.k.yH2, key.yH2, 0.01) || !near1(mc.k.Te, key.Te, 0.01) || !near1(mc.k.ve, key.ve, 0.01) ||
        !near1(mc.k.pa, key.pa, 0.02) || !near1(mc.k.Ta, key.Ta, 0.01) || mc.k.yO2 != key.yO2) {
        mc.k = key;
        H2Chem::Mix(mc.t, j.yH2, Te, j.ve, std::max(pa, 1.0), std::max(Ta, 20.0), yO2);
    }
    j.fsU = mc.t.fs > 0 ? mc.t.fs : 0.03;
    for (int i = 0; i < 32; i++) { j.ohMix[i] = pa > 1e-3 ? mc.t.oh[i] : 0.0f; j.tMix[i] = mc.t.T[i]; }
    {   // the glow's reach on the axis (c_axis ~ Lc / z): the leanest mix whose OH^2 is still 1 % of the peak
        float mx = 0; for (float o : j.ohMix) mx = std::max(mx, o);
        double cMin = 1;
        for (int i = 0; i < 32; i++) if (mx > 0 && j.ohMix[i] >= 0.1f * mx) { double u = (i + 0.5) / 32; cMin = j.fsU * u / (1 - u); break; }
        j.glowZ = mx > 0 ? std::min(j.Lc / std::max(cMin, 1e-4), 200 * j.Lc) : 0;
    }
    j.ohE = H2Chem::ExhaustOH(j.yH2, Te, j.pE) * j.pE / (1.380649e-23 * Te) * 1e-20;
    // the flame's reach: the axis' share falls as Lc / z; the light lasts down to ~0.3 of the stoichiometric share
    j.flameZ = mc.t.fs > 0 && pa > 1e-3 ? j.Lc / (0.3 * mc.t.fs) : 0;
    // the water: condensed where the mixing line crosses saturation (Schmidt-Appleman, Schumann 1996), per kg of exhaust
    // W kg of water (the afterburning's too) and Q J of heat (the exhaust's, its kinetic energy, the burning's: 120 MJ/kg)
    if (!cfg.particles || pa < 1.0 || ra < 1e-4 || j.vacW > 0.5) return;
    const double burn = yO2 > 0 ? 1.0 : 0.0;
    const double W = (1 - j.yH2) + burn * 8.937 * j.yH2;
    const double Q = H2Chem::ExhaustHeat(j.yH2, Te, pa, Ta) + 0.5 * j.ve * j.ve + burn * 1.2e8 * j.yH2;
    const double cp = Rg * gam / (gam - 1), Ma = 8314.46 / Rg;
    const double ea = !strcmp(bn, "Earth") ? cfg.rh * H2Chem::EsLiquid(Ta) : 0.0;   // other bodies: dry (Mars: a trace)
    double cStar = 0;
    const double fc = H2Chem::Condense(W, Q, pa, Ta, cp, Ma, ea, cStar);
    j.cond = fc;
    // (the 3D flow takes the jet's heat and momentum whether or not water condenses: the heat haze needs them)
    const bool vis = fc >= cfg.condMin && cStar > 0;
    if (!vis && !SmokeEnabled()) return;
    // where the jet's mean mix reaches c*: the entrained mass ~ 0.32 x/d_eff of the jet's own (Ricou & Spalding 1961),
    // d_eff = D (rho_jet / rho_air)^0.5. The condensate does not wait for it: the jet's outer mixing layer is diluted
    // past c* from the first centimetres, and the share of the jet's water there grows as (z / z*)^2 (the Gaussian
    // profile's mass flux beyond the share c*: (c* / c_axis)^2, c_axis ~ 1/z) - a thin wisp at the nozzle, thickening
    // to the whole plume at z*. Four sources along the jet carry those shares; a jet meeting the ground first leaves
    // the rest there, spreading along it (the wall jet)
    const double rhoJ = pa / (j.Rs * Te), dEff = 2 * j.rj * sqrt(std::clamp(rhoJ / ra, 1e-6, 1.0));
    const double zStar = vis ? std::clamp((1 / cStar - 1) * dEff / 0.32, 4 * j.rExit, 60.0) : 1.0;
    const double mdot = j.thrust / std::max(j.ve, 100.0);
    VECTOR3 up;
    {
        VECTOR3 gp, bp; v->GetGlobalPos(gp); oapiGetGlobalPos(v->GetSurfaceRef(), &bp);
        MATRIX3 R; v->GetRotationMatrix(R);
        up = Unit(tmul(R, gp - bp));               // the local vertical in the vessel's frame
    }
    // the height of the exit over what is under it: the ground or a liquid's surface (Titan's seas), whichever is higher
    double alt = v->GetAltitude(ALTMODE_GROUND);
    if (sc.Rliq > 0) { VECTOR3 gp, bp; v->GetGlobalPos(gp); oapiGetGlobalPos(v->GetSurfaceRef(), &bp); alt = std::min(alt, length(gp - bp) - sc.Rliq); }
    alt += dotp(eL, up);
    const double down = -dotp(dL, up);
    const double zHit = down > 0.2 && alt > 0 ? (alt - 0.15) / down : 1e9;   // a little over the surface (fog dies on it)
    const double life = Ta < 160 ? 40.0 : (ea >= H2Chem::EsIce(Ta) ? 20.0 : 3.0);
    // one stream along the jet from the exit (GpuParticles: kind 4 - born over 0..z* by the (z / z*)^2 share, carried by
    // the jet's own flow field: its momentum flux is the thrust); a jet meeting the ground: the stream ends there and
    // the fog flows out along it with the wall jet
    if (SmokeEnabled()) {
        // the 3D flow (Smoke.cpp): the jet's own flow imposed down to where it has slowed to ~30 m/s (or the ground),
        // its heat there from the mix table, the condensate formed over 0..z*
        SmokeSrc q;
        q.eg = j.eg; q.dg = j.dg; q.rExit = j.rExit;
        q.kJ = sqrt(j.thrust / std::max(ra, 1e-4));
        q.mVis = vis ? fc * W * mdot : 0.0; q.zFull = zStar;
        q.inject = std::clamp(7 * q.kJ / 30.0, 0.3, std::min(3.0, zHit > 0 ? zHit : 3.0));
        {
            const double cAx = std::min(1.0, j.Lc / q.inject), u = cAx / (cAx + j.fsU);
            const int i = std::clamp((int)(u * 32 - 0.5), 0, 31);
            q.Tex = std::max(0.0, (double)j.tMix[i] - Ta);
        }
        q.decay = life > 30 ? 0.0 : 1.0 / life;
        smoke.push_back(q);
    } else {
        const double zS = std::min(zStar, zHit > 0 ? zHit : zStar);
        const double pv[3] = { eL.x, eL.y, eL.z }, vel[3] = { dL.x * 300, dL.y * 300, dL.z * 300 };
        parts.Emit(j.h, 9500 + 8 * j.id, pv, vel, zS, 0.0, fc * W * mdot, 4, 0, 0.0, life, j.thrust);
        steamOn.insert({ j.h, 9500 + 8 * j.id });
    }
    j.condZ = std::min(zStar, zHit);
}

void ExhaustFx::Targets(D3D11Client* gc)
{
    UINT w = gc->SceneW(), h = gc->SceneH();
    if (w == tw && h == th && copyTex) return;
    ID3D11Device* dev = gc->Dev();
    D3D11_TEXTURE2D_DESC td = { w, h, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    copyTex.Reset(); copySRV.Reset(); offTex.Reset(); offSRV.Reset(); offRTV.Reset();
    dev->CreateTexture2D(&td, nullptr, &copyTex);
    if (copyTex) dev->CreateShaderResourceView(copyTex.Get(), nullptr, &copySRV);
    td.Format = DXGI_FORMAT_R16G16_FLOAT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    dev->CreateTexture2D(&td, nullptr, &offTex);
    if (offTex) { dev->CreateShaderResourceView(offTex.Get(), nullptr, &offSRV); dev->CreateRenderTargetView(offTex.Get(), nullptr, &offRTV); }
    tw = w; th = h;
}

void ExhaustFx::DrawGlow(D3D11Client* gc, const ExScene& sc)
{
    if (!cfg.glow || heat.empty()) return;
    ID3D11DeviceContext* ctx = gc->Ctx();
    GlowCB k = {};
    int n = 0;
    for (const ExHeat& h : heat) {
        const double T = h.Tair + h.dT;
        if (T < 650) continue;          // B(650 K) in the visible: ~1e-12 of the Sun's - nothing to see
        // a hot node by a nozzle's exit (the modules put it a little inside the chamber, for the shimmer): what glows is
        // the nozzle - its lip and inner wall round the exit - not the housing round the node
        VECTOR3 cg = h.g; double rr = h.r; VECTOR3 ax = _V(0, 0, 0); bool nozzle = false;
        for (const ExJet& j : jets)
            if (j.h == h.h && length(j.eg - h.g) < 3 * h.r + j.rExit) { cg = j.eg; rr = j.rExit; ax = j.dg; nozzle = true; break; }
        if (!nozzle) {          // a jet stopped this frame: its nozzle by the node's vessel frame (the exits seen before)
            auto it = lastExit.find(h.h);
            if (it != lastExit.end()) for (auto& e : it->second)
                if (length(e.eg - h.g) < 3 * h.r + e.rExit) { cg = e.eg; rr = e.rExit; ax = e.dg; nozzle = true; break; }
        }
        if (!nozzle) continue;      // only nozzles glow: a hot node with no jet exit by it lights no housing (the user, 2026-10-19)
        VECTOR3 c = cg - sc.cp;
        if (length(c) > 5000 + h.r) continue;
        k.node[3 * n] = F4(c, rr);
        k.node[3 * n + 1] = { (float)h.Tair, (float)h.dT, (float)cfg.emis, 0 };
        k.node[3 * n + 2] = F4(ax, nozzle ? 1.0 : 0.0);
        if (++n >= 32) break;
    }
    if (!n) return;
    k.glow = { (float)sc.kE, (float)n, 0, 0 };
    XMStoreFloat4x4(&k.vp, XMMatrixTranspose(g_exViewProj));
    D3D11_MAPPED_SUBRESOURCE m;
    ctx->Map(cbGlow.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, &k, sizeof k); ctx->Unmap(cbGlow.Get(), 0);
    ctx->OMSetBlendState(bsAdd.Get(), nullptr, ~0u);
    ctx->VSSetShader(vsGlow.Get(), nullptr, 0); ctx->PSSetShader(psGlow.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(5, 1, cbGlow.GetAddressOf()); ctx->PSSetConstantBuffers(5, 1, cbGlow.GetAddressOf());
    ctx->DrawInstanced(36, n, 0, 0);
}

bool ExhaustFx::DrawShimmer(D3D11Client* gc, const ExScene& sc, const XMMATRIX& proj, ID3D11RenderTargetView* rtv)
{
    if (!cfg.shimmer || !sc.dense || sc.Tg <= 10) return false;
    ShimCB k = {};
    int n = 0;
    VECTOR3 up = Unit(sc.cp - sc.pp);
    const double rhoA = sc.rhoG, Ta = sc.Tg;
    auto add = [&](const VECTOR3& a, double wa, const VECTOR3& b, double wb, double dT, double speed, double seed, int kind) {
        if (n >= 32) return;
        double amp = 5.0 * K_REFR * rhoA * dT / (Ta + dT);       // the rays' bending [rad]
        VECTOR3 mid = (a + b) * 0.5 - sc.cp;
        if (amp < 2e-6 || length(mid) > 3000 + length(b - a)) return;
        k.col[3 * n] = F4(a - sc.cp, wa); k.col[3 * n + 1] = F4(b - sc.cp, wb);
        k.col[3 * n + 2] = { (float)amp, (float)speed, (float)seed, (float)kind };
        n++;
    };
    const double g = sc.GM / (sc.R * sc.R);
    for (const ExHeat& h : heat) {      // a hot node's buoyant column: speed ~ sqrt(g r dT / T)
        double sp = std::min(3.0, sqrt(g * h.r * h.dT / std::max(Ta, 10.0)));
        add(h.g - up * (0.5 * h.r), 1.3 * h.r, h.g + up * (4 * h.r), 1.6 * h.r, h.dT, sp, fmod(h.r * 977.0 + h.dT * 0.37, 1.0), 1);
    }
    for (const ExJet& j : jets) {       // a jet's hot gas: down to where its excess temperature is ~0.3 of the air's
        if (j.h2 && SmokeLive()) continue;   // (its haze: the 3D flow's own temperature field, SmokeShimmer)
        double dT = j.T - Ta;
        if (dT < 20 || j.vacW > 0.9) continue;
        // the mixed gas: excess temperature (Te - Ta) Lc / x - refracting down to ~10 % of the air's temperature
        double L = std::clamp(j.Lc * dT / (0.1 * Ta), 20 * j.rExit, 15.0);
        double sp = std::min(0.5 * j.ve, 300.0);
        if (j.h2) {
            // a hydrogen jet: its mix stays hot far down (the jet's kinetic energy dissipates into it, H2Chem's table):
            // the haze reaches where the axis' mix is within 10 % of the air's temperature; the eddies there move at the
            // axis' speed ~ ve Lc / z, shown no faster than the frames can follow (a camera would blur the rest)
            double cHot = 1;
            for (int i = 0; i < 32; i++) if (j.tMix[i] - Ta > 0.1 * Ta) { double u = (i + 0.5) / 32; cHot = j.fsU * u / (1 - u); break; }
            L = std::clamp(j.Lc / std::max(cHot, 1e-4), 20 * j.rExit, 15.0);
            const double w = j.rExit + 0.11 * L, fps = std::max(oapiGetFrameRate(), 10.0);
            sp = std::min(0.6 * j.ve * j.Lc / (0.5 * L), 0.35 * fps * 0.3 * 0.5 * w);
        }
        add(j.eg, j.rExit * 1.2, j.eg + j.dg * L, j.rExit + 0.11 * L, dT, sp, fmod(j.id * 0.618, 1.0), 0);
    }
    if (!n && !SmokeLive()) return false;
    ID3D11DeviceContext* ctx = gc->Ctx();
    XMFLOAT4X4 P; XMStoreFloat4x4(&P, proj);
    k.misc = { (float)(P._22 * 0.5 * gc->SceneH()), (float)fmod(sc.simt, 1000.0), 1.0f / gc->SceneW(), 1.0f / gc->SceneH() };
    XMStoreFloat4x4(&k.vp, XMMatrixTranspose(g_exViewProj));
    D3D11_MAPPED_SUBRESOURCE m;
    ctx->Map(cbShim.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, &k, sizeof k); ctx->Unmap(cbShim.Get(), 0);
    // the scene so far (the depth resolved with it)
    gc->CopySceneColour(copyTex.Get());
    {   // the resolve took the depth view off the pipeline (MSAA: it was its target): bind ours again
        ID3D11ShaderResourceView* srv[2] = { gc->depthSRV.Get(), bbSRV.Get() };
        ctx->PSSetShaderResources(5, 2, srv);
        ctx->RSSetState(gc->rsNone.Get());
        ctx->OMSetDepthStencilState(gc->dsRead.Get(), 0);
    }
    float z[4] = {};
    ctx->ClearRenderTargetView(offRTV.Get(), z);
    ctx->OMSetRenderTargets(1, offRTV.GetAddressOf(), nullptr);
    ctx->OMSetBlendState(bsAdd.Get(), nullptr, ~0u);
    ctx->VSSetShader(vsProxy.Get(), nullptr, 0); ctx->PSSetShader(psProxy.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(5, 1, cbShim.GetAddressOf()); ctx->PSSetConstantBuffers(5, 1, cbShim.GetAddressOf());
    if (n) ctx->DrawInstanced(6, n, 0, 0);
    SmokeShimmer(gc);                  // the 3D flow's hot gas: its refraction into the same offsets (additive)
    ctx->VSSetConstantBuffers(5, 1, cbShim.GetAddressOf()); ctx->PSSetConstantBuffers(5, 1, cbShim.GetAddressOf());
    // the scene re-read through the offsets
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ID3D11ShaderResourceView* srv[2] = { offSRV.Get(), copySRV.Get() };
    ctx->PSSetShaderResources(3, 2, srv);
    ctx->VSSetShader(vsFull.Get(), nullptr, 0); ctx->PSSetShader(psComp.Get(), nullptr, 0);
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul[2] = {};
    ctx->PSSetShaderResources(3, 2, nul);
    return true;
}

void ExhaustFx::DrawPlumes(D3D11Client* gc, const ExScene& sc)
{
    if (!cfg.plumes || jets.empty()) return;
    ID3D11DeviceContext* ctx = gc->Ctx();
    ctx->OMSetBlendState(bsDual.Get(), nullptr, ~0u);
    ctx->VSSetShader(vsPlume.Get(), nullptr, 0); ctx->PSSetShader(psPlume.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(5, 1, cbPlume.GetAddressOf()); ctx->PSSetConstantBuffers(5, 1, cbPlume.GetAddressOf());
    const double Tvis = 750;       // below it the gas gives no visible light worth marching
    for (const ExJet& j : jets) {
        if (j.T < Tvis || j.thrust <= 0) continue;
        const double Ta = std::max(sc.Tg, 0.0);
        if (length(j.eg - sc.cp) > 20000) continue;
        PlumeCB k = {};
        // the box: thick air down to where the exhaust's share no longer keeps it above Tvis; vacuum: a few radii, wide
        const double re = j.rExit;
        double cmin = std::clamp((Tvis - Ta) / std::max(j.T * (1 + j.shock) - Ta, 1.0), 0.02, 1.0);
        double La = std::min(j.Lc / cmin, 60 * j.Lc), Ha = j.rj + 0.09 * La;
        const double thMax = std::clamp(PI / 2 * (sqrt((j.gamma + 1) / (j.gamma - 1)) - 1) - PrandtlMeyer(j.Me, j.gamma), 0.2, 2.3);
        double Lv = re * pow(Tvis / j.T, -0.5 / (j.gamma - 1));      // (re / R)^(2 (gamma-1)) = Tvis / Te
        Lv = std::clamp(Lv, 2 * re, 60 * re);
        double Hv = Lv * (thMax > PI / 2 ? 1.0 : sin(thMax)), z0v = thMax > PI / 2 ? -Lv * std::min(1.0, -cos(thMax)) : 0;
        double L = 0, H = 0, z0 = 0;
        if (j.vacW < 0.999) { L = La; H = Ha; }
        if (j.h2) { const double zg = std::max(j.flameZ, j.glowZ) * 1.3; L = std::max(L, zg); H = std::max(H, 2.2 * (j.rj + 0.09 * zg)); }   // the glow's whole reach
        if (j.vacW > 0.001) { L = std::max(L, Lv); H = std::max(H, Hv); z0 = z0v; }
        VECTOR3 ez = j.dg, ex = Unit(crossp(ez, fabs(ez.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0))), ey = crossp(ez, ex);
        const double rhoJ = sc.dense ? sc.pa / (j.Rs * j.T) : j.rhoE;
        k.e = F4(j.eg - sc.cp, re);
        k.x = F4(ex, sc.kE); k.y = F4(ey, fmod(sc.simt, 1000.0)); k.z = F4(ez, g_cfg.aa == 5 ? -cfg.steps : cfg.steps);   // < 0: TAA, the steps' offsets move per frame
        k.box = { (float)H, (float)z0, (float)L, (float)(j.h2 ? 0.0 : cfg.kabs) };   // H2 + H2O: no visible absorption (no soot)
        k.gas = { (float)j.T, (float)Ta, (float)j.gamma, (float)j.Me };
        k.jet = { (float)j.rj, (float)j.Lc, (float)j.cell, (float)j.shock };
        const double flick = 1.0 + 0.04 * (((j.id * 2654435761u + (unsigned)(sc.simt * 997)) & 1023) / 1023.0 - 0.5);
        k.vac = { (float)j.vacW, (float)thMax, (float)j.rhoE, (float)flick };
        k.air = { (float)rhoJ, 0.09f, (float)fmod(j.id * 0.618, 1.0), (float)(0.5 * j.ve) };
        // a hydrogen jet: its light the blue continuum ~ [OH]^2 - OH over the exhaust's share in the mix (the flame sheet
        // where that share is stoichiometric), at the exit (the vacuum's frozen flow); per [OH]^2 path the radiance per channel
        k.h2 = { j.h2 ? 1.0f : 0.0f, (float)j.ohE, (float)std::clamp(1.0 / std::max(oapiGetFrameRate(), 1.0), 1.0 / 240, 0.1), (float)std::max(std::max(j.flameZ, j.glowZ), 4 * j.rj) };
        k.kc = { (float)kc[0], (float)kc[1], (float)kc[2], (float)j.fsU };
        for (int q = 0; q < 8; q++) k.oh[q] = { j.ohMix[4 * q], j.ohMix[4 * q + 1], j.ohMix[4 * q + 2], j.ohMix[4 * q + 3] };
        XMStoreFloat4x4(&k.vp, XMMatrixTranspose(g_exViewProj));
        D3D11_MAPPED_SUBRESOURCE m;
        ctx->Map(cbPlume.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, &k, sizeof k); ctx->Unmap(cbPlume.Get(), 0);
        ctx->Draw(36, 0);
    }
}

void ExhaustFx::Log(const ExScene& sc)
{
    LARGE_INTEGER now, f; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
    fpsAcc += oapiGetFrameRate(); fpsN++;
    if (tLog.QuadPart && (now.QuadPart - tLog.QuadPart) < 10 * f.QuadPart) return;
    tLog = now;
    std::string js;
    for (const ExJet& j : jets) {
        char b[400];
        sprintf_s(b, " [jet %d: T %.0f N, ve %.0f, Te %.0f K, re %.3f m, n %.3g, Me %.2f, rj %.3f, Lc %.2f, cell %.2f, shock %.2f, vac %.2f, H2 %.2f",
                  j.id, j.thrust, j.ve, j.T, j.rExit, j.n, j.Me, j.rj, j.Lc, j.cell, j.shock, j.vacW, j.yH2);
        js += b;
        if (j.h2) {
            // the brightest mix's OH and a luminance for a chord of ~0.5 m of it (an order of magnitude to compare with the
            // scene: sunlit ground ~5000 cd/m2, the night sky 1e-3)
            double ohMax = 0; for (float o : j.ohMix) ohMax = std::max(ohMax, (double)o);
            sprintf_s(b, ", fs %.3f, flame to %.2f m, OH max %.3g e20/m3 (~%.3g cd/m2 per 0.5 m), exit OH %.3g, water condensing %.0f %% at %.1f m",
                      j.fsU, j.flameZ, ohMax, kcLum * ohMax * ohMax * 0.5, j.ohE, 100 * j.cond, j.condZ);
            js += b;
        }
        js += "]";
    }
    LOGC("exhaust: GPU %.2f ms | fps %.0f | jets %zu, hot nodes %zu | particles alive %u, born %u/frame | gas %.3g kg/m3 %.0f K, kE %.3g%s",
         gpuMs, fpsN ? fpsAcc / fpsN : 0.0, jets.size(), heat.size(), parts.AliveLast(), parts.SpawnedLast(), sc.rhoG, sc.Tg, sc.kE, js.c_str());
    fpsAcc = 0; fpsN = 0;
}

void ExhaustFx::Render(D3D11Client* gc, const VECTOR3& cp, const MATRIX3& cr, const XMMATRIX& proj, double simDt)
{
    if (!ok || !gc->depthSRV) return;
    ExScene sc;
    if (!Scene(sc, cp, simDt)) return;
    Collect(sc);
    ID3D11DeviceContext* ctx = gc->Ctx();
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    g_exViewProj = V * proj;
    UINT qi = qFrame % 4;
    ctx->Begin(qDis[qi].Get()); ctx->End(qT0[qi].Get());
    {
        PipeState saved(ctx);      // everything as it was on return
        if (partsOk) parts.Update(gc, sc, jets);
        SmokeUpdate(gc, sc, smoke);
        ComPtr<ID3D11RenderTargetView> rtv; ComPtr<ID3D11DepthStencilView> dsv;
        ctx->OMGetRenderTargets(1, &rtv, &dsv);
        bool any = (cfg.glow && !heat.empty()) || (cfg.plumes && !jets.empty()) || partsOk;
        if (any && rtv) {
            Targets(gc);
            gc->ResolveDepth();
            ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);      // the depth is read as a texture
            D3D11_VIEWPORT vp = { 0, 0, (float)gc->SceneW(), (float)gc->SceneH(), 0, 1 };
            ctx->RSSetViewports(1, &vp);
            ctx->OMSetDepthStencilState(gc->dsRead.Get(), 0);
            ctx->RSSetState(gc->rsNone.Get());
            ctx->IASetInputLayout(nullptr);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11ShaderResourceView* srv[2] = { gc->depthSRV.Get(), bbSRV.Get() };
            ctx->PSSetShaderResources(5, 2, srv);
            ctx->PSSetSamplers(3, 1, sLin.GetAddressOf());
            DrawGlow(gc, sc);
            if (copyTex && offRTV && DrawShimmer(gc, sc, proj, rtv.Get())) {
                ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
                ctx->PSSetShaderResources(5, 2, srv);
            }
            DrawPlumes(gc, sc);
            if (partsOk) parts.Render(gc, sc, gc->depthSRV.Get());
            SmokeRender(gc);
            SprayRender(gc, sc, gc->depthSRV.Get());
            RenderVesselExhausts(gc, sc.cp);
            ID3D11ShaderResourceView* nul[2] = {};
            ctx->PSSetShaderResources(5, 2, nul);
        }
    }
    ctx->End(qT1[qi].Get()); ctx->End(qDis[qi].Get());
    qFrame++;
    if (qFrame >= 4) {
        UINT pf = (qFrame - 4) % 4;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj; UINT64 a, b;
        if (ctx->GetData(qDis[pf].Get(), &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !dj.Disjoint &&
            ctx->GetData(qT0[pf].Get(), &a, 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && ctx->GetData(qT1[pf].Get(), &b, 8, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
            gpuMs = 0.9 * gpuMs + 0.1 * (double)(b - a) * 1000.0 / dj.Frequency;
    }
    if (g_cfg.statsLog) Log(sc);
}

ExhaustFx fx;

}  // namespace

// ------------------------------------------------------------------------------------------ the client's hooks
void ExhaustInit(D3D11Client* gc) { if (!fx.Init(gc)) { LOGC("exhaust effects FAILED (shaders or resources; see above)"); fx.Release(); } }
void ExhaustRender(D3D11Client* gc, const VECTOR3& camPos, const MATRIX3& camRot, const XMMATRIX& proj, double simDt) { fx.Render(gc, camPos, camRot, proj, simDt); }
void ExhaustRelease() { fx.Release(); SprayRelease(); VesselExhaustRelease(); SmokeRelease(); }
void ExhaustEmit(OBJHANDLE h, int id, const double* pos, const double* vel, double cone, double spread, double kgps, int kind, int ground, double wet, double life, double jetJ)
{
    fx.parts.Emit(h, id, pos, vel, cone, spread, kgps, kind, ground, wet, life, jetJ);
}
void ExhaustBox(OBJHANDLE h, int id, const double* c, const double* half) { fx.parts.Box(h, id, c, half); }
