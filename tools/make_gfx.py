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

# The palette. cpp/gui/palette.hpp holds the same numbers on the C++ side -
# these are baked into PNGs, those are drawn live, and the two halves are a
# pair: change one and change the other, then rerun this script.
BG0 = (12, 10, 8)
PANEL = (25, 20, 16)
PANEL_HI = (38, 31, 25)
EDGE = (86, 76, 64)
EDGE_HI = (140, 124, 104)
INK = (244, 237, 228)
MUTED = (157, 140, 120)
EMBER = (255, 122, 46)
EMBER_HOT = (255, 176, 60)
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
    # A painter may want the image itself - the shaded helpers composite a
    # gradient through a mask and cannot work from a draw context alone.
    try:
        paint(d, size * SS, img)
    except TypeError:
        paint(d, size * SS)
    img = soften(img, 1.1)
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


# --- Material: the one thing the flat painters never had. -------------------
# A shape drawn as a single flat fill has no weight, which is why the old
# icons all read as the same grey blob once SDL had scaled them down to the
# twenty-four pixels they are actually rendered at. These give a shape a lit
# crown, a cooling foot and a keyline - the same light the blocks in the well
# are lit by, so the map and the board look like one game.

def _ramp(size, top, bottom):
    """A vertical two-stop gradient the size of the canvas."""
    w, h = size
    ramp = Image.new("RGBA", size)
    px = ramp.load()
    for y in range(h):
        t = y / max(1, h - 1)
        row = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = row + (255,)
    return ramp


def shaded_poly(img, pts, top, bottom, keyline=None, width=None):
    """A polygon poured full of a gradient, with an optional dark keyline.

    The keyline is what lets an icon sit on a dark plate without dissolving
    into it - the old set relied on a 27%-alpha halo and nine of its icons
    switched even that off.
    """
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).polygon(pts, fill=255)
    ramp = _ramp(img.size, top, bottom)
    ramp.putalpha(mask)
    img.alpha_composite(ramp)
    if keyline is not None:
        ImageDraw.Draw(img).polygon(
            pts, outline=keyline, width=width or SS * 2)


def shaded_ellipse(img, box, top, bottom, keyline=None, width=None):
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).ellipse(box, fill=255)
    ramp = _ramp(img.size, top, bottom)
    ramp.putalpha(mask)
    img.alpha_composite(ramp)
    if keyline is not None:
        ImageDraw.Draw(img).ellipse(
            box, outline=keyline, width=width or SS * 2)


# The two ends of every metal shape, and the keyline under all of them.
IRON_HI = (214, 200, 180)
IRON_LO = (120, 106, 92)
GOLD_HI = (255, 232, 150)
GOLD_LO = (188, 138, 30)
FIRE_HI = (255, 214, 120)
FIRE_LO = (198, 66, 18)
KEY = (10, 7, 5, 220)


def hammer(d, s, angle):
    """One hammer, drawn heavy. Two thin sticks crossed never read at all."""
    cx, cy = s * 0.5, s * 0.52
    cos, sin = math.cos(angle), math.sin(angle)

    def rot(x, y):
        dx, dy = x - cx, y - cy
        return (cx + dx * cos - dy * sin, cy + dx * sin + dy * cos)

    return rot


