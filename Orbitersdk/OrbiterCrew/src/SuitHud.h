// OrbiterCrew - the suit computer: a holographic display projected in the helmet, in Russian only.
// Layout (Tantra_Design/suit_computer_mfd.html): data on the visor's rim, thermal control and consumption on the upper
// sides, two multifunction displays low in the corners (left: map, orbit, power, options; right: targets and the
// autopilot - transfer, landing, approach, docking, the body), flight tapes and the target box in the middle.
// Replaces Orbiter's default HUD for the crew member (clbkRenderHUD is suppressed). Text comes from a glyph atlas
// (Jura, SIL OFL) so Cyrillic works on any Windows; the lines use the D3D9 client's extended Sketchpad with alpha and glow.
#pragma once
#include <Orbitersdk.h>
#include "D3D11Hud.h"
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace ocrew
{
	struct Suit;
	class Frame;
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
		Frame* frame{}; // the amplifier frame when worn: «СОЕДИНЕНИЯ» reads its cassette and switches its link to the suit
		double liftKg{}, heldKg{};       // what she lifts now (the frame's, the suit's, her own) and the weight she holds
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
		int lampMode{ 1 };               // the lamps' beam: 0 flood, 1 work, 2 spot
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
		// the pack's working state, always on the display: which pack (0 the light «Наплечный», 1 the impeller «Вихрь»), the
		// turbine running, the rotors' speed, the rocket share (boost / pods), the turbine's power left by the heat, the
		// share of the full thrust the fans can give in this air
		int jetModel{}; bool jetTurbine{};
		double jetFanLvl{}, jetRocket{}, jetHeat{ 1 }, jetFanAuth{}; bool jetForsazh{}; int jetChFault{};
		// autopilot (Autopilot::Mode) and what it says; its commanded acceleration, horizon frame
		int apMode{}; std::string apStatus; VECTOR3 apCmd{};
		bool apGoal{}; double apGoalLat{}, apGoalLng{};   // ПЕРЕЛЁТ's set-down point (conformal pad and gates)
		// inside the suit, and the wind (Orbiter's own: ground speed minus air speed, horizon frame x east z north)
		double tInC{ 22 }; VECTOR3 wind{};
		// radiation (from the radiation model when present: radValid): dose rate Sv/h, the dose taken Sv; the suit's field
		bool radValid{}; double radRate{}, radDose{}, radCareer{}; bool fieldOn{}; double fieldW{};
		// walking
		bool landed{}, servo{}; double speed{};
		// the Pantsir's view (CrewMember fills it): the camera it shows (-1 left wide, 0 stereo, +1 right wide), the window
		// open (the shutter up: her eyes through the glass), the shutter closed by the suit itself and why
		int pzCam{}; bool pzWindow{}, pzAutoShut{}; std::string pzShutReason;
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
		struct HelmetView { VECTOR3 eye; double yaw{}, pitch{}; bool show{}, glass{}; bool helm{}; VECTOR3 hX{}, hY{}, hZ{}, hO{}; bool ext{}; };   // hO: the helmet's eye point (vessel frame); ext: the glass layer seen from outside (the light suit worn, the helmet closed)   // glass: the closed visor is before her eyes; helm: the head turns inside a helmet fixed to the body - its axes (vessel frame: right, up, forward)   // glass: the closed visor is before her eyes   // vessel frame: eye point, gaze yaw/pitch (rad)
		UINT HelmetMesh(VESSEL* v);            // once, before the VC is loaded (clbkSetClassCaps): adds the plate, returns its mesh index
		void RegisterVC();                     // in clbkLoadVC: the plate's group is the VC HUD
		// every step, after the head is aimed; with the display's data the whole HUD is drawn into the light layer
		void HelmetFrame(VESSEL* v, VISHANDLE vis, const HelmetView& hv, const HudData* d = nullptr);
		Suit* life{};                          // the suit of the last frame (the switches act on it)
		Frame* frame{};                        // the frame of the last frame (the link switch acts on it)
		UINT plateIdx{ static_cast<UINT>(-1) }; bool onVisor{}; VISHANDLE plateVis{};
		VESSEL* plateOwner{};    // the body the plate was added to (a new body - standing up, coming out - adds its own)
		// the visor's modulator: the photonic layer in the glass that dims the outside light locally (behind the panels, along
		// the visor's edges, over the sun, the whole view when the shade is down) - drawn into its own texture each step
		SURFHANDLE modSrf{}; int modW{}, modH{}; DEVMESHHANDLE modDm{}; bool modLogged{};
		double modKey[11]{}; bool modKeyed{};
		int alertLines{};
		int lastPzCam{ -9 }; double camSwitchT{ -1 };
		// the helmet the head turns inside (HelmetView = TRUE): its axes in the vessel frame, this frame. The MFDs and the
		// lower plates are set on the helmet's glass then, not on the view - the look slides across them
		bool helmOk{}; VECTOR3 helmX{}, helmY{}, helmZ{};
		double helmPx[2]{};
		// ---- the glass layer: the MFDs and the lower plates drawn on the visor itself (the user, 2026-10-20: «строго на
		// стекле, повторяя его форму»). Meshes\Tantra\VisorGlassHud.msh (Tantra_Design\blender\make_visor_glass_hud.py): the
		// visor's own geometry, 1.5 mm in, its texture coordinates seen from the eye - an angular picture (yaw, pitch) the
		// panels are drawn into (the glass canvas). Group 0 seen from inside, group 1 from outside (dim). Carried with the
		// helmet each step; the panels are then fixed on the glass whatever the view's angle or zoom
		bool glassWanted{}; VECTOR3 glassEye{}; UINT glassIdx{ static_cast<UINT>(-1) }; VESSEL* glassOwner{}; MESHHANDLE glassTpl{};
		std::vector<VECTOR3> glassRest; D3D11Hud::Canvas glassCv{}; DEVMESHHANDLE glassMeshDm{}; int glassShown{ -1 }; bool glassPass{}, glassLive{};
		void GlassSource(VESSEL* v, const VECTOR3& eyeRest);   // the suit has the glass layer (HelmetView): its mesh added now, with the vessel's others
		bool glassLogged{};
		bool mfdSide{}, hideLadder{}, hudMin{}, glassExtPass{}; double glassExtT{ -1 };   // glassExtPass: the glass drawn for the outside view   // the MFDs at the sides; the pitch ladder hidden; the HUD down to its top arc
		HelmetView lastHv{};   // the helmet of the last step: PlaceVisor carries the glass with the camera (the moment the view is drawn)        // the MFDs on the glass: 0 in front (the corners), 1 at the sides (by the cheeks) - SuitDisplay.cfg MFD_SIDE
		LightEmitter* glassGlow{};   // the glass seen from outside: a small light of its own
		void GlassFrame(VESSEL* v, VISHANDLE vis, const HelmetView& hv);
		bool GlassOn() const { return glassLive && glassCv && helmOk; }
		struct GlassLine { std::string text; int col; double y, size; };
		std::vector<GlassLine> glassLines;   // the lines low on the glass (the flight module, the autopilot, messages): collected in the view's pass, drawn on the glass   // the helmet's forward in the view, pixels from the centre (the modulator's window follows it)
		// a direction fixed in the helmet (yaw right, pitch up, rad) in camera space (x right, y down, z forward), with the
		// panel's right and down axes facing the eye: out[0..2] centre, [3..5] right, [6..8] down. False: no helmet frame
		bool HelmDir(VESSEL* v, double yaw, double pitch, double out[9]) const;
		// the glow of MFD i (0, 1) on the helmet's lining: where it sits (yaw right, pitch up, rad, in the helmet), its colour
		// (sRGB 0..1) and level 0..1. False: no light (folded, not «Буран», the display dark)
		bool MfdGlow(int i, double& yaw, double& pitch, double rgb[3], double& level) const;   // the Pantsir's camera view: the camera shown last, when it changed (a flash)   // «Буран»: the lines of dangers and messages under the lamps (the modulator darkens the glass behind them)   // what the modulator drew last: drawn again only when that changes
		// the sun's spot: a small sprite of its own (group 0), set on the sun's line every frame with the plate - it never
		// trails behind a turn of the head; its texture is drawn once (and again only when its density changes)
		SURFHANDLE sunSrf{}; DEVMESHHANDLE sunDm{}; double sunAmp{ -1 };
		// the visor's filter over the sun (D3D11, group 0's flag 0x200): grey (neutral, 10^-4.5) or H-alpha (the chromosphere,
		// filaments, prominences); the user's setting (SuitDisplay.cfg SUN_FILTER), applied to the group when it changes
		bool sunHAlpha{}; int sunHAlphaSet{ -1 };
		// the glass before the eyes (group 0's flag 0x400, for the client's drops on the visor - VisorDrops.cpp): the suit
		// on, the helmet closed, her own view
		int glassSet{ -1 }; DEVMESHHANDLE glassDm{};
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
		// the D3D11 client's own drawing (D3D11Hud.h): the light layer, the modulator and the sun's spot are GPU canvases
		// drawn directly - no Sketchpad, no image-processing shader. Under another client the table is absent (null) and
		// everything above is used as it was
		const D3D11Hud::Api* gx{}; bool gxTried{};
		D3D11Hud::Canvas lightCv{}, modCv{}, sunCv{}, drawCv{};   // drawCv: the canvas Draw() draws into now (else its Sketchpad)
		D3D11Hud::Font sdf[2]{};                                  // the display's font as signed distances: Jura 600, 700
		const D3D11Hud::Api* Native();                            // the table, the fonts loaded once; null: the Sketchpad path
		void NightVisionNative(VESSEL* v, double W, double H, double SW, double SH);
		// the cost kept down without losing quality: the display redrawn 30 times a second (at once after a click), the
		// modulator 15; between them the plate shows the last picture and still follows the camera every frame
		double lightT{ -1 }, modT{ -1 }; unsigned clickSeq{}, drawnSeq{};
		// the time it takes, logged every 5 s (ms per redraw; redraws per second)
		double perfDraw{}, perfShader{}, perfMod{}, perfT0{ -1 }; int perfN{}, perfModN{};
		int horizonMode{};       // the pitch ladder: 0 auto (in flight and in space, not on foot), 1 always, 2 off
		bool synth{ true };      // «СИНТЕТИКА»: the terrain's contour lines drawn on the ground by the AtmoFX client
		int hudMarks[8]{};       // the conformal markers now set in the client (0 none, else the type + 1)
		void ConformalHud(VESSEL* v, const HudData& d);
		int hudScale{ 2 };
		// at a machine's post: its state this frame; the page МАШИНА comes up while she is there, the one before comes back
		MpuState mpu{}; bool onMpu{}; int rpageBeforeMpu{ -1 }; OBJHANDLE mpuPerson{};
		// others' signs on the helmet display (the crew's context menu and its Alt marks): called at the end of every frame
		// the display is drawn, into the same surface - the light layer (or Orbiter's VC HUD while paused). W x H the view in
		// pixels; kx, ky the surface's pixels per view pixel (draw at x * kx, y * ky)
		std::function<void(oapi::Sketchpad* skp, double W, double H, double kx, double ky)> overlay;
		std::function<void(const D3D11Hud::Api* gx, D3D11Hud::Canvas cv, double W, double H, double kx, double ky)> overlayNative;   // the same signs on the native canvas       // the light layer 2x smoothed (the default, the user's choice) or 1:1 with the screen
		// the display's settings are the user's, not the scenario's: kept for good in Config\OrbiterCrew\SuitDisplay.cfg
		// (look, palette, brightness, the map, the resolution) - read once, written on every change
		bool prefsLoaded{};
		void LoadPrefs();
		void SavePrefs() const;
		void SaveLanguage(bool english) const;   // LANGUAGE in Config\OrbiterCrew\OrbiterCrew.cfg (shared with the crew's menu)
		bool lightLive{}, drawingLight{};   // the HUD goes through the light layer (the VC HUD texture stays empty)
		void PlaceVisor(VESSEL* v);
		void HideVisor(VESSEL* v);
		static constexpr double MOD_MARGIN = 1.3;   // the modulator's plate against the view (its picture in the inner part)            // the plate over the camera's own frame (position, axes, aperture) as it is now
		void Click(double x, double y);        // a left click in the view, pixels of the HUD surface
		bool mouseWasDown{};
		// requests for the crew member: an autopilot button (AP_*) and the target it applies to; -1 = none
		enum { AP_ALT = 100, AP_LAND = 101, AP_SHADE = 102, AP_LAMP = 103, AP_MANUAL = 104, AP_FIELD = 105, AP_STOP = 106, AP_FINE = 107, AP_LAMPMODE = 108, AP_HELMET = 109 };
		int TakeRequest() { const int r = request; request = -1; return r; }
		OBJHANDLE SelectedTarget() const { return sel; }
		int AlarmLevel() const { int l = 0; for (const auto& a : alerts) if (a.active && !a.ack) l = (std::max)(l, a.level); return l; }   // for the alarm tone
		std::string Save() const;              // one scenario line
		void Load(const std::string& line);

		enum Mode { EVA, FLIGHT, RDV, SYS };
		enum LPage { L_LOCAL, L_ORBIT, L_POWER, L_RES, L_OPTS, L_COUNT };
		enum RPage { R_TARGETS, R_TRANSFER, R_LANDING, R_APPROACH, R_DOCK, R_BODY, R_FLIGHT, R_MACHINE, R_LINKS };
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
		// the light suit's layout «Буран» (2026-10-20; the one before it stays in the code as legacy, not drawn): the two MFDs
		// at eye level in the upper corners, the reserves' counters low left, the suit's mechanization low right, all four
		// as flat plates turned to the eye; the brow's analogue scales; the journal folded, opening by itself on a warning.
		// Its themes (SuitDisplay.cfg HUD_THEME): 0 laser (green), 1 projector (cyan, soft glow), 2 augmented reality (white
		// and blue, flat), 3 high contrast (orange)
		int hudTheme{};
		double shadeK{ 0.1 };   // the visor's shading behind the display, 0 .. 2 (SuitDisplay.cfg SHADE)
		bool logOpen{}; int logPage{}; double logShowT{ -1 }; bool logUnacked{};
		void BuranRects(double W, double H, double r[4][4]) const;   // the four plates in view pixels: x0, y0, x1, y1
		int pzTab{ 1 };   // the heavy suit's own panel «ПАНЦИРЬ»: 0 data, 1 radiator, 2 drives, 3 field, 4 damage
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
		enum HitKind { H_LTAB, H_RTAB, H_LFOLD, H_RFOLD, H_TGT, H_AP, H_PAL, H_ZOOM, H_MODE, H_NVG, H_NEXT, H_BRIGHT, H_LOOK, H_ACK, H_SUIT, H_RES, H_HORIZON, H_MPU, H_LANG, H_LINK, H_PZTAB, H_SYNTH,
			H_LOG, H_LOGPG, H_SUNF, H_THEME, H_SHADEK, H_MFDSIDE, H_LADDER, H_HUDMIN };
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
		std::string alertSig;            // the raised ones as last logged
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
		bool nvg{}, gcTried{}, gcOk{}; SURFHANDLE nvSrf{}; void* nvCam{}; int nvW{}, nvH{}, nvTick{}; std::string nvNote;
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
