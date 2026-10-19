// Wakes on liquids (Titan's seas). The modules place wake nodes on their parts (AtmoFX_SetWake: a point on the vessel,
// its radius, a strength; strength > 1 = an impulse, 0 = removed). Each frame they become wave sources that the liquid
// shader (shaders\liquid.hlsl) sums per pixel as slopes of the surface:
//  - the Kelvin wake of a moving part: the linear deep-water solution for a moving pressure patch, stored in units of the
//    wavelength lambda = 2 pi U^2 / g (Textures\AtmoFX\kelvin_wake.png: rg = slope along the motion and across, 0.5 = 0),
//    scaled and turned along the motion;
//  - rings around a bobbing part (capillary-gravity ripples, ~0.35-1 m);
//  - an expanding packet after an impulse (a step, a fall): wavelength ~ the part, moving out at the group speed.
// The same model as the AtmoFX D3D9 fork, written for this client.
#include "Wake.h"
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace {

struct Node { VECTOR3 pos; double r = 0, s = 0; bool impulse = false; double amp = 0; };
struct Impulse { OBJHANDLE body; VECTOR3 p; double r, amp, t0; };     // p: planet frame, on the level

std::mutex mx;
std::map<std::pair<OBJHANDLE, int>, Node> nodes;
std::vector<Impulse> impulses;

}  // namespace

void WakeSet(OBJHANDLE hv, int id, const double* pos, double radius, double strength)
{
    std::lock_guard<std::mutex> lk(mx);
    auto key = std::make_pair(hv, id);
    if (strength <= 0 || radius <= 0) {
        auto it = nodes.find(key);
        if (it != nodes.end() && it->second.impulse) { it->second.r = it->second.r > 0 ? it->second.r : radius; it->second.s = 0; return; }   // the impulse still goes out
        nodes.erase(key); return;
    }
    Node& n = nodes[key];
    n.pos = _V(pos[0], pos[1], pos[2]); n.r = radius; n.s = std::min(strength, 1.0);
    if (strength > 1.0) { n.impulse = true; n.amp = std::min(strength, 4.0); }
}

void WakeRipple(OBJHANDLE body, const VECTOR3& gpos, double radius, double amp)
{
    std::lock_guard<std::mutex> lk(mx);
    VECTOR3 pp; oapiGetGlobalPos(body, &pp);
    MATRIX3 Rp; oapiGetRotationMatrix(body, &Rp);
    if (impulses.size() >= 96) impulses.erase(impulses.begin());
    impulses.push_back({ body, tmul(Rp, gpos - pp), radius, std::min(amp, 4.0), oapiGetSimTime() });
}

int WakeSources(OBJHANDLE body, double Rl, const VECTOR3& cam, WakeSrc* out, int maxN)
{
    std::lock_guard<std::mutex> lk(mx);
    const double simt = oapiGetSimTime();
    VECTOR3 pp; oapiGetGlobalPos(body, &pp);
    MATRIX3 Rp; oapiGetRotationMatrix(body, &Rp);
    std::vector<std::pair<double, WakeSrc>> list;
    for (auto it = nodes.begin(); it != nodes.end();) {
        OBJHANDLE hv = it->first.first; Node& n = it->second;
        if (!oapiIsVessel(hv)) { it = nodes.erase(it); continue; }
        VESSEL* v = oapiGetVesselInterface(hv);
        if (v->GetSurfaceRef() != body) { ++it; continue; }
        VECTOR3 g; v->Local2Global(n.pos, g);
        VECTOR3 rv = g - pp; const double rl = length(rv);
        const VECTOR3 up = rv / rl, P = pp + up * Rl;
        const double depth = Rl - rl;                       // below the level (> 0: in the liquid)
        const double gacc = GGRAV * oapiGetMass(body) / (Rl * Rl);
        if (n.impulse) {                                    // an impulse: an expanding packet from here
            if (depth > -n.r) {
                if (impulses.size() >= 96) impulses.erase(impulses.begin());
                impulses.push_back({ body, tmul(Rp, P - pp), n.r, n.amp, simt });
            }
            n.impulse = false;
            if (n.s <= 0) { it = nodes.erase(it); continue; }
        }
        if (depth < -n.r || n.s <= 0) { ++it; continue; }  // not touching the liquid
        const VECTOR3 c = P - cam;
        const double dist = length(c);
        if (dist > 3000) { ++it; continue; }
        VECTOR3 vg; v->GetGroundspeedVector(FRAME_GLOBAL, vg);
        const VECTOR3 vh = vg - up * dotp(vg, up);
        const double U = length(vh);
        if (U > 0.15) {        // gravity waves need more than the minimum phase speed (~0.11 m/s on liquid methane)
            const VECTOR3 f = vh / U;
            const double lam = 2 * PI * U * U / gacc;
            const double amp = 0.10 * n.s * std::min(1.0, 2.5 * n.r / lam) * std::min(1.0, (U - 0.15) / 0.3);
            WakeSrc w = {};
            w.a = { (float)lam, (float)amp, (float)n.r, 0 };
            w.b = { (float)c.x, (float)c.y, (float)c.z, 0 };
            w.f = { (float)f.x, (float)f.y, (float)f.z, 0 };
            list.push_back({ dist, w });
        }
        {                      // rings around the part
            WakeSrc w = {};
            w.a = { (float)std::max(0.35, 0.6 * n.r), (float)(0.06 * n.s), (float)n.r, (float)fmod(simt, 1000.0) };
            w.b = { (float)c.x, (float)c.y, (float)c.z, 1 };
            list.push_back({ dist, w });
        }
        ++it;
    }
    for (size_t i = 0; i < impulses.size();) {
        const Impulse& im = impulses[i];
        const double t = simt - im.t0;
        if (t > 40 || t < 0) { impulses.erase(impulses.begin() + i); continue; }
        i++;
        if (im.body != body) continue;
        const VECTOR3 P = pp + mul(Rp, im.p), c = P - cam;
        const double dist = length(c);
        if (dist > 3000) continue;
        const double gacc = GGRAV * oapiGetMass(body) / (Rl * Rl);
        const double lam = std::max(im.r < 0.1 ? 0.04 : 0.5, 2 * im.r);   // drops: capillary rings of a few cm
        const double cg = 0.5 * sqrt(gacc * lam / (2 * PI));               // the group speed: half the phase speed
        const double rf = im.r + cg * t;
        const double amp = 0.12 * im.amp * sqrt(im.r / std::max(rf, im.r)) * exp(-t / std::min(25.0, 2.0 + 20.0 * im.r));
        if (amp < 1e-4) continue;
        WakeSrc w = {};
        w.a = { (float)lam, (float)amp, (float)rf, (float)t };
        w.b = { (float)c.x, (float)c.y, (float)c.z, 2 };
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
        } else LOGC("wakes: Textures\\AtmoFX\\kelvin_wake.png NOT loaded (Kelvin wakes off; rings still drawn)");
    }
    if (SUCCEEDED(hc)) CoUninitialize();
    return srv;
}
