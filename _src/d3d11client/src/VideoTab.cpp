// The Launchpad's Video tab for this client (Orbiter's standard dialog, filled from DXGI) and the "Advanced" dialog with
// the client's own settings (Config\D3D11Client\Client.cfg).
#include "Client.h"
#include "resource_video.h"
#include "resource.h"
#include <commctrl.h>

ClientCfg g_cfg;

// ------------------------------------------------------------------------------------------ the client's settings
void ClientCfg::Load()
{
    FILE* f = fopen("Config\\D3D11Client\\Client.cfg", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* sc = strchr(line, ';'); if (sc) *sc = 0;
        char key[64]; double v;
        if (sscanf(line, " %63[A-Za-z] = %lf", key, &v) != 2) continue;
        std::string k = key;
        if (k == "LodBias") lodBias = (float)v;
        else if (k == "Anisotropy") aniso = (int)v;
        else if (k == "Haze") haze = (float)v;
        else if (k == "Liquids") liquids = v != 0;
        else if (k == "Shadows") shadows = v != 0;
        else if (k == "MicroTextures") microTex = v != 0;
        else if (k == "GroundDetail") groundDetail = v != 0;
        else if (k == "Tessellation") tess = v != 0;
        else if (k == "TerrainNormals") terrainNormals = v != 0;
        else if (k == "Geomorph") geomorph = v != 0;
        else if (k == "Exposure") exposure = (float)v;
        else if (k == "StatsLog") statsLog = v != 0;
        else if (k == "AntiAliasing") aa = std::clamp((int)v, 0, 5);
        else if (k == "RenderScale") renderScale = std::clamp((float)v, 0.5f, 1.0f);
        else if (k == "DriverCacheMB") driverCacheMB = std::max((int)v, 0);
        else if (k == "AmbientOcclusion") ao = v != 0;
        else if (k == "Bloom") bloom = v != 0;
        else if (k == "Sharpen") sharpen = std::clamp((float)v, 0.0f, 1.0f);
    }
    fclose(f);
}

void ClientCfg::Save() const
{
    CreateDirectoryA("Config\\D3D11Client", nullptr);
    FILE* f = fopen("Config\\D3D11Client\\Client.cfg", "w");
    if (!f) return;
    fprintf(f, "; D3D11Client settings (Launchpad > Video > Advanced)\n");
    fprintf(f, "LodBias = %.2f      ; terrain detail: -2 coarser .. +2 finer\n", lodBias);
    fprintf(f, "Anisotropy = %d     ; texture filtering 1..16\n", aniso);
    fprintf(f, "Haze = %.2f         ; atmospheric haze, 1 = as configured per planet\n", haze);
    fprintf(f, "Exposure = %.2f     ; overall brightness\n", exposure);
    fprintf(f, "Liquids = %d        ; liquid surfaces (Titan's seas)\n", liquids ? 1 : 0);
    fprintf(f, "Shadows = %d        ; shadows (when available)\n", shadows ? 1 : 0);
    fprintf(f, "MicroTextures = %d  ; close-up surface detail from Config\\MicroTex.cfg\n", microTex ? 1 : 0);
    fprintf(f, "GroundDetail = %d   ; ground material near the camera (Huygens-based on Titan)\n", groundDetail ? 1 : 0);
    fprintf(f, "Tessellation = %d   ; ground relief geometry near the camera (0: shading detail only)\n", tess ? 1 : 0);
    fprintf(f, "TerrainNormals = %d ; per-pixel terrain normals from the elevation data\n", terrainNormals ? 1 : 0);
    fprintf(f, "Geomorph = %d       ; terrain detail levels blend smoothly instead of popping\n", geomorph ? 1 : 0);
    fprintf(f, "StatsLog = %d       ; fps and GPU times in Orbiter.log every 10 s\n", statsLog ? 1 : 0);
    fprintf(f, "Sharpen = %.2f     ; adaptive sharpening of the fine detail, 0 off .. 1\n", sharpen);
    fprintf(f, "Bloom = %d          ; the bright parts glow softly (the Sun in the haze, glints, lamps)\n", bloom ? 1 : 0);
    fprintf(f, "AmbientOcclusion = %d ; darker folds of the ground and under the vessels (from the depth)\n", ao ? 1 : 0);
    fprintf(f, "DriverCacheMB = %d ; the video driver's shader cache for Orbiter: set aside at start above this (0: never)\n", driverCacheMB);
    fprintf(f, "AntiAliasing = %d   ; 0 off, 1 MSAA 2x, 2 MSAA 4x, 3 MSAA 8x, 4 FXAA, 5 TAA (applies at the next start)\n", aa);
    fprintf(f, "RenderScale = %.2f  ; TAA: scene resolution 0.5..1 of the window, upsampled by TAAU (next start)\n", renderScale);
    fclose(f);
}

