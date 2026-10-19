# Pantsir game asset: load the approved P3 «Гибрид» generator (build_pantsir_3d.py) as a library inside Blender -
# its functions without the render main(), with lighter tessellation for the game. Read only: the generator file
# itself is not changed; its source is executed in a private namespace.
import os, math
HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.abspath(os.path.join(HERE, "..", "build_pantsir_3d.py"))


def load(game=True):
    src = open(GEN, encoding="utf-8").read()
    cut = src.rindex("\ntry:\n    main()")                                   # drop the render entry point
    ns = {"__name__": "pantsir_lib", "__file__": GEN}
    exec(compile(src[:cut], GEN, "exec"), ns)
    if game:
        # game tessellation: fewer segments round the limbs and drives; no fins, no bolts (flat ring faces instead)
        _loft4, _bands, _cyl, _ringax = ns["loft4"], ns["shield_bands"], ns["cyl"], ns["ring_motor_ax"]
        def loft4(rows, m, name, seg=64, sharp=1.0, part=None, cap=(True, True)): return _loft4(rows, m, name, min(seg, 36), sharp, part, cap)
        def shield_bands(*a, **k):
            k["nt"] = min(k.get("nt", 120), 36); return _bands(*a, **k)
        def cyl(p1, p2, r1, m, name, r2=None, seg=32, part=None): return _cyl(p1, p2, r1, m, name, r2, max(6, min(seg, 10 if r1 < 0.02 else 18)), part)
        _rbox = ns["rbox"]
        def rbox(c, size, m, name, q=None, r=0.004, seg=2, part=None): return _rbox(c, size, m, name, q, 0.0 if min(size) < 0.012 else min(r, 0.003), 1, part)   # one chamfer; thin parts plain
        ns.update(loft4=loft4, shield_bands=shield_bands, cyl=cyl, rbox=rbox, rib=lambda *a, **k: None, bolt=lambda *a, **k: None)
    return ns
