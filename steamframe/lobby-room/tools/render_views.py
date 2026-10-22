# Renders preview images of the lobby .blend from the player's place (eye height 1.65 m), toward several directions.
#
#   Blender -b <lobby.blend> --python render_views.py -- <output dir> [samples] [views...]
#
# Views: front, left, right, back (yaw around the player), and corner (from the back left corner, a whole view of the room).
import bpy, math, os, sys

args = sys.argv[sys.argv.index("--") + 1:]
out = args[0]
samples = int(args[1]) if len(args) > 1 else 64
views = args[2:] or ["front", "left", "right", "back", "corner"]
scene = bpy.context.scene
scene.cycles.samples = samples
prefs = bpy.context.preferences.addons["cycles"].preferences
prefs.compute_device_type = "METAL"
prefs.get_devices()
for d in prefs.devices:
    d.use = True
scene.cycles.device = "GPU"
cam = scene.camera
yaws = {"front": 0, "left": 60, "right": -60, "back": 180}
for view in views:
    if view == "corner":
        cam.location = (-3.6, -2.6, 1.75)
        cam.rotation_euler = (math.radians(84), 0, math.radians(-38))
    else:
        cam.location = (0, 0, 1.65)
        cam.rotation_euler = (math.radians(86), 0, math.radians(yaws[view]))
    scene.render.filepath = os.path.join(out, f"view_{view}.png")
    bpy.ops.render.render(write_still=True)
    print("RENDER", scene.render.filepath)
