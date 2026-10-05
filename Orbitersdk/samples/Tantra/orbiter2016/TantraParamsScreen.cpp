// TantraParamsScreen: see TantraParamsScreen.h. Draw() follows pageParams() of Tantra_Design/refine/front_v3.html block by
// block, Preview() its preview(), Stepper() its stepper(); the coordinates are the mockup's.
#include "TantraParamsScreen.h"

#include <algorithm>
#include <cmath>

namespace tantra::paramsscreen {

using namespace tantra::front;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr unsigned cSky = 0x0f2b2f, cGround = 0x0c1714;    // the preview's sky and ground

// the views: the name, the HUD mode it sets, the line over its key
struct ViewDef { const wchar_t* name; int hud; const wchar_t* sub; };
const ViewDef kView[4] = {{L"ГОРИЗОНТ", HUD_SURFACE, L"атмосфера и посадка: горизонт, тангаж, вектор скорости, ленты"},
                          {L"ОРБИТА", HUD_ORBIT, L"орбита: прогрейд, нормаль, апоцентр и перицентр"},
                          {L"СТЫКОВКА", HUD_DOCKING, L"сближение: цель, дальность, скорость сближения"},
                          {L"ВЫКЛ", HUD_NONE, L"чистый вид без HUD"}};
int ViewOf(int hud) { for (int i = 0; i < 4; ++i) if (kView[i].hud == hud) return i; return 3; }
// the palettes: the name, in lower case (ЧТО МЕНЯЕТСЯ), the primary and the accent colour
struct PalDef { const wchar_t* name; const wchar_t* lower; unsigned p, a; };
const PalDef kPal[kPaletteCount] = {{L"ОРАНЖ + ЦИАН", L"оранж + циан", kOr, kCy}, {L"ЦИАН + ОРАНЖ", L"циан + оранж", kCy, kOr}, {L"ИЗУМРУД", L"изумруд", kTx, kGr}};
const wchar_t* const kLayerName[kLayerCount] = {L"ТАНГАЖ", L"ВЕКТОР", L"ЛЕНТЫ", L"ТРЕВОГИ"};
struct RcsDef { const wchar_t* name; const wchar_t* lower; int mode; };
const RcsDef kRcs[3] = {{L"ВРАЩЕНИЕ", L"вращение", RCS_ROT}, {L"ЛИНЕЙНОЕ", L"линейное", RCS_LIN}, {L"ВЫКЛ", L"выкл", RCS_NONE}};
int RcsOf(int mode) { for (int i = 0; i < 3; ++i) if (kRcs[i].mode == mode) return i; return 2; }

// − / value / + side by side (SPEC: related keys together); the − key gives -1, the + key +1
void Stepper(Pad& g, double x, double y, const std::wstring& val, int cmd) {
    g.Key(x, y, 56, 48, L"", kKeyOff, cmd, -1); g.Line(x + 19, y + 24, x + 37, y + 24, kTx, 3);
    g.T(val, x + 56 + 46, y + 24 + Pad::CapH(22) / 2, kWh, 22, 1, true);
    g.Key(x + 148, y, 56, 48, L"", kKeyOff, cmd, +1); g.Line(x + 167, y + 24, x + 185, y + 24, kTx, 3); g.Line(x + 176, y + 15, x + 176, y + 33, kTx, 3);
}

// the main view in small: the sky, the ground, the chosen HUD in the chosen colours at its brightness (mixed: no alpha); the
// readouts the ship's own
void Preview(Pad& g, double x, double y, double w, double h, const View& v, int pal) {
    const double hy = y + std::round(h * 0.55), cx = x + w / 2;
    g.Fill(x, y, w, hy - y, cSky); g.Fill(x, hy, w, y + h - hy, cGround);
    const double a = (std::min)(1.0, 0.4 + 0.5 * v.hudGain);
    const unsigned cP = kPal[pal].p, cA = kPal[pal].a;
    auto over = [&](unsigned c, double yy) { return MixC(yy < hy ? cSky : cGround, c, a); };
    auto seg = [&](double x0, double y0, double x1, double y1, unsigned c) { g.Line(x0, y0, x1, y1, over(c, (y0 + y1) / 2), 2); };
    auto ln = [&](double x0, double y0, double x1, double y1, unsigned c) {   // a line, split where it crosses the horizon
        if ((y0 < hy) != (y1 < hy)) { const double xm = x0 + (x1 - x0) * (hy - y0) / (y1 - y0); seg(x0, y0, xm, hy, c); seg(xm, hy, x1, y1, c); }
        else seg(x0, y0, x1, y1, c);
    };
    auto rb = [&](double bx, double by, double bw, double bh, unsigned c) { ln(bx, by, bx + bw, by, c); ln(bx + bw, by, bx + bw, by + bh, c); ln(bx + bw, by + bh, bx, by + bh, c); ln(bx, by + bh, bx, by, c); };
    auto tx = [&](const std::wstring& s, double xx, double yy, unsigned c, int al) { g.T(s, xx, yy, over(c, yy), 15, al, true); };
    if (v.hud == HUD_SURFACE) {
        ln(x + 16, hy, x + w - 16, hy, cP);
        if (v.layer[kLayerLadder])
            for (int p : {-10, 10}) { const double yy = hy - p * 3.2; ln(cx - 46, yy, cx + 46, yy, cP); tx(L"10", cx - 52, yy + 5, cP, 2); tx(L"10", cx + 52, yy + 5, cP, 0); }
        tx(L"ТАНГАЖ " + NumS(v.pitch, 1) + L"°", x + 50, y + 24, cA, 0);
        tx(std::wstring(L"КРЕН ") + (v.bank >= 0 ? L"П " : L"Л ") + Num(std::fabs(v.bank), 1) + L"°", x + w - 50, y + 24, cA, 2);
        if (v.layer[kLayerFpm]) g.Ring(cx, hy - 12, 8, over(cA, hy - 12), 2);
        if (v.layer[kLayerTapes]) { rb(x + 10, y + 34, 26, h - 54, cP); rb(x + w - 36, y + 34, 26, h - 54, cP); }
    } else if (v.hud == HUD_ORBIT) {
        g.Arc(cx, hy, 14, kPi, 2 * kPi, over(cP, hy - 1), 2); g.Arc(cx, hy, 14, 0, kPi, over(cP, hy + 1), 2);
        ln(cx - 26, hy, cx - 14, hy, cP); ln(cx + 14, hy, cx + 26, hy, cP); ln(cx, hy - 26, cx, hy - 14, cP);
        tx(L"ПРОГРЕЙД", cx, hy + 38, cA, 1);
    } else if (v.hud == HUD_DOCKING) {
        rb(cx - 26, hy - 26, 52, 52, cP); ln(cx - 44, hy, cx + 44, hy, cA); ln(cx, hy - 44, cx, hy + 44, cA);
        tx(L"ЦЕЛЬ —", cx, hy + 66, cA, 1);                                 // the ship has no docking target data yet
    }
    g.Stroke(x, y, w, h, kFr, 2);
}

}  // namespace

