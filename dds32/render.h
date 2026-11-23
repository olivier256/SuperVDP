#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#include "assets_types.h"
#include "cost.h"
#include "zone.h"

/* =========================================================================
   DDS 32 : dessin d'une ligne d'écran.

   1. Le décor : la ligne de la zone sous la caméra, recopiée depuis les
      triangles de la mosaïque (voir zone.h, iso.h). Chaque pixel est écrit
      une fois, par mots entiers, sans transparence : dans chaque colonne
      de 16 pixels, la ligne est partagée entre deux triangles, et leurs
      bords sont pairs. Un triangle vide donne le fond.
   2. L'objet mobile, s'il passe sur cette ligne : sa tranche par la zone
      overwrite (sprite.c), puis, par-dessus, les morceaux des triangles
      du décor qui sont devant lui (zone_voxel_after_object). Le décor
      cache l'objet au triangle près, sans bit de priorité.

   La ligne commence toujours à une abscisse paire de la zone : si la
   caméra est à une abscisse impaire, l'affichage est décalé d'un pixel
   (SFT ; render_shift).
   ========================================================================= */

typedef struct {
    const Zone *zone;
    int zx0;       /* abscisse de la zone au début de la ligne (paire) */
    int camY;      /* ligne de la zone en haut de l'écran */
    uint16_t fill; /* deux pixels du fond */
    const Sprite *sprite; /* objet mobile (NULL : aucun) */
    int u, v, w;          /* sa position (unités du monde) */
    int ox, oy;           /* coin haut gauche de son sprite dans la zone */
} RenderView;

void render_view(RenderView *rv, const Zone *zone, int cameraX, int cameraY,
                 uint8_t background, const Sprite *sprite, int u, int v, int w);

/* 1 si l'affichage doit être décalé d'un pixel (SFT) */
static inline int render_shift(int cameraX) { return cameraX & 1; }

/* Dessine la ligne y de l'écran (FB_PITCH octets) */
void render_line(const RenderView *rv, int y, DrawCost *cost);

#endif
