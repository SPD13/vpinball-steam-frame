# Builds the "sunlit studio" lobby in Blender: a bright room for work and play, long and narrow, the sun of a garden coming in through two
# tall windows that reach almost to the ceiling, made from CC0 Poly Haven assets and procedural models and artwork (studio_art.py), and saves it as a
# .blend.
#
#   Blender -b --factory-startup --python build_studio.py -- <downloads dir> <output .blend>
#
# Layout, in meters, in Blender axes: the player stands at the origin and faces +Y (the VR menu panel shows up there), Z up. In front,
# 8 m away, a tan leather sofa under a dartboard, between two telescopes on tripods; on the left, the windows on a garden (an HDRI, shown
# on a distant backdrop baked from the player's eye) with radiators and a dartboard between them; under the first window the desk, with a
# monitor, keyboard and mouse, and a mesh office chair on a rug, and nearer, a reading armchair and its ottoman; on the right, a grey
# cabinet of drawers between two big speakers, a poster and another dartboard; behind, a shelf unit, a bookcase with books, cameras and
# a clock, the door and a tall plant.
import bpy, bmesh, json, math, os, random, sys
from mathutils import Vector, Matrix

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import roomkit as rk
import studio_art as art

args = sys.argv[sys.argv.index("--") + 1:]
scene = rk.init(args[0])
OUT = os.path.abspath(args[1])

X0, X1, Y0, Y1, H = -3.4, 3.4, -3.2, 8.4, 3.0
EYE = Vector((0.0, 0.0, 1.65))
INSIDE = (0.0, 1.0, 1.5)
REVEAL = 0.3  # Thickness of the left wall, around the windows
WINDOWS_Y = (4.3, 6.9)  # Centers of the two windows, on the left wall: far enough to be seen in the desktop view, with the desk
WIN_W, WIN_Z0, WIN_Z1 = 1.9, 0.6, 2.85
WIN_TRANSOM = WIN_Z1 - 0.52  # Bottom of the top lights

scene["bake_sizes"] = json.dumps({"floor": 4096, "walls": 4096, "ceiling": 2048, "furniture": 4096, "props": 4096, "desk": 4096,
                                  "backdrop": 4096})
scene["bake_emit_only"] = json.dumps(["backdrop"])
scene["bake_hide_when_lighting"] = json.dumps(["backdrop"])


# ---------------------------------------------------------------------------------------------------------------------------
# Materials

M_FLOOR = rk.pbr_material("Floor", "old_wooden_floor_03", scale=(2.4, 2.4), tint=(1.05, 0.95, 0.85), roughness_mul=1.3)
M_WALL = rk.pbr_material("Wall", "plastered_wall_04", scale=(2.5, 2.5), tint=(1.8, 1.8, 1.78))  # A light grey
M_CEILING = rk.flat_material("Ceiling", (0.88, 0.89, 0.92), 0.9)  # A little cool: the orange floor warms it a lot
M_WHITE = rk.flat_material("WhitePaint", (0.8, 0.8, 0.78), 0.45)
M_BLACK = rk.flat_material("Black", (0.02, 0.02, 0.022), 0.5)
M_STEEL = rk.flat_material("BlackSteel", (0.03, 0.03, 0.03), 0.35, 1.0)
M_CHROME = rk.flat_material("Chrome", (0.85, 0.85, 0.87), 0.2, 1.0)
M_OAK = rk.pbr_material("Oak", "oak_veneer_01", scale=(0.8, 0.8), tint=(1.0, 0.82, 0.62), roughness_mul=0.8)
M_DOOR = rk.pbr_material("Door", "oak_veneer_01", scale=(1.2, 1.2), tint=(0.95, 0.9, 0.85))
M_LEATHER = rk.pbr_material("TanLeather", "leather_white", scale=(0.5, 0.5), tint=(0.78, 0.5, 0.3))
M_GREY = rk.flat_material("GreyCabinet", (0.3, 0.31, 0.32), 0.5)
M_GREY_LIGHT = rk.flat_material("GreyDrawer", (0.42, 0.43, 0.44), 0.45)
M_CONE = rk.flat_material("SpeakerCone", (0.95, 0.35, 0.05), 0.6)
M_PLANTER = rk.flat_material("Planter", (0.85, 0.84, 0.8), 0.6)
M_SOIL = rk.flat_material("Soil", (0.04, 0.03, 0.02), 1.0)
M_CEILING_LED = rk.emissive_material("CeilingLed", (1.0, 0.95, 0.88), 12.0)
M_RUG1 = rk.image_material("RugLounge", art.kilim_rug(art.RUG_WARM, seed=3), roughness=0.95)
M_RUG2 = rk.image_material("RugDesk", art.kilim_rug(art.RUG_COOL, seed=8), roughness=0.95)
M_POSTER = rk.image_material("Poster", art.poster(), roughness=0.6)
M_DESKTOP = rk.image_material("Desktop", art.desktop(), emission=1.6)
M_GRAPHITE = rk.flat_material("Graphite", (0.045, 0.047, 0.05), 0.35, 0.6)
M_SEAT_FABRIC = rk.flat_material("SeatFabric", (0.07, 0.075, 0.08), 0.9)
M_CHAIR_MESH = rk.image_material("ChairMesh", art.chair_mesh(), roughness=0.8)
M_KEYBOARD = rk.image_material("Keyboard", art.keyboard(), roughness=0.4)
M_ALU = rk.flat_material("Aluminium", (0.78, 0.79, 0.8), 0.3, 1.0)
M_GLASS_DARK = rk.flat_material("LensGlass", (0.01, 0.015, 0.03), 0.05)
M_TUBE_WHITE = rk.flat_material("TubeWhite", (0.86, 0.86, 0.84), 0.25)
M_RUBBER = rk.flat_material("Rubber", (0.015, 0.015, 0.015), 0.8)
M_MUG = rk.flat_material("Mug", (0.95, 0.42, 0.08), 0.25)
M_COFFEE = rk.flat_material("Coffee", (0.05, 0.025, 0.01), 0.1)
M_PAD = rk.flat_material("MousePad", (0.06, 0.07, 0.08), 0.9)
M_WHITE_PLASTIC = rk.flat_material("WhitePlastic", (0.9, 0.9, 0.88), 0.35)
M_BOOKS = [rk.flat_material(f"Book{i}", c, 0.55) for i, c in enumerate(((0.55, 0.12, 0.06), (0.08, 0.16, 0.3), (0.75, 0.6, 0.35), (0.12, 0.3, 0.22),
                                                                      (0.85, 0.82, 0.74), (0.6, 0.3, 0.08), (0.2, 0.2, 0.22), (0.7, 0.45, 0.4)))]


