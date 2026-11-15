# Builds the default VR cabinet in Blender and exports it as a glTF binary for the application (src/assets/vr-cabinet.glb).
#   Blender -b --factory-startup --python build_cabinet.py -- <work dir> [--no-export] [--blend]
# The work dir receives the textures (art/*.png), the .glb and, with --blend, the scene for render_cabinet.py.
#
# A Williams WPC standard body (dims.py), its generic artwork drawn by cabinet_art.py, and both kinds of ball launchers: a spring plunger
# and a launch button. Every exported object is a node of the glTF file with these extras, read by the application:
#   fit:     "deform" (each vertex follows the fit to the table's playfield) or "rigid" (moved as a whole with its anchor)
#   anchor:  [x, y, z] of a rigid node
#   space:   "cabinet" (VR cabinet space) or "feet" (legs, stretched to the floor)
#   variant: "spring" or "button" for the parts of one launcher only
#   role:    part animated or placed by the application (plunger_rod, plunger_spring, launch_button, flipper_left, flipper_right, backglass,
#            display)
# The coordinates are those of the design (dims.py: Z up, meters), exported without the glTF Y-up conversion.
import bpy, bmesh, math, os, sys, json
import numpy as np
from mathutils import Vector, Matrix

sys.path.insert(0, os.path.dirname(__file__))
import dims as D
import cabinet_art as art

args = sys.argv[sys.argv.index("--") + 1:]
WORK = os.path.abspath(args[0])
EXPORT = "--no-export" not in args
SAVE_BLEND = "--blend" in args
ASSET = os.path.join(WORK, "asset")  # What goes to src/assets/vr-cabinet
os.makedirs(os.path.join(WORK, "art"), exist_ok=True)
os.makedirs(ASSET, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
IN = D.IN


# ---------------------------------------------------------------------------------------------------------------------------
# Materials. vpx_* custom properties go to the glTF material extras for the application (see DefaultCabinet.cpp)

def save_png(name, rgb):
    h, w = rgb.shape[:2]
    img = bpy.data.images.new(name, w, h)
    rgba = np.concatenate([np.clip(rgb, 0, 1), np.ones((h, w, 1))], axis=2)[::-1]
    img.pixels.foreach_set(rgba.astype(np.float32).ravel())
    img.filepath_raw = os.path.join(WORK, "art", name + ".png")
    img.file_format = "PNG"
    img.save()
    # The application loads the textures as images of the table (see DefaultCabinet.cpp), from WEBP files next to the .glb
    webp = bpy.data.images.new(name + "_webp", w, h)
    webp.pixels.foreach_set(rgba.astype(np.float32).ravel())
    webp.filepath_raw = os.path.join(ASSET, name + ".webp")
    webp.file_format = "WEBP"
    scene.render.image_settings.quality = 92
    webp.save()
    return img

def material(name, color=(1, 1, 1), roughness=0.5, metallic=0.0, image=None, **vpx):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*[c ** 2.2 for c in color], 1.0)  # Colors given in sRGB
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    if image is not None:
        mat["vpx_image"] = image.name
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = image
        tex.extension = "EXTEND"
        links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    for k, v in vpx.items():
        mat["vpx_" + k] = v
    return mat

def hexc(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5))

side_len, side_h = D.SIDE_L, D.BACK_H
IMG = {
    "side": save_png("cab_side", art.side(D.SIDE_L, D.FRONT_H, D.BACK_H, D.Y_FLAT - D.Y_FRONT)),
    "front": save_png("cab_front", art.front(D.CAB_W, D.FRONT_H)),
    "bbside": save_png("cab_backbox_side", art.backbox_side(D.BB_D_TOP, D.BB_D_BOTTOM, D.BB_H)),
    "backglass": save_png("cab_backglass", art.backglass()),
    "coin": save_png("cab_coin_insert", art.coin_insert()),
}
SPK_H = D.BB_SPEAKER[1] - D.BB_SPEAKER[0]
DMD_RECT = ((D.BB_INNER_W - D.DMD_W) / 2, (SPK_H - D.DMD_H) / 2 - 0.1 * IN, (D.BB_INNER_W + D.DMD_W) / 2, (SPK_H + D.DMD_H) / 2 - 0.1 * IN)
IMG["speaker"] = save_png("cab_speaker_panel", art.speaker_panel(D.BB_INNER_W, SPK_H, DMD_RECT,
    [(D.BB_INNER_W / 2 - D.SPEAKER_X, SPK_H / 2, D.SPEAKER_R), (D.BB_INNER_W / 2 + D.SPEAKER_X, SPK_H / 2, D.SPEAKER_R)]))

