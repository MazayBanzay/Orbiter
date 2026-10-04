// OrbiterCrew - a crew member as an Orbiter vessel: walks, runs, jumps, breathes, tires.
// Controls (focus on her: on the surface and inside a ship):
//   W / S       forward / back; S while moving forward is a hard, friction-limited brake
//   A / D       turn            Q / E   side step
//   right mouse button held, through her eyes: the mouse turns her head; inside a ship, walking, she turns with it
//               and A / D step aside. Released: the cursor is free, as in Orbiter. Seated: Orbiter's classic head
//   left mouse  inside a ship: the ship's buttons and touch screens within her reach
//   Shift       run; in the suit: servo boost (faster, bounding stride, paid from the battery)
//   Space       jump
//   F           the action: what is within reach (an entrance, a seat); in a seat - stand up
//   V           seated at a helm: the ship from outside; V again - her own view
//   K           put on / take off the suit (off only in breathable air)
//   H           the suit computer's display      Shift+V  the helmet sun shade      L  helmet lamps
//   B           take / leave the jet pack (suit on, pack within 2.5 m); its own keys: see JetPack.h
#pragma once
#include "Autopilot.h"
#include "JetPack.h"
#include "LifeSupport.h"
#include "Person.h"
#include "Radiation.h"
#include "Motion.h"
#include "Skin.h"
#include "SuitComputer.h"
#include "SuitHud.h"
#include "Sound.h"
#include "HeadSway.h"
#include <string>
#include <vector>

namespace ocrew
{
	struct ShipInterior;
	class CrewMember : public VESSEL4
	{
	public:
		const std::string& DisplayName() const { return name; }
		Person& Who() { return who; }
		void KeepWorn() { if (n2) who.worn.suit.n2Kg = GetPropellantMass(n2); }   // before the body leaves the world
		~CrewMember();
	public:
		CrewMember(OBJHANDLE hVessel, int fModel);

		void clbkSetClassCaps(FILEHANDLE cfg) override;
		void clbkLoadStateEx(FILEHANDLE scn, void* status) override;
		void clbkSaveState(FILEHANDLE scn) override;
		void clbkPostCreation() override;
		void clbkVisualCreated(VISHANDLE vis, int refcount) override;
		void clbkVisualDestroyed(VISHANDLE vis, int refcount) override;
		int clbkConsumeDirectKey(char* kstate) override;
		int clbkConsumeBufferedKey(DWORD key, bool down, char* kstate) override;
		void clbkPreStep(double simt, double simdt, double mjd) override;
		bool clbkDrawHUD(int mode, const HUDPAINTSPEC* hps, oapi::Sketchpad* skp) override;
		bool clbkLoadGenericCockpit() override;   // none: her eyes are the empty VC, in the suit and out of it
		void clbkRenderHUD(int mode, const HUDPAINTSPEC* hps, SURFHANDLE hTex) override {}   // no default HUD: the suit computer draws
		bool clbkLoadVC(int id) override;

	private:
		struct Figure { UINT mesh{}; Skin skin; ClipSet clips; bool ok{}; };
		struct Keys { bool fwd{}, back{}, left{}, right{}, stepL{}, stepR{}, run{}; };

		Figure& Active() { return suitOn && suitFig.ok ? suitFig : bodyFig; }
		double Mass() const { return bio.mass + who.worn.DryMass(); }   // the propellant: Orbiter adds its tanks
		double Gravity() const;
		// a line for her display; level 1 caution (amber), 2 danger (red)
		void Say(const std::string& text, int level = 0) { message = text; messageLevel = level; messageTime = 6; }
		int messageLevel{};
		void ShowFigure();
		void SetSuit(bool on);
		void Place(bool lying);
		void Drive(double dt, double g);
		Thermal Surroundings() const;
		void Jump();
		void Liftoff();
		void Land();

