"""Draw the cat's frames from simple shapes and write the sprite sheet.

This bootstraps art/cat_sheet.png and art/animations.json. Once the sheet
is hand-edited in Piskel or Aseprite, stop running this script: it would
overwrite those edits.

Every part (body, head, legs, tail) is filled on its own layer and gets a
1px outline, then layers are stacked back to front. That gives each part a
readable edge even where it overlaps another part of the same colour.
All poses face right; the mod mirrors them for walking left.

Usage: python tools/cat_rig.py   (then python tools/sprites_to_cpp.py)
"""

import json
import math
from pathlib import Path

from PIL import Image

from sprites_to_cpp import CELL, PALETTE, argb_to_rgba

ROOT = Path(__file__).resolve().parent.parent
SHEET = ROOT / "art" / "cat_sheet.png"
ANIMATIONS = ROOT / "art" / "animations.json"
PREVIEW = ROOT / "art" / "preview.png"
PREVIEW_SCALE = 6

COLOURS = {key: argb_to_rgba(argb) for key, argb, _ in PALETTE}

# Every head() call records where the top of the skull is, so each frame can
# export an anchor for effects and hats: (x, y, upright).
HEAD_LOG = []


# --- layers -----------------------------------------------------------------

def blank():
    return [["."] * CELL for _ in range(CELL)]


def inside_ellipse(x, y, cx, cy, rx, ry):
    return ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1.0


def fill_ellipse(layer, cx, cy, rx, ry, ch):
    for y in range(CELL):
        for x in range(CELL):
            if inside_ellipse(x, y, cx, cy, rx, ry):
                layer[y][x] = ch


def distance_to_segment(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    length_sq = dx * dx + dy * dy
    t = 0.0 if length_sq == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length_sq))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def fill_stroke(layer, points, width, ch):
    """Thick polyline, round caps. Points are in pixel-centre coordinates."""
    half = width / 2
    for y in range(CELL):
        for x in range(CELL):
            px, py = x + 0.5, y + 0.5
            for (ax, ay), (bx, by) in zip(points, points[1:]):
                if distance_to_segment(px, py, ax, ay, bx, by) <= half:
                    layer[y][x] = ch
                    break


def fill_triangle(layer, a, b, c, ch):
    def side(p, q, r):
        return (p[0] - r[0]) * (q[1] - r[1]) - (q[0] - r[0]) * (p[1] - r[1])

    for y in range(CELL):
        for x in range(CELL):
            p = (x + 0.5, y + 0.5)
            d1, d2, d3 = side(p, a, b), side(p, b, c), side(p, c, a)
            has_neg = d1 < 0 or d2 < 0 or d3 < 0
            has_pos = d1 > 0 or d2 > 0 or d3 > 0
            if not (has_neg and has_pos):
                layer[y][x] = ch


def outlined(layer):
    """Return a copy with a 1px outline around every filled pixel."""
    out = [row[:] for row in layer]
    for y in range(CELL):
        for x in range(CELL):
            if layer[y][x] != ".":
                continue
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < CELL and 0 <= ny < CELL and layer[ny][nx] != ".":
                    out[y][x] = "k"
                    break
    return out


def stack(*layers):
    frame = blank()
    for layer in layers:
        for y in range(CELL):
            for x in range(CELL):
                if layer[y][x] != ".":
                    frame[y][x] = layer[y][x]
    return frame


def recolour(layer, ch, where):
    """Repaint filled pixels where where(x, y) is true. Returns a new layer."""
    return [
        [ch if layer[y][x] not in ".k" and where(x, y) else layer[y][x] for x in range(CELL)]
        for y in range(CELL)
    ]


def put(layer, pixels, ch):
    out = [row[:] for row in layer]
    for x, y in pixels:
        out[y][x] = ch
    return out


# --- parts ------------------------------------------------------------------

