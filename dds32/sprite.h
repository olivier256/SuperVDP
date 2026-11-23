#ifndef SPRITE_H
#define SPRITE_H

#include <stdint.h>

#include "assets_types.h"
#include "cost.h"

/* =========================================================================
   Dessin d'une tranche de sprite (une de ses lignes) par la zone
   overwrite du VDP (2402 0000h) : les octets nuls ne sont pas écrits, ce
   qui est déjà dessiné reste visible autour du sprite, sans masque ni
   lecture du frame buffer.

   Règles d'écriture :
   - uniquement des écritures 16 bits, à des adresses paires ;
   - un mot dont les deux pixels sont transparents n'est pas écrit du tout
     (la zone overwrite ne fait pas gagner de temps : un octet filtré
     coûte quand même l'accès au bus) ;
   - rien n'est écrit hors de [clipX0, clipX1[ : le sprite est coupé au
     pixel près. clipX0 et clipX1 sont pairs (rectangles du moteur).
   ========================================================================= */

/* Dessine la ligne 'row' du sprite, son pixel 0 à l'abscisse 'sx' de
   l'écran (quelconque), coupée à [clipX0, clipX1[. 'lineOffset' : offset
   du pixel 0 de la ligne d'écran, fb_offset(0, y). */
void sprite_draw_row(uint32_t lineOffset, const Sprite *sprite, int row, int sx,
                     int clipX0, int clipX1, DrawCost *cost);

#endif