M = {
    "art_side": material("CabArtSide", roughness=0.28, image=IMG["side"], clearcoat=1),
    "art_front": material("CabArtFront", roughness=0.28, image=IMG["front"], clearcoat=1),
    "art_backbox": material("CabArtBackbox", roughness=0.28, image=IMG["bbside"], clearcoat=1),
    "black": material("CabBlack", hexc("#141416"), roughness=0.5),
    "interior": material("CabInterior", hexc("#0c0c0d"), roughness=0.85),
    "chrome": material("CabChrome", hexc("#d4d8de"), roughness=0.12, metallic=1.0),
    "steel": material("CabDarkSteel", hexc("#2a2c31"), roughness=0.35, metallic=1.0),
    "door": material("CabCoinDoor", hexc("#18191c"), roughness=0.4, metallic=0.6),
    "insert": material("CabCoinInsert", image=IMG["coin"], roughness=0.3, unlit=0.6),
    "flipper": material("CabFlipperButton", hexc("#d8121c"), roughness=0.22),
    "start": material("CabStartButton", hexc("#f4f1e8"), roughness=0.25, unlit=0.25),
    "knob": material("CabPlungerKnob", hexc("#c0101a"), roughness=0.18),
    "launch": material("CabLaunchButton", hexc("#e0141c"), roughness=0.2, unlit=0.2),
    "rubber": material("CabRubber", hexc("#060606"), roughness=0.9),
    "speaker": material("CabSpeakerPanel", image=IMG["speaker"], roughness=0.45),
    "backglass": material("CabBackglass", image=IMG["backglass"], roughness=0.1, unlit=1),
    "glass": material("CabDisplayGlass", hexc("#050506"), roughness=0.05),
}


# ---------------------------------------------------------------------------------------------------------------------------
# Geometry helpers. Every part is a closed mesh with its faces outward (VPX culls back faces) and UVs; parts of the same node are
# joined at the end (see NODES).

PARTS = []  # (node name, object)

def new_object(node, name, bm, materials, smooth_angle=None):
    bm.normal_update()
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    for m in (materials if isinstance(materials, (list, tuple)) else [materials]):
        mesh.materials.append(m)
    if smooth_angle is not None:
        mesh.shade_smooth()
        mesh.set_sharp_from_angle(angle=math.radians(smooth_angle))
    PARTS.append((node, obj))
    return obj

def planar_uv(bm, origin, u_axis, v_axis, size_u, size_v, faces=None):
    """UVs 0..1 across a rectangle of the plane (origin, u_axis, v_axis), for the faces given (all by default)."""
    uv = bm.loops.layers.uv.verify()
    o, u, v = Vector(origin), Vector(u_axis).normalized(), Vector(v_axis).normalized()
    for f in (faces if faces is not None else bm.faces):
        for loop in f.loops:
            p = loop.vert.co - o
            loop[uv].uv = (p.dot(u) / size_u, p.dot(v) / size_v)

def box_uv(bm, scale=0.25):
    bm.normal_update()
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        n = f.normal
        axis = max(range(3), key=lambda i: abs(n[i]))
        for loop in f.loops:
            p = loop.vert.co
            a, b = [(p.y, p.z), (p.x, p.z), (p.x, p.y)][axis]
            loop[uv].uv = (a / scale, b / scale)

def box(node, name, lo, hi, mat):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    size, center = Vector(hi) - Vector(lo), (Vector(hi) + Vector(lo)) / 2
    bmesh.ops.transform(bm, matrix=Matrix.Translation(center) @ Matrix.Diagonal((*size, 1.0)), verts=bm.verts)
    box_uv(bm)
    return new_object(node, name, bm, mat)

def slab_x(node, name, profile, x_out, x_in, mat_out, mat_in, mat_edge, uv_rect):
    """A panel in a YZ plane: the polygon `profile` [(y, z)] extruded from x_out (its outside face, with the art) to x_in.
    uv_rect = (y0, z0, size_y, size_z, mirror): the art spans that rectangle of the outside face."""
    bm = bmesh.new()
    outer = [bm.verts.new((x_out, y, z)) for y, z in profile]
    inner = [bm.verts.new((x_in, y, z)) for y, z in profile]
    f_out, f_in = bm.faces.new(outer), bm.faces.new(list(reversed(inner)))
    n = len(profile)
    edges = [bm.faces.new([outer[(i + 1) % n], outer[i], inner[i], inner[(i + 1) % n]]) for i in range(n)]
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    box_uv(bm)
    y0, z0, sy, sz, mirror = uv_rect
    uv = bm.loops.layers.uv.verify()
    for loop in f_out.loops:
        u = (loop.vert.co.y - y0) / sy
        loop[uv].uv = (1 - u if mirror else u, (loop.vert.co.z - z0) / sz)
    f_out.material_index, f_in.material_index = 0, 1
    for f in edges:
        f.material_index = 2
    return new_object(node, name, bm, [mat_out, mat_in, mat_edge])

def basis(axis):
    a = Vector(axis).normalized()
    ref = Vector((0, 0, 1)) if abs(a.z) < 0.9 else Vector((1, 0, 0))
    u = a.cross(ref).normalized()
    v = a.cross(u).normalized()
    return a, u, v

def revolve(node, name, profile, center, axis, mat, segments=32, smooth=40, rotate=0.0):
    """Lathe: profile [(radius, height)] turned around `axis` from `center` (height along the axis). A radius of 0 closes the end.
    The first and last rings are closed by caps."""
    a, u, v = basis(axis)
    c = Vector(center)
    bm = bmesh.new()
    rings = []
    for r, h in profile:
        if r <= 1e-6:
            rings.append([bm.verts.new(c + a * h)])
        else:
            rings.append([bm.verts.new(c + a * h + (u * math.cos(t) + v * math.sin(t)) * r)
                          for t in (rotate + 2 * math.pi * i / segments for i in range(segments))])
    for r0, r1 in zip(rings, rings[1:]):
        if len(r0) == 1 and len(r1) == 1:
            continue
        if len(r0) == 1:
            for i in range(segments):
                bm.faces.new([r0[0], r1[i], r1[(i + 1) % segments]])
        elif len(r1) == 1:
            for i in range(segments):
                bm.faces.new([r0[i], r0[(i + 1) % segments], r1[0]])
        else:
            for i in range(segments):
                bm.faces.new([r0[i], r0[(i + 1) % segments], r1[(i + 1) % segments], r1[i]])
    for ring in (rings[0], rings[-1]):
        if len(ring) > 2:
            bm.faces.new(ring)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    box_uv(bm, 0.05)
    return new_object(node, name, bm, mat, smooth_angle=smooth)

