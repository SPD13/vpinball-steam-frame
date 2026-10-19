# Artwork of the game room, drawn procedurally with numpy (no outside artwork): arcade attract screens and marquees in pixel art,
# cabinet side art, and the playfield, backglass and display of the pinball machine. Every function returns an RGB float array
# (rows from the top, sRGB values in 0..1).
import numpy as np

# 5x7 pixel font, enough for the titles and scores
FONT = {
    "A": "01110 10001 10001 11111 10001 10001 10001", "B": "11110 10001 10001 11110 10001 10001 11110",
    "C": "01110 10001 10000 10000 10000 10001 01110", "D": "11110 10001 10001 10001 10001 10001 11110",
    "E": "11111 10000 10000 11110 10000 10000 11111", "F": "11111 10000 10000 11110 10000 10000 10000",
    "G": "01110 10001 10000 10111 10001 10001 01111", "H": "10001 10001 10001 11111 10001 10001 10001",
    "I": "01110 00100 00100 00100 00100 00100 01110", "J": "00111 00010 00010 00010 00010 10010 01100",
    "K": "10001 10010 10100 11000 10100 10010 10001", "L": "10000 10000 10000 10000 10000 10000 11111",
    "M": "10001 11011 10101 10101 10001 10001 10001", "N": "10001 10001 11001 10101 10011 10001 10001",
    "O": "01110 10001 10001 10001 10001 10001 01110", "P": "11110 10001 10001 11110 10000 10000 10000",
    "Q": "01110 10001 10001 10001 10101 10010 01101", "R": "11110 10001 10001 11110 10100 10010 10001",
    "S": "01111 10000 10000 01110 00001 00001 11110", "T": "11111 00100 00100 00100 00100 00100 00100",
    "U": "10001 10001 10001 10001 10001 10001 01110", "V": "10001 10001 10001 10001 10001 01010 00100",
    "W": "10001 10001 10001 10101 10101 10101 01010", "X": "10001 10001 01010 00100 01010 10001 10001",
    "Y": "10001 10001 01010 00100 00100 00100 00100", "Z": "11111 00001 00010 00100 01000 10000 11111",
    "0": "01110 10001 10011 10101 11001 10001 01110", "1": "00100 01100 00100 00100 00100 00100 01110",
    "2": "01110 10001 00001 00010 00100 01000 11111", "3": "11110 00001 00001 01110 00001 00001 11110",
    "4": "00010 00110 01010 10010 11111 00010 00010", "5": "11111 10000 11110 00001 00001 10001 01110",
    "6": "00110 01000 10000 11110 10001 10001 01110", "7": "11111 00001 00010 00100 01000 01000 01000",
    "8": "01110 10001 10001 01110 10001 10001 01110", "9": "01110 10001 10001 01111 00001 00010 01100",
    "-": "00000 00000 00000 11111 00000 00000 00000", " ": "00000 00000 00000 00000 00000 00000 00000",
}

def hexrgb(h):
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5)])

def text_mask(text, scale=1):
    """Mask of a line of text in the pixel font, 1 pixel between letters, scaled by an integer."""
    cols = []
    for ch in text.upper():
        rows = FONT.get(ch, FONT[" "]).split()
        glyph = np.array([[c == "1" for c in r] for r in rows])
        cols.append(glyph)
        cols.append(np.zeros((7, 1), bool))
    mask = np.concatenate(cols[:-1], axis=1)
    return np.kron(mask, np.ones((scale, scale), bool))

def stamp(img, mask, x, y, color):
    h, w = mask.shape
    region = img[y:y + h, x:x + w]
    region[mask[:region.shape[0], :region.shape[1]]] = color

