# «Панцирь» (class 2 heavy suit) - the 3D model from the approved sketches, revision G:
#   body  : sketch/build_sketch_tile_rad.py  (front / side / back outlines, dimensions in metres)
#   helmet: sketch/build_sketch_helmets.py   variant V1 (k1): hinge behind the crown, cap lifts 65 deg up-back
# Rev. G (user 2026-10-16): anatomical hard shells instead of cones (thigh wide at the hip, knee cap, calf, deltoid, forearm
# wider at the elbow), no chest console, style "Soviet engineering + 500 years + techno-frame": matte enamel, signal red,
# black rubber, stencilled Cyrillic markings, fasteners and hatches, a visible structural frame of flat channel profiles
# (spine beam carrying the door, pelvis ring, outer leg members hip-knee-ankle with a stirrup, arm members, shoulder yoke)
# with the drives as compact housings in its nodes; tiles 8 mm, warm matte ceramic, stencil numbers.
# Sketch axes: x across, y up; side view +x forward.  Blender: x across, z up, front = -y  =>  y_bl = -x_side.
#   blender -b ..\blender\astronavigator_suit.blend --python build_pantsir_3d.py      (the .blend is only loaded, never saved)
#   -> rev. H: pantsir_H_front.png, pantsir_H_back.png (3/4), pantsir_H_back_ortho.png, pantsir_H_side.png
# Rev. H (user): legs (and the backs of the arms) as faceted anatomical shells - no round sections: flattened chamfered
# polygons with front / outer / inner / back faces and two back lobes with a groove (hamstrings, gastrocnemius), the
# gluteal fold into a flat seat plate, the popliteal hollow with a bellows insert, femoral condyles at the knee motor,
# the Achilles recess, malleoli (inner higher), a shaped heel cup; the elbow's pointed olecranon plate and inner fold,
# the forearm a flattened oval with a ridge. The gloves are left as they are (new hands come separately).
# Rev. I (user): legs as TAPERING faceted plate shells (irregular decagon sections: two front planes to a crease, 30-45 deg
# chamfers, two back planes in a V valley), the knee slightly flexed (shin leaning forward ~7 deg, S-line), the thigh
# overlapping the knee band, the calf tucked under it, a narrow ankle; no glued-on strips. The hand is fully mechanised:
# the human hand stays inside a closed armoured capsule on a grip controller; a three-digit effector (slave) comes out of
# its front. -> pantsir_I_front/back/back_ortho/side.png, pantsir_I_hand.png (3/4 back, palm, cutaway with the hand inside)
# Rev. J (user): the hand = variant 2 «Ладонь» (sketch/build_sketch_mech_hands.py v2); the thermal shield by physics -
# no separate tiles: shingled 3 mm reflective ceramic bands on standoffs all round every segment, folded lips, sliding fans
# at the joints, caps over the ring motors, shield plates on the helmet cap; the door-radiator stays open to the sky.
# -> pantsir_J_front/back/side.png, pantsir_J_hand.png, pantsir_J_shield.png (close-up of the thigh and knee)
# Rev. K (user): the shield gap INSIDE the envelope - the shells shrink inward by gap + plate (~22 mm), so shell + gap +
# shield = the rev. I silhouette; arms as in I with 2 bands per segment; the hand capsule as ONE bent back plate and ONE
# palm plate on posts, flat-ish and no wider than the forearm end; longer effector fingers (55/40/32, 52/40/32, thumb
# 48/38 opposed under the index); light grey ceramic shield (not white); «ПАНЦИРЬ-2» on the upper chest shell.
#   python build_pantsir_3d.py --vs-i  -> pantsir_K_vs_I.png (front orthos of I and K side by side)
# Rev. L (user): grey ceramic shield (0.5), the chest shield faceted (sternum ridge, pectoral planes, sides wrapping to the
# armpits, chevron bands), 2 bands per leg segment, the hand capsule shaped as the back of a hand (trapezoid widening to
# the knuckles, back sloping down to a knuckle ridge, a thenar mound with the thumb, palm slightly concave).
#   python build_pantsir_3d.py --vs K  -> pantsir_L_vs_K.png
# Rev. M (user): the chest shield (plates C1-C4 of flat planes) and the hand capsule built exactly per the master sketch
# sketch/build_sketch_chest_hand.py; flat shading, Freestyle seam lines, a sun key from the upper front side.
#   -> pantsir_M_front/side/chest/hand.png;  python build_pantsir_3d.py --vs-sketch -> pantsir_M_vs_sketch.png
# Rev. N (user): cleanup of M only - Freestyle seam lines only on the chest plates C1-C4 and the hand capsule + digits
# (a lineset restricted to the "SeamLines" collection); the hand cuff is half as long (coupling ring close to the capsule).
#   -> pantsir_N_front/back/side/chest/hand.png
# Rev. O (user, HEAVY_SUIT.md «Ноги и ботинки при +500 °C»): legs thicker (+20-25 mm radius: hex tile skin 8 mm on a carrier,
# 30 mm multilayer insulation, cooled inner shell), a bigger knee block, a wider ankle; the boot rebuilt: two-part foot
# hinged at the ball (ring drives + elastic return strap, toe rocker 10 deg), a 64 mm segmented sole (ceramic studs, aerogel
# fill, carrier plates), a C-shaped titanium heel leaf spring + elastomer pad, a two-axis ankle (pitch + inversion ring
# drives), an instep fan, a heavy faceted upper; ALL shield plates carry a hex ceramic tile mosaic (35 mm, bump + grout).
#   -> pantsir_O_front/back/side/ortho_front/hex/chest.png, boot shots;  --boot-sheet -> pantsir_O_boot.png;  --vs N
# Rev. P (user): legs (and arms) as STRAIGHT TAPERED FACETED PRISMS - two sections per segment, linear taper, flat back, no
# belly (thigh 0.26 -> 0.20 m, shin 0.21 -> 0.155 m outside the shield); three shield material variants on the same
# geometry: P1 gold embossed foil (fine hex dimples, creases), P2 matte technical ceramic, P3 hybrid (ceramic on the
# wear zones, foil elsewhere); graphite edge trims; a soft environment for the metal to reflect.
#   -> pantsir_P1/P2/P3_front.png, _side.png, pantsir_P1_foil.png;  --variants -> pantsir_P_variants.png;  --vs-o -> pantsir_P_vs_O.png
# Rev. Q (user, HEAVY_SUIT.md «Замена: фольга — техника нашего века»): no foil - impact zones in polished amber borazon
# (cBN) plates on a BN-aerogel layer, the rest a photonic mirror under a borazon window (thin-film multilayer on a metal
# reflector, amber glassy window), every plate in a light-metal frame; thinner walls (borazon replaces polyethylene):
# limbs 15 mm and torso 22 mm per side slimmer than P3.  -> pantsir_Q_front/back/side/plates.png;  --vs-p3 -> pantsir_Q_vs_P3.png
# Rev. R (user): the honest wall (HEAVY_SUIT.md, limbs ~79 mm, torso ~93 mm per side): legs P3 + 9 mm, torso back to P3
# (+3 mm); arms by the limb rule (body + 2 x 79 mm, elbow 15 mm clearance), shoulders 6 cm wider; hand capsule = hand +
# 2 x 40 mm blended to the wrist, digits in proportion; layers visible at the plate edges (BN aerogel + insulation) and
# coolant manifolds at the joints; clearer borazon crystal, darker photonic mirror; no bolts, rivets, post heads or band
# numbers.  -> pantsir_R_front/back/side/arm/plates.png;  --r-sheets -> pantsir_R_vs.png, pantsir_R_section.png
#   python build_pantsir_3d.py --compare  -> pantsir_G_vs_sketch.png      (rev. F outputs pantsir_F_* are kept as they are)
# Rev. S (user 2026-10-18): helmet variant C «Фотонное окно под шторкой» (sketch/build_sketch_helmet_optics.py row C): a compact
# faceted plate shell 0.256 m wide, 30 mm wall, on a graphite lock ring over the collar; a graphite visor frame with all the
# sensors as armoured ports (stereo pair 65 mm, peripheral cameras, thermal + lidar, recessed lamps); in its recess the
# photonic window 176 x 68 mm behind 6 faceted W-ZrB2 louvres (retract up into the brow bar). V1 hinge / split kept.
#   -> pantsir_S_front.png, pantsir_S_helmet_closed / _louvres_open / _side / _cap_open.png
#   env ONLY_HELMET=1 (skip the full figure), HELM_SHOTS=closed,louvres,side,open, ALL=1 (also back / side / ortho front)
# env: QUICK=1 small preview renders into _preview\.  Objects are named Pantsir_* by part, ready for a later .msh export.
import math, os, sys, traceback
HERE = os.path.dirname(os.path.abspath(__file__))
REV = "S"
ORTHO_SCALE, ORTHO_W, ORTHO_H = 2.3, 1000, 1200
CAM_Z = 0.95


def compare():
    from PIL import Image, ImageDraw, ImageFont
    sk = Image.open(os.path.join(HERE, "sketch", "pantsir_tile_rad.png")).convert("RGB")
    S_SK, OY = 360.0, 930.0; ppm = ORTHO_H / ORTHO_SCALE; k = ppm / S_SK
    ground = ORTHO_H / 2 + CAM_Z * ppm
    def crop(ox):
        x0, x1, y0, y1 = ox - 0.62 * S_SK, ox + 0.62 * S_SK, OY - 2.05 * S_SK, OY + 0.08 * S_SK
        im = sk.crop((int(x0), int(y0), int(x1), int(y1))); return im.resize((int(im.width * k), int(im.height * k))), (OY - y0) * k
    f_sk, gf = crop(560); s_sk, gs = crop(1080)
    f3, s3 = [Image.open(os.path.join(HERE, "pantsir_%s_ortho_%s.png" % (REV, n))).convert("RGB") for n in ("front", "side")]
    top = int(max(ground, gf, gs)) + 10; H = top + int(0.1 * ppm) + 60
    panels = [(f3, ground, "3D спереди"), (f_sk, gf, "эскиз спереди"), (s3, ground, "3D сбоку"), (s_sk, gs, "эскиз сбоку")]
    Wt = sum(p[0].width for p in panels) + 30 * 3
    sheet = Image.new("RGB", (Wt, H), (250, 250, 247)); d = ImageDraw.Draw(sheet); f = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 30)
    x = 0
    for im, g, lab in panels:
        sheet.paste(im, (x, int(top - g) + 50)); d.text((x + 20, 10), lab, font=f, fill=(30, 40, 50)); x += im.width + 30
    for zz in (0.0, 1.0, 1.85):
        yy = int(top + 50 - zz * ppm); d.line([(0, yy), (Wt, yy)], fill=(200, 80, 40), width=1)
    sheet.save(os.path.join(HERE, "pantsir_%s_vs_sketch.png" % REV))


if __name__ == "__main__" and "--vs-sketch" in sys.argv:
    from PIL import Image, ImageDraw, ImageFont
    sk = Image.open(os.path.join(HERE, "sketch", "pantsir_chest_hand.png")).convert("RGB")
    ch = Image.open(os.path.join(HERE, "pantsir_%s_chest_ortho.png" % REV)).convert("RGB")
    ht = Image.open(os.path.join(HERE, "pantsir_%s_hand_top.png" % REV)).convert("RGB")
    skc = sk.crop((330 - 400, 430 - 400, 330 + 400, 430 + 400)); skh = sk.crop((317 - 340, 1110 - 340, 317 + 340, 1110 + 340))
    sh = Image.new("RGB", (800 * 2 + 20, 800 + 680 + 120), (250, 250, 247)); d = ImageDraw.Draw(sh); f = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 26)
    sh.paste(ch, (0, 40)); sh.paste(skc, (820, 40)); sh.paste(ht, (60, 880)); sh.paste(skh, (880, 880))
    d.text((10, 6), "3D: грудь спереди (орто, 1000 px/м)", font=f, fill=(30, 40, 50)); d.text((830, 6), "эскиз: грудь спереди", font=f, fill=(30, 40, 50))
    d.text((10, 846), "3D: кисть сверху (орто, 1700 px/м)", font=f, fill=(30, 40, 50)); d.text((830, 846), "эскиз: кисть сверху", font=f, fill=(30, 40, 50))
    sh.save(os.path.join(HERE, "pantsir_%s_vs_sketch.png" % REV)); sys.exit(0)
if __name__ == "__main__" and "--r-sheets" in sys.argv:
    from PIL import Image, ImageDraw, ImageFont
    f = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 28)
    fs_ = [("pantsir_P3_ortho_front.png", "P3 спереди"), ("pantsir_Q_ortho_front.png", "Q спереди"), ("pantsir_R_ortho_front.png", "R спереди"),
           ("pantsir_P3_side.png", "P3 сбоку"), ("pantsir_Q_side.png", "Q сбоку"), ("pantsir_R_side.png", "R сбоку")]
    ims = [Image.open(os.path.join(HERE, n)).convert("RGB") for n, _ in fs_]
    w, h = ims[0].size; sh = Image.new("RGB", (6 * w + 100, h + 60), (250, 250, 247)); d = ImageDraw.Draw(sh)
    for k, (im, (_, t)) in enumerate(zip(ims, fs_)): sh.paste(im.resize((w, h)), (k * (w + 20), 60)); d.text((k * (w + 20) + 20, 10), t, font=f, fill=(30, 40, 50))
    sh.save(os.path.join(HERE, "pantsir_R_vs.png"))
    ims = [Image.open(os.path.join(HERE, "pantsir_R_plates_%s.png" % k)).convert("RGB") for k in ("a", "b")]
    w, h = ims[0].size; sh = Image.new("RGB", (2 * w + 20, h + 60), (250, 250, 247)); d = ImageDraw.Draw(sh)
    for k, (im, t) in enumerate(zip(ims, ("кромки плит: аэрогель BN и изоляция; коллектор у колена", "другой угол: перелив зеркала, кристалл боразона"))):
        sh.paste(im, (k * (w + 20), 60)); d.text((k * (w + 20) + 20, 10), t, font=f, fill=(30, 40, 50))
    sh.save(os.path.join(HERE, "pantsir_R_plates.png"))
    # the wall section of a limb (technical drawing): 7 layers from the body outward, ~79 mm
    LAY = [("Зазор до тела", 10, (236, 236, 232), "10 мм (у колена и локтя 15)"), ("Охлаждающий костюм", 8, (120, 168, 205), "8 мм"),
           ("Гермооболочка", 6, (150, 155, 160), "6 мм"), ("Каналы теплоносителя", 4, (70, 120, 170), "4 мм"),
           ("Радиационный слой: боразон + гидрированные нанотрубки BN", 13, (196, 140, 60), "13 мм"),
           ("Аэрогель BN / теплоизоляция", 30, (232, 228, 214), "30 мм"), ("Зазор, рама, плита (боразон или зеркало)", 8, (150, 96, 30), "8 мм")]
    S = 9; W0, H0 = 1800, 980; img = Image.new("RGB", (W0, H0), (250, 250, 247)); d = ImageDraw.Draw(img)
    ft = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", 34); fl = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 24); fs2 = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 20)
    d.text((40, 24), "«Панцирь», рев. R: стенка конечности в разрезе, ~79 мм на сторону (торс и таз ~93 мм)", font=ft, fill=(25, 30, 40))
    x0, y0, y1 = 220, 150, 610
    d.rectangle((x0 - 150, y0, x0, y1), fill=(222, 178, 150)); d.text((x0 - 140, y1 + 12), "тело", font=fl, fill=(60, 50, 40))
    x = x0; mids = []
    for name, mm, col, lab in LAY:
        xe = x + mm * S; d.rectangle((x, y0, xe, y1), fill=col, outline=(40, 44, 50), width=2); mids.append(((x + xe) // 2, name, lab, mm)); x = xe
    d.rectangle((x - 8 * S, y0, x - 8 * S + 10, y1), fill=(180, 184, 188))                                   # light-metal frame at the plate edge
    d.text((x + 12, y0 + 6), "снаружи", font=fl, fill=(60, 60, 70))
    yy = y1 + 70
    for k, (xm, name, lab, mm) in enumerate(mids):
        ty = yy + k * 40; d.line((xm, y1, xm, ty + 12), fill=(40, 44, 50), width=2); d.line((xm, ty + 12, 1060, ty + 12), fill=(40, 44, 50), width=1)
        d.text((1070, ty), "%d. %s — %s" % (k + 1, name, lab), font=fl, fill=(25, 30, 40))
    d.line((x0, y0 - 30, x, y0 - 30), fill=(25, 30, 40), width=2)
    for xx in (x0, x): d.line((xx, y0 - 40, xx, y0 - 20), fill=(25, 30, 40), width=2)
    d.text(((x0 + x) // 2 - 50, y0 - 66), "%d мм" % sum(m for _, m, _, _ in LAY), font=fl, fill=(25, 30, 40))
    d.text((40, H0 - 46), "масштаб: 1 мм = %d px; торс: зазор 10 + тантал 1 + полиэтилен/боразон + оболочка 6 + изоляция 30 + экран ≈ 93 мм" % S, font=fs2, fill=(80, 80, 90))
    img.save(os.path.join(HERE, "pantsir_R_section.png")); sys.exit(0)
if __name__ == "__main__" and ("--vs-p3" in sys.argv or "--q-sheets" in sys.argv):
    from PIL import Image, ImageDraw, ImageFont
    f = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 28)
    fs_ = [("pantsir_P3_ortho_front.png", "P3 спереди"), ("pantsir_Q_ortho_front.png", "Q спереди (стенки тоньше)"), ("pantsir_P3_side.png", "P3 сбоку"), ("pantsir_Q_side.png", "Q сбоку")]
    ims = [Image.open(os.path.join(HERE, n)).convert("RGB") for n, _ in fs_]
    w, h = ims[0].size; sh = Image.new("RGB", (4 * w + 60, h + 60), (250, 250, 247)); d = ImageDraw.Draw(sh)
    for k, (im, (_, t)) in enumerate(zip(ims, fs_)): sh.paste(im.resize((w, h)), (k * (w + 20), 60)); d.text((k * (w + 20) + 20, 10), t, font=f, fill=(30, 40, 50))
    sh.save(os.path.join(HERE, "pantsir_Q_vs_P3.png"))
    ims = [Image.open(os.path.join(HERE, "pantsir_Q_plates_%s.png" % k)).convert("RGB") for k in ("a", "b")]
    w, h = ims[0].size; sh = Image.new("RGB", (2 * w + 20, h + 60), (250, 250, 247)); d = ImageDraw.Draw(sh)
    for k, (im, t) in enumerate(zip(ims, ("зеркало под боразоном (бедро), плиты боразона (колено, голень)", "другой угол: перелив зеркала"))):
        sh.paste(im, (k * (w + 20), 60)); d.text((k * (w + 20) + 20, 10), t, font=f, fill=(30, 40, 50))
    sh.save(os.path.join(HERE, "pantsir_Q_plates.png")); sys.exit(0)
if __name__ == "__main__" and "--variants" in sys.argv:
    from PIL import Image, ImageDraw, ImageFont
    ims = [Image.open(os.path.join(HERE, "pantsir_P%d_front.png" % k)).convert("RGB") for k in (1, 2, 3)]
    w, h = ims[0].size; sh = Image.new("RGB", (3 * w + 40, h + 60), (250, 250, 247)); d = ImageDraw.Draw(sh); f = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 36)
    for k, (im, t) in enumerate(zip(ims, ("1 Фольга", "2 Керамика", "3 Гибрид"))):
        sh.paste(im, (k * (w + 20), 60)); d.text((k * (w + 20) + 20, 10), t, font=f, fill=(30, 40, 50))
    sh.save(os.path.join(HERE, "pantsir_P_variants.png")); sys.exit(0)
if __name__ == "__main__" and "--vs-o" in sys.argv:
    from PIL import Image, ImageDraw, ImageFont
    fs_ = [("pantsir_O_ortho_front.png", "O спереди"), ("pantsir_P_ortho_front.png", "P спереди (прямые призмы)"),
           ("pantsir_O_side.png", "O сбоку"), ("pantsir_P2_side.png", "P сбоку")]
    ims = [Image.open(os.path.join(HERE, n)).convert("RGB") for n, _ in fs_]
    w, h = ims[0].size; sh = Image.new("RGB", (4 * w + 60, h + 60), (250, 250, 247)); d = ImageDraw.Draw(sh); f = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 34)
    for k, (im, (_, t)) in enumerate(zip(ims, fs_)):
        sh.paste(im.resize((w, h)), (k * (w + 20), 60)); d.text((k * (w + 20) + 20, 10), t, font=f, fill=(30, 40, 50))
    sh.save(os.path.join(HERE, "pantsir_P_vs_O.png")); sys.exit(0)
if __name__ == "__main__" and "--boot-sheet" in sys.argv:
    from PIL import Image, ImageDraw, ImageFont
    names = (("boot_side", "ботинок фигуры, сбоку (орто), стопа ровно"), ("boot_flex_side", "отрыв носка 25°, сбоку (орто)"),
             ("boot_flex_front", "отрыв носка, 3/4 спереди"), ("boot_flex_rear", "отрыв носка, 3/4 сзади: рессора, шипы"))
    ims = [Image.open(os.path.join(HERE, "pantsir_%s_%s.png" % (REV, n))).convert("RGB") for n, _ in names]
    w, h = ims[0].size; sh = Image.new("RGB", (2 * w + 20, 2 * h + 100), (250, 250, 247)); d = ImageDraw.Draw(sh); f = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 26)
    for k, (im, (n, t)) in enumerate(zip(ims, names)):
        x, y = (k % 2) * (w + 20), (k // 2) * (h + 50) + 40; sh.paste(im.resize((w, h)), (x, y)); d.text((x + 10, y - 34), t, font=f, fill=(30, 40, 50))
    sh.save(os.path.join(HERE, "pantsir_%s_boot.png" % REV)); sys.exit(0)
if __name__ == "__main__" and "--hand-sheet" in sys.argv:
    from PIL import Image
    ims = [Image.open(os.path.join(HERE, "pantsir_%s_hand_%s.png" % (REV, v))).convert("RGB") for v in ("back", "palm", "side", "cut")]
    sh = Image.new("RGB", (sum(i.width for i in ims) + 20, max(i.height for i in ims)), (250, 250, 247)); x = 0
    for im in ims: sh.paste(im, (x, 0)); x += im.width + 20
    sh.save(os.path.join(HERE, "pantsir_%s_hand.png" % REV)); sys.exit(0)
if __name__ == "__main__" and ("--vs-i" in sys.argv or "--vs" in sys.argv):
    from PIL import Image, ImageDraw, ImageFont
    OLD = sys.argv[sys.argv.index("--vs") + 1] if "--vs" in sys.argv else "I"
    a, b = [Image.open(os.path.join(HERE, "pantsir_%s_ortho_front.png" % r)).convert("RGB") for r in (OLD, REV)]
    sh = Image.new("RGB", (a.width + b.width + 20, a.height + 50), (250, 250, 247)); sh.paste(a, (0, 50)); sh.paste(b, (a.width + 20, 50))
    d = ImageDraw.Draw(sh); f = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", 30)
    d.text((20, 8), OLD, font=f, fill=(30, 40, 50)); d.text((a.width + 40, 8), REV, font=f, fill=(30, 40, 50))
    sh.save(os.path.join(HERE, "pantsir_%s_vs_%s.png" % (REV, OLD))); sys.exit(0)
if __name__ == "__main__" and "--compare" in sys.argv:
    compare(); sys.exit(0)

import bpy, bmesh
from mathutils import Vector as V, Quaternion, Matrix
from mathutils.bvhtree import BVHTree
QUICK = os.environ.get("QUICK") == "1"
OUT = os.path.join(HERE, "_preview") if QUICK else HERE
LOG = os.path.join(HERE, "build_pantsir_3d.log")


class _Log(list):                                                   # rev. P: written through, so a crash still leaves a trace
    def append(self, x):
        list.append(self, x)
        try: open(LOG, "w", encoding="utf-8").write(chr(10).join(self) + chr(10))
        except Exception: pass


log = _Log()
COLL = [None]; M = {}; PARTS = {}; FONT = [None]


# ------------------------------------------------------------------ materials (matte enamel, ceramic, graphite, red, rubber)
def mat(name, base, metal=0.0, rough=0.5, coat=0.0, emit=None, aniso=0.0, bump=0.0):
    m = bpy.data.materials.new("Pantsir_" + name); m.use_nodes = True; nt = m.node_tree
    p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Base Color"].default_value = (*base, 1); p.inputs["Metallic"].default_value = metal; p.inputs["Roughness"].default_value = rough
    if coat: p.inputs["Coat Weight"].default_value = coat
    if aniso:
        try: p.inputs["Anisotropic"].default_value = aniso
        except Exception: pass
    if emit: p.inputs["Emission Color"].default_value = (*emit[0], 1); p.inputs["Emission Strength"].default_value = emit[1]
    if bump:                                                       # a faint orange-peel of enamel / ceramic grain
        nz = nt.nodes.new("ShaderNodeTexNoise"); nz.inputs["Scale"].default_value = 900; b = nt.nodes.new("ShaderNodeBump")
        b.inputs["Strength"].default_value = bump; b.inputs["Distance"].default_value = 0.0005
        nt.links.new(nz.outputs["Fac"], b.inputs["Height"]); nt.links.new(b.outputs["Normal"], p.inputs["Normal"])
    m.diffuse_color = (*base, 1); M[name] = m


def materials():
    mat("shell", (0.19, 0.195, 0.19), rough=0.55, bump=0.08)          # light grey enamel
    mat("tile", (0.80, 0.74, 0.62), rough=0.78, bump=0.15)           # warm off-white matte ceramic
    mat("door", (0.17, 0.175, 0.17), rough=0.55, bump=0.08)
    mat("ghost", (0.95, 0.55, 0.3), rough=0.6, emit=((1.0, 0.5, 0.25), 2.5)); mat("ghostgrip", (0.2, 0.5, 0.9), rough=0.5, emit=((0.2, 0.5, 1.0), 0.8))
    mat("ghostshell", (0.6, 0.62, 0.64), rough=0.4)
    mat("shieldp", (0.42, 0.41, 0.38), rough=0.45, coat=0.15)                     # high-reflective ceramic shield plates
    for nm, al in (("ghostshell", 0.1), ("ghost", 0.95)):
        pp = next(n for n in M[nm].node_tree.nodes if n.type == "BSDF_PRINCIPLED"); pp.inputs["Alpha"].default_value = al
    mat("frame", (0.07, 0.075, 0.08), metal=0.35, rough=0.48)         # dark graphite
    mat("bear", (0.11, 0.115, 0.12), metal=0.6, rough=0.38, aniso=0.4)
    mat("hub", (0.30, 0.31, 0.32), metal=1.0, rough=0.3)
    mat("rubber", (0.02, 0.02, 0.022), rough=0.85)                    # black rubberised details
    mat("red", (0.50, 0.015, 0.01), rough=0.5)                          # signal red
    mat("fin", (0.05, 0.055, 0.06), metal=0.5, rough=0.45)
    mat("gold", (1.0, 0.74, 0.32), metal=1.0, rough=0.16)
    mat("gold2", (0.85, 0.6, 0.22), metal=1.0, rough=0.24)
    mat("dark", (0.03, 0.035, 0.04), rough=0.45)
    mat("titan", (0.42, 0.40, 0.37), metal=1.0, rough=0.34, aniso=0.3)      # rev. O: heel leaf spring
    mat("aerogel", (0.30, 0.32, 0.34), rough=0.92, bump=0.3)               # rev. O: insulating fill between the sole studs
    log.append("pre fc"); foil_ceramic(); q_materials(); log.append("fc ok")                                                        # rev. P: shield variants (swapped onto the shield objects)
    mat("slit", (0.02, 0.03, 0.05), metal=0.5, rough=0.05, coat=1.0)
    mat("stencil_red", (0.50, 0.015, 0.01), rough=0.6); mat("stencil_dark", (0.06, 0.06, 0.06), rough=0.7)
    mat("lgreen", (0.3, 0.9, 0.45), emit=((0.3, 0.9, 0.45), 6.0)); mat("lamber", (1, 0.75, 0.3), emit=((1.0, 0.72, 0.25), 6.0))


# ------------------------------------------------------------------ mesh helpers
def mk(vs, fs, mname, name, sharp=0.6, part=None, mats=None, fm=None):
    me = bpy.data.meshes.new(name); me.from_pydata([tuple(v) for v in vs], [], fs); me.validate()
    for mm in (mats or [mname]): me.materials.append(M[mm])
    if fm: me.polygons.foreach_set("material_index", fm)
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.recalc_face_normals(bm, faces=bm.faces); bm.to_mesh(me); bm.free()
    try: me.shade_smooth(); me.set_sharp_from_angle(angle=sharp)
    except Exception:
        for p in me.polygons: p.use_smooth = True
    o = bpy.data.objects.new("Pantsir_" + name, me); COLL[0].objects.link(o)
    PARTS.setdefault(part or name, []).append(o)
    return o


def bevel(o, w=0.003, seg=2, ang=40):
    b = o.modifiers.new("bev", "BEVEL"); b.width = w; b.segments = seg; b.limit_method = 'ANGLE'; b.angle_limit = math.radians(ang)
    return o


def se(a, n):
    c, s = math.cos(a), math.sin(a)
    return math.copysign(abs(c) ** (2 / n), c), math.copysign(abs(s) ** (2 / n), s)


def poly_pts(row):
    """faceted anatomical section (11-tuple row): (z, cx, cy, s, W_out, W_in, Front, B_out, B_in, groove, chamfer);
    flat front, outer and inner faces with chamfered corners, the back as two lobes with a groove (negative = a ridge)"""
    z, cx, cy, s_, Wo, Wi, F, Bo, Bi, g, c = row[:11]
    gd = min(Bo, Bi) - g; kb = 0.97 if g <= 0.0005 else 0.94
    of = [(-Wi + c, F), (Wo - c, F), (Wo, F - c), (Wo, -Bo * 0.4), (Wo * 0.86, -Bo * 0.86), (Wo * 0.45, -Bo), (Wo * 0.12, -Bo * kb),
          (0.0, -gd), (-Wi * 0.12, -Bi * kb), (-Wi * 0.45, -Bi), (-Wi * 0.86, -Bi * 0.86), (-Wi, -Bi * 0.4), (-Wi, F - c)]
    return [(s_ * o, -f) for o, f in of]


def chest_pts(row):
    """rev. L faceted chest for the shield (13 values): (z, cx, cy, s, W, W, F, B, ridge, 0, 0, 0, 0): a sternum ridge,
    pectoral planes angled outward, a chamfer wrapping round toward the armpits; encloses the superellipse shell"""
    z, cx, cy, s_, W, _, F, B, cr = row[:9]
    of = [(0.0, F + cr), (W * 0.5, F * 0.965), (W * 0.85, F * 0.78), (W, F * 0.4), (W, -B), (-W, -B), (-W, F * 0.4), (-W * 0.85, F * 0.78), (-W * 0.5, F * 0.965)]
    return [(o, -f) for o, f in of]


def poly_hit(pts, t):
    dx, dy = math.cos(t), math.sin(t); best = 0.0
    for (px, py), (qx, qy) in zip(pts, pts[1:] + pts[:1]):
        ex, ey = qx - px, qy - py; den = dx * ey - dy * ex
        if abs(den) < 1e-12: continue
        lam = (px * ey - py * ex) / den; u = (px * dy - py * dx) / den
        if -1e-9 <= u <= 1 + 1e-9 and lam > best: best = lam
    return dx * best, dy * best


def sec_pt(row, t):
    """point of an anatomical section: superellipse row = (z, cx, cy, r_px, r_nx, r_front(-y), r_back(+y), n[, tilt_y]),
    or a faceted polygon row (11 values, see poly_pts)"""
    if len(row) == 11:
        x, y = poly_hit(poly_pts(row), t); return V((row[1] + x, row[2] + y, row[0]))
    if len(row) == 13:
        x, y = poly_hit(chest_pts(row), t); return V((row[1] + x, row[2] + y, row[0]))
    if len(row) == 12:
        x, y = poly_hit(facet_pts(row), t); return V((row[1] + x, row[2] + y, row[0]))
    z, cx, cy, rpx, rnx, rf, rb, n = row[:8]; ty = row[8] if len(row) > 8 else 0.0
    c, s = se(t, n); x = c * (rpx if c > 0 else rnx); y = s * (rb if s > 0 else rf)
    return V((cx + x, cy + y, z + ty * y))


def loft4(rows, m, name, seg=64, sharp=1.0, part=None, cap=(True, True)):
    vs = []; fs = []; R = []
    for row in rows:
        R.append(len(vs)); vs += [sec_pt(row, 2 * math.pi * j / seg) for j in range(seg)]
    for a, b in zip(R, R[1:]):
        for j in range(seg): j1 = (j + 1) % seg; fs.append((a + j, a + j1, b + j1, b + j))
    if cap[0]: fs.append(tuple(reversed(range(R[0], R[0] + seg))))
    if cap[1]: fs.append(tuple(range(R[-1], R[-1] + seg)))
    return mk(vs, fs, m, name, sharp, part)


def sample_rows(rows, z):
    """interpolate an anatomical row at height z"""
    rs = sorted(rows, key=lambda r: r[0])
    if z <= rs[0][0]: return rs[0]
    for a, b in zip(rs, rs[1:]):
        if a[0] <= z <= b[0]:
            t = (z - a[0]) / max(b[0] - a[0], 1e-9); return tuple(a[i] + (b[i] - a[i]) * t for i in range(len(a)))
    return rs[-1]


def plate(rows, z0, z1, t0, t1, m, name, off=0.006, thick=0.009, part="plates", nz=10, nt=14):
    """a sculpted shell plate over the limb: the section sector t0..t1 between z0..z1, offset and thickened, bevelled"""
    vs = []; fs = []
    def P(i, j, o):
        z = z0 + (z1 - z0) * i / nz; row = sample_rows(rows, z); t = t0 + (t1 - t0) * j / nt
        p = sec_pt(row, t); c = V((row[1], row[2], p.z)); d = (p - c); d.z = 0; d.normalize(); return p + d * o
    idx = lambda l, i, j: l * (nz + 1) * (nt + 1) + i * (nt + 1) + j
    for l, o in ((0, off + thick), (1, off)):
        for i in range(nz + 1):
            for j in range(nt + 1): vs.append(P(i, j, o))
    for i in range(nz):
        for j in range(nt):
            fs.append((idx(0, i, j), idx(0, i, j + 1), idx(0, i + 1, j + 1), idx(0, i + 1, j)))
            fs.append((idx(1, i + 1, j), idx(1, i + 1, j + 1), idx(1, i, j + 1), idx(1, i, j)))
    for i in range(nz):
        for j in (0, nt): fs.append((idx(0, i, j), idx(0, i + 1, j), idx(1, i + 1, j), idx(1, i, j)))
    for j in range(nt):
        for i in (0, nz): fs.append((idx(0, i, j), idx(1, i, j), idx(1, i, j + 1), idx(0, i, j + 1)))
    o = mk(vs, fs, m, name, 0.9, part); return bevel(o, 0.0025, 2, 50)


def prism(pts, xc, w, m, name, part=None):
    vs = []; n = len(pts); fs = []
    for side in (-1, 1):
        for xs, z in pts: vs.append((xc + side * w / 2, -xs, z))
    fs.append(tuple(range(n))); fs.append(tuple(reversed(range(n, 2 * n))))
    for i in range(n): i1 = (i + 1) % n; fs.append((i, i1, n + i1, n + i))
    return mk(vs, fs, m, name, 0.5, part)


def rbox(c, size, m, name, q=None, r=0.004, seg=2, part=None):
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts: v.co = V((v.co.x * size[0], v.co.y * size[1], v.co.z * size[2]))
    if r > 0: bmesh.ops.bevel(bm, geom=list(bm.edges), offset=min(r, 0.45 * min(size)), segments=seg, profile=0.5, affect='EDGES')
    if q is not None: bmesh.ops.rotate(bm, verts=bm.verts, cent=V((0, 0, 0)), matrix=q.to_matrix())
    bmesh.ops.translate(bm, verts=bm.verts, vec=V(c))
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    vs = [v.co.copy() for v in me.vertices]; fs = [tuple(p.vertices) for p in me.polygons]; bpy.data.meshes.remove(me)
    return mk(vs, fs, m, name, 0.5, part)


def cyl(p1, p2, r1, m, name, r2=None, seg=32, part=None):
    p1, p2 = V(p1), V(p2); d = p2 - p1; r2 = r1 if r2 is None else r2
    q = d.normalized().to_track_quat('Z', 'Y'); L = d.length; vs = []; fs = []
    for k, (z, r) in enumerate(((0.0, r1), (L, r2))):
        for j in range(seg):
            a = 2 * math.pi * j / seg; vs.append(p1 + q @ V((r * math.cos(a), r * math.sin(a), z)))
    for j in range(seg): j1 = (j + 1) % seg; fs.append((j, j1, seg + j1, seg + j))
    fs.append(tuple(reversed(range(seg)))); fs.append(tuple(range(seg, 2 * seg)))
    return mk(vs, fs, m, name, 0.7, part)


def bar(a, b, w, h, m, name, up=(0, 0, 1), part=None, r=0.003):
    a, b = V(a), V(b); d = b - a; L = d.length; d.normalize(); up = V(up); zz = (up - d * up.dot(d)).normalized(); xx = d.cross(zz)
    return rbox((a + b) / 2, (w, L, h), m, name, Matrix((xx, d, zz)).transposed().to_quaternion(), r=r, part=part)


def channel(a, b, n, w=0.04, d=0.018, t=0.005, m="frame", name="member"):
    """a flat structural channel profile from a to b, its web facing n (outward), flanges toward the body"""
    a, b = V(a), V(b); dd = (b - a).normalized(); n = V(n); n = (n - dd * n.dot(dd)).normalized(); xx = dd.cross(n)
    bar(a, b, w, t, m, name, up=n, part="frame")
    for k in (-1, 1): bar(a + xx * k * (w / 2 - t / 2) - n * d / 2, b + xx * k * (w / 2 - t / 2) - n * d / 2, t, d, m, name + "_fl", up=n, part="frame")
    for f in (0.2, 0.5, 0.8): bolt(a.lerp(b, f) + n * (t / 2), n)


BOLTS = []


def bolt(p, n): BOLTS.append((V(p), V(n).normalized()))


def flush_bolts():
    vs = []; fs = []
    for p, n in BOLTS:
        e1 = n.orthogonal().normalized(); e2 = n.cross(e1); base = len(vs); seg = 6
        for h in (0.0, 0.003):
            for j in range(seg):
                a = 2 * math.pi * j / seg; vs.append(p + (e1 * math.cos(a) + e2 * math.sin(a)) * 0.0042 + n * h)
        for j in range(seg): j1 = (j + 1) % seg; fs.append((base + j, base + j1, base + seg + j1, base + seg + j))
        fs.append(tuple(base + seg + j for j in range(seg)))
    BOLTS.clear()
    if vs: mk(vs, fs, "hub", "fasteners", 0.5, "fasteners")


def nquat(n, up=(0, 0, 1)):
    n = V(n).normalized(); up = V(up) - n * n.dot(V(up)); up.normalize(); rt = up.cross(n).normalized()
    return Matrix((rt, n, up)).transposed().to_quaternion()            # local x = rt, local y = normal, local z = up


def stencil(txt, p, n, up=(0, 0, 1), size=0.03, m="stencil_dark", name="stencil"):
    """stencilled Cyrillic marking lying on the surface (normal n)"""
    if FONT[0] is None: FONT[0] = bpy.data.fonts.load("C:/Windows/Fonts/arialbd.ttf")
    cu = bpy.data.curves.new(name, 'FONT'); cu.body = txt; cu.font = FONT[0]; cu.size = size; cu.align_x = 'CENTER'; cu.align_y = 'CENTER'
    cu.extrude = 0.0003; o = bpy.data.objects.new("Pantsir_" + name, cu); COLL[0].objects.link(o); cu.materials.append(M[m])
    n = V(n).normalized(); u = V(up) - n * n.dot(V(up)); u.normalize(); r = u.cross(n)
    o.matrix_world = Matrix.Translation(V(p) + n * 0.0012) @ Matrix((r, u, n)).transposed().to_4x4()
    PARTS.setdefault("stencils", []).append(o); return o


def bvh_of(objs):
    vs = []; fs = []
    for o in objs:
        base = len(vs); vs += [o.matrix_world @ v.co for v in o.data.vertices]; fs += [tuple(base + i for i in p.vertices) for p in o.data.polygons]
    return BVHTree.FromPolygons(vs, fs)


def interp(pts, z):
    if z <= pts[0][0]: return pts[0][1]
    for (z0, a), (z1, b) in zip(pts, pts[1:]):
        if z0 <= z <= z1: return a + (b - a) * (z - z0) / max(z1 - z0, 1e-9)
    return pts[-1][1]


RIBS = []


def rib(c, a, b, n, sa, sb, sn):
    """a small box: centre c, axes a (along), b (across), n (out), half sizes"""
    base = len(RIBS[0]) if RIBS else 0
    if not RIBS: RIBS.extend([[], []])
    vs, fs = RIBS; base = len(vs)
    for i in (-1, 1):
        for j in (-1, 1):
            for k in (-1, 1): vs.append(c + a * sa * i + b * sb * j + n * sn * k)
    for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)): fs.append(tuple(base + x for x in f))