def head(cx, cy, eyes="open", mouth=None):
    """Profile head facing right, ears included. (cx, cy) is the skull centre.

    eyes: open | closed | happy (^ shape). mouth: None | small | wide | yawn.
    """
    HEAD_LOG.append((cx, cy - 4.5, True))
    layer = blank()
    fill_triangle(layer, (cx - 3.2, cy - 1.5), (cx - 2.2, cy - 6.5), (cx + 0.2, cy - 3), "o")
    fill_triangle(layer, (cx - 0.2, cy - 3), (cx + 2.0, cy - 6.8), (cx + 3.4, cy - 1.8), "o")
    fill_ellipse(layer, cx, cy, 4.4, 3.8, "o")
    fill_ellipse(layer, cx + 2.6, cy + 1.6, 1.9, 1.5, "c")  # muzzle
    layer = recolour(layer, "d", lambda x, y: y == int(cy - 3) and x in (int(cx) - 1, int(cx) + 1))
    layer = outlined(layer)
    ex, ey = int(cx + 1), int(cy - 1)
    layer = put(layer, [(int(cx - 2), int(cy - 4)), (int(cx + 2), int(cy - 4))], "p")  # inner ears
    if eyes == "open":
        layer = put(layer, [(ex, ey), (ex, ey + 1)], "b")
        layer = put(layer, [(ex + 1, ey)], "g")
    elif eyes == "happy":
        layer = put(layer, [(ex - 1, ey + 1), (ex, ey), (ex + 1, ey + 1)], "k")
    else:
        layer = put(layer, [(ex, ey + 1), (ex + 1, ey + 1)], "k")
    nose_x = max(x for x in range(CELL) if layer[int(cy + 1)][x] not in ".k")
    layer = put(layer, [(nose_x, int(cy + 1))], "p")
    mx, my = int(cx + 2), int(cy + 2)
    if mouth == "small":
        layer = put(layer, [(mx + 1, my + 1)], "k")
    elif mouth == "wide":
        layer = put(layer, [(mx + 1, my + 1), (mx + 2, my + 1)], "k")
        layer = put(layer, [(mx + 1, my + 2), (mx + 2, my + 2)], "p")
        layer = put(layer, [(mx + 1, my + 3), (mx + 2, my + 3)], "k")
    elif mouth == "yawn":
        layer = put(layer, [(mx, my + 1), (mx + 1, my + 1), (mx + 2, my + 1)], "k")
        layer = put(layer, [(mx, my + 2), (mx + 1, my + 2), (mx + 2, my + 2)], "p")
        layer = put(layer, [(mx + 1, my + 3), (mx + 2, my + 3)], "p")
        layer = put(layer, [(mx, my + 4), (mx + 1, my + 4), (mx + 2, my + 4)], "k")
    return layer


def flipped_vertically(layer, about_y):
    """Mirror a layer top-to-bottom around the row about_y."""
    out = blank()
    for y in range(CELL):
        src = int(round(2 * about_y - y))
        if 0 <= src < CELL:
            out[y] = layer[src][:]
    return out


def tabby_stripes(layer, top, every=4, depth=3):
    return recolour(layer, "d", lambda x, y: x % every == 1 and y <= top + depth)


def leg(hip, foot, ch, paw="l"):
    layer = blank()
    fill_stroke(layer, [hip, foot], 2.4, ch)
    fx, fy = int(foot[0]), int(foot[1])
    layer = recolour(layer, paw, lambda x, y: y >= fy and abs(x - fx) <= 1)
    return outlined(layer)


def tail(points, width=2.2):
    layer = blank()
    fill_stroke(layer, points, width, "o")
    tip = points[-1]
    layer = recolour(layer, "d", lambda x, y: math.hypot(x + 0.5 - tip[0], y + 0.5 - tip[1]) < 1.6)
    return outlined(layer)


# --- poses ------------------------------------------------------------------

