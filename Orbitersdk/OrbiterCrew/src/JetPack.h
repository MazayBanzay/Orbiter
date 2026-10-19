// OrbiterCrew - the jet pack «Наплечный» for the «Каркас» suit.
// A moulded shell round the life-support pack is the propellant tank (12 kg of monopropellant, exhaust ~2.9 km/s,
// ~280 m/s for her with suit and pack). Two pods of 150 N on folding booms at shoulder height give the lift: the
// thrust acts above the centre of mass, so she hangs under it like a pendulum. The pods tilt +-20 deg about their
// lateral axis: together for pitch, against each other for yaw; differential thrust gives roll. Thrust always runs
// along her body ("up"): she hovers upright and flies head first in space. Small RCS ports in the shell corners.
// Worn, the pack is part of the crew member (its propellant is one of her resources); dropped, it is a plain vessel
// (Config\Vessels\OrbiterCrew\JetPack.cfg). Geometry comes from Config\Tantra\JetPack.geo (export_skin.py).
//
// Keys while worn: numpad 0 / . thrust up / down (with the height hold: the held height up / down);
//   W swings the jets aft (push forward), S forward (brake, back); Shift: full vector (75 deg instead of 30);
//   A / D turn (the pods against each other); J height hold (it also limits the vector so the lift holds her weight);
//   C landing autopilot; G booms out / folded. Near a surface the pack's small ports keep her upright.
#pragma once
#include <Orbitersdk.h>
#include "Skin.h"
#include "SuitHud.h"
#include <string>
#include <vector>

namespace ocrew
{
	// flight keys while the pack is worn: pitch W+ S-, yaw D+ A-, roll E+ Q-, climb R+ F-
	// throttle numpad 0 + . -; pitch W+ S-; yaw D+ A-; strafe E+ Q-; vertical Space+ LCtrl-; boost Shift
	struct FlightInput { double pitch{}, yaw{}, roll{}, climb{}, throttle{}, strafe{}, vertical{}; bool boost{}, ap{}; };   // ap: the autopilot steers (pitch/strafe are the pods' tilt)

	struct RcsSet
	{
		THRUSTER_HANDLE th[2][2][6]{};   // [x side][y side][direction +x -x +y -y +z -z]
		std::vector<THRUSTER_HANDLE> all;
	};

