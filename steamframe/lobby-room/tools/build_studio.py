# Builds the "sunlit studio" lobby in Blender: a bright room for work and play, the sun of a garden coming in through three tall windows,
# made from CC0 Poly Haven assets and procedural models and artwork (studio_art.py), and saves it as a .blend.
#
#   Blender -b --factory-startup --python build_studio.py -- <downloads dir> <output .blend>
#
# Layout, in meters, in Blender axes: the player stands at the origin and faces +Y (the VR menu panel shows up there), Z up. In front, a
# tan leather sofa between two tripod lights; on the left, the windows on a garden (an HDRI, shown on a distant backdrop baked from the
# player's eye) with radiators and a dartboard; behind on the left, a desk with a monitor and an orange office chair; on the right, a
# grey cabinet of drawers between two big speakers, a poster and another dartboard; the door on the wall behind.
import bpy, bmesh, json, math, os, sys
from mathutils import Vector, Matrix

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import roomkit as rk
import studio_art as art

args = sys.argv[sys.argv.index("--") + 1:]
scene = rk.init(args[0])
OUT = os.path.abspath(args[1])

X0, X1, Y0, Y1, H = -4.2, 4.2, -3.2, 5.3, 3.0
EYE = Vector((0.0, 0.0, 1.65))
INSIDE = (0.0, 1.0, 1.5)
REVEAL = 0.3  # Thickness of the left wall, around the windows
WINDOWS_Y = (-1.6, 0.9, 3.4)  # Centers of the three windows, on the left wall
WIN_W, WIN_Z0, WIN_Z1 = 1.3, 0.55, 2.75

scene["bake_sizes"] = json.dumps({"floor": 4096, "walls": 4096, "ceiling": 2048, "furniture": 4096, "desk": 2048, "backdrop": 4096})
scene["bake_emit_only"] = json.dumps(["backdrop"])
scene["bake_hide_when_lighting"] = json.dumps(["backdrop"])


# ---------------------------------------------------------------------------------------------------------------------------
# Materials

M_FLOOR = rk.pbr_material("Floor", "old_wooden_floor_03", scale=(2.4, 2.4), tint=(1.05, 0.95, 0.85), roughness_mul=1.3)
M_WALL = rk.pbr_material("Wall", "plastered_wall_04", scale=(2.5, 2.5), tint=(1.6, 1.56, 1.48))
M_CEILING = rk.flat_material("Ceiling", (0.8, 0.82, 0.86), 0.9)  # A little cool: the orange floor warms it a lot
M_WHITE = rk.flat_material("WhitePaint", (0.8, 0.8, 0.78), 0.45)
M_BLACK = rk.flat_material("Black", (0.02, 0.02, 0.022), 0.5)
M_STEEL = rk.flat_material("BlackSteel", (0.03, 0.03, 0.03), 0.35, 1.0)
M_CHROME = rk.flat_material("Chrome", (0.85, 0.85, 0.87), 0.2, 1.0)
M_OAK = rk.pbr_material("Oak", "oak_veneer_01", scale=(0.8, 0.8), tint=(1.0, 0.82, 0.62), roughness_mul=0.8)
M_DOOR = rk.pbr_material("Door", "oak_veneer_01", scale=(1.2, 1.2), tint=(0.95, 0.9, 0.85))
M_LEATHER = rk.pbr_material("TanLeather", "leather_white", scale=(0.5, 0.5), tint=(0.78, 0.5, 0.3))
M_ORANGE = rk.flat_material("OrangePlastic", (0.9, 0.22, 0.03), 0.3)
M_GREY = rk.flat_material("GreyCabinet", (0.3, 0.31, 0.32), 0.5)
M_GREY_LIGHT = rk.flat_material("GreyDrawer", (0.42, 0.43, 0.44), 0.45)
M_CONE = rk.flat_material("SpeakerCone", (0.95, 0.35, 0.05), 0.6)
M_PLANTER = rk.flat_material("Planter", (0.85, 0.84, 0.8), 0.6)
M_SOIL = rk.flat_material("Soil", (0.04, 0.03, 0.02), 1.0)
M_CEILING_LED = rk.emissive_material("CeilingLed", (1.0, 0.95, 0.88), 12.0)
M_TRIPOD_LENS = rk.emissive_material("TripodLens", (1.0, 0.88, 0.7), 6.0)
M_RUG1 = rk.image_material("RugLounge", art.kilim_rug(art.RUG_WARM, seed=3), roughness=0.95)
M_RUG2 = rk.image_material("RugDesk", art.kilim_rug(art.RUG_COOL, seed=8), roughness=0.95)
M_POSTER = rk.image_material("Poster", art.poster(), roughness=0.6)
M_WALLPAPER = rk.image_material("Wallpaper", art.wallpaper(), emission=1.6)
M_CODE = rk.image_material("CodeScreen", art.code_screen(), emission=1.4)