# ---------------------------------------------------------------------------------------------------------------------------
# Shell: floor, walls (windows on the left, the door behind), ceiling with two light slots

rk.quad("Floor", [(X0, Y0, 0), (X1, Y0, 0), (X1, Y1, 0), (X0, Y1, 0)], M_FLOOR, "floor", toward=INSIDE)
rk.wall_with_openings("WallLeft", Y0, Y1, lambda u, v: (X0, u, v), [(y - WIN_W / 2, y + WIN_W / 2, WIN_Z0, WIN_Z1) for y in WINDOWS_Y], INSIDE, M_WALL, "walls", H)
DOOR_X, DOOR_W, DOOR_H = 1.6, 1.0, 2.2
rk.wall_with_openings("WallBack", X0, X1, lambda u, v: (u, Y0, v), [(DOOR_X - DOOR_W / 2, DOOR_X + DOOR_W / 2, 0.0, DOOR_H)], INSIDE, M_WALL, "walls", H)
rk.quad("WallFront", [(X1, Y1, 0), (X0, Y1, 0), (X0, Y1, H), (X1, Y1, H)], M_WALL, "walls", toward=INSIDE)
rk.quad("WallRight", [(X1, Y0, 0), (X1, Y1, 0), (X1, Y1, H), (X1, Y0, H)], M_WALL, "walls", toward=INSIDE)
rk.quad("Ceiling", [(X0, Y0, H), (X0, Y1, H), (X1, Y1, H), (X1, Y0, H)], M_CEILING, "ceiling", toward=INSIDE)
for i, x in enumerate((-1.2, 1.2)):
    rk.box(f"CeilingSlot{i}", (x - 0.03, -2.0, H - 0.01), (x + 0.03, 7.6, H), M_BLACK, "ceiling", skip=("+z",))
    rk.box(f"CeilingLed{i}", (x - 0.012, -1.95, H - 0.012), (x + 0.012, 7.55, H - 0.01), M_CEILING_LED, "ceiling", skip=("+z",))
    rk.area(f"CeilingLight{i}", (x, 2.8, H - 0.03), 0.05, 640, (1.0, 0.96, 0.9), size_y=9.5)

