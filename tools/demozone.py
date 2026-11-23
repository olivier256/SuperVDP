#!/usr/bin/env python3
"""
Écrit la zone de démo de DDS 32 au format MagicaVoxel (.vox), pour qu'on
puisse ensuite l'ouvrir et la modifier dans MagicaVoxel.

    python3 tools/demozone.py demo/zone/demo.vox

Zone de 24 x 20 cases, 8 niveaux. Le numéro de couleur de chaque voxel est
le type de cube (voir demo/zone/cubes.txt) :
  1 herbe, 2 terre, 3 pierre, 4 eau, 5 planches, 6 caisse.
- le sol : terre au niveau 0, herbe au niveau 1 ;
- une rivière (j = 11 à 14) : de l'eau au niveau 0, un niveau sous le sol ;
- un pont de planches (i = 5 et 6) au niveau 2, une marche au-dessus du
  sol : entre l'eau et les planches, un niveau de vide, qu'on voit par le
  côté ;
- une arche de pierre (i = 12) : deux piliers et un linteau au niveau 5 ;
  on passe dessous ;
- un plateau de deux niveaux de plus (i = 15 à 22, j = 2 à 9), avec une
  marche devant ;
- deux murs de pierre au fond, et deux caisses.

Format .vox (MagicaVoxel, version 150) : en-tête « VOX », version, puis
le bloc MAIN qui contient SIZE (dimensions), XYZI (voxels : x, y, z,
couleur) et RGBA (palette ; la couleur n est la n-ième entrée, à partir
de 1). x, y, z de MagicaVoxel = i, j, k ; z vers le haut.
"""

import struct
import sys

W, D, H = 24, 20, 8
GRASS, DIRT, STONE, WATER, PLANKS, CRATE = 1, 2, 3, 4, 5, 6
COLORS = {GRASS: (98, 160, 60), DIRT: (120, 80, 48), STONE: (140, 140, 152),
          WATER: (40, 92, 176), PLANKS: (180, 128, 66), CRATE: (204, 156, 90)}


def build():
    v = {}

    def put(i, j, k, t):
        if 0 <= i < W and 0 <= j < D and 0 <= k < H:
            v[(i, j, k)] = t

    river = (11, 12, 13, 14)
    for i in range(W):
        for j in range(D):
            if j in river:
                put(i, j, 0, WATER)  # un niveau sous le sol
            else:
                put(i, j, 0, DIRT)
                put(i, j, 1, GRASS)
    # Murs du fond, deux niveaux
    for i in range(W):
        for k in (2, 3):
            put(i, 0, k, STONE)
    for j in range(D):
        if j in river:
            continue
        for k in (2, 3):
            put(0, j, k, STONE)
    # Pont : une marche au-dessus du sol, de j = 10 à 15 ; au-dessus de la
    # rivière, un niveau de vide entre l'eau et les planches
    for i in (5, 6):
        for j in range(10, 16):
            put(i, j, 2, PLANKS)
    # Arche : piliers en j = 5 et 8, linteau de j = 5 à 8
    for j in (5, 8):
        for k in (2, 3, 4):
            put(12, j, k, STONE)
    for j in range(5, 9):
        put(12, j, 5, STONE)
    # Plateau : deux niveaux de plus
    for i in range(15, 23):
        for j in range(2, 10):
            put(i, j, 2, DIRT)
            put(i, j, 3, GRASS)
    # Escalier, devant le plateau (côté visible) : une marche
    for i in (18, 19):
        put(i, 10, 2, STONE)
    # Caisses
    put(8, 4, 2, CRATE)
    put(9, 4, 2, CRATE)
    put(9, 4, 3, CRATE)
    return v


def chunk(name, content, children=b""):
    return name + struct.pack("<ii", len(content), len(children)) + content + children


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "demo/zone/demo.vox"
    voxels = build()
    size = chunk(b"SIZE", struct.pack("<iii", W, D, H))
    xyzi = struct.pack("<i", len(voxels)) + b"".join(
        struct.pack("<BBBB", i, j, k, t) for (i, j, k), t in sorted(voxels.items()))
    palette = b""
    for n in range(1, 257):
        r, g, b = COLORS.get(n, (64, 64, 64))
        palette += struct.pack("<BBBB", r, g, b, 255)
    children = size + chunk(b"XYZI", xyzi) + chunk(b"RGBA", palette)
    data = b"VOX " + struct.pack("<i", 150) + chunk(b"MAIN", b"", children)
    open(out, "wb").write(data)
    print("%s : %d x %d x %d, %d voxels" % (out, W, D, H, len(voxels)))


if __name__ == "__main__":
    main()
