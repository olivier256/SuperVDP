/* =========================================================================
   DDS 32 : dessin d'une ligne d'écran (voir render.h).

   Dans chaque colonne de 16 pixels, la ligne y de la zone traverse deux
   triangles : (c, t) à sa rangée r et (c, t - 1) à sa rangée r + 8
   (t = y / 8, r = y % 8). Leurs rangées sont rangées sur 16 octets, chaque
   pixel à sa place dans la colonne et des 0 ailleurs, et leurs parts sont
   complémentaires (zone.h) : la colonne est le OU des deux rangées. Une
   colonne coûte donc deux lectures dans la mosaïque, puis quatre fois :
   deux lectures 32 bits, un OU, deux écritures 16 bits dans le frame
   buffer. Aucun test : le fond est un triangle comme les autres.

   Les colonnes entières de la zone passent par cette boucle ; celles des
   bords (coupées par l'écran, ou hors de la zone) par un chemin général.

   L'objet mobile : sa tranche par la zone overwrite (sprite.c), puis les
   rangées des triangles du décor qui sont devant lui, elles aussi par la
   zone overwrite : leurs 0 (hors du triangle) n'effacent rien.
   ========================================================================= */

#include "render.h"

#include "bigendian.h"
#include "framebuffer.h"
#include "iso.h"
#include "sprite.h"
#include "vdp.h"

void render_view(RenderView *rv, const Zone *zone, int cameraX, int cameraY,
                 uint8_t background, const Sprite *sprite, int u, int v,
                 int w) {
    (void)background; /* le fond est dans les triangles 0 et 1 */
    rv->zone = zone;
    rv->zx0 = cameraX & ~1;
    rv->camY = cameraY;
    rv->sprite = sprite;
    rv->u = u;
    rv->v = v;
    rv->w = w;
    rv->ox = zone->originX + (u - v);
    rv->oy = zone->originY + ((u + v) >> 1) - w;
}

/* Rangée de 16 octets du fond, pour ce qui est hors de la zone */
static const uint8_t *fillRow(const Zone *z) {
    /* rangée 7 des triangles 0 et 1 : 16 pixels de fond à eux deux ;
       on prend le triangle 0 à la rangée 7 (16 pixels) */
    return zone_triangle_row(z, 0, 7);
}

/* La colonne c, octets [k0, k1[ (pairs) : OU des rangées a et b */
static inline void columnPart(uint32_t dst, const uint8_t *a, const uint8_t *b,
                              int k0, int k1, DrawCost *cost) {
    const uint16_t *pa = (const uint16_t *)(a + k0);
    const uint16_t *pb = (const uint16_t *)(b + k0);
    for (int k = k0; k < k1; k += 2) {
        COST_ADD(cost, copies, 1);
        vdp_dram_write16(dst + (uint32_t)k, BE16((uint16_t)(*pa++ | *pb++)));
    }
}

/* Les colonnes entières [c0, c1[ : rangées A (t) et B (t - 1) */
static void fullColumns(const uint16_t *ma, const uint16_t *mb,
                        const uint8_t *rowsA, const uint8_t *rowsB, int c0,
                        int c1, uint32_t dst, DrawCost *cost) {
    for (int c = c0; c < c1; c++, dst += ISO_TRI_W) {
        COST_ADD(cost, columns, 1);
        const uint32_t *a = (const uint32_t *)(rowsA + ((uint32_t)ma[c] << 4));
        const uint32_t *b = (const uint32_t *)(rowsB + ((uint32_t)mb[c] << 4));
        for (int i = 0; i < 4; i++) {
            const uint32_t px = BE32(a[i] | b[i]);
            vdp_dram_write16(dst + 4 * (uint32_t)i, (uint16_t)(px >> 16));
            vdp_dram_write16(dst + 4 * (uint32_t)i + 2, (uint16_t)px);
        }
    }
}

