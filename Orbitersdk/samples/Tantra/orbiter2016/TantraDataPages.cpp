// TantraDisplays - the main screen's ДАННЫЕ band (the user, 2026-10-05: «погасить камеры, показывать только данные»): no
// cameras, every zone of the big screen is a page (TantraScreen::SetPages). Zone 0, the front arc: a synthetic horizon from the
// ship's attitude (no picture: sky / ground, the pitch ladder, the heading tape, the flight path marker), the speed on the left,
// the height on the right, the unacknowledged alerts under it. Zone 1, the port wall (the flight engineer's side): the energy
// core's nodes (core/TantraCore). Zone 2, the starboard wall (the astronavigator's side): the position and the orbit.
// Every value is the ship's; the screen is read from ~3.5 m (≈ 180 px per metre on the arc): big letters.
#include "TantraDisplays.h"
#include "Tantra.h"
#include "TantraScreenCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using namespace tantra::scr;

constexpr double kPi = 3.14159265358979323846, kDeg = kPi / 180.0;
// the helmet's blue photonics (as the consoles), the sky and the ground of the synthetic view
constexpr unsigned kBg = 0x061014, kMain = 0xb8ecff, kDim = 0x6f9fb8, kAcc = 0xffc46a, kRed = 0xff3b30, kWhite = 0xe6f6ff, kGreen = 0x4dd88a;
constexpr unsigned kSky = 0x123a5c, kGround = 0x4a3320, kLine = 0x2a4a5c;

// text with the GOST font; a leading '-' is drawn as a line (the font has no minus)
void Txt(Canvas& g, tantra::GostFont& f, double x, double y, const std::wstring& s0, double em, unsigned col, int align) {
    std::wstring s = s0;
    const bool neg = !s.empty() && s[0] == L'-';
    if (neg) s = s.substr(1);
    const double wTxt = tantra::GostFont::Width(s, em), wMin = neg ? em * 0.62 : 0.0, wAll = wTxt + wMin;
    const double x0 = align == 1 ? x - wAll / 2 : align == 2 ? x - wAll : x;
    if (neg) g.Line(x0 + em * 0.08, y - em * 0.34, x0 + em * 0.48, y - em * 0.34, col, (std::max)(2.0, em * 0.09));
    f.Text(g.Skp(), int(x0 + wMin), int(y), s, int(em), col, 0);
}

void Clear(SURFHANDLE s, unsigned rgb) { oapiClearSurface(s, 0xFF000000 | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF)); }

// the convex part of a rectangle on one side of a line (n . p >= c): the ground under the horizon
std::vector<Pt> ClipRect(double x0, double y0, double x1, double y1, double nx, double ny, double c) {
    const Pt r[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    std::vector<Pt> out;
    for (int i = 0; i < 4; ++i) {
        const Pt a = r[i], b = r[(i + 1) % 4];
        const double da = nx * a.x + ny * a.y - c, db = nx * b.x + ny * b.y - c;
        if (da >= 0) out.push_back(a);
        if ((da >= 0) != (db >= 0)) { const double t = da / (da - db); out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}); }
    }
    return out;
}

const wchar_t* const kStateRu[6] = {L"выкл", L"готов", L"работа", L"предел", L"отказ", L"потерян"};
unsigned StateCol(int st) { return st == 2 ? kGreen : st == 1 ? kMain : st == 3 ? kAcc : st >= 4 ? kRed : kDim; }

}  // namespace

