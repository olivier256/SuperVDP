#include "compose.h"
#include "framebuffer.h"
#include "vdp.h"

/* =========================================================================
   Composition d'une ligne par segments.

   Principe
   --------
   On parcourt la ligne du plan A tile par tile. bmp2c a donné la classe
   de chaque ligne de tile, et pour chaque ligne de pixels des plans les
   colonnes où cette classe change (assets_types.h) :
   - transparente : l'écran montre le plan B, qu'on copie ;
   - opaque       : l'écran montre le plan A, qu'on copie ; B n'est pas lu ;
   - mixte        : on fusionne pixel par pixel (A si non nul, sinon B).
   Les tiles voisines de même classe forment un segment. Sur l'image de la
   démo, 95 % des lignes de tile de A sont entièrement transparentes ou
   entièrement opaques : la fusion, plus chère, reste rare.

   Règles d'écriture (comme dans AMB96)
   ------------------------------------
   Dans le frame buffer, uniquement des écritures 16 bits à des adresses
   paires : on écrit des mots d'écran (pixels 2k et 2k + 1). La ligne
   commence à une adresse paire et l'écran est écrit à partir de x = 0 :
   la destination est toujours paire. C'est la source qui hérite de la
   parité du défilement :
   - source paire : chaque mot est lu d'un coup (MOV.W) dans la tile ;
   - source impaire : on lit des mots alignés, et chaque mot écrit réunit
     l'octet de poids faible d'un mot lu et l'octet de poids fort du
     suivant (quatre MOV.L pour une tile entière).
   Tiles et plans ont une largeur paire : la parité de la source ne change
   pas le long de la ligne. La décision est prise une fois par ligne et par
   plan (copyA, copyB), pas pour chaque segment.

   Frontières de segment
   ---------------------
   Si le défilement de A est impair, les frontières de tile de A tombent à
   des colonnes d'écran impaires : le mot qui chevauche deux segments passe
   par la fusion (un mot).

   Écrit pour le SH2 (mesuré avec tools/sh2cycles)
   -----------------------------------------------
   Le SH2 n'a ni division, ni décalage à distance variable (seulement 1,
   2, 8 ou 16 bits) : un « % » ou un « >> n » devient un appel à libgcc.
   Le code les évite :
   - positions ramenées dans le plan par soustraction (wrapIn) ;
   - classe d'une ligne de tile lue avec des masques préparés une fois par
     ligne (classMasks), sans décalage ;
   - positions en non signé : x >> 4 devient deux SHLR2 ;
   - segments formés depuis les changements de classe (un bit par colonne
     de tile) : 32 colonnes lues à la fois, la boucle ne s'arrête que sur
     les changements et finit après le dernier ;
   - tiles parcourues par un pointeur sur leur numéro dans le plan, qui
     revient au début de la rangée au bout du plan ;
   - tiles entières copiées par une suite de 8 mots, sans boucle ;
   - option SUPERVDP_ASM=1 (SH2) : fusion en assembleur (merge_sh2.S).
   ========================================================================= */

/* Fonctions appelées quelques fois par ligne : gardées en un seul
   exemplaire (pas de recopie dans compose_line), pour que le code de
   rendu tienne dans les 2 Ko de RAM interne du SH2. */
#define ONCE_PER_LINE __attribute__((noinline))

/* Code froid : la copie depuis une source impaire. Avec le décalage
   d'écran choisi ligne par ligne, elle ne sert plus qu'au plan minoritaire
   d'une ligne, quelques pour cent des mots.
   Sur SH2, il est rangé à part (section .text.sdram) : l'édition de liens
   le laisse en SDRAM, derrière le cache, et réserve la RAM interne au
   code chaud (voir tools/sh2cycles/bench_ram.ld). */
#if defined(__sh__)
#define COLD_CODE __attribute__((noinline, section(".text.sdram")))
#else
#define COLD_CODE __attribute__((noinline))
#endif

/* Ramène v dans [0, n), par additions ou soustractions : les valeurs
   courantes (un défilement, une ligne) sont à moins d'un tour du plan. */
static unsigned wrapIn(int v, unsigned n) {
    while (v < 0)
        v += (int)n;
    while ((unsigned)v >= n)
        v -= (int)n;
    return (unsigned)v;
}

/* x + d, ramené dans [0, n), pour x < n */
static unsigned advance(unsigned x, unsigned d, unsigned n) {
    x += d;
    while (x >= n)
        x -= n;
    return x;
}

/* Deux pixels, l'un derrière l'autre, lus à une adresse paire : un MOV.W
   sur SH2. Sur l'hôte (petit-boutiste), on rétablit l'ordre du SH2 :
   pixel de gauche dans l'octet de poids fort. */
static inline uint16_t readPair(const uint8_t *p) {
#if defined(__sh__)
    return *(const uint16_t *)p;
#else
    return (uint16_t)((p[0] << 8) | p[1]);
#endif
}

