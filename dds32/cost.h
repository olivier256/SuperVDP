#ifndef COST_H
#define COST_H

#include <stdint.h>

/* =========================================================================
   Modèle de coûts : cycles SH2 estimés.

   Le simulateur exécute le moteur en code natif : il ne mesure pas le
   temps SH2. Le moteur compte donc ce qu'il fait (lignes, colonnes de
   triangles, mots copiés...), et multiplie par un coût unitaire en
   cycles. Le résultat est déclaré au simulateur (vdp_count_cpu_cycles),
   qui ajoute lui-même le coût des écritures dans le frame buffer (3
   cycles, 5 si le FIFO est plein) et l'affiche avec VDP_STATS=1.

   Sur SH2, les compteurs disparaissent (sauf compilation avec DDS32_COST) :
   le moteur n'est pas ralenti par sa propre mesure.
   ========================================================================= */

#if !defined(__sh__) || defined(DDS32_COST)
#define DDS32_COST_ENABLED 1
#define COST_ADD(cost, field, n) ((cost)->field += (uint32_t)(n))
#else
#define DDS32_COST_ENABLED 0
#define COST_ADD(cost, field, n) ((void)0)
#endif

typedef struct {
    /* Décor (render.c) */
    uint32_t lines;    /* lignes d'écran */
    uint32_t columns;  /* colonnes entières (boucle rapide : 16 pixels) */
    uint32_t partials; /* colonnes des bords ou hors de la zone */
    uint32_t copies;   /* mots copiés hors de la boucle rapide (bords,
                          décor devant l'objet) */
    /* Objet mobile : recouvrements (render.c) */
    uint32_t fronts;   /* triangles examinés sur les lignes de l'objet */
    uint32_t segments; /* triangles redessinés devant l'objet */
    /* Objet mobile : tranches de sprite (sprite.c) */
    uint32_t rows;  /* tranches dessinées */
    uint32_t words; /* mots examinés */
    uint32_t edges; /* pixels isolés au bord */
} DrawCost;

/* Coûts unitaires, en cycles SH2, hors écritures dans le frame buffer.
   ÉTALONNÉS par le banc tools/sh2cycles (bench_render.c) : le vrai code
   de render.c et sprite.c, compilé pour SH2 (gcc 13.2 -m2 -O2), exécuté
   sous qemu avec un modèle de temps du SH2 (cache à 4 voies, SDRAM), sur
   240 images (caméra qui suit la caisse, et positions tirées au hasard).
   Ajustement par moindres carrés, à coûts positifs ou nuls : le modèle
   reproduit les temps du banc à -5 % / +6 % près. Les refaire après
   toute modification de render.c ou sprite.c : tools/sh2cycles/run.sh
   imprime les nouvelles valeurs.

   Les coûts nuls ne sont pas gratuits : l'ajustement les a comptés dans
   un autre (les mots d'une tranche de sprite dans CYCLES_ROW, par
   exemple), faute d'images où ils varient seuls. */
#define CYCLES_LINE 14 /* début de ligne : rangées, bornes */
#define CYCLES_COLUMN                                                          \
    80                     /* colonne entière : 2 lectures de la mosaïque,   \
                              8 lectures 32 bits, 4 OU, 8 écritures, et       \
                              les défauts de cache */
#define CYCLES_PARTIAL 217 /* colonne du bord, mots compris */
#define CYCLES_COPY 0      /* compté dans CYCLES_PARTIAL et CYCLES_SEGMENT */
#define CYCLES_FRONT 136   /* profondeur lue et comparée */
#define CYCLES_SEGMENT 89  /* triangle redessiné devant l'objet */
#define CYCLES_ROW 318     /* tranche de sprite, mots compris */
#define CYCLES_WORD 0
#define CYCLES_EDGE 0

static inline uint32_t cost_cycles(const DrawCost *c) {
    return c->lines * CYCLES_LINE + c->columns * CYCLES_COLUMN +
           c->partials * CYCLES_PARTIAL + c->copies * CYCLES_COPY +
           c->fronts * CYCLES_FRONT + c->segments * CYCLES_SEGMENT +
           c->rows * CYCLES_ROW + c->words * CYCLES_WORD +
           c->edges * CYCLES_EDGE;
}

#endif
