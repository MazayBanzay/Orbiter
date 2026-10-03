// TantraInterior: the Tantra's interior as given to OrbiterCrew (OcInterior, OrbiterCrewApi.h).
// A person walks inside with his or her own body (OrbiterCrew moves it); the ship only answers where the floors and the walls
// are (the collision boxes of tools/gen_mesh.py in InteriorLayout.h) and what can be used (the bridge seats).
// Sitting down hands the person to the ship's virtual cockpit (the body leaves, the focus goes to the ship);
// standing up gives the body back behind the seat.
#pragma once
#define STRICT
#include "orbitersdk.h"
#include "../../../OrbiterCrew/include/OrbiterCrewApi.h"

class TantraCrew;

class TantraInterior {
public:
    // meshDZ: the mesh frame -> ship frame offset (z) of the interior, read on every call (the CG moves)
    // crew: the items of TantraCrew (the lift and the way in from outside, ids from 100) are served together with ours
    void Init(VESSEL* ship, UINT vcMeshIdx, double (*meshDZ)(void*), void* dzCtx, TantraCrew* crew);
    void Register();                          // clbkPostCreation
    void Unregister();                        // destructor
    bool Available() const { return reg_; }
    // the ship's rule whether one may walk now (takeoff, landing, anamezon drive...); reason in cp1251
    void SetCanWalk(int (*fn)(void*, char*, int), void* ctx) { canWalk_ = fn; canCtx_ = ctx; }

    // the seat taken by a person in Use (the ship then shows the VC from it); -1 = none
    int TakenSeat() const { return seat_; }
    int SeatedPerson() const { return person_; }
    // F in seat N: the person sitting there (or, if none came in, the crew member of that seat) stands up behind the seat
    // with his or her body; false = nobody to stand up (no OrbiterCrew interior, no member aboard for that seat)
    bool StandUp(int seat);
    OBJHANDLE ViewerBody() const;
    void Step();                              // clbkPreStep: the deferred stand-up / sit-down (not from a key or a callback)             // the body of a person walking inside this ship, if it has the focus; else nullptr                           // E in the seat: the body stands behind the seat; false = no person in the seat

private:
    static ATTACHMENTHANDLE cAttach(void* c);
    static int cGround(void* c, const VECTOR3* p, double stepUp, double* floorY);
    static void cWalls(void* c, const VECTOR3* from, VECTOR3* to, double radius, double height);
    static int cCount(void* c);
    static int cItem(void* c, int i, OcItem* out);
    static void cUse(void* c, int id, int personId);
    static void cSeat(void* c, int id, VECTOR3* pos, VECTOR3* dir);
    static void cViewing(void* c, int on);
    static void cOrigin(void* c, VECTOR3* o);
    static int cCanWalk(void* c, char* reason, int n);

    double DZ() const { return dz_(dzCtx_); }
    bool InCapsule(double x, double y, double z) const;      // mesh frame
    void Resolve(double& x, double& z, double feet, double radius, double height, bool capsule) const;

    VESSEL* v_ = nullptr;
    TantraCrew* crew_ = nullptr;
    UINT vcMesh_ = 0;
    double (*dz_)(void*) = nullptr;
    void* dzCtx_ = nullptr;
    OcApi api_;
    OcInterior fns_ = {};
    OcInteriorExt ext_ = {};
    int (*canWalk_)(void*, char*, int) = nullptr;
    void* canCtx_ = nullptr;
    ATTACHMENTHANDLE att_ = nullptr;
    bool reg_ = false;
    int seat_ = -1, person_ = 0;
    int standSeat_ = -1;                     // queued stand-up
    bool attachCam_ = false;                 // queued: the cockpit view of the ship after sitting down
};
