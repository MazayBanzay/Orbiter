// 2D for Orbiter: surfaces have a CPU side (a 32-bit DIB) that GDI Sketchpads, GetDC users and blits work on; changed
// surfaces go to their D3D11 textures once per frame. The main window's 2D layer (HUD, info boxes, glass-cockpit MFDs)
// is a screen-sized DIB, cleared to a key colour each frame and composited over the back buffer before Present; 2D
// instrument panels (clbkRender2DPanel) are drawn on the GPU.
#include "Client.h"
#include "Sketch.h"
#include <climits>

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
    GPen(int style, int width, DWORD col) : oapi::Pen(style, width, col), st(style), wd(width), cl(col)
    {
        h = style == 0 ? (HPEN)GetStockObject(NULL_PEN) : CreatePen(style == 2 ? PS_DOT : PS_SOLID, width, col);
        own = style != 0;
    }
    ~GPen() { if (own && h) DeleteObject(h); }
    HPEN h = nullptr; bool own = false;
    int st, wd; DWORD cl;
};

class GBrush : public oapi::Brush
{
public:
    GBrush(DWORD col) : oapi::Brush(col), cl(col) { h = CreateSolidBrush(col & 0xFFFFFF); }
    ~GBrush() { if (h) DeleteObject(h); }
    HBRUSH h = nullptr; DWORD cl;
};

// the colours and alpha of the Sketchpad 2 calls (QuickPen, QuickBrush, StretchRect, the blend states, the 2D world
// transform) are drawn in software on the surface's DIB: one primitive's coverage is gathered first (anti-aliased
// lines and ellipses, the overlaps of a polyline counted once), then blended in. The rest stays GDI.
static const DWORD SCREEN_KEY = 0x00010001;
// the 2D drawing's cost by kind (logged every 5 s while there is any)
enum { PF_FILL, PF_LINE, PF_ELLIPSE, PF_POLY, PF_STRETCH, PF_FIXALPHA, PF_UPLOAD, PF_N };
static double g_pf[PF_N]; static int g_pfn[PF_N];
struct UplInfo { int n = 0; UINT w = 0, h = 0; DWORD attrib = 0; std::string name; };
static std::map<const void*, UplInfo> g_upl;     // diagnostics: which surfaces go up, how often
struct PfScope { int k; LARGE_INTEGER t0; PfScope(int k) : k(k) { QueryPerformanceCounter(&t0); }
    ~PfScope() { LARGE_INTEGER t1, f; QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&f); g_pf[k] += 1000.0 * (t1.QuadPart - t0.QuadPart) / f.QuadPart; g_pfn[k]++; } };
void Prof2DTick()
{
    static double t0 = -1; const double now = oapiGetSysTime();
    if (t0 < 0) t0 = now;
    if (now - t0 < 5) return;
    double sum = 0; for (double v : g_pf) sum += v;
    if (sum > 1) LOGC("2D ms/s: fill %.1f (%d), lines %.1f (%d), ellipses %.1f (%d), polygons %.1f (%d), stretch %.1f (%d), gdi alpha %.1f (%d), upload %.1f (%d)",
        g_pf[0] / (now - t0), g_pfn[0], g_pf[1] / (now - t0), g_pfn[1], g_pf[2] / (now - t0), g_pfn[2], g_pf[3] / (now - t0), g_pfn[3],
        g_pf[4] / (now - t0), g_pfn[4], g_pf[5] / (now - t0), g_pfn[5], g_pf[6] / (now - t0), g_pfn[6]);
    for (auto& kv : g_upl) if (kv.second.n >= 25)
        LOGC("diag 2D: surface %ux%u attrib 0x%X %s sent up %d times in 5 s", kv.second.w, kv.second.h, kv.second.attrib, kv.second.name.c_str(), kv.second.n);
    g_upl.clear();
    for (int i = 0; i < PF_N; i++) { g_pf[i] = 0; g_pfn[i] = 0; }
    t0 = now;
}   // the screen layer's "nothing drawn" colour (Draw.cpp, below)

struct Raster
{
    int W = 0, H = 0;
    std::vector<BYTE> cov; std::vector<int> rx0, rx1; int ry0 = INT_MAX, ry1 = -1;
    void Begin(int w, int h)
    {
        if (w == W && h == H) return;
        W = w; H = h; cov.assign((size_t)w * h, 0); rx0.assign(h, INT_MAX); rx1.assign(h, -1); ry0 = INT_MAX; ry1 = -1;
    }
    inline void Put(int x, int y, float a)
    {
        if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H || a <= 0.002f) return;
        BYTE v = (BYTE)std::min(255.0f, a * 255.0f + 0.5f);
        BYTE& d = cov[(size_t)y * W + x]; if (v > d) d = v;
        if (x < rx0[y]) rx0[y] = x;
        if (x > rx1[y]) rx1[y] = x;
        if (y < ry0) ry0 = y;
        if (y > ry1) ry1 = y;
    }
    // a line of half width r (round ends); dash > 0: dashes of that length along it
    void Segment(float ax, float ay, float bx, float by, float r, float dash)
    {
        const float dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy, L = sqrtf(L2), R = r + 1;
        int y0 = std::max(0, (int)floorf(std::min(ay, by) - R)), y1 = std::min(H - 1, (int)ceilf(std::max(ay, by) + R));
        for (int y = y0; y <= y1; y++) {
            const float yc = y + 0.5f;
            float xa, xb;
            if (fabsf(dy) < 1e-6f) { if (fabsf(ay - yc) > R) continue; xa = std::min(ax, bx); xb = std::max(ax, bx); }
            else {
                float t0 = (yc - R - ay) / dy, t1 = (yc + R - ay) / dy;
                if (t0 > t1) std::swap(t0, t1);
                t0 = std::max(t0, 0.0f); t1 = std::min(t1, 1.0f);
                if (t0 > t1) continue;
                const float x0 = ax + dx * t0, x1 = ax + dx * t1;
                xa = std::min(x0, x1); xb = std::max(x0, x1);
            }
            const int ix0 = std::max(0, (int)floorf(xa - R)), ix1 = std::min(W - 1, (int)ceilf(xb + R));
            for (int x = ix0; x <= ix1; x++) {
                const float px = x + 0.5f;
                float t = L2 > 0 ? ((px - ax) * dx + (yc - ay) * dy) / L2 : 0;
                t = std::clamp(t, 0.0f, 1.0f);
                const float qx = ax + dx * t - px, qy = ay + dy * t - yc;
                float c = r + 0.5f - sqrtf(qx * qx + qy * qy);
                if (c <= 0) continue;
                if (dash > 0 && fmodf(t * L, dash * 2) > dash) continue;
                Put(x, y, std::min(c, 1.0f));
            }
        }
    }
    // an axis-aligned ellipse: filled (hw < 0) or its outline of half width hw
    void Ellipse(float cx, float cy, float a, float b, float hw)
    {
        a = std::max(a, 0.5f); b = std::max(b, 0.5f);
        const float R = std::max(hw, 0.0f) + 1;
        const int y0 = std::max(0, (int)floorf(cy - b - R)), y1 = std::min(H - 1, (int)ceilf(cy + b + R));
        const int x0 = std::max(0, (int)floorf(cx - a - R)), x1 = std::min(W - 1, (int)ceilf(cx + a + R));
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            const float X = x + 0.5f - cx, Y = y + 0.5f - cy;
            const float k0 = sqrtf(X * X / (a * a) + Y * Y / (b * b)), k1 = sqrtf(X * X / (a * a * a * a) + Y * Y / (b * b * b * b));
            const float d = k1 > 1e-6f ? k0 * (k0 - 1) / k1 : -std::min(a, b);
            const float c = hw < 0 ? 0.5f - d : hw + 0.5f - fabsf(d);
            if (c > 0) Put(x, y, std::min(c, 1.0f));
        }
    }
    // a polygon, filled at the pixel centres (even-odd)
    void PolyFill(const std::vector<float>& p)
    {
        const int n = (int)p.size() / 2;
        if (n < 3) return;
        float ymin = 1e30f, ymax = -1e30f;
        for (int i = 0; i < n; i++) { ymin = std::min(ymin, p[i * 2 + 1]); ymax = std::max(ymax, p[i * 2 + 1]); }
        std::vector<float> xs;
        for (int y = std::max(0, (int)floorf(ymin)); y <= std::min(H - 1, (int)ceilf(ymax)); y++) {
            const float yc = y + 0.5f; xs.clear();
            for (int i = 0, j = n - 1; i < n; j = i++) {
                const float ya = p[j * 2 + 1], yb = p[i * 2 + 1];
                if ((ya <= yc) == (yb <= yc)) continue;
                xs.push_back(p[j * 2] + (yc - ya) / (yb - ya) * (p[i * 2] - p[j * 2]));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2)
                for (int x = std::max(0, (int)ceilf(xs[k] - 0.5f)); x <= std::min(W - 1, (int)ceilf(xs[k + 1] - 0.5f) - 1); x++) Put(x, y, 1);
        }
    }
    template<class F> void Commit(F pix)
    {
        for (int y = ry0; y <= ry1; y++) {
            BYTE* row = cov.data() + (size_t)y * W;
            for (int x = rx0[y]; x <= rx1[y]; x++) if (row[x]) { pix(x, y, row[x] * (1.0f / 255)); row[x] = 0; }
            rx0[y] = INT_MAX; rx1[y] = -1;
        }
        ry0 = INT_MAX; ry1 = -1;
    }
};
static Raster g_ras;    // the 2D drawing runs on the main thread

