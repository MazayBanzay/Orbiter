// TantraDisplays - the engine console of the right riser: the mockup screen (TantraEngineScreen) fed with the ship's engines,
// its scales and keys turned into the ship's set-points and actions (the same as the keyboard). Drawn into eng_ at 800 px high.
#include "TantraDisplays.h"
#include "Tantra.h"

#include <algorithm>
#include <cmath>

namespace es = tantra::enginescreen;

void TantraDisplays::FillEngineView(es::View& v) const {
    Tantra* t = t_;
    namespace sp = tantra::spec;
    v.t = oapiGetSimTime();
    v.ana = t->engineSet_ == Tantra::EngineSet::Anamezon;
    v.bypass = t->hotStartOverride_;
    v.bypassArmed = oapiGetSysTime() - bypassArm_ < 3.0;
    v.gLimOn = t->gLimitOn_; v.gLim = t->gLimit_; v.feltG = t->accelG_;
    v.massKg = t->GetMass(); v.g = t->LocalG();
    v.sCG = t->frameS_;
    // ---- planetary ----
    const bool ground = t->GroundContact() || t->GetAltitude(ALTMODE_GROUND) < 100.0;
    v.envKind = ground ? 0 : t->GetAtmDensity() > 1e-5 ? 1 : 2;
    const tantra::plant::Output& o = t->plantOut_;
    v.mass = o.mass; v.massMode = t->plant_.MassMode(); v.plantRun = t->plant_.Running();
    const bool anaMain = t->AnaIsMain();
    v.mSet = !anaMain ? t->GetThrusterGroupLevel(THGROUP_MAIN) : 0.0;
    v.mAct = t->march_ ? t->GetThrusterLevel(t->march_) : 0.0;
    v.Fm = o.thrust; v.FmField = o.fieldThrust; v.vM = o.exhaust; v.mdotM = o.mdot; v.PjetM = o.jetPower;
    double lvl = 0.0, F = 0.0; int n = 0;
    for (THRUSTER_HANDLE h : t->pod_) if (h) { const double L = t->GetThrusterLevel(h); lvl += L; F += L * t->GetThrusterMax0(h); ++n; }
    v.pAct = n ? lvl / n : 0.0;
    v.pSet = t->GetGroupThrusterCount(THGROUP_HOVER) > 0 ? t->GetThrusterGroupLevel(THGROUP_HOVER) : v.pAct;
    v.podsOk = t->podOut_ > 0.99;
    v.podF = F / sp::kPodCount; v.podFCap = t->prm_.podThrustTotal / sp::kPodCount;
    v.vP = t->pod_[0] ? t->GetThrusterIsp0(t->pod_[0]) : 3e4;
    v.nozSet = t->podTarget_; v.nozAct = t->podAngle_;
    v.tvcMax = sp::kTvcMaxDeg;
    if (t->march_) { VECTOR3 d; t->GetThrusterDir(t->march_, d); v.tvc = std::atan2(d.y, d.z) * DEG; }
    VECTOR3 T; t->GetThrustVector(T); v.Fx = T.z; v.Fy = T.y;
    VECTOR3 M; t->GetTorqueVector(M); v.pitchM = M.x * t->GetMass();
    v.argon = t->argon_ ? t->GetPropellantMass(t->argon_) : 0.0; v.iron = t->iron_ ? t->GetPropellantMass(t->iron_) : 0.0;
    v.hoverPods = v.g > 0 && t->prm_.podThrustTotal > 0 ? v.massKg * v.g / t->prm_.podThrustTotal : 0.0;
    // ---- anamezon ----
    v.stage = int(t->ignition_.Stage()); v.trans = t->ignition_.Transitioning();
    v.fieldL = t->ignition_.FieldLevel(); v.beamL = t->ignition_.BeamLevel(); v.feedL = t->ignition_.FeedLevel();
    double fa = 0; n = 0; for (THRUSTER_HANDLE h : t->ana_) if (h) { fa += t->GetThrusterLevel(h); ++n; }
    v.fsAct = n ? fa / n : 0.0;
    double fr = 0; n = 0; for (THRUSTER_HANDLE h : t->retro_) if (h) { fr += t->GetThrusterLevel(h); ++n; }
    v.frAct = n ? fr / n : 0.0;
    v.fsSet = anaMain ? t->GetThrusterGroupLevel(THGROUP_MAIN) : (std::max)(0.0, fsPend_);
    v.frSet = anaMain && t->GetGroupThrusterCount(THGROUP_RETRO) > 0 ? t->GetThrusterGroupLevel(THGROUP_RETRO) : (std::max)(0.0, frPend_);
    v.chamberF = t->prm_.anaThrust; v.retroF = t->prm_.anaThrust * sp::kRetroAreaFrac;
    v.Fs = v.chamberF * v.fsAct * sp::kAnaCount; v.Fr = v.retroF * v.frAct * sp::kRetroCount;
    v.pelletRate = t->drive_.PelletRate(1.0); v.pelletMass = t->drive_.PelletMass();
    v.vEff = t->ana_[0] ? t->GetThrusterIsp0(t->ana_[0]) : 0.0; v.vJet = t->drive_.Spec().exhaust;
    v.mdotA = v.vEff > 0 ? (v.Fs + v.Fr) / v.vEff : 0.0;
    v.Pjet = t->drive_.JetPower(v.fsAct) + t->drive_.JetPower(v.frAct) * sp::kRetroAreaFrac * sp::kRetroCount / sp::kAnaCount;
    v.store = t->drive_.StoreFraction();
    v.compAcc = t->drive_.CompensatedAccel(); v.residG = t->drive_.ResidualAccel() / 9.80665;
    v.beta = t->beta_; v.gamma = 1.0 / std::sqrt((std::max)(1e-12, 1.0 - t->beta_ * t->beta_));
    v.activeTrap = t->activeTrap_;
    for (int i = 0; i < 4 && i < sp::kTrapCount; ++i) {
        v.trapPresent[i] = t->trapPresent_[i];
        v.traps[i] = t->trapPresent_[i] && t->trap_[i] ? t->GetPropellantMass(t->trap_[i]) : 0.0;
    }
    v.capHot = t->HotStartLevelCap(); v.capSafe = t->SafetyLevelCap();
    OBJHANDLE sun = oapiGetGbodyByIndex(0);
    if (sun) {
        VECTOR3 sp_, ss; t->GetGlobalPos(sp_); oapiGetGlobalPos(sun, &ss);
        const VECTOR3 d = ss - sp_;
        v.starAU = length(d) / 1.495978707e11;
        VECTOR3 nose; t->GlobalRot(_V(0, 0, 1), nose);
        v.starCos = dotp(unit(d), unit(nose));
    }
}