		// the person this vessel is the body of. The person comes first and lives in the crew registry, not in the
		// vessel; the names below are the person's own data, kept under their old names
		Person& who;
		bool reborn{};                 // the body of a person who already existed (out of a ship): the cfg does not reset him or her
		std::string& name; std::string& role; std::string& sex;
		double& age; double& heightM;
		Figure bodyFig, suitFig;
		Motion motion;
		CrewSound sound;
		oc::HeadSway headSway;               // the eyes on the neck aboard a ship: lag and vibration
		VECTOR3 HeadSwayStep(double dt, const VECTOR3& eye);
		std::string voice{ "female1" };
		VISHANDLE vis{};
		VECTOR3 eye{ 0, 0.69, 0.17 };
		double height{ 0.93 };
		double walkSpeed{ 1.45 }, runSpeed{ 5.0 };

		Body& bio;                     // who.body
		// what the person wears (who.worn): the body only carries its physics; the old names point into it
		bool& suitOn; bool& suitFromScenario; double& suitMass;
		Suit& suit;
		JetPack& jet;
		SuitHud& hud;
		Autopilot& ap;
		Air air;
		Radiation rad;
		RadEnv radEnv;
		Thermal thermal;
		bool boost{};                        // servo boost (suit, live drives, Shift)
		double humanW{}, driveDemandW{};     // who pays for the movement: her muscles / the drives

		Keys keys;
		bool keysFresh{};
		double fwd{}, lat{}, turn{}, accel{};
		bool airborne{}, lying{}, placed{};
		double settleT{}, diagT{};   // next check of the feet against the ground while standing still
		double fallSpeed{}, landingSpeed{}, jumpHeading{};

		// suit RCS: SAFER-class cold-gas unit in the pack (self-rescue, ~10 m/s); off on the ground and without power
		PROPELLANT_HANDLE n2{};
		std::vector<THRUSTER_HANDLE> rcs;
		bool rcsLive{};
		RcsSet rcsN2;
		int rcsState{ -1 };                  // live / which set, to switch only on change
		void BuildAttGroups(const RcsSet& set);

