// ShipView::MfdBank - see ShipMfd.h.
#include "ShipMfd.h"

#include <cstring>

namespace shipview {

// A touch MFD: the display over the whole surface, the button labels drawn on its edges (see HitButton). The resolution is
// the bank's (res_: 1024 by default, 2048 on request - 256 was blurred on the big monitors); the surfaces follow it.

// ExternMFD that stays with the ship whatever is in focus; tells the bank when to redraw.
class MfdBank::Sticky : public ExternMFD {
public:
    Sticky(const MFDSPEC& spec, OBJHANDLE ship) : ExternMFD(spec), ship_(ship) { SetVessel(ship); }
    void clbkFocusChanged(OBJHANDLE) override { if (GetVessel() != ship_) SetVessel(ship_); }
    void clbkRefreshDisplay(SURFHANDLE) override { dirty = true; }
    void clbkRefreshButtons() override { dirty = true; }
    bool dirty = true;
private:
    OBJHANDLE ship_;
};

int MfdBank::Add(DWORD texSlot, int mode) {
    Unit u; u.slot = texSlot; u.mode = mode;
    m_.push_back(u);
    return int(m_.size()) - 1;
}

void MfdBank::Start() {
    if (started_) return;
    started_ = true;
    // the buttons' rows: v = .19 - (k + .5) * .05 on a screen .30 high (v .19 .. -.11) -> 256 px: first at 21, every 43 px
    const int kDisp = res_;
    MFDSPEC spec = {{0, 0, kDisp, kDisp}, 6, 6, kDisp * 21 / 256, kDisp * 43 / 256};
    for (Unit& u : m_) {
        u.mfd = new Sticky(spec, v_->GetHandle());
        oapiRegisterExternMFD(u.mfd, spec);
        u.mfd->SetMode(u.mode);
    }
    if (!font_) font_ = oapiCreateFont(26 * res_ / 512, false, "Arial", FONT_BOLD);
    oapiWriteLogV("ShipView: %d console MFDs registered", int(m_.size()));
}

void MfdBank::OnVisualCreated(VISHANDLE vis) {
    vis_ = vis;
    DEVMESHHANDLE dm = v_->GetDevMesh(vis, mesh_);
    int bound = 0;
    for (Unit& u : m_) {
        if (!u.tex && u.slot) u.tex = oapiCreateSurfaceEx(res_, res_, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_NOMIPMAPS);
        if (u.slot && dm && u.tex && oapiSetTexture(dm, u.slot, u.tex)) bound++;
        if (u.mfd) u.mfd->dirty = true;
    }
    oapiWriteLogV("ShipView: console MFD screens bound %d of %d", bound, int(m_.size()));
}

// Touch layout (u right, v down, 0..1 over the whole square screen): the side buttons are the strips u < .14 and u > .86,
// rows at v = .10 + .15 k; PWR / SEL / MNU are the bottom strip v > .91 (thirds).
int MfdBank::HitButton(double u, double v) {
    if (u < 0 || u > 1 || v < 0 || v > 1) return -1;
    if (v > .91) return 12 + (u < 1.0 / 3 ? 0 : u < 2.0 / 3 ? 1 : 2);
    if (u > .14 && u < .86) return -1;
    int row = int((v - .025) / .15); if (row < 0) row = 0; if (row > 5) row = 5;
    return (u < .5 ? 0 : 6) + row;
}

bool MfdBank::Touch(int i, double u, double v) {
    const int b = HitButton(u, v);
    if (b < 0) return false;
    Press(i, b);
    return true;
}

// A new resolution: the MFDs resized (their modes kept), the textures made again at it.
void MfdBank::SetRes(int px) {
    if (px == res_ || (px != 1024 && px != 2048)) return;
    res_ = px;
    const MFDSPEC spec = {{0, 0, res_, res_}, 6, 6, res_ * 21 / 256, res_ * 43 / 256};
    for (Unit& u : m_) {
        if (u.mfd) { u.mfd->Resize(spec); u.mfd->dirty = true; }
        if (u.tex) { oapiDestroySurface(u.tex); u.tex = nullptr; }
    }
    if (font_) { oapiReleaseFont(font_); font_ = oapiCreateFont(26 * res_ / 512, false, "Arial", FONT_BOLD); }
    if (vis_) OnVisualCreated(vis_);
    oapiWriteLogV("ShipView: console MFDs at %d px", res_);
}

void MfdBank::Compose(Unit& u) {
    const int kTex = res_;
    oapiClearSurface(u.tex, 0xFF000000);
    if (u.mfd && u.mfd->Active()) {
        RECT tr = {0, 0, kTex, kTex}, sr = {0, 0, kTex, kTex};
        if (SURFHANDLE d = u.mfd->GetDisplaySurface()) oapiBlt(u.tex, d, &tr, &sr);
    }
    oapi::Sketchpad* skp = oapiGetSketchpad(u.tex);
    if (!skp) return;
    skp->SetFont(font_); skp->SetBackgroundMode(oapi::Sketchpad::BK_OPAQUE); skp->SetBackgroundColor(0x101010);
    skp->SetTextColor(0x30A0E6);
    for (int b = 0; b < 12; b++) {                                       // the labels on the screen's edges, at their rows
        const char* l = u.mfd ? u.mfd->GetButtonLabel(b) : nullptr;
        if (!l || !l[0]) continue;
        const int y = int((.10 + .15 * (b % 6)) * kTex) - 12 * kTex / 512;
        skp->SetTextAlign(b < 6 ? oapi::Sketchpad::LEFT : oapi::Sketchpad::RIGHT);
        skp->Text(b < 6 ? 6 * kTex / 512 : kTex - 6 * kTex / 512, y, l, int(std::strlen(l)));
    }
    static const char* kBot[3] = {"PWR", "SEL", "MNU"};
    skp->SetTextColor(0x70C8E0); skp->SetTextAlign(oapi::Sketchpad::CENTER);
    for (int k = 0; k < 3; k++) skp->Text(int(kTex * (k + .5) / 3), kTex - 34 * kTex / 512, kBot[k], 3);
    oapiReleaseSketchpad(skp);
}

SURFHANDLE MfdBank::Display(int i) const {
    if (i < 0 || i >= int(m_.size()) || !m_[i].mfd || !m_[i].mfd->Active()) return nullptr;
    return m_[i].mfd->GetDisplaySurface();
}

void MfdBank::Step() {
    if (!vis_) return;
    for (Unit& u : m_)
        if (u.slot && u.tex && u.mfd && u.mfd->dirty) { u.mfd->dirty = false; Compose(u); }
}

void MfdBank::Press(int i, int b) {
    if (i < 0 || i >= int(m_.size()) || !m_[i].mfd) return;
    Sticky* m = m_[i].mfd;
    switch (b) {
        case 12: m->SendKey(OAPI_KEY_ESCAPE); break;                     // PWR
        case 13: m->SendKey(OAPI_KEY_F1); break;                         // SEL: the mode list
        case 14: m->SendKey(OAPI_KEY_GRAVE); break;                      // MNU: the button menu
        default:
            if (b >= 0 && b < 12) { m->ProcessButton(b, PANEL_MOUSE_LBDOWN); m->ProcessButton(b, PANEL_MOUSE_LBUP); }
            break;
    }
    m->dirty = true;
}

const char* MfdBank::Label(int i, int b) const {
    static const char* kBot[3] = {"PWR", "SEL", "MNU"};
    if (b >= 12 && b < 15) return kBot[b - 12];
    if (i < 0 || i >= int(m_.size()) || !m_[i].mfd || b < 0 || b >= 12) return "";
    const char* l = m_[i].mfd->GetButtonLabel(b);
    return l ? l : "";
}

void MfdBank::Shutdown() {
    for (Unit& u : m_) {
        if (u.mfd) { oapiUnregisterExternMFD(u.mfd); u.mfd = nullptr; }   // Orbiter deletes the instance
        if (u.tex) { oapiDestroySurface(u.tex); u.tex = nullptr; }
    }
    if (font_) { oapiReleaseFont(font_); font_ = nullptr; }
}

}  // namespace shipview
