// TantraBellyLand: see TantraBellyLand.h.
#include "TantraBellyLand.h"

#include <algorithm>
#include <cmath>

namespace tantra::guidance {

const wchar_t* const kBPhases[kBPhaseCount] = {L"ГОТОВНОСТЬ: ВЫДВ. БЛОКИ", L"УДЕРЖАНИЕ", L"ТОРМОЖЕНИЕ · ШАССИ", L"СНИЖЕНИЕ 3 м/с",
                                               L"СНИЖЕНИЕ 1 м/с", L"КАСАНИЕ", L"ОТСЕЧКА"};

namespace {
double Cl(double x, double a, double b) { return (std::max)(a, (std::min)(b, x)); }
bool PodsReady(const BellyState& s) { return s.podOut >= 1.0 && s.podAimed && s.podsWanted; }
}

// ---- the pods' table ----
void BellyLand::PodDir(const BellyState& s, double ang, double& fwd, double& up) {
    const double a = Cl(ang, 0.0, 180.0) / 10.0;
    const int i = (std::min)(kPodTab - 2, int(a));
    const double f = a - i;
    fwd = s.podTab[i][0] + (s.podTab[i + 1][0] - s.podTab[i][0]) * f;
    up = s.podTab[i][1] + (s.podTab[i + 1][1] - s.podTab[i][1]) * f;
}

// the elevation unwrapped along the table (from forward, deg); the cup angle giving `elev` nearest the cups' angle now
double BellyLand::CupFor(const BellyState& s, double elev) {
    double e[kPodTab];
    for (int i = 0; i < kPodTab; ++i) {
        e[i] = std::atan2(s.podTab[i][1], s.podTab[i][0]) * kR2D;
        if (i > 0) { while (e[i] - e[i - 1] > 180.0) e[i] -= 360.0; while (e[i] - e[i - 1] < -180.0) e[i] += 360.0; }
    }
    double best = kNaN, bestD = kInf, near = 0.0, nearD = kInf;
    for (int i = 0; i + 1 < kPodTab; ++i) {
        const double lo = (std::min)(e[i], e[i + 1]), hi = (std::max)(e[i], e[i + 1]);
        for (int k = -1; k <= 1; ++k) {
            const double t = elev + 360.0 * k;
            if (t >= lo && t <= hi && hi > lo) {
                const double a = 10.0 * (i + (t - e[i]) / (e[i + 1] - e[i]));
                if (std::fabs(a - s.podAngle) < bestD) { bestD = std::fabs(a - s.podAngle); best = a; }
            }
            const double d = (std::min)(std::fabs(t - e[i]), std::fabs(t - e[i + 1]));
            if (d < nearD) { nearD = d; near = std::fabs(t - e[i]) < std::fabs(t - e[i + 1]) ? 10.0 * i : 10.0 * (i + 1); }
        }
    }
    return std::isfinite(best) ? best : near;
}

double BellyLand::VertAngle() const {
    const double a = CupFor(st_, 90.0);
    double f, u; PodDir(st_, a, f, u);
    return u > 0.0 && std::fabs(std::atan2(u, f) * kR2D - 90.0) < 1.0 ? a : kNaN;
}

// the most lift within the tilt window (the elevation 90 - fwd .. 90 + aft), at the lever's ceiling
double BellyLand::LiftMax() const {
    double best = 0.0;
    for (int a = 0; a <= 180; ++a) {
        double f, u; PodDir(st_, a, f, u);
        const double el = std::atan2(u, f) * kR2D;
        if (el >= 90.0 - kBTiltFwdNoMarch && el <= 90.0 + kBTiltAft) best = (std::max)(best, u);
    }
    return best * st_.podMax * st_.podLvMax;
}

double BellyLand::PrepTime() const {
    if (PodsReady(st_)) return 0.0;
    const double va = VertAngle();
    if (!std::isfinite(va)) return kInf;
    return (1.0 - Cl(st_.podOut, 0, 1)) * 12.0 + std::fabs(va - (st_.podOut >= 1.0 ? st_.podAngle : 0.0)) / 15.0;
}

std::wstring BellyLand::TLabel() const { return mode_ == kOff && t0_ <= 0.0 ? std::wstring(L"Т —") : L"Т+" + Clock((std::max)(0.0, st_.simt - t0_)); }

// ---- the readiness check (ВКЛ) ----
std::vector<Check> BellyLand::BuildChecks(bool* ok) const {
    const BellyState& s = st_;
    std::vector<Check> c;
    bool all = true;
    auto add = [&](const std::wstring& t, bool good, bool must = true) { c.push_back({t, good}); if (must && !good) all = false; };
    add(s.contact ? L"на грунте: ВКЛ только в воздухе" : L"в воздухе · " + Fmt(s.alt, 0) + L" м над грунтом", !s.contact);
    add(s.wingsFolded ? L"крылья сложены: выдвижные блоки не выйдут" : L"крылья развёрнуты: выдвижные блоки могут выйти", !s.wingsFolded);
    add(s.mach < 0.75 ? L"М " + Fmt(s.mach, 2) + L" < 0,75: створки выдвижных блоков открыты" : L"М " + Fmt(s.mach, 2) + L": створки выдвижных блоков закрыты выше М 0,8", s.mach < 0.75);
    add(s.plantRun ? L"энергоустановка на режиме" : L"энергоустановка не на режиме", s.plantRun);
    const double W = Weight(), L = LiftMax();
    add(L"тяга выдвижных блоков вверх " + Force(L) + (s.podMaxEst ? L" (оценка)" : L"") + L" " + (L >= kBThrustMargin * W ? L"≥" : L"<") + L" 1,1 × вес " + Force(W)
        + L" (×" + Fmt(W > 0 ? L / W : 0.0, 2) + L")", L >= kBThrustMargin * W);
    const double tp = PrepTime();
    if (tp <= 0.0) add(L"выдвижные блоки выпущены, чаши на " + Fmt(s.podAngle, 0) + L"°", true);
    else if (!std::isfinite(tp)) add(L"выдвижные блоки: тяга вверх недостижима ни при каком угле чаш", false);
    else {
        // the fall while they come out, then the stop at the braking the laws allow (3 m/s^2, less if the lift is short)
        const double vEnd = (std::max)(0.0, -s.vz + s.g * tp), aStop = (std::min)(3.0, (L / (std::max)(W, 1.0) - 1.0) * s.g);
        const double drop = (std::max)(0.0, -s.vz * tp + 0.5 * s.g * tp * tp) + (aStop > 0.1 ? vEnd * vEnd / (2 * aStop) : kInf);
        add(L"выдвижные блоки выйдут за " + Fmt(tp, 0) + L" с · просадка до " + Fmt(drop, 0) + L" м", s.alt - drop > 100.0);
    }
    add(L"тангаж " + Fmt(s.pitch, 0) + L"°, крен " + Fmt(s.bank, 0) + L"° (≤ 30°)", std::fabs(s.pitch) <= 30.0 && std::fabs(s.bank) <= 30.0);
    add(s.marchMax > 0.0 ? L"маршевая чаша: " + Force(s.marchMax) + L" (разгон)" : L"маршевая чаша не на ходу: скорость только наклоном чаш", true, false);
    if (s.otherAp) add(L"взлёт / посадка на корму отпустят управление", true, false);
    if (ok) *ok = all;
    return c;
}

// ---- the keys ----
void BellyLand::Engage(double) {
    if (!have_) return;
    bool ok = false;
    checks_ = BuildChecks(&ok);
    if (!ok) {
        for (const Check& c : checks_) if (!c.ok) { Note(L"ВКЛ отклонено: " + c.t, kBad); break; }
        return;
    }
    const BellyState& s = st_;
    altT_ = Cl(std::round(s.alt), kBAltMin, kBAltMax);
    spdT_ = Cl(std::round(s.vF), 0.0, kBSpdMax);
    hdgT_ = N360(std::round(s.hdg));
    mode_ = kHoldM; phase_ = PodsReady(s) ? 1 : 0;
    t0_ = tPh_ = s.simt; iVz_ = iAx_ = 0.0; warnLift_ = gearAsked_ = false;
    cupCmd_ = PodsReady(s) ? s.podAngle : (std::isfinite(VertAngle()) ? VertAngle() : 90.0);
    tdVz_ = tdVh_ = kNaN;
    Note(L"ВКЛ: держу " + Fmt(altT_, 0) + L" м · " + Fmt(spdT_, 0) + L" м/с · курс " + Fmt(hdgT_, 0) + L"°", kOk);
    if (phase_ == 0) Note(L"выдвижные блоки выходят: " + Fmt(PrepTime(), 0) + L" с без подъёмной тяги", kWarn);
}

void BellyLand::StepAlt(int dir, bool coarse) {
    if (!Engaged()) return;
    if (mode_ == kLand) { altT_ = std::round(st_.alt); mode_ = kHoldM; Note(L"ПОСАДКА прервана: удержание высоты", kWarn); }
    altT_ = Cl(altT_ + dir * (coarse ? kBAltCoarse : kBAltStep), kBAltMin, kBAltMax);
    Note(L"ВЫСОТА " + Fmt(altT_, 0) + L" м", kOk);
}

void BellyLand::StepSpd(int dir, bool coarse) {
    if (!Engaged()) return;
    if (mode_ == kLand) { altT_ = std::round(st_.alt); spdT_ = 0.0; mode_ = kHoldM; Note(L"ПОСАДКА прервана: удержание высоты", kWarn); }
    spdT_ = Cl(spdT_ + dir * (coarse ? kBSpdCoarse : kBSpdStep), 0.0, kBSpdMax);
    Note(L"ГОР. СКОРОСТЬ " + Fmt(spdT_, 0) + L" м/с" + (spdT_ <= 0.0 ? L" (висение)" : L""), kOk);
}

void BellyLand::StepHdg(int dir, bool coarse) {
    if (!Engaged()) return;
    hdgT_ = N360(hdgT_ + dir * (coarse ? kBHdgCoarse : kBHdgStep));
    Note(L"КУРС " + Fmt(hdgT_, 0) + L"°", kOk);
}

void BellyLand::Land(double) {
    if (mode_ != kHoldM) return;
    if (!st_.gearLying && st_.gear > 0.0) { Note(L"ПОСАДКА отклонена: выпущены кормовые ноги — убрать, затем посадка лёжа", kBad); return; }
    mode_ = kLand; tPh_ = st_.simt; gearAsked_ = false;
    Note(L"ПОСАДКА: скорость в 0, шасси лёжа, снижение 3 м/с, ниже 50 м — 1 м/с", kOk);
}

void BellyLand::Release(const std::wstring& why) {
    if (!Engaged()) return;
    mode_ = kOff; cmd_ = BellyCmd();
    Note(why, kWarn);
}

// ---- every step ----
void BellyLand::Step(const BellyState& st, double) {
    st_ = st; have_ = true;
    if (!Engaged()) {
        const bool landed = mode_ == kLandedM;
        cmd_ = BellyCmd();
        if (landed) phase_ = kBPhaseCount - 1;
        return;
    }
    Laws();
}

void BellyLand::Laws() {
    const BellyState& s = st_;
    BellyCmd& c = cmd_;
    const double dt = (std::max)(0.0, s.dt), m = s.mass, g = s.g, W = m * g;
    const bool ready = PodsReady(s);
    const double vh = std::hypot(s.vF, s.vS);
    c.attitude = c.thrust = c.pods = true;
    c.gear = mode_ == kLand;
    c.pitch = 0.0; c.hdg = hdgT_;
    // ---- the contact: the pods run down, then cut-off ----
    if (mode_ == kLand && (s.contact || phase_ == 5)) {
        if (phase_ != 5) {
            phase_ = 5; tTouch_ = s.simt; lvTouch_ = s.podLv; tdVz_ = -s.vz; tdVh_ = vh;
            const bool okT = tdVz_ <= kBNormVz && tdVh_ <= kBNormVh;
            Note(L"КАСАНИЕ: верт " + Fmt(tdVz_, 2) + L" м/с · гориз " + Fmt(tdVh_, 2) + L" м/с" + (okT ? L" · в норме" : L" · ВНЕ НОРМ"), okT ? kOk : kBad);
        }
        const double k = Cl(1.0 - (s.simt - tTouch_) / kBUnloadT, 0.0, 1.0);
        c.podLv = lvTouch_ * k; c.march = 0.0; c.podAngle = cupCmd_; c.bank = 0.0;
        if (s.simt - tTouch_ >= kBUnloadT + 0.5) {
            mode_ = kLandedM; phase_ = 6;
            Note(L"ОТСЕЧКА: корабль на лопастях и передней опоре", kOk);
        }
        return;
    }
    // ---- vertical: the altitude (hold) or the descent (landing) ----
    double vzCmd;
    if (mode_ == kLand) {
        const double rate = Cl(kBDescLo + (s.alt - kBLoAlt) * 0.2, kBDescLo, kBDescHi);
        vzCmd = -rate;
        const bool gearWait = !(s.gearLying && s.gear >= 1.0), vhWait = vh > kBTouchVh;
        if (gearWait) vzCmd = (std::max)(vzCmd, Cl(0.15 * (kBGearAlt - s.alt), -rate, 0.0));
        if (vhWait) vzCmd = (std::max)(vzCmd, Cl(0.15 * (kBLoAlt - s.alt), -rate, 0.0));
        phase_ = !ready ? 0 : (gearWait && s.alt < kBGearAlt + 5.0) || (vhWait && s.alt < kBLoAlt + 5.0) ? 2 : s.alt > kBLoAlt + 10.0 ? 3 : 4;
        if (!gearAsked_ && gearWait) { gearAsked_ = true; Note(L"шасси лёжа: выпуск (12 с)", kOk); }
    } else {
        vzCmd = Cl(0.15 * (altT_ - s.alt), -kBClimb, kBClimb);
        phase_ = ready ? 1 : 0;
    }
    const double ez = vzCmd - s.vz;
    double az = Cl(0.6 * ez + iVz_, -3.0, 3.0);
    // ---- horizontal: the speed along the heading, the side drift ----
    const double vT = mode_ == kLand ? 0.0 : (std::min)(spdT_, s.mach > 0.7 ? s.vF : kBSpdMax);
    const double ex = vT - s.vF;
    const double ax = Cl(0.2 * ex + iAx_, -kBAccMax, kBAccMax);
    const double as = Cl(-0.3 * s.vS, -g * std::tan(kBBankMax * kD2R), g * std::tan(kBBankMax * kD2R));
    c.bank = std::atan2(as, g) * kR2D;
    c.vzCmd = vzCmd; c.axCmd = ax;
    // ---- the split: the horizon's forces into the ship's axes (pitch, bank), the pods up + the tilt, the march the rest ----
    const double Fz = m * (g + az), Fx = m * ax;
    const double th = s.pitch * kD2R, cb = (std::max)(0.5, std::cos(s.bank * kD2R));
    const double fwdS = Fx * std::cos(th) + Fz * std::sin(th), upS = (std::max)(0.0, (-Fx * std::sin(th) + Fz * std::cos(th)) / cb);
    const bool march = s.marchMax > 0.02 * W;
    const double tiltF = march ? kBTiltFwd : kBTiltFwdNoMarch;
    const double fp = Cl(fwdS, -upS * std::tan(kBTiltAft * kD2R), upS * std::tan(tiltF * kD2R));
    double want = CupFor(s, 90.0 - std::atan2(fp, (std::max)(upS, 1.0)) * kR2D);
    if (!ready) cupCmd_ = std::isfinite(VertAngle()) ? VertAngle() : cupCmd_;          // coming out: the cups to the vertical
    else cupCmd_ += Cl(want - cupCmd_, -kBCupRate * dt, kBCupRate * dt);
    c.podAngle = Cl(cupCmd_, 0.0, 180.0);
    // the lever from the cups' angle now (the lift first), the march for what the cups' tilt does not give forward
    double f0, u0; PodDir(s, s.podAngle, f0, u0);
    double lv = ready && u0 > 0.05 && s.podMax > 0 ? upS / (s.podMax * u0) : 0.0;
    c.satUp = lv >= s.podLvMax - 1e-6;
    lv = Cl(lv, 0.0, s.podLvMax);
    c.podLv = lv;
    const double podFwd = lv * s.podMax * f0;
    c.march = march ? Cl((fwdS - podFwd) / s.marchMax, 0.0, 1.0) : 0.0;
    c.Fz = Fz; c.Fx = Fx; c.FpUp = lv * s.podMax * u0; c.FpFwd = podFwd; c.Fm = c.march * s.marchMax;
    // the integrals (not wound up against a ceiling, nor while the pods come out)
    if (ready && !(c.satUp && ez > 0)) iVz_ = Cl(iVz_ + 0.08 * ez * dt, -1.5, 1.5);
    if (ready) iAx_ = Cl(iAx_ + 0.02 * ex * dt, -0.5, 0.5);
    if (c.satUp && ez > 0.5 && ready) { if (!warnLift_) { warnLift_ = true; Note(L"тяга выдвижных блоков на пределе: " + Force(LiftMax()) + L" при весе " + Force(W), kBad); } }
    else if (!c.satUp) warnLift_ = false;
}

}  // namespace tantra::guidance
