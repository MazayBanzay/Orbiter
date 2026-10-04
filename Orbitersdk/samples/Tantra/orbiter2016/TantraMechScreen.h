// TantraMechScreen: the mechanisation screen of the left riser and its keys on the left wing's shelf - the mockup
// Tantra_Design/tantra_mech_screen.html v2 (the user, 2026-10-04) carried over in its own pixels and colours:
//   the screen (1600 x 800): a narrow band of the pose and the gear (~20 %), the supports with their loads (plan, % of the rating),
//   the hull's mechanisms with the rear view, the warnings, the journal; tabs МЕХАНИЗАЦИЯ | ТЕПЛО in the title bar;
//   the shelf (1600 x ~570): ONE lit scale with four position keys - В ГОРИЗОНТ (lying), НА НОГИ (lifted to the top),
//   75° (the stele: lifted and turned to 75 deg), ВЗЛЁТНОЕ (on the four stern legs) - and СТОП; the gear, hull, hangar/port/
//   airlock groups and the emergency stop. The phases are the carriage's own; a key sets where it goes and stops.
#pragma once
#include "orbitersdk.h"
#include "TantraScreenCanvas.h"

#include <vector>

namespace tantra::mechscreen {

enum Cmd {
    kCmdPos0, kCmdPos1, kCmdPos2, kCmdPos3, kCmdPos4,   // В ГОРИЗОНТ, НА ТРЁХ, НА НОГИ, 75°, ВЗЛЁТНОЕ
    kCmdStop, kCmdEstop, kCmdGear, kCmdSetLevel, kCmdSetStand,
    kCmdWings, kCmdPods, kCmdRovers, kCmdPodsAft, kCmdPodsDown,
    kCmdHangar, kCmdPort, kCmdAirlock, kCmdTableUp, kCmdTableStop,
    kCmdTabMech, kCmdTabThermal,
};

// the carriage progress of the four positions (the carriage's phases 0..6; the turn is phase 2, theta = 90 * Ease(p - 2))
constexpr int kPosCount = 5;
// tripodP: НА ТРЁХ's progress (the axis at Carriage::kTripodAxisH - it depends on the CG: Carriage::LiftProgressFor)
double PositionP(int i, double tripodP);

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
    double theta = 0.0, trunnionH = 24.0, mastLen = 30.5, cgRes = 0.3, sway = 0.0;   // rad, m
    double weight = 0.0;                     // N
    double legN[7] = {}, legR[7] = {};       // load [N] and share of the rating: blade port, stbd, stern legs 0..3, kangaroo
    double driveMoment = 0.0, bend = 0.0;    // N m held by the trunnion drives; the blades' bending share of the rating
    int wingMode = 0;                        // 0 = 90 deg, 1 = 30 deg, 2 = folded
    double wingFold = 0.0;                   // the folding now 0 (as set) .. 1 (folded flat)
    double podOut = 0.0, podAngle = 0.0, podTarget = 0.0; bool podsWanted = false;
    double rovers = 0.0, hangar = 0.0, hangarT = 0.0, bayDoors = 0.0;
    double irisAna = 0.0, irisNose = 0.0, marchOut = 0.0;
    bool liftStowed = true, liftAtGround = false; double liftMove = 0.0;   // the airlock lift (0 stowed .. 1 at the ground)
    bool airlock = false;                    // the airlock key's state
    int portStep = 0;                        // the port table: 0 idle
    bool hotSkin = false;                    // the hull is hot (the ТЕПЛО tab blinks)
};

class Screen {
public:
    // the screen on the riser: w x h of the mockup's pixels (1600 x 800) at (ox, oy) of the surface
    void DrawTop(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v);
    // the keys on the shelf: 1600 x h (h ~ 570)
    void DrawPult(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v);
    int HitTop(double x, double y) const { return scr::HitTest(hitsTop_, x, y, nullptr); }
    int HitPult(double x, double y) const { return scr::HitTest(hitsPult_, x, y, nullptr); }
    void Note(double t, const std::wstring& s, int lvl) { log_.Add(t, s, lvl); }

private:
    void Watch(const View& v);               // the journal: what changed since the last draw
    std::vector<scr::Hit> hitsTop_, hitsPult_;
    scr::Journal log_;
    bool seen_ = false;
    View last_;
};

}  // namespace tantra::mechscreen
