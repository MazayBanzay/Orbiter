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
		bLFingers = skin.Bone("LeftHandFinger1"); bRFingers = skin.Bone("RightHandFinger1");
		bLForeArm = skin.Bone("LeftForeArm"); bRForeArm = skin.Bone("RightForeArm"); bLHand = skin.Bone("LeftHand"); bRHand = skin.Bone("RightHand");
		bLFingerBase = skin.Bone("LeftFingerBase"); bRFingerBase = skin.Bone("RightFingerBase"); bLThumb = skin.Bone("LThumb"); bRThumb = skin.Bone("RThumb");
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
		// suit: blend by the clips' own speeds (unchanged). Coverall: walking (no Shift) is the walk clip at any speed up
		// to the top walking speed; the run takes over only above it - never a half-walk, half-run mix while walking
		const double tRunSuit = Smooth(1.3 * walk.speed, 0.9 * run.speed, speed);
		const double top = in.walkTop > 0 ? in.walkTop : walk.speed;
		const double tRunCov = Smooth(1.04 * top, 1.04 * top + 0.6, speed);
		const double tRun = Lerp(tRunSuit, tRunCov, 1 - suitW);
		wMove = Follow(wMove, tMove, tMove > wMove ? 0.12 : 0.22, dt);
		wRun = Follow(wRun, tRun, 0.30, dt);

		strideVar = Follow(strideVar, strideVarTarget, 0.4, dt);
		const double vref = Lerp(walk.speed, run.speed, wRun);
		// stride ~ sqrt(speed) around the clip's own speed, as people do: faster means longer steps and quicker cadence.
		// Coverall: below the clip speed the step shortens too, instead of the same stride at a floating, slow cadence
		// (the suit keeps its full stride and its own layer)
		const double slower = Lerp(1.0, std::sqrt((std::max)(0.6, speed / vref)), 1 - suitW);
		// above the clip speed: suit sqrt (as before); coverall grows the stride faster (the Neutral run is a jog, and
		// people reach 5 m/s mostly with longer strides, not a frantic cadence)
		const double grow = speed > vref ? std::pow(speed / vref, Lerp(0.5, 0.72, 1 - suitW)) : slower;
		const double stride = Lerp(walk.stride, run.stride, wRun) * grow * strideVar;
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
		// a foot touches down at every half cycle (phase 0: left foot forward, 0.5: right), also on the last settling step
		footfalls = in.grounded && wMove > 0.2 && std::floor(before * 2) != std::floor(phase * 2) ? 1 : 0;
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
		skin.Turn(pOut, bNeck1, AX_UP, F(headYaw + rest * 0.22 * Noise(1, time * 0.12) + wMove * (1 - wRun * (1 - suitW)) * 0.04 * Noise(1, time * 0.5)));
		skin.Turn(pOut, bHead, AX_LAT, F(rest * (0.05 * Noise(2, time * 0.19) + 0.10 * in.fatigue)));
		skin.Turn(pOut, bHead, AX_FWD, F(rest * 0.03 * Noise(3, time * 0.15)));

		// ---- suit: arms held clear of the pack and struts, wider stance, a heavy footfall at every step ----
		suitW = Follow(suitW, in.suited ? 1.0 : 0.0, 0.5, dt);
		if (suitW > 1e-3)
		{
			skin.Turn(pOut, bLArm, AX_FWD, F(-0.13 * suitW)); skin.Turn(pOut, bRArm, AX_FWD, F(0.13 * suitW));     // left arm is on -x
			skin.Turn(pOut, bLUpLeg, AX_FWD, F(-0.035 * suitW)); skin.Turn(pOut, bRUpLeg, AX_FWD, F(0.035 * suitW));
			skin.Turn(pOut, bLFoot, AX_FWD, F(0.035 * suitW)); skin.Turn(pOut, bRFoot, AX_FWD, F(-0.035 * suitW));  // soles stay flat
			if (in.grounded && std::floor(before * 2) != std::floor(phase * 2) && !settling)
				crouchV += suitW * (0.25 + 0.12 * (std::min)(speed, 6.0));                                                 // the step lands: knees give
		}

		// ---- coverall: the suit's motion (same clips), lighter - she carries no suit: arms half as far out,
		//      a narrower stance, a softer knee at each footfall. Fades out in the suit, which has its own layer ----
		const double u = 1 - suitW;
		if (u > 1e-3)
		{
			// walking: arms by the body, an ordinary stance (the clean-up is in the walk clip); running: the suit's
			// arms-out and stance, halved
			const double arms = u * (0.01 + 0.05 * wRun), stance = u * 0.015 * wRun;
			skin.Turn(pOut, bLArm, AX_FWD, F(-arms)); skin.Turn(pOut, bRArm, AX_FWD, F(arms));                  // left arm is on -x
			skin.Turn(pOut, bLUpLeg, AX_FWD, F(-stance)); skin.Turn(pOut, bRUpLeg, AX_FWD, F(stance));
			skin.Turn(pOut, bLFoot, AX_FWD, F(stance)); skin.Turn(pOut, bRFoot, AX_FWD, F(-stance));            // soles stay flat
			if (in.grounded && std::floor(before * 2) != std::floor(phase * 2) && !settling)
				crouchV += u * (0.06 + wRun * (0.08 + 0.06 * (std::min)(speed, 6.0)));                          // the step lands: knees give
			// fingers: the mesh carries a relaxed hand (fingers together, softly curled); running closes it into a fist
			skin.SetMorph("fist", F(u * wRun));
			// hair on springs: driven by the body's own acceleration (speed changes, turns) and gravity
			skin.SetHairDrive(dt, _V(in.fwd * in.turn, 0, in.accel), in.g);
			skin.Reattach(pOut, pWalk, bLHand, bLFingerBase, F(u)); skin.Reattach(pOut, pWalk, bLHand, bLThumb, F(u));
			skin.Reattach(pOut, pWalk, bRHand, bRFingerBase, F(u)); skin.Reattach(pOut, pWalk, bRHand, bRThumb, F(u));
		}

		// ---- blinking: every 2-6 s; the lid closes in 70 ms and opens in 120 ms (MORPH "blink", if the skin has it) ----
		if (blinkT < 0 && (blinkIn -= dt) <= 0) { blinkT = 0; blinkIn = std::uniform_real_distribution<double>(2.0, 6.0)(rng); }
		if (blinkT >= 0)
		{
			blinkT += dt;
			const double w = blinkT < 0.07 ? blinkT / 0.07 : blinkT < 0.10 ? 1.0 : 1.0 - (blinkT - 0.10) / 0.12;
			skin.SetMorph("blink", F(std::clamp(w, 0.0, 1.0)));
			if (blinkT > 0.22) { blinkT = -1; skin.SetMorph("blink", 0.0f); }
		}

		// ---- weightlessness: the neutral body posture of people in orbit, slow drift of the limbs,
		//      and limbs that lag behind the thrust (underdamped springs: they swing and settle) ----
		floatW = Follow(floatW, in.floating ? 1.0 : 0.0, 0.8, dt);
		{
			// rad per m/s^2 (and per rad/s^2). Without the pack (suit RCS, free float): as it was, the user likes it.
			// With the pack: a body that holds itself under 1000 N, not a doll (user: too loose) - blended by jetW
			const double j = jetW;
			const double K = Lerp(0.55, 0.2, j), KR = Lerp(0.45, 0.15, j), LIM = Lerp(0.35, 0.18, j), KW = Lerp(0.35, 0.12, j);
			auto spring = [dt, j](double& x, double& v, double target)
			{
				const double w = Lerp(5.0, 6.0, j), z = Lerp(0.45, 0.85, j);   // muscles hold the limbs (with the pack: barely any swing back)
				const int n = (std::max)(1, static_cast<int>(std::ceil(dt * w / 0.2))); const double h = dt / n;
				for (int i = 0; i < n; ++i) { v += (w * w * (target - x) - 2 * z * w * v) * h; x += v * h; }
			};
			spring(swayZ, swayZV, in.floating ? std::clamp(K * in.thrustAcc.z + KR * in.angAcc.x + KW * in.angVel.x, -LIM, LIM) : 0.0);    // forward thrust / pitch: limbs trail
			spring(swayX, swayXV, in.floating ? std::clamp(-K * in.thrustAcc.x - KR * in.angAcc.y - KW * in.angVel.y - KW * in.angVel.z, -LIM, LIM) : 0.0);   // sideways / yaw: the other way
			spring(swayY, swayYV, in.floating ? std::clamp(-K * in.thrustAcc.y, -LIM, LIM) : 0.0);
		}
		// under thrust she hangs from the pack: the felt acceleration (thrust, not gravity) straightens the legs downwards
		hangW = Follow(hangW, in.floating ? std::clamp(length(in.thrustAcc) / 2.5, 0.0, 1.0) : 0.0, 0.5, dt);
		jetW = Follow(jetW, in.jet && in.floating ? 1.0 : 0.0, 0.6, dt);
		if (jetW > 1e-3)   // hands forward and in, elbows bent, as on handles: clear of the pods' jets
			for (int sd = 0; sd < 2; ++sd)
			{
				const float out = sd ? 1.0f : -1.0f;
				skin.Turn(pOut, sd ? bRArm : bLArm, AX_LAT, F(-0.15 * jetW));                 // a little forward
				skin.Turn(pOut, sd ? bRArm : bLArm, AX_FWD, F(out * 0.14 * jetW));            // a little out, clear of the hips
				skin.Turn(pOut, sd ? bRForeArm : bLForeArm, AX_LAT, F(-0.35 * jetW));         // elbows soft
			}
		if (hangW > 1e-3)   // hanging under the pack: knees soft, legs never quite still, they answer every turn
			for (int sd = 0; sd < 2; ++sd)
			{
				const double h = hangW * floatW, n1 = Noise(sd + 1, time * 0.9 + 3 * sd), n2 = Noise(sd + 3, time * 1.3 + 7 * sd);
				skin.Turn(pOut, sd ? bRUpLeg : bLUpLeg, AX_LAT, F(h * (-0.10 + 0.06 * n1) + swayZ * 0.9));
				skin.Turn(pOut, sd ? bRLeg : bLLeg, AX_LAT, F(h * (0.22 + 0.08 * n2) + swayZ * 0.5));
				skin.Turn(pOut, sd ? bRUpLeg : bLUpLeg, AX_FWD, F((sd ? 1.0 : -1.0) * h * 0.03 + swayX * 0.8));
				skin.Turn(pOut, sd ? bRFoot : bLFoot, AX_LAT, F(h * (0.25 + 0.1 * n2)));   // toes drop when the feet hang
			}
		if (floatW > 1e-3)
		{
			const double fw = floatW * (1 - hangW);
			for (int sd = 0; sd < 2; ++sd)
			{
				const int up = sd ? bRUpLeg : bLUpLeg, lo = sd ? bRLeg : bLLeg, ft = sd ? bRFoot : bLFoot;
				const int ar = sd ? bRArm : bLArm, fa = sd ? bRForeArm : bLForeArm;
				const float out = sd ? 1.0f : -1.0f;   // left limbs are on -x
				// legs are never still in weightlessness: hips, knees and feet each drift on their own slow rhythm
				const double dl = 0.14 * Noise(sd, time * 0.33 + 3 * sd), da = 0.09 * Noise(sd + 2, time * 0.19 + 5 * sd);
				const double dk = 0.20 * Noise(sd + 1, time * 0.41 + 7 * sd), df = 0.18 * Noise(sd + 3, time * 0.53 + 11 * sd);
				skin.Turn(pOut, up, AX_LAT, F(fw * (-0.40 + dl) + swayZ * 0.8));
				skin.Turn(pOut, up, AX_FWD, F(out * fw * 0.06 + swayX * 0.6));
				skin.Turn(pOut, lo, AX_LAT, F(fw * (0.75 + dk) + swayZ * 0.6));
				skin.Turn(pOut, ft, AX_LAT, F(fw * (-0.25 + df) + swayZ * 0.3));
				skin.Turn(pOut, ft, AX_FWD, F(out * fw * 0.5 * df));
				skin.Turn(pOut, ar, AX_FWD, F(out * fw * (1 - jetW) * (0.30 + da) + swayX * (1 - 0.6 * jetW) + out * swayY * 0.5));
				skin.Turn(pOut, ar, AX_LAT, F(fw * (1 - jetW) * (-0.30 + da) + swayZ * (1 - 0.6 * jetW)));
				skin.Turn(pOut, fa, AX_LAT, F(fw * -0.35 + swayZ * 0.5));
			}
			skin.Turn(pOut, bLowerBack, AX_LAT, F(fw * 0.10 - swayZ * 0.3));   // a slight forward hunch, the trunk lags too
		}

		// ---- crouch: absorbs landings, tucks the legs in the air ----
		if (in.landing > 0) crouchV += (std::min)(in.landing, 6.0) * 0.9;
		crouch = std::clamp(crouch + crouchV * dt, 0.0, 0.9);
		crouchV += (-81 * crouch - 12.6 * crouchV) * dt;   // w 9 rad/s, damping 0.7
		tuck = Follow(tuck, in.grounded || in.floating ? 0.0 : 0.45, in.grounded ? 0.08 : 0.15, dt);
		const double knee = (std::max)(crouch, tuck);
		if (knee > 1e-3)
		{
			skin.Turn(pOut, bLUpLeg, AX_LAT, F(-knee)); skin.Turn(pOut, bLLeg, AX_LAT, F(2 * knee)); skin.Turn(pOut, bLFoot, AX_LAT, F(-knee));
			skin.Turn(pOut, bRUpLeg, AX_LAT, F(-knee)); skin.Turn(pOut, bRLeg, AX_LAT, F(2 * knee)); skin.Turn(pOut, bRFoot, AX_LAT, F(-knee));
			skin.Turn(pOut, bLowerBack, AX_LAT, F(0.5 * knee));
			if (in.grounded) skin.Shift(pOut, _V(0, -(THIGH + SHIN) * (1 - std::cos(crouch)), 0));   // the hips drop, the soles stay down
		}

		// ---- servo boost: bounding stride, the body rises between footfalls (contacts are at phase 0 and 0.5) ----
		bounce = Follow(bounce, in.bound, 0.3, dt);
		if (bounce > 1e-3 && in.grounded) skin.Shift(pOut, _V(0, bounce * 0.07 * (0.5 - 0.5 * std::cos(4 * PI * phase)), 0));

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
