# Builds the "game room" lobby in Blender: a lounge at night with arcade cabinets, a pinball machine and a city view, made from CC0
# Poly Haven assets and procedural models and artwork (gameroom_art.py), and saves it as a .blend.
#
#   Blender -b --factory-startup --python build_gameroom.py -- <downloads dir> <output .blend>
#
# Layout, in meters, in Blender axes: the player stands at the origin and faces +Y (the VR menu panel shows up there), Z up. The front
# wall is mostly a window on a night city (an HDRI, shown on a distant backdrop baked from the player's eye), arcade cabinets line the
# left wall under a warm LED ledge, a lounge with a sofa and a pinball machine is on the right, the door on the wall behind.
import bpy, bmesh, json, math, os, sys
from mathutils import Vector, Matrix

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import roomkit as rk
import gameroom_art as art

args = sys.argv[sys.argv.index("--") + 1:]
scene = rk.init(args[0])
OUT = os.path.abspath(args[1])
FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "src", "assets", "LiberationSans-Regular.ttf")

X0, X1, Y0, Y1, H = -4.5, 4.5, -3.0, 5.5, 3.0
EYE = Vector((0.0, 0.0, 1.65))
WX0, WX1, WZ1, WALL_T = -1.0, 4.1, 2.85, 0.2  # Window opening in the front wall, from the floor
INSIDE = (0.0, 1.0, 1.5)

# Texture size of each bake group, and the groups that only emit light (no lighting to bake)
scene["bake_sizes"] = json.dumps({"floor": 4096, "walls": 4096, "ceiling": 2048, "arcades": 4096, "pinball": 2048, "furniture": 4096,
                                  "exterior": 2048, "backdrop": 4096})
scene["bake_emit_only"] = json.dumps(["backdrop"])


# ---------------------------------------------------------------------------------------------------------------------------
# Materials

M_FLOOR = rk.pbr_material("Floor", "herringbone_parquet", scale=(2.0, 2.0), tint=(0.85, 0.7, 0.55), roughness_mul=2.0)
M_WALL = rk.pbr_material("Wall", "plastered_wall_04", scale=(2.5, 2.5), tint=(1.05, 1.0, 0.95))
M_CEILING = rk.flat_material("Ceiling", (0.32, 0.32, 0.33), 0.9)
M_BLACK = rk.flat_material("BlackMatte", (0.02, 0.02, 0.022), 0.6)
M_STEEL = rk.flat_material("BlackSteel", (0.03, 0.03, 0.03), 0.35, 1.0)
M_CHROME = rk.flat_material("Chrome", (0.85, 0.85, 0.87), 0.18, 1.0)
M_OAK = rk.pbr_material("Oak", "oak_veneer_01", scale=(0.8, 0.8), tint=(0.85, 0.68, 0.5), roughness_mul=0.7)
M_DOOR = rk.pbr_material("Door", "dark_wooden_planks", scale=(1.2, 1.2), tint=(0.8, 0.7, 0.62))
M_RUG = rk.pbr_material("Rug", "quatrefoil_jacquard_fabric", scale=(0.45, 0.45), tint=(0.55, 0.5, 0.5))
M_CUSHION = rk.pbr_material("StoolLeather", "leather_red_02", scale=(0.3, 0.3), tint=(1.0, 0.55, 0.3))
M_PLANTER = rk.flat_material("Planter", (0.05, 0.05, 0.05), 0.5)
M_SOIL = rk.flat_material("Soil", (0.04, 0.03, 0.02), 1.0)
M_TERRACE = rk.flat_material("Terrace", (0.07, 0.07, 0.075), 0.7)
M_PARAPET = rk.flat_material("Parapet", (0.18, 0.18, 0.19), 0.85)
M_LED_WARM = rk.emissive_material("LedAmber", (1.0, 0.55, 0.2), 25.0)
M_SPOT_LENS = rk.emissive_material("SpotLens", (1.0, 0.9, 0.75), 30.0)
M_COIN = rk.emissive_material("CoinSlot", (1.0, 0.1, 0.05), 6.0)
M_NEON_PINK = rk.emissive_material("NeonPink", (1.0, 0.12, 0.55), 25.0)
M_NEON_CYAN = rk.emissive_material("NeonCyan", (0.1, 0.75, 1.0), 25.0)
M_SOFA = rk.pbr_material("SofaFabric", "jersey_melange", scale=(0.4, 0.4), tint=(0.42, 0.42, 0.45))


# ---------------------------------------------------------------------------------------------------------------------------
# Shell: floor, walls with the window opening and the door, ceiling

