// OrbiterCrew - a crew member as an Orbiter vessel (see CrewMember.h).
#include "CrewMember.h"
#include <gcCoreAPI.h>
#include "../include/OrbiterCrewApi.h"
#include "Surface.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <fstream>
#include <map>

// oapiCameraAttach modes (OrbiterAPI.h: 0 internal, 1 external) - once used the other way round, by mistake
const int CAM_INSIDE = 0, CAM_OUTSIDE = 1;

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

	namespace { std::vector<CrewMember*> everyone; }   // for the suit computer: names of the other crew
	std::vector<CrewMember*>& everyoneRef() { return everyone; }
	// ships that told OrbiterCrew about their insides (ocRegisterInterior): their entrances, seats, terminals
	struct ShipInterior
	{
		OBJHANDLE ship; OcInterior fns; void* ctx; OcInteriorExt ext{};
		VECTOR3 Origin() const { VECTOR3 o{}; if (ext.Origin) ext.Origin(ctx, &o); return o; }
	};
	std::vector<ShipInterior>& interiors() { static std::vector<ShipInterior> v; return v; }
	ShipInterior* InteriorOf(OBJHANDLE ship) { for (ShipInterior& s : interiors()) if (s.ship == ship) return &s; return nullptr; }
	struct PendingPlace { OBJHANDLE ship{}; VECTOR3 feet{}; double hdg{}; };
	PendingPlace& PendingInterior() { static PendingPlace p; return p; }
	// the ship's label (its own code page, windows-1251) to UTF-8 for our HUD
	std::string Utf8(const char* ansi)
	{
		wchar_t w[128]; char u[384];
		const int n = MultiByteToWideChar(1251, 0, ansi, -1, w, 128);
		if (n <= 0) return ansi;
		return WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, nullptr, nullptr) > 0 ? std::string(u) : std::string(ansi);
	}

	namespace
	{
		// a base's landing pads as its config draws them (Orbiter's API gives only their centres): LPAD1 is an octagon,
		// LPAD2/LPAD2A a square, 80 m across at scale 1 (OrbiterConfig.pdf), turned by ROT. Read once per base.
		struct PadShape { double half{ 40 }; bool square{}; double rot{}; };
		const std::vector<PadShape>& PadShapes(OBJHANDLE base, OBJHANDLE planet)
		{
			static std::map<OBJHANDLE, std::vector<PadShape>> cache;
			if (auto it = cache.find(base); it != cache.end()) return it->second;
			std::vector<PadShape>& out = cache[base];
			char bname[256] = "", pname[256] = "";
			oapiGetObjectName(base, bname, 255); oapiGetObjectName(planet, pname, 255);
			const std::string dir = std::string("Config\\") + pname + "\\Base\\";
			WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA((dir + "*.cfg").c_str(), &fd);
			if (h == INVALID_HANDLE_VALUE) return out;
			do
			{
				std::ifstream f(dir + fd.cFileName); std::string line; bool ours = false, inPad = false; PadShape cur;
				std::vector<PadShape> pads;
				while (std::getline(f, line))
				{
					std::istringstream ss(line); std::string w; ss >> w;
					if (w == "Name") { std::string eq, rest; ss >> eq; std::getline(ss >> std::ws, rest); while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' ')) rest.pop_back(); ours = rest == bname; }
					else if (w == "LPAD1" || w == "LPAD2" || w == "LPAD2A") { inPad = true; cur = PadShape{}; cur.square = w != "LPAD1"; }
					else if (inPad && w == "SCALE") { double sc = 1; if (ss >> sc && sc > 0) cur.half = 40 * sc; }
					else if (inPad && w == "ROT") ss >> cur.rot;
					else if (inPad && w == "END") { pads.push_back(cur); inPad = false; }
				}
				if (ours) { out = pads; break; }
			} while (FindNextFileA(h, &fd));
			FindClose(h);
			return out;
		}
		// is a point (m east, m north of the pad centre) on that pad
		bool OnPad(const PadShape& p, double east, double north)
		{
			const double r = p.rot * RAD, x = std::abs(east * std::cos(r) - north * std::sin(r)), y = std::abs(east * std::sin(r) + north * std::cos(r));
			return p.square ? (std::max)(x, y) <= p.half : (std::max)({ x, y, (x + y) / std::sqrt(2.0) }) <= p.half;
		}
	}
	CrewMember::CrewMember(OBJHANDLE hVessel, int fModel) : VESSEL4(hVessel, fModel),
		who(Crew::Claim()), name(who.name), role(who.role), sex(who.sex), age(who.age), heightM(who.heightM), bio(who.body),
		suitOn(who.worn.suit.on), suitFromScenario(who.worn.suit.fromScenario), suitMass(who.worn.suit.mass), suit(who.worn.suit.life),
		jet(*who.worn.pack), hud(who.worn.suit.computer->hud), ap(who.worn.suit.computer->ap), jetFromScenario(who.worn.packFromScenario)
	{
		everyone.push_back(this);
		reborn = Crew::ClaimedExisting();
		if (reborn && PendingInterior().ship) { inShip = PendingInterior().ship; inFeet = PendingInterior().feet; inHdg = PendingInterior().hdg; attachWait = 3; }
		PendingInterior() = {};
		if (reborn) { suitFromScenario = true; jetFromScenario = true; }   // what is worn keeps its state
		who.where = inShip ? Person::INTERIOR : Person::IN_WORLD; who.vessel = hVessel; if (inShip) who.ship = inShip;
	}

	CrewMember::~CrewMember()
	{
		everyone.erase(std::remove(everyone.begin(), everyone.end(), this), everyone.end());
		// the body leaves the world; the person stays in the registry with everything he or she is and wears:
		// the worn items take their state back from the body's parts (fuel, cold gas) and let go of them
		jet.Detach();
		who.worn.suit.computer->Detach();
		if (inShip) LeaveShip();
		if (who.where == Person::IN_WORLD || who.where == Person::INTERIOR) { who.where = Person::NOWHERE; who.vessel = nullptr; }   // (boarding has set ABOARD)
		// no body left and nobody aboard a ship: the simulation is closing - start clean next time
		if (everyone.empty() && !Crew::AnyAboard()) Crew::Clear();
	}

	void CrewMember::clbkSetClassCaps(FILEHANDLE cfg)
	{
		if (!atmospheres.Loaded()) atmospheres.Load();
		char buf[256];
		double v;
		who.bodyClass = GetClassNameA();
		if (!reborn) {   // the person's own data (a person out of a ship keeps his or hers)
		if (oapiReadItem_string(cfg, const_cast<char*>("Name"), buf)) name = buf;
		if (oapiReadItem_string(cfg, const_cast<char*>("Role"), buf)) role = buf;
		if (oapiReadItem_string(cfg, const_cast<char*>("Voice"), buf)) voice = buf;
		if (oapiReadItem_float(cfg, const_cast<char*>("BodyMass"), v)) bio.mass = v;
		if (oapiReadItem_float(cfg, const_cast<char*>("VO2max"), v)) bio.vo2max = v;
		if (oapiReadItem_string(cfg, const_cast<char*>("Sex"), buf)) sex = buf;
		oapiReadItem_float(cfg, const_cast<char*>("Age"), age);
		oapiReadItem_float(cfg, const_cast<char*>("Height"), heightM);
		if (oapiReadItem_float(cfg, const_cast<char*>("LiftMax"), v)) bio.liftMax = v;
		if (oapiReadItem_float(cfg, const_cast<char*>("BodyFat"), v)) bio.fat = v;
		}
		oapiReadItem_float(cfg, const_cast<char*>("SuitMass"), suitMass);
		oapiReadItem_float(cfg, const_cast<char*>("StandHeight"), height);
		oapiReadItem_float(cfg, const_cast<char*>("WalkSpeed"), walkSpeed);
		oapiReadItem_float(cfg, const_cast<char*>("RunSpeed"), runSpeed);
		oapiReadItem_vec(cfg, const_cast<char*>("EyePos"), eye);
		if (!reborn) {   // the suit's supplies as issued (a person out of a ship has what is left)
		if (oapiReadItem_float(cfg, const_cast<char*>("SuitO2"), v)) suit.o2 = suit.o2Cap = v;
		if (oapiReadItem_float(cfg, const_cast<char*>("SuitSorbent"), v)) suit.sorbCap = v;
		if (oapiReadItem_float(cfg, const_cast<char*>("SuitBattery"), v)) suit.batt = suit.battCap = v * 3.6e6;
		}
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
		hud.HelmetMesh(this);   // the helmet display's plate (the suit computer's, «Архитектор»): her VC HUD

		SetSize(1.0);
		SetPMI(_V(0.15, 0.03, 0.15));
		SetCrossSections(_V(0.55, 0.35, 0.75));
		SetEmptyMass(Mass());
		SetCameraOffset(eye); camEye = eye;
		// the head turns inside the helmet, the visor bounds the view: as far as a neck and a visor allow, not round
		// to her own back and shoulders (from inside, the suit's parts at the camera show cut edges)
		// her head turns her eyes (HeadStep / AimHead): the camera's default stays forward (Orbiter reads it only on entering
		// the cockpit view and keeps the view when it changes), the look is set through oapiCameraSetCockpitDir every step
		SetCameraDefaultDirection(_V(0, 0, 1));
		SetCameraRotationRange(PI * 0.98, PI * 0.98, PI05 * 0.95, PI05 * 0.95);   // Orbiter's own turning is not used (wide: no pull back)
		SetCameraCatchAngle(0);                                                  // no snapping back to ahead

		// feet first (they define the stance), then head, shoulders, hips and chest for tumbles
		const double k = 2e4, d = 2.6e3;
		static TOUCHDOWNVTX td[8];
		const VECTOR3 pts[8] = { { 0, -height, 0.12 }, { -0.13, -height, -0.10 }, { 0.13, -height, -0.10 },
			{ 0, 0.80, 0 }, { -0.24, 0.48, 0 }, { 0.24, 0.48, 0 }, { 0, 0.30, 0.16 }, { 0, 0.30, -0.16 } };
		for (int i = 0; i < 8; ++i) td[i] = { pts[i], k, d, 1.5, 1.5 };
		SetTouchdownPoints(td, 8);
		SetupRcs();
		inChild = CreateAttachment(true, _V(0, -height, 0), _V(0, 0, -1), _V(0, 1, 0), "OCINT");   // dir anti-parallel to the parent's: she faces along it
		jet.Setup(this);
		jet.SetupDust(-height);

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
			if (who.LoadLine(key, ss)) {}   // the person: who, the organism, what is worn
			else if (key == "FACESUN") { int f = 0; ss >> f; faceSun = f != 0; }
			else if (key == "SEAT") { ss >> seatId; seat = seatId >= 0 ? 2 : 0; seatT = 1; }
			else if (key == "INTERIOR") { ss >> inFeet.x >> inFeet.y >> inFeet.z >> inHdg; std::getline(ss >> std::ws, inShipName); }
			else if (key == "SHADE") { ss >> shadeTarget; shade = shadeTarget; }
			else if (key == "LAMP") ss >> lampOn;
			else ParseScenarioLineEx(line, status);
		}
		suit.Seal();
	}

	void CrewMember::clbkSaveState(FILEHANDLE scn)
	{
		VESSEL4::clbkSaveState(scn);
		if (n2) who.worn.suit.n2Kg = GetPropellantMass(n2);   // the suit's cold gas as the body holds it now
		who.Save(scn);                 // the person: who, the organism, what is worn
		oapiWriteScenario_float(scn, const_cast<char*>("SHADE"), shadeTarget);   // the body's own state
		oapiWriteScenario_int(scn, const_cast<char*>("LAMP"), lampOn);
		if (inShip && oapiIsVessel(inShip))
		{
			char b[160]; std::snprintf(b, sizeof b, "%.3f %.3f %.3f %.4f %s", inFeet.x, inFeet.y, inFeet.z, inHdg, oapiGetVesselInterface(inShip)->GetName());
			oapiWriteScenario_string(scn, const_cast<char*>("INTERIOR"), b);
			if (seat == 2 || seat == 4) oapiWriteScenario_int(scn, const_cast<char*>("SEAT"), seatId);
		}
	}

	void CrewMember::clbkPostCreation()
	{
		air = atmospheres.Sample(this);
		if (!suitFromScenario) suitOn = !air.Breathable();
		// the pack's fuel: its own line (JET_FUEL) or, in older scenarios, the body's PRPLEVEL
		if (jetFromScenario && suitOn) jet.Wear(jet.FuelKnown() ? jet.Kept() : GetPropellantMass(jet.Propellant()));
		if (n2 && who.worn.suit.n2Kg >= 0) SetPropellantMass(n2, who.worn.suit.n2Kg);   // the suit's cold gas, its own line
		else SetPropellantMass(jet.Propellant(), 0);
		SetEmptyMass(Mass());
		ShowFigure();
		if (!suitFig.ok) oapiWriteLogV("OrbiterCrew: %s has no suit figure yet, the coverall stands in for it", name.c_str());
		sound.Init(this, voice);
		// (gcCore RENDERPROC_HUD_2ND is not used: D3D9Client 30.7 of Orbiter 2024 passes it a null matrix and crashes -
		// dump 2026-10-04, D3D9Client+0x26594. The suit HUD goes through Orbiter's HUD in the glass cockpit, below)
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
	// her eyes: an empty virtual cockpit (no mesh) - nothing of a ship's instruments over them, the VC's own near plane
	bool CrewMember::clbkLoadVC(int)
	{
		SetCameraDefaultDirection(_V(0, 0, 1));
		SetCameraRotationRange(PI * 0.98, PI * 0.98, PI05 * 0.95, PI05 * 0.95);
		SetCameraCatchAngle(0);
		hud.RegisterVC();   // the helmet display's plate is the VC HUD (the suit computer draws into it: clbkDrawHUD)
		return true;
	}



	void CrewMember::SetSuit(bool on)
	{
		if (!bio.CanAct()) return;
		if (!on && jet.Worn()) { Say("Сначала снимите ранец (B)"); return; }
		if (!on)
		{
			std::string why;
			if (!air.Breathable(&why)) { Say("Нельзя снять скафандр: " + why, 2); return; }
		}
		suitOn = on;
		if (on) suit.Seal();
		// the suit computer comes on with the helmet (the coverall's 'no HUD' may have been set before this flight)
		if (on) hudOn = true;
		if (on) hudHidden = false;
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
		if (lie) surface::Lie(s, LieHeight() + PadLift()); else surface::Stand(s, height + PadLift());
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
		if (!(GetFlightStatus() & 1) && !airborne && !inShip) return 0;   // in space the keys stay with Orbiter (inside a ship: walking)
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
		if (key == OAPI_KEY_F) { if (down) DoUse(); return 1; }   // the action
		// walking keys are hers: Orbiter must not take them as its own (A - its autopilot, etc.)
		if ((inShip || (GetFlightStatus() & 1) || airborne) && !jet.Worn()) for (DWORD k : MOVE_KEYS) if (key == k) return 1;
		if (key == OAPI_KEY_K) { if (down) SetSuit(!suitOn); return 1; }
		if (key == OAPI_KEY_H && suitOn && oapiCameraInternal() && oapiCameraTarget() == GetHandle()) { if (down) hudOn = !hudOn; return 1; }
		if (key == OAPI_KEY_CAPITAL && jet.Worn()) { if (down) ApRequest(SuitHud::AP_FINE); return 1; }   // the limiter
		if (key == OAPI_KEY_V && !KEYMOD_SHIFT(kstate)) { if (down) ShipView(); return 1; }   // the ship from outside; again: her view
		if (key == OAPI_KEY_V && suitOn) { if (down) { shadeTarget = shadeTarget > 0.5 ? 0 : 1; Say(shadeTarget > 0.5 ? "Щиток опущен" : "Щиток поднят"); } return 1; }
		if (key == OAPI_KEY_L && suitOn) { if (down) { lampOn = !lampOn; Say(lampOn ? "Фонари включены" : "Фонари выключены"); } return 1; }
		if (key == OAPI_KEY_B && suitOn) { if (down && bio.CanAct()) { if (jet.Worn()) DropPack(); else TakePack(); } return 1; }
		if (jet.Worn() && (key == OAPI_KEY_J || key == OAPI_KEY_C || key == OAPI_KEY_G || key == OAPI_KEY_INSERT || key == OAPI_KEY_DELETE))
		{
			if (down) jet.Key(key, (GetFlightStatus() & 1) != 0);
			return 1;
		}
		// with the pack the flight keys are ours everywhere (Orbiter's A = hold altitude, [ ] = prograde/retrograde...)
		if (jet.Worn())
		{
			if (key == OAPI_KEY_SPACE) return 1;
			for (DWORD k : MOVE_KEYS) if (key == k) return 1;
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

		const double side = mouseMode ? std::clamp(static_cast<double>((k.right - k.left) + (k.stepR - k.stepL)), -1.0, 1.0) : static_cast<double>(k.stepR - k.stepL);   // the mouse turns: A/D step aside
		const double latTarget = side * 0.9;
		lat = Approach(lat, latTarget, std::abs(latTarget) > std::abs(lat) ? (std::min)(3.0, 0.5 * g) : (std::min)(5.0, 0.7 * g), dt);

		// turning: on the spot freely, at speed limited by the sideways grip (v * w <= 0.7 g)
		const double wmax = std::abs(fwd) < 0.3 ? 1.8 : (std::min)(1.8, 0.7 * g / std::abs(fwd));
		const double turnTarget = mouseMode ? std::clamp(mouseTurnBy / dt, -wmax, wmax) : (k.right - k.left) * wmax;
		turn = Approach(turn, turnTarget, std::abs(turnTarget) > std::abs(turn) ? 10.0 : 14.0, dt);

		if (fwd || lat || turn || mouseTurnBy)
		{
			VESSELSTATUS2 s = Status(this);
			// the mouse: she faces where it turned her, at once (its own limit is the grip, through turn above)
			s.surf_hdg = std::fmod(s.surf_hdg + (mouseMode ? mouseTurnBy : turn * dt) + PI2, PI2);
			surface::Walk(s, oapiGetSize(s.rbody), fwd * dt, lat * dt);
			surface::Stand(s, height + PadLift());
			DefSetStateEx(&s);
		}
	}

	void CrewMember::Jump()
	{
		// without the suit's drives a jump in the suit is beyond her legs where gravity is real (Earth, Mars); on the Moon
		// she still can (the suit's mass is light there)
		if (suitOn && !suit.Drives() && Gravity() > 2.0) { Say("Без сервоприводов не прыгнуть"); return; }
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
		const double padLift = PadLift();   // standing over a raised pad: its deck is the ground
		auto heightOf = [&](const VECTOR3& local)
		{
			VECTOR3 gp; Local2Global(local, gp);
			double lng, lat, rad; oapiGlobalToEqu(body, gp, &lng, &lat, &rad);
			return rad - R - oapiSurfaceElevation(body, lng, lat) - padLift;
		};
		const double feet = (std::min)(heightOf(_V(0.12, -height, 0.05)), heightOf(_V(-0.12, -height, 0.05)));
		double other = 1e9;
		for (const VECTOR3& p : { _V(0, 0.80, 0.02), _V(0.12, -0.45, 0.08), _V(-0.12, -0.45, 0.08), _V(0.30, -0.05, 0.10),
			_V(-0.30, -0.05, 0.10), _V(0, 0.10, -0.26), _V(0, 0.0, 0.12) })
			other = (std::min)(other, heightOf(p));
		if (jet.Worn() && jet.Deploy() > 0.5)
			for (double sx : { 0.60, -0.60 }) other = (std::min)(other, heightOf(_V(sx, 0.43, -0.17)));

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

		// head down (her 'up' towards the ground) or the helmet the lowest point: head first; otherwise on the back/side
		const double headH = heightOf(_V(0, 0.80, 0.02));
		const Body::Contact hitBy = lean > 110 * RAD || (headH < 0.02 && headH <= other + 1e-6) ? Body::ON_HEAD : Body::ON_BACK;
		if (other < 0.02) { Fall(length(hv), "Удар о грунт", hitBy); return; }      // head, knees, hands, pack: a fall, not a landing
		if (feet < 0.12 && into > -0.3)   // the soles at the ground and not climbing away: down
		{
			if (along > 4.0 || lean > 30 * RAD) { Fall(into + 0.5 * along, "Падение при посадке", lean > 110 * RAD ? Body::ON_HEAD : Body::ON_BACK); return; }
			freeVy = -into;                                                  // the impact that counts is the one into the slope
			Touchdown();
		}
	}

	void CrewMember::Fall(double impact, const char* why, Body::Contact c)
	{
		jet.Touchdown();
		VESSELSTATUS2 s = Status(this);
		double lng, lat_, rad, hdg = 0;
		s.rbody = GetEquPos(lng, lat_, rad);
		oapiGetHeading(GetHandle(), &hdg);
		s.surf_lng = lng; s.surf_lat = lat_; s.surf_hdg = hdg;
		surface::Lie(s, LieHeight() + PadLift());
		DefSetStateEx(&s);
		lying = true; fallenT = 2.5; wasFree = false; freeT = 0;
		fwd = lat = turn = accel = 0;
		HitBody(impact, c);   // a fall: through the shell and the frame, by what hit first
		landingSpeed = (std::min)(impact, 6.0);
		Say(bio.state == Body::DEAD ? "Смертельный удар" : why, 2);
	}

	void CrewMember::Touchdown()
	{
		VESSELSTATUS2 s = Status(this);
		double lng, lat_, rad, hdg = 0;
		s.rbody = GetEquPos(lng, lat_, rad);
		oapiGetHeading(GetHandle(), &hdg);
		s.surf_lng = lng; s.surf_lat = lat_; s.surf_hdg = hdg;
		surface::Stand(s, height + PadLift());
		DefSetStateEx(&s);
		landingSpeed = (std::max)(0.0, -freeVy);
		HitBody(landingSpeed, Body::ON_FEET);
		jet.Touchdown();
		if (ap.Get() != Autopilot::OFF) ap.Off(this, "на грунте");
		wasFree = false; freeT = 0;
		if (landingSpeed > 7) Say(bio.state == Body::DEAD ? "Смертельный удар" : "Жёсткая посадка - травма", 2);
	}

	double CrewMember::PadLift() const
	{
		OBJHANDLE body = GetSurfaceRef();
		if (!body) return 0;
		double lng, lat, rad; GetEquPos(lng, lat, rad);
		const double R = oapiGetSize(body), terr = oapiSurfaceElevation(body, lng, lat), cl = (std::max)(0.05, std::cos(lat));
		const double PAD_TOP = 0.03;   // the pad's deck above the reference
		double lift = 0;
		for (DWORD b = 0; b < oapiGetBaseCount(body); ++b)
		{
			OBJHANDLE hb = oapiGetBaseByIndex(body, b);
			double blng, blat; oapiGetBaseEquPos(hb, &blng, &blat);
			if (std::hypot((blat - lat) * R, (blng - lng) * R * cl) > 5000) continue;
			const std::vector<PadShape>& shapes = PadShapes(hb, body);
			for (DWORD k = 0; k < oapiGetBasePadCount(hb); ++k)
			{
				double plng, plat, prad = R;
				if (!oapiGetBasePadEquPos(hb, k, &plng, &plat, &prad)) continue;
				const PadShape shape = k < shapes.size() ? shapes[k] : PadShape{};   // no config found: an 80 m pad
				if (!OnPad(shape, (plng - lng) * -R * cl, (plat - lat) * -R)) continue;   // her offset from the pad centre
				// a deck a little above the relief is the pad mesh standing on it; tens or hundreds of metres means the
				// pad's sphere radius and the relief simply disagree (the client lays the pad on the relief): ignore it
				const double d = (prad - R) + PAD_TOP - terr;
				if (d > 0 && d < 1.5) lift = (std::max)(lift, d);
			}
		}
		return lift;
	}

	void CrewMember::StandUp()
	{
		VESSELSTATUS2 s = Status(this);
		double lng, lat_, rad, hdg = 0;
		s.rbody = GetEquPos(lng, lat_, rad);
		oapiGetHeading(GetHandle(), &hdg);
		s.surf_lng = lng; s.surf_lat = lat_; s.surf_hdg = hdg;
		surface::Stand(s, height + PadLift());
		DefSetStateEx(&s);
		lying = false; wasFree = false; freeT = 0; fwd = lat = turn = 0;
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
		// she keeps her stride: walking and running are kinematic on the ground (Orbiter's landed state does not move),
		// so the speed of her feet goes into the flight here, as in a jump
		const VECTOR3 f = mul(R, _V(0, 0, 1)), r = mul(R, _V(1, 0, 0));   // she stands upright: her axes are the horizon's
		s.rvel = rvel + unit(rpos) * 0.2 + f * fwd + r * lat;
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
			// the ground she sees with half of her surface. Airless: hot regolith by day, ~100 K by night (the Moon).
			// A thin atmosphere (Mars, 0.6 kPa) holds the night up (~180 K, -93 C) and the day down (~290 K at noon)
			const double scale = std::pow(t.sunFlux / 1361, 0.25);
			const double thin = air.p > 0.05 ? (std::min)(1.0, air.p / 0.6) : 0.0;
			const double floorT = 100 + 80 * thin, amp = 290 * scale * (1 - 0.53 * thin);
			t.groundT = t.sunlit && elev > 0 ? floorT + amp * std::pow(std::sin(elev), 0.25) : floorT;
			q += 0.5 * 0.95 * SIGMA * std::pow(t.groundT, 4);
		}
		// the sky: deep space, or the glow of a thin atmosphere (its CO2 radiates at roughly the air's temperature - 35 K)
		const double skyT = air.p > 0.05 && air.T > 40 ? air.T - 35 : 2.7;
		q += 0.5 * SIGMA * std::pow(skyT, 4);
		t.tEnv = std::pow(q / (EPS * SIGMA), 0.25);
		return t;
	}

	void CrewMember::Land()
	{
		VESSELSTATUS2 s = Status(this);
		double lng, lat_, rad;
		s.rbody = GetEquPos(lng, lat_, rad);
		s.surf_lng = lng; s.surf_lat = lat_; s.surf_hdg = jumpHeading;
		surface::Stand(s, height + PadLift());
		DefSetStateEx(&s);
		airborne = false;
		landingSpeed = fallSpeed;
		HitBody(landingSpeed, Body::ON_FEET);
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
		const bool live = lampOn && suitOn && suit.LampsUp();   // the suit sheds the lamps under overload
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
		// the suit's RCS lives on the suit's power; her hands only for manual control - an autopilot of the suit computer
		// keeps using it when she cannot (the user's rule: the suit's systems do not hang on the wearer's state)
		const bool live = free && suitOn && suit.Powered() && (bio.CanAct() || ap.Get() != Autopilot::OFF);
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
		if (n2) who.worn.suit.n2Kg = GetPropellantMass(n2);   // the suit's own record of its cold gas, kept current
		FindUse(dt);
		if (!inShip && !inShipName.empty())   // a scenario with her inside a ship: find it
		{
			OBJHANDLE s = oapiGetVesselByName(const_cast<char*>(inShipName.c_str()));
			const VECTOR3 f = inFeet; const double h = inHdg;
			if (s) { EnterShip(s, f, _V(std::sin(h), 0, std::cos(h))); if (inShip) inShipName.clear(); }   // (the ship may register its interior later)
		}
		// the user's view of this person, remembered in the person (through the eyes / from outside at that distance)
		if (!takeView && oapiCameraTarget() == GetHandle()) { who.viewOutside = !oapiCameraInternal(); if (who.viewOutside) who.viewDist = oapiCameraTargetDist(); }
		if (takeView && !inShip) ApplyView();
		// once at the start: where the Sun is for her (horizon frame: elevation, azimuth from north to the east) - and,
		// with FACESUN 1 in her scenario block, she turns to face it (standing on the ground)
		if (!sunLogged && !inShip && simt > 0.5 && GetSurfaceRef())
		{
			sunLogged = true;
			VECTOR3 sp, me; oapiGetGlobalPos(oapiGetGbodyByIndex(0), &sp); GetGlobalPos(me);
			MATRIX3 Rv; GetRotationMatrix(Rv);
			VECTOR3 hz; HorizonRot(tmul(Rv, unit(sp - me)), hz);   // x east, y up, z north
			const double az = std::atan2(hz.x, hz.z), el = std::asin(std::clamp(hz.y, -1.0, 1.0));
			oapiWriteLogV("OrbiterCrew: %s: the Sun at elevation %.1f deg, azimuth %.1f deg (MJD %.5f)", name.c_str(), el * DEG, std::fmod(az * DEG + 360, 360), oapiGetSimMJD());
			if (faceSun && (GetFlightStatus() & 1))
			{
				VESSELSTATUS2 s = Status(this);
				s.surf_hdg = std::fmod(az + PI2, PI2);
				DefSetStateEx(&s);
			}
		}
		if (inShip) { InteriorStep(dt); if (takeView && inParent) ApplyView(); return; }
		jet.SetOxygen(air.ppO2 > 1.0);   // steam behind the jets only where hydrogen can burn
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
		suit.vent = air.Breathable(); suit.ventPpO2 = air.ppO2; suit.ventPpCO2 = air.ppCO2; suit.pOut = air.p;   // breathable air: no tank, no sorbent
		if (suitOn) heat = suitResidual = suit.Step(dt, bio.O2Use(), bio.CO2Made(), driveDemandW, bio.Heat(), thermal.tEnv);
		else
		{
			const double net = bio.Heat() + 8 * (air.p > 1 ? air.T - 295 : 0);   // unsuited: her own regulation covers +-150 W
			heat = net > 150 ? net - 150 : net < -150 ? net + 150 : 0;
		}
		bio.Step(dt, humanW + (turn ? 25 : 0), suitOn ? suit.ppO2 : air.ppO2, suitOn ? suit.ppCO2 : air.ppCO2, air.p, suitOn, heat);
		// radiation: the environment here, through the suit's shell (a coverall is ~0.3 g/cm^2) and its field
		radEnv = rad.Sample(this, air.p, Gravity());
		bio.Irradiate(dt, Radiation::Dose(radEnv, suitOn ? suit.shieldGcm2 : 0.3, suitOn && suit.FieldUp() ? suit.fieldFactor : 1.0));
		// water and food: at hand in breathable air on the ground (her kit, a base); elsewhere only what the suit carries
		bio.Sustain(dt, air.Breathable() && (GetFlightStatus() & 1), suitOn ? &suit.water : nullptr, air.T, suitOn);
		// time acceleration: a person outside a safe zone (unbreathable air, not standing on the ground, or a radiation
		// field) holds the simulation to 10x at most, as UMmu did - the organism needs real attention there
		{
			const bool safe = air.Breathable() && (GetFlightStatus() & 1) && bio.doseRate < 1e-3;
			if (!safe && bio.state != Body::DEAD && oapiGetTimeAcceleration() > 10.0)
			{
				oapiSetTimeAcceleration(10.0);
				Say("Ускорение времени ограничено 10x: экипаж вне безопасной зоны");
			}
		}

		// ---- jet pack ----
		const double altFeet = GetAltitude(ALTMODE_GROUND) - height;
		const int req = hud.TakeRequest();
		if (req >= 0) ApRequest(req);
		FlightInput fin = flightFresh ? flight : FlightInput{};
		// the pack and the suit computer's autopilots run on the suit's power; her own commands only while she can act
		// (unconscious or dead: no manual input, the holds and the autopilot go on)
		if (!bio.CanAct()) fin = FlightInput{};
		ap.Step(this, jet, dt, g, landed, suitOn && suit.Powered(), fin.pitch != 0 || fin.yaw != 0 || fin.strafe != 0, fin);
		jet.Update(dt, landed, suitOn && suit.Powered(), g, altFeet, fin);
		flightFresh = false;
		freeT = (!landed && !airborne) ? freeT + dt : 0;
		if (!landed && !airborne && !lying && freeT > 0.3 && jet.SurfaceMode()) GroundContactCheck(dt);   // lying: already down, no new falls
		if (fallenT > 0) fallenT -= dt;
		if (!landed && !airborne) { VECTOR3 hv; GetGroundspeedVector(FRAME_HORIZON, hv); freeVy = hv.y; }
		if (landed && wasFree)   // back on her feet after a flight
		{
			landingSpeed = (std::max)(0.0, -freeVy); HitBody(landingSpeed, Body::ON_FEET); Place(!bio.CanAct());
			jet.Touchdown();
			if (ap.Get() == Autopilot::HOVER) ap.Off(this, "на грунте");
		}
		wasFree = !landed && !airborne;

		if (airborne && jet.Thrust() > 0) airborne = false;                                   // the jump turns into a flight
		bool lifted = false;
		if (landed && !airborne && bio.CanAct() && jet.Lift() > GetMass() * g * 1.02) { Liftoff(); lifted = true; }

		// fallen and pushed loose by the ground contact (lying on the pack): she gets up when the fall is over all the same
		if (lying && !landed && !airborne && fallenT <= 0 && bio.CanAct()) StandUp();

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
			// standing still, our placement can go stale: Orbiter fills in finer elevation tiles after the scenario starts
			// (and the ground under a resting person is never re-read). Check the feet against the ground now and then
			// and stand her on it again - our position, not Orbiter's terrain
			if ((settleT -= dt) <= 0)
			{
				settleT = 0.5;
				const double off = GetAltitude(ALTMODE_GROUND) - (lying ? LieHeight() : height) - PadLift();
				if (std::abs(off) > 0.02 && fwd == 0 && lat == 0) Place(lying);
			}
			if (!bio.CanAct() && !lying) { fwd = lat = turn = 0; Place(true); }
			else if (bio.CanAct() && lying && fallenT <= 0) Place(false);   // gets up once the fall is over
			mouseTurnBy = 0; HeadStep(dt, false);   // outside: the right button turns her head (the keys turn her)
			if (bio.CanAct() && !lying) Drive(dt, g); else fwd = lat = turn = accel = 0;
		}
		else fwd = lat = turn = accel = 0;
		keysFresh = false;
		HudBySuit();
		Animate(dt, g, landed);
	}

	// The eyes aboard a ship: the specific force at the eyes (the ship's thrust, lift and drag over its mass, plus the
	// rotation about its CG), in her frame, drives the neck (HeadSway); the engines and the air shake the structure.
	VECTOR3 CrewMember::HeadSwayStep(double dt, const VECTOR3& eye)
	{
		VESSEL* ship = inShip && oapiIsVessel(inShip) ? oapiGetVesselInterface(inShip) : nullptr;
		if (!ship) { headSway.Reset(); return _V(0, 0, 0); }
		const double m = ship->GetMass();
		VECTOR3 T, L, D, w, a, eg, r;
		ship->GetThrustVector(T); ship->GetLiftVector(L); ship->GetDragVector(D);
		ship->GetAngularVel(w); ship->GetAngularAcc(a);
		Local2Global(eye, eg);
		ship->Global2Local(eg, r);
		// a point at r on the rotating ship: alpha x r + w x (w x r); the specific force there is the CG's plus that
		const VECTOR3 fShip = (T + L + D) / m + crossp(a, r) + crossp(w, crossp(w, r));
		MATRIX3 Rs, Rp;
		ship->GetRotationMatrix(Rs); GetRotationMatrix(Rp);
		const VECTOR3 f = tmul(Rp, mul(Rs, fShip));
		return headSway.Step(dt, f, length(T) / m / 9.81, ship->GetDynPressure(), ship->GetMachNumber());
	}

	void CrewMember::HudBySuit()
	{
		// ---- no HUD without the suit (the generic cockpit has one; switch it off, give it back with the suit) ----
		// Orbiter's own HUD is never hers (the suit computer draws its own, DrawSuitHud); in her eyes it stays off
		const bool inHead = oapiCameraInternal() && oapiCameraTarget() == GetHandle();
		const bool want = suitOn && hudOn;   // Orbiter's HUD carries the suit computer's, and nothing else
		if (inHead && !want && oapiGetHUDMode() != HUD_NONE) { oapiSetHUDMode(HUD_NONE); hudHidden = true; }
		else if (inHead && want && oapiGetHUDMode() == HUD_NONE) { oapiSetHUDMode(HUD_SURFACE); hudHidden = false; }
		// the suit computer comes on whenever she enters the helmet view in the suit
		const bool inHelmet = inHead && suitOn;
		if (inHelmet && !wasInHelmet) hudOn = true;
		wasInHelmet = inHelmet;
	}

	void CrewMember::Animate(double dt, double g, bool landed)
	{
		// ---- animation ----
		int footfalls = 0;
		Figure& fig = Active();
		if (vis && fig.ok && fig.skin.Attached())
		{
			MotionInput in;
			in.dt = dt; in.fwd = fwd; in.lat = lat; in.turn = turn; in.accel = accel; in.g = g;
				in.seat = seat == 4 ? 2 : seat; in.seatT = seatT;
				in.heading = inShip ? std::remainder(inHdg - vesselYaw, PI2) : 0;   // her turn from her vessel's axes
			in.grounded = !airborne && landed && !lying;
			in.lying = lying && landed;
			in.landing = landingSpeed;
			in.effort = (std::min)(1.0, bio.Effort()); in.fatigue = bio.Fatigue(); in.breathRate = bio.breath;
			in.walkTop = walkSpeed * (suitOn ? 0.9 : 1.0);
			in.suited = suitOn; in.bound = boost ? std::clamp((std::hypot(fwd, lat) - 3.5) / 2.5, 0.0, 1.0) : 0.0;
			in.floating = !landed && !airborne;
			if (in.floating) GetAngularVel(in.angVel);
			if (in.floating) { VECTOR3 f; GetThrustVector(f); in.thrustAcc = f / GetMass(); GetAngularAcc(in.angAcc); }
			const bool firstPerson = oapiCameraInternal() && oapiCameraTarget() == GetHandle();
			// seated: the ship's cockpit view is her eyes in the seat - her own head must not be in it
			fig.skin.SetHideHead(firstPerson);
			// from inside the helmet its own parts sit at the camera: seen from within they show cut edges and their dark
			// inner faces. The body below the neck stays in view; the helmet, the visor, the shade and its arms do not
			for (const char* part : { "Helm", "Visor", "SunShade", "ShadeArms", "NeckSeal" }) fig.skin.SetPartHidden(part, firstPerson);
			UpdateHelmet(dt, fig);
			if (&fig == &suitFig) jet.Pose(fig.skin);
			in.jet = jet.Worn();
			motion.Update(in, fig.clips, fig.skin);
			footfalls = motion.Footfalls();
			if (firstPerson) {   // the camera rides the head; aboard a ship the head rides the ship's accelerations
				const VECTOR3 e = fig.skin.Point(fig.skin.Bone("Head"), eye);
				camEye = e + HeadSwayStep(dt, e);
				SetCameraOffset(camEye);
			} else headSway.Reset();
		}
		// ---- sound ----
		CrewSound::Input si;
		si.dt = dt; si.footfalls = footfalls; si.runWeight = motion.RunWeight(); si.speed = std::hypot(fwd, lat);
		si.landing = landingSpeed; si.suited = suitOn; si.vacuum = air.p < 1; si.fanOn = suitOn && suit.Powered(); si.alarm = suitOn ? hud.AlarmLevel() : 0;
		// the heat the control moves against its capacity: cooling or heating, full capacity = full speed
		si.fanLoad = suit.Powered() && suitOn ? std::clamp(std::abs(suit.heatW) / (suit.heatW >= 0 ? suit.coolMaxW : suit.heatMaxW), 0.0, 1.0) : 0.0;
		si.alive = bio.state != Body::DEAD; si.breathRate = bio.breath;
		si.intensity = std::clamp(0.8 * bio.Effort() + 0.5 * bio.Fatigue() + 0.6 * (1 - bio.reserve), 0.0, 1.0);
		si.mine = oapiGetFocusObject() == GetHandle();
		// the wind where SHE is (the user: the sound follows the person, not the camera): none inside a ship (a sealed
		// hull) or in vacuum; louder with denser air and with her speed through it; the helmet muffles it
		// (the person in focus: the wind plays for the user's ears whatever the camera is - other people's winds stay off)
		if (!inShip && air.p > 0.05 && air.T > 30 && oapiGetFocusObject() == GetHandle())
		{
			const double Rgas = (air.body == "Mars" || air.body == "Venus") ? 189.0 : 287.0;   // CO2 or nitrogen-oxygen air
			const double rho = air.p * 1000 / (Rgas * air.T);
			const double v = GetAirspeed();
			si.wind = std::sqrt((std::min)(1.5, rho) / 1.2) * (0.25 + 0.75 * std::clamp(v / 15.0, 0.0, 1.0)) * (suitOn ? 0.35 : 1.0);
		}
		sound.Update(si);

		landingSpeed = 0;
		if (messageTime > 0) messageTime -= dt;
		AimHead(dt);
	}

	// her eyes are not a ship's glass cockpit: Orbiter's navigation-mode and RCS buttons are not shown over them
	bool CrewMember::clbkLoadGenericCockpit() { return false; }   // her eyes are the empty VC, in the suit and out of it

	bool CrewMember::clbkDrawHUD(int, const HUDPAINTSPEC* hps, oapi::Sketchpad* skp) { if (hps && skp) DrawSuitHud(skp, hps->W, hps->H); return true; }


	// every frame for every body: only the one whose eyes we look through, in the suit, with the HUD on, draws it
	void CrewMember::DrawSuitHud(oapi::Sketchpad* skp, DWORD W, DWORD H)
	{
		// the person and the suit are apart (the user's rule): the suit computer's HUD exists only in the suit
		if (!suitOn || !hudOn || !oapiCameraInternal() || oapiCameraTarget() != GetHandle()) return;
		if (!W || !H) return;
		static const std::pair<const char*, const char*> WARN[] = {
			{ "VACUUM - NO SUIT", "ВАКУУМ БЕЗ СКАФАНДРА" }, { "OVERHEATING", "ПЕРЕГРЕВ" }, { "HYPOTHERMIA", "ПЕРЕОХЛАЖДЕНИЕ" },
			{ "HYPOXIA", "ГИПОКСИЯ" }, { "LOW OXYGEN", "МАЛО КИСЛОРОДА" }, { "CO2 NARCOSIS", "ОТРАВЛЕНИЕ CO2" },
			{ "HIGH CO2", "ВЫСОКИЙ CO2" }, { "EXHAUSTED", "ИСТОЩЕНИЕ" }, { "INJURED", "ТРАВМА" },
			{ "RADIATION - TAKE COVER", "РАДИАЦИЯ - В УКРЫТИЕ" }, { "RADIATION SICKNESS", "ЛУЧЕВАЯ БОЛЕЗНЬ" },
			{ "DEHYDRATED", "ОБЕЗВОЖИВАНИЕ" }, { "STARVING", "ГОЛОД" } };
		HudData d;
		d.name = name; d.role = role == "astronavigator" ? "астронавигатор" : role; d.state = static_cast<int>(bio.state); d.suit = suitOn;
		d.pulse = bio.pulse; d.breath = bio.breath; d.effort = (std::min)(1.0, bio.Effort()); d.stamina = bio.wbal;
		d.ppO2 = suitOn ? suit.ppO2 : air.ppO2; d.ppCO2 = suitOn ? suit.ppCO2 : air.ppCO2; d.coreC = bio.coreT - 273.15; d.injury = (std::min)(1.0, bio.injury);
		d.o2 = suit.o2 / suit.o2Cap; d.o2Vent = suitOn && suit.Venting(); d.o2Hours = d.o2Vent ? 0 : suit.HoursLeft(bio.O2Use()); d.sorbent = 1 - suit.sorbUsed / suit.sorbCap;
		d.batt = suit.batt / suit.battCap; d.battHours = suit.BattHours(); d.powerW = suit.drawW; d.lifeW = suit.Powered() ? suit.lifeW : 0;
		d.thermalW = suit.thermalW; d.driveW = suit.driveW; d.powered = suit.Powered();
		d.lampW = suitOn ? suit.lampW : 0; d.heatW = suit.heatW; d.residualW = suitResidual; d.battKWh = suit.batt / 3.6e6; d.o2Flow = bio.O2Use();
		d.sorbHours = bio.CO2Made() > 0 ? (suit.sorbCap - suit.sorbUsed) / bio.CO2Made() / 3600 : 0;
		d.landed = (GetFlightStatus() & 1) != 0; d.servo = boost;
		d.tInC = suit.tIn - 273.15; d.suitPMax = suit.pMaxOutKPa; d.suitBreached = suit.breached;
		d.water = suit.water; d.waterDef = bio.waterDef; d.bodyMass = bio.mass; d.fastDays = bio.fastDays;
		for (int i = 0; i < 4; ++i) d.hurt[i] = bio.hurt[i];
		d.radValid = true; d.radRate = bio.doseRate; d.radDose = bio.doseSv; d.radCareer = bio.careerSv;
		d.fieldOn = suitOn && suit.FieldUp(); d.fieldW = d.fieldOn ? suit.fieldW : 0;
		d.apMode = static_cast<int>(ap.Get()); d.apStatus = ap.Status(); d.apCmd = ap.CmdHorizon();
		if (air.p > 1) { VECTOR3 gsv, asv; GetGroundspeedVector(FRAME_HORIZON, gsv); GetAirspeedVector(FRAME_HORIZON, asv); d.wind = gsv - asv; }
		d.envC = thermal.tEnv - 273.15; d.ratedMinC = suit.tMin - 273.15; d.ratedMaxC = suit.tMax - 273.15; d.sunlit = thermal.sunlit;
		d.inSpec = suit.InSpec(thermal.tEnv); d.hasGround = thermal.groundT > 0; d.groundC = thermal.groundT - 273.15;
		d.body = air.body; d.vacuum = air.body.empty() || air.p < 0.01; d.airKPa = air.p; d.airC = air.T - 273.15; d.breathable = air.Breathable();
		d.n2 = n2 ? GetPropellantMass(n2) / 1.5 : 0; d.n2Dv = RcsDeltaV();
		d.rcs = rcsLive ? 1 : ((GetFlightStatus() & 1) || airborne) ? 0 : 2;
		d.life = &suit;   // the suit itself: its loads, equipment heat, SERVO and ECONOMY switches (the display's)
		d.shadeDown = shadeTarget > 0.5; d.lampsOn = lampOn && suit.LampsUp(); d.shade = shade;
		d.firstPerson = oapiCameraInternal() && oapiCameraTarget() == GetHandle();
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
		if (messageTime > 0) { d.message = message; d.messageLevel = messageLevel; }
		else if (!useHint.empty()) { d.message = "F - " + useHint; d.messageLevel = 0; }
		hud.Mouse(W, H);                    // the clicks on the HUD (the suit computer's own, «Архитектор»)
		hud.Draw(skp, W, H, d, this);
	}
}

