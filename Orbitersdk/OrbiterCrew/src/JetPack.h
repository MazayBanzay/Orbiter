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
	struct FlightInput { double pitch{}, yaw{}, roll{}, climb{}, throttle{}, strafe{}, vertical{}; bool boost{}; };

	struct RcsSet
	{
		THRUSTER_HANDLE th[2][2][6]{};   // [x side][y side][direction +x -x +y -y +z -z]
		std::vector<THRUSTER_HANDLE> all;
	};

	class JetPack
	{
	public:
		static constexpr double DRY = 16, FUEL = 12, ISP = 2900, POD_F = 150, RCS_F = 10, TILT = 20 * RAD;

		void Setup(VESSEL4* vessel);
		bool Worn() const { return worn; }
		double Fuel() const;
		void Wear(double fuel);           // the pack becomes part of her
		double Remove();                  // returns the fuel left in it
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
		const RcsSet& Rcs() const { return rcs; }
		PROPELLANT_HANDLE Propellant() const { return prop; }
		double Deploy() const { return deploy; }
		// thrust the pods give now (N): booms locked and throttle open
		double Thrust() const { return worn && deploy > 0.98 ? 2 * POD_F * level : 0.0; }
		VECTOR3 ShellCentre() const { return shellC; }
		double ShellHalfHeight() const { return shellSize.y / 2; }

	private:
		bool LoadGeo();
		VESSEL4* v{};
		bool worn{};
		PROPELLANT_HANDLE prop{};
		THRUSTER_HANDLE pod[2]{};         // [0] on +x (her right), [1] on -x
		RcsSet rcs;
		VECTOR3 hinge[2]{}, pivot[2]{}, shellC{}, shellSize{};
		double exitDy{ -0.15 };
		double deploy{}, deployTarget{}, idleT{}, tilt[2]{}, side{}, level{}, cmd{};   // side: sideways gimbal angle (strafe)
		bool manualDeploy{};
		enum Mode { MANUAL, HOLD, DESCENT } mode{ MANUAL };
		double vTarget{ 1.0 };
		bool surfaceMode{ true }, prevValid{}, prevSpaceKeys{};
		double prevLeanF{}, prevLeanR{}, prevHdg{}, leanFRate{}, leanRRate{}, yawRate{};
		// telemetry for the helmet display
		double tLeanF{}, tLeanR{}, tWantF{}, tWantR{}, tVs{}, tVsCmd{}, tAlt{}, tGs{};
		bool tFlying{}, tBoost{}, tProtect{}, tTerrain{}, tLimited{}, holdingAttitude{};
		double altTarget{ 1.5 }, tMaxTilt{ 30 * RAD };
	};
}
