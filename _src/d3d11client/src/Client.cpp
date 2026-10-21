// D3D11Client: the module, the device and the window, textures and surfaces, Orbiter's visual events, the frame.
#define ORBITER_MODULE   // once: the module boilerplate (ModuleDate, calldummy)
#include "Client.h"
#include <wincodec.h>
#include "Fx.h"
#include "Exhaust.h"
#include "Wake.h"
#include <cstdio>
#include <exception>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")
#include <typeinfo>

// an exception nobody caught ends the process without a crash dump (std::terminate -> abort -> fast fail 0xC0000409):
// what it was and where it came from go to Orbiter.log first (module + offset per frame, for the build's PDB)
bool g_traceAlloc = false;     // test runs: log big allocations (TestScript turns it on)
int g_traceFrames = 0;
void LogMemory(const char* when);
void HangWatchStart(); void HangWatchStop();     // HangWatch.cpp
void HeapTrackStart();     // HeapTrack.cpp: the heap by module, test runs with "HeapByModule = 1" only
// what the client itself holds (LogMemory): the surfaces made for drawing (on the GPU) and their CPU copies (DIBs, private
// memory of the process)
long g_surfLive = 0, g_dibLive = 0; double g_surfMB = 0, g_dibMB = 0;
static void LogTerminate()
{
    char what[256] = "unknown";
    if (std::exception_ptr e = std::current_exception()) {
        try { std::rethrow_exception(e); }
        catch (const std::exception& x) { snprintf(what, sizeof what, "%s: %s", typeid(x).name(), x.what()); }
        catch (...) { snprintf(what, sizeof what, "a non-std exception"); }
    } else snprintf(what, sizeof what, "no active exception (terminate called directly)");
    oapiWriteLogV("D3D11Client: FATAL terminate: %s", what);
    void* fr[48] = {};
    const USHORT n = CaptureStackBackTrace(0, 48, fr, nullptr);
    for (USHORT i = 0; i < n; i++) {
        HMODULE m = nullptr; char path[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)fr[i], &m) && m)
            GetModuleFileNameA(m, path, MAX_PATH);
        const char* base = strrchr(path, 92);   // '\\'
        oapiWriteLogV("D3D11Client:   #%02u %s+0x%x", i, base ? base + 1 : path, (unsigned)((const char*)fr[i] - (const char*)m));
    }
    abort();
}

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
    HeapTrackStart();    // HeapTrack.cpp: test runs with "HeapByModule = 1" only
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
// Alt pressed and let go alone puts a window into the menu mode (WM_SYSCOMMAND SC_KEYMENU): Windows runs its own modal
// loop until another key or a click, and Orbiter's loop - the render, the simulation - stands still (the user,
// 2026-10-20: «при нажатии L_ALT орбитер замирает»; the crew's marks are held on Alt). The render window has no menu:
// that command is dropped; Alt itself still reaches the modules (their keys, Alt+F4 closes as before)
LRESULT D3D11Client::RenderWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_KEYMENU) return 0;
    return oapi::GraphicsClient::RenderWndProc(hWnd, uMsg, wParam, lParam);
}

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
    {   // test runs: the frame at a size of its own, whatever the window (D3D11CLIENT_TEST_RES = 2560x1440: a tester's 2K
        // screen on a smaller one - the swap chain is that size and is scaled into the window when shown)
        char rs[32] = ""; unsigned tw = 0, th = 0;
        if (GetEnvironmentVariableA("D3D11CLIENT_TEST_RES", rs, sizeof rs) && sscanf(rs, "%ux%u", &tw, &th) == 2 && tw >= 320 && th >= 200) {
            W = tw; H = th;
            LOGC("test: frame forced to %ux%u (window %ldx%ld)", W, H, rc.right, rc.bottom);
        }
    }
    // test runs (tools/run_test.ps1 sets D3D11CLIENT_TEST): the window never shows and never takes the focus
    std::set_terminate(LogTerminate);     // (this thread: the render and simulation loop)
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
    if (shadersOk) { FxInit(this); ExhaustInit(this); }
    loaderRun = true;
    {   // the tile loaders: a few threads (the device makes its resources from any of them); each holds a tile's working
        // buffers (~10 MB at most), so not more than 6 in the 32-bit address space
        const int nl = std::clamp((int)std::thread::hardware_concurrency() - 2, 2, 6);
        for (int k = 0; k < nl; k++) loaders.emplace_back(&D3D11Client::LoaderMain, this);
        LOGC("tile loader: %d threads", nl);
    }
    HangWatchStart();       // a freeze of 4 s leaves every thread's stack in the log (HangWatch.cpp)
    QueryPerformanceFrequency(&qpf); QueryPerformanceCounter(&tStart); tLog = tStart;
    LOGC("render window %ux%u, D3D11 device ready", W, H);
    return hWnd;
}

// The video driver's shader cache (NVIDIA: %LOCALAPPDATA%\NVIDIA\DXCache\<per program>.nvph). The driver maps the whole
// file into the process when the device is made, and the file only grows (each new shader adds to it, the file doubling:
// 256, 512, 1024 MB). In a 32-bit Orbiter a 1 GB cache takes half the address space and later allocations fail.
// After the device is made, the cache files it mapped are written to Modules\D3D11Client\drivercache.txt; at the next
// start, before the device, a file over DriverCacheMB is moved to ...\NVIDIA\DXCache_Orbiter_old (replacing the copy
// there): the driver starts a new small one and fills it again with the shaders it meets.
static const wchar_t* kCacheList = L"Modules\\D3D11Client\\drivercache.txt";
static void TrimDriverCache()
{
    if (g_cfg.driverCacheMB <= 0) return;
    FILE* f = _wfopen(kCacheList, L"r, ccs=UTF-8");
    if (!f) return;
    wchar_t line[MAX_PATH];
    while (fgetws(line, MAX_PATH, f)) {
        size_t n = wcslen(line); while (n && (line[n - 1] == L'\n' || line[n - 1] == L'\r')) line[--n] = 0;
        if (!n) continue;
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExW(line, GetFileExInfoStandard, &fa)) continue;
        const unsigned long long sz = ((unsigned long long)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        if (sz <= (unsigned long long)g_cfg.driverCacheMB << 20) continue;
        const std::wstring src = line, dir = src.substr(0, src.find_last_of(L'\\'));
        const std::wstring old = dir.substr(0, dir.find_last_of(L'\\')) + L"\\DXCache_Orbiter_old";
        CreateDirectoryW(old.c_str(), nullptr);
        const std::wstring dst = old + src.substr(src.find_last_of(L'\\'));
        const bool ok = MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
        LOGC("driver shader cache %u MB (over %d): %s", (unsigned)(sz >> 20), g_cfg.driverCacheMB,
             ok ? "set aside, the driver starts a new one" : "could not be moved (in use?)");
    }
    fclose(f);
}

// the driver's cache files mapped into the process: their names for the next start, their size in the log
static void NoteDriverCache()
{
    wchar_t la[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) return;
    std::vector<std::wstring> files; unsigned long long total = 0;
    MEMORY_BASIC_INFORMATION mi; const void* lastBase = nullptr;
    for (const char* a = nullptr; VirtualQuery(a, &mi, sizeof mi); a = (const char*)mi.BaseAddress + mi.RegionSize) {
        if (mi.State != MEM_FREE && mi.Type == MEM_MAPPED && mi.AllocationBase != lastBase) {
            lastBase = mi.AllocationBase;
            wchar_t fn[MAX_PATH] = {};
            if (GetMappedFileNameW(GetCurrentProcess(), mi.AllocationBase, fn, MAX_PATH)) {
                const std::wstring s = fn; const size_t k = s.find(L"\\NVIDIA\\DXCache\\");
                if (k != std::wstring::npos) {
                    const std::wstring p = std::wstring(la) + s.substr(k);
                    if (std::find(files.begin(), files.end(), p) == files.end()) {
                        files.push_back(p);
                        WIN32_FILE_ATTRIBUTE_DATA fa;
                        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) total += fa.nFileSizeLow;
                    }
                }
            }
        }
        if ((const char*)mi.BaseAddress + mi.RegionSize < (const char*)mi.BaseAddress) break;
    }
    if (files.empty()) return;
    CreateDirectoryA("Modules\\D3D11Client", nullptr);
    if (FILE* f = _wfopen(kCacheList, L"w, ccs=UTF-8")) { for (auto& p : files) fwprintf(f, L"%s\n", p.c_str()); fclose(f); }
    LOGC("driver shader cache: %u file(s), %u MB mapped (set aside at the next start above %d MB)",
         (unsigned)files.size(), (unsigned)(total >> 20), g_cfg.driverCacheMB);
}