rk.quad("Floor", [(X0, Y0, 0), (X1, Y0, 0), (X1, Y1, 0), (X0, Y1, 0)], M_FLOOR, "floor", uv_scale=1.0, toward=INSIDE)

def wall_with_openings(name, plane, openings, toward):
    """A wall as a set of quads around rectangular openings: `plane` maps (u, v) to a point, the wall spans u0..u1 x 0..H."""
    u0, u1, to_point = plane
    bm = bmesh.new()
    cuts = sorted(openings)
    # Columns between and around the openings, full height, and the parts above and below each opening
    edges = [u0] + [c for o in cuts for c in (o[0], o[1])] + [u1]
    for i in range(0, len(edges), 2):
        a, b = edges[i], edges[i + 1]
        if b - a > 1e-4:
            bm.faces.new([bm.verts.new(to_point(u, v)) for u, v in ((a, 0), (b, 0), (b, H), (a, H))])
    for (a, b, v0, v1) in cuts:
        if v1 < H:
            bm.faces.new([bm.verts.new(to_point(u, v)) for u, v in ((a, v1), (b, v1), (b, H), (a, H))])
        if v0 > 0:
            bm.faces.new([bm.verts.new(to_point(u, v)) for u, v in ((a, 0), (b, 0), (b, v0), (a, v0))])
    rk.face_toward(bm, toward)
    rk.box_uv(bm)
    return rk.new_object(name, bm, M_WALL, "walls")

wall_with_openings("WallFront", (X0, X1, lambda u, v: (u, Y1, v)), [(WX0, WX1, 0.0, WZ1)], INSIDE)
DOOR_X, DOOR_W, DOOR_H = 2.6, 1.0, 2.2
wall_with_openings("WallBack", (X0, X1, lambda u, v: (u, Y0, v)), [(DOOR_X - DOOR_W / 2, DOOR_X + DOOR_W / 2, 0.0, DOOR_H)], INSIDE)
rk.quad("WallLeft", [(X0, Y1, 0), (X0, Y0, 0), (X0, Y0, H), (X0, Y1, H)], M_WALL, "walls", toward=INSIDE)
rk.quad("WallRight", [(X1, Y0, 0), (X1, Y1, 0), (X1, Y1, H), (X1, Y0, H)], M_WALL, "walls", toward=INSIDE)
rk.quad("Ceiling", [(X0, Y0, H), (X0, Y1, H), (X1, Y1, H), (X1, Y0, H)], M_CEILING, "ceiling", toward=INSIDE)

# Reveals of the window opening (the wall's thickness) and its black frame with mullions
bm = bmesh.new()
for c in (((WX0, Y1, 0), (WX0, Y1 + WALL_T, 0), (WX0, Y1 + WALL_T, WZ1), (WX0, Y1, WZ1)),
          ((WX1, Y1, 0), (WX1, Y1 + WALL_T, 0), (WX1, Y1 + WALL_T, WZ1), (WX1, Y1, WZ1)),
          ((WX0, Y1, WZ1), (WX1, Y1, WZ1), (WX1, Y1 + WALL_T, WZ1), (WX0, Y1 + WALL_T, WZ1))):
    bm.faces.new([bm.verts.new(p) for p in c])
rk.face_toward(bm, ((WX0 + WX1) / 2, Y1 + WALL_T / 2, WZ1 / 2))
rk.box_uv(bm)
rk.new_object("WindowReveal", bm, M_WALL, "walls")
FY = Y1 + 0.12  # Plane of the glazing
rk.box("FrameBottom", (WX0, FY - 0.03, 0), (WX1, FY + 0.03, 0.07), M_STEEL, "walls", skip=("-z",))
rk.box("FrameTop", (WX0, FY - 0.03, WZ1 - 0.05), (WX1, FY + 0.03, WZ1), M_STEEL, "walls", skip=("+z",))
for i in range(5):
    x = WX0 + i * (WX1 - WX0) / 4
    rk.box(f"Mullion{i}", (x - 0.03, FY - 0.03, 0.07), (x + 0.03, FY + 0.03, WZ1 - 0.05), M_STEEL, "walls")
