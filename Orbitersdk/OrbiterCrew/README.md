# OrbiterCrew

Living crew members for Orbiter 2024 (x86): skinned, mocap-animated people who walk, run, jump, breathe and tire,
walk inside ships and sit at their consoles. The person comes first: the body in Orbiter's world is only where he or
she is now. Focus: animation and a physically grounded life-support model. Open source, GPL-3.0-or-later.

## Crew member (module `OrbiterCrew\CrewMember`)
- CPU-skinned mesh; idle/walk/run mocap blended by speed, gait phase locked to distance (no foot skating).
- Procedural layers: breathing (rate/depth from physiology), weight shifts and glances at rest, lean into
  acceleration and braking, bank into turns, crouch on landing. Critically damped springs, no snapping.
- Ground movement is kinematic (landed state): friction-limited acceleration, braking and turning; jumps are
  ballistic flight in Orbiter's physics with a landing back into the landed state.
- Life support: gas mix per planet (`Config\OrbiterCrew\Atmospheres.cfg`), suit O2 tank / CO2 sorbent / battery,
  metabolism from activity, stamina (anaerobic reserve), hypoxia, CO2 narcosis, vacuum exposure, body heat,
  water and food, injuries by part of the body, radiation dose.
- The person (`Person`, the crew registry) is kept apart from the body: aboard a ship without a body, saved in the
  ship's scenario block, the same person with the same organism and suit when the body comes back.
- Inside a ship (`include/OrbiterCrewApi.h`, e.g. «Тантра»): the same body walks in through a lift or an airlock,
  walks on the ship's floors between its walls, sits in the seat she comes up to, presses the ship's buttons and touch
  screens with the mouse. The camera always stays with the person: through her eyes, or from outside at the same
  distance.
- Her eyes are an empty virtual cockpit: no ship instruments over them. In the suit, its computer draws the helmet
  display (Russian only).

## Controls
- W / S move (S brakes), A / D turn, Q / E side step, Shift run (in the suit: servo boost), Space jump.
- Right mouse button held, through her eyes: the mouse turns her head; inside a ship, walking, she turns with it and
  A / D step aside. Released: the cursor is free.
- Left mouse inside a ship: the ship's buttons and screens within her reach.
- F the action: an entrance, a seat; in a seat F stands up.
- V seated at a helm: the ship from outside; V again: her own view.
- K suit on / off (off only in breathable air), H the suit computer's display, Shift+V sun shade, L helmet lamps,
  B take / leave the jet pack (its keys: `src/JetPack.h`).
- `DebugLog = 1` in the body's config writes test lines to Orbiter.log (clicks, the head camera).

## Build
VS2022 Build Tools, `CrewMember.vcxproj`, Release|Win32 -> `Modules\OrbiterCrew\CrewMember.dll`
(uses `Orbitersdk\resources\Orbiter.props`, Orbiter 2024 SDK, /MD, XRSound.lib).

## Assets
Meshes, skeletons and clips come from the Blender pipeline in `Tantra_Design/blender`
(MakeHuman/MPFB CC0 assets, CMU and 100STYLE motion capture).
The crew suit is built on `elvs_racing_fire_suit_female1` by Elvaerwyn (MakeHuman Community, CC-BY 4.0); the hair is
`cortu_strawberry_cloud_hair` (CC0).