// ------------------------------------------------------------------------------------------ the Video tab
struct Mode { UINT w, h, hz; };
static std::vector<Mode> g_modes;

static void FillModes(HWND hTab, int adapter)
{
    g_modes.clear();
    ComPtr<IDXGIFactory1> f;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) return;
    ComPtr<IDXGIAdapter1> ad;
    if (FAILED(f->EnumAdapters1(adapter, &ad))) return;
    ComPtr<IDXGIOutput> out;
    if (FAILED(ad->EnumOutputs(0, &out))) {   // a laptop's discrete GPU has no outputs: the modes of the display it drives
        ComPtr<IDXGIAdapter1> a0;
        if (FAILED(f->EnumAdapters1(0, &a0)) || FAILED(a0->EnumOutputs(0, &out))) return;
    }
    UINT n = 0;
    out->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &n, nullptr);
    std::vector<DXGI_MODE_DESC> m(n);
    if (n) out->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &n, m.data());
    for (auto& d : m) {
        UINT hz = d.RefreshRate.Denominator ? (d.RefreshRate.Numerator + d.RefreshRate.Denominator / 2) / d.RefreshRate.Denominator : 60;
        if (d.Width < 800) continue;
        bool dup = false;
        for (auto& x : g_modes) if (x.w == d.Width && x.h == d.Height && x.hz == hz) dup = true;
        if (!dup) g_modes.push_back({ d.Width, d.Height, hz });
    }
    SendDlgItemMessageA(hTab, IDC_VID_MODE, CB_RESETCONTENT, 0, 0);
    for (auto& x : g_modes) {
        char s[64]; sprintf_s(s, "%u x %u  %u Hz", x.w, x.h, x.hz);
        SendDlgItemMessageA(hTab, IDC_VID_MODE, CB_ADDSTRING, 0, (LPARAM)s);
    }
}

static void ShowModeControls(HWND hTab, bool full)
{
    SendDlgItemMessageA(hTab, IDC_VID_FULL, BM_SETCHECK, full ? BST_CHECKED : BST_UNCHECKED, 0);
    SendDlgItemMessageA(hTab, IDC_VID_WINDOW, BM_SETCHECK, full ? BST_UNCHECKED : BST_CHECKED, 0);
    for (int id : { IDC_VID_MODE, IDC_VID_BPP }) EnableWindow(GetDlgItem(hTab, id), full);
    for (int id : { IDC_VID_WIDTH, IDC_VID_HEIGHT, IDC_VID_ASPECT, IDC_VID_4X3, IDC_VID_16X10, IDC_VID_16X9 }) EnableWindow(GetDlgItem(hTab, id), !full);
}

