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
constexpr double kStandR = 36.0;                 // stern feet circle (legs at +-30 deg off the horizontal)
constexpr double kStandClear = 22.5;             // stern plane above ground standing: marching cup lip 16 m up
constexpr double kStandInradius = 18.0;          // stern feet polygon (R 36 at +-30 deg), from the CG line [m]

// Stations (after the insert).
constexpr double kHangarS0 = 99.6, kHangarS1 = 120.6;     // closed port: XR2 / DG-IV class craft
constexpr double kDoorS = 129.0;                          // port airlock
constexpr double kControlPostS = 137.0;
constexpr double kShoulderS0 = 144.0, kShoulderS1 = 152.0;  // water/charges screen, deflector coil at the nose root

// Engines.
constexpr int kTrapCount = 4;                   // traps D 8 x 66.6 m, 2x2 columns, s 21..87.6
constexpr int kAnaCount = 4;                    // fixed anamezon cups in the stern clover
constexpr double kAnaCupY[kAnaCount] = {4.8 + kWellCentreY, 4.8 + kWellCentreY, -4.8 + kWellCentreY, -4.8 + kWellCentreY};
constexpr double kAnaCupX[kAnaCount] = {-4.8, 4.8, -4.8, 4.8};
// Cup (gen_mesh: rim s -2, aperture R 3.0, depth 1.5): focus f = R^2/(4 depth) = 1.5 m in front of the vertex,
// i.e. in the rim plane - every ray from the reaction forward hits the cup: the ship stands in its shadow.
constexpr double kAnaFocusS = -2.0;
constexpr int kRetroCount = 2;                  // nose retro anamezon cups (MeshLayout kNoseCup*), jets forward
constexpr double kRetroAreaFrac = 0.54;         // (2.2 / 3.0)^2 of a stern cup
// Marching planetary cup (MeshLayout kMarch*): one cup R 2.2 in the central well, 12.1 T, run out past the rims to
// thrust. Reaction mass: argon below kMarchArgonAlt (jet ~30 km/s, no burning in air), iron above (300 km/s).
constexpr double kMarchArgonAlt = 30.0e3;       // m
// Four planetary pods x 3 cups R 0.8 in the flank bays at CG height (MeshLayout kPods): telescopic arm out, the
// pod turns about the arm axis (0 cups aft .. 90 down .. 180 forward). Jets splay out / down away from the hull.
constexpr int kPodCount = 4, kCupsPerPod = 3, kPodCups = kPodCount * kCupsPerPod;
constexpr double kPodSwivelMaxDeg = 180.0;
constexpr double kPodSwivelRate = 15.0;         // deg/s
constexpr double kPodSwingTime = 12.0;          // s, arm out / in
constexpr double kPodMaxMach = 0.8;             // bays stay shut above: the pods would be torn off
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

// Leg ratings (DESIGN_LOCAL «Ноги Т8 - окончательно», «Передняя нога-кенгуру»): CNT composite, safety 1.5 on everything.
constexpr double kCntE = 0.6e12, kCntSigma = 1.0e9, kSafety = 1.5;
// Telescopes as STEPPED columns (FE Euler, pinned ends): I = the uniform column with the same critical force at the
// reference length, A = the smallest section (strength).
constexpr double kBladeA = 1.86, kBladeI = 1.433;          // 6 stages 6.7x2.8 .. 5.2x1.3, walls 0.15 (Pcr 1346 MN at 79.4 m)
constexpr double kSternShinA = 0.782, kSternShinI = 0.548; // 4 sections 3.0x2.5 .. 1.91x1.59, walls 0.12 (Pcr 917 MN at 59.5 m)
constexpr double kSternLegL = 59.5;                       // hinge -> ankle, standing
constexpr double kKangShinA = 0.245, kKangShinI = 0.129;  // shin 9 sections 2.2 .. 1.08, walls 0.06 (Pcr 175 MN at 66 m)
// Design case of every support (2026-10-02): launch mass on a 2.5 g planet, load x kSafety within the strength and
// the buckling force; the stern legs land with the MR struts adding up to kStrutExtraG over the weight.
constexpr double kDesignG = 2.5, kLaunchMass = 52.34e6, kStrutExtraG = 1.5;
constexpr double kBladeColumnL = 79.4, kKangColumnL = 66.0;   // standing / lifted column lengths [m]
// Cup feet («чаша опоры», mesh: tools/gen_mesh.py FOOT_KINDS): R for loose sand at touchdown (2.5 g x1.5, 0.9 m skirt),
// 12 box ribs 0.5 x depth (walls 60 mm), two struts per rib (0.5 L and the tip) from a mast on the hub, CNT canopy.
constexpr double kFootRStern = 11.25, kFootRBlade = 12.2, kFootRKang = 6.35, kFootSkirt = 0.9;
constexpr double kFootRibDepthStern = 0.68, kFootRibDepthBlade = 0.84, kFootRibDepthKang = 0.30;
constexpr double kFootStrutDStern = 0.58, kFootStrutDBlade = 0.69, kFootStrutDKang = 0.29;   // outer struts (to the rib tips)

// Mass budget (T9, Tantra_Design mass_t9): dry ship 4 203 t (stern frame 400 at s 9, structure 263 over s 18..121,
// hangar 200, crew and systems 486, nose shield 200, nose cups 120, blades 1 014 at s 64, pods 480, stern legs 440 at
// s 18, planetary installation 600 at s 8) with its CG at s 65.6; traps and their anamezon at s 54.3; iron charges in
// the slit between the columns at s 53.9; argon in the body at s 60.8 and in the insert at s 92.5. Launch 52.3 kt at
// s 58.1; empty 4.9 kt at s 64.1 - always inside the trunnion track (53.4..71.6).
constexpr double kDryCGS = 65.6, kTrapCGS = 54.3, kIronCGS = 53.9, kArgonBodyCGS = 60.8, kArgonInsertCGS = 92.5;
constexpr double kArgonBodyShare = 0.44;        // of the argon: body tanks (2.73 of 6.2 kt), the rest in the insert

// Attitude micro-motor blocks.
constexpr double kAttNoseS = 164.0, kAttNoseR = 6.0;
constexpr double kAttTailS = 8.0, kAttTailR = 10.0;
constexpr double kPmiPitch = 2200.0, kPmiRoll = 110.0;  // m^2

// Operations.
constexpr int kCrewSeats = 20;                  // 14 own + 6 from "Amat"

}  // namespace tantra::spec