def sweep(node, name, profile, p0, p1, side, mat, smooth=None, up=(0, 0, 1)):
    """A straight bar: the 2D profile [(s, t)] (s along `side`, t along `up`, both made square to the bar) extruded from p0 to p1."""
    p0, p1 = Vector(p0), Vector(p1)
    d = (p1 - p0).normalized()
    s = Vector(side)
    s = (s - d * s.dot(d)).normalized()
    t = Vector(up)
    t = (t - d * t.dot(d) - s * t.dot(s)).normalized()
    bm = bmesh.new()
    r0 = [bm.verts.new(p0 + s * a + t * b) for a, b in profile]
    r1 = [bm.verts.new(p1 + s * a + t * b) for a, b in profile]
    n = len(profile)
    for i in range(n):
        bm.faces.new([r0[i], r0[(i + 1) % n], r1[(i + 1) % n], r1[i]])
    bm.faces.new(r0)
    bm.faces.new(list(reversed(r1)))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    box_uv(bm, 0.1)
    return new_object(node, name, bm, mat, smooth_angle=smooth)

def rounded_rect(w, h, r, steps=6):
    pts = []
    for cx, cy, a0 in ((w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90), (-w / 2 + r, -h / 2 + r, 180), (w / 2 - r, -h / 2 + r, 270)):
        for i in range(steps + 1):
            a = math.radians(a0 + 90 * i / steps)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts

def plate_on_front(node, name, outline, center, thickness, mat, bevel=0.0):
    """A flat plate on the front of the cabinet (a plane Y = const), outline [(x, z)] around `center`, standing `thickness` out."""
    cx, cy, cz = center
    bm = bmesh.new()
    back = [bm.verts.new((cx + x, cy, cz + z)) for x, z in outline]
    front = [bm.verts.new((cx + x * (1 - bevel), cy - thickness, cz + z * (1 - bevel))) for x, z in outline]
    n = len(outline)
    for i in range(n):
        bm.faces.new([back[i], back[(i + 1) % n], front[(i + 1) % n], front[i]])
    bm.faces.new(back)
    bm.faces.new(list(reversed(front)))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    box_uv(bm, 0.05)
    return new_object(node, name, bm, mat, smooth_angle=50)

def front_quad(node, name, x0, x1, z0, z1, y, mat, uv=(0, 1, 0, 1), facing=-1):
    """A textured rectangle facing the player (facing -1: toward -Y) or the back (+1) at depth y."""
    bm = bmesh.new()
    vs = [bm.verts.new(p) for p in ((x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1))]
    f = bm.faces.new(vs)
    bm.normal_update()
    if f.normal.y * facing < 0:
        f.normal_flip()
    lay = bm.loops.layers.uv.verify()
    u0, u1, v0, v1 = uv
    for loop in f.loops:
        p = loop.vert.co
        loop[lay].uv = (u0 + (u1 - u0) * (p.x - x0) / (x1 - x0), v0 + (v1 - v0) * (p.z - z0) / (z1 - z0))
    return new_object(node, name, bm, mat)