# ---------------------------------------------------------------------------------------------------------------------------
# Shell: floor, walls (windows on the left, the door behind), ceiling with two light slots

rk.quad("Floor", [(X0, Y0, 0), (X1, Y0, 0), (X1, Y1, 0), (X0, Y1, 0)], M_FLOOR, "floor", toward=INSIDE)
rk.wall_with_openings("WallLeft", Y0, Y1, lambda u, v: (X0, u, v), [(y - WIN_W / 2, y + WIN_W / 2, WIN_Z0, WIN_Z1) for y in WINDOWS_Y], INSIDE, M_WALL, "walls", H)
DOOR_X, DOOR_W, DOOR_H = 2.3, 1.0, 2.2
rk.wall_with_openings("WallBack", X0, X1, lambda u, v: (u, Y0, v), [(DOOR_X - DOOR_W / 2, DOOR_X + DOOR_W / 2, 0.0, DOOR_H)], INSIDE, M_WALL, "walls", H)
rk.quad("WallFront", [(X1, Y1, 0), (X0, Y1, 0), (X0, Y1, H), (X1, Y1, H)], M_WALL, "walls", toward=INSIDE)
rk.quad("WallRight", [(X1, Y0, 0), (X1, Y1, 0), (X1, Y1, H), (X1, Y0, H)], M_WALL, "walls", toward=INSIDE)
rk.quad("Ceiling", [(X0, Y0, H), (X0, Y1, H), (X1, Y1, H), (X1, Y0, H)], M_CEILING, "ceiling", toward=INSIDE)
for i, x in enumerate((-1.6, 1.6)):
    rk.box(f"CeilingSlot{i}", (x - 0.03, -1.5, H - 0.01), (x + 0.03, 4.0, H), M_BLACK, "ceiling", skip=("+z",))
    rk.box(f"CeilingLed{i}", (x - 0.012, -1.45, H - 0.012), (x + 0.012, 3.95, H - 0.01), M_CEILING_LED, "ceiling", skip=("+z",))
    rk.area(f"CeilingLight{i}", (x, 1.25, H - 0.03), 0.05, 220, (1.0, 0.95, 0.88), size_y=5.4)

