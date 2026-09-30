// Tantra core: geometry and fixed design numbers. No Orbiter dependencies.
// Layout "C-148" (docs/DESIGN.md, Tantra_Design/tantra_c148.html): hull «Б» - flat-sided aft body
// with the stern well, traps and ion charges; armoured shoulder with the deflector coil and the
// spine; fore body with the hangar and the crew; iridium nose on a blunted ogive; lateral crests
// with elevons, dorsal fin, body flap; four planetary pods; carriage («лафет») gear.
// Mesh-side geometry (pods, legs, doors) comes from orbiter2016/MeshLayout.h.
// Tunable numbers live in Params.h / Tantra.cfg.
#pragma once
#include "Physics.h"

namespace tantra::spec {

// Vessel frame: +z forward, +y up, +x starboard. s = metres from the stern plane.
constexpr double kLength = 146.0;
constexpr double kOriginS = 38.0;               // mesh origin; the vessel frame follows the CG (Tantra::frameS_)
constexpr double kSternZ = -kOriginS;
constexpr double kNoseZ = kSternZ + kLength;
constexpr double Z(double s) { return s - kOriginS; }

constexpr double kAftHalfWidth = 14.6;          // aft body, s 0..47 (chines)
constexpr double kAftTop = 14.4, kAftBottom = -9.6;
constexpr double kForeHalfWidth = 9.0, kForeTop = 8.9, kForeBottom = -8.7;  // fore body, s 75..122
constexpr double kAxisHeight = 14.0;            // hull axis above ground, resting level
constexpr double kWellRadius = 11.5;            // armoured engine well at the stern
constexpr double kWellCentreY = 2.2;            // over the flat bottom; thrust vectored through the CG
constexpr double kTvcMaxDeg = 7.0;              // magnetic nozzles: jet deflection by the field, no hinges

// Undercarriage: carriage columns and stern legs (core/Carriage, orbiter2010/MeshLayout.h).
constexpr double kColumnX = 19.0;                // hips of the carriage legs (deployed)
constexpr double kColumnFootHalf = 8.0;          // carriage-leg pads 16 x 6 m (fore-aft)
constexpr double kStandR = 19.0;                 // stern pads (MeshLayout kStandR, feet per leg)
constexpr double kTurnClear = 16.0, kStandClear = 12.0;

// Stations.
constexpr double kHangarS0 = 80.0, kHangarS1 = 100.0;
constexpr double kDoorS = 104.0;                          // port airlock
constexpr double kControlPostS = 119.0;
constexpr double kShoulderS0 = 48.0, kShoulderS1 = 63.0;  // armoured belt, deflector coil inside

// Engines.
constexpr int kTrapCount = 4;                   // traps 10 x 25 m around the spine, s 24..49
constexpr int kAnaCount = 4;                    // chambers in the well
constexpr double kAnaCupY[kAnaCount] = {4.4 + kWellCentreY, 4.4 + kWellCentreY, -4.4 + kWellCentreY, -4.4 + kWellCentreY};
constexpr double kAnaCupX[kAnaCount] = {-4.4, 4.4, -4.4, 4.4};
// Cup (gen_mesh: rim s -1.5, depth 1.8, radius 3.2): paraboloid focus f = R^2/(4 depth)
// = 1.42 m in front of the vertex (s +0.3) - the reaction sits there, just inside the rim.
constexpr double kAnaFocusS = -1.12;

constexpr int kPlanCount = 12;                  // central planetary cups, ring R 11 m behind the baffle
constexpr double kPlanR = 10.75, kPlanS = -0.6;  // ring round the well axis (y kWellCentreY)
// Four planetary pods x 3 cups on the lower chines, pairs at s 30 and s 76 (MeshLayout kPods). The bay
// door is the pod's swing arm; the pod turns on a trunnion normal to the door. Pitch in hover by the
// thrust split between the pairs: the thrust centre follows the CG.
constexpr int kPodCount = 4, kCupsPerPod = 3, kPodCups = kPodCount * kCupsPerPod;
constexpr double kPodSwivelMaxDeg = 100.0;      // 0 = thrust forward (aft-pointing cups), 90 = up
constexpr double kPodSwivelRate = 15.0;         // deg/s
constexpr double kPodSwingTime = 12.0;          // s, door arm out / in
constexpr double kPodMaxMach = 0.8;             // doors stay shut above: the pods would be torn off
constexpr bool kPodAssistOn = false;            // pods help the carriage: off until the pod model is proven
constexpr double kPodAssistShare = 0.5;         // standing up / laying down: pods carry up to this share of the weight
constexpr double kPodTvcDeg = 7.0;              // pod cups: lateral jet deflection (gust compensation)

// Wind: measured by the ship (air data: groundspeed - airspeed); Orbiter's aerodynamics apply its force.
// With Orbiter's wind off (or zero at the surface) the ship's own gust model runs instead, on the broadside.
constexpr double kWindMean = 12.0, kGustSigma = 5.0, kGustTau = 4.0;  // m/s, m/s, s (own model)
constexpr double kSideCd = 0.8, kSideArea = 2700.0;                    // m^2, lying or standing

// Leg ratings (tantra_c148.html, «Ноги: полный расчёт»): CNT composite, safety factor 1.5 on everything.
constexpr double kCntE = 0.6e12, kCntSigma = 1.0e9, kSafety = 1.5;
constexpr double kLafShinA = 0.71, kLafShinI = 0.431;     // band mast 5.4 x 1.7 m, wall 5 cm
constexpr double kSternShinA = 0.56, kSternShinI = 0.225; // 2.0 x 1.5 m, wall 8 cm
constexpr double kStandInradius = 11.5;                   // stern feet polygon, from the CG line [m]

// Mass budget (tantra_c148.html, «Массы и центры»): the dry ship (1786 t) has its CG at s 64.7;
// trap cassettes and their anamezon at s 33.9, ion charges at s 34. The vessel frame is shifted to
// the CG as propellant is used (empty 56.5, landing ~53, loaded ~35.3).
constexpr double kDryCGS = 64.7, kTrapCGS = 33.9, kIonCGS = 34.0;

// Attitude micro-motor blocks.
constexpr double kAttNoseS = 132.0, kAttNoseR = 7.0;
constexpr double kAttTailS = 8.0, kAttTailR = 13.0;
constexpr double kPmiPitch = 1500.0, kPmiRoll = 120.0;  // m^2

// Operations.
constexpr int kCrewSeats = 20;                  // 14 own + 6 from "Amat"

}  // namespace tantra::spec