rk.box("Door", (DOOR_X - DOOR_W / 2, Y0 - 0.06, 0), (DOOR_X + DOOR_W / 2, Y0 - 0.02, DOOR_H), M_DOOR, "walls", skip=("-y",))
bm = bmesh.new()
for c in (((DOOR_X - DOOR_W / 2, Y0, 0), (DOOR_X - DOOR_W / 2, Y0 - 0.06, 0), (DOOR_X - DOOR_W / 2, Y0 - 0.06, DOOR_H), (DOOR_X - DOOR_W / 2, Y0, DOOR_H)),
          ((DOOR_X + DOOR_W / 2, Y0, 0), (DOOR_X + DOOR_W / 2, Y0 - 0.06, 0), (DOOR_X + DOOR_W / 2, Y0 - 0.06, DOOR_H), (DOOR_X + DOOR_W / 2, Y0, DOOR_H)),
          ((DOOR_X - DOOR_W / 2, Y0, DOOR_H), (DOOR_X + DOOR_W / 2, Y0, DOOR_H), (DOOR_X + DOOR_W / 2, Y0 - 0.06, DOOR_H), (DOOR_X - DOOR_W / 2, Y0 - 0.06, DOOR_H))):
    bm.faces.new([bm.verts.new(p) for p in c])
rk.face_toward(bm, (DOOR_X, Y0 - 0.03, 1.0))
rk.box_uv(bm)
rk.new_object("DoorReveal", bm, M_WALL, "walls")
rk.box("DoorHandle", (DOOR_X - DOOR_W / 2 + 0.06, Y0 - 0.02, 1.0), (DOOR_X - DOOR_W / 2 + 0.18, Y0 + 0.03, 1.03), M_CHROME, "walls")
for name, lo, hi in (("SkirtLeft", (X0, Y0, 0), (X0 + 0.012, Y1, 0.08)), ("SkirtRight", (X1 - 0.012, Y0, 0), (X1, Y1, 0.08)),
                     ("SkirtFrontL", (X0, Y1 - 0.012, 0), (WX0, Y1, 0.08)), ("SkirtFrontR", (WX1, Y1 - 0.012, 0), (X1, Y1, 0.08)),
                     ("SkirtBackL", (X0, Y0, 0), (DOOR_X - DOOR_W / 2, Y0 + 0.012, 0.08)), ("SkirtBackR", (DOOR_X + DOOR_W / 2, Y0, 0), (X1, Y0 + 0.012, 0.08))):
    rk.box(name, lo, hi, M_BLACK, "walls", skip=("-z",))


# ---------------------------------------------------------------------------------------------------------------------------
# Outside: a terrace with a parapet, and the city on a distant backdrop

rk.quad("Terrace", [(WX0 - 3, Y1, -0.02), (WX1 + 3, Y1, -0.02), (WX1 + 3, Y1 + 3.2, -0.02), (WX0 - 3, Y1 + 3.2, -0.02)], M_TERRACE, "exterior")
rk.box("Parapet", (WX0 - 3, Y1 + 3.2, -0.02), (WX1 + 3, Y1 + 3.4, 1.15), M_PARAPET, "exterior", skip=("-z", "+y"))

HDRI = os.path.join(rk.DL, "shanghai_bund", "shanghai_bund_8k.hdr")
HDRI_ROTATION = math.radians(215)  # Turns the skyline of the panorama to face the window
HDRI_STRENGTH = 0.5

def environment_nodes(tree, vector_socket):
    """Environment texture of the city, looked up along a direction (the same mapping for the world and the backdrop)."""
    mapping = tree.nodes.new("ShaderNodeMapping")
    mapping.inputs["Rotation"].default_value = (0, 0, HDRI_ROTATION)
    tree.links.new(vector_socket, mapping.inputs["Vector"])
    env = tree.nodes.new("ShaderNodeTexEnvironment")
    env.image = bpy.data.images.load(HDRI, check_existing=True)
    tree.links.new(mapping.outputs["Vector"], env.inputs["Vector"])
    return env

world = bpy.data.worlds.new("City")
world.use_nodes = True
wt = world.node_tree
env = environment_nodes(wt, wt.nodes.new("ShaderNodeTexCoord").outputs["Generated"])
wt.links.new(env.outputs["Color"], wt.nodes["Background"].inputs["Color"])
wt.nodes["Background"].inputs["Strength"].default_value = HDRI_STRENGTH
scene.world = world