/* ---- Destination dans le frame buffer --------------------------------------
   Sur SH2 : un pointeur. Les mots d'une tile s'écrivent alors en
   MOV.W R0,@(disp,Rn), sans recalculer l'adresse à chaque mot.
   Sur l'hôte : un offset, et chaque écriture passe par vdp_dram_write16(),
   qui la compte. */
#if HYPO_BUS32
/* Hypothèse bus32 : on écrit des mots de 32 bits (4 pixels), à des
   adresses multiples de 4. Le moteur produit toujours ses pixels deux par
   deux, mais il écrit chaque ligne de gauche à droite, chaque mot une
   seule fois : un « LineOut » réunit les mots par paires. Un mot de
   gauche (pixels 4k, 4k + 1) attend dans 'hi' ; le mot de droite le
   complète et part avec lui en une seule écriture. PUT ignore donc k : les
   appels se font dans l'ordre de la ligne, et SKIP n'a rien à faire. */
typedef struct {
#if defined(__sh__)
    volatile uint32_t *p; /* prochain mot de 32 bits de la ligne */
#else
    uint32_t off; /* son offset dans le frame buffer de dessin */
#endif
    uint32_t hi;   /* mot de gauche en attente */
    unsigned half; /* 1 : 'hi' attend son voisin de droite */
} LineOut;
typedef LineOut *Dest;

static inline void outWrite32(LineOut *o, uint32_t v) {
#if defined(__sh__)
    *o->p++ = v;
#else
    vdp_dram_write32(o->off, v);
    o->off += 4;
#endif
}

/* Deux pixels (un mot de 16 bits) */
static inline void out16(LineOut *o, unsigned v) {
    if (o->half) {
        outWrite32(o, (o->hi << 16) | (v & 0xFFFFu));
        o->half = 0;
    } else {
        o->hi = v & 0xFFFFu;
        o->half = 1;
    }
}

/* Quatre pixels d'un coup : une écriture si la ligne est alignée, sinon
   le mot de gauche de v complète celui qui attend, et son mot de droite
   attend à son tour. */
static inline void out32(LineOut *o, uint32_t v) {
    if (o->half) {
        outWrite32(o, (o->hi << 16) | (v >> 16));
        o->hi = v & 0xFFFFu;
    } else {
        outWrite32(o, v);
    }
}

/* La destination d'une copie : toujours la suite de la ligne. Sur l'hôte,
   on vérifie que le moteur écrit bien dans l'ordre (sinon, abandon). */
#if defined(__sh__)
#define lineDest(o, offset) (o)
#define lineEnd(o, offset) ((void)0)
#else
#include <stdio.h>
#include <stdlib.h>
static inline LineOut *lineDest(LineOut *o, uint32_t offset) {
    if (o->off + 2 * o->half != offset) {
        fprintf(stderr, "compose_line : ecriture hors ordre (%u au lieu de %u)\n",
                (unsigned)offset, (unsigned)(o->off + 2 * o->half));
        abort();
    }
    return o;
}
static inline void lineEnd(LineOut *o, uint32_t offset) {
    lineDest(o, offset);
    if (o->half) {
        fprintf(stderr, "compose_line : mot de 16 bits en attente en fin de ligne\n");
        abort();
    }
}
#endif

#define PUT(d, k, v) out16((d), (v)) /* dans l'ordre de la ligne */
#define SKIP(d, words) ((void)0)
#elif defined(__sh__)
typedef volatile uint16_t *Dest;
#define DEST(offset) ((Dest)(VDP_DRAM_ADDRESS + (offset)))
#define PUT(d, k, v) ((d)[k] = (v)) /* mot k après d */
#define SKIP(d, words) ((d) += (words))
#else
typedef uint32_t Dest;
#define DEST(offset) (offset)
#define PUT(d, k, v) vdp_dram_write16((d) + 2 * (k), (v))
#define SKIP(d, words) ((d) += 2 * (words))
#endif

/* Quatre pixels lus à une adresse multiple de 4 : un MOV.L sur SH2 */
static inline uint32_t readQuad(const uint8_t *p) {
#if defined(__sh__)
    return *(const uint32_t *)p;
#else
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
#endif
}

/* ---- Une ligne d'un plan --------------------------------------------------
 */

/* Ce que les fonctions de copie et de fusion lisent d'un plan. Rangé
   dans la pile (passé par adresse) : le moins de champs possible, chaque
   écriture en SDRAM coûtant un cycle de plus. */
typedef struct {
    const uint16_t *row;     /* numéros de tile de la rangée traversée */
    const uint16_t *rowEnd;  /* juste après le dernier */
    const uint8_t *tileLine; /* ligne voulue dans la tile 0 */
    unsigned width;          /* largeur du plan en pixels */
} PlaneRow;

