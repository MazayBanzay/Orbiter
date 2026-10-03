#include "Legs.h"

#include <algorithm>
#include <cmath>

namespace tantra::legs {

namespace {
constexpr double kMu0 = 4.0e-7 * 3.14159265358979323846;
constexpr double kG0 = 9.80665;
double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
}  // namespace

double BearingCapacity(double area, double tesla) { return tesla * tesla / (2.0 * kMu0) * area; }

TouchdownLimits Limits(double stroke) {
    return {std::sqrt(2.0 * stroke * kSoftG * kG0), std::sqrt(2.0 * stroke * kBreakG * kG0)};
}

void LegLoads(const LegLoadInput& in, double out[kLegCount]) {
    const double col = Clamp01(in.columnShare), stern = 1.0 - col, kang = Clamp01(in.kangShare);
    const double front = col * in.weight;
    out[kKangaroo] = front * kang;
    const double mSide = in.windSide * in.h;   // force to starboard loads the starboard leg
    const double base = front * (1.0 - kang) / 2.0;
    out[0] = col > 0.0 ? (std::max)(0.0, base - mSide / (2.0 * in.hipX)) : 0.0;
    out[1] = col > 0.0 ? (std::max)(0.0, base + mSide / (2.0 * in.hipX)) : 0.0;
    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < 4; ++i) {
        sx += in.feetX[i] * in.feetX[i];
        sy += in.feetY[i] * in.feetY[i];
    }
    const double a = sx > 0.0 ? in.windX * in.h / sx : 0.0, b = sy > 0.0 ? in.windY * in.h / sy : 0.0;
    for (int i = 0; i < 4; ++i)
        out[2 + i] = stern > 0.0 ? (std::max)(0.0, stern * in.weight / 4.0 + a * in.feetX[i] + b * in.feetY[i]) / in.cosSplay : 0.0;
}

void Soles::Update(double dt, bool contact, bool resting) {
    if (!contact) {
        jam_ = rest_ = 0.0;
        anchors_ = (std::max)(0.0, anchors_ - dt / 10.0);   // the root stays in the ground: the foot just leaves it
        return;
    }
    jam_ = Clamp01(jam_ + dt / kJamTime);
    rest_ = resting ? rest_ + dt : 0.0;
    const bool out = resting && rest_ >= kAnchorDelay;
    anchors_ = Clamp01(anchors_ + (out ? dt / kAnchorTime : -dt / 10.0));
}

double Soles::Mu() const {
    return kMuConform + (kMuJammed - kMuConform) * jam_ + (kMuAnchored - kMuJammed) * anchors_;
}

void Regen::Update(double dt, double weight, double h, bool moving) {
    power_ = 0.0;
    if (h_ < 0.0 || !moving || dt <= 0.0) {
        h_ = h;
        return;
    }
    const double e = weight * (h - h_);         // + lifted, - lowered
    if (e < 0.0) back_ += -e * kRegenEff;
    else spent_ += e / kRegenEff;
    power_ = (e < 0.0 ? -e * kRegenEff : -e / kRegenEff) / dt;
    h_ = h;
}

}  // namespace tantra::legs