struct TextScratch
{
    HDC dc = nullptr; HBITMAP bmp = nullptr; HGDIOBJ old = nullptr; DWORD* bits = nullptr; int w = 0, h = 0;
    HDC Get() { if (!dc) dc = CreateCompatibleDC(nullptr); return dc; }
    bool Ensure(int ww, int hh)
    {
        Get();
        if (bmp && ww <= w && hh <= h) return true;
        w = std::max(ww, w); h = std::max(hh, h);
        BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        HBITMAP nb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
        if (!nb) return false;
        HGDIOBJ prev = SelectObject(dc, nb);
        if (!old) old = prev; else DeleteObject(prev);
        bmp = nb; return true;
    }
};
static TextScratch g_txt;

class GPad : public oapi::Sketchpad
{
public:
    GPad(SURFHANDLE h, Surf* s, bool screen, HDC dc) : oapi::Sketchpad(h), hdc(dc), surf(s), isScreen(screen)
    {
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        SelectObject(hdc, GetStockObject(WHITE_PEN));
        SetTextColor(0xFFFFFF);
        ::SetTextAlign(hdc, TA_LEFT | TA_TOP);
    }
    // on the GPU: the batch draws, the scratch DC measures and renders text
    GPad(SURFHANDLE h, Surf* s, SkBatch* b) : oapi::Sketchpad(h), hdc(g_txt.Get()), surf(s), isScreen(false), gb(b)
    {
        SetBkMode(hdc, TRANSPARENT); ::SetTextAlign(hdc, TA_LEFT | TA_TOP);
    }
    ~GPad() { if (qpenH) DeleteObject(qpenH); delete gb; }
    SkBatch* gb = nullptr;
    UINT talign = TA_LEFT | TA_TOP; DWORD tcol = 0xFFFFFFFF, bkcol = 0xFF000000; bool bkOpaque = false;
    oapi::Font* SetFont(oapi::Font* f) override { oapi::Font* p = font; font = f; if (f) SelectObject(hdc, ((GFont*)f)->h); return p; }
    oapi::Pen* SetPen(oapi::Pen* p) override { oapi::Pen* o = pen; pen = p; qpen = false; SelectObject(hdc, p ? ((GPen*)p)->h : (HPEN)GetStockObject(NULL_PEN)); return o; }
    oapi::Brush* SetBrush(oapi::Brush* b) override { oapi::Brush* o = brush; brush = b; qbrush = false; SelectObject(hdc, b ? ((GBrush*)b)->h : (HBRUSH)GetStockObject(NULL_BRUSH)); return o; }
    void SetTextAlign(TAlign_horizontal th, TAlign_vertical tv) override
    {
        UINT a = (th == CENTER ? TA_CENTER : th == RIGHT ? TA_RIGHT : TA_LEFT) | (tv == BASELINE ? TA_BASELINE : tv == BOTTOM ? TA_BOTTOM : TA_TOP);
        talign = a;
        if (!gb) ::SetTextAlign(hdc, a);
    }
    DWORD SetTextColor(DWORD col) override { DWORD o = tcol; tcol = Comp(col); if (!gb) ::SetTextColor(hdc, col & 0xFFFFFF); return o & 0xFFFFFF; }
    DWORD SetBackgroundColor(DWORD col) override { DWORD o = bkcol; bkcol = Comp(col); if (!gb) ::SetBkColor(hdc, col & 0xFFFFFF); return o & 0xFFFFFF; }
    void SetBackgroundMode(BkgMode m) override { bkOpaque = m == BK_OPAQUE; if (!gb) SetBkMode(hdc, m == BK_OPAQUE ? OPAQUE : TRANSPARENT); }
    DWORD GetCharSize() override { TEXTMETRICA tm; GetTextMetricsA(hdc, &tm); return MAKELONG(tm.tmHeight - tm.tmInternalLeading, tm.tmAveCharWidth); }
    DWORD GetTextWidth(const char* str, int len) override { SIZE s; GetTextExtentPoint32A(hdc, str, len ? len : (int)strlen(str), &s); return s.cx; }
    void SetOrigin(int x, int y) override { if (!gb) SetViewportOrgEx(hdc, x, y, nullptr); ox = x; oy = y; }
    void GetOrigin(int* x, int* y) const override { *x = ox; *y = oy; }
    bool Text(int x, int y, const char* str, int len) override { if (gb) return GText(x, y, str, nullptr, len); return TextOutA(hdc, x, y, str, len) != 0; }
    bool TextW(int x, int y, const LPWSTR str, int len) override { if (gb) return GText(x, y, nullptr, str, len); return TextOutW(hdc, x, y, str, len) != 0; }
    void Pixel(int x, int y, DWORD col) override
    {
        if (gb) { float X, Y; Tr((float)x, (float)y, X, Y); gb->Prim(); DWORD c = Comp(col); gb->Tri(X, Y, X + 1, Y, X + 1, Y + 1, c); gb->Tri(X, Y, X + 1, Y + 1, X, Y + 1, c); return; }
        SetPixel(hdc, x, y, col & 0xFFFFFF);
    }
    void MoveTo(int x, int y) override { cx = x; cy = y; if (!gb) MoveToEx(hdc, x, y, nullptr); }
    void LineTo(int x, int y) override
    {
        if (gb) { GLines({ (float)cx, (float)cy, (float)x, (float)y }, false); cx = x; cy = y; return; }
        if (Soft(qpen)) { Seg((float)cx, (float)cy, (float)x, (float)y); CommitPen(); MoveToEx(hdc, x, y, nullptr); }
        else ::LineTo(hdc, x, y);
        cx = x; cy = y;
    }
    void Line(int x0, int y0, int x1, int y1) override
    {
        if (gb) { GLines({ (float)x0, (float)y0, (float)x1, (float)y1 }, false); cx = x1; cy = y1; return; }
        if (Soft(qpen)) { Seg((float)x0, (float)y0, (float)x1, (float)y1); CommitPen(); MoveToEx(hdc, x1, y1, nullptr); }
        else { MoveToEx(hdc, x0, y0, nullptr); ::LineTo(hdc, x1, y1); }
        cx = x1; cy = y1;
    }
    void Lines(const oapi::FVECTOR2* pt, int n) override
    {
        if (gb) { DWORD c; float w; bool d; if (!PenOn(c, w, d)) return; gb->Prim(); for (int i = 0; i < n; i++) GSeg(pt[i * 2].x, pt[i * 2].y, pt[i * 2 + 1].x, pt[i * 2 + 1].y, c, w, d); return; }
        for (int i = 0; i < n; i++) {
            if (Soft(qpen)) Seg(pt[i * 2].x, pt[i * 2].y, pt[i * 2 + 1].x, pt[i * 2 + 1].y);
            else { MoveToEx(hdc, (int)pt[i * 2].x, (int)pt[i * 2].y, nullptr); ::LineTo(hdc, (int)pt[i * 2 + 1].x, (int)pt[i * 2 + 1].y); }
        }
        if (Soft(qpen)) CommitPen();
    }
    void Rectangle(int x0, int y0, int x1, int y1) override
    {
        if (gb) { GRect(x0, y0, x1, y1); return; }
        if (!Soft(qpen || qbrush)) { ::Rectangle(hdc, x0, y0, x1, y1); return; }
        if (x1 < x0) std::swap(x0, x1);
        if (y1 < y0) std::swap(y0, y1);
        const float px[4] = { (float)x0, (float)x1, (float)x1, (float)x0 }, py[4] = { (float)y0, (float)y0, (float)y1, (float)y1 };
        if (qbrush) {
            if (rot == 0 && scale == 1) FillRectFast(x0, y0, x1, y1);
            else { std::vector<float> p; for (int i = 0; i < 4; i++) { float X, Y; Tr(px[i], py[i], X, Y); p.push_back(X); p.push_back(Y); } g_ras.Begin(surf->w, surf->h); g_ras.PolyFill(p); CommitColour(bcol); }
        } else if (brush) GdiOnly(false, [&] { ::Rectangle(hdc, x0, y0, x1 + 1, y1 + 1); });
        if (qpen) {
            // the outline along the pixel centres of the edge
            const float ex[4] = { x0 + 0.5f, x1 - 0.5f, x1 - 0.5f, x0 + 0.5f }, ey[4] = { y0 + 0.5f, y0 + 0.5f, y1 - 0.5f, y1 - 0.5f };
            for (int i = 0; i < 4; i++) SegC(ex[i], ey[i], ex[(i + 1) & 3], ey[(i + 1) & 3]);
            CommitPen();
        } else if (pen) GdiOnly(true, [&] { ::Rectangle(hdc, x0, y0, x1, y1); });
    }
    void Ellipse(int x0, int y0, int x1, int y1) override
    {
        if (gb) { GEllipse(x0, y0, x1, y1); return; }
        if (!Soft(qpen || qbrush)) { ::Ellipse(hdc, x0, y0, x1, y1); return; }
        const float ecx = (x0 + x1) * 0.5f, ecy = (y0 + y1) * 0.5f, ea = fabsf(x1 - x0) * 0.5f, eb = fabsf(y1 - y0) * 0.5f;
        if (rot != 0) {    // turned: a polygon
            std::vector<float> p; const int N = 48;
            for (int i = 0; i < N; i++) { float X, Y; Tr(ecx + ea * cosf(6.2831853f * i / N), ecy + eb * sinf(6.2831853f * i / N), X, Y); p.push_back(X); p.push_back(Y); }
            g_ras.Begin(surf->w, surf->h);
            if (qbrush) { g_ras.PolyFill(p); CommitColour(bcol); }
            if (qpen) { for (int i = 0; i < N; i++) g_ras.Segment(p[i * 2], p[i * 2 + 1], p[(i + 1) % N * 2], p[(i + 1) % N * 2 + 1], HalfW(), 0); CommitPen(); }
            return;
        }
        PfScope pf(PF_ELLIPSE);
        float X, Y; Tr(ecx, ecy, X, Y);
        g_ras.Begin(surf->w, surf->h);
        if (qbrush) { g_ras.Ellipse(X, Y, ea * scale, eb * scale, -1); CommitColour(bcol); }
        else if (brush) GdiOnly(false, [&] { ::Ellipse(hdc, x0, y0, x1, y1); });
        if (qpen) { g_ras.Ellipse(X, Y, ea * scale - 0.5f, eb * scale - 0.5f, HalfW()); CommitPen(); }
        else if (pen) GdiOnly(true, [&] { ::Ellipse(hdc, x0, y0, x1, y1); });
    }
    void Polygon(const oapi::IVECTOR2* pt, int npt) override
    {
        if (gb) { GPoly(pt, npt, true); return; }
        if (!Soft(qpen || qbrush)) { ::Polygon(hdc, (const POINT*)pt, npt); return; }
        PfScope pf(PF_POLY);
        if (qbrush) {
            std::vector<float> p; for (int i = 0; i < npt; i++) { float X, Y; Tr((float)pt[i].x, (float)pt[i].y, X, Y); p.push_back(X); p.push_back(Y); }
            g_ras.Begin(surf->w, surf->h); g_ras.PolyFill(p); CommitColour(bcol);
        } else if (brush) GdiOnly(false, [&] { ::Polygon(hdc, (const POINT*)pt, npt); });
        if (qpen) { for (int i = 0; i < npt; i++) Seg((float)pt[i].x, (float)pt[i].y, (float)pt[(i + 1) % npt].x, (float)pt[(i + 1) % npt].y); CommitPen(); }
        else if (pen) GdiOnly(true, [&] { ::Polygon(hdc, (const POINT*)pt, npt); });
    }
    void Polyline(const oapi::IVECTOR2* pt, int npt) override
    {
        if (gb) { GPoly(pt, npt, false); return; }
        if (!Soft(qpen)) { ::Polyline(hdc, (const POINT*)pt, npt); return; }
        for (int i = 0; i + 1 < npt; i++) Seg((float)pt[i].x, (float)pt[i].y, (float)pt[i + 1].x, (float)pt[i + 1].y);
        CommitPen();
    }
    void PolyPolygon(const oapi::IVECTOR2* pt, const int* npt, const int n) override
    {
        if (gb) { for (int k = 0; k < n; pt += npt[k], k++) GPoly(pt, npt[k], true); return; }
        if (!Soft(qpen || qbrush)) { ::PolyPolygon(hdc, (const POINT*)pt, npt, n); return; }
        for (int k = 0; k < n; pt += npt[k], k++) Polygon(pt, npt[k]);
    }
    void PolyPolyline(const oapi::IVECTOR2* pt, const int* npt, const int n) override
    {
        if (gb) { for (int k = 0; k < n; pt += npt[k], k++) GPoly(pt, npt[k], false); return; }
        if (!Soft(qpen)) { std::vector<DWORD> c(npt, npt + n); ::PolyPolyline(hdc, (const POINT*)pt, c.data(), (DWORD)n); return; }
        for (int k = 0; k < n; pt += npt[k], k++) for (int i = 0; i + 1 < npt[k]; i++) Seg((float)pt[i].x, (float)pt[i].y, (float)pt[i + 1].x, (float)pt[i + 1].y);
        CommitPen();
    }
    HDC GetDC() override
    {
        if (gb) {     // GDI wanted: the GPU's drawing comes down to the DIB and the rest of the session is drawn there
            gb->End(); delete gb; gb = nullptr;
            surf->cpuPref = true;
            if (!g_client->EnsureDIB(surf)) return nullptr;
            hdc = surf->dc; SaveDC(hdc); ownsDC = true;
            SetBkMode(hdc, bkOpaque ? OPAQUE : TRANSPARENT); ::SetTextAlign(hdc, talign);
            ::SetTextColor(hdc, tcol & 0xFFFFFF); ::SetBkColor(hdc, bkcol & 0xFFFFFF);
            if (font) SelectObject(hdc, ((GFont*)font)->h);
            SelectObject(hdc, pen ? ((GPen*)pen)->h : (HPEN)GetStockObject(NULL_PEN));
            SelectObject(hdc, brush ? ((GBrush*)brush)->h : (HBRUSH)GetStockObject(NULL_BRUSH));
            SetViewportOrgEx(hdc, ox, oy, nullptr);
        }
        return hdc;
    }
    bool ownsDC = false;