/* Remplit 'pr' pour la ligne worldY du plan ; renvoie cette ligne ramenée
   dans le plan (0 à hauteur - 1). Recopiée dans compose_line avec la
   fusion en assembleur (SUPERVDP_ASM) ; appelée sinon : tout en C, le code
   chaud ne tiendrait plus dans les 2 Ko de RAM interne. */
#if defined(__sh__) && SUPERVDP_ASM
#define PLANE_ROW_INIT static inline
#else
#define PLANE_ROW_INIT static
#endif
PLANE_ROW_INIT unsigned planeRowInit(PlaneRow *pr, const Tilemap *map,
                                     const Tileset *tiles, int worldY) {
    const unsigned y = wrapIn(worldY, (unsigned)map->heightTiles << TILE_SHIFT);
    pr->row = map->cells + (y >> TILE_SHIFT) * map->widthTiles;
    pr->rowEnd = pr->row + map->widthTiles;
    pr->tileLine = tile_row(tiles, 0, y & (TILE_SIZE - 1));
    pr->width = (unsigned)map->widthTiles << TILE_SHIFT;
    return y;
}

/* Premier pixel de la tile dont 'cell' donne le numéro, sur la ligne */
static inline const uint8_t *tileStart(const PlaneRow *pr,
                                       const uint16_t *cell) {
    return pr->tileLine + (unsigned)*cell * TILE_PIXELS;
}

/* Tile suivante dans la rangée (le plan boucle) */
static inline const uint16_t *nextCell(const PlaneRow *pr,
                                       const uint16_t *cell) {
    return ++cell == pr->rowEnd ? pr->row : cell;
}

/* ---- Copie d'un segment depuis un plan ------------------------------------
   n pixels (pair) du plan, à partir de sa colonne x, vers l'offset 'offset'
   (pair) du frame buffer. */

typedef void (*CopySpan)(Dest d, const PlaneRow *pr, unsigned x, unsigned n,
                         ComposeCost *cost);

/* Une ligne de tile entière, source paire : 8 mots, sans boucle */
#if HYPO_BUS32
/* Hypothèse bus32 : la source commence au début d'une tile, donc à une
   adresse multiple de 4 : quatre MOV.L lus, quatre mots de 32 bits
   écrits (un de plus en attente si la ligne n'est pas alignée ici). */
static inline void copyTileEven(Dest d, const uint8_t *src) {
    out32(d, readQuad(src));
    out32(d, readQuad(src + 4));
    out32(d, readQuad(src + 8));
    out32(d, readQuad(src + 12));
}
#else
static inline void copyTileEven(Dest d, const uint8_t *src) {
    PUT(d, 0, readPair(src));
    PUT(d, 1, readPair(src + 2));
    PUT(d, 2, readPair(src + 4));
    PUT(d, 3, readPair(src + 6));
    PUT(d, 4, readPair(src + 8));
    PUT(d, 5, readPair(src + 10));
    PUT(d, 6, readPair(src + 12));
    PUT(d, 7, readPair(src + 14));
}
#endif

/* Source paire : chaque morceau commence à une adresse paire et a une
   longueur paire, dans la tile comme dans le frame buffer. */
static void copySpanEven(Dest d, const PlaneRow *pr, unsigned x, unsigned n,
                         ComposeCost *cost) {
    const uint16_t *cell = pr->row + (x >> TILE_SHIFT);
    unsigned xInTile = x & (TILE_SIZE - 1);

    COST_ADD(cost, wordsEven, n / 2);
    while (n > 0) {
        const uint8_t *src = tileStart(pr, cell) + xInTile;
        cell = nextCell(pr, cell);
        COST_ADD(cost, tileChunks, 1);

        if (xInTile == 0 && n >= TILE_SIZE) {
            copyTileEven(d, src);
            SKIP(d, TILE_SIZE / 2);
            n -= TILE_SIZE;
            continue;
        }

        unsigned count = TILE_SIZE - xInTile;
        if (count > n)
            count = n;
        n -= count;
        xInTile = 0;
        for (; count > 0; count -= 2) {
            PUT(d, 0, readPair(src));
            SKIP(d, 1);
            src += 2;
        }
    }
}

/* Une ligne de tile entière, source impaire : le pixel gardé de la tile
   précédente avec le pixel 0, puis (1, 2) ... (13, 14). Renvoie le
   pixel 15, qui ira avec le premier de la tile suivante. */
