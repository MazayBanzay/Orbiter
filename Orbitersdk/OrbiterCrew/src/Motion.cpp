// OrbiterCrew - the body animator (see Motion.h).
#include "Motion.h"
#include <algorithm>
#include <cmath>

namespace ocrew
{
	namespace
	{
		const VECTOR3 AX_LAT{ 1, 0, 0 };   // model x: to the right; + angle pitches the top forward
		const VECTOR3 AX_UP{ 0, 1, 0 };    // model y: up; + angle turns the face to the right
		const VECTOR3 AX_FWD{ 0, 0, 1 };   // model z: forward; + angle rolls the top to the left
		const VECTOR3 SOLES{ 0, -0.93, 0 };
		const double THIGH = 0.45, SHIN = 0.43;

		double Smooth(double e0, double e1, double x) { const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0); return t * t * (3 - 2 * t); }
		double Lerp(double a, double b, double t) { return a + (b - a) * t; }
		double Follow(double x, double target, double tau, double dt) { return x + (target - x) * (1 - std::exp(-dt / tau)); }
		float F(double x) { return static_cast<float>(x); }
	}

	bool ClipSet::Load(const std::string& clipDir)
	{
		const std::string dir = "Config\\" + clipDir + "\\";
		const bool ok = idle.Load(dir + "idle.clip") && walk.Load(dir + "walk.clip") && run.Load(dir + "run.clip");
		if (!ok) oapiWriteLogV("OrbiterCrew: cannot load the clips in Config\\%s", clipDir.c_str());
		return ok;
	}

	void Motion::Spring::Step(double target, double w, double dt)
	{
		// critically damped: reaches the target in about 4/w seconds without overshoot; substeps keep it stable at any dt
		const int n = (std::max)(1, static_cast<int>(std::ceil(dt * w / 0.2)));
		const double h = dt / n;
		for (int i = 0; i < n; ++i) { v += (w * w * (target - x) - 2 * w * v) * h; x += v * h; }
	}

	Motion::Motion() : rng(std::random_device{}())
	{
		std::uniform_real_distribution<double> u(0, 6.283);
		for (double& s : seeds) s = u(rng);
	}

	double Motion::Noise(int ch, double t) const
	{
		// smooth pseudo-random signal in about -1..1: incommensurate sines with random phases per channel
		const double* s = &seeds[(ch * 3) % 12];
		return (std::sin(t + s[0]) + 0.6 * std::sin(2.31 * t + s[1]) + 0.3 * std::sin(4.67 * t + s[2])) / 1.9;
	}

	void Motion::Bind(const Skin& skin)
	{
		bound = &skin;
		bHips = skin.Bone("Hips"); bLowerBack = skin.Bone("LowerBack"); bSpine = skin.Bone("Spine"); bSpine1 = skin.Bone("Spine1");
		bNeck1 = skin.Bone("Neck1"); bHead = skin.Bone("Head");
		bLShoulder = skin.Bone("LeftShoulder"); bRShoulder = skin.Bone("RightShoulder"); bLArm = skin.Bone("LeftArm"); bRArm = skin.Bone("RightArm");
		bLUpLeg = skin.Bone("LeftUpLeg"); bRUpLeg = skin.Bone("RightUpLeg"); bLLeg = skin.Bone("LeftLeg"); bRLeg = skin.Bone("RightLeg");
		bLFoot = skin.Bone("LeftFoot"); bRFoot = skin.Bone("RightFoot");
	}

	void Motion::Update(const MotionInput& in, const ClipSet& clips, Skin& skin)
	{
		const double dt = in.dt;
		if (dt <= 0 || !skin.Loaded()) return;
		if (bound != &skin) Bind(skin);
		time += dt;
		const Clip& walk = clips.walk; const Clip& run = clips.run;

		// ---- gait: blend weights and phase ----
		const double speed = in.grounded ? std::abs(in.fwd) + 0.5 * std::abs(in.lat) + 0.3 * std::abs(in.turn) : 0;
		if (std::abs(in.fwd) > 0.05) dir = in.fwd < 0 ? -1 : 1;
		const double tMove = in.grounded ? Smooth(0.03, 0.6 * walk.speed, speed) : wMove;
		const double tRun = Smooth(1.3 * walk.speed, 0.9 * run.speed, speed);
		wMove = Follow(wMove, tMove, tMove > wMove ? 0.12 : 0.22, dt);
		wRun = Follow(wRun, tRun, 0.30, dt);

		strideVar = Follow(strideVar, strideVarTarget, 0.4, dt);
		const double vref = Lerp(walk.speed, run.speed, wRun);
		const double stride = Lerp(walk.stride, run.stride, wRun) * (speed > vref ? std::sqrt(speed / vref) : 1.0) * strideVar;
		const double effStride = stride * (std::max)(0.35, wMove);
		const double before = phase;

		if (in.grounded && speed > 0.03)
		{
			settling = false;
			lastRate = speed / effStride;
			phase += dir * lastRate * dt;
		}
		else if (in.grounded && wMove > 0.01)
		{
			// stopping: finish the current step into the next double support instead of freezing mid-stride
			if (!settling) { settling = true; settleTarget = dir > 0 ? (phase < 0.5 ? 0.5 : 1.0) : (phase > 0.5 ? 0.5 : 0.0); }
			const double rate = (std::max)(lastRate * (0.4 + 0.6 * wMove), 0.5);
			phase = dir > 0 ? (std::min)(phase + rate * dt, settleTarget) : (std::max)(phase - rate * dt, settleTarget);
		}
		phase -= std::floor(phase);
		// a new step: pick a slightly different stride so no two steps are identical
		if (std::floor(before * 2) != std::floor(phase * 2) && !settling)
			strideVarTarget = std::uniform_real_distribution<double>(0.965, 1.035)(rng);

		clips.idle.Sample(0, pIdle);
		walk.Sample(phase, pWalk);
		run.Sample(phase, pRun);
		BlendPose(pWalk, pRun, F(wRun), pLoco);
		BlendPose(pIdle, pLoco, F(wMove), pOut);

		const double rest = 1 - wMove;

		// ---- breathing: chest and shoulders; deeper with effort and fatigue ----
		breathPhase += 6.2832 * in.breathRate / 60.0 * dt;
		const double b = std::sin(breathPhase);
		const double depth = 0.008 + 0.022 * in.effort + 0.02 * in.fatigue;
		skin.Turn(pOut, bSpine1, AX_LAT, F(-depth * b));
		skin.Turn(pOut, bLShoulder, AX_FWD, F(-0.5 * depth * b));
		skin.Turn(pOut, bRShoulder, AX_FWD, F(0.5 * depth * b));

		// ---- life at rest: weight shifts, glances, arms never perfectly still; slumps when exhausted ----
		skin.TurnAll(pOut, SOLES, AX_FWD, F(rest * 0.010 * Noise(0, time * 0.23)));
		skin.TurnAll(pOut, SOLES, AX_LAT, F(rest * 0.006 * Noise(1, time * 0.17)));
		skin.Turn(pOut, bLowerBack, AX_UP, F(rest * 0.03 * Noise(2, time * 0.21) + wMove * 0.02 * Noise(2, time * 1.3)));
		skin.Turn(pOut, bLArm, AX_LAT, F(rest * 0.035 * Noise(3, time * 0.31)));
		skin.Turn(pOut, bRArm, AX_LAT, F(rest * 0.035 * Noise(0, time * 0.29 + 7)));
		skin.Turn(pOut, bLowerBack, AX_LAT, F(rest * 0.12 * in.fatigue));

		// the head looks around at rest and into turns while moving
		headYaw = Follow(headYaw, std::clamp(0.35 * in.turn, -0.35, 0.35), 0.25, dt);
		skin.Turn(pOut, bNeck1, AX_UP, F(headYaw + rest * 0.22 * Noise(1, time * 0.12) + wMove * 0.04 * Noise(1, time * 0.5)));
		skin.Turn(pOut, bHead, AX_LAT, F(rest * (0.05 * Noise(2, time * 0.19) + 0.10 * in.fatigue)));
		skin.Turn(pOut, bHead, AX_FWD, F(rest * 0.03 * Noise(3, time * 0.15)));

		// ---- crouch: absorbs landings, tucks the legs in the air ----
		if (in.landing > 0) crouchV += (std::min)(in.landing, 6.0) * 0.9;
		crouch = std::clamp(crouch + crouchV * dt, 0.0, 0.9);
		crouchV += (-81 * crouch - 12.6 * crouchV) * dt;   // w 9 rad/s, damping 0.7
		tuck = Follow(tuck, in.grounded ? 0.0 : 0.45, in.grounded ? 0.08 : 0.15, dt);
		const double knee = (std::max)(crouch, tuck);
		if (knee > 1e-3)
		{
			skin.Turn(pOut, bLUpLeg, AX_LAT, F(-knee)); skin.Turn(pOut, bLLeg, AX_LAT, F(2 * knee)); skin.Turn(pOut, bLFoot, AX_LAT, F(-knee));
			skin.Turn(pOut, bRUpLeg, AX_LAT, F(-knee)); skin.Turn(pOut, bRLeg, AX_LAT, F(2 * knee)); skin.Turn(pOut, bRFoot, AX_LAT, F(-knee));
			skin.Turn(pOut, bLowerBack, AX_LAT, F(0.5 * knee));
			if (in.grounded) skin.Shift(pOut, _V(0, -(THIGH + SHIN) * (1 - std::cos(crouch)), 0));   // the hips drop, the soles stay down
		}

		// ---- dynamic lean: only while the speed changes, upper body only; bank into turns ----
		// the clips already carry the natural posture of walking and running, so there is no standing tilt here;
		// the braking lean fades as the speed runs out, so she is upright when the feet stop (no nod after the stop)
		const double g = in.g > 0.1 ? in.g : 0.1;
		const double fade = in.accel * in.fwd < 0 ? (std::min)(1.0, std::abs(in.fwd) / 1.2) : 1.0;
		const double pitchTarget = std::clamp(0.35 * in.accel / g, -0.10, 0.12) * fade;
		const double rollTarget = std::clamp(-std::atan(in.fwd * in.turn / g), -0.25, 0.25) * 0.6;
		Spring ps{ pitch, pitchV }; ps.Step(in.grounded ? pitchTarget : 0.0, 8, dt); pitch = ps.x; pitchV = ps.v;
		Spring rs{ roll, rollV }; rs.Step(in.grounded ? rollTarget : 0.0, 7, dt); roll = rs.x; rollV = rs.v;
		skin.Turn(pOut, bLowerBack, AX_LAT, F(pitch));
		skin.TurnAll(pOut, SOLES, AX_FWD, F(roll));

		skin.Apply(pOut);
	}
}