def ic_maul(d, s, img=None):
    """The maul itself, for the blow that opens a run.

    Not the crossed pair the battle node wears: that one is a badge and
    cannot be swung. Drawn upright in the classic T - a head across the top
    of a haft - because that silhouette is what says "hammer" at any size,
    and the animation turns the whole sprite anyway.

    It took three tries. A wide thin head read as a signpost; the same head
    moved to the foot read as a pedestal. What fixes it is the asymmetry a
    real one has: a flat pale striking face at one end and a tapered peen
    at the other, so the bar across the haft is a tool and not a crossbeam.
    """
    cx = s * 0.5
    haft = [(cx - s * 0.05, s * 0.27), (cx + s * 0.05, s * 0.27),
            (cx + s * 0.042, s * 0.97), (cx - s * 0.042, s * 0.97)]
    head = [(cx - s * 0.37, s * 0.05), (cx + s * 0.20, s * 0.05),
            (cx + s * 0.33, s * 0.13), (cx + s * 0.33, s * 0.24),
            (cx + s * 0.20, s * 0.32), (cx - s * 0.37, s * 0.32)]
    face = [(cx - s * 0.37, s * 0.05), (cx - s * 0.29, s * 0.05),
            (cx - s * 0.29, s * 0.32), (cx - s * 0.37, s * 0.32)]
    # The wedge that holds the haft in the eye.
    wedge = [(cx - s * 0.075, s * 0.28), (cx + s * 0.075, s * 0.28),
             (cx + s * 0.062, s * 0.38), (cx - s * 0.062, s * 0.38)]
    if img is not None:
        shaded_poly(img, haft, (156, 132, 104), (88, 70, 52), KEY)
        shaded_poly(img, wedge, (196, 170, 138), (120, 100, 78), KEY)
        shaded_poly(img, head, IRON_HI, IRON_LO, KEY)
        shaded_poly(img, face, (224, 210, 192), (160, 146, 130), KEY)
    else:
        d.polygon(haft, fill=INK)
        d.polygon(head, fill=INK)


def ic_battle(d, s, img=None):
    """Crossed hammers - but with a haft you can see and a head with mass."""
    for angle in (math.radians(48), math.radians(-48)):
        rot = hammer(d, s, angle)
        cx = s * 0.5
        haft = [rot(cx - s * 0.05, s * 0.34), rot(cx + s * 0.05, s * 0.34),
                rot(cx + s * 0.05, s * 0.94), rot(cx - s * 0.05, s * 0.94)]
        head = [rot(cx - s * 0.26, s * 0.08), rot(cx + s * 0.26, s * 0.08),
                rot(cx + s * 0.21, s * 0.36), rot(cx - s * 0.21, s * 0.36)]
        if img is not None:
            shaded_poly(img, haft, (150, 128, 102), (86, 70, 54), KEY)
            shaded_poly(img, head, IRON_HI, IRON_LO, KEY)
        else:
            d.polygon(haft, fill=INK)
            d.polygon(head, fill=INK)


def ic_boss(d, s, img=None):
    """A crown. Nothing else on the map is a crown, which is the point."""
    def p(x, y):
        return (s * x, s * y)

    band = [p(0.14, 0.62), p(0.86, 0.62), p(0.86, 0.82), p(0.14, 0.82)]
    spikes = [p(0.14, 0.62), p(0.20, 0.22), p(0.32, 0.48), p(0.50, 0.12),
              p(0.68, 0.48), p(0.80, 0.22), p(0.86, 0.62)]
    if img is not None:
        shaded_poly(img, spikes, GOLD_HI, GOLD_LO, KEY)
        shaded_poly(img, band, GOLD_HI, GOLD_LO, KEY)
        # Three set stones, so the band is not a bare bar.
        for x in (0.30, 0.50, 0.70):
            shaded_ellipse(img, (s * (x - 0.05), s * 0.66,
                s * (x + 0.05), s * 0.78), (255, 250, 230), EMBER_DEEP)
    else:
        d.polygon(spikes, fill=GOLD)
        d.polygon(band, fill=GOLD)


def ic_mini(d, s, img=None):
    """A blade, point down - the warden barring the short way up."""
    def p(x, y):
        return (s * x, s * y)

    blade = [p(0.50, 0.94), p(0.33, 0.46), p(0.38, 0.30), p(0.62, 0.30),
             p(0.67, 0.46)]
    guard = [p(0.22, 0.24), p(0.78, 0.24), p(0.78, 0.34), p(0.22, 0.34)]
    grip = [p(0.44, 0.06), p(0.56, 0.06), p(0.56, 0.24), p(0.44, 0.24)]
    if img is not None:
        shaded_poly(img, blade, (232, 224, 212), (128, 138, 150), KEY)
        shaded_poly(img, guard, GOLD_HI, GOLD_LO, KEY)
        shaded_poly(img, grip, (120, 84, 56), (72, 48, 32), KEY)
    else:
        d.polygon(blade, fill=INK)
        d.polygon(guard, fill=GOLD)
        d.polygon(grip, fill=MUTED)


