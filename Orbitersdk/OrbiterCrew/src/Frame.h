// OrbiterCrew - the amplifier frame «Усилитель» (Orbitersdk/OrbiterCrew/AMPLIFIER.md): a powered frame worn over the
// coverall or the light suit. Its load goes into the ground through its own legs, past her; her hands are on grips
// inside its forearms. It carries itself, the suit and the load; she carries only herself.
//   class   - Config\OrbiterCrew\Frames\<cls>.cfg (masses, the joints' torques, the cassette); defaults below
//   mesh    - Meshes\OrbiterCrew\Usilitel.msh, her rest model frame, a group per bone (Config\OrbiterCrew\Usilitel.geo);
//             every frame each group rides its bone (as the impeller pack's PoseFan)
//   limits  - what she lifts and carries with it, by the joints' torques and the gravity (AMPLIFIER.md, п. 2)
#pragma once
#include "Orbitersdk.h"
#include <string>
#include <vector>

namespace ocrew
{
	class Skin;
	constexpr double kFramePalm = 0.17;   // m: the frame's palm ahead of her own hand, along the forearm

	class Frame
	{
	public:
		// the class: «Усилитель» (AMPLIFIER.md, the 30th century's drives: superconducting motors and MR hydraulics)
		std::string cls{ "Usilitel" }, title{ "Usilitel" };
		double mass{ 60 };                         // kg with the cassette
		double kneeCont{ 2000 }, kneePeak{ 4000 }; // N*m: the knee sets everything (hip the same, ankle 0.7 of it)
		double armsCont{ 1200 }, armsPeak{ 1800 }; // kg of Earth weight, both arms together
		double peakS{ 5 };                         // s of the peak before the drives heat up
		double liftCap{ 400 };                     // kg: what it lifts and carries (Guardian XO's 90 kg on 68 kg, the 30th century's drives x4-5)
		double balanceKg{ 355 };                   // kg held in the arms at the chest, edge-on: past it she tips forward (any g)
		double battCap{ 6 * 3.6e6 };               // J: the cassette, 6 kWh (the MPU cell's density, the Karkas battery's format)
		double supplyW{ 1500 }, idleW{ 30 };       // W: the continuous limit; the electronics and the gyro
		bool ApplyClass(const std::string& name);

		bool Worn() const { return worn; }
		void SetWorn(bool w) { worn = w; }
		double Batt() const { return batt; }       // J
		void SetBatt(double j) { batt = j < 0 ? 0 : (j > battCap ? battCap : j); }
		bool Live() const { return worn && (batt > 0 || fromSuit); }
		// the suit computer's «СОЕДИНЕНИЯ»: the frame draws on the suit's battery when its cassette is flat (set each step by
		// CrewMember: the link switched on, the suit on and not flat)
		bool link{};                               // the switch (saved)
		bool fromSuit{};                           // this step: the suit's battery feeds it

		// what she can do in it, kg of Earth weight (as CrewMember::HeldWeight): gRel - the gravity in Earth g; ownKg - she,
		// what she wears (the frame too) without the load
		double LiftKg(double gRel, double ownKg) const;    // briefly: the peak of the arms and the legs
		// what she holds at the chest without tipping forward (any gravity: weight and footing scale together): its centre
		// 0.25 m + depth/2 before her ankles, the frame's toe 0.27 m, she leans back 0.10 m (CARGO_STANDARD.md)
		double BalanceKg(double ownKg, double depth) const;
		double CarryKg(double gRel, double ownKg) const;   // on the move: their continuous ratings
		// the power the drives draw (W) for carrying the extra over what her own legs do; the cassette pays it
		double Step(double dt, double drawW);      // -> J the suit's battery has to give (the cassette flat, the link on)
		double DrawW() const { return draw; }

		void Attach(VESSEL* v);                    // the body it is on: its mesh
		void Detach();
		void Pose(const Skin& skin, VISHANDLE vis);// each group onto its bone as she stands now

	private:
		bool worn{};
		double batt{ 6 * 3.6e6 }, draw{};
		VESSEL* v{};
		UINT mesh{ static_cast<UINT>(-1) };
		MESHHANDLE tpl{};
		struct Grp { std::string boneName; int bone{ -1 }; std::vector<NTVERTEX> rest; VECTOR3 c{}; };
		std::vector<Grp> grp;
		const Skin* bound{};
		bool shown{};
		void LoadGeo();
	};
}
