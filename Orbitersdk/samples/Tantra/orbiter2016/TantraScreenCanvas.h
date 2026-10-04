// TantraScreenCanvas: drawing in the pixels of an HTML mockup (Tantra_Design/*.html, a 2D canvas) on an Orbiter sketchpad -
// the helpers the mockups' code uses (fillRect, strokeRect, lines, arcs, T() text, frames, bars, buttons), the palette of the
// plant screen (the user's reference) and the ru-RU number format. Shared by the mechanisation, thermal and engine screens
// (TantraMechScreen, TantraThermalScreen, TantraEngineScreen) and the plant screen; the text: TantraScreenFont (Segoe UI).
#pragma once
#include "orbitersdk.h"
#include "SkpCompat.h"
#include "TantraScreenFont.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace tantra::scr {

constexpr double kPi = 3.14159265358979323846;

// the mockups' palette (CSS 0xRRGGBB): COL.bg fr tx dim or ye rd gr wh bl vi
constexpr unsigned cBg = 0x0a1311, cFr = 0x1f3a35, cTx = 0x7fe0d0, cDim = 0x4f8f86, cOr = 0xee9a3a, cYe = 0xe8d35a, cRd = 0xff5a4a,
                   cGr = 0x5ad0a0, cWh = 0xe8fffa, cBl = 0x7fb8ff, cVi = 0xb59cff;

inline DWORD Skp(unsigned rgb, double a = 1.0) {   // 0xRRGGBB + alpha -> the sketchpad's 0xAABBGGRR
    const unsigned r = (rgb >> 16) & 255u, g = (rgb >> 8) & 255u, b = rgb & 255u;
    long A = std::lround((std::max)(0.0, (std::min)(1.0, a)) * 255.0);
    if (A < 1) A = 1;
    return (DWORD(A) << 24) | (b << 16) | (g << 8) | r;
}
inline unsigned Mix(unsigned c, unsigned bg, double a) {   // c over bg at alpha a (the canvas' globalAlpha)
    a = (std::max)(0.0, (std::min)(1.0, a));
    auto ch = [&](int s) { return unsigned(std::lround(((c >> s) & 255u) * a + ((bg >> s) & 255u) * (1.0 - a))) << s; };
    return ch(16) | ch(8) | ch(0);
}
inline unsigned Rgb(int r, int g, int b) { return (unsigned(r & 255) << 16) | (unsigned(g & 255) << 8) | unsigned(b & 255); }