def anvil(d, s, scale=1.0, dy=0.0, img=None):
    """The anvil, with a horn - and now only the forge stop wears it."""
    def p(x, y):
        return (s * (0.5 + (x - 0.5) * scale), s * (y * scale + dy))

    body = [p(0.06, 0.40), p(0.30, 0.34), p(0.94, 0.34), p(0.88, 0.50),
            p(0.62, 0.53), p(0.66, 0.72), p(0.34, 0.72), p(0.38, 0.53),
            p(0.16, 0.50)]
    base = [p(0.24, 0.72), p(0.76, 0.72), p(0.84, 0.86), p(0.16, 0.86)]
    if img is not None:
        shaded_poly(img, body, IRON_HI, IRON_LO, KEY)
        shaded_poly(img, base, (150, 134, 116), (74, 62, 50), KEY)
    else:
        d.polygon(body, fill=INK)
        d.polygon(base, fill=INK)


def flame(d, s, cx, cy, scale, ink=EMBER, img=None):
    """A hand-cut teardrop, not a wobbled circle.

    Every parametric flame I tried came out a cloud: a radius that varies
    smoothly around a centre has no tip and no direction. So the outline is
    written down instead - a point at the top, the body swelling low, and one
    lick curling off the left so the base is not a bowl. Read at 24px, that
    asymmetry is the whole tell.
    """
    u = s * scale
    outline = [(0.00, -1.00), (0.14, -0.60), (0.28, -0.18), (0.40, 0.26),
        (0.34, 0.66), (0.14, 0.88), (-0.10, 0.88), (-0.30, 0.70),
        (-0.40, 0.34), (-0.21, 0.08), (-0.30, -0.22), (-0.22, -0.58),
        (-0.10, -0.82)]
    pts = [(cx + x * u * 1.1, cy + y * u * 0.95) for x, y in outline]
    core = (cx - u * 0.15, cy - u * 0.02, cx + u * 0.16, cy + u * 0.52)
    if img is not None:
        shaded_poly(img, pts, FIRE_HI, FIRE_LO)
        shaded_ellipse(img, core, (255, 255, 236), (255, 206, 96))
    else:
        d.polygon(pts, fill=ink)
        d.ellipse(core, fill=GOLD)


def ic_forge(d, s, img=None):
    anvil(d, s, 0.94, 0.20, img)
    flame(d, s, s * 0.5, s * 0.30, 0.34, img=img)


def ic_event(d, s, img=None):
    """A question, cut as a shape rather than traced as a hairline."""
    def p(x, y):
        return (s * x, s * y)

    diamond = [p(0.5, 0.04), p(0.96, 0.5), p(0.5, 0.96), p(0.04, 0.5)]
    inner = [p(0.5, 0.16), p(0.84, 0.5), p(0.5, 0.84), p(0.16, 0.5)]
    hook = [p(0.36, 0.34), p(0.44, 0.28), p(0.58, 0.28), p(0.66, 0.36),
            p(0.66, 0.46), p(0.56, 0.54), p(0.54, 0.62), p(0.46, 0.62),
            p(0.46, 0.50), p(0.57, 0.43), p(0.57, 0.37), p(0.50, 0.35),
            p(0.44, 0.40), p(0.36, 0.40)]
    if img is not None:
        shaded_poly(img, diamond, (96, 82, 66), (52, 42, 34), KEY)
        shaded_poly(img, inner, (34, 27, 21), (22, 17, 13))
        shaded_poly(img, hook, GOLD_HI, GOLD_LO, KEY, SS)
        shaded_ellipse(img, (s * 0.455, s * 0.68, s * 0.545, s * 0.77),
            GOLD_HI, GOLD_LO, KEY, SS)
    else:
        d.polygon(diamond, outline=INK, width=int(s * 0.05))
        d.polygon(hook, fill=GOLD)


def spin(pts, cx, cy, angle):
    """Rotate a point list about a centre - cheaper than a second painter."""
    cos, sin = math.cos(angle), math.sin(angle)
    return [(cx + (x - cx) * cos - (y - cy) * sin,
        cy + (x - cx) * sin + (y - cy) * cos) for x, y in pts]


