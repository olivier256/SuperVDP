#!/usr/bin/env python3
"""
Banc sh2cycles (DDS 32) : relie le temps mesuré sur le code SH2 de
dds32/render.c et dds32/sprite.c aux compteurs du modèle (dds32/cost.h),
et en déduit les coûts unitaires par moindres carrés (coûts positifs ou
nuls).

    python3 fit_render.py DOSSIER

DOSSIER contient, pour CPU = master et slave :
    render_costs_CPU.csv   compteurs du modèle (programme hôte)
    render_cycles_CPU.txt  temps mesurés (greffon : « sh2cycles;... »)
    render_prints_CPU.txt  empreintes du frame buffer (code SH2)
"""

import sys

import numpy as np

FIELDS = ["lines", "columns", "partials", "copies", "fronts", "segments",
          "rows", "words", "edges"]
CONSTS = ["CYCLES_LINE", "CYCLES_COLUMN", "CYCLES_PARTIAL", "CYCLES_COPY",
          "CYCLES_FRONT", "CYCLES_SEGMENT", "CYCLES_ROW", "CYCLES_WORD",
          "CYCLES_EDGE"]
N = len(FIELDS)


def load(folder, cpu):
    costs = []
    for line in open("%s/render_costs_%s.csv" % (folder, cpu)):
        p = line.strip().split(";")
        row = dict(zip(FIELDS, map(int, p[1:1 + N])))
        row["model"], row["print"], row["writes"] = (int(p[1 + N]), p[2 + N],
                                                     int(p[3 + N]))
        row["image"] = int(p[0])
        costs.append(row)
    measured = []
    for line in open("%s/render_cycles_%s.txt" % (folder, cpu)):
        if line.startswith("sh2cycles;"):
            p = line.strip().split(";")
            measured.append({"cycles": int(p[2]), "insns": int(p[3]),
                             "misses": int(p[4]), "fb": int(p[5])})
    prints = [l.strip() for l in open("%s/render_prints_%s.txt" % (folder, cpu))]
    if not (len(costs) == len(measured) == len(prints)):
        sys.exit("%s : nombres d'images differents (%d, %d, %d)"
                 % (cpu, len(costs), len(measured), len(prints)))
    bad = [i for i in range(len(costs)) if costs[i]["print"] != prints[i]]
    if bad:
        sys.exit("%s : le code SH2 ne dessine pas comme l'hote (images %s)"
                 % (cpu, bad[:10]))
    bad = [i for i in range(len(costs)) if costs[i]["writes"] != measured[i]["fb"]]
    if bad:
        sys.exit("%s : nombres d'ecritures differents (images %s)" % (cpu, bad[:10]))
    return costs, measured


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    rows, ys = [], []
    for cpu in ("master", "slave"):
        costs, measured = load(folder, cpu)
        print("%s : %d images, empreintes et ecritures identiques a l'hote"
              % (cpu, len(costs)))
        rows += costs
        ys += measured

    cycles = np.array([m["cycles"] for m in ys], dtype=float)
    misses = np.array([m["misses"] for m in ys], dtype=float)
    writes = np.array([m["fb"] for m in ys], dtype=float)
    old = np.array([r["model"] for r in rows], dtype=float)
    print("Mesure SH2 par CPU et par image (ecran entier), hors ecritures FB :")
    print("  cycles : min %d, moyenne %d, max %d ; ecritures FB : %d a %d"
          % (cycles.min(), cycles.mean(), cycles.max(), writes.min(),
             writes.max()))
    print("  defauts de cache : moyenne %d" % misses.mean())
    w = 384000.0
    walk = np.array([r["image"] % 3 != 2 for r in rows])
    for name, sel in (("camera qui suit la caisse (le jeu)", walk),
                      ("caisse et camera au hasard", ~walk)):
        c, f = cycles[sel], writes[sel]
        print("  %s : %d a %d cycles ; avec les ecritures, pire image %.1f "
              "a %.1f %% du budget" % (name, c.min(), c.max(),
                                       100 * (c + 3 * f).max() / w,
                                       100 * (c + 5 * f).max() / w))
    err = (old - cycles) / cycles
    print("Modele actuel (cost.h) : ecart moyen %+.1f %%, de %+.1f %% a "
          "%+.1f %%" % (100 * err.mean(), 100 * err.min(), 100 * err.max()))

    A = np.array([[r[f] for f in FIELDS] for r in rows], dtype=float)
    active = list(range(N))
    while True:
        sub, *_ = np.linalg.lstsq(A[:, active], cycles, rcond=None)
        if (sub >= 0).all():
            break
        active = [j for j, k in zip(active, sub) if k >= 0]
    coef = np.zeros(N)
    coef[active] = sub
    err = (A @ coef - cycles) / cycles
    print("Nouveau modele : ecart moyen %+.2f %%, de %+.2f %% a %+.2f %%"
          % (100 * err.mean(), 100 * err.min(), 100 * err.max()))
    print("Couts unitaires ajustes (cycles) :")
    for const, k in zip(CONSTS, coef):
        print("  #define %-15s %d   /* ajuste : %.2f */" % (const, round(k), k))
    err = (A @ np.round(coef) - cycles) / cycles
    print("Avec les valeurs arrondies : ecart de %+.2f %% a %+.2f %%"
          % (100 * err.min(), 100 * err.max()))


if __name__ == "__main__":
    main()
