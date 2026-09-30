// OrbiterCrew - the suit computer's autopilots.
// Near a surface they fly the jet pack through the same inputs the pilot uses (pod vector, sideways gimbal, turn)
// while the pack's height hold keeps the height: HOVER kills the drift, TRANSFER flies to the selected target and
// lands beside it. In space they drive the suit's (or the pack's) RCS directly: SYNC matches the target's velocity,
// HOLD keeps the place relative to it, APPROACH closes in along a speed corridor, DOCK comes in along a port's axis.
// Manual input on the same axes takes the controls back.
#pragma once
#include "JetPack.h"
#include <string>
#include <map>

namespace ocrew
{
	class Autopilot
	{
	public:
		enum Mode { OFF, HOVER, TRANSFER, SYNC, HOLD, APPROACH, DOCK };
		static const char* Name(int m);

		void Engage(Mode m, OBJHANDLE target, VESSEL* v);   // the same mode again switches it off
		void Off(VESSEL* v, const char* why = nullptr);
		Mode Get() const { return mode; }
		OBJHANDLE Target() const { return tgt; }
		const std::string& Status() const { return status; }
		VECTOR3 CmdHorizon() const { return cmdH; }          // commanded acceleration, horizon frame (for the map)

		// manual: the pilot is steering on the autopilot's axes this step (takes over)
		void Step(VESSEL* v, JetPack& jet, double dt, double g, bool landed, bool canFly, bool manual, FlightInput& in);

	private:
		void Space(VESSEL* v, double dt);
		void Surface(VESSEL* v, JetPack& jet, double dt, double g, FlightInput& in);
		void Drive(VESSEL* v, const VECTOR3& aBody, const VECTOR3& rot);   // rot: pitch-up, yaw-right, roll-right (-1..1)
		void Zero(VESSEL* v);
		Mode mode{ OFF };
		OBJHANDLE tgt{};
		std::string status;
		VECTOR3 holdRel{}, cmdH{};
		double prevErr[3]{}, errRate[3]{}, prevHerr{}, herrRate{};
		bool prevOk{}, driving{};
	};
}
