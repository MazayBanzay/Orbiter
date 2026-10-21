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
#include "EmpuGeo.h"                // EMPU cabin and individually animated battery-cover groups
#include "OrbiterCrewApi.h"
#include "XRSound.h"
#include "TVApi.h"                  // the transport module: sockets, the station's cable, what people carry
#include "TVThermal.h"              // the bodies' day / night surface temperatures
#include "TVHeat.h"                 // heat damage on hot ground and lava (the vehicles' common model)
#include "LavaThermal.h"            // the lava field's rasters (Config\<body>\LavaField)
#include "../AtmoFX/AtmoFXHeat.h"   // the AtmoFX client's heat shimmer over warm parts (nothing with another client)
#include "../AtmoFX/AtmoFXPart.h"   // its free particles: the wheels' soil, the vents' ice (Orbiter's streams without it)
#include "../AtmoFX/AtmoFXDirt.h"   // the AtmoFX client's dirt on the machine's lower body
#include "../AtmoFX/AtmoFXMark.h"   // the AtmoFX client's marks on the ground: ruts, pits
#include "../AtmoFX/AtmoFXWake.h"   // the AtmoFX client's wakes and rings on a liquid surface
#include "../AtmoFX/AtmoFXFoam.h"   // the AtmoFX client's bubbles and foam from warm parts in a liquid
#include "TVGround.h"               // a site's ground-type raster (Config\OrbiterCrew\Ground, shared with the suit)

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
// W alone and Shift reach the same top speed (80 km/h); they differ in the drive's power: W is the cruise drive (calm: the
// throttle eased in, a fifth of the power, the force limited, the wheels held to 8 % slip - no spinning), Shift the full one
constexpr double kPcruise = 6.0e5, kFcruise = 8.0e4;   // W, N: the cruise drive's power and force
// The limiter (the terminal's «speed limiter», on by default). Off: the drive runs past its rating - 4.5 MW, 380 kN, up to
// 36 m/s (130 km/h: the hub motors' 6000 rpm and the airless tyres' rating) - and pays for it in heat: the iron losses grow
// with the speed (rpm^1.6), past the base speed (22 m/s) the field weakening adds its currents, the copper loses with the
// square of the force. The motors' heat (below): 3e5 J/K of iron, copper and coolant, cooled by a 20 m2 radiator loop and
// in air by the airflow; hot above 160 C the power drops, above 200 C the drive trips until it is below 147 C
constexpr double kPover = 4.5e6, kFover = 3.8e5, kVunl = 36.0;
constexpr double kMotC = 3.0e5, kIronW = 1.5e3, kFwW = 4.0e4, kCuW = 1.2e4;   // J/K; W per motor at 22 m/s; W at 10 m/s over base; W at 285 kN
constexpr double kMotDerate0 = 433.0, kMotTrip = 473.0, kMotReset = 420.0, kRadA = 20.0;   // K, K, K, m2
constexpr double kSlipCruise = 0.08;         // the slip the cruise drive holds the wheels to: they roll, they do not spin
constexpr double kVreverse = 4.2;            // m/s, 15 km/h backwards
constexpr double kWheelBaseHalf = 3.3;       // m, centre to the outer axles (all-wheel steer: the turning lever)
constexpr double kEjectV = 30.0 / 3.6;      // m/s: a stop harder than this throws the people off
constexpr double kRideMax = 0.40, kRideMin = 0.0;    // platform height from its normal: the normal is the lowest (the user), Space raises it, Ctrl brings it back down
// where people are on it (vessel frame): the deck top, its extent, the post, the steps on the left beside the post
constexpr double kDeckY = 0.62, kDeckX = 1.45, kDeckZ0 = -4.1, kDeckZ1 = 3.0, kPostPlateX = 1.0, kPostPlateZ1 = 4.2;
constexpr double kBodyX = 2.20, kBodyZ0 = -4.45, kBodyZ1 = 4.50, kBodyTop = 0.90;   // the outer box people meet
const VECTOR3 kPostFeet = {0.0, kDeckY, 3.30}, kOnDeck = {-1.5, kDeckY, 2.5}, kStepFoot = {-1.90, -0.9, 0.0};   // ground just outside the lowest side step
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
// Titan (the user, 2026-10-18; estimates from Huygens: its penetrometer met ~0.1-0.4 MPa under a thin crust, the ground damp
// with methane, rounded water-ice cobbles of 5-15 cm round it). Damp organic sand: cohesive from the methane in it (c ~2 kPa,
// as damp terrestrial sand), friction 25 deg, light (organics and ice, ~1000 kg/m3), softer than lunar regolith (kphi);
// rounded ice pebbles: no cohesion, interlocking friction 38 deg, stiff (gravel's kphi); bedrock water ice at 94 K: no
// sinkage worth the name, ice on ice ~0.5 (27 deg); the wet margin: sand saturated with liquid methane, weaker and softer;
// the liquid's bed (the sea's own floor): soft sediment - the liquid's buoyancy and drag are not modelled
const Soil kTitanSand    = {"Titan damp sand",     0.9, 2000.0,   400000.0, 2000.0, 25.0 * RAD, 0.020, 1000.0};
const Soil kTitanPebbles = {"Titan ice pebbles",   1.0,    0.0,  5000000.0,    0.0, 38.0 * RAD, 0.030,  600.0};
const Soil kTitanIce     = {"Titan bedrock ice",   1.0,    0.0, 50000000.0,    0.0, 27.0 * RAD, 0.010,  930.0};
const Soil kTitanWet     = {"Titan wet margin",    0.8, 1000.0,   150000.0, 1000.0, 20.0 * RAD, 0.025, 1300.0};
const Soil kTitanBed     = {"Titan sea bed",       0.8,  500.0,    50000.0,  500.0, 15.0 * RAD, 0.030, 1300.0};
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
        // Orbiter's rotation drag is an angular deceleration w q S_y r (not divided by the inertia - an aircraft's model): 0.5
        // killed any yaw in an atmosphere in milliseconds (the user, 2026-10-19: «очень туго поворачивают на Земле, на Титане
        // вообще плохо» - Titan's gas is 4.5x denser). A box of 9.5 x 3 m yawing at 10 m/s in air is damped by its crossflow at
        // ~0.01 w per second: r ~1e-5 gives that order (it grows as v^2 here, not v - still negligible on the ground)
        SetRotDrag(_V(1e-5, 1e-5, 1e-5));
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
        // XRSound's own default sounds (tyres rolling, wheel chirp, touchdown, crunch, brakes, wind...) off: ours are physical
        // (none through vacuum) - its were heard on the Moon (the user, 2026-10-21)
        if (snd_ && snd_->IsPresent())
        {
            for (int id = XRSound::AirConditioning; id < XRSound::LastDefaultSound; ++id) snd_->SetDefaultSoundEnabled((XRSound::DefaultSoundID)id, false);
            for (int id = XRSound::RadioATCGroup; id < XRSound::MusicFolder; ++id) snd_->SetDefaultSoundEnabled((XRSound::DefaultSoundID)id, false);
        }
        if (snd_ && snd_->IsPresent()) {
            using PT = XRSound::PlaybackType;
            snd_->LoadWav(kSndDrive, "XRSound\\MPU\\drive.wav", PT::Global);          // played faster / slower with the speed
            snd_->LoadWav(kSndInverter, "XRSound\\MPU\\inverter.wav", PT::Global);
            snd_->LoadWav(kSndHum, "XRSound\\MPU\\hum.wav", PT::Global);
            snd_->LoadWav(kSndRoll, "XRSound\\MPU\\roll.wav", PT::Global);
            snd_->LoadWav(kSndKnock, "XRSound\\MPU\\knock.wav", PT::Global);
            snd_->LoadWav(kSndLift, "XRSound\\MPU\\lift.wav", PT::Global);
            static const char* sfx[] = {"brake", "ladder_dn", "ladder_up", "airlock", "click", "tick", "crash", "couple", "uncouple", "alarm", "beep", "wind"};
            for (int k = 0; k < 12; ++k) { char f[64]; std::snprintf(f, sizeof f, "XRSound\\MPU\\%s.wav", sfx[k]); snd_->LoadWav(kSndBrake + k, f, PT::Global); }
            // the second pass (Tantra_Design/tools/make_mpu_sounds_v2.py): the load, the grounds, the frame, the suspension
            static const char* sfx2[] = {"drive_load", "roll_sand", "roll_ice", "rattle", "skid", "bumpstop", "susp1", "susp2", "susp3", "susp4"};
            for (int k = 0; k < 10; ++k) { char f[64]; std::snprintf(f, sizeof f, "XRSound\\MPU\\%s.wav", sfx2[k]); snd_->LoadWav(kSndDriveLoad + k, f, PT::Global); }
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
            else if (!std::strncmp(line, "LIMITER", 7)) { int p = 1; std::sscanf(line + 7, "%d", &p); limOff_ = p == 0; }     // LIMITER 0: the speed limiter is off
            else if (!std::strncmp(line, "DAMAGE", 6)) {
                std::sscanf(line + 6, "%lf %lf %lf %lf %lf %lf %lf %lf %lf", &hullLeak_, &wheelHp_[0], &wheelHp_[1], &wheelHp_[2], &wheelHp_[3],
                            &wheelHp_[4], &wheelHp_[5], &wheelHp_[6], &wheelHp_[7]);
                hullLeak_ *= 1e-4;                                                // written in cm2
            }
            else if (!std::strncmp(line, "HEATW", 5)) {                           // one wheel's heat: tyre K, its damage, motor K, its damage, melt m
                int i = -1; double t = -1, d = 0, mt = -1, md = 0, ms = 0, mtg = 0;
                if (std::sscanf(line + 5, "%d %lf %lf %lf %lf %lf %lf", &i, &t, &d, &mt, &md, &ms, &mtg) >= 5 && i >= 0 && i < mpu::kWheels) {
                    tyreT_[i] = t; tyreDmg_[i] = Clamp(d, 0.0, 1.0); motDmg_[i] = Clamp(md, 0.0, 1.0); meltSink_[i] = (std::max)(0.0, ms); meltTarget_[i] = (std::max)(0.0, mtg);
                    if (mt > 0.0) { motTi_[i] = mt; motT_ = (std::max)(motT_, mt); }
                }
            }
            else if (!std::strncmp(line, "ATMOFX_MARK", 11)) AtmoFXMark::Load(GetHandle(), line + 11);   // its ruts and pits, to the client
            else if (!std::strncmp(line, "PITN", 4)) {                            // the next pit's number of each wheel (so a new pit is new)
                std::sscanf(line + 4, "%d %d %d %d %d %d %d %d", &pitN_[0], &pitN_[1], &pitN_[2], &pitN_[3], &pitN_[4], &pitN_[5], &pitN_[6], &pitN_[7]);
                for (int& k : pitN_) k = (k + 1) % 50;                            // past the saved ones
            }
            else if (!std::strncmp(line, "DIRT", 4)) { std::sscanf(line + 4, "%lf %lf %lf %lf", &dirt_, &dirtC_[0], &dirtC_[1], &dirtC_[2]); dirt_ = Clamp(dirt_, 0.0, 1.0); }
            else if (!std::strncmp(line, "FLOOD", 5)) { std::sscanf(line + 5, "%lf", &floodV_); }   // m3 of the sea in the cabin
            else if (!std::strncmp(line, "CLOG", 4)) {                            // the sand stuck in each wheel's mesh, 0..1
                std::sscanf(line + 4, "%lf %lf %lf %lf %lf %lf %lf %lf", &clog_[0], &clog_[1], &clog_[2], &clog_[3], &clog_[4], &clog_[5], &clog_[6], &clog_[7]);
                for (double& c : clog_) c = Clamp(c, 0.0, 1.0);
            }
            else if (!std::strncmp(line, "HEATB", 5)) {                           // the belly: K, damage, burnt through, stage
                int holed = 0; std::sscanf(line + 5, "%lf %lf %d %d", &bellyT_, &bellyDmg_, &holed, &bellyStage_);
                bellyHoled_ = holed != 0; bellyDmg_ = Clamp(bellyDmg_, 0.0, 1.0);
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
        if (limOff_) oapiWriteScenario_int(scn, const_cast<char*>("LIMITER"), 0);
        bool hurt = hullLeak_ > 0.0; for (double h : wheelHp_) hurt |= h < 1.0;
        if (hurt) {
            char db[160]; std::snprintf(db, sizeof db, "%.2f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f", hullLeak_ * 1e4, wheelHp_[0], wheelHp_[1], wheelHp_[2],
                                        wheelHp_[3], wheelHp_[4], wheelHp_[5], wheelHp_[6], wheelHp_[7]);
            oapiWriteScenario_string(scn, const_cast<char*>("DAMAGE"), db);
        }
        { bool any = false; for (double c : clog_) any |= c > 0.005;
          if (any) { char cb2[128]; std::snprintf(cb2, sizeof cb2, "%.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f", clog_[0], clog_[1], clog_[2], clog_[3], clog_[4], clog_[5], clog_[6], clog_[7]);
                     oapiWriteScenario_string(scn, const_cast<char*>("CLOG"), cb2); } }
        {   // the marks it left on the ground stay with the scenario (the client keeps them; ours: the newest 3000)
            char pb[96]; std::snprintf(pb, sizeof pb, "%d %d %d %d %d %d %d %d", pitN_[0], pitN_[1], pitN_[2], pitN_[3], pitN_[4], pitN_[5], pitN_[6], pitN_[7]);
            oapiWriteScenario_string(scn, const_cast<char*>("PITN"), pb);
            AtmoFXMark::Save(GetHandle(), scn);
        }
        if (dirt_ > 0.002) { char db2[96]; std::snprintf(db2, sizeof db2, "%.3f %.3f %.3f %.3f", dirt_, dirtC_[0], dirtC_[1], dirtC_[2]); oapiWriteScenario_string(scn, const_cast<char*>("DIRT"), db2); }
        if (floodV_ > 0.001) { char fb[32]; std::snprintf(fb, sizeof fb, "%.3f", floodV_); oapiWriteScenario_string(scn, const_cast<char*>("FLOOD"), fb); }
        if (heatInit_) {                                                          // the heat nodes and their damage (TVHeat.h)
            for (int i = 0; i < mpu::kWheels; ++i) {
                char hb[160]; std::snprintf(hb, sizeof hb, "%d %.1f %.4f %.1f %.4f %.3f %.3f", i, tyreT_[i], tyreDmg_[i], motTi_[i], motDmg_[i], meltSink_[i], meltTarget_[i]);
                oapiWriteScenario_string(scn, const_cast<char*>("HEATW"), hb);
            }
            char bb[96]; std::snprintf(bb, sizeof bb, "%.1f %.4f %d %d", bellyT_, bellyDmg_, bellyHoled_ ? 1 : 0, bellyStage_);
            oapiWriteScenario_string(scn, const_cast<char*>("HEATB"), bb);
        }
        ModuleSave(scn);
    }


    void clbkPreStep(double simt, double simdt, double mjd) override {
        (void)simt; (void)mjd;
        if (simdt <= 0.0) return;
        OBJHANDLE ref = GetSurfaceRef();
        if (!ref) return;
        const double g = LocalG();
        if (std::fabs(g - gSet_) > 0.05 * gSet_) { Probe("contacts again: g %.3f (set for %.3f)", g, gSet_); Contacts(g); }
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
                UpdateHatches(simdt);
                UpdateCells(simdt);
                Animate();
                Steps(simdt);
                return;
            }
        }

        // the parked rest decided last step goes in now, before any force of this step (the user, 2026-10-19: «смещение на
        // грунте»). Rested at the end of a step, the wheels' forces already given in that step lifted it straight out of
        // Orbiter's landed state (the probe: «Orbiter took it out» one frame after each rest) - the soft ride rocked it, it
        // rested again, re-seated by up to 9 cm: the machine twitched and hopped every second
        bool restedNow = false;
        if (restPending_) { restPending_ = false; Rest(ref); restedNow = true; }
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
            driveIn = lead->drive_; full = !lead->cruise_; park_ = lead->park_;
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
        if (std::fabs(ride_ - rideSet_) > 0.01) { if (!rideMoving_) Probe("contacts again: ride %.3f (set for %.3f)", ride_, rideSet_); Contacts(g); }
        vCap_ = vTop(); cruise_ = !full;
        if (selftest_ == 2) park_ = false;
        if (selftest_ == 3) { testT_ += simdt; }
        else if (selftest_ == 4) {                                                // =4: every platform full speed straight ahead
            testT_ += simdt;
            if (testT_ > 1.0) { driveIn = 1.0; full = true; vCap_ = kVmax; cruise_ = false; }
        }
        else if (selftest_ == 5) {                                                // =5: as 4 for 10 s, then hands off: it must stand still
            testT_ += simdt;
            if (testT_ > 1.0 && testT_ < 10.0) { driveIn = 1.0; full = true; vCap_ = kVmax; cruise_ = false; }
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
                driveIn = 1.0; full = true; vCap_ = kVmax; cruise_ = false;
                steerIn = Clamp(std::atan2(tl.x, tl.z) * 1.5, -1.0, 1.0);
                if (std::fabs(std::atan2(tl.x, tl.z)) > 0.5 && std::fabs(speed_) > 4.0) driveIn = -1.0;   // slow down to turn
            }
        }
        steer_ += Clamp(steerIn - steer_, -4.0 * simdt, 4.0 * simdt);          // the steering: quick (a quarter second lock to lock)
        // the throttle: eased in on the cruise drive (0.35 per second - a smooth start), at once with Shift and on the way down
        if (full || driveIn <= drive_) driveSm_ = driveIn;
        else driveSm_ += (std::min)(driveIn - driveSm_, 0.35 * simdt);
        if (std::fabs(driveIn) < 0.05) driveSm_ = 0.0;
        drive_ = driveSm_;
        absBrake_ = drive_ < 0.0 && speed_ > 0.5;                                  // S while going forward: the service brake (ABS)
        if (drive_ > 0.0 && speed_ < -0.5) absBrake_ = true;                       // W while rolling back: the same
        if (absBrake_) drive_ = 0.0;
        if (std::fabs(drive_) > 0.05 && park_) park_ = false;                    // driving off releases the brake

        VESSELSTATUS2 st;
        std::memset(&st, 0, sizeof st);
        st.version = 2;
        GetStatusEx(&st);
        if (restedNow) st.status = 1;                                             // just set landed: no wheel forces this step
        if (st.status != 1 && !drivingMode_) { Probe("Orbiter took it out of the landed state (we did not)"); drivingMode_ = true; Contacts(g); for (double& q : xPrev_) q = -1e9; }
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
            wLat_[i] = lat; wLng_[i] = lng;                                      // where it stands (the lava field, HeatStep)
            soilW_[i] = &SoilAt(ref, lat, lng, gndKind_[i]);                      // and on what (a site's ground raster)
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
                    // the hole it dug (dig_): the ground under it is that much lower - the body settles into it on the soft
                    // spring (kS per metre), down to its belly's own points
                    double add = (-(k_ - kS) * (x - kSag) - kS * dig_[i] + cS * xd) * softBlend_;   // eased in over 1 s after it starts
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
        double mechW = 0.0;
        for (int i = 0; i < mpu::kWheels; ++i) pI_[i] = fI_[i] = 0.0;
        for (double& x : vf_) x = 0.0;
        if (st.status != 1) {
            const double vAbs = std::fabs(v.z);
            const Soil& S = SoilOf(ref);
            // every wheel on the soil at its load: sinkage, contact, the peak shear force, the resistances
            WheelSoil ws[mpu::kWheels];
            double tractMax = 0.0, wSum = 0.0;
            for (int i = 0; i < mpu::kWheels; ++i) {
                const Soil& S = *soilW_[i];                                       // this wheel's own ground
                ws[i] = SoilUnder(S, (std::max)(n_[i], 1.0), kWheelRflex, mpu::kWheelW, g);
                if (meltSink_[i] > 0.02) ws[i].Hmax *= tvheat::kMeltGrip;
                ws[i].Hmax *= 1.0 - 0.25 * clog_[i];                              // the tread clogged with sticky sand
                if (dig_[i] > 0.0) {                                              // dug in by its own spinning: a hole to climb out of
                    // the hole is soil carried away, not compacted: the wheel climbs its wall - a rigid wheel's motion
                    // resistance ~ its sinkage over its contact length, sqrt(z / 2R) of its load. (Before: Bekker's compaction
                    // over the dug depth, kk z^(n+1) - a pressure the wheel never applies: 26 cm on the wet margin came to
                    // 2.4 x the load, the grip went negative, the machine could not move - the user, 2026-10-19)
                    const double z0 = ws[i].z, z1 = z0 + dig_[i], D = 2.0 * mpu::kWheelR;
                    ws[i].Rc += (std::max)(n_[i], 0.0) * (std::sqrt(z1 / D) - std::sqrt(z0 / D));
                }      // through the crust: only the mush and the crust's edges to push on
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
            const bool cruise = cruise_;
            // stuck (the throttle open, hardly moving): the cruise drive's power, force and slip limits lift to the full drive over
            // ~3 s - a soft bank, a rut, a slope: it climbs out; moving off, the slip allowed is wider at low speed (a start)
            if (cruise && std::fabs(drive_) > 0.5 && vAbs < 1.0) stuckT_ += simdt;   // backwards too (the user: «задний ход почти пропал») else stuckT_ = (std::max)(0.0, stuckT_ - 2.0 * simdt);
            const double boost = Clamp(stuckT_ / 2.0 - 0.5, 0.0, 1.0);
            if (boost > 0.0 && !boostWas_) {                                       // and why, for the log
                double dg = 0.0, cl = 0.0, sk = 0.0; for (int i = 0; i < mpu::kWheels; ++i) { dg = (std::max)(dg, dig_[i]); cl = (std::max)(cl, clog_[i]); sk += sink_[i] / mpu::kWheels; }
                oapiWriteLogV("TVChassis %s: stuck - the drive's full power lifted (grip %.2f, sinkage %.3f m, dug %.3f m, sand stuck %.0f %%, lift %.2f, ground %d)",
                              GetName(), muEff_, sk, dg, cl * 100.0, buoyFrac_, gndKind_[0]);
            }
            boostWas_ = boost > 0.0;
            slipCruise_ = kSlipCruise + 0.10 * Clamp(1.0 - vAbs / 3.0, 0.0, 1.0) + 0.27 * boost;
            const double pDrv = cruise ? kPcruise + (kPmax - kPcruise) * boost : limOff_ ? kPover : kPmax,
                         fDrv = cruise ? kFcruise + (kFmotor - kFcruise) * boost : limOff_ ? kFover : kFmotor;
            double fCmd = drive_ * pDrv / (std::max)(vAbs, 2.0);
            fCmd = Clamp(fCmd, -fDrv, fDrv);

            // the speed limit holds both ways, downhill too: above it the motors brake (and give the energy back). Forward:
            // 55 km/h on W, 80 with Shift, 80 rolling free; backwards: 15 on S, 80 rolling free
            {
                const double m = GetMass();
                const double capF = drive_ > 0.05 ? vCap_ : vTop(), capR = drive_ < -0.05 ? kVreverse : kVmax;
                if (v.z > capF) fCmd = -Clamp(m * 1.5 * (v.z - capF), 0.0, kFmotor);
                else if (v.z < -capR) fCmd = Clamp(m * 1.5 * (-capR - v.z), 0.0, kFmotor);
                else if (fCmd > 0.0 && v.z > capF - (cruise ? 1.5 : 0.3)) fCmd *= Clamp((capF - v.z) / (cruise ? 1.5 : 0.3), 0.0, 1.0);   // the top speed, eased into
                else if (fCmd < 0.0 && v.z < -capR + 0.3) fCmd *= Clamp((capR + v.z) / 0.3, 0.0, 1.0);
            }
            const bool brake = park_ || brakeHeld_ || (std::fabs(drive_) < 0.05 && vAbs < 0.4);
            for (int i = 0; i < mpu::kWheels; ++i) {
                const auto& W = mpu::kWheel[i];
                const Soil& S = *soilW_[i];
                const double axle = W.hub[2];
                // every wheel aimed at one turning centre (Ackermann over all four axles): the front axle at steer * sMax, the rear
                // at rearK of it; the middle axles and the inner / outer wheels follow from that centre. Before, the middle axles
                // took a fixed half of the front's angle and both sides the same - the wheels scrubbed sideways against the turn
                double d = 0.0;
                {
                    const double zf = mpu::kWheel[0].hub[2], zr = mpu::kWheel[mpu::kWheels - 1].hub[2];
                    const double tf = std::tan(steer_ * sMax), tr = std::tan(steer_ * sMax * rearK);
                    if (std::fabs(tf - tr) > 1e-6) {
                        const double Rc = (zf - zr) / (tf - tr), z0 = zf - Rc * tf;   // the centre: Rc across (+ right), z0 along
                        d = std::atan2((axle - z0), (Rc - W.hub[0]) * (Rc >= 0.0 ? 1.0 : -1.0)) * (Rc >= 0.0 ? 1.0 : -1.0);
                    }
                }
                delta_[i] = d;
                sink_[i] = n_[i] > 0.0 ? ws[i].z : 0.0;
                sinkMean_ += sink_[i] / mpu::kWheels;
                if (n_[i] <= 0.0) { slip_[i] = 0.0; FreeWheel(i, d, v, fCmd, simdt); continue; }   // off the ground (or the sea's bed)
                const double vx = v.x + yawRate_ * axle, vz = v.z - yawRate_ * W.hub[0];
                const double sd = std::sin(d), cd = std::cos(d);
                const double vf = vx * sd + vz * cd, vl = vx * cd - vz * sd;   // along / across the wheel
                const WheelSoil& w = ws[i];
                // along the wheel: the motor's share drives it through the soil's shear (Janosi-Hanamoto) at the slip that
                // gives it; more than the soil holds - the wheel spins (slip 1) and gives what it can. Braking: the wheel
                // locked, skidding (slip 1 against the motion). Always: the compaction of the rut and the tread's loss
                double ff = 0.0, sl = 0.0, wantF = 0.0;                        // wantF: the motor's force at the rim, N
                if (brake || WheelHealth(i) < 0.15) {                                // the brake - or a wrecked wheel: locked, it skids
                    sl = -1.0;
                    ff = -ShearAt(S, w, 1.0) * std::tanh(vf / 0.2);
                } else if (absBrake_) {                                           // S: the service brake with ABS - the best slip,
                    sl = -kSlipABS;                                               // no lock, the wheels still steer
                    ff = -ShearAt(S, w, kSlipABS) * std::tanh(vf / 0.3);
                } else if (fCmd != 0.0) {
                    double want = std::fabs(fCmd) * n_[i] / (std::max)(wSum, 1.0) * WheelHealth(i) * (fCmd > 0.0 ? motDer_[i] : 1.0) + w.Rc;   // a damaged or hot motor gives less
                    // traction control on W: no more than the soil gives at 8 % slip (rolling, no spinning, no digging); Shift: none
                    if (cruise_ && fCmd * vf >= 0.0) want = (std::min)(want, ShearAt(S, w, slipCruise_));
                    sl = SlipFor(S, w, want);
                    wantF = want;
                    ff = (fCmd > 0 ? 1.0 : -1.0) * (std::min)(want, ShearAt(S, w, 1.0));
                    if (fCmd * vf < 0.0) sl = -sl;                                  // the motors against the motion: braking slip
                }
                ff -= (w.Rc + 0.01 * n_[i] * (1.0 + 4.0 * (1.0 - WheelHealth(i)))) * std::tanh(vf / 0.3);   // a bent (or burnt) wheel drags
                if (meltSink_[i] > 0.02) ff -= tvheat::MeltDrag(mpu::kWheelR, mpu::kWheelW, meltSink_[i], vf) * std::tanh(vf / 0.3);   // through the melt
                ff -= 0.03 * clog_[i] * n_[i] * std::tanh(vf / 0.3);                // the stuck sand: a heavier, lumpy tyre rolls harder
                GroundFeel(i, vf, simdt);
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
                const double rim = WheelSpin(i, S, w, sl, vf, wantF, fCmd, brake, simdt);   // the wheel's own rim speed (inertia, spin-up)
                Marks(i, vf, d, simdt);                                                     // its rut and its pit on the ground
                if (!brake) {                                                     // driving or ABS braking by the motors
                    const double p = ff * rim;                                    // the motors' power through the slip
                    powerW += p > 0.0 ? p / 0.92 : p * 0.6;
                    mechW += p; pI_[i] = p; fI_[i] = std::fabs(ff);
                }
                spin_[i] = std::fmod(spin_[i] + rim * simdt / mpu::kWheelR, PI2);
                vf_[i] = vf; rim_[i] = rim;
            }
            // standing still with no drive asked for (half a second): Orbiter's landed state - no physics, it does not
            // move or tremble (the tyre forces round zero speed made it twitch along the wheels). Time warp above 10x:
            // landed at once, nothing computed
            const bool contact = nSum > 0.0;
            still_ = std::fabs(drive_) < 0.05 && contact && length(v) < 0.15 ? still_ + simdt : 0.0;
            // and only once the soft ride has settled (no bounce left, the body at its height - the re-seat puts it at its
            // static height: resting mid-bounce it jumped by the bounce), or after 4 s still whatever it does
            double digMean = 0.0; for (double dg : dig_) digMean += dg / mpu::kWheels;   // settled into its holes: that much lower
            const double sag = GetAltitude(ALTMODE_GROUND) - (mpu::kCgH - kSag + ride_ - digMean);
            const bool settled = std::fabs(v.y) < 0.03 && std::fabs(sag) < 0.015;
            // never on its side or roof: the re-seat stands it on its wheels - a rolled machine stays where it lies
            VECTOR3 upH; HorizonRot(_V(0, 1, 0), upH);
            const bool upright = upH.y > 0.75;
            if (upright && ((buoyFrac_ < 0.9 && ((still_ > 0.5 && settled) || still_ > 4.0)) || (contact && oapiGetTimeAcceleration() > 4.0))) {
                if (!restPending_) Probe("rest: still %.2f s, |v| %.3f m/s, vertical %.3f m/s, %+.3f m off its height, load %.2f of mg", still_, length(v), v.y, sag, nSum / (GetMass() * g));
                restPending_ = true;                                              // at the next step's start (above)
            }
        } else if (std::fabs(drive_) > 0.05 && batt_ > 0.0 && oapiGetTimeAcceleration() <= 4.0) {
            // drive asked: out of the landed state. A small force did not always wake it (it stayed until the deck height was
            // changed - that sets the touchdown points again): setting them again leaves the landed state every time
            drivingMode_ = true;
            Probe("wake: drive %.2f", drive_);
            SeatOnWheels(ref);                                                    // out of the levelled pose: back on the ground's plane first
            Contacts(g);
            for (double& q : xPrev_) q = -1e9;                                    // no damper kick on the first step
        }
        ImmersionStep(simdt, st.status != 1 && !restedNow);
        HeatStep(simdt);
        ProbeStep(simdt);
        Dirt(simdt);
        MotorThermal(simdt);
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
        UpdateHatches(simdt);
        UpdateCells(simdt);
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
        if (!cab_ && !selftest_) return true;                    // МПУ: no readouts over the outside view (the user, 2026-10-21)
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
        if (liquidHere_) {                                                        // in a liquid
            if (floating_) std::snprintf(buf, sizeof buf, "AFLOAT: lift %.2f of the weight, drifting%s", buoyFrac_, floodV_ > 0.01 ? " - FLOODING" : "");
            else std::snprintf(buf, sizeof buf, "wading %.2f m deep, lift %.2f of the weight%s", wadeDepth_, buoyFrac_, floodV_ > 0.01 ? " - FLOODING" : "");
            skp->Text(x, y - dy, buf, (int)std::strlen(buf));
        }
        if (heatInit_ && (HeatAlarm() || heatGroundK_ > 400.0)) {                 // the heat: the hottest tyre, the belly, wheels in the melt
            int hot = 0, melt = 0; double tMax = 0.0, dMax = 0.0;
            for (int i = 0; i < mpu::kWheels; ++i) { if (tyreT_[i] > tyreT_[hot]) hot = i; dMax = (std::max)(dMax, tyreDmg_[i]); melt += InMelt(i); }
            tMax = tyreT_[hot];
            // (this HUD line is English as the rest of it: the system font draws no Cyrillic here)
            std::snprintf(buf, sizeof buf, "heat: tyres to %.0f C (burnt %.0f %%)  belly %.0f C (burnt %.0f %%)%s",
                          tMax - 273.15, dMax * 100.0, bellyT_ - 273.15, bellyDmg_ * 100.0, melt ? "  WHEELS IN THE MELT" : "");
            skp->Text(x, y + 3 * dy, buf, (int)std::strlen(buf));
        }
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
        Sfx(kSndCouple, 1.0);
        towing_ = follower; follower->towBy_ = this;
        towL_ = follower->towL_ = Clamp(gap, kBarMin, kBarMax);
        oapiWriteLogV("TVChassis: %s coupled to %s, bar %.2f m", follower->GetName(), GetName(), towL_);
    }
    void Uncouple() {
        Sfx(kSndUncouple, 1.0);
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
        Probe("wake: pulled or pushed (tow bar)");
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
        Sfx(kSndCrash, Clamp(dv / 12.0, 0.25, 1.0));
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
    enum { kActPlug = 1, kActUnplug, kActCellOut, kActCellIn, kActCouple, kActUncouple, kActDoorToggle };
    int ChassisNodeCount() const { return 3 + kCellsN; }
    // the menu's language (OrbiterCrew's one setting, Config\OrbiterCrew\OrbiterCrew.cfg LANGUAGE): Russian or English
    bool En() const { return api_.English(); }
    const char* L(const char* ru, const char* en) const { return En() ? en : ru; }
    bool Carried(int person, TvCarry* out) {
        if (out) *out = TvCarry{};
        if (tv_.CarryGet && tv_.CarryGet(person, out)) return true;
        if (!api_.HeldOf) return false;
        OcHeld h{}; h.size = sizeof h;
        if (!api_.HeldOf(person, &h) || h.kind != OC_HELD_CELL || std::strcmp(h.owner, "TVehicles") || std::strncmp(h.data, "cell ", 5)) return false;
        if (out) { out->kind = TV_CELL; out->energyJ = Clamp(std::atof(h.data + 5), 0.0, 1.0) * kCellJ; }
        if (tv_.CarrySet) { TvCarry c{}; c.kind = TV_CELL; c.energyJ = Clamp(std::atof(h.data + 5), 0.0, 1.0) * kCellJ; tv_.CarrySet(person, &c); }
        return true;
    }
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
        TvCarry c{}; const bool carries = Carried(person, &c);
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
            // A slot has one owner and one in-flight transfer. Do not expose a second
            // operation until the first transfer has either committed or rolled back.
            if (cellMotion_[k] != 0) {
                const bool takingOut = cellMotion_[k] == 1 || cellMotion_[k] == 3;
                tvAct(out, n, max, takingOut ? kActCellOut : kActCellIn,
                      L("ячейка перемещается", "cell is moving"), false,
                      L("дождитесь завершения движения", "wait for the transfer to finish"), 1.2);
                return n;
            }
            if (cab_) tvAct(out, n, max, kActDoorToggle, L(hatchTarget_[k] ? "закрыть крышку" : "открыть крышку", hatchTarget_[k] ? "close access cover" : "open access cover"),
                            !busy, busy ? L("нужны свободные руки", "free hands required") : nullptr, 0.8);
            if (cab_ && hatchOpen_[k] < 0.995) {
                tvAct(out, n, max, kActCellOut, L("взять ячейку", "take the cell"), false, L("сначала откройте крышку", "open the cover first"), 1.2);
            } else if (cell_[k] >= 0) {
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
        for (int i = 0; i < n; ++i) if (a[i].id == act) {
            if (!a[i].available) return 0;
            if (node >= kNodeCell0 && node < kNodeCell0 + kCellsN && (act == kActCellOut || act == kActCellIn)) {
                const int k = node - kNodeCell0;
                const int flow = act == kActCellOut ? 1 : 2;
                if (cellMotion_[k] == 0) { cellMotion_[k] = flow; cellProgress_[k] = 0.0; cellActor_[k] = person; }
            }
            return 1;
        }
        return 0;
    }
    void ChassisEnd(int node, int act, int person, int completed) {
        const bool cellAction = node >= kNodeCell0 && node < kNodeCell0 + kCellsN && (act == kActCellOut || act == kActCellIn);
        const int k = cellAction ? node - kNodeCell0 : -1;
        if (cellAction) {
            const int expected = act == kActCellOut ? 1 : 2;
            // End belongs to the reservation created by Begin. Never re-run Actions/Begin
            // here: availability can legitimately change while the actor is transferring it.
            if (cellMotion_[k] != expected || cellActor_[k] != person) return;
            if (!completed) { cellMotion_[k] = expected == 1 ? 3 : 4; return; }
            ChassisUse(kItemCell0 + k, person);
            const bool moved = act == kActCellOut ? cell_[k] < 0 : cell_[k] >= 0;
            if (moved) { cellMotion_[k] = 0; cellProgress_[k] = 0.0; cellActor_[k] = 0; }
            else cellMotion_[k] = expected == 1 ? 3 : 4;
            return;
        }
        if (!completed) return;
        if (!ChassisBegin(node, act, person)) { if (cellAction) cellMotion_[k] = cellMotion_[k] == 1 ? 3 : 4; return; }
        if (act == kActDoorToggle && node >= kNodeCell0 && node < kNodeCell0 + kCellsN) { hatchTarget_[node - kNodeCell0] = !hatchTarget_[node - kNodeCell0]; return; }
        if (node == kNodeSocket) ChassisUse(kItemSocket, person);
        else if (node == kNodeHitchF) ChassisUse(kItemHitchF, person);
        else if (node == kNodeHitchR) ChassisUse(kItemHitchR, person);
    }
    bool ChassisItem(int i, OcItem* out) const {
        *out = OcItem{};
        out->kind = OC_AIRLOCK; out->dir = _V(-1, 0, 0); out->radius = 1.5;
        if (i == 0) {
            out->id = kItemSocket; out->pos = kSocket; out->dir = _V(0, 0, -1); out->radius = 1.8;
            if (tv_.Ok() && tv_.PluggedTo(GetHandle()))
                std::snprintf(out->label, sizeof out->label, L("отключить кабель (заряд %.0f %%, %.0f кВт)", "unplug the cable (charge %.0f %%, %.0f kW)"), batt_ * 100.0, chargeW_ * 1e-3);
            else std::snprintf(out->label, sizeof out->label, L("подключить кабель (заряд %.0f %%)", "plug the cable in (charge %.0f %%)"), batt_ * 100.0);
            return true;
        }
        if (i == 1 + kCellsN || i == 2 + kCellsN) {                    // the couplings
            const bool rear = i == 2 + kCellsN;
            out->id = rear ? kItemHitchR : kItemHitchF; out->radius = 1.8;
            out->pos = _V(0.0, 0.06, rear ? -4.95 : 4.95); out->dir = _V(0, 0, rear ? 1 : -1);
            TVChassis* linked = rear ? towing_ : towBy_;
            double gap = 0; TVChassis* cand = linked ? nullptr : Candidate(rear, &gap);
            if (linked) std::snprintf(out->label, sizeof out->label, L("расцепить с %s", "uncouple from %s"), linked->GetName());
            else if (cand) std::snprintf(out->label, sizeof out->label, L("сцепить с %s (%.1f м)", "couple to %s (%.1f m)"), cand->GetName(), gap);
            else std::snprintf(out->label, sizeof out->label, "%s", rear ? L("сцепка: сзади никого (встать в 2–4 м)", "coupling: nobody behind (stop 2-4 m off)")
                                                                          : L("сцепка: спереди никого (встать в 2–4 м)", "coupling: nobody ahead (stop 2-4 m off)"));
            return true;
        }
        const int k = i - 1;
        if (k < 0 || k >= kCellsN) return false;
        out->id = kItemCell0 + k; out->pos = _V(1.30, -0.05, kCellZ[k]);
        if (cell_[k] >= 0) std::snprintf(out->label, sizeof out->label, L("вынуть ячейку %d (%.0f %%)", "take cell %d out (%.0f %%)"), k + 1, cell_[k] / kCellJ * 100.0);
        else std::snprintf(out->label, sizeof out->label, L("вставить ячейку в гнездо %d", "put a cell into bay %d"), k + 1);
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
        if (id == kItemSocket) {
            if (!tv_.Ok()) return;
            if (tv_.PluggedTo(GetHandle())) { if (tv_.Unplug(GetHandle(), person)) oapiWriteLogV("TVChassis %s: the cable unplugged", GetName()); }
            else if (tv_.Plug(GetHandle(), person)) oapiWriteLogV("TVChassis %s: the cable plugged in", GetName());
            return;
        }
        const int k = id - kItemCell0;
        if (k < 0 || k >= kCellsN) return;
        if (cab_ && hatchOpen_[k] < 0.995) return;                           // the battery bay stays physically inaccessible under its cover
        TvCarry c{};
        const bool carries = Carried(person, &c);
        const bool hands = !carries && !tvHolding(api_, person);
        if (cell_[k] >= 0 && hands) {                                   // out into her hands (OrbiterCrew may refuse: too heavy here)
            OcHeld h; tvHeldCell(&h, cell_[k] / kCellJ);
            if (!tvHandOver(api_, person, &h)) return;
            c = {TV_CELL, GetHandle(), cell_[k]}; tv_.CarrySet(person, &c);
            cell_[k] = -1.0; SetEmptyMass(GetEmptyMass() - kCellMass);
            oapiWriteLogV("TVChassis %s: cell %d out (%.0f %%)", GetName(), k + 1, c.energyJ / kCellJ * 100.0);
        } else if (cell_[k] < 0 && carries && c.kind == TV_CELL) {        // the one she carries into the empty bay
            cell_[k] = c.energyJ; TvCarry none{}; tv_.CarrySet(person, &none); tvTakeBack(api_, person); SetEmptyMass(GetEmptyMass() + kCellMass);
            oapiWriteLogV("TVChassis %s: cell %d in (%.0f %%)", GetName(), k + 1, cell_[k] / kCellJ * 100.0);
        }
        batt_ = CellsSum() / kBattJ;
    }

    // ---------------- stepping off (as in the simulators: F from anywhere aboard, OcInteriorExt::ExitAnywhere) ----------------
    // Where she stands after stepping off: on the ground clear of the body's outer box (OuterWalls and the people contact
    // use it - a point inside it traps her and the platform strikes her), facing away. The candidates in order: `first`
    // (the EMPU's stairs), her side abreast of her, her side fore and aft, the other side, the stern, the bow; a coupled end
    // is skipped, a spot on another platform, a small vessel or a person is taken as blocked. None clear: the first one.
    static int cExitAnywhere(void*) { return 1; }
    bool ExitStatus(int person, const VECTOR3* first, VESSELSTATUS2& vs) {
        OBJHANDLE ref = GetSurfaceRef();
        if (!ref) return false;
        VECTOR3 me = _V(0, 0, 0);
        OcInfo in{};
        if (api_.Info && api_.Info(person, &in) && in.vessel && oapiIsVessel(in.vessel)) { VECTOR3 g; oapiGetGlobalPos(in.vessel, &g); Global2Local(g, me); }
        const double side = me.x > 0.0 ? 1.0 : -1.0, gap = 0.75, y = -0.9;
        const double xs = kBodyX + gap, zc = Clamp(me.z, bz0_ + 0.6, bz1_ - 0.6);
        std::vector<VECTOR3> c;
        if (first) c.push_back(*first);
        c.push_back(_V(side * xs, y, zc));
        c.push_back(_V(side * xs, y, Clamp(zc + 2.0, bz0_ + 0.6, bz1_ - 0.6)));
        c.push_back(_V(side * xs, y, Clamp(zc - 2.0, bz0_ + 0.6, bz1_ - 0.6)));
        c.push_back(_V(-side * xs, y, zc));
        if (!towing_) c.push_back(_V(0.0, y, bz0_ - gap - 0.4));
        if (!towBy_) c.push_back(_V(0.0, y, bz1_ + gap + 0.6));            // ahead of the folded front bar
        size_t pick = 0;
        for (size_t k = 0; k < c.size(); ++k) if (SpotClear(c[k], in.vessel)) { pick = k; break; }
        VECTOR3 gp; Local2Global(c[pick], gp);
        double lng, lat, rad, hdg;
        oapiGlobalToEqu(ref, gp, &lng, &lat, &rad);
        oapiGetHeading(GetHandle(), &hdg);
        std::memset(&vs, 0, sizeof vs);
        vs.version = 2; vs.rbody = ref; vs.status = 1; vs.arot.x = 10.0;     // landed: Orbiter stands the body on its own points
        vs.surf_lng = lng; vs.surf_lat = lat;
        const double away = std::fabs(c[pick].x) > 0.1 ? (c[pick].x > 0 ? PI05 : -PI05) : (c[pick].z < 0 ? PI : 0.0);
        vs.surf_hdg = std::fmod(hdg + away + 2.0 * PI2, PI2);
        if (pick) oapiWriteLogV("TVChassis %s: stepping off at spot %d (the nearer ones are blocked)", GetName(), (int)pick);
        return true;
    }
    bool SpotClear(const VECTOR3& local, OBJHANDLE self) const {
        VECTOR3 g; Local2Global(local, g);
        for (TVChassis* o : All()) {                                    // another platform's (or our own) outer box
            VECTOR3 l; o->Global2Local(g, l);
            if (std::fabs(l.x) < kBodyX + 0.45 && l.z > o->bz0_ - 0.45 && l.z < o->bz1_ + 0.45 && l.y > -2.5 && l.y < o->btop_ + 1.0) return false;
        }
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {               // small things and people standing there
            OBJHANDLE h = oapiGetVesselByIndex(i);
            if (h == GetHandle() || h == self) continue;
            bool chassis = false; for (TVChassis* o : All()) if (o->GetHandle() == h) chassis = true;
            if (chassis) continue;
            VESSEL* v = oapiGetVesselInterface(h);
            const double s = v->GetSize();
            if (s > 5.0) continue;                                       // a ship: its own walls keep her off it
            VECTOR3 p; oapiGetGlobalPos(h, &p);
            const double r = api_.PersonOfBody && api_.PersonOfBody(h) ? 0.7 : 0.6 + 0.5 * s;
            if (length(p - g) < r) return false;
        }
        return true;
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
        // the same solids from the buildings (TVehicles.dll tvOuterSolids, TVBuilding.h: the technical point's walls and gate)
        static tantraOuterSolids_t fns[2] = {};
        if (!fns[0]) if (HMODULE h = GetModuleHandleA("Tantra.dll")) fns[0] = (tantraOuterSolids_t)GetProcAddress(h, "tantraOuterSolids");
        if (!fns[1]) if (HMODULE h = GetModuleHandleA("TVehicles.dll")) fns[1] = (tantraOuterSolids_t)GetProcAddress(h, "tvOuterSolids");
        if (!fns[0] && !fns[1]) return;
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
            int n = 0;
            for (tantraOuterSolids_t fn : fns) if (fn && n <= 0) n = fn(h, sol, 32);
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
            RectHit(o, 0.0, cz, hx, hz, o->GetMass(), dt);
        }
        // the charging station: a fixed block (its cabinet and the cell rack) - an immovable wall for the platform
        for (DWORD i = 0; i < oapiGetVesselCount(); ++i) {
            OBJHANDLE h = oapiGetVesselByIndex(i);
            if (h == GetHandle()) continue;
            VECTOR3 op;
            oapiGetGlobalPos(h, &op);
            if (length(op - me) > 20.0) continue;
            VESSEL* v = oapiGetVesselInterface(h);
            const char* cn = v ? v->GetClassName() : nullptr;
            if (!cn || std::strcmp(cn, "TVehicles\\Station")) continue;
            RectHit(v, 0.0, 0.245, 0.82, 0.655, 1.0e9, dt);                 // x +-0.82, z -0.41..0.90 (the cabinet and the rack in front)
        }
        carHitT_ -= dt;
    }
    // One rectangle (the other's, in its frame: centre ocx/ocz, half sizes ohx/ohz) against mine: the least overlap along the four
    // axes is the normal, the platform pushes ITSELF out (the other, if a vehicle, does the same from its side). m2: its mass
    void RectHit(VESSEL* o, double ocx, double ocz, double ohx, double ohz, double m2, double dt) {
        const double hx = kBodyX, hz = 0.5 * (bz1_ - bz0_), cz = 0.5 * (bz1_ + bz0_);
        {
            // the other's corners and axes in my frame
            VECTOR3 oc[4], ax, az, oo, g;
            const double cx[4] = {-ohx, ohx, ohx, -ohx}, czs[4] = {-ohz, -ohz, ohz, ohz};
            for (int k = 0; k < 4; ++k) { o->Local2Global(_V(ocx + cx[k], 0, ocz + czs[k]), g); Global2Local(g, oc[k]); }
            o->Local2Global(_V(ocx, 0, ocz), g); Global2Local(g, oo);
            if (std::fabs(oo.y) > 2.5) return;
            o->GlobalRot(_V(1, 0, 0), g); { MATRIX3 Rm; GetRotationMatrix(Rm); ax = tmul(Rm, g); }
            o->GlobalRot(_V(0, 0, 1), g); { MATRIX3 Rm; GetRotationMatrix(Rm); az = tmul(Rm, g); }
            ax.y = 0; ax = ax / length(ax); az.y = 0; az = az / length(az);
            const VECTOR3 mc[4] = {_V(-hx, 0, cz - hz), _V(hx, 0, cz - hz), _V(hx, 0, cz + hz), _V(-hx, 0, cz + hz)};
            const VECTOR3 axes[4] = {_V(1, 0, 0), _V(0, 0, 1), ax, az};
            double best = 1e9; VECTOR3 nrm = _V(0, 0, 0);
            for (const VECTOR3& a : axes) {
                double a0 = 1e9, a1 = -1e9, b0 = 1e9, b1 = -1e9;
                for (int k = 0; k < 4; ++k) {
                    const double pa = mc[k].x * a.x + mc[k].z * a.z, pb = oc[k].x * a.x + oc[k].z * a.z;
                    a0 = (std::min)(a0, pa); a1 = (std::max)(a1, pa); b0 = (std::min)(b0, pb); b1 = (std::max)(b1, pb);
                }
                const double ov = (std::min)(a1, b1) - (std::max)(a0, b0);
                if (ov <= 0.0) return;                                         // a separating axis: no contact
                if (ov < best) {
                    best = ov;
                    const double toOther = (oo.x - 0.0) * a.x + (oo.z - cz) * a.z;
                    nrm = toOther > 0 ? _V(-a.x, 0, -a.z) : _V(a.x, 0, a.z);       // from the other towards me
                }
            }
            // where they touch: the corners of each inside the other, else the middle between the centres
            VECTOR3 cp = _V(0, 0, 0); int nc = 0;
            for (int k = 0; k < 4; ++k)
                if (std::fabs(oc[k].x) <= hx && std::fabs(oc[k].z - cz) <= hz) { cp = cp + oc[k]; ++nc; }
            if (!nc)                                                               // none of its corners in me: mine in it
                for (int k = 0; k < 4; ++k) {
                    const VECTOR3 r = mc[k] - oo;
                    if (std::fabs(r.x * ax.x + r.z * ax.z) <= ohx && std::fabs(r.x * az.x + r.z * az.z) <= ohz) { cp = cp + mc[k]; ++nc; }
                }
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
            if ((GetFlightStatus() & 1) && closing < 0.5) return;
            const double m1 = GetMass(), mr = m1 * m2 / (m1 + m2);
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
    // The dust is the body's own: its colour (the texture) and how it flies (the stream). The Moon and Io (no air): grey / sulphur,
    // ballistic sheets - fast, thin, low and long (a low gravity), no billowing. Mars (0.6 kPa): rust-red, it leaves in a wide fan
    // and hangs. Earth: brown-tan, a drag-held billow. Titan (146 kPa): orange, heavy and slow. Others: the old tan
    struct DustKind { const char* body; const char* tex; double size, rate, v0, spread, life, grow, slow; };
    static const DustKind* DustOf(const char* body) {
        static const DustKind k[] = {
            {"Moon",  "MPU_dust_grey",   0.16, 42.0, 7.0, 0.14, 2.6, 0.10, 0.0},
            {"Io",    "MPU_dust_yellow", 0.16, 42.0, 6.0, 0.16, 2.0, 0.10, 0.0},
            {"Mars",  "MPU_dust_red",    0.35, 30.0, 4.5, 0.60, 3.6, 1.40, 0.6},
            {"Earth", "MPU_dust_tan",    0.40, 24.0, 3.5, 0.45, 2.2, 2.20, 2.5},
            // Titan (the «Анимации» session's spec, 2026-10-18, after Huygens' landing): damp sticky organic sand - a low slow
            // cloud from the contact patch, hanging 8-15 s, not thrown far (0.14 g, 5.4 kg/m3 of gas); its own soft ochre texture
            {"Titan", "Tantra\\titan_dust", 0.12, 30.0, 0.6, 0.70, 10.0, 0.10, 3.0},
        };
        for (const DustKind& d : k) if (!std::strcmp(d.body, body)) return &d;
        return nullptr;
    }
    std::string dustBody_;
    // the ground under a wheel for its thrown soil: the client's class, its moisture, and the loose layer the tread lifts [m]
    // (a site raster's byte as TVGround / the suit's jet: 0 damp sand, 1 ice cobbles, 2 ice bedrock, 3 wet margin, 4 liquid;
    // off a raster the body's own ground)
    void SoilClass(const char* body, int kind, int& gnd, double& wet, double& delta) const {
        switch (kind) {
        case 0: gnd = AtmoFXPart::DRY; wet = 0.25; delta = 3.0e-4; return;         // Titan's damp organic sand
        case 1: gnd = AtmoFXPart::ICEGROUND; wet = 0.1; delta = 5.0e-5; return;    // water-ice cobbles: chips
        case 2: gnd = AtmoFXPart::ICEGROUND; wet = 0.0; delta = 2.0e-5; return;    // ice bedrock: frost scraped off
        case 3: gnd = AtmoFXPart::WET; wet = 0.8; delta = 4.0e-4; return;          // the wet margin: clods
        case 4: gnd = AtmoFXPart::LIQUID; wet = 1.0; delta = 0.0; return;          // in the liquid: nothing thrown here
        default: break;
        }
        // loose fine regolith: ~0.1 mm a pass - the Apollo 16 film's dust loops are translucent (optical depth ~0.4 across a
        // ~1 m loop with ~15 m2/kg of the soil's fines: ~0.02 kg a burst, ~0.08 kg/s a wheel at 2.5 m/s, 0.23 m wide)
        if (!std::strcmp(body, "Moon") || !std::strcmp(body, "Io")) { gnd = AtmoFXPart::REGOLITH; wet = 0.0; delta = 1.0e-4; }
        else if (!std::strcmp(body, "Mars")) { gnd = AtmoFXPart::DRY; wet = 0.0; delta = 3.0e-4; }
        else if (!std::strcmp(body, "Earth")) { gnd = AtmoFXPart::DRY; wet = 0.15; delta = 1.0e-4; }      // firm loam: a thin loose skin
        else if (!std::strcmp(body, "Titan")) { gnd = AtmoFXPart::DRY; wet = 0.25; delta = 3.0e-4; }
        else if (GetAtmDensity() < 1e-4) { gnd = AtmoFXPart::REGOLITH; wet = 0.0; delta = 3.0e-4; }
        else { gnd = AtmoFXPart::DRY; wet = 0.1; delta = 2.0e-4; }
    }
    // the wheels' soil through the AtmoFX client (free particles, the body's gravity and gas). The tread lifts the loose
    // layer it rolls over (rho * w * delta * |rim|); slipping, its lugs shear a layer off the rut's floor each pass and fling
    // it (rho * w * 3 mm * |rim - v|: the wheel digs in at ~3 cm/s spinning 5 m/s over its ~0.5 m patch, as a stuck wheel
    // does). The soil comes off the lugs round the rear of the rim, where the tread leaves the ground: dry soil soon (15-45 deg
    // up from the bottom), damp soil later (up to 60 deg; above that the arch stops it) - a fresh angle each frame, so the
    // spray fans out over the arc. It leaves at the tread's velocity relative to the hub
    // there, |rim| (0, sin, -cos); the client adds the vehicle's motion. Rolling, the soil rises and falls back about where it
    // came from; spinning, the rim outruns the vehicle and the spray goes back - a rooster tail, the harder the faster it spins.
    // The client splits it by the ground's moisture: a damp ground's wet share comes off as clods, the wet margin all clods.
    // Sand stuck on the tyre (clog_) is flung off once the rim is fast - round the rear too: the arch above stops the rest
    unsigned rng_ = 0x9e3779b9u;
    double U01() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ & 0xFFFFFF) / 16777216.0; }
    void DustClient(const char* body) {
        const double R = mpu::kWheelR;
        for (int i = 0; i < mpu::kWheels; ++i) {
            const auto& W = mpu::kWheel[i];
            const double rim = rim_[i], vr = std::fabs(rim), s = rim >= 0.0 ? 1.0 : -1.0;   // s: the tread's travel way along the wheel
            int gnd = AtmoFXPart::DRY; double wet = 0.0, delta = 3.0e-4;
            SoilClass(body, gndKind_[i], gnd, wet, delta);
            double kg = 0.0;
            if (n_[i] > 0.0 && vr > 0.3 && gnd != AtmoFXPart::LIQUID) {
                const double rho = soilW_[i] ? soilW_[i]->rho : 1500.0;
                // (a slipping tyre digs: 3 mm of the ground a metre of slip - on the regolith the same in the share of its pass)
                const double dig = gnd == AtmoFXPart::REGOLITH ? 0.003 * 1.0e-4 / 4.0e-4 : 0.003;
                kg = rho * mpu::kWheelW * (delta * vr + dig * std::fabs(rim - vf_[i]));
            }
            const double late = gnd == AtmoFXPart::WET ? 1.0 : Clamp(wet * 2.0, 0.0, 1.0);   // damp soil clings longer
            // regolith (the client flies it in a vacuum as one ballistic sheet): the whole arc it leaves over at once - its
            // middle and its half-width as the cone; elsewhere a fresh angle each frame over the arc
            const bool arc = gnd == AtmoFXPart::REGOLITH;
            // (regolith: 20-60 deg up - the Apollo 16 film's dust tip left at 45-55 deg, the rest lower and higher)
            const double arcW = arc ? 40.0 * RAD : (30.0 + 15.0 * late) * RAD;
            const double phi = arc ? 20.0 * RAD + 0.5 * arcW : (15.0 + (30.0 + 15.0 * late) * U01()) * RAD;
            const VECTOR3 at = _V(W.hub[0], W.hub[1] - R * std::cos(phi) - ride_, W.hub[2] - s * R * std::sin(phi));
            AtmoFXPart::Emit(GetHandle(), 8000 + i, at, _V(0.0, std::sin(phi), -s * std::cos(phi)) * vr, arc ? 0.5 * arcW : 0.12, 0.35, kg,
                             AtmoFXPart::SOIL, gnd, wet, 0.0, 0.0);
            // (the airless regolith's film, GroundFeel: what the rim sheds of it - the film's ~5 kg on a full tyre (1 mm over
            // its 3.3 m2) at the shedding rate; it leaves as dry aggregates of the ground's colour, not wet clods)
            const bool film = regolith_ && gndKind_[i] < 0;
            const double clods = n_[i] > 0.0 && vr > 1.0 && clog_[i] > 0.05
                                 ? (film ? 5.0 * std::pow(vr / 10.0, 2.0) / 20.0 * clog_[i] : 0.4 * clog_[i] * mpu::kWheelW * vr) : 0.0;
            const double phi2 = (25.0 + 35.0 * U01()) * RAD;
            const VECTOR3 at2 = _V(W.hub[0], W.hub[1] - R * std::cos(phi2) - ride_, W.hub[2] - s * R * std::sin(phi2));
            AtmoFXPart::Emit(GetHandle(), 8100 + i, at2, _V(0.0, std::sin(phi2), -s * std::cos(phi2)) * vr, 0.10, 0.3, clods,
                             AtmoFXPart::SOIL, film ? AtmoFXPart::REGOLITH : AtmoFXPart::WET, film ? 1.0 : 0.9, 0.0, 0.0);
        }
    }
    void Dust() {
        const bool air = GetAtmPressure() > 50.0;
        char nm[64] = ""; if (OBJHANDLE ref = GetSurfaceRef()) oapiGetObjectName(ref, nm, sizeof nm);
        regolith_ = !std::strcmp(nm, "Moon") || !std::strcmp(nm, "Io") || (nm[0] && !air && GetAtmDensity() < 1e-4 && std::strcmp(nm, "Titan"));
        if (AtmoFXPart::Available()) {                                         // the client flies it: Orbiter's streams off
            if (dustMade_) { for (PSTREAM_HANDLE& h : dust_) if (h) { DelExhaustStream(h); h = nullptr; } dustMade_ = false; }
            DustClient(nm);
            return;
        }
        if (!dustMade_ || air != dustAir_ || dustBody_ != nm) {
            for (PSTREAM_HANDLE& h : dust_) if (h) { DelExhaustStream(h); h = nullptr; }
            const DustKind* dk = DustOf(nm);
            SURFHANDLE tex = oapiRegisterParticleTexture(const_cast<char*>(dk ? dk->tex : "MPU_dust"));
            PARTICLESTREAMSPEC vac = {0, 0.25, 30.0, 5.0, 0.3, 0.9, 0.6, 0.0, PARTICLESTREAMSPEC::DIFFUSE,
                                      PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, tex};
            PARTICLESTREAMSPEC atm = {0, 0.4, 12.0, 2.0, 0.5, 5.0, 1.8, 1.5, PARTICLESTREAMSPEC::DIFFUSE,
                                      PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, tex};
            if (dk) {
                PARTICLESTREAMSPEC mine = {0, dk->size, dk->rate, dk->v0, dk->spread, dk->life, dk->grow, dk->slow, PARTICLESTREAMSPEC::DIFFUSE,
                                           PARTICLESTREAMSPEC::LVL_LIN, 0, 1, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, tex};
                vac = atm = mine;
            }
            for (int i = 0; i < mpu::kWheels; ++i) {
                const auto& W = mpu::kWheel[i];
                const bool titan = !std::strcmp(nm, "Titan");                    // Titan: at the patch, just behind it
                const VECTOR3 at = titan ? _V(W.hub[0], W.hub[1] - mpu::kWheelR + 0.03, W.hub[2] - 0.1)
                                         : _V(W.hub[0], W.hub[1] - mpu::kWheelR + 0.1, W.hub[2] - 0.5 * (W.hub[2] > 0 ? 1 : 1));
                dust_[i] = AddParticleStream(air ? &atm : &vac, at, _V(0, 0.55, -0.83), &dustLv_[i]);
            }
            dustAir_ = air; dustMade_ = true; dustBody_ = nm;
        }
        const bool titan = dustBody_ == "Titan";                                // its cloud rises from a walking pace, at most 0.55
        for (int i = 0; i < mpu::kWheels; ++i)
            dustLv_[i] = n_[i] <= 0.0 ? 0.0 : titan ? 0.55 * Clamp(std::fabs(vf_[i]) / 3.0, 0.0, 1.0) : Clamp((std::fabs(vf_[i]) - 1.0) / 12.0, 0.0, 1.0);
    }

    // sound: only through air - none on the Moon; on Mars thin and near, on Earth full (Orbiter's air at the platform);
    // fades with the camera's distance. Hub motors (two layers crossfaded by speed), tyres rolling, knocks of the
    // suspension, the lift drive while the platform goes up or down.
    enum { kSndDrive = 1, kSndInverter, kSndRoll, kSndKnock, kSndLift, kSndHum,
           kSndBrake = 20, kSndLadderDn, kSndLadderUp, kSndAirlock, kSndClick, kSndTick, kSndCrash, kSndCouple, kSndUncouple, kSndAlarm, kSndBeep, kSndWind,
           kSndDriveLoad = 50, kSndRollSand, kSndRollIce, kSndRattle, kSndSkid, kSndBumpStop, kSndSusp0 };   // (40: MPU.cpp's ventilation)
    double sfxAtt_ = 1.0;                                            // how loud the machine's own noises are heard now (Sound())
    // a one-shot of the machine (steel, hatches, switches, a crash): through the air by the distance, aboard at once
    void Sfx(int id, double vol = 1.0, bool restart = true) {
        if (!snd_ || !snd_->IsPresent()) return;
        const double a = Clamp(sfxAtt_ * vol, 0.0, 1.0);
        if (a < 0.02) return;
        if (!restart && snd_->IsWavPlaying(id)) return;
        if (snd_->IsWavPlaying(id)) snd_->StopWav(id);
        snd_->PlayWav(id, false, (float)a);
    }
    double beepT_ = 0.0;
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
            // in the cabin: the motors through the structure and the cabin's air - fully heard (the user, 2026-10-21: «пусть
            // будет нормально слышно»); on the open deck only through an air outside, in vacuum nothing
            if (pid && api_.ShipOf(pid) == GetHandle()) att = (std::max)(att, cab_ ? 1.0 : (air > 0.01 ? 0.4 : 0.0));
        }
        sfxAtt_ = (std::max)(att, 0.0);
        const bool aboard = att >= (cab_ ? 0.79 : 0.39);                  // a person of ours aboard (the structure carries it)
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
        // what torque adds to the drive (the pole-passing growl, the iron's buzz, the gears' flanks rattling): with the force
        double fTot = 0.0; for (double f : fI_) fTot += f;
        const double load = Clamp(fTot / kFmotor, 0.0, 1.0);
        layer(kSndDriveLoad, att * turning * Clamp(std::sqrt(load) * 1.3, 0.0, 1.0) * 0.7);
        if (snd_->IsWavPlaying(kSndDriveLoad)) snd_->SetPlaybackSpeed(kSndDriveLoad, (float)Clamp(v / 11.1, 0.15, 2.2));
        // the wheels on the ground (recorded at 8 m/s, played at the speed's rate): what it is decides how it sounds - stones
        // crunch, sand hisses, ice cracks; in a liquid less of it
        int rk = 0;                                                         // 0 gravel / regolith / crust, 1 sand, 2 ice
        {
            int cnt[5] = {}; for (int i = 0; i < mpu::kWheels; ++i) if (gndKind_[i] >= 0 && gndKind_[i] < 5) ++cnt[gndKind_[i]];
            int best = -1, bn = 0; for (int k = 0; k < 5; ++k) if (cnt[k] > bn) { bn = cnt[k]; best = k; }
            if (best == tvground::SAND || best == tvground::WET || best == tvground::LIQUID) rk = 1;
            else if (best == tvground::ICE) rk = 2;
            else if (best < 0) { char nm[64] = ""; if (OBJHANDLE r = GetSurfaceRef()) oapiGetObjectName(r, nm, sizeof nm); if (!std::strcmp(nm, "Earth")) rk = 1; }
        }
        const double inLiq = Clamp(wadeDepth_ / mpu::kWheelR, 0.0, 1.0);
        const double onGround = [&] { int c = 0; for (double n : n_) c += n > 0.0; return c / double(mpu::kWheels); }();
        static const int rollIds[3] = {kSndRoll, kSndRollSand, kSndRollIce};
        for (int k = 0; k < 3; ++k) {
            layer(rollIds[k], k == rk ? att * onGround * Clamp(v / 10.0, 0.0, 1.0) * 0.85 * (1.0 - 0.6 * inLiq) : 0.0);
            if (snd_->IsWavPlaying(rollIds[k])) snd_->SetPlaybackSpeed(rollIds[k], (float)Clamp(v / 8.0, 0.3, 2.0));
        }
        // the brakes. The service brake (S, ABS) is the motors': their whine (above) and the tyres; Space and the parking brake
        // are the friction brakes - the pads groan, squeal near a stop - and lock the wheels: they scrub the ground
        const bool friction = (brakeHeld_ || park_) && v > 0.3 && (GetFlightStatus() & 1) == 0;
        layer(kSndBrake, friction ? att * (0.45 + 0.55 * Clamp(1.0 - v / 8.0, 0.0, 1.0)) * 0.8 : 0.0);
        if (snd_->IsWavPlaying(kSndBrake)) snd_->SetPlaybackSpeed(kSndBrake, (float)(0.8 + 0.3 * Clamp(v / 10.0, 0.0, 1.0)));
        layer(kSndSkid, friction ? att * onGround * Clamp(v / 6.0, 0.0, 1.0) * 0.9 : 0.0);
        if (snd_->IsWavPlaying(kSndSkid)) snd_->SetPlaybackSpeed(kSndSkid, (float)Clamp(v / 8.0, 0.4, 1.8));
        // the wind round the body (an atmosphere only), the cabin shuts most of it out
        const double rho = (std::max)(0.0, GetAtmDensity());
        layer(kSndWind, rho > 0.003 ? (aboard && cab_ ? 0.3 : 1.0) * Clamp(std::pow(v / 28.0, 1.4), 0.0, 1.0) * Clamp(std::sqrt(rho / 1.2), 0.0, 1.3) * 0.7 : 0.0);
        // the reverse beeper, outside loud, in the cabin dull
        if (speed_ < -0.3 && drive_ < -0.05) { if ((beepT_ -= dt) <= 0.0) { Sfx(kSndBeep, aboard ? 0.35 : 1.0); beepT_ = 0.9; } } else beepT_ = 0.0;
        layer(kSndLift, att * (rideMoving_ ? 0.6 : 0.0));
        // the suspension (the user, 2026-10-19: the old knock «не удар подвески совсем»): each wheel's stroke - its
        // compression's rate - over a rock or a rut's edge: from 0.35 m/s a stroke (four takes, louder the faster), bottoming
        // on its stop (compressed past its soft travel) a hard clank; the roughness of the last half second shakes the frame
        double rough = 0.0;
        const bool live = (GetFlightStatus() & 1) == 0 && oapiGetTimeAcceleration() <= 2.0 && dt > 0.0;
        for (int i = 0; i < mpu::kWheels; ++i) {
            const double c = live ? (hPrev_[i] - height_[i]) / dt : 0.0;   // the compression's rate, m/s (+ in)
            hPrev_[i] = height_[i];
            rough += std::fabs(c) / mpu::kWheels;
            suspCool_[i] -= dt;
            if (!live || n_[i] <= 0.0) { stopHit_[i] = false; continue; }
            const bool onStop = -height_[i] > kSag + kTravel;
            if (onStop && !stopHit_[i] && c > 0.2) { Sfx(kSndBumpStop, Clamp(std::fabs(c) / 1.5, 0.35, 1.0)); suspCool_[i] = 0.3; }
            stopHit_[i] = onStop;
            if (std::fabs(c) > 0.35 && suspCool_[i] <= 0.0) {
                Sfx(kSndSusp0 + (suspNext_++ & 3), Clamp((std::fabs(c) - 0.35) / 1.2, 0.12, 1.0));
                suspCool_[i] = 0.18;
            }
        }
        rough_ += (rough - rough_) * Clamp(dt / 0.5, 0.0, 1.0);
        layer(kSndRattle, att * Clamp(v / 6.0, 0.0, 1.0) * Clamp(rough_ / 0.6, 0.0, 1.0) * 0.6);
    }
    double suspCool_[mpu::kWheels] = {}, rough_ = 0.0; bool stopHit_[mpu::kWheels] = {}; int suspNext_ = 0;

    // Stand it on its eight wheels: the ground under each wheel, a plane through them (least squares), the platform's up
    // along that plane's normal, its heading kept, the centre (kCgH - kSag) above the plane - every wheel at its static
    // sag. (Orbiter's own "stand on your points" tilts it by the slope under the centre only: on uneven ground one wheel
    // went 0.9 m into it and the platform sat 0.4 m low.) Orbiter's landed state at that pose.
    // ---------------- the jitter probe (the user, 2026-10-18: «дергаются на грунте, на Титане особенно»): what moves a machine
    // that should stand or roll smoothly - into Orbiter.log, «MPU probe <name>»: our rests and wakes, Orbiter leaving the
    // landed state by itself, the touchdown points set again, the terrain's elevation changing under it (tiles of a finer level
    // arriving), the pose a re-seat moves it by, and a parked machine drifting. At most ~4 lines a second a vehicle
    bool restPending_ = false;
    double probeT_ = 0.0, probeElev_ = -1e9, probeLng_ = 0.0, probeLat_ = 0.0, probeRad_ = 0.0, probeAcc_ = 0.0; int probeN_ = 0, probeStatus_ = -1;
    template <typename... A> void Probe(const char* fmt, A... a) {
        if (probeN_ >= 4) return;
        ++probeN_;
        char b[300]; int n = std::snprintf(b, sizeof b, "MPU probe %s t %.2f: ", GetName(), oapiGetSimTime());
        std::snprintf(b + n, sizeof b - n, fmt, a...);
        oapiWriteLog(b);
    }
    void ProbeStep(double dt) {
        OBJHANDLE ref = GetSurfaceRef();
        if (!ref || dt <= 0.0) return;
        probeAcc_ += dt;
        if (probeAcc_ >= 1.0) { probeAcc_ = 0.0; probeN_ = 0; }
        double lng, lat, rad; GetEquPos(lng, lat, rad);
        const double e = oapiSurfaceElevation(ref, lng, lat), R = oapiGetSize(ref);
        if (probeElev_ > -1e8 && (GetFlightStatus() & 1) && std::fabs(e - probeElev_) > 0.005) Probe("the terrain under it changed %+.3f m (elevation %.3f) - a tile arrived?", e - probeElev_, e);
        probeElev_ = e;
        const int status = (GetFlightStatus() & 1) ? 1 : 0;
        if (status != probeStatus_) { Probe("Orbiter status %s, altitude %.3f m, driving mode %d", status ? "LANDED" : "free", GetAltitude(ALTMODE_GROUND), drivingMode_ ? 1 : 0); probeStatus_ = status; }
        // a landed machine moves only by our re-seats: anything else is Orbiter's doing (or the terrain's)
        if (status && probeRad_ > 0.0) {
            const double dh = std::hypot((lat - probeLat_) * R, (lng - probeLng_) * R * std::cos(lat)), dv = rad - probeRad_;
            if (dh > 0.01 || std::fabs(dv) > 0.01) Probe("parked, moved %.3f m across, %+.3f m up (alt %.3f, elevation %.3f)", dh, dv, GetAltitude(ALTMODE_GROUND), e);
        }
        probeLng_ = lng; probeLat_ = lat; probeRad_ = rad;
    }
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
            const double e = oapiSurfaceElevation(ref, lng, lat) - dig_[i];   // the ground under it: the bottom of its hole
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
        Probe("re-seat%s: altitude %.3f -> %.3f m; pitch %.2f, bank %.2f deg -> the plane's slopes %.2f along, %.2f across deg", level ? " (levelled)" : "",
              GetAltitude(ALTMODE_GROUND), vs.vrot.x, GetPitch() * DEG, GetBank() * DEG, std::atan(sF) * DEG, std::atan(sR) * DEG);
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
        if (!std::strcmp(nm, "Titan")) return kTitanSand;
        return kMoonSoil;
    }
    // the soil at a point: a site's ground raster where there is one (Config\OrbiterCrew\Ground\<body>.cfg, the suit's too),
    // else the body's own. kind: the raster's byte, -1 off it
    static tvground::Raster* GroundFor(OBJHANDLE ref) {
        static std::map<std::string, tvground::Raster> rs;
        char nm[64] = ""; oapiGetObjectName(ref, nm, sizeof nm);
        auto it = rs.find(nm);
        if (it == rs.end()) {
            tvground::Raster& r = rs[nm];
            const std::string path = std::string("Config\\OrbiterCrew\\Ground\\") + nm + ".cfg";
            if (r.Load(path.c_str(), nm)) oapiWriteLogV("TVChassis: ground raster for %s loaded (%s)", nm, path.c_str());
            it = rs.find(nm);
        }
        return it->second.Loaded() ? &it->second : nullptr;
    }
    const Soil& SoilAt(OBJHANDLE ref, double lat, double lng, int& kind) const {
        kind = -1;
        char nm[64] = ""; oapiGetObjectName(ref, nm, sizeof nm);
        if (std::strcmp(nm, "Titan")) return SoilOf(ref);                  // (the kinds are Titan's; other bodies: their own soil)
        if (const tvground::Raster* r = GroundFor(ref)) kind = r->At(lat, lng, oapiGetSize(ref));
        switch (kind) {
        case tvground::PEBBLES: return kTitanPebbles;
        case tvground::ICE: return kTitanIce;
        case tvground::WET: return kTitanWet;
        case tvground::LIQUID: return kTitanBed;
        default: kind = kind < 0 ? tvground::SAND : kind; return kTitanSand;
        }
    }
    // what the ground does to a rolling wheel besides its terramechanics (Titan): damp sand sticks in the mesh and builds up
    // with the distance (e-fold ~300 m on sand, ~80 m on the wet margin), rolling over pebbles and ice knocks it off (~40 m) and
    // so does speed (the rim's acceleration v^2/R past the sand's ~2 kPa of cohesion: from ~10 m/s); over the pebbles (5-15 cm,
    // ~12 cm apart) the wheel is struck at v / 0.12 Hz - a vertical force up to half its load, the body shakes (estimates)
    // The wheel's rim speed as a state of its own (the user, 2026-10-19: «пробуксовка не похожа на пробуксовку»). Before, it
    // was read off the slip: at a standstill any partial slip gave a wheel that did not turn (stuck, pushing - the wheels
    // stood), and a spinning wheel turned at the ground's speed + 6 m/s whatever the motor did. Now:
    //  - the inertia at the rim: the tyre (150 kg, mostly at the rim: ~80 kg m2) and the hub motor's rotor seen through its
    //    12:1 gear (~0.4 kg m2 x 144 ~ 60 kg m2): ~140 kg m2 over R^2 - ~215 kg at the rim;
    //  - spinning (the motor asks more than the soil holds): it spins up by (the motor's force - the soil's at full slip) / that
    //    mass, the force no more than its power over the rim speed, up to the motors' top speed (the limiter's 22 m/s, 36 off);
    //  - gripping: it follows its slip, the slip taken against at least 0.3 m/s (a stuck wheel pushing turns slowly, digging);
    //    a wheel coming back to grip slows at the soil's force over that mass; locked by the brake: stopped at once.
    // Slip-sinkage: a slipping wheel digs (the soil it shears is carried out behind it): ~4 mm per metre of slip on Titan's
    // sand, less the stiffer the soil (kphi), up to 0.4 R; rolling on, it climbs out over ~3 m. The dug depth adds to the
    // rut's compaction resistance - a spinning wheel digs itself in, as real ones do (estimates)
    static constexpr double kRimMass = 215.0;
    double dig_[mpu::kWheels] = {}, wRim_[mpu::kWheels] = {};
    double WheelSpin(int i, const Soil& S, const WheelSoil& w, double sl, double vf, double wantF, double fCmd, bool brake, double dt) {
        double& r = wRim_[i];
        const double dir = fCmd >= 0.0 ? 1.0 : -1.0;
        if (brake || WheelHealth(i) < 0.15) r = 0.0;                              // locked
        else if (sl >= 0.999 && wantF > 0.0) {                                    // spinning: the motor's surplus spins it up
            const double pWheel = (cruise_ ? kPcruise : limOff_ ? kPover : kPmax) / mpu::kWheels * motDer_[i];
            const double fMot = (std::min)(wantF, pWheel / (std::max)(std::fabs(r), 1.0));
            const double acc = (fMot - ShearAt(S, w, 1.0)) / kRimMass;
            r += dir * acc * dt;
            if (r * dir < vf * dir) r = vf;                                       // never slower than the ground it spins on
            r = Clamp(r, -vTop(), vTop());
        } else {
            double t = vf;                                                        // rolling
            if (sl > 0.0 && sl < 0.999) t = vf + dir * sl / (1.0 - sl) * (std::max)(std::fabs(vf), 0.3);   // driving: ahead of the ground by its slip
            else if (sl < 0.0 && sl > -0.999) t = vf * (1.0 + sl);                // ABS / the motors braking: a little slower
            const double aMax = (w.Hmax + wantF) / kRimMass;                      // how fast the soil (and the motor) can bring it there
            r += Clamp(t - r, -aMax * dt, aMax * dt);
            r += (t - r) * Clamp(dt / 0.05, 0.0, 1.0);                            // and the last of it at once (the tread's own give)
        }
        // slip-sinkage: dug by the slipping, climbed out of by rolling
        // how deep the slip digs it: the slip-sinkage of the rigid wheel (Lyasko), z = z0 (1 + s) / (1 - s / 2) - at full
        // slip three times its own sinkage more, no deeper (before: without a bound - 26 cm in seconds on the wet margin);
        // it gets there at ~1 cm per metre of slip (the chevrons carry the soil out behind), softer soils faster
        const double vs = std::fabs(r - vf);
        // the slip the soil is sheared at: the traction's own (sl) - at a standstill the kinematic ratio is always 1 and made
        // every start dig the full hole (the user, 2026-10-19: stuck on the wet margin even without Shift); a true spin: 1
        const double s = sl >= 0.999 ? 1.0 : Clamp(std::fabs(sl), 0.0, 0.999);
        const double digT = w.z * ((1.0 + s) / (1.0 - 0.5 * s) - 1.0);
        const double kDig = 0.01 * Clamp(4.0e5 / (std::max)(S.kphi, 1.0), 0.05, 2.0);
        if (dig_[i] < digT) dig_[i] = (std::min)(digT, dig_[i] + kDig * vs * dt);
        // spinning in place (the machine not moving) it goes on throwing the soil out past that: a third as fast, until the
        // belly lies on the ground (the user, 2026-10-19: «закопаться она может глубже») - out of that, only a tow
        else if (sl >= 0.999 && std::fabs(vf) < 0.1 && vs > 0.5) dig_[i] += 0.3 * kDig * vs * dt;   // only a true spin
        // rolling on, it leaves the hole behind (over ~3 m) - always, not only without slip: on the cruise drive's 8 % the
        // hole had grown on the move to its 0.32 m cap within a minute on Titan's sand and the machine bogged in its own rut
        // (the user, 2026-10-19: «иногда застревают»); now with a steady slip s it holds kDig s/(1-s) 3 m - ~1 mm at 8 %
        dig_[i] -= dig_[i] * std::fabs(vf) * dt / 3.0;
        dig_[i] = Clamp(dig_[i], 0.0, 0.5);                                       // ~ the belly's clearance
        return r;
    }
    // ---------------- in a liquid (the user, 2026-10-19: Titan's seas, «как есть в реалиях») ----------------
    // The site's ground raster gives the seas' surface (LiquidLevel, Config\OrbiterCrew\Ground\<body>.cfg); the depth of a
    // point = that level - its height over the mean radius. The liquid: Kraken Mare's methane-ethane-nitrogen mix, ~520 kg/m3
    // at 93 K (Cassini: Moray Sinus ~70 % CH4, 16 % N2, 14 % C2H6; pure CH4 450, C2H6 650), a dielectric - no shorts.
    // The machine as cells (vessel frame): the spine's sealed box (1.6 x 0.9 x 8.6 m, ~12 m3), the cabin (Э.МПУ: 3.0 x 2.2 x
    // 8.2 m, ~55 m3, sealed), the eight hub motors' housings (~0.1 m3 each); the airless mesh wheels give no lift, only drag.
    // Each cell under the surface: buoyancy rho g V (its submerged share, at its centre - the moments right it or tip it),
    // and the liquid's drag on its point's own motion (box Cd 1.0 on its projected area, plus a linear part ~ rho A 0.3 m/s
    // for the waves it makes: the heave and the roll die down). Э.МПУ (15.6 t): floats once ~30 m3 is under - the spine and
    // the cabin's lower part, ~2.2 m of liquid over the ground; МПУ (9 t, the spine only, 6.4 t of lift): never - it wades
    // and stays on the bed, lighter on its wheels. Wading: the wheels' loads (and grip) fall by the lift by themselves.
    // Off the bed the wheels spin free (the motor against the liquid's drag on the chevrons) and paddle: a wheel half under
    // pushes by its lower half's chevrons, a wheel all under gives nothing (the top half pulls back as much) - a floating
    // Э.МПУ with its wheels hanging under it hardly moves: it drifts. A holed cabin under the surface floods: the liquid in
    // through the holes (0.6 A sqrt(2 dp / rho), dp = the outside's pressure at the floor - the cabin's 99 kPa); its weight
    // sinks it. The cold: the liquid takes the heat of what it wets - film boiling, ~300 W/m2K (TVHeat: the tyres, the
    // belly; the motors in MotorThermal); the cabin's hull loses its outer gas film (MPU.cpp HullUA). Above 2x time
    // acceleration no forces (the steps are too long): it sinks to the bed and lands; back at 1x it lifts off again.
    static constexpr double kLiqRho = 520.0, kLiqT = 93.0, kLiqBoilT = 97.0, kCabP = 99.0e3;
    struct Cell { VECTOR3 c, half; double vol; int kind; int idx; };   // kind 0 spine, 1 cabin, 2 motor, 3 wheel (drag only)
    std::vector<Cell> icells_;
    std::vector<VECTOR3> icellPrev_;
    bool icellPrevOk_ = false;
    double buoyFrac_ = 0.0, cabWet_ = 0.0, floodV_ = 0.0, liqLevel_ = 0.0, wadeDepth_ = 0.0;
    bool liquidHere_ = false, floating_ = false;
    double motWet_[mpu::kWheels] = {}, wheelLiq_[mpu::kWheels] = {}, hubDepth_[mpu::kWheels] = {}, bellyWet_ = 0.0;
    void BuildCells() {
        icells_.clear();
        auto box = [&](double x0, double x1, double y0, double y1, double z0, double z1, int nx, int ny, int nz, int kind) {
            const double dx = (x1 - x0) / nx, dy = (y1 - y0) / ny, dz = (z1 - z0) / nz;
            for (int a = 0; a < nx; ++a) for (int b = 0; b < ny; ++b) for (int c = 0; c < nz; ++c)
                icells_.push_back({_V(x0 + (a + 0.5) * dx, y0 + (b + 0.5) * dy, z0 + (c + 0.5) * dz), _V(dx / 2, dy / 2, dz / 2), dx * dy * dz, kind, -1});
        };
        box(-0.8, 0.8, -0.37, 0.53, -4.3, 4.3, 2, 2, 4, 0);                           // the spine (sealed)
        if (cab_) box(-1.5, 1.5, kDeckY, 2.85, -4.3, 3.9, 2, 3, 4, 1);                // the cabin (sealed)
        for (int i = 0; i < mpu::kWheels; ++i) {
            const auto& W = mpu::kWheel[i];
            icells_.push_back({_V(W.hub[0], W.hub[1], W.hub[2]), _V(0.23, 0.23, 0.23), 0.1, 2, i});                              // the hub motor
            icells_.push_back({_V(W.hub[0], W.hub[1], W.hub[2]), _V(mpu::kWheelW / 2, mpu::kWheelR, mpu::kWheelR), 0.0, 3, i});   // the wheel
        }
        icellPrev_.assign(icells_.size(), _V(0, 0, 0)); icellPrevOk_ = false;
    }
    void ImmersionStep(double dt, bool free) {
        OBJHANDLE ref = GetSurfaceRef();
        liquidHere_ = false; buoyFrac_ = 0.0; cabWet_ = 0.0; wadeDepth_ = 0.0; floating_ = false; bellyWet_ = 0.0;
        for (int i = 0; i < mpu::kWheels; ++i) { motWet_[i] = 0.0; wheelLiq_[i] = 0.0; }
        const tvground::Raster* gr = ref ? GroundFor(ref) : nullptr;
        if (!gr || !gr->HasLiquid() || dt <= 0.0) { icellPrevOk_ = false; ClearFoam(); ClearWake(); ClearSpray(); return; }
        liqLevel_ = gr->LiquidLevel();
        double lng, lat, rad; GetEquPos(lng, lat, rad);
        const double R = oapiGetSize(ref);
        if (rad - R - liqLevel_ > 12.0) { icellPrevOk_ = false; ClearFoam(); ClearWake(); ClearSpray(); return; }  // well above the sea
        if (icells_.empty()) BuildCells();
        const double g = LocalG(), W = GetMass() * g;
        MATRIX3 Rv, Rp; GetRotationMatrix(Rv); oapiGetRotationMatrix(ref, &Rp);
        VECTOR3 pc; oapiGetGlobalPos(ref, &pc);
        VECTOR3 me; GetGlobalPos(me);
        const VECTOR3 upL = tmul(Rv, unit(me - pc));                                  // the local vertical, vessel frame
        const bool apply = free && oapiGetTimeAcceleration() <= 2.0;
        double lift = 0.0, cabVol = 0.0, cabSub = 0.0, floorDepth = -1e9;
        for (size_t k = 0; k < icells_.size(); ++k) {
            const Cell& c = icells_[k];
            VECTOR3 gp; Local2Global(c.c, gp);
            double clng, clat, crad; oapiGlobalToEqu(ref, gp, &clng, &clat, &crad);
            const double depth = liqLevel_ - (crad - R);                              // of the cell's centre
            // its vertical extent: the half-sizes projected on the vertical
            const double hv = std::fabs(upL.x) * c.half.x + std::fabs(upL.y) * c.half.y + std::fabs(upL.z) * c.half.z;
            const double frac = Clamp((depth + hv) / (2.0 * hv), 0.0, 1.0);
            VECTOR3 lp; oapiGlobalToLocal(ref, &gp, &lp);                             // planet-fixed: the point's motion over the ground
            VECTOR3 vL = _V(0, 0, 0);
            if (icellPrevOk_) vL = tmul(Rv, mul(Rp, (lp - icellPrev_[k]) / dt));
            icellPrev_[k] = lp;
            if (c.kind == 1) { cabVol += c.vol; cabSub += c.vol * frac; if (c.c.y - c.half.y < kDeckY + 0.01) floorDepth = (std::max)(floorDepth, depth + hv); }
            if (c.kind == 2) { motWet_[c.idx] = frac; hubDepth_[c.idx] = depth; }
            if (c.kind == 0 && c.c.y < 0.08) bellyWet_ += frac / 8.0;                 // the spine's lower layer: the belly
            if (c.kind == 3) wheelLiq_[c.idx] = Clamp(depth + mpu::kWheelR, 0.0, 2.0 * mpu::kWheelR);
            if (frac <= 0.0) continue;
            liquidHere_ = true;
            const double b = kLiqRho * g * c.vol * frac;
            lift += b;
            if (!apply) continue;
            VECTOR3 F = upL * b;
            // the drag: on the projected area across its motion (the wheel's mesh: half of its disc), quadratic and wave-making
            const double sp = length(vL);
            if (sp > 1e-4) {
                const double ax = 4.0 * c.half.y * c.half.z, ay = 4.0 * c.half.x * c.half.z, az = 4.0 * c.half.x * c.half.y;
                double A = (ax * std::fabs(vL.x) + ay * std::fabs(vL.y) + az * std::fabs(vL.z)) / sp * frac;
                double cd = 1.0;
                if (c.kind == 3) { A *= 0.5; cd = 0.8; }
                F -= vL * (0.5 * kLiqRho * cd * A * sp + kLiqRho * A * 0.3);
            }
            AddForce(F, c.c);
        }
        icellPrevOk_ = true;
        // the cabin floods through its holes where they are under the surface (the floor, the worst case)
        if (cab_ && hullLeak_ > 0.0 && floorDepth > 0.0) {
            const double pOut = GetAtmPressure() + kLiqRho * g * floorDepth, dp = pOut - kCabP;
            if (dp > 0.0) floodV_ = (std::min)(cabVol, floodV_ + 0.6 * hullLeak_ * std::sqrt(2.0 * dp / kLiqRho) * dt);
        }
        if (floodV_ > 0.0) {
            lift -= kLiqRho * g * floodV_;                                            // the liquid inside: its weight
            if (apply) AddForce(upL * (-kLiqRho * g * floodV_), _V(0.0, kDeckY + 0.3, -0.2));
        }
        cabWet_ = cabVol > 0.0 ? cabSub / cabVol : 0.0;
        buoyFrac_ = W > 0.0 ? lift / W : 0.0;
        double nSum = 0.0; for (double n : n_) nSum += n;
        floating_ = buoyFrac_ > 0.98 && nSum < 0.02 * W;
        { double e = 0.0; int k = 0; for (int i = 0; i < mpu::kWheels; ++i) if (wheelLiq_[i] > 0.0) { e += wheelLiq_[i]; ++k; } wadeDepth_ = k ? e / k : 0.0; }
        // standing landed but lifted by the liquid: out of the landed state (it floats up or settles lighter)
        if (!free && buoyFrac_ > 0.9 && oapiGetTimeAcceleration() <= 2.0) { Probe("lifted by the liquid (%.2f of its weight): out of the landed state", buoyFrac_); Wake(); }
        Foam();
        Wake(upL, liqLevel_ - (rad - R));
        Spray(upL, liqLevel_ - (rad - R));
    }
    // the spray (the AtmoFX client's free particles, kind DROP - flown in the gravity and the gas, broken by the Weber number,
    // rings where they fall back, wet spots on the ground). A wading wheel's chevrons lift the liquid and throw it off where
    // the rim comes up out of it: the rear crossing of the surface, along the rim's own velocity there (r (-cos phi) along
    // the travel, |r| sin phi up, phi from the bottom: cos phi = the hub's depth / R); ~10 % of what the wetted chevrons
    // carry is seen as drops (0.1 rho A |r|). The hull's bow wave, wading or afloat from 0.5 m/s: ~1 % of the flux through
    // its wetted front (rho B d v) thrown up and ahead at about half the speed. Ids 8400+i, 8500 (the vents use 8200, 8201).
    // Off until the client has DROP (an older client flies kind 3 as soil): kSprayOn
    static constexpr bool kSprayOn = true;
    bool sprayOn_ = false;
    void Spray(const VECTOR3& upL, double depth0) {
        if (!kSprayOn || !AtmoFXPart::Available()) return;
        sprayOn_ = true;
        const double R = mpu::kWheelR;
        for (int i = 0; i < mpu::kWheels; ++i) {
            const auto& W = mpu::kWheel[i];
            const double dh = hubDepth_[i], r = wRim_[i];
            double kg = 0.0; VECTOR3 at = _V(0, 0, 0), vel = _V(0, 0, 0);
            if (dh > -R && dh < R && std::fabs(r) > 0.5) {                           // the wheel cuts the surface and turns
                const double cphi = Clamp(dh / R, -1.0, 1.0), sphi = std::sqrt(1.0 - cphi * cphi), sg = r >= 0.0 ? 1.0 : -1.0;
                const VECTOR3 f = _V(std::sin(delta_[i]), 0.0, std::cos(delta_[i]));   // along the wheel
                at = _V(W.hub[0], W.hub[1], W.hub[2]) + f * (-sg * R * sphi) + upL * (dh + 0.05);   // 5 cm over the surface
                vel = f * (-cphi * r) + upL * (sphi * std::fabs(r));
                const double sub = Clamp((dh + R) / (2.0 * R), 0.0, 1.0);
                kg = 0.1 * kLiqRho * (16.0 * 0.03 * mpu::kWheelW) * sub * std::fabs(r);
            }
            AtmoFXPart::Emit(GetHandle(), 8400 + i, at, vel, 0.35, 0.5, kg, 3, 0, 0.0, 0.0, 0.0);
        }
        // the bow wave
        VECTOR3 v; GetGroundspeedVector(FRAME_LOCAL, v);
        const double sp = std::fabs(v.z), hullTop = cab_ ? 2.85 : 0.53, hullBot = -0.37;
        const double wet = Clamp(depth0 - hullBot, 0.0, hullTop - hullBot), B = cab_ && depth0 > kDeckY ? 3.0 : 1.6;
        double kg = 0.0; VECTOR3 at = _V(0, 0, 0), vel = _V(0, 0, 0);
        if (wet > 0.05 && depth0 < hullTop && sp > 0.5) {
            const double sg = v.z >= 0.0 ? 1.0 : -1.0;
            at = _V(0.0, 0.0, sg * 4.4) + upL * (depth0 + 0.05);
            vel = _V(0.0, 0.0, sg * 0.4 * sp) + upL * (0.5 * sp);
            kg = 0.01 * kLiqRho * B * wet * sp;
        }
        AtmoFXPart::Emit(GetHandle(), 8500, at, vel, 0.6, 0.6, kg, 3, 0, 0.0, 0.0, 0.0);
    }
    void ClearSpray() {
        if (!sprayOn_) return;
        for (int i = 0; i < mpu::kWheels; ++i) AtmoFXPart::Emit(GetHandle(), 8400 + i, _V(0, 0, 0), _V(0, 0, 0), 0, 0, 0.0, 3, 0, 0, 0, 0);
        AtmoFXPart::Emit(GetHandle(), 8500, _V(0, 0, 0), _V(0, 0, 0), 0, 0, 0.0, 3, 0, 0, 0, 0);
        sprayOn_ = false;
    }
    // the wakes on the surface (the AtmoFX client: the Kelvin wave behind a moving part, rings round a bobbing one, rings
    // spreading after an entry). Nodes: ids 1..8 the wheels where they cut the surface (half under: 0.3, deep in it: 0.6),
    // 10 the hull where it cuts the surface (floating: 1); a wheel or the belly coming into the liquid: one ring impulse,
    // 2..3 by how fast it came in. Refreshed when they change (or every 0.5 s)
    double wakeT_ = 0.0, wakeS_[11] = {}, wheelSubPrev_[mpu::kWheels] = {}, bellyWetPrev_ = 0.0; bool wakeOn_ = false;
    void Wake(const VECTOR3& upL, double depth0) {
        const double dt = oapiGetSimStep();
        if (dt <= 0.0) return;
        wakeT_ += dt;
        const bool refresh = wakeT_ > 0.5;
        if (refresh) wakeT_ = 0.0;
        wakeOn_ = true;
        auto set = [&](int id, const VECTOR3& pos, double r, double st) {
            if (st > 1.0 || refresh || std::fabs(st - wakeS_[id]) > 0.05) { AtmoFXWake::Set(GetHandle(), id, pos, r, st); if (st <= 1.0) wakeS_[id] = st; }
        };
        for (int i = 0; i < mpu::kWheels; ++i) {
            const auto& W = mpu::kWheel[i];
            const double sub = Clamp(wheelLiq_[i] / (2.0 * mpu::kWheelR), 0.0, 1.0);
            const VECTOR3 at = _V(W.hub[0], W.hub[1], W.hub[2]) + upL * hubDepth_[i];          // its centre at the waterline
            const double vin = (sub - wheelSubPrev_[i]) * 2.0 * mpu::kWheelR / dt;             // how fast it goes in, m/s
            if (wheelSubPrev_[i] <= 0.0 && sub > 0.0 && vin > 0.3) AtmoFXWake::Set(GetHandle(), 1 + i, at, mpu::kWheelW / 2, Clamp(2.0 + vin / 2.0, 2.0, 3.0));
            wheelSubPrev_[i] = sub;
            set(1 + i, at, mpu::kWheelW / 2, sub > 0.0 && sub < 1.0 ? 0.3 + 0.3 * sub : 0.0);  // all under: it no longer cuts the surface
        }
        // the hull: where its waterline is - the spine's and the cabin's sides cut the surface
        const double hullTop = cab_ ? 2.85 : 0.53, hullBot = -0.37;
        const double yW = depth0;                                                           // the surface over the origin, along the vertical
        const bool cuts = yW > hullBot && yW < hullTop;
        const double halfB = cab_ && yW > kDeckY ? 1.5 : 0.8;
        const double st = cuts ? (floating_ ? 1.0 : Clamp(buoyFrac_, 0.2, 1.0)) : 0.0;
        set(10, upL * yW, halfB, st);
        if (bellyWetPrev_ <= 0.0 && bellyWet_ > 0.0) {                                      // the belly comes into it
            VECTOR3 v; GetGroundspeedVector(FRAME_LOCAL, v);
            AtmoFXWake::Set(GetHandle(), 10, upL * yW, halfB, Clamp(2.0 + std::fabs(v.y) + 0.2 * length(v), 2.0, 3.0));
        }
        bellyWetPrev_ = bellyWet_;
    }
    void ClearWake() {
        if (!wakeOn_) return;
        for (int id = 1; id <= 10; ++id) { AtmoFXWake::Set(GetHandle(), id, _V(0, 0, 0), 0.0, 0.0); if (id < 11) wakeS_[id] = 0.0; }
        for (double& w : wheelSubPrev_) w = 0.0;
        bellyWetPrev_ = 0.0; wakeOn_ = false;
    }
    // the wheel off the ground: it spins free - the motor against the liquid's drag on its chevrons (or nothing in air) - and,
    // half in a liquid, paddles
    void FreeWheel(int i, double d, const VECTOR3& v, double fCmd, double dt) {
        const auto& W = mpu::kWheel[i];
        const double axle = W.hub[2];
        const double vx = v.x + yawRate_ * axle, vz = v.z - yawRate_ * W.hub[0];
        const double sd = std::sin(d), cd = std::cos(d), vf = vx * sd + vz * cd;
        const double sub = Clamp(wheelLiq_[i] / (2.0 * mpu::kWheelR), 0.0, 1.0);    // its submerged share (from the bottom)
        const double subLow = Clamp(sub * 2.0, 0.0, 1.0), subTop = Clamp(sub * 2.0 - 1.0, 0.0, 1.0);
        const double aCh = 16.0 * 0.03 * mpu::kWheelW;                             // the chevrons: 16 x 3 cm x the width, m2
        double& r = wRim_[i];
        const double dir = fCmd >= 0.0 ? 1.0 : -1.0;
        const double pWheel = (cruise_ ? kPcruise : limOff_ ? kPover : kPmax) / mpu::kWheels * MotorHealth(i);
        const double fMot = fCmd != 0.0 ? (std::min)(std::fabs(fCmd) / mpu::kWheels, pWheel / (std::max)(std::fabs(r), 1.0)) * dir : 0.0;
        const double rel = r - vf;
        const double fLiq = 0.5 * kLiqRho * 1.2 * aCh * sub * rel * std::fabs(rel);  // the liquid on the turning chevrons
        const double fFric = 30.0 * (r > 0 ? 1.0 : r < 0 ? -1.0 : 0.0);             // the bearings
        r += (fMot - fLiq - fFric) / kRimMass * dt;
        if (fCmd == 0.0 && sub <= 0.0) r *= std::exp(-dt / 20.0);                    // in air: it runs down slowly
        r = Clamp(r, -vTop(), vTop());
        if (std::fabs(r) < 0.02 && fCmd == 0.0) r = 0.0;
        spin_[i] = std::fmod(spin_[i] + r * dt / mpu::kWheelR, PI2);
        rim_[i] = r; vf_[i] = vf;
        // the paddle: the lower half's chevrons push the liquid back, the upper half's (if under too) pull it forward
        const double thrust = 0.5 * kLiqRho * 1.2 * (aCh * 0.5) * (subLow - subTop) * rel * std::fabs(rel);
        if (std::fabs(thrust) > 1.0 && oapiGetTimeAcceleration() <= 2.0) AddForce(_V(thrust * sd, 0.0, thrust * cd), _V(W.hub[0], W.hub[1] - mpu::kWheelR, W.hub[2]));
    }
    // the bubbles off what is warmer than the liquid (the AtmoFX client: dissolved N2 out, film boiling): the hub motors,
    // the belly; ids 1..8, 9 (the heat shimmer's numbers - another list in the client)
    double foamT_ = 0.0; bool foamOn_ = false;
    void Foam() {
        if ((foamT_ += oapiGetSimStep()) < 0.5) return;
        foamT_ = 0.0; foamOn_ = true;
        for (int i = 0; i < mpu::kWheels; ++i) {
            const auto& W = mpu::kWheel[i];
            // only above the mix's boiling point (~97 K for methane with its nitrogen at 1.5 bar): a part colder than that
            // does not boil it, and the wheel's own churning does not either (the liquid is not supersaturated)
            const double Tp = (std::max)(motTi_[i], tyreT_[i]);
            const double dT = motWet_[i] > 0.0 && Tp > kLiqBoilT ? Tp - kLiqT : 0.0;
            AtmoFXFoam::Set(GetHandle(), 1 + i, _V(W.hub[0], W.hub[1], W.hub[2]), 0.45, dT);
        }
        const double bT = bellyWet_ > 0.0 && bellyT_ > kLiqBoilT ? bellyT_ - kLiqT : 0.0;
        AtmoFXFoam::Set(GetHandle(), 9, _V(0.0, -0.37, 0.0), 2.0, bT);
    }
    void ClearFoam() {
        if (!foamOn_) return;
        for (int id = 1; id <= 9; ++id) AtmoFXFoam::Set(GetHandle(), id, _V(0, 0, 0), 0.0, 0.0);
        foamOn_ = false;
    }