namespace ocrew
{
	// ---- inside a ship ----
	void CrewMember::EnterShip(OBJHANDLE ship, const VECTOR3& feet, const VECTOR3& dir)
	{
		ShipInterior* si = InteriorOf(ship);
		if (!si || !si->fns.Attach || !oapiIsVessel(ship)) return;
		VESSEL* sv = oapiGetVesselInterface(ship);
		inParent = si->fns.Attach(si->ctx);
		if (!inParent) return;
		inShip = ship; inFeet = feet; inHdg = std::atan2(dir.x, dir.z);
		double fy = 0;
		if (si->fns.Ground && !si->fns.Ground(si->ctx, &inFeet, 0.45, &fy) && si->fns.Count && si->fns.Item)
		{
			// no floor under her (a place saved in a frame that has moved since, e.g. with the ship's CG): behind the first seat
			for (int i = 0, n = si->fns.Count(si->ctx); i < n; ++i)
			{
				OcItem it{};
				if (!si->fns.Item(si->ctx, i, &it) || it.kind != OC_SEAT) continue;
				inFeet = it.pos - it.dir * 0.95; inHdg = std::atan2(it.dir.x, it.dir.z);
				oapiWriteLogV("OrbiterCrew: %s: no floor at (%.2f %.2f %.2f) in %s, stands behind '%s'", name.c_str(), feet.x, feet.y, feet.z,
					sv->GetName(), it.label);
				break;
			}
		}
		else if (si->fns.Ground) inFeet.y = fy;
		oapiWriteLogV("OrbiterCrew: %s inside %s at (%.2f %.2f %.2f)", name.c_str(), sv->GetName(), inFeet.x, inFeet.y, inFeet.z);
		if (seat == 2 && seatId >= 0)   // seated in the scenario: back into that seat, the ship is told
		{
			if (SeatPlace(seatId)) { inFeet = seatFeet; inHdg = seatHdg; if (si->ext.Seated) si->ext.Seated(si->ctx, seatId, who.id, 1);
				seatHelm = false;
				for (int i = 0, n = si->fns.Count ? si->fns.Count(si->ctx) : 0; i < n; ++i) { OcItem it{}; if (si->fns.Item && si->fns.Item(si->ctx, i, &it) && it.id == seatId) seatHelm = it.kind == OC_HELM; } }
			else { seat = 0; seatId = -1; }
		}
		fwd = lat = turn = accel = 0; airborne = lying = false;
		vesselYaw = 0;   // the vessel keeps the ship's axes; she turns in her pose, her head turns the camera
		sv->SetAttachmentParams(inParent, inFeet + si->Origin(), _V(std::sin(vesselYaw), 0, std::cos(vesselYaw)), _V(0, 1, 0));
		if (GetAttachmentStatus(inChild) != ship) sv->AttachChild(GetHandle(), inParent, inChild);
		who.where = Person::INTERIOR; who.ship = ship; who.vessel = GetHandle();
		if (si->fns.Viewing) { si->fns.Viewing(si->ctx, 1); inViewing = true; }
	}