def walk_frame(i, count=6):
    phase = i / count * 2 * math.pi
    bob = -1 if i % 3 == 0 else 0
    body_cy = 20.5 + bob

    def foot(hip_x, offset):
        swing = math.sin(phase + offset)
        lift = 1 if math.cos(phase + offset) > 0.5 else 0
        return (hip_x + 2.6 * swing, 29.5 - lift)

    far_back = leg((11.5, 22.5 + bob), foot(11.5, math.pi), "d", paw="d")
    far_front = leg((21.5, 22.5 + bob), foot(21.5, 0), "d", paw="d")
    near_back = leg((10.0, 22.5 + bob), foot(10.0, 0), "o")
    near_front = leg((20.0, 22.5 + bob), foot(20.0, math.pi), "o")

    body = blank()
    fill_ellipse(body, 15.5, body_cy, 8.2, 4.6, "o")
    body = tabby_stripes(body, int(body_cy - 4.6))
    body = recolour(body, "l", lambda x, y: y >= body_cy + 2.5)
    body = recolour(body, "c", lambda x, y: x >= 20 and y >= body_cy)
    body = outlined(body)

    sway = math.sin(phase) * 1.2
    tail_layer = tail([(8.5, body_cy - 1), (5.5, body_cy - 4), (4 + sway, body_cy - 8), (5 + sway, body_cy - 11)])
    return stack(tail_layer, far_back, far_front, body, near_back, near_front, head(25.0, 14.5 + bob))


def sit_frame(tail_tip=(2.5, 23.5), eyes="open", mouth=None, near_lift=0.0,
              far_lift=0.0, head_dy=0.0, head_dx=0.0):
    haunch = blank()
    fill_ellipse(haunch, 13.0, 25.0, 6.2, 5.6, "o")
    haunch = recolour(haunch, "d", lambda x, y: (x + y) % 4 == 0 and y < 26)
    haunch = outlined(haunch)

    torso = blank()
    fill_ellipse(torso, 17.5, 20.5, 4.6, 6.6, "o")
    fill_ellipse(torso, 20.4, 19.8, 1.9, 3.2, "c")  # chest
    torso = outlined(torso)

    tail_layer = tail([(8.0, 28.5), (4.5, 29.5), (2.5, 27.0), tail_tip])
    far_front = leg((18.5, 23.0), (18.5, 29.5 - far_lift), "d", paw="d")
    near_front = leg((20.5, 22.0), (20.8, 29.5 - near_lift), "o")
    face = head(21.0 + head_dx, 11.5 + head_dy, eyes, mouth)
    return stack(tail_layer, haunch, far_front, torso, near_front, face)


def curled_frame(breath, head_up=False):
    ry = 5.0 + breath * 0.7
    body = blank()
    fill_ellipse(body, 14.5, 30.6 - ry, 10.2, ry, "o")
    body = tabby_stripes(body, int(30.6 - 2 * ry), every=3, depth=2)
    body = outlined(body)

    tail_layer = tail([(5.5, 28.8), (12.0, 29.6), (19.5, 29.4)], width=2.4)
    if head_up:
        face = head(23.0, 21.0, eyes="open")
    else:
        face = head(23.0, 24.5 - breath * 0.5, eyes="closed")
    return stack(body, tail_layer, face)


def loaf_frame():
    body = blank()
    fill_ellipse(body, 14.0, 25.6, 9.0, 5.0, "o")
    body = tabby_stripes(body, 21, every=3, depth=2)
    body = recolour(body, "c", lambda x, y: x >= 20)
    body = outlined(body)
    paws = blank()
    fill_stroke(paws, [(18.5, 29.5), (23.5, 29.5)], 2.0, "l")
    paws = outlined(paws)
    tail_layer = tail([(5.5, 28.5), (2.5, 29.5), (1.8, 27.5)], width=2.0)
    return stack(tail_layer, body, paws, head(22.0, 18.5))


