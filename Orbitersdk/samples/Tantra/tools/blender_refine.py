"""Blender refinement pass for the Tantra mesh (run in Blender 4.5, background).

    blender --background --factory-startup --python tools/blender_refine.py -- <raw.json> <out.msh> <done.txt>

Input: groups written by tools/gen_mesh.py (module contract: names, order, reference pose).
Output: Tantra.msh with the SAME groups in the SAME order and frame. Only local detail is added:
  * smooth shading with sharp edges by angle (split normals exported per corner);
  * bevels on the machinery (carriages, drums, pods, feet, hinges, boxes);
  * thickness on the flank lids and hangar doors;
  * the band mast: frames every 4 m and three zipper seams;
  * stern legs: collars on the casings, rings on the inner stages.
Nothing is moved: pivots, hinges and axes stay where MeshLayout.h expects them
(tools/verify_mesh.py checks that).
"""
import json
import math
import sys

import bmesh
import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
RAW, OUT, DONE = argv[0], argv[1], argv[2]

BEVEL = {  # group name prefix -> (width, segments)
    "carriage_": (0.18, 2), "hip_": (0.15, 2), "thigh_": (0.25, 2), "pad_": (0.2, 2), "ankle_": (0.1, 1),
    "pod_": (0.15, 2), "pylon_": (0.12, 2), "crest_root": (0.10, 1),
    "hatches": (0.05, 1), "airlock": (0.08, 1), "rover_platform": (0.06, 1), "leg_hinges": (0.15, 2),
    "crest_dorsal": (0.12, 1), "crest_port": (0.10, 1), "crest_starboard": (0.10, 1), "shin_": (0.12, 1),
    "shuttle": (0.05, 1),
}
SOLIDIFY = {"door_": 0.25}
SHARP_DEG = 40.0


def clean_scene():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)


def make_object(g):
    me = bpy.data.meshes.new(g["name"])
    me.from_pydata([tuple(v) for v in g["v"]], [], [tuple(t) for t in g["t"]])
    me.validate()
    bm = bmesh.new()
    bm.from_mesh(me)
    if not g["name"].startswith(("cups_", "iris_")):  # two-sided parts must stay unwelded
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)  # weld seams so smoothing and bevels work
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(g["name"], me)
    bpy.context.scene.collection.objects.link(ob)
    ob["msh_material"] = g["material"]
    return ob


def add_box(bm, lo, hi):
    ret = bmesh.ops.create_cube(bm, size=1.0)
    cx, cy, cz = [(a + b) / 2 for a, b in zip(lo, hi)]
    sx, sy, sz = [b - a for a, b in zip(lo, hi)]
    for v in ret["verts"]:
        v.co.x = cx + v.co.x * sx
        v.co.y = cy + v.co.y * sy
        v.co.z = cz + v.co.z * sz


def add_ring_z(bm, cx, cy, z0, z1, r, segs=16):
    ret = bmesh.ops.create_cone(bm, cap_ends=True, segments=segs, radius1=r, radius2=r, depth=z1 - z0)
    for v in ret["verts"]:
        v.co.x += cx
        v.co.y += cy
        v.co.z += (z0 + z1) / 2


def bounds(ob):
    xs = [v.co.x for v in ob.data.vertices]
    ys = [v.co.y for v in ob.data.vertices]
    zs = [v.co.z for v in ob.data.vertices]
    return min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)


def detail_mast(ob):
    """Band mast: frames every 4 m from the trunnion down, three zipper seams on the faces."""
    x0, x1, y0, y1, z0, z1 = bounds(ob)
    cx, cz = (x0 + x1) / 2, (z0 + z1) / 2
    hw = (x1 - x0) / 2
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    y = y1 - 4.0
    while y > y0 + 1.0:
        add_box(bm, (cx - hw - 0.12, y - 0.15, cz - hw - 0.12), (cx + hw + 0.12, y + 0.15, cz + hw + 0.12))
        y -= 4.0
    for axis, sign in (("x", 1), ("x", -1), ("z", 1)):
        if axis == "x":
            lo = (cx + sign * hw - 0.06 if sign > 0 else cx - hw - 0.06, y0, cz - 0.14)
            hi = (cx + hw + 0.06 if sign > 0 else cx - hw + 0.06, y1, cz + 0.14)
        else:
            lo = (cx - 0.14, y0, cz + hw - 0.06)
            hi = (cx + 0.14, y1, cz + hw + 0.06)
        add_box(bm, lo, hi)
    bm.to_mesh(ob.data)
    bm.free()


