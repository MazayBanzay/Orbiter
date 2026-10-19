// 2D for Orbiter: surfaces have a CPU side (a 32-bit DIB) that GDI Sketchpads, GetDC users and blits work on; changed
// surfaces go to their D3D11 textures once per frame. The main window's 2D layer (HUD, info boxes, glass-cockpit MFDs)
// is a screen-sized DIB, cleared to a key colour each frame and composited over the back buffer before Present; 2D
// instrument panels (clbkRender2DPanel) are drawn on the GPU.
#include "Client.h"

namespace {

class GFont : public oapi::Font
{
public:
    GFont(int height, bool prop, const char* face, FontStyle style, int orientation) : oapi::Font(height, prop, face, style, orientation)
    {
        const char* f = face && *face ? face : (prop ? "Arial" : "Courier New");
        if (!_stricmp(f, "Sans")) f = "Arial";
        else if (!_stricmp(f, "Serif")) f = "Times New Roman";
        else if (!_stricmp(f, "Fixed")) f = "Courier New";
        h = CreateFontA(height, 0, orientation, orientation, (style & FONT_BOLD) ? FW_BOLD : FW_NORMAL, (style & FONT_ITALIC) ? TRUE : FALSE,
                        (style & FONT_UNDERLINE) ? TRUE : FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        ANTIALIASED_QUALITY, (prop ? VARIABLE_PITCH : FIXED_PITCH) | FF_DONTCARE, f);
    }
    ~GFont() { if (h) DeleteObject(h); }
    HFONT GetGDIFont() const override { return h; }
    HFONT h = nullptr;
};

class GPen : public oapi::Pen
{
public:
    GPen(int style, int width, DWORD col) : oapi::Pen(style, width, col)
    {
        h = style == 0 ? (HPEN)GetStockObject(NULL_PEN) : CreatePen(style == 2 ? PS_DOT : PS_SOLID, width, col);
        own = style != 0;
    }
    ~GPen() { if (own && h) DeleteObject(h); }
    HPEN h = nullptr; bool own = false;
};

class GBrush : public oapi::Brush
{
public:
    GBrush(DWORD col) : oapi::Brush(col) { h = CreateSolidBrush(col & 0xFFFFFF); }
    ~GBrush() { if (h) DeleteObject(h); }
    HBRUSH h = nullptr;
};

class GPad : public oapi::Sketchpad
{
public:
    GPad(SURFHANDLE s, HDC dc) : oapi::Sketchpad(s), hdc(dc)
    {
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        SelectObject(hdc, GetStockObject(WHITE_PEN));
        SetTextColor(0xFFFFFF);
        ::SetTextAlign(hdc, TA_LEFT | TA_TOP);
    }
    oapi::Font* SetFont(oapi::Font* f) override { oapi::Font* p = font; font = f; if (f) SelectObject(hdc, ((GFont*)f)->h); return p; }
    oapi::Pen* SetPen(oapi::Pen* p) override { oapi::Pen* o = pen; pen = p; SelectObject(hdc, p ? ((GPen*)p)->h : (HPEN)GetStockObject(NULL_PEN)); return o; }
    oapi::Brush* SetBrush(oapi::Brush* b) override { oapi::Brush* o = brush; brush = b; SelectObject(hdc, b ? ((GBrush*)b)->h : (HBRUSH)GetStockObject(NULL_BRUSH)); return o; }
    void SetTextAlign(TAlign_horizontal th, TAlign_vertical tv) override
    {
        UINT a = (th == CENTER ? TA_CENTER : th == RIGHT ? TA_RIGHT : TA_LEFT) | (tv == BASELINE ? TA_BASELINE : tv == BOTTOM ? TA_BOTTOM : TA_TOP);
        ::SetTextAlign(hdc, a);
    }
    DWORD SetTextColor(DWORD col) override { return ::SetTextColor(hdc, col & 0xFFFFFF); }
    DWORD SetBackgroundColor(DWORD col) override { return ::SetBkColor(hdc, col & 0xFFFFFF); }
    void SetBackgroundMode(BkgMode m) override { SetBkMode(hdc, m == BK_OPAQUE ? OPAQUE : TRANSPARENT); }
    DWORD GetCharSize() override { TEXTMETRICA tm; GetTextMetricsA(hdc, &tm); return MAKELONG(tm.tmHeight - tm.tmInternalLeading, tm.tmAveCharWidth); }
    DWORD GetTextWidth(const char* str, int len) override { SIZE s; GetTextExtentPoint32A(hdc, str, len ? len : (int)strlen(str), &s); return s.cx; }
    void SetOrigin(int x, int y) override { SetViewportOrgEx(hdc, x, y, nullptr); }
    void GetOrigin(int* x, int* y) const override { POINT p; GetViewportOrgEx(hdc, &p); *x = p.x; *y = p.y; }
    bool Text(int x, int y, const char* str, int len) override { return TextOutA(hdc, x, y, str, len) != 0; }
    bool TextW(int x, int y, const LPWSTR str, int len) override { return TextOutW(hdc, x, y, str, len) != 0; }
    void Pixel(int x, int y, DWORD col) override { SetPixel(hdc, x, y, col & 0xFFFFFF); }
    void MoveTo(int x, int y) override { MoveToEx(hdc, x, y, nullptr); }
    void LineTo(int x, int y) override { ::LineTo(hdc, x, y); }
    void Line(int x0, int y0, int x1, int y1) override { MoveToEx(hdc, x0, y0, nullptr); ::LineTo(hdc, x1, y1); }
    void Rectangle(int x0, int y0, int x1, int y1) override { ::Rectangle(hdc, x0, y0, x1, y1); }
    void Ellipse(int x0, int y0, int x1, int y1) override { ::Ellipse(hdc, x0, y0, x1, y1); }
    void Polygon(const oapi::IVECTOR2* pt, int npt) override { ::Polygon(hdc, (const POINT*)pt, npt); }
    void Polyline(const oapi::IVECTOR2* pt, int npt) override { ::Polyline(hdc, (const POINT*)pt, npt); }
    void PolyPolygon(const oapi::IVECTOR2* pt, const int* npt, const int n) override { ::PolyPolygon(hdc, (const POINT*)pt, npt, n); }
    void PolyPolyline(const oapi::IVECTOR2* pt, const int* npt, const int n) override
    {
        std::vector<DWORD> c(npt, npt + n); ::PolyPolyline(hdc, (const POINT*)pt, c.data(), (DWORD)n);
    }
    HDC GetDC() override { return hdc; }
    HDC hdc;
    oapi::Font* font = nullptr; oapi::Pen* pen = nullptr; oapi::Brush* brush = nullptr;
};

}  // namespace


