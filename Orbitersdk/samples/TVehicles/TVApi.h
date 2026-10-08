// TVehicles - the transport API (Modules\TVehicles.dll): what vehicles, stations and people share across modules.
//   - the power sockets of vehicles and the sources (charging stations; later the Tantra's outlet, the supply module)
//   - the portable cable: carried by a person from a station's reel to a vehicle's socket
//   - what a person carries in the hands (the cable's plug, an energy cell)
// Plain C functions; TvApi below loads them at run time (a vehicle still loads without TVehicles.dll - no charging then).
#pragma once
#include <windows.h>
#include <OrbiterAPI.h>
#include "OrbiterCrewApi.h"

extern "C" {
// what a person carries in the hands
enum TvCarryKind { TV_NOTHING = 0, TV_CABLE = 1, TV_CELL = 2 };
struct TvCarry { int kind; OBJHANDLE station; double energyJ; };   // TV_CABLE: whose cable; TV_CELL: the cell's charge
}
typedef int (*tvCarryGet_t)(int personId, TvCarry* out);            // -> 1 if the person carries something
typedef void (*tvCarrySet_t)(int personId, const TvCarry* c);       // TV_NOTHING clears
// a vehicle's power socket (vessel frame); the station whose cable is plugged into it
typedef void (*tvSocketRegister_t)(OBJHANDLE vehicle, const VECTOR3* socketLocal);
typedef void (*tvSocketUnregister_t)(OBJHANDLE vehicle);
typedef int (*tvPlug_t)(OBJHANDLE vehicle, int personId);          // the person's cable into the vehicle's socket -> 1 done
typedef int (*tvUnplug_t)(OBJHANDLE vehicle, int personId);        // the plug out, into the person's hands -> 1 done
typedef OBJHANDLE (*tvPluggedTo_t)(OBJHANDLE vehicle);             // the station plugged in, or NULL
typedef double (*tvDraw_t)(OBJHANDLE vehicle, double wantW, double dt);   // energy (J) the station gives this step, <= wantW*dt

// what OrbiterCrew is handed (OrbiterCrewApi.h OcHeld): an energy cell, the cable's plug. Our mark in owner / data comes back
// unchanged when she puts the thing down (X), picks it up again (F) or after a save: the cell's charge travels with it
#include <cstdio>
#include <cstring>
inline void tvHeldCell(OcHeld* h, double charge01) {
    std::memset(h, 0, sizeof *h); h->size = sizeof(OcHeld); h->kind = OC_HELD_CELL; h->massKg = 80.0;
    h->dims[0] = 0.30; h->dims[1] = 0.30; h->dims[2] = 0.32;
    std::snprintf(h->label, sizeof h->label, "энергоячейка %.0f %%", charge01 * 100.0);
    std::snprintf(h->owner, sizeof h->owner, "%s", "TVehicles"); std::snprintf(h->data, sizeof h->data, "cell %.6f", charge01);
}
inline void tvHeldPlug(OcHeld* h) {
    std::memset(h, 0, sizeof *h); h->size = sizeof(OcHeld); h->kind = OC_HELD_PLUG; h->massKg = 2.0;
    h->dims[0] = 0.10; h->dims[1] = 0.10; h->dims[2] = 0.16;
    std::snprintf(h->label, sizeof h->label, "%s", "вилка кабеля");
    std::snprintf(h->owner, sizeof h->owner, "%s", "TVehicles"); std::snprintf(h->data, sizeof h->data, "%s", "cable");
}
// hand it over: 1 if she has it (or OrbiterCrew is too old to show hands - then it is only ours), 0 refused (too heavy here,
// hands busy, seated...: OrbiterCrew says why)
inline int tvHandOver(OcApi& api, int person, const OcHeld* h) { return api.Give ? api.Give(person, h) : 1; }
inline void tvTakeBack(OcApi& api, int person) { if (api.Take && person) api.Take(person, nullptr); }
inline bool tvHolding(OcApi& api, int person) { OcHeld h{}; h.size = sizeof h; return api.HeldOf ? api.HeldOf(person, &h) != 0 : false; }
// one row of a context menu (OrbiterCrew F-1 / M-1); n grows; reason: why it is grey (empty when available)
inline void tvAct(OcAction* out, int& n, int max, int id, const char* label, bool ok, const char* reason, double durS) {
    if (n >= max) return;
    OcAction& a = out[n++];
    std::memset(&a, 0, sizeof a); a.size = sizeof(OcAction); a.id = id; a.available = ok ? 1 : 0; a.durationS = durS; a.interruptible = 1;
    std::snprintf(a.label, sizeof a.label, "%s", label);
    if (!ok && reason) std::snprintf(a.reason, sizeof a.reason, "%s", reason);
}

struct TvApi {
    HMODULE dll = nullptr;
    tvCarryGet_t CarryGet = nullptr; tvCarrySet_t CarrySet = nullptr;
    tvSocketRegister_t SocketRegister = nullptr; tvSocketUnregister_t SocketUnregister = nullptr;
    tvPlug_t Plug = nullptr; tvUnplug_t Unplug = nullptr; tvPluggedTo_t PluggedTo = nullptr; tvDraw_t Draw = nullptr;
    bool Load() {
        if (dll) return true;
        dll = GetModuleHandleA("TVehicles.dll");
        if (!dll) dll = LoadLibraryA("Modules\\TVehicles.dll");
        if (!dll) return false;
        CarryGet = (tvCarryGet_t)GetProcAddress(dll, "tvCarryGet"); CarrySet = (tvCarrySet_t)GetProcAddress(dll, "tvCarrySet");
        SocketRegister = (tvSocketRegister_t)GetProcAddress(dll, "tvSocketRegister");
        SocketUnregister = (tvSocketUnregister_t)GetProcAddress(dll, "tvSocketUnregister");
        Plug = (tvPlug_t)GetProcAddress(dll, "tvPlug"); Unplug = (tvUnplug_t)GetProcAddress(dll, "tvUnplug");
        PluggedTo = (tvPluggedTo_t)GetProcAddress(dll, "tvPluggedTo"); Draw = (tvDraw_t)GetProcAddress(dll, "tvDraw");
        return CarryGet && CarrySet && SocketRegister && Plug && Unplug && PluggedTo && Draw;
    }
    bool Ok() const { return dll && Draw; }
};
