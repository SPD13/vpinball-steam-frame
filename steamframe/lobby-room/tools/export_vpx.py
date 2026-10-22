# Writes the baked lobby room as a VPX table: one unlit primitive per bake group, with its baked texture.
#
#   Blender -b <bake dir>/baked.blend --python export_vpx.py -- <bake dir> <base table expanded dir> <output .vpx> [scale] [name]
#
# <base table expanded dir> is `vpxtool extract` of the application's blankTable.vpx: its table settings are kept, its parts and
# images are replaced by the room. The baked textures hold the linear scene values divided by `scale` (default 4), so that the
# bright parts of the room fit in 8 bits; the table's exposure is set to `scale` to undo it, with the AgX tone mapper that the
# Blender previews used.
import bpy, json, os, shutil, subprocess, sys
import numpy as np

args = sys.argv[sys.argv.index("--") + 1:]
bake_dir, base_dir, out_vpx = (os.path.abspath(a) for a in args[:3])
scale = float(args[3]) if len(args) > 3 else 4.0
room_name = args[4] if len(args) > 4 else "Modern room"
VPXTOOL = os.path.expanduser("~/.local/bin/vpxtool")

# The player stands at the origin of the Blender scene. In VPX, the lobby places the player at the middle of the bottom edge
# of the table (see BuildLobby in src/steamframe/Launcher.cpp), in the room space where z = 0 is the floor.
MTOVPU = 50.0 / (0.0254 * 1.0625)
gamedata = json.load(open(os.path.join(base_dir, "gamedata.json")))
ORIGIN_X = 0.5 * (gamedata["left"] + gamedata["right"])
ORIGIN_Y = gamedata["bottom"]

work = os.path.join(bake_dir, "vpx")
shutil.rmtree(work, ignore_errors=True)
shutil.copytree(base_dir, work, ignore=shutil.ignore_patterns("gameitems", "images", "sounds"))
for sub in ("gameitems", "images", "sounds"):
    os.makedirs(os.path.join(work, sub))

scene = bpy.context.scene
scene.view_settings.view_transform = "Standard"
scene.view_settings.look = "None"
scene.view_settings.exposure = 0.0
scene.view_settings.gamma = 1.0