	void CrewMember::LeaveShip()
	{
		if (!inShip) return;
		if (oapiIsVessel(inShip))
		{
			if (ShipInterior* si = InteriorOf(inShip)) if (inViewing && si->fns.Viewing) si->fns.Viewing(si->ctx, 0);
			if (inParent) oapiGetVesselInterface(inShip)->DetachChild(inParent);
		}
		inViewing = false; inShip = nullptr; inParent = nullptr;
		who.where = Person::IN_WORLD; who.ship = nullptr;
	}

	// the focus and the person's own view: through the eyes, or from outside at the distance the user had
	void CrewMember::ApplyView()
	{
		takeView = false;
		oapiSetFocusObject(GetHandle());
		if (!who.viewOutside) { oapiCameraAttach(GetHandle(), CAM_INSIDE); return; }
		oapiCameraAttach(GetHandle(), CAM_OUTSIDE);
		const double d = oapiCameraTargetDist();
		if (who.viewDist > 0.5 && d > 0) oapiCameraScaleDist(who.viewDist / d);
	}

	// where she is in that seat: the feet under the ship's hips point (the clip's seated hips are hipY over the feet)
	bool CrewMember::SeatPlace(int id)
	{
		ShipInterior* si = InteriorOf(inShip);
		Figure& fig = Active();
		if (!si || !si->fns.Seat || !fig.ok || !fig.clips.seats) return false;
		VECTOR3 hips{}, dir{ 0, 0, 1 };
		si->fns.Seat(si->ctx, id, &hips, &dir);
		const int hb = fig.skin.Bone("Hips");
		const double hipY = hb >= 0 && fig.clips.sit.data.size() > static_cast<size_t>(hb * 7 + 5) ? fig.clips.sit.data[hb * 7 + 5] + height : 0.5;
		seatHdg = std::atan2(dir.x, dir.z);
		seatFeet = _V(hips.x, hips.y - hipY, hips.z);
		return true;
	}

