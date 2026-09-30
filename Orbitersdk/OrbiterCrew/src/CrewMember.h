// OrbiterCrew - a crew member as an Orbiter vessel: walks, runs, jumps, breathes, tires.
// Ground controls (focus on her, on the surface):
//   W / S       forward / back; S while moving forward is a hard, friction-limited brake
//   A / D       turn            Q / E   side step
//   Shift       run             Space   jump
//   K           put on / take off the suit (off only in breathable air)
#pragma once
#include "LifeSupport.h"
#include "Motion.h"
#include "Skin.h"
#include <string>

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

	private:
		struct Figure { UINT mesh{}; Skin skin; ClipSet clips; bool ok{}; };
		struct Keys { bool fwd{}, back{}, left{}, right{}, stepL{}, stepR{}, run{}; };

		Figure& Active() { return suitOn && suitFig.ok ? suitFig : bodyFig; }
		double Mass() const { return bio.mass + (suitOn ? suitMass : 0); }
		double Gravity() const;
		void Say(const std::string& text) { message = text; messageTime = 6; }
		void ShowFigure();
		void SetSuit(bool on);
		void Place(bool lying);
		void Drive(double dt, double g);
		void Jump();
		void Land();

		std::string name{ "Crew member" }, role{ "crew" };
		Figure bodyFig, suitFig;
		Motion motion;
		VISHANDLE vis{};
		VECTOR3 eye{ 0, 0.69, 0.17 };
		double height{ 0.93 };
		double suitMass{ 25 };
		double walkSpeed{ 1.45 }, runSpeed{ 5.0 };

		bool suitOn{ true }, suitFromScenario{};
		Suit suit;
		Body bio;
		Air air;

		Keys keys;
		bool keysFresh{};
		double fwd{}, lat{}, turn{}, accel{};
		bool airborne{}, lying{}, placed{};
		double fallSpeed{}, landingSpeed{}, jumpHeading{};

		std::string message;
		double messageTime{};
	};
}