    // ---- Sketchpad 2 and 3
    void QuickPen(DWORD color, float width, DWORD style) override
    {
        pen = nullptr;
        qpen = color != 0 && style != 0;
        pcol = Comp(color); pw = std::max(width, 0.5f); pdash = style == 2;
        SelectObject(hdc, GetStockObject(NULL_PEN));
        if (qpenH) { DeleteObject(qpenH); qpenH = nullptr; }
    }
    void QuickBrush(DWORD color) override
    {
        brush = nullptr;
        qbrush = color != 0;
        bcol = Comp(color);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
    }
    void SetBlendState(BlendState s) override { blend = (int)(s & 0xF); filterPoint = (s & 0xF0) == FILTER_POINT; if (!blend) blend = ALPHABLEND; if (gb) gb->SetBlend(blend); }
    void SetWorldTransform2D(float s, float r, const oapi::IVECTOR2* c, const oapi::IVECTOR2* t) override
    {
        scale = s; rot = r; tcx = c ? (float)c->x : 0; tcy = c ? (float)c->y : 0; ttx = t ? (float)t->x : 0; tty = t ? (float)t->y : 0;
    }
    void SetBrightness(const oapi::FVECTOR4* b) override { bright[0] = b ? b->r : 1; bright[1] = b ? b->g : 1; bright[2] = b ? b->b : 1; bright[3] = b ? b->a : 1; }
    void SetRenderParam(RenderParam p, const oapi::FVECTOR4* d) override
    {
        if (p == PRM_GAMMA) { gammaOn = d != nullptr; if (d) { gam[0] = d->r; gam[1] = d->g; gam[2] = d->b; } }
    }
    void SetColorMatrix(const oapi::FMATRIX4*) override {}
    void CopyRect(const SURFHANDLE hSrc, const LPRECT src, int tx, int ty) override
    {
        Surf* s = (Surf*)hSrc; if (!s) return;
        RECT sr = src ? *src : RECT{ 0, 0, (LONG)s->w, (LONG)s->h };
        RECT tr = { tx, ty, tx + (sr.right - sr.left), ty + (sr.bottom - sr.top) };
        StretchRect(hSrc, &sr, &tr);
    }
    void StretchRect(const SURFHANDLE hSrc, const LPRECT src, const LPRECT tgt) override;
    void ColorKey(const SURFHANDLE hSrc, const LPRECT src, int tx, int ty) override
    {
        Surf* s = (Surf*)hSrc; if (!s) return;
        RECT sr = src ? *src : RECT{ 0, 0, (LONG)s->w, (LONG)s->h };
        RECT tr = { tx, ty, tx + (sr.right - sr.left), ty + (sr.bottom - sr.top) };
        if (gb) { GStretch(s, sr, tr, true); return; }
        if (!g_client->EnsureDIB(s) || !surf || !surf->dc) return;
        GdiFlush();
        TransparentBlt(hdc, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top, s->dc, sr.left, sr.top, sr.right - sr.left, sr.bottom - sr.top, RGB((s->ckey >> 16) & 255, (s->ckey >> 8) & 255, s->ckey & 255));
    }

