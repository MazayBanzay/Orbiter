// TantraDisplays - the left glass's third page АВТОПИЛОТ (Tantra_Design/tantra_autopilot_screen.html; TantraAutopilotScreen draws it,
// TantraGuidance reckons): ВЗЛЁТ НА ОРБИТУ (the targets, the plan from where the ship stands, the readiness check) and ПОСАДКА НА
// КОРМУ (the hover height, the field АВТО / РУЧН, the plan from the ship's height and speed). The guidance is stepped every frame
// with the ship's real state, so the plan, the kinematics and the checks are the ship's own. It does not fly the ship yet: applying
// its commands (the thrust, the attitude by the RCS, the march's TVC, the pods, the field) touches the ship's physics and comes with
// the fork; ПУСК says so instead of pretending. ПОСАДКА ЛЁЖА (TantraBellyLand): the belly autopilot, its state filled here.
#include "TantraDisplays.h"
#include "Tantra.h"
#include "MeshLayout.h"

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

// The belly autopilot's state: the CG over the ground, the ground speed along the heading and to the right, the attitude, the
// march, the pods (their table of the thrust direction by cup angle - UpdatePods' geometry, the pairs' shares), the lying gear.
void TantraDisplays::FillBellyState(gd::BellyState& s, double dt) {
    namespace m = tantra::mesh;
    namespace sp = tantra::spec;
    Tantra* t = t_;
    s.simt = oapiGetSimTime(); s.dt = dt;
    double lng = 0, lat = 0, rad = 0; t->GetEquPos(lng, lat, rad);
    if (OBJHANDLE ref = t->GetGravityRef()) { const double r = (std::max)(1.0, rad); s.g = GGRAV * oapiGetMass(ref) / (r * r); }
    VECTOR3 f, r; t->HorizonRot(_V(0, 0, 1), f); t->HorizonRot(_V(1, 0, 0), r);   // the nose, the right wing (x east, y up, z north)
    s.pitch = std::asin((std::max)(-1.0, (std::min)(1.0, f.y))) * DEG;
    s.bank = std::asin((std::max)(-1.0, (std::min)(1.0, -r.y))) * DEG;
    s.hdg = std::atan2(f.x, f.z) * DEG; if (s.hdg < 0) s.hdg += 360.0;
    VECTOR3 v; t->GetGroundspeedVector(FRAME_HORIZON, v);
    const double h = s.hdg * RAD;
    s.alt = t->GetAltitude(ALTMODE_GROUND); s.vz = v.y;
    s.vF = v.x * std::sin(h) + v.z * std::cos(h); s.vS = v.x * std::cos(h) - v.z * std::sin(h);
    s.mach = t->GetMachNumber(); s.mass = t->GetMass(); s.contact = t->GroundContact();
    s.marchMax = t->march_ && !t->AnaIsMain() ? t->GetThrusterMax0(t->march_) : 0.0;
    s.marchLv = t->march_ ? t->GetThrusterLevel(t->march_) : 0.0;
    // the pods: the direction table weighted by the pairs' shares, turned as UpdatePods turns them (RotateAbout's formula)
    const double so = sp::kPodSplayOutDeg * RAD, sd = sp::kPodSplayDownDeg * RAD;
    double wsum = 0.0;
    for (int p = 0; p < sp::kPodCount; ++p) wsum += t->podShare_[p / 2];
    for (int i = 0; i < gd::kPodTab; ++i) {
        const double sw = i * 10.0 / sp::kPodSwivelMaxDeg * m::kPodSwivelMax, c = std::cos(sw), sn = std::sin(sw);
        double F = 0.0, U = 0.0;
        for (int p = 0; p < sp::kPodCount; ++p) {
            const double sgn = m::kPods[p].pivot.x >= 0.0 ? 1.0 : -1.0;
            const VECTOR3 k = _V(m::kPods[p].axis.x, m::kPods[p].axis.y, m::kPods[p].axis.z) * sgn;
            const VECTOR3 b = _V(-sgn * std::sin(so) * std::cos(sd), std::sin(sd), std::cos(so) * std::cos(sd));
            const VECTOR3 d = b * c + crossp(k, b) * sn + k * (dotp(k, b) * (1.0 - c));
            F += t->podShare_[p / 2] * d.z; U += t->podShare_[p / 2] * d.y;
        }
        s.podTab[i][0] = wsum > 0 ? F / wsum : 0.0; s.podTab[i][1] = wsum > 0 ? U / wsum : 0.0;
    }
    double pm = 0.0, lv = 0.0;
    for (THRUSTER_HANDLE th : t->pod_) if (th) { pm += t->GetThrusterMax0(th); lv += t->GetThrusterLevel(th); }
    s.podMaxEst = !(t->podOut_ >= 1.0 && t->podAimed_);
    s.podMax = s.podMaxEst ? t->prm_.podThrustTotal / sp::kPodCount * wsum : pm;   // not aimed: the nominal (no thrust until aimed)
    s.podLv = lv / sp::kPodCups;
    s.podAngle = t->podAngle_; s.podCmdAngle = t->podTarget_; s.podOut = t->podOut_;
    s.podAimed = t->podAimed_; s.podsWanted = t->podsWanted_;
    s.podLvMax = t->interior_.UvtOn() ? 0.85 : 1.0;
    s.gearLying = t->carriage_.Set() == tantra::Carriage::FlightSet::Level;
    s.gear = t->carriage_.Gear(); s.gearDown = t->carriage_.GearDown();
    s.wingsFolded = t->wingMode_ == 2 || t->tuck_ >= 0.5;
    s.plantRun = t->plant_.Running();
    s.otherAp = asc_.Engaged() || land_.Engaged() || rent_.Engaged();
}

