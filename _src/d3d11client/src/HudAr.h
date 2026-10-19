// World-space paths of the modules (D3D11Hud.h: ArPath), kept by HudApi.cpp for the scene's AR pass (Ar.cpp) to draw.
#pragma once
#include "OrbiterAPI.h"
#include <vector>

struct HudArPath
{
    OBJHANDLE body = nullptr;
    std::vector<double> llh;     // lng, lat [rad], alt above the ground [m], per point
    float width = 2;             // px
    DWORD col = 0xFFFFFFFF;      // 0xAABBGGRR
    unsigned flags = 0;
};
std::vector<HudArPath> HudArPaths();     // a copy, under the lock
