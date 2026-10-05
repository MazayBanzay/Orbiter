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

    // --- bands (opt-in; without SetBand the zones are plain cameras as above). Optical: the camera's colour picture on the
    // screen (a monitor), rendered straight into its texture, the HUD on a glass in front. IR / UV: a sensor - a camera at a
    // third of the resolution, its picture processed on the graphics card (colour matrix, gamma, sensor noise) at the
    // sensor's own rate. Off: a dark screen. Zones marked as cameras (a telescope) keep their cameras whatever the band.
    // (A window - the zone groups and occluders not drawn - is there for a ship that wants one: SetWindowOpen.)
    enum Band { kBandCamera = -1, kBandOff = 0, kBandOptical = 1, kBandIR = 2, kBandUV = 3, kBandData = 4 };
    // the data band: no cameras at all - every zone is a canvas for its page (2D, a few times a second)
    typedef void (*Page)(void* ctx, int zone, SURFHANDLE surf, int w, int h);
    void SetZonePage(int zone, Page fn, void* ctx);
    // the viewer is where the screen can be seen (e.g. in the bridge capsule): without it no camera at all
    void SetViewerPresent(bool present) { present_ = present; }
    // the monitor's own refresh rate: a camera renders once per 1/hz, between that it costs nothing
    void SetRefreshHz(double hz) { hz_ = hz > 1.0 ? hz : 1.0; }
    void SetBand(int band);
    int GetBand() const { return band_; }
    void SetZoneGroup(int zone, DWORD group);        // the zone's own screen group (not drawn when the window is open)
    void SetZoneCamera(int zone, bool camera);       // a camera whatever the band (a telescope)
    void AddOccluder(UINT mesh, DWORD group);        // a group in the way of the window: not drawn while it is open
    void SetWindowOpen(bool open);                   // every step from the host
    bool WindowOpen() const { return windowShown_; }
    void RefreshWindow() { windowShown_ = false; }   // someone redrew the groups (all shown): hide them again next frame
    // a sheet of glass in front of a zone for its overlay (a HUD) in the optical band and over the sensor pictures:
    // its texture is transparent, the overlay of zone `from` is drawn on it with that zone's projection
    int AddGlass(UINT mesh, DWORD texSlot, int w, int h, int from);
    static constexpr double kSensorHz = 12.0, kSensorScale = 1.0 / 3.0;
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
        DWORD group = DWORD(-1); bool camera = false;         // its screen group (the window); a camera in every band
        SURFHANDLE sens = nullptr; int sw = 0, sh = 0;        // the sensor's own low-resolution picture (IR / UV)
        bool glass = false; int from = -1;                    // a HUD glass over zone `from` (no camera)
        double seenT = -1e9, shotT = -1e9; int onFrames = 0; bool camOn = false;   // in sight last; its last frame; on now
        Page page = nullptr; void* pageCtx = nullptr;         // its data page
        SURFHANDLE half = nullptr; bool halfOn = false;       // the half-resolution picture (the screen small on the display)
        bool geom = false; VECTOR3 gc = {0, 0, 0}; double gw = 0.0;   // its group's centre and width (mesh frame)
    };
    void Geometry(Zone& z);                // the zone's real size from its mesh group
    void Resolution(Zone& z);              // full or half by how big the screen is on the user's display
    SURFHANDLE Target(const Zone& z) const;
    bool present_ = true;
    double hz_ = 30.0, pageT_ = -1e9;
    bool Sensing(const Zone& z) const { return z.camera || (band_ != kBandOff && band_ != kBandData); }
    void ShowGroups();                     // the window: its zone groups and the occluders drawn or not
    void Process(Zone& z);                 // the sensor picture -> the screen texture (IR / UV)
    struct Occluder { UINT mesh; DWORD group; };
    std::vector<Occluder> occluders_;
    int band_ = kBandCamera;
    bool windowWanted_ = false, windowShown_ = false;
    double sensorT_ = -1e9;
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
