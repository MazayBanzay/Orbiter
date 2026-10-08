// TantraAutopilotScreen: the bridge's АВТОПИЛОТ screen - the mockup Tantra_Design/tantra_autopilot_screen.html (the user,
// 2026-10-04) carried over in its own pixels (1600 x 800) and colours; two pages by the keys of the title bar:
//   ВЗЛЁТ НА ОРБИТУ - the trajectory (the plan dashed, the flight solid, on a root scale of the downrange; the plan's events;
//     the readiness check while on the stand), the course (the target's and the launch azimuth, the heading, the velocity's
//     azimuth; the inclination and its error, the speed across the plane, the distance off the plan's ground track), the target
//     (set by the arrows until ВЗВЕСТИ), the values now, what the autopilot commands the engines (the pitch dial, the march's
//     and the pods' thrust, the TVC, the heading, the working mass, the limit, the attitude), the flight program, the keys
//     ВЗВЕСТИ / ПУСК (ПОДТВЕРДИТЬ) / УДЕРЖАНИЕ / РУЧНОЕ / ОТМЕНА (ПОДТВЕРДИТЬ) / СБРОС, the journal, the trends;
//   ПОСАДКА НА КОРМУ - the profile (the stern's height on a root scale, the descent rate), the side view over the point (the
//     wind, the drift, the tilt x5, the TVC), the conditions (the field АВТО / РУЧН, the hover height, the start, the norms, the
//     plan), the values now, the thrust / field / legs commands, the landing program, its keys, the journal, the stern's heat,
//     the trends of thrust, field and load;
//   ПОСАДКА ЛЁЖА - the belly autopilot (TantraBellyLand): the targets (height, ground speed, heading) by the arrows in flight,
//     the values now, the thrust split (the pods' lift against the weight, the cups' angle, the march), the gear, the phases,
//     ВКЛ / ПОСАДКА / ОТМЕНА, the readiness check, the journal. Sizes after Tantra_Design/refine/SPEC.md (text >= 15 px, keys 48);
//   СХОД - the deorbit and the entry (TantraReentry, refine/REENTRY.md): the choice (the regime, the wings, the g limit, the site),
//     the plan's profile (altitude over downrange) with the ship on it, the plan, the values now, the commands (the thrust and
//     what limits it, the attitude, the wings, the forecast's verdict), the phases, ВЗВЕСТИ / ПУСК / УДЕРЖАНИЕ / РУЧНОЕ / ОТМЕНА /
//     СБРОС, the readiness check, the journal.
// Everything shown comes from the guidance (TantraGuidance: the targets, the plans, the flights, the journals) and the plant's
// heat flows; the keys go back to the guidance (Press). In the game the launch site is where the ship stands and the wind is
// measured: those two rows only show. The mockup's "модель ×N" (its own time speed) is the time acceleration.
#pragma once
#include "orbitersdk.h"
#include "TantraScreenCanvas.h"
#include "TantraGuidance.h"
#include "TantraBellyLand.h"
#include "TantraReentry.h"

#include <string>
#include <vector>

