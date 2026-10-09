// «Грань» 25,4 м - the interior as given to OrbiterCrew (OcInterior + OcInteriorExt, OrbiterCrewApi.h), as in «Тантра»
// (the user, 2026-10-09: «КАК В ТАНТРЕ!!! ЗАШЕЛ И СРАЗУ В КОКПИТЕ!»; the recipe from the session «Экипаж»): the person
// sits and stands with the body, the focus and the camera are the person's all the time. The interior frame is the mesh
// frame (Origin = the CG shift). Now: the drum - its floor and walls, the two couches (OC_HELM: the pilot flies from
// there, LanderInterior tells the vessel who holds the helm), the hands on the yoke and МАРШ, the touch screens, the felt
// gravity, the drum's and the modules' motion (Zone). The salon with its 12 couches and the way to the hatch come next.
// A ship without people in its scenario seats its pilots at the start (CREW n in the scenario, n ≤ 2): ocSitAt - the
// commander last, so the focus and the camera are his, in the cockpit at once.
#pragma once
#include "Orbitersdk.h"
#include "../../../OrbiterCrew/include/OrbiterCrewApi.h"

namespace tantra::lander {

class Cockpit;

class LanderInterior {
public:
    struct Host {                             // what the interior asks of the vessel
        void* ctx = nullptr;
        bool (*MayWalk)(void* ctx) = nullptr;   // landed or held in the hangar
        int (*Gauges)(void* ctx, OcGauge* out, int max) = nullptr;
    };
    void Init(VESSEL4* v, UINT cabinMesh, Cockpit* cockpit, const Host& host);
    void Register();                          // clbkPostCreation
    void Unregister();                        // the vessel's destructor
    void Step(double dt, double frameZ, int crewSeed);   // the default pilots (once), the frame
    bool Available() const { return reg_; }
    bool Seeded() const { return seeded_; }
    void SetSeeded(bool s) { seeded_ = s; }
    // the pilot at the helm has the focus (his body is Orbiter's focus, the Orbiter window in front): the vessel then
    // reads his keys itself; -1 none, else the seat 0 / 1
    int HelmFocused() const;

private:
    static ATTACHMENTHANDLE cAttach(void* c);
    static int cGround(void* c, const VECTOR3* p, double stepUp, double* floorY);
    static void cWalls(void* c, const VECTOR3* from, VECTOR3* to, double radius, double height);
    static int cZone(void* c, const VECTOR3* p, MATRIX3* R, VECTOR3* t);
    static void cGravity(void* c, const VECTOR3* p, VECTOR3* g);
    static int cCount(void* c);
    static int cItem(void* c, int i, OcItem* out);
    static void cUse(void* c, int id, int personId);
    static void cSeat(void* c, int id, VECTOR3* pos, VECTOR3* dir);
    static void cViewing(void* c, int on);
    static void cOrigin(void* c, VECTOR3* o);
    static int cCanWalk(void* c, char* reason, int n);
    static void cSeated(void* c, int seatId, int personId, int on);
    static int cClick(void* c, const VECTOR3* origin, const VECTOR3* dir, int personId);
    static void cSeatHands(void* c, int seatId, int personId, OcHand* left, OcHand* right);
    static int cSeatGauges(void* c, int seatId, int personId, OcGauge* out, int max);
    static double cCeiling(void* c, const VECTOR3* at);

    VESSEL4* v_ = nullptr;
    UINT cabin_ = 0;
    Cockpit* ck_ = nullptr;
    Host host_;
    OcApi api_;
    OcInterior fns_ = {};
    OcInteriorExt ext_ = {};
    ATTACHMENTHANDLE att_ = nullptr;
    bool reg_ = false, seeded_ = false;
    int seatPerson_[2] = {0, 0};
    double frameZ_ = 0, seedT_ = 0;
    int seedTries_ = 0;
};

}  // namespace tantra::lander
