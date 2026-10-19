# Builds the lobby room scene in Blender from the downloaded CC0 assets, and saves it as a .blend.
#
#   Blender -b --factory-startup --python build_room.py -- <downloads dir> <output .blend>
#
# Layout, in meters, in Blender axes: the player stands at the origin and faces +Y (the VR menu panel shows up there), Z up.
# Every mesh gets a 'bake_group' custom property: the objects of a group are merged into one VPX primitive with one baked texture.
import bpy, bmesh, math, os, sys
from mathutils import Vector, Matrix

args = sys.argv[sys.argv.index("--") + 1:]
DL, OUT = os.path.abspath(args[0]), os.path.abspath(args[1])

# Room extent: 8 m wide, 8.5 m deep, the player 3 m from the back wall
X0, X1, Y0, Y1, H = -4.0, 4.0, -3.0, 5.5, 3.2
SOFFIT_W, SOFFIT_Z = 0.6, 2.9  # Lowered perimeter band of the ceiling, hiding the cove light strip

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene


# ---------------------------------------------------------------------------------------------------------------------------
# Materials

def tex_node(nodes, path, colorspace="sRGB"):
    node = nodes.new("ShaderNodeTexImage")
    node.image = bpy.data.images.load(path, check_existing=True)
    node.image.colorspace_settings.name = colorspace
    return node

def pbr_material(name, folder, scale=(1.0, 1.0), tint=None, roughness_mul=1.0):
    """Principled material from a Poly Haven texture folder, tiled with object space box mapping in meters (`scale` = size of a tile)."""
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
    diff = tex_node(nodes, find("_diff_"))
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
        nmap.inputs["Strength"].default_value = 0.6
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
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Emission Color"].default_value = (*color, 1.0)
    bsdf.inputs["Emission Strength"].default_value = strength
    return mat

def art_material(name, colors, seed):
    """An abstract painting: soft bands of color, made procedurally (no outside artwork)."""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.85
    coord = nodes.new("ShaderNodeTexCoord")
    noise = nodes.new("ShaderNodeTexNoise")
    noise.noise_dimensions = "4D"
    noise.inputs["Scale"].default_value = 1.6
    noise.inputs["Detail"].default_value = 1.5
    noise.inputs["Distortion"].default_value = 0.8
    noise.inputs["W"].default_value = seed
    sep = nodes.new("ShaderNodeSeparateXYZ")
    links.new(coord.outputs["UV"], sep.inputs["Vector"])
    add = nodes.new("ShaderNodeMath")
    add.operation = "MULTIPLY_ADD"
    add.inputs[1].default_value = 0.55
    links.new(noise.outputs["Fac"], add.inputs[0])
    links.new(sep.outputs["Y"], add.inputs[2])
    links.new(coord.outputs["UV"], noise.inputs["Vector"])
    ramp = nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.interpolation = "EASE"
    elements = ramp.color_ramp.elements
    while len(elements) < len(colors):
        elements.new(0.0)
    for i, c in enumerate(colors):
        elements[i].position = 0.3 + 0.55 * i / (len(colors) - 1)
        elements[i].color = (*c, 1.0)
    links.new(add.outputs["Value"], ramp.inputs["Fac"])
    links.new(ramp.outputs["Color"], bsdf.inputs["Base Color"])
    return mat


