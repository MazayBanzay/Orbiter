// МПУ / Э.МПУ - the deck modules on the TVChassis (Orbitersdk\samples\TVehicles\TVChassis.h): the open platform with a
// standing post (МПУ) and the crew cabin with the thrust module (Э.МПУ, Variant = EMPU in the config). Here: their mass, mesh,
// their interiors for OrbiterCrew (the deck, the post, the cabin, the seats, the yoke) and who drives.
#include "TVChassis.h"
#include "EmpuGeo.h"
#include "TVThermal.h"
#include "TantraGlyphs.h"   // the glyph atlas (Cyrillic): D3D9 system fonts draw only the Western charset on a surface

namespace {

class MPU : public TVChassis {
public:
    MPU(OBJHANDLE h, int fm) : TVChassis(h, fm) {}

protected:
    void ModuleCaps(FILEHANDLE cfg) override {
        char var[32] = "";
        cab_ = cfg && oapiReadItem_string(cfg, const_cast<char*>("Variant"), var) && !_stricmp(var, "EMPU");
        bz0_ = kBodyZ0; bz1_ = kBodyZ1; btop_ = cab_ ? 2.85 : kBodyTop;
        char lg[16] = "";
        if (cfg && oapiReadItem_string(cfg, const_cast<char*>("Language"), lg)) ru_ = lg[0] == 'r' || lg[0] == 'R';
        if (cab_) LightsInit();
    }