    // ---- the GPU's side
    bool PenOn(DWORD& c, float& w, bool& dash) const
    {
        if (qpen) { c = pcol; w = pw; dash = pdash; return true; }
        if (!pen || ((GPen*)pen)->st == 0) return false;
        const GPen* p = (GPen*)pen; c = Comp(p->cl); w = (float)std::max(1, p->wd); dash = p->st == 2; return true;
    }
    bool BrushOn(DWORD& c) const
    {
        if (qbrush) { c = bcol; return true; }
        if (!brush) return false;
        c = Comp(((GBrush*)brush)->cl); return true;
    }
    void GSeg(float x0, float y0, float x1, float y1, DWORD c, float w, bool dash)
    {
        float X0, Y0, X1, Y1; Tr(x0 + 0.5f, y0 + 0.5f, X0, Y0); Tr(x1 + 0.5f, y1 + 0.5f, X1, Y1);
        gb->Seg(X0, Y0, X1, Y1, w * 0.5f * scale, dash ? std::max(4.0f, 3 * w) : 0, c);
    }
    void GLines(std::initializer_list<float> p, bool closed)
    {
        DWORD c; float w; bool d; if (!PenOn(c, w, d)) return;
        gb->Prim();
        const float* a = p.begin(); const int n = (int)p.size() / 2;
        for (int i = 0; i + 1 < n; i++) GSeg(a[i * 2], a[i * 2 + 1], a[i * 2 + 2], a[i * 2 + 3], c, w, d);
        if (closed && n > 2) GSeg(a[n * 2 - 2], a[n * 2 - 1], a[0], a[1], c, w, d);
    }
    void GFillPoly(const std::vector<float>& P, DWORD c)
    {
        // ear clipping (the polygons here are small); the points already on the surface
        const int n = (int)P.size() / 2;
        if (n < 3) return;
        std::vector<int> idx(n); for (int i = 0; i < n; i++) idx[i] = i;
        double area = 0; for (int i = 0, j = n - 1; i < n; j = i++) area += (double)P[j * 2] * P[i * 2 + 1] - (double)P[i * 2] * P[j * 2 + 1];
        const double sgn = area >= 0 ? 1 : -1;
        auto cross = [&](int a, int b, int d) { return ((double)P[b * 2] - P[a * 2]) * ((double)P[d * 2 + 1] - P[a * 2 + 1]) - ((double)P[b * 2 + 1] - P[a * 2 + 1]) * ((double)P[d * 2] - P[a * 2]); };
        int guard = 0;
        while (idx.size() > 3 && guard++ < 4 * n) {
            bool cut = false;
            for (size_t k = 0; k < idx.size(); k++) {
                int a = idx[(k + idx.size() - 1) % idx.size()], b = idx[k], d = idx[(k + 1) % idx.size()];
                if (cross(a, b, d) * sgn <= 0) continue;
                bool inside = false;
                for (int m : idx) {
                    if (m == a || m == b || m == d) continue;
                    if (cross(a, b, m) * sgn > 0 && cross(b, d, m) * sgn > 0 && cross(d, a, m) * sgn > 0) { inside = true; break; }
                }
                if (inside) continue;
                gb->Tri(P[a * 2], P[a * 2 + 1], P[b * 2], P[b * 2 + 1], P[d * 2], P[d * 2 + 1], c);
                idx.erase(idx.begin() + k); cut = true; break;
            }
            if (!cut) break;
        }
        if (idx.size() == 3) gb->Tri(P[idx[0] * 2], P[idx[0] * 2 + 1], P[idx[1] * 2], P[idx[1] * 2 + 1], P[idx[2] * 2], P[idx[2] * 2 + 1], c);
        else for (size_t k = 1; k + 1 < idx.size(); k++)    // what is left (self-crossing): a fan
            gb->Tri(P[idx[0] * 2], P[idx[0] * 2 + 1], P[idx[k] * 2], P[idx[k] * 2 + 1], P[idx[k + 1] * 2], P[idx[k + 1] * 2 + 1], c);
    }
    void GRect(int x0, int y0, int x1, int y1)
    {
        if (x1 < x0) std::swap(x0, x1);
        if (y1 < y0) std::swap(y0, y1);
        DWORD c; float w; bool d;
        if (BrushOn(c)) {
            float X[4], Y[4];
            Tr((float)x0, (float)y0, X[0], Y[0]); Tr((float)x1, (float)y0, X[1], Y[1]); Tr((float)x1, (float)y1, X[2], Y[2]); Tr((float)x0, (float)y1, X[3], Y[3]);
            gb->Prim(); gb->Tri(X[0], Y[0], X[1], Y[1], X[2], Y[2], c); gb->Tri(X[0], Y[0], X[2], Y[2], X[3], Y[3], c);
        }
        if (PenOn(c, w, d)) {
            gb->Prim();
            const float ex[4] = { (float)x0, (float)x1 - 1, (float)x1 - 1, (float)x0 }, ey[4] = { (float)y0, (float)y0, (float)y1 - 1, (float)y1 - 1 };
            for (int i = 0; i < 4; i++) GSeg(ex[i], ey[i], ex[(i + 1) & 3], ey[(i + 1) & 3], c, w, d);
        }
    }
    void GEllipse(int x0, int y0, int x1, int y1)
    {
        const float ecx = (x0 + x1) * 0.5f, ecy = (y0 + y1) * 0.5f, ea = fabsf((float)(x1 - x0)) * 0.5f, eb = fabsf((float)(y1 - y0)) * 0.5f;
        DWORD c; float w; bool d;
        if (rot != 0) {     // turned: a polygon
            std::vector<float> P; const int N = 48;
            for (int i = 0; i < N; i++) { float X, Y; Tr(ecx + ea * cosf(6.2831853f * i / N), ecy + eb * sinf(6.2831853f * i / N), X, Y); P.push_back(X); P.push_back(Y); }
            if (BrushOn(c)) { gb->Prim(); GFillPoly(P, c); }
            if (PenOn(c, w, d)) { gb->Prim(); for (int i = 0; i < N; i++) gb->Seg(P[i * 2], P[i * 2 + 1], P[(i + 1) % N * 2], P[(i + 1) % N * 2 + 1], w * 0.5f * scale, 0, c); }
            return;
        }
        float X, Y; Tr(ecx, ecy, X, Y);
        if (BrushOn(c)) { gb->Prim(); gb->Ellipse(X, Y, ea * scale, eb * scale, -1, c); }
        if (PenOn(c, w, d)) { gb->Prim(); gb->Ellipse(X, Y, ea * scale - 0.5f, eb * scale - 0.5f, w * 0.5f * scale, c); }
    }
    void GPoly(const oapi::IVECTOR2* pt, int npt, bool closed)
    {
        if (npt < 2) return;
        DWORD c; float w; bool d;
        if (closed && npt >= 3 && BrushOn(c)) {
            std::vector<float> P; for (int i = 0; i < npt; i++) { float X, Y; Tr(pt[i].x + 0.5f, pt[i].y + 0.5f, X, Y); P.push_back(X); P.push_back(Y); }
            gb->Prim(); GFillPoly(P, c);
        }
        if (PenOn(c, w, d)) {
            gb->Prim();
            for (int i = 0; i + 1 < npt; i++) GSeg((float)pt[i].x, (float)pt[i].y, (float)pt[i + 1].x, (float)pt[i + 1].y, c, w, d);
            if (closed && npt > 2) GSeg((float)pt[npt - 1].x, (float)pt[npt - 1].y, (float)pt[0].x, (float)pt[0].y, c, w, d);
        }
    }
    bool GText(int x, int y, const char* a, const wchar_t* wstr, int len)
    {
        if (len <= 0) len = a ? (int)strlen(a) : (int)wcslen(wstr);
        if (len <= 0) return true;
        SIZE sz;
        if (a) GetTextExtentPoint32A(hdc, a, len, &sz); else GetTextExtentPoint32W(hdc, wstr, len, &sz);
        TEXTMETRICA tm; GetTextMetricsA(hdc, &tm);
        const int m = 2 + tm.tmHeight / 4, w = sz.cx + 2 * m, h = sz.cy + 2 * m;
        int tx = x, ty = y;
        if (talign & TA_CENTER) tx -= sz.cx / 2; else if (talign & TA_RIGHT) tx -= sz.cx;
        if ((talign & TA_BASELINE) == TA_BASELINE) ty -= tm.tmAscent; else if (talign & TA_BOTTOM) ty -= sz.cy;
        float X, Y; Tr((float)tx, (float)ty, X, Y);
        if (bkOpaque) { gb->Prim(); gb->Tri(X, Y, X + sz.cx, Y, X + sz.cx, Y + sz.cy, bkcol); gb->Tri(X, Y, X + sz.cx, Y + sz.cy, X, Y + sz.cy, bkcol); }
        if (!g_txt.Ensure(w, h)) return false;
        for (int j = 0; j < h; j++) memset(g_txt.bits + (size_t)j * g_txt.w, 0, w * 4);
        ::SetTextColor(hdc, 0xFFFFFF);
        if (a) TextOutA(hdc, m, m, a, len); else TextOutW(hdc, m, m, wstr, len);
        GdiFlush();
        gb->Prim();
        gb->Text(floorf(X) - m, floorf(Y) - m, w, h, g_txt.bits, g_txt.w * 4, tcol);
        return true;
    }
    void GStretch(Surf* s, const RECT& sr, const RECT& tr, bool keyed)
    {
        if (s == surf) return;    // drawing a surface into itself: not on the GPU
        gb->SetSource(s);
        if (keyed) gb->SetKey(s->ckey);
        gb->SetGamma(gammaOn, gam);
        float X[4], Y[4];
        Tr((float)tr.left, (float)tr.top, X[0], Y[0]); Tr((float)tr.right, (float)tr.top, X[1], Y[1]);
        Tr((float)tr.right, (float)tr.bottom, X[2], Y[2]); Tr((float)tr.left, (float)tr.bottom, X[3], Y[3]);
        const float u0 = sr.left / (float)s->w, v0 = sr.top / (float)s->h, u1 = sr.right / (float)s->w, v1 = sr.bottom / (float)s->h;
        const float xy[8] = { X[0], Y[0], X[1], Y[1], X[2], Y[2], X[3], Y[3] }, uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
        const bool srcAlpha = (s->attrib & OAPISURFACE_ALPHA) || !s->name.empty();
        gb->Prim();
        gb->Quad(xy, uv, 0xFFFFFFFF, bright, keyed ? 5 : 3, filterPoint, !srcAlpha);
    }