// The driver's disk cache kept out of the process (DriverCacheOff = 1 in Client.cfg): the NVIDIA cache is opened by the
// driver's NvMemMapStorage.dll with CreateFileW and mapped whole (256, 512, 1024 MB: a quarter to a half of a 32-bit
// process's addresses, and it grows during a run - the crash in NvMemMapStorage when no free block is big enough).
// The library is loaded here before the device (its path, as the driver loads it, noted at the previous start in
// Modules\D3D11Client\nvmemmap.txt) and its CreateFileW import pointed at ours, which refuses *.nvph: the driver then
// works without its disk cache, in this process only (other programs keep theirs; nothing in the system is changed).
static const wchar_t* kMemMapList = L"Modules\\D3D11Client\\nvmemmap.txt";
typedef HANDLE(WINAPI* CreateFileWF)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileWF g_realCreateFileW = nullptr;
static volatile LONG g_nvphRefused = 0;
static HANDLE WINAPI NoNvphCreateFileW(LPCWSTR n, DWORD a, DWORD sh, LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fl, HANDLE t)
{
    if (n) { const size_t l = wcslen(n); if (l > 5 && !_wcsicmp(n + l - 5, L".nvph")) { InterlockedIncrement(&g_nvphRefused); SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; } }
    return g_realCreateFileW(n, a, sh, sa, cd, fl, t);
}
static bool PatchImport(HMODULE h, const char* dllName, const char* fn, void* to, void** real)
{
    auto* b = (BYTE*)h;
    auto* nt = (IMAGE_NT_HEADERS*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    bool done = false;
    for (auto* d = (IMAGE_IMPORT_DESCRIPTOR*)(b + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp((const char*)(b + d->Name), dllName) || !d->OriginalFirstThunk) continue;
        auto* nm = (IMAGE_THUNK_DATA*)(b + d->OriginalFirstThunk); auto* iat = (IMAGE_THUNK_DATA*)(b + d->FirstThunk);
        for (; nm->u1.AddressOfData; nm++, iat++) {
            if (IMAGE_SNAP_BY_ORDINAL(nm->u1.Ordinal)) continue;
            if (strcmp((const char*)((IMAGE_IMPORT_BY_NAME*)(b + nm->u1.AddressOfData))->Name, fn)) continue;
            DWORD old;
            if (!VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) continue;
            if (real && !*real) *real = (void*)iat->u1.Function;
            iat->u1.Function = (DWORD_PTR)to;
            VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
            done = true;
        }
    }
    return done;
}
// the cache library without a previous start (a fresh install - its first start compiles every shader, the cache grows
// the most then): the NVIDIA driver's folder in the DriverStore, from its 32-bit user-mode driver's path in the display
// adapters' class key (UserModeDriverNameWow); NvMemMapStorage.dll sits in that same folder
static bool FindNvMemMapLib(wchar_t* out, DWORD cap)
{
    const wchar_t* cls = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
    for (int i = 0; i < 16; i++) {
        wchar_t key[160]; swprintf(key, 160, L"%s\\%04d", cls, i);
        wchar_t prov[64] = {}; DWORD sz = sizeof prov;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"ProviderName", RRF_RT_REG_SZ, nullptr, prov, &sz) != ERROR_SUCCESS) continue;
        if (!wcsstr(prov, L"NVIDIA")) continue;
        wchar_t umd[1024] = {}; sz = sizeof umd;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"UserModeDriverNameWow", RRF_RT_REG_MULTI_SZ, nullptr, umd, &sz) != ERROR_SUCCESS || !umd[0]) continue;
        wchar_t* slash = wcsrchr(umd, L'\\');
        if (!slash) continue;
        *slash = 0;
        swprintf(out, cap, L"%s\\NvMemMapStorage.dll", umd);
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return true;
    }
    return false;
}
static void DriverCacheGuardBefore()
{
    if (!g_cfg.driverCacheOff) return;
    wchar_t path[MAX_PATH] = {};
    if (FILE* f = _wfopen(kMemMapList, L"r, ccs=UTF-8")) { fgetws(path, MAX_PATH, f); fclose(f); }
    size_t n = wcslen(path); while (n && (path[n - 1] == L'\n' || path[n - 1] == L'\r')) path[--n] = 0;
    if (!n || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {   // first start, or the driver updated since
        if (!FindNvMemMapLib(path, MAX_PATH)) { LOGC("driver shader cache: off asked, no NVIDIA cache library found (another vendor's card, or noted at this start)"); return; }
        n = wcslen(path);
        LOGC("driver shader cache: the cache library found by the driver's registry entry");
    }
    HMODULE h = n ? LoadLibraryW(path) : nullptr;      // kept loaded: the driver gets this same module
    if (!h) { LOGC("driver shader cache: off asked, the cache library could not be loaded (driver updated?) - the cache stays on"); return; }
    const bool ok = PatchImport(h, "KERNEL32.dll", "CreateFileW", (void*)NoNvphCreateFileW, (void**)&g_realCreateFileW) && g_realCreateFileW;
    LOGC("driver shader cache: %s", ok ? "off for this process (the driver's cache library may not open *.nvph)" : "off asked, the library's import was not found - the cache stays on");
}
static void DriverCacheGuardAfter()
{
    HMODULE h = GetModuleHandleW(L"NvMemMapStorage.dll");
    if (h) {
        wchar_t p[MAX_PATH] = {};
        if (GetModuleFileNameW(h, p, MAX_PATH)) {
            CreateDirectoryA("Modules\\D3D11Client", nullptr);
            if (FILE* f = _wfopen(kMemMapList, L"w, ccs=UTF-8")) { fwprintf(f, L"%s\n", p); fclose(f); }
        }
    }
    if (g_cfg.driverCacheOff) LOGC("driver shader cache: %ld cache file open(s) refused", g_nvphRefused);
}

unsigned g_devGen = 0;
bool D3D11Client::CreateDevice()
{
    g_devGen++;
    TrimDriverCache();
    DriverCacheGuardBefore();
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
    UINT devFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (!g_cfg.driverThreads) devFlags |= D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;   // DriverThreads = 0
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, devFlags, &fl, 1, D3D11_SDK_VERSION, &dev, &got, &ctx);
    if (FAILED(hr)) { LOGC("D3D11CreateDevice failed 0x%08x", (unsigned)hr); return false; }
    NoteDriverCache();
    DriverCacheGuardAfter();
    ComPtr<IDXGIDevice> dxd; dev.As(&dxd);
    ComPtr<IDXGIAdapter> ad; dxd->GetAdapter(&ad);
    DXGI_ADAPTER_DESC add; ad->GetDesc(&add);
    char an[128]; WideCharToMultiByte(CP_UTF8, 0, add.Description, -1, an, 128, nullptr, nullptr);
    // the driver's version with it (a crash inside a driver depends on it: testers' reports, 2026-10-21)
    LARGE_INTEGER umd = {}; char dv[48] = "unknown";
    if (SUCCEEDED(ad->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd)))
        sprintf_s(dv, "%u.%u.%u.%u", (unsigned)HIWORD(umd.HighPart), (unsigned)LOWORD(umd.HighPart), (unsigned)HIWORD(umd.LowPart), (unsigned)LOWORD(umd.LowPart));
    LOGC("adapter %s, %u MB, driver %s, driver threads %s", an, (unsigned)(add.DedicatedVideoMemory >> 20), dv, g_cfg.driverThreads ? "on" : "off (DriverThreads = 0)");
    {   // ... and the processor (a race in a driver's worker threads shows on some core counts and not others)
        char cpu[96] = "unknown"; DWORD sz = sizeof cpu;
        RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString", RRF_RT_REG_SZ, nullptr, cpu, &sz);
        SYSTEM_INFO si = {}; GetNativeSystemInfo(&si);
        const char* c = cpu; while (*c == ' ') c++;
        LOGC("processor %s, %u logical cores", c, (unsigned)si.dwNumberOfProcessors);
    }
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
    // TAA averages jittered frames: the textures one level sharper (the bias DLSS and FSR prescribe: log2(scale) - 1), or the
    // detail is averaged away
    if (g_cfg.aa == 5) s.MipLODBias = log2f(std::clamp(g_cfg.renderScale, 0.5f, 1.0f)) - 1.0f;
    dev->CreateSamplerState(&s, &sampAniso);
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    dev->CreateSamplerState(&s, &sampClamp);
    auto cb = [&](ComPtr<ID3D11Buffer>& b, UINT size) {
        D3D11_BUFFER_DESC d = { (size + 15) & ~15u, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        dev->CreateBuffer(&d, nullptr, &b);
    };
    cb(cbObj, 1024); cb(cbGrp, 256); cb(cbFrame, 1024);     // cbObj: the planet tiles need more than 512
    // the shadow map
    {
        // four cascades in one atlas, 2 x 2 tiles of SHADOW_N (RenderShadows)
        D3D11_TEXTURE2D_DESC st = { 2 * SHADOW_N, 2 * SHADOW_N, 1, 1, DXGI_FORMAT_R32_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
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
        rs2.DepthBias = 0; rs2.SlopeScaledDepthBias = 1.5f; rs2.DepthBiasClamp = 0.002f;   // + the normal offset in the shader
        dev->CreateRasterizerState(&rs2, &rsShadow);
        // the static casters' atlas (the same layout) and the tile copy/clear (shadow_cache.hlsl); without them every tile
        // is drawn whole each frame, as before
        shadowStatTex.Reset(); shadowStatDSV.Reset(); shadowStatSRV.Reset(); vsShTile.Reset(); psShCopy.Reset(); psShClear.Reset(); dsShAlways.Reset();
        for (auto& k : shKey) k.valid = false;
        if (SUCCEEDED(dev->CreateTexture2D(&st, nullptr, &shadowStatTex))) {
            dev->CreateDepthStencilView(shadowStatTex.Get(), &sdv, &shadowStatDSV);
            dev->CreateShaderResourceView(shadowStatTex.Get(), &ssv, &shadowStatSRV);
        }
        auto bv = CompileShaderFile("shadow_cache.hlsl", "VS", "vs_5_0"), bc = CompileShaderFile("shadow_cache.hlsl", "PSCopy", "ps_5_0"),
             bk = CompileShaderFile("shadow_cache.hlsl", "PSClear", "ps_5_0");
        if (bv && bc && bk) {
            dev->CreateVertexShader(bv->GetBufferPointer(), bv->GetBufferSize(), nullptr, &vsShTile);
            dev->CreatePixelShader(bc->GetBufferPointer(), bc->GetBufferSize(), nullptr, &psShCopy);
            dev->CreatePixelShader(bk->GetBufferPointer(), bk->GetBufferSize(), nullptr, &psShClear);
        }
        D3D11_DEPTH_STENCIL_DESC sa = {}; sa.DepthEnable = TRUE; sa.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; sa.DepthFunc = D3D11_COMPARISON_ALWAYS;
        dev->CreateDepthStencilState(&sa, &dsShAlways);
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
    // the same grid at half the density (every other node, 32 x 32 quads) after it: for tiles whose fine triangles
    // would be a pixel or two on the screen (Planet::Draw) - a quad of pixels shaded for a sliver of a triangle, 8 MSAA
    // samples rasterised for each; the skirts hide the T-junctions against finer neighbours as they do between levels
    for (int i = 0; i < G; i += 2) for (int j = 0; j < G; j += 2)
        ix.insert(ix.end(), { id(i, j), id(i + 2, j), id(i + 2, j + 2), id(i, j), id(i + 2, j + 2), id(i, j + 2) });
    for (int k = 0; k < G; k += 2) {
        uint32_t s0 = sk + k, n0 = sk + (G + 1) + k, w0 = sk + 2 * (G + 1) + k, e0 = sk + 3 * (G + 1) + k;
        ix.insert(ix.end(), { id(0, k), s0, s0 + 2, id(0, k), s0 + 2, id(0, k + 2) });
        ix.insert(ix.end(), { id(G, k), id(G, k + 2), n0 + 2, id(G, k), n0 + 2, n0 });
        ix.insert(ix.end(), { id(k, 0), id(k + 2, 0), w0 + 2, id(k, 0), w0 + 2, w0 });
        ix.insert(ix.end(), { id(k, G), e0, e0 + 2, id(k, G), e0 + 2, id(k + 2, G) });
    }
    tileIdxCount2 = (UINT)ix.size() - tileIdxCount;
    D3D11_BUFFER_DESC ib = { (UINT)(ix.size() * 4), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA isd = { ix.data(), 0, 0 };
    dev->CreateBuffer(&ib, &isd, &tileIB);

    for (int f = 0; f < 4; f++) {
        D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
        dev->CreateQuery(&qd, &qDis[f]);
        qd.Query = D3D11_QUERY_TIMESTAMP;
        for (int i = 0; i < 4; i++) dev->CreateQuery(&qd, &qTs[f][i]);
        for (int i = 0; i < kMarks; i++) dev->CreateQuery(&qd, &qMk[f][i]);
        for (int i = 0; i < kSub; i++) dev->CreateQuery(&qd, &qSub[f][i]);
        for (int i = 0; i < kEx; i++) dev->CreateQuery(&qd, &qEx[f][i]);
    }
    return true;
}

static ComPtr<ID3DBlob> CompileFile(const std::string& path, const char* entry, const char* target);
ComPtr<ID3DBlob> D3D11Client::CompileShaderFile(const char* file, const char* entry, const char* target) { return CompileFile(shaderDir + file, entry, target); }

// The shaders go to the video driver one at a time: each new one is compiled to the GPU's code by the driver on worker
// threads of its own, and a burst of ~120 at the start loaded every core at once - on one tester's machine the driver's
// compiler crashed in that burst every start (nvgpucomp32.dll, 2026-10-21; the same pattern NVIDIA and game studios
// traced to unstable 13th/14th-gen Intel CPUs under all-core load). Here at least kShaderGapMs pass between two, and the
// tile loaders wait while shaders are being made (g_shaderTick)
std::atomic<ULONGLONG> g_shaderTick{ 0 };
static const ULONGLONG kShaderGapMs = 12;
static ComPtr<ID3DBlob> CompileFile(const std::string& path, const char* entry, const char* target)
{
    static std::mutex gapMx;
    std::lock_guard<std::mutex> gap(gapMx);
    {
        const ULONGLONG now = GetTickCount64(), last = g_shaderTick.load();
        if (last && now - last < kShaderGapMs) Sleep((DWORD)(kShaderGapMs - (now - last)));
    }
    struct Stamp { ~Stamp() { g_shaderTick = GetTickCount64(); } } stamp;
    ComPtr<ID3DBlob> code, err;
    std::wstring wp(path.begin(), path.end());
    { void NoteShader(const char*, const char*); NoteShader(path.c_str(), entry); }   // HangWatch.cpp: the last ones go into crash.log
    HRESULT hr = CachedCompileFromFile(wp.c_str(), nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, &code, &err);
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
    {   // the colour pass's variant without the mirror's discard (early depth rejection after the prepass); none: psMesh
        psMeshMain.Reset();
        if (auto b = CompileFile(shaderDir + "mesh.hlsl", "PSMain", "ps_5_0")) dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &psMeshMain);
    }
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
        for (std::thread& th : loaders) if (th.joinable()) th.join();
        loaders.clear();
        HangWatchStop();
    }
    for (Planet* p : planets) delete p;
    planets.clear();
    FxRelease();
    ExhaustRelease();
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
            // the tile that matters most first: nearest the camera for its size (Planet::LoadPrio, kept fresh by the
            // drawing each frame) - the ground under the camera before the far ones. A tile asked for by nobody for
            // half a second goes back unloaded (the view moved on); a child is only ever asked for once its parent is in
            const DWORD now = frameNo;
            // the stale ones out first, the best looked for after (a deque's erase invalidates every iterator)
            loadQ.erase(std::remove_if(loadQ.begin(), loadQ.end(), [&](const std::pair<Planet*, Tile*>& j) {
                if (now - j.second->lastUsed <= 30) return false;
                j.second->state = 0; return true;
            }), loadQ.end());
            if (loadQ.empty()) continue;
            // shaders being made just now (CompileFile): no tiles meanwhile - their textures and buffers would go to the
            // driver alongside its compiles, all cores at once (the start's burst); the queue waits half a second
            {
                extern std::atomic<ULONGLONG> g_shaderTick;
                const ULONGLONG st = g_shaderTick.load();
                if (st && GetTickCount64() - st < 500) {
                    lk.unlock(); Sleep(50); continue;
                }
            }
            auto best = loadQ.begin();
            for (auto it = loadQ.begin(); it != loadQ.end(); ++it) if (it->second->prio < best->second->prio) best = it;
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
    // a surface without alpha starts opaque black (its alpha stays 1: the drawing writes colour only); with alpha, clear.
    // Cleared on the GPU: no CPU copy of the whole surface (a big one does not fit the 32-bit process's address space)
    if (dev && w && h && SUCCEEDED(dev->CreateTexture2D(&td, nullptr, &s->tex))) {
        dev->CreateShaderResourceView(s->tex.Get(), &sd, &s->srv);
        ComPtr<ID3D11DeviceContext> c; dev->GetImmediateContext(&c);
        D3D11_RENDER_TARGET_VIEW_DESC rd = {}; rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11RenderTargetView> rtv;
        if (c && SUCCEEDED(dev->CreateRenderTargetView(s->tex.Get(), &rd, &rtv))) {
            const float k[4] = { 0, 0, 0, (attrib & OAPISURFACE_ALPHA) ? 0.0f : 1.0f };
            c->ClearRenderTargetView(rtv.Get(), k);
        }
    } else if (dev && w && h) LOGC("surface %ux%u not made (attrib 0x%X)", w, h, attrib);
    if (g_traceAlloc && (size_t)w * h * 4 >= (1u << 20)) LOGC("trace: surface %ux%u attrib 0x%X (%.1f MB)", w, h, attrib, w * h * 4 / 1048576.0);
    if (s->tex) { s->counted = true; g_surfLive++; g_surfMB += w * (double)h * 4 / 1048576.0; }
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
    extern thread_local int g_ddsAlpha;
    Surf* s = new Surf; s->srv = srv; s->w = w; s->h = h; s->name = key; s->immutable = true; s->alpha = (signed char)g_ddsAlpha;
    texCache[key] = s;
    if (g_traceAlloc && (size_t)w * h >= (512u << 10)) LOGC("trace: texture %s %ux%u", fname, w, h);
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
    if (s->counted) { g_surfLive--; g_surfMB -= s->w * (double)s->h * 4 / 1048576.0; }
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
    s->Attach(hVessel, ref, dir, lvl); s->engine = true;
    return s;
}

oapi::ParticleStream* D3D11Client::clbkCreateExhaustStream(PARTICLESTREAMSPEC* pss, OBJHANDLE hVessel, const double* lvl, const VECTOR3& ref, const VECTOR3& dir)
{
    PStream* s = new PStream(this, pss); streams.push_back(s);
    s->Attach(hVessel, ref, dir, lvl); s->engine = true;
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

// oapiSetMaterialEx / oapiMeshMaterialEx: the material's parts one by one (a module recolours its glow this way: the
// jet pack's chambers by the wall's temperature). Emission is D3D9's "emission2": output = max(lit, emission)
int D3D11Client::clbkSetMeshMaterialEx(DEVMESHHANDLE hMesh, DWORD matidx, MatProp prp, const oapi::FVECTOR4* in)
{
    DevMesh* m = (DevMesh*)hMesh;
    if (!m || !in) return 2;
    if (matidx >= m->mtrl.size()) return 4;
    MATERIAL& t = m->mtrl[matidx];
    switch (prp) {
    case Diffuse:  t.diffuse = { in->r, in->g, in->b, in->a }; break;
    case Ambient:  t.ambient = { in->r, in->g, in->b, t.ambient.a }; break;
    case Specular: t.specular = { in->r, in->g, in->b, t.specular.a }; t.power = in->a; break;
    case Light:    t.emissive = { in->r, in->g, in->b, t.emissive.a }; break;
    case Emission:
        if (m->emis2.size() < m->mtrl.size()) m->emis2.resize(m->mtrl.size(), XMFLOAT4(0, 0, 0, 0));
        m->emis2[matidx] = { in->r, in->g, in->b, 1 };
        break;
    default: return 3;     // reflectivity, smoothness, metalness, Fresnel, heat: not kept per material here
    }
    return 0;
}

int D3D11Client::clbkMeshMaterialEx(DEVMESHHANDLE hMesh, DWORD matidx, MatProp prp, oapi::FVECTOR4* out)
{
    DevMesh* m = (DevMesh*)hMesh;
    if (!m || !out) return 2;
    if (matidx >= m->mtrl.size()) return 4;
    const MATERIAL& t = m->mtrl[matidx];
    switch (prp) {
    case Diffuse:  *out = oapi::FVECTOR4(t.diffuse.r, t.diffuse.g, t.diffuse.b, t.diffuse.a); break;
    case Ambient:  *out = oapi::FVECTOR4(t.ambient.r, t.ambient.g, t.ambient.b, 1.0f); break;
    case Specular: *out = oapi::FVECTOR4(t.specular.r, t.specular.g, t.specular.b, (float)t.power); break;
    case Light:    *out = oapi::FVECTOR4(t.emissive.r, t.emissive.g, t.emissive.b, 1.0f); break;
    case Emission: { XMFLOAT4 e = matidx < m->emis2.size() ? m->emis2[matidx] : XMFLOAT4(0, 0, 0, 0); *out = oapi::FVECTOR4(e.x, e.y, e.z, 1.0f); break; }
    default: return 3;
    }
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

void D3D11Client::AddVisual(OBJHANDLE h)
{
    if (!h || visuals.count(h)) return;
    VesselVis* vv = new VesselVis;
    vv->hObj = h; vv->v = oapiGetVesselInterface(h);
    vv->CreateMeshes();
    visuals[h] = vv;
    RegisterVisObject(h, (VISHANDLE)vv);
    vv->registered = true;
}

void D3D11Client::RemoveVisual(OBJHANDLE h)
{
    auto it = visuals.find(h);
    if (it == visuals.end()) return;
    VesselVis* vv = it->second;
    visuals.erase(it);
    if (vv->registered) UnregisterVisObject(h);
    delete vv;
}

void D3D11Client::clbkNewVessel(OBJHANDLE hVessel) { AddVisual(hVessel); }

void D3D11Client::clbkDeleteVessel(OBJHANDLE hVessel)
{
    RemoveVisual(hVessel);
    {   // what the modules sent for it (jets, wakes, heat): gone with it
        std::lock_guard<std::mutex> lk(g_ext.mx);
        for (auto it = g_ext.jets.begin(); it != g_ext.jets.end();) it = it->first.h == hVessel ? g_ext.jets.erase(it) : std::next(it);
        for (auto it = g_ext.wakes.begin(); it != g_ext.wakes.end();) it = it->first.h == hVessel ? g_ext.wakes.erase(it) : std::next(it);
        for (auto it = g_ext.heat.begin(); it != g_ext.heat.end();) it = it->first.h == hVessel ? g_ext.heat.erase(it) : std::next(it);
    }
    WakeForget(hVessel);
    { void LightsApiForget(OBJHANDLE); LightsApiForget(hVessel); }   // its interior volumes and area lamps (LightsApi.cpp)
}

void D3D11Client::ScanVessels()
{
    // a fallback for the callbacks: new vessels get a visual; vessels that left (or whose handle now belongs to another
    // vessel) lose theirs
    std::map<OBJHANDLE, bool> alive;
    for (DWORD i = 0; i < oapiGetVesselCount(); i++) {
        OBJHANDLE h = oapiGetVesselByIndex((int)i);
        if (!h) continue;
        alive[h] = true;
        auto it = visuals.find(h);
        if (it != visuals.end() && it->second->v != oapiGetVesselInterface(h)) RemoveVisual(h);
        AddVisual(h);
    }
    for (auto it = visuals.begin(); it != visuals.end();) {
        if (!alive.count(it->first)) { OBJHANDLE h = it->first; ++it; RemoveVisual(h); }
        else ++it;
    }
}

// ------------------------------------------------------------------------------------------ the frame
struct ObjCB { XMFLOAT4X4 wvp, w; XMFLOAT4 sun, diff, amb, spec, emis, flags; };

void D3D11Client::clbkRenderScene()
{
    if (!dev || !sc || !shadersOk) return;     // a shader that failed to compile is in Orbiter.log; nothing is drawn
    QueryPerformanceCounter(&tSceneBeg);
    frameNo++;
    if (testMode) diagFrames = (frameNo >= 300 && frameNo < 302) ? 1 : 0;
    if (testMode && frameNo == 600) {      // D3D11CLIENT_TEST_HANG: one frame held 6 s (the hang watchdog's own check)
        static const bool hang = getenv("D3D11CLIENT_TEST_HANG") != nullptr;
        if (hang) { LOGC("test: holding frame %u for 6 s", frameNo); Sleep(6000); }
        static const bool crash = getenv("D3D11CLIENT_TEST_CRASH") != nullptr;   // ... and a crash (the crash report's own check)
        if (crash) { LOGC("test: crashing at frame %u", frameNo); *(volatile int*)(UINT_PTR)0x10 = 1; }
    }
    if (testMode) { TestScript(); void FxTestFrame(); FxTestFrame(); }   // FxTest.cpp: effects without a player
    if (g_traceFrames > 0) {     // the memory frame by frame after a scripted action, when it moved by 2 MB or more
        static size_t last = 0;
        PROCESS_MEMORY_COUNTERS_EX pm = {}; pm.cb = sizeof pm; GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof pm);
        if (pm.PrivateUsage > last + (2u << 20) || pm.PrivateUsage + (2u << 20) < last) { char b[48]; sprintf_s(b, "frame %u", frameNo); LogMemory(b); last = pm.PrivateUsage; }
        g_traceFrames--;
    }
    FlushDirty();         // surfaces drawn on since the last frame (MFDs, displays) to their textures
    BeginScreenLayer();   // the 2D layer the HUD and the info boxes draw into after this call
    if ((frameNo & 31) == 0) ScanVessels();
    UINT fi = frameNo % 4;
    ctx->Begin(qDis[fi].Get());
    ctx->End(qTs[fi][0].Get());
    GpuMark(fi, 0);

    VECTOR3 cp; oapiCameraGlobalPos(&cp);
    MATRIX3 cr; oapiCameraRotationMatrix(&cr);
    {   // test runs (Client.cfg TestLookSun = 1): the view turned onto the Sun, the local vertical up (Sun.cpp checks in air)
        double FxTestNum(const char* key);
        static const bool look = FxTestNum("TestLookSun") > 0.5;
        if (look) {
            VECTOR3 sp = _V(0, 0, 0);
            for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) { oapiGetGlobalPos(h, &sp); break; } }
            VECTOR3 f = sp - cp; f = f / length(f); VECTOR3 up = _V(0, 1, 0);
            if (OBJHANDLE hp = oapiCameraProxyGbody()) { VECTOR3 pp; oapiGetGlobalPos(hp, &pp); up = cp - pp; up = up / length(up); }
            VECTOR3 r = crossp(up, f);
            if (length(r) > 1e-6) {
                r = r / length(r); up = crossp(f, r);
                cr = _M(r.x, up.x, f.x, r.y, up.y, f.y, r.z, up.z, f.z);
            }
        }
    }
    double ap = oapiCameraAperture();             // half the vertical field of view
    float tanY = (float)tan(ap);
    // the near plane: 10 cm; from inside a vessel (a cockpit, a helmet: its liner, rim and chin bar 5-15 cm from the eye)
    // 1.5 cm. Reverse Z with an infinite far plane stores n / z in a float: a smaller n costs no depth precision, and
    // every pass reads n back from the projection (P._43) or its inverse (2026-10-20, the helmet view)
    float n = oapiCameraInternal() ? 0.015f : 0.1f;
    XMMATRIX proj(1.0f / (tanY * W / H), 0, 0, 0,
                  0, 1.0f / tanY, 0, 0,
                  0, 0, 0, 1,
                  0, 0, n, 0);                    // reverse Z, infinite far: depth = n / z
    XMMATRIX projNoJit = proj;
    { XMMATRIX Vn(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1); XMStoreFloat4x4(&curVPNoJit, Vn * projNoJit); }
    { XMFLOAT2 j = Jitter(); proj.r[2] = XMVectorSet(j.x, j.y, 0, 1); }   // TAA: the sub-pixel offset of this frame

    FxFrame(this, cp, cr, proj, oapiGetSimStep());   // effects' compute work (waves, spray) before the scene
    GpuMark(fi, 1);
    RenderShadows(cp);
    GpuEx(fi, 0);
    LightsFrame(cp);      // the vessels' local lights: gathered, clustered, the spots' shadows (Lights.cpp)
    GpuMark(fi, 2);
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
    LightsBind();         // b9, t17..t20 for the scene's shaders
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
    GpuMark(fi, 3);

    // the depth prepass (as Frostbite, id Tech, Unreal's early Z pass): the opaque vessels' depth first, no pixel shader;
    // their colour pass and the terrain then light only what is seen. Before the planets unless a liquid needs the
    // terrain's depth alone (SaveLiquidBed) - indoors the ground under the floor is not lit at all then.
    // Client.cfg NoDepthPrepass = 1: off (a comparison)
    OBJHANDLE own = oapiCameraInternal() ? oapiGetFocusObject() : nullptr;
    auto depthPrepass = [&]() {
        GpuEx(fi, 1);
        double FxTestNum(const char* key);
        static const bool off = FxTestNum("NoDepthPrepass") > 0.5;
        if (!off) {
            ctx->OMSetRenderTargets(0, nullptr, dsv.Get());
            prepass = true;
            for (auto& kv : visuals) if (kv.first != own) RenderVessel(kv.second, cp, cr, proj, false);
            prepass = false;
            ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
        }
        GpuEx(fi, 2);
    };
    bool LiquidBedWanted();
    const bool preEarly = !LiquidBedWanted();
    if (preEarly) depthPrepass();
    // the opaque terrain and vessels also write the sky's share of their light (target 2) for the AO after them (Ssao.cpp)
    ID3D11RenderTargetView* aoRT = AOShareTarget();
    if (aoRT) { const float z[4] = {}; ctx->ClearRenderTargetView(aoRT, z); }
    auto bindScene = [&](bool mv) {
        ID3D11RenderTargetView* rt[3] = { hdrRTV.Get(), mv ? mvRTV.Get() : nullptr, aoRT };
        ctx->OMSetRenderTargets(aoRT ? 3 : mv && mvRTV ? 2 : 1, rt, dsv.Get());
    };
    bindScene(false);

    // planets, the farthest first (one reverse-Z depth range holds them all; the order only helps early depth rejection)
    std::vector<std::pair<double, Planet*>> order;
    for (Planet* p : planets) { VECTOR3 pp; oapiGetGlobalPos(p->hObj, &pp); order.push_back({ -length(pp - cp), p }); }
    std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::reverse(order.begin(), order.end());
    int tiles = 0;
    for (auto& o : order) { o.second->Render(ctx.Get(), cp, cr, proj, tanY, (int)H); tiles += o.second->tilesDrawn; }
    for (Planet* p : planets) p->Purge(frameNo);
    SaveLiquidBed();     // the bed under a liquid, before the vessels go into the depth (waves fade on the shore, not at a leg)
    if (!preEarly) depthPrepass();
    ctx->End(qTs[fi][1].Get());
    GpuMark(fi, 4);

    // vessels: opaque groups, then the transparent ones
    if (mvRTV) {         // the opaque vessels also write their motion vectors
        float none[4] = { 1000, 1000, 0, 0 };
        ctx->ClearRenderTargetView(mvRTV.Get(), none);
    }
    bindScene(true);
    // the camera's own vessel seen from inside (a cockpit, a helmet with its visor layers) comes last, after the air and
    // the effects: its near meshes must not end the other vessels' or the sky's rays (as D3D9 draws the cockpit)
    for (auto& kv : visuals) if (kv.first != own) RenderVessel(kv.second, cp, cr, proj, false);
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
    GpuSub(fi, 0);
    RenderAO(proj);      // the folds and the contacts darken (before the liquid and the air: those are not)
    GpuMark(fi, 5);
    // the liquid over the terrain and the opaque vessels: what stands in it is seen through its surface (refracted along
    // the bent ray, dimmed by the column, under the waves' Fresnel and mirror); what stands out of it hides it by depth
    RenderLiquid(cp, cr, proj, tanY);
    GpuMark(fi, 6);
    RenderSun(cp, cr, proj);                      // the Sun on the sky's depth, before the air dims it (Sun.cpp)
    GpuSub(fi, 1);
    for (auto& kv : visuals) if (kv.first != own) RenderVessel(kv.second, cp, cr, proj, true);
    RenderVisorDropsOutside(cp, cr, proj);        // rain drops on the suits' glass, before the air (VisorDrops.cpp)
    GpuSub(fi, 2);
    RenderStormShadow(cp, cr, proj);  GpuSub(fi, 3);              // Earth's cyclone: its shadow on what is drawn, before the air (Storm.cpp)
    FxRenderTransparent(this, cp, cr, proj);      // the air (sky, aerial perspective) from the depth
    GpuSub(fi, 4);
    RenderStorm(cp, cr, proj);                    // Earth's cyclone: the volumetric clouds, the air in front of them (Storm.cpp)
    { void StormFrameFallback(); StormFrameFallback(); }   // its turbulence, if Orbiter does not call the client's clbkPreStep
    GpuSub(fi, 5);
    RenderClouds(cp, cr, proj);                   // cloud layers over it, the air in front of them included (Clouds.cpp)
    GpuSub(fi, 6);
    LightsRenderAir(cp, cr, proj);                // the headlights' beams in the air, the lenses, the beacons (Lights.cpp)
    GpuMark(fi, 7);
    ExhaustRender(this, cp, cr, proj, oapiGetSimStep());   // glow, shimmer, plumes, GPU particles
    ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
    RenderAr(cp, cr, proj);                       // the helmet display's markers and paths, against the scene's depth
    UpdateRenderParticles(cp, cr, proj, oapiGetSimStep());
    RenderRain(cp, cr, proj);                     // rain (Titan; Fx.cfg Rain = mm/h)
    RenderStormRain(cp, cr, proj);                // rain under Earth's cyclone (StormRain.cpp)
    RenderUnderwater(cp, cr, proj);               // the camera inside a liquid: its column over everything
    RenderVisorDropsInside(cp, cr, proj);         // drops on the own visor's outer face, under its tint, plates and rim (VisorDrops.cpp)
    GpuMark(fi, 8);
    if (own && visuals.count(own)) {
        ctx->OMSetRenderTargets(1, hdrRTV.GetAddressOf(), dsv.Get());
        D3D11_VIEWPORT ovp = { 0, 0, (float)RW, (float)RH, 0, 1 };
        ctx->RSSetViewports(1, &ovp);
        ID3D11SamplerState* oss[3] = { sampAniso.Get(), sampClamp.Get(), sampShadow.Get() };
        ctx->PSSetSamplers(0, 3, oss);
        ctx->PSSetShaderResources(7, 1, shadowSRV.GetAddressOf());
        ctx->VSSetConstantBuffers(0, 1, cbFrame.GetAddressOf()); ctx->PSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
        LightsBind();
        ownPass = true;
        RenderVessel(visuals[own], cp, cr, proj, false);
        RenderVessel(visuals[own], cp, cr, proj, true);
        ownPass = false;
    }
    RenderSunGlare();        // the Sun's glare in the eye over everything, the own cockpit too - hidden by it, none (Sun.cpp)
    ctx->End(qTs[fi][2].Get());
    GpuMark(fi, 9);

    // the single-sampled image (resolve / TAA), tone mapping (+ FXAA) into the back buffer
    {
        XMMATRIX Vt(cr.m11, cr.m12, cr.m13, 0, cr.m21, cr.m22, cr.m23, 0, cr.m31, cr.m32, cr.m33, 0, 0, 0, 0, 1);
        ID3D11ShaderResourceView* img = FinishScene(cp, Vt * proj, Vt * projNoJit);
        LightsProbe(img);     // what the view shows and what the local lights light there: the eye's adaptation (Lights.cpp)
        ToneMapAndAA(img);
    }
    GpuMark(fi, 10);
    // 2D panels (drawn now on the GPU); the HUD and the MFDs come through the Sketchpad after this call returns
    Render2DOverlay();
    ctx->End(qTs[fi][3].Get());
    GpuMark(fi, 11);
    ctx->End(qDis[fi].Get());

    if (frameNo >= 4) {
        UINT pf = (frameNo - 3) % 4;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
        if (ctx->GetData(qDis[pf].Get(), &dj, sizeof dj, 0) == S_OK && !dj.Disjoint) {
            UINT64 t[4]; bool ok = true;
            for (int i = 0; i < 4; i++) ok = ok && ctx->GetData(qTs[pf][i].Get(), &t[i], 8, 0) == S_OK;
            if (ok) for (int i = 0; i < 3; i++) passMs[i] = 0.9 * passMs[i] + 0.1 * (double)(t[i + 1] - t[i]) * 1000.0 / dj.Frequency;
            UINT64 m[kMarks]; bool okm = true;
            for (int i = 0; i < kMarks; i++) okm = okm && ctx->GetData(qMk[pf][i].Get(), &m[i], 8, 0) == S_OK;
            if (okm) for (int i = 1; i < kMarks; i++) markMs[i] = 0.9 * markMs[i] + 0.1 * (double)(m[i] - m[i - 1]) * 1000.0 / dj.Frequency;
            UINT64 s[kSub]; bool oks = okm;
            for (int i = 0; i < kSub; i++) oks = oks && ctx->GetData(qSub[pf][i].Get(), &s[i], 8, 0) == S_OK;
            if (oks) {      // the AO; the Sun, transparent vessels, storm shadow, air, storm, clouds, lights in the air
                const UINT64 e[kSub + 1] = { m[5], s[1], s[2], s[3], s[4], s[5], s[6], m[7] };
                const UINT64 b[kSub + 1] = { s[0], m[6], s[1], s[2], s[3], s[4], s[5], s[6] };
                for (int i = 0; i <= kSub; i++) subMs[i] = 0.9 * subMs[i] + 0.1 * (double)(e[i] - b[i]) * 1000.0 / dj.Frequency;
            }
            UINT64 x[kEx]; bool okx = okm;
            for (int i = 0; i < kEx; i++) okx = okx && ctx->GetData(qEx[pf][i].Get(), &x[i], 8, 0) == S_OK;
            if (okx) {      // the Sun's shadows | the local lights (mark 2); the vessels' depth prepass
                const double k = 1000.0 / dj.Frequency;
                exMs[0] = 0.9 * exMs[0] + 0.1 * (double)(x[0] - m[1]) * k;
                exLightsMs = 0.9 * exLightsMs + 0.1 * (double)(m[2] - x[0]) * k;
                exMs[1] = 0.9 * exMs[1] + 0.1 * (double)(x[2] - x[1]) * k;
            }
        }
    }
    // self-check: a line in Orbiter.log every 10 s, a screenshot 15 s after the start
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (g_cfg.statsLog && (now.QuadPart - tLog.QuadPart) / (double)qpf.QuadPart > 10.0) {
        tLog = now;
        size_t q; { std::lock_guard<std::mutex> lk(loadMx); q = loadQ.size(); }
        LOGC("fps %.0f | GPU ms: planets %.2f, vessels %.2f, post %.2f | tiles drawn %d, load queue %zu | vessels %zu",
             oapiGetFrameRate(), passMs[0], passMs[1], passMs[2], tiles, q, visuals.size());
        LOGC("GPU ms by pass: fx compute %.2f, shadows %.2f, sky %.2f, planets %.2f, vessels opaque %.2f, liquid %.2f, "
             "transparent + air %.2f, exhaust/AR/particles %.2f, own vessel %.2f, resolve + post %.2f, 2D %.2f",
             markMs[1], markMs[2], markMs[3], markMs[4], markMs[5], markMs[6], markMs[7], markMs[8], markMs[9], markMs[10], markMs[11]);
        LOGC("GPU ms within: AO %.2f | the Sun %.2f, transparent vessels %.2f, storm shadow %.2f, air %.2f, storm %.2f, clouds %.2f, lights in the air %.2f",
             subMs[0], subMs[1], subMs[2], subMs[3], subMs[4], subMs[5], subMs[6], subMs[7]);
        LOGC("GPU ms split: the Sun's shadows %.2f, local lights (their shadows, sky maps) %.2f | vessels' depth prepass %.2f",
             exMs[0], exLightsMs, exMs[1]);
        // the CPU's side of the same passes (the time to issue them) and round the frame; the frame's own sum is the
        // Present to Present time: a long "present" - the GPU's frame, a long "outside" or passes - the CPU's
        LOGC("CPU ms: frame %.2f | outside the client (Orbiter, modules) %.2f, surfaces + 2D layer %.2f | fx compute %.2f, "
             "Sun shadows %.2f, local lights %.2f, sky %.2f, planets %.2f, vessels opaque %.2f (prepass %.2f), liquid %.2f, "
             "transparent + air %.2f, exhaust/AR/particles %.2f, own vessel %.2f, resolve + post %.2f, 2D panels %.2f | "
             "HUD (Sketchpad) %.2f, HUD procs + layer %.2f, present %.2f",
             cpuFrameMs, cpuOutMs, cpuPreMs, cpuMs[1], cpuExMs[0], cpuExLightsMs, cpuMs[3], cpuMs[4], cpuMs[5], cpuExMs[1],
             cpuMs[6], cpuMs[7], cpuMs[8], cpuMs[9], cpuMs[10], cpuMs[11], cpuHudMs, cpuDispMs, cpuPresentMs);
        std::string ls = Planet::LoadStats();
        if (!ls.empty()) LOGC("%s", ls.c_str());
        LogMemory("now");     // a leak shows as a steady climb from one of these lines to the next
        { void HeapTrackReport(); HeapTrackReport(); }
        static int nNow = 0;
        ++nNow; { double FxTestNum(const char*); const int mc = (int)FxTestNum("MemCensus"); if (mc > 0 && (nNow == 2 || nNow == mc)) { void LogMemoryCensus(); LogMemoryCensus(); } }   // MemCensus = N: also at the N-th tick (after a test script's actions)   // MemCensus.cpp: who holds the address space (a ~1.5 s hitch: test runs only, -Set "MemCensus = 1")
    }
    QueryPerformanceCounter(&tSceneEnd);
}

