"""The lobby's new look (18 Nov 2026, the user's design made with an image AI: Super-Characters/lobby-design-18Nov/
design.png, 1672 x 941). Pieces with fixed words or pictures are cut out of the design itself (masked to their round
or rounded shapes); the ones the game writes on (panels, plates, START, the top bar) are drawn here in its colours.

    lobby_art.py <out folder>        writes lb2_*.png there and prints each size (for include/menu/tbo_sizes.h)"""
import os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
DESIGN = os.path.join(HERE, "..", "lobby-design-18Nov", "design.png")
OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
D = Image.open(DESIGN).convert("RGBA")
SS = 4  # supersampling for drawn shapes


def save(name, im):
    im.save(os.path.join(OUT, name + ".png"))
    print("%-18s %4d x %4d" % (name, im.width, im.height))


def rounded_mask(w, h, r, feather=1.0):
    m = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w * SS - 1, h * SS - 1), r * SS, fill=255)
    m = m.resize((w, h), Image.LANCZOS)
    return m.filter(ImageFilter.GaussianBlur(feather)) if feather else m


def cut_round(box, r, feather=0.8):
    im = D.crop(box)
    im.putalpha(rounded_mask(im.width, im.height, r, feather))
    return im


def cut_circle(cx, cy, rad, feather=0.8):
    im = D.crop((cx - rad, cy - rad, cx + rad, cy + rad))
    m = Image.new("L", (rad * 2 * SS, rad * 2 * SS), 0)
    ImageDraw.Draw(m).ellipse((0, 0, rad * 2 * SS - 1, rad * 2 * SS - 1), fill=255)
    im.putalpha(m.resize((rad * 2, rad * 2), Image.LANCZOS).filter(ImageFilter.GaussianBlur(feather)))
    return im


def cut_light(box, lo=95, hi=150):
    """A small light icon on a dark panel: its light parts kept, the panel made clear."""
    im = D.crop(box)
    a = np.array(im).astype(float)
    lum = a[..., :3].max(2)
    a[..., 3] = np.clip((lum - lo) / (hi - lo), 0, 1) * 255
    return Image.fromarray(a.astype(np.uint8))


def cut_gold(box):
    """A gold thing over the sky (the crown): gold kept, the rest clear."""
    im = D.crop(box)
    a = np.array(im).astype(float)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    gold = np.clip(((r - b) - 70) / 40, 0, 1) * np.clip((g - 120) / 40, 0, 1)
    a[..., 3] = gold * 255
    return Image.fromarray(a.astype(np.uint8))


def glass(w, h, r, fill=(10, 22, 44, 205), edge=(96, 178, 238, 255), glow=(60, 160, 255, 90)):
    """The design's panel: dark blue glass, a thin light-blue rim, a faint glow inside the rim, a lighter top."""
    W, H = w * SS, h * SS
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((2 * SS, 2 * SS, W - 2 * SS - 1, H - 2 * SS - 1), r * SS, fill=fill)
    g = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(g).rounded_rectangle((4 * SS, 4 * SS, W - 4 * SS - 1, H - 4 * SS - 1), (r - 2) * SS, outline=glow, width=5 * SS)
    im = Image.alpha_composite(im, g.filter(ImageFilter.GaussianBlur(3 * SS)))
    top = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(top).rounded_rectangle((3 * SS, 3 * SS, W - 3 * SS - 1, int(H * 0.45)), r * SS, fill=(120, 170, 230, 26))
    im = Image.alpha_composite(im, top)
    ImageDraw.Draw(im).rounded_rectangle((2 * SS, 2 * SS, W - 2 * SS - 1, H - 2 * SS - 1), r * SS, outline=edge, width=int(1.6 * SS))
    return im.resize((w, h), Image.LANCZOS)


