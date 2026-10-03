// ShipView::ViewScreen - see ViewScreen.h.
#include "ViewScreen.h"
#if __has_include("gcCoreAPI.h")   // Orbiter 2024: gcAPI became the gcCore interface (no library, bound at run time)
#include "gcCoreAPI.h"
static bool gcInitialize() { return gcGetCoreInterface() != nullptr; }
static bool gcEnabled() { return pCoreInterface != nullptr; }
static DWORD gcClientID() { return 0x2024; }
static CAMERAHANDLE gcSetupCustomCamera(CAMERAHANDLE c, OBJHANDLE h, VECTOR3& p, VECTOR3& d, VECTOR3& u, double fov, SURFHANDLE s, DWORD f) {
    return pCoreInterface->SetupCustomCamera(c, h, p, d, u, fov, s, f);
}
static void gcCustomCameraOnOff(CAMERAHANDLE c, bool on) { pCoreInterface->CustomCameraOnOff(c, on); }
static int gcDeleteCustomCamera(CAMERAHANDLE c) { return pCoreInterface->DeleteCustomCamera(c); }
#else                               // Orbiter 2016: gcAPI.h + gcAPI.lib
#include "gcAPI.h"
#endif

#include <cmath>

namespace shipview {

namespace {
constexpr double kDeg = 3.14159265358979 / 180.0;
}

int ViewScreen::AddZone(UINT meshIdx, DWORD texSlot, int w, int h, VECTOR3 pos, VECTOR3 dir, VECTOR3 up, double vfovDeg) {
    Zone z; z.mesh = meshIdx; z.slot = texSlot; z.w = w; z.h = h; z.pos = pos; z.dir = dir; z.up = up; z.vfov = vfovDeg;
    z.look = dir; z.lookHalf = std::atan(std::tan(0.5 * vfovDeg * kDeg) * std::sqrt(1.0 + double(w) * w / (double(h) * h)));
    zones_.push_back(z);
    return int(zones_.size()) - 1;
}

bool ViewScreen::Start() {
    if (tried_) return gc_;
    tried_ = true;
    gc_ = gcInitialize() && gcEnabled();
    oapiWriteLogV("ShipView: %s (client %08X)", gc_ ? "custom cameras available" : "NO custom cameras (D3D9Client + CustomCamMode = 1 needed)",
                  unsigned(gc_ ? gcClientID() : 0));
    for (Zone& z : zones_) {
        z.surf = oapiCreateSurfaceEx(z.w, z.h, OAPISURFACE_RENDER3D | OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS | (z.over ? OAPISURFACE_SKETCHPAD : 0));
        if (z.over) z.raw = oapiCreateSurfaceEx(z.w, z.h, OAPISURFACE_RENDER3D | OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS);
        if (!z.surf) oapiWriteLogV("ShipView: could not create a %dx%d render surface", z.w, z.h);
        else Mark(z, gc_ ? "" : "NO CAMERA", 0xFF101820);
    }
    return gc_;
}

void ViewScreen::Mark(Zone& z, const char* text, DWORD bg) {
    oapiClearSurface(z.surf, bg);
    if (!text[0]) return;
    if (oapi::Sketchpad* skp = oapiGetSketchpad(z.surf)) {
        skp->SetTextColor(0x4060FF); skp->SetTextAlign(oapi::Sketchpad::CENTER, oapi::Sketchpad::BASELINE);
        skp->Text(z.w / 2, z.h / 2, text, int(strlen(text)));
        oapiReleaseSketchpad(skp);
    }
}

void ViewScreen::Aim(Zone& z, bool force) {
    if (!gc_ || !z.surf) return;
    VECTOR3 pos = z.pos + shift_, dir = z.dir, up = z.up;
    OBJHANDLE h = v_->GetHandle();
    if (host_ && host_ != h && oapiIsVessel(host_)) {                   // ship frame -> the host's frame
        VESSEL* hv = oapiGetVesselInterface(host_);
        MATRIX3 Rs, Rh; v_->GetRotationMatrix(Rs); hv->GetRotationMatrix(Rh);
        VECTOR3 g; v_->Local2Global(pos, g); hv->Global2Local(g, pos);
        dir = tmul(Rh, mul(Rs, dir)); up = tmul(Rh, mul(Rs, up));
        h = host_;
    }
    // The client renders the cameras some frames late and one at a time: re-aiming every frame makes the zones disagree
    // (seams flicker). Only a noticeable change of the pose is passed on.
    if (!force && z.cam && z.aimed && length(pos - z.lastPos) < 0.3 && dotp(dir, z.lastDir) > 0.999994 && dotp(up, z.lastUp) > 0.999994) return;
    z.lastPos = pos; z.lastDir = dir; z.lastUp = up; z.aimed = true;
    // gcAPI takes the half of the vertical field of view (like oapiCameraAperture)
    z.cam = gcSetupCustomCamera(z.cam, h, pos, dir, up, 0.5 * z.vfov * kDeg, z.raw ? z.raw : z.surf, CUSTOMCAM_DEFAULTS);
    if (!z.cam) oapiWriteLogV("ShipView: gcSetupCustomCamera failed");
    else gcCustomCameraOnOff(z.cam, true);
}

void ViewScreen::Bind() {
    if (!vis_) return;
    for (Zone& z : zones_) {
        if (!z.surf) continue;
        DEVMESHHANDLE m = v_->GetDevMesh(vis_, z.mesh);
        const bool ok = m && oapiSetTexture(m, z.slot, z.surf);
        oapiWriteLogV("ShipView: mesh %u texture slot %u -> %s", z.mesh, unsigned(z.slot), ok ? "bound" : (m ? "oapiSetTexture FAILED" : "no device mesh"));
    }
}

void ViewScreen::OnVisualCreated(VISHANDLE vis) {
    vis_ = vis;
    Start();
    Bind();
    Cull();                                                              // creates the cameras wanted now
    for (Zone& z : zones_) if (z.cam) Aim(z);
}

void ViewScreen::OnVisualDestroyed() { vis_ = nullptr; }

void ViewScreen::SetZoneView(int i, VECTOR3 pos, VECTOR3 dir, VECTOR3 up) {
    if (i < 0 || i >= int(zones_.size())) return;
    Zone& z = zones_[i]; z.pos = pos; z.dir = dir; z.up = up;
    if (z.cam) Aim(z);
}

void ViewScreen::SetShift(VECTOR3 s) {
    if (std::fabs(s.x - shift_.x) + std::fabs(s.y - shift_.y) + std::fabs(s.z - shift_.z) < 0.01) return;
    shift_ = s;
    for (Zone& z : zones_) if (z.cam) Aim(z);
}

void ViewScreen::SetHost(OBJHANDLE host) {
    if (host == v_->GetHandle()) host = nullptr;
    if (host == host_) return;
    host_ = host;
    for (Zone& z : zones_) if (z.cam) Aim(z);
}

void ViewScreen::AimAt(int i, OBJHANDLE target) {
    if (i >= 0 && i < int(zones_.size())) zones_[i].target = target;
}

void ViewScreen::SetZoneFov(int i, double vfovDeg) {
    if (i < 0 || i >= int(zones_.size())) return;
    zones_[i].vfov = vfovDeg;
    if (zones_[i].cam) Aim(zones_[i]);
}

void ViewScreen::SetZoneLook(int i, VECTOR3 dir, double halfDeg) {
    if (i < 0 || i >= int(zones_.size())) return;
    zones_[i].look = dir / length(dir); zones_[i].lookHalf = halfDeg * kDeg;
}

// The client renders one custom camera per turn, going round ALL the cameras it has - a camera switched off still takes its
// turn. So a zone not needed has no camera at all: the end walls get theirs only while the viewer looks at them (on demand),
// a switched-off zone or a screen turned off none. Fewer cameras = each picture fresher.
void ViewScreen::Cull() {
    if (!gc_) return;
    VECTOR3 g; oapiCameraGlobalDir(&g);
    MATRIX3 R; v_->GetRotationMatrix(R);
    const VECTOR3 gaze = tmul(R, g);                                       // the viewer's gaze in the ship (= mesh) frame
    DWORD vw = 0, vh = 0; oapiGetViewportSize(&vw, &vh);
    const double a = vh ? double(vw) / vh : 1.6;
    const double half = std::atan(std::tan(oapiCameraAperture()) * std::sqrt(1.0 + a * a));   // the view's half diagonal
    for (Zone& z : zones_) {
        if (!z.surf) continue;
        bool want = on_ && z.enabled;
        if (want && z.demand) {
            const double ang = std::acos((std::max)(-1.0, (std::min)(1.0, dotp(gaze, z.look))));
            const bool sight = ang - z.lookHalf < half + (z.cam ? 15.0 : 5.0) * kDeg;   // (a little hysteresis)
            want = sight;
        }
        if (want && !z.cam) Aim(z);
        else if (!want && z.cam) { gcDeleteCustomCamera(z.cam); z.cam = nullptr; z.aimed = false; }
    }
}

void ViewScreen::SetZoneOverlay(int i, Overlay fn, void* ctx) {
    if (i >= 0 && i < int(zones_.size())) { zones_[i].over = fn; zones_[i].overCtx = ctx; }
}

void ViewScreen::Frame() {
    Cull();
    if (!gc_ || !on_) return;
    for (Zone& z : zones_) {
        if (!z.cam) continue;
        if (z.over && z.raw && z.surf) {                                   // the picture (last rendered), then the overlay over it
            oapiBlt(z.surf, z.raw, 0, 0, 0, 0, DWORD(z.w), DWORD(z.h));
            z.over(z.overCtx, z.surf, z.w, z.h, z.dir, z.up, z.vfov);
        }
        if (z.target) {                                                    // telescope: towards the target, the ship's 'up' kept
            VECTOR3 cg, tg; v_->Local2Global(z.pos + shift_, cg); oapiGetGlobalPos(z.target, &tg);
            MATRIX3 R; v_->GetRotationMatrix(R);
            VECTOR3 d = tmul(R, tg - cg); const double n = length(d);
            if (n < 1.0) continue;
            d /= n;
            VECTOR3 up = _V(0, 1, 0) - d * d.y; if (length(up) < 1e-3) up = _V(0, 0, 1) - d * d.z;
            z.dir = d; z.up = up / length(up);
            Aim(z, false);
        } else if (host_) Aim(z, false);                                   // a walking host: only on a noticeable change
    }
}

void ViewScreen::SetOn(bool on) {
    if (on == on_) return;
    on_ = on;
    Cull();
    if (!on_) for (Zone& z : zones_) if (z.surf) Mark(z, "", 0xFF000000);
    oapiWriteLogV("ShipView: screen %s", on_ ? "on" : "off");
}

void ViewScreen::SetZoneOnDemand(int i, bool demand) { if (i >= 0 && i < int(zones_.size())) zones_[i].demand = demand; }

void ViewScreen::SetZoneEnabled(int i, bool en) {
    if (i < 0 || i >= int(zones_.size()) || zones_[i].enabled == en) return;
    zones_[i].enabled = en;
    Cull();
    if (!en && zones_[i].surf) Mark(zones_[i], "", 0xFF000000);         // a switched-off zone: dark
}

void ViewScreen::Shutdown() {
    for (Zone& z : zones_) if (z.cam) { gcDeleteCustomCamera(z.cam); z.cam = nullptr; }      // cameras before their surfaces
    for (Zone& z : zones_) if (z.surf) { oapiDestroySurface(z.surf); z.surf = nullptr; }
    for (Zone& z : zones_) if (z.raw) { oapiDestroySurface(z.raw); z.raw = nullptr; }
}

}  // namespace shipview
