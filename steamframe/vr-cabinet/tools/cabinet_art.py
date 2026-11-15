# Generic artwork of the default VR cabinet, drawn with numpy (no outside artwork, no text, no look-alike of a real machine): side
# panels, front panel, backbox sides, a default backglass, the speaker panel and the coin door inserts. Every function returns an RGB
# float array (rows from the top, sRGB values in 0..1). Shapes are drawn 2x larger then averaged, for smooth edges.
import numpy as np

SS = 2  # Supersampling

NAVY_TOP, NAVY_BOTTOM = "#121a4a", "#05071a"
BAND = ("#ffb21a", "#ff4f1f", "#ff1f6d", "#9b2bff")  # Warm sweep, from the front to the back
CYAN = "#2de2f0"
GOLD = "#e0b44c"


def hexrgb(h):
    return np.array([int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5)])


def ramp(t, colors):
    """Color ramp through evenly spaced colors, t in 0..1 (any shape)."""
    cols = np.array([hexrgb(c) for c in colors])
    t = np.clip(t, 0, 1) * (len(cols) - 1)
    i = np.minimum(t.astype(int), len(cols) - 2)
    f = (t - i)[..., None]
    return cols[i] * (1 - f) + cols[i + 1] * f


def canvas(w, h):
    """Supersampled pixel grid: y down, x right, in output pixels."""
    yy, xx = np.mgrid[0:h * SS, 0:w * SS]
    return (yy + 0.5) / SS, (xx + 0.5) / SS


def down(img, w, h):
    return img.reshape(h, SS, w, SS, 3).mean(axis=(1, 3))


def blend(img, mask, color, alpha=1.0):
    a = (np.clip(mask, 0, 1) * alpha)[..., None]
    img[:] = img * (1 - a) + np.broadcast_to(color, img.shape) * a


def smooth_step(d, width):
    """1 inside (d < 0), 0 outside, over `width` pixels."""
    return np.clip(0.5 - d / max(width, 1e-6), 0, 1)


def stars(img, yy, xx, rng, count, region, size=1.6):
    h, w = yy.shape[0] // SS, yy.shape[1] // SS
    for _ in range(count):
        x, y = rng.uniform(0, w), rng.uniform(0, h)
        if not region(x, y):
            continue
        r = rng.uniform(0.6, 1.0) * size
        x0, x1, y0, y1 = int(max(0, (x - 8 * r) * SS)), int(min(w * SS, (x + 8 * r) * SS)), int(max(0, (y - 8 * r) * SS)), int(min(h * SS, (y + 8 * r) * SS))
        sy, sx = yy[y0:y1, x0:x1], xx[y0:y1, x0:x1]
        d = np.sqrt((sx - x) ** 2 + (sy - y) ** 2)
        glow = np.exp(-(d / r) ** 2) + 0.35 * np.exp(-d / (3 * r)) * (np.abs(sx - x) < 0.6 * r) + 0.35 * np.exp(-d / (3 * r)) * (np.abs(sy - y) < 0.6 * r)
        sub = img[y0:y1, x0:x1]
        sub[:] = sub + np.clip(glow, 0, 1)[..., None] * hexrgb("#fff6e0") * 0.9


def ball(img, yy, xx, cx, cy, r, trail_dir=(1.0, -0.35), trail=6.0):
    """A chrome ball with a highlight, and motion streaks behind it (along trail_dir, in units of r)."""
    dx, dy = trail_dir
    n = np.hypot(dx, dy)
    dx, dy = dx / n, dy / n
    for i, (off, length, col) in enumerate(((-0.55, 0.85, CYAN), (0.0, 1.0, "#ffffff"), (0.55, 0.75, BAND[0]))):
        # Streak: a tapered stroke from the ball backwards
        px, py = xx - cx, yy - cy
        along = px * dx + py * dy
        across = -px * dy + py * dx - off * r
        t = along / (trail * r * length)
        width = 0.16 * r * (1 - t)
        mask = (t > 0.05) & (t < 1) & (np.abs(across) < width)
        blend(img, mask * np.clip(1 - t, 0, 1) ** 1.5, hexrgb(col), 0.9)
    d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    inside = smooth_step(d - r, 1.5)
    # Chrome: dark rim, a sky reflection on the top half, the floor below, a sharp highlight
    nx, ny = (xx - cx) / r, (yy - cy) / r
    nz = np.sqrt(np.clip(1 - nx ** 2 - ny ** 2, 0, 1))
    sky = np.clip(-ny * 0.9 + 0.3, 0, 1)[..., None]
    base = hexrgb("#2a2f45") * (1 - sky) + hexrgb("#dfe8ff") * sky
    base = base * (0.35 + 0.65 * nz[..., None])
    hl = np.exp(-(((nx + 0.35) ** 2 + (ny + 0.4) ** 2) / 0.02))[..., None]
    col = np.clip(base + hl, 0, 1)
    a = inside[..., None]
    img[:] = img * (1 - a) + col * a


