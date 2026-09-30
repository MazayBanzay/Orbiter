// OrbiterCrew - the suit computer's autopilots (see Autopilot.h).
#include "Autopilot.h"
#include "SuitHud.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

namespace ocrew
{
	namespace
	{
		double Wrap(double a) { while (a > PI) a -= PI2; while (a < -PI) a += PI2; return a; }
		VECTOR3 Unit(const VECTOR3& v) { const double l = length(v); return l > 1e-9 ? v / l : v; }
		VECTOR3 Clamp(const VECTOR3& v, double m) { const double l = length(v); return l > m ? v * (m / l) : v; }
		std::string N(double x, int dec = 1) { char b[32]; snprintf(b, sizeof b, "%.*f", dec, x); std::string s = b; for (char& c : s) if (c == '.') c = ','; return s; }
		bool IsPack(OBJHANDLE h)
		{
			VESSEL* o = oapiGetVesselInterface(h);
			std::string c = o && o->GetClassName() ? o->GetClassName() : "";
			std::transform(c.begin(), c.end(), c.begin(), [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
			return c.find("jetpack") != std::string::npos;
		}
		// how close to come: beside a pack or a person, clear of anything bigger
		double Standoff(OBJHANDLE h)
		{
			if (IsPack(h)) return 1.2;
			if (!CrewDisplayName(h).empty()) return 2.0;
			return (std::min)(0.6 * oapiGetSize(h) + 3, 40.0);
		}
		// the target's port nearest to her: position, axis out of the port, and its "up", global frame
		bool NearestPort(VESSEL* v, VESSEL* o, VECTOR3& P, VECTOR3& D, VECTOR3& U)
		{
			VECTOR3 me; v->GetGlobalPos(me);
			double best = 1e18; bool found = false;
			for (UINT i = 0; i < o->DockCount(); ++i)
			{
				VECTOR3 p, d, r, gp; o->GetDockParams(o->GetDockHandle(i), p, d, r);
				o->Local2Global(p, gp);
				const double dist = length(me - gp);
				if (dist < best) { best = dist; P = gp; o->GlobalRot(d, D); o->GlobalRot(r, U); found = true; }
			}
			return found;
		}
	}

	const char* Autopilot::Name(int m)
	{
		static const char* N[] = { "", "ЗАВИС", "ПЕРЕЛЁТ", "СИНХР", "УДЕРЖ", "СБЛИЖ", "СТЫК" };
		return m >= 0 && m <= DOCK ? N[m] : "";
	}

	void Autopilot::Engage(Mode m, OBJHANDLE target, VESSEL* v)
	{
		if (m == mode && (m == HOVER || target == tgt)) { Off(v, "выключен"); return; }
		if (m != HOVER && !target) { status = "нет цели · выберите на странице ЦЕЛИ"; return; }
		Zero(v);
		mode = m; tgt = target; prevOk = false;
		if (m == HOLD && target) v->GetRelativePos(target, holdRel);
		status = std::string(Name(m)) + " · включён";
	}

	void Autopilot::Off(VESSEL* v, const char* why)
	{
		if (mode == OFF) return;
		Zero(v);
		status = std::string(Name(mode)) + (why ? std::string(" · ") + why : " · выключен");
		mode = OFF; cmdH = _V(0, 0, 0);
	}

	void Autopilot::Zero(VESSEL* v)
	{
		if (!driving) return;
		for (THGROUP_TYPE t : { THGROUP_ATT_RIGHT, THGROUP_ATT_LEFT, THGROUP_ATT_UP, THGROUP_ATT_DOWN, THGROUP_ATT_FORWARD, THGROUP_ATT_BACK,
			THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN, THGROUP_ATT_YAWLEFT, THGROUP_ATT_YAWRIGHT, THGROUP_ATT_BANKLEFT, THGROUP_ATT_BANKRIGHT })
			for (DWORD i = 0; i < v->GetGroupThrusterCount(t); ++i) v->SetThrusterLevel(v->GetGroupThruster(t, i), 0);
		driving = false;
	}

	// translation and rotation share the RCS nozzles: sum what each group wants per nozzle, then set the nozzles
	void Autopilot::Drive(VESSEL* v, const VECTOR3& aBody, const VECTOR3& rot)
	{
		const double m = v->GetMass();
		std::map<THRUSTER_HANDLE, double> lvl;
		auto add = [&](THGROUP_TYPE t, double level)
		{
			const DWORD n = v->GetGroupThrusterCount(t);
			for (DWORD i = 0; i < n; ++i) { THRUSTER_HANDLE th = v->GetGroupThruster(t, i); lvl[th]; }
			if (level <= 0 || n == 0) return;
			for (DWORD i = 0; i < n; ++i) lvl[v->GetGroupThruster(t, i)] += level;
		};
		auto axis = [&](THGROUP_TYPE plus, THGROUP_TYPE minus, double a)
		{
			const THGROUP_TYPE t = a >= 0 ? plus : minus;
			double f = 0; for (DWORD i = 0; i < v->GetGroupThrusterCount(t); ++i) f += v->GetThrusterMax0(v->GetGroupThruster(t, i));
			const double level = f > 0 && std::abs(a) > 0.002 ? (std::min)(1.0, std::abs(a) * m / f) : 0.0;
			add(plus, a >= 0 ? level : 0); add(minus, a < 0 ? level : 0);
		};
		axis(THGROUP_ATT_RIGHT, THGROUP_ATT_LEFT, aBody.x);
		axis(THGROUP_ATT_UP, THGROUP_ATT_DOWN, aBody.y);
		axis(THGROUP_ATT_FORWARD, THGROUP_ATT_BACK, aBody.z);
		auto turn = [&](THGROUP_TYPE plus, THGROUP_TYPE minus, double c) { add(plus, c > 0 ? c : 0); add(minus, c < 0 ? -c : 0); };
		turn(THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN, rot.x);
		turn(THGROUP_ATT_YAWRIGHT, THGROUP_ATT_YAWLEFT, rot.y);
		turn(THGROUP_ATT_BANKRIGHT, THGROUP_ATT_BANKLEFT, rot.z);
		for (auto& p : lvl) v->SetThrusterLevel(p.first, (std::min)(1.0, p.second));
		driving = true;
	}

	void Autopilot::Step(VESSEL* v, JetPack& jet, double dt, double g, bool landed, bool canFly, bool manual, FlightInput& in)
	{
		if (mode == OFF || dt <= 0) return;
		if (tgt && !oapiIsVessel(tgt)) { Off(v, "цель исчезла"); return; }
		if (!canFly) { Off(v, "нет питания или сил"); return; }
		const bool surfaceAp = mode == HOVER || mode == TRANSFER;
		if (surfaceAp)
		{
			if (!jet.Worn() || !jet.SurfaceMode()) { Off(v, "только у поверхности с ранцем"); return; }
			if (manual) { Off(v, "снят вручную"); return; }
			Surface(v, jet, dt, g, in);
		}
		else
		{
			if (landed || (jet.Worn() && jet.SurfaceMode())) { Off(v, "только в космосе"); return; }
			Space(v, dt);
		}
	}

	void Autopilot::Surface(VESSEL* v, JetPack& jet, double dt, double g, FlightInput& in)
	{
		VECTOR3 hv; v->GetGroundspeedVector(FRAME_HORIZON, hv);   // x east, y up, z north
		double hdg = 0; oapiGetHeading(v->GetHandle(), &hdg);
		const double ch = std::cos(hdg), sh = std::sin(hdg);
		const double vf = hv.z * ch + hv.x * sh, vr = hv.x * ch - hv.z * sh;   // forward, to her right
		if (jet.ModeId() != 1) jet.SetHold((std::max)(1.5, jet.AltNow()));
		double aF = -0.8 * vf, aR = -0.8 * vr;
		if (mode == TRANSFER)
		{
			OBJHANDLE body = v->GetSurfaceRef();
			VECTOR3 me, it; v->GetGlobalPos(me); oapiGetGlobalPos(tgt, &it);
			double lng, lat, rad, tl, tb, tr; oapiGlobalToEqu(body, me, &lng, &lat, &rad); oapiGlobalToEqu(body, it, &tl, &tb, &tr);
			const double R = oapiGetSize(body), north = (tb - lat) * R, east = Wrap(tl - lng) * R * std::cos(lat);
			const double dist = std::hypot(north, east), left = dist - Standoff(tgt);
			const double herr = Wrap(std::atan2(east, north) - hdg);
			herrRate += ((prevOk ? Wrap(herr - prevHerr) / dt : 0) - herrRate) * (std::min)(1.0, dt / 0.2);
			prevHerr = herr; prevOk = true;
			in.yaw = std::clamp(1.0 * herr + 0.8 * herrRate, -1.0, 1.0);
			const double vdes = std::abs(herr) < 25 * RAD ? (std::min)(6.0, std::sqrt(2 * 0.7 * (std::max)(0.0, left))) : 0.0;
			aF = 0.8 * (vdes - vf);
			if (left > 6 && jet.AltTarget() < 5) jet.SetHold(5);   // cruise at 5 m or more (the hold rises over the ground ahead)
			status = "ПЕРЕЛЁТ · осталось " + N(left, 0) + " м";
			if (left < 0.8 && std::hypot(vf, vr) < 0.4)
			{
				jet.SetDescent(); mode = OFF; in.yaw = 0;
				status = "ПЕРЕЛЁТ · прибыла · посадка";
				cmdH = _V(0, 0, 0); return;
			}
		}
		else status = "ЗАВИС · снос " + N(std::hypot(vf, vr)) + " м/с";
		aF = std::clamp(aF, -2.0, 2.0); aR = std::clamp(aR, -2.0, 2.0);
		// the pods' tilt gives g * tan(tilt) sideways while the height hold keeps the lift; the pack's own limits
		const double maxT = (std::max)(5 * RAD, jet.MaxTilt()), maxS = (std::min)(20 * RAD, maxT);
		in.pitch = std::clamp(std::atan(aF / (std::max)(0.5, g)) / maxT, -1.0, 1.0);
		in.strafe = std::clamp(std::atan(aR / (std::max)(0.5, g)) / maxS, -1.0, 1.0);
		cmdH = _V(aF * sh + aR * ch, 0, aF * ch - aR * sh);
	}

	void Autopilot::Space(VESSEL* v, double dt)
	{
		VESSEL* o = oapiGetVesselInterface(tgt);
		VECTOR3 rel, relV; v->GetRelativePos(tgt, rel); v->GetRelativeVel(tgt, relV);   // her minus the target
		const double dist = length(rel);
		MATRIX3 R; v->GetRotationMatrix(R);
		auto body = [&](const VECTOR3& gv) { return tmul(R, gv); };
		VECTOR3 aG = _V(0, 0, 0), look = Unit(-rel), upRef;
		{ VECTOR3 r; v->GetRelativePos(v->GetGravityRef(), r); upRef = Unit(r); }
		switch (mode)
		{
		case SYNC:
			aG = Clamp(-relV * 0.5, 0.3);
			status = "СИНХР · Vотн " + N(length(relV), 2) + " м/с";
			if (length(relV) < 0.02) { mode = HOLD; holdRel = rel; status = "СИНХР · скорость уравнена → УДЕРЖ"; }
			break;
		case HOLD:
			aG = Clamp(-(rel - holdRel) * 0.05 - relV * 0.5, 0.3);
			status = "УДЕРЖ · уход " + N(length(rel - holdRel), 2) + " м";
			break;
		case APPROACH:
		{
			VECTOR3 P, D, U, goal;
			if (o && NearestPort(v, o, P, D, U)) goal = P + D * 3.0;      // 3 m out in front of the nearest port
			else { VECTOR3 it; oapiGetGlobalPos(tgt, &it); VECTOR3 me; v->GetGlobalPos(me); goal = it + Unit(me - it) * Standoff(tgt); }
			VECTOR3 me; v->GetGlobalPos(me);
			const VECTOR3 to = goal - me; const double d = length(to);
			double vc = std::clamp(d / 60, 0.05, 1.0); if (d < 30) vc = (std::min)(vc, 0.25);
			aG = Clamp((Unit(to) * vc - relV) * 0.6, 0.3);
			status = "СБЛИЖ · до точки " + N(d, d < 10 ? 1 : 0) + " м";
			if (d < 0.5 && length(relV) < 0.05) { mode = HOLD; holdRel = rel; status = "СБЛИЖ · на месте → УДЕРЖ"; }
			break;
		}
		case DOCK:
		{
			VECTOR3 P, D, U;
			if (!o || !NearestPort(v, o, P, D, U)) { Off(v, "у цели нет узлов"); return; }
			VECTOR3 me; v->GetGlobalPos(me);
			const VECTOR3 rp = me - P; const double ax = dotp(rp, D); const VECTOR3 lat = rp - D * ax;
			if (ax < 0) { Off(v, "она за плоскостью узла"); return; }
			const double lw = length(lat);
			double vax = 0;
			if (lw < 0.2 + 0.1 * ax) vax = -std::clamp(0.02 * ax + 0.03, 0.03, 0.25);
			else if (ax < 1.0) vax = 0.05;   // too close and off the axis: back off a little
			const VECTOR3 vdes = Clamp(-lat * 0.3, 0.3) + D * vax;
			aG = Clamp((vdes - relV) * 0.8, 0.3);
			look = -D; upRef = U;
			status = "СТЫК · по оси " + N(ax, 1) + " м · вбок " + N(lw * 100, 0) + " см";
			if (ax < 0.6 && lw < 0.15) { mode = HOLD; holdRel = rel; status = "СТЫК · у узла · держу"; }
			break;
		}
		default: break;
		}
		(void)dist;
		// attitude: face the target (or down the port axis), head to the reference "up"
		const VECTOR3 fb = body(look), ub = body(upRef);
		const double err[3] = { std::atan2(fb.y, std::hypot(fb.x, fb.z)), std::atan2(fb.x, fb.z), std::atan2(ub.x, ub.y) };
		double cmd[3];
		for (int i = 0; i < 3; ++i)
		{
			const double r = prevOk ? (err[i] - prevErr[i]) / dt : 0;
			errRate[i] += (r - errRate[i]) * (std::min)(1.0, dt / 0.25);
			prevErr[i] = err[i];
			cmd[i] = std::abs(err[i]) < 1 * RAD && std::abs(errRate[i]) < 0.5 * RAD ? 0.0 : std::clamp(1.2 * err[i] + 4.0 * errRate[i], -1.0, 1.0);
		}
		prevOk = true;
		Drive(v, body(aG), _V(cmd[0], cmd[1], cmd[2]));
		cmdH = aG;
	}
}
