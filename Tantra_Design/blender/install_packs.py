# Installs the downloaded MakeHuman asset packs into MPFB's user data.
import bpy, os, traceback
HERE = os.path.dirname(__file__)
ASSETS = os.path.join(os.path.dirname(HERE), "assets")
LOG = os.path.join(HERE, "install_packs.log")
PACKS = ["makehuman_system_assets_cc0.zip", "skins01_cc0.zip", "hair01_cc0.zip", "eyebrows01_cc0.zip", "eyelashes01_cc0.zip"]
out = []
try:
    import addon_utils
    addon_utils.enable("bl_ext.tantra.mpfb", default_set=True, persistent=True)
    for p in PACKS:
        fp = os.path.join(ASSETS, p)
        try:
            r = bpy.ops.mpfb.load_pack(filepath=fp)
            out.append("%s -> %s" % (p, r))
        except Exception as e:
            out.append("%s FAILED: %r" % (p, e))
    from bl_ext.tantra.mpfb.services.locationservice import LocationService
    ud = LocationService.get_user_data()
    for d in sorted(os.listdir(ud)):
        full = os.path.join(ud, d)
        if os.path.isdir(full):
            out.append("  %s: %d entries" % (d, len(os.listdir(full))))
    skins = os.path.join(ud, "skins")
    if os.path.isdir(skins): out.append("skins: " + ", ".join(sorted(os.listdir(skins))))
    hair = os.path.join(ud, "hair")
    if os.path.isdir(hair): out.append("hair: " + ", ".join(sorted(os.listdir(hair))))
    prox = os.path.join(ud, "proxymeshes")
    if os.path.isdir(prox): out.append("proxymeshes: " + ", ".join(sorted(os.listdir(prox))))
except Exception:
    out.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(out))
