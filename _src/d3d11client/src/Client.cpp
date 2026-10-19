// D3D11Client: the module, the device and the window, textures and surfaces, Orbiter's visual events, the frame.
#define ORBITER_MODULE   // once: the module boilerplate (ModuleDate, calldummy)
#include "Client.h"
#include <wincodec.h>
#include "Fx.h"
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "msimg32.lib")

D3D11Client* g_client = nullptr;

DLLCLBK void InitModule(HINSTANCE hDLL)
{
    g_client = new D3D11Client(hDLL);
    if (!oapiRegisterGraphicsClient(g_client)) { delete g_client; g_client = nullptr; return; }
    g_client->LoadShim();
}

DLLCLBK void ExitModule(HINSTANCE hDLL)
{
    if (g_client) { oapiUnregisterGraphicsClient(g_client); delete g_client; g_client = nullptr; }
}

D3D11Client::D3D11Client(HINSTANCE h) : oapi::GraphicsClient(h) {}
D3D11Client::~D3D11Client() {}

bool D3D11Client::clbkInitialise()
{
    if (!oapi::GraphicsClient::clbkInitialise()) return false;
    g_cfg.Load();
    InitVideoTab();
    VIDEODATA* vd = GetVideoData();
    W = vd->winw > 0 ? vd->winw : 1280; H = vd->winh > 0 ? vd->winh : 800;
    shaderDir = "Modules\\D3D11Client\\";
    LOGC("initialised, window %ux%u", W, H);
    return true;
}

bool D3D11Client::clbkGetRenderParam(DWORD prm, DWORD* value) const
{
    switch (prm) {
    case RP_COLOURDEPTH: *value = 32; return true;
    case RP_ZBUFFERDEPTH: *value = 32; return true;
    case RP_STENCILDEPTH: *value = 0; return true;
    case RP_MAXLIGHTS: *value = 8; return true;
    case RP_ISTLDEVICE: *value = 1; return true;
    case RP_REQUIRETEXPOW2: *value = 0; return true;
    }
    return false;
}

