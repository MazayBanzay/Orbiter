// TantraSolids: the ship's supports standing on the ground, for other vessels to collide with (the МПУ rover of
// «Архитектор - Транспорт»). Exported from Tantra.dll without linking:
//   typedef int (*tantraOuterSolids_t)(OBJHANDLE ship, TantraSolid* out, int max);
//   auto fn = (tantraOuterSolids_t)GetProcAddress(GetModuleHandleA("Tantra.dll"), "tantraOuterSolids");
// Every frame: the solids in the SHIP (vessel) frame, only what stands at the ground (the legs on it, the feet, the lift
// cabin when it is down); returns how many were written (0 for a ship that is not a Tantra or flying).
#pragma once
#include "orbitersdk.h"

struct TantraSolid {
    int kind;          // 1 capsule (a..b, radius r), 2 oriented box (centre c, half sizes half along the columns of R)
    VECTOR3 a, b;      // capsule axis ends
    double r;          // capsule radius
    VECTOR3 c, half;   // box
    MATRIX3 R;         // box axes (columns: the box's x, y, z in the ship frame)
};
