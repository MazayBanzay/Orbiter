// Tantra core: the planetary power plant - see Plant.h.
#include "Plant.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace tantra::plant {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMu0 = 4e-7 * kPi, kSigma = 5.670e-8;
constexpr double kEtaN = 0.9;                              // magnetic nozzle: charged products -> jet
constexpr double kAreaMarch = kPi * 2.2 * 2.2;             // m^2 (R 2.2 m)
constexpr double kBRupture = Plant::kBNom * 1.41421356;    // twice the design stress: the winding tears
constexpr double kSternFraction = 0.1411;                  // radiation from the burn 15 m aft that hits the stern face
constexpr double kRadArea = 2700.0, kRadEps = 0.9;         // the crests and the fin
const double kCool[3] = {1.5e6, 6.0e6, 0.0};               // J/kg the reaction mass carries away through the jacket
const double kMassV[3] = {3.0e4, 3.0e5, 7.0e6};
constexpr size_t kJournal = 12;

struct Cup { double F, v, Pf, Pjet; bool field; };
Cup CupAt(double B, double PfMax, int mass, double chi, double vProducts) {
    const double Ffield = Plant::FieldThrust(B, kAreaMarch);
    double v = mass == kProducts ? vProducts : kMassV[mass];
    if (mass == kIron) v = std::max(kMassV[kArgon], std::min(kMassV[kIron], 2.0 * kEtaN * PfMax * (1.0 - chi) / std::max(1.0, Ffield)));
    Cup c{2.0 * kEtaN * PfMax * (1.0 - chi) / v, v, PfMax, 0.0, false};
    if (c.F >= Ffield * 0.999) { c.F = std::min(c.F, Ffield); c.Pf = c.F * v / (2.0 * kEtaN * (1.0 - chi)); c.field = true; }
    c.Pjet = c.F * v / 2.0;
    return c;
}
std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

}  // namespace

double Plant::FieldThrust(double B, double area) { return B * B / (2.0 * kMu0) * area; }
double Plant::BRupture() { return kBRupture; }

void Config::Defaults() {
    fails.clear();
    auto add = [this](int cause, double w, int eff, double val, double heat, bool q, const char* ru, const char* en) {
        Failure f; f.cause = cause; f.weight = w; f.effect = eff; f.value = val; f.heatJump = heat; f.quench = q; f.ru = ru; f.en = en;
        fails.push_back(f);
    };
    add(kField, 5, kQuench, 1.0, 0, true, "Срыв поля (потеря сверхпроводимости) — тяга ноль, охлаждение", "Field quench - no thrust while the windings cool");
    add(kField, 3, kCoils, 0.7, 0, true, "Сгорела секция обмотки — тяга -30 % до станции", "A winding section burnt - thrust -30 % until a station");
    add(kField, 1, kCoilCap, 0.5, 0, true, "Лопнула опора обмотки — поле не выше 12 Тл до станции", "A winding support cracked - field capped until a station");
    add(kPower, 4, kDip, 1.5, 0, false, "Серия пропусков поджига — провал тяги", "A series of trigger misfires - thrust dips");
    add(kPower, 3, kDrivers, 0.75, 0, false, "Сгорел модуль ионного триггера — мощность -25 % до станции", "An ion trigger module burnt - power -25 % until a station");
    add(kPower, 2, kJacket, 0.7, 120, false, "Плазма прорвала поле и ударила в чашу — охлаждение -30 %", "Plasma broke through the field into the cup - cooling -30 %");
    add(kHeat, 3, kRadiators, 0.8, 0, false, "Покоробилась панель гребня — сброс тепла -20 %", "A crest panel warped - heat rejection -20 %");
    add(kHeat, 3, kJacket, 0.7, 80, false, "Лопнул канал рубашки — охлаждение -30 %", "A jacket channel burst - cooling -30 %");
    add(kHeat, 2, kCryo, 0.6, 0, false, "Отказ линии криогеники — обмотка теплеет", "A cryo line failed - the windings warm up");
    add(kHeat, 1, kQuench, 1.0, 0, true, "Срыв поля от нагрева обмотки", "Field quench - the windings overheated");
}