bool D3D11Client::clbkDisplayFrame()
{
    if (!sc) return false;
    QueryPerformanceCounter(&tDispBeg);
    if (testMode && IsWindowVisible(hWnd)) ShowWindow(hWnd, SW_HIDE);
    CallRenderProcs();    // the modules' HUD procs (gcCore RegisterRenderProc)
    CompositeScreenLayer();
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    static double shotT = -1;      // test runs: D3D11CLIENT_TEST_SHOT = seconds to the shot (15 by default)
    if (shotT < 0) {
        char v[16] = {}; double FxTestShotTime(); const double cfgT = FxTestShotTime();
        shotT = GetEnvironmentVariableA("D3D11CLIENT_TEST_SHOT", v, sizeof v) ? std::max(atof(v), 1.0) : cfgT > 0 ? std::max(cfgT, 1.0) : 15.0;
    }
    if (!shotDone && (now.QuadPart - tStart.QuadPart) / (double)qpf.QuadPart > shotT) {
        shotDone = true;
        CreateDirectoryA("Modules\\D3D11Client\\shots", nullptr);
        SaveShot("Modules\\D3D11Client\\shots\\start.png");
        if (testMode) {     // this run's own copy (tests of several sessions overlap; tools\run_test.ps1 takes this one)
            char own[96]; sprintf_s(own, "Modules\\D3D11Client\\shots\\start_%lu.png", GetCurrentProcessId());
            SaveShot(own);
        }
    }
    // test runs: D3D11CLIENT_TEST_BURST = n frames shot one after another from the shot's time on (burst_<pid>_<k>.png):
    // what changes from frame to frame (shimmer, flicker) measured on still pictures
    static int burst = -1, burstK = 0;
    if (burst < 0) { char v[8] = {}; burst = testMode && GetEnvironmentVariableA("D3D11CLIENT_TEST_BURST", v, sizeof v) ? std::clamp(atoi(v), 0, 32) : 0; }
    if (shotDone && burstK < burst) {
        char fn[128]; sprintf_s(fn, "Modules\\D3D11Client\\shots\\burst_%lu_%02d.png", GetCurrentProcessId(), burstK++);
        SaveShot(fn);
    }
    LARGE_INTEGER tPres; QueryPerformanceCounter(&tPres);
    sc->Present(testMode || GetVideoData()->novsync ? 0 : 1, 0);   // uncapped when vsync is off (and in test runs)
    {   // the CPU's frame (the stats line "CPU ms"): the marks of clbkRenderScene, the time round it
        LARGE_INTEGER pe; QueryPerformanceCounter(&pe);
        const double k = 1000.0 / (double)qpf.QuadPart;
        auto sm = [&](double& v, const LARGE_INTEGER& a, const LARGE_INTEGER& b) { v = 0.9 * v + 0.1 * (double)(b.QuadPart - a.QuadPart) * k; };
        if (tPresentEnd.QuadPart && tSceneBeg.QuadPart > tPresentEnd.QuadPart && tSceneEnd.QuadPart > tSceneBeg.QuadPart) {
            sm(cpuOutMs, tPresentEnd, tSceneBeg); sm(cpuPreMs, tSceneBeg, cpuMk[0]);
            for (int i = 1; i < kMarks; i++) sm(cpuMs[i], cpuMk[i - 1], cpuMk[i]);
            sm(cpuExMs[0], cpuMk[1], cpuEx[0]); sm(cpuExLightsMs, cpuEx[0], cpuMk[2]); sm(cpuExMs[1], cpuEx[1], cpuEx[2]);
            sm(cpuHudMs, tSceneEnd, tDispBeg); sm(cpuDispMs, tDispBeg, tPres); sm(cpuPresentMs, tPres, pe); sm(cpuFrameMs, tPresentEnd, pe);
        }
        tPresentEnd = pe;
    }
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
struct FrameCB { XMFLOAT4X4 invVP; XMFLOAT4 sunDir, sky0, sky1, amb, sun, up, planet, hazeRGB; XMFLOAT4X4 lightVP; XMFLOAT4 shadow, adapt, liq0, liq1, liq2; XMFLOAT4X4 lightVP2; XMFLOAT4X4 casc[4]; XMFLOAT4 cascFar, cascTex, cascRange; };

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
    f.lightVP = lightVP; f.lightVP2 = lightVP2;
    for (int i = 0; i < 4; i++) f.casc[i] = casc[i];
    f.cascFar = { cascFar[0], cascFar[1], cascFar[2], cascFar[3] }; f.cascTex = { cascTex[0], cascTex[1], cascTex[2], cascTex[3] };
    f.cascRange = { cascRange[0], cascRange[1], cascRange[2], cascRange[3] };
    static float shDbg = -1;      // test runs: D3D11CLIENT_SHADOWDBG=1 draws the Sun's visibility alone (planet.hlsl, mesh.hlsl)
    if (shDbg < 0) { char v[8] = {}; shDbg = GetEnvironmentVariableA("D3D11CLIENT_SHADOWDBG", v, sizeof v) ? (float)atof(v) : 0.0f; }
    f.shadow = { shadowOn ? 1.0f : 0.0f, 1.0f / SHADOW_N, shDbg, sunDiam };     // w: the Sun's angular diameter (the penumbra)
    // the eye's adaptation: the colour balance per channel and the brightness factor (1 without the FX atmosphere)
    if (g_fxAtmo.active) f.adapt = { g_fxAtmo.white[0], g_fxAtmo.white[1], g_fxAtmo.white[2], g_fxAtmo.exposure };
    else f.adapt = { 1, 1, 1, 1 };
    LightsAdaptEye(f.adapt, f.hazeRGB.w);     // ... and to the headlights, the cabin's lamps where they light the view (Lights.cpp)
    // the liquid the camera is over (as RenderLiquid picks it): the vessels' parts below its level are seen through it
    for (Planet* p : planets) {
        if (!p->liq.has || !g_cfg.liquids) continue;
        VECTOR3 q; oapiGetGlobalPos(p->hObj, &q);
        const double dc = length(cp - q), Rl = LiquidLevelFor(p->R + p->liq.level, dc), alt = dc - Rl;
        if (alt < 0 || alt > 300000) continue;
        const VECTOR3 c = q - cp; const LiquidCfg& L = p->liq;
        f.liq0 = { (float)c.x, (float)c.y, (float)c.z, (float)Rl };
        f.liq1 = { (float)L.colour.x, (float)L.colour.y, (float)L.colour.z, (float)L.ext };
        f.liq2 = { (float)L.wts.x, (float)L.wts.y, (float)L.wts.z, 1 };
        break;
    }
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(cbFrame.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &f, sizeof f); ctx->Unmap(cbFrame.Get(), 0);
    ctx->VSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
    ctx->PSSetConstantBuffers(0, 1, cbFrame.GetAddressOf());
}

