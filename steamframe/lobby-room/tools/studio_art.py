# Artwork of the sunlit studio, drawn procedurally with numpy (no outside artwork): flat-woven striped rugs, a poster, the wallpaper of
# the monitor and the screen of the laptop. Every function returns an RGB float array (rows from the top, sRGB values in 0..1).
import numpy as np

from gameroom_art import grid, hexrgb, stamp, text_mask

def kilim_rug(palette, w=1024, h=640, seed=1):
    """A flat-woven rug: bands of color across its length, some with a zigzag, a plain border and a slight weave."""
    rng = np.random.default_rng(seed)
    yy, xx = grid(h, w)
    img = np.zeros((h, w, 3))
    x = 0
    bands = []
    while x < w:
        width = int(rng.choice([8, 14, 22, 36, 60]))
        bands.append((x, min(w, x + width), palette[rng.integers(len(palette))], rng.random() < 0.3))
        x += width
    for x0, x1, col, zigzag in bands:
        img[:, x0:x1] = hexrgb(col)
        if zigzag and x1 - x0 >= 22:
            other = hexrgb(palette[(palette.index(col) + 2) % len(palette)])
            mid = (x0 + x1) / 2
            amp = (x1 - x0) * 0.3
            tri = np.abs(((yy[:, x0:x1] / 18.0) % 2) - 1) * 2 - 1  # -1..1 triangle wave along the rug
            mask = np.abs(xx[:, x0:x1] - (mid + tri * amp)) < 3
            img[:, x0:x1][mask] = other
    border = (yy < 26) | (yy > h - 27)
    img[border] = hexrgb(palette[0]) * 0.8
    weave = 0.92 + 0.08 * ((xx // 2 + yy // 2) % 2)[..., None]
    img *= weave * (0.95 + 0.05 * rng.random((h, w, 1)))
    return img

RUG_WARM = ["#3b2f2a", "#b5532c", "#e3c9a0", "#d99a3e", "#6f7d74", "#f2e8d8"]
RUG_COOL = ["#2e3440", "#e9e2d6", "#c46a3a", "#8a9a95", "#d8b56b"]

def poster(w=420, h=600):
    """A retro poster: sun rays behind a big ball, and TILT in the pixel font."""
    yy, xx = grid(h, w)
    cx, cy = w / 2, h * 0.42
    angle = np.arctan2(yy - cy, xx - cx)
    rays = (np.floor((angle + np.pi) / (np.pi / 12)) % 2) == 0
    img = np.where(rays[..., None], hexrgb("#f2b632"), hexrgb("#e8762c"))
    d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    shade = np.clip(1 - np.sqrt((xx - cx + 40) ** 2 + (yy - cy + 40) ** 2) / 200, 0, 1)
    ball = d < 120
    img[ball] = (0.3 + 0.7 * shade[ball])[:, None] * hexrgb("#eef1f5")
    img[(d >= 120) & (d < 128)] = hexrgb("#1f2a44")
    img[int(h * 0.75):] = hexrgb("#1f2a44")
    m = text_mask("TILT", 14)
    stamp(img, m, (w - m.shape[1]) // 2, int(h * 0.8), hexrgb("#f2e8d8"))
    img[:10] = img[-10:] = img[:, :10] = img[:, -10:] = hexrgb("#f2e8d8")
    return img

def wallpaper(w=640, h=360):
    """Desktop of the monitor: soft waves in blues."""
    yy, xx = grid(h, w)
    img = np.ones((h, w, 3)) * hexrgb("#16324f")
    for i, col in enumerate(("#1d4e7a", "#2a6f97", "#61a5c2", "#a9d6e5")):
        wave = h * (0.45 + 0.1 * i) + 30 * np.sin(xx / w * np.pi * 2 + i)
        img[yy > wave] = hexrgb(col)
    return img

def keyboard(w=860, h=280):
    """Top of the desk keyboard: white keys on brushed aluminium, a function row, the usual rows and a wide space bar."""
    yy, xx = grid(h, w)
    img = np.ones((h, w, 3)) * hexrgb("#b9bcc0")
    img *= (0.97 + 0.03 * np.sin(xx * 1.7 + yy * 0.05))[..., None]  # Brushed
    rows = [[1] * 14, [1] * 13 + [1.5], [1.5] + [1] * 12 + [1], [1.75] + [1] * 11 + [1.75], [2.25] + [1] * 10 + [2.25],
            [1, 1, 1, 1.25, 5.25, 1.25, 1, 1, 1]]
    unit = (w - 20) / 14.5
    row_unit = (h - 20 - 2 * len(rows)) / 5.55
    y = 10
    for r, row in enumerate(rows):
        kh = row_unit * (0.55 if r == 0 else 1.0)
        x = 10.0
        scale = 14.5 / sum(row)
        for k in row:
            kw = k * unit * scale
            x0, x1, y0, y1 = int(x + 2), int(x + kw - 2), int(y + 2), int(y + kh - 2)
            img[y0 + 2:y1 + 2, x0 + 1:x1 + 1] *= 0.8  # Shadow
            img[y0:y1, x0:x1] = hexrgb("#f3f3f1")
            img[y0:y0 + 3, x0:x1] = hexrgb("#ffffff")
            x += kw
        y += kh + 2
    return img

def code_screen(w=480, h=300):
    """Screen of the laptop: a dark editor with colored lines of code."""
    rng = np.random.default_rng(4)
    img = np.ones((h, w, 3)) * hexrgb("#1b2b22")
    img[:18] = hexrgb("#24392d")
    colors = ["#7fd28a", "#e5c07b", "#61afef", "#c678dd", "#d0d0d0"]
    for row in range(26):
        y = 26 + row * 10
        x = 12 + 16 * int(rng.integers(0, 4))
        for _ in range(int(rng.integers(1, 5))):
            length = int(rng.integers(20, 90))
            img[y:y + 5, x:min(w - 8, x + length)] = hexrgb(colors[rng.integers(len(colors))])
            x += length + 8
            if x > w - 40:
                break
    return img

def desktop(w=960, h=540):
    """Screen of the desk monitor: the wallpaper, an editor window with code, a light window with tiles and a dock."""
    img = wallpaper(w, h)
    def window(x, y, content, title):
        ch, cw = content.shape[:2]
        img[y + 6:y + 28 + ch + 6, x + 6:x + cw + 6] *= 0.6  # Shadow
        img[y:y + 22, x:x + cw] = hexrgb(title)
        for k, c in enumerate(("#ec6a5e", "#f4bf4f", "#61c554")):
            img[y + 7:y + 15, x + 8 + 14 * k:x + 16 + 14 * k] = hexrgb(c)
        img[y + 22:y + 22 + ch, x:x + cw] = content
    window(70, 60, code_screen(500, 320), "#24392d")
    tiles = np.ones((230, 300, 3)) * hexrgb("#f4f5f7")
    for k in range(6):
        tx, ty = 14 + (k % 3) * 96, 14 + (k // 3) * 106
        tiles[ty:ty + 90, tx:tx + 84] = hexrgb(("#e8762c", "#2a6f97", "#61a5c2", "#f2b632", "#7fd28a", "#c678dd")[k])
        tiles[ty + 70:ty + 90, tx:tx + 84] = hexrgb("#ffffff")
    window(600, 150, tiles, "#d9dce1")
    dock = (slice(h - 46, h - 10), slice(w // 2 - 170, w // 2 + 170))
    img[dock] = img[dock] * 0.5 + 0.5 * hexrgb("#e9eef3")
    for k in range(7):
        x0 = w // 2 - 160 + k * 46
        img[h - 42:h - 14, x0:x0 + 28] = hexrgb(("#e8762c", "#2a6f97", "#7fd28a", "#f2b632", "#c678dd", "#d0d0d0", "#61afef")[k])
    return img

def chair_mesh(w=512, h=512):
    """Back of the office chair: a fine dark grey woven mesh in a black frame (the edge of the UV square)."""
    yy, xx = grid(h, w)
    img = np.ones((h, w, 3)) * hexrgb("#2d3034")
    weave = ((xx + yy) % 6 < 2) | ((xx - yy) % 6 < 2)
    img[weave] = hexrgb("#43474c")
    border = (xx < 26) | (xx > w - 27) | (yy < 26) | (yy > h - 27)
    img[border] = hexrgb("#0b0b0c")
    return img