// every frame: both guidances follow the ship (the plan, the kinematics, the checks); their commands are not applied yet
void TantraDisplays::ApStep(double dt) {
    gd::AscentState as; FillAscentState(as, dt);
    gd::LandState ls; FillLandState(ls, dt);
    const double now = oapiGetSysTime();
    asc_.Step(as, now);
    land_.Step(ls, now);
    gd::BellyState bs; FillBellyState(bs, dt);
    belly_.Step(bs, now);
    // СХОД: the planet's bases for the site (every 30 s, at once on another planet), then its step
    {
        Tantra* t = t_;
        OBJHANDLE ref = t->GetSurfaceRef();
        const double simt = oapiGetSimTime();
        if (ref != rnRef_ || std::fabs(simt - rnSitesT_) > 30.0) {
            rnRef_ = ref; rnSitesT_ = simt;
            std::vector<tantra::reentry::Site> sites;
            if (ref) for (DWORD i = 0; i < oapiGetBaseCount(ref); i++) {
                OBJHANDLE b = oapiGetBaseByIndex(ref, i); double bl = 0, bb = 0; oapiGetBaseEquPos(b, &bl, &bb);
                char nm[64]; oapiGetObjectName(b, nm, sizeof nm);
                sites.push_back({tantra::scr::W1251(nm), bb, bl});
            }
            rent_.SetSites(sites);
        }
        tantra::reentry::ReentryState rs; FillReentryState(rs, dt);
        rent_.Step(rs, now);
    }
    ApApply(dt);                                                          // and they fly the ship (TantraFlightCtl.cpp)
}