// ------------------------------------------------------------------------------------------ shadows
// Cascaded shadow maps, after the DirectX SDK's CascadedShadowMaps11 and "Common Techniques to Improve Shadow Depth Maps":
// four cascades over the view's depth (0.3..300 m, practical split scheme), each a 2048 tile of one 4096 atlas. A cascade
// is fitted to its slice of the view frustum by a bounding sphere (its size does not change as the camera turns) and its
// centre is moved in whole texels of a light frame fixed to the world (the shadows do not crawl as the camera moves).
// The casters are the vessels; the bias is slope-scaled in the rasteriser and a normal offset of a texel or so in the
// shader (common.hlsli ShadowAtN) - no acne on surfaces turned from the light, no gap at the feet.
void D3D11Client::RenderShadows(const VECTOR3& cp)
{
    shadowOn = false;
    ID3D11ShaderResourceView* nul = nullptr;
    ctx->PSSetShaderResources(7, 1, &nul);
    if (!g_cfg.shadows || visuals.empty()) return;
    VECTOR3 sp = _V(0, 0, 0);
    for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) { oapiGetGlobalPos(h, &sp); break; } }
    VECTOR3 L = sp - cp;
    {   // the Sun's angular diameter here (the penumbra's width, common.hlsli)
        OBJHANDLE hs = nullptr; for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) { hs = h; break; } }
        sunDiam = hs ? (float)(2 * oapiGetSize(hs) / std::max(length(L), 1.0)) : 0.0093f;
    }
    L = L / length(L);
    // the light's frame, fixed to the nearest planet's ground - its rotation carries the terrain and what stands or
    // flies over it (a grid fixed in space would crawl over them every frame: the shadows' edges shimmer whatever the
    // snapping). u, v across the light, L along it, in the planet's axes; the light's axis held while the Sun moves
    // less than half a degree over that ground (then taken anew: one step, not a crawl)
    MATRIX3 Rp = _M(1, 0, 0, 0, 1, 0, 0, 0, 1); VECTOR3 pp = _V(0, 0, 0);
    OBJHANDLE axisPlanet = nullptr;     // the ground the grid is fixed to (null: the focus vessel's frame)
    {
        OBJHANDLE hp = nullptr; double best = 1e300;
        for (DWORD i = 0; i < oapiGetGbodyCount(); i++) {
            OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) != OBJTP_PLANET) continue;
            VECTOR3 q; oapiGetGlobalPos(h, &q); const double alt = length(cp - q) - oapiGetSize(h);
            if (alt < best) { best = alt; hp = h; pp = q; }
        }
        if (hp && best < 20e3) oapiGetRotationMatrix(hp, &Rp);
        else {      // in flight or in orbit: the grid goes with the focus vessel (its own shadows stay put on it)
            hp = nullptr;
            OBJHANDLE hf = oapiGetFocusObject();
            if (hf) oapiGetGlobalPos(hf, &pp); else pp = cp;
        }
        static OBJHANDLE axP = nullptr; static VECTOR3 axL = _V(0, 0, 0);
        const VECTOR3 Lp = tmul(Rp, L);
        if (axP != hp || dotp(Lp, axL) < cos(0.5 * RAD)) { axP = hp; axL = Lp; }
        L = mul(Rp, axL);
        axisPlanet = hp;
    }
    const VECTOR3 Lp = tmul(Rp, L);
    const VECTOR3 ap = fabs(Lp.y) < 0.9 ? _V(0, 1, 0) : _V(1, 0, 0);
    VECTOR3 up_ = crossp(ap, Lp); up_ = up_ / length(up_);
    const VECTOR3 vp_ = crossp(Lp, up_);
    const VECTOR3 u = mul(Rp, up_), v = mul(Rp, vp_);
    // a map's centre (camera-relative) moved to whole texels of that frame (double: the positions are ~1e11 m)
    // ... and along the light by whole metres (a constant offset of the depths, nothing else: the tile's key below stays
    // the same while the camera moves within a texel, so the static casters' cache holds)
    long long sIu = 0, sIv = 0, sIl = 0;
    auto snap = [&](const VECTOR3& c, double texel) {
        const VECTOR3 q = tmul(Rp, (cp - pp) + c);
        const double cu = dotp(q, up_), cv = dotp(q, vp_), cl = dotp(q, Lp);
        sIu = (long long)floor(cu / texel); sIv = (long long)floor(cv / texel); sIl = (long long)floor(cl);
        return c + mul(Rp, up_ * (sIu * texel - cu) + vp_ * (sIv * texel - cv) + Lp * ((double)sIl - cl));
    };
    // the casters of a tile: what stands still into the static atlas (when the tile's key changed), copied into the atlas,
    // the people and what moves over it. A person (an OrbiterCrew vessel: skinned, animated every frame) is always drawn;
    // anything else is "still" by its signature - its place and attitude in the planet's frame, its animations' states -
    // a vehicle driving off changes it every frame and is then simply drawn every frame, as before
    const bool cacheOk = shadowStatDSV && shadowStatSRV && vsShTile && psShCopy && psShClear && dsShAlways;
    auto fnv = [](uint64_t& h, long long v) { for (int i = 0; i < 8; i++) { h ^= (uint64_t)((v >> (8 * i)) & 0xFF); h *= 1099511628211ull; } };
    auto tilePass = [&](ID3D11PixelShader* ps, ID3D11ShaderResourceView* srv) {
        ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vsShTile.Get(), nullptr, 0); ctx->PSSetShader(ps, nullptr, 0);
        ctx->HSSetShader(nullptr, nullptr, 0); ctx->DSSetShader(nullptr, nullptr, 0); ctx->GSSetShader(nullptr, nullptr, 0);
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->OMSetDepthStencilState(dsShAlways.Get(), 0); ctx->RSSetState(rsShadow.Get());
        ctx->Draw(3, 0);
        ID3D11ShaderResourceView* none = nullptr; ctx->PSSetShaderResources(0, 1, &none);
    };
    auto drawTile = [&](int ti, const VECTOR3& c, double r, double backD, const XMMATRIX& LVP, const D3D11_VIEWPORT& vp) {
        std::vector<VesselVis*> stat, dyn;
        uint64_t sig = 1469598103934665603ull;
        fnv(sig, (long long)(intptr_t)axisPlanet); fnv(sig, llround(Lp.x * 1e6)); fnv(sig, llround(Lp.y * 1e6)); fnv(sig, llround(Lp.z * 1e6));
        for (auto& kv : visuals) {
            VECTOR3 q; oapiGetGlobalPos(kv.first, &q);
            VECTOR3 d = (q - cp) - c;
            const double along = dotp(d, L);
            VECTOR3 across = d - L * along;
            const double sz = oapiGetSize(kv.first);
            if (length(across) > r + sz || along > backD + sz || along < -(r + sz)) continue;
            VESSEL* v = kv.second->v;
            const char* cn = v ? v->GetClassNameA() : nullptr;
            if (!cacheOk || !v || (cn && strstr(cn, "OrbiterCrew"))) { dyn.push_back(kv.second); continue; }
            stat.push_back(kv.second);
            const VECTOR3 qL = tmul(Rp, q - pp); MATRIX3 Rv; v->GetRotationMatrix(Rv);
            const VECTOR3 fx = tmul(Rp, _V(Rv.m11, Rv.m21, Rv.m31)), fz = tmul(Rp, _V(Rv.m13, Rv.m23, Rv.m33));
            fnv(sig, (long long)(intptr_t)kv.first);
            fnv(sig, llround(qL.x * 1000)); fnv(sig, llround(qL.y * 1000)); fnv(sig, llround(qL.z * 1000));
            fnv(sig, llround(fx.x * 1e5)); fnv(sig, llround(fx.y * 1e5)); fnv(sig, llround(fx.z * 1e5));
            fnv(sig, llround(fz.x * 1e5)); fnv(sig, llround(fz.y * 1e5)); fnv(sig, llround(fz.z * 1e5));
            ANIMATION* an = nullptr; const UINT na = v->GetAnimPtr(&an);
            for (UINT k = 0; k < na && an; k++) fnv(sig, llround(an[k].state * 1e5));
        }
        if (cacheOk) {
            ShadowTileKey& k = shKey[ti];
            const bool same = k.valid && k.iu == sIu && k.iv == sIv && k.iL == sIl && k.r == r && k.sig == sig;
            if (!same) {        // the static casters drawn anew into their atlas
                ctx->OMSetRenderTargets(0, nullptr, shadowStatDSV.Get());
                ctx->RSSetViewports(1, &vp);
                tilePass(psShClear.Get(), nullptr);
                for (VesselVis* vv : stat) RenderVessel(vv, cp, _M(1, 0, 0, 0, 1, 0, 0, 0, 1), XMMatrixIdentity(), false, &LVP);
                k.valid = true; k.iu = sIu; k.iv = sIv; k.iL = sIl; k.r = r; k.sig = sig;
                shStatDrawn++;
            } else shStatKept++;
            ctx->OMSetRenderTargets(0, nullptr, shadowDSV.Get());
            ctx->RSSetViewports(1, &vp);
            tilePass(psShCopy.Get(), shadowStatSRV.Get());
        } else {
            ctx->OMSetRenderTargets(0, nullptr, shadowDSV.Get());
            ctx->RSSetViewports(1, &vp);
        }
        for (VesselVis* vv : dyn) RenderVessel(vv, cp, _M(1, 0, 0, 0, 1, 0, 0, 0, 1), XMMatrixIdentity(), false, &LVP);
    };
    MATRIX3 cr; oapiCameraRotationMatrix(&cr);
    const double tanY = tan(oapiCameraAperture()), tanX = tanY * W / (double)H;
    const double nearD = 0.3, farD = 300.0, lambda = 0.85;
    double split[5]; split[0] = 0; split[1] = nearD;      // [0]: the focus map (below); 1..3: the view's slices
    for (int i = 2; i <= 4; i++) {
        const double f = (i - 1) / 3.0;
        split[i] = lambda * nearD * pow(farD / nearD, f) + (1 - lambda) * (nearD + (farD - nearD) * f);
    }
    const double back = 300.0;          // casters up to this far towards the Sun from a cascade's centre
    ctx->ClearDepthStencilView(shadowDSV.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ctx->OMSetRenderTargets(0, nullptr, shadowDSV.Get());
    // tile 0, as D3D9Client draws its shadows: one map fitted to the focus vessel's bounding sphere (a person: ~2 mm a
    // texel), its depth range just round the casters - the finest self-shadowing where the eye looks
    {
        OBJHANDLE hf = oapiGetFocusObject();
        if (hf && visuals.count(hf)) {
            VECTOR3 fp; oapiGetGlobalPos(hf, &fp);
            const double r = std::max(oapiGetSize(hf) * 1.02, 0.5);
            const double texel = 2 * r / SHADOW_N;
            VECTOR3 c = snap(fp - cp, texel);       // (the grid fixed to the ground: no shimmer as the focus moves)
            XMVECTOR Lv = XMVectorSet((float)L.x, (float)L.y, (float)L.z, 0);
            XMVECTOR uvv = XMVectorSet((float)v.x, (float)v.y, (float)v.z, 0);
            const double back0 = r + 30.0;     // the casters: the focus and what stands within 30 m towards the Sun
            XMVECTOR eye = XMVectorSet((float)c.x, (float)c.y, (float)c.z, 1) + Lv * (float)back0;
            // the depth range reaches 60 m past the focus: the ground its shadow falls on (a low Sun: metres behind it) is in
            const double far0 = back0 + r + 60.0;
            XMMATRIX LVP = XMMatrixLookToLH(eye, -Lv, uvv) * XMMatrixOrthographicLH((float)(2 * r), (float)(2 * r), 0.0f, (float)far0);
            XMStoreFloat4x4(&casc[0], XMMatrixTranspose(LVP));
            cascFar[0] = -1; cascTex[0] = (float)texel; cascRange[0] = (float)(back0 + r + 60.0);   // tile 0 by the box, not by the distance
            D3D11_VIEWPORT vp = { 0, 0, (float)SHADOW_N, (float)SHADOW_N, 0, 1 };
            drawTile(0, c, r, back0, LVP, vp);
        } else { cascFar[0] = -2; cascTex[0] = 0; }
    }
    for (int ci = 1; ci < 4; ci++) {
        // the slice's bounding sphere (camera-centred): centre on the view axis, radius to the far corners
        const double z0 = split[ci], z1 = split[ci + 1];
        const double k2 = tanX * tanX + tanY * tanY;
        // the centre on the axis that minimises the radius to the near and far corner rings
        double zc = 0.5 * (z0 + z1) + 0.5 * (z1 * z1 - z0 * z0) * k2 / (z1 - z0) * 0.5;
        zc = std::clamp(zc, z0, z1);
        double r = std::max(sqrt((z1 - zc) * (z1 - zc) + z1 * z1 * k2), sqrt((zc - z0) * (zc - z0) + z0 * z0 * k2));
        r = ceil(r * 16.0) / 16.0;      // quantised: the tile's scale stays the same frame to frame
        VECTOR3 fwd = _V(cr.m13, cr.m23, cr.m33);
        const double texel = 2 * r / SHADOW_N;
        VECTOR3 c = snap(fwd * zc, texel);
        XMVECTOR Lv = XMVectorSet((float)L.x, (float)L.y, (float)L.z, 0);
        XMVECTOR uv = XMVectorSet((float)v.x, (float)v.y, (float)v.z, 0);
        XMVECTOR eye = XMVectorSet((float)c.x, (float)c.y, (float)c.z, 1) + Lv * (float)back;
        XMMATRIX LV = XMMatrixLookToLH(eye, -Lv, uv);
        XMMATRIX LP = XMMatrixOrthographicLH((float)(2 * r), (float)(2 * r), 0.0f, (float)(back + r));
        XMMATRIX LVP = LV * LP;
        XMStoreFloat4x4(&casc[ci], XMMatrixTranspose(LVP));
        cascFar[ci] = (float)z1; cascTex[ci] = (float)texel; cascRange[ci] = (float)(back + r);
        D3D11_VIEWPORT vp = { (float)((ci & 1) * SHADOW_N), (float)((ci >> 1) * SHADOW_N), (float)SHADOW_N, (float)SHADOW_N, 0, 1 };
        drawTile(ci, c, r, back, LVP, vp);
    }
    lightVP = casc[0]; lightVP2 = casc[1];
    shadowOn = true;
    static ULONGLONG shLog = 0;
    if (GetTickCount64() - shLog > 10000) {
        shLog = GetTickCount64();
        LOGC("sun shadows: static casters' tiles drawn %d, kept %d (last 10 s; %s)", shStatDrawn, shStatKept, cacheOk ? "cache on" : "no cache");
        shStatDrawn = shStatKept = 0;
    }
}

