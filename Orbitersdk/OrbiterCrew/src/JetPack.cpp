// OrbiterCrew - the jet pack (see JetPack.h).
#include "JetPack.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ocrew
{
	namespace
	{
		// exhaust: hot monopropellant products are all but invisible in vacuum, a white plume in air
		PARTICLESTREAMSPEC PodVac = { 0, 0.035, 40, 60, 0.10, 0.20, 2.5, 1.0, PARTICLESTREAMSPEC::EMISSIVE,
			PARTICLESTREAMSPEC::LVL_PSQRT, 0, 0.18, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, 0 };
		PARTICLESTREAMSPEC PodAir = { 0, 0.05, 60, 40, 0.12, 1.2, 1.6, 3.0, PARTICLESTREAMSPEC::DIFFUSE,
			PARTICLESTREAMSPEC::LVL_PSQRT, 0, 0.9, PARTICLESTREAMSPEC::ATM_PLOG, 20, 1e5, 0 };
		PARTICLESTREAMSPEC RcsVac = { 0, 0.006, 30, 30, 0.08, 0.10, 0.8, 1.5, PARTICLESTREAMSPEC::DIFFUSE,
			PARTICLESTREAMSPEC::LVL_PSQRT, 0, 0.3, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, 0 };
		PARTICLESTREAMSPEC RcsAir = { 0, 0.012, 40, 20, 0.10, 0.5, 1.0, 3.0, PARTICLESTREAMSPEC::DIFFUSE,
			PARTICLESTREAMSPEC::LVL_PSQRT, 0, 0.7, PARTICLESTREAMSPEC::ATM_PLOG, 20, 1e5, 0 };

		void RotY(double a, double R[9]) { const double c = std::cos(a), s = std::sin(a); const double M[9] = { c, 0, s, 0, 1, 0, -s, 0, c }; std::copy(M, M + 9, R); }
		void RotZ(double a, double R[9]) { const double c = std::cos(a), s = std::sin(a); const double M[9] = { c, -s, 0, s, c, 0, 0, 0, 1 }; std::copy(M, M + 9, R); }
		void RotX(double a, double R[9]) { const double c = std::cos(a), s = std::sin(a); const double M[9] = { 1, 0, 0, 0, c, -s, 0, s, c }; std::copy(M, M + 9, R); }
		void Mul(const double A[9], const double B[9], double C[9])
		{
			for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) C[r * 3 + c] = A[r * 3] * B[c] + A[r * 3 + 1] * B[3 + c] + A[r * 3 + 2] * B[6 + c];
		}
		VECTOR3 Apply(const double R[9], const VECTOR3& p) { return _V(R[0] * p.x + R[1] * p.y + R[2] * p.z, R[3] * p.x + R[4] * p.y + R[5] * p.z, R[6] * p.x + R[7] * p.y + R[8] * p.z); }
	}

	bool JetPack::LoadGeo()
	{
		std::ifstream f("Config\\Tantra\\JetPack.geo");
		if (!f) { oapiWriteLog(const_cast<char*>("OrbiterCrew: Config\\Tantra\\JetPack.geo missing, jet pack geometry guessed")); return false; }
		std::string line; int nh = 0, np = 0;
		while (std::getline(f, line))
		{
			std::istringstream ss(line); std::string k; ss >> k;
			if (k == "HINGE" && nh < 2) { ss >> hinge[nh].x >> hinge[nh].y >> hinge[nh].z; ++nh; }
			else if (k == "PIVOT" && np < 2) { ss >> pivot[np].x >> pivot[np].y >> pivot[np].z; ++np; }
			else if (k == "SHELL") ss >> shellC.x >> shellC.y >> shellC.z >> shellSize.x >> shellSize.y >> shellSize.z;
			else if (k == "EXITDY") ss >> exitDy;
		}
		// index 0 is the pod on +x (her right)
		if (pivot[0].x < pivot[1].x) { std::swap(pivot[0], pivot[1]); std::swap(hinge[0], hinge[1]); }
		return nh == 2 && np == 2;
	}

	void JetPack::Setup(VESSEL4* vessel)
	{
		v = vessel;
		if (!LoadGeo())
		{
			for (int i = 0; i < 2; ++i) { const double s = i ? -1 : 1; hinge[i] = _V(s * 0.204, 0.54, -0.18); pivot[i] = _V(s * 0.44, 0.575, -0.17); }
			shellC = _V(0, 0.29, -0.21); shellSize = _V(0.40, 0.475, 0.215);
		}
		prop = v->CreatePropellantResource(FUEL, 0);
		for (int i = 0; i < 2; ++i)
		{
			// physics: at the pod's height and side, in the plane of the centre of mass (no pitch torque while upright)
			const VECTOR3 exitPt = pivot[i] + _V(0, exitDy, 0);
			// the vector steers her path, not her body: the frame and the small ports keep the body upright, so the
			// thrust is applied at the height of the centre of mass
			pod[i] = v->CreateThruster(_V(pivot[i].x, 0, 0), _V(0, 1, 0), POD_F, prop, ISP);
			v->AddExhaustStream(pod[i], exitPt, &PodVac);
			v->AddExhaustStream(pod[i], exitPt, &PodAir);
		}
		// RCS in the shell corners: same layout as the suit's own N2 unit, ten times the push
		const double PX = 0.22, PY = 0.25;
		const VECTOR3 dirs[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		for (int ix = 0; ix < 2; ++ix)
			for (int iy = 0; iy < 2; ++iy)
				for (int k = 0; k < 6; ++k)
				{
					const double sx = ix ? 1 : -1, sy = iy ? 1 : -1;
					if ((k == 0 && ix == 1) || (k == 1 && ix == 0)) continue;   // sideways only from the outward faces
					THRUSTER_HANDLE t = v->CreateThruster(_V(sx * PX, sy * PY, 0), dirs[k], RCS_F, nullptr, ISP);
					const VECTOR3 corner = shellC + _V(sx * (shellSize.x / 2 - 0.03), sy * (shellSize.y / 2 - 0.04), 0);
					v->AddExhaustStream(t, corner - dirs[k] * 0.02, &RcsVac);
					v->AddExhaustStream(t, corner - dirs[k] * 0.02, &RcsAir);
					rcs.th[ix][iy][k] = t; rcs.all.push_back(t);
				}
	}

	double JetPack::Fuel() const { return prop ? v->GetPropellantMass(prop) : 0; }

	void JetPack::Wear(double fuel)
	{
		worn = true;
		v->SetPropellantMass(prop, std::clamp(fuel, 0.0, FUEL));
		deploy = deployTarget = 0; cmd = 0; mode = MANUAL; manualDeploy = false; tilt[0] = tilt[1] = 0;
		v->SetPMI(_V(0.17, 0.05, 0.16));   // the pack and the booms far out: slower to pitch, and much slower to turn
	}

	double JetPack::Remove()
	{
		const double f = Fuel();
		worn = false;
		v->SetPMI(_V(0.15, 0.03, 0.15));
		for (THRUSTER_HANDLE t : pod) v->SetThrusterLevel(t, 0);
		cmd = 0;
		v->SetPropellantMass(prop, 0);
		mode = MANUAL;
		return f;
	}

	void JetPack::Throttle(double delta)
	{
		if (!worn) return;
		mode = MANUAL;
		cmd = std::clamp(cmd + delta, 0.0, 1.0);
	}

	double JetPack::DeltaV() const
	{
		const double m = v->GetMass(), p = Fuel();
		return p > 0 && m > p ? ISP * std::log(m / (m - p)) : 0;
	}

	bool JetPack::Key(DWORD key, bool landed)
	{
		if (!worn) return false;
		switch (key)
		{
		case OAPI_KEY_J:
			if (!surfaceMode) return true;
			if (mode == HOLD) mode = MANUAL;
			else { mode = HOLD; altTarget = landed ? 1.5 : (std::max)(1.0, tAlt); }
			return true;
		case OAPI_KEY_C: mode = mode == DESCENT ? MANUAL : DESCENT; return true;
		case OAPI_KEY_INSERT: if (mode == HOLD) vTarget = (std::min)(vTarget + 0.5, 5.0); return true;
		case OAPI_KEY_DELETE: if (mode == HOLD) vTarget = (std::max)(vTarget - 0.5, -5.0); return true;
		case OAPI_KEY_G: manualDeploy = !manualDeploy; return true;
		}
		return false;
	}

	void JetPack::Update(double dt, bool landed, bool canFly, double g, double altFeet, const FlightInput& in)
	{
		if (!worn || dt <= 0) return;
		if (!canFly) mode = MANUAL;
		const bool free = !landed;
		surfaceMode = altFeet < 3000 && g > 0.05;
		if (!surfaceMode && mode == HOLD) mode = MANUAL;       // no height to hold far from any surface

		// ---- where is up, and how does she lean ----
		OBJHANDLE ref = v->GetSurfaceRef();
		VECTOR3 rpos, rG, uG, fG; v->GetRelativePos(ref, rpos);
		const VECTOR3 up = unit(rpos);
		v->GlobalRot(_V(1, 0, 0), rG); v->GlobalRot(_V(0, 1, 0), uG); v->GlobalRot(_V(0, 0, 1), fG);
		// how far from upright, as a rotation in her own frame: right at any angle (lying, head down), not only small leans
		MATRIX3 Rm; v->GetRotationMatrix(Rm);
		const VECTOR3 ax = crossp(uG, up); const double sn = length(ax), cs = dotp(uG, up);
		const double tiltAng = std::atan2(sn, cs);
		const VECTOR3 rb = sn > 1e-6 ? tmul(Rm, ax * (tiltAng / sn)) : (cs < 0 ? _V(-PI, 0, 0) : _V(0, 0, 0));   // head down: pitch over
		const double leanF = -rb.x;    // + leaning forward (rad, up to pi)
		const double leanR = rb.z;     // + leaning to her right
		double hdg = 0; oapiGetHeading(v->GetHandle(), &hdg);
		if (prevValid)
		{
			auto rate = [&](double now, double before, double& r) { r += ((now - before) / dt - r) * (std::min)(1.0, dt / 0.06); };
			rate(leanF, prevLeanF, leanFRate); rate(leanR, prevLeanR, leanRRate);
			double dh = hdg - prevHdg; if (dh > PI) dh -= PI2; if (dh < -PI) dh += PI2;
			yawRate += (dh / dt - yawRate) * (std::min)(1.0, dt / 0.06);
		}
		prevLeanF = leanF; prevLeanR = leanR; prevHdg = hdg; prevValid = true;
		VECTOR3 hv; v->GetGroundspeedVector(FRAME_HORIZON, hv);
		const double tmax = 2 * POD_F, m = v->GetMass();

		// ---- thrust vector limits: 30 deg, 75 with Shift ("full vector"); the height hold keeps enough lift ----
		double maxTilt = in.boost ? 75 * RAD : 30 * RAD;
		tLimited = false;
		if (mode != MANUAL && surfaceMode)
		{
			const double c = m * g / (0.95 * tmax);
			const double lim = c < 1 ? std::acos(c) : 0.0;
			if (lim < maxTilt) { maxTilt = lim; tLimited = true; }
		}

		// ---- throttle: by hand (numpad 0 / .), or the height hold, or the landing autopilot ----
		const double cosAll = (std::max)(0.2, dotp(uG, up) * std::cos(0.5 * (std::abs(tilt[0]) + std::abs(tilt[1]))) * std::cos(side));
		if (surfaceMode && in.vertical != 0 && mode != HOLD && canFly && !(landed && in.vertical < 0))
		{
			mode = HOLD; altTarget = landed ? 0.3 : (std::max)(0.5, altFeet);   // Space lifts off from the ground
		}
		if (mode == HOLD) altTarget = (std::max)(0.5, altTarget + (in.boost ? 3.0 : 1.5) * in.vertical * dt);
		if (mode == MANUAL) cmd = std::clamp(cmd + 0.5 * in.throttle * dt, 0.0, 1.0);
		else if (mode == HOLD)
		{
			altTarget = (std::max)(0.5, altTarget + 1.5 * in.throttle * dt);       // numpad 0 / . move the held height
			// the ground ahead rising: hold a little higher over it
			tTerrain = false;
			if (free)
			{
				double lng, lat, rad; OBJHANDLE body = v->GetEquPos(lng, lat, rad);
				const double R = oapiGetSize(body), here = oapiSurfaceElevation(body, lng, lat);
				for (double t : { 1.0, 2.0 })
				{
					const double la = lat + hv.z * t / R, lo = lng + hv.x * t / (R * (std::max)(0.05, std::cos(lat)));
					const double rise = oapiSurfaceElevation(body, lo, la) - here;
					if (rise + 1.5 > altTarget) { altTarget = rise + 1.5; tTerrain = true; }
				}
			}
			const double aCmd = std::clamp(1.5 * (altTarget - altFeet) - 2.0 * hv.y, -3.0, 4.0);
			cmd = std::clamp(m * (g + aCmd) / (tmax * cosAll), 0.0, 1.0);
		}
		else   // DESCENT: sink slower the lower she is, touch down at 0.5 m/s
		{
			const double vt = -std::clamp(0.35 * altFeet, 0.5, 4.0);
			cmd = std::clamp(m * (g + 1.5 * (vt - hv.y)) / (tmax * cosAll), 0.0, 1.0);
			if (landed) { mode = MANUAL; cmd = 0; }
		}
		// ground proximity, in any mode: sinking too fast for the height left -> the pods brake on their own
		tProtect = false;
		if (surfaceMode && free && canFly && altFeet < 25)
		{
			const double safe = -(1.0 + 0.8 * altFeet);
			if (hv.y < safe)
			{
				cmd = (std::max)(cmd, std::clamp(m * (g + 2.0 * (0.5 * safe - hv.y)) / (tmax * cosAll), 0.0, 1.0));
				tProtect = true;
			}
		}
		if (surfaceMode && free) cmd *= std::clamp((cs - 0.3) / 0.4, 0.0, 1.0);   // full up to 45 deg of lean, none past 72
		tUpright = cs;
		level = cmd;

		// ---- booms: out when the pods are wanted, folded again after a while idle on the ground ----
		const bool wanted = manualDeploy || level > 1e-3 || mode != MANUAL || free;
		idleT = wanted ? 0 : idleT + dt;
		if (wanted) deployTarget = 1;
		else if (landed && idleT > 1.5) deployTarget = 0;
		deploy += std::clamp(deployTarget - deploy, -dt / 1.2, dt / 1.2);
		const bool ready = deploy > 0.98 && canFly;

		// ---- the pods: W swings the jets aft (push forward), S forward (brake, back); A/D against each other (turn) ----
		const double tYaw = YAW_TILT * in.yaw;
		const double sideMax = (std::min)(in.boost ? 35 * RAD : 20 * RAD, maxTilt + 1e-6);
		side += std::clamp((ready ? sideMax * in.strafe : 0.0) - side, -dt * 90 * RAD, dt * 90 * RAD);
		for (int i = 0; i < 2; ++i)
		{
			const double sgn = i == 0 ? 1 : -1;   // +1: the pod on her right (not the sideways gimbal angle 'side')
			const double target = ready ? std::clamp(maxTilt * in.pitch - sgn * tYaw, -maxTilt - YAW_TILT, maxTilt + YAW_TILT) : 0.0;
			tilt[i] += std::clamp(target - tilt[i], -dt * 90 * RAD, dt * 90 * RAD);
			v->SetThrusterDir(pod[i], _V(std::sin(side), std::cos(side) * std::cos(tilt[i]), std::cos(side) * std::sin(tilt[i])));
			v->SetThrusterLevel(pod[i], ready ? level : 0.0);
		}
		tMaxTilt = maxTilt;

		// ---- the body stays upright: the pack's small ports hold her attitude near a surface ----
		if (free && surfaceMode && canFly && Fuel() > 0)
		{
			const double p = std::clamp(4.0 * leanF + 2.0 * leanFRate, -1.0, 1.0);    // leaning forward -> nose up
			const double r = std::clamp(4.0 * leanR + 2.0 * leanRRate, -1.0, 1.0);    // leaning right -> roll left
			const double y = in.yaw ? 0.0 : std::clamp(1.5 * yawRate, -1.0, 1.0);    // no turn asked: stop turning
			v->SetThrusterGroupLevel(THGROUP_ATT_PITCHUP, p > 0 ? p : 0); v->SetThrusterGroupLevel(THGROUP_ATT_PITCHDOWN, p < 0 ? -p : 0);
			v->SetThrusterGroupLevel(THGROUP_ATT_BANKLEFT, r > 0 ? r : 0); v->SetThrusterGroupLevel(THGROUP_ATT_BANKRIGHT, r < 0 ? -r : 0);
			v->SetThrusterGroupLevel(THGROUP_ATT_YAWLEFT, y > 0 ? y : 0); v->SetThrusterGroupLevel(THGROUP_ATT_YAWRIGHT, y < 0 ? -y : 0);
			holdingAttitude = true;
		}
		else if (holdingAttitude)
		{
			for (THGROUP_TYPE t : { THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN, THGROUP_ATT_BANKLEFT, THGROUP_ATT_BANKRIGHT, THGROUP_ATT_YAWLEFT, THGROUP_ATT_YAWRIGHT })
				v->SetThrusterGroupLevel(t, 0);
			holdingAttitude = false;
		}

		tLeanF = leanF; tLeanR = leanR; tVs = hv.y; tAlt = altFeet; tGs = std::hypot(hv.x, hv.z); tFlying = free; tBoost = in.boost;
	}

	void JetPack::Touchdown() { mode = MANUAL; cmd = level = 0; for (THRUSTER_HANDLE t : pod) v->SetThrusterLevel(t, 0); }

	void JetPack::Pose(Skin& skin) const
	{
		skin.SetPartHidden("Jet", !worn);
		if (!worn) return;
		// modelled deployed; the booms swing back about the hinge's vertical axis to fold, the pods tilt on their pivots.
		// Label side: the model's "L" parts sit on +x (her right) in the Orbiter frame.
		for (int i = 0; i < 2; ++i)
		{
			const char* boom = i == 0 ? "JetBoomL" : "JetBoomR"; const char* podName = i == 0 ? "JetPodL" : "JetPodR";
			const double side = pivot[i].x > 0 ? 1 : -1;
			double Ry[9], Rx[9], Rz[9], Rxz[9], R[9];
			RotY(side * PI05 * (1 - deploy), Ry); RotX(tilt[i], Rx); RotZ(-this->side, Rz); Mul(Rx, Rz, Rxz); Mul(Ry, Rxz, R);
			const VECTOR3 tb = hinge[i] - Apply(Ry, hinge[i]);
			const VECTOR3 tp = Apply(Ry, pivot[i] - Apply(Rxz, pivot[i]) - hinge[i]) + hinge[i];
			float Rf[9], Tb[3] = { float(tb.x), float(tb.y), float(tb.z) }, Rp[9], Tp[3] = { float(tp.x), float(tp.y), float(tp.z) };
			for (int k = 0; k < 9; ++k) { Rf[k] = float(Ry[k]); Rp[k] = float(R[k]); }
			skin.SetPartTransform(boom, Rf, Tb);
			skin.SetPartTransform(podName, Rp, Tp);
		}
	}

	std::string JetPack::Hud() const
	{
		if (!worn) return std::string();
		char b[200];
		static const char* MODE[] = { "manual", "HOVER HOLD", "AUTO DESCENT" };
		snprintf(b, sizeof b, "JET  fuel %d%%  dv %.0f m/s  thrust %d%%  booms %s  %s%s", static_cast<int>(100 * Fuel() / FUEL), DeltaV(),
			static_cast<int>(100 * level), deploy > 0.98 ? "out" : deploy < 0.02 ? "folded" : "moving", MODE[mode],
			mode == HOLD ? (vTarget >= 0 ? "  +" : "  ") : "");
		std::string s = b;
		if (mode == HOLD) { snprintf(b, sizeof b, "%.1f m/s", vTarget); s += b; }
		if (tFlying && surfaceMode)
		{
			snprintf(b, sizeof b, "\nFLY  lean fwd %+.0f (want %+.0f)  side %+.0f (want %+.0f)  vs %+.1f m/s (hold %+.1f)  alt %.1f m  gs %.1f m/s%s%s",
				tLeanF * DEG, tWantF * DEG, tLeanR * DEG, tWantR * DEG, tVs, tVsCmd, tAlt, tGs, tBoost ? "  TURBO" : "", tProtect ? "  GROUND PROXIMITY" : tTerrain ? "  TERRAIN AHEAD" : "");
			s += b;
		}
		return s;
	}
}

namespace ocrew
{
	void JetPack::Fill(HudData& d) const
	{
		d.jet = worn;
		if (!worn) return;
		d.jetFlying = tFlying; d.jetSurface = surfaceMode; d.jetLimited = tLimited; d.jetProtect = tProtect; d.jetTerrain = tTerrain;
		d.jetFuel = Fuel() / FUEL; d.jetDv = DeltaV(); d.jetThrottle = level; d.jetFlow = Thrust() / ISP; d.jetTilt[0] = tilt[0]; d.jetTilt[1] = tilt[1];
		d.jetMaxTilt = tMaxTilt; d.jetDeploy = deploy; d.jetAltHold = altTarget; d.alt = tAlt; d.vs = tVs; d.gs = tGs;
		d.leanF = tLeanF; d.leanR = tLeanR; d.jetMode = static_cast<int>(mode); d.boost = tBoost;
	}
}