def flush_ribs():
    if RIBS and RIBS[0]: mk(RIBS[0], RIBS[1], "fin", "motor_fins", 0.5, "drives")
    RIBS.clear()


def ring_motor(c, s, r, thick, name, fins=56):
    """30th-century drive: a flat high-torque ring motor (axial flux, direct drive) built into the joint, its axis across
    the body; the cooling-fin / coil band round its rim, a small connector, the frame node lug and fasteners"""
    c = V(c); ax = V((s, 0, 0))
    cyl(c - ax * thick / 2, c + ax * thick / 2, r, "frame", name, seg=56, part="drives")
    cyl(c + ax * thick / 2, c + ax * (thick / 2 + 0.004), r * 0.82, "bear", name + "_face", seg=56, part="drives")
    cyl(c + ax * (thick / 2 + 0.004), c + ax * (thick / 2 + 0.007), r * 0.3, "hub", name + "_hub", seg=32, part="drives")
    fins = max(fins, int(2 * math.pi * r / 0.0042))                            # a fine ribbed band (cooling fins over the coils)
    for k in range(fins):
        a = 2 * math.pi * k / fins; rad = V((0, math.cos(a), math.sin(a)))
        rib(c + rad * (r + 0.0008), ax, rad.cross(ax), rad, thick * 0.36, 0.0009, 0.0016)
    rbox(c + ax * (thick / 2 + 0.0045) + V((0, 0, r * 0.62)), (0.002, 0.006, r * 0.22), "red", name + "_mark", r=0.0005, seg=1, part="drives")
    for k in range(6):
        a = math.pi / 6 + k * math.pi / 3; bolt(c + ax * (thick / 2 + 0.004) + V((0, math.cos(a), math.sin(a))) * r * 0.62, ax)
    con = c + V((0, 0.0, r + 0.004)) + ax * 0.0                                  # power connector with its cap
    rbox(con + V((0, 0.0, 0.006)), (thick * 0.6, 0.016, 0.012), "frame", name + "_con", r=0.002, part="drives")
    cyl(con + V((0, 0, 0.012)), con + V((0, 0, 0.02)), 0.006, "red", name + "_concap", seg=16, part="drives")


def muscle_strip(rows, z0, z1, t, step=0.011):
    """ribbed strip of an artificial-muscle pack seen in a plate gap"""
    z = z0
    while z <= z1:
        row = sample_rows(rows, z); p = sec_pt(row, t); cc = V((row[1], row[2], z)); rad = p - cc; rad.z = 0; rad.normalize()
        tan = V((0, 0, 1)).cross(rad).normalized()
        rib(p + rad * 0.004, tan, V((0, 0, 1)), rad, 0.009, 0.0032, 0.004); z += step


def housing(c, s, size, name, cover=None):
    """a compact drive housing built into a frame node: rounded block, round cover, red index mark, fasteners"""
    c = V(c); w, h, dpt = size
    rbox(c, (w, dpt, h), "frame", name, r=0.012, seg=3, part="drives")
    rc = cover or min(dpt, h) * 0.36
    cyl(c + V((s * w / 2, 0, 0)), c + V((s * (w / 2 + 0.006), 0, 0)), rc, "bear", name + "_cover", seg=40, part="drives")
    cyl(c + V((s * (w / 2 + 0.006), 0, 0)), c + V((s * (w / 2 + 0.009), 0, 0)), rc * 0.35, "hub", name + "_hub", seg=24, part="drives")
    rbox(c + V((s * (w / 2 + 0.0065), 0, rc * 0.75)), (0.002, 0.006, rc * 0.4), "red", name + "_mark", r=0.0005, seg=1, part="drives")
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2; bolt(c + V((s * w / 2, math.cos(a) * dpt * 0.4, math.sin(a) * h * 0.4)), V((s, 0, 0)))


# ------------------------------------------------------------------ tiles (8 mm, rows offset half a tile, on standoffs, numbered)
TILE_NO = [0]


def tile_field(x0, x1, z0, z1, cols, rows, bvh, origin_y, name, frame=None, dir_=V((0, -1, 0)), gap=0.010, t=0.008):
    tw, th = (x1 - x0) / cols, (z1 - z0) / rows; n_t = 0
    for r in range(rows):
        sh = tw / 2 if r % 2 else 0
        for c in range(-1, cols + 1):
            a, b = x0 + c * tw + sh + 0.004, x0 + (c + 1) * tw + sh - 0.004
            a, b = max(a, x0), min(b, x1)
            if b - a < 0.012: continue
            za, zb = z0 + r * th + 0.004, z0 + (r + 1) * th - 0.002
            xm, zm = (a + b) / 2, (za + zb) / 2
            p0 = V(frame(xm, zm, origin_y)) if frame else V((xm, origin_y, zm))
            hit = bvh.ray_cast(p0, dir_, 0.5)
            if hit[0] is None: continue
            p, nrm = hit[0], hit[1].normalized()
            if nrm.dot(dir_) < 0: nrm = -nrm
            q = nquat(nrm); w_, h_ = b - a, zb - za
            rbox(p + nrm * (gap + t / 2), (w_, t, h_), "tile", "%s_tile%02d" % (name, n_t), q, r=0.0018, seg=2, part="tiles")
            for sx in (-1, 1):
                for sz in (-1, 1):
                    o = q @ V((sx * (w_ / 2 - 0.011), 0, sz * (h_ / 2 - 0.011)))
                    cyl(p + o - nrm * 0.004, p + o + nrm * (gap + 0.001), 0.004, "frame", "%s_post" % name, seg=8, part="standoffs")
            TILE_NO[0] += 1
            if w_ > 0.03 and h_ > 0.03:
                stencil("%03d" % TILE_NO[0], p + nrm * (gap + t) + q @ V((w_ / 2 - 0.016, 0, -h_ / 2 + 0.008)), nrm, size=0.0085, name="tileno")
            n_t += 1
    return n_t


# ------------------------------------------------------------------ the body
def facet_pts(row):
    """rev. I faceted leg section (12 values): (z, cx, cy, s, W_out, W_in, Front, Back, valley, crease, 0, 0):
    an irregular decagon - two front planes meeting at a central crease, 30-45 deg chamfers to the outer and inner faces,
    two back planes meeting in a shallow V valley"""
    z, cx, cy, s_, Wo, Wi, F, Bk, vv, cr = row[:10]
    k1 = F * 0.20
    of = [(0.0, F + cr), (Wo * 0.62, F - k1), (Wo, F * 0.28), (Wo, -Bk * 0.34), (Wo * 0.52, -Bk), (0.0, -Bk + vv),
          (-Wi * 0.52, -Bk), (-Wi, -Bk * 0.34), (-Wi, F * 0.28), (-Wi * 0.62, F - k1)]
    return [(s_ * o, -f) for o, f in of]


def leg_rows(s):
    """rev. I: tapering faceted plate shells; the knee slightly flexed (shin leaning forward ~7 deg, the knee ahead of
    the ankle and the hip), the thigh bowed forward, the calf bulging back in its upper third"""
    c = s * LEG_C
    def P(z, wo, wi, f, bk, vv, cy, cr=0.004): return (z, c, cy, s, wo, wi, f, bk, vv, cr, 0.0, 0.0)
    thigh = [P(0.50, 0.090, 0.084, 0.102, 0.078, 0.006, -0.046), P(0.53, 0.084, 0.078, 0.097, 0.072, 0.006, -0.045),
             P(0.60, 0.094, 0.086, 0.108, 0.084, 0.008, -0.040), P(0.67, 0.104, 0.094, 0.118, 0.096, 0.010, -0.033),
             P(0.74, 0.114, 0.100, 0.124, 0.106, 0.010, -0.025), P(0.80, 0.123, 0.105, 0.126, 0.114, 0.008, -0.016),
             P(0.83, 0.125, 0.105, 0.124, 0.116, 0.006, -0.012)]
    knee = [P(0.455, 0.086, 0.082, 0.095, 0.062, 0.010, -0.045), P(0.485, 0.094, 0.090, 0.098, 0.058, 0.010, -0.045),
            P(0.515, 0.086, 0.082, 0.096, 0.062, 0.010, -0.046)]
    shin = [P(0.15, 0.056, 0.056, 0.068, 0.046, 0.004, -0.005), P(0.20, 0.064, 0.064, 0.076, 0.056, 0.006, -0.011),
            P(0.26, 0.078, 0.080, 0.086, 0.080, 0.008, -0.018), P(0.32, 0.090, 0.092, 0.092, 0.098, 0.010, -0.025),
            P(0.37, 0.096, 0.094, 0.095, 0.104, 0.010, -0.030), P(0.42, 0.090, 0.086, 0.095, 0.092, 0.008, -0.036),
            P(0.465, 0.080, 0.076, 0.094, 0.072, 0.006, -0.042)]
    def shr(r, d): return (r[0], r[1], r[2], r[3], r[4] - d, r[5] - d, r[6] - d, r[7] - d) + tuple(r[8:])
    def grow(r, do, di, df, db): return (r[0], r[1], r[2], r[3], r[4] + do, r[5] + di, r[6] + df, r[7] + db) + tuple(r[8:])
    # rev. O: the thermal stack (tile skin + 30 mm insulation + cooled shell) - the outer silhouette grows 20-26 mm vs N
    # (outer face +26, front/back +22, inner +12 with the stance 10 mm wider); the knee block more massive still
    # rev. P: no belly - each segment a straight tapered faceted prism (two sections, linear taper, flat back); the outer
    # silhouette with the shield: thigh 0.26 at the hip -> 0.20 at the knee, shin 0.21 under the knee -> 0.155 at the ankle
    thigh = [P(0.50, 0.081, 0.061, 0.089, 0.066, 0.0, -0.047), P(0.83, 0.117, 0.089, 0.114, 0.107, 0.0, -0.012)]
    shin = [P(0.15, 0.057, 0.044, 0.066, 0.046, 0.0, -0.005), P(0.465, 0.088, 0.068, 0.096, 0.074, 0.0, -0.042)]
    knee = [grow(r, 0.018, 0.004, 0.010, 0.004) for r in knee]
    q = lambda rows: [grow(r, 0.009, 0.009, 0.009, 0.009) for r in rows]                             # rev. R: 79 mm walls (P3 + 9)
    return q(thigh), q(shin), q(knee)


def legs():
    for s in (1, -1):
        c = s * LEG_C; L = "L" if s > 0 else "R"; thigh, shin, knee = leg_rows(s)
        boot_O(s)                                                                                                 # rev. O boot
        # the calf shell (tucks under the knee band), the overlapping tibia plate in front
        bevel(loft4(shin, "shell", "shin" + L, 120, sharp=0.35, part="shell"), 0.003, 2, 25)
        bevel(loft4(knee, "bear", "knee" + L, 120, sharp=0.35, part="bearings"), 0.002, 2, 25)
        z = 0.462                                                                                                # popliteal bellows, smaller, recessed
        while z < 0.51:
            row = sample_rows(knee, z); yb = row[2] + row[7] - row[8] - 0.012 + (0.003 if int((z - 0.462) / 0.008) % 2 == 0 else 0.0)
            bar(V((c - s * row[5] * 0.4, yb, z)), V((c + s * row[4] * 0.4, yb, z)), 0.007, 0.005, "rubber", "bellows" + L, up=(0, 1, 0), part="bearings"); z += 0.008
        kc = [(z, c, -0.06, s, 0.058, 0.058, 0.075, 0.03, 0.0, 0.006, 0, 0) for z in (0.43, 0.485, 0.545)]
        # the thigh shell: its lower edge flares over the knee band (a visible step), a thin recessed seam at the back
        bevel(loft4(thigh, "shell", "thigh" + L, 120, sharp=0.35, part="shell"), 0.003, 2, 25)
        tf = (-math.pi * 0.86, -math.pi * 0.2) if s > 0 else (-math.pi * 0.8, -math.pi * 0.14)

    loft4([(z, 0.0, 0.0, 1, w, w, d, d, d, 0.0, 0.05) for z, w, d in ((0.83, 0.258, 0.139), (0.90, 0.253, 0.137), (0.97, 0.240, 0.138))],
          "shell", "brief", 120, sharp=0.5, part="shell")
    loft4([(0.97, 0, 0.0, 0.238, 0.238, 0.153, 0.153, 2.8, 0.1), (1.02, 0, 0.0, 0.243, 0.243, 0.155, 0.155, 2.8, 0.1)], "bear", "waist", 72, part="bearings")
    # (user, 2026-10-17: the two red connectors on the front of the brief removed)



# ------------------------------------------------------------------ the hand: variant B «Трёхпалая» (sketch/build_sketch_hands.py: hand_B, wrist, digit)
PALM_B = [(-0.046, 0.012), (0.046, 0.012), (0.052, 0.08), (0.040, 0.102), (-0.044, 0.102), (-0.052, 0.07)]   # (u across, v along)
HAND_T = 0.032                                                                     # palm plate thickness


ARM_CX = 0.36                                                     # rev. R: the shoulders 6 cm wider (thicker arms clear the torso)
HAND_W, HAND_T, HAND_WALL = 0.120, 0.058, 0.030                   # the person's hand (measured on the game rig) and the capsule wall


def hand_frame(s, R, sc):
    """the wrist coupling centre O and the hand axes: D along the hand (down the arm), B the back of the hand (outward),
    U across (thumb side = -U, toward the front)"""
    if globals().get("HAND_OD") and s in HAND_OD: O, D = HAND_OD[s][0].copy(), HAND_OD[s][1].normalized()
    else:
        x1, z1 = R(s * ARM_CX, 0.802); x2, z2 = R(s * ARM_CX, 0.70)
        O = V((x1, sc(0.802)[0], z1)); D = (V((x2, sc(0.70)[0], z2)) - O).normalized()
    B = V((s, 0, 0)); B = (B - D * B.dot(D)).normalized()
    U = D.cross(B)
    if U.y < 0: U = -U
    return O, D, B, U


def frame_quat(xv, yv, zv):
    """rotation with local y = yv, local z = zv (x completed right-handed); boxes are symmetric in x"""
    yv = yv.normalized(); zv = (zv - yv * zv.dot(yv)).normalized(); xv = yv.cross(zv)
    return Matrix((xv, yv, zv)).transposed().to_quaternion()


def hand_ring(O, D, U, B, v0, v1, ru, rb, m, name, n=3.0, seg=48):
    vs = []; fs = []
    for v in (v0, v1):
        for j in range(seg):
            c, s_ = se(2 * math.pi * j / seg, n); vs.append(O + D * v + U * (ru * c) + B * (rb * s_))
    for j in range(seg): j1 = (j + 1) % seg; fs.append((j, j1, seg + j1, seg + j))
    fs.append(tuple(reversed(range(seg)))); fs.append(tuple(range(seg, 2 * seg)))
    return mk(vs, fs, m, name, 0.6, "hand")


# ------------------------------------------------------------------ rev. I hand B: a massive armoured gauntlet (~1.4x a bare hand)
def ring_loft(O, U, Bv, D, rings, m, name, sharp=0.35, part="hand", bev=0.002):
    """loft of section rings along the hand: rings = [(v, [(u, b), ...])] in the frame O + U*u + Bv*b + D*v"""
    vs = []; fs = []; n = len(rings[0][1])
    for v, pts in rings:
        for u, b in pts: vs.append(O + U * u + Bv * b + D * v)
    for k in range(len(rings) - 1):
        a, b_ = k * n, (k + 1) * n
        for j in range(n): j1 = (j + 1) % n; fs.append((a + j, a + j1, b_ + j1, b_ + j))
    fs.append(tuple(reversed(range(n)))); fs.append(tuple(range((len(rings) - 1) * n, len(rings) * n)))
    o = mk(vs, fs, m, name, sharp, part)
    if bev: bevel(o, bev, 2, 30)
    return o


def oct_sec(hw, t, cup=0.0, back=1.0):
    """faceted irregular octagon (u, b): flat back with chamfers, palm side (negative b) slightly cupped"""
    return [(hw * 0.66, t / 2 * back), (hw, t * 0.16), (hw, -t * 0.22), (hw * 0.62, -t / 2), (0.0, -t / 2 + cup),
            (-hw * 0.62, -t / 2), (-hw, -t * 0.22), (-hw, t * 0.16), (-hw * 0.66, t / 2 * back), (0.0, t / 2 * back + 0.002)]


def gaunt_digit(start, d, Bv, segs, w0, t0, curls, name, broad=False):
    """thick armoured phalanx shells (graphite core, motor inside), overlapping enamel back plates, elongated rubber pads"""
    p = V(start); d = d.normalized()
    for k, L in enumerate(segs):
        Bk = (Bv - d * Bv.dot(d)).normalized(); Uk = d.cross(Bk).normalized()
        w = w0 * ((1.04 ** k) if broad else (0.94 ** k)); t = t0 * (0.92 ** k); wt = w * (1.03 if broad else 0.93); tt = t * 0.93
        last = k == len(segs) - 1
        rings = [(0.0, oct_sec(w / 2, t)), (L - (0.012 if last else 0.002), oct_sec(wt / 2, tt))]
        if last: rings.append((L, oct_sec(wt / 2 * 0.72, tt * 0.7)))
        ring_loft(p, Uk, Bk, d, rings, "frame", name + "_ph%d" % k)
        pl = [(-0.012 if k else 0.0, None), (L - 0.004, None)]                                     # back plate, overlapping proximally
        def plate_sec(hw, tt_):
            return [(-hw - 0.003, tt_ * 0.12), (-hw * 0.66, tt_ / 2 + 0.006), (hw * 0.66, tt_ / 2 + 0.006), (hw + 0.003, tt_ * 0.12),
                    (hw + 0.0005, tt_ * 0.12 - 0.004), (hw * 0.64, tt_ / 2 + 0.001), (-hw * 0.64, tt_ / 2 + 0.001), (-hw - 0.0005, tt_ * 0.12 - 0.004)]
        ring_loft(p, Uk, Bk, d, [(-0.012 if k else 0.0, plate_sec(w / 2 * 1.04, t)), (L - (0.016 if last else 0.004), plate_sec(wt / 2 * 1.02, tt))], "shell", name + "_plate%d" % k, bev=0.0015)
        if broad:
            for gu in (-w / 6, w / 6):
                bar(p + Uk * gu + Bk * (t / 2 + 0.0065) + d * 0.002, p + Uk * gu + Bk * (tt / 2 + 0.0065) + d * (L - 0.008), 0.0018, 0.0016, "dark", name + "_line", up=Bk, part="hand")
        pc = p + d * (L * 0.5) - Bk * (t / 2 + 0.002)                                             # elongated pad (shingled)
        rbox(pc, (w * 0.55, 0.004, L * 0.82), "rubber", name + "_pad%d" % k, frame_quat(None, -Bk, d + Bk * 0.06), r=0.0015, seg=2, part="hand")
        p = p + d * L
        if not last:
            cyl(p - Uk * (w / 2 - 0.004), p + Uk * (w / 2 - 0.004), t * 0.34, "bear", name + "_motor%d" % k, seg=20, part="hand")   # mostly hidden
            d = (d * math.cos(curls[k]) - Bk * math.sin(curls[k])).normalized()


CAPSULE = {}; GHOST = {}


def hand_B3(s, R, sc, L):
    """rev. I hand: the human hand stays inside a closed, armoured, heat-shielded capsule at the end of the forearm and
    holds a grip controller (master); a mechanical three-digit effector (slave: thumb, index, broad digit) comes out of
    the capsule's front, driven by ring motors in its joints with force feedback - nothing of the hand is outside"""
    O, D, Bv, U = hand_frame(s, R, sc)
    before = set(COLL[0].objects)
    # wide wrist cuff blending into the forearm, the ribbed coupling ring with its red release
    ring_loft(O, U, Bv, D, [(-0.045, oct_sec(0.064, 0.098)), (-0.012, oct_sec(0.072, 0.096)), (0.004, oct_sec(0.074, 0.094))], "shell", "cuff" + L)
    ring_loft(O, U, Bv, D, [(0.004, oct_sec(0.077, 0.1)), (0.022, oct_sec(0.077, 0.1))], "bear", "coupling" + L)
    for k in range(20):
        u_ = -0.07 + k * 0.0074; rbox(O + U * u_ + Bv * 0.0505 + D * 0.013, (0.0025, 0.003, 0.016), "fin", "coupling_rib", frame_quat(None, Bv, D), r=0.0004, seg=1, part="hand")
    cyl(O + D * 0.013 + U * 0.077, O + D * 0.013 + U * 0.088, 0.008, "red", "coupling_release" + L, seg=20, part="hand")
    # palm block: tapered toward the fingers, slightly cupped (the palm plate bent ~10 deg), faceted
    bend = math.radians(10)
    def pr(v, hw, t, cup):
        return (v, oct_sec(hw, t, cup))
    ring_loft(O, U, Bv, D, [pr(0.022, 0.072, 0.086, 0.006), pr(0.07, 0.07, 0.082, 0.010), pr(0.115, 0.065, 0.072, 0.008), pr(0.135, 0.062, 0.064, 0.004)], "shell", "palm" + L)
    # back-of-hand armour plate over the knuckles: three shield lines, red grip-lock indicator, the serial
    def bp(hw, t): return [(-hw - 0.004, t * 0.12), (-hw * 0.66, t / 2 + 0.008), (hw * 0.66, t / 2 + 0.008), (hw + 0.004, t * 0.12),
                           (hw + 0.001, t * 0.12 - 0.006), (hw * 0.64, t / 2 + 0.001), (-hw * 0.64, t / 2 + 0.001), (-hw - 0.001, t * 0.12 - 0.006)]
    ring_loft(O, U, Bv, D, [(0.026, bp(0.072, 0.086)), (0.09, bp(0.069, 0.08)), (0.15, bp(0.063, 0.066))], "door", "backplate" + L, bev=0.002)
    for k in range(3):
        v = 0.05 + k * 0.03; tb = interp([(0.026, 0.086), (0.09, 0.08), (0.15, 0.066)], v)
        bar(O + Bv * (tb / 2 + 0.0085) + D * v - U * 0.045, O + Bv * (tb / 2 + 0.0085) + D * v + U * 0.045, 0.0022, 0.0015, "dark", "shield_line", up=Bv, part="hand")
    rbox(O + U * 0.05 + Bv * (0.043 + 0.009) + D * 0.06, (0.016, 0.002, 0.034), "red", "grip_lock" + L, frame_quat(None, Bv, D), r=0.0008, seg=1, part="hand")
    stencil("2-0417", O + U * (-0.012) + Bv * (0.043 + 0.0092) + D * 0.035, Bv, up=-D, size=0.012, name="hand_serial")
    # thenar mound (faceted) and elongated palm pads along the grip lines (shingled)
    ring_loft(O, U, Bv, D, [(0.03, [(u - 0.045, b) for u, b in oct_sec(0.022, 0.03)]), (0.075, [(u - 0.045, b - 0.006) for u, b in oct_sec(0.02, 0.026)]),
                            (0.098, [(u - 0.042, b - 0.012) for u, b in oct_sec(0.012, 0.014)])], "shell", "thenar" + L)
    for pu, pv, ang, ln in ((-0.045, 0.06, 0.15, 0.06), (0.018, 0.045, -0.35, 0.07), (0.02, 0.1, 0.2, 0.08), (-0.01, 0.12, -0.1, 0.07)):
        dd = (D * math.cos(ang) + U * math.sin(ang)).normalized()
        rbox(O + U * pu + D * pv - Bv * (0.044 if abs(pu) < 0.03 else 0.038), (0.022, 0.005, ln), "rubber", "palm_pad", frame_quat(None, -Bv, dd), r=0.0018, seg=2, part="hand")
    # capsule thermal tiles on the back plate, the front flange carrying the effector
    for iu, iv in ((-1, 0), (1, 0), (-0.5, 1), (0.5, 1)):
        v = 0.045 + iv * 0.045; tb = interp([(0.026, 0.086), (0.09, 0.08), (0.15, 0.066)], v)
        rbox(O + U * (iu * 0.032) + Bv * (tb / 2 + 0.017) + D * v, (0.058, 0.007, 0.04), "tile", "capsule_tile", frame_quat(None, Bv, D), r=0.0018, seg=2, part="hand")
        for su in (-1, 1):
            for sv in (-1, 1): cyl(O + U * (iu * 0.032 + su * 0.02) + Bv * (tb / 2 + 0.008) + D * (v + sv * 0.013), O + U * (iu * 0.032 + su * 0.02) + Bv * (tb / 2 + 0.014) + D * (v + sv * 0.013), 0.003, "frame", "capsule_post", seg=8, part="hand")
    ring_loft(O, U, Bv, D, [(0.133, oct_sec(0.064, 0.066)), (0.147, oct_sec(0.06, 0.06))], "frame", "effector_flange" + L)
    stencil("ЭФФЕКТОР", O + U * 0.077 + D * 0.08, U, up=-D, size=0.009, name="capsule_mark")
    CAPSULE[s] = [o for o in COLL[0].objects if o not in before and o.type == 'MESH']
    # digits: index, the broad digit (middle+ring+little), the opposed thumb; relaxed curl
    def indir(ang): a = math.radians(ang); return (D * math.cos(a) + U * math.sin(a)).normalized()
    gaunt_digit(O + U * (-0.04) + D * 0.147 - Bv * 0.004, indir(-3), Bv, (0.058, 0.040, 0.034), 0.040, 0.040, [math.radians(18), math.radians(22)], "index" + L)
    gaunt_digit(O + U * 0.022 + D * 0.147 - Bv * 0.002, indir(3), Bv, (0.056, 0.040, 0.034), 0.080, 0.044, [math.radians(20), math.radians(24)], "broad" + L, broad=True)
    td = indir(-38); td = (td * math.cos(math.radians(34)) - Bv * math.sin(math.radians(34))).normalized()        # opposed, curled in
    gaunt_digit(O + U * (-0.066) + D * 0.09 - Bv * 0.016, td, Bv, (0.048, 0.038, 0.030), 0.040, 0.038, [math.radians(26), math.radians(30)], "thumb" + L)
    # the human hand inside (ghost, shown only in the cutaway): a fist on the grip controller
    g0 = set(COLL[0].objects)
    ring_loft(O, U, Bv, D, [(0.012, oct_sec(0.038, 0.05)), (0.07, oct_sec(0.044, 0.056)), (0.112, oct_sec(0.04, 0.05)), (0.124, oct_sec(0.03, 0.036))], "ghost", "ghost_hand" + L, bev=0.0)
    ring_loft(O + U * (-0.042) + D * 0.04, U, Bv, (D * 0.6 - Bv * 0.8).normalized(), [(0.0, oct_sec(0.012, 0.018)), (0.045, oct_sec(0.01, 0.015))], "ghost", "ghost_thumb" + L, bev=0.0)
    cyl(O + D * 0.075 - Bv * 0.012 - U * 0.055, O + D * 0.075 - Bv * 0.012 + U * 0.055, 0.013, "ghostgrip", "grip_controller" + L, seg=24, part="hand")
    GHOST[s] = [o for o in COLL[0].objects if o not in g0]
    for o in GHOST[s]: o.hide_render = True
    return O, D, Bv, U


