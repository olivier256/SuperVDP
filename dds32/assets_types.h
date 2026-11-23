#ifndef ASSETS_TYPES_H
#define ASSETS_TYPES_H

#include <stdint.h>

/* =========================================================================
   Types des données graphiques, telles que les produit tools/bmp2c.

   Tous les pixels sont des index de palette sur 8 bits (mode Packed Pixel
   du VDP 32X). L'index 0 est transparent : c'est du logiciel (la zone
   overwrite du VDP ne l'écrit pas), sans rapport avec le bit 15 des
   couleurs de la palette (priorité par rapport à l'écran Mega Drive),
   qui reste à 0.
   ========================================================================= */

/* Sprite : width x height pixels, ligne par ligne */
typedef struct {
    uint16_t width;
    uint16_t height;
    const uint8_t *pixels;
} Sprite;

/* Palette : 256 couleurs au format du VDP 32X, 0bbbbbgggggrrrrr */
#define PALETTE_COLORS 256

#endif
