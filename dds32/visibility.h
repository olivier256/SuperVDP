#ifndef VISIBILITY_H
#define VISIBILITY_H

#include <stdint.h>

/* =========================================================================
   DDS 32 : quels triangles de l'image d'une zone montrent quel morceau de
   face. C portable : le convertisseur (tools/dds32conv) s'en sert pour
   toute la zone ; les SH2 s'en serviront plus tard pour une petite région
   quand un voxel change en cours de jeu.

   Méthode : les voxels sont parcourus de l'arrière vers l'avant (par
   i + j + k, puis par k), et chacun écrit ses six triangles dans la
   mosaïque, sauf ceux d'une face cachée par un voisin plein (dessus sous
   un voxel, face gauche contre le voxel j + 1, face droite contre le
   voxel i + 1). Ce qui est écrit plus tard est plus proche : à la fin,
   chaque triangle donne la face visible. Pour des cubes de la grille,
   c'est exact (vérifié contre un lancer de rayons, tests/test_visibility).
   ========================================================================= */

typedef struct {
    int width, depthCells, height; /* W, D, H */
    const uint8_t *voxels;         /* voir zone.h */
    int cols, rows;                /* triangles de l'image */
    int originX, originY;          /* voir zone.h */
    /* Pour chaque type de cube (1 à 255) : le triangle de pixels (numéro
       + 1, 0 = rien) de chacun de ses six emplacements (iso_cube_slots) */
    const uint16_t (*typeSlots)[6];
} VisibilityInput;

/* Remplit mosaic et depth (cols x rows chacun) */
void visibility_build(const VisibilityInput *in, uint16_t *mosaic,
                      uint16_t *depth);

#endif
