// ShipView::ViewScreen - "a slot into the world": live outside views on screens inside our ships (common to all our vessels).
//
// Each zone is one mesh group with its own texture slot. A D3D9Client custom camera (gcAPI) renders the outside view into a
// render surface, and the surface replaces that texture of the device mesh. Several zones together make one wide screen.
//
// Use in a vessel:
//   clbkSetClassCaps: screen_.Init(this); screen_.AddZone(vcMesh, 1, 640, 256, pos, dir, up, vfovDeg); ...
//   clbkVisualCreated:  screen_.OnVisualCreated(vis);       clbkVisualDestroyed: screen_.OnVisualDestroyed();
//   any step:           screen_.SetShift(_V(0,0,dz));        (when the mesh moves in the vessel frame, e.g. CG shift)
//   on/off:             screen_.SetOn(true/false);          destructor: screen_.Shutdown();
// Needs D3D9Client with CustomCamMode = 1 (D3D9Client.cfg) and gcAPI.lib. Without it the zones show "NO CAMERA".
#pragma once
#define STRICT
#include "orbitersdk.h"
#include <vector>

namespace shipview {

class ViewScreen {
public:
    void Init(VESSEL* v) { v_ = v; }
    // meshIdx: vessel mesh index; texSlot: texture index of the group in the mesh file (1 = first texture of the TEXTURES list);
    // pos/dir/up: camera in the MESH frame (dir, up unit and perpendicular); vfovDeg: full vertical field of view of the zone.
    int AddZone(UINT meshIdx, DWORD texSlot, int w, int h, VECTOR3 pos, VECTOR3 dir, VECTOR3 up, double vfovDeg);
    void SetZoneView(int zone, VECTOR3 pos, VECTOR3 dir, VECTOR3 up);   // re-aim (e.g. the capsule turns)
    void OnVisualCreated(VISHANDLE vis);
    void OnVisualDestroyed();
    void SetShift(VECTOR3 shift);        // mesh frame -> vessel frame offset
    void SetOn(bool on);
    // gcAPI renders only the cameras of the vessel in focus. When the viewer is another vessel inside the ship (a person's
    // body walking in the interior), the cameras are hung on it: call every frame with that vessel (nullptr = the ship).
    void SetHost(OBJHANDLE host);
    // a telescope: a zone that follows a celestial body or a vessel (nullptr = fixed direction again); narrow field of view
    void AimAt(int zone, OBJHANDLE target);
    void SetZoneFov(int zone, double vfovDeg);
    // where the zone's picture is seen from the viewer (mesh frame direction, angular half-size): the client renders the custom
    // cameras one after another, so only the zones the viewer looks at keep their cameras on (default: the camera's own view)
    void SetZoneLook(int zone, VECTOR3 dir, double halfDeg);
    void SetZoneOnDemand(int zone, bool demand);   // its camera exists only while the viewer looks at its picture
    void SetZoneEnabled(int zone, bool on);        // off: no camera, the zone dark (e.g. the astronomer's screen)
    // something drawn right over a zone's picture (a HUD): the camera renders into an inner surface, every step the picture is
    // copied to the screen's texture and the overlay drawn on it. dir / up / vfovDeg: the zone's camera (mesh frame), to project with
    typedef void (*Overlay)(void* ctx, SURFHANDLE surf, int w, int h, const VECTOR3& dir, const VECTOR3& up, double vfovDeg);
    void SetZoneOverlay(int zone, Overlay fn, void* ctx);
    void Frame();                        // every time step: re-aim the moving cameras (a walking host, telescopes), the overlays
    bool On() const { return on_; }
    bool Available() const { return gc_; }
    void Shutdown();

private:
    struct Zone {
        UINT mesh; DWORD slot; int w, h; VECTOR3 pos, dir, up; double vfov;
        OBJHANDLE target = nullptr;
        VECTOR3 look = {0, 0, 1}; double lookHalf = 0.5;      // where its picture is seen from the viewer
        bool demand = false, enabled = true;                  // a camera only while looked at; the zone switched on
        VECTOR3 lastPos = {0, 0, 0}, lastDir = {0, 0, 0}, lastUp = {0, 0, 0}; bool aimed = false;   // the last set pose (host frame)
        SURFHANDLE surf = nullptr; void* cam = nullptr;      // CAMERAHANDLE of the client
        Overlay over = nullptr; void* overCtx = nullptr; SURFHANDLE raw = nullptr;   // an overlay: the camera renders into raw
    };
    bool Start();
    void Aim(Zone& z, bool force = true);   // force = false: only if the pose moved noticeably (>30 cm, >0.2 deg)
    void Bind();
    void Mark(Zone& z, const char* text, DWORD bg);
    void Cull();                            // the cameras of the zones out of the viewer's sight off

    VESSEL* v_ = nullptr;
    VISHANDLE vis_ = nullptr;
    std::vector<Zone> zones_;
    VECTOR3 shift_ = {0, 0, 0};
    OBJHANDLE host_ = nullptr;
    bool tried_ = false, gc_ = false, on_ = false;
};

}  // namespace shipview
