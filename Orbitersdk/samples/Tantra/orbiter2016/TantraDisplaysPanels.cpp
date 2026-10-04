// TantraDisplays - the bridge panels (the user, 2026-10-04; Tantra_Design/tantra_bridge_panels_mockup.html):
//   screens 0 / 1, the side panels 1.16 x 0.96 m: three MFDs over a screen of the mockups (1600 x 800): on the left the
//   mechanisation (its tabs: ТЕПЛО, АВТОПИЛОТ to come), on the right the power plant, always;
//   screen 2, the front screen 1.50 x 0.65 m: ПОЛЁТ (the flight terminal: the HUD and RCS keys, the flight, the thrust) or
//   ДВИГАТЕЛИ (the engine console of the mockup), by two tabs in its top right corner.
// The panels are drawn in the mockups' own pixels: 1600 wide, the MFD band 428 high, the screen 800 under it.
#include "TantraDisplays.h"
#include "Tantra.h"
#include "SkpCompat.h"
#include "TantraScreenCanvas.h"

#if __has_include("gcCoreAPI.h")
#include "gcCoreAPI.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
using namespace tantra::scr;
using namespace tantra::scr::ui;
constexpr int kPanelW = 1600, kMfdBand = 428, kZoneH = 800;
enum { kHitMfd = 1000, kHitZone = 2000 };
// one MFD tile (b 0..2) of the band: its display, its 12 side keys, its 3 bottom keys
struct Tile { double dx, dy, ds, kw; };
Tile TileOf(int b) { const double tw = kPanelW / 3.0, ds = 356, kw = 44, x0 = b * tw + (tw - (ds + 2 * kw + 16)) / 2; return {x0 + kw + 8, 12, ds, kw}; }
}

void TantraDisplays::DrawPanel(int k) {
    SURFHANDLE s = s_[k];
    if (!s) return;
    Tantra* t = t_;
    oapiClearSurface(s, 0xFF000000 | 0x0a1311);
    std::vector<Hit>& hits = panelHits_[k];
    hits.clear();
    if (oapi::Sketchpad* skp = oapiGetSketchpad(s)) {
        {
            Canvas g(skp, font_, 0, 0);
            g.Fill(0, 0, kPanelW, kMfdBand, 0x0a1311);
            for (int b = 0; b < 3; ++b) {
                const int id = 3 * k + b;
                const Tile T = TileOf(b);
                g.Fill(T.dx - T.kw - 12, 4, T.ds + 2 * T.kw + 24, kMfdBand - 8, 0x0a1311); g.Stroke(T.dx - T.kw - 12, 4, T.ds + 2 * T.kw + 24, kMfdBand - 8, cFr, 2);
                if (id == selMfd_) g.Stroke(T.dx - T.kw - 9, 7, T.ds + 2 * T.kw + 18, kMfdBand - 14, 0xffc46a, 4);   // the one the right console drives
                g.Fill(T.dx, T.dy, T.ds, T.ds, 0x050b0a); g.Stroke(T.dx - 2, T.dy - 2, T.ds + 4, T.ds + 4, 0x2c6a54, 1.5);
                hits.push_back({T.dx, T.dy, T.ds, T.ds, kHitMfd + id * 16 + 15});                     // the MFD itself: a touch chooses it
                for (int i = 0; i < 12; ++i) {
                    const double kh = T.ds / 6, x = i < 6 ? T.dx - T.kw - 6 : T.dx + T.ds + 6, y = T.dy + (i % 6) * kh + 4;
                    g.Fill(x, y, T.kw, kh - 8, 0x0f1c19); g.Stroke(x, y, T.kw, kh - 8, 0x2c6a54, 1);
                    std::wstring lab = W1251(t->interior_.MfdLabel(id, i));
                    if (!lab.empty()) g.T(g.Fit(lab, T.kw - 4, 11, 700), x + T.kw / 2, y + (kh - 8) / 2 + 4, cTx, 11, 1, 700);
                    hits.push_back({x, y, T.kw, kh - 8, kHitMfd + id * 16 + i});
                }
                static const wchar_t* const kb[3] = {L"PWR", L"SEL", L"MNU"};
                for (int j = 0; j < 3; ++j) {
                    const double w = (T.ds - 16) / 3, x = T.dx + j * (w + 8), y = T.dy + T.ds + 10;
                    g.Fill(x, y, w, 40, 0x0f1c19); g.Stroke(x, y, w, 40, 0x2c6a54, 1); g.T(kb[j], x + w / 2, y + 26, cTx, 14, 1, 700);
                    hits.push_back({x, y, w, 40, kHitMfd + id * 16 + 12 + j});
                }
            }
        }
        // the screen under the MFDs
        if (k == kLeft) {
            tantra::mechscreen::View v; FillMechView(v);
            mechScr_.DrawTop(skp, font_, 0, kMfdBand, kPanelW, kZoneH, v);
        } else {
            tantra::plantscreen::View v; FillPlantView(v);
            plantScr_.Draw(skp, 0, kMfdBand, kPanelW, kZoneH, v, riserDt_);
        }
        oapiReleaseSketchpad(skp);
    }
    // the MFDs: blitted into a texture first (the sketchpad can't read an MFD surface), then stretched in with the brightness
    static SURFHANDLE tmp[6] = {};
    static int tmpRes = 0;
    const int res = t->interior_.MfdRes();
    if (tmpRes != res) { for (SURFHANDLE& q : tmp) if (q) { oapiDestroySurface(q); q = nullptr; } tmpRes = res; }
    SURFHANDLE src[3] = {};
    const RECT msr = {0, 0, res, res};
    for (int b = 0; b < 3; ++b)
        if (SURFHANDLE m = t->interior_.MfdDisplay(3 * k + b)) {
            const int ti = 3 * k + b;
            if (!tmp[ti]) tmp[ti] = oapiCreateSurfaceEx(res, res, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS);
            RECT r = msr;
            if (tmp[ti]) { oapiBlt(tmp[ti], m, &r, &r); src[b] = tmp[ti]; }
        }
    oapi::Sketchpad* sk = oapiGetSketchpad(s);
    tantra::Skp3* s3 = tantra::AsSkp3(sk);
    const double gG = MfdGain(), gM = MfdGamma();
    if (s3) { const FVECTOR4 br(float(gG), float(gG), float(gG * 1.04), 1.0f), gm(float(gM), float(gM), float(gM), 1.0f); s3->SetBrightness(&br); s3->SetRenderParam(tantra::kSkpGamma, &gm); }
    RECT dst[3];
    for (int b = 0; b < 3; ++b) { const Tile T = TileOf(b); dst[b] = {LONG(T.dx), LONG(T.dy), LONG(T.dx + T.ds), LONG(T.dy + T.ds)}; }
    for (int b = 0; b < 3; ++b) if (src[b] && s3) { RECT r = msr; s3->StretchRect(src[b], &r, &dst[b]); }
    if (s3) { s3->SetBrightness(nullptr); s3->SetRenderParam(tantra::kSkpGamma, nullptr); }
    if (sk) oapiReleaseSketchpad(sk);
    for (int b = 0; b < 3; ++b) if (src[b] && !s3) { RECT r = msr; oapiBlt(s, src[b], &dst[b], &r); }
#if __has_include("gcCoreAPI.h")
    if (gcCore2* gc = gcGetCoreInterface()) gc->GenerateMipmaps(s);
#endif
    if (dumpRiser_ && k == kRight && ++riserDraws_ == 80) oapiSaveSurface("Tantra_Design\\ingame_panelR", s, oapi::IMAGE_PNG);   // a check (the flag)
}

