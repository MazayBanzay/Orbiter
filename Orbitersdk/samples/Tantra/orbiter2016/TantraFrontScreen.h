// TantraFrontScreen: the front glass of the commander's bridge (screen 2: the low wide glass under the big screen, top 1.24 m,
// 0.52 m high, +-45 deg - the user's «ШИРЕ», 2026-10-05). Its three pages follow Tantra_Design/refine/front_v3.html (the 2430 x 611
// canvas) under the rules of refine/SPEC.md: GOST 2.304 type A everywhere (TantraGost), no text under 15 px, one key module
// (56 px, 48 in dense rows), every block with its title in its own band, nothing needed in flight under the yoke's hub.
// This header is the kit the pages share - the scaled pad, the text with the signs GOST.TTF lacks drawn as lines, the key, the
// block, the touch areas - and the ПОЛЁТ page itself (ДВИГАТЕЛИ: TantraEngineScreen, ПАРАМЕТРЫ: TantraParamsScreen).
// Everything is laid out in DESIGN px: kDesignH high, the width the glass's own (TantraDisplays gW[2]: ~2365 on the 3.87 : 1 glass,
// front_v3 drew 2430 - its formulas follow the width: the centre keeps its size, the side zones give or take the rest). The surface
// is k times the design (the pad scales); touches arrive in design px.
#pragma once
#include "orbitersdk.h"
#include "TantraScreenFont.h"
#include "TantraGost.h"

#include <cmath>
#include <string>
#include <vector>

