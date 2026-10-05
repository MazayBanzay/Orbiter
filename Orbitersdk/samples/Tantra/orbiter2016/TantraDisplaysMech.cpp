// TantraDisplays - the left riser's screen (МЕХАНИЗАЦИЯ | ТЕПЛО) and the left wing's mechanisation keys: the mockup screens
// (TantraMechScreen) fed with the ship's state, their keys turned into the ship's actions (the same as the keyboard and panels).
#include "TantraDisplays.h"
#include "Tantra.h"

#if __has_include("gcCoreAPI.h")
#include "gcCoreAPI.h"
#endif

#include <algorithm>
#include <cmath>

namespace ms = tantra::mechscreen;

void TantraDisplays::FillMechView(ms::View& v) const {
    Tantra* t = t_;
    const tantra::Carriage& c = t->carriage_;
    const tantra::CarriagePose& p = c.Pose();
    v.t = oapiGetSimTime();
    v.P = c.Progress(); v.PT = c.Target(); v.tripodP = c.LiftProgressFor(tantra::Carriage::kTripodAxisH);
    v.estop = mechEstop_; v.balHold = t->balHold_;
    v.held = mechEstop_ || t->balHold_;
    v.gear = c.Gear(); v.gearDown = c.GearDown();
    v.setStand = c.Set() == tantra::Carriage::FlightSet::Standing;
    v.port = c.Port(); v.grounded = t->GroundContact();
    v.theta = p.theta; v.trunnionH = p.trunnionH; v.mastLen = p.mastLen; v.cgRes = c.CgResidual(); v.sway = t->colX_;
    v.weight = t->GetMass() * t->LocalG();
    for (int i = 0; i < 7; ++i) { v.legN[i] = t->legN_[i]; v.legR[i] = t->legR_[i]; }
    v.driveMoment = c.Statics(v.weight, c.CgResidual()).driveMoment;
    v.bend = (std::max)(t->legR_[0], t->legR_[1]);
    v.wingMode = t->wingMode_; v.wingFold = p.tuck;
    v.podOut = t->podOut_; v.podAngle = t->podAngle_; v.podTarget = t->podTarget_; v.podsWanted = t->podsWanted_;
    v.rovers = t->rovers_; v.hangar = t->hangar_; v.hangarT = t->hangarT_; v.bayDoors = t->bayDoors_;
    v.irisAna = t->irisAna_; v.irisNose = t->irisNose_; v.marchOut = t->marchOut_;
    v.liftStowed = t->lift_.Stowed(); v.liftAtGround = t->lift_.AtGround();
    v.airlock = t->crew_.AirlockOpen();                                        // what ActToggleAirlock switches
    v.portStep = t->portStep_ == Tantra::PortStep::Idle ? 0 : 1;
    v.sink = 0.0; v.petals = 0; v.petalsAll = tantra::foot::kLegs * tantra::foot::kCells;
    for (int l = 0; l < tantra::foot::kLegs; ++l) {
        if (t->legN_[l] > 0.0) v.sink = (std::max)(v.sink, t->legPen_[l]);
        v.petals += t->feet_.CellsAlive(l);
    }
    v.liftLowering = t->lift_.Lowering();
    // Tantra::UpdatePods: the bays open only below Mach kPodMaxMach and with the wings out (the ship's and the carriage's fold)
    v.podBlock = t->GetAtmDensity() > 1e-6 && t->GetMachNumber() > tantra::spec::kPodMaxMach ? 2 : (t->tuck_ >= 0.5 || p.tuck >= 0.5) ? 1 : 0;
    // the gear: each support out / going, on the ground, whole (the carriage's pose, the sensors, the damage model, the feet)
    namespace dm = tantra::damage;
    static const int kLegPart[7] = {dm::kLegPort, dm::kLegStbd, dm::kSternLeg0, dm::kSternLeg1, dm::kSternLeg2, dm::kSternLeg3, dm::kKangLeg};
    const double gT = c.GearDown() ? 1.0 : 0.0;
    const bool gearRuns = v.gear != gT && !(v.P != v.PT && gT < v.gear);   // Carriage::Update: stowing waits for the carriage
    v.hipS = p.hipS; v.sCG = t->frameS_;
    for (int l = 0; l < 7; ++l) {
        v.legOut[l] = ms::LegOut(l, v.P, v.gear, v.port);
        double next = v.legOut[l];
        if (!v.held && v.P != v.PT) next = ms::LegOut(l, v.P + (v.PT > v.P ? 0.002 : -0.002), v.gear, v.port);
        else if (gearRuns) next = ms::LegOut(l, v.P, v.gear + (gT > v.gear ? 0.002 : -0.002), v.port);
        v.legDir[l] = next > v.legOut[l] + 1e-9 ? 1 : next < v.legOut[l] - 1e-9 ? -1 : 0;
        // on the ground: its pads in it, or loaded; the stern feet are planted at the end of phase 3-4
        v.legTouch[l] = t->penLeg_[l] > 0.0 || t->legN_[l] > 0.0 || (l >= 2 && l <= 5 && v.P >= 4.0 && v.legOut[l] >= 0.998);
        v.legInteg[l] = t->damage_.Integrity(kLegPart[l]);
        v.legBroken[l] = t->feet_.LegBroken(l);
    }
    v.jam = (std::max)(t->solesCar_.Jam(), t->solesStern_.Jam());
    v.anchors = (std::max)(t->solesCar_.Anchors(), t->solesStern_.Anchors());
    v.hipCatcher = t->hipCatcher_;
    v.driveRate = t->hipCatcher_ ? 0.6 : 1.0;                                   // Tantra::UpdateGear: slower on the catchers
    v.turnTime = (std::min)(150.0, 45.0 * std::sqrt((std::max)(1.0, tantra::G0 / (std::max)(0.1, t->LocalG()))));   // as Tantra::UpdateGear sets it
    // the wings and the fin: the angles TantraGear::Apply gives the mesh (the crew's mode and the ground fold)
    namespace m = tantra::mesh;
    static const double kOuterDeg = [] {
        for (const m::RigComp& r : m::kRig) if (r.anim == m::ANIM_WING_OUTER && r.kind == m::RIG_ROTATE) return std::fabs(r.angle) * 180.0 / PI;
        return 177.3;
    }();
    const double crestIn = (std::max)(t->wingIn_, (std::min)(1.0, 2.0 * p.tuck));          // ANIM_CREST_LATERAL
    const double crestOut = (std::max)(t->wingOut_, (std::max)(0.0, 2.0 * p.tuck - 1.0));  // ANIM_WING_OUTER
    const double finIn = (std::max)(p.tuck, t->tuck_);                                     // ANIM_CREST_DORSAL
    v.wingInMax = m::kWingFoldDeg; v.wingRaise = m::kWingRaiseDeg; v.wingOutMax = kOuterDeg; v.finMax = m::kFinRetract;
    v.wingInDeg = crestIn * v.wingInMax; v.wingOutDeg = crestOut * v.wingOutMax; v.finOut = (1.0 - finIn) * v.finMax;
    v.wingGround = p.tuck > 0.001 && t->wingMode_ != 2;
    v.crestInteg[0] = t->damage_.Integrity(dm::kCrestPort); v.crestInteg[1] = t->damage_.Integrity(dm::kCrestStbd);
    v.finInteg = t->damage_.Integrity(dm::kFin);
    // the radiators: the crests and the fin (the energy core's snapshot of the plant)
    const tantra::tcore::Snapshot& cs = t->CoreState();
    v.radiated = cs.radiated; v.radHealth = cs.radHealth; v.radOut = cs.radOut; v.sternT = cs.sternT;
    v.tSafe = t->plant_.Cfg().tSafe; v.tBoil = t->plant_.Cfg().tBoil;
    v.hotSkin = cs.sternT >= 0.75 * v.tBoil;                                              // mech_v3: the hot stern
    for (int z = 0; z < tantra::damage::kZoneCount; ++z) if (t->damage_.Temperature(z) >= 0.75 * t->damage_.Limit(z)) v.hotSkin = true;
}

