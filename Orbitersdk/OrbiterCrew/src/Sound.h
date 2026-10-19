// OrbiterCrew - crew sounds through XRSound (optional: silent without it).
// Footsteps fire on the foot contacts of the animated gait; breaths are paced by the physiology (rate and depth).
// Each sound is one of many takes, never the same take twice in a row, with a small volume spread.
// Air carries sound; vacuum does not: outside a suit in vacuum there is nothing to hear, inside it only what the
// body conducts (muffled footfalls), the helmet breath and the ventilation fan.
// Files: XRSound\OrbiterCrew\ (built by Tantra_Design/audio/build_crew_sounds.py from CC0 recordings).
#pragma once
#include <Orbitersdk.h>
#include <random>
#include <string>
#include <vector>

class XRSound;

namespace ocrew
{
	class CrewSound
	{
	public:
		struct Input
		{
			double dt{};
			int footfalls{};          // foot contacts this step
			double runWeight{};       // 0 walk .. 1 run
			double speed{};           // m/s
			double landing{};         // touchdown speed after a jump, m/s
			bool suited{}, vacuum{}, fanOn{};
			double fanLoad{};         // 0..1 thermal control load: the fan runs harder when heating or cooling
			// the suit's machinery heard inside (HEAVY_SUIT.md): the heat pump's compressor (its load 0..1 and its strain - the
			// COP falling in heat makes it labour, louder and higher), the evaporator boiling (0..1), the field's coils humming
			// (0..1 of their full power), the shell's thermal stress (0..1: deep cold or strong heat - it creaks now and then)
			double compLoad{}, compStrain{}, evap{}, coils{}, shellStress{};
			double drives{}, driveRate{};   // the joint drives: 0..1 their load, and how fast the joints move (0..1)
			double stepPitch{ 1 };     // the suit's footfall: lower and heavier in a heavy suit
			// whose ears: eyes - the camera is her own eyes (first person): what the suit and her body carry is heard (the fan,
			// the machinery, her breath, the footfalls through the boots). Otherwise the listener is outside: only what the gas
			// around carries reaches it - nothing in vacuum (Io, the Moon), very little and dull in thin air (Mars)
			bool eyes{};
			int alarm{};               // the suit computer's caution (1) / warning (2) not yet acknowledged
			bool alive{ true };
			bool conscious{ true };   // out cold: her breath shallow, barely heard
			double breathRate{ 13 };  // per minute
			double intensity{};       // 0..1: how hard she breathes
			bool mine{};              // she is the person in focus: her sounds are the user's ears (Global, wherever the camera is)
			double wind{};            // 0..1: the air moving past her where SHE is (0 in vacuum and inside a ship)
			// the jet pack: the impeller pack's turbine running, its rotors' speed (0..1), the rocket share (the boost, or the
			// light pack's pods), the gas around her (density kg/m^3, the speed of sound m/s; 0 in vacuum)
			bool jetTurbine{};
			double jetFan{}, jetRocket{}, airRho{}, soundSpeed{};
		};

		~CrewSound();
		void Init(VESSEL* vessel, const std::string& voice);
		void Update(const Input& in);

	private:
		struct Set { int first{}, firstG{}, count{}, last{ -1 }, prev{ -1 }; std::vector<double> level, length; };   // firstG: the same takes, Global
		int Load(Set& set, int firstId, const std::string& pattern, int playback);
		void Play(Set& set, double volume, int pick = -1, double speed = 0);   // speed > 0: its playback speed (pitch)
		int PickBreath(const Set& set, double intensity, double maxLength);

		XRSound* xr{};
		Set walk, run, suit, suitVac, breath, helmetBreath;
		int fanId{}, cautionId{}, warningId{}, windId{};
		int fanG{}, cautionG{}, warningG{};
		bool mine{};
		int fanNow{};                      // the fan loop that plays (spatial or Global)
		bool windPlaying{};
		double windVolume{};
		double alarmClock{};
		bool fanPlaying{};
		double fanVolume{};
		double breathClock{};
		// the jet pack's loops: the fans with the turbine, the rocket; each a spatial and a Global twin (her ears)
		int jetFanId{}, jetFanG{}, jetRocketId{}, jetRocketG{};
		// the suit's machinery loops (heard inside only) and the shell's creaks
		int compId{}, evapId{}, coilId{}; int compNow{}, evapNow{}, coilNow{};
		double compVol{}, compSpeed{ 1 }, evapVol{}, coilVol{}, creakClock{ 5 };
		int driveId{}, driveNow{}; double driveVol{}, driveSpeed{ 1 };
		Set creak;
		int jetFanNow{}, jetRocketNow{};
		double jetFanVol{}, jetFanSpeed{ 1 }, jetRocketVol{};
		std::mt19937 rng{ std::random_device{}() };
	};
}
