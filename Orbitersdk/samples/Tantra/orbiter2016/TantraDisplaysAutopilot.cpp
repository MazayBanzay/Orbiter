// TantraDisplays - the left glass's third page АВТОПИЛОТ (Tantra_Design/tantra_autopilot_screen.html; TantraAutopilotScreen draws it,
// TantraGuidance reckons): ВЗЛЁТ НА ОРБИТУ (the targets, the plan from where the ship stands, the readiness check) and ПОСАДКА НА
// КОРМУ (the hover height, the field АВТО / РУЧН, the plan from the ship's height and speed). The guidance is stepped every frame
// with the ship's real state, so the plan, the kinematics and the checks are the ship's own. It does not fly the ship yet: applying
// its commands (the thrust, the attitude by the RCS, the march's TVC, the pods, the field) touches the ship's physics and comes with
// the fork; ПУСК says so instead of pretending.
#include "TantraDisplays.h"
#include "Tantra.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gd = tantra::guidance;
namespace ap = tantra::apscreen;

void TantraDisplays::FillAscentState(gd::AscentState& s, double dt) {
    Tantra* t = t_;
    s.simt = oapiGetSimTime(); s.dt = dt;
    if (OBJHANDLE ref = t->GetGravityRef()) {
        s.planet.R = oapiGetSize(ref); s.planet.mu = GGRAV * oapiGetMass(ref);
        const double per = oapiGetPlanetPeriod(ref);
        s.planet.omega = std::fabs(per) > 1.0 ? 2 * PI / per : 0.0;
    }
    double lng = 0, lat = 0, rad = 0; t->GetEquPos(lng, lat, rad);
    s.lat = lat; s.lon = lng; s.alt = t->GetAltitude(ALTMODE_MEANRAD);
    VECTOR3 v; t->GetGroundspeedVector(FRAME_HORIZON, v);                 // x east, y up, z north
    s.vE = v.x; s.vU = v.y; s.vN = v.z;
    s.pitch = t->GetPitch() * DEG;
    VECTOR3 f, u; t->HorizonRot(_V(0, 0, 1), f); t->HorizonRot(_V(0, 1, 0), u);
    const VECTOR3 d = f - u;                                              // where the nose tips: defined at the vertical too
    s.hdg = std::atan2(d.x, d.z) * DEG; if (s.hdg < 0) s.hdg += 360.0;
    s.rho = t->GetAtmDensity(); s.mach = t->GetMachNumber(); s.q = t->GetDynPressure(); s.contact = t->GroundContact();
    s.mass = t->GetMass(); s.argon = t->ArgonMass(); s.iron = t->IronMass();
    s.reactionMass = s.argon <= 0 && s.iron <= 0 ? gd::kNoMass : t->marchHigh_ ? gd::kIron : gd::kArgon;
    s.Fmarch = t->march_ ? t->GetThrusterMax0(t->march_) : 0.0;
    s.FmNow = t->march_ ? s.Fmarch * t->GetThrusterLevel(t->march_) : 0.0;
    s.Fpods = s.FpNow = 0.0;
    for (THRUSTER_HANDLE h : t->pod_) if (h) { const double m = t->GetThrusterMax0(h); s.Fpods += m; s.FpNow += m * t->GetThrusterLevel(h); }
    s.mdot = t->plantOut_.mdot;
    s.plantRun = t->plant_.Running(); s.field = t->plant_.Field(); s.power = t->plant_.PowerPct() / 100.0;
    const char* lim = t->plantOut_.limit ? t->plantOut_.limit : "";
    s.plantLim = !std::strcmp(lim, "мощность") ? 1 : !std::strcmp(lim, "тепло") ? 2 : 0;
}

