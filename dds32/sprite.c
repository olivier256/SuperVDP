/* =========================================================================
   Tranche de sprite par la zone overwrite (voir sprite.h).

   La tranche va de l'abscisse x0 à x1 (exclue), une fois coupée. Ses
   octets vont de a0 = lineOffset + x0 à a1 = lineOffset + x1 dans le
   frame buffer. Selon la parité de a0 et de a1 :

        a0 impair         mots entiers            a1 impair
     [ 00 | p0 ]  [ p1 | p2 ] ... [ pn-2 | pn-1 ]  [ pn | 00 ]

   Le premier et le dernier mot peuvent n'avoir qu'un pixel de la
   tranche : l'autre octet est mis à 0, donc conservé par la zone
   overwrite. Ce pixel voisin est toujours dans le rectangle (ses bords
   sont pairs) : il n'est jamais hors de la ligne.

   Le sprite est lu octet par octet : sa position à l'écran est
   quelconque, et le SH2 ne lit pas un mot à une adresse impaire. C'est
   la version simple, mesurée avant toute optimisation (sprite rangé
   deux fois, pair et impair, pour des lectures MOV.W ; bornes opaques de
   chaque ligne ; DMA).
   ========================================================================= */

#include "sprite.h"

#include "vdp.h"

void sprite_draw_row(uint32_t lineOffset, const Sprite *sprite, int row, int sx,
                     int clipX0, int clipX1, DrawCost *cost) {
    int x0 = sx < clipX0 ? clipX0 : sx;
    int x1 = sx + sprite->width;
    if (x1 > clipX1)
        x1 = clipX1;
    if (x0 >= x1)
        return;
    COST_ADD(cost, rows, 1);

    /* src[i] : pixel de l'abscisse x0 + i */
    const uint8_t *src = sprite->pixels + row * sprite->width + (x0 - sx);
    uint32_t a = lineOffset + (uint32_t)x0;
    const uint32_t end = lineOffset + (uint32_t)x1;

    /* Premier pixel seul, à une adresse impaire : mot [00 | p0] */
    if (a & 1) {
        COST_ADD(cost, edges, 1);
        const uint8_t p = *src++;
        if (p != 0)
            vdp_overwrite_write16(a - 1, p);
        a++;
    }

    /* Mots entiers */
    for (; a + 2 <= end; a += 2, src += 2) {
        COST_ADD(cost, words, 1);
        const uint16_t pixels = (uint16_t)((src[0] << 8) | src[1]);
        if (pixels != 0)
            vdp_overwrite_write16(a, pixels);
    }

    /* Dernier pixel seul : mot [pn | 00] */
    if (a < end) {
        COST_ADD(cost, edges, 1);
        const uint8_t p = *src;
        if (p != 0)
            vdp_overwrite_write16(a, (uint16_t)(p << 8));
    }
}