static inline uint8_t copyTileOdd(Dest d, const uint8_t *src, uint8_t carry) {
    /* Les 16 pixels en quatre mots longs (MOV.L, gros-boutiste) : le mot
       écrit garde les 16 bits de poids faible, sans masque. */
    const uint32_t l0 = readQuad(src), l1 = readQuad(src + 4),
                   l2 = readQuad(src + 8), l3 = readQuad(src + 12);
#if HYPO_BUS32
    /* Quatre pixels écrits par mot : le dernier pixel du mot long
       précédent (ou la retenue), puis les trois premiers du suivant. */
    out32(d, ((uint32_t)carry << 24) | (l0 >> 8));
    out32(d, (l0 << 24) | (l1 >> 8));
    out32(d, (l1 << 24) | (l2 >> 8));
    out32(d, (l2 << 24) | (l3 >> 8));
#else
    PUT(d, 0, (uint16_t)((carry << 8) | (l0 >> 24)));
    PUT(d, 1, (uint16_t)(l0 >> 8));
    PUT(d, 2, (uint16_t)((l0 << 8) | (l1 >> 24)));
    PUT(d, 3, (uint16_t)(l1 >> 8));
    PUT(d, 4, (uint16_t)((l1 << 8) | (l2 >> 24)));
    PUT(d, 5, (uint16_t)(l2 >> 8));
    PUT(d, 6, (uint16_t)((l2 << 8) | (l3 >> 24)));
    PUT(d, 7, (uint16_t)(l3 >> 8));
#endif
    return (uint8_t)l3;
}

/* Source impaire : chaque mot écrit réunit un pixel de colonne impaire et
   le suivant. On lit le plan par mots alignés (MOV.W à une adresse
   paire) : (x - 1, x), (x + 1, x + 2)... Le mot écrit k réunit l'octet
   de poids faible du mot lu k et l'octet de poids fort du mot lu k + 1 :
   (prev << 8) | (next >> 8), le mot écrit ne gardant que 16 bits. Le
   passage d'une tile à la suivante n'est qu'un changement de pointeur :
   le mot lu suivant est le premier de la tile suivante. Les tiles
   entières passent par copyTileOdd (quatre MOV.L). */
static void copySpanOdd(Dest d, const PlaneRow *pr, unsigned x, unsigned n,
                        ComposeCost *cost) {
    const uint16_t *cell = pr->row + (x >> TILE_SHIFT);
    const unsigned xInTile = x & (TILE_SIZE - 1); /* impair */
    const uint8_t *src = tileStart(pr, cell) + xInTile - 1;
    unsigned prev = readPair(src);                 /* pixels x - 1 et x */
    unsigned left = (TILE_SIZE - 1 - xInTile) / 2; /* mots lus restants */
    unsigned words = n / 2;
    src += 2;

    COST_ADD(cost, wordsOdd, words);
    COST_ADD(cost, tileChunks, 1);
    for (;;) {
        /* Mots dont la source reste dans la tile en cours */
        unsigned count = left < words ? left : words;
        words -= count;
        for (; count > 0; count--) {
            const unsigned next = readPair(src);
            src += 2;
            PUT(d, 0, (uint16_t)((prev << 8) | (next >> 8)));
            SKIP(d, 1);
            prev = next;
        }
        if (words == 0)
            return;

        /* Tile suivante ; entières tant qu'il reste 8 mots ou plus */
        cell = nextCell(pr, cell);
        src = tileStart(pr, cell);
        COST_ADD(cost, tileChunks, 1);
        while (words >= TILE_SIZE / 2) {
            prev = copyTileOdd(d, src, (uint8_t)prev);
            SKIP(d, TILE_SIZE / 2);
            words -= TILE_SIZE / 2;
            if (words == 0)
                return;
            cell = nextCell(pr, cell);
            src = tileStart(pr, cell);
            COST_ADD(cost, tileChunks, 1);
        }
        left = TILE_SIZE / 2;
    }
}

/* ---- Fusion pixel par pixel ------------------------------------------------
   n pixels (pair) : A s'il est non nul, sinon B. Ce chemin ne sert
   qu'aux lignes de tile mixtes et aux frontières de segment, rares.
   On avance par morceaux où ni A ni B ne change de tile : à l'intérieur,
   deux pointeurs, sans test de fin de tile à chaque pixel. Les pixels
   sont réunis deux par deux en mots ; un morceau peut finir sur un pixel
   de gauche, qui attend alors le premier pixel du morceau suivant
   ('pending') ; le reste de ce morceau repart sur des mots entiers. */

typedef struct {
    const uint16_t *cell; /* tile suivante */
    const uint8_t *src;   /* prochain pixel */
    unsigned left;        /* pixels restants dans la tile en cours */
} Cursor;

static inline void cursorAt(Cursor *c, const PlaneRow *pr, unsigned x) {
    const uint16_t *cell = pr->row + (x >> TILE_SHIFT);
    const unsigned xInTile = x & (TILE_SIZE - 1);
    c->src = tileStart(pr, cell) + xInTile;
    c->left = TILE_SIZE - xInTile;
    c->cell = nextCell(pr, cell);
}