// ------------------------------------------------------------------------------------------ window and device
HWND D3D11Client::clbkCreateRenderWindow()
{
    hWnd = oapi::GraphicsClient::clbkCreateRenderWindow();
    if (GetVideoData()->fullscreen) {
        HMONITOR mon = MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof mi }; GetMonitorInfoA(mon, &mi);
        SetWindowLongA(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
    }
    RECT rc; GetClientRect(hWnd, &rc);
    if (rc.right > 0 && rc.bottom > 0) { W = rc.right; H = rc.bottom; }
    // test runs (tools/run_test.ps1 sets D3D11CLIENT_TEST): the window never shows and never takes the focus
    char tv[8] = "";
    if (GetEnvironmentVariableA("D3D11CLIENT_TEST", tv, sizeof tv) && tv[0] == '1') {
        testMode = true;
        SetWindowLongA(hWnd, GWL_EXSTYLE, GetWindowLongA(hWnd, GWL_EXSTYLE) | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
        ShowWindow(hWnd, SW_HIDE);
        LOGC("test mode: hidden window");
    }
    if (!CreateDevice()) { LOGC("device creation FAILED"); return hWnd; }
    shadersOk = CreateShaders();
    if (!shadersOk) LOGC("shader compilation FAILED - see above; the scene is not drawn");
    CreateLiquid();
    CreateParticleTexture();
    if (shadersOk) FxInit(this);
    loaderRun = true;
    loader = std::thread(&D3D11Client::LoaderMain, this);
    QueryPerformanceFrequency(&qpf); QueryPerformanceCounter(&tStart); tLog = tStart;
    LOGC("render window %ux%u, D3D11 device ready", W, H);
    return hWnd;
}

bool D3D11Client::CreateDevice()
{
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &fl, 1, D3D11_SDK_VERSION, &dev, &got, &ctx);
    if (FAILED(hr)) { LOGC("D3D11CreateDevice failed 0x%08x", (unsigned)hr); return false; }
    ComPtr<IDXGIDevice> dxd; dev.As(&dxd);
    ComPtr<IDXGIAdapter> ad; dxd->GetAdapter(&ad);
    DXGI_ADAPTER_DESC add; ad->GetDesc(&add);
    char an[128]; WideCharToMultiByte(CP_UTF8, 0, add.Description, -1, an, 128, nullptr, nullptr);
    LOGC("adapter %s, %u MB", an, (unsigned)(add.DedicatedVideoMemory >> 20));
    ComPtr<IDXGIFactory2> fac; ad->GetParent(IID_PPV_ARGS(&fac));
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = W; sd.Height = H; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = fac->CreateSwapChainForHwnd(dev.Get(), hWnd, &sd, nullptr, nullptr, &sc);
    if (FAILED(hr)) { LOGC("swap chain failed 0x%08x", (unsigned)hr); return false; }
    fac->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER);
    sc->GetBuffer(0, IID_PPV_ARGS(&bbTex));
    D3D11_RENDER_TARGET_VIEW_DESC rv = {}; rv.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    dev->CreateRenderTargetView(bbTex.Get(), &rv, &bbRTV);
    CreateSceneTargets();     // the scene colour/depth for the chosen anti-aliasing (AA.cpp)

    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_BACK; rd.DepthClipEnable = TRUE;
    dev->CreateRasterizerState(&rd, &rsBack);
    rd.CullMode = D3D11_CULL_NONE; dev->CreateRasterizerState(&rd, &rsNone);
    D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    dev->CreateDepthStencilState(&dd, &dsWrite);
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dev->CreateDepthStencilState(&dd, &dsRead);
    D3D11_BLEND_DESC bd = {}; bd.RenderTarget[0].RenderTargetWriteMask = 15;
    dev->CreateBlendState(&bd, &bsOpaque);
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    dev->CreateBlendState(&bd, &bsAlpha);
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    dev->CreateBlendState(&bd, &bsAdd);
    // Direct2D on the back buffer: the loading screen
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2f.GetAddressOf());
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)dwf.GetAddressOf());
    {
        ComPtr<IDXGISurface> surf; bbTex.As(&surf);
        D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (d2f && surf) d2f->CreateDxgiSurfaceRenderTarget(surf.Get(), &rp, &d2rt);
        if (dwf) {
            dwf->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"en-us", &loadFont);
            dwf->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 28.0f, L"en-us", &loadFontBig);
        }
        FILE* f = fopen("Modules\\D3D11Client\\loadsteps.txt", "r");
        if (f) { int n = 0; if (fscanf(f, "%d", &n) == 1 && n > 10) loadExpected = n; fclose(f); }
    }
    D3D11_SAMPLER_DESC s = {}; s.Filter = D3D11_FILTER_ANISOTROPIC; s.MaxAnisotropy = std::clamp(g_cfg.aniso, 1, 16);
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; s.MaxLOD = D3D11_FLOAT32_MAX;
    // a smaller scene (TAAU) samples the textures as the output resolution would: a negative mip bias
    if (g_cfg.aa == 5 && g_cfg.renderScale < 0.999f) s.MipLODBias = log2f(std::clamp(g_cfg.renderScale, 0.5f, 1.0f));
    dev->CreateSamplerState(&s, &sampAniso);
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    dev->CreateSamplerState(&s, &sampClamp);
    auto cb = [&](ComPtr<ID3D11Buffer>& b, UINT size) {
        D3D11_BUFFER_DESC d = { (size + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&d, nullptr, &b);
    };
    cb(cbObj, 512); cb(cbGrp, 256); cb(cbFrame, 512);
    // the shadow map
    {
        D3D11_TEXTURE2D_DESC st = { SHADOW_N, SHADOW_N, 1, 1, DXGI_FORMAT_R32_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        dev->CreateTexture2D(&st, nullptr, &shadowTex);
        D3D11_DEPTH_STENCIL_VIEW_DESC sdv = {}; sdv.Format = DXGI_FORMAT_D32_FLOAT; sdv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        dev->CreateDepthStencilView(shadowTex.Get(), &sdv, &shadowDSV);
        D3D11_SHADER_RESOURCE_VIEW_DESC ssv = {}; ssv.Format = DXGI_FORMAT_R32_FLOAT; ssv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; ssv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(shadowTex.Get(), &ssv, &shadowSRV);
        D3D11_SAMPLER_DESC cs = {}; cs.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; cs.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
        cs.AddressU = cs.AddressV = cs.AddressW = D3D11_TEXTURE_ADDRESS_BORDER; cs.BorderColor[0] = cs.BorderColor[1] = cs.BorderColor[2] = cs.BorderColor[3] = 1.0f; cs.MaxLOD = D3D11_FLOAT32_MAX;
        dev->CreateSamplerState(&cs, &sampShadow);
        D3D11_DEPTH_STENCIL_DESC sd2 = {}; sd2.DepthEnable = TRUE; sd2.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; sd2.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        dev->CreateDepthStencilState(&sd2, &dsShadow);
        D3D11_RASTERIZER_DESC rs2 = {}; rs2.FillMode = D3D11_FILL_SOLID; rs2.CullMode = D3D11_CULL_NONE; rs2.DepthClipEnable = TRUE;
        rs2.DepthBias = 200; rs2.SlopeScaledDepthBias = 2.0f;
        dev->CreateRasterizerState(&rs2, &rsShadow);
    }
    UINT white = 0xFFFFFFFF;
    D3D11_TEXTURE2D_DESC wt = { 1, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    D3D11_SUBRESOURCE_DATA wd = { &white, 4, 0 };
    ComPtr<ID3D11Texture2D> wtx; dev->CreateTexture2D(&wt, &wd, &wtx);
    dev->CreateShaderResourceView(wtx.Get(), nullptr, &whiteTex);

    // the tile grid: 65x65 nodes + a skirt ring, shared by all tiles
    const int G = 64;
    std::vector<uint32_t> ix;
    auto id = [&](int i, int j) { return (uint32_t)(i * (G + 1) + j); };   // i: lat row (south->north), j: lng column
    for (int i = 0; i < G; i++) for (int j = 0; j < G; j++) {
        // seen from outside the planet, (lng east, lat north) is a left-handed screen: clockwise = (0,0),(1,0)... in (j, i)
        ix.insert(ix.end(), { id(i, j), id(i + 1, j), id(i + 1, j + 1), id(i, j), id(i + 1, j + 1), id(i, j + 1) });
    }
    uint32_t sk = (G + 1) * (G + 1);    // skirt nodes: south row, north row, west column, east column
    for (int k = 0; k < G; k++) {
        uint32_t s0 = sk + k, n0 = sk + (G + 1) + k, w0 = sk + 2 * (G + 1) + k, e0 = sk + 3 * (G + 1) + k;
        ix.insert(ix.end(), { id(0, k), s0, s0 + 1, id(0, k), s0 + 1, id(0, k + 1) });
        ix.insert(ix.end(), { id(G, k), id(G, k + 1), n0 + 1, id(G, k), n0 + 1, n0 });
        ix.insert(ix.end(), { id(k, 0), id(k + 1, 0), w0 + 1, id(k, 0), w0 + 1, w0 });
        ix.insert(ix.end(), { id(k, G), e0, e0 + 1, id(k, G), e0 + 1, id(k + 1, G) });
    }
    tileIdxCount = (UINT)ix.size();
    D3D11_BUFFER_DESC ib = { (UINT)(ix.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA isd = { ix.data(), 0, 0 };
    dev->CreateBuffer(&ib, &isd, &tileIB);

    for (int f = 0; f < 4; f++) {
        D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
        dev->CreateQuery(&qd, &qDis[f]);
        qd.Query = D3D11_QUERY_TIMESTAMP;
        for (int i = 0; i < 4; i++) dev->CreateQuery(&qd, &qTs[f][i]);
    }
    return true;
}

static ComPtr<ID3DBlob> CompileFile(const std::string& path, const char* entry, const char* target)
{
    ComPtr<ID3DBlob> code, err;
    std::wstring wp(path.begin(), path.end());
    HRESULT hr = D3DCompileFromFile(wp.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (err) LOGC("shader %s %s: %s", path.c_str(), entry, (const char*)err->GetBufferPointer());
    if (FAILED(hr)) return nullptr;
    return code;
}

bool D3D11Client::CreateShaders()
{
    auto vs = [&](const char* f, ComPtr<ID3D11VertexShader>& s, ComPtr<ID3DBlob>* keep) {
        auto b = CompileFile(shaderDir + f, "VS", "vs_5_0"); if (!b) return false;
        dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s); if (keep) *keep = b; return true;
    };
    auto ps = [&](const char* f, ComPtr<ID3D11PixelShader>& s) {
        auto b = CompileFile(shaderDir + f, "PS", "ps_5_0"); if (!b) return false;
        dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &s); return true;
    };
    ComPtr<ID3DBlob> bm, bp;
    if (!vs("mesh.hlsl", vsMesh, &bm) || !ps("mesh.hlsl", psMesh)) return false;
    if (!vs("planet.hlsl", vsPlanet, &bp) || !ps("planet.hlsl", psPlanet)) return false;
    if (!vs("post.hlsl", vsPost, nullptr) || !ps("post.hlsl", psPost)) return false;
    if (!vs("blit.hlsl", vsBlit, nullptr) || !ps("blit.hlsl", psBlit)) return false;
    if (!vs("sky.hlsl", vsSky, nullptr) || !ps("sky.hlsl", psSky)) return false;
    {
        if (!vs("aa.hlsl", vsAA, nullptr)) return false;
        auto pe = [&](const char* e, ComPtr<ID3D11PixelShader>& sh) {
            auto b = CompileFile(shaderDir + "aa.hlsl", e, "ps_5_0"); if (!b) return false;
            dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &sh); return true;
        };
        if (!pe("ResolveDepthPS", psResolveDepth) || !pe("TAAPS", psTAA) || !pe("FXAAPS", psFXAA)) return false;
    }
    if (!vs("stars.hlsl", vsStars, nullptr) || !ps("stars.hlsl", psStars)) return false;
    ComPtr<ID3DBlob> bl;
    if (!vs("liquid.hlsl", vsLiquid, &bl) || !ps("liquid.hlsl", psLiquid)) return false;
    D3D11_INPUT_ELEMENT_DESC el[] = { { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    dev->CreateInputLayout(el, 1, bl->GetBufferPointer(), bl->GetBufferSize(), &ilLiquid);
    ComPtr<ID3DBlob> bpp;
    if (!vs("particles.hlsl", vsPart, &bpp) || !ps("particles.hlsl", psPart)) return false;
    D3D11_INPUT_ELEMENT_DESC ep[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    dev->CreateInputLayout(ep, 3, bpp->GetBufferPointer(), bpp->GetBufferSize(), &ilPart);
    if (!vs("panel.hlsl", vsPanel, nullptr) || !ps("panel.hlsl", psPanel)) return false;
    D3D11_INPUT_ELEMENT_DESC em[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    dev->CreateInputLayout(em, 3, bm->GetBufferPointer(), bm->GetBufferSize(), &ilMesh);
    dev->CreateInputLayout(em, 3, bp->GetBufferPointer(), bp->GetBufferSize(), &ilPlanet);
    return true;
}

void D3D11Client::clbkPostCreation()
{
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) {
        OBJHANDLE h = oapiGetGbodyByIndex(i);
        if (oapiGetObjectType(h) == OBJTP_PLANET) planets.push_back(new Planet(h));
    }
    ScanVessels();
    if (shadersOk) CreateStars();
    LOGC("session: %zu planets, %zu vessels", planets.size(), visuals.size());
    if (loadSteps > 10) { FILE* f = fopen("Modules\\D3D11Client\\loadsteps.txt", "w"); if (f) { fprintf(f, "%d\n", loadSteps); fclose(f); } }
}

void D3D11Client::clbkCloseSession(bool fastclose)
{
    {
        std::lock_guard<std::mutex> lk(loadMx);
        loadQ.clear();
    }
    for (auto& kv : visuals) { if (kv.second->registered) UnregisterVisObject(kv.first); delete kv.second; }
    visuals.clear();
}

void D3D11Client::clbkDestroyRenderWindow(bool fastclose)
{
    if (loaderRun) {
        { std::lock_guard<std::mutex> lk(loadMx); loaderRun = false; loadQ.clear(); }
        loadCv.notify_all();
        if (loader.joinable()) loader.join();
    }
    for (Planet* p : planets) delete p;
    planets.clear();
    FxRelease();
    for (PStream* s : streams) delete s;     // the vessels are gone by now
    streams.clear();
    if (ctx) { ctx->ClearState(); ctx->Flush(); }
    d2rt.Reset();
    delete screen; screen = nullptr; screenTex.Reset(); screenSRV.Reset();
    bbRTV.Reset(); bbTex.Reset(); sc.Reset();
    oapi::GraphicsClient::clbkDestroyRenderWindow(fastclose);
    hWnd = nullptr;
}

// ------------------------------------------------------------------------------------------ the tile loader thread
void D3D11Client::QueueTile(Planet* p, Tile* t)
{
    int expected = 0;
    if (!t->state.compare_exchange_strong(expected, 1)) return;
    { std::lock_guard<std::mutex> lk(loadMx); loadQ.emplace_back(p, t); }
    loadCv.notify_one();
}

void D3D11Client::LoaderMain()
{
    for (;;) {
        std::pair<Planet*, Tile*> job;
        {
            std::unique_lock<std::mutex> lk(loadMx);
            loadCv.wait(lk, [&] { return !loaderRun || !loadQ.empty(); });
            if (!loaderRun) return;
            // the coarsest first: a tile needs its parent's elevation
            auto best = loadQ.begin();
            for (auto it = loadQ.begin(); it != loadQ.end(); ++it) if (it->second->lvl < best->second->lvl) best = it;
            job = *best; loadQ.erase(best);
        }
        job.first->Load(job.second);
    }
}

// ------------------------------------------------------------------------------------------ textures and surfaces
static Surf* MakeSurf(ID3D11Device* dev, UINT w, UINT h, DWORD attrib)
{
    Surf* s = new Surf; s->w = w; s->h = h; s->attrib = attrib;
    D3D11_TEXTURE2D_DESC td = { w, h, 1, 1, DXGI_FORMAT_B8G8R8A8_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT,
                                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0 };
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
    // a surface without alpha starts opaque black (its alpha stays 1: the drawing writes colour only); with alpha, clear
    std::vector<DWORD> init((size_t)w * h, (attrib & OAPISURFACE_ALPHA) ? 0u : 0xFF000000u);
    D3D11_SUBRESOURCE_DATA id = { init.data(), w * 4, 0 };
    if (dev && w && h && SUCCEEDED(dev->CreateTexture2D(&td, &id, &s->tex))) dev->CreateShaderResourceView(s->tex.Get(), &sd, &s->srv);
    return s;
}

SURFHANDLE D3D11Client::clbkLoadTexture(const char* fname, DWORD flags)
{
    if (!fname || !*fname || !dev) return nullptr;
    std::string key = fname;
    auto it = texCache.find(key);
    if (it != texCache.end()) { it->second->refs++; return (SURFHANDLE)it->second; }
    std::string p2 = std::string("Textures2\\") + fname, p1 = std::string("Textures\\") + fname;
    UINT w = 0, h = 0;
    auto srv = LoadDDSFile(dev.Get(), ctx.Get(), p2.c_str(), true, &w, &h);
    if (!srv) srv = LoadDDSFile(dev.Get(), ctx.Get(), p1.c_str(), true, &w, &h);
    if (!srv) srv = LoadDDSFile(dev.Get(), ctx.Get(), fname, true, &w, &h);
    if (!srv) { LOGC("texture not loaded: %s", fname); return nullptr; }
    Surf* s = new Surf; s->srv = srv; s->w = w; s->h = h; s->name = key; s->immutable = true;
    texCache[key] = s;
    return (SURFHANDLE)s;
}

SURFHANDLE D3D11Client::clbkLoadSurface(const char* fname, DWORD attrib, bool bPath)
{
    return clbkLoadTexture(fname, 0);
}

void D3D11Client::clbkReleaseTexture(SURFHANDLE hTex) { clbkReleaseSurface(hTex); }

bool D3D11Client::clbkReleaseSurface(SURFHANDLE surf)
{
    Surf* s = (Surf*)surf;
    if (!s) return false;
    if (--s->refs > 0) return true;
    if (!s->name.empty()) texCache.erase(s->name);
    dirty.erase(s);
    delete s;
    return true;
}

void D3D11Client::clbkIncrSurfaceRef(SURFHANDLE surf) { if (surf) ((Surf*)surf)->refs++; }

bool D3D11Client::clbkGetSurfaceSize(SURFHANDLE surf, DWORD* w, DWORD* h)
{
    Surf* s = (Surf*)surf;
    if (!s) return false;
    *w = s->w; *h = s->h; return true;
}

SURFHANDLE D3D11Client::clbkCreateSurfaceEx(DWORD w, DWORD h, DWORD attrib) { return (SURFHANDLE)MakeSurf(dev.Get(), w, h, attrib); }
SURFHANDLE D3D11Client::clbkCreateSurface(DWORD w, DWORD h, SURFHANDLE) { return (SURFHANDLE)MakeSurf(dev.Get(), w, h, 0); }
SURFHANDLE D3D11Client::clbkCreateTexture(DWORD w, DWORD h) { return (SURFHANDLE)MakeSurf(dev.Get(), w, h, OAPISURFACE_ALPHA); }

// ------------------------------------------------------------------------------------------ particle streams
oapi::ParticleStream* D3D11Client::clbkCreateParticleStream(PARTICLESTREAMSPEC* pss) { PStream* s = new PStream(this, pss); streams.push_back(s); return s; }

oapi::ParticleStream* D3D11Client::clbkCreateExhaustStream(PARTICLESTREAMSPEC* pss, OBJHANDLE hVessel, const double* lvl, const VECTOR3* ref, const VECTOR3* dir)
{
    PStream* s = new PStream(this, pss); streams.push_back(s);
    s->Attach(hVessel, ref, dir, lvl);
    return s;
}

oapi::ParticleStream* D3D11Client::clbkCreateExhaustStream(PARTICLESTREAMSPEC* pss, OBJHANDLE hVessel, const double* lvl, const VECTOR3& ref, const VECTOR3& dir)
{
    PStream* s = new PStream(this, pss); streams.push_back(s);
    s->Attach(hVessel, ref, dir, lvl);
    return s;
}

oapi::ParticleStream* D3D11Client::clbkCreateReentryStream(PARTICLESTREAMSPEC* pss, OBJHANDLE hVessel)
{
    static const double one = 1.0;
    PStream* s = new PStream(this, pss); streams.push_back(s);
    s->Attach(hVessel, _V(0, 0, 0), _V(0, 0, -1), &one);
    return s;
}

// ------------------------------------------------------------------------------------------ device meshes
bool D3D11Client::clbkSetMeshTexture(DEVMESHHANDLE hMesh, DWORD texidx, SURFHANDLE tex)
{
    DevMesh* m = (DevMesh*)hMesh;
    if (!m || texidx >= m->tex.size()) return false;
    if (tex) clbkIncrSurfaceRef(tex);
    if (m->tex[texidx]) clbkReleaseSurface(m->tex[texidx]);
    m->tex[texidx] = tex;
    return true;
}

int D3D11Client::clbkSetMeshMaterial(DEVMESHHANDLE hMesh, DWORD matidx, const MATERIAL* mat)
{
    DevMesh* m = (DevMesh*)hMesh;
    if (!m) return 2;
    if (matidx >= m->mtrl.size()) return 4;
    m->mtrl[matidx] = *mat;
    return 0;
}

int D3D11Client::clbkMeshMaterial(DEVMESHHANDLE hMesh, DWORD matidx, MATERIAL* mat)
{
    DevMesh* m = (DevMesh*)hMesh;
    if (!m) return 2;
    if (matidx >= m->mtrl.size()) return 4;
    *mat = m->mtrl[matidx];
    return 0;
}

int D3D11Client::clbkGetMeshGroup(DEVMESHHANDLE hMesh, DWORD grpidx, GROUPREQUESTSPEC* grs)
{
    DevMesh* m = (DevMesh*)hMesh;
    if (!m) return -2;
    if (grpidx >= m->grp.size()) return -1;
    DGroup& g = m->grp[grpidx];
    int ret = 0;
    if (grs->nVtx && grs->Vtx) {
        for (DWORD i = 0; i < grs->nVtx; i++) {
            DWORD k = grs->VtxPerm ? grs->VtxPerm[i] : i;
            if (k < g.vtx.size()) grs->Vtx[i] = g.vtx[k]; else ret = 1;
        }
    }
    if (grs->nIdx && grs->Idx) {
        for (DWORD i = 0; i < grs->nIdx; i++) {
            DWORD k = grs->IdxPerm ? grs->IdxPerm[i] : i;
            if (k < g.idx.size()) grs->Idx[i] = g.idx[k]; else ret = 1;
        }
    }
    grs->MtrlIdx = g.mtrl;
    grs->TexIdx = g.tex;
    return ret;
}

int D3D11Client::clbkEditMeshGroup(DEVMESHHANDLE hMesh, DWORD grpidx, GROUPEDITSPEC* ges)
{
    DevMesh* m = (DevMesh*)hMesh;
    if (!m) return -2;
    if (grpidx >= m->grp.size()) return -1;
    DGroup& g = m->grp[grpidx];
    DWORD f = ges->flags;
    if (f & GRPEDIT_SETUSERFLAG) g.usrFlag = ges->UsrFlag;
    else if (f & GRPEDIT_ADDUSERFLAG) g.usrFlag |= ges->UsrFlag;
    else if (f & GRPEDIT_DELUSERFLAG) g.usrFlag &= ~ges->UsrFlag;
    if ((f & GRPEDIT_VTXMOD) && ges->Vtx) {
        for (DWORD i = 0; i < ges->nVtx; i++) {
            DWORD k = ges->vIdx ? ges->vIdx[i] : i;
            if (k >= g.vtx.size()) continue;
            NTVERTEX& d = g.vtx[k]; const NTVERTEX& s = ges->Vtx[i];
            if (f & GRPEDIT_VTXCRDX) d.x = s.x; if (f & GRPEDIT_VTXCRDY) d.y = s.y; if (f & GRPEDIT_VTXCRDZ) d.z = s.z;
            if (f & GRPEDIT_VTXNMLX) d.nx = s.nx; if (f & GRPEDIT_VTXNMLY) d.ny = s.ny; if (f & GRPEDIT_VTXNMLZ) d.nz = s.nz;
            if (f & GRPEDIT_VTXTEXU) d.tu = s.tu; if (f & GRPEDIT_VTXTEXV) d.tv = s.tv;
            if (f & GRPEDIT_VTXCRDADDX) d.x += s.x; if (f & GRPEDIT_VTXCRDADDY) d.y += s.y; if (f & GRPEDIT_VTXCRDADDZ) d.z += s.z;
            if (f & GRPEDIT_VTXNMLADDX) d.nx += s.nx; if (f & GRPEDIT_VTXNMLADDY) d.ny += s.ny; if (f & GRPEDIT_VTXNMLADDZ) d.nz += s.nz;
            if (f & GRPEDIT_VTXTEXADDU) d.tu += s.tu; if (f & GRPEDIT_VTXTEXADDV) d.tv += s.tv;
        }
        m->UploadGroup(ctx.Get(), grpidx);
    }
    return 0;
}

// ------------------------------------------------------------------------------------------ visuals
int D3D11Client::clbkVisEvent(OBJHANDLE hObj, VISHANDLE vis, DWORD msg, DWORD_PTR context)
{
    VesselVis* vv = (VesselVis*)vis;
    if (!vv) return 0;
    switch (msg) {
    case EVENT_VESSEL_INSMESH: vv->DeleteMesh((int)context); vv->CreateMeshes(); break;
    case EVENT_VESSEL_DELMESH: vv->DeleteMesh((int)(INT_PTR)context); break;
    case EVENT_VESSEL_RESETANIM: case EVENT_VESSEL_CLEARANIM: vv->ResetAnimations(); break;
    case EVENT_VESSEL_NEWANIM: case EVENT_VESSEL_DELANIM: vv->ResetAnimations(); break;
    case EVENT_VESSEL_MODMESHGROUP: break;
    default: break;
    }
    return 1;
}

MESHHANDLE D3D11Client::clbkGetMesh(VISHANDLE vis, UINT idx)
{
    VesselVis* vv = (VesselVis*)vis;
    if (!vv || idx >= vv->mesh.size()) return nullptr;
    return (MESHHANDLE)vv->mesh[idx];
}

void D3D11Client::ScanVessels()
{
    // new vessels get a visual; vessels that left the simulation lose it
    std::map<OBJHANDLE, bool> alive;
    for (DWORD i = 0; i < oapiGetVesselCount(); i++) {
        OBJHANDLE h = oapiGetVesselByIndex((int)i);
        if (!h) continue;
        alive[h] = true;
        if (visuals.count(h)) continue;
        VesselVis* vv = new VesselVis;
        vv->hObj = h; vv->v = oapiGetVesselInterface(h);
        vv->CreateMeshes();
        visuals[h] = vv;
        RegisterVisObject(h, (VISHANDLE)vv);
        vv->registered = true;
    }
    for (auto it = visuals.begin(); it != visuals.end();) {
        if (!alive.count(it->first)) { delete it->second; it = visuals.erase(it); }
        else ++it;
    }
}

// ------------------------------------------------------------------------------------------ the frame
struct ObjCB { XMFLOAT4X4 wvp, w; XMFLOAT4 sun, diff, amb, spec, emis, flags; };

void D3D11Client::clbkRenderScene()
{
    if (!dev || !sc || !shadersOk) return;     // a shader that failed to compile is in Orbiter.log; nothing is drawn
    frameNo++;
    if (testMode) diagFrames = (frameNo >= 300 && frameNo < 302) ? 1 : 0;
    FlushDirty();         // surfaces drawn on since the last frame (MFDs, displays) to their textures
    BeginScreenLayer();   // the 2D layer the HUD and the info boxes draw into after this call
    if ((frameNo & 31) == 0) ScanVessels();
    UINT fi = frameNo % 4;
    ctx->Begin(qDis[fi].Get());
    ctx->End(qTs[fi][0].Get());

    VECTOR3 cp; oapiCameraGlobalPos(&cp);
    MATRIX3 cr; oapiCameraRotationMatrix(&cr);
    double ap = oapiCameraAperture();             // half the vertical field of view
    float tanY = (float)tan(ap);
    float n = 0.1f;
    XMMATRIX proj(1.0f / (tanY * W / H), 0, 0, 0,
                  0, 1.0f / tanY, 0, 0,
                  0, 0, 0, 1,
                  0, 0, n, 0);                    // reverse Z, infinite far: depth = n / z
    XMMATRIX projNoJit = proj;
    { XMMATRIX Vn(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1); XMStoreFloat4x4(&curVPNoJit, Vn * projNoJit); }
    { XMFLOAT2 j = Jitter(); proj.r[2] = XMVectorSet(j.x, j.y, 0, 1); }   // TAA: the sub-pixel offset of this frame

    FxFrame(this, cp, cr, proj, oapiGetSimStep());   // effects' compute work (waves, spray) before the scene
    RenderShadows(cp);
    D3D11_VIEWPORT vp = { 0, 0, (float)RW, (float)RH, 0, 1 };    // the scene's size (TAA render scale)
    ctx->RSSetViewports(1, &vp);
    float clr[4] = { 0, 0, 0, 1 };
    ctx->ClearRenderTargetView(hdrRTV.Get(), clr);
    ctx->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
    ID3D11SamplerState* ss[3] = { sampAniso.Get(), sampClamp.Get(), sampShadow.Get() };
    ctx->PSSetSamplers(0, 3, ss);
    ctx->PSSetShaderResources(7, 1, shadowSRV.GetAddressOf());
    XMMATRIX V(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
    UpdateFrameCB(cp, XMMatrixInverse(nullptr, V * proj));
    if (skyOn) {
        ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
        ctx->OMSetDepthStencilState(dsRead.Get(), 0);
        ctx->RSSetState(rsNone.Get());
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vsSky.Get(), nullptr, 0);
        ctx->PSSetShader(psSky.Get(), nullptr, 0);
        ctx->Draw(3, 0);
    }
    RenderStars(cr, proj, starVis);

    // planets, the farthest first (one reverse-Z depth range holds them all; the order only helps early depth rejection)
    std::vector<std::pair<double, Planet*>> order;
    for (Planet* p : planets) { VECTOR3 pp; oapiGetGlobalPos(p->hObj, &pp); order.push_back({ -length(pp - cp), p }); }
    std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::reverse(order.begin(), order.end());
    int tiles = 0;
    for (auto& o : order) { o.second->Render(ctx.Get(), cp, cr, proj, tanY, (int)H); tiles += o.second->tilesDrawn; }
    for (Planet* p : planets) p->Purge(frameNo);
    ctx->End(qTs[fi][1].Get());

    // vessels: opaque groups, then the transparent ones
    if (mvRTV) {         // the opaque vessels also write their motion vectors
        float none[4] = { 1000, 1000, 0, 0 };
        ctx->ClearRenderTargetView(mvRTV.Get(), none);
        ID3D11RenderTargetView* rt2[2] = { hdrRTV.Get(), mvRTV.Get() };
        ctx->OMSetRenderTargets(2, rt2, dsv.Get());
    }
    for (auto& kv : visuals) RenderVessel(kv.second, cp, cr, proj, false);
    if (mvRTV) ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
    for (auto& kv : visuals) RenderVessel(kv.second, cp, cr, proj, true);
    RenderLiquid(cp, cr, proj, tanY);      // over the opaque scene, reading its colour and depth
    FxRenderTransparent(this, cp, cr, proj);      // spray and mist over the liquid
    UpdateRenderParticles(cp, cr, proj, oapiGetSimStep());
    ctx->End(qTs[fi][2].Get());

    // the single-sampled image (resolve / TAA), tone mapping (+ FXAA) into the back buffer
    {
        XMMATRIX Vt(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
        ID3D11ShaderResourceView* img = FinishScene(cp, Vt * proj, Vt * projNoJit);
        ToneMapAndAA(img);
    }
    // 2D panels (drawn now on the GPU); the HUD and the MFDs come through the Sketchpad after this call returns
    Render2DOverlay();
    ctx->End(qTs[fi][3].Get());
    ctx->End(qDis[fi].Get());

    if (frameNo >= 4) {
        UINT pf = (frameNo - 3) % 4;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
        if (ctx->GetData(qDis[pf].Get(), &dj, sizeof dj, 0) == S_OK && !dj.Disjoint) {
            UINT64 t[4]; bool ok = true;
            for (int i = 0; i < 4; i++) ok = ok && ctx->GetData(qTs[pf][i].Get(), &t[i], 8, 0) == S_OK;
            if (ok) for (int i = 0; i < 3; i++) passMs[i] = 0.9 * passMs[i] + 0.1 * (double)(t[i + 1] - t[i]) * 1000.0 / dj.Frequency;
        }
    }
    // self-check: a line in Orbiter.log every 10 s, a screenshot 15 s after the start
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (g_cfg.statsLog && (now.QuadPart - tLog.QuadPart) / (double)qpf.QuadPart > 10.0) {
        tLog = now;
        size_t q; { std::lock_guard<std::mutex> lk(loadMx); q = loadQ.size(); }
        LOGC("fps %.0f | GPU ms: planets %.2f, vessels %.2f, post %.2f | tiles drawn %d, load queue %zu | vessels %zu",
             oapiGetFrameRate(), passMs[0], passMs[1], passMs[2], tiles, q, visuals.size());
    }
}

bool D3D11Client::clbkDisplayFrame()
{
    if (!sc) return false;
    if (testMode && IsWindowVisible(hWnd)) ShowWindow(hWnd, SW_HIDE);
    CallRenderProcs();    // the modules' HUD procs (gcCore RegisterRenderProc)
    CompositeScreenLayer();
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!shotDone && (now.QuadPart - tStart.QuadPart) / (double)qpf.QuadPart > 15.0) {
        shotDone = true;
        CreateDirectoryA("Modules\\D3D11Client\\shots", nullptr);
        SaveShot("Modules\\D3D11Client\\shots\\start.png");
    }
    sc->Present(testMode || GetVideoData()->novsync ? 0 : 1, 0);   // uncapped when vsync is off (and in test runs)
    return true;
}

void D3D11Client::SaveShot(const char* path)
{
    D3D11_TEXTURE2D_DESC d; bbTex->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> st; if (FAILED(dev->CreateTexture2D(&d, nullptr, &st))) return;
    ctx->CopyResource(st.Get(), bbTex.Get());
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &ms))) return;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<IWICImagingFactory> wf;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wf));
    ComPtr<IWICStream> s;
    if (wf && SUCCEEDED(wf->CreateStream(&s))) {
        wchar_t wp[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, path, -1, wp, MAX_PATH);
        if (SUCCEEDED(s->InitializeFromFilename(wp, GENERIC_WRITE))) {
            ComPtr<IWICBitmapEncoder> enc; wf->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
            enc->Initialize(s.Get(), WICBitmapEncoderNoCache);
            ComPtr<IWICBitmapFrameEncode> fr; enc->CreateNewFrame(&fr, nullptr);
            fr->Initialize(nullptr); fr->SetSize(d.Width, d.Height);
            WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA; fr->SetPixelFormat(&pf);
            std::vector<BYTE> px((size_t)d.Width * d.Height * 4);
            for (UINT y = 0; y < d.Height; y++) memcpy(&px[(size_t)y * d.Width * 4], (BYTE*)ms.pData + (size_t)y * ms.RowPitch, d.Width * 4);
            for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
            fr->WritePixels(d.Height, d.Width * 4, (UINT)px.size(), px.data());
            fr->Commit(); enc->Commit();
            LOGC("screenshot %s", path);
        }
    }
    ctx->Unmap(st.Get(), 0);
}

