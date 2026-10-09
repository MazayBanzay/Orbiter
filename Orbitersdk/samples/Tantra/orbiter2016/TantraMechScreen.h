// TantraMechScreen: the mechanisation screen of the left glass and its keys - the mockup Tantra_Design/refine/mech_v3.html
// (the user, 2026-10-05: «Шасси. крылья. Хвост. В механизации я не вижу.») under the rules of refine/SPEC.md, drawn with the
// front glass's kit (TantraFrontScreen: GOST 2.304 type A, no text under 15 px, one key module, the blocks with their title
// bands):
//   the screen (1600 x 800): the tabs МЕХАНИЗАЦИЯ | ТЕПЛО and the state; ПОЛОЖЕНИЕ КОРПУСА (the side view, the pose's numbers);
//   ПОЛОЖЕНИЯ (five position keys over one lit scale, СТОП); ШАССИ · ОПОРЫ (the plan with the load rings, the seven supports'
//   table: Л, П, Кн, К1-К4 - out / going / touching / loaded / overloaded / damaged, % of the rating, MN; the weight, the
//   trunnions, the soles, the sink); КРЫЛЬЯ И ПЕРО (the rear view by the mesh: the inner panels' angle, the outer panels, the
//   telescopic fin; the folding scale); РАДИАТОРЫ: ГРЕБНИ И ПЕРО (out / folded, the heat shed, the temperature, the health);
//   КОРПУС · МЕХАНИЗМЫ, ПРЕДУПРЕЖДЕНИЯ, ЖУРНАЛ;
//   the shelf keys (1600 x ~570, the older v2 pult: no shelf draws it on the bridge of variant 7).
// The phases are the carriage's own; a position key sets where it goes and stops there.
#pragma once
#include "orbitersdk.h"
#include "TantraScreenCanvas.h"
#include "TantraGost.h"

#include <vector>

