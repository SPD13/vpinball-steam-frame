# Helpers to build a lobby room in Blender: materials from Poly Haven textures or generated images, simple geometry with UVs in
# meters, props imported from glTF, and the render settings of the previews. Every mesh gets a 'bake_group' custom property: the
# objects of a group become one VPX primitive with one baked texture (see bake.py).
import bpy, bmesh, math, os
import numpy as np
from mathutils import Vector, Matrix

DL = ""  # Downloads folder, set by init()


def init(downloads):
    global DL
    DL = os.path.abspath(downloads)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    return bpy.context.scene


# ---------------------------------------------------------------------------------------------------------------------------
# Materials

def tex_node(nodes, path, colorspace="sRGB"):
    node = nodes.new("ShaderNodeTexImage")
    node.image = bpy.data.images.load(path, check_existing=True)
    node.image.colorspace_settings.name = colorspace
    return node

def pbr_material(name, folder, scale=(1.0, 1.0), tint=None, roughness_mul=1.0, normal_strength=0.6):
    """Principled material from a Poly Haven texture folder, tiled on the UVs in meters (`scale` = size of a tile)."""
    files = os.listdir(os.path.join(DL, folder))
    find = lambda key: next((os.path.join(DL, folder, f) for f in files if key in f), None)
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    coord = nodes.new("ShaderNodeTexCoord")
    mapping = nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (1.0 / scale[0], 1.0 / scale[1], 1.0)
    links.new(coord.outputs["UV"], mapping.inputs["Vector"])
    diff = tex_node(nodes, find("_diff_") or find("_coll1_"))
    links.new(mapping.outputs["Vector"], diff.inputs["Vector"])
    color = diff.outputs["Color"]
    if tint:
        mix = nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        mix.blend_type = "MULTIPLY"
        mix.inputs["Factor"].default_value = 1.0
        mix.inputs["B"].default_value = (*tint, 1.0)
        links.new(color, mix.inputs["A"])
        color = mix.outputs["Result"]
    links.new(color, bsdf.inputs["Base Color"])
    rough_path = find("_rough_")
    if rough_path:
        rough = tex_node(nodes, rough_path, "Non-Color")
        links.new(mapping.outputs["Vector"], rough.inputs["Vector"])
        mul = nodes.new("ShaderNodeMath")
        mul.operation = "MULTIPLY"
        mul.inputs[1].default_value = roughness_mul
        links.new(rough.outputs["Color"], mul.inputs[0])
        links.new(mul.outputs["Value"], bsdf.inputs["Roughness"])
    nor_path = find("_nor_gl_")
    if nor_path:
        nor = tex_node(nodes, nor_path, "Non-Color")
        links.new(mapping.outputs["Vector"], nor.inputs["Vector"])
        nmap = nodes.new("ShaderNodeNormalMap")
        nmap.inputs["Strength"].default_value = normal_strength
        links.new(nor.outputs["Color"], nmap.inputs["Color"])
        links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
    return mat

def flat_material(name, color, roughness=0.6, metallic=0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    return mat

def emissive_material(name, color, strength):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0, 0, 0, 1.0)
    bsdf.inputs["Emission Color"].default_value = (*color, 1.0)
    bsdf.inputs["Emission Strength"].default_value = strength
    return mat

def array_image(name, rgb):
    """A packed Blender image from an RGB array (rows from the top, sRGB values)."""
    h, w = rgb.shape[:2]
    img = bpy.data.images.new(name, w, h)
    rgba = np.concatenate([np.clip(rgb, 0, 1), np.ones((h, w, 1))], axis=2)[::-1]
    img.pixels.foreach_set(rgba.astype(np.float32).ravel())
    img.pack()
    return img