void Config::Parse(const std::string& text) {
    std::istringstream in(text);
    std::string line, section;
    std::vector<Failure> list;
    auto num = [](const std::string& v) { return std::strtod(v.c_str(), nullptr); };
    while (std::getline(in, line)) {
        std::string l = Trim(line);
        if (l.empty() || l[0] == ';') continue;
        {   // a comment after ';' - except in the message texts (ru / en keep the whole line)
            const size_t eq0 = l.find('=');
            const std::string k0 = eq0 == std::string::npos ? std::string() : Trim(l.substr(0, eq0));
            const size_t sc = l.find(';');
            if (k0 != "ru" && k0 != "en" && sc != std::string::npos) l = Trim(l.substr(0, sc));
        }
        if (l[0] == '[') {
            section = Trim(l.substr(1, l.find(']') - 1));
            if (section == "failure") list.emplace_back();
            continue;
        }
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = Trim(l.substr(0, eq)), v = Trim(l.substr(eq + 1));
        if (section == "failure" && !list.empty()) {
            Failure& f = list.back();
            if (k == "cause") f.cause = v == "power" ? kPower : v == "heat" ? kHeat : kField;
            else if (k == "weight") f.weight = num(v);
            else if (k == "effect") f.effect = v == "coils" ? kCoils : v == "coil_cap" ? kCoilCap : v == "dip" ? kDip : v == "drivers" ? kDrivers :
                                               v == "jacket" ? kJacket : v == "radiators" ? kRadiators : v == "cryo" ? kCryo : kQuench;
            else if (k == "value") f.value = num(v);
            else if (k == "heat") f.heatJump = num(v);
            else if (k == "quench") f.quench = num(v) != 0.0;
            else if (k == "ru") f.ru = v;
            else if (k == "en") f.en = v;
            continue;
        }
        const double x = num(v);
        if (k == "chi") chi = x; else if (k == "risk0") risk0 = x; else if (k == "risk_k") riskK = x;
        else if (k == "t_safe") tSafe = x; else if (k == "t_boil") tBoil = x; else if (k == "t_soft") tSoft = x;
        else if (k == "t_breach") tBreach = x; else if (k == "t_lost") tLost = x; else if (k == "stern_store") sternStore = x;
        else if (k == "leak") leak = x; else if (k == "reflect_argon") reflect[0] = x; else if (k == "reflect_iron") reflect[1] = x;
        else if (k == "reflect_products") reflect[2] = x; else if (k == "air_iron") airLayer[1] = x; else if (k == "air_products") airLayer[2] = x;
        else if (k == "quench_cool") quenchCool = x; else if (k == "limiter_hot") limiterHot = x;
    }
    fails = list;
    if (fails.empty()) Defaults();
}

void Plant::Log(const std::string& ru, const std::string& en, int level, bool fault) {
    Event e; e.ru = ru; e.en = en; e.level = level; e.bad = fault;
    events_.push_back(e);
    journal_.push_front({now_, ru, en, level});
    while (journal_.size() > kJournal) journal_.pop_back();
}