	class JetPack
	{
	public:
		// thrust: 2 x 500 N gives T/W ~4.7 on the Moon, ~2 on Mars (132 kg with her); on Earth it only eases jumps (0.77).
		// The delta-v stays 2900 * ln(132/120) ~ 280 m/s: thrust changes how hard it pushes, not how far it goes.
		// metallic hydrogen: one component, it relaxes to hot H2 in the pods' chambers; diluted with molecular hydrogen
		// to keep the chamber alive, ~10 km/s exhaust. 12 kg: ~950 m/s, ~9 min hovering on the Moon.
		static constexpr double DRY = 16, FUEL = 12, ISP = 10000, POD_F = 500, RCS_F = 20, TILT = 20 * RAD, YAW_TILT = 5 * RAD;
		// (2026-10-16) the impeller pack «Вихрь» for Earth and the like (JETPACK_FAN.md): a turboshaft on the same metallic
		// hydrogen (30 % of 216 MJ/kg on the shaft) drives two ducted fans D0.55 m. A fan's static thrust from its power:
		// T = (FM * P * sqrt(2 rho A))^(2/3) - 1.38 kN at sea level on 90 kW, nothing in vacuum; no reverse thrust; the
		// rings tilt 35 deg fore and aft, 12 deg aside; the thrust follows the throttle as the rotors spool (0.7 s / 0.5 s)
		enum PackModel { SHOULDER = 0, FAN = 1, TREFOIL = 2, HEXA = 3 };
		// v2 (the user: it must fly on Earth, the upper Venus, Titan, Mars): D0.50 - the smallest disc the 90 kW lift her
		// on at sea level; a rocket boost in each hub (the same hydrogen, 10 km/s, 400 N) takes what the fans cannot give
		// at once - the spool-up, thin air (Mars: the fans 2 x 0.3 kN, the boost the rest), vacuum (the boost alone);
		// hot air (the deep Venus) starves the turbine: full power to 400 K, none at 480 K
		// v3 (the user: closer to the real thing, with the materials of the future): the dry mass by its units (JETPACK_FAN.md)
		// - a 220 kW turboshaft at 10 kW/kg, the gearboxes, the rotors, the ducts, the gimbals, the booms, the tank, the
		// boost, the ballistic parachute: 87 kg; 110 kW a fan. A fan's thrust is also held by its blade tips (0.85 of the
		// speed of sound, thrust coefficient 0.10): in the Martian air the fans give ~12 N - there the boost (600 N a hub)
		// alone lifts her, for minutes
		static constexpr double FAN_DRY = 65, FAN_FUEL = 20, FAN_D = 0.553, FAN_FM = 0.65, FAN_P = 110e3, FAN_TMAX = 2400,
			FAN_ETA = 0.30, MH_E = 216e6, FAN_IDLE = 1.5e3, FAN_UP = 0.15, FAN_DOWN = 0.12, FAN_TILT = 30 * RAD, FAN_SIDE = 10 * RAD,
			FAN_BOOST = 600, FAN_TLIM = 400, FAN_TCUT = 480, FAN_CT = 0.10, FAN_TIP_M = 0.85;
		// (2026-10-16) the experimental modular pack «Трилистник» (TREFOIL, the user: «пробуем ввести трилистник для тестов,
		// старый не менять»): «Наплечный»'s own core, an arm a side carrying a gondola - its capsule pod with three hex cells
		// round it (the atmo kit) - and the kit's 300 kW power unit under the core. The same physics as «Соты» with its own
		// numbers (Tantra_Design/tools/jetpack_modular_check.py, sketch 8b): a side's three cells are an equivalent disc of
		// 0.122 m2 (D 0.394) on 150 kW; the boost is the capsule (500 N, the base pod); 74 kg dry (the base 18 + the kit 56),
		// 32 kg of hydrogen (12 + 20); the arms tilt 35 deg, twist 15 deg; folded, the gondolas hang down along her back
		struct AirSpec { double dry, fuel, D, FM, P, TMAX, BOOST, TILT, SIDE; VECTOR3 pmi; const char* cls; const char* geo; const char* worn; };
		static const AirSpec& Spec(int m);
		const AirSpec& S() const { return Spec(model); }
		bool Air() const { return model == FAN || model == TREFOIL; }   // an atmospheric pack: cells and a power unit, the boost on top
		// (2026-10-18) «Наплечный» M1 «Гекса» (HEXA): M1's rocket physics in the user's finished design - the hexagonal body, the
		// booms in three telescopic stages on screw hinges (the fold: the telescope in, then the swing back lifting the boom
		// 9 cm), the twin-chamber capsules (2 x 250 N); in air the «форсаж» (Shift): the same jet power at 5 km/s, 2 x 500 N a
		// capsule. The capsules' nozzles glow by their temperature (heats in ~2 s of thrust, cools in ~15 s)
		bool HasKit() const { return model != SHOULDER; }   // drawn as its own mesh (PoseFan), not the suit figure's Jet parts
		bool Forsazh() const { return model == HEXA && forsazh; }
		// the chambers' state for the helmet display and the scenario: bit (2*capsule + chamber) * 2 damaged, +1 overheated
		int ChamberFault() const;
		int DamageMask() const { int m = 0; for (int i = 0; i < 2; ++i) for (int c = 0; c < 2; ++c) if (chDamaged[i][c]) m |= 1 << (2 * i + c); return m; }
		void SetDamageMask(int m) { for (int i = 0; i < 2; ++i) for (int c = 0; c < 2; ++c) chDamaged[i][c] = (m >> (2 * i + c)) & 1; }
		static constexpr double HEXA_DRY = 18, HEXA_FUEL = 13, HEXA_TMAX = 1850, HEXA_UP = 2.0, HEXA_DOWN = 15.0;
		int GetModel() const { return model; }
		// for the sounds and the helmet display: the turbine running (booms out, fuel, air for the fans); the rotors' speed
		// (0..1 of full); the rocket share (the hubs' boost - or the light pack's pods); the turbine's power share left by
		// the heat; the share of the full thrust the fans can give here (the rest is the boost's)
		bool TurbineOn() const { return worn && Air() && deploy > 0.02 && Fuel() > 0; }   // turning (starting, idle, running, stopping)
		bool TurbineReady() const { return worn && Air() && deploy > 0.98 && Fuel() > 0; }   // up to idle: thrust can be asked
		bool TurbineCmd() const { return manualDeploy; }
		bool BoostOnly() const { return worn && Air() && rho < 1e-3; }   // no air: the boost lifts her, no turbine needed
		double SpdMax() const { return Air() ? 28.0 : 40.0; }   // the cruise's top ground speed, m/s (the fans: their ram drag)
		// (the rotors turn at idle - 12 % - while the turbine runs, and run down with it: heard and shown; the thrust is
		// the spooled part above that)
		double FanLevel() const { return worn && Air() ? (std::max)(0.5 * (fanLvl[0] + fanLvl[1]), 0.12 * deploy) : 0.0; }
		double RocketLevel() const { return !worn ? 0.0 : Air() ? 0.5 * (boostLvl[0] + boostLvl[1]) : 0.5 * (outLvl[0] + outLvl[1]); }
		double HeatShare() const { return heatShare; }
		double FanAuthority() const { return pf > 1 && Air() ? pfFan / pf : 0.0; }
		double AirDensity() const { return rho; }
		const char* VesselClass() const { return HasKit() ? S().cls : "OrbiterCrew\\JetPack"; }
		double DryMass() const { return HasKit() ? S().dry : DRY; }
		double FuelMax() const { return HasKit() ? S().fuel : FUEL; }
		void KeepModel(int m) { model = m == FAN ? FAN : m == TREFOIL ? TREFOIL : m == HEXA ? HEXA : SHOULDER; }   // the model as the scenario gives it (before Wear)
		// (2026-10-16) the honeycomb «Соты» in place of the impellers (the user's choice of five): FAN_D is now the equivalent
		// disc of one wing's 35 cells (0.24 m2 of rotor discs), FM 0.65 for the small cells, their rotors answer in 0.15 s.
		// The energy per kilogram: the metallic hydrogen's relaxation heat to a generator (30 %) and, where the air has
		// oxygen, the hydrogen then to a fuel cell (60 %) - through the motors (95 %): 130 MJ/kg on Earth, 62 elsewhere
		double FanEnergy() const { return ((steamOn ? 0.60 * 120e6 : 0.0) + FAN_ETA * MH_E) * 0.95; }
		double FanThrust(double rho, double powerShare = 1.0, double pressure = 0.0) const;   // one fan's full static thrust here, N
		double FanPower(double thrust, double rho) const;   // the shaft power one fan needs for that thrust, W
		// the impeller pack's own mesh on her back (the suit's figure, bone Spine1): booms, rings, spinning rotors
		void PoseFan(const Skin& skin, VISHANDLE vis);

