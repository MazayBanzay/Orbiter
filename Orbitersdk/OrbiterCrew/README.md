# OrbiterCrew

Living crew members for Orbiter 2016: skinned, mocap-animated people who walk, run, jump, breathe and tire.
Focus: animation and a physically grounded life-support model. Open source, GPL-3.0-or-later.

## Crew member (module `OrbiterCrew\CrewMember`)
- CPU-skinned mesh; idle/walk/run mocap blended by speed, gait phase locked to distance (no foot skating).
- Procedural layers: breathing (rate/depth from physiology), weight shifts and glances at rest, lean into
  acceleration and braking, bank into turns, crouch on landing. Critically damped springs, no snapping.
- Ground movement is kinematic (landed state): friction-limited acceleration, braking and turning; jumps are
  ballistic flight in Orbiter's physics with a landing back into the landed state.
- Life support: gas mix per planet (`Config\OrbiterCrew\Atmospheres.cfg`), suit O2 tank / CO2 sorbent / battery,
  metabolism from activity, stamina (anaerobic reserve), hypoxia, CO2 narcosis, vacuum exposure.

Controls on the ground: W/S move (S brakes), A/D turn, Q/E side step, Shift run, Space jump, K suit.

## Build
VS2022, `CrewMember.vcxproj`, Release|Win32 -> `Modules\OrbiterCrew\CrewMember.dll`
(uses `Orbitersdk\resources\Orbiter.props`).

## Assets
Meshes, skeletons and clips come from the Blender pipeline in `Tantra_Design/blender`
(MakeHuman/MPFB CC0 assets, CMU motion capture database).