# ------------------------------------------------------------------ rev. J thermal shield (by physics)
# Thin (3 mm) high-reflective ceramic plates on low standoff posts, 16-28 mm off the shell; large bands that go CONTINUOUSLY
# round each segment, shingled like roof tiles (each band overlaps the one below by 1/3 of its height, the upper band
# outermost), a folded lip at every lower edge (labyrinth: no line of sight to the shell); fans of sliding bands at the
# joints, small shield caps over the ring motors; the door-radiator and the evaporator slots stay uncovered.
SHIELD_NO = [0]


def _band_mesh(ringpts, name, part="shield", thick=0.003, posts=(), m="shieldp"):
    """ringpts: list of rings (each a list of points), the last ring is the lip; closed rings"""
    vs = []; fs = []; n = len(ringpts[0])
    for r in ringpts: vs += [V(p) for p in r]
    for k in range(len(ringpts) - 1):
        a, b = k * n, (k + 1) * n
        for j in range(n): j1 = (j + 1) % n; fs.append((a + j, a + j1, b + j1, b + j))
    o = mk(vs, fs, m, name, 0.5, part, mats=[m, "frame", m])
    so = o.modifiers.new("sol", "SOLIDIFY"); so.thickness = thick; so.offset = 0.0; so.material_offset_rim = 1
    bevel(o, 0.001, 1, 40)
    for a, b in posts: cyl(a, b, 0.0035, "frame", name + "_post", seg=8, part="standoffs")
    return o


def _open_band_mesh(grid, name, part="shield", thick=0.003, m="shieldp"):
    """grid[i][j]: rows of points (not closed in j)"""
    vs = []; fs = []; n = len(grid[0])
    for r in grid: vs += [V(p) for p in r]
    for k in range(len(grid) - 1):
        a, b = k * n, (k + 1) * n
        for j in range(n - 1): fs.append((a + j, a + j + 1, b + j + 1, b + j))
    o = mk(vs, fs, m, name, 0.5, part, mats=[m, "frame", m])
    so = o.modifiers.new("sol", "SOLIDIFY"); so.thickness = thick; so.offset = 0.0; so.material_offset_rim = 1
    bevel(o, 0.001, 1, 40)
    return o


def shield_bands(rows, z_top, z_bot, n, name, gap=0.016, step=0.004, trange=None, nt=120, mark=False, seams=3, lip=0.011, thick=0.003):
    """n shingled bands between z_top and z_bot over a limb/torso given by its section rows"""
    H = z_top - z_bot; hb = H / (n - (n - 1) / 3.0); out = []
    t0, t1 = trange if trange else (0.0, 2 * math.pi); closed = trange is None
    for i in range(n):
        zt = z_top - i * hb * 2.0 / 3.0; zb = zt - hb; off = gap + (n - 1 - i) * step
        def P(z, t, o):
            row = sample_rows(rows, z); p = sec_pt(row, t); c = V((row[1], row[2], p.z)); d = p - c; d.z = 0
            return p + d.normalized() * o
        ts = [t0 + (t1 - t0) * j / (nt if closed else nt - 1) for j in range(nt)]
        rings = [[P(zt, t, off) for t in ts], [P((zt + zb) / 2, t, off) for t in ts], [P(zb, t, off) for t in ts],
                 [P(zb + 0.006, t, off - lip) for t in ts]]                                              # the folded lip
        posts = []
        for j in range(0, nt, nt // 8 if closed else max(1, nt // 4)):
            t = ts[j]; zp = zb + hb * 0.3
            posts.append((P(zp, t, -0.002), P(zp, t, off - 0.002)))
        nm = "%s_band%d" % (name, i)
        o = _band_mesh(rings, nm, posts=posts, thick=thick) if closed else _open_band_mesh([r for r in rings], nm, thick=thick)
        if False and closed and "bnaero" in M:                    # (off: the strips round every band read as hoops)
            ro = off + thick / 2 - 0.0022
            _band_mesh([[P(zb + 0.002, t, ro) for t in ts], [P(zb - 0.005, t, ro) for t in ts]], nm + "_aero", thick=0.0025, m="bnaero")
            _band_mesh([[P(zb - 0.005, t, ro) for t in ts], [P(zb - 0.0085, t, ro) for t in ts]], nm + "_insul", thick=0.0025, m="dark")
        if not closed:
            for a, b in posts: cyl(a, b, 0.0035, "frame", nm + "_post", seg=8, part="standoffs")
        for k in range(seams if closed else 0):                                                       # plate joints (vertical seams)
            t = 2 * math.pi * (k + 0.5) / seams + 0.3
            bar(P(zt - 0.002, t, off + 0.0018), P(zb + 0.002, t, off + 0.0018), 0.0025, 0.0012, "shieldp", nm + "_seam", up=(P(zt, t, 1) - P(zt, t, 0)), part="shield")
        if mark:
            SHIELD_NO[0] += 1; pm = P(zb + hb * 0.22, -math.pi / 2, off + 0.002); nn = (P(zb + hb * 0.22, -math.pi / 2, 1) - P(zb + hb * 0.22, -math.pi / 2, 0)).normalized()
            stencil("%d" % SHIELD_NO[0], pm + V((0.03, 0, 0)), nn, size=0.013, m="stencil_red", name="shield_no")
        out.append(o)
    return out


def motor_cap(c, s, r, name):
    """a small shield cap over a ring motor: the motor stays visible as a disc under its edge"""
    c = V(c); ax = V((s, 0, 0))
    cyl(c + ax * 0.03, c + ax * 0.033, r * 0.78, "shieldp", name, seg=40, part="shield")
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.4; o = V((0, math.cos(a), math.sin(a))) * r * 0.45
        cyl(c + o + ax * 0.01, c + o + ax * 0.03, 0.0035, "frame", name + "_post", seg=8, part="standoffs")


def surf_patch(bvh, C, az, el, off, name, nu=18, nv=8):
    """a shield plate over a surface patch found by rays from C (azimuth around z from +x, front = -90; elevation deg)"""
    grid = []
    for i in range(nv + 1):
        e = math.radians(el[0] + (el[1] - el[0]) * i / nv); row = []
        for j in range(nu + 1):
            a = math.radians(az[0] + (az[1] - az[0]) * j / nu); d = V((math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)))
            h = bvh.ray_cast(C + d * 0.5, -d, 0.6)
            p = h[0] if h[0] is not None else C + d * 0.2; nrm = d
            row.append(p + nrm * off)
        grid.append(row)
    lipr = [p + (C - p).normalized() * 0.008 + V((0, 0, 0.004)) for p in grid[0]]
    return _open_band_mesh([lipr] + grid, name)


# ------------------------------------------------------------------ rev. J hand: variant 2 «Ладонь» (sketch/build_sketch_mech_hands.py v2)
# x forward from the coupling face (= v along D), y up in the side view (= b, back of the hand), y in the top view
# (thumb side positive = -U here). The man's flat hand lies in a cradle with finger-ring sensors inside the palm-shaped
# capsule; the effector fingers start at the capsule's front (the man's knuckle line).
CAP_SIDE_TOP = [(0.0, 0.036), (0.03, 0.036), (0.15, 0.008), (0.165, 0.002)]   # rev. L: the back slopes down to the knuckles
CAP_SIDE_BOT = [(0.0, -0.027), (0.08, -0.028), (0.15, -0.026), (0.165, -0.016)]
CAP_TOP_W = [(0.0, 0.046), (0.12, 0.058), (0.15, 0.056), (0.165, 0.046)]        # trapezoid widening to the knuckles, bevel


def cap_sec(v, grow=0.0):
    bt, bb, hw = interp(CAP_SIDE_TOP, v) + grow, interp(CAP_SIDE_BOT, v) - grow, interp(CAP_TOP_W, v) + grow
    t = bt - bb; c = (bt + bb) / 2
    return [(hw * 0.68, bt), (hw, c + t * 0.22), (hw, c - t * 0.18), (hw * 0.66, bb), (0.0, bb + 0.008), (-hw * 0.66, bb), (-hw, c - t * 0.18),
            (-hw, c + t * 0.22), (-hw * 0.68, bt), (0.0, bt + 0.002)]


def mech_digit(start, d, Bv, segs, w, t, curl, name, pad_side=-1, curl_to=None, lines=False):
    """a machine digit: chamfered tapering segments, a ring motor in every joint, the contact pad on the gripping face"""
    p = V(start); d = d.normalized()
    for k, L in enumerate(segs):
        Bk = (Bv - d * Bv.dot(d)).normalized(); Uk = d.cross(Bk).normalized()
        w0, w1 = w * (1 - 0.07 * k), w * (1 - 0.07 * (k + 1)); t0, t1 = t * (1 - 0.08 * k), t * (1 - 0.08 * (k + 1))
        last = k == len(segs) - 1
        rings = [(0.006 if k else 0.0, oct_sec(w0 / 2, t0)), (L - (0.012 if last else 0.006), oct_sec(w1 / 2, t1))]
        if last: rings.append((L, oct_sec(w1 / 2 * 0.62, t1 * 0.6)))
        ring_loft(p, Uk, Bk, d, rings, "shell", name + "_seg%d" % k, bev=0.0015)
        pn = Bk * pad_side
        if curl_to is not None: pn = (V(curl_to) - d * V(curl_to).dot(d)).normalized()
        pc = p + d * (L * 0.52) + pn * (t0 / 2 + 0.0015)
        rbox(pc, (w0 * 0.6, 0.0035, L * 0.62), "rubber", name + "_pad%d" % k, frame_quat(None, pn, d), r=0.0012, seg=2, part="hand")
        if lines:
            for gu in (-w0 / 6, w0 / 6):
                bar(p + Uk * gu + Bk * (t0 / 2 + 0.0008) + d * 0.008, p + Uk * gu + Bk * (t1 / 2 + 0.0008) + d * (L - 0.01), 0.0018, 0.0014, "dark", name + "_line", up=Bk, part="hand")
        p = p + d * L
        if not last:
            cyl(p - Uk * (w1 / 2 + 0.0015), p + Uk * (w1 / 2 + 0.0015), t1 * 0.46, "bear", name + "_motor%d" % k, seg=24, part="hand")
            cv = -Bk if curl_to is None else (V(curl_to) - d * V(curl_to).dot(d)).normalized()
            d = (d * math.cos(curl) + cv * math.sin(curl)).normalized()


def hand_B3(s, R, sc, L):
    O, D, Bv, U = hand_frame(s, R, sc)
    before = set(COLL[0].objects)
    TH = -U                                                                                              # the thumb side (sketch +y top view)
    ring_loft(O, U, Bv, D, [(-0.045, oct_sec(0.056, 0.092)), (-0.014, oct_sec(0.058, 0.084)), (0.0, oct_sec(0.058, 0.078))], "shell", "cuff" + L)
    ring_loft(O, U, Bv, D, [(-0.006, oct_sec(0.061, 0.082)), (0.006, oct_sec(0.061, 0.082))], "bear", "coupling" + L)
    for k in range(18):
        u_ = -0.054 + k * 0.0064; rbox(O + U * u_ + Bv * 0.0415 + D * 0.0, (0.0022, 0.003, 0.011), "fin", "coupling_rib", frame_quat(None, Bv, D), r=0.0004, seg=1, part="hand")
    cyl(O + U * 0.061, O + U * 0.07, 0.007, "red", "coupling_release" + L, seg=20, part="hand")
    # the palm-shaped capsule (thicker at the wrist end, tapering to the knuckle line), its knuckle motor bar
    ring_loft(O, U, Bv, D, [(v, cap_sec(v)) for v in (0.006, 0.05, 0.10, 0.13, 0.15, 0.16)], "shell", "capsule" + L)
    cyl(O + D * 0.168 - Bv * 0.006 - U * 0.046, O + D * 0.168 - Bv * 0.006 + U * 0.046, 0.015, "bear", "knuckle_motor" + L, seg=32, part="hand")
    ring_loft(O, U, Bv, D, [(0.148, [(u * 0.98, b) for u, b in ((0.05, 0.010), (0.053, 0.0), (-0.053, 0.0), (-0.05, 0.010), (0.0, 0.019))]),
                            (0.166, [(u * 0.9, b) for u, b in ((0.045, 0.004), (0.047, -0.004), (-0.047, -0.004), (-0.045, 0.004), (0.0, 0.012))])],
              "frame", "knuckle_ridge" + L)                                                               # the low knuckle ridge
    th = -U                                                                                              # thenar mound (thumb side, underside)
    ring_loft(O, U, Bv, D, [(0.02, [(u - 0.036 * 1, b - 0.02) for u, b in oct_sec(0.018, 0.024)]), (0.06, [(u - 0.038, b - 0.03) for u, b in oct_sec(0.022, 0.03)]),
                            (0.095, [(u - 0.036, b - 0.032) for u, b in oct_sec(0.018, 0.026)])], "shell", "thenar" + L)
    CAPSULE[s] = [o for o in COLL[0].objects if o not in before and o.type == 'MESH']
    # the capsule's shield: ONE bent, chamfered back plate over the back and sides, ONE palm plate, on posts, a thin gap
    def shp(v, idx, o):
        sec = cap_sec(v); c = sum(b for _, b in sec) / len(sec); u, b = sec[idx]; dl = math.hypot(u, b - c); k = 1 + o / max(dl, 1e-6)
        return O + D * v + U * (u * k) + Bv * (c + (b - c) * k)
    vs_ = [0.008, 0.04, 0.08, 0.12, 0.15, 0.16]
    _open_band_mesh([[shp(v, j, 0.009) for j in (2, 1, 0, 9, 8, 7, 6)] for v in vs_], "capsule_back" + L, part="hand")
    _open_band_mesh([[shp(v, j, 0.008) for j in (3, 4, 5)] for v in vs_[:-1]], "capsule_palm" + L, part="hand")
    for v in (0.03, 0.13):
        for j in (0, 8, 4):
            cyl(shp(v, j, 0.0), shp(v, j, 0.008), 0.0032, "frame", "capsule_post", seg=8, part="hand")
    stencil("ЭФФЕКТОР", shp(0.08, 1, 0.0105) + U * 0.0 , U, up=-D, size=0.008, name="capsule_mark")
    CAPSULE[s] = [o for o in COLL[0].objects if o not in before and o.type == 'MESH']
    # effector fingers at the capsule front: index, the broad digit, the thumb from the thenar underside; natural curl
    def dirv(side_deg, top_deg):
        a, b = math.radians(side_deg), math.radians(top_deg)
        return (D * math.cos(a) * math.cos(b) + Bv * math.sin(a) + TH * math.sin(b)).normalized()
    mech_digit(O + D * 0.172 + Bv * 0.002 + TH * 0.022, dirv(-6, 3), Bv, (0.055, 0.040, 0.032), 0.024, 0.022, math.radians(18), "index" + L)
    mech_digit(O + D * 0.172 + Bv * 0.002 - TH * 0.018, dirv(-6, -2), Bv, (0.044, 0.036, 0.030), 0.046, 0.028, math.radians(16), "broad" + L, lines=True)
    for k in (-1, 1):                                                                                    # broad digit's separation lines
        pass
    tb = O + D * 0.1 - Bv * 0.036 + TH * 0.036                                                            # out of the thenar mound, opposed under the index
    cyl(tb - D * 0.015, tb + D * 0.015, 0.016, "bear", "thumb_motor" + L, seg=28, part="hand")
    tdir = (D * 0.82 - Bv * 0.42 + TH * 0.2).normalized()
    mech_digit(tb, tdir, Bv, (0.048, 0.038), 0.024, 0.024, math.radians(26), "thumb" + L, curl_to=(-TH * 0.75 + Bv * 0.45))
    # the man's hand inside (ghost): flat in the cradle, finger-ring sensors at the knuckle line
    g0 = set(COLL[0].objects)
    ring_loft(O, U, Bv, D, [(0.006, oct_sec(0.040, 0.03)), (0.08, oct_sec(0.044, 0.028)), (0.105, oct_sec(0.043, 0.024))], "ghost", "ghost_palm" + L, bev=0.0)
    for k, u in enumerate((-0.03, -0.01, 0.01, 0.03)):
        ring_loft(O + U * u + D * 0.105, U, Bv, D, [(0.0, oct_sec(0.0085, 0.017)), (0.05 - 0.006 * abs(k - 1.5), oct_sec(0.007, 0.014))], "ghost", "ghost_finger" + L, bev=0.0)
        hand_ring(O + U * u, D, U, Bv, 0.135, 0.143, 0.012, 0.012, "ghostgrip", "finger_ring" + L, n=2.0, seg=16)
    ring_loft(O + TH * 0.045 + D * 0.03, U, Bv, (D * 0.7 + TH * 0.5).normalized(), [(0.0, oct_sec(0.011, 0.018)), (0.05, oct_sec(0.009, 0.015))], "ghost", "ghost_thumb" + L, bev=0.0)
    ring_loft(O, U, Bv, D, [(0.0, [(u, b - 0.022) for u, b in oct_sec(0.05, 0.008)]), (0.15, [(u, b - 0.02) for u, b in oct_sec(0.048, 0.008)])], "ghostgrip", "cradle" + L, bev=0.0)
    GHOST[s] = [o for o in COLL[0].objects if o not in g0]
    for o in GHOST[s]: o.hide_render = True
    return O, D, Bv, U


def body_shield(arm_data):
    """rev. J: all the shield - legs, joints, arms, torso (front and sides; the door stays open to the sky), motor caps"""
    for s in (1, -1):
        L = "L" if s > 0 else "R"; thigh, shin, knee = leg_rows(s)
        shield_bands(thigh, 0.83, 0.535, 2, "thigh" + L, mark=False, gap=0.013, step=0.004, seams=2, thick=0.008)   # rev. O: 8 mm tile skin
        shield_bands(shin, 0.455, 0.18, 2, "shin" + L, mark=False, gap=0.013, step=0.004, seams=2, thick=0.008)
        shield_bands(knee + [r for r in thigh if r[0] <= 0.56], 0.55, 0.44, 3, "knee" + L, gap=0.016, step=0.004, thick=0.008)  # heavier fan
        ank = [(z, s * LEG_C, -0.006, s, w, w, f, b, 0.004, 0.002, 0, 0) for z, w, f, b in ((0.14, 0.075, 0.086, 0.064), (0.172, 0.073, 0.084, 0.062))]
        shield_bands(ank, 0.19, 0.162, 1, "ankle" + L, gap=0.007, seams=2, thick=0.006)
        fa, R, sc, fore, upper = arm_data[s]
        shield_bands(fore, 1.06, 0.83, 2, "forearm" + L, gap=0.012, step=0.004)
        shield_bands(upper, 1.36, 1.135, 2, "upperarm" + L, gap=0.012, step=0.004)
        tr = (-0.85, 0.85) if s > 0 else (math.pi - 0.85, math.pi + 0.85)
        shield_bands(upper, 1.425, 1.355, 2, "shoulder" + L, gap=0.014, step=0.003, trange=tr, nt=24)          # two small shoulder plates
    # rev. M: the chest shield is built from the master sketch (chest_plates_M, called from build)
    brief_rows = [(z, 0.0, 0.0, 1, w, w, d, d, d, 0.0, 0.05) for z, w, d in ((0.83, 0.258, 0.139), (0.90, 0.253, 0.137), (0.97, 0.240, 0.138))]
    shield_bands(brief_rows, 0.965, 0.905, 1, "hip", gap=0.016)


# ------------------------------------------------------------------ rev. M: chest shield and hand capsule from the master sketch
# sketch/build_sketch_chest_hand.py (all coordinates in metres, the source of truth). Every facet one flat plane, flat
# shading, Freestyle lines on the edges (dark seams), a sun key from the upper front side.
def facet(pts, thick, m, name, part="shield", lip=None):
    """a flat plate from a planar 3D polygon: front face, back face offset along -normal, side walls; optional inward lip
    along the edge lip=(i, j) (indices of the lower edge), lip direction given as a vector"""
    pts = [V(q) for q in pts]; n = len(pts)
    nrm = V((0, 0, 0))
    for i in range(n): nrm += (pts[i] - pts[0]).cross(pts[(i + 1) % n] - pts[0])
    nrm.normalize()
    vs = pts + [q - nrm * thick for q in pts]
    fs = [tuple(range(n)), tuple(reversed(range(n, 2 * n)))] + [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    o = mk(vs, fs, m, name, 0.01, part, mats=[m, "frame"], fm=[0, 0] + [1] * n)                 # rev. P: graphite edge trim
    if lip:
        (i, j), dv = lip; a, b = pts[i], pts[j]; dv = V(dv)
        mk([a, b, b + dv, a + dv, a - nrm * thick, b - nrm * thick, b + dv - nrm * thick, a + dv - nrm * thick],
           [(0, 1, 2, 3), (7, 6, 5, 4), (0, 4, 5, 1), (1, 5, 6, 2), (2, 6, 7, 3), (3, 7, 4, 0)], "frame", name + "_lip", 0.01, part)
    return o, nrm


CH = {   # side profile of each plate at the sternum ridge: (z_top, y_top), (z_bot, y_bot); inner slope (ridge drop per m of x)
    "C1": (((1.452, 0.130), (1.372, 0.205)), 0.05),
    "C2": (((1.398, 0.205), (1.215, 0.207)), 0.015 / 0.18),
    "C3": (((1.242, 0.198), (1.118, 0.180)), 0.0833 * 14 / 22),
    "C4": (((1.142, 0.178), (1.040, 0.150)), 0.0833 * 8 / 22)}
C1 = [(0.0, 1.445), (0.150, 1.445), (0.215, 1.405), (0.205, 1.370), (0.0, 1.372)]
C2i = [(0.0, 1.395), (0.180, 1.395), (0.195, 1.225), (0.0, 1.215)]
C2o = [(0.180, 1.395), (0.215, 1.405), (0.272, 1.360), (0.262, 1.250), (0.195, 1.225)]
C3i = [(0.0, 1.240), (0.180, 1.240), (0.170, 1.125), (0.0, 1.118)]
C3o = [(0.180, 1.240), (0.250, 1.250), (0.236, 1.140), (0.170, 1.125)]
C4i = [(0.0, 1.140), (0.165, 1.140), (0.150, 1.048), (0.0, 1.040)]
C4o = [(0.165, 1.140), (0.228, 1.150), (0.212, 1.060), (0.150, 1.048)]
K_OUT = (0.192 - 0.120) / (0.262 - 0.180)                         # the outer wrap plane (plan section z = 1.30)


SEAM_OBJS = []                                                                   # rev. N: the only objects that get dark seam lines


def chest_plates_M(hut_bvh):
    b0 = set(COLL[0].objects)
    def yr(key, z):
        (zt, yt), (zb, yb) = CH[key][0]; return yt + (yb - yt) * (z - zt) / (zb - zt)
    def inner(key, x, z): return yr(key, z) - CH[key][1] * x
    def make(key, poly_i, poly_o=None, brk=None):
        for s in (1, -1):
            def P(x, z, outer=False):
                if not outer: y = inner(key, x, z)
                else:
                    (x0, z0), (x1, z1) = brk; xb = x0 + (x1 - x0) * (z - z0) / (z1 - z0)
                    y = inner(key, xb, z) - K_OUT * (x - xb)
                return V((s * x * 1.01, -y - 0.003, z))                                     # rev. R: on the P3 torso (+3 mm)
            pi = [P(x, z) for x, z in poly_i]
            lo = sorted(range(len(poly_i)), key=lambda i: poly_i[i][1])[:2]; lo.sort()
            facet(pi if s > 0 else pi[::-1], 0.004, "shieldp", key + "i", lip=((lo[0], lo[1]) if s > 0 else (len(pi) - 1 - lo[1], len(pi) - 1 - lo[0]), (0, 0.014, 0.004)))
            if poly_o:
                po = [P(x, z, True) for x, z in poly_o]
                lo = sorted(range(len(poly_o)), key=lambda i: poly_o[i][1])[:2]; lo.sort()
                facet(po if s > 0 else po[::-1], 0.004, "shieldp", key + "o", lip=((lo[0], lo[1]) if s > 0 else (len(po) - 1 - lo[1], len(po) - 1 - lo[0]), (-s * 0.008, 0.012, 0.004)))
        return P
    make("C4", C4i, C4o, ((0.165, 1.140), (0.150, 1.048)))
    make("C3", C3i, C3o, ((0.180, 1.240), (0.170, 1.125)))
    make("C2", C2i, C2o, ((0.180, 1.395), (0.195, 1.225)))
    make("C1", C1)
    for z, key in ((1.43, "C1"), (1.33, "C2"), (1.25, "C2"), (1.17, "C3"), (1.09, "C4")):         # standoff posts with heads
        for s in (1, -1):
            for xs in (0.03, 0.15):
                p = V((s * xs * 1.01, -inner(key, xs, z) - 0.003, z)); h = hut_bvh.ray_cast(p, V((0, 1, 0)), 0.2)     # rev. Q: with the plates
                q = h[0] if h[0] is not None else p + V((0, 0.03, 0))
                cyl(p - V((0, 0.002, 0)), q, 0.0035, "frame", "chest_post", seg=10, part="standoffs")
                cyl(p - V((0, 0.0012, 0)), p - V((0, 0.0045, 0)), 0.0045, "bear", "chest_post_head", seg=12, part="standoffs")
    SEAM_OBJS.extend(o for o in COLL[0].objects if o not in b0 and "chest_post" not in o.name)


# hand (sketch: x forward from the coupling face, y lateral thumb side +, z up = back of the hand)
H_TOP = [(0.0, 0.040), (0.060, 0.042), (0.145, 0.026), (0.160, 0.014)]          # back at the crease
H_CR = 0.004; H_T12 = math.tan(math.radians(12))
H_YT = [(0.0, 0.050), (0.115, 0.062), (0.145, 0.050), (0.160, 0.044)]          # thumb-side outline (chamfer outer edge)
H_YL = [(0.0, -0.050), (0.115, -0.066), (0.145, -0.054), (0.160, -0.050)]
H_ZP = [(0.0, -0.034), (0.110, -0.030), (0.160, -0.026)]                         # palm-side wall bottom


def hand_B3(s, R, sc, L):
    O, D, Bv, U = hand_frame(s, R, sc); TH = -U
    def W(x, y, z): return O + D * x + TH * y + Bv * z
    before = set(COLL[0].objects)
    # forearm end (cuff) and the coupling ring with its red release
    ring_loft(O, -TH, Bv, D, [(-0.04, [(u, b + 0.0025) for u, b in oct_sec(0.05, 0.082)]), (-0.02, [(u, b + 0.0015) for u, b in oct_sec(0.048, 0.077)])], "shell", "cuff" + L)
    ring_loft(O, -TH, Bv, D, [(-0.02, [(u, b + 0.0015) for u, b in oct_sec(0.052, 0.087)]), (0.0, [(u, b + 0.0015) for u, b in oct_sec(0.052, 0.087)])], "bear", "coupling" + L)
    cyl(W(-0.010, 0.052, 0.0), W(-0.010, 0.062, 0.0), 0.005, "red", "coupling_release" + L, seg=20, part="hand")
    # the capsule wall (enamel) under the plates
    rows = []
    for x in (0.0, 0.06, 0.115, 0.145, 0.16):
        c, yt, yl, zp = interp(H_TOP, x), interp(H_YT, x) - 0.003, interp(H_YL, x) + 0.003, interp(H_ZP, x)
        rows.append((x, [(-yt, c - 0.030), (-yt, zp + 0.004), (-yt + 0.006, zp), (-yl - 0.006, zp), (-yl, zp + 0.004), (-yl, c - 0.030), (-H_CR, c - 0.009)]))
    ring_loft(O, U, Bv, D, rows, "shell", "capsule" + L, sharp=0.01, bev=0.0)
    # the back plate: two metacarpal planes either side of the crease (12 deg), thin steep chamfers, flat to x 0.06 then the
    # 12 deg slope to 0.145, the 35 deg knuckle bevel to the knuckle line
    def zb(x, y): return interp(H_TOP, x) - abs(y - H_CR) * H_T12
    def edge(tab, x): return interp(tab, x)
    def P3(x, y, z=None): return W(x, y, zb(x, y) if z is None else z)
    for nm, side in (("A", 1), ("B", -1)):
        tab = H_YT if side > 0 else H_YL
        e0, e06, e115 = edge(tab, 0.0) - side * 0.004, edge(tab, 0.06) - side * 0.0045, edge(tab, 0.115) - side * 0.006
        e145 = edge(tab, 0.145)
        for k, pts in enumerate(([(0.0, H_CR), (0.0, e0), (0.06, e06), (0.06, H_CR)],
                                 [(0.06, H_CR), (0.06, e06), (0.115, e115), (0.145, e145), (0.145, H_CR)])):
            facet([P3(x, y) for x, y in (pts if side > 0 else pts[::-1])], 0.004, "shieldp", "hand_%s%d" % (nm, k) + L, part="hand")
        for k, (xa, xb_) in enumerate(((0.0, 0.06), (0.06, 0.115))):                              # the chamfer strips
            ya_i, yb_i = (e0, e06) if k == 0 else (e06, e115); ya_o, yb_o = edge(tab, xa), edge(tab, xb_)
            zc = lambda x: interp(H_TOP, x) - 0.028
            pts = [P3(xa, ya_i), P3(xa, ya_o, zc(xa)), P3(xb_, yb_o, zc(xb_)), P3(xb_, yb_i)]
            facet(pts if side > 0 else pts[::-1], 0.004, "shieldp", "hand_%sc%d" % (nm, k) + L, part="hand")
    kpts = [P3(0.145, H_CR), P3(0.145, edge(H_YT, 0.145)), P3(0.16, edge(H_YT, 0.16)), P3(0.16, H_CR)]
    facet(kpts, 0.004, "shieldp", "hand_K_thumb" + L, part="hand")
    kpts = [P3(0.145, H_CR), P3(0.16, H_CR), P3(0.16, edge(H_YL, 0.16)), P3(0.145, edge(H_YL, 0.145))]
    facet(kpts, 0.004, "shieldp", "hand_K_little" + L, part="hand")
    for x, y in ((0.03, 0.03), (0.03, -0.03), (0.11, 0.035), (0.11, -0.04)):                   # post heads
        cyl(P3(x, y) + Bv * 0.0, P3(x, y) + Bv * 0.0025, 0.003, "bear", "hand_post_head", seg=12, part="hand")
    rbox(W(0.105, -0.035, zb(0.105, -0.035) + 0.001), (0.01, 0.002, 0.03), "red", "grip_lock" + L, frame_quat(None, (Bv - TH * H_T12).normalized(), D), r=0.0006, seg=1, part="hand")
    # palm plate (slightly concave), the knuckle ridge bar, the thenar mound (3-facet block)
    grid = []
    for i in range(7):
        x = 0.16 * i / 6; yt, yl = interp(H_YT, x) * 0.95, interp(H_YL, x) * 0.95; row = []
        for j in range(9):
            y = yl + (yt - yl) * j / 8; row.append(W(x, y, interp(H_ZP, x) - 0.002 + 0.004 * (1 - abs(y) / 0.06)))
        grid.append(row)
    pp = _open_band_mesh(grid, "palm_plate" + L, part="hand", thick=0.006)
    rbox(W(0.164, -0.003, -0.008), (0.094, 0.04, 0.008), "bear", "knuckle_ridge" + L, frame_quat(None, D, Bv), r=0.002, part="hand")
    import bmesh as _bm
    tp = [(0.030, 0.056, -0.031), (0.110, 0.066, -0.029), (0.118, 0.07, -0.032), (0.045, 0.062, -0.033),
          (0.040, 0.060, -0.040), (0.085, 0.072, -0.055), (0.120, 0.072, -0.046), (0.090, 0.086, -0.050), (0.060, 0.080, -0.054),
          (0.080, 0.090, -0.040), (0.118, 0.082, -0.038)]
    bm = _bm.new()
    for x, y, z in tp: bm.verts.new(W(x, y, z))
    _bm.ops.convex_hull(bm, input=bm.verts[:])
    me = bpy.data.meshes.new("thenar"); bm.to_mesh(me); bm.free()
    vs_ = [v.co.copy() for v in me.vertices]; fs_ = [tuple(p.vertices) for p in me.polygons]; bpy.data.meshes.remove(me)
    mk(vs_, fs_, "shieldp", "thenar" + L, 0.01, "hand")
    CAPSULE[s] = [o for o in COLL[0].objects if o not in before and o.type == 'MESH']
    # effector fingers as in K/L; the thumb out of the thenar, curling under the index
    def dirv(side_deg, top_deg):
        a, b = math.radians(side_deg), math.radians(top_deg)
        return (D * math.cos(a) * math.cos(b) + Bv * math.sin(a) + TH * math.sin(b)).normalized()
    mech_digit(W(0.168, 0.026, -0.004), dirv(-6, 0), Bv, (0.044, 0.036, 0.030), 0.024, 0.028, math.radians(16), "index" + L)
    mech_digit(W(0.168, -0.018, -0.004), dirv(-6, 0), Bv, (0.044, 0.036, 0.030), 0.046, 0.028, math.radians(16), "broad" + L, lines=True)
    cyl(W(0.104, 0.075, -0.050) - D * 0.012, W(0.104, 0.075, -0.050) + D * 0.012, 0.0075 * 2, "bear", "thumb_motor" + L, seg=24, part="hand")
    mech_digit(W(0.104, 0.075, -0.050), dirv(-40, -15), Bv, (0.042, 0.034), 0.026, 0.026, math.radians(24), "thumb" + L, curl_to=(-TH * 0.75 + Bv * 0.35))
    # the man's hand inside (ghost): flat, the finger-ring sensors at the knuckle line
    g0 = set(COLL[0].objects)
    gr = [(x, [(u, b) for u, b in ((0.04, zt), (0.04, zb_), (-0.04, zb_), (-0.04, zt))]) for x, zt, zb_ in ((0.005, 0.020, -0.018), (0.12, 0.012, -0.014), (0.15, 0.006, -0.010))]
    ring_loft(O, -TH, Bv, D, gr, "ghost", "ghost_hand" + L, bev=0.0)
    for u in (-0.03, -0.01, 0.01, 0.03): hand_ring(O + TH * u, D, -TH, Bv, 0.13, 0.138, 0.009, 0.012, "ghostgrip", "finger_ring" + L, n=2.0, seg=16)
    GHOST[s] = [o for o in COLL[0].objects if o not in g0]
    for o in GHOST[s]: o.hide_render = True
    return O, D, Bv, U


def shoot_frame(cam, name, loc, fwd, up, lens=None, ortho=None, res=(1200, 1200)):
    """camera with an explicit frame: looking along fwd, image up = up"""
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = (res[0] // 2, res[1] // 2) if QUICK else res
    cam.data.type = 'ORTHO' if ortho else 'PERSP'
    if ortho: cam.data.ortho_scale = ortho
    else: cam.data.lens = lens
    f = V(fwd).normalized(); u = (V(up) - f * f.dot(V(up))).normalized(); r = f.cross(u)
    cam.matrix_world = Matrix.Translation(V(loc)) @ Matrix((r, u, -f)).transposed().to_4x4()
    bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, name + ".png"); bpy.ops.render.render(write_still=True); log.append("rendered " + name)


LEG_C = 0.135                                   # rev. O: leg centre (stance 10 mm wider than N)
# ------------------------------------------------------------------ rev. O: hex ceramic tile skin, the +500 C boot
HEX_R = 0.0202                                  # hex circumradius: 35 mm across the flats
HEX_W = 7 * math.sqrt(3) * HEX_R               # one texture tile = 7 x 4 hex pairs (tileable, 1 % anisotropy)


def hex_image(N=1024):
    """a tileable hex-tile map generated in memory: R = tile height (0 in the grout, bevelled to 1), G = per-tile tone"""
    import numpy as np
    R = HEX_R; sx = math.sqrt(3) * R; sy = 3 * R; k, m = 7, 4; W = k * sx; H = m * sy
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float64)
    X = (xs + 0.5) / N * W; Y = (ys + 0.5) / N * H
    best = None
    rng = np.random.RandomState(7); tone = rng.rand(2, k + 1, m + 1)
    for lid, (ox, oy) in enumerate(((0.0, 0.0), (sx / 2, sy / 2))):
        i = np.round((X - ox) / sx); j = np.round((Y - oy) / sy)
        dx = X - (ox + i * sx); dy = Y - (oy + j * sy); d2 = dx * dx + dy * dy
        tid = tone[lid, (i.astype(int) % k), (j.astype(int) % m)]
        if best is None: best = [d2, dx, dy, tid]
        else:
            sel = d2 < best[0]
            for n_, v in enumerate((d2, dx, dy, tid)): best[n_] = np.where(sel, v, best[n_])
    _, dx, dy, tid = best
    norm = np.maximum(np.abs(dx), 0.5 * np.abs(dx) + 0.8660254 * np.abs(dy)); rin = sx / 2
    h = np.clip((rin - norm - 0.0013) / 0.0025, 0.0, 1.0)                     # grout ~2.6 mm, a 2.5 mm bevel
    px = np.zeros((N, N, 4), dtype=np.float32); px[..., 0] = h; px[..., 1] = tid; px[..., 3] = 1.0
    img = bpy.data.images.new("PantsirHex", N, N, alpha=False, float_buffer=True)
    img.colorspace_settings.name = 'Non-Color'; img.pixels.foreach_set(px.ravel()); img.update(); img.pack()
    return img