def stamp_centered(img, text, y, scale, color):
    m = text_mask(text, scale)
    stamp(img, m, (img.shape[1] - m.shape[1]) // 2, y, color)

def sprite(rows):
    return np.array([[c == "X" for c in r] for r in rows])

def upscale(img, factor):
    return np.kron(img, np.ones((factor, factor, 1)))

def grid(h, w):
    return np.mgrid[0:h, 0:w]

def scanlines(img, strength=0.25):
    out = img.copy()
    out[1::2] *= 1.0 - strength
    return out


# ---------------------------------------------------------------------------------------------------------------------------
# Attract screens, 160 x 128 pixels then upscaled 4 times (nearest) so that the pixels stay crisp

# Saucers and winged bugs of our own
ALIEN_A = sprite(["...XXXX...", "..X.XX.X..", ".XXXXXXXX.", "XX.XX.XX.X", "XXXXXXXXXX", "..X....X..", ".X......X."])
ALIEN_B = sprite(["X........X", ".X.XXXX.X.", "..XXXXXX..", ".XX.XX.XX.", "XXXXXXXXXX", "X..X..X..X", "...X..X..."])
SHIP = sprite(["....X....", "...XXX...", "...XXX...", ".XXXXXXX.", "XXXXXXXXX", "XX.XXX.XX"])

def screen_shooter():
    h, w = 128, 160
    img = np.zeros((h, w, 3))
    rng = np.random.default_rng(3)
    for _ in range(90):
        y, x = rng.integers(0, h), rng.integers(0, w)
        img[y, x] = rng.choice([0.4, 0.7, 1.0]) * np.array([0.8, 0.85, 1.0])
    colors = [hexrgb("#ff4fd8"), hexrgb("#4fe0ff"), hexrgb("#7dff5a"), hexrgb("#ffd34f")]
    for row in range(4):
        for col in range(8):
            spr = ALIEN_A if row % 2 == 0 else ALIEN_B
            stamp(img, spr, 18 + col * 16, 20 + row * 12, colors[row])
    stamp(img, SHIP, 74, 112, hexrgb("#ffffff"))
    img[96:108, 79] = hexrgb("#ffff80")  # Shot
    img[70:76, 50] = hexrgb("#ff6060")
    for x0 in (20, 64, 108, 140):  # Shields
        img[100:106, x0 - 6:x0 + 8] = hexrgb("#38c858")
        img[104:106, x0 - 1:x0 + 3] = 0
    stamp(img, text_mask("1UP 04250"), 4, 3, hexrgb("#ff5050"))
    stamp(img, text_mask("HI 25000"), 100, 3, hexrgb("#ffffff"))
    return scanlines(upscale(img, 4))

def screen_maze():
    h, w = 128, 160
    img = np.zeros((h, w, 3))
    cell = 8
    rng = np.random.default_rng(7)
    wall = hexrgb("#2a4bff")
    # A symmetric maze of blocks on a grid, with corridors of dots
    blocks = rng.random((14, 10)) < 0.38
    blocks = np.concatenate([blocks, blocks[:, ::-1]], axis=1)
    for gy in range(14):
        for gx in range(20):
            y, x = 12 + gy * cell, gx * cell
            if gy in (0, 13) or gx in (0, 19) or (blocks[gy, gx] and gy % 2 == 1 and gx % 2 == 1):
                img[y:y + cell, x:x + cell] = wall * 0.35
                img[y, x:x + cell] = wall
                img[y + cell - 1, x:x + cell] = wall
                img[y:y + cell, x] = wall
                img[y:y + cell, x + cell - 1] = wall
            else:
                img[y + 3:y + 5, x + 3:x + 5] = hexrgb("#ffd8a0")
    yy, xx = grid(h, w)
    for (cx, cy, col) in ((84, 76, "#ffe600"), (36, 44, "#ff3030"), (124, 44, "#ff9ee6"), (52, 108, "#30e8ff")):
        disc = (xx - cx) ** 2 + (yy - cy) ** 2 <= 12
        img[disc] = hexrgb(col)
    stamp(img, text_mask("SCORE 1280"), 4, 3, hexrgb("#ffffff"))
    return scanlines(upscale(img, 4))

def screen_bricks():
    h, w = 128, 160
    img = np.zeros((h, w, 3))
    img[:, :4] = img[:, -4:] = img[12:16, :] = 0.55
    rows = ["#ff3b3b", "#ff8a2b", "#ffd52b", "#3bff6a", "#2bc8ff", "#9b5bff"]
    rng = np.random.default_rng(11)
    for r, col in enumerate(rows):
        for c in range(13):
            if rng.random() < 0.85:
                y, x = 26 + r * 6, 6 + c * 12
                img[y:y + 5, x:x + 11] = hexrgb(col)
                img[y, x:x + 11] = np.minimum(hexrgb(col) * 1.4, 1)
    img[116:119, 64:88] = hexrgb("#e0e0ff")  # Paddle
    img[90:93, 100:103] = 1.0  # Ball
    stamp(img, text_mask("1 02470"), 6, 3, hexrgb("#ffffff"))
    stamp(img, text_mask("BALL 3"), 112, 3, hexrgb("#ffffff"))
    return scanlines(upscale(img, 4))

def screen_racer():
    h, w = 128, 160
    yy, xx = grid(h, w)
    img = np.zeros((h, w, 3))
    horizon = 54
    sky = np.clip((horizon - yy) / horizon, 0, 1)[..., None]
    img[:] = (1 - sky) * hexrgb("#ff3c8e") + sky * hexrgb("#1a0638")
    sun = ((xx - 80) ** 2 + (yy - 46) ** 2 <= 22 ** 2) & (yy < horizon) & ~((yy % 6 < 2) & (yy > 36))
    img[sun] = (hexrgb("#ffd040") * (1 - (yy[sun] - 24) / 30.0)[:, None] + hexrgb("#ff5070") * ((yy[sun] - 24) / 30.0)[:, None])
    ground = yy >= horizon
    img[ground] = hexrgb("#12021f")
    depth = (yy - horizon + 1).clip(1)
    # Perspective grid lines on the ground
    lines_h = ground & ((np.log(depth) * 7) % 1 < 0.18)
    lines_v = ground & ((((xx - 80) / depth) * 6) % 6 < 0.8 * 6 / depth.clip(1) + 0.25)
    img[lines_h | lines_v] = hexrgb("#c03cff")
    # Road
    half = (yy - horizon) * 0.9 + 2
    road = ground & (np.abs(xx - 80) < half)
    img[road] = hexrgb("#2a2238")
    edge = ground & (np.abs(np.abs(xx - 80) - half) < 1.2)
    img[edge] = hexrgb("#ffffff")
    stripe = road & (np.abs(xx - 80) < 0.04 * half + 0.6) & ((np.log(depth) * 5) % 1 < 0.5)
    img[stripe] = hexrgb("#ffe066")
    car = sprite(["..XXXXXXX..", ".XXXXXXXXX.", "XXX.XXX.XXX", "XXXXXXXXXXX", "XX.......XX"])
    stamp(img, car, 75, 112, hexrgb("#ff2e2e"))
    stamp(img, text_mask("TIME 48"), 4, 3, hexrgb("#ffff60"))
    stamp(img, text_mask("128 KMH"), 116, 3, hexrgb("#ffffff"))
    return scanlines(upscale(img, 4))

SCREENS = [screen_shooter, screen_maze, screen_bricks, screen_racer]


# ---------------------------------------------------------------------------------------------------------------------------
# Marquees (titles invented for the room), 128 x 32 then upscaled 4 times, and cabinet side art

def marquee(title, top, bottom, text_color):
    h, w = 40, 144
    yy, xx = grid(h, w)
    t = (yy / (h - 1))[..., None]
    img = (1 - t) * hexrgb(top) + t * hexrgb(bottom)
    rng = np.random.default_rng(len(title))
    for _ in range(40):
        img[rng.integers(0, h), rng.integers(0, w)] = 1.0
    m = text_mask(title, 2)
    x, y = (w - m.shape[1]) // 2, (h - m.shape[0]) // 2
    stamp(img, m, x + 1, y + 1, hexrgb("#000000"))
    stamp(img, m, x, y, hexrgb(text_color))
    img[0, :] = img[-1, :] = hexrgb(text_color)
    return upscale(img, 4)

MARQUEES = [("STAR SWARM", "#2a0a5e", "#ff2f8e", "#ffe14f"), ("MAZE CHASE", "#00184f", "#2a7bff", "#ffffff"),
            ("BRICK BLITZ", "#4f0a00", "#ff8a1f", "#fff4c0"), ("NEON DRIVE", "#14002e", "#b026ff", "#3ff4ff")]

def side_art(base, stripe1, stripe2, w=256, h=512):
    """Side panel: a solid color with two diagonal racing stripes, darker toward the floor."""
    yy, xx = grid(h, w)
    img = np.ones((h, w, 3)) * hexrgb(base)
    d = (xx * 0.8 + (h - yy)) / h
    img[(d > 0.62) & (d < 0.72)] = hexrgb(stripe1)
    img[(d > 0.76) & (d < 0.8)] = hexrgb(stripe2)
    img *= (0.65 + 0.35 * (1 - yy / h))[..., None]
    return img

SIDE_ARTS = [("#151525", "#ff2f8e", "#ffe14f"), ("#0b1d4a", "#2a7bff", "#ffffff"), ("#2a0d05", "#ff8a1f", "#ffd54f"), ("#140726", "#b026ff", "#3ff4ff")]


# ---------------------------------------------------------------------------------------------------------------------------
# Pinball machine

def _circle(img, yy, xx, cx, cy, r, color, ring=0.0):
    d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    mask = d <= r if ring == 0 else (d <= r) & (d >= r - ring)
    img[mask] = color

def _polygon(img, yy, xx, pts, color):
    """Convex polygon fill."""
    area = sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))
    mask = np.ones(yy.shape, bool)
    for i in range(len(pts)):
        (x1, y1), (x2, y2) = pts[i], pts[(i + 1) % len(pts)]
        mask &= ((x2 - x1) * (yy - y1) - (y2 - y1) * (xx - x1)) * np.sign(area) >= 0
    img[mask] = color