    // ---------------- Э.МПУ: the cabin's air and heat (the habitat and the airlock share one air: the inner door open) ----------------
    // The gas: moles of O2, N2, CO2, H2O in 34 m3; p = nRT/V. The people aboard (their bodies inside) breathe it: per person
    // O2 0.84, CO2 1.0, water vapour 1.8 kg a day and 100 W of heat at rest - scaled by their pulse (OrbiterCrew). Life
    // support: the O2 regulator from the tank (keeps 21 kPa, passive - works without power), N2 make-up (keeps 99 kPa), the
    // CO2 absorber (a fan, powered; its cartridge used up by what it takes), the dehumidifier (powered). Heat: the people,
    // the electronics, the lamps; the hull (multi-layer insulation, UA 15 W/K) to its skin's temperature - the air outside, or in vacuum
    // the Sun and the night sky; the thermostat (21 C): an electric heater up to 4 kW, the coolant loop to the radiators up
    // to 6 kW (pumps 15 % of it). All the powered parts draw from the cells; with the cells empty they stop.
    static constexpr double kCabV = 34.0, kRgas = 8.314, kCabHeatCap = 3.0e5, kHullUA = 15.0, kTset = 294.15;
    static constexpr double kO2TankMol = 1562.0, kN2TankMol = 714.0, kAbsorbMol = 909.0;   // 50 kg O2, 20 kg N2, 40 kg of CO2 taken
    double nO2_ = 296.0, nN2_ = 1096.0, nCO2_ = 0.56, nH2O_ = 16.7, cabT_ = kTset;
    double o2Tank_ = kO2TankMol, n2Tank_ = kN2TankMol, absorb_ = kAbsorbMol;
    double heatW_ = 0.0, coolW_ = 0.0, lifeW_ = 0.0, peopleT_ = 9.0;
    int people_ = 0; double activity_ = 0.0;
    double CabP() const { return (nO2_ + nN2_ + nCO2_ + nH2O_) * kRgas * cabT_ / kCabV; }          // Pa
    double PP(double n) const { return n * kRgas * cabT_ / kCabV; }                                // Pa
    void People() {                                                         // who breathes here: the bodies inside us
        people_ = 0; activity_ = 0.0;
        if (!api_.Ok() || !api_.PersonOfBody || !api_.ShipOf) return;
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
            const int pid = api_.PersonOfBody(oapiGetVesselByIndex(i));
            if (!pid || api_.ShipOf(pid) != GetHandle()) continue;
            OcInfo in{}; if (!api_.Info(pid, &in) || in.state == 2) continue;
            ++people_; activity_ += Clamp(in.pulse > 1.0 ? in.pulse / 70.0 : 1.0, 0.6, 3.0);
        }
    }
    double SkinT() {                                                        // what the hull's insulation faces outside
        OBJHANDLE sun = oapiGetGbodyByIndex(0), ref = GetSurfaceRef(); VECTOR3 sp, me, upG;
        oapiGetGlobalPos(sun, &sp); GetGlobalPos(me); GlobalRot(_V(0, 1, 0), upG);
        const VECTOR3 toSun = sp - me; const double dist = length(toSun);
        const double sinE = dotp(toSun / dist, unit(upG));
        char nm[64] = ""; if (ref) oapiGetObjectName(ref, nm, sizeof nm);
        const tvthermal::Temps tt = tvthermal::SurfaceTemps(nm, sinE);                // the bodies' day / night table
        if (GetAtmPressure() > 100.0) return tt.known ? tt.airT : GetAtmTemperature();
        // vacuum: the white hull's radiative balance (absorbs 0.2 of the sunlight on a third of its surface, radiates at 0.85),
        // the ground below it (half the sky) and the black sky (the rest)
        const double SIG = 5.670e-8, flux = 3.828e26 / (4.0 * PI * dist * dist);
        const double tg = tt.known ? tt.ground : 100.0 + 290.0 * std::pow((std::max)(0.0, sinE), 0.5);
        const double q = (sinE > 0.0 ? 0.2 * flux * 0.3 : 0.0) + 0.5 * 0.95 * SIG * std::pow(tg, 4) + 0.5 * SIG * std::pow(2.7, 4);
        return std::pow(q / (0.85 * SIG), 0.25);
    }
    double ModuleLoadW(double dt) override {
        if (!cab_ || dt <= 0.0) return 0.0;
        if ((peopleT_ += dt) > 1.0) { peopleT_ = 0.0; People(); }
        // sub-steps of at most 5 s (up to 120 of them a call): at a day's time acceleration one step is thousands of seconds,
        // and the thermostat's explicit step (gain 1500 W/K on 3e5 J/K) swung the cabin to infinity - the air gone NaN, the
        // person aboard with it. Every part is now bounded by what there is to move, the heat solved exactly
        const int ns = (int)Clamp(std::ceil(dt / 5.0), 1.0, 120.0);
        const double h = dt / ns, skin = SkinT();
        double wSum = 0.0, hSum = 0.0, cSum = 0.0;
        for (int i = 0; i < ns; ++i) { wSum += CabinStep(h, skin); hSum += heatW_; cSum += coolW_; }
        heatW_ = hSum / ns; coolW_ = cSum / ns; lifeW_ = wSum / ns;
        if (!std::isfinite(cabT_) || !std::isfinite(nO2_) || !std::isfinite(nN2_) || !std::isfinite(nCO2_) || !std::isfinite(nH2O_)) {
            oapiWriteLogV("EMPU %s: the cabin's air went non-finite - reset", GetName());
            nO2_ = 296.0; nN2_ = 1096.0; nCO2_ = 0.56; nH2O_ = 16.7; cabT_ = kTset;
        }
        return CellsSum() > 1.0 ? lifeW_ : 0.0;
    }
    double CabinStep(double dt, double skin) {
        const bool power = CellsSum() > 1.0;
        const double day = 86400.0;
        // breathing
        nO2_ = (std::max)(0.0, nO2_ - activity_ * 26.25 / day * dt);
        nCO2_ += activity_ * 22.7 / day * dt; nH2O_ += activity_ * 100.0 / day * dt;
        Leak(dt);
        // the slow leak of a sealed hull (0.05 kg a day), all gases alike
        const double nAll = nO2_ + nN2_ + nCO2_ + nH2O_, leak = (std::min)(nAll, 1.7 / day * dt * CabP() / 101300.0);
        if (nAll > 0) { const double k = 1.0 - leak / nAll; nO2_ *= k; nN2_ *= k; nCO2_ *= k; nH2O_ *= k; }
        // the O2 regulator (passive) and the N2 make-up
        if (PP(nO2_) < 21000.0 && o2Tank_ > 0) { const double a = (std::min)({0.01 * dt, o2Tank_, (21000.0 - PP(nO2_)) * kCabV / (kRgas * cabT_)}); nO2_ += a; o2Tank_ -= a; }
        if (CabP() < 99000.0 && n2Tank_ > 0) { const double a = (std::min)({0.01 * dt, n2Tank_, (99000.0 - CabP()) * kCabV / (kRgas * cabT_)}); nN2_ += a; n2Tank_ -= a; }
        double w = 0.0;
        if (power) {
            // the CO2 absorber: holds the cabin near 0.3 kPa while the cartridge lasts (never more than there is in the air)
            const double take = (std::max)(0.0, (std::min)({0.003 * dt, absorb_, nCO2_, 2.6e-6 * PP(nCO2_) * dt}));
            nCO2_ -= take; absorb_ -= take; w += 150.0;
            // the dehumidifier: holds the water vapour at 1.2 kPa (~45 % at 21 C)
            if (PP(nH2O_) > 1200.0) { nH2O_ -= (std::min)({0.002 * dt, nH2O_, (PP(nH2O_) - 1200.0) * kCabV / (kRgas * cabT_)}); w += 120.0; }
        }
        // heat: in from the people, the electronics, the lamps; through the hull (UA); the thermostat (gain K, band 21 +-0.5 C,
        // up to 4 kW of heating, 6 kW of cooling). dT/dt = (q - UA T + heat - cool) / C, solved exactly in the regime the cabin
        // is in: it relaxes to that regime's equilibrium with its time constant
        const double lamps = light_ == kLightFull ? 250.0 : light_ == kLightStandby ? 30.0 : 0.0;
        const double q = activity_ * 100.0 + 600.0 + lamps + kHullUA * skin, UA = kHullUA, K = 1500.0, C = kCabHeatCap;
        double Teq = q / UA, tau = C / UA;                                    // no heater, no cooler
        if (power) {
            if (Teq < kTset - 0.5) {                                          // heating
                const double Th = (q + K * (kTset - 0.5)) / (UA + K);
                if (K * (kTset - 0.5 - Th) > 4000.0) Teq = (q + 4000.0) / UA; else { Teq = Th; tau = C / (UA + K); }
            } else if (Teq > kTset + 0.5) {                                   // cooling
                const double Tc = (q + K * (kTset + 0.5)) / (UA + K);
                if (K * (Tc - kTset - 0.5) > 6000.0) Teq = (q - 6000.0) / UA; else { Teq = Tc; tau = C / (UA + K); }
            }
        }
        cabT_ = Teq + (cabT_ - Teq) * std::exp(-dt / tau);
        heatW_ = power ? Clamp(K * (kTset - 0.5 - cabT_), 0.0, 4000.0) : 0.0;
        coolW_ = power ? Clamp(K * (cabT_ - kTset - 0.5), 0.0, 6000.0) : 0.0;
        w += 600.0 + lamps + heatW_ + 0.15 * coolW_;
        return w;
    }
    // the hull's holes: the air out choked (sonic) while the cabin's pressure is over twice the outside's, then by the
    // difference; all the gases alike
    void Leak(double dt) {
        if (hullLeak_ <= 0.0) return;
        const double p = CabP(), p0 = GetAtmPressure();
        if (p <= p0) return;
        const double md = p > 1.9 * p0 ? 1.415e-3 * hullLeak_ * p : 0.6 * hullLeak_ * std::sqrt(2.0 * 1.2 * (p - p0));   // kg/s
        const double nAll = nO2_ + nN2_ + nCO2_ + nH2O_; if (nAll <= 0) return;
        const double k = (std::max)(0.0, 1.0 - md / 0.029 * dt / nAll);
        nO2_ *= k; nN2_ *= k; nCO2_ *= k; nH2O_ *= k;
    }
    // a crash felt inside (the user: from 50 km/h, as real): each person aboard is struck by the cabin at the speed change -
    // a belted one in a seat at about half of it (the belt and the seat take the rest), one standing at all of it; the
    // injuries are OrbiterCrew's (ocImpact, the vehicle's mass as the striker)
    std::set<int> seated_;
    void Occupants(const VECTOR3& nrm, double dv) override {
        if (!cab_ || dv < 50.0 / 3.6 || !api_.Ok() || !api_.PersonOfBody || !api_.ShipOf) return;
        if (!impact_ && api_.dll) impact_ = (ocImpact_t)GetProcAddress(api_.dll, "ocImpact");
        if (!impact_) return;
        VECTOR3 nG; GlobalRot(nrm, nG);
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
            OBJHANDLE h = oapiGetVesselByIndex(i);
            const int pid = api_.PersonOfBody(h);
            if (!pid || api_.ShipOf(pid) != GetHandle()) continue;
            const double v = dv * (seated_.count(pid) ? 0.55 : 1.0);
            VECTOR3 pG; oapiGetGlobalPos(h, &pG);
            const VECTOR3 vS = nG * v;
            impact_(h, &vS, GetMass(), &pG, &nG);
            oapiWriteLogV("EMPU %s: crash %.1f m/s - %s struck at %.1f m/s (%s)", GetName(), dv, oapiGetVesselInterface(h)->GetName(), v,
                          seated_.count(pid) ? "belted" : "standing");
        }
    }
    // the ventilation's sound inside the cabin (XRSound's own recording "Air Conditioning"): the fans always while there is
    // power, louder with the heater or the cooling's work (full at 4 kW); heard only by someone aboard
    static constexpr int kSndHvac = 40;
    bool hvacLoaded_ = false;
    void Hvac() {
        if (!snd_ || !snd_->IsPresent()) return;
        if (!hvacLoaded_) { snd_->LoadWav(kSndHvac, "XRSound\\Default\\Air Conditioning.wav", XRSound::PlaybackType::Global); hvacLoaded_ = true; }
        bool inside = false;
        if (api_.Ok() && api_.PersonOfBody && api_.ShipOf) {
            const OBJHANDLE f = oapiGetFocusObject(); const int pid = f ? api_.PersonOfBody(f) : 0;
            inside = pid && api_.ShipOf(pid) == GetHandle();
        }
        const bool power = CellsSum() > 1.0;
        const double vol = inside && power ? Clamp(0.18 + 0.6 * (heatW_ + coolW_) / 4000.0, 0.0, 0.8) : 0.0;
        if (vol > 0.01) snd_->PlayWav(kSndHvac, true, (float)vol);
        else if (snd_->IsWavPlaying(kSndHvac)) snd_->StopWav(kSndHvac);
    }
    void AirlockPass() {                                                    // through the outer hatch: the airlock cycled, 10 % of its air lost
        const double k = 1.0 - 0.1 * 9.0 / kCabV;
        nO2_ *= k; nN2_ *= k; nCO2_ *= k; nH2O_ *= k;
    }
    bool ModuleLoad(const char* line) override {
        if (std::strncmp(line, "CABIN", 5)) return false;
        std::sscanf(line + 5, "%lf %lf %lf %lf %lf %lf %lf %lf", &nO2_, &nN2_, &nCO2_, &nH2O_, &cabT_, &o2Tank_, &n2Tank_, &absorb_);
        return true;
    }
    void ModuleSave(FILEHANDLE scn) override {
        if (!cab_) return;
        char b[200]; std::snprintf(b, sizeof b, "%.2f %.2f %.3f %.2f %.2f %.1f %.1f %.1f", nO2_, nN2_, nCO2_, nH2O_, cabT_, o2Tank_, n2Tank_, absorb_);
        oapiWriteScenario_string(scn, const_cast<char*>("CABIN"), b);
    }

    // ---------------- Э.МПУ: the cabin's lights - off, standby (dim red, the default), full (white) ----------------
    // Three lamps (the cockpit, the habitat, the airlock), each a red and a white emitter (Orbiter's lights keep the colour
    // they are made with); the ceiling strips' material (LampIn) glows to match. Switched at the switch box on the bulkhead.
    enum { kLightOff = 0, kLightStandby = 1, kLightFull = 2 };
    int light_ = kLightStandby, lightDrawn_ = -1;
    int lightLvl_ = 4;                                                      // the dimmer: 1..5 (4 - as the user liked it)
    LightEmitter* lamp_[3][2] = {};
    LightEmitter* head_[2] = {};
    void LightsInit() {
        const VECTOR3 at[3] = {_V(0.0, kDeckY + 1.75, 2.60), _V(0.0, kDeckY + 1.75, -0.20), _V(0.0, kDeckY + 1.75, -3.40)};
        const COLOUR4 red = {0.8f, 0.07f, 0.02f, 0}, white = {1.0f, 0.96f, 0.88f, 0}, none = {0, 0, 0, 0};
        for (int k = 0; k < 3; ++k) {
            lamp_[k][0] = AddPointLight(at[k], 4.0, 0.0, 0.0, 0.6, red, red, none);       // as the DeltaGlider's cockpit light:
            lamp_[k][1] = AddPointLight(at[k], 4.5, 0.0, 0.0, 0.35, white, white, none);  // the inverse square law
            for (LightEmitter* l : lamp_[k]) l->SetVisibility(LightEmitter::VIS_ALWAYS);
            // their intensity is the dimmer's (LightsApply)
        }
        const COLOUR4 hw = {1.0f, 0.97f, 0.9f, 0}, h0 = {0, 0, 0, 0};
        for (int sgn = -1; sgn <= 1; sgn += 2) {                              // the headlights: 150 m, 25/45 deg cones
            LightEmitter* h = AddSpotLight(_V(sgn * 0.95, 0.27, 4.50), unit(_V(0, -0.08, 1)), 150.0, 1e-3, 0.0, 2e-4, 25 * RAD, 45 * RAD, hw, hw, h0);
            h->SetVisibility(LightEmitter::VIS_ALWAYS); h->Activate(false); head_[sgn < 0 ? 0 : 1] = h;
        }
        lightDrawn_ = -1;
    }
    void LightsApply() {
        for (LightEmitter* h : head_) if (h && h->IsActive() != heads_) h->Activate(heads_);
        if (light_ * 10 + lightLvl_ == lightDrawn_) return;
        for (auto& l : lamp_) { if (l[0]) l[0]->SetIntensity(0.15 * lightLvl_); if (l[1]) l[1]->SetIntensity(0.25 * lightLvl_); }
        for (auto& l : lamp_) { if (l[0]) l[0]->Activate(light_ == kLightStandby); if (l[1]) l[1]->Activate(light_ == kLightFull); }
        if (dev_) {
            MATERIAL m{};
            const float dim = float(0.4 + 0.15 * lightLvl_);
            const float r = (light_ == kLightFull ? 1.0f : light_ == kLightStandby ? 0.55f : 0.08f) * (light_ == kLightOff ? 1.0f : dim);
            const float g = light_ == kLightFull ? 0.97f : light_ == kLightStandby ? 0.10f : 0.08f;
            const float b = light_ == kLightFull ? 0.90f : light_ == kLightStandby ? 0.06f : 0.08f;
            m.diffuse = {r, g, b, 1}; m.ambient = {r, g, b, 1}; m.specular = {0.1f, 0.1f, 0.1f, 1}; m.emissive = light_ == kLightOff ? COLOUR4{0, 0, 0, 1} : COLOUR4{r, g, b, 1}; m.power = 10;
            oapiSetMaterial(dev_, (DWORD)empu::kLampInMat, &m);
        }
        lightDrawn_ = light_ * 10 + lightLvl_;
    }
    enum { kNodeLight = 101, kNodeKit = 102, kActLightStandby = 1, kActLightFull = 2, kActLightOff = 3, kActPatch = 4, kActBrighter = 5, kActDimmer = 6 };
    static inline const VECTOR3 kKit = {-0.66, kDeckY + 1.30, -2.40};          // the emergency kit, left of the inner door
    static inline const VECTOR3 kLightSwitch = {0.65, kDeckY + 1.30, -2.40};   // the switch box, habitat side of the bulkhead
    int NodeCountE() { return ChassisNodeCount() + (cab_ ? 2 : 0); }
    bool NodeE(int i, OcNode* o) {
        if (i < ChassisNodeCount()) return ChassisNode(i, o);
        if (!cab_ || i > ChassisNodeCount() + 1) return false;
        const bool kit = i == ChassisNodeCount() + 1;
        o->id = kit ? kNodeKit : kNodeLight; o->pos = kit ? kKit : kLightSwitch; o->reach = 1.2; o->inside = 1;
        std::snprintf(o->label, sizeof o->label, "%s", kit ? L("аварийный комплект", "emergency kit") : L("свет кабины", "cabin light"));
        return true;
    }
    int ActionsE(int node, int person, OcAction* out, int max) {
        int n = 0;
        if (node == kNodeKit) {                                         // the patches: up to 10 cm2 of holes each, 20 s
            char b[64];
            std::snprintf(b, sizeof b, L("заделать течь (%.1f см²)", "patch the leak (%.1f cm2)"), hullLeak_ * 1e4);
            tvAct(out, n, max, kActPatch, hullLeak_ > 0.0 ? b : L("заделать течь", "patch the leak"), hullLeak_ > 0.0, L("течи нет", "no leak"), 20.0);
            return n;
        }
        if (node != kNodeLight) return ChassisActions(node, person, out, max);
        if (light_ != kLightStandby) tvAct(out, n, max, kActLightStandby, L("дежурный свет", "standby light"), true, nullptr, 0.0);
        if (light_ != kLightFull) tvAct(out, n, max, kActLightFull, L("полный свет", "full light"), true, nullptr, 0.0);
        if (light_ != kLightOff) tvAct(out, n, max, kActLightOff, L("выключить свет", "light off"), true, nullptr, 0.0);
        char lb[48];
        std::snprintf(lb, sizeof lb, L("ярче (%d из 5)", "brighter (%d of 5)"), lightLvl_); tvAct(out, n, max, kActBrighter, lb, lightLvl_ < 5, L("ярче некуда", "at full"), 0.0);
        std::snprintf(lb, sizeof lb, L("тусклее (%d из 5)", "dimmer (%d of 5)"), lightLvl_); tvAct(out, n, max, kActDimmer, lb, lightLvl_ > 1, L("тусклее некуда", "at the lowest"), 0.0);
        return n;
    }
    int BeginE(int node, int act, int person) {
        if (node == kNodeKit) return hullLeak_ > 0.0 ? 1 : 0;
        if (node != kNodeLight) return ChassisBegin(node, act, person);
        if (act == kActBrighter || act == kActDimmer) { lightLvl_ = Clamp(lightLvl_ + (act == kActBrighter ? 1 : -1), 1, 5); return 1; }
        light_ = act == kActLightFull ? kLightFull : act == kActLightOff ? kLightOff : kLightStandby;
        return 1;
    }
    void EndE(int node, int act, int person, int done) {
        if (node == kNodeKit) {
            if (done) { hullLeak_ = (std::max)(0.0, hullLeak_ - 10.0e-4); oapiWriteLogV("EMPU %s: a patch on the hull - leak %.1f cm2 left", GetName(), hullLeak_ * 1e4); }
            return;
        }
        if (node != kNodeLight) ChassisEnd(node, act, person, done);
    }
    double DryMass() const override { return cab_ ? kMassE : kMass; }
    double FuelMass() const override { return cab_ ? kFuelE : 0.0; }
    const char* MeshName() const override { return cab_ ? "MPU\\EMPU" : "MPU\\MPU"; }

    // ---------------- people: the deck, the steps, the post (OrbiterCrew interior, vessel frame) ----------------
    enum { kItemBoard = 1, kItemPost = 2, kItemDown = 3 };
    static MPU* Self(void* c) { return static_cast<MPU*>(c); }

    void CrewInit() override {
        if (!api_.Load() || !api_.RegisterInterior) return;
        fns_ = OcInterior{};
        fns_.Attach = [](void* c) { return Self(c)->att_; };
        fns_.Ground = cab_ ? &MPU::cGroundE : &MPU::cGround;
        fns_.Walls = cab_ ? &MPU::cWallsE : &MPU::cWalls;
        fns_.Gravity = &TVChassis::cGravity;
        fns_.Count = [](void* c) { return Self(c)->ChassisItemCount() + (Self(c)->cab_ ? 2 + kSeatsN : 5); };
        fns_.Item = cab_ ? &MPU::cItemE : &MPU::cItem;
        fns_.Use = cab_ ? &MPU::cUseE : &MPU::cUse;
        if (cab_) fns_.Seat = &MPU::cSeatE;
        api_.RegisterInterior(GetHandle(), &fns_, this);
        registered_ = true;
        if (api_.SetInteriorExt) {
            ext_ = OcInteriorExt{};
            ext_.size = sizeof(OcInteriorExt);
            ext_.Cabin = cab_ ? &MPU::cCabinE : &MPU::cCabin;
            if (cab_) { ext_.Seated = &MPU::cSeatedE; ext_.SeatHands = &MPU::cSeatHandsE; }
            if (!cab_) ext_.SeatGauges = &MPU::cSeatGaugesE;     // Э.МПУ: on the glass instead (the user: «не внизу»)
            ext_.OuterWalls = &TVChassis::cOuterWalls;
            if (cab_) { ext_.Click = &MPU::cClickE; ext_.Ceiling = &MPU::cCeilingE; }
            ext_.NodeCount = [](void* c) { return Self(c)->NodeCountE(); };
            ext_.Node = [](void* c, int i, OcNode* o) { return Self(c)->NodeE(i, o) ? 1 : 0; };
            ext_.Actions = [](void* c, int node, int person, OcAction* out, int max) { return Self(c)->ActionsE(node, person, out, max); };
            ext_.Begin = [](void* c, int node, int act, int person) { return Self(c)->BeginE(node, act, person); };
            ext_.End = [](void* c, int node, int act, int person, int done) { Self(c)->EndE(node, act, person, done); };
            api_.SetInteriorExt(GetHandle(), &ext_);
        }
    }

    static bool OnDeck(const VECTOR3& p) {
        return (std::fabs(p.x) <= kDeckX && p.z >= kDeckZ0 && p.z <= kDeckZ1) || (std::fabs(p.x) <= kPostPlateX && p.z > kDeckZ1 && p.z <= kPostPlateZ1);
    }
    static int cGround(void*, const VECTOR3* p, double stepUp, double* floorY) {
        if (!OnDeck(*p) || p->y + stepUp < kDeckY) return 0;
        *floorY = kDeckY;
        return 1;
    }
    // the deck's coaming all round (the post plate narrower), the pedestal of the post
    static void cWalls(void*, const VECTOR3* from, VECTOR3* to, double radius, double) {
        const double zMax = std::fabs(to->x) <= kPostPlateX - radius ? kPostPlateZ1 - radius : kDeckZ1 - radius;
        to->x = Clamp(to->x, -kDeckX + radius, kDeckX - radius);
        to->z = Clamp(to->z, kDeckZ0 + radius, std::fabs(from->x) <= kPostPlateX - radius ? kPostPlateZ1 - radius : zMax);
        if (std::fabs(to->x) < 0.45 + radius && to->z > 3.85 - radius) to->z = 3.85 - radius;       // the pedestal
    }
    static int cCabin(void* c, const VECTOR3*, OcCabin* out) {                  // open platform: the air outside, or vacuum
        MPU* m = Self(c);
        const double p = m->GetAtmPressure() / 1000.0;
        out->p = p;
        out->T = p > 0.1 ? m->GetAtmTemperature() : 250.0;
        out->ppO2 = 0.0; out->ppCO2 = 0.0; out->doseSvh = 0.0; out->supplied = 0;
        return 1;
    }
    static int cItem(void* c, int i, OcItem* out) {
        MPU* m = Self(c);
        if (i < m->ChassisItemCount()) return m->ChassisItem(i, out) ? 1 : 0;   // the socket, the cell hatches
        i -= m->ChassisItemCount();
        *out = OcItem{};
        if (i <= 2) {
            out->id = kItemBoard; out->kind = OC_AIRLOCK;   // from outside OrbiterCrew offers only entrances (lift, airlock, exit)
            out->dir = _V(1, 0, 0);
            // anywhere round it: OrbiterCrew measures from her feet to this point, so the three points (front, middle,
            // rear) sit deep under the platform - from the ground each reaches ~4.2 m round, from the deck not at all
            out->pos = _V(0.0, -8.0, i == 0 ? 2.6 : i == 1 ? 0.0 : -2.6); out->radius = 8.25;
            std::snprintf(out->label, sizeof out->label, "%s", "подняться на МПУ");
        } else if (i == 3) {
            out->id = kItemPost; out->kind = OC_TERMINAL; out->pos = _V(0.0, kDeckY + 1.0, 3.6); out->dir = _V(0, 0, 1); out->radius = 1.0;
            std::snprintf(out->label, sizeof out->label, "%s", m->driver_ ? "отойти от пульта" : "встать за пульт МПУ");
        } else if (i == 4) {
            out->id = kItemDown; out->kind = OC_EXIT; out->pos = _V(-1.7, kDeckY + 1.0, 2.5); out->dir = _V(-1, 0, 0); out->radius = 0.9;
            std::snprintf(out->label, sizeof out->label, "%s", "сойти с МПУ");
        } else return 0;
        return 1;
    }
    static void cUse(void* c, int id, int personId) {
        MPU* m = Self(c);
        if (id >= kItemSocket) { m->ChassisUse(id, personId); return; }
        OcInfo in{};
        if (!m->api_.Info(personId, &in)) return;
        if (id == kItemBoard) {
            if (in.where != 1 || !in.vessel || !m->api_.EnterShip) return;
            // up onto the deck where she stands: the nearest point of the deck
            VECTOR3 gp, lp;
            oapiGetGlobalPos(in.vessel, &gp);
            m->Global2Local(gp, lp);
            const VECTOR3 at = _V(Clamp(lp.x, -kDeckX + 0.45, kDeckX - 0.45), kDeckY, Clamp(lp.z, kDeckZ0 + 0.45, kDeckZ1 - 0.3));
            const VECTOR3 dir = _V(0, 0, 1);
            m->api_.EnterShip(in.vessel, m->GetHandle(), &at, &dir);
        } else if (id == kItemPost) {
            if (m->driver_ == personId) m->driver_ = 0;
            else if (!m->driver_) m->driver_ = personId;
        } else if (id == kItemDown) {
            if (m->driver_ == personId) m->driver_ = 0;
            OBJHANDLE ref = m->GetSurfaceRef();
            if (!ref || !m->api_.ExitTo) return;
            VECTOR3 gp;
            m->Local2Global(kStepFoot, gp);
            double lng, lat, rad, hdg;
            oapiGlobalToEqu(ref, gp, &lng, &lat, &rad);
            oapiGetHeading(m->GetHandle(), &hdg);
            VESSELSTATUS2 vs;
            std::memset(&vs, 0, sizeof vs);
            vs.version = 2; vs.rbody = ref; vs.status = 1; vs.arot.x = 10.0;     // Orbiter stands the body on its own points
            vs.surf_lng = lng; vs.surf_lat = lat; vs.surf_hdg = hdg - PI05;
            std::string name = in.name;
            for (char& ch : name) if (ch == ' ') ch = '_';
            m->api_.ExitTo(personId, name.c_str(), &vs);
        }
    }

    // ---------------- Э.МПУ «C»: the habitat and the stern airlock (vessel frame; mesh: build_mpu.py empu_c) ----------------
    // The floor is the deck. Rooms: the habitat (the cockpit at the console, bunks on the right, the table, benches and the
    // galley on the left) and the airlock behind the bulkhead, joined by the inner door; the outer door in the stern.
    struct Box { double x0, x1, z0, z1; };
    static constexpr Box kHab = {-1.38, 1.38, -2.45, 3.60}, kLock = {-1.10, 1.10, -4.16, -2.55};
    // the inner door (0.90 x 1.80 m): a passage through the bulkhead that reaches well into both rooms, so walking from one
    // into the other never meets a strip that is in neither (the rooms are taken inset by her radius, the passage is not)
    static constexpr Box kDoor = {-0.45, 0.45, -2.95, -2.05};
    static constexpr Box kSolid[] = {
        {-0.92, -0.38, 2.92, 3.46}, {0.38, 0.92, 2.92, 3.46},        // the two seats at the console
        {0.78, 1.38, -2.10, 2.30},                                   // the bunks (right)
        {-1.38, -0.80, 1.40, 2.20}, {-1.38, -0.80, -0.50, 0.30},     // the benches (left)
        {-1.38, -0.75, 0.50, 1.20},                                  // the table
        {-1.38, -0.85, -2.35, -1.00},                                // the galley
        {0.78, 1.38, -4.05, -2.90}};                                 // the two suits on their stands along the airlock's right wall
    static constexpr double kSolidTop[] = {1.32, 1.32, 1.42, 0.48, 0.48, 0.76, 1.80, 1.75};   // their tops above the floor, m
    // seats: hips (x, z), their height above the floor, facing along z; the driver's first
    struct SeatE { double x, z, hips, fz; const char* label; };
    static constexpr SeatE kSeatsE[] = {{-0.65, 3.15, 0.72, 1.0, "кресло водителя"}, {0.65, 3.15, 0.72, 1.0, "кресло штурмана"},
                                        {-1.06, 1.80, 0.57, -1.0, "скамья у стола"}, {-1.06, -0.10, 0.57, 1.0, "скамья у стола"}};
    static constexpr int kSeatsN = 4;
    static bool In(const Box& b, double x, double z, double r) { return x > b.x0 + r && x < b.x1 - r && z > b.z0 + r && z < b.z1 - r; }
    static bool Free(double x, double z, double r, double y = -1e9) {        // inside a room or the doorway, outside the furniture
        if (!(In(kHab, x, z, r) || In(kLock, x, z, r) || In(kDoor, x, z, 0.0))) return false;
        for (size_t i = 0; i < std::size(kSolid); ++i) {
            const Box& b = kSolid[i];
            if (y > kDeckY + kSolidTop[i]) continue;                         // over it (the follow camera at head height)
            if (x > b.x0 - r && x < b.x1 + r && z > b.z0 - r && z < b.z1 + r) return false;
        }
        return true;
    }
    static int cGroundE(void*, const VECTOR3* p, double stepUp, double* floorY) {
        const bool inside = In(kHab, p->x, p->z, -0.05) || In(kLock, p->x, p->z, -0.05) || In(kDoor, p->x, p->z, -0.05);
        if (!inside || p->y + stepUp < kDeckY) return 0;
        *floorY = kDeckY;
        return 1;
    }
    // walls and furniture: the step is taken if it ends free; else slid along x or z; else stopped
    // how deep a person of this radius stands in the furniture (0: clear)
    static double Overlap(double x, double z, double r) {
        double d = 0.0;
        for (const Box& b : kSolid) d = (std::max)(d, (std::min)((std::min)(x - (b.x0 - r), b.x1 + r - x), (std::min)(z - (b.z0 - r), b.z1 + r - z)));
        return d;
    }
    static void cWallsE(void*, const VECTOR3* from, VECTOR3* to, double radius, double) {
        const double y = to->y;
        if (Free(to->x, to->z, radius, y)) return;
        // risen from a seat she may stand in it (or between two): any step that takes her out of it is let through
        const bool inRoom = In(kHab, to->x, to->z, radius) || In(kLock, to->x, to->z, radius) || In(kDoor, to->x, to->z, 0.0);
        if (inRoom && Overlap(to->x, to->z, radius) < Overlap(from->x, from->z, radius) - 1e-4) return;
        if (Free(to->x, from->z, radius, y)) { to->z = from->z; return; }
        if (Free(from->x, to->z, radius, y)) { to->x = from->x; return; }
        to->x = from->x; to->z = from->z;
    }
    // the ceiling over a point (interior frame = the vessel's): 2.48 in the habitat and the airlock, falling to 2.3 along the
    // nose (z 3.05 -> 3.85); lower towards the walls by the lining's top chamfers (45 deg from |x| 0.90)
    static double cCeilingE(void*, const VECTOR3* at) {
        double y = 2.48;
        if (at->z > 3.05) y -= 0.18 * Clamp((at->z - 3.05) / 0.80, 0.0, 1.0);
        const double ax = std::fabs(at->x);
        if (ax > 0.90) y -= ax - 0.90;
        return y;
    }
    static int cCabinE(void* c, const VECTOR3*, OcCabin* out) {                // the cabin's air as it is (CabinAir below)
        const MPU* m = Self(c);
        out->p = m->CabP() * 1e-3; out->T = m->cabT_; out->ppO2 = m->PP(m->nO2_) * 1e-3; out->ppCO2 = m->PP(m->nCO2_) * 1e-3;
        out->doseSvh = 0.0; out->supplied = 1;
        return 1;
    }
    enum { kItemHatchOut = 20, kItemHatchIn = 21, kItemSeat0 = 30 };
    // the driver's row on the screen (OrbiterCrew draws it in the helmet display's letters, SEAT_HUD.md): Э.МПУ - the driver's
    // seat; МПУ - the standing post (seatId -1); the other seats: none
    static int cSeatGaugesE(void* c, int seatId, int, OcGauge* out, int max) {
        MPU* m = Self(c);
        if (seatId != (m->cab_ ? int(kItemSeat0) : -1)) return 0;
        int n = 0;
        auto add = [&](const char* label, double frac, int state, const char* fmt, auto... v) {
            if (n >= max) return;
            OcGauge& g = out[n++];
            std::snprintf(g.label, sizeof g.label, "%s", label);
            std::snprintf(g.value, sizeof g.value, fmt, v...);
            g.frac = frac; g.state = state;
        };
        const bool back = m->speed_ < -0.3;
        const double lim = (back ? kVreverse : m->vCap_) * 3.6, kmh = std::fabs(m->speed_) * 3.6;
        const bool en = m->En();
        auto T2 = [en](const char* ru, const char* e) { return en ? e : ru; };
        add(T2("СКОРОСТЬ", "SPEED"), -1.0, kmh > lim + 2.0 ? 1 : 0, back ? T2("Н %.0f км/ч", "R %.0f km/h") : T2("%.0f км/ч", "%.0f km/h"), kmh);
        add(T2("ПРЕДЕЛ", "LIMIT"), -1.0, 0, T2("%.0f км/ч", "%.0f km/h"), lim);
        add(T2("ЗАРЯД", "CHARGE"), m->batt_, m->batt_ < 0.15 ? 2 : m->batt_ < 0.30 ? 1 : 0, "%.0f %%", m->batt_ * 100.0);
        if (m->chargeW_ > 1.0) add(T2("ЗАРЯДКА", "CHARGING"), -1.0, 0, T2("%+.0f кВт", "%+.0f kW"), m->chargeW_ * 1e-3);
        else add(T2("МОЩНОСТЬ", "POWER"), -1.0, 0, T2("%+.0f кВт", "%+.0f kW"), m->powerW_ * 1e-3);
        const double rk = m->RangeKm();
        if (rk >= 0.0) add(T2("ЗАПАС ХОДА", "RANGE"), -1.0, 0, T2("%.0f км", "%.0f km"), rk); else add(T2("ЗАПАС ХОДА", "RANGE"), -1.0, 0, "%s", "-");
        if (m->cab_) { OcCabin cab{}; cCabinE(c, nullptr, &cab); add(T2("КАБИНА", "CABIN"), -1.0, 0, T2("%.0f кПа %.0f °C", "%.0f kPa %.0f C"), cab.p, cab.T - 273.15); }
        const double pit = m->GetPitch() * DEG, rol = m->GetBank() * DEG;
        add(T2("КРЕН / ТАНГАЖ", "ROLL / PITCH"), -1.0, std::fabs(rol) > 15.0 || std::fabs(pit) > 20.0 ? 1 : 0, T2("%+.0f° / %+.0f°", "%+.0f / %+.0f"), rol, pit);
        if (m->hullLeak_ > 0.0) add(T2("ТЕЧЬ", "LEAK"), -1.0, 2, T2("%.1f см²", "%.1f cm2"), m->hullLeak_ * 1e4);
        { int bad = 0; for (double h : m->wheelHp_) bad += h < 0.7; if (bad) add(T2("КОЛЁСА", "WHEELS"), -1.0, 1, T2("повреждено %d", "%d damaged"), bad); }
        if (m->lastHitT_ > 0.0) add(T2("НАЕЗД", "HIT"), -1.0, 2, "%s", T2("человек", "a person"));
        else if (m->park_) add(T2("ТОРМОЗ", "BRAKE"), -1.0, 1, "%s", T2("стояночный", "parking"));
        else if (m->brakeHeld_) add(T2("ТОРМОЗ", "BRAKE"), -1.0, 0, "%s", T2("тормоз", "on"));
        if (m->towBy_) add(T2("СЦЕПКА", "COUPLED"), -1.0, 0, T2("за %s", "behind %s"), m->towBy_->GetName());
        else if (m->towing_) add(T2("СЦЕПКА", "COUPLED"), -1.0, 0, T2("ведёт %s", "towing %s"), m->towing_->GetName());
        return n;
    }
    static int cItemE(void* c, int i, OcItem* out) {
        MPU* m = Self(c);
        if (i < m->ChassisItemCount()) return m->ChassisItem(i, out) ? 1 : 0;
        i -= m->ChassisItemCount();
        *out = OcItem{};
        if (i == 0) {                                            // from outside: the stern door, at the foot of its stairs
            out->id = kItemHatchOut; out->kind = OC_AIRLOCK; out->pos = _V(0.0, -0.9, -5.40); out->dir = _V(0, 0, 1); out->radius = 2.0;
            std::snprintf(out->label, sizeof out->label, "%s", "войти в шлюз Э.МПУ");
        } else if (i == 1) {                                     // inside the airlock: out through the stern door
            out->id = kItemHatchIn; out->kind = OC_EXIT; out->pos = _V(0.0, kDeckY + 1.0, -4.00); out->dir = _V(0, 0, -1); out->radius = 0.8;
            std::snprintf(out->label, sizeof out->label, "%s", "выйти наружу");
        } else if (i >= 2 && i < 2 + kSeatsN) {
            const SeatE& se = kSeatsE[i - 2];
            out->id = kItemSeat0 + i - 2; out->kind = i == 2 ? OC_HELM : OC_SEAT;
            out->pos = _V(se.x, kDeckY, se.z); out->dir = _V(0, 0, se.fz); out->radius = 0.95;
            std::snprintf(out->label, sizeof out->label, "%s", se.label);
        } else return 0;
        return 1;
    }
    static void cSeatE(void*, int id, VECTOR3* pos, VECTOR3* dir) {
        const int k = id - kItemSeat0;
        if (k < 0 || k >= kSeatsN) return;
        if (pos) *pos = _V(kSeatsE[k].x, kDeckY + kSeatsE[k].hips, kSeatsE[k].z);
        if (dir) *dir = _V(0, 0, kSeatsE[k].fz);
    }
    static void cSeatedE(void* c, int seatId, int personId, int on) {
        MPU* m = Self(c);
        if (on) m->seated_.insert(personId); else m->seated_.erase(personId);   // belted in (the crash below)
        if (seatId != kItemSeat0) return;                         // only the driver's seat drives
        if (on) m->driver_ = personId;
        else if (m->driver_ == personId) m->driver_ = 0;
        oapiWriteLogV("EMPU: person %d %s the driver's seat", personId, on ? "takes" : "leaves");
    }
    // ---------------- Э.МПУ: the yoke turns with the steering (build_mpu.py part "Yoke", EmpuGeo.h) ----------------
    // About its own axis (kYokeZv, towards the driver) through the hub, ±25° at full lock, at most 60°/s; right: clockwise as she sees it
    // (Orbiter's frame is left-handed: a positive turn about an axis pointing at her is clockwise)
    static constexpr double kYokeTurn = 25.0 * RAD, kYokeRate = 60.0 * RAD;   // full lock; how fast the hands turn it
    static inline const VECTOR3 kHubE = {-0.65, kDeckY + 1.02, 3.62};
    static VECTOR3 Turn(const VECTOR3& v, const VECTOR3& k, double a) {
        return v * std::cos(a) + crossp(k, v) * std::sin(a) + k * (dotp(k, v) * (1.0 - std::cos(a)));
    }
    double YokeAngle() const { return yokeA_; }
    // the stern ladder: down on the parking brake, folded up against the hatch (132 deg about its top hinge) otherwise
    static constexpr double kLadderFold = 132.0 * RAD, kLadderRate = 90.0 * RAD;
    double ladderA_ = 0.0, ladderDrawn_ = 1e9;
    std::vector<std::vector<NTVERTEX>> ladderRest_;
    void Ladder() {
        const double want = park_ && ladderWant_ ? 0.0 : kLadderFold, st = kLadderRate * oapiGetSysStep();
        ladderA_ += Clamp(want - ladderA_, -st, st);
        if (!dev_ || !mesh_ || std::fabs(ladderA_ - ladderDrawn_) < 0.002) return;
        if (ladderRest_.empty())
            for (int g = 0; g < empu::kLadderN; ++g) {
                MESHGROUP* mg = oapiMeshGroup(mesh_, (DWORD)empu::kLadderGrp[g]);
                ladderRest_.push_back(mg ? std::vector<NTVERTEX>(mg->Vtx, mg->Vtx + mg->nVtx) : std::vector<NTVERTEX>{});
            }
        const VECTOR3 h = V3(empu::kLadderHinge);
        for (int g = 0; g < empu::kLadderN; ++g) {
            std::vector<NTVERTEX> w = ladderRest_[g];
            if (w.empty()) continue;
            for (NTVERTEX& v : w) {
                const VECTOR3 p = RotX(_V(v.x, v.y, v.z) - h, ladderA_) + h, n = RotX(_V(v.nx, v.ny, v.nz), ladderA_);
                v.x = (float)p.x; v.y = (float)p.y; v.z = (float)p.z; v.nx = (float)n.x; v.ny = (float)n.y; v.nz = (float)n.z;
            }
            GROUPEDITSPEC ges{}; ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; ges.Vtx = w.data(); ges.nVtx = (DWORD)w.size();
            oapiEditMeshGroup(dev_, (DWORD)empu::kLadderGrp[g], &ges);
        }
        ladderDrawn_ = ladderA_;
    }
    double yokeA_ = 0.0;
    std::vector<std::vector<NTVERTEX>> yokeRest_;
    double yokeDrawn_ = 1e9;


    // ---------------- Э.МПУ: the terminal and the windscreen projection (photonic: the light written straight into the
    // screen and the glass - one wavelength, chosen at the terminal) ----------------
    // The terminal (build_mpu.py e_terminal): a touch screen (a click on it) and five hard keys each side. Left: the pages
    // (navigation, energy, life support, running gear, coupling); right: light, colour, route on the glass, the projection,
    // brightness. The projection (e_projection): the heading and the speed; the route to the target chosen on the
    // navigation page - a ribbon on the ground to it, its name and distance.
    static constexpr int kTW = 512, kTH = 384, kYW = 256, kYH = 160;
    enum { kPgNav = 0, kPgEnergy, kPgLife, kPgGear, kPgTow };
    SURFHANDLE term_ = nullptr, yokeScr_ = nullptr;
    oapi::Font *tfBig_ = nullptr, *tfMid_ = nullptr, *tfSm_ = nullptr;
    int page_ = kPgNav, colour_ = 0, bright_ = 2;
    bool ru_ = false;                                                       // the screens' language: Russian, else English
    const char* Tl(const char* ru, const char* en) const { return ru_ ? ru : en; }
    bool route_ = false, heads_ = false, ladderWant_ = true;
    double dispT_ = 1.0, navT_ = 9.0;
    bool warpTested_ = false;
    int dispLog_ = 0;                                                       // the first draws logged (a sketchpad got or not)
    struct Target { std::string name; double lng, lat; };
    std::vector<Target> targets_;
    int target_ = -1;
    static DWORD Rgb(int r, int g, int b) { return 0xFF000000u | (DWORD)((b << 16) | (g << 8) | r); }   // Sketchpad: 0xAABBGGRR, opaque
    DWORD Ink(double k = 1.0) const {                                       // the chosen wavelength at this brightness
        static const int pal[4][3] = {{70, 226, 255}, {255, 148, 40}, {242, 248, 255}, {70, 226, 255}};
        const double b = (0.45 + 0.275 * bright_) * k;
        return Rgb(int(pal[colour_][0] * b), int(pal[colour_][1] * b), int(pal[colour_][2] * b));
    }
    const char* ColourName(int c) const {
        static const char* ru[4] = {"бирюзовый", "янтарный", "белый", "бирюзовый"}; static const char* en[4] = {"cyan", "amber", "white", "cyan"};
        return (ru_ ? ru : en)[c & 3];
    }
    void DisplaysAttach() {
        if (!cab_ || !dev_) return;
        const DWORD at = OAPISURFACE_TEXTURE | OAPISURFACE_RENDERTARGET | OAPISURFACE_SKETCHPAD | OAPISURFACE_NOMIPMAPS;
        if (!term_) term_ = oapiCreateSurfaceEx(kTW, kTH, at);
        if (!yokeScr_) yokeScr_ = oapiCreateSurfaceEx(kYW, kYH, at);
        if (!tfBig_) {
            tfBig_ = oapiCreateFont(46, true, "GOST type A", FONT_NORMAL, 0);
            tfMid_ = oapiCreateFont(26, true, "GOST type A", FONT_NORMAL, 0);
            tfSm_ = oapiCreateFont(19, true, "GOST type A", FONT_NORMAL, 0);
        }
        const bool t1 = term_ && oapiSetTexture(dev_, empu::kTermTex, term_), t2 = yokeScr_ && oapiSetTexture(dev_, empu::kYokeScrTex, yokeScr_);
        oapiWriteLogV("EMPU %s: displays - terminal surface %s, texture %d %s; projection surface %s, texture %d %s", GetName(),
                      term_ ? "made" : "NOT MADE", empu::kTermTex, t1 ? "set" : "NOT SET", yokeScr_ ? "made" : "NOT MADE", empu::kYokeScrTex, t2 ? "set" : "NOT SET");
        dispLog_ = 2;
        dispT_ = 1.0;
    }
    void DisplaysFree() {
        if (term_) oapiDestroySurface(term_);
        if (yokeScr_) oapiDestroySurface(yokeScr_);
        for (oapi::Font* f : {tfBig_, tfMid_, tfSm_}) if (f) oapiReleaseFont(f);
        term_ = yokeScr_ = nullptr; tfBig_ = tfMid_ = tfSm_ = nullptr;
    }
    void Displays() {
        if (!dev_ || !term_) return;
        if ((navT_ += oapiGetSysStep()) > 3.0) { navT_ = 0.0; Targets(); }
        if ((dispT_ += oapiGetSysStep()) < 0.1) return;
        dispT_ = 0.0;
        DrawTerminal();
        DrawYoke();
    }
    // where we are; the bases of this body and the vehicles standing on it within 200 km
    bool HereEqu(double& lng, double& lat, double& R) const {
        OBJHANDLE ref = GetSurfaceRef(); if (!ref) return false;
        double rad; GetEquPos(lng, lat, rad); R = oapiGetSize(ref); return true;
    }
    static void Course(double lng1, double lat1, double lng2, double lat2, double R, double& dist, double& brg) {
        const double dl = lng2 - lng1, a = std::sin((lat2 - lat1) / 2), b = std::sin(dl / 2);
        dist = 2 * R * std::asin((std::min)(1.0, std::sqrt(a * a + std::cos(lat1) * std::cos(lat2) * b * b)));
        brg = std::atan2(std::sin(dl) * std::cos(lat2), std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dl));
        if (brg < 0) brg += PI2;
    }
    void Targets() {
        OBJHANDLE ref = GetSurfaceRef(); double lng, lat, R;
        if (!ref || !HereEqu(lng, lat, R)) return;
        std::string keep = target_ >= 0 && target_ < (int)targets_.size() ? targets_[target_].name : "";
        std::vector<std::pair<double, Target>> found;
        char nm[64];
        for (DWORD i = 0; i < oapiGetBaseCount(ref); ++i) {
            OBJHANDLE b = oapiGetBaseByIndex(ref, i); double bl, bt;
            oapiGetBaseEquPos(b, &bl, &bt); oapiGetObjectName(b, nm, sizeof nm);
            double d, br; Course(lng, lat, bl, bt, R, d, br); found.push_back({d, {nm, bl, bt}});
        }
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
            OBJHANDLE v = oapiGetVesselByIndex(i);
            if (v == GetHandle() || (api_.PersonOfBody && api_.PersonOfBody(v))) continue;
            VESSEL* vi = oapiGetVesselInterface(v);
            if (vi->GetSurfaceRef() != ref || vi->GetAltitude() > 50.0) continue;
            double vl, vt, vr; vi->GetEquPos(vl, vt, vr);
            double d, br; Course(lng, lat, vl, vt, R, d, br);
            if (d < 2.0e5) found.push_back({d, {vi->GetName(), vl, vt}});
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        targets_.clear(); target_ = -1;
        for (size_t k = 0; k < found.size() && k < 8; ++k) { targets_.push_back(found[k].second); if (found[k].second.name == keep) target_ = (int)k; }
        if (target_ < 0) route_ = false;
    }
    bool TargetCourse(double& dist, double& rel, std::string& name) const {
        if (target_ < 0 || target_ >= (int)targets_.size()) return false;
        double lng, lat, R, brg, hdg; if (!HereEqu(lng, lat, R)) return false;
        Course(lng, lat, targets_[target_].lng, targets_[target_].lat, R, dist, brg);
        oapiGetHeading(GetHandle(), &hdg);
        rel = std::remainder(brg - hdg, PI2); name = targets_[target_].name;
        return true;
    }
    TantraGlyphs glyphs_;
    int tSz_ = 1, tCol_ = 0, tH_ = 0, tV_ = 0;                             // the text state: size 0..2, atlas colour, align
    void TF(int sz) { tSz_ = sz; }
    void TC(DWORD c) { tCol_ = TantraGlyphs::Colour(c); }
    void TA(oapi::Sketchpad::TAlign_horizontal h, oapi::Sketchpad::TAlign_vertical v) {
        tH_ = h == oapi::Sketchpad::LEFT ? 0 : h == oapi::Sketchpad::CENTER ? 1 : 2; tV_ = v == oapi::Sketchpad::TOP ? 0 : 1;
    }
    void Txt(oapi::Sketchpad* k, int x, int y, const char* t) {
        static const double px[3] = {15.0, 22.0, 46.0};                    // the capitals' height, pixels
        static const int as[3] = {0, 1, 3};                                 // the atlas sizes
        const double h = px[tSz_];
        glyphs_.DrawA(k, x, tV_ ? y - h : y, t, as[tSz_], tCol_, h, tH_);
    }
    void Bar(oapi::Sketchpad* k, int x, int y, int w, int h, double f, oapi::Brush* br) {
        k->SetBrush(nullptr); k->Rectangle(x, y, x + w, y + h);
        const int fw = (int)std::lround((w - 4) * Clamp(f, 0.0, 1.0));
        if (fw > 0) { k->SetBrush(br); k->Rectangle(x + 2, y + 2, x + 2 + fw, y + h - 2); k->SetBrush(nullptr); }
    }
    // The terminal, 512 x 384. The bezel's recess hides the edges at a glance from the seat: everything is kept inside
    // x 36..476, y 14..352. Two languages (Tl), the common font atlas
    void DrawTerminal() {
        oapi::Sketchpad* k = oapiGetSketchpad(term_);
        if (dispLog_ > 0) { --dispLog_; oapiWriteLogV("EMPU %s: terminal sketchpad %s", GetName(), k ? "got" : "NOT GOT"); }
        if (!k) return;
        oapi::Brush* bg = oapiCreateBrush(Rgb(6, 14, 10));
        oapi::Brush* fill = oapiCreateBrush(Ink(0.85));
        oapi::Pen* pen = oapiCreatePen(1, 2, Ink(0.9));
        oapi::Pen* thin = oapiCreatePen(1, 1, Ink(0.45));
        const DWORD warn = Rgb(255, 180, 60), alarm = Rgb(255, 70, 60);
        k->SetPen(nullptr); k->SetBrush(bg); k->Rectangle(0, 0, kTW, kTH);
        k->SetBackgroundMode(oapi::Sketchpad::BK_TRANSPARENT); TC(Ink()); k->SetPen(pen); k->SetBrush(nullptr);
        // the soft legends of the hard keys, at the screen's edges
        const char* L[5] = {Tl("НАВ", "NAV"), Tl("ЭНЕРГ", "POWER"), Tl("СЖО", "LIFE"), Tl("ХОД", "GEAR"), Tl("СЦЕП", "TOW")};
        const char* Rr[5] = {Tl("СВЕТ", "LIGHT"), Tl("ЦВЕТ", "COLOR"), Tl("МАРШ", "ROUTE"), Tl("ФАРЫ", "HEADL"), Tl("ЯРК", "BRIGHT")};
        TF(0);
        for (int i = 0; i < 5; ++i) {
            const int y = 22 + i * 66;
            TC(Ink(i == page_ ? 1.0 : 0.6)); TA(oapi::Sketchpad::LEFT, oapi::Sketchpad::TOP); Txt(k, 40, y, L[i]);
            if (i == page_) k->Rectangle(34, y - 3, 104, y + 22);
            TC(Ink(0.6)); TA(oapi::Sketchpad::RIGHT, oapi::Sketchpad::TOP); Txt(k, kTW - 40, y, Rr[i]);
        }
        k->SetPen(thin); k->Line(110, 14, 110, kTH - 14); k->Line(kTW - 110, 14, kTW - 110, kTH - 14); k->SetPen(pen);
        TA(oapi::Sketchpad::LEFT, oapi::Sketchpad::TOP); TC(Ink());
        const int x0 = 120, x1 = kTW - 118;
        char b[96];
        TF(1);
        if (page_ == kPgNav) {
            Txt(k, x0, 14, Tl("НАВИГАЦИЯ", "NAVIGATION"));
            double hdg; oapiGetHeading(GetHandle(), &hdg);
            TF(0);
            std::snprintf(b, sizeof b, Tl("курс %03.0f°   %.0f км/ч", "hdg %03.0f   %.0f km/h"), hdg * DEG, std::fabs(speed_) * 3.6); Txt(k, x0, 44, b);
            Txt(k, x0, 66, Tl("цель (нажать):", "target (tap):"));
            double lng, lat, R; HereEqu(lng, lat, R);
            for (size_t i = 0; i < targets_.size() && i < 7; ++i) {
                double d, br; Course(lng, lat, targets_[i].lng, targets_[i].lat, R, d, br);
                const int y = 90 + (int)i * 30;
                if ((int)i == target_) { k->SetBrush(fill); k->SetPen(nullptr); k->Rectangle(x0 - 4, y - 3, x1 + 4, y + 25); k->SetBrush(nullptr); k->SetPen(pen); TC(Rgb(6, 14, 10)); }
                else TC(Ink(0.85));
                std::snprintf(b, sizeof b, "%-.16s", targets_[i].name.c_str()); Txt(k, x0, y, b);
                std::snprintf(b, sizeof b, d < 1e4 ? Tl("%.2f км %03.0f°", "%.2f km %03.0f") : Tl("%.0f км %03.0f°", "%.0f km %03.0f"), d * 1e-3, br * DEG);
                TA(oapi::Sketchpad::RIGHT, oapi::Sketchpad::TOP); Txt(k, x1, y, b); TA(oapi::Sketchpad::LEFT, oapi::Sketchpad::TOP);
            }
            TC(Ink());
            Txt(k, x0, 310, route_ ? Tl("маршрут на руле: ВКЛ", "route on the yoke: ON") : Tl("маршрут на руле: выкл", "route on the yoke: off"));
        } else if (page_ == kPgEnergy) {
            Txt(k, x0, 14, Tl("ЭНЕРГИЯ", "POWER"));
            TF(0);
            for (int c = 0; c < kCellsN; ++c) {
                const int x = x0 + c * 68;
                std::snprintf(b, sizeof b, Tl("яч %d", "cell %d"), c + 1); Txt(k, x, 46, b);
                if (cell_[c] < 0) Txt(k, x, 70, Tl("нет", "none"));
                else { std::snprintf(b, sizeof b, "%.0f %%", cell_[c] / kCellJ * 100.0); Txt(k, x, 70, b); Bar(k, x, 94, 56, 14, cell_[c] / kCellJ, fill); }
            }
            std::snprintf(b, sizeof b, Tl("всего %.0f кВт·ч (%.0f %%)", "total %.0f kWh (%.0f %%)"), CellsSum() / 3.6e6, batt_ * 100.0); Txt(k, x0, 126, b);
            std::snprintf(b, sizeof b, Tl("отбор %+.0f кВт", "draw %+.0f kW"), powerW_ * 1e-3); Txt(k, x0, 154, b);
            std::snprintf(b, sizeof b, Tl("  в т.ч. СЖО и обогрев %.1f кВт", "  of it life support, heating %.1f kW"), moduleW_ * 1e-3); Txt(k, x0, 178, b);
            std::snprintf(b, sizeof b, Tl("зарядка %.0f кВт", "charging %.0f kW"), chargeW_ * 1e-3); Txt(k, x0, 206, b);
            const double rk = RangeKm();
            if (rk >= 0) std::snprintf(b, sizeof b, Tl("запас хода %.0f км", "range %.0f km"), rk); else std::snprintf(b, sizeof b, "%s", Tl("запас хода —", "range -"));
            Txt(k, x0, 234, b);
            std::snprintf(b, sizeof b, Tl("фары %s   свет %s", "headlights %s   cabin light %s"), heads_ ? Tl("вкл", "on") : Tl("выкл", "off"),
                          light_ == kLightFull ? Tl("полный", "full") : light_ == kLightStandby ? Tl("дежурный", "standby") : Tl("выкл", "off")); Txt(k, x0, 262, b);
        } else if (page_ == kPgLife) {
            Txt(k, x0, 14, Tl("ЖИЗНЕОБЕСПЕЧЕНИЕ", "LIFE SUPPORT"));
            TF(0);
            OcCabin cab{}; cCabinE(this, nullptr, &cab);
            std::snprintf(b, sizeof b, Tl("давление %.1f кПа", "pressure %.1f kPa"), cab.p); TC(cab.p < 90 ? warn : Ink()); Txt(k, x0, 44, b);
            std::snprintf(b, sizeof b, "O2 %.1f kPa", cab.ppO2); if (ru_) std::snprintf(b, sizeof b, "O2 %.1f кПа", cab.ppO2);
            TC(cab.ppO2 < 19 ? warn : Ink()); Txt(k, x0, 68, b);
            std::snprintf(b, sizeof b, Tl("CO2 %.2f кПа", "CO2 %.2f kPa"), cab.ppCO2); TC(cab.ppCO2 > 0.7 ? warn : Ink()); Txt(k, x0 + 150, 68, b);
            std::snprintf(b, sizeof b, Tl("темп. %.1f °C   влажн. %.0f %%", "temp %.1f C   humidity %.0f %%"), cab.T - 273.15, PP(nH2O_) / (611.0 * std::exp(17.27 * (cabT_ - 273.15) / (cabT_ - 35.85))) * 100.0);
            TC(std::fabs(cab.T - kTset) > 4 ? warn : Ink()); Txt(k, x0, 92, b); TC(Ink());
            std::snprintf(b, sizeof b, Tl("запас O2 %.0f %%   N2 %.0f %%", "O2 tank %.0f %%   N2 %.0f %%"), o2Tank_ / kO2TankMol * 100, n2Tank_ / kN2TankMol * 100); Txt(k, x0, 120, b);
            std::snprintf(b, sizeof b, Tl("поглотитель CO2 %.0f %%", "CO2 absorber %.0f %%"), absorb_ / kAbsorbMol * 100); Txt(k, x0, 144, b);
            std::snprintf(b, sizeof b, Tl("людей %d   обогрев %.1f   охлажд. %.1f кВт", "people %d   heating %.1f   cooling %.1f kW"), people_, heatW_ * 1e-3, coolW_ * 1e-3); Txt(k, x0, 172, b);
            std::snprintf(b, sizeof b, Tl("СЖО берёт %.2f кВт", "life support draws %.2f kW"), lifeW_ * 1e-3); Txt(k, x0, 196, b);
            if (o2Tank_ > 1 && people_ > 0) { std::snprintf(b, sizeof b, Tl("O2 хватит на %.0f чел·сут", "O2 for %.0f person-days"), o2Tank_ / 26.25); Txt(k, x0, 220, b); }
            if (hullLeak_ > 0.0) {
                TC(alarm);
                std::snprintf(b, sizeof b, Tl("ТЕЧЬ %.1f см² — комплект у двери", "LEAK %.1f cm2 - kit by the door"), hullLeak_ * 1e4); Txt(k, x0, 246, b);
                TC(Ink());
            }
            k->SetPen(pen); k->SetBrush(nullptr); k->Rectangle(x0 - 4, 284, x1 + 4, 314);   // a touch line: the dimmer
            std::snprintf(b, sizeof b, Tl("яркость света %d из 5 (нажать)", "cabin light %d of 5 (tap)"), lightLvl_); TC(Ink()); Txt(k, x0 + 4, 290, b);
        } else if (page_ == kPgGear) {
            Txt(k, x0, 14, Tl("ХОДОВАЯ", "RUNNING GEAR"));
            TF(0);
            static const char* Wr[8] = {"ПЛ1", "ПП1", "ПЛ2", "ПП2", "ЗЛ1", "ЗП1", "ЗЛ2", "ЗП2"};
            static const char* We[8] = {"FL1", "FR1", "FL2", "FR2", "RL1", "RR1", "RL2", "RR2"};
            for (int i = 0; i < mpu::kWheels; ++i) {
                const int x = x0 + (i % 2) * 140, y = 44 + (i / 2) * 46;
                const bool slips = slip_[i] > 0.25;
                TC(slips ? warn : Ink());
                std::snprintf(b, sizeof b, Tl("%s  букс %.0f %%", "%s  slip %.0f %%"), (ru_ ? Wr : We)[i], slip_[i] * 100.0); Txt(k, x, y, b);
                TC(wheelHp_[i] < 0.15 ? alarm : wheelHp_[i] < 0.7 ? warn : Ink());
                std::snprintf(b, sizeof b, Tl("осадка %.0f см  цел %.0f %%", "sink %.0f cm  ok %.0f %%"), sink_[i] * 100.0, wheelHp_[i] * 100.0); Txt(k, x, y + 20, b);
            }
            TC(Ink());
            std::snprintf(b, sizeof b, Tl("сцепл. с грунтом %.2f   клиренс %+.2f м", "grip %.2f   ride height %+.2f m"), muEff_, ride_); Txt(k, x0, 236, b);
            std::snprintf(b, sizeof b, Tl("крен %+.1f°  тангаж %+.1f°", "roll %+.1f  pitch %+.1f"), GetBank() * DEG, GetPitch() * DEG); Txt(k, x0, 260, b);
            std::snprintf(b, sizeof b, Tl("стояночный %s   трап %s", "parking brake %s   ladder %s"), park_ ? Tl("вкл", "on") : Tl("выкл", "off"), ladderWant_ ? Tl("на стоянке", "when parked") : Tl("убран", "stowed")); Txt(k, x0, 284, b);
        } else {
            Txt(k, x0, 14, Tl("СЦЕПКА", "COUPLING"));
            TF(0);
            if (towBy_) { std::snprintf(b, sizeof b, Tl("нас ведёт %s", "towed by %s"), towBy_->GetName()); Txt(k, x0, 50, b); }
            if (towing_) { std::snprintf(b, sizeof b, Tl("ведём %s, дышло %.2f м", "towing %s, bar %.2f m"), towing_->GetName(), towL_); Txt(k, x0, 80, b); }
            if (!towBy_ && !towing_) { Txt(k, x0, 50, Tl("не сцеплены", "not coupled")); Txt(k, x0, 76, Tl("дышло 3 м, встать в 2–4 м", "3 m bar: stand 2-4 m off")); }
        }
        // the status line: light, colour, the language (a tap switches it)
        TF(0); TC(Ink(0.55)); TA(oapi::Sketchpad::LEFT, oapi::Sketchpad::BOTTOM);
        std::snprintf(b, sizeof b, Tl("свет %s · %s · ярк %d · RU/EN", "light %s · %s · bright %d · EN/RU"),
                      light_ == kLightFull ? Tl("полн", "full") : light_ == kLightStandby ? Tl("дежур", "standby") : Tl("выкл", "off"), ColourName(colour_), bright_ + 1);
        Txt(k, x0, kTH - 16, b);
        oapiReleaseSketchpad(k);
        oapiReleaseBrush(bg); oapiReleaseBrush(fill); oapiReleasePen(pen); oapiReleasePen(thin);
    }
    // the yoke's screen (256 x 160, turning with the yoke): the speed and the limit, the heading, the charge and the range;
    // the route's arrow and distance when one is chosen; over the four buttons below it their legends, lit when on
    void DrawYoke() {
        if (!yokeScr_) return;
        oapiClearSurface(yokeScr_, 0xFF060E0A);
        oapi::Sketchpad* k = oapiGetSketchpad(yokeScr_);
        if (!k) return;
        const DWORD warn = Rgb(255, 148, 40), alarm = Rgb(255, 70, 70);
        oapi::Pen* pen = oapiCreatePen(1, 2, Ink()); oapi::Brush* fill = oapiCreateBrush(Ink(0.9));
        k->SetBackgroundMode(oapi::Sketchpad::BK_TRANSPARENT); k->SetPen(pen); k->SetBrush(nullptr);
        char b[64];
        const bool back = speed_ < -0.3; const double lim = (back ? kVreverse : vCap_) * 3.6, kmh = std::fabs(speed_) * 3.6;
        TF(2); TA(oapi::Sketchpad::RIGHT, oapi::Sketchpad::TOP); TC(kmh > lim + 2 ? warn : Ink());
        std::snprintf(b, sizeof b, "%.0f", kmh); Txt(k, 112, 8, b);
        TF(0); TA(oapi::Sketchpad::LEFT, oapi::Sketchpad::TOP); TC(Ink());
        Txt(k, 118, 12, back ? Tl("км/ч Н", "km/h R") : Tl("км/ч", "km/h"));
        std::snprintf(b, sizeof b, Tl("пр %.0f", "lim %.0f"), lim); Txt(k, 118, 34, b);
        double hdg; oapiGetHeading(GetHandle(), &hdg);
        std::snprintf(b, sizeof b, "%03.0f", hdg * DEG); TA(oapi::Sketchpad::RIGHT, oapi::Sketchpad::TOP); Txt(k, 248, 10, b);
        // the charge
        TA(oapi::Sketchpad::LEFT, oapi::Sketchpad::TOP); TC(batt_ < 0.15 ? alarm : batt_ < 0.3 ? warn : Ink());
        std::snprintf(b, sizeof b, "%.0f %%", batt_ * 100.0); Txt(k, 8, 70, b);
        k->Rectangle(60, 72, 160, 86);
        if (batt_ > 0) { k->SetBrush(fill); k->Rectangle(62, 74, 62 + (int)(96 * Clamp(batt_, 0.0, 1.0)), 84); k->SetBrush(nullptr); }
        const double rk = RangeKm(); TC(Ink());
        if (rk >= 0) { std::snprintf(b, sizeof b, Tl("%.0f км", "%.0f km"), rk); TA(oapi::Sketchpad::RIGHT, oapi::Sketchpad::TOP); Txt(k, 248, 70, b); }
        // the route: an arrow towards the target off her nose, its distance
        double dist, rel; std::string name;
        if (route_ && TargetCourse(dist, rel, name)) {
            const int cx = 200, cy = 44, r = 16;
            const double a = rel;                                            // 0: straight ahead (up on the screen)
            const int tx = cx + (int)std::lround(std::sin(a) * r), ty = cy - (int)std::lround(std::cos(a) * r);
            k->Line(cx - (tx - cx), cy - (ty - cy), tx, ty);
            k->Line(tx, ty, tx - (int)std::lround(std::sin(a - 0.5) * 8), ty + (int)std::lround(std::cos(a - 0.5) * 8));
            k->Line(tx, ty, tx - (int)std::lround(std::sin(a + 0.5) * 8), ty + (int)std::lround(std::cos(a + 0.5) * 8));
            std::snprintf(b, sizeof b, dist < 1e4 ? Tl("%.2f км", "%.2f km") : Tl("%.0f км", "%.0f km"), dist * 1e-3); TA(oapi::Sketchpad::CENTER, oapi::Sketchpad::TOP); Txt(k, cx, 96, b);
        }
        if (hullLeak_ > 0.0) { TC(alarm); TA(oapi::Sketchpad::LEFT, oapi::Sketchpad::TOP); Txt(k, 8, 96, Tl("ТЕЧЬ", "LEAK")); }
        // the buttons' legends at the bottom (over the buttons: headlights, platform, ladder, brake)
        const char* L4[4] = {Tl("ФАРЫ", "LIGHTS"), Tl("ПЛАТФ", "RIDE"), Tl("ТРАП", "LADDER"), Tl("ТОРМ", "BRAKE")};
        const bool on4[4] = {heads_, rideTarget_ > 0.5 * kRideMax, ladderWant_, park_};
        TA(oapi::Sketchpad::CENTER, oapi::Sketchpad::TOP);
        for (int i = 0; i < 4; ++i) {
            const int x = 32 + i * 64;
            TC(on4[i] ? (i == 3 ? warn : Ink()) : Ink(0.45)); Txt(k, x, 132, L4[i]);
            if (on4[i]) { k->SetBrush(fill); k->Rectangle(x - 18, 152, x + 18, 156); k->SetBrush(nullptr); }
        }
        oapiReleaseSketchpad(k);
        oapiReleasePen(pen); oapiReleaseBrush(fill);
    }
    // a click inside the cabin: the ray against the terminal's keys and screen (vessel frame = interior frame)
    static int cClickE(void* c, const VECTOR3* o, const VECTOR3* d, int) {
        MPU* m = Self(c);
        {
            const double ang = -m->YokeAngle();
            const VECTOR3 oo = Turn(*o - kHubE, kYokeZv, ang), dd = Turn(*d, kYokeZv, ang);   // the ray in the unturned yoke
            const double dn = dotp(dd, kYokeZv);
            if (std::fabs(dn) > 1e-6) {
                const double t = (0.058 - dotp(oo, kYokeZv)) / dn;
                if (t > 0 && t < 2.0) {
                    const VECTOR3 h = oo + dd * t; const double a = dotp(h, kYokeX), bb = dotp(h, kYokeYv);
                    for (int i = 0; i < 4; ++i)
                        if (std::fabs(a - (-0.075 + i * 0.05)) < 0.022 && std::fabs(bb + 0.08) < 0.016) { m->YokeButton(i); return 1; }
                }
            }
        }
        const VECTOR3 C = V3(empu::kTermC), R = V3(empu::kTermR), U = V3(empu::kTermU), N = V3(empu::kTermN);
        auto hit = [&](double out, double& a, double& b) {
            const VECTOR3 P = C + N * out; const double dn = dotp(*d, N);
            if (std::fabs(dn) < 1e-6) return false;
            const double t = dotp(P - *o, N) / dn;
            if (t < 0 || t > 2.5) return false;
            const VECTOR3 h = *o + *d * t - P; a = dotp(h, R); b = dotp(h, U); return true;
        };
        double a, b;
        if (hit(0.013, a, b)) {                                             // the hard keys
            for (int sgn = -1; sgn <= 1; sgn += 2)
                for (int i = 0; i < 5; ++i) {
                    const double ka = sgn * (empu::kTermSW / 2 + 0.065), kb = 0.16 - i * 0.08;
                    if (std::fabs(a - ka) < 0.04 && std::fabs(b - kb) < 0.028) { m->Key(sgn < 0 ? i : 5 + i); return 1; }
                }
        }
        if (hit(-empu::kTermRecess, a, b) && std::fabs(a) < empu::kTermSW / 2 && std::fabs(b) < empu::kTermSH / 2) {   // the screen
            const int px = (int)((a / empu::kTermSW + 0.5) * kTW), py = (int)((0.5 - b / empu::kTermSH) * kTH);
            m->Touch(px, py);
            return 1;
        }
        return 0;
    }
    void YokeButton(int i) {
        if (i == 0) heads_ = !heads_;
        else if (i == 1) rideTarget_ = rideTarget_ > 0.5 * kRideMax ? kRideMin : kRideMax;
        else if (i == 2) ladderWant_ = !ladderWant_;
        else park_ = !park_;
        dispT_ = 1.0;
    }
    void Key(int k) {
        if (k < 5) page_ = k;
        else if (k == 5) light_ = light_ == kLightStandby ? kLightFull : light_ == kLightFull ? kLightOff : kLightStandby;
        else if (k == 10) lightLvl_ = lightLvl_ % 5 + 1;                   // the cabin light's dimmer (the life support page)
        else if (k == 6) colour_ = (colour_ + 1) % 3;
        else if (k == 7) route_ = !route_ && target_ >= 0;
        else if (k == 8) heads_ = !heads_;
        else bright_ = (bright_ + 1) % 3;
        dispT_ = 1.0;
    }
    void Touch(int px, int py) {
        if (py > kTH - 40) { ru_ = !ru_; dispT_ = 1.0; return; }       // the status line: the language
        if (px < 110 || px > kTW - 110) {
            const int i = Clamp((py - 10) / 66, 0, 4);
            Key(px < 110 ? i : 5 + i);
            return;
        }
        if (page_ == kPgLife && py >= 280 && py <= 318) { Key(10); return; }
        if (page_ == kPgNav && px > 110 && px < kTW - 110) {
            const int i = (py - 88) / 30;
            if (py >= 88 && i >= 0 && i < (int)targets_.size()) { target_ = (target_ == i) ? -1 : i; route_ = target_ >= 0; }
        }
        dispT_ = 1.0;
    }

