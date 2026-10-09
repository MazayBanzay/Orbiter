// «Грань» 25,4 м: the avionics as the «Тантра» concept has it - photonics and analog, no silicon, three contours with a smooth
// degradation: 3 the photonic «intelligence» (all modes, 0,05 s) -> 2 the analog automat (hold modes, 0,3 s) -> 1 mechanics
// and hydraulics (the pilot's levers, a hydraulic interlock that sheds the opposite cup). A contour that fails hands over to
// the next one with its reason; a mode the active contour cannot fly stays manual with its reason. No Orbiter dependencies.
// Т1Б-А: the hover with the crew aboard on a body with an atmosphere (the runway) only by the emergency command (hover_ru);
// the entry glides with the flap 15° (balance_A); the ballistic descent trims with the nose tank full (α 63,8° at −25°).
#pragma once
#include "LanderPropulsion.h"

namespace tantra::lander {

enum Mode { kHover = 0, kTransition, kFlight, kEntry, kBallistic, kRunway, kVertLand, kDock, kModeCount };
const char* ModeRu(int m);
enum Contour { kMech = 0, kAnalog = 1, kPhoton = 2 };

// the vehicle as the sensors see it (the ship gives it; in the tests - the headless model)
struct Flight {
    double h = 0, vz = 0;                     // m above the ground, m/s up
    double roll = 0, pitch = 0, yaw = 0;      // rad off the hold
    double rollRate = 0, pitchRate = 0, yawRate = 0;
    double alpha = 0, alphaRate = 0;          // rad (entry, ballistic)
    double mass = 0, g = 9.80665;             // kg, m/s² local
    double wingLift = 0;                      // N the wings carry (transition)
    double Ixx = 0, Izz = 0, Iyy = 0;         // kg·m² roll, pitch, yaw
    bool atmosphere = false;                  // the body has an atmosphere (and the runway: Земля) - hover_ru
    double mach = 0;                          // the flight Mach number (the wing tips' schedule)
    double shipDist = 1e9;                    // m to «Тантра» (the afterburner is refused closer than kAfterSafeDist)
};

struct Pilot {
    double collective = 0;                    // 0..1 (mechanical contour)
    double roll = 0, pitch = 0, yaw = 0;      // -1..1 the stick
    double march = 0;                         // 0..1 the march lever
    bool emergency = false;                   // the emergency mode allowed (lift cups 11,5 km/s)
    bool nose = false;                        // the porous nose blow by hand
    double hHold = 30.0;                      // m the hover holds
    bool hoverEmergency = false;              // the emergency hover allowed (crew aboard, a body with an atmosphere)
    bool afterburner = false;                 // the marches' afterburner asked (форсаж)
};

struct AvIn {
    double dt = 0;
    int mode = kHover;
    Flight f;
    Pilot p;
    const Propulsion* prop = nullptr;
    bool photonPower = true, analogPower = true;
    double shockCabin = 0;                    // g on the instrument bay
    int crew = 0;                             // people aboard (spec::kCrewMax)
};

struct AvOut {
    double thr[8] = {};
    bool em[8] = {};
    double tvcP[2] = {}, tvcY[2] = {};
    double rcs = 0;                           // N·m yaw asked of the attitude thrusters
    double elevon = 0;                        // deg
    double flap = 0;                          // deg the body flap
    double frontSwing = 0;                    // deg
    bool noseValve = false;
    bool deploy[2] = {};
    double tip = 90;                          // deg the wing tips (0 flat, 60 keel, 90 folded) - the mesh modes
    bool gearDown = false;                    // the gear out (the mesh modes)
};

// the wing and the gear by mode, as the mesh has them (gen_lander.py MODES): the tips 0° in the hover, the transition, the
// subsonic flight and the runway (glide); 60° the keel above M 1 and in the entry; 90° folded - the ballistic descent and the
// hangar (dock). The tips' switch at M 1 is the mode boundary (сверх- и гиперзвук - одна конфигурация, отдельно не считано).
struct WingCfg { double tip; bool gearDown; };
WingCfg WingConfig(int mode, double mach);

class Avionics {
public:
    void Step(const AvIn& in, AvOut& out);
    Node photon, analog, mech, mode;
    int active = kPhoton;
    bool shed[8] = {};                        // the cups the automat put out (the opposite of a failed one)
    const char* decision = "";                // the automat's decision now (a literal)
    double Latency() const { return active == kPhoton ? 0.05 : active == kAnalog ? 0.3 : 0.5; }
    static bool Allowed(int mode, int contour);
    static bool HoverAllowed(bool atmosphere, int crew, bool emergency) { return emergency || crew <= 0 || !atmosphere; }
    double liftCmd = 0;                       // N the automat asks of the lift cups
private:
    void Contours(const AvIn& in);
    void Hover(const AvIn& in, AvOut& out, bool land);
    double failT_[8] = {}, okT_[8] = {};
    bool seenFail_[8] = {};
    bool photonBroken_ = false, analogBroken_ = false;
    const char* photonWhy_ = "", *analogWhy_ = "";
    double marchOutT_ = 0;
};

}  // namespace tantra::lander