def ic_ember(d, s, img=None):
    """A live coal. The old one was three concentric circles; this one has a
    hot floor and a cooling crown, which is what a coal actually looks like."""
    if img is not None:
        shaded_ellipse(img, (s * 0.10, s * 0.10, s * 0.90, s * 0.90),
            (255, 176, 72), (150, 40, 10))
        shaded_ellipse(img, (s * 0.24, s * 0.26, s * 0.76, s * 0.80),
            (255, 236, 168), (255, 118, 34))
        shaded_ellipse(img, (s * 0.36, s * 0.34, s * 0.62, s * 0.56),
            (255, 255, 244), (255, 214, 120))
    else:
        d.ellipse((s * 0.10, s * 0.10, s * 0.90, s * 0.90), fill=EMBER_DEEP)
        d.ellipse((s * 0.24, s * 0.26, s * 0.76, s * 0.80), fill=EMBER)
        d.ellipse((s * 0.36, s * 0.34, s * 0.62, s * 0.56), fill=GOLD)


def ic_slag(d, s, img=None):
    """A cast ingot: a lit top face, a front wall and a shaded end. Slag buys
    the permanent things, so it should look like a bar you could pick up."""
    def p(x, y):
        return (s * x, s * y)

    top = [p(0.16, 0.44), p(0.34, 0.28), p(0.90, 0.28), p(0.72, 0.44)]
    face = [p(0.16, 0.44), p(0.72, 0.44), p(0.72, 0.76), p(0.16, 0.76)]
    side = [p(0.72, 0.44), p(0.90, 0.28), p(0.90, 0.60), p(0.72, 0.76)]
    if img is not None:
        shaded_poly(img, top, (196, 210, 226), (140, 158, 178))
        shaded_poly(img, face, (132, 150, 170), (74, 86, 102))
        shaded_poly(img, side, (96, 110, 128), (52, 60, 72))
    else:
        d.polygon(top, fill=STEEL)
        d.polygon(face, fill=(STEEL[0] - 40, STEEL[1] - 40, STEEL[2] - 36))
        d.polygon(side, fill=(STEEL[0] - 60, STEEL[1] - 58, STEEL[2] - 50))


def star_points(s, cx, cy, outer, inner):
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        r = s * (outer if i % 2 == 0 else inner)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def ic_star(d, s, img=None):
    pts = star_points(s, s / 2, s / 2, 0.44, 0.19)
    if img is not None:
        shaded_poly(img, pts, (255, 246, 198), (206, 148, 24))
    else:
        d.polygon(pts, fill=GOLD)


# --- Mode and menu emblems. -------------------------------------------------

def em_map(d, s, img=None):
    """The road as a chain of forges, the summit picked out in gold."""
    dots = {"a": (0.5, 0.86), "b": (0.28, 0.6), "c": (0.72, 0.6),
            "d": (0.38, 0.36), "e": (0.66, 0.34), "f": (0.5, 0.12)}
    for a, b in (("a", "b"), ("a", "c"), ("b", "d"), ("c", "d"), ("c", "e"),
                 ("d", "f"), ("e", "f")):
        stroke(d, [(dots[a][0] * s, dots[a][1] * s),
                   (dots[b][0] * s, dots[b][1] * s)], int(s * 0.045),
            ink=(112, 96, 78))
    for key, (x, y) in dots.items():
        r = s * (0.09 if key != "f" else 0.12)
        box = (x * s - r, y * s - r, x * s + r, y * s + r)
        if img is not None:
            if key == "f":
                shaded_ellipse(img, box, GOLD_HI, GOLD_LO, KEY)
            else:
                shaded_ellipse(img, box, IRON_HI, IRON_LO, KEY)
        else:
            d.ellipse(box, fill=GOLD if key == "f" else INK)


def em_free(d, s, img=None):
    # 0.5 and no more: the outline runs to +-0.95u, so anything
    # larger clips its own tip against the canvas edge.
    flame(d, s, s * 0.5, s * 0.5, 0.5, img=img)