def playfield(w=512, h=1024):
    """Top view of a playfield, the drain at the bottom of the image (toward the player)."""
    ss = 2
    yy, xx = grid(h * ss, w * ss)
    yy, xx = yy / ss, xx / ss
    t = (yy / h)[..., None]
    img = (1 - t) * hexrgb("#0b2a6b") + t * hexrgb("#1a0b3d")
    # Arcs of colored inserts around the middle
    for i, a in enumerate(np.linspace(-0.9, 0.9, 7)):
        cx, cy = w / 2 + np.sin(a) * 170, 560 - np.cos(a) * 170
        _circle(img, yy, xx, cx, cy, 16, hexrgb(["#ff3b3b", "#ffd52b", "#3bff6a", "#2bc8ff", "#3bff6a", "#ffd52b", "#ff3b3b"][i]))
        _circle(img, yy, xx, cx, cy, 16, hexrgb("#ffffff"), ring=2.5)
    # Big center insert: an arrow pointing up the playfield over a round insert
    _circle(img, yy, xx, w / 2, 560, 44, hexrgb("#ffffff"))
    _circle(img, yy, xx, w / 2, 560, 38, hexrgb("#ff8a1f"))
    _polygon(img, yy, xx, [(w / 2, 470), (w / 2 + 30, 510), (w / 2 - 30, 510)], hexrgb("#ffe14f"))
    # Pop bumpers near the top
    for cx, cy in ((180, 230), (330, 230), (255, 320)):
        _circle(img, yy, xx, cx, cy, 34, hexrgb("#ffffff"))
        _circle(img, yy, xx, cx, cy, 28, hexrgb("#e8132c"))
        _circle(img, yy, xx, cx, cy, 12, hexrgb("#ffd52b"))
    # Top lanes
    for x in (150, 210, 270, 330, 390):
        img[(np.abs(xx - x) < 3) & (yy > 70) & (yy < 150)] = hexrgb("#d0d0d0")
    for x in (180, 240, 300, 360):
        _circle(img, yy, xx, x, 120, 9, hexrgb("#ffe14f"))
    # Outer wall and the arch at the top
    d_top = np.sqrt((xx - w / 2) ** 2 + (yy - w / 2) ** 2)
    img[(yy < w / 2) & (d_top > w / 2 - 18)] = hexrgb("#c0c4cc")
    img[(xx < 18) | (xx > w - 18)] = hexrgb("#c0c4cc")
    img[(xx > w - 64) & (xx < w - 58) & (yy > 300)] = hexrgb("#c0c4cc")  # Shooter lane
    # Slingshots and inlanes
    _polygon(img, yy, xx, [(90, 760), (150, 860), (90, 860)], hexrgb("#ffffff"))
    _polygon(img, yy, xx, [(w - 130, 760), (w - 130, 860), (w - 190, 860)], hexrgb("#ffffff"))
    _polygon(img, yy, xx, [(96, 775), (138, 852), (96, 852)], hexrgb("#ff2f8e"))
    _polygon(img, yy, xx, [(w - 136, 775), (w - 136, 852), (w - 178, 852)], hexrgb("#ff2f8e"))
    # Flippers
    for (x0, y0, x1, y1) in ((160, 900, 230, 930), (w - 124, 900, w - 194, 930)):
        for s in np.linspace(0, 1, 40):
            _circle(img, yy, xx, x0 + (x1 - x0) * s, y0 + (y1 - y0) * s, 12 - 6 * s, hexrgb("#ffffff"))
        _circle(img, yy, xx, x0, y0, 5, hexrgb("#e8132c"))
    _circle(img, yy, xx, w / 2, 700, 13, hexrgb("#e0e4ea"))  # Ball
    img = img.reshape(h, ss, w, ss, 3).mean(axis=(1, 3))
    return img

