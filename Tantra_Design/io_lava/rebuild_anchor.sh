#!/bin/bash
# rebuild everything that hangs on the IoLava vessels' anchors (lights, glow mesh, lobe skin, pit lining, footprints)
set -e
CFG=../../Config/Io/LavaField/Effects.cfg


python -X utf8 build.py effects
python -X utf8 build.py glowmesh
python -X utf8 skin.py lobe
python -X utf8 skin.py vent
python -X utf8 build.py footprints
cat keep_lines.tmp >> "$CFG"
echo REBUILD DONE
