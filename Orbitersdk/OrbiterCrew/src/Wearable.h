// OrbiterCrew - what the person wears (shared by «Экипаж» and «Скафандр»: change only by agreement).
// The person comes first; what is on him or her belongs to the person, not to the body in Orbiter's world. The body
// only carries the physics: a worn item puts its parts on the body it is on (mass, thrusters, meshes, particles) and
// takes them off again, keeping its own state (tanks, battery, fuel, the computer's pages).
//   coverall - always on, part of the person's figure (no state of its own yet)
//   suit     - over the coverall; self-contained: its own tanks, battery, cold-gas RCS store and computer
//   pack     - over the suit, on the suit's mounts; not part of the suit
//   frame    - the amplifier frame over the coverall or the light suit, on the same back mounts: the pack or the frame
// Kept light for Person.h: the heavy classes are only declared here (Wearable.cpp, SuitComputer.h).
#pragma once
#include "LifeSupport.h"
#include <iosfwd>
#include <memory>
#include <string>

namespace ocrew
{
	class JetPack;
	class Frame;     // the amplifier frame «Усилитель» (Frame.h, AMPLIFIER.md)
	struct SuitComputer;   // the suit's computer: part of the suit, kept apart (SuitComputer.h)

	struct SuitItem
	{
		SuitItem(); ~SuitItem();
		SuitItem(const SuitItem&) = delete; SuitItem& operator=(const SuitItem&) = delete;
		bool on{ true };                        // worn (false: taken off - the person is in the coverall)
		bool fromScenario{};                    // "on" came from the scenario (else: by the air where the body appears)
		double mass{ 25 };                      // kg, set by the body's config (SuitMass)
		// the suit's class (HEAVY_SUIT.md): Config\OrbiterCrew\Suits\<cls>.cfg gives its mass, shield, field, battery,
		// supply, thermal control and ratings; applying it issues the suit full (the scenario's SUIT_* lines follow)
		// classes (the user, 2026-10-18): Suit_T10 the light suit (the prototype and the new type are both T10, their
		// meshes differ), Suit_T30 the heavy «Панцирь». The old names Karkas / Pantsir still load (older saves)
		std::string cls{ "Suit_T10" }, title;
		bool ApplyClass(const std::string& name);
		Suit life;                              // tanks, battery, thermal control, drives, shield
		// a set for the class (Config\OrbiterCrew\Suits\Sets\<name>.cfg; «Бор» - the borazon plates of Suit_T10): the class
		// stays, the set adds its mass and its shield, cuts the shell's conductance (the plates stand off on BN aerogel) and
		// adds a little sunlight (amber over white). Its plates are the figure's groups "Set*", shown only while it is on.
		// Put on / taken off only by the scenario's SUIT_SET line for now
		bool setOn{};
		std::string setName{ "Boron" }, setCls;   // setCls: the class it fits (its cfg's Class)
		double setMass{}, setShield{}, setCondMul{ 1 }, setAlphaAdd{};
		bool setRead{};
		bool SetPlates(bool put);               // false: no such set, or not for this class
		double n2Kg{ -1 };                      // its cold-gas RCS store while off a body, kg (-1: as the body has it)
		std::unique_ptr<SuitComputer> computer;
	};

	struct Worn
	{
		Worn(); ~Worn();
		Worn(const Worn&) = delete; Worn& operator=(const Worn&) = delete;
		SuitItem suit;
		std::unique_ptr<JetPack> pack;          // always owned; worn or not: pack->Worn()
		bool packFromScenario{};                // the scenario says the pack is on
		std::unique_ptr<Frame> frame;           // always owned; worn or not: frame->Worn()

		double DryMass() const;                 // what the person wears without propellant, kg
		double Mass() const;                    // with the pack's fuel (for a fallen person as cargo)
		// scenario lines of what is worn (keys unique to it); Load returns false for a key that is not its own
		void Save(FILEHANDLE scn) const;
		bool Load(const std::string& key, std::istream& ss);
	};
}