public:
    double CabinWet() const { return cabWet_; }
    bool Floating() const { return floating_; }
    double WadeDepth() const { return wadeDepth_; }
    double FloodV() const { return floodV_; }
    double BuoyFrac() const { return buoyFrac_; }
protected:
    // ---------------- the marks a wheel leaves on the ground (the user, 2026-10-19: «визуально мы не видим» how it dug in; for
    // every vehicle on this chassis). The AtmoFX client lays them on the terrain (AtmoFXMark): the rut - a segment per 0.6 m
    // of the wheel's way, its depth the wheel's sinkage and the hole it dug, its width the tread's (a wet rut on wet ground:
    // darker, drying out); the pit - where a wheel spins in place, growing with the hole (dig_), left behind when it drives
    // on (each new pit its own id: 8600 + 8 k + i). Ruts last an hour, pits half an hour (the client fades them, at most 3000)
    double rutPath_[mpu::kWheels] = {}, pitDepth_[mpu::kWheels] = {};
    int pitN_[mpu::kWheels] = {};
    void Marks(int i, double vf, double delta, double dt) {
        // off until the client draws them right (2026-10-19: black blots under the machine - the user: «это что??? следы???»)
        static constexpr bool kMarksOn = true;
        if (!kMarksOn || !AtmoFXMark::Available() || n_[i] <= 0.0 || height_[i] > 0.05) return;
        const auto& W = mpu::kWheel[i];
        const VECTOR3 at = _V(W.hub[0], W.hub[1] - mpu::kWheelR - ride_ - height_[i], W.hub[2]);   // the ground under it
        const VECTOR3 dir = _V(std::sin(delta), 0.0, std::cos(delta));
        const int k = gndKind_[i];
        const bool wet = k == tvground::WET || k == tvground::LIQUID;
        // the rut
        rutPath_[i] += std::fabs(vf) * dt;
        if (rutPath_[i] >= 0.6) {
            const double depth = sink_[i] + dig_[i];
            if (depth > 0.005)
                AtmoFXMark::Add(GetHandle(), -1, at - dir * ((vf >= 0.0 ? 0.5 : -0.5) * rutPath_[i]), dir, rutPath_[i], mpu::kWheelW, depth,
                                wet ? AtmoFXMark::WETRUT : AtmoFXMark::RUT, 3600.0);
            rutPath_[i] = 0.0;
        }
        // the pit: while it digs in place it grows; driving on, it is left where it is
        if (dig_[i] > 0.01 && std::fabs(vf) < 0.3) {
            if (dig_[i] > pitDepth_[i] + 0.005 || pitDepth_[i] <= 0.0) {
                const double len = 2.0 * std::sqrt((std::max)(0.0, 2.0 * mpu::kWheelR * dig_[i] - dig_[i] * dig_[i])) + 0.3;
                AtmoFXMark::Add(GetHandle(), 8600 + 8 * pitN_[i] + i, at, dir, len, mpu::kWheelW + 0.1, dig_[i], AtmoFXMark::PIT, 1800.0);
                pitDepth_[i] = dig_[i];
            }
        } else if (pitDepth_[i] > 0.0 && std::fabs(vf) >= 0.3) {
            pitDepth_[i] = 0.0; pitN_[i] = (pitN_[i] + 1) % 50;                    // that one stays; the next is new
        }
    }
    // ---------------- the dirt on the machine (the user, 2026-10-19, «1-4 можно делать»: dirt by the world and the mileage;
    // drawn by the AtmoFX client - AtmoFXDirt: dense up to topY in the vessel frame, fading over ~0.6 m above, a light film on
    // the top faces). The amount grows with the distance driven on loose ground, e-fold per L metres by what it is - Titan's
    // wet margin (sticky, methane-damp tholin sand) 150 m, its sand 400, regolith 600, Mars 500, Io's crust and sulphur
    // 500, Earth's loam 1500, pebbles and ice 2000 - faster still where the wheels spin and throw it; a liquid it wades in
    // rinses the loose part off (~2 min). Its colour: what it picked up, mixed by the share each added
    double dirt_ = 0.0, dirtC_[3] = {0.35, 0.30, 0.25}, dirtSent_ = -1.0, dirtT_ = 0.0;
    // the body as boxes for the AtmoFX client's particles (AtmoFXPart::Box: they bounce off, stick or slide, the dust's
    // sprites keep out - the user, 2026-10-19: «частицы и пыль могут не проходить сквозь ЭМПУ?»). Above the wheels only (the
    // dust and the spray start at the wheels: a source inside a box is pushed out at once). Vessel frame, sent once
    bool boxesSent_ = false;
    void Boxes() {
        if (boxesSent_) return;
        boxesSent_ = true;
        AtmoFXPart::Box(GetHandle(), 1, _V(0.0, 0.08, 0.0), _V(0.8, 0.45, 4.3));                        // the spine
        AtmoFXPart::Box(GetHandle(), 2, _V(0.0, 0.67, 0.0), _V(kBodyX, 0.06, 0.5 * (kBodyZ1 - kBodyZ0)));   // the deck over the wheels
        if (cab_) AtmoFXPart::Box(GetHandle(), 3, _V(0.0, 0.5 * (kDeckY + 2.85), -0.2), _V(1.5, 0.5 * (2.85 - kDeckY), 4.1));   // the cabin
        else AtmoFXPart::Box(GetHandle(), 3, _V(0.0, kDeckY + 0.6, 3.6), _V(0.5, 0.6, 0.4));             // the post
        // the wheels (ids 100+: the client's vacuum regolith alone - the grains the front wheels throw back meet the rear ones)
        for (int i = 0; i < mpu::kWheels; ++i)
            AtmoFXPart::Box(GetHandle(), 100 + i, _V(mpu::kWheel[i].hub[0], mpu::kWheel[i].hub[1], mpu::kWheel[i].hub[2]), _V(0.5 * mpu::kWheelW, mpu::kWheelR, mpu::kWheelR));
    }
    void Dirt(double dt) {
        Boxes();
        OBJHANDLE ref = GetSurfaceRef();
        if (!ref || dt <= 0.0) return;
        char nm[64] = ""; oapiGetObjectName(ref, nm, sizeof nm);
        int cnt[5] = {}; for (int i = 0; i < mpu::kWheels; ++i) if (gndKind_[i] >= 0 && gndKind_[i] < 5) ++cnt[gndKind_[i]];
        int k = -1, kn = 0; for (int q = 0; q < 5; ++q) if (cnt[q] > kn) { kn = cnt[q]; k = q; }
        double L = 1500.0, c[3] = {0.38, 0.31, 0.23};
        if (!std::strcmp(nm, "Titan")) {
            if (k == tvground::WET) { L = 150.0; c[0] = 0.22; c[1] = 0.15; c[2] = 0.09; }
            else if (k == tvground::PEBBLES || k == tvground::ICE) { L = 2000.0; c[0] = 0.40; c[1] = 0.32; c[2] = 0.22; }
            else { L = 400.0; c[0] = 0.30; c[1] = 0.20; c[2] = 0.12; }
        }
        else if (!std::strcmp(nm, "Moon")) { L = 600.0; c[0] = 0.42; c[1] = 0.41; c[2] = 0.39; }
        else if (!std::strcmp(nm, "Mars")) { L = 500.0; c[0] = 0.55; c[1] = 0.32; c[2] = 0.20; }
        else if (!std::strcmp(nm, "Io")) { L = 500.0; c[0] = 0.70; c[1] = 0.62; c[2] = 0.28; }
        const bool moving = (GetFlightStatus() & 1) == 0;
        double add = 0.0;
        if (moving) {
            double path = 0.0, spin = 0.0, on = 0.0;
            for (int i = 0; i < mpu::kWheels; ++i) if (n_[i] > 0.0) { path += std::fabs(vf_[i]) * dt; spin += std::fabs(wRim_[i] - vf_[i]) * dt; on += 1.0; }
            if (on > 0.0) add = (path / on + 2.0 * spin / on) / L;            // thrown by the spinning too
        }
        if (add > 0.0) {
            const double d0 = dirt_, d1 = d0 + add * (1.0 - d0);
            for (int q = 0; q < 3; ++q) dirtC_[q] = d1 > 0.0 ? (dirtC_[q] * d0 + c[q] * (d1 - d0)) / d1 : c[q];
            dirt_ = d1;
        }
        if (wadeDepth_ > 0.1) dirt_ -= dirt_ * (1.0 - std::exp(-dt / 120.0)) * 0.5;   // rinsed (the half that is loose)
        dirt_ = Clamp(dirt_, 0.0, 1.0);
        if ((dirtT_ += dt) > 0.5 && (std::fabs(dirt_ - dirtSent_) > 0.01 || dirtSent_ < 0.0)) {
            dirtT_ = 0.0; dirtSent_ = dirt_;
            AtmoFXDirt::Set(GetHandle(), dirt_, dirtC_[0], dirtC_[1], dirtC_[2], -0.2);   // dense to the hubs and the belly
        }
    }
    void GroundFeel(int i, double vf, double dt) {
        const double v = std::fabs(vf);
        const int k = gndKind_[i];
        if (dt <= 0.0) return;
        if (k < 0) {
            // off a site raster, an airless body's regolith: its fines cling to the tyre (van der Waals, and the charge the
            // grains take in the Sun's UV - the Apollo rovers came back grey to the hubs), a dust film to about half the
            // sticky sand's clog, built over ~250 m; flung off as the rim speeds up (DustClient throws what is shed)
            if (!regolith_) return;
            if (n_[i] > 0.0) clog_[i] += v * dt / 250.0 * (1.0 - clog_[i] / 0.5);   // picked up only in contact
            clog_[i] -= std::pow(v / 10.0, 2.0) * dt / 20.0 * clog_[i];
            clog_[i] = Clamp(clog_[i], 0.0, 0.5);
            return;
        }
        if (k == tvground::SAND || k == tvground::WET || k == tvground::LIQUID) clog_[i] += v * dt / (k == tvground::SAND ? 300.0 : 80.0) * (1.0 - clog_[i]);
        else clog_[i] -= v * dt / 40.0 * clog_[i];
        clog_[i] -= std::pow(v / 10.0, 2.0) * dt / 20.0 * clog_[i];
        clog_[i] = Clamp(clog_[i], 0.0, 1.0);
        if (k == tvground::PEBBLES && v > 0.3 && n_[i] > 0.0 && oapiGetTimeAcceleration() <= 2.0) {
            bumpT_[i] -= dt;
            if (bumpT_[i] <= 0.0) { bumpT_[i] = 0.12 / v; bumpF_[i] = (std::rand() / (double)RAND_MAX * 2.0 - 1.0) * 0.5 * n_[i] * Clamp(v / 4.0, 0.0, 1.0); }
            const auto& W = mpu::kWheel[i];
            AddForce(_V(0.0, bumpF_[i], 0.0), _V(W.hub[0], W.hub[1] - mpu::kWheelR - ride_, W.hub[2]));
        } else bumpF_[i] = 0.0;
    }
    const Soil* soilW_[mpu::kWheels] = {&kMoonSoil, &kMoonSoil, &kMoonSoil, &kMoonSoil, &kMoonSoil, &kMoonSoil, &kMoonSoil, &kMoonSoil};
    int gndKind_[mpu::kWheels] = {-1, -1, -1, -1, -1, -1, -1, -1};
    double clog_[mpu::kWheels] = {}, bumpT_[mpu::kWheels] = {}, bumpF_[mpu::kWheels] = {};
    bool regolith_ = false;                                          // on an airless body's regolith (Dust(): the tyres' film)

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
        TOUCHDOWNVTX t[32];
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
        // the roof and the upper sides (the user, 2026-10-19: rolled over on Io, the body sank into the ground - «крыша должна
        // быть физической»): on its roof or its side it lies on these, as stiff as the belly's
        const double zf = bz1_ - 0.3, zr = bz0_ + 0.3, ym = 0.5 * (btop_ - 0.37), xr = cab_ ? 1.5 : kDeckX;
        const VECTOR3 top[8] = {{-xr, btop_, zf}, {xr, btop_, zf}, {-xr, btop_, zr}, {xr, btop_, zr},
                                {-kBodyX, ym, zf}, {kBodyX, ym, zf}, {-kBodyX, ym, zr}, {kBodyX, ym, zr}};
        for (const VECTOR3& p : top) t[n++] = {p, 10.0 * k_, 2.0 * c, 0.8, 0.8};
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
        for (int i = 0; i < kCellsN; ++i) {
            if (cab_) for (int j = 0; j < empu::kCellDoorN[i]; ++j) hatches_.push_back(RestCopy(empu::kCellDoorGrp[i][j], i, kDoor));
            cells_.push_back(RestCopy(cab_ ? empu::kCellPackGrp[i] : mpu::kCellPackGrp[i], i, kCellPack));
        }
    }

    enum { kDoor = 4, kCellPack = 5 };
    struct Part { int grp, wheel, kind; std::vector<NTVERTEX> rest, work; };
    Part RestCopy(int grp, int wheel, int kind) const {
        Part p{grp, wheel, kind, {}, {}};
        if (MESHGROUP* g = oapiMeshGroup(mesh_, (DWORD)grp)) { p.rest.assign(g->Vtx, g->Vtx + g->nVtx); p.work = p.rest; }
        return p;
    }

    void clbkVisualCreated(VISHANDLE vis, int refcount) override {
        (void)refcount; dev_ = GetDevMesh(vis, meshIdx_);
        for (double& t : lastTh_) t = 9.0;                                       // a new visual: every wheel placed again
        for (Part& p : hatches_) if (MESHGROUP* g = oapiMeshGroup(mesh_, (DWORD)p.grp)) { p.rest.assign(g->Vtx, g->Vtx + g->nVtx); p.work = p.rest; }
        for (Part& p : cells_) if (MESHGROUP* g = oapiMeshGroup(mesh_, (DWORD)p.grp)) { p.rest.assign(g->Vtx, g->Vtx + g->nVtx); p.work = p.rest; }
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
            dv[i] = (height_[i] < 0.0 ? (std::min)(-height_[i], 0.45) : -(std::min)(height_[i], 0.45)) - ride_ - sink_[i] - meltSink_[i] - dig_[i];
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
        for (Part& p : hatches_) {
            // (2026-10-16) hinged at its bottom edge, it lowers outwards to level: a shelf in line with the slot's rails
            // that the cartridge slides out over (it hung up over the slot before)
            const int k = p.wheel;
            const double a = -hatchOpen_[k] * (90.0 * PI / 180.0);
            const double c = std::cos(a), s = std::sin(a);
            const double px = 1.265, py = -0.175;
            for (size_t i = 0; i < p.rest.size(); ++i) {
                const NTVERTEX& v0 = p.rest[i]; NTVERTEX& v = p.work[i];
                const double dx = v0.x - px, dy = v0.y - py;
                v.x = (float)(px + dx * c - dy * s); v.y = (float)(py + dx * s + dy * c); v.z = v0.z;
                v.nx = (float)(v0.nx * c - v0.ny * s); v.ny = (float)(v0.nx * s + v0.ny * c); v.nz = v0.nz;
            }
            GROUPEDITSPEC ges{}; ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; ges.Vtx = p.work.data(); ges.nVtx = (DWORD)p.work.size();
            oapiEditMeshGroup(dev_, (DWORD)p.grp, &ges);
        }
        for (Part& p : cells_) {
            const int k = p.wheel;
            const VECTOR3 center = _V(1.01, 0.005, kCellZ[k]);
            VECTOR3 hand = center + _V(0.70, 0.75, 0.0); double yaw = 0.0;
            CellHandPose(cellActor_[k], hand, yaw);
            const int motion = cellMotion_[k];
            const bool visible = cell_[k] >= 0 || motion == 1 || motion == 2 || motion == 3 || motion == 4;
            const double progress = Clamp(cellProgress_[k], 0.0, 1.0);
            auto ease = [](double v) { v = Clamp(v, 0.0, 1.0); return v * v * (3.0 - 2.0 * v); };
            double t = cell_[k] >= 0 ? 0.0 : 1.0, turn = 0.0;
            if (motion == 1 || motion == 3) {
                // Straight extraction first; only once the cartridge clears the rails does
                // it turn into the astronaut's grip, in place at the end of the reach.
                t = ease(progress / 0.82); turn = ease((progress - 0.82) / 0.18);
            } else if (motion == 2 || motion == 4) {
                // Insertion is the exact reverse: align the cartridge at the hands, then slide it in.
                t = 1.0 - ease((progress - 0.18) / 0.82); turn = 1.0 - ease(progress / 0.18);
            }
            const double a = yaw * turn, c = std::cos(a), s = std::sin(a);
            // the path: first straight out along the rails over the lowered cover (0.45 m, level), then up to her hands
            const VECTOR3 shelf = center + _V(0.45, 0.012, 0.0);
            const double t1 = Clamp(t / 0.45, 0.0, 1.0), t2 = Clamp((t - 0.45) / 0.55, 0.0, 1.0);
            const VECTOR3 at = center + (shelf - center) * t1 + (hand - shelf) * t2;
            for (size_t i = 0; i < p.rest.size(); ++i) {
                const NTVERTEX& v0 = p.rest[i]; NTVERTEX& v = p.work[i];
                const double dx = v0.x - center.x, dz = v0.z - center.z;
                const double sc = visible ? 1.0 : 0.0;
                v.x = (float)(at.x + (dx * c + dz * s) * sc);
                v.y = (float)(at.y + (v0.y - center.y) * sc);
                v.z = (float)(at.z + (-dx * s + dz * c) * sc);
                v.nx = (float)(v0.nx * c + v0.nz * s); v.ny = v0.ny; v.nz = (float)(-v0.nx * s + v0.nz * c);
            }
            GROUPEDITSPEC ges{}; ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; ges.Vtx = p.work.data(); ges.nVtx = (DWORD)p.work.size();
            oapiEditMeshGroup(dev_, (DWORD)p.grp, &ges);
        }
    }

    void UpdateHatches(double dt) {
        for (int k = 0; k < kCellsN; ++k) {
            const double target = hatchTarget_[k] ? 1.0 : 0.0;
            hatchOpen_[k] += Clamp(target - hatchOpen_[k], -dt / 1.0, dt / 1.0);
        }
    }

    void UpdateCells(double dt) {
        for (int k = 0; k < kCellsN; ++k) {
            int& m = cellMotion_[k]; double& p = cellProgress_[k];
            if (m == 1 || m == 2) p = Clamp(p + dt / 1.2, 0.0, 1.0);
            else if (m == 3 || m == 4) {
                p = (std::max)(0.0, p - dt / 0.35);
                if (p <= 0.0) { m = 0; cellActor_[k] = 0; }
            }
        }
    }

    void CellHandPose(int person, VECTOR3& hand, double& yaw) const {
        if (!person || !api_.Info) return;
        OcInfo info{};
        if (!api_.Info(person, &info) || !info.vessel || !oapiIsVessel(info.vessel)) return;
        VECTOR3 gp; oapiGetGlobalPos(info.vessel, &gp); Global2Local(gp, hand);
        hand.y += 0.05;   // OrbiterCrew positions the body origin at its stand height; held cells use this same frame
        double shipH = 0.0, personH = 0.0; GetHeading(shipH); oapiGetHeading(info.vessel, &personH);
        yaw = personH - shipH;
        while (yaw > PI) yaw -= PI2; while (yaw < -PI) yaw += PI2;
        hand.x += std::sin(yaw) * 0.32; hand.z += std::cos(yaw) * 0.32;
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
        // the touchdown points again only if they changed: setting them takes it out of Orbiter's landed state (the drive's
        // wake relies on that) - set here, the rest never held (the probe: «Orbiter took it out» one frame after every rest)
        const double g = LocalG();
        if (std::fabs(g - gSet_) > 1e-6 || std::fabs(ride_ - rideSet_) > 1e-6) Contacts(g);
        SeatOnWheels(ref, true);                                                    // parked: levelled
        still_ = 0.0;
    }

    MESHHANDLE mesh_ = nullptr;
    UINT meshIdx_ = 0;
    DEVMESHHANDLE dev_ = nullptr;
    double lastTh_[mpu::kWheels] = {9, 9, 9, 9, 9, 9, 9, 9}, lastDelta_[mpu::kWheels] = {}, lastSpin_[mpu::kWheels] = {};
    std::vector<Part> part_, steps_, hatches_, cells_;
    double hatchOpen_[kCellsN] = {0.0, 0.0, 0.0, 0.0};
    bool hatchTarget_[kCellsN] = {false, false, false, false};
    int cellMotion_[kCellsN] = {0, 0, 0, 0}, cellActor_[kCellsN] = {0, 0, 0, 0};
    double cellProgress_[kCellsN] = {0.0, 0.0, 0.0, 0.0};
    Part bar_;
    bool cab_ = false;                       // Э.МПУ: the crew cabin and the thrust module (Variant = EMPU in the config)
    double bz0_ = kBodyZ0, bz1_ = kBodyZ1, btop_ = kBodyTop;
    double stepsOut_ = 1.0, stepsDrawn_ = -1.0;
    double height_[mpu::kWheels] = {}, n_[mpu::kWheels] = {}, delta_[mpu::kWheels] = {}, spin_[mpu::kWheels] = {};
    double driveSm_ = 0.0, stuckT_ = 0.0, slipCruise_ = 0.08; bool boostWas_ = false;
    bool limOff_ = false, motTrip_ = false;                          // the limiter is off; a motor has tripped on heat
    double motT_ = -1.0, lossW_ = 0.0, motDerate_ = 1.0;             // the hottest motor K (-1: not set yet), their heat W, the least derate
    double motTi_[mpu::kWheels] = {}, motDer_[mpu::kWheels] = {1, 1, 1, 1, 1, 1, 1, 1}, lossI_[mpu::kWheels] = {};
    double pI_[mpu::kWheels] = {}, fI_[mpu::kWheels] = {};           // each motor's mechanical power W and force N this step
    bool tripI_[mpu::kWheels] = {};
    double vTop() const { return limOff_ ? kVunl : kVmax; }
    // the motors' heat balance: backward-Euler sub-steps (a day's time acceleration is thousands of seconds a step)
    // Eight motors, each with 1/8 of the heat capacity, the radiators and the airflow; the coolant loop couples them to their
    // mean (150 W/K each). The losses come from each one's own load (pI_, fI_, filled by the wheel loop)
    void MotorThermal(double dt) {
        // the radiators see the ground and the sky: half and half (TVHeat RadiatorSinkK; 200 K before the ground was known) -
        // over hot ground they cannot reject. In air its temperature, or that ground if hotter
        const double sinkK = heatInit_ ? heatSinkK_ : 200.0;
        const double tamb = GetAtmPressure() > 100.0 ? (std::max)(GetAtmTemperature(), sinkK) : sinkK;
        if (motT_ < 0.0) { for (double& t : motTi_) t = (std::max)(tamb, 280.0); motT_ = motTi_[0]; }
        const double rho = (std::max)(0.0, GetAtmDensity()), v = std::fabs(speed_);
        const double G = (60.0 + (rho > 0.005 ? 600.0 * (1.0 + v / 8.0) * std::sqrt(rho / 1.2) : 0.0)) / mpu::kWheels;   // W/K each
        const double eA = 0.85 * 5.67e-8 * kRadA / mpu::kWheels, Ci = kMotC / mpu::kWheels, kc = 150.0;
        const double over = (std::max)(0.0, v - 22.2) / 10.0;
        lossW_ = 0.0;
        for (int i = 0; i < mpu::kWheels; ++i) {
            const double p = pI_[i], kf = fI_[i] / (kFmotor / mpu::kWheels);
            lossI_[i] = kIronW * std::pow(v / 22.2, 1.6) * (p != 0.0 ? 1.0 : 0.15) + (kFwW / mpu::kWheels) * over * over * (p != 0.0 ? 1.0 : 0.0)
                      + (kCuW / mpu::kWheels) * kf * kf + 0.01 * (std::max)(0.0, p) + 0.25 * (std::max)(0.0, -p);
            lossW_ += lossI_[i];
        }
        const int ns = (int)Clamp(std::ceil(dt / 5.0), 1.0, 120.0); const double h = dt / ns;
        for (int s = 0; s < ns; ++s) {
            double tm = 0.0; for (double t : motTi_) tm += t / mpu::kWheels;
            for (int i = 0; i < mpu::kWheels; ++i) {
                const double T = motTi_[i];
                const double gl = 240.0 * motWet_[i];                                  // in a liquid: its housing's boiling film
                const double Q = G * (T - tamb) + eA * (std::pow(T, 4) - std::pow(tamb, 4)) + kc * (T - tm) + gl * (T - kLiqT), dQ = G + 4.0 * eA * T * T * T + kc + gl;
                motTi_[i] += h * (lossI_[i] + rimW_[i] - Q) / Ci / (1.0 + h * dQ / Ci);   // + the hot rim's heat (HeatStep)
            }
        }
        motT_ = 0.0; motDerate_ = 1.0; motTrip_ = false;
        for (int i = 0; i < mpu::kWheels; ++i) {
            if (!std::isfinite(motTi_[i])) motTi_[i] = tamb;
            if (motTi_[i] > kMotTrip && !tripI_[i]) { tripI_[i] = true; oapiWriteLogV("TVChassis %s: traction motor %d tripped on heat (%.0f C)", GetName(), i + 1, motTi_[i] - 273.15); }
            if (motTi_[i] < kMotReset) tripI_[i] = false;
            motDer_[i] = tripI_[i] ? 0.0 : Clamp((kMotTrip - motTi_[i]) / (kMotTrip - kMotDerate0), 0.0, 1.0);
            // heat damage that stays: the motor's own windings and magnets past their onset, the inverters and the harness
            // in the burning belly (TVHeat.h)
            const double before = motDmg_[i];
            motDmg_[i] = (std::min)(1.0, motDmg_[i] + tvheat::DamageRate(motTi_[i], tvheat::kMotorOnsetK) * dt);
            if (before < 0.5 && motDmg_[i] >= 0.5) oapiWriteLogV("TVChassis %s: hub motor %d burnt (%.0f C) - dead", GetName(), i + 1, motTi_[i] - 273.15);
            motDer_[i] *= MotorHealth(i);
            motT_ = (std::max)(motT_, motTi_[i]); motDerate_ = (std::min)(motDerate_, motDer_[i]); motTrip_ |= tripI_[i];
        }
    }

    // ---------------- heat damage on hot ground and lava (the user, 2026-10-18; the model, its numbers: TVHeat.h) ----------------
    // Each wheel: what it stands on (the lava field's raster, else the body's day / night ground), does the crust hold it
    // (the crew's rule at the wheel's load), the tyre's heat node and its damage, the heat its rim passes to the hub motor.
    // The belly: the ground it sees, its damage and stages. Stepped every 0.25 s (implicit, time acceleration safe)
