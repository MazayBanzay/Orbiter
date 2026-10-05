// TantraParamsScreen: the front glass's ПАРАМЕТРЫ page - pageParams() of Tantra_Design/refine/front_v3.html (the low wide glass,
// the user's «ШИРЕ», 2026-10-05) drawn with the front glass's kit (TantraFrontScreen). What the side monitors' НАСТР. page and
// the ПОЛЁТ page's ГЛАВНЫЙ ЭКРАН set, gathered on one page:
//   HUD: СЛОИ, ЦВЕТ, ЯРКОСТЬ (top left): the HUD's layers (ТАНГАЖ, ВЕКТОР, ЛЕНТЫ, ТРЕВОГИ), its colours (keys with the swatches),
//   its brightness (− value +), ПО СВЕТУ (the brightness follows the daylight at the ship);
//   ГЛАВНЫЙ ВИД (top centre, right under the big screen): the main view in small with that HUD in its colours;
//   ЧТО МЕНЯЕТСЯ (top right): what the settings act on, in words;
//   ВИД НА ГЛАВНОМ ЭКРАНЕ (bottom left, to the hand): ГОРИЗОНТ, ОРБИТА, СТЫКОВКА, ВЫКЛ with a line on each over its key;
//   MFD (brightness, contrast, resolution) and РСУ И ЕДИНИЦЫ (bottom right).
// A key gives its command and a value (Hit's third argument): the mode or the number it sets, or the step of a − + pair. The ship
// keeps the settings and applies them (TantraDisplays::ParamsCommand); the screen only shows them.
#pragma once
#include "orbitersdk.h"
#include "TantraFrontScreen.h"

#include <vector>

namespace tantra::paramsscreen {

enum Layer { kLayerLadder, kLayerFpm, kLayerTapes, kLayerAlerts, kLayerCount };   // ТАНГАЖ (the ladder), ВЕКТОР (velocity), ЛЕНТЫ, ТРЕВОГИ
constexpr int kPaletteCount = 3;                                                  // ОРАНЖ + ЦИАН, ЦИАН + ОРАНЖ, ИЗУМРУД

// the keys; the value each gives through Hit(.., along)
enum Cmd {
    kCmdHud,            // ГОРИЗОНТ, ОРБИТА, СТЫКОВКА, ВЫКЛ: the HUD mode to set (HUD_SURFACE, HUD_ORBIT, HUD_DOCKING, HUD_NONE)
    kCmdLayer,          // слои HUD: the layer to switch on / off (Layer)
    kCmdPalette,        // ЦВЕТ HUD: the palette, 0 .. kPaletteCount - 1
    kCmdHudGain,        // ЯРКОСТЬ HUD − / +: the step, -1 or +1
    kCmdMfdGain,        // ЯРКОСТЬ MFD − / +: -1 or +1
    kCmdMfdContrast,    // КОНТРАСТ MFD − / +: -1 or +1 (the contrast is 1 / gamma: + lowers the gamma)
    kCmdAutoLum,        // ПО СВЕТУ: on / off (no value)
    kCmdMfdRes,         // РАЗРЕШЕНИЕ MFD: the resolution, 1024 or 2048
    kCmdRcs,            // РСУ: the attitude mode (RCS_ROT, RCS_LIN, RCS_NONE)
    kCmdUnits,          // ЕДИНИЦЫ: 0 СИ, 1 СИ + узлы/футы
};

// a − + step (the old НАСТР. page's steps and ranges): now is the value in use - the HUD gain (kCmdHudGain: 0.1 a step,
// 0.4 .. 1.5), the MFD gain (kCmdMfdGain: 0.2, 1 .. 3) or the MFD gamma (kCmdMfdContrast: -0.05 a step of the contrast,
// 0.4 .. 1); step -1 or +1. Another cmd: now. A step sets the brightness by hand (ПО СВЕТУ off): the ship takes the automatic
// values over first.
double Stepped(int cmd, double now, double step);

// the settings as the screen shows them (the values in use)
struct View {
    int hud = HUD_SURFACE;                   // the main view's HUD: HUD_SURFACE ГОРИЗОНТ, HUD_ORBIT, HUD_DOCKING, HUD_NONE ВЫКЛ
    bool layer[kLayerCount] = {true, true, true, true};   // the HUD's layers (Layer)
    int palette = 0;                         // 0 ОРАНЖ + ЦИАН, 1 ЦИАН + ОРАНЖ, 2 ИЗУМРУД
    double hudGain = 1.0;                    // the HUD's brightness
    double mfdGain = 1.6, mfdGamma = 0.7;    // the MFDs' brightness and gamma (the page shows the contrast, 1 / gamma)
    bool autoLum = true;                     // ПО СВЕТУ: the brightness follows the light at the ship (day / dusk / night)
    int mfdRes = 1024;                       // the MFDs' resolution [px]: 1024 or 2048
    int rcs = RCS_ROT;                       // the attitude mode: RCS_ROT, RCS_LIN, RCS_NONE
    int units = 0;                           // 0 СИ, 1 СИ + узлы/футы
    double pitch = 0.0, bank = 0.0;          // the ship's attitude [deg; bank + right wing down] - the preview's readouts
};

class Screen {
public:
    // the page: W x kDesignH design px, k surface px per design px
    void Draw(oapi::Sketchpad* skp, ScreenFont& font, GostFont& gost, double k, double W, const View& v);
    // the command under a point of the page (design px), -1 none; along: the key's value (see Cmd)
    int Hit(double x, double y, double* along = nullptr) const { return front::FindHit(hits_, x, y, along); }

private:
    std::vector<front::Hit> hits_;
};

}  // namespace tantra::paramsscreen
