// Waves on liquids (Titan's seas). The modules place wake nodes on their parts (AtmoFX_SetWake: a point on the vessel,
// its radius, a strength; strength > 1 = an impulse, 0 = removed). Two models shape the surface, as in the AtmoFX D3D9
// fork (written for this client):
//  - the wave field (shaders\wavefield.hlsl): a window of N x N cells on the level around the focus vessel where each
//    node is a moving hollow (depth 0.9 r s - on Titan a wading leg's bow stands ~U^2/2g high: a few cm, ramped in over 2 s): every frame it is lifted where it was and pressed where
//    it is, so a part at rest makes nothing and a moving one makes its own bow and wake; a node that goes away lets its
//    hollow fill and run out as waves; impulses (steps, splashes) kick the surface's velocity. The equation follows the
//    deep-liquid dispersion, so a push sends out a train of waves, not a ring; the engines' jets press their cavities
//    into it (JetCavity) and anything falling in kicks it by its speed (the impacts);
//  - the Kelvin wake of a part moving faster than the waves (the linear deep-water solution for a moving pressure patch,
//    in units of the wavelength lambda = 2 pi U^2 / g: Textures\AtmoFX\kelvin_wake.png, rg = slope along the motion and
//    across, 0.5 = 0), summed per pixel by the liquid shader.
#include "Wake.h"
#include <cmath>
#include <set>
#include <DirectXPackedVector.h>
using DirectX::PackedVector::XMConvertHalfToFloat;
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace {

struct Node
{
    VECTOR3 pos; double r = 0, s = 0;
    bool impulse = false, dead = false; double amp = 0;
    double applied = 0; double px = 0, py = 0; bool hasPrev = false;     // the field: the hollow pressed last frame, where
};
struct Kick { OBJHANDLE body; VECTOR3 g; double r, amp; };              // a splash to put into the field (global position)

std::mutex mx;
std::map<std::pair<OBJHANDLE, int>, Node> nodes;
std::vector<Kick> kicks;

// ---- the field
const int FN = 1024;
struct Field
{
    bool tried = false, ok = false, active = false; unsigned gen = 0;
    ComPtr<ID3D11ComputeShader> csStamp, csSim, csShift, csSlope;
    ComPtr<ID3D11Texture2D> hv[2], slope;
    ComPtr<ID3D11ShaderResourceView> hvSRV[2], slopeSRV;
    ComPtr<ID3D11UnorderedAccessView> hvUAV[2], slopeUAV;
    ComPtr<ID3D11Buffer> cb;
    int cur = 0;
    OBJHANDLE body = nullptr, focus = nullptr;
    VECTOR3 C = { 0, 0, 0 }, a1 = { 0, 0, 0 }, a2 = { 0, 0, 0 };     // the window's centre on the level and axes (planet frame)
    double dx = 0.125, acc = 0;
} F;

struct FieldCB { float p[4], k[4]; int shift[4]; XMFLOAT4 stamp[64]; float ker[172]; XMFLOAT4 jet[8]; };

// the liquid (Titan's methane-ethane, as the particles take it: ExhaustInternal.h)
const double RHO_LIQ = 520.0;

// A gas jet on a liquid: the cavity it holds (Banks & Chandrasekhara 1963, the steelmaking lance on a melt). A round jet's
// centreline stagnation pressure falls as p_c = M / (pi (0.11 z)^2) at a distance z from its nozzle (momentum M kept, the
// Gaussian velocity profile's half-width 0.11 z - whatever the jet's density, which only moves the potential core), and at
// most the exit's own 1/2 rho u0^2 = M / (2 pi r0^2); the cavity's floor sits where that pressure equals the liquid's head:
// rho_l g n0 = p_c(x + n0), x the nozzle's height along the jet. The pressure's profile is u^2's Gaussian: sigma = 0.078 z.
// Out: the head at the centre [m] and sigma [m]
void JetCavity(double M, double x, double r0, double g, double& head, double& sigma)
{
    const double rg = RHO_LIQ * g, pExit = M / (2 * PI * r0 * r0);
    auto pc = [&](double z) { return std::min(M / (PI * 0.0121 * z * z), pExit); };
    double lo = 0, hi = 50;                          // n0: rho g n0 - p_c(x + n0) rises with n0
    for (int i = 0; i < 40; i++) { const double m = 0.5 * (lo + hi); (rg * m < pc(x + m) ? lo : hi) = m; }
    const double z = x + 0.5 * (lo + hi);
    head = pc(z) / rg;
    sigma = std::max(0.078 * z, 0.7 * r0);
}

std::map<OBJHANDLE, double> lowPrev;   // the vessels' lowest point over the level last frame (the impacts)

// An impact's air cavity (Glasheen & McMahon 1996, discs entering water vertically): the body goes in at U and drags an
// open cavity of about its own radius behind it, slowed by the drag of the liquid it pushes, 1/2 rho Cd pi R^2 v^2
// (Cd ~0.8), so its depth is ln(1 + k U t) / k with k = rho Cd pi R^2 / (2 m) - down to the bed at most; the hydrostatic
// pressure closes it at t = 2.285 sqrt(R / g) (independent of U), and the liquid then rushes in: the collapse throws up the
// Worthington jet in the middle and runs out as rings - the field does that by itself once the cavity lets go.
// The cavity goes along with the body (an oblique entry, a run into the liquid): its trail closes behind it.
// What the surface shows of it: the deep air cavity's neck is narrow (R) and mostly hidden; the open crater at the level
// holds the same volume pi R^2 L but no deeper than wide - sigma = cbrt(R^2 L), depth sigma (a height field can carry
// walls no steeper; a narrow pit 2 m deep in it was a forest of spikes, 2026-10-20).
// The liquid the cavity pushes aside rises round it (the crater's volume as it grows, put in as a wider hump - the cavity
// keeps the middle down): the rim. The crown's sheet: at the entry the liquid round the final crater is thrown up at half
// the entry speed (less for a light body: its added mass ~4/3 rho R^3 takes the momentum), at most what linear waves of
// that width carry (2 sqrt(g sigma)); it rises, falls back and runs out as the first ring.
struct Impact { OBJHANDLE body, hv; VECTOR3 off; double R, U, k, dMax, t0, tSeal, V; };
std::vector<Impact> impacts;
float g_ker[169];      // iWave's kernel for sqrt(-laplacian), 13 x 13 cells (FieldInit)

// Tessendorf's iWave kernel: G(r) = sum q^2 exp(-q^2) J0(q r), then made exact where it matters: no response to a
// level surface (the sum is 0) and the response to a long wave equal to its wavenumber (k = 0.3 per cell)
void BuildKernel()
{
    std::map<int, double> byR2;
    for (int r2 = 0; r2 <= 72; r2++) {
        double s = 0; const double r = sqrt((double)r2);
        for (int n = 1; n <= 10000; n++) { const double q = n * 0.001; s += q * q * exp(-q * q) * std::cyl_bessel_j(0.0, q * r); }
        byR2[r2] = s;
    }
    double sum = 0;
    for (int j = -6; j <= 6; j++) for (int i = -6; i <= 6; i++) { double v = byR2[i * i + j * j]; g_ker[(j + 6) * 13 + i + 6] = (float)v; sum += v; }
    g_ker[6 * 13 + 6] -= (float)sum;                          // a level surface stays level
    double resp = 0; const double k = 0.3;
    for (int j = -6; j <= 6; j++) for (int i = -6; i <= 6; i++) resp += g_ker[(j + 6) * 13 + i + 6] * cos(k * i);
    const double scale = resp != 0 ? k / resp : 1;             // the operator's symbol: +k (w^2 = g k)
    for (float& v : g_ker) v = (float)(v * scale);
}

bool FieldInit(D3D11Client* c)
{
    if (F.tried && F.gen == g_devGen) return F.ok;
    F = Field(); F.gen = g_devGen;     // (a new device: the old one's objects go)
    F.tried = true;
    ID3D11Device* dev = c->Dev();
    std::string path = c->ShaderDir() + "wavefield.hlsl"; std::wstring wp(path.begin(), path.end());
    auto cs = [&](const char* e, ComPtr<ID3D11ComputeShader>& out) {
        ComPtr<ID3DBlob> code, err;
        if (FAILED(D3DCompileFromFile(wp.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, e, "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
            LOGC("wavefield.hlsl %s: %s", e, err ? (const char*)err->GetBufferPointer() : "not found"); return false;
        }
        return SUCCEEDED(dev->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &out));
    };
    if (!cs("CSStamp", F.csStamp) || !cs("CSSim", F.csSim) || !cs("CSShift", F.csShift) || !cs("CSSlope", F.csSlope)) return false;
    D3D11_TEXTURE2D_DESC td = { FN, FN, 1, 1, DXGI_FORMAT_R32G32_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
    for (int i = 0; i < 2; i++) {
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &F.hv[i]))) return false;
        dev->CreateShaderResourceView(F.hv[i].Get(), nullptr, &F.hvSRV[i]);
        dev->CreateUnorderedAccessView(F.hv[i].Get(), nullptr, &F.hvUAV[i]);
    }
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &F.slope))) return false;
    dev->CreateShaderResourceView(F.slope.Get(), nullptr, &F.slopeSRV);
    dev->CreateUnorderedAccessView(F.slope.Get(), nullptr, &F.slopeUAV);
    BuildKernel();
    D3D11_BUFFER_DESC bd = { (UINT)((sizeof(FieldCB) + 15) & ~15u), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    dev->CreateBuffer(&bd, nullptr, &F.cb);
    F.ok = F.cb != nullptr;
    LOGC("wave field %s (%dx%d)", F.ok ? "ready" : "NOT available", FN, FN);
    return F.ok;
}

