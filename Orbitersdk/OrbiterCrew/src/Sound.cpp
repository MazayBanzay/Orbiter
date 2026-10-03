// OrbiterCrew - crew sounds (see Sound.h).
#include "Sound.h"
#include "XRSound.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ocrew
{
	namespace
	{
		const char* ROOT = "XRSound\\OrbiterCrew\\";
		bool Exists(const std::string& path) { std::ifstream f(path); return f.good(); }
	}

	CrewSound::~CrewSound() { delete xr; }

	int CrewSound::Load(Set& set, int firstId, const std::string& pattern, int playback)
	{
		set.first = firstId; set.count = 0;
		char name[256];
		for (int i = 0; i < 100; ++i)
		{
			snprintf(name, sizeof name, pattern.c_str(), i);
			const std::string path = std::string(ROOT) + name;
			if (!Exists(path) || !xr->LoadWav(firstId + i, path.c_str(), static_cast<XRSound::PlaybackType>(playback))) break;
			++set.count;
		}
		// the same takes again as Global (no fading, no focus needed): for the person in focus - her own ears
		set.firstG = firstId + 50;
		for (int i = 0; i < (std::min)(set.count, 50); ++i)
		{
			snprintf(name, sizeof name, pattern.c_str(), i);
			if (!xr->LoadWav(set.firstG + i, (std::string(ROOT) + name).c_str(), XRSound::PlaybackType::Global)) { set.firstG = 0; break; }
		}
		return firstId + 100;
	}

	void CrewSound::Init(VESSEL* vessel, const std::string& voice)
	{
		xr = XRSound::CreateInstance(vessel);
		if (!xr || !xr->IsPresent()) { oapiWriteLog(const_cast<char*>("OrbiterCrew: XRSound not present, crew sounds off")); delete xr; xr = nullptr; return; }
		// XRSound plays its cockpit air-conditioning loop in every vessel: a person has no cabin - only the suit's own fan
		xr->SetDefaultSoundEnabled(XRSound::AirConditioning, false);
		// the wind is our own (where the person is, the user's rule): XRSound's vessel winds would follow the camera
		xr->SetDefaultSoundEnabled(XRSound::LandedWind, false);
		xr->SetDefaultSoundEnabled(XRSound::FlightWind, false);
		int id = 1000;
		id = Load(walk, id, "steps\\walk_%02d.wav", static_cast<int>(XRSound::PlaybackType::BothViewClose));
		id = Load(run, id, "steps\\run_%02d.wav", static_cast<int>(XRSound::PlaybackType::BothViewClose));
		id = Load(suit, id, "steps\\suit_%02d.wav", static_cast<int>(XRSound::PlaybackType::BothViewClose));
		id = Load(suitVac, id, "steps\\suitvac_%02d.wav", static_cast<int>(XRSound::PlaybackType::InternalOnly));
		id = Load(breath, id, "voice\\" + voice + "\\breath_%02d.wav", static_cast<int>(XRSound::PlaybackType::BothViewClose));
		id = Load(helmetBreath, id, "voice\\" + voice + "\\helmet_breath_%02d.wav", static_cast<int>(XRSound::PlaybackType::InternalOnly));
		fanId = id;
		if (!xr->LoadWav(fanId, (std::string(ROOT) + "suit\\fan.wav").c_str(), XRSound::PlaybackType::InternalOnly)) fanId = 0;
		// the suit computer's alarm tones (XRSound's own tones, no words: the display is Russian, the voice set English)
		cautionId = fanId + 1; warningId = fanId + 2;
		if (!xr->LoadWav(cautionId, "XRSound\\Default\\BeepLow.wav", XRSound::PlaybackType::InternalOnly)) cautionId = 0;
		if (!xr->LoadWav(warningId, "XRSound\\Default\\Warning Beep.wav", XRSound::PlaybackType::InternalOnly)) warningId = 0;
		// Global twins of the fan and the tones (her ears); windId is Global only
		fanG = fanId + 4; cautionG = fanId + 5; warningG = fanId + 6;
		if (!fanId || !xr->LoadWav(fanG, (std::string(ROOT) + "suit\\fan.wav").c_str(), XRSound::PlaybackType::Global)) fanG = 0;
		if (!xr->LoadWav(cautionG, "XRSound\\Default\\BeepLow.wav", XRSound::PlaybackType::Global)) cautionG = 0;
		if (!xr->LoadWav(warningG, "XRSound\\Default\\Warning Beep.wav", XRSound::PlaybackType::Global)) warningG = 0;
		windId = fanId + 3;
		if (!xr->LoadWav(windId, "XRSound\\Default\\Landed Wind.wav", XRSound::PlaybackType::Global)) windId = 0;   // her ears, wherever the camera is

		// breath takes: how hard each one is and how long it lasts, so the right take fits the breathing rate
		std::ifstream f(std::string(ROOT) + "voice\\" + voice + "\\breath.txt");
		std::string line;
		while (std::getline(f, line))
		{
			if (line.empty() || line[0] == ';') continue;
			std::istringstream ss(line); int i; double lv, len;
			if (ss >> i >> lv >> len) { breath.level.push_back(lv); breath.length.push_back(len); }
		}
		helmetBreath.level = breath.level; helmetBreath.length = breath.length;
		oapiWriteLogV("OrbiterCrew: sounds walk %d run %d suit %d breath %d (%s)", walk.count, run.count, suit.count, breath.count, voice.c_str());
	}

	void CrewSound::Play(Set& set, double volume, int pick)
	{
		if (!xr || set.count == 0) return;
		if (pick < 0)
		{
			// any take except the last two
			std::uniform_int_distribution<int> u(0, set.count - 1);
			for (int tries = 0; tries < 8; ++tries) { pick = u(rng); if (set.count < 3 || (pick != set.last && pick != set.prev)) break; }
		}
		set.prev = set.last; set.last = pick;
		const double spread = std::uniform_real_distribution<double>(0.88, 1.0)(rng);
		const int base = mine && set.firstG && pick < 50 ? set.firstG : set.first;   // her own ears, or heard where she is
		xr->PlayWav(base + pick, false, static_cast<float>(std::clamp(volume * spread, 0.0, 1.0)));
	}

	int CrewSound::PickBreath(const Set& set, double intensity, double maxLength)
	{
		// the closest few takes in intensity that fit into one breath, one of them at random
		std::vector<std::pair<double, int>> c;
		const int n = (std::min)(set.count, static_cast<int>(set.level.size()));
		for (int i = 0; i < n; ++i)
			if (set.length[i] <= maxLength && i != set.last && i != set.prev) c.push_back({ std::abs(set.level[i] - intensity), i });
		if (c.empty()) return -1;
		std::sort(c.begin(), c.end());
		const int k = (std::min)(3, static_cast<int>(c.size()));
		return c[std::uniform_int_distribution<int>(0, k - 1)(rng)].second;
	}

	void CrewSound::Update(const Input& in)
	{
		if (!xr) return;
		mine = in.mine;

		// ---- footsteps: on the gait's foot contacts; heavier in the suit; in vacuum only through the body ----
		for (int i = 0; i < in.footfalls; ++i)
		{
			if (in.suited)
			{
				const double v = 0.55 + 0.1 * (std::min)(in.speed, 3.0);
				if (in.vacuum) Play(suitVac, v); else Play(suit, v);
			}
			else if (in.runWeight > 0.5) Play(run, 0.65 + 0.07 * (std::min)(in.speed, 5.0));
			else Play(walk, 0.45 + 0.15 * (std::min)(in.speed, 2.0));
		}
		if (in.landing > 0.5)
		{
			const double v = std::clamp(0.4 + 0.2 * in.landing, 0.0, 1.0);
			Set& s = in.suited ? (in.vacuum ? suitVac : suit) : run;
			Play(s, v); Play(s, 0.8 * v);
		}

		// ---- breathing: one exhale per breath, a take that matches how hard she breathes ----
		if (in.alive && in.breathRate > 1)
		{
			const double period = 60.0 / in.breathRate;
			breathClock += in.dt;
			if (breathClock >= period)
			{
				breathClock = std::fmod(breathClock, period);
				const double I = std::clamp(in.intensity, 0.0, 1.0);
				if (in.suited)
				{
					// the helmet is close to the mouth: every breath is heard inside, even at rest
					const int pick = PickBreath(helmetBreath, 0.15 + 0.85 * I, 0.85 * period);
					if (pick >= 0) Play(helmetBreath, 0.22 + 0.6 * I, pick);
				}
				else if (!in.vacuum && I > 0.12)
				{
					// calm breathing in open air is barely audible; it comes up with effort
					const int pick = PickBreath(breath, I, 0.85 * period);
					if (pick >= 0) Play(breath, 0.15 + 0.7 * I, pick);
				}
			}
		}
		else breathClock = 0;

		// ---- suit thermal control: heard only when it works - a low hum for a little heating or cooling, louder the
		// harder it has to work (strong cold or heat); quiet when nothing is needed (heard inside only) ----
		const bool fan = in.suited && in.fanOn && in.alive && fanId && in.fanLoad > 0.03;
		if (fan)
		{
			const double target = 0.15 + 0.65 * std::clamp(in.fanLoad, 0.0, 1.0);
			fanVolume += (target - fanVolume) * (std::min)(1.0, in.dt / 1.5);   // the fan spins up and down over a second or two
			const int id = mine && fanG ? fanG : fanId;
			if (fanPlaying && fanNow != id) { xr->StopWav(fanNow); fanPlaying = false; }   // the focus moved: the other twin
			if (!fanPlaying || std::abs(target - fanVolume) > 0.01) xr->PlayWav(id, true, static_cast<float>(fanVolume));
			fanPlaying = true; fanNow = id;
		}
		else if (fanPlaying) { xr->StopWav(fanNow); fanPlaying = false; fanVolume = 0.15; }

		// ---- the air around her: the wind, by its density and her speed through it; through the helmet muffled ----
		if (windId && in.wind > 0.01 && in.alive)
		{
			const double target = std::clamp(in.wind, 0.0, 1.0);
			windVolume += (target - windVolume) * (std::min)(1.0, in.dt / 0.8);
			if (!windPlaying || std::abs(target - windVolume) > 0.01) xr->PlayWav(windId, true, static_cast<float>(windVolume));
			windPlaying = true;
		}
		else if (windPlaying) { xr->StopWav(windId); windPlaying = false; windVolume = 0; }

		// ---- alarm: a tone while a caution or a warning waits for acknowledgement ----
		if (in.alarm > 0 && in.alive)
		{
			alarmClock -= in.dt;
			if (alarmClock <= 0)
			{
				const int id = in.alarm >= 2 ? (mine && warningG ? warningG : warningId) : (mine && cautionG ? cautionG : cautionId);
				if (id) xr->PlayWav(id, false, in.alarm >= 2 ? 0.8f : 0.6f);
				alarmClock = in.alarm >= 2 ? 1.5 : 4.0;
			}
		}
		else alarmClock = 0;
	}
}