# Windows: deep reveals, white frames with a cross, sills, radiators below
for i, y in enumerate(WINDOWS_Y):
    a, b = y - WIN_W / 2, y + WIN_W / 2
    bm = bmesh.new()
    for c in (((X0, a, WIN_Z0), (X0 - REVEAL, a, WIN_Z0), (X0 - REVEAL, a, WIN_Z1), (X0, a, WIN_Z1)),
              ((X0, b, WIN_Z0), (X0 - REVEAL, b, WIN_Z0), (X0 - REVEAL, b, WIN_Z1), (X0, b, WIN_Z1)),
              ((X0, a, WIN_Z1), (X0 - REVEAL, a, WIN_Z1), (X0 - REVEAL, b, WIN_Z1), (X0, b, WIN_Z1)),
              ((X0, a, WIN_Z0), (X0 - REVEAL, a, WIN_Z0), (X0 - REVEAL, b, WIN_Z0), (X0, b, WIN_Z0))):
        bm.faces.new([bm.verts.new(p) for p in c])
    rk.face_toward(bm, (X0 - REVEAL / 2, y, (WIN_Z0 + WIN_Z1) / 2))
    rk.box_uv(bm)
    rk.new_object(f"WindowReveal{i}", bm, M_WALL, "walls")
    fx = X0 - REVEAL + 0.06  # Plane of the frame, near the outside
    f = 0.05
    for name, lo, hi in (("Top", (fx - 0.03, a, WIN_Z1 - f), (fx + 0.03, b, WIN_Z1)), ("Bottom", (fx - 0.03, a, WIN_Z0), (fx + 0.03, b, WIN_Z0 + f)),
                         ("Left", (fx - 0.03, a, WIN_Z0), (fx + 0.03, a + f, WIN_Z1)), ("Right", (fx - 0.03, b - f, WIN_Z0), (fx + 0.03, b, WIN_Z1)),
                         ("Mullion", (fx - 0.025, y - 0.025, WIN_Z0), (fx + 0.025, y + 0.025, WIN_Z1)),
                         ("Transom", (fx - 0.025, a, WIN_Z0 + 1.45), (fx + 0.025, b, WIN_Z0 + 1.5))):
        rk.box(f"WindowFrame{i}{name}", lo, hi, M_WHITE, "walls")
    rk.box(f"WindowSill{i}", (X0 - REVEAL, a - 0.05, WIN_Z0 - 0.04), (X0 + 0.06, b + 0.05, WIN_Z0), M_WHITE, "walls", skip=("-z",))
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
HDRI_STRENGTH = 1.5
rk.world_environment(scene, HDRI_LIGHT, HDRI_ROTATION, HDRI_STRENGTH, "Garden")
rk.backdrop(HDRI, HDRI_ROTATION, HDRI_STRENGTH, EYE, facing_deg=-90, span_deg=170)


# ---------------------------------------------------------------------------------------------------------------------------
# In front: a tan leather sofa against the wall, two tripod lights, a white side table, a rug

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
rk.quad("RugLounge", [(1.3, 3.0, 0.004), (1.3, 5.0, 0.004), (-1.3, 5.0, 0.004), (-1.3, 3.0, 0.004)], M_RUG1, "floor", uv01=True)
# Side table: a white top on a single pedestal
rk.cylinder("SideTableTop", (0.0, 3.85, 0.45), 0.3, 0.03, M_WHITE, "furniture", segments=32)
rk.cylinder("SideTableStem", (0.0, 3.85, 0.22), 0.03, 0.42, M_WHITE, "furniture", segments=12)
rk.cylinder("SideTableFoot", (0.0, 3.85, 0.01), 0.2, 0.02, M_WHITE, "furniture", segments=32)
rk.import_prop("potted_plant_04", (0.08, 3.8, 0.465), rot_z_deg=20)

def tripod_light(name, x, y, target, height=1.6):
    """A studio light on a tripod, its head turned toward `target`."""
    top = Vector((x, y, height))
    for k in range(3):
        a = math.radians(90 + 120 * k)
        rk.rod(f"{name}Leg{k}", top - Vector((0, 0, 0.25)), (x + 0.38 * math.cos(a), y + 0.38 * math.sin(a), 0), 0.012, M_STEEL, "furniture")
    rk.rod(f"{name}Pole", (x, y, height - 0.25), top, 0.012, M_STEEL, "furniture")
    head = rk.cylinder(f"{name}Head", (0, 0, 0), 0.12, 0.24, M_BLACK, "furniture", segments=24, radius_top=0.09)
    lens = rk.cylinder(f"{name}Lens", (0, 0, -0.121), 0.105, 0.002, M_TRIPOD_LENS, "furniture", segments=24)
    direction = (Vector(target) - top).normalized()
    rot = direction.to_track_quat("-Z", "Y").to_matrix().to_4x4()
    rk.transform([head, lens], Matrix.Translation(top + Vector((0, 0, 0.05))) @ rot)
    rk.spot(f"{name}Spot", top + Vector((0, 0, 0.05)) + direction * 0.14, target, 25, (1.0, 0.88, 0.7), size_deg=50, blend=0.7, radius=0.1)

