// TVChassis - the МПУ chassis as a module of its own: the eight wheel modules on double wishbones, the terramechanics
// of the wheel on each body's soil, the active suspension (ride height, levelling), drive, brakes, steering, the battery,
// sound, dust, collisions (people, the Tantra's legs, other vehicles), the steps, the start hold and the parked rest.
// A deck module (the open post, the crew cabin, a tanker...) derives from it and gives its mass, mesh, interior and driver.
// Included by one translation unit per vessel DLL (its definitions live in an anonymous namespace).
#pragma once
// МПУ - мобильная платформа универсальная, the test sample: an open 8x8 platform with a control post up front.
// Physics: eight wheels are Orbiter touchdown points (a spring and a damper each, sized to the local gravity: the active
// suspension keeps the same static sag on any body); the tyres are ours - each wheel in contact gets a longitudinal force
// (drive, brake) and a lateral force (its slip across the wheel's own heading), both inside the friction circle mu*N, so
// steering, cornering and skidding come out of the wheels. Orbiter's own friction on the wheels is kept small.
// Driven only by a person at its post (the user: it does not drive itself). The deck, the steps and the post are an interior
// for OrbiterCrew: a person climbs the steps (F), walks the deck, takes the post (F) - the platform holds him or her there
// (ocCarry) and reads the person's own keys while the focus is on that body: W/S drive and brake (S from standstill:
// reverse), A/D steer, Shift with W - full speed, Space raises the platform, Ctrl lowers it, B - parking brake.
// F at the post again - steps back; F at the deck edge - down to the ground.
// Energy: a battery; drive power out of it, braking partly back. At rest with the brake on it is handed to Orbiter's landed state.
#define ORBITER_MODULE
#include "orbitersdk.h"
#include "MpuGeo.h"                 // the chassis geometry (wheels, wishbones, steps), written by build_mpu.py
#include "OrbiterCrewApi.h"
#include "XRSound.h"
#include "TVApi.h"                  // the transport module: sockets, the station's cable, what people carry

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <map>
#include <set>

// The state of the platform a person drives, for the suit's helmet display («Архитектор - скафандр» reads it through
// GetModuleHandle("MPU.dll") / GetProcAddress, once a frame). SI units. -> true only while that person is at a post.
struct MpuState {
    double speed;          // m/s, signed (backwards < 0)
    double speedMax;       // m/s: the present limit (40 or 80 km/h; backwards 15)
    int full;              // 1 = full speed (Shift)
    double steer;          // -1..+1 (+ right)
    double deck;           // m, the deck above the ground
    double deckMin, deckMax;
    int brake;             // 1 = parking brake
    double charge;         // 0..1
    double powerW;         // W drawn now (negative: braking back into the battery)
    double energyKWh;      // left in the battery
    double rangeKm;        // at the present draw and speed (-1: standing)
};

// the Tantra's outer solids, as Tantra.dll exports them (TantraSolids.h): ship frame, taken each frame
struct TantraSolid { int kind; /*1 capsule a..b radius r, 2 box c/half/R*/ VECTOR3 a, b; double r; VECTOR3 c, half; MATRIX3 R; };
typedef int (*tantraOuterSolids_t)(OBJHANDLE ship, TantraSolid* out, int max);

namespace {

constexpr double kMass = 9000.0;             // kg, МПУ empty: CNT-composite spine and deck 2.8 t, eight airless wheels 1.2 t, eight
                                             // hub motors 2.0 t, arms and active suspension 1.0 t, 1 MWh battery 1.4 t, electronics 0.6 t
constexpr double kMassE = 14100.0;           // kg, Э.МПУ dry: the chassis + the crew cabin 3.5 t + the thrust module (four pods) 1.2 t
                                             // + the metallic hydrogen tank 0.4 t
constexpr double kFuelE = 1500.0;            // kg of metallic hydrogen (the pods: exhaust ~10 km/s, ~1 km/s of delta-v)
constexpr double kSag = 0.12;
constexpr double kTravel = 0.45;             // (chassis v2: the wishbones give +-0.45 m of wheel travel)
constexpr double kLevelF = 0.90 / 6.60, kLevelR = 0.90 / 3.70;   // the slopes the struts can level out (along, across)             // m, the soft travel of our suspension (the bump stop beyond it)                // static sag of each wheel at the local gravity, m
constexpr double kMu = 0.8;                  // tyre grip
constexpr double kPmax = 3.0e6;              // W, all hub motors together (8 x 375 kW, geared hub motors of a heavy tractor)
constexpr double kSlipTC = 0.30;
constexpr double kSlipABS = 0.15;            // the service brake's slip (ABS): near the soil's best braking, still steerable             // traction control: the slip the motors keep on W
constexpr double kFmotor = 2.85e5;           // N, the hub motors' total force at low speed (8 x 25 kN m over the 0.70 m wheel)
constexpr double kVmax = 22.2;               // m/s, 80 km/h (Shift)
constexpr double kVcruise = 11.1;            // m/s, 40 km/h (W alone)
constexpr double kVreverse = 4.2;            // m/s, 15 km/h backwards
constexpr double kWheelBaseHalf = 3.3;       // m, centre to the outer axles (all-wheel steer: the turning lever)
constexpr double kEjectV = 30.0 / 3.6;      // m/s: a stop harder than this throws the people off
constexpr double kRideMax = 0.40, kRideMin = 0.0;    // platform height from its normal: the normal is the lowest (the user), Space raises it, Ctrl brings it back down
// where people are on it (vessel frame): the deck top, its extent, the post, the steps on the left beside the post
constexpr double kDeckY = 0.62, kDeckX = 1.45, kDeckZ0 = -4.1, kDeckZ1 = 3.0, kPostPlateX = 1.0, kPostPlateZ1 = 4.2;
constexpr double kBodyX = 2.20, kBodyZ0 = -4.45, kBodyZ1 = 4.50, kBodyTop = 0.90;   // the outer box people meet
const VECTOR3 kPostFeet = {0.0, kDeckY, 3.30}, kOnDeck = {-1.5, kDeckY, 2.5}, kStepFoot = {-2.9, -0.9, 2.5};
// Э.МПУ (cab_): the cabin's floor is the deck; walls, the console up front, six seats (hips: x, z), the hatch on the left
constexpr double kCabX = 1.55, kCabZ0 = -3.05, kCabZ1 = 2.78;
constexpr double kSeatXZ[6][2] = {{-0.70, 2.25}, {0.70, 2.25}, {-0.70, 1.20}, {0.70, 1.20}, {-0.70, -1.35}, {0.70, -1.35}};
constexpr double kHipsH = 0.72;              // the pelvis above the floor in a seat (as the Tantra's)
const VECTOR3 kYokeHub = {-0.70, kDeckY + 1.02, 2.70}, kYokeX = {1, 0, 0}, kYokeYv = {0, 0.7960, 0.6053}, kYokeZv = {0, 0.6053, -0.7960};
const VECTOR3 kHatchIn = {-1.15, kDeckY, 0.0}, kHatchFoot = {-2.9, -0.9, 0.0};
constexpr double kBattJ = 1.0e3 * 3.6e6;     // 1 MWh: four energy cells
constexpr int kCellsN = 4;
constexpr double kCellJ = kBattJ / kCellsN;  // 250 kWh, 80 kg each: in bays in the right side between the middle axles
constexpr double kCellMass = 80.0;
const double kCellZ[kCellsN] = {0.675, 0.225, -0.225, -0.675};   // their hatches (vessel frame: x 1.30, y -0.05)
const VECTOR3 kSocket = {0.55, 0.15, 4.60};  // the power socket on the front bumper (right)
constexpr double kBaseW = 3.0e3;             // lights, control, cooling
constexpr double kSteerMax = 0.52;           // rad at walking pace (30 deg), less at speed
constexpr double kArmRange = 0.6;            // rad either way, the arm animation
constexpr double kSteerRange = 0.6;          // rad either way, the steering animation

double Clamp(double x, double a, double b) { return x < a ? a : x > b ? b : x; }

// ---------------- terramechanics: the wheel on deformable ground ----------------
// Bekker pressure-sinkage p = (kc/b + kphi) z^n (sinkage and the compaction resistance); Janosi-Hanamoto shear
// tau = (c + sigma tan phi)(1 - e^(-j/K)) integrated over the contact length (traction against slip, the side force
// against the slip angle); the side wall of the rut pushes like a bulldozer blade (Terzaghi's bearing factors).
// Soils: Moon - Lunar Sourcebook trafficability values; Earth - firm loam (Grenville loam, Wong's table, as given in
// NASA TM-20250006958; its K assumed); Mars - our assumption within the rovers' estimates (sand: cohesion up to 1 kPa,
// friction 20-35 deg) with dry sand's compressibility.
// The wheel is airless and gives under load: its patch is that of an equivalent rigid wheel of kWheelRflex.
struct Soil {
    const char* name;
    double n, kc, kphi;    // sinkage exponent; kc N/m^(n+1), kphi N/m^(n+2)
    double c, phi, K;      // cohesion Pa, internal friction rad, shear deformation modulus m
    double rho;            // bulk density kg/m3 (its weight density = rho g on that body)
};
const Soil kMoonSoil  = {"lunar regolith",   1.0, 1400.0, 820000.0,  170.0, 35.0 * RAD, 0.0178, 1500.0};
const Soil kMarsSoil  = {"martian soil",     1.1,  990.0, 1528430.0, 1000.0, 30.0 * RAD, 0.015,  1500.0};
const Soil kEarthSoil = {"firm loam",        1.01,  60.0, 5880000.0, 3100.0, 29.8 * RAD, 0.015,  1700.0};
constexpr double kTyreK = 7.0e5;             // N/m, the airless D1.6 wheel's radial stiffness (5 cm at a full 29 t / 8 on Earth)
constexpr double kWheelRflex = 1.83;        // m, the equivalent rigid wheel of the flattened airless D1.6 wheel (its patch)

struct WheelSoil {          // one wheel on the soil at its load W
    double z, L, A, Hmax, Rc, Rb1;   // sinkage, contact length, contact area, peak shear force, compaction resistance,
};                                   // bulldozing force per unit sin(slip angle)
WheelSoil SoilUnder(const Soil& S, double W, double r, double b, double g) {
    WheelSoil w{};
    const double kk = S.kc / b + S.kphi;
    // rigid-wheel sinkage (Bekker): z = [3W / ((3 - n) kk b sqrt(2r))]^(2/(2n+1))
    w.z = (std::min)(0.35, std::pow(3.0 * W / ((3.0 - S.n) * kk * b * std::sqrt(2.0 * r)), 2.0 / (2.0 * S.n + 1.0)));
    // the contact length: the rut's (soft ground) or the flattened tyre's chord (firm ground: the airless wheel gives under
    // its load, radial stiffness kTyreK - ~2 cm at 11 kN), whichever is longer
    const double defl = (std::min)(W / kTyreK, 0.12), Ltyre = 2.0 * std::sqrt((std::max)(2.0 * mpu::kWheelR * defl - defl * defl, 0.0));
    w.L = (std::max)((std::max)(std::sqrt(2.0 * r * w.z), Ltyre), 0.10);
    w.A = b * w.L;
    w.Hmax = w.A * S.c + W * std::tan(S.phi);
    w.Rc = b * kk * std::pow(w.z, S.n + 1.0) / (S.n + 1.0);
    const double Nq = std::exp(PI * std::tan(S.phi)) * std::pow(std::tan(PI / 4 + S.phi / 2), 2.0);
    const double Nc = (Nq - 1.0) / std::tan(S.phi), Ng = 2.0 * (Nq + 1.0) * std::tan(S.phi);
    w.Rb1 = w.L * (S.c * w.z * Nc + 0.5 * S.rho * g * w.z * w.z * Ng);
    return w;
}
// the shear force at slip s (0..1) over the contact length: H (1 - K/(sL) (1 - e^(-sL/K)))
double ShearAt(const Soil& S, const WheelSoil& w, double s) {
    const double x = s * w.L / S.K;
    if (x < 1e-6) return 0.0;
    return w.Hmax * (1.0 - (1.0 - std::exp(-x)) / x);
}
// the slip that gives the force F (bisection; F above the force at full slip: 1 - the wheel spins)
double SlipFor(const Soil& S, const WheelSoil& w, double F) {
    if (F <= 0.0) return 0.0;
    if (F >= ShearAt(S, w, 1.0)) return 1.0;
    double lo = 0.0, hi = 1.0;
    for (int k = 0; k < 24; ++k) { const double m = 0.5 * (lo + hi); (ShearAt(S, w, m) < F ? lo : hi) = m; }
    return 0.5 * (lo + hi);
}

class TVChassis;
std::vector<TVChassis*>& All() { static std::vector<TVChassis*> v; return v; }

class TVChassis : public VESSEL4 {
public:
    TVChassis(OBJHANDLE h, int fm) : VESSEL4(h, fm) { All().push_back(this); }

    // ---- what the module on the chassis gives (the deck module: post, cabin, tanker...) ----
    virtual void ModuleCaps(FILEHANDLE cfg) { (void)cfg; }               // reads its config before the chassis sets itself up
    virtual double DryMass() const { return kMass; }
    virtual double FuelMass() const { return 0.0; }
    virtual const char* MeshName() const { return "MPU\\MPU"; }
    virtual void CrewInit() {}                                           // registers the module's interior with OrbiterCrew
    virtual OBJHANDLE DriverHold() { return nullptr; }                  // the driver's body while someone drives, else nothing
    virtual double ModuleLoadW(double simdt) { (void)simdt; return 0.0; }  // W the module draws from the cells too (life support...)
    virtual bool ModuleLoad(const char* line) { (void)line; return false; }   // its own scenario lines
    virtual void ModuleSave(FILEHANDLE scn) { (void)scn; }