# The backdrop shows, from the player's eye, what the world shows in each direction: an emissive cylinder 70 m away
backdrop = bpy.data.materials.new("Backdrop")
backdrop.use_nodes = True
bt = backdrop.node_tree
bsdf = bt.nodes["Principled BSDF"]
bsdf.inputs["Base Color"].default_value = (0, 0, 0, 1)
geo = bt.nodes.new("ShaderNodeNewGeometry")
sub = bt.nodes.new("ShaderNodeVectorMath")
sub.operation = "SUBTRACT"
sub.inputs[1].default_value = EYE
bt.links.new(geo.outputs["Position"], sub.inputs[0])
# The panorama was taken at ground level, with a crowd along the river in front of the skyline: the backdrop looks it up a few
# degrees higher than the real direction, which sinks the crowd below the parapet and keeps the towers
HORIZON_LIFT = math.tan(math.radians(5))
sep = bt.nodes.new("ShaderNodeSeparateXYZ")
bt.links.new(sub.outputs["Vector"], sep.inputs[0])
flat = bt.nodes.new("ShaderNodeCombineXYZ")
bt.links.new(sep.outputs["X"], flat.inputs["X"])
bt.links.new(sep.outputs["Y"], flat.inputs["Y"])
horizontal = bt.nodes.new("ShaderNodeVectorMath")
horizontal.operation = "LENGTH"
bt.links.new(flat.outputs["Vector"], horizontal.inputs[0])
lift = bt.nodes.new("ShaderNodeMath")
lift.operation = "MULTIPLY_ADD"
lift.inputs[1].default_value = HORIZON_LIFT
bt.links.new(horizontal.outputs["Value"], lift.inputs[0])
bt.links.new(sep.outputs["Z"], lift.inputs[2])
lifted = bt.nodes.new("ShaderNodeCombineXYZ")
bt.links.new(sep.outputs["X"], lifted.inputs["X"])
bt.links.new(sep.outputs["Y"], lifted.inputs["Y"])
bt.links.new(lift.outputs["Value"], lifted.inputs["Z"])
norm = bt.nodes.new("ShaderNodeVectorMath")
norm.operation = "NORMALIZE"
bt.links.new(lifted.outputs["Vector"], norm.inputs[0])
env = environment_nodes(bt, norm.outputs["Vector"])
bt.links.new(env.outputs["Color"], bsdf.inputs["Emission Color"])
bsdf.inputs["Emission Strength"].default_value = HDRI_STRENGTH
R, SEG = 70.0, 40
bm = bmesh.new()
angles = [math.radians(-80 + 160 * i / SEG) for i in range(SEG + 1)]
ring = [(EYE.x + R * math.sin(a), EYE.y + R * math.cos(a)) for a in angles]
for i in range(SEG):
    (xa, ya), (xb, yb) = ring[i], ring[i + 1]
    bm.faces.new([bm.verts.new(p) for p in ((xa, ya, -30), (xb, yb, -30), (xb, yb, 45), (xa, ya, 45))])
rk.face_toward(bm, EYE)
rk.box_uv(bm)
rk.new_object("Backdrop", bm, backdrop, "backdrop")


# ---------------------------------------------------------------------------------------------------------------------------
# Left wall: a ledge with warm LED strips above and below, and four arcade cabinets

LEDGE_Z = 2.25
rk.box("Ledge", (X0, Y0 + 0.4, LEDGE_Z), (X0 + 0.14, Y1 - 0.3, LEDGE_Z + 0.04), M_BLACK, "walls", skip=("-x",))
rk.box("LedgeLedDown", (X0 + 0.09, Y0 + 0.45, LEDGE_Z - 0.006), (X0 + 0.12, Y1 - 0.35, LEDGE_Z), M_LED_WARM, "walls", skip=("+z",))
rk.box("LedgeLedUp", (X0 + 0.02, Y0 + 0.45, LEDGE_Z + 0.04), (X0 + 0.05, Y1 - 0.35, LEDGE_Z + 0.046), M_LED_WARM, "walls", skip=("-z",))
ledge_len = Y1 - Y0 - 0.8
rk.area("LedgeWashDown", (X0 + 0.1, (Y0 + Y1) / 2 + 0.05, LEDGE_Z - 0.01), 0.04, 350, (1.0, 0.55, 0.22), size_y=ledge_len)
rk.area("LedgeWashUp", (X0 + 0.04, (Y0 + Y1) / 2 + 0.05, LEDGE_Z + 0.05), 0.04, 120, (1.0, 0.55, 0.22), rotation=(math.pi, 0, 0), size_y=ledge_len)

CAB_W, CAB_D, CAB_H = 0.68, 0.82, 1.80
CAB_PROFILE = [(0, 0), (0.72, 0), (0.72, 0.86), (0.82, 0.90), (0.82, 0.98), (0.56, 1.05), (0.54, 1.09), (0.38, 1.52), (0.44, 1.56), (0.44, 1.80), (0, 1.80)]

def viewer_quad(name, x_half, a, b, offset, normal, material, group):
    """A quad facing the viewer standing in front of the cabinet (at +y, so that their right is -x), from the bottom edge point
    `a` to the top edge point `b` (each (y, z)), pushed by `offset` along `normal`, with 0..1 UVs (u to their right, v up)."""
    ay, az = a[0] + normal[0] * offset, a[1] + normal[1] * offset
    by, bz = b[0] + normal[0] * offset, b[1] + normal[1] * offset
    return rk.quad(name, [(x_half, ay, az), (-x_half, ay, az), (-x_half, by, bz), (x_half, by, bz)], material, group, uv01=True)

