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

### Heat damage (hot ground and lava, e.g. the Io lava field)

The machines burn on hot ground and lava. Past a material's onset, damage grows e-fold per 60 K: 30 min at the onset, about 4 min at +120 K. The damage stays and is saved in the scenario. It shows on the GEAR and MOTOR pages, in the driver's row and as HEAT on the yoke.

| Part | Material | Onset | What fails |
|---|---|---|---|
| Tyre (each wheel) | Mo-47.5Re spring mesh, C/SiC tread chevrons, 48 kJ/K | 1150 K | the wheel drags, then it is dead (locked) |
| Hub motor | glass-polyimide windings, SmCo magnets | 573 K | dead at 50 % (it trips at 473 K first) |
| Belly | CNT in phthalonitrile resin, 0.9 MJ/K, 25 m2 | 650 K | 30 % inverters (drive halved), 50 % battery shorts (2 %/min), 70 % harness (no drive), 100 % burnt through (EMPU: cabin floor holed) |

- **Radiators.** They see half ground and half sky. Over ~500 K ground they can no longer cool the motors.
- **Crust.** The crew's rule applies, scaled by sqrt(load). One EMPU wheel (3.5 kN on Io) breaks crusts under 5.6 cm and may break crusts under 14 cm. For the MPU the figures are 4.2 and 10.6 cm.
- **In the melt.** A wheel settles 0.15-0.30 m (0.45 m in the lake). It keeps 30 % of its grip and drags through a melt of ~300 Pa s.

EMPU on Io (computed by `Orbitersdk/samples/TVehicles/tests/run_heat_test.bat` on the field's own data):

| Situation | Motors trip | Motors burnt | Belly onset | Belly burnt through |
|---|---|---|---|---|
| Parked on a 2 cm crust over the 1450 K melt (crust 861 K; the wheels break through) | 4.7 min | 8.8 min | 9.4 min | 15 min |
| Driving across 600 K ground at 30 km/h | 32 min | never | never | never |
| Parked on 600 K ground | 35 min | never | never | never |
| Parked on 700 K ground | 17 min | 41 min | 34 min | 53 min |

- **2 cm crust:** the tyres reach about 1000 K and stay below their onset.
- **600 K ground:** the belly settles at 593 K. The machine stops because its motors trip, but nothing burns.
- **700 K ground:** the belly passes its onset at 34 min.

All numbers and materials are in `Orbitersdk/samples/TVehicles/TVHeat.h`.

### Titan (cold dense nitrogen, damp ground)

- **Cabin insulation.** MLI works only in vacuum. In a gas the 4 cm aerogel blanket under it carries the load. The hull's conductance is 15 W/K in vacuum, ~20 on Mars, ~30 on Earth and ~36 on Titan.
- **Heating.** On Titan the cabin needs ~7 kW. The heater now gives up to 12 kW, drawn from the cells.
- **Ground.** Each wheel reads the site's ground raster, the same file the suit reads: `Config/OrbiterCrew/Ground/Titan.cfg` with its `.bin`. Without the raster, Titan is damp sand. All values are estimates from Huygens:

| Ground | Cohesion | Friction | Notes |
|---|---|---|---|
| Damp organic sand | 2 kPa | 25 deg | wheels sink ~3 cm |
| Rounded ice pebbles | none | 38 deg | stiff |
| Bedrock ice | none | 27 deg | |
| Wet margin | 1 kPa | 20 deg | soft |
| Sea bed | 0.5 kPa | 15 deg | the liquid's buoyancy and drag are not modelled |

- **Stuck sand.** Damp sand builds up in the wheel mesh: e-fold every ~300 m on sand, every ~80 m on the wet margin. Pebbles, ice and speed knock it off. A clogged wheel loses up to 25 % grip and rolls 3 % harder. It shows on the GEAR page and is saved.
- **Pebbles.** Over pebbles the wheels are struck at v / 0.12 m: up to half their load, so the machine shakes.
- **The seas (Kraken Mare).** The liquid level comes from the ground raster (`LiquidLevel`). The sea is a methane-ethane-nitrogen mix at ~520 kg/m3 and 93 K, an electrical insulator.
  - *Wading:* the liquid lifts the machine, so its wheels lose load and grip, and it drags.
  - *EMPU (15.6 t):* floats at ~2.2 m depth, stable. Its wheels hang fully under and cannot paddle, so it drifts. A wheel only half under paddles a little.
  - *MPU (9 t):* never floats. In deep liquid it stays on the bed with its wheels 76 % unloaded.
  - *A holed cabin* floods: the outside pressure is higher than the cabin's 99 kPa, so the liquid comes in, and its weight sinks the machine.
  - *Cold:* the liquid chills whatever it wets (boiling film): tyres, belly, hub motors. The cabin's hull loses its outer gas film.
  - *Bubbles:* warm parts give off dissolved nitrogen (the AtmoFX fork draws it).
  - *Time acceleration:* above 2x no liquid forces apply; the machine settles on the bed and lifts off again at 1x.
- **Heat shimmer.** In an atmosphere, with the AtmoFX D3D9 fork, the air shimmers over the warm parts: the wheel hubs (hub motor or tyre, whichever is hotter), the belly, and the EMPU cabin's skin. On Titan the skin runs ~17 K above the air.
- **Vented air.** Air vented from the EMPU (an airlock cycle, a hole in the hull) freezes into a puff of ice crystals. In dense cold gas it hangs low and settles over ~9 s; in vacuum it flashes out.

The suit computer (H, eyes view), the interaction menu and the messages are English or Russian (see Language above); the scenario texts are English.

Binaries only. The suit cannot be sat in the EMPU's seat yet.
