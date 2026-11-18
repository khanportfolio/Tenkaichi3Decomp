"""The main menu's new look (19 Nov 2026, the user's design made with an image AI:
Super-Characters/menu-design-19Nov/design.webp and its pieces on magenta, pieces.webp 1774 x 887).
Each piece is cut from the magenta sheet (the magenta made clear, the edges' magenta taken out of their colour); the
dragon balls are made 1 to 7 stars from the sheet's lit and dim ball (its 4 stars painted over, new stars drawn).

    menu_art.py <out folder>        writes mm2_*.png there and prints each size"""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
SHEET = os.path.join(HERE, "..", "menu-design-19Nov", "pieces.webp")
OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
A = np.asarray(Image.open(SHEET).convert("RGB")).astype(float)
MAG = np.array([255.0, 0.0, 255.0])


def save(name, im):
    im.save(os.path.join(OUT, name + ".png"))
    print("%-18s %4d x %4d" % (name, im.width, im.height))


def main_part(im):
    """Only the piece the box's middle belongs to (a neighbour's edge or flames caught in the box left out)."""
    from PIL import ImageDraw as D
    m = im.getchannel("A").point(lambda v: 255 if v > 40 else 0)
    D.floodfill(m, (m.width // 2, m.height // 2), 128)
    keep = m.point(lambda v: 255 if v == 128 else 0).filter(ImageFilter.MaxFilter(5))
    a = np.minimum(np.asarray(im.getchannel("A")), np.asarray(keep))
    out = im.copy()
    out.putalpha(Image.fromarray(a.astype(np.uint8)))
    bb = out.getchannel("A").point(lambda v: 255 if v > 10 else 0).getbbox()
    return out.crop(bb)


def cut(box, pad=4):
    x0, y0, x1, y1 = box
    x0, y0, x1, y1 = max(0, x0 - pad), max(0, y0 - pad), min(A.shape[1], x1 + pad), min(A.shape[0], y1 + pad)
    c = A[y0:y1, x0:x1]
    dist = np.abs(c[..., 0] - 255) + c[..., 1] + np.abs(c[..., 2] - 255)
    a = np.clip((dist - 70) / 150, 0, 1)
    rgb = (c - (1 - a)[..., None] * MAG) / np.maximum(a, 0.05)[..., None]  # the magenta taken out of the edges
    rgb = np.clip(rgb, 0, 255)
    edge = np.asarray(Image.fromarray(((a < 0.95) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(9))) > 0
    tint = np.minimum(rgb[..., 0], rgb[..., 2]) - rgb[..., 1]  # (magenta: red and blue over green)
    over = np.clip(tint - 18, 0, None) * edge
    rgb[..., 0] -= over
    rgb[..., 2] -= over
    a = np.where(edge & (tint > 70) & (rgb.max(2) < 140), 0, a)  # (dark purple left of the magenta behind)
    out = np.dstack([np.clip(rgb, 0, 255), a * 255]).astype(np.uint8)
    im = Image.fromarray(out, "RGBA")
    bb = im.getchannel("A").point(lambda v: 255 if v > 10 else 0).getbbox()
    return im.crop(bb) if bb else im


PIECES = {
    "title": (2, 78, 628, 286), "bar": (642, 114, 962, 190), "bar_sel": (622, 192, 968, 290),
    "up": (60, 318, 194, 404), "down": (284, 326, 418, 414), "desc": (6, 472, 534, 590), "frame": (536, 492, 776, 594),
    "i_shenron": (784, 446, 958, 626), "i_book": (30, 638, 208, 796), "i_swords": (270, 638, 440, 798),
    "i_globe": (508, 632, 684, 802), "i_vs": (744, 634, 954, 812), "i_wifi": (1004, 106, 1174, 244),
    "i_data": (1220, 104, 1330, 238), "i_gear": (1384, 100, 1540, 254), "k_enter": (1578, 118, 1756, 232),
    "k_esc": (1006, 338, 1168, 448), "k_x": (1214, 324, 1352, 462), "k_o": (1398, 324, 1536, 462), "cloud": (1570, 338, 1758, 458),
}
for name, box in PIECES.items():
    im = cut(box)
    if name == "bar":  # (the title's flames touch the blue bar on the sheet: its warm pixels are theirs)
        px = np.asarray(im).astype(int)
        warm = (px[..., 0] > px[..., 2] + 30) & (px[..., 0] > 110)
        px[..., 3] = np.where(warm, 0, px[..., 3])
        im = Image.fromarray(px.astype(np.uint8), "RGBA")
    if name in ("bar", "bar_sel", "frame", "desc"):
        im = main_part(im)
    if name == "title":  # (its flames' tail, cut at the box's right edge, fades out instead)
        px = np.asarray(im).astype(float)
        ramp = np.clip((im.width - 1 - np.arange(im.width)) / 70.0, 0, 1)
        px[..., 3] *= ramp[None, :]
        im = Image.fromarray(px.astype(np.uint8), "RGBA")
    save("mm2_" + name, im)

# --- the seven dragon balls, lit and dim: the sheet's stars painted over, 1..7 stars drawn
STARS = {1: [(0, 0)], 2: [(-0.22, 0), (0.22, 0)], 3: [(0, -0.2), (-0.21, 0.15), (0.21, 0.15)],
         4: [(-0.2, -0.18), (0.2, -0.18), (-0.2, 0.2), (0.2, 0.2)], 5: [(0, -0.25), (-0.24, -0.04), (0.24, -0.04), (-0.15, 0.24), (0.15, 0.24)],
         6: [(-0.2, -0.24), (0.2, -0.24), (-0.28, 0.02), (0.28, 0.02), (-0.13, 0.26), (0.13, 0.26)],
         7: [(0, 0), (0, -0.28), (-0.25, -0.13), (0.25, -0.13), (-0.25, 0.15), (0.25, 0.15), (0, 0.29)]}


def star(d, cx, cy, r, fill):
    pts = []
    for k in range(10):
        ang = -math.pi / 2 + k * math.pi / 5
        rr = r if k % 2 == 0 else r * 0.42
        pts.append((cx + rr * math.cos(ang), cy + rr * math.sin(ang)))
    d.polygon(pts, fill=fill)


for kind in ("lit", "dim"):
    R = 64 * 4
    W = R * 2 + 16
    for n in range(1, 8):
        b = Image.new("RGBA", (W, W), (0, 0, 0, 0))
        g = np.zeros((W, W, 4))
        yy, xx = np.mgrid[0:W, 0:W].astype(float)
        cx = cy = W / 2
        rr = np.hypot(xx - cx, yy - cy) / R
        lx, ly = (xx - (cx - R * 0.35)) / R, (yy - (cy - R * 0.4)) / R
        light = np.clip(1 - np.hypot(lx, ly) * 0.75, 0, 1)
        if kind == "lit":
            c0, c1 = np.array([255, 236, 120]), np.array([235, 110, 10])
        else:
            c0, c1 = np.array([190, 170, 140]), np.array([90, 70, 55])
        col = c1[None, None, :] * (1 - light[..., None]) + c0[None, None, :] * light[..., None]
        g[..., :3] = col
        g[..., 3] = np.clip((1 - rr) * R * 0.6, 0, 1) * 255
        ball = Image.fromarray(g.astype(np.uint8), "RGBA")
        d = ImageDraw.Draw(ball)
        d.ellipse((cx - R, cy - R, cx + R, cy + R), outline=(110, 50, 0, 255) if kind == "lit" else (60, 45, 35, 255), width=10)
        for sx, sy in STARS[n]:
            star(d, cx + sx * R * 1.85, cy + sy * R * 1.85, R * (0.21 if n < 6 else 0.18), (215, 30, 20, 255) if kind == "lit" else (125, 55, 45, 255))
        hl = Image.new("RGBA", (W, W), (0, 0, 0, 0))
        ImageDraw.Draw(hl).ellipse((cx - R * 0.62, cy - R * 0.78, cx - R * 0.05, cy - R * 0.38), fill=(255, 255, 255, 150 if kind == "lit" else 70))
        ball = Image.alpha_composite(ball, hl.filter(ImageFilter.GaussianBlur(R * 0.06)))
        save("mm2_ball_%s_%d" % (kind, n), ball.resize((W // 4, W // 4), Image.LANCZOS))
