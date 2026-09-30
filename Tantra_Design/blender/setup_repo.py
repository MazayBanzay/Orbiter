# Moves MPFB into a short-path local extension repository (Windows MAX_PATH with the Store build of Blender)
# and points MPFB user data to a short directory. Then reinstalls the asset packs there.
import bpy, os, traceback
HERE = os.path.dirname(__file__); ROOT = os.path.dirname(HERE)
REPO_DIR = os.path.join(ROOT, "bx"); USER_DIR = os.path.join(ROOT, "mpfbu"); ASSETS = os.path.join(ROOT, "assets")
LOG = os.path.join(HERE, "setup_repo.log"); log = []
def L(*a): log.append(" ".join(str(x) for x in a))
try:
    os.makedirs(REPO_DIR, exist_ok=True); os.makedirs(USER_DIR, exist_ok=True)
    prefs = bpy.context.preferences
    repos = prefs.extensions.repos
    L("repos before:", [(r.module, r.directory, r.enabled) for r in repos])
    repo = next((r for r in repos if r.module == "tantra"), None)
    if repo is None:
        repo = repos.new(name="tantra", module="tantra", custom_directory=REPO_DIR, remote_url="", source='USER')
        repo.use_custom_directory = True; repo.custom_directory = REPO_DIR
    L("repo:", repo.module, repo.directory)
    import addon_utils
    for m in [m for m in prefs.addons.keys() if m.endswith(".mpfb")]:
        addon_utils.disable(m, default_set=True); L("disabled", m)
    r = bpy.ops.extensions.package_install_files(repo="tantra", filepath=os.path.join(ASSETS, "add-on-mpfb-v2.0.17.zip"), enable_on_install=True)
    L("install:", r)
    L("tantra repo contents:", os.listdir(REPO_DIR))
    try:
        r = bpy.ops.extensions.package_uninstall(repo_index=[i for i, x in enumerate(repos) if x.module == "user_default"][0], pkg_id="mpfb"); L("uninstall user_default.mpfb:", r)
    except Exception as e: L("uninstall old:", repr(e))
    mod = "bl_ext.tantra.mpfb"
    addon_utils.enable(mod, default_set=True)
    ap = prefs.addons[mod].preferences
    ap.mpfb_user_data = USER_DIR
    L("mpfb_user_data =", ap.mpfb_user_data)
    bpy.ops.wm.save_userpref()
except Exception:
    L(traceback.format_exc())
open(LOG, "w", encoding="utf-8").write("\n".join(log))