public:
    ~MPU() override { DisplaysFree(); }
    void clbkVisualCreated(VISHANDLE vis, int refcount) override { TVChassis::clbkVisualCreated(vis, refcount); yokeDrawn_ = 1e9; ladderDrawn_ = 1e9; lightDrawn_ = -1; DisplaysAttach(); }
    void clbkPostStep(double simt, double simdt, double mjd) override {
        (void)simt; (void)simdt; (void)mjd;
        if (!cab_) return;
        if (std::getenv("EMPU_WARP") && !warpTested_) {                       // WARPTEST (temporary)
            warpTested_ = true;
            for (double stepS : {600.0, 3000.0, 86400.0, 86400.0, 86400.0}) {
                const double w = ModuleLoadW(stepS);
                oapiWriteLogV("WARPTEST step %.0f s: T %.2f K, p %.2f kPa, O2 %.2f, CO2 %.3f kPa, heat %.0f cool %.0f W, draw %.0f W", stepS, cabT_, CabP() * 1e-3,
                              PP(nO2_) * 1e-3, PP(nCO2_) * 1e-3, heatW_, coolW_, w);
            }
        }
        LightsApply();
        Displays();
        Hvac();
        Ladder();
        const double want = Clamp(steer_, -1.0, 1.0) * kYokeTurn, step = kYokeRate * oapiGetSysStep();   // turned by hand, not snapped
        yokeA_ += Clamp(want - yokeA_, -step, step);
        if (!dev_ || !mesh_) return;
        const double a = YokeAngle();
        if (std::fabs(a - yokeDrawn_) < 0.002) return;
        if (yokeRest_.empty())
            for (int g = 0; g < empu::kYokeN; ++g) {
                MESHGROUP* mg = oapiMeshGroup(mesh_, (DWORD)empu::kYokeGrp[g]);
                yokeRest_.push_back(mg ? std::vector<NTVERTEX>(mg->Vtx, mg->Vtx + mg->nVtx) : std::vector<NTVERTEX>{});
            }
        for (int g = 0; g < empu::kYokeN; ++g) {
            std::vector<NTVERTEX> w = yokeRest_[g];
            if (w.empty()) continue;
            for (NTVERTEX& v : w) {
                const VECTOR3 p = Turn(_V(v.x, v.y, v.z) - kHubE, kYokeZv, a) + kHubE, n = Turn(_V(v.nx, v.ny, v.nz), kYokeZv, a);
                v.x = (float)p.x; v.y = (float)p.y; v.z = (float)p.z; v.nx = (float)n.x; v.ny = (float)n.y; v.nz = (float)n.z;
            }
            GROUPEDITSPEC ges{};
            ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; ges.Vtx = w.data(); ges.nVtx = (DWORD)w.size();
            oapiEditMeshGroup(dev_, (DWORD)empu::kYokeGrp[g], &ges);
        }
        yokeDrawn_ = a;
    }