tripod_light("TripodL", -1.75, Y1 - 0.55, (-0.4, Y1, 1.4))
tripod_light("TripodR", 1.75, Y1 - 0.55, (0.4, Y1, 1.4))
for i, (x, y, pick, scale) in enumerate(((X1 - 0.5, Y1 - 0.5, 0, 2.2), (X0 + 0.55, Y1 - 0.5, 1, 2.0))):
    top = 0.5 - 0.03
    rk.cylinder(f"Planter{i}", (x, y, 0.25), 0.2, 0.5, M_PLANTER, "furniture", segments=32, radius_top=0.24)
    rk.cylinder(f"Planter{i}Soil", (x, y, top), 0.22, 0.01, M_SOIL, "furniture", segments=24)
    rk.import_prop("anthurium_botany_01", (x, y, top), rot_z_deg=60 * i, scale=scale, pick=pick)


# ---------------------------------------------------------------------------------------------------------------------------
# Left: dartboard between the windows. Behind on the left: the desk, its monitor, laptop and lamp, the office chair, a rug

rk.import_prop("dartboard", (X0 + 0.005, (WINDOWS_Y[1] + WINDOWS_Y[2]) / 2, 1.73), rot_z_deg=90)  # The board faces -Y
DX0, DX1, DY0, DY1 = X0 + 0.3, X0 + 1.9, Y0 + 0.55, Y0 + 1.3
rk.box("DeskTop", (DX0, DY0, 0.72), (DX1, DY1, 0.76), M_OAK, "desk")
for x in (DX0 + 0.05, DX1 - 0.09):
    for y in (DY0 + 0.05, DY1 - 0.09):
        rk.box(f"DeskLeg{x:.1f}{y:.1f}", (x, y, 0), (x + 0.04, y + 0.04, 0.72), M_STEEL, "desk", skip=("-z", "+z"))
MX = (DX0 + DX1) / 2 - 0.15
rk.box("MonitorBody", (MX - 0.42, DY0 + 0.1, 0.92), (MX + 0.42, DY0 + 0.13, 1.42), M_BLACK, "desk")
# Screens face +Y, toward the chair: seen from there, +X is on the left (quads from the bottom left corner, counter clockwise)
rk.quad("MonitorScreen", [(MX + 0.4, DY0 + 0.131, 0.94), (MX - 0.4, DY0 + 0.131, 0.94), (MX - 0.4, DY0 + 0.131, 1.4), (MX + 0.4, DY0 + 0.131, 1.4)], M_WALLPAPER, "desk", uv01=True)
rk.box("MonitorNeck", (MX - 0.03, DY0 + 0.06, 0.76), (MX + 0.03, DY0 + 0.1, 0.95), M_CHROME, "desk")
rk.box("MonitorFoot", (MX - 0.13, DY0 + 0.0, 0.76), (MX + 0.13, DY0 + 0.2, 0.77), M_CHROME, "desk", skip=("-z",))
LX, LY = DX1 - 0.35, DY0 + 0.45
rk.box("LaptopBase", (LX - 0.17, LY - 0.12, 0.76), (LX + 0.17, LY + 0.12, 0.775), M_CHROME, "desk", skip=("-z",))
# The lid hinges on the far edge and leans back toward the wall
rk.quad("LaptopScreen", [(LX + 0.16, LY - 0.118, 0.785), (LX - 0.16, LY - 0.118, 0.785), (LX - 0.16, LY - 0.166, 0.985), (LX + 0.16, LY - 0.166, 0.985)], M_CODE, "desk", uv01=True)
rk.quad("LaptopLid", [(LX - 0.17, LY - 0.122, 0.775), (LX + 0.17, LY - 0.122, 0.775), (LX + 0.17, LY - 0.172, 0.995), (LX - 0.17, LY - 0.172, 0.995)], M_CHROME, "desk", toward=(LX, LY - 1.0, 1.0))
rk.import_prop("desk_lamp_arm_01", (DX0 + 0.2, DY0 + 0.25, 0.76), rot_z_deg=-30, group="desk")
rk.quad("RugDesk", [(X0 + 0.15, Y0 + 0.25, 0.004), (X0 + 2.35, Y0 + 0.25, 0.004), (X0 + 2.35, Y0 + 2.75, 0.004), (X0 + 0.15, Y0 + 2.75, 0.004)], M_RUG2, "floor", uv01=True)

