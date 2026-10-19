// GPU particles of the engine effects (see Exhaust.h): the jets' ground ejecta and spray, the modules' free particles.
// The CPU decides what is born this frame (the physics of the sources: a jet's stagnation pressure on the ground against
// the grains' threshold, the liquid's Kelvin-Helmholtz onset and the drops' Weber limit, the modules' mass rates) and
// uploads the newborn; the GPU keeps the pool (65536), flies them (shaders\particles_gpu.hlsl) and draws this frame's
// alive list indirectly. Physics after the AtmoFX D3D9 fork (JetEjecta, FreeParticles) - re-written, with the ground
// also in vacuum and soft particles against the scene's depth.
#include "ExhaustInternal.h"
#include "Fx.h"
#include "Wake.h"

namespace {

struct SimCB { XMFLOAT4 time, gas, geo, misc; XMFLOAT4 imp[16], fr[16], box[64]; XMFLOAT4 m0, m1, m2; XMFLOAT4X4 vp; XMFLOAT4 liq; };   // as particles_gpu.hlsl's Sim
struct DrawCB { XMFLOAT4 m0, m1, m2, camR, camU, geo; XMFLOAT4X4 vp; XMFLOAT4 alb[16]; };

enum Look { LOOK_GRAIN = 0, LOOK_CLOD = 1, LOOK_DROP = 2, LOOK_FOG = 3, LOOK_ICE = 4, LOOK_SPOT = 5, LOOK_SPLASH = 6, LOOK_MIST = 7 };
enum Alb { ALB_DRY = 0, ALB_WET = 1, ALB_ICEG = 2, ALB_DROP = 3, ALB_FOG = 4, ALB_CRYSTAL = 5, ALB_MIST = 6 };
const UINT F_SETTLED = 0x100, F_LIQUID = 0x200, F_HARD = 0x400, F_GROUNDCOL = 0x800;   // GROUNDCOL: the ground's own colour at birth
UINT Flags(int look, int alb, UINT extra = 0) { return (UINT)look | ((UINT)alb << 4) | extra; }

struct Cls { double d, frac, s0, a0; };
const Cls DRY_C[] = { { 5e-4, .12, .10, .55 }, { 2e-4, .28, .18, .35 }, { 1e-4, .25, .28, .22 }, { 3e-5, .22, .40, .10 }, { 1e-5, .13, .60, .04 } };
const Cls WET_C[] = { { 3e-3, .75, .08, .85 }, { 2e-4, .17, .18, .25 }, { 1e-5, .08, .60, .05 } };
const Cls REG_C[] = { { 2e-4, .30, .15, .40 }, { 1e-4, .30, .22, .25 }, { 3e-5, .25, .30, .15 }, { 1e-5, .15, .40, .10 } };
const Cls ICE_C[] = { { 2e-4, .40, .14, .35 }, { 5e-5, .40, .25, .18 }, { 1e-5, .20, .45, .08 } };
const Cls LIQ_C[] = { { 1e-5, 1.0, .90, .10 } };
const Cls DRYF_C[] = { { 5e-4, .20, .10, .55 }, { 2e-4, .40, .18, .35 }, { 1e-4, .30, .28, .22 }, { 3e-5, .07, .40, .10 }, { 1e-5, .03, .60, .04 } };
const Cls CRYS_C[] = { { 1e-4, .25, .08, .40 }, { 4e-5, .45, .14, .25 }, { 1.5e-5, .30, .24, .12 } };
const Cls FOG_C[] = { { 4e-6, 1.0, .10, .16 } };
const Cls DROP_C[] = { { 3e-3, .30, .07, 0 }, { 1e-3, .40, .07, 0 }, { 3e-4, .25, .07, 0 }, { 1e-4, .05, .07, 0 } };   // a spray is a veil of fine streaks: small sprites, many
template<size_t N> std::pair<const Cls*, int> L(const Cls (&a)[N]) { return { a, (int)N }; }

const double JET_RATE_GPU = 2.0;

// drops falling back into a liquid ring it (the wave field, Wake.cpp): their landing is foreseen at birth (ballistic,
// the gas's drag shortening the flight across) and the ring put in when they get there
struct Landing { double due; VECTOR3 l; double r, amp; };
std::vector<Landing> landings;
bool sceneCopy = false;
// crown splashes asked for this frame (Wake.cpp, on an impact)
struct CrownReq { OBJHANDLE body; VECTOR3 g; double r, U; };
std::mutex crownMx;
std::vector<CrownReq> crowns;
void ForeseeLanding(const ExScene& sc, const VECTOR3& p, const VECTOR3& v, double m, double s0)
{
    if (sc.Rliq <= 0 || landings.size() > 600) return;
    const double gyL = sc.Rliq - length(sc.aP);                 // the level in the particles' frame
    const double g = sc.GM / (sc.Rliq * sc.Rliq);
    const double disc = v.y * v.y + 2 * g * (p.y - gyL);
    if (disc < 0) return;
    const double t = (v.y + sqrt(disc)) / g;
    if (t <= 0 || t > 10) return;
    const double k = sc.dense ? 0.6 : 1.0;                       // the gas's drag: shorter across, a little later
    landings.push_back({ sc.simt + t * (sc.dense ? 1.2 : 1.0), _V(p.x + v.x * t * k, gyL, p.z + v.z * t * k),
                         std::clamp(0.5 * s0, 0.02, 0.12), std::clamp(0.5 + m / 0.01, 0.3, 2.0) });
}     // the GPU flies more sprites than the D3D9 fork's CPU did (each a little fainter)

VECTOR3 Unit(const VECTOR3& v) { double l = length(v); return l > 0 ? v / l : _V(0, 1, 0); }
XMFLOAT3 F3(const VECTOR3& v) { return { (float)v.x, (float)v.y, (float)v.z }; }
XMFLOAT4 F4(const VECTOR3& v, double w) { return { (float)v.x, (float)v.y, (float)v.z, (float)w }; }

}  // namespace

double GpuParticles::Rnd()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (rng & 0xffffff) / 16777216.0;
}

