"""Render a demo GIF of the cat on a taskbar-like strip, for the mod's readme.

Uses the real sprite sheet and head anchors, so it shows exactly the art the
mod draws. Writes art/preview.gif.

Usage: python tools/make_preview_gif.py
"""

import json
import math
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SHEET = Image.open(ROOT / "art" / "cat_sheet.png").convert("RGBA")
ANIMS = {a["name"]: (i, a) for i, a in enumerate(json.loads((ROOT / "art" / "animations.json").read_text()))}
OUT = ROOT / "art" / "preview.gif"

CELL = 32
WIDTH = 360          # strip width in sprite pixels
HEIGHT = 56          # cat (32) + headroom for effects
BAR = 40             # taskbar height
SCALE = 2
STEP_MS = 100        # GIF frame time
BACKGROUND = (60, 66, 82, 255)
TASKBAR = (32, 32, 38, 255)


def cell(name, frame):
    row, _ = ANIMS[name]
    return SHEET.crop((frame * CELL, row * CELL, frame * CELL + CELL, row * CELL + CELL))


def frame_index(name, t_ms, reverse=False):
    _, a = ANIMS[name]
    step = int(t_ms // a["ms"])
    i = step % a["frames"] if a["loop"] else min(step, a["frames"] - 1)
    return a["frames"] - 1 - i if reverse else i


def anchor(name, i):
    _, a = ANIMS[name]
    found = a["anchors"][i]
    return found if found else [16, 8, False]


class Scene:
    def __init__(self):
        self.frames = []

    def shot(self, name, t, x, facing=1, lift=0, reverse=False, effect=None, effect_t=0,
             hat=None, toy=None):
        img = Image.new("RGBA", (WIDTH, HEIGHT), BACKGROUND)
        img.paste(TASKBAR, (0, HEIGHT - BAR, WIDTH, HEIGHT))
        floor = HEIGHT - CELL
        if toy:
            toy_name, toy_frame, toy_x, toy_lift, toy_mirror = toy
            sprite = cell(toy_name, toy_frame)
            if toy_mirror:
                sprite = sprite.transpose(Image.FLIP_LEFT_RIGHT)
            img.alpha_composite(sprite, (int(toy_x), int(floor - toy_lift)))
        i = frame_index(name, t, reverse)
        sprite = cell(name, i).copy()
        ax, ay, upright = anchor(name, i)
        if hat and upright:
            sprite_big = Image.new("RGBA", (CELL, CELL + 16), (0, 0, 0, 0))
            sprite_big.alpha_composite(sprite, (0, 16))
            sprite_big.alpha_composite(cell(hat, 0), (ax - 16, ay - 11 + 16))
            sprite = sprite_big
            top = floor - 16
        else:
            top = floor
        if facing < 0:
            sprite = sprite.transpose(Image.FLIP_LEFT_RIGHT)
            ax = CELL - 1 - ax
        img.alpha_composite(sprite, (int(x), int(top - lift)))
        if effect:
            head_x, head_y = int(x) + ax, floor + ay - int(lift)
            if effect == "heart":
                rise = int((effect_t % 1500) * 8 / 1500)
                img.alpha_composite(cell("heart", frame_index("heart", effect_t)),
                                    (head_x - 5, head_y - 9 - rise))
            elif effect == "zzz":
                rise = int((effect_t % 2400) * 9 / 2400)
                img.alpha_composite(cell("zzz", frame_index("zzz", effect_t)),
                                    (head_x + facing * (2 + rise // 3) - 3, head_y - 7 - rise))
            elif effect == "note":
                rise = int((effect_t % 1500) * 8 / 1500)
                sway = (effect_t // 400) % 2 * 2 - 1
                img.alpha_composite(cell("note", 0), (head_x - facing * 6 + sway - 3, head_y - 9 - rise))
            elif effect == "alert":
                img.alpha_composite(cell("alert", frame_index("alert", effect_t)),
                                    (head_x + 3 * facing - 2, head_y - 10))
        self.frames.append(img.resize((WIDTH * SCALE, HEIGHT * SCALE), Image.NEAREST))

    def hold(self, name, ms, x, **kw):
        for t in range(0, ms, STEP_MS):
            self.shot(name, t, x, effect_t=t, **kw)


def build():
    s = Scene()
    x = 20.0
    # Stroll in, then sit with a tail flick.
    for t in range(0, 2600, STEP_MS):
        s.shot("walk", t, x)
        x += 60 * STEP_MS / 1000
    s.hold("sit_tail", 600, x)
    s.hold("sit", 500, x)
    # Groom, then purr with hearts (you're hovering).
    s.hold("groom", 1900, x)
    s.hold("happy", 2000, x, effect="heart")
    # A butterfly drifts in; crouch, wiggle, pounce, catch it, proud meow.
    bx = WIDTH - 10.0
    for t in range(0, 2400, STEP_MS):
        bob = 6 + 3 * math.sin(t / 1300 * 2 * math.pi)
        name = "sit" if t < 1000 else "crouch"
        s.shot(name, t, x, toy=("butterfly", frame_index("butterfly", t), bx, bob, True),
               effect="alert" if t < 1000 else None, effect_t=t)
        bx -= 30 * STEP_MS / 1000
    start, target = x, bx - 8
    for t in range(0, 400, STEP_MS):
        k = t / 390
        lift = 4 * 12 * k * (1 - k)
        s.shot("pounce", t, start + (target - start) * k, lift=lift,
               toy=("butterfly", frame_index("butterfly", t), bx, 6, True))
    x = target
    s.hold("crouch", 300, x)
    s.hold("meow", 480, x, effect="heart")
    # Music comes on: dance with notes.
    s.hold("dance", 2700, x, effect="note")
    # Bedtime: lie down, sleep with Zzz.
    s.hold("wake", 780, x, reverse=True)
    s.hold("sleep", 3000, x, effect="zzz")
    # Wake up in October: witch hat, stretch, yawn.
    s.hold("wake", 780, x, hat="hat_witch")
    s.hold("stretch", 1150, x, hat="hat_witch")
    s.hold("yawn", 800, x, hat="hat_witch")
    s.hold("sit", 900, x, hat="hat_witch")
    return s.frames


def main():
    frames = build()
    frames[0].save(OUT, save_all=True, append_images=frames[1:], duration=STEP_MS, loop=0,
                   disposal=2, optimize=True)
    print(f"wrote {OUT.name}: {len(frames)} frames, {OUT.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
