// ShipView::ViewScreen - see ViewScreen.h.
#include "ViewScreen.h"
#include <algorithm>
#if __has_include("DrawAPI.h")
#include "DrawAPI.h"   // the colour matrix, gamma, noise of the D3D9 pad (IR / UV)
#endif
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
        if (z.glass) {                                                   // a HUD glass: transparent, drawn on, no camera
            z.surf = oapiCreateSurfaceEx(z.w, z.h, OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_ALPHA | OAPISURFACE_SKETCHPAD | OAPISURFACE_NOMIPMAPS);
            if (z.surf) oapiClearSurface(z.surf, 0x00000000);
            continue;
        }
        z.sw = (std::max)(64, int(z.w * kSensorScale)); z.sh = (std::max)(64, int(z.h * kSensorScale));
        z.sens = oapiCreateSurfaceEx(z.sw, z.sh, OAPISURFACE_RENDER3D | OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS);
        z.surf = oapiCreateSurfaceEx(z.w, z.h, OAPISURFACE_RENDER3D | OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS | OAPISURFACE_SKETCHPAD);
        if (!z.camera) z.half = oapiCreateSurfaceEx((std::max)(64, z.w / 2), (std::max)(64, z.h / 2), OAPISURFACE_RENDER3D | OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS);
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
    z.cam = gcSetupCustomCamera(z.cam, h, pos, dir, up, 0.5 * z.vfov * kDeg, Target(z), CUSTOMCAM_DEFAULTS);
    if (!z.cam) oapiWriteLogV("ShipView: gcSetupCustomCamera failed");
    else { gcCustomCameraOnOff(z.cam, true); z.camOn = true; z.onFrames = 2; z.shotT = oapiGetSysTime(); }
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
    VECTOR3 eg; oapiCameraGlobalPos(&eg);
    VECTOR3 eye; v_->Global2Local(eg, eye);                                // the viewer's eye in the ship frame
    const double now = oapiGetSysTime();
    for (Zone& z : zones_) {
        if (!z.surf || z.glass) continue;
        bool want = on_ && z.enabled && Sensing(z) && (z.camera || present_);
        if (want && z.demand) {
            // in sight now (with 15 deg to spare) - or within the last 1.5 s: a glance away and back does not drop it,
            // and a camera that goes leaves its last picture on the screen (no flash of black)
            const VECTOR3 look = z.geom ? z.gc + shift_ - eye : z.look;
            const double ln = length(look);
            const double ang = std::acos((std::max)(-1.0, (std::min)(1.0, dotp(gaze, ln > 1e-6 ? look / ln : z.look))));
            const double sz = z.geom && ln > 0.5 ? std::atan(0.5 * z.gw / ln) : z.lookHalf;
            if (ang - sz < half + 15.0 * kDeg) z.seenT = now;
            want = now - z.seenT < 1.5;
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
    ShowGroups();
    const double now = oapiGetSysTime();
    // under time warp over x10 the screens refresh less often (the user): the simulation keeps its frame time
    const bool warped = oapiGetTimeAcceleration() > 10.0;
    const double hz = warped ? (std::min)(hz_, 10.0) : hz_;
    const bool sensorTick = (band_ == kBandIR || band_ == kBandUV) && now - sensorT_ >= 1.0 / (warped ? kSensorHz * 0.5 : kSensorHz);
    if (sensorTick) sensorT_ = now;
    for (Zone& z : zones_) {                                               // the HUD glasses: cleared, the overlay drawn
        if (!z.glass || !z.surf) continue;
        oapiClearSurface(z.surf, 0x00000000);
        const bool show = on_ && band_ != kBandOff && z.from >= 0 && z.from < int(zones_.size());
        if (show) {
            const Zone& f = zones_[z.from];
            if (f.over) f.over(f.overCtx, z.surf, z.w, z.h, f.dir, f.up, f.vfov);
        }
    }
    if (band_ == kBandData && on_ && now - pageT_ >= (warped ? 0.5 : 0.2)) {               // the data pages: 5 times a second, no camera
        pageT_ = now;
        for (int i = 0; i < int(zones_.size()); ++i) {
            Zone& z = zones_[i];
            if (z.glass || z.camera || !z.surf || !z.page) continue;
            z.page(z.pageCtx, i, z.surf, z.w, z.h);
        }
    }
    if (!gc_ || !on_) return;
    int active = 0;
    for (const Zone& z : zones_) active += z.cam ? 1 : 0;
    for (Zone& z : zones_) {
        if (z.glass) continue;
        if (z.cam && sensorTick && !z.camera) Process(z);
        if (!z.cam) continue;
        // the monitor's refresh: the client renders one camera per frame going round them all, so a camera stays on for
        // as many frames as there are cameras (its turn comes), then off until its next 1/hz - off, its turn costs nothing
        if (!z.camera) {
            if (now - z.shotT >= 1.0 / hz) {
                if (!z.camOn) { gcCustomCameraOnOff(z.cam, true); z.camOn = true; }
                z.shotT = now; z.onFrames = active;
            } else if (z.camOn && --z.onFrames < 0) { gcCustomCameraOnOff(z.cam, false); z.camOn = false; }
            Resolution(z);
        }
        if (z.over && z.raw && z.surf && band_ == kBandCamera) {          // the picture (last rendered), then the overlay over it
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

// --- bands

void ViewScreen::SetBand(int band) {
    if (band == band_) return;
    band_ = band;
    for (Zone& z : zones_) if (z.cam && !z.camera) { gcDeleteCustomCamera(z.cam); z.cam = nullptr; z.aimed = false; }   // re-made for the band
    for (Zone& z : zones_) if (z.surf && !z.glass && !z.camera) {
        Mark(z, "", 0xFF000000);
        if (z.halfOn && vis_) if (DEVMESHHANDLE m = v_->GetDevMesh(vis_, z.mesh)) oapiSetTexture(m, z.slot, z.surf);
        z.halfOn = false;
    }
    Cull();
    ShowGroups();
    oapiWriteLogV("ShipView: band %d", band_);
}

void ViewScreen::SetZoneGroup(int i, DWORD group) {
    if (i < 0 || i >= int(zones_.size())) return;
    zones_[i].group = group;
    Geometry(zones_[i]);
}

void ViewScreen::SetZonePage(int i, Page fn, void* ctx) { if (i >= 0 && i < int(zones_.size())) { zones_[i].page = fn; zones_[i].pageCtx = ctx; } }

// The screen's real size and place: the bounding box of its mesh group (the mesh template, mesh frame)
void ViewScreen::Geometry(Zone& z) {
    if (z.group == DWORD(-1)) return;
    MESHHANDLE mh = v_->GetMeshTemplate(z.mesh);
    MESHGROUP* g = mh ? oapiMeshGroup(mh, z.group) : nullptr;
    if (!g || !g->nVtx) return;
    VECTOR3 lo = _V(1e9, 1e9, 1e9), hi = _V(-1e9, -1e9, -1e9);
    for (DWORD k = 0; k < g->nVtx; ++k) {
        const NTVERTEX& v = g->Vtx[k];
        lo = _V((std::min)(lo.x, double(v.x)), (std::min)(lo.y, double(v.y)), (std::min)(lo.z, double(v.z)));
        hi = _V((std::max)(hi.x, double(v.x)), (std::max)(hi.y, double(v.y)), (std::max)(hi.z, double(v.z)));
    }
    z.gc = (lo + hi) * 0.5;
    z.gw = (std::max)(hi.x - lo.x, hi.z - lo.z);                           // across: the screen's width
    z.geom = z.gw > 0.1;
}

// Dynamic resolution: the width of the screen on the user's display (its real size, the eye's real distance, the view's
// aperture) against the camera's picture; under 55 % of it the half picture is rendered and shown (back over 65 %).
void ViewScreen::Resolution(Zone& z) {
    if (!z.geom || !z.half || !vis_ || band_ != kBandOptical) return;
    VECTOR3 eg; oapiCameraGlobalPos(&eg);
    VECTOR3 eye; v_->Global2Local(eg, eye);
    const double d = length(z.gc + shift_ - eye);
    DWORD vw = 0, vh = 0; oapiGetViewportSize(&vw, &vh);
    if (d < 0.3 || !vh) return;
    const double px = z.gw / d * (0.5 * vh) / std::tan(oapiCameraAperture());
    const bool half = z.halfOn ? px < 0.65 * z.w : px < 0.55 * z.w;
    if (half == z.halfOn) return;
    z.halfOn = half;
    if (DEVMESHHANDLE m = v_->GetDevMesh(vis_, z.mesh)) oapiSetTexture(m, z.slot, half ? z.half : z.surf);
    Aim(z);                                                                // the camera onto the other picture
}

SURFHANDLE ViewScreen::Target(const Zone& z) const {
    if (!z.camera && (band_ == kBandIR || band_ == kBandUV) && z.sens) return z.sens;
    if ((band_ == kBandCamera || z.camera) && z.raw) return z.raw;
    return z.halfOn && z.half && band_ == kBandOptical ? z.half : z.surf;
}
void ViewScreen::SetZoneCamera(int i, bool camera) { if (i >= 0 && i < int(zones_.size())) zones_[i].camera = camera; }
void ViewScreen::AddOccluder(UINT mesh, DWORD group) { occluders_.push_back({mesh, group}); }
void ViewScreen::SetWindowOpen(bool open) { windowWanted_ = open; }

int ViewScreen::AddGlass(UINT mesh, DWORD texSlot, int w, int h, int from) {
    Zone z; z.mesh = mesh; z.slot = texSlot; z.w = w; z.h = h; z.pos = _V(0, 0, 0); z.dir = _V(0, 0, 1); z.up = _V(0, 1, 0);
    z.vfov = 1.0; z.glass = true; z.from = from;
    zones_.push_back(z);
    return int(zones_.size()) - 1;
}

// The window: in the optical band, with the screen on and the viewer where it works, the zone groups (the screen) and
// the occluders are not drawn - the world itself is seen through. Otherwise everything is drawn (the screen dark or a
// sensor picture).
void ViewScreen::ShowGroups() {
    const bool open = on_ && band_ == kBandOptical && windowWanted_;
    if (!vis_ || open == windowShown_) return;
    windowShown_ = open;
    GROUPEDITSPEC e = {};
    e.flags = open ? GRPEDIT_ADDUSERFLAG : GRPEDIT_DELUSERFLAG;
    e.UsrFlag = 0x3;                                                       // not drawn, no shadow
    for (Zone& z : zones_)
        if (!z.glass && !z.camera && z.group != DWORD(-1))
            if (DEVMESHHANDLE m = v_->GetDevMesh(vis_, z.mesh)) oapiEditMeshGroup(m, z.group, &e);
    for (const Occluder& o : occluders_)
        if (DEVMESHHANDLE m = v_->GetDevMesh(vis_, o.mesh)) oapiEditMeshGroup(m, o.group, &e);
    oapiWriteLogV("ShipView: the window %s (%d occluders)", open ? "open" : "shut", int(occluders_.size()));
}

// A sensor picture on the screen: stretched from its low resolution, a false colour by the band (a linear map of the
// channels - IR: white-hot, the red end weighted; UV: violet, the blue end weighted), the contrast of a detector (gamma)
// and its noise.
void ViewScreen::Process(Zone& z) {
    if (!z.sens || !z.surf) return;
    oapi::Sketchpad* skp = oapiGetSketchpad(z.surf);
    if (!skp) return;
#if __has_include("DrawAPI.h")
    if (GetModuleHandleA("D3D9Client.dll")) {
        const bool ir = band_ == kBandIR;
        const float w[3] = {ir ? 0.62f : 0.08f, ir ? 0.30f : 0.27f, ir ? 0.08f : 0.65f};   // what the detector sees of r, g, b
        const float c[3] = {ir ? 1.0f : 0.62f, ir ? 1.0f : 0.40f, 1.0f};                   // the colour it is shown in
        oapi::FMATRIX4 M(w[0] * c[0], w[0] * c[1], w[0] * c[2], 0.0f,
                         w[1] * c[0], w[1] * c[1], w[1] * c[2], 0.0f,
                         w[2] * c[0], w[2] * c[1], w[2] * c[2], 0.0f,
                         0.0f, 0.0f, 0.0f, 1.0f);
        skp->SetColorMatrix(&M);
        const oapi::FVECTOR4 gamma(ir ? 0.65f : 0.8f, ir ? 0.65f : 0.8f, ir ? 0.65f : 0.8f, 1.0f);
        skp->SetRenderParam(oapi::Sketchpad::PRM_GAMMA, &gamma);
        const oapi::FVECTOR4 noise(0.5f, 0.5f, 0.5f, 0.06f);
        skp->SetRenderParam(oapi::Sketchpad::PRM_NOISE, &noise);
        RECT src = {0, 0, z.sw, z.sh}, tgt = {0, 0, z.w, z.h};
        skp->StretchRect(z.sens, &src, &tgt);
        skp->SetColorMatrix(nullptr);
        skp->SetRenderParam(oapi::Sketchpad::PRM_GAMMA, nullptr);
        skp->SetRenderParam(oapi::Sketchpad::PRM_NOISE, nullptr);
    }
#endif
    oapiReleaseSketchpad(skp);
}

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
    for (Zone& z : zones_) if (z.sens) { oapiDestroySurface(z.sens); z.sens = nullptr; }
    for (Zone& z : zones_) if (z.half) { oapiDestroySurface(z.half); z.half = nullptr; }
}

}  // namespace shipview