// numbers as the mockups' toLocaleString("ru-RU"): a decimal comma, the thousands by a no-break space
inline std::wstring Fmt(double v, int d) {
    if (!std::isfinite(v)) return L"—";
    wchar_t b[64];
    std::swprintf(b, 64, L"%.*f", d, std::fabs(v));
    std::wstring s(b), ip = s, fp;
    const size_t dot = s.find(L'.');
    if (dot != std::wstring::npos) { ip = s.substr(0, dot); fp = s.substr(dot + 1); }
    std::wstring g;
    const int n = int(ip.size());
    for (int i = 0; i < n; ++i) { g += ip[i]; const int rest = n - 1 - i; if (rest > 0 && rest % 3 == 0) g += L' '; }
    bool neg = false;
    if (v < 0) for (wchar_t c : s) if (c >= L'1' && c <= L'9') neg = true;
    return (neg ? L"-" : L"") + g + (fp.empty() ? L"" : L"," + fp);
}
inline std::wstring W1251(const char* s) {   // the ship's strings are in the program's charset
    if (!s || !*s) return std::wstring();
    const int n = MultiByteToWideChar(1251, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(1251, 0, s, -1, &w[0], n);
    return w;
}

struct Pt { double x, y; };

// the canvas: the mockup's pixels at an offset on the surface. Orbiter 2024's D3D9 client draws only with pen / brush / font
// objects on these surfaces (no QuickPen / QuickBrush, no alpha): the objects come from the ScreenFont, a translucent colour
// is mixed over the screen's ground.
class Canvas {
public:
    Canvas(oapi::Sketchpad* s, ScreenFont& f, int ox, int oy) : s_(s), f_(f), ox_(ox), oy_(oy) { f_.BeginPass(); }
    ~Canvas() { s_->SetPen(nullptr); s_->SetBrush(nullptr); }
    int X(double x) const { return int(std::lround(x)) + ox_; }
    int Y(double y) const { return int(std::lround(y)) + oy_; }
    oapi::Sketchpad* Skp() const { return s_; }
    static unsigned A(unsigned c, double a) { return a >= 0.999 ? c : Mix(c, cBg, a); }
    void Brush(unsigned c, double a) { s_->SetBrush(f_.Brush(A(c, a))); }
    void NoBrush() { s_->SetBrush(nullptr); }
    void Pen(unsigned c, double lw, double a = 1.0) { s_->SetPen(f_.Pen(A(c, a), (std::max)(1, int(std::lround(lw))))); }
    void NoPen() { s_->SetPen(nullptr); }
    void Fill(double x, double y, double w, double h, unsigned c, double a = 1.0) {
        if (w < 0) { x += w; w = -w; }
        if (h < 0) { y += h; h = -h; }
        if (w < 0.5 || h < 0.5) return;
        NoPen(); Brush(c, a);
        s_->Rectangle(X(x), Y(y), X(x + w), Y(y + h));
    }
    void Stroke(double x, double y, double w, double h, unsigned c, double lw, double a = 1.0) {
        NoBrush(); Pen(c, lw, a);
        s_->Rectangle(X(x), Y(y), X(x + w), Y(y + h));
    }
    void Line(double x0, double y0, double x1, double y1, unsigned c, double lw, double a = 1.0) {
        NoBrush(); Pen(c, lw, a);
        s_->Line(X(x0), Y(y0), X(x1), Y(y1));
    }
    void Dashed(double x0, double y0, double x1, double y1, unsigned c, double lw, double on, double off, double a = 1.0) {
        const double L = std::hypot(x1 - x0, y1 - y0);
        if (L < 1e-6) return;
        const double ux = (x1 - x0) / L, uy = (y1 - y0) / L;
        for (double s = 0; s < L; s += on + off) { const double e = (std::min)(L, s + on); Line(x0 + ux * s, y0 + uy * s, x0 + ux * e, y0 + uy * e, c, lw, a); }
    }
    void Polyline(const std::vector<Pt>& p, unsigned c, double lw, double a = 1.0) {
        if (p.size() < 2) return;
        std::vector<oapi::IVECTOR2> v(p.size());
        for (size_t i = 0; i < p.size(); ++i) { v[i].x = X(p[i].x); v[i].y = Y(p[i].y); }
        NoBrush(); Pen(c, lw, a);
        s_->Polyline(v.data(), int(v.size()));
    }
    void Shape(const std::vector<Pt>& p, unsigned fill, double a = 1.0, unsigned edge = 0, double lw = 0.0) {   // a convex polygon
        if (p.size() < 3) return;
        std::vector<oapi::IVECTOR2> v(p.size());
        for (size_t i = 0; i < p.size(); ++i) { v[i].x = X(p[i].x); v[i].y = Y(p[i].y); }
        Brush(fill, a);
        if (lw > 0) Pen(edge, lw); else NoPen();
        s_->Polygon(v.data(), int(v.size()));
    }
    void Disc(double x, double y, double r, unsigned c, double a = 1.0) {
        NoPen(); Brush(c, a);
        s_->Ellipse(X(x - r), Y(y - r), X(x + r), Y(y + r));
    }
    void Ellipse(double cx, double cy, double rx, double ry, unsigned fill, unsigned edge, double lw, double fa = 1.0) {
        if (fill) Brush(fill, fa); else NoBrush();
        if (lw > 0) Pen(edge, lw); else NoPen();
        s_->Ellipse(X(cx - rx), Y(cy - ry), X(cx + rx), Y(cy + ry));
    }
    void Circle(double x, double y, double r, unsigned c, double lw, double a = 1.0) {
        NoBrush(); Pen(c, lw, a);
        s_->Ellipse(X(x - r), Y(y - r), X(x + r), Y(y + r));
    }
    // an arc from a0 to a1 (canvas angles: 0 to the right, clockwise in screen y), as a thick polyline
    void Arc(double cx, double cy, double r, double a0, double a1, unsigned c, double lw, double a = 1.0) {
        const int n = (std::max)(4, int(std::fabs(a1 - a0) / (2 * kPi) * 64));
        std::vector<Pt> p;
        for (int i = 0; i <= n; ++i) { const double t = a0 + (a1 - a0) * i / n; p.push_back({cx + r * std::cos(t), cy + r * std::sin(t)}); }
        Polyline(p, c, lw, a);
    }
    void Arrow(double x0, double y0, double x1, double y1, unsigned c, double lw) {
        Line(x0, y0, x1, y1, c, lw);
        const double a = std::atan2(y1 - y0, x1 - x0), L = 10 + lw * 2;
        Shape({{x1, y1}, {x1 - L * std::cos(a - 0.4), y1 - L * std::sin(a - 0.4)}, {x1 - L * std::cos(a + 0.4), y1 - L * std::sin(a + 0.4)}}, c);
    }
    // a thick polyline (pipes): quads, square into the joints as the canvas' miter
    void Thick(const std::vector<Pt>& p, double w, unsigned c, double a = 1.0) {
        for (size_t i = 1; i < p.size(); ++i) {
            double ax = p[i - 1].x, ay = p[i - 1].y, bx = p[i].x, by = p[i].y;
            const double dx = bx - ax, dy = by - ay, L = std::hypot(dx, dy);
            if (L < 1e-6) continue;
            const double ux = dx / L, uy = dy / L, nx = -uy * w / 2, ny = ux * w / 2;
            const double e0 = i > 1 ? w / 2 : 0.0, e1 = i + 1 < p.size() ? w / 2 : 0.0;
            ax -= ux * e0; ay -= uy * e0; bx += ux * e1; by += uy * e1;
            Shape({{ax + nx, ay + ny}, {bx + nx, by + ny}, {bx - nx, by - ny}, {ax - nx, ay - ny}}, c, a);
        }
    }
    double Width(const std::wstring& s, int size, int weight) { return f_.Width(s, f_.Spec(size, weight)); }
    // text as the mockup's T(): (x, baseline y), align 0 left / 1 centre / 2 right
    double T(const std::wstring& s, double x, double y, unsigned col, int size = 15, int align = 0, int weight = 500, double a = 1.0) {
        if (s.empty()) return 0.0;
        f_.Text(s_, X(x), Y(y), s, size, weight, A(col, a), align);
        return Width(s, size, weight);
    }
    // the longest head of s that fits w (with an ellipsis when cut)
    std::wstring Fit(const std::wstring& s, double w, int size, int weight = 500) {
        const int sp = f_.Spec(size, weight);
        if (f_.Width(s, sp) <= w) return s;
        std::wstring t = s;
        while (!t.empty() && f_.Width(t + L"…", sp) > w) t.pop_back();
        return t + L"…";
    }

private:
    oapi::Sketchpad* s_;
    ScreenFont& f_;
    int ox_, oy_;
};

// a touch area of a screen (its own pixels) and the command it gives; along: where along the area (0 at the bottom / left)
struct Hit { double x, y, w, h; int cmd; bool vertical = false; };
inline int HitTest(const std::vector<Hit>& hits, double x, double y, double* along, double margin = 3.0) {
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        const Hit& a = *it;
        if (x >= a.x - margin && x <= a.x + a.w + margin && y >= a.y - margin && y <= a.y + a.h + margin) {
            if (along) *along = a.vertical ? (std::max)(0.0, (std::min)(1.0, (a.y + a.h - y) / a.h)) : (std::max)(0.0, (std::min)(1.0, (x - a.x) / a.w));
            return a.cmd;
        }
    }
    return -1;
}

