// TantraDisplays - the autopilots fly the ship (the user, 2026-10-05: «должны управлять с горячей корректировкой»): the guidance
// (TantraGuidance: ВЗЛЁТ НА ОРБИТУ, ПОСАДКА НА КОРМУ) gives its commands every step and here they go to the ship - the attitude
// by the RCS (as РАД ±: the error toward the target direction in the ship's axes, its rate filtered, a rate-limited PD), the
// engines' levels (the march's group, the pods' common lever), the pods out / in, the stern legs. Released once when the
// autopilot lets go (the RCS groups to 0; the levels stay where they were - the pilot's levers take over).
#include "TantraDisplays.h"
#include "Tantra.h"

#include <algorithm>
#include <cmath>

namespace gd = tantra::guidance;

namespace {
const THGROUP_TYPE kAtt[6] = {THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN, THGROUP_ATT_YAWLEFT, THGROUP_ATT_YAWRIGHT, THGROUP_ATT_BANKLEFT, THGROUP_ATT_BANKRIGHT};
}

// The nose (+z) toward `nose` and, if `up` is given, the ship's +y toward it (both in the ship's own axes, unit): the angle errors
// in pitch, yaw and roll, their rates filtered from the change, a PD with the rate limited to `rateMax` [rad/s].
void TantraDisplays::FlyAttitude(const VECTOR3& nose, const VECTOR3* up, double dt, double rateMax) {
    Tantra* t = t_;
    dt = (std::max)(dt, 1e-4);
    const double ep = std::atan2(nose.y, nose.z), ey = std::atan2(nose.x, nose.z);
    double er = 0.0;
    if (up) er = std::atan2(up->x, up->y);                               // the target up to the ship's right (+x): roll right
    if (fcInit_) {
        auto rate = [&](double now, double was, double& w) { const double d = now - was; if (std::fabs(d) < PI) w += (-d / dt - w) * (std::min)(1.0, dt * 5); };
        rate(ep, fcEp_, fcWp_); rate(ey, fcEy_, fcWy_); rate(er, fcEr_, fcWr_);
    } else { fcWp_ = fcWy_ = fcWr_ = 0.0; fcInit_ = true; }
    fcEp_ = ep; fcEy_ = ey; fcEr_ = er;
    auto axis = [&](double err, double w, THGROUP_TYPE plus, THGROUP_TYPE minus) {
        const double wd = (std::max)(-rateMax, (std::min)(rateMax, 0.5 * err)), u = (std::max)(-1.0, (std::min)(1.0, (wd - w) * 60.0));
        t->SetThrusterGroupLevel(plus, (std::max)(0.0, u)); t->SetThrusterGroupLevel(minus, (std::max)(0.0, -u));
    };
    axis(ep, fcWp_, THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN);
    axis(ey, fcWy_, THGROUP_ATT_YAWRIGHT, THGROUP_ATT_YAWLEFT);
    axis(up ? er : 0.0, fcWr_, THGROUP_ATT_BANKRIGHT, THGROUP_ATT_BANKLEFT);
    fcFlying_ = true;
}

void TantraDisplays::FlyRelease() {
    if (!fcFlying_) return;
    for (THGROUP_TYPE g : kAtt) t_->SetThrusterGroupLevel(g, 0.0);
    fcFlying_ = false; fcInit_ = false;
}

