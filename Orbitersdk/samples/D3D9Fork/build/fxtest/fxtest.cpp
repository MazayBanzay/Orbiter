// Loads Modules/D3D9Client/AtmoFX_Volume.fxo and the AtmoFX textures on a hidden D3D9 device, like AtmoVolume::Init.
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdio>
int main(int argc, char** argv)
{
    const char* root = argc > 1 ? argv[1] : ".";
    SetCurrentDirectoryA(root);
    HWND w = CreateWindowA("STATIC", "fxtest", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, 0, 0, 0, 0);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp = {}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = w;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, w, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    printf("device %08lx\n", hr); fflush(stdout);
    if (FAILED(hr)) return 1;
    LPD3DXEFFECT fx = nullptr; LPD3DXBUFFER err = nullptr;
    hr = D3DXCreateEffectFromFileA(dev, "Modules/D3D9Client/AtmoFX_Volume.fxo", nullptr, nullptr, 0, nullptr, &fx, &err);
    printf("effect fxo %08lx %s\n", hr, err ? (const char*)err->GetBufferPointer() : ""); fflush(stdout);
    if (fx) { D3DXEFFECT_DESC d; fx->GetDesc(&d); printf("  params %u techniques %u\n", d.Parameters, d.Techniques);
        printf("  Deck valid %08lx, Composite valid %08lx\n", fx->ValidateTechnique("Deck"), fx->ValidateTechnique("Composite")); }
    LPDIRECT3DVOLUMETEXTURE9 nz = nullptr;
    hr = D3DXCreateVolumeTextureFromFileExA(dev, "Textures/AtmoFX/noise3d.dds", D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT,
        0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_NONE, D3DX_FILTER_BOX, 0, nullptr, nullptr, &nz);
    printf("noise3d %08lx\n", hr); fflush(stdout);
    const char* tx[] = { "Textures/AtmoFX/jup_weather.dds", "Textures/AtmoFX/jup_global.dds", "Textures/AtmoFX/jup_flowdir.dds" };
    for (auto f : tx) { LPDIRECT3DTEXTURE9 t = nullptr;
        hr = D3DXCreateTextureFromFileExA(dev, f, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_FILTER_NONE, D3DX_FILTER_BOX, 0, nullptr, nullptr, &t);
        printf("%s %08lx\n", f, hr); fflush(stdout); if (t) t->Release(); }
    printf("done\n");
    return 0;
}
