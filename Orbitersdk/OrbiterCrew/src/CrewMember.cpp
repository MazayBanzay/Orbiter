// OrbiterCrew - a crew member as an Orbiter vessel (see CrewMember.h).
#include "CrewMember.h"
#include "Surface.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace ocrew
{
	namespace
	{
		Atmospheres atmospheres;

		VESSELSTATUS2 Status(const VESSEL* v)
		{
			VESSELSTATUS2 s;
			memset(&s, 0, sizeof(s));
			s.version = 2;
			v->GetStatusEx(&s);
			return s;
		}

		double Sign(double x) { return x < 0 ? -1.0 : 1.0; }

		// move x towards target by at most rate*dt
		double Approach(double x, double target, double rate, double dt)
		{
			const double d = target - x, step = rate * dt;
			return std::abs(d) <= step ? target : x + Sign(d) * step;
		}

		const DWORD MOVE_KEYS[] = { OAPI_KEY_W, OAPI_KEY_S, OAPI_KEY_A, OAPI_KEY_D, OAPI_KEY_Q, OAPI_KEY_E, OAPI_KEY_LSHIFT, OAPI_KEY_RSHIFT };
	}

	CrewMember::CrewMember(OBJHANDLE hVessel, int fModel) : VESSEL4(hVessel, fModel) {}

	void CrewMember::clbkSetClassCaps(FILEHANDLE cfg)
	{
		if (!atmospheres.Loaded()) atmospheres.Load();
		char buf[256];
		double v;
		if (oapiReadItem_string(cfg, const_cast<char*>("Name"), buf)) name = buf;
		if (oapiReadItem_string(cfg, const_cast<char*>("Role"), buf)) role = buf;
		if (oapiReadItem_string(cfg, const_cast<char*>("Voice"), buf)) voice = buf;
		if (oapiReadItem_float(cfg, const_cast<char*>("BodyMass"), v)) bio.mass = v;
		if (oapiReadItem_float(cfg, const_cast<char*>("VO2max"), v)) bio.vo2max = v;
		oapiReadItem_float(cfg, const_cast<char*>("SuitMass"), suitMass);
		oapiReadItem_float(cfg, const_cast<char*>("StandHeight"), height);
		oapiReadItem_float(cfg, const_cast<char*>("WalkSpeed"), walkSpeed);
		oapiReadItem_float(cfg, const_cast<char*>("RunSpeed"), runSpeed);
		oapiReadItem_vec(cfg, const_cast<char*>("EyePos"), eye);
		if (oapiReadItem_float(cfg, const_cast<char*>("SuitO2"), v)) suit.o2 = suit.o2Cap = v;
		if (oapiReadItem_float(cfg, const_cast<char*>("SuitSorbent"), v)) suit.sorbCap = v;
		if (oapiReadItem_float(cfg, const_cast<char*>("SuitBattery"), v)) suit.batt = suit.battCap = v * 3.6e6;
		oapiReadItem_float(cfg, const_cast<char*>("SuitPressure"), suit.pressure);
		suit.Seal();

		auto load = [&](Figure& f, const char* skinKey, const char* clipKey)
		{
			char skin[256], clips[256];
			if (!oapiReadItem_string(cfg, const_cast<char*>(skinKey), skin) || !oapiReadItem_string(cfg, const_cast<char*>(clipKey), clips)) return;
			f.ok = f.skin.Load(skin) && f.clips.Load(clips);
			if (!f.ok) return;
			f.mesh = AddMesh(f.skin.MeshName().c_str());
			SetMeshVisibilityMode(f.mesh, MESHVIS_NEVER);
		};
		load(bodyFig, "BodySkin", "BodyClips");
		bodyFig.clips.walk.cubic = bodyFig.clips.run.cubic = true;   // coverall: smooth interpolation between mocap frames
		load(suitFig, "SuitSkin", "SuitClips");
		if (!bodyFig.ok) oapiWriteLogV("OrbiterCrew: %s has no usable BodySkin/BodyClips", GetClassNameA());

		SetSize(1.0);
		SetPMI(_V(0.15, 0.03, 0.15));
		SetCrossSections(_V(0.55, 0.35, 0.75));
		SetEmptyMass(Mass());
		SetCameraOffset(eye);

		// feet first (they define the stance), then head, shoulders, hips and chest for tumbles
		const double k = 2e4, d = 2.6e3;
		static TOUCHDOWNVTX td[8];
		const VECTOR3 pts[8] = { { 0, -height, 0.12 }, { -0.13, -height, -0.10 }, { 0.13, -height, -0.10 },
			{ 0, 0.80, 0 }, { -0.24, 0.48, 0 }, { 0.24, 0.48, 0 }, { 0, 0.30, 0.16 }, { 0, 0.30, -0.16 } };
		for (int i = 0; i < 8; ++i) td[i] = { pts[i], k, d, 1.5, 1.5 };
		SetTouchdownPoints(td, 8);
		SetupRcs();
		jet.Setup(this);

		// helmet lamps in the side pods (rest pose, model frame); they follow the torso at run time
		for (int i = 0; i < 2; ++i)
		{
			const VECTOR3 pos = _V(i ? -0.162 : 0.162, 0.707, 0.11), dir = unit(_V(0, -0.10, 1));
			lamps[i] = static_cast<SpotLight*>(AddSpotLight(pos, dir, 60, 0.5, 0, 2e-3, 18 * RAD, 40 * RAD,
				{ 1.0f, 0.97f, 0.90f, 0 }, { 1, 1, 1, 0 }, { 0, 0, 0, 0 }));
			lamps[i]->Activate(false);
			lampGlowPos[i] = pos + dir * 0.012;
			lampGlow[i] = { BEACONSHAPE_COMPACT, &lampGlowPos[i], &lampGlowCol, 0.035, 0.45, 0, 0, 0, false };
			AddBeacon(&lampGlow[i]);
		}
	}

	void CrewMember::clbkLoadStateEx(FILEHANDLE scn, void* status)
	{
		char* line;
		while (oapiReadScenario_nextline(scn, line))
		{
			std::istringstream ss(line);
			std::string key;
			ss >> key;
			if (key == "NAME") { std::getline(ss >> std::ws, name); }
			else if (key == "ROLE") { std::getline(ss >> std::ws, role); }
			else if (key == "SUIT") { ss >> suitOn; suitFromScenario = true; }
			else if (key == "SUIT_O2") ss >> suit.o2;
			else if (key == "SUIT_SORBENT") ss >> suit.sorbUsed;
			else if (key == "SUIT_BATTERY") { double kwh; ss >> kwh; suit.batt = kwh * 3.6e6; }
			else if (key == "STAMINA") ss >> bio.wbal;
			else if (key == "RESERVE") ss >> bio.reserve;
			else if (key == "SHADE") { ss >> shadeTarget; shade = shadeTarget; }
			else if (key == "LAMP") ss >> lampOn;
			else if (key == "JETPACK") ss >> jetFromScenario;
			else if (key == "BODY") { int st; ss >> st; bio.state = static_cast<Body::State>(std::clamp(st, 0, 2)); }
			else ParseScenarioLineEx(line, status);
		}
		suit.Seal();
	}

	void CrewMember::clbkSaveState(FILEHANDLE scn)
	{
		VESSEL4::clbkSaveState(scn);
		oapiWriteScenario_string(scn, const_cast<char*>("NAME"), const_cast<char*>(name.c_str()));
		oapiWriteScenario_string(scn, const_cast<char*>("ROLE"), const_cast<char*>(role.c_str()));
		oapiWriteScenario_int(scn, const_cast<char*>("SUIT"), suitOn);
		oapiWriteScenario_float(scn, const_cast<char*>("SUIT_O2"), suit.o2);
		oapiWriteScenario_float(scn, const_cast<char*>("SUIT_SORBENT"), suit.sorbUsed);
		oapiWriteScenario_float(scn, const_cast<char*>("SUIT_BATTERY"), suit.batt / 3.6e6);
		oapiWriteScenario_float(scn, const_cast<char*>("STAMINA"), bio.wbal);
		oapiWriteScenario_float(scn, const_cast<char*>("RESERVE"), bio.reserve);
		oapiWriteScenario_int(scn, const_cast<char*>("BODY"), static_cast<int>(bio.state));
		oapiWriteScenario_float(scn, const_cast<char*>("SHADE"), shadeTarget);
		oapiWriteScenario_int(scn, const_cast<char*>("LAMP"), lampOn);
		oapiWriteScenario_int(scn, const_cast<char*>("JETPACK"), jet.Worn());
	}

	void CrewMember::clbkPostCreation()
	{
		air = atmospheres.Sample(this);
		if (!suitFromScenario) suitOn = !air.Breathable();
		if (jetFromScenario && suitOn) jet.Wear(GetPropellantMass(jet.Propellant()));   // fuel came back with PRPLEVEL
		else SetPropellantMass(jet.Propellant(), 0);
		SetEmptyMass(Mass());
		ShowFigure();
		if (!suitFig.ok) oapiWriteLogV("OrbiterCrew: %s has no suit figure yet, the coverall stands in for it", name.c_str());
		sound.Init(this, voice);
	}

	void CrewMember::clbkVisualCreated(VISHANDLE v, int)
	{
		vis = v;
		if (bodyFig.ok) bodyFig.skin.Attach(this, vis, bodyFig.mesh);
		if (suitFig.ok) suitFig.skin.Attach(this, vis, suitFig.mesh);
	}

	void CrewMember::clbkVisualDestroyed(VISHANDLE v, int)
	{
		if (v != vis) return;
		vis = nullptr;
		bodyFig.skin.Detach(); suitFig.skin.Detach();
	}

	void CrewMember::ShowFigure()
	{
		Figure& on = Active();
		if (bodyFig.ok) SetMeshVisibilityMode(bodyFig.mesh, &on == &bodyFig ? MESHVIS_ALWAYS | MESHVIS_VC : MESHVIS_NEVER);
		if (suitFig.ok) SetMeshVisibilityMode(suitFig.mesh, &on == &suitFig ? MESHVIS_ALWAYS | MESHVIS_VC : MESHVIS_NEVER);
	}

	// without the suit there is nothing between her eyes and the world: the "virtual cockpit" is just her head,
	// with no instruments and no HUD. The suit gets its own helmet display later.
	bool CrewMember::clbkLoadVC(int) { return !suitOn; }

	void CrewMember::SetSuit(bool on)
	{
		if (!bio.CanAct()) return;
		if (!on && jet.Worn()) { Say("Сначала снимите ранец (B)"); return; }
		if (!on)
		{
			std::string why;
			if (!air.Breathable(&why)) { Say("Нельзя снять скафандр: " + why); return; }
		}
		suitOn = on;
		if (on) suit.Seal();
		SetEmptyMass(Mass());
		ShowFigure();
		Say(on ? "Скафандр надет, шлем загерметизирован" : "Скафандр снят");
	}

	double CrewMember::Gravity() const
	{
		OBJHANDLE ref = GetSurfaceRef();
		if (!ref) return 9.81;
		const double r = oapiGetSize(ref);
		return GGRAV * oapiGetMass(ref) / (r * r);
	}

	void CrewMember::Place(bool lie)
	{
		VESSELSTATUS2 s = Status(this);
		if (s.status != 1) return;
		lying = lie;
		if (lie) surface::Lie(s, 0.12); else surface::Stand(s, height);
		DefSetStateEx(&s);
	}

	int CrewMember::clbkConsumeDirectKey(char* kstate)
	{
		keys = {};
		// with the pack: Space up, left Ctrl down (read before Orbiter's Ctrl shortcuts are let through)
		const double vertical = jet.Worn() ? (KEYDOWN(kstate, OAPI_KEY_SPACE) ? 1.0 : 0.0) - (KEYDOWN(kstate, OAPI_KEY_LCONTROL) ? 1.0 : 0.0) : 0.0;
		if (jet.Worn()) RESETKEY(kstate, OAPI_KEY_SPACE);
		if (KEYMOD_ALT(kstate)) return 0;
		if (KEYMOD_CONTROL(kstate) && !jet.Worn()) return 0;
		if (jet.Worn())   // the pack: numpad 0 / . thrust everywhere; in the air W/S vector, A/D turn, Shift full vector
		{
			flight = {};
			flight.throttle = (KEYDOWN(kstate, OAPI_KEY_NUMPAD0) ? 1.0 : 0.0) - (KEYDOWN(kstate, OAPI_KEY_DECIMAL) ? 1.0 : 0.0);
			flight.vertical = vertical;
			flight.boost = KEYDOWN(kstate, OAPI_KEY_LSHIFT) || KEYDOWN(kstate, OAPI_KEY_RSHIFT);
			RESETKEY(kstate, OAPI_KEY_NUMPAD0); RESETKEY(kstate, OAPI_KEY_DECIMAL);
			if (!(GetFlightStatus() & 1) && !airborne)
			{
				flight.pitch = (KEYDOWN(kstate, OAPI_KEY_W) ? 1.0 : 0.0) - (KEYDOWN(kstate, OAPI_KEY_S) ? 1.0 : 0.0);
				flight.yaw = (KEYDOWN(kstate, OAPI_KEY_D) ? 1.0 : 0.0) - (KEYDOWN(kstate, OAPI_KEY_A) ? 1.0 : 0.0);
				flight.strafe = (KEYDOWN(kstate, OAPI_KEY_E) ? 1.0 : 0.0) - (KEYDOWN(kstate, OAPI_KEY_Q) ? 1.0 : 0.0);
				flight.boost = KEYDOWN(kstate, OAPI_KEY_LSHIFT) || KEYDOWN(kstate, OAPI_KEY_RSHIFT);
				for (DWORD k : MOVE_KEYS) RESETKEY(kstate, k);
			}
			flightFresh = true;
		}
		if (!(GetFlightStatus() & 1) && !airborne) return 0;   // in space the keys stay with Orbiter
		keys.fwd = KEYDOWN(kstate, OAPI_KEY_W) != 0;
		keys.back = KEYDOWN(kstate, OAPI_KEY_S) != 0;
		keys.left = KEYDOWN(kstate, OAPI_KEY_A) != 0;
		keys.right = KEYDOWN(kstate, OAPI_KEY_D) != 0;
		keys.stepL = KEYDOWN(kstate, OAPI_KEY_Q) != 0;
		keys.stepR = KEYDOWN(kstate, OAPI_KEY_E) != 0;
		keys.run = KEYDOWN(kstate, OAPI_KEY_LSHIFT) || KEYDOWN(kstate, OAPI_KEY_RSHIFT);
		for (DWORD k : MOVE_KEYS) RESETKEY(kstate, k);
		keysFresh = true;
		return 0;
	}

	int CrewMember::clbkConsumeBufferedKey(DWORD key, bool down, char* kstate)
	{
		if (KEYMOD_CONTROL(kstate) || KEYMOD_ALT(kstate)) return 0;
		if (key == OAPI_KEY_K) { if (down) SetSuit(!suitOn); return 1; }
		if (key == OAPI_KEY_V && suitOn) { if (down) { shadeTarget = shadeTarget > 0.5 ? 0 : 1; Say(shadeTarget > 0.5 ? "Щиток опущен" : "Щиток поднят"); } return 1; }
		if (key == OAPI_KEY_L && suitOn) { if (down) { lampOn = !lampOn; Say(lampOn ? "Фонари включены" : "Фонари выключены"); } return 1; }
		if (key == OAPI_KEY_B && suitOn) { if (down && bio.CanAct()) { if (jet.Worn()) DropPack(); else TakePack(); } return 1; }
		if (jet.Worn() && (key == OAPI_KEY_J || key == OAPI_KEY_C || key == OAPI_KEY_G || key == OAPI_KEY_INSERT || key == OAPI_KEY_DELETE))
		{
			if (down) jet.Key(key, (GetFlightStatus() & 1) != 0);
			return 1;
		}
		if (!(GetFlightStatus() & 1) && !airborne) return 0;
		if (key == OAPI_KEY_SPACE)
		{
			if (down && !airborne && !lying && bio.CanAct() && !jet.Worn()) Jump();   // with the pack Space is "up"
			return 1;
		}
		for (DWORD k : MOVE_KEYS) if (key == k) return 1;
		return 0;
	}

	void CrewMember::Drive(double dt, double g)
	{
		const Keys k = keysFresh ? keys : Keys{};
		// three profiles: coverall; suit with live drives (Shift = servo boost); suit with a flat battery (dead weight)
		const bool drives = suitOn && suit.Drives();
		double walkV = walkSpeed, runV = (std::min)(runSpeed, bio.RunLimit());
		if (drives) runV = 4.2 + 2.3 * std::sqrt(bio.wbal);
		else if (suitOn) { walkV = 1.0; runV = (std::min)(2.2, bio.RunLimit()); }
		boost = drives && k.run && k.fwd;

		// forward: accelerate within the grip of the soles, stop fast, brake hard on the opposite key
		double target = 0;
		if (k.fwd && !k.back) target = k.run ? runV : walkV;
		else if (k.back && !k.fwd) target = -0.9;
		const bool braking = (k.back && fwd > 0.05) || (k.fwd && fwd < -0.05);
		double nf;
		if (braking)
		{
			nf = fwd - Sign(fwd) * (std::min)(drives ? 9.0 : 7.5, 0.85 * g) * dt;   // live drives lock the knees into the brake
			if (nf * fwd <= 0) nf = 0;
		}
		else
		{
			const bool speedingUp = std::abs(target) > std::abs(fwd) && target * fwd >= 0;
			const double push = boost ? 4.5 : k.run ? 3.2 : 2.0;   // the drives add thrust at the push-off
			const double rate = speedingUp ? (std::min)(push, 0.5 * g * (boost ? 1.4 : 1.0)) : (std::min)(5.0, 0.7 * g);
			nf = Approach(fwd, target, rate, dt);
		}
		accel = (nf - fwd) / dt;
		fwd = nf;

		const double latTarget = (k.stepR - k.stepL) * 0.9;
		lat = Approach(lat, latTarget, std::abs(latTarget) > std::abs(lat) ? (std::min)(3.0, 0.5 * g) : (std::min)(5.0, 0.7 * g), dt);

		// turning: on the spot freely, at speed limited by the sideways grip (v * w <= 0.7 g)
		const double wmax = std::abs(fwd) < 0.3 ? 1.8 : (std::min)(1.8, 0.7 * g / std::abs(fwd));
		const double turnTarget = (k.right - k.left) * wmax;
		turn = Approach(turn, turnTarget, std::abs(turnTarget) > std::abs(turn) ? 10.0 : 14.0, dt);

		if (fwd || lat || turn)
		{
			VESSELSTATUS2 s = Status(this);
			s.surf_hdg = std::fmod(s.surf_hdg + turn * dt + PI2, PI2);
			surface::Walk(s, oapiGetSize(s.rbody), fwd * dt, lat * dt);
			surface::Stand(s, height);
			DefSetStateEx(&s);
		}
	}

	void CrewMember::Jump()
	{
		const VESSELSTATUS2 now = Status(this);
		OBJHANDLE ref = GetSurfaceRef();
		if (!ref || now.status != 1) return;
		jumpHeading = now.surf_hdg;

		VECTOR3 rpos, rvel;
		GetRelativePos(ref, rpos); GetRelativeVel(ref, rvel);
		const VECTOR3 up = unit(rpos);
		MATRIX3 R; GetRotationMatrix(R);
		const VECTOR3 f = mul(R, _V(0, 0, 1)), r = mul(R, _V(1, 0, 0));
		const bool drives = suitOn && suit.Drives();
		const double v0 = suitOn ? (drives ? 3.4 : 1.6) : 2.5 * (0.55 + 0.45 * bio.wbal);   // live drives jump higher than legs alone

		VESSELSTATUS2 s;
		memset(&s, 0, sizeof(s));
		s.version = 2;
		s.rbody = ref;
		s.status = 0;
		s.rpos = rpos + up * 0.03;
		s.rvel = rvel + up * v0 + f * fwd + r * lat;
		s.arot = surface::Euler(R);
		DefSetStateEx(&s);

		airborne = true;
		fallSpeed = 0;
		if (drives) { suit.batt = (std::max)(0.0, suit.batt - 0.5 * Mass() * v0 * v0 / 0.85); bio.Spend(300); }
		else bio.Spend(0.5 * Mass() * v0 * v0 / 0.25);   // muscles are about 25 % efficient
	}

	void CrewMember::GroundContactCheck(double dt)
	{
		// every body point against the relief under that very point (hills, crater walls), not only the soles
		OBJHANDLE body = GetSurfaceRef();
		if (!body) return;
		const double R = oapiGetSize(body);
		auto heightOf = [&](const VECTOR3& local)
		{
			VECTOR3 gp; Local2Global(local, gp);
			double lng, lat, rad; oapiGlobalToEqu(body, gp, &lng, &lat, &rad);
			return rad - R - oapiSurfaceElevation(body, lng, lat);
		};
		const double feet = (std::min)(heightOf(_V(0.12, -height, 0.05)), heightOf(_V(-0.12, -height, 0.05)));
		double other = 1e9;
		for (const VECTOR3& p : { _V(0, 0.80, 0.02), _V(0.12, -0.45, 0.08), _V(-0.12, -0.45, 0.08), _V(0.30, -0.05, 0.10),
			_V(-0.30, -0.05, 0.10), _V(0, 0.10, -0.26), _V(0, 0.0, 0.12) })
			other = (std::min)(other, heightOf(p));
		if (jet.Worn() && jet.Deploy() > 0.5)
			for (double sx : { 0.44, -0.44 }) other = (std::min)(other, heightOf(_V(sx, 0.43, -0.17)));

		// the ground's slope under her (horizon frame: x east, y up, z north) and her speed into it
		double lng, lat, rad; GetEquPos(lng, lat, rad);
		const double d = 1.0, e0 = oapiSurfaceElevation(body, lng, lat);
		const double dEast = oapiSurfaceElevation(body, lng + d / (R * (std::max)(0.05, std::cos(lat))), lat) - e0;
		const double dNorth = oapiSurfaceElevation(body, lng, lat + d / R) - e0;
		const VECTOR3 n = unit(_V(-dEast, d, -dNorth));
		VECTOR3 hv; GetGroundspeedVector(FRAME_HORIZON, hv);
		const double into = -dotp(hv, n);                                    // + towards the ground
		const double along = length(hv + n * into);
		VECTOR3 upG, uB; GetRelativePos(body, upG); GlobalRot(_V(0, 1, 0), uB);
		const double lean = std::acos(std::clamp(dotp(unit(upG), uB), -1.0, 1.0));

		if (other < 0.02) { Fall(length(hv), "Удар о грунт"); return; }      // head, knees, hands, pack: a fall, not a landing
		if (feet < 0.05 && into > -0.05)
		{
			if (along > 4.0 || lean > 30 * RAD) { Fall(into + 0.5 * along, "Падение при посадке"); return; }
			freeVy = -into;                                                  // the impact that counts is the one into the slope
			Touchdown();
		}
	}

	void CrewMember::Fall(double impact, const char* why)
	{
		jet.Touchdown();
		VESSELSTATUS2 s = Status(this);
		double lng, lat_, rad, hdg = 0;
		s.rbody = GetEquPos(lng, lat_, rad);
		oapiGetHeading(GetHandle(), &hdg);
		s.surf_lng = lng; s.surf_lat = lat_; s.surf_hdg = hdg;
		surface::Lie(s, 0.12);
		DefSetStateEx(&s);
		lying = true; fallenT = 2.5; wasFree = false; freeT = 0;
		fwd = lat = turn = accel = 0;
		bio.Impact(impact);
		landingSpeed = (std::min)(impact, 6.0);
		Say(bio.state == Body::DEAD ? "Смертельный удар" : why);
	}

	void CrewMember::Touchdown()
	{
		VESSELSTATUS2 s = Status(this);
		double lng, lat_, rad, hdg = 0;
		s.rbody = GetEquPos(lng, lat_, rad);
		oapiGetHeading(GetHandle(), &hdg);
		s.surf_lng = lng; s.surf_lat = lat_; s.surf_hdg = hdg;
		surface::Stand(s, height);
		DefSetStateEx(&s);
		landingSpeed = (std::max)(0.0, -freeVy);
		bio.Impact(landingSpeed);
		jet.Touchdown();
		wasFree = false; freeT = 0;
		if (landingSpeed > 7) Say(bio.state == Body::DEAD ? "Смертельный удар" : "Жёсткая посадка - травма");
	}

	void CrewMember::Liftoff()
	{
		// from standing to free flight: the same hand-over as a jump, with no push of her own - the pods do the lifting
		OBJHANDLE ref = GetSurfaceRef();
		if (!ref) return;
		VECTOR3 rpos, rvel; GetRelativePos(ref, rpos); GetRelativeVel(ref, rvel);
		MATRIX3 R; GetRotationMatrix(R);
		VESSELSTATUS2 s;
		memset(&s, 0, sizeof(s));
		s.version = 2; s.rbody = ref; s.status = 0;
		s.rpos = rpos + unit(rpos) * 0.03;
		s.rvel = rvel + unit(rpos) * 0.2;
		s.arot = surface::Euler(R);
		DefSetStateEx(&s);
		fwd = lat = turn = 0;
		wasFree = true;
		Say("Взлёт");
	}

	Thermal CrewMember::Surroundings() const
	{
		const double SIGMA = 5.670e-8, ALPHA = 0.20, EPS = 0.85;   // white outer layer: absorbs little sunlight, radiates well
		Thermal t;
		OBJHANDLE sun = oapiGetGbodyByIndex(0), ref = GetSurfaceRef();
		VECTOR3 p, s, c{};
		GetGlobalPos(p); oapiGetGlobalPos(sun, &s);
		const VECTOR3 d = s - p; const double dist = length(d); const VECTOR3 u = d / dist;
		t.sunFlux = 3.828e26 / (4 * PI * dist * dist);
		double elev = PI05;
		if (ref)
		{
			oapiGetGlobalPos(ref, &c);
			const double R = oapiGetSize(ref), along = dotp(c - p, u);
			if (along > 0 && length((c - p) - u * along) < R) t.sunlit = false;   // the planet is between her and the Sun
			elev = std::asin(dotp(unit(p - c), u));
		}
		if (air.p > 1) { t.tEnv = air.T + (t.sunlit ? 6 : 0); return t; }   // in air the suit mostly exchanges heat with the gas
		double q = t.sunlit ? ALPHA * t.sunFlux * 0.3 : 0;   // about a third of her surface faces the Sun
		if (ref && ((GetFlightStatus() & 1) || GetAltitude() < 1000))
		{
			// airless ground: hot regolith by day, cold by night; she sees it with half of her surface
			const double scale = std::pow(t.sunFlux / 1361, 0.25);
			t.groundT = t.sunlit && elev > 0 ? 100 + 290 * std::pow(std::sin(elev), 0.25) * scale : 100;
			q += 0.5 * 0.95 * SIGMA * std::pow(t.groundT, 4);
		}
		q += 0.5 * SIGMA * std::pow(2.7, 4);
		t.tEnv = std::pow(q / (EPS * SIGMA), 0.25);
		return t;
	}

	void CrewMember::Land()
	{
		VESSELSTATUS2 s = Status(this);
		double lng, lat_, rad;
		s.rbody = GetEquPos(lng, lat_, rad);
		s.surf_lng = lng; s.surf_lat = lat_; s.surf_hdg = jumpHeading;
		surface::Stand(s, height);
		DefSetStateEx(&s);
		airborne = false;
		landingSpeed = fallSpeed;
		bio.Impact(landingSpeed);
	}

	void CrewMember::TakePack()
	{
		// the nearest jet pack within reach becomes part of her
		OBJHANDLE best = nullptr; double bestD = 2.5;
		for (DWORD i = 0; i < oapiGetVesselCount(); ++i)
		{
			OBJHANDLE h = oapiGetVesselByIndex(i);
			if (h == GetHandle()) continue;
			VESSEL* pv = oapiGetVesselInterface(h);
			if (!strstr(pv->GetClassNameA(), "JetPack")) continue;
			VECTOR3 d; GetRelativePos(h, d);
			if (length(d) < bestD) { bestD = length(d); best = h; }
		}
		if (!best) { Say("Ранца рядом нет"); return; }
		VESSEL* pv = oapiGetVesselInterface(best);
		const double fuel = pv->GetPropellantMass(pv->GetPropellantHandleByIndex(0));
		oapiDeleteVessel(best, GetHandle());
		jet.Wear(fuel);
		rcsState = -1;
		SetEmptyMass(Mass());
		Say("Ранец надет");
	}

	void CrewMember::DropPack()
	{
		if (jet.Deploy() > 0.02 && !(GetFlightStatus() & 1)) { Say("Сначала сложите балки (G)"); return; }
		const double fuel = jet.Remove();
		rcsState = -1;
		SetEmptyMass(Mass());
		VESSELSTATUS2 s = Status(this);
		const bool landed = s.status == 1;
		if (landed)
		{
			surface::Walk(s, oapiGetSize(s.rbody), -0.7, 0);   // set down behind her
			surface::Stand(s, ShellHalf());
		}
		else
		{
			MATRIX3 R; GetRotationMatrix(R);
			s.rpos = s.rpos + mul(R, jet.ShellCentre());
			s.rvel = s.rvel + mul(R, _V(0, 0, -0.3));        // a gentle push away behind her
			s.status = 0;
		}
		char nm[64];
		for (int i = 1; ; ++i) { snprintf(nm, sizeof nm, "JetPack%d", i); if (!oapiGetVesselByName(nm)) break; }
		OBJHANDLE h = oapiCreateVesselEx(nm, "OrbiterCrew\\JetPack", &s);
		if (h)
		{
			VESSEL* pv = oapiGetVesselInterface(h);
			pv->SetPropellantMass(pv->GetPropellantHandleByIndex(0), fuel);
		}
		Say("Ранец снят");
	}

	void CrewMember::UpdateHelmet(double dt, Figure& fig)
	{
		// sun shade: about 0.8 s to travel; turned about the helmet centre's lateral axis (rest pose, model frame)
		shade = Approach(shade, suitOn ? shadeTarget : 0.0, 1.25, dt);
		if (&fig == &suitFig)
			for (const char* part : { "SunShade", "ShadeArms" })   // the hinge axis runs through the helmet centre and the side pods
				fig.skin.MovePart(part, _V(0, 0.695, 0.045), _V(1, 0, 0), static_cast<float>(-(1 - shade) * 1.12));
		// lamps: need the suit's power; they ride the upper torso like the helmet
		const bool live = lampOn && suitOn && suit.Powered();
		suit.lampW = live ? 2 * 8.0 : 0.0;
		const int spine = fig.skin.Bone("Spine1");
		for (int i = 0; i < 2; ++i)
		{
			if (!lamps[i]) continue;
			lamps[i]->Activate(live);
			lampGlow[i].active = live;
			if (!live || spine < 0) continue;
			const VECTOR3 rest = _V(i ? -0.162 : 0.162, 0.707, 0.11), p = fig.skin.Point(spine, rest);
			lamps[i]->SetPosition(p);
			lampGlowPos[i] = fig.skin.Point(spine, rest + _V(0, -0.0012, 0.012));
			static_cast<SpotLight*>(lamps[i])->SetDirection(unit(fig.skin.Point(spine, rest + _V(0, -0.10, 1)) - p));
		}
	}

	void CrewMember::SetupRcs()
	{
		// 1.5 kg of nitrogen at ~30 MPa; cold gas, exhaust ~690 m/s; 20 nozzles of ~3.6 N in four clusters on the pack.
		// For the forces the corners sit symmetric about the centre of mass (torques cancel in translation);
		// the plumes leave the RCS blocks on the pack.
		const double ISP = 686, F = 3.6, PX = 0.22, PY = 0.20, PZ = 0.0;   // physics: symmetric about the CG, so translation adds no torque
		n2 = CreatePropellantResource(1.5);
		// cold nitrogen in vacuum: a short faint jet that thins out at once
		static PARTICLESTREAMSPEC gas = { 0, 0.004, 30, 25, 0.08, 0.10, 0.6, 1.5, PARTICLESTREAMSPEC::DIFFUSE,
			PARTICLESTREAMSPEC::LVL_PSQRT, 0, 0.35, PARTICLESTREAMSPEC::ATM_FLAT, 1, 1, 0 };
		const VECTOR3 dirs[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
		THRUSTER_HANDLE th[2][2][6];   // [x side][y side][direction]
		for (int ix = 0; ix < 2; ++ix)
			for (int iy = 0; iy < 2; ++iy)
				for (int k = 0; k < 6; ++k)
				{
					const double sx = ix ? 1 : -1, sy = iy ? 1 : -1;
					// sideways thrust only from the nozzles that face outwards (right push from the left blocks, and back)
					if ((k == 0 && ix == 1) || (k == 1 && ix == 0)) { th[ix][iy][k] = nullptr; continue; }
					th[ix][iy][k] = CreateThruster(_V(sx * PX, sy * PY, PZ), dirs[k], F, n2, ISP);
					rcsN2.th[ix][iy][k] = th[ix][iy][k]; rcsN2.all.push_back(th[ix][iy][k]);
					const VECTOR3 block = _V(sx * 0.238, iy ? 0.535 : 0.165, -0.17);   // nozzle cluster on the pack
					AddExhaustStream(th[ix][iy][k], block - dirs[k] * 0.024, &gas);
					rcs.push_back(th[ix][iy][k]);
				}
		BuildAttGroups(rcsN2);
		rcsLive = true;
	}

	void CrewMember::BuildAttGroups(const RcsSet& set)
	{
		// the numpad drives whichever RCS she wears: the suit's N2 unit, or the jet pack's ports when it has fuel
		const auto& th = set.th;
		enum { PX_ = 0, NX, PY_, NY, PZ_, NZ };
		for (THGROUP_TYPE t : { THGROUP_ATT_RIGHT, THGROUP_ATT_LEFT, THGROUP_ATT_UP, THGROUP_ATT_DOWN, THGROUP_ATT_FORWARD, THGROUP_ATT_BACK,
			THGROUP_ATT_PITCHUP, THGROUP_ATT_PITCHDOWN, THGROUP_ATT_YAWLEFT, THGROUP_ATT_YAWRIGHT, THGROUP_ATT_BANKLEFT, THGROUP_ATT_BANKRIGHT })
			DelThrusterGroup(t);
		auto group = [&](THGROUP_TYPE type, std::initializer_list<THRUSTER_HANDLE> list)
		{
			std::vector<THRUSTER_HANDLE> v(list); CreateThrusterGroup(v.data(), static_cast<int>(v.size()), type);
		};
		// translation
		group(THGROUP_ATT_RIGHT, { th[0][0][PX_], th[0][1][PX_] });
		group(THGROUP_ATT_LEFT, { th[1][0][NX], th[1][1][NX] });
		group(THGROUP_ATT_UP, { th[0][0][PY_], th[0][1][PY_], th[1][0][PY_], th[1][1][PY_] });
		group(THGROUP_ATT_DOWN, { th[0][0][NY], th[0][1][NY], th[1][0][NY], th[1][1][NY] });
		group(THGROUP_ATT_FORWARD, { th[0][0][PZ_], th[0][1][PZ_], th[1][0][PZ_], th[1][1][PZ_] });
		group(THGROUP_ATT_BACK, { th[0][0][NZ], th[0][1][NZ], th[1][0][NZ], th[1][1][NZ] });
		// rotation: couples of fore/aft forces at the corners (torque = r x F)
		group(THGROUP_ATT_PITCHUP, { th[0][1][NZ], th[1][1][NZ], th[0][0][PZ_], th[1][0][PZ_] });     // top back, bottom forward
		group(THGROUP_ATT_PITCHDOWN, { th[0][1][PZ_], th[1][1][PZ_], th[0][0][NZ], th[1][0][NZ] });
		group(THGROUP_ATT_YAWLEFT, { th[1][0][PZ_], th[1][1][PZ_], th[0][0][NZ], th[0][1][NZ] });     // right forward, left back
		group(THGROUP_ATT_YAWRIGHT, { th[0][0][PZ_], th[0][1][PZ_], th[1][0][NZ], th[1][1][NZ] });
		group(THGROUP_ATT_BANKLEFT, { th[1][0][PY_], th[1][1][PY_], th[0][0][NY], th[0][1][NY] });    // right up, left down
		group(THGROUP_ATT_BANKRIGHT, { th[0][0][PY_], th[0][1][PY_], th[1][0][NY], th[1][1][NY] });
	}

	void CrewMember::UpdateRcs(bool free)
	{
		// valves need the suit's power; no pack on the coverall; on the ground and in a jump the numpad does nothing
		const bool live = free && suitOn && suit.Powered() && bio.CanAct();
		const bool jetSet = jet.Worn() && jet.Fuel() > 0;
		const int state = (live ? 1 : 0) + (jetSet ? 2 : 0);
		if (state == rcsState) return;
		if (rcsState < 0 || ((rcsState & 2) != 0) != jetSet) BuildAttGroups(jetSet ? jet.Rcs() : rcsN2);
		rcsState = state; rcsLive = live;
		for (THRUSTER_HANDLE t : rcs) SetThrusterResource(t, live && !jetSet ? n2 : nullptr);
		for (THRUSTER_HANDLE t : jet.Rcs().all) SetThrusterResource(t, live && jetSet ? jet.Propellant() : nullptr);
		if (!live) SetAttitudeMode(RCS_NONE);
	}

	double CrewMember::RcsDeltaV() const
	{
		const double m = GetMass(), p = n2 ? GetPropellantMass(n2) : 0;
		return p > 0 && m > p ? 686 * std::log(m / (m - p)) : 0;
	}

	void CrewMember::clbkPreStep(double simt, double simdt, double mjd)
	{
		const double dt = simdt;
		if (dt <= 0) return;
		air = atmospheres.Sample(this);
		const double g = Gravity();
		const bool landed = (GetFlightStatus() & 1) != 0;
		UpdateRcs(!landed && !airborne);

		// ---- life support ----
		// her legs pay for what the drives do not carry; the drives pay in watts (motors ~85 %, muscle ~25 % efficient)
		const double v = std::hypot(fwd, lat);
		const bool drives = suitOn && suit.Drives();
		driveDemandW = 0;
		if (!suitOn) humanW = LocomotionPower(Mass(), v, g);
		else if (drives)
		{
			const double carried = LocomotionPower(Mass(), v, g), own = LocomotionPower(bio.mass, (std::min)(v, 3.2), g);
			humanW = own; driveDemandW = 25 + (std::max)(0.0, carried - own) * 0.3;
		}
		else humanW = LocomotionPower(Mass(), v, g) * 1.35;   // dead drives: extra mass and stiff joints
		thermal = Surroundings();
		double heat;
		if (suitOn) heat = suit.Step(dt, bio.O2Use(), bio.CO2Made(), driveDemandW, bio.Heat(), thermal.tEnv);
		else
		{
			const double net = bio.Heat() + 8 * (air.p > 1 ? air.T - 295 : 0);   // unsuited: her own regulation covers +-150 W
			heat = net > 150 ? net - 150 : net < -150 ? net + 150 : 0;
		}
		bio.Step(dt, humanW + (turn ? 25 : 0), suitOn ? suit.ppO2 : air.ppO2, suitOn ? suit.ppCO2 : air.ppCO2, air.p, suitOn, heat);

		// ---- jet pack ----
		const double altFeet = GetAltitude(ALTMODE_GROUND) - height;
		jet.Update(dt, landed, suitOn && bio.CanAct() && suit.Powered(), g, altFeet, flightFresh ? flight : FlightInput{});
		flightFresh = false;
		freeT = (!landed && !airborne) ? freeT + dt : 0;
		if (!landed && !airborne && freeT > 0.3 && jet.SurfaceMode()) GroundContactCheck(dt);
		if (fallenT > 0) fallenT -= dt;
		if (!landed && !airborne) { VECTOR3 hv; GetGroundspeedVector(FRAME_HORIZON, hv); freeVy = hv.y; }
		if (landed && wasFree) { landingSpeed = (std::max)(0.0, -freeVy); bio.Impact(landingSpeed); Place(!bio.CanAct()); }   // back on her feet after a flight
		wasFree = !landed && !airborne;

		if (airborne && jet.Thrust() > 0) airborne = false;                                   // the jump turns into a flight
		bool lifted = false;
		if (landed && !airborne && bio.CanAct() && jet.Thrust() > GetMass() * g * 1.02) { Liftoff(); lifted = true; }

		// ---- posture and movement ----
		if (airborne)
		{
			SetAngularVel(_V(0, 0, 0));   // stays upright in the air
			VECTOR3 gv; GetGroundspeedVector(FRAME_HORIZON, gv);
			fallSpeed = (std::max)(0.0, -gv.y);
			if (landed || GroundContact()) Land();
		}
		else if (landed && !lifted)
		{
			if (!placed) { placed = true; Place(!bio.CanAct()); }
			if (!bio.CanAct() && !lying) { fwd = lat = turn = 0; Place(true); }
			else if (bio.CanAct() && lying && fallenT <= 0) Place(false);   // gets up once the fall is over
			if (bio.CanAct() && !lying) Drive(dt, g); else fwd = lat = turn = accel = 0;
		}
		else fwd = lat = turn = accel = 0;
		keysFresh = false;

		// ---- no HUD without the suit (the generic cockpit has one; switch it off, give it back with the suit) ----
		const bool inHead = oapiCameraInternal() && oapiCameraTarget() == GetHandle();
		if (inHead && !suitOn && !hudHidden && oapiGetHUDMode() != HUD_NONE) { oapiSetHUDMode(HUD_NONE); hudHidden = true; }
		else if (hudHidden && (suitOn || !inHead)) { if (suitOn && inHead) oapiSetHUDMode(HUD_SURFACE); hudHidden = false; }

		// ---- animation ----
		int footfalls = 0;
		Figure& fig = Active();
		if (vis && fig.ok && fig.skin.Attached())
		{
			MotionInput in;
			in.dt = dt; in.fwd = fwd; in.lat = lat; in.turn = turn; in.accel = accel; in.g = g;
			in.grounded = !airborne && landed && !lying;
			in.landing = landingSpeed;
			in.effort = (std::min)(1.0, bio.Effort()); in.fatigue = bio.Fatigue(); in.breathRate = bio.breath;
			in.walkTop = walkSpeed * (suitOn ? 0.9 : 1.0);
			in.suited = suitOn; in.bound = boost ? std::clamp((std::hypot(fwd, lat) - 3.5) / 2.5, 0.0, 1.0) : 0.0;
			in.floating = !landed && !airborne;
			if (in.floating) GetAngularVel(in.angVel);
			if (in.floating) { VECTOR3 f; GetThrustVector(f); in.thrustAcc = f / GetMass(); GetAngularAcc(in.angAcc); }
			const bool firstPerson = oapiCameraInternal() && oapiCameraTarget() == GetHandle();
			fig.skin.SetHideHead(firstPerson);
			UpdateHelmet(dt, fig);
			if (&fig == &suitFig) jet.Pose(fig.skin);
			in.jet = jet.Worn();
			motion.Update(in, fig.clips, fig.skin);
			footfalls = motion.Footfalls();
			if (firstPerson) SetCameraOffset(fig.skin.Point(fig.skin.Bone("Head"), eye));   // the camera rides the head
		}
		// ---- sound ----
		CrewSound::Input si;
		si.dt = dt; si.footfalls = footfalls; si.runWeight = motion.RunWeight(); si.speed = std::hypot(fwd, lat);
		si.landing = landingSpeed; si.suited = suitOn; si.vacuum = air.p < 1; si.fanOn = suit.Powered();
		si.fanLoad = suit.Powered() && suitOn ? std::clamp(suit.thermalW / (suit.coolMaxW / suit.copCool), 0.0, 1.0) : 0.0;   // full cooling = full speed
		si.alive = bio.state != Body::DEAD; si.breathRate = bio.breath;
		si.intensity = std::clamp(0.8 * bio.Effort() + 0.5 * bio.Fatigue() + 0.6 * (1 - bio.reserve), 0.0, 1.0);
		sound.Update(si);

		landingSpeed = 0;
		if (messageTime > 0) messageTime -= dt;
	}

	bool CrewMember::clbkDrawHUD(int mode, const HUDPAINTSPEC* hps, oapi::Sketchpad* skp)
	{
		static const std::pair<const char*, const char*> WARN[] = {
			{ "VACUUM - NO SUIT", "ВАКУУМ БЕЗ СКАФАНДРА" }, { "OVERHEATING", "ПЕРЕГРЕВ" }, { "HYPOTHERMIA", "ПЕРЕОХЛАЖДЕНИЕ" },
			{ "HYPOXIA", "ГИПОКСИЯ" }, { "LOW OXYGEN", "МАЛО КИСЛОРОДА" }, { "CO2 NARCOSIS", "ОТРАВЛЕНИЕ CO2" },
			{ "HIGH CO2", "ВЫСОКИЙ CO2" }, { "EXHAUSTED", "ИСТОЩЕНИЕ" }, { "INJURED", "ТРАВМА" } };
		HudData d;
		d.name = name; d.role = role == "astronavigator" ? "астронавигатор" : role; d.state = static_cast<int>(bio.state); d.suit = suitOn;
		d.pulse = bio.pulse; d.breath = bio.breath; d.effort = (std::min)(1.0, bio.Effort()); d.stamina = bio.wbal;
		d.ppO2 = suitOn ? suit.ppO2 : air.ppO2; d.ppCO2 = suitOn ? suit.ppCO2 : air.ppCO2; d.coreC = bio.coreT - 273.15; d.injury = (std::min)(1.0, bio.injury);
		d.o2 = suit.o2 / suit.o2Cap; d.o2Hours = suit.HoursLeft(bio.O2Use()); d.sorbent = 1 - suit.sorbUsed / suit.sorbCap;
		d.batt = suit.batt / suit.battCap; d.battHours = suit.BattHours(); d.powerW = suit.drawW; d.lifeW = suit.Powered() ? suit.lifeW : 0;
		d.thermalW = suit.thermalW; d.driveW = suit.driveW; d.powered = suit.Powered();
		d.envC = thermal.tEnv - 273.15; d.ratedMinC = suit.tMin - 273.15; d.ratedMaxC = suit.tMax - 273.15; d.sunlit = thermal.sunlit;
		d.inSpec = suit.InSpec(thermal.tEnv); d.hasGround = thermal.groundT > 0; d.groundC = thermal.groundT - 273.15;
		d.body = air.body; d.vacuum = air.body.empty() || air.p < 0.01; d.airKPa = air.p; d.airC = air.T - 273.15; d.breathable = air.Breathable();
		d.n2 = n2 ? GetPropellantMass(n2) / 1.5 : 0; d.n2Dv = RcsDeltaV();
		d.rcs = rcsLive ? 1 : ((GetFlightStatus() & 1) || airborne) ? 0 : 2;
		d.shadeDown = shadeTarget > 0.5; d.lampsOn = lampOn && suit.Powered();
		d.speed = std::hypot(fwd, lat); d.simt = oapiGetSimTime();
		jet.Fill(d);
		std::string w = bio.warning;
		if (w.empty() && suitOn)
		{
			if (suit.batt <= 0) w = "БАТАРЕЯ РАЗРЯЖЕНА - НЕТ ЖИЗНЕОБЕСПЕЧЕНИЯ";
			else if (!suit.InSpec(thermal.tEnv)) w = "СРЕДА ВНЕ ДОПУСКА СКАФАНДРА";
			else if (suit.o2 <= 0) w = "КИСЛОРОД КОНЧИЛСЯ";
			else if (suit.o2 < 0.1 * suit.o2Cap) w = "МАЛО КИСЛОРОДА";
			else if (suit.sorbUsed >= suit.sorbCap) w = "ПОГЛОТИТЕЛЬ НАСЫЩЕН";
			else if (suit.batt < 0.1 * suit.battCap) w = "БАТАРЕЯ НА ИСХОДЕ";
		}
		for (const auto& p : WARN) if (w == p.first) w = p.second;
		d.warning = w;
		if (messageTime > 0) d.message = message;
		hud.Draw(skp, hps, d);
		return true;
	}
}

DLLCLBK VESSEL* ovcInit(OBJHANDLE hVessel, int fModel) { return new ocrew::CrewMember(hVessel, fModel); }
DLLCLBK void ovcExit(VESSEL* vessel) { delete static_cast<ocrew::CrewMember*>(vessel); }