    void clbkSetClassCaps(FILEHANDLE cfg) override {
        ModuleCaps(cfg);
        SetSize(6.0);
        SetEmptyMass(DryMass());
        if (FuelMass() > 0.0) CreatePropellantResource(FuelMass());
        SetPMI(_V(6.62, 7.92, 1.63));
        SetCrossSections(_V(9.0, 30.0, 8.0));
        SetCW(1.0, 1.0, 1.2, 1.2);
        SetRotDrag(_V(0.5, 0.5, 0.5));
        mesh_ = oapiLoadMeshGlobal(MeshName());
        meshIdx_ = AddMesh(mesh_);
        SetMeshVisibilityMode(meshIdx_, MESHVIS_ALWAYS);
        SetCameraOffset(_V(mpu::kDriverEye[0], mpu::kDriverEye[1], mpu::kDriverEye[2]));
        Contacts(1.62);
        DefineAnimations();
        // the people's attachment point exists from the start: a saved scenario with a person on the deck (ATTACHED 0:0,MPU-1)
        // is resolved before clbkPostCreation - without it the load crashed
        att_ = CreateAttachment(false, _V(0, 0, 0), _V(0, 0, 1), _V(0, 1, 0), "OCINT");
    }

    void clbkPostCreation() override {
        Contacts(LocalG());
        GetHeading(hdgPrev_);
        if (OBJHANDLE ref = GetSurfaceRef()) { VESSELSTATUS2 v0; std::memset(&v0, 0, sizeof v0); v0.version = 2; GetStatusEx(&v0); if (v0.status == 1) SeatOnWheels(ref); }
        CrewInit();
        if (tv_.Load() && tv_.SocketRegister) tv_.SocketRegister(GetHandle(), &kSocket);
        stepsOut_ = park_ ? 1.0 : 0.0;
        snd_ = XRSound::CreateInstance(this);
        if (snd_ && snd_->IsPresent()) {
            using PT = XRSound::PlaybackType;
            snd_->LoadWav(kSndDrive, "XRSound\\MPU\\drive.wav", PT::Global);          // played faster / slower with the speed
            snd_->LoadWav(kSndInverter, "XRSound\\MPU\\inverter.wav", PT::Global);
            snd_->LoadWav(kSndHum, "XRSound\\MPU\\hum.wav", PT::Global);
            snd_->LoadWav(kSndRoll, "XRSound\\MPU\\roll.wav", PT::Global);
            snd_->LoadWav(kSndKnock, "XRSound\\MPU\\knock.wav", PT::Global);
            snd_->LoadWav(kSndLift, "XRSound\\MPU\\lift.wav", PT::Global);
        }
    }

    virtual ~TVChassis() { if (towing_) towing_->towBy_ = nullptr; if (towBy_) towBy_->towing_ = nullptr; if (tv_.Ok() && tv_.SocketUnregister) tv_.SocketUnregister(GetHandle()); delete snd_; auto& a = All(); a.erase(std::remove(a.begin(), a.end(), this), a.end()); if (registered_ && api_.UnregisterInterior) api_.UnregisterInterior(GetHandle()); api_.Unload(); }

    void clbkLoadStateEx(FILEHANDLE scn, void* vs) override {
        char* line;
        while (oapiReadScenario_nextline(scn, line)) {
            if (!std::strncmp(line, "BATT", 4)) { std::sscanf(line + 4, "%lf", &batt_); for (double& c : cell_) c = Clamp(batt_, 0.0, 1.0) * kCellJ; }
            else if (!std::strncmp(line, "TOWING", 6)) { char nm[128] = ""; double L = kBarLen; if (std::sscanf(line + 6, "%127s %lf", nm, &L) >= 1) { towName_ = nm; towL_ = L; } }
            else if (!std::strncmp(line, "CELLS", 5)) {
                double v[kCellsN] = {1, 1, 1, 1};
                std::sscanf(line + 5, "%lf %lf %lf %lf", &v[0], &v[1], &v[2], &v[3]);
                for (int k = 0; k < kCellsN; ++k) cell_[k] = v[k] < 0 ? -1.0 : Clamp(v[k], 0.0, 1.0) * kCellJ;
                batt_ = CellsSum() / kBattJ;
            }
            else if (!std::strncmp(line, "PARK", 4)) { int p = 1; std::sscanf(line + 4, "%d", &p); park_ = p != 0; }
            else if (!std::strncmp(line, "DAMAGE", 6)) {
                std::sscanf(line + 6, "%lf %lf %lf %lf %lf %lf %lf %lf %lf", &hullLeak_, &wheelHp_[0], &wheelHp_[1], &wheelHp_[2], &wheelHp_[3],
                            &wheelHp_[4], &wheelHp_[5], &wheelHp_[6], &wheelHp_[7]);
                hullLeak_ *= 1e-4;                                                // written in cm2
            }
            else if (ModuleLoad(line)) {}
            else ParseScenarioLineEx(line, vs);
        }
    }

    void clbkSaveState(FILEHANDLE scn) override {
        VESSEL4::clbkSaveState(scn);
        char cb[64]; std::snprintf(cb, sizeof cb, "%.3f %.3f %.3f %.3f", cell_[0] < 0 ? -1.0 : cell_[0] / kCellJ, cell_[1] < 0 ? -1.0 : cell_[1] / kCellJ,
                                   cell_[2] < 0 ? -1.0 : cell_[2] / kCellJ, cell_[3] < 0 ? -1.0 : cell_[3] / kCellJ);
        oapiWriteScenario_string(scn, const_cast<char*>("CELLS"), cb);
        if (towing_) { char tb[160]; std::snprintf(tb, sizeof tb, "%s %.2f", towing_->GetName(), towL_); oapiWriteScenario_string(scn, const_cast<char*>("TOWING"), tb); }
        oapiWriteScenario_int(scn, const_cast<char*>("PARK"), park_ ? 1 : 0);
        bool hurt = hullLeak_ > 0.0; for (double h : wheelHp_) hurt |= h < 1.0;
        if (hurt) {
            char db[160]; std::snprintf(db, sizeof db, "%.2f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f", hullLeak_ * 1e4, wheelHp_[0], wheelHp_[1], wheelHp_[2],
                                        wheelHp_[3], wheelHp_[4], wheelHp_[5], wheelHp_[6], wheelHp_[7]);
            oapiWriteScenario_string(scn, const_cast<char*>("DAMAGE"), db);
        }
        ModuleSave(scn);
    }


    void clbkPreStep(double simt, double simdt, double mjd) override {
        (void)simt; (void)mjd;
        if (simdt <= 0.0) return;
        OBJHANDLE ref = GetSurfaceRef();
        if (!ref) return;
        const double g = LocalG();
        if (std::fabs(g - gSet_) > 0.05 * gSet_) Contacts(g);
        const double R = oapiGetSize(ref);
        // the start: the terrain is not loaded at once (the user) - the elevation under it keeps changing while its tiles
        // arrive. Until it has stood still for 1 s (and the simulation has run 1 s; 15 s at most) the platform is held
        // landed on its wheels each step: no physics, no drive, no blows
        if (holding_) {
            double lng, lat, rad;
            GetEquPos(lng, lat, rad);
            const double e = oapiSurfaceElevation(ref, lng, lat);
            stableT_ = std::fabs(e - elevPrev_) < 0.005 ? stableT_ + simdt : 0.0;
            elevPrev_ = e;
            holdT_ += simdt;
            if ((stableT_ > 1.0 && simt > 1.0) || holdT_ > 15.0) holding_ = false;
            if (selftest_) {
                char b[200];
                std::snprintf(b, sizeof b, "MPU hold t %.2f st %d alt %.3f elev %.3f stable %.2f pitch %.1f bank %.1f holding %d", simt, st0Status(),
                              GetAltitude(ALTMODE_GROUND), e, stableT_, GetPitch() * DEG, GetBank() * DEG, holding_ ? 1 : 0);
                oapiWriteLog(b);
            }
            if (holding_) {
                SeatOnWheels(ref);
                Animate();
                Steps(simdt);
                return;
            }
        }

        // the driver's input: only a person at the post, only while the focus is on that person and Orbiter has the keyboard
        double driveIn = 0.0, steerIn = 0.0;
        bool full = false, up = false, down = false;
        const OBJHANDLE driverBody = DriverHold();
        driverBody_ = driverBody;
        if (driverBody && driverBody == oapiGetFocusObject() && OurWindow()) {
            auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
            const bool w = held('W'), sk = held('S'), a = held('A'), d = held('D');
            full = held(VK_SHIFT);
            brakeHeld_ = held(VK_SPACE);                                          // Space: the brake (the user)
            const bool caps = held(VK_CAPITAL);                                   // Caps Lock: the platform up / back down
            if (caps && !capsWas_) rideTarget_ = rideTarget_ > 0.5 * kRideMax ? kRideMin : kRideMax;
            capsWas_ = caps;
            driveIn = brakeHeld_ ? 0.0 : w ? 1.0 : sk ? -1.0 : 0.0;
            steerIn = (d ? 1.0 : 0.0) - (a ? 1.0 : 0.0);
            const bool b = held('B');
            if (b && !bWas_) park_ = !park_;
            bWas_ = b;
            // V: the platform from outside (Orbiter's external camera on it: mouse, F2); V again - the driver's own view.
            // The focus and the keys stay with the person (OrbiterCrew's own V works only in a seated helm)
            const bool vk = !cab_ && held('V') && !held(VK_SHIFT);
            if (vk && !vWas_) {
                if (oapiCameraTarget() == GetHandle()) oapiCameraAttach(driverBody, 0);   // 0 internal: her eyes
                else oapiCameraAttach(GetHandle(), 1);                                     // 1 external: the platform
            }
            vWas_ = vk;
        }
        // in a train behind a leader, nobody at our own post: we follow it - its drive, brakes and parking brake; our wheels
        // steer along the tow bar (the trailer tracks the leader's path)
        TVChassis* lead = towBy_;
        const bool following = lead && !driverBody;
        if (following) {
            driveIn = lead->drive_; full = lead->vCap_ >= kVmax; park_ = lead->park_;
            VECTOR3 pg, pl;
            lead->Local2Global(_V(mpu::kHitchR[0], mpu::kHitchR[1], mpu::kHitchR[2]), pg);
            Global2Local(pg, pl);                                                 // the leader's rear pin, in our frame
            steerIn = Clamp(2.0 * std::atan2(pl.x - mpu::kHitchF[0], pl.z - mpu::kHitchF[2]), -1.0, 1.0);
        }
        if (!driverBody && !following && std::fabs(speed_) < 0.3) park_ = true;  // nobody at the post: it stands braked
        const double ride0 = ride_;
        if (!driverBody) brakeHeld_ = following ? lead->brakeHeld_ : false;
        (void)up; (void)down;
        ride_ += Clamp(rideTarget_ - ride_, -0.15 * simdt, 0.15 * simdt);       // the platform moves to where Caps Lock set it
        rideMoving_ = ride_ != ride0;
        if (std::fabs(ride_ - rideSet_) > 0.01) Contacts(g);
        vCap_ = full ? kVmax : kVcruise;
        if (selftest_ == 2) park_ = false;
        if (selftest_ == 3) { testT_ += simdt; }
        else if (selftest_ == 4) {                                                // =4: every platform full speed straight ahead
            testT_ += simdt;
            if (testT_ > 1.0) { driveIn = 1.0; full = true; vCap_ = kVmax; }
        }
        else if (selftest_ == 5) {                                                // =5: as 4 for 10 s, then hands off: it must stand still
            testT_ += simdt;
            if (testT_ > 1.0 && testT_ < 10.0) { driveIn = 1.0; full = true; vCap_ = kVmax; }
        }
        else if (selftest_ == 1) {                                                     // MPU_SELFTEST=1: drive a fixed course, log
            testT_ += simdt;
            driveIn = testT_ < 2.0 ? 0.0 : testT_ < 22.0 ? 1.0 : testT_ < 30.0 ? -1.0 : 0.0;
            steerIn = testT_ > 12.0 && testT_ < 18.0 ? 0.6 : 0.0;
        } else if (selftest_ == 2) {                                              // =2: a person aboard, full speed into the nearest leg
            testT_ += simdt;
            if (testT_ > 1.0 && !boarded_ && api_.Ok() && api_.EnterShip) {
                for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
                    OBJHANDLE h = oapiGetVesselByIndex(i);
                    if (!api_.PersonOfBody(h)) continue;
                    const VECTOR3 at = _V(0.0, kDeckY, 0.0), dir = _V(0, 0, 1);
                    boarded_ = api_.EnterShip(h, GetHandle(), &at, &dir) != 0;
                    char b[120]; std::snprintf(b, sizeof b, "MPU test: %s boarded: %d", oapiGetVesselInterface(h)->GetName(), boarded_ ? 1 : 0); oapiWriteLog(b);
                    break;
                }
            }
            VECTOR3 tl;
            if (testT_ > 3.0 && NearestLeg(tl)) {
                driveIn = 1.0; full = true; vCap_ = kVmax;
                steerIn = Clamp(std::atan2(tl.x, tl.z) * 1.5, -1.0, 1.0);
                if (std::fabs(std::atan2(tl.x, tl.z)) > 0.5 && std::fabs(speed_) > 4.0) driveIn = -1.0;   // slow down to turn
            }
        }
        steer_ += Clamp(steerIn - steer_, -4.0 * simdt, 4.0 * simdt);          // the steering: quick (a quarter second lock to lock)
        drive_ = driveIn;
        absBrake_ = drive_ < 0.0 && speed_ > 0.5;                                  // S while going forward: the service brake (ABS)
        if (drive_ > 0.0 && speed_ < -0.5) absBrake_ = true;                       // W while rolling back: the same
        if (absBrake_) drive_ = 0.0;
        if (std::fabs(drive_) > 0.05 && park_) park_ = false;                    // driving off releases the brake

