// TantraInterior - the commander's place of variant 7 (Tantra_Design/bridge_variants/v7.js, the user, 2026-10-04):
//   the seat: empty it stands 1.0 m back from the desk (room to stand up and step out between the side consoles), rides in when
//     he has sat down; his own place −0.10…+0.35 m and the pan's height −0.20…+0.05 m are set by a touch on two sensor strips on
//     the right armrest's top (ХОД, ВЫСОТА: the fill up to the value, the scale in cm, the nominal 0);
//   the cursor unit on the left armrest's front end: a press on its ball switches the light spot («солнечный зайчик») on, the
//     next one off; while it is on the ball glows, the spot follows the mouse over the screens (the light on the glass where the
//     cursor points; it stays where it was last when the cursor leaves the glasses) and the screens take a click at any distance -
//     so he reaches them from the seat; its small keys: ВВОД (press what the spot is on), ОТМ. (the MFDs' menus shut), ◄ / ►
//     (the cursor to the middle of the glass to the left / right), МЕНЮ (the button menu of the MFD the right console drives).
#include "TantraInterior.h"
#include "Tantra.h"
#include "InteriorLayout.h"
#include "TantraScreenCanvas.h"
#if __has_include("gcCoreAPI.h")
#include "gcCoreAPI.h"   // the render window (the mouse over the 3D view only)
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace tantra::interior;

namespace {
using namespace tantra::scr;
constexpr unsigned kMain = 0xb8ecff, kDimB = 0x6f9fb8, kAcc = 0xffc46a, kWhite = 0xe6f6ff, kStripBg = 0x03080a, kHousing = 0x0a1114, kGlassOff = 0x051017;
// the cursor unit's plate (texture px of the seat's panel: x from the plate's left edge, the strips take the first 2 x 60 px):
// x, y (centre), w, h of ВВОД, ОТМ., ◄, МЕНЮ, ► (the mockup's places, 2000 px/m) and the ball's socket
struct KeyR { double x, y, w, h; const wchar_t* lab; };
const KeyR kCurKey[5] = {{246, 98, 36, 30, L"ВВОД"}, {246, 146, 36, 26, L"ОТМ."}, {50, 34, 34, 26, L""}, {120, 34, 40, 26, L"МЕНЮ"}, {190, 34, 34, 26, L""}};
constexpr double kBallX = 120, kBallY = 130, kBallR = 46;
constexpr int kStripMb = 28, kStripMt = 23;                         // the strips' scales: margins at the bottom and the top (px)
}  // namespace

double TantraInterior::CmdSeatOffset() const { return kSeatTravel[0] * seatPos_[0] + AdjOffset(); }
double TantraInterior::CmdSeatHeight() const { return kSeatHgtMin + (kSeatHgtMax - kSeatHgtMin) * hgtPos_; }
bool TantraInterior::CmdAtDesk() const { return seat_ == 0 && person_ && seatPos_[0] >= 0.999; }