def hex_material(m):
    """the shield = a mosaic of hex ceramic tiles: relief (bump), thin dark grout, slight tone scatter, matte"""
    nt = m.node_tree; p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    base = tuple(p.inputs["Base Color"].default_value)
    p.inputs["Roughness"].default_value = 0.66; p.inputs["Coat Weight"].default_value = 0.0
    tc = nt.nodes.new("ShaderNodeTexCoord"); mp = nt.nodes.new("ShaderNodeMapping"); mp.inputs["Scale"].default_value = (1 / HEX_W,) * 3
    it = nt.nodes.new("ShaderNodeTexImage"); it.image = hex_image(); it.projection = 'BOX'; it.projection_blend = 0.3; it.interpolation = 'Linear'
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"]); nt.links.new(mp.outputs["Vector"], it.inputs["Vector"])
    sp = nt.nodes.new("ShaderNodeSeparateColor"); nt.links.new(it.outputs["Color"], sp.inputs["Color"])
    tn = nt.nodes.new("ShaderNodeMapRange"); tn.inputs["To Min"].default_value = 0.84; tn.inputs["To Max"].default_value = 1.16
    nt.links.new(sp.outputs["Green"], tn.inputs["Value"])
    hs = nt.nodes.new("ShaderNodeHueSaturation"); hs.inputs["Color"].default_value = base
    nt.links.new(tn.outputs["Result"], hs.inputs["Value"])
    gr = nt.nodes.new("ShaderNodeMapRange"); gr.inputs["From Max"].default_value = 0.3; gr.inputs["To Min"].default_value = 1.0; gr.inputs["To Max"].default_value = 0.0
    nt.links.new(sp.outputs["Red"], gr.inputs["Value"])
    mx = nt.nodes.new("ShaderNodeMix"); mx.data_type = 'RGBA'
    nt.links.new(gr.outputs["Result"], mx.inputs[0]); nt.links.new(hs.outputs["Color"], mx.inputs[6]); mx.inputs[7].default_value = (0.045, 0.047, 0.05, 1)
    nt.links.new(mx.outputs[2], p.inputs["Base Color"])
    b = nt.nodes.new("ShaderNodeBump"); b.inputs["Strength"].default_value = 0.9; b.inputs["Distance"].default_value = 0.003
    nt.links.new(sp.outputs["Red"], b.inputs["Height"]); nt.links.new(b.outputs["Normal"], p.inputs["Normal"])


def ring_motor_ax(c, ax, r, thick, name, fins=36):
    """ring_motor with any axis (the ankle inversion drive, the metatarsal hinge drives)"""
    c = V(c); ax = V(ax).normalized(); up = V((0, 0, 1)) - ax * ax.z
    up = up.normalized() if up.length > 0.2 else ax.orthogonal().normalized(); e2 = ax.cross(up)
    cyl(c - ax * thick / 2, c + ax * thick / 2, r, "frame", name, seg=48, part="drives")
    cyl(c + ax * thick / 2, c + ax * (thick / 2 + 0.003), r * 0.82, "bear", name + "_face", seg=48, part="drives")
    cyl(c + ax * (thick / 2 + 0.003), c + ax * (thick / 2 + 0.0055), r * 0.3, "hub", name + "_hub", seg=24, part="drives")
    fins = max(fins, int(2 * math.pi * r / 0.0042))
    for k in range(fins):
        a = 2 * math.pi * k / fins; rad = up * math.cos(a) + e2 * math.sin(a)
        rib(c + rad * (r + 0.0008), ax, rad.cross(ax), rad, thick * 0.36, 0.0009, 0.0016)
    for k in range(6):
        a = math.pi / 6 + k * math.pi / 3; bolt(c + ax * (thick / 2 + 0.003) + (up * math.cos(a) + e2 * math.sin(a)) * r * 0.62, ax)
    cyl(c + up * r, c + up * (r + 0.007), 0.0045, "red", name + "_concap", seg=16, part="drives")


def hull_obj(pts, m, name, part="boots"):
    """a faceted convex shell from a point cloud (flat shading)"""
    bm = bmesh.new(); vv = [bm.verts.new(V(p)) for p in pts]
    res = bmesh.ops.convex_hull(bm, input=vv)
    dv = list({g for g in res["geom_interior"] + res["geom_unused"] if isinstance(g, bmesh.types.BMVert)})
    if dv: bmesh.ops.delete(bm, geom=dv, context='VERTS')
    bm.verts.index_update(); vs = [v.co.copy() for v in bm.verts]; fs = [tuple(v.index for v in f.verts) for f in bm.faces]; bm.free()
    return mk(vs, fs, m, name, 0.01, part)


def _in_poly(poly, x, f):
    sg = 0
    for (x0, f0), (x1, f1) in zip(poly, poly[1:] + poly[:1]):
        c = (x1 - x0) * (f - f0) - (f1 - f0) * (x - x0)
        if abs(c) < 1e-12: continue
        if sg == 0: sg = 1 if c > 0 else -1
        elif (c > 0) != (sg > 0): return False
    return True


def _inset(poly, d):
    cx = sum(p[0] for p in poly) / len(poly); cf = sum(p[1] for p in poly) / len(poly); out = []
    for x, f in poly:
        v = V((cx - x, cf - f, 0)); L = v.length; out.append((x + v.x / L * d, f + v.y / L * d))
    return out


F_H, Z_H = 0.125, 0.078                                         # metatarsal hinge line (forward of the ankle) and height
SOLE_T = 0.064                                                  # sole: 44 mm ceramic studs + aerogel, 20 mm carrier
REAR_O = [(-0.048, -0.112), (0.048, -0.112), (0.068, -0.098), (0.078, -0.05), (0.083, 0.118), (-0.082, 0.118), (-0.076, -0.05), (-0.066, -0.098)]
FORE_O = [(0.083, 0.132), (0.074, 0.18), (0.058, 0.215), (0.02, 0.240), (-0.035, 0.246), (-0.062, 0.236), (-0.082, 0.19), (-0.082, 0.132)]


def toe_rise(f): return max(0.0, f - 0.17) * math.tan(math.radians(10))         # the forefoot rocker (toe spring 10 deg)


def boot_O(s, tag=""):
    """rev. O boot for +500 C ground: two-part foot hinged at the ball line (small ring drives + flat elastic return strap),
    a 64 mm segmented sole (ceramic studs on the ground face, aerogel fill, carrier plates), a C-shaped titanium heel leaf
    spring with an elastomer pad, a two-axis ankle (pitch + inversion ring drives), a heavy faceted upper with an instep fan.
    Local coordinates: x outward (big toe at -x), f forward, z up.  Objects of the forefoot carry '_fore' in the name."""
    c = s * LEG_C; L = ("L" if s > 0 else "R") + tag
    def P(x, f, z): return V((c + s * x, -f, z))
    # sole: studs (one mesh per segment), aerogel fill, carrier
    for seg, poly, fr, nm in (("rear", REAR_O, [-0.099 + 0.0265 * i for i in range(8)], "sole_studs"), ("fore", FORE_O, [0.142 + 0.024 * i for i in range(5)], "sole_studs_fore")):
        vs = []; fs = []; hs = 0.0085
        for f in fr:
            for x in (-0.0675, -0.0405, -0.0135, 0.0135, 0.0405, 0.0675):
                if not all(_in_poly(poly, x + a * (hs + 0.002), f + b * (hs + 0.002)) for a in (-1, 1) for b in (-1, 1)): continue
                z0 = toe_rise(f) if seg == "fore" else 0.0; base = len(vs)
                for zz in (z0, 0.044):
                    for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)): vs.append(P(x + a * hs, f + b * hs, zz))
                fs += [(base, base + 1, base + 2, base + 3), (base + 4, base + 7, base + 6, base + 5)] + [(base + q, base + (q + 1) % 4, base + 4 + (q + 1) % 4, base + 4 + q) for q in range(4)]
        mk(vs, fs, "tile", nm + L, 0.01, "boots")
        ins = _inset(poly, 0.006)
        hull_obj([P(x, f, (toe_rise(f) if seg == "fore" else 0.0) + 0.008) for x, f in ins] + [P(x, f, 0.044) for x, f in ins], "aerogel", ("sole_fill_fore" if seg == "fore" else "sole_fill") + L)
        top = SOLE_T if seg == "rear" else SOLE_T - 0.002
        bevel(hull_obj([P(x, f, 0.044) for x, f in poly] + [P(x, f, top) for x, f in poly], "frame", ("carrier_fore" if seg == "fore" else "carrier") + L), 0.002, 1, 30)
    # heel: C-shaped titanium leaf spring (bulging back past the heel) and an elastomer pad inside it
    # two narrow leaves side by side (a leaf spring, not a roller): an open elliptical C, 5 mm thick, 24 mm wide each
    cf, cz, rr, t = -0.100, 0.087, 0.0215, 0.005; rf = 0.024
    path = [(-0.05, SOLE_T + t / 2)] + [(cf - rf * math.cos(math.radians(a)), cz + rr * math.sin(math.radians(a))) for a in range(-90, 91, 10)] + [(-0.07, cz + rr)]
    for side in (-1, 1):
        vs = []; fs = []; xc = side * 0.037
        for k, (f, z) in enumerate(path):
            f0, z0 = path[max(k - 1, 0)]; f1, z1 = path[min(k + 1, len(path) - 1)]; tg = V((f1 - f0, z1 - z0, 0)).normalized(); nn = (-tg.y, tg.x)
            for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)): vs.append(P(xc + a * 0.012, f + b * nn[0] * t / 2, z + b * nn[1] * t / 2))
        for k in range(len(path) - 1):
            for q in range(4): fs.append((4 * k + q, 4 * k + (q + 1) % 4, 4 * (k + 1) + (q + 1) % 4, 4 * (k + 1) + q))
        fs += [(0, 1, 2, 3), tuple(4 * (len(path) - 1) + q for q in (3, 2, 1, 0))]
        mk(vs, fs, "titan", "heel_spring%d" % (side + 1) + L, 0.6, "boots")
        for f, z in ((-0.056, SOLE_T + t), (-0.076, cz + rr + t / 2)): bolt(P(xc, f, z), (0, 0, 1 if z < 0.08 else -1))
    rbox(P(0, -0.074, cz), (0.035, 0.04, 2 * rr - t - 0.002), "rubber", "heel_pad" + L, r=0.006, seg=2, part="boots")
    # the upper: heavy faceted shell, raised over the heel spring; a separate faceted forefoot shell
    up = [P(x, f, 0.110 if f < -0.04 else SOLE_T) for x, f in REAR_O] + [P(0.080, -0.03, SOLE_T), P(-0.078, -0.03, SOLE_T)]
    up += [P(0.086, 0.06, 0.095), P(-0.085, 0.06, 0.095), P(0.084, -0.04, 0.11), P(-0.082, -0.04, 0.11)]
    up += [P(a * 0.05, -0.12, 0.13) for a in (-1, 1)] + [P(a * 0.045, -0.112, 0.172) for a in (-1, 1)]
    up += [P(a * 0.068, -0.07, 0.174) for a in (-1, 1)] + [P(a * 0.07, 0.02, 0.174) for a in (-1, 1)] + [P(a * 0.045, 0.05, 0.168) for a in (-1, 1)]
    up += [P(a * 0.072, 0.118, 0.102) for a in (-1, 1)] + [P(a * 0.05, 0.118, 0.11) for a in (-1, 1)]
    hull_obj(up, "shieldp", "boot_upper" + L)
    fo = [P(x, f, SOLE_T - 0.002) for x, f in FORE_O] + [P(0.07, 0.132, 0.104), P(-0.07, 0.132, 0.104), P(0.084, 0.14, 0.085), P(-0.083, 0.14, 0.085)]
    fo += [P(0.05, 0.2, 0.09), P(-0.06, 0.205, 0.092), P(-0.04, 0.238, 0.078), P(0.015, 0.234, 0.078)]
    hull_obj(fo, "shieldp", "boot_fore" + L)
    bar(P(-0.034, 0.2462, 0.068), P(0.02, 0.2402, 0.068), 0.006, 0.009, "red", "boot_toe_fore" + L, up=(0, 0, 1), part="boots")
    # instep fan: three shingled flat plates, the upper one over the lower
    def zl(f): return 0.168 + (f - 0.05) * (-0.058 / 0.068)
    for k, (fa, fb, off, w) in enumerate(((0.048, 0.078, 0.009, 0.05), (0.07, 0.10, 0.0065, 0.055), (0.092, 0.122, 0.004, 0.06))):
        pts = [P(-w, fa, zl(fa) + off), P(w, fa, zl(fa) + off), P(w, fb, zl(fb) + off), P(-w, fb, zl(fb) + off)]
        nrm = (pts[1] - pts[0]).cross(pts[2] - pts[0])
        facet(pts if nrm.z > 0 else pts[::-1], 0.004, "shieldp", "instep%d" % k + L, part="boots")
    # metatarsal hinge: pin, two small ring drives, a flat elastic return strap across the top
    cyl(P(-0.084, F_H, Z_H), P(0.084, F_H, Z_H), 0.012, "bear", "hinge_pin_fore" + L, seg=24, part="boots")
    for a in (-1, 1): ring_motor_ax(P(a * 0.090, F_H, Z_H), V((s * a, 0, 0)), 0.02, 0.012, "hinge_drive_fore%d" % (a + 1) + L)
    bar(P(0, 0.104, 0.115), P(0, 0.155, 0.1035), 0.045, 0.004, "rubber", "return_strap_fore" + L, up=(0, 0, 1), part="boots", r=0.0015)
    for f, z in ((0.108, 0.1175), (0.151, 0.1065)): bolt(P(0, f, z), (0, 0, 1))
    # ankle: the collar (bearing), inner malleolus, pitch ring drive outside, inversion ring drive at the back, stirrup
    ank = [(z, c, -0.006, s, w, w, f, b, 0.004, 0.002, 0, 0) for z, w, f, b in ((0.14, 0.075, 0.086, 0.064), (0.156, 0.071, 0.082, 0.06), (0.172, 0.073, 0.084, 0.062))]
    loft4(ank, "bear", "ankle" + L, 120, sharp=0.35, part="bearings")
    rbox((c - s * 0.078, -0.008, 0.162), (0.016, 0.03, 0.026), "shell", "malleolus_in" + L, r=0.007, seg=3, part="bearings")
    ring_motor_ax(P(0.11, 0.006, 0.156), V((s, 0, 0)), 0.034, 0.022, "motor_ankle" + L)
    ring_motor_ax(P(0.0, -0.090, 0.198), V((0, 1, 0)), 0.03, 0.02, "motor_ankle_inv" + L)
    bar(P(0, -0.092, 0.172), P(0, -0.106, 0.14), 0.03, 0.012, "frame", "inv_bracket" + L, up=(0, 1, 0), part="frame")
    channel(P(0.11, 0.006, 0.122), P(0.11, 0.0, 0.07), (s, 0, 0), w=0.03, d=0.012, name="stirrup" + L)
    bar(P(0.114, 0.0, 0.07), P(0.082, 0.0, 0.07), 0.03, 0.016, "frame", "stirrup_strap" + L, up=(0, 0, 1), part="frame")
    if not tag: stencil("ЛЕВ" if s > 0 else "ПРАВ", P(0.0855, 0.035, 0.092), (s, 0, 0), size=0.016, name="boot_mark")


DEMO = []
DEMO_OFF = V((3.0, 2.5, 0.0))


def demo_boot():
    """a separate left boot (with a shin stub) in toe-off: the rear foot turned 25 deg about the metatarsal hinge, the
    forefoot flat on the ground; placed far off the figure, rendered only in the boot close-ups"""
    nb = len(BOLTS); nrv = len(RIBS[0]) if RIBS else 0; nrf = len(RIBS[1]) if RIBS else 0
    before = set(COLL[0].objects)
    boot_O(1, tag="_demo")
    thigh, shin, knee = leg_rows(1); stub = [r for r in shin if r[0] <= 0.33]
    bevel(loft4(stub, "shell", "shin_demo", 120, sharp=0.35, part="shell"), 0.003, 2, 25)
    shield_bands(stub, 0.32, 0.18, 1, "shin_demo", gap=0.013, seams=2, thick=0.008)
    new = [o for o in COLL[0].objects if o not in before]
    piv = V((0, -F_H, Z_H)); R = Matrix.Translation(piv) @ Matrix.Rotation(math.radians(25), 4, 'X') @ Matrix.Translation(-piv)
    T = Matrix.Translation(DEMO_OFF); TR = T @ R
    for o in new:
        Mx = T if "_fore" in o.name else TR
        if o.type == 'MESH': o.data.transform(Mx)
        else: o.matrix_world = Mx @ o.matrix_world
        DEMO.append(o)
    nb_ = [(TR @ p, (TR.to_3x3() @ n).normalized()) for p, n in BOLTS[nb:]]; del BOLTS[nb:]
    keep = BOLTS[:]; BOLTS[:] = nb_
    b0 = set(COLL[0].objects); flush_bolts(); DEMO.extend(o for o in COLL[0].objects if o not in b0); BOLTS[:] = keep
    if RIBS and len(RIBS[0]) > nrv:
        vs = [TR @ v for v in RIBS[0][nrv:]]; fs = [tuple(i - nrv for i in f) for f in RIBS[1][nrf:]]
        del RIBS[0][nrv:]; del RIBS[1][nrf:]
        DEMO.append(mk(vs, fs, "fin", "motor_fins_demo", 0.5, "drives"))


# ------------------------------------------------------------------ rev. P: shield material variants (foil / ceramic / hybrid)
DIM_R = 0.0069                                   # dimple cell circumradius: 12 mm across the flats


def dimple_image(N=512):
    """tileable fine hex dimple map for embossed foil: soft rounded bumps, shallow valleys between cells"""
    import numpy as np
    R = DIM_R; sx = math.sqrt(3) * R; sy = 3 * R; k, m = 7, 4; W = k * sx; H = m * sy
    ys, xs = np.mgrid[0:N, 0:N].astype(np.float64)
    X = (xs + 0.5) / N * W; Y = (ys + 0.5) / N * H; best = None
    for ox, oy in ((0.0, 0.0), (sx / 2, sy / 2)):
        i = np.round((X - ox) / sx); j = np.round((Y - oy) / sy); dx = X - (ox + i * sx); dy = Y - (oy + j * sy); d2 = dx * dx + dy * dy
        if best is None: best = [d2, dx, dy]
        else:
            sel = d2 < best[0]
            for n_, v in enumerate((d2, dx, dy)): best[n_] = np.where(sel, v, best[n_])
    _, dx, dy = best
    norm = np.maximum(np.abs(dx), 0.5 * np.abs(dx) + 0.8660254 * np.abs(dy)) / (sx / 2)
    h = np.clip(1.0 - norm ** 2, 0.0, 1.0) ** 0.6
    px = np.zeros((N, N, 4), dtype=np.float32); px[..., 0] = h; px[..., 1] = h; px[..., 2] = h; px[..., 3] = 1.0
    img = bpy.data.images.new("PantsirDimple", N, N, alpha=False, float_buffer=True)
    img.colorspace_settings.name = 'Non-Color'; img.pixels.foreach_set(px.ravel()); img.update(); img.pack()
    return img, W


def _nodes(m):
    nt = m.node_tree; p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    for n in list(nt.nodes):
        if n.type not in ("OUTPUT_MATERIAL", "BSDF_PRINCIPLED"): nt.nodes.remove(n)
    return nt, next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")


def _noise(nt, tc, scale, detail=4.0, rough=0.5, dist=0.0):
    nz = nt.nodes.new("ShaderNodeTexNoise"); nz.inputs["Scale"].default_value = scale; nz.inputs["Detail"].default_value = detail
    nz.inputs["Roughness"].default_value = rough; nz.inputs["Distortion"].default_value = dist; nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
    return nz


def _range(nt, src, a, b, fa=0.0, fb=1.0):
    mr = nt.nodes.new("ShaderNodeMapRange"); mr.inputs["From Min"].default_value = fa; mr.inputs["From Max"].default_value = fb
    mr.inputs["To Min"].default_value = a; mr.inputs["To Max"].default_value = b; nt.links.new(src, mr.inputs["Value"]); return mr.outputs["Result"]


def foil_ceramic():
    """P1 foil: gold-coated metal foil embossed with fine hex dimples, laid over the panels with soft creases;
    P2 ceramic: rigid matte light-grey plates with an IR-reflective coating sheen, speckle and roughness scatter"""
    mat("foil", (0.95, 0.74, 0.36), metal=1.0, rough=0.3)
    nt, p = _nodes(M["foil"]); tc = nt.nodes.new("ShaderNodeTexCoord")
    log.append("fc1"); img, W = dimple_image(); log.append("fc2")
    mp = nt.nodes.new("ShaderNodeMapping"); mp.inputs["Scale"].default_value = (1 / W,) * 3; nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    it = nt.nodes.new("ShaderNodeTexImage"); it.image = img; it.projection = 'BOX'; it.projection_blend = 0.15; it.interpolation = 'Linear'
    nt.links.new(mp.outputs["Vector"], it.inputs["Vector"])
    b1 = nt.nodes.new("ShaderNodeBump"); b1.inputs["Strength"].default_value = 0.22; b1.inputs["Distance"].default_value = 0.0012
    nt.links.new(it.outputs["Color"], b1.inputs["Height"])
    log.append("fcA"); cr = _noise(nt, tc, 13.0, 7.0, 0.58, 0.45)                                  # crinkle / creases, ~8 cm
    log.append("fcA1"); b2 = nt.nodes.new("ShaderNodeBump"); b2.inputs["Strength"].default_value = 0.28; b2.inputs["Distance"].default_value = 0.006
    log.append("fcA2"); nt.links.new(cr.outputs["Fac"], b2.inputs["Height"]); log.append("fcA3"); nt.links.new(b1.outputs["Normal"], b2.inputs["Normal"]); log.append("fcA4"); nt.links.new(b2.outputs["Normal"], p.inputs["Normal"])
    log.append("fcB"); rn = _noise(nt, tc, 7.0, 3.0, 0.5)
    nt.links.new(_range(nt, rn.outputs["Fac"], 0.2, 0.38, 0.3, 0.7), p.inputs["Roughness"])
    log.append("fcC"); hs = nt.nodes.new("ShaderNodeHueSaturation"); hs.inputs["Color"].default_value = (0.95, 0.74, 0.36, 1)
    nt.links.new(_range(nt, cr.outputs["Fac"], 0.9, 1.06, 0.3, 0.7), hs.inputs["Value"]); nt.links.new(hs.outputs["Color"], p.inputs["Base Color"])
    log.append("fc3"); mat("ceram", (0.45, 0.445, 0.43), metal=0.15, rough=0.5)
    nt, p = _nodes(M["ceram"]); tc = nt.nodes.new("ShaderNodeTexCoord")
    sp = _noise(nt, tc, 450.0, 2.0, 0.5)
    nt.links.new(_range(nt, sp.outputs["Fac"], 0.42, 0.58, 0.3, 0.7), p.inputs["Roughness"])
    hs = nt.nodes.new("ShaderNodeHueSaturation"); hs.inputs["Color"].default_value = (0.45, 0.445, 0.43, 1)
    big = _noise(nt, tc, 5.0, 2.0, 0.5)
    mx = nt.nodes.new("ShaderNodeMath"); mx.operation = 'MULTIPLY'
    nt.links.new(_range(nt, sp.outputs["Fac"], 0.93, 1.05, 0.3, 0.7), mx.inputs[0]); nt.links.new(_range(nt, big.outputs["Fac"], 0.96, 1.04, 0.3, 0.7), mx.inputs[1])
    nt.links.new(mx.outputs[0], hs.inputs["Value"]); nt.links.new(hs.outputs["Color"], p.inputs["Base Color"])
    b = nt.nodes.new("ShaderNodeBump"); b.inputs["Strength"].default_value = 0.05; b.inputs["Distance"].default_value = 0.0005
    nt.links.new(sp.outputs["Fac"], b.inputs["Height"]); nt.links.new(b.outputs["Normal"], p.inputs["Normal"])


