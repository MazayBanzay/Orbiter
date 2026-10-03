#include "Ignition.h"

#include <algorithm>

namespace tantra {

namespace {
double Ramp(double value, bool up, double upTime, double downTime, double dt) {
    return up ? (std::min)(1.0, value + dt / upTime) : (std::max)(0.0, value - dt / downTime);
}
}  // namespace

void Ignition::StepUp() {
    int next = (std::max)(static_cast<int>(stage_), static_cast<int>(target_)) + 1;
    target_ = static_cast<IgnStage>((std::min)(next, static_cast<int>(IgnStage::Feed)));
}

void Ignition::Shutdown() { target_ = IgnStage::Off; }

void Ignition::EmergencyCut() {
    target_ = IgnStage::Off;
    feed_ = 0.0;
    beam_ = 0.0;
}

void Ignition::Update(double dt, bool powerOk) {
    if (!powerOk && target_ != IgnStage::Off) EmergencyCut();

    const bool wantField = target_ >= IgnStage::Field;
    const bool wantBeam = target_ >= IgnStage::Beam;
    const bool wantFeed = target_ >= IgnStage::Feed;

    // Feed stops instantly; the beam needs the field; the field decays slowly.
    if (!wantFeed || beam_ < 1.0) feed_ = 0.0;
    else feed_ = Ramp(feed_, true, kFeedTime, kFeedTime, dt);

    if (field_ < 1.0) beam_ = Ramp(beam_, false, kBeamTime, kFeedTime, dt);
    else beam_ = Ramp(beam_, wantBeam, kBeamTime, kFeedTime, dt);

    field_ = Ramp(field_, wantField, kFieldTime, kDecayTime, dt);

    if (feed_ >= 1.0) stage_ = IgnStage::Feed;
    else if (beam_ >= 1.0) stage_ = IgnStage::Beam;
    else if (field_ >= 1.0) stage_ = IgnStage::Field;
    else stage_ = IgnStage::Off;
}

void Ignition::Load(int value) {
    value = std::clamp(value, 0, static_cast<int>(IgnStage::Feed));
    stage_ = target_ = static_cast<IgnStage>(value);
    field_ = value >= 1 ? 1.0 : 0.0;
    beam_ = value >= 2 ? 1.0 : 0.0;
    feed_ = value >= 3 ? 1.0 : 0.0;
}

const char* StageName(IgnStage s, bool russian) {
    switch (s) {
        case IgnStage::Field: return russian ? "ПОЛЕ КАМЕР" : "CHAMBER FIELD";
        case IgnStage::Beam: return russian ? "ЛУЧ К-ЧАСТИЦ" : "K-PARTICLE BEAM";
        case IgnStage::Feed: return russian ? "ПОДАЧА АНАМЕЗОНА" : "ANAMEZON FEED";
        default: return russian ? "ВЫКЛ" : "OFF";
    }
}

}  // namespace tantra