void render_line(const RenderView *rv, int y, DrawCost *cost) {
    const Zone *z = rv->zone;
    const uint32_t line = fb_offset(0, y);
    const int x0 = rv->zx0, x1 = rv->zx0 + FB_PITCH;
    const int zy = rv->camY + y;
    const uint8_t *fill = fillRow(z);
    COST_ADD(cost, lines, 1);

    const int inside = zy >= 0 && zy < z->pixelHeight;
    const int t = zy >> 3, r = zy & 7;
    const uint16_t *ma = z->mosaic + t * z->cols;       /* (c, t) */
    const uint16_t *mb = z->mosaic + (t - 1) * z->cols; /* (c, t - 1) */
    const uint8_t *rowsA = zone_triangle_row(z, 0, r);
    const uint8_t *rowsB = zone_triangle_row(z, 0, r + 8);

    /* Le décor */
    int cFull0 = (x0 + ISO_TRI_W - 1) >> 4, cFull1 = x1 >> 4;
    if (cFull0 < 0)
        cFull0 = 0;
    if (cFull1 > z->cols)
        cFull1 = z->cols;
    if (!inside || cFull1 < cFull0)
        cFull0 = cFull1 = x1 >> 4; /* tout par le chemin général */
    for (int c = x0 >> 4; ISO_TRI_W * c < x1; c++) {
        if (c == cFull0 && cFull0 < cFull1) {
            fullColumns(ma, mb, rowsA, rowsB, cFull0, cFull1,
                        line + (uint32_t)(ISO_TRI_W * cFull0 - x0), cost);
            c = cFull1 - 1;
            continue;
        }
        COST_ADD(cost, partials, 1);
        int k0 = x0 - ISO_TRI_W * c, k1 = x1 - ISO_TRI_W * c;
        if (k0 < 0)
            k0 = 0;
        if (k1 > ISO_TRI_W)
            k1 = ISO_TRI_W;
        const uint32_t dst = line + (uint32_t)(ISO_TRI_W * c - x0);
        if (!inside || c < 0 || c >= z->cols) {
            static const uint8_t zeros[ISO_TRI_W] __attribute__((aligned(4)));
            columnPart(dst, fill, zeros, k0, k1, cost);
        } else
            columnPart(dst, rowsA + ((uint32_t)ma[c] << 4),
                       rowsB + ((uint32_t)mb[c] << 4), k0, k1, cost);
    }

    /* L'objet mobile */
    if (rv->sprite == 0)
        return;
    const int row = zy - rv->oy;
    if (row < 0 || row >= rv->sprite->height)
        return;
    sprite_draw_row(line, rv->sprite, row, rv->ox - x0, 0, FB_PITCH, cost);
    if (!inside)
        return;

    /* Le décor qui est devant lui, sur sa largeur (bornes paires), par la
       zone overwrite : chaque rangée n'écrit que les pixels de son
       triangle */
    int a0 = rv->ox & ~1, a1 = (rv->ox + rv->sprite->width + 1) & ~1;
    if (a0 < x0)
        a0 = x0;
    if (a1 > x1)
        a1 = x1;
    const uint16_t *depthA = z->depth + t * z->cols;
    const uint16_t *depthB = z->depth + (t - 1) * z->cols;
    for (int c = a0 >> 4; ISO_TRI_W * c < a1; c++) {
        if (c < 0 || c >= z->cols)
            continue;
        int k0 = a0 - ISO_TRI_W * c, k1 = a1 - ISO_TRI_W * c;
        if (k0 < 0)
            k0 = 0;
        if (k1 > ISO_TRI_W)
            k1 = ISO_TRI_W;
        const uint32_t dst = line + (uint32_t)(ISO_TRI_W * c - x0);
        for (int which = 0; which < 2; which++) {
            COST_ADD(cost, fronts, 1);
            const uint16_t d = (which ? depthB : depthA)[c];
            if (!zone_voxel_after_object(d, rv->u, rv->v, rv->w))
                continue;
            COST_ADD(cost, segments, 1);
            const uint16_t *src =
                (const uint16_t *)((which ? rowsB : rowsA) +
                                   ((uint32_t)(which ? mb : ma)[c] << 4) + k0);
            for (int k = k0; k < k1; k += 2, src++) {
                COST_ADD(cost, copies, 1);
                const uint16_t px = BE16(*src);
                if (px != 0)
                    vdp_overwrite_write16(dst + (uint32_t)k, px);
            }
        }
    }
}
