// Tantra core: geometry and fixed design numbers. No Orbiter dependencies.
// Layout "T9" (Tantra_Design/DESIGN_LOCAL.md 2026-10-02, docs/T9_PLAN.md): the T8 hull with a 9 m insert at s 88
// (argon charges), stern clover of four fixed anamezon cups and the marching planetary cup on a sliding mount in the
// central well, two retro anamezon cups in the nose, four planetary pods in flank bays at CG height on telescopic
// arms, two blade legs with umbrella feet, four stern legs of four sections, one kangaroo leg (third support lying).
// Mesh-side geometry (pods, legs, cups, doors) comes from orbiter2016/MeshLayout.h. Tunables live in Params.h.
#pragma once
#include "Physics.h"

namespace tantra::spec {

// Vessel frame: +z forward, +y up, +x starboard. s = metres from the stern plane.
constexpr double kLength = 177.968;
constexpr double kOriginS = 60.0;               // mesh origin; the vessel frame follows the CG (Tantra::frameS_)
constexpr double kSternZ = -kOriginS;
constexpr double kNoseZ = kSternZ + kLength;
constexpr double Z(double s) { return s - kOriginS; }

constexpr double kAftHalfWidth = 13.55;         // body and fairings, s 40..121 (chines; the hangar 13.35)
constexpr double kAftTop = 10.92, kAftBottom = -7.28;
constexpr double kForeHalfWidth = 7.8, kForeTop = 8.16, kForeBottom = -5.44;  // crew section and screen, s 136..152
constexpr double kAxisHeight = 24.0;            // hull axis above ground, lying on the gear (folded blade + cup foot + 2.5 m travel)
constexpr double kWellRadius = 10.9;            // nacelle cluster at the stern (centres +-4.8, R 4.1)
constexpr double kWellCentreY = 1.82;           // stern axis (mid-height of the stern section)
constexpr double kTvcMaxDeg = 10.0;             // magnetic nozzles: jet deflection by the field, no hinges

// Undercarriage (core/Carriage, orbiter2016/MeshLayout.h).
constexpr double kColumnX = 19.0;                // hips of the blade legs (deployed)
// (2026-10-09) the stern legs stow in body bays (s 18.4-50, hinges at s 49.6, the feet aft); standing feet on
// MeshLayout kStandR = 33 m: friction 0.38 at the feet, tip-over 13.1 deg empty
constexpr double kStandClear = 22.5;             // stern plane above ground standing: marching cup lip 16 m up
constexpr double kStandInradius = 20.3;          // stern feet polygon (R 33, legs on the body diagonals), from the CG line [m]

// Stations (after the insert).
constexpr double kHangarS0 = 99.6, kHangarS1 = 120.6;     // closed port: XR2 / DG-IV class craft
constexpr double kDoorS = 129.0;                          // port airlock
constexpr double kControlPostS = 137.0;
constexpr double kShoulderS0 = 144.0, kShoulderS1 = 152.0;  // water/charges screen, deflector coil at the nose root

// Engines.
constexpr int kTrapCount = 4;                   // traps: cassettes 8.1 m square, 2x2 columns, s 53..87.6 (2026-10-09)
constexpr int kAnaCount = 4;                    // fixed anamezon cups in the stern clover
constexpr double kAnaCupY[kAnaCount] = {4.8 + kWellCentreY, 4.8 + kWellCentreY, -4.8 + kWellCentreY, -4.8 + kWellCentreY};
constexpr double kAnaCupX[kAnaCount] = {-4.8, 4.8, -4.8, 4.8};
// Cup (gen_mesh: rim s -2, aperture R 3.0, depth 1.5): focus f = R^2/(4 depth) = 1.5 m in front of the vertex,
// i.e. in the rim plane - every ray from the reaction forward hits the cup: the ship stands in its shadow.
constexpr double kAnaFocusS = -2.0;
// (2026-10-09) the stern blocks are dual-mode: on the ion charges in the air (the plant's cascade fusion in the cup, its field
// cap at 12.1 T x pi 3.0^2 = 1.65 GN a block), on the anamezon above. The cup area over the marching cup's:
constexpr double kSternPlanAreaRatio = (3.0 * 3.0) / (2.2 * 2.2);
// (2026-10-09, variant Б: 276 kt; the user: the central marching cup removed) the stern blocks are the planetary main engine:
// the charges at 15 T in their own chamber coils (the anamezon coils, under their ~16 T quench): 2.53 GN a block, with the pods
// 10.8 GN - T/W 1.60, 1.22 with a block out
constexpr double kSternPlanTesla = 15.0;
constexpr int kRetroCount = 2;                  // nose retro anamezon cups (MeshLayout kNoseCup*), jets forward
constexpr double kRetroAreaFrac = 0.54;         // (2.2 / 3.0)^2 of a stern cup
// Marching planetary cup (MeshLayout kMarch*): one cup R 2.2 in the central well, 12.1 T, run out past the rims to
// thrust. Reaction mass: argon below kMarchArgonAlt (jet ~30 km/s, no burning in air), iron above (300 km/s).
constexpr double kMarchArgonAlt = 30.0e3;       // m (the plant's air mode)
// (2026-10-09) the cups' reaction mass by the air, not the height: iron only in air thinner than kIronMaxRho and over
// kArgonGroundAlt (on airless bodies argon too near the ground); the pods' argon jet is slower near the ground and in
// dense air (ShipParams argonGroundExhaust, argonDenseExhaust): its power on the ground 400 -> ~90 GW on the Moon.
constexpr double kIronMaxRho = 2.0e-5;          // kg/m^3
constexpr double kArgonGroundAlt = 1.0e3;       // m over the ground
constexpr double kArgonDenseAlt = 10.0e3, kDenseRho = 0.1;   // m over the ground, kg/m^3 (Titan, Earth)
// Four planetary pods x 3 cups in the flank bays at CG height (MeshLayout kPods): arm out, the pod turns about the arm
// axis, the cups going down (0 cups aft .. ~62 thrust vertical .. 180 forward; Tantra::PodVerticalDeg). Jets splay
// out / down away from the hull.
constexpr int kPodCount = 4, kCupsPerPod = 3, kPodCups = kPodCount * kCupsPerPod;
constexpr double kPodSwivelMaxDeg = 180.0;
constexpr double kPodSwivelRate = 15.0;         // deg/s
constexpr double kPodSwingTime = 12.0;          // s, arm out / in
constexpr double kPodMaxQ = 45.0e3;             // Pa: the bays open below this dynamic pressure (doors and arms rated x kSafety),
                                                // the wings folded or not (stern-first descent)
constexpr double kPodHoverCantDeg = 15.0;       // hover: the aft pair leans its thrust forward, the fore pair aft - the jets
                                                // keep ~7 m off the blade feet (3.4 % of the lift)
constexpr double kPodSplayOutDeg = 15.0, kPodSplayDownDeg = 25.0;
constexpr bool kPodAssistOn = false;            // pods help the legs stand the ship up: off (the legs do it alone)
constexpr double kPodAssistShare = 0.5;
constexpr double kPodTvcDeg = 7.0;              // pod cups: lateral jet deflection (gust compensation)

// Balance device («устройство равновесия»): the trunnion drives hold the hull at the commanded erection angle, the
// blade drives hold roll; the erection pauses while the hull sways and resumes once it is calm. Gains for the inverted
// pendulum m g h ~ 4e10 N m/rad at 80 m: the drive beats it only at small angles - hence the pause thresholds.
constexpr double kBalanceMoment = 2.0e9;                  // drive moment available for correction [N m]
constexpr double kBalanceKp = 8.0e10, kBalanceKd = 1.3e11; // N m/rad, N m s/rad
constexpr double kBalancePauseDeg = 2.5, kBalanceResumeDeg = 1.0;        // sway over the static lean (fast part)
constexpr double kBalancePauseRate = 0.05, kBalanceResumeRate = 0.02;    // rad/s beyond the commanded motion (the contact solver jitters at ~0.02)
constexpr double kBalanceCalmTime = 2.0;                   // s calm before the erection resumes
constexpr double kBalanceBiasTime = 5.0;
constexpr double kBalanceRampTime = 6.0;     // s, soft start of the erection after a balance pause
constexpr double kBalanceRateHold = 0.5;     // s, the sway rate must stay over the limit this long to pause
// Time acceleration: Orbiter's explicit contact integration is unstable at large steps - on the ground above this
// warp the ship is frozen in place (landed status), the erection pauses, the balance device rests.
constexpr double kMechSwayOmega = 1.5;   // the hull on the trunnions held by the drives: sway frequency [rad/s]
constexpr double kMechSwayZeta = 0.5;    // and damping (the balance device)
constexpr double kWarpFreeze = 10.0;                   // s: the slow zero follows the static lean of the hull on its feet

// Wind: measured by the ship (air data: groundspeed - airspeed); Orbiter's aerodynamics apply its force.
// With Orbiter's wind off (or zero at the surface) the ship's own gust model runs instead, on the broadside.
constexpr double kWindMean = 12.0, kGustSigma = 5.0, kGustTau = 4.0;  // m/s, m/s, s (own model)
constexpr double kSideCd = 0.8, kSideArea = 2600.0;                    // m^2, lying or standing

// Leg ratings (DESIGN_LOCAL «Ноги Т8 - окончательно», «Передняя нога-кенгуру»): safety 1.5 on everything.
constexpr double kCntE = 0.6e12, kCntSigma = 1.0e9, kSafety = 1.5;   // CNT composite (the struts, the guys)
// (2026-10-09, the full ship) the stages' walls are boron-nitride ceramic («боразон», canon; PCBN lower bounds: compression
// 3 GPa, E 0.65 TPa, 3.45 t/m3) - the same envelopes, thinner walls; MR physics carries the rest:
//   * the blades are guyed at mid-length to the rims of their own feet (CNT cables D 0.66 m on MR winches): the weak axis
//     buckles in two halves - effective length x kGuyK;
//   * the stern legs and the front support stand with MR-locked ends (hinge / knee and ankle braked, the foot's root
//     sintered): x kLockK; landing (free ankles) they are pinned columns - kLandMass;
//   * the blade hips ride 20 T magnetic bearings 32 m^2 (Legs.h): the full load without the catchers.
constexpr double kLegE = 0.65e12, kLegSigma = 3.0e9;
constexpr double kGuyK = 0.5, kLockK = 0.5;
// (2026-10-09, the user: «усиль без переработки» - a careful landing of the full ship) the stern legs land with their hull
// hinges MR-braked (as standing) and the ankles free: fixed-pinned columns x kLandLockK (Pcr 1.65 GN, was pinned 0.81) until
// the roots sinter (x kLockK). The MR struts add over the weight only what the columns take x kSafety (Tantra::UpdateGear):
// the full 280 kt on a 1 g planet lands at <= 3.3 m/s with +0.54 g (f 1.54), on dense ground only (loose sand f 0.30).
constexpr double kLandLockK = 0.7;
// (2026-10-09) the stern blocks' magnetic jet deflection (an asymmetric nozzle field, no moving parts), as the march cup's
// TVC the autopilots were built for (TantraGuidance kTvcM)
constexpr double kSternTvcDeg = 10.0;
// Telescopes as STEPPED columns (FE Euler, pinned ends): I = the uniform column with the same critical force at the
// reference length, A = the smallest section (strength).
constexpr double kBladeA = 1.718, kBladeI = 1.339;          // 6 stages 6.7x2.8 .. 5.2x1.3, BN walls 0.138 (Pcr 1.45 GN at 79.4 m)
constexpr double kBladeIStrong = 8.49;                      // the same about the strong axis (along the hull)
constexpr double kSternShinA = 0.895, kSternShinI = 0.648;  // 5 sections 3.0x2.5 .. 1.97x1.64, BN walls 0.134 (Pcr 0.81 GN)
constexpr double kSternLegL = 71.7;                        // hinge -> ankle, standing (2026-10-09: hinge at s 49.6)
constexpr double kKangShinA = 0.279, kKangShinI = 0.145;    // shin 9 sections 2.2 .. 1.08, BN walls 0.069 (Pcr 0.21 GN)
// MR-locked ankle on a sintered root: the foot's rocking stiffness on dense ground (8 G R^3 / 3(1-nu), G 100 MPa, R 12.2);
// the trunnion held by the hip motor-generators (magnetic, position hold) - the columns stand fore-aft at the full mass.
constexpr double kAnchorKa = 6.9e11, kTrunnionHoldK = 4.0e11;   // N m/rad
// Design cases (2026-10-09): the full ship kFullMass on a 2.5 g planet - lying, the turn on the blades, standing - with the
// guys and the locks; landing kLandMass with free ankles and the MR struts adding up to kStrutExtraG over the weight.
constexpr double kDesignG = 2.5, kStrutExtraG = 1.5;
constexpr double kLandMass = 52.0e6;                      // touchdown rating: stern legs, cup feet on loose sand (arrival ~45 kt)
constexpr double kLaunchMass = kLandMass;                 // (old name: the touchdown cases)
// (2026-10-09, canon, variant Б of the user) the full ship 276 kt (4 x 57.6 kt anamezon, 4 x 817 t traps, 30.3 kt charges,
// 4 kt iron, dry 7.88 kt with these supports) + 3.6 kt of the transport kit if it flies = the supports' design mass.
constexpr double kFullMass = 280.0e6;
constexpr double kBladeColumnL = 79.4, kKangColumnL = 66.0;   // standing / lifted column lengths [m]
// Cup feet («чаша опоры», mesh: tools/gen_mesh.py FOOT_KINDS): R for loose sand at touchdown (2.5 g x1.5, 0.9 m skirt),
// 12 box ribs 0.5 x depth (walls 60 mm), two struts per rib (0.5 L and the tip) from a mast on the hub, CNT canopy.
constexpr double kFootRStern = 11.25, kFootRBlade = 12.2, kFootRKang = 6.35, kFootSkirt = 0.9;
constexpr double kFootRibDepthStern = 0.68, kFootRibDepthBlade = 0.84, kFootRibDepthKang = 0.30;
constexpr double kFootStrutDStern = 0.58, kFootStrutDBlade = 0.69, kFootStrutDKang = 0.29;   // outer struts (to the rib tips)

// Mass budget (2026-10-09, Tantra_Design refactor_T9_concept §3, ±430 t): structure and systems 5 832 t at s 65.4 -
// stern frame 400 (s 9), planetary installation 600 (s 8), gyros 270, ВЭУ 150, field store 100, radiators 30 and iron
// racks 115 (s 24-30), hull 338, hangar kit 200, blades 1 014 (s 64), pods 400, stern legs 760 (in their bays, s 37),
// argon tanks 190, nose cups 120, nose 200, screen 223, coil 60, armour 60, feed lines 50, crew zone, bridge drum and
// systems 416, front support 80; with the hangar cargo (not the lander and the MPU), crew and consumables 5 992 t at
// s 67.1. Traps: 4 x 133 t, cassettes s 53-87.6 (the stern-leg bays end at s 50), CG s 70.3. The legs and the
// cassettes moved the CG forward; it comes back by the layout (2026-10-09): the iron charges on a rack s 39-47 and an
// aft argon tank (1.5 kt) s 30-38, both in the free middle between the stern-leg bays; the body argon tanks at s 60.8,
// the insert's at s 92.5. Argon drains insert first, then the body tanks, the aft tank last.
// Launch 54.2 kt (4 x 9.37 kt) at s 67.3; on a planet with iron and argon 16.7 kt at s 60.4; iron spent, argon full
// 12.7 kt at s 65.9; argon down to 3 kt s 61.1; empty 6.5 kt at s 67.3 - every state inside the trunnion track
// (53.4..71.6, >= 4 m to spare) and in the blades' reach (stand at the nominal height, marching cup lip 16 m up).
// (2026-10-09, variant Б) dry 7 880 t: + the supports for the full ship (blades 1 014 -> 2 012 t at s 64, stern legs
// 760 -> 1 372 t at s 37, front support 80 -> 198 t at s 95, guys 110 t, MR locks and 20 T hip drums ~50 t) - CG s 64.7.
constexpr double kDryCGS = 64.7, kTrapCGS = 70.3, kIronCGS = 43.0, kArgonBodyCGS = 60.8, kArgonInsertCGS = 92.5;
constexpr double kArgonAftCGS = 34.0;
// (2026-10-09) the ion charges are dense now (30.3 kt): body tanks 19.5 kt, aft tank 10.8 kt; the insert holds none (it
// is free for the transport kit - Transport). The AFT tank drains first now: with empty traps and full charges the CG is
// s 54.2, the body tanks hold it inside the trunnion track as the charges go.
constexpr double kArgonBodyShare = 0.644;       // of the charges: body tanks
constexpr double kArgonAftShare = 0.356;        // aft tank; the insert 0

// Attitude micro-motor blocks.
constexpr double kAttNoseS = 164.0, kAttNoseR = 6.0;
constexpr double kAttTailS = 8.0, kAttTailR = 10.0;
constexpr double kPmiPitch = 2200.0, kPmiRoll = 110.0;  // m^2

// Operations.
constexpr int kCrewSeats = 20;                  // 14 own + 6 from "Amat"

}  // namespace tantra::spec