protected:
    // the driver's hands on the yoke's horns, as on the Tantra's bridge: the grip through the fist, the thumb on top
    static void cSeatHandsE(void* c, int seatId, int, OcHand* left, OcHand* right) {
        if (seatId != kItemSeat0 || !left || !right || left->size < int(sizeof(OcHand)) || right->size < int(sizeof(OcHand))) return;
        const VECTOR3 hub = kHubE;
        const double elbowMin = kDeckY + 0.86 + 0.05;
        const double ang = Self(c)->YokeAngle();                                   // the yoke turned: the grips go round with it
        const VECTOR3 yx = Turn(kYokeX, kYokeZv, ang), yy = Turn(kYokeYv, kYokeZv, ang);
        for (int s = 0; s < 2; ++s) {
            const double sx = s == 0 ? -1 : 1, r = 0.020;
            const VECTOR3 gt = hub + yx * (sx * 0.243) + yy * -0.058, gb = hub + yx * (sx * 0.268) + yy * -0.168;
            const VECTOR3 a = unit(gb - gt), gm = gt + (gb - gt) * 0.35;
            VECTOR3 n = kYokeZv * -0.95 + yx * (-sx * 0.3);
            n = unit(n - a * dotp(n, a));
            const VECTOR3 fw = s == 0 ? crossp(a, n) : crossp(n, a);
            OcHand* h = s == 0 ? left : right;
            h->on = 1; h->what = 10 + s; h->pos = gm - n * r; h->palm = n; h->fwd = fw; h->grip = OC_GRIP_HANDLE; h->radius = r; h->elbowMinY = elbowMin;
        }
    }
    static void cUseE(void* c, int id, int personId) {
        MPU* m = Self(c);
        if (id >= kItemSocket) { m->ChassisUse(id, personId); return; }
        OcInfo in{};
        if (!m->api_.Info(personId, &in)) return;
        if (id == kItemHatchOut) {                                // in through the stern door: she stands in the airlock
            if (in.where != 1 || !in.vessel || !m->api_.EnterShip) return;
            const VECTOR3 at = _V(0.0, kDeckY, -3.10), dir = _V(0, 0, 1);
            m->api_.EnterShip(in.vessel, m->GetHandle(), &at, &dir);
            m->AirlockPass();
        } else if (id == kItemHatchIn) {                          // out: down the stairs, at their foot, facing away
            if (m->driver_ == personId) m->driver_ = 0;
            OBJHANDLE ref = m->GetSurfaceRef();
            if (!ref || !m->api_.ExitTo) return;
            VECTOR3 gp;
            m->Local2Global(_V(0.0, -0.9, -5.70), gp);
            double lng, lat, rad, hdg;
            oapiGlobalToEqu(ref, gp, &lng, &lat, &rad);
            oapiGetHeading(m->GetHandle(), &hdg);
            VESSELSTATUS2 vs;
            std::memset(&vs, 0, sizeof vs);
            vs.version = 2; vs.rbody = ref; vs.status = 1; vs.arot.x = 10.0;
            vs.surf_lng = lng; vs.surf_lat = lat; vs.surf_hdg = std::fmod(hdg + PI, PI2);
            std::string name = in.name;
            for (char& ch : name) if (ch == ' ') ch = '_';
            m->api_.ExitTo(personId, name.c_str(), &vs);
            m->AirlockPass();
        }
    }

    // the driver: still aboard and at the post - held there each frame (Э.МПУ: seated in the driver's seat); -> the body, or nothing
    OBJHANDLE DriverHold() override {
        if (!driver_ || !api_.Ok()) return nullptr;
        OcInfo in{};
        if (!api_.Info(driver_, &in) || in.state != 0 || (api_.ShipOf && api_.ShipOf(driver_) != GetHandle())) { driver_ = 0; return nullptr; }
        if (cab_) return in.vessel;
        const VECTOR3 dir = _V(0, 0, 1);
        if (api_.Carry) api_.Carry(driver_, GetHandle(), &kPostFeet, &dir);
        return in.vessel;
    }
};

}  // namespace

