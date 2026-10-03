// OrbiterCrew - the interface for ships (and any other module): people aboard, boarding and going out.
//
// The person is the unit (see src/Person.h). A ship does not own people and does not copy them: it asks OrbiterCrew.
//   - aboard, a person has no body in Orbiter's world; OrbiterCrew keeps him or her whole (organism, suit, pack)
//   - the ship saves its people in its own scenario block (ocSavePerson) and gives them back on loading (ocLoadPerson)
//   - going out: ocDisembark creates the body of that very person at the given place
//   - coming in: ocBoard takes a body out of the world, the person is aboard the ship
// Plain C functions exported by Modules\OrbiterCrew\CrewMember.dll; OcApi below loads them at run time, so a ship
// module has no link dependency on OrbiterCrew (and still loads when OrbiterCrew is not installed).
#pragma once
#include <windows.h>
#include <OrbiterAPI.h>

extern "C" {
struct OcInfo {
    char name[64], role[48], sex[16];
    double age, heightM, massKg;
    double pulse, coreT;        // per minute, K
    int state;                  // 0 well, 1 unconscious, 2 dead
    int where;                  // 0 nowhere, 1 in the world (a body), 2 aboard a ship without a body (a seat, stored),
                                // 3 aboard in the ship's interior (the body walks inside, attached to the ship)
    OBJHANDLE vessel;           // the body (1, 3), or the ship (2)
};   // (the layout is fixed: ships built against it read it as is; the ship of a person in an interior: ocShipOf)

// ---- the ship's interior, as the ship gives it to OrbiterCrew (a person walks inside with his or her own body) ----
// Everything in the ship's frame (the vessel's local coordinates; the ship accounts for its own mesh offsets).
// The person moves; the ship only answers where the floor and the walls are and what can be used.
enum OcItemKind { OC_SEAT = 1, OC_TERMINAL, OC_DOOR, OC_LIFT, OC_AIRLOCK, OC_EXIT };
struct OcItem {
    int id;                     // the ship's own id (given back in Use / Seat)
    int kind;                   // OcItemKind
    VECTOR3 pos, dir;           // where it is; which way one faces to use it
    double radius;              // m: within reach
    char label[64];             // shown in the hint ("E - сесть: кресло пилота")
};
struct OcInterior {
    ATTACHMENTHANDLE (*Attach)(void* ctx);      // the ship's parent point (toparent = false); OrbiterCrew moves it each
                                                // frame with SetAttachmentParams through oapiGetVesselInterface(ship)
    int (*Ground)(void* ctx, const VECTOR3* p, double stepUp, double* floorY);   // the highest floor not above p.y + stepUp; 0 = none
    void (*Walls)(void* ctx, const VECTOR3* from, VECTOR3* to, double radius, double height);  // walls along feet..feet+height
    int (*Zone)(void* ctx, const VECTOR3* p, MATRIX3* R, VECTOR3* t);           // a part with its own motion (the bridge capsule):
                                                                                 // zone -> ship; 0 = the fixed hull. May be NULL
    void (*Gravity)(void* ctx, const VECTOR3* p, VECTOR3* g);                    // felt gravity (m/s^2, ship frame); NULL = 9.81 along -y
    int (*Count)(void* ctx);                                                     // items: seats, terminals, doors, lift, airlocks
    int (*Item)(void* ctx, int i, OcItem* out);
    void (*Use)(void* ctx, int id, int personId);                                // the person pressed E at the item
    void (*Seat)(void* ctx, int id, VECTOR3* pos, VECTOR3* dir);                 // the seat's pose (hips, facing)
    void (*Viewing)(void* ctx, int on);                                          // the person's camera is inside this ship (show the interior as an
                                                                                 // outside mesh) / has left it
};
}

// function types (the exported names are the same without the trailing "_t")
typedef int (*ocCreatePerson_t)(const char* name, const char* role, double age, double massKg);      // -> person id
typedef void (*ocSetAboard_t)(int id, OBJHANDLE ship);
typedef int (*ocBoard_t)(OBJHANDLE body, OBJHANDLE ship);        // -> person id, 0 if 'body' is not a person's body
typedef OBJHANDLE (*ocDisembark_t)(int id, const char* vesselName, const VESSELSTATUS2* vs);   // -> the body
typedef int (*ocPersonOfBody_t)(OBJHANDLE body);                 // -> person id or 0
typedef int (*ocInfo_t)(int id, OcInfo* out);                    // -> 1 if the person exists
typedef void (*ocSavePerson_t)(int id, FILEHANDLE scn);          // writes OC_PERSON ... OC_END into the open block
typedef int (*ocLoadPerson_t)(const char* lines);                // the lines between OC_PERSON and OC_END, '\n'-separated -> id
// interior (newer CrewMember.dll; optional below: an older module simply lacks them)
typedef void (*ocRegisterInterior_t)(OBJHANDLE ship, const OcInterior* fns, void* ctx);   // in clbkPostCreation
typedef void (*ocUnregisterInterior_t)(OBJHANDLE ship);                                 // in the ship's destructor
typedef OBJHANDLE (*ocEnterInterior_t)(int id, const VECTOR3* posInShip, const VECTOR3* dirInShip);
    // aboard without a body -> the body stands in the interior at that place; the focus goes to the body. -> the body