		void Setup(VESSEL4* vessel);
		// the dust the pods blow off a surface, emitted at her feet (feetY: the soles in the model frame)
		void SetupDust(double feetY);
		// hydrogen burns to steam only where the air has oxygen: the steam trails come and go with it
		void SetOxygen(bool o2);
		void SetTitan(bool t) { titan = t; }
		void GroundUnder(int& gnd, double& wet) const;   // the ground class under her for the jet's ejecta (AtmoFXJet)   // cold dense air without oxygen: the slow low dust (titan_dust)
		bool Worn() const { return worn; }
		double Fuel() const;
		void Wear(double fuel, int model = SHOULDER);   // the pack becomes part of her
		double Remove();                  // returns the fuel left in it
		// the pack is an item of the person: off a body it keeps its fuel itself; on a body the body's tank holds it
		void Detach();                    // the body leaves the world: keep the fuel, let go of the body's parts
		void Keep(double fuel) { kept = fuel; keptKnown = true; }   // the fuel as the scenario gives it
		bool FuelKnown() const { return keptKnown; }
		double Kept() const { return kept; }
		// autopilots, pod vectoring and booms; canFly: suited, conscious, powered
		void Update(double dt, bool landed, bool canFly, double g, double altFeet, const FlightInput& in);
		void Touchdown();                 // she is back on her feet: throttle closed, autopilot off
		bool SurfaceMode() const { return surfaceMode; }
		// the pack's parts on her figure follow the booms and the pods
		void Pose(Skin& skin) const;
		bool Key(DWORD key, bool landed);
		// her own throttle for the pods (numpad 0 / . while held); manual input takes over from the autopilot
		void Throttle(double delta);
		double DeltaV() const;
		std::string Hud() const;
		void Fill(HudData& d) const;       // the suit computer's jet pack page
		// the autopilot's levers: the height hold, the landing, hands off
		int ModeId() const { return static_cast<int>(mode); }   // 0 manual, 1 height hold, 2 landing, 3 autopilot vector
		// the autopilot's hop: thrust acceleration wanted this step, m/s^2, horizon frame (x east, y up, z north);
		// to be given every step - left without it, the pack falls back to holding the height
		void SetThrustVector(const VECTOR3& aH) { if (!worn) return; mode = VECTOR; vecA = aH; vecFresh = true; lockout = false; assist = true; }
		double MaxAccel() const;   // full thrust / her mass
		// a command from the computer: the touchdown lockout (waiting for the thrust keys) does not apply to it
		void SetHold(double alt) { if (worn && surfaceMode) { if (mode != HOLD) spdSet = false; mode = HOLD; altTarget = alt; lockout = false; } }
		// the cruise: move the held height and the held ground speed (forward, along her heading); stop: speed 0
		void Nudge(double dAlt, double dSpd, bool stop);
		double SpdTarget() const { return spdTarget; }
		// full stop: kill every speed, keep the height she is at
		void Stop();
		bool Braking() const { return braking; }
		void SetDescent() { if (worn) { mode = DESCENT; pinned = false; lockout = false; } }

