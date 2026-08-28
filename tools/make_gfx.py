"""The game's art, generated.

Every image the GUI ships lives in gfx/ and is produced by this script, so
the look is versioned twice: as the PNGs the game reads, and as the code
that made them. Change a colour here, rerun, and the whole set stays of a
piece. Everything is drawn at 4x and downscaled, deterministic (fixed
seed), and in the palette the GUI's theme tokens quote.

    python3 tools/make_gfx.py        # writes gfx/*.png
"""

import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(__file__), "..", "gfx")
SS = 4  # Supersample factor.

# The palette - theme.hpp quotes these.
BG0 = (12, 10, 8)
PANEL = (25, 20, 16)
PANEL_HI = (38, 31, 25)
EDGE = (86, 76, 64)
EDGE_HI = (140, 124, 104)
INK = (235, 223, 206)
MUTED = (154, 138, 120)
EMBER = (255, 122, 46)
EMBER_DEEP = (196, 74, 24)
GOLD = (255, 214, 94)
STEEL = (143, 163, 184)

random.seed(20260828)


def canvas(w, h, color=(0, 0, 0, 0)):
    return Image.new("RGBA", (w * SS, h * SS), color)


def save(img, name, w, h):
    img = img.resize((w, h), Image.LANCZOS)
    os.makedirs(OUT, exist_ok=True)
    img.save(os.path.join(OUT, name + ".png"))
    print("gfx/%s.png  %dx%d" % (name, w, h))


