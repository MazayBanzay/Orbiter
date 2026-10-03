// OrbiterCrew - the body animator.
// Mocap clips (idle / walk / run) are blended by ground speed; the gait phase follows the distance travelled so
// the feet do not skate. On top of the clips procedural layers keep the figure alive: breathing driven by the
// physiology, small weight shifts and glances at rest, a lean into acceleration and braking, a bank into turns,
// and a crouch for jumps and landings. Leans are critically damped springs, so nothing snaps or overshoots.
#pragma once
#include "Skin.h"
#include <random>

namespace ocrew
{
	struct ClipSet
	{
		Clip idle, walk, run;
		Clip sitDown, sit, standUp;      // optional (a seat in a ship): sit_down.clip, sit.clip, stand_up.clip
		bool seats{};
		bool Load(const std::string& clipDir);
	};

	struct MotionInput
	{
		double dt{};
		double fwd{}, lat{}, turn{};   // ground speed along the heading and to the right (m/s), turn rate (rad/s, + = right)
		double accel{};                // forward acceleration commanded by the legs (m/s^2)
		double g{ 9.81 };
		bool grounded{ true };         // on the surface (false while in the air after a jump)
		bool lying{};                  // down on the ground (fallen, unconscious, dead): no tuck of the legs
		double landing{};              // vertical speed at touchdown this step (m/s), else 0
		double effort{};               // 0..1 metabolic load
		double fatigue{};              // 0..1
		double breathRate{ 13 };       // breaths per minute
		double walkTop{};              // top walking speed (no Shift), m/s; coverall: pure walk clip up to it
		bool suited{};                 // space suit: arms out, wider stance, heavy footfall
		double bound{};                // 0..1 servo boost bounding
		bool floating{};               // free fall in space (not a jump): neutral body posture
		VECTOR3 thrustAcc{};           // acceleration from thrusters, vessel frame (m/s^2): the limbs lag behind it
		VECTOR3 angAcc{};              // angular acceleration, vessel frame (rad/s^2): turning swings the limbs too
		bool jet{};                    // wearing the jet pack: hands forward, clear of the pods' jets
		VECTOR3 angVel{};              // angular velocity, vessel frame (rad/s): limbs lag behind the turning
		int seat{};                    // 0 standing, 1 sitting down, 2 seated, 3 standing up
		double seatT{};                // 0..1 through sitting down / standing up
		double heading{};              // rad, + to the right: the whole figure turned about the vertical through the origin
		                               // (inside a ship the vessel keeps the ship's orientation; she turns in her pose)
	};

	class Motion
	{
	public:
		Motion();
		// build the pose for this step and deform the skin
		void Update(const MotionInput& in, const ClipSet& clips, Skin& skin);
		int Footfalls() const { return footfalls; }   // foot contacts in the last Update
		double RunWeight() const { return wRun; }
		void Reset() { wMove = wRun = 0; pitch = pitchV = roll = rollV = crouch = crouchV = 0; settling = false; }

	private:
		struct Spring { double x{}, v{}; void Step(double target, double w, double dt); };

		void Bind(const Skin& skin);
		double Noise(int ch, double t) const;

		const Skin* bound{};
		int bHips{ -1 }, bLowerBack{ -1 }, bSpine{ -1 }, bSpine1{ -1 }, bNeck1{ -1 }, bHead{ -1 };
		int bLShoulder{ -1 }, bRShoulder{ -1 }, bLArm{ -1 }, bRArm{ -1 };
		int bLFingers{ -1 }, bRFingers{ -1 }, bLForeArm{ -1 }, bRForeArm{ -1 }, bLHand{ -1 }, bRHand{ -1 }, bLFingerBase{ -1 }, bRFingerBase{ -1 }, bLThumb{ -1 }, bRThumb{ -1 };
		int bLUpLeg{ -1 }, bRUpLeg{ -1 }, bLLeg{ -1 }, bRLeg{ -1 }, bLFoot{ -1 }, bRFoot{ -1 };

		double phase{}, wMove{}, wRun{}, lastRate{}, dir{ 1 };
		bool settling{};
		int footfalls{};
		double settleTarget{};
		double strideVar{ 1 }, strideVarTarget{ 1 };
		double time{}, breathPhase{};
		double pitch{}, pitchV{}, roll{}, rollV{}, crouch{}, crouchV{}, tuck{}, headYaw{};
		double suitW{}, bounce{};
		double floatW{}, swayX{}, swayXV{}, swayZ{}, swayZV{}, swayY{}, swayYV{};
		double hangW{}, jetW{};
		double seeds[12]{};
		double lyingW{};                       // 0 .. 1: down on the ground, limp
		double blinkIn{ 2.5 }, blinkT{ -1 };   // seconds to the next blink; time into the current one (-1: eyes open)
		std::mt19937 rng;
		double seatW{};                        // 0 .. 1: the seat clips over the standing ones
		Pose pIdle, pWalk, pRun, pLoco, pOut, pSeat;
	};
}