	// F at a seat: she sits down in that very seat (the clip has the seat at the model origin and ends with the hips
	// at seatHipY over the feet; the ship's Seat() gives the hips point and the facing)
	void CrewMember::SitDown(int id)
	{
		ShipInterior* si = InteriorOf(inShip);
		Figure& fig = Active();
		if (!si || !si->fns.Seat || !fig.ok || !fig.clips.seats) return;
		SeatPlace(id);
		seatHelm = useKind == OC_HELM;
		// the clip starts standing seatStartZ in front of the seat: she is eased from where she is onto that start
		const VECTOR3 f0 = _V(std::sin(seatHdg), 0, std::cos(seatHdg));
		seatFrom = inFeet - f0 * seatStartZ; seatHdgFrom = inHdg;
		seat = 1; seatT = 0; seatId = id; fwd = lat = turn = accel = 0;
		if (!kSeatClips)   // for now (the user): no sitting-down motion - she is in the seat at once, the seat moves up
		{
			seat = 2; seatT = 1; inFeet = seatFeet; inHdg = seatHdg;
			// seated, Orbiter's own head turning about the vessel's nose: the vessel faces the way the seat faces
			headYaw = 0; headPitch = 0;   // seated: she looks ahead, at the console
			if (si->ext.Seated) si->ext.Seated(si->ctx, seatId, who.id, 1);
		}
		oapiWriteLogV("OrbiterCrew: %s sits down in seat %d of %s", name.c_str(), id, oapiGetVesselInterface(inShip)->GetName());
	}