def em_blaze(d, s, img=None):
    """An hourglass with the sand alight.

    Blaze is the three-minute fire, and the old emblem was four abstract
    chevrons that said nothing about a clock. An hourglass says it at any
    size, and nothing else in the picker is one.
    """
    def p(x, y):
        return (s * x, s * y)

    cap_top = [p(0.16, 0.08), p(0.84, 0.08), p(0.84, 0.18), p(0.16, 0.18)]
    cap_bot = [p(0.16, 0.82), p(0.84, 0.82), p(0.84, 0.92), p(0.16, 0.92)]
    glass = [p(0.24, 0.18), p(0.76, 0.18), p(0.54, 0.50), p(0.76, 0.82),
             p(0.24, 0.82), p(0.46, 0.50)]
    sand_up = [p(0.30, 0.24), p(0.70, 0.24), p(0.52, 0.50), p(0.48, 0.50)]
    sand_dn = [p(0.50, 0.60), p(0.68, 0.78), p(0.32, 0.78)]
    if img is not None:
        shaded_poly(img, glass, (72, 62, 52), (44, 37, 30), KEY)
        shaded_poly(img, sand_up, FIRE_HI, FIRE_LO)
        shaded_poly(img, sand_dn, FIRE_HI, FIRE_LO)
        d.line([p(0.5, 0.50), p(0.5, 0.62)], fill=GOLD, width=int(s * 0.035))
        shaded_poly(img, cap_top, IRON_HI, IRON_LO, KEY)
        shaded_poly(img, cap_bot, IRON_HI, IRON_LO, KEY)
    else:
        d.polygon(glass, outline=INK, width=int(s * 0.05))
        d.polygon(sand_up, fill=EMBER)
        d.polygon(sand_dn, fill=EMBER)
        d.polygon(cap_top, fill=INK)
        d.polygon(cap_bot, fill=INK)


def em_inferno(d, s, img=None):
    """The floor coming up: three slabs and two chevrons driving them.

    This used to be a bar chart, pixel for pixel the same drawing as the
    Profile icon - two different screens showing the same picture for two
    unrelated things.
    """
    def p(x, y):
        return (s * x, s * y)

    slabs = [(0.62, (150, 134, 116), (78, 66, 54)),
             (0.74, (128, 114, 98), (66, 56, 46)),
             (0.86, (108, 96, 82), (54, 46, 38))]
    for top, hi, lo in slabs:
        bar = [p(0.10, top), p(0.90, top), p(0.90, top + 0.10),
               p(0.10, top + 0.10)]
        if img is not None:
            shaded_poly(img, bar, hi, lo, KEY)
        else:
            d.polygon(bar, fill=MUTED)
    for top in (0.14, 0.36):
        chevron = [p(0.50, top), p(0.86, top + 0.20), p(0.70, top + 0.20),
                   p(0.50, top + 0.09), p(0.30, top + 0.20),
                   p(0.14, top + 0.20)]
        if img is not None:
            shaded_poly(img, chevron, FIRE_HI, FIRE_LO, KEY)
        else:
            d.polygon(chevron, fill=EMBER)


def em_cheese(d, s, img=None):
    """Four rubble rows with a hole punched in each - the thing the mode is
    actually about, drawn the way it appears in the well."""
    cols, rows = 6, 4
    gaps = (4, 1, 5, 2)
    cw, ch = 0.76 / cols, 0.60 / rows
    for r in range(rows):
        for c in range(cols):
            if c == gaps[r]:
                continue
            x, y = 0.12 + c * cw, 0.24 + r * ch
            cell = [(s * x, s * y), (s * (x + cw * 0.92), s * y),
                    (s * (x + cw * 0.92), s * (y + ch * 0.9)),
                    (s * x, s * (y + ch * 0.9))]
            if img is not None:
                shaded_poly(img, cell, (150, 136, 120), (78, 68, 58), KEY, SS)
            else:
                d.polygon(cell, fill=MUTED)