// the process's memory (a 32-bit process: the address space runs out before the RAM): committed private bytes and the
// largest free block of addresses (what a big allocation needs)
void LogMemory(const char* when)
{
    PROCESS_MEMORY_COUNTERS_EX pm = {}; pm.cb = sizeof pm;
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof pm);
    SIZE_T freeTotal = 0, freeMax = 0, img = 0, map = 0, prv = 0, rsv = 0; MEMORY_BASIC_INFORMATION mi;
    for (const char* a = nullptr; VirtualQuery(a, &mi, sizeof mi); a = (const char*)mi.BaseAddress + mi.RegionSize) {
        if (mi.State == MEM_FREE) { freeTotal += mi.RegionSize; freeMax = std::max(freeMax, mi.RegionSize); }
        else if (mi.State == MEM_RESERVE) rsv += mi.RegionSize;
        else if (mi.Type == MEM_IMAGE) img += mi.RegionSize;
        else if (mi.Type == MEM_MAPPED) map += mi.RegionSize;
        else prv += mi.RegionSize;
        if ((const char*)mi.BaseAddress + mi.RegionSize < (const char*)mi.BaseAddress) break;
    }
    // the address space by kind: the modules' code (image), mapped files and views, committed private memory, and the
    // ranges only reserved (driver heaps, thread stacks); the client's own surfaces and their CPU copies
    LOGC("memory %s: private %u MB, address space free %u MB, largest free block %u MB | used: image %u, mapped %u, "
         "private %u, reserved %u MB | client surfaces %ld (%.0f MB on the GPU), CPU copies %ld (%.0f MB)", when,
         (unsigned)(pm.PrivateUsage >> 20), (unsigned)(freeTotal >> 20), (unsigned)(freeMax >> 20),
         (unsigned)(img >> 20), (unsigned)(map >> 20), (unsigned)(prv >> 20), (unsigned)(rsv >> 20),
         g_surfLive, g_surfMB, g_dibLive, g_dibMB);
    // once a run, when mapped views take much of the address space: the biggest ones and the file behind each (none: a
    // section without a file, as a driver's memory shared with the GPU)
    static bool listed = false;
    if (!listed && map > (256u << 20)) {
        listed = true;
        std::map<const void*, SIZE_T> byBase;
        for (const char* a = nullptr; VirtualQuery(a, &mi, sizeof mi); a = (const char*)mi.BaseAddress + mi.RegionSize) {
            if (mi.State != MEM_FREE && mi.Type == MEM_MAPPED) byBase[mi.AllocationBase] += mi.RegionSize;
            if ((const char*)mi.BaseAddress + mi.RegionSize < (const char*)mi.BaseAddress) break;
        }
        std::vector<std::pair<SIZE_T, const void*>> v;
        for (auto& e : byBase) v.push_back({ e.second, e.first });
        std::sort(v.begin(), v.end(), [](auto& x, auto& y) { return x.first > y.first; });
        SIZE_T anon = 0; int nAnon = 0;
        for (auto& e : v) { char fn[MAX_PATH] = {}; if (!GetMappedFileNameA(GetCurrentProcess(), (LPVOID)e.second, fn, MAX_PATH)) { anon += e.first; nAnon++; } }
        LOGC("memory: %zu mapped views, %d of them without a file (%u MB)", v.size(), nAnon, (unsigned)(anon >> 20));
        for (size_t i = 0; i < v.size() && i < 15; i++) {
            char fn[MAX_PATH] = {};
            GetMappedFileNameA(GetCurrentProcess(), (LPVOID)v[i].second, fn, MAX_PATH);
            LOGC("memory:   mapped %6.1f MB at %p  %s", v[i].first / 1048576.0, v[i].second, fn[0] ? fn : "(no file)");
        }
    }
}

