#pragma once
// The extended Sketchpad (QuickPen/QuickBrush, StretchRect, SetBrightness/SetRenderParam) on both SDKs.
// Orbiter 2016: Sketchpad2/Sketchpad3 are subclasses the D3D9Client returns (dynamic_cast finds them).
// Orbiter 2024: merged into oapi::Sketchpad; the base methods assert, and GetVersion() is 1 for every client - so the
// D3D9Client (its module loaded) is what tells that the pad has them.
#if __has_include("gcCoreAPI.h")   // the Orbiter 2024 SDK
#include "DrawAPI.h"
#include <windows.h>
using oapi::IVECTOR2;
using oapi::FVECTOR2;
using oapi::FVECTOR4;
namespace tantra {
using Skp2 = oapi::Sketchpad;
using Skp3 = oapi::Sketchpad;
inline bool SkpExtended() { static const bool d3d9 = GetModuleHandleA("D3D9Client.dll") != nullptr; return d3d9; }
inline Skp2* AsSkp2(oapi::Sketchpad* s) { return s && SkpExtended() ? s : nullptr; }
inline Skp3* AsSkp3(oapi::Sketchpad* s) { return s && SkpExtended() ? s : nullptr; }
constexpr auto kSkpGamma = oapi::Sketchpad::PRM_GAMMA;
}  // namespace tantra
#else                               // the Orbiter 2016 SDK
#include "Sketchpad2.h"
namespace tantra {
using Skp2 = oapi::Sketchpad2;
using Skp3 = oapi::Sketchpad3;
inline Skp2* AsSkp2(oapi::Sketchpad* s) { return dynamic_cast<Skp2*>(s); }
inline Skp3* AsSkp3(oapi::Sketchpad* s) { return dynamic_cast<Skp3*>(s); }
constexpr auto kSkpGamma = SKP3_PRM_GAMMA;
}  // namespace tantra
#endif