def band(img, yy, xx, curve, half_width, colors, outline="#ffffff", outline_px=3.0):
    """A band around curve(x) (y of its center for each column), colored along x."""
    w = xx.max() + 0.5
    cy = curve(xx)
    d = np.abs(yy - cy) - half_width(xx)
    if outline:
        blend(img, smooth_step(d - outline_px, 1.0), hexrgb(outline))
    t = xx / w
    col = ramp(t, colors)
    # A soft shine along the band
    shine = np.clip(1 - np.abs((yy - cy) / np.maximum(half_width(xx), 1) + 0.35) * 1.6, 0, 1)[..., None] * 0.35
    col = np.clip(col + shine, 0, 1)
    a = smooth_step(d, 1.0)[..., None]
    img[:] = img * (1 - a) + col * a


def pinstripe(img, yy, xx, inside_dist, inset, px=4.0, color=GOLD):
    """A line `inset` pixels inside the shape whose signed distance (negative inside) is inside_dist."""
    d = np.abs(inside_dist + inset) - px / 2
    blend(img, smooth_step(d, 1.0), hexrgb(color))


def background(yy, h, top=NAVY_TOP, bottom=NAVY_BOTTOM):
    t = (yy / h)[..., None]
    return hexrgb(top) * (1 - t) + hexrgb(bottom) * t


# ---------------------------------------------------------------------------------------------------------------------------
# Side panels: the image spans the bounding rectangle of the panel, the front at the left, the top at row 0. The panel is lower at
# the front (front_h) than at the back (back_h); its top is sloped up to `flat_from` (from the front, in meters), then flat.

def side(length, front_h, back_h, flat_from, w=2048):
    h = int(round(w * back_h / length))
    yy, xx = canvas(w, h)
    k = w / length  # Pixels per meter
    img = background(yy, h)
    # Panel outline in pixels: top edge row as a function of x
    top_row = lambda x: h - k * (front_h + (back_h - front_h) * np.clip(x / (k * flat_from), 0, 1))
    inside = np.maximum.reduce([top_row(xx) - yy, yy - h, -xx, xx - w])  # < 0 inside
    rng = np.random.default_rng(11)
    # Big warm sweep from the lower front to the upper back, and a cyan one above it
    sweep = lambda x: h * 0.78 - (x / w) ** 1.15 * h * 0.42 + np.sin(x / w * 5.0) * h * 0.04
    stars(img, yy, xx, rng, 260, lambda x, y: y < sweep(x) - 0.12 * h, size=1.8)
    band(img, yy, xx, lambda x: sweep(x) - h * 0.17, lambda x: h * (0.018 + 0.02 * x / w), (CYAN, "#5b7bff"), outline=None)
    band(img, yy, xx, sweep, lambda x: h * (0.05 + 0.07 * x / w), BAND, outline="#ffffff", outline_px=4)
    band(img, yy, xx, lambda x: sweep(x) + h * 0.16, lambda x: h * (0.012 + 0.012 * x / w), (GOLD, BAND[1]), outline=None)
    # The ball, near the front under the top edge, racing toward the player
    ball(img, yy, xx, w * 0.2, h * 0.5, h * 0.085, trail_dir=(1.0, -0.42), trail=7.0)
    pinstripe(img, yy, xx, inside, 26, px=6)
    pinstripe(img, yy, xx, inside, 40, px=2.5, color=CYAN)
    return down(img, w, h)


def front(width, height, w=1024):
    h = int(round(w * height / width))
    yy, xx = canvas(w, h)
    img = background(yy, h)
    rng = np.random.default_rng(12)
    stars(img, yy, xx, rng, 70, lambda x, y: y < h * 0.3, size=1.5)
    band(img, yy, xx, lambda x: h * 0.62 + 0 * x, lambda x: h * 0.06 + 0 * x, BAND, outline="#ffffff", outline_px=3)
    band(img, yy, xx, lambda x: h * 0.48 + 0 * x, lambda x: h * 0.015 + 0 * x, (CYAN, "#5b7bff"), outline=None)
    band(img, yy, xx, lambda x: h * 0.76 + 0 * x, lambda x: h * 0.01 + 0 * x, (GOLD, BAND[1]), outline=None)
    inside = np.maximum.reduce([-yy, yy - h, -xx, xx - w])
    pinstripe(img, yy, xx, inside, 18, px=5)
    return down(img, w, h)