void TantraDisplays::EngineSetPoint(int bar, double value) {
    Tantra* t = t_;
    const bool anaMain = t->AnaIsMain();
    switch (bar) {
        case es::kBarMarch:
            if (t->engineSet_ != Tantra::EngineSet::Planetary) { t->Message("Главная тяга на анамезоне: выберите ПЛАНЕТАРНЫЕ", "Main thrust is on the anamezon: select PLANETARY"); return; }
            t->SetThrusterGroupLevel(THGROUP_MAIN, value); break;
        case es::kBarPods: SetPodLevel(value); break;
        case es::kBarNozzle: t->ActPodsTo(value); break;
        case es::kBarGLim: t->gLimit_ = (std::max)(1.0, value); break;
        case es::kBarResid: t->gLimit_ = (std::max)(1.0, value); break;
        case es::kBarFeed:
            if (anaMain) t->SetThrusterGroupLevel(THGROUP_MAIN, value);
            else { fsPend_ = value; if (value > 0) t->Message("Подача запомнена: пойдёт после ПОДАЧИ камер", "Feed stored: it goes once the chambers feed"); }
            break;
        case es::kBarRetro:
            if (anaMain && t->GetGroupThrusterCount(THGROUP_RETRO) > 0) t->SetThrusterGroupLevel(THGROUP_RETRO, value);
            else { frPend_ = value; if (value > 0) t->Message("Реверс запомнен: пойдёт после ПОДАЧИ камер", "Retro stored: it goes once the chambers feed"); }
            break;
        default: break;
    }
}