// The seat's panel texture: the strip ХОД (x 0..60), the strip ВЫСОТА (60..120), the cursor unit's plate (120..400).
void TantraInterior::DrawAdj() {
    if (!adjSurf_) return;
    const double now = oapiGetSysTime();
    oapiClearSurface(adjSurf_, 0xFF000000);
    oapi::Sketchpad* skp = oapiGetSketchpad(adjSurf_);
    if (!skp) return;
    {
        Canvas g(skp, pfont_, 0, 0);
        const double H = kSeatPanelH;
        auto strip = [&](double x0, double val, double lo, double hi, int step, const wchar_t* title) {
            const double w = kSeatStripWid * kSeatPanelPx;
            auto yOf = [&](double v) { return H - kStripMb - (v - lo) / (hi - lo) * (H - kStripMb - kStripMt); };
            g.Fill(x0, 0, w, H, kStripBg);
            const double y = yOf(val);
            g.Fill(x0 + 6, y, 22, H - kStripMb - y, Mix(kMain, kStripBg, 0.5));                 // the fill up to the value
            for (int i = int(std::lround(lo * 100)); i <= int(std::lround(hi * 100)); ++i) {    // the scale: every cm, every 5 cm longer
                const double yy = yOf(i / 100.0); const bool big = i % 5 == 0;
                g.Line(x0 + 6, yy, x0 + (big ? 33 : 22), yy, kDimB, big ? 2 : 1);
                if (i % step == 0) {
                    wchar_t b[8]; std::swprintf(b, 8, L"%s%d", i > 0 ? L"+" : i < 0 ? L"-" : L"", std::abs(i));
                    gost_.Text(skp, int(x0 + 46), int(yy + 4), b, 12, kDimB, 1);
                }
            }
            g.Line(x0 + 3, yOf(0), x0 + 36, yOf(0), kAcc, 2);                                    // the nominal place / height
            g.Line(x0 + 3, y, x0 + 39, y, kWhite, 3);                                            // where it is now
            gost_.Text(skp, int(x0 + w / 2), int(H - 8), title, 11, kDimB, 1);
        };
        strip(0, AdjOffset(), kSeatAdjMin, kSeatAdjMax, 10, L"ХОД");
        strip(kSeatStripWid * kSeatPanelPx, CmdSeatHeight(), kSeatHgtMin, kSeatHgtMax, 5, L"ВЫСОТА");
        const double px0 = 2 * kSeatStripWid * kSeatPanelPx;                                     // the cursor unit's plate
        g.Fill(px0, 0, kCurPlateW * kSeatPanelPx, H, kHousing);
        if (spotOn_) for (int i = 4; i >= 1; --i) g.Disc(px0 + kBallX, kBallY, kBallR + 4 + i * 5, Mix(kMain, kHousing, 0.10 + 0.08 * (4 - i)));   // its light on the plate
        g.Disc(px0 + kBallX, kBallY, kBallR + 4, spotOn_ ? Mix(kMain, kHousing, 0.6) : 0x050a0c);   // the socket
        g.Circle(px0 + kBallX, kBallY, kBallR + 4, spotOn_ ? kWhite : Mix(kMain, kHousing, 0.35), 2);
        for (int k = 0; k < 5; ++k) {                                                            // the small glass keys
            const KeyR& r = kCurKey[k];
            const bool on = curLit_[k] > now;
            const double x = px0 + r.x - r.w / 2, y = r.y - r.h / 2;
            g.Fill(x, y, r.w, r.h, on ? Mix(kMain, kGlassOff, 0.85) : kGlassOff);
            g.Stroke(x + 2, y + 2, r.w - 4, r.h - 4, on ? kWhite : Mix(kMain, kGlassOff, 0.5), 2);
            const unsigned fg = on ? 0x03141c : kMain;
            if (k == 2 || k == 4) {                                                              // ◄ ►: drawn (the font has no arrows)
                const double cx = px0 + r.x, cy = r.y, s = k == 2 ? -1.0 : 1.0;
                g.Shape({{cx + s * 7, cy}, {cx - s * 5, cy - 7}, {cx - s * 5, cy + 7}}, fg);
            } else {
                const double em = (std::min)(r.h * 0.55, (r.w - 6) / (std::max)(1.0, tantra::GostFont::Width(r.lab, 1.0)));
                gost_.Text(skp, int(px0 + r.x), int(r.y + em * 0.36), r.lab, int(em), fg, 1);
            }
        }
    }
    oapiReleaseSketchpad(skp);
}

// The ray from the camera through the mouse cursor (as OrbiterCrew's click), in the interior frame; only over Orbiter's 3D view
// and only while the camera is inside this ship (the seated commander's own).
bool TantraInterior::HoverRay(VECTOR3& o, VECTOR3& d) const {
    if (!oapiCameraInternal()) return false;
    POINT p; if (!GetCursorPos(&p)) return false;
    HWND w = WindowFromPoint(p); DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
#if __has_include("gcCoreAPI.h")
    static gcCore2* core = nullptr; static bool tried = false;
    if (!tried) { tried = true; core = gcGetCoreInterface(); }
    const HWND view = core ? core->GetRenderWindow() : nullptr;
#else
    const HWND view = nullptr;
#endif
    RECT rc{};
    const HWND area = view ? view : w;                                   // the 3D view's own client area (not a child window's over it)
    if (!w || pid != GetCurrentProcessId() || (view && w != view && !IsChild(view, w)) || !ScreenToClient(area, &p) || !GetClientRect(area, &rc) || rc.right <= 0 || rc.bottom <= 0) return false;
    const double W = rc.right, H = rc.bottom, fpx = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
    VECTOR3 cp; oapiCameraGlobalPos(&cp); MATRIX3 Rc; oapiCameraRotationMatrix(&Rc);
    const VECTOR3 dg = mul(Rc, unit(_V((p.x - W / 2) / fpx, (H / 2 - p.y) / fpx, 1.0)));
    v_->Global2Local(cp, o); o.z -= DZ();                                // ship -> interior frame
    MATRIX3 Rs; v_->GetRotationMatrix(Rs);
    d = tmul(Rs, dg);
    return true;
}

