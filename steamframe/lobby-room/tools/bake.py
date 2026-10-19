# Bakes the lighting of the lobby room into one texture per bake group, for VPX to show unlit.
#
#   Blender -b <lobby.blend> --python bake.py -- <output dir> [samples] [groups...]
#
# The objects of each bake group (custom property 'bake_group') are joined into one mesh, given a non overlapping 'Lightmap' UV
# layout, and baked with Cycles: diffuse and glossy, each as color x (direct + indirect) light, plus emission. The result, in
# linear scene values, is saved as <group>.exr; the joined scene is saved as baked.blend next to it, for the export step.
# The light passes are denoised with OpenImageDenoise (compositor), the color and emission passes need only a few samples.
import bpy, json, math, os, sys, time
import numpy as np

args = sys.argv[sys.argv.index("--") + 1:]
out = os.path.abspath(args[0])
samples = int(args[1]) if len(args) > 1 else 256
only = set(args[2:])
os.makedirs(out, exist_ok=True)

scene = bpy.context.scene
# Texture size of each group, for about 400 texels per meter on the large surfaces and more on the furniture seen up close, unless
# the scene lists its own; the groups that only emit light (a backdrop) skip the lighting passes
SIZES = json.loads(scene["bake_sizes"]) if "bake_sizes" in scene else {"floor": 4096, "walls": 4096, "ceiling": 2048, "feature": 2048, "furniture": 4096}
EMIT_ONLY = set(json.loads(scene["bake_emit_only"])) if "bake_emit_only" in scene else set()
HIDE_WHEN_LIGHTING = set(json.loads(scene["bake_hide_when_lighting"])) if "bake_hide_when_lighting" in scene else set()
scene.render.engine = "CYCLES"
prefs = bpy.context.preferences.addons["cycles"].preferences
prefs.compute_device_type = "METAL"
prefs.get_devices()
for d in prefs.devices:
    d.use = True
scene.cycles.device = "GPU"
scene.cycles.samples = samples
scene.render.bake.margin = 8
scene.render.bake.margin_type = "EXTEND"


def join_group(group):
    objs = [o for o in scene.objects if o.type == "MESH" and o.get("bake_group") == group]
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        # Shared meshes (props imported twice) must be single user to apply the transforms
        if o.data.users > 1:
            o.data = o.data.copy()
        # Same UV layer name everywhere, so that the join keeps one texture layer
        while len(o.data.uv_layers) > 1:
            o.data.uv_layers.remove(o.data.uv_layers[1])
        o.data.uv_layers[0].name = "UVMap"
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = obj.data.name = f"Lobby_{group}"
    obj["bake_group"] = group
    return obj


def unwrap(obj, size):
    uv = obj.data.uv_layers.new(name="Lightmap")
    obj.data.uv_layers.active = uv
    obj.data.uv_layers["UVMap"].active_render = True  # The materials keep sampling their textures with the original layout
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    margin = 12.0 / size
    bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=margin, area_weight=0.0, scale_to_bounds=False)
    bpy.ops.uv.pack_islands(rotate=True, margin=margin, shape_method="CONCAVE")
    bpy.ops.object.mode_set(mode="OBJECT")


def save_exr(data, path):
    size = data.shape[1], data.shape[0]
    img = bpy.data.images.new("save_tmp", *size, float_buffer=True)
    img.colorspace_settings.name = "Linear Rec.709"
    img.pixels.foreach_set(np.concatenate([data, np.ones(data.shape[:2] + (1,), np.float32)], axis=2).ravel())
    settings = scene.render.image_settings
    settings.file_format, settings.color_depth, settings.exr_codec = "OPEN_EXR", "16", "ZIP"
    img.save_render(path, scene=scene)
    bpy.data.images.remove(img)


