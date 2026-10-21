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
#include "Frame.h"
#include "LifeSupport.h"
#include "Person.h"
#include "Radiation.h"
#include "Motion.h"
#include "../include/OrbiterCrewApi.h"
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
	// a thing lying about: what a person put down (X) or threw (Shift+X); F takes it again (OrbiterCrew\Item, this module)
	class ItemVessel : public VESSEL4
	{
	public:
		ItemVessel(OBJHANDLE h, int fm);
		~ItemVessel();
		void clbkSetClassCaps(FILEHANDLE cfg) override;
		void clbkLoadStateEx(FILEHANDLE scn, void* status) override;
		void clbkSaveState(FILEHANDLE scn) override;
		void clbkPostCreation() override;
		OcHeld held{};
	private:
		void Shape();
		UINT mesh{ static_cast<UINT>(-1) }; MESHHANDLE tpl{};
	};
	ItemVessel* ItemOf(OBJHANDLE h);
	class CrewMember : public VESSEL4
	{
	public:
		const std::string& DisplayName() const { return name; }
		Person& Who() { return who; }
		// struck by a heavier body (a machine running into her, ocImpact): thrown along n, down, hurt; false if not taken
		bool Struck(const VECTOR3& vStrikeGlobal, double strikerMass, const VECTOR3& nGlobal);
		// thrown off the machine she is on (ocEject): free, at that global velocity; she falls where she lands
		bool Eject(const VECTOR3& vGlobal);
		bool thrown{};
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
		void clbkRCSMode(int mode) override;    // the N2 RCS: rotation / translation, said in her line
		void clbkPreStep(double simt, double simdt, double mjd) override;
		bool clbkDrawHUD(int mode, const HUDPAINTSPEC* hps, oapi::Sketchpad* skp) override;
		bool clbkLoadGenericCockpit() override;   // none: her eyes are the empty VC, in the suit and out of it
		void clbkRenderHUD(int mode, const HUDPAINTSPEC* hps, SURFHANDLE hTex) override {}   // no default HUD: the suit computer draws
		bool clbkLoadVC(int id) override;

	private:
		struct Figure { UINT mesh{}; Skin skin; ClipSet clips; bool ok{}; };
		struct Keys { bool fwd{}, back{}, left{}, right{}, stepL{}, stepR{}, run{}; };

		Figure& Active() { return suitOn && suitFig.ok ? suitFig : bodyFig; }
		double Mass() const { return bio.mass + who.worn.DryMass() + (holding ? held.massKg : 0); }   // the propellant: Orbiter adds its tanks; + what she holds
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
		LightEmitter* mfdLight[2]{};   // the MFDs' glow on the helmet's lining (cockpit-only point lights, the D3D11 client)
		Motion motion;
		CrewSound sound;
		oc::HeadSway headSway;               // the eyes on the neck aboard a ship: lag and vibration
		VECTOR3 HeadSwayStep(double dt, const VECTOR3& eye);
		VECTOR3 swayV{}, swayA{}; bool swayVOk{};   // the ship's velocity last step (to the surface), its measured acceleration
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
		Frame& frame;                  // the amplifier frame (who.worn.frame)
		bool frameFlatSaid{};
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
		// molten lava under a crust that cannot hold her (Config\<body>\LavaField, HEAVY_SUIT.md): how deep her feet are in
		// the melt now and where they settle (buoyancy: lava ~2600 kg/m3 against ~1100 - she does not go under)
		double sink{}, sinkTarget{}; int lastFootfalls{};
		void TakePack(OBJHANDLE which = nullptr);   // which: that one (F at it), else the nearest within reach
		void DropHeld(bool throwIt);                // X: what she holds put down before her; Shift+X: thrown
		void DoffFrame();                           // X with empty hands: the amplifier frame off, standing before her
		bool DonFrame(const OcHeld& h);             // F at a standing frame: on (its cassette in h.data)
		void DropPack();
		double ShellHalf() const { return jet.ShellHalfHeight(); }
		void SetupRcs();
		// Titan (2026-10-18, the Titan session's plan): cold dense air without oxygen - the soles puff up the damp organic
		// sand (low, slow, settling in seconds); a valve's vent (water or gas) flash-freezes into ice crystals there and in
		// vacuum. IceVent: model frame, kgps the vent's flow, for seconds (the suit's purge / emergency opening calls it)
		bool titanEnv{};
		PSTREAM_HANDLE stepDust{}, iceStream{};
		double stepDustLvl{}, stepPuff{}, iceLvl{}, iceT{};
		double stepKick{}; bool stepJetOn{};
		// wading in a sea (Titan's methane): the liquid's depth over the ground under her, its level per body (the client's
		// AtmoFX_LiquidLevel), the wake nodes last sent, the foot that steps next
		double liqDepth{}, liqLevel{}; std::string liqBody; bool liqHas{};
		double wakeSent[3]{ -1, -1, -1 }, wadePrevPuff{}, wadeImpT{}; int wadeFoot{};
		void UpdateWading(double dt);   // the footfall's puff through the AtmoFX client: the boot's down speed, the "jet" running
		void IceVent(const VECTOR3& pos, const VECTOR3& dir, double kgps, double seconds);
		void UpdateTitanFx(double dt);
		// what the suit's N2 thrusters leave in the air, through the AtmoFX client's free particles (AtmoFXPart): nothing in
		// vacuum (clear nitrogen), a CO2-frost puff on Mars, a faint water wisp on Earth; on Titan the jet is warmer than the
		// air - no condensate, the air shimmers over the nozzle (AtmoFXHeat). The ice vent's crystals the same way. Orbiter's
		// particle streams (their ground shadows the client draws as squares) only without the client
		struct RcsFx { THRUSTER_HANDLE th; VECTOR3 pos, dir; bool heat; };
		std::vector<RcsFx> rcsFx;
		VECTOR3 icePos{}, iceDir{}; double iceKgps{};
		void UpdateRcsFx();
		void UpdateRcs(bool free);
		double RcsDeltaV() const;
		bool hudHidden{};   // we switched Orbiter's HUD off (no suit, generic cockpit)
		bool wasInHelmet{}; // suit on and the camera in her head last step (the suit computer is brought up on entering)
		std::string message;
		double messageTime{};
		// the suit computer's display (variant 3, the user and «Архитектор», 2026-10-04): the VC HUD on the helmet plate
		// (SuitHud::HelmetFrame), drawn through clbkDrawHUD; only in the suit, H switches it
		bool hudOn{ true };
		double handLogT{};                   // the seated hands' test line (DebugLog)
		bool ctxTest{};                      // CtxTest = 1 in her config: the caption and a gauge row always shown (a drawing test)
		bool debugLog{};                     // DebugLog = 1 in her config: test lines in Orbiter.log (clicks, the head camera)
		void DrawSuitHud(oapi::Sketchpad* skp, DWORD W, DWORD H);
		void FillHudData(HudData& d);
		// F - the action (user 2026-10-03: F everywhere): what is within reach - a ship's lift or airlock, later
		// a seat, a terminal, a door inside
		OBJHANDLE useShip{}; int useId{ -1 }, useKind{}; std::string useHint; double useScan{};
		bool useFallback{};   // the exit offered from anywhere aboard (OcInteriorExt::ExitAnywhere): context actions go first
		void FindUse(double dt);
		bool AimsAtDoor(OBJHANDLE ship, const VECTOR3& shipPos, const VECTOR3& shipNormal) const;   // OC_DOORWAY: her look in its opening
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
		// the constant look (CONTEXT_ACTIONS.md «Мышь», the user's decision 2026-10-07): on foot and at a standing post,
		// through her eyes, the mouse always turns the look - the cursor hidden and held in the middle, an aim dot there;
		// Alt frees the cursor (the menu's M-1, buttons, Orbiter's menus and dialogs). In a seat: the cursor, the look by the
		// right button, as before
		// the free look (the user, 2026-10-07): on foot, out of a machine's control, the right button turns the head alone;
		// let go, the look comes back to where it was before it
		bool freeLook{}, lookBack{}; double freeYaw0{}, freePitch0{};
		bool lookCapture{}, lookWas{}, cursorHidden{}, simCursor{}; double lookHintT{}; bool lookHinted{};
		static constexpr double kLookPerPx = 0.0022;   // rad of the look per pixel of the mouse
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
		// at the start the terrain is not loaded yet (the user, 2026-10-05): standing, she is held on the ground as it comes
		// until its height under her has stood still for a second (at most 15 s); no falls, no blows meanwhile
		bool settling{ true }; double settleElev{ -1e9 }, settleStill{};
		void Settle(double simt, double dt);
		double bornSim{ oapiGetSimTime() };
	public:
		// a thing in her hands (ocGive / ocTake / ocHeldOf; the user, 2026-10-07: «и предметы»)
		bool Give(const OcHeld& h, bool check = true);
		bool Take(OcHeld* out);
		bool Held(OcHeld* out) const { if (out && holding) *out = held; return holding; }
	private:
		OcHeld held{}; bool holding{}; bool heldPending{};
		UINT heldMesh{ static_cast<UINT>(-1) }; MESHHANDLE heldTpl{};
		std::vector<std::vector<NTVERTEX>> heldVtx;   // the thing's mesh in its own frame, per group
		double HeldWeight() const;                    // kg it weighs here (mass x g / 9.81)
		double LiftLimit() const;                     // kg she can lift here and now (the suit's servos help)
		double CarryLimit() const;                    // kg she carries: as much as she lifts (slower and dearer near it - HeldSlow, HoldW)
		double HoldW() const;                         // W her body pays for holding what she holds (the drives take most of it)
		double LocalG() const;                        // the gravity where she stands (a ship's interior gives its own), m/s^2
		double HeldSlow() const;                      // her walking speed's factor under it (1: none)
		void HeldMeshMake(); void HeldMeshDrop();
		void HeldTargets(HandTarget hand[2]) const;   // her hands on it (model frame)
		void HeldPlace(Figure& fig);                  // the thing where her hands are, after the pose
		// context actions (CONTEXT_ACTIONS.md, F-1 / M-1): the node she looks at, its actions, the one picked, a long one
		struct CtxState
		{
			OBJHANDLE ship{}; int node{ -1 }; OcNode nd{}; std::vector<OcAction> acts; int sel{}; double scanT{}, refreshT{};
			VECTOR3 at{};                              // the node, global
			bool busy{}; int act{ -1 }; double t{}, dur{}; bool interruptible{}; VECTOR3 feet0{};
			bool cursor{}, lmbWas{};                  // M-1: Alt held; the left button last step
			int local{};                              // the node is ours, not a ship's: 1 a thing lying (OrbiterCrew\\Item), 2 a jet pack
			std::string painted;                      // what the panel shows now
			// with Alt: every place of an action within 5 m, marked (the user, 2026-10-07: «на альт подсветить оранжевым
			// зоны интерактивности»); the point in its own vessel's frame, so the moving global frame never shifts it
			struct Mark { OBJHANDLE ship{}; VECTOR3 pos{}; bool reach{}; };
			std::vector<Mark> marks;
		} cx;
		double cxLogT{}; UINT cxMesh{ static_cast<UINT>(-1) }; SURFHANDLE cxSurf{}; VISHANDLE cxVis{}; oapi::Font* cxFont[2]{};
		HudText cxText; bool cxTextTried{}; bool layerLogged{};
		std::string useHintTitle;                    // the caption's title for F's old uses (empty)
		// the seat's display (SEAT_HUD.md): its gauges, asked 10 times a second while she sits; drawn along the bottom
		std::vector<OcGauge> seatGauges; double seatGaugeT{}; SURFHANDLE gaugeSurf{}, gaugeShadow{}, cxShadow{}; VISHANDLE gaugeVis{}; std::string gaugePainted;
		bool farNode{};                              // she looks at a node out of her reach: «подойдите ближе»
		void GaugePaint(int& usedW);          // the helmet display's own letters (Jura, its atlas): the same look everywhere
		void CtxStep(double dt);                      // find the node, keep its actions, the wheel, a long action's time
		bool CtxUse();                                // F: the picked action (false: no node - the old F)
		void CtxBreak(bool done);                     // a long action ends
		void CtxDraw();                               // the marker and the panel at the node
		// in the suit with its display on, the caption goes onto the helmet display's own plate, drawn with the frame
		// (clbkDrawHUD: the camera is final then) - no layer of ours between her eye and the plate to fight it
		struct CtxRun { double x, base; std::string u; int size, col, align; bool black; };
		struct CtxBox { double x0, y0, x1, y1; DWORD col; };
		std::vector<CtxRun> plateRuns; std::vector<CtxBox> plateBoxes; bool plateOn{}, plateMarks{};
		void CtxPaintPlate(oapi::Sketchpad* skp, double vw, double vh, double kx, double ky);   // (SuitHud's overlay)
		void CtxPaintPlateNative(const D3D11Hud::Api* gx, D3D11Hud::Canvas cv, double vw, double vh, double kx, double ky);   // the same on the D3D11 canvas
		void CtxPaint();
		bool CtxRay(VECTOR3& o, VECTOR3& d) const;    // global: through the cursor (Alt), the view's middle, or her look
		ShipInterior* CtxInterior() const;
	public:
		bool fovSet{};                       // her default view angle given once
		static constexpr double kViewAperture = 35.0 * PI / 180.0;   // half the vertical field: 70 deg
		double seatPoseLogT{};    // (DebugLog: the couch's angles every 2 s)
		double seatViewPitch{};   // the seat's own pitch (a drum, OcInterior::Zone): her view pitches with it, rad
		static constexpr double kMarkRange = 50.0;   // the Alt marks, m from her eyes
		static constexpr double kCamMinDist = 2.0;   // the outside view's least distance, m (D3D9's near plane outside: 1 m)
		// inside a cabin the view from behind her is a view from inside (the user's choice, 2026-10-07): D3D9 cuts away all
		// within 1 m of an outside camera, the walls with it; from inside its near plane is 0.1 m. F1 there: her eyes <->
		// behind her; the camera stops short of the walls
		bool thirdIn{};
		static constexpr double kThirdDist = 1.6;    // m behind her head
		double noWalkLogT{};               // the "does not walk" log line, once a second  // sim time she was made (a blow in her first seconds is the machines settling)
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
		// aboard a ship that is not ours (Config\OrbiterCrew\Ships\<its class>.cfg: its airlock in her menu): she flies it
		// (the user, 2026-10-08: «в не наших кораблях только путь 1») - her body hidden in it, moved with it, breathing its
		// cabin; the ship has the focus; F on it takes her out at its airlock when it stands on the ground
		OBJHANDLE ride{}; std::string rideName; bool rideKeyWas{}; NOTEHANDLE rideNote{}; double rideNoteT{}, rideWait{}, rideCamT{};
		void RideStep(double dt);
		bool RideBoard(OBJHANDLE ship);
		void RideExit();
		void RideSay(const std::string& utf8, double seconds = 6);
		void HitBody(double v, Body::Contact c) { if (v > 1.5) motion.Blink(); bio.Impact(suitOn ? Suit::ImpactThrough(v, bio.mass, who.worn.Mass(), c) : v, c); }
		double LieHeight() const { return jet.Worn() ? 0.36 : 0.18; }
		// how far a base's landing pad under her stands above the relief there (0 = none): bases with
		// MapObjectsToSphere put the pads on the smooth sphere, which can be above the real ground
		double PadLift() const;   // her back (or the pack) on the ground, not under it
		void StandUp();
		double suitResidual{};   // heat the suit could not move, W (+ in, - out)

		// helmet: sun shade (V) and lamps (L)
		double shade{}, shadeTarget{};        // 0 raised over the crown .. 1 lowered over the visor
		// a visor with its own light filter (VisorMtrl in the class's cfg: its material, 0-based): no shade - the suit's button
		// (shadeTarget) darkens the glass itself. photo: its darkness 0 clear .. ~0.94
		int visorMtrl{ -1 }; double photo{}, photoSent{ -1 };
		// the view from inside the helmet (HelmetView in the class's cfg): its liner, rim, chin bar and neck ring frame the
		// view, fixed on the torso, the head (the camera) turning inside it; only under the D3D11 client (its near plane
		// comes to 1.5 cm in a cockpit view; D3D9's 10 cm would cut them)
		bool helmetView{}, inHelmView{};
		// the eye there: her real eyes (the head hidden), not the camera point before her face - the visor 7-8 cm ahead, the
		// chin bar 5-6 cm under and before them: it comes into view as she looks down (HelmetEye in the cfg)
		VECTOR3 helmEye{ 0, 0.700, 0.150 };   // between the helmet's lamps (LampPos +-0.165 0.700), at her eyes' depth (InHelm is built round it)
		// a helmet whose upper part lifts on a hinge (the heavy suit's V1, HEAVY_SUIT.md): the mesh groups "HelmCap..." turned
		// about HelmCapHinge (rest pose, model frame) round HelmCapAxis by HelmCapOpen degrees at full open (body cfg)
		VECTOR3 capHinge{}, capAxis{ 1, 0, 0 };
		int synthCam{};
		bool pzWindow{};
		bool pzAutoShut{}; std::string pzShutReason;
		double louvreT{ 1 }; bool louvreMoving{};   // the louvres: 1 closed .. 0 open (1.2 s), moving: their drives heard   // the louvres closed by the suit itself, and why (lava / radiation / heat)   // the heavy suit: louvres open, she looks through the window herself (Shift+V); closed: the cameras   // the heavy suit's synthetic vision: 0 the stereo pair, +1 / -1 the right / left side module (her look +yaw = right, model +x = right)
		VECTOR3 lampRest{ 0.162, 0.707, 0.11 };   // the helmet lamps (the +x one; mirrored), rest pose, model frame: cfg LampPos
		bool hideHair{};                            // cfg HideHair: under this suit's closed helmet the hair is tucked away
		double capOpen{};
		// parts that can get red-hot (HEAVY_SUIT.md): their material indices in the suit mesh (body cfg HeatCeram, HeatSole,
		// HeatRad; -1 none) - ceramic at the surroundings' temperature, the soles at the ground's, the fins at the radiator's
		int heatMtrl[3]{ -1, -1, -1 };
		bool lampOn{};
		SpotLight* lamps[2]{};
		BEACONLIGHTSPEC lampGlow[2]{};          // the lamp glass itself, glowing when the lamps are on
		BEACONLIGHTSPEC lampHalo[2]{};          // its soft halo round it (seen from outside; larger the harder it drives)
		VECTOR3 lampGlowPos[2]{}, lampGlowCol{ 1.0, 0.96, 0.85 }, lampHaloCol{ 0.55, 0.52, 0.45 };
		// the beam (the user, 2026-10-16: the spot wider, the setting on the suit's display, it takes charge): LED behind a TIR
		// lens, 40 mm aperture - 0 flood, 1 work, 2 spot (LAMP_MODE in the scenario; Shift+L or the display's button)
		int lampMode{ 1 }, lampModeSet{ -1 };
		void LampModeNext();
		void UpdateHelmet(double dt, Figure& fig);
	};
}
