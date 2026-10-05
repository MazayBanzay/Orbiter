// TantraEngineScreen: the front glass's ДВИГАТЕЛИ page - pageEng() of Tantra_Design/refine/front_v3.html (the low wide glass,
// the user's «ШИРЕ», 2026-10-05) drawn with the front glass's kit (TantraFrontScreen: GOST type A, the key module, the blocks).
// The header: АНАМЕЗОН / ПЛАНЕТАРНЫЕ and the drive's state in one line. Up top the pictures (ЧАШИ И ВЕКТОРЫ: the stern and the
// thrust lines; КАМЕРЫ И СТРУИ: the chambers, the retro cups, the jets and the star) and the output turned on its side (ВЫХОД
// ЧАШ / КАМЕР: rows the quantities, columns the sources); below left РАСЧЁТ and ЖУРНАЛ; under the yoke's hub ТРЕНДЫ (history:
// seen with the yoke stowed); right of the hub УПРАВЛЕНИЕ on full height - the touch scales of UCGO Arrow (the fill what the drive
// gives, the yellow arrow the set-point; a tap sets it, ▲ ▼ step it) and the keys in two rows under the hand.
#pragma once
#include "orbitersdk.h"
#include "TantraScreenCanvas.h"
#include "TantraFrontScreen.h"

#include <deque>
#include <vector>

namespace tantra::enginescreen {

enum Bar { kBarMarch, kBarPods, kBarNozzle, kBarTvc, kBarGLim, kBarFeed, kBarRetro, kBarResid, kBarCount };
enum Cmd {
    kCmdTabAna = 0, kCmdTabPlan, kCmdMassAuto, kCmdMassArgon, kCmdMassIron, kCmdCut, kCmdBypass, kCmdGLimOnOff,
    kCmdIgnition, kCmdTrap, kCmdPhase1, kCmdPhase2, kCmdPhase3,
    kCmdBar = 100,       // + bar: a tap on the scale or its ▲ ▼ - Hit's value is the scale's new set-point
};

struct View {
    double t = 0.0;
    bool ana = false;                    // the anamezon page (the main throttle drives the chambers)
    bool bypass = false, bypassArmed = false;
    bool blink = false;                  // the blinking phase (a key waiting for its second press)
    bool gLimOn = true; double gLim = 5.0, feltG = 0.0;
    // ---- planetary ----
    int envKind = 0;                     // 0 ground, 1 air, 2 space
    int mass = 0, massMode = -1;         // reaction mass in use: 0 argon, 1 iron, 2 products; mode -1 auto
    bool plantRun = false;
    bool argonLock = false;              // the plant's limiter holds argon (in the air)
    double mSet = 0.0, mAct = 0.0;       // march lever and what it gives (level)
    double Fm = 0.0, FmField = 0.0, vM = 3e4, mdotM = 0.0, PjetM = 0.0;
    double pSet = 0.0, pAct = 0.0;       // pods
    bool podsOk = false;                 // out of the bays
    int nPods = 4;
    double podF = 0.0, podFCap = 0.0, vP = 3e4;   // per pod (3 cups) now and at full; exhaust
    double nozSet = 0.0, nozAct = 0.0;   // pods' nozzles [deg]: 0 aft, 90 down, 180 forward
    double tvc = 0.0, tvcMax = 10.0;     // the march jet's deflection (automatic: through the CG)
    double Fx = 0.0, Fy = 0.0, pitchM = 0.0;   // thrust along the hull / up (ship frame), pitch moment [N m]
    double massKg = 0.0, g = 0.0;        // ship mass, local gravity
    double argon = 0.0, iron = 0.0;
    double sCG = 58.1;
    double hoverPods = 0.0;              // pods' share needed to hover lying (weight / pods at full)
    // ---- anamezon ----
    int stage = 0, ignTarget = 0; bool trans = false;   // ignition: 0 off, 1 field, 2 beam, 3 feed; where the handle is
    double fieldL = 0.0, beamL = 0.0, feedL = 0.0;
    double fsSet = 0.0, fsAct = 0.0, frSet = 0.0, frAct = 0.0;   // stern feed, retro feed
    double Fs = 0.0, Fr = 0.0, chamberF = 2.17e10, retroF = 1.17e10;
    int nAna = 4, nRetro = 2;            // the stern chambers, the nose retro cups
    double pelletRate = 0.0, pelletMass = 0.0, vEff = 0.0, vJet = 0.0, Pjet = 0.0, mdotA = 0.0;
    double store = 1.0;                  // field store fraction
    double compAcc = 0.0, residG = 0.0, compMax = 200.0 * 9.80665;
    double beta = 0.0, gamma = 1.0;
    double traps[4] = {}, trapMax = 1.0; int activeTrap = 0; bool trapPresent[4] = {true, true, true, true};
    double capHot = 1.0, capSafe = 1.0;  // the interlocks' caps (hot start in the air, radiation near people)
    double starAU = 0.0, starCos = 0.0;  // the Sun: distance, cos of its direction against the nose
};

class Screen {
public:
    // the journal and the trends follow the drive (every redraw of the glass, whatever page it shows)
    void Sample(const View& v);
    // the page: W x kDesignH design px, k surface px per design px
    void Draw(oapi::Sketchpad* skp, ScreenFont& font, GostFont& gost, double k, double W, const View& v);
    // the command under a point (design px), -1 none; value: a scale's new set-point
    int Hit(double x, double y, double* value) const { return front::FindHit(hits_, x, y, value); }
    void Note(double t, const std::wstring& s, int lvl) { log_.Add(t, s, lvl); }

private:
    struct ScaleDef {
        int bar; double x, w; const wchar_t* label; unsigned col; double min, max, step, snap, set, act, zero;
        std::wstring valTxt, sub, lim; bool live;
    };
    void Scale(front::Pad& g, const ScaleDef& o);
    void KeyRow2(front::Pad& g, const View& v, double x, double w);
    void Planetary(front::Pad& g, const View& v, double W);
    void Anamezon(front::Pad& g, const View& v, double W);
    void JournalLines(front::Pad& g, double x, double y, double w, int n, double step);
    void TrendBlock(front::Pad& g, double x, double y, double w, double h, int jn, bool ana);
    void Watch(const View& v);
    std::vector<front::Hit> hits_;
    scr::Journal log_;
    struct TP { double t, a, b, c; };
    std::deque<TP> trend_;
    bool trendAna_ = false;
    bool seen_ = false;
    View last_;
};

}  // namespace tantra::enginescreen