    HDC hdc;
    Surf* surf; bool isScreen;
    oapi::Font* font = nullptr; oapi::Pen* pen = nullptr; oapi::Brush* brush = nullptr;
    int ox = 0, oy = 0, cx = 0, cy = 0;
    bool qpen = false, qbrush = false, pdash = false; DWORD pcol = 0, bcol = 0; float pw = 1; HPEN qpenH = nullptr;
    int blend = ALPHABLEND; bool filterPoint = false;
    float scale = 1, rot = 0, tcx = 0, tcy = 0, ttx = 0, tty = 0;
    float bright[4] = { 1, 1, 1, 1 }; bool gammaOn = false; float gam[3] = { 1, 1, 1 };

    // Sketchpad colours with no alpha are opaque (D3D9Client's colour compatibility)
    static DWORD Comp(DWORD c) { return (c & 0xFF000000) ? c : (c | 0xFF000000); }
    bool Soft(bool quick) const { return surf && surf->bits && (quick || rot != 0 || scale != 1); }
    float HalfW() const { return pw * 0.5f * scale; }
    // the 2D world transform (D3DX's affine 2D: scaled, turned about the centre, moved), then the origin
    void Tr(float x, float y, float& X, float& Y) const
    {
        float sx = x * scale - tcx, sy = y * scale - tcy;
        const float c = cosf(rot), s = sinf(rot);
        X = sx * c - sy * s + tcx + ttx + ox; Y = sx * s + sy * c + tcy + tty + oy;
    }
    // a line between pixel centres (the integer coordinates name pixels)
    void Seg(float x0, float y0, float x1, float y1) { SegC(x0 + 0.5f, y0 + 0.5f, x1 + 0.5f, y1 + 0.5f); }
    void SegC(float x0, float y0, float x1, float y1)
    {
        PfScope pf(PF_LINE);
        float X0, Y0, X1, Y1; Tr(x0, y0, X0, Y0); Tr(x1, y1, X1, Y1);
        g_ras.Begin(surf->w, surf->h);
        g_ras.Segment(X0, Y0, X1, Y1, HalfW(), pdash ? std::max(4.0f, 3 * pw) : 0);
    }
    void CommitPen() { PfScope pf(PF_LINE); CommitColour(pcol); }
    void CommitColour(DWORD c)
    {
        const float r = (float)(c & 255), g = (float)((c >> 8) & 255), b = (float)((c >> 16) & 255), a = (float)(c >> 24);
        RECT bb = { INT_MAX, INT_MAX, -1, -1 };
        g_ras.Commit([&](int x, int y, float cov) {
            Blend((BYTE*)surf->bits + ((size_t)y * surf->w + x) * 4, r, g, b, a, cov);
            bb.left = std::min<LONG>(bb.left, x); bb.right = std::max<LONG>(bb.right, x + 1); bb.top = std::min<LONG>(bb.top, y); bb.bottom = std::max<LONG>(bb.bottom, y + 1);
        });
        Touched(bb);
    }
    void FillRectFast(int x0, int y0, int x1, int y1)
    {
        PfScope pf(PF_FILL);
        RECT bb = { std::max(0, x0 + ox), std::max(0, y0 + oy), std::min((int)surf->w, x1 + ox), std::min((int)surf->h, y1 + oy) };
        if (bb.right <= bb.left || bb.bottom <= bb.top) return;
        const float r = (float)(bcol & 255), g = (float)((bcol >> 8) & 255), b = (float)((bcol >> 16) & 255), a = (float)(bcol >> 24);
        for (LONG y = bb.top; y < bb.bottom; y++) {
            BYTE* row = (BYTE*)surf->bits + ((size_t)y * surf->w) * 4;
            for (LONG x = bb.left; x < bb.right; x++) Blend(row + x * 4, r, g, b, a, 1);
        }
        Touched(bb);
    }
    // the drawn area joins the DC's bounds (the screen layer sends only that)
    void Touched(const RECT& bb) { if (bb.right > bb.left) { RECT r = { bb.left - ox, bb.top - oy, bb.right - ox, bb.bottom - oy }; SetBoundsRect(hdc, &r, DCB_ACCUMULATE); } }
    // GDI draws only the fill (pen off) or only the outline (brush off)
    template<class F> void GdiOnly(bool outline, F f)
    {
        HGDIOBJ op = SelectObject(hdc, outline ? (pen ? ((GPen*)pen)->h : GetStockObject(NULL_PEN)) : GetStockObject(NULL_PEN));
        HGDIOBJ ob = SelectObject(hdc, outline ? GetStockObject(NULL_BRUSH) : (brush ? ((GBrush*)brush)->h : GetStockObject(NULL_BRUSH)));
        f();
        SelectObject(hdc, op); SelectObject(hdc, ob);
    }
    // one pixel (B, G, R, A in the DIB) under the blend state. Off the screen layer an alpha of 0 is kept as 1 (GDI
    // writes 0 where it draws, made opaque later); on the screen layer the key colour is "nothing", alpha 0 opaque (GDI)
    inline void Blend(BYTE* p, float r, float g, float b, float a, float cov) const
    {
        const bool key = isScreen && ((*(DWORD*)p) & 0xFFFFFF) == SCREEN_KEY;
        switch (blend) {
        case COPY:
            if (cov < 0.5f) return;
            if (isScreen && a < 1) { *(DWORD*)p = SCREEN_KEY; return; }
            p[0] = (BYTE)b; p[1] = (BYTE)g; p[2] = (BYTE)r; p[3] = isScreen ? (a >= 255 ? 0 : (BYTE)a) : (BYTE)std::max(1.0f, a);
            return;
        case COPY_ALPHA:
            if (cov < 0.5f || isScreen) return;
            p[3] = (BYTE)std::max(1.0f, a); return;
        case COPY_COLOR:
            if (cov < 0.5f) return;
            p[0] = (BYTE)b; p[1] = (BYTE)g; p[2] = (BYTE)r;
            if (key) p[3] = 0;
            return;
        default: {
            const float k = a * cov * (1.0f / 255);
            if (k <= 0) return;
            if (key) {      // over nothing: the colour with its alpha
                p[0] = (BYTE)b; p[1] = (BYTE)g; p[2] = (BYTE)r;
                const float na = k * 255; p[3] = na >= 254.5f ? 0 : (BYTE)std::max(1.0f, na);
                return;
            }
            p[0] = (BYTE)(p[0] + (b - p[0]) * k + 0.5f); p[1] = (BYTE)(p[1] + (g - p[1]) * k + 0.5f); p[2] = (BYTE)(p[2] + (r - p[2]) * k + 0.5f);
            if (isScreen && p[3]) { const float da = p[3] / 255.0f, na = (k + da * (1 - k)) * 255; p[3] = na >= 254.5f ? 0 : (BYTE)na; }
        }
        }
    }
};

