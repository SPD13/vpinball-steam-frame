# Preview renders of the cabinet built by build_cabinet.py --blend, in a plain studio with a stand-in playfield (preview only).
#   Blender -b <work>/cabinet.blend --python render_cabinet.py -- <out dir> <samples> [variant: spring|button[+dmd|full]] [views...]
# Views: player, threequarter, side, back, plunger, door, backbox, low
import bpy, math, os, sys
from mathutils import Vector

sys.path.insert(0, os.path.dirname(__file__))
import dims as D

args = sys.argv[sys.argv.index("--") + 1:]
out, samples = os.path.abspath(args[0]), int(args[1])
variant = args[2] if len(args) > 2 else "spring"
variant, display = (variant.split("+") + ["dmd"])[:2]  # Launcher, speaker panel
views = args[3:] or ["player", "threequarter", "side", "back", "plunger"]
os.makedirs(out, exist_ok=True)
scene = bpy.context.scene

for o in scene.objects:
    v = o.get("vpx_variant")
    if v is not None:
        o.hide_render = v != variant
    d = o.get("vpx_display")
    if d is not None:
        o.hide_render = d != display

# Stand-in playfield (the table's in the application): a dark quad with a few light blobs, on the nominal slope
mat = bpy.data.materials.new("StandInPlayfield")
mat.use_nodes = True
b = mat.node_tree.nodes["Principled BSDF"]
b.inputs["Base Color"].default_value = (0.02, 0.05, 0.16, 1)
b.inputs["Roughness"].default_value = 0.3
s = math.radians(D.SLOPE)
pf = [(-D.PF_W / 2, 0, D.PF_Z), (D.PF_W / 2, 0, D.PF_Z), (D.PF_W / 2, D.PF_D * math.cos(s), D.PF_Z + D.PF_D * math.sin(s)), (-D.PF_W / 2, D.PF_D * math.cos(s), D.PF_Z + D.PF_D * math.sin(s))]
me = bpy.data.meshes.new("PF")
me.from_pydata(pf, [], [(0, 1, 2, 3)])
pfo = bpy.data.objects.new("PF", me)
pfo.data.materials.append(mat)
scene.collection.objects.link(pfo)

# Studio: grey floor, soft key and fill lights, dim gradient world
me = bpy.data.meshes.new("Floor")
me.from_pydata([(-6, -6, 0), (6, -6, 0), (6, 6, 0), (-6, 6, 0)], [], [(0, 1, 2, 3)])
fl = bpy.data.objects.new("Floor", me)
fm = bpy.data.materials.new("Floor")
fm.use_nodes = True
fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.18, 0.18, 0.2, 1)
fm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.6
fl.data.materials.append(fm)
scene.collection.objects.link(fl)
world = bpy.data.worlds.new("W")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.12, 0.13, 0.16, 1)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6
scene.world = world

def area(name, loc, target, energy, size):
    l = bpy.data.lights.new(name, "AREA")
    l.energy, l.size = energy, size
    o = bpy.data.objects.new(name, l)
    o.location = loc
    o.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(o)

area("Key", (-1.6, -1.8, 2.6), (0, 0.5, 1.0), 700, 1.5)
area("Fill", (2.0, -1.2, 1.8), (0, 0.5, 1.0), 260, 2.0)
area("Rim", (0.5, 3.2, 2.8), (0, 0.8, 1.2), 400, 1.5)
area("Top", (0, 0.6, 3.2), (0, 0.6, 1.0), 200, 2.5)

scene.render.engine = "CYCLES"
scene.cycles.device = "GPU"
prefs = bpy.context.preferences.addons["cycles"].preferences
prefs.compute_device_type = "METAL"
prefs.get_devices()
for d in prefs.devices:
    d.use = True
scene.cycles.samples = samples
scene.cycles.use_denoising = True
scene.view_settings.view_transform = "AgX"
scene.render.resolution_x, scene.render.resolution_y = 1400, 1000

VIEWS = {
    # name: (eye, target, lens)
    "player": ((0.0, -0.55, 1.62), (0.0, 0.6, 1.0), 20),
    "threequarter": ((-1.7, -1.9, 1.55), (0.0, 0.55, 0.95), 28),
    "side": ((2.6, 0.55, 1.1), (0.0, 0.55, 0.95), 24),
    "back": ((1.4, 2.9, 1.7), (0.0, 0.8, 1.1), 26),
    "plunger": ((0.45, -0.45, 0.95), (D.PLUNGER_X - 0.02, D.Y_FRONT, D.PLUNGER_Z), 45),
    "door": ((-0.25, -0.75, 0.85), (0.0, D.Y_FRONT, 0.72), 35),
    "backbox": ((0.0, -0.2, 1.55), (0.0, 1.15, 1.45), 28),
    "low": ((-0.9, -1.1, 0.35), (0.0, 0.3, 0.45), 24),
}
cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
scene.collection.objects.link(cam)
scene.camera = cam
for v in views:
    eye, target, lens = VIEWS[v]
    cam.location = eye
    cam.rotation_euler = (Vector(target) - Vector(eye)).to_track_quat("-Z", "Y").to_euler()
    cam.data.lens = lens
    scene.render.filepath = os.path.join(out, f"{v}-{variant}{'' if display == 'dmd' else '-' + display}.png")
    bpy.ops.render.render(write_still=True)
    print("RENDER", scene.render.filepath)