void TantraDisplays::MechCommand(int cmd) {
    Tantra* t = t_;
    tantra::Carriage& c = t->carriage_;
    switch (cmd) {
        case ms::kCmdPos0: case ms::kCmdPos1: case ms::kCmdPos2: case ms::kCmdPos3: case ms::kCmdPos4: {
            const double P = ms::PositionP(cmd - ms::kCmdPos0, c.LiftProgressFor(tantra::Carriage::kTripodAxisH));
            if (mechEstop_) { t->Message("Аварийный стоп включён: снимите его на пульте механизации", "Emergency stop is on: release it on the mechanisation keys"); return; }
            if (!t->lift_.Stowed()) { t->Message("Сначала поднимите лифт шлюза (Shift+A)", "Raise the airlock lift first (Shift+A)"); return; }
            if (t->hangar_ > 0.01 && P > 0.0) { t->Message("Ангар открыт: положения заперты", "The hangar is open: the positions are locked"); return; }
            const bool up = P > c.Progress();
            if (!c.CommandTo(P, t->GroundContact())) { t->Message("Лафет: только на грунте, шасси выпущено", "Carriage: on the ground with the gear down only"); return; }
            if (up && t->wingMode_ == 1) { t->wingMode_ = 0; t->crestsFolded_ = false; }   // raised wings would meet the stern legs
            static const char* const kRu[5] = {"Положение «В ГОРИЗОНТ»: укладка на лопасти", "Положение «НА ТРЁХ»: ось 32 м, на лопастях и передней опоре", "Положение «НА НОГИ»: подъём до верхней точки",
                                               "Положение «75°»: подъём и поворот, стела", "Положение «ВЗЛЁТНОЕ»: на четыре кормовые ноги"};
            static const char* const kEn[5] = {"Position LEVEL: laying down on the blades", "Position ON THREE: the axis at 32 m, on the blades and the kangaroo", "Position ON THE LEGS: lifted to the top",
                                               "Position 75 DEG: lifted and turned, the stele", "Position LAUNCH: on the four stern legs"};
            t->Message(kRu[cmd - ms::kCmdPos0], kEn[cmd - ms::kCmdPos0]);
            break;
        }
        case ms::kCmdStop: c.Hold(); break;
        case ms::kCmdEstop:
            mechEstop_ = !mechEstop_;
            if (mechEstop_) { c.Hold(); t->ActPortStop(); }
            t->Message(mechEstop_ ? "Механизация: АВАРИЙНЫЙ СТОП, приводы заперты" : "Механизация: аварийный стоп снят",
                       mechEstop_ ? "Mechanisation: EMERGENCY STOP, drives locked" : "Mechanisation: emergency stop released");
            break;
        case ms::kCmdGear: t->ActGear(); break;
        case ms::kCmdSetLevel: if (c.Set() != tantra::Carriage::FlightSet::Level) t->ActGearSet(); break;
        case ms::kCmdSetStand: if (c.Set() != tantra::Carriage::FlightSet::Standing) t->ActGearSet(); break;
        case ms::kCmdWings: t->ActCrests(); break;
        case ms::kCmdWing90: case ms::kCmdWing30: case ms::kCmdWingFold: {   // the ship cycles its modes (ActCrests): step it to the one chosen
            const int want = cmd - ms::kCmdWing90;
            if (want == 1 && c.Set() == tantra::Carriage::FlightSet::Standing && c.Gear() > 0.0) {   // ActCrests' own rule
                t->Message("Крылья 30°: нельзя при выпущенных кормовых ногах", "Wings 30: not with the stern legs out");
                return;
            }
            for (int i = 0; i < 3 && t->wingMode_ != want; ++i) t->ActCrests();
            break;
        }
        case ms::kCmdPods:   // as the left console's pods key: stowing only with their thrust at 0
            if (t->podsWanted_) {
                if (PodLevel() > 0.001) { t->Message("Уборка гондол: сначала их тяга 0", "Stowing the pods: their thrust to 0 first"); return; }
                t->podsWanted_ = false; t->podTarget_ = 0.0; t->Message("Гондолы: чаши в 0°, в отсеки", "Pods: cups aft, into the bays");
            } else t->ActPods(false);
            break;
        case ms::kCmdLift: t->ActLift(); break;
        case ms::kCmdRovers: t->ActRovers(); break;
        case ms::kCmdPodsAft: t->ActPodsTo(0.0); break;
        case ms::kCmdPodsDown: t->ActPodsTo(90.0); break;
        case ms::kCmdHangar: t->ActHangar(); break;
        case ms::kCmdPort: t->ActPort(); break;
        case ms::kCmdAirlock: t->ActToggleAirlock(); break;
        case ms::kCmdTableUp: t->ActPortLift(); break;
        case ms::kCmdTableStop: t->ActPortStop(); break;
        case ms::kCmdTabMech: leftTab_ = 0; break;
        case ms::kCmdTabThermal: leftTab_ = 1; break;
        default: break;
    }
}

