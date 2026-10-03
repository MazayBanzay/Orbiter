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
			int alarm{};               // the suit computer's caution (1) / warning (2) not yet acknowledged
			bool alive{ true };
			double breathRate{ 13 };  // per minute
			double intensity{};       // 0..1: how hard she breathes
			bool mine{};              // she is the person in focus: her sounds are the user's ears (Global, wherever the camera is)
			double wind{};            // 0..1: the air moving past her where SHE is (0 in vacuum and inside a ship)
		};

		~CrewSound();
		void Init(VESSEL* vessel, const std::string& voice);
		void Update(const Input& in);

	private:
		struct Set { int first{}, firstG{}, count{}, last{ -1 }, prev{ -1 }; std::vector<double> level, length; };   // firstG: the same takes, Global
		int Load(Set& set, int firstId, const std::string& pattern, int playback);
		void Play(Set& set, double volume, int pick = -1);
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
		std::mt19937 rng{ std::random_device{}() };
	};
}