static inline void cursorNextTile(Cursor *c, const PlaneRow *pr) {
    c->src = tileStart(pr, c->cell);
    c->cell = nextCell(pr, c->cell);
    c->left = TILE_SIZE;
}

ONCE_PER_LINE __attribute__((unused)) static void
mergeSpan(Dest d, const PlaneRow *pa, unsigned xa, const PlaneRow *pb,
          unsigned xb, unsigned n, ComposeCost *cost) {
    Cursor a, b;
    cursorAt(&a, pa, xa);
    cursorAt(&b, pb, xb);
    COST_ADD(cost, tileChunks, 2);
    COST_ADD(cost, wordsMerged, n / 2);

    unsigned pending = 0, hasPending = 0;
    for (;;) {
        unsigned piece = a.left < b.left ? a.left : b.left;
        if (piece > n)
            piece = n;
        n -= piece;
        a.left -= piece;
        b.left -= piece;

        const uint8_t *srcA = a.src, *srcB = b.src;
        a.src += piece;
        b.src += piece;

        /* Un pixel en attente : le premier pixel du morceau complète son
           mot. Le reste du morceau repart sur des mots entiers. */
        if (hasPending && piece > 0) {
            unsigned pixel = *srcA++;
            const unsigned back = *srcB++;
            if (pixel == 0)
                pixel = back;
            PUT(d, 0, (uint16_t)((pending << 8) | pixel));
            SKIP(d, 1);
            hasPending = 0;
            piece--;
        }

        /* Deux pixels à la fois, un mot écrit */
        for (; piece >= 2; piece -= 2) {
            unsigned left = srcA[0], right = srcA[1];
            if (left == 0)
                left = srcB[0];
            if (right == 0)
                right = srcB[1];
            PUT(d, 0, (uint16_t)((left << 8) | right));
            SKIP(d, 1);
            srcA += 2;
            srcB += 2;
        }

        /* Un pixel de gauche reste : il attend le morceau suivant */
        if (piece > 0) {
            pending = *srcA != 0 ? *srcA : *srcB;
            hasPending = 1;
        }

        if (n == 0)
            break;
        if (a.left == 0) {
            cursorNextTile(&a, pa);
            COST_ADD(cost, tileChunks, 1);
        }
        if (b.left == 0) {
            cursorNextTile(&b, pb);
            COST_ADD(cost, tileChunks, 1);
        }
    }
    /* n étant pair, il ne reste jamais de pixel en attente ici */
}

/* Option de compilation SUPERVDP_ASM=1 (SH2 seulement) : la fusion passe
   par merge_sh2.S, même interface et même résultat que mergeSpan, qui
   reste la référence (et la version de l'hôte). merge_sh2.S lit row,
   rowEnd et tileLine dans PlaneRow : leurs positions sont vérifiées
   ici. Hypothèse bus32 : toujours la fusion en C (merge_sh2.S écrit des
   mots de 16 bits). */
#if defined(__sh__) && SUPERVDP_ASM && !HYPO_BUS32
#include <stddef.h>
void merge_span_sh2(Dest d, const PlaneRow *pa, unsigned xa, const PlaneRow *pb,
                    unsigned xb, unsigned n);
typedef char planeRowLayoutForAsm[offsetof(PlaneRow, row) == 0 &&
                                          offsetof(PlaneRow, rowEnd) == 4 &&
                                          offsetof(PlaneRow, tileLine) == 8
                                      ? 1
                                      : -1];
#define MERGE_SPAN(d, pa, xa, pb, xb, n, cost)                                 \
    merge_span_sh2(d, pa, xa, pb, xb, n)
#else
#define MERGE_SPAN mergeSpan
#endif

/* Un pixel d'un plan, à sa colonne x (x < largeur) */
static inline unsigned pixelAt(const PlaneRow *pr, unsigned x) {
    return tileStart(pr, pr->row + (x >> TILE_SHIFT))[x & (TILE_SIZE - 1)];
}

/* Le mot de frontière entre deux segments, à la colonne p de la ligne
   (colonnes xa0 + p et suivante de A, xb0 + p et suivante de B) : quatre
   pixels lus directement, sans curseurs. Les colonnes sont calculées ici,
   dans le code froid, plutôt que dans compose_line. */
COLD_CODE static void mergeWord(Dest d, const PlaneRow *pa, unsigned xa0,
                                const PlaneRow *pb, unsigned xb0, unsigned p,
                                ComposeCost *cost) {
    const unsigned xa = advance(xa0, p, pa->width);
    const unsigned xb = advance(xb0, p, pb->width);
    const unsigned xa1 = xa + 1 == pa->width ? 0 : xa + 1;
    const unsigned xb1 = xb + 1 == pb->width ? 0 : xb + 1;
    unsigned left = pixelAt(pa, xa), right = pixelAt(pa, xa1);
    if (left == 0)
        left = pixelAt(pb, xb);
    if (right == 0)
        right = pixelAt(pb, xb1);
    PUT(d, 0, (uint16_t)((left << 8) | right));
    COST_ADD(cost, wordsMerged, 1);
}

