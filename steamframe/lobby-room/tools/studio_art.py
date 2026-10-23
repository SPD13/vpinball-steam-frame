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