// the same, sized: the caller says how much it holds (its sizeof), we write no more - MpuState only grows at its end
extern "C" __declspec(dllexport) int mpuDriverStateSized(OBJHANDLE person, void* out, int size) {
    if (!out || size <= 0) return 0;
    MpuState s{};
    for (TVChassis* m : All())
        if (m->DriverState(person, &s)) { std::memcpy(out, &s, (std::min)((size_t)size, sizeof s)); return (int)sizeof s; }
    return 0;
}
// a command from the person's suit computer: 1 the parking brake on/off, 2 the platform up/down -> true if she is at a post
extern "C" __declspec(dllexport) bool mpuDriverCommand(OBJHANDLE person, int cmd) {
    for (TVChassis* m : All())
        if (m->DriverCommand(person, cmd)) return true;
    return false;
}
extern "C" __declspec(dllexport) bool mpuDriverState(OBJHANDLE person, MpuState* s) {
    if (!s) return false;
    for (TVChassis* m : All())
        if (m->DriverState(person, s)) return true;
    return false;
}

DLLCLBK VESSEL* ovcInit(OBJHANDLE hvessel, int flightmodel) { return new MPU(hvessel, flightmodel); }
DLLCLBK void ovcExit(VESSEL* vessel) { delete static_cast<MPU*>(vessel); }