namespace tantra::mechscreen {

enum Cmd {
    kCmdPos0, kCmdPos1, kCmdPos2, kCmdPos3, kCmdPos4,   // В ГОРИЗОНТ, НА ТРЁХ, НА НОГИ, 75°, ВЗЛЁТНОЕ
    kCmdStop, kCmdEstop, kCmdGear, kCmdSetLevel, kCmdSetStand,
    kCmdWings, kCmdPods, kCmdRovers, kCmdPodsAft, kCmdPodsDown,
    kCmdHangar, kCmdPort, kCmdAirlock, kCmdTableUp, kCmdTableStop,
    kCmdTabMech, kCmdTabThermal,
    kCmdWing90, kCmdWing30, kCmdWingFold, kCmdLift,     // the wings' mode chosen directly; the airlock's crew lift
};

// the carriage progress of the positions (the carriage's phases 0..6; the turn is phase 2, theta = 90 * Ease(p - 2))
constexpr int kPosCount = 5;
// tripodP: НА ТРЁХ's progress (the axis at Carriage::kTripodAxisH - it depends on the CG: Carriage::LiftProgressFor)
double PositionP(int i, double tripodP);
// how far a leg is out, 0 stowed .. 1 out, by the deploy parameter of Carriage::BuildPose: the blades and the kangaroo come out
// with the gear lying (P 0), the kangaroo folds in 1.5-2, the stern legs swing out in 3-4 (with the gear in the standing set,
// none on the port's table), the blades come home in 5-6. leg: 0 blade port, 1 stbd, 2..5 stern legs, 6 kangaroo
double LegOut(int leg, double P, double gear, bool port);

// the ship as the screen shows it
struct View {
    double t = 0.0;                          // sim time [s] (the journal)
    double P = 0.0, PT = 0.0;                // carriage progress and its target (0 lying .. 6 standing)
    double tripodP = 0.2;                    // НА ТРЁХ: the progress with the axis at 32 m (on the blades and the kangaroo)
    bool held = false;                       // the carriage held (СТОП / emergency stop / balance pause)
    bool estop = false;                      // the emergency stop is on
    bool balHold = false;                    // the balance device paused the erection (sway)
    double gear = 1.0; bool gearDown = true; // gear 0..1 and its command
    bool setStand = false, port = false, grounded = true;
    double theta = 0.0, trunnionH = 24.0, mastLen = 30.5, cgRes = 0.3, sway = 0.0;   // rad, m (trunnionH: the CG over the ground)
    double sink = 0.0; int petals = 84, petalsAll = 84;   // the loaded feet's pads in the ground [m]; the feet's petals whole / all
    double weight = 0.0;                     // N
    double legN[7] = {}, legR[7] = {};       // load [N] and share of the rating: blade port, stbd, stern legs 0..3, kangaroo
    double driveMoment = 0.0, bend = 0.0;    // N m held by the trunnion drives; the blades' bending share of the rating
    int wingMode = 0;                        // 0 = 90 deg, 1 = 30 deg, 2 = folded
    double wingFold = 0.0;                   // the ground fold now (pose.tuck): 0 as set .. 1 folded flat
    double podOut = 0.0, podAngle = 0.0, podTarget = 0.0; bool podsWanted = false;
    int podBlock = 0;                        // the bays held shut (Tantra::UpdatePods): 0 no, 1 the carriage lies/moves, 2 over kPodMaxQ
    double rovers = 0.0, hangar = 0.0, hangarT = 0.0, bayDoors = 0.0;
    double irisAna = 0.0, irisNose = 0.0, marchOut = 0.0;
    bool liftStowed = true, liftAtGround = false, liftLowering = false; double liftMove = 0.0;   // the airlock lift
    bool airlock = false;                    // the airlock key's state
    int portStep = 0;                        // the port table: 0 idle
    bool hotSkin = false;                    // the hull or the stern is hot (the ТЕПЛО tab's red frame)
    // the gear: each support (the order of legN)
    double legOut[7] = {};                   // how far out (LegOut)
    int legDir[7] = {};                      // where it goes now: +1 out, -1 in, 0 still
    bool legTouch[7] = {};                   // its pads on the ground (or the stern feet planted in phase 4+)
    double legInteg[7] = {1, 1, 1, 1, 1, 1, 1};   // the damage model's integrity: 1 whole .. 0 lost
    bool legBroken[7] = {};                  // the foot's ankle or the lowest stage joint broken (core/Foot)
    double hipS = 53.4, sCG = 58.1;          // the trunnions' station, the CG's [m from the stern]
    double jam = 1.0, anchors = 0.0;         // the soles: settled 0..1, the anchors' root sintered 0..1 (core/Legs)
    bool hipCatcher = false;                 // the hip magnetic bearings overloaded: on the catchers
    double turnTime = 45.0, driveRate = 1.0; // the turn's time [s] (Tantra::UpdateGear), the drives' rate (0.6 on the catchers)
    // the wings and the fin: the mesh's animation angles (TantraGear::Apply)
    double wingInDeg = 0.0;                  // the inner panels up from the horizontal [deg] (90 deg mode 0, 30 deg mode 30, folded 85)
    double wingOutDeg = 0.0;                 // the outer panels folded down along them [deg] (0 in line .. 177)
    double wingInMax = 85.0, wingOutMax = 177.3;  // their full travel [deg] (MeshLayout kWingFoldDeg, ANIM_WING_OUTER)
    double wingRaise = 30.0;                 // the 30 deg mode (MeshLayout kWingRaiseDeg)
    double finOut = 23.0, finMax = 23.0;     // the telescopic fin out of its slot [m], its travel (MeshLayout kFinRetract)
    bool wingGround = false;                 // folded by the ground or the carriage (not by the crew's mode)
    double crestInteg[2] = {1, 1}, finInteg = 1.0;   // the damage model: the crests port, stbd, the fin
    // the radiators: the crests and the fin (Tantra::CoreState, core/Plant)
    double radiated = 0.0, radHealth = 1.0, sternT = 300.0, tSafe = 800.0, tBoil = 1100.0;   // W, 0..1, K, K, K
    bool radOut = true;                      // out (folded: a tenth)
};

class Screen {
public:
    // the screen on the glass: w x h of the mockup's pixels (1600 x 800) at (ox, oy) of the surface
    void DrawTop(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v);
    // the keys on the shelf: 1600 x h (h ~ 570)
    void DrawPult(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v);
    int HitTop(double x, double y) const { return scr::HitTest(hitsTop_, x / kTop_, y / kTop_, nullptr); }
    int HitPult(double x, double y) const { return scr::HitTest(hitsPult_, x, y, nullptr); }
    void Note(double t, const std::wstring& s, int lvl) { log_.Add(t, s, lvl); }

private:
    void Watch(const View& v);               // the journal: what changed since the last draw
    std::vector<scr::Hit> hitsTop_, hitsPult_;
    scr::Journal log_;
    bool seen_ = false;
    View last_;
    double kTop_ = 1.0;                      // the surface's px per design px (the touches arrive in the surface's)
    double foldPrev_ = -1.0, finPrev_ = -1.0, tPrev_ = -1.0;   // the wings' fold and the fin last drawn (their motion)
    int foldDir_ = 0, finDir_ = 0;
    GostFont gost_;                          // GOST type A (the screen's own: the caller hands only the Segoe font)
};

}  // namespace tantra::mechscreen