// ------------------------------------------------------------------------------------------ CPU side of surfaces
bool D3D11Client::EnsureDIB(Surf* s)
{
    if (s->dc) return true;
    if (!s->w || !s->h) return false;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = (LONG)s->w; bi.bmiHeader.biHeight = -(LONG)s->h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    s->dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &s->bits, nullptr, 0);
    if (!s->dib) return false;
    s->dc = CreateCompatibleDC(nullptr);
    s->oldBmp = SelectObject(s->dc, s->dib);
    memset(s->bits, 0, (size_t)s->w * s->h * 4);
    // a surface with GPU content (loaded from a file): its pixels come back once (rendered through a target, so
    // compressed formats decode the same way the shaders see them)
    if (s->srv && s->immutable && shadersOk) Readback(s);
    // GDI writes alpha 0 where it draws: transparency is kept as alpha 1 (invisible), so whatever GDI touches shows up
    // as alpha 0 within the DC's bounds rectangle and is made opaque when the drawing ends (FixGdiAlpha)
    BYTE* p = (BYTE*)s->bits;
    for (size_t i = 3; i < (size_t)s->w * s->h * 4; i += 4) if (!p[i]) p[i] = 1;
    SetBoundsRect(s->dc, nullptr, DCB_RESET | DCB_ENABLE);
    return true;
}

void D3D11Client::FixGdiAlpha(Surf* s)
{
    if (!s->dc || !s->bits || s == screen) return;
    GdiFlush();
    RECT r;
    UINT f = GetBoundsRect(s->dc, &r, DCB_RESET);
    if (!(f & DCB_SET)) return;
    r.left = std::max(0L, r.left); r.top = std::max(0L, r.top);
    r.right = std::min((LONG)s->w, r.right); r.bottom = std::min((LONG)s->h, r.bottom);
    for (LONG y = r.top; y < r.bottom; y++) {
        BYTE* row = (BYTE*)s->bits + ((size_t)y * s->w) * 4;
        for (LONG x = r.left; x < r.right; x++) if (!row[x * 4 + 3]) row[x * 4 + 3] = 255;
    }
}