bool TantraDisplays::TouchPanel(int k, double x, double y) {
    // the screen under the MFDs: its own pixels
    if (y >= kMfdBand) {
        const double sy = y - kMfdBand;
        if (k == kLeft) { const int cmd = mechScr_.HitTop(x, sy); if (cmd < 0) return false; MechCommand(cmd); return true; }
        double along = 0.0; const int cmd = plantScr_.Hit(x, sy, &along);
        if (cmd < 0) return false;
        PlantCommand(cmd, along); tRiser_ = 0.0; return true;
    }
    const int cmd = HitTest(panelHits_[k], x, y, nullptr, 2.0);
    if (cmd >= kHitMfd && cmd < kHitZone) {                               // its keys press it; any touch chooses it for the right console
        const int c = cmd - kHitMfd; selMfd_ = c / 16;
        if (c % 16 < 15) t_->interior_.MfdPress(c / 16, c % 16);
        return true;
    }
    return false;
}

// ---- the front screen: ПОЛЁТ (the flight terminal as it was, bigger) | ДВИГАТЕЛИ (the mockup's engine console) ----
void TantraDisplays::DrawFrontTabs(SURFHANDLE s) {
    if (oapi::Sketchpad* skp = oapiGetSketchpad(s)) {
        int w = 0, h = 0; oapiGetSurfaceSize(s, &w, &h);
        Canvas g(skp, font_, 0, 0);
        frontHits_.clear();
        static const wchar_t* const kTab[2] = {L"ПОЛЁТ", L"ДВИГАТЕЛИ"};
        for (int i = 0; i < 2; ++i) {
            const double tw = 170, x = w - 14 - (2 - i) * (tw + 8), y = 8;   (void)h;
            Btn(g, frontHits_, kTab[i], x, y, tw, 40, frontTab_ == i, i, i ? cVi : cOr);
        }
        oapiReleaseSketchpad(skp);
    }
}

void TantraDisplays::DrawFront() {
    SURFHANDLE s = s_[kCentre];
    if (!s) return;
    if (frontTab_ == 0) DrawCentre();
    else {
        Tantra* t = t_;
        if (t->AnaIsMain()) {
            if (fsPend_ >= 0) { t->SetThrusterGroupLevel(THGROUP_MAIN, fsPend_); fsPend_ = -1; }
            if (frPend_ >= 0 && t->GetGroupThrusterCount(THGROUP_RETRO) > 0) { t->SetThrusterGroupLevel(THGROUP_RETRO, frPend_); frPend_ = -1; }
        }
        oapiClearSurface(s, 0xFF000000 | 0x0a1311);
        if (oapi::Sketchpad* skp = oapiGetSketchpad(s)) {
            tantra::enginescreen::View v; FillEngineView(v);
            int w = 0, h = 0; oapiGetSurfaceSize(s, &w, &h);
            engScr_.Draw(skp, font_, 0, 0, w, h, v);
            oapiReleaseSketchpad(skp);
        }
    }
    DrawFrontTabs(s);
#if __has_include("gcCoreAPI.h")
    if (gcCore2* gc = gcGetCoreInterface()) gc->GenerateMipmaps(s);
#endif
}

bool TantraDisplays::TouchFront(double px, double py) {
    const int tab = HitTest(frontHits_, px, py, nullptr, 2.0);
    if (tab >= 0) { frontTab_ = tab; return true; }
    if (frontTab_ == 0) return false;   // the flight terminal: the caller passes it on
    return TouchEngineScreen(px, py);
}