def em_duel(d, s, img=None):
    """Two blades crossed, heraldry-style: hilts low, points high and apart.

    The map's warden carries one blade point-down; a duel is two of them
    crossed, which is a different silhouette at any size.
    """
    for angle in (math.radians(38), math.radians(-38)):
        cx, cy = s * 0.5, s * 0.5

        def r(pts):
            return spin([(s * x, s * y) for x, y in pts], cx, cy, angle)

        blade = r([(0.50, 0.02), (0.575, 0.20), (0.575, 0.62),
                   (0.425, 0.62), (0.425, 0.20)])
        guard = r([(0.26, 0.62), (0.74, 0.62), (0.74, 0.70), (0.26, 0.70)])
        grip = r([(0.44, 0.70), (0.56, 0.70), (0.56, 0.90), (0.44, 0.90)])
        pommel = r([(0.40, 0.88), (0.60, 0.88), (0.56, 0.97), (0.44, 0.97)])
        if img is not None:
            shaded_poly(img, blade, (240, 234, 224), (118, 130, 146), KEY)
            shaded_poly(img, guard, GOLD_HI, GOLD_LO, KEY)
            shaded_poly(img, grip, (120, 84, 56), (70, 46, 30), KEY)
            shaded_poly(img, pommel, GOLD_HI, GOLD_LO, KEY)
        else:
            d.polygon(blade, fill=INK)
            d.polygon(guard, fill=GOLD)
            d.polygon(grip, fill=MUTED)


def ic_replay(d, s, img=None):
    """A play head inside a rewind ring, cut with a gap so the ring reads."""
    if img is not None:
        shaded_ellipse(img, (s * 0.10, s * 0.10, s * 0.90, s * 0.90),
            IRON_HI, IRON_LO, KEY)
        shaded_ellipse(img, (s * 0.24, s * 0.24, s * 0.76, s * 0.76),
            (30, 24, 19), (18, 14, 11))
        head = [(s * 0.42, s * 0.32), (s * 0.42, s * 0.68), (s * 0.72, s * 0.5)]
        shaded_poly(img, head, GOLD_HI, GOLD_LO, KEY, SS)
    else:
        d.ellipse((s * 0.14, s * 0.14, s * 0.86, s * 0.86), outline=INK,
            width=int(s * 0.06))
        d.polygon([(s * 0.42, s * 0.32), (s * 0.42, s * 0.68),
                   (s * 0.72, s * 0.5)], fill=INK)


def ic_profile(d, s, img=None):
    """Growth: steel columns under a gold trend line."""
    def p(x, y):
        return (s * x, s * y)

    tops = (0.56, 0.42, 0.48, 0.24)
    for i, top in enumerate(tops):
        x = 0.16 + i * 0.19
        bar = [p(x, top), p(x + 0.13, top), p(x + 0.13, 0.84), p(x, 0.84)]
        if img is not None:
            shaded_poly(img, bar, (168, 186, 206), (78, 90, 106), KEY)
        else:
            d.polygon(bar, fill=INK)
    if img is not None:
        d.line([p(0.225, 0.52), p(0.415, 0.38), p(0.605, 0.44),
                p(0.795, 0.20)], fill=GOLD, width=int(s * 0.05),
            joint="curve")
        shaded_poly(img, [p(0.10, 0.86), p(0.90, 0.86), p(0.90, 0.94),
                          p(0.10, 0.94)], IRON_HI, IRON_LO, KEY)
    else:
        stroke(d, [p(0.14, 0.9), p(0.86, 0.9)], int(s * 0.04))


def ic_scores(d, s, img=None):
    """A cup with handles, on a plinth."""
    def p(x, y):
        return (s * x, s * y)

    cup = [p(0.30, 0.14), p(0.70, 0.14), p(0.66, 0.50), p(0.50, 0.62),
           p(0.34, 0.50)]
    stem = [p(0.44, 0.60), p(0.56, 0.60), p(0.56, 0.76), p(0.44, 0.76)]
    foot = [p(0.32, 0.76), p(0.68, 0.76), p(0.72, 0.88), p(0.28, 0.88)]
    if img is not None:
        shaded_poly(img, cup, GOLD_HI, GOLD_LO, KEY)
        shaded_poly(img, stem, GOLD_HI, GOLD_LO, KEY)
        shaded_poly(img, foot, GOLD_HI, GOLD_LO, KEY)
    else:
        d.polygon(cup, fill=GOLD)
        d.polygon(stem, fill=GOLD)
        d.polygon(foot, fill=GOLD)
    stroke(d, [p(0.31, 0.20), p(0.16, 0.24), p(0.22, 0.42), p(0.35, 0.48)],
        int(s * 0.05), ink=GOLD_LO)
    stroke(d, [p(0.69, 0.20), p(0.84, 0.24), p(0.78, 0.42), p(0.65, 0.48)],
        int(s * 0.05), ink=GOLD_LO)