void D3D11Client::Readback(Surf* s)
{
    D3D11_TEXTURE2D_DESC td = { s->w, s->h, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    ComPtr<ID3D11Texture2D> rt, st;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &rt))) return;
    ComPtr<ID3D11RenderTargetView> rtv; dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &st))) return;
    ComPtr<ID3D11RenderTargetView> oRtv; ComPtr<ID3D11DepthStencilView> oDsv; ctx->OMGetRenderTargets(1, &oRtv, &oDsv);
    UINT nvp = 1; D3D11_VIEWPORT ovp; ctx->RSGetViewports(&nvp, &ovp);
    D3D11_VIEWPORT vp = { 0, 0, (float)s->w, (float)s->h, 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    ctx->OMSetBlendState(bsOpaque.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsBlit.Get(), nullptr, 0);
    ctx->PSSetShader(psBlit.Get(), nullptr, 0);
    ctx->PSSetShaderResources(0, 1, s->srv.GetAddressOf());
    ctx->PSSetSamplers(1, 1, sampClamp.GetAddressOf());
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul = nullptr; ctx->PSSetShaderResources(0, 1, &nul);
    ctx->OMSetRenderTargets(1, oRtv.GetAddressOf(), oDsv.Get());
    if (nvp) ctx->RSSetViewports(1, &ovp);
    ctx->CopyResource(st.Get(), rt.Get());
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &ms))) {
        for (UINT y = 0; y < s->h; y++) memcpy((BYTE*)s->bits + (size_t)y * s->w * 4, (BYTE*)ms.pData + (size_t)y * ms.RowPitch, s->w * 4);
        ctx->Unmap(st.Get(), 0);
    }
}

void Surf::FreeDIB()
{
    if (dc) { SelectObject(dc, oldBmp); DeleteDC(dc); dc = nullptr; }
    if (dib) { DeleteObject(dib); dib = nullptr; bits = nullptr; }
}

void D3D11Client::MarkDirty(Surf* s) { if (s && s != screen) { FixGdiAlpha(s); dirty.insert(s); } }

// changed surfaces to their textures (once per frame, before they are drawn)
void D3D11Client::FlushDirty()
{
    for (Surf* s : dirty) UploadSurface(s);
    dirty.clear();
}

void D3D11Client::UploadSurface(Surf* s)
{
    if (!s->dc || !s->bits) return;
    GdiFlush();
    // surfaces made without an alpha channel are opaque; the others keep theirs (file textures, OAPISURFACE_ALPHA)
    BYTE* p = (BYTE*)s->bits;
    if (!(s->attrib & OAPISURFACE_ALPHA) && s->name.empty()) for (size_t i = 3; i < (size_t)s->w * s->h * 4; i += 4) p[i] = 255;
    if (!s->tex || s->immutable) {
        D3D11_TEXTURE2D_DESC td = { s->w, s->h, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, {1, 0}, D3D11_USAGE_DEFAULT,
                                    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0 };
        s->tex.Reset(); s->srv.Reset();
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &s->tex))) return;
        dev->CreateShaderResourceView(s->tex.Get(), nullptr, &s->srv);
        s->immutable = false;
    }
    ctx->UpdateSubresource(s->tex.Get(), 0, nullptr, s->bits, s->w * 4, 0);
}

Surf* D3D11Client::Target(SURFHANDLE h) const { return h ? (Surf*)h : screen; }

// ------------------------------------------------------------------------------------------ Sketchpad, tools, DC
oapi::Sketchpad* D3D11Client::clbkGetSketchpad(SURFHANDLE surf)
{
    Surf* s = Target(surf);
    if (!s || !EnsureDIB(s)) return nullptr;
    SaveDC(s->dc);
    return new GPad(surf, s->dc);
}

void D3D11Client::clbkReleaseSketchpad(oapi::Sketchpad* sp)
{
    if (!sp) return;
    GPad* g = (GPad*)sp;
    Surf* s = Target(g->GetSurface());
    if (s && s->dc) RestoreDC(s->dc, -1);
    delete g;
    MarkDirty(s);
}