double Stepped(int cmd, double now, double step) {
    auto clamp = [](double x, double lo, double hi) { return (std::max)(lo, (std::min)(hi, x)); };
    if (cmd == kCmdHudGain) return clamp(now + 0.1 * step, 0.4, 1.5);
    if (cmd == kCmdMfdGain) return clamp(now + 0.2 * step, 1.0, 3.0);
    if (cmd == kCmdMfdContrast) return clamp(now - 0.05 * step, 0.4, 1.0);
    return now;
}

void Screen::Draw(oapi::Sketchpad* skp, ScreenFont& font, GostFont& gost, double k, double W, const View& v) {
    hits_.clear();
    if (!skp) return;
    Pad g(skp, font, gost, k, &hits_);
    g.Fill(0, 0, W, kDesignH, kBg); g.Stroke(4, 4, W - 8, kDesignH - 8, kFr, 2);
    const Rect hb = HubRect(W);
    const double lw = hb.x0 - kGap - kM, rx = hb.x1 + kGap, rw = W - kM - rx;
    const int vi = ViewOf(v.hud), pal = (std::max)(0, (std::min)(kPaletteCount - 1, v.palette)), ri = RcsOf(v.rcs), un = v.units ? 1 : 0;
    const double contrast = 1.0 / (std::max)(0.05, v.mfdGamma);
    const double ix = kM + 16;
    // ---- left, top: the HUD - layers, colour, brightness, by the light ----
    if (lw >= 480) {
        g.Block(kM, kUY0, lw, kUY1 - kUY0, L"HUD: СЛОИ, ЦВЕТ, ЯРКОСТЬ");
        const double kx = ix + 150, kiw = kM + lw - 16 - kx;
        auto row = [&](int i) { return kUY0 + kBandH + 16 + i * 64.0; };
        auto lab = [&](const wchar_t* t, int i) { g.T(t, ix, row(i) + 24 + Pad::CapH(17) / 2, kTx, 17, 0, true); };
        lab(L"СЛОИ", 0);
        const double k4 = std::floor((kiw - 30) / 4 / 10) * 10;
        for (int i = 0; i < kLayerCount; ++i) g.Key(kx + i * (k4 + 10), row(0), k4, 48, kLayerName[i], v.layer[i] ? kKeyOn : kKeyOff, kCmdLayer, i);
        lab(L"ЦВЕТ", 1);
        const double k3 = std::floor((kiw - 20) / 3 / 10) * 10;
        for (int i = 0; i < kPaletteCount; ++i) {                          // the keys carry swatches of their two colours
            const double kx3 = kx + i * (k3 + 10), y1 = row(1);
            const bool on = pal == i;
            g.RRect(kx3 + 0.75, y1 + 0.75, k3 - 1.5, 46.5, 4, on ? kOr : 0x0f1c1a, on ? kOr : 0x2f5a52, 1.5);
            g.T(kPal[i].name, kx3 + k3 / 2, y1 + 20 + Pad::CapH(18) / 2, on ? kInk : kTx, Pad::FitSize(kPal[i].name, k3 - 14, 18), 1, true);
            g.Fill(kx3 + 12, y1 + 36, k3 / 2 - 14, 5, kPal[i].p); g.Fill(kx3 + k3 / 2 + 2, y1 + 36, k3 / 2 - 14, 5, kPal[i].a);
            g.AddHit({kx3, y1, k3, 48, kCmdPalette, double(i)});
        }
        lab(L"ЯРКОСТЬ", 2); Stepper(g, kx, row(2), Num(v.hudGain, 1), kCmdHudGain);
        lab(L"ПО СВЕТУ", 3);
        g.Key(kx, row(3), 120, 48, v.autoLum ? L"ВКЛ" : L"ВЫКЛ", v.autoLum ? kKeyOn : kKeyOff, kCmdAutoLum, 0);
        g.T(v.autoLum ? L"день · сумерки · ночь" : L"вручную", kx + 136, row(3) + 24 + Pad::CapH(17) / 2, kTx, 17);
    }
    // ---- centre, top: the main view in small, right under the big screen ----
    {
        g.Block(hb.x0, kUY0, hb.x1 - hb.x0, kUY1 - kUY0, L"ГЛАВНЫЙ ВИД");
        const double pvw = hb.x1 - hb.x0 - 32, pvh = std::round(pvw * 9 / 16);
        Preview(g, hb.x0 + 16, kUY0 + kBandH + 12, pvw, pvh, v, pal);
        g.T(L"так выглядит главный вид", (hb.x0 + hb.x1) / 2, kUY0 + kBandH + 12 + pvh + 28, kDim, 15, 1);
    }
    // ---- right, top: what the settings act on ----
    if (rw >= 320) {
        g.Block(rx, 88, rw, kUY1 - 88, L"ЧТО МЕНЯЕТСЯ");
        const std::wstring rows[5][2] = {
            {L"главный экран", std::wstring(L"HUD ") + kView[vi].name + L", цвет " + kPal[pal].lower},
            {L"MFD 1–6", L"яркость " + Num(v.mfdGain, 1) + L", контраст " + Num(contrast, 2) + L", " + Num(v.mfdRes) + L" px"},
            {L"освещение", v.autoLum ? L"следует за светом у корабля" : L"вручную"},
            {L"РСУ", kRcs[ri].lower},
            {L"единицы", un ? L"СИ + узлы, футы" : L"СИ"}};
        for (int i = 0; i < 5; ++i) {
            const double yb = 88 + kBandH + 8 + 16 + Pad::CapH(17) / 2 + i * 40;
            g.T(rows[i][0], rx + 16, yb, kDim, 17);
            g.T(Pad::FitTxt(rows[i][1], rw - 200, 22), rx + rw - 16, yb, kWh, 22, 2, true);
        }
    }
    // ---- left, bottom (the hand): the view on the big screen; its line over each key ----
    if (lw >= 480) {
        g.Block(kM, kLY0, lw, kLY1 - kLY0, L"ВИД НА ГЛАВНОМ ЭКРАНЕ");
        const double kv = std::floor((lw - 32 - 30) / 4 / 10) * 10, ky = kLY1 - 8 - 56;
        for (int i = 0; i < 4; ++i) {
            const std::vector<std::wstring> ls = Pad::Wrap(kView[i].sub, kv, 15);
            const size_t n = (std::min)(ls.size(), size_t(4));
            for (size_t j = 0; j < n; ++j) g.T(ls[j], ix + i * (kv + 10), ky - 14 - (n - 1 - j) * 22.0, vi == i ? kTx : kDim, 15);
        }
        for (int i = 0; i < 4; ++i) g.Key(ix + i * (kv + 10), ky, kv, 56, kView[i].name, vi == i ? kKeyOn : kKeyOff, kCmdHud, kView[i].hud);
    }
    // ---- right, bottom: the MFDs; the RCS and the units ----
    if (rw >= 560) {
        const double mw = std::round((rw - kGap) / 2), ux = rx + mw + kGap, uw = rw - mw - kGap;
        g.Block(rx, kLY0, mw, kLY1 - kLY0, L"MFD");
        auto mrow = [&](int i) { return kLY1 - 8 - 48 - (2 - i) * 58.0; };
        auto mlab = [&](const wchar_t* t, int i) { g.T(t, rx + 16, mrow(i) + 24 + Pad::CapH(17) / 2, kTx, 17, 0, true); };
        const double mkx = rx + mw - 16 - 204;
        mlab(L"ЯРКОСТЬ", 0); Stepper(g, mkx, mrow(0), Num(v.mfdGain, 1), kCmdMfdGain);
        mlab(L"КОНТРАСТ", 1); Stepper(g, mkx, mrow(1), Num(contrast, 2), kCmdMfdContrast);
        mlab(L"РАЗРЕШЕНИЕ", 2);
        for (int i = 0; i < 2; ++i) { const int r = i ? 2048 : 1024; g.Key(mkx + i * 107, mrow(2), 97, 48, Num(r), v.mfdRes == r ? kKeyOn : kKeyOff, kCmdMfdRes, r); }
        g.Block(ux, kLY0, uw, kLY1 - kLY0, L"РСУ И ЕДИНИЦЫ");
        const double u2 = kLY1 - 8 - 48, u1 = u2 - 32 - 48, kr = std::floor((uw - 32 - 20) / 3 / 10) * 10, ku = std::floor((uw - 32 - 10) / 2 / 10) * 10;
        g.T(L"РСУ", ux + 16, u1 - 8, kDim, 17, 0, true);
        for (int i = 0; i < 3; ++i) g.Key(ux + 16 + i * (kr + 10), u1, kr, 48, kRcs[i].name, ri == i ? kKeyOn : kKeyOff, kCmdRcs, kRcs[i].mode);
        g.T(L"ЕДИНИЦЫ", ux + 16, u2 - 8, kDim, 17, 0, true);
        static const wchar_t* const kUnits[2] = {L"СИ", L"СИ + УЗЛЫ, ФУТЫ"};
        for (int i = 0; i < 2; ++i) g.Key(ux + 16 + i * (ku + 10), u2, ku, 48, kUnits[i], un == i ? kKeyOn : kKeyOff, kCmdUnits, i);
    }
}

}  // namespace tantra::paramsscreen