def crouch_frame(wiggle=0):
    """Low hunting crouch, rear end wiggling before a pounce."""
    haunch = blank()
    fill_ellipse(haunch, 10.0 + wiggle, 25.5, 5.0, 4.6, "o")
    haunch = recolour(haunch, "d", lambda x, y: (x + y) % 4 == 0 and y < 26)
    haunch = outlined(haunch)
    body = blank()
    fill_ellipse(body, 16.5, 27.0, 8.0, 3.4, "o")
    body = recolour(body, "c", lambda x, y: x >= 20 and y >= 27)
    body = outlined(body)
    paws = blank()
    fill_stroke(paws, [(21.5, 29.6), (26.5, 29.6)], 2.0, "l")
    paws = outlined(paws)
    tail_layer = tail([(5.5 + wiggle, 26.0), (2.5, 27.5 - wiggle), (0.9, 28.5 - wiggle)], width=2.0)
    return stack(tail_layer, haunch, body, paws, head(25.5, 23.5))


def pounce_launch_frame():
    back = leg((10.0, 22.0), (5.5, 29.5), "o")
    far_back = leg((11.5, 22.0), (7.5, 29.5), "d", paw="d")
    front = leg((21.0, 18.0), (26.5, 15.5), "o")
    body = blank()
    fill_ellipse(body, 15.5, 19.5, 8.8, 3.9, "o")
    body = tabby_stripes(body, 16, depth=2)
    body = outlined(body)
    tail_layer = tail([(8.0, 19.0), (4.0, 18.0), (1.2, 17.5)], width=2.0)
    return stack(tail_layer, far_back, body, back, front, head(24.5, 13.0))


def pounce_air_frame():
    back = leg((9.0, 18.5), (3.5, 21.5), "o")
    front = leg((22.0, 18.5), (28.5, 20.0), "o")
    body = blank()
    fill_ellipse(body, 15.5, 17.0, 9.5, 3.7, "o")
    body = tabby_stripes(body, 14, depth=2)
    body = outlined(body)
    tail_layer = tail([(7.0, 16.5), (3.0, 15.0), (0.9, 13.5)], width=2.0)
    return stack(tail_layer, body, back, front, head(25.0, 13.0))


def side_lying_frame():
    legs = blank()
    fill_stroke(legs, [(17.0, 28.0), (22.0, 29.6)], 2.2, "o")
    fill_stroke(legs, [(10.0, 28.0), (15.0, 29.6)], 2.2, "o")
    legs = recolour(legs, "l", lambda x, y: x in (14, 15, 21, 22) and y >= 29)
    legs = outlined(legs)
    body = blank()
    fill_ellipse(body, 14.0, 26.0, 9.8, 4.2, "o")
    body = tabby_stripes(body, 22, depth=2)
    body = recolour(body, "c", lambda x, y: y >= 28)
    body = outlined(body)
    tail_layer = tail([(5.0, 28.0), (1.5, 29.5)], width=2.0)
    return stack(tail_layer, body, legs, head(24.0, 25.0))


def belly_up_frame(kick=0):
    paws = blank()
    for hip_x, tip_dx in ((10.5, -1), (13.5, 0), (17.5, 1), (20.5, 0)):
        fill_stroke(paws, [(hip_x, 24.0), (hip_x + tip_dx + kick, 19.5)], 2.2, "o")
    paws = recolour(paws, "l", lambda x, y: y <= 19)
    paws = outlined(paws)
    body = blank()
    fill_ellipse(body, 15.0, 27.0, 9.5, 4.2, "o")
    body = recolour(body, "c", lambda x, y: y <= 26)
    body = outlined(body)
    face = flipped_vertically(head(24.5, 21.0, eyes="happy"), 24.5)
    HEAD_LOG[-1] = (24.5, 28.0, False)  # upside down: no hats
    tail_layer = tail([(6.0, 29.0), (2.5, 29.5), (1.2, 27.5 + kick)], width=2.0)
    return stack(paws, tail_layer, body, face)


