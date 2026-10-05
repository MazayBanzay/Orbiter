// TantraPlantScreen: the power plant's screen on the right riser, left of the engine console - the mockup
// Tantra_Design/tantra_plant_screen.html (the user's, 2026-10-04) carried over as it is: its pixels (1600 wide), its colours
// (the CSS hex), its font (Segoe UI from the PlantFont atlas, tools/gen_plant_font.py, tinted), its blocks - the mimic of the
// installation, the computer's reckoning, the thermal map, the trends, the journal, the controls. The riser's area is taller
// than the mockup (1600 x ~1030): the blocks keep their sizes, their rows spread.
// In the game: the environment lamps light by themselves (no buttons); no СБРОС (repairs at a station); the thrust bar sets
// the march lever.
// The mimic reads the energy core (core/TantraCore, 2026-10-05): every node its lamp (выкл / готов / работа / предел / отказ /
// потерян), its readings and its reason straight from the core's snapshot - the screen invents no number of its own (what
// neither the core nor the plant models shows «—»). The ВЭУ and the field store are nodes of their own.
#pragma once
#include "orbitersdk.h"
#include "../core/Plant.h"
#include "../core/TantraCore.h"

#include <vector>

namespace tantra { class ScreenFont; }   // TantraScreenFont.h

namespace tantra::plantscreen {

enum Cmd { kCmdStart, kCmdMassAuto, kCmdMassArgon, kCmdMassIron, kCmdMassProducts, kCmdFieldDown, kCmdFieldUp, kCmdLimiter,
           kCmdPowerDown, kCmdPowerUp, kCmdPods, kCmdThrottle };

// the ship round the plant, as the screen shows it
struct View {
    const tantra::plant::Plant* plant = nullptr;
    tantra::plant::Output out;
    double sysNow = 0.0;              // the limiter's clock (system seconds)
    double thr = 0.0;                 // the march lever 0..1
    bool thrOwn = true;               // the march is the main engine (else the lever belongs to the anamezon)
    int env = 0;                      // 0 atmosphere, 1 space, 2 cruise (the Sun's field), 3 entry
    bool onGround = false;
    double g = 0.0;                   // gravity for thrust / weight [m/s^2]; 0 away from a body
    double mass = 0.0, argon = 0.0, iron = 0.0;   // kg
    int pods = 0;                     // 0 in the bays, 1 out, 2 thrusting
    double podCup = 117e6;            // N per pod cup at full
    double podThrust = 0.0, podLevel = 0.0;
    double skin[7] = {}, skinLim[7] = {}, flux[7] = {};   // core/Damage zones: nose, belly, wing edges, fin, stern, gear, pods
    double aoa = 0.0, alt = 0.0, vAir = 0.0, q = 0.0, gLoad = 0.0;
    bool hullLost = false;
    tantra::tcore::Snapshot core;     // the energy core's nodes and readings (TantraDisplays::FillCoreView)
};

class Screen {
public:
    Screen();
    ~Screen();
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;
    // draws at (ox, oy) of the sketchpad's surface; w is the mockup's 1600, h >= 800; dt: the time since the last draw
    void Draw(oapi::Sketchpad* skp, int ox, int oy, int w, int h, const View& v, double dt);
    // the command under a point of the screen (its own pixels), -1 none; along: 0..1 along the thrust bar
    int Hit(double x, double y, double* along) const;

    struct Area { double x, y, w, h; int cmd; };
private:
    tantra::ScreenFont* font_ = nullptr;   // the Segoe UI atlas (one per ship: its texture lives with the session)
    std::vector<Area> hits_;
    double flow_ = 0.0, pellet_ = 0.0;   // the pipes' dots, the burn's flashes (the capsules fed)
};

}  // namespace tantra::plantscreen
