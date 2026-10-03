import bpy, os
HERE = os.path.dirname(__file__); L = os.path.join(HERE, "ping2.log"); log = []
def mark(t):
    log.append(t); open(L, "w").write("\n".join(log))
mark("a")
bpy.ops.wm.open_mainfile(filepath=os.path.join(HERE, "astronavigator_crewsuit.blend"))
mark("opened")
cov = bpy.data.objects["Coverall"]; mark("cov")
me = cov.data; mark("me %s" % me.name)
mark("uv layers %s" % [u.name for u in me.uv_layers])
mark("active %s" % me.uv_layers.active)
