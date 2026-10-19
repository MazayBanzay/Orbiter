# Image frames -> MP4 (H.264) with Blender's own FFmpeg (no ffmpeg needed on the system).
# blender --background --python frames_to_video.py -- <frames folder> <out.mp4> [fps]
import bpy, os, sys
args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
src, out = args[0], args[1]; fps = int(args[2]) if len(args) > 2 else 24
files = sorted(f for f in os.listdir(src) if f.lower().endswith(".png"))
sc = bpy.context.scene
if not sc.sequence_editor: sc.sequence_editor_create()
seqs = sc.sequence_editor.strips if hasattr(sc.sequence_editor, "strips") else sc.sequence_editor.sequences
st = seqs.new_image("frames", os.path.join(src, files[0]), 1, 1)
for f in files[1:]: st.elements.append(f)
st.frame_final_duration = len(files)
import struct
with open(os.path.join(src, files[0]), "rb") as fh: fh.read(16); w, h = struct.unpack(">II", fh.read(8))
sc.render.resolution_x, sc.render.resolution_y, sc.render.resolution_percentage = w - w % 2, h - h % 2, 100
sc.frame_start, sc.frame_end = 1, len(files); sc.render.fps = fps
sc.render.image_settings.file_format = 'FFMPEG'; sc.render.ffmpeg.format = 'MPEG4'; sc.render.ffmpeg.codec = 'H264'
sc.render.ffmpeg.constant_rate_factor = 'HIGH'; sc.render.filepath = out
bpy.ops.render.render(animation=True)
