"""Build the MPU removable battery mesh and its D3D9 diffuse/normal/spec maps.

Run after build_mpu.py when either vehicle mesh is regenerated. The helper also patches
the four battery groups in both in-game vehicle meshes and writes the standalone hand mesh.
"""
from pathlib import Path
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
MESH_DIR = ROOT / "Meshes" / "MPU"
TEX_DIR = ROOT / "Textures" / "MPU"
TEX_DIR.mkdir(parents=True, exist_ok=True)
N = 1024


def dds(path, rgba, normal=False):
    arr = np.asarray(rgba, dtype=np.float32)
    if arr.ndim == 2:
        arr = np.repeat(arr[..., None], 3, axis=2)
    if arr.shape[2] == 3:
        arr = np.concatenate([arr, np.full((*arr.shape[:2], 1), 255.0)], axis=2)
    levels = [np.clip(arr, 0, 255)]
    while levels[-1].shape[0] > 1 or levels[-1].shape[1] > 1:
        src = Image.fromarray(np.clip(levels[-1], 0, 255).astype(np.uint8), "RGBA")
        wh = (max(1, src.width // 2), max(1, src.height // 2))
        dst = np.asarray(src.resize(wh, Image.Resampling.BOX), dtype=np.float32)
        if normal:
            v = dst[..., :3] / 127.5 - 1.0
            v /= np.maximum(1e-6, np.linalg.norm(v, axis=2, keepdims=True))
            dst[..., :3] = (v * 0.5 + 0.5) * 255.0
        levels.append(dst)
    h, w = levels[0].shape[:2]
    header = bytearray(128)
    header[0:4] = b"DDS "
    for off, val in {4: 124, 8: 0x000A100F, 12: h, 16: w, 20: w * 4, 28: len(levels),
                     76: 32, 80: 0x41, 88: 32, 92: 0x00FF0000, 96: 0x0000FF00,
                     100: 0x000000FF, 104: 0xFF000000, 108: 0x401008}.items():
        header[off:off + 4] = int(val).to_bytes(4, "little")
    with path.open("wb") as f:
        f.write(header)
        for level in levels:
            u8 = np.clip(np.round(level), 0, 255).astype(np.uint8)
            f.write(np.ascontiguousarray(u8[..., [2, 1, 0, 3]]).tobytes())


def make_maps():
    rng = np.random.default_rng(23015)
    color = np.empty((N, N, 3), dtype=np.float32)
    color[:] = (67, 73, 77)
    height = np.zeros((N, N), dtype=np.float32)
    # Main powder-coated aluminium sheet: brushed grain, machined seams and fasteners.
    y, x = np.mgrid[:512, :N]
    grain = rng.normal(0, 1.5, (512, N)) + 2.0 * np.sin(x * 2 * math.pi / 7.0)
    color[:512] = np.stack([116 + grain, 123 + grain, 128 + grain], axis=2)
    h = height[:512]
    draw_h = Image.fromarray(np.uint8(np.clip(height + 128, 0, 255)))
    dh = ImageDraw.Draw(draw_h)
    color_image = Image.fromarray(np.uint8(np.clip(color, 0, 255)))
    dc = ImageDraw.Draw(color_image)
    dc.rectangle((8, 8, 1015, 503), outline=(48, 55, 60), width=8)
    dc.rectangle((20, 20, 1003, 491), outline=(176, 182, 185), width=3)
    dh.rectangle((8, 8, 1015, 503), outline=109, width=7)
    dh.rectangle((20, 20, 1003, 491), outline=142, width=2)
    for px in (36, 988):
        for py in (36, 476):
            dc.ellipse((px - 12, py - 12, px + 12, py + 12), fill=(64, 70, 74), outline=(185, 190, 190), width=3)
            dc.line((px - 6, py, px + 6, py), fill=(35, 39, 42), width=3)
            dh.ellipse((px - 12, py - 12, px + 12, py + 12), fill=112, outline=154, width=2)
    # Sub-panels and cooling ribs on the broad shell face.
    for k in range(1, 7):
        px = 80 + k * 128
        dc.line((px, 44, px, 468), fill=(91, 99, 104), width=4)
        dc.line((px + 5, 44, px + 5, 468), fill=(148, 154, 157), width=2)
        dh.line((px, 44, px, 468), fill=92, width=3)
        dh.line((px + 5, 44, px + 5, 468), fill=147, width=2)
    # Dark recessed service panel atlas quadrant.
    dc.rectangle((8, 520, 500, 1015), fill=(45, 51, 55), outline=(173, 180, 184), width=10)
    dc.rectangle((28, 540, 480, 995), outline=(20, 24, 27), width=5)
    for px in (42, 466):
        for py in (554, 981):
            dc.ellipse((px - 9, py - 9, px + 9, py + 9), fill=(159, 166, 169), outline=(12, 15, 17), width=4)
    dc.text((65, 565), "ENERGY CELL  /  250 kWh", fill=(198, 205, 207), font=ImageFont.load_default())
    dc.rectangle((64, 610, 444, 824), fill=(27, 32, 35), outline=(101, 111, 116), width=5)
    dc.text((84, 632), "Li-ion  |  120 V DC", fill=(171, 181, 184), font=ImageFont.load_default())
    for i in range(5):
        dc.rectangle((86, 684 + i * 25, 410, 690 + i * 25), fill=(86, 96, 101))
    dh.rectangle((8, 520, 500, 1015), outline=102, width=8)
    dh.rectangle((28, 540, 480, 995), outline=56, width=5)
    # Safety marking, textured grip, and terminal wells occupy separate UV islands.
    dc.rectangle((520, 520, 760, 760), fill=(215, 160, 34), outline=(48, 52, 54), width=8)
    for px in range(500, 790, 38):
        dc.polygon([(px, 520), (px + 18, 520), (px - 100, 760), (px - 118, 760)], fill=(39, 43, 46))
    dh.rectangle((520, 520, 760, 760), outline=82, width=7)
    dc.rectangle((780, 520, 1015, 760), fill=(104, 112, 116), outline=(198, 202, 203), width=8)
    for px in range(800, 1005, 14):
        dc.line((px, 536, px, 744), fill=(67, 74, 78), width=4)
        dc.line((px + 4, 536, px + 4, 744), fill=(157, 164, 166), width=2)
    dh.rectangle((780, 520, 1015, 760), outline=148, width=6)
    dc.rectangle((520, 780, 1015, 1015), fill=(39, 45, 49), outline=(176, 183, 186), width=8)
    for px in (650, 880):
        dc.ellipse((px - 72, 830, px + 72, 974), fill=(20, 24, 27), outline=(159, 167, 170), width=10)
        dc.ellipse((px - 48, 854, px + 48, 950), fill=(95, 106, 111), outline=(36, 42, 46), width=8)
        dc.ellipse((px - 27, 875, px + 27, 929), fill=(29, 35, 39), outline=(190, 150, 50), width=7)
        dh.ellipse((px - 72, 830, px + 72, 974), outline=158, width=8)
    color = np.asarray(color_image, dtype=np.float32)
    # Build a tangent-space normal and a low-gloss metal specular map.
    hh = np.asarray(draw_h, dtype=np.float32) - 128.0
    dx = (np.roll(hh, -1, axis=1) - np.roll(hh, 1, axis=1)) * 0.5
    dy = (np.roll(hh, -1, axis=0) - np.roll(hh, 1, axis=0)) * 0.5
    nrm = np.stack([-dx * 1.2, dy * 1.2, np.ones_like(hh)], axis=2)
    nrm /= np.maximum(1e-6, np.linalg.norm(nrm, axis=2, keepdims=True))
    norm = np.uint8(np.clip((nrm * 0.5 + 0.5) * 255, 0, 255))
    spec = np.zeros((N, N, 4), dtype=np.uint8)
    spec[:, :, :3] = (75, 80, 84)
    spec[:, :, 3] = 32  # matte metal, exponent 8 with the D3D9Client's alpha/4 convention
    dds(TEX_DIR / "mpu_cell.dds", np.dstack([np.clip(color, 0, 255), np.full((N, N), 255)]).astype(np.uint8))
    dds(TEX_DIR / "mpu_cell_norm.dds", norm, normal=True)
    dds(TEX_DIR / "mpu_cell_spec.dds", spec)


verts, faces = [], []


def quad(points, normal, rect):
    p0, p1, p2 = points[:3]
    a = (p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2])
    b = (p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2])
    cross = (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
    if sum(cross[i] * normal[i] for i in range(3)) < 0:
        points = [points[0], points[3], points[2], points[1]]
    base = len(verts)
    u0, v0, u1, v1 = rect
    uv = ((u0, v0), (u1, v0), (u1, v1), (u0, v1))
    for p, (u, v) in zip(points, uv):
        verts.append((*p, *normal, u, v))
    faces.extend(((base, base + 1, base + 2), (base, base + 2, base + 3)))


def box(c, h, rect):
    x, y, z = c; a, b, d = h
    # Corners are ordered so the listed normal points out of each face.
    fs = [
        ([(x+a,y-b,z-d),(x+a,y+b,z-d),(x+a,y+b,z+d),(x+a,y-b,z+d)], (1,0,0)),
        ([(x-a,y-b,z+d),(x-a,y+b,z+d),(x-a,y+b,z-d),(x-a,y-b,z-d)], (-1,0,0)),
        ([(x-a,y+b,z-d),(x-a,y+b,z+d),(x+a,y+b,z+d),(x+a,y+b,z-d)], (0,1,0)),
        ([(x-a,y-b,z+d),(x-a,y-b,z-d),(x+a,y-b,z-d),(x+a,y-b,z+d)], (0,-1,0)),
        ([(x+a,y-b,z+d),(x+a,y+b,z+d),(x-a,y+b,z+d),(x-a,y-b,z+d)], (0,0,1)),
        ([(x-a,y-b,z-d),(x-a,y+b,z-d),(x+a,y+b,z-d),(x+a,y-b,z-d)], (0,0,-1)),
    ]
    for p, n in fs: quad(p, n, rect)


def cylinder_y(x, z, y0, y1, radius, rect, steps=16):
    for i in range(steps):
        a0, a1 = math.tau * i / steps, math.tau * (i + 1) / steps
        x0, z0 = x + radius * math.cos(a0), z + radius * math.sin(a0)
        x1, z1 = x + radius * math.cos(a1), z + radius * math.sin(a1)
        n = (math.cos((a0+a1)/2), 0, math.sin((a0+a1)/2))
        quad([(x0,y0,z0),(x1,y0,z1),(x1,y1,z1),(x0,y1,z0)], n, rect)
    box((x, y1, z), (radius, 0.002, radius), rect)


def build_geo(center=(0.0, 0.0, 0.0)):
    global verts, faces
    verts, faces = [], []
    body = (0.0, 0.0, 1.0, 0.5)      # grey anodized panel island
    dark = (0.02, 0.52, 0.49, 0.98)  # recessed service panel
    warn = (0.52, 0.52, 0.74, 0.74)  # hazard band
    grip = (0.77, 0.52, 0.99, 0.74)  # machined grip
    conn = (0.52, 0.77, 0.99, 0.99)  # electrical terminals
    # Main aluminium shell, with a recessed, framed front service panel.
    box((0, 0, 0), (0.172, 0.166, 0.119), body)
    box((0, 0.005, 0.121), (0.118, 0.106, 0.006), grip)
    box((0, 0.005, 0.128), (0.096, 0.083, 0.004), dark)
    box((0, 0.108, 0.128), (0.125, 0.018, 0.004), warn)
    # Protective corner rails and cooling ribs keep the cartridge legible at game distance.
    for sx in (-1, 1):
        box((sx * 0.163, 0, 0.124), (0.012, 0.15, 0.010), grip)
        box((sx * 0.179, 0, 0), (0.008, 0.15, 0.095), body)
        box((sx * 0.12, 0.18, 0), (0.012, 0.025, 0.035), grip)
    box((0, 0.196, 0), (0.135, 0.012, 0.035), grip)  # built-in lift handle, not a cake-box tab
    for z in (-0.075, -0.035, 0.005, 0.045, 0.085):
        box((0, -0.005, z), (0.152, 0.008, 0.006), grip)
    for x in (-0.075, 0.075):
        cylinder_y(x, 0, 0.168, 0.177, 0.035, conn)
        cylinder_y(x, 0, 0.177, 0.182, 0.021, dark)
    if center != (0.0, 0.0, 0.0):
        cx, cy, cz = center
        verts = [(x+cx, y+cy, z+cz, nx, ny, nz, u, v) for x,y,z,nx,ny,nz,u,v in verts]
    return verts, faces


def mesh_group(label, material, texture, center, scale=1.0):
    v, f = build_geo(center)
    if scale != 1.0:
        v = [(x*scale, y*scale, z*scale, nx, ny, nz, u, vv) for x,y,z,nx,ny,nz,u,vv in v]
    out = [f"LABEL {label}\n", f"MATERIAL {material}\n", f"TEXTURE {texture}\n", f"GEOM {len(v)} {len(f)}\n"]
    out.extend("%.5f %.5f %.5f %.4f %.4f %.4f %.5f %.5f\n" % tuple(row) for row in v)
    out.extend("%d %d %d\n" % tri for tri in f)
    return "".join(out)


def update_mesh(path):
    lines = path.read_text(encoding="ascii").splitlines()
    n_groups = int(lines[1].split()[1])
    mat_at = next(i for i, line in enumerate(lines) if line.startswith("MATERIALS "))
    mat_count = int(lines[mat_at].split()[1])
    name_at = mat_at + 1
    mat_names = lines[name_at:name_at + mat_count]
    cell_mat = mat_names.index("MPU_Cell") + 1 if "MPU_Cell" in mat_names else mat_count + 1
    texture_at = next(i for i, line in enumerate(lines) if line.startswith("TEXTURES "))
    tex_count = int(lines[texture_at].split()[1])
    tex_names = lines[texture_at + 1:texture_at + 1 + tex_count]
    cell_tex = tex_names.index("MPU\\mpu_cell.dds") + 1 if "MPU\\mpu_cell.dds" in tex_names else tex_count + 1
    group_heads = [i for i, line in enumerate(lines[:mat_at]) if line.startswith("LABEL ")]
    if len(group_heads) != n_groups:
        raise RuntimeError(f"{path.name}: expected {n_groups} groups, found {len(group_heads)}")
    groups = []
    for j, start in enumerate(group_heads):
        end = group_heads[j + 1] if j + 1 < len(group_heads) else mat_at
        block = lines[start:end]
        label = block[0].split(maxsplit=1)[1]
        if label.startswith("CellPack"):
            k = int(label[len("CellPack")])
            center = (1.01, 0.005, (0.675, 0.225, -0.225, -0.675)[k])
            block = mesh_group(f"CellPack{k}_Cell", cell_mat, cell_tex, center).splitlines()
        groups.append(block)
    header = ["MSHX1", f"GROUPS {n_groups}"]
    body = [line for block in groups for line in block]
    # Retain every existing material definition and append the battery coating once.
    names_end = name_at + mat_count
    defs = lines[names_end:texture_at]
    names = list(mat_names)
    if "MPU_Cell" not in names:
        names.append("MPU_Cell")
        defs.extend(["MATERIAL MPU_Cell", "0.720 0.750 0.770 1.00", "0.720 0.750 0.770 1.00", "0.42 0.44 0.46 1 18", "0.000 0.000 0.000 1"])
    textures = list(tex_names)
    if "MPU\\mpu_cell.dds" not in textures:
        textures.append("MPU\\mpu_cell.dds")
    result = header + body + [f"MATERIALS {len(names)}"] + names + defs + [f"TEXTURES {len(textures)}"] + textures
    path.write_text("\n".join(result) + "\n", encoding="ascii", newline="\r\n")


def main():
    make_maps()
    # One physical cartridge model is shared by the vehicle bay and the astronaut's hands.
    group = mesh_group("BatteryCell", 1, 1, (0.0, 0.0, 0.0))
    held_group = mesh_group("BatteryCellHeld", 1, 1, (0.0, 0.0, 0.0), scale=25.0)
    cell = ["MSHX1", "GROUPS 1", *group.splitlines(), "MATERIALS 1", "MPU_Cell",
            "MATERIAL MPU_Cell", "0.720 0.750 0.770 1.00", "0.720 0.750 0.770 1.00",
            "0.42 0.44 0.46 1 18", "0.000 0.000 0.000 1", "TEXTURES 1", "MPU\\mpu_cell.dds"]
    (MESH_DIR / "BatteryCell.msh").write_text("\n".join(cell) + "\n", encoding="ascii", newline="\r\n")
    cell_held = ["MSHX1", "GROUPS 1", *held_group.splitlines(), "MATERIALS 1", "MPU_Cell",
                 "MATERIAL MPU_Cell", "0.720 0.750 0.770 1.00", "0.720 0.750 0.770 1.00",
                 "0.42 0.44 0.46 1 18", "0.000 0.000 0.000 1", "TEXTURES 1", "MPU\\mpu_cell.dds"]
    (MESH_DIR / "BatteryCellHeld.msh").write_text("\n".join(cell_held) + "\n", encoding="ascii", newline="\r\n")
    for name in ("MPU", "EMPU"):
        update_mesh(MESH_DIR / f"{name}.msh")
    print("battery cell mesh, texture and normal/spec maps written")


if __name__ == "__main__":
    main()