def denoise(data):
    """OpenImageDenoise on a light pass, through the compositor of a scene that renders nothing else."""
    tmp_in, tmp_out = os.path.join(out, "_noisy.exr"), os.path.join(out, "_denoised.exr")
    save_exr(data, tmp_in)
    img = bpy.data.images.load(tmp_in)
    sc = bpy.data.scenes.get("Denoise") or bpy.data.scenes.new("Denoise")
    tree = bpy.data.node_groups.new("DenoiseTree", "CompositorNodeTree")
    sc.compositing_node_group = tree
    n_img = tree.nodes.new("CompositorNodeImage")
    n_img.image = img
    n_dn = tree.nodes.new("CompositorNodeDenoise")
    n_out = tree.nodes.new("NodeGroupOutput")
    tree.interface.new_socket("Image", in_out="OUTPUT", socket_type="NodeSocketColor")
    tree.links.new(n_img.outputs["Image"], n_dn.inputs["Image"])
    tree.links.new(n_dn.outputs["Image"], n_out.inputs[0])
    if sc.camera is None:
        cam = bpy.data.objects.new("DenoiseCamera", bpy.data.cameras.new("DenoiseCamera"))
        sc.collection.objects.link(cam)
        sc.camera = cam
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x, sc.render.resolution_y = img.size
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format, sc.render.image_settings.color_depth = "OPEN_EXR", "32"
    sc.view_settings.view_transform = "Standard"
    sc.render.filepath = tmp_out
    bpy.ops.render.render(write_still=True, scene=sc.name)
    result = bpy.data.images.load(tmp_out)
    px = np.empty(result.size[0] * result.size[1] * 4, dtype=np.float32)
    result.pixels.foreach_get(px)
    for i in (img, result):
        bpy.data.images.remove(i)
    bpy.data.node_groups.remove(tree)
    for f in (tmp_in, tmp_out):
        os.remove(f)
    return np.maximum(px.reshape(data.shape[0], data.shape[1], 4)[..., :3], 0.0)


def bake_pass(obj, image, bake_type, pass_filter, pass_samples):
    scene.cycles.samples = pass_samples
    for mat in obj.data.materials:
        nodes = mat.node_tree.nodes
        node = nodes.get("BakeTarget") or nodes.new("ShaderNodeTexImage")
        node.name = "BakeTarget"
        node.image = image
        nodes.active = node
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.bake(type=bake_type, pass_filter=pass_filter, margin=8, use_clear=True)
    px = np.empty(image.size[0] * image.size[1] * 4, dtype=np.float32)
    image.pixels.foreach_get(px)
    return px.reshape(image.size[1], image.size[0], 4)[..., :3]


groups = [g for g in SIZES if not only or g in only]
objects = {g: join_group(g) for g in SIZES}
for g in SIZES:
    unwrap(objects[g], SIZES[g])
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out, "baked.blend"))

for g in groups:
    obj, size = objects[g], SIZES[g]
    image = bpy.data.images.new(f"bake_{g}", size, size, float_buffer=True)
    image.colorspace_settings.name = "Linear Rec.709"
    t0 = time.time()
    for other in HIDE_WHEN_LIGHTING:
        objects[other].hide_render = g not in EMIT_ONLY and other != g
    emit = bake_pass(obj, image, "EMIT", set(), 4)
    if g in EMIT_ONLY:
        result = emit
    else:
        diffuse = bake_pass(obj, image, "DIFFUSE", {"COLOR"}, 4) * denoise(bake_pass(obj, image, "DIFFUSE", {"DIRECT", "INDIRECT"}, samples))
        glossy = bake_pass(obj, image, "GLOSSY", {"COLOR"}, 4) * denoise(bake_pass(obj, image, "GLOSSY", {"DIRECT", "INDIRECT"}, max(samples // 2, 16)))
        result = diffuse + glossy + emit
    save_exr(result, os.path.join(out, f"{g}.exr"))
    bpy.data.images.remove(image)
    lum = (result @ np.array([0.2126, 0.7152, 0.0722], np.float32)).ravel()
    print(f"BAKE {g} {size}px {time.time() - t0:.0f}s luminance p50 {np.percentile(lum, 50):.3f} p99 {np.percentile(lum, 99):.3f} p99.9 {np.percentile(lum, 99.9):.3f}")
