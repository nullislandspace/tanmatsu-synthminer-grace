#!/usr/bin/env python3
"""Generate SynthMiner's block textures into textures/ (one
subdirectory per reel segment, as the code in main/<segment>/).

Procedural and seeded, so the PNGs are reproducible from this script
instead of being opaque binaries: change a number here, re-run, commit
both. Every texture tiles seamlessly -- all noise is built periodic
(FFT smoothing wraps around) and every feature is drawn modulo the
texture size -- because the ship repeats them across large faces.

Plates are 64x64: the engine needs power-of-two edges (it wraps with a
mask), and at 2 bytes a texel in RGB565 a plate costs 8 KB of internal
SRAM. The flame is 64x8 (1 KB); the two planet maps are 128x64
(equirectangular, 16 KB).

    python3 tools/make_textures.py            # write textures/<segment>/*.png
    python3 tools/make_textures.py --preview  # also write a 4x contact sheet
"""

# Lifted from tanmatsu-showreel-grace (tools/make_textures.py); trimmed to SynthMiner's
# own blocks. The showreel remains the origin to diff against.
import sys
from pathlib import Path

import numpy as np
from PIL import Image

N = 64
OUT = Path(__file__).resolve().parent.parent / "textures"
rng = np.random.default_rng(0x5E3D)


def periodic_noise(sx, sy, gen=None):
    """Tileable noise: white noise low-passed with a Gaussian in the FFT
    domain (convolution there is circular, so the result wraps). sx/sy
    are the blur radii in texels; unequal radii give streaks. `gen` is
    the random generator (default: the shared one of the hull plates)."""
    white = (gen or rng).standard_normal((N, N))
    fy = np.fft.fftfreq(N)[:, None]
    fx = np.fft.fftfreq(N)[None, :]
    g = np.exp(-2 * (np.pi ** 2) * ((fx * sx) ** 2 + (fy * sy) ** 2))
    out = np.real(np.fft.ifft2(np.fft.fft2(white) * g))
    return out / (np.abs(out).max() + 1e-9)          # roughly -1..1


def to_rgb(lum, tint):
    """Luminance offsets around a base colour -> clipped uint8 RGB."""
    base = np.array(tint, dtype=float)[None, None, :]
    return np.clip(base + lum[:, :, None], 0, 255).astype(np.uint8)


def seam(lum, dark=-48, light=22):
    """A panel seam along row 0 / column 0, lit from the top-left: a dark
    groove with a bright lip below / right of it. Repeating the texture
    turns that into a plate grid."""
    lum[0, :] += dark
    lum[:, 0] += dark
    lum[1, 1:] += light
    lum[1:, 1] += light


def rivet(lum, x, y):
    """A 3x3 domed rivet head: bright top-left, shadow bottom-right."""
    for dy in range(-1, 2):
        for dx in range(-1, 2):
            lum[(y + dy) % N, (x + dx) % N] += 26 - 16 * (dx + dy)
    lum[(y + 2) % N, (x + 2) % N] -= 34


def riveted():
    """Fuselage: mid steel, soft mottling, one plate per tile with a
    rivet line inset along its two seams."""
    lum = 9 * periodic_noise(6, 6) + 4 * periodic_noise(1.2, 1.2)
    seam(lum)
    for k in range(4, N, 8):
        rivet(lum, k, 4)
        rivet(lum, 4, k)
    return to_rgb(lum, (148, 151, 156))


def brushed():
    """Wings: bright brushed steel -- long horizontal grain -- with a
    single seam, so the wing reads as a few large sheets."""
    lum = 15 * periodic_noise(14, 0.45) + 5 * periodic_noise(3, 0.8)
    seam(lum, dark=-40, light=18)
    return to_rgb(lum, (176, 179, 184))


def gunmetal():
    """Pods: dark blue-grey, vertical cooling grooves every 8 texels and
    a few bright scratches."""
    lum = 7 * periodic_noise(5, 5)
    for x in range(0, N, 8):
        lum[:, x] -= 30
        lum[:, (x + 1) % N] += 14
    for _ in range(6):
        x0, y0 = rng.integers(0, N, 2)
        length = rng.integers(8, 22)
        dx = rng.choice([-1, 1])
        for i in range(length):
            lum[(y0 + i) % N, (x0 + dx * i) % N] += 30
    return to_rgb(lum, (92, 98, 110))


