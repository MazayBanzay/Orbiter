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
#include "JetPack.h"
#include "LifeSupport.h"
#include "Motion.h"
#include "Skin.h"
#include "SuitHud.h"
#include "Sound.h"
#include <string>
#include <vector>

namespace ocrew
{
	class CrewMember : public VESSEL4
	{
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
		double Mass() const { return bio.mass + (suitOn ? suitMass : 0) + (jet.Worn() ? JetPack::DRY : 0); }
		double Gravity() const;
		void Say(const std::string& text) { message = text; messageTime = 6; }
		void ShowFigure();
		void SetSuit(bool on);
		void Place(bool lying);
		void Drive(double dt, double g);
		Thermal Surroundings() const;
		void Jump();
		void Liftoff();
		void Land();

		std::string name{ "Crew member" }, role{ "crew" };
		Figure bodyFig, suitFig;
		Motion motion;
		CrewSound sound;
		std::string voice{ "female1" };
		VISHANDLE vis{};
		VECTOR3 eye{ 0, 0.69, 0.17 };
		double height{ 0.93 };
		double suitMass{ 25 };
		double walkSpeed{ 1.45 }, runSpeed{ 5.0 };

		bool suitOn{ true }, suitFromScenario{};
		Suit suit;
		Body bio;
		Air air;
		Thermal thermal;
		bool boost{};                        // servo boost (suit, live drives, Shift)
		double humanW{}, driveDemandW{};     // who pays for the movement: her muscles / the drives

		Keys keys;
		bool keysFresh{};
		double fwd{}, lat{}, turn{}, accel{};
		bool airborne{}, lying{}, placed{};
		double fallSpeed{}, landingSpeed{}, jumpHeading{};

		// suit RCS: SAFER-class cold-gas unit in the pack (self-rescue, ~10 m/s); off on the ground and without power
		PROPELLANT_HANDLE n2{};
		std::vector<THRUSTER_HANDLE> rcs;
		bool rcsLive{};
		RcsSet rcsN2;
		int rcsState{ -1 };                  // live / which set, to switch only on change
		void BuildAttGroups(const RcsSet& set);

		// jet pack
		JetPack jet;
		bool jetFromScenario{}, wasFree{};
		double freeVy{}, freeT{};
		FlightInput flight{};
		bool flightFresh{};
		void Touchdown();
		void GroundContactCheck(double dt);
		void Fall(double impact, const char* why);
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
		SuitHud hud;

		// helmet: sun shade (V) and lamps (L)
		double shade{}, shadeTarget{};        // 0 raised over the crown .. 1 lowered over the visor
		bool lampOn{};
		SpotLight* lamps[2]{};
		BEACONLIGHTSPEC lampGlow[2]{};          // the lamp glass itself, glowing when the lamps are on
		VECTOR3 lampGlowPos[2]{}, lampGlowCol{ 1.0, 0.96, 0.85 };
		void UpdateHelmet(double dt, Figure& fig);
	};
}