def write_obj(obj, path):
    """Wavefront OBJ the way VPX exports its meshes (VP units, Z negated): Blender (x, y, z) in meters becomes
    VPX (ORIGIN_X + x, ORIGIN_Y - y, z), Y toward the player, so the OBJ holds (X, Y, -Z), which is a rotation of the Blender
    axes and keeps the faces' winding."""
    mesh = obj.data
    mesh.calc_loop_triangles()
    uv = mesh.uv_layers["Lightmap"].data
    normals = mesh.corner_normals
    lines = [f"# Lobby room, bake group {obj['bake_group']}", f"o {obj.name}"]
    verts, index = [], {}
    faces = []
    for tri in mesh.loop_triangles:
        face = []
        for loop in tri.loops:
            co = mesh.vertices[mesh.loops[loop].vertex_index].co
            n = normals[loop].vector
            u, v = uv[loop].uv
            key = (round(co.x, 5), round(co.y, 5), round(co.z, 5), round(n.x, 3), round(n.y, 3), round(n.z, 3), round(u, 6), round(v, 6))
            if key not in index:
                index[key] = len(verts) + 1
                verts.append(key)
            face.append(index[key])
        faces.append(face)
    for x, y, z, *_ in verts:
        lines.append(f"v {ORIGIN_X + x * MTOVPU:.4f} {ORIGIN_Y - y * MTOVPU:.4f} {-z * MTOVPU:.4f}")
    for *_, u, v in verts:
        lines.append(f"vt {u:.6f} {v:.6f}")
    for _, _, _, nx, ny, nz, _, _ in verts:
        lines.append(f"vn {nx:.4f} {-ny:.4f} {-nz:.4f}")
    for a, b, c in faces:
        lines.append(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return len(verts), len(faces)


def write_texture(group, path):
    img = bpy.data.images.load(os.path.join(bake_dir, f"{group}.exr"))
    px = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    px = px.reshape(-1, 4)
    # Lights brighter than what 8 bits hold keep their hue (a pink neon stays pink instead of turning white)
    rgb = np.maximum(px[:, :3] / scale, 0.0)
    peak = rgb.max(axis=1, keepdims=True)
    px[:, :3] = np.where(peak > 1.0, rgb / np.maximum(peak, 1e-6), rgb)
    px[:, 3] = 1.0
    out = bpy.data.images.new(f"tex_{group}", *img.size, float_buffer=True)
    out.colorspace_settings.name = "Linear Rec.709"
    out.pixels.foreach_set(px.ravel())
    settings = scene.render.image_settings
    settings.file_format, settings.color_mode, settings.quality = "WEBP", "RGB", 92
    out.save_render(path, scene=scene)  # Standard view: linear to sRGB


groups = sorted(o["bake_group"] for o in scene.objects if o.type == "MESH" and o.name.startswith("Lobby_"))
group_json = json.load(open(os.path.join(base_dir, "gameitems", "PartGroup.VRRoom.json")))
group_json["PartGroup"].update(name="LobbyRoom", space_reference="room", player_mode_visibility_mask=65535, is_timer_enabled=False)
json.dump(group_json, open(os.path.join(work, "gameitems", "PartGroup.LobbyRoom.json"), "w"), indent=2)
items = [{"file_name": "PartGroup.LobbyRoom.json", "is_locked": True, "editor_layer_visibility": True}]

prim_base = json.load(open(os.path.join(base_dir, "gameitems", "Primitive.VRGround.json")))["Primitive"]
images = []
for group in groups:
    obj = scene.objects[f"Lobby_{group}"]
    name = f"Lobby{group.capitalize()}"
    nv, nf = write_obj(obj, os.path.join(work, "gameitems", f"Primitive.{name}.obj"))
    write_texture(group, os.path.join(work, "images", f"{name}.webp"))
    images.append({"name": name, "path": f"{name}.webp", "alpha_test_value": -1.0, "is_opaque": True})
    prim = dict(prim_base)
    prim.update(name=name, image=name, material="LobbyUnlit", mesh_file_name=f"{name}.obj", part_group_name="LobbyRoom",
        position={"x": 0.0, "y": 0.0, "z": 0.0}, size={"x": 1.0, "y": 1.0, "z": 1.0}, rot_and_tra=[0.0] * 9,
        is_visible=True, is_collidable=False, is_toy=True, use_3d_mesh=True, static_rendering=True,
        disable_lighting_top=1.0, disable_lighting_below=1.0, is_reflection_enabled=False, backfaces_enabled=False,
        light_map="", reflection_probe="", refraction_probe="", normal_map="", color="#ffffff", alpha=100.0)
    json.dump({"Primitive": prim}, open(os.path.join(work, "gameitems", f"Primitive.{name}.json"), "w"), indent=2)
    items.append({"file_name": f"Primitive.{name}.json", "is_locked": True, "editor_layer_visibility": True})
    print(f"EXPORT {name}: {nv} vertices, {nf} triangles")
# Outside of VR, the near clipping plane is derived from the corners of the bounding box of each part: with the room made of a few
# large parts, it would end up meters away and clip the floor in front of the player. A tiny quad hidden 1 cm under the floor, half a
# meter in front of the player, brings it close.
marker = "LobbyNearPlaneMarker"
with open(os.path.join(work, "gameitems", f"Primitive.{marker}.obj"), "w") as f:
    f.write(f"o {marker}\n")
    for dx, dy in ((-0.01, 0.49), (0.01, 0.49), (-0.01, 0.51), (0.01, 0.51)):
        f.write(f"v {ORIGIN_X + dx * MTOVPU:.4f} {ORIGIN_Y - dy * MTOVPU:.4f} {0.01 * MTOVPU:.4f}\n")
    f.write("vt 0 0\nvt 1 0\nvt 0 1\nvt 1 1\nvn 0 0 -1\nf 1/1/1 2/2/1 4/4/1\nf 1/1/1 4/4/1 3/3/1\n")
prim = dict(prim, name=marker, mesh_file_name=f"{marker}.obj", image="LobbyFloor")
json.dump({"Primitive": prim}, open(os.path.join(work, "gameitems", f"Primitive.{marker}.json"), "w"), indent=2)
items.append({"file_name": f"Primitive.{marker}.json", "is_locked": True, "editor_layer_visibility": True})
json.dump(items, open(os.path.join(work, "gameitems.json"), "w"), indent=2)
json.dump(images, open(os.path.join(work, "images.json"), "w"), indent=2)
json.dump([], open(os.path.join(work, "sounds.json"), "w"))
json.dump([], open(os.path.join(work, "collections.json"), "w"))

materials = json.load(open(os.path.join(base_dir, "materials.json")))
unlit = dict(next(m for m in materials if m["name"] == "Ground"))
unlit.update(name="LobbyUnlit", base_color="#ffffff", glossy_color="#000000", clearcoat_color="#000000", roughness=1.0, wrap_lighting=0.0)
json.dump([unlit], open(os.path.join(work, "materials.json"), "w"), indent=2)
if os.path.exists(os.path.join(work, "materials-old.json")):
    os.remove(os.path.join(work, "materials-old.json"))

with open(os.path.join(work, "script.vbs"), "w", newline="\r\n") as f:
    f.write("Option Explicit\n")
# The images of the base table are gone: no playfield, ball or environment image (the room is unlit)
for key in [k for k in gamedata if "image" in k]:
    if isinstance(gamedata[key], str):
        gamedata[key] = ""
gamedata.update(name="Lobby", tone_mapper="agx", exposure=scale, use_ao=0, use_ssr=0)
json.dump(gamedata, open(os.path.join(work, "gamedata.json"), "w"), indent=2)
info = json.load(open(os.path.join(work, "info.json")))
info.update(table_name=f"Visual Pinball Lobby - {room_name}", author_name="Visual Pinball Steam Frame Edition",
    table_description="The room of the lobby, baked in Blender from CC0 assets (see steamframe/lobby-room/CREDITS.md).", table_rules="", table_blurb="")
json.dump(info, open(os.path.join(work, "info.json"), "w"), indent=2)

if os.path.exists(out_vpx):
    os.remove(out_vpx)
subprocess.run([VPXTOOL, "assemble", work, out_vpx], check=True)
print("VPX", out_vpx, os.path.getsize(out_vpx) // 1024, "KB")