        VESSELSTATUS2 st;
        std::memset(&st, 0, sizeof st);
        st.version = 2;
        GetStatusEx(&st);
        if (st.status != 1 && !drivingMode_) { drivingMode_ = true; Contacts(g); for (double& q : xPrev_) q = -1e9; }
        softBlend_ = st.status != 1 ? (std::min)(1.0, softBlend_ + simdt) : 0.0;   // standing: stiff; moving: the soft ride eases in      // moving: our springs, Orbiter's as stops
        VECTOR3 v;
        GetGroundspeedVector(FRAME_LOCAL, v);
        double hdg;
        GetHeading(hdg);
        double dh = hdg - hdgPrev_;
        if (dh > PI) dh -= PI2; else if (dh < -PI) dh += PI2;
        hdgPrev_ = hdg;
        yawRate_ = dh / simdt;
        speed_ = v.z;

        // where each wheel meets the ground
        double nSum = 0.0;
        for (int i = 0; i < mpu::kWheels; ++i) {
            const auto& W = mpu::kWheel[i];
            VECTOR3 c = _V(W.hub[0], W.hub[1] - mpu::kWheelR - ride_, W.hub[2]), gp;
            Local2Global(c, gp);
            double lng, lat, rad;
            oapiGlobalToEqu(ref, gp, &lng, &lat, &rad);
            height_[i] = rad - (R + oapiSurfaceElevation(ref, lng, lat));
            const double x = -height_[i];                                        // compression from the free wheel
            // above 2x time warp the step is too long for our soft spring (it pumped the body over): Orbiter's stiff
            // points alone, which it integrates itself
            if (st.status != 1 && drivingMode_ && oapiGetTimeAcceleration() <= 2.0) {
                // the soft ride: Orbiter's point pushes k_ x (stiff, its static sag kSag); we take (k_ - kS)(x - kSag) off it
                // and add our damper - together a preloaded soft spring (the static load at kSag, kTravel of soft travel):
                // the body squats, dives, rolls and rocks on it
                const double m = GetMass(), kS = m * g / (mpu::kWheels * kTravel);
                const double cS = 2.0 * 0.30 * std::sqrt(kS * m / mpu::kWheels);   // light damping: it squats and rocks
                const double xd = xPrev_[i] < -1e8 ? 0.0 : (x - xPrev_[i]) / simdt;
                if (x > 0.0) {
                    const double orb = k_ * (std::min)(x, 0.6);
                    double add = (-(k_ - kS) * (x - kSag) + cS * xd) * softBlend_;   // eased in over 1 s after it starts
                    add = Clamp(add, -orb, 4.0 * m * g / mpu::kWheels);                // never pulling the wheel off the ground
                    susp_[i] = add;
                    n_[i] = orb + add;
                } else { susp_[i] = 0.0; n_[i] = 0.0; }
            } else { susp_[i] = 0.0; n_[i] = x > 0.0 ? k_ * (std::min)(x, 0.6) : 0.0; }   // no stale soft force left
            xPrev_[i] = x;
            nSum += n_[i];
        }

        moduleW_ = ModuleLoadW(simdt);
        double powerW = kBaseW + moduleW_;
        for (double& x : vf_) x = 0.0;
        if (st.status != 1) {
            const double vAbs = std::fabs(v.z);
            const Soil& S = SoilOf(ref);
            // every wheel on the soil at its load: sinkage, contact, the peak shear force, the resistances
            WheelSoil ws[mpu::kWheels];
            double tractMax = 0.0, wSum = 0.0;
            for (int i = 0; i < mpu::kWheels; ++i) {
                ws[i] = SoilUnder(S, (std::max)(n_[i], 1.0), kWheelRflex, mpu::kWheelW, g);
                if (n_[i] > 0.0) { tractMax += ShearAt(S, ws[i], 1.0) - ws[i].Rc; wSum += n_[i]; }
            }
            muEff_ = wSum > 0.0 ? tractMax / wSum : 0.3;                          // what this ground gives, per newton of load
            sinkMean_ = 0.0;
            // the steering the grip allows: lateral acceleration v^2 * delta / L within the ground's grip
            // the steering at speed: up to 2.5x what the grip holds (the user: sharper) - past the grip it slides, the physics
            // decides; never under ~9 deg
            const double sMax = Clamp(2.5 * kWheelBaseHalf * (std::max)(0.1, muEff_) * g / (std::max)(vAbs * vAbs, 1.0), 0.16, kSteerMax);
            const double rearK = -0.6 * (1.0 - Clamp(vAbs / 12.0, 0.0, 1.0));   // rear axles counter-steer at low speed
            // the motors' force wanted in total: power limited, nothing past the top speed (the ground decides what it gives)
            double fCmd = drive_ * kPmax / (std::max)(vAbs, 2.0);
            fCmd = Clamp(fCmd, -kFmotor, kFmotor);
            // the speed limit holds both ways, downhill too: above it the motors brake (and give the energy back). Forward:
            // 40 km/h on W, 80 with Shift, 80 rolling free; backwards: 15 on S, 80 rolling free
            {
                const double m = GetMass();
                const double capF = drive_ > 0.05 ? vCap_ : kVmax, capR = drive_ < -0.05 ? kVreverse : kVmax;
                if (v.z > capF) fCmd = -Clamp(m * 1.5 * (v.z - capF), 0.0, kFmotor);
                else if (v.z < -capR) fCmd = Clamp(m * 1.5 * (-capR - v.z), 0.0, kFmotor);
                else if (fCmd > 0.0 && v.z > capF - 0.3) fCmd *= Clamp((capF - v.z) / 0.3, 0.0, 1.0);
                else if (fCmd < 0.0 && v.z < -capR + 0.3) fCmd *= Clamp((capR + v.z) / 0.3, 0.0, 1.0);
            }
            const bool brake = park_ || brakeHeld_ || (std::fabs(drive_) < 0.05 && vAbs < 0.4);
            for (int i = 0; i < mpu::kWheels; ++i) {
                const auto& W = mpu::kWheel[i];
                const double axle = W.hub[2];
                double d = steer_ * sMax * (std::fabs(axle) > 2.5 ? 1.0 : 0.5);
                if (axle < 0.0) d *= rearK;
                delta_[i] = d;
                sink_[i] = n_[i] > 0.0 ? ws[i].z : 0.0;
                sinkMean_ += sink_[i] / mpu::kWheels;
                if (n_[i] <= 0.0) { slip_[i] = 0.0; continue; }
                const double vx = v.x + yawRate_ * axle, vz = v.z - yawRate_ * W.hub[0];
                const double sd = std::sin(d), cd = std::cos(d);
                const double vf = vx * sd + vz * cd, vl = vx * cd - vz * sd;   // along / across the wheel
                const WheelSoil& w = ws[i];
                // along the wheel: the motor's share drives it through the soil's shear (Janosi-Hanamoto) at the slip that
                // gives it; more than the soil holds - the wheel spins (slip 1) and gives what it can. Braking: the wheel
                // locked, skidding (slip 1 against the motion). Always: the compaction of the rut and the tread's loss
                double ff = 0.0, sl = 0.0;
                if (brake || wheelHp_[i] < 0.15) {                                // the brake - or a wrecked wheel: locked, it skids
                    sl = -1.0;
                    ff = -ShearAt(S, w, 1.0) * std::tanh(vf / 0.2);
                } else if (absBrake_) {                                           // S: the service brake with ABS - the best slip,
                    sl = -kSlipABS;                                               // no lock, the wheels still steer
                    ff = -ShearAt(S, w, kSlipABS) * std::tanh(vf / 0.3);
                } else if (fCmd != 0.0) {
                    double want = std::fabs(fCmd) * n_[i] / (std::max)(wSum, 1.0) * wheelHp_[i] + w.Rc;   // a damaged hub gives less
                    // traction control on W: no more than the soil gives at 30 % slip (no digging); Shift: none - it spins
                    if (vCap_ < kVmax && fCmd * vf >= 0.0) want = (std::min)(want, ShearAt(S, w, kSlipTC));
                    sl = SlipFor(S, w, want);
                    ff = (fCmd > 0 ? 1.0 : -1.0) * (std::min)(want, ShearAt(S, w, 1.0));
                    if (fCmd * vf < 0.0) sl = -sl;                                  // the motors against the motion: braking slip
                }
                ff -= (w.Rc + 0.01 * n_[i] * (1.0 + 4.0 * (1.0 - wheelHp_[i]))) * std::tanh(vf / 0.3);   // a bent wheel drags
                slip_[i] = sl;
                // across the wheel: the soil's shear against the slip angle, and the rut's side wall (bulldozing)
                const double alpha = std::atan2(std::fabs(vl), std::fabs(vf) + 0.3);
                const double xs = w.L * std::tan(alpha) / S.K;
                const double fyShear = xs > 1e-6 ? w.Hmax * (1.0 - (1.0 - std::exp(-xs)) / xs) : 0.0;
                const double fyMax = w.Hmax + w.Rb1;
                double fl = -(vl > 0 ? 1.0 : -1.0) * (fyShear + w.Rb1 * std::sin(alpha));
                // the side force first (stability): along the wheel what the soil has left
                fl = Clamp(fl, -fyMax, fyMax);
                const double fRest = std::sqrt((std::max)(0.0, w.Hmax * w.Hmax - (std::min)(fl * fl, w.Hmax * w.Hmax)));
                ff = Clamp(ff, -fRest - w.Rc, fRest + w.Rc);
                const VECTOR3 F = _V(ff * sd + fl * cd, susp_[i], ff * cd - fl * sd);   // the soil and our part of the spring
                AddForce(F, _V(W.hub[0], W.hub[1] - mpu::kWheelR - ride_, W.hub[2]));
                // the wheel's rim speed: faster than the ground when driving with slip, stopped when locked
                const double rim = sl >= 0.999 ? (vf + (fCmd >= 0 ? 1.0 : -1.0) * 6.0)
                                 : sl > 0.0 ? vf / (1.0 - sl) : sl <= -0.999 ? 0.0 : vf * (1.0 + sl);   // ABS: turning, a little slower
                if (!brake) {                                                     // driving or ABS braking by the motors
                    const double p = ff * rim;                                    // the motors' power through the slip
                    powerW += p > 0.0 ? p / 0.92 : p * 0.6;
                }
                spin_[i] = std::fmod(spin_[i] + rim * simdt / mpu::kWheelR, PI2);
                vf_[i] = vf;
            }
            // standing still with no drive asked for (half a second): Orbiter's landed state - no physics, it does not
            // move or tremble (the tyre forces round zero speed made it twitch along the wheels). Time warp above 10x:
            // landed at once, nothing computed
            const bool contact = nSum > 0.0;
            still_ = std::fabs(drive_) < 0.05 && contact && length(v) < 0.15 ? still_ + simdt : 0.0;
            if (still_ > 0.5 || (contact && oapiGetTimeAcceleration() > 4.0)) Rest(ref);
        } else if (std::fabs(drive_) > 0.05 && batt_ > 0.0 && oapiGetTimeAcceleration() <= 4.0) {
            // drive asked: out of the landed state. A small force did not always wake it (it stayed until the deck height was
            // changed - that sets the touchdown points again): setting them again leaves the landed state every time
            drivingMode_ = true;
            SeatOnWheels(ref);                                                    // out of the levelled pose: back on the ground's plane first
            Contacts(g);
            for (double& q : xPrev_) q = -1e9;                                    // no damper kick on the first step
        }
        powerW_ = powerW;
        powerAvg_ += (powerW - powerAvg_) * Clamp(simdt / 30.0, 0.0, 1.0);    // the draw over the last half minute
        CellsUse(powerW * simdt);                                                // from the cells (the motors' regeneration back in)
        chargeW_ = 0.0;
        if (tv_.Ok() && tv_.PluggedTo(GetHandle())) {                            // the cable: the station charges, the emptiest cell first
            const double room = CellsRoom();
            if (room > 1.0) { const double e = tv_.Draw(GetHandle(), room / simdt, simdt); CellsCharge(e); chargeW_ = e / simdt; }
        }
        batt_ = CellsSum() / kBattJ;
        if (batt_ <= 0.0) drive_ = 0.0;
        Animate();
        Steps(simdt);
        Contacts_People(simdt);
        Obstacles(simdt);
        Vehicles(simdt);
        TowLink();
        TowForce();
        DrawBar();
        Dust();
        Sound(simdt);
        GearShock(simdt);
        // the driver's row is in the person's HUD now (OrbiterCrew SeatGauges, MPU.cpp); the old debug line is gone
        wasDriving_ = driverBody && driverBody == oapiGetFocusObject();
        if (selftest_ && (logT_ += simdt) >= (testT_ < 3.0 ? 0.05 : 0.5)) {
            logT_ = 0.0;
            double pitch = GetPitch() * DEG, bank = GetBank() * DEG;
            char b[900];
            int n = std::snprintf(b, sizeof b, "MPU t %.1f st %d alt %.3f v %.2f pitch %.1f bank %.1f drive %.2f steer %.2f solids %d leg %.1f m |", testT_, st.status,
                                  GetAltitude(ALTMODE_GROUND), v.z, pitch, bank, drive_, steer_, tgtN_, tgtDist_);
            n += std::snprintf(b + n, sizeof b - n, " dm %d mu %.2f sink %.3f slip0 %.2f slip7 %.2f", drivingMode_ ? 1 : 0, muEff_, sinkMean_, slip_[0], slip_[7]);
            for (int i = 0; i < mpu::kWheels; ++i) n += std::snprintf(b + n, sizeof b - n, " h%d %.3f N %.0f", i, height_[i], n_[i]);
            oapiWriteLog(b);
        }
    }