		// jet pack
		bool& jetFromScenario;
		bool wasFree{};
		double freeVy{}, freeT{};
		FlightInput flight{};
		bool flightFresh{};
		void Touchdown();
		void GroundContactCheck(double dt);
		void Fall(double impact, const char* why, Body::Contact c = Body::ON_BACK);
		double fallenT{};
		void TakePack();
		void DropPack();
		double ShellHalf() const { return jet.ShellHalfHeight(); }
		void SetupRcs();
		void UpdateRcs(bool free);
		double RcsDeltaV() const;
		bool hudHidden{};   // we switched Orbiter's HUD off (no suit, generic cockpit)
		bool wasInHelmet{}; // suit on and the camera in her head last step (the suit computer is brought up on entering)
		std::string message;
		double messageTime{};
		// the suit computer's display (variant 3, the user and «Архитектор», 2026-10-04): the VC HUD on the helmet plate
		// (SuitHud::HelmetFrame), drawn through clbkDrawHUD; only in the suit, H switches it
		bool hudOn{ true };
		bool debugLog{};                     // DebugLog = 1 in her config: test lines in Orbiter.log (clicks, the head camera)
		void DrawSuitHud(oapi::Sketchpad* skp, DWORD W, DWORD H);
		// F - the action (user 2026-10-03: F everywhere): what is within reach - a ship's lift or airlock, later
		// a seat, a terminal, a door inside
		OBJHANDLE useShip{}; int useId{ -1 }, useKind{}; std::string useHint; double useScan{};
		void FindUse(double dt);
	public:
		// inside a ship (user 2026-10-03: the person walks inside with her own body): the body hangs on the ship's
		// attachment point, which we move each frame; the ship gives floors, walls and items (OcInterior)
		OBJHANDLE inShip{};
		ATTACHMENTHANDLE inParent{}, inChild{};
		VECTOR3 inFeet{};                    // feet, in the ship's frame
		double inHdg{};                      // heading about the ship's up axis (0 = along +z)
		bool inViewing{};
		// a seat in the ship: 0 standing, 1 sitting down, 2 seated, 3 standing up, 4 the seat moving back before she rises
		int seat{}, seatId{ -1 };
		double seatT{}, seatStill{};
		VECTOR3 seatFeet{}, seatFrom{};
		double seatHdg{}, seatHdgFrom{};
		// the mouse (the user, 2026-10-03): right button held, through her eyes - it turns her head; inside a ship,
		// walking, she turns with it (mouseMode) and A/D step aside. Released - the cursor is free, A/D turn her
		static constexpr bool kMouseWalk = true;
		bool mouseMode{}, mouseRmb{};
		bool seatHelm{};                     // the seat she sits in is a helm (OC_HELM): V works
		bool shipView{};                     // V: the camera on the ship from outside
		void ShipView();
		bool faceSun{}, sunLogged{};          // FACESUN 1: she turns to the Sun at the start
		int orbitCal{};                      // external camera: 0 not yet, 1 probing, 2 known, -1 failed
		double orbitY0{}, orbitP0{}, orbitKa{ 1 }, orbitKp{ 1 };
		double mouseTurnBy{};
		// her head (2026-10-04, variant 3): the person's own look - the neck, the body that follows it, the seat - is ours.
		// What Orbiter's camera turned since the last step (the mouse, right button) is the user's look given to her
		// head; then the camera is set where her head looks, every step (oapiCameraSetCockpitDir)
		double headYaw{}, headPitch{};       // rad, from her body's facing (yaw + to the right, pitch + up)
		double headWantYaw{}, headWantPitch{}, headCheckT{};   // what the camera was asked to show (the check in the log)
		bool headAsked{}, headStepped{};
		VECTOR3 camEye{};                    // the camera's point (her eye), vessel frame
		double HeadStep(double dt, bool canTurn);   // the mouse with the right button: -> the body's turn it asks for
		void AimHead(double dt);                      // the camera where her head looks
		void ClampToShips(VESSELSTATUS2& s);          // outside: her step against the ships' outer solids
		bool outerLogged{};                           // "stopped by the outer solids" written once
		bool CalibrateOrbit();
		static constexpr bool kSeatClips = false;   // sitting-down / getting-up motion: off for now (the user, 2026-10-03: later, cosmetics)
		static constexpr double seatStartZ = 0.318, seatStandZ = 0.295;   // sit_down starts / stand_up ends standing this far in front of the seat (export_sit.log)
		void SitDown(int id);
		bool SeatPlace(int id);
		void StandFromSeat();
		int SeatState() const { return seat; }
		bool lmbWas{};
		void ClickInside(ShipInterior& si);   // a mouse click on a button of the ship (OC_BUTTON)
		double carried{};                    // seconds left of being carried by the ship (ocCarry): no walking
		int attachWait{};                    // frames a new body waits before it is attached
		bool takeView{};                     // take the focus and the person's own view at the next step
		void ApplyView();
		std::string inShipName;              // from the scenario, found again after loading
		void EnterShip(OBJHANDLE ship, const VECTOR3& feet, const VECTOR3& dir);
		void HangOn(VESSEL* sv);             // on the ship's interior point (and on no other point of it)
		void LeaveShip();                    // detached, back in the world (where she is now)
		void InteriorStep(double dt);
		void Animate(double dt, double g, bool landed);
		void LookDir(double& yaw, double& pitch);
		void HudBySuit();                    // the user's rule: no HUD without the suit, the suit computer in the helmet
	private:
		bool DoUse();
		void ApRequest(int req);
		// what of an impact reaches the body: in the suit, through its frame and dampers (Suit::ImpactThrough)
		void HitBody(double v, Body::Contact c) { bio.Impact(suitOn ? Suit::ImpactThrough(v, bio.mass, who.worn.Mass(), c) : v, c); }
		double LieHeight() const { return jet.Worn() ? 0.36 : 0.18; }
		// how far a base's landing pad under her stands above the relief there (0 = none): bases with
		// MapObjectsToSphere put the pads on the smooth sphere, which can be above the real ground
		double PadLift() const;   // her back (or the pack) on the ground, not under it
		void StandUp();
		double suitResidual{};   // heat the suit could not move, W (+ in, - out)

		// helmet: sun shade (V) and lamps (L)
		double shade{}, shadeTarget{};        // 0 raised over the crown .. 1 lowered over the visor
		bool lampOn{};
		SpotLight* lamps[2]{};
		BEACONLIGHTSPEC lampGlow[2]{};          // the lamp glass itself, glowing when the lamps are on
		VECTOR3 lampGlowPos[2]{}, lampGlowCol{ 1.0, 0.96, 0.85 };
		void UpdateHelmet(double dt, Figure& fig);
	};
}