def along(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)

def slant_normal(a, b):
    dy, dz = b[0] - a[0], b[1] - a[1]
    n = math.hypot(dy, dz)
    return (dz / n, -dy / n)

def arcade(index, y_center):
    objs = []
    side = rk.image_material(f"CabSide{index}", art.side_art(*art.SIDE_ARTS[index]), roughness=0.4)
    objs.append(rk.prism(f"Cab{index}", CAB_PROFILE, -CAB_W / 2, CAB_W / 2, M_BLACK, side, "arcades", cap_uv=lambda y, z: (y / CAB_D, z / CAB_H)))
    # Screen in the slanted face, marquee at the top, both lights
    sa, sb = (0.54, 1.09), (0.38, 1.52)
    n = slant_normal(sa, sb)
    screen = rk.image_material(f"Screen{index}", art.SCREENS[index](), emission=2.2)
    objs.append(viewer_quad(f"CabScreen{index}", CAB_W / 2 - 0.07, along(sa, sb, 0.09), along(sa, sb, 0.93), 0.003, n, screen, "arcades"))
    marquee = rk.image_material(f"Marquee{index}", art.marquee(*art.MARQUEES[index]), emission=3.0)
    objs.append(viewer_quad(f"CabMarquee{index}", CAB_W / 2 - 0.02, (0.44, 1.58), (0.44, 1.78), 0.003, (1, 0), marquee, "arcades"))
    # Control panel, with a joystick and buttons
    ca, cb = (0.82, 0.98), (0.56, 1.05)
    cn = slant_normal(ca, cb)
    panel_color = art.hexrgb(art.SIDE_ARTS[index][1]) * 0.6
    panel = rk.flat_material(f"Panel{index}", tuple(panel_color ** 2.2), 0.4)
    objs.append(viewer_quad(f"CabPanel{index}", CAB_W / 2 - 0.03, ca, cb, 0.002, cn, panel, "arcades"))
    py, pz = along(ca, cb, 0.5)
    objs.append(rk.cylinder(f"CabStick{index}", (0.14, py, pz + 0.03), 0.007, 0.06, M_BLACK, "arcades", segments=8))
    objs.append(rk.cylinder(f"CabBall{index}", (0.14, py, pz + 0.065), 0.02, 0.03, rk.flat_material(f"Ball{index}", (0.8, 0.03, 0.03), 0.3), "arcades", segments=12, radius_top=0.012))
    button = rk.flat_material(f"Button{index}", tuple(art.hexrgb(art.SIDE_ARTS[index][2]) ** 2.2), 0.3)
    for j, (bx, t) in enumerate(((0.0, 0.42), (-0.06, 0.38), (-0.12, 0.36), (0.02, 0.62), (-0.04, 0.58), (-0.10, 0.56))):
        by_, bz_ = along(ca, cb, t)
        objs.append(rk.cylinder(f"CabButton{index}_{j}", (bx, by_, bz_ + 0.006), 0.014, 0.014, button, "arcades", segments=12))
    # Coin door with its two lit slots
    objs.append(rk.box(f"CabCoinDoor{index}", (-0.12, 0.72, 0.40), (0.12, 0.735, 0.70), M_STEEL, "arcades", skip=("-y",)))
    for x in (-0.07, 0.03):
        objs.append(rk.box(f"CabCoin{index}_{x}", (x, 0.735, 0.60), (x + 0.04, 0.74, 0.64), M_COIN, "arcades", skip=("-y",)))
    rk.transform(objs, Matrix.Translation((X0 + 0.04, y_center, 0)) @ Matrix.Rotation(math.radians(-90), 4, "Z"))

for i, y in enumerate((0.85, 1.72, 2.59, 3.46)):
    arcade(i, y)


# ---------------------------------------------------------------------------------------------------------------------------
# Right: the pinball machine, against the wall near the window, and the lounge