    bool clbkDrawHUD(int mode, const HUDPAINTSPEC* hps, oapi::Sketchpad* skp) override {
        VESSEL4::clbkDrawHUD(mode, hps, skp);
        char buf[160];
        const int x = hps->W / 40, y = hps->H - hps->H / 6, dy = hps->H / 30;
        if (selftest_) { const char* t = "SELF-TEST: the MPU drives a fixed course, controls off"; skp->Text(x, y - 2 * dy, t, (int)std::strlen(t)); }
        std::snprintf(buf, sizeof buf, "%s   %+.0f km/h   battery %.0f %%   power %.2f MW", cab_ ? "EMPU" : "MPU", speed_ * 3.6, batt_ * 100.0, powerW_ * 1e-6);
        skp->Text(x, y, buf, (int)std::strlen(buf));
        std::snprintf(buf, sizeof buf, "steer %+.0f deg   drive %s   %s", steer_ * kSteerMax * DEG, drive_ > 0.05 ? "forward" : drive_ < -0.05 ? "back/brake" : "-",
                      park_ ? "PARKING BRAKE (B)" : "");
        skp->Text(x, y + dy, buf, (int)std::strlen(buf));
        char bar[64] = "suspension ";
        for (int i = 0; i < mpu::kWheels; ++i) {
            const double c = height_[i] < 0.0 ? -height_[i] : 0.0;
            std::strcat(bar, c < 0.005 ? "_" : c < kSag * 0.7 ? "." : c < kSag * 1.4 ? ":" : "|");
        }
        skp->Text(x, y + 2 * dy, bar, (int)std::strlen(bar));
        return true;
    }

public:
    // the range at this speed: what driving on at it costs - the draw averaged over half a minute, never less than the
    // rolling resistance of this mass on soil (Crr 0.05, motors 85 %) plus what the module draws (life support, heating).
    // Coasting downhill the draw is near nothing: the range from it alone came out in thousands of km (the user)
    double RangeKm() const {
        const double v = std::fabs(speed_);
        if (v < 1.0) return -1.0;
        const double roll = 0.05 * GetMass() * LocalG() * v / 0.85;
        const double P = (std::max)(powerAvg_, roll + kBaseW + moduleW_);
        return batt_ * kBattJ / P * v / 1000.0;
    }
    double powerAvg_ = 0.0, moduleW_ = 0.0;
    // for the suit's helmet display (mpuDriverState): this platform's state while 'person' is at its post
    bool DriverState(OBJHANDLE person, ::MpuState* s);
    // a command from the person's suit computer (its MFD page «МАШИНА»): 1 the parking brake on/off, 2 the platform up/down
    bool DriverCommand(OBJHANDLE person, int cmd) {
        if (!person || driverBody_ != person) return false;
        if (cmd == 1) { park_ = !park_; return true; }
        if (cmd == 2) { rideTarget_ = rideTarget_ > 0.5 * kRideMax ? kRideMin : kRideMax; return true; }
        return false;
    }
    OBJHANDLE DriverBody() const { return driverBody_; }

protected:
    static TVChassis* Self(void* c) { return static_cast<TVChassis*>(c); }

    // ---------------- energy cells and the power socket ----------------
    double CellsSum() const { double s = 0; for (double c : cell_) if (c > 0) s += c; return s; }
    double CellsRoom() const { double s = 0; for (double c : cell_) if (c >= 0) s += kCellJ - c; return s; }
    void CellsUse(double E) {                                       // E > 0 drawn (shared by the charge), E < 0 given back
        if (E < 0) { CellsCharge(-E); return; }
        const double tot = CellsSum();
        if (tot <= 0) return;
        for (double& c : cell_) if (c > 0) c = (std::max)(0.0, c - E * c / tot);
    }
    void CellsCharge(double E) {
        for (int pass = 0; pass < kCellsN && E > 1.0; ++pass) {
            int lo = -1;
            for (int k = 0; k < kCellsN; ++k) if (cell_[k] >= 0 && cell_[k] < kCellJ && (lo < 0 || cell_[k] < cell_[lo])) lo = k;
            if (lo < 0) return;
            const double e = (std::min)(E, kCellJ - cell_[lo]); cell_[lo] += e; E -= e;
        }
    }
    // ---------------- the coupling: a tow bar between this vehicle's rear pin and the follower's front pin ----------------
    // A stiff spring with a damper along the bar (its length set when coupled), the pins free to turn: the train jack-knifes in
    // a turn, pulls and pushes. Each vehicle applies the bar's force to itself (equal and opposite). The leader draws the bar.
    TVChassis* towBy_ = nullptr;          // the leader in front of us
    TVChassis* towing_ = nullptr;         // the follower behind us
    // a rigid drawbar of a fixed length between the pins (room for the bumpers to swing past each other in a turn); coupled
    // from 2..4 m apart, the bar then draws them to its length at a walking pace (towL_ eases to kBarLen)
    static constexpr double kBarLen = 3.0, kBarMin = 2.0, kBarMax = 4.0;
    double towL_ = kBarLen;               // the bar's length as it stands now, m
    std::string towName_;                 // from the scenario: the follower's name, linked on the first step
    static VECTOR3 V3(const double* p) { return _V(p[0], p[1], p[2]); }
    double PinGap(TVChassis* o, bool rear) const {                      // our rear (front) pin to its front (rear) pin
        VECTOR3 a, b;
        Local2Global(V3(rear ? mpu::kHitchR : mpu::kHitchF), a);
        o->Local2Global(V3(rear ? mpu::kHitchF : mpu::kHitchR), b);
        return length(b - a);
    }
    TVChassis* Candidate(bool rear, double* gap) const {               // the nearest free vehicle by that pin, 2..4 m off
        TVChassis* best = nullptr; double bd = kBarMax;
        for (TVChassis* o : All()) {
            if (o == this || (rear ? o->towBy_ : o->towing_)) continue;
            const double d = PinGap(o, rear);
            if (d >= kBarMin && d < bd) { bd = d; best = o; }
        }
        if (gap) *gap = bd;
        return best;
    }
    void Couple(TVChassis* follower, double gap) {
        towing_ = follower; follower->towBy_ = this;
        towL_ = follower->towL_ = Clamp(gap, kBarMin, kBarMax);
        oapiWriteLogV("TVChassis: %s coupled to %s, bar %.2f m", follower->GetName(), GetName(), towL_);
    }
    void Uncouple() {
        if (towing_) { oapiWriteLogV("TVChassis: %s uncoupled from %s", towing_->GetName(), GetName()); towing_->towBy_ = nullptr; towing_ = nullptr; }
        barDrawn_ = -1;
    }
    void TowLink() {
        if (towName_.empty()) return;
        for (TVChassis* o : All()) if (o != this && towName_ == o->GetName()) { towing_ = o; o->towBy_ = this; o->towL_ = towL_; break; }
        towName_.clear();
    }
    void Wake() {                                                       // out of the parked rest (as when the driver drives off)
        if (drivingMode_) return;
        OBJHANDLE ref = GetSurfaceRef();
        if (!ref) return;
        drivingMode_ = true;
        SeatOnWheels(ref);
        Contacts(LocalG());
        for (double& q : xPrev_) q = -1e9;
    }
    void TowForce() {
        if (towing_) {                                                  // the bar settles to its length (0.3 m/s)
            const double st = 0.3 * oapiGetSimStep();
            towL_ += Clamp(kBarLen - towL_, -st, st); towing_->towL_ = towL_;
        }
        auto pull = [&](TVChassis* o, const double* mine, const double* theirs) {
            VECTOR3 a, b, va, vb;
            Local2Global(V3(mine), a); o->Local2Global(V3(theirs), b);
            GetGlobalVel(va); o->GetGlobalVel(vb);
            const VECTOR3 d = b - a; const double L = length(d);
            if (L < 1e-4) return;
            const VECTOR3 n = d / L;
            const double m1 = GetMass(), m2 = o->GetMass(), mr = m1 * m2 / (m1 + m2);
            const double k = mr * 12.6 * 12.6, c = 2.0 * 0.8 * std::sqrt(k * mr);   // ~2 Hz, well damped
            const double f = Clamp(k * (L - towL_) + c * dotp(vb - va, n), -4.0e5, 4.0e5);
            if (std::fabs(f) > 0.15 * mr * LocalG()) { Wake(); o->Wake(); }     // a real pull or push: both out of the parked rest
            if (!drivingMode_) return;
            MATRIX3 R; GetRotationMatrix(R);
            AddForce(tmul(R, n * f), V3(mine));
        };
        if (towBy_) pull(towBy_, mpu::kHitchF, mpu::kHitchR);
        if (towing_) pull(towing_, mpu::kHitchR, mpu::kHitchF);
    }
    void DrawBar() {
        if (!dev_ || bar_.rest.empty()) return;
        const int state = towing_ ? 1 : 0;
        if (!state && barDrawn_ == 0) return;
        barDrawn_ = state;
        const VECTOR3 A = V3(mpu::kHitchR);
        VECTOR3 B = A;
        if (towing_) { VECTOR3 g; towing_->Local2Global(V3(mpu::kHitchF), g); Global2Local(g, B); }
        const VECTOR3 d = B - A; const double L = (std::max)(length(d), 1e-4);
        const VECTOR3 e3 = d / L;
        VECTOR3 e1 = crossp(_V(0, 1, 0), e3); e1 = length(e1) > 1e-4 ? unit(e1) : _V(1, 0, 0);
        const VECTOR3 e2 = crossp(e3, e1);
        for (size_t k = 0; k < bar_.rest.size(); ++k) {
            const NTVERTEX& v0 = bar_.rest[k]; NTVERTEX& v = bar_.work[k];
            const double t = v0.z;
            const VECTOR3 p = state ? A + d * t + e1 * v0.x + e2 * (v0.y + mpu::kCgH) : A;
            v.x = (float)p.x; v.y = (float)p.y; v.z = (float)p.z;
        }
        GROUPEDITSPEC e{}; e.flags = GRPEDIT_VTXCRD; e.Vtx = bar_.work.data(); e.nVtx = (DWORD)bar_.work.size();
        oapiEditMeshGroup(dev_, (DWORD)bar_.grp, &e);
    }
    int barDrawn_ = -1;


    // ---------------- damage (the user, 2026-10-07): the pressure hull's tightness (Э.МПУ) and the running gear ----------------
    // A blow's speed change for this vehicle (the masses: another vehicle takes its share, a ship's leg does not give). The
    // wheel nearest the blow (its rim, blades, arms) loses health by ((dv - 3 m/s) / 9)^2; a hard landing on a wheel too
    // (its vertical speed over 4 m/s). The hull: a blow straight at the cabin (above the deck) holes it from 10 km/h, one
    // through the frame from 30 km/h - the hole's area grows with the square of the excess (50 km/h through the frame ~2 cm2).
    // A damaged wheel drives with what it has left and drags more; below 15 % it is wrecked - it drags locked.
    double hullLeak_ = 0.0;                                             // m2 of holes in the pressure hull
    double wheelHp_[mpu::kWheels] = {1, 1, 1, 1, 1, 1, 1, 1};
    double gearH_[mpu::kWheels] = {}, gearInit_ = 0.0;
    virtual void Occupants(const VECTOR3& nrmLocal, double dv) { (void)nrmLocal; (void)dv; }   // the module's people feel the crash
    void Damage(const VECTOR3& cp, const VECTOR3& nrm, double dv, double height) {
        if (dv <= 0.0) return;
        int wi = -1; double wd = 1.6;
        for (int i = 0; i < mpu::kWheels; ++i) {
            const double d = std::hypot(cp.x - mpu::kWheel[i].hub[0], cp.z - mpu::kWheel[i].hub[2]);
            if (d < wd) { wd = d; wi = i; }
        }
        if (wi >= 0 && dv > 3.0) {
            const double hit = Clamp(std::pow((dv - 3.0) / 9.0, 2.0), 0.0, 1.0);
            wheelHp_[wi] = (std::max)(0.0, wheelHp_[wi] - hit);
            oapiWriteLogV("TVChassis %s: wheel %d hit at %.1f m/s - health %.0f %%", GetName(), wi + 1, dv, wheelHp_[wi] * 100.0);
        }
        if (cab_) {
            const double v0 = height > kDeckY ? 10.0 / 3.6 : 30.0 / 3.6;
            if (dv > v0) {
                hullLeak_ += 2.0e-4 * std::pow((dv - v0) / 5.5, 2.0);
                oapiWriteLogV("TVChassis %s: the pressure hull holed (%.1f m/s) - leak %.1f cm2", GetName(), dv, hullLeak_ * 1e4);
            }
        }
        Occupants(nrm, dv);
    }
    void GearShock(double dt) {                                         // a wheel slammed down (a fall, a step off a rock)
        if (dt <= 0.0) return;
        if (gearInit_ < 1.0) { for (int i = 0; i < mpu::kWheels; ++i) gearH_[i] = height_[i]; gearInit_ += dt; return; }
        for (int i = 0; i < mpu::kWheels; ++i) {
            const double vz = std::fabs(height_[i] - gearH_[i]) / dt;
            gearH_[i] = height_[i];
            if (vz > 4.0 && n_[i] > 0.0 && oapiGetTimeAcceleration() <= 4.0) {
                const double hit = Clamp(std::pow((vz - 4.0) / 6.0, 2.0), 0.0, 1.0) * dt * 20.0;
                wheelHp_[i] = (std::max)(0.0, wheelHp_[i] - hit);
            }
        }
    }

    // the chassis' own F items (ids 200..): the socket, the four cell hatches, the front and rear couplings - from outside,
    // as OrbiterCrew's entrances
    static constexpr int kItemSocket = 200, kItemCell0 = 201, kItemHitchF = 205, kItemHitchR = 206;
    int ChassisItemCount() const { return 0; }          // (the socket, cells and couplings are context nodes now - below)