# Windows: deep reveals, white frames with a cross, sills, radiators below
for i, y in enumerate(WINDOWS_Y):
    a, b = y - WIN_W / 2, y + WIN_W / 2
    bm = bmesh.new()
    for c in (((X0, a, WIN_Z0), (X0 - REVEAL, a, WIN_Z0), (X0 - REVEAL, a, WIN_Z1), (X0, a, WIN_Z1)),
              ((X0, b, WIN_Z0), (X0 - REVEAL, b, WIN_Z0), (X0 - REVEAL, b, WIN_Z1), (X0, b, WIN_Z1)),
              ((X0, a, WIN_Z1), (X0 - REVEAL, a, WIN_Z1), (X0 - REVEAL, b, WIN_Z1), (X0, b, WIN_Z1))):  # The sill covers the bottom
        bm.faces.new([bm.verts.new(p) for p in c])
    rk.face_toward(bm, (X0 - REVEAL / 2, y, (WIN_Z0 + WIN_Z1) / 2))
    rk.box_uv(bm)
    rk.new_object(f"WindowReveal{i}", bm, M_WALL, "walls")
    fx = X0 - REVEAL + 0.06  # Plane of the frame, near the outside
    f = 0.06
    # The pieces butt against each other: overlapping faces would show as dark squares
    for name, lo, hi in (("Top", (fx - 0.03, a + f, WIN_Z1 - f), (fx + 0.03, b - f, WIN_Z1)), ("Bottom", (fx - 0.03, a + f, WIN_Z0), (fx + 0.03, b - f, WIN_Z0 + f)),
                         ("Left", (fx - 0.03, a, WIN_Z0), (fx + 0.03, a + f, WIN_Z1)), ("Right", (fx - 0.03, b - f, WIN_Z0), (fx + 0.03, b, WIN_Z1)),
                         ("Mullion", (fx - 0.03, y - 0.03, WIN_Z0 + f), (fx + 0.03, y + 0.03, WIN_TRANSOM)),
                         ("Transom", (fx - 0.03, a + f, WIN_TRANSOM), (fx + 0.03, b - f, WIN_TRANSOM + f))):
        rk.box(f"WindowFrame{i}{name}", lo, hi, M_WHITE, "walls")
    # The two casements below the transom have their own frame, a little inside, and a handle on the mullion side
    for k, (c0, c1) in enumerate(((a + f, y - 0.03), (y + 0.03, b - f))):
        sw = 0.045
        for name, lo, hi in (("Top", (fx + 0.03, c0 + sw, WIN_TRANSOM - sw), (fx + 0.05, c1 - sw, WIN_TRANSOM)),
                             ("Bottom", (fx + 0.03, c0 + sw, WIN_Z0 + f), (fx + 0.05, c1 - sw, WIN_Z0 + f + sw)),
                             ("Left", (fx + 0.03, c0, WIN_Z0 + f), (fx + 0.05, c0 + sw, WIN_TRANSOM)),
                             ("Right", (fx + 0.03, c1 - sw, WIN_Z0 + f), (fx + 0.05, c1, WIN_TRANSOM))):
            rk.box(f"WindowSash{i}{k}{name}", lo, hi, M_WHITE, "walls", skip=("-x",))
        hy = c1 - 0.025 if k == 0 else c0 + 0.025
        rk.box(f"WindowHandle{i}{k}", (fx + 0.05, hy - 0.008, 1.2), (fx + 0.07, hy + 0.008, 1.34), M_CHROME, "walls", skip=("-x",))
    rk.box(f"WindowSill{i}", (X0 - REVEAL, a - 0.05, WIN_Z0 - 0.04), (X0 + 0.08, b + 0.05, WIN_Z0), M_WHITE, "walls", skip=("-z",))
    # Radiator: a white body with vertical fins
    rk.box(f"Radiator{i}", (X0 + 0.04, a + 0.1, 0.12), (X0 + 0.1, b - 0.1, 0.5), M_WHITE, "walls", skip=("-x",))
    n = int((WIN_W - 0.24) / 0.05)
    bm = bmesh.new()
    for k in range(n):
        yk = a + 0.13 + k * 0.05
        geom = bmesh.ops.create_cube(bm, size=1.0)
        bmesh.ops.transform(bm, matrix=Matrix.Translation((X0 + 0.12, yk, 0.31)) @ Matrix.Diagonal((0.04, 0.018, 0.37, 1.0)), verts=geom["verts"])
    rk.box_uv(bm)
    rk.new_object(f"RadiatorFins{i}", bm, M_WHITE, "walls")
    for y_pipe in (a + 0.12, b - 0.12):
        rk.cylinder(f"RadiatorPipe{i}{y_pipe:.1f}", (X0 + 0.07, y_pipe, 0.06), 0.012, 0.12, M_WHITE, "walls", segments=8)

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
for name, lo, hi in (("SkirtFront", (X0, Y1 - 0.012, 0), (X1, Y1, 0.09)), ("SkirtRight", (X1 - 0.012, Y0, 0), (X1, Y1, 0.09)),
                     ("SkirtLeft", (X0, Y0, 0), (X0 + 0.012, Y1, 0.09)),
                     ("SkirtBackL", (X0, Y0, 0), (DOOR_X - DOOR_W / 2, Y0 + 0.012, 0.09)), ("SkirtBackR", (DOOR_X + DOOR_W / 2, Y0, 0), (X1, Y0 + 0.012, 0.09))):
    rk.box(name, lo, hi, M_WHITE, "walls", skip=("-z",))


# ---------------------------------------------------------------------------------------------------------------------------
# Outside: the garden, lighting the room and seen through the windows

HDRI = os.path.join(rk.DL, "garden_nook", "garden_nook_8k.hdr")
HDRI_LIGHT = os.path.join(rk.DL, "garden_nook", "garden_nook_4k.hdr")
HDRI_ROTATION = math.radians(171)  # The sun (26 degrees high) outside the left windows, a little forward: sunbeams across the floor
HDRI_STRENGTH = 2.0
rk.world_environment(scene, HDRI_LIGHT, HDRI_ROTATION, HDRI_STRENGTH, "Garden")
rk.backdrop(HDRI, HDRI_ROTATION, HDRI_STRENGTH, EYE, facing_deg=-90, span_deg=170)


# ---------------------------------------------------------------------------------------------------------------------------
# In front: a tan leather sofa against the wall under a dartboard, two telescopes on tripods, a white side table, a rug

SX0, SX1, SY = -1.15, 1.15, Y1 - 0.04  # Sofa: along the front wall, facing the player
for x in (SX0 + 0.06, SX1 - 0.1):
    for y in (SY - 0.8, SY - 0.1):
        rk.box(f"SofaLeg{x:.1f}{y:.1f}", (x, y, 0), (x + 0.04, y + 0.04, 0.1), M_OAK, "furniture", skip=("-z",))
rk.soft_box("SofaBase", (SX0, SY - 0.85, 0.1), (SX1, SY, 0.3), M_LEATHER, "furniture", radius=0.02)
rk.soft_box("SofaBack", (SX0 + 0.16, SY - 0.16, 0.3), (SX1 - 0.16, SY, 0.82), M_LEATHER, "furniture")
SM = (SX0 + SX1) / 2
for k, (a, b) in enumerate(((SX0 + 0.16, SM - 0.005), (SM + 0.005, SX1 - 0.16))):
    rk.soft_box(f"SofaSeat{k}", (a, SY - 0.83, 0.3), (b, SY - 0.16, 0.46), M_LEATHER, "furniture", radius=0.04)
    rk.soft_box(f"SofaCushion{k}", (a + 0.01, SY - 0.36, 0.46), (b - 0.01, SY - 0.16, 0.8), M_LEATHER, "furniture", radius=0.06)