namespace tantra::front {

constexpr int kDesignH = 611;                              // the glass's design height (TantraDisplays kH[2])
// the grid of SPEC.md: 24 px from the edge, 16 between blocks, the title band 36; the upper band (along the big screen's horizon)
// and the lower band (toward the hands)
constexpr double kM = 24, kGap = 16, kBandH = 36, kUY0 = 24, kUY1 = 339, kLY0 = 355, kLY1 = 587;
// the yoke's hub hides the glass's lower middle: 400 px wide, the lower 256 px (y >= 355) - nothing needed in flight goes there
struct HubZone { double w, h; };
constexpr HubZone kHub = {400.0, 256.0};
struct Rect { double x0, y0, x1, y1; };
inline Rect HubRect(double W) { const double x0 = std::round(W / 2 - kHub.w / 2); return {x0, kDesignH - kHub.h, x0 + kHub.w, double(kDesignH)}; }
inline double TabsX0(double W) { return W - kM - 470; }   // the page tabs: three keys 150 x 48 in the top right corner

// the palette of SPEC.md (0xRRGGBB): the dim text lighter (kDim), the old dim only for lines (kLn); the tapes, the sky / ground
constexpr unsigned kBg = 0x0a1311, kFr = 0x1f3a35, kTx = 0x7fe0d0, kDim = 0x6fb3a8, kLn = 0x4f8f86, kOr = 0xee9a3a, kYe = 0xe8d35a,
                   kRd = 0xff5a4a, kGr = 0x5ad0a0, kWh = 0xe8fffa, kBl = 0x7fb8ff, kVi = 0xb59cff, kCy = 0x46e2ff, kBandBg = 0x0f1d1a,
                   kTrack = 0x14211e, kTape = 0x0b1513, kSky = 0x1b4660, kGround = 0x4a3519, kInk = 0x0a1311, kNone = 0xFFFFFFFFu;
// the mockup's mix(a, b, f): a moved toward b by f (no alpha on the glass: the translucent colours are mixed beforehand)
inline unsigned MixC(unsigned a, unsigned b, double f) {
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    auto ch = [&](int s) { const double x = ((a >> s) & 255u), y = ((b >> s) & 255u); return unsigned(std::lround(x + (y - x) * f)) << s; };
    return ch(16) | ch(8) | ch(0);
}
inline double Clamp(double v, double a, double b) { return v < a ? a : v > b ? b : v; }

// numbers as front_v3's fmt(): a decimal comma, groups of three from 10 000, a real minus (drawn as a line: GOST.TTF has none)
std::wstring Num(double v, int d = 0);
std::wstring NumS(double v, int d = 0);                    // with the sign (+ for zero)
std::wstring Hdg3(double deg);                             // a heading: three digits
std::wstring Clock(double t);                              // m:ss (h:mm:ss)

// a touch area (design px): a key gives its value; a scale (drag) the value under the touch, from lo at y1 to hi at y0, snapped
struct Hit {
    double x, y, w, h; int cmd; double val = 0.0;
    bool drag = false; double y0 = 0, y1 = 0, lo = 0, hi = 1, snap = 0;
};
int FindHit(const std::vector<Hit>& hits, double x, double y, double* val);   // the command (-1 none) and its value

enum KeySt { kKeyOff, kKeyOn, kKeyOnT, kKeyWarn, kKeyWarnOn, kKeyNa };     // off, on (amber), on (teal), warning, warning lit, n/a

struct P2 { double x, y; };

// one draw pass on a sketchpad in design px (k: surface px per design px); brushes and pens from the ship's ScreenFont, the text
// GOST type A (GostFont); the keys register their touch areas in hits
class Pad {
public:
    Pad(oapi::Sketchpad* s, ScreenFont& f, GostFont& g, double k, std::vector<Hit>* hits);
    ~Pad();
    Pad(const Pad&) = delete;
    Pad& operator=(const Pad&) = delete;
    double K() const { return k_; }
    // shapes; lw in design px
    void Fill(double x, double y, double w, double h, unsigned c);
    void Stroke(double x, double y, double w, double h, unsigned c, double lw);   // inside the rectangle (the mockup's box())
    void Line(double x0, double y0, double x1, double y1, unsigned c, double lw);
    void Poly(const std::vector<P2>& p, unsigned fill, unsigned edge = kNone, double lw = 0.0);   // a convex polygon
    void PolyLine(const std::vector<P2>& p, unsigned c, double lw);
    void Disc(double x, double y, double r, unsigned c);
    void Ring(double x, double y, double r, unsigned c, double lw);
    void Ellipse(double x, double y, double rx, double ry, unsigned fill, unsigned edge, double lw);
    void Arc(double x, double y, double r, double a0, double a1, unsigned c, double lw);   // canvas angles (clockwise, y down)
    void Arrow(double x0, double y0, double x1, double y1, unsigned c, double lw);
    void RRect(double x, double y, double w, double h, double r, unsigned fill, unsigned edge, double lw);   // 4 px corners
    // text: (x, baseline y), align 0 left / 1 centre / 2 right; bold = the mockup's 700 (GOST has one weight: drawn twice, a pixel
    // apart); the signs GOST.TTF lacks (minus, dashes, arrows, ×, ÷, ², ³, Δ, ▲▼▶◀) are drawn as lines and triangles
    static double CapH(double size) { return 0.684 * size; }   // GOST type A: the capital 1400 / 2048 em
    static double TW(const std::wstring& s, double size);
    double T(const std::wstring& s, double x, double y, unsigned c, double size = 17, int align = 0, bool bold = false);
    double TS(const std::wstring& s, double x, double y, unsigned c, double size = 18, bool bold = true, double sp = 1.0, int align = 0);   // spaced capitals
    struct Part { std::wstring s; double size; bool bold; unsigned c; double gap; };   // a run's piece; gap: the space before it
    static double RunW(const std::vector<Part>& p);
    double Run(const std::vector<Part>& p, double x, double y, int align);
    static int FitSize(const std::wstring& s, double mw, int size, int min = 15);    // the largest size from size down that fits
    static std::wstring FitTxt(const std::wstring& s, double mw, double size);        // cut with ".." when even that does not
    static std::vector<std::wstring> Wrap(const std::wstring& s, double w, double size);
    // the elements of SPEC.md
    void Key(double x, double y, double w, double h, const std::wstring& label, int st, int cmd, double val = 0.0, int size = 20);
    void Block(double x, double y, double w, double h, const std::wstring& title);
    static double BandBase(double y, double size = 18) { return y + kBandH / 2 + CapH(size) / 2; }
    void Lamp(double x, double y, unsigned c, double r = 9);
    void AddHit(const Hit& h) { if (hits_) hits_->push_back(h); }

private:
    int X(double v) const { return int(std::lround(v * k_)); }
    int Pw(double lw) const { const long w = std::lround(lw * k_); return w < 1 ? 1 : int(w); }
    double Special(wchar_t ch, double x, double y, double size, bool bold, unsigned c, bool draw);   // its width; drawn if draw
    oapi::Sketchpad* s_;
    ScreenFont& f_;
    GostFont& g_;
    double k_;
    std::vector<Hit>* hits_;
};

// the page tabs ПОЛЁТ | ДВИГАТЕЛИ | ПАРАМЕТРЫ (the same three keys on every page; cmd = the page)
void Tabs(Pad& p, double W, int tab);

// ---- ПОЛЁТ ----
enum FlightCmd { kFlHud = 1, kFlRcs, kFlTerm, kFlAck, kFlThrust };   // values: the HUD mode, the RCS mode, 0 space / 1 air, -, the level
struct FlightView {
    bool atmo = true;                    // the view: АТМОСФЕРА (tapes, the attitude indicator) or КОСМОС (the ball)
    bool termAuto = true;                // the view chosen by the altitude (or by hand)
    bool air = true;                     // there is air (МАХ shown)
    std::wstring ap;                     // the autopilot on (empty: РУЧНОЕ)
    int alert = 0;                       // the alerts: 0 none, 1 ВНИМАНИЕ, 2 ОПАСНОСТЬ; the newest active one's text
    std::wstring alertText;
    bool alertAcked = true, blink = false;
    double v = 0, alt = 0, vs = 0;       // speed, altitude, vertical speed [m/s, m]
    double hdg = 0, pitch = 0, bank = 0; // deg; bank + right wing down
    double aoa = 0, slip = 0, fpa = 0, mach = 0, pods = 0;   // deg (pods: the cups' angle)
    int hud = HUD_SURFACE, rcs = RCS_ROT;
    bool ana = false; int stage = 0, mass = 0;   // the main drive; the ignition stage; the reaction mass 0 argon, 1 iron, 2 products
    double thrAct = 0, thrSet = 0;       // the main thrust: given / set (0..1)
    double fuel = 0, g = 0, gLim = 5;    // fuel fraction, felt g, the g limit
    bool gLimOn = true;
};
class Flight {
public:
    void Draw(oapi::Sketchpad* skp, ScreenFont& font, GostFont& gost, double k, double W, const FlightView& v);
    int Hit(double x, double y, double* val) const { return FindHit(hits_, x, y, val); }

private:
    std::vector<front::Hit> hits_;
};

}  // namespace tantra::front
