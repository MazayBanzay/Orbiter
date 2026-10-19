// OrbiterCrew - cargo classes (Tantra_Design/CARGO_PLAN.md, step 1): a cargo describes itself in its own cfg,
// Config\OrbiterCrew\Cargo\<class>.cfg - its label, its box, its tare, its mesh, the resource it holds, what it unpacks
// into. A scenario names only the class (ITEM_CLASS <class> [kg of resource]); a thing in her hands or lying about
// carries it in OcHeld::data as "cargo <class> <kg>" (no change of the API's layout).
#pragma once
#include "Orbitersdk.h"
#include "../include/OrbiterCrewApi.h"
#include <string>

namespace ocrew
{
	struct CargoClass
	{
		std::string name;                 // the cfg's file name, no extension
		std::string labelRu, labelEn;     // in the ship's code page (cp1251) and in English
		double dims[3]{ 0.5, 0.5, 0.5 };  // m: x, y (up), z - the box (M 0.5, S 0.75: CARGO_STANDARD.md) or its own
		double tare{ 2 };                 // kg: the box itself, empty
		std::string mesh;                 // Meshes\..., without extension
		std::string resource;             // what it holds that can be poured out (o2, n2, water, food, sorbent, mh, ...), "" none
		double resourceKg{}, resourceCap{};   // kg: in it when issued full; at most
		std::string unpack;               // the vessel class it unpacks into (CARGO_PLAN.md, step 7), "" none
		bool ok{};
	};

	// the class by name (cached; a missing cfg - ok false, logged once)
	const CargoClass& FindCargoClass(const std::string& name);
	// a thing of that class: its label in the interface's language, its mass (tare + resource), its box and mesh;
	// resourceKg < 0 - full as issued
	OcHeld MakeCargo(const CargoClass& c, double resourceKg = -1);
	// what OcHeld::data says: the class and the resource left (false: not a cargo of a class)
	bool CargoOf(const OcHeld& h, std::string& cls, double& resourceKg);
}