bool Plant::FieldStep(double dT) {
    const double want = std::round((Bset_ + dT) * 10.0) / 10.0;
    Bset_ = std::max(1.0, std::min(lim_ ? kBNom : kBRupture, want));
    if (lim_ && want > kBNom + 1e-9) {
        heldT_ = now_;
        Log("Ограничитель держит поле 12,1 Тл — снять: ОГРАНИЧИТЕЛЬ дважды", "The limiter holds 12.1 T - remove it: LIMITER twice", 1);
        return false;
    }
    return true;
}
bool Plant::PowerStep(double dPct) {
    const double want = P_ + dPct;
    P_ = std::max(5.0, std::min(lim_ ? 100.0 : 200.0, want));
    if (lim_ && want > 100.0 + 1e-9) {
        heldT_ = now_;
        Log("Ограничитель держит мощность 100 % — снять: ОГРАНИЧИТЕЛЬ дважды", "The limiter holds 100 % power - remove it: LIMITER twice", 1);
        return false;
    }
    return true;
}
void Plant::CycleMass() { massMode_ = massMode_ >= kProducts ? -1 : massMode_ + 1; }
const char* Plant::LimiterPress(double now, bool russian) {
    if (!lim_) {
        lim_ = true; armT_ = -1e9;
        Bset_ = std::min(Bset_, kBNom); P_ = std::min(P_, 100.0);
        Log("Ограничитель включён — АВТОМАТ", "Limiter on - AUTOMATIC", 0);
        return russian ? "Ограничитель включён — АВТОМАТ" : "Limiter on - AUTOMATIC";
    }
    if (now - armT_ > 4.0) {
        armT_ = now;
        Log("Снять ограничитель? Ещё раз в течение 4 с", "Remove the limiter? Press again within 4 s", 1);
        return russian ? "Снять ограничитель? Ещё раз в течение 4 с" : "Remove the limiter? Press again within 4 s";
    }
    lim_ = false; armT_ = -1e9;
    Log("Ограничитель снят — РУЧНОЙ режим", "Limiter removed - MANUAL", 2);
    return russian ? "Ограничитель снят — РУЧНОЙ режим" : "Limiter removed - MANUAL";
}
const char* Plant::StartStop(bool russian) {
    if (stage_ == kStGone) return russian ? "Установка потеряна" : "The plant is lost";
    if (stage_ == kStQuench) {
        if (coilsLost_) return russian ? "Обмотка потеряна — пуск только после станции" : "The windings are lost - no start until a station";
        return russian ? "Обмотка охлаждается — пуск после охлаждения" : "The windings cool - start after that";
    }
    if (stage_ == kStOff) {
        SetStage(kStCryo);
        Log("Пуск: проверка криогеники", "Start: the cryo check", 0);
        return russian ? "Пуск: проверка криогеники" : "Start: the cryo check";
    }
    SetStage(kStOff);
    Log("Останов: поле снимается", "Stop: the field comes down", 0);
    return russian ? "Останов: поле снимается" : "Stop: the field comes down";
}
double Plant::Damage(int effect) const {
    switch (effect) {
        case kCoils: return coils_; case kDrivers: return drivers_; case kJacket: return jacket_;
        case kRadiators: return radiators_; case kCryo: return cryo_; default: return 1.0;
    }
}
void Plant::Fail(const Failure& f) {
    switch (f.effect) {
        case kCoils: coils_ *= f.value; break;
        case kCoilCap: coils_ = std::min(coils_, f.value); break;
        case kDip: dipT_ = f.value; break;
        case kDrivers: drivers_ *= f.value; break;
        case kJacket: jacket_ *= f.value; break;
        case kRadiators: radiators_ *= f.value; break;
        case kCryo: cryo_ *= f.value; break;
        default: break;
    }
    T_ += f.heatJump;
    Log(f.ru, f.en, 2, true);
    if (f.quench || f.effect == kQuench) SetStage(kStQuench);
    if (coils_ < 0.3 && !coilsLost_) {
        coilsLost_ = true; SetStage(kStQuench);
        Log("Обмотка потеряна — маршевая не работает до станции", "The windings are lost - no march until a station", 2, true);
    }
}