for k, (a, b) in enumerate(((SX0, SX0 + 0.16), (SX1 - 0.16, SX1))):
    rk.soft_box(f"SofaArm{k}", (a, SY - 0.85, 0.3), (b, SY, 0.64), M_LEATHER, "furniture", radius=0.04)
rk.import_prop("throw_pillows_01", (SM, SY - 0.42, 0.46), rot_z_deg=180)
rk.import_prop("dartboard", (SM, Y1 - 0.005, 1.78))  # The board faces -Y
rk.quad("RugLounge", [(1.3, Y1 - 2.3, 0.004), (1.3, Y1 - 0.3, 0.004), (-1.3, Y1 - 0.3, 0.004), (-1.3, Y1 - 2.3, 0.004)], M_RUG1, "floor", uv01=True)
# Side table: a white top on a single pedestal
TY = Y1 - 1.45
rk.cylinder("SideTableTop", (0.0, TY, 0.45), 0.3, 0.03, M_WHITE, "furniture", segments=32)
rk.cylinder("SideTableStem", (0.0, TY, 0.22), 0.03, 0.42, M_WHITE, "furniture", segments=12)
rk.cylinder("SideTableFoot", (0.0, TY, 0.01), 0.2, 0.02, M_WHITE, "furniture", segments=32)
rk.import_prop("potted_plant_04", (0.08, TY - 0.05, 0.465), rot_z_deg=20, max_tris=8000)

def telescope(name, x, y, direction, tube, legs, height=1.18, length=1.0, radius=0.055):
    """A refractor on an alt-azimuth mount and a tripod with an accessory tray, aimed along `direction`: dew shield and lens in
    front, focuser, star diagonal and eyepiece behind, a finder scope on top."""
    g = "props"
    d = Vector(direction).normalized()
    up = Vector((0, 0, 1))
    u = (up - d * up.dot(d)).normalized()  # Up, square to the tube
    side = d.cross(u).normalized()
    head = Vector((x, y, height))
    c = head + up * 0.1  # Middle of the tube, above the mount
    front, back = c + d * length * 0.42, c - d * length * 0.42
    rk.rod(f"{name}Tube", back, front, radius, tube, g, segments=24)
    rk.rod(f"{name}DewShield", front, front + d * 0.18, radius + 0.01, M_BLACK, g, segments=24)
    rk.rod(f"{name}Lens", front + d * 0.06, front + d * 0.065, radius + 0.004, M_GLASS_DARK, g, segments=24)
    rk.rod(f"{name}Cell", back - d * 0.04, back, radius - 0.004, M_BLACK, g, segments=24)
    for t in (-0.18, 0.18):
        rk.rod(f"{name}Ring{t:+.1f}", c + d * (t - 0.015), c + d * (t + 0.015), radius + 0.006, M_BLACK, g, segments=24)
    focuser = back - d * 0.13
    rk.rod(f"{name}Focuser", back - d * 0.04, focuser, 0.024, M_ALU, g, segments=16)
    rk.rod(f"{name}FocusKnob", back - d * 0.07 - side * 0.05, back - d * 0.07 + side * 0.05, 0.012, M_BLACK, g, segments=12)
    rk.rod(f"{name}Diagonal", focuser - d * 0.03, focuser - d * 0.03 + u * 0.06, 0.02, M_BLACK, g, segments=16)
    rk.rod(f"{name}Eyepiece", focuser - d * 0.03 + u * 0.06, focuser - d * 0.03 + u * 0.13, 0.016, M_RUBBER, g, segments=16)
    finder = c + u * (radius + 0.045) + side * 0.03
    rk.rod(f"{name}Finder", finder - d * 0.12, finder + d * 0.1, 0.016, tube, g, segments=12)
    for t in (-0.06, 0.05):
        rk.rod(f"{name}FinderFoot{t:+.2f}", c + d * t + u * radius * 0.8, finder + d * t, 0.006, M_BLACK, g, segments=6)
    # Mount: a fork arm from the head to the dovetail under the tube
    rk.rod(f"{name}Dovetail", c - d * 0.2 - u * (radius + 0.01), c + d * 0.2 - u * (radius + 0.01), 0.012, M_ALU, g, segments=6)
    rk.rod(f"{name}Arm", head, c - u * (radius + 0.02), 0.022, M_BLACK, g, segments=12)
    rk.rod(f"{name}Head", head - up * 0.09, head + up * 0.01, 0.05, M_BLACK, g, segments=20)
    rk.rod(f"{name}PanHandle", head + up * 0.02, head + up * 0.02 - d * 0.28 - up * 0.08, 0.008, M_BLACK, g, segments=8)
    # Tripod: three legs, a tray between them halfway down
    top = head - up * 0.09
    feet = []
    for k in range(3):
        a = math.radians(30 + 120 * k)
        foot = Vector((x + 0.46 * math.cos(a), y + 0.46 * math.sin(a), 0.0))
        feet.append(foot)
        rk.rod(f"{name}Leg{k}", top, foot + up * 0.03, 0.017, legs, g, segments=10)
        rk.rod(f"{name}Foot{k}", foot + up * 0.035, foot, 0.019, M_RUBBER, g, segments=10)
    tray = [top.lerp(f, 0.55) for f in feet]
    bm = bmesh.new()
    lo = [bm.verts.new(p - up * 0.006) for p in tray]
    hi = [bm.verts.new(p) for p in tray]
    bm.faces.new(hi)
    bm.faces.new(list(reversed(lo)))
    for k in range(3):
        bm.faces.new([lo[k], lo[(k + 1) % 3], hi[(k + 1) % 3], hi[k]])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    rk.box_uv(bm)
    rk.new_object(f"{name}Tray", bm, legs, g)