def office_chair(name, x, y, rot_deg):
    objs = [rk.soft_box(f"{name}Seat", (-0.24, -0.23, 0.46), (0.24, 0.23, 0.53), M_ORANGE, "desk", radius=0.03),
            rk.soft_box(f"{name}Back", (-0.23, -0.27, 0.58), (0.23, -0.22, 1.0), M_ORANGE, "desk", radius=0.025),
            rk.box(f"{name}BackBar", (-0.025, -0.25, 0.5), (0.025, -0.23, 0.62), M_CHROME, "desk"),
            rk.cylinder(f"{name}Column", (0, 0, 0.29), 0.025, 0.34, M_CHROME, "desk", segments=12)]
    for k in range(5):
        a = math.radians(90 + 72 * k)
        end = Vector((0.3 * math.cos(a), 0.3 * math.sin(a), 0.07))
        objs.append(rk.rod(f"{name}Spoke{k}", (0, 0, 0.1), end, 0.016, M_CHROME, "desk", segments=6))
        objs.append(rk.cylinder(f"{name}Wheel{k}", (end.x, end.y, 0.03), 0.028, 0.03, M_BLACK, "desk", segments=10, axis="x"))
    rk.transform(objs, Matrix.Translation((x, y, 0)) @ Matrix.Rotation(math.radians(rot_deg), 4, "Z"))

office_chair("Chair", MX + 0.05, DY1 + 0.45, 180)  # Built facing +Y: turned toward the desk, its back toward the player


# ---------------------------------------------------------------------------------------------------------------------------
# Right: a grey cabinet of drawers between two big speakers, plants on it, a poster above, a dartboard nearer the sofa

CY0, CY1, CD, CH = 0.55, 2.95, 0.45, 0.8
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
rk.import_prop("potted_plant_04", (X1 - 0.22, CY0 + 0.35, CH), rot_z_deg=10)
rk.import_prop("calathea_orbifolia_01", (X1 - 0.22, CY1 - 0.45, CH), scale=1.2, pick=2)
rk.box("PosterFrame", (X1 - 0.03, 1.3, 1.25), (X1, 2.2, 2.55), M_BLACK, "furniture", skip=("+x",))
rk.quad("Poster", [(X1 - 0.031, 2.16, 1.29), (X1 - 0.031, 1.34, 1.29), (X1 - 0.031, 1.34, 2.51), (X1 - 0.031, 2.16, 2.51)], M_POSTER, "furniture", uv01=True)
rk.import_prop("dartboard", (X1 - 0.005, 4.2, 1.73), rot_z_deg=-90)

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

# Behind: shelves by the door
rk.import_prop("wooden_display_shelves_01", (-0.6, Y0 + 0.22, 0), rot_z_deg=90)  # Built sideways


rk.setup_render(scene, EYE.z)
bpy.ops.wm.save_as_mainfile(filepath=OUT)
tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in scene.objects if o.type == "MESH")
print("BUILD ok", OUT, "objects", len([o for o in scene.objects if o.type == "MESH"]), "triangles", tris)
