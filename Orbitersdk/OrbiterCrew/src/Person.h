// OrbiterCrew - the person: the unit of the crew.
// The Person comes first. He or she lives as long as the organism lives; vessels - the body in Orbiter's world, a
// carrier ship - are places and means the person uses. A person is never part of a vessel: a destroyed ship does not
// take its people with it, their own organism decides. The crew registry holds every person of the simulation.
#pragma once
#include "LifeSupport.h"
#include "Wearable.h"
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace ocrew
{
	struct Person
	{
		int id{};
		std::string name{ "Crew member" }, role{ "crew" };
		std::string sex{ "female" };     // a description: abilities are set per person (VO2max, LiftMax), not derived from it
		double age{}, heightM{};
		Body body;                       // the organism: water, food, injuries, dose, stamina, consciousness
		Worn worn;                       // what is on him or her: coverall, suit (with its computer), pack

		enum Where { NOWHERE, IN_WORLD, ABOARD, INTERIOR };
		Where where{ NOWHERE };          // IN_WORLD: her body walks in Orbiter's world; ABOARD: in a seat of a ship, no body;
		                                 // INTERIOR: aboard, her body walks inside the ship (attached to it)
		OBJHANDLE vessel{};              // that body (IN_WORLD, INTERIOR), or the carrier ship (ABOARD)
		OBJHANDLE ship{};                // the ship he or she is aboard (ABOARD, INTERIOR)
		// the view the user had of him or her (the user's rule 2026-10-03: going in and out of ships the camera stays with
		// the person - through the eyes, or from outside at the same distance; never the ship's panel)
		bool viewOutside{};
		double viewDist{};
		std::string bodyClass{ "OrbiterCrew\\Astronavigator" };   // the vessel class of his or her body in the world

		bool Alive() const { return body.state != Body::DEAD; }
		// the person's own scenario lines (who, the organism, what is worn) - the same in a body's block and aboard a ship
		void Save(FILEHANDLE scn);
		bool LoadLine(const std::string& key, std::istringstream& ss);
	};

	// the crew registry: every person of the simulation
	class Crew
	{
	public:
		static Person& Create();
		static Person* Find(int id);
		// a body being made for a person who exists (going out of a ship): its constructor takes that person
		static void ExpectBody(int id) { pending = id; }
		static Person& Claim();                   // the expected person, or a new one
		static bool ClaimedExisting() { return claimedExisting; }
		static const std::vector<std::unique_ptr<Person>>& All() { return people; }
		static void Clear() { people.clear(); pending = 0; }
		static bool AnyAboard();
	private:
		inline static std::vector<std::unique_ptr<Person>> people;
		inline static int nextId{ 1 };
		inline static int pending{};
		inline static bool claimedExisting{};
	};
}
