// TantraScreen: the big screen (outside view), its button and the console MFD. See TantraScreen.h.
#include "TantraScreen.h"
#include "InteriorLayout.h"
#include <cmath>
using namespace tantra::interior;

namespace {
constexpr int kBtnArea = 1;                              // VC area id of the button
constexpr double kDeg = 3.14159265358979 / 180.0;
// The U-shaped screen: the three front zones share one wide camera (one picture, slot 2), the end walls their own, on
// TantraVC.msh; their cameras (yaw, pitch, fov, aspect, slot) come from the generator (kScreenZones), which maps the screen
// with the projection from the design eye of the bridge. The astronomer's screen (port wall, by the navigator) is a telescope.
constexpr double kTeleFovDeg = 2.0;
// The cameras sit 1 m ahead of the nose tip (station 177; mesh z 117) so that the hull never gets into the picture.
// a monitor seen on the user's display: the front screen spans ~1000-1500 display pixels, so ~485 rows are as sharp as
// it shows (the camera renders the world a second time - its pixels are what it costs: 5x fewer than the 1080 rows before)
constexpr int kH = 360, kFrontH = 485;                  // picture heights: the end walls, the front
constexpr double kCamY = 0.3, kCamZ = 118.0;
}  // namespace

void TantraScreen::Init(VESSEL* v, UINT vcMeshIdx) {
    v_ = v; vcMesh_ = vcMeshIdx;
    view_.Init(v);
    for (int i = 0; i < kScreenZoneCount; i++) {
        const ScreenZone& z = kScreenZones[i];
        const double y = z.yaw * kDeg, p = z.pitch * kDeg;
        const VECTOR3 dir = _V(std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p));
        const VECTOR3 up = _V(-std::sin(y) * std::sin(p), std::cos(p), -std::cos(y) * std::sin(p));
        const int h = i == 0 ? kFrontH : kH;                            // the front: one wide camera for the three front zones
        const int zi = view_.AddZone(vcMeshIdx, DWORD(z.slot), int(h * z.aspect + 0.5), h, _V(0.0, kCamY, kCamZ), dir, up, z.vfov);
        if (i > 0) view_.SetZoneOnDemand(zi, true);                     // the end walls: a camera only while looked at
    }
    tele_ = view_.AddZone(vcMeshIdx, DWORD(kAstroSlot), int(kH * kAstroAspect + 0.5), kH, _V(0.0, kCamY, kCamZ), _V(0, 0, 1), _V(0, 1, 0), kTeleFovDeg);
    view_.SetZoneLook(tele_, _V(-1.0, 0.1, -0.2), 40.0);                     // its picture: the astronomer's screen on the port wall
    view_.SetZoneOnDemand(tele_, true);
    view_.SetZoneEnabled(tele_, false);                                        // dark until the navigator switches it on
    view_.AimAt(tele_, oapiGetObjectByName(const_cast<char*>("Moon")));     // until the navigator's console chooses the target
    view_.SetZoneCamera(tele_, true);                                          // the telescope is a camera in every band
    // the concave monitor (the user's design): optical = the camera's picture on it, IR / UV a sensor picture on it, off
    // dark, data = pages, no camera; the HUD on its glass in front of the screen (transparent texture). The front camera
    // too only while the screen is seen from the capsule (on demand, kept 1.5 s after a glance away), 30 pictures a
    // second, half resolution while the screen is small on the display - its size from its own group
    view_.SetZoneGroup(0, DWORD(kGrpScreen[1]));
    view_.SetZoneGroup(1, DWORD(kGrpScreen[3]));
    view_.SetZoneGroup(2, DWORD(kGrpScreen[4]));
    view_.SetZoneOnDemand(0, true);
    view_.SetRefreshHz(30.0);
    view_.AddGlass(vcMeshIdx, DWORD(kHudSlot), int(540 * kHudAspect + 0.5), 540, 0);
    view_.SetBand(shipview::ViewScreen::kBandOptical);
    view_.SetOn(true);
}

void TantraScreen::OnVisual(VISHANDLE vis) {
    mesh_ = v_->GetDevMesh(vis, vcMesh_);
    view_.OnVisualCreated(vis);
    Lamps();
}

void TantraScreen::Lamps() {
    if (!mesh_) return;
    GROUPEDITSPEC e; e.flags = GRPEDIT_SETUSERFLAG;
    e.UsrFlag = view_.On() ? 0 : 2; oapiEditMeshGroup(mesh_, kGrpBtnOn, &e);      // user flag 2 = group not drawn
    e.UsrFlag = view_.On() ? 2 : 0; oapiEditMeshGroup(mesh_, kGrpBtnOff, &e);
}

void TantraScreen::Place(double meshDZ) {
    oapiVCSetAreaClickmode_Spherical(kBtnArea, _V(kBtnX, kBtnY, kBtnZ + meshDZ), 0.12);
    dz_ = meshDZ;
}

void TantraScreen::OnLoadVC(double meshDZ) {
    VCMFDSPEC spec; spec.nmesh = vcMesh_; spec.ngroup = kGrpMfd;
    oapiVCRegisterMFD(MFD_LEFT, &spec);
    oapiVCRegisterArea(kBtnArea, PANEL_REDRAW_NEVER, PANEL_MOUSE_LBDOWN);
    Place(meshDZ);
    registered_ = true;
    Lamps();
}

void TantraScreen::Update(double meshDZ) {
    view_.SetShift(_V(0, 0, meshDZ));
    if (registered_ && std::fabs(meshDZ - dz_) > 0.01) Place(meshDZ);
}

bool TantraScreen::OnMouse(int id, int event) {
    if (id != kBtnArea || !(event & PANEL_MOUSE_LBDOWN)) return false;
    view_.SetOn(!view_.On());
    Lamps();
    return true;
}