void TantraDisplays::FillLandState(gd::LandState& s, double dt) {
    Tantra* t = t_;
    s.simt = oapiGetSimTime(); s.dt = dt;
    double lng = 0, lat = 0, rad = 0; t->GetEquPos(lng, lat, rad);
    if (OBJHANDLE ref = t->GetGravityRef()) { const double r = (std::max)(1.0, rad); s.g = GGRAV * oapiGetMass(ref) / (r * r); }
    s.rho = t->GetAtmDensity();
    VECTOR3 f; t->HorizonRot(_V(0, 0, 1), f);                             // the hull's axis in the horizon frame (x east, y up, z north)
    s.z = t->GetAltitude(ALTMODE_GROUND) - t->frameS_ * f.y;               // the stern over the ground (the CG is frameS_ up the axis)
    VECTOR3 v; t->GetGroundspeedVector(FRAME_HORIZON, v);
    s.vz = v.y; s.vE = v.x; s.vN = v.z;
    if (havePad_) { s.xE = (lng - padLon_) * rad * std::cos(lat); s.xN = (lat - padLat_) * rad; } else s.xE = s.xN = 0.0;
    s.thE = std::atan2(f.x, f.y) * DEG; s.thN = std::atan2(f.z, f.y) * DEG;
    if (dt > 1e-4 && apThInit_) { s.omE = (s.thE - apThE_) * RAD / dt; s.omN = (s.thN - apThN_) * RAD / dt; }
    apThE_ = s.thE; apThN_ = s.thN; apThInit_ = true;
    VECTOR3 a; t->GetHorizonAirspeedVector(a);                            // the wind: the ground speed less the airspeed
    s.windE = v.x - a.x; s.windN = v.z - a.z;
    s.mass = t->GetMass(); s.argon = t->ArgonMass();
    s.Fm = t->march_ ? t->GetThrusterMax0(t->march_) * t->GetThrusterLevel(t->march_) : 0.0;
    s.Fp = 0.0; for (THRUSTER_HANDLE h : t->pod_) if (h) s.Fp += t->GetThrusterMax0(h) * t->GetThrusterLevel(h);
    s.B = t->plant_.Field(); s.legs = t->carriage_.Gear(); s.contact = t->GroundContact(); s.sternT = t->plant_.SternT();
}

// every frame: both guidances follow the ship (the plan, the kinematics, the checks); their commands are not applied yet
void TantraDisplays::ApStep(double dt) {
    gd::AscentState as; FillAscentState(as, dt);
    gd::LandState ls; FillLandState(ls, dt);
    const double now = oapiGetSysTime();
    asc_.Step(as, now);
    land_.Step(ls, now);
}

void TantraDisplays::DrawAutopilot(oapi::Sketchpad* skp, int ox, int oy, int w, int h) {
    Tantra* t = t_;
    ap::View v;
    v.page = apPage_; v.sysT = oapiGetSysTime(); v.warp = oapiGetTimeAcceleration();
    v.asc = &asc_; v.land = &land_;
    v.qGround = t->plantOut_.sources[2]; v.qRad = t->plantOut_.sources[0]; v.qRegen = t->plantOut_.regen; v.qCrests = t->plantOut_.radiated;
    if (OBJHANDLE ref = t->GetSurfaceRef()) {                             // the place: the nearest base within 20 km
        double lng = 0, lat = 0, rad = 0; t->GetEquPos(lng, lat, rad);
        OBJHANDLE best = nullptr; double bd = 2.0e4;
        for (DWORD i = 0; i < oapiGetBaseCount(ref); i++) {
            OBJHANDLE bs = oapiGetBaseByIndex(ref, i); double bl, bb; oapiGetBaseEquPos(bs, &bl, &bb);
            const double dd = oapiGetSize(ref) * std::acos((std::max)(-1.0, (std::min)(1.0, std::sin(lat) * std::sin(bb) + std::cos(lat) * std::cos(bb) * std::cos(bl - lng))));
            if (dd < bd) { bd = dd; best = bs; }
        }
        if (best) { char nm[64]; oapiGetObjectName(best, nm, sizeof nm); v.site = tantra::scr::W1251(nm); }
    }
    apScr_.Draw(skp, font_, ox, oy, w, h, v);
}

bool TantraDisplays::TouchAutopilot(double x, double y) {
    const int c = apScr_.Hit(x, y);
    if (c < 0) return false;
    if (c == ap::kCmdStart || c == ap::kCmdLStart) {                      // not yet: the plan is the ship's, the flying is not
        t_->Message("Автопилот: план и проверка — по кораблю; управление кораблём подключается следующей сборкой",
                    "Autopilot: the plan and the check follow the ship; flying it comes with a later build");
        return true;
    }
    if (c == ap::kCmdLArm) {                                              // the landing point: where the ship is when armed
        double lng = 0, lat = 0, rad = 0; t_->GetEquPos(lng, lat, rad);
        padLon_ = lng; padLat_ = lat; havePad_ = true;
    }
    ap::Press(c, asc_, land_, oapiGetSysTime(), &apPage_);
    return true;
}
