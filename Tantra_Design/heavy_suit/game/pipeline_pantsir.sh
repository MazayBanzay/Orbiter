#!/bin/bash
# «Панцирь» game asset, whole chain: build -> UV + bakes -> compose -> apply -> export. Stops at the first step that
# did not finish (each Blender step must write its "saved" line), so a half-done blend never reaches the game.
set -u
cd "$(dirname "$0")"
B="$LOCALAPPDATA/Microsoft/WindowsApps/BlenderFoundation.Blender_ppwjx1n5r4v9t/blender-launcher.exe"
wait_blender() { sleep 3; until ! tasklist | grep -qi "blender.exe"; do sleep 10; done; }
step() { echo "[$(date +%H:%M:%S)] $*"; }
step build;   "$B" -b ../../blender/astronavigator_suit.blend --python build_pantsir_game_S.py >/dev/null 2>&1; wait_blender
grep -q "^saved" build_pantsir_game_S.log || { step "build FAILED"; exit 1; }
step bake;    UVBAKE_STAGE=bake "$B" -b ../../blender/astronavigator_pantsir.blend --python uvbake_pantsir.py >/dev/null 2>&1; wait_blender
grep -q "^saved" uvbake_pantsir.log && grep -q "^bake S" uvbake_pantsir.log || { step "bake FAILED"; tail -5 uvbake_pantsir.log; exit 1; }
step compose; python compose_pantsir.py 2>&1 | grep -v Deprecat | tail -6 || exit 1
step apply;   UVBAKE_STAGE=apply "$B" -b ../../blender/astronavigator_pantsir.blend --python uvbake_pantsir.py >/dev/null 2>&1; wait_blender
grep -q "^saved" uvbake_pantsir.log || { step "apply FAILED"; exit 1; }
step export;  (cd ../../blender && TANTRA_VARIANT=pantsir "$B" -b astronavigator_pantsir.blend --python export_skin.py >/dev/null 2>&1); wait_blender
grep -q "retarget" ../../blender/export_skin.log && ! grep -q Traceback ../../blender/export_skin.log || { step "export FAILED"; exit 1; }
python fix_visor_msh.py ../../../Meshes/Tantra/AstronavigatorPantsir.msh || { step "visor fix FAILED"; exit 1; }
python -I fix_helmet_inside.py ../../../Meshes/Tantra/AstronavigatorPantsir.msh ../../../Config/Tantra/AstronavigatorPantsir.skin || { step "helmet inside FAILED"; exit 1; }
step done; ls -l --time-style=+%H:%M:%S ../../../Meshes/Tantra/AstronavigatorPantsir.msh
