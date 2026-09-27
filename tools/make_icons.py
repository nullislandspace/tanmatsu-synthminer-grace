#!/usr/bin/env python3
"""Generate SynthMiner's application icons into metadata/.

The launcher puts one of these beside the app's name; metadata.json
names all three sizes and the launcher picks by how much room it has.
They are generated, like the block textures, so what is committed can be
re-derived from the numbers in this file instead of from a lost .xcf:

    python3 tools/make_icons.py            # metadata/icon{16,32,64}.png
    python3 tools/make_icons.py --preview  # also a magnified sheet

The subject is the game's iron pickaxe, and the backdrop is the game's
stone -- imported from make_textures.py rather than copied, so neither
can drift away from what the player actually sees.

WHY IT IS DRAWN ON A 16-UNIT GRID. The three icons are one picture at
three scales, not three drawings that will disagree after the first
edit: every coordinate below is in sixteenths of the icon and multiplied
by 1, 2 or 4 on the way out. The larger two then get detail that has
nowhere to go at 16 -- grain along the handle, a bevel on the head --
and the smallest gets the dark outline it needs to survive being an
inch wide next to text.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_textures import sm_stone                                    # noqa: E402

SIZES = (16, 32, 64)
OUT = Path(__file__).resolve().parent.parent / "metadata"

IRON = (222, 222, 228)          # the same iron as item_pickaxe_iron.png
WOOD = (138, 98, 56)            # the same handle every tool in the game has
OUTLINE = (20, 18, 24)


def _shade(rgb, d):
    return tuple(int(max(0, min(255, c + d))) for c in rgb)


def _band(img, s, x0, y0, x1, y1, rgb):
    """Fill a rectangle given in grid units. Coordinates may be halves;
    at s == 1 they land on whole texels, which is the point of choosing
    a 16-unit grid for a 16-pixel icon."""
    y = slice(int(round(y0 * s)), int(round(y1 * s)))
    x = slice(int(round(x0 * s)), int(round(x1 * s)))
    img[y, x, :3] = rgb
    img[y, x, 3] = 255


# The head, by columns rather than rows: x0, x1, y0, y1. A pickaxe head
# is a crescent -- thickest and highest in the middle, curving down to a
# point at each end. Drawn as a flat bar with two teeth under it (which
# is the cheaper shape, and what a first attempt gives you) the icon
# reads as a table with a stick leaning on it.
HEAD_COLS = (
    (2.25, 3.25, 3.6, 6.5),
    (3.25, 4.75, 3.0, 5.9),
    (4.75, 6.50, 2.4, 4.9),
    (6.50, 9.50, 1.8, 4.1),
    (9.50, 11.25, 2.4, 4.9),
    (11.25, 12.75, 3.0, 5.9),
    (12.75, 13.75, 3.6, 6.5),
)

# The shaft, as a stepped diagonal: top end, bottom end, width, steps.
# It stops under the middle of the head, not beside it -- a shaft that
# passes the head turns the whole icon into a figure 7.
SHAFT_TOP = (7.0, 3.2)
SHAFT_BOTTOM = (1.5, 14.0)
SHAFT_W = 2.5
SHAFT_STEPS = 11


def _shaft_steps():
    x0, y0 = SHAFT_TOP
    x1, y1 = SHAFT_BOTTOM
    for i in range(SHAFT_STEPS):
        t = i / (SHAFT_STEPS - 1)
        yield x0 + (x1 - x0) * t, y0 + (y1 - y0) * t


def _shaft(img, s):
    """Drawn before the head, which stands in front of it: cheaper than
    cutting a socket out of the head afterwards, and it cannot go wrong.
    The grain belongs here for the same reason -- painted after the head
    it runs straight across it, which is what the first version did."""
    dy = (SHAFT_BOTTOM[1] - SHAFT_TOP[1]) / (SHAFT_STEPS - 1)
    for x, y in _shaft_steps():
        _band(img, s, x, y, x + SHAFT_W, y + dy, WOOD)
        _band(img, s, x + SHAFT_W - 0.75, y, x + SHAFT_W, y + dy, _shade(WOOD, -34))
        if s >= 2:                                    # grain: no room at 16
            _band(img, s, x + 0.25, y + 0.2, x + SHAFT_W - 1.0, y + dy * 0.5,
                  _shade(WOOD, 26))


def _head(img, s):
    for x0, x1, y0, y1 in HEAD_COLS:
        _band(img, s, x0, y0, x1, y1, IRON)
    for x0, x1, y0, y1 in HEAD_COLS:                  # lit along the top,
        _band(img, s, x0, y0, x1, y0 + 0.4, _shade(IRON, 20))
        _band(img, s, x0, y1 - 0.5, x1, y1, _shade(IRON, -36))   # dark underneath
    for x0, x1, y0, y1 in HEAD_COLS[4:]:              # the far side turned away
        _band(img, s, x0, y0 + 0.4, x1, y1 - 0.5, _shade(IRON, -14))
    if s >= 2:
        _band(img, s, 5.0, 2.5, 7.5, 3.1, _shade(IRON, 30))      # the glint
    if s >= 4:
        _band(img, s, 12.75, 4.2, 13.75, 5.6, _shade(IRON, -50))
        _band(img, s, 2.25, 4.2, 3.25, 5.6, _shade(IRON, 12))


def _pickaxe(img, s):
    _shaft(img, s)
    _head(img, s)


def _outline(img, s):
    """A one-pixel dark edge everywhere the sprite meets the background,
    which keeps the tool crisp against the stone's grain.

    Only above 16, where the ring is a sixteenth of the icon: there it
    merges with the dark background instead of separating from it, and
    thickens the crescent back into the flat slab this drawing exists to
    avoid. The background is dark enough at that size to do the job on
    its own."""
    solid = img[:, :, 3] > 0
    grown = np.zeros_like(solid)
    for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)):
        grown |= np.roll(np.roll(solid, dy, 0), dx, 1)
    ring = grown & ~solid
    img[ring, :3] = OUTLINE
    img[ring, 3] = 255


def _background(size, s):
    """The game's stone, darkened, with the corners dimmed further. Dark
    so the iron reads; still stone, so the icon says what the game is."""
    stone = sm_stone().astype(float)
    bg = np.repeat(np.repeat(stone, s, axis=0), s, axis=1) * 0.34

    ys, xs = np.mgrid[0:size, 0:size] + 0.5
    r = np.hypot(xs - size / 2, ys - size / 2) / (size / 2)
    bg *= np.clip(1.18 - 0.42 * r * r, 0.0, 1.2)[:, :, None]

    out = np.zeros((size, size, 4), np.uint8)
    out[:, :, :3] = np.clip(bg, 0, 255).astype(np.uint8)
    out[:, :, 3] = 255
    out[0, :, :3] = out[-1, :, :3] = out[:, 0, :3] = out[:, -1, :3] = (14, 13, 16)
    return out


def icon(size):
    s = size // 16
    sprite = np.zeros((size, size, 4), np.uint8)
    _pickaxe(sprite, s)
    if s >= 2:
        _outline(sprite, s)

    img = _background(size, s)
    on = sprite[:, :, 3] > 0
    img[on] = sprite[on]
    return img


def main():
    icons = []
    for size in SIZES:
        img = icon(size)
        path = OUT / f"icon{size}.png"
        Image.fromarray(img, "RGBA").save(path, optimize=True)
        icons.append(img)
        print(f"wrote {path}")
    if "--preview" in sys.argv:
        pad = max(SIZES)
        row = [np.pad(i, ((0, pad - i.shape[0]), (0, 4), (0, 0))) for i in icons]
        sheet = np.concatenate(row, axis=1)
        prev = Image.fromarray(sheet, "RGBA").resize(
            (sheet.shape[1] * 6, sheet.shape[0] * 6), Image.NEAREST)
        path = OUT.parent / "build" / "icons_preview.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        prev.save(path)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
