// OrbiterCrew - the suit computer: a holographic display projected in the helmet, in Russian only.
// Layout (Tantra_Design/suit_computer_mfd.html): data on the visor's rim, thermal control and consumption on the upper
// sides, two multifunction displays low in the corners (left: map, orbit, power, options; right: targets and the
// autopilot - transfer, landing, approach, docking, the body), flight tapes and the target box in the middle.
// Replaces Orbiter's default HUD for the crew member (clbkRenderHUD is suppressed). Text comes from a glyph atlas
// (Jura, SIL OFL) so Cyrillic works on any Windows; the lines use Sketchpad2 (D3D9 client) with alpha and glow.
#pragma once
#include <Orbitersdk.h>
#include <string>
#include <vector>
#include <map>

namespace ocrew
{
	struct HudData
	{
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
		// jet pack
		bool jet{}, jetFlying{}, jetSurface{}, jetLimited{}, jetProtect{}, jetTerrain{};
		double jetFuel{}, jetDv{}, jetThrottle{}, jetFlow{}, jetTilt[2]{}, jetMaxTilt{}, jetDeploy{}, jetAltHold{}, alt{}, vs{}, gs{}, leanF{}, leanR{};
		int jetMode{};                   // 0 manual, 1 height hold, 2 landing
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
		bool Load();
		bool Ok() const { return tex != nullptr; }
		// size 0 tiny, 1 small, 2 mid, 3 big; colour = atlas colour 0..6; align 0 left, 1 centre, 2 right;
		// (x, y) = pen at the baseline in pixels; scale = pixels per display unit
		bool Draw(oapi::Sketchpad* skp, double x, double y, const std::string& utf8, int size, int colour, int align, double scale) const;
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
		void Draw(oapi::Sketchpad* skp, const HUDPAINTSPEC* hps, const HudData& d, VESSEL* v);
		bool Key(DWORD key);                   // the computer's keys; true = used
		std::string Save() const;              // one scenario line
		void Load(const std::string& line);

		enum Mode { EVA, FLIGHT, RDV, SYS };
		enum LPage { L_LOCAL, L_ORBIT, L_POWER, L_OPTS, L_COUNT };
		enum RPage { R_TARGETS, R_TRANSFER, R_LANDING, R_APPROACH, R_DOCK, R_BODY };

		struct Target
		{
			OBJHANDLE h{}; std::string name, kind;
			double dist{}, rate{};             // m; m/s, - closing
			double north{}, east{}, up{};      // m, local horizon at her position (surface)
			VECTOR3 rel{}, relV{};             // her position / velocity relative to the target, global frame
			bool pack{}, crew{}, landed{};
		};

	private:
		void SetMode(int m, bool manual);
		void Nav(VESSEL* v, const HudData& d);
		void Terrain(VESSEL* v);
		void Profile(VESSEL* v);
		const Target* Selected() const;
		std::vector<RPage> RPages() const;

		// ---- ui state (saved) ----
		int pal{ 2 }, mode{ FLIGHT }, lpage{ L_LOCAL }, rpage{ R_TARGETS }, zoom{ 1 };
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
		struct Prof { OBJHANDLE tgt{}; double t{ -1 }, dist{}, brg{}; std::vector<double> h; } prof;
		// history
		struct Trend { double t{ -1 }; std::vector<double> pulse, breath, core; } trend;
		std::vector<std::pair<double, double>> rrHist;  // range, closing rate
		double lastRR{ -1 };

		HudText glyphs;
		bool glyphsTried{};
		std::map<unsigned long long, oapi::Pen*> pens;       // fallback without Sketchpad2
		std::map<unsigned long long, oapi::Brush*> brushes;
		friend class Gfx;
	};

	// UTF-8 -> the system code page, for the fallback text
	std::string Ru(const std::string& utf8);
}