// a surface (or part) drawn into the rectangle, through the world transform, with the brightness and gamma; the source
// filtered linearly (or by point), blended by the blend state
void GPad::StretchRect(const SURFHANDLE hSrc, const LPRECT src, const LPRECT tgt)
{
    Surf* s = (Surf*)hSrc;
    if (gb && s) {
        RECT sr = src ? *src : RECT{ 0, 0, (LONG)s->w, (LONG)s->h };
        RECT tr = tgt ? *tgt : RECT{ 0, 0, (LONG)surf->w, (LONG)surf->h };
        GStretch(s, sr, tr, false);
        return;
    }
    if (!s || !surf || !surf->bits || !g_client || !g_client->EnsureDIB(s)) return;
    PfScope pf(PF_STRETCH);
    GdiFlush();
    RECT sr = src ? *src : RECT{ 0, 0, (LONG)s->w, (LONG)s->h };
    RECT tr = tgt ? *tgt : RECT{ 0, 0, (LONG)surf->w, (LONG)surf->h };
    const float tw = (float)(tr.right - tr.left), th = (float)(tr.bottom - tr.top);
    if (tw == 0 || th == 0 || sr.right <= sr.left || sr.bottom <= sr.top) return;
    // the target's corners on the surface, and the way back
    float X[4], Y[4];
    Tr((float)tr.left, (float)tr.top, X[0], Y[0]); Tr((float)tr.right, (float)tr.top, X[1], Y[1]);
    Tr((float)tr.right, (float)tr.bottom, X[2], Y[2]); Tr((float)tr.left, (float)tr.bottom, X[3], Y[3]);
    const float ux = (X[1] - X[0]) / tw, uy = (Y[1] - Y[0]) / tw, vx = (X[3] - X[0]) / th, vy = (Y[3] - Y[0]) / th;
    const float det = ux * vy - uy * vx;
    if (fabsf(det) < 1e-12f) return;
    int x0 = (int)floorf(std::min({ X[0], X[1], X[2], X[3] })), x1 = (int)ceilf(std::max({ X[0], X[1], X[2], X[3] }));
    int y0 = (int)floorf(std::min({ Y[0], Y[1], Y[2], Y[3] })), y1 = (int)ceilf(std::max({ Y[0], Y[1], Y[2], Y[3] }));
    x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, (int)surf->w); y1 = std::min(y1, (int)surf->h);
    const float sw = (float)(sr.right - sr.left), sh = (float)(sr.bottom - sr.top);
    const bool srcAlpha = (s->attrib & OAPISURFACE_ALPHA) || !s->name.empty();
    const BYTE* sb = (const BYTE*)s->bits;
    auto texel = [&](int x, int y, float* o) {
        x = std::clamp(x, (int)sr.left, (int)sr.right - 1); y = std::clamp(y, (int)sr.top, (int)sr.bottom - 1);
        const BYTE* q = sb + ((size_t)y * s->w + x) * 4;
        o[0] = q[2]; o[1] = q[1]; o[2] = q[0]; o[3] = srcAlpha ? (q[3] <= 1 ? 0.0f : q[3]) : 255.0f;
    };
    RECT bb = { INT_MAX, INT_MAX, -1, -1 };
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        const float dx = x + 0.5f - X[0], dy = y + 0.5f - Y[0];
        const float u = (dx * vy - dy * vx) / det, v = (ux * dy - uy * dx) / det;     // in target units from the corner
        if (u < 0 || v < 0 || u >= tw || v >= th) continue;
        const float su = sr.left + u * sw / tw, sv = sr.top + v * sh / th;
        float c[4];
        if (filterPoint) texel((int)su, (int)sv, c);
        else {
            const float fx = su - 0.5f, fy = sv - 0.5f; const int ix = (int)floorf(fx), iy = (int)floorf(fy); const float ax = fx - ix, ay = fy - iy;
            float a[4], b[4], d[4], e[4]; texel(ix, iy, a); texel(ix + 1, iy, b); texel(ix, iy + 1, d); texel(ix + 1, iy + 1, e);
            // weighted by alpha (no dark fringes from the transparent texels' colour)
            float w0 = (1 - ax) * (1 - ay) * a[3], w1 = ax * (1 - ay) * b[3], w2 = (1 - ax) * ay * d[3], w3 = ax * ay * e[3], ws = w0 + w1 + w2 + w3;
            c[3] = (1 - ax) * (1 - ay) * a[3] + ax * (1 - ay) * b[3] + (1 - ax) * ay * d[3] + ax * ay * e[3];
            for (int k = 0; k < 3; k++) c[k] = ws > 0 ? (a[k] * w0 + b[k] * w1 + d[k] * w2 + e[k] * w3) / ws : a[k];
        }
        for (int k = 0; k < 3; k++) {
            float v2 = c[k] * bright[k];
            if (gammaOn) v2 = 255 * powf(std::clamp(v2 / 255, 0.0f, 1.0f), gam[k]);
            c[k] = std::clamp(v2, 0.0f, 255.0f);
        }
        c[3] = std::clamp(c[3] * bright[3], 0.0f, 255.0f);
        Blend((BYTE*)surf->bits + ((size_t)y * surf->w + x) * 4, c[0], c[1], c[2], c[3], 1);
        bb.left = std::min<LONG>(bb.left, x); bb.right = std::max<LONG>(bb.right, x + 1); bb.top = std::min<LONG>(bb.top, y); bb.bottom = std::max<LONG>(bb.bottom, y + 1);
    }
    Touched(bb);
}

}  // namespace


