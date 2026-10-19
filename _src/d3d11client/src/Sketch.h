// The Sketchpad on the GPU (GpuPad.cpp): the primitives of a Sketchpad session gathered into one vertex batch and drawn
// into the surface's gamma-space render target (shaders\sketch.hlsl). Used for the surfaces made as render targets
// (OAPISURFACE_RENDERTARGET): the modules' HUD layers, displays and MFD surfaces; the rest stays on the DIB (Draw.cpp).
#pragma once
#include "Client.h"

struct SkV
{
    float x, y, u, v;
    float p[4]; float q[2];
    UINT col;            // 0xAABBGGRR (as R8G8B8A8_UNORM)
    float tint[4];
    UINT kind;           // 0 solid, 1 line, 2 ellipse, 3 surface, 4 text, 5 surface with a colour key
    float z;
};

class SkBatch
{
public:
    // blend states as Sketchpad::BlendState (1 alpha blend, 2 copy, 3 copy alpha, 4 copy colour)
    bool Begin(Surf* s);
    void End();
    void SetBlend(int mode);
    void SetSource(Surf* src);
    void SetGamma(bool on, const float* g);
    void SetKey(DWORD key);       // device colour 0xRRGGBB
    void Prim();                  // the next primitive: its own depth
    void Seg(float ax, float ay, float bx, float by, float r, float dash, DWORD col);
    void Ellipse(float cx, float cy, float a, float b, float hw, DWORD col);
    void Tri(float x0, float y0, float x1, float y1, float x2, float y2, DWORD col);
    void Quad(const float* xy, const float* uv, DWORD col, const float* tint, UINT kind, bool point, bool noAlpha);
    // text drawn by GDI white on black (bits: BGRA rows of w x h), placed with its top left at x, y
    void Text(float x, float y, int w, int h, const DWORD* bits, int pitch, DWORD col);
    void Flush();
    Surf* tgt = nullptr;
private:
    std::vector<SkV> v;
    int blend = 1;
    Surf* src = nullptr;
    bool gammaOn = false; float gam[3] = { 1, 1, 1 };
    float key[3] = { 0, 0, 0 };
    UINT prim = 0;
    float Z() const;
    void Put(float x, float y, float u, float vv, const float* p, const float* q, DWORD col, const float* tint, UINT kind);
};
