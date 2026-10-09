// TantraFrontScreen: see TantraFrontScreen.h. The kit follows front_v3.html's helpers (T, TS, RUN, key, block, lamp, the special
// signs); Flight::Draw follows its pageFlight() block by block, the coordinates and sizes are the mockup's.
#include "TantraFrontScreen.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>

namespace tantra::front {

namespace {

constexpr double kPi = 3.14159265358979323846, kRad = kPi / 180.0;
const wchar_t* const kSpecials = L"−–—-×÷²³→←↑↓▶◀▲▼Δ";
bool IsSpecial(wchar_t c) { return std::wcschr(kSpecials, c) != nullptr && c != 0; }
// GOST.TTF's advance (1/1000 em): TantraGost's table, and the signs it does not list (the font's own, tools: fontTools, GOST.TTF)
double Adv(wchar_t c) {
    switch (c) {
        case 0x00B7: return 146;   // ·
        case 0x00A0: return 293;   // no-break space
        case 0x00AB: case 0x00BB: return 415;   // « »
        case 0x2026: return 438;   // …
        case 0x003B: return 220;   // ;
        case 0x0022: return 244;   // "
        case 0x0027: return 146;   // '
        default: return GostFont::Width(std::wstring(1, c), 1000.0);
    }
}

// a half-plane clip of a convex polygon: keep a*x + b*y + c >= 0
std::vector<P2> ClipHalf(const std::vector<P2>& in, double a, double b, double c) {
    std::vector<P2> out;
    for (size_t i = 0; i < in.size(); ++i) {
        const P2 p = in[i], q = in[(i + 1) % in.size()];
        const double dp = a * p.x + b * p.y + c, dq = a * q.x + b * q.y + c;
        if (dp >= 0) out.push_back(p);
        if ((dp >= 0) != (dq >= 0)) { const double t = dp / (dp - dq); out.push_back({p.x + t * (q.x - p.x), p.y + t * (q.y - p.y)}); }
    }
    return out;
}
// a segment clipped to a rectangle (Liang-Barsky); false when outside
bool ClipSeg(P2 p, P2 q, double x0, double y0, double x1, double y1, P2& a, P2& b) {
    double t0 = 0, t1 = 1;
    const double dx = q.x - p.x, dy = q.y - p.y;
    const double pp[4] = {-dx, dx, -dy, dy}, qq[4] = {p.x - x0, x1 - p.x, p.y - y0, y1 - p.y};
    for (int i = 0; i < 4; ++i) {
        if (pp[i] == 0) { if (qq[i] < 0) return false; continue; }
        const double r = qq[i] / pp[i];
        if (pp[i] < 0) { if (r > t1) return false; if (r > t0) t0 = r; }
        else { if (r < t0) return false; if (r < t1) t1 = r; }
    }
    a = {p.x + t0 * dx, p.y + t0 * dy}; b = {p.x + t1 * dx, p.y + t1 * dy};
    return true;
}
// a segment clipped to a circle; false when outside
bool ClipCircle(P2 p, P2 q, double cx, double cy, double r, P2& a, P2& b) {
    const double dx = q.x - p.x, dy = q.y - p.y, fx = p.x - cx, fy = p.y - cy;
    const double A = dx * dx + dy * dy, B = 2 * (fx * dx + fy * dy), C = fx * fx + fy * fy - r * r, D = B * B - 4 * A * C;
    if (D <= 0 || A <= 0) return false;
    const double s = std::sqrt(D), t0 = (std::max)(0.0, (-B - s) / (2 * A)), t1 = (std::min)(1.0, (-B + s) / (2 * A));
    if (t1 <= t0) return false;
    a = {p.x + t0 * dx, p.y + t0 * dy}; b = {p.x + t1 * dx, p.y + t1 * dy};
    return true;
}

}  // namespace

// ================= numbers =================
std::wstring Num(double v, int d) {
    if (!std::isfinite(v)) return L"—";
    wchar_t b[96];
    if (std::fabs(v) >= 1e18) std::swprintf(b, 96, L"%.2e", std::fabs(v));
    else std::swprintf(b, 96, L"%.*f", (std::max)(0, (std::min)(6, d)), std::fabs(v));
    std::wstring s(b), ip = s, fp;
    const size_t dot = s.find(L'.');
    if (dot != std::wstring::npos) { ip = s.substr(0, dot); fp = s.substr(dot + 1); }
    bool nz = false;
    for (wchar_t c : s) if (c >= L'1' && c <= L'9') nz = true;
    if (ip.size() > 4 && ip.find(L'e') == std::wstring::npos) {
        std::wstring g; const int n = int(ip.size());
        for (int i = 0; i < n; ++i) { g += ip[i]; const int rest = n - 1 - i; if (rest > 0 && rest % 3 == 0) g += L' '; }
        ip = g;
    }
    return std::wstring(v < 0 && nz ? L"−" : L"") + ip + (fp.empty() ? L"" : L"," + fp);
}
std::wstring NumS(double v, int d) {
    const std::wstring s = Num(v, d);
    return (s.empty() || s[0] == L'−' || s == L"—") ? s : L"+" + s;
}
std::wstring Hdg3(double deg) {
    long r = std::lround(deg) % 360; if (r < 0) r += 360;
    wchar_t b[8]; std::swprintf(b, 8, L"%03ld", r);
    return b;
}
std::wstring Clock(double t) {
    const long s = long((std::max)(0.0, t));
    wchar_t b[32];
    if (s < 3600) std::swprintf(b, 32, L"%ld:%02ld", s / 60, s % 60);
    else std::swprintf(b, 32, L"%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    return b;
}

int FindHit(const std::vector<Hit>& hits, double x, double y, double* val) {
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        const Hit& h = *it;
        if (x < h.x - 2 || x > h.x + h.w + 2 || y < h.y - 2 || y > h.y + h.h + 2) continue;
        double v = h.val;
        if (h.drag && h.y1 != h.y0) {
            const double f = Clamp((h.y1 - y) / (h.y1 - h.y0), 0.0, 1.0);
            v = h.lo + f * (h.hi - h.lo);
            if (h.snap > 0) v = std::round(v / h.snap) * h.snap;
            v = Clamp(v, (std::min)(h.lo, h.hi), (std::max)(h.lo, h.hi));
        }
        if (val) *val = v;
        return h.cmd;
    }
    return -1;
}

// ================= the pad =================
Pad::Pad(oapi::Sketchpad* s, ScreenFont& f, GostFont& g, double k, std::vector<Hit>* hits) : s_(s), f_(f), g_(g), k_(k > 0.01 ? k : 1.0), hits_(hits) {
    f_.BeginPass();
    s_->SetBackgroundMode(oapi::Sketchpad::BK_TRANSPARENT);
}
Pad::~Pad() { s_->SetPen(nullptr); s_->SetBrush(nullptr); }

void Pad::Fill(double x, double y, double w, double h, unsigned c) {
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    if (w * k_ < 0.5 || h * k_ < 0.5) return;
    s_->SetPen(nullptr); s_->SetBrush(f_.Brush(c));
    s_->Rectangle(X(x), X(y), X(x + w), X(y + h));
}
void Pad::Stroke(double x, double y, double w, double h, unsigned c, double lw) {
    if (w <= lw || h <= lw) return;
    s_->SetBrush(nullptr); s_->SetPen(f_.Pen(c, Pw(lw)));
    s_->Rectangle(X(x + lw / 2), X(y + lw / 2), X(x + w - lw / 2), X(y + h - lw / 2));
}
void Pad::Line(double x0, double y0, double x1, double y1, unsigned c, double lw) {
    s_->SetPen(f_.Pen(c, Pw(lw)));
    s_->Line(X(x0), X(y0), X(x1), X(y1));
}
void Pad::Poly(const std::vector<P2>& p, unsigned fill, unsigned edge, double lw) {
    if (p.size() < 3) return;
    std::vector<oapi::IVECTOR2> v(p.size());
    for (size_t i = 0; i < p.size(); ++i) { v[i].x = X(p[i].x); v[i].y = X(p[i].y); }
    s_->SetBrush(fill == kNone ? nullptr : f_.Brush(fill));
    s_->SetPen(edge == kNone || lw <= 0 ? nullptr : f_.Pen(edge, Pw(lw)));
    s_->Polygon(v.data(), int(v.size()));
}
void Pad::PolyLine(const std::vector<P2>& p, unsigned c, double lw) {
    if (p.size() < 2) return;
    std::vector<oapi::IVECTOR2> v(p.size());
    for (size_t i = 0; i < p.size(); ++i) { v[i].x = X(p[i].x); v[i].y = X(p[i].y); }
    s_->SetBrush(nullptr); s_->SetPen(f_.Pen(c, Pw(lw)));
    s_->Polyline(v.data(), int(v.size()));
}
void Pad::Disc(double x, double y, double r, unsigned c) {
    s_->SetPen(nullptr); s_->SetBrush(f_.Brush(c));
    s_->Ellipse(X(x - r), X(y - r), X(x + r), X(y + r));
}
void Pad::Ring(double x, double y, double r, unsigned c, double lw) {
    s_->SetBrush(nullptr); s_->SetPen(f_.Pen(c, Pw(lw)));
    s_->Ellipse(X(x - r), X(y - r), X(x + r), X(y + r));
}
void Pad::Ellipse(double x, double y, double rx, double ry, unsigned fill, unsigned edge, double lw) {
    s_->SetBrush(fill == kNone ? nullptr : f_.Brush(fill));
    s_->SetPen(edge == kNone || lw <= 0 ? nullptr : f_.Pen(edge, Pw(lw)));
    s_->Ellipse(X(x - rx), X(y - ry), X(x + rx), X(y + ry));
}
void Pad::Arc(double x, double y, double r, double a0, double a1, unsigned c, double lw) {
    const int n = (std::max)(4, int(std::fabs(a1 - a0) / (2 * kPi) * 64));
    std::vector<P2> p;
    for (int i = 0; i <= n; ++i) { const double t = a0 + (a1 - a0) * i / n; p.push_back({x + r * std::cos(t), y + r * std::sin(t)}); }
    PolyLine(p, c, lw);
}
void Pad::Arrow(double x0, double y0, double x1, double y1, unsigned c, double lw) {
    const double a = std::atan2(y1 - y0, x1 - x0), L = 9 + lw * 2, len = std::hypot(x1 - x0, y1 - y0);
    if (len < 2) return;
    Line(x0, y0, x1 - std::cos(a) * L * 0.8, y1 - std::sin(a) * L * 0.8, c, lw);
    Poly({{x1, y1}, {x1 - L * std::cos(a - 0.42), y1 - L * std::sin(a - 0.42)}, {x1 - L * std::cos(a + 0.42), y1 - L * std::sin(a + 0.42)}}, c);
}
void Pad::RRect(double x, double y, double w, double h, double r, unsigned fill, unsigned edge, double lw) {
    std::vector<P2> p;
    const double cs[4][3] = {{x + w - r, y + r, -kPi / 2}, {x + w - r, y + h - r, 0}, {x + r, y + h - r, kPi / 2}, {x + r, y + r, kPi}};
    for (const auto& c : cs) for (int j = 0; j <= 3; ++j) { const double a = c[2] + j * kPi / 6; p.push_back({c[0] + r * std::cos(a), c[1] + r * std::sin(a)}); }
    Poly(p, fill, edge, lw);
}

// ---- text ----
double Pad::Special(wchar_t ch, double x, double y, double size, bool bold, unsigned c, bool draw) {
    double ww;
    switch (ch) {
        case 0x00B2: case 0x00B3: ww = TW(L"2", std::round(size * 0.62)) + size * 0.06; break;
        case 0x2014: ww = 0.8 * size; break;
        case 0x2013: ww = 0.55 * size; break;
        case 0x2212: ww = 0.5 * size; break;
        case L'-': ww = 0.42 * size; break;
        case 0x00D7: case 0x00F7: ww = 0.55 * size; break;
        case 0x2192: case 0x2190: ww = 0.9 * size; break;
        case 0x2191: case 0x2193: ww = 0.5 * size; break;
        case 0x25B6: case 0x25C0: ww = 0.62 * size; break;
        case 0x25B2: case 0x25BC: ww = 0.7 * size; break;
        default: ww = 0.62 * size; break;   // Δ
    }
    if (!draw) return ww;
    const double cap = CapH(size), lw = (std::max)(1.5, size * (bold ? 0.095 : 0.07)), m = y - cap * 0.5;
    switch (ch) {
        case 0x00B2: case 0x00B3: T(ch == 0x00B2 ? L"2" : L"3", x + size * 0.03, y - cap * 0.45, c, std::round(size * 0.62), 0, bold); break;
        case 0x2014: case 0x2013: case 0x2212: case L'-': { const double p = ch == L'-' ? ww * 0.18 : ww * 0.12; Line(x + p, m, x + ww - p, m, c, lw); break; }
        case 0x00D7: { const double r = cap * 0.27, cx = x + ww / 2; Line(cx - r, m - r, cx + r, m + r, c, lw); Line(cx - r, m + r, cx + r, m - r, c, lw); break; }
        case 0x00F7: Line(x + ww * 0.15, m, x + ww * 0.85, m, c, lw); Disc(x + ww / 2, m - cap * 0.3, lw * 0.8, c); Disc(x + ww / 2, m + cap * 0.3, lw * 0.8, c); break;
        case 0x2192: case 0x2190: {
            const double d = ch == 0x2192 ? 1 : -1, x0 = x + ww * 0.08, x1 = x + ww * 0.92, tip = d > 0 ? x1 : x0, hh = cap * 0.32;
            Line(d > 0 ? x0 : x0 + hh, m, d > 0 ? x1 - hh : x1, m, c, lw);
            Poly({{tip, m}, {tip - d * hh * 1.5, m - hh}, {tip - d * hh * 1.5, m + hh}}, c);
            break;
        }
        case 0x2191: case 0x2193: {
            const double d = ch == 0x2191 ? -1 : 1, cx = x + ww / 2, tip = d < 0 ? y - cap : y, hh = cap * 0.3;
            Line(cx, d < 0 ? y : y - cap, cx, tip - d * hh, c, lw);
            Poly({{cx, tip}, {cx - hh, tip - d * hh * 1.5}, {cx + hh, tip - d * hh * 1.5}}, c);
            break;
        }
        case 0x25B6: Poly({{x + ww * 0.15, y - cap}, {x + ww * 0.9, y - cap / 2}, {x + ww * 0.15, y}}, c); break;
        case 0x25C0: Poly({{x + ww * 0.85, y - cap}, {x + ww * 0.1, y - cap / 2}, {x + ww * 0.85, y}}, c); break;
        case 0x25B2: Poly({{x + ww * 0.05, y}, {x + ww / 2, y - cap}, {x + ww * 0.95, y}}, c); break;
        case 0x25BC: Poly({{x + ww * 0.05, y - cap}, {x + ww * 0.95, y - cap}, {x + ww / 2, y}}, c); break;
        default: Poly({{x + ww * 0.08, y}, {x + ww / 2, y - cap}, {x + ww * 0.92, y}}, kNone, c, lw); break;   // Δ
    }
    return ww;
}
double Pad::TW(const std::wstring& s, double size) {
    double a = 0.0;
    for (wchar_t c : s) {
        if (!IsSpecial(c)) { a += Adv(c) * size / 1000.0; continue; }
        if (c == 0x00B2 || c == 0x00B3) a += TW(L"2", std::round(size * 0.62)) + size * 0.06;
        else {
            static const struct { wchar_t c; double w; } kW[] = {{0x2014, 0.8}, {0x2013, 0.55}, {0x2212, 0.5}, {L'-', 0.42}, {0x00D7, 0.55}, {0x00F7, 0.55},
                {0x2192, 0.9}, {0x2190, 0.9}, {0x2191, 0.5}, {0x2193, 0.5}, {0x25B6, 0.62}, {0x25C0, 0.62}, {0x25B2, 0.7}, {0x25BC, 0.7}, {0x0394, 0.62}};
            for (const auto& q : kW) if (q.c == c) { a += q.w * size; break; }
        }
    }
    return a;
}
double Pad::T(const std::wstring& s, double x, double y, unsigned c, double size, int align, bool bold) {
    if (s.empty()) return 0.0;
    const double tw = TW(s, size);
    double cx = align == 2 ? x - tw : align == 1 ? x - tw / 2 : x;
    const int em = (std::max)(2, int(std::lround(size * k_)));
    const int nb = bold ? (std::max)(1, int(std::lround(em / 24.0))) : 0;   // the faux bold: copies a pixel apart
    std::wstring run;
    auto flush = [&]() {
        if (run.empty()) return;
        for (int i = 0; i <= nb; ++i) g_.Text(s_, X(cx) + i, X(y), run, em, c, 0);
        cx += TW(run, size);
        run.clear();
    };
    for (wchar_t ch : s) {
        if (!IsSpecial(ch)) { run += ch; continue; }
        flush();
        cx += Special(ch, cx, y, size, bold, c, true);
    }
    flush();
    return tw;
}
double Pad::TS(const std::wstring& s, double x, double y, unsigned c, double size, bool bold, double sp, int align) {
    auto cw = [&](wchar_t ch) { return IsSpecial(ch) ? Special(ch, 0, 0, size, bold, c, false) : Adv(ch) * size / 1000.0; };
    double tw = -sp;
    for (wchar_t ch : s) tw += cw(ch) + sp;
    double cx = align == 2 ? x - tw : align == 1 ? x - tw / 2 : x;
    for (wchar_t ch : s) {
        if (IsSpecial(ch)) Special(ch, cx, y, size, bold, c, true);
        else if (ch != L' ') T(std::wstring(1, ch), cx, y, c, size, 0, bold);
        cx += cw(ch) + sp;
    }
    return tw;
}
double Pad::RunW(const std::vector<Part>& p) { double a = 0; for (const Part& q : p) a += q.gap + TW(q.s, q.size); return a; }
double Pad::Run(const std::vector<Part>& p, double x, double y, int align) {
    const double tw = RunW(p);
    double cx = align == 2 ? x - tw : align == 1 ? x - tw / 2 : x;
    for (const Part& q : p) { cx += q.gap; cx += T(q.s, cx, y, q.c, q.size, 0, q.bold); }
    return tw;
}
int Pad::FitSize(const std::wstring& s, double mw, int size, int min) {
    while (size > min && TW(s, size) > mw) --size;
    return size;
}
std::wstring Pad::FitTxt(const std::wstring& s, double mw, double size) {
    if (TW(s, size) <= mw) return s;
    std::wstring t = s;
    while (t.size() > 1 && TW(t + L"..", size) > mw) t.pop_back();
    while (!t.empty() && t.back() == L' ') t.pop_back();
    return t + L"..";
}
std::vector<std::wstring> Pad::Wrap(const std::wstring& s, double w, double size) {
    std::vector<std::wstring> out;
    std::wstring cur, word;
    auto push = [&]() {
        if (word.empty()) return;
        const std::wstring t = cur.empty() ? word : cur + L" " + word;
        if (TW(t, size) <= w || cur.empty()) cur = t; else { out.push_back(cur); cur = word; }
        word.clear();
    };
    for (wchar_t c : s) { if (c == L' ') push(); else word += c; }
    push();
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---- the elements ----
void Pad::Key(double x, double y, double w, double h, const std::wstring& label, int st, int cmd, double val, int size) {
    struct KS { unsigned f, e, t; };
    static const KS kS[6] = {{0x0f1c1a, 0x2f5a52, kTx}, {kOr, kOr, kInk}, {kTx, kTx, kInk}, {0x0f1c1a, kRd, kRd}, {kRd, kRd, kInk}, {0x161d1c, kNone, 0x4d5c59}};
    const KS& k = kS[st < 0 || st > 5 ? 0 : st];
    RRect(x + 0.75, y + 0.75, w - 1.5, h - 1.5, 4, k.f, k.e, st == kKeyWarn || st == kKeyWarnOn ? 2.0 : 1.5);
    if (!label.empty()) {
        int s = FitSize(label, w - 14, size, 18);
        if (TW(label, s) > w - 14) s = FitSize(label, w - 14, s, 15);   // SPEC: 18-20; down to 15 only when it would not fit
        T(label, x + w / 2, y + h / 2 + CapH(s) / 2, k.t, s, 1, true);
    }
    if (cmd >= 0 && st != kKeyNa) AddHit({x, y, w, h, cmd, val});
}
void Pad::Block(double x, double y, double w, double h, const std::wstring& title) {
    if (w < 8 || h < kBandH + 4) return;
    Fill(x, y, w, kBandH, kBandBg);
    Stroke(x, y, w, h, kFr, 1.5);
    Line(x, y + kBandH, x + w, y + kBandH, kFr, 1.5);
    if (title.empty()) return;
    double size = 18;
    auto wsp = [&](double sz) { return TW(title, sz) + title.size() - 1.0; };
    while (size > 15 && wsp(size) > w - 32) size -= 1;
    TS(title, x + 16, BandBase(y), kDim, size, true, 1);
}
void Pad::Lamp(double x, double y, unsigned c, double r) {
    Disc(x, y, r, c);
    Ring(x, y, r + 3, MixC(kBg, c, 0.45), 1.5);
}

void Tabs(Pad& p, double W, int tab) {
    static const wchar_t* const kName[3] = {L"ПОЛЁТ", L"ДВИГАТЕЛИ", L"ПАРАМЕТРЫ"};
    for (int i = 0; i < 3; ++i) p.Key(TabsX0(W) + i * 160, 24, 150, 48, kName[i], tab == i ? kKeyOn : kKeyOff, i);
}

// ================= ПОЛЁТ =================
namespace {

// a vertical gauge: the track, 10 % ticks on the left, the fill from the bottom; a touch bar gets the set-point arrow (yellow)
void VBar(Pad& g, double x, double y, double w, double h, double f, unsigned col, bool touch, double set) {
    g.Fill(x, y, w, h, 0x0d1715); g.Stroke(x, y, w, h, kFr, 1.5);
    for (int k = 1; k < 10; ++k) g.Line(x, y + h * k / 10, x + (k == 5 ? 12 : 6), y + h * k / 10, kLn, 1.5);
    const double fh = h * Clamp(f, 0, 1);
    if (fh > 1) { g.Fill(x + 2, y + h - fh, w - 4, fh - 2, MixC(kBg, col, 0.35)); g.Fill(x + 6, y + h - fh, w - 12, fh - 2, col); }
    if (touch) {
        const double yp = y + h - h * Clamp(set, 0, 1);
        g.Line(x, yp, x + w, yp, MixC(kBg, kYe, 0.75), 1.5);
        g.Poly({{x + w + 1, yp}, {x + w + 18, yp - 9}, {x + w + 18, yp + 9}}, kYe);
    }
}
void HeadingTape(Pad& g, double x, double y, double w, double h, double hd) {
    g.Fill(x, y, w, h, kTape); g.Stroke(x, y, w, h, kFr, 1.5);
    const double cx = x + w / 2, ppd = w / 60;
    for (int d = int(std::floor((hd - 32) / 5)) * 5; d <= hd + 32; d += 5) {
        const double xx = cx + (d - hd) * ppd;
        if (xx < x + 4 || xx > x + w - 4) continue;
        const bool big = ((d % 10) + 10) % 10 == 0;
        g.Line(xx, y + h - (big ? 14 : 8), xx, y + h, kLn, 2);
        if (big) {
            const std::wstring s = Hdg3(d);
            const double tw = Pad::TW(s, 15);
            if (std::fabs(xx - cx) < 44 + tw / 2 + 6 || xx - tw / 2 < x + 4 || xx + tw / 2 > x + w - 4) continue;
            g.T(s, xx, y + 22, kTx, 15, 1);
        }
    }
    g.Poly({{cx - 44, y + 2}, {cx + 44, y + 2}, {cx + 44, y + h - 4}, {cx, y + h + 6}, {cx - 44, y + h - 4}}, 0x000000, kOr, 2);
    g.T(Hdg3(hd) + L"°", cx, y + h / 2 + Pad::CapH(26) / 2 - 1, kWh, 26, 1, true);
}
// the attitude indicator: wide and low (the glass is); the bank scale fixed on top, its pointer turns with the sky
void Adi(Pad& g, double x, double y, double w, double h, const FlightView& F) {
    const double cx = x + w / 2, cy = y + h / 2, k = (h / 2) / 22, b = F.bank * kRad, sb = std::sin(b), cb = std::cos(b);
    g.Fill(x, y, w, h, kSky);
    const double off = F.pitch * k;
    auto P = [&](double u, double v) { return P2{cx + u * cb + v * sb, cy - u * sb + v * cb}; };
    const P2 hp = P(0, off);
    const std::vector<P2> gnd = ClipHalf({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, sb, cb, -(sb * hp.x + cb * hp.y));
    if (gnd.size() >= 3) g.Poly(gnd, kGround);
    P2 a, c;
    if (ClipSeg(P(-2 * w, off), P(2 * w, off), x + 1, y + 1, x + w - 1, y + h - 1, a, c)) g.Line(a.x, a.y, c.x, c.y, kWh, 2.5);
    const double R = h / 2 - 4;
    const int tk[9] = {-45, -30, -20, -10, 0, 10, 20, 30, 45};
    P2 tp[9];
    for (int i = 0; i < 9; ++i) { const double L = tk[i] % 30 == 0 ? 14 : 8, an = tk[i] * kRad; tp[i] = {cx + (R - L / 2) * std::sin(an), cy - (R - L / 2) * std::cos(an)}; }
    for (int d = -90; d <= 90; d += 5) {
        if (!d) continue;
        const double v = (F.pitch - d) * k;
        if (std::fabs(v) > h / 2 - 40) continue;
        const double hw = d % 10 ? 28 : 56;
        if (ClipSeg(P(-hw, v), P(hw, v), x + 6, y + 6, x + w - 6, y + h - 6, a, c)) g.Line(a.x, a.y, c.x, c.y, kWh, 2);
        if (d % 10) continue;
        for (int sd = -1; sd <= 1; sd += 2) {
            const P2 q = P(sd * (hw + 18), v);
            const std::wstring st = Num(std::abs(d), 0);
            const double tw = Pad::TW(st, 15), ch = Pad::CapH(15);
            if (q.x - tw / 2 < x + 6 || q.x + tw / 2 > x + w - 6 || q.y - ch / 2 < y + 6 || q.y + ch / 2 > y + h - 6) continue;
            bool nearTick = false;
            for (const P2& p : tp) if (std::fabs(p.x - q.x) < tw / 2 + 10 && std::fabs(p.y - q.y) < ch / 2 + 10) nearTick = true;
            if (nearTick) continue;                                                               // never over the bank scale
            if (std::fabs(q.x - cx) < 120 + tw / 2 + 6 && std::fabs(q.y - cy - 7) < ch / 2 + 13) continue;   // nor the aircraft symbol
            g.T(st, q.x, q.y + ch / 2, kWh, 15, 1, true);
        }
    }
    for (int i = 0; i < 9; ++i) {
        const double L = tk[i] % 30 == 0 ? 14 : 8, an = tk[i] * kRad;
        g.Line(cx + R * std::sin(an), cy - R * std::cos(an), cx + (R - L) * std::sin(an), cy - (R - L) * std::cos(an), kWh, 2);
    }
    {
        const double an = -b, r1 = R - 18, r2 = R - 32, sp = 0.08;
        g.Poly({{cx + r1 * std::sin(an), cy - r1 * std::cos(an)}, {cx + r2 * std::sin(an - sp), cy - r2 * std::cos(an - sp)}, {cx + r2 * std::sin(an + sp), cy - r2 * std::cos(an + sp)}}, kYe);
    }
    const double lim = h / 2 - 16, fx = cx + Clamp(F.slip * k, -lim, lim), fy = cy + Clamp((F.pitch - F.fpa) * k, -lim, lim);
    g.Ring(fx, fy, 8, kGr, 2); g.Line(fx - 20, fy, fx - 8, fy, kGr, 2); g.Line(fx + 8, fy, fx + 20, fy, kGr, 2); g.Line(fx, fy - 8, fx, fy - 16, kGr, 2);
    g.PolyLine({{cx - 120, cy}, {cx - 44, cy}, {cx - 30, cy + 14}}, kOr, 4);
    g.PolyLine({{cx + 120, cy}, {cx + 44, cy}, {cx + 30, cy + 14}}, kOr, 4);
    g.Fill(cx - 4, cy - 4, 8, 8, kOr);
    g.Stroke(x, y, w, h, kFr, 1.5);
}
// a tape: ticks toward the attitude indicator, the labels 15 px clear of the window, the value 34 px in a window pointing at it
struct TapeScale { double ppu, tick; int every; };
void Tape(Pad& g, double x, double y, double w, double h, double val, TapeScale sc, bool left) {
    g.Fill(x, y, w, h, kTape); g.Stroke(x, y, w, h, kFr, 1.5);
    const double cy = y + h / 2, win = 26, lo = val - (h / 2) / sc.ppu, hi = val + (h / 2) / sc.ppu, ch = Pad::CapH(15);
    for (double v = std::ceil(lo / sc.tick) * sc.tick; v <= hi; v += sc.tick) {
        if (v < 0) continue;
        const double yy = cy - (v - val) * sc.ppu;
        if (yy < y + 4 || yy > y + h - 4) continue;
        const bool big = (long long)std::llround(v / sc.tick) % sc.every == 0;
        const double L = big ? 16 : 8;
        if (left) g.Line(x + w - L, yy, x + w, yy, kLn, 2); else g.Line(x, yy, x + L, yy, kLn, 2);
        if (!big || std::fabs(yy - cy) < win + ch / 2 + 4 || yy - ch / 2 < y + 4 || yy + ch / 2 > y + h - 4) continue;
        if (left) g.T(Num(v), x + w - 22, yy + ch / 2, kTx, 15, 2); else g.T(Num(v), x + 22, yy + ch / 2, kTx, 15, 0);
    }
    const double x0 = x + 3, x1 = x + w - 3;
    if (left) g.Poly({{x0, cy - win}, {x1, cy - win}, {x1 + 9, cy}, {x1, cy + win}, {x0, cy + win}}, 0x000000, kOr, 2);
    else g.Poly({{x0, cy - win}, {x1, cy - win}, {x1, cy + win}, {x0, cy + win}, {x0 - 9, cy}}, 0x000000, kOr, 2);
    const std::wstring s = Num(val);
    g.T(s, (x0 + x1) / 2, cy + Pad::CapH(34) / 2, kWh, Pad::FitSize(s, x1 - x0 - 8, 34, 26), 1, true);
}
void VsBar(Pad& g, double x, double y, double w, double h, double vs) {
    g.Fill(x, y, w, h, kTape); g.Stroke(x, y, w, h, kFr, 1.5);
    const double cy = y + h / 2, win = 22, span = h / 2 - win - 6;
    auto f = [&](double v) { return (std::min)(1.0, std::sqrt(std::fabs(v) / 60)) * span; };
    for (double v : {5.0, 20.0, 60.0}) for (int sd = -1; sd <= 1; sd += 2) { const double yy = cy - sd * (win + f(v)); g.Line(x, yy, x + 10, yy, kLn, 2); }
    const double d = f(vs);
    if (d > 0.5) g.Fill(x + 16, vs >= 0 ? cy - win - d : cy + win, w - 22, d, vs >= 0 ? kGr : kOr);
    g.Fill(x + 3, cy - win, w - 6, 2 * win, 0x000000); g.Stroke(x + 3, cy - win, w - 6, 2 * win, kOr, 2);
    const std::wstring s = std::fabs(vs) >= 10 ? NumS(vs, 0) : NumS(vs, 1);
    g.T(s, x + w / 2, cy + Pad::CapH(24) / 2, vs < -5 ? kRd : kWh, Pad::FitSize(s, w - 12, 24, 17), 1, true);
}
// space: the same places - the speed left, the altitude and the vertical right, the heading on top, the horizon ball in the middle
void SpaceView(Pad& g, double ix0, double ix1, double rowY, double iy0, double iy1, const FlightView& F) {
    const double cx = (ix0 + ix1) / 2, cy = (iy0 + iy1) / 2, r = 100, k = r / 40, b = F.bank * kRad, sb = std::sin(b), cb = std::cos(b);
    g.Run({{L"КУРС", 17, true, kDim, 0}, {Hdg3(F.hdg) + L"°", 26, true, kWh, 12}}, cx, rowY + 20 + Pad::CapH(26) / 2, 1);
    g.Disc(cx, cy, r, 0x0c1a17); g.Ring(cx, cy, r, 0x60c060, 2);
    auto P = [&](double u, double v) { return P2{cx + u * cb + v * sb, cy - u * sb + v * cb}; };
    for (int d = -30; d <= 30; d += 10) {
        const double v = (F.pitch - d) * k;
        if (std::fabs(v) > r * 0.9) continue;
        const double hw = d == 0 ? r * 1.2 : r * 0.3;
        P2 a, c;
        if (ClipCircle(P(-hw, v), P(hw, v), cx, cy, r - 4, a, c)) g.Line(a.x, a.y, c.x, c.y, d == 0 ? 0x60e070 : 0x3f7a48, d == 0 ? 2.5 : 2);
    }
    g.Line(cx - 50, cy, cx - 16, cy, kOr, 4); g.Line(cx + 16, cy, cx + 50, cy, kOr, 4);
    g.Fill(cx - 3, cy - 3, 6, 6, kOr);
    const double lx = cx - r - 40, rx = cx + r + 40;
    g.T(L"СКОРОСТЬ", lx, 150, kDim, 17, 2, true);
    g.Run({{Num(F.v), 44, true, kWh, 0}, {L"м/с", 17, false, kDim, 8}}, lx, 202, 2);
    g.T(L"ВЫСОТА", rx, 150, kDim, 17, 0, true);
    g.Run({{Num(F.alt / 1000, 1), 44, true, kWh, 0}, {L"км", 17, false, kDim, 8}}, rx, 202, 0);
    g.T(L"ВЕРТИКАЛЬНАЯ", rx, 250, kDim, 17, 0, true);
    g.Run({{NumS(F.vs, 1), 34, true, F.vs < -5 ? kRd : kWh, 0}, {L"м/с", 17, false, kDim, 8}}, rx, 292, 0);
}

}  // namespace

void Flight::Draw(oapi::Sketchpad* skp, ScreenFont& font, GostFont& gost, double k, double W, const FlightView& F) {
    hits_.clear();
    if (!skp) return;
    Pad g(skp, font, gost, k, &hits_);
    const double H = kDesignH;
    g.Fill(0, 0, W, H, kBg); g.Stroke(4, 4, W - 8, H - 8, kFr, 2);
    const Rect hb = HubRect(W);
    const double cw = 874, cx0 = std::round(W / 2 - cw / 2), cx1 = cx0 + cw;
    const double Lw = cx0 - kGap - kM, Rx = cx1 + kGap, Rw = W - kM - Rx;
    const bool atmo = F.atmo;
    // ---- the centre, along the top: the attitude, the speed, the altitude, the vertical speed, the heading ----
    g.Block(cx0, kUY0, cw, kUY1 - kUY0, L"");
    const double bb = Pad::BandBase(kUY0);
    const std::vector<Pad::Part> apRun = {{L"автопилот", 15, false, kDim, 0}, {F.ap.empty() ? L"РУЧНОЕ" : F.ap, 18, true, F.ap.empty() ? kTx : kOr, 10}};
    const double apW = g.Run(apRun, cx1 - 16, bb, 2);
    if (F.alert) {                                                        // an alert takes the title's place: at the top, by the big screen
        const unsigned c = F.alert == 2 ? kRd : kYe;
        const std::wstring word = F.alert == 2 ? L"ОПАСНОСТЬ" : L"ВНИМАНИЕ";
        g.Lamp(cx0 + 26, kUY0 + 18, c, 7);
        const double room = (cx1 - 16 - apW - 16) - (cx0 + 44) - Pad::TW(word, 18) - 12;
        g.Run({{word, 18, true, c, 0}, {Pad::FitTxt(F.alertText, (std::max)(20.0, room), 18), 18, true, kWh, 12}}, cx0 + 44, bb, 0);
    } else g.TS(atmo ? L"ПОЛЁТ · АТМОСФЕРА" : L"ПОЛЁТ · КОСМОС", cx0 + 16, bb, kDim, 18, true, 1);
    const double ix0 = cx0 + 16, ix1 = cx1 - 16, rowY = 68, iy0 = 116, iy1 = 331;
    if (atmo) {
        const double spdX = ix0, vsX = ix1 - 64, altX = vsX - 12 - 120, adiX = ix0 + 132, adiW = altX - 12 - adiX;
        auto lab = [&](double cx, const wchar_t* t, const wchar_t* u) { g.Run({{t, 17, true, kDim, 0}, {u, 15, false, kDim, 6}}, cx, rowY + 20 + Pad::CapH(17) / 2, 1); };
        lab(spdX + 60, L"СКОРОСТЬ", L"м/с"); lab(altX + 60, L"ВЫСОТА", L"м");
        g.T(L"ВЕРТ.", vsX + 32, rowY + 16, kDim, 17, 1, true); g.T(L"м/с", vsX + 32, rowY + 36, kDim, 15, 1);
        HeadingTape(g, adiX, rowY, adiW, 40, F.hdg);
        Adi(g, adiX, iy0, adiW, iy1 - iy0, F);
        Tape(g, spdX, iy0, 120, iy1 - iy0, F.v, F.v < 600 ? TapeScale{2.15, 10, 2} : TapeScale{0.215, 100, 2}, true);
        Tape(g, altX, iy0, 120, iy1 - iy0, F.alt, F.alt < 3000 ? TapeScale{0.43, 50, 2} : F.alt < 30000 ? TapeScale{0.043, 500, 2} : TapeScale{0.0043, 5000, 2}, false);
        VsBar(g, vsX, iy0, 64, iy1 - iy0, F.vs);
    } else SpaceView(g, ix0, ix1, rowY, iy0, iy1, F);
    // ---- left, top: the secondary angles ----
    if (Lw >= 160) {
        g.Block(kM, kUY0, Lw, kUY1 - kUY0, L"УГЛЫ · МАХ");
        const std::wstring cells[7][2] = {{L"ТАНГАЖ", NumS(F.pitch, 1) + L"°"}, {L"КРЕН", std::wstring(F.bank >= 0 ? L"П " : L"Л ") + Num(std::fabs(F.bank), 1) + L"°"},
                                          {L"АТАКА", Num(F.aoa, 1) + L"°"}, {L"СКОЛЬЖЕНИЕ", Num(F.slip, 1) + L"°"}, {L"ГЛИССАДА", NumS(F.fpa, 1) + L"°"},
                                          {L"ВЫДВ. БЛОКИ", Num(F.pods, 0) + L"°"}, {L"МАХ", F.air ? Num(F.mach, 2) : L"—"}};
        const int cols = Lw >= 700 ? 3 : 2, rows = (7 + cols - 1) / cols;
        const double step = rows >= 4 ? 64 : 80, cwid = (Lw - 32) / cols, vsz = step >= 80 ? 34 : 26;
        for (int i = 0; i < 7; ++i) {
            const double cx = kM + 16 + (i % cols) * cwid, cy = kUY0 + kBandH + 8 + (i / cols) * step;
            g.T(cells[i][0], cx, cy + 22, kDim, 17, 0, true);
            g.T(cells[i][1], cx, cy + step - 12, kWh, Pad::FitSize(cells[i][1], cwid - 8, int(vsz), 22), 0, true);
        }
        // ---- left, bottom (the left hand): what the big screen shows, the RCS ----
        g.Block(kM, kLY0, Lw, kLY1 - kLY0, L"ГЛАВНЫЙ ЭКРАН");
        const double kw4 = std::floor((Lw - 32 - 30) / 4 / 10) * 10, kw3 = std::floor((Lw - 32 - 20) / 3 / 10) * 10, r2 = kLY1 - 8 - 56, r1 = r2 - 32 - 56;
        static const struct { const wchar_t* n; int mode; } kHud[4] = {{L"ГОРИЗОНТ", HUD_SURFACE}, {L"ОРБИТА", HUD_ORBIT}, {L"СТЫКОВКА", HUD_DOCKING}, {L"ВЫКЛ", HUD_NONE}};
        static const struct { const wchar_t* n; int mode; } kRcs[3] = {{L"ВРАЩЕНИЕ", RCS_ROT}, {L"ЛИНЕЙНОЕ", RCS_LIN}, {L"ВЫКЛ", RCS_NONE}};
        g.T(L"HUD", kM + 16, r1 - 8, kDim, 17, 0, true);
        for (int i = 0; i < 4; ++i) g.Key(kM + 16 + i * (kw4 + 10), r1, kw4, 56, kHud[i].n, F.hud == kHud[i].mode ? kKeyOn : kKeyOff, kFlHud, kHud[i].mode);
        g.T(L"РСУ", kM + 16, r2 - 8, kDim, 17, 0, true);
        for (int i = 0; i < 3; ++i) g.Key(kM + 16 + i * (kw3 + 10), r2, kw3, 56, kRcs[i].n, F.rcs == kRcs[i].mode ? kKeyOn : kKeyOff, kFlRcs, kRcs[i].mode);
    }
    const double r2 = kLY1 - 8 - 56;
    // ---- centre, bottom, left of the hub: the screen's mode ----
    const double clw = hb.x0 - kGap - cx0;
    if (clw >= 120) {
        g.Block(cx0, kLY0, clw, kLY1 - kLY0, L"РЕЖИМ");
        g.T(Pad::FitTxt(F.termAuto ? L"выбор: авто, по высоте" : L"выбор: вручную", clw - 32, 17), cx0 + 16, kLY0 + kBandH + 32, kTx, 17);
        g.Key(cx0 + 16, r2 - 66, clw - 32, 56, L"КОСМОС", atmo ? kKeyOff : kKeyOnT, kFlTerm, 0);
        g.Key(cx0 + 16, r2, clw - 32, 56, L"АТМОСФЕРА", atmo ? kKeyOnT : kKeyOff, kFlTerm, 1);
    }
    // ---- centre, bottom, right of the hub: the alerts' acknowledge, always in the same place under the right hand ----
    const double crx = hb.x1 + kGap, crw = cx1 - crx;
    if (crw >= 120) {
        g.Block(crx, kLY0, crw, kLY1 - kLY0, L"ТРЕВОГИ");
        if (F.alert) {
            const unsigned c = F.alert == 2 ? kRd : kYe;
            g.Lamp(crx + 26, kLY0 + kBandH + 26, c, 7);
            g.T(F.alert == 2 ? L"ОПАСНОСТЬ" : L"ВНИМАНИЕ", crx + 44, kLY0 + kBandH + 26 + Pad::CapH(22) / 2, c, Pad::FitSize(L"ОПАСНОСТЬ", crw - 60, 22), 0, true);
            g.T(F.alertAcked ? L"подтверждена" : L"не подтверждена", crx + 16, kLY0 + kBandH + 74, kTx, 17);
        } else g.T(L"нет", crx + 16, kLY0 + kBandH + 32, kTx, 17);
        g.Key(crx + 16, r2, crw - 32, 56, L"ПОДТВЕРДИТЬ", F.alert ? (!F.alertAcked && F.blink ? kKeyWarnOn : kKeyWarn) : kKeyNa, kFlAck);
    }
    // ---- right: the thrust, the fuel, the g - three gauges of one height; the thrust bar takes a touch ----
    if (Rw >= 200) {
        g.Block(Rx, 88, Rw, kLY1 - 88, L"ТЯГА");
        static const wchar_t* const kSt[4] = {L"ВЫКЛ", L"ПОЛЕ", L"ЛУЧ", L"ПОДАЧА"};
        static const wchar_t* const kMass[3] = {L"аргон", L"железо", L"продукты"};
        const std::wstring set = F.ana ? std::wstring(L"анамезон: ") + kSt[(std::max)(0, (std::min)(3, F.stage))] : std::wstring(L"планетарные · ") + kMass[(std::max)(0, (std::min)(2, F.mass))];
        g.T(set, Rx + Rw - 16, Pad::BandBase(88, 17), F.ana ? kOr : kTx, 17, 2, true);
        const double gw = (Rw - 32) / 3, by0 = 240, by1 = kLY1 - 16;
        auto gx = [&](int i) { return Rx + 16 + gw * i + gw / 2; };
        const double gLim = F.gLim, gv = F.g, gmax = (std::max)(2.0, gLim * 1.25);
        const bool gHot = F.gLimOn && gv > gLim * 0.9;
        const wchar_t* const kLab[3] = {L"ТЯГА", L"ТОПЛИВО", L"ПЕРЕГРУЗКА"};
        for (int i = 0; i < 3; ++i) g.T(kLab[i], gx(i), 160, kDim, Pad::FitSize(kLab[i], gw - 6, 17), 1, true);
        g.Run({{Num(F.thrAct * 100), 34, true, kWh, 0}, {L"%", 22, true, kDim, 4}}, gx(0), 202, 1);
        g.Run({{Num(F.fuel * 100), 34, true, F.fuel < 0.1 ? kRd : kWh, 0}, {L"%", 22, true, kDim, 4}}, gx(1), 202, 1);
        g.Run({{Num(gv, 2), 34, true, gHot ? kRd : kWh, 0}, {L"g", 22, true, kDim, 4}}, gx(2), 202, 1);
        g.T(F.gLimOn ? L"предел " + Num(gLim, 1) : std::wstring(L"предел выкл"), gx(2), 226, kTx, 15, 1);
        const double tbw = gw >= 200 ? 80 : 64;
        VBar(g, gx(0) - tbw / 2, by0, tbw, by1 - by0, F.thrAct, kCy, true, F.thrSet);
        VBar(g, gx(1) - 20, by0, 40, by1 - by0, F.fuel, F.fuel < 0.1 ? kRd : kGr, false, 0);
        VBar(g, gx(2) - 20, by0, 40, by1 - by0, gv / gmax, gHot ? kRd : kTx, false, 0);
        if (F.gLimOn) {
            const double yl = by1 - (by1 - by0) * Clamp(gLim / gmax, 0, 1);
            g.Line(gx(2) - 26, yl, gx(2) + 26, yl, kRd, 3);
            g.T(Num(gLim, 1), gx(2) + 32, yl + Pad::CapH(15) / 2, kRd, 15);
        }
        front::Hit th = {gx(0) - tbw / 2 - 10, by0 - 6, tbw + 20, by1 - by0 + 12, kFlThrust};
        th.drag = true; th.y0 = by0; th.y1 = by1; th.lo = 0; th.hi = 1;
        g.AddHit(th);
    }
}

}  // namespace tantra::front
