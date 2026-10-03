// OrbiterCrew - the suit's computer: part of the suit, kept apart from it - its display (SuitHud) and its autopilots.
// It reads the suit and the body it is on and gives commands to the suit and the pack; it holds its own state
// (pages, targets, the look of the display), which goes with the suit wherever the person goes.
#pragma once
#include "Autopilot.h"
#include "SuitHud.h"

namespace ocrew
{
	struct SuitComputer
	{
		SuitHud hud;
		Autopilot ap;
		// the body it was drawn on leaves the world: let go of what is bound to that body (the IR camera)
		void Detach() { hud.Detach(); }
	};
}