namespace ui {   // not found by argument-dependent lookup: the screens may keep helpers of the same names

// the mockups' frame(): a box with its title cut into the top edge
inline void Frame(Canvas& g, double x, double y, double w, double h, const std::wstring& title) {
    g.Stroke(x, y, w, h, cFr, 1.5);
    if (title.empty()) return;
    g.Fill(x + 10, y - 9, g.Width(title, 12, 700) + 16, 18, cBg);
    g.T(title, x + 18, y + 5, cDim, 12, 0, 700);
}
inline void Bar(Canvas& g, double x, double y, double w, double h, double f, unsigned col) {
    g.Fill(x, y, w, h, 0x14211e);
    g.Fill(x, y, w * (std::max)(0.0, (std::min)(1.0, f)), h, col);
    g.Stroke(x, y, w, h, cFr, 1);
}
inline void Lamp(Canvas& g, double x, double y, bool on, unsigned col = cGr) {
    g.Disc(x, y, 6, on ? col : 0x1c2a27);
    g.Circle(x, y, 6, 0x2e4d45, 1);
}
// the mockups' btn(): a key with a label (and a small second line), registered as a touch area
inline void Btn(Canvas& g, std::vector<Hit>& hits, const std::wstring& s, double x, double y, double w, double h, bool on, int cmd,
                unsigned col = cOr, const std::wstring& sub = L"", int size = 14) {
    g.Fill(x, y, w, h, on ? 0x3b2a14 : 0x121d1b);
    g.Stroke(x, y, w, h, on ? col : 0x5a4325, on ? 2 : 1);
    int sz = size;
    while (sz > 10 && g.Width(s, sz, 700) > w - 8) --sz;
    g.T(s, x + w / 2, y + (sub.empty() ? h / 2 + 5 : h / 2), on ? 0xffd29a : col, sz, 1, 700);
    if (!sub.empty()) g.T(g.Fit(sub, w - 6, 11), x + w / 2, y + h / 2 + 17, cDim, 11, 1);
    if (cmd >= 0) hits.push_back({x, y, w, h, cmd});
}
}  // namespace ui

inline unsigned RatioCol(double f) { return f > 0.85 ? cRd : f > 0.6 ? cYe : cGr; }
inline unsigned ZoneCol(double Tk, double lim) { return Tk >= lim ? cRd : Tk >= 0.9 * lim ? cOr : Tk >= 0.75 * lim ? cYe : cGr; }

// a small journal kept by a screen: its own events with the sim time
struct Journal {
    struct Line { double t; std::wstring txt; int lvl; };   // lvl 0 ok, 1 warn, 2 bad
    std::vector<Line> lines;
    void Add(double t, const std::wstring& s, int lvl) { lines.insert(lines.begin(), {t, s, lvl}); if (lines.size() > 8) lines.pop_back(); }
    static unsigned Col(int lvl) { return lvl >= 2 ? cRd : lvl == 0 ? cGr : cYe; }
};
inline std::wstring Clock(double t) {   // m:ss, h:mm:ss
    const long s = long((std::max)(0.0, t));
    wchar_t b[32];
    if (s < 3600) std::swprintf(b, 32, L"%ld:%02ld", s / 60, s % 60);
    else std::swprintf(b, 32, L"%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    return b;
}

}  // namespace tantra::scr