def backglass(w=512, h=384):
    yy, xx = grid(h, w)
    t = (yy / h)[..., None]
    img = (1 - t) * hexrgb("#120233") + t * hexrgb("#ff2f8e") * 0.8
    rng = np.random.default_rng(5)
    for _ in range(160):
        y, x = rng.integers(0, h), rng.integers(0, w)
        img[y, x] = 1.0
    # A big shiny ball
    d = np.sqrt((xx - w * 0.72) ** 2 + (yy - h * 0.58) ** 2)
    ball = d < 90
    shade = np.clip(1 - np.sqrt((xx - w * 0.72 + 30) ** 2 + (yy - h * 0.58 + 30) ** 2) / 150, 0, 1)
    img[ball] = (0.25 + 0.75 * shade[ball])[:, None] * hexrgb("#e8ecf4")
    # Speed lines behind it
    for i in range(6):
        y = int(h * 0.42 + i * 18)
        img[y:y + 4, 40:int(w * 0.72) - 100 + i * 6] = hexrgb("#ffe14f")
    m = text_mask("PINBALL", 8)
    stamp(img, m, (w - m.shape[1]) // 2 + 5, 37, hexrgb("#000000"))
    stamp(img, m, (w - m.shape[1]) // 2, 32, hexrgb("#ffe14f"))
    img[:6] = img[-6:] = img[:, :6] = img[:, -6:] = hexrgb("#ffe14f")
    return img

def dot_matrix(text="PLAY BALL", w=128, h=32):
    """An orange dot matrix display, each dot a disc."""
    dots = np.zeros((h, w), bool)
    m = text_mask(text, 2)
    dots[(h - m.shape[0]) // 2:(h - m.shape[0]) // 2 + m.shape[0], (w - m.shape[1]) // 2:(w - m.shape[1]) // 2 + m.shape[1]] = m
    scale = 4
    yy, xx = grid(h * scale, w * scale)
    disc = ((yy % scale) - 1.5) ** 2 + ((xx % scale) - 1.5) ** 2 <= 2.2
    on = np.kron(dots, np.ones((scale, scale), bool))
    img = np.zeros((h * scale, w * scale, 3))
    img[disc] = hexrgb("#3a1500")
    img[disc & on] = hexrgb("#ff7a1a")
    return img

def pinball_side(w=512, h=192):
    yy, xx = grid(h, w)
    img = np.ones((h, w, 3)) * hexrgb("#120233")
    d = (xx + yy * 1.5) / w
    for i, col in enumerate(("#ff2f8e", "#ffe14f", "#3ff4ff")):
        img[np.abs(((d * 6) % 3) - i) < 0.18] = hexrgb(col)
    img *= (0.7 + 0.3 * (1 - yy / h))[..., None]
    return img
