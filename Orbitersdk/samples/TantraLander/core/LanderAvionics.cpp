#include "LanderAvionics.h"
#include "LanderSpec.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tantra::lander {
using namespace spec;

const char* ModeRu(int m) {
    static const char* k[] = {"висение", "переход", "полёт", "вход", "баллистический спуск днищем", "посадка на полосу",
                              "вертикальная посадка", "стыковка/захват в ангаре"};
    return (m >= 0 && m < kModeCount) ? k[m] : "?";
}

// what each contour flies by itself: the photonic one everything; the analog one the hold modes (it holds α 90° in the
// ballistic descent - the json's «аналоговый автомат»); the mechanics none (the pilot's levers, the hydraulic interlock)
bool Avionics::Allowed(int mode, int contour) {
    if (contour == kPhoton) return true;
    if (contour == kAnalog) return mode == kHover || mode == kTransition || mode == kFlight || mode == kEntry ||
                                   mode == kBallistic || mode == kVertLand;
    return false;
}

static bool Failed(const Unit& u) {
    return u.stage == kStStall || u.stage == kStQuench || u.stage == kStBroken;
}

void Avionics::Contours(const AvIn& in) {
    if (in.shockCabin >= kShockPhoton && !photonBroken_) { photonBroken_ = true; photonWhy_ = "удар: разъюстировка оптического стола"; }
    if (in.shockCabin >= kShockAnalog && !analogBroken_) { analogBroken_ = true; analogWhy_ = "удар: обрыв аналоговых плат"; }
    photon = photonBroken_ ? Node{kFault, photonWhy_} : !in.photonPower ? Node{kOff, "нет питания: снят с накопителя"} : Node{kRun, "работа"};
    analog = analogBroken_ ? Node{kFault, analogWhy_} : !in.analogPower ? Node{kOff, "батарея аналогового контура пуста"} : Node{kReady, "горячий резерв"};
    mech = Node{kReady, "гидроаккумулятор, рычаги"};
    const int was = active;
    if (photon.state == kRun) active = kPhoton;
    else if (!analogBroken_ && in.analogPower) active = kAnalog;
    else active = kMech;
    if (active == kAnalog) analog = Node{kRun, photon.state == kFault ? "ведёт: фотонный контур отказал" : "ведёт: фотонный контур без питания"};
    if (active == kMech) mech = Node{kRun, "ведёт: ручное управление, гидравлическая блокировка пар"};
    if (was != active) for (int i = 0; i < kUnits; ++i) failT_[i] = 0;   // the next contour re-reads the units
}