// ------------------------------------------------------------------------------------------ CPU side of surfaces
bool D3D11Client::EnsureDIB(Surf* s)
{
    if (s->dc && s->gpuNewer && shadersOk) {     // a module's shader wrote it: its pixels come back
        Readback(s); s->gpuNewer = false;
        BYTE* p = (BYTE*)s->bits;
        for (size_t i = 3; i < (size_t)s->w * s->h * 4; i += 4) if (!p[i]) p[i] = 1;
        return true;
    }
    if (s->dc) return true;
    if (!s->w || !s->h) return false;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = (LONG)s->w; bi.bmiHeader.biHeight = -(LONG)s->h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    s->dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &s->bits, nullptr, 0);
    if (!s->dib) return false;
    { extern long g_dibLive; extern double g_dibMB; g_dibLive++; g_dibMB += s->w * (double)s->h * 4 / 1048576.0; }
    s->dc = CreateCompatibleDC(nullptr);
    s->oldBmp = SelectObject(s->dc, s->dib);
    memset(s->bits, 0, (size_t)s->w * s->h * 4);
    // a surface with GPU content (loaded from a file): its pixels come back once (rendered through a target, so
    // compressed formats decode the same way the shaders see them)
    if (s->srv && (s->immutable || s->gpuNewer) && shadersOk) Readback(s);
    s->gpuNewer = false;
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
    PfScope pf(PF_FIXALPHA);
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
    { static std::set<Surf*> seen; if (!s->immutable && seen.insert(s).second) LOGC("diag 2D: readback of %ux%u attrib 0x%X cpuPref %d", s->w, s->h, s->attrib, s->cpuPref); }
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
    if (dib) {
        extern long g_dibLive; extern double g_dibMB; g_dibLive--; g_dibMB -= w * (double)h * 4 / 1048576.0;
        DeleteObject(dib); dib = nullptr; bits = nullptr;
    }
}

void D3D11Client::MarkDirty(Surf* s) { if (s && s != screen) { FixGdiAlpha(s); dirty.insert(s); } }

// changed surfaces to their textures (once per frame, before they are drawn)
void D3D11Client::FlushDirty()
{
    for (Surf* s : dirty) UploadSurface(s);
    dirty.clear();
    Prof2DTick();
}

void D3D11Client::UploadSurface(Surf* s)
{
    if (!s->dc || !s->bits) return;
    PfScope pf(PF_UPLOAD);
    { auto& u = g_upl[s]; u.n++; u.w = s->w; u.h = s->h; u.attrib = s->attrib; u.name = s->name; }
    GdiFlush();
    // surfaces made without an alpha channel are opaque; the others keep theirs (file textures, OAPISURFACE_ALPHA)
    BYTE* p = (BYTE*)s->bits;
    if (!(s->attrib & OAPISURFACE_ALPHA) && s->name.empty()) for (size_t i = 3; i < (size_t)s->w * s->h * 4; i += 4) p[i] = 255;
    if (!s->tex || s->immutable) {
        D3D11_TEXTURE2D_DESC td = { s->w, s->h, 1, 1, DXGI_FORMAT_B8G8R8A8_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT,
                                    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0 };
        s->tex.Reset(); s->srv.Reset(); s->srvLin.Reset(); s->rtvLin.Reset();
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &s->tex))) return;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(s->tex.Get(), &sd, &s->srv);
        s->immutable = false;
    }
    ctx->UpdateSubresource(s->tex.Get(), 0, nullptr, s->bits, s->w * 4, 0);
    s->gpuNewer = false;
}

bool D3D11Client::SurfToGpu(Surf* s)
{
    if (!s) return false;
    if (dirty.count(s)) { UploadSurface(s); dirty.erase(s); }
    return SurfGpuViews(s) && s->srv;
}

