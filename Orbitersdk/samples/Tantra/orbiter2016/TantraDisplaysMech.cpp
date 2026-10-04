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
    v.driveMoment = (v.P > 1.75 && v.P < 4.0) ? v.weight * c.CgResidual() : 0.0;
    v.bend = (std::max)(t->legR_[0], t->legR_[1]);
    v.wingMode = t->wingMode_; v.wingFold = p.tuck;
    v.podOut = t->podOut_; v.podAngle = t->podAngle_; v.podTarget = t->podTarget_; v.podsWanted = t->podsWanted_;
    v.rovers = t->rovers_; v.hangar = t->hangar_; v.hangarT = t->hangarT_; v.bayDoors = t->bayDoors_;
    v.irisAna = t->irisAna_; v.irisNose = t->irisNose_; v.marchOut = t->marchOut_;
    v.liftStowed = t->lift_.Stowed(); v.liftAtGround = t->lift_.AtGround();
    v.airlock = t->airlockUp_ > 0.5;
    v.portStep = t->portStep_ == Tantra::PortStep::Idle ? 0 : 1;
    v.hotSkin = false;
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
            static const char* const kRu[5] = {"Положение «В ГОРИЗОНТ»: укладка на лопасти", "Положение «НА ТРЁХ»: ось 32 м, на лопастях и кенгуру", "Положение «НА НОГИ»: подъём до верхней точки",
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
        case ms::kCmdPods:
            if (t->podsWanted_) { t->podsWanted_ = false; t->podTarget_ = 0.0; t->Message("Гондолы: в отсеки", "Pods: into the bays"); }
            else t->ActPods(false);
            break;
        case ms::kCmdRovers: t->ActRovers(); break;
        case ms::kCmdPodsAft: t->ActPodsTo(0.0); break;
        case ms::kCmdPodsDown: t->ActPodsTo(90.0); break;
        case ms::kCmdHangar: t->ActHangar(); break;
        case ms::kCmdPort: t->ActPort(); break;
        case ms::kCmdAirlock: t->ActToggleAirlock(); break;
        case ms::kCmdTableUp: t->ActPortLift(); break;
        case ms::kCmdTableStop: t->ActPortStop(); break;
        case ms::kCmdTabMech: leftTab_ = 0; break;
        case ms::kCmdTabThermal: t->Message("Тепловой экран — следующей сборкой", "The thermal screen comes with the next build"); break;
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
