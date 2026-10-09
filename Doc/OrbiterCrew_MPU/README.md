# OrbiterCrew and MPU - beta 0.2081026b (Orbiter 2024)

OrbiterCrew: people with physiology, a space suit and a jet pack. MPU / EMPU: an open and a sealed electric 8x8 mobile platform, with a charging station. Early beta.

## Install

1. A clean **Orbiter 2024** (x86). Start it with **Orbiter_ng.exe** (the D3D9 client); the inline-graphics orbiter.exe is not supported.
2. Launchpad, tab Modules: tick **XRSound** (all sound goes through it).
3. Unpack the archive into the Orbiter folder (it only adds files).
4. Two settings are needed for the EMPU cabin:
   - `D3D9Client.cfg`: `NearClipPlaneMode = 1` (otherwise holes appear in the cabin walls) and `ShadowMapMode = 2` (otherwise the Sun shines through the hull and the cabin lamps are invisible).
   - `Orbiter_NG.cfg`: `EnableLocalLights = TRUE`, `MaxLights = 8` (the cabin lamps and headlights).
   - Recommended: `D3D9Client.cfg`: `ShaderCacheUse = 1` (the client keeps its compiled shaders: much faster starts after the first).
5. Scenarios: folder **MPU** (start with "MPU - Earth, KSC"; its description lists the controls). In any of them the person can stand up (F), put the suit on (K) and use the suit computer.

## Scenarios

| Scenario | What to do |
|---|---|
| MPU - Earth, KSC | Drive the EMPU, charge it at the station, swap cells, couple the open MPU |
| MPU - Moon, Brighton Beach | The same on regolith in vacuum |
| MPU - Mars, Olympus | The same under a thin red-dust sky |
| MPU - Earth, crash test | Speed limiter off, 130 km/h into the charging station |
| MPU - Earth, KSC, Delta Glider | Board a stock Delta Glider through its nose airlock, fly it, get out (see "Boarding other ships" below) |

## On foot

- W/S walk, A/D turn, Q/E side step, Shift run, Space jump. F1 eyes / outside view; right mouse held looks around.
- K suit on/off (off only in breathable air), L lamps, B takes / drops the jet pack.
- Look at a thing within reach: its actions show beside the aim; the mouse wheel picks one, F does it. Alt gives the cursor and marks every place of an action within 8 m with an orange square.
- Inside a cabin F1 switches between the eyes and a view from behind (it stops at the walls).
- Language (English / Russian): the suit computer's left MFD, page Options, switch RU | EN - it sets the suit computer and the interaction menu at once and is remembered (`LANGUAGE` in `Config\OrbiterCrew\OrbiterCrew.cfg`). The EMPU's screens have their own switch (terminal, SETUP: RUS / ENG).
- In a cabin the head sways with the machine's inertia and a light road shake at speed: `SHAKE` in the same file (1 as modelled, 0.5 half, 0 none).

## Platforms

- W/S drive and brake (reverse from standstill), A/D steer, Shift+W full power, Space brake, B parking brake, Caps Lock platform up/down.
- EMPU: sealed cabin with an airlock, a yoke, a touch terminal (NAV, POWER, LIFE / ENV, GEAR, MOTOR, SETUP; RUS / ENG; speed limiter) and cabin lights.
- Charging: F at the station's cable reel, then at the machine's power socket (front bumper, right). Cells come out and go in at the bays and at the station's rack.
- Crashes damage wheels and the hull; above 50 km/h they injure the people inside.

The suit computer (H, eyes view), the interaction menu and the messages are English or Russian (see Language above); the scenario texts are English.

Binaries only. The suit cannot be sat in the EMPU's seat yet.