/* ---- La ligne -------------------------------------------------------------
 */

/* Masque des 2 bits de classe de chaque ligne de tile (assets_types.h) :
   la classe se lit par un ET, sans décalage à distance variable. */
static const uint32_t classMasks[TILE_SIZE] = {
    0x00000003, 0x0000000C, 0x00000030, 0x000000C0, 0x00000300, 0x00000C00,
    0x00003000, 0x0000C000, 0x00030000, 0x000C0000, 0x00300000, 0x00C00000,
    0x03000000, 0x0C000000, 0x30000000, 0xC0000000,
};
/* Bits de la classe opaque (01) dans toutes les lignes */
#define ALL_ROWS_OPAQUE 0x55555555u

/* Segment de la ligne : il finit à la colonne 'end' (comptée depuis la
   colonne scrollAX - 1 du plan A) et commence où finit le précédent.
   'kind' : ce qu'on y dessine. */
enum { RUN_COPY_B, RUN_COPY_A, RUN_MERGE };
typedef struct {
    uint16_t end;
    uint16_t kind;
} Run;

/* Décalage d'écran le plus grand (valeur de SFT) : 1 sur la vraie 32X,
   3 avec l'hypothèse bus32 (4 pixels par mot). */
#if HYPO_BUS32
#define MAX_SHIFT 3
#else
#define MAX_SHIFT 1
#endif

/* Segments formés sur LINE_SPAN pixels, depuis la colonne
   scrollAX - MAX_SHIFT de A : de quoi dessiner la ligne avec n'importe
   quel décalage (voir compose_line). Vraie 32X : 322 pixels, de
   scrollAX - 1 à scrollAX + 320. Bus32 : 262 pixels, de scrollAX - 3 à
   scrollAX + 258 (une ligne décalée de 1 fait 260 pixels depuis
   scrollAX - 1). */
#define LINE_SPAN (FB_WIDTH + FB_LINE_EXTRA + MAX_SHIFT - 1)

/* Une tile de A par segment au pire, plus les deux tiles entamées */
#define MAX_RUNS (LINE_SPAN / TILE_SIZE + 2)

/* Choix du décalage : surcoûts, en cycles SH2, d'un mot copié depuis une
   source impaire et d'un mot de frontière (fusion), par rapport à un mot
   copié depuis une source paire. Ordres de grandeur mesurés avec
   tools/sh2cycles ; seule leur proportion compte. */
#define ODD_WORD_EXTRA 7
#define BOUNDARY_WORD_EXTRA 50
/* Hypothèse bus32 : surcoût d'un mot quand la ligne n'est pas alignée sur
   4 pixels dans le plan (mot de 32 bits recomposé par out32). Estimation,
   à mesurer avec tools/sh2cycles. */
#define HALF_WORD_EXTRA 1