def pinball(x_center, y_center):
    objs = []
    L, W = 0.66, 0.36
    side = rk.image_material("PinSide", art.pinball_side(), roughness=0.35)
    body = [(-L, 0.66), (L, 0.66), (L, 0.92), (-L, 1.04)]
    objs.append(rk.prism("PinBody", body, -W, W, M_BLACK, side, "pinball", cap_uv=lambda y, z: ((y + L) / (2 * L), (z - 0.66) / 0.38)))
    top = lambda y: 0.92 + (L - y) / (2 * L) * 0.12
    playfield = rk.image_material("Playfield", art.playfield(), emission=0.7)
    objs.append(rk.quad("PinPlayfield", [(W - 0.05, 0.6, top(0.6) + 0.002), (-W + 0.05, 0.6, top(0.6) + 0.002), (-W + 0.05, -0.6, top(-0.6) + 0.002),
                                         (W - 0.05, -0.6, top(-0.6) + 0.002)], playfield, "pinball", uv01=True))
    for s in (-1, 1):
        objs.append(rk.box(f"PinRail{s}", (s * W - 0.03 if s > 0 else -W, -L, 0), (W if s > 0 else -W + 0.03, L, 0.02), M_CHROME, "pinball"))
        rail = objs[-1]
        for v in rail.data.vertices:  # Follow the slope of the top
            v.co.z = top(v.co.y) + v.co.z
        objs.append(rk.box(f"PinLeg{s}F", (s * 0.3 - 0.025, 0.55, 0), (s * 0.3 + 0.025, 0.6, 0.66), M_CHROME, "pinball", skip=("-z",)))
        objs.append(rk.box(f"PinLeg{s}B", (s * 0.3 - 0.025, -0.6, 0), (s * 0.3 + 0.025, -0.55, 0.66), M_CHROME, "pinball", skip=("-z",)))
    objs.append(rk.box("PinLockbar", (-W, L - 0.06, 0.92), (W, L + 0.01, 0.95), M_CHROME, "pinball"))
    objs.append(rk.box("PinBackbox", (-W, -L, 1.0), (W, -0.42, 1.84), M_BLACK, "pinball", skip=("-y",)))
    glass = rk.image_material("Backglass", art.backglass(), emission=2.4)
    objs.append(viewer_quad("PinBackglass", 0.31, (-0.417, 1.33), (-0.417, 1.795), 0.0, (1, 0), glass, "pinball"))
    dmd = rk.image_material("Display", art.dot_matrix(), emission=3.0)
    objs.append(viewer_quad("PinDisplay", 0.22, (-0.417, 1.12), (-0.417, 1.23), 0.0, (1, 0), dmd, "pinball"))
    objs.append(rk.box("PinCoinDoor", (-0.13, L, 0.70), (0.13, L + 0.012, 0.88), M_STEEL, "pinball", skip=("-y",)))
    for x in (-0.08, 0.04):
        objs.append(rk.box(f"PinCoin{x}", (x, L + 0.012, 0.80), (x + 0.04, L + 0.016, 0.84), M_COIN, "pinball", skip=("-y",)))
    rk.transform(objs, Matrix.Translation((x_center, y_center, 0)) @ Matrix.Rotation(math.radians(90), 4, "Z"))

pinball(X1 - 0.06 - 0.66, 4.0)

# Lounge: a grey sofa against the wall, two leather lounge chairs, an oak coffee table on a rug
def soft_box(name, lo, hi, material, group, radius=0.03):
    """A box with rounded edges, for cushions."""
    obj = rk.box(name, lo, hi, material, group)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.bevel(bm, geom=list(bm.edges), offset=radius, segments=2, affect="EDGES")
    rk.box_uv(bm)
    bm.to_mesh(obj.data)
    bm.free()
    return obj

SY0, SY1, SX = 0.15, 2.35, X1 - 0.04  # Sofa: along the right wall, facing the room
SM = (SY0 + SY1) / 2
for sy in (SY0 + 0.04, SY1 - 0.08):
    for sx in (SX - 0.82, SX - 0.06):
        rk.box(f"SofaLeg{sy:.1f}{sx:.1f}", (sx, sy, 0), (sx + 0.04, sy + 0.04, 0.08), M_STEEL, "furniture", skip=("-z",))
soft_box("SofaBase", (SX - 0.86, SY0, 0.08), (SX, SY1, 0.30), M_SOFA, "furniture", radius=0.02)
soft_box("SofaBack", (SX - 0.14, SY0 + 0.2, 0.30), (SX, SY1 - 0.2, 0.80), M_SOFA, "furniture")
for k, (a, b) in enumerate(((SY0 + 0.2, SM - 0.005), (SM + 0.005, SY1 - 0.2))):
    soft_box(f"SofaSeat{k}", (SX - 0.84, a, 0.30), (SX - 0.14, b, 0.45), M_SOFA, "furniture", radius=0.04)
    soft_box(f"SofaCushion{k}", (SX - 0.34, a + 0.01, 0.45), (SX - 0.14, b - 0.01, 0.80), M_SOFA, "furniture", radius=0.06)
for k, (a, b) in enumerate(((SY0, SY0 + 0.2), (SY1 - 0.2, SY1))):
    soft_box(f"SofaArm{k}", (SX - 0.86, a, 0.30), (SX, b, 0.62), M_SOFA, "furniture", radius=0.04)