def q_materials():
    """rev. Q: borazon (polished amber cBN), photonic mirror under a borazon window (thin film on a metal reflector),
    light-metal plate frames, BN aerogel under the plates"""
    mat("borazon", (0.30, 0.068, 0.0), rough=0.16)                          # the fork's borazon (boron_mockups.py BM_borazon)
    p = next(n for n in M["borazon"].node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    for k, v in (("IOR", 2.12), ("Specular IOR Level", 0.8), ("Coat Weight", 0.6), ("Coat Roughness", 0.10), ("Coat IOR", 2.12),
                 ("Transmission Weight", 0.0), ("Subsurface Weight", 0.0)):
        if k in p.inputs: p.inputs[k].default_value = v
    if "Coat Tint" in p.inputs: p.inputs["Coat Tint"].default_value = (1.0, 0.62, 0.30, 1)   # the depth: amber under the polish
    # photonic mirror: in Cycles the thin film colours a dielectric, hardly a metal - so a dark high-index dielectric with
    # the multilayer film (the interference reflector), mixed with a little metal mirror (the ZrN back-up), under an amber
    # borazon window (coat)
    mat("mirror", (0.015, 0.011, 0.008), rough=0.04)                        # rev. R: darker, the interference colour carries it
    nt = M["mirror"].node_tree; p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED"); out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
    p.inputs["IOR"].default_value = 3.0; p.inputs["Thin Film IOR"].default_value = 1.45
    p.inputs["Coat Weight"].default_value = 0.5; p.inputs["Coat Roughness"].default_value = 0.03; p.inputs["Coat IOR"].default_value = 2.12
    p.inputs["Coat Tint"].default_value = (1.0, 0.78, 0.45, 1)
    tc = nt.nodes.new("ShaderNodeTexCoord"); nz = nt.nodes.new("ShaderNodeTexNoise"); nz.inputs["Scale"].default_value = 2.5; nz.inputs["Detail"].default_value = 1.0
    nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
    mr = nt.nodes.new("ShaderNodeMapRange"); mr.inputs["From Min"].default_value = 0.3; mr.inputs["From Max"].default_value = 0.7
    mr.inputs["To Min"].default_value = 560.0; mr.inputs["To Max"].default_value = 620.0
    nt.links.new(nz.outputs["Fac"], mr.inputs["Value"]); nt.links.new(mr.outputs["Result"], p.inputs["Thin Film Thickness"])
    mt = nt.nodes.new("ShaderNodeBsdfPrincipled"); mt.inputs["Base Color"].default_value = (0.95, 0.70, 0.36, 1); mt.inputs["Metallic"].default_value = 1.0; mt.inputs["Roughness"].default_value = 0.06
    mx = nt.nodes.new("ShaderNodeMixShader"); mx.inputs[0].default_value = 0.42                # rev. S: more amber metal, less interference
    nt.links.new(p.outputs[0], mx.inputs[1]); nt.links.new(mt.outputs[0], mx.inputs[2]); nt.links.new(mx.outputs[0], out.inputs["Surface"])
    mat("rim", (0.13, 0.135, 0.14), metal=0.6, rough=0.4)                    # dark graphite frames: light rims read as hoops
    mat("bnaero", (0.16, 0.165, 0.17), rough=0.95)                         # the layer edge dark: pale strips read as hoops


def manifold(rows, z, name, off=0.032, seg=72):
    """rev. R: a slim ring coolant manifold round a joint (graphite), two flush couplings, no hoses outside"""
    def P(zz, t, o):
        row = sample_rows(rows, zz); p = sec_pt(row, t); c = V((row[1], row[2], p.z)); d = p - c; d.z = 0; return p + d.normalized() * o
    ts = [2 * math.pi * j / seg for j in range(seg)]
    _band_mesh([[P(z + 0.009, t, off) for t in ts], [P(z - 0.009, t, off) for t in ts]], name, thick=0.009, m="frame", part="frame")
    for t in (-math.pi * 0.3, -math.pi * 0.7):
        a = P(z, t, off + 0.003); b = P(z, t, off + 0.013); cyl(a, b, 0.0065, "frame", name + "_cpl", seg=16, part="frame")
        cyl(b, P(z, t, off + 0.016), 0.0045, "bear", name + "_cpl_face", seg=16, part="frame")


SHIELD_SET = []
CER_WHOLE = ("boot_upper", "boot_fore", "instep", "Pantsir_hand_", "Pantsir_C2")   # P3: ceramic armour zones (whole objects)


def _cer_whole(nm): return any(k in nm for k in CER_WHOLE) or ("knee" in nm and "_band" in nm)


def _partial(nm):
    """P3: ceramic only on part of a band - the shin front, the forearm outer face"""
    if "_band" not in nm: return None
    if nm.startswith("Pantsir_shin"): return lambda d, s: d.y < -0.45
    if nm.startswith("Pantsir_forearm"): return lambda d, s: d.x * s > 0.45
    return None


def collect_shield():
    SHIELD_SET[:] = [o for o in COLL[0].objects if o.type == 'MESH' and len(o.data.materials) and o.data.materials[0] == M["shieldp"]]


def set_variant(k):
    if k == 4:                                                    # rev. Q: borazon impact plates, photonic mirror elsewhere
        foil, cer = M["mirror"], M["borazon"]
        for o in COLL[0].objects:
            if o.type == 'MESH' and len(o.data.materials) > 1 and o in SHIELD_SET: o.data.materials[1] = o.data.materials[0]
            pass   # the lips in the plate's own material: the step reads by its shading, no contrasting hoop
        k = 3
    else:
        foil, cer = M["foil"], M["ceram"]
    for o in SHIELD_SET:
        nm = o.name; me = o.data
        prim = foil if k == 1 else (cer if k == 2 else (cer if _cer_whole(nm) else foil))
        me.materials[0] = prim
        if len(me.materials) > 2:
            me.materials[2] = cer if k == 3 else prim
            test = _partial(nm) if k == 3 else None
            ctr = sum((v.co for v in me.vertices), V((0, 0, 0))) / max(len(me.vertices), 1); s = 1 if "L_band" in nm else -1
            for pg in me.polygons:
                if pg.material_index == 1: continue
                d = pg.center - ctr; d.z = 0; d = d.normalized() if d.length > 1e-6 else d
                pg.material_index = 2 if (test and test(d, s)) else 0


HAND_INFO = {}; HAND_OBJS = {}


HUT_F = [(1.02, 0.24), (1.20, 0.238), (1.30, 0.245), (1.36, 0.262), (1.42, 0.28), (1.47, 0.275), (1.545, 0.165)]
HUT_SF = [(1.02, 0.16), (1.22, 0.19), (1.40, 0.185), (1.545, 0.12)]
HUT_SB = [(1.02, 0.16), (1.15, 0.175), (1.40, 0.18), (1.545, 0.14)]


def hut_row(z):
    rx = interp(HUT_F, z) + 0.003; f, b = interp(HUT_SF, z) + 0.003, interp(HUT_SB, z) + 0.003                  # rev. R: 93 mm walls
    f -= 0.022 * max(0.0, min(1.0, (z - 1.02) / 0.03, (1.475 - z) / 0.03))                                    # rev. K: shield gap inside
    return (z, 0.0, (b - f) / 2, rx, rx, (f + b) / 2, (f + b) / 2, 3.2)


def torso():
    zs = [1.02, 1.06, 1.12, 1.20, 1.28, 1.36, 1.42, 1.47, 1.51, 1.545]
    hut = loft4([hut_row(z) for z in zs], "shell", "hut", 80, part="shell"); bevel(hut, 0.008, 3, 30)
    z = 1.37; r = hut_row(z)
    loft4([(z - 0.006,) + r[1:3] + (r[3] + 0.003, r[4] + 0.003, r[5] + 0.003, r[6] + 0.003, 3.2), (z + 0.006,) + r[1:3] + (r[3] + 0.003, r[4] + 0.003, r[5] + 0.003, r[6] + 0.003, 3.2)],
          "red", "fieldband", 80, part="shell")                                                                       # the field ring: a red band
    loft4([(1.545, 0, 0.01, 0.17, 0.17, 0.13, 0.13, 2.4), (1.56, 0, 0.01, 0.168, 0.168, 0.13, 0.13, 2.4), (1.575, 0, 0.01, 0.165, 0.165, 0.128, 0.128, 2.4)], "bear", "collar", 72, part="bearings")
    return hut


def arms():
    out = {}
    for s in (1, -1):
        L = "L" if s > 0 else "R"; a = -s * math.radians(8); cx, cy = s * ARM_CX, 1.40
        def R(x, y): return (cx + (x - cx) * math.cos(a) + (y - cy) * math.sin(a), cy - (x - cx) * math.sin(a) + (y - cy) * math.cos(a))
        side_c = [(0.67, -0.075, 0.055), (0.81, -0.055, 0.065), (0.82, -0.055, 0.065), (1.119, -0.01, 0.07), (1.12, -0.005, 0.065), (1.42, -0.005, 0.075)]
        def sc(y): return interp([(p[0], p[1]) for p in side_c], y), interp([(p[0], p[2]) for p in side_c], y)
        def row(y, out_, in_, fr, bk, n=2.8, ty=0.0):
            x, z = R(cx, y); c_, _ = sc(y)
            return (z, x, c_, out_ if s > 0 else in_, in_ if s > 0 else out_, fr, bk, n, ty)
        def prow(y, wo, wi, f, bo, bi, g, ch, d=0.034):                                                     # rev. Q: 15 mm slimmer                                                     # rev. K: room for the shield
            x, z = R(cx, y); c_, _ = sc(y); return (z, x, c_, s, wo - d, wi - d, f - d, bo - d, bi - d, g, ch)
        loft4([row(1.43, 0.116, 0.116, 0.108, 0.108), row(1.39, 0.116, 0.116, 0.108, 0.108)], "bear", "shoulder" + L, part="bearings")
        # rev. R: body + 2 x 79 mm outside the shield (shell = outer - 2 x 19 mm band offset), elbow 15 mm clearance
        upper = [prow(1.39, 0.112, 0.100, 0.106, 0.106, 0.106, 0.0, 0.026, d=0.032), prow(1.12, 0.104, 0.098, 0.111, 0.111, 0.111, 0.0, 0.024, d=0.032)]   # ~62 mm arm walls
        loft4(upper, "shell", "upperarm" + L, 96, sharp=0.5, part="shell")
        dl = (-math.pi * 0.35, math.pi * 0.35) if s > 0 else (math.pi * 0.65, math.pi * 1.35)
        plate(upper, 1.20, 1.385, dl[0], dl[1], "shell", "deltoid" + L, off=0.005)
        loft4([row(1.12, 0.112, 0.112, 0.118, 0.118, 2.4, 0.2), row(1.07, 0.11, 0.11, 0.117, 0.117, 2.4, 0.2)], "bear", "elbow" + L, part="bearings")
        fore = [prow(1.07, 0.105, 0.100, 0.110, 0.110, 0.110, -0.006, 0.026, d=0.032), prow(0.82, 0.082, 0.078, 0.085, 0.085, 0.085, -0.003, 0.02, d=0.032)]
        fa = loft4(fore, "shell", "forearm" + L, 96, sharp=0.5, part="shell")
        x, z = R(cx, 1.095); ob = V((x, sc(1.095)[0] + 0.105, z - 0.004))                                          # pointed olecranon plate
        cyl(ob - V((0, 0.012, -0.012)), ob + V((0, 0.03, -0.022)), 0.034, "shell", "olecranon" + L, r2=0.004, seg=4, part="shell")
        for k in range(4):                                                                                         # inner-elbow fold
            x, z = R(cx - s * 0.02, 1.075 + k * 0.011); yf = sc(1.09)[0] - 0.10 - (0.003 if k % 2 else 0.0)
            bar(V((x - 0.035, yf, z)), V((x + 0.035, yf, z)), 0.008, 0.006, "rubber", "elbow_fold" + L, up=(0, -1, 0), part="shell")
        before = set(COLL[0].objects)
        HAND_INFO[s] = hand_B3(s, R, sc, L)                                                                        # rev. H: hand B «Трёхпалая»
        HAND_OBJS[s] = [o for o in COLL[0].objects if o not in before]
        O, D, Bv, Uv = HAND_INFO[s]
        kU, kB = (HAND_W + 2 * HAND_WALL) / 0.126, (HAND_T + 2 * HAND_WALL) / 0.074; cU, cB = 0.145 / 0.10, 0.14 / 0.082;   # the cuff: the slimmer forearm's wrist (~62 mm arm walls)
        kF = 0.5 * (kU + kB)
        for o in HAND_OBJS[s]:                                                                                    # rev. R
            if o.type != 'MESH': continue
            for v in o.data.vertices:
                d = v.co - O; a, b, u = d.dot(D), d.dot(Bv), d.dot(Uv); t = max(0.0, min(1.0, (a + 0.005) / 0.055)); t = t * t * (3 - 2 * t)
                sU, sB = cU + (kU - cU) * t, cB + (kB - cB) * t; a2 = a if a < 0.165 else 0.165 + (a - 0.165) * kF
                v.co = O + D * a2 + Bv * (b * sB) + Uv * (u * sU)
        SEAM_OBJS.extend(o for o in HAND_OBJS[s] if not o.name.startswith("Pantsir_cuff") and o not in GHOST.get(s, []))
        # red band under the shoulder bearing
        loft4([prow(1.385, 0.118, 0.106, 0.112, 0.112, 0.112, 0.0, 0.026, 0.032), prow(1.365, 0.118, 0.106, 0.112, 0.112, 0.112, 0.0, 0.026, 0.032)], "red", "shoulder_band" + L, 96, sharp=0.5, part="shell")
        out[s] = (fa, R, sc, fore, upper)
    return out


def door():
    BACK = [(1.05, 0.235), (1.58, 0.25), (1.62, 0.215), (1.66, 0.18)]
    SF = [(1.04, 0.175), (1.545, 0.14), (1.60, 0.235), (1.65, 0.33)]
    SB = [(1.04, 0.40), (1.07, 0.40), (1.60, 0.42), (1.65, 0.33)]
    zs = [1.05, 1.07, 1.15, 1.30, 1.45, 1.545, 1.58, 1.61, 1.635, 1.65]
    rows = []
    for z in zs:
        f, b = interp(SF, z), interp(SB, z); hw = interp(BACK, z); hd = max((b - f) / 2, 0.004)
        rows.append((z, 0.0, (f + b) / 2, hw, hw, hd, hd, 6.0))
    d = loft4(rows, "door", "door", 72, part="door"); bevel(d, 0.008, 3, 35)
    yb = lambda z: interp(SB, z)
    rbox((0, yb(1.325) + 0.003, 1.325), (0.40, 0.008, 0.45), "frame", "radfield", r=0.002, part="door")
    for k in range(17):
        x = -0.19 + k * 0.0238
        bar((x, yb(1.11) + 0.022, 1.11), (x, yb(1.54) + 0.022, 1.54), 0.0035, 0.042, "fin", "fin%02d" % k, up=(0, 1, 0), part="radiator")
    for z in (1.105, 1.545): rbox((0, yb(z) + 0.03, z), (0.41, 0.05, 0.008), "frame", "fin_rail", r=0.001, part="radiator")
    stencil("РАДИАТОР", (0, yb(1.575) + 0.006, 1.575), (0, 1, 0.25), size=0.026, name="door_mark")
    stencil("ПАНЦИРЬ-2   ЗАВ. № 2-0417", (0, yb(1.075) + 0.006, 1.072), (0, 1, 0), size=0.012, name="door_serial")
    for k in range(5):
        x = -0.105 + k * 0.05; y = (interp(SF, 1.625) + interp(SB, 1.625)) / 2
        rbox((x, y, 1.648), (0.03, 0.05, 0.012), "dark", "evap%d" % k, Quaternion((1, 0, 0), math.radians(30)), r=0.002, part="door")
    stencil("ИСПАРИТЕЛЬ", (0, 0.31, 1.668), (0, 0.6, 1), up=(0, -1, 0.6), size=0.014, name="evap_mark")
    for sx in (-1, 1):
        for z in (1.12, 1.35, 1.55):
            x = sx * (interp(BACK, z) + 0.004); y = interp(SF, z) + 0.05
            rbox((x, y, z), (0.016, 0.03, 0.04), "red", "latch", r=0.003, part="door")
    # grab handle (rubber grip on graphite posts) at the door's top, grip, battery slot, the seal (black rubber)
    for k in (-0.08, 0.08): bar((k, 0.33, 1.62), (k, 0.375, 1.69), 0.012, 0.012, "frame", "handle_post", up=(1, 0, 0), part="door")
    cyl((-0.09, 0.375, 1.69), (0.09, 0.375, 1.69), 0.011, "rubber", "handle_grip", part="door")
    rbox((0, yb(1.085) + 0.01, 1.092), (0.12, 0.02, 0.02), "rubber", "grip", r=0.004, part="door")
    rbox((0.15, yb(1.076) + 0.004, 1.09), (0.10, 0.01, 0.028), "dark", "battery_slot", r=0.002, part="door")
    stencil("АКБ", (0.15, yb(1.09) + 0.012, 1.115), (0, 1, 0), size=0.012, name="akb_mark")
    for sx in (-1, 1):
        pts = [V((sx * (interp(BACK, z) - 0.002), interp(SF, z) - 0.002, z)) for z in (1.05, 1.2, 1.35, 1.5, 1.545)]
        for p, q in zip(pts, pts[1:]): bar(p, q, 0.006, 0.008, "rubber", "door_seal", up=(sx, 0, 0), part="door")


# ------------------------------------------------------------------ techno-frame (flat channel members, drives in the nodes)
def frame(arm_data):
    # pelvis ring: a flat band round the brief with fasteners
    loft4([(0.865, 0, 0.0, 0.293, 0.293, 0.183, 0.183, 2.8), (0.895, 0, 0.0, 0.294, 0.294, 0.184, 0.184, 2.8)], "frame", "pelvis_ring", 80, part="frame")
    for k in range(16):
        a = 2 * math.pi * k / 16; p = sec_pt((0.88, 0, 0.0, 0.269, 0.269, 0.159, 0.159, 2.8), a); bolt(p, V((p.x, p.y, 0)))
    for k in range(10):                                                                                     # ring brackets to the brief
        a = 2 * math.pi * k / 10 + 0.2; p0 = sec_pt((0.88, 0, 0.0, 0.253, 0.253, 0.149, 0.149, 2.8), a); p1 = sec_pt((0.88, 0, 0.0, 0.293, 0.293, 0.183, 0.183, 2.8), a)
        bar(p0, p1, 0.02, 0.012, "frame", "ring_bracket", up=(0, 0, 1), part="frame")
    # spine beam (I-section) on the back: pelvis ring to the collar, carries the rear door (hinge brackets)
    for z0, z1, y0, y1 in ((0.89, 1.05, 0.165, 0.175),):
        pass
    a, b = V((0, 0.17, 0.885)), V((0, 0.15, 1.545))
    bar(a + V((0, 0.012, 0)), b + V((0, 0.012, 0)), 0.05, 0.006, "frame", "spine_flange_out", up=(0, 1, 0), part="frame")
    bar(a, b, 0.006, 0.024, "frame", "spine_web", up=(0, 1, 0), part="frame")
    for z in (1.0, 1.2, 1.4): bolt(V((0.0, interp([(0.885, 0.188), (1.545, 0.168)], z), z)), (0, 1, 0))
    for z in (1.08, 1.5):                                                                                   # door hinge brackets on the beam
        y = interp([(0.885, 0.19), (1.545, 0.17)], z); rbox((0.0, y + 0.012, z), (0.07, 0.03, 0.035), "frame", "door_hinge", r=0.005, part="frame")
        cyl((-0.04, y + 0.03, z), (0.04, y + 0.03, z), 0.009, "hub", "door_pin", part="frame")
    for s in (1, -1):
        L = "L" if s > 0 else "R"
        hip, knee, ank = V((s * 0.32, 0.0, 0.875)), V((s * 0.31, -0.035, 0.485)), V((s * (LEG_C + 0.11), -0.006, 0.156))   # rev. O
        ring_motor(hip, s, 0.064, 0.034, "motor_hip" + L)
        rbox((s * 0.297, 0.0, 0.88), (0.03, 0.05, 0.034), "frame", "hip_lug" + L, r=0.004, part="frame")
        ring_motor(knee, s, 0.064, 0.034, "motor_knee" + L)
        motor_cap(hip, s, 0.064, "cap_hip" + L); motor_cap(knee, s, 0.064, "cap_knee" + L)
        channel(hip + V((0, 0, -0.06)), knee + V((0, 0, 0.055)), (s, 0, 0), name="leg_upper" + L)
        channel(knee + V((0, 0, -0.06)), ank + V((0, 0, 0.036)), (s, 0, 0), w=0.034, name="leg_lower" + L)
        stencil("НЕ НАСТУПАТЬ", knee + V((s * 0.0225, 0, -0.09)), (s, 0, 0), size=0.0085, m="stencil_red", name="leg_warn")
        # arm: shoulder node, member down the outer arm to the elbow drive; shoulder yoke to the spine
        fa, R, sc, fore = arm_data[s][:4]; cx = s * ARM_CX
        x, z = R(cx, 1.40); sh = V((x + s * 0.145, -0.005, z))
        ring_motor(sh, s, 0.075, 0.034, "motor_shoulder" + L)
        x, z = R(cx + s * 0.13, 1.095); el = V((x, -0.02, z))
        ring_motor(el, s, 0.038, 0.026, "motor_elbow" + L, fins=40)
        channel(sh + V((0, 0, -0.07)), el + V((0, 0, 0.04)), (s, 0, 0), w=0.032, name="arm_member" + L)
        motor_cap(sh, s, 0.075, "cap_shoulder" + L)
        x, z = R(cx + s * 0.115, 0.86); channel(el + V((0, 0, -0.04)), V((x, -0.045, z)), (s, 0, 0), w=0.026, d=0.012, name="forearm_member" + L)
        channel(V((s * 0.04, 0.16, 1.53)), sh + V((-s * 0.02, 0.03, 0.065)), (0, 0.3, 1), w=0.04, name="shoulder_yoke" + L)


# ------------------------------------------------------------------ rev. S: the body revision (sketch/build_sketch_body_rev.py)
# HEAVY_SUIT.md «Ревизия S» (30th-century materials): walls ~43 mm limbs / ~57 mm torso per side. Limbs as faceted plate
# prisms (10-gon sections: flat side plates, a front crease, chamfers, different front / back depth): thigh 0.27 -> 0.20 at
# the knee, the calf widest in its upper third (0.21), three narrowings (above the knee, the knee bearing 0.18, the ankle
# 0.165); nested knee shells (3 front with the borazon guard, 2 back), the Achilles recess. Torso a wedge (chest 0.57 ->
# waist 0.464), a separate pelvis with a high leg cut, the dark waist ring Р0. Plates: borazon / photonic mirror in recessed
# light-metal frames with 2 mm split lines, 3-4 mm chamfers. 40 x 12 exoskeleton profiles, ring motors at the nodes. Arms
# hang (no abduction, elbow 8 deg). The back unit «блок на спине скафандра»: one-piece chamfered frame, the mechanised
# heat-pump radiator (22 fins + 2 wings) with three states, battery cassettes Б1/Б2, five capped ports, a recessed handle,
# the red emergency pull. Stencils via stencil(); worn matte enamel.
BODY_S = True
ARM_X = 0.410                                                       # upper-arm axis (sheet: 0.300 .. 0.494)
ELB_Z, ELB_DEG = 1.162, 8.0
HF_Z = [0.802, 0.70]                                                # hand_frame sample heights (rev. S: the wrist at 0.879)
RAD = {"tilt": [], "wingL": [], "wingR": [], "louvres": [], "seal": [], "flaps_open": [], "flaps_closed": [], "caps": []}
RAD_PIVOT = V((0.0, 0.400, 1.330)); WING_AX = {}
# the three radiator states for a later export / animation: wing angle (deg), louvre position (0 open .. 1 closed),
# seal cover (0/1), face tilt (deg up-back)
RAD_STATES = {"hot": {"wing": 68.0, "louvre": 0.0, "seal": 0.0, "tilt": 5.0},       # «Отвод жара»
              "cryo": {"wing": 0.0, "louvre": 1.0, "seal": 0.0, "tilt": 0.0},      # «Криогеника»
              "liquid": {"wing": 0.0, "louvre": 1.0, "seal": 1.0, "tilt": 0.0}}    # «Жидкость»


def s_mats():
    mat("lmetal", (0.42, 0.43, 0.44), metal=0.85, rough=0.36)                      # light-metal plate frames
    mat("sealor", (0.85, 0.30, 0.04), rough=0.55)                                   # orange seal line
    mat("stencil_white", (0.80, 0.80, 0.78), rough=0.6)
    if "louvre" not in M: mat("louvre", (0.048, 0.050, 0.056), metal=0.9, rough=0.36, aniso=0.7)
    for nm in ("shell", "door"): wear_mat(M[nm])


def wear_mat(m):
    """worn matte enamel: chips to bare metal on the convex edges (AO inside), faint mottling"""
    nt = m.node_tree; p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    base = tuple(p.inputs["Base Color"].default_value)
    ao = nt.nodes.new("ShaderNodeAmbientOcclusion"); ao.inside = True; ao.inputs["Distance"].default_value = 0.005; ao.samples = 8
    inv = nt.nodes.new("ShaderNodeMath"); inv.operation = 'SUBTRACT'; inv.inputs[0].default_value = 1.0; nt.links.new(ao.outputs["AO"], inv.inputs[1])
    nz = nt.nodes.new("ShaderNodeTexNoise"); nz.inputs["Scale"].default_value = 160.0; nz.inputs["Detail"].default_value = 6.0
    mul = nt.nodes.new("ShaderNodeMath"); mul.operation = 'MULTIPLY'; nt.links.new(inv.outputs[0], mul.inputs[0]); nt.links.new(nz.outputs["Fac"], mul.inputs[1])
    mr = nt.nodes.new("ShaderNodeMapRange"); mr.inputs["From Min"].default_value = 0.22; mr.inputs["From Max"].default_value = 0.34
    nt.links.new(mul.outputs[0], mr.inputs["Value"])
    nz2 = nt.nodes.new("ShaderNodeTexNoise"); nz2.inputs["Scale"].default_value = 9.0; nz2.inputs["Detail"].default_value = 3.0
    mo = nt.nodes.new("ShaderNodeMapRange"); mo.inputs["To Min"].default_value = 0.86; mo.inputs["To Max"].default_value = 1.06; nt.links.new(nz2.outputs["Fac"], mo.inputs["Value"])
    tint = nt.nodes.new("ShaderNodeMix"); tint.data_type = 'RGBA'; tint.blend_type = 'MULTIPLY'; tint.inputs[0].default_value = 1.0
    tint.inputs[6].default_value = base; nt.links.new(mo.outputs[0], tint.inputs[7])
    mx = nt.nodes.new("ShaderNodeMix"); mx.data_type = 'RGBA'; nt.links.new(mr.outputs[0], mx.inputs[0])
    nt.links.new(tint.outputs[2], mx.inputs[6]); mx.inputs[7].default_value = (0.42, 0.42, 0.43, 1)
    nt.links.new(mx.outputs[2], p.inputs["Base Color"])
    rr = nt.nodes.new("ShaderNodeMapRange"); rr.inputs["To Min"].default_value = p.inputs["Roughness"].default_value; rr.inputs["To Max"].default_value = 0.3
    nt.links.new(mr.outputs[0], rr.inputs["Value"]); nt.links.new(rr.outputs[0], p.inputs["Roughness"])
    mm = nt.nodes.new("ShaderNodeMapRange"); mm.inputs["To Max"].default_value = 0.9; nt.links.new(mr.outputs[0], mm.inputs["Value"]); nt.links.new(mm.outputs[0], p.inputs["Metallic"])


def sec10(wo, wi, F, B, cf, cb, kf=0.0, kb=0.0):
    """a faceted limb / torso section (x outward, f forward): front crease kf, chamfers, flat outer / inner side plates"""
    return [(0.0, F + kf), (wo - cf, F), (wo, F - cf), (wo, -B + cb), (wo - cb, -B), (0.0, -B - kb),
            (-(wi - cb), -B), (-wi, -B + cb), (-wi, F - cf), (-(wi - cf), F)]


SEGREG = {}


def sec_clear(name, v):
    """signed 2D distance (m) of point v to the section of segment name at v's height: + inside, - outside; None off range"""
    rows, s, Ti = SEGREG[name]
    if Ti is not None: v = Ti(v)
    if not (rows[0][0] + 0.008 <= v.z <= rows[-1][0] - 0.008): return None
    if math.hypot(v.x - rows[0][1], v.y - rows[0][2]) > max(rows[0][3:7]) + 0.06: return None
    for a, b in zip(rows, rows[1:]):
        if a[0] <= v.z <= b[0]: break
    t = (v.z - a[0]) / max(b[0] - a[0], 1e-9)
    pa = [(a[1] + s * xo, a[2] - f) for xo, f in sec10(*a[3:])]; pb = [(b[1] + s * xo, b[2] - f) for xo, f in sec10(*b[3:])]
    P = [(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t) for (x0, y0), (x1, y1) in zip(pa, pb)]
    x, y = v.x, v.y; ins = False; dmin = 1e9
    for (x0, y0), (x1, y1) in zip(P, P[1:] + P[:1]):
        if (y0 > y) != (y1 > y) and x < x0 + (x1 - x0) * (y - y0) / (y1 - y0): ins = not ins
        dx, dy = x1 - x0, y1 - y0; L2 = dx * dx + dy * dy; u = max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / L2))
        dmin = min(dmin, math.hypot(x - x0 - u * dx, y - y0 - u * dy))
    return dmin if ins else -dmin


def seg_loft(rows, s, m, name, part="shell", T=None, bev=0.0035, caps=(True, True), Ti=None):
    """rows: (z, cx, cy, wo, wi, F, B, cf, cb, kf, kb); flat-faceted loft; returns (object, rings of 3D points)"""
    SEGREG[name] = (sorted(rows, key=lambda r: r[0]), s, Ti)
    vs = []; rings = []
    for r in rows:
        z, cx, cy = r[:3]; ring = [V((cx + s * xo, cy - f, z)) for xo, f in sec10(*r[3:])]
        if T: ring = [T(p) for p in ring]
        rings.append(ring); vs += ring
    n = 10; fs = []
    for k in range(len(rows) - 1):
        a, b = k * n, (k + 1) * n
        for j in range(n): j1 = (j + 1) % n; fs.append((a + j, a + j1, b + j1, b + j))
    if caps[0]: fs.append(tuple(reversed(range(n))))
    if caps[1]: fs.append(tuple(range((len(rows) - 1) * n, len(rows) * n)))
    o = mk(vs, fs, m, name, 0.05, part)
    for pg in o.data.polygons: pg.use_smooth = False
    if bev: bevel(o, bev, 2, 25)
    return o, rings


def face_plate(rings, k, j, m, name, mg=0.012, lift=0.004, frame=True, part="plates", vlim=(0.0, 1.0), k2=None):
    """a plate on face j of band k: inset by mg, lifted, in a recessed light-metal frame (2 mm split line = the plate's
    dark edge wall), 3 mm chamfer"""
    r0, r1 = rings[k], rings[k + 1 if k2 is None else k2]; n = len(r0); j1 = (j + 1) % n
    P0, P1, P2, P3 = r0[j], r0[j1], r1[j1], r1[j]
    nrm = (P1 - P0).cross(P3 - P0).normalized(); cen = sum(r0, V((0, 0, 0))) / n; mid = (P0 + P1 + P2 + P3) / 4
    if nrm.dot(mid - cen) < 0: nrm = -nrm
    W_ = ((P1 - P0).length + (P2 - P3).length) / 2; H_ = ((P3 - P0).length + (P2 - P1).length) / 2
    def poly_(m_, h_):
        u0, u1 = m_ / W_, 1 - m_ / W_; v0 = vlim[0] + m_ / H_; v1 = vlim[1] - m_ / H_
        at = lambda u, v: P0.lerp(P1, u).lerp(P3.lerp(P2, u), v) + nrm * h_
        pts = [at(u0, v0), at(u1, v0), at(u1, v1), at(u0, v1)]
        if (pts[1] - pts[0]).cross(pts[3] - pts[0]).dot(nrm) < 0: pts = pts[::-1]
        return pts
    if W_ < 2.6 * mg or H_ < 2.6 * mg: return None
    if frame:
        o, _ = facet(poly_(mg - 0.0055, 0.0016), 0.003, "lmetal", name + "_frame", part="plates"); bevel(o, 0.0012, 1, 30)
    o, _ = facet(poly_(mg, lift), 0.0045, m, name, part=part); bevel(o, 0.0032, 2, 30)
    return o