def pill(w, h, top, bottom, edge, r=None):
    W, H = w * SS, h * SS
    r = (h // 2) if r is None else r
    grad = Image.new("RGBA", (W, H))
    for y in range(H):
        t = y / (H - 1)
        grad.paste(tuple(int(top[i] * (1 - t) + bottom[i] * t) for i in range(4)), (0, y, W, y + 1))
    m = Image.new("L", (W, H), 0)
    ImageDraw.Draw(m).rounded_rectangle((SS, SS, W - SS - 1, H - SS - 1), r * SS, fill=255)
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    out.paste(grad, (0, 0), m)
    ImageDraw.Draw(out).rounded_rectangle((SS, SS, W - SS - 1, H - SS - 1), r * SS, outline=edge, width=2 * SS)
    return out.resize((w, h), Image.LANCZOS)


# --- panels and plates (drawn; the game writes on them)
save("lb2_glass", glass(128, 128, 16))                                            # 9-slice, corner 24
save("lb2_plate", glass(256, 80, 22, fill=(12, 26, 50, 225), edge=(110, 196, 250, 255)))
save("lb2_topbar", glass(256, 64, 10, fill=(14, 26, 70, 215), edge=(60, 110, 200, 200), glow=(40, 90, 200, 60)))
save("lb2_tile", glass(128, 128, 14, fill=(16, 30, 54, 225), edge=(120, 140, 170, 255), glow=(70, 120, 200, 50)))
save("lb2_btn_blue", pill(320, 72, (60, 150, 255, 255), (20, 70, 190, 255), (150, 210, 255, 255), r=14))
save("lb2_btn_green", pill(200, 60, (40, 190, 110, 255), (10, 110, 60, 255), (140, 240, 180, 255)))
save("lb2_btn_red", pill(200, 60, (230, 60, 60, 255), (140, 20, 30, 255), (255, 150, 150, 255)))
save("lb2_btn_dark", pill(200, 60, (40, 50, 70, 255), (20, 26, 40, 255), (110, 130, 160, 255)))
save("lb2_input", pill(320, 56, (20, 34, 60, 235), (14, 24, 44, 235), (80, 110, 160, 255), r=14))
save("lb2_tab_on", pill(160, 48, (255, 196, 60, 255), (200, 120, 20, 255), (255, 230, 150, 255), r=8))

# --- the rank badge on a plate (drawn: the game writes P1 / P2 in it)
W = 96 * SS
b = Image.new("RGBA", (W, W), (0, 0, 0, 0))
d = ImageDraw.Draw(b)
d.ellipse((2 * SS, 2 * SS, W - 2 * SS, W - 2 * SS), fill=(120, 70, 10, 255))
d.ellipse((7 * SS, 7 * SS, W - 7 * SS, W - 7 * SS), fill=(250, 196, 70, 255))
d.ellipse((13 * SS, 13 * SS, W - 13 * SS, W - 13 * SS), fill=(40, 26, 10, 255))
d.ellipse((16 * SS, 16 * SS, W - 16 * SS, W - 16 * SS), outline=(255, 220, 120, 255), width=2 * SS)
save("lb2_badge", b.resize((96, 96), Image.LANCZOS))

# --- START: the design's gold button with its words painted out (the game writes START, the countdown and the line)
st = D.crop((1281, 744, 1659, 900)).copy()
a = np.array(st).astype(float)
x0, x1 = 88, 301                       # the words lie between the two dragon balls (balls end at 64 and start at 313)
for y in range(a.shape[0]):
    L, R = a[y, 72:88, :3].mean(0), a[y, 301:310, :3].mean(0)
    for x in range(x0, x1):
        t = (x - x0) / (x1 - x0)
        a[y, x, :3] = L * (1 - t) + R * t
st = Image.fromarray(a.astype(np.uint8))
st = st.filter(ImageFilter.GaussianBlur(0.6))
mask = Image.new("L", (st.width * SS, st.height * SS), 0)
w_, h_ = st.width * SS, st.height * SS
poly = [(28 * SS, 8 * SS), (w_ - 28 * SS, 8 * SS), (w_ - 4 * SS, h_ // 2), (w_ - 28 * SS, h_ - 8 * SS), (28 * SS, h_ - 8 * SS), (4 * SS, h_ // 2)]
ImageDraw.Draw(mask).polygon(poly, fill=255)
st.putalpha(mask.resize(st.size, Image.LANCZOS).filter(ImageFilter.GaussianBlur(0.8)))
save("lb2_start", st)

# --- cut from the design: words and pictures that never change
save("lb2_logo", cut_circle(60, 51, 43))
for name, cx in (("mic", 85), ("spk", 175), ("inv", 266), ("rank", 356), ("chat", 447)):
    c = cut_circle(cx, 606, 36)
    save("lb2_r_" + name, c)
    g = np.array(c).astype(float)  # an "off" look: grey and darker
    lum = g[..., :3].mean(2, keepdims=True)
    g[..., :3] = lum * 0.55
    save("lb2_r_" + name + "_off", Image.fromarray(g.astype(np.uint8)))
for name, (x0, x1) in (("daily", (777, 891)), ("tour", (894, 1007)), ("history", (1011, 1124)), ("costume", (1128, 1242))):
    t = cut_round((x0, 760, x1, 889), 10)
    if name == "daily":  # (the design's red "!" over its corner: no daily challenges yet; the corner is made again
        a = np.array(t)      #  from the tile's own top-left corner, mirrored)
        a[0:34, a.shape[1] - 38:, :] = a[0:34, 0:38, :][:, ::-1, :]
        t = Image.fromarray(a)
    save("lb2_f_" + name, t)
save("lb2_ready", cut_round((530, 194, 697, 229), 16))
save("lb2_crown", cut_gold((585, 88, 645, 128)))
save("lb2_emblem", cut_circle(533, 46, 30, 1.2))
save("lb2_change", cut_round((1294, 527, 1651, 573), 12))
save("lb2_copy", cut_round((354, 59, 449, 96), 17))
save("lb2_leave", cut_round((1394, 21, 1511, 59), 12))
save("lb2_close", cut_round((1534, 21, 1651, 59), 12))
save("lb2_tabs", cut_round((26, 690, 306, 724), 6))
save("lb2_chatin", cut_round((29, 834, 458, 886), 16))
for name, box, *rest in (("i_eye", (42, 131, 78, 157)), ("i_rules", (1311, 338, 1338, 364)), ("i_friends", (1303, 589, 1340, 614)),
                  ("i_pin", (1307, 288, 1326, 308)), ("i_battle", (1314, 374, 1340, 398)), ("i_time", (1314, 401, 1340, 425)),
                  ("i_speed", (1314, 428, 1340, 452)), ("i_roll", (1314, 455, 1340, 479)), ("i_delay", (1314, 482, 1340, 506)),
                  ("i_globe", (937, 22, 977, 64), 140), ("i_bolt", (500, 713, 524, 737))):
    pass
for item in (("i_eye", (42, 131, 78, 157)), ("i_rules", (1311, 338, 1338, 364)), ("i_friends", (1303, 589, 1340, 614)),
             ("i_pin", (1307, 288, 1326, 308)), ("i_battle", (1314, 374, 1340, 398)), ("i_time", (1314, 401, 1340, 425)),
             ("i_speed", (1314, 428, 1340, 452)), ("i_roll", (1314, 455, 1340, 479)), ("i_delay", (1314, 482, 1340, 506)),
             ("i_globe", (937, 22, 977, 64), 150), ("i_bolt", (500, 713, 524, 737))):
    save("lb2_" + item[0], cut_light(item[1], *((item[2], item[2] + 50) if len(item) > 2 else ())))
