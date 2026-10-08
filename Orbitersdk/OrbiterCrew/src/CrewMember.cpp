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
#include <atomic>

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

	// ---- context actions: the wheel and helpers ----
	namespace
	{
		// the wheel while a list of two or more is shown is hers (the view keeps it otherwise; Shift+wheel: always the view)
		WNDPROC g_prevProc{}; HWND g_hookWnd{}; std::atomic<int> g_wheel{ 0 }; std::atomic<bool> g_wheelEat{ false }; int g_ctxUsers{};
		LRESULT CALLBACK CtxWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
		{
			if (m == WM_MOUSEWHEEL && g_wheelEat.load() && !(GET_KEYSTATE_WPARAM(w) & MK_SHIFT)) { g_wheel += GET_WHEEL_DELTA_WPARAM(w); return 0; }
			return CallWindowProc(g_prevProc, h, m, w, l);
		}
		void HookWheel()
		{
			if (g_hookWnd) return;
			gcCore2* core = gcGetCoreInterface();
			HWND v = core ? core->GetRenderWindow() : nullptr;
			if (!v) return;
			g_prevProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(v, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CtxWndProc)));
			g_hookWnd = v;
		}
		void UnhookWheel()
		{
			if (g_hookWnd && IsWindow(g_hookWnd) && reinterpret_cast<WNDPROC>(GetWindowLongPtr(g_hookWnd, GWLP_WNDPROC)) == CtxWndProc)
				SetWindowLongPtr(g_hookWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_prevProc));
			g_hookWnd = nullptr; g_prevProc = nullptr;
		}
		std::string ToCp1251(const std::string& u8)   // our UTF-8 into the ships' code page (an OcAction's label)
		{
			int n = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0); std::wstring w(n > 0 ? n : 1, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, &w[0], n);
			int m = WideCharToMultiByte(1251, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string o(m > 0 ? m : 1, '\0');
			WideCharToMultiByte(1251, 0, w.c_str(), -1, &o[0], m, nullptr, nullptr);
			if (!o.empty() && o.back() == '\0') o.pop_back();
			return o;
		}
		std::wstring Wide(const char* s, UINT cp) { int n = MultiByteToWideChar(cp, 0, s, -1, nullptr, 0); std::wstring w(n > 0 ? n - 1 : 0, L' '); if (n > 1) MultiByteToWideChar(cp, 0, s, -1, &w[0], n); return w; }
		const DWORD kAmber = 0x30B0FF, kGrey = 0x707070, kDark = 0x0C0C0C, kSelBg = 0x123048;   // Sketchpad colours: 0xBBGGRR
	}


	struct PendingPlace { OBJHANDLE ship{}; VECTOR3 feet{}; double hdg{}; int seat{ -1 }; };   // seat >= 0: the body is made in it (ocSitAt)
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
		if (reborn && PendingInterior().ship) { inShip = PendingInterior().ship; inFeet = PendingInterior().feet; inHdg = PendingInterior().hdg; attachWait = 3;
			if (PendingInterior().seat >= 0) { seat = 2; seatId = PendingInterior().seat; seatT = 1; } }   // made seated: EnterShip puts her in it, the ship is told
		PendingInterior() = {};
		if (reborn) { suitFromScenario = true; jetFromScenario = true; }   // what is worn keeps its state
		who.where = inShip ? Person::INTERIOR : Person::IN_WORLD; who.vessel = hVessel; if (inShip) who.ship = inShip;
	}

	CrewMember::~CrewMember()
	{
		everyone.erase(std::remove(everyone.begin(), everyone.end(), this), everyone.end());
		if (everyone.empty()) UnhookWheel();   // the render window's wheel back to Orbiter alone
		if (cursorHidden) { ShowCursor(TRUE); cursorHidden = false; }
		if (cxSurf) { oapiDestroySurface(cxSurf); cxSurf = nullptr; }
		if (gaugeSurf) { oapiDestroySurface(gaugeSurf); gaugeSurf = nullptr; }
		if (gaugeShadow) { oapiDestroySurface(gaugeShadow); gaugeShadow = nullptr; }
		if (cxShadow) { oapiDestroySurface(cxShadow); cxShadow = nullptr; }
		for (oapi::Font*& f : cxFont) if (f) { oapiReleaseFont(f); f = nullptr; }
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
		{ int dl = 0; if (oapiReadItem_int(cfg, const_cast<char*>("DebugLog"), dl)) debugLog = dl != 0; }
		{ int ct = 0; if (oapiReadItem_int(cfg, const_cast<char*>("CtxTest"), ct)) ctxTest = ct != 0; }   // test lines in Orbiter.log (clicks, the head camera)
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
			else if (key == "HELD")   // what she holds: kind mass w h d mesh|- label...
			{
				OcHeld h{}; h.size = sizeof h; std::string mesh, label;
				ss >> h.kind >> h.massKg >> h.dims[0] >> h.dims[1] >> h.dims[2] >> mesh; std::getline(ss >> std::ws, label);
				if (mesh != "-") std::snprintf(h.mesh, sizeof h.mesh, "%s", mesh.c_str());
				std::snprintf(h.label, sizeof h.label, "%s", label.c_str());
				held = h; heldPending = true;
			}
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
		if (holding)
		{
			char b[200]; std::snprintf(b, sizeof b, "%d %.3f %.3f %.3f %.3f %s %s", held.kind, held.massKg, held.dims[0], held.dims[1], held.dims[2],
				held.mesh[0] ? held.mesh : "-", held.label);
			oapiWriteScenario_string(scn, const_cast<char*>("HELD"), b);
		}
		if (inShip && oapiIsVessel(inShip))
		{
			char b[160]; std::snprintf(b, sizeof b, "%.3f %.3f %.3f %.4f %s", inFeet.x, inFeet.y, inFeet.z, inHdg, oapiGetVesselInterface(inShip)->GetName());
			oapiWriteScenario_string(scn, const_cast<char*>("INTERIOR"), b);
			if (seat == 2 || seat == 4) oapiWriteScenario_int(scn, const_cast<char*>("SEAT"), seatId);
		}
	}

	void CrewMember::clbkPostCreation()
	{
		if (heldPending) { heldPending = false; const OcHeld h = held; Give(h, false); }   // what she held when saved
		air = atmospheres.Sample(this);
		if (!suitFromScenario) suitOn = !air.Breathable();
		// the pack's fuel: its own line (JET_FUEL) or, in older scenarios, the body's PRPLEVEL
		if (jetFromScenario && suitOn) jet.Wear(jet.FuelKnown() ? jet.Kept() : GetPropellantMass(jet.Propellant()));
		// the suit's cold gas: its own line (SUIT_N2), or a full bottle when the scenario does not say (an unlisted tank
		// is left empty). The pack's fuel is not touched here - it came with the pack above
		if (n2) SetPropellantMass(n2, who.worn.suit.n2Kg >= 0 ? who.worn.suit.n2Kg : GetPropellantMaxMass(n2));
		SetEmptyMass(Mass());
		ShowFigure();
		if (!suitFig.ok) oapiWriteLogV("OrbiterCrew: %s has no suit figure yet, the coverall stands in for it", name.c_str());
		sound.Init(this, voice);
		// (gcCore RENDERPROC_HUD_2ND is not used: D3D9Client 30.7 of Orbiter 2024 passes it a null matrix and crashes -
		// dump 2026-10-04, D3D9Client+0x26594. The suit computer's display is the VC HUD on the helmet plate instead)
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
		if (bodyFig.ok) SetMeshVisibilityMode(bodyFig.mesh, &on == &bodyFig ? MESHVIS_ALWAYS | MESHVIS_VC | MESHVIS_EXTPASS : MESHVIS_NEVER);   // one depth with the ship around her
		if (suitFig.ok) SetMeshVisibilityMode(suitFig.mesh, &on == &suitFig ? MESHVIS_ALWAYS | MESHVIS_VC | MESHVIS_EXTPASS : MESHVIS_NEVER);
	}

	// her eyes: an empty virtual cockpit (no mesh) - nothing of a ship's instruments over them, the VC's own near plane.
	// Without the suit there is nothing between her eyes and the world; in the suit its computer's display is on the
	// helmet plate (the VC HUD, SuitHud)
	bool CrewMember::clbkLoadVC(int)
	{
		SetCameraDefaultDirection(_V(0, 0, 1));
		SetCameraRotationRange(PI * 0.98, PI * 0.98, PI05 * 0.95, PI05 * 0.95);
		SetCameraCatchAngle(0);
		hud.RegisterVC();   // the helmet display's plate is the VC HUD (the suit computer draws into it: clbkDrawHUD)
		// the caption drawn by the display itself, last, into the same surface as its own signs (its light layer, or the VC
		// HUD when paused): the same layer and sharpness as the HUD («Архитектор - скафандр», 2026-10-07)
		hud.overlay = [this](oapi::Sketchpad* s, double W, double H, double kx, double ky) { CtxPaintPlate(s, W, H, kx, ky); };
		return true;
	}



	void CrewMember::SetSuit(bool on)
	{
		if (!bio.CanAct()) return;
		if (on && seat) { Say("В кресле скафандр не надеть - сначала встаньте (F)"); return; }   // no sitting in the suit yet
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
		if (key == OAPI_KEY_X && !KEYMOD_CONTROL(kstate) && !KEYMOD_ALT(kstate)) { if (down && holding) DropHeld(KEYMOD_SHIFT(kstate)); return holding || !down ? 1 : 0; }
		if (KEYMOD_CONTROL(kstate) || KEYMOD_ALT(kstate)) return 0;
		if (key == OAPI_KEY_F) { if (down && !CtxUse()) DoUse(); return 1; }   // the action: the node she looks at first
		// held at a machine's post (ocCarry, e.g. the MPU's driver): its keys drive the machine (it reads them itself) -
		// no jump, no pack, no B here; F still lets her step off
		if (inShip && carried > 0 && (key == OAPI_KEY_SPACE || key == OAPI_KEY_B || key == OAPI_KEY_K)) return 1;
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
			if (down && !airborne && !lying && bio.CanAct() && !jet.Worn() && !holding) Jump();   // with the pack Space is "up"; not with her hands full
			return 1;
		}
		for (DWORD k : MOVE_KEYS) if (key == k) return 1;
		return 0;
	}

	void CrewMember::Drive(double dt, double g)
	{
		const Keys k = keysFresh && !cx.busy ? keys : Keys{};   // a long action: she stays where she is
		// three profiles: coverall; suit with live drives (Shift = servo boost); suit with a flat battery (dead weight)
		const bool drives = suitOn && suit.Drives();
		double walkV = walkSpeed, runV = (std::min)(runSpeed, bio.RunLimit());
		if (drives) runV = 4.2 + 2.3 * std::sqrt(bio.wbal);
		else if (suitOn) { walkV = 1.0; runV = (std::min)(2.2, bio.RunLimit()); }
		if (holding) { const double f = HeldSlow(); walkV *= f; runV = HeldWeight() > 0.6 * CarryLimit() ? walkV : runV * f; }   // her hands full
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
			ClampToShips(s);   // the ships' outer solids (legs, pads): she does not walk through them
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

	void CrewMember::Settle(double simt, double dt)
	{
		OBJHANDLE body = GetSurfaceRef();
		if (!body || !(GetFlightStatus() & 1) || airborne) { settling = false; return; }   // not standing: nothing to hold
		double lng, la, rad; GetEquPos(lng, la, rad);
		const double e = oapiSurfaceElevation(body, lng, la);
		if (std::abs(e - settleElev) > 0.005) { settleElev = e; settleStill = 0; } else settleStill += dt;
		VESSELSTATUS2 s = Status(this); s.status = 1; DefSetStateEx(&s);   // landed again: on the ground as it is now
		if ((settleStill > 1.0 && simt > 1.0) || simt > 15.0)
		{
			settling = false;
			oapiWriteLogV("OrbiterCrew: %s: the ground under her settled at %.2f m (sim %.1f s)", name.c_str(), e, simt);
		}
	}

	void CrewMember::GroundContactCheck(double dt)
	{
		if (settling) return;   // the terrain is still coming in
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

	bool CrewMember::Struck(const VECTOR3& vStrike, double strikerMass, const VECTOR3& nIn)
	{
		if (inShip || length(nIn) < 1e-6) return false;
		const VECTOR3 n = nIn / length(nIn);
		const double vn = dotp(vStrike, n);
		// a slow touch is not a blow: she takes up to ~1 m/s by stepping back (the machines settling at the start touched her at
		// 0.34 m/s and threw her - the user, 2026-10-05: «подскок при старте»); nor in her first seconds
		if (settling || vn < 1.0 || oapiGetSimTime() - bornSim < 3.0) return false;
		const double m = GetMass(), kick = vn * 1.2 * strikerMass / (strikerMass + m);   // the light side takes the heavy one's speed
		VESSELSTATUS2 s = Status(this);
		if (s.status == 1 && s.rbody)   // on the ground: thrown along the ground, sliding to a stop (friction 0.6), at most 3 m
		{
			VECTOR3 me, pc; GetGlobalPos(me); oapiGetGlobalPos(s.rbody, &pc);
			const VECTOR3 up = unit(me - pc);
			VECTOR3 h = n - up * dotp(n, up);
			if (length(h) > 1e-6)
			{
				const double slide = (std::min)(3.0, kick * kick / (2 * 0.6 * (std::max)(0.5, Gravity())));
				double lng, lat, rad; oapiGlobalToEqu(s.rbody, me + unit(h) * slide, &lng, &lat, &rad);
				s.surf_lng = lng; s.surf_lat = lat; DefSetStateEx(&s);
			}
		}
		Fall(kick, kick > 3 ? "Сбита машиной" : "Сбита с ног", Body::ON_BACK);
		oapiWriteLogV("OrbiterCrew: %s struck at %.2f m/s by %.0f kg, thrown at %.2f m/s", name.c_str(), vn, strikerMass, kick);
		return true;
	}

	bool CrewMember::Eject(const VECTOR3& vGlobal)
	{
		if (!inShip) return false;
		if (seat && seatId >= 0) if (ShipInterior* si = InteriorOf(inShip)) if (si->ext.Seated) si->ext.Seated(si->ctx, seatId, who.id, 0);
		seat = 0; seatId = -1; carried = 0; fwd = lat = turn = accel = 0;
		LeaveShip();
		OBJHANDLE ref = GetSurfaceRef();
		if (!ref) return true;
		VECTOR3 rpos, rv; GetRelativePos(ref, rpos); oapiGetGlobalVel(ref, &rv);
		MATRIX3 R; GetRotationMatrix(R);
		double hdg = 0; oapiGetHeading(GetHandle(), &hdg); jumpHeading = hdg;
		VESSELSTATUS2 s; memset(&s, 0, sizeof s); s.version = 2;
		s.rbody = ref; s.status = 0;
		s.rpos = rpos + unit(rpos) * 0.05;
		s.rvel = vGlobal - rv;
		s.arot = surface::Euler(R);
		DefSetStateEx(&s);
		airborne = true; thrown = true; lying = false;
		oapiWriteLogV("OrbiterCrew: %s thrown off at %.1f m/s", name.c_str(), length(s.rvel));
		return true;
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
		if (thrown)   // thrown off a machine: down at the speed she comes in with, on her back
		{
			thrown = false; airborne = false;
			VECTOR3 gv; GetGroundspeedVector(FRAME_HORIZON, gv);
			Fall(length(gv), "Выброшена с машины", Body::ON_BACK);
			return;
		}
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

	void CrewMember::TakePack(OBJHANDLE which)
	{
		// that jet pack (F at it) or the nearest within reach becomes part of her
		OBJHANDLE best = nullptr; double bestD = 2.5;
		if (which && oapiIsVessel(which)) { VECTOR3 d; GetRelativePos(which, d); if (length(d) < 3.0) { best = which; bestD = 0; } }
		if (!best)
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
		CtxStep(dt);
		if (!inShip && !inShipName.empty())   // a scenario with her inside a ship: find it
		{
			OBJHANDLE s = oapiGetVesselByName(const_cast<char*>(inShipName.c_str()));
			const VECTOR3 f = inFeet; const double h = inHdg;
			if (s) { EnterShip(s, f, _V(std::sin(h), 0, std::cos(h))); if (inShip) inShipName.clear(); }   // (the ship may register its interior later)
		}
		// the user's view of this person, remembered in the person (through the eyes / from outside at that distance)
		if (!takeView && oapiCameraTarget() == GetHandle()) { who.viewOutside = !oapiCameraInternal(); if (who.viewOutside) who.viewDist = oapiCameraTargetDist(); }
		if (takeView && !inShip) ApplyView();
		// the view on her at the start (through the eyes or from outside): 70 deg by default, once; the wheel changes it after
		if (!fovSet && oapiCameraTarget() == GetHandle()) { oapiCameraSetAperture(kViewAperture); fovSet = true; }
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
		if (inShip) { settling = false; InteriorStep(dt); if (takeView && inParent) ApplyView(); return; }
		if (settling) Settle(simt, dt);
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
		suit.vent = air.Breathable(); suit.ventPpO2 = air.ppO2; suit.ventPpCO2 = air.ppCO2; suit.pOut = air.p; suit.radRate = bio.doseRate;   // breathable air: no tank, no sorbent
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
			mouseTurnBy = HeadStep(dt, bio.CanAct() && !lying && !jet.Worn());   // outside: the constant look turns her (A/D step aside); the pack: the keys
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

	// the interface's language (the user, 2026-10-07: «2 язык - в конфиге будет настройка»; one for all of OrbiterCrew):
	// Config\OrbiterCrew\OrbiterCrew.cfg, LANGUAGE ru | en
	bool UiEnglish()
	{
		static int lang = -1;
		if (lang < 0)
		{
			lang = 0;
			std::ifstream f("Config\\OrbiterCrew\\OrbiterCrew.cfg");
			std::string line;
			while (std::getline(f, line))
			{
				std::istringstream ls(line); std::string k, v; ls >> k >> v;
				if (k == "LANGUAGE") lang = v.size() >= 2 && (v[0] == 'e' || v[0] == 'E') ? 1 : 0;
			}
		}
		return lang == 1;
	}
	// a caption in the interface's language
	const char* Tr(const char* ru, const char* en) { return UiEnglish() ? en : ru; }

	void CrewMember::HudBySuit()
	{
		// ---- the outside view never comes nearer than 2 m: near the ground D3D9Client cuts away everything within 1 m of
		// the camera (its near plane outside, whatever its settings), and her body reaches a metre from her centre (the
		// user, 2026-10-07: «как избавиться от обрезания камерой») ----
		if (oapiCameraTarget() == GetHandle())
		{
			if (inShip && !oapiCameraInternal()) { thirdIn = !thirdIn; oapiCameraAttach(GetHandle(), 0); }   // F1 in a cabin
			else if (!inShip && thirdIn) { thirdIn = false; oapiCameraAttach(GetHandle(), 1); }             // out: the outside view
			else if (!inShip && !oapiCameraInternal())
			{
				const double d = oapiCameraTargetDist();
				if (d > 0.05 && d < kCamMinDist) oapiCameraScaleDist(kCamMinDist / d);
			}
		}
		else thirdIn = false;
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
		// over x10 she is not drawn anew at all (the user, 2026-10-05: «их вообще после 10х не надо считать... смысл?»): no
		// motion, no arms, no hair, no mesh; at x10 and under it goes on from where it stood. (Her organism is not here: it runs on)
		int footfalls = 0;
		Figure& fig = Active();
		if (vis && fig.ok && fig.skin.Attached() && oapiGetTimeAcceleration() <= 10.0)
		{
			MotionInput in;
			in.dt = dt; in.fwd = fwd; in.lat = lat; in.turn = turn; in.accel = accel; in.g = g;
				in.seat = seat == 4 ? 2 : seat; in.seatT = seatT;
				in.heading = inShip ? std::remainder(inHdg, PI2) : 0;   // her turn from her vessel's axes (inside: the ship's)
			in.grounded = !airborne && landed && !lying;
			in.lying = lying && landed;
			in.landing = landingSpeed;
			in.effort = (std::min)(1.0, bio.Effort()); in.fatigue = bio.Fatigue(); in.breathRate = bio.breath;
			in.walkTop = walkSpeed * (suitOn ? 0.9 : 1.0);
			in.suited = suitOn; in.bound = boost ? std::clamp((std::hypot(fwd, lat) - 3.5) / 2.5, 0.0, 1.0) : 0.0;
			in.floating = !landed && !airborne;
			if (in.floating) GetAngularVel(in.angVel);
			if (in.floating) { VECTOR3 f; GetThrustVector(f); in.thrustAcc = f / GetMass(); GetAngularAcc(in.angAcc); }
			const bool firstPerson = oapiCameraInternal() && oapiCameraTarget() == GetHandle() && !thirdIn;
			// seated: the ship's cockpit view is her eyes in the seat - her own head must not be in it
			fig.skin.SetHideHead(firstPerson);
			// from inside the helmet its own parts sit at the camera: seen from within they show cut edges and their dark
			// inner faces. The body below the neck stays in view; the helmet, the visor, the shade and its arms do not
			for (const char* part : { "Helm", "Visor", "SunShade", "ShadeArms", "NeckSeal" }) fig.skin.SetPartHidden(part, firstPerson);
			UpdateHelmet(dt, fig);
			if (&fig == &suitFig) jet.Pose(fig.skin);
			in.jet = jet.Worn();
			// seated in a ship: where her hands rest or what they hold, as the ship gives it (OcInteriorExt::SeatHands), moved
			// into her vessel's frame (the ship's axes, its origin at her feet + the stand height). Getting up: her own arms
			if (inShip && seat == 2 && seatId >= 0)
			{
				ShipInterior* si = InteriorOf(inShip);
				if (si && si->ext.SeatHands)
				{
					OcHand h[2]{};
					h[0].size = h[1].size = sizeof(OcHand);
					si->ext.SeatHands(si->ctx, seatId, who.id, &h[0], &h[1]);
					const VECTOR3 o = inFeet + _V(0, height, 0);
					for (int i = 0; i < 2; ++i)
						if (h[i].on) { HandTarget& t = in.hand[i]; t.on = true; t.what = h[i].what; t.pos = h[i].pos - o; t.palm = h[i].palm; t.fwd = h[i].fwd; t.grip = h[i].grip; t.radius = h[i].radius; t.floor = h[i].elbowMinY != 0; t.elbowMinY = h[i].elbowMinY - o.y; }
				}
			}
			if (holding && !seat && bio.CanAct()) { in.hold = true; HeldTargets(in.hand); }   // her hands on what she holds
			motion.Update(in, fig.clips, fig.skin);
			if (holding) HeldPlace(fig);
			if (debugLog && seat == 2 && (handLogT -= oapiGetSysStep()) <= 0)   // (real time: not every frame under time acceleration)   // the seated hands: what the ship gives, how the arm reaches it
			{
				handLogT = 2;
				for (int i = 0; i < 2; ++i)
				{
					const SeatArms::Diag& d = motion.Arms().Last(i); const HandTarget& t = in.hand[i];
					oapiWriteLogV("OrbiterCrew: %s hand: target %s what %d (%.3f %.3f %.3f) grip %d r %.3f floor %.3f; arm %s short %.3f off %.3f elbow %.0f wrist %.0f",
						i ? "right" : "left", t.on ? "on" : "off", t.what, t.pos.x, t.pos.y, t.pos.z, t.grip, t.radius, t.elbowMinY,
						d.active ? "IK" : "own", d.shortM, d.offM, d.elbowDeg, d.wristDeg);
				}
			}
			footfalls = motion.Footfalls();
			if (firstPerson) {   // the camera rides the head; aboard a ship the head rides the ship's accelerations
				const VECTOR3 e = fig.skin.Point(fig.skin.Bone("Head"), eye);
				camEye = e + HeadSwayStep(dt, e);
				SetCameraOffset(camEye);
			} else { headSway.Reset(); if (thirdIn) camEye = fig.skin.Point(fig.skin.Bone("Head"), eye); }   // (from behind: AimHead)
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
		CtxDraw();
	}

	// her eyes are not a ship's glass cockpit: Orbiter's navigation-mode and RCS buttons are not shown over them
	bool CrewMember::clbkLoadGenericCockpit() { return false; }   // her eyes are the empty VC, in the suit and out of it

	bool CrewMember::clbkDrawHUD(int, const HUDPAINTSPEC* hps, oapi::Sketchpad* skp)
	{
		if (hps && skp) DrawSuitHud(skp, hps->W, hps->H);   // (the caption: through the display's overlay)
		return true;
	}

	// the caption on the helmet display: laid out in the screen's pixels each step (CtxDraw), painted by the display's own
	// overlay into its surface (the view vw x vh; kx, ky its pixels per view pixel); the Alt marks projected through the
	// camera as it is now, so they sit on their places
	void CrewMember::CtxPaintPlate(oapi::Sketchpad* skp, double vw, double vh, double kx, double ky)
	{
		if (!skp || !plateOn || vw < 1 || vh < 1 || !suitOn || !hudOn || !oapiCameraInternal() || oapiCameraTarget() != GetHandle()) return;
		if (!cxTextTried) { cxTextTried = true; cxText.Load(); }
		auto rect = [&](double x0, double y0, double x1, double y1, DWORD col)
		{
			oapi::Brush* br = oapiCreateBrush(col);
			skp->SetPen(nullptr); skp->SetBrush(br);
			skp->Rectangle(static_cast<int>(std::lround(x0 * kx)), static_cast<int>(std::lround(y0 * ky)), static_cast<int>(std::lround(x1 * kx)), static_cast<int>(std::lround(y1 * ky)));
			skp->SetBrush(nullptr); oapiReleaseBrush(br);
		};
		for (const CtxBox& b : plateBoxes) rect(b.x0, b.y0, b.x1, b.y1, b.col);
		if (plateMarks)
		{
			VECTOR3 cg; oapiCameraGlobalPos(&cg); MATRIX3 Rc; oapiCameraRotationMatrix(&Rc);
			const double f = vh / (2 * std::tan((std::max)(0.1, oapiCameraAperture())));
			for (const auto& m : cx.marks)
			{
				if (!oapiIsVessel(m.ship)) continue;
				VECTOR3 g; oapiGetVesselInterface(m.ship)->Local2Global(m.pos, g);
				const VECTOR3 c = tmul(Rc, g - cg);
				if (c.z < 0.1) continue;
				const double sx = std::round(vw / 2.0 + c.x / c.z * f), sy = std::round(vh / 2.0 - c.y / c.z * f);
				const double h = m.reach ? 7 : 4, th = m.reach ? 2 : 1;
				const DWORD o = 0xFF2699FF;   // orange
				rect(sx - h, sy - h, sx + h, sy - h + th, o); rect(sx - h, sy + h - th, sx + h, sy + h, o);
				rect(sx - h, sy - h + th, sx - h + th, sy + h - th, o); rect(sx + h - th, sy - h + th, sx + h, sy + h - th, o);
			}
		}
		if (!cxText.Ok()) return;
		const oapi::FVECTOR4 black(0.0f, 0.0f, 0.0f, 1.0f);
		for (const CtxRun& r : plateRuns)
		{
			if (r.black) skp->SetBrightness(&black);
			cxText.Draw(skp, r.x * kx, r.base * ky, r.u, r.size, r.col, r.align, 2.0, kx, ky);
			if (r.black) skp->SetBrightness(nullptr);
		}
	}


	// every frame for every body: only the one whose eyes we look through, in the suit, with the HUD on, draws it
	void CrewMember::DrawSuitHud(oapi::Sketchpad* skp, DWORD W, DWORD H)
	{
		// the person and the suit are apart (the user's rule): the suit computer's HUD exists only in the suit
		if (!suitOn || !hudOn || !oapiCameraInternal() || oapiCameraTarget() != GetHandle() || thirdIn) return;
		if (!W || !H) return;
		HudData d;
		FillHudData(d);
		hud.Mouse(W, H);                    // the clicks on the HUD (the suit computer's own, «Архитектор»)
		hud.Draw(skp, W, H, d, this);
	}

	// what the suit computer shows: the person, the suit, the pack, the world around (for its HUD in the VC HUD and in its
	// own light layer, SuitHud::HelmetFrame)
	void CrewMember::FillHudData(HudData& d)
	{
		static const std::pair<const char*, const char*> WARN[] = {
			{ "VACUUM - NO SUIT", "ВАКУУМ БЕЗ СКАФАНДРА" }, { "OVERHEATING", "ПЕРЕГРЕВ" }, { "HYPOTHERMIA", "ПЕРЕОХЛАЖДЕНИЕ" },
			{ "HYPOXIA", "ГИПОКСИЯ" }, { "LOW OXYGEN", "МАЛО КИСЛОРОДА" }, { "CO2 NARCOSIS", "ОТРАВЛЕНИЕ CO2" },
			{ "HIGH CO2", "ВЫСОКИЙ CO2" }, { "EXHAUSTED", "ИСТОЩЕНИЕ" }, { "INJURED", "ТРАВМА" },
			{ "RADIATION - TAKE COVER", "РАДИАЦИЯ - В УКРЫТИЕ" }, { "RADIATION SICKNESS", "ЛУЧЕВАЯ БОЛЕЗНЬ" },
			{ "DEHYDRATED", "ОБЕЗВОЖИВАНИЕ" }, { "STARVING", "ГОЛОД" } };
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
		d.firstPerson = oapiCameraInternal() && oapiCameraTarget() == GetHandle() && !thirdIn;
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
	}
}

namespace ocrew
{
	// ---- a thing lying about (OrbiterCrew\Item): what she put down or threw; F takes it again ----
	namespace
	{
		std::vector<ItemVessel*>& itemsRef() { static std::vector<ItemVessel*> v; return v; }
		OcHeld& PendingItem() { static OcHeld h{}; return h; }
	}
	ItemVessel* ItemOf(OBJHANDLE h) { for (ItemVessel* it : itemsRef()) if (it->GetHandle() == h) return it; return nullptr; }

	ItemVessel::ItemVessel(OBJHANDLE h, int fm) : VESSEL4(h, fm)
	{
		itemsRef().push_back(this);
		if (PendingItem().size) { held = PendingItem(); PendingItem() = OcHeld{}; }
	}
	ItemVessel::~ItemVessel()
	{
		itemsRef().erase(std::remove(itemsRef().begin(), itemsRef().end(), this), itemsRef().end());
		if (tpl && !held.mesh[0]) oapiDeleteMesh(tpl);
	}
	void ItemVessel::clbkSetClassCaps(FILEHANDLE)
	{
		SetEmptyMass(1); SetSize(0.4); SetCW(0.5, 0.5, 0.5, 0.5); SetCrossSections(_V(0.1, 0.1, 0.1)); SetPMI(_V(0.02, 0.02, 0.02));
		SetEnableFocus(false);
		EnableTransponder(false);
		Shape();
	}
	void ItemVessel::clbkLoadStateEx(FILEHANDLE scn, void* status)
	{
		char* line;
		while (oapiReadScenario_nextline(scn, line))
		{
			std::istringstream ss(line); std::string key; ss >> key;
			if (key == "ITEM")   // kind mass w h d mesh|- label...
			{
				OcHeld h{}; h.size = sizeof h; std::string mesh, label;
				ss >> h.kind >> h.massKg >> h.dims[0] >> h.dims[1] >> h.dims[2] >> mesh; std::getline(ss >> std::ws, label);
				if (mesh != "-") std::snprintf(h.mesh, sizeof h.mesh, "%s", mesh.c_str());
				std::snprintf(h.label, sizeof h.label, "%s", label.c_str());
				std::snprintf(h.owner, sizeof h.owner, "%s", held.owner); std::snprintf(h.data, sizeof h.data, "%s", held.data);
				held = h;
			}
			else if (key == "ITEM_OWNER") { std::string v; std::getline(ss >> std::ws, v); std::snprintf(held.owner, sizeof held.owner, "%s", v.c_str()); }
			else if (key == "ITEM_DATA") { std::string v; std::getline(ss >> std::ws, v); std::snprintf(held.data, sizeof held.data, "%s", v.c_str()); }
			else ParseScenarioLineEx(line, status);
		}
		Shape();
	}
	void ItemVessel::clbkSaveState(FILEHANDLE scn)
	{
		VESSEL4::clbkSaveState(scn);
		char b[220]; std::snprintf(b, sizeof b, "%d %.3f %.3f %.3f %.3f %s %s", held.kind, held.massKg, held.dims[0], held.dims[1], held.dims[2], held.mesh[0] ? held.mesh : "-", held.label);
		oapiWriteScenario_string(scn, const_cast<char*>("ITEM"), b);
		if (held.owner[0]) oapiWriteScenario_string(scn, const_cast<char*>("ITEM_OWNER"), held.owner);
		if (held.data[0]) oapiWriteScenario_string(scn, const_cast<char*>("ITEM_DATA"), held.data);
	}
	void ItemVessel::clbkPostCreation() { Shape(); }
	// its mass, its feet (the box's lower corners and its top: it tumbles), its look (its mesh, or a dark box of its size)
	void ItemVessel::Shape()
	{
		if (!held.size) return;
		const double hx = (std::max)(0.02, held.dims[0] / 2), hy = (std::max)(0.02, held.dims[1] / 2), hz = (std::max)(0.02, held.dims[2] / 2), m = (std::max)(0.2, held.massKg);
		SetEmptyMass(m); SetSize((std::max)(0.2, std::sqrt(hx * hx + hy * hy + hz * hz)));
		SetPMI(_V((hy * hy + hz * hz) / 3, (hx * hx + hz * hz) / 3, (hx * hx + hy * hy) / 3));
		const double k = (std::max)(2000.0, m * 900), dmp = 2 * std::sqrt(k * m);
		static TOUCHDOWNVTX td[8];
		const VECTOR3 p[8] = { { -hx, -hy, -hz }, { hx, -hy, -hz }, { hx, -hy, hz }, { -hx, -hy, hz }, { -hx, hy, -hz }, { hx, hy, -hz }, { hx, hy, hz }, { -hx, hy, hz } };
		for (int i = 0; i < 8; ++i) td[i] = { p[i], k, dmp, 1.2, 1.2 };
		SetTouchdownPoints(td, 8);
		if (mesh == static_cast<UINT>(-1))
		{
			if (held.mesh[0]) tpl = oapiLoadMeshGlobal(held.mesh);
			if (!tpl)
			{
				static const int F[6][4] = { { 1, 5, 7, 3 }, { 4, 0, 2, 6 }, { 2, 3, 7, 6 }, { 4, 5, 1, 0 }, { 5, 4, 6, 7 }, { 0, 1, 3, 2 } };
				static const double Nn[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
				std::vector<NTVERTEX> v(24); std::vector<WORD> idx;
				for (int f = 0; f < 6; ++f)
				{
					for (int c4 = 0; c4 < 4; ++c4)
					{
						const int c = F[f][c4]; NTVERTEX& q = v[f * 4 + c4];
						q.x = static_cast<float>((c & 1) ? hx : -hx); q.y = static_cast<float>((c & 2) ? hy : -hy); q.z = static_cast<float>((c & 4) ? hz : -hz);
						q.nx = static_cast<float>(Nn[f][0]); q.ny = static_cast<float>(Nn[f][1]); q.nz = static_cast<float>(Nn[f][2]);
					}
					const WORD b0 = static_cast<WORD>(f * 4);
					for (WORD t : { WORD(0), WORD(2), WORD(1), WORD(0), WORD(3), WORD(2) }) idx.push_back(static_cast<WORD>(b0 + t));
				}
				MESHGROUP g{}; g.Vtx = v.data(); g.nVtx = 24; g.Idx = idx.data(); g.nIdx = static_cast<DWORD>(idx.size()); g.TexIdx = static_cast<DWORD>(-1);
				tpl = oapiCreateMesh(1, &g);
				MATERIAL mt{}; mt.diffuse = { 0.22f, 0.23f, 0.24f, 1 }; mt.ambient = mt.diffuse; mt.specular = { 0.3f, 0.3f, 0.3f, 1 }; mt.power = 20; mt.emissive = { 0, 0, 0, 1 };
				oapiAddMaterial(tpl, &mt);
			}
			mesh = AddMesh(tpl);
			SetMeshVisibilityMode(mesh, MESHVIS_ALWAYS | MESHVIS_EXTPASS);
		}
	}

	// X: what she holds is put down before her on the ground; Shift+X: thrown ahead (her speed + 3 m/s ahead, a little up)
	void CrewMember::DropHeld(bool throwIt)
	{
		if (!holding) return;
		// a plug is on its cable: let go, it goes back with the cable (its owner winds it in); it is neither laid nor thrown
		if (held.kind == OC_HELD_PLUG) { Say("Отпущено: " + Utf8(held.label)); Take(nullptr); return; }
		if (inShip) { Say("В корабле положить пока нельзя"); return; }
		VESSELSTATUS2 s = Status(this);
		MATRIX3 R; GetRotationMatrix(R);
		const VECTOR3 fw = mul(R, _V(0, 0, 1)), up = mul(R, _V(0, 1, 0));
		if (!throwIt && s.status == 1 && s.rbody)
		{
			surface::Walk(s, oapiGetSize(s.rbody), 0.6, 0);   // landed, 0.6 m before her: Orbiter sets it on its feet
		}
		else
		{
			if (s.status == 1 && s.rbody)   // from standing: a free start where her hands are
			{
				VECTOR3 me, pc; GetGlobalPos(me); oapiGetGlobalPos(s.rbody, &pc);
				VECTOR3 vr; GetRelativeVel(s.rbody, vr);
				s.status = 0; s.rpos = me - pc + fw * 0.45 + up * 0.15; s.rvel = vr;
				s.arot = surface::Euler(R);
			}
			else s.rpos = s.rpos + fw * 0.45;
			if (throwIt) s.rvel = s.rvel + fw * 3.0 + up * 1.2;
		}
		s.flag = 0; s.fuel = nullptr; s.thruster = nullptr; s.dockinfo = nullptr; s.nfuel = s.nthruster = s.ndockinfo = 0;
		char nm[64];
		for (int i = 1; ; ++i) { std::snprintf(nm, sizeof nm, "Item%d", i); if (!oapiGetVesselByName(nm)) break; }
		PendingItem() = held; PendingItem().size = sizeof(OcHeld);
		OBJHANDLE h = oapiCreateVesselEx(nm, "OrbiterCrew\\Item", &s);
		PendingItem() = OcHeld{};
		if (!h) { Say("Не вышло положить", 1); return; }
		Say(std::string(throwIt ? "Брошено: " : "Положено: ") + Utf8(held.label));
		Take(nullptr);
	}

	// ---- a thing in her hands ----
	double CrewMember::HeldWeight() const
	{
		double g = 9.81;
		if (inShip) { if (ShipInterior* si = InteriorOf(inShip)) if (si->fns.Gravity) { VECTOR3 gv{}; VECTOR3 f = inFeet; si->fns.Gravity(si->ctx, &f, &gv); g = length(gv); } }
		else g = Gravity();
		return held.massKg * g / 9.81;
	}

	double CrewMember::LiftLimit() const
	{
		double k = 1.0;
		if (suitOn) k = suit.Drives() ? 2.0 : 0.85;   // the servos lift with her; a dead suit's stiff joints take from it
		return bio.LiftCapacity() * k;
	}

	double CrewMember::HeldSlow() const
	{
		const double r = HeldWeight() / (std::max)(1.0, CarryLimit());   // its weight against what she carries on the move
		return std::clamp(1.15 - 0.45 * r, 0.35, 1.0);
	}

	bool CrewMember::Give(const OcHeld& h0, bool check)
	{
		OcHeld h = h0; h.size = sizeof h;
		if (check)
		{
			if (holding) { Say("Руки заняты: " + std::string(held.label)); return false; }
			if (!bio.CanAct()) return false;
			if (seat) { Say("В кресле не взять"); return false; }
			const OcHeld keep = held; held = h;
			const double w = HeldWeight(), lim = LiftLimit();
			held = keep;
			if (w > lim)
			{
				char b[128]; std::snprintf(b, sizeof b, "Слишком тяжело: %.0f кг здесь, могу поднять %.0f", w, lim);
				Say(b, 1); return false;
			}
		}
		held = h; holding = true;
		SetEmptyMass(Mass());
		HeldMeshMake();
		if (check) Say(std::string("В руках: ") + h.label);
		oapiWriteLogV("OrbiterCrew: %s takes %s (%.1f kg)", name.c_str(), h.label, h.massKg);
		return true;
	}

	bool CrewMember::Take(OcHeld* out)
	{
		if (!holding) return false;
		if (out) *out = held;
		holding = false;
		HeldMeshDrop();
		SetEmptyMass(Mass());
		oapiWriteLogV("OrbiterCrew: %s gives away %s", name.c_str(), held.label);
		held = OcHeld{};
		return true;
	}

	void CrewMember::HeldMeshMake()
	{
		HeldMeshDrop();
		if (held.mesh[0]) heldTpl = oapiLoadMeshGlobal(held.mesh);
		if (!heldTpl)   // a box of its size: dark, a lighter band (a cell's handle side)
		{
			const double hx = (std::max)(0.01, held.dims[0] / 2), hy = (std::max)(0.01, held.dims[1] / 2), hz = (std::max)(0.01, held.dims[2] / 2);
			static const int F[6][4] = { { 1, 5, 7, 3 }, { 4, 0, 2, 6 }, { 2, 3, 7, 6 }, { 4, 5, 1, 0 }, { 5, 4, 6, 7 }, { 0, 1, 3, 2 } };   // +x -x +y -y +z -z
			static const double Nn[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
			std::vector<NTVERTEX> v(24); std::vector<WORD> idx;
			for (int f = 0; f < 6; ++f)
			{
				for (int k = 0; k < 4; ++k)
				{
					const int c = F[f][k];
					NTVERTEX& p = v[f * 4 + k];
					p.x = static_cast<float>((c & 1) ? hx : -hx); p.y = static_cast<float>((c & 2) ? hy : -hy); p.z = static_cast<float>((c & 4) ? hz : -hz);
					p.nx = static_cast<float>(Nn[f][0]); p.ny = static_cast<float>(Nn[f][1]); p.nz = static_cast<float>(Nn[f][2]);
					p.tu = static_cast<float>(k == 1 || k == 2); p.tv = static_cast<float>(k >= 2);
				}
				const WORD b = static_cast<WORD>(f * 4);
				for (WORD t : { WORD(0), WORD(2), WORD(1), WORD(0), WORD(3), WORD(2) }) idx.push_back(static_cast<WORD>(b + t));   // (the left-handed frame's winding)
			}
			heldVtx.assign(1, v);   // its true shape; the template is blown up 25 times: the client's culling box is made from it once
			for (NTVERTEX& p : v) { p.x *= 25; p.y *= 25; p.z *= 25; }
			MESHGROUP g{}; g.Vtx = v.data(); g.nVtx = 24; g.Idx = idx.data(); g.nIdx = static_cast<DWORD>(idx.size()); g.MtrlIdx = 0; g.TexIdx = static_cast<DWORD>(-1);   // (no texture)
			heldTpl = oapiCreateMesh(1, &g);
			MATERIAL m{}; m.diffuse = { 0.22f, 0.23f, 0.24f, 1 }; m.ambient = m.diffuse; m.specular = { 0.3f, 0.3f, 0.3f, 1 }; m.power = 20; m.emissive = { 0, 0, 0, 1 };
			oapiAddMaterial(heldTpl, &m);
		}
		if (held.mesh[0]) heldVtx.clear();
		if (held.mesh[0]) for (DWORD gi = 0, n = oapiMeshGroupCount(heldTpl); gi < n; ++gi)
		{
			const MESHGROUP* g = oapiMeshGroup(heldTpl, gi);
			heldVtx.emplace_back(g->Vtx, g->Vtx + g->nVtx);
		}
		heldMesh = AddMesh(heldTpl);
		SetMeshVisibilityMode(heldMesh, MESHVIS_ALWAYS | MESHVIS_VC | MESHVIS_EXTPASS);   // one depth with the ship around her
	}

	void CrewMember::HeldMeshDrop()
	{
		if (heldMesh != static_cast<UINT>(-1)) { DelMesh(heldMesh); heldMesh = static_cast<UINT>(-1); }
		if (heldTpl && !held.mesh[0]) oapiDeleteMesh(heldTpl);
		heldTpl = nullptr; heldVtx.clear();
	}

	// her hands on it (model frame, the vessel's origin; inside a ship her heading is in the pose): a cell in both hands
	// before her belly, the palms on its sides; a plug in her right hand
	void CrewMember::HeldTargets(HandTarget hand[2]) const
	{
		const double h = inShip ? inHdg : 0.0;
		const VECTOR3 rt = _V(std::cos(h), 0, -std::sin(h)), fw = _V(std::sin(h), 0, std::cos(h));
		for (int s = 0; s < 2; ++s) hand[s] = HandTarget{};
		if (held.kind == OC_HELD_PLUG)
		{
			HandTarget& t = hand[1]; t.on = true; t.what = 31;
			t.pos = rt * 0.17 + fw * 0.30 + _V(0, 0.02, 0); t.palm = -rt; t.fwd = fw; t.grip = OC_GRIP_HANDLE; t.radius = 0.02;
			return;
		}
		const double w = (std::max)(0.08, held.dims[0]), d = (std::max)(0.05, held.dims[2]);
		const VECTOR3 c = fw * (0.16 + d / 2) + _V(0, 0.05, 0);
		hand[0].on = hand[1].on = true; hand[0].what = 30; hand[1].what = 31;
		hand[0].pos = c - rt * (w / 2); hand[0].palm = rt; hand[0].fwd = fw; hand[0].grip = OC_GRIP_FLAT;
		hand[1].pos = c + rt * (w / 2); hand[1].palm = -rt; hand[1].fwd = fw; hand[1].grip = OC_GRIP_FLAT;
	}

	// the thing between her palms (or in her right one), turned with her: its vertices moved every frame
	void CrewMember::HeldPlace(Figure& fig)
	{
		if (!vis || heldMesh == static_cast<UINT>(-1)) return;
		DEVMESHHANDLE dev = GetDevMesh(vis, heldMesh);
		if (!dev) return;
		const double h = inShip ? inHdg : 0.0;
		VECTOR3 X = _V(std::cos(h), 0, -std::sin(h)), Y = _V(0, 1, 0);
		VECTOR3 C;
		const int bL = fig.skin.Bone("LeftHand"), bR = fig.skin.Bone("RightHand");
		const Skin::HandRest& hl = fig.skin.Hand(0); const Skin::HandRest& hr = fig.skin.Hand(1);
		const bool posed = !seat && bL >= 0 && bR >= 0 && hl.ok && hr.ok;
		if (held.kind == OC_HELD_PLUG)
		{
			if (posed) { const VECTOR3 p = fig.skin.Point(bR, hr.palm), n = fig.skin.Point(bR, hr.palm + hr.normal) - p; C = p + n * 0.02; }
			else C = X * 0.17 + _V(std::sin(h), 0, std::cos(h)) * 0.30;
		}
		else if (posed)
		{
			const VECTOR3 pl = fig.skin.Point(bL, hl.palm), pr = fig.skin.Point(bR, hr.palm);
			C = (pl + pr) * 0.5;
			VECTOR3 x = pr - pl; x.y = 0;
			if (length(x) > 0.02) X = x / length(x);
		}
		else { HandTarget t[2]; HeldTargets(t); C = (t[0].pos + t[1].pos) * 0.5; }
		const VECTOR3 Z = crossp(X, Y);
		for (size_t gi = 0; gi < heldVtx.size(); ++gi)
		{
			std::vector<NTVERTEX> v = heldVtx[gi];
			for (NTVERTEX& p : v)
			{
				const VECTOR3 q = C + X * p.x + Y * p.y + Z * p.z, n = X * p.nx + Y * p.ny + Z * p.nz;
				p.x = static_cast<float>(q.x); p.y = static_cast<float>(q.y); p.z = static_cast<float>(q.z);
				p.nx = static_cast<float>(n.x); p.ny = static_cast<float>(n.y); p.nz = static_cast<float>(n.z);
			}
			GROUPEDITSPEC ges{}; ges.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML; ges.Vtx = v.data(); ges.nVtx = static_cast<DWORD>(v.size());
			oapiEditMeshGroup(dev, static_cast<DWORD>(gi), &ges);
		}
	}

	// ---- context actions (CONTEXT_ACTIONS.md) ----
	ShipInterior* CrewMember::CtxInterior() const { return cx.ship ? InteriorOf(cx.ship) : nullptr; }

	bool CrewMember::CtxRay(VECTOR3& o, VECTOR3& d) const
	{
		if (cx.cursor)   // M-1: through the cursor, as a click
		{
			POINT p; if (!GetCursorPos(&p)) return false;
			gcCore2* core = gcGetCoreInterface(); const HWND view = core ? core->GetRenderWindow() : nullptr;
			RECT rc{};
			if (!view || !ScreenToClient(view, &p) || !GetClientRect(view, &rc) || rc.right <= 0 || rc.bottom <= 0) return false;
			const double W = rc.right, H = rc.bottom, fpx = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
			MATRIX3 Rc; oapiCameraGlobalPos(&o); oapiCameraRotationMatrix(&Rc);
			d = mul(Rc, unit(_V((p.x - W / 2) / fpx, (H / 2 - p.y) / fpx, 1.0)));
			return true;
		}
		if (oapiCameraInternal() && oapiCameraTarget() == GetHandle()) { oapiCameraGlobalPos(&o); oapiCameraGlobalDir(&d); return true; }
		// from outside: her own look, from her eyes
		const double yaw = inShip ? inHdg + headYaw : headYaw, p = headPitch;
		MATRIX3 R; GetRotationMatrix(R);
		Local2Global(camEye, o);
		d = mul(R, _V(std::sin(yaw) * std::cos(p), std::sin(p), std::cos(yaw) * std::cos(p)));
		return true;
	}

	void CrewMember::CtxStep(double dt)
	{
		const bool mine = oapiGetFocusObject() == GetHandle();
		cx.cursor = mine && (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
		// a long action: its time, or broken off (F again, stepping away, not able)
		if (cx.busy)
		{
			cx.t += dt;
			VECTOR3 f = inFeet;
			if (!inShip) { double lng, la, rad; GetEquPos(lng, la, rad); f = _V(lng * rad * std::cos(la), la * rad, 0); }   // on the ground: its own turning frame (m)
			if (!bio.CanAct() || length(f - cx.feet0) > 0.6) CtxBreak(false);
			else if (cx.t >= cx.dur) CtxBreak(true);
		}
		// the seat's display: while she sits, 10 times a second
		const bool atPost = inShip && seat == 0 && carried > 0;   // a standing post (the MPU's): seatId -1
		if (mine && (seat == 2 || atPost) && inShip && (seatGaugeT -= dt) <= 0)
		{
			seatGaugeT = 0.1;
			seatGauges.clear();
			if (ShipInterior* sg = InteriorOf(inShip))
				if (sg->ext.SeatGauges)
				{
					OcGauge buf[12]{}; for (OcGauge& g : buf) g.size = sizeof g;
					const int n = (std::min)(12, sg->ext.SeatGauges(sg->ctx, atPost ? -1 : seatId, who.id, buf, 12));
					if (n > 0) seatGauges.assign(buf, buf + n);
				}
		}
		else if ((seat != 2 && !atPost) || !inShip || !mine) seatGauges.clear();
		if (!mine || !bio.CanAct() || seat || airborne || lying) { cx.ship = nullptr; cx.node = -1; cx.acts.clear(); cx.marks.clear(); g_wheelEat = false; farNode = false; return; }
		if (!g_hookWnd) HookWheel();
		// which node: every 0.1 s, the one nearest the ray within reach
		if ((cx.scanT -= dt) <= 0 && !cx.busy)
		{
			cx.scanT = 0.1;
			cx.marks.clear();
			VECTOR3 o, d; OBJHANDLE bestShip{}; OcNode best{}; double bestOff = 1.0; VECTOR3 bestG{};   // the score: the ray's miss against what is taken (1 = the edge)
			// taken: the ray passing within 22 cm of the node (a thing has a size: close by, its point may be 20 deg off
			// the look) or within 12 deg far off; with the cursor 10 cm / 5 deg
			auto score = [&](double miss, double t) { const double tol = cx.cursor ? (std::max)(0.10, 0.08 * t) : (std::max)(0.22, 0.21 * t); return miss / tol; };
			VECTOR3 eye; Local2Global(camEye, eye);
			int dbgShips = 0, dbgNodes = 0; double dbgOff = 1e9, dbgDist = 0, dbgReach = 0; std::string dbgName;   // (DebugLog: why no node)
			const bool rayOk = CtxRay(o, d);
			if (rayOk)
				for (ShipInterior& si : interiors())
				{
					if (!oapiIsVessel(si.ship) || !si.ext.NodeCount || !si.ext.Node) continue;
					if (inShip && si.ship != inShip) continue;
					++dbgShips;
					VESSEL* sv = oapiGetVesselInterface(si.ship);
					VECTOR3 sp; oapiGetGlobalPos(si.ship, &sp);
					if (!inShip && length(sp - eye) > sv->GetSize() + 30) continue;
					for (int i = 0, n = si.ext.NodeCount(si.ctx); i < n; ++i)
					{
						OcNode nd{}; nd.size = sizeof nd;
						if (!si.ext.Node(si.ctx, i, &nd) || (nd.inside != 0) != (inShip != nullptr)) continue;
						VECTOR3 g; sv->Local2Global(nd.inside ? nd.pos + si.Origin() : nd.pos, g);
						const VECTOR3 v = g - o; const double t = dotp(v, d);
						const double off = t > 0.05 ? length(v - d * t) / t : 1e9;
						++dbgNodes;
						if (off < dbgOff) { dbgOff = off; dbgDist = length(g - eye); dbgReach = nd.reach > 0 ? nd.reach : 1.2; dbgName = nd.label; }
						if (cx.cursor && t > 0.05 && length(g - eye) < 5.0)
							cx.marks.push_back({ si.ship, nd.inside ? nd.pos + si.Origin() : nd.pos, length(g - eye) <= (nd.reach > 0 ? nd.reach : 1.2) });
						// out of her reach; the one she has is kept 15 cm further (her eye sways with her breath: on the edge of
						// the reach the caption came and went, the user 2026-10-07 «мерцает»)
						const bool cur = si.ship == cx.ship && nd.id == cx.node;
						if (length(g - eye) > (nd.reach > 0 ? nd.reach : 1.2) + (cur ? 0.15 : 0.0)) continue;
						if (t <= 0.05) continue;
						const double sc = score(length(v - d * t), t);
						const double offK = cur ? sc * 0.5 : sc;   // the one she has: kept to twice the limit
						if (offK < bestOff) { bestOff = offK; best = nd; bestShip = si.ship; bestG = g; }
					}
				}
			// ours: a thing lying about (F: take it), a jet pack (F: put it on) - outside, within 2 m of her eyes
			int bestLocal = 0;
			if (rayOk && !inShip)
				for (DWORD i = 0, nv = oapiGetVesselCount(); i < nv; ++i)
				{
					OBJHANDLE h = oapiGetVesselByIndex(i);
					if (h == GetHandle()) continue;
					ItemVessel* item = ItemOf(h);
					const bool pack = !item && std::strcmp(oapiGetVesselInterface(h)->GetClassNameA(), "OrbiterCrew\\JetPack") == 0;
					if (!item && !pack) continue;
					VECTOR3 g; oapiGetGlobalPos(h, &g);
					if (cx.cursor && length(g - eye) < 5.0 && dotp(g - o, d) > 0.05) cx.marks.push_back({ h, _V(0, 0, 0), length(g - eye) <= 2.0 });
					if (length(g - eye) > (h == cx.ship && cx.local ? 2.15 : 2.0)) continue;
					const VECTOR3 v = g - o; const double t = dotp(v, d);
					if (t <= 0.05) continue;
					const double sc = score(length(v - d * t), t), offK = h == cx.ship && cx.local ? sc * 0.5 : sc;
					if (offK < bestOff)
					{
						bestOff = offK; bestShip = h; bestG = g; bestLocal = item ? 1 : 2;
						best = OcNode{}; best.size = sizeof best; best.id = 0;
						std::snprintf(best.label, sizeof best.label, "%s", item ? item->held.label : Tr("\xD0\xE0\xED\xE5\xF6", "Jet pack"));   // (cp1251: «Ранец»)
					}
				}
			farNode = !bestShip && dbgOff < 0.21 && dbgDist < 8.0 && dbgDist > dbgReach;   // looked at (12 deg), but out of reach
			if (debugLog && (cxLogT -= 0.1) <= 0)
			{
				cxLogT = 2.0;
				oapiWriteLogV("OrbiterCrew: ctx: ray %d%s, %d ships with nodes, %d nodes; nearest to the ray '%s' off %.3f (limit %.3f) at %.2f m (reach %.2f) -> %s",
					rayOk ? 1 : 0, cx.cursor ? " (cursor)" : "", dbgShips, dbgNodes, dbgName.c_str(), dbgOff, cx.cursor ? 0.06 : 0.12, dbgDist, dbgReach, bestShip ? best.label : "none");
			}
			if (bestShip != cx.ship || (bestShip && best.id != cx.node) || bestLocal != cx.local) { cx.ship = bestShip; cx.node = bestShip ? best.id : -1; cx.nd = best; cx.acts.clear(); cx.refreshT = 0; cx.local = bestLocal; }
			cx.at = bestG; if (bestShip) cx.nd = best;
			if (cx.local)   // our own action: take it / put it on (in the ships' code page, as theirs)
			{
				OcAction a{}; a.size = sizeof a; a.id = 1; a.available = 1;
				if (cx.local == 1) std::snprintf(a.label, sizeof a.label, "%s", Tr("\xC2\xE7\xFF\xF2\xFC", "Pick up"));   // Взять
				else
				{
					std::snprintf(a.label, sizeof a.label, "%s", Tr("\xCD\xE0\xE4\xE5\xF2\xFC", "Put on"));   // Надеть
					if (!suitOn) { a.available = 0; std::snprintf(a.reason, sizeof a.reason, "%s", Tr("\xF1\xED\xE0\xF7\xE0\xEB\xE0 \xF1\xEA\xE0\xF4\xE0\xED\xE4\xF0 (K)", "suit first (K)")); }   // сначала скафандр (K)
					else if (jet.Worn()) { a.available = 0; std::snprintf(a.reason, sizeof a.reason, "%s", Tr("\xF0\xE0\xED\xE5\xF6 \xF3\xE6\xE5 \xED\xE0\xE4\xE5\xF2", "pack already on")); }   // ранец уже надет
				}
				cx.acts.assign(1, a); cx.sel = 0;
			}
		}
		// its actions: when it is new, twice a second, and the picked one kept by its id
		ShipInterior* si = cx.local ? nullptr : CtxInterior();
		if (si && cx.node >= 0 && si->ext.Actions && (cx.refreshT -= dt) <= 0)
		{
			cx.refreshT = 0.5;
			OcAction buf[8]{}; for (OcAction& a : buf) a.size = sizeof a;
			const int n = (std::min)(8, si->ext.Actions(si->ctx, cx.node, who.id, buf, 8));
			const int keep = cx.sel >= 0 && cx.sel < static_cast<int>(cx.acts.size()) ? cx.acts[cx.sel].id : -1;
			cx.acts.assign(buf, buf + (std::max)(0, n));
			cx.sel = 0;
			for (int k = 0; k < static_cast<int>(cx.acts.size()); ++k) if (cx.acts[k].id == keep) cx.sel = k;
			if (keep < 0) for (int k = 0; k < static_cast<int>(cx.acts.size()); ++k) if (cx.acts[k].available) { cx.sel = k; break; }
		}
		if ((!si && !cx.local) || cx.node < 0) cx.acts.clear();
		// the wheel: hers only while two or more are listed
		const int n = static_cast<int>(cx.acts.size());
		g_wheelEat = n >= 2 && !cx.busy;
		const int w = g_wheel.exchange(0);
		if (n >= 2 && w != 0) cx.sel = ((cx.sel + (w < 0 ? 1 : -1) * (std::max)(1, std::abs(w) / WHEEL_DELTA)) % n + n) % n;
		// M-1: a click on a row of the panel picks it and does it; on the node: does the picked one
		const bool lmb = cx.cursor && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		if (lmb && !cx.lmbWas && n > 0) CtxUse();
		cx.lmbWas = lmb;
	}

	bool CrewMember::CtxUse()
	{
		if (cx.busy) { if (cx.interruptible) CtxBreak(false); return true; }
		if (cx.local && cx.ship && oapiIsVessel(cx.ship) && !seat)
		{
			if (cx.local == 2) { if (!suitOn) Say("Ранец - на скафандр: сначала скафандр (K)", 1); else if (!jet.Worn()) TakePack(cx.ship); }
			else if (ItemVessel* item = ItemOf(cx.ship))
			{
				const OcHeld h = item->held;
				if (Give(h)) { oapiDeleteVessel(cx.ship, GetHandle()); cx.ship = nullptr; cx.node = -1; cx.local = 0; cx.acts.clear(); }
			}
			return true;
		}
		ShipInterior* si = CtxInterior();
		if (!si || cx.node < 0 || cx.acts.empty() || seat) return false;
		const OcAction& a = cx.acts[(std::min)(cx.sel, static_cast<int>(cx.acts.size()) - 1)];
		if (!a.available) { Say(a.reason[0] ? Utf8(a.reason) : std::string("Сейчас нельзя"), 1); return true; }
		if (!si->ext.Begin || !si->ext.Begin(si->ctx, cx.node, a.id, who.id)) { Say("Не вышло: " + Utf8(a.label), 1); return true; }
		oapiWriteLogV("OrbiterCrew: %s begins '%s' at node %d of %s (%.1f s)", name.c_str(), a.label, cx.node, oapiGetVesselInterface(cx.ship)->GetName(), a.durationS);
		if (a.durationS > 0)
		{
			cx.busy = true; cx.act = a.id; cx.t = 0; cx.dur = a.durationS; cx.interruptible = a.interruptible != 0;
			cx.feet0 = inFeet;
			if (!inShip) { double lng, la, rad; GetEquPos(lng, la, rad); cx.feet0 = _V(lng * rad * std::cos(la), la * rad, 0); }
			fwd = lat = turn = 0;
		}
		cx.refreshT = 0;
		return true;
	}

	void CrewMember::CtxBreak(bool done)
	{
		if (!cx.busy) return;
		cx.busy = false;
		if (ShipInterior* si = CtxInterior()) if (si->ext.End) si->ext.End(si->ctx, cx.node, cx.act, who.id, done ? 1 : 0);
		oapiWriteLogV("OrbiterCrew: %s %s action %d at node %d", name.c_str(), done ? "finishes" : "breaks off", cx.act, cx.node);
		if (!done) Say("Прервано");
		cx.act = -1; cx.refreshT = 0;
	}

	// Painting letters on nothing: the sketchpad's ALPHABLEND keeps the target's alpha (D3D9: «retain destination alpha
	// unchanged») - on a cleared, transparent surface everything stayed invisible. So the letters go with COPY (their
	// atlas colour and alpha), and the shadow is a layer of its own (the same letters, black, on its own surface, drawn a
	// pixel lower right and a little behind) - two copies never wipe each other's alpha
	namespace
	{
		void CtxText(oapi::Sketchpad* skp, const HudText& t, int x, int base, const std::string& u, int size, int col, int align, double sc, bool shadow)
		{
			if (!t.Ok()) return;
			const oapi::FVECTOR4 black(0.0f, 0.0f, 0.0f, 0.80f);
			skp->SetBlendState(oapi::Sketchpad::COPY);
			if (shadow) skp->SetBrightness(&black);
			t.Draw(skp, x, base, u, size, col, align, sc);
			if (shadow) skp->SetBrightness(nullptr);
			skp->SetBlendState();
		}
		void CtxBar(oapi::Sketchpad* skp, int x0, int y0, int x1, int y1, DWORD argb, bool shadow)
		{
			if (x1 <= x0) return;
			oapi::Brush* b = oapiCreateBrush(shadow ? 0xCC000000 : argb);
			skp->SetBlendState(oapi::Sketchpad::COPY);
			skp->SetPen(nullptr); skp->SetBrush(b); skp->Rectangle(x0, y0, x1, y1);
			skp->SetBrush(nullptr); oapiReleaseBrush(b);
			skp->SetBlendState();
		}
	}

	// the seat's gauges into their row: the captions small and dim over the values (white; amber a warning, red an
	// alarm), a thin bar under a value that has one
	void CrewMember::GaugePaint(int& usedW)
	{
		std::string key;
		for (const OcGauge& g : seatGauges) key += std::string(g.label) + "|" + g.value + "|" + std::to_string(static_cast<int>(g.frac * 100)) + "|" + std::to_string(g.state) + "#";
		if (!cxTextTried) { cxTextTried = true; cxText.Load(); }
		const double sc = 2.0;
		std::vector<int> w;
		int total = 0;
		for (const OcGauge& g : seatGauges)
		{
			const int lw = cxText.Ok() ? static_cast<int>(cxText.Width(Utf8(g.label), 0, sc)) : 8 * static_cast<int>(std::strlen(g.label));
			const int vw = cxText.Ok() ? static_cast<int>(cxText.Width(Utf8(g.value), 1, sc)) : 11 * static_cast<int>(std::strlen(g.value));
			w.push_back((std::max)((std::max)(lw, vw), 40)); total += w.back() + 28;
		}
		total = (std::min)(1024, (std::max)(64, total));
		usedW = total;
		if (key == gaugePainted) return;
		gaugePainted = key;
		for (int layer = 0; layer < 2; ++layer)
		{
			SURFHANDLE surf = layer ? gaugeShadow : gaugeSurf;
			if (!surf) continue;
			const bool sh = layer == 1;
			oapiClearSurface(surf, 0);
			oapi::Sketchpad* skp = oapiGetSketchpad(surf);
			if (!skp) continue;
			int x = 14;
			for (size_t k = 0; k < seatGauges.size() && k < w.size(); ++k)
			{
				const OcGauge& g = seatGauges[k];
				const int cxm = x + w[k] / 2;
				CtxText(skp, cxText, cxm, 20, Utf8(g.label), 0, 4, 1, sc, sh);
				CtxText(skp, cxText, cxm, 46, Utf8(g.value), 1, g.state == 2 ? 3 : g.state == 1 ? 1 : 2, 1, sc, sh);
				if (g.frac >= 0)
				{
					const DWORD col = g.state == 2 ? 0xFF4646FF : g.state == 1 ? 0xFF2894FF : 0xFFFFE246;
					CtxBar(skp, x, 54, x + w[k], 56, 0x70FFE246, sh);
					CtxBar(skp, x, 54, x + static_cast<int>(w[k] * std::clamp(g.frac, 0.0, 1.0)), 56, col, sh);
				}
				x += w[k] + 28;
			}
			oapiReleaseSketchpad(skp);
		}
	}

	// the caption (the user's choice, 2026-10-07: «только текст с тенью»): no frame, no plate - the helmet display's own
	// letters, one atlas texel to a screen pixel (as sharp as the suit's display), their shadow a layer of its own. One
	// action: «F  ВСТАТЬ» - the key in cyan, the action white. Several: the node's name small and dim, the actions (the
	// picked one white with a thin cyan bar at its left, one that cannot be done dim with why in amber), «колесо — выбор»
	static const int kCtxPanelW = 420, kCtxTitleH = 22, kCtxRowH = 26, kCtxHintH = 22;
	static const double kCtxScale = 2.0;   // the atlas's own pixels: 1:1
	void CrewMember::CtxPaint()
	{
		if (!cxSurf) return;
		std::string key = std::string(cx.nd.label) + "|" + std::to_string(cx.node) + "|" + std::to_string(cx.sel) + "|" + std::to_string(cx.busy ? static_cast<int>(60 * cx.t / (std::max)(0.1, cx.dur)) : -1);
		for (const OcAction& a : cx.acts) key += std::string("|") + std::to_string(a.id) + a.label + (a.available ? "+" : "-") + a.reason;
		if (key == cx.painted) return;
		cx.painted = key;
		if (!cxTextTried) { cxTextTried = true; cxText.Load(); }
		auto upper = [](const std::string& u)
		{
			std::wstring w = Wide(u.c_str(), CP_UTF8);
			if (!w.empty()) CharUpperBuffW(&w[0], static_cast<DWORD>(w.size()));
			int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string o(n > 0 ? n - 1 : 0, ' ');
			if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &o[0], n, nullptr, nullptr);
			return o;
		};
		const int n = static_cast<int>(cx.acts.size()), rows = (std::min)(n, 6);
		const double prog = cx.busy ? std::clamp(cx.t / (std::max)(0.1, cx.dur), 0.0, 1.0) : 0.0;
		for (int layer = 0; layer < 2; ++layer)
		{
			SURFHANDLE surf = layer ? cxShadow : cxSurf;
			if (!surf) continue;
			const bool sh = layer == 1;
			oapiClearSurface(surf, 0);
			oapi::Sketchpad* skp = oapiGetSketchpad(surf);
			if (!skp) continue;
			auto text = [&](int x, int base, const std::string& u, int size, int col, int align) { CtxText(skp, cxText, x, base, u, size, col, align, kCtxScale, sh); };
			if (n == 1)
			{
				const OcAction& a = cx.acts[0];
				if (a.id != -101) text(4, kCtxRowH - 6, "F", 1, a.available ? 0 : 4, 0);
				text(28, kCtxRowH - 6, upper(Utf8(a.label)), 1, a.available ? 2 : 4, 0);
				if (!a.available && a.reason[0]) text(28, kCtxRowH + 16, Utf8(a.reason), 0, 1, 0);
				if (cx.busy) CtxBar(skp, 28, kCtxRowH - 1, 28 + static_cast<int>(200 * prog), kCtxRowH + 1, 0xFFFFE246, sh);
			}
			else
			{
				if (cx.node >= 0) text(10, kCtxTitleH - 5, upper(Utf8(cx.nd.label)), 0, 4, 0);
				int y = kCtxTitleH;
				for (int k = 0; k < rows; ++k, y += kCtxRowH)
				{
					const OcAction& a = cx.acts[k];
					const bool picked = k == cx.sel;
					if (picked) CtxBar(skp, 2, y + 5, 4, y + kCtxRowH - 4, 0xFFFFE246, sh);
					text(10, y + kCtxRowH - 6, upper(Utf8(a.label)), 1, !a.available ? 4 : picked ? 2 : 0, 0);
					if (!a.available && a.reason[0]) text(kCtxPanelW - 6, y + kCtxRowH - 7, Utf8(a.reason), 0, 1, 2);
					if (cx.busy && a.id == cx.act) CtxBar(skp, 10, y + kCtxRowH - 2, 10 + static_cast<int>((kCtxPanelW - 20) * prog), y + kCtxRowH, 0xFFFFE246, sh);
				}
				const std::string hint = cx.busy ? (cx.interruptible ? Tr("F \xE2\x80\x94 \xD0\xBF\xD1\x80\xD0\xB5\xD1\x80\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C", "F \xE2\x80\x94 stop") : "")
				                                 : Tr("\xD0\xBA\xD0\xBE\xD0\xBB\xD0\xB5\xD1\x81\xD0\xBE \xE2\x80\x94 \xD0\xB2\xD1\x8B\xD0\xB1\xD0\xBE\xD1\x80", "wheel \xE2\x80\x94 choose");   // F — прервать / колесо — выбор
				if (!hint.empty()) text(10, y + kCtxHintH - 6, hint, 0, 4, 0);
			}
			oapiReleaseSketchpad(skp);
		}
	}

	// ---- the context layer's letters: the helmet display's own glyphs (Config\Tantra\HudFont.txt + its atlas
	// Textures\Tantra\HudFont.dds, Jura), each a small quad of the layer's mesh with the atlas's own alpha. Painting them
	// into a surface lost them (ALPHABLEND keeps the target's alpha, COPY took the wrong cells); quads of the atlas are
	// exact, one atlas texel to a screen pixel, and their shadow is the same quads in black ----
	namespace
	{
		struct AGlyph { int x{}, y{}, w{}, h{}, ox{}, oy{}; float adv{}; };
		struct Atlas { bool tried{}, ok{}; double W{ 2048 }, H{ 2048 }; std::map<unsigned long long, AGlyph> g; };
		unsigned long long AKey(int size, int col, unsigned cp) { return (static_cast<unsigned long long>(size) << 40) | (static_cast<unsigned long long>(col) << 32) | cp; }
		Atlas& TheAtlas()
		{
			static Atlas a;
			if (a.tried) return a;
			a.tried = true;
			FILE* f = std::fopen("Config\\Tantra\\HudFont.txt", "r");
			if (!f) return a;
			char line[256];
			while (std::fgets(line, sizeof line, f))
			{
				if (line[0] == '#') continue;
				int aw, ah, u;
				if (std::sscanf(line, "ATLAS %d %d %d", &aw, &ah, &u) == 3) { a.W = aw; a.H = ah; continue; }
				int si, ci, x, y, w, h, ox, oy; unsigned cp; float adv;
				if (std::sscanf(line, "%d %d %u %d %d %d %d %d %d %f", &si, &ci, &cp, &x, &y, &w, &h, &ox, &oy, &adv) == 10) a.g[AKey(si, ci, cp)] = { x, y, w, h, ox, oy, adv };
			}
			std::fclose(f);
			a.ok = !a.g.empty();
			return a;
		}
		std::vector<unsigned> U8(const std::string& s)
		{
			std::vector<unsigned> o;
			for (size_t i = 0; i < s.size();)
			{
				const unsigned char c = static_cast<unsigned char>(s[i]);
				unsigned cp = c; int n = 1;
				if (c >= 0xF0 && i + 3 < s.size()) { cp = ((c & 7u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu); n = 4; }
				else if (c >= 0xE0 && i + 2 < s.size()) { cp = ((c & 15u) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu); n = 3; }
				else if (c >= 0xC0 && i + 1 < s.size()) { cp = ((c & 31u) << 6) | (s[i + 1] & 0x3Fu); n = 2; }
				o.push_back(cp); i += n;
			}
			return o;
		}
		struct GQuad { double x0, y0, x1, y1; float u0, v0, u1, v1; };   // screen px from the top left of its block (y down), its texels
		double TextW(const std::string& u, int size)
		{
			Atlas& A = TheAtlas(); double w = 0;
			for (unsigned cp : U8(u)) { auto it = A.g.find(AKey(size, 0, cp)); if (it != A.g.end()) w += it->second.adv; }
			return w;
		}
		// a line at the pen (x, the baseline), in the atlas's colour col: its quads
		void Lay(std::vector<GQuad>& q, double x, double base, const std::string& u, int size, int col, int align)
		{
			Atlas& A = TheAtlas();
			double pen = x - (align == 1 ? 0.5 : align == 2 ? 1.0 : 0.0) * TextW(u, size);
			for (unsigned cp : U8(u))
			{
				auto it = A.g.find(AKey(size, col, cp));
				if (it == A.g.end()) continue;
				const AGlyph& g = it->second;
				if (g.w > 0 && g.h > 0)
				{
					const double x0 = std::floor(pen + g.ox + 0.5), y0 = std::floor(base + g.oy + 0.5);
					q.push_back({ x0, y0, x0 + g.w, y0 + g.h, static_cast<float>(g.x / A.W), static_cast<float>(g.y / A.H), static_cast<float>((g.x + g.w) / A.W), static_cast<float>((g.y + g.h) / A.H) });
				}
				pen += g.adv;
			}
		}
		std::string Upper8(const std::string& u)
		{
			std::wstring w = Wide(u.c_str(), CP_UTF8);
			if (!w.empty()) CharUpperBuffW(&w[0], static_cast<DWORD>(w.size()));
			int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string o(n > 0 ? n - 1 : 0, ' ');
			if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &o[0], n, nullptr, nullptr);
			return o;
		}
	}

	// on the screen: the node's caption beside the aim (or beside the cursor with Alt), the seat's gauges along the
	// bottom, the cursor's cross. No brackets, no frames, no plates (the user, 2026-10-07: «только текст с тенью»):
	// the helmet display's letters with their shadow. All on a plane 0.3 m before her eyes, laid out in the screen's
	// pixels and placed in her vessel's own frame from her camera there (never through the global frame)
	void CrewMember::CtxDraw()
	{
		if (ctxTest)   // a drawing test: a caption and a gauge row always
		{
			if (useHint.empty()) useHint = "\xD1\x82\xD0\xB5\xD1\x81\xD1\x82 \xD0\xBC\xD0\xB5\xD0\xBD\xD1\x8E";   // тест меню (UTF-8)
			if (seatGauges.empty())
			{
				OcGauge g{}; g.size = sizeof g;   // (cp1251, as a ship gives them)
				std::snprintf(g.label, sizeof g.label, "%s", "\xD1\xCA\xCE\xD0\xCE\xD1\xD2\xDC"); std::snprintf(g.value, sizeof g.value, "%s", "12 \xEA\xEC/\xF7"); g.frac = -1; seatGauges.push_back(g);
				std::snprintf(g.label, sizeof g.label, "%s", "\xC7\xC0\xD0\xDF\xC4"); std::snprintf(g.value, sizeof g.value, "%s", "78 %"); g.frac = 0.78; seatGauges.push_back(g);
				std::snprintf(g.label, sizeof g.label, "%s", "\xCA\xD0\xC5\xCD"); std::snprintf(g.value, sizeof g.value, "%s", "+16\xB0"); g.frac = -1; g.state = 1; seatGauges.push_back(g);
			}
		}
		const bool nodeMenu = cx.ship && cx.node >= 0 && !cx.acts.empty();
		// F's old uses (seen without the suit's display); a node out of reach. Not while she sits: F stands her up, and that
		// is not shown all the time she drives (the user, 2026-10-07: «постоянное ВСТАТЬ в машине - убрать»)
		const bool fHint = !nodeMenu && !seat && ((!useHint.empty() && (!(suitOn && hudOn) || ctxTest)) || farNode) && bio.CanAct();
		const bool menu = nodeMenu || fHint;
		const bool cross = simCursor;
		const bool gauges = !seatGauges.empty();
		const bool marks = cx.cursor && !cx.marks.empty();
		const bool show = menu || cross || gauges || marks;
		// in the suit with its display on: onto the display's plate (CtxPaintPlate); else our own layer before her eyes
		const bool plate = oapiCameraInternal() && oapiCameraTarget() == GetHandle() && suitOn && hudOn && !thirdIn;
		plateOn = false; plateRuns.clear(); plateBoxes.clear(); plateMarks = plate && marks;
		if (plate) { if (cxMesh != static_cast<UINT>(-1)) SetMeshVisibilityMode(cxMesh, MESHVIS_NEVER); }
		else
		{
			if (cxMesh == static_cast<UINT>(-1))
			{
				if (!show) return;
				cxMesh = AddMesh("OrbiterCrew\\CtxMarker");
			}
			SetMeshVisibilityMode(cxMesh, !show ? MESHVIS_NEVER : oapiCameraInternal() ? MESHVIS_COCKPIT | MESHVIS_VC : MESHVIS_EXTERNAL);
		}
		if (!show) return;
		DEVMESHHANDLE dev = nullptr;
		if (!plate)
		{
			if (!vis) return;
			dev = GetDevMesh(vis, cxMesh);
			if (!dev) return;
		}
		if (!TheAtlas().ok) return;
		gcCore2* core = gcGetCoreInterface(); const HWND view = core ? core->GetRenderWindow() : nullptr;
		RECT rc{}; if (!view || !GetClientRect(view, &rc) || rc.bottom <= 0) return;
		const double Wp = rc.right, Hp = rc.bottom;
		MATRIX3 Rc; oapiCameraRotationMatrix(&Rc); MATRIX3 Rv; GetRotationMatrix(Rv);
		VECTOR3 R = tmul(Rv, mul(Rc, _V(1, 0, 0))), U = tmul(Rv, mul(Rc, _V(0, 1, 0))), F = tmul(Rv, mul(Rc, _V(0, 0, 1)));
		VECTOR3 C;
		if (oapiCameraInternal() && oapiCameraTarget() == GetHandle() && !thirdIn)
		{
			C = camEye;
			if (headAsked)   // her eyes: the look set this frame (yaw from her vessel's nose, + right; pitch + up)
			{
				const double y = headWantYaw, p = headWantPitch;
				F = _V(std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p));
				R = _V(std::cos(y), 0, -std::sin(y));
				U = crossp(F, R);
			}
		}
		else { VECTOR3 cg; oapiCameraGlobalPos(&cg); Global2Local(cg, C); }
		// 0.25 m: well beyond the cockpit's near plane (0.1 m, D3D9Client VCNearPlane). The helmet display's plate is never
		// shown with this layer (in the suit with the display on the caption is on the plate itself): nothing to fight.
		// The mesh's groups go back to front: band 0, key 1, bar shadow 2, letter shadow 3, bars 4, letters 5, cross
		// outline 6, cross 7, Alt marks 8
		const double q = 0.25, tanA = std::tan((std::max)(0.1, oapiCameraAperture())), px = 2 * q * tanA / Hp;
		// screen px: from the top left of the window (x right, y down), on the pixel grid (D3D9's half pixel)
		const double ox = Wp / 2, oy = Hp / 2;
		auto at = [&](double sx, double sy, double dq) { const double k = (q + dq) / q, X = sx - 0.5 - ox, Y = oy - (sy - 0.5); return C + F * (q + dq) + R * (X * px * k) + U * (Y * px * k); };
		auto put = [&](NTVERTEX& v, const VECTOR3& l, float tu, float tv)
		{
			v.x = static_cast<float>(l.x); v.y = static_cast<float>(l.y); v.z = static_cast<float>(l.z);
			v.nx = static_cast<float>(-F.x); v.ny = static_cast<float>(-F.y); v.nz = static_cast<float>(-F.z); v.tu = tu; v.tv = tv;
		};
		// a quad, clockwise as seen: bottom left, top left, top right, bottom right
		auto quad = [&](NTVERTEX* v, double x0, double y0, double x1, double y1, double dq, float u0 = 0, float v0 = 0, float u1 = 0, float v1 = 0)
		{
			put(v[0], at(x0, y1, dq), u0, v1); put(v[1], at(x0, y0, dq), u0, v0); put(v[2], at(x1, y0, dq), u1, v0); put(v[3], at(x1, y1, dq), u1, v1);
		};
		auto edit = [&](DWORD g, std::vector<NTVERTEX>& v)
		{
			GROUPEDITSPEC e{}; e.flags = GRPEDIT_VTXCRD | GRPEDIT_VTXNML | GRPEDIT_VTXTEX; e.Vtx = v.data(); e.nVtx = static_cast<DWORD>(v.size());
			oapiEditMeshGroup(dev, g, &e);
		};
		std::vector<GQuad> glyphs, keyLetters;               // screen px (top left, y down); a key cap's letter: black, on the cap
		auto lay = [&](std::vector<GQuad>& qv, double x, double base, const std::string& u, int size, int col, int align)
		{
			Lay(qv, x, base, u, size, col, align);
			if (plate) plateRuns.push_back({ x, base, u, size, col, align, &qv == &keyLetters });
		};
		struct Bar { double x0, y0, x1, y1; };
		std::vector<Bar> bars;
		// ---- the caption: a neutral key cap with its letter, the picked action white on a soft dark band, the others dim
		// above and below it (the list stays where it is, the band moves), a thin bar of a long action's time along the
		// band's foot (the user's examples, 2026-10-07) ----
		struct Box { double x0, y0, x1, y1; };
		std::vector<Box> keys, bands;
		double ex = ox, ey = oy;   // the eye's point: the middle, or the cursor
		if (cross) { POINT p; if (GetCursorPos(&p) && ScreenToClient(view, &p)) { ex = p.x; ey = p.y; } }
		if (menu)
		{
			std::vector<OcAction> acts = cx.acts;
			int sel = (std::min)(cx.sel, static_cast<int>(acts.size()) - 1);
			if (fHint)
			{
				OcAction a{}; a.size = sizeof a; a.id = -100; a.available = 1;
				const bool own = !useHint.empty() && (!(suitOn && hudOn) || ctxTest);
				const std::string h = own ? useHint : std::string(Tr("\xD0\xBF\xD0\xBE\xD0\xB4\xD0\xBE\xD0\xB9\xD0\xB4\xD0\xB8\xD1\x82\xD0\xB5 \xD0\xB1\xD0\xBB\xD0\xB8\xD0\xB6\xD0\xB5", "come closer"));   // подойдите ближе
				if (!own) { a.available = 0; a.id = -101; }
				std::snprintf(a.label, sizeof a.label, "%s", ToCp1251(h).c_str());
				acts.assign(1, a); sel = 0;
			}
			const int n = (std::min)(static_cast<int>(acts.size()), 7);
			const double rowH = 30, smallH = 22, keyW = 22;
			const double prog = cx.busy ? std::clamp(cx.t / (std::max)(0.1, cx.dur), 0.0, 1.0) : 0.0;
			// the picked row level with the eye's point, a little right of it; the others stacked above and below
			const double xk = std::floor(ex + 26), xt = xk + keyW + 10;
			const double ySel = std::floor(ey - rowH / 2 + 2);
			for (int k = 0; k < n; ++k)
			{
				const OcAction& a = acts[k];
				if (k == sel)
				{
					const std::string t = Utf8(a.label);
					const double tw = TextW(t, 2);
					bands.push_back({ xk - 6, ySel, xt + tw + 14, ySel + rowH });
					if (a.id != -101) { keys.push_back({ xk, ySel + 4, xk + keyW, ySel + rowH - 4 }); lay(keyLetters, xk + keyW / 2, ySel + rowH - 9, "F", 1, 0, 1); }
					lay(glyphs, xt, ySel + rowH - 8, t, 2, a.available ? 2 : 6, 0);
					if (!a.available && a.reason[0]) lay(glyphs, xt + tw + 24, ySel + rowH - 9, Utf8(a.reason), 0, 1, 0);
					if (cx.busy && a.id == cx.act) bars.push_back({ xk - 6, ySel + rowH - 2, xk - 6 + (xt + tw + 20 - xk) * prog, ySel + rowH });
				}
				else
				{
					const double y = k < sel ? ySel - smallH * (sel - k) : ySel + rowH + smallH * (k - sel - 1);
					lay(glyphs, xt, y + smallH - 6, Utf8(a.label), 1, 6, 0);
				}
			}
			// the node's name: small and dim over the list
			if (cx.node >= 0 && !fHint) lay(glyphs, xt, ySel - smallH * sel - 8, Upper8(Utf8(cx.nd.label)), 0, 6, 0);
		}
		// ---- the seat's gauges along the bottom: each caption over its value ----
		if (gauges)
		{
			std::vector<double> w; double total = 0;
			for (const OcGauge& g : seatGauges) { w.push_back((std::max)((std::max)(TextW(Utf8(g.label), 0), TextW(Utf8(g.value), 1)), 40.0)); total += w.back() + 28; }
			total -= 28;
			double x = std::floor(ox - total / 2);
			const double yb = Hp - (suitOn && hudOn ? 150 : 34);   // the values' baseline
			for (size_t k = 0; k < seatGauges.size(); ++k)
			{
				const OcGauge& g = seatGauges[k];
				const double cxm = x + w[k] / 2;
				lay(glyphs, cxm, yb - 26, Utf8(g.label), 0, 4, 1);
				lay(glyphs, cxm, yb, Utf8(g.value), 1, g.state == 2 ? 3 : g.state == 1 ? 1 : 2, 1);
				if (g.frac >= 0) bars.push_back({ x, yb + 8, x + w[k] * std::clamp(g.frac, 0.0, 1.0), yb + 10 });
				x += w[k] + 28;
			}
		}
		if (plate)
		{
			// the plate adds light to the view: the dark band and the shadows would be nothing on it - the key caps, the
			// bars and the cross are (ABGR)
			for (const Box& b : keys) plateBoxes.push_back({ b.x0, b.y0, b.x1, b.y1, 0xFFDBD6D1 });
			for (const Bar& b : bars) if (b.x1 > b.x0) plateBoxes.push_back({ b.x0, b.y0, b.x1, b.y1, 0xFFFFE345 });
			if (cross)
			{
				const double g = 3, Ln = 8, h = 1;
				const double arms[4][4] = { { g, -h, g + Ln, h }, { -g - Ln, -h, -g, h }, { -h, g, h, g + Ln }, { -h, -g - Ln, h, -g } };
				for (const auto& a : arms) plateBoxes.push_back({ ex + a[0], ey + a[1], ex + a[2], ey + a[3], 0xFFFFE345 });
			}
			plateOn = true;
			return;
		}
		// ---- into the mesh: the letters (1) and their shadow (4), the bars (0) and theirs (5), the cross (2, 3) ----
		{
			std::vector<NTVERTEX> L(4000), S(4000);
			int k = 0, ks = 0;
			// each glyph a hair nearer than the one before: their quads overlap, and on one depth the clear edge of one
			// hid a strip of its neighbour, a different one each frame
			for (const GQuad& g : glyphs)
			{
				if (k >= 1000 || ks >= 1000) break;
				quad(&L[k * 4], g.x0, g.y0, g.x1, g.y1, -2e-6 * k, g.u0, g.v0, g.u1, g.v1);
				quad(&S[ks * 4], g.x0 + 1, g.y0 + 1, g.x1 + 1, g.y1 + 1, 0.0015 - 2e-6 * ks, g.u0, g.v0, g.u1, g.v1);
				++k; ++ks;
			}
			for (const GQuad& g : keyLetters)   // black, on its cap (the cap lies behind it)
			{
				if (ks >= 1000) break;
				quad(&S[ks * 4], g.x0, g.y0, g.x1, g.y1, -2e-6 * ks, g.u0, g.v0, g.u1, g.v1);
				++ks;
			}
			edit(5, L); edit(3, S);
			std::vector<NTVERTEX> K(64), BD(64);
			int kk = 0, kb = 0;
			for (const Box& b : keys) if (kk < 16) quad(&K[kk++ * 4], b.x0, b.y0, b.x1, b.y1, 0.002);
			for (const Box& b : bands) if (kb < 16) quad(&BD[kb++ * 4], b.x0, b.y0, b.x1, b.y1, 0.003);
			edit(1, K); edit(0, BD);
		}
		{
			std::vector<NTVERTEX> B(256), BS(256);
			int k = 0;
			for (const Bar& b : bars)
			{
				if (k >= 64 || b.x1 <= b.x0) continue;
				quad(&B[k * 4], b.x0, b.y0, b.x1, b.y1, 0.0);
				quad(&BS[k * 4], b.x0 + 1, b.y0 + 1, b.x1 + 1, b.y1 + 1, 0.0015);
				++k;
			}
			edit(4, B); edit(2, BS);
		}
		{
			std::vector<NTVERTEX> A(16), O(16);
			if (cross)
			{
				const double g = 3, L = 8, h = 0.75;
				const double arms[4][4] = { { g, -h, g + L, h }, { -g - L, -h, -g, h }, { -h, g, h, g + L }, { -h, -g - L, h, -g } };
				for (int k = 0; k < 4; ++k)
				{
					quad(&A[k * 4], ex + arms[k][0], ey + arms[k][1], ex + arms[k][2], ey + arms[k][3], 0.0);
					quad(&O[k * 4], ex + arms[k][0] - 1, ey + arms[k][1] - 1, ex + arms[k][2] + 1, ey + arms[k][3] + 1, 0.002);
				}
			}
			edit(7, A); edit(6, O);
		}
		{   // ---- with Alt: the places of actions, orange squares - near her hand big and bright, further small ----
			std::vector<NTVERTEX> MK(256);
			int k = 0;
			if (marks)
			{
				const double f = Hp / (2 * tanA);   // screen px per unit of the view's tangent
				for (const auto& m : cx.marks)
				{
					if (!oapiIsVessel(m.ship) || k + 4 > 64) continue;
					VECTOR3 g, l; oapiGetVesselInterface(m.ship)->Local2Global(m.pos, g); Global2Local(g, l);
					const VECTOR3 v = l - C; const double z = dotp(v, F);
					if (z < 0.1) continue;
					const double sx = std::round(ox + dotp(v, R) / z * f), sy = std::round(oy - dotp(v, U) / z * f);
					if (sx < 0 || sy < 0 || sx > Wp || sy > Hp) continue;
					const double h = m.reach ? 7 : 4, th = m.reach ? 2 : 1;
					quad(&MK[k++ * 4], sx - h, sy - h, sx + h, sy - h + th, 0.0);
					quad(&MK[k++ * 4], sx - h, sy + h - th, sx + h, sy + h, 0.0);
					quad(&MK[k++ * 4], sx - h, sy - h + th, sx - h + th, sy + h - th, 0.0);
					quad(&MK[k++ * 4], sx + h - th, sy - h + th, sx + h, sy + h - th, 0.0);
				}
			}
			edit(8, MK);
		}
	}

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
		// her vessel keeps the ship's axes (a turning body made the ship's screens flicker); she turns in her pose,
		// her head turns the camera
		sv->SetAttachmentParams(inParent, inFeet + si->Origin(), _V(0, 0, 1), _V(0, 1, 0));
		HangOn(sv);
		who.where = Person::INTERIOR; who.ship = ship; who.vessel = GetHandle();
		if (si->fns.Viewing) { si->fns.Viewing(si->ctx, 1); inViewing = true; }
	}

	// she hangs on the ship's interior point and on no other point of it: a saved scenario attaches her again by the
	// point's number (ATTACHED i:j), which is that point only while the ship makes its points in the same order
	void CrewMember::HangOn(VESSEL* sv)
	{
		if (sv->GetAttachmentStatus(inParent) == GetHandle()) return;
		for (DWORD i = 0, n = sv->AttachmentCount(false); i < n; ++i)
		{
			ATTACHMENTHANDLE a = sv->GetAttachmentHandle(false, i);
			if (a == inParent || sv->GetAttachmentStatus(a) != GetHandle()) continue;
			sv->DetachChild(a);
			oapiWriteLogV("OrbiterCrew: %s was on another attachment point of %s - moved to its interior point", name.c_str(), sv->GetName());
		}
		sv->AttachChild(GetHandle(), inParent, inChild);
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
		oapiCameraSetAperture(kViewAperture); fovSet = true;   // her view: 70 deg (the user, 2026-10-05)
		if (!who.viewOutside) { oapiCameraAttach(GetHandle(), CAM_INSIDE); return; }
		oapiCameraAttach(GetHandle(), CAM_OUTSIDE);
		const double d = oapiCameraTargetDist();
		if (who.viewDist > 0.5 && d > 0) oapiCameraScaleDist(who.viewDist / d);
	}

	// where she is in that seat: the feet under the ship's hips point (the clip's seated hips are hipY over the feet).
	// A figure without the seat clips stands with its hips there: her eyes are where a seated person's are (a fallback;
	// the suit has none and does not sit for now - it comes off first)
	bool CrewMember::SeatPlace(int id)
	{
		ShipInterior* si = InteriorOf(inShip);
		Figure& fig = Active();
		if (!si || !si->fns.Seat || !fig.ok) return false;
		VECTOR3 hips{}, dir{ 0, 0, 1 };
		si->fns.Seat(si->ctx, id, &hips, &dir);
		const int hb = fig.skin.Bone("Hips");
		const double hipY = fig.clips.seats && hb >= 0 && fig.clips.sit.data.size() > static_cast<size_t>(hb * 7 + 5) ? fig.clips.sit.data[hb * 7 + 5] + height : height;
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
		if (!si || !si->fns.Seat || !fig.ok) return;
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
		// only a click into the 3D view: Orbiter's dialogs and MFD windows over it are not the ship's buttons
		static gcCore2* core = nullptr; static bool coreTried = false;
		if (!coreTried) { coreTried = true; core = gcGetCoreInterface(); }
		const HWND view = core ? core->GetRenderWindow() : nullptr;
		RECT rc{};
		// the cursor and the frame's size in the 3D view's own client area: over a child window of it (the menu bar) its
		// rect would shift and scale the ray (the user, 2026-10-05: the seat's keys pressed beside where clicked)
		const HWND area = view ? view : w;
		if (!w || pid != GetCurrentProcessId() || (view && w != view && !IsChild(view, w)) || !ScreenToClient(area, &p) || !GetClientRect(area, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
		const double W = rc.right, H = rc.bottom, fpx = (H / 2) / std::tan((std::max)(0.1, oapiCameraAperture()));
		VECTOR3 cp; oapiCameraGlobalPos(&cp); MATRIX3 Rc; oapiCameraRotationMatrix(&Rc);
		const VECTOR3 dg = mul(Rc, unit(_V((p.x - W / 2) / fpx, (H / 2 - p.y) / fpx, 1.0)));   // camera -> global
		if (debugLog) oapiWriteLogV("OrbiterCrew: click at (%ld,%ld) of %.0fx%.0f, aperture %.1f deg%s", p.x, p.y, W, H, oapiCameraAperture() * DEG, w != area ? " (over a child window)" : "");
		VESSEL* sv = oapiGetVesselInterface(si.ship);
		VECTOR3 o; sv->Global2Local(cp, o); o -= si.Origin();
		MATRIX3 Rs; sv->GetRotationMatrix(Rs);
		const VECTOR3 d = tmul(Rs, dg);                                                          // global -> ship
		// the ship's own touch screens first
		if (si.ext.Click && si.ext.Click(si.ctx, &o, &d, who.id))
		{
			if (debugLog) oapiWriteLogV("OrbiterCrew: click (%ld,%ld) taken by a screen of %s", p.x, p.y, sv->GetName());
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
		// what a click was aimed at: in the log with DebugLog = 1 in her config (tests); a pressed button always below
		if (debugLog)
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
		// the constant look: her eyes, Orbiter's 3D view in front (no dialog over it), not seated, Alt not held, not the
		// right button (Orbiter's own turning then)
		static gcCore2* core = nullptr; static bool coreTried = false;
		if (!coreTried) { coreTried = true; core = gcGetCoreInterface(); }
		const HWND view = core ? core->GetRenderWindow() : nullptr;
		const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
		const bool front = fw && view && (fw == view || IsChild(fw, view) || IsChild(view, fw) || GetAncestor(view, GA_ROOT) == fw);
		lookCapture = eyes && front && seat == 0 && !alt && !rmb && bio.state != Body::DEAD;
		simCursor = false;   // (Alt: Windows' own cursor for now - the sim's cross broke clicks on Orbiter's menu, the user 2026-10-07)
		double mdx = 0, mdy = 0;
		if (lookCapture && view)
		{
			RECT rc{}; GetClientRect(view, &rc);
			POINT c = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 }; ClientToScreen(view, &c);
			POINT p; GetCursorPos(&p);
			if (lookWas) { mdx = p.x - c.x; mdy = p.y - c.y; }
			if (p.x != c.x || p.y != c.y) SetCursorPos(c.x, c.y);
			if (!cursorHidden) { ShowCursor(FALSE); cursorHidden = true; }
			if (!lookHinted) { lookHinted = true; lookHintT = 7.0; }
		}
		else if (simCursor) { if (!cursorHidden) { ShowCursor(FALSE); cursorHidden = true; } }
		else if (cursorHidden) { ShowCursor(TRUE); cursorHidden = false; }
		lookWas = lookCapture;
		if (lookHintT > 0) lookHintT -= dt;
		// the free look: on foot (not seated, not at a post), the right button turns the head alone
		const bool driving = seat != 0 || carried > 0;
		const bool fl = eyes && rmb && !driving;
		if (fl && !freeLook) { freeYaw0 = headYaw; freePitch0 = headPitch; lookBack = false; }
		if (!fl && freeLook) lookBack = true;
		freeLook = fl;
		mouseRmb = (rmb && !fl) || lookCapture;   // the body follows the look (not in the free look)
		mouseMode = kMouseWalk && eyes && seat == 0 && canTurn && !fl && (inShip || lookCapture);
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
		dYaw += mdx * kLookPerPx; dPitch -= mdy * kLookPerPx;   // the constant look's own mouse
		double turnBy = 0;
		if (lookBack && !freeLook)   // the free look let go: back where it was, in ~0.25 s (the mouse meanwhile turns as ever)
		{
			const double k = (std::min)(1.0, dt / 0.08);
			headYaw += (freeYaw0 - headYaw) * k; headPitch += (freePitch0 - headPitch) * k;
			if (std::abs(headYaw - freeYaw0) < 0.002 && std::abs(headPitch - freePitch0) < 0.002) { headYaw = freeYaw0; headPitch = freePitch0; lookBack = false; }
		}
		if (mouseMode) turnBy = dYaw;                                         // walking inside: she turns where she looks
		else headYaw = std::clamp(headYaw + dYaw, -70 * RAD, 70 * RAD);       // in a seat, outside: the neck
		if (mouseMode && !mouseRmb) headYaw = Approach(headYaw, 0.0, 1.5, dt);   // walking on, the head comes back ahead
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
		if (debugLog && headAsked && (headCheckT += dt) > 2.0)
		{
			headCheckT = 0;
			double y, p; LookDir(y, p);
			oapiWriteLogV("OrbiterCrew: head camera asked yaw %.1f pitch %.1f, shows yaw %.1f pitch %.1f (deg)",
				headWantYaw * DEG, headWantPitch * DEG, y * DEG, p * DEG);
		}
		const double yaw = inShip ? std::remainder(inHdg + headYaw, PI2) : headYaw;
		// Orbiter's own limit is the person's: walking inside she turns all round (wider than a full turn: its limit never
		// bites); seated and outside the neck (70 deg aside, 75 up and down, about her vessel's nose - her body, her seat)
		if (mouseMode) SetCameraRotationRange(PI2, PI2, 75 * RAD, 75 * RAD);
		else SetCameraRotationRange(70 * RAD, 70 * RAD, 75 * RAD, 75 * RAD);
		// Orbiter's cockpit angles (Camera::SetCockpitDir, measured 2026-10-03): the first turns the view to the LEFT
		// (yaw = -first), the second up (pitch = second); set at once, not limited by the rotation range
		oapiCameraSetCockpitDir(-yaw, headPitch);
		headWantYaw = yaw; headWantPitch = headPitch; headAsked = true;
		if (thirdIn)
		{
			// behind her head along her look, a little over it; in the interior's frame the walls stop it (a camera 0.15 m
			// round), and it keeps within 0.35 m over / 0.3 m under her eyes (no ceiling of its own: no farther)
			const VECTOR3 F = _V(std::sin(yaw) * std::cos(headPitch), std::sin(headPitch), std::cos(yaw) * std::cos(headPitch));
			VECTOR3 c = camEye - F * kThirdDist + _V(0, 0.12, 0);
			ShipInterior* si = InteriorOf(inShip);
			const bool ceil = si && si->ext.Ceiling;
			c.y = std::clamp(c.y, camEye.y - 0.3, camEye.y + (ceil ? 0.6 : 0.2));
			if (si)
			{
				const VECTOR3 o = inFeet + _V(0, height, 0);   // her frame -> the interior's
				if (si->fns.Walls)
				{
					VECTOR3 from = camEye + o - _V(0, 0.05, 0), to = c + o - _V(0, 0.05, 0);
					si->fns.Walls(si->ctx, &from, &to, 0.15, 0.1);
					c = to - o + _V(0, 0.05, 0);
				}
				if (ceil)   // 0.15 m under the ceiling there (the near plane is 0.1 m: nothing of it is cut)
				{
					const VECTOR3 at = c + o;
					const double top = si->ext.Ceiling(si->ctx, &at) - o.y - 0.15;
					if (c.y > top) c.y = (std::max)(top, camEye.y - 0.3);
				}
			}
			SetCameraOffset(c);
		}
		// the helmet display: before her eyes, along her look, in the suit with the HUD on
		SuitHud::HelmetView hv;
		hv.eye = camEye; hv.yaw = yaw; hv.pitch = headPitch;
		hv.show = suitOn && hudOn && !thirdIn;
		if (hv.show) { HudData d; FillHudData(d); hud.HelmetFrame(this, vis, hv, &d); }   // the whole HUD into its light layer
		else hud.HelmetFrame(this, vis, hv);
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
		shipView = true;
		oapiCameraAttach(inShip, CAM_OUTSIDE);   // the ship from outside; its camera is Orbiter's own from here (mouse, F2)
	}

	// outside on the ground: the step is clipped by the outer solids of the ships near her (OcInteriorExt::OuterWalls)
	void CrewMember::ClampToShips(VESSELSTATUS2& s)
	{
		if (!s.rbody) return;
		VECTOR3 me; GetGlobalPos(me);
		double lng0, lat0, rad0;
		oapiGlobalToEqu(s.rbody, me, &lng0, &lat0, &rad0);
		const double feetRad = rad0 - height;
		for (ShipInterior& si : interiors())
		{
			if (!si.ext.OuterWalls || !oapiIsVessel(si.ship)) continue;
			VESSEL* sv = oapiGetVesselInterface(si.ship);
			VECTOR3 sp; sv->GetGlobalPos(sp);
			if (length(sp - me) > 3 * sv->GetSize() + 200) continue;   // (a ship's feet can stand well beyond its size)
			VECTOR3 gFrom, gTo, lFrom, lTo;
			oapiEquToGlobal(s.rbody, lng0, lat0, feetRad, &gFrom);
			oapiEquToGlobal(s.rbody, s.surf_lng, s.surf_lat, feetRad, &gTo);
			sv->Global2Local(gFrom, lFrom); sv->Global2Local(gTo, lTo);
			const VECTOR3 want = lTo;
			si.ext.OuterWalls(si.ctx, &lFrom, &lTo, 0.25, heightM > 0 ? heightM : 1.75);
			if (length(lTo - want) < 1e-6) continue;
			if (!outerLogged) { outerLogged = true; oapiWriteLogV("OrbiterCrew: %s stopped by the outer solids of %s", name.c_str(), sv->GetName()); }
			VECTOR3 g; sv->Local2Global(lTo, g);
			double lng, lat, rad;
			oapiGlobalToEqu(s.rbody, g, &lng, &lat, &rad);
			s.surf_lng = lng; s.surf_lat = lat;
		}
	}

	// the camera's direction relative to the body (her head): yaw right +, pitch up +
	void CrewMember::LookDir(double& yaw, double& pitch)
	{
		VECTOR3 g; oapiCameraGlobalDir(&g);
		MATRIX3 R; GetRotationMatrix(R);
		const VECTOR3 l = tmul(R, g);
		yaw = std::atan2(l.x, l.z); pitch = std::atan2(l.y, std::hypot(l.x, l.z));
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
		// the ship's air, dose and supplies where she stands (OcInteriorExt::Cabin); without it a sealed cabin
		air = Air{}; air.body = "ship"; air.p = 101.3; air.T = 294; air.ppO2 = 21.2; air.ppCO2 = 0.04;
		double cabinDose = 0; bool supplied = true, open = false;   // no Cabin: a sealed cabin (Tantra); an open post says so itself
		if (si->ext.Cabin)
		{
			OcCabin c{}; c.size = sizeof c;
			if (si->ext.Cabin(si->ctx, &inFeet, &c)) { air.p = c.p; air.T = c.T; air.ppO2 = c.ppO2; air.ppCO2 = c.ppCO2; cabinDose = c.doseSvh; supplied = c.supplied != 0; }
			open = !supplied && c.ppO2 <= 0;   // no oxygen, no supplies: an open platform (the MPU), not a cabin
		}
		double g = 9.81;
		if (si->fns.Gravity) { VECTOR3 gv{}; si->fns.Gravity(si->ctx, &inFeet, &gv); g = length(gv); }
		// an open platform (its Cabin gives no oxygen and no supplies: the MPU's post): the world outside is hers - its air (the suit's oxygen in vacuum), its cold
		// and heat, its radiation through the suit (the user, 2026-10-05)
		double tEnv = air.T;
		if (open)
		{
			air = atmospheres.Sample(this);
			thermal = Surroundings(); tEnv = thermal.tEnv;
			radEnv = rad.Sample(this, air.p, g);
			cabinDose = Radiation::Dose(radEnv, suitOn ? suit.shieldGcm2 : 0.3, suitOn && suit.FieldUp() ? suit.fieldFactor : 1.0);
			supplied = false;
		}
		// the organism: walking costs, the ship's air, shielded (the ship's own radiation model belongs to the ship)
		const double v = std::hypot(fwd, lat);
		humanW = LocomotionPower(suitOn ? Mass() : bio.mass, v, g);
		double heat = 0;
		suit.vent = air.Breathable(); suit.ventPpO2 = air.ppO2; suit.ventPpCO2 = air.ppCO2; suit.pOut = air.p; suit.radRate = bio.doseRate;   // a cabin without air: the suit's own
		if (suitOn) heat = suitResidual = suit.Step(dt, bio.O2Use(), bio.CO2Made(), 0, bio.Heat(), tEnv);
		else if (open) { const double net = bio.Heat() + 8 * (air.p > 1 ? air.T - 295 : 0); heat = net > 150 ? net - 150 : net < -150 ? net + 150 : 0; }   // as outside
		bio.Step(dt, humanW + (turn ? 25 : 0), suitOn ? suit.ppO2 : air.ppO2, suitOn ? suit.ppCO2 : air.ppCO2, air.p, suitOn, heat);
		bio.Irradiate(dt, cabinDose);
		bio.Sustain(dt, supplied, suitOn ? &suit.water : nullptr, air.T, suitOn);

		// walking: the keys as outside, in the ship's frame
		Keys k = keysFresh ? keys : Keys{};
		keysFresh = false;
		if (seat || cx.busy) k = Keys{};             // in a seat (or getting in / out of it), or at a long action, she does not walk
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
		const double hs = holding ? HeldSlow() : 1.0, runIn = holding && HeldWeight() > 0.6 * CarryLimit() ? walkSpeed : (std::min)(runSpeed, 3.0);
		const double target = !act ? 0 : k.fwd && !k.back ? (k.run ? runIn : walkSpeed) * hs : k.back && !k.fwd ? -0.9 * hs : 0;
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
		// why she does not walk when asked (the user, 2026-10-05: «не двигается экипаж в корабле»): once a second into the log
		if ((k.fwd || k.back || k.stepL || k.stepR) && std::hypot(fwd, lat) < 0.05 && (noWalkLogT -= oapiGetSysStep()) <= 0)
		{
			noWalkLogT = 1.0; double gy = 0; VECTOR3 here = inFeet;
			const bool floorHere = !si->fns.Ground || si->fns.Ground(si->ctx, &here, 0.45, &gy);
			oapiWriteLogV("OrbiterCrew: %s does not walk: seat %d carried %.2f canAct %d shipLets %d (%s) floor here %d at (%.2f %.2f %.2f)",
				name.c_str(), seat, carried, bio.CanAct() ? 1 : 0, shipLets ? 1 : 0, why, floorHere ? 1 : 0, inFeet.x, inFeet.y, inFeet.z);
		}
		// the vessel does not turn with her (the ship's screens are rendered from cameras on this focus body; a turning
		// body made their zones flicker): its frame stays the ship's, her heading is in the pose (Motion heading)
		sv->SetAttachmentParams(inParent, inFeet + si->Origin(), _V(0, 0, 1), _V(0, 1, 0));
		HangOn(sv);

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
		if (inShip && seat) { if (seat == 2) { useShip = inShip; useHint = Tr("встать", "stand up"); } return; }   // in the seat: F - stand up
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
		// a seat: she sits in it herself, and the ship is never given her seat to "take" (that old way took her body
		// out of the world and gave the ship the focus - the user's rule: never)
		if (inShip && useShip == inShip && (useKind == OC_SEAT || useKind == OC_HELM) && seat == 0)
		{
			ShipInterior* si = InteriorOf(inShip);
			// in the suit: not yet (the user, 2026-10-04: the suit comes off first; sitting in it needs its own motion -
			// a future branch). She takes it off herself
			if (suitOn) Say("В скафандре не сесть - снимите скафандр (K)", 1);
			else if (si && si->ext.Seated && si->fns.Seat && Active().ok) SitDown(useId);
			else Say("Сесть нельзя: корабль не даёт этого кресла");
			return true;
		}
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

// two classes in this module: the person's body, and a thing lying about (OrbiterCrew\Item)
DLLCLBK VESSEL* ovcInit(OBJHANDLE hVessel, int fModel)
{
	VESSEL probe(hVessel, fModel);
	const char* cn = probe.GetClassNameA();
	if (cn && std::strcmp(cn, "OrbiterCrew\\Item") == 0) return new ocrew::ItemVessel(hVessel, fModel);
	return new ocrew::CrewMember(hVessel, fModel);
}
DLLCLBK void ovcExit(VESSEL* vessel)
{
	if (ocrew::ItemVessel* it = ocrew::ItemOf(vessel->GetHandle())) { delete it; return; }   // (VESSEL is not polymorphic: by the registry)
	delete static_cast<ocrew::CrewMember*>(vessel);
}

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
	p.where = ocrew::Person::ABOARD; p.vessel = ship; p.place = ocrew::Person::STORED;
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
// a person aboard without a body is given one sitting in the ship's seat 'seatId' (the ship's default roster: the commander
// at the helm), as if she had sat down there: the ship is told (Seated on), the focus and the camera go to the person
extern "C" __declspec(dllexport) int ocSitAt(int id, OBJHANDLE ship, int seatId)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || p->where != ocrew::Person::ABOARD || p->vessel != ship || !oapiIsVessel(ship)) return 0;
	ocrew::ShipInterior* si = ocrew::InteriorOf(ship);
	if (!si || !si->fns.Seat || !si->ext.Seated) return 0;
	VECTOR3 hips{}, dir{ 0, 0, 1 };
	si->fns.Seat(si->ctx, seatId, &hips, &dir);
	VESSEL* sv = oapiGetVesselInterface(ship);
	VESSELSTATUS2 vs; std::memset(&vs, 0, sizeof vs); vs.version = 2;
	sv->GetStatusEx(&vs);
	vs.flag = 0; vs.fuel = nullptr; vs.thruster = nullptr; vs.dockinfo = nullptr; vs.nfuel = vs.nthruster = vs.ndockinfo = 0;
	auto& pi = ocrew::PendingInterior(); pi.ship = ship; pi.feet = hips - _V(0, 0.5, 0); pi.hdg = std::atan2(dir.x, dir.z); pi.seat = seatId;
	p->ship = ship;
	ocrew::Crew::ExpectBody(id);
	OBJHANDLE h = oapiCreateVesselEx(FreeName(p->name).c_str(), p->bodyClass.c_str(), &vs);
	ocrew::Crew::ExpectBody(0); pi = {};
	if (!h) return 0;
	if (ocrew::CrewMember* c = BodyOf(h)) c->takeView = true;
	return 1;
}
// a heavier body (a machine) runs into a person's body: vStrike its speed at the contact relative to her (global),
// normal from it to her. She is thrown, falls, is hurt through what she wears; -> 1 if taken (0: not a body, inside, slow)
// a person inside a machine (ocShipOf == it) is thrown off it where she is, at vGlobal (the machine's velocity before it
// struck): off its post, free, she falls where she lands. -> 1 if thrown
extern "C" __declspec(dllexport) int ocEject(int id, const VECTOR3* vGlobal)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || !vGlobal || p->where != ocrew::Person::INTERIOR) return 0;
	ocrew::CrewMember* c = BodyOf(p->vessel);
	return c && c->Eject(*vGlobal) ? 1 : 0;
}
extern "C" __declspec(dllexport) int ocImpact(OBJHANDLE body, const VECTOR3* vStrike, double strikerMass, const VECTOR3* point, const VECTOR3* normal)
{
	(void)point;
	ocrew::CrewMember* c = BodyOf(body);
	return c && vStrike && normal && c->Struck(*vStrike, strikerMass > 0 ? strikerMass : 1e4, *normal) ? 1 : 0;
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
// a thing into a person's hands, out of them, what she holds (OcHeld; CONTEXT_ACTIONS.md)
extern "C" __declspec(dllexport) int ocGive(int id, const OcHeld* h)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || !h || h->size < static_cast<int>(offsetof(OcHeld, owner)) || (p->where != ocrew::Person::IN_WORLD && p->where != ocrew::Person::INTERIOR)) return 0;
	ocrew::CrewMember* c = BodyOf(p->vessel);
	OcHeld full{}; std::memcpy(&full, h, (std::min)(static_cast<size_t>(h->size), sizeof full)); full.size = sizeof full;   // an older caller: no owner, no data
	return c && c->Give(full) ? 1 : 0;
}
extern "C" __declspec(dllexport) int ocTake(int id, OcHeld* out)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p) return 0;
	ocrew::CrewMember* c = BodyOf(p->vessel);
	if (!c) return 0;
	OcHeld full{};
	if (!c->Take(&full)) return 0;
	if (out && out->size >= static_cast<int>(offsetof(OcHeld, owner))) { const int n = out->size; std::memcpy(out, &full, (std::min)(static_cast<size_t>(n), sizeof full)); out->size = n; }
	return 1;
}
namespace ocrew { bool UiEnglish(); }
extern "C" __declspec(dllexport) const char* ocLanguage() { return ocrew::UiEnglish() ? "en" : "ru"; }

extern "C" __declspec(dllexport) int ocHeldOf(int id, OcHeld* out)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p) return 0;
	ocrew::CrewMember* c = BodyOf(p->vessel);
	if (!c) return 0;
	OcHeld full{};
	if (!c->Held(&full)) return 0;
	if (out && out->size >= static_cast<int>(offsetof(OcHeld, owner))) { const int n = out->size; std::memcpy(out, &full, (std::min)(static_cast<size_t>(n), sizeof full)); out->size = n; }
	return 1;
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
	p->where = ocrew::Person::ABOARD; p->vessel = ship; p->place = ocrew::Person::STORED;
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
// the people aboard that ship without a body live on: the ship's cabin (and its medical bay) every step
extern "C" __declspec(dllexport) void ocStepAboard(OBJHANDLE ship, double dt, const OcCabin* cabin)
{
	if (dt <= 0 || !ship) return;
	OcCabin c{}; c.size = sizeof c; c.p = 101.3; c.T = 294; c.ppO2 = 21.2; c.ppCO2 = 0.04; c.supplied = 1;
	if (cabin && cabin->size > 0) std::memcpy(&c, cabin, (std::min)(static_cast<size_t>(cabin->size), sizeof c));
	for (const auto& up : ocrew::Crew::All())
	{
		ocrew::Person& p = *up;
		if (p.where != ocrew::Person::ABOARD || p.vessel != ship) continue;
		ocrew::Body& b = p.body;
		b.Step(dt, 0, c.ppO2, c.ppCO2, c.p, false, 0);   // at rest, the helmet open: the cabin's air
		if (p.place == ocrew::Person::MEDBAY && c.medHoursPerUnit > 0) b.Treat(dt, { c.medHoursPerUnit, c.medSvPerDay, c.medMaxSv });
		b.Irradiate(dt, c.doseSvh);
		b.Sustain(dt, c.supplied != 0, nullptr, c.T, false);
	}
}
extern "C" __declspec(dllexport) int ocSetPlace(int id, int place)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	if (!p || p->where != ocrew::Person::ABOARD) return 0;
	p->place = static_cast<ocrew::Person::Place>(std::clamp(place, 0, 2));
	return 1;
}
extern "C" __declspec(dllexport) int ocPlaceOf(int id)
{
	ocrew::Person* p = ocrew::Crew::Find(id);
	return p && p->where == ocrew::Person::ABOARD ? static_cast<int>(p->place) : -1;
}
DLLCLBK void ExitModule(HINSTANCE) { ocrew::Crew::Clear(); ocrew::interiors().clear(); }   // the simulation ends: nobody is left