def bellows(s, name, cx, cy, base, zs, gro, tilt=0.0, m="bear", part="bearings"):
    """an armoured «гармошка»: nested faceted graphite rings, alternately folded out (gro[0]) and in (gro[1]), each
    chamfered and overlapping the next (no see-through at any angle); folds wider at the back / outside (weights)"""
    wo, wi, F, B, cf, cb = base; objs = []
    T = (lambda p: V((p.x, p.y, p.z + (p.x - cx) * s * tilt))) if tilt else None
    for k, (z0, z1) in enumerate(zip(zs, zs[1:])):
        g = gro[k % 2]; zt, zb = z0 + 0.003, z1 - 0.003
        r = lambda z, gg: (z, cx, cy, wo + gg * 1.0, wi + gg * 0.6, F + gg * 0.45, B + gg * 1.0, cf + gg * 0.5, cb + gg * 0.6, 0.0, 0.0)
        o, _ = seg_loft([r(zb, g * 0.75), r((zt + zb) / 2, g), r(zt, g * 0.75)], s, m, "%s_fold%d" % (name, k), part=part, T=T, bev=0.0018)
        objs.append(o)
    return objs


def leg_S(s):
    L = "L" if s > 0 else "R"; c = s * LEG_C
    R_ = lambda z, cy, wo, wi, F, B, cf, cb, kf=0.006, kb=0.004, cx_=None: (z, s * cx_ if cx_ is not None else c, cy, wo, wi, F, B, cf, cb, kf, kb)
    # rev. S fit: measured on the posed figure (fit_table) - the inner thigh wall thins to ~28 mm where the thighs meet
    thigh = [R_(0.905, 0.030, 0.125, 0.116, 0.130, 0.168, 0.042, 0.036, cx_=0.120), R_(0.760, 0.000, 0.128, 0.124, 0.134, 0.128, 0.044, 0.048, 0.006, 0.006, cx_=0.118),
             R_(0.600, -0.010, 0.120, 0.118, 0.118, 0.114, 0.040, 0.042, cx_=0.128)]
    shin = [R_(0.490, -0.008, 0.110, 0.108, 0.110, 0.120, 0.038, 0.040, cx_=0.132), R_(0.400, -0.004, 0.124, 0.118, 0.104, 0.134, 0.040, 0.056, 0.006, 0.010, cx_=0.136),
            R_(0.300, 0.002, 0.108, 0.102, 0.096, 0.110, 0.034, 0.044, 0.006, 0.006, cx_=0.139), R_(0.205, 0.004, 0.099, 0.097, 0.095, 0.095, 0.030, 0.034, 0.005, 0.0, cx_=0.142)]
    knee = [R_(0.585, -0.008, 0.116, 0.116, 0.116, 0.116, 0.038, 0.040, 0.0, 0.0, cx_=0.130), R_(0.497, -0.008, 0.116, 0.116, 0.116, 0.116, 0.038, 0.040, 0.0, 0.0, cx_=0.130)]
    c = s * 0.130
    to, tr = seg_loft(thigh[::-1], s, "shell", "thigh" + L)
    bellows(s, "hip_bellows" + L, s * 0.120, 0.020, (0.122, 0.112, 0.126, 0.146, 0.040, 0.050), [0.995, 0.975, 0.956, 0.937, 0.918, 0.899], (0.012, 0.003), tilt=0.30)
    bellows(s, "ankle_bellows" + L, s * 0.140, 0.004, (0.092, 0.090, 0.088, 0.088, 0.030, 0.034), [0.222, 0.205, 0.188, 0.170], (0.008, 0.002))
    so, sr = seg_loft(shin[::-1], s, "shell", "shin" + L)
    ko, kr = seg_loft(knee[::-1], s, "bear", "knee" + L, part="bearings", bev=0.002)
    # plates: thigh mirror (front halves, outer side, back halves); shin borazon front, mirror outer / calf
    for k in (0, 1):
        for j in (0, 9, 2, 4, 5): face_plate(tr, k, j, "mirror", "thigh_pl%d%d" % (k, j) + L, mg=0.016)
    for j in (0, 9): face_plate(sr, 1, j, "borazon", "shin_pl%d" % j + L, mg=0.014, k2=3)
    for k in (1, 2):
        for j in (2, 4, 5): face_plate(sr, k, j, "mirror", "calf_pl%d%d" % (k, j) + L, mg=0.013)
    # Р9: the oblique ring pair on the thigh
    seg_loft([R_(0.840, 0.012, 0.123, 0.118, 0.134, 0.142, 0.042, 0.050, 0.0, 0.0, cx_=0.119), R_(0.826, 0.011, 0.122, 0.117, 0.134, 0.141, 0.042, 0.050, 0.0, 0.0, cx_=0.119)],
             s, "bear", "ring_p9" + L, part="bearings", T=lambda p: V((p.x, p.y, p.z + (p.x - c) * s * 0.21)), bev=0.0015)
    # knee: nested shells (3 front, the top one the borazon guard), 2 back shells
    yk = -0.008 - 0.116
    for k, (z0, z1, w, f) in enumerate(((0.478, 0.514, 0.090, 0.008), (0.506, 0.540, 0.086, 0.016))):
        pts = [V((c - w, yk - f, z0)), V((c + w, yk - f, z0)), V((c + w * 0.97, yk - f - 0.004, z1)), V((c - w * 0.97, yk - f - 0.004, z1))]
        o, _ = facet(pts if s > 0 else pts, 0.006, "shell", "knee_shell%d" % k + L); bevel(o, 0.003, 2, 30)
    g = [(-0.068, 0.532), (0.068, 0.532), (0.082, 0.565), (0.058, 0.610), (-0.058, 0.610), (-0.082, 0.565)]
    pts = [V((c + x, yk - 0.026 - (z - 0.532) * 0.12, z)) for x, z in g]
    o, _ = facet(pts, 0.007, "borazon", "knee_guard" + L, part="plates"); bevel(o, 0.0035, 2, 30)
    o, _ = facet([V((c + x * 1.12, yk - 0.024 - (z - 0.532) * 0.12, z + (0.004 if z > 0.55 else -0.004))) for x, z in g], 0.003, "lmetal", "knee_guard_frame" + L, part="plates")
    yb = -0.008 + 0.116
    for k in range(5):                                                       # popliteal armoured bellows: chamfered graphite folds
        z = 0.500 + k * 0.0175; d = 0.010 if k % 2 == 0 else 0.004
        rbox(V((c, yb + d, z)), (0.176 - 0.006 * (k % 2), 0.016, 0.021), "bear", "knee_bellows%d" % k + L, r=0.0045, seg=1, part="bearings")
    # Achilles recess: a dark ribbed strip in the back keel above the boot
    ya = 0.004 + 0.086; c = s * 0.140
    rbox(V((c, ya, 0.248)), (0.040, 0.010, 0.080), "rubber", "achilles" + L, r=0.003, seg=1, part="shell")
    for k in range(6): rbox(V((c, ya + 0.005, 0.215 + k * 0.013)), (0.036, 0.003, 0.004), "fin", "achilles_rib" + L, r=0.0008, seg=1, part="shell")
    # exoskeleton: 40 x 12 flat profiles hip - knee - ankle on the outer side, ring motors at the nodes (Р8, Р10)
    hip, kn = V((s * 0.292, 0.020, 0.880)), V((s * 0.249 + s * 0.0, -0.006, 0.540)); kn = V((s * 0.262, -0.006, 0.540))
    ring_motor(hip, s, 0.058, 0.030, "motor_hip" + L); ring_motor(kn, s, 0.046, 0.026, "motor_knee" + L)
    for a, b in ((hip + V((-s * 0.004, 0, -0.05)), kn + V((0, 0, 0.042))), (kn + V((0, 0, -0.042)), V((s * 0.260, -0.004, 0.40))), (V((s * 0.260, -0.004, 0.40)), V((s * 0.246, -0.006, 0.19)))):
        bar(a, b, 0.040, 0.012, "frame", "leg_member" + L, up=(s, 0, 0), part="frame", r=0.003)
    for p, t in ((hip, "Р8"), (kn, "Р10"), (V((s * 0.245, 0.006, 0.156)), "Р11")):
        stencil(t, p + V((s * 0.02, 0, 0.075 if t != "Р11" else 0.05)), (s, 0, 0), size=0.013, m="stencil_white", name="mk_" + t)
    stencil("НЕ НАСТУПАТЬ", V((s * 0.2405, 0.030, 0.69)), (s, 0, 0), size=0.010, m="stencil_red", name="leg_warn" + L)
    stencil("Р9", V((s * 0.243, 0.070, 0.80)), (s, 0, 0), size=0.011, m="stencil_dark", name="mk_p9" + L)


def torso_S():
    T_ = lambda z, cy, w, F, B, cf, cb, kf=0.006: (z, 0.0, cy, w, w, F, B, cf, cb, kf, 0.0)
    rows = [T_(1.060, 0.0, 0.184, 0.138, 0.222, 0.060, 0.055), T_(1.150, 0.0, 0.186, 0.160, 0.205, 0.064, 0.060), T_(1.240, 0.0, 0.196, 0.168, 0.190, 0.068, 0.062),
            T_(1.300, 0.0, 0.210, 0.174, 0.200, 0.070, 0.064), T_(1.380, 0.0, 0.244, 0.174, 0.222, 0.074, 0.030), T_(1.440, 0.0, 0.252, 0.170, 0.226, 0.078, 0.030),
            T_(1.500, 0.0, 0.216, 0.150, 0.212, 0.070, 0.046), T_(1.545, 0.005, 0.180, 0.120, 0.176, 0.050, 0.055, 0.0), T_(1.565, 0.008, 0.165, 0.105, 0.130, 0.040, 0.040, 0.0)]
    hut, tr = seg_loft(rows, 1, "shell", "hut", bev=0.004)
    for k, k2, mm, nm in ((0, 2, "mirror", "abdomen"), (2, 5, "borazon", "C2")):                       # per side: chest C2, the outer wrap; abdomen
        for j in (0, 9): face_plate(tr, k, j, mm, "chest_%s_%d" % (nm, j), mg=0.014, k2=k2)
    for j in (1, 8): face_plate(tr, 2, j, "mirror", "chest_wrap_%d" % j, mg=0.014, k2=5)
    loft4([(1.545, 0, 0.01, 0.17, 0.17, 0.13, 0.13, 2.4), (1.56, 0, 0.01, 0.168, 0.168, 0.13, 0.13, 2.4), (1.575, 0, 0.01, 0.165, 0.165, 0.128, 0.128, 2.4)], "bear", "collar", 72, part="bearings")
    p = tr[5][0].lerp(tr[6][0], 0.5); stencil("ПАНЦИРЬ-2", p + V((0, -0.001, 0)), (0, -1, 0.3), size=0.024, m="stencil_red", name="chest_mark")
    # pelvis: a separate part, the high leg cut (narrowing V between the thighs), front and seat plates
    P_ = lambda z, w, F, B, cf, cb: (z, 0.0, 0.005, w, w, F, B, cf, cb, 0.005, 0.0)
    pv, pr = seg_loft([P_(1.025, 0.226, 0.150, 0.236, 0.05, 0.07), P_(0.975, 0.240, 0.152, 0.238, 0.05, 0.08), P_(0.930, 0.190, 0.142, 0.232, 0.05, 0.08),
                       P_(0.870, 0.110, 0.128, 0.190, 0.04, 0.06), P_(0.795, 0.090, 0.108, 0.140, 0.03, 0.04)][::-1], 1, "shell", "pelvis", bev=0.0035)
    for j in (0, 9): face_plate(pr, 2, j, "mirror", "pelvis_front%d" % j, mg=0.013)
    for j in (4, 5): face_plate(pr, 2, j, "mirror", "pelvis_seat%d" % j, mg=0.013)
    for j in (2, 7): face_plate(pr, 3, j, "borazon", "pelvis_side%d" % j, mg=0.010)
    stencil("ЗАВ. № 2-0417", V((-0.2405, 0.02, 0.955)), (-1, 0, 0), size=0.011, m="stencil_dark", name="serial_mark")
    # the waist ring Р0 (dark, ribbed rim) with its red zero mark
    seg_loft([(1.024, 0.0, 0.004, 0.214, 0.214, 0.146, 0.230, 0.05, 0.06, 0.0, 0.0), (1.062, 0.0, 0.004, 0.196, 0.196, 0.146, 0.224, 0.05, 0.06, 0.0, 0.0)],
             1, "bear", "waist_P0", part="bearings", bev=0.002)
    seg_loft([(1.050, 0.0, 0.004, 0.204, 0.204, 0.152, 0.232, 0.062, 0.058, 0.0, 0.0), (1.078, 0.0, 0.004, 0.190, 0.190, 0.146, 0.226, 0.060, 0.056, 0.0, 0.0)],
             1, "shell", "waist_skirt", part="shell", bev=0.003)                                                # skirt plates lapping over Р0
    for k in range(5): rbox(V((-0.10 + k * 0.05, -0.151, 1.043)), (0.004, 0.004, 0.030), "fin", "waist_rib", r=0.0008, seg=1, part="bearings")
    rbox(V((0.0, -0.152, 1.043)), (0.004, 0.004, 0.034), "red", "waist_zero", r=0.0008, seg=1, part="bearings")
    stencil("Р0", V((0.035, -0.1515, 1.043)), (0, -1, 0), size=0.012, m="stencil_white", name="mk_p0")
    # torso side exo members (pelvis -> armpit) and the shoulder yokes to the Р2 drives behind the shoulders
    for s in (1, -1):
        bar(V((s * 0.192, 0.100, 1.075)), V((s * 0.209, 0.100, 1.280)), 0.040, 0.012, "frame", "torso_member", up=(s, 0, 0), part="frame", r=0.003)
        p2 = V((s * 0.215, 0.150, 1.505))
        ring_motor_ax(p2, (0, 1, 0), 0.040, 0.026, "motor_shoulder_P2" + ("L" if s > 0 else "R"))
        bar(V((s * 0.05, 0.176, 1.530)), p2 + V((-s * 0.03, 0.0, 0.012)), 0.040, 0.012, "frame", "shoulder_yoke", up=(0, 0.35, 1), part="frame", r=0.003)
        stencil("Р2", p2 + V((0, 0.016, 0.058)), (0, 1, 0), size=0.012, m="stencil_white", name="mk_p2")
    return hut


SHOULDER_J = V((0.160, 0.047, 1.445))        # the figure's glenohumeral joint (rig, figure scale 1.046)
UA_LEN, FA_LEN = 0.270, 0.246                 # the figure's upper arm / forearm (rig)
UA_ABD, FA_ABD, FA_FLEX = 32.0, 12.0, 8.0     # rest pose in the suit: the walls under the arm need ~32 deg at the shoulder
HAND_OD = {}; ARM_GEO = {}


def arm_geo(s):
    """the arm chain on the figure's bones: shoulder joint J, elbow E, wrist W; segment rotations"""
    J = V((s * SHOULDER_J.x, SHOULDER_J.y, SHOULDER_J.z))
    R1 = Matrix.Rotation(math.radians(-UA_ABD * s), 3, 'Y')
    R2 = Matrix.Rotation(math.radians(-FA_ABD * s), 3, 'Y') @ Matrix.Rotation(math.radians(-FA_FLEX), 3, 'X')
    u1 = R1 @ V((0, 0, -1)); u2 = R2 @ V((0, 0, -1))
    E = J + u1 * UA_LEN; W = E + u2 * FA_LEN
    return J, E, W, R1, R2, u1, u2


def arm_S(s):
    """rev. S (fit): the sleeve follows the figure's arm bones - shoulder bearing on her shoulder joint, the upper arm
    along her humerus (0.21 outer), the elbow drive on her elbow, the forearm 0.19 -> 0.17 to the wrist coupling on her
    wrist; the hand capsule holds her hand"""
    L = "L" if s > 0 else "R"; J, E, W, R1, R2, u1, u2 = arm_geo(s); ARM_GEO[s] = (J, E, W, u1, u2)
    T1 = lambda p: J + R1 @ p; T1i = lambda p: R1.transposed() @ (p - J)
    T2 = lambda p: E + R2 @ p; T2i = lambda p: R2.transposed() @ (p - E)
    A_ = lambda z, wo, wi, F, B, cf=0.036, cb=0.040, kf=0.004: (z, 0.0, 0.0, wo, wi, F, B, cf, cb, kf, 0.0)
    # Р1 shoulder bearing: a ring round her shoulder joint, its axis along the humerus
    cyl(T1(V((0, 0, 0.010))), T1(V((0, 0, -0.022))), 0.118, "bear", "armhole_P1" + L, seg=48, part="bearings")
    stencil("Р1", T1(V((s * 0.119, 0, -0.004))), R1 @ V((s, 0, 0)), up=R1 @ V((0, 0, 1)), size=0.012, m="stencil_white", name="mk_p1")
    do, dr = seg_loft([A_(0.070, 0.060, 0.044, 0.062, 0.068, 0.026, 0.028), A_(0.030, 0.112, 0.100, 0.100, 0.108, 0.042, 0.046), A_(-0.110, 0.110, 0.102, 0.100, 0.108)][::-1],
                      s, "shell", "deltoid" + L, T=T1, Ti=T1i)
    face_plate(dr, 0, 2, "mirror", "deltoid_out" + L, mg=0.014)
    seg_loft([A_(-0.108, 0.108, 0.102, 0.100, 0.106, kf=0.0), A_(-0.138, 0.108, 0.102, 0.100, 0.106, kf=0.0)], s, "bear", "ring_P3" + L, part="bearings", T=T1, Ti=T1i, bev=0.0015)
    uo, ur = seg_loft([A_(-0.136, 0.106, 0.100, 0.098, 0.106), A_(-0.232, 0.100, 0.096, 0.094, 0.102, 0.036, 0.040)][::-1], s, "shell", "upperarm" + L, T=T1, Ti=T1i)
    for j in (2, 0): face_plate(ur, 0, j, "mirror", "upper_pl%d" % j + L, mg=0.014)
    seg_loft([A_(-0.232, 0.103, 0.101, 0.098, 0.117, kf=0.0), A_(-0.300, 0.103, 0.101, 0.098, 0.117, kf=0.0)], s, "bear", "elbow" + L, part="bearings", T=T1, Ti=T1i, bev=0.002)
    fo, fr = seg_loft([A_(-0.028, 0.098, 0.094, 0.092, 0.108), A_(-0.070, 0.097, 0.093, 0.091, 0.104), A_(-0.228, 0.086, 0.084, 0.082, 0.088, 0.030, 0.030)][::-1],
                      s, "shell", "forearm" + L, T=T2, Ti=T2i)
    face_plate(fr, 0, 2, "borazon", "fore_out" + L, mg=0.013, k2=2)
    face_plate(fr, 0, 0, "mirror", "fore_f" + L, mg=0.013, k2=2)
    seg_loft([A_(-0.072, 0.099, 0.095, 0.093, 0.101, kf=0.0), A_(-0.086, 0.099, 0.095, 0.093, 0.101, kf=0.0)], s, "bear", "ring_P5" + L, part="bearings", T=T2, Ti=T2i, bev=0.0012)
    g = [(-0.045, -0.060), (0.045, -0.060), (0.055, -0.005), (0.0, 0.040), (-0.055, -0.005)]                      # olecranon guard (back of the elbow)
    o, _ = facet([T2(V((x, 0.104 + (z + 0.06) * 0.10, z))) for x, z in g][::-1], 0.007, "borazon", "olecranon" + L, part="plates"); bevel(o, 0.003, 2, 30)
    for k in range(5):                                                       # inside of the elbow bend: armoured bellows (folds compress)
        zf = -0.238 - k * 0.0135; d = 0.008 if k % 2 == 0 else 0.003
        rbox(T1(V((0, -(0.096 + d), zf))), (0.150 - 0.006 * (k % 2), 0.014, 0.016), "bear", "elbow_bellows%d" % k + L, R1.to_quaternion(), r=0.004, seg=1, part="bearings")
    seg_loft([A_(0.018, 0.122, 0.112, 0.110, 0.118, 0.046, 0.050, kf=0.0), A_(-0.004, 0.126, 0.116, 0.114, 0.122, 0.048, 0.052, kf=0.0)], s, "shell", "shoulder_collar" + L, T=T1, Ti=T1i, bev=0.003)
    ax = R1 @ V((s, 0, 0)); el = E + ax * 0.112
    ring_motor_ax(el, ax, 0.040, 0.024, "motor_elbow" + L, fins=40)
    stencil("Р4", el + ax * 0.0145 + V((0, 0, 0.0)), ax, up=u1 * -1, size=0.011, m="stencil_white", name="mk_p4")
    p2 = V((s * 0.215, 0.150, 1.505))
    bar(T1(V((s * 0.124, 0.0, -0.020))), T1(V((s * 0.124, 0.0, -0.250))), 0.040, 0.012, "frame", "arm_member" + L, up=ax, part="frame", r=0.003)
    bar(p2 + V((s * 0.02, -0.02, -0.02)), T1(V((s * 0.124, 0.02, -0.020))), 0.030, 0.012, "frame", "arm_link" + L, up=(0, 1, 0), part="frame", r=0.003)
    bar(T2(V((s * 0.110, 0.0, -0.040))), T2(V((s * 0.100, 0.0, -0.215))), 0.032, 0.010, "frame", "forearm_member" + L, up=R2 @ V((s, 0, 0)), part="frame", r=0.003)
    # the hand «Ладонь» on her wrist, capsule 0.14 outer (palm to back), digits shorter and thicker
    HAND_OD[s] = (W, u2)
    R = lambda x, y: (x, y); sc = lambda z: (0.0, 0.07)
    before = set(COLL[0].objects)
    HAND_INFO[s] = hand_B3(s, R, sc, L)
    HAND_OBJS[s] = [o for o in COLL[0].objects if o not in before]
    O, D, Bv, Uv = HAND_INFO[s]
    # the back of a hand: a flat trapezoid 0.14 wide, 0.10 thick at the wrist tapering to 0.08 at the knuckles; the cuff
    # steps down gently from the 0.17 wrist (0.155 x 0.13)
    cU, cB = 0.155 / 0.10, 0.130 / 0.082
    for o in HAND_OBJS[s]:
        if o.type != 'MESH': continue
        for v in o.data.vertices:
            d = v.co - O; a, b, u = d.dot(D), d.dot(Bv), d.dot(Uv); t = max(0.0, min(1.0, (a + 0.005) / 0.055)); t = t * t * (3 - 2 * t)
            if a > 0.165: t = 1.0
            ta = max(0.0, min(1.0, a / 0.16)); kU = 0.140 / 0.126; kB = (0.100 - 0.020 * ta) / 0.074
            sU, sB = cU + (kU - cU) * t, cB + (kB - cB) * t
            if a > 0.170: sU, sB = 1.30, 1.30
            v.co = O + D * a + Bv * (b * sB) + Uv * (u * sU)
    SEAM_OBJS.extend(o for o in HAND_OBJS[s] if not o.name.startswith("Pantsir_cuff") and o not in GHOST.get(s, []))
    stencil("Р6", O + Bv * 0.068 + D * (-0.02), Bv, up=-D, size=0.010, m="stencil_white", name="mk_p6")
    stencil("ЭФФЕКТОР", O + D * 0.085 + Bv * 0.047, Bv, up=-D, size=0.0085, m="stencil_dark", name="capsule_mark")
    for o in HAND_OBJS[s]:
        if o.name.startswith("Pantsir_coupling_release"): o.hide_render = True                 # one red index mark: the grip lock


def _off2(pts, d):
    """offset a convex (y, z) polygon inward by d"""
    n = len(pts); A = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n)); sg = 1.0 if A > 0 else -1.0
    res = []
    for i in range(n):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % n]
        def nr(a, b):
            dx, dy = b[0] - a[0], b[1] - a[1]; Ln = math.hypot(dx, dy); return (-dy / Ln * sg, dx / Ln * sg)
        n1, n2 = nr(p0, p1), nr(p1, p2); bx, by = n1[0] + n2[0], n1[1] + n2[1]; bl = math.hypot(bx, by)
        k = d / max(0.35, (bx / bl) * n1[0] + (by / bl) * n1[1]); res.append((p1[0] + bx / bl * k, p1[1] + by / bl * k))
    return res


BU_PRO = [(0.176, 1.050), (0.370, 1.050), (0.398, 1.075), (0.400, 1.330), (0.335, 1.600), (0.300, 1.635), (0.190, 1.635), (0.176, 1.600)]
BU_W, BU_CH = 0.215, 0.022
BU_DY = 0.040; BU_BEFORE = [set()]


def backunit_S():
    """«блок на спине скафандра»: one-piece chamfered frame (graphite chamfers, enamel body), the mechanised radiator on the
    face tilted up-back, battery bay Б1/Б2, five capped ports, the recessed top handle, the red emergency pull"""
    BU_BEFORE[0] = set(COLL[0].objects)
    inner = _off2(BU_PRO, BU_CH); rings = []
    for x, pro in ((-BU_W, inner), (-(BU_W - BU_CH), BU_PRO), (BU_W - BU_CH, BU_PRO), (BU_W, inner)):
        rings.append([V((x, y, z)) for y, z in pro])
    n = len(BU_PRO); vs = [p for r in rings for p in r]; fs = []; fm = []
    for k in range(3):
        for j in range(n): j1 = (j + 1) % n; fs.append((k * n + j, k * n + j1, (k + 1) * n + j1, (k + 1) * n + j)); fm.append(0 if k == 1 else 1)
    fs += [tuple(reversed(range(n))), tuple(range(3 * n, 4 * n))]; fm += [1, 1]
    o = mk(vs, fs, "door", "backunit", 0.05, "door", mats=["door", "frame"], fm=fm); bevel(o, 0.004, 2, 25)
    for pg in o.data.polygons: pg.use_smooth = False
    Y = 0.400; N_ = V((0, 1, 0))
    def bx(x, z, w, h, d, m, nm, y=Y, r=0.002): return rbox(V((x, y + d / 2, z)), (w, d, h), m, nm, r=r, seg=2, part="door")
    # the vertical back face (x_bl = -x_sheet: the figure's right is -x)
    o, _ = facet([V((0.160, Y + 0.004, 1.220)), V((0.160, Y + 0.004, 1.322)), V((-0.080, Y + 0.004, 1.322)), V((-0.080, Y + 0.004, 1.220))], 0.004, "door", "pump_hatch", part="door"); bevel(o, 0.003, 2, 30)
    stencil("ОСТОРОЖНО · РАДИАТОР ДО +300 °C", V((0.040, Y + 0.004, 1.305)), N_, size=0.0105, m="stencil_red", name="bu_warn")
    stencil("ПАНЦИРЬ-2", V((0.040, Y + 0.004, 1.272)), N_, size=0.024, m="stencil_red", name="bu_mark")
    stencil("ЗАВ. № 2-0417 · ТЕПЛОВОЙ НАСОС", V((0.040, Y + 0.004, 1.240)), N_, size=0.0085, m="stencil_dark", name="bu_serial")
    o, _ = facet([V((0.160, Y + 0.002, 1.060)), V((0.160, Y + 0.002, 1.212)), V((-0.080, Y + 0.002, 1.212)), V((-0.080, Y + 0.002, 1.060))], 0.002, "door", "battery_bay", part="door")
    for x0, x1, lab in ((0.152, 0.040, "Б1"), (0.032, -0.072, "Б2")):
        o, _ = facet([V((x0, Y + 0.0055, 1.068)), V((x0, Y + 0.0055, 1.184)), V((x1, Y + 0.0055, 1.184)), V((x1, Y + 0.0055, 1.068))], 0.0035, "door", "cassette_" + lab, part="door"); bevel(o, 0.0025, 2, 30)
        xm = (x0 + x1) / 2
        bx(xm, 1.090, 0.070, 0.013, 0.003, "dark", "cassette_pull", y=Y + 0.0045, r=0.002)
        for xe in (x0 + (x1 - x0) * 0.04, x1 - (x1 - x0) * 0.04): bx(xe, 1.11, 0.003, 0.11, 0.004, "frame", "cassette_rail", y=Y + 0.002, r=0.0006)
        stencil(lab, V((xm, Y + 0.0056, 1.137)), N_, size=0.024, m="stencil_dark", name="bu_" + lab)
    stencil("Б1 · Б2 — ЗАМЕНА БЕЗ НАГРУЗКИ", V((0.042, Y + 0.0025, 1.198)), N_, size=0.0075, m="stencil_dark", name="bu_bat")
    for z, lab in ((1.272, "О2"), (1.226, "ВОДА"), (1.180, "ТН"), (1.134, "28 В"), (1.088, "ДАННЫЕ")):
        cyl(V((-0.160, Y - 0.002, z)), V((-0.160, Y + 0.010, z)), 0.017, "door", "port_cap", seg=36, part="door")
        cyl(V((-0.160, Y + 0.010, z)), V((-0.160, Y + 0.013, z)), 0.011, "frame", "port_cap_in", seg=32, part="door")
        bx(-0.160, z, 0.016, 0.003, 0.002, "lmetal", "port_cap_slot", y=Y + 0.0128, r=0.0005)
        stencil(lab, V((-0.118, Y + 0.0005, z - 0.003)), N_, size=0.0085, m="stencil_dark", name="bu_port")
    rbox(V((-0.198, Y + 0.010, 1.135)), (0.014, 0.020, 0.074), "red", "emergency_pull", r=0.003, seg=2, part="door")
    bx(-0.198, 1.135, 0.020, 0.080, 0.003, "dark", "emergency_pocket", y=Y, r=0.002)
    stencil("АВАРИЙНОЕ ОТКРЫТИЕ — ТЯНУТЬ", V((-0.170, Y + 0.004, 1.192)), N_, size=0.0058, m="stencil_red", name="bu_emerg")
    # recessed top handle, side grab slots
    rbox(V((0.0, 0.262, 1.633)), (0.190, 0.034, 0.010), "dark", "handle_pocket", r=0.003, seg=2, part="door")
    rbox(V((0.0, 0.262, 1.634)), (0.160, 0.012, 0.009), "lmetal", "handle_bar", r=0.003, seg=2, part="door")
    stencil("ВЗЯТЬ ЗДЕСЬ", V((0.0, 0.234, 1.6355)), (0, 0, 1), up=(0, -1, 0), size=0.0075, m="stencil_dark", name="bu_take")
    for s in (1, -1):
        rbox(V((s * BU_W, 0.33, 1.455)), (0.010, 0.030, 0.110), "dark", "grab_slot", r=0.003, seg=2, part="door")
    # the radiator on the tilted face: tray, 22 fins, rails (the face group tilts), louvres, seal, wings
    t = V((0, -0.065, 0.270)).normalized(); nT = V((0, 0.270, 0.065)).normalized(); Ls = 0.2777
    def F_(x, s_, h): return RAD_PIVOT + V((x, 0, 0)) + t * s_ + nT * h
    q = Matrix((V((1, 0, 0)), nT, t)).transposed().to_quaternion()
    face = [rbox(F_(0, Ls / 2, 0.002), (0.360, 0.004, 0.250), "fin", "rad_tray", q, r=0.001, seg=1, part="radiator")]
    for k in range(22):
        x = -0.150 + 0.300 * k / 21
        face.append(rbox(F_(x, Ls / 2, 0.016), (0.0035, 0.028, 0.232), "lmetal" if k % 2 else "hub", "rad_fin%02d" % k, q, r=0.0008, seg=1, part="radiator"))
    for s_ in (0.010, Ls - 0.010): face.append(rbox(F_(0, s_, 0.012), (0.380, 0.024, 0.014), "frame", "rad_rail", q, r=0.002, seg=1, part="radiator"))
    RAD["tilt"] += face
    stencil("Р — ТЕПЛОВОЙ НАСОС", F_(0.0, -0.012, 0.0005) + V((0, 0.004, 0)), (0, 1, 0), size=0.0075, m="stencil_dark", name="bu_rad")
    # louvres: 6 faceted W-ZrB2 plates shingled down the slope (closed states), stowed in the top housing when hot
    prof = [(0.0, 0.0), (0.0, 0.006), (-0.006, 0.011), (-0.040, 0.012), (-0.048, 0.009), (-0.050, 0.003), (-0.044, 0.0)]
    for k in range(6):
        top = Ls - 0.006 - k * 0.0435; vs = []; fs = []; npf = len(prof); xs = (-0.160, 0.0, 0.160)
        for i, x in enumerate(xs):
            for ds, dh in prof: vs.append(F_(x, top + ds, 0.028 + dh - ds * 0.06 + (0.004 if i == 1 else 0.0) * (dh / 0.012)))
        for i in range(2):
            for j in range(npf): j1 = (j + 1) % npf; fs.append((i * npf + j, i * npf + j1, (i + 1) * npf + j1, (i + 1) * npf + j))
        fs += [tuple(range(npf)), tuple(2 * npf + j for j in reversed(range(npf)))]
        o = mk(vs, fs, "louvre", "rad_louvre%d" % k, 0.3, "radiator"); bevel(o, 0.0025, 2, 25); RAD["louvres"].append(o)
    for (a, b) in (((-0.170, 0.0), (0.170, 0.0)), ((-0.170, Ls), (0.170, Ls)), ((-0.170, 0.0), (-0.170, Ls)), ((0.170, 0.0), (0.170, Ls))):
        RAD["seal"].append(bar(F_(a[0], a[1], 0.031), F_(b[0], b[1], 0.031), 0.008, 0.004, "sealor", "rad_seal", up=nT, part="radiator", r=0.001))
    # side wings: radiator panels folded flat on the unit's side faces, swinging out on ring hinges at the back corner
    for s in (1, -1):
        L = "L" if s > 0 else "R"; ax0, ax1 = V((s * (BU_W + 0.006), 0.386, 1.080)), V((s * (BU_W + 0.006), 0.386, 1.360)); WING_AX[s] = (ax0, ax1)
        w = [rbox(V((s * (BU_W + 0.010), 0.312, 1.220)), (0.008, 0.146, 0.272), "door", "wing_panel" + L, r=0.003, seg=2, part="radiator")]
        for k in range(5):
            w.append(rbox(V((s * (BU_W + 0.022), 0.252 + k * 0.028, 1.220)), (0.020, 0.003, 0.250), "lmetal", "wing_fin" + L, r=0.0006, seg=1, part="radiator"))
            w.append(rbox(V((s * (BU_W + 0.0005), 0.252 + k * 0.028, 1.220)), (0.002, 0.003, 0.250), "lmetal", "wing_fin_in" + L, r=0.0, seg=1, part="radiator"))
        RAD["wing" + L] += w
        for z in (1.085, 1.355):
            cyl(V((s * (BU_W + 0.006), 0.386, z - 0.012)), V((s * (BU_W + 0.006), 0.386, z + 0.012)), 0.011, "bear", "wing_hinge_motor" + L, seg=32, part="radiator")
            cyl(V((s * (BU_W + 0.006), 0.386, z + 0.012)), V((s * (BU_W + 0.006), 0.386, z + 0.0135)), 0.006, "hub", "wing_hinge_hub" + L, seg=24, part="radiator")
    # evaporator slots on the top face: flaps closed (cryo), open (hot), capped (liquid)
    for k in range(5):
        x = -0.10 + k * 0.05
        rbox(V((x, 0.215, 1.6345)), (0.030, 0.034, 0.004), "dark", "evap_slot", r=0.001, seg=1, part="door")
        RAD["flaps_closed"].append(rbox(V((x, 0.215, 1.6365)), (0.028, 0.032, 0.003), "door", "evap_flap", r=0.001, seg=1, part="door"))
        RAD["flaps_open"].append(rbox(V((x, 0.226, 1.645)), (0.028, 0.032, 0.003), "door", "evap_flap_open", Quaternion((1, 0, 0), math.radians(-40)), r=0.001, seg=1, part="door"))
        RAD["caps"].append(rbox(V((x, 0.215, 1.6385)), (0.036, 0.040, 0.007), "door", "evap_cap", r=0.002, seg=2, part="door"))
        RAD["caps"].append(rbox(V((x, 0.215, 1.6362)), (0.040, 0.044, 0.003), "sealor", "evap_cap_seal", r=0.001, seg=1, part="door"))
    stencil("ИСПАРИТЕЛЬ", V((0.0, 0.196, 1.6355)), (0, 0, 1), up=(0, -1, 0), size=0.007, m="stencil_dark", name="bu_evap")
    global RAD_PIVOT
    for o in [o for o in COLL[0].objects if o not in BU_BEFORE[0]]: o.matrix_world = Matrix.Translation(V((0, BU_DY, 0))) @ o.matrix_world
    RAD_PIVOT = RAD_PIVOT + V((0, BU_DY, 0))
    for k_ in WING_AX: WING_AX[k_] = (WING_AX[k_][0] + V((0, BU_DY, 0)), WING_AX[k_][1] + V((0, BU_DY, 0)))
    bpy.context.view_layer.update()
    for o in RAD["tilt"] + RAD["louvres"] + RAD["seal"] + RAD["wingL"] + RAD["wingR"] + RAD["flaps_open"] + RAD["flaps_closed"] + RAD["caps"]:
        SAVED["rad_" + o.name] = o.matrix_world.copy()


