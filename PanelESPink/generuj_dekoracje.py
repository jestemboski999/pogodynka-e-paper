"""Generator swiatecznych opraw ekranu -> dekoracje_bitmapy.h.

Kazda okazja to:
  * rama 800x480 (girlandy przez caly gorny brzeg, ozdoby zwisajace po bokach),
    rysowana wylacznie w strefach, w ktorych uklad nigdy nie ma tresci,
  * zestaw malych motywow (sniezynki, gwiazdki, konfetti...), ktore firmware
    rozsiewa w wolnych miejscach ekranu, omijajac tekst i wykres.
Rysunek powstaje w 3-krotnym powiekszeniu i jest progowany do czerni/bieli;
kolor (czerwien) oddaje kreskowanie. Uruchomienie z katalogu PanelESPink:
    python generuj_dekoracje.py
"""
import math
import random
from pathlib import Path
from PIL import Image, ImageDraw

W, H, S = 800, 480, 3
HERE = Path(__file__).resolve().parent

# Strefy, w ktorych rama NIE moze miec ani piksela (x0, y0, x1, y1 wlacznie).
KEEP_OUT = [
    (29, 46, 775, 479),      # cala tresc: zegar, ikona, temperatura, podsumowanie, wykres
    (630, 7, 775, 32),       # "Aktualizacja 12:34" / "Ostatnie dane"
    (536, 44, 775, 479),     # cyfry temperatury siegaja y=48
    (385, 44, 545, 60),      # gora ikony pogody
    (700, 450, 799, 479),    # wskaznik baterii
]
LABEL_BOX = (28, 10, 210, 40)   # "Warszawa · Wola" -- wolne tylko, gdy etykieta ukryta
CLOCK_STRIP = (16, 45, 29, 162)  # lewy margines wymazywany co minute przez zegar