    // ---------------- context actions (OrbiterCrew F-1 / M-1): the socket, the four cell bays, the couplings ----------------
    // Outside nodes (the vessel frame). Long actions run their time and act at the end (End completed); broken off - nothing.
    enum { kNodeSocket = 1, kNodeCell0 = 11, kNodeHitchF = 21, kNodeHitchR = 22 };
    enum { kActPlug = 1, kActUnplug, kActCellOut, kActCellIn, kActCouple, kActUncouple };
    int ChassisNodeCount() const { return 3 + kCellsN; }
    // the menu's language (OrbiterCrew's one setting, Config\OrbiterCrew\OrbiterCrew.cfg LANGUAGE): Russian or English
    bool En() const { return api_.English(); }
    const char* L(const char* ru, const char* en) const { return En() ? en : ru; }
    bool ChassisNode(int i, OcNode* o) const {
        o->reach = 1.6; o->inside = 0;
        if (i == 0) { o->id = kNodeSocket; o->pos = kSocket; std::snprintf(o->label, sizeof o->label, "%s", L("разъём питания", "power socket")); return true; }
        if (i >= 1 && i <= kCellsN) {
            o->id = kNodeCell0 + i - 1; o->pos = _V(1.30, 0.10, kCellZ[i - 1]);           // the hatch's handle
            std::snprintf(o->label, sizeof o->label, L("гнездо ячейки %d", "cell bay %d"), i); return true;
        }
        if (i == kCellsN + 1 || i == kCellsN + 2) {
            const bool rear = i == kCellsN + 2;
            o->id = rear ? kNodeHitchR : kNodeHitchF; o->pos = _V(0.0, 0.50, rear ? -4.62 : 4.62);   // the folded bar's head (where the hand takes it)
            std::snprintf(o->label, sizeof o->label, "%s", rear ? L("сцепка задняя", "rear coupling") : L("сцепка передняя", "front coupling")); return true;
        }
        return false;
    }
    int ChassisActions(int node, int person, OcAction* out, int max) {
        int n = 0; char b[64];
        TvCarry c{}; const bool carries = tv_.Ok() && tv_.CarryGet(person, &c);
        const bool busy = carries || tvHolding(api_, person);
        if (node == kNodeSocket) {
            const bool plugged = tv_.Ok() && tv_.PluggedTo(GetHandle());
            std::snprintf(b, sizeof b, L("отключить кабель (заряд %.0f %%, %.0f кВт)", "unplug the cable (charge %.0f %%, %.0f kW)"), batt_ * 100.0, chargeW_ * 1e-3);
            if (plugged) tvAct(out, n, max, kActUnplug, b, !busy, L("руки заняты", "hands busy"), 0.8);
            else tvAct(out, n, max, kActPlug, L("подключить кабель", "plug the cable in"), carries && c.kind == TV_CABLE,
                       !tv_.Ok() ? L("нет модуля TVehicles", "no TVehicles module") : L("в руках нет кабеля станции", "no station cable in hand"), 1.0);
            return n;
        }
        if (node >= kNodeCell0 && node < kNodeCell0 + kCellsN) {
            const int k = node - kNodeCell0;
            if (cell_[k] >= 0) {
                std::snprintf(b, sizeof b, L("вынуть ячейку (%.0f %%)", "take the cell out (%.0f %%)"), cell_[k] / kCellJ * 100.0);
                tvAct(out, n, max, kActCellOut, b, !busy, L("руки заняты", "hands busy"), 1.2);
            } else tvAct(out, n, max, kActCellIn, L("вставить ячейку", "put the cell in"), carries && c.kind == TV_CELL, L("в руках нет ячейки", "no cell in hand"), 1.2);
            return n;
        }
        if (node == kNodeHitchF || node == kNodeHitchR) {
            const bool rear = node == kNodeHitchR;
            TVChassis* linked = rear ? towing_ : towBy_;
            if (linked) { std::snprintf(b, sizeof b, L("расцепить с %s", "uncouple from %s"), linked->GetName()); tvAct(out, n, max, kActUncouple, b, true, nullptr, 1.5); }
            else {
                double gap = 0; TVChassis* cand = Candidate(rear, &gap);
                if (cand) std::snprintf(b, sizeof b, L("сцепить с %s (%.1f м)", "couple to %s (%.1f m)"), cand->GetName(), gap);
                else std::snprintf(b, sizeof b, "%s", L("сцепить", "couple"));
                tvAct(out, n, max, kActCouple, b, cand != nullptr, rear ? L("сзади никого в 2–4 м", "nobody 2-4 m behind") : L("спереди никого в 2–4 м", "nobody 2-4 m ahead"), 1.5);
            }
            return n;
        }
        return 0;
    }
    int ChassisBegin(int node, int act, int person) {
        OcAction a[4]; const int n = ChassisActions(node, person, a, 4);
        for (int i = 0; i < n; ++i) if (a[i].id == act) return a[i].available;
        return 0;
    }
    void ChassisEnd(int node, int act, int person, int completed) {
        if (!completed || !ChassisBegin(node, act, person)) return;     // still possible at the end of its time?
        if (node == kNodeSocket) ChassisUse(kItemSocket, person);
        else if (node >= kNodeCell0 && node < kNodeCell0 + kCellsN) ChassisUse(kItemCell0 + node - kNodeCell0, person);
        else if (node == kNodeHitchF) ChassisUse(kItemHitchF, person);
        else if (node == kNodeHitchR) ChassisUse(kItemHitchR, person);
    }
    bool ChassisItem(int i, OcItem* out) const {
        *out = OcItem{};
        out->kind = OC_AIRLOCK; out->dir = _V(-1, 0, 0); out->radius = 1.5;
        if (i == 0) {
            out->id = kItemSocket; out->pos = kSocket; out->dir = _V(0, 0, -1); out->radius = 1.8;
            if (tv_.Ok() && tv_.PluggedTo(GetHandle()))
                std::snprintf(out->label, sizeof out->label, "отключить кабель (заряд %.0f %%, %.0f кВт)", batt_ * 100.0, chargeW_ * 1e-3);
            else std::snprintf(out->label, sizeof out->label, "подключить кабель (заряд %.0f %%)", batt_ * 100.0);
            return true;
        }
        if (i == 1 + kCellsN || i == 2 + kCellsN) {                    // the couplings
            const bool rear = i == 2 + kCellsN;
            out->id = rear ? kItemHitchR : kItemHitchF; out->radius = 1.8;
            out->pos = _V(0.0, 0.06, rear ? -4.95 : 4.95); out->dir = _V(0, 0, rear ? 1 : -1);
            TVChassis* linked = rear ? towing_ : towBy_;
            double gap = 0; TVChassis* cand = linked ? nullptr : Candidate(rear, &gap);
            if (linked) std::snprintf(out->label, sizeof out->label, "расцепить с %s", linked->GetName());
            else if (cand) std::snprintf(out->label, sizeof out->label, "сцепить с %s (%.1f м)", cand->GetName(), gap);
            else std::snprintf(out->label, sizeof out->label, "%s", rear ? "сцепка: сзади никого (встать в 2–4 м)" : "сцепка: спереди никого (встать в 2–4 м)");
            return true;
        }
        const int k = i - 1;
        if (k < 0 || k >= kCellsN) return false;
        out->id = kItemCell0 + k; out->pos = _V(1.30, -0.05, kCellZ[k]);
        if (cell_[k] >= 0) std::snprintf(out->label, sizeof out->label, "вынуть ячейку %d (%.0f %%)", k + 1, cell_[k] / kCellJ * 100.0);
        else std::snprintf(out->label, sizeof out->label, "вставить ячейку в гнездо %d", k + 1);
        return true;
    }
    void ChassisUse(int id, int person) {
        if (id == kItemHitchF || id == kItemHitchR) {
            const bool rear = id == kItemHitchR;
            if (rear && towing_) Uncouple();
            else if (!rear && towBy_) towBy_->Uncouple();
            else { double gap = 0; if (TVChassis* c = Candidate(rear, &gap)) { if (rear) Couple(c, gap); else c->Couple(this, gap); } }
            (void)person;
            return;
        }
        if (!tv_.Ok()) return;
        if (id == kItemSocket) {
            if (tv_.PluggedTo(GetHandle())) { if (tv_.Unplug(GetHandle(), person)) oapiWriteLogV("TVChassis %s: the cable unplugged", GetName()); }
            else if (tv_.Plug(GetHandle(), person)) oapiWriteLogV("TVChassis %s: the cable plugged in", GetName());
            return;
        }
        const int k = id - kItemCell0;
        if (k < 0 || k >= kCellsN) return;
        TvCarry c{};
        const bool hands = !tv_.CarryGet(person, &c);
        if (cell_[k] >= 0 && hands) {                                   // out into her hands (OrbiterCrew may refuse: too heavy here)
            OcHeld h; tvHeldCell(&h, cell_[k] / kCellJ);
            if (!tvHandOver(api_, person, &h)) return;
            c = {TV_CELL, GetHandle(), cell_[k]}; tv_.CarrySet(person, &c);
            cell_[k] = -1.0; SetEmptyMass(GetEmptyMass() - kCellMass);
            oapiWriteLogV("TVChassis %s: cell %d out (%.0f %%)", GetName(), k + 1, c.energyJ / kCellJ * 100.0);
        } else if (cell_[k] < 0 && c.kind == TV_CELL) {                  // the one she carries into the empty bay
            cell_[k] = c.energyJ; TvCarry none{}; tv_.CarrySet(person, &none); tvTakeBack(api_, person); SetEmptyMass(GetEmptyMass() + kCellMass);
            oapiWriteLogV("TVChassis %s: cell %d in (%.0f %%)", GetName(), k + 1, cell_[k] / kCellJ * 100.0);
        }
        batt_ = CellsSum() / kBattJ;
    }

    static void cGravity(void* c, const VECTOR3*, VECTOR3* g) {
        VECTOR3 d;
        Self(c)->HorizonInvRot(_V(0, -Self(c)->LocalG(), 0), d);
        *g = d;
    }
    // people walking outside: the platform's body (wheels, hull, deck) stops them - a box, slid along (vessel frame)
    static void cOuterWalls(void* c, const VECTOR3* from, VECTOR3* to, double radius, double height) {
        const double x0 = kBodyX + radius, z0 = Self(c)->bz0_ - radius, z1 = Self(c)->bz1_ + radius;
        if (to->y > Self(c)->btop_ || to->y + height < -1.2) return;                 // above the deck (on it: the interior) or far below
        auto inside = [&](double x, double z) { return std::fabs(x) < x0 && z > z0 && z < z1; };
        if (!inside(to->x, to->z)) return;
        if (!inside(from->x, to->z)) to->x = from->x;                             // slide along the side
        else if (!inside(to->x, from->z)) to->z = from->z;                        // slide along the end
        else { to->x = from->x; to->z = from->z; }
    }