// ------------------------------------------------------------------------------------------ test runs: a script of actions
// D3D11CLIENT_TEST_SCRIPT (set by tools/run_test.ps1 -Script, test runs only): "t action args; ...", t in seconds of
// system time from the first frame. Actions: lift <vessel> <metres> (into the air over where it stands), cam <vessel>
// in|out (the camera on it, inside or outside), focus <vessel>, dist <vessel> <m>, polar|azim <vessel> <deg> (the external
// camera's distance and turns round its target). Each is logged as it runs.
void D3D11Client::TestScript()
{
    static bool parsed = false; static double t0 = 0;
    struct Act { double t; std::string what, name; double arg; bool done; };
    static std::vector<Act> acts;
    if (!parsed) {
        parsed = true; t0 = oapiGetSysTime();
        char buf[1024] = {};
        if (!GetEnvironmentVariableA("D3D11CLIENT_TEST_SCRIPT", buf, sizeof buf)) return;
        std::string s = buf;
        for (size_t i = 0; i < s.size();) {
            size_t j = s.find(';', i); if (j == std::string::npos) j = s.size();
            char what[32] = {}, name[128] = {}, arg[32] = {}; double t = 0;
            if (sscanf(s.substr(i, j - i).c_str(), "%lf %31s %127s %31s", &t, what, name, arg) >= 3)
                acts.push_back({ t, what, name, !strcmp(arg, "in") ? 1.0 : !strcmp(arg, "out") ? 0.0 : atof(arg), false });
            i = j + 1;
        }
        LOGC("test script: %u actions", (unsigned)acts.size());
    }
    const double t = oapiGetSysTime() - t0;
    {   // "orbit <vessel> <deg/s>": the external camera turns round its target from then on (TAA checks in motion)
        static double orbitRate = 0, lastT = -1;
        for (Act& a : acts) if (a.done && a.what == "orbit") orbitRate = a.arg;
        if (orbitRate != 0 && lastT >= 0) oapiCameraRotAzimuth(orbitRate * PI / 180 * (t - lastT));
        lastT = t;
    }
    {   // "sun <vessel> <fov deg>": the cockpit (a helmet) camera turns onto the Sun and zooms (Sun.cpp checks): a search on
        // the cockpit's direction, one step a frame, as its angles' signs are the camera's own
        static double az = 0, pol = 0, step = 0.3, best = 1e9; static int axis = 0, sgn = 1, phase = 0;
        for (Act& a : acts) if (a.done && a.what == "sun" && a.arg > 0) { oapiCameraSetAperture(a.arg * 0.5 * RAD); a.arg = -1; phase = 1; }
        if (phase == 1 && oapiCameraInternal()) {
            VECTOR3 sp = _V(0, 0, 0), cpos, cd;
            for (DWORD i = 0; i < oapiGetGbodyCount(); i++) { OBJHANDLE h = oapiGetGbodyByIndex(i); if (oapiGetObjectType(h) == OBJTP_STAR) { oapiGetGlobalPos(h, &sp); break; } }
            oapiCameraGlobalPos(&cpos); oapiCameraGlobalDir(&cd);
            const VECTOR3 s = (sp - cpos) / length(sp - cpos);
            const double err = acos(std::clamp(dotp(s, cd), -1.0, 1.0));
            if (err < best) best = err;                       // the last step helped: keep going that way
            else {                                            // it did not: undo it, try the other sign, then the other axis
                (axis ? pol : az) -= sgn * step;
                if (sgn > 0) sgn = -1; else { sgn = 1; axis ^= 1; if (!axis) step *= 0.5; }
            }
            if (step < 2e-4) { phase = 2; LOGC("test script: camera on the Sun, %.3f deg off", err * DEG); }
            else { (axis ? pol : az) += sgn * step; oapiCameraSetCockpitDir(az, pol, false); }
        }
    }
    for (Act& a : acts) {
        if (a.done || t < a.t) continue;
        a.done = true;
        OBJHANDLE h = oapiGetVesselByName(const_cast<char*>(a.name.c_str()));
        if (!h) { LOGC("test script: t %.1f %s - no vessel %s", t, a.what.c_str(), a.name.c_str()); continue; }
        if (a.what == "lift") {
            VESSEL* v = oapiGetVesselInterface(h);
            VESSELSTATUS2 vs; memset(&vs, 0, sizeof vs); vs.version = 2;
            v->GetStatusEx(&vs);
            const double r = length(vs.rpos);
            if (r > 0) vs.rpos = vs.rpos * ((r + a.arg) / r);
            vs.status = 0;
            v->DefSetStateEx(&vs);
        }
        else if (a.what == "cam") oapiCameraAttach(h, a.arg > 0.5 ? 0 : 1);
        else if (a.what == "dist") oapiCameraScaleDist(a.arg / std::max(oapiCameraTargetDist(), 0.1));   // the external camera <arg> m off
        else if (a.what == "polar") oapiCameraRotPolar(a.arg * RAD);         // ... turned up (+) round its target by <arg> degrees
        else if (a.what == "azim") oapiCameraRotAzimuth(a.arg * RAD);        // ... and round it by <arg> degrees
        else if (a.what == "focus") oapiSetFocusObject(h);
        else if (a.what == "fov") oapiCameraSetAperture(a.arg * 0.5 * RAD);         // the field of view, <arg> degrees (the zoom)
        else if (a.what == "look") oapiCameraSetCockpitDir(a.arg * RAD, 0, false);   // the cockpit view turned <arg> degrees round
        else if (a.what == "mfdl" || a.what == "mfdr") oapiOpenMFD((int)a.arg, a.what == "mfdl" ? MFD_LEFT : MFD_RIGHT);   // an MFD's mode (MFD_ORBIT ...)
        else if (a.what == "btnl" || a.what == "btnr") {     // an MFD's button <arg> pressed and let go
            const int mfd = a.what == "btnl" ? MFD_LEFT : MFD_RIGHT;
            oapiProcessMFDButton(mfd, (int)a.arg, PANEL_MOUSE_LBDOWN); oapiProcessMFDButton(mfd, (int)a.arg, PANEL_MOUSE_LBUP);
        }
        else if (a.what == "key") oapiSimulateBufferedKey((DWORD)a.arg);       // a key by its OAPI_KEY code (F8: 66, the cockpit modes)
        else if (a.what == "panel") oapiSetPanel((int)a.arg);                  // a 2D panel by its id
        LOGC("test script: t %.1f %s %s %g", t, a.what.c_str(), a.name.c_str(), a.arg);
        LogMemory("after the action");
        g_traceAlloc = true; g_traceFrames = 60;
    }
}
