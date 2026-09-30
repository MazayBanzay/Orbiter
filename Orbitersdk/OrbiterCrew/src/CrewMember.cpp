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
	}

	void CrewMember::clbkPostCreation()
	{
		air = atmospheres.Sample(this);
		if (!suitFromScenario) suitOn = !air.Breathable();
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
		if (!on)
		{
			std::string why;
			if (!air.Breathable(&why)) { Say("Cannot take the suit off: " + why); return; }
		}
		suitOn = on;
		if (on) suit.Seal();
		SetEmptyMass(Mass());
		ShowFigure();
		Say(on ? "Suit on, helmet sealed" : "Suit off");
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
		if (KEYMOD_CONTROL(kstate) || KEYMOD_ALT(kstate)) return 0;
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
		if (!(GetFlightStatus() & 1) && !airborne) return 0;
		if (key == OAPI_KEY_SPACE)
		{
			if (down && !airborne && !lying && bio.CanAct()) Jump();
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
	}

	void CrewMember::clbkPreStep(double simt, double simdt, double mjd)
	{
		const double dt = simdt;
		if (dt <= 0) return;
		air = atmospheres.Sample(this);
		const double g = Gravity();
		const bool landed = (GetFlightStatus() & 1) != 0;

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

		// ---- posture and movement ----
		if (airborne)
		{
			SetAngularVel(_V(0, 0, 0));   // stays upright in the air
			VECTOR3 gv; GetGroundspeedVector(FRAME_HORIZON, gv);
			fallSpeed = (std::max)(0.0, -gv.y);
			if (landed || GroundContact()) Land();
		}
		else if (landed)
		{
			if (!placed) { placed = true; Place(!bio.CanAct()); }
			if (!bio.CanAct() && !lying) { fwd = lat = turn = 0; Place(true); }
			else if (bio.CanAct() && lying) Place(false);
			if (bio.CanAct()) Drive(dt, g); else fwd = lat = turn = accel = 0;
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
			in.suited = suitOn; in.bound = boost ? std::clamp((std::hypot(fwd, lat) - 3.5) / 2.5, 0.0, 1.0) : 0.0;
			const bool firstPerson = oapiCameraInternal() && oapiCameraTarget() == GetHandle();
			fig.skin.SetHideHead(firstPerson);
			motion.Update(in, fig.clips, fig.skin);
			footfalls = motion.Footfalls();
			if (firstPerson) SetCameraOffset(fig.skin.Point(fig.skin.Bone("Head"), eye));   // the camera rides the head
		}
		// ---- sound ----
		CrewSound::Input si;
		si.dt = dt; si.footfalls = footfalls; si.runWeight = motion.RunWeight(); si.speed = std::hypot(fwd, lat);
		si.landing = landingSpeed; si.suited = suitOn; si.vacuum = air.p < 1; si.fanOn = suit.Powered();
		si.alive = bio.state != Body::DEAD; si.breathRate = bio.breath;
		si.intensity = std::clamp(0.8 * bio.Effort() + 0.5 * bio.Fatigue() + 0.6 * (1 - bio.reserve), 0.0, 1.0);
		sound.Update(si);

		landingSpeed = 0;
		if (messageTime > 0) messageTime -= dt;
	}

	bool CrewMember::clbkDrawHUD(int mode, const HUDPAINTSPEC* hps, oapi::Sketchpad* skp)
	{
		const DWORD NORMAL = 0x80FF80, DIM = 0x60A060, ALERT = 0x4040FF;
		const int x = hps->W / 40 + 4, dy = static_cast<int>(LOWORD(skp->GetCharSize()));
		int y = hps->H / 6;
		char b[256];
		auto line = [&](DWORD color, const char* text) { skp->SetTextColor(color); skp->Text(x, y, text, static_cast<int>(strlen(text))); y += dy; };

		static const char* STATE[] = { "", "  -  UNCONSCIOUS", "  -  DEAD" };
		snprintf(b, sizeof b, "%s  |  %s%s", name.c_str(), role.c_str(), STATE[bio.state]);
		line(bio.state == Body::OK ? NORMAL : ALERT, b);
		if (bio.state == Body::DEAD) return true;

		snprintf(b, sizeof b, "%s", suitOn ? (suitFig.ok ? "Space suit" : "Space suit (coverall mesh stands in)") : "Coverall");
		line(DIM, b);
		snprintf(b, sizeof b, "Pulse %d   Breath %d/min   Effort %d%%   Stamina %d%%", static_cast<int>(bio.pulse), static_cast<int>(bio.breath),
			static_cast<int>(100 * bio.Effort()), static_cast<int>(100 * bio.wbal));
		line(NORMAL, b);
		snprintf(b, sizeof b, "Breathing: O2 %.1f kPa   CO2 %.2f kPa", suitOn ? suit.ppO2 : air.ppO2, suitOn ? suit.ppCO2 : air.ppCO2);
		line(NORMAL, b);
		if (suitOn)
		{
			snprintf(b, sizeof b, "Suit: O2 %d%% (%.1f h)   Sorbent %d%%   Core %.1f C", static_cast<int>(100 * suit.o2 / suit.o2Cap), suit.HoursLeft(bio.O2Use()),
				static_cast<int>(100 * (1 - suit.sorbUsed / suit.sorbCap)), bio.coreT - 273.15);
			line(NORMAL, b);
			snprintf(b, sizeof b, "Power %d W (life %d, thermal %d, drives %d)   Battery %d%% (%.1f h)%s", static_cast<int>(suit.drawW),
				static_cast<int>(suit.Powered() ? suit.lifeW : 0), static_cast<int>(suit.thermalW), static_cast<int>(suit.driveW),
				static_cast<int>(100 * suit.batt / suit.battCap), suit.BattHours(), boost ? "   BOOST" : "");
			line(suit.Powered() ? NORMAL : ALERT, b);
			char ground[48] = "";
			if (thermal.groundT > 0) snprintf(ground, sizeof ground, "   ground %d C", static_cast<int>(thermal.groundT - 273.15));
			snprintf(b, sizeof b, "Surroundings %d C (rated %d..%d C)   %s%s", static_cast<int>(thermal.tEnv - 273.15), static_cast<int>(suit.tMin - 273.15),
				static_cast<int>(suit.tMax - 273.15), thermal.sunlit ? "sun" : "shadow", ground);
			line(suit.InSpec(thermal.tEnv) ? DIM : ALERT, b);
		}
		std::string why;
		const bool ok = air.Breathable(&why);
		if (air.body.empty()) snprintf(b, sizeof b, "Outside: vacuum");
		else snprintf(b, sizeof b, "Outside: %s %.1f kPa %d K, O2 %.1f kPa - %s", air.body.c_str(), air.p, static_cast<int>(air.T), air.ppO2, ok ? "breathable" : ("not breathable: " + why).c_str());
		line(DIM, b);
		snprintf(b, sizeof b, "Speed %.1f m/s", std::hypot(fwd, lat));
		line(DIM, b);

		std::string alert = bio.warning;
		if (alert.empty() && suitOn)
		{
			if (suit.batt <= 0) alert = "SUIT BATTERY DEAD - NO LIFE SUPPORT, NO DRIVES";
			else if (!suit.InSpec(thermal.tEnv)) alert = "SURROUNDINGS OUTSIDE THE SUIT RATING";
			else if (suit.o2 <= 0) alert = "SUIT O2 TANK EMPTY";
			else if (suit.o2 < 0.1 * suit.o2Cap) alert = "SUIT O2 LOW";
			else if (suit.sorbUsed >= suit.sorbCap) alert = "SORBENT SATURATED";
			else if (suit.batt < 0.1 * suit.battCap) alert = "SUIT BATTERY LOW";
		}
		if (!alert.empty()) line(ALERT, alert.c_str());
		if (messageTime > 0) line(NORMAL, message.c_str());
		y += dy / 2;
		line(DIM, suitOn ? "W/S move  A/D turn  Q/E step  Shift boost  Space jump  K suit" : "W/S move  A/D turn  Q/E step  Shift run  Space jump  K suit");
		return true;
	}
}

DLLCLBK VESSEL* ovcInit(OBJHANDLE hVessel, int fModel) { return new ocrew::CrewMember(hVessel, fModel); }
DLLCLBK void ovcExit(VESSEL* vessel) { delete static_cast<ocrew::CrewMember*>(vessel); }
