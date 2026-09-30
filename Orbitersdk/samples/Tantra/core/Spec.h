// Tantra core: geometry and fixed design numbers. No Orbiter dependencies.
// Layout "Spear C-146", final (docs/DESIGN.md): swollen aft body with the armoured engine
// well, traps and ion charges; armoured shoulder with the deflector coil; one continuous
// fore body of 17 m holding the hangar and the crew; iridium nose shield; carriage
// («лафет») gear. Tunable numbers live in Params.h / Tantra.cfg.
#pragma once
#include "Physics.h"

namespace tantra::spec {

// Vessel frame: +z forward, +y up, +x starboard. s = metres from the stern plane.
constexpr double kLength = 146.0;
constexpr double kOriginS = 38.0;               // vessel origin = loaded CG (s = 37.6) = pod line
constexpr double kSternZ = -kOriginS;
constexpr double kNoseZ = kSternZ + kLength;
constexpr double Z(double s) { return s - kOriginS; }

constexpr double kAftHalfWidth = 14.0;          // aft body, s 0..52
constexpr double kAftTop = 14.0, kAftBottom = -9.5;
constexpr double kForeRadius = 8.6;             // fore body, s 62..122 (hangar, crew)
constexpr double kAxisHeight = 14.0;            // hull axis above ground, resting level
constexpr double kWellRadius = 10.5;            // armoured engine well at the stern
constexpr double kWellCentreY = 0.0;             // thrust lines through the CG axis

// Undercarriage: carriage columns and stern legs (core/Carriage, orbiter2010/MeshLayout.h).
constexpr double kColumnX = 19.0;                // hips of the carriage legs (deployed)
constexpr double kColumnFootHalf = 8.0;          // carriage-leg pads 16 x 6 m (fore-aft)
constexpr double kStandR = 26.0;                 // stern pads circle
constexpr double kTurnClear = 16.0, kStandClear = 12.0;

// Stations.
constexpr double kHangarS0 = 79.0, kHangarS1 = 101.0;
constexpr double kDoorS = 104.0;                          // port airlock
constexpr double kControlPostS = 119.0;
constexpr double kShoulderS0 = 48.0, kShoulderS1 = 63.0;  // armoured belt, deflector coil inside

// Engines.
constexpr int kTrapCount = 4;                   // traps 10 x 25 m around the spine, s 24..49
constexpr int kAnaCount = 4;                    // chambers in the well
constexpr double kAnaCupY[kAnaCount] = {4.4, 4.4, -4.4, -4.4};
constexpr double kAnaCupX[kAnaCount] = {-4.4, 4.4, -4.4, 4.4};
// Cup (gen_mesh: rim s -1.5, depth 1.8, radius 3.2): paraboloid focus f = R^2/(4 depth)
// = 1.42 m in front of the vertex (s +0.3) - the reaction sits there, just inside the rim.
constexpr double kAnaFocusS = -1.12;

constexpr int kPlanCount = 12;                  // central planetary cups, ring R 11 m behind the baffle
constexpr double kPlanR = 11.0, kPlanS = -0.6;
constexpr int kPodCount = 2, kCupsPerPod = 3, kPodCups = kPodCount * kCupsPerPod;
constexpr double kPodS = 60.0, kPodX = 15.0, kPodXIn = 10.6;  // shoulder; sunk into recesses when tucked
constexpr double kPodCantDeg = 10.0;            // cups canted outboard
constexpr double kPodSwivelMaxDeg = 100.0;      // 0 = thrust forward (aft-pointing cups), 90 = up
constexpr double kPodSwivelRate = 15.0;         // deg/s
// Until the CG follows the propellant (ShiftCG, needs the exhaust module to follow), the
// pods apply their thrust on the CG line so that hovering does not pitch the ship.

// Attitude micro-motor blocks.
constexpr double kAttNoseS = 132.0, kAttNoseR = 7.0;
constexpr double kAttTailS = 8.0, kAttTailR = 13.0;
constexpr double kPmiPitch = 1500.0, kPmiRoll = 120.0;  // m^2

// Operations.
constexpr int kCrewSeats = 20;                  // 14 own + 6 from "Amat"

}  // namespace tantra::spec