def dangle_frame(swing=0):
    """Held up by the scruff: hanging straight down, legs dangling."""
    legs = blank()
    fill_stroke(legs, [(17.5, 21.0), (18.0 + swing, 27.5)], 2.2, "o")
    fill_stroke(legs, [(13.5, 23.0), (13.0 + swing, 29.5)], 2.2, "o")
    legs = recolour(legs, "l", lambda x, y: y >= 27 and x >= 15 or y >= 29)
    legs = outlined(legs)
    body = blank()
    fill_ellipse(body, 15.5, 17.5, 4.2, 7.2, "o")
    body = recolour(body, "c", lambda x, y: x >= 17)
    body = outlined(body)
    tail_layer = tail([(13.0, 22.0), (11.0 - swing, 27.0), (10.5 - swing, 30.5)], width=2.0)
    return stack(tail_layer, body, legs, head(16.5, 7.5, eyes="closed"))


HEART_BIG = [".rr.rr.", "rrrrrrr", "rrrrrrr", ".rrrrr.", "..rrr..", "...r..."]
HEART_SMALL = [".r.r.", "rrrrr", ".rrr.", "..r.."]


ALERT = ["ww", "ww", "ww", "ww", "..", "ww"]


def alert_frame(rise=0):
    """Effect sprite: an exclamation mark in the top-left of the cell."""
    layer = blank()
    for y, row in enumerate(ALERT):
        for x, ch in enumerate(row):
            if ch != ".":
                layer[y + 2 - rise][x + 1] = ch
    return outlined(layer)


def heart_frame(rows):
    """Effect sprite: a heart in the top-left of the cell."""
    layer = blank()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                layer[y + 1][x + 1] = ch
    return outlined(layer)


def sit_parts(tail_tip=(2.5, 23.5)):
    """Haunch, torso, tail and far front leg of the sitting pose."""
    haunch = blank()
    fill_ellipse(haunch, 13.0, 25.0, 6.2, 5.6, "o")
    haunch = recolour(haunch, "d", lambda x, y: (x + y) % 4 == 0 and y < 26)
    haunch = outlined(haunch)
    torso = blank()
    fill_ellipse(torso, 17.5, 20.5, 4.6, 6.6, "o")
    fill_ellipse(torso, 20.4, 19.8, 1.9, 3.2, "c")
    torso = outlined(torso)
    tail_layer = tail([(8.0, 28.5), (4.5, 29.5), (2.5, 27.0), tail_tip])
    far_front = leg((18.5, 23.0), (18.5, 29.5), "d", paw="d")
    return tail_layer, haunch, torso, far_front


def thick_body(points, width):
    body = blank()
    fill_stroke(body, points, width, "o")
    top = min(y for y in range(CELL) if any(c != "." for c in body[y]))
    body = recolour(body, "d", lambda x, y: x % 4 == 1 and y <= top + 2)
    return outlined(body)


def stretch_frame(full):
    """Front stretch: chest down, rear up, front legs reaching forward."""
    if full:
        body = thick_body([(10.0, 19.5), (20.5, 25.0)], 7.0)
        rear = [leg((9.0, 22.0), (8.5, 29.5), "o"), leg((11.0, 22.0), (11.0, 29.5), "d", paw="d")]
        front = [leg((18.0, 26.5), (25.0, 29.6), "d", paw="d"), leg((19.5, 26.5), (27.5, 29.6), "o")]
        tail_layer = tail([(7.5, 17.5), (5.5, 12.5), (6.5, 8.5)])
        face = head(25.0, 23.5)
    else:
        body = thick_body([(10.0, 21.0), (19.5, 23.5)], 8.0)
        rear = [leg((9.5, 24.0), (9.5, 29.5), "o"), leg((11.5, 24.0), (11.5, 29.5), "d", paw="d")]
        front = [leg((18.0, 25.0), (22.0, 29.6), "d", paw="d"), leg((19.5, 25.0), (24.0, 29.6), "o")]
        tail_layer = tail([(7.0, 20.5), (3.5, 19.0), (2.5, 15.0)])
        face = head(23.5, 18.0)
    return stack(tail_layer, rear[1], front[0], body, rear[0], front[1], face)