namespace tantra::apscreen {

enum Cmd {
    kCmdPageAsc = 0, kCmdPageLand,                       // the title bar: ВЗЛЁТ НА ОРБИТУ | ПОСАДКА НА КОРМУ
    // ЦЕЛЬ: the arrows (single - the fine step, double - the coarse one), only until ВЗВЕСТИ
    kCmdIncDn, kCmdIncUp, kCmdIncDn5, kCmdIncUp5,        // НАКЛОНЕНИЕ ОРБИТЫ: 0,1° / 5°
    kCmdAzDn, kCmdAzUp, kCmdAzDn5, kCmdAzUp5,            // АЗИМУТ · ИНЕРЦ. / ПУСКА: 0,5° / 5° (sets the inclination and the branch)
    kCmdPeriDn, kCmdPeriUp, kCmdPeriDn50, kCmdPeriUp50,  // ПЕРИЦЕНТР: 10 / 50 км
    kCmdApoDn, kCmdApoUp, kCmdApoDn50, kCmdApoUp50,      // АПОЦЕНТР: 10 / 50 км
    kCmdGLimDn, kCmdGLimUp,                              // ПРЕДЕЛ ПЕРЕГРУЗКИ: 0,5 g
    kCmdArm, kCmdStart, kCmdHold, kCmdManual, kCmdAbort, kCmdReset,   // the ascent's keys
    // ПОСАДКА НА КОРМУ
    kCmdFieldAuto, kCmdFieldManual,                      // ПОЛЕ МАРШЕВОЙ ЧАШИ: АВТО | РУЧН
    kCmdHoverDn, kCmdHoverUp,                            // ВЫСОТА ВИСЕНИЯ: 5 м
    kCmdLArm, kCmdLStart, kCmdLGoAround, kCmdLAbort, kCmdLManual, kCmdLReset,
    // ПОСАДКА ЛЁЖА: the third tab; the arrows (the fine step, the coarse one), the keys
    kCmdPageBelly,
    kCmdBAltDn, kCmdBAltUp, kCmdBAltDn100, kCmdBAltUp100,   // ВЫСОТА: 10 / 100 м
    kCmdBSpdDn, kCmdBSpdUp, kCmdBSpdDn20, kCmdBSpdUp20,     // ГОР. СКОРОСТЬ: 5 / 20 м/с
    kCmdBHdgDn, kCmdBHdgUp, kCmdBHdgDn30, kCmdBHdgUp30,     // КУРС: 5 / 30°
    kCmdBOn, kCmdBLand, kCmdBOff,                           // ВКЛ, ПОСАДКА, ОТМЕНА · РУЧНОЕ
    // СХОД: the fourth tab; the choice's arrows, the keys
    kCmdPageRent,
    kCmdRRegDn, kCmdRRegUp, kCmdRWingDn, kCmdRWingUp, kCmdRGDn, kCmdRGUp, kCmdRSiteDn, kCmdRSiteUp,
    kCmdRArm, kCmdRStart, kCmdRHold, kCmdRManual, kCmdRAbort, kCmdRReset,
};

struct View {
    int page = 0;                                        // 0 ВЗЛЁТ НА ОРБИТУ, 1 ПОСАДКА НА КОРМУ, 2 ПОСАДКА ЛЁЖА
    double sysT = 0.0;                                   // system time: the lamps blink, the check runs its lines, the 3 s confirmations
    double warp = 1.0;                                   // the time acceleration (the mockup's "модель ×N")
    std::wstring site;                                   // the place under the ship (the nearest base; empty: the latitude alone)
    const guidance::Ascent* asc = nullptr;
    const guidance::Landing* land = nullptr;
    const guidance::BellyLand* belly = nullptr;
    const reentry::Reentry* rent = nullptr;              // СХОД (page 3)
    // КОРМА · ТЕПЛО (plant::Output): W into the stern - the jet off the ground (sources[2]), the reaction's radiation (sources[0]);
    // W out - the reaction mass through the jacket (regen), the crests (radiated)
    double qGround = 0.0, qRad = 0.0, qRegen = 0.0, qCrests = 0.0;
};

class Screen {
public:
    // the mockup's 1600 x 800 at (ox, oy) of the surface
    void Draw(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v);
    int Hit(double x, double y, double* along = nullptr) const { return scr::HitTest(hits_, x, y, along, 1.5); }

private:
    void Header(scr::Canvas& g, const View& v, const std::wstring& txt, unsigned lc, bool blink, const std::wstring& right, double W, double H);
    void PageAscent(scr::Canvas& g, const View& v, double W, double H);
    void PageLanding(scr::Canvas& g, const View& v, double W, double H);
    void Trajectory(scr::Canvas& g, const View& v);
    void Course(scr::Canvas& g, const View& v);
    void Target(scr::Canvas& g, const View& v);
    void Now(scr::Canvas& g, const View& v);
    void Commands(scr::Canvas& g, const View& v, unsigned lc);
    void JournalBox(scr::Canvas& g, const guidance::Journal& j, double w, double txtX, int txtSize);
    void Trends(scr::Canvas& g, const View& v);
    void LProfile(scr::Canvas& g, const View& v);
    void LSide(scr::Canvas& g, const View& v);
    void LTarget(scr::Canvas& g, const View& v);
    void LNow(scr::Canvas& g, const View& v);
    void LCommands(scr::Canvas& g, const View& v, unsigned lc);
    void LHeat(scr::Canvas& g, const View& v);
    void LTrends(scr::Canvas& g, const View& v);
    void PageBelly(scr::Canvas& g, const View& v, double W, double H);
    void PageReentry(scr::Canvas& g, const View& v, double W, double H);
    std::vector<scr::Hit> hits_;
};

// a key of the screen -> the autopilot (the mockup's onArm, onStart, ... and the target's arrows); page: the page shown.
// One autopilot flies at a time: the one engaged by the key lets the others go.
void Press(int cmd, guidance::Ascent& asc, guidance::Landing& land, guidance::BellyLand& belly, double sysNow, int* page);
// the same with СХОД (its keys; it too lets the others go and is let go by them)
void Press(int cmd, guidance::Ascent& asc, guidance::Landing& land, guidance::BellyLand& belly, reentry::Reentry& rent, double sysNow, int* page);

}  // namespace tantra::apscreen