bool GpuParticles::Init(D3D11Client* gc)
{
    ID3D11Device* dev = gc->Dev();
    auto cs = [&](const char* e, ComPtr<ID3D11ComputeShader>& s) {
        auto b = FxCompile("particles_gpu.hlsl", e, "cs_5_0");
        return b && SUCCEEDED(dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s));
    };
    if (!cs("CSInit", csInit) || !cs("CSEmit", csEmit) || !cs("CSSim", csSim)) return false;
    auto bv = FxCompile("particles_gpu.hlsl", "VS", "vs_5_0"), bp = FxCompile("particles_gpu.hlsl", "PS", "ps_5_0");
    if (!bv || !bp || FAILED(dev->CreateVertexShader(bv->GetBufferPointer(), bv->GetBufferSize(), nullptr, &vs)) ||
        FAILED(dev->CreatePixelShader(bp->GetBufferPointer(), bp->GetBufferSize(), nullptr, &ps))) return false;

    auto sbuf = [&](UINT n, UINT stride, UINT bind, D3D11_USAGE use, UINT cpu, ComPtr<ID3D11Buffer>& b) {
        D3D11_BUFFER_DESC d = { n * stride, use, bind, cpu, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, stride };
        return SUCCEEDED(dev->CreateBuffer(&d, nullptr, &b));
    };
    if (!sbuf(POOL, sizeof(GPart), D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, pool) ||
        !sbuf(POOL, 4, D3D11_BIND_UNORDERED_ACCESS, D3D11_USAGE_DEFAULT, 0, dead) ||
        !sbuf(POOL, 4, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, alive) ||
        !sbuf(NEW_MAX, sizeof(GPart), D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE, newBuf)) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC u = {}; u.Format = DXGI_FORMAT_UNKNOWN; u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    u.Buffer.NumElements = POOL;
    if (FAILED(dev->CreateUnorderedAccessView(pool.Get(), &u, &poolUAV))) return false;
    u.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    if (FAILED(dev->CreateUnorderedAccessView(dead.Get(), &u, &deadUAV)) || FAILED(dev->CreateUnorderedAccessView(alive.Get(), &u, &aliveUAV))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC s = {}; s.Format = DXGI_FORMAT_UNKNOWN; s.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    s.Buffer.NumElements = POOL;
    if (FAILED(dev->CreateShaderResourceView(pool.Get(), &s, &poolSRV)) || FAILED(dev->CreateShaderResourceView(alive.Get(), &s, &aliveSRV))) return false;
    s.Buffer.NumElements = NEW_MAX;
    if (FAILED(dev->CreateShaderResourceView(newBuf.Get(), &s, &newSRV))) return false;
    // indirect draw arguments: 6 vertices x the alive count
    UINT a0[4] = { 6, 0, 0, 0 };
    D3D11_BUFFER_DESC ad = { 16, D3D11_USAGE_DEFAULT, 0, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, 0 };
    D3D11_SUBRESOURCE_DATA sd = { a0, 0, 0 };
    if (FAILED(dev->CreateBuffer(&ad, &sd, &args))) return false;
    D3D11_BUFFER_DESC cd = { 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    if (FAILED(dev->CreateBuffer(&cd, nullptr, &cbCount))) return false;
    D3D11_BUFFER_DESC rd = { 16, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0 };
    for (auto& r : countRead) if (FAILED(dev->CreateBuffer(&rd, nullptr, &r))) return false;
    D3D11_BUFFER_DESC kd = { (UINT)((sizeof(SimCB) + 15) & ~15u), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    if (FAILED(dev->CreateBuffer(&kd, nullptr, &cbSim))) return false;
    kd.ByteWidth = (UINT)((sizeof(DrawCB) + 15) & ~15u);
    if (FAILED(dev->CreateBuffer(&kd, nullptr, &cbDraw))) return false;
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0] = { TRUE, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
    if (FAILED(dev->CreateBlendState(&bd, &bsPremul))) return false;
    needInit = true;
    return true;
}

// a crown splash (Marston, Vakarelski & Thoroddsen 2012, a disc hitting a liquid; Gilou et al.): above a Weber number
// rho U^2 D / sigma of ~140 the splash sheet's rim breaks into fingers (Rayleigh-Plateau, driven by Rayleigh-Taylor
// as the rim decelerates) whose tips pinch off into drops. A step on Titan's methane: We ~ 470 x 1^2 x 0.15 / 0.017
// ~ 4000 - a broken crown. The sheet leaves at about the impact speed, steeply (60-80 deg); the fingers ~2-3 capillary
// wavelengths apart (methane's capillary length sqrt(sigma / rho g) ~5 mm on Titan: finer than water's on Earth); the
// drops about the rim's thickness (0.3-3 mm); in the low gravity they rise ~U^2 / 2g and fall slowly
void ParticlesCrown(OBJHANDLE body, const VECTOR3& gpos, double radius, double speed)
{
    std::lock_guard<std::mutex> lk(crownMx);
    if (crowns.size() < 32) crowns.push_back({ body, gpos, radius, speed });
}

void GpuParticles::SpawnCrowns(ExScene& sc)
{
    std::vector<CrownReq> list;
    { std::lock_guard<std::mutex> lk(crownMx); list.swap(crowns); }
    if (!sc.hBody || sc.Rliq <= 0) return;
    const double g = sc.GM / (sc.Rliq * sc.Rliq);
    const double lc = sqrt(sc.sigma / (sc.rhoL * g));                       // capillary length
    for (const CrownReq& c : list) {
        if (c.body != sc.hBody) continue;
        const double D = std::max(2 * c.r, 0.05), U0 = std::max(c.U, 0.2);
        const double We = sc.rhoL * U0 * U0 * D / std::max(sc.sigma, 1e-4);
        if (We < 140) continue;                                               // the rim stays whole: a ring, no drops
        VECTOR3 p0 = sc.ToLocal(c.g);
        p0.y = sc.Rliq - length(sc.aP) + 0.01;                                // on the level
        const double R0 = 0.5 * D;
        const int nF = (int)std::clamp(2 * 3.14159265 * R0 / (2.5 * 2 * 3.14159265 * lc), 8.0, 36.0);
        const int perF = (int)std::clamp(2 + log10(We / 140) * 2, 2.0, 7.0);
        const double ph = Rnd() * 6.2831853;
        for (int f = 0; f < nF; f++) {
            const double azF = ph + 6.2831853 * f / nF;
            for (int k = 0; k < perF; k++) {
                const double az = azF + U(-0.08, 0.08);
                const VECTOR3 rad = _V(cos(az), 0, sin(az));
                const double sp = U0 * U(0.6, 1.4) * (k == 0 ? 1.15 : 1.0);         // the finger's tip fastest
                const double el = U(58, 82) * RAD;
                const VECTOR3 v0 = rad * (sp * cos(el)) + _V(0, sp * sin(el), 0);
                const VECTOR3 p = p0 + rad * (R0 * U(0.95, 1.25));
                const double d = std::clamp(1.2e-3 * U(0.3, 1.7) * pow(140 / We, 0.15) * 3, 3e-4, 3e-3);
                Push(sc, p, v0, d, sc.rhoL, d, 0.9, 12, sc.Rliq - length(sc.aP), Flags(LOOK_DROP, ALB_DROP, F_LIQUID));
                ForeseeLanding(sc, p, v0, sc.rhoL * 0.52 * d * d * d * 2e4, 0.03);
            }
        }
    }
}

void GpuParticles::Release()
{
    landings.clear();
    pool.Reset(); dead.Reset(); alive.Reset(); newBuf.Reset(); args.Reset(); cbSim.Reset(); cbCount.Reset(); cbDraw.Reset();
    for (auto& r : countRead) r.Reset();
    poolUAV.Reset(); deadUAV.Reset(); aliveUAV.Reset(); poolSRV.Reset(); aliveSRV.Reset(); newSRV.Reset();
    csInit.Reset(); csEmit.Reset(); csSim.Reset(); vs.Reset(); ps.Reset(); bsPremul.Reset();
    std::lock_guard<std::mutex> lk(mx);
    sources.clear(); boxes.clear(); jetAcc.clear(); anch = false; needInit = true;
}

// ------------------------------------------------------------------------------------------ the exports
void GpuParticles::Emit(OBJHANDLE h, int id, const double* pos, const double* vel, double cone, double spread, double kgps,
                        int kind, int ground, double wet, double life, double jetJ)
{
    std::lock_guard<std::mutex> lk(mx);
    if (kgps <= 0) { sources.erase({ h, id }); return; }
    bool fresh = !sources.count({ h, id });
    Source& s = sources[{ h, id }];
    for (int i = 0; i < 3; i++) { s.pos[i] = pos[i]; s.vel[i] = vel[i]; }
    s.cone = cone; s.spread = std::clamp(spread, 0.0, 1.0); s.kgps = kgps; s.kind = kind; s.ground = ground;
    s.wet = std::clamp(wet, 0.0, 1.0); s.life = life; s.jetJ = jetJ; s.t = oapiGetSimTime();
    if (fresh) for (double& a : s.acc) a = Rnd();     // a short burst still gets its fraction of a sprite
}

void GpuParticles::Box(OBJHANDLE h, int id, const double* c, const double* half)
{
    std::lock_guard<std::mutex> lk(mx);
    if (half[0] <= 0 && half[1] <= 0 && half[2] <= 0) { boxes.erase({ h, id }); return; }
    BoxDef& b = boxes[{ h, id }];
    for (int i = 0; i < 3; i++) { b.c[i] = c[i]; b.h[i] = half[i]; }
}

// ------------------------------------------------------------------------------------------ spawning
void GpuParticles::Push(const ExScene& sc, const VECTOR3& p, const VECTOR3& v, double d, double rhoP, double s0, double a0,
                        double life, double gy, UINT flags, const VECTOR3& ug0, double tg)
{
    if (spawn.size() >= NEW_MAX) return;
    GPart q = {};
    q.p = F3(p); q.v = F3(v); q.age = 0; q.life = (float)life;
    q.ug0 = F3(ug0); q.tg = (float)(tg > 0 && length(ug0) > 0 ? tg : 0);
    q.d = (float)d; q.rhoP = (float)rhoP; q.s0 = (float)s0; q.a0 = (float)std::min(a0, 1.0);
    q.gy = (float)gy; q.flags = flags; q.seed = (float)Rnd(); q.gustT = 0;
    spawn.push_back(q);
}

void GpuParticles::AnchorAt(ExScene& sc)
{
    OBJHANDLE f = oapiGetFocusObject();
    VECTOR3 fp = sc.cp; if (f) oapiGetGlobalPos(f, &fp);
    VECTOR3 q = tmul(sc.Rp, fp - sc.pp);
    VECTOR3 qs = q * (sc.R / length(q));
    if (!anch || aBody != sc.hBody || length(qs - aP) > 3000) {
        aBody = sc.hBody; aP = qs; aU = Unit(qs);
        a1 = Unit(crossp(aU, fabs(aU.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0)));
        a2 = crossp(a1, aU);
        if (anch) needInit = true;      // the old frame's particles go
        anch = true;
        std::lock_guard<std::mutex> lk(mx);
        jetAcc.clear();
    }
    sc.anchored = true; sc.aP = aP; sc.a1 = a1; sc.aU = aU; sc.a2 = a2;
}

void GpuParticles::SpawnJet(ExScene& sc, const ExJet& j, JetAcc& acc, std::vector<XMFLOAT4>& imp)
{
    VESSEL* v = oapiIsVessel(j.h) ? oapiGetVesselInterface(j.h) : nullptr;
    if (!v || j.thrust <= 0) return;
    VECTOR3 vp; v->GetGlobalPos(vp);
    const double Rground = length(vp - sc.pp) - v->GetAltitude(ALTMODE_GROUND);
    const bool liquid = sc.Rliq > Rground;
    const double Rs = liquid ? sc.Rliq : Rground;
    VECTOR3 up = Unit(j.eg - sc.pp);
    const double cosd = -dotp(j.dg, up);
    if (cosd < 0.25) return;
    double hExit = length(j.eg - sc.pp) - Rs;
    if (hExit < -0.05) return;                   // the nozzle under the liquid: no spray drawn (the foam is not done)
    hExit = std::max(hExit, 0.02);
    const double t = hExit / cosd;
    const double th = (sc.dense ? 12.0 : 30.0) * RAD;
    const double r = j.rExit + t * tan(th);
    const double q = j.thrust * cosd / (3.14159265 * r * r);    // stagnation pressure over the footprint [Pa]
    const VECTOR3 hitG = j.eg + j.dg * t;
    const VECTOR3 hit = sc.ToLocal(hitG);
    const double gy = Rs - length(sc.aP);
    const double uw = sc.dense ? std::min(0.5 * sqrt(q / sc.rhoG), 80.0) : 0.0;
    if (sc.dense && imp.size() < 16) { imp.push_back(F4(hit, uw)); imp.push_back({ (float)r, 0, 0, 0 }); }
    const double dt = sc.dt;
    if (dt <= 0) return;
    VECTOR3 dL = sc.DirToLocal(j.dg);
    VECTOR3 tilt = _V(dL.x, 0, dL.z);
    const double g = sc.GM / std::max(dotp(hitG - sc.pp, hitG - sc.pp), 1.0);
    auto radial = [&](double& az) { az = U(0, 2 * 3.14159265); return _V(cos(az), 0, sin(az)); };

    if (liquid) {
        return;      // nothing over a liquid: spray, mist and splashes are not this work's (the user, 2026-10-19)
        // the liquid: the gas's shear lifts it once the stagnation pressure passes the Kelvin-Helmholtz onset
        // q_KH = 2 sqrt(g sigma rho_l); drops no larger than the gas lets them fly whole (We < 12) at the wall jet's speed
        if (!sc.dense) return;
        const double qKH = 2 * sqrt(g * sc.sigma * sc.rhoL);
        if (q <= qKH) return;
        const double ms = std::min(1e-3 * 3.14159265 * r * r * sqrt(sc.rhoL * (q - qKH)), 20.0);   // liquid lifted [kg/s]
        const double dWe = std::clamp(12 * sc.sigma / (sc.rhoG * std::max(uw * uw, 1e-6)), 5e-5, 4e-3);
        const Cls cls[4] = { { 2 * dWe, .30, .21, 0 }, { dWe, .40, .21, 0 }, { .4 * dWe, .20, .21, 0 }, { 2e-5, .10, .6, .06 } };
        for (int c = 0; c < 4; c++) {
            const double kc = ms * cls[c].frac;
            const bool mist = c == 3;
            const double M = mist ? 2e-4 : 2e-2;
            const double rate = std::clamp(kc / M, mist ? 15.0 : 4.0, mist ? 120.0 : 160.0) * JET_RATE_GPU;
            const double m = kc / rate;
            acc.acc[c] += rate * dt;
            while (acc.acc[c] >= 1) {
                acc.acc[c] -= 1;
                double az; VECTOR3 rad = radial(az);
                VECTOR3 p = hit + rad * (r * U(0.3, 1.0)) + _V(0, 0.02, 0);
                if (mist) {
                    double el = U(5, 30) * RAD, sp = uw * U(0.2, 0.6);
                    Push(sc, p, rad * (sp * cos(el)) + _V(0, sp * sin(el), 0) + tilt * (0.3 * sp), cls[c].d, sc.rhoL,
                         cls[c].s0 * U(0.7, 1.3), cls[c].a0 * U(0.7, 1.2), U(20, 40), gy, Flags(LOOK_MIST, ALB_MIST, F_LIQUID));
                } else {
                    double d = cls[c].d * U(0.7, 1.3);
                    double el = U(15, 55) * RAD, sp = uw * U(0.3, 1.0);
                    double s0 = cls[c].s0 * sqrt(m / M) * U(0.7, 1.3);
                    s0 = std::clamp(s0, 0.02, 0.4);
                    double tau = 0.6 * 1.5 * m / (sc.rhoL * d) / (2 * s0 * s0);
                    const VECTOR3 v0 = rad * (sp * cos(el)) + _V(0, sp * sin(el), 0) + tilt * (0.3 * sp);
                    Push(sc, p, v0, d, sc.rhoL, s0, std::min(0.9, 1 - exp(-tau)) * U(0.8, 1.1), 30, gy, Flags(LOOK_DROP, ALB_DROP, F_LIQUID));
                    ForeseeLanding(sc, p, v0, m, s0);
                }
            }
        }
        return;
    }

    // ground at the liquid's edge (within 0.3 m of its level) is wet: clods and wet sand, no dry dust
    const int ground = sc.Rliq > 0 && Rground - sc.Rliq < 0.3 ? (int)EXG_WET : j.ground;
    // the ground: the wall shear 0.06 q against the grains' threshold (Shao & Lu 2000, with cohesion growing with moisture)
    const double rhoP = ground == EXG_ICE ? 920.0 : sc.titan ? 1200.0 : 2650.0;
    const double dref = 1e-4, gam = 3e-4 * (1 + 20 * j.wet);
    const double tauC = 0.0123 * (rhoP * g * dref + gam / dref);
    const double E = 0.06 * q / tauC;
    if (E <= 1) return;
    // the mass eroded [kg/s]: the excess shear over the footprint, carried off by the wall jet (an erosion-rate law of the
    // Roberts / Metzger kind: m' ~ (tau - tau_c) A / u, coefficient 0.02); each sprite stands for a share of that mass,
    // its opacity from the grains' cross-section (tau = 1.5 m / (rho d) over the sprite's area)
    const double tauW = 0.06 * q;
    const double uE = sc.dense ? std::max(uw, 1.0) : 50.0;
    const double mErode = std::min(0.02 * (tauW - tauC) * 3.14159265 * r * r / uE, 2.0);
    std::pair<const Cls*, int> mix = ground == EXG_WET ? L(WET_C) : ground == EXG_REGOLITH ? L(REG_C) :
                                     ground == EXG_ICE ? L(ICE_C) : ground == EXG_LIQUID ? L(LIQ_C) : L(DRY_C);
    const int alb = ground == EXG_WET ? ALB_WET : ground == EXG_ICE ? ALB_ICEG : ground == EXG_LIQUID ? ALB_MIST : ALB_DRY;
    for (int c = 0; c < mix.second && c < 8; c++) {
        const Cls& k = mix.first[c];
        const double kc = mErode * k.frac, M = 6e-3;
        const double rate = std::clamp(kc / (0.25 * M), 8.0, 480.0);   // finer sprites, more of them: the same mass, a softer sheet
        const double m = kc / rate;
        acc.acc[c] += rate * dt;
        while (acc.acc[c] >= 1) {
            acc.acc[c] -= 1;
            double az; VECTOR3 rad = radial(az);
            VECTOR3 p = hit + rad * (r * U(0.6, 1.1)) + _V(0, 0.05, 0);
            VECTOR3 vel;
            if (sc.dense) {
                double u = uw;
                vel = rad * (u * U(0.3, 1)) + _V(0, u * U(0.05, 0.35), 0) + tilt * (0.5 * u);
            } else {          // vacuum: ballistic, low and fast, the fines fastest
                double sp = std::clamp(8 * sqrt(q), 20.0, 600.0) * U(0.3, 1) * pow(1e-4 / k.d, 0.3);
                double el = U(1, 6) * RAD;
                vel = rad * (sp * cos(el)) + _V(0, sp * sin(el), 0) + tilt * (0.3 * sp);
            }
            double life = ground == EXG_LIQUID ? U(20, 40) : k.d < 2e-5 ? U(30, 70) : 40.0;
            const double rp = ground == EXG_LIQUID ? 450.0 : rhoP;
            const double s0 = k.s0 * U(0.7, 1.3);
            const double tau = 1.5 * m / (rp * k.d) / (2 * s0 * s0);
            Push(sc, p, vel, k.d, rp, s0, std::min(0.9, 1 - exp(-tau)) * U(0.8, 1.1), life, gy,
                 Flags(ground == EXG_LIQUID ? LOOK_MIST : LOOK_GRAIN, alb, ground == EXG_LIQUID ? 0 : F_GROUNDCOL));
        }
    }
}

void GpuParticles::SpawnSource(ExScene& sc, OBJHANDLE h, Source& s, std::vector<XMFLOAT4>& fj)
{
    VESSEL* v = oapiIsVessel(h) ? oapiGetVesselInterface(h) : nullptr;
    if (!v) return;
    VECTOR3 vp; v->GetGlobalPos(vp);
    VECTOR3 pg; v->Local2Global(_V(s.pos[0], s.pos[1], s.pos[2]), pg);
    VECTOR3 velV = _V(s.vel[0], s.vel[1], s.vel[2]);
    const double speed = length(velV);
    VECTOR3 axG; v->GlobalRot(speed > 0 ? velV / speed : _V(0, -1, 0), axG);
    VECTOR3 p0 = sc.ToLocal(pg); const VECTOR3 ax = Unit(sc.DirToLocal(axG));
    VECTOR3 gs; v->GetGroundspeedVector(FRAME_GLOBAL, gs);
    const VECTOR3 gsL = sc.DirToLocal(gs);
    const double Rground = length(vp - sc.pp) - v->GetAltitude(ALTMODE_GROUND);
    const bool inLiq = sc.Rliq > Rground;
    const double gy = (inLiq ? sc.Rliq : Rground) - length(sc.aP);
    // a liquid: no dust, crystals or fog out of it or from under it (a foot in the shallows); drops start at its surface;
    // soil at its edge (within 0.3 m of its level) is wet - clods and wet sand, no dry dust
    const double hLiq = sc.Rliq > 0 ? length(pg - sc.pp) - sc.Rliq : 1e9;
    if (s.kind != 3 && hLiq < 0.05) return;
    if (s.kind == 3 && hLiq < 0.02) p0.y += 0.02 - hLiq;
    const int ground = s.kind == 0 && sc.Rliq > 0 && Rground - sc.Rliq < 0.3 ? (int)EXG_WET : s.ground;
    if (s.jetJ > 0 && sc.dense && fj.size() < 16) {
        fj.push_back(F4(p0, sqrt(s.jetJ / sc.rhoG)));
        fj.push_back(F4(ax, std::max(speed, 5.0)));
    }
    const double dt = sc.dt;
    if (dt <= 0) return;
    const bool puff = s.kind == 0 && s.life > 0;
    const double rhoP = ground == EXG_ICE ? 920.0 : sc.titan ? 1200.0 : 2650.0;
    // the mixture by kind (and ground for soil): classes; for DRY soil its wet share as the WET mix
    struct C { Cls k; int look, alb; bool hard; double rho, M; };
    std::vector<C> mix;
    auto add = [&](std::pair<const Cls*, int> a, double share, int look, int alb, double rho, double M) {
        for (int i = 0; i < a.second; i++) { Cls k = a.first[i]; k.frac *= share; mix.push_back({ k, look, alb, false, rho, M }); }
    };
    auto addWet = [&](double share) {
        if (!puff) mix.push_back({ { 0, .65 * share, 0, .97 }, LOOK_CLOD, ALB_WET, true, rhoP, 1.5e-3 });
        mix.push_back({ { 2e-4, .35 * share, .18, .25 }, LOOK_GRAIN, ALB_WET, false, rhoP, 1.5e-3 });
    };
    switch (s.kind) {
    case 1: add(L(CRYS_C), 1, LOOK_ICE, ALB_CRYSTAL, 917, 2e-4); break;
    case 2: add(L(FOG_C), 1, LOOK_FOG, ALB_FOG, 600, 2e-5); break;
    case 3: add(L(DROP_C), 1, LOOK_DROP, ALB_DROP, sc.rhoL, 2e-2); break;
    default:
        if (ground == EXG_WET) addWet(1);
        else if (ground == EXG_LIQUID) add(L(LIQ_C), 1, LOOK_MIST, ALB_MIST, 450, 1.5e-3);
        else if (ground == EXG_REGOLITH) add(L(REG_C), 1, LOOK_GRAIN, ALB_DRY, rhoP, 1.5e-3);
        else if (ground == EXG_ICE) add(L(ICE_C), 1, LOOK_GRAIN, ALB_ICEG, rhoP, 1.5e-3);
        else {
            double cl = puff ? 0 : std::clamp(2 * s.wet, 0.0, 0.9);
            if (cl > 0) addWet(cl);
            add(L(DRYF_C), 1 - cl, LOOK_GRAIN, ALB_DRY, rhoP, 1.5e-3);
        }
    }
    const double load = (double)aliveRead / POOL;
    const double capK = std::clamp((0.85 - load) / 0.35, 0.15, 1.0);
    // the basis about the axis
    VECTOR3 b1 = Unit(crossp(ax, fabs(ax.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0))), b2 = crossp(ax, b1);
    for (size_t c = 0; c < mix.size() && c < 8; c++) {
        const C& G = mix[c];
        const double kc = s.kgps * G.k.frac;
        if (kc <= 0) continue;
        const double want = kc / G.M;
        const bool longLived = !G.hard && G.k.d < 5e-5 && s.kind != 2;
        const double cap = (longLived ? 5.0 : std::max(30.0, 160.0 * G.k.frac)) * capK * 4.0;
        double rate = std::max(std::min(want, cap), std::min((longLived ? 5.0 : 20.0) * capK, 50.0 * want));
        if (G.hard) rate = std::max(rate, kc / 0.3);
        const double m = kc / rate;
        s.acc[c] += rate * dt;
        while (s.acc[c] >= 1) {
            s.acc[c] -= 1;
            const double ct = 1 - Rnd() * (1 - cos(s.cone)), st = sqrt(std::max(0.0, 1 - ct * ct)), ph = U(0, 2 * 3.14159265);
            VECTOR3 dir = ax * ct + b1 * (st * cos(ph)) + b2 * (st * sin(ph));
            double sp = speed * (1 - s.spread * Rnd());
            if (s.kind == 0 && !G.hard && G.k.d > 0) sp *= std::min(1.3, pow(2e-4 / G.k.d, 0.15));
            VECTOR3 p = p0 + _V(U(-1, 1), U(-1, 1), U(-1, 1)) * 0.03;
            if (s.jetJ > 0) p += ax * U(0.03, 0.15);        // the condensate forms a little down the jet
            VECTOR3 vel = gsL + dir * sp;
            double d = G.k.d, s0, a0, life, rho = G.rho;
            UINT fl = Flags(G.look, G.alb, (inLiq ? F_LIQUID : 0) | (s.kind == 0 ? F_GROUNDCOL : 0));
            if (G.hard) {
                const double rc = 0.65 * rhoP + (sc.titan ? 160 : 350);
                d = std::min(0.08, cbrt(6 * m / (3.14159265 * rc))) * U(0.75, 1.25);
                s0 = 0.62 * d; a0 = 0.97; life = 60; rho = rc; fl |= F_HARD;
            } else {
                if (s.kind == 3 && sc.dense) d = std::max(5e-5, std::min(d, 12 * sc.sigma / (sc.rhoG * std::max(dotp(vel, vel), 1e-6)))) * U(0.7, 1.3);
                s0 = G.k.s0 * sqrt(m / G.M) * U(0.7, 1.3);
                s0 = std::clamp(s0, 0.005, 1.0);
                if (s.kind == 0 || s.kind == 3) {
                    double tau = 1.5 * m / (rho * d) / (2 * s0 * s0) * (s.kind == 3 ? 0.6 : 1.0);
                    a0 = std::min(0.9, 1 - exp(-tau)) * U(0.8, 1.1);
                } else a0 = G.k.a0 * U(0.7, 1.2);
                life = s.kind == 2 ? std::max(0.2, s.life) * U(0.6, 1.3) : s.kind == 1 ? (s.life > 0 ? s.life * U(0.7, 1.3) : U(20, 40)) :
                       s.kind == 3 ? 30.0 : d < 2e-5 ? U(30, 70) : 40.0;
            }
            Push(sc, p, vel, d, rho, s0, a0, life, gy, fl, puff ? dir * sp : _V(0, 0, 0), puff ? s.life : 0);
            if (s.kind == 3 && sc.Rliq > 0) ForeseeLanding(sc, p, vel, m, s0);
        }
    }
}

void GpuParticles::UpdateBoxes(const ExScene& sc)
{
    boxCB.clear(); nBox = 0;
    std::vector<std::pair<double, std::array<XMFLOAT4, 4>>> list;
    for (auto& kv : boxes) {
        OBJHANDLE h = kv.first.h;
        if (!oapiIsVessel(h)) continue;
        VESSEL* v = oapiGetVesselInterface(h);
        VECTOR3 cg; v->Local2Global(_V(kv.second.c[0], kv.second.c[1], kv.second.c[2]), cg);
        VECTOR3 c = sc.ToLocal(cg);
        double dist = length(c - _V(0, length(sc.aP) - sc.R, 0));
        if (dist > 2000) continue;
        std::array<XMFLOAT4, 4> b;
        b[0] = F4(c, 0);
        for (int i = 0; i < 3; i++) {
            VECTOR3 e = _V(i == 0, i == 1, i == 2), eg; v->GlobalRot(e, eg);
            b[1 + i] = F4(Unit(sc.DirToLocal(eg)), kv.second.h[i]);
        }
        list.push_back({ dist, b });
    }
    std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& e : list) { if (nBox >= 16) break; for (auto& f : e.second) boxCB.push_back(f); nBox++; }
}

// ------------------------------------------------------------------------------------------ the frame's update
void GpuParticles::Update(D3D11Client* gc, ExScene& sc, const std::vector<ExJet>& jets)
{
    ID3D11DeviceContext* ctx = gc->Ctx();
    skip = false;
    AnchorAt(sc);
    // the rings of drops that land in the liquid now
    for (size_t i = 0; i < landings.size();) {
        if (landings[i].due > sc.simt) { i++; continue; }
        if (sc.hBody && sc.Rliq > 0) WakeRipple(sc.hBody, sc.LocalToGlobal(landings[i].l), landings[i].r, landings[i].amp);
        landings[i] = landings.back(); landings.pop_back();
    }
    spawn.clear();
    std::vector<XMFLOAT4> imp, fj;
    {
        std::lock_guard<std::mutex> lk(mx);
        // sources not refreshed for 0.5 s of simulation time, or whose vessel is gone, are dropped
        for (auto it = sources.begin(); it != sources.end();) {
            if (!oapiIsVessel(it->first.h) || sc.simt - it->second.t > 0.5) it = sources.erase(it); else ++it;
        }
        for (auto it = boxes.begin(); it != boxes.end();) { if (!oapiIsVessel(it->first.h)) it = boxes.erase(it); else ++it; }
        for (const ExJet& j : jets) {
            JetAcc& a = jetAcc[{ j.h, j.id }];
            if (a.t == 0) for (double& x : a.acc) x = Rnd();
            a.t = sc.simt;
            SpawnJet(sc, j, a, imp);
        }
        for (auto it = jetAcc.begin(); it != jetAcc.end();) { if (sc.simt - it->second.t > 1.0) it = jetAcc.erase(it); else ++it; }
        for (auto& kv : sources) SpawnSource(sc, kv.first.h, kv.second, fj);
        SpawnCrowns(sc);
        UpdateBoxes(sc);
    }
    spawnedLast = (UINT)spawn.size();
    frame++;

    SimCB k = {};
    const double dt = std::clamp(sc.dt, 0.0, 0.16);
    const int ns = dt > 0 ? std::min(8, (int)ceil(dt / 0.02)) : 0;
    simTime += sc.dt;
    k.time = { (float)sc.dt, ns ? (float)(dt / ns) : 0.0f, (float)ns, (float)fmod(simTime, 10000.0) };
    const double gAcc = sc.GM / (sc.R * sc.R);
    k.gas = { (float)sc.rhoG, (float)sc.mu, sc.dense ? 1.0f : 0.0f, (float)gAcc };
    k.geo = { (float)length(sc.aP), (float)spawn.size(), (float)(imp.size() / 2), (float)(fj.size() / 2) };
    k.misc = { (float)nBox, (float)frame, 0, 0 };
    {   // the particles' frame -> camera-centred global, the camera's view-projection, the liquid; and the scene so far for
        // the ground's own colour under the newborn grains (GroundAlbedo in CSEmit)
        VECTOR3 c0 = mul(sc.Rp, sc.a1), c1 = mul(sc.Rp, sc.aU), c2 = mul(sc.Rp, sc.a2);
        VECTOR3 o = mul(sc.Rp, sc.aP) + sc.pp - sc.cp;
        k.m0 = { (float)c0.x, (float)c1.x, (float)c2.x, (float)o.x };
        k.m1 = { (float)c0.y, (float)c1.y, (float)c2.y, (float)o.y };
        k.m2 = { (float)c0.z, (float)c1.z, (float)c2.z, (float)o.z };
        XMStoreFloat4x4(&k.vp, XMMatrixTranspose(g_exViewProj));
        bool groundCol = false;
        for (const GPart& q : spawn) if (q.flags & F_GROUNDCOL) { groundCol = true; break; }
        if (groundCol && gc->hdrCopy && gc->hdrCopySRV) { gc->CopySceneColour(gc->hdrCopy.Get()); sceneCopy = true; } else sceneCopy = false;
        k.liq = { (float)sc.sigma, (float)sc.rhoL, sc.Rliq > 0 ? (float)(sc.Rliq - length(sc.aP)) : -1e9f, sceneCopy ? 1.0f : 0.0f };
    }
    for (size_t i = 0; i < imp.size() && i < 16; i++) k.imp[i] = imp[i];
    for (size_t i = 0; i < fj.size() && i < 16; i++) k.fr[i] = fj[i];
    for (size_t i = 0; i < boxCB.size() && i < 64; i++) k.box[i] = boxCB[i];
    D3D11_MAPPED_SUBRESOURCE m;
    ctx->Map(cbSim.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, &k, sizeof k); ctx->Unmap(cbSim.Get(), 0);

    ID3D11UnorderedAccessView* nulU[3] = {};
    ID3D11ShaderResourceView* nulS[1] = {};
    ctx->CSSetConstantBuffers(5, 1, cbSim.GetAddressOf());
    if (needInit) {
        ID3D11UnorderedAccessView* u[2] = { poolUAV.Get(), deadUAV.Get() };
        UINT c[2] = { (UINT)-1, 0 };
        ctx->CSSetUnorderedAccessViews(0, 2, u, c);
        ctx->CSSetShader(csInit.Get(), nullptr, 0);
        ctx->Dispatch(POOL / 256, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 2, nulU, nullptr);
        needInit = false;
        aliveRead = 0;
    }
    if (!spawn.empty()) {
        ctx->Map(newBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, spawn.data(), spawn.size() * sizeof(GPart)); ctx->Unmap(newBuf.Get(), 0);
        ctx->CopyStructureCount(cbCount.Get(), 0, deadUAV.Get());
        ID3D11UnorderedAccessView* u[2] = { poolUAV.Get(), deadUAV.Get() };
        UINT c[2] = { (UINT)-1, (UINT)-1 };
        ctx->CSSetUnorderedAccessViews(0, 2, u, c);
        ctx->CSSetShaderResources(4, 1, newSRV.GetAddressOf());
        ID3D11ShaderResourceView* sc3 = sceneCopy ? gc->hdrCopySRV.Get() : nullptr;   // the scene (t3): the ground's colour
        ctx->CSSetShaderResources(3, 1, &sc3);
        ctx->CSSetConstantBuffers(6, 1, cbCount.GetAddressOf());
        ctx->CSSetShader(csEmit.Get(), nullptr, 0);
        ctx->Dispatch((UINT)(spawn.size() + 63) / 64, 1, 1);
        ctx->CSSetShaderResources(4, 1, nulS);
        ctx->CSSetShaderResources(3, 1, nulS);
        ctx->CSSetUnorderedAccessViews(0, 2, nulU, nullptr);
    }
    {
        ID3D11UnorderedAccessView* u[3] = { poolUAV.Get(), deadUAV.Get(), aliveUAV.Get() };
        UINT c[3] = { (UINT)-1, (UINT)-1, 0 };
        ctx->CSSetUnorderedAccessViews(0, 3, u, c);
        ctx->CSSetShader(csSim.Get(), nullptr, 0);
        ctx->Dispatch(POOL / 256, 1, 1);
        ctx->CSSetUnorderedAccessViews(0, 3, nulU, nullptr);
        ctx->CSSetShader(nullptr, nullptr, 0);
    }
    ctx->CopyStructureCount(args.Get(), 4, aliveUAV.Get());
    // the alive count for the log and the sources' load (read three frames late, no stall)
    ctx->CopyStructureCount(countRead[frame % 3].Get(), 0, aliveUAV.Get());
    if (frame >= 3 && ctx->Map(countRead[(frame + 1) % 3].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) == S_OK) {
        aliveRead = *(const UINT*)m.pData;
        ctx->Unmap(countRead[(frame + 1) % 3].Get(), 0);
    }
}

// ------------------------------------------------------------------------------------------ drawing
void GpuParticles::Render(D3D11Client* gc, const ExScene& sc, ID3D11ShaderResourceView* depth)
{
    if (skip || !anch || (aliveRead == 0 && spawnedLast == 0 && frame > 4)) return;
    ID3D11DeviceContext* ctx = gc->Ctx();
    DrawCB k = {};
    // local -> camera-centred global: Rp [a1 aU a2] and the offset Rp aP + pp - cp
    VECTOR3 c0 = mul(sc.Rp, sc.a1), c1 = mul(sc.Rp, sc.aU), c2 = mul(sc.Rp, sc.a2);
    VECTOR3 o = mul(sc.Rp, sc.aP) + sc.pp - sc.cp;
    k.m0 = { (float)c0.x, (float)c1.x, (float)c2.x, (float)o.x };
    k.m1 = { (float)c0.y, (float)c1.y, (float)c2.y, (float)o.y };
    k.m2 = { (float)c0.z, (float)c1.z, (float)c2.z, (float)o.z };
    MATRIX3 cr; oapiCameraRotationMatrix(&cr);
    k.camR = { (float)cr.m11, (float)cr.m21, (float)cr.m31, (float)fmod(simTime, 10000.0) };
    k.camU = { (float)cr.m12, (float)cr.m22, (float)cr.m32, 0 };
    const float focal = (float)(0.5 * gc->SceneH() / std::max(oapiCameraAperture() > 0 ? tan(oapiCameraAperture()) : 1.0, 1e-3));
    k.geo = { (float)length(sc.aP), sc.dense ? 1.0f : 0.0f, sc.Rliq > 0 ? (float)(sc.Rliq - length(sc.aP)) : -1e9f, focal };   // z: the liquid's level (local)
    XMStoreFloat4x4(&k.vp, XMMatrixTranspose(g_exViewProj));
    // albedo by body and class (after the AtmoFX fork's table)
    VECTOR3 dry = _V(0.18, 0.17, 0.16);
    char nm[64] = ""; if (sc.hBody) oapiGetObjectName(sc.hBody, nm, 63);
    if (!strcmp(nm, "Titan")) dry = _V(0.78, 0.80, 0.90);
    else if (!strcmp(nm, "Moon")) dry = _V(0.15, 0.145, 0.14);
    else if (!strcmp(nm, "Mars")) dry = _V(0.36, 0.20, 0.11);
    else if (!strcmp(nm, "Earth")) dry = _V(0.46, 0.40, 0.31);
    else if (!strcmp(nm, "Io")) dry = _V(0.42, 0.38, 0.18);
    k.alb[ALB_DRY] = F4(dry, 1); k.alb[ALB_WET] = F4(dry * 0.6, 1); k.alb[ALB_ICEG] = { 0.80f, 0.82f, 0.86f, 1 };
    // a cloud of clear drops scatters nearly all it takes (single-scattering albedo ~1, as white water and spray): light,
    // the liquid's own faint tint (Titan's methane carries dissolved organics: a little yellow)
    k.alb[ALB_DROP] = sc.titan ? XMFLOAT4{ 0.86f, 0.82f, 0.70f, 1 } : XMFLOAT4{ 0.90f, 0.91f, 0.92f, 1 };
    k.alb[ALB_FOG] = { 0.86f, 0.87f, 0.88f, 1 };
    k.alb[ALB_CRYSTAL] = { 0.90f, 0.92f, 0.95f, 1 }; k.alb[ALB_MIST] = { 0.85f, 0.85f, 0.85f, 1 };
    D3D11_MAPPED_SUBRESOURCE m;
    ctx->Map(cbDraw.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); memcpy(m.pData, &k, sizeof k); ctx->Unmap(cbDraw.Get(), 0);

    FxBindLight(gc);                         // the sky's tables, whatever a pass before left in their slots
    ctx->OMSetBlendState(bsPremul.Get(), nullptr, ~0u);
    ctx->RSSetState(gc->rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->PSSetShader(ps.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(5, 1, cbDraw.GetAddressOf());
    ctx->PSSetConstantBuffers(5, 1, cbDraw.GetAddressOf());
    ID3D11ShaderResourceView* vsr[2] = { aliveSRV.Get(), poolSRV.Get() };
    ctx->VSSetShaderResources(3, 2, vsr);
    // the light in the vertex shader: the client's shadow map (t7, s2) and the FX atmosphere's light (t8, t9, b4) as the
    // pixel shaders have them
    ID3D11ShaderResourceView* lt[3] = {}; ID3D11SamplerState* ss = nullptr; ID3D11Buffer* b4 = nullptr;
    ID3D11ShaderResourceView* vlt[3] = {}; ID3D11SamplerState* vss = nullptr; ID3D11Buffer* vb4 = nullptr;   // the VS's own, put back after
    ctx->VSGetShaderResources(7, 3, vlt); ctx->VSGetSamplers(2, 1, &vss); ctx->VSGetConstantBuffers(4, 1, &vb4);
    ctx->PSGetShaderResources(7, 3, lt); ctx->PSGetSamplers(2, 1, &ss); ctx->PSGetConstantBuffers(4, 1, &b4);
    ctx->VSSetShaderResources(7, 3, lt); ctx->VSSetSamplers(2, 1, &ss); ctx->VSSetConstantBuffers(4, 1, &b4);
    ctx->PSSetShaderResources(5, 1, &depth);
    ctx->DrawInstancedIndirect(args.Get(), 0);
    ID3D11ShaderResourceView* nul[2] = {};
    ctx->VSSetShaderResources(3, 2, nul);
    ctx->VSSetShaderResources(7, 3, vlt); ctx->VSSetSamplers(2, 1, &vss); ctx->VSSetConstantBuffers(4, 1, &vb4);
    for (auto* r : vlt) if (r) r->Release();
    if (vss) vss->Release();
    if (vb4) vb4->Release();
    for (auto* r : lt) if (r) r->Release();
    if (ss) ss->Release();
    if (b4) b4->Release();
}