Output Plant::Step(double dt, const Env& e, double (*rnd)()) {
    Output o;
    now_ += dt; stageT_ += dt;
    if (dipT_ > 0.0) dipT_ -= dt;
    // the sequence (the field comes up from the cryo check on; a quench or a stop takes it down)
    if (stage_ == kStCryo && stageT_ > 1.0) SetStage(kStFieldUp);
    const double Bcap = (lim_ ? kBNom : kBRupture) * std::sqrt(coils_);
    const bool down = stage_ == kStOff || stage_ == kStQuench || stage_ == kStGone;
    const double Btarget = down ? 0.0 : std::min(Bset_, Bcap);
    const double rate = stage_ == kStQuench || stage_ == kStGone ? 12.0 : 0.8;
    B_ += std::max(-rate * dt, std::min(rate * dt, Btarget - B_));
    if (stage_ == kStFieldUp && std::fabs(B_ - Btarget) < 0.05) SetStage(kStTrigger);
    if (stage_ == kStTrigger && stageT_ > 2.0) SetStage(kStFeed);
    if (stage_ == kStFeed && stageT_ > 0.5) { SetStage(kStRun); Log("Установка на режиме", "The plant is on the run", 0); }
    if (stage_ == kStQuench && !coilsLost_ && stageT_ > cfg_.quenchCool) {
        SetStage(kStOff); Log("Обмотка охлаждена — можно перезапускать", "The windings cool - the plant can be started", 0);
    }
    const bool run = stage_ == kStRun;
    const double B = std::max(0.01, B_);
    // the reaction mass
    int mass = massMode_ >= 0 ? massMode_ : (e.air ? kArgon : kIron);
    if (lim_ && e.air && mass != kArgon) {
        if (!refused_) Log(mass == kIron ? "Ограничитель: железо в атмосфере нельзя — аргон" : "Ограничитель: продукты в атмосфере нельзя — аргон",
                           "Limiter: argon only in the air", 1);
        refused_ = true; mass = kArgon;
    } else refused_ = false;
    // power in % of the nominal (what the field holds, or the whole plant where the power is the limit)
    const Cup c0 = CupAt(B, kFusionMax * drivers_, mass, cfg_.chi, productsV_);
    const double Pset = std::min(P_, lim_ ? 100.0 : 200.0);
    const double want = c0.Pf * Pset / 100.0, beta = want / std::max(1.0, c0.Pf);
    double F, Pf = want, v = c0.v, over = 0.0;
    if (beta <= 1.0 + 1e-9) { const Cup c = CupAt(B, want, mass, cfg_.chi, productsV_); F = c.F; Pf = c.Pf; v = c.v; o.limit = c.field ? "поле" : "мощность"; }
    else if (c0.field) {
        const double Freq = 2.0 * kEtaN * want * (1.0 - cfg_.chi) / v;
        F = c0.F + 0.3 * (Freq - c0.F) * (1.0 - 1.0 / beta);
        over = c0.Pjet * cfg_.leak * (beta - 1.0) * (beta - 1.0);
        o.limit = "поле";
    } else { F = c0.F * beta; o.limit = "мощность"; }
    o.fieldThrust = FieldThrust(B, kAreaMarch);
    o.powerThrust = 2.0 * kEtaN * kFusionMax * drivers_ * Pset / 100.0 * (1.0 - cfg_.chi) / v;
    o.beta = beta;
    F *= coils_;
    if (lim_ && T_ > cfg_.tSafe) { const double k = std::max(0.0, 1.0 - (T_ - cfg_.tSafe) / cfg_.limiterHot); F *= k; Pf *= k; o.limit = "тепло"; }
    if (dipT_ > 0.0) F *= 0.4;
    o.cupThrust = F;
    if (!run || fuel_ <= 0.0) { F = 0.0; Pf = 0.0; over = 0.0; }
    o.maxThrust = F; o.exhaust = v; o.mass = mass;
    // heat at the level Orbiter runs the cup at
    const double L = std::max(0.0, std::min(1.0, e.level));
    const double PfL = Pf * L, Pjet = F * L * v / 2.0, mdot = F * L / v;
    o.level = L; o.fusion = PfL; o.thrust = F * L; o.mdot = mdot; o.jetPower = Pjet;
    o.sources[0] = cfg_.chi * PfL * kSternFraction;
    o.sources[1] = over * L;
    if (e.sternH >= 0.0) { const double f = 20.0 / (20.0 + e.sternH); o.sources[2] = Pjet * cfg_.reflect[mass] * f * f; }
    if (e.air) o.sources[3] = Pjet * cfg_.airLayer[mass] * std::min(2.0, e.rho / 1.225);
    if (T_ > cfg_.tBreach) o.sources[4] = 0.004 * Pjet * std::min(1.0, (T_ - cfg_.tBreach) / 300.0);
    o.heatIn = o.sources[0] + o.sources[1] + o.sources[2] + o.sources[3] + o.sources[4];
    const double boil = T_ < cfg_.tBoil ? 1.0 : std::max(0.3, 1.0 - (T_ - cfg_.tBoil) / 500.0);
    o.regen = mdot * kCool[mass] * boil * jacket_;
    const double Tr = std::min(T_, 1800.0);
    o.radiated = kRadEps * kSigma * Tr * Tr * Tr * Tr * kRadArea * radiators_ * (e.crestsOut ? 1.0 : 0.1);
    o.cooling = o.regen + o.radiated;
    const double C = cfg_.sternStore / 1400.0;
    T_ = std::max(290.0, T_ + (o.heatIn - o.cooling) * dt / C);
    if (run && fuel_ > 0.0) {
        fuel_ = std::max(0.0, fuel_ - PfL / kEFus * dt);
        if (fuel_ <= 0.0) Log("Топливо p-11B кончилось — подача капсул остановлена", "Out of p-11B fuel - the capsule feed stopped", 2, true);
    }
    const double marks[4] = {cfg_.tSafe, cfg_.tBoil, cfg_.tSoft, cfg_.tBreach};
    static const char* ru[4] = {"Корма выше безопасной (800 К): обмотка теплеет", "Кипение в рубашке (1100 К): отвод тепла падает",
                                "Композит кормы размягчается (1400 К)", "Рубашка чаши прорвана (1700 К): плазма на чашу — нагрев разгоняется"};
    static const char* en[4] = {"Stern over its safe temperature: the windings warm", "Boiling in the jacket: cooling falls",
                                "The stern composite softens", "The cup jacket breached: plasma on the cup - the heating runs away"};
    for (int i = 0; i < 4; ++i) {
        if (T_ > marks[i] && !crossed_[i]) { crossed_[i] = true; Log(ru[i], en[i], i > 0 ? 2 : 1, i > 0); }
        if (T_ < marks[i] - 50.0) crossed_[i] = false;
    }
    if (!lost_ && T_ > cfg_.tLost) {
        lost_ = true; SetStage(kStGone);
        Log("КОРМА ПРОГОРЕЛА — КОРАБЛЬ ПОТЕРЯН", "THE STERN BURNT THROUGH - THE SHIP IS LOST", 2, true);
    }
    // the windings: warmer with the field over the nominal, with a hot stern, a damaged cryo line, while the field moves
    const double coilTarget = 20.0 + 6.0 * std::max(0.0, (B_ / kBNom) * (B_ / kBNom) - 1.0) + std::max(0.0, T_ - cfg_.tSafe) / 60.0 +
                              (1.0 - cryo_) * 12.0 + 0.3 * std::fabs(Btarget - B_);
    coilT_ += (coilTarget - coilT_) * std::min(1.0, dt / 8.0);
    // excesses and the random failures
    o.excess[kField] = std::max(0.0, (B_ / kBNom) * (B_ / kBNom) - 1.0) + std::max(0.0, (coilT_ - 23.0) / 12.0);
    o.excess[kPower] = run && L > 0.0 && beta > 1.0 ? beta - 1.0 : 0.0;
    o.excess[kHeat] = std::max(0.0, (T_ - cfg_.tSafe) / cfg_.tSafe);
    for (int c = 0; c < kCauseCount; ++c)
        o.riskPerMin[c] = o.excess[c] > 0.0 ? std::min(5.0, cfg_.risk0 * (std::exp(cfg_.riskK * o.excess[c]) - 1.0)) : 0.0;
    if (rnd && B_ > 1.0 && stage_ != kStQuench && !lost_) {
        for (int c = 0; c < kCauseCount; ++c) {
            if (o.riskPerMin[c] <= 0.0 || rnd() >= o.riskPerMin[c] / 60.0 * dt) continue;
            double tot = 0.0;
            for (const Failure& f : cfg_.fails) if (f.cause == c) tot += f.weight;
            if (tot <= 0.0) continue;
            double r = rnd() * tot;
            for (const Failure& f : cfg_.fails) {
                if (f.cause != c) continue;
                r -= f.weight;
                if (r <= 0.0) { Fail(f); break; }
            }
        }
    }
    // the trend for the screen: every 0.25 s, the last minute
    if (dt > 0.0 && (trend_.empty() || now_ - trend_.back().t >= 0.25)) {
        trend_.push_back({now_, o.thrust, B_, T_, e.noseT});
        while (!trend_.empty() && now_ - trend_.front().t > 60.0) trend_.pop_front();
    }
    return o;
}

