// TantraTrap: a stand-alone anamezon trap cassette of the starship «Тантра»: an octagonal
// armoured cassette (9.8 m across flats, 24.8 m, axial trunnions) around a cylindrical 30 T
// trap with its own field store. Taken into the ship through the anamezon port by the column
// lifts (forks on the trunnions), handed out when spent. Its 9.37 kt of anamezon is a
// propellant resource that the ship takes over on hand-off.
#define STRICT
#include "orbitersdk.h"

namespace {
const double kStructMass = 164.0e3;     // trap container (ShipParams::trapStructMass)
const double kAnamezonMax = 9370.0e3;   // ShipParams::trapFuelMass
const double kR = 4.9, kHalfLen = 12.4;  // half the flat width, half the body length
}  // namespace

class TantraTrap : public VESSEL3 {
public:
    TantraTrap(OBJHANDLE h, int fm) : VESSEL3(h, fm) {}

    void clbkSetClassCaps(FILEHANDLE) override {
        SetSize(kHalfLen + 1.0);
        SetEmptyMass(kStructMass);
        CreatePropellantResource(kAnamezonMax, kAnamezonMax);
        SetCrossSections(_V(250.0, 250.0, 80.0));
        SetPMI(_V(60.0, 60.0, 12.0));
        SetRotDrag(_V(0.5, 0.5, 0.5));
        // Standing on its flat bottom face.
        SetTouchdownPoints(_V(0, -kR, kHalfLen - 2.0), _V(-2.5, -kR, -kHalfLen + 2.0), _V(2.5, -kR, -kHalfLen + 2.0));
        SetMeshVisibilityMode(AddMesh(oapiLoadMeshGlobal("Tantra\\TantraTrap")), MESHVIS_ALWAYS);
        // Hand-off point on the axis at the middle (the port forks lock on the trunnions at both ends).
        CreateAttachment(true, _V(0, 0, 0), _V(0, 1, 0), _V(0, 0, 1), "TTRAP");
    }
};

DLLCLBK VESSEL* ovcInit(OBJHANDLE h, int fm) { return new TantraTrap(h, fm); }
DLLCLBK void ovcExit(VESSEL* v) { delete static_cast<TantraTrap*>(v); }