class Canvas:
    def __init__(self, w=W, h=H):
        self.w, self.h = w, h
        size = (w * S, h * S)
        self.ink = Image.new('L', size, 255)
        self.hatch = Image.new('L', size, 255)
        self.dots = Image.new('L', size, 255)
        self.erase = Image.new('L', size, 255)

    @staticmethod
    def p(pts):
        return [(round(x * S), round(y * S)) for x, y in pts]

    def line(self, pts, w=1.6, fill=0):
        ImageDraw.Draw(self.ink).line(self.p(pts), fill=fill, width=max(1, round(w * S)), joint='curve')

    def poly(self, pts, fill=None, w=1.6):
        ImageDraw.Draw(self.ink).polygon(self.p(pts), fill=fill)
        if w:
            self.line(list(pts) + [pts[0]], w)

    def ellipse(self, cx, cy, rx, ry, fill=None, w=1.6):
        box = [round((cx - rx) * S), round((cy - ry) * S), round((cx + rx) * S), round((cy + ry) * S)]
        ImageDraw.Draw(self.ink).ellipse(box, fill=fill, outline=0 if w else None, width=round(w * S) if w else 0)

    def dot(self, x, y, r=1.1):
        self.ellipse(x, y, r, r, fill=0, w=0)

    def region(self, pts, layer):
        ImageDraw.Draw(layer).polygon(self.p(pts), fill=0)

    def white(self, pts):
        ImageDraw.Draw(self.erase).polygon(self.p(pts), fill=0)

    def star(self, cx, cy, r, fill=0, points=5, inner=0.45, rot=-90, w=0):
        pts = []
        for i in range(points * 2):
            rr = r if i % 2 == 0 else r * inner
            a = math.radians(rot + i * 180 / points)
            pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
        self.poly(pts, fill=fill, w=w)

    def heart(self, cx, cy, r, fill=None, w=1.6):
        pts = []
        for i in range(90):
            t = i * 2 * math.pi / 90
            pts.append((cx + r * math.sin(t) ** 3,
                        cy - r * (13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)) / 16))
        self.poly(pts, fill=fill, w=w)
        return pts

    def render(self):
        def small(img):
            return img.resize((self.w, self.h), Image.Resampling.LANCZOS).point(lambda v: 0 if v < 128 else 255)
        ink, hatch, dots, erase = small(self.ink), small(self.hatch), small(self.dots), small(self.erase)
        out = Image.new('L', (self.w, self.h), 255)
        px, ip, hp, dp, ep = out.load(), ink.load(), hatch.load(), dots.load(), erase.load()
        for y in range(self.h):
            for x in range(self.w):
                black = ip[x, y] == 0
                if not black and ep[x, y] != 0:
                    if hp[x, y] == 0 and (x + y) % 3 == 0:
                        black = True
                    elif dp[x, y] == 0 and x % 3 == 0 and y % 3 == ((x // 3) % 2) * 2:
                        black = True
                if black:
                    px[x, y] = 0
        return out


# ---------------------------------------------------------------------------
#  Geometria girland
# ---------------------------------------------------------------------------
def swag(x0, y0, x1, y1, dip, n=80):
    """Zwis miedzy dwoma haczykami: prosta + parabola ugiecia."""
    return [(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t + dip * 4 * t * (1 - t)) for t in [i / n for i in range(n + 1)]]


def swags(hooks, dip):
    pts = []
    for (x0, y0), (x1, y1) in zip(hooks, hooks[1:]):
        seg = swag(x0, y0, x1, y1, dip)
        pts += seg if not pts else seg[1:]
    return pts


def along(pts, spacing, offset=0.0):
    """Punkty rozmieszczone co 'spacing' wzdluz lamanej: (x, y, kat stycznej)."""
    out, acc, nxt = [], 0.0, offset
    for (xa, ya), (xb, yb) in zip(pts, pts[1:]):
        seg = math.hypot(xb - xa, yb - ya)
        while seg > 0 and nxt <= acc + seg:
            t = (nxt - acc) / seg
            out.append((xa + (xb - xa) * t, ya + (yb - ya) * t, math.atan2(yb - ya, xb - xa)))
            nxt += spacing
        acc += seg
    return out


def paper_chain(c, pts, rx=6.0, ry=3.6, spacing=9.5):
    """Lancuch z papieru: ogniwa na przemian jasne (kreskowane) i ciemne, wzdluz toru."""
    for k, (x, y, a) in enumerate(along(pts, spacing, spacing / 2)):
        ring = []
        for i in range(28):
            t = i * 2 * math.pi / 28
            ex, ey = rx * math.cos(t), (ry if k % 2 == 0 else ry * 0.55) * math.sin(t)
            ring.append((x + ex * math.cos(a) - ey * math.sin(a), y + ex * math.sin(a) + ey * math.cos(a)))
        if k % 2 == 0:
            c.poly(ring, fill=255, w=1.5)
            c.region(ring, c.hatch)
        else:
            c.poly(ring, fill=0, w=1.2)


def light_string(c, pts, spacing=24, offset=12):
    c.line(pts, 1.2)
    for k, (x, y, a) in enumerate(along(pts, spacing, offset)):
        tilt = math.radians(-11 if k % 2 else 11)
        def rot(dx, dy):
            return (x + dx * math.cos(tilt) - dy * math.sin(tilt), y + dx * math.sin(tilt) + dy * math.cos(tilt))
        c.poly([rot(-2.2, 0), rot(2.2, 0), rot(2.2, 3.5), rot(-2.2, 3.5)], fill=0, w=0)
        bulb = [rot(3.6 * math.sin(t), 8.5 - 5.2 * math.cos(t) - (1.2 if math.cos(t) < 0 else 0))
                for t in [i * 2 * math.pi / 30 for i in range(30)]]
        c.poly(bulb, fill=0 if k % 3 == 0 else 255, w=1.4)
        if k % 3 == 1:
            c.region(bulb, c.hatch)


def bow(c, x, y, s=1.0):
    for sg in (-1, 1):
        c.poly([(x, y), (x + sg * 9 * s, y - 5 * s), (x + sg * 10 * s, y + 4 * s)], fill=0, w=1.2)
        c.line([(x, y), (x + sg * 5 * s, y + 11 * s)], 2.0)
    c.ellipse(x, y, 2.4 * s, 2.4 * s, fill=0, w=0)


def bauble(c, x, top, cy, r, style):
    c.line([(x, top), (x, cy - r - 4)], 1.1)
    c.poly([(x - 3, cy - r - 3), (x + 3, cy - r - 3), (x + 3, cy - r + 1), (x - 3, cy - r + 1)], fill=0, w=0)
    ball = [(x + r * math.cos(t), cy + r * math.sin(t)) for t in [i * 2 * math.pi / 48 for i in range(48)]]
    if style == 'black':
        c.poly(ball, fill=0, w=1.4)
        c.ellipse(x - r * 0.38, cy - r * 0.4, r * 0.2, r * 0.28, fill=255, w=0)
        c.line([(x - r + 2, cy + 1), (x - r / 2, cy - 2), (x, cy + 1), (x + r / 2, cy - 2), (x + r - 2, cy + 1)], 1.3, fill=255)
    else:
        c.poly(ball, fill=255, w=1.6)
        c.region([(px, max(min(py, cy + r * 0.3), cy - r * 0.3)) for px, py in ball], c.hatch)
        c.line([(x - r + 1, cy - r * 0.3), (x + r - 1, cy - r * 0.3)], 1.0)
        c.line([(x - r + 1, cy + r * 0.3), (x + r - 1, cy + r * 0.3)], 1.0)
        c.ellipse(x - r * 0.4, cy - r * 0.6, r * 0.14, r * 0.2, fill=0, w=0)


def hanging_star(c, x, top, y, r):
    c.line([(x, top), (x, y - r)], 1.1)
    c.star(x, y, r, fill=255, w=1.4)
    c.star(x, y, r * 0.45, fill=0)


def snowflake(c, cx, cy, r, w=1.5, branches=True):
    for k in range(6):
        a = math.radians(90 + k * 60)
        ex, ey = cx + r * math.cos(a), cy - r * math.sin(a)
        c.line([(cx, cy), (ex, ey)], w)
        if branches:
            for f, l in ((0.5, 0.36), (0.75, 0.24)):
                bx, by = cx + r * f * math.cos(a), cy - r * f * math.sin(a)
                for sg in (-1, 1):
                    b = a + sg * math.radians(45)
                    c.line([(bx, by), (bx + r * l * math.cos(b), by - r * l * math.sin(b))], w * 0.8)
    c.ellipse(cx, cy, r * 0.14, r * 0.14, fill=0, w=0)


# ---------------------------------------------------------------------------
#  Ozdoby do zawieszania (pisanki, serca, choragiewki...)
# ---------------------------------------------------------------------------
def egg(c, cx, cy, rx, ry, pattern, tilt=0):
    def rot(x, y):
        a = math.radians(tilt)
        dx, dy = x - cx, y - cy
        return (cx + dx * math.cos(a) - dy * math.sin(a), cy + dx * math.sin(a) + dy * math.cos(a))
    outline = [rot(cx + rx * math.sin(t) * (1 - 0.18 * math.cos(t)), cy - ry * math.cos(t))
               for t in [i * 2 * math.pi / 60 for i in range(60)]]
    if pattern == 'dark':
        c.poly(outline, fill=0)
        for x, y in [(-0.5, -0.3), (0, -0.55), (0.5, -0.3), (-0.5, 0.35), (0, 0.1), (0.5, 0.35), (0, 0.7)]:
            px, py = rot(cx + x * rx, cy + y * ry)
            c.ellipse(px, py, max(1.1, rx * 0.14), max(1.1, rx * 0.14), fill=255, w=0)
        return
    c.poly(outline, fill=255)
    if pattern == 'zigzag':
        for yy in (-0.35, 0.35):
            c.line([rot(cx - rx * 0.92, cy + yy * ry), rot(cx + rx * 0.92, cy + yy * ry)], 1.1)
        c.line([rot(cx - rx * 0.8 + i * rx * 0.32, cy + (-0.18 if i % 2 else 0.18) * ry) for i in range(6)], 1.1)
    elif pattern == 'hatch':
        c.region([rot(cx - rx, cy - ry * 0.28), rot(cx + rx, cy - ry * 0.28), rot(cx + rx, cy + ry * 0.28), rot(cx - rx, cy + ry * 0.28)], c.hatch)
        c.white([rot(cx - rx * 2, cy - ry * 2), rot(cx + rx * 2, cy - ry * 2), rot(cx + rx * 2, cy - ry * 0.3), rot(cx - rx * 2, cy - ry * 0.3)])
        c.line([rot(cx - rx * 0.95, cy - ry * 0.28), rot(cx + rx * 0.95, cy - ry * 0.28)], 1.1)
        c.line([rot(cx - rx * 0.95, cy + ry * 0.28), rot(cx + rx * 0.95, cy + ry * 0.28)], 1.1)
        for x, y in [(-0.3, -0.6), (0.3, -0.6), (0, 0.62)]:
            px, py = rot(cx + x * rx, cy + y * ry)
            c.dot(px, py, 1.1)
    else:  # waves
        for yy in (-0.4, 0.05, 0.5):
            c.line([rot(cx - rx * (0.9 - abs(yy) * 0.4) + i * rx * (1.8 - abs(yy) * 0.8) / 10,
                        cy + yy * ry + 1.3 * math.sin(i * 1.3)) for i in range(11)], 1.0)


def pennant(c, x, y, a, k, kind):
    """Choragiewka na sznurku: triangle (trojkat) albo poland (flaga biało-czerwona)."""
    ca, sa = math.cos(a), math.sin(a)
    def rot(dx, dy):
        return (x + dx * ca - dy * sa, y + dx * sa + dy * ca)
    if kind == 'poland':
        low = [rot(-8, 6), rot(8, 6), rot(8, 12), rot(-8, 12)]
        c.region(low, c.hatch)
        c.poly([rot(-8, 0), rot(8, 0), rot(8, 12), rot(-8, 12)], w=1.3)
        c.line([rot(-8, 6), rot(8, 6)], 1.0)
        return
    tri = [rot(-9, 0), rot(9, 0), rot(0, 19)]
    style = k % 3
    c.poly(tri, fill=0 if style == 0 else 255, w=1.4)
    if style == 1:
        c.region(tri, c.hatch)
    elif style == 2:
        c.dot(*rot(0, 7), 1.6)


# ---------------------------------------------------------------------------
#  Ramy poszczegolnych okazji
# ---------------------------------------------------------------------------
HOOKS = [(-12, 2), (150, 4), (300, 3), (452, 4), (612, 3)]


def top_rail(c, draw, y=3):
    """Prosty odcinek wzdluz gornej krawedzi nad godzina aktualizacji (do rogu)."""
    draw(c, [(612 + i * 4, y) for i in range(48)])


def frame_christmas(c):
    paper_chain(c, swags(HOOKS, 30))
    light_string(c, swags([(-12, 3), (75, 5), (225, 4), (375, 5), (530, 4), (612, 4)], 16), 23, 8)
    top_rail(c, lambda cc, pts: paper_chain(cc, pts, 4.6, 2.4, 7.5))
    for x, y in HOOKS[1:]:
        bow(c, x, y + 2)
    bauble(c, 12, 0, 214, 11, 'black')
    hanging_star(c, 7, 0, 118, 6.5)
    bauble(c, 787, 6, 176, 10.5, 'hatch')
    hanging_star(c, 787, 188, 300, 9)
    bauble(c, 787, 310, 400, 10.5, 'black')
    for x, y, r in [(13, 300, 9), (14, 400, 7), (11, 460, 6)]:
        snowflake(c, x, y, r, 1.2)


def frame_santa(c):
    path = swags(HOOKS, 20)
    c.line(path, 1.3)
    for k, (x, y, a) in enumerate(along(path, 38, 19)):
        if k % 2 == 0:
            c.line([(x, y), (x, y + 6)], 1.0)
            c.star(x, y + 13, 7.5, fill=255, w=1.4)
            c.star(x, y + 13, 3, fill=0)
        else:  # laska cukrowa
            hook = [(x - 4 + 4 * math.cos(t), y + 6 - 4 * math.sin(t)) for t in [math.pi * i / 10 for i in range(11)]]
            stick = hook[::-1] + [(x, y + 6), (x, y + 20)]
            c.line(stick, 3.6)
            c.line(stick, 1.4, fill=255)
            for yy in range(8, 20, 4):
                c.line([(x - 1.7, y + yy + 1.5), (x + 1.7, y + yy - 1.5)], 1.2)
    top_rail(c, lambda cc, pts: cc.line(pts, 1.3))
    for x, y in HOOKS[1:]:
        bow(c, x, y + 2)
    hanging_star(c, 7, 0, 150, 6.5)
    bauble(c, 787, 6, 200, 10.5, 'hatch')
    for x, y, r in [(13, 260, 9), (787, 300, 9), (13, 380, 7), (787, 390, 8)]:
        snowflake(c, x, y, r, 1.2)


def frame_newyear(c):
    # serpentyny: faliste wstazki przewieszone przez gore
    for base, amp, ph in [(8, 6, 0.0), (16, 5, 1.7)]:
        pts = [(x, base + amp * math.sin(x / 23 + ph) + 10 * math.sin(math.pi * (x % 152) / 152)) for x in range(-5, 615, 3)]
        c.line(pts, 2.6)
        c.line(pts, 0.9, fill=255)
    top_rail(c, lambda cc, pts: cc.line([(x, 4 + 2 * math.sin(x / 9)) for x, _ in pts], 1.6))
    # fajerwerki w pasie naglowka; srodek lewej czesci zostaje na numer roku
    for x, y, r, n in [(40, 28, 15, 12), (330, 22, 17, 14), (470, 18, 12, 10), (575, 24, 14, 12)]:
        for k in range(n):
            a = 2 * math.pi * k / n
            c.line([(x + r * 0.3 * math.cos(a), y + r * 0.3 * math.sin(a)), (x + r * 0.8 * math.cos(a), y + r * 0.8 * math.sin(a))], 1.3)
            c.dot(x + r * math.cos(a), y + r * math.sin(a), 1.1)
    # serpentyny splywajace po bokach
    for x0 in (8, 787):
        pts = [(x0 + 5 * math.sin(y / 11), y) for y in range(20, 440, 2)]
        c.line(pts, 2.4)
        c.line(pts, 0.8, fill=255)


def frame_easter(c):
    path = swags(HOOKS, 17)
    c.line(path, 1.2)
    pats = ['zigzag', 'dark', 'hatch', 'waves']
    for k, (x, y, a) in enumerate(along(path, 34, 17)):
        drop = 2 + (k % 2) * 3
        c.line([(x, y), (x, y + drop)], 1.0)
        egg(c, x, y + drop + 8.5, 6.2, 8.5, pats[k % 4], tilt=(-8 if k % 2 else 8))
    top_rail(c, lambda cc, pts: cc.line(pts, 1.2))
    for x, y in HOOKS[1:]:
        bow(c, x, y + 2, 0.9)
    # bazie w marginesach
    for x0 in (7, 787):
        pts = [(x0 + 3 * math.sin(y / 30), y) for y in range(20, 440, 3)]
        c.line(pts, 1.4)
        for i, (x, y) in enumerate(pts[6::9]):
            side = 1 if i % 2 else -1
            c.ellipse(x + side * 3.4, y, 3.0, 4.2, fill=255, w=1.1)
            c.line([(x, y + 3), (x + side * 2, y + 1)], 1.0)


def frame_hearts(c):
    path = swags(HOOKS, 16)
    c.line(path, 1.1)
    for k, (x, y, a) in enumerate(along(path, 30, 15)):
        drop = 3 + (k % 3) * 3
        c.line([(x, y), (x, y + drop)], 1.0)
        r = 8.5 if k % 2 == 0 else 7
        pts = c.heart(x, y + drop + r * 0.9, r, fill=255 if k % 2 == 0 else 0, w=1.5)
        if k % 2 == 0:
            c.region(pts, c.hatch)
    top_rail(c, lambda cc, pts: cc.line(pts, 1.1))
    for x0 in (8, 787):
        c.line([(x0, 0), (x0, 425)], 1.0)
        for i, y in enumerate(range(70, 440, 62)):
            r = 6 if x0 < 400 else 8.5
            pts = c.heart(x0, y, r, fill=0 if i % 2 else 255, w=1.4)
            if i % 2 == 0:
                c.region(pts, c.hatch)


def paczek(c, cx, cy, r):
    body = [(cx + r * math.cos(t), cy + r * 0.86 * math.sin(t)) for t in [i * 2 * math.pi / 70 for i in range(70)]]
    c.region(body, c.dots)
    icing = [(cx + r * 0.97 * math.cos(math.pi + math.pi * i / 30), cy + r * 0.84 * math.sin(math.pi + math.pi * i / 30)) for i in range(31)]
    for i in range(1, 10):
        icing.append((cx + r * 0.97 - i * r * 1.94 / 10, cy + 1.5 + (3.2 if i % 2 else 0.3)))
    c.white(icing)
    c.poly(body, w=1.6)
    c.line(icing[30:] + [icing[0]], 1.2)
    rnd = random.Random(int(cx * 7 + cy))
    for _ in range(6):
        px, py = cx + rnd.uniform(-0.6, 0.6) * r, cy - rnd.uniform(0.15, 0.65) * r
        a = rnd.uniform(0, math.pi)
        c.line([(px - 1.7 * math.cos(a), py - 1.7 * math.sin(a)), (px + 1.7 * math.cos(a), py + 1.7 * math.sin(a))], 1.5)


def frame_donut(c):
    path = swags(HOOKS, 18)
    c.line(path, 1.2)
    for k, (x, y, a) in enumerate(along(path, 40, 20)):
        c.line([(x, y), (x, y + 4)], 1.0)
        paczek(c, x, y + 14, 10)
    top_rail(c, lambda cc, pts: cc.line(pts, 1.2))
    for x, y in HOOKS[1:]:
        bow(c, x, y + 2, 0.9)
    # paczki zawieszone jeden pod drugim w marginesach
    for x0, r, ys in ((8, 6.5, (230, 360)), (787, 10, (120, 270, 400))):
        top = 0
        for y in ys:
            c.line([(x0, top), (x0, y - r * 0.86 - 1)], 1.0)
            paczek(c, x0, y, r)
            top = y + r * 0.86 + 1


def frame_flags(c):
    path = swags(HOOKS, 22)
    c.line(path, 1.2)
    for k, (x, y, a) in enumerate(along(path, 26, 13)):
        pennant(c, x, y, a, k, 'poland')
    top_rail(c, lambda cc, pts: cc.line(pts, 1.2))
    # kotyliony w marginesach
    for x0, y0, r in ((788, 110, 9.5), (12, 250, 9)):
        c.line([(x0, 0), (x0, y0 - r)], 1.0)
        outer = [(x0 + (r + 1.2 * math.cos(k * 12 * 2 * math.pi / 72)) * math.cos(k * 2 * math.pi / 72),
                  y0 + (r + 1.2 * math.cos(k * 12 * 2 * math.pi / 72)) * math.sin(k * 2 * math.pi / 72)) for k in range(72)]
        c.region(outer, c.hatch)
        c.poly(outer, w=1.2)
        inner = [(x0 + r * 0.5 * math.cos(k * 2 * math.pi / 30), y0 + r * 0.5 * math.sin(k * 2 * math.pi / 30)) for k in range(30)]
        c.white(inner)
        c.poly(inner, fill=255, w=1.2)
        for sg in (-1, 1):
            tail = [(x0 + sg * 1, y0 + r * 0.6), (x0 + sg * 6, y0 + r + 12), (x0 + sg * 2, y0 + r + 10), (x0, y0 + r + 13)]
            c.region(tail, c.hatch)
            c.poly(tail, w=1.0)


def blossom(c, x, y, r, filled):
    for k in range(5):
        a = math.radians(-90 + k * 72)
        c.ellipse(x + r * 0.62 * math.cos(a), y + r * 0.62 * math.sin(a), r * 0.45, r * 0.45, fill=255, w=1.1)
    c.ellipse(x, y, r * 0.3, r * 0.3, fill=0 if filled else 255, w=1.0)


def frame_spring(c):
    path = swags(HOOKS, 22)
    c.line(path, 1.4)
    for k, (x, y, a) in enumerate(along(path, 13, 6)):
        side = 1 if k % 2 else -1
        n = a + side * math.radians(70)
        lx, ly = x + 7 * math.cos(n), y + 7 * math.sin(n)
        leaf = [(x, y), ((x + lx) / 2 + 2.5 * math.cos(n + 1.57), (y + ly) / 2 + 2.5 * math.sin(n + 1.57)), (lx, ly),
                ((x + lx) / 2 - 2.5 * math.cos(n + 1.57), (y + ly) / 2 - 2.5 * math.sin(n + 1.57))]
        c.poly(leaf, fill=0 if k % 4 == 1 else 255, w=1.1)
    for k, (x, y, a) in enumerate(along(path, 44, 22)):
        blossom(c, x, y + 4, 8, k % 2 == 0)
    top_rail(c, lambda cc, pts: cc.line(pts, 1.3))
    for x0 in (9, 788):
        pts = [(x0 + 2 * math.sin(y / 25), y) for y in range(10, 440, 3)]
        c.line(pts, 1.3)
        for i, (x, y) in enumerate(pts[10::22]):
            blossom(c, x, y, 7 if x0 < 400 else 8, i % 2 == 0)


def balloon(c, cx, cy, rx, ry, style, string_to):
    c.line([(cx, cy + ry + 2)] + [(cx + 1.8 * math.sin(i * 0.9), cy + ry + 2 + i * (string_to - cy - ry - 2) / 14) for i in range(1, 15)], 1.0)
    c.poly([(cx - 2, cy + ry + 2.5), (cx + 2, cy + ry + 2.5), (cx, cy + ry - 0.5)], fill=0, w=0)
    shape = [(cx + rx * math.cos(t), cy + ry * math.sin(t) * (1.06 if math.sin(t) > 0 else 1)) for t in [i * 2 * math.pi / 48 for i in range(48)]]
    c.poly(shape, fill=0 if style == 0 else 255, w=1.5)
    if style == 1:
        c.region(shape, c.hatch)
    c.ellipse(cx - rx * 0.4, cy - ry * 0.42, rx * 0.18, ry * 0.25, fill=255 if style == 0 else 0, w=0)


def frame_party(c):
    path = swags(HOOKS, 19)
    c.line(path, 1.3)
    for k, (x, y, a) in enumerate(along(path, 24, 12)):
        pennant(c, x, y, a, k, 'triangle')
    top_rail(c, lambda cc, pts: cc.line(pts, 1.3))
    for x, y in HOOKS[1:]:
        bow(c, x, y + 2)
    balloon(c, 14, 206, 11, 14, 0, 300)
    balloon(c, 12, 340, 9, 12, 1, 440)
    balloon(c, 787, 80, 10, 13, 1, 190)
    balloon(c, 787, 230, 10, 13, 0, 330)
    balloon(c, 787, 365, 10, 13, 2, 440)


def lantern(c, cx, base, s):
    top = base - 30 * s
    c.poly([(cx - 7 * s, base), (cx + 7 * s, base), (cx + 6 * s, base - 4 * s), (cx - 6 * s, base - 4 * s)], fill=0, w=1.2)
    c.poly([(cx - 6 * s, base - 4 * s), (cx + 6 * s, base - 4 * s), (cx + 5 * s, top + 7 * s), (cx - 5 * s, top + 7 * s)], fill=255, w=1.3)
    c.poly([(cx - 6 * s, top + 7 * s), (cx + 6 * s, top + 7 * s), (cx + 3 * s, top + 2 * s), (cx - 3 * s, top + 2 * s)], fill=0, w=1.2)
    c.ellipse(cx, top + 1 * s, 1.4 * s, 1.4 * s, fill=255, w=1.1)
    fy = base - 12 * s
    c.poly([(cx, fy - 8 * s), (cx + 2.6 * s, fy - 2 * s), (cx + 2 * s, fy + 1 * s), (cx, fy + 2 * s),
            (cx - 2 * s, fy + 1 * s), (cx - 2.6 * s, fy - 2 * s)], fill=0, w=0)


def frame_candle(c):
    # 1-2 listopada: bez girland, tylko trzy znicze w naglowku.
    lantern(c, 300, 43, 0.95)
    lantern(c, 324, 43, 1.25)
    lantern(c, 348, 43, 0.95)


FRAMES = {
    'christmas': frame_christmas, 'santa': frame_santa, 'newyear': frame_newyear, 'easter': frame_easter,
    'hearts': frame_hearts, 'donut': frame_donut, 'flags': frame_flags, 'spring': frame_spring,
    'children': frame_party, 'birthday': frame_party, 'candle': frame_candle,
}
SHOW_LABEL = {'candle'}   # w pozostale dni girlanda zastepuje nazwe miejscowosci


# ---------------------------------------------------------------------------
#  Motywy rozsiewane przez firmware
# ---------------------------------------------------------------------------
def motif(size, draw):
    c = Canvas(size, size)
    draw(c, size / 2)
    return c.render()


def m_snow(size, branches=True):
    return motif(size, lambda c, m: snowflake(c, m, m, m - 1.5, 1.4 if size > 16 else 1.2, branches))


def m_star(size, filled=False):
    def d(c, m):
        c.star(m, m, m - 1.2, fill=0 if filled else 255, w=0 if filled else 1.3)
        if not filled:
            c.star(m, m, (m - 1.2) * 0.35, fill=0)
    return motif(size, d)


def m_twinkle(size):
    return motif(size, lambda c, m: c.star(m, m, m - 0.8, points=4, inner=0.25))


def m_heart(size, style):
    def d(c, m):
        pts = c.heart(m, m + 0.5, m - 1.8, fill=0 if style == 0 else 255, w=1.4)
        if style == 1:
            c.region(pts, c.hatch)
    return motif(size, d)


def m_confetti(size, kind):
    def d(c, m):
        if kind == 0:
            a = 0.6
            pts = [(m + dx * math.cos(a) - dy * math.sin(a), m + dx * math.sin(a) + dy * math.cos(a))
                   for dx, dy in [(-5, -2), (5, -2), (5, 2), (-5, 2)]]
            c.poly(pts, fill=0, w=0)
        elif kind == 1:
            c.ellipse(m, m, m - 2.5, m - 2.5, w=1.6)
        else:
            c.line([(m - 6 + i, m + 3 * math.sin(i / 1.6)) for i in range(13)], 1.8)
    return motif(size, d)


def m_egg(size, pattern):
    return motif(size, lambda c, m: egg(c, m, m, m * 0.62, m - 1.5, pattern, tilt=12))


def m_blossom(size, filled):
    return motif(size, lambda c, m: blossom(c, m, m, m - 1.5, filled))


def m_butterfly(size):
    def d(c, m):
        k = (m - 1) / 9
        for sg in (-1, 1):
            c.poly([(m, m - k), (m + sg * 4 * k, m - 7 * k), (m + sg * 8.5 * k, m - 6 * k), (m + sg * 7.5 * k, m), (m + sg * 1.5 * k, m + k)], fill=255, w=1.3)
            c.poly([(m + sg * k, m + k), (m + sg * 7 * k, m + 2 * k), (m + sg * 6 * k, m + 6.5 * k), (m + sg * 1.5 * k, m + 5.5 * k)], fill=0, w=1.1)
        c.line([(m, m - 4 * k), (m, m + 6 * k)], 1.7)
        c.line([(m, m - 4 * k), (m - 2 * k, m - 8 * k)], 1.0)
        c.line([(m, m - 4 * k), (m + 2 * k, m - 8 * k)], 1.0)
    return motif(size, d)


def m_paczek(size):
    return motif(size, lambda c, m: paczek(c, m, m + 0.5, m - 1.5))


def m_gift(size):
    def d(c, m):
        s = size
        c.poly([(2, 5), (s - 2, 5), (s - 2, s - 1.5), (2, s - 1.5)], fill=255, w=1.4)
        c.line([(m, 5), (m, s - 1.5)], 1.6)
        c.line([(2, (s + 5) / 2), (s - 2, (s + 5) / 2)], 1.6)
        c.ellipse(m - 2.4, 3, 2.2, 1.6, w=1.1)
        c.ellipse(m + 2.4, 3, 2.2, 1.6, w=1.1)
    return motif(size, d)


MOTIFS = {
    'christmas': (9, lambda: [m_snow(29), m_snow(25), m_snow(21), m_star(21)]),
    'santa': (8, lambda: [m_snow(27), m_snow(21), m_gift(21)]),
    'newyear': (9, lambda: [m_star(21, True), m_star(23), m_twinkle(21), m_twinkle(17)]),
    'easter': (7, lambda: [m_egg(25, 'zigzag'), m_egg(23, 'dark'), m_egg(23, 'waves'), m_blossom(21, True)]),
    'hearts': (8, lambda: [m_heart(25, 1), m_heart(21, 2), m_heart(17, 0)]),
    'donut': (6, lambda: [m_paczek(27), m_paczek(23)]),
    'flags': (0, lambda: []),
    'spring': (7, lambda: [m_butterfly(25), m_blossom(21, True), m_blossom(19, False)]),
    'children': (8, lambda: [m_star(21, True), m_star(23), m_twinkle(19), m_heart(19, 1)]),
    'birthday': (8, lambda: [m_star(21, True), m_star(23), m_twinkle(19), m_gift(21)]),
    'candle': (0, lambda: []),
}
KINDS = ['christmas', 'easter', 'newyear', 'hearts', 'donut', 'flags', 'santa', 'spring', 'children', 'candle', 'birthday']


def pack_bits(im):
    w, h = im.size
    px = im.load()
    out = bytearray()
    for y in range(h):
        for bx in range((w + 7) // 8):
            v = 0
            for b in range(8):
                x = bx * 8 + b
                if x < w and px[x, y] == 0:
                    v |= 0x80 >> b
            out.append(v)
    return bytes(out)


def rle(data):
    """0..127: (n+1) bajtow doslownie; 128..255: (n-127) bajtow zerowych."""
    out, i = bytearray(), 0
    while i < len(data):
        if data[i] == 0:
            j = i
            while j < len(data) and data[j] == 0 and j - i < 128:
                j += 1
            out.append(127 + (j - i))
            i = j
        else:
            j = i
            while j < len(data) and j - i < 128 and not (data[j] == 0 and j + 1 < len(data) and data[j + 1] == 0):
                j += 1
            out.append(j - i - 1)
            out += data[i:j]
            i = j
    return bytes(out)


def unrle(data):
    out, i = bytearray(), 0
    while i < len(data):
        c = data[i]
        i += 1
        if c >= 128:
            out += bytes(c - 127)
        else:
            out += data[i:i + c + 1]
            i += c + 1
    return bytes(out)


def check_keepout(kind, im):
    px = im.load()
    zones = KEEP_OUT + [CLOCK_STRIP] + ([LABEL_BOX] if kind in SHOW_LABEL else [])
    bad = [(x, y) for (x0, y0, x1, y1) in zones for y in range(y0, y1 + 1) for x in range(x0, x1 + 1) if px[x, y] == 0]
    if bad:
        print('BLAD %s: %d pikseli w strefie tresci, np. %s' % (kind, len(bad), bad[:5]))
    return not bad


def main():
    parts = ['// Generated by generuj_dekoracje.py -- ramy 800x480 (RLE) i motywy do rozsiewania.', '#pragma once',
             'struct DecoMotif { uint8_t w, h; const uint8_t *bits; };',
             'struct DecoTheme { const uint8_t *frame; uint16_t frameLen; bool showLabel; uint8_t maxScatter, motifCount; const DecoMotif *motifs; };']
    ok = True
    for kind in KINDS:
        c = Canvas()
        FRAMES[kind](c)
        frame = c.render()
        ok &= check_keepout(kind, frame)
        raw = pack_bits(frame)
        packed = rle(raw)
        assert unrle(packed) == raw
        parts.append('static const uint8_t deco_%s_frame[] = {%s};' % (kind, ','.join(map(str, packed))))
        count, make = MOTIFS[kind]
        names = []
        for i, m in enumerate(make()):
            name = 'deco_%s_m%d' % (kind, i)
            parts.append('static const uint8_t %s[] = {%s};' % (name, ','.join(map(str, pack_bits(m)))))
            names.append('{%d,%d,%s}' % (m.size[0], m.size[1], name))
        if names:
            parts.append('static const DecoMotif deco_%s_motifs[] = {%s};' % (kind, ','.join(names)))
        parts.append('static const DecoTheme deco_%s = {deco_%s_frame,%d,%s,%d,%d,%s};' % (
            kind, kind, len(packed), 'true' if kind in SHOW_LABEL else 'false', count, len(names),
            'deco_%s_motifs' % kind if names else 'nullptr'))
        print('%-10s rama %5d B po RLE, motywow %d' % (kind, len(packed), len(names)))
    if not ok:
        raise SystemExit('Rama wchodzi w tresc ekranu -- nie zapisano dekoracje_bitmapy.h')
    (HERE / 'dekoracje_bitmapy.h').write_text('\n'.join(parts) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
