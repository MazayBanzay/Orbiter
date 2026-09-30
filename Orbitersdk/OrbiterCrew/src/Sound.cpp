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
		return firstId + 100;
	}

	void CrewSound::Init(VESSEL* vessel, const std::string& voice)
	{
		xr = XRSound::CreateInstance(vessel);
		if (!xr || !xr->IsPresent()) { oapiWriteLog(const_cast<char*>("OrbiterCrew: XRSound not present, crew sounds off")); delete xr; xr = nullptr; return; }
		int id = 1000;
		id = Load(walk, id, "steps\\walk_%02d.wav", XRSound::BothViewClose);
		id = Load(run, id, "steps\\run_%02d.wav", XRSound::BothViewClose);
		id = Load(suit, id, "steps\\suit_%02d.wav", XRSound::BothViewClose);
		id = Load(suitVac, id, "steps\\suitvac_%02d.wav", XRSound::InternalOnly);
		id = Load(breath, id, "voice\\" + voice + "\\breath_%02d.wav", XRSound::BothViewClose);
		id = Load(helmetBreath, id, "voice\\" + voice + "\\helmet_breath_%02d.wav", XRSound::InternalOnly);
		fanId = id;
		if (!xr->LoadWav(fanId, (std::string(ROOT) + "suit\\fan.wav").c_str(), XRSound::InternalOnly)) fanId = 0;

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
		xr->PlayWav(set.first + pick, false, static_cast<float>(std::clamp(volume * spread, 0.0, 1.0)));
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

		// ---- suit ventilation: always on in a powered suit, louder under thermal load (heard inside only) ----
		const bool fan = in.suited && in.fanOn && in.alive && fanId;
		if (fan)
		{
			const double target = 0.25 + 0.55 * std::clamp(in.fanLoad, 0.0, 1.0);
			fanVolume += (target - fanVolume) * (std::min)(1.0, in.dt / 1.5);   // the fan spins up and down over a second or two
			if (!fanPlaying || std::abs(target - fanVolume) > 0.01) xr->PlayWav(fanId, true, static_cast<float>(fanVolume));
			fanPlaying = true;
		}
		else if (fanPlaying) { xr->StopWav(fanId); fanPlaying = false; fanVolume = 0.25; }
	}
}