# The left one looks out of the front window, the other one at the sky above the room, like a stargazer's pair
telescope("TelescopeL", -1.75, Y1 - 0.6, (-0.85, -0.45, 0.25), M_TUBE_WHITE, M_ALU)
telescope("TelescopeR", 1.75, Y1 - 0.6, (0.75, 0.4, 0.4), M_BLACK, M_STEEL, length=0.85, radius=0.05)
# Plants in the front corners, far enough from the walls for their leaves
for i, (x, y, pick, scale, rot) in enumerate(((X1 - 0.7, Y1 - 0.65, 0, 1.7, 0), (X0 + 0.7, Y1 - 0.65, 1, 1.8, 60))):
    top = 0.5 - 0.03
    rk.cylinder(f"Planter{i}", (x, y, 0.25), 0.2, 0.5, M_PLANTER, "furniture", segments=32, radius_top=0.24)
    rk.cylinder(f"Planter{i}Soil", (x, y, top), 0.22, 0.01, M_SOIL, "furniture", segments=24)
    rk.import_prop("anthurium_botany_01", (x, y, top), rot_z_deg=rot, scale=scale, pick=pick)


# ---------------------------------------------------------------------------------------------------------------------------
# Left: a reading armchair and its ottoman, a dartboard between the windows, a plant on a sill. Under the first window: the desk, its
# monitor, keyboard, mouse, lamp and supplies, the office chair on a rug

rk.import_prop("dartboard", (X0 + 0.005, (WINDOWS_Y[0] + WINDOWS_Y[1]) / 2, 1.73), rot_z_deg=90)  # The board faces -Y
rk.import_prop("potted_plant_04", (X0 - 0.12, WINDOWS_Y[1] + 0.5, WIN_Z0), rot_z_deg=40, group="props", max_tris=8000)
# Out of the desktop view (about 45 degrees on each side), as the ottoman would stand in front of it
rk.import_prop("modern_arm_chair_01", (X0 + 0.75, 0.7, 0), rot_z_deg=115, group="props")  # The chair faces -Y
rk.import_prop("Ottoman_01", (X0 + 1.5, 1.0, 0), rot_z_deg=25, scale=0.8, group="props")
rk.import_prop("potted_plant_02", (X0 + 0.4, -0.4, 0), rot_z_deg=200, group="props", max_tris=8000)

DX0, DX1, DY0, DY1 = X0 + 0.22, X0 + 0.97, WINDOWS_Y[0] - 0.85, WINDOWS_Y[0] + 0.85  # Along the wall, under the window
DZ = 0.74
DC = WINDOWS_Y[0]  # Where the chair stands
rk.box("DeskTop", (DX0, DY0, DZ - 0.04), (DX1, DY1, DZ), M_OAK, "desk")
for x in (DX0 + 0.04, DX1 - 0.08):
    for y in (DY0 + 0.04, DY1 - 0.08):
        rk.box(f"DeskLeg{x:.1f}{y:.1f}", (x, y, 0), (x + 0.04, y + 0.04, DZ - 0.04), M_OAK, "desk", skip=("-z", "+z"))
rk.box("DeskApron", (DX0 + 0.04, DY0 + 0.04, DZ - 0.12), (DX0 + 0.06, DY1 - 0.04, DZ - 0.04), M_OAK, "desk")

def at_desk(objs, x, y, rot_deg=-90):
    """Places objects built facing +Y at the origin: on the desk, turned toward the chair (+X)."""
    rk.transform(objs, Matrix.Translation((x, y, DZ)) @ Matrix.Rotation(math.radians(rot_deg), 4, "Z"))

# Monitor: a 27 inch screen with thin borders, graphite, on an aluminium stand with a flat foot
mon = [rk.box("MonitorBody", (-0.31, -0.014, 0.11), (0.31, 0.0, 0.475), M_GRAPHITE, "desk"),
       rk.box("MonitorBack", (-0.16, -0.045, 0.2), (0.16, -0.014, 0.39), M_GRAPHITE, "desk"),
       # Screens face +Y, toward the chair: seen from there, +X is on the left (quads from the bottom left corner, counter clockwise)
       rk.quad("MonitorGlass", [(0.31, 0.001, 0.11), (-0.31, 0.001, 0.11), (-0.31, 0.001, 0.475), (0.31, 0.001, 0.475)], M_BLACK, "desk"),
       rk.quad("MonitorScreen", [(0.302, 0.002, 0.125), (-0.302, 0.002, 0.125), (-0.302, 0.002, 0.467), (0.302, 0.002, 0.467)],
               M_DESKTOP, "desk", uv01=True),
       rk.box("MonitorStand", (-0.03, -0.075, 0.006), (0.03, -0.045, 0.3), M_ALU, "desk"),
       rk.soft_box("MonitorFoot", (-0.14, -0.2, 0.0), (0.14, 0.02, 0.008), M_ALU, "desk", radius=0.003)]
