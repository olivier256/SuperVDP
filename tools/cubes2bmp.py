#!/usr/bin/env python3
"""
Dessine les sprites de la démo DDS 32 : des cubes isométriques de
32 x 32 pixels, en BMP 8 bits indexés (Pillow), avec une palette commune.

    python3 tools/cubes2bmp.py demo/assets

Géométrie (voir dds32/world.h) : une case de la grille fait un losange de
32 x 16 pixels à l'écran, un niveau de hauteur 16 pixels. Le cube est un
hexagone :
  - dessus : losange, lignes 0 à 15 ; demi-largeur 2, 4, ... 16, 16, 14,
    ... 2 ;
  - faces : lignes 8 à 31, face gauche (x < 16) et face droite (x >= 16),
    sous le losange ; le bas reprend la moitié basse du losange, 16 lignes
    plus bas.
Deux cubes voisins se recouvrent de 2 pixels sur leurs bords : dessinés de
l'arrière vers l'avant, le cube de devant recouvre l'autre.

Palette : 0 transparent (jamais affiché), 1 fond, 2 contour, puis des
dégradés de 8 nuances (du plus sombre au plus clair) :
  herbe 16-23, terre 24-31, pierre 32-39, bois 40-47, eau 48-55.
"""

import random
import sys

from PIL import Image

SIZE = 32

RAMPS = {
    "grass": (16, [(18, 40, 16), (26, 58, 20), (36, 78, 26), (48, 98, 32),
                   (62, 120, 38), (80, 142, 46), (104, 164, 58), (136, 188, 80)]),
    "dirt": (24, [(36, 22, 14), (52, 32, 20), (70, 44, 26), (88, 58, 34),
                  (108, 72, 42), (128, 88, 52), (150, 106, 66), (172, 128, 84)]),
    "stone": (32, [(30, 30, 38), (44, 44, 54), (60, 60, 72), (78, 78, 92),
                   (98, 98, 112), (120, 120, 134), (144, 144, 156), (170, 170, 180)]),
    "wood": (40, [(44, 24, 10), (64, 36, 14), (86, 50, 20), (108, 66, 28),
                  (132, 84, 38), (156, 104, 50), (180, 128, 66), (204, 156, 90)]),
    "water": (48, [(10, 24, 60), (14, 36, 90), (20, 52, 120), (28, 70, 150),
                   (40, 92, 176), (60, 118, 200), (92, 150, 220), (150, 196, 240)]),
}
BACKGROUND = 1
OUTLINE = 2


def palette():
    pal = [(0, 0, 0)] * 256
    pal[BACKGROUND] = (12, 14, 30)
    pal[OUTLINE] = (8, 8, 12)
    for first, colors in RAMPS.values():
        for i, c in enumerate(colors):
            pal[first + i] = c
    flat = []
    for c in pal:
        flat += c
    return flat


def shade(ramp, level):
    first, colors = RAMPS[ramp]
    return first + max(0, min(len(colors) - 1, level))


def iso_row_width(r):
    """Largeur de la ligne r (0 à 15) d'un triangle du treillis."""
    return 2 * (r + 1) if r < 8 else 2 * (15 - r)


# Emplacements du cube : (colonne, rangée) -> face (voir dds32/iso.h)
SLOTS = {(0, 0): "top", (1, 0): "top", (0, 1): "left", (0, 2): "left",
         (1, 1): "right", (1, 2): "right"}


def region(x, y):
    """'top', 'left', 'right' ou None (transparent), d'après le treillis."""
    if not (0 <= x < SIZE and 0 <= y < SIZE):
        return None
    c, lx = x // 16, x % 16
    t, r = y // 8, y % 8
    for tt, rr in ((t, r), (t - 1, r + 8)):
        w = iso_row_width(rr)
        points_left = (c + tt) % 2 == 0
        start = 16 - w if points_left else 0
        if start <= lx < start + w:
            return SLOTS.get((c, tt))
    return None


def edge(x, y):
    """Vrai sur le contour de l'hexagone et sur les arêtes entre faces."""
    here = region(x, y)
    if here is None:
        return False
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        nx, ny = x + dx, y + dy
        if not (0 <= nx < SIZE and 0 <= ny < SIZE) or region(nx, ny) is None:
            return True
    return False