public:
    double WheelHealth(int i) const { return wheelHp_[i] * (1.0 - tyreDmg_[i]); }
    double MotorHealth(int i) const {
        const double own = motDmg_[i] >= 0.5 ? 0.0 : 1.0 - motDmg_[i];
        const double feed = bellyDmg_ >= tvheat::kStageHarness ? 0.0 : bellyDmg_ >= tvheat::kStageInverters ? 0.5 : 1.0;
        return own * feed;
    }
    double HullUAFactor() const { return 1.0 + 4.0 * bellyDmg_; }       // the cabin floor's insulation burns with the belly
    double HeatGroundK() const { return heatInit_ ? heatGroundK_ : 0.0; }   // the ground at 1 m (lava-aware), K
    bool InMelt(int i) const { return meltSink_[i] > 0.02; }
    // something to warn about: a tyre or the belly near its onset, a wheel in the melt, a motor burning
    bool HeatAlarm() const {
        if (!heatInit_) return false;
        if (bellyT_ > bellySpec_.onsetK - 30.0 || bellyDmg_ > 0.0) return true;
        for (int i = 0; i < mpu::kWheels; ++i)
            if (tyreT_[i] > tyreSpec_.onsetK - 50.0 || tyreDmg_[i] > 0.0 || InMelt(i) || motDmg_[i] > 0.0 || motTi_[i] > tvheat::kMotorOnsetK - 30.0) return true;
        return false;
    }