oapi::Font* D3D11Client::clbkCreateFont(int height, bool prop, const char* face, FontStyle style, int orientation) const
{
    return new GFont(height, prop, face, style, orientation);
}
oapi::Font* D3D11Client::clbkCreateFontEx(int height, char* face, int width, int weight, FontStyle style, float spacing) const
{
    return new GFont(height, true, face, weight >= 600 ? (FontStyle)(style | FONT_BOLD) : style, 0);
}
void D3D11Client::clbkReleaseFont(oapi::Font* font) const { delete (GFont*)font; }
oapi::Pen* D3D11Client::clbkCreatePen(int style, int width, DWORD col) const { return new GPen(style, width, col); }
void D3D11Client::clbkReleasePen(oapi::Pen* pen) const { delete (GPen*)pen; }
oapi::Brush* D3D11Client::clbkCreateBrush(DWORD col) const { return new GBrush(col); }
void D3D11Client::clbkReleaseBrush(oapi::Brush* brush) const { delete (GBrush*)brush; }

HDC D3D11Client::clbkGetSurfaceDC(SURFHANDLE surf)
{
    Surf* s = Target(surf);
    if (!s || !EnsureDIB(s)) return nullptr;
    return s->dc;
}

void D3D11Client::clbkReleaseSurfaceDC(SURFHANDLE surf, HDC hDC) { MarkDirty(Target(surf)); }

// ------------------------------------------------------------------------------------------ fills and blits
static COLORREF DevToRef(DWORD c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }   // device colour 0xRRGGBB -> GDI

bool D3D11Client::clbkFillSurface(SURFHANDLE surf, DWORD col) const
{
    Surf* s = Target(surf);
    if (!s) return false;
    return clbkFillSurface(surf, 0, 0, s->w, s->h, col);
}

bool D3D11Client::clbkFillSurface(SURFHANDLE surf, DWORD tx, DWORD ty, DWORD w, DWORD h, DWORD col) const
{
    D3D11Client* me = const_cast<D3D11Client*>(this);
    Surf* s = Target(surf);
    if (!s || !me->EnsureDIB(s)) return false;
    RECT r = { (LONG)tx, (LONG)ty, (LONG)(tx + w), (LONG)(ty + h) };
    HBRUSH b = CreateSolidBrush(DevToRef(col));
    FillRect(s->dc, &r, b);
    DeleteObject(b);
    me->MarkDirty(s);
    return true;
}

bool D3D11Client::clbkSetSurfaceColourKey(SURFHANDLE surf, DWORD ckey)
{
    Surf* s = (Surf*)surf;
    if (!s) return false;
    s->ckey = ckey; s->hasKey = true;
    return true;
}

bool D3D11Client::BltImpl(SURFHANDLE tgt, int tx, int ty, int tw, int th, SURFHANDLE src, int sx, int sy, int sw, int sh, DWORD flag)
{
    Surf* t = Target(tgt);
    Surf* s = (Surf*)src;
    if (!t || !s || !EnsureDIB(t) || !EnsureDIB(s)) return false;
    GdiFlush();
    if ((flag & BLT_SRCCOLORKEY) && s->hasKey) {
        TransparentBlt(t->dc, tx, ty, tw, th, s->dc, sx, sy, sw, sh, DevToRef(s->ckey));
    } else if (tw == sw && th == sh) {
        BitBlt(t->dc, tx, ty, tw, th, s->dc, sx, sy, SRCCOPY);
    } else {
        SetStretchBltMode(t->dc, HALFTONE); SetBrushOrgEx(t->dc, 0, 0, nullptr);
        StretchBlt(t->dc, tx, ty, tw, th, s->dc, sx, sy, sw, sh, SRCCOPY);
    }
    MarkDirty(t);
    return true;
}

bool D3D11Client::clbkBlt(SURFHANDLE tgt, DWORD tgtx, DWORD tgty, SURFHANDLE src, DWORD flag) const
{
    Surf* s = (Surf*)src;
    if (!s) return false;
    return const_cast<D3D11Client*>(this)->BltImpl(tgt, tgtx, tgty, s->w, s->h, src, 0, 0, s->w, s->h, flag);
}

bool D3D11Client::clbkBlt(SURFHANDLE tgt, DWORD tgtx, DWORD tgty, SURFHANDLE src, DWORD srcx, DWORD srcy, DWORD w, DWORD h, DWORD flag) const
{
    return const_cast<D3D11Client*>(this)->BltImpl(tgt, tgtx, tgty, w, h, src, srcx, srcy, w, h, flag);
}