		// land at this very point (the autopilot's goal), not where the drift would stop
		void SetDescentAt(double lat, double lng) { if (worn) { mode = DESCENT; landSet = true; pinned = true; landLat = lat; landLng = lng; lockout = false; } }
		// (a plain landing has no point: it comes straight down and kills the drift only as the ground comes near)
		void SetManual() { mode = MANUAL; }
		double AltTarget() const { return altTarget; }
		double AltNow() const { return tAlt; }
		double MaxTilt() const { return tMaxTilt; }
		// manual (default): Space / Ctrl raise and lower the throttle while held (relative), as numpad 0 / . do; no compensation.
		// the assistant: Space / Ctrl climb and sink at safe rates and then hold the height, thrust capped to safe values,
		// the body held upright, the ground protection on. The autopilots and J / C switch the assistant on.
		// In both: Shift+Space = full thrust while held, Shift+Ctrl = thrust off while held.
		// the limiter (Caps Lock, the computer's button): fine control as Orbiter's Ctrl does for the RCS - the steering,
		// the turn and the rates of the hand controls at a tenth; the lift and the assistant's safety stay full
		void SetFine(bool on) { fine = on; }
		bool Fine() const { return fine; }
		bool Assist() const { return assist; }
		void SetAssist(bool a) { assist = a; if (!a) mode = MANUAL; }
		const RcsSet& Rcs() const { return rcs; }
		PROPELLANT_HANDLE Propellant() const { return prop; }
		double Deploy() const { return deploy; }
		// thrust the pods give now (N): booms locked and throttle open
		// (the fans: what the spooled rotors give, not what was asked - a lift-off before they are up falls back at once and
		// the touchdown's lockout shut them down)
		// (the fans: the lift-off waits for the spooled rotors themselves - the boost alone lifted her a few cm, the ground
		// check set her down, the touchdown stopped the rotors: she hopped in place on the boost, burning fuel)
		double Lift() const { return worn && deploy > 0.98 && Fuel() > 0 ? (Air() ? (std::min)(lift, liftNow) : lift) : 0.0; }   // up along her vertical, N
		double LiftoffMargin() const { return Air() ? 1.15 : 1.02; }   // the lift over her weight that lifts her off
		double Thrust() const { return worn && deploy > 0.98 && Fuel() > 0 ? pf * (outLvl[0] + outLvl[1]) : 0.0; }   // both pods, N
		VECTOR3 ShellCentre() const { return HasKit() ? fanG().shellC : shellC; }
		double ShellHalfHeight() const { return (HasKit() ? fanG().shellSize.y : shellSize.y) / 2; }

