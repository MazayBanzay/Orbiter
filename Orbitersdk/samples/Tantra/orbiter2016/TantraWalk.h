// TantraWalk: free walking inside the ship in the virtual cockpit view (test stage).
// The walker lives in the mesh frame of TantraVC.msh; walls and furniture are the collision boxes written by
// tools/gen_mesh.py to InteriorLayout.h. Orbiter has no interior collision of its own, so this is ours.
#pragma once
#define STRICT
#include "orbitersdk.h"

class TantraWalk {
public:
    void Init(VESSEL* v);
    void Shutdown();
    void OnLoadVC();                          // the virtual cockpit view was selected: enter the walk mode
    int Keys(char* kstate, double meshDZ);    // direct keys: moves, collides, sets the camera
    bool Active() const;
    void SitAt(int seat);                     // a person sat down in that bridge seat (OrbiterCrew): the VC view from it
    int Seat() const { return seated_ ? seat_ : -1; }
    void SetStandHook(bool (*hook)(void*, int), void* ctx) { standHook_ = hook; standCtx_ = ctx; }   // F in seat N: the person's body stands up

private:
    struct P3 { double x, y, z; };
    bool ViewIsMine() const;
    void Teleport(int place);
    void Interact();                          // E: sit down / stand up / climb the stairwell and the ladder
    void Move(double dx, double dz);
    double GroundAt(double x, double z, double feet, bool* found) const;
    bool Blocked(double x, double z, double feet) const;
    void ResolveWalls(double& x, double& z, double feet) const;
    const char* RoomAt(double x, double z, double feet) const;
    void ApplyCamera(double meshDZ);
    void Report(const char* extra);
    void EnsureNote();

    VESSEL* v_ = nullptr;
    NOTEHANDLE note_ = nullptr;
    bool active_ = false, inBridge_ = false, seated_ = false;
    int seat_ = -1;
    double x_ = 0.0, z_ = 0.0, feet_ = 1.0, vy_ = 0.0, yaw_ = 0.0;
    double lastSys_ = 0.0, lastLog_ = 0.0, lastNote_ = 0.0;
    char prev_[256] = {0};
    char noteText_[256] = {0};
    bool (*standHook_)(void*, int) = nullptr;
    void* standCtx_ = nullptr;
};
