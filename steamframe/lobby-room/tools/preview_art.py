# Saves every generated artwork as PNG to look at it: Blender -b --python preview_art.py -- <output dir>
import bpy, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import gameroom_art as art

out = sys.argv[sys.argv.index("--") + 1]
os.makedirs(out, exist_ok=True)

def save(name, rgb):
    h, w = rgb.shape[:2]
    img = bpy.data.images.new(name, w, h)
    rgba = np.concatenate([np.clip(rgb, 0, 1), np.ones((h, w, 1))], axis=2)[::-1]
    img.pixels.foreach_set(rgba.astype(np.float32).ravel())
    img.filepath_raw = os.path.join(out, name + ".png")
    img.file_format = "PNG"
    img.save()

for i, f in enumerate(art.SCREENS):
    save(f"screen{i}", f())
for i, m in enumerate(art.MARQUEES):
    save(f"marquee{i}", art.marquee(*m))
for i, s in enumerate(art.SIDE_ARTS):
    save(f"side{i}", art.side_art(*s))
save("playfield", art.playfield())
save("backglass", art.backglass())
save("dmd", art.dot_matrix())
save("pinball_side", art.pinball_side())
print("ART ok")