void TantraDisplays::DrawDataPage(int zone, SURFHANDLE s, int w, int h) {
    if (!s || w <= 0 || h <= 0) return;
    Tantra* t = t_;
    Clear(s, kBg);
    oapi::Sketchpad* skp = oapiGetSketchpad(s);
    if (!skp) return;
    {
        Canvas g(skp, font_, 0, 0);
        g.Fill(0, 0, w, h, kBg);
        const double k = h / 485.0;                                          // the layout is for the front arc 1620 x 485
        if (zone == 0) {
            // ---- the synthetic horizon in the middle ----
            const double hx0 = w * 0.28, hx1 = w * 0.72, hy0 = 64 * k, hy1 = h - 70 * k;
            const double cx = (hx0 + hx1) / 2, cy = (hy0 + hy1) / 2, ppd = (hy1 - hy0) / 50.0;   // px per degree: +-25 deg seen
            const double pitch = t->GetPitch() / kDeg, bank = t->GetBank() / kDeg;
            const double sb = -std::sin(bank * kDeg), cb = std::cos(bank * kDeg);   // (the bank's sign as the ПОЛЁТ page's horizon)
            // the horizon: through the point pitch*ppd under the centre (nose up: the horizon goes down), turned by -bank
            const double ox = cx + sb * pitch * ppd, oy = cy + cb * pitch * ppd;   // a point of the horizon
            const double nx = sb, ny = cb;                                       // its normal toward the ground (screen y down)
            g.Fill(hx0, hy0, hx1 - hx0, hy1 - hy0, kSky);
            const std::vector<Pt> gnd = ClipRect(hx0, hy0, hx1, hy1, nx, ny, nx * ox + ny * oy);
            if (gnd.size() >= 3) g.Shape(gnd, kGround);
            // the pitch ladder every 10 deg (dashed under the horizon), clipped to the window
            auto inWin = [&](double x, double y) { return x > hx0 + 4 && x < hx1 - 4 && y > hy0 + 4 && y < hy1 - 4; };
            for (int p = -80; p <= 80; p += 10) {
                if (p == 0) continue;
                const double d = (pitch - p) * ppd;                              // the rung's offset from the centre along the "down" normal
                const double rx = cx + sb * d, ry = cy + cb * d, half = (p % 20 ? 70 : 110) * k;
                const double ax = rx - cb * half, ay = ry + sb * half, bx = rx + cb * half, by = ry - sb * half;
                if (!inWin(rx, ry)) continue;
                if (p > 0) g.Line(ax, ay, bx, by, kWhite, 3 * k);
                else g.Dashed(ax, ay, bx, by, kWhite, 3 * k, 16 * k, 10 * k);
                if (inWin(bx + cb * 40 * k, by)) Txt(g, gost_, bx + cb * 14 * k, by - sb * 14 * k + 10 * k, std::to_wstring(std::abs(p)), 26 * k, kWhite, 0);
            }
            {   // the horizon line itself, across the window
                const double L = (hx1 - hx0);
                const double ax = ox - cb * L, ay = oy + sb * L, bx = ox + cb * L, by = oy - sb * L;
                // clip the long line to the window by sampling (the line is straight: its two window crossings)
                std::vector<Pt> pts;
                for (int i = 0; i <= 400; ++i) { const double u = i / 400.0, x = ax + (bx - ax) * u, y = ay + (by - ay) * u; if (inWin(x, y)) pts.push_back({x, y}); }
                if (pts.size() >= 2) g.Line(pts.front().x, pts.front().y, pts.back().x, pts.back().y, kWhite, 4 * k);
            }
            // the flight path marker: where the ship goes (its velocity against the nose: angle of attack and slip)
            {
                const double aoa = t->GetAOA() / kDeg, slip = t->GetSlipAngle() / kDeg;
                VECTOR3 vh; const bool air = t->GetHorizonAirspeedVector(vh);
                if (air && length(vh) > 5.0) {
                    const double fx = cx + (std::max)(-200.0, (std::min)(200.0, slip * ppd)), fy = cy + (std::max)(-200.0, (std::min)(200.0, aoa * ppd));
                    g.Circle(fx, fy, 14 * k, kGreen, 3 * k);
                    g.Line(fx - 34 * k, fy, fx - 14 * k, fy, kGreen, 3 * k); g.Line(fx + 14 * k, fy, fx + 34 * k, fy, kGreen, 3 * k); g.Line(fx, fy - 14 * k, fx, fy - 28 * k, kGreen, 3 * k);
                }
            }
            // the ship: a fixed W at the centre
            g.Line(cx - 90 * k, cy, cx - 34 * k, cy, kAcc, 6 * k); g.Line(cx - 34 * k, cy, cx - 17 * k, cy + 17 * k, kAcc, 6 * k);
            g.Line(cx - 17 * k, cy + 17 * k, cx, cy, kAcc, 6 * k); g.Line(cx, cy, cx + 17 * k, cy + 17 * k, kAcc, 6 * k);
            g.Line(cx + 17 * k, cy + 17 * k, cx + 34 * k, cy, kAcc, 6 * k); g.Line(cx + 34 * k, cy, cx + 90 * k, cy, kAcc, 6 * k);
            g.Stroke(hx0, hy0, hx1 - hx0, hy1 - hy0, kLine, 3 * k);
            // the heading tape over the window
            {
                double hdg = 0.0; oapiGetHeading(t->GetHandle(), &hdg); hdg /= kDeg;
                const double ty = hy0 - 8 * k, ppdH = (hx1 - hx0) / 60.0;           // +-30 deg
                g.Fill(hx0, 4 * k, hx1 - hx0, hy0 - 12 * k, kBg);
                for (int d = int(std::floor(hdg - 30)); d <= int(std::ceil(hdg + 30)); ++d) {
                    if (d % 5) continue;
                    const double x = cx + (d - hdg) * ppdH;
                    if (x < hx0 + 2 || x > hx1 - 2) continue;
                    g.Line(x, ty, x, ty - (d % 10 ? 10 : 18) * k, kDim, 2 * k);
                    if (d % 10 == 0 && std::fabs(x - cx) > 60 * k) { const int dd = ((d % 360) + 360) % 360; wchar_t b[8]; std::swprintf(b, 8, L"%03d", dd); Txt(g, gost_, x, ty - 24 * k, b, 22 * k, kDim, 1); }
                }
                g.Fill(cx - 56 * k, 6 * k, 112 * k, 44 * k, kBg); g.Stroke(cx - 56 * k, 6 * k, 112 * k, 44 * k, kAcc, 3 * k);
                wchar_t b[8]; std::swprintf(b, 8, L"%03d", (int(std::lround(hdg)) % 360 + 360) % 360);
                Txt(g, gost_, cx, 42 * k, b, 34 * k, kAcc, 1);
            }
            // ---- the speed (left) and the height (right) ----
            const bool atmo = t->GetAtmDensity() > 1e-6;
            const double gNow = t->structG_;
            auto block = [&](double x, double y, double wd, const wchar_t* lab, const std::wstring& val, const wchar_t* unit, unsigned col) {
                Txt(g, gost_, x + wd - 10 * k, y, lab, 24 * k, kDim, 2);
                Txt(g, gost_, x + wd - 10 * k - 70 * k, y + 64 * k, val, 60 * k, col, 2);
                Txt(g, gost_, x + wd - 10 * k, y + 64 * k, unit, 24 * k, kDim, 2);
            };
            const double lx = 24 * k, lw = w * 0.28 - 48 * k, rx = w * 0.72 + 24 * k, rw = w * 0.28 - 48 * k;
            OBJHANDLE ref = t->GetSurfaceRef();
            if (atmo) {
                block(lx, 70 * k, lw, L"ВОЗДУШНАЯ СКОРОСТЬ", Fmt(t->GetAirspeed(), 0), L"м/с", kMain);
                block(lx, 190 * k, lw, L"ЧИСЛО МАХА", Fmt(t->GetMachNumber(), 2), L"", kMain);
                block(lx, 310 * k, lw, L"СКОРОСТНОЙ НАПОР", Fmt(t->GetDynPressure() / 1e3, 1), L"кПа", t->GetDynPressure() > 15e3 ? kAcc : kMain);
            } else {
                VECTOR3 rv; t->GetRelativeVel(ref, rv);
                block(lx, 70 * k, lw, L"СКОРОСТЬ ОТНОС. ТЕЛА", Fmt(length(rv) / 1e3, 3), L"км/с", kMain);
                ELEMENTS el; ORBITPARAM op;
                if (ref && t->GetElements(ref, el, &op)) {
                    block(lx, 190 * k, lw, L"НАКЛОНЕНИЕ", Fmt(el.i / kDeg, 2), L"°", kMain);
                    block(lx, 310 * k, lw, L"ПЕРИОД", op.T > 0 && op.T < 1e8 ? Fmt(op.T / 60.0, 1) : std::wstring(L"—"), L"мин", kMain);
                }
            }
            {
                const double alt = t->GetAltitude(ALTMODE_GROUND);
                VECTOR3 vh; t->GetHorizonAirspeedVector(vh);
                if (atmo || alt < 100e3) {
                    block(rx, 70 * k, rw, L"ВЫСОТА НАД ГРУНТОМ", alt < 10e3 ? Fmt(alt, 0) : Fmt(alt / 1e3, 2), alt < 10e3 ? L"м" : L"км", alt < 300 ? kAcc : kMain);
                    block(rx, 190 * k, rw, L"ВЕРТИКАЛЬНАЯ СКОРОСТЬ", Fmt(vh.y, 1), L"м/с", vh.y < -20 && alt < 2000 ? kRed : kMain);
                } else {
                    ELEMENTS el; ORBITPARAM op; const double R = ref ? oapiGetSize(ref) : 0.0;
                    if (ref && t->GetElements(ref, el, &op)) {
                        block(rx, 70 * k, rw, L"ПЕРИЦЕНТР", Fmt((op.PeD - R) / 1e3, 1), L"км", op.PeD - R < 100e3 ? kAcc : kMain);
                        block(rx, 190 * k, rw, L"АПОЦЕНТР", op.ApD > 0 ? Fmt((op.ApD - R) / 1e3, 1) : std::wstring(L"—"), L"км", kMain);
                    }
                }
                block(rx, 310 * k, rw, L"ПЕРЕГРУЗКА", Fmt(gNow, 2), L"g", gNow > 3.0 ? kRed : gNow > 2.0 ? kAcc : kMain);
            }
            // ---- the alerts under the window: the unacknowledged, the worst first ----
            {
                const Alert* worst = nullptr;
                for (const Alert& a : alerts_) if (a.active && !a.ack && (!worst || a.level > worst->level)) worst = &a;
                if (worst) {
                    const unsigned col = worst->level >= 2 ? kRed : kAcc;
                    g.Fill(hx0, hy1 + 10 * k, hx1 - hx0, 50 * k, col == kRed ? 0x3a0a08 : 0x3a2a08);
                    Txt(g, gost_, cx, hy1 + 46 * k, W1251(worst->text), 30 * k, col, 1);
                }
            }
        } else if (zone == 1) {
            // ---- the energy core: its nodes, two columns ----
            const tantra::tcore::Snapshot& c = t->CoreState();
            const double kk = h / 360.0;
            Txt(g, gost_, 16 * kk, 36 * kk, L"ЭНЕРГОЯДРО", 28 * kk, kAcc, 0);
            Txt(g, gost_, w - 16 * kk, 36 * kk, L"тяга " + Fmt(c.thrust / 1e6, 0) + L" МН", 24 * kk, kMain, 2);
            struct N { const wchar_t* n; const tantra::tcore::Node* nd; std::wstring v; };
            const N nodes[] = {
                {L"ВЭУ", &c.veu, Fmt(c.veuPower / 1e9, 2) + L" ГВт"}, {L"НАКОПИТЕЛЬ", &c.store, Fmt(c.storeMax > 0 ? 100.0 * c.storeE / c.storeMax : 0.0, 0) + L" %"},
                {L"ТОПЛИВО", &c.fuel, Fmt(c.fuelKg / 1e6, 2) + L" кт"}, {L"ПОДАЧА", &c.feed, Fmt(c.capsHz, 0) + L" Гц"},
                {L"ТРИГГЕР", &c.trigger, Fmt(c.trigPower / 1e12, 2) + L" ТВт"}, {L"ОБМОТКА", &c.coil, Fmt(c.B, 1) + L" Тл"},
                {L"КРИОГЕНИКА", &c.cryo, Fmt(c.coilT, 1) + L" К"}, {L"РАБ. МАССА", &c.mass, Fmt(c.mdot / 1e3, 1) + L" т/с"},
                {L"НАСОСЫ", &c.pump, Fmt(c.pumpHealth * 100, 0) + L" %"}, {L"РУБАШКА", &c.jacket, Fmt(c.tOut, 0) + L" К"},
                {L"ЧАША", &c.cup, Fmt(c.cupHealth * 100, 0) + L" %"}, {L"КОРМА", &c.stern, Fmt(c.sternT, 0) + L" К"},
                {L"РАДИАТОРЫ", &c.rad, Fmt(c.radiated / 1e6, 0) + L" МВт"}};
            const int n = int(sizeof nodes / sizeof nodes[0]), rows = (n + 1) / 2;
            const double rh = (h - 60 * kk) / rows, colW = w / 2.0;
            for (int i = 0; i < n; ++i) {
                const double x = (i / rows) * colW + 14 * kk, y = 56 * kk + (i % rows) * rh;
                const unsigned col = StateCol(nodes[i].nd->state);
                g.Disc(x + 9 * kk, y + rh * 0.42, 8 * kk, col);
                Txt(g, gost_, x + 26 * kk, y + rh * 0.42 + 8 * kk, nodes[i].n, 20 * kk, kMain, 0);
                Txt(g, gost_, x + colW - 26 * kk, y + rh * 0.42 + 8 * kk, nodes[i].v, 20 * kk, col, 2);
            }
        } else {
            // ---- the position and the orbit ----
            const double kk = h / 360.0;
            OBJHANDLE ref = t->GetSurfaceRef();
            char nm[64] = "—"; if (ref) oapiGetObjectName(ref, nm, 64);
            Txt(g, gost_, 16 * kk, 36 * kk, L"НАВИГАЦИЯ", 28 * kk, kAcc, 0);
            Txt(g, gost_, w - 16 * kk, 36 * kk, W1251(nm), 24 * kk, kMain, 2);
            double lng = 0, lat = 0, rad = 0; t->GetEquPos(lng, lat, rad);
            VECTOR3 gs; t->GetGroundspeedVector(FRAME_HORIZON, gs);
            ELEMENTS el; ORBITPARAM op; const bool orb = ref && t->GetElements(ref, el, &op);
            const double R = ref ? oapiGetSize(ref) : 0.0;
            struct L { const wchar_t* n; std::wstring v; };
            const L rows[] = {{L"широта", Fmt(lat / kDeg, 3) + L"°"}, {L"долгота", Fmt(lng / kDeg, 3) + L"°"},
                              {L"путевая скорость", Fmt(std::sqrt(gs.x * gs.x + gs.z * gs.z), 0) + L" м/с"},
                              {L"перицентр", orb ? Fmt((op.PeD - R) / 1e3, 1) + L" км" : std::wstring(L"—")},
                              {L"апоцентр", orb && op.ApD > 0 ? Fmt((op.ApD - R) / 1e3, 1) + L" км" : std::wstring(L"—")},
                              {L"наклонение", orb ? Fmt(el.i / kDeg, 2) + L"°" : std::wstring(L"—")}};
            const int n = int(sizeof rows / sizeof rows[0]);
            const double rh = (h - 60 * kk) / n;
            for (int i = 0; i < n; ++i) {
                const double y = 56 * kk + i * rh + rh * 0.62;
                Txt(g, gost_, 18 * kk, y, rows[i].n, 22 * kk, kDim, 0);
                Txt(g, gost_, w - 18 * kk, y, rows[i].v, 26 * kk, kMain, 2);
                g.Line(14 * kk, 56 * kk + (i + 1) * rh - 2, w - 14 * kk, 56 * kk + (i + 1) * rh - 2, kLine, 1.5);
            }
        }
    }
    oapiReleaseSketchpad(skp);
}