def groom_frame(stage):
    """Licking a raised front paw (stages 0, 1) or wiping the face (stage 2)."""
    tail_layer, haunch, torso, far_front = sit_parts()
    paw = [(23.5, 14.5), (23.5, 14.5), (23.0, 10.5)][stage]
    face = head(21.5, 12.5, eyes="closed")
    near_front = leg((20.5, 21.5), paw, "o")
    frame = stack(tail_layer, haunch, far_front, torso, face, near_front)
    if stage == 0:
        frame = put(frame, [(int(paw[0]) + 1, int(paw[1]) - 1)], "p")  # tongue
    return frame


def scratch_frame(k):
    """Sitting, scratching behind the ear with a back leg."""
    tail_layer, haunch, torso, far_front = sit_parts()
    near_front = leg((20.5, 22.0), (20.8, 29.5), "o")
    face = head(21.5, 12.0, eyes="happy")
    back_leg = leg((12.5, 24.0), (17.0 + k, 10.5 - k), "o")
    return stack(tail_layer, haunch, far_front, torso, near_front, face, back_leg)


Z_BIG = ["wwwww", "...ww", "..ww.", ".ww..", "wwwww"]
Z_SMALL = ["wwww", "..ww", ".ww.", "wwww"]


def glyph_frame(rows):
    """Effect sprite: a glyph in the top-left of the cell."""
    layer = blank()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                layer[y + 1][x + 1] = ch
    return outlined(layer)


def bat_frame(stage):
    """Sitting and swiping a front paw at something on the floor."""
    tail_layer, haunch, torso, far_front = sit_parts()
    paw = [(25.0, 21.0), (26.5, 28.5), (23.0, 27.0)][stage]
    face = head(21.5, 12.0)
    near_front = leg((20.5, 21.5), paw, "o")
    return stack(tail_layer, haunch, far_front, torso, face, near_front)


BUTTERFLY_OPEN = ["pp...pp", "ppp.ppp", ".ppbpp.", "..pbp..", ".pp.pp."]
BUTTERFLY_SHUT = ["...b...", "..pbp..", "..pbp..", "..pbp..", "...b..."]


def toy_glyph(rows, left, top):
    layer = blank()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != ".":
                layer[top + y][left + x] = ch
    return outlined(layer)


def butterfly_frame(open_wings):
    """Drawn low in the cell; the mod lifts it into the air."""
    return toy_glyph(BUTTERFLY_OPEN if open_wings else BUTTERFLY_SHUT, 13, 25)


def yarn_frame(turn):
    layer = blank()
    fill_ellipse(layer, 16.0, 27.5, 3.4, 3.4, "r")
    strands = [[(14, 26), (15, 27), (16, 28), (17, 29)], [(18, 26), (17, 27), (16, 28), (15, 29)]]
    layer = put(layer, strands[turn], "w")
    fill_stroke(layer, [(12.8, 30.0), (9.5, 30.3), (7.5, 29.6)], 1.0, "r")  # loose thread
    return outlined(layer)


def gift_frame():
    """A little grey toy mouse."""
    layer = blank()
    fill_stroke(layer, [(12.0, 29.6), (9.5, 29.8), (8.0, 28.5)], 1.0, "p")  # tail
    fill_ellipse(layer, 16.5, 28.5, 4.2, 2.4, "y")
    fill_ellipse(layer, 18.5, 26.2, 1.4, 1.4, "p")  # ear
    layer = put(layer, [(19, 28)], "b")
    layer = put(layer, [(20, 29)], "p")
    return outlined(layer)


def dance_frame(step):
    """Sitting and bobbing to music: head and tail sway together."""
    sway = [-1.0, 0.0, 1.0, 0.0][step]
    tips = [(1.0, 21.5), (2.5, 21.0), (4.0, 21.5), (2.5, 21.0)]
    return sit_frame(tips[step], eyes="happy", head_dx=sway, head_dy=abs(sway) * 0.6,
                     near_lift=1.0 if step == 1 else 0.0)


