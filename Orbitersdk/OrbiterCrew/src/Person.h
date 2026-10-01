// OrbiterCrew - the person: the unit of the crew.
// The Person comes first. He or she lives as long as the organism lives; vessels - the body in Orbiter's world, a
// carrier ship - are places and means the person uses. A person is never part of a vessel: a destroyed ship does not
// take its people with it, their own organism decides. The crew registry holds every person of the simulation.
#pragma once
#include "LifeSupport.h"
#include <memory>
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

		enum Where { NOWHERE, IN_WORLD, ABOARD };
		Where where{ NOWHERE };          // IN_WORLD: her body walks in Orbiter's world; ABOARD: in a seat of a ship
		OBJHANDLE vessel{};              // that body, or the carrier ship

		bool Alive() const { return body.state != Body::DEAD; }
	};

	// the crew registry: every person of the simulation
	class Crew
	{
	public:
		static Person& Create();
		static Person* Find(int id);
		static const std::vector<std::unique_ptr<Person>>& All() { return people; }
		static void Clear() { people.clear(); }
	private:
		inline static std::vector<std::unique_ptr<Person>> people;
		inline static int nextId{ 1 };
	};
}
