// Tantra: radiation safety of the anamezon drive for Orbiter 2010.
#include "TantraSafety.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace tantra;

namespace {
// A planet has an ecosystem if its air holds oxygen (Earth: ~21 kPa).
constexpr double kEcosystemO2 = 1000.0;  // Pa

double Angle(const VECTOR3& a, const VECTOR3& b) {
    const double c = dotp(a, b) / (length(a) * length(b));
    return std::acos((std::max)(-1.0, (std::min)(1.0, c)));
}
}  // namespace

void TantraSafety::Scan(VESSEL* self, double jetPowerFull, double level) {
    VECTOR3 me, axis;
    self->GetGlobalPos(me);
    self->GlobalRot(_V(0, 0, -1), axis);  // the jet leaves aft
    const double power = jetPowerFull * level;

    planets_.clear();
    for (DWORD i = 0; i < oapiGetGbodyCount(); ++i) {
        OBJHANDLE h = oapiGetGbodyByIndex(i);
        if (oapiGetObjectType(h) != OBJTP_PLANET) continue;
        const ATMCONST* atm = oapiGetPlanetAtmConstants(h);
        if (!atm || atm->O2pp < kEcosystemO2) continue;
        VECTOR3 p;
        oapiGetGlobalPos(h, &p);
        const VECTOR3 to = p - me;
        const double d = length(to), R = oapiGetSize(h);
        PlanetRisk r;
        char name[256];
        oapiGetObjectName(h, name, sizeof name);
        r.name = name;
        r.distance = d;
        r.inBeam = rad_.InBeam(Angle(axis, to), std::asin((std::min)(1.0, R / d)));
        r.intercepted = rad_.InterceptedPower(power, R, d, r.inBeam);
        r.hazard = rad_.PlanetHazard(r.intercepted);
        r.safeLevel = rad_.PlanetSafeLevel(jetPowerFull, R, d, r.inBeam);
        planets_.push_back(r);
    }

    crews_.clear();
    for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
        OBJHANDLE h = oapiGetVesselByIndex(i);
        if (!h || h == self->GetHandle()) continue;
        VESSEL* v = oapiGetVesselInterface(h);
        if (!v) continue;
        VECTOR3 p;
        v->GetGlobalPos(p);
        const VECTOR3 to = p - me;
        CrewRisk c;
        c.name = v->GetName();
        c.distance = length(to);
        c.inBeam = rad_.InBeam(Angle(axis, to), 0.0);
        c.rate = rad_.DoseRate(power, c.distance, c.inBeam);
        c.safeLevel = rad_.PersonSafeLevel(jetPowerFull, c.distance, c.inBeam);
        crews_.push_back(c);
    }

    cap_ = 1.0;
    reasonRu_.clear();
    reasonEn_.clear();
    char ru[256], en[256];
    for (const PlanetRisk& r : planets_) {
        if (r.safeLevel < cap_) {
            cap_ = r.safeLevel;
            std::snprintf(ru, sizeof ru, "экосистема: %s%s", r.name.c_str(), r.inBeam ? " (в луче)" : "");
            std::snprintf(en, sizeof en, "ecosystem: %s%s", r.name.c_str(), r.inBeam ? " (in the jet)" : "");
            reasonRu_ = ru;
            reasonEn_ = en;
        }
    }
    for (const CrewRisk& c : crews_) {
        if (c.safeLevel < cap_) {
            cap_ = c.safeLevel;
            std::snprintf(ru, sizeof ru, "экипаж: %s, %.0f км%s", c.name.c_str(), c.distance / 1e3, c.inBeam ? " (в луче)" : "");
            std::snprintf(en, sizeof en, "crew: %s, %.0f km%s", c.name.c_str(), c.distance / 1e3, c.inBeam ? " (in the jet)" : "");
            reasonRu_ = ru;
            reasonEn_ = en;
        }
    }
}

void TantraSafety::Update(VESSEL* self, double jetPowerFull, double level, double dt) {
    scanTimer_ -= dt;
    if (scanTimer_ <= 0.0) {
        Scan(self, jetPowerFull, level);
        scanTimer_ = 0.5;
    } else {
        // Keep the rates in step with the throttle between scans.
        for (CrewRisk& c : crews_) c.rate = rad_.DoseRate(jetPowerFull * level, c.distance, c.inBeam);
    }

    for (CrewRisk& c : crews_) {
        if (c.rate <= 0.0) continue;
        DoseRecord& d = ledger_.Add(c.name, c.rate, dt);
        c.dose = d.dose;
        const int now = d.dose >= rad_.Spec().lethalDose ? 2 : d.dose >= 1.0 ? 1 : 0;
        if (now > d.reported) {
            d.reported = now;
            Notice n;
            char ru[256], en[256];
            if (now == 2) {
                std::snprintf(ru, sizeof ru, "%s: экипаж получил смертельную дозу (%.1f Гр)", c.name.c_str(), d.dose);
                std::snprintf(en, sizeof en, "%s: crew received a lethal dose (%.1f Gy)", c.name.c_str(), d.dose);
            } else {
                std::snprintf(ru, sizeof ru, "%s: экипаж облучён, %.1f Гр - лучевая болезнь", c.name.c_str(), d.dose);
                std::snprintf(en, sizeof en, "%s: crew irradiated, %.1f Gy - radiation sickness", c.name.c_str(), d.dose);
            }
            n.ru = ru;
            n.en = en;
            notices_.push_back(n);
        }
    }
}

std::vector<TantraSafety::Notice> TantraSafety::TakeNotices() {
    std::vector<Notice> out;
    out.swap(notices_);
    return out;
}