def backbox_side(depth_top, depth_bottom, height, w=512):
    """Backbox side, the front at the left: the front edge leans toward the player at the top (deeper at the top)."""
    h = int(round(w * height / depth_top))
    yy, xx = canvas(w, h)
    k = w / depth_top
    img = background(yy, h, top="#0d1440")
    front_col = lambda y: k * (depth_top - depth_bottom) * (y / h)  # The front edge, from x=0 at the top to the bottom
    inside = np.maximum.reduce([front_col(yy) - xx, -yy, yy - h, xx - w])
    rng = np.random.default_rng(13)
    stars(img, yy, xx, rng, 80, lambda x, y: y < h * 0.55, size=1.6)
    # Bands rising toward the top, continuing the side of the cabinet
    band(img, yy, xx, lambda x: h * 0.86 - x / w * h * 0.12, lambda x: h * 0.045 + 0 * x, BAND, outline="#ffffff", outline_px=3)
    band(img, yy, xx, lambda x: h * 0.75 - x / w * h * 0.12, lambda x: h * 0.012 + 0 * x, (CYAN, "#5b7bff"), outline=None)
    pinstripe(img, yy, xx, inside, 16, px=5)
    return down(img, w, h)


def backglass(w=1024, h=720):
    """Shown when the table has no backglass of its own: a night sky over the warm sweep, a big ball, no text."""
    yy, xx = canvas(w, h)
    img = background(yy, h, top="#060a26", bottom="#1d0b3d")
    rng = np.random.default_rng(14)
    stars(img, yy, xx, rng, 220, lambda x, y: True, size=1.8)
    band(img, yy, xx, lambda x: h * 0.86 - (x / w) * h * 0.28, lambda x: h * (0.05 + 0.04 * x / w), BAND, outline="#ffffff", outline_px=3)
    band(img, yy, xx, lambda x: h * 0.72 - (x / w) * h * 0.28, lambda x: h * 0.012 + 0 * x, (CYAN, "#5b7bff"), outline=None)
    ball(img, yy, xx, w * 0.62, h * 0.42, h * 0.16, trail_dir=(-1.0, 0.25), trail=5.0)
    inside = np.maximum.reduce([-yy, yy - h, -xx, xx - w])
    pinstripe(img, yy, xx, inside, 14, px=6)
    return down(img, w, h)


def speaker_panel(width, height, dmd_rect, speakers, w=1024):
    """Black panel with two perforated speaker grilles; the display window (dmd_rect = x0, y0, x1, y1 in meters from the top left) is
    dark glass, the display itself is drawn in front of it by the application."""
    h = int(round(w * height / width))
    yy, xx = canvas(w, h)
    k = w / width
    img = np.ones(yy.shape + (3,)) * hexrgb("#0b0b0e")
    # Brushed look: faint horizontal streaks
    rng = np.random.default_rng(15)
    streak = rng.normal(0, 1, (yy.shape[0], 1)) * 0.012
    img += streak[..., None]
    for (cx, cy, r) in speakers:
        cx, cy, r = cx * k, cy * k, r * k
        d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
        blend(img, smooth_step(np.abs(d - r) - 2.5, 1.0), hexrgb("#8a8f99"))  # Chrome ring
        pitch = r / 7.5
        holes = ((np.mod(xx - cx, pitch) - pitch / 2) ** 2 + (np.mod(yy - cy, pitch) - pitch / 2) ** 2) < (pitch * 0.32) ** 2
        blend(img, holes * (d < r - 6), hexrgb("#000000"))
        blend(img, (~holes) * (d < r - 6), hexrgb("#1c1d22"))
    x0, y0, x1, y1 = (v * k for v in dmd_rect)
    inside = np.maximum.reduce([x0 - xx, xx - x1, y0 - yy, yy - y1])
    blend(img, smooth_step(inside, 1.0), hexrgb("#020203"))
    pinstripe(img, yy, xx, inside, -6, px=3, color="#6a6f78")  # Bezel around the window
    return down(np.clip(img, 0, 1), w, h)


def coin_insert(w=64, h=128):
    """The red lit price insert of a coin entry: a slot outline over a red window."""
    yy, xx = canvas(w, h)
    img = np.ones(yy.shape + (3,)) * hexrgb("#c8141e")
    shade = np.clip(1 - np.abs(xx / w - 0.5) * 1.2, 0, 1)[..., None]
    img = img * (0.6 + 0.4 * shade)
    slot = np.maximum(np.abs(xx - w / 2) - w * 0.08, np.abs(yy - h * 0.32) - h * 0.2)
    blend(img, smooth_step(slot, 1.0), hexrgb("#1a0204"))
    arrow = np.maximum(np.abs(xx - w / 2) - (yy - h * 0.62) * 0.6, h * 0.62 - yy)
    blend(img, smooth_step(np.maximum(arrow, yy - h * 0.82), 1.0), hexrgb("#ffe6c0"))
    return down(img, w, h)