// ------------------------------------------------------------------------------------------ the loading screen
bool D3D11Client::clbkSplashLoadMsg(const char* msg, int line)
{
    if (!d2rt || !sc) return false;
    loadSteps++;
    if (line == 0 && msg) loadGroup = msg;
    float W2 = (float)W, H2 = (float)H;
    float frac = std::min(1.0f, (float)loadSteps / (float)loadExpected);
    d2rt->BeginDraw();
    d2rt->Clear(D2D1::ColorF(0.04f, 0.035f, 0.03f));
    ComPtr<ID2D1SolidColorBrush> fg, bar, back;
    d2rt->CreateSolidColorBrush(D2D1::ColorF(0.92f, 0.86f, 0.78f), &fg);
    d2rt->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.55f, 0.22f), &bar);
    d2rt->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.17f, 0.14f), &back);
    float bw = W2 * 0.5f, bx = (W2 - bw) / 2, by = H2 * 0.62f;
    d2rt->DrawText(L"Orbiter \u2014 D3D11Client", 22, loadFontBig.Get(), D2D1::RectF(bx, by - 90, bx + bw, by - 50), fg.Get());
    d2rt->FillRectangle(D2D1::RectF(bx, by, bx + bw, by + 10), back.Get());
    d2rt->FillRectangle(D2D1::RectF(bx, by, bx + bw * frac, by + 10), bar.Get());
    wchar_t buf[512];
    std::string text = loadGroup + (line && msg ? std::string(": ") + msg : std::string());
    int n = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, buf, 480);
    if (n > 0) d2rt->DrawText(buf, (UINT32)wcslen(buf), loadFont.Get(), D2D1::RectF(bx, by + 24, bx + bw, by + 60), fg.Get());
    d2rt->EndDraw();
    sc->Present(0, 0);
    return true;
}

