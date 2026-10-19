// Waves on liquids (Titan's seas). The modules place wake nodes on their parts (AtmoFX_SetWake: a point on the vessel,
// its radius, a strength; strength > 1 = an impulse, 0 = removed). Two models shape the surface, as in the AtmoFX D3D9
// fork (written for this client):
//  - the wave field (shaders\wavefield.hlsl): a window of N x N cells on the level around the focus vessel where each
//    node is a moving hollow (depth 0.9 r s - on Titan a wading leg's bow stands ~U^2/2g high: a few cm, ramped in over 2 s): every frame it is lifted where it was and pressed where
//    it is, so a part at rest makes nothing and a moving one makes its own bow and wake; a node that goes away lets its
//    hollow fill and run out as waves; impulses (steps, splashes) kick the surface's velocity. The equation follows the
//    deep-liquid dispersion, so a push sends out a train of waves, not a ring;
//  - the Kelvin wake of a part moving faster than the waves (the linear deep-water solution for a moving pressure patch,
//    in units of the wavelength lambda = 2 pi U^2 / g: Textures\AtmoFX\kelvin_wake.png, rg = slope along the motion and
//    across, 0.5 = 0), summed per pixel by the liquid shader.
#include "Wake.h"
#include <cmath>
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
    bool tried = false, ok = false, active = false;
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

struct FieldCB { float p[4], k[4]; int shift[4]; XMFLOAT4 stamp[64]; float ker[172]; };
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
    if (F.tried) return F.ok;
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
    // the equation's constants for this grid
    const double dts = 1.0 / 60;
    cb.p[2] = (float)dts;
    cb.p[3] = (float)(0.2 * dts);                                  // damping: waves fade over ~5 s
    cb.k[0] = (float)(g * dts * dts / dx); cb.k[2] = (float)(FN * 0.05); cb.k[3] = (float)ns;
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