def set_rad(state):
    """switch the radiator: RAD_STATES[state] = wing angle, louvre position, seal cover, face tilt"""
    st = RAD_STATES[state]
    for k in RAD:
        for o in RAD[k]: o.matrix_world = SAVED["rad_" + o.name].copy()
    T = Matrix.Translation(RAD_PIVOT) @ Matrix.Rotation(math.radians(-st["tilt"]), 4, 'X') @ Matrix.Translation(-RAD_PIVOT)
    for o in RAD["tilt"]: o.matrix_world = T @ o.matrix_world
    for s, L in ((1, "L"), (-1, "R")):
        a0, a1 = WING_AX[s]; ax = (a1 - a0).normalized()
        W = Matrix.Translation(a0) @ Matrix.Rotation(math.radians(st["wing"] * s), 4, ax) @ Matrix.Translation(-a0)
        for o in RAD["wing" + L]: o.matrix_world = W @ o.matrix_world
    for o in RAD["louvres"]:
        o.hide_render = st["louvre"] < 0.5
        if st["seal"] > 0.5: o.matrix_world = Matrix.Translation(-V((0, 0.270, 0.065)).normalized() * 0.002) @ o.matrix_world
    for o in RAD["seal"]: o.hide_render = st["seal"] < 0.5
    for o in RAD["flaps_open"]: o.hide_render = state != "hot"
    for o in RAD["flaps_closed"]: o.hide_render = state != "cryo"
    for o in RAD["caps"]: o.hide_render = state != "liquid"
    bpy.context.view_layer.update()


def body_S():
    s_mats()
    for s in (1, -1): boot_O(s); leg_S(s)
    hut = torso_S()
    for s in (1, -1): arm_S(s)
    backunit_S()
    return hut


# ------------------------------------------------------------------ the helmet V1, variant C «Фотонное окно под шторкой» (rev. S)
# sketch/build_sketch_helmet_optics.py row C, HEAVY_SUIT.md «Шлем — вариант С». Kinematics V1 unchanged: the cap swings 65 deg
# up-back on HINGE behind the crown (game HelmCapHinge 0, 0.8745, -0.0943); split plane chin -> cheekbones -> behind the
# ears -> nape; the back of the head and the chin bar stay on the collar. Shell: faceted octagonal plate sections, outer
# width 0.256 m, wall 30 mm, 4 mm chamfers. Front: a graphite visor frame (wrapped in 3 facets) carrying all sensors as
# armoured ports; in its recess the photonic window 176 x 68 mm (borazon pane + Bragg IR reflector) behind 6 faceted W-ZrB2
# louvres that retract up into the brow bar.
CHIN = 1.615
SPLIT_A, SPLIT_B = (0.105, CHIN), (-0.125, 1.83)
HINGE = V((0, -SPLIT_B[0], SPLIT_B[1])); OPEN_DEG = 65.0
OPENABLE = []; SEAL = []; SAVED = {}; ACT_OPEN = []; ACT_CLOSED = []; LOUV_OPEN = []; LOUV_CLOSED = []
HO = V((0.0, 0.013, 1.708))           # the sketch origin (head centre at eye level): the sketch hinge (-0.112, 0.122) -> HINGE
# shell sections (h above HO, half width w, front F, back B, front / back chamfers) - sketch FRONT / SIDE outlines
# (12-gon: a keel kf on the front / back centre line, a side crease bulging sb at mid depth -> outer width 2 x (w + sb) = 0.256)
HSEC = [(-0.130, 0.100, 0.108, 0.118, 0.034, 0.050, 0.004, 0.004), (-0.100, 0.114, 0.130, 0.136, 0.040, 0.056, 0.005, 0.006),
        (-0.035, 0.123, 0.146, 0.152, 0.044, 0.060, 0.005, 0.006), (0.055, 0.123, 0.146, 0.148, 0.044, 0.060, 0.005, 0.006),
        (0.115, 0.104, 0.126, 0.118, 0.040, 0.052, 0.004, 0.005), (0.150, 0.068, 0.082, 0.068, 0.028, 0.036, 0.002, 0.003),
        (0.158, 0.056, 0.068, 0.054, 0.022, 0.028, 0.0, 0.0)]
WRAP_X, WRAP_S, FR_U0, FR_D, FR_V = 0.068, 0.55, 0.160, 0.044, 0.07     # facet break, wing slope, frame front, depth, keel slope      # window facet break, wing slope, frame front, frame depth
CFRAME = [(-0.100, 0.068), (0.100, 0.068), (0.118, 0.036), (0.118, -0.046), (0.100, -0.070), (-0.100, -0.070), (-0.118, -0.046), (-0.118, 0.036)]
CWIN = [(-0.068, 0.036), (0.068, 0.036), (0.088, 0.020), (0.088, -0.022), (0.068, -0.032), (-0.068, -0.032), (-0.088, -0.022), (-0.088, 0.020)]
WIN_T, WIN_B = 0.036, -0.032


def hp(x, h, u): return V((x, HO.y - u, HO.z + h))                     # sketch (across, up, forward) -> Blender


def fr_u(x, h, inset=0.0):
    """the frame's front surface: centre facet flat, wings 22 deg back, brow bar leaning back above, bottom bar below"""
    u = FR_U0 - min(abs(x), WRAP_X) * FR_V - max(0.0, abs(x) - WRAP_X) * WRAP_S
    if h > WIN_T: u -= 0.25 * (h - WIN_T)
    elif h < WIN_B: u -= 0.30 * (WIN_B - h)
    return u - inset


def fr_pn(x, h, inset=0.0, uf=None):
    uf = uf or (lambda x_, h_: fr_u(x_, h_, inset))
    p = hp(x, h, uf(x, h)); e = 1e-4
    tx = hp(x + e, h, uf(x + e, h)) - hp(x - e, h, uf(x - e, h))
    th = hp(x, h + e, uf(x, h + e)) - hp(x, h - e, uf(x, h - e))
    n = tx.cross(th).normalized()
    return p, (n if n.y < 0 else -n)


def h_mats():
    mat("graphite", (0.032, 0.035, 0.039), metal=0.2, rough=0.52, bump=0.05)            # graphite enamel zones
    mat("wzr", (0.11, 0.112, 0.12), metal=0.9, rough=0.34, aniso=0.3)                 # W-ZrB2 louvres and port housings
    mat("iris", (0.018, 0.02, 0.028), metal=1.0, rough=0.32)
    mat("louvre", (0.048, 0.050, 0.056), metal=0.9, rough=0.36, aniso=0.7)              # W-ZrB2 louvres: dark brushed metal
    mat("shell2", (0.150, 0.155, 0.153), rough=0.6, bump=0.08)                         # the fixed back shell, a shade darker
    mat("lens", (0.004, 0.006, 0.010), rough=0.02, coat=1.0)
    mat("germ", (0.035, 0.034, 0.036), metal=0.7, rough=0.10)
    mat("lampglass", (0.85, 0.80, 0.68), rough=0.22, emit=((1.0, 0.86, 0.62), 0.6))
    for nm, th in (("lens", 280.0), ("germ", 220.0)):
        p = next(n for n in M[nm].node_tree.nodes if n.type == "BSDF_PRINCIPLED")
        p.inputs["Thin Film Thickness"].default_value = th; p.inputs["Thin Film IOR"].default_value = 1.38
        p.inputs["IOR"].default_value = 1.75
    mat("boraz_glass", (0.98, 0.90, 0.78), rough=0.015)                                   # the outer borazon pane: clear amber
    p = next(n for n in M["boraz_glass"].node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Transmission Weight"].default_value = 1.0; p.inputs["IOR"].default_value = 2.12
    # the photonic pane: the suit's mirror-plate shader (thin film -> gold / violet sheen) mixed with amber transmission
    pm = M["mirror"].copy(); pm.name = "Pantsir_photonic"; nt = pm.node_tree; out = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
    src = out.inputs["Surface"].links[0].from_socket
    g = nt.nodes.new("ShaderNodeBsdfPrincipled"); g.inputs["Base Color"].default_value = (1.0, 0.66, 0.30, 1)
    g.inputs["Transmission Weight"].default_value = 1.0; g.inputs["Roughness"].default_value = 0.02; g.inputs["IOR"].default_value = 1.6
    g.inputs["Thin Film Thickness"].default_value = 470.0; g.inputs["Thin Film IOR"].default_value = 1.45
    lw = nt.nodes.new("ShaderNodeLayerWeight"); lw.inputs["Blend"].default_value = 0.35            # gold head-on -> violet at grazing
    cr = nt.nodes.new("ShaderNodeValToRGB"); el = cr.color_ramp.elements
    tc2 = nt.nodes.new("ShaderNodeTexCoord"); nz2 = nt.nodes.new("ShaderNodeTexNoise"); nz2.inputs["Scale"].default_value = 6.0
    nz2.inputs["Detail"].default_value = 1.5; nt.links.new(tc2.outputs["Object"], nz2.inputs["Vector"])
    ad = nt.nodes.new("ShaderNodeMath"); ad.operation = 'MULTIPLY_ADD'; ad.inputs[1].default_value = 0.6; ad.inputs[2].default_value = -0.3
    nt.links.new(nz2.outputs["Fac"], ad.inputs[0])                                                     # interference bands across the pane
    ad2 = nt.nodes.new("ShaderNodeMath"); ad2.operation = 'ADD'; ad2.use_clamp = True
    nt.links.new(ad.outputs[0], ad2.inputs[0]); nt.links.new(lw.outputs["Facing"], ad2.inputs[1]); nt.links.new(ad2.outputs[0], cr.inputs["Fac"])
    el[0].color = (1.0, 0.74, 0.32, 1); el[1].position = 0.92; el[1].color = (0.55, 0.36, 0.80, 1); e = el.new(0.62); e.color = (0.96, 0.58, 0.30, 1)
    lw.inputs["Blend"].default_value = 0.55
    rm = nt.nodes.new("ShaderNodeBsdfPrincipled"); rm.inputs["Metallic"].default_value = 1.0; rm.inputs["Roughness"].default_value = 0.07
    nt.links.new(cr.outputs["Color"], rm.inputs["Base Color"])
    ma = nt.nodes.new("ShaderNodeMixShader"); ma.inputs[0].default_value = 0.5; nt.links.new(src, ma.inputs[1]); nt.links.new(rm.outputs[0], ma.inputs[2])
    mx = nt.nodes.new("ShaderNodeMixShader"); mx.inputs[0].default_value = 0.15
    nt.links.new(ma.outputs[0], mx.inputs[1]); nt.links.new(g.outputs[0], mx.inputs[2]); nt.links.new(mx.outputs[0], out.inputs["Surface"])
    M["photonic"] = pm


def _col_h(poly, x):
    hs = []
    for (x0, h0), (x1, h1) in zip(poly, poly[1:] + poly[:1]):
        if abs(x1 - x0) < 1e-9:
            if abs(x - x0) < 1e-9: hs += [h0, h1]
        elif min(x0, x1) - 1e-9 <= x <= max(x0, x1) + 1e-9: hs.append(h0 + (h1 - h0) * (x - x0) / (x1 - x0))
    return (min(hs), max(hs)) if hs else None


def _clip(poly, lo, hi):
    """clip a convex (x, h) polygon to the band lo <= h <= hi"""
    for lim, keep in ((lo, lambda h: h >= lo - 1e-9), (hi, lambda h: h <= hi + 1e-9)):
        res = []
        for a, b in zip(poly, poly[1:] + poly[:1]):
            ia, ib = keep(a[1]), keep(b[1])
            if ia: res.append(a)
            if ia != ib: t = (lim - a[1]) / (b[1] - a[1]); res.append((a[0] + (b[0] - a[0]) * t, lim))
        poly = res
        if len(poly) < 3: return []
    return poly


def wrapped_sheet(outer, hole, ufun, name, m, depth, part="helmet", bev=0.003, xb=(), hb=()):
    """a flat-faceted sheet over the (x, h) outline (with an optional hole), its front on u = ufun(x, h), solidified back by
    depth. Columns split at every outline / hole vertex and at xb, bands at hb, so each face lies on one facet plane."""
    xs = sorted(set([round(p[0], 6) for p in outer] + [round(p[0], 6) for p in (hole or [])] + [round(v, 6) for v in xb]))
    x0o, x1o = min(p[0] for p in outer), max(p[0] for p in outer); xs = [x for x in xs if x0o - 1e-9 <= x <= x1o + 1e-9]
    hx0, hx1 = (min(p[0] for p in hole), max(p[0] for p in hole)) if hole else (1, -1)
    def nodes(x, lo, hi):
        ns = [h for h in hb if lo < h < hi]
        if hole and hx0 - 1e-9 <= x <= hx1 + 1e-9: ns += [h for h in _col_h(hole, x) if lo < h < hi]
        return sorted(set(ns))
    polys = []
    for xa, xb_ in zip(xs, xs[1:]):
        oa, ob = _col_h(outer, xa), _col_h(outer, xb_)
        if hole and xa >= hx0 - 1e-9 and xb_ <= hx1 + 1e-9:
            ha, hb2 = _col_h(hole, xa), _col_h(hole, xb_); rngs = [((oa[0], ha[0]), (ob[0], hb2[0])), ((ha[1], oa[1]), (hb2[1], ob[1]))]
        else: rngs = [(oa, ob)]
        for (la, ua), (lb, ub) in rngs:
            pl = [(xa, la), (xb_, lb)] + [(xb_, h) for h in nodes(xb_, lb, ub)] + [(xb_, ub), (xa, ua)] + [(xa, h) for h in reversed(nodes(xa, la, ua))]
            bands = [-1.0] + sorted(hb) + [1.0]
            for lo, hi in zip(bands, bands[1:]):
                c = _clip(pl, lo, hi)
                if len(c) >= 3: polys.append(c)
    bm = bmesh.new(); cache = {}
    def vert(x, h):
        k = (round(x, 6), round(h, 6))
        if k not in cache: cache[k] = bm.verts.new(hp(x, h, ufun(x, h)))
        return cache[k]
    for pl in polys:
        vv = []
        for x, h in pl:
            v = vert(x, h)
            if not vv or vv[-1] is not v: vv.append(v)
        if len(vv) > 2 and vv[0] is vv[-1]: vv.pop()
        if len(vv) >= 3:
            try: bm.faces.new(vv)
            except ValueError: pass
    for f in bm.faces:
        f.normal_update()
        if f.normal.y > 0: f.normal_flip()
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free(); me.materials.append(M[m])
    o = bpy.data.objects.new("Pantsir_" + name, me); COLL[0].objects.link(o)
    for pg in me.polygons: pg.use_smooth = False
    so = o.modifiers.new("sol", "SOLIDIFY"); so.thickness = depth; so.offset = -1.0; so.use_even_offset = True
    if bev: bevel(o, bev, 2, 30)
    PARTS.setdefault(part, []).append(o); return o


def _shape(kind, N, a, b=None):
    """N points round an outline, sampled at the same polar angles for every kind (so rings of two outlines pair up)"""
    out = []
    for j in range(N):
        t = 2 * math.pi * j / N; c, s = math.cos(t), math.sin(t)
        if kind == "circle": r = a
        elif kind == "hex":
            k = (t - math.radians(30)) % (math.pi / 3) - math.pi / 6; r = a * math.cos(math.pi / 6) / math.cos(k)
        else: r = min(a / max(abs(c), 1e-9), b / max(abs(s), 1e-9))       # "rect": half sizes a, b
        out.append((r * c, r * s))
    return out


def ring_prism(c, n, up, outer, inner, z0, z1, m, name, part="helmet_ports", bev=0.0006):
    n = V(n).normalized(); u = V(up) - n * n.dot(V(up)); u.normalize(); r = u.cross(n)
    P = lambda q, z: V(c) + r * q[0] + u * q[1] + n * z
    N = len(outer); vs = [P(q, z) for z in (z0, z1) for q in outer] + [P(q, z) for z in (z0, z1) for q in inner]; fs = []
    oi, ot, ii, it = 0, N, 2 * N, 3 * N
    for j in range(N):
        j1 = (j + 1) % N
        fs += [(ot + j, ot + j1, it + j1, it + j), (oi + j1, oi + j, ii + j, ii + j1), (oi + j, oi + j1, ot + j1, ot + j), (ii + j1, ii + j, it + j, it + j1)]
    o = mk(vs, fs, m, name, 0.5, part)
    if bev: bevel(o, bev, 1, 30)
    return o


def h_port(x, h, kind, tag, pn=None):
    """an armoured sensor port on the frame: a W-ZrB2 boss, a bright metal bezel, a dark iris ring, coated lens glass,
    the lens ~3 mm below the bezel lip"""
    p, n = pn if pn else fr_pn(x, h); up = V((0, 0, 1)); parts = []; N = globals().get("PORT_N", 48)
    def disc(r1, r2, z0, z1, m, nm): parts.append(cyl(p + n * z0, p + n * z1, r1, m, nm, r2=r2, seg=40, part="helmet_ports"))
    if kind in ("st", "pe", "th"):
        R, ri, lens_m = {"st": (0.0118, 0.0078, "lens"), "pe": (0.0102, 0.0072, "lens"), "th": (0.0105, 0.0076, "germ")}[kind]
        outer = _shape("rect", N, R, R * 0.86) if kind == "st" else (_shape("rect", N, R, R) if kind == "th" else _shape("circle", N, R))
        parts.append(ring_prism(p, n, up, outer, _shape("circle", N, ri), -0.004, 0.0036, "wzr", "port_boss_" + tag))
        parts.append(ring_prism(p, n, up, _shape("circle", N, ri + 0.0002), _shape("circle", N, ri - 0.0011), -0.002, 0.0043, "hub", "port_bezel_" + tag))
        parts.append(ring_prism(p, n, up, _shape("circle", N, ri - 0.0010), _shape("circle", N, ri - 0.0024), -0.002, 0.0010, "iris", "port_iris_" + tag))
        rl = ri - 0.0020
        disc(rl, rl, -0.002, 0.0004, lens_m, "port_lens_" + tag)
        disc(rl, rl * (0.45 if kind == "pe" else 0.62), 0.0004, 0.0004 + (0.0026 if kind == "pe" else 0.0011), lens_m, "port_dome_" + tag)
    elif kind == "li":                                                     # lidar: emitter + receiver in one housing
        parts.append(rbox(p + n * (-0.0003), (0.030, 0.0074, 0.0165), "wzr", "port_boss_" + tag, nquat(n), r=0.0012, seg=2, part="helmet_ports"))
        r_ = n.cross(up).normalized()
        for k, s in enumerate((-1, 1)):
            q = p + r_ * s * 0.0072
            parts.append(ring_prism(q, n, up, _shape("circle", N, 0.0052), _shape("circle", N, 0.0040), 0.002, 0.0046, "hub", "port_bezel_%s%d" % (tag, k)))
            parts.append(cyl(q + n * 0.0015, q + n * 0.0026, 0.0042, "lens", "port_lens_%s%d" % (tag, k), seg=40, part="helmet_ports"))
    elif kind == "lamp":                                                   # a recessed lamp: deep rectangular pocket, ribbed glass
        parts.append(ring_prism(p, n, up, _shape("rect", N, 0.0125, 0.0072), _shape("rect", N, 0.0098, 0.0049), -0.004, 0.0032, "wzr", "lamp_boss_" + tag))
        parts.append(rbox(p + n * (-0.0012), (0.0205, 0.0026, 0.0105), "lampglass", "lamp_glass_" + tag, nquat(n), r=0.0004, seg=1, part="helmet_ports"))
        for k in (-1, 0, 1):
            parts.append(rbox(p + n * 0.0002 + V((0, 0, k * 0.0028)), (0.0195, 0.0006, 0.0007), "hub", "lamp_rib_" + tag, nquat(n), r=0.0, part="helmet_ports"))
    return parts


def h_louvre(top, inset, name, k):
    """one W-ZrB2 louvre: a faceted armour slat (thin top tongue, upper facet, ridge face, lower chamfer), tilted 12 deg so
    the lower edge sits forward over the next slat; plan follows the window wrap; ends chamfered"""
    prof = [(0.0, 0.0), (0.0, 0.0058), (-0.0022, 0.0084), (-0.0148, 0.0088), (-0.0175, 0.0064), (-0.0175, 0.0016), (-0.0150, 0.0)]
    if k == 4: prof = prof[:4] + [(-0.0172, 0.0098), (-0.0205, 0.0084), (-0.0208, 0.0022), (-0.0180, 0.0)]     # the lowest: a heavy lip
    prof = [(dh, du - dh * 0.10) for dh, du in prof]
    dmin, dmax = min(p[1] for p in prof), max(p[1] for p in prof); xe, ch = 0.0905, 0.0
    xs = [-xe, -WRAP_X, 0.0, WRAP_X, xe]; vs = []; fs = []; n = len(prof); last = len(xs) - 1
    for i, x in enumerate(xs):
        for dh, du in prof:
            xx = x
            if i in (0, last): xx = math.copysign(xe - ch * (du - dmin) / (dmax - dmin), x)
            keel = 0.0045 * max(0.0, 1.0 - abs(x) / WRAP_X) * (du - dmin) / (dmax - dmin)     # the slat's own centre ridge
            vs.append(hp(xx, top + dh, fr_u(x, 0.0, inset) + du + keel))
    for i in range(last):
        for j in range(n): j1 = (j + 1) % n; fs.append((i * n + j, i * n + j1, (i + 1) * n + j1, (i + 1) * n + j))
    fs.append(tuple(range(n))); fs.append(tuple(last * n + j for j in reversed(range(n))))
    o = mk(vs, fs, "louvre", name, 0.3, "helmet_louvres"); bevel(o, 0.0012, 2, 25)
    return o


def helmet():
    h_mats()
    vs = []; fs = []; Rr = []
    for h, w, F, B, cf, cb, sb, kf in HSEC:
        um = ((F - cf) + (-B + cb)) / 2
        ring = [(0.0, F + kf), (w - cf, F), (w, F - cf), (w + sb, um), (w, -B + cb), (w - cb, -B), (0.0, -B - kf),
                (-(w - cb), -B), (-w, -B + cb), (-(w + sb), um), (-w, F - cf), (-(w - cf), F)]
        Rr.append(len(vs)); vs += [tuple(hp(x, h, u)) for x, u in ring]
    seg = 12
    for a, b in zip(Rr, Rr[1:]):
        for j in range(seg): j1 = (j + 1) % seg; fs.append((a + j, a + j1, b + j1, b + j))
    fs.append(tuple(range(Rr[-1], Rr[-1] + seg)))
    A = V((0, -SPLIT_A[0], SPLIT_A[1])); B = V((0, -SPLIT_B[0], SPLIT_B[1])); d = (B - A).normalized(); nrm = V((1, 0, 0)).cross(d).normalized()
    if nrm.dot(V((0, -1, 0.3))) < 0: nrm = -nrm
    # the window aperture through the cap wall (boolean), the window outline grown 1 mm
    cut_me = bpy.data.meshes.new("helmet_cut"); cw = [(x * 1.017, h * 1.04 + 0.0002) for x, h in CWIN]; nC = len(cw)
    cut_me.from_pydata([tuple(hp(x, h, u)) for u in (0.06, 0.30) for x, h in cw], [], [tuple(range(nC)), tuple(range(nC, 2 * nC))] +
                       [(j, (j + 1) % nC, nC + (j + 1) % nC, nC + j) for j in range(nC)])
    cutter = bpy.data.objects.new("Pantsir_helmet_cutter", cut_me); COLL[0].objects.link(cutter); cutter.hide_render = True; cutter.display_type = 'WIRE'; OPENABLE.append(cutter)
    def part(keep_cap, name, thick=0.030):
        bm = bmesh.new()
        for v in vs: bm.verts.new(v)
        bm.verts.ensure_lookup_table()
        for f_ in fs: bm.faces.new([bm.verts[i] for i in f_])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=A, plane_no=(-nrm if keep_cap else nrm), clear_outer=True)
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        o = bpy.data.objects.new("Pantsir_" + name, me); COLL[0].objects.link(o)
        me.materials.append(M["shell" if keep_cap else "shell2"]); me.materials.append(M["graphite"]); me.materials.append(M["dark"])
        for pg in me.polygons:
            pg.use_smooth = False
            if pg.center.z < HO.z - 0.098: pg.material_index = 1                        # the graphite jaw band (chin bar)
        so = o.modifiers.new("sol", "SOLIDIFY"); so.thickness = thick; so.offset = -1.0; so.material_offset = 2; so.use_even_offset = True
        so.material_offset_rim = 1
        if keep_cap:
            bo = o.modifiers.new("win", "BOOLEAN"); bo.operation = 'DIFFERENCE'; bo.object = cutter; bo.solver = 'EXACT'
        bevel(o, 0.004, 2, 30); PARTS.setdefault("helmet", []).append(o)
        return o
    part(False, "helmet_back"); cap = part(True, "helmet_cap"); OPENABLE.append(cap)
    # the seal ring along the split (shown only with the cap open)
    bm = bmesh.new()
    for v in vs: bm.verts.new((V(v) - HO) * 1.004 + HO)
    bm.verts.ensure_lookup_table()
    for f_ in fs: bm.faces.new([bm.verts[i] for i in f_])
    bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=A + nrm * 0.004, plane_no=nrm, clear_outer=True)
    bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=A - nrm * 0.004, plane_no=-nrm, clear_outer=True)
    me = bpy.data.meshes.new("seal"); bm.to_mesh(me); bm.free(); o = bpy.data.objects.new("Pantsir_helmet_seal", me); COLL[0].objects.link(o)
    me.materials.append(M["red"]); so = o.modifiers.new("sol", "SOLIDIFY"); so.thickness = 0.030; so.offset = -1.0; SEAL.append(o)
    bm = bmesh.new()                                                         # the 2 mm split groove, seen with the cap closed
    for v in vs: bm.verts.new((V(v) - HO) * 1.0012 + HO)
    bm.verts.ensure_lookup_table()
    for f_ in fs: bm.faces.new([bm.verts[i] for i in f_])
    bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=A + nrm * 0.0011, plane_no=nrm, clear_outer=True)
    bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=A - nrm * 0.0011, plane_no=-nrm, clear_outer=True)
    me = bpy.data.meshes.new("groove"); bm.to_mesh(me); bm.free(); o = bpy.data.objects.new("Pantsir_helmet_groove", me); COLL[0].objects.link(o)
    me.materials.append(M["dark"]); so = o.modifiers.new("sol", "SOLIDIFY"); so.thickness = 0.002; so.offset = 0.0; ACT_CLOSED.append(o)
    # the helmet's lock ring on the torso's rigid collar ring (fixed, graphite): a faceted flange collar -> helmet base
    lr = []; lf = []
    for z, w, F, Bk, cf, cb in ((1.5745, 0.163, 0.131, 0.126, 0.050, 0.062), (1.5815, 0.150, 0.127, 0.128, 0.046, 0.060), (1.5935, 0.121, 0.122, 0.129, 0.034, 0.054)):
        ring = [(w - cf, F), (w, F - cf), (w, -Bk + cb), (w - cb, -Bk), (-(w - cb), -Bk), (-w, -Bk + cb), (-w, F - cf), (-(w - cf), F)]
        lr.append(len(lf)); lf += [tuple(hp(x, z - HO.z, u)) for x, u in ring]
    lfs = [(a + j, a + (j + 1) % 8, b + (j + 1) % 8, b + j) for a, b in zip(lr, lr[1:]) for j in range(8)]
    o = mk(lf, lfs, "graphite", "helmet_lockring", 0.3, "helmet"); s_ = o.modifiers.new("sol", "SOLIDIFY"); s_.thickness = 0.012; bevel(o, 0.003, 2, 30)
    for pg in o.data.polygons: pg.use_smooth = False
    # the visor frame, the window panes, the louvres, the sensors - all on the moving cap
    def grow(poly, d): return [(x + math.copysign(d, x), h + (d if h > 0.002 else -d)) for x, h in poly]
    cw5 = grow(CWIN, 0.005)
    parts = [wrapped_sheet(CFRAME, cw5, fr_u, "helmet_frame", "graphite", FR_D, bev=0.003, xb=(-WRAP_X, 0.0, WRAP_X), hb=(WIN_B, WIN_T))]
    parts.append(wrapped_sheet(grow(CWIN, 0.0065), CWIN, lambda x, h: fr_u(x, 0.0, 0.003), "helmet_bezel", "wzr", 0.034, bev=0.0012,
                               xb=(-WRAP_X, 0.0, WRAP_X)))                  # the stepped window bezel (light metal), 3 mm under the frame
    def ub(x, h): return FR_U0 + 0.003 - min(abs(x), WRAP_X) * FR_V - max(0.0, abs(x) - WRAP_X) * WRAP_S - 0.45 * (h - 0.056)
    BROW = [(-0.106, 0.087), (0.106, 0.087), (0.122, 0.066), (0.122, 0.055), (-0.122, 0.055), (-0.122, 0.066)]
    parts.append(wrapped_sheet(BROW, None, ub, "helmet_brow", "graphite", 0.042, bev=0.0035, xb=(-WRAP_X, 0.0, WRAP_X)))   # the brow bar: the louvres stow in it
    xs_ = [-0.104 + 0.104 * k / 8 for k in range(17)]; pp = [fr_pn(x, 0.0745, uf=ub) for x in xs_]          # the red index line on the brow bar
    for (pa, na), (pb, nb) in zip(pp, pp[1:]):
        nn = (na + nb).normalized(); parts.append(bar(pa + na * 0.0008, pb + nb * 0.0008, 0.0034, 0.0016, "red", "helmet_redline", up=nn, part="helmet", r=0.0004))
    for s in (1, -1):                                                       # cheek modules: the peripheral cameras, angled 40 deg out
        a = math.radians(40 * s); nC = V((math.sin(a), -math.cos(a), 0.0)); c = hp(s * 0.121, -0.006, 0.112)
        q = Matrix.Rotation(a, 3, 'Z').to_quaternion()
        parts.append(rbox(c, (0.030, 0.050, 0.084), "graphite", "helmet_cheek", q, r=0.008, seg=1, part="helmet"))
        parts += h_port(0, 0, "pe", "peri" + ("L" if s > 0 else "R"), pn=(c + nC * 0.025 + V((0, 0, 0.004)), nC))
    cr = []; cf_ = []                                                       # the crown plate, 3 mm proud, on the cap
    for h, w, F, Bk, cf, cb, sb, kf in HSEC[-2:]:
        w, F, Bk = w + 0.003, F - 0.010, Bk - 0.006
        ring = [(0.0, F + kf), (w - cf, F), (w, F - cf), (w + sb, ((F - cf) + (-Bk + cb)) / 2), (w, -Bk + cb), (w - cb, -Bk), (0.0, -Bk - kf),
                (-(w - cb), -Bk), (-w, -Bk + cb), (-(w + sb), ((F - cf) + (-Bk + cb)) / 2), (-w, F - cf), (-(w - cf), F)]
        cr.append(len(cf_)); cf_ += [tuple(hp(x, h + 0.003, u)) for x, u in ring]
    cfs = [(cr[0] + j, cr[0] + (j + 1) % 12, cr[1] + (j + 1) % 12, cr[1] + j) for j in range(12)] + [tuple(range(cr[1], cr[1] + 12))]
    o = mk(cf_, cfs, "shell", "helmet_crown", 0.2, "helmet"); s_ = o.modifiers.new("sol", "SOLIDIFY"); s_.thickness = 0.004; bevel(o, 0.002, 2, 30)
    for pg in o.data.polygons: pg.use_smooth = False
    parts.append(o)
    gw = [(x + math.copysign(0.0025, x), h + (0.0025 if h > 0 else -0.0025)) for x, h in CWIN]                  # panes run 2.5 mm into the frame
    parts.append(wrapped_sheet(gw, None, lambda x, h: fr_u(x, 0.0, 0.0165), "helmet_pane_borazon", "boraz_glass", 0.005, bev=0, xb=(-WRAP_X, 0.0, WRAP_X)))
    parts.append(wrapped_sheet(gw, None, lambda x, h: fr_u(x, 0.0, 0.0220), "helmet_pane_photonic", "photonic", 0.003, bev=0, xb=(-WRAP_X, 0.0, WRAP_X)))
    for k in range(5):                                                       # closed: shingled downward over the window
        o = h_louvre(WIN_T + 0.0005 - k * 0.0136, 0.0168, "helmet_louvre%d" % k, k); LOUV_CLOSED.append(o); parts.append(o)
    for k in range(5):                                                       # open: stacked up behind the brow bar, one lip showing
        o = h_louvre(WIN_T + 0.0180 + k * 0.0018, 0.0225, "helmet_louvre_up%d" % k, k); LOUV_OPEN.append(o); parts.append(o)
    for s, t in ((1, "L"), (-1, "R")):
        parts += h_port(s * 0.0325, 0.0455, "st", "stereo" + t)              # stereo pair, 65 mm
        parts += h_port(s * 0.0835, -0.0505, "lamp", "lamp" + t)             # recessed lamps
    parts += h_port(-0.040, -0.0505, "th", "thermal") + h_port(0.040, -0.0505, "li", "lidar")
    # markings: red index tag on the crown, red line over the frame, red side index, stencils (П-2 chin, ШЛ-07 side)
    hb = BVHTree.FromPolygons([V(v) for v in vs], fs)
    def surf(o_, d_):
        h_ = hb.ray_cast(V(o_), V(d_).normalized(), 1.0)
        nn = h_[1].normalized(); return h_[0], (nn if nn.dot(V(d_)) < 0 else -nn)
    pt, nt_ = surf(hp(0.046, 0.128, 0.5), (0, 1, 0)); parts.append(rbox(pt + nt_ * 0.0037, (0.032, 0.0016, 0.0085), "red", "helmet_index", nquat(nt_), r=0.0005, seg=1, part="helmet"))
    for s in (1, -1):
        pt, nt_ = surf(hp(s * 0.5, -0.0575, -0.128), (-s, 0, 0))
        rbox(pt + nt_ * 0.0007, (0.0016, 0.012, 0.034), "red", "helmet_side_index", r=0.0005, seg=1, part="helmet")
        pt, nt_ = surf(hp(s * 0.5, -0.078, -0.080), (-s, 0, 0)); stencil("ШЛ-07", pt, nt_, up=(0, 0, 1), size=0.0105, m="stencil_dark", name="helmet_mark")
    for s in (1, -1):
        pt, nt_ = surf(hp(s * 0.5, -0.040, -0.030), (-s, 0, 0)); q_ = nquat(nt_)
        rbox(pt + nt_ * 0.0015, (0.070, 0.006, 0.050), "graphite", "helmet_sideplate", q_, r=0.0028, seg=2, part="helmet")
        rbox(pt + nt_ * 0.0045 + V((0, 0.012, 0.0)), (0.028, 0.002, 0.004), "red", "helmet_sideplate_mark", q_, r=0.0006, seg=1, part="helmet")
    pt, nt_ = surf(hp(0.0, -0.114, 0.5), (0, 1, 0)); stencil("П-2", pt, nt_, up=(0, 0, 1), size=0.011, m="stencil_red", name="helmet_mark_p2")
    pt, nt_ = surf(hp(-0.072, 0.128, 0.5), (0, 1, 0)); parts.append(stencil("ПАНЦИРЬ-2", pt, nt_, up=(0, 0, 1), size=0.0075, m="stencil_dark", name="helmet_mark_crown"))
    fl = bpy.data.lights.new("HelmFaceLight", 'POINT'); fl.energy = 1.2; fl.shadow_soft_size = 0.02; fl.color = (1.0, 0.86, 0.7)
    flo = bpy.data.objects.new("Pantsir_helmet_facelight", fl); COLL[0].objects.link(flo); flo.location = hp(0.0, -0.045, 0.085); parts.append(flo)
    OPENABLE.extend(parts)
    # the cap hinge (axis = HINGE): sunk into the fixed back shell - a faceted cover continuing the back plates over the axis,
    # two short ring motors built in flush at its ends; nothing above the helmet silhouette
    prof = [(-0.030, 0.006), (-0.012, 0.016), (0.006, 0.013), (0.016, 0.000), (0.012, -0.020), (-0.004, -0.034), (-0.030, -0.020)]   # (back, up) about the axis
    o = prism([(-(HINGE.y + dy), HINGE.z + dz) for dy, dz in prof], 0.0, 0.112, "shell2", "hinge_cover", part="helmet"); bevel(o, 0.003, 2, 30)
    for pg in o.data.polygons: pg.use_smooth = False
    for sx in (-1, 1):
        cyl(HINGE + V((sx * 0.046, 0, 0)), HINGE + V((sx * 0.0565, 0, 0)), 0.0125, "bear", "hinge_motor", seg=40, part="helmet")
        cyl(HINGE + V((sx * 0.0555, 0, 0)), HINGE + V((sx * 0.0570, 0, 0)), 0.0068, "hub", "hinge_hub", seg=32, part="helmet")
        for k in range(10):
            a = 2 * math.pi * k / 10; e = V((0, math.cos(a), math.sin(a))) * 0.0098
            cyl(HINGE + e + V((sx * 0.0555, 0, 0)), HINGE + e + V((sx * 0.0568, 0, 0)), 0.0011, "fin", "hinge_rib", seg=8, part="helmet")
    set_louvres(False)