void TantraDisplays::EngineCommand(int cmd) {
    Tantra* t = t_;
    switch (cmd) {
        case es::kCmdTabAna: t->ActSelectEngine(true); break;
        case es::kCmdTabPlan: t->ActSelectEngine(false); break;
        case es::kCmdMassAuto: t->PlantKey(7); break;
        case es::kCmdMassArgon: t->PlantKey(8); break;
        case es::kCmdMassIron: t->PlantKey(9); break;
        case es::kCmdCut:
            t->SetThrusterGroupLevel(THGROUP_MAIN, 0.0);
            if (t->GetGroupThrusterCount(THGROUP_RETRO) > 0) t->SetThrusterGroupLevel(THGROUP_RETRO, 0.0);
            if (t->GetGroupThrusterCount(THGROUP_HOVER) > 0) t->SetThrusterGroupLevel(THGROUP_HOVER, 0.0);
            fsPend_ = frPend_ = -1.0;
            t->Message("Отсечка: тяга в ноль", "Cut-off: thrust to zero");
            break;
        case es::kCmdBypass:
            if (t->hotStartOverride_) t->ActToggleOverride();
            else if (oapiGetSysTime() - bypassArm_ > 3.0) { bypassArm_ = oapiGetSysTime(); t->Message("Обход блокировок: подтвердите в течение 3 с", "Interlock override: confirm within 3 s"); }
            else { bypassArm_ = -99.0; t->ActToggleOverride(); }
            break;
        case es::kCmdGLimOnOff: t->ActToggleGLimit(); break;
        case es::kCmdIgnition: t->ActIgnitionTo(t->ignition_.Stage() == tantra::IgnStage::Off && t->ignition_.Target() == tantra::IgnStage::Off ? 3 : 0); break;
        case es::kCmdTrap: t->ActNextTrap(); break;
        case es::kCmdPhase1: case es::kCmdPhase2: case es::kCmdPhase3: t->ActIgnitionTo(cmd - es::kCmdPhase1 + 1); break;
        default: break;
    }
}

void TantraDisplays::DrawEngineScreen() {
    if (!eng_) return;
    Tantra* t = t_;
    if (t->AnaIsMain()) {   // set-points stored before the feed go now
        if (fsPend_ >= 0) { t->SetThrusterGroupLevel(THGROUP_MAIN, fsPend_); fsPend_ = -1; }
        if (frPend_ >= 0 && t->GetGroupThrusterCount(THGROUP_RETRO) > 0) { t->SetThrusterGroupLevel(THGROUP_RETRO, frPend_); frPend_ = -1; }
    }
    oapiClearSurface(eng_, 0xFF000000 | 0x0a1311);
    if (oapi::Sketchpad* skp = oapiGetSketchpad(eng_)) {
        es::View v;
        FillEngineView(v);
        int w = 0, h = 0; oapiGetSurfaceSize(eng_, &w, &h);
        engScr_.Draw(skp, font_, 0, 0, int(w), int(h), v);
        oapiReleaseSketchpad(skp);
    }
}

bool TantraDisplays::TouchEngineScreen(double x, double y) {
    double along = 0.0;
    const int cmd = engScr_.Hit(x, y, &along);
    if (cmd < 0) return false;
    int bar = -1; double value = 0.0;
    if (engScr_.BarValue(cmd, along, &bar, &value)) EngineSetPoint(bar, value);
    else EngineCommand(cmd);
    tRiser_ = 0.0;
    return true;
}