M_FLOOR = pbr_material("Floor", "laminate_floor_02", scale=(2.0, 2.0), roughness_mul=0.9)
M_WALL = pbr_material("Wall", "plastered_wall_04", scale=(2.5, 2.5), tint=(1.3, 1.28, 1.24))
M_CEILING = flat_material("Ceiling", (0.86, 0.85, 0.83), 0.9)
M_SLAT = pbr_material("OakSlat", "oak_veneer_01", scale=(0.6, 0.6), tint=(0.95, 0.85, 0.72), roughness_mul=0.8)
M_SLAT_BACK = flat_material("SlatBacking", (0.035, 0.035, 0.038), 0.8)
M_SKIRTING = flat_material("Skirting", (0.08, 0.08, 0.085), 0.5)
M_CABINET = pbr_material("Walnut", "oak_veneer_01", scale=(0.8, 0.8), tint=(0.42, 0.28, 0.2), roughness_mul=0.7)
M_RUG = pbr_material("Rug", "poly_wool_herringbone", scale=(0.5, 0.5), tint=(0.55, 0.55, 0.58))
M_FRAME = flat_material("Frame", (0.03, 0.03, 0.03), 0.4)
M_DOOR = pbr_material("Door", "oak_veneer_01", scale=(1.0, 1.0), tint=(0.5, 0.38, 0.3), roughness_mul=0.7)
M_METAL = flat_material("Brass", (0.8, 0.62, 0.38), 0.35, 1.0)
M_PLANTER = flat_material("Planter", (0.12, 0.12, 0.12), 0.7)
M_SOIL = flat_material("Soil", (0.05, 0.035, 0.025), 1.0)
M_LED = emissive_material("LedStrip", (1.0, 0.88, 0.76), 40.0)
M_LED_COOL = emissive_material("LedStripSlats", (1.0, 0.84, 0.66), 25.0)
M_DOWNLIGHT = emissive_material("Downlight", (1.0, 0.86, 0.7), 30.0)
M_ART1 = art_material("Art1", [(0.62, 0.30, 0.18), (0.85, 0.66, 0.45), (0.20, 0.27, 0.30)], 1.3)
M_ART2 = art_material("Art2", [(0.18, 0.26, 0.24), (0.70, 0.62, 0.50), (0.66, 0.36, 0.22)], 7.9)


# ---------------------------------------------------------------------------------------------------------------------------
# Geometry helpers

def new_object(name, bm, material, group):
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    if isinstance(material, (list, tuple)):
        for m in material:
            mesh.materials.append(m)
    else:
        mesh.materials.append(material)
    obj["bake_group"] = group
    return obj