// ------------------------------------------------------------------------------------------ per-frame constants: the atmosphere
struct FrameCB { XMFLOAT4X4 invVP; XMFLOAT4 sunDir, sky0, sky1, amb, sun, up, planet, hazeRGB; XMFLOAT4X4 lightVP; XMFLOAT4 shadow; };

void D3D11Client::UpdateFrameCB(const VECTOR3& cp, const XMMATRIX& invVP)
{
    FrameCB f = {};
    XMStoreFloat4x4(&f.invVP, XMMatrixTranspose(invVP));
    // the Sun
    VECTOR3 sp = _V(0, 0, 0);
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) { oapiGetGlobalPos(h, &sp); break; } }
    VECTOR3 sd = sp - cp; sd = sd / length(sd);
    // the planet whose atmosphere the camera is in (the nearest one, below its top)
    Planet* ap = nullptr; double best = 1e300; VECTOR3 pp = _V(0, 0, 0);
    for (Planet* p : planets) {
        if (!p->atm.has) continue;
        VECTOR3 q; oapiGetGlobalPos(p->hObj, &q);
        double alt = length(cp - q) - p->R;
        if (alt < p->atm.top && alt < best) { best = alt; ap = p; pp = q; }
    }
    skyOn = ap != nullptr;
    starVis = 1;
    f.sunDir = { (float)sd.x, (float)sd.y, (float)sd.z, 1 };
    if (ap) {
        const AtmCfg& a = ap->atm;
        VECTOR3 up = cp - pp; up = up / length(up);
        double alt = best;
        double day = std::clamp((dotp(up, sd) + 0.08) / 0.25, 0.0, 1.0);      // the Sun from 5 deg below to ~10 deg above the horizon
        double fade = exp(-std::max(alt, 0.0) / (a.scaleH * 2.5));             // the sky thins out with height
        f.sunDir.w = (float)day;
        // the stars through the air: hidden by a bright sky and by the haze's vertical optical depth (Titan ~3)
        starVis = (float)((1 - day * fade) * exp(-a.hazeExt * g_cfg.haze * a.scaleH * fade));
        f.sky0 = { (float)a.horizon.x, (float)a.horizon.y, (float)a.horizon.z, 1 };
        f.sky1 = { (float)a.zenith.x, (float)a.zenith.y, (float)a.zenith.z, (float)fade };
        f.amb = { (float)a.ambient.x, (float)a.ambient.y, (float)a.ambient.z, (float)(a.hazeExt * g_cfg.haze) };
        // the direct beam: reddened and dimmed through the air, towards space it brightens to white
        VECTOR3 sun = a.sun;
        f.sun = { (float)sun.x, (float)sun.y, (float)sun.z, (float)a.scaleH };
        f.up = { (float)up.x, (float)up.y, (float)up.z, (float)alt };
        VECTOR3 pc = pp - cp;
        f.planet = { (float)pc.x, (float)pc.y, (float)pc.z, (float)ap->R };
        f.hazeRGB = { (float)a.hazeRGB.x, (float)a.hazeRGB.y, (float)a.hazeRGB.z, 0 };
    }
    if (g_fxAtmo.active) { skyOn = false; starVis = g_fxAtmo.starVis; f.amb.w = 0; }   // the FX pass draws sky and haze
    f.hazeRGB.w = g_cfg.exposure * (g_fxAtmo.active ? g_fxAtmo.exposure : 1.0f);
    f.lightVP = lightVP;
    f.shadow = { shadowOn ? 1.0f : 0.0f, 1.0f / SHADOW_N, 0, 0 };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(cbFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &f, sizeof f); ctx->Unmap(cbFrame.Get(), 0);
    ctx->VSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
    ctx->PSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
}

