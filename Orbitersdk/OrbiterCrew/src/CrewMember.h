// OrbiterCrew - a crew member as an Orbiter vessel: walks, runs, jumps, breathes, tires.
// Ground controls (focus on her, on the surface):
//   W / S       forward / back; S while moving forward is a hard, friction-limited brake
//   A / D       turn            Q / E   side step
//   Shift       run; in the suit: servo boost (faster, bounding stride, paid from the battery)
//   Space       jump
//   K           put on / take off the suit (off only in breathable air)
//   V           helmet sun shade: gold mirror visor against glare and radiation      L   helmet lamps
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
#include <string>
#include <vector>

namespace ocrew
{
	class CrewMember : public VESSEL4
	{
	public:
		const std::string& DisplayName() const { return name; }
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
		std::string& name; std::string& role; std::string& sex;
		double& age; double& heightM;
		Figure bodyFig, suitFig;
		Motion motion;
		CrewSound sound;
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
		std::string message;
		double messageTime{};
		bool mouseWasDown{};
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