void TantraDisplays::DrawLeftScreen(SURFHANDLE s, int x0, int y0, int x1, int y1, double sc) {
    if (!s) return;
    if (oapi::Sketchpad* skp = oapiGetSketchpad(s)) {
        ms::View v;
        FillMechView(v);
        mechScr_.DrawTop(skp, font_, int(x0 * sc + .5), int(y0 * sc + .5), int((x1 - x0) * sc + .5), int((y1 - y0) * sc + .5), v);
        oapiReleaseSketchpad(skp);
    }
#if __has_include("gcCoreAPI.h")
    if (gcCore2* gc = gcGetCoreInterface()) gc->GenerateMipmaps(s);
#endif
}

bool TantraDisplays::TouchLeftScreen(double x, double y) {
    const int cmd = mechScr_.HitTop(x, y);
    if (cmd < 0) return false;
    MechCommand(cmd);
    return true;
}

void TantraDisplays::DrawShelfL(SURFHANDLE s, double sc, int w, int h) {
    oapiClearSurface(s, 0xFF000000 | 0x1C1816);
    if (oapi::Sketchpad* skp = oapiGetSketchpad(s)) {
        ms::View v;
        FillMechView(v);
        mechScr_.DrawPult(skp, font_, int(6 * sc + .5), int(6 * sc + .5), int((w - 12) * sc + .5), int((h - 12) * sc + .5), v);
        oapiReleaseSketchpad(skp);
    }
#if __has_include("gcCoreAPI.h")
    if (gcCore2* gc = gcGetCoreInterface()) gc->GenerateMipmaps(s);
#endif
}

bool TantraDisplays::TouchShelfL(double x, double y) {
    const int cmd = mechScr_.HitPult(x, y);
    if (cmd < 0) return false;
    MechCommand(cmd);
    return true;
}