// Every step after the guidance: the engaged autopilot's commands to the ship.
void TantraDisplays::ApApply(double dt) {
    Tantra* t = t_;
    auto toLocal = [&](const VECTOR3& h) { VECTOR3 l; t->HorizonInvRot(h, l); return unit(l); };   // horizon (x E, y up, z N) -> ship
    if (asc_.Engaged()) {
        radMode_ = 0;                                                     // (РАД ± would fight it)
        const gd::AscentCmd& c = asc_.Cmd();
        if (c.attitude) {
            const double p = c.pitch * RAD, h = c.hdg * RAD;
            const VECTOR3 n = toLocal(_V(std::cos(p) * std::sin(h), std::sin(p), std::cos(p) * std::cos(h)));
            // the roll: near the vertical the belly toward the pitch plane's azimuth (+y away from it), else the top up
            const VECTOR3 u = c.pitch > 80.0 ? toLocal(_V(-std::sin(h), 0.0, -std::cos(h))) : toLocal(_V(0, 1, 0));
            VECTOR3 up = u - n * dotp(u, n);
            if (length(up) > 1e-3) { up = unit(up); FlyAttitude(n, &up, dt, (c.pitch > 80.0 ? 1.5 : 2.0) * RAD); } else FlyAttitude(n, nullptr, dt, 2.0 * RAD);
        } else FlyRelease();
        if (c.thrust) { t->SetThrusterGroupLevel(THGROUP_MAIN, c.level); if (c.pods) SetPodLevel(c.podsOut ? c.level : 0.0); }
        if (c.pods && c.podsOut != t->podsWanted_) { if (c.podsOut) t->ActPods(false); else if (PodLevel() < 0.001) { t->podsWanted_ = false; t->podTarget_ = 0.0; } }
        apFlew_ = true;
        return;
    }
    if (land_.Engaged()) {
        radMode_ = 0;
        const gd::LandCmd& c = land_.Cmd();
        if (c.attitude) {                                                 // the hull tilted from the vertical toward east / north
            const VECTOR3 n = toLocal(unit(_V(std::tan(c.thE * RAD), 1.0, std::tan(c.thN * RAD))));
            FlyAttitude(n, nullptr, dt, 2.0 * RAD);
        } else FlyRelease();
        if (c.thrust) {
            const double fm = t->march_ ? t->GetThrusterMax0(t->march_) : 0.0;
            double fp = 0.0; for (THRUSTER_HANDLE hh : t->pod_) if (hh) fp += t->GetThrusterMax0(hh);
            t->SetThrusterGroupLevel(THGROUP_MAIN, fm > 1.0 ? (std::min)(1.0, c.Fm / fm) : 0.0);
            SetPodLevel(fp > 1.0 ? (std::min)(1.0, c.Fp / fp) : 0.0);
        }
        if (c.legs && !t->carriage_.GearDown()) t->ActGear();             // the stern legs out from 400 m
        apFlew_ = true;
        return;
    }
    if (!rent_.Engaged()) rnHand_ = 0;
    if (rent_.Engaged()) {                                                // СХОД: the deorbit, the entry, the braking to 5 km
        radMode_ = 0;
        const tantra::reentry::ReentryCmd& c = rent_.Cmd();
        const double now = oapiGetSysTime();
        if (c.attitude) {                                                 // its directions: x east, y north, z up -> the horizon frame's x, z, y
            const VECTOR3 n = toLocal(_V(c.nose.x, c.nose.z, c.nose.y));
            VECTOR3 up = _V(0, 0, 0);
            if (std::fabs(c.up.x) + std::fabs(c.up.y) + std::fabs(c.up.z) > 1e-3) { up = toLocal(_V(c.up.x, c.up.z, c.up.y)); up = up - n * dotp(up, n); }
            if (length(up) > 1e-3) { up = unit(up); FlyAttitude(n, &up, dt, 2.0 * RAD); } else FlyAttitude(n, nullptr, dt, 2.0 * RAD);
        } else FlyRelease();
        if (c.thrust) {
            if (t->march_ && !t->AnaIsMain()) t->SetThrusterLevel(t->march_, c.march);   // the march itself, never the anamezon
            SetPodLevel(c.pods);
        }
        if (c.podsOut && !t->podsWanted_) t->ActPods(false);              // out, cups aft: thrust along the hull
        if (c.wingMode >= 0 && c.wingMode != t->wingMode_ && now - rnWingT_ > 3.0) { t->ActCrests(); rnWingT_ = now; }   // 90 -> 30 -> folded
        // the handover at the landing's entry: ПОСАДКА НА КОРМУ checks (the point under the ship) and starts; СХОД holds 150 m/s
        // down until it does, then lets go
        if (c.handover) {
            if (rnHand_ == 0) {
                double lng = 0, lat = 0, rad = 0; t->GetEquPos(lng, lat, rad);
                padLon_ = lng; padLat_ = lat; havePad_ = true;
                land_.Arm(now); rnHand_ = 1; apPage_ = 1;
            } else if (rnHand_ == 1 && land_.Armed()) {
                land_.Start(now); land_.Start(now);                       // ПУСК and its confirmation
                if (land_.Engaged()) { rnHand_ = 2; rent_.HandedOver(); }
            }
        }
        apFlew_ = true;
        return;
    }
    if (belly_.Engaged()) {                                               // ПОСАДКА ЛЁЖА: level, the heading held, the pods' lift
        radMode_ = 0;
        const gd::BellyCmd& c = belly_.Cmd();
        if (c.attitude) {                                                 // the nose on the heading at the pitch, the top banked
            const double p = c.pitch * RAD, h = c.hdg * RAD, b = c.bank * RAD;
            const VECTOR3 n = toLocal(_V(std::cos(p) * std::sin(h), std::sin(p), std::cos(p) * std::cos(h)));
            const VECTOR3 rt = _V(std::cos(h), 0.0, -std::sin(h));
            VECTOR3 up = toLocal(_V(0, 1, 0) * std::cos(b) + rt * std::sin(b));
            up = up - n * dotp(up, n);
            if (length(up) > 1e-3) { up = unit(up); FlyAttitude(n, &up, dt, 2.0 * RAD); } else FlyAttitude(n, nullptr, dt, 2.0 * RAD);
        } else FlyRelease();
        if (c.pods) { t->podsWanted_ = true; t->podTarget_ = (std::max)(0.0, (std::min)(180.0, c.podAngle)); }   // not ActPodsTo: no message every step
        if (c.thrust) {
            if (t->march_ && !t->AnaIsMain()) t->SetThrusterLevel(t->march_, c.march);   // the march itself, never the anamezon
            SetPodLevel(c.podLv);
        }
        if (c.gear) {                                                     // the lying gear: the set first (only with the gear stowed), then out
            const double now = oapiGetSysTime();
            if (t->carriage_.Set() != tantra::Carriage::FlightSet::Level) { if (t->carriage_.Gear() <= 0.0 && now - blGearT_ > 3.0) { t->ActGearSet(); blGearT_ = now; } }
            else if (!t->carriage_.GearDown() && now - blGearT_ > 3.0) { t->ActGear(); blGearT_ = now; }
        }
        apFlew_ = true;
        return;
    }
    if (apFlew_) { FlyRelease(); apFlew_ = false; }                       // let go: the RCS quiet, the levers the pilot's
}