def ic_help(d, s, img=None):
    """An open book. The old glyph was a hexagon with a line down it, which
    at menu size read as a nut off a bolt rather than a manual."""
    def p(x, y):
        return (s * x, s * y)

    left = [p(0.08, 0.24), p(0.48, 0.32), p(0.48, 0.88), p(0.08, 0.80)]
    right = [p(0.92, 0.24), p(0.52, 0.32), p(0.52, 0.88), p(0.92, 0.80)]
    spine = [p(0.46, 0.30), p(0.54, 0.30), p(0.54, 0.90), p(0.46, 0.90)]
    if img is not None:
        shaded_poly(img, left, (238, 230, 214), (150, 138, 120), KEY)
        shaded_poly(img, right, (238, 230, 214), (150, 138, 120), KEY)
        shaded_poly(img, spine, (128, 88, 56), (74, 48, 28), KEY)
        for i in range(3):
            y = 0.44 + i * 0.13
            d.line([p(0.16, y - 0.015), p(0.42, y + 0.035)],
                fill=(122, 108, 92), width=int(s * 0.03))
            d.line([p(0.58, y + 0.035), p(0.84, y - 0.015)],
                fill=(122, 108, 92), width=int(s * 0.03))
    else:
        d.polygon(left, outline=INK, width=int(s * 0.05))
        d.polygon(right, outline=INK, width=int(s * 0.05))


def ic_settings(d, s, img=None):
    """A cog with teeth you can count, not eight spokes off a disc."""
    cx = cy = s / 2
    teeth = []
    for i in range(8):
        a = i * math.pi / 4
        tooth = [(cx - s * 0.075, cy - s * 0.46), (cx + s * 0.075,
            cy - s * 0.46), (cx + s * 0.12, cy - s * 0.26),
            (cx - s * 0.12, cy - s * 0.26)]
        teeth.append(spin(tooth, cx, cy, a))
    if img is not None:
        for tooth in teeth:
            shaded_poly(img, tooth, IRON_HI, IRON_LO, KEY)
        shaded_ellipse(img, (cx - s * 0.32, cy - s * 0.32, cx + s * 0.32,
            cy + s * 0.32), IRON_HI, IRON_LO, KEY)
        shaded_ellipse(img, (cx - s * 0.13, cy - s * 0.13, cx + s * 0.13,
            cy + s * 0.13), (26, 21, 17), (14, 11, 9), KEY, SS)
    else:
        for tooth in teeth:
            d.polygon(tooth, fill=INK)
        d.ellipse((cx - s * 0.32, cy - s * 0.32, cx + s * 0.32, cy + s * 0.32),
            fill=INK)
        d.ellipse((cx - s * 0.13, cy - s * 0.13, cx + s * 0.13, cy + s * 0.13),
            fill=PANEL)


def main():
    backdrop()
    frame("plate", 48, 14, rivets=False)
    icon("node_battle", 64, ic_battle)
    icon("maul", 160, ic_maul, glow=False)
    icon("node_boss", 64, ic_boss)
    icon("node_forge", 64, ic_forge)
    icon("node_event", 64, ic_event)
    icon("node_mini", 64, ic_mini)
    icon("ember", 28, ic_ember)
    icon("slag", 28, ic_slag, glow=False)
    icon("star", 28, ic_star, glow=False)
    icon("em_map", 96, em_map)
    icon("em_free", 96, em_free)
    icon("em_blaze", 96, em_blaze)
    icon("em_inferno", 96, em_inferno)
    icon("em_cheese", 96, em_cheese, glow=False)
    icon("em_duel", 96, em_duel)
    icon("ic_replay", 64, ic_replay, glow=False)
    icon("ic_profile", 64, ic_profile, glow=False)
    icon("ic_scores", 64, ic_scores, glow=False)
    icon("ic_help", 64, ic_help, glow=False)
    icon("ic_settings", 64, ic_settings, glow=False)


if __name__ == "__main__":
    main()