void D3D11Client::InitVideoTab()
{
    HWND t = hVid;
    if (!t) return;
    VIDEODATA* vd = GetVideoData();
    // the adapters
    SendDlgItemMessageA(t, IDC_VID_DEVICE, CB_RESETCONTENT, 0, 0);
    ComPtr<IDXGIFactory1> f;
    int nAd = 0;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) {
        ComPtr<IDXGIAdapter1> ad;
        for (UINT i = 0; f->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND; i++, ad.Reset()) {
            DXGI_ADAPTER_DESC1 d; ad->GetDesc1(&d);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            char s[160]; WideCharToMultiByte(CP_ACP, 0, d.Description, -1, s, 128, nullptr, nullptr);
            char s2[200]; sprintf_s(s2, "%s (%u MB)", s, (unsigned)(d.DedicatedVideoMemory >> 20));
            SendDlgItemMessageA(t, IDC_VID_DEVICE, CB_ADDSTRING, 0, (LPARAM)s2);
            nAd++;
        }
    }
    // default: the adapter with the most video memory (the discrete GPU on a laptop)
    if (vd->deviceidx < 0 || vd->deviceidx >= nAd) {
        size_t bestMem = 0; int best = 0;
        ComPtr<IDXGIAdapter1> ad;
        for (UINT i = 0; f && f->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND && (int)i < nAd; i++, ad.Reset()) {
            DXGI_ADAPTER_DESC1 d; ad->GetDesc1(&d);
            if (d.DedicatedVideoMemory > bestMem) { bestMem = d.DedicatedVideoMemory; best = (int)i; }
        }
        vd->deviceidx = best;
    }
    SendDlgItemMessageA(t, IDC_VID_DEVICE, CB_SETCURSEL, vd->deviceidx, 0);
    FillModes(t, vd->deviceidx);
    // full screen = a borderless window over the whole monitor at its own resolution
    SendDlgItemMessageA(t, IDC_VID_BPP, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessageA(t, IDC_VID_BPP, CB_ADDSTRING, 0, (LPARAM)"Borderless window");
    SendDlgItemMessageA(t, IDC_VID_BPP, CB_SETCURSEL, 0, 0);
    int native = 0;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    for (size_t i = 0; i < g_modes.size(); i++) if ((int)g_modes[i].w == sw && (int)g_modes[i].h == sh) native = (int)i;
    SendDlgItemMessageA(t, IDC_VID_MODE, CB_SETCURSEL, vd->modeidx >= 0 && vd->modeidx < (int)g_modes.size() ? vd->modeidx : native, 0);
    // window: too small (Orbiter's 400 x 300 default) -> the desktop's work area
    RECT wa; SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    if (vd->winw < 800 || vd->winh < 600) {
        RECT r = { 0, 0, 100, 100 }; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        vd->winw = (wa.right - wa.left) - (r.right - r.left - 100);
        vd->winh = (wa.bottom - wa.top) - (r.bottom - r.top - 100);
    }
    SetDlgItemInt(t, IDC_VID_WIDTH, vd->winw, FALSE);
    SetDlgItemInt(t, IDC_VID_HEIGHT, vd->winh, FALSE);
    SendDlgItemMessageA(t, IDC_VID_ASPECT, BM_SETCHECK, BST_UNCHECKED, 0);
    SendDlgItemMessageA(t, IDC_VID_VSYNC, BM_SETCHECK, vd->novsync ? BST_CHECKED : BST_UNCHECKED, 0);
    // what this client does not use
    for (int id : { IDC_VID_PAGEFLIP, IDC_VID_STENCIL, IDC_VID_ENUM }) ShowWindow(GetDlgItem(t, id), SW_HIDE);
    SetDlgItemTextA(t, IDC_VID_INFO, "Advanced");
    ShowModeControls(t, vd->fullscreen);
}

void D3D11Client::clbkRefreshVideoData()
{
    HWND t = hVid;
    if (!t) return;
    VIDEODATA* vd = GetVideoData();
    vd->deviceidx = (int)SendDlgItemMessageA(t, IDC_VID_DEVICE, CB_GETCURSEL, 0, 0);
    vd->modeidx = (int)SendDlgItemMessageA(t, IDC_VID_MODE, CB_GETCURSEL, 0, 0);
    vd->fullscreen = SendDlgItemMessageA(t, IDC_VID_FULL, BM_GETCHECK, 0, 0) == BST_CHECKED;
    vd->novsync = SendDlgItemMessageA(t, IDC_VID_VSYNC, BM_GETCHECK, 0, 0) == BST_CHECKED;
    vd->winw = std::max(640, (int)GetDlgItemInt(t, IDC_VID_WIDTH, nullptr, FALSE));
    vd->winh = std::max(480, (int)GetDlgItemInt(t, IDC_VID_HEIGHT, nullptr, FALSE));
    if (vd->fullscreen && vd->modeidx >= 0 && vd->modeidx < (int)g_modes.size()) { vd->winw = g_modes[vd->modeidx].w; vd->winh = g_modes[vd->modeidx].h; }
}