void Run(ID3D11DeviceContext* ctx, ID3D11ComputeShader* s, const FieldCB& c, bool toSlope)
{
    ctx->UpdateSubresource(F.cb.Get(), 0, nullptr, &c, 0, 0);
    ctx->CSSetShader(s, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, F.cb.GetAddressOf());
    ctx->CSSetShaderResources(0, 1, F.hvSRV[F.cur].GetAddressOf());
    ID3D11UnorderedAccessView* u[2] = { toSlope ? nullptr : F.hvUAV[1 - F.cur].Get(), toSlope ? F.slopeUAV.Get() : nullptr };
    ctx->CSSetUnorderedAccessViews(0, 2, u, nullptr);
    ctx->Dispatch(FN / 16, FN / 16, 1);
    ID3D11UnorderedAccessView* nu[2] = {}; ID3D11ShaderResourceView* ns = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 2, nu, nullptr);
    ctx->CSSetShaderResources(0, 1, &ns);
    if (!toSlope) F.cur = 1 - F.cur;
}

// a global point on the level, in the window's cells
void ToCells(const VECTOR3& gq, const VECTOR3& pp, const MATRIX3& Rp, double Rl, double& x, double& y, double& depth)
{
    VECTOR3 q = tmul(Rp, gq - pp);
    const double rq = length(q);
    depth = Rl - rq;
    VECTOR3 ql = q * (Rl / rq) - F.C;
    x = dotp(ql, F.a1) / F.dx + FN * 0.5;
    y = dotp(ql, F.a2) / F.dx + FN * 0.5;
}

}  // namespace