	// she stands up from the seat (F in the seat, through the ship): the body takes the focus and her view back
	void CrewMember::StandFromSeat()
	{
		if (seat != 2 || !inShip) return;
		// the ship raised or taking off (tilted over 15 deg from the local vertical near a planet): she stays in the seat
		// (the user, 2026-10-03). Above 100 km the ship's own gravity holds the deck - no limit there
		if (oapiIsVessel(inShip))
		{
			VESSEL* sv = oapiGetVesselInterface(inShip);
			OBJHANDLE ref = sv->GetSurfaceRef();
			if (ref && sv->GetAltitude() < 100e3)
			{
				VECTOR3 sp, rp; sv->GetGlobalPos(sp); oapiGetGlobalPos(ref, &rp);
				MATRIX3 R; sv->GetRotationMatrix(R);
				const VECTOR3 deckUp = mul(R, _V(0, 1, 0));
				const double tilt = std::acos(std::clamp(dotp(deckUp, unit(sp - rp)), -1.0, 1.0)) * DEG;
				if (tilt > 15)
				{
					char b[96]; std::snprintf(b, sizeof b, "Вставать нельзя: корабль наклонён на %.0f°", tilt);
					Say(b, 1);
					oapiWriteLogV("OrbiterCrew: %s stays seated - the ship is tilted %.1f deg", name.c_str(), tilt);
					return;
				}
			}
		}
		if (shipView) ShipView();   // up from the seat: the view comes back to her
		if (ShipInterior* si = InteriorOf(inShip)) if (si->ext.Seated) si->ext.Seated(si->ctx, seatId, who.id, 0);   // the seat moves back
		seat = 4; seatT = 0; seatStill = 0;
		oapiWriteLogV("OrbiterCrew: %s stands up from seat %d", name.c_str(), seatId);
	}

