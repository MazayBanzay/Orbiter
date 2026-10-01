// OrbiterCrew - the jet pack (see JetPack.h).
#include "JetPack.h"
#include <map>
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
		PARTICLESTREAMSPEC Dust = { 0, 0.35, 70, 3.5, 0.9, 1.8, 1.6, 0.2, PARTICLESTREAMSPEC::DIFFUSE,
			PARTICLESTREAMSPEC::LVL_LIN, 0, 0.55, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, 0 };
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
			for (int i = 0; i < 2; ++i) { const double s = i ? -1 : 1; hinge[i] = _V(s * 0.204, 0.54, -0.18); pivot[i] = _V(s * 0.60, 0.575, -0.17); }
			shellC = _V(0, 0.29, -0.21); shellSize = _V(0.40, 0.475, 0.215);
		}
		prop = v->CreatePropellantResource(FUEL, 0);
		// the particle streams use Orbiter's own textures: the D3D9 client shapes the puff itself, custom ones drew squares
		for (int i = 0; i < 2; ++i)
		{
			// physics: at the pod's height and side, in the plane of the centre of mass (no pitch torque while upright)
			const VECTOR3 exitPt = pivot[i] + _V(0, exitDy, 0);
			// the vector steers her path, not her body: the frame and the small ports keep the body upright, so the
			// thrust is applied at the height of the centre of mass
			pod[i] = v->CreateThruster(_V(pivot[i].x, 0, 0), _V(0, 1, 0), POD_F, prop, ISP);
			v->AddExhaustStream(pod[i], exitPt, &PodVac);
			steam.push_back({ pod[i], exitPt, &PodAir, v->AddExhaustStream(pod[i], exitPt, &PodAir) });
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
					steam.push_back({ t, corner - dirs[k] * 0.02, &RcsAir, v->AddExhaustStream(t, corner - dirs[k] * 0.02, &RcsAir) });
					rcs.th[ix][iy][k] = t; rcs.all.push_back(t);
				}
	}

	void JetPack::SetupDust(double feetY)
	{
		if (!dust) dust = v->AddParticleStream(&Dust, _V(0, feetY + 0.05, 0), _V(0, 1, 0), &dustLevel);
	}

	void JetPack::SetOxygen(bool o2)
	{
		if (o2 == steamOn) return;
		steamOn = o2;
		for (Steam& s : steam)
			if (o2 && !s.h) s.h = v->AddExhaustStream(s.th, s.pos, s.spec);
			else if (!o2 && s.h) { v->DelExhaustStream(s.h); s.h = nullptr; }
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
			assist = true;   // the height hold is the assistant's
			if (!surfaceMode) return true;
			if (mode == HOLD) mode = MANUAL;
			else { mode = HOLD; altTarget = landed ? 1.5 : (std::max)(1.0, tAlt); }
			return true;
		case OAPI_KEY_C: assist = true; mode = mode == DESCENT ? MANUAL : DESCENT; return true;
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
		if (mode == VECTOR && !vecFresh) { mode = surfaceMode ? HOLD : MANUAL; altTarget = (std::max)(1.5, altFeet); spdSet = false; }
		const bool vecNow = mode == VECTOR; vecFresh = false;

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
		if (braking && (mode != HOLD || in.pitch != 0 || in.boost)) braking = false;   // the pilot steers again
		double maxTilt = in.boost ? 75 * RAD : mode == DESCENT || braking ? 55 * RAD : 30 * RAD;
		tLimited = false;
		if (mode != MANUAL && surfaceMode)
		{
			const double c = m * g / (0.95 * tmax);
			const double lim = c < 1 ? std::acos(c) : 0.0;
			if (lim < maxTilt) { maxTilt = lim; tLimited = true; }
		}

		// ---- throttle: by hand (numpad 0 / .), or the height hold, or the landing autopilot ----
		// the share of the pods' thrust that lifts: their real directions against the local vertical
		double cosAll;
		{
			double lift = 0, sum = 0;
			for (int i = 0; i < 2; ++i)
			{
				VECTOR3 dg; v->GlobalRot(_V(std::sin(sidePod[i]), std::cos(sidePod[i]) * std::cos(tilt[i]), std::cos(sidePod[i]) * std::sin(tilt[i])), dg);
				const double w = podLvl[i] > 1e-3 || level > 1e-3 ? (std::max)(podLvl[i], 1e-3) : 1.0;
				lift += w * dotp(dg, up); sum += w;
			}
			cosAll = (std::max)(0.2, lift / sum);
		}
		if (assist && !in.boost && surfaceMode && in.vertical != 0 && mode != HOLD && canFly && !(landed && in.vertical < 0))
		{
			mode = HOLD; spdSet = false; altTarget = landed ? 0.3 : (std::max)(0.5, altFeet);   // Space lifts off from the ground
		}
		if (mode == HOLD && !in.boost) altTarget = (std::max)(0.5, altTarget + 1.5 * in.vertical * dt);
		if (mode == HOLD && altTarget <= 0.55 && altFeet < 0.8 && in.vertical <= 0) mode = DESCENT;   // held at the bottom, near the ground: touch down
		if (vecNow) cmd = std::clamp(m * length(vecA) / tmax, 0.0, 1.0);   // the autopilot sets the whole vector
		else if (mode == MANUAL) cmd = std::clamp(cmd + (0.5 * in.throttle + (assist || in.boost ? 0.0 : 0.8 * in.vertical)) * dt, 0.0, 1.0);
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
			const double drift = std::hypot(hv.x, hv.z);
			double vt = -(std::max)(0.5, (std::min)(4.0, std::sqrt(2 * 0.8 * (std::max)(0.0, altFeet - 0.3))));   // stop the sink by 0.3 m
			if (drift > 2.5) vt = (std::max)(vt, -1.5);   // still braking a big drift: sink gently meanwhile
			if (drift > 0.35) vt = (std::max)(vt, altFeet > 1.2 ? -0.5 : 0.4 * (1.2 - altFeet));   // no touchdown while sliding: wait at ~1 m
			cmd = std::clamp(m * (g + 1.5 * (vt - hv.y)) / (tmax * cosAll), 0.0, 1.0);
			if (landed) { mode = MANUAL; cmd = 0; }
		}
		// ground proximity, in any mode: sinking too fast for the height left -> the pods brake on their own
		tProtect = false;
		if (assist && surfaceMode && free && canFly && altFeet < 25 && !vecNow)
		{
			const double safe = -(1.0 + 0.8 * altFeet);
			if (hv.y < safe)
			{
				cmd = (std::max)(cmd, std::clamp(m * (g + 2.0 * (0.5 * safe - hv.y)) / (tmax * cosAll), 0.0, 1.0));
				tProtect = true;
			}
		}
		if (assist && surfaceMode && free) cmd *= std::clamp((cs - 0.3) / 0.4, 0.0, 1.0);   // full up to 45 deg of lean, none past 72
		tUpright = cs;
		tHover = surfaceMode && g > 0.05 ? Fuel() / (m * g / ISP) : 0;
		if (assist && surfaceMode && !tProtect && !vecNow) cmd = (std::min)(cmd, std::clamp((mode == DESCENT ? 2.2 : 1.6) * m * g / (tmax * cosAll), 0.0, 1.0));   // the assistant: 1.6 of her weight, 2.2 to land
		// Shift+Space: full thrust, Shift+Ctrl: none, while held (both modes); otherwise the throttle or the assistant
		level = in.boost && in.vertical > 0 ? 1.0 : in.boost && in.vertical < 0 ? 0.0 : cmd;
		if (Fuel() <= 0) cmd = level = 0;   // the tank is dry: nothing to push with
		// down: the engines are off and stay off until the thrust keys are released and pressed again
		if (lockout)
		{
			const bool asking = in.vertical > 0 || in.throttle > 0;
			if (!asking) released = true;
			else if (released) lockout = false;
			if (lockout) { cmd = level = 0; if (mode != MANUAL) mode = MANUAL; }
		}
		dustLevel = worn && surfaceMode && deploy > 0.98 ? level * std::clamp(1.0 - altFeet / 1.5, 0.0, 1.0) * std::clamp(cs, 0.0, 1.0) : 0.0;

		// ---- booms: out when the pods are wanted, folded again after a while idle on the ground ----
		const bool wanted = manualDeploy || level > 1e-3 || mode != MANUAL || free;
		idleT = wanted ? 0 : idleT + dt;
		if (wanted) deployTarget = 1;
		else if (landed && idleT > 1.5) deployTarget = 0;
		deploy += std::clamp(deployTarget - deploy, -dt / 1.2, dt / 1.2);
		const bool ready = deploy > 0.98 && canFly;

		// ---- the landing brakes the drift by itself (W/S/Q/E take over); the pilot's input otherwise ----
		FlightInput pin = in;
		// ---- the cruise (height hold by hand): W/S set the ground speed, the pods keep it and kill the side drift ----
		{
			const double ch = std::cos(hdg), sh = std::sin(hdg);
			const double vf = hv.z * ch + hv.x * sh, vr = hv.x * ch - hv.z * sh;
			tVf = vf;
			if (mode == HOLD && assist && free && !in.ap && !in.boost)
			{
				if (!spdSet) { spdTarget = std::clamp(vf, -5.0, 40.0); spdSet = true; }
				spdTarget = std::clamp(spdTarget + 3.0 * in.pitch * dt, -5.0, 40.0);
				if (braking) { spdTarget = 0; if (std::hypot(vf, vr) < 0.15 && std::abs(hv.y) < 0.2) braking = false; }
				const double gg = (std::max)(0.5, g), mt = (std::max)(5 * RAD, maxTilt), ms = (std::min)(braking ? 45 * RAD : 20 * RAD, mt);
				pin.pitch = std::clamp(std::atan(std::clamp(1.0 * (spdTarget - vf), -4.0, 4.0) / gg) / mt, -1.0, 1.0);
				if (in.strafe == 0 || braking) pin.strafe = std::clamp(std::atan(std::clamp(-0.8 * vr, -4.0, 4.0) / gg) / ms, -1.0, 1.0);
			}
			else if (mode != HOLD) spdSet = false;
		}
		if (mode != DESCENT || !free || in.pitch != 0 || in.strafe != 0) landSet = false;   // steering by hand: a new point when let go
		if (mode == DESCENT && assist && free && in.pitch == 0 && in.strafe == 0)
		{
			double lng, lat, rad; OBJHANDLE body = v->GetEquPos(lng, lat, rad);
			const double R = oapiGetSize(body), cl = (std::max)(0.05, std::cos(lat)), gs = std::hypot(hv.x, hv.z);
			if (!landSet)
			{
				// the point she can stop at, braking at ~3 m/s^2: that is where she lands
				const double dStop = gs * gs / (2 * 3.0);
				landLat = lat + (gs > 0.1 ? hv.z / gs : 0) * dStop / R;
				landLng = lng + (gs > 0.1 ? hv.x / gs : 0) * dStop / (R * cl);
				landSet = true;
			}
			double dl = landLng - lng; while (dl > PI) dl -= PI2; while (dl < -PI) dl += PI2;
			const double errN = (landLat - lat) * R, errE = dl * R * cl;
			const double aN = std::clamp(0.6 * errN - 1.6 * hv.z, -4.0, 4.0), aE = std::clamp(0.6 * errE - 1.6 * hv.x, -4.0, 4.0);
			const double ch = std::cos(hdg), sh = std::sin(hdg);
			const double aF = aN * ch + aE * sh, aR = aE * ch - aN * sh;
			const double gg = (std::max)(0.5, g), mt = (std::max)(5 * RAD, maxTilt);
			pin.pitch = std::clamp(std::atan(aF / gg) / mt, -1.0, 1.0);
			pin.strafe = std::clamp(std::atan(aR / gg) / (std::min)(45 * RAD, mt), -1.0, 1.0);
		}
		// ---- the pods: W swings the jets aft (push forward), S forward (brake, back); A/D against each other (turn) ----
		const double tYaw = assist ? YAW_TILT * std::clamp(1.5 * (1.0 * in.yaw - yawRate), -1.0, 1.0) : YAW_TILT * in.yaw;
		const double sideMax = (std::min)(in.boost ? 35 * RAD : mode == DESCENT || braking ? 45 * RAD : 20 * RAD, maxTilt + 1e-6);
		// the thrust direction wanted: with the assistant, from the local vertical (her lean is taken out - the vector,
		// not the body, sets the path); by hand, from her own axis
		double tiltCmd = maxTilt * pin.pitch, sideCmd = sideMax * pin.strafe;
		if (assist && free && surfaceMode)
		{
			const VECTOR3 fH0 = fG - up * dotp(fG, up), fH = length(fH0) > 0.2 ? unit(fH0) : unit(uG - up * dotp(uG, up));
			const VECTOR3 rH0 = rG - up * dotp(rG, up) - fH * dotp(rG, fH), rH = length(rH0) > 1e-3 ? unit(rH0) : _V(0, 0, 0);
			const VECTOR3 w = unit(up + fH * std::tan(tiltCmd) + rH * std::tan(sideCmd));
			const VECTOR3 b = tmul(Rm, w);   // in her frame
			tiltCmd = std::clamp(std::atan2(b.z, b.y), -80 * RAD, 80 * RAD);
			sideCmd = std::asin(std::clamp(b.x, -1.0, 1.0));
		}
		if (vecNow && length(vecA) > 1e-3)   // the autopilot's vector: from the horizon frame into hers
		{
			const VECTOR3 fH0 = fG - up * dotp(fG, up), fH = length(fH0) > 0.2 ? unit(fH0) : unit(uG - up * dotp(uG, up));
			const VECTOR3 rH0 = rG - up * dotp(rG, up) - fH * dotp(rG, fH), rH = length(rH0) > 1e-3 ? unit(rH0) : _V(0, 0, 0);
			const double ch = std::cos(hdg), sh = std::sin(hdg);
			const VECTOR3 north = fH * ch - rH * sh, east = fH * sh + rH * ch;
			const VECTOR3 w = unit(east * vecA.x + up * (std::max)(0.05, vecA.y) + north * vecA.z);   // the pods only lift
			const VECTOR3 b = tmul(Rm, w);
			tiltCmd = std::clamp(std::atan2(b.z, b.y), -80 * RAD, 80 * RAD);
			sideCmd = std::asin(std::clamp(b.x, -1.0, 1.0));
		}
		side += std::clamp((ready ? sideCmd : 0.0) - side, -dt * 90 * RAD, dt * 90 * RAD);
		// sideways: only the pod whose jet goes outwards swings (the other one's would cross her body and the pack);
		// it swings twice as far for the same push, and runs harder so both lift alike (no roll)
		const double INWARD = 6 * RAD, sOne = std::atan(2 * std::tan(std::abs(side)));
		for (int i = 0; i < 2; ++i)
		{
			const double sgn = i == 0 ? 1 : -1;   // +1: the pod on her right
			// thrust to +x (side > 0) blows the jet to -x: outwards for the left pod (sgn -1)
			const bool outward = side * sgn < 0;
			sidePod[i] = (outward ? (std::min)(sOne, 60 * RAD) * (side > 0 ? 1 : -1) : std::clamp(side, -INWARD, INWARD));
		}
		podLvl[0] = podLvl[1] = level;
		{
			const double c0 = std::cos(sidePod[0]), c1 = std::cos(sidePod[1]), cm = (std::max)(c0, c1);
			double l0 = level * cm / c0, l1 = level * cm / c1;
			const double top = (std::max)(l0, l1);
			if (top > 1) { l0 /= top; l1 /= top; }
			podLvl[0] = l0; podLvl[1] = l1;
		}
		for (int i = 0; i < 2; ++i)
		{
			const double sgn = i == 0 ? 1 : -1;
			const double lim = (std::max)(maxTilt, std::abs(tiltCmd)) + YAW_TILT;
			const double target = ready ? std::clamp(tiltCmd - sgn * tYaw, -lim, lim) : 0.0;
			tilt[i] += std::clamp(target - tilt[i], -dt * 90 * RAD, dt * 90 * RAD);
			v->SetThrusterDir(pod[i], _V(std::sin(sidePod[i]), std::cos(sidePod[i]) * std::cos(tilt[i]), std::cos(sidePod[i]) * std::sin(tilt[i])));
			v->SetThrusterLevel(pod[i], ready ? podLvl[i] : 0.0);
			// the nozzle heats up in a few seconds of thrust and cools slower (for a glowing exit material, not yet drawn)
			const double want = ready ? podLvl[i] : 0.0;
			heat[i] += (want - heat[i]) * (std::min)(1.0, dt / (want > heat[i] ? 3.0 : 7.0));
		}
		tMaxTilt = maxTilt;

		// ---- the body stays upright: the pack's small ports hold her attitude near a surface ----
		if (assist && free && surfaceMode && canFly && Fuel() > 0)
		{
			const double p = std::clamp(4.0 * leanF + 2.0 * leanFRate, -1.0, 1.0);    // leaning forward -> nose up
			const double r = std::clamp(4.0 * leanR + 2.0 * leanRRate, -1.0, 1.0);    // leaning right -> roll left
			const double y = in.yaw ? 0.0 : std::clamp(1.5 * yawRate, -1.0, 1.0);    // no turn asked: the ports help stop the turn
			// pitch and yaw share the fore/aft nozzles: setting the groups one after another let the yaw groups overwrite
			// the pitch every step (the flight log: pitch-up and pitch-down equal, she tipped over unopposed). Sum per nozzle.
			std::map<THRUSTER_HANDLE, double> lv;
			auto add = [&](THGROUP_TYPE t, double l)
			{
				for (DWORD i = 0; i < v->GetGroupThrusterCount(t); ++i) lv[v->GetGroupThruster(t, i)] += (std::max)(0.0, l);
			};
			add(THGROUP_ATT_PITCHUP, p); add(THGROUP_ATT_PITCHDOWN, -p);
			add(THGROUP_ATT_BANKLEFT, r); add(THGROUP_ATT_BANKRIGHT, -r);
			add(THGROUP_ATT_YAWLEFT, y); add(THGROUP_ATT_YAWRIGHT, -y);
			for (auto& t : lv) v->SetThrusterLevel(t.first, (std::min)(1.0, t.second));
			holdingAttitude = true;
		}
		else if (holdingAttitude)
		{
			for (THGROUP_TYPE t : { THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN, THGROUP_ATT_BANKLEFT, THGROUP_ATT_BANKRIGHT, THGROUP_ATT_YAWLEFT, THGROUP_ATT_YAWRIGHT })
				for (DWORD i = 0; i < v->GetGroupThrusterCount(t); ++i) v->SetThrusterLevel(v->GetGroupThruster(t, i), 0);
			holdingAttitude = false;
		}

		tLeanF = leanF; tLeanR = leanR; tVs = hv.y; tAlt = altFeet; tGs = std::hypot(hv.x, hv.z); tFlying = free; tBoost = in.boost;
	}

	void JetPack::Nudge(double dAlt, double dSpd, bool stop)
	{
		if (!worn) return;
		assist = true; lockout = false;
		if (mode != HOLD) { mode = HOLD; altTarget = (std::max)(1.5, tAlt); spdTarget = std::clamp(tVf, -5.0, 40.0); spdSet = true; }
		if (!spdSet) { spdTarget = std::clamp(tVf, -5.0, 40.0); spdSet = true; }
		altTarget = (std::max)(0.5, altTarget + dAlt);
		spdTarget = stop ? 0.0 : std::clamp(spdTarget + dSpd, -5.0, 40.0);
	}

	double JetPack::MaxAccel() const { return worn && v ? 2 * POD_F / v->GetMass() : 0.0; }

	void JetPack::Stop()
	{
		if (!worn || !surfaceMode) return;
		assist = true; lockout = false;
		mode = HOLD; altTarget = (std::max)(1.0, tAlt); spdTarget = 0; spdSet = true; braking = true;
	}

	void JetPack::Touchdown()
	{
		braking = false;
		mode = MANUAL; cmd = level = 0; lockout = true; released = false; landSet = false;
		for (THRUSTER_HANDLE t : pod) v->SetThrusterLevel(t, 0);
		// the attitude ports too: Orbiter wakes a landed vessel that still has a thruster firing (the 'static flight'
		// after a touchdown: set down, lifted by the ports set earlier in the same step, set down again...)
		for (THGROUP_TYPE t : { THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN, THGROUP_ATT_BANKLEFT, THGROUP_ATT_BANKRIGHT, THGROUP_ATT_YAWLEFT, THGROUP_ATT_YAWRIGHT,
			THGROUP_ATT_RIGHT, THGROUP_ATT_LEFT, THGROUP_ATT_UP, THGROUP_ATT_DOWN, THGROUP_ATT_FORWARD, THGROUP_ATT_BACK })
			for (DWORD i = 0; i < v->GetGroupThrusterCount(t); ++i) v->SetThrusterLevel(v->GetGroupThruster(t, i), 0);
		holdingAttitude = false;
	}

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
			RotY(side * PI05 * (1 - deploy), Ry); RotX(tilt[i], Rx); RotZ(-sidePod[i], Rz); Mul(Rx, Rz, Rxz); Mul(Ry, Rxz, R);
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
		static const char* MODE[] = { "manual", "HOVER HOLD", "AUTO DESCENT", "AP VECTOR" };
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
		d.jetMaxTilt = tMaxTilt; d.jetDeploy = deploy; d.jetAltHold = altTarget; d.jetSpd = spdTarget; d.jetVf = tVf; d.jetBraking = braking; d.alt = tAlt; d.vs = tVs; d.gs = tGs;
		d.leanF = tLeanF; d.leanR = tLeanR; d.jetMode = static_cast<int>(mode); d.boost = tBoost; d.jetAssist = assist; d.jetHover = tHover;
	}
}