protected:
    tvheat::TyreSpec tyreSpec_; tvheat::BellySpec bellySpec_;
    double tyreT_[mpu::kWheels] = {-1, -1, -1, -1, -1, -1, -1, -1}, tyreDmg_[mpu::kWheels] = {}, motDmg_[mpu::kWheels] = {};
    double meltSink_[mpu::kWheels] = {}, meltTarget_[mpu::kWheels] = {}, standT_[mpu::kWheels] = {}, rimW_[mpu::kWheels] = {};
    double wLat_[mpu::kWheels] = {}, wLng_[mpu::kWheels] = {}, crustM_[mpu::kWheels] = {}, contactK_[mpu::kWheels] = {}, wRadK_[mpu::kWheels] = {};
    int cellA_[mpu::kWheels] = {}, cellB_[mpu::kWheels] = {}, kind_[mpu::kWheels] = {};
    bool cellSet_[mpu::kWheels] = {}, lavaW_[mpu::kWheels] = {}, tyreOn_[mpu::kWheels] = {}, tyreDead_[mpu::kWheels] = {};
    double bellyT_ = -1.0, bellyDmg_ = 0.0, bellyRadK_ = 0.0; bool bellyHoled_ = false; int bellyStage_ = 0;
    double heatGroundK_ = 0.0, heatSinkK_ = 200.0, heatAcc_ = 0.0, meltK_ = 1450.0, tableK_ = 0.0;
    bool heatInit_ = false;
    // the lava field of a body (Config\<body>\LavaField\LavaField.cfg), loaded once per body for every vehicle of this DLL;
    // its melt's temperature from the same file (T_ERUPT)
    static const lava::Field* LavaFor(OBJHANDLE ref, double* meltK) {
        struct F { lava::Field f; double melt = 1450.0; bool ok = false; };
        static std::map<std::string, F> fields;
        char nm[64] = ""; oapiGetObjectName(ref, nm, sizeof nm);
        auto it = fields.find(nm);
        if (it == fields.end()) {
            F& f = fields[nm];
            const std::string path = std::string("Config\\") + nm + "\\LavaField\\LavaField.cfg";
            f.ok = f.f.Load(path.c_str());
            if (f.ok) {
                if (FILE* fp = std::fopen(path.c_str(), "r")) {
                    char ln[256];
                    while (std::fgets(ln, sizeof ln, fp)) if (!std::strncmp(ln, "T_ERUPT", 7)) { const char* e = std::strchr(ln, '='); if (e) f.melt = std::atof(e + 1); }
                    std::fclose(fp);
                }
                oapiWriteLogV("TVChassis: lava field for %s loaded (melt %.0f K) - heat damage of the vehicles", nm, f.melt);
            }
            it = fields.find(nm);
        }
        if (meltK) *meltK = it->second.melt;
        return it->second.ok ? &it->second.f : nullptr;
    }
    // the body's own ground (day / night table; elsewhere the airless curve) and the sunlight here, W/m2 (0 at night)
    double TableGround(OBJHANDLE ref, double& sunW) const {
        OBJHANDLE sun = oapiGetGbodyByIndex(0); VECTOR3 sp, me, c;
        oapiGetGlobalPos(sun, &sp); GetGlobalPos(me); oapiGetGlobalPos(ref, &c);
        const VECTOR3 toSun = sp - me; const double dist = length(toSun);
        const double sinE = dotp(toSun / dist, unit(me - c));
        sunW = sinE > 0.0 ? 3.828e26 / (4.0 * PI * dist * dist) : 0.0;
        char nm[64] = ""; oapiGetObjectName(ref, nm, sizeof nm);
        const tvthermal::Temps tt = tvthermal::SurfaceTemps(nm, sinE);
        return tt.known ? tt.ground : 100.0 + 290.0 * std::pow((std::max)(0.0, sinE), 0.5);
    }
    // the warm parts for the AtmoFX client's heat shimmer (in an air only; ids 1..8 the wheels' hubs - the motor or the tyre,
    // whichever is hotter - 9 the belly; the module's own from 10): their temperature over the air's
    void Shimmer() {
        const bool air = GetAtmPressure() > 50.0;
        const double amb = air ? GetAtmTemperature() : 0.0;
        auto dT = [&](double T) { return air && T - amb > 3.0 ? T - amb : 0.0; };
        for (int i = 0; i < mpu::kWheels; ++i) {
            const auto& W = mpu::kWheel[i];
            AtmoFXHeat::Set(GetHandle(), 1 + i, _V(W.hub[0], W.hub[1], W.hub[2]), 0.6, dT((std::max)(motTi_[i], tyreT_[i])));
        }
        AtmoFXHeat::Set(GetHandle(), 9, _V(0.0, -0.37, 0.0), 2.5, dT(bellyT_));
    }
    void HeatStep(double dt) {
        OBJHANDLE ref = GetSurfaceRef();
        if (!ref || dt <= 0.0) return;
        const lava::Field* lf = LavaFor(ref, &meltK_);
        const double g = LocalG(), wLoad = GetMass() * g / mpu::kWheels;
        char body[64] = ""; oapiGetObjectName(ref, body, sizeof body);
        // every step: the crust under each wheel. Decided when the wheel comes onto a new cell of the raster (the crew's
        // rule and hash, its thicknesses scaled to this wheel's load); through it the wheel settles in ~2 s, out in ~1 s
        for (int i = 0; i < mpu::kWheels; ++i) {
            const double latD = wLat_[i] * DEG, lngD = wLng_[i] * DEG;
            lava::Sample s;
            const bool on = lf && lf->At(latD, lngD, s) && s.lava;
            lavaW_[i] = on; kind_[i] = on ? s.kind : 0; crustM_[i] = on ? s.crustM : 0.0; contactK_[i] = on ? s.contactK : 0.0;
            if (height_[i] > 0.10) continue;                                      // in the air: nothing under it
            const int ca = (int)std::floor(latD / 7.866e-6), cb = (int)std::floor(lngD / 7.870e-6);
            if (!cellSet_[i] || ca != cellA_[i] || cb != cellB_[i]) {
                cellSet_[i] = true; cellA_[i] = ca; cellB_[i] = cb;
                if (on) {
                    double hB, hM; tvheat::CrustLimits(n_[i] > 0.0 ? n_[i] : wLoad, hB, hM);
                    const double u = tvheat::CellHash(latD, lngD);
                    if (tvheat::CrustBreaks(s.kind, s.crustM, u, hB, hM)) {
                        if (meltTarget_[i] <= 0.0) {
                            // the shield's summit lake (as the crew's: ~16 m round its centre on Io): knee-deep
                            const double R = oapiGetSize(ref), dLat = wLat_[i] - (-1.7734924 * RAD), dLng = wLng_[i] - (-153.6089511 * RAD);
                            const bool lake = !std::strcmp(body, "Io") && std::hypot(dLat * R, dLng * R * std::cos(wLat_[i])) < 16.0;
                            meltTarget_[i] = tvheat::MeltDepth(s.kind, lake, u);
                            oapiWriteLogV("TVChassis %s: wheel %d broke through the crust (%.1f cm, breaks under %.1f at %.0f N) - %.2f m into the melt",
                                          GetName(), i + 1, s.crustM * 100.0, hB * 100.0, n_[i] > 0.0 ? n_[i] : wLoad, meltTarget_[i]);
                        }
                    } else meltTarget_[i] = 0.0;                                  // onto a crust that holds: out
                } else meltTarget_[i] = 0.0;
            }
            meltSink_[i] += (meltTarget_[i] - meltSink_[i]) * (1.0 - std::exp(-dt / (meltTarget_[i] > meltSink_[i] ? 2.0 : 1.0)));
            standT_[i] = std::fabs(vf_[i]) > 0.05 ? 0.0 : standT_[i] + dt;
        }
        heatAcc_ += dt;
        if (heatInit_ && heatAcc_ < 0.25) return;
        const double h = heatAcc_; heatAcc_ = 0.0;
        // the surroundings: the body's ground, the lava field's glow (T^4 by the view factor at the height that looks)
        double sunW = 0.0;
        const double tg = TableGround(ref, sunW);
        tableK_ = tg;
        double lng, lat, rad; GetEquPos(lng, lat, rad);
        const double bellyH = (std::max)(0.2, mpu::kCgH - kSag - 0.37 + ride_);   // the belly over the ground
        heatGroundK_ = tg; bellyRadK_ = tg;
        if (lf) {
            const double r1 = lf->GroundRadiativeK(lat * DEG, lng * DEG, 1.0, tg), rb = lf->GroundRadiativeK(lat * DEG, lng * DEG, bellyH, tg);
            if (r1 > 0.0) heatGroundK_ = (std::max)(tg, r1);
            if (rb > 0.0) bellyRadK_ = (std::max)(tg, rb);
        }
        heatSinkK_ = tvheat::RadiatorSinkK(heatGroundK_);
        if (!heatInit_) {                                                         // not from the scenario: as it stands here
            for (double& t : tyreT_) if (t < 0.0) t = tg;
            if (bellyT_ < 0.0) bellyT_ = (std::max)(tg, 200.0);
            heatInit_ = true;
        }
        for (int i = 0; i < mpu::kWheels; ++i) {
            const Soil& S = *soilW_[i];
            tvheat::TyreEnv e;
            e.groundK = lavaW_[i] ? contactK_[i] : tg;
            e.radK = tg;
            if (lf) { const double r = lf->GroundRadiativeK(wLat_[i] * DEG, wLng_[i] * DEG, mpu::kWheelR, tg); if (r > 0.0) e.radK = (std::max)(tg, r); }
            wRadK_[i] = e.radK;
            e.meltK = meltK_; e.melt = meltSink_[i];
            if (wheelLiq_[i] > 0.005) { e.meltK = kLiqT; e.melt = wheelLiq_[i]; }        // a sea: the same contact, cold
            const WheelSoil w = SoilUnder(S, (std::max)(n_[i] > 0.0 ? n_[i] : wLoad, 1.0), kWheelRflex, mpu::kWheelW, g);
            e.patchA = height_[i] > 0.05 ? 0.0 : w.A; e.patchL = w.L;
            e.speed = std::fabs(vf_[i]); e.standT = standT_[i]; e.lava = lavaW_[i]; e.crustM = crustM_[i];
            e.sunW = sunW; e.motorK = motTi_[i] > 0.0 ? motTi_[i] : 300.0;
            tyreT_[i] = tvheat::TyreStep(tyreSpec_, e, tyreT_[i], h, rimW_[i]);
            if (!std::isfinite(tyreT_[i])) tyreT_[i] = tg;
            tyreDmg_[i] = (std::min)(1.0, tyreDmg_[i] + tvheat::DamageRate(tyreT_[i], tyreSpec_.onsetK) * h);
            if (!tyreOn_[i] && tyreT_[i] > tyreSpec_.onsetK) { tyreOn_[i] = true; oapiWriteLogV("TVChassis %s: tyre %d past its onset (%.0f K) - the mesh creeps", GetName(), i + 1, tyreT_[i]); }
            if (!tyreDead_[i] && WheelHealth(i) < 0.15 && tyreDmg_[i] > 0.0) { tyreDead_[i] = true; oapiWriteLogV("TVChassis %s: tyre %d burnt - the wheel is dead", GetName(), i + 1); }
        }
        // the belly: the ground it sees; a melt it sits in (the wheels sunk deeper than it stands)
        tvheat::BellyEnv be; be.radK = bellyRadK_; be.meltK = meltK_;
        for (int i = 0; i < mpu::kWheels; ++i) if (meltSink_[i] > bellyH) be.wetFrac += 1.0 / mpu::kWheels;
        if (bellyWet_ > 0.0) { be.wetFrac = bellyWet_; be.meltK = kLiqT; }                // a sea under it
        bellyT_ = tvheat::BellyStep(bellySpec_, be, bellyT_, h);
        if (!std::isfinite(bellyT_)) bellyT_ = tg;
        bellyDmg_ = (std::min)(1.0, bellyDmg_ + tvheat::DamageRate(bellyT_, bellySpec_.onsetK) * h);
        static const char* stageLog[5] = {"", "the drive's inverters lost - the drive halved", "the battery bay - the cells short inside",
                                          "the traction harness burnt - no drive", "the belly burnt through"};
        const int stage = bellyDmg_ >= 1.0 ? 4 : bellyDmg_ >= tvheat::kStageHarness ? 3 : bellyDmg_ >= tvheat::kStageBattery ? 2 : bellyDmg_ >= tvheat::kStageInverters ? 1 : 0;
        if (stage > bellyStage_) { bellyStage_ = stage; oapiWriteLogV("TVChassis %s: belly %.0f K, damage %.0f %% - %s", GetName(), bellyT_, bellyDmg_ * 100.0, stageLog[stage]); Sfx(kSndAlarm, 1.0); }
        if (bellyDmg_ >= tvheat::kStageBattery) CellsUse(CellsSum() * (1.0 - std::exp(-tvheat::kBatteryLoss * h)));   // shorting cells
        Shimmer();
        if (bellyDmg_ >= 1.0 && !bellyHoled_) {
            bellyHoled_ = true;
            if (cab_) { hullLeak_ += 20.0e-4; oapiWriteLogV("TVChassis %s: the cabin's floor burnt through - leak %.1f cm2", GetName(), hullLeak_ * 1e4); }
        }
    }
    double k_ = 0.0, gSet_ = 0.0, steer_ = 0.0, drive_ = 0.0, speed_ = 0.0, yawRate_ = 0.0, hdgPrev_ = 0.0;
    double batt_ = 1.0, powerW_ = 0.0, still_ = 0.0;
    bool park_ = true;
    double armState_[mpu::kWheels] = {}, testT_ = 0.0, logT_ = 0.0;
    double ride_ = 0.0, rideSet_ = 0.0, vCap_ = kVmax; bool cruise_ = true;
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
    double dustLv_[mpu::kWheels] = {}, vf_[mpu::kWheels] = {}, rim_[mpu::kWheels] = {};
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
    s->full = cruise_ ? 0 : 1;
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
