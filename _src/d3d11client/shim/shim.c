// The forwarding shim "D3D9Client.dll" (only the D3D11 client loads it, from Modules\D3D11Client\): every export
// is forwarded to D3D11Client.dll. No code of its own.
#include <windows.h>
#pragma comment(linker, "/export:gcBindCoreMethod=D3D11Client.gcBindCoreMethod")
#pragma comment(linker, "/export:gcGetCoreAPI=D3D11Client.gcGetCoreAPI")
#pragma comment(linker, "/export:AtmoFX_SetJet=D3D11Client.AtmoFX_SetJet")
#pragma comment(linker, "/export:AtmoFX_SetWake=D3D11Client.AtmoFX_SetWake")
#pragma comment(linker, "/export:AtmoFX_SetHeatSource=D3D11Client.AtmoFX_SetHeatSource")
#pragma comment(linker, "/export:AtmoFX_SetFoam=D3D11Client.AtmoFX_SetFoam")
#pragma comment(linker, "/export:AtmoFX_SetDirt=D3D11Client.AtmoFX_SetDirt")
#pragma comment(linker, "/export:AtmoFX_Emit=D3D11Client.AtmoFX_Emit")
#pragma comment(linker, "/export:AtmoFX_Box=D3D11Client.AtmoFX_Box")
#pragma comment(linker, "/export:AtmoFX_Mark=D3D11Client.AtmoFX_Mark")
#pragma comment(linker, "/export:AtmoFX_MarkCount=D3D11Client.AtmoFX_MarkCount")
#pragma comment(linker, "/export:AtmoFX_MarkGet=D3D11Client.AtmoFX_MarkGet")
#pragma comment(linker, "/export:AtmoFX_MarkPut=D3D11Client.AtmoFX_MarkPut")
#pragma comment(linker, "/export:AtmoFX_HudFrame=D3D11Client.AtmoFX_HudFrame")
#pragma comment(linker, "/export:AtmoFX_HudMarker=D3D11Client.AtmoFX_HudMarker")
#pragma comment(linker, "/export:D3D11Hud_GetApi=D3D11Client.D3D11Hud_GetApi")   // D3D11Hud.h (HudApi.cpp)
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p) { return TRUE; }