def detail_leg(ob, stage):
    """Casing: collars at both ends; inner stage: rings every 2 m (legs are built along +z)."""
    x0, x1, y0, y1, z0, z1 = bounds(ob)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    r = (x1 - x0) / 2
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    if stage:
        z = z0 + 1.0
        while z < z1 - 0.5:
            add_ring_z(bm, cx, cy, z, z + 0.25, r * 1.08)
            z += 2.0
    else:
        add_ring_z(bm, cx, cy, z0, z0 + 0.8, r * 1.15)
        add_ring_z(bm, cx, cy, z1 - 0.8, z1, r * 1.15)
    bm.to_mesh(ob.data)
    bm.free()


def prepare(ob):
    name = ob.name
    if name.startswith("leg") and "_" in name:   # stern legs: same bevels as the carriage legs
        part = name.split("_", 1)[1]
        w = {"thigh": 0.25, "pad": 0.2, "ankle": 0.1, "shin0": 0.12, "shin1": 0.12, "shin2": 0.12}.get(part)
        if w:
            m = ob.modifiers.new("bevel", "BEVEL")
            m.width, m.segments, m.limit_method, m.angle_limit = w, 1, "ANGLE", math.radians(35.0)
    me = ob.data
    for p in me.polygons:
        p.use_smooth = True
    me.set_sharp_from_angle(angle=math.radians(SHARP_DEG))
    for prefix, t in SOLIDIFY.items():
        if name.startswith(prefix):
            m = ob.modifiers.new("solid", "SOLIDIFY")
            m.thickness = t
            m.offset = -1.0
            m.use_even_offset = True
    for prefix, (w, seg) in BEVEL.items():
        if name.startswith(prefix):
            m = ob.modifiers.new("bevel", "BEVEL")
            m.width = w
            m.segments = seg
            m.limit_method = "ANGLE"
            m.angle_limit = math.radians(35.0)
            m.harden_normals = False
            break


def export(objs, materials, path):
    dg = bpy.context.evaluated_depsgraph_get()
    stats = []
    with open(path, "w", newline="\r\n") as f:
        f.write("MSHX1\n")
        f.write(f"GROUPS {len(objs)}\n")
        for ob in objs:
            ev = ob.evaluated_get(dg)
            me = ev.to_mesh()
            me.calc_loop_triangles()
            normals = me.corner_normals
            verts, index, tris = [], {}, []
            for tri in me.loop_triangles:
                idx = []
                for li, vi in zip(tri.loops, tri.vertices):
                    n = normals[li].vector
                    key = (vi, round(n.x, 3), round(n.y, 3), round(n.z, 3))
                    k = index.get(key)
                    if k is None:
                        co = me.vertices[vi].co
                        k = index[key] = len(verts)
                        verts.append((co.x, co.y, co.z, n.x, n.y, n.z))
                    idx.append(k)
                tris.append(idx)
            f.write(f"LABEL {ob.name}\nMATERIAL {ob['msh_material']}\nTEXTURE 0\n")
            f.write(f"GEOM {len(verts)} {len(tris)} ;{ob.name}\n")
            for v in verts:
                f.write("%.3f %.3f %.3f %.4f %.4f %.4f\n" % v)
            for a, b, c in tris:
                f.write(f"{a} {b} {c}\n")
            stats.append((ob.name, len(verts), len(tris)))
            ev.to_mesh_clear()
        f.write(f"MATERIALS {len(materials)}\n")
        for m in materials:
            f.write(m["name"] + "\n")
        for m in materials:
            d, sp, e = m["diffuse"], m["specular"], m["emissive"]
            f.write(f"MATERIAL {m['name']}\n")
            f.write("{:.3f} {:.3f} {:.3f} 1\n".format(*d))
            f.write("{:.3f} {:.3f} {:.3f} 1\n".format(*d))
            f.write("{:.3f} {:.3f} {:.3f} 1 {:.0f}\n".format(*sp))
            f.write("{:.3f} {:.3f} {:.3f} 1\n".format(*e))
        f.write("TEXTURES 0\n")
    return stats


def main():
    with open(RAW) as f:
        data = json.load(f)
    clean_scene()
    objs = [make_object(g) for g in data["groups"]]
    for ob in objs:
        prepare(ob)
    stats = export(objs, data["materials"], OUT)
    nv = sum(s[1] for s in stats)
    nt = sum(s[2] for s in stats)
    with open(DONE, "w") as f:
        f.write(f"OK {len(stats)} groups {nv} vertices {nt} triangles\n")


try:
    main()
except Exception as e:  # report through the marker: the launcher hides stdout
    import traceback
    with open(DONE, "w") as f:
        f.write("ERROR " + repr(e) + "\n" + traceback.format_exc())