NOTE = ["..ww.", "..w.w", "..w..", "www..", "www.."]
FISH_BUBBLE = [
    "..wwwwwww..",
    ".wwwwwwwww.",
    "wwllllwlwww",
    "wlbllllwwww",
    "wwllllwlwww",
    ".wwwwwwwww.",
    "..wwwwwww..",
    "..ww.......",
    ".w.........",
]


def hat_frame(kind):
    """Hats are drawn with the brim's centre at (16, 12); the mod lines that
    point up with the top of the head."""
    layer = blank()
    if kind == "witch":
        fill_triangle(layer, (12.6, 11.5), (19.4, 11.5), (16.6, 3.0), "v")
        fill_stroke(layer, [(16.6, 3.6), (19.6, 2.6)], 1.2, "v")
        fill_stroke(layer, [(10.8, 12.0), (21.2, 12.0)], 1.4, "v")
        layer = recolour(layer, "o", lambda x, y: y in (9, 10) and 13 <= x <= 19)
    elif kind == "santa":
        fill_triangle(layer, (12.6, 11.0), (19.4, 11.0), (19.0, 3.6), "r")
        fill_stroke(layer, [(11.6, 11.6), (20.4, 11.6)], 2.0, "w")
        fill_ellipse(layer, 19.6, 3.4, 1.5, 1.5, "w")
    else:  # party
        fill_triangle(layer, (13.2, 12.0), (18.8, 12.0), (16.0, 3.2), "l")
        layer = recolour(layer, "p", lambda x, y: (x + y) % 3 == 0)
        fill_ellipse(layer, 16.0, 2.8, 1.3, 1.3, "w")
    return outlined(layer)


# --- sheet ------------------------------------------------------------------

def framed(thunk):
    """Draws one frame and returns it with its head anchor (or None)."""
    HEAD_LOG.clear()
    frame = thunk()
    return frame, (HEAD_LOG[-1] if HEAD_LOG else None)


def F(fn, *args, **kwargs):
    """A frame to draw later, so its head anchor can be captured."""
    return lambda: fn(*args, **kwargs)


