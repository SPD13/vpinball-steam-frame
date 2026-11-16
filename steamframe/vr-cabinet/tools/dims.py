# Dimensions of the default VR cabinet, a Williams WPC standard body (measured plans: github.com/jonaskello/wpc-cabinet, figures only).
# Meters. Axes of the design: X to the player's right, Y away from the player, Z up; X = 0 is the middle of the playfield, Y = 0 its
# front edge (under the lockdown bar), Z = 0 the floor. The application fits the cabinet to each table (see DefaultCabinet.cpp): the
# nominal playfield below is the one the model is drawn around.
import math

IN = 0.0254

# Nominal table the model is drawn around (VPX defaults where VPX has one)
PF_W = 20.25 * IN          # Playfield width
PF_D = 42.0 * IN           # Playfield length (WPC)
SLOPE = 6.5                # Playfield slope, degrees
LOCKBAR_TOP = 0.91         # Lockbar top above the floor (VPX default ground to lockbar height, 91 cm)
GLASS_BOTTOM = 3.0 * IN    # Lockbar top to playfield front edge, the table's 'glass bottom height' in VR
PF_Z = LOCKBAR_TOP - GLASS_BOTTOM

# Cabinet body
CAB_W = 22.0 * IN          # Outside width (sides 3/4" plywood)
WALL = 0.75 * IN
SIDE_L = 51.5 * IN         # Outside length
FRONT_H = 15.625 * IN      # Height of the body at the front
BACK_H = 23.5 * IN         # ... and at the back
FLAT = 7.125 * IN          # Flat part of the top, at the back, under the backbox
Y_FRONT = -1.5 * IN        # Front face of the cabinet (the playfield front edge is 1.5" behind it)
Y_BACK = Y_FRONT + SIDE_L
Y_FLAT = Y_BACK - FLAT
LOCKBAR_RISE = 0.016       # Lockbar top above the front top edge of the body
Z_FRONT_TOP = LOCKBAR_TOP - LOCKBAR_RISE
Z_BOTTOM = Z_FRONT_TOP - FRONT_H
Z_BACK_TOP = Z_BOTTOM + BACK_H
TOP_ANGLE = math.degrees(math.atan2(Z_BACK_TOP - Z_FRONT_TOP, Y_FLAT - Y_FRONT))  # About 10 degrees

# Backbox
BB_W = 28.0 * IN
BB_H = 28.5 * IN
BB_D_TOP = 9.875 * IN
BB_D_BOTTOM = 6.375 * IN
BB_LEAN = math.atan2(BB_D_TOP - BB_D_BOTTOM, BB_H)  # The front leans toward the player, about 7 degrees
Z_BB = Z_BACK_TOP
BB_SPEAKER = (0.75 * IN, 9.0 * IN)       # Speaker panel, along the front from its bottom edge
BB_TRANSLITE = (9.25 * IN, 27.75 * IN)   # Backglass (translite) window
BB_INNER_W = BB_W - 2 * WALL
TRANSLITE_W = 26.0 * IN
DMD_W, DMD_H = 13.5 * IN, 3.75 * IN      # Display window, centered in the speaker panel
FULL_DMD_H = 7.0 * IN                    # Full DMD variant: a 16:9 screen (14.3") between the speakers, for PinUP Player packs drawing one
FULL_DMD_W = FULL_DMD_H * 16 / 9
SPEAKER_R = 2.1 * IN
SPEAKER_X = 9.4 * IN

# Legs: chamfered square tubes on the corners, levelers below
LEG = 2.25 * IN
LEG_OUT = 1.25 * IN        # How far a leg stands out of each face of the corner
LEG_TOP_BELOW = 6.5 * IN   # Leg tops below the front top edge (they stay under the plunger)
LEG_BOTTOM = 1.75 * IN

# Front parts
COIN_DOOR_W, COIN_DOOR_H = 13.25 * IN, 11.4 * IN
COIN_DOOR_Z = Z_BOTTOM + 2.4 * IN + COIN_DOOR_H / 2
LEG_TOP = Z_FRONT_TOP - LEG_TOP_BELOW
START_X, START_Z = -CAB_W / 2 + 3.4 * IN, Z_FRONT_TOP - 2.3 * IN
FLIPPER_Y, FLIPPER_Z = Y_FRONT + 3.25 * IN, Z_FRONT_TOP - 2.3 * IN
PLUNGER_X = PF_W / 2 - 0.8 * IN            # Shooter lane of a usual table; the application moves it to the table's plunger
PLUNGER_Z = PF_Z - 1.1 * IN                # Where the rod comes out of the front, below the playfield front edge

# Zones of the fit (see DefaultCabinet.cpp): across, the middle keeps its size (coin door), the sides move with the playfield width and
# the part between is stretched; lengthwise, the front keeps its place, the back moves with the playfield length, the middle stretches.
FIT_X_KEEP = 0.175
FIT_X_SIDE = PF_W / 2 - 0.002
FIT_Y_FRONT = 0.10
FIT_Y_BACK = PF_D


def nominal():
    """Figures the application needs, saved in the glTF file (extras of the scene)."""
    return {
        "playfield_width": PF_W, "playfield_length": PF_D, "slope": SLOPE, "lockbar_top": LOCKBAR_TOP, "glass_bottom": GLASS_BOTTOM,
        "half_width": CAB_W / 2, "leg_top": LEG_TOP, "y_front": Y_FRONT, "z_bottom": Z_BOTTOM, "z_front_top": Z_FRONT_TOP, "y_flat": Y_FLAT, "z_back_top": Z_BACK_TOP,
        "fit_x_keep": FIT_X_KEEP, "fit_x_side": FIT_X_SIDE, "fit_y_front": FIT_Y_FRONT, "fit_y_back": FIT_Y_BACK,
        "plunger": [PLUNGER_X, Y_FRONT, PLUNGER_Z],
    }
