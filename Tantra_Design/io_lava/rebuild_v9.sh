#!/bin/bash
set -e
CFG=../../Config/Io/LavaField/Effects.cfg
python -X utf8 build.py elev thermal
python -X utf8 build.py surf
python -X utf8 build.py effects
cat keep_lines.tmp >> "$CFG"
echo REBUILD V9 DONE
