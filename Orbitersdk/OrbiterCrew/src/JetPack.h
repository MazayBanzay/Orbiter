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

		void Setup(VESSEL4* vessel);
		// the dust the pods blow off a surface, emitted at her feet (feetY: the soles in the model frame)
		void SetupDust(double feetY);
		// hydrogen burns to steam only where the air has oxygen: the steam trails come and go with it
		void SetOxygen(bool o2);
		bool Worn() const { return worn; }
		double Fuel() const;
		void Wear(double fuel);           // the pack becomes part of her
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
		double Lift() const { return worn && deploy > 0.98 && Fuel() > 0 ? lift : 0.0; }   // up along her vertical, N
		double Thrust() const { return worn && deploy > 0.98 && Fuel() > 0 ? POD_F * (podLvl[0] + podLvl[1]) : 0.0; }   // both pods, N
		VECTOR3 ShellCentre() const { return shellC; }
		double ShellHalfHeight() const { return shellSize.y / 2; }

	private:
		bool LoadGeo();
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
		double heat[2]{};   // after a touchdown the pods stay off until the thrust keys are let go and pressed anew
		double dustLevel{}; PSTREAM_HANDLE dust{};
	};
}