void WakeSet(OBJHANDLE hv, int id, const double* pos, double radius, double strength)
{
    std::lock_guard<std::mutex> lk(mx);
    auto key = std::make_pair(hv, id);
    auto it = nodes.find(key);
    if (strength <= 0 || radius <= 0) {
        if (it != nodes.end()) it->second.dead = true;     // its hollow fills (the field) before it goes
        return;
    }
    Node& n = nodes[key];
    n.pos = _V(pos[0], pos[1], pos[2]); n.r = radius; n.s = std::min(strength, 1.0); n.dead = false;
    if (strength > 1.0) {
        n.impulse = true; n.amp = std::min(strength, 4.0);
        // the impact's crown (a step, a wheel or a hull going in): its speed from the strength (2..3 -> ~1..1.5 m/s)
        if (oapiIsVessel(hv)) {
            VESSEL* v = oapiGetVesselInterface(hv);
            VECTOR3 g; v->Local2Global(n.pos, g);
            ParticlesCrown(v->GetSurfaceRef(), g, radius, 0.5 * std::min(strength, 4.0));
        }
    }
}

void WakeForget(OBJHANDLE hv)
{
    std::lock_guard<std::mutex> lk(mx);
    for (auto it = nodes.begin(); it != nodes.end();) it = it->first.first == hv ? nodes.erase(it) : std::next(it);
}

void WakeRipple(OBJHANDLE body, const VECTOR3& gpos, double radius, double amp)
{
    std::lock_guard<std::mutex> lk(mx);
    if (kicks.size() < 256) kicks.push_back({ body, gpos, radius, std::min(amp, 4.0) });
}