	// the mouse on the ship's buttons: a left click in Orbiter's window is a ray from the camera through the cursor,
	// taken into the ship's interior frame; the nearest OC_BUTTON it passes through, within her arm's reach, is pressed
	void CrewMember::ClickInside(ShipInterior& si)
	{
		const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		const bool edge = down && !lmbWas;
		lmbWas = down;
		if (!edge || oapiGetFocusObject() != GetHandle() || !bio.CanAct()) return;
		POINT p; GetCursorPos(&p);
		HWND w = WindowFromPoint(p); DWORD pid = 0;
		if (w) GetWindowThreadProcessId(w, &pid);
		RECT rc{};
		if (!w || pid != GetCurrentProcessId() || !ScreenToClient(w, &p) || !GetClientRect(w, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
		const double W = rc.right, H = rc.bottom, fpx = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
		VECTOR3 cp; oapiCameraGlobalPos(&cp); MATRIX3 Rc; oapiCameraRotationMatrix(&Rc);
		const VECTOR3 dg = mul(Rc, unit(_V((p.x - W / 2) / fpx, (H / 2 - p.y) / fpx, 1.0)));   // camera -> global
		VESSEL* sv = oapiGetVesselInterface(si.ship);
		VECTOR3 o; sv->Global2Local(cp, o); o -= si.Origin();
		MATRIX3 Rs; sv->GetRotationMatrix(Rs);
		const VECTOR3 d = tmul(Rs, dg);                                                          // global -> ship
		// the ship's own touch screens first
		if (si.ext.Click && si.ext.Click(si.ctx, &o, &d, who.id))
		{
			oapiWriteLogV("OrbiterCrew: click (%ld,%ld) taken by a screen of %s", p.x, p.y, sv->GetName());
			return;
		}
		if (!si.fns.Count || !si.fns.Item || !si.fns.Use) return;
		const VECTOR3 head = inFeet + _V(0, 1.45, 0);
		double best = 1e9; int hit = -1, buttons = 0, nearId = -1; double nearMiss = 1e9, nearReach = 0;
		for (int i = 0, n = si.fns.Count(si.ctx); i < n; ++i)
		{
			OcItem it{};
			if (!si.fns.Item(si.ctx, i, &it) || it.kind != OC_BUTTON) continue;
			++buttons;
			const VECTOR3 v = it.pos - o;
			const double t = dotp(v, d);
			const double miss = t > 0 ? length(v - d * t) : 1e9, reach = length(it.pos - head);
			if (miss < nearMiss) { nearMiss = miss; nearId = it.id; nearReach = reach; }
			if (reach > 1.5) continue;                                                            // out of her reach
			if (miss < (std::max)(it.radius, 0.04) && t < best) { best = t; hit = it.id; }
		}
		// every click is logged: what it was aimed at (for the user's tests)
		oapiWriteLogV("OrbiterCrew: click at (%ld,%ld) of %.0fx%.0f, ray from (%.2f %.2f %.2f) dir (%.2f %.2f %.2f); %d buttons, nearest %d misses by %.3f m, %.2f m from her head -> %s",
			p.x, p.y, W, H, o.x, o.y, o.z, d.x, d.y, d.z, buttons, nearId, nearMiss, nearReach, hit >= 0 ? "pressed" : "nothing");
		if (hit < 0) return;
		si.fns.Use(si.ctx, hit, who.id);
		oapiWriteLogV("OrbiterCrew: %s presses button %d of %s", name.c_str(), hit, sv->GetName());
	}

	// her head: the right mouse button held, the mouse turns it (read here, before Orbiter's own input - Orbiter's
	// cockpit turning is off). Walking inside a ship her body follows the look (the user: she goes where she looks);
	// outside and in a seat the head turns within the neck (70 deg aside, 75 up and down); the keys turn the body
	double CrewMember::HeadStep(double dt, bool canTurn)
	{
		const bool eyes = oapiCameraInternal() && oapiCameraTarget() == GetHandle() && oapiGetFocusObject() == GetHandle();
		HWND fw = GetForegroundWindow(); DWORD pid = 0;
		if (fw) GetWindowThreadProcessId(fw, &pid);
		const bool rmb = eyes && fw && pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		mouseRmb = rmb;
		mouseMode = kMouseWalk && eyes && inShip && seat == 0 && canTurn;
		headStepped = true;
		if (!eyes) { headAsked = false; return 0; }
		// the mouse is Orbiter's (right button: it hides the cursor, keeps it in place and turns its cockpit camera): what
		// it turned since our last setting is the user's look, given to her head; the camera is then set where she looks
		double dYaw = 0, dPitch = 0;
		if (headAsked)
		{
			double y, p; LookDir(y, p);
			dYaw = std::remainder(y - headWantYaw, PI2);
			dPitch = p - headWantPitch;
		}
		double turnBy = 0;
		if (mouseMode) turnBy = dYaw;                                         // walking inside: she turns where she looks
		else headYaw = std::clamp(headYaw + dYaw, -70 * RAD, 70 * RAD);       // in a seat, outside: the neck
		if (mouseMode && !rmb) headYaw = Approach(headYaw, 0.0, 1.5, dt);     // walking on, the head comes back ahead
		headPitch = std::clamp(headPitch + dPitch, -75 * RAD, 75 * RAD);
		return turnBy;
	}

	// the camera where her head looks, in her vessel's frame (inside a ship that is the ship's frame: her heading is
	// in her pose), every step. Also a check: what was asked last step against what the camera shows now
	void CrewMember::AimHead(double dt)
	{
		if (!headStepped) HeadStep(dt, false);   // a step without walking (flying, lying): her head still turns
		headStepped = false;
		const bool eyes = oapiCameraInternal() && oapiCameraTarget() == GetHandle();
		if (!eyes) { headAsked = false; SuitHud::HelmetView hv; hv.eye = camEye; hv.show = false; hud.HelmetFrame(this, vis, hv); return; }
		if (headAsked && (headCheckT += dt) > 2.0)
		{
			headCheckT = 0;
			double y, p; LookDir(y, p);
			oapiWriteLogV("OrbiterCrew: head camera asked yaw %.1f pitch %.1f, shows yaw %.1f pitch %.1f (deg)",
				headWantYaw * DEG, headWantPitch * DEG, y * DEG, p * DEG);
		}
		const double yaw = inShip ? std::remainder(inHdg + headYaw - vesselYaw, PI2) : headYaw;
		// Orbiter's own limit is the person's: walking inside she turns all round (wider than a full turn: its limit never
		// bites); seated and outside the neck (70 deg aside, 75 up and down, about her vessel's nose - her body, her seat)
		if (mouseMode) SetCameraRotationRange(PI2, PI2, 75 * RAD, 75 * RAD);
		else SetCameraRotationRange(70 * RAD, 70 * RAD, 75 * RAD, 75 * RAD);
		// Orbiter's cockpit angles (Camera::SetCockpitDir, measured 2026-10-03): the first turns the view to the LEFT
		// (yaw = -first), the second up (pitch = second); set at once, not limited by the rotation range
		oapiCameraSetCockpitDir(-yaw, headPitch);
		headWantYaw = yaw; headWantPitch = headPitch; headAsked = true;
		// the helmet display: before her eyes, along her look, in the suit with the HUD on
		SuitHud::HelmetView hv;
		hv.eye = camEye; hv.yaw = yaw; hv.pitch = headPitch;
		hv.show = suitOn && hudOn;
		hud.HelmetFrame(this, vis, hv);
	}

	// the mouse: right button held - she turns with it; released - it looks around, the cursor kept in the middle
	double CrewMember::MouseLook(double dt, bool canTurn)
	{
		const bool rmb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0, alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
		mouseRmb = rmb;
		HWND fw = GetForegroundWindow(); DWORD pid = 0;
		if (fw) GetWindowThreadProcessId(fw, &pid);
		RECT rc{};
		const bool win = fw && pid == GetCurrentProcessId() && GetClientRect(fw, &rc) && rc.right > 0 && rc.bottom > 0;
		mouseMode = kMouseWalk && inShip && oapiCameraInternal() && canTurn && win   // through her eyes only; from outside the classic controls (the user)
			   // only inside a ship (the user); outside the keys turn her as before
			 && seat == 0 && oapiGetFocusObject() == GetHandle() && oapiCameraTarget() == GetHandle();
		if (!mouseMode) { mouseLocked = false; return 0; }
		const bool eyes = oapiCameraInternal();
		double turnBy = 0, y = 0, p = 0;
		if (rmb)
		{
			// Orbiter turns the camera; she turns to where it looks
			mouseLocked = false;
			LookDir(y, p);   // the camera's look about the ship's axes (her vessel keeps the ship's orientation)
			const double rel = std::remainder(y + vesselYaw - inHdg, PI2);
			turnBy = eyes ? rel : std::clamp(rel, -8.0 * dt, 8.0 * dt);   // through her eyes at once; from outside she turns to it
			return turnBy;
		}
		if (alt || !kMouseFreeLook) { mouseLocked = false; return 0; }   // the cursor is free (the user: without the right button as before)
		// released: the mouse looks around; the cursor goes back to the middle every step (the aim)
		POINT c{ rc.right / 2, rc.bottom / 2 }; ClientToScreen(fw, &c);
		POINT m; GetCursorPos(&m);
		const double k = 0.0025;   // rad per pixel
		const double dx = mouseLocked ? (m.x - c.x) * k : 0, dyv = mouseLocked ? (m.y - c.y) * k : 0;
		SetCursorPos(c.x, c.y);
		if (eyes)
		{
			if (!CalibrateLook()) { mouseLocked = true; return 0; }
			if (!mouseLocked) LookDir(freeYaw, freePitch);
			freeYaw = std::clamp(freeYaw + dx, -1.2, 1.2); freePitch = std::clamp(freePitch - dyv, -1.25, 1.25);   // the neck's range
			if (dx || dyv) SetLook(freeYaw, freePitch);
		}
		else if (CalibrateOrbit() && (dx || dyv))
		{
			oapiCameraRotAzimuth(dx * orbitKa);
			oapiCameraRotPolar(-dyv * orbitKp);
		}
		mouseLocked = true;
		return 0;
	}

	// the outside camera: how Orbiter's azimuth / polar turns move the view (its sign and frame are not documented):
	// one small probe turn, read back the next step, then turned back
	bool CrewMember::CalibrateOrbit()
	{
		if (orbitCal == 2) return true;
		if (orbitCal < 0) return false;
		double y, p;
		LookDir(y, p);
		if (orbitCal == 0) { orbitY0 = y; orbitP0 = p; oapiCameraRotAzimuth(0.1); oapiCameraRotPolar(0.05); orbitCal = 1; return false; }
		const double dy = std::remainder(y - orbitY0, PI2), dp = p - orbitP0;
		oapiCameraRotAzimuth(-0.1); oapiCameraRotPolar(-0.05);
		oapiWriteLogV("OrbiterCrew: orbit probe (azimuth 0.10, polar 0.05) -> yaw %+.3f pitch %+.3f", dy, dp);
		if (std::abs(dy) < 0.02) { orbitCal = -1; oapiWriteLogV("OrbiterCrew: outside camera mapping unknown - the mouse does not turn her there"); return false; }
		orbitKa = 0.1 / dy;                                        // azimuth per radian of yaw
		orbitKp = std::abs(dp) > 0.01 ? 0.05 / dp : 0;              // polar per radian of pitch (0: none)
		orbitCal = 2;
		return true;
	}

	// V: the camera goes to the ship (the one she is in, or the nearest) from outside; the focus and the controls stay
	// with her; V again - her own view back (the user, 2026-10-03, through «Тантра»)
	void CrewMember::ShipView()
	{
		if (shipView && oapiCameraTarget() != GetHandle())
		{
			shipView = false;
			if (who.viewOutside) { oapiCameraAttach(GetHandle(), CAM_OUTSIDE); const double d = oapiCameraTargetDist(); if (who.viewDist > 0.5 && d > 0) oapiCameraScaleDist(who.viewDist / d); }
			else oapiCameraAttach(GetHandle(), CAM_INSIDE);
			return;
		}
		// only for the one who controls the ship: seated in its seat (the user); standing, walking, outside - nothing
		if (seat != 2 || !inShip || !seatHelm) return;   // a helm seat (OC_HELM): the commander's, the navigator's
		OBJHANDLE ship = inShip;
		if (!ship)
		{
			VECTOR3 me; GetGlobalPos(me); double best = 5000;
			for (DWORD i = 0; i < oapiGetVesselCount(); ++i)
			{
				OBJHANDLE h = oapiGetVesselByIndex(i);
				if (h == GetHandle()) continue;
				VESSEL* v = oapiGetVesselInterface(h);
				if (std::strstr(v->GetClassNameA(), "OrbiterCrew")) continue;   // not a person or a pack
				VECTOR3 p; oapiGetGlobalPos(h, &p);
				const double d = length(p - me) - v->GetSize();
				if (d < best) { best = d; ship = h; }
			}
		}
		if (!ship) { Say("Рядом нет корабля"); return; }
		shipView = true;
		oapiCameraAttach(ship, CAM_OUTSIDE);   // the ship from outside; its camera is Orbiter's own from here (mouse, F2)
	}

	// the camera's direction relative to the body (her head): yaw right +, pitch up +
	void CrewMember::LookDir(double& yaw, double& pitch)
	{
		VECTOR3 g; oapiCameraGlobalDir(&g);
		MATRIX3 R; GetRotationMatrix(R);
		const VECTOR3 l = tmul(R, g);
		yaw = std::atan2(l.x, l.z); pitch = std::atan2(l.y, std::hypot(l.x, l.z));
	}

	// sets the look relative to the body, with the mapping found by Calibrate
	void CrewMember::SetLook(double yaw, double pitch)
	{
		if (lookSwap) oapiCameraSetCockpitDir(lookSa * yaw, lookSp * pitch);   // Orbiter 2016 as measured: the first angle turns aside
		else oapiCameraSetCockpitDir(lookSp * pitch, lookSa * yaw);
	}

	// Orbiter's polar/azimuth signs are not documented: one probe direction is set, and read back in the next frame
	// (the camera takes a new direction only when it updates). Returns true once the mapping is known
	bool CrewMember::CalibrateLook()
	{
		if (lookCal == 2) return true;
		if (lookCal < 0) return false;
		double y, p;
		LookDir(y, p);
		if (lookCal == 0) { lookY0 = y; lookP0 = p; oapiCameraSetCockpitDir(0.15, 0.3); lookCal = 1; return false; }
		// the documented order (polar 0.15 -> pitch, azimuth 0.30 -> yaw), or the two swapped (0.15 -> yaw, 0.30 -> pitch)
		const bool asDoc = std::abs(std::abs(y) - 0.3) < 0.05 && std::abs(std::abs(p) - 0.15) < 0.05;
		const bool swapped = std::abs(std::abs(y) - 0.15) < 0.05 && std::abs(std::abs(p) - 0.3) < 0.05;
		oapiWriteLogV("OrbiterCrew: look probe (0.15, 0.30) -> pitch %.3f yaw %.3f (%s)", p, y, asDoc ? "as documented" : swapped ? "swapped" : "unknown");
		if (!asDoc && !swapped) { lookCal = -1; oapiCameraSetCockpitDir(0, 0); oapiWriteLogV("OrbiterCrew: look mapping unknown - the body does not follow the mouse"); return false; }
		lookSwap = swapped; lookSa = y > 0 ? 1 : -1; lookSp = p > 0 ? 1 : -1; lookCal = 2;
		SetLook(lookY0, lookP0);
		oapiWriteLogV("OrbiterCrew: look mapping %s, yaw %+.0f, pitch %+.0f", lookSwap ? "swapped" : "as documented", lookSa, lookSp);
		return true;
	}

	// a step inside: the ship's air (its life support), its felt gravity, walking on its floors between its walls
	void CrewMember::InteriorStep(double dt)
	{
		ShipInterior* si = InteriorOf(inShip);
		if (!si || !oapiIsVessel(inShip)) { LeaveShip(); return; }
		VESSEL* sv = oapiGetVesselInterface(inShip);
		// a body made this frame must not be attached yet: Orbiter has not set it up, and moving the attachment of such a
		// child crashes it (SetAttachmentParams -> null +0x38; every stand-up crash 2026-10-03). It waits a few frames
		if (!inParent && attachWait > 0) { --attachWait; return; }
		if (!inParent) { const OBJHANDLE s = inShip; const VECTOR3 f0 = inFeet; const double h0 = inHdg; inShip = nullptr;
			EnterShip(s, f0, _V(std::sin(h0), 0, std::cos(h0))); if (!inShip) { who.where = Person::IN_WORLD; return; } }
		air = Air{}; air.body = "ship"; air.p = 101.3; air.T = 294; air.ppO2 = 21.2; air.ppCO2 = 0.04;
		double g = 9.81;
		if (si->fns.Gravity) { VECTOR3 gv{}; si->fns.Gravity(si->ctx, &inFeet, &gv); g = length(gv); }
		// the organism: walking costs, the ship's air, shielded (the ship's own radiation model belongs to the ship)
		const double v = std::hypot(fwd, lat);
		humanW = LocomotionPower(suitOn ? Mass() : bio.mass, v, g);
		double heat = 0;
		suit.vent = true; suit.ventPpO2 = air.ppO2; suit.ventPpCO2 = air.ppCO2; suit.pOut = air.p;
		if (suitOn) heat = suitResidual = suit.Step(dt, bio.O2Use(), bio.CO2Made(), 0, bio.Heat(), air.T);
		bio.Step(dt, humanW + (turn ? 25 : 0), suitOn ? suit.ppO2 : air.ppO2, suitOn ? suit.ppCO2 : air.ppCO2, air.p, suitOn, heat);
		bio.Irradiate(dt, 0);
		bio.Sustain(dt, true, suitOn ? &suit.water : nullptr, air.T, suitOn);

		// walking: the keys as outside, in the ship's frame
		Keys k = keysFresh ? keys : Keys{};
		keysFresh = false;
		if (seat) k = Keys{};                        // in a seat (or getting in / out of it) she does not walk
		if (carried > 0) { carried -= dt; k = Keys{}; fwd = lat = turn = accel = 0; }   // the ship carries her (a lift)
		if (seat)
		{
			// sitting down / standing up: the clip runs ~1 s; her place is eased onto the seat over its first half
			const double dur = 1.05;
			if (seat == 1 || seat == 3) seatT = (std::min)(1.0, seatT + dt / dur);
			if (seat == 1)
			{
				const double e = std::clamp(seatT / 0.5, 0.0, 1.0), s = e * e * (3 - 2 * e);
				inFeet = seatFrom + (seatFeet - seatFrom) * s;
				const double v = std::clamp((seatT - 0.3) / 0.7, 0.0, 1.0);   // the height: while the hips go down
				inFeet.y = seatFrom.y + (seatFeet.y - seatFrom.y) * v * v * (3 - 2 * v);
				double dh = std::remainder(seatHdg - seatHdgFrom, PI2);
				inHdg = seatHdgFrom + dh * s;
				// seated: the ship is told (its seat moves up to the console). The focus and the view stay with her - the
				// person is the one who acts, never the ship as such (the user, 2026-10-03)
				if (seatT >= 1) { seat = 2; inFeet = seatFeet; inHdg = seatHdg;
					if (ShipInterior* s2 = InteriorOf(inShip)) if (s2->ext.Seated) s2->ext.Seated(s2->ctx, seatId, who.id, 1);
				}
			}
			else if (seat == 2) { SeatPlace(seatId); inFeet = seatFeet; inHdg = seatHdg; }   // she goes with the seat
			else if (seat == 4)   // getting up: first the seat moves back from the console; she rides it, then rises
			{
				const VECTOR3 was = seatFeet;
				SeatPlace(seatId); inFeet = seatFeet; inHdg = seatHdg;
				seatT += dt;
				seatStill = length(seatFeet - was) < 1e-4 ? seatStill + dt : 0;
				if ((seatStill > 0.25 && seatT > 0.3) || seatT > 4)
				{
					seat = 3; seatT = 0;
					if (!kSeatClips)   // for now: she stands behind the seat at once (the free aisle), facing the way it faces
					{
						seat = 0; seatId = -1;
						const VECTOR3 f0 = _V(std::sin(seatHdg), 0, std::cos(seatHdg));
						VECTOR3 to = seatFeet - f0 * 0.95; to.y += 0.3;
						double fy = to.y; if (!si->fns.Ground || si->fns.Ground(si->ctx, &to, 0.45, &fy)) to.y = fy; else to = seatFeet;
						// clear of the walls and the vault there (the navigator's seat is close to the drum's end)
						if (si->fns.Walls) { VECTOR3 from = to; si->fns.Walls(si->ctx, &from, &to, 0.25, heightM > 0 ? heightM : 1.75); }
						inFeet = to; inHdg = seatHdg; fwd = lat = turn = accel = 0;
					}
				}
			}
			else if (seat == 3 && seatT < 1)   // getting up: the hips rise, the feet come down to the floor
			{
				const double v = std::clamp(seatT / 0.6, 0.0, 1.0);
				double fy = seatFeet.y; VECTOR3 fl = seatFeet;
				if (si->fns.Ground) { fl.y += 0.05; si->fns.Ground(si->ctx, &fl, 0.0, &fy); }
				inFeet = seatFeet; inFeet.y = seatFeet.y + (fy - seatFeet.y) * v * v * (3 - 2 * v);
			}
			else if (seat == 3 && seatT >= 1)
			{
				// up: the clip ends standing ~0.3 m in front of the seat, which has moved back from the console
				seat = 0; seatId = -1;
				const VECTOR3 f0 = _V(std::sin(seatHdg), 0, std::cos(seatHdg));
				VECTOR3 from = seatFeet; double fy0 = from.y; from.y += 0.3;
				if (si->fns.Ground && si->fns.Ground(si->ctx, &from, 0.45, &fy0)) from.y = fy0;
				VECTOR3 to = from + f0 * seatStandZ;
				if (si->fns.Walls) si->fns.Walls(si->ctx, &from, &to, 0.25, heightM > 0 ? heightM : 1.75);
				double fy = to.y; if (!si->fns.Ground || si->fns.Ground(si->ctx, &to, 0.45, &fy)) to.y = fy; else to = from;
				inFeet = to; inHdg = seatHdg; fwd = lat = turn = accel = 0;
			}
		}
		// the ship says whether one may walk now (takeoff, landing, the anamezon drive: no); she stays where she stands
		char why[96] = "";
		const bool shipLets = !si->ext.CanWalk || si->ext.CanWalk(si->ctx, why, sizeof why);
		if (!shipLets && (k.fwd || k.back || k.left || k.right || k.stepL || k.stepR) && messageTime <= 0)
			Say(why[0] ? Utf8(why) : std::string("Ходить сейчас нельзя"));
		const bool act = bio.CanAct() && shipLets;
		const double target = !act ? 0 : k.fwd && !k.back ? (k.run ? (std::min)(runSpeed, 3.0) : walkSpeed) : k.back && !k.fwd ? -0.9 : 0;
		const double nf = Approach(fwd, target, std::abs(target) > std::abs(fwd) ? 2.5 : 6.0, dt);
		accel = (nf - fwd) / dt; fwd = nf;
		// inside a ship the mouse turns her (an experiment, the user 2026-10-03; outside as before): A/D step aside.
		// Through her eyes the body follows the look at once (the view stays where the mouse put it)
		const double look = HeadStep(dt, act && !seat && carried <= 0);   // the body's turn the mouse asks for (right button)
		const bool mouseTurn = mouseMode && mouseRmb;   // right button held: the mouse turns, A/D step aside; released: A/D turn as outside
		const double side = mouseTurn ? std::clamp(static_cast<double>((k.right - k.left) + (k.stepR - k.stepL)), -1.0, 1.0) : static_cast<double>(k.stepR - k.stepL);
		lat = Approach(lat, act ? std::clamp(side, -1.0, 1.0) * 0.9 : 0, 4.0, dt);
		const double keyTurn = mouseTurn ? 0.0 : (k.right - k.left) * 1.8;
		if (mouseTurn)   // the mouse turns her at once; 'turn' only feeds the animation
		{
			turn = Approach(turn, act ? std::clamp(look / dt, -3.0, 3.0) : 0, 12.0, dt);
			inHdg += look;
		}
		else             // A/D turn her as outside (and past 60 deg of the head the body follows the look)
		{
			turn = Approach(turn, act ? keyTurn : 0, 12.0, dt);
			inHdg += look + turn * dt;
			// from outside the camera is in the ship's frame: her keys' turn takes it along, it stays behind her
			// (through her eyes AimHead does it: her head turns with her body)
			const double dk = turn * dt;
			if (dk != 0 && oapiCameraTarget() == GetHandle() && !seat && !oapiCameraInternal() && CalibrateOrbit()) oapiCameraRotAzimuth(dk * orbitKa);
		}
		inHdg = std::fmod(inHdg + PI2, PI2);
		const VECTOR3 f = _V(std::sin(inHdg), 0, std::cos(inHdg)), r = _V(std::cos(inHdg), 0, -std::sin(inHdg));
		VECTOR3 to = inFeet + f * (fwd * dt) + r * (lat * dt);
		if (seat || carried > 0) to = inFeet;
		if (si->fns.Walls) si->fns.Walls(si->ctx, &inFeet, &to, 0.25, heightM > 0 ? heightM : 1.75);
		double fy = to.y;
		if (seat || carried > 0) {}
		else if (!si->fns.Ground || si->fns.Ground(si->ctx, &to, 0.45, &fy)) { to.y = fy; inFeet = to; }
		else { fwd = lat = 0; }                       // no floor there: she does not step into the void
		// the vessel does not turn with her (the ship's screens are rendered from cameras on this focus body; a turning
		// body made their zones flicker): its frame stays the ship's, her heading is in the pose (Motion heading)
		sv->SetAttachmentParams(inParent, inFeet + si->Origin(), _V(std::sin(vesselYaw), 0, std::cos(vesselYaw)), _V(0, 1, 0));
		if (GetAttachmentStatus(inChild) != inShip) sv->AttachChild(GetHandle(), inParent, inChild);

		const int req = hud.TakeRequest();
		if (req >= 0) ApRequest(req);
		ClickInside(*si);
		HudBySuit();
		Animate(dt, g, true);
	}

	// what is within reach: the nearest entrance of a ship (outside: its lifts and airlocks)
	void CrewMember::FindUse(double dt)
	{
		useScan -= dt;
		if (useScan > 0) return;
		useScan = 0.2;
		useShip = nullptr; useId = -1; useHint.clear();
		if ((who.where != Person::IN_WORLD && who.where != Person::INTERIOR) || !bio.CanAct()) return;
		if (inShip && seat) { if (seat == 2) { useShip = inShip; useHint = "встать"; } return; }   // in the seat: F - stand up
		if (inShip)
		{
			ShipInterior* si = InteriorOf(inShip);
			if (!si || !si->fns.Count || !si->fns.Item) return;
			double best = 1e9;
			const int n = si->fns.Count(si->ctx);
			for (int i = 0; i < n; ++i)
			{
				OcItem it{};
				if (!si->fns.Item(si->ctx, i, &it) || it.kind == OC_BUTTON) continue;   // buttons: the mouse
				// the item she has come up to: within reach, and the one she faces (a neighbouring seat may be as near)
				const VECTOR3 dv = it.pos - inFeet;
				const double d = std::hypot(dv.x, dv.z);
				if (d >= it.radius || std::abs(dv.y) >= 2.0) continue;
				const double facing = d > 0.05 ? (dv.x * std::sin(inHdg) + dv.z * std::cos(inHdg)) / d : 1.0;   // cos of the angle off her nose
				if (facing < 0.2) continue;                                           // beside or behind her
				const double score = (1.0 - facing) * 2.0 + d / it.radius;
				if (score < best) { best = score; useShip = inShip; useId = it.id; useKind = it.kind; useHint = Utf8(it.label); }
			}
			return;
		}
		VECTOR3 feet; Local2Global(_V(0, -height, 0), feet);
		double best = 1e9;
		for (ShipInterior& si : interiors())
		{
			if (!oapiIsVessel(si.ship) || !si.fns.Count || !si.fns.Item) continue;
			VECTOR3 sp; oapiGetGlobalPos(si.ship, &sp);
			VESSEL* sv = oapiGetVesselInterface(si.ship);
			if (length(sp - feet) > sv->GetSize() + 50) continue;
			const int n = si.fns.Count(si.ctx);
			for (int i = 0; i < n; ++i)
			{
				OcItem it{};
				if (!si.fns.Item(si.ctx, i, &it)) continue;
				if (it.kind != OC_LIFT && it.kind != OC_AIRLOCK && it.kind != OC_EXIT) continue;   // from outside: the entrances
				VECTOR3 g; sv->Local2Global(it.pos, g);
				const double d = length(g - feet);
				if (d < it.radius && d < best) { best = d; useShip = si.ship; useId = it.id; useHint = Utf8(it.label); }
			}
		}
	}

	bool CrewMember::DoUse()
	{
		if (seat == 2) { StandFromSeat(); return true; }
		if (seat) return true;   // getting in or out of the seat
		if (useId < 0 || !useShip) return false;
		if (inShip && useShip == inShip && (useKind == OC_SEAT || useKind == OC_HELM) && seat == 0)
			if (ShipInterior* si = InteriorOf(inShip)) if (si->ext.Seated && si->fns.Seat && Active().ok && Active().clips.seats) { SitDown(useId); return true; }
		for (ShipInterior& si : interiors())
			if (si.ship == useShip && si.fns.Use && oapiIsVessel(si.ship)) { si.fns.Use(si.ctx, useId, who.id); return true; }
		return false;
	}

	// a button of the suit computer: the pack's height hold and landing, or one of the autopilots on the selected target
	void CrewMember::ApRequest(int req)
	{
		if (jet.Worn() && !jet.Assist() && (req == SuitHud::AP_ALT || req == SuitHud::AP_LAND || req == Autopilot::HOVER || req == Autopilot::TRANSFER))
			jet.SetAssist(true);   // the autopilots fly through the assistant
		if (req == SuitHud::AP_ALT)
		{
			if (!jet.Worn()) return;
			if (ap.Get() != Autopilot::OFF) ap.Off(this, "удержание высоты");
			else if (jet.ModeId() == 1) jet.SetManual();
			else jet.SetHold((std::max)(1.5, jet.AltNow()));
			return;
		}
		if (req == SuitHud::AP_LAND)
		{
			if (!jet.Worn()) return;
			ap.Off(this, "посадка");
			if (jet.ModeId() == 2) jet.SetManual(); else jet.SetDescent();
			return;
		}
		if (req >= SuitHud::AP_CRUISE && req < SuitHud::AP_CRUISE + 20)   // the ПОЛЁТ page: the cruise's height and speed
		{
			if (!jet.Worn()) return;
			ap.Off(this, "круиз");
			static const double A[4] = { -10, -1, 1, 10 }, S[5] = { -5, -1, 0, 1, 5 };
			const int k = req - SuitHud::AP_CRUISE;
			if (k < 4) jet.Nudge(A[k], 0, false); else if (k == 12) jet.Stop(); else if (k >= 10 && k < 15) jet.Nudge(0, S[k - 10], false);
			return;
		}
		if (req == SuitHud::AP_STOP)   // full stop: every speed to zero, the height held
		{
			if (!jet.Worn()) return;
			ap.Off(this, "стоп");
			jet.Stop();
			return;
		}
		if (req == SuitHud::AP_MANUAL)
		{
			if (!jet.Worn()) return;
			jet.SetAssist(!jet.Assist());
			if (!jet.Assist()) { ap.Off(this, "ручной режим"); if (rcsLive) SetAttitudeMode(RCS_ROT); }
			Say(jet.Assist() ? "Ранец: помощник - безопасная тяга, Shift+пробел полная" : "Ранец: ручное - пробел / Ctrl тяга больше / меньше");
			return;
		}
		if (req == SuitHud::AP_FINE) { if (jet.Worn()) { jet.SetFine(!jet.Fine()); Say(jet.Fine() ? "Ранец: ограничитель - точное управление" : "Ранец: ограничитель выключен"); } return; }
		if (req == SuitHud::AP_FIELD) { if (suitOn) { suit.fieldOn = !suit.fieldOn; Say(suit.fieldOn ? "Магнитное поле включено" : "Магнитное поле выключено"); } return; }
		if (req == SuitHud::AP_SHADE) { if (suitOn) { shadeTarget = shadeTarget > 0.5 ? 0 : 1; Say(shadeTarget > 0.5 ? "Щиток опущен" : "Щиток поднят"); } return; }
		if (req == SuitHud::AP_LAMP) { if (suitOn) { lampOn = !lampOn; Say(lampOn ? "Фонари включены" : "Фонари выключены"); } return; }
		if (req >= Autopilot::HOVER && req <= Autopilot::DOCK) ap.Engage(static_cast<Autopilot::Mode>(req), hud.SelectedTarget(), this);
	}

	std::string CrewDisplayName(OBJHANDLE h)
	{
		for (CrewMember* c : everyone) if (c->GetHandle() == h) return c->DisplayName();
		return std::string();
	}
}

DLLCLBK VESSEL* ovcInit(OBJHANDLE hVessel, int fModel) { return new ocrew::CrewMember(hVessel, fModel); }
DLLCLBK void ovcExit(VESSEL* vessel) { delete static_cast<ocrew::CrewMember*>(vessel); }

// ---- the interface for ships (include/OrbiterCrewApi.h) ----

namespace
{
	ocrew::CrewMember* BodyOf(OBJHANDLE h)
	{
		for (ocrew::CrewMember* c : ocrew::everyoneRef()) if (c->GetHandle() == h) return c;
		return nullptr;
	}
}
extern "C" __declspec(dllexport) int ocCreatePerson(const char* name, const char* role, double age, double massKg)
{
	ocrew::Person& p = ocrew::Crew::Create();
	if (name) p.name = name;
	if (role) p.role = role;
	if (age > 0) p.age = age;
	if (massKg > 0) p.body.mass = massKg;
	return p.id;
}
extern "C" __declspec(dllexport) void ocSetAboard(int id, OBJHANDLE ship)
{
	if (ocrew::Person* p = ocrew::Crew::Find(id)) { p->where = ocrew::Person::ABOARD; p->vessel = ship; }
}
extern "C" __declspec(dllexport) int ocPersonOfBody(OBJHANDLE body)
{
	ocrew::CrewMember* c = BodyOf(body);
	return c ? c->Who().id : 0;
}
extern "C" __declspec(dllexport) int ocBoard(OBJHANDLE body, OBJHANDLE ship)
{
	ocrew::CrewMember* c = BodyOf(body);
	if (!c) return 0;
	ocrew::Person& p = c->Who();
	c->KeepWorn();                     // the suit's cold gas as the body holds it now
	p.where = ocrew::Person::ABOARD; p.vessel = ship;
	if (oapiGetFocusObject() == body) oapiSetFocusObject(ship);
	oapiDeleteVessel(body);            // the destructor lets the worn items go and keeps ABOARD
	return p.id;
}
extern "C" __declspec(dllexport) OBJHANDLE ocDisembark(int id, const char* vesselName, const VESSELSTATUS2* vs)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || !vs || p->where == ocrew::Person::IN_WORLD) return nullptr;
	ocrew::Crew::ExpectBody(id);
	OBJHANDLE h = oapiCreateVesselEx(vesselName, p->bodyClass.c_str(), vs);
	ocrew::Crew::ExpectBody(0);
	if (ocrew::CrewMember* c = BodyOf(h)) c->takeView = true;   // the camera goes with the person, as the user last had it
	return h;
}
extern "C" __declspec(dllexport) int ocInfo(int id, OcInfo* out)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || !out) return 0;
	std::memset(out, 0, sizeof *out);
	strncpy_s(out->name, p->name.c_str(), _TRUNCATE); strncpy_s(out->role, p->role.c_str(), _TRUNCATE); strncpy_s(out->sex, p->sex.c_str(), _TRUNCATE);
	out->age = p->age; out->heightM = p->heightM; out->massKg = p->body.mass;
	out->pulse = p->body.pulse; out->coreT = p->body.coreT; out->state = static_cast<int>(p->body.state);
	out->where = static_cast<int>(p->where); out->vessel = p->vessel;   // 3 = INTERIOR (the body)
	return 1;
}
extern "C" __declspec(dllexport) void ocSavePerson(int id, FILEHANDLE scn)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || p->where == ocrew::Person::INTERIOR) return;   // walking inside: his or her body saves the person
	oapiWriteScenario_int(scn, const_cast<char*>("OC_PERSON"), p->id);
	p->Save(scn);
	oapiWriteScenario_string(scn, const_cast<char*>("OC_END"), const_cast<char*>(""));
}
extern "C" __declspec(dllexport) int ocLoadPerson(const char* lines)
{
	if (!lines) return 0;
	ocrew::Person& p = ocrew::Crew::Create();
	std::istringstream all(lines); std::string line;
	while (std::getline(all, line))
	{
		std::istringstream ss(line); std::string key; ss >> key;
		if (!key.empty()) p.LoadLine(key, ss);
	}
	p.worn.suit.fromScenario = true; p.worn.packFromScenario = true;   // what is worn is as saved
	return p.id;
}
extern "C" __declspec(dllexport) void ocRegisterInterior(OBJHANDLE ship, const OcInterior* fns, void* ctx)
{
	auto& v = ocrew::interiors();
	v.erase(std::remove_if(v.begin(), v.end(), [&](const ocrew::ShipInterior& s) { return s.ship == ship; }), v.end());
	if (fns) v.push_back({ ship, *fns, ctx });
}
extern "C" __declspec(dllexport) void ocUnregisterInterior(OBJHANDLE ship)
{
	auto& v = ocrew::interiors();
	v.erase(std::remove_if(v.begin(), v.end(), [&](const ocrew::ShipInterior& s) { return s.ship == ship; }), v.end());
}
extern "C" __declspec(dllexport) void ocSetInteriorExt(OBJHANDLE ship, const OcInteriorExt* ext)
{
	ocrew::ShipInterior* si = ocrew::InteriorOf(ship);
	if (!si) return;
	si->ext = OcInteriorExt{};
	if (ext && ext->size > 0) std::memcpy(&si->ext, ext, (std::min)(static_cast<size_t>(ext->size), sizeof si->ext));
	si->ext.size = sizeof si->ext;
}
extern "C" __declspec(dllexport) OBJHANDLE ocShipOf(int id)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p) return nullptr;
	return p->where == ocrew::Person::ABOARD ? p->vessel : p->where == ocrew::Person::INTERIOR ? p->ship : nullptr;
}
namespace
{
	std::string FreeName(const std::string& base)
	{
		std::string v = base; for (char& c : v) if (c == ' ') c = '_';
		std::string out = v;
		for (int i = 2; oapiGetVesselByName(const_cast<char*>(out.c_str())); ++i) out = v + "_" + std::to_string(i);
		return out;
	}
}
// aboard without a body -> the body stands in the interior at that place (ship frame); the focus goes to the body
extern "C" __declspec(dllexport) OBJHANDLE ocEnterInterior(int id, const VECTOR3* pos, const VECTOR3* dir)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || !pos || p->where != ocrew::Person::ABOARD || !p->vessel || !oapiIsVessel(p->vessel)) return nullptr;
	OBJHANDLE ship = p->vessel;
	if (!ocrew::InteriorOf(ship)) return nullptr;
	VESSEL* sv = oapiGetVesselInterface(ship);
	VESSELSTATUS2 vs; std::memset(&vs, 0, sizeof vs); vs.version = 2;
	sv->GetStatusEx(&vs);                               // where the ship is; the attachment places her at once
	vs.flag = 0; vs.fuel = nullptr; vs.thruster = nullptr; vs.dockinfo = nullptr; vs.nfuel = vs.nthruster = vs.ndockinfo = 0;
	const VECTOR3 d = dir ? *dir : _V(0, 0, 1);
	auto& pi = ocrew::PendingInterior(); pi.ship = ship; pi.feet = *pos; pi.hdg = std::atan2(d.x, d.z);
	p->ship = ship;
	ocrew::Crew::ExpectBody(id);
	OBJHANDLE h = oapiCreateVesselEx(FreeName(p->name).c_str(), p->bodyClass.c_str(), &vs);
	ocrew::Crew::ExpectBody(0); pi = {};
	if (!h) return nullptr;
	// attaching a vessel in the frame it is made in, and moving the focus inside the ship's key callback, crash Orbiter:
	// the body hangs itself on the ship, takes the focus and the camera in its own first step
	if (ocrew::CrewMember* c = BodyOf(h)) c->takeView = true;
	return h;
}
// the person's body walks into the ship (through a lift or an airlock): the same body, now inside at that place
// (interior frame); the focus and the camera stay as they are
extern "C" __declspec(dllexport) int ocEnterShip(OBJHANDLE body, OBJHANDLE ship, const VECTOR3* pos, const VECTOR3* dir)
{
	ocrew::CrewMember* c = BodyOf(body);
	if (!c || !pos || c->inShip || c->Who().where != ocrew::Person::IN_WORLD || !ocrew::InteriorOf(ship)) return 0;
	c->EnterShip(ship, *pos, dir ? *dir : _V(0, 0, 1));
	return c->inShip ? c->Who().id : 0;
}
extern "C" __declspec(dllexport) int ocCarry(int id, OBJHANDLE ship, const VECTOR3* pos, const VECTOR3* dir)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || !pos || p->where != ocrew::Person::INTERIOR || p->ship != ship) return 0;
	ocrew::CrewMember* c = BodyOf(p->vessel);
	if (!c || c->SeatState()) return 0;
	c->inFeet = *pos;
	if (dir) c->inHdg = std::atan2(dir->x, dir->z);
	c->carried = 0.3;
	return 1;
}
extern "C" __declspec(dllexport) int ocInteriorPos(int id, VECTOR3* feet, double* hdg)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || p->where != ocrew::Person::INTERIOR) return 0;
	ocrew::CrewMember* c = BodyOf(p->vessel);
	if (!c || !c->inShip) return 0;
	if (feet) *feet = c->inFeet;
	if (hdg) *hdg = c->inHdg;
	return 1;
}
// is the person in the space suit (1), or not (0); -1 no such person
extern "C" __declspec(dllexport) int ocSuitWorn(int id)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p) return -1;
	if (p->where == ocrew::Person::IN_WORLD || p->where == ocrew::Person::INTERIOR)
		if (ocrew::CrewMember* c = BodyOf(p->vessel)) c->KeepWorn();   // the body's own state into the person
	return p->worn.suit.on ? 1 : 0;
}
extern "C" __declspec(dllexport) int ocStand(int id)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || p->where != ocrew::Person::INTERIOR) return 0;
	ocrew::CrewMember* c = BodyOf(p->vessel);
	if (!c || c->SeatState() != 2) return 0;
	c->StandFromSeat();
	return 1;
}
// the body leaves the world (into the seat 'seatId', or stored with -1); the focus goes to the ship
extern "C" __declspec(dllexport) void ocLeaveInterior(int id, int seatId)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || p->where != ocrew::Person::INTERIOR) return;
	ocrew::CrewMember* c = BodyOf(p->vessel);
	OBJHANDLE ship = p->ship;
	if (c) { c->KeepWorn(); c->LeaveShip(); }
	p->where = ocrew::Person::ABOARD; p->vessel = ship;
	if (c) { if (oapiGetFocusObject() == c->GetHandle() && ship) oapiSetFocusObject(ship); oapiDeleteVessel(c->GetHandle()); }
	(void)seatId;
}
// out of the ship (an airlock): the body stands at vs, detached, in the world
extern "C" __declspec(dllexport) OBJHANDLE ocExitTo(int id, const char* vesselName, const VESSELSTATUS2* vs)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || !vs) return nullptr;
	if (p->where == ocrew::Person::INTERIOR)
	{
		ocrew::CrewMember* c = BodyOf(p->vessel);
		if (!c) return nullptr;
		c->LeaveShip();
		c->DefSetStateEx(vs);
		p->where = ocrew::Person::IN_WORLD;
		return c->GetHandle();
	}
	if (p->where == ocrew::Person::ABOARD) return ocDisembark(id, vesselName, vs);
	return nullptr;
}
DLLCLBK void ExitModule(HINSTANCE) { ocrew::Crew::Clear(); ocrew::interiors().clear(); }   // the simulation ends: nobody is left
