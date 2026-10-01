// OrbiterCrew - the person and the crew registry (see Person.h).
#include "Person.h"

namespace ocrew
{
	Person& Crew::Create()
	{
		people.push_back(std::make_unique<Person>());
		people.back()->id = nextId++;
		return *people.back();
	}

	Person* Crew::Find(int id)
	{
		for (auto& p : people) if (p->id == id) return p.get();
		return nullptr;
	}
}