// the hover: the vertical channel, roll and pitch by throttling the cups; a failed cup (seen after the contour's latency)
// puts out its centrally symmetric partner, the remaining four hold on the nominal; a restarted cup brings its partner back
void Avionics::Hover(const AvIn& in, AvOut& out, bool land) {
    const Propulsion& P = *in.prop;
    const Flight& f = in.f;
    const double lat = Latency();
    for (int i = kMarch; i < kUnits; ++i) {
        const bool bad = Failed(P.u[i]);
        if (bad) { failT_[i] += in.dt; okT_[i] = 0; } else { okT_[i] += in.dt; failT_[i] = 0; }
        if (bad && failT_[i] >= lat) seenFail_[i] = true;
        if (!bad && okT_[i] >= lat && P.u[i].stage == kStRun) seenFail_[i] = false;
    }
    // a failed cup puts out its centrally symmetric partner (unless the partner has failed itself)
    for (int i = kMarch; i < kUnits; ++i) shed[i] = false;
    for (int i = kMarch; i < kUnits; ++i) { const int o = Propulsion::Opposite(i); if (seenFail_[i] && !seenFail_[o]) shed[o] = true; }
    for (int r = 0; r < 2; ++r) out.deploy[r] = true;
    // the set on the run carries the load; the cups still starting (the field, the trigger) are asked but not counted
    int idx[6]; int n = 0, nStart = 0; int start[6];
    for (int i = kMarch; i < kUnits; ++i) {
        if (seenFail_[i] || shed[i] || P.u[i].broken || !P.RowOut(P.u[i].side)) continue;
        if (P.u[i].stage == kStRun) idx[n++] = i; else start[nStart++] = i;
    }
    // a pair rejoins together: a cup on the run whose partner is still starting idles until the partner runs
    int hold[6]; int nHold = 0;
    for (int k = 0; k < n;) {
        const int o = Propulsion::Opposite(idx[k]);
        bool partnerStarting = false;
        for (int j = 0; j < nStart; ++j) partnerStarting = partnerStarting || start[j] == o;
        if (partnerStarting) { hold[nHold++] = idx[k]; idx[k] = idx[--n]; } else ++k;
    }
    if (active == kMech) {
        for (int i = kMarch; i < kUnits; ++i) {
            if (shed[i] || P.u[i].broken) continue;
            out.thr[i] = std::clamp(in.p.collective + 0.2 * (in.p.roll * P.u[i].x / kLiftX + in.p.pitch * P.u[i].z / kLiftZ[2]), 0.0, 1.0);
        }
        decision = "ручное: рычаг общего шага, гидравлическая блокировка пар";
        return;
    }
    const double gain = active == kPhoton ? 1.0 : 0.7;     // the analog loop is slower: softer gains
    double acc;
    if (land) {
        const double vzT = f.h > 0.3 ? -std::clamp(f.h / 10.0, 0.5, 3.0) : -0.5;
        acc = 1.5 * gain * (vzT - f.vz);
    } else acc = gain * (0.4 * (in.p.hHold - f.h) - 1.2 * f.vz);
    acc = std::clamp(acc, -3.0, 4.0);
    const double Fz = std::max(0.0, f.mass * (f.g + acc) - f.wingLift);
    liftCmd = Fz;
    const double each = Fz / std::max(1, n);
    // the starting cups: the whole share when nothing runs yet (all start together), else an idle until they join in pairs
    for (int k = 0; k < nStart; ++k) out.thr[start[k]] = n == 0 ? std::clamp(Fz / std::max(1, nStart + nHold) / kLiftF, 0.05, 1.0) : 0.05;
    for (int k = 0; k < nHold; ++k) out.thr[hold[k]] = n == 0 ? std::clamp(Fz / std::max(1, nStart + nHold) / kLiftF, 0.05, 1.0) : 0.05;
    // a failed cup that is not destroyed keeps its command: the unit restarts it (the second injector, the field after a
    // quench); it rejoins the set after the contour's latency on the run, and its partner with it
    for (int i = kMarch; i < kUnits; ++i)
        if (seenFail_[i] && !P.u[i].broken) out.thr[i] = std::clamp(each / kLiftF, 0.05, 1.0);
    out.rcs = -f.Iyy * gain * (2.0 * f.yaw + 3.0 * f.yawRate);
    if (n == 0) {
        bool any = false; for (int i = kMarch; i < kUnits; ++i) any = any || !P.u[i].broken;
        decision = nStart > 0 ? "запуск подъёмных чаш" : any ? "подъёмные чаши не работают: попытки перезапуска" : "подъёмных чаш нет: посадка планированием";
        return;
    }
    // roll and pitch: F_i = each + a·x_i + b·z_i with Σ F x = τroll, Σ F z = τpitch (the set is centrally symmetric: Σx = Σz = 0)
    // ЦМ не на центре рядов (перекачка не довозит: конец висения, отказ бака) - ряды делят тягу так, чтобы равнодействующая
    // шла через ЦМ (Σ F x = Fz·Δx); обратная связь по углам - поверх
    const double tr = -f.Ixx * gain * (4.0 * f.roll + 4.0 * f.rollRate) + Fz * (P.XCg() - kXcgHover);
    const double tp = -f.Izz * gain * (4.0 * f.pitch + 4.0 * f.pitchRate);
    double sxx = 0, sxz = 0, szz = 0;
    for (int k = 0; k < n; ++k) { const Unit& u = P.u[idx[k]]; sxx += u.x * u.x; sxz += u.x * u.z; szz += u.z * u.z; }
    const double det = sxx * szz - sxz * sxz;
    double a = 0, b = 0;
    if (std::fabs(det) > 1e-9) { a = (tr * szz - tp * sxz) / det; b = (tp * sxx - tr * sxz) / det; }
    else if (sxx > 0) a = tr / sxx;
    const bool em = in.p.emergency && each > 0.95 * kLiftF;
    const double Fmax = em ? kLiftF * kLiftV / kEmV : kLiftF;
    for (int k = 0; k < n; ++k) {
        const int i = idx[k];
        const double Fi = each + a * P.u[i].x + b * P.u[i].z;
        out.thr[i] = std::clamp(Fi / Fmax, 0.0, 1.0);        // the unit's thr scales its nominal power
        out.em[i] = em;
    }
    bool anyShed = false; for (int i = kMarch; i < kUnits; ++i) anyShed = anyShed || shed[i];
    decision = land ? (f.h <= 0.3 ? "касание: тяга снимается" : "вертикальная посадка")
             : anyShed ? "отказ чаши: противолежащая погашена, висят четыре" : "висение";
    if (land && f.h <= 0.05 && std::fabs(f.vz) < 1.0) for (int k = 0; k < n; ++k) out.thr[idx[k]] = 0.0;
}