// the field's step for this frame: the window follows the focus vessel, the nodes press, the kicks go in, the waves run
bool WakeField(D3D11Client* c, OBJHANDLE body, double Rl, const VECTOR3& cam, double simDt, WakeFieldView& view)
{
    view.on = false;
    if (!FieldInit(c)) return false;
    ID3D11DeviceContext* ctx = c->Ctx();
    std::lock_guard<std::mutex> lk(mx);
    VECTOR3 pp; oapiGetGlobalPos(body, &pp);
    MATRIX3 Rp; oapiGetRotationMatrix(body, &Rp);
    const double g = GGRAV * oapiGetMass(body) / (Rl * Rl);
    // the focus vessel at the liquid: the window around it
    OBJHANDLE fo = oapiGetFocusObject();
    VESSEL* fv = fo && oapiIsVessel(fo) ? oapiGetVesselInterface(fo) : nullptr;
    if (!fv || fv->GetSurfaceRef() != body) { F.active = false; return false; }
    VECTOR3 fg; fv->GetGlobalPos(fg);
    VECTOR3 fq = tmul(Rp, fg - pp); const double fr = length(fq);
    if (fr - Rl > 200 || length(fg - cam) > 3000) { F.active = false; return false; }
    const double S = std::clamp(10 * fv->GetSize(), 32.0, 96.0), dx = S / FN;     // ~3-9 cm cells
    const VECTOR3 fl = fq * (Rl / fr);
    FieldCB cb = {};
    cb.p[0] = (float)FN;
    auto clearAll = [&]() {
        float z[4] = { 0, 0, 0, 0 };
        for (int i = 0; i < 2; i++) ctx->ClearUnorderedAccessViewFloat(F.hvUAV[i].Get(), z);
        ctx->ClearUnorderedAccessViewFloat(F.slopeUAV.Get(), z);
        for (auto& kv : nodes) { kv.second.applied = 0; kv.second.hasPrev = false; }
    };
    if (!F.active || F.body != body || F.focus != fo || fabs(F.dx - dx) > 1e-6 || length(fl - F.C) > S) {
        F.active = true; F.body = body; F.focus = fo; F.dx = dx; F.C = fl; F.acc = 0;
        const VECTOR3 n = fl / Rl;
        VECTOR3 a = fabs(n.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0);
        F.a1 = crossp(n, a); F.a1 = F.a1 / length(F.a1);
        F.a2 = crossp(n, F.a1);
        clearAll();
    }
    cb.p[1] = (float)dx;
    // re-centre by whole cells when the focus has gone an eighth of the window off the centre
    {
        const VECTOR3 d = fl - F.C;
        const int di = (int)lround(dotp(d, F.a1) / dx), dj = (int)lround(dotp(d, F.a2) / dx);
        if (abs(di) > FN / 8 || abs(dj) > FN / 8) {
            cb.shift[0] = di; cb.shift[1] = dj;
            Run(ctx, F.csShift.Get(), cb, false);
            F.C = F.C + (F.a1 * di + F.a2 * dj) * dx;
            F.C = F.C * (Rl / length(F.C));
            const VECTOR3 n = F.C / Rl;
            F.a1 = F.a1 - n * dotp(F.a1, n); F.a1 = F.a1 / length(F.a1);
            F.a2 = crossp(n, F.a1);
            for (auto& kv : nodes) { kv.second.px -= di; kv.second.py -= dj; }
            cb.shift[0] = cb.shift[1] = 0;
        }
    }
    // the stamps: the nodes' hollows (lifted where they were, pressed where they are), the kicks
    int ns = 0;
    auto add = [&](double x, double y, double a, double sigmaCells, bool velocity) {
        if (ns >= 64 || fabs(a) < 1e-7) return;
        if (x < -8 || y < -8 || x > FN + 8 || y > FN + 8) return;
        cb.stamp[ns++] = { (float)x, (float)y, (float)a, (float)(velocity ? -sigmaCells : sigmaCells) };
    };
    const double dtReal = std::max(simDt, 0.0);
    {   // test runs (D3D11CLIENT_TEST_WAVES=1): a part walking straight through the liquid 3 m beside the focus vessel
        static int test = -1;
        if (test < 0) { char v[8] = {}; test = GetEnvironmentVariableA("D3D11CLIENT_TEST_WAVES", v, sizeof v) && v[0] == '1' ? 1 : 0; }
        if (test == 1) {
            const double t = oapiGetSimTime();
            Node& n = nodes[std::make_pair(fo, 99999)];
            n.pos = _V(-10 + fmod(t * 1.2, 20.0), -0.95, 3.0); n.r = 0.15; n.s = 1; n.dead = false;   // straight, 1.2 m/s
        }
    }
    for (auto it = nodes.begin(); it != nodes.end();) {
        OBJHANDLE hv = it->first.first; Node& n = it->second;
        if (!oapiIsVessel(hv)) { it = nodes.erase(it); continue; }
        VESSEL* v = oapiGetVesselInterface(hv);
        const double sig = std::max(0.6 * n.r, 1.5 * dx) / dx;
        if (v->GetSurfaceRef() != body) { ++it; continue; }
        VECTOR3 gq; v->Local2Global(n.pos, gq);
        double x, y, depth; ToCells(gq, pp, Rp, Rl, x, y, depth);
        // only in the liquid: the ground under the part must lie under the level (no waves on the shore)
        double glng, glat, grad; oapiGlobalToEqu(body, gq, &glng, &glat, &grad);
        const double water = (Rl - oapiGetSize(body)) - oapiSurfaceElevation(body, glng, glat);
        const double wet = std::clamp((water - 0.02) / 0.05, 0.0, 1.0);
        if (n.impulse) {      // a step, a fall: the surface pushed down
            if (depth > -n.r && wet > 0) add(x, y, -0.5 * n.amp * sqrt(g * n.r) * wet, std::max(n.r, 1.5 * dx) / dx, true);
            n.impulse = false;
        }
        if (n.dead) {         // gone: its hollow fills and runs out as waves
            if (n.hasPrev && n.applied > 0) add(n.px, n.py, n.applied, sig, false);
            it = nodes.erase(it); continue;
        }
        const double sub = std::clamp((depth + n.r) / (2 * n.r), 0.0, 1.0) * wet;
        // a part at rest still bobs (breath, sway): its hollow breathes and sends out slow rings; a moving one adds its
        // bow and wake on top
        const double phase = (double)((uintptr_t)it->first.second * 2654435761u % 1000) * 0.00628;
        const double A = 0.9 * n.r * n.s * sub * (1.0 + 0.25 * sin(oapiGetSimTime() * 2 * PI / 1.6 + phase));
        const double Anew = A > n.applied ? std::min(A, n.applied + A * dtReal / 2.0) : A;    // ramped in over 2 s
        if (n.hasPrev && n.applied > 0) add(n.px, n.py, n.applied, sig, false);
        add(x, y, -Anew, sig, false);
        n.applied = Anew; n.px = x; n.py = y; n.hasPrev = true;
        ++it;
    }
    for (const Kick& k : kicks) {
        if (k.body != body) continue;
        double x, y, depth; ToCells(k.g, pp, Rp, Rl, x, y, depth);
        add(x, y, -0.5 * k.amp * sqrt(g * k.r), std::max(k.r, 1.5 * dx) / dx, true);
    }
    kicks.clear();
    // things falling in (any vessel over the liquid): where its lowest touchdown point crosses the level going down, the
    // impact - its air cavity (Impact) held in the field while it opens, then let go; the crown's sheet throws its drops
    // (ParticlesCrown: from the Weber number)
    {
        std::map<OBJHANDLE, double> seen;
        const VECTOR3 Cg = pp + mul(Rp, F.C);
        for (DWORD i = 0, nv = oapiGetVesselCount(); i < nv; i++) {
            OBJHANDLE hv = oapiGetVesselByIndex(i);
            VESSEL* v = hv ? oapiGetVesselInterface(hv) : nullptr;
            if (!v || v->GetSurfaceRef() != body) continue;
            VECTOR3 vg; v->GetGlobalPos(vg);
            if (length(vg - Cg) > 0.5 * S + v->GetSize()) continue;          // outside the window
            const DWORD nt = v->GetTouchdownPointCount();
            if (nt == 0) continue;
            double low = 1e9; VECTOR3 lowG = vg;
            std::vector<VECTOR3> tg(nt);
            std::vector<double> th(nt);
            for (DWORD t = 0; t < nt; t++) {
                TOUCHDOWNVTX td; v->GetTouchdownPoint(td, t);
                v->Local2Global(td.pos, tg[t]);
                th[t] = length(tmul(Rp, tg[t] - pp)) - Rl;
                if (th[t] < low) { low = th[t]; lowG = tg[t]; }
            }
            // the footprint: the touchdown points within a fifth of its size of the lowest - their centre and spread
            const double band = 0.2 * v->GetSize();
            VECTOR3 cen = { 0, 0, 0 }; int nc = 0;
            for (DWORD t = 0; t < nt; t++) if (th[t] < low + band) { cen += tg[t]; nc++; }
            cen = cen / nc;
            double R = 0;
            for (DWORD t = 0; t < nt; t++) if (th[t] < low + band) R = std::max(R, length(tg[t] - cen));
            R = std::clamp(R, 0.08, 0.5 * v->GetSize());
            seen[hv] = low;
            auto pv = lowPrev.find(hv);
            if (pv == lowPrev.end() || !(pv->second > 0 && low <= 0)) continue;
            VECTOR3 vh; v->GetGroundspeedVector(FRAME_HORIZON, vh);
            const double U = -vh.y;
            if (U < 0.3) continue;
            double glng, glat, grad; oapiGlobalToEqu(body, cen, &glng, &glat, &grad);
            const double water = (Rl - oapiGetSize(body)) - oapiSurfaceElevation(body, glng, glat);
            if (water < 0.05) continue;                                                       // the shore
            const double m = std::max(v->GetMass(), 1.0), k = RHO_LIQ * 0.8 * PI * R * R / (2 * m);
            const double tSeal = 2.285 * sqrt(R / g), dMax = std::min(water - 0.02, 3.0);
            // the footprint's offset from the vessel's centre, on the level (planet frame): the cavity follows the vessel
            VECTOR3 q = tmul(Rp, cen - pp); q = q * (Rl / length(q));
            VECTOR3 qv = tmul(Rp, vg - pp); qv = qv * (Rl / length(qv));
            if (impacts.size() < 8) impacts.push_back({ body, hv, q - qv, R, U, k, dMax, oapiGetSimTime(), tSeal, 0.0 });
            const double Lf = std::min(log(1 + k * U * tSeal) / k, dMax), sf = std::max(R, cbrt(R * R * Lf));
            const double ma = 4.0 / 3.0 * RHO_LIQ * R * R * R, sk = 2 * sf;
            const double vUp = std::min(0.5 * U * m / (m + ma), 2 * sqrt(g * sk));
            double x, y, depth; ToCells(cen, pp, Rp, Rl, x, y, depth);
            add(x, y, vUp, std::max(sk, 1.5 * dx) / dx, true);
            ParticlesCrown(body, cen, R, U);
            LOGC("wave field: %s hits the liquid at %.2f m/s, %.2f m/s along (disc %.2f m, %.0f kg): an air cavity %.2f m deep "
                 "when it closes after %.2f s, a crater %.2f m deep and wide at the level, the crown thrown up at %.2f m/s",
                 v->GetName(), U, sqrt(vh.x * vh.x + vh.z * vh.z), R, m, Lf, tSeal, sf, vUp);
        }
        lowPrev.swap(seen);
    }
    // the engines' jets on the liquid: the modules' (AtmoFX_SetJet) and the stock thrusters of the vessels that send none,
    // as pressure heads (JetCavity) where their axes meet the level - only from nozzles above it (a sole's puff under the
    // liquid is no jet on it)
    int nj = 0;
    {
        struct J { double x, y, head, sig; };
        std::vector<J> js;
        double logS = 0, logU = 0, logP = 0, logH = 0, logF = 0;     // the strongest jet: its path, flow speed, pressure, cavity
        // vgG: the vessel's velocity over the ground (global): the gas reaching the level left the nozzle a while ago, from
        // where the nozzle was then - the far jet's centreline speed u = A / z, A = sqrt(2 M / (pi 0.0121 rho_air)), takes
        // z^2 / 2A to the level, and the cavity lags behind the nozzle by the horizontal speed times that
        auto jet = [&](const VECTOR3& ge, const VECTOR3& gdIn, double T, double r0, double rhoA, const VECTOR3& vgG) {
            if (T <= 0) return;
            const VECTOR3 pe = tmul(Rp, ge - pp), de = tmul(Rp, gdIn) / length(gdIn);
            const VECTOR3 up = pe / length(pe);
            const double h = length(pe) - Rl, down = -dotp(de, up);
            if (h <= 0 || down < 0.1) return;
            const double s = h / down;                    // along the jet to the level
            if (s > 300) return;
            double head, sig; JetCavity(T * down, s, std::max(r0, 0.01), g, head, sig);
            if (head < 1e-3) return;
            if (T * down > logF) {    // the flow at the surface: the stagnation pressure's speed in the gas around (mixed out)
                logF = T * down; logS = s; logH = head; logP = head * RHO_LIQ * g;
                logU = sqrt(2 * logP / std::max(rhoA, 1e-4));
            }
            // a cavity deeper than its width is the penetrating, splashing mode: its walls flap and throw the liquid off, the
            // jet wanders in it and the cavity's mean shape is about as wide (2 sigma) as deep - the same force (head x
            // sigma^2: the volume pushed aside)
            if (head > 2 * sig) { sig = cbrt(head * sig * sig / 2); head = 2 * sig; }
            VECTOR3 hit = pe + de * s;
            {
                const VECTOR3 vp = tmul(Rp, vgG), vhz = vp - up * dotp(vp, up);
                const double A = sqrt(2 * T * down / (PI * 0.0121 * std::max(rhoA, 1e-4)));
                const double tFly = std::min(s * s / (2 * A), 3.0);
                hit = hit - vhz * tFly;
            }
            hit = hit * (Rl / length(hit));
            double x, y, depth; ToCells(mul(Rp, hit) + pp, pp, Rp, Rl, x, y, depth);
            double sc = sig / dx;
            if (x < -3 * sc || y < -3 * sc || x > FN + 3 * sc || y > FN + 3 * sc) return;
            const double sc0 = sc; sc = std::clamp(sc, 1.5, 48.0);
            head = std::min(head * (sc0 * sc0) / (sc * sc), 3.0);    // the same force over what the grid carries
            js.push_back({ x, y, head, sc });
        };
        std::set<OBJHANDLE> sends;
        const double now = oapiGetSimTime();
        {
            std::lock_guard<std::mutex> lj(g_ext.mx);
            for (auto& kv : g_ext.jets) {
                const ExtState::Jet& j = kv.second;
                if (!oapiIsVessel(kv.first.h)) continue;
                sends.insert(kv.first.h);
                if (j.thrust <= 0 || now - j.t > 0.5) continue;
                if (j.ve < 100) continue;      // a sole's puff (the air squeezed from under it, a few m/s): no jet on the liquid
                VESSEL* v = oapiGetVesselInterface(kv.first.h);
                if (v->GetSurfaceRef() != body) continue;
                VECTOR3 ge, gd, vgG; v->Local2Global(j.pos, ge); v->GlobalRot(j.dir, gd); v->GetGroundspeedVector(FRAME_GLOBAL, vgG);
                jet(ge, gd, j.thrust, j.rExit, v->GetAtmDensity(), vgG);
            }
        }
        const VECTOR3 Cg = pp + mul(Rp, F.C);
        for (DWORD i = 0, nv = oapiGetVesselCount(); i < nv; i++) {
            OBJHANDLE hv = oapiGetVesselByIndex(i);
            VESSEL* v = hv ? oapiGetVesselInterface(hv) : nullptr;
            if (!v || sends.count(hv) || v->GetSurfaceRef() != body) continue;
            VECTOR3 vg; v->GetGlobalPos(vg);
            if (length(vg - Cg) > 0.5 * S + 300) continue;
            for (DWORD t = 0, nt = v->GetThrusterCount(); t < nt; t++) {
                THRUSTER_HANDLE th = v->GetThrusterHandleByIndex(t);
                const double T = v->GetThrusterLevel(th) * v->GetThrusterMax(th);
                if (T <= 0) continue;
                double r0 = 0;      // the nozzle: half its exhaust's width
                for (DWORD e = 0, ne = v->GetExhaustCount(); e < ne; e++) {
                    EXHAUSTSPEC es = {};
                    if (v->GetExhaustSpec(e, &es) && es.th == th) r0 = std::max(r0, 0.5 * es.wsize);
                }
                VECTOR3 lp, ld; v->GetThrusterRef(th, lp); v->GetThrusterDir(th, ld);
                VECTOR3 ge, gd, vgG; v->Local2Global(lp, ge); v->GlobalRot(-ld, gd);     // the gas goes against the thrust
                v->GetGroundspeedVector(FRAME_GLOBAL, vgG);
                jet(ge, gd, T, r0 > 0 ? r0 : 0.1, v->GetAtmDensity(), vgG);
            }
        }
        {   // test runs (D3D11CLIENT_TEST_JET=1): the «Модуль полёта»'s two nozzles of 87 N, exit radius 40.5 mm, hovering
            // 1.2 m over the liquid 3 m beside the focus vessel (a headless run cannot fly it)
            static int testJ = -1;
            if (testJ < 0) { char e[8] = {}; testJ = GetEnvironmentVariableA("D3D11CLIENT_TEST_JET", e, sizeof e) && e[0] == '1' ? 1 : 0; }
            if (testJ == 1) {
                VECTOR3 fu; fv->GlobalRot(_V(0, 0, 1), fu);     // its forward
                const VECTOR3 up = (fg - pp) / length(fg - pp), side = crossp(up, fu) / std::max(length(crossp(up, fu)), 1e-6);
                const VECTOR3 base = pp + up * (Rl + 1.2) + side * 3.0;
                VECTOR3 fwd = crossp(side, up);
                for (int k = 0; k < 2; k++) jet(base + fwd * (k ? -0.3 : 0.3), -up, 87.0, 0.0405, fv->GetAtmDensity(), _V(0, 0, 0));
            }
        }
        // the impacts' cavities first (sigma < 0 for the shader: a cavity the body holds open - the surface no higher than
        // it), then the strongest jets
        const double tNow = oapiGetSimTime();
        for (auto it = impacts.begin(); it != impacts.end();) {
            const double t = tNow - it->t0;
            if (it->body != body || t > it->tSeal || t < 0) { it = impacts.erase(it); continue; }    // closed: let go
            if (!oapiIsVessel(it->hv)) { it = impacts.erase(it); continue; }
            const double L = std::min(log(1 + it->k * it->U * t) / it->k, it->dMax);       // the air cavity's depth
            const double sg = std::max(it->R, cbrt(it->R * it->R * L)), H = std::min(L, sg);   // the crater at the level
            VECTOR3 vg; oapiGetGlobalPos(it->hv, &vg);
            VECTOR3 q = tmul(Rp, vg - pp); q = q * (Rl / length(q)); q = q + it->off; q = q * (Rl / length(q));
            double x, y, depth; ToCells(mul(Rp, q) + pp, pp, Rp, Rl, x, y, depth);
            const double sc = std::max(sg / dx, 1.5);
            if (nj < 8 && H > 1e-3) cb.jet[nj++] = { (float)x, (float)y, (float)H, (float)-sc };
            // the rim: the crater's growth dV as exp(-r^2 / (2 sg)^2) (the stamp's sigma sqrt 2 sg): amplitude dV / (4 pi sg^2)
            const double Vc = PI * sg * sg * H;
            add(x, y, (Vc - it->V) / (4 * PI * sg * sg), 1.41421356 * sc, false);
            it->V = Vc;
            ++it;
        }
        std::sort(js.begin(), js.end(), [](const J& a, const J& b) { return a.head * a.sig * a.sig > b.head * b.sig * b.sig; });
        for (const J& j : js) { if (nj >= 8) break; cb.jet[nj++] = { (float)j.x, (float)j.y, (float)j.head, (float)j.sig }; }
        static double tLogJ = -1e9;
        if (!js.empty() && oapiGetSysTime() - tLogJ > 10) {
            tLogJ = oapiGetSysTime();
            LOGC("wave field: %d jets on the liquid; the strongest %.0f N on it, %.2f m along the jet: the flow there %.0f m/s, "
                 "%.0f Pa -> a cavity %.2f m deep (drawn %.2f m, sigma %.2f m)", (int)js.size(), logF, logS, logU, logP, logH, js[0].head, js[0].sig * dx);
        }
    }
    // the equation's constants for this grid
    const double dts = 1.0 / 60;
    cb.p[2] = (float)dts;
    cb.p[3] = (float)(0.2 * dts);                                  // damping: waves fade over ~5 s
    cb.k[0] = (float)(g * dts * dts / dx); cb.k[1] = (float)nj; cb.k[2] = (float)(FN * 0.05); cb.k[3] = (float)ns;
    memcpy(cb.ker, g_ker, sizeof g_ker);
    if (ns > 0) Run(ctx, F.csStamp.Get(), cb, false);
    // fixed steps of 1/60 s of simulation time (at most 4 a frame; a pause stops the waves)
    F.acc = std::min(F.acc + dtReal, 4.0 / 60);
    cb.k[3] = 0;
    int steps = 0;
    while (F.acc >= 1.0 / 60 && steps < 4) { Run(ctx, F.csSim.Get(), cb, false); F.acc -= 1.0 / 60; steps++; }
    Run(ctx, F.csSlope.Get(), cb, true);
    {   // diagnostics, every 10 s: the largest height and slope in the window
        static double tLog = -1e9; const double now = oapiGetSysTime();
        if (now - tLog > 10) {
            tLog = now;
            D3D11_TEXTURE2D_DESC td; F.slope->GetDesc(&td);
            td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> st;
            if (SUCCEEDED(c->Dev()->CreateTexture2D(&td, nullptr, &st))) {
                ctx->CopyResource(st.Get(), F.slope.Get());
                D3D11_MAPPED_SUBRESOURCE m;
                if (SUCCEEDED(ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) {
                    float hmax = 0, smax = 0;
                    for (int y = 0; y < FN; y++) {
                        const uint16_t* row = (const uint16_t*)((const BYTE*)m.pData + (size_t)y * m.RowPitch);
                        for (int x = 0; x < FN; x++) {
                            float gx = XMConvertHalfToFloat(row[x * 4]), gy = XMConvertHalfToFloat(row[x * 4 + 1]), h = XMConvertHalfToFloat(row[x * 4 + 2]);
                            hmax = std::max(hmax, fabsf(h)); smax = std::max(smax, sqrtf(gx * gx + gy * gy));
                        }
                    }
                    ctx->Unmap(st.Get(), 0);
                    LOGC("wave field: window %.1f m, %d nodes, max height %.4f m, max slope %.4f", FN * dx, (int)nodes.size(), hmax, smax);
                }
            }
        }
    }
    // for the liquid shader: the window's corner from the camera and its axes (global)
    const VECTOR3 Cg = pp + mul(Rp, F.C), e1 = mul(Rp, F.a1), e2 = mul(Rp, F.a2);
    view.origin = Cg - cam - (e1 + e2) * (FN * 0.5 * dx);
    view.e1 = e1; view.e2 = e2; view.dx = dx; view.n = FN; view.srv = F.slopeSRV.Get(); view.on = true;
    return true;
}

// the Kelvin wakes of the parts moving faster than the waves (the field carries the rest)
int WakeSources(OBJHANDLE body, double Rl, const VECTOR3& cam, WakeSrc* out, int maxN)
{
    std::lock_guard<std::mutex> lk(mx);
    VECTOR3 pp; oapiGetGlobalPos(body, &pp);
    std::vector<std::pair<double, WakeSrc>> list;
    for (auto& kv : nodes) {
        OBJHANDLE hv = kv.first.first; const Node& n = kv.second;
        if (n.dead || !oapiIsVessel(hv)) continue;
        VESSEL* v = oapiGetVesselInterface(hv);
        if (v->GetSurfaceRef() != body) continue;
        VECTOR3 g; v->Local2Global(n.pos, g);
        VECTOR3 rv = g - pp; const double rl = length(rv);
        const VECTOR3 up = rv / rl, P = pp + up * Rl;
        if (Rl - rl < -n.r || n.s <= 0) continue;        // not touching the liquid
        const VECTOR3 c = P - cam;
        const double dist = length(c);
        if (dist > 3000) continue;
        const double gacc = GGRAV * oapiGetMass(body) / (Rl * Rl);
        VECTOR3 vg; v->GetGroundspeedVector(FRAME_GLOBAL, vg);
        const VECTOR3 vh = vg - up * dotp(vg, up);
        const double U = length(vh);
        if (U <= 0.15) continue;      // gravity waves need more than the minimum phase speed (~0.11 m/s on liquid methane)
        const VECTOR3 f = vh / U;
        const double lam = 2 * PI * U * U / gacc;
        const double amp = 0.18 * n.s * std::min(1.0, 2.5 * n.r / lam) * std::min(1.0, (U - 0.15) / 0.3);
        WakeSrc w = {};
        w.a = { (float)lam, (float)amp, (float)n.r, 0 };
        w.b = { (float)c.x, (float)c.y, (float)c.z, 0 };
        w.f = { (float)f.x, (float)f.y, (float)f.z, 0 };
        list.push_back({ dist, w });
    }
    std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.first < b.first; });
    int n = 0;
    for (auto& e : list) { if (n >= maxN) break; out[n++] = e.second; }
    return n;
}