	private:
		bool LoadGeo();
		void PodStreams(bool on);         // the pods' plumes: the rocket pack's, not the fans'
		int model{ SHOULDER };
		double pf{ POD_F };               // what one pod / fan can push now, N (the fans: by the air)
		double rho{};                     // the air's density here, kg/m^3
		double outLvl[2]{};               // the level each pod gives (the fans: spooling after podLvl)
		double fanFlow{};                 // the pack's fuel flow now, kg/s (the turbine's and the boost's)
		double pfFan{}, fanLvl[2]{}, boostLvl[2]{}, heatShare{ 1 };
		double liftNow{}, glow{};          // glow: the aerospikes' ramps, heated by the boost (drawn)                 // the fans: what lifts her now - the rotors, and the boost once they are up to their share   // the fans' own part (spooling), the boost's (at once), the turbine's power share
		PSTREAM_HANDLE boostPs[2]{};
		// the boost's flame: hydrogen burning in the air round the jet - a short pale blue tongue, all but invisible by day,
		// fainter still in vacuum (an Orbiter exhaust: level, position and direction follow the ring)
		UINT boostEx[2]{}; double boostExLvl[2]{}; VECTOR3 boostExPos[2]{}, boostExDir[2]{};
		std::vector<PSTREAM_HANDLE> rcsVac; std::vector<VECTOR3> rcsPos;   // the attitude ports' puffs (the light pack's: the fans' pack steers by its vanes)
		void RcsStreams(bool on);
		double spin[2]{};                 // the rotors' angles (drawn)
		PSTREAM_HANDLE podVac[2]{};
		struct FanGeo
		{
			bool ok{};
			VECTOR3 shellC{ 0, 0.34, -0.265 }, shellSize{ 0.46, 0.56, 0.30 }, hinge[2]{}, pivot[2]{};
			double rotorY{ 0.47 }, fold{ 0 }, boostY{ 0.24 }; bool foldZ{}; int glowMat{ -1 };
			VECTOR3 foldShift{};   // folded, the wing's parts also move by this (the trefoil: the gondolas back behind her)
			// KIN 2 (the trefoil v2, 2026-10-17): a chain of real joints, no translations - the root joint turns the arm and its
			// gondola about the arm's own axis (hinge -> pivot: the vector's pitch, and twist to stow), then swings them about
			// the fold axis foldU (the right side's; the left mirrors it); the rotors spin and the exit vanes turn about their own
			// centres and axes (gC, gA; gDir: the rotor's sense)
			int kin{ 1 }; double twist{}, foldA{}; VECTOR3 foldU{ 0, 1, 0 };
			std::vector<VECTOR3> gC, gA; std::vector<int> gDir;
			// KIN 3 (the Гекса): the telescope's stroke, the screw hinge's lift and swing, the capsules' glowing materials
			double stroke{}, lift{}, swing{ 90 * RAD }; int glowPod[4]{ -1, -1, -1, -1 };   // foldZ: the wings fold about the fore-aft axis
			std::vector<int> role, side;  // per group of the worn mesh: 0 core, 1 boom, 2 ring, 3 rotor; 0 her right, 1 left, -1 none
		};
		// each air pack's worn mesh (its groups at rest, her model frame) and geometry: [0] «Соты», [1] «Трилистник»
		struct AirKit { FanGeo geo; MESHHANDLE tpl{}; UINT mesh{ static_cast<UINT>(-1) }; std::vector<std::vector<NTVERTEX>> vtx; } kits[3];
		int K() const { return model == TREFOIL ? 1 : model == HEXA ? 2 : 0; }
		const FanGeo& fanG() const { return kits[K()].geo; }
		bool LoadFanGeo(const char* file, FanGeo& fan);
		VESSEL4* v{};
		bool worn{};
		PROPELLANT_HANDLE prop{};
		double kept{}; bool keptKnown{};
		bool fine{};    // fuel while off a body (or from the scenario), kg
		THRUSTER_HANDLE pod[2]{};         // [0] on +x (her right), [1] on -x
		RcsSet rcs;
		VECTOR3 hinge[2]{}, pivot[2]{}, shellC{}, shellSize{};
		double exitDy{ -0.15 };
		double deploy{}, deployTarget{}, idleT{}, tilt[2]{}, side{}, level{}, cmd{};   // side: sideways gimbal angle wanted (strafe)
		double lift{}, tiltBase{};   // tiltBase: the pods' common swing angle   // the vertical force asked of both pods, N (signed)
		double sidePod[2]{}, podLvl[2]{};   // each pod's own sideways angle (only outwards: the jet never crosses her) and level
		bool manualDeploy{}, assist{ false };
		enum Mode { MANUAL, HOLD, DESCENT, VECTOR } mode{ MANUAL };
		VECTOR3 vecA{}; bool vecFresh{};   // VECTOR: the thrust acceleration wanted this step (x east, y up, z north)
		double vTarget{ 1.0 };
		bool surfaceMode{ true }, prevValid{}, prevSpaceKeys{};
		double sgnF{ -1 }, sgnR{ 1 }, sgnY{ 1 };   // body rate -> lean forward / lean right / heading rate (learned upright)
		double prevLeanF{}, prevLeanR{}, prevHdg{}, leanFRate{}, leanRRate{}, yawRate{};
		// telemetry for the helmet display
		double tLeanF{}, tLeanR{}, tWantF{}, tWantR{}, tVs{}, tVsCmd{}, tAlt{}, tGs{};
		bool tFlying{}, tBoost{}, tProtect{}, tTerrain{}, tLimited{}, holdingAttitude{};
		double altTarget{ 1.5 }, tMaxTilt{ 30 * RAD }, tUpright{ 1 }, tHover{};
		// the landing: the point she lands on (where she can stop when C was pressed), set on entering the landing
		bool landSet{}, pinned{}; double landLat{}, landLng{};
		struct Steam { THRUSTER_HANDLE th; VECTOR3 pos; PARTICLESTREAMSPEC* spec; PSTREAM_HANDLE h; };
		std::vector<Steam> steam;
		bool steamOn{ true };
		bool lockout{}, released{};
		double spdTarget{}, tVf{}; bool spdSet{}, braking{};   // the cruise: held forward ground speed; her forward speed now
		// hot nozzles: a glow at each pod's exit, following the heat of the chamber
		double heat[2]{}; bool forsazh{};
		// the Гекса's hydrogen exhaust, out of each capsule's exit along its gimbal: [0] vacuum (a faint violet-blue core),
		// [1] air (the pale blue flame with the shock diamonds), [2] air on the «форсаж» (Textures\Tantra\h2*.dds)
		UINT hxEnd[2][4]{}; double hxEndLvl[2][4]{};   // the jet seen end-on (the client's camera-facing flare quad)
		UINT hxEx[2][4]{}; double hxLvl[2][4]{}; VECTOR3 hxPos[2]{}, hxDir[2]{};   // [3]: dense air without oxygen (Titan, Venus)
		// the twin chambers [capsule][1 outboard, 2 inboard]: damaged (until repaired), overheated (off until the wall cools),
		// the wall's load toward overheating, the glow's heat
		bool chDamaged[2][2]{}, chHot[2][2]{}; double chLoad[2][2]{}, chHeat[2][2]{}, lastVs{};   // after a touchdown the pods stay off until the thrust keys are let go and pressed anew
		double dustLevel{}; PSTREAM_HANDLE dust{};
		double dustTLevel{}; PSTREAM_HANDLE dustT{}; bool titan{};
		double heatDT[2]{};   // the hot nozzles' last heat node sent to the AtmoFX client (K over the air)
	};
}