bool D3D11Client::clbkScaleBlt(SURFHANDLE tgt, DWORD tgtx, DWORD tgty, DWORD tgtw, DWORD tgth, SURFHANDLE src, DWORD srcx, DWORD srcy, DWORD srcw, DWORD srch, DWORD flag) const
{
    return const_cast<D3D11Client*>(this)->BltImpl(tgt, tgtx, tgty, tgtw, tgth, src, srcx, srcy, srcw, srch, flag);
}

int D3D11Client::clbkBeginBltGroup(SURFHANDLE tgt) { return 0; }
int D3D11Client::clbkEndBltGroup() { return 0; }

// ------------------------------------------------------------------------------------------ the main window's 2D layer
static const DWORD KEY = 0x00010001;   // the screen layer's "nothing drawn" colour (near black: GDI's anti-aliasing fringes stay dark)

void D3D11Client::BeginScreenLayer()
{
    if (!screen) {
        screen = new Surf; screen->w = W; screen->h = H;
        if (!EnsureDIB(screen)) return;
        DWORD* p = (DWORD*)screen->bits;
        std::fill(p, p + (size_t)W * H, KEY);
        screenPrev = { 0, 0, (LONG)W, (LONG)H };
    }
    // clear what was drawn last frame back to the key colour; start collecting this frame's bounds
    for (LONG y = screenPrev.top; y < screenPrev.bottom; y++) std::fill((DWORD*)screen->bits + (size_t)y * W + screenPrev.left, (DWORD*)screen->bits + (size_t)y * W + screenPrev.right, KEY);
    SetBoundsRect(screen->dc, nullptr, DCB_RESET | DCB_ENABLE);
}

