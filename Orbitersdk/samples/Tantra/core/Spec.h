// Tantra core: geometry and fixed design numbers. No Orbiter dependencies.
// Layout "T8" (Tantra_Design/DESIGN_LOCAL.md, tantra_mockup.html «Т8 · итог»): hull built round its contents -
// stern frame with four nacelles (anamezon cup + 3 planetary cups each), 2x2 trap columns D 8 m with lift shafts,
// fairings of the carriage legs and pods, closed hangar port for an XR2 class craft, crew modules and screen,
// blunted iridium nose; two-panel wings (90 / 30 deg / folded), telescopic dorsal fin, body flap; carriage gear.
// Mesh-side geometry (pods, legs, doors) comes from orbiter2016/MeshLayout.h.
// Tunable numbers live in Params.h / Tantra.cfg.
#pragma once
#include "Physics.h"

namespace tantra::spec {

// Vessel frame: +z forward, +y up, +x starboard. s = metres from the stern plane.
constexpr double kLength = 168.968;
constexpr double kOriginS = 60.0;               // mesh origin; the vessel frame follows the CG (Tantra::frameS_)
constexpr double kSternZ = -kOriginS;
constexpr double kNoseZ = kSternZ + kLength;
constexpr double Z(double s) { return s - kOriginS; }

constexpr double kAftHalfWidth = 13.55;         // body and fairings, s 40..112 (chines; the hangar 13.35)
constexpr double kAftTop = 10.92, kAftBottom = -7.28;
constexpr double kForeHalfWidth = 7.8, kForeTop = 8.16, kForeBottom = -5.44;  // crew section and screen, s 127..143
constexpr double kAxisHeight = 14.0;            // hull axis above ground, resting level
constexpr double kWellRadius = 10.9;            // nacelle cluster at the stern (centres +-4.8, R 4.1)
constexpr double kWellCentreY = 1.82;           // stern axis (mid-height of the stern section)
constexpr double kTvcMaxDeg = 7.0;              // magnetic nozzles: jet deflection by the field, no hinges

// Undercarriage: carriage columns and stern legs (core/Carriage, orbiter2010/MeshLayout.h).
constexpr double kColumnX = 19.0;                // hips of the carriage legs (deployed)
constexpr double kColumnFootHalf = 8.0;          // carriage-leg pads 16 x 6 m (fore-aft)
constexpr double kStandR = 33.0;                 // stern pads (MeshLayout kStandR, feet per leg; legs at +-30 deg)
constexpr double kTurnClear = 16.0, kStandClear = 12.0;

// Stations.
constexpr double kHangarS0 = 90.6, kHangarS1 = 111.6;     // closed port: XR2 / DG-IV class craft
constexpr double kDoorS = 120.0;                          // port airlock
constexpr double kControlPostS = 128.0;
constexpr double kShoulderS0 = 135.0, kShoulderS1 = 143.0;  // water/charges screen, deflector coil at the nose root

// Engines.
constexpr int kTrapCount = 4;                   // traps D 8 x 66.6 m, 2x2 columns, s 21..87.6
constexpr int kAnaCount = 4;                    // chambers in the well
constexpr double kAnaCupY[kAnaCount] = {4.8 + kWellCentreY, 4.8 + kWellCentreY, -4.8 + kWellCentreY, -4.8 + kWellCentreY};
constexpr double kAnaCupX[kAnaCount] = {-4.8, 4.8, -4.8, 4.8};
// Cup (gen_mesh: rim s -2, aperture R 3.0, depth 1.5): focus f = R^2/(4 depth) = 1.5 m in front of the vertex,
// i.e. in the rim plane - every ray from the reaction forward hits the cup: the ship stands in its shadow.
constexpr double kAnaFocusS = -2.0;

constexpr int kPlanCount = 12;                  // planetary cups, three on the rim of each nacelle (MeshLayout kPlanCup)
constexpr double kPlanS = -2.25;
// Four planetary pods x 3 cups in the fairings on the lower chines, pairs at s 43.6 and s 78 (MeshLayout kPods). The bay
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
constexpr double kSideCd = 0.8, kSideArea = 2450.0;                    // m^2, lying or standing

// Leg ratings (tantra_c148.html, «Ноги: полный расчёт»): CNT composite, safety factor 1.5 on everything.
constexpr double kCntE = 0.6e12, kCntSigma = 1.0e9, kSafety = 1.5;
constexpr double kLafShinA = 0.85, kLafShinI = 0.66;      // band mast 6.2 x 2.0 m (T8: up to 94 m; loaded 2.5 g erection 0.96)
constexpr double kSternShinA = 0.56, kSternShinI = 0.225; // 2.0 x 1.5 m, wall 8 cm
constexpr double kStandInradius = 15.0;                   // stern feet polygon (R 33 at +-27 deg), from the CG line [m]

// Mass budget (T8): the dry ship (1786 t: stern frame 400 t at s 9, structure 240 t over s 18..112, carriage legs and
// pods 260 t in the fairings at s 60, hangar 200 t, crew and systems 486 t over s 112..159, nose shield 200 t) has its
// CG at s 86.2; traps and their anamezon at s 54.3, ion charges at s 36. The vessel frame follows the CG
// (empty 77.6 - ahead of the carriage track, landing ~71, loaded ~53.8).
constexpr double kDryCGS = 86.2, kTrapCGS = 54.3, kIonCGS = 36.0;

// Attitude micro-motor blocks.
constexpr double kAttNoseS = 155.0, kAttNoseR = 6.0;
constexpr double kAttTailS = 8.0, kAttTailR = 10.0;
constexpr double kPmiPitch = 2000.0, kPmiRoll = 110.0;  // m^2

// Operations.
constexpr int kCrewSeats = 20;                  // 14 own + 6 from "Amat"

}  // namespace tantra::spec