def box(name, lo, hi, material, group, uv_scale=1.0):
    """An axis aligned box from corner `lo` to corner `hi`, with UVs in meters (box projection) for the tiled materials."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    size = Vector(hi) - Vector(lo)
    center = (Vector(hi) + Vector(lo)) / 2
    bmesh.ops.transform(bm, matrix=Matrix.Translation(center) @ Matrix.Diagonal((*size, 1.0)), verts=bm.verts)
    box_uv(bm, uv_scale)
    return new_object(name, bm, material, group)

def quad(name, corners, material, group, uv_scale=1.0, shell=False):
    """A quad from 4 corners, counter clockwise seen from the side it faces (or turned toward the inside of the room for the shell)."""
    bm = bmesh.new()
    verts = [bm.verts.new(c) for c in corners]
    bm.faces.new(verts)
    if shell:
        face_inward(bm)
    box_uv(bm, uv_scale)
    return new_object(name, bm, material, group)

def face_inward(bm, inside=(0.0, 1.0, 1.5)):
    """Turns the faces of the room shell toward the inside of the room (VPX culls back faces)."""
    bm.normal_update()
    flip = [f for f in bm.faces if f.normal.dot(Vector(inside) - f.calc_center_median()) < 0]
    bmesh.ops.reverse_faces(bm, faces=flip)

def box_uv(bm, uv_scale):
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


# ---------------------------------------------------------------------------------------------------------------------------
# Shell: floor, walls, ceiling

quad("Floor", [(X0, Y0, 0), (X1, Y0, 0), (X1, Y1, 0), (X0, Y1, 0)], M_FLOOR, "floor", shell=True)

# Walls, facing inward. The back wall has an opening for the door, which is recessed.
DOOR_X, DOOR_W, DOOR_H, DOOR_DEPTH = 2.2, 1.0, 2.2, 0.12
quad("WallFront", [(X1, Y1, 0), (X0, Y1, 0), (X0, Y1, H), (X1, Y1, H)], M_WALL, "walls", shell=True)
quad("WallLeft", [(X0, Y1, 0), (X0, Y0, 0), (X0, Y0, H), (X0, Y1, H)], M_WALL, "walls", shell=True)
quad("WallRight", [(X1, Y0, 0), (X1, Y1, 0), (X1, Y1, H), (X1, Y0, H)], M_WALL, "walls", shell=True)
bm = bmesh.new()
dl, dr = DOOR_X - DOOR_W / 2, DOOR_X + DOOR_W / 2
for a, b in (((X0, 0), (dl, H)), ((dr, 0), (X1, H)), ((dl, DOOR_H), (dr, H))):
    v = [bm.verts.new(c) for c in ((a[0], Y0, a[1]), (b[0], Y0, a[1]), (b[0], Y0, b[1]), (a[0], Y0, b[1]))]
    bm.faces.new(v)
# Reveal of the door opening
for c in (((dl, Y0, 0), (dl, Y0 - DOOR_DEPTH, 0), (dl, Y0 - DOOR_DEPTH, DOOR_H), (dl, Y0, DOOR_H)),
          ((dr, Y0 - DOOR_DEPTH, 0), (dr, Y0, 0), (dr, Y0, DOOR_H), (dr, Y0 - DOOR_DEPTH, DOOR_H)),
          ((dl, Y0, DOOR_H), (dl, Y0 - DOOR_DEPTH, DOOR_H), (dr, Y0 - DOOR_DEPTH, DOOR_H), (dr, Y0, DOOR_H))):
    bm.faces.new([bm.verts.new(p) for p in c])
face_inward(bm, (DOOR_X, 0.0, 1.0))
box_uv(bm, 1.0)
new_object("WallBack", bm, M_WALL, "walls")
box("Door", (dl, Y0 - DOOR_DEPTH - 0.04, 0), (dr, Y0 - DOOR_DEPTH, DOOR_H), M_DOOR, "walls")
box("DoorHandle", (dr - 0.16, Y0 - DOOR_DEPTH, 1.0), (dr - 0.06, Y0 - DOOR_DEPTH + 0.05, 1.03), M_METAL, "walls")

# Skirting boards
SK_H, SK_D = 0.08, 0.012
box("SkirtFront", (X0, Y1 - SK_D, 0), (X1, Y1, SK_H), M_SKIRTING, "walls")
box("SkirtLeft", (X0, Y0, 0), (X0 + SK_D, Y1, SK_H), M_SKIRTING, "walls")
box("SkirtRight", (X1 - SK_D, Y0, 0), (X1, Y1, SK_H), M_SKIRTING, "walls")
box("SkirtBackL", (X0, Y0, 0), (dl, Y0 + SK_D, SK_H), M_SKIRTING, "walls")
box("SkirtBackR", (dr, Y0, 0), (X1, Y0 + SK_D, SK_H), M_SKIRTING, "walls")

# Ceiling: a recessed center at H, and a lowered perimeter band whose inner edge hides an LED strip lighting the recess
ix0, ix1, iy0, iy1 = X0 + SOFFIT_W, X1 - SOFFIT_W, Y0 + SOFFIT_W, Y1 - SOFFIT_W
# The ceiling covers the whole room above the band, so that the slot shows the lit cavity
quad("CeilingCenter", [(X0, Y0, H), (X0, Y1, H), (X1, Y1, H), (X1, Y0, H)], M_CEILING, "ceiling", shell=True)
bm = bmesh.new()
# Underside of the band (facing down), as 4 quads around the recess
for c in (((X0, Y0), (X0, Y1), (ix0, iy1), (ix0, iy0)), ((ix1, iy0), (ix1, iy1), (X1, Y1), (X1, Y0)),
          ((ix0, Y0), (ix0, iy0), (ix1, iy0), (ix1, Y0)), ((ix0, iy1), (ix0, Y1), (ix1, Y1), (ix1, iy1))):
    bm.faces.new([bm.verts.new((p[0], p[1], SOFFIT_Z)) for p in c])
# Top of the band (facing up), where the LED strips lie
for lo, hi in (((X0, Y0), (ix0, Y1)), ((ix1, Y0), (X1, Y1)), ((ix0, Y0), (ix1, iy0)), ((ix0, iy1), (ix1, Y1))):
    bm.faces.new([bm.verts.new((p[0], p[1], SOFFIT_Z + 0.07)) for p in ((lo[0], lo[1]), (hi[0], lo[1]), (hi[0], hi[1]), (lo[0], hi[1]))])
# Inner vertical faces of the band, facing the center
for a, b in (((ix0, iy0), (ix1, iy0)), ((ix1, iy0), (ix1, iy1)), ((ix1, iy1), (ix0, iy1)), ((ix0, iy1), (ix0, iy0))):
    bm.faces.new([bm.verts.new(p) for p in ((a[0], a[1], SOFFIT_Z), (b[0], b[1], SOFFIT_Z), (b[0], b[1], SOFFIT_Z + 0.07), (a[0], a[1], SOFFIT_Z + 0.07))])
# Underside down, top up, inner faces toward the center
bm.normal_update()
for f in bm.faces:
    c = f.calc_center_median()
    want = Vector((0, 0, -1)) if abs(c.z - SOFFIT_Z) < 1e-4 else Vector((0, 0, 1)) if abs(c.z - SOFFIT_Z - 0.07) < 1e-4 else Vector((0, (iy0 + iy1) / 2, c.z)) - c
    if f.normal.dot(want) < 0:
        f.normal_flip()
box_uv(bm, 1.0)
new_object("CeilingBand", bm, M_CEILING, "ceiling")
# The LED strips, on top of the band, just behind its inner edge, facing up and inward
LED = 0.03
for i, (lo, hi) in enumerate((((ix0 - 0.08, iy0 - 0.08), (ix1 + 0.08, iy0 - 0.08 + LED)), ((ix0 - 0.08, iy1 + 0.08 - LED), (ix1 + 0.08, iy1 + 0.08)),
                              ((ix0 - 0.08, iy0), (ix0 - 0.08 + LED, iy1)), ((ix1 + 0.08 - LED, iy0), (ix1 + 0.08, iy1)))):
    box(f"CoveLed{i}", (lo[0], lo[1], SOFFIT_Z + 0.07), (hi[0], hi[1], SOFFIT_Z + 0.08), M_LED, "ceiling")

# Recessed downlights in the band, washing the side walls
DL_Y = [Y0 + 1.2 + i * (Y1 - Y0 - 2.4) / 3 for i in range(4)]
for i, (x, y) in enumerate([(X0 + 0.3, y) for y in DL_Y] + [(X1 - 0.3, y) for y in DL_Y]):
    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=True, radius=0.045, segments=16)
    bmesh.ops.translate(bm, vec=(x, y, SOFFIT_Z - 0.002), verts=bm.verts)
    bmesh.ops.reverse_faces(bm, faces=bm.faces)
    box_uv(bm, 1.0)
    new_object(f"Downlight{i}", bm, M_DOWNLIGHT, "ceiling")
    light = bpy.data.lights.new(f"DownlightSpot{i}", "SPOT")
    light.energy = 120
    light.color = (1.0, 0.86, 0.7)
    light.spot_size = math.radians(70)
    light.spot_blend = 0.8
    light.shadow_soft_size = 0.04
    lobj = bpy.data.objects.new(f"DownlightSpot{i}", light)
    lobj.location = (x + (0.12 if x < 0 else -0.12), y, SOFFIT_Z - 0.01)
    scene.collection.objects.link(lobj)


# ---------------------------------------------------------------------------------------------------------------------------
# Feature wall in front of the player: vertical oak slats on a dark backing, grazed by a strip at the top

SL_X0, SL_X1, SL_Z1 = -2.4, 2.4, SOFFIT_Z
box("SlatBacking", (SL_X0, Y1 - 0.02, 0), (SL_X1, Y1, SL_Z1), M_SLAT_BACK, "feature")
SLAT_W, SLAT_D, SLAT_GAP = 0.04, 0.03, 0.035
n = int((SL_X1 - SL_X0 + SLAT_GAP) / (SLAT_W + SLAT_GAP))
start = (SL_X0 + SL_X1) / 2 - (n * SLAT_W + (n - 1) * SLAT_GAP) / 2
bm = bmesh.new()
for i in range(n):
    x = start + i * (SLAT_W + SLAT_GAP)
    geom = bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.transform(bm, matrix=Matrix.Translation((x + SLAT_W / 2, Y1 - 0.02 - SLAT_D / 2, SL_Z1 / 2)) @ Matrix.Diagonal((SLAT_W, SLAT_D, SL_Z1, 1.0)), verts=geom["verts"])
# The back faces against the backing are never seen
bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.normal.y > 0.9 or f.normal.z < -0.9], context="FACES")
box_uv(bm, 1.0)
new_object("Slats", bm, M_SLAT, "feature")
box("SlatLed", (SL_X0, Y1 - 0.09, SOFFIT_Z - 0.012), (SL_X1, Y1 - 0.06, SOFFIT_Z - 0.002), M_LED_COOL, "feature")

# Low floating walnut cabinet along the feature wall
CAB_X0, CAB_X1 = -1.3, 1.3
box("Cabinet", (CAB_X0, Y1 - 0.05 - 0.42, 0.22), (CAB_X1, Y1 - 0.05, 0.62), M_CABINET, "furniture")
box("CabinetShadowGap", (CAB_X0 + 0.02, Y1 - 0.05 - 0.40, 0.2), (CAB_X1 - 0.02, Y1 - 0.06, 0.22), M_SKIRTING, "furniture")


# ---------------------------------------------------------------------------------------------------------------------------
# Furniture from Poly Haven (glTF)

def import_prop(asset, location, rot_z_deg=0.0, scale=1.0, pick=None, group="furniture"):
    folder = os.path.join(DL, asset)
    path = next(os.path.join(folder, f) for f in os.listdir(folder) if f.endswith(".gltf"))
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    meshes = [o for o in new if o.type == "MESH"]
    others = [o for o in new if o.type != "MESH"]
    if pick is not None:
        # Asset files laid out with several variants side by side: keep one
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
            if m:
                m.surface_render_method = "DITHERED"
    for o in others:
        bpy.data.objects.remove(o)
    return meshes

# A small plant on the feature wall cabinet, off center so that the menu does not hide it
import_prop("potted_plant_04", (CAB_X1 - 0.35, Y1 - 0.26, 0.62), rot_z_deg=15)

# Left: sofa against the wall facing the room, rug, coffee table, two paintings above
import_prop("sofa_02", (X0 + 0.5, 1.6, 0), rot_z_deg=-90)
rug = quad("Rug", [(X0 + 0.6, 0.4, 0.004), (X0 + 2.7, 0.4, 0.004), (X0 + 2.7, 2.8, 0.004), (X0 + 0.6, 2.8, 0.004)], M_RUG, "floor")
import_prop("modern_coffee_table_01", (X0 + 1.85, 1.6, 0), rot_z_deg=0)
import_prop("potted_plant_04", (X0 + 1.85, 1.35, 0.39))
for i, (y, mat) in enumerate(((1.05, M_ART1), (2.15, M_ART2))):
    box(f"ArtFrame{i}", (X0, y - 0.42, 1.25), (X0 + 0.035, y + 0.42, 2.25), M_FRAME, "furniture")
    quad(f"Art{i}", [(X0 + 0.036, y - 0.39, 1.28), (X0 + 0.036, y + 0.39, 1.28), (X0 + 0.036, y + 0.39, 2.22), (X0 + 0.036, y - 0.39, 2.22)], mat, "furniture", uv_scale=1.0)
    art = bpy.data.objects[f"Art{i}"]
    # UVs 0..1 over the canvas for the procedural painting
    for loop in art.data.loops:
        p = art.data.vertices[loop.vertex_index].co
        art.data.uv_layers.active.data[loop.index].uv = ((y + 0.39 - p.y) / 0.78, (p.z - 1.28) / 0.94)
import_prop("modern_ceiling_lamp_01", (X0 + 1.85, 1.6, H - 0.95 - 0.22))

# Right: lounge chair turned toward the center, side table with a small plant
import_prop("mid_century_lounge_chair", (X1 - 1.1, 1.0, 0), rot_z_deg=-60)
import_prop("side_table_01", (X1 - 0.55, 2.0, 0), rot_z_deg=90)
import_prop("potted_plant_04", (X1 - 0.55, 2.0, 0.55), rot_z_deg=40)

# Plants in tall planters in the corners next to the feature wall, and next to the door
def planter(name, x, y, radius=0.22, height=0.55):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=32, radius1=radius * 0.85, radius2=radius, depth=height)
    bmesh.ops.translate(bm, vec=(x, y, height / 2), verts=bm.verts)
    box_uv(bm, 1.0)
    new_object(name, bm, M_PLANTER, "furniture")
    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=True, radius=radius - 0.015, segments=32)
    bmesh.ops.translate(bm, vec=(x, y, height - 0.04), verts=bm.verts)
    box_uv(bm, 1.0)
    new_object(name + "Soil", bm, M_SOIL, "furniture")
    return height - 0.04

for i, (x, y, pick) in enumerate(((X0 + 0.55, Y1 - 0.55, 0), (X1 - 0.55, Y1 - 0.55, 1), (DOOR_X - 1.0, Y0 + 0.45, 0))):
    top = planter(f"Planter{i}", x, y)
    import_prop("calathea_orbifolia_01", (x, y, top - 0.02), rot_z_deg=70 * i, scale=2.0, pick=pick)
# A bench by the door
box("Bench", (X0 + 0.9, Y0 + 0.08, 0.40), (X0 + 2.5, Y0 + 0.48, 0.45), M_CABINET, "furniture")
for x in (X0 + 1.0, X0 + 2.36):
    box(f"BenchLeg{x:.1f}", (x, Y0 + 0.12, 0), (x + 0.04, Y0 + 0.44, 0.40), M_FRAME, "furniture")


# ---------------------------------------------------------------------------------------------------------------------------
# Lighting (for the bake): the emissive strips do most of the work; a faint world keeps the darkest corners from going black

world = bpy.data.worlds.new("World")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.02, 0.02, 0.022, 1)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.0
scene.world = world

# Soft fill from the recessed ceiling, standing in for the light bounced by it
fill = bpy.data.lights.new("CeilingFill", "AREA")
fill.shape = "RECTANGLE"
fill.size, fill.size_y = ix1 - ix0 - 0.4, iy1 - iy0 - 0.4
fill.energy = 250
fill.color = (1.0, 0.9, 0.8)
fobj = bpy.data.objects.new("CeilingFill", fill)
fobj.location = (0, (iy0 + iy1) / 2, H - 0.02)
scene.collection.objects.link(fobj)

# The globe of the ceiling lamp
lamp = bpy.data.lights.new("LampGlobe", "POINT")
lamp.energy = 35
lamp.color = (1.0, 0.82, 0.62)
lamp.shadow_soft_size = 0.1
lobj = bpy.data.objects.new("LampGlobe", lamp)
lobj.location = (X0 + 1.85, 1.6, H - 0.95 - 0.22 + 0.12)
scene.collection.objects.link(lobj)


# ---------------------------------------------------------------------------------------------------------------------------
# Render settings and a camera at eye height for the previews

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
cobj.location = (0, 0, 1.65)
cobj.rotation_euler = (math.radians(88), 0, 0)
scene.collection.objects.link(cobj)
scene.camera = cobj

bpy.ops.wm.save_as_mainfile(filepath=OUT)
tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in scene.objects if o.type == "MESH")
print("BUILD ok", OUT, "objects", len([o for o in scene.objects if o.type == "MESH"]), "triangles", tris)
