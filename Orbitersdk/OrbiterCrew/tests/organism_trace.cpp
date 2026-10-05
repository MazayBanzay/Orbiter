// OrbiterCrew - organism trace: runs the Body (the person's organism) through fixed situations and prints its state.
// Used to prove that a restructuring of the organism changes nothing: record a golden trace before, compare after.
// Builds without Orbiter: tests\stub\Orbitersdk.h stands in for the SDK header. Build: tests\build_trace.cmd; run: organism_trace.exe > trace.txt
#include "../src/LifeSupport.h"   // with tests\stub first on the include path: no Orbiter needed
#include <cstdio>

using ocrew::Body;

static void Print(const char* scene, double t, const Body& b)
{
	std::printf("%-10s t=%8.0f st=%d res=%.6g core=%.6g wbal=%.6g met=%.6g pulse=%.6g br=%.6g wdef=%.6g gly=%.6g fat=%.6g fast=%.6g "
		"hurt=%.4g,%.4g,%.4g,%.4g inj=%.6g dose=%.6g aer=%.6g run=%.6g lift=%.6g warn=%s\n",
		scene, t, static_cast<int>(b.state), b.reserve, b.coreT, b.wbal, b.met, b.pulse, b.breath, b.waterDef, b.glycogen, b.fat,
		b.fastDays, b.hurt[0], b.hurt[1], b.hurt[2], b.hurt[3], b.injury, b.doseSv, b.AerobicMax(), b.RunLimit(), b.LiftCapacity(),
		b.warning.empty() ? "-" : b.warning.c_str());
}

// one step of the organism the way CrewMember drives it: Step, (Treat in a medical bay), Irradiate, Sustain
struct In { double activity{}, ppO2{ 21.2 }, ppCO2{ 0.04 }, ambient{ 101.3 }, heat{}, dose{}, airT{ 293.15 }; bool suited{}, supplied{ true }; double* water{}; bool medbay{}; };
static void Run(const char* scene, Body& b, const In& in, double seconds, double dt, double every)
{
	double next = 0;
	for (double t = 0; t <= seconds; t += dt)
	{
		b.Step(dt, in.activity, in.ppO2, in.ppCO2, in.ambient, in.suited, in.heat);
		if (in.medbay) b.Treat(dt, Body::Care{});
		b.Irradiate(dt, in.dose);
		b.Sustain(dt, in.supplied, in.water, in.airT, in.suited);
		if (t >= next) { Print(scene, t, b); next += every; }
	}
}

// the time of death (s) after a given acute dose, without care (-1: alive after 'limit' seconds)
static double DeathAfter(double sv, double limit)
{
	Body b; b.doseSv = sv; In in;
	for (double t = 0; t <= limit; t += 60)
	{
		b.Step(60, 0, in.ppO2, in.ppCO2, in.ambient, false, 0); b.Irradiate(60, 0); b.Sustain(60, true, nullptr, in.airT, false);
		if (b.state == Body::DEAD) return t;
	}
	return -1;
}

int main()
{
	{ Body b; In in; Run("rest", b, in, 3600, 1, 600); }
	{ Body b; In in; in.activity = ocrew::LocomotionPower(62, 4.0, 9.81); Run("run", b, in, 600, 0.1, 60); }
	{ Body b; In in; in.ppO2 = 0; in.ppCO2 = 0; in.ambient = 0; Run("vacuum", b, in, 150, 0.1, 10); }
	{ Body b; In in; in.heat = 300; Run("heat", b, in, 7200, 1, 600); }
	{ Body b; In in; in.heat = -300; Run("cold", b, in, 7200, 1, 600); }
	{ Body b; In in; b.Impact(9); Print("impact9", 0, b); b.Impact(12); Print("impact12", 0, b); Run("impact", b, in, 86400, 10, 21600); }
	{ Body b; b.Impact(12, Body::ON_FEET); Print("feet12", 0, b); }
	{ Body b; b.Impact(9, Body::ON_BACK); Print("back9", 0, b); }
	{ Body b; b.Impact(7, Body::ON_HEAD); Print("head7", 0, b); }
	{ Body b; In in; in.supplied = false; Run("nowater", b, in, 5 * 86400, 10, 43200); }
	{ Body b; In in; in.supplied = false; double water = 1e9; in.water = &water; Run("nofood", b, in, 9 * 86400, 30, 86400); }
	{ Body b; In in; in.dose = 0.5; Run("rad", b, in, 24 * 3600, 1, 3600); }
	{ Body b; In in; in.ppO2 = 8; Run("hypoxia", b, in, 300, 0.1, 30); }
	{ Body b; In in; in.ppCO2 = 9; Run("co2", b, in, 600, 0.1, 60); }
	// 2026-10-04 (the user): death in 3 minutes only without oxygen; radiation by its level, "смерть под лучом"; the medical bay
	{ Body b; In in; in.ppO2 = 0; Run("suffoc", b, in, 400, 0.1, 30); }
	{ Body b; In in; in.ppO2 = 8; Run("hypoxia8", b, in, 1200, 0.1, 120); }
	{ Body b; In in; in.ppCO2 = 25; Run("co2hi", b, in, 1800, 0.1, 180); }
	{ Body b; In in; in.heat = 300; Run("heatlong", b, in, 4 * 3600, 1, 1800); }
	{ Body b; In in; in.heat = -300; Run("coldlong", b, in, 5 * 3600, 1, 1800); }
	{ const double sv[] = { 3, 5, 8, 15, 50, 120 };
	  for (double s : sv) { const double t = DeathAfter(s, 90 * 86400.0); std::printf("ars %5.0f Sv: %s %.2f days\n", s, t < 0 ? "alive after 90 days," : "dead after", t < 0 ? 90.0 : t / 86400); } }
	{ Body b; In in; in.dose = 1e5; Run("beam", b, in, 600, 1, 30); }
	{ Body b; In in; b.Impact(12); b.doseSv = 8; in.medbay = true; Run("medbay", b, in, 6 * 86400, 60, 43200); }
	{ Body b; In in; b.doseSv = 30; in.medbay = true; Run("medbay30", b, in, 8 * 86400, 60, 86400); }
	return 0;
}