// the Kelvin wake's table (PNG, decoded by WIC); linear data, not colour
ComPtr<ID3D11ShaderResourceView> WakeLoadKelvin(ID3D11Device* dev)
{
    ComPtr<ID3D11ShaderResourceView> srv;
    HRESULT hc = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IWICImagingFactory> f; ComPtr<IWICBitmapDecoder> dec; ComPtr<IWICBitmapFrameDecode> fr; ComPtr<IWICFormatConverter> cv;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
            SUCCEEDED(f->CreateDecoderFromFilename(L"Textures\\AtmoFX\\kelvin_wake.png", nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
            SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&cv)) &&
            SUCCEEDED(cv->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
            UINT w = 0, h = 0; cv->GetSize(&w, &h);
            std::vector<BYTE> px((size_t)w * h * 4);
            if (SUCCEEDED(cv->CopyPixels(nullptr, w * 4, (UINT)px.size(), px.data()))) {
                D3D11_TEXTURE2D_DESC td = { w, h, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
                D3D11_SUBRESOURCE_DATA sd = { px.data(), w * 4, 0 };
                ComPtr<ID3D11Texture2D> t;
                if (SUCCEEDED(dev->CreateTexture2D(&td, &sd, &t))) dev->CreateShaderResourceView(t.Get(), nullptr, &srv);
                LOGC("wakes: Kelvin table %ux%u %s", w, h, srv ? "loaded" : "NOT created");
            }
        } else LOGC("wakes: Textures\\AtmoFX\\kelvin_wake.png NOT loaded (Kelvin wakes off)");
    }
    if (SUCCEEDED(hc)) CoUninitialize();
    return srv;
}