def tread():
    """Undersides: diamond tread plate -- raised lozenges on a 16-texel
    grid, alternating 45-degree orientation checkerboard-style, each lit
    from the top-left."""
    lum = 6 * periodic_noise(4, 4)
    yy, xx = np.mgrid[0:N, 0:N]
    cell = 16
    for cy in range(0, N, cell):
        for cx in range(0, N, cell):
            flip = ((cx // cell) + (cy // cell)) % 2
            mx, my = cx + cell // 2, cy + cell // 2
            # Distance to the cell centre, wrapped so edge lozenges tile.
            ddx = (xx - mx + N // 2) % N - N // 2
            ddy = (yy - my + N // 2) % N - N // 2
            a = (ddx + ddy) if not flip else (ddx - ddy)   # along the lozenge
            b = (ddx - ddy) if not flip else (ddx + ddy)   # across it
            body = (np.abs(a) <= 7) & (np.abs(b) <= 1.5)
            lum[body] += 22
            # Lit edge on the upper-left side, shadow on the lower-right.
            edge = (np.abs(a) <= 7) & (np.abs(b) > 1.5) & (np.abs(b) <= 2.6)
            upper = (ddy + ddx) < 0 if flip else (ddy - ddx) < 0
            lum[edge & upper] += 12
            lum[edge & ~upper] -= 26
    return to_rgb(lum, (122, 124, 129))


def flame():
    """Engine flame, 64x8: u (x) runs along the flame from the nozzle
    (x = 0) to the tip, v (y) around it. A white-hot core fading through
    blue to a deep blue tip, with faint lengthwise streaks that fade in
    after the core so the white stays clean. Drawn emissive, so these
    are the exact on-screen colours. Not tiled along u: the flame maps it
    once, stopping short of the right edge so the tip never wraps back
    to white."""
    w, h = 64, 8
    stops = [(0.00, (235, 250, 255)), (0.15, (150, 212, 255)), (0.40, (60, 132, 255)),
             (0.75, (26, 62, 205)), (1.00, (10, 26, 112))]
    xs = np.linspace(0.0, 1.0, w)
    ramp = np.zeros((w, 3))
    for c in range(3):
        ramp[:, c] = np.interp(xs, [p for p, _ in stops], [col[c] for _, col in stops])
    streak = np.array([1.00, 0.90, 1.06, 0.94, 1.00, 0.88, 1.05, 0.95])[:, None, None]
    fade = np.clip((xs - 0.12) / 0.3, 0.0, 1.0)[None, :, None]   # no streaks in the core
    img = ramp[None, :, :] * (1.0 + (streak - 1.0) * fade)
    return np.clip(img, 0, 255).astype(np.uint8)


# --- Station and marauder textures -----------------------------------
#
# Their own generator, so adding them left the hull plates above (which
# share `rng`, in order) byte-identical.
rng2 = np.random.default_rng(0x57A7)


def panel_grid(lum, step, dark=-34, light=14):
    """Panel seams every `step` texels (tileable when step divides N)."""
    for k in range(0, N, step):
        lum[k, :] += dark
        lum[:, k] += dark
        lum[(k + 1) % N, :] += light
        lum[:, (k + 1) % N] += light


def station_hull():
    """Station hub and faces: the 2001 station's off-white, large panels
    (two per tile each way), soft mottling and a few small dark vents."""
    lum = 5 * periodic_noise(7, 7, rng2) + 2 * periodic_noise(1.5, 1.5, rng2)
    panel_grid(lum, 32)
    for _ in range(5):
        x0, y0 = rng2.integers(2, N - 8, 2)
        w, h = rng2.integers(3, 7), rng2.integers(2, 4)
        lum[y0:y0 + h, x0:x0 + w] -= 70
        lum[y0 + h, x0:x0 + w] += 20      # lit lower lip
    return to_rgb(lum, (200, 202, 204))


def station_ring():
    """Ring walls: the hull panelling with a band of lit windows across
    the middle -- read as habitation. (Lit only by the scene light, not
    emissive: on the night side the windows go dark with the wall.)"""
    lum = 5 * periodic_noise(7, 7, rng2)
    panel_grid(lum, 32)
    img = to_rgb(lum, (198, 200, 202)).astype(int)
    band = slice(26, 38)
    img[band, :, :] = (img[band, :, :] * 0.35).astype(int)      # dark window band
    for x in range(2, N, 8):                                     # 8 windows per tile
        on = rng2.random() < 0.8
        col = (255, 226, 150) if on else (60, 66, 80)
        img[29:35, x:x + 4, :] = col
    return np.clip(img, 0, 255).astype(np.uint8)


def marauder_wear():
    """Shared by both marauder liveries, so the two ships are visibly the
    same type: panel seams, grime (a luminance field) and a mask of where
    the paint is scratched or chipped down to bare metal."""
    grime = 16 * periodic_noise(5, 5, rng2) + 6 * periodic_noise(1.2, 1.2, rng2)
    panel_grid(grime, 16, dark=-40, light=10)
    bare = np.zeros((N, N), bool)
    for _ in range(9):                                   # scratches
        x0, y0 = rng2.integers(0, N, 2)
        length = rng2.integers(6, 20)
        dx = rng2.choice([-1, 1])
        for i in range(length):
            bare[(y0 + i // 2) % N, (x0 + dx * i) % N] = True
    chips = periodic_noise(2.2, 2.2, rng2) > 0.55        # chipped patches
    return grime, bare | chips


_WEAR = None


def marauder(paint):
    global _WEAR
    if _WEAR is None:
        _WEAR = marauder_wear()
    grime, bare = _WEAR
    img = to_rgb(grime, paint).astype(int)
    metal = to_rgb(grime * 0.6, (128, 130, 134)).astype(int)
    img[bare] = metal[bare]
    return np.clip(img, 0, 255).astype(np.uint8)


def marauder_green():
    """Marauder #1: old, dirty green paint."""
    return marauder((70, 94, 56))


def marauder_yellow():
    """Marauder #2: darkened, sooty yellow."""
    return marauder((150, 124, 38))


def flame_red():
    """Marauder engine flame, 64x8: the counterpart of flame(), running
    white-hot -> orange -> deep red."""
    w, h = 64, 8
    stops = [(0.00, (255, 246, 222)), (0.15, (255, 196, 96)), (0.40, (255, 112, 32)),
             (0.75, (196, 38, 16)), (1.00, (92, 12, 8))]
    xs = np.linspace(0.0, 1.0, w)
    ramp = np.zeros((w, 3))
    for c in range(3):
        ramp[:, c] = np.interp(xs, [p for p, _ in stops], [col[c] for _, col in stops])
    streak = np.array([1.00, 0.90, 1.06, 0.94, 1.00, 0.88, 1.05, 0.95])[:, None, None]
    fade = np.clip((xs - 0.12) / 0.3, 0.0, 1.0)[None, :, None]
    img = ramp[None, :, :] * (1.0 + (streak - 1.0) * fade)
    return np.clip(img, 0, 255).astype(np.uint8)


# --- Planet base, asteroid and planet textures (full reel, step 9.1) ---
#
# Again their own generator, so everything above stays byte-identical.
rng3 = np.random.default_rng(0xB0D5)


def pnoise(h, w, sx, sy, gen=rng3):
    """periodic_noise() for an h x w texture (the planet maps are 128x64)."""
    white = gen.standard_normal((h, w))
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.fftfreq(w)[None, :]
    g = np.exp(-2 * (np.pi ** 2) * ((fx * sx) ** 2 + (fy * sy) ** 2))
    out = np.real(np.fft.ifft2(np.fft.fft2(white) * g))
    return out / (np.abs(out).max() + 1e-9)


def ground():
    """The apron round the landing pad: packed ochre dust, darker gravel
    speckle, a few paler patches. Its average colour is what the PPA's flat
    ground has to match where the apron ends."""
    lum = 12 * pnoise(N, N, 6, 6) + 7 * pnoise(N, N, 1.4, 1.4) + 5 * pnoise(N, N, 0.6, 0.6)
    speck = rng3.random((N, N))
    lum[speck < 0.06] -= 26                       # gravel
    lum[speck > 0.97] += 18                       # pale grit
    return to_rgb(lum, (112, 92, 64))


def pad():
    """Landing pad: poured concrete slabs (two per tile each way) with
    tar-filled joints, scorch mottling from engine blasts and oil stains."""
    lum = 6 * pnoise(N, N, 5, 5) + 3 * pnoise(N, N, 1.0, 1.0)
    for k in (0, 32):
        lum[k, :] -= 46
        lum[:, k] -= 46
        lum[(k + 1) % N, :] += 10
        lum[:, (k + 1) % N] += 10
    lum += np.minimum(0, 30 * pnoise(N, N, 9, 9)) * 1.2   # soot, only darkens
    for _ in range(4):                                     # oil stains
        cx, cy = rng3.integers(0, N, 2)
        r = rng3.integers(2, 5)
        yy, xx = np.mgrid[0:N, 0:N]
        d = ((xx - cx + N // 2) % N - N // 2) ** 2 + ((yy - cy + N // 2) % N - N // 2) ** 2
        lum[d <= r * r] -= 30
    return to_rgb(lum, (150, 148, 142))


def industrial_wall():
    """Factory siding: vertical corrugated steel (a rib every 4 texels, lit
    from the left), one horizontal panel seam per tile, and rust running
    down from the seam."""
    xs = np.arange(N)
    rib = 16 * np.cos(2 * np.pi * xs / 4.0)[None, :].repeat(N, axis=0)
    lum = rib + 5 * pnoise(N, N, 4, 4)
    lum[0, :] -= 40
    lum[1, :] += 14
    img = to_rgb(lum, (118, 124, 128)).astype(int)
    rust = np.clip(pnoise(N, N, 1.2, 9) * 1.4, 0, 1)       # streaks: stretched vertically
    fall = np.linspace(1.0, 0.2, N)[:, None]               # strongest just below the seam
    w = (rust * fall)[:, :, None]
    img = img * (1 - w) + np.array([124, 70, 38])[None, None, :] * w
    return np.clip(img, 0, 255).astype(np.uint8)


def rock():
    """Asteroid: grey-brown rock at several scales, pocked with small
    craters (dark bowl, bright rim on the lit upper-left side)."""
    lum = 18 * pnoise(N, N, 8, 8) + 10 * pnoise(N, N, 2.5, 2.5) + 5 * pnoise(N, N, 0.7, 0.7)
    yy, xx = np.mgrid[0:N, 0:N]
    for _ in range(9):
        cx, cy = rng3.integers(0, N, 2)
        r = rng3.uniform(2.0, 6.0)
        dx = (xx - cx + N // 2) % N - N // 2
        dy = (yy - cy + N // 2) % N - N // 2
        d = np.sqrt(dx * dx + dy * dy)
        lum[d < r] -= 22
        rim = (d >= r) & (d < r + 1.5)
        lum[rim & (dx + dy < 0)] += 20
        lum[rim & (dx + dy >= 0)] -= 10
    return to_rgb(lum, (112, 104, 96))


def planet_terran():
    """The industrial planet from orbit, 128x64 equirectangular (u wraps
    round the equator): ochre continents, dark slate seas, grey ice at the
    poles and thin cloud."""
    h, w = 64, 128
    height = pnoise(h, w, 7, 7) + 0.35 * pnoise(h, w, 2, 2)
    land = height > 0.05
    img = np.zeros((h, w, 3))
    img[:] = (44, 60, 72)                                   # sea
    shade = (height - 0.05)[:, :, None] * 120
    img[land] = (np.array([150, 118, 72])[None, :] + shade[land]).clip(0, 255)
    lat = np.abs(np.linspace(-1, 1, h))[:, None]
    ice = lat + 0.08 * pnoise(h, w, 3, 3) > 0.82
    img[ice] = (196, 200, 206)
    cloud = np.clip((pnoise(h, w, 4, 1.5) - 0.25) * 2.2, 0, 1)[:, :, None]
    img = img * (1 - 0.7 * cloud) + 225 * 0.7 * cloud
    return np.clip(img, 0, 255).astype(np.uint8)


def planet_gas():
    """The gas giant of the second system, 128x64 equirectangular:
    latitude bands in cream, tan and rust, their edges torn by turbulence,
    and one oval storm."""
    h, w = 64, 128
    y = np.linspace(-1, 1, h)[:, None].repeat(w, axis=1)
    warp = 0.09 * pnoise(h, w, 10, 2.5) + 0.03 * pnoise(h, w, 2, 1)
    yw = y + warp
    # Broad belts of uneven width: two latitude frequencies beating.
    b = 0.65 * np.sin(yw * np.pi * 3.1) + 0.35 * np.sin(yw * np.pi * 7.7 + 1.3)
    stops = np.array([[92, 50, 34], [176, 118, 72], [222, 196, 150], [196, 150, 100]], float)
    t = (b + 1) / 2 * (len(stops) - 1)
    i = np.clip(t.astype(int), 0, len(stops) - 2)
    f = (t - i)[:, :, None]
    img = stops[i] * (1 - f) + stops[i + 1] * f
    yy, xx = np.mgrid[0:h, 0:w]
    storm = ((xx - 88) / 9.0) ** 2 + ((yy - 40) / 4.0) ** 2
    img[storm < 1] = img[storm < 1] * 0.4 + np.array([200, 96, 60]) * 0.6
    img[(storm >= 1) & (storm < 1.6)] *= 1.12
    return np.clip(img, 0, 255).astype(np.uint8)


# --- SynthMiner block textures (claudeplans/synthminer.md, step 1.1) ---
#
# 16x16, drawn texel by texel (the engine samples nearest-texel, so each
# texel shows as a crisp square). Every texture has its own generator,
# seeded from its own tag, so adding or reordering one leaves the others
# byte-identical. All tile: features wrap modulo 16.
B = 16


def sm_gen(tag):
    return np.random.default_rng([0xC4AF7, tag])


def sm_speckle(gen, tint, spread, h=B, w=B):
    """A base colour with independent per-texel brightness steps -- the
    pixel-art grain of every block."""
    lum = gen.integers(-spread, spread + 1, (h, w)).astype(float)
    return lum, tint


def sm_rgb(lum, tint):
    return to_rgb(lum, tint)


def sm_dirt_lum(gen):
    lum = gen.integers(-14, 15, (B, B)).astype(float)
    for _ in range(10):                               # darker clods, lighter grit
        x, y = gen.integers(0, B, 2)
        lum[y, x] -= 22
    for _ in range(6):
        x, y = gen.integers(0, B, 2)
        lum[y, x] += 18
    return lum


DIRT = (122, 86, 58)
GRASS = (92, 150, 52)


def sm_dirt():
    return sm_rgb(sm_dirt_lum(sm_gen(1)), DIRT)


def sm_grass_top():
    gen = sm_gen(2)
    lum = gen.integers(-16, 17, (B, B)).astype(float)
    for _ in range(14):                               # darker blades
        x, y = gen.integers(0, B, 2)
        lum[y, x] -= 20
    return sm_rgb(lum, GRASS)


def sm_grass_side():
    """Dirt with the grass hanging over the top edge, 2-5 texels deep per
    column. v = 0 is the top of the block."""
    gen = sm_gen(3)
    img = sm_rgb(sm_dirt_lum(gen), DIRT).astype(int)
    grass = sm_rgb(gen.integers(-16, 17, (B, B)).astype(float), GRASS).astype(int)
    depth = 3 + gen.integers(-1, 2, B)
    depth[gen.integers(0, B, 3)] += 2                 # a few longer drips
    for x in range(B):
        img[: depth[x], x] = grass[: depth[x], x]
    return img.astype(np.uint8)


def sm_stone():
    gen = sm_gen(4)
    lum = 10 * pnoise(B, B, 1.2, 1.2, gen) + gen.integers(-8, 9, (B, B))
    for _ in range(3):                                # short dark streaks
        x, y = gen.integers(0, B, 2)
        n = int(gen.integers(2, 5))
        for k in range(n):
            lum[y % B, (x + k) % B] -= 20
            y += int(gen.integers(0, 2))
    return sm_rgb(lum, (122, 122, 124))


def sm_cobble():
    """Rounded stones in mortar: each texel belongs to its nearest seed
    (distance wraps, so the pattern tiles); texels near a cell edge are
    mortar."""
    gen = sm_gen(5)
    # One stone per cell of a jittered 3x3 grid, so they spread evenly.
    grid = (np.mgrid[0:3, 0:3].reshape(2, -1).T + 0.5) * (B / 3)
    seeds = grid + gen.uniform(-1.3, 1.3, grid.shape)
    shade = gen.integers(-9, 10, len(seeds))
    ys, xs = np.mgrid[0:B, 0:B] + 0.5
    d = []
    for sx, sy in seeds:
        dx = np.abs(xs - sx)
        dy = np.abs(ys - sy)
        dx = np.minimum(dx, B - dx)
        dy = np.minimum(dy, B - dy)
        d.append(np.sqrt(dx * dx + dy * dy))
    d = np.array(d)
    order = np.sort(d, axis=0)
    cell = np.argmin(d, axis=0)
    # Rounded: brightest at the stone's middle, darker towards its edge.
    lum = shade[cell] + 22 - 2.2 * order[0] + gen.integers(-5, 6, (B, B))
    lum[(order[1] - order[0]) < 0.7] = -40             # mortar
    return sm_rgb(lum, (118, 118, 118))


def sm_sand():
    gen = sm_gen(6)
    lum = gen.integers(-10, 11, (B, B)).astype(float)
    return sm_rgb(lum, (214, 200, 150))


def sm_water(cut=True):
    """The surface of water, and the only face a liquid ever draws
    (blocks.h, K_LIQUID): deep blue with lighter wave crests running
    across u, so a scrolled u reads as flowing.

    CUT-OUT, on a checkerboard. The engine has no blending -- alpha is
    one bit -- so the way to make water look like water rather than like
    a blue floor is to punch every other texel out and let what is
    behind show through it. From above that is the lake bed; from
    underneath it is the sky. A checkerboard rather than the random
    scatter the leaves use, because a regular grid reads as a
    half-transparent sheet where a random one reads as damage.

    The crests stay solid: they are the part the eye reads as a surface,
    and holes through them would make the water look torn."""
    gen = sm_gen(7)
    lum = gen.integers(-6, 7, (B, B)).astype(float)
    crest = np.zeros((B, B), dtype=bool)
    for y in range(0, B, 4):
        off = int(gen.integers(0, B))
        for k in range(5):
            lum[(y + (k % 2)) % B, (off + k) % B] += 26
            crest[(y + (k % 2)) % B, (off + k) % B] = True
    yy, xx = np.mgrid[0:B, 0:B]
    holes = ((xx + yy) % 2 == 0) & ~crest
    if not cut:
        # SOLID, for the blended path: the rasterizer mixes the whole
        # texel with what is behind it, so punching holes in it as well
        # would be transparency twice and the checkerboard would still
        # be visible through the mix.
        return sm_alpha(sm_rgb(lum, (48, 84, 196)), np.zeros_like(holes))
    return sm_alpha(sm_rgb(lum, (48, 84, 196)), holes)


def sm_log_side():
    """Bark: vertical furrows, v along the trunk."""
    gen = sm_gen(8)
    col = gen.integers(-12, 13, B).astype(float)
    lum = np.tile(col[None, :], (B, 1)) + gen.integers(-6, 7, (B, B))
    for x in gen.choice(B, 4, replace=False):         # deep furrows, broken
        for y in range(B):
            if gen.random() < 0.8:
                lum[y, x] -= 26
    return sm_rgb(lum, (100, 76, 46))


def sm_log_top():
    """The cut end: rings round the centre, bark round the rim."""
    gen = sm_gen(9)
    ys, xs = np.mgrid[0:B, 0:B] + 0.5
    r = np.maximum(np.abs(xs - B / 2), np.abs(ys - B / 2))   # squarish rings
    ring = np.floor(r).astype(int)
    lum = np.where(ring % 2 == 0, 10.0, -8.0) + gen.integers(-5, 6, (B, B))
    img = sm_rgb(lum, (168, 132, 82)).astype(int)
    bark = sm_rgb(gen.integers(-10, 11, (B, B)).astype(float), (100, 76, 46)).astype(int)
    rim = r >= B / 2 - 1
    img[rim] = bark[rim]
    return img.astype(np.uint8)


def sm_planks():
    """Four boards per block, 4 texels each, joints staggered."""
    gen = sm_gen(10)
    lum = gen.integers(-7, 8, (B, B)).astype(float)
    for board in range(4):
        y0 = board * 4
        lum[y0 : y0 + 4, :] += int(gen.integers(-8, 9))
        lum[y0 + 3, :] -= 30                          # the gap under the board
        j = (board * 7 + 3) % B                       # its end joint
        lum[y0 : y0 + 3, j] -= 24
        for x in gen.integers(0, B, 3):               # grain
            lum[y0 + int(gen.integers(0, 3)), x] -= 12
    return sm_rgb(lum, (164, 128, 78))


def sm_planks_lum(tag):
    """The plank pattern on its own, so a block that is MADE of planks
    can put something on top of it rather than inventing its own wood."""
    gen = sm_gen(tag)
    lum = gen.integers(-7, 8, (B, B)).astype(float)
    for board in range(4):
        y0 = board * 4
        lum[y0 : y0 + 4, :] += int(gen.integers(-8, 9))
        lum[y0 + 3, :] -= 30
        j = (board * 7 + 3) % B
        lum[y0 : y0 + 3, j] -= 24
        for x in gen.integers(0, B, 3):
            lum[y0 + int(gen.integers(0, 3)), x] -= 12
    return lum, gen


def sm_table_top():
    """The crafting table seen from above: planks with a 3x3 grid burnt
    into them, which is the one picture of crafting everybody knows --
    even though this game has no grid to fill in (Part C)."""
    lum, _ = sm_planks_lum(30)
    # Two lines each way at thirds of the block, and a border, so the
    # nine cells read at the size a block actually gets drawn.
    for at in (5, 10):
        lum[at, 1:15] -= 46
        lum[1:15, at] -= 46
    lum[0, :] -= 26
    lum[15, :] -= 26
    lum[:, 0] -= 26
    lum[:, 15] -= 26
    return sm_rgb(lum, (164, 128, 78))


def sm_table_side():
    """... and from the side: the same planks with a tool rack on them,
    dark pegs under a rail."""
    lum, gen = sm_planks_lum(31)
    lum[4, 1:15] -= 34                     # the rail
    for x in (3, 7, 11):                   # what hangs off it
        lum[5:9, x] -= 40
        lum[8, x - 1 : x + 2] -= 28
    lum += gen.integers(-4, 5, (B, B))
    return sm_rgb(lum, (152, 118, 72))


def sm_stone_lum(tag):
    """The stone pattern on its own, for blocks BUILT of stone."""
    gen = sm_gen(tag)
    lum = 10 * pnoise(B, B, 1.2, 1.2, gen) + gen.integers(-8, 9, (B, B))
    for _ in range(3):
        x, y = gen.integers(0, B, 2)
        n = int(gen.integers(2, 5))
        for k in range(n):
            lum[y % B, (x + k) % B] -= 20
            y += int(gen.integers(0, 2))
    return lum, gen


def sm_furnace_top():
    """... and its lid, with a rim so the block reads as a box from
    above rather than as a patch of floor."""
    lum, _ = sm_stone_lum(33)
    lum[0:2, :] += 12
    lum[14:16, :] -= 16
    lum[:, 0:2] += 8
    lum[:, 14:16] -= 12
    lum[4:12, 4:12] -= 10
    return sm_rgb(lum, (112, 112, 114))


def sm_furnace_front():
    """The face with the fire in it: an arched opening, three bars
    across, and the dark of the firebox behind them."""
    lum, gen = sm_stone_lum(34)
    # The opening: rows 5..13, inset, with the top two corners cut so it
    # arches rather than sitting there as a rectangle.
    for y in range(5, 14):
        for x in range(3, 13):
            corner = (y == 5 and (x < 5 or x > 10)) or (y == 6 and (x < 4 or x > 11))
            if corner:
                continue
            lum[y, x] = -66 + int(gen.integers(-6, 7))
    for x in range(3, 13):                 # the grate
        for y in (8, 11):
            lum[y, x] += 26
    lum[13, 3:13] += 14                    # the lip it all sits on
    return sm_rgb(lum, (114, 114, 116))


def sm_iron_ore():
    """Stone with pale tan blobs in it, so it reads as ore but not as
    coal -- the two sit next to each other underground."""
    lum, gen = sm_stone_lum(35)
    rgb = sm_rgb(lum, (122, 122, 124))
    for cx, cy, r in ((4, 4, 2), (11, 6, 2), (6, 11, 2), (12, 12, 1)):
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r and 0 <= x < B and 0 <= y < B:
                    d = int(gen.integers(-12, 13))
                    rgb[y, x] = np.clip(np.array([206 + d, 168 + d, 132 + d]), 0, 255)
    return rgb


CHEST_IRON = (86, 86, 92)


def sm_chest_side():
    """Planks with two iron bands and a latch: a box, from any side."""
    lum, gen = sm_planks_lum(36)
    rgb = sm_rgb(lum, (150, 110, 62))
    for y in (2, 13):
        rgb[y, :] = CHEST_IRON
        rgb[y + 1, :] = tuple(c - 18 for c in CHEST_IRON)
    # The latch, in the middle of the front.
    for y in range(6, 11):
        for x in range(6, 10):
            rgb[y, x] = CHEST_IRON if (y + x) % 3 else tuple(c + 26 for c in CHEST_IRON)
    return rgb


def sm_chest_top():
    """... and its lid, banded the other way."""
    lum, _ = sm_planks_lum(37)
    rgb = sm_rgb(lum, (146, 106, 58))
    for x in (2, 13):
        rgb[:, x] = CHEST_IRON
        rgb[:, x + 1] = tuple(c - 18 for c in CHEST_IRON)
    return rgb


def sm_trash_side():
    """The same box, in grey with a dark mouth: it is a chest that eats
    what you put in it, and it has to look like it."""
    lum, _ = sm_planks_lum(38)
    rgb = sm_rgb(lum - 18, (104, 100, 98))
    for y in (2, 13):
        rgb[y, :] = (62, 60, 58)
        rgb[y + 1, :] = (48, 46, 44)
    for y in range(6, 12):
        for x in range(4, 12):
            rgb[y, x] = (34, 32, 30)
    return rgb


def sm_trash_top():
    lum, _ = sm_planks_lum(39)
    rgb = sm_rgb(lum - 18, (100, 96, 94))
    for y in range(3, 13):
        for x in range(3, 13):
            rgb[y, x] = (30, 28, 26)
    return rgb


def sm_bench_top():
    """The crafting table's opposite number: the same planks, with the
    grid replaced by a cut across it."""
    lum, gen = sm_planks_lum(41)
    for at in (7, 8):
        lum[at, 1:15] -= 52
    for x in range(2, 14, 2):                # the teeth of the saw
        lum[6, x] -= 34
        lum[9, x + 1] -= 34
    lum += gen.integers(-3, 4, (B, B))
    lum[0, :] -= 26
    lum[15, :] -= 26
    return sm_rgb(lum, (150, 116, 70))


# --- Item icons -------------------------------------------------------
#
# The things that are NOT blocks need a picture of their own: a block can
# be drawn in the inventory with its own texture, and coal cannot. 16x16
# with a cut-out background (alpha < 128 is a hole, se_texture.h), so the
# slot shows through around them.

def _icon():
    return np.zeros((B, B, 4), np.uint8)


def _dot(img, x, y, rgb):
    if 0 <= x < B and 0 <= y < B:
        img[y, x, 0] = rgb[0]
        img[y, x, 1] = rgb[1]
        img[y, x, 2] = rgb[2]
        img[y, x, 3] = 255


def _rect(img, x0, y0, x1, y1, rgb):
    for y in range(y0, y1):
        for x in range(x0, x1):
            _dot(img, x, y, rgb)


def _shade(rgb, d):
    return tuple(int(max(0, min(255, c + d))) for c in rgb)


HANDLE = (138, 98, 56)


def _tool_handle(img):
    """Corner to corner, two texels wide, as every tool has."""
    for i in range(10):
        _dot(img, 3 + i, 13 - i, HANDLE)
        _dot(img, 4 + i, 13 - i, _shade(HANDLE, -26))


def sm_item_pickaxe(rgb):
    img = _icon()
    _tool_handle(img)
    # A wide head with both points turned down.
    for i in range(7):
        _dot(img, 6 + i, 4, rgb)
        _dot(img, 6 + i, 5, _shade(rgb, -22))
    _rect(img, 5, 5, 7, 8, rgb)
    _rect(img, 11, 5, 13, 8, rgb)
    _dot(img, 5, 4, _shade(rgb, 20))
    return img


def sm_item_axe(rgb):
    img = _icon()
    _tool_handle(img)
    # A wedge on one side of the top of the handle.
    for i in range(5):
        _rect(img, 6, 3 + i, 11 - (i // 2), 4 + i, rgb)
    _rect(img, 6, 3, 8, 8, _shade(rgb, 18))
    return img


def sm_item_shovel(rgb):
    img = _icon()
    _tool_handle(img)
    _rect(img, 8, 3, 12, 8, rgb)
    _rect(img, 8, 3, 12, 4, _shade(rgb, 22))
    _rect(img, 8, 7, 12, 8, _shade(rgb, -22))
    return img


def sm_item_ingot():
    """A bar: a flat-topped trapezium with a highlight along the top."""
    img = _icon()
    rgb = (214, 214, 220)
    for i, y in enumerate(range(6, 11)):
        x0 = 3 + i // 2
        x1 = 13 - i // 2
        _rect(img, x0, y, x1, y + 1, _shade(rgb, -6 * i))
    _rect(img, 4, 6, 12, 7, _shade(rgb, 22))
    return img


def sm_item_coal():
    """Three lumps, because one reads as a hole in the slot."""
    img = _icon()
    gen = sm_gen(40)
    for cx, cy, r in ((6, 7, 3), (10, 5, 2), (10, 10, 2)):
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    lum = int(gen.integers(-10, 11))
                    _dot(img, x, y, _shade((44, 44, 50), lum))
    _dot(img, 5, 6, (96, 96, 104))
    _dot(img, 9, 4, (86, 86, 94))
    return img


BUCKET_BODY = (196, 200, 210)
BUCKET_DARK = (140, 146, 158)
BUCKET_RIM = (228, 232, 240)


def _bucket(fill=None):
    """A pail: a tapering body under a wide rim, with a wire handle.

    The shape has to read at 16 px with no outline, so the rim is drawn
    ONE TEXEL WIDER than the body on each side. That overhang is the
    whole silhouette -- without it a bucket and an ingot are the same
    grey trapezium."""
    img = _icon()
    # The wire handle, arching over the mouth.
    for x, y in ((4, 5), (5, 3), (6, 2), (7, 2), (8, 2), (9, 2), (10, 3), (11, 5)):
        _dot(img, x, y, BUCKET_DARK)
    # The body, lit from the left and darkening towards the bottom.
    for i, y in enumerate(range(7, 15)):
        x0, x1 = 4 + i // 3, 12 - i // 3
        _rect(img, x0, y, x1, y + 1, _shade(BUCKET_BODY, -5 * i))
        _dot(img, x0, y, _shade(BUCKET_RIM, -4 * i))
        _dot(img, x1 - 1, y, _shade(BUCKET_DARK, -4 * i))
    _rect(img, 3, 6, 13, 7, BUCKET_RIM)
    # What is in it, seen through the mouth.
    if fill is not None:
        _rect(img, 4, 7, 12, 10, fill)
        _rect(img, 4, 7, 12, 8, _shade(fill, 30))
    return img


def sm_item_bucket():
    return _bucket()


def sm_item_bucket_water():
    return _bucket((56, 98, 200))


def sm_item_stick():
    img = _icon()
    for i in range(11):
        _dot(img, 3 + i, 13 - i, HANDLE)
        _dot(img, 4 + i, 13 - i, _shade(HANDLE, -30))
    _dot(img, 3, 14, _shade(HANDLE, -30))
    return img


def sm_alpha(rgb, holes):
    """RGB plus a hole mask -> RGBA: the engine draws alpha < 128 as a
    hole (cut-out transparency), everything else opaque."""
    a = np.where(holes, 0, 255).astype(np.uint8)[:, :, None]
    return np.concatenate([rgb, a], axis=2)


def sm_leaves_lum():
    gen = sm_gen(11)
    lum = gen.integers(-18, 19, (B, B)).astype(float)
    holes = gen.random((B, B)) < 0.22
    return lum, holes


def sm_leaves():
    """Leaves with gaps (cut-out): the sky and the branches behind show
    through, as in Minecraft's "fancy" leaves. For canopies close by."""
    lum, holes = sm_leaves_lum()
    return sm_alpha(sm_rgb(lum, (58, 112, 38)), holes)


def sm_leaves_fast():
    """The same leaves, opaque: the gaps dark ("fast" leaves), for the
    textured canopies further off, which are drawn without their
    insides."""
    lum, holes = sm_leaves_lum()
    lum[holes] = -52
    return sm_rgb(lum, (58, 112, 38))


def sm_birch_leaves_lum():
    gen = sm_gen(42)
    lum = gen.integers(-16, 17, (B, B)).astype(float)
    holes = gen.random((B, B)) < 0.24
    return lum, holes


def sm_birch_leaves():
    """Lighter and yellower than oak: what makes a birch wood read as a
    different wood from across a valley."""
    lum, holes = sm_birch_leaves_lum()
    return sm_alpha(sm_rgb(lum, (108, 152, 62)), holes)


def sm_birch_leaves_fast():
    lum, holes = sm_birch_leaves_lum()
    lum[holes] = -48
    return sm_rgb(lum, (108, 152, 62))


def sm_birch_side():
    """White bark with the dark scars birches have, which is the whole
    signature of the tree."""
    gen = sm_gen(43)
    lum = gen.integers(-7, 8, (B, B)).astype(float)
    for _ in range(5):                       # the scars, short and level
        y = int(gen.integers(0, B))
        x = int(gen.integers(0, B))
        n = int(gen.integers(2, 5))
        for k in range(n):
            lum[y, (x + k) % B] -= 58
            if gen.random() < 0.4:
                lum[(y + 1) % B, (x + k) % B] -= 34
    for x in range(B):                       # a faint vertical grain
        lum[:, x] += int(gen.integers(-5, 6))
    return sm_rgb(lum, (216, 214, 202))


def sm_birch_top():
    """The cut end: rings, like the oak's but paler."""
    gen = sm_gen(44)
    ys, xs = np.mgrid[0:B, 0:B] + 0.5
    d = np.sqrt((xs - B / 2) ** 2 + (ys - B / 2) ** 2)
    lum = (np.sin(d * 1.9) * 9.0) + gen.integers(-6, 7, (B, B))
    lum[d > 7.1] -= 26                       # the bark round the edge
    return sm_rgb(lum, (196, 184, 156))


def sm_cactus():
    """Green with vertical ribs and a paler edge, so a stack of them
    still reads as separate blocks."""
    gen = sm_gen(45)
    lum = gen.integers(-6, 7, (B, B)).astype(float)
    for x in range(1, B, 4):                 # the ribs
        lum[:, x] -= 26
        lum[:, (x + 1) % B] += 10
    lum[0, :] += 16
    lum[15, :] -= 22
    for _ in range(14):                      # spines
        lum[int(gen.integers(0, B)), int(gen.integers(0, B))] += 40
    return sm_rgb(lum, (74, 128, 60))


def sm_snow():
    """Nearly white, and nearly flat: snow has no features, and any it
    is given read as dirt on it."""
    gen = sm_gen(46)
    lum = gen.integers(-5, 6, (B, B)).astype(float)
    lum += 6.0 * pnoise(B, B, 2.6, 2.6, gen)
    return sm_rgb(lum, (236, 240, 248))


def sm_sandstone():
    """Sand, pressed: the same colour with level bedding lines through
    it, which is what tells the two apart underground."""
    gen = sm_gen(47)
    lum = gen.integers(-5, 6, (B, B)).astype(float)
    lum += 4.0 * pnoise(B, B, 3.0, 3.0, gen)
    for y in (3, 7, 12):                     # bedding
        lum[y, :] -= 20
        lum[(y + 1) % B, :] += 7
    return sm_rgb(lum, (214, 198, 148))


def sm_coal_ore():
    img = sm_stone().astype(int)
    gen = sm_gen(12)
    lump = [(0, 0), (1, 0), (0, 1), (1, 1), (2, 1), (1, 2), (-1, 1), (2, 0)]
    for _ in range(5):                                # black lumps of 5-8 texels
        x, y = (int(v) for v in gen.integers(0, B, 2))
        for dx, dy in lump[: int(gen.integers(5, 9))]:
            img[(y + dy) % B, (x + dx) % B] = (26 + gen.integers(0, 18),) * 3
    return img.astype(np.uint8)


def sm_face():
    """The miner's face, the head's front: skin, dark eyes with a white
    glint, bushy brows, a big brown moustache. v = 0 is the top."""
    gen = sm_gen(13)
    skin = (222, 170, 128)
    lum = gen.integers(-5, 6, (B, B)).astype(float)
    img = sm_rgb(lum, skin).astype(int)
    brow, hair, eye_w, eye_d, mouth = (70, 46, 28), (96, 62, 36), (240, 240, 236), (40, 44, 70), (150, 80, 70)
    img[0:3, :] = hair                                # hair under the hat's brim
    img[5, 2:7] = brow
    img[5, 9:14] = brow
    img[7, 3:6] = eye_w
    img[7, 10:13] = eye_w
    img[7, 4] = eye_d
    img[7, 11] = eye_d
    img[8, 3:6] = eye_d
    img[8, 10:13] = eye_d
    img[9:11, 7:9] = (200, 140, 104)                  # the nose, shaded
    img[11, 3:13] = hair                              # the moustache
    img[12, 2:6] = hair
    img[12, 10:14] = hair
    img[13, 6:10] = mouth
    return np.clip(img, 0, 255).astype(np.uint8)


def sm_sprite(tag, stem, bloom):
    """A plant on a cross of two quads (cut-out): a green stem with two
    leaves, and a bloom of `bloom` colours on top; the rest is holes."""
    gen = sm_gen(tag)
    rgb = np.zeros((B, B, 3), np.uint8)
    holes = np.ones((B, B), bool)

    def px(x, y, c):
        rgb[y, x] = np.clip(np.array(c) + gen.integers(-10, 11, 3), 0, 255)
        holes[y, x] = False

    for y in range(7, B):
        px(7, y, stem)
    for k in range(3):                                # the two leaves
        px(6 - k, 12 - k, stem)
        px(8 + k, 11 - k, stem)
    for dy, row in enumerate(bloom):
        for dx, c in enumerate(row):
            if c is not None:
                px(5 + dx, 2 + dy, c)
    return sm_alpha(rgb, holes)


R, Y, W, K = (214, 40, 36), (250, 212, 40), (250, 244, 214), (96, 40, 20)


def sm_flower_red():
    return sm_sprite(14, (60, 130, 40), [[None, R, R, R, None], [R, R, K, R, R], [R, K, Y, K, R], [R, R, K, R, R],
                                          [None, R, R, R, None]])


def sm_flower_yellow():
    return sm_sprite(15, (70, 140, 44), [[None, None, Y, None, None], [None, Y, Y, Y, None], [Y, Y, W, Y, Y],
                                          [None, Y, Y, Y, None], [None, None, Y, None, None]])


def sm_tall_grass():
    """Tufts of grass blades (cut-out), for crossed quads."""
    gen = sm_gen(16)
    rgb = np.zeros((B, B, 3), np.uint8)
    holes = np.ones((B, B), bool)
    for x in range(1, B - 1):
        if gen.random() < 0.35:
            continue
        top = int(gen.integers(3, 12))
        lean = int(gen.integers(-1, 2))
        for y in range(top, B):
            xx = x + (lean if y < top + 3 else 0)
            rgb[y, xx] = np.clip(np.array(GRASS) + gen.integers(-22, 12, 3) - 6 * (B - y) // 4, 0, 255)
            holes[y, xx] = False
    return sm_alpha(rgb, holes)


def sm_glass():
    """A window pane (cut-out): a frame round the edge and a few white
    glints; the rest is holes -- no blending, so clear glass is empty."""
    gen = sm_gen(17)
    rgb = np.zeros((B, B, 3), np.uint8)
    holes = np.ones((B, B), bool)
    frame = (200, 216, 222)
    for i in range(B):
        for x, y in ((i, 0), (i, B - 1), (0, i), (B - 1, i)):
            rgb[y, x] = frame
            holes[y, x] = False
    for x, y in ((4, 3), (3, 4), (5, 3), (3, 5), (11, 9), (10, 10), (9, 11)):
        rgb[y, x] = (236, 244, 248)
        holes[y, x] = False
    return sm_alpha(rgb, holes)


# The torch burns: TORCH_FRAMES of it, swapped by chunk_render on a
# timer so every torch in the world flickers together. One shared
# material means one texture pointer to change and no per-torch state,
# which is the whole reason this is a flipbook rather than geometry --
# the user's call: "If all torches show a syncronized animation, that is
# fine. We don't need a per block clock."
TORCH_FRAMES = 4


def sm_torch_frame(frame):
    """The torch's stick: dark wood, with a burning tip that flickers.

    THE FIRE DOES NOT MOVE. The stick is identical in every frame and so
    is the extent of the flame -- the same four rows burn in all of
    them, and only their COLOUR changes, cycling yellow -> orange ->
    orange-red and back.

    Both of those are corrections. Varying a stripe pattern gave four
    pictures the eye could not separate. Varying the flame's height then
    gave it movement, but the wrong movement: "the animation looks like
    the fire is moving up and down the stick" (the user, 2026-09-28).
    Fire on a torch stays where it is; what changes is its colour. And
    the frames stay within a narrow band of brightness, because the same
    round dimmed to near-embers and "it dims too much".
    """
    gen = sm_gen(18)                      # the same wood every frame
    lum = gen.integers(-10, 11, (B, B)).astype(float)
    img = sm_rgb(lum, (110, 80, 44)).astype(int)

    # Four rows of fire, hottest at the top, and one palette per frame.
    # Read down a column for a single frame's flame; read across for
    # what one row does over the cycle -- which is the flicker, and it
    # never leaves yellow-orange-red.
    FIRE = (
        ((255, 246, 184), (255, 210, 92), (250, 162, 52), (232, 122, 38)),
        ((255, 238, 156), (255, 192, 74), (244, 144, 44), (226, 110, 32)),
        ((255, 226, 126), (252, 174, 60), (238, 128, 38), (220, 98, 28)),
        ((255, 240, 164), (255, 200, 82), (246, 152, 48), (228, 116, 34)),
    )[frame % 4]
    for row in range(4):
        img[row, :] = FIRE[row]
    # The char line under the fire, so wood does not meet flame with a
    # hard edge. Fixed, like everything else about the geometry here.
    img[4, :] = (200, 110, 40)
    return np.clip(img, 0, 255).astype(np.uint8)


def sm_torch():
    return sm_torch_frame(0)


def sm_item_torch():
    """The torch as an ITEM: a stick with a flame on top and everything
    else cut away.

    The inventory used to draw the BLOCK's side texture here, which is a
    full 16x16 square of wood with a glowing band across the top -- "the
    torch image in the inventory looks like a block with a yellow top
    instead of a torch" (the user, 2026-09-28). A torch is a thin thing
    and has to be drawn as one."""
    img = _icon()
    # The stick: two texels wide, standing in the lower two thirds.
    for y in range(6, 15):
        _dot(img, 7, y, HANDLE)
        _dot(img, 8, y, _shade(HANDLE, -30))
    # The burning end.
    _rect(img, 6, 4, 10, 6, (226, 120, 32))
    _rect(img, 7, 2, 9, 5, (255, 200, 72))
    _dot(img, 7, 1, (255, 248, 206))
    _dot(img, 8, 2, (255, 248, 206))
    return img


def sm_bedrock():
    """Beta's bedrock: dark grey, blotched near-black and pale grey at
    random, with no pattern to it."""
    gen = sm_gen(19)
    lum = gen.integers(-10, 11, (B, B)).astype(float)
    pick = gen.random((B, B))
    lum[pick < 0.30] -= 34                            # near-black patches
    lum[pick > 0.82] += 40                            # pale chips
    lum = lum + 8 * pnoise(B, B, 1.0, 1.0, gen)
    return sm_rgb(lum, (84, 84, 84))


def sm_gravel():
    """Pebbles in grey, brown-grey and near-white, packed tight: each
    texel belongs to its nearest pebble (wrapping, so it tiles), darker at
    the pebble's rim."""
    gen = sm_gen(20)
    n = 22
    seeds = gen.uniform(0, B, (n, 2))
    tints = np.array([(128, 122, 118), (104, 98, 94), (150, 142, 136), (92, 84, 80), (170, 164, 160)])
    tint = tints[gen.integers(0, len(tints), n)]
    ys, xs = np.mgrid[0:B, 0:B] + 0.5
    d = []
    for sx, sy in seeds:
        dx = np.abs(xs - sx)
        dy = np.abs(ys - sy)
        dx = np.minimum(dx, B - dx)
        dy = np.minimum(dy, B - dy)
        d.append(np.sqrt(dx * dx + dy * dy))
    d = np.array(d)
    order = np.sort(d, axis=0)
    cell = np.argmin(d, axis=0)
    img = tint[cell].astype(float)
    img += (gen.integers(-8, 9, (B, B)) - 3.0 * order[0])[:, :, None]
    img[(order[1] - order[0]) < 0.6] *= 0.55          # the gaps between pebbles
    return np.clip(img, 0, 255).astype(np.uint8)


# A 5x7 pixel font for the sign texts, one string of 7 rows of 5 per
# glyph ('#' ink). Written out here rather than taken from PIL, so the
# PNGs come out the same whatever PIL is installed. Only the letters the
# texts use.
FONT5x7 = {
    " ": [".....", ".....", ".....", ".....", ".....", ".....", "....."],
    "!": ["..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#.."],
    "B": ["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."],
    "F": ["#####", "#....", "#....", "####.", "#....", "#....", "#...."],
    "K": ["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"],
    "a": [".....", ".....", ".###.", "....#", ".####", "#...#", ".####"],
    "d": ["....#", "....#", ".##.#", "#..##", "#...#", "#...#", ".####"],
    "e": [".....", ".....", ".###.", "#...#", "#####", "#....", ".###."],
    "f": ["..##.", ".#..#", ".#...", "###..", ".#...", ".#...", ".#..."],
    "h": ["#....", "#....", "#.##.", "##..#", "#...#", "#...#", "#...#"],
    "i": ["..#..", ".....", ".##..", "..#..", "..#..", "..#..", ".###."],
    "l": [".##..", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "n": [".....", ".....", "#.##.", "##..#", "#...#", "#...#", "#...#"],
    "o": [".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###."],
    "r": [".....", ".....", "#.##.", "##..#", "#....", "#....", "#...."],
    "s": [".....", ".....", ".####", "#....", ".###.", "....#", "####."],
    "t": [".#...", ".#...", "###..", ".#...", ".#...", ".#..#", "..##."],
    "u": [".....", ".....", "#...#", "#...#", "#...#", "#..##", ".##.#"],
    "w": [".....", ".....", "#...#", "#...#", "#.#.#", "#.#.#", ".#.#."],
}

# The sign texts, in VM_SIGN_0.. order (voxel_mesh.h lists them too):
# two lines each, at most 10 characters a line (6 texels each on 64).
SIGN_TEXTS = [("Kurt", "was here"), ("Wolfie", "was here"), ("Far Lands", "or Bust!")]


def sm_sign(lines):
    """A sign's front, 64x32 across a board 1 block wide and 1/2 high:
    the planks at four texels to one of theirs -- the same boards as the
    planks block, so it matches the rest of the sign -- with the text
    in dark ink, centred, two lines."""
    planks = sm_planks()
    img = np.repeat(np.repeat(planks, 4, axis=0), 4, axis=1)[:32, :64].astype(int)
    img[0, :] = img[-1, :] = (70, 50, 28)             # the board's edge
    img[:, 0] = img[:, -1] = (70, 50, 28)
    ink = (38, 26, 14)
    for row, text in enumerate(lines):
        width = 6 * len(text) - 1
        x0 = (64 - width) // 2
        y0 = 7 + row * 11
        for k, ch in enumerate(text):
            glyph = FONT5x7[ch]
            for gy in range(7):
                for gx in range(5):
                    if glyph[gy][gx] == "#":
                        img[y0 + gy, x0 + 6 * k + gx] = ink
    return img.astype(np.uint8)


def sm_torch_flame():
    """Torch flame, 64x8 like flame(): white-yellow core -> orange ->
    red tip."""
    w = 64
    stops = [(0.00, (255, 252, 214)), (0.20, (255, 222, 110)), (0.50, (255, 150, 40)),
             (0.80, (220, 70, 20)), (1.00, (120, 24, 8))]
    xs = np.linspace(0.0, 1.0, w)
    ramp = np.zeros((w, 3))
    for c in range(3):
        ramp[:, c] = np.interp(xs, [p for p, _ in stops], [col[c] for _, col in stops])
    streak = np.array([1.00, 0.92, 1.05, 0.95, 1.00, 0.90, 1.04, 0.96])[:, None, None]
    fade = np.clip((xs - 0.15) / 0.3, 0.0, 1.0)[None, :, None]
    img = ramp[None, :, :] * (1.0 + (streak - 1.0) * fade)
    return np.clip(img, 0, 255).astype(np.uint8)


# Keyed by the path under textures/. The generators share their seeded
# random streams in this order, so keep it: a reordered entry changes
# every texture after it.
# ---------------------------------------------------------------------
#  Farming (step 9): tilled soil, a composter, and five crops in four
#  growth stages each.
#
#  A CROP IS ONE FUNCTION, CALLED FOUR TIMES. The stage decides how tall
#  the plant stands and whether it is carrying anything, so the four
#  textures of a crop cannot drift apart -- and a sixth crop is a row in
#  CROPS below, not twenty new pictures.
# ---------------------------------------------------------------------


def sm_farmland(wet=False):
    """Tilled soil seen from above: dirt, combed into furrows. Wet is the
    same picture darker and browner, which is what water does to soil and
    is the whole of the player-facing difference (D-106)."""
    gen = sm_gen(60 if not wet else 61)
    lum = sm_dirt_lum(gen)
    # Four furrows running east-west, a groove and a ridge each. The
    # jitter along each one matters more than the contrast: a straight
    # dark line every four rows reads as PLANKS, which is what the first
    # attempt looked like. Broken, it reads as soil somebody has been
    # over with a hoe.
    for y in range(B):
        if y % 4 == 0:
            for x in range(B):
                lum[y, x] -= 14 + int(gen.integers(0, 9))
        elif y % 4 == 1:
            for x in range(B):
                lum[y, x] += 6 + int(gen.integers(0, 7))
    lum += gen.integers(-6, 7, (B, B))
    tint = (120, 86, 54) if not wet else (74, 52, 30)
    return sm_rgb(lum - (0 if not wet else 10), tint)


def sm_composter_side():
    """Staves and two hoops, like the chest but taller-looking: it is a
    wooden box, and it should read as one from across a field."""
    lum, _ = sm_planks_lum(62)
    for y in range(B):
        for x in range(B):
            if x % 4 == 3:
                lum[y, x] -= 30          # the gap between two staves
    for y in (3, 12):                    # the hoops
        lum[y, :] += 22
        lum[y + 1, :] -= 18
    return sm_rgb(lum, (156, 122, 70))


def sm_composter_top():
    """Open, with the scraps showing: a dark ring of wood round a middle
    of rotting green-brown."""
    lum, gen = sm_planks_lum(63)
    out = sm_rgb(lum, (150, 118, 68))
    for y in range(2, B - 2):
        for x in range(2, B - 2):
            v = int(gen.integers(-18, 19))
            c = (78 + v, 66 + v, 36 + v)
            out[y, x] = np.clip(np.array(c), 0, 255)
    return out


# THE FIVE CROPS, drawn so that a stage is a SHAPE and not a shade.
#
# The first version changed height and little else, and the user's
# verdict was that the plants "only slightly change color in their
# growth stages instead of actually growing". So each stage now differs
# in three ways at once -- how tall it is, how many stems it has, and
# what it is carrying -- and the ripe stage carries something the colour
# of the harvest, which is what a player is actually looking for when
# they walk past a field.
#
#   stage 0   two sprouts, a third of the tile, leaves only
#   stage 1   three stems, half the tile, first leaves spread
#   stage 2   four stems, three quarters, buds in the crop's own colour
#   stage 3   five stems, full height, heavy with the harvest
#
# One row per crop below; a sixth is a row and not a new function.
CROPS = {
    #          stem colour      leaf colour      fruit colour     what it carries
    # WHEAT GOES GOLD ALL OVER when it is ripe -- the user, 2026-09-29:
    # "the wheat should be fully yellow when ripe". `ripe_stem` and
    # `ripe_leaf` replace the green at the last stage, so a ripe field
    # reads as a colour from across the valley rather than as green with
    # a yellow fringe.
    "wheat":  dict(stem=(122, 150, 66), leaf=(140, 164, 74), fruit=(232, 202, 96), style="ear",
                   ripe_stem=(206, 174, 74), ripe_leaf=(222, 192, 92)),
    "potato": dict(stem=(70, 122, 52), leaf=(86, 142, 60), fruit=(232, 216, 120), style="flower"),
    "tomato": dict(stem=(74, 118, 54), leaf=(92, 140, 64), fruit=(212, 58, 42), style="berry"),
    # The pods are warmer than the leaves on purpose: a pod the same
    # green as the plant is a leaf as far as 16 pixels are concerned.
    "beans":  dict(stem=(78, 132, 60), leaf=(96, 152, 70), fruit=(198, 176, 92), style="pod"),
    "rice":   dict(stem=(104, 154, 78), leaf=(120, 168, 88), fruit=(214, 202, 112), style="blade"),
    # The upper half of the rice plant: taller, thinner, and it is the
    # half that carries the grain when it is ripe (blocks.h, BF2_TALL_TOP).
    "rice_top": dict(stem=(110, 158, 84), leaf=(126, 172, 94), fruit=(222, 210, 120), style="grain"),
}

STAGE_TOP = [11, 8, 4, 1]   # the y a plant reaches at each stage (0 is the top of the tile)
STAGE_STEMS = [2, 3, 4, 5]


def sm_crop(name, stage):
    """One growth stage of one crop, as a cut-out sprite for the crossed
    quads a K_PLANT is drawn with."""
    c = dict(CROPS[name])
    ripe = stage == 3
    if ripe and "ripe_stem" in c:
        c["stem"] = c["ripe_stem"]
        c["leaf"] = c["ripe_leaf"]
    gen = sm_gen(70 + 8 * list(CROPS).index(name) + stage)
    rgb = np.zeros((B, B, 3), np.uint8)
    holes = np.ones((B, B), bool)

    def px(x, y, col, jitter=10):
        if 0 <= x < B and 0 <= y < B:
            rgb[y, x] = np.clip(np.array(col) + gen.integers(-jitter, jitter + 1, 3), 0, 255)
            holes[y, x] = False

    top = STAGE_TOP[stage]
    n = STAGE_STEMS[stage]
    # The stems, spread evenly and leaning outwards from the middle, so a
    # grown plant is a sheaf rather than a comb.
    xs = [2 + int(round(i * 11.0 / max(1, n - 1))) for i in range(n)] if n > 1 else [8]
    for i, x0 in enumerate(xs):
        lean = (x0 - 8) // 5
        for y in range(top + (i % 2), B):
            t = (y - top) / max(1.0, float(B - top))
            xx = x0 + int(round(lean * (1.0 - t)))
            px(xx, y, c["stem"])
            # A second texel of width near the base: a stem that is one
            # pixel wide disappears at the distance a field is seen from.
            if y > B - 4:
                px(xx + 1, y, _shade_t(c["stem"], -18))

        # Leaves: short diagonal runs off the stem, more of them as the
        # plant grows.
        for k in range(stage + 1):
            ly = top + 2 + 3 * k
            if ly >= B - 1:
                break
            d = 1 if (i + k) % 2 == 0 else -1
            for j in range(1, 3):
                px(x0 + d * j, ly + j - 1, c["leaf"])

    if stage >= 2:
        style = c["style"]
        for i, x0 in enumerate(xs):
            # EVERY OTHER STEM, always. Five stems in twelve pixels are
            # three pixels apart, so a fruit on each one joins up into a
            # solid bar across the tile -- which is exactly what the
            # first tomato looked like.
            if i % 2:
                continue
            y0 = top + 1
            if style == "ear":            # wheat: a head of grain up the stalk
                for k in range(3 if stage == 3 else 2):
                    px(x0, y0 + k, c["fruit"], 6)
                    px(x0 + 1, y0 + k, _shade_t(c["fruit"], -22), 6)
            elif style == "flower":       # potato: small pale flowers
                px(x0, y0 + 1, c["fruit"], 6)
                if stage == 3:
                    px(x0 + 1, y0 + 2, _shade_t(c["fruit"], -22), 6)
            elif style == "berry":        # tomato: round fruit, hanging
                fy = y0 + 4
                px(x0, fy, c["fruit"], 4)
                px(x0 + 1, fy, _shade_t(c["fruit"], -18), 4)
                if stage == 3:
                    px(x0, fy + 1, _shade_t(c["fruit"], -26), 4)
                    px(x0 + 1, fy + 1, _shade_t(c["fruit"], -34), 4)
                    px(x0, fy - 1, _shade_t(c["fruit"], 28), 4)  # a highlight, so it reads as round
            elif style == "pod":          # beans: pods hanging along the
                                          # stem, two texels wide so they
                                          # are not mistaken for a leaf
                d = 1 if i % 4 == 0 else -1
                for k in range(3 if stage == 3 else 2):
                    px(x0 + d, y0 + 3 + k, c["fruit"], 5)
                    px(x0 + d + (1 if d > 0 else -1), y0 + 3 + k, _shade_t(c["fruit"], -26), 5)
            elif style == "grain":        # rice, upper half: drooping heads
                for k in range(3 if stage == 3 else 2):
                    px(x0 + (1 if i % 2 else -1) * (k // 2), y0 + k, c["fruit"], 6)
            else:                         # rice, lower half: blades tipped pale
                px(x0, y0, c["fruit"], 6)

    return sm_alpha(rgb, holes)


def _shade_t(rgb, d):
    return tuple(int(max(0, min(255, c + d))) for c in rgb)


def sm_item_hoe(rgb):
    """A handle with a blade turned over at right angles to it -- which is
    what tells a hoe from an axe at 16 px."""
    img = _icon()
    _tool_handle(img)
    _rect(img, 6, 3, 12, 5, rgb)
    _rect(img, 6, 3, 8, 6, _shade(rgb, 18))
    _rect(img, 6, 5, 12, 6, _shade(rgb, -24))
    return img


def sm_item_seeds(rgb):
    """Seeds: teardrops, not dots. Each one is three texels -- a bright
    head, a body and a dark tail -- which is the least that reads as a
    SEED rather than as grit, and they are scattered off the middle so
    the pile has a shape."""
    img = _icon()
    for x, y, d in ((5, 9, 1), (8, 5, -1), (11, 9, 1), (7, 12, -1), (10, 6, 1)):
        _dot(img, x, y, _shade(rgb, 26))
        _dot(img, x + d, y + 1, rgb)
        _dot(img, x + d, y + 2, _shade(rgb, -40))
        _dot(img, x + 2 * d, y + 2, _shade(rgb, -18))
    return img


def _blob(img, rgb, r, cx, cy, squash=1.0):
    """A shaded round body, lit from the top left."""
    for y in range(cy - r - 1, cy + r + 2):
        for x in range(cx - r - 1, cx + r + 2):
            dx, dy = (x - cx), (y - cy) * squash
            if dx * dx + dy * dy > r * r + 0.4:
                continue
            _dot(img, x, y, _shade(rgb, int(16 - 4.5 * (dx + dy))))


def sm_item_potato(rgb=(196, 156, 92)):
    """A potato: an OVAL, not a circle, with eyes. A plain shaded disc
    was the first attempt and read as a coin -- the eyes and the squash
    are the whole of what makes it a vegetable."""
    img = _icon()
    _blob(img, rgb, 5, 8, 8, squash=1.25)
    for x, y in ((6, 6), (10, 7), (7, 10), (11, 10)):
        _dot(img, x, y, _shade(rgb, -52))
        _dot(img, x + 1, y, _shade(rgb, -28))
    _dot(img, 5, 6, _shade(rgb, 34))   # a highlight where the light is
    _dot(img, 6, 5, _shade(rgb, 30))
    return img


def sm_item_tomato(rgb=(210, 56, 42)):
    """A tomato: a round red body with a green calyx on top and a
    highlight. The calyx is what tells it from an apple, and at 16 px it
    is the only thing that does."""
    img = _icon()
    _blob(img, rgb, 5, 8, 9)
    green = (86, 142, 58)
    for x, y in ((8, 3), (7, 4), (8, 4), (9, 4), (6, 5), (10, 5)):
        _dot(img, x, y, green)
    _dot(img, 8, 2, _shade(green, -30))       # the stalk
    _dot(img, 6, 7, _shade(rgb, 44))          # the shine
    _dot(img, 5, 8, _shade(rgb, 30))
    return img


def sm_item_round(rgb, r=4, cy=8, cx=8):
    """A plain shaded blob, for anything that has no shape of its own."""
    img = _icon()
    _blob(img, rgb, r, cx, cy)
    return img


def sm_item_wheat():
    """A sheaf: three gold stalks, each with grains up its length. One
    stalk read as a twig, and a single ear read as a feather."""
    img = _icon()
    gold = (230, 200, 96)
    for k, x0 in enumerate((5, 8, 11)):
        for y in range(4 + (k % 2), 14):
            _dot(img, x0, y, _shade(gold, -34))
        for y in range(4 + (k % 2), 11, 2):
            _dot(img, x0 - 1, y, gold)
            _dot(img, x0 + 1, y + 1, _shade(gold, -14))
    return img


def sm_item_beans():
    """Two pods, CURVED, with the beans swelling inside them -- plus
    three loose beans. Straight bars read as dashes and a single diagonal
    read as a twig; the curve is what says pod at this size."""
    img = _icon()
    pod = (168, 180, 88)
    bean = (206, 184, 96)
    for x0, y0, d in ((3, 5, 1), (7, 9, -1)):
        curve = (0, 0, 1, 1, 0)                    # a shallow arc
        for i, dy in enumerate(curve):
            x, y = x0 + i, y0 + dy * d
            _dot(img, x, y, pod)
            _dot(img, x, y + 1, _shade(pod, -34))
            if i in (1, 3):
                _dot(img, x, y, bean)              # a bean showing through
    for x, y in ((11, 4), (12, 7), (10, 12)):      # loose beans beside them
        _dot(img, x, y, bean)
        _dot(img, x, y + 1, _shade(bean, -38))
    return img


def sm_item_rice():
    """A heap of grains: a mound, like the compost, but pale and with the
    individual grains picked out so it is not read as snow."""
    img = _icon()
    gen = sm_gen(91)
    grain = (230, 226, 206)
    for j, y in enumerate(range(12, 6, -1)):
        w = 6 - j
        for x in range(8 - w, 8 + w):
            _dot(img, x, y, _shade(grain, int(gen.integers(-22, 10))))
    for x, y in ((6, 11), (9, 10), (7, 9), (10, 8), (8, 7)):
        _dot(img, x, y, (252, 250, 240))
    return img


def sm_item_compost():
    """Dark crumbly stuff in a heap, flecked so it is not a black hole in
    the slot -- the same problem sm_item_coal has, solved the same way."""
    img = _icon()
    gen = sm_gen(90)
    base = (84, 64, 44)
    for y in range(8, 14):
        w = (y - 6)
        for x in range(8 - w, 8 + w):
            v = int(gen.integers(-16, 17))
            _dot(img, x, y, _shade(base, v))
    for x, y in ((6, 10), (9, 9), (11, 12)):
        _dot(img, x, y, (128, 150, 70))    # a leaf that has not gone yet
    return img


# --- The animals themselves --------------------------------------------
#
# A creature's hide is ONE 16x16 texture put once on each box of its
# model (fred/beast.c), not a Minecraft-style unwrapped skin: the models
# are half a dozen boxes and an unwrap would be a layout to maintain for
# every one of them. So what these have to be is a PATTERN that reads
# the same whichever face it lands on -- which is what a breed marking
# is anyway.

def _blotches(gen, base, spot, n, rmin, rmax, edge=0):
    """A field of `base` with `n` soft-edged blobs of `spot` on it."""
    out = np.zeros((B, B, 3), np.uint8)
    for y in range(B):
        for x in range(B):
            v = int(gen.integers(-8, 9))
            out[y, x] = np.clip(np.array(base) + v, 0, 255)
    for _ in range(n):
        cx, cy = int(gen.integers(0, B)), int(gen.integers(0, B))
        r = float(gen.integers(rmin, rmax + 1))
        for y in range(B):
            for x in range(B):
                # Wrapped distance: the texture repeats across faces, so
                # a blotch that runs off one edge has to come back on the
                # other or the seam is a straight line of base colour.
                dx = min(abs(x - cx), B - abs(x - cx))
                dy = min(abs(y - cy), B - abs(y - cy))
                d = (dx * dx + dy * dy) ** 0.5
                if d > r + float(gen.integers(0, 2)):
                    continue
                v = int(gen.integers(-10, 11))
                out[y, x] = np.clip(np.array(spot) + v, 0, 255)
    if edge:
        for y in range(B):
            for x in range(B):
                if (x + y) % 7 == 0:
                    out[y, x] = np.clip(out[y, x].astype(int) - edge, 0, 255)
    return out


def sm_pig_hide():
    """OXFORD SANDY AND BLACK, the user's choice of breed: a sandy ginger
    pig with big irregular black blotches. The blotches are what the
    breed IS -- a plain sandy pig is a Tamworth -- so they are large and
    few rather than a speckle."""
    gen = sm_gen(80)
    return _blotches(gen, (206, 146, 96), (44, 38, 38), 5, 3, 5)


def sm_pig_face():
    """The same sandy ground with the breed's pale blaze down it."""
    img = _blotches(sm_gen(81), (212, 154, 104), (48, 40, 40), 2, 2, 3)
    for y in range(B):
        for x in range(6, 10):
            img[y, x] = np.clip(np.array((232, 194, 160)), 0, 255)
    return img


def sm_cow_hide():
    """AYRSHIRE, the user's choice: white with sharply edged red-brown
    patches. Sharply edged is the point -- a Hereford's markings are
    soft and a Holstein's are black, and at 16 px the edge is most of
    what tells them apart."""
    gen = sm_gen(82)
    return _blotches(gen, (238, 234, 226), (150, 72, 40), 4, 3, 5)


def sm_cow_face():
    """Mostly white, as an Ayrshire's face is, with brown round the eyes."""
    img = _blotches(sm_gen(83), (240, 236, 228), (150, 72, 40), 2, 2, 3)
    return img


def sm_sheep_wool():
    """Fleece: cream, and CURLY. A flat cream square reads as paper, so
    the texture is all short arcs -- at this size the curl is the whole
    of what says wool."""
    gen = sm_gen(84)
    out = np.zeros((B, B, 3), np.uint8)
    base = (236, 232, 220)
    for y in range(B):
        for x in range(B):
            v = int(gen.integers(-10, 11))
            out[y, x] = np.clip(np.array(base) + v, 0, 255)
    for cy in range(1, B, 4):
        for cx in range(1, B, 4):
            ox, oy = int(gen.integers(-1, 2)), int(gen.integers(-1, 2))
            for dx, dy in ((0, 0), (1, 0), (2, 1), (0, 1), (2, 0), (1, 2)):
                x, y = (cx + ox + dx) % B, (cy + oy + dy) % B
                out[y, x] = np.clip(np.array(base) - 26, 0, 255)
    return out


def sm_sheep_shorn():
    """A sheep that has just been sheared: pink skin with the stubble
    still on it. Its own texture rather than a tint, so a shorn sheep is
    unmistakable across a field -- which is the point of shearing one."""
    gen = sm_gen(85)
    out = np.zeros((B, B, 3), np.uint8)
    base = (226, 184, 176)
    for y in range(B):
        for x in range(B):
            v = int(gen.integers(-9, 10))
            out[y, x] = np.clip(np.array(base) + v, 0, 255)
    for i in range(24):
        x, y = int(gen.integers(0, B)), int(gen.integers(0, B))
        out[y, x] = np.clip(np.array(base) - 30, 0, 255)
    return out


def sm_sheep_face():
    """A dark face, which is what a white sheep has."""
    return _blotches(sm_gen(86), (72, 64, 58), (52, 46, 42), 3, 2, 4)


# --- The animals' two machines, and the fence (step 10) ----------------

def sm_barrel_side():
    """The cheese maker: staves and three hoops, paler and tighter than
    the composter's, because this one holds a liquid and should look as
    though it does."""
    lum, _ = sm_planks_lum(70)
    for y in range(B):
        for x in range(B):
            if x % 5 == 4:
                lum[y, x] -= 26          # the seam between two staves
    for y in (1, 7, 13):                 # three hoops, evenly spaced
        lum[y, :] += 26
        lum[y + 1, :] -= 20
    return sm_rgb(lum, (168, 132, 78))


def sm_barrel_top():
    """The rim and the boards of its floor, seen when it is empty."""
    lum, gen = sm_planks_lum(71)
    out = sm_rgb(lum - 14, (150, 116, 66))
    for y in range(3, B - 3):
        for x in range(3, B - 3):
            v = int(gen.integers(-10, 11))
            out[y, x] = np.clip(np.array((104 + v, 80 + v, 46 + v)), 0, 255)
    return out


def _liquid(tag, base, spots):
    """A still surface: the colour, a little noise, and a highlight in
    one corner so it reads as a SURFACE and not as a flat fill."""
    gen = sm_gen(tag)
    out = np.zeros((B, B, 3), np.uint8)
    for y in range(B):
        for x in range(B):
            v = int(gen.integers(-8, 9))
            out[y, x] = np.clip(np.array(base) + v, 0, 255)
    for x, y in spots:
        out[y, x] = np.clip(np.array(base) + 26, 0, 255)
    return out


def sm_milk():
    """Milk standing in the barrel: near-white, barely textured."""
    return _liquid(72, (238, 236, 226), ((4, 4), (5, 4), (4, 5), (11, 10)))


def sm_cheese():
    """Cheese: yellow-orange, with holes in it. The holes are what tell
    it from butter, and at 16 px they are the only thing that does."""
    img = _liquid(73, (226, 176, 70), ())
    for x, y in ((4, 5), (10, 4), (7, 9), (12, 11), (3, 11)):
        for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
            img[y + dy, x + dx] = np.clip(np.array((176, 130, 44)), 0, 255)
    return img


def sm_sausage_side():
    """The sausage maker: an iron box with a hopper mouth and a crank.
    It is the only machine made of metal, so it should not read as the
    furnace -- hence the mouth across the top third rather than a door."""
    gen = sm_gen(74)
    lum = np.full((B, B), 148, np.int32) + gen.integers(-10, 11, (B, B))
    lum[0:2, :] += 22                      # a bright top edge
    lum[4:7, 2:14] -= 46                   # the mouth
    lum[7:8, 2:14] += 20                   # its lip
    for y in range(9, 14):                 # the crank housing
        lum[y, 10:14] -= 16
    lum[11, 11:15] += 40
    return sm_rgb(lum, (150, 152, 158))


def sm_sausage_top():
    """Plain plate with a seam, so a row of them does not shimmer."""
    gen = sm_gen(75)
    lum = np.full((B, B), 140, np.int32) + gen.integers(-8, 9, (B, B))
    lum[:, 7:9] -= 24
    return sm_rgb(lum, (146, 148, 154))


# --- What the animals are worth, in a slot ------------------------------

def _meat(img, rgb, fat):
    """A cut of meat: a red body with a rind of fat along one side. The
    rind is what stops raw pork and raw beef being the same pink blob."""
    _blob(img, rgb, 5, 8, 9, squash=1.1)
    for x, y in ((4, 7), (4, 8), (5, 6), (5, 10), (4, 9)):
        _dot(img, x, y, fat)
    _dot(img, 10, 7, _shade(rgb, 34))
    return img


def sm_item_pork():
    return _meat(_icon(), (226, 132, 124), (244, 232, 220))


def sm_item_beef():
    return _meat(_icon(), (172, 62, 58), (238, 226, 212))


def sm_item_cheese():
    """A wedge, seen from the side, with two holes in the cut face."""
    img = _icon()
    rgb = (230, 184, 76)
    for i, y in enumerate(range(5, 13)):
        _rect(img, 4, y, 5 + i, y + 1, _shade(rgb, 12 - 3 * i))
    for x, y in ((6, 9), (8, 11)):
        _dot(img, x, y, _shade(rgb, -60))
        _dot(img, x + 1, y, _shade(rgb, -44))
    return img


def _sausage(rgb):
    """A curved link, tied at both ends. The tie is two dark texels, and
    it is what makes a sausage rather than a slug."""
    img = _icon()
    path = ((4, 11), (5, 10), (6, 9), (7, 8), (8, 7), (9, 6), (10, 5), (11, 5))
    for x, y in path:
        _dot(img, x, y, rgb)
        _dot(img, x + 1, y, _shade(rgb, 20))
        _dot(img, x, y + 1, _shade(rgb, -36))
        _dot(img, x + 1, y + 1, _shade(rgb, -16))
    for x, y in ((3, 12), (12, 4)):
        _dot(img, x, y, _shade(rgb, -70))
    return img


def sm_item_sausage():
    return _sausage((176, 88, 66))


def sm_item_sausage_veg():
    """The bean one. The same shape in a browner, duller colour -- they
    are worth the same and a player should be able to tell them apart in
    the slot without reading the label."""
    return _sausage((146, 108, 62))


def sm_item_bone():
    """A bone, and the KNUCKLES are the whole picture: a shaft with two
    lobes at each end, the lobes set across the shaft so they read as
    knobs rather than as more shaft.

    The first version drew the lobes along the same diagonal as the
    shaft, so they merged into it and the icon was a plain stick."""
    img = _icon()
    white = (238, 234, 222)
    dark = _shade(white, -46)
    mid = _shade(white, -20)

    # The shaft, two texels thick along the diagonal.
    for i in range(6):
        _dot(img, 5 + i, 10 - i, white)
        _dot(img, 6 + i, 10 - i, mid)
        _dot(img, 6 + i, 11 - i, dark)     # its shaded underside

    # Four lobes, in pairs, set ACROSS the shaft at each end. Each is a
    # 2 x 2 knob with one bright texel and one dark one, which is the
    # least that reads as round at this size.
    for cx, cy in ((3, 9), (5, 12), (10, 2), (12, 5)):
        _dot(img, cx, cy, white)
        _dot(img, cx + 1, cy, mid)
        _dot(img, cx, cy + 1, mid)
        _dot(img, cx + 1, cy + 1, dark)
    # ... joined to the shaft, so the knobs are not floating.
    for x, y in ((4, 10), (5, 11), (11, 3), (11, 4)):
        _dot(img, x, y, mid)
    return img


def sm_item_fence(gate=False):
    """A fence in the hand: two posts and two rails. The gate is the same
    picture with the rails pulled in between the posts, which is what
    the block itself looks like from the side."""
    img = _icon()
    wood = (168, 132, 78)
    for x in (4, 11):
        _rect(img, x, 2 if not gate else 4, x + 2, 14, wood)
        _rect(img, x, 2 if not gate else 4, x + 1, 14, _shade(wood, 20))
    x0, x1 = (1, 15) if not gate else (6, 10)
    for y in (5, 10):
        _rect(img, x0, y, x1, y + 2, _shade(wood, -14))
        _rect(img, x0, y, x1, y + 1, _shade(wood, 6))
    return img


def sm_item_bucket_milk():
    return _bucket((238, 236, 226))


# --- What a sheep is worth, and what wool becomes ----------------------

def sm_item_mutton():
    return _meat(_icon(), (198, 96, 88), (240, 230, 216))


def sm_item_mutton_mash():
    """A plate with a mound of mash and a cut of meat beside it. The
    PLATE is what makes it a dish rather than another lump of food: an
    ellipse under everything, lit at its far rim and dark at the near
    one, so the two things on it are ON something."""
    img = _icon()
    plate = (188, 190, 198)
    mash = (240, 226, 170)
    meat = (176, 74, 62)
    for i, y in enumerate(range(9, 14)):
        half = (6, 6, 5, 4, 2)[i]
        _rect(img, 8 - half, y, 9 + half, y + 1, _shade(plate, 14 - 11 * i))
    _blob(img, mash, 3, 6, 8, squash=1.2)
    _dot(img, 5, 6, _shade(mash, 26))
    _dot(img, 6, 6, _shade(mash, 26))
    _blob(img, meat, 2, 11, 8, squash=1.1)
    for x, y in ((12, 7), (12, 8)):
        _dot(img, x, y, (238, 226, 212))
    return img


def sm_item_kebab():
    """A skewer: a STRAIGHT stick with four lumps threaded on it, which
    is how it stays apart from the sausage's curve at this size. Meat,
    tomato, bean, meat -- the recipe in order, so the icon names its own
    ingredients.

    The lumps are 3x3 rather than round, and three apart: at four apart
    the skewer runs off the tile, and round ones at three apart ran into
    each other and made a caterpillar. A texel of bare stick between
    each pair is the whole difference."""
    img = _icon()
    stick = (150, 112, 62)
    for i in range(14):
        _dot(img, 1 + i, 14 - i, _shade(stick, 16))
        _dot(img, 2 + i, 14 - i, _shade(stick, -30))
    lumps = (((182, 76, 62), 4, 11), ((206, 58, 44), 7, 8), ((122, 160, 74), 10, 5), ((182, 76, 62), 13, 2))
    for rgb, cx, cy in lumps:
        _rect(img, cx - 1, cy - 1, cx + 2, cy + 2, rgb)
        _rect(img, cx - 1, cy - 1, cx + 2, cy, _shade(rgb, 26))     # lit along the top
        _rect(img, cx - 1, cy + 1, cx + 2, cy + 2, _shade(rgb, -34))  # and dark underneath
    return img


def sm_item_wool():
    """A fleece: a pale curly lump, the same curls as the sheep."""
    img = _icon()
    base = (238, 234, 222)
    _blob(img, base, 5, 8, 8, squash=1.1)
    for x, y in ((5, 6), (8, 5), (11, 7), (6, 10), (10, 11), (8, 8)):
        _dot(img, x, y, _shade(base, -30))
        _dot(img, x + 1, y + 1, _shade(base, -18))
    return img


def sm_item_shears():
    """Two blades crossed on a pivot. The X is the whole silhouette --
    one blade reads as a knife."""
    img = _icon()
    steel = (208, 212, 220)
    for i in range(7):
        _dot(img, 3 + i, 3 + i, steel)
        _dot(img, 4 + i, 3 + i, _shade(steel, -34))
        _dot(img, 12 - i, 3 + i, steel)
        _dot(img, 11 - i, 3 + i, _shade(steel, -34))
    _dot(img, 8, 8, (120, 124, 132))          # the pivot
    for x, y in ((3, 12), (4, 13), (12, 12), (11, 13)):
        _dot(img, x, y, (90, 96, 104))        # the handles
    return img


def sm_item_string():
    """A loose coil. Thin, pale, and wound -- a straight line reads as a
    stick."""
    img = _icon()
    pale = (226, 222, 210)
    path = ((4, 11), (5, 9), (7, 8), (9, 8), (11, 9), (11, 11), (9, 12), (7, 12), (5, 11), (4, 9), (6, 7), (9, 6))
    for x, y in path:
        _dot(img, x, y, pale)
    for x, y in ((6, 10), (8, 10), (10, 10)):
        _dot(img, x, y, _shade(pale, -40))
    return img


def sm_item_rod():
    """A rod on the diagonal with a line hanging off the tip and a float
    on the end of it. The float is what makes it a FISHING rod rather
    than a stick."""
    img = _icon()
    wood = (176, 136, 72)
    for i in range(12):
        _dot(img, 2 + i, 13 - i, wood)
        _dot(img, 3 + i, 13 - i, _shade(wood, -34))
    for y in range(3, 11):
        _dot(img, 14, y, (232, 228, 216))       # the line
    _dot(img, 14, 11, (216, 56, 48))            # ... and the float
    _dot(img, 14, 12, (238, 234, 226))
    return img


def sm_item_fish(body, belly):
    """A fish from the side: a body, a paler belly, a tail fin and an
    eye. The tail is the silhouette; without it this is a bean."""
    img = _icon()
    _blob(img, body, 4, 9, 8, squash=1.5)
    for x in range(6, 13):
        _dot(img, x, 10, belly)
    for y, w in ((6, 1), (7, 2), (8, 3), (9, 2), (10, 1)):
        for x in range(4 - w, 5):
            _dot(img, x, y, _shade(body, -18))  # the tail
    _dot(img, 11, 7, (32, 30, 30))              # the eye
    _dot(img, 9, 5, _shade(body, 26))           # a dorsal highlight
    return img


def sm_item_shrimp():
    """A shrimp: curled, segmented, with a fan of a tail."""
    img = _icon()
    body = (240, 158, 120)
    path = ((5, 10), (5, 8), (6, 6), (8, 5), (10, 5), (11, 7), (11, 9))
    for i, (x, y) in enumerate(path):
        _dot(img, x, y, body if i % 2 == 0 else _shade(body, -26))
        _dot(img, x + 1, y, _shade(body, 14))
        _dot(img, x, y + 1, _shade(body, -38))
    for x, y in ((11, 11), (12, 12), (10, 12)):
        _dot(img, x, y, _shade(body, -14))      # the tail fan
    _dot(img, 9, 4, (40, 34, 32))               # the eye
    return img


def sm_item_bed():
    """A bed from the side, and what makes it read as one is the
    LAYERS: a dark frame, a red mattress with a lit top edge, a white
    pillow standing proud at the head, and legs with daylight between
    them.

    The first version filled the tile edge to edge in one dark tone
    with a white corner -- a slab with a stain on it."""
    img = _icon()
    frame = (128, 92, 52)
    quilt = (190, 58, 54)
    pillow = (240, 238, 230)

    # Legs first, so the frame sits on them. Daylight between the pair
    # is most of what says "furniture" rather than "block".
    _rect(img, 3, 12, 5, 15, _shade(frame, -30))
    _rect(img, 11, 12, 13, 15, _shade(frame, -30))
    # The frame rail, with a dark lip under it.
    _rect(img, 2, 10, 14, 12, frame)
    _rect(img, 2, 11, 14, 12, _shade(frame, -34))
    # The quilt on top of it, lit along its upper edge and creased once
    # so it is cloth rather than a painted band.
    _rect(img, 2, 7, 14, 10, quilt)
    _rect(img, 2, 7, 14, 8, _shade(quilt, 26))
    for y in range(7, 10):
        _dot(img, 9, y, _shade(quilt, -30))
    # The pillow at the head, standing above the quilt line.
    _rect(img, 3, 5, 8, 8, pillow)
    _rect(img, 3, 5, 8, 6, _shade(pillow, 12))
    _rect(img, 3, 7, 8, 8, _shade(pillow, -34))
    return img


def sm_bed_top(head=False):
    """Seen from above: the quilt, and the pillow on the head half."""
    gen = sm_gen(88 if head else 89)
    out = np.zeros((B, B, 3), np.uint8)
    for y in range(B):
        for x in range(B):
            v = int(gen.integers(-8, 9))
            out[y, x] = np.clip(np.array((186, 58, 54)) + v, 0, 255)
    for y in range(B):                              # a seam down the middle
        out[y, 7] = np.clip(np.array((150, 42, 40)), 0, 255)
    if head:
        for y in range(1, 7):
            for x in range(2, 14):
                v = int(gen.integers(-6, 7))
                out[y, x] = np.clip(np.array((238, 236, 228)) + v, 0, 255)
    return out


def sm_bed_side():
    """The frame, with the mattress sitting on it."""
    lum, _ = sm_planks_lum(90)
    out = sm_rgb(lum, (150, 112, 64))
    for y in range(0, 7):
        for x in range(B):
            out[y, x] = np.clip(np.array((188, 58, 54)) + (x % 3) * 4, 0, 255)
    return out


def sm_item_worm():
    """A curled worm. Pink-brown, two texels thick, so it reads at slot
    size as a body rather than a line."""
    img = _icon()
    body = (196, 128, 120)
    path = ((5, 10), (6, 11), (7, 11), (8, 10), (9, 9), (10, 9), (11, 8), (11, 7), (10, 6), (9, 6))
    for x, y in path:
        _dot(img, x, y, body)
        _dot(img, x, y + 1, _shade(body, -34))
    _dot(img, 9, 5, _shade(body, 18))
    return img


TEXTURES = {
    "grass_top.png": sm_grass_top,
    "grass_side.png": sm_grass_side,
    "dirt.png": sm_dirt,
    "stone.png": sm_stone,
    "cobble.png": sm_cobble,
    "sand.png": sm_sand,
    "water.png": sm_water,
    "log_side.png": sm_log_side,
    "log_top.png": sm_log_top,
    "planks.png": sm_planks,
    "leaves.png": sm_leaves,
    "coal_ore.png": sm_coal_ore,
    "miner_face.png": sm_face,
    "torch_flame.png": sm_torch_flame,
    "flower_red.png": sm_flower_red,
    "flower_yellow.png": sm_flower_yellow,
    "tall_grass.png": sm_tall_grass,
    "glass.png": sm_glass,
    "torch.png": sm_torch,
    "torch_1.png": lambda: sm_torch_frame(1),
    "torch_2.png": lambda: sm_torch_frame(2),
    "torch_3.png": lambda: sm_torch_frame(3),
    "item_torch.png": sm_item_torch,
    "leaves_fast.png": sm_leaves_fast,
    "bedrock.png": sm_bedrock,
    "gravel.png": sm_gravel,
    "iron_ore.png": sm_iron_ore,
    "birch_side.png": sm_birch_side,
    "birch_top.png": sm_birch_top,
    "birch_leaves.png": sm_birch_leaves,
    "birch_leaves_fast.png": sm_birch_leaves_fast,
    "cactus.png": sm_cactus,
    "snow.png": sm_snow,
    "sandstone.png": sm_sandstone,
    "chest_top.png": sm_chest_top,
    "chest_side.png": sm_chest_side,
    "trash_top.png": sm_trash_top,
    "trash_side.png": sm_trash_side,
    "bench_top.png": sm_bench_top,
    "item_coal.png": sm_item_coal,
    "item_iron_ingot.png": sm_item_ingot,
    "item_pickaxe_iron.png": lambda: sm_item_pickaxe((222, 222, 228)),
    "item_axe_iron.png": lambda: sm_item_axe((222, 222, 228)),
    "item_shovel_iron.png": lambda: sm_item_shovel((222, 222, 228)),
    "item_stick.png": sm_item_stick,
    # The same water without its holes, for SE_TRI_BLEND (M in game).
    "water_blend.png": lambda: sm_water(cut=False),
    "item_bucket.png": sm_item_bucket,
    "item_bucket_water.png": sm_item_bucket_water,
    "item_pickaxe_wood.png": lambda: sm_item_pickaxe((176, 128, 64)),
    "item_pickaxe_stone.png": lambda: sm_item_pickaxe((144, 152, 160)),
    "item_axe_wood.png": lambda: sm_item_axe((192, 136, 72)),
    "item_axe_stone.png": lambda: sm_item_axe((160, 168, 176)),
    "item_shovel_wood.png": lambda: sm_item_shovel((160, 120, 56)),
    "item_shovel_stone.png": lambda: sm_item_shovel((136, 143, 152)),
    "furnace_front.png": sm_furnace_front,
    "furnace_top.png": sm_furnace_top,
    "table_top.png": sm_table_top,
    "table_side.png": sm_table_side,
    "sign_kurt.png": lambda: sm_sign(SIGN_TEXTS[0]),
    "sign_wolfie.png": lambda: sm_sign(SIGN_TEXTS[1]),
    "sign_flob.png": lambda: sm_sign(SIGN_TEXTS[2]),
    # Farming (step 9).
    "farmland.png": sm_farmland,
    "farmland_wet.png": lambda: sm_farmland(wet=True),
    "composter_top.png": sm_composter_top,
    "composter_side.png": sm_composter_side,
    "wheat_0.png": lambda n="wheat", s=0: sm_crop(n, s),
    "wheat_1.png": lambda n="wheat", s=1: sm_crop(n, s),
    "wheat_2.png": lambda n="wheat", s=2: sm_crop(n, s),
    "wheat_3.png": lambda n="wheat", s=3: sm_crop(n, s),
    "potato_0.png": lambda n="potato", s=0: sm_crop(n, s),
    "potato_1.png": lambda n="potato", s=1: sm_crop(n, s),
    "potato_2.png": lambda n="potato", s=2: sm_crop(n, s),
    "potato_3.png": lambda n="potato", s=3: sm_crop(n, s),
    "tomato_0.png": lambda n="tomato", s=0: sm_crop(n, s),
    "tomato_1.png": lambda n="tomato", s=1: sm_crop(n, s),
    "tomato_2.png": lambda n="tomato", s=2: sm_crop(n, s),
    "tomato_3.png": lambda n="tomato", s=3: sm_crop(n, s),
    "beans_0.png": lambda n="beans", s=0: sm_crop(n, s),
    "beans_1.png": lambda n="beans", s=1: sm_crop(n, s),
    "beans_2.png": lambda n="beans", s=2: sm_crop(n, s),
    "beans_3.png": lambda n="beans", s=3: sm_crop(n, s),
    "rice_0.png": lambda n="rice", s=0: sm_crop(n, s),
    "rice_1.png": lambda n="rice", s=1: sm_crop(n, s),
    "rice_2.png": lambda n="rice", s=2: sm_crop(n, s),
    "rice_3.png": lambda n="rice", s=3: sm_crop(n, s),
    "rice_top_0.png": lambda s=0: sm_crop("rice_top", s),
    "rice_top_1.png": lambda s=1: sm_crop("rice_top", s),
    "rice_top_2.png": lambda s=2: sm_crop("rice_top", s),
    "rice_top_3.png": lambda s=3: sm_crop("rice_top", s),
    "item_hoe_wood.png": lambda: sm_item_hoe((192, 140, 72)),
    "item_hoe_stone.png": lambda: sm_item_hoe((150, 158, 166)),
    "item_hoe_iron.png": lambda: sm_item_hoe((222, 222, 228)),
    "item_wheat_seeds.png": lambda: sm_item_seeds((186, 176, 108)),
    "item_tomato_seeds.png": lambda: sm_item_seeds((196, 172, 124)),
    "item_wheat.png": sm_item_wheat,
    "item_potato.png": sm_item_potato,
    "item_tomato.png": sm_item_tomato,
    "item_beans.png": sm_item_beans,
    "item_rice.png": sm_item_rice,
    "item_compost.png": sm_item_compost,
    "item_worm.png": sm_item_worm,
    # Step 10: the animals, their two machines and the fence.
    "barrel_side.png": sm_barrel_side,
    "barrel_top.png": sm_barrel_top,
    "milk.png": sm_milk,
    "cheese.png": sm_cheese,
    "sausage_side.png": sm_sausage_side,
    "sausage_top.png": sm_sausage_top,
    "item_pork.png": sm_item_pork,
    "item_beef.png": sm_item_beef,
    "item_bucket_milk.png": sm_item_bucket_milk,
    "item_cheese.png": sm_item_cheese,
    "item_sausage.png": sm_item_sausage,
    "item_sausage_veg.png": sm_item_sausage_veg,
    "item_bone.png": sm_item_bone,
    "item_fence.png": lambda: sm_item_fence(False),
    "item_fence_gate.png": lambda: sm_item_fence(True),
    # The animals' own hides (fred/beast.c), and the sheep round them.
    "pig_hide.png": sm_pig_hide,
    "pig_face.png": sm_pig_face,
    "cow_hide.png": sm_cow_hide,
    "cow_face.png": sm_cow_face,
    "sheep_wool.png": sm_sheep_wool,
    "sheep_shorn.png": sm_sheep_shorn,
    "sheep_face.png": sm_sheep_face,
    "bed_top.png": lambda: sm_bed_top(False),
    "bed_head.png": lambda: sm_bed_top(True),
    "bed_side.png": sm_bed_side,
    "item_mutton.png": sm_item_mutton,
    "item_mutton_mash.png": sm_item_mutton_mash,
    "item_kebab.png": sm_item_kebab,
    "item_wool.png": sm_item_wool,
    "item_shears.png": sm_item_shears,
    "item_string.png": sm_item_string,
    # NAMED AFTER THE BLOCK, not after the thing: a block with
    # BF2_ITEM_ICON is asked for as item_<block name>.png, and the
    # block is bed_foot (F-128).
    "item_bed_foot.png": sm_item_bed,
    # Fishing (step 12).
    "item_fishing_rod.png": sm_item_rod,
    "item_sardine.png": lambda: sm_item_fish((186, 196, 206), (228, 234, 240)),
    "item_salmon.png": lambda: sm_item_fish((222, 124, 78), (244, 186, 150)),
    "item_shrimp.png": sm_item_shrimp,
}


def main():
    tiles = []
    for name, fn in TEXTURES.items():
        img = fn()
        (OUT / name).parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(img, "RGBA" if img.shape[2] == 4 else "RGB").save(OUT / name, optimize=True)
        # The contact sheet shows holes in magenta, like the engine's key.
        if img.shape[2] == 4:
            img = np.where(img[:, :, 3:] < 128, np.array([255, 0, 255], np.uint8), img[:, :, :3])
        tiles.append(img)
        print(f"wrote {OUT / name}")
    if "--preview" in sys.argv:
        # Each texture repeated 2x2 (so seams and wrap-around are visible)
        # and scaled 4x, side by side.
        # Each texture 2x2, padded to the tallest so they sit side by side.
        tall = max(t.shape[0] for t in tiles) * 2
        blocks = [np.tile(t, (2, 2, 1)) for t in tiles]
        blocks = [np.pad(b, ((0, tall - b.shape[0]), (0, 4), (0, 0))) for b in blocks]
        row = np.concatenate(blocks, axis=1)
        prev = Image.fromarray(row, "RGB").resize((row.shape[1] * 4, row.shape[0] * 4), Image.NEAREST)
        path = Path(sys.argv[sys.argv.index("--preview") + 1]) if len(sys.argv) > sys.argv.index("--preview") + 1 \
            else OUT.parent / "build" / "textures_preview.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        prev.save(path)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