// The spot on a touch screen now (the screens paint it into their picture: TantraDisplays).
bool TantraInterior::SpotScreen(int& screen, double& u, double& v) const {
    if (!(spotOn_ && seat_ == 0 && person_ != 0) || spotHit_.kind != 2) return false;
    const TouchFacet& q = kTouchFacets[spotHit_.idx];
    screen = q.screen; u = q.u0 + spotHit_.u * (q.u1 - q.u0); v = q.v0 + spotHit_.v * (q.v1 - q.v0);
    return true;
}

// Every frame: the ball dark / lit, the seat's panel when it changed, the light spot where the mouse points.
void TantraInterior::SpotStep() {
    const bool on = spotOn_ && seat_ == 0 && person_ != 0;
    const double now = oapiGetSysTime();
    int lit = 0; for (int k = 0; k < 5; ++k) if (curLit_[k] > now) lit |= 1 << k;
    const double key = std::lround(adjPos_ * 500) + std::lround(hgtPos_ * 500) * 1000.0 + (spotOn_ ? 1e6 : 0.0) + lit * 1e7;
    if (key != panelKey_) { panelKey_ = key; DrawAdj(); }
    if (!mesh_) return;
    if (ballShown_ != int(spotOn_)) {                                    // the ball glows while the spot is on
        ballShown_ = spotOn_;
        GROUPEDITSPEC e = {}; e.flags = GRPEDIT_SETUSERFLAG;
        e.UsrFlag = spotOn_ ? 2 : 0; if (kCurBallGrp >= 0) oapiEditMeshGroup(mesh_, DWORD(kCurBallGrp), &e);   // user flag 2: not drawn
        e.UsrFlag = spotOn_ ? 0 : 2; if (kCurLitGrp >= 0) oapiEditMeshGroup(mesh_, DWORD(kCurLitGrp), &e);
    }
    if (!spotOn_) spotHit_ = Pick();
    if (on) {
        Pick h; VECTOR3 o, d;
        if (HoverRay(o, d) && PickScreens(o, d, nullptr, h)) spotHit_ = h;   // off the glasses it stays where it was
        if (spotHit_.kind < 0) {                                         // just switched on: the middle of the front glass
            int best = -1; double bd = 1e9;
            const VECTOR3 c2 = _V(kTouch[2].c[0], kTouch[2].c[1], kTouch[2].c[2]);
            for (int k = 0; k < kTouchFacetCount; ++k) {
                const TouchFacet& q = kTouchFacets[k];
                if (q.screen != 2) continue;
                const double dd = length(_V(q.c[0], q.c[1], q.c[2]) - c2);
                if (dd < bd) { bd = dd; best = k; }
            }
            if (best >= 0) {
                const TouchFacet& q = kTouchFacets[best];
                spotHit_.kind = 2; spotHit_.idx = best; spotHit_.u = spotHit_.v = 0.5;
                spotHit_.p = _V(q.c[0], q.c[1], q.c[2]); spotHit_.ex = _V(q.ex[0], q.ex[1], q.ex[2]); spotHit_.up = _V(q.up[0], q.up[1], q.up[2]); spotHit_.n = _V(q.n[0], q.n[1], q.n[2]);
            }
        }
    }
    const bool show = on && spotHit_.kind >= 0 && kSpotGrp >= 0;
    if (show) {                                                          // the discs on the glass, 4 mm toward him
        static NTVERTEX vtx[2 * kSpotDiscs * 13];
        const VECTOR3 c = spotHit_.p + spotHit_.n * 0.004;
        const double pulse = 0.95 + 0.05 * std::sin(now * 9.0);           // the sun spot shimmers
        for (int s = 0; s < 2; ++s)
            for (int k = 0; k < kSpotDiscs; ++k) {
                NTVERTEX* v = vtx + (s * kSpotDiscs + k) * 13;
                v[0].x = float(c.x); v[0].y = float(c.y); v[0].z = float(c.z);
                for (int i = 0; i < 12; ++i) {
                    static const double kLaserR[kSpotDiscs] = {0.0010, 0.0016, 0.0024, 0.0034, 0.0046};   // a laser pointer's dot (the user, 2026-10-05: «тонкая»), not the sun spot's kSpotR
                    const double a = 2 * PI * i / 12, r = kLaserR[k] * pulse;
                    const VECTOR3 q = c + (spotHit_.ex * std::cos(a) + spotHit_.up * std::sin(a)) * r;
                    v[1 + i].x = float(q.x); v[1 + i].y = float(q.y); v[1 + i].z = float(q.z);
                }
            }
        GROUPEDITSPEC e = {}; e.flags = GRPEDIT_VTXCRD; e.nVtx = DWORD(2 * kSpotDiscs * 13); e.vIdx = nullptr; e.Vtx = vtx;
        oapiEditMeshGroup(mesh_, DWORD(kSpotGrp), &e);
    }
    if (kSpotGrp >= 0 && spotShown_ != int(show)) {
        spotShown_ = show;
        GROUPEDITSPEC e = {}; e.flags = GRPEDIT_SETUSERFLAG; e.UsrFlag = show ? 0 : 2;
        oapiEditMeshGroup(mesh_, DWORD(kSpotGrp), &e);
    }
}