void D3D11Client::CompositeScreenLayer()
{
    if (!screen || !screen->bits || !shadersOk) return;
    GdiFlush();
    RECT cur = { 0, 0, 0, 0 };
    if (!(GetBoundsRect(screen->dc, &cur, DCB_RESET) & DCB_SET)) cur = { 0, 0, 0, 0 };
    cur.left = std::max(0L, cur.left); cur.top = std::max(0L, cur.top);
    cur.right = std::min((LONG)W, cur.right); cur.bottom = std::min((LONG)H, cur.bottom);
    if (cur.right < cur.left) cur.right = cur.left;
    if (cur.bottom < cur.top) cur.bottom = cur.top;
    // the area to send: this frame's drawing and last frame's (now cleared)
    RECT u = cur;
    if (screenPrev.right > screenPrev.left) {
        if (u.right <= u.left) u = screenPrev;
        else { u.left = std::min(u.left, screenPrev.left); u.top = std::min(u.top, screenPrev.top); u.right = std::max(u.right, screenPrev.right); u.bottom = std::max(u.bottom, screenPrev.bottom); }
    }
    screenPrev = cur;
    if (!screenTex) {
        D3D11_TEXTURE2D_DESC td = { W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        std::vector<DWORD> zero((size_t)W * H, 0);
        D3D11_SUBRESOURCE_DATA sd = { zero.data(), W * 4, 0 };
        dev->CreateTexture2D(&td, &sd, &screenTex);
        dev->CreateShaderResourceView(screenTex.Get(), nullptr, &screenSRV);
    }
    if (u.right > u.left && u.bottom > u.top) {
        UINT uw = u.right - u.left, uh = u.bottom - u.top;
        screenTmp.resize((size_t)uw * uh);
        for (UINT y = 0; y < uh; y++) {
            const DWORD* src = (const DWORD*)screen->bits + (size_t)(u.top + y) * W + u.left;
            DWORD* dst = screenTmp.data() + (size_t)y * uw;
            for (UINT x = 0; x < uw; x++) { DWORD c = src[x] & 0xFFFFFF; dst[x] = c == KEY ? 0 : (c | 0xFF000000); }
        }
        D3D11_BOX box = { (UINT)u.left, (UINT)u.top, 0, (UINT)u.right, (UINT)u.bottom, 1 };
        ctx->UpdateSubresource(screenTex.Get(), 0, &box, screenTmp.data(), uw * 4, 0);
    }
    if (cur.right <= cur.left) return;     // nothing on the layer this frame
    D3D11_VIEWPORT vp = { 0, 0, (float)W, (float)H, 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, bbRTV.GetAddressOf(), nullptr);
    ctx->OMSetBlendState(bsAlpha.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsBlit.Get(), nullptr, 0);
    ctx->PSSetShader(psBlit.Get(), nullptr, 0);
    ctx->PSSetShaderResources(0, 1, screenSRV.GetAddressOf());
    ctx->PSSetSamplers(1, 1, sampClamp.GetAddressOf());
    ctx->Draw(3, 0);
    ID3D11ShaderResourceView* nul = nullptr; ctx->PSSetShaderResources(0, 1, &nul);
}

// 2D instrument panels: the panel mesh in panel pixels, scaled and shifted by T into the window
struct PanelCB { XMFLOAT4 xf; XMFLOAT4 misc; };

void D3D11Client::clbkRender2DPanel(SURFHANDLE* hSurf, MESHHANDLE hMesh, MATRIX3* T, bool additive)
{
    clbkRender2DPanel(hSurf, hMesh, T, 1.0f, additive);
}

void D3D11Client::clbkRender2DPanel(SURFHANDLE* hSurf, MESHHANDLE hMesh, MATRIX3* T, float alpha, bool additive)
{
    DWORD ng = oapiMeshGroupCount(hMesh);
    if (!ng || !shadersOk) return;
    FlushDirty();
    PanelCB c;
    c.xf = { (float)T->m11, (float)T->m22, (float)T->m13, (float)T->m23 };
    c.misc = { 2.0f / W, 2.0f / H, alpha, 0 };
    D3D11_MAPPED_SUBRESOURCE ms;
    ctx->Map(cbObj.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, &c, sizeof c); ctx->Unmap(cbObj.Get(), 0);
    D3D11_VIEWPORT vp = { 0, 0, (float)W, (float)H, 0, 1 };
    ctx->RSSetViewports(1, &vp);
    ctx->OMSetRenderTargets(1, bbRTV.GetAddressOf(), nullptr);
    ctx->OMSetBlendState(additive ? bsAdd.Get() : bsAlpha.Get(), nullptr, ~0u);
    ctx->OMSetDepthStencilState(dsRead.Get(), 0);
    ctx->RSSetState(rsNone.Get());
    ctx->IASetInputLayout(ilMesh.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vsPanel.Get(), nullptr, 0);
    ctx->PSSetShader(psPanel.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ctx->PSSetConstantBuffers(1, 1, cbObj.GetAddressOf());
    ctx->PSSetSamplers(1, 1, sampClamp.GetAddressOf());
    for (DWORD g = 0; g < ng; g++) {
        MESHGROUP* grp = oapiMeshGroup(hMesh, g);
        if (!grp || !grp->nIdx || (grp->UsrFlag & 2)) continue;
        Surf* s = hSurf && grp->TexIdx != (DWORD)-1 ? (Surf*)hSurf[grp->TexIdx] : nullptr;
        ID3D11ShaderResourceView* srv = s && s->srv ? s->srv.Get() : whiteTex.Get();
        // the group's vertices change between frames (needles, buttons): a dynamic buffer
        UINT vbytes = grp->nVtx * sizeof(NTVERTEX), ibytes = ((grp->nIdx * 2) + 3) & ~3u;
        if (vbytes > panelVBSize) { panelVB.Reset(); panelVBSize = std::max(vbytes, 65536u); D3D11_BUFFER_DESC bd = { panelVBSize, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 }; dev->CreateBuffer(&bd, nullptr, &panelVB); }
        if (ibytes > panelIBSize) { panelIB.Reset(); panelIBSize = std::max(ibytes, 65536u); D3D11_BUFFER_DESC bd = { panelIBSize, D3D11_USAGE_DYNAMIC, D3D11_BIND_INDEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 }; dev->CreateBuffer(&bd, nullptr, &panelIB); }
        ctx->Map(panelVB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, grp->Vtx, vbytes); ctx->Unmap(panelVB.Get(), 0);
        ctx->Map(panelIB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms); memcpy(ms.pData, grp->Idx, grp->nIdx * 2); ctx->Unmap(panelIB.Get(), 0);
        UINT stride = sizeof(NTVERTEX), off = 0;
        ctx->IASetVertexBuffers(0, 1, panelVB.GetAddressOf(), &stride, &off);
        ctx->IASetIndexBuffer(panelIB.Get(), DXGI_FORMAT_R16_UINT, 0);
        ctx->PSSetShaderResources(0, 1, &srv);
        ctx->DrawIndexed(grp->nIdx, 0, 0);
    }
}
