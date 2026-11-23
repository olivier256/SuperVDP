#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

#include "vdp.h"

/* =========================================================================
   Frame buffer du VDP 32X en mode Packed Pixel (un octet par pixel).

   La DRAM commence par la table de lignes : un mot par ligne affichée,
   qui donne l'adresse (en mots) des pixels de la ligne. On réserve
   256 mots (512 octets), comme le 32X Hardware Manual, puis les lignes se
   suivent, de FB_PITCH octets chacune : 322 pixels (161 mots), pour
   qu'une ligne affichée avec le décalage d'écran (SFT, 321 pixels lus)
   ne déborde pas sur la suivante. 224 lignes : 72 640 octets sur 128 Ko.

   Le décalage d'écran sert aux positions impaires de la caméra : le
   moteur dessine toujours à partir d'une abscisse paire de la zone (des
   mots entiers), et l'affichage commence un pixel plus loin.
   ========================================================================= */

#define FB_WIDTH VDP_SCREEN_WIDTH
#define FB_PITCH (VDP_SCREEN_WIDTH + 2)
#define FB_LINE_TABLE_BYTES 512

/* Remplit la table de lignes du frame buffer de dessin */
void fb_init_line_table(int screenHeight);

/* Toutes les écritures du moteur dans le frame buffer passent par
   vdp_dram_write*() ou vdp_overwrite_write*(), pour être comptées par le
   simulateur (voir vdp.h). Seule exception : la table de lignes, à
   l'initialisation (voir framebuffer.c). */

/* Position du pixel (x, y) en octets depuis le début du frame buffer de
   dessin : c'est l'offset attendu par vdp_dram_write*() et
   vdp_overwrite_write*(). */
static inline uint32_t fb_offset(int x, int y) {
    return (uint32_t)(FB_LINE_TABLE_BYTES + y * FB_PITCH + x);
}

#endif