INT_PTR D3D11Client::LaunchpadVideoWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case IDC_VID_FULL: ShowModeControls(hWnd, true); return TRUE;
        case IDC_VID_WINDOW: ShowModeControls(hWnd, false); return TRUE;
        case IDC_VID_DEVICE:
            if (HIWORD(wParam) == CBN_SELCHANGE) FillModes(hWnd, (int)SendDlgItemMessageA(hWnd, IDC_VID_DEVICE, CB_GETCURSEL, 0, 0));
            return TRUE;
        case IDC_VID_INFO: OpenAdvanced(hWnd); return TRUE;
        }
    }
    return FALSE;
}

// ------------------------------------------------------------------------------------------ the Advanced dialog
static INT_PTR CALLBACK AdvProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    char s[32];
    switch (m) {
    case WM_INITDIALOG:
        sprintf_s(s, "%.2f", g_cfg.lodBias); SetDlgItemTextA(h, IDC_ADV_LOD, s);
        sprintf_s(s, "%d", g_cfg.aniso); SetDlgItemTextA(h, IDC_ADV_ANISO, s);
        sprintf_s(s, "%.2f", g_cfg.haze); SetDlgItemTextA(h, IDC_ADV_HAZE, s);
        sprintf_s(s, "%.2f", g_cfg.exposure); SetDlgItemTextA(h, IDC_ADV_EXPOSURE, s);
        CheckDlgButton(h, IDC_ADV_LIQUIDS, g_cfg.liquids ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(h, IDC_ADV_SHADOWS, g_cfg.shadows ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(h, IDC_ADV_MICRO, g_cfg.microTex ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(h, IDC_ADV_STATS, g_cfg.statsLog ? BST_CHECKED : BST_UNCHECKED);
        for (const char* t : { "Off", "MSAA 2x", "MSAA 4x", "MSAA 8x", "FXAA (post)", "TAA (temporal)" }) SendDlgItemMessageA(h, IDC_ADV_AA, CB_ADDSTRING, 0, (LPARAM)t);
        SendDlgItemMessageA(h, IDC_ADV_AA, CB_SETCURSEL, g_cfg.aa, 0);
        sprintf_s(s, "%d", (int)(g_cfg.renderScale * 100 + 0.5f)); SetDlgItemTextA(h, IDC_ADV_SCALE, s);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            GetDlgItemTextA(h, IDC_ADV_LOD, s, 32); g_cfg.lodBias = std::clamp((float)atof(s), -2.0f, 2.0f);
            GetDlgItemTextA(h, IDC_ADV_ANISO, s, 32); g_cfg.aniso = std::clamp(atoi(s), 1, 16);
            GetDlgItemTextA(h, IDC_ADV_HAZE, s, 32); g_cfg.haze = std::clamp((float)atof(s), 0.0f, 5.0f);
            GetDlgItemTextA(h, IDC_ADV_EXPOSURE, s, 32); g_cfg.exposure = std::clamp((float)atof(s), 0.2f, 5.0f);
            g_cfg.liquids = IsDlgButtonChecked(h, IDC_ADV_LIQUIDS) == BST_CHECKED;
            g_cfg.shadows = IsDlgButtonChecked(h, IDC_ADV_SHADOWS) == BST_CHECKED;
            g_cfg.microTex = IsDlgButtonChecked(h, IDC_ADV_MICRO) == BST_CHECKED;
            g_cfg.statsLog = IsDlgButtonChecked(h, IDC_ADV_STATS) == BST_CHECKED;
            g_cfg.aa = std::clamp((int)SendDlgItemMessageA(h, IDC_ADV_AA, CB_GETCURSEL, 0, 0), 0, 5);
            GetDlgItemTextA(h, IDC_ADV_SCALE, s, 32); g_cfg.renderScale = std::clamp((float)atof(s) / 100.0f, 0.5f, 1.0f);
            g_cfg.Save();
            EndDialog(h, IDOK); return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

void D3D11Client::OpenAdvanced(HWND parent)
{
    DialogBoxParamA(ModuleInstance(), MAKEINTRESOURCEA(IDD_ADVANCED), parent, AdvProc, 0);
}
