// OrbiterCrew - the suit computer: a holographic display projected in the helmet, in Russian only.
// Layout (Tantra_Design/suit_computer_mfd.html): data on the visor's rim, thermal control and consumption on the upper
// sides, two multifunction displays low in the corners (left: map, orbit, power, options; right: targets and the
// autopilot - transfer, landing, approach, docking, the body), flight tapes and the target box in the middle.
// Replaces Orbiter's default HUD for the crew member (clbkRenderHUD is suppressed). Text comes from a glyph atlas
// (Jura, SIL OFL) so Cyrillic works on any Windows; the lines use the D3D9 client's extended Sketchpad with alpha and glow.
#pragma once
#include <Orbitersdk.h>
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace ocrew
{
	struct Suit;
	// the suit computer's language (Config\OrbiterCrew\OrbiterCrew.cfg, LANGUAGE): a Russian text in the display's language
	const char* SuitTr(const char* ru);
	bool SuitEnglish();
	// the machine she drives from its post (МПУ, Modules\MPU.dll, mpuDriverState): the same layout as MPU.dll's MpuState
	// (TVChassis.h) field for field, with room behind it should that one grow
	struct MpuState
	{
		double speed, speedMax; int full; double steer, deck, deckMin, deckMax; int brake; double charge;
		double powerW, energyKWh, rangeKm;
		double spare[32];
	};
	struct HudData
	{
		Suit* life{};   // the suit itself (power, loads, equipment, servo, economy): read and switched by the display
		// person
		std::string name, role;
		int state{};                     // 0 ok, 1 unconscious, 2 dead
		bool suit{};
		double pulse{}, breath{}, effort{}, stamina{}, ppO2{}, ppCO2{}, coreC{}, injury{};
		// suit
		double o2{}, o2Hours{}, o2Flow{}, sorbent{}, sorbHours{}, batt{}, battKWh{}, battHours{}, powerW{}, lifeW{}, thermalW{}, driveW{}, lampW{};
		double heatW{};                  // heat moved by the thermal control: + cooling, - heating
		double residualW{};              // what it could not move (0 = holds)
		double envC{}, ratedMinC{}, ratedMaxC{}, groundC{}; bool sunlit{}, inSpec{}, hasGround{}, powered{};
		// outside
		std::string body; double airKPa{}, airC{}, airO2{}; bool breathable{}, vacuum{};
		// suit RCS
		double n2{}, n2Dv{}; int rcs{};  // 0 safe on the ground, 1 ready, 2 no power
		// helmet
		bool shadeDown{}, lampsOn{}, boost{};
		double shade{};                  // the sun shade's position, 0 up .. 1 down
		bool firstPerson{};              // the view is from her eyes (the visor effects apply)
		// jet pack
		bool jet{}, jetFlying{}, jetSurface{}, jetLimited{}, jetProtect{}, jetTerrain{};
		double jetFuel{}, jetDv{}, jetThrottle{}, jetFlow{}, jetTilt[2]{}, jetMaxTilt{}, jetDeploy{}, jetAltHold{}, jetSpd{}, jetVf{}, alt{}, vs{}, gs{}, leanF{}, leanR{};
		double water{}, waterDef{}, bodyMass{ 62 }, fastDays{}, hurt[4]{};   // suit drink bag kg; the body: water short kg, days without food, injuries by part
		bool jetBraking{}, jetFine{};
		int messageLevel{};             // 0 info, 1 caution, 2 danger
		double suitPMax{ 200 }; bool suitBreached{};   // the shell's rated outside pressure, kPa; crushed   // jetFine: the limiter is on
		bool o2Vent{};                  // breathing the air around (breathable): the tank rests               // the full stop is braking
		int jetMode{};                   // 0 manual, 1 height hold, 2 landing
		bool jetAssist{ true };          // false: hands-on, no compensation
		double jetHover{};               // s: how long the fuel left would hold her hovering here
		// autopilot (Autopilot::Mode) and what it says; its commanded acceleration, horizon frame
		int apMode{}; std::string apStatus; VECTOR3 apCmd{};
		// inside the suit, and the wind (Orbiter's own: ground speed minus air speed, horizon frame x east z north)
		double tInC{ 22 }; VECTOR3 wind{};
		// radiation (from the radiation model when present: radValid): dose rate Sv/h, the dose taken Sv; the suit's field
		bool radValid{}; double radRate{}, radDose{}, radCareer{}; bool fieldOn{}; double fieldW{};
		// walking
		bool landed{}, servo{}; double speed{};
		// talk
		std::string warning, message;    // UTF-8, Russian
		double simt{};
	};

	// the display name of a crew member vessel (implemented by CrewMember), empty for other vessels
	std::string CrewDisplayName(OBJHANDLE h);

	// text from a glyph atlas (Textures\Tantra\HudFont.dds + Config\Tantra\HudFont.txt, made by make_hud_font.py)
	class HudText
	{
	public:
		HudText() = default;
		~HudText();                       // the atlas texture is released with it (a second session needs the device clean)
		HudText(const HudText&) = delete; HudText& operator=(const HudText&) = delete;   // one owner of the texture
		bool Load();
		void Release();                  // the texture back (the body leaves the world); Load again later
		bool Ok() const { return tex != nullptr; }
		// size 0 tiny, 1 small, 2 mid, 3 big; colour = atlas colour 0..6; align 0 left, 1 centre, 2 right;
		// (x, y) = pen at the baseline in pixels; scale = pixels per display unit
		// kx, ky: the target surface's pixels per screen pixel (the helmet display's square texture is stretched back on the visor)
		bool Draw(oapi::Sketchpad* skp, double x, double y, const std::string& utf8, int size, int colour, int align, double scale, double kx = 1, double ky = 1) const;
		double Width(const std::string& utf8, int size, double scale) const;   // pixels
	private:
		struct Glyph { int x{}, y{}, w{}, h{}, ox{}, oy{}; float adv{}; };
		const Glyph* Find(int size, int colour, unsigned cp) const;
		SURFHANDLE tex{};
		double unit{ 2 };                // atlas px per display unit
		std::vector<Glyph> glyphs;
		std::vector<std::pair<unsigned long long, int>> index;   // key (size, colour, codepoint) -> glyph, sorted
	};

	class SuitHud
	{
	public:
		~SuitHud();
		// the body it draws on leaves the world: drop what is bound to that body (the IR camera)
		void Detach();
		// the whole display, drawn into a surface of W x H pixels (clbkDrawHUD). The layout is always the viewport's: on the
		// helmet display the surface is Orbiter's square VC HUD texture, stretched back over the view by the visor plate
		void Draw(oapi::Sketchpad* skp, double W, double H, const HudData& d, VESSEL* v);
		void Mouse(double W, double H);        // a fresh left click in Orbiter's window -> Click, in viewport pixels (W, H unused)

		// ---- the helmet display: the suit's HUD is light projected on the visor - Orbiter's own VC HUD (drawn additively,
		// no instruments of Orbiter's), on a plate that rides the head and exactly covers the view ----
		struct HelmetView { VECTOR3 eye; double yaw{}, pitch{}; bool show{}; };   // vessel frame: eye point, gaze yaw/pitch (rad)
		UINT HelmetMesh(VESSEL* v);            // once, before the VC is loaded (clbkSetClassCaps): adds the plate, returns its mesh index
		void RegisterVC();                     // in clbkLoadVC: the plate's group is the VC HUD
		// every step, after the head is aimed; with the display's data the whole HUD is drawn into the light layer
		void HelmetFrame(VESSEL* v, VISHANDLE vis, const HelmetView& hv, const HudData* d = nullptr);
		Suit* life{};                          // the suit of the last frame (the switches act on it)
		UINT plateIdx{ static_cast<UINT>(-1) }; bool onVisor{}; VISHANDLE plateVis{};
		VESSEL* plateOwner{};    // the body the plate was added to (a new body - standing up, coming out - adds its own)
		// the visor's modulator: the photonic layer in the glass that dims the outside light locally (behind the panels, along
		// the visor's edges, over the sun, the whole view when the shade is down) - drawn into its own texture each step
		SURFHANDLE modSrf{}; int modW{}, modH{}; DEVMESHHANDLE modDm{}; bool modLogged{};
		double modKey[7]{}; bool modKeyed{};   // what the modulator drew last: drawn again only when that changes
		// the sun's spot: a small sprite of its own (group 0), set on the sun's line every frame with the plate - it never
		// trails behind a turn of the head; its texture is drawn once (and again only when its density changes)
		SURFHANDLE sunSrf{}; DEVMESHHANDLE sunDm{}; double sunAmp{ -1 };
		void SunSprite(VESSEL* v, DEVMESHHANDLE dm, const VECTOR3& eye, const VECTOR3& r, const VECTOR3& up, const VECTOR3& f, double dist);
		double lastShade{}; bool lastSunlit{};
		// particles through the optics: short sparks and streaks on the image, as dense as the dose rate (the field thins
		// them)
		struct Spark { double x, y, dx, dy, life; };
		std::vector<Spark> sparks; double sparkAcc{}, sparkT{ -1 }; unsigned rng{ 12345 };
		double Rand() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; }   // 0..1
		void Modulator(VESSEL* v);
		// the light layer (trial): our own texture exactly the viewport's size, drawn as light on black and turned into
		// colour + alpha by a pixel shader (Modules\OrbiterCrew\HudLight.hlsl through the client's gcIPInterface) - sharp
		// 1:1, independent of Orbiter's PanelMfdHudSize. Group 2 of the visor plate
		SURFHANDLE lightSrc{}, lightOut{}; int lightW{}, lightH{}; DEVMESHHANDLE lightDm{}; void* ipi{}; bool ipiTried{}, lightLogged{};
		void LightLayer(VESSEL* v, const HudData* d);
		// the cost kept down without losing quality: the display redrawn 30 times a second (at once after a click), the
		// modulator 15; between them the plate shows the last picture and still follows the camera every frame
		double lightT{ -1 }, modT{ -1 }; unsigned clickSeq{}, drawnSeq{};
		// the time it takes, logged every 5 s (ms per redraw; redraws per second)
		double perfDraw{}, perfShader{}, perfMod{}, perfT0{ -1 }; int perfN{}, perfModN{};
		int horizonMode{};       // the pitch ladder: 0 auto (in flight and in space, not on foot), 1 always, 2 off
		int hudScale{ 2 };
		// at a machine's post: its state this frame; the page МАШИНА comes up while she is there, the one before comes back
		MpuState mpu{}; bool onMpu{}; int rpageBeforeMpu{ -1 }; OBJHANDLE mpuPerson{};
		// others' signs on the helmet display (the crew's context menu and its Alt marks): called at the end of every frame
		// the display is drawn, into the same surface - the light layer (or Orbiter's VC HUD while paused). W x H the view in
		// pixels; kx, ky the surface's pixels per view pixel (draw at x * kx, y * ky)
		std::function<void(oapi::Sketchpad* skp, double W, double H, double kx, double ky)> overlay;       // the light layer 2x smoothed (the default, the user's choice) or 1:1 with the screen
		// the display's settings are the user's, not the scenario's: kept for good in Config\OrbiterCrew\SuitDisplay.cfg
		// (look, palette, brightness, the map, the resolution) - read once, written on every change
		bool prefsLoaded{};
		void LoadPrefs();
		void SavePrefs() const;
		void SaveLanguage(bool english) const;   // LANGUAGE in Config\OrbiterCrew\OrbiterCrew.cfg (shared with the crew's menu)
		bool lightLive{}, drawingLight{};   // the HUD goes through the light layer (the VC HUD texture stays empty)
		void PlaceVisor(VESSEL* v);            // the plate over the camera's own frame (position, axes, aperture) as it is now
		void Click(double x, double y);        // a left click in the view, pixels of the HUD surface
		bool mouseWasDown{};
		// requests for the crew member: an autopilot button (AP_*) and the target it applies to; -1 = none
		enum { AP_ALT = 100, AP_LAND = 101, AP_SHADE = 102, AP_LAMP = 103, AP_MANUAL = 104, AP_FIELD = 105, AP_STOP = 106, AP_FINE = 107 };
		int TakeRequest() { const int r = request; request = -1; return r; }
		OBJHANDLE SelectedTarget() const { return sel; }
		int AlarmLevel() const { int l = 0; for (const auto& a : alerts) if (a.active && !a.ack) l = (std::max)(l, a.level); return l; }   // for the alarm tone
		std::string Save() const;              // one scenario line
		void Load(const std::string& line);

		enum Mode { EVA, FLIGHT, RDV, SYS };
		enum LPage { L_LOCAL, L_ORBIT, L_POWER, L_OPTS, L_COUNT };
		enum RPage { R_TARGETS, R_TRANSFER, R_LANDING, R_APPROACH, R_DOCK, R_BODY, R_FLIGHT, R_MACHINE };
		enum { AP_CRUISE = 200 };   // + 0..3: height -10 -1 +1 +10 m; + 10..14: speed -5 -1 stop +1 +5 m/s

		struct Target
		{
			OBJHANDLE h{}; std::string name, kind;
			double dist{}, rate{};             // m; m/s, - closing
			double north{}, east{}, up{};      // m, local horizon at her position (surface)
			VECTOR3 rel{}, relV{};             // her position / velocity relative to the target, global frame
			bool pack{}, crew{}, landed{}, base{};
			int pads{};                         // a base: its landing pads
		};

	// state below is read by the MFD pages (SuitHud.cpp)
		void SetMode(int m, bool manual);
		void Nav(VESSEL* v, const HudData& d);
		void Terrain(VESSEL* v);
		void Profile(VESSEL* v);
		const Target* Selected() const;
		std::vector<RPage> RPages() const;

		// ---- ui state (saved) ----
		int pal{ 1 }, mode{ FLIGHT }, lpage{ L_LOCAL }, rpage{ R_TARGETS }, zoom{ 1 };
		bool autoMode{ true }, openL{ true }, openR{ true };
		// ---- nav state ----
		std::vector<Target> targets;
		OBJHANDLE sel{};
		OBJHANDLE hBody{}; double bodyR{}, lat{}, lng{}, hdg{}, pitch{}, bank{}, sunElev{}, alt{};
		bool surface{}, space{};
		VECTOR3 gsH{};                         // ground speed, horizon frame (x east, y up, z north)
		double lastNav{ -1 }, lastTrail{ -1 };
		std::vector<std::pair<double, double>> trail;   // lat, lng
		// orbit
		bool orbitOk{}, tgtOrbitOk{}; ELEMENTS el{}, tel{}; ORBITPARAM op{}, top{}; double relInc{};
		// terrain around her (north-up), cached
		struct Grid { int n{}; double half{}, lat{}, lng{}, t{ -1 }; std::vector<double> h; } grid;
		Grid next; int nextRow{ -1 };   // the grid being sampled, a few rows per frame
		struct Prof { OBJHANDLE tgt{}; double t{ -1 }, dist{}, brg{}; std::vector<double> h; } prof;
		// history
		struct Trend { double t{ -1 }; std::vector<double> pulse, breath, core; } trend;
		std::vector<std::pair<double, double>> rrHist;  // range, closing rate
		double lastRR{ -1 };

		// what can be clicked, from the last frame (pixels)
		enum HitKind { H_LTAB, H_RTAB, H_LFOLD, H_RFOLD, H_TGT, H_AP, H_PAL, H_ZOOM, H_MODE, H_NVG, H_NEXT, H_BRIGHT, H_LOOK, H_ACK, H_SUIT, H_RES, H_HORIZON, H_MPU, H_LANG };
		struct Hit { double x0, y0, x1, y1; int kind, arg; };
		std::vector<Hit> hits;
		int request{ -1 };
		// infrared view: the D3D9 client's custom camera renders her view into a texture, shown amplified, neutral grey
		// screen brightness against the light around: automatic, or by hand (saved); ambient 0 dark .. 1 bright
		// the display's technology, as it looks: 0 hologram, 1 light-built (a thin coherent line, hatched, no fills), 2 electroluminescent
		bool mapMono{};   // the local map in one colour (off by default: the user's choice)
		int look{ 1 };   // photonics by default (the user's choice); the scenario's HUD line overrides
		// caution & warning: what is wrong now (and was), acknowledged or not; newest first
		struct Alert { std::string key, tile, text; int level{}; double t0{}; bool ack{}, active{}, seen{}; };
		std::vector<Alert> alerts;
		void Alerts(const HudData& d);
		// radiation: dose rate (uSv/h) and the dose of this outing (uSv)
		double doseRate{}, dose{}, lastDoseT{ -1 };
		// short history for the trend arrows: O2, battery, sorbent, CO2, inside temperature (every 5 s, one minute)
		struct Hist { double t{ -1 }; std::vector<double> v[5]; } hist;
		int Trend(int i, double thr) const;
		bool brightAuto{ true }; double brightManual{ 0.6 }, ambient{ 0.5 }, lastAmbT{ -1 };
		// the ranging relief for the IR view: the ground around her out to ~300 m (rings x sectors), from the elevation
		// data - what an active ranger sees where there is no light at all (the night side of Io)
		struct Relief { OBJHANDLE body{}; double lat{}, lng{}, t{ -1 }; int nr{}, na{}; std::vector<double> lat_, lng_, rad_; } relief;
		bool nvg{}, gcTried{}, gcOk{}; SURFHANDLE nvSrf{}; void* nvCam{}; int nvW{}, nvH{}; std::string nvNote;
		void NightVision(oapi::Sketchpad* skp, VESSEL* v, double W, double H, double SW, double SH);   // W x H the view, SW x SH the surface
		HudText glyphs;
		bool glyphsTried{};
		std::map<unsigned long long, oapi::Pen*> pens;       // fallback without the D3D9 client
		std::map<unsigned long long, oapi::Brush*> brushes;
		std::map<int, oapi::Font*> fonts;
		friend class Gfx;
		friend struct Ctx;
		friend void TerrainRows(SuitHud& h, OBJHANDLE body, double bodyR);
	};

	// UTF-8 -> the system code page, for the fallback text
	std::string Ru(const std::string& utf8);
}