def animation_specs():
    """(name, ms per frame, loops, [frames to draw])."""
    sit_tail_tips = [(2.5, 23.5), (0.9, 21.0), (2.5, 19.6), (3.8, 22.0)]
    return [
        ("walk", 110, True, [F(walk_frame, i) for i in range(6)]),
        ("sit", 1000, True, [F(sit_frame)]),
        ("sit_tail", 150, False, [F(sit_frame, tip) for tip in sit_tail_tips]),
        ("sleep", 900, True, [F(curled_frame, 0), F(curled_frame, 1)]),
        ("wake", 260, False, [F(curled_frame, 0, head_up=True), F(loaf_frame), F(sit_frame)]),
        ("loaf", 1000, True, [F(loaf_frame)]),
        ("crouch", 150, True, [F(crouch_frame, 0), F(crouch_frame, 1)]),
        ("pounce", 130, False, [F(pounce_launch_frame), F(pounce_air_frame), F(crouch_frame, 0)]),
        ("happy", 350, True, [F(sit_frame, eyes="happy"), F(sit_frame, (1.5, 22.5), eyes="happy")]),
        ("meow", 160, False, [F(sit_frame, mouth="small"),
                              F(sit_frame, eyes="closed", mouth="wide"),
                              F(sit_frame, mouth="small")]),
        ("roll", 220, False, [F(side_lying_frame), F(belly_up_frame, 0)]),
        ("belly", 320, True, [F(belly_up_frame, 0), F(belly_up_frame, 1)]),
        ("dangle", 260, True, [F(dangle_frame, 0), F(dangle_frame, 1)]),
        ("heart", 250, True, [F(heart_frame, HEART_BIG), F(heart_frame, HEART_SMALL)]),
        ("alert", 200, True, [F(alert_frame, 0), F(alert_frame, 1)]),
        ("stretch", 230, False, [F(stretch_frame, False), F(stretch_frame, True),
                                 F(stretch_frame, True), F(stretch_frame, True),
                                 F(stretch_frame, False)]),
        ("yawn", 200, False, [F(sit_frame, eyes="closed", mouth="small", head_dy=-0.6),
                              F(sit_frame, eyes="closed", mouth="yawn", head_dy=-1.2),
                              F(sit_frame, eyes="closed", mouth="yawn", head_dy=-1.2),
                              F(sit_frame, eyes="closed", mouth="small", head_dy=-0.6)]),
        ("groom", 240, True, [F(groom_frame, 0), F(groom_frame, 1), F(groom_frame, 0),
                              F(groom_frame, 2)]),
        ("scratch", 90, True, [F(scratch_frame, 0), F(scratch_frame, 1)]),
        ("knead", 260, True, [F(sit_frame, eyes="happy", near_lift=2.0),
                              F(sit_frame, eyes="happy", far_lift=2.0)]),
        ("zzz", 600, True, [F(glyph_frame, Z_BIG), F(glyph_frame, Z_SMALL)]),
        ("bat", 110, False, [F(bat_frame, 0), F(bat_frame, 1), F(bat_frame, 2)]),
        ("butterfly", 120, True, [F(butterfly_frame, True), F(butterfly_frame, False)]),
        ("yarn", 1000, True, [F(yarn_frame, 0), F(yarn_frame, 1)]),
        ("gift", 1000, True, [F(gift_frame)]),
        ("dance", 300, True, [F(dance_frame, i) for i in range(4)]),
        ("note", 400, True, [F(glyph_frame, NOTE)]),
        ("fish", 1000, True, [F(glyph_frame, FISH_BUBBLE)]),
        ("hat_witch", 1000, True, [F(hat_frame, "witch")]),
        ("hat_santa", 1000, True, [F(hat_frame, "santa")]),
        ("hat_party", 1000, True, [F(hat_frame, "party")]),
    ]


def build_animations():
    return [(name, ms, loop, [framed(f) for f in frames])
            for name, ms, loop, frames in animation_specs()]


def to_image(rows_of_frames, width_frames):
    img = Image.new("RGBA", (width_frames * CELL, len(rows_of_frames) * CELL), (0, 0, 0, 0))
    for row, frames in enumerate(rows_of_frames):
        for col, frame in enumerate(frames):
            for y in range(CELL):
                for x in range(CELL):
                    if frame[y][x] != ".":
                        img.putpixel((col * CELL + x, row * CELL + y), COLOURS[frame[y][x]])
    return img


def write_preview(sheet):
    backdrop = Image.new("RGBA", sheet.size, (32, 32, 40, 255))
    backdrop.alpha_composite(sheet)
    size = (sheet.width * PREVIEW_SCALE, sheet.height * PREVIEW_SCALE)
    backdrop.resize(size, Image.NEAREST).save(PREVIEW)


def anchor_json(anchor):
    """[x, y, upright] in whole pixels, or null for frames without a head."""
    if anchor is None:
        return None
    x, y, upright = anchor
    return [int(round(x)), int(round(y)), upright]


def main():
    animations = build_animations()
    width = max(len(frames) for _, _, _, frames in animations)
    sheet = to_image([[f for f, _ in frames] for _, _, _, frames in animations], width)
    SHEET.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(SHEET)
    meta = [{"name": n, "frames": len(f), "ms": ms, "loop": loop,
             "anchors": [anchor_json(a) for _, a in f]}
            for n, ms, loop, f in animations]
    ANIMATIONS.write_text(json.dumps(meta, indent=2) + "\n")
    write_preview(sheet)
    print(f"wrote {SHEET.name}, {ANIMATIONS.name}, {PREVIEW.name}")


if __name__ == "__main__":
    main()