def image_material(name, rgb, emission=0.0, roughness=0.5):
    """A material showing a generated image on the 0..1 UVs, as its color or, with `emission`, as a light (a screen, a backglass)."""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    tex = nodes.new("ShaderNodeTexImage")
    tex.image = array_image(name, rgb)
    tex.interpolation = "Closest" if emission else "Linear"
    bsdf.inputs["Roughness"].default_value = roughness
    if emission:
        bsdf.inputs["Base Color"].default_value = (0, 0, 0, 1)
        links.new(tex.outputs["Color"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = emission
    else:
        links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


# ---------------------------------------------------------------------------------------------------------------------------
# Geometry

def new_object(name, bm, material, group):
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    for m in (material if isinstance(material, (list, tuple)) else [material]):
        mesh.materials.append(m)
    obj["bake_group"] = group
    return obj

def box_uv(bm, uv_scale=1.0):
    """UVs in meters by projecting each face on the axis plane closest to it, for the tiled materials."""
    bm.normal_update()
    uv = bm.loops.layers.uv.verify()
    for face in bm.faces:
        n = face.normal
        axis = max(range(3), key=lambda i: abs(n[i]))
        for loop in face.loops:
            p = loop.vert.co
            if axis == 0:
                u, v = (p.y if n.x > 0 else -p.y), p.z
            elif axis == 1:
                u, v = (-p.x if n.y > 0 else p.x), p.z
            else:
                u, v = p.x, (p.y if n.z > 0 else -p.y)
            loop[uv].uv = (u / uv_scale, v / uv_scale)

def face_toward(bm, inside):
    """Turns the faces toward a point (the inside of the room: VPX culls back faces)."""
    bm.normal_update()
    flip = [f for f in bm.faces if f.normal.dot(Vector(inside) - f.calc_center_median()) < 0]
    bmesh.ops.reverse_faces(bm, faces=flip)

def box(name, lo, hi, material, group, uv_scale=1.0, skip=()):
    """An axis aligned box from corner `lo` to corner `hi`. `skip` lists faces never seen, by normal: '-z', '+x'..."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    size = Vector(hi) - Vector(lo)
    center = (Vector(hi) + Vector(lo)) / 2
    bmesh.ops.transform(bm, matrix=Matrix.Translation(center) @ Matrix.Diagonal((*size, 1.0)), verts=bm.verts)
    bm.normal_update()
    axes = {"x": 0, "y": 1, "z": 2}
    dead = [f for f in bm.faces for s in skip if f.normal[axes[s[1]]] * (1 if s[0] == "+" else -1) > 0.9]
    bmesh.ops.delete(bm, geom=dead, context="FACES")
    box_uv(bm, uv_scale)
    return new_object(name, bm, material, group)

def quad(name, corners, material, group, uv_scale=1.0, toward=None, uv01=False):
    """A quad from 4 corners, counter clockwise seen from the side it faces, or turned toward a point. With `uv01`, the UVs span
    0..1 from the first corner (u along the first edge, v along the last one), for images."""
    bm = bmesh.new()
    verts = [bm.verts.new(c) for c in corners]
    bm.faces.new(verts)
    if toward is not None:
        face_toward(bm, toward)
    box_uv(bm, uv_scale)
    if uv01:
        uv = bm.loops.layers.uv.verify()
        o, a, b = Vector(corners[0]), Vector(corners[1]), Vector(corners[3])
        bm.faces.ensure_lookup_table()
        for loop in bm.faces[0].loops:
            p = loop.vert.co - o
            loop[uv].uv = (p.dot(a - o) / (a - o).length_squared, p.dot(b - o) / (b - o).length_squared)
    return new_object(name, bm, material, group)

def prism(name, profile, x0, x1, side_material, cap_material, group, cap_uv=None):
    """Extrudes a side profile [(y, z)...] (a simple polygon) between x0 and x1. The two caps take `cap_material`
    (with UVs from `cap_uv(y, z)` when given), the faces around take `side_material`."""
    bm = bmesh.new()
    left = [bm.verts.new((x0, y, z)) for y, z in profile]
    right = [bm.verts.new((x1, y, z)) for y, z in profile]
    caps = [bm.faces.new(list(reversed(left))), bm.faces.new(right)]
    n = len(profile)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new([left[i], left[j], right[j], right[i]])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)  # Outward, whatever the direction of the profile
    box_uv(bm)
    if cap_uv:
        uv = bm.loops.layers.uv.verify()
        for f in caps:
            for loop in f.loops:
                loop[uv].uv = cap_uv(loop.vert.co.y, loop.vert.co.z)
    for f in caps:
        f.material_index = 1
    obj = new_object(name, bm, [side_material, cap_material], group)
    return obj

def cylinder(name, center, radius, depth, material, group, segments=24, radius_top=None, axis="z"):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=segments, radius1=radius, radius2=radius if radius_top is None else radius_top, depth=depth)
    if axis == "x":
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "Y"))
    elif axis == "y":
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "X"))
    bmesh.ops.translate(bm, vec=center, verts=bm.verts)
    box_uv(bm)
    return new_object(name, bm, material, group)

def transform(objects, matrix):
    """Moves objects built in a local frame (a cabinet built at the origin) to their place."""
    for o in objects:
        o.data.transform(matrix)

def import_prop(asset, location, rot_z_deg=0.0, scale=1.0, pick=None, group="furniture", tint=None):
    """A Poly Haven glTF model. `pick` keeps one variant of a file holding several side by side (0 = the tallest); `tint`
    multiplies its base color."""
    folder = os.path.join(DL, asset)
    path = next(os.path.join(folder, f) for f in os.listdir(folder) if f.endswith(".gltf"))
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == "MESH"]
    others = [o for o in new if o.type != "MESH"]
    if pick is not None:
        size = lambda o: max(Vector(v).z for v in o.bound_box) - min(Vector(v).z for v in o.bound_box)
        keep = sorted(meshes, key=size, reverse=True)[pick]
        for o in meshes:
            if o is not keep:
                bpy.data.objects.remove(o)
        meshes = [keep]
    for o in meshes:
        mw = o.matrix_world.copy()
        o.parent = None
        if pick is not None:
            # Center the kept variant on its own footprint
            o.data.transform(mw)
            co = [v.co for v in o.data.vertices]
            lo = Vector((min(c.x for c in co), min(c.y for c in co), min(c.z for c in co)))
            hi = Vector((max(c.x for c in co), max(c.y for c in co), max(c.z for c in co)))
            o.data.transform(Matrix.Translation((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z)))
            mw = Matrix.Identity(4)
        o.matrix_world = Matrix.Translation(location) @ Matrix.Rotation(math.radians(rot_z_deg), 4, "Z") @ Matrix.Diagonal((scale,) * 3 + (1,)) @ mw
        o["bake_group"] = group
        for m in o.data.materials:
            if not m:
                continue
            m.surface_render_method = "DITHERED"
            if tint:
                tint_material(m, tint)
    for o in others:
        bpy.data.objects.remove(o)
    return meshes

def tint_material(mat, tint):
    """Multiplies the base color of a material (glTF materials: an image into the Principled BSDF)."""
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    if not bsdf.inputs["Base Color"].links:
        c = bsdf.inputs["Base Color"].default_value
        bsdf.inputs["Base Color"].default_value = (c[0] * tint[0], c[1] * tint[1], c[2] * tint[2], 1)
        return
    src = bsdf.inputs["Base Color"].links[0].from_socket
    mix = nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.blend_type = "MULTIPLY"
    mix.inputs["Factor"].default_value = 1.0
    mix.inputs["B"].default_value = (*tint, 1.0)
    links.new(src, mix.inputs["A"])
    links.new(mix.outputs["Result"], bsdf.inputs["Base Color"])


# ---------------------------------------------------------------------------------------------------------------------------
# Lights and render settings

def spot(name, location, target, energy, color, size_deg=40, blend=0.5, radius=0.03):
    light = bpy.data.lights.new(name, "SPOT")
    light.energy = energy
    light.color = color
    light.spot_size = math.radians(size_deg)
    light.spot_blend = blend
    light.shadow_soft_size = radius
    obj = bpy.data.objects.new(name, light)
    obj.location = location
    obj.rotation_euler = (Vector(target) - Vector(location)).to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.collection.objects.link(obj)
    return obj

def area(name, location, size, energy, color, rotation=(0, 0, 0), size_y=None):
    light = bpy.data.lights.new(name, "AREA")
    light.shape = "RECTANGLE"
    light.size, light.size_y = size, size if size_y is None else size_y
    light.energy = energy
    light.color = color
    obj = bpy.data.objects.new(name, light)
    obj.location = location
    obj.rotation_euler = rotation
    bpy.context.scene.collection.objects.link(obj)
    return obj

def point(name, location, energy, color, radius=0.05):
    light = bpy.data.lights.new(name, "POINT")
    light.energy = energy
    light.color = color
    light.shadow_soft_size = radius
    obj = bpy.data.objects.new(name, light)
    obj.location = location
    bpy.context.scene.collection.objects.link(obj)
    return obj

def setup_render(scene, eye_height=1.65):
    scene.render.engine = "CYCLES"
    scene.cycles.device = "GPU"
    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = "METAL"
    prefs.get_devices()
    for d in prefs.devices:
        d.use = True
    scene.cycles.samples = 128
    scene.cycles.use_denoising = True
    scene.view_settings.view_transform = "AgX"
    scene.render.resolution_x, scene.render.resolution_y = 1600, 1000
    cam = bpy.data.cameras.new("Eye")
    cam.lens = 16
    cobj = bpy.data.objects.new("Eye", cam)
    cobj.location = (0, 0, eye_height)
    cobj.rotation_euler = (math.radians(88), 0, 0)
    scene.collection.objects.link(cobj)
    scene.camera = cobj
