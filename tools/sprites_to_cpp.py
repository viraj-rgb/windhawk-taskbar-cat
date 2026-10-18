"""Convert the cat sprite sheet into the generated block inside the mod.

Reads art/cat_sheet.png (32x32 cells, one row per animation) and
art/animations.json, then rewrites everything between the
BEGIN/END GENERATED SPRITES markers in taskbar-cat.wh.cpp.

Every pixel must be one of the PALETTE colours (or fully transparent).
Frames are run-length encoded: one byte per run, high nibble = run length - 1,
low nibble = palette index.

Usage: python tools/sprites_to_cpp.py
"""

import json
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SHEET = ROOT / "art" / "cat_sheet.png"
ANIMATIONS = ROOT / "art" / "animations.json"
MOD = ROOT / "taskbar-cat.wh.cpp"
BEGIN = "// BEGIN GENERATED SPRITES"
END = "// END GENERATED SPRITES"

CELL = 32
MAX_RUN = 16
DEFAULT_ANCHOR = (16, 8)  # frames without a head (effects), or hand-drawn ones

# Single source of truth for the cat's colours. Index 0 must be transparent.
# (key, ARGB, name)
PALETTE = [
    (".", 0x00000000, "transparent"),
    ("k", 0xFF3B2314, "outline"),
    ("o", 0xFFF28C28, "orange body"),
    ("d", 0xFFC8641A, "dark orange stripes"),
    ("l", 0xFFF8B25A, "light orange"),
    ("c", 0xFFFDE6C4, "cream"),
    ("p", 0xFFF4A0A8, "pink"),
    ("b", 0xFF1A1A1A, "pupil"),
    ("w", 0xFFFFFFFF, "eye shine"),
    ("g", 0xFF6BBF59, "eye green"),
    ("r", 0xFFE8475B, "heart red"),
    ("y", 0xFF9A9AA6, "toy grey"),
    ("v", 0xFF7A4FB5, "witch purple"),
]


def argb_to_rgba(argb):
    return ((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, argb >> 24)


def pixel_to_index(rgba, lookup, where):
    if rgba[3] == 0:
        return 0
    if rgba not in lookup:
        sys.exit(f"error: colour {rgba} at {where} is not in the palette")
    return lookup[rgba]


def encode_rle(indices):
    out = []
    i = 0
    while i < len(indices):
        run = 1
        while i + run < len(indices) and run < MAX_RUN and indices[i + run] == indices[i]:
            run += 1
        out.append(((run - 1) << 4) | indices[i])
        i += run
    return out


def decode_rle(data):
    pixels = []
    for byte in data:
        pixels.extend([byte & 0x0F] * ((byte >> 4) + 1))
    return pixels


def read_frames(sheet, animations):
    lookup = {argb_to_rgba(argb): i for i, (_, argb, _) in enumerate(PALETTE) if i}
    frames = []
    for row, anim in enumerate(animations):
        for col in range(anim["frames"]):
            indices = []
            for y in range(CELL):
                for x in range(CELL):
                    px, py = col * CELL + x, row * CELL + y
                    where = f"{anim['name']} frame {col} ({x},{y})"
                    indices.append(pixel_to_index(sheet.getpixel((px, py)), lookup, where))
            frames.append(indices)
    return frames


def camel(name):
    return "".join(part.capitalize() for part in name.split("_"))


def render_block(frames, animations):
    data, offsets = [], []
    for indices in frames:
        encoded = encode_rle(indices)
        if decode_rle(encoded) != indices:
            sys.exit("error: RLE round trip failed")
        offsets.append(len(data))
        data.extend(encoded)
    offsets.append(len(data))

    lines = [BEGIN + " (tools/sprites_to_cpp.py, do not edit by hand)", "namespace sprites {", ""]
    lines.append("constexpr uint32_t kPalette[] = {")
    for _, argb, name in PALETTE:
        lines.append(f"    0x{argb:08X},  // {name}")
    lines.append("};")
    lines.append("")
    lines.append(f"constexpr int kFrameCount = {len(frames)};")
    lines.append("")
    lines.append("// One past the last frame is the end of the data.")
    lines.append("constexpr uint16_t kFrameOffsets[] = {")
    for i in range(0, len(offsets), 12):
        lines.append("    " + ", ".join(str(o) for o in offsets[i:i + 12]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("constexpr uint8_t kFrameData[] = {")
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("enum Anim {")
    for anim in animations:
        lines.append(f"    k{camel(anim['name'])},")
    lines.append("    kAnimCount,")
    lines.append("};")
    lines.append("")
    lines.append("// Top of the head in each frame (sprite pixels), for effects and hats.")
    lines.append("constexpr cat::Anchor kFrameAnchors[kFrameCount] = {")
    for anim in animations:
        anchors = anim.get("anchors") or [None] * anim["frames"]
        for a in anchors:
            if a is None:
                lines.append(f"    {{{DEFAULT_ANCHOR[0]}, {DEFAULT_ANCHOR[1]}, false}},  // {anim['name']}")
            else:
                upright = "true" if a[2] else "false"
                lines.append(f"    {{{a[0]}, {a[1]}, {upright}}},  // {anim['name']}")
    lines.append("};")
    lines.append("")
    lines.append("// first frame, frame count, ms per frame, loops")
    lines.append("constexpr cat::AnimInfo kAnims[kAnimCount] = {")
    first = 0
    for anim in animations:
        loop = "true" if anim["loop"] else "false"
        lines.append(f"    {{{first}, {anim['frames']}, {anim['ms']}, {loop}}},  // {anim['name']}")
        first += anim["frames"]
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace sprites")
    lines.append(END)
    return "\n".join(lines)


def main():
    animations = json.loads(ANIMATIONS.read_text())
    sheet = Image.open(SHEET).convert("RGBA")
    frames = read_frames(sheet, animations)
    block = render_block(frames, animations)

    source = MOD.read_text(encoding="utf-8")
    start, end = source.find(BEGIN), source.find(END)
    if start < 0 or end < 0:
        sys.exit(f"error: markers not found in {MOD.name}")
    end += len(END)
    MOD.write_text(source[:start] + block + source[end:], encoding="utf-8", newline="\n")
    size = sum(len(encode_rle(f)) for f in frames)
    print(f"wrote {len(frames)} frames ({size} bytes) into {MOD.name}")


if __name__ == "__main__":
    main()