void Avionics::Step(const AvIn& in, AvOut& out) {
    out = AvOut{};
    Contours(in);
    const Propulsion& P = *in.prop;
    const Flight& f = in.f;
    const bool automat = Allowed(in.mode, active);
    mode = automat ? Node{kRun, ModeRu(in.mode)} : Node{kLimit, active == kAnalog ? "режим только фотонному контуру: ручное с аналоговым демпфером"
                                                                                  : "ручное управление: механика и гидравлика"};
    decision = "";
    const bool cups = in.mode == kHover || in.mode == kVertLand || in.mode == kTransition;
    if (cups && !HoverAllowed(f.atmosphere, in.crew, in.p.hoverEmergency)) {
        // hover_ru: the hover with the crew - on the bodies without a runway and in an emergency; on Earth the runway
        mode = Node{kLimit, "висение запрещено: тело с атмосферой, экипаж на борту"};
        decision = "висение с экипажем только аварийно: посадка на полосу планированием";
        if (in.mode == kTransition) for (int i = 0; i < kMarch; ++i) out.thr[i] = in.p.march;
        out.noseValve = in.p.nose;
        return;
    }
    switch (in.mode) {
    case kHover: case kVertLand: case kTransition: {
        auto anyShedNow = [&] { for (int i = kMarch; i < kUnits; ++i) if (shed[i]) return true; return false; };
        Hover(in, out, in.mode == kVertLand);
        if (in.mode == kTransition) for (int i = 0; i < kMarch; ++i) out.thr[i] = in.p.march;
        if (in.mode == kTransition && !anyShedNow() && std::strcmp(decision, "висение") == 0) decision = "переход: крыло берёт вес, чаши разгружаются";
        if (in.p.hoverEmergency && in.crew > 0 && f.atmosphere && std::strcmp(decision, "висение") == 0) decision = "аварийное висение с экипажем";
        break;
    }
    case kFlight: {
        // the marches; the axis held by the УВТ: a march out - the other one turns its jet through the CM if the angle
        // atan(x/arm) is within ±15°. 25,4 м: x 2,31 m, arm from the УВТ pivot −10,28 to the CG ~7,0–7,7 m:
        // 17–18° > 15° - one march cannot hold the axis; the automat keeps the good one at idle until the other restarts
        double F[2], sum = 0;
        const double lat = Latency();
        const bool out0 = Failed(P.u[0]), out1 = Failed(P.u[1]);
        marchOutT_ = (out0 || out1) ? marchOutT_ + in.dt : 0.0;
        const double arm = std::fabs(P.u[0].z);
        const bool oneHolds = std::atan(kMarchX / std::max(arm, 1e-6)) * 180.0 / kPi <= kTvcMax;
        for (int i = 0; i < kMarch; ++i) {
            out.thr[i] = in.p.march;
            const Unit& o = P.u[1 - i];
            // the pair runs together: a running march whose partner is out follows its decaying thrust down to idle (5 %)
            // and waits there until the partner runs again
            if (automat && !oneHolds && in.p.march > 0.05 && P.u[i].stage == kStRun && o.stage != kStRun)
                out.thr[i] = std::clamp(o.thrust / o.Fnom, 0.05, in.p.march);
            F[i] = P.u[i].thrust; sum += F[i];
        }
        const double imb = -(F[0] * P.u[0].x + F[1] * P.u[1].x);            // yaw torque of the thrust offsets
        const double want = automat ? -f.Iyy * (1.5 * f.yaw + 2.0 * f.yawRate) : in.p.yaw * 2e6;
        double s = sum > 1.0 ? (want - imb) / (sum * arm) : 0.0;
        s = std::clamp(s, -std::sin(kTvcMax * kPi / 180.0), std::sin(kTvcMax * kPi / 180.0));
        const double d = std::asin(s) * 180.0 / kPi;
        for (int i = 0; i < kMarch; ++i) { out.tvcY[i] = d; out.tvcP[i] = automat ? -std::clamp(57.3 * (1.0 * f.pitch + 1.0 * f.pitchRate), -kTvcMax, kTvcMax) : in.p.pitch * kTvcMax; }
        // the afterburner: both marches together (one alone turns the ship), never near «Тантра»
        const bool abNear = in.p.afterburner && f.shipDist < kAfterSafeDist;
        for (int i = 0; i < kMarch; ++i) out.em[i] = in.p.afterburner && !abNear && in.p.march > 0.05;
        if (marchOutT_ > lat) decision = marchOutT_ < 10.0 ? (oneHolds ? "маршевый отказал: второй держит ось через УВТ, перезапуск"
                                                                       : "маршевый отказал: УВТ 15° не проводит струю второго через ЦМ, второй на малом газе до перезапуска")
                                       : f.h < 40e3 || !oneHolds ? "маршевый не перезапустился: возврат планированием" : "после 40 км: продолжение на одном маршевом";
        else decision = abNear ? "форсаж запрещён: рядом «Тантра» (струя)" : (out.em[0] ? "полёт на форсаже" : "полёт");
        break;
    }
    case kEntry:
        // 25,4 м: the glide with all the entry argon in the nose tank (ЦМ −2,51), the flap 15°, the elevons 0 - the trim α 32,8°
        out.flap = kGlideFlap;
        // the elevon sign as the mesh and the Newton tables have it: negative - the trailing edge up, nose up (−25° trims higher α)
        out.elevon = automat ? std::clamp(57.3 * (2.0 * (f.alpha - kGlideAlpha * kPi / 180.0) + 1.0 * f.alphaRate), -25.0, 25.0) : -in.p.pitch * 25.0;
        decision = P.XCg() < kXBallNeedE0 ? "вход: ЦМ позади, перекачка в носовой не завершена" : "планирующий вход, щиток 15°, атака 33°";
        break;
    case kBallistic: {
        // 25,4 м (balance): no trim at α 90°; with the entry argon in the nose tank (ЦМ −2,51) the elevons −25° trim the
        // belly at α 58,3°; all of it in the aft tank (ЦМ −2,94) still trims (α 87°); aft of −2,97 - no stable trim at −25°
        const double trim = kBallAlphaE25 * kPi / 180.0;
        const double err = f.alpha - trim;
        const bool balanced = P.XCg() >= kXBallNeedE25;
        out.elevon = automat ? std::clamp(kBallElevon + 57.3 * (2.0 * err + 1.5 * f.alphaRate), -25.0, 25.0) : kBallElevon - in.p.pitch * 25.0;
        const bool noseFirst = std::fabs(err) > 45.0 * kPi / 180.0;
        out.noseValve = in.p.nose || (automat && noseFirst);
        decision = out.noseValve ? (in.p.nose ? "вдув в нос по команде" : "идёт носом: вдув аргона в нос, элевоны на возврат к атака 58°")
                 : !balanced ? "ЦМ позади -2,97 м: устойчивой балансировки нет - аргон в носовой бак"
                             : "баллистический спуск днищем, атака 58°, элевоны -25°";
        break;
    }
    case kRunway:
        out.flap = kRunwayFlap;
        out.elevon = automat ? std::clamp(57.3 * (1.0 * f.pitch + 1.0 * f.pitchRate), -25.0, 25.0) : -in.p.pitch * 25.0;
        decision = automat ? "автомат посадки на полосу: глиссада атака 18°, ~105 м/с без тяги, щиток 10°" : "посадка на полосу вручную";
        break;
    case kDock:
        Hover(in, out, false);
        decision = automat ? "стыковка/захват: точное висение по маякам ангара" : "захват вручную";
        break;
    }
    if (!automat && in.mode != kHover && in.mode != kVertLand && in.mode != kTransition && in.mode != kDock) {
        if (std::fabs(in.p.yaw) > 0) out.rcs = in.p.yaw * 4.0 * kRcsF * 3.0;
    }
    out.noseValve = out.noseValve || in.p.nose;
    out.frontSwing = std::clamp(in.p.pitch * kFrontSwing, -kFrontSwing, kFrontSwing);
    const WingCfg w = WingConfig(in.mode, f.mach);
    out.tip = w.tip; out.gearDown = w.gearDown;
}

WingCfg WingConfig(int mode, double mach) {
    switch (mode) {
    case kHover: case kVertLand: case kTransition: return {0.0, mode != kTransition};
    case kRunway: return {0.0, true};
    case kFlight: return {mach < 1.0 ? 0.0 : 60.0, false};
    case kEntry: return {60.0, false};
    case kBallistic: return {90.0, false};
    case kDock: default: return {90.0, false};
    }
}

}  // namespace tantra::lander