// A touch on the cursor unit's plate (the seat panel's px): its keys and its ball.
void TantraInterior::SeatPlate(double px, double py) {
    const double x = px - 2 * kSeatStripWid * kSeatPanelPx, now = oapiGetSysTime();
    if (std::hypot(x - kBallX, py - kBallY) <= kBallR + 6) { spotOn_ = !spotOn_; panelKey_ = -1.0; return; }
    for (int k = 0; k < 5; ++k) {
        const KeyR& r = kCurKey[k];
        if (std::fabs(x - r.x) > r.w / 2 + 3 || std::fabs(py - r.y) > r.h / 2 + 3) continue;
        curLit_[k] = now + 0.25;
        switch (k) {
            case 0: if (spotOn_ && spotHit_.kind >= 0) PressAt(spotHit_); break;              // ВВОД: what the spot is on
            case 1: if (touch_.Touch) touch_.Touch(touch_.ctx, 8, 1.0, 0.0); break;           // ОТМ.: the MFDs' menus shut
            case 3: if (touch_.Touch) touch_.Touch(touch_.ctx, 8, 0.0, 0.0); break;           // МЕНЮ: the chosen MFD's menu
            case 2: WarpToGlass(-1); break;
            case 4: WarpToGlass(1); break;
        }
        return;
    }
}

// ◄ / ►: the mouse cursor (the spot follows it) to the middle of the glass to the left / right of the one it is on (L, F, R).
void TantraInterior::WarpToGlass(int dir) {
    static const int kOrder[3] = {0, 2, 1};
    const int cur = spotHit_.kind == 2 ? kTouchFacets[spotHit_.idx].screen : 2;
    int i = 1; for (int k = 0; k < 3; ++k) if (kOrder[k] == cur) i = k;
    int next = -1;
    for (int s = 1; s <= 2 && next < 0; ++s) { const int k = kOrder[((i + dir * s) % 3 + 3) % 3]; if (k == 2 || SideDisplayUsable(k)) next = k; }
    if (next < 0) return;
#if __has_include("gcCoreAPI.h")
    gcCore2* core = gcGetCoreInterface();
    const HWND view = core ? core->GetRenderWindow() : nullptr;
#else
    const HWND view = nullptr;
#endif
    RECT rc{};
    if (!view || !GetClientRect(view, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
    double c[3] = {kTouch[next].c[0], kTouch[next].c[1], kTouch[next].c[2]};
    if (next < 2) {                                                      // a side glass in ЛЕНТА: the middle of its part over the desk
        for (int j = 0; j < 3; ++j) c[j] += kSideRise[next][j] * sidePos_[next];
        if (c[1] < kDeskTopY + 0.12) { const double up = kDeskTopY + 0.12 - c[1]; for (int j = 0; j < 3; ++j) c[j] += kTouch[next].up[j] * up / (std::max)(0.3, kTouch[next].up[1]); }
    }
    VECTOR3 G; v_->Local2Global(_V(c[0], c[1], c[2] + DZ()), G);
    VECTOR3 cp; oapiCameraGlobalPos(&cp); MATRIX3 Rc; oapiCameraRotationMatrix(&Rc);
    const VECTOR3 q = tmul(Rc, G - cp);                                  // global -> camera (x right, y up, z forward)
    if (q.z < 0.05) return;
    const double W = rc.right, H = rc.bottom, fpx = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
    POINT pt = {LONG(W / 2 + q.x / q.z * fpx), LONG(H / 2 - q.y / q.z * fpx)};
    if (pt.x < 0 || pt.y < 0 || pt.x >= rc.right || pt.y >= rc.bottom) return;   // not in the view: the cursor stays
    ClientToScreen(view, &pt);
    SetCursorPos(pt.x, pt.y);
}