// the gamma-space (UNORM) views of a surface for the modules' shaders; a file texture keeps its own view (read only)
bool D3D11Client::SurfGpuViews(Surf* s)
{
    if (!s) return false;
    if (s->srvLin || s->immutable) return true;
    if (!s->tex) {
        D3D11_TEXTURE2D_DESC td = { s->w, s->h, 1, 1, DXGI_FORMAT_B8G8R8A8_TYPELESS, {1, 0}, D3D11_USAGE_DEFAULT,
                                    D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0 };
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &s->tex))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(s->tex.Get(), &sd, &s->srv);
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {}; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
    if (FAILED(dev->CreateShaderResourceView(s->tex.Get(), &sd, &s->srvLin))) return false;
    D3D11_RENDER_TARGET_VIEW_DESC rd = {}; rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    dev->CreateRenderTargetView(s->tex.Get(), &rd, &s->rtvLin);
    return true;
}

Surf* D3D11Client::Target(SURFHANDLE h) const { return h ? (Surf*)h : screen; }

// ------------------------------------------------------------------------------------------ Sketchpad, tools, DC
oapi::Sketchpad* D3D11Client::clbkGetSketchpad(SURFHANDLE surf)
{
    Surf* s = Target(surf);
    if (s && GpuSurface(s)) {
        SkBatch* b = new SkBatch;
        if (b->Begin(s)) { SaveDC(g_txt.Get()); return new GPad(surf, s, b); }
        delete b;
    }
    if (!s || !EnsureDIB(s)) return nullptr;
    SaveDC(s->dc);
    return new GPad(surf, s, s == screen, s->dc);
}

void D3D11Client::clbkReleaseSketchpad(oapi::Sketchpad* sp)
{
    if (!sp) return;
    GPad* g = (GPad*)sp;
    Surf* s = Target(g->GetSurface());
    if (g->gb) {
        g->gb->End();
        if (DiagFrame()) {     // diagnostics (test runs): each surface drawn on the GPU this frame, once
            static std::set<Surf*> dumped;
            if (s && dumped.insert(s).second && dumped.size() <= 8) {
                char p[128]; sprintf_s(p, "Modules/D3D11Client/shots/pad_%ux%u_%X.bmp", s->w, s->h, s->attrib); DumpSurface(s, p);
            }
        }
        RestoreDC(g_txt.Get(), -1);
        delete g;
        return;
    }
    if (g->ownsDC) RestoreDC(g_txt.Get(), -1);       // it started on the GPU
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

// a surface drawn on the GPU: made as a render target or for Sketchpads, not a file texture, and GDI has not asked for it (then it stays
// on its DIB, so the two don't trade the picture back and forth every frame)
bool D3D11Client::GpuSurface(Surf* s) const
{
    return s && s != screen && shadersOk && !s->immutable && !s->cpuPref && (s->attrib & (OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD));
}

HDC D3D11Client::clbkGetSurfaceDC(SURFHANDLE surf)
{
    Surf* s = Target(surf);
    if (s) s->cpuPref = true;
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
    if (!s) return false;
    if (me->GpuSurface(s) && (s->gpuNewer || !s->dc) && me->SurfToGpu(s) && s->rtvLin) {    // the GPU holds it: cleared there
        float a = (s->attrib & OAPISURFACE_ALPHA) ? (col >> 24) / 255.0f : 1.0f;
        const float c[4] = { ((col >> 16) & 255) / 255.0f, ((col >> 8) & 255) / 255.0f, (col & 255) / 255.0f, a };
        if (tx == 0 && ty == 0 && w >= s->w && h >= s->h) me->ctx->ClearRenderTargetView(s->rtvLin.Get(), c);
        else {
            ComPtr<ID3D11DeviceContext1> c1;
            if (SUCCEEDED(me->ctx.As(&c1))) { D3D11_RECT r = { (LONG)tx, (LONG)ty, (LONG)(tx + w), (LONG)(ty + h) }; c1->ClearView(s->rtvLin.Get(), c, &r, 1); }
        }
        s->gpuNewer = true; me->dirty.erase(s);
        return true;
    }
    if (!me->EnsureDIB(s)) return false;
    if (s != screen && (s->attrib & OAPISURFACE_RENDERTARGET)) {   // diagnostics: a render target filled on the CPU (once per surface)
        static std::set<Surf*> seen;
        if (seen.insert(s).second) LOGC("diag 2D: CPU fill of render target %ux%u attrib 0x%X gpuNewer %d dib %d cpuPref %d", s->w, s->h, s->attrib, s->gpuNewer, s->dc != nullptr, s->cpuPref);
    }
    if (s != screen) {     // straight into the DIB: fast, and an alpha surface gets the colour's alpha (0: transparent, kept as 1)
        PfScope pf(PF_FILL);
        GdiFlush();
        const UINT x0 = std::min<UINT>(tx, s->w), y0 = std::min<UINT>(ty, s->h), x1 = std::min<UINT>(tx + w, s->w), y1 = std::min<UINT>(ty + h, s->h);
        DWORD a = col >> 24;
        if (!(s->attrib & OAPISURFACE_ALPHA) && s->name.empty()) a = 255; else if (a == 0) a = 1;
        const DWORD v = (col & 0xFFFFFF) | (a << 24);
        for (UINT y = y0; y < y1; y++) std::fill((DWORD*)s->bits + (size_t)y * s->w + x0, (DWORD*)s->bits + (size_t)y * s->w + x1, v);
        me->dirty.insert(s);
        return true;
    }
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
    if (!t || !s) return false;
    // a target the GPU holds: the blit is drawn there (the source goes up if its DIB is newer)
    if (t != s && GpuSurface(t) && (t->gpuNewer || !t->dc)) {
        SkBatch b;
        if (b.Begin(t)) {
            const bool keyed = (flag & BLT_SRCCOLORKEY) && s->hasKey;
            b.SetBlend(2);       // a blit copies
            b.SetSource(s);
            if (keyed) b.SetKey(s->ckey);
            const float xy[8] = { (float)tx, (float)ty, (float)(tx + tw), (float)ty, (float)(tx + tw), (float)(ty + th), (float)tx, (float)(ty + th) };
            const float u0 = sx / (float)s->w, v0 = sy / (float)s->h, u1 = (sx + sw) / (float)s->w, v1 = (sy + sh) / (float)s->h;
            const float uv[8] = { u0, v0, u1, v0, u1, v1, u0, v1 };
            const bool srcAlpha = (s->attrib & OAPISURFACE_ALPHA) || !s->name.empty();
            b.Prim();
            b.Quad(xy, uv, 0xFFFFFFFF, nullptr, keyed ? 5 : 3, tw == sw && th == sh, !srcAlpha);
            b.End();
            return true;
        }
    }
    if (!EnsureDIB(t) || !EnsureDIB(s)) return false;
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
            for (UINT x = 0; x < uw; x++) {     // alpha 0 (GDI) or 255: opaque; between: drawn with alpha (the Sketchpad 2 colours)
                DWORD c = src[x] & 0xFFFFFF, a = src[x] >> 24;
                dst[x] = c == KEY ? 0 : (a == 0 ? (c | 0xFF000000) : src[x]);
            }
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
        // the group's texture: TEXIDX_MFD0 and above name an MFD's surface, the rest index hSurf (or the mesh's own
        // textures when there is no hSurf)
        Surf* s = nullptr;
        if (grp->TexIdx >= TEXIDX_MFD0) s = (Surf*)GetMFDSurface((int)(grp->TexIdx - TEXIDX_MFD0));
        else if (hSurf) s = (Surf*)hSurf[grp->TexIdx];
        else s = (Surf*)oapiGetTextureHandle(hMesh, grp->TexIdx + 1);
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
