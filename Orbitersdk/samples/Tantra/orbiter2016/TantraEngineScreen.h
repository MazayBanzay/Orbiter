// TantraEngineScreen: the engine console of the right riser - the mockup Tantra_Design/tantra_engines_screen.html v2 (the user,
// 2026-10-04) in its own pixels and colours. A page at full height: АНАМЕЗОН (the four stern chambers and the two nose retro
// cups, their output, the jets and the star, the reckoning) or ПЛАНЕТАРНЫЕ (the marching cup and the pods, the thrust vectors,
// the output, the reckoning). On the right the touch scales of UCGO Arrow: a tap or a drag sets the yellow arrow (the set-point),
// the fill is what the engine gives; ▲ ▼ step it. The console is wider than the mockup (the riser: ~2.4 : 1): the middle column
// (the tables) takes the extra width, the blocks keep their sizes.
#pragma once
#include "orbitersdk.h"
#include "TantraScreenCanvas.h"

#include <deque>
#include <vector>

namespace tantra::enginescreen {

enum Bar { kBarMarch, kBarPods, kBarNozzle, kBarTvc, kBarGLim, kBarFeed, kBarRetro, kBarResid, kBarCount };
enum Cmd {
    kCmdTabAna = 0, kCmdTabPlan, kCmdMassAuto, kCmdMassArgon, kCmdMassIron, kCmdCut, kCmdBypass, kCmdGLimOnOff,
    kCmdIgnition, kCmdTrap, kCmdPhase1, kCmdPhase2, kCmdPhase3,
    kCmdBar = 100,       // + bar: a tap / drag on the scale (along = 0 bottom .. 1 top)
    kCmdUp = 200,        // + bar: ▲
    kCmdDown = 300,      // + bar: ▼
};

struct View {
    double t = 0.0;
    bool ana = false;                    // the anamezon page (the main throttle drives the chambers)
    bool bypass = false, bypassArmed = false;
    bool gLimOn = true; double gLim = 5.0, feltG = 0.0;
    // ---- planetary ----
    int envKind = 0;                     // 0 ground, 1 air, 2 space
    int mass = 0, massMode = -1;         // reaction mass in use: 0 argon, 1 iron, 2 products; mode -1 auto
    bool plantRun = false;
    double mSet = 0.0, mAct = 0.0;       // march lever and what it gives (level)
    double Fm = 0.0, FmField = 0.0, vM = 3e4, mdotM = 0.0, PjetM = 0.0;
    double pSet = 0.0, pAct = 0.0;       // pods
    bool podsOk = false;                 // out of the bays
    double podF = 0.0, podFCap = 0.0, vP = 3e4;   // per pod (3 cups) now and at full; exhaust
    double nozSet = 0.0, nozAct = 0.0;   // pods' nozzles [deg]: 0 aft, 90 down, 180 forward
    double tvc = 0.0, tvcMax = 10.0;     // the march jet's deflection (automatic: through the CG)
    double Fx = 0.0, Fy = 0.0, pitchM = 0.0;   // thrust along the hull / up (ship frame), pitch moment [N m]
    double massKg = 0.0, g = 0.0;        // ship mass, local gravity
    double argon = 0.0, iron = 0.0;
    double sCG = 58.1;
    double hoverPods = 0.0;              // pods' share needed to hover lying (weight / pods at full)
    // ---- anamezon ----
    int stage = 0; bool trans = false;   // ignition: 0 off, 1 field, 2 beam, 3 feed
    double fieldL = 0.0, beamL = 0.0, feedL = 0.0;
    double fsSet = 0.0, fsAct = 0.0, frSet = 0.0, frAct = 0.0;   // stern feed, retro feed
    double Fs = 0.0, Fr = 0.0, chamberF = 2.17e10, retroF = 1.17e10;
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
    void Draw(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v);
    int Hit(double x, double y, double* along) const { return scr::HitTest(hits_, x, y, along, 4.0); }
    // a hit on a scale (or its ▲ ▼) -> the new set-point of that scale; false for the other keys
    bool BarValue(int cmd, double along, int* bar, double* value) const;
    void Note(double t, const std::wstring& s, int lvl) { log_.Add(t, s, lvl); }

private:
    struct BarDef { double min = 0, max = 1, step = 0.05, snap = 0.01, set = 0; bool live = false; };
    void TBar(scr::Canvas& g, int bar, double x, double w, const wchar_t* label, unsigned col, double min, double max, double step, double snap,
              double set, double act, double zero, const std::wstring& actTxt, const std::wstring& sub, const std::wstring& lim, bool live,
              const std::vector<std::pair<double, std::wstring>>& ticks);
    void Planetary(scr::Canvas& g, const View& v, double W, double H);
    void Anamezon(scr::Canvas& g, const View& v, double W, double H);
    void Trends(scr::Canvas& g, double x, double y, double w, double h, bool ana);
    void JournalBox(scr::Canvas& g, double x, double y, double w, double h);
    void Watch(const View& v);
    std::vector<scr::Hit> hits_;
    BarDef bars_[kBarCount];
    scr::Journal log_;
    struct TP { double t, a, b, c; };
    std::deque<TP> trend_;
    bool trendAna_ = false;
    bool seen_ = false;
    View last_;
};

}  // namespace tantra::enginescreen