def helix(node, name, center, axis, radius, wire, length, turns, mat, steps_per_turn=24, sides=8):
    a, u, v = basis(axis)
    c = Vector(center)
    bm = bmesh.new()
    rings = []
    n = int(turns * steps_per_turn)
    for i in range(n + 1):
        t = i / n
        ang = 2 * math.pi * turns * t
        p = c + a * (length * t) + (u * math.cos(ang) + v * math.sin(ang)) * radius
        tangent = (a * length + (-u * math.sin(ang) + v * math.cos(ang)) * radius * 2 * math.pi * turns).normalized()
        nrm = (u * math.cos(ang) + v * math.sin(ang))
        bin_ = tangent.cross(nrm).normalized()
        rings.append([bm.verts.new(p + (nrm * math.cos(2 * math.pi * k / sides) + bin_ * math.sin(2 * math.pi * k / sides)) * wire)
                      for k in range(sides)])
    for r0, r1 in zip(rings, rings[1:]):
        for k in range(sides):
            bm.faces.new([r0[k], r0[(k + 1) % sides], r1[(k + 1) % sides], r1[k]])
    bm.faces.new(rings[0])
    bm.faces.new(list(reversed(rings[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    box_uv(bm, 0.05)
    return new_object(node, name, bm, mat, smooth_angle=70)

def hex_bolt(node, name, center, axis, across=0.75 * IN, height=0.28 * IN, mat=None):
    """A hex bolt head on a washer."""
    r_hex = across / 2 / math.cos(math.pi / 6)
    revolve(node, name + "Washer", [(0, 0), (across * 0.72, 0), (across * 0.72, 0.06 * IN), (0, 0.06 * IN)], center, axis, mat, segments=24)
    revolve(node, name + "Head", [(0, 0.05 * IN), (r_hex, 0.05 * IN), (r_hex, height), (r_hex * 0.85, height + 0.03 * IN), (0, height + 0.03 * IN)],
            center, axis, mat, segments=6, smooth=None, rotate=math.pi / 6)


# ---------------------------------------------------------------------------------------------------------------------------
# Cabinet body

XO, XI = D.CAB_W / 2, D.CAB_W / 2 - D.WALL
# The sides start behind the front panel, whose ends carry the start of the side art (the corner looks mitred, as on the real body)
top_at = lambda y: D.Z_FRONT_TOP + (D.Z_BACK_TOP - D.Z_FRONT_TOP) * (y - D.Y_FRONT) / (D.Y_FLAT - D.Y_FRONT)
YS = D.Y_FRONT + D.WALL
side_profile = [(YS, D.Z_BOTTOM), (D.Y_BACK, D.Z_BOTTOM), (D.Y_BACK, D.Z_BACK_TOP), (D.Y_FLAT, D.Z_BACK_TOP), (YS, top_at(YS))]
art_rect = (D.Y_FRONT, D.Z_BOTTOM, D.SIDE_L, D.BACK_H, False)
slab_x("Body", "SideLeft", side_profile, -XO, -XI, M["art_side"], M["interior"], M["black"], art_rect)
slab_x("Body", "SideRight", side_profile, XO, XI, M["art_side"], M["interior"], M["black"], art_rect)

# Front panel: art outside, the top edge black
bm = bmesh.new()
bmesh.ops.create_cube(bm, size=1.0)
lo, hi = Vector((-XO, D.Y_FRONT, D.Z_BOTTOM)), Vector((XO, D.Y_FRONT + D.WALL, D.Z_FRONT_TOP))
bmesh.ops.transform(bm, matrix=Matrix.Translation((lo + hi) / 2) @ Matrix.Diagonal((*(hi - lo), 1.0)), verts=bm.verts)
box_uv(bm)
bm.normal_update()
front_faces = [f for f in bm.faces if f.normal.y < -0.9]
planar_uv(bm, (-XO, 0, D.Z_BOTTOM), (1, 0, 0), (0, 0, 1), D.CAB_W, D.FRONT_H, front_faces)
end_faces = [f for f in bm.faces if abs(f.normal.x) > 0.9]
uv = bm.loops.layers.uv.verify()
for f in end_faces:
    for loop in f.loops:
        loop[uv].uv = ((loop.vert.co.y - D.Y_FRONT) / D.SIDE_L, (loop.vert.co.z - D.Z_BOTTOM) / D.BACK_H)
for f in bm.faces:
    f.material_index = 0 if f.normal.y < -0.9 else (2 if f.normal.y > 0.9 else (3 if abs(f.normal.x) > 0.9 else 1))
new_object("Body", "Front", bm, [M["art_front"], M["black"], M["interior"], M["art_side"]])

box("Body", "Back", (-XI, D.Y_BACK - D.WALL, D.Z_BOTTOM), (XI, D.Y_BACK, D.Z_BACK_TOP), M["black"])
box("Body", "Bottom", (-XI, D.Y_FRONT + D.WALL, D.Z_BOTTOM), (XI, D.Y_BACK - D.WALL, D.Z_BOTTOM + D.WALL), M["interior"])
box("Body", "Top", (-XI, D.Y_FLAT, D.Z_BACK_TOP - D.WALL), (XI, D.Y_BACK - D.WALL, D.Z_BACK_TOP), M["black"])
# The board behind the far end of the playfield, under the backbox
box("Body", "HeadBoard", (-XI, D.Y_FLAT, D.Z_BOTTOM + D.WALL), (XI, D.Y_FLAT + D.WALL, D.Z_BACK_TOP - D.WALL), M["interior"])
# Vents of the back panel
for x in (-0.12, 0.12):
    front_quad("Body", f"Vent{x}", x - 3 * IN, x + 3 * IN, D.Z_BOTTOM + 15 * IN, D.Z_BOTTOM + 17 * IN, D.Y_BACK + 0.001, M["rubber"], facing=1)

# Lockdown bar: a chrome channel over the front of the glass, across the whole width
lb_profile = [(-0.25 * IN, 0), (2.6 * IN, 0), (2.6 * IN, -0.4 * IN), (2.5 * IN, -0.4 * IN), (2.5 * IN, -0.1 * IN), (-0.15 * IN, -0.1 * IN),
              (-0.15 * IN, -1.0 * IN), (-0.25 * IN, -1.0 * IN)]
sweep("Lockbar", "Lockbar", lb_profile, (-XO - 0.1 * IN, D.Y_FRONT, D.LOCKBAR_TOP), (XO + 0.1 * IN, D.Y_FRONT, D.LOCKBAR_TOP), (0, 1, 0), M["chrome"])

# Side armor: chrome angles on the top edges, from behind the lockdown bar to the backbox
for sgn in (-1, 1):
    y0, y1 = D.Y_FRONT + 2.55 * IN, D.Y_FLAT + 0.2 * IN
    top = lambda y: D.Z_FRONT_TOP + (D.Z_BACK_TOP - D.Z_FRONT_TOP) * min(1, (y - D.Y_FRONT) / (D.Y_FLAT - D.Y_FRONT))
    # Profile across the edge: s outward (x), t up (normal to the sloped top)
    prof = [(-D.WALL - 0.45 * IN, 0), (0.06 * IN, 0), (0.06 * IN, -0.8 * IN), (0.0, -0.8 * IN), (0.0, -0.08 * IN), (-D.WALL - 0.45 * IN, -0.08 * IN)]
    prof = [(s, t + 0.08 * IN) for s, t in prof]
    p0, p1 = Vector((sgn * XO, y0, top(y0))), Vector((sgn * XO, y1, top(y1)))
    sweep("SideRails", f"Rail{sgn}", prof if sgn > 0 else [(-s, t) for s, t in reversed(prof)], p0, p1, (1, 0, 0), M["chrome"])


# ---------------------------------------------------------------------------------------------------------------------------
# Legs (feet space): chamfered square tubes on the corners, two bolts on the chamfer, levelers

def leg(name, cx, cy, sx, sy):
    """Leg on the corner (cx, cy); sx, sy: the directions out of the cabinet."""
    c = D.LEG * 0.32  # Chamfer
    o, i_ = D.LEG_OUT, D.LEG - D.LEG_OUT
    # Square from -i_ (inside) to +o (outside) on each axis, the outer corner chamfered; in local (a, b) = outward axes
    local = [(-i_, -i_), (o, -i_), (o, o - c), (o - c, o), (-i_, o)]
    pts = [(cx + sx * a, cy + sy * b) for a, b in local]
    bm = bmesh.new()
    lo = [bm.verts.new((x, y, D.LEG_BOTTOM)) for x, y in pts]
    hi = [bm.verts.new((x, y, D.LEG_TOP)) for x, y in pts]
    n = len(pts)
    for k in range(n):
        bm.faces.new([lo[k], lo[(k + 1) % n], hi[(k + 1) % n], hi[k]])
    bm.faces.new(lo)
    bm.faces.new(list(reversed(hi)))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bmesh.ops.bevel(bm, geom=[e for e in bm.edges if abs(e.verts[0].co.z - e.verts[1].co.z) > 0.1], offset=0.004, segments=2, affect="EDGES")
    box_uv(bm, 0.1)
    new_object("Legs", name, bm, M["chrome"], smooth_angle=35)
    # Bolts on the chamfer face, 2-1/4" apart
    mid = Vector((cx + sx * (o - c / 2), cy + sy * (o - c / 2), 0))
    nrm = Vector((sx, sy, 0)).normalized()
    for k, z in enumerate((D.LEG_TOP - 1.4 * IN, D.LEG_TOP - 3.65 * IN)):
        hex_bolt("Legs", f"{name}Bolt{k}", (mid.x, mid.y, z), nrm, mat=M["chrome"])
    # Leveler: threaded stem, nut, foot pad
    fx, fy = cx + sx * (o - i_) / 2, cy + sy * (o - i_) / 2
    revolve("Legs", name + "Stem", [(0, 0), (0.25 * IN, 0), (0.25 * IN, D.LEG_BOTTOM + 0.1 * IN), (0, D.LEG_BOTTOM + 0.1 * IN)], (fx, fy, 0), (0, 0, 1), M["steel"], segments=12)
    revolve("Legs", name + "Nut", [(0, 0), (0.48 * IN, 0), (0.48 * IN, 0.3 * IN), (0, 0.3 * IN)], (fx, fy, D.LEG_BOTTOM - 0.32 * IN), (0, 0, 1), M["steel"], segments=6, smooth=None)
    revolve("Legs", name + "Foot", [(0, 0), (0.8 * IN, 0), (0.82 * IN, 0.25 * IN), (0.6 * IN, 0.5 * IN), (0, 0.5 * IN)], (fx, fy, 0), (0, 0, 1), M["rubber"], segments=24)

for sx in (-1, 1):
    leg(f"LegFront{sx}", sx * XO, D.Y_FRONT, sx, -1)
    leg(f"LegBack{sx}", sx * XO, D.Y_BACK, sx, 1)


# ---------------------------------------------------------------------------------------------------------------------------
# Coin door: frame, door, two coin entries with their lit price inserts and reject buttons, two coin returns, the lock

YF = D.Y_FRONT
cz = D.COIN_DOOR_Z
plate_on_front("CoinDoor", "DoorFrame", rounded_rect(D.COIN_DOOR_W, D.COIN_DOOR_H, 0.35 * IN), (0, YF, cz), 0.12 * IN, M["door"])
plate_on_front("CoinDoor", "Door", rounded_rect(D.COIN_DOOR_W - 1.4 * IN, D.COIN_DOOR_H - 1.3 * IN, 0.25 * IN), (0, YF - 0.12 * IN, cz), 0.16 * IN, M["door"], bevel=0.01)
YD = YF - 0.28 * IN  # Face of the door
for k, x in enumerate((-2.7 * IN, 2.7 * IN)):
    ez = cz + 1.6 * IN
    plate_on_front("CoinDoor", f"Entry{k}", rounded_rect(1.7 * IN, 3.6 * IN, 0.2 * IN), (x, YD, ez), 0.3 * IN, M["chrome"], bevel=0.03)
    front_quad("CoinDoor", f"Insert{k}", x - 0.55 * IN, x + 0.55 * IN, ez - 0.75 * IN, ez + 1.45 * IN, YD - 0.305 * IN, M["insert"])
    plate_on_front("CoinDoor", f"Reject{k}", rounded_rect(0.7 * IN, 0.5 * IN, 0.1 * IN), (x, YD - 0.3 * IN, ez - 1.25 * IN), 0.12 * IN, M["steel"], bevel=0.05)
    # Coin return: a chrome frame around a dark opening
    rz = cz - 3.4 * IN
    plate_on_front("CoinDoor", f"Return{k}", rounded_rect(2.1 * IN, 1.4 * IN, 0.15 * IN), (x, YD, rz), 0.18 * IN, M["chrome"], bevel=0.04)
    front_quad("CoinDoor", f"ReturnHole{k}", x - 0.8 * IN, x + 0.8 * IN, rz - 0.45 * IN, rz + 0.45 * IN, YD - 0.185 * IN, M["rubber"])
revolve("CoinDoor", "Lock", [(0, 0), (0.42 * IN, 0), (0.42 * IN, 0.35 * IN), (0.36 * IN, 0.42 * IN), (0, 0.42 * IN)], (4.6 * IN, YD, cz - 0.6 * IN), (0, -1, 0), M["chrome"])
front_quad("CoinDoor", "LockSlot", 4.6 * IN - 0.05 * IN, 4.6 * IN + 0.05 * IN, cz - 0.85 * IN, cz - 0.35 * IN, YD - 0.425 * IN, M["rubber"])


# ---------------------------------------------------------------------------------------------------------------------------
# Buttons: flipper buttons on the sides, start button on the front (rigid: it sits where the front is stretched for wide tables)

def button(node_bezel, node_button, name, center, axis, cap_mat, d_button=1.1 * IN, d_bezel=1.5 * IN, travel=0.5 * IN, dome=0.14 * IN):
    rb, rz = d_button / 2, d_bezel / 2
    revolve(node_bezel, name + "Bezel", [(rb * 1.02, 0), (rz, 0), (rz, 0.06 * IN), (rz * 0.9, 0.13 * IN), (rb * 1.02, 0.13 * IN)], center, axis, M["chrome"])
    prof = [(0, 0), (rb, 0), (rb, travel - dome * 0.6)] + [(rb * math.cos(t), travel - dome + dome * math.sin(t) + dome * 0.4) for t in np.linspace(0.3, math.pi / 2, 6)]
    revolve(node_button, name, prof, center, axis, cap_mat)

button("Trim", "FlipperLeft", "FlipperLeft", (-XO, D.FLIPPER_Y, D.FLIPPER_Z), (-1, 0, 0), M["flipper"])
button("Trim", "FlipperRight", "FlipperRight", (XO, D.FLIPPER_Y, D.FLIPPER_Z), (1, 0, 0), M["flipper"])
button("StartButton", "StartButton", "Start", (D.START_X, YF, D.START_Z), (0, -1, 0), M["start"], d_button=1.0 * IN, d_bezel=1.45 * IN, travel=0.35 * IN)


# ---------------------------------------------------------------------------------------------------------------------------
# Ball launchers, built at the nominal shooter lane (dims.PLUNGER_X) and moved by the application to the table's plunger

PX, PZ = D.PLUNGER_X, D.PLUNGER_Z
slope = math.radians(D.SLOPE)
out_dir = Vector((0, -math.cos(slope), -math.sin(slope)))  # The rod comes out along the playfield slope, down toward the player
hole = Vector((PX, YF, PZ))

def plate_bolts(node, prefix, center, w):
    for k, x in enumerate((-w, 0.0, w)):
        z = 0.62 * IN if k == 1 else -0.45 * IN
        revolve(node, f"{prefix}Bolt{k}", [(0, 0), (0.2 * IN, 0), (0.17 * IN, 0.07 * IN), (0, 0.1 * IN)], (center.x + x, center.y - 0.14 * IN, center.z + z), (0, -1, 0), M["chrome"], segments=16)

# Spring plunger: housing plate with three bolts, outer sleeve, rod, exterior spring, knob
plate_on_front("PlungerHousing", "PlungerPlate", rounded_rect(2.9 * IN, 1.9 * IN, 0.7 * IN, 8), (PX, YF, PZ + 0.05 * IN), 0.14 * IN, M["chrome"], bevel=0.02)
plate_bolts("PlungerHousing", "PlungerPlate", Vector((PX, YF, PZ + 0.05 * IN)), 1.0 * IN)
sleeve0 = hole + out_dir * (0.14 * IN / math.cos(slope))
revolve("PlungerHousing", "PlungerSleeve", [(0, 0), (0.42 * IN, 0), (0.42 * IN, 0.45 * IN), (0.36 * IN, 0.55 * IN), (0, 0.55 * IN)], sleeve0, out_dir, M["chrome"])
spring0 = sleeve0 + out_dir * 0.55 * IN
SPRING_L = 1.7 * IN
helix("PlungerSpring", "PlungerSpring", spring0, out_dir, 0.27 * IN, 0.035 * IN, SPRING_L, 9, M["chrome"])
knob0 = spring0 + out_dir * SPRING_L
revolve("PlungerRod", "PlungerRodBar", [(0, -2.5 * IN), (0.19 * IN, -2.5 * IN), (0.19 * IN, 0.2 * IN), (0, 0.2 * IN)], knob0, out_dir, M["chrome"], segments=16)
revolve("PlungerRod", "PlungerKnob", [(0, 0), (0.34 * IN, 0), (0.42 * IN, 0.15 * IN), (0.48 * IN, 0.9 * IN), (0.6 * IN, 1.7 * IN), (0.62 * IN, 1.85 * IN),
                                      (0.56 * IN, 2.02 * IN), (0.4 * IN, 2.12 * IN), (0, 2.16 * IN)], knob0, out_dir, M["knob"])

# Launch button: the same kind of plate, a chrome bezel and a big domed button
plate_on_front("LaunchHousing", "LaunchPlate", rounded_rect(2.9 * IN, 1.9 * IN, 0.7 * IN, 8), (PX, YF, PZ + 0.05 * IN), 0.14 * IN, M["black"], bevel=0.02)
plate_bolts("LaunchHousing", "LaunchPlate", Vector((PX, YF, PZ + 0.05 * IN)), 1.0 * IN)
launch_c = Vector((PX, YF - 0.14 * IN, PZ + 0.05 * IN))
button("LaunchHousing", "LaunchButton", "Launch", launch_c, (0, -1, 0), M["launch"], d_button=1.3 * IN, d_bezel=1.75 * IN, travel=0.55 * IN, dome=0.25 * IN)


# ---------------------------------------------------------------------------------------------------------------------------
# Backbox: art sides, black top, back and bottom; the front holds the speaker panel (display window) and the backglass

ZB, YB = D.Z_BB, D.Y_BACK
BXO, BXI = D.BB_W / 2, D.BB_W / 2 - D.WALL
bb_profile = [(YB, ZB), (YB - D.BB_D_BOTTOM, ZB), (YB - D.BB_D_TOP, ZB + D.BB_H), (YB, ZB + D.BB_H)]
bb_rect = (YB - D.BB_D_TOP, ZB, D.BB_D_TOP, D.BB_H)
# Both sides take the art the same way (front of the image at the front): the left one reads mirrored, as on real machines
slab_x("Backbox", "BBSideLeft", bb_profile, -BXO, -BXI, M["art_backbox"], M["interior"], M["black"], (*bb_rect, False))
slab_x("Backbox", "BBSideRight", bb_profile, BXO, BXI, M["art_backbox"], M["interior"], M["black"], (*bb_rect, False))
box("Backbox", "BBTop", (-BXI, YB - D.BB_D_TOP, ZB + D.BB_H - D.WALL), (BXI, YB, ZB + D.BB_H), M["black"])
box("Backbox", "BBBack", (-BXI, YB - D.WALL, ZB + D.WALL), (BXI, YB, ZB + D.BB_H - D.WALL), M["black"])
box("Backbox", "BBBottom", (-BXI, YB - D.BB_D_BOTTOM, ZB), (BXI, YB, ZB + D.WALL), M["black"])

lean = D.BB_LEAN
t_up = Vector((0, -math.sin(lean), math.cos(lean)))       # Up along the front
n_out = Vector((0, -math.cos(lean), -math.sin(lean)))     # Out of the front, toward the player
front0 = Vector((0, YB - D.BB_D_BOTTOM, ZB))             # Bottom of the front, in the middle

def on_front(x, v, inset):
    return front0 + Vector((x, 0, 0)) + t_up * v - n_out * inset

def front_rect(node, name, x0, x1, v0, v1, inset, mat, uv=(0, 1, 0, 1), role=None):
    bm = bmesh.new()
    vs = [bm.verts.new(on_front(x, v, inset)) for x, v in ((x0, v0), (x1, v0), (x1, v1), (x0, v1))]
    f = bm.faces.new(vs)
    bm.normal_update()
    if f.normal.dot(n_out) < 0:
        f.normal_flip()
    lay = bm.loops.layers.uv.verify()
    u0, u1, w0, w1 = uv
    for loop in f.loops:
        p = loop.vert.co - front0
        x, v = p.x, p.dot(t_up)
        loop[lay].uv = (u0 + (u1 - u0) * (x - x0) / (x1 - x0), w0 + (w1 - w0) * (v - v0) / (v1 - v0))
    return new_object(node, name, bm, mat)

FH = D.BB_H / math.cos(lean)  # Length of the front, bottom to top
sp0, sp1 = D.BB_SPEAKER
tl0, tl1 = D.BB_TRANSLITE
front_rect("Backbox", "SpeakerPanel", -BXI, BXI, sp0, sp1, 0.3 * IN, M["speaker"])
front_rect("Backbox", "Backglass", -D.TRANSLITE_W / 2, D.TRANSLITE_W / 2, tl0, tl1, 0.5 * IN, M["backglass"])
# Black frame around the backglass, chrome channel between the panels, black strips at the bottom and the top of the front
front_rect("Backbox", "TransliteFrameL", -BXI, -D.TRANSLITE_W / 2, tl0, tl1, 0.45 * IN, M["black"])
front_rect("Backbox", "TransliteFrameR", D.TRANSLITE_W / 2, BXI, tl0, tl1, 0.45 * IN, M["black"])
front_rect("Backbox", "Channel", -BXI, BXI, sp1, tl0, 0.2 * IN, M["chrome"])
front_rect("Backbox", "BottomStrip", -BXI, BXI, 0, sp0, 0.0, M["black"])
front_rect("Backbox", "TopStrip", -BXI, BXI, tl1, FH, 0.0, M["black"])
# Display window, dark glass in front of the speaker panel; the application draws the display in front of it
dx0, dv0, dx1, dv1 = -D.DMD_W / 2, sp0 + (SPK_H - D.DMD_H) / 2 + 0.1 * IN, D.DMD_W / 2, sp0 + (SPK_H + D.DMD_H) / 2 + 0.1 * IN
front_rect("Backbox", "DisplayGlass", dx0, dx1, dv0, dv1, 0.28 * IN, M["glass"])

# Hinge brackets at the back of the backbox
for sx in (-1, 1):
    x0, x1 = sorted((sx * BXO, sx * (BXO + 0.15 * IN)))
    box("Backbox", f"Hinge{sx}", (x0, YB - 1.6 * IN, ZB - 0.3 * IN), (x1, YB + 0.05 * IN, ZB + 3.0 * IN), M["steel"])

# Rectangles where the application shows the table's backglass and display (4 corners, counter clockwise from the bottom left)
RECTS = {
    "BackglassRect": ("backglass", [on_front(x, v, 0.48 * IN) for x, v in ((-D.TRANSLITE_W / 2, tl0), (D.TRANSLITE_W / 2, tl0), (D.TRANSLITE_W / 2, tl1), (-D.TRANSLITE_W / 2, tl1))]),
    "DisplayRect": ("display", [on_front(x, v, 0.26 * IN) for x, v in ((dx0, dv0), (dx1, dv0), (dx1, dv1), (dx0, dv1))]),
}


# ---------------------------------------------------------------------------------------------------------------------------
# Nodes: parts joined per node, with the extras of the application

NODES = {
    "Body": dict(fit="deform", space="cabinet"),
    "Lockbar": dict(fit="deform", space="cabinet"),
    "SideRails": dict(fit="deform", space="cabinet"),
    "Trim": dict(fit="deform", space="cabinet"),
    "CoinDoor": dict(fit="deform", space="cabinet"),
    "FlipperLeft": dict(fit="deform", space="cabinet", role="flipper_left"),
    "FlipperRight": dict(fit="deform", space="cabinet", role="flipper_right"),
    "StartButton": dict(fit="rigid", space="cabinet", anchor=[D.START_X, YF, D.START_Z], role="start_button"),
    "Backbox": dict(fit="deform", space="cabinet"),
    "Legs": dict(fit="deform", space="feet"),
    "PlungerHousing": dict(fit="rigid", space="cabinet", anchor=list(hole), variant="spring"),
    "PlungerSpring": dict(fit="rigid", space="cabinet", anchor=list(hole), variant="spring", role="plunger_spring"),
    "PlungerRod": dict(fit="rigid", space="cabinet", anchor=list(hole), variant="spring", role="plunger_rod"),
    "LaunchHousing": dict(fit="rigid", space="cabinet", anchor=list(hole), variant="button"),
    "LaunchButton": dict(fit="rigid", space="cabinet", anchor=list(hole), variant="button", role="launch_button"),
}

exported = []
for node, props in NODES.items():
    objs = [o for n, o in PARTS if n == node]
    if not objs:
        continue
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = obj.data.name = node
    for k, v in props.items():
        obj["vpx_" + k] = v
    exported.append(obj)
# Plunger travel and spring length, for the animation
bpy.data.objects["PlungerRod"]["vpx_axis"] = list(out_dir)
bpy.data.objects["PlungerSpring"]["vpx_axis"] = list(out_dir)
bpy.data.objects["PlungerSpring"]["vpx_spring_start"] = list(spring0)
bpy.data.objects["PlungerSpring"]["vpx_spring_length"] = SPRING_L
bpy.data.objects["LaunchButton"]["vpx_axis"] = [0.0, -1.0, 0.0]
for name, (role, corners) in RECTS.items():
    e = bpy.data.objects.new(name, None)
    scene.collection.objects.link(e)
    e["vpx_role"] = role
    e["vpx_space"] = "cabinet"
    e["vpx_fit"] = "deform"
    e["vpx_corners"] = [c for p in corners for c in p]
    exported.append(e)
scene["vpx_cabinet"] = json.dumps(D.nominal())

tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in exported if o.type == "MESH")
print(f"CABINET nodes={len([o for o in exported if o.type == 'MESH'])} triangles={tris}")
for o in exported:
    if o.type == "MESH":
        print(f"  {o.name}: {sum(len(p.vertices) - 2 for p in o.data.polygons)} tris, materials {[m.name for m in o.data.materials]}")

if EXPORT:
    bpy.ops.object.select_all(action="DESELECT")
    for o in exported:
        o.select_set(True)
    path = os.path.join(ASSET, "vr-cabinet.glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_extras=True, export_yup=False,
                              export_apply=True, export_image_format="NONE", export_materials="EXPORT")
    print("CABINET exported", path, os.path.getsize(path))
if SAVE_BLEND:
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(WORK, "cabinet.blend"))
