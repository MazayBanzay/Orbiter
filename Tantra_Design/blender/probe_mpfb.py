# Probe: enable MPFB, report its operators related to asset packs and its data directories.
import bpy, os, traceback
LOG = os.path.join(os.path.dirname(__file__), "probe_mpfb.log")
out = []
try:
    import addon_utils
    mods = [m.__name__ for m in addon_utils.modules() if "mpfb" in m.__name__.lower()]
    out.append("modules: %s" % mods)
    for m in mods:
        addon_utils.enable(m, default_set=True, persistent=True)
    out.append("enabled: %s" % [m for m in bpy.context.preferences.addons.keys() if "mpfb" in m])
    ops = [o for o in dir(bpy.ops.mpfb)]
    out.append("mpfb ops (%d): %s" % (len(ops), ", ".join(ops)))
    try:
        from bl_ext.tantra.mpfb.services.locationservice import LocationService
        out.append("user data: %s" % LocationService.get_user_data())
        out.append("mpfb data: %s" % LocationService.get_mpfb_data())
    except Exception as e:
        out.append("locationservice: %r" % e)
    bpy.ops.wm.save_userpref()
except Exception:
    out.append(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(out))