def set_louvres(on):
    """the third state: the louvres retracted into the brow bar (window open) with the cap closed or open"""
    for o in LOUV_OPEN: o.hide_render = not on
    for o in LOUV_CLOSED: o.hide_render = on


def set_open(on):
    T = Matrix.Translation(HINGE) @ Matrix.Rotation(math.radians(-OPEN_DEG), 4, 'X') @ Matrix.Translation(-HINGE)
    for o in OPENABLE:
        if on: SAVED[o.name] = o.matrix_world.copy(); o.matrix_world = T @ o.matrix_world
        elif o.name in SAVED: o.matrix_world = SAVED.pop(o.name)
    for o in SEAL + ACT_OPEN: o.hide_render = not on
    for o in ACT_CLOSED: o.hide_render = on
    bpy.context.view_layer.update()


# ------------------------------------------------------------------ stage, figure head, renders
def stage():
    sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "blender"))); import render_util
    cam = render_util.setup_stage(); sc = bpy.context.scene
    w = sc.world; nt = w.node_tree; bg = next(n for n in nt.nodes if n.type == 'BACKGROUND')
    bg.inputs[0].default_value = (0.30, 0.31, 0.33, 1); bg.inputs[1].default_value = 0.55
    fl = bpy.data.objects.get("Floor")
    if fl:
        fm = bpy.data.materials.new("Pantsir_floor"); fm.use_nodes = True
        p = next(n for n in fm.node_tree.nodes if n.type == "BSDF_PRINCIPLED"); p.inputs["Base Color"].default_value = (0.18, 0.185, 0.19, 1); p.inputs["Roughness"].default_value = 0.8
        fl.data.materials.clear(); fl.data.materials.append(fm)
    l = bpy.data.lights.new("RimP", 'AREA'); l.energy = 700; l.size = 1.2; l.color = (0.75, 0.85, 1.0)
    lo = bpy.data.objects.new("RimP", l); sc.collection.objects.link(lo); lo.location = (-1.6, 2.5, 2.6)
    lo.rotation_mode = 'QUATERNION'; lo.rotation_quaternion = (V((0, 0, 1.1)) - V(lo.location)).to_track_quat('-Z', 'Y')
    sc.render.engine = 'CYCLES'; sc.cycles.device = 'CPU'; sc.cycles.samples = 12 if QUICK else 64; sc.cycles.use_denoising = True
    sc.view_settings.view_transform = 'AgX'; sc.view_settings.look = 'AgX - Base Contrast'; sc.view_settings.exposure = -1.0
    # rev. M: a sun key from the upper front side (35 deg to the side, 40 deg up) so neighbouring facets differ; Freestyle seams
    Lk = V((math.sin(math.radians(35)) * math.cos(math.radians(40)), -math.cos(math.radians(35)) * math.cos(math.radians(40)), math.sin(math.radians(40))))
    sl = bpy.data.lights.new("SunKey", 'SUN'); sl.energy = 2.2; sl.angle = math.radians(3)
    so = bpy.data.objects.new("SunKey", sl); sc.collection.objects.link(so); so.rotation_mode = 'QUATERNION'; so.rotation_quaternion = (-Lk).to_track_quat('-Z', 'Y')
    sc.render.use_freestyle = True; sc.render.line_thickness_mode = 'ABSOLUTE'; sc.render.line_thickness = 0.9
    # rev. P: a soft environment (dark ground, bright warm horizon, cool sky, large blotches) seen only by reflections and
    # bounce light - camera rays still see the old plain background
    wt = sc.world.node_tree; bg = next(n for n in wt.nodes if n.type == 'BACKGROUND'); wo = next(n for n in wt.nodes if n.type == 'OUTPUT_WORLD')
    tc = wt.nodes.new("ShaderNodeTexCoord"); sx = wt.nodes.new("ShaderNodeSeparateXYZ"); wt.links.new(tc.outputs["Generated"], sx.inputs[0])
    mr = wt.nodes.new("ShaderNodeMapRange"); mr.inputs["From Min"].default_value = -1.0; wt.links.new(sx.outputs["Z"], mr.inputs["Value"])
    cr = wt.nodes.new("ShaderNodeValToRGB"); wt.links.new(mr.outputs["Result"], cr.inputs["Fac"]); el = cr.color_ramp.elements
    el[0].position = 0.0; el[0].color = (0.03, 0.03, 0.035, 1); el[1].position = 1.0; el[1].color = (0.28, 0.32, 0.40, 1)
    for pos, col in ((0.46, (0.09, 0.09, 0.09, 1)), (0.52, (0.80, 0.76, 0.68, 1)), (0.62, (0.45, 0.47, 0.5, 1))): e = el.new(pos); e.color = col
    nz = wt.nodes.new("ShaderNodeTexNoise"); nz.inputs["Scale"].default_value = 2.5; wt.links.new(tc.outputs["Generated"], nz.inputs["Vector"])
    nm = wt.nodes.new("ShaderNodeMapRange"); nm.inputs["From Min"].default_value = 0.3; nm.inputs["From Max"].default_value = 0.7
    nm.inputs["To Min"].default_value = 0.55; nm.inputs["To Max"].default_value = 1.5; wt.links.new(nz.outputs["Fac"], nm.inputs["Value"])
    hs = wt.nodes.new("ShaderNodeHueSaturation"); wt.links.new(cr.outputs["Color"], hs.inputs["Color"]); wt.links.new(nm.outputs["Result"], hs.inputs["Value"])
    env = wt.nodes.new("ShaderNodeBackground"); env.inputs[1].default_value = 0.8; wt.links.new(hs.outputs["Color"], env.inputs[0])
    lp = wt.nodes.new("ShaderNodeLightPath"); ms = wt.nodes.new("ShaderNodeMixShader")
    wt.links.new(lp.outputs["Is Camera Ray"], ms.inputs[0]); wt.links.new(env.outputs[0], ms.inputs[1]); wt.links.new(bg.outputs[0], ms.inputs[2])
    wt.links.new(ms.outputs[0], wo.inputs[0])
    fs = bpy.context.view_layer.freestyle_settings; fs.crease_angle = math.radians(152)
    ls = fs.linesets[0] if len(fs.linesets) else fs.linesets.new("Seams")
    if ls.linestyle is None: ls.linestyle = bpy.data.linestyles.new("SeamLines")
    ls.linestyle.color = (0.2, 0.2, 0.21); sc.render.line_thickness = 0.6; ls.select_by_visibility = True; ls.select_crease = True; ls.select_border = True; ls.select_silhouette = True
    return cam


FIT = []


def pose_fit():
    """pose the person (armature) into the suit's rest pose: upper arm shoulder joint -> suit elbow axis, forearm -> wrist
    coupling, hand down the capsule; thigh -> knee axis, shin -> ankle. Joints of the figure stay where they are."""
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE'); arm.data.pose_position = 'POSE'; bpy.context.view_layer.update()
    Aw = arm.matrix_world; Ai = Aw.inverted(); tz = math.tan(math.radians(ELB_DEG))
    for s, side in ((1, "Left"), (-1, "Right")):
        J_, E_, W_, R1_, R2_, u1_, u2_ = arm_geo(s)
        tg = [(side + "Arm", E_), (side + "ForeArm", W_), (side + "Hand", W_ + u2_ * 0.08),
              (side + "UpLeg", V((s * LEG_C, -0.009, 0.541))), (side + "Leg", V((s * LEG_C, 0.004, 0.074)))]
        for bn, tw in tg:
            pb = arm.pose.bones.get(bn)
            if pb is None: continue
            bpy.context.view_layer.update()
            head = pb.head.copy(); dl = (Ai @ tw - head).normalized()
            M0 = pb.bone.matrix_local.to_3x3(); y0 = (pb.matrix.to_3x3() @ V((0, 1, 0))).normalized()
            q = y0.rotation_difference(dl); R3 = q.to_matrix() @ pb.matrix.to_3x3()
            pb.matrix = Matrix.Translation(head) @ R3.to_4x4()
    bpy.context.view_layer.update()


def fit_table():
    """clearance between the person's skin and the suit's inner wall (outer shell - wall per HEAVY_SUIT.md «Ревизия S»)"""
    body = bpy.data.objects.get("Astronavigator.body")
    if body is None: log.append("fit: no figure body"); return
    body.hide_viewport = False
    try: body.hide_set(False)
    except Exception: pass
    log.append("  body in view layer: %s" % (body.name in bpy.context.view_layer.objects))
    for m_ in body.modifiers:
        if m_.type == 'MASK': m_.show_viewport = False                                 # full skin, vertex order = the mesh's
    bpy.context.view_layer.update(); dg = bpy.context.evaluated_depsgraph_get(); dg.update(); be = body.evaluated_get(dg); me = be.to_mesh(); vs = [body.matrix_world @ v.co for v in me.vertices]; be.to_mesh_clear()
    gname = {g.index: g.name for g in body.vertex_groups}; VG = []; BONES_ = set(b.name for b in next(o for o in bpy.data.objects if o.type == 'ARMATURE').data.bones)                     # each vertex's dominant bone (vertex group)
    for v in body.data.vertices:
        if any(gname.get(g.group, "") in ("HelperGeometry", "JointCubes") and g.weight > 0.0 for g in v.groups): VG.append(""); continue
        gg = [g for g in v.groups if gname.get(g.group, "") in BONES_]
        best = max(gg, key=lambda g: g.weight, default=None); VG.append(gname.get(best.group, "") if best else "")
    if len(VG) != len(vs): VG = [""] * len(vs)
    log.append("  groups: " + ", ".join(sorted(set(VG))))
    def bvh(names):
        V_ = []; F_ = []
        for nm in names:
            o = bpy.data.objects.get("Pantsir_" + nm)
            if o is None: continue
            oe = o.evaluated_get(dg); m = oe.to_mesh(); b = len(V_)
            V_ += [o.matrix_world @ v.co for v in m.vertices]; F_ += [tuple(b + i for i in p.vertices) for p in m.polygons]; oe.to_mesh_clear()
        return BVHTree.FromPolygons(V_, F_) if V_ else None
    WALL = {"hut": 0.057, "pelvis": 0.057, "capsuleL": 0.020}
    zones = [("плечо (дельта)", ["deltoidL", "hut"], ("LeftArm",), 0.043),
             ("верх руки", ["upperarmL", "deltoidL"], ("LeftArm",), 0.043),
             ("локоть", ["elbowL", "upperarmL", "forearmL"], ("LeftArm", "LeftForeArm"), 0.043),
             ("предплечье/запястье", ["forearmL"], ("LeftForeArm",), 0.043),
             ("капсула кисти", ["capsuleL"], ("LeftHand",), 0.020),
             ("бедро", ["thighL", "pelvis"], ("LeftUpLeg",), 0.043),
             ("колено", ["kneeL", "thighL", "shinL"], ("LeftUpLeg", "LeftLeg"), 0.043),
             ("голень/лодыжка", ["shinL"], ("LeftLeg",), 0.043),
             ("торс", ["hut", "deltoidL", "deltoidR", "pelvis"], ("Spine", "Spine1", "Hips", "LeftShoulder", "RightShoulder", "Neck"), 0.057),
             ("таз", ["pelvis", "hut", "thighL", "thighR"], ("Hips", "LHipJoint", "RHipJoint"), 0.057)]
    arm_ = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    for bn in ("LeftArm", "LeftForeArm", "LeftHand", "LeftUpLeg", "LeftLeg"):
        pb = arm_.pose.bones.get(bn)
        if pb: log.append("  bone %s %s -> %s" % (bn, tuple(round(c, 3) for c in arm_.matrix_world @ pb.head), tuple(round(c, 3) for c in arm_.matrix_world @ pb.tail)))
    log.append("  body mods: %s parent=%s arms=%s" % ([(m.type, getattr(getattr(m, "object", None), "name", None), getattr(m, "show_render", None), getattr(m, "show_viewport", None), getattr(m, "use_vertex_groups", None)) for m in body.modifiers], body.parent.name if body.parent else None, [o.name for o in bpy.data.objects if o.type == 'ARMATURE']))
    for z in (0.25, 0.55, 0.75, 0.92, 1.16, 1.25):
        p_ = [v for v in vs if abs(v.z - z) < 0.006 and v.x > 0.03]
        if p_: log.append("  body z=%.2f x[%.3f,%.3f] y[%.3f,%.3f]" % (z, min(v.x for v in p_), max(v.x for v in p_), min(v.y for v in p_), max(v.y for v in p_)))
    log.append("FIT (зазор кожа -> внутренняя стенка, мм; стенка: конечности 43, торс 57, капсула 20):")
    arm_ = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    for nm, objs, grp, wall in zones:
        sel_i = [i for i, g in enumerate(VG) if any(g == k for k in grp)]
        pts = [vs[i] for i in sel_i]
        if objs[0] in SEGREG:
            mn = 1e9; out_ = 0; n_ = 0; worst = None
            for v in pts:
                cs = [(sec_clear(o_, v), WALL.get(o_, 0.043)) for o_ in objs if o_ in SEGREG]
                if cs[0][0] is None: continue                                    # the zone = its own shell's range
                cs = [(d, w) for d, w in cs if d is not None]
                c_ = max(d - w for d, w in cs); n_ += 1; out_ += max(d for d, w in cs) < 0
                if c_ < mn: mn = c_; worst = v
            log.append("  %-20s мин. зазор %6.1f мм  (точек %d, снаружи оболочки %d) худшая %s" % (nm, mn * 1000, n_, out_, tuple(round(c, 3) for c in worst) if worst else "-"))
            FIT.append((nm, mn * 1000, out_)); continue
        t = bvh(objs)
        if t is None or not pts: log.append("  %-20s нет данных" % nm); FIT.append((nm, None, 0)); continue
        mn = 1e9; out_ = 0
        for v in pts:
            loc, nrm, idx, d = t.find_nearest(v)
            if loc is None or abs(nrm.z) > 0.7: continue
            inside = (v - loc).dot(nrm) < 0
            c = (d - wall) if inside else -(d + wall)
            if not inside: out_ += 1
            mn = min(mn, c)
        log.append("  %-20s мин. зазор %6.1f мм  (точек %d, снаружи оболочки %d)" % (nm, mn * 1000, len(pts), out_)); FIT.append((nm, mn * 1000, out_))


def xray_fit(cam):
    """x-ray view: the person visible inside the left arm and leg (shells see-through)"""
    body = bpy.data.objects.get("Astronavigator.body")
    if body:
        body.hide_render = False; mq = bpy.data.materials.new("Pantsir_mannequin"); mq.use_nodes = True
        pq = next(n for n in mq.node_tree.nodes if n.type == "BSDF_PRINCIPLED"); pq.inputs["Base Color"].default_value = (0.30, 0.45, 0.65, 1); pq.inputs["Roughness"].default_value = 0.6
        body.data.materials.clear(); body.data.materials.append(mq)
        for sl in body.material_slots: sl.material = mq
    gm = bpy.data.materials.new("Pantsir_xray"); gm.use_nodes = True; nt = gm.node_tree; p = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Base Color"].default_value = (0.6, 0.75, 0.9, 1); p.inputs["Alpha"].default_value = 0.18; p.inputs["Roughness"].default_value = 0.4
    keys = ("thighL", "shinL", "kneeL", "upperarmL", "forearmL", "deltoidL", "elbowL", "ring_P3L", "ring_P5L", "capsuleL", "cuffL", "couplingL", "ring_p9L", "hut", "pelvis", "waist_P0", "armhole_P1L")
    for o in COLL[0].objects:
        b = o.name.split(".")[0][len("Pantsir_"):]
        if o.type != 'MESH': continue
        if b in keys:
            for i in range(len(o.data.materials)): o.data.materials[i] = gm
        elif b.endswith("L") and any(k in b for k in ("pl", "shell", "guard", "back", "olecranon", "fore_", "upper_", "deltoid", "calf", "thigh", "shin", "hand_", "thenar", "palm", "chest", "achilles")):
            o.hide_render = True
        elif b.startswith(("chest_", "pelvis_")): o.hide_render = True
    shoot(cam, "pantsir_%s_fit" % REV, (1.55, -1.55, 1.05), (0.24, 0.0, 0.92), lens=42, res=(1000, 1200))


def figure_head():
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    arm.scale = (1.046, 1.046, 1.046); arm.location = (0.0, 0.07, 0.0)
    for o in bpy.data.objects:
        if o.type != 'MESH' or o.name == "Floor" or o.name.startswith("Pantsir_"): continue
        o.hide_render = not (o.parent is arm and o.name not in ("SuitBody", "Astronavigator.body"))
    bpy.context.view_layer.update()


def shoot(cam, name, loc, tgt, lens=None, ortho=None, res=(1200, 1200)):
    sc = bpy.context.scene; sc.render.resolution_x, sc.render.resolution_y = (res[0] // 2, res[1] // 2) if QUICK else res
    cam.data.type = 'ORTHO' if ortho else 'PERSP'
    if ortho: cam.data.ortho_scale = ortho
    else: cam.data.lens = lens
    cam.location = loc; cam.rotation_mode = 'QUATERNION'; cam.rotation_quaternion = (V(tgt) - V(loc)).to_track_quat('-Z', 'Y')
    bpy.context.view_layer.update(); sc.render.filepath = os.path.join(OUT, name + ".png"); bpy.ops.render.render(write_still=True); log.append("rendered " + name)


def build():
    log.append("materials"); materials(); log.append("materials ok")
    c = bpy.data.collections.new("Pantsir"); bpy.context.scene.collection.children.link(c); COLL[0] = c
    hut = body_S(); helmet(); n = 0                               # rev. S: the body revision (legs(), torso(), arms(), door(), frame() kept, unused)
    bpy.context.view_layer.update()
    BOLTS.clear()                                                  # rev. R: 30th century - no visible fasteners
    for o in list(COLL[0].objects):
        b = o.name.split(".")[0]
        if o in PARTS.get("standoffs", []) or "post_head" in b or b.endswith("_post"):
            if "handle_post" not in b: o.hide_render = True
    log.append("geometry ok"); flush_bolts(); flush_ribs(); collect_shield(); log.append("shield %d" % len(SHIELD_SET))
    log.append("tiles: %d" % n)


def main():
    cam = stage(); build(); figure_head(); pose_fit(); fit_table(); set_open(False)
    sl_coll = bpy.data.collections.new("SeamLines")                                   # rev. N: lines only on the chest plates and hands
    for o in SEAM_OBJS: sl_coll.objects.link(o)
    ls = bpy.context.view_layer.freestyle_settings.linesets[0]
    ls.select_by_collection = True; ls.collection = sl_coll; ls.collection_negation = 'INCLUSIVE'
    log.append("seam objects: %d" % len(SEAM_OBJS))
    set_variant(4)
    for o in COLL[0].objects:                                                 # rev. S hand: borazon on the back and pinky edge only
        b = o.name.split(".")[0][len("Pantsir_"):]
        if b.startswith(("palm_plate", "thenar", "hand_Ac", "hand_K_thumb")) and o.type == 'MESH':
            for i in range(len(o.data.materials)): o.data.materials[i] = M["shell"] if i == 0 else M["frame"]
    env = os.environ.get; only = env("HELM_SHOTS", "")                     # rev. S: HELM_SHOTS=closed,louvres,side,open to iterate
    set_rad("hot")
    if env("FIT") == "1": xray_fit(cam); return
    if env("DUMP") == "1":
        for o in COLL[0].objects:
            if o.type == 'MESH' and any(k in o.name for k in ("rad_", "wing", "backunit", "louvre")):
                bb = [o.matrix_world @ V(c) for c in o.bound_box]
                log.append("%s hide=%s min=%s max=%s" % (o.name, o.hide_render, [round(min(p[i] for p in bb), 3) for i in range(3)], [round(max(p[i] for p in bb), 3) for i in range(3)]))
        return
    if env("BODY_WIP") == "1":
        shoot(cam, "pantsir_%s_body_wip_front" % REV, (0, -6.0, CAM_Z), (0, 0, CAM_Z), ortho=ORTHO_SCALE, res=(ORTHO_W, ORTHO_H))
        shoot(cam, "pantsir_%s_body_wip_side" % REV, (-6.0, 0, CAM_Z), (0, 0, CAM_Z), ortho=ORTHO_SCALE, res=(ORTHO_W, ORTHO_H))
        shoot(cam, "pantsir_%s_body_wip_back" % REV, (-2.5, 3.9, 1.5), (0, 0, 0.95), lens=80)
        return
    if env("ONLY_HELMET") != "1" and env("ONLY_RAD") != "1":
        shoot(cam, "pantsir_%s_34" % REV, (2.9, -3.5, 1.5), (0, 0, 0.95), lens=80)
        if env("ONLY_FRONT") == "1": return
        shoot(cam, "pantsir_%s_front" % REV, (0, -6.0, CAM_Z), (0, 0, CAM_Z), ortho=ORTHO_SCALE, res=(ORTHO_W, ORTHO_H))
        shoot(cam, "pantsir_%s_side" % REV, (-6.0, 0, CAM_Z), (0, 0, CAM_Z), ortho=ORTHO_SCALE, res=(ORTHO_W, ORTHO_H))
        shoot(cam, "pantsir_%s_back" % REV, (-2.5, 3.9, 1.5), (0, 0, 0.95), lens=80)
        shoot(cam, "pantsir_%s_leg" % REV, (0.95, -1.05, 0.62), (0.135, -0.01, 0.52), lens=60, res=(1000, 1200))
    if env("ONLY_HELMET") != "1":
        for st in ("hot", "cryo", "liquid"):
            set_rad(st); shoot(cam, "pantsir_%s_rad_%s" % (REV, st), (-1.25, 1.75, 1.85), (0, 0.29, 1.33), lens=70, res=(1200, 1200))
        set_rad("hot")
        if env("ONLY_RAD") == "1": return
    HT = V((0, -0.02, 1.712))                                               # rev. S: helmet close-ups (variant C)
    def hs(nm): return not only or nm in only.split(",")
    if hs("closed"): set_louvres(False); shoot(cam, "pantsir_%s_helmet_closed" % REV, (0.60, -0.98, 1.84), HT, lens=100)
    if hs("louvres"): set_louvres(True); shoot(cam, "pantsir_%s_helmet_louvres_open" % REV, (0.10, -1.16, 1.76), HT, lens=100)
    if hs("louvres34"): set_louvres(True); shoot(cam, "pantsir_%s_helmet_louvres_open34" % REV, (-0.62, -0.96, 1.80), HT, lens=100)
    if hs("side"): set_louvres(False); shoot(cam, "pantsir_%s_helmet_side" % REV, (1.16, -0.06, 1.74), (0, 0.0, 1.72), lens=100)
    if hs("open"):
        set_louvres(True); set_open(True)
        shoot(cam, "pantsir_%s_helmet_cap_open" % REV, (0.95, -0.80, 1.92), (0, 0.04, 1.77), lens=80)
        set_open(False); set_louvres(False)
    log.append("parts: " + ", ".join("%s=%d" % (k, len(v)) for k, v in PARTS.items()))


try:
    main()
except Exception:
    log.append(traceback.format_exc())
finally:
    open(LOG, "w", encoding="utf-8").write("\n".join(log) + "\n")
    bpy.ops.wm.quit_blender()