at_desk(mon, DX0 + 0.3, DC)
# Keyboard, mouse on its pad
kb = [rk.box("KeyboardBody", (-0.215, -0.07, 0.0), (0.215, 0.07, 0.01), M_ALU, "desk", skip=("-z",)),
      rk.quad("KeyboardKeys", [(0.21, 0.065, 0.0105), (-0.21, 0.065, 0.0105), (-0.21, -0.065, 0.0105), (0.21, -0.065, 0.0105)],
              M_KEYBOARD, "desk", uv01=True)]
at_desk(kb, DX0 + 0.55, DC)
pad = rk.box("MousePad", (-0.11, -0.09, 0.0), (0.11, 0.09, 0.003), M_PAD, "desk", skip=("-z",))
mouse = rk.soft_box("Mouse", (-0.03, -0.055, 0.003), (0.03, 0.055, 0.03), M_WHITE_PLASTIC, "desk", radius=0.012)
at_desk([pad, mouse], DX0 + 0.55, DC + 0.36, -80)
# A mug, the pens, sticky notes, the desk lamp
mug = [rk.cylinder("Mug", (0, 0, 0.0475), 0.04, 0.095, M_MUG, "desk", segments=24),
       rk.cylinder("MugCoffee", (0, 0, 0.0955), 0.035, 0.001, M_COFFEE, "desk", segments=20)]
for k, (p, q) in enumerate((((0.04, 0, 0.075), (0.065, 0, 0.07)), ((0.065, 0, 0.07), (0.065, 0, 0.03)), ((0.065, 0, 0.03), (0.04, 0, 0.025)))):
    mug.append(rk.rod(f"MugHandle{k}", p, q, 0.007, M_MUG, "desk", segments=8))
at_desk(mug, DX0 + 0.65, DC - 0.38, 30)
rk.import_prop("stationery_supplies", (DX0 + 0.22, DC - 0.45, DZ + 0.07), rot_z_deg=-70, group="desk", max_tris=4000)
rk.import_prop("office_notepads", (DX0 + 0.5, DC - 0.6, DZ), rot_z_deg=-80, pick=0, group="desk")
rk.import_prop("desk_lamp_arm_01", (DX0 + 0.12, DY0 + 0.15, DZ), rot_z_deg=-60, group="desk", max_tris=8000)
rk.quad("RugDesk", [(X0 + 0.3, DY0 - 0.15, 0.004), (X0 + 2.6, DY0 - 0.15, 0.004), (X0 + 2.6, DY1 + 0.15, 0.004), (X0 + 0.3, DY1 + 0.15, 0.004)],
        M_RUG2, "floor", uv01=True)

def curved_panel(name, z0, z1, width, y_of, curve, material, group, thickness, rows=8, cols=9):
    """A panel curved like the back of a chair: rows from z0 to z1, `width(t)` and depth `y_of(t)` along its height (t = 0..1), its
    sides wrapped forward by `curve`; UVs span 0..1 over it. Thickened and smoothed by modifiers, then applied."""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.verify()
    grid = []
    for i in range(rows):
        t = i / (rows - 1)
        grid.append([bm.verts.new(((j / (cols - 1) * 2 - 1) * width(t) / 2, y_of(t) + curve * (j / (cols - 1) * 2 - 1) ** 2, z0 + (z1 - z0) * t))
                     for j in range(cols)])
    for i in range(rows - 1):
        for j in range(cols - 1):
            f = bm.faces.new([grid[i][j], grid[i][j + 1], grid[i + 1][j + 1], grid[i + 1][j]])
            for loop, (a, b) in zip(f.loops, ((j, i), (j + 1, i), (j + 1, i + 1), (j, i + 1))):
                loop[uv].uv = (a / (cols - 1), b / (rows - 1))
    obj = rk.new_object(name, bm, material, group)
    solid = obj.modifiers.new("Thickness", "SOLIDIFY")
    solid.thickness = thickness
    smooth = obj.modifiers.new("Smooth", "SUBSURF")
    smooth.levels = smooth.render_levels = 2
    mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    obj.modifiers.clear()
    obj.data = mesh
    for p in obj.data.polygons:
        p.use_smooth = True
    return obj