rk.import_prop("throw_pillows_01", (SX - 0.4, SM, 0.45), rot_z_deg=90)
rk.import_prop("mid_century_lounge_chair", (1.7, 1.25, 0), rot_z_deg=-90)
rk.import_prop("mid_century_lounge_chair", (2.6, 2.75, 0), rot_z_deg=180)
rk.quad("Rug", [(1.1, 0.0, 0.004), (3.95, 0.0, 0.004), (3.95, 2.6, 0.004), (1.1, 2.6, 0.004)], M_RUG, "floor")
TX, TY = 2.65, 1.25
rk.box("TableTop", (TX - 0.35, TY - 0.62, 0.40), (TX + 0.35, TY + 0.62, 0.45), M_OAK, "furniture")
for sx in (-1, 1):
    for sy in (-1, 1):
        rk.box(f"TableLeg{sx}{sy}", (TX + sx * 0.3 - 0.025, TY + sy * 0.56 - 0.025, 0), (TX + sx * 0.3 + 0.025, TY + sy * 0.56 + 0.025, 0.40), M_STEEL, "furniture", skip=("-z", "+z"))
rk.import_prop("potted_plant_04", (TX - 0.1, TY + 0.35, 0.45))

# Window counter with two leather stools
CX0, CX1 = 0.7, 3.7
rk.box("Counter", (CX0, Y1 - 0.38, 0.98), (CX1, Y1 - 0.02, 1.02), M_OAK, "furniture")
for x in (CX0 + 0.15, CX1 - 0.15):
    rk.box(f"CounterLeg{x:.1f}", (x - 0.02, Y1 - 0.25, 0), (x + 0.02, Y1 - 0.21, 0.98), M_STEEL, "furniture", skip=("-z",))

def stool(name, x, y):
    rk.cylinder(name + "Seat", (x, y, 0.71), 0.2, 0.09, M_CUSHION, "furniture", segments=28, radius_top=0.19)
    for k in range(4):
        a = math.radians(45 + 90 * k)
        rk.box(f"{name}Leg{k}", (x + 0.13 * math.cos(a) - 0.012, y + 0.13 * math.sin(a) - 0.012, 0), (x + 0.13 * math.cos(a) + 0.012, y + 0.13 * math.sin(a) + 0.012, 0.66), M_STEEL, "furniture", skip=("-z",))
    bpy.ops.mesh.primitive_torus_add(major_radius=0.135, minor_radius=0.007, major_segments=24, minor_segments=6, location=(x, y, 0.3))
    ring = bpy.context.active_object
    ring.name = name + "Ring"
    ring.data.materials.append(M_STEEL)
    ring["bake_group"] = "furniture"

stool("Stool1", 1.5, Y1 - 0.75)
stool("Stool2", 2.9, Y1 - 0.7)

# Plants: a large one in the front left corner and next to the window, one by the door
def planter(name, x, y, radius=0.24, height=0.5):
    rk.cylinder(name, (x, y, height / 2), radius * 0.8, height, M_PLANTER, "furniture", segments=32, radius_top=radius)
    rk.cylinder(name + "Soil", (x, y, height - 0.03), radius - 0.02, 0.01, M_SOIL, "furniture", segments=24)
    return height - 0.03

for i, (x, y, pick, scale) in enumerate(((X0 + 0.5, Y1 - 0.55, 0, 2.4), (WX0 - 0.55, Y1 - 0.5, 1, 2.0), (DOOR_X - 1.0, Y0 + 0.45, 0, 2.0))):
    rk.import_prop("anthurium_botany_01", (x, y, planter(f"Planter{i}", x, y)), rot_z_deg=50 * i, scale=scale, pick=pick)

# Back wall: a dartboard
rk.import_prop("dartboard", (-1.4, Y0 + 0.02, 1.55), rot_z_deg=0)


# ---------------------------------------------------------------------------------------------------------------------------
# Neon sign hanging in the window: a ball and a flipper in cyan, and PLAY in pink

def neon_mesh(curve_obj, material, name):
    curve_obj.data.bevel_depth = 0.008
    curve_obj.data.bevel_resolution = 1
    curve_obj.data.resolution_u = 4
    deps = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(curve_obj.evaluated_get(deps))
    mesh.transform(curve_obj.matrix_world)
    bpy.data.objects.remove(curve_obj)
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    mesh.materials.clear()
    mesh.materials.append(material)
    obj["bake_group"] = "furniture"
    return obj