typedef void (*ocLeaveInterior_t)(int id, int seatId);
    // the body leaves the world (into the seat 'seatId', or stored with -1); the focus goes to the ship
typedef OBJHANDLE (*ocShipOf_t)(int id);                          // the ship he or she is aboard (where 2 or 3), else NULL
typedef OBJHANDLE (*ocExitTo_t)(int id, const char* vesselName, const VESSELSTATUS2* vs);
    // out of the ship (an airlock): the body stands at vs, detached, in the world -> the body

// ---- the interior, more (after ocRegisterInterior; OcInterior itself stays as it is for ships already built) ----
// With Origin set, every OcInterior callback and ocEnterInterior work in the ship's INTERIOR frame (for Tantra: its
// mesh frame), which does not move when the ship's CG does; ship frame = interior + Origin(). OrbiterCrew keeps and
// saves the person's place in the interior frame and moves the attachment in the ship frame.
// Inside, Orbiter's physics does not act on the person (no ship inertia; the ship gives the felt gravity). Whether one
// may walk is the ship's: CanWalk = 0 (with a reason) holds her where she stands (takeoff, landing, anamezon drive...).
struct OcInteriorExt {
    int size;                                                    // sizeof(OcInteriorExt): fields may be added at the end
    void (*Origin)(void* ctx, VECTOR3* o);                       // NULL: (0,0,0) - the interior frame is the ship frame
    int (*CanWalk)(void* ctx, char* reason, int n);              // NULL: always; reason in the ship's code page
};
typedef void (*ocSetInteriorExt_t)(OBJHANDLE ship, const OcInteriorExt* ext);
// a person outside walks in (a lift, an airlock): the SAME body is now inside at pos/dir (interior frame); the focus
// and the camera stay with the person (the user's rule: never the ship's panel on going in or out). -> person id, 0
typedef int (*ocEnterShip_t)(OBJHANDLE body, OBJHANDLE ship, const VECTOR3* pos, const VECTOR3* dir);

struct OcApi {
    HMODULE dll = nullptr;
    ocCreatePerson_t CreatePerson = nullptr;
    ocSetAboard_t SetAboard = nullptr;
    ocBoard_t Board = nullptr;
    ocDisembark_t Disembark = nullptr;
    ocPersonOfBody_t PersonOfBody = nullptr;
    ocInfo_t Info = nullptr;
    ocSavePerson_t SavePerson = nullptr;
    ocLoadPerson_t LoadPerson = nullptr;
    ocRegisterInterior_t RegisterInterior = nullptr;   // optional (NULL with an older CrewMember.dll)
    ocUnregisterInterior_t UnregisterInterior = nullptr;
    ocEnterInterior_t EnterInterior = nullptr;
    ocLeaveInterior_t LeaveInterior = nullptr;
    ocExitTo_t ExitTo = nullptr;
    ocShipOf_t ShipOf = nullptr;
    ocSetInteriorExt_t SetInteriorExt = nullptr;
    ocEnterShip_t EnterShip = nullptr;

    bool Load() {
        if (dll) return true;
        dll = LoadLibraryA("Modules\\OrbiterCrew\\CrewMember.dll");
        if (!dll) return false;
        CreatePerson = (ocCreatePerson_t)GetProcAddress(dll, "ocCreatePerson");
        SetAboard = (ocSetAboard_t)GetProcAddress(dll, "ocSetAboard");
        Board = (ocBoard_t)GetProcAddress(dll, "ocBoard");
        Disembark = (ocDisembark_t)GetProcAddress(dll, "ocDisembark");
        PersonOfBody = (ocPersonOfBody_t)GetProcAddress(dll, "ocPersonOfBody");
        Info = (ocInfo_t)GetProcAddress(dll, "ocInfo");
        SavePerson = (ocSavePerson_t)GetProcAddress(dll, "ocSavePerson");
        LoadPerson = (ocLoadPerson_t)GetProcAddress(dll, "ocLoadPerson");
        RegisterInterior = (ocRegisterInterior_t)GetProcAddress(dll, "ocRegisterInterior");
        UnregisterInterior = (ocUnregisterInterior_t)GetProcAddress(dll, "ocUnregisterInterior");
        EnterInterior = (ocEnterInterior_t)GetProcAddress(dll, "ocEnterInterior");
        LeaveInterior = (ocLeaveInterior_t)GetProcAddress(dll, "ocLeaveInterior");
        ExitTo = (ocExitTo_t)GetProcAddress(dll, "ocExitTo");
        ShipOf = (ocShipOf_t)GetProcAddress(dll, "ocShipOf");
        SetInteriorExt = (ocSetInteriorExt_t)GetProcAddress(dll, "ocSetInteriorExt");
        EnterShip = (ocEnterShip_t)GetProcAddress(dll, "ocEnterShip");
        if (!(CreatePerson && SetAboard && Board && Disembark && PersonOfBody && Info && SavePerson && LoadPerson)) { Unload(); return false; }
        return true;
    }
    void Unload() { if (dll) FreeLibrary(dll); *this = OcApi(); }
    bool Ok() const { return dll != nullptr; }
    bool HasInterior() const { return RegisterInterior && EnterInterior && LeaveInterior && ExitTo; }
};