std::string Plant::Save() const {
    char b[256];
    std::snprintf(b, sizeof b, "%.1f %.0f %d %d %.1f %.2f %.3f %.3f %.3f %.3f %.3f %d %d %.0f", Bset_, P_, lim_ ? 1 : 0, massMode_, T_, coilT_,
                  coils_, drivers_, jacket_, radiators_, cryo_, lost_ ? 1 : 0, stage_, fuel_);
    return b;
}
void Plant::Load(const std::string& s) {
    int lim = 1, lost = 0, stage = -1;
    double fuel = kFuelFull;
    std::sscanf(s.c_str(), "%lf %lf %d %d %lf %lf %lf %lf %lf %lf %lf %d %d %lf", &Bset_, &P_, &lim, &massMode_, &T_, &coilT_,
                &coils_, &drivers_, &jacket_, &radiators_, &cryo_, &lost, &stage, &fuel);
    lim_ = lim != 0; lost_ = lost != 0; fuel_ = std::max(0.0, fuel);
    coilsLost_ = coils_ < 0.3;
    if (stage < kStOff || stage > kStGone) stage = kStRun;                 // an older scenario: the plant was on the run
    if (lost_) stage = kStGone; else if (coilsLost_) stage = kStQuench; else if (stage == kStGone) stage = kStOff;
    if (stage == kStCryo || stage == kStFieldUp || stage == kStTrigger || stage == kStFeed) stage = kStRun;   // the sequence had run its course
    SetStage(stage);
    const double Bcap = (lim_ ? kBNom : kBRupture) * std::sqrt(coils_);
    B_ = stage_ == kStRun ? std::min(Bset_, Bcap) : 0.0;
}
void Plant::Repair() {
    coils_ = drivers_ = jacket_ = radiators_ = cryo_ = 1.0; T_ = 300.0; coilT_ = 20.0; lost_ = false; coilsLost_ = false;
    if (stage_ == kStQuench || stage_ == kStGone) SetStage(kStOff);
}

}  // namespace tantra::plant