// ------------------------------------------------------------------------------------------ shadows
void D3D11Client::RenderShadows(const VECTOR3& cp)
{
    shadowOn = false;
    ID3D11ShaderResourceView* nul = nullptr;
    ctx->PSSetShaderResources(7, 1, &nul);
    OBJHANDLE hf = oapiGetFocusObject();
    if (!g_cfg.shadows || !hf || !visuals.count(hf)) return;
    VECTOR3 fp; oapiGetGlobalPos(hf, &fp);
    VECTOR3 sp = _V(0, 0, 0);
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) { oapiGetGlobalPos(h, &sp); break; } }
    VECTOR3 L = sp - fp; L = L / length(L);
    double rs = std::clamp(oapiGetSize(hf) * 4.0, 25.0, 400.0);
    // light frame around the focus (camera-centred global coordinates), the box centred on the focus
    VECTOR3 c = fp - cp;
    XMVECTOR Lv = XMVectorSet((float)L.x, (float)L.y, (float)L.z, 0);
    XMVECTOR upv = fabs(L.y) < 0.9 ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(1, 0, 0, 0);
    XMVECTOR eye = XMVectorSet((float)c.x, (float)c.y, (float)c.z, 1) + Lv * 2000.0f;
    XMMATRIX LV = XMMatrixLookToLH(eye, -Lv, upv);
    XMMATRIX LP = XMMatrixOrthographicLH((float)(2 * rs), (float)(2 * rs), 1.0f, 4000.0f);
    XMVECTOR ctr = XMVector3TransformCoord(XMVectorSet((float)c.x, (float)c.y, (float)c.z, 1), LV);
    float sx = XMVectorGetX(ctr), sy = XMVectorGetY(ctr);
    XMMATRIX snap = XMMatrixTranslation(-sx, -sy, 0);   // the box centred on the focus
    XMMATRIX LVP = LV * snap * LP;
    XMStoreFloat4x4(&lightVP, XMMatrixTranspose(LVP));
    D3D11_VIEWPORT vp = { 0, 0, (float)SHADOW_N, (float)SHADOW_N, 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->ClearDepthStencilView(shadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->OMSetRenderTargets(0, nullptr, shadowDSV.Get());
    for (auto& kv : visuals) {
        VECTOR3 vp2; oapiGetGlobalPos(kv.first, &vp2);
        if (length(vp2 - fp) > rs + oapiGetSize(kv.first)) continue;
        RenderVessel(kv.second, cp, _M(1, 0, 0, 0, 1, 0, 0, 0, 1), XMMatrixIdentity(), false, &LVP);
    }
    shadowOn = true;
}