NEON = Matrix.Translation((2.75, FY - 0.15, 1.75)) @ Matrix.Rotation(math.radians(90), 4, "X")  # Text plane (x, y) to the window plane (x, z)
text = bpy.data.curves.new("NeonText", "FONT")
text.body = "PLAY"
text.font = bpy.data.fonts.load(FONT)
text.size = 0.32
text.align_x = "CENTER"
text.fill_mode = "NONE"
tobj = bpy.data.objects.new("NeonText", text)
scene.collection.objects.link(tobj)
tobj.matrix_world = NEON @ Matrix.Translation((0, -0.1, 0))
neon_mesh(tobj, M_NEON_PINK, "NeonPlay")

shape = bpy.data.curves.new("NeonShapes", "CURVE")
shape.dimensions = "2D"
shape.fill_mode = "NONE"
def poly(points, cyclic=True):
    spline = shape.splines.new("POLY")
    spline.points.add(len(points) - 1)
    for p, (x, y) in zip(spline.points, points):
        p.co = (x, y, 0, 1)
    spline.use_cyclic_u = cyclic
# Ball
poly([(-0.45 + 0.09 * math.cos(a), 0.32 + 0.09 * math.sin(a)) for a in [2 * math.pi * k / 20 for k in range(20)]])
# Flipper: a rounded tapered bat, pivot on the left
pivot, tip, r0, r1 = Vector((-0.2, 0.32)), Vector((0.45, 0.18)), 0.07, 0.03
d = (tip - pivot).normalized()
nrm = Vector((-d.y, d.x))
outline = [pivot + nrm * r0 * math.cos(a) - d * r0 * math.sin(a) for a in [math.pi * k / 10 for k in range(11)]]
outline += [tip - nrm * r1 * math.cos(a) + d * r1 * math.sin(a) for a in [math.pi * k / 8 for k in range(9)]]
poly([(p.x, p.y) for p in outline])
sobj = bpy.data.objects.new("NeonShapes", shape)
scene.collection.objects.link(sobj)
sobj.matrix_world = NEON
neon_mesh(sobj, M_NEON_CYAN, "NeonFlipper")
rk.point("NeonGlowPink", NEON @ Vector((0, -0.0, 0.1)), 40, (1.0, 0.15, 0.55), radius=0.3)
rk.point("NeonGlowCyan", NEON @ Vector((0.0, 0.3, 0.1)), 30, (0.1, 0.7, 1.0), radius=0.3)


# ---------------------------------------------------------------------------------------------------------------------------
# Ceiling: two black track rails with spotlights on the cabinets, the pinball and the lounge

def track(name, x, y0, y1, targets):
    rk.box(name, (x - 0.02, y0, H - 0.035), (x + 0.02, y1, H), M_STEEL, "ceiling", skip=("+z",))
    for k, (y, target) in enumerate(targets):
        pos = Vector((x, y, H - 0.09))
        direction = (Vector(target) - pos).normalized()
        rk.cylinder(f"{name}Stem{k}", (x, y, H - 0.06), 0.006, 0.05, M_STEEL, "ceiling", segments=6)
        head = rk.cylinder(f"{name}Head{k}", (0, 0, 0), 0.035, 0.13, M_STEEL, "ceiling", segments=16)
        lens = rk.cylinder(f"{name}Lens{k}", (0, 0, -0.066), 0.028, 0.002, M_SPOT_LENS, "ceiling", segments=16)
        rot = direction.to_track_quat("-Z", "Y").to_matrix().to_4x4()
        rk.transform([head, lens], Matrix.Translation(pos) @ rot)
        rk.spot(f"{name}Spot{k}", pos + direction * 0.08, target, 70, (1.0, 0.85, 0.65), size_deg=30, blend=0.6)

track("TrackLeft", -2.7, -0.6, 4.4, [(0.2, (X0 + 0.5, 0.85, 1.4)), (1.4, (X0 + 0.5, 1.72, 1.4)), (2.6, (X0 + 0.5, 2.59, 1.4)), (3.8, (X0 + 0.5, 3.46, 1.4)), (-0.3, (-1.4, Y0, 1.6))])
track("TrackRight", 2.4, -0.6, 4.6, [(-0.4, (X1 - 0.4, SM, 0.6)), (1.2, (TX, TY, 0.45)), (3.1, (X1 - 0.7, 4.0, 1.0)), (4.4, (2.2, Y1 - 0.4, 1.0))])
rk.area("Fill", (0, 1.2, H - 0.02), 3.0, 12, (1.0, 0.9, 0.8), size_y=4.0)


rk.setup_render(scene, EYE.z)
bpy.ops.wm.save_as_mainfile(filepath=OUT)
tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in scene.objects if o.type == "MESH")
print("BUILD ok", OUT, "objects", len([o for o in scene.objects if o.type == "MESH"]), "triangles", tris)
