#!/bin/bash
set -e
CFG=../../Config/Io/LavaField/Effects.cfg
python -X utf8 build.py effects
python -X utf8 build.py glowmesh
python -X utf8 skin.py lobe
python -X utf8 skin.py vent
python -X utf8 build.py footprints
cat keep_lines.tmp >> "$CFG"
echo REBUILD TAIL DONE