    // A person outside meets the platform's body: a kinetic blow, judged on the person's side (OrbiterCrew ocImpact:
    // injury through the suit, falling, being thrown). The platform does not brake for people - it strikes them.
    // Once per contact (again only after 1 s apart from that body); a person walking into the standing platform only
    // meets its wall (OuterWalls), no blow below ~0.3 m/s.
    typedef int (*ocImpact_t)(OBJHANDLE body, const VECTOR3* vStrikeGlobal, double strikerMass, const VECTOR3* pointGlobal, const VECTOR3* normalGlobal);
    void Contacts_People(double dt) {
        if (!api_.Ok()) return;
        if (!impact_ && api_.dll) impact_ = (ocImpact_t)GetProcAddress(api_.dll, "ocImpact");
        for (auto& c : hitCool_) c.second -= dt;
        VECTOR3 myPos, myVel;
        GetGlobalPos(myPos);
        GetGlobalVel(myVel);
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
            OBJHANDLE h = oapiGetVesselByIndex(i);
            if (h == GetHandle()) continue;
            const int id = api_.PersonOfBody(h);
            if (!id || (api_.ShipOf && api_.ShipOf(id) == GetHandle())) continue;   // ours on the deck: not struck
            VECTOR3 gp, lp;
            oapiGetGlobalPos(h, &gp);
            if (length(gp - myPos) > 12.0) continue;
            Global2Local(gp, lp);
            const double rP = 0.30;                                                  // the body as a column of this radius
            if (lp.y > btop_ + 0.2 || lp.y < -2.2) continue;
            const double dx = std::fabs(lp.x) - kBodyX, dz0 = bz0_ - lp.z, dz1 = lp.z - bz1_;
            const double gap = (std::max)(dx, (std::max)(dz0, dz1));
            if (gap > rP) continue;                                                  // not touching
            // the normal from the platform to the person (local), along the nearest face
            VECTOR3 nl = dx >= dz0 && dx >= dz1 ? _V(lp.x > 0 ? 1.0 : -1.0, 0, 0) : dz1 > dz0 ? _V(0, 0, 1) : _V(0, 0, -1);
            const VECTOR3 contactL = _V(Clamp(lp.x, -kBodyX, kBodyX), Clamp(lp.y, -1.0, btop_), Clamp(lp.z, bz0_, bz1_));
            VECTOR3 cG, nG, pv;
            Local2Global(contactL, cG);
            GlobalRot(nl, nG);
            oapiGetGlobalVel(h, &pv);
            // the platform's point velocity at the contact: its own plus the yaw turning it
            VECTOR3 wl = _V(-yawRate_ * contactL.z, 0, yawRate_ * contactL.x), wG;
            GlobalRot(wl, wG);
            const VECTOR3 vStrike = (myVel + wG) - pv;
            const double closing = dotp(vStrike, nG);
            if (closing < 0.3 || hitCool_[h] > 0.0) continue;
            hitCool_[h] = 1.0;
            const int ok = impact_ ? impact_(h, &vStrike, GetMass(), &cG, &nG) : 0;
            char b[200];
            std::snprintf(b, sizeof b, "MPU: struck %s at %.2f m/s (closing), %s", oapiGetVesselInterface(h)->GetName(), closing, ok ? "taken by OrbiterCrew" : "no ocImpact - nothing done to the person");
            oapiWriteLog(b);
            lastHit_ = closing; lastHitT_ = 3.0;
        }
        lastHitT_ -= dt;
    }

    static bool OurWindow() {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        return pid == GetCurrentProcessId();
    }

    // ---------------- the Tantra's legs, feet and lift cabin: the platform meets them (Tantra.dll tantraOuterSolids) ----------------
    // The platform is a box (vessel frame); each solid is sampled along its axis, the nearest box point found; inside its
    // radius - a contact: the closing speed along the normal is taken out in one step (a dead, slightly springy stop) and
    // the overlap pushed back. The ship is the immovable side.
    void Obstacles(double dt) {
        static tantraOuterSolids_t fn = nullptr;
        static bool looked = false;
        if (!looked) { looked = true; if (HMODULE h = GetModuleHandleA("Tantra.dll")) fn = (tantraOuterSolids_t)GetProcAddress(h, "tantraOuterSolids"); }
        if (!fn) { if (HMODULE h = GetModuleHandleA("Tantra.dll")) fn = (tantraOuterSolids_t)GetProcAddress(h, "tantraOuterSolids"); if (!fn) return; }
        VECTOR3 me;
        GetGlobalPos(me);
        const double m = GetMass();
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
            OBJHANDLE h = oapiGetVesselByIndex(i);
            if (h == GetHandle()) continue;
            VECTOR3 sp;
            oapiGetGlobalPos(h, &sp);
            if (length(sp - me) > 400.0) continue;
            TantraSolid sol[32];
            const int n = fn(h, sol, 32);
            if (n <= 0) continue;
            VESSEL* ship = oapiGetVesselInterface(h);
            ThrownAgainst(ship, sol, n);
            for (int k = 0; k < n; ++k) {
                VECTOR3 a, b;
                double r;
                if (sol[k].kind == 1) { a = sol[k].a; b = sol[k].b; r = sol[k].r; }
                else {                                                           // a box: as an upright capsule round it
                    const VECTOR3 up = _V(sol[k].R.m12, sol[k].R.m22, sol[k].R.m32);
                    a = sol[k].c - up * sol[k].half.y; b = sol[k].c + up * sol[k].half.y;
                    r = (std::max)(sol[k].half.x, sol[k].half.z);
                }
                VECTOR3 ga, gb, la, lb;
                ship->Local2Global(a, ga); ship->Local2Global(b, gb);
                Global2Local(ga, la); Global2Local(gb, lb);
                for (int q = 0; q <= 16; ++q) {
                    const VECTOR3 p = la + (lb - la) * (q / 16.0);
                    const VECTOR3 c = _V(Clamp(p.x, -kBodyX, kBodyX), Clamp(p.y, -1.0, btop_), Clamp(p.z, bz0_, bz1_));
                    VECTOR3 d = c - p;
                    const double dist = length(d);
                    if (dist >= r || dist < 1e-6) continue;
                    const VECTOR3 nrm = d / dist;                                    // from the solid into the platform: the push
                    VECTOR3 v;
                    GetGroundspeedVector(FRAME_LOCAL, v);
                    const VECTOR3 vp = v + _V(-yawRate_ * c.z, 0, yawRate_ * c.x);
                    const double closing = -dotp(vp, nrm);                          // moving into the solid
                    // a dead stop: half the closing speed taken out each step (the point also turns the body, a full
                    // impulse overshot into a rebound), the overlap pushed out gently; never more than 20 g
                    if ((GetFlightStatus() & 1) && closing < 0.5) break;           // standing: a touch does nothing
                    double f = (r - dist) * m * 10.0;
                    if (closing > 0.0) f += 0.5 * m * closing / (std::max)(dt, 1e-3);
                    f = (std::min)(f, 20.0 * 9.81 * m);
                    AddForce(nrm * f, c);
                    if (closing > 1.0 && obstHitT_ <= 0.0) Damage(c, nrm, closing * 1.1, c.y);   // a ship's leg does not give
                    if (closing > kEjectV && obstHitT_ <= 0.0) EjectAll();          // a hard stop: everyone aboard flies on
                    if (closing > 1.0 && obstHitT_ <= 0.0) {
                        char lb2[160];
                        std::snprintf(lb2, sizeof lb2, "MPU: struck the %s's %s at %.1f m/s", ship->GetName(), sol[k].kind == 1 ? "leg/foot" : "lift cabin", closing);
                        oapiWriteLog(lb2);
                        obstHitT_ = 1.0;
                    }
                    break;                                                            // one contact per solid per step
                }
            }
        }
        obstHitT_ -= dt;
        for (auto it = thrown_.begin(); it != thrown_.end();) { it->second -= dt; it = it->second <= 0.0 || !oapiIsVessel(it->first) ? thrown_.erase(it) : std::next(it); }
    }

    // the self-test's target: the nearest leg/foot of a ship (local frame, its surface point); its distance and the count
    bool NearestLeg(VECTOR3& out) {
        HMODULE hm = GetModuleHandleA("Tantra.dll");
        tantraOuterSolids_t fn = hm ? (tantraOuterSolids_t)GetProcAddress(hm, "tantraOuterSolids") : nullptr;
        if (!fn) return false;
        double best = 1e9; bool found = false; tgtN_ = 0;
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
            OBJHANDLE h = oapiGetVesselByIndex(i);
            TantraSolid sol[32];
            const int n = fn(h, sol, 32);
            if (n <= 0) continue;
            tgtN_ += n;
            VESSEL* ship = oapiGetVesselInterface(h);
            for (int k = 0; k < n; ++k) {
                if (sol[k].kind != 1) continue;
                VECTOR3 g, l;
                ship->Local2Global(sol[k].a, g);
                Global2Local(g, l);
                const double d = std::hypot(l.x, l.z) - sol[k].r;
                if (d < best) { best = d; out = l; found = true; }
            }
        }
        tgtDist_ = found ? best : -1.0;
        return found;
    }

    // people thrown off in a crash, still flying: a leg or a foot in their way strikes them (ocImpact, the ship's mass)
    void ThrownAgainst(VESSEL* ship, const TantraSolid* sol, int n) {
        if (thrown_.empty() || !impact_) return;
        VECTOR3 sv;
        ship->GetGlobalVel(sv);
        for (auto& t : thrown_) {
            if (t.second <= 0.0 || !oapiIsVessel(t.first)) continue;
            VECTOR3 gp, pv, lp;
            oapiGetGlobalPos(t.first, &gp);
            oapiGetGlobalVel(t.first, &pv);
            ship->Global2Local(gp, lp);
            for (int k = 0; k < n; ++k) {
                VECTOR3 a = sol[k].a, b = sol[k].b; double r = sol[k].r;
                if (sol[k].kind != 1) {
                    const VECTOR3 up = _V(sol[k].R.m12, sol[k].R.m22, sol[k].R.m32);
                    a = sol[k].c - up * sol[k].half.y; b = sol[k].c + up * sol[k].half.y; r = (std::max)(sol[k].half.x, sol[k].half.z);
                }
                const VECTOR3 ab = b - a;
                const double tt = Clamp(dotp(lp - a, ab) / (std::max)(dotp(ab, ab), 1e-9), 0.0, 1.0);
                const VECTOR3 q = a + ab * tt, d = lp - q;
                const double dist = length(d);
                if (dist > r + 0.3 || dist < 1e-6) continue;
                VECTOR3 nG, qG;
                ship->GlobalRot(d / dist, nG);                                  // from the leg to the person
                ship->Local2Global(q + d / dist * r, qG);
                const VECTOR3 vStrike = sv - pv;                                // the leg against the flying person
                if (dotp(vStrike, nG) < 0.3) continue;
                impact_(t.first, &vStrike, ship->GetMass(), &qG, &nG);
                char m[160];
                std::snprintf(m, sizeof m, "MPU: %s thrown into the %s's leg at %.1f m/s", oapiGetVesselInterface(t.first)->GetName(), ship->GetName(), dotp(vStrike, nG));
                oapiWriteLog(m);
                t.second = 0.0;                                                  // one blow; the fall is OrbiterCrew's
                break;
            }
        }
    }

    // Another MPU: Orbiter has no collisions between vessels. Each platform sees the other's body as a rectangle in plan
    // (separating axes: its own two and the other's two), takes the least overlap as the normal, and pushes ITSELF out
    // along it - the other does the same from its side, so the pair gets equal and opposite pushes; the closing speed is
    // taken out by the reduced mass. A crash above 30 km/h throws the people off both decks.
    void Vehicles(double dt) {
        const double hx = kBodyX, hz = 0.5 * (bz1_ - bz0_), cz = 0.5 * (bz1_ + bz0_);
        VECTOR3 me;
        GetGlobalPos(me);
        for (TVChassis* o : All()) {
            if (o == this || o == towBy_ || o == towing_) continue;          // coupled: the bar holds them, not the bumpers
            VECTOR3 op;
            o->GetGlobalPos(op);
            if (length(op - me) > 15.0) continue;
            // the other's corners and axes in my frame
            VECTOR3 oc[4], ax, az, oo, g;
            const double cx[4] = {-hx, hx, hx, -hx}, czs[4] = {-hz, -hz, hz, hz};
            for (int k = 0; k < 4; ++k) { o->Local2Global(_V(cx[k], 0, cz + czs[k]), g); Global2Local(g, oc[k]); }
            o->Local2Global(_V(0, 0, cz), g); Global2Local(g, oo);
            if (std::fabs(oo.y) > 2.0) continue;
            o->GlobalRot(_V(1, 0, 0), g); { MATRIX3 Rm; GetRotationMatrix(Rm); ax = tmul(Rm, g); }
            o->GlobalRot(_V(0, 0, 1), g); { MATRIX3 Rm; GetRotationMatrix(Rm); az = tmul(Rm, g); }
            ax.y = 0; ax = ax / length(ax); az.y = 0; az = az / length(az);
            const VECTOR3 mc[4] = {_V(-hx, 0, cz - hz), _V(hx, 0, cz - hz), _V(hx, 0, cz + hz), _V(-hx, 0, cz + hz)};
            const VECTOR3 axes[4] = {_V(1, 0, 0), _V(0, 0, 1), ax, az};
            double best = 1e9; VECTOR3 nrm = _V(0, 0, 0);
            bool sep = false;
            for (const VECTOR3& a : axes) {
                double a0 = 1e9, a1 = -1e9, b0 = 1e9, b1 = -1e9;
                for (int k = 0; k < 4; ++k) {
                    const double pa = mc[k].x * a.x + mc[k].z * a.z, pb = oc[k].x * a.x + oc[k].z * a.z;
                    a0 = (std::min)(a0, pa); a1 = (std::max)(a1, pa); b0 = (std::min)(b0, pb); b1 = (std::max)(b1, pb);
                }
                const double ov = (std::min)(a1, b1) - (std::max)(a0, b0);
                if (ov <= 0.0) { sep = true; break; }
                if (ov < best) {
                    best = ov;
                    const double toOther = (oo.x - 0.0) * a.x + (oo.z - cz) * a.z;
                    nrm = toOther > 0 ? _V(-a.x, 0, -a.z) : _V(a.x, 0, a.z);       // from the other towards me
                }
            }
            if (sep) continue;
            // where they touch: the corners of each inside the other, else the middle between the centres
            VECTOR3 cp = _V(0, 0, 0); int nc = 0;
            for (int k = 0; k < 4; ++k)
                if (std::fabs(oc[k].x) <= hx && std::fabs(oc[k].z - cz) <= hz) { cp = cp + oc[k]; ++nc; }
            if (!nc) cp = (oo + _V(0, 0, cz)) * 0.5; else cp = cp / nc;
            cp.y = 0.0;
            // the closing speed along the normal (the other's own velocity, mine with my turning)
            VECTOR3 vme, vo, d;
            GetGlobalVel(vme); o->GetGlobalVel(vo);
            { MATRIX3 Rm; GetRotationMatrix(Rm); d = tmul(Rm, vme - vo); }
            d = d + _V(-yawRate_ * cp.z, 0, yawRate_ * cp.x);
            const double closing = -dotp(d, nrm);
            // standing on its brake (landed): a touch does nothing - the moving one pushes itself out; only a real blow
            // (closing above 0.5 m/s) moves it (each push took it out of the landed state: it twitched and jumped)
            if ((GetFlightStatus() & 1) && closing < 0.5) continue;
            const double m1 = GetMass(), m2 = o->GetMass(), mr = m1 * m2 / (m1 + m2);
            double f = best * m1 * 10.0;
            if (closing > 0.0) f += 0.5 * mr * closing / (std::max)(dt, 1e-3);
            f = (std::min)(f, 20.0 * 9.81 * m1);
            AddForce(nrm * f, cp);
            if (closing > 1.0 && carHitT_ <= 0.0) {
                char b[160];
                std::snprintf(b, sizeof b, "MPU: %s struck %s at %.1f m/s", GetName(), o->GetName(), closing);
                oapiWriteLog(b);
                carHitT_ = 1.0;
                Damage(cp, nrm, closing * m2 / (m1 + m2) * 1.1, 0.0);           // its share of the speed change
                if (closing > kEjectV) EjectAll();
            }
        }
        carHitT_ -= dt;
    }

    // A crash above 30 km/h: the people on the deck and at the post are thrown forward off the platform with the speed it
    // had - their flight, the fall onto the ground or into a leg and the injuries are OrbiterCrew's (ocEject, optional)
    typedef int (*ocEject_t)(int id, const VECTOR3* vGlobal);
    void EjectAll() {
        if (cab_) return;                                                         // Э.МПУ: belted in the cabin
        if (!api_.Ok()) return;
        static ocEject_t ej = nullptr;
        if (!ej && api_.dll) ej = (ocEject_t)GetProcAddress(api_.dll, "ocEject");
        VECTOR3 vG;
        GetGlobalVel(vG);
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
            OBJHANDLE h = oapiGetVesselByIndex(i);
            const int id = api_.PersonOfBody(h);
            if (!id || !api_.ShipOf || api_.ShipOf(id) != GetHandle()) continue;
            if (id == driver_) driver_ = 0;
            const int ok = ej ? ej(id, &vG) : 0;
            if (ok) thrown_[h] = 6.0;                                            // watched in flight: a leg in the way strikes
            char b[160];
            // the log gives the speed over the ground (vG itself is global - from the Sun; ocEject takes the planet's off)
            std::snprintf(b, sizeof b, "MPU: crash - %s thrown off at %.1f m/s over the ground: %s", oapiGetVesselInterface(h)->GetName(), GetGroundspeed(),
                          ok ? "taken by OrbiterCrew" : "no ocEject - stays aboard");
            oapiWriteLog(b);
        }
    }

    // dust from under the wheels: on an airless body thrown in short ballistic sheets, in air it hangs as a cloud
    void Dust() {
        const bool air = GetAtmPressure() > 50.0;
        if (!dustMade_ || air != dustAir_) {
            for (PSTREAM_HANDLE& h : dust_) if (h) { DelExhaustStream(h); h = nullptr; }
            static SURFHANDLE tex = oapiRegisterParticleTexture(const_cast<char*>("Tantra_dust"));
            PARTICLESTREAMSPEC vac = {0, 0.25, 30.0, 5.0, 0.3, 0.9, 0.6, 0.0, PARTICLESTREAMSPEC::DIFFUSE,
                                      PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, tex};
            PARTICLESTREAMSPEC atm = {0, 0.4, 12.0, 2.0, 0.5, 5.0, 1.8, 1.5, PARTICLESTREAMSPEC::DIFFUSE,
                                      PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, tex};
            for (int i = 0; i < mpu::kWheels; ++i) {
                const auto& W = mpu::kWheel[i];
                const VECTOR3 at = _V(W.hub[0], W.hub[1] - mpu::kWheelR + 0.1, W.hub[2] - 0.5 * (W.hub[2] > 0 ? 1 : 1));
                dust_[i] = AddParticleStream(air ? &atm : &vac, at, _V(0, 0.55, -0.83), &dustLv_[i]);
            }
            dustAir_ = air; dustMade_ = true;
        }
        for (int i = 0; i < mpu::kWheels; ++i) dustLv_[i] = n_[i] > 0.0 ? Clamp((std::fabs(vf_[i]) - 1.0) / 12.0, 0.0, 1.0) : 0.0;
    }

    // sound: only through air - none on the Moon; on Mars thin and near, on Earth full (Orbiter's air at the platform);
    // fades with the camera's distance. Hub motors (two layers crossfaded by speed), tyres rolling, knocks of the
    // suspension, the lift drive while the platform goes up or down.
    enum { kSndDrive = 1, kSndInverter, kSndRoll, kSndKnock, kSndLift, kSndHum };
    void Sound(double dt) {
        if (!snd_ || !snd_->IsPresent()) return;
        VECTOR3 ear, cg;
        oapiCameraGlobalPos(&ear);
        GetGlobalPos(cg);
        // loudness through the air: a gentle law of the density (Mars ~1/3 of Earth's, as its recordings sound), nothing
        // in vacuum
        const double air = (std::min)(1.0, std::pow((std::max)(0.0, GetAtmDensity()) / 1.2, 0.25));
        double att = air / (1.0 + length(ear - cg) / 60.0);
        // through the structure: the person aboard (the cabin, the deck, the post) hears the motors through the floor even in
        // vacuum - in the cabin also through its air (louder), on the open deck through the boots
        if (api_.Ok() && api_.PersonOfBody && api_.ShipOf) {
            const OBJHANDLE f = oapiGetFocusObject();
            const int pid = f ? api_.PersonOfBody(f) : 0;
            if (pid && api_.ShipOf(pid) == GetHandle()) att = (std::max)(att, cab_ ? 0.8 : 0.4);
        }
        const double v = std::fabs(speed_);
        const double work = Clamp(std::fabs(powerW_ - kBaseW) / kPmax * 2.0 + v / kVmax * 0.4, 0.0, 1.0);
        const double hiK = Clamp(v / 18.0, 0.0, 1.0);
        auto layer = [this](int id, double vol) {
            if (vol > 0.01) snd_->PlayWav(id, true, (float)(std::min)(1.0, vol));
            else if (snd_->IsWavPlaying(id)) snd_->StopWav(id);
        };
        // the traction drive: one loop recorded at 40 km/h, played at the speed's rate (the pitch follows the wheels, as on
        // a tram or an electric haul truck); its loudness follows the power - a pull roars, coasting only whispers, standing
        // it is silent. On the start the inverter whistles and fades out by ~20 km/h
        const double pw = Clamp(std::fabs(powerW_ - kBaseW - moduleW_) / kPmax, 0.0, 1.0);
        const double turning = Clamp(v / 0.4, 0.0, 1.0);
        layer(kSndDrive, att * turning * (0.18 + 0.82 * std::sqrt(pw)));
        if (snd_->IsWavPlaying(kSndDrive)) snd_->SetPlaybackSpeed(kSndDrive, (float)Clamp(v / 11.1, 0.15, 2.2));
        layer(kSndInverter, att * Clamp(pw * 4.0, 0.0, 1.0) * Clamp(1.0 - v / 5.5, 0.0, 1.0) * 0.45 * (v > 0.05 || pw > 0.01 ? 1.0 : 0.0));
        if (snd_->IsWavPlaying(kSndInverter)) snd_->SetPlaybackSpeed(kSndInverter, (float)(0.85 + 0.12 * Clamp(v / 5.5, 0.0, 1.0)));
        // the power's hum under it all: the iron and the inverter bus's choke - the more kilowatts, the deeper it presses
        layer(kSndHum, att * Clamp(std::pow(pw, 0.6) * 1.3, 0.0, 1.0) * 0.55 * (powerW_ > kBaseW + 20e3 ? 1.0 : 0.0));
        (void)hiK; (void)work;
        layer(kSndRoll, att * Clamp(v / 10.0, 0.0, 1.0) * 0.8);
        layer(kSndLift, att * (rideMoving_ ? 0.6 : 0.0));
        double kick = 0.0;
        for (int i = 0; i < mpu::kWheels; ++i) {
            kick = (std::max)(kick, std::fabs(height_[i] - hPrev_[i]) / (std::max)(dt, 1e-3));
            hPrev_[i] = height_[i];
        }
        knockCool_ -= dt;
        if (kick > 1.5 && knockCool_ <= 0.0 && att * kick / 4.0 > 0.02) {
            snd_->PlayWav(kSndKnock, false, (float)(std::min)(1.0, att * kick / 4.0));
            knockCool_ = 0.25;
        }
    }

    // Stand it on its eight wheels: the ground under each wheel, a plane through them (least squares), the platform's up
    // along that plane's normal, its heading kept, the centre (kCgH - kSag) above the plane - every wheel at its static
    // sag. (Orbiter's own "stand on your points" tilts it by the slope under the centre only: on uneven ground one wheel
    // went 0.9 m into it and the platform sat 0.4 m low.) Orbiter's landed state at that pose.
    void SeatOnWheels(OBJHANDLE ref, bool level = false) {
        const double R = oapiGetSize(ref);
        VECTOR3 fH, rH;                                     // the heading and its right, horizontal (horizon frame)
        HorizonRot(_V(0, 0, 1), fH); fH.y = 0; fH = fH / length(fH);
        HorizonRot(_V(1, 0, 0), rH); rH.y = 0; rH = rH - fH * dotp(rH, fH); rH = rH / length(rH);
        double A[3][3] = {}, B[3] = {};
        for (int i = 0; i < mpu::kWheels; ++i) {
            const auto& W = mpu::kWheel[i];
            VECTOR3 lp = _V(W.hub[0], W.hub[1] - mpu::kWheelR, W.hub[2]), hp, gp;
            HorizonRot(lp, hp);                             // the wheel's offset from the centre, horizon frame
            Local2Global(lp, gp);
            double lng, lat, rad;
            oapiGlobalToEqu(ref, gp, &lng, &lat, &rad);
            const double e = oapiSurfaceElevation(ref, lng, lat);
            const double row[3] = {1.0, dotp(hp, fH), dotp(hp, rH)};
            for (int a = 0; a < 3; ++a) { B[a] += row[a] * e; for (int b = 0; b < 3; ++b) A[a][b] += row[a] * row[b]; }
        }
        // solve the 3x3 normal equations (Cramer)
        auto det = [](double m[3][3]) { return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                                               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]); };
        const double D = det(A);
        if (std::fabs(D) < 1e-9) return;
        double sol[3];
        for (int c = 0; c < 3; ++c) { double M[3][3]; for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) M[a][b] = b == c ? B[a] : A[a][b]; sol[c] = det(M) / D; }
        const double planeC = sol[0];
        double sF = sol[1], sR = sol[2];
        if (level) {                                                         // parked: the deck level, the wheels follow the ground
            sF = (sF > 0 ? 1.0 : -1.0) * (std::max)(0.0, std::fabs(sF) - kLevelF);
            sR = (sR > 0 ? 1.0 : -1.0) * (std::max)(0.0, std::fabs(sR) - kLevelR);
        }
        double lng, lat, rad;
        GetEquPos(lng, lat, rad);
        const double eC = oapiSurfaceElevation(ref, lng, lat);
        // the wanted axes in the horizon frame, then in the present vessel frame
        VECTOR3 up = _V(0, 1, 0) - fH * sF - rH * sR; up = up / length(up);
        VECTOR3 fw = fH - up * dotp(fH, up); fw = fw / length(fw);
        VECTOR3 rt = rH - up * dotp(rH, up) - fw * dotp(rH, fw); rt = rt / length(rt);
        VECTOR3 lr, lu, lf;
        HorizonInvRot(rt, lr); HorizonInvRot(up, lu); HorizonInvRot(fw, lf);
        const MATRIX3 M = _M(lr.x, lu.x, lf.x, lr.y, lu.y, lf.y, lr.z, lu.z, lf.z);   // wanted axes as columns, present frame
        MATRIX3 Rs, Rp, Lm;
        GetRotationMatrix(Rs);
        const MATRIX3 Rw = mul(Rs, M);                     // the wanted vessel -> global
        oapiGetRotationMatrix(ref, &Rp);
        const double* a = Rp.data; const double* b = Rw.data; double* l = Lm.data;   // L = Rp^T Rw
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) l[3 * i + j] = a[i] * b[j] + a[3 + i] * b[3 + j] + a[6 + i] * b[6 + j];
        VESSELSTATUS2 vs;
        std::memset(&vs, 0, sizeof vs);
        vs.version = 2;
        GetStatusEx(&vs);
        vs.status = 1; vs.rbody = ref; vs.surf_lng = lng; vs.surf_lat = lat;
        oapiGetHeading(GetHandle(), &vs.surf_hdg);
        vs.arot = _V(std::atan2(Lm.m23, Lm.m33), -std::asin(Clamp(Lm.m13, -1.0, 1.0)), std::atan2(Lm.m12, Lm.m11));
        vs.vrot = _V((planeC - eC) + (mpu::kCgH - kSag + ride_) / (std::max)(0.5, up.y), 0.0, 0.0);
        DefSetStateEx(&vs);
        (void)R;
    }

    int st0Status() { VESSELSTATUS2 v; std::memset(&v, 0, sizeof v); v.version = 2; GetStatusEx(&v); return (int)v.status; }

    // the soil of the body it drives on (by its name; anything unknown: the lunar regolith)
    const Soil& SoilOf(OBJHANDLE ref) const {
        char nm[64] = "";
        oapiGetObjectName(ref, nm, sizeof nm);
        if (!std::strcmp(nm, "Earth")) return kEarthSoil;
        if (!std::strcmp(nm, "Mars")) return kMarsSoil;
        return kMoonSoil;
    }

    double LocalG() const {
        OBJHANDLE ref = GetSurfaceRef();
        if (!ref) return 9.81;
        const double r = oapiGetSize(ref);
        return GGRAV * oapiGetMass(ref) / (r * r);
    }

    void GetHeading(double& h) const { oapiGetHeading(GetHandle(), &h); }

    // the eight wheels first (the first three span Orbiter's ground plane: front, left rear, right rear), then the hull
    void Contacts(double g) {
        gSet_ = g; rideSet_ = ride_;
        const double m = GetMass() > 0.0 ? GetMass() : kMass;
        k_ = m * g / (mpu::kWheels * kSag);
        const double c = 2.0 * 0.7 * std::sqrt(k_ * m / mpu::kWheels);
        const int order[8] = {1, 7, 6, 0, 2, 3, 4, 5};   // front left, rear left, rear right, then the rest
        TOUCHDOWNVTX t[16];
        int n = 0;
        for (int k : order) {
            const auto& W = mpu::kWheel[k];
            // Orbiter's wheel points never change (moved or softened, Orbiter re-seated the platform on them - sunk 0.3-0.9 m,
            // then thrown up and over); the soft ride is our force taken off their stiffness (see the loads)
            t[n++] = {_V(W.hub[0], W.hub[1] - mpu::kWheelR - ride_, W.hub[2]), k_, c, 0.05, 0.05};
        }
        const VECTOR3 hull[8] = {{-0.5, -0.37, 4.0}, {0.5, -0.37, 4.0}, {-0.5, -0.37, -4.0}, {0.5, -0.37, -4.0},
                                 {-1.25, -0.06, 4.35}, {1.25, -0.06, 4.35}, {-1.25, -0.06, -4.35}, {1.25, -0.06, -4.35}};
        for (const VECTOR3& p : hull) t[n++] = {p, 10.0 * k_, 2.0 * c, 0.8, 0.8};
        SetTouchdownPoints(t, n);
    }

    // The suspension is moved by its vertices (not by Orbiter's animations): from the rest copies of the groups, each
    // frame. Double wishbones: the hub goes up or down by d; each wishbone turns about its inner pivot's longitudinal axis
    // so its ball joint keeps its length; the knuckle (hub motor, fender) moves with the joints and steers about the hub's
    // vertical; the wheel does too and spins about its axle.
    enum { kLow = 0, kUp, kKnuckle, kWheelP };
    void DefineAnimations() {
        for (int i = 0; i < mpu::kWheels; ++i) {
            const auto& W = mpu::kWheel[i];
            for (int k = 0; k < W.nLow; ++k) part_.push_back(RestCopy(W.lowGrp[k], i, kLow));
            for (int k = 0; k < W.nUp; ++k) part_.push_back(RestCopy(W.upGrp[k], i, kUp));
            for (int k = 0; k < W.nKn; ++k) part_.push_back(RestCopy(W.knGrp[k], i, kKnuckle));
            for (int k = 0; k < W.nWheel; ++k) part_.push_back(RestCopy(W.wheelGrp[k], i, kWheelP));
        }
        for (int k = 0; k < mpu::kStepsN; ++k) steps_.push_back(RestCopy(mpu::kStepsGrp[k], 0, kLow));
        bar_ = RestCopy(mpu::kTowBarGrp, 0, kLow);
    }

    struct Part { int grp, wheel, kind; std::vector<NTVERTEX> rest, work; };
    Part RestCopy(int grp, int wheel, int kind) const {
        Part p{grp, wheel, kind, {}, {}};
        if (MESHGROUP* g = oapiMeshGroup(mesh_, (DWORD)grp)) { p.rest.assign(g->Vtx, g->Vtx + g->nVtx); p.work = p.rest; }
        return p;
    }

    void clbkVisualCreated(VISHANDLE vis, int refcount) override {
        (void)refcount; dev_ = GetDevMesh(vis, meshIdx_);
        for (double& t : lastTh_) t = 9.0;                                       // a new visual: every wheel placed again
        stepsDrawn_ = -1.0; barDrawn_ = -1;
    }
    void clbkVisualDestroyed(VISHANDLE vis, int refcount) override { (void)vis; (void)refcount; dev_ = nullptr; }

    // right-handed rotations in the vessel frame (as Orbiter's own: +y over +z for x, +z over +x for y)
    static VECTOR3 RotX(const VECTOR3& p, double a) { const double c = std::cos(a), s = std::sin(a); return _V(p.x, p.y * c - p.z * s, p.y * s + p.z * c); }
    static VECTOR3 RotY(const VECTOR3& p, double a) { const double c = std::cos(a), s = std::sin(a); return _V(p.x * c + p.z * s, p.y, -p.x * s + p.z * c); }

    // the turn of a wishbone (pivot P, joint K, vessel frame) about the longitudinal axis when its joint rises by d; dx: how
    // far the joint moves across
    static double ArmTurn(const double* P, const double* K, double d, double* dx) {
        const double rx = K[0] - P[0], ry = K[1] - P[1], L = std::hypot(rx, ry);
        const double ny = Clamp(ry + d, -0.97 * L, 0.97 * L);
        const double nx = (rx >= 0.0 ? 1.0 : -1.0) * std::sqrt(L * L - ny * ny);
        *dx = nx - rx;
        return std::atan2(ny, nx) - std::atan2(ry, rx);
    }
    static MATRIX3 MatZ(double a) { const double c = std::cos(a), s = std::sin(a); return _M(c, -s, 0, s, c, 0, 0, 0, 1); }

    void Animate() {
        double dv[mpu::kWheels];
        for (int i = 0; i < mpu::kWheels; ++i) {
            // the hub up by the penetration (in contact), down by the gap (in the air), +-0.45 m of travel; the deck's height
            // and the wheel's rut take it down
            dv[i] = (height_[i] < 0.0 ? (std::min)(-height_[i], 0.45) : -(std::min)(height_[i], 0.45)) - ride_ - sink_[i];
            armState_[i] = dv[i];
        }
        if (!dev_) return;
        VECTOR3 ear, cg;                                                          // far from the camera: not moved at all
        oapiCameraGlobalPos(&ear);
        GetGlobalPos(cg);
        if (length(ear - cg) > 300.0) return;
        MATRIX3 Ml[mpu::kWheels], Mu[mpu::kWheels], Mk[mpu::kWheels], Mw[mpu::kWheels];
        VECTOR3 shift[mpu::kWheels];
        bool moved[mpu::kWheels];
        for (int i = 0; i < mpu::kWheels; ++i) {
            moved[i] = std::fabs(dv[i] - lastTh_[i]) > 1e-4 || std::fabs(delta_[i] - lastDelta_[i]) > 1e-4 || std::fabs(spin_[i] - lastSpin_[i]) > 1e-3;
            if (!moved[i]) continue;
            lastTh_[i] = dv[i]; lastDelta_[i] = delta_[i]; lastSpin_[i] = spin_[i];
            const auto& W = mpu::kWheel[i];
            double dxl, dxu;
            Ml[i] = MatZ(ArmTurn(W.pl, W.kl, dv[i], &dxl));
            Mu[i] = MatZ(ArmTurn(W.pu, W.ku, dv[i], &dxu));
            shift[i] = _V(0.5 * (dxl + dxu), dv[i], 0.0);
            Mk[i] = MatY(delta_[i]);
            Mw[i] = mul(MatY(delta_[i]), MatX(spin_[i]));
        }
        for (Part& p : part_) {
            if (!moved[p.wheel]) continue;
            const auto& W = mpu::kWheel[p.wheel];
            const VECTOR3 H = _V(W.hub[0], W.hub[1], W.hub[2]);
            VECTOR3 C, O;
            const MATRIX3* M;
            switch (p.kind) {
            case kLow: C = O = _V(W.pl[0], W.pl[1], W.pl[2]); M = &Ml[p.wheel]; break;
            case kUp:  C = O = _V(W.pu[0], W.pu[1], W.pu[2]); M = &Mu[p.wheel]; break;
            case kKnuckle: C = H; O = H + shift[p.wheel]; M = &Mk[p.wheel]; break;
            default: C = H; O = H + shift[p.wheel]; M = &Mw[p.wheel]; break;
            }
            for (size_t k = 0; k < p.rest.size(); ++k) {
                const NTVERTEX& v0 = p.rest[k];
                NTVERTEX& v = p.work[k];
                const VECTOR3 q = O + mul(*M, _V(v0.x - C.x, v0.y - C.y, v0.z - C.z)), n = mul(*M, _V(v0.nx, v0.ny, v0.nz));
                v.x = (float)q.x; v.y = (float)q.y; v.z = (float)q.z; v.nx = (float)n.x; v.ny = (float)n.y; v.nz = (float)n.z;
            }
            GROUPEDITSPEC ges{};
            ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML;
            ges.Vtx = p.work.data();
            ges.nVtx = (DWORD)p.work.size();
            oapiEditMeshGroup(dev_, (DWORD)p.grp, &ges);
        }
    }

    // the boarding steps: deployed (as built) while the platform stands on its parking brake; driving, they slide up and
    // into the pocket in the spine's side (mpu::kStepsStow) - their bottom level with the spine's belly, nothing below it
    void Steps(double dt) {
        const bool out = park_ && std::fabs(speed_) < 0.3;
        stepsOut_ = Clamp(stepsOut_ + (out ? dt : -dt) / 1.2, 0.0, 1.0);        // 1.2 s either way
        if (!dev_ || std::fabs(stepsOut_ - stepsDrawn_) < 1e-3) return;
        stepsDrawn_ = stepsOut_;
        const double k = 1.0 - stepsOut_;
        for (Part& p : steps_) {
            for (size_t i = 0; i < p.rest.size(); ++i) {
                p.work[i] = p.rest[i];
                p.work[i].x += (float)(k * mpu::kStepsStow[0]); p.work[i].y += (float)(k * mpu::kStepsStow[1]); p.work[i].z += (float)(k * mpu::kStepsStow[2]);
            }
            GROUPEDITSPEC ges{};
            ges.flags = GRPEDIT_VTXCRD;
            ges.Vtx = p.work.data();
            ges.nVtx = (DWORD)p.work.size();
            oapiEditMeshGroup(dev_, (DWORD)p.grp, &ges);
        }
    }

    static MATRIX3 MatX(double a) { const double c = std::cos(a), s = std::sin(a); return _M(1, 0, 0, 0, c, -s, 0, s, c); }
    static MATRIX3 MatY(double a) { const double c = std::cos(a), s = std::sin(a); return _M(c, 0, s, 0, 1, 0, -s, 0, c); }

    // at rest: Orbiter's landed state in the attitude it stands in, its centre at its height
    // at rest: Orbiter's landed state in its equilibrium on its wheels - the plane through the ground under the eight
    // wheels, the centre at its height (the pose of the moment, after a blow squatting or tilted, made it jump on starting)
    void Rest(OBJHANDLE ref) {
        drivingMode_ = false;
        Contacts(LocalG());                                                         // standing: on the wheels' own points
        SeatOnWheels(ref, true);                                                    // parked: levelled
        still_ = 0.0;
    }

    MESHHANDLE mesh_ = nullptr;
    UINT meshIdx_ = 0;
    DEVMESHHANDLE dev_ = nullptr;
    double lastTh_[mpu::kWheels] = {9, 9, 9, 9, 9, 9, 9, 9}, lastDelta_[mpu::kWheels] = {}, lastSpin_[mpu::kWheels] = {};
    std::vector<Part> part_, steps_;
    Part bar_;
    bool cab_ = false;                       // Э.МПУ: the crew cabin and the thrust module (Variant = EMPU in the config)
    double bz0_ = kBodyZ0, bz1_ = kBodyZ1, btop_ = kBodyTop;
    double stepsOut_ = 1.0, stepsDrawn_ = -1.0;
    double height_[mpu::kWheels] = {}, n_[mpu::kWheels] = {}, delta_[mpu::kWheels] = {}, spin_[mpu::kWheels] = {};
    double k_ = 0.0, gSet_ = 0.0, steer_ = 0.0, drive_ = 0.0, speed_ = 0.0, yawRate_ = 0.0, hdgPrev_ = 0.0;
    double batt_ = 1.0, powerW_ = 0.0, still_ = 0.0;
    bool park_ = true;
    double armState_[mpu::kWheels] = {}, testT_ = 0.0, logT_ = 0.0;
    double ride_ = 0.0, rideSet_ = 0.0, vCap_ = kVcruise;
    bool brakeHeld_ = false, capsWas_ = false, absBrake_ = false;
    double rideTarget_ = 0.0;
    bool bWas_ = false, vWas_ = false, wasDriving_ = false, registered_ = false;
    int driver_ = 0;
    OBJHANDLE driverBody_ = nullptr;
    ocImpact_t impact_ = nullptr;
    std::map<OBJHANDLE, double> hitCool_, thrown_;
    double lastHit_ = 0.0, lastHitT_ = 0.0;
    XRSound* snd_ = nullptr;
    double obstHitT_ = 0.0, carHitT_ = 0.0;
    bool rideMoving_ = false, drivingMode_ = false, holding_ = true;
    double holdT_ = 0.0, stableT_ = 0.0, elevPrev_ = -1e9, softBlend_ = 0.0;
    double xPrev_[mpu::kWheels] = {}, susp_[mpu::kWheels] = {};
    double sink_[mpu::kWheels] = {}, slip_[mpu::kWheels] = {}, muEff_ = 0.5, sinkMean_ = 0.0;
    double knockCool_ = 0.0, hPrev_[mpu::kWheels] = {};
    PSTREAM_HANDLE dust_[mpu::kWheels] = {};
    double dustLv_[mpu::kWheels] = {}, vf_[mpu::kWheels] = {};
    bool dustAir_ = false, dustMade_ = false;
    OcApi api_;
    TvApi tv_;
    double cell_[kCellsN] = {kCellJ, kCellJ, kCellJ, kCellJ}, chargeW_ = 0.0;
    OcInterior fns_{};
    OcInteriorExt ext_{};
    ATTACHMENTHANDLE att_ = nullptr;
    const int selftest_ = std::getenv("MPU_SELFTEST") ? std::atoi(std::getenv("MPU_SELFTEST")) : 0;
    double tgtDist_ = -1.0;
    bool boarded_ = false;
    int tgtN_ = 0;
};

inline bool TVChassis::DriverState(OBJHANDLE person, ::MpuState* s) {
    if (!person || driverBody_ != person) return false;
    const double nominal = mpu::kCgH - kSag + kDeckY;
    s->speed = speed_;
    s->speedMax = speed_ < -0.1 ? kVreverse : vCap_;
    s->full = vCap_ >= kVmax ? 1 : 0;
    s->steer = steer_;
    s->deck = GetAltitude(ALTMODE_GROUND) + kDeckY;
    s->deckMin = nominal + kRideMin;
    s->deckMax = nominal + kRideMax;
    s->brake = park_ ? 1 : 0;
    s->charge = batt_;
    s->powerW = powerW_;
    s->energyKWh = batt_ * kBattJ / 3.6e6;
    s->rangeKm = RangeKm();
    return true;
}

}  // namespace