def make_cube(top, left, right):
    """top, left, right : fonctions (x, y, rng) -> index de palette."""
    img = Image.new("P", (SIZE, SIZE), 0)
    img.putpalette(palette())
    rng = random.Random(SIZE)
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r = region(x, y)
            if r is None:
                continue
            if edge(x, y):
                px[x, y] = OUTLINE
            else:
                px[x, y] = {"top": top, "left": left, "right": right}[r](x, y, rng)
    return img


# ---- Matières ---------------------------------------------------------------

def grass_top(x, y, rng):
    return shade("grass", 4 + rng.choice((-1, 0, 0, 1, 2)))


def dirt_side(base):
    def f(x, y, rng):
        depth = y - (8 + abs(x - 15.5) // 2)  # lignes sous le bord du dessus
        if depth < 3:
            return shade("grass", base + 1 - (depth == 2))
        return shade("dirt", base + rng.choice((-1, 0, 0, 1)))
    return f


def stone_top(x, y, rng):
    # dalles : un joint tous les 8 pixels le long des deux axes du losange
    u, v = (x + 2 * y) % 16, (2 * y - x) % 16
    if u in (0, 1) or v in (0, 1):
        return shade("stone", 3)
    return shade("stone", 6 + rng.choice((-1, 0, 0, 0)))


def stone_side(base, left):
    def f(x, y, rng):
        # briques de 8 x 4 pixels, qui suivent la pente de la face
        k = x if left else 31 - x
        row = (y - k // 2) // 4
        joint_h = (y - k // 2) % 4 == 0
        joint_v = (k + (4 if row % 2 else 0)) % 8 == 0
        if joint_h or joint_v:
            return shade("stone", base - 2)
        return shade("stone", base + rng.choice((-1, 0, 0)))
    return f


def wood_top(x, y, rng):
    u, v = (x + 2 * y) % 32, (2 * y - x) % 32
    if u < 3 or v < 3 or u > 29 or v > 29:
        return shade("wood", 6)
    if (x + 2 * y) % 8 == 0:
        return shade("wood", 3)
    return shade("wood", 5 + rng.choice((-1, 0, 0)))


def wood_side(base, left):
    def f(x, y, rng):
        k = x if left else 31 - x
        yy = y - k // 2  # ligne le long de la face
        if k < 2 or k > 13 or yy < 10 or yy > 21:
            return shade("wood", base + 1)  # cadre
        if (yy - 10) % 4 == 0:
            return shade("wood", base - 2)  # joint de planche
        return shade("wood", base + rng.choice((-1, 0, 0)))
    return f


def water_top(x, y, rng):
    if (x + 2 * y + rng.choice((0, 0, 0, 1))) % 11 == 0:
        return shade("water", 7)
    return shade("water", 4 + rng.choice((0, 0, 1)))


def water_side(base):
    def f(x, y, rng):
        return shade("water", base + rng.choice((-1, 0, 0)))
    return f


def dirt_all(base):
    def f(x, y, rng):
        return shade("dirt", base + rng.choice((-1, 0, 0, 1)))
    return f


def planks_top(x, y, rng):
    if (x + 2 * y) % 8 in (0, 1):
        return shade("wood", 2)
    return shade("wood", 5 + rng.choice((-1, 0, 0)))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "demo/assets"
    cubes = {
        "grass": make_cube(grass_top, dirt_side(4), dirt_side(2)),
        "stone": make_cube(stone_top, stone_side(5, True), stone_side(3, False)),
        "crate": make_cube(wood_top, wood_side(5, True), wood_side(3, False)),
        "dirt": make_cube(dirt_all(5), dirt_all(4), dirt_all(2)),
        "water": make_cube(water_top, water_side(3), water_side(2)),
        "planks": make_cube(planks_top, wood_side(4, True), wood_side(2, False)),
    }
    for name, img in cubes.items():
        img.save("%s/%s.bmp" % (out, name))
        print("%s/%s.bmp" % (out, name))


if __name__ == "__main__":
    main()