def office_chair(name, x, y, rot_deg):
    """A modern ergonomic swivel chair: a mesh back in a black frame, curved for the spine, a headrest, a padded seat, armrests, a
    gas lift on a five star base with casters."""
    g = "desk"
    back_y = lambda t: -0.24 - 0.09 * t + 0.025 * math.sin(math.pi * min(t / 0.6, 1.0))  # Leans back, pushed forward at the lumbar
    objs = [curved_panel(f"{name}Back", 0.58, 1.1, lambda t: 0.44 + 0.05 * math.sin(math.pi * t), back_y, 0.06, M_CHAIR_MESH, g, 0.02),
            curved_panel(f"{name}Headrest", 1.17, 1.31, lambda t: 0.3, lambda t: -0.345 - 0.01 * t, 0.04, M_CHAIR_MESH, g, 0.03, rows=4),
            rk.soft_box(f"{name}Seat", (-0.25, -0.22, 0.45), (0.25, 0.25, 0.53), M_SEAT_FABRIC, g, radius=0.04),
            rk.box(f"{name}SeatPan", (-0.2, -0.18, 0.42), (0.2, 0.2, 0.45), M_BLACK, g),
            rk.rod(f"{name}Spine0", (0, -0.12, 0.43), (0, -0.3, 0.5), 0.022, M_BLACK, g, segments=10),
            rk.rod(f"{name}Spine1", (0, -0.3, 0.5), (0, back_y(0.2) - 0.02, 0.69), 0.022, M_BLACK, g, segments=10),
            rk.rod(f"{name}HeadPost", (0, back_y(0.95) - 0.02, 1.07), (0, -0.36, 1.2), 0.014, M_BLACK, g, segments=8),
            rk.cylinder(f"{name}Column", (0, 0, 0.3), 0.025, 0.26, M_CHROME, g, segments=12),
            rk.cylinder(f"{name}Sleeve", (0, 0, 0.16), 0.034, 0.14, M_BLACK, g, segments=12)]
    for sx in (-1, 1):
        objs.append(rk.rod(f"{name}ArmPost{sx}", (sx * 0.2, -0.06, 0.43), (sx * 0.27, -0.06, 0.66), 0.018, M_BLACK, g, segments=8))
        objs.append(rk.soft_box(f"{name}ArmPad{sx}", (sx * 0.27 - 0.04, -0.18, 0.66), (sx * 0.27 + 0.04, 0.1, 0.69), M_BLACK, g, radius=0.012))
    for k in range(5):
        a = math.radians(90 + 72 * k)
        end = Vector((0.32 * math.cos(a), 0.32 * math.sin(a), 0.075))
        objs.append(rk.rod(f"{name}Spoke{k}", (0, 0, 0.1), end, 0.02, M_GRAPHITE, g, segments=6))
        objs.append(rk.cylinder(f"{name}Caster{k}", (end.x, end.y, 0.06), 0.012, 0.03, M_BLACK, g, segments=8))
        objs.append(rk.cylinder(f"{name}Wheel{k}", (end.x, end.y, 0.028), 0.028, 0.035, M_BLACK, g, segments=12, axis="x"))
    rk.transform(objs, Matrix.Translation((x, y, 0)) @ Matrix.Rotation(math.radians(rot_deg), 4, "Z"))

office_chair("Chair", DX1 + 0.45, DC, 75)  # Built facing +Y: turned toward the desk, a little toward the room


# ---------------------------------------------------------------------------------------------------------------------------
# Right: a grey cabinet of drawers between two big speakers, plants and a boombox on it, a poster above, a dartboard nearer the sofa

CY0, CY1, CD, CH = 3.4, 5.8, 0.45, 0.8
rk.box("Cabinet", (X1 - CD, CY0, 0.08), (X1, CY1, CH), M_GREY, "furniture", skip=("+x",))
for k in range(4):
    for j in range(3):
        y0 = CY0 + 0.02 + k * (CY1 - CY0 - 0.04) / 4
        y1 = y0 + (CY1 - CY0 - 0.04) / 4 - 0.015
        z0 = 0.1 + j * (CH - 0.12) / 3
        z1 = z0 + (CH - 0.12) / 3 - 0.015
        rk.box(f"Drawer{k}{j}", (X1 - CD - 0.012, y0, z0), (X1 - CD, y1, z1), M_GREY_LIGHT, "furniture", skip=("+x",))
        rk.box(f"Handle{k}{j}", (X1 - CD - 0.03, (y0 + y1) / 2 - 0.08, z1 - 0.05), (X1 - CD - 0.012, (y0 + y1) / 2 + 0.08, z1 - 0.035), M_STEEL, "furniture")
for x in (X1 - CD + 0.04, X1 - 0.06):
    for y in (CY0 + 0.04, CY1 - 0.06):
        rk.box(f"CabinetLeg{x:.1f}{y:.1f}", (x, y, 0), (x + 0.03, y + 0.03, 0.08), M_STEEL, "furniture", skip=("-z",))
rk.import_prop("ceramic_vase_01", (X1 - 0.22, CY0 + 0.3, CH), group="props", max_tris=4000)
rk.import_prop("boombox", (X1 - 0.2, (CY0 + CY1) / 2 + 0.1, CH), rot_z_deg=-90, group="props")  # The boombox faces -Y
rk.import_prop("calathea_orbifolia_01", (X1 - 0.22, CY1 - 0.35, CH), scale=1.2, pick=2)
PC = (CY0 + CY1) / 2
rk.box("PosterFrame", (X1 - 0.03, PC - 0.45, 1.45), (X1, PC + 0.45, 2.75), M_BLACK, "furniture", skip=("+x",))
rk.quad("Poster", [(X1 - 0.031, PC + 0.41, 1.49), (X1 - 0.031, PC - 0.41, 1.49), (X1 - 0.031, PC - 0.41, 2.71), (X1 - 0.031, PC + 0.41, 2.71)], M_POSTER,
        "furniture", uv01=True)
rk.import_prop("dartboard", (X1 - 0.005, CY1 + 0.8, 1.73), rot_z_deg=-90)  # Between the speaker and the plant

