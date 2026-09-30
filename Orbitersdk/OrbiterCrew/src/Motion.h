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
		bool Load(const std::string& clipDir);
	};

	struct MotionInput
	{
		double dt{};
		double fwd{}, lat{}, turn{};   // ground speed along the heading and to the right (m/s), turn rate (rad/s, + = right)
		double accel{};                // forward acceleration commanded by the legs (m/s^2)
		double g{ 9.81 };
		bool grounded{ true };         // on the surface (false while in the air after a jump)
		double landing{};              // vertical speed at touchdown this step (m/s), else 0
		double effort{};               // 0..1 metabolic load
		double fatigue{};              // 0..1
		double breathRate{ 13 };       // breaths per minute
	};

	class Motion
	{
	public:
		Motion();
		// build the pose for this step and deform the skin
		void Update(const MotionInput& in, const ClipSet& clips, Skin& skin);
		void Reset() { wMove = wRun = 0; pitch = pitchV = roll = rollV = crouch = crouchV = 0; settling = false; }

	private:
		struct Spring { double x{}, v{}; void Step(double target, double w, double dt); };

		void Bind(const Skin& skin);
		double Noise(int ch, double t) const;

		const Skin* bound{};
		int bHips{ -1 }, bLowerBack{ -1 }, bSpine{ -1 }, bSpine1{ -1 }, bNeck1{ -1 }, bHead{ -1 };
		int bLShoulder{ -1 }, bRShoulder{ -1 }, bLArm{ -1 }, bRArm{ -1 };
		int bLUpLeg{ -1 }, bRUpLeg{ -1 }, bLLeg{ -1 }, bRLeg{ -1 }, bLFoot{ -1 }, bRFoot{ -1 };

		double phase{}, wMove{}, wRun{}, lastRate{}, dir{ 1 };
		bool settling{};
		double settleTarget{};
		double strideVar{ 1 }, strideVarTarget{ 1 };
		double time{}, breathPhase{};
		double pitch{}, pitchV{}, roll{}, rollV{}, crouch{}, crouchV{}, tuck{}, headYaw{};
		double seeds[12]{};
		std::mt19937 rng;
		Pose pIdle, pWalk, pRun, pLoco, pOut;
	};
}