def glow_layer(size, shapes, color, blur, alpha=255):
    """Shapes drawn into a mask, blurred wide, tinted: the soft light."""
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    shapes(draw)
    mask = mask.filter(ImageFilter.GaussianBlur(blur))
    layer = Image.new("RGBA", size, color + (0,))
    layer.putalpha(mask.point(lambda v: v * alpha // 255))
    return layer


def soften(img, radius=1.2):
    return img.filter(ImageFilter.GaussianBlur(radius))


def add_noise(img, strength=6, mono=True):
    px = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            n = random.randint(-strength, strength)
            r, g, b, a = px[x, y]
            px[x, y] = (max(0, min(255, r + n)), max(0, min(255, g + n)),
                        max(0, min(255, b + n)), a)
    return img


# --- The backdrop: a dark hall with the furnace somewhere below. ------------

def backdrop():
    w, h = 1600, 900
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        t = y / h
        # Near-black steel above, a breath of warmth toward the floor.
        r = BG0[0] + int(16 * t * t)
        g = BG0[1] + int(9 * t * t)
        b = BG0[2] + int(4 * t * t)
        for x in range(w):
            px[x, y] = (r, g, b, 255)
    # The furnace glows: three soft pools of ember light low in the frame.
    for cx, cy, rad, alpha in ((0.5, 1.06, 0.52, 66), (0.16, 1.02, 0.30, 40),
                               (0.86, 1.04, 0.34, 44)):
        img.alpha_composite(glow_layer(
            (w, h),
            lambda d, cx=cx, cy=cy, rad=rad: d.ellipse(
                (w * cx - w * rad, h * cy - w * rad * 0.6,
                 w * cx + w * rad, h * cy + w * rad * 0.6), fill=255),
            EMBER_DEEP, blur=90, alpha=alpha))
    # Columns: barely-there vertical slabs, the hall's architecture.
    slab = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    sd = ImageDraw.Draw(slab)
    for cx in (0.08, 0.3, 0.7, 0.92):
        x = int(w * cx)
        sd.rectangle((x - 34, 0, x + 34, h), fill=(0, 0, 0, 40))
    img.alpha_composite(slab.filter(ImageFilter.GaussianBlur(24)))
    # Vignette.
    img.alpha_composite(glow_layer(
        (w, h), lambda d: d.rectangle((0, 0, w, h), fill=255),
        (0, 0, 0), blur=0, alpha=0))
    vig = Image.new("L", (w, h), 0)
    vd = ImageDraw.Draw(vig)
    vd.ellipse((-w * 0.25, -h * 0.35, w * 1.25, h * 1.2), fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(120)).point(lambda v: 255 - v)
    dark = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    dark.putalpha(vig.point(lambda v: v * 150 // 255))
    img.alpha_composite(dark)
    add_noise(img, 3)
    os.makedirs(OUT, exist_ok=True)
    img.save(os.path.join(OUT, "backdrop.png"))
    print("gfx/backdrop.png  %dx%d" % (w, h))


# --- Nine-slice frames. -----------------------------------------------------

def frame(name, size, border, rivets):
    """A dark metal frame around a darker fill. The border strips are kept
    uniform along their length so the nine-slice can stretch them, and the
    rivets live in the corners, which never stretch."""
    s = size * SS
    b = border * SS
    img = canvas(size, size)
    d = ImageDraw.Draw(img)
    # The plate itself.
    d.rounded_rectangle((0, 0, s - 1, s - 1), radius=b // 2, fill=PANEL)
    # Bevel: light along the top and left, shadow along the bottom.
    for i in range(b // 3):
        t = 1 - i / (b // 3)
        d.rounded_rectangle((i, i, s - 1 - i, s - 1 - i), radius=b // 2,
            outline=(EDGE[0] + int((EDGE_HI[0] - EDGE[0]) * t * 0.5),
                     EDGE[1] + int((EDGE_HI[1] - EDGE[1]) * t * 0.5),
                     EDGE[2] + int((EDGE_HI[2] - EDGE[2]) * t * 0.5),
                     max(0, 190 - i * 3)))
    # The inner well, a touch darker than the frame.
    inner = b - SS * 3
    d.rounded_rectangle((inner, inner, s - 1 - inner, s - 1 - inner),
        radius=SS * 3, fill=(PANEL[0] - 6, PANEL[1] - 5, PANEL[2] - 4, 255))
    d.rounded_rectangle((inner, inner, s - 1 - inner, s - 1 - inner),
        radius=SS * 3, outline=(0, 0, 0, 120), width=SS)
    if rivets:
        r = SS * 4
        for cx, cy in ((b // 2, b // 2), (s - b // 2, b // 2),
                       (b // 2, s - b // 2), (s - b // 2, s - b // 2)):
            d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=EDGE_HI)
            d.ellipse((cx - r, cy - r, cx + r, cy + r),
                outline=(0, 0, 0, 160), width=SS)
            d.ellipse((cx - r // 2, cy - r, cx + r // 3, cy - r // 3),
                fill=(255, 244, 224, 90))
    save(soften(img, 1.0), name, size, size)


# --- Icons: warm ivory line-work with an ember breath. ----------------------

def icon(name, size, paint, glow=True, ink=INK):
    img = canvas(size, size)
    d = ImageDraw.Draw(img)
    paint(d, size * SS)
    img = soften(img, 1.4)
    if glow:
        halo = img.split()[3].filter(ImageFilter.GaussianBlur(SS * 3))
        back = Image.new("RGBA", img.size, EMBER + (0,))
        back.putalpha(halo.point(lambda v: v * 70 // 255))
        out = Image.new("RGBA", img.size, (0, 0, 0, 0))
        out.alpha_composite(back)
        out.alpha_composite(img)
        img = out
    save(img, name, size, size)
    return ink


def stroke(d, points, width, ink=INK):
    d.line(points, fill=ink, width=width, joint="curve")


def hammer(d, s, angle):
    """One war hammer, drawn rotated about the centre."""
    cx = cy = s / 2
    ca, sa = math.cos(angle), math.sin(angle)

    def rot(x, y):
        x, y = x - cx, y - cy
        return (cx + x * ca - y * sa, cy + x * sa + y * ca)

    w = s * 0.055
    stroke(d, [rot(cx, s * 0.18), rot(cx, s * 0.84)], int(w))
    head = [rot(cx - s * 0.16, s * 0.18), rot(cx + s * 0.16, s * 0.18),
            rot(cx + s * 0.13, s * 0.34), rot(cx - s * 0.13, s * 0.34)]
    d.polygon(head, fill=INK)


def ic_battle(d, s):
    hammer(d, s, math.radians(38))
    hammer(d, s, math.radians(-38))


def anvil(d, s, scale=1.0, dy=0.0):
    u = s * scale

    def p(x, y):
        return (s / 2 + (x - 0.5) * u, s * (0.52 + dy) + (y - 0.5) * u)

    body = [p(0.08, 0.36), p(0.92, 0.36), p(0.86, 0.5), p(0.62, 0.52),
            p(0.66, 0.72), p(0.34, 0.72), p(0.38, 0.52), p(0.2, 0.5),
            p(0.02, 0.44)]
    d.polygon(body, fill=INK)
    d.polygon([p(0.26, 0.72), p(0.74, 0.72), p(0.8, 0.82), p(0.2, 0.82)],
        fill=INK)


def flame(d, s, cx, cy, scale, ink=EMBER):
    u = s * scale
    pts = []
    for i in range(24):
        a = i / 24 * 2 * math.pi
        r = u * (0.5 + 0.16 * math.sin(a * 3 + 1.2))
        wob = 1.0 - 0.55 * max(0.0, math.cos(a))  # Lean the tip upward.
        pts.append((cx + r * math.sin(a) * 0.62,
                    cy - r * math.cos(a) * wob * 0.9 + u * 0.1))
    d.polygon(pts, fill=ink)
    d.ellipse((cx - u * 0.16, cy - u * 0.05, cx + u * 0.16, cy + u * 0.3),
        fill=GOLD)


def ic_boss(d, s):
    anvil(d, s, 0.9, 0.08)
    # The crown above the horn.
    base = s * 0.30
    d.polygon([(s * 0.3, base), (s * 0.7, base), (s * 0.66, base - s * 0.05),
               (s * 0.6, base - s * 0.17), (s * 0.5, base - s * 0.05),
               (s * 0.4, base - s * 0.17), (s * 0.34, base - s * 0.05)],
        fill=GOLD)


def ic_mini(d, s):
    # The miniboss: the boss's anvil, uncrowned, with a single blade
    # struck across it - a duel, but not yet the master's.
    anvil(d, s, 0.8, 0.1)
    stroke(d, [(s * 0.24, s * 0.14), (s * 0.76, s * 0.6)], int(s * 0.055))
    stroke(d, [(s * 0.64, s * 0.5), (s * 0.58, s * 0.7)], int(s * 0.05))
    stroke(d, [(s * 0.84, s * 0.54), (s * 0.68, s * 0.76)], int(s * 0.05))


def ic_forge(d, s):
    anvil(d, s, 0.86, 0.14)
    flame(d, s, s * 0.5, s * 0.26, 0.3)


def ic_event(d, s):
    # A rune-stone diamond with a question cut into it.
    d.polygon([(s * 0.5, s * 0.06), (s * 0.94, s * 0.5), (s * 0.5, s * 0.94),
               (s * 0.06, s * 0.5)], outline=INK, width=int(s * 0.05))
    w = int(s * 0.075)
    stroke(d, [(s * 0.38, s * 0.4), (s * 0.4, s * 0.32), (s * 0.5, s * 0.28),
               (s * 0.6, s * 0.32), (s * 0.62, s * 0.42), (s * 0.5, s * 0.5),
               (s * 0.5, s * 0.58)], w)
    d.ellipse((s * 0.46, s * 0.66, s * 0.54, s * 0.74), fill=INK)


def ic_lock(d, s):
    d.rounded_rectangle((s * 0.26, s * 0.44, s * 0.74, s * 0.84),
        radius=s * 0.06, fill=MUTED)
    stroke(d, [(s * 0.35, s * 0.46), (s * 0.35, s * 0.32), (s * 0.42, s * 0.2),
               (s * 0.58, s * 0.2), (s * 0.65, s * 0.32), (s * 0.65, s * 0.46)],
        int(s * 0.07), ink=MUTED)
    d.ellipse((s * 0.46, s * 0.56, s * 0.54, s * 0.66), fill=PANEL)


def ic_ember(d, s):
    d.ellipse((s * 0.12, s * 0.12, s * 0.88, s * 0.88), fill=EMBER_DEEP)
    d.ellipse((s * 0.2, s * 0.2, s * 0.8, s * 0.8), fill=EMBER)
    d.ellipse((s * 0.32, s * 0.28, s * 0.62, s * 0.58), fill=GOLD)


def ic_slag(d, s):
    d.polygon([(s * 0.14, s * 0.62), (s * 0.28, s * 0.36), (s * 0.86, s * 0.36),
               (s * 0.72, s * 0.62)], fill=STEEL)
    d.polygon([(s * 0.14, s * 0.62), (s * 0.72, s * 0.62), (s * 0.72, s * 0.74),
               (s * 0.14, s * 0.74)],
        fill=(STEEL[0] - 40, STEEL[1] - 40, STEEL[2] - 36))
    d.polygon([(s * 0.72, s * 0.62), (s * 0.86, s * 0.36), (s * 0.86, s * 0.5),
               (s * 0.72, s * 0.74)],
        fill=(STEEL[0] - 60, STEEL[1] - 58, STEEL[2] - 50))


def ic_star(d, s):
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        r = s * (0.42 if i % 2 == 0 else 0.18)
        pts.append((s / 2 + r * math.cos(a), s / 2 + r * math.sin(a)))
    d.polygon(pts, fill=GOLD)


# --- Mode and menu emblems. -------------------------------------------------

def em_map(d, s):
    dots = {"a": (0.5, 0.86), "b": (0.28, 0.6), "c": (0.72, 0.6),
            "d": (0.38, 0.36), "e": (0.66, 0.34), "f": (0.5, 0.12)}
    for a, b in (("a", "b"), ("a", "c"), ("b", "d"), ("c", "d"), ("c", "e"),
                 ("d", "f"), ("e", "f")):
        stroke(d, [(dots[a][0] * s, dots[a][1] * s),
                   (dots[b][0] * s, dots[b][1] * s)], int(s * 0.035),
            ink=MUTED)
    for key, (x, y) in dots.items():
        r = s * (0.085 if key != "f" else 0.11)
        d.ellipse((x * s - r, y * s - r, x * s + r, y * s + r),
            fill=GOLD if key == "f" else INK)


def em_free(d, s):
    flame(d, s, s * 0.5, s * 0.5, 0.72)


def em_blaze(d, s):
    w = int(s * 0.05)
    stroke(d, [(s * 0.24, s * 0.14), (s * 0.76, s * 0.14)], w)
    stroke(d, [(s * 0.24, s * 0.9), (s * 0.76, s * 0.9)], w)
    stroke(d, [(s * 0.28, s * 0.16), (s * 0.62, s * 0.52), (s * 0.28, s * 0.88)],
        w)
    stroke(d, [(s * 0.72, s * 0.16), (s * 0.38, s * 0.52), (s * 0.72, s * 0.88)],
        w)
    flame(d, s, s * 0.5, s * 0.74, 0.2)


def em_inferno(d, s):
    for i, height in enumerate((0.3, 0.52, 0.4, 0.66)):
        x = s * (0.16 + i * 0.19)
        d.rectangle((x, s * (0.9 - height * 0.6), x + s * 0.12, s * 0.9),
            fill=EMBER_DEEP if i % 2 == 0 else EMBER)
    flame(d, s, s * 0.66, s * 0.3, 0.3)


def em_cheese(d, s):
    d.rectangle((s * 0.14, s * 0.3, s * 0.86, s * 0.86), fill=MUTED)
    for y in (0.3, 0.49, 0.68):
        stroke(d, [(s * 0.14, s * y), (s * 0.86, s * y)], int(s * 0.025),
            ink=PANEL)
    for x, y in ((0.3, 0.395), (0.62, 0.395), (0.44, 0.585), (0.74, 0.585),
                 (0.26, 0.77)):
        d.ellipse((s * x, s * y, s * (x + 0.1), s * (y + 0.09)), fill=BG0)


def em_duel(d, s):
    for sign in (1, -1):
        cx = s / 2

        def q(x, y):
            return (cx + sign * (x - 0.5) * s, y * s)

        stroke(d, [q(0.2, 0.16), q(0.78, 0.74)], int(s * 0.05))
        stroke(d, [q(0.66, 0.62), q(0.62, 0.82)], int(s * 0.045))
        stroke(d, [q(0.86, 0.66), q(0.7, 0.9)], int(s * 0.045))


def em_temper(d, s):
    d.rounded_rectangle((s * 0.24, s * 0.1, s * 0.76, s * 0.9),
        radius=s * 0.06, outline=INK, width=int(s * 0.045))
    flame(d, s, s * 0.5, s * 0.44, 0.26)


def small(fn):
    def paint(d, s):
        fn(d, s)
    return paint


def ic_replay(d, s):
    d.ellipse((s * 0.14, s * 0.14, s * 0.86, s * 0.86), outline=INK,
        width=int(s * 0.06))
    d.polygon([(s * 0.42, s * 0.32), (s * 0.42, s * 0.68), (s * 0.72, s * 0.5)],
        fill=INK)


def ic_profile(d, s):
    for i, height in enumerate((0.28, 0.46, 0.38, 0.62)):
        x = s * (0.16 + i * 0.19)
        d.rectangle((x, s * (0.84 - height), x + s * 0.12, s * 0.84), fill=INK)
    stroke(d, [(s * 0.14, s * 0.9), (s * 0.86, s * 0.9)], int(s * 0.04))


def ic_scores(d, s):
    d.polygon([(s * 0.3, s * 0.14), (s * 0.7, s * 0.14), (s * 0.66, s * 0.5),
               (s * 0.5, s * 0.62), (s * 0.34, s * 0.5)], fill=GOLD)
    stroke(d, [(s * 0.3, s * 0.2), (s * 0.16, s * 0.24), (s * 0.22, s * 0.42),
               (s * 0.35, s * 0.48)], int(s * 0.035), ink=GOLD)
    stroke(d, [(s * 0.7, s * 0.2), (s * 0.84, s * 0.24), (s * 0.78, s * 0.42),
               (s * 0.65, s * 0.48)], int(s * 0.035), ink=GOLD)
    d.rectangle((s * 0.44, s * 0.6, s * 0.56, s * 0.76), fill=GOLD)
    d.rectangle((s * 0.34, s * 0.76, s * 0.66, s * 0.86), fill=GOLD)


def ic_help(d, s):
    d.polygon([(s * 0.5, s * 0.14), (s * 0.86, s * 0.22), (s * 0.86, s * 0.82),
               (s * 0.5, s * 0.74), (s * 0.14, s * 0.82), (s * 0.14, s * 0.22)],
        outline=INK, width=int(s * 0.05))
    stroke(d, [(s * 0.5, s * 0.16), (s * 0.5, s * 0.72)], int(s * 0.04))


def ic_settings(d, s):
    cx = cy = s / 2
    for i in range(8):
        a = i * math.pi / 4
        stroke(d, [(cx + s * 0.26 * math.cos(a), cy + s * 0.26 * math.sin(a)),
                   (cx + s * 0.4 * math.cos(a), cy + s * 0.4 * math.sin(a))],
            int(s * 0.09))
    d.ellipse((cx - s * 0.26, cy - s * 0.26, cx + s * 0.26, cy + s * 0.26),
        fill=INK)
    d.ellipse((cx - s * 0.11, cy - s * 0.11, cx + s * 0.11, cy + s * 0.11),
        fill=PANEL)


def grain():
    size = 128
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = img.load()
    for y in range(size):
        for x in range(size):
            v = random.randint(0, 255)
            px[x, y] = (v, v, v, 14 if v > 128 else 10)
    img.save(os.path.join(OUT, "grain.png"))
    print("gfx/grain.png  %dx%d" % (size, size))


def main():
    backdrop()
    frame("panel", 96, 24, rivets=True)
    frame("plate", 48, 14, rivets=False)
    icon("node_battle", 64, ic_battle)
    icon("node_boss", 64, ic_boss)
    icon("node_forge", 64, ic_forge)
    icon("node_event", 64, ic_event)
    icon("node_mini", 64, ic_mini)
    icon("lock", 64, ic_lock, glow=False)
    icon("ember", 28, ic_ember)
    icon("slag", 28, ic_slag, glow=False)
    icon("star", 28, ic_star, glow=False)
    icon("em_map", 96, em_map)
    icon("em_free", 96, em_free)
    icon("em_blaze", 96, em_blaze)
    icon("em_inferno", 96, em_inferno)
    icon("em_cheese", 96, em_cheese, glow=False)
    icon("em_duel", 96, em_duel)
    icon("em_temper", 96, em_temper)
    icon("ic_replay", 64, ic_replay, glow=False)
    icon("ic_profile", 64, ic_profile, glow=False)
    icon("ic_scores", 64, ic_scores, glow=False)
    icon("ic_help", 64, ic_help, glow=False)
    icon("ic_settings", 64, ic_settings, glow=False)
    grain()


if __name__ == "__main__":
    main()