def speaker(name, y):
    """A big floor speaker: a black cabinet, an orange cone with its surround, a tweeter above."""
    rk.box(name, (X1 - 0.42, y - 0.21, 0.02), (X1 - 0.05, y + 0.21, 1.05), M_BLACK, "furniture", skip=("+x",))
    fx = X1 - 0.42
    rk.cylinder(name + "Surround", (fx - 0.006, y, 0.42), 0.18, 0.012, M_STEEL, "furniture", segments=32, axis="x")
    rk.cylinder(name + "Cone", (fx - 0.03, y, 0.42), 0.16, 0.035, M_CONE, "furniture", segments=32, axis="x", radius_top=0.05)
    rk.cylinder(name + "Cap", (fx - 0.05, y, 0.42), 0.045, 0.01, M_STEEL, "furniture", segments=16, axis="x")
    rk.cylinder(name + "Tweeter", (fx - 0.01, y, 0.82), 0.05, 0.02, M_CONE, "furniture", segments=20, axis="x")

speaker("SpeakerA", CY0 - 0.3)
speaker("SpeakerB", CY1 + 0.3)


# ---------------------------------------------------------------------------------------------------------------------------
# Behind: an open shelf unit with drawers in the corner, a bookcase with books, cameras and a clock, the door, a tall plant in the
# corner on the right

def books(name, x0, x1, y_back, z, rng, depth=(0.15, 0.21), height=(0.17, 0.27), lean_last=True):
    """A row of books standing on a shelf from x0 to x1 along X, their backs against y_back (the wall is at lower Y)."""
    x = x0
    k = 0
    while True:
        w = rng.uniform(0.018, 0.045)
        if x + w > x1:
            break
        h, dp = rng.uniform(*height), rng.uniform(*depth)
        rk.box(f"{name}{k}", (x, y_back, z), (x + w, y_back + dp, z + h), M_BOOKS[rng.randrange(len(M_BOOKS))], "props", skip=("-z", "-y"))
        x += w + 0.002
        k += 1

def book_stack(name, x, y, z, count, rng):
    """A few books lying flat, one on the other."""
    for k in range(count):
        w, dp, t = rng.uniform(0.17, 0.24), rng.uniform(0.12, 0.17), rng.uniform(0.02, 0.04)
        dx, dy = rng.uniform(-0.01, 0.01), rng.uniform(-0.01, 0.01)
        rk.box(f"{name}{k}", (x - w / 2 + dx, y - dp / 2 + dy, z), (x + w / 2 + dx, y + dp / 2 + dy, z + t), M_BOOKS[rng.randrange(len(M_BOOKS))],
               "props", skip=("-z",))
        z += t
    return z

rng = random.Random(7)
# Shelf unit with drawers (CC0 model, built facing -Y): turned to the room
SU_X = X0 + 0.7
rk.import_prop("drawer_cabinet", (SU_X, Y0 + 0.25, 0), rot_z_deg=180, group="props")
books("ShelfBooks", SU_X - 0.5, SU_X + 0.1, Y0 + 0.08, 1.13, rng)
rk.import_prop("alarm_clock_01", (SU_X + 0.3, Y0 + 0.3, 1.13), rot_z_deg=200, group="props", max_tris=4000)
book_stack("ShelfStack", SU_X - 0.25, Y0 + 0.25, 1.45, 3, rng)
rk.import_prop("Camera_01", (SU_X + 0.25, Y0 + 0.28, 1.45), rot_z_deg=165, group="props", max_tris=4000)
rk.import_prop("potted_plant_04", (SU_X, Y0 + 0.25, 1.76), rot_z_deg=10, group="props", max_tris=8000)
# Bookcase (CC0 model, built sideways): three columns, shelves at 0.41, 0.79 and 1.17 m, the top at 1.54 m
BX, BY = -0.6, Y0 + 0.22
rk.import_prop("wooden_display_shelves_01", (BX, BY, 0), rot_z_deg=90, group="props")
for k, cx in enumerate((BX - 0.35, BX, BX + 0.35)):
    for j, z in enumerate((0.41, 0.79, 1.17)):
        kind = (k + 2 * j) % 3
        if kind == 0:
            books(f"Books{k}{j}", cx - 0.15, cx + 0.15, BY - 0.17, z, rng)
        elif kind == 1:
            book_stack(f"Stack{k}{j}", cx, BY, z, rng.randint(2, 4), rng)
        elif j == 0:
            rk.import_prop("binder_notebook", (cx, BY, z), rot_z_deg=rng.uniform(-10, 10), pick=0, group="props", max_tris=4000)
        elif j == 1:
            rk.import_prop("ceramic_vase_01", (cx, BY, z), scale=0.75, group="props", max_tris=4000)
        else:
            rk.import_prop("Camera_01", (cx, BY + 0.02, z), rot_z_deg=170, group="props", max_tris=4000)
rk.import_prop("potted_plant_04", (BX - 0.3, BY, 1.54), rot_z_deg=80, group="props", max_tris=8000)
book_stack("TopStack", BX + 0.25, BY, 1.54, 2, rng)
# A tall plant in a white planter, in the corner behind on the right
PX, PY = X1 - 0.55, Y0 + 0.55
rk.cylinder("PlanterTall", (PX, PY, 0.22), 0.24, 0.44, M_PLANTER, "props", segments=32, radius_top=0.27)
rk.cylinder("PlanterTallSoil", (PX, PY, 0.42), 0.25, 0.01, M_SOIL, "props", segments=24)
rk.import_prop("pachira_aquatica_01", (PX, PY, 0.4), rot_z_deg=30, variant="_d", group="props")


rk.setup_render(scene, EYE.z)
bpy.ops.wm.save_as_mainfile(filepath=OUT)
tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in scene.objects if o.type == "MESH")
print("BUILD ok", OUT, "objects", len([o for o in scene.objects if o.type == "MESH"]), "triangles", tris)