int compose_line(uint32_t offset, const Tileset *tiles, const Tilemap *planeA,
                 int scrollAX, int worldAY, const Tilemap *planeB, int scrollBX,
                 int worldBY, ComposeCost *cost) {
    PlaneRow pa, pb;
    const unsigned yA = planeRowInit(&pa, planeA, tiles, worldAY);
    planeRowInit(&pb, planeB, tiles, worldBY);

    /* Classes de cette ligne de tile, masquées */
    const uint32_t classMask = classMasks[yA & (TILE_SIZE - 1)];
    const uint32_t opaque = classMask & ALL_ROWS_OPAQUE;
    const uint32_t *const rowClasses = tiles->rowClasses;

    COST_ADD(cost, lines, 1);

    /* 1. Segments : la ligne de A depuis la colonne scrollAX - 1, les
          tiles voisines de même classe regroupées. Les colonnes où la
          classe change sont lues dans classChanges (assets_types.h), 32 à la
          fois : on ne s'arrête que sur les changements, et la boucle finit
          après le dernier. La classe du segment en cours reste dans une
          variable locale (un registre) ; le segment n'est rangé qu'une fois
          terminé. On compte au passage les pixels copiés depuis A (opaques)
          et depuis B (transparents), pour le choix du décalage. */
    const unsigned xs = wrapIn(scrollAX - MAX_SHIFT, pa.width);
    const unsigned col0 = xs >> TILE_SHIFT; /* tile entamée */
    const unsigned firstSx = TILE_SIZE - (xs & (TILE_SIZE - 1));
/* Colonnes de tile qui suivent la tile entamée : SPAN_COLS + 1 si la
   ligne entame sa première tile par l'un de ses derniers pixels
   (firstSx < FIRST_SX_LIMIT), SPAN_COLS sinon. Vraie 32X (LINE_SPAN =
   322) : 21 si firstSx = 1, 20 sinon. Bus32 (262) : 17 si firstSx < 6,
   16 sinon. */
#define SPAN_COLS ((LINE_SPAN - 2) / TILE_SIZE)
#define FIRST_SX_LIMIT (LINE_SPAN - TILE_SIZE * SPAN_COLS)
#if TILE_SIZE != 16 || SPAN_COLS + 1 > 25 || FIRST_SX_LIMIT < 2 ||            \
    FIRST_SX_LIMIT > TILE_SIZE
#error "nombre de colonnes de la ligne (masque de 'bits') à revoir"
#endif
#define COLS_MASK(n) (0xFFFFFFFFu << (32 - (n)))
#if FIRST_SX_LIMIT == 2
#define MANY_COLS(firstSx) ((firstSx) == 1) /* même code qu'avant */
#else
#define MANY_COLS(firstSx) ((firstSx) < FIRST_SX_LIMIT)
#endif
    Run runs[MAX_RUNS];
    Run *run = runs;
    unsigned start = 0;
    unsigned fromA = 0, fromB = 0;
    COST_ADD(cost, aTiles,
             (LINE_SPAN - firstSx + 2 * TILE_SIZE - 1) >> TILE_SHIFT);

    /* Ce qu'on dessine dans le segment en cours, d'après la classe de sa
       ligne de tile */
#define KIND_OF(rowClass)                                                      \
    ((rowClass) == opaque ? RUN_COPY_A                                         \
     : (rowClass) == 0    ? RUN_COPY_B                                         \
                          : RUN_MERGE)
    unsigned kind = KIND_OF(rowClasses[pa.row[col0]] & classMask);

    /* Bits des colonnes suivantes, col0 + 1 et au-delà, en tête de
       'bits' : 32 bits lus depuis l'octet qui contient le premier, puis
       décalés de 0 à 7 bits ; il en reste au moins 25. On garde ceux des
       colonnes de la ligne : SPAN_COLS + 1 ou SPAN_COLS (voir
       MANY_COLS). */
    const unsigned b = col0 + 1;
    const uint8_t *const p8 =
        planeA->classChanges + yA * planeA->changeBytes + (b >> 3);
    uint32_t bits = (uint32_t)p8[0] << 24 | (uint32_t)p8[1] << 16 |
                    (uint32_t)p8[2] << 8 | p8[3];
    if (b & 4)
        bits <<= 4;
    if (b & 2)
        bits <<= 2;
    if (b & 1)
        bits <<= 1;
    bits &= MANY_COLS(firstSx) ? COLS_MASK(SPAN_COLS + 1) : COLS_MASK(SPAN_COLS);

    for (unsigned sx = firstSx; bits != 0; bits <<= 1, sx += TILE_SIZE) {
        if ((int32_t)bits >= 0)
            continue;
        unsigned col = b + ((sx - firstSx) >> TILE_SHIFT);
        while (col >= planeA->widthTiles)
            col -= planeA->widthTiles;
        if (kind == RUN_COPY_A)
            fromA += sx - start;
        else if (kind == RUN_COPY_B)
            fromB += sx - start;
        run->end = (uint16_t)sx;
        run->kind = (uint16_t)kind;
        run++;
        start = sx;
        kind = KIND_OF(rowClasses[pa.row[col]] & classMask);
    }
#undef KIND_OF
    if (kind == RUN_COPY_A)
        fromA += LINE_SPAN - start;
    else if (kind == RUN_COPY_B)
        fromB += LINE_SPAN - start;
    run->end = LINE_SPAN; /* la dernière tile est coupée */
    run->kind = (uint16_t)kind;
    const Run *const lastRun = run;

    /* 2. Décalage d'écran (SFT) de cette ligne. Sans décalage, le pixel x
          de l'écran est le pixel x de la ligne, qui montre la colonne
          scrollAX + x de A. Avec décalage, l'écran montre le pixel x + 1
          de la ligne : on la dessine depuis la colonne scrollAX - 1, sur
          322 pixels (161 mots). La parité des deux plans s'inverse : on
          choisit le décalage qui rend pair le plan le plus coûteux en
          source impaire (un A impair coûte aussi ses mots de frontière). */
    /* Largeurs multiples de TILE_SIZE, donc paires : ramener un scroll
       dans le plan ne change pas sa parité. */
#if HYPO_BUS32
    /* Hypothèse bus32 : décalage de 0 à 3. Avec le décalage s, la ligne
       part des colonnes scrollAX - s de A et scrollBX - s de B. Pour
       chaque plan, selon sa colonne de départ modulo 4 : 0, ses tiles
       entières s'écrivent en mots de 32 bits alignés (rien de plus) ;
       2, chaque mot de 32 bits est recomposé à cheval sur deux mots
       lus ; impaire, la copie depuis une source impaire et, pour A, les
       mots de frontière. On garde le décalage le moins cher, le plus
       petit à égalité (une ligne non décalée fait 256 pixels, 260
       sinon). */
    const unsigned costA = fromA / 2 * ODD_WORD_EXTRA +
                           (unsigned)(lastRun - runs) * BOUNDARY_WORD_EXTRA;
    const unsigned costB = fromB / 2 * ODD_WORD_EXTRA;
    const unsigned halfA = fromA / 2 * HALF_WORD_EXTRA;
    const unsigned halfB = fromB / 2 * HALF_WORD_EXTRA;
    int shift = 0;
    unsigned best = ~0u;
    for (int s = 0; s <= MAX_SHIFT; s++) {
        const unsigned ra = (unsigned)(scrollAX - s) & 3;
        const unsigned rb = (unsigned)(scrollBX - s) & 3;
        const unsigned c = (ra & 1 ? costA : ra ? halfA : 0) +
                           (rb & 1 ? costB : rb ? halfB : 0);
        if (c < best) {
            best = c;
            shift = s;
        }
    }
    const unsigned width = shift ? FB_WIDTH + FB_LINE_EXTRA : FB_WIDTH;
#else
    const unsigned evenA = !(scrollAX & 1);
    const unsigned evenB = !(scrollBX & 1);
    const unsigned costA = fromA / 2 * ODD_WORD_EXTRA +
                           (unsigned)(lastRun - runs) * BOUNDARY_WORD_EXTRA;
    const unsigned costB = fromB / 2 * ODD_WORD_EXTRA;
    const unsigned costNoShift = (evenA ? 0 : costA) + (evenB ? 0 : costB);
    const unsigned costShift = (evenA ? costA : 0) + (evenB ? costB : 0);
    const int shift = costShift < costNoShift;
    const unsigned width = FB_WIDTH + 2 * (unsigned)shift;
#endif

    /* Ligne dessinée : 'width' pixels ; son pixel 0 est le pixel 'skip'
       des segments, et montre la colonne xa0 de A et xb0 de B. */
    const unsigned skip = MAX_SHIFT - (unsigned)shift;
    const unsigned xa0 = advance(xs, skip, pa.width);
    const unsigned xb0 = wrapIn(scrollBX - shift, pb.width);

    /* Une décision de parité par plan, pour toute la ligne */
    const CopySpan copyA = (xa0 & 1) ? copySpanOdd : copySpanEven;
    const CopySpan copyB = (xb0 & 1) ? copySpanOdd : copySpanEven;

    /* 3. Dessin, mot par mot. 'p' est la prochaine colonne de la ligne à
          écrire, toujours paire. Un segment qui commence à une colonne
          impaire a déjà eu son premier pixel écrit avec le mot de
          frontière précédent. */
#if HYPO_BUS32
    LineOut out;
#if defined(__sh__)
    out.p = (volatile uint32_t *)(VDP_DRAM_ADDRESS + offset);
#else
    out.off = offset;
#endif
    out.hi = 0;
    out.half = 0;
#define LINE_DEST(o) lineDest(&out, (o))
#else
#define LINE_DEST(o) DEST(o)
#endif

    unsigned p = 0;
    for (run = runs; run <= lastRun; run++) {
#if MAX_SHIFT > 1
        /* Les premiers segments peuvent finir avant le pixel 'skip' */
        unsigned runEnd = run->end > skip ? (unsigned)run->end - skip : 0;
#else
        unsigned runEnd = (unsigned)run->end - skip;
#endif
        if (runEnd > width)
            runEnd = width;
        const unsigned end = runEnd & ~1u; /* mots entiers */

        if (end > p) {
            const unsigned count = end - p;
            COST_ADD(cost, runs, 1);

            /* Colonne de départ calculée pour le seul plan lu */
            if (run->kind == RUN_COPY_B) {
                copyB(LINE_DEST(offset + p), &pb, advance(xb0, p, pb.width), count,
                      cost);
            } else {
                const unsigned xa = advance(xa0, p, pa.width);
                if (run->kind == RUN_COPY_A)
                    copyA(LINE_DEST(offset + p), &pa, xa, count, cost);
                else
                    MERGE_SPAN(LINE_DEST(offset + p), &pa, xa, &pb,
                               advance(xb0, p, pb.width), count, cost);
            }
            p = end;
        }

        /* Frontière à une colonne impaire : le mot (end, end + 1) est à
           cheval sur ce segment et le suivant. */
        if (runEnd & 1) {
            COST_ADD(cost, runs, 1);
            mergeWord(LINE_DEST(offset + p), &pa, xa0, &pb, xb0, p, cost);
            p += 2;
        }
    }
#if HYPO_BUS32
    lineEnd(&out, offset + width);
#endif
#undef LINE_DEST
    return shift;
}