// СХОД's state: the ascent's kinematics and engines, the attitude, the plant's stern, the skin (the thermal page's exposure and air
// table), the wings, the energy core's readiness
void TantraDisplays::FillReentryState(tantra::reentry::ReentryState& s, double dt) {
    namespace tc = tantra::tcore;
    Tantra* t = t_;
    s.simt = oapiGetSimTime(); s.dt = dt;
    if (OBJHANDLE ref = t->GetGravityRef()) {
        s.planet.R = oapiGetSize(ref); s.planet.mu = GGRAV * oapiGetMass(ref);
        const double per = oapiGetPlanetPeriod(ref);
        s.planet.omega = std::fabs(per) > 1.0 ? 2 * PI / per : 0.0;
    }
    s.air = thermAir_;
    double lng = 0, lat = 0, rad = 0; t->GetEquPos(lng, lat, rad);
    s.lat = lat; s.lon = lng; s.alt = t->GetAltitude(ALTMODE_MEANRAD);
    VECTOR3 v; t->GetGroundspeedVector(FRAME_HORIZON, v);                 // x east, y up, z north
    s.vE = v.x; s.vU = v.y; s.vN = v.z;
    VECTOR3 f; t->HorizonRot(_V(0, 0, 1), f);
    s.pitch = std::asin((std::max)(-1.0, (std::min)(1.0, f.y))) * DEG;
    s.hdg = std::atan2(f.x, f.z) * DEG; if (s.hdg < 0) s.hdg += 360.0;
    s.bank = t->GetBank() * DEG; s.aoa = t->GetAOA() * DEG;
    s.rho = t->GetAtmDensity(); s.mach = t->GetMachNumber(); s.q = t->GetDynPressure(); s.contact = t->GroundContact();
    s.mass = t->GetMass(); s.argon = t->ArgonMass(); s.iron = t->IronMass();
    s.reactionMass = s.argon <= 0 && s.iron <= 0 ? gd::kNoMass : t->marchHigh_ ? gd::kIron : gd::kArgon;
    s.Fmarch = t->march_ && !t->AnaIsMain() ? t->GetThrusterMax0(t->march_) : 0.0;
    s.FmNow = t->march_ ? t->GetThrusterMax0(t->march_) * t->GetThrusterLevel(t->march_) : 0.0;
    s.Fpods = s.FpNow = 0.0;
    for (THRUSTER_HANDLE h : t->pod_) if (h) { const double m = t->GetThrusterMax0(h); s.FpNow += m * t->GetThrusterLevel(h); if (t->podOut_ >= 1.0 && t->podAimed_) s.Fpods += m; }
    s.plantRun = t->plant_.Running(); s.sternT = t->plant_.SternT(); s.tSafe = t->plant_.Cfg().tSafe; s.limiter = t->plant_.Limiter();
    for (int z = 0; z < tantra::reentry::kZones; ++z) { s.skinT[z] = t->damage_.Temperature(z); s.skinLim[z] = t->damage_.Limit(z); s.skinFlux[z] = t->damage_.HeatFlux(z); }
    s.skin = t->damage_;
    const tantra::CarriagePose& cp = t->carriage_.Pose();
    s.fold = (std::max)(t->tuck_, cp.tuck);
    s.expo.crests = s.expo.fin = 1.0 - s.fold;
    s.expo.pods = t->podOut_; s.expo.gear = t->carriage_.Gear(); s.expo.hangar = t->hangar_; s.expo.bays = t->bayDoors_;
    s.expo.sternCups = (std::max)(t->marchOut_, t->irisAna_);
    s.wingMode = t->wingMode_; s.crestAvail = t->aeroCrest_; s.gearArea = t->aeroGearArea_; s.podsOut = t->podOut_;
    const tc::Snapshot& k = t->CoreState();
    auto ok = [](const tc::Node& n) { return n.state != tc::kFault && n.state != tc::kLost; };
    s.coilMargin = k.margin;
    s.cryoOk = ok(k.cryo); s.pumpsOk = ok(k.pump); s.veuOk = ok(k.veu);
    s.storeOk = ok(k.store) && k.storeE > 0.01 * (std::max)(1.0, k.storeMax);
}

void TantraDisplays::DrawAutopilot(oapi::Sketchpad* skp, int ox, int oy, int w, int h) {
    Tantra* t = t_;
    ap::View v;
    v.page = apPage_; v.sysT = oapiGetSysTime(); v.warp = oapiGetTimeAcceleration();
    v.asc = &asc_; v.land = &land_; v.belly = &belly_; v.rent = &rent_;
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

// (2026-10-09) the self-test (Tantra::SelfTestStep, SELFTEST 2) presses the landing page's keys as the pilot would: ВЗВЕСТИ
// (the point under the ship), then, once the check has passed, ПУСК and ПОДТВЕРДИТЬ
int TantraDisplays::TestLandEngage() {
    const double now = oapiGetSysTime();
    if (land_.Engaged()) return 2;
    if (land_.Armed()) { land_.Start(now); land_.Start(now); return land_.Engaged() ? 2 : 1; }
    if (!land_.Checking() && land_.CanArm()) {
        double lng = 0, lat = 0, rad = 0; t_->GetEquPos(lng, lat, rad);
        padLon_ = lng; padLat_ = lat; havePad_ = true;
        land_.Arm(now); apPage_ = 1;
    }
    return land_.Checking() ? 1 : 0;
}

bool TantraDisplays::TouchAutopilot(double x, double y) {
    const int c = apScr_.Hit(x, y);
    if (c < 0) return false;
    if (c == ap::kCmdLArm) {                                              // the landing point: where the ship is when armed
        double lng = 0, lat = 0, rad = 0; t_->GetEquPos(lng, lat, rad);
        padLon_ = lng; padLat_ = lat; havePad_ = true;
    }
    ap::Press(c, asc_, land_, belly_, rent_, oapiGetSysTime(), &apPage_);
    return true;
}
